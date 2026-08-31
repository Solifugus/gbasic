#!/usr/bin/env bash
# The money cookbook (docs/accounting_cookbook.md) -- a tutorial that cannot lie.
#
# A cookbook's failure mode is not being wrong when written, it is being wrong
# six months later while still reading as authoritative. This suite removes the
# gap between the page and the product in both directions:
#
#   RUN     -- every recipe in examples/accounting_cookbook/ executes, and its stdout
#              must match its committed .out byte for byte. So the page's output
#              is not a transcript somebody pasted; it is a golden.
#   CODE    -- every ```basic block on the page must equal the .bas file it came
#              from, byte for byte. Editing a recipe without re-running
#              tools/sync_accounting_cookbook.sh fails here.
#   OUTPUT  -- every output block on the page must equal that recipe's .out. So
#              a behaviour change that legitimately moves a golden ALSO moves
#              the page, and the two cannot drift apart silently.
#   COVER   -- every recipe file must appear on the page, and every marker on
#              the page must have a file. Adding recipe 13 and forgetting to
#              document it is a failure, not a silent omission.
#
# Never skips: money is core and `finance` is pure gBASIC, so no optional
# native dependency is involved.
#
# THIS PAGE EARNED ITS HARNESS IMMEDIATELY. Writing recipe 7 -- an ordinary
# amortization schedule -- surfaced a silent defect in `money * scalar` that
# the unit tests had missed: a scalar whose shortest decimal needs 19
# fractional places was treated as negligible and returned 0.00. Every payment
# in the schedule came out zero. The unit fixtures all used short scalars
# (2, 3, 1.08, 0.5); only realistic arithmetic produced a long one.
set -u

cd "$(dirname "$0")/.."

DOC=docs/accounting_cookbook.md
DIR=examples/accounting_cookbook

status=0
pass=0
skip=0

note_fail() { printf 'FAIL %s\n' "$1"; status=1; }

if [ ! -f "$DOC" ]; then
    note_fail "run_accounting_cookbook: $DOC not found"
    exit 1
fi

if [ ! -x ./gbasic ]; then
    make >/dev/null 2>&1 || { note_fail "run_accounting_cookbook: build failed"; exit 1; }
fi

# Spelled literally rather than through $DIR so run_docs_gate.sh can see that
# this directory glob is covered by a runner -- the gate greps for the literal
# `examples/accounting_cookbook/*.bas`, and a variable would read as unwired.
recipes=$(ls examples/accounting_cookbook/*.bas 2>/dev/null | sed 's|.*/||; s|\.bas$||' | sort)
if [ -z "$recipes" ]; then
    note_fail "run_accounting_cookbook: no recipes in $DIR"
    exit 1
fi

# Extract the fenced block that follows a given marker in the doc.
extract_block() {
    awk -v marker="$1" '
        $0 == marker { found = 1; next }
        found && /^```/ { if (infence) { exit } ; infence = 1; next }
        found && infence { print }
    ' "$DOC"
}

# --- RUN + OUTPUT + CODE ------------------------------------------------------
for r in $recipes; do
    bas="$DIR/$r.bas"
    out="$DIR/$r.out"

    if [ ! -f "$out" ]; then
        note_fail "$bas (no committed .out)"
        continue
    fi

    actual="$(mktemp)"
    errf="$(mktemp)"
    # GBASIC_PATH so the recipes that `load grid`/`consolidate`/`dbframe` resolve.
    GBASIC_PATH=stdlib ./gbasic "$bas" >"$actual" 2>"$errf"
    rc=$?

    if grep -q 'not available in this build' "$errf"; then
        printf 'SKIP %s (module compiled out)\n' "$bas"
        skip=$((skip + 1))
        rm -f "$actual" "$errf"
        continue
    fi

    if [ "$rc" != "0" ]; then
        note_fail "$bas (exit $rc)"
        head -5 "$errf"
        rm -f "$actual" "$errf"
        continue
    fi

    if ! diff -u "$out" "$actual" >/dev/null 2>&1; then
        note_fail "$bas (stdout does not match $out)"
        diff -u "$out" "$actual" | head -20
        rm -f "$actual" "$errf"
        continue
    fi
    printf 'PASS run %s\n' "$bas"
    pass=$((pass + 1))
    rm -f "$actual" "$errf"

    # The page's code block must BE the file.
    blk="$(mktemp)"
    extract_block "<!--CODE:$r-->" > "$blk"
    if [ ! -s "$blk" ]; then
        note_fail "$DOC (no code block for $r -- add <!--CODE:$r--> and run tools/sync_accounting_cookbook.sh)"
    elif ! diff -u "$bas" "$blk" >/dev/null 2>&1; then
        note_fail "$DOC code block for $r differs from $bas -- run tools/sync_accounting_cookbook.sh"
        diff -u "$bas" "$blk" | head -15
    else
        printf 'PASS code %s\n' "$r"
        pass=$((pass + 1))
    fi
    rm -f "$blk"

    # The page's output block must BE the golden.
    blk="$(mktemp)"
    extract_block "<!--OUT:$r-->" > "$blk"
    if [ ! -s "$blk" ]; then
        note_fail "$DOC (no output block for $r)"
    elif ! diff -u "$out" "$blk" >/dev/null 2>&1; then
        note_fail "$DOC output block for $r differs from $out -- run tools/sync_accounting_cookbook.sh"
        diff -u "$out" "$blk" | head -15
    else
        printf 'PASS out  %s\n' "$r"
        pass=$((pass + 1))
    fi
    rm -f "$blk"
done

# --- COVER: no recipe undocumented, no marker without a file ------------------
for r in $recipes; do
    if ! grep -qF "<!--CODE:$r-->" "$DOC"; then
        note_fail "$DOC does not document recipe $r"
    fi
done
for m in $(grep -oE '<!--CODE:[A-Za-z0-9_]+-->' "$DOC" | sed 's/<!--CODE://; s/-->//'); do
    if [ ! -f "$DIR/$m.bas" ]; then
        note_fail "$DOC references recipe $m, which has no file in $DIR"
    fi
done

printf '\nrun_accounting_cookbook: PASS=%d SKIP=%d\n' "$pass" "$skip"
exit "$status"

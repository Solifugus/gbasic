# Shared harness for the cookbook suites. Sourced, never run directly.
#
# WHY THIS EXISTS. Every cookbook suite asserts the SAME four things, and it
# asserted them in eight copies of the same 130 lines. Five of the eight were
# byte-identical once the cookbook's name was substituted out; the other three
# differed only in when they skip. That is the shape a parameterised harness is
# for, and the project had already made this move ONE LEVEL OVER --
# tools/sync_xlsx_cookbook.sh is parameterised and every other sync tool calls
# it. This finishes the job on the checking side.
#
# It is not a line count that makes it worth doing. It is that a fix to the
# harness used to have to be applied eight times, and the two runners that had
# already drifted (a different SKIP wording, an extra stderr pattern) are the
# evidence that it would not have been.
#
# THE FOUR TIERS, unchanged:
#
#   RUN     -- every recipe executes and its stdout matches its committed .out
#              byte for byte. The page's output is a golden, not a transcript.
#   CODE    -- every ```basic block on the page equals the .bas it came from.
#              Editing a recipe without re-running the sync tool fails here.
#   OUTPUT  -- every output block equals that recipe's .out, so a behaviour
#              change that legitimately moves a golden ALSO moves the page and
#              the two cannot drift apart silently.
#   COVER   -- every recipe is documented and every marker has a file, so
#              adding recipe 13 and forgetting to write it up fails rather than
#              passing quietly.
#
# CONTRACT. Set before sourcing:
#
#   COOKBOOK      the name: money, xlsx, ... (DOC and DIR derive from it)
#   RECIPE_GLOB   the glob, spelled LITERALLY -- e.g.
#                 examples/money_cookbook/*.bas. It has to be a literal in a
#                 tests/*.sh file because run_docs_gate.sh greps for exactly
#                 that string to decide the directory is wired into a runner;
#                 a variable would read as unwired.
#
# Optional, defined before sourcing:
#
#   cookbook_precheck()        run first; print a `SKIP ...` line and `exit 0`
#                              to skip the whole suite (a missing native module,
#                              no display).
#   cookbook_recipe_skip R     return 0 to skip recipe R, having printed its own
#                              reason; return 1 to run it.
#   COOKBOOK_SKIP_PATTERN      an extra egrep pattern over a recipe's stderr
#                              meaning "this machine, not the cookbook".
#   COOKBOOK_SKIP_REASON       the parenthesised reason printed for it.
#   COOKBOOK_CLEANUP           files to remove on exit.

set -u

cd "$(dirname "${BASH_SOURCE[0]}")/.."

: "${COOKBOOK:?cookbook_harness: COOKBOOK must be set}"
: "${RECIPE_GLOB:?cookbook_harness: RECIPE_GLOB must be set}"

SUITE="run_${COOKBOOK}_cookbook"
DOC="docs/${COOKBOOK}_cookbook.md"
DIR="examples/${COOKBOOK}_cookbook"
SYNC="tools/sync_${COOKBOOK}_cookbook.sh"

status=0
pass=0
skip=0

note_fail() { printf 'FAIL %s\n' "$1"; status=1; }

if [ -n "${COOKBOOK_CLEANUP:-}" ]; then
    # shellcheck disable=SC2064
    trap "rm -f ${COOKBOOK_CLEANUP}" EXIT
fi

if [ ! -f "$DOC" ]; then
    note_fail "$SUITE: $DOC not found"
    exit 1
fi

if [ ! -x ./gbasic ]; then
    make >/dev/null 2>&1 || { note_fail "$SUITE: build failed"; exit 1; }
fi

# The whole-suite skip runs AFTER the build, because deciding whether a native
# module is in the build means asking the binary.
if declare -F cookbook_precheck >/dev/null 2>&1; then
    cookbook_precheck
fi

recipes=$(ls $RECIPE_GLOB 2>/dev/null | sed 's|.*/||; s|\.bas$||' | sort)
if [ -z "$recipes" ]; then
    note_fail "$SUITE: no recipes in $DIR"
    exit 1
fi

# The fenced block following a marker in the doc.
extract_block() {
    awk -v marker="$1" '
        $0 == marker { found = 1; next }
        found && /^```/ { if (infence) { exit } ; infence = 1; next }
        found && infence { print }
    ' "$DOC"
}

# --- RUN + CODE + OUTPUT -----------------------------------------------------
for r in $recipes; do
    bas="$DIR/$r.bas"
    out="$DIR/$r.out"

    if declare -F cookbook_recipe_skip >/dev/null 2>&1 && cookbook_recipe_skip "$r"; then
        skip=$((skip + 1))
        continue
    fi

    if [ ! -f "$out" ]; then
        note_fail "$bas (no committed .out)"
        continue
    fi

    actual="$(mktemp)"
    errf="$(mktemp)"
    # GBASIC_PATH so a recipe's `load` of a stdlib library resolves from the
    # source tree rather than an installed copy.
    GBASIC_PATH=stdlib ./gbasic "$bas" >"$actual" 2>"$errf"
    rc=$?

    if grep -q 'not available in this build' "$errf"; then
        printf 'SKIP %s (module compiled out)\n' "$bas"
        skip=$((skip + 1))
        rm -f "$actual" "$errf"
        continue
    fi

    # A second skip class, distinguished on purpose: the module is IN the
    # build but the machine cannot supply what it needs (no driver, no server).
    # Reporting that as a failure would report a fact about the machine as a
    # defect in the cookbook.
    if [ -n "${COOKBOOK_SKIP_PATTERN:-}" ] && grep -qE "$COOKBOOK_SKIP_PATTERN" "$errf"; then
        printf 'SKIP %s (%s)\n' "$bas" "${COOKBOOK_SKIP_REASON:-unavailable on this machine}"
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
        note_fail "$DOC (no code block for $r -- add <!--CODE:$r--> and run $SYNC)"
    elif ! diff -u "$bas" "$blk" >/dev/null 2>&1; then
        note_fail "$DOC code block for $r differs from $bas -- run $SYNC"
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
        note_fail "$DOC output block for $r differs from $out -- run $SYNC"
        diff -u "$out" "$blk" | head -15
    else
        printf 'PASS out  %s\n' "$r"
        pass=$((pass + 1))
    fi
    rm -f "$blk"
done

# --- COVER: no recipe undocumented, no marker without a file -----------------
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

printf '\n%s: PASS=%d SKIP=%d\n' "$SUITE" "$pass" "$skip"
exit "$status"

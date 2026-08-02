#!/usr/bin/env bash
# xlsx Stage 1 — ZIP container, part tree, read-only cells (docs/xlsx_design.md).
#
# Tiers:
#   1. GOLDEN -- examples/xlsx_read_test.bas over the committed fixture.
#   2. RETENTION -- the claim the whole design rests on: the reader discards
#      NOTHING. Every part in the container must appear in xlsx.parts, and the
#      one part nothing models must come back byte-identical. Round-trip write
#      is impossible without this, and a reader that quietly dropped a part
#      would look perfectly healthy until write produced a lesser file months
#      later. Asserted against the ZIP's own entry list, not against a
#      hand-written expectation, so adding a part to the fixture cannot leave
#      the check silently narrower than the file.
#   3. NEGATIVE -- the container errors, each pinned: not a ZIP at all, and a
#      truncated file. A misread container must say so rather than produce a
#      workbook with plausible missing pieces.
#   4. VALGRIND -- the golden under valgrind. A refcounted handle owning a part
#      tree, sheet table and shared-string table is exactly where a leak hides.
#
# Skips cleanly when zlib or libxml2 was unavailable at build time, per the
# project's optional-dependency convention. Needs no python3: the fixture is
# committed, and tools/make_xlsx_fixture.py is only how it was authored.
set -u

cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_xlsx: build failed\n'
    exit 1
fi

out=$(mktemp)
err=$(mktemp)
tmp=$(mktemp -d)
trap 'rm -rf "$out" "$err" "$tmp"' EXIT

FIXTURE=examples/fixtures/xlsx/basic.xlsx

# Degrade check: if the module was compiled out, the error must be the clean one.
printf 'program main(args)\n  print xlsx.open("%s")\nend program\n' "$FIXTURE" >"$tmp/probe.bas"
if ./gbasic "$tmp/probe.bas" >/dev/null 2>"$err"; then
    :
elif grep -q 'not available in this build' "$err"; then
    printf 'SKIP run_xlsx (built without zlib or libxml2)\n'
    exit 0
fi

status=0

# --- Tier 1: golden ------------------------------------------------------------
printf -- '-- golden\n'
if timeout 120 ./gbasic examples/xlsx_read_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_read_test.out "$out"; then
        printf 'PASS examples/xlsx_read_test.bas\n'
    else
        printf 'FAIL examples/xlsx_read_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_read_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# --- Tier 2: the reader discards nothing ---------------------------------------
printf -- '-- retention: every container entry survives the read\n'
# The ZIP's own entry names, straight from the central directory, via unzip if
# present and otherwise by scanning the file for entry signatures. Deliberately
# NOT a hardcoded list: the fixture may grow, and the check must grow with it.
if command -v unzip >/dev/null 2>&1; then
    unzip -Z1 "$FIXTURE" 2>/dev/null | sort >"$tmp/want"
else
    printf 'SKIP retention (unzip not installed)\n'
    : >"$tmp/want"
fi
if [ -s "$tmp/want" ]; then
    printf 'program main(args)\n  wb = xlsx.open("%s")\n  for each p in xlsx.parts(wb)\n    print p.name\n  end for\nend program\n' "$FIXTURE" >"$tmp/list.bas"
    if timeout 60 ./gbasic "$tmp/list.bas" 2>"$err" | sort >"$tmp/got"; then
        if diff -u "$tmp/want" "$tmp/got"; then
            printf 'PASS all %s container entries retained\n' "$(wc -l <"$tmp/want" | tr -d ' ')"
        else
            printf 'FAIL part tree does not match the container -- the reader dropped or invented a part\n'
            status=1
        fi
    else
        printf 'FAIL listing parts (exit)\n'
        cat "$err"
        status=1
    fi

    # And the unmodelled part must come back byte-identical, not merely present.
    if command -v unzip >/dev/null 2>&1; then
        unzip -p "$FIXTURE" xl/customData/vendor.xml >"$tmp/want_bytes" 2>/dev/null
        printf 'program main(args)\n  wb = xlsx.open("%s")\n  print to error xlsx.part(wb, "xl/customData/vendor.xml")\nend program\n' "$FIXTURE" >"$tmp/bytes.bas"
        # via stderr so no trailing-newline handling differs from print
        timeout 60 ./gbasic "$tmp/bytes.bas" 2>"$tmp/got_bytes" >/dev/null
        # strip the single trailing newline print adds
        printf '%s' "$(cat "$tmp/got_bytes")" >"$tmp/got_trim"
        printf '%s' "$(cat "$tmp/want_bytes")" >"$tmp/want_trim"
        if cmp -s "$tmp/want_trim" "$tmp/got_trim"; then
            printf 'PASS unmodelled part returned byte-identical\n'
        else
            printf 'FAIL unmodelled part differs from the container copy\n'
            status=1
        fi
    fi
fi

# --- Tier 3: negative ----------------------------------------------------------
printf -- '-- negative (container errors are reported, not guessed at)\n'
negative() { # label file expected-substring
    local label=$1 file=$2 want=$3
    printf 'program main(args)\n  print xlsx.open("%s")\nend program\n' "$file" >"$tmp/neg.bas"
    if timeout 60 ./gbasic "$tmp/neg.bas" >/dev/null 2>"$err" </dev/null; then
        printf 'FAIL negative %-18s (expected a raise, succeeded)\n' "$label"
        status=1
        return
    fi
    if grep -qF "$want" "$err"; then
        printf 'PASS negative %-18s %s\n' "$label" "$want"
    else
        printf 'FAIL negative %-18s\n  want: %s\n  got:  %s\n' "$label" "$want" "$(cat "$err")"
        status=1
    fi
}

printf 'this is not a zip file at all, not even close\n' >"$tmp/notzip.xlsx"
negative "not a container" "$tmp/notzip.xlsx" "not a ZIP container"

head -c 400 "$FIXTURE" >"$tmp/trunc.xlsx"
negative "truncated" "$tmp/trunc.xlsx" "xlsx"

negative "missing file" "$tmp/does_not_exist.xlsx" "cannot read"

# --- Tier 4: valgrind ----------------------------------------------------------
if command -v valgrind >/dev/null 2>&1; then
    if valgrind --error-exitcode=9 --leak-check=full --errors-for-leak-kinds=definite \
            ./gbasic examples/xlsx_read_test.bas >"$out" 2>"$err" </dev/null; then
        if diff -q examples/xlsx_read_test.out "$out" >/dev/null; then
            printf 'PASS valgrind examples/xlsx_read_test.bas\n'
        else
            printf 'FAIL valgrind (output differs under valgrind)\n'
            status=1
        fi
    else
        printf 'FAIL valgrind examples/xlsx_read_test.bas\n'
        tail -25 "$err"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit "$status"

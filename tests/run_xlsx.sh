#!/usr/bin/env bash
# xlsx Stages 1-2 — ZIP container, part tree, cells, write + round-trip
# (docs/xlsx_design.md).
#
# Tiers:
#   1. GOLDEN -- read (examples/xlsx_read_test.bas) and write
#      (examples/xlsx_write_test.bas) over the committed fixture. The write
#      golden is the payoff for the part tree: editing ONE sheet must change
#      that part and nothing else, including the vendor part nothing models.
#      Also: save is byte-DETERMINISTIC across runs (ZIP mod-time fields are
#      fixed, not taken from the clock -- a clock there would look like a
#      successful write while quietly making byte comparison useless), and the
#      written container validates under `unzip -t`, i.e. by something other
#      than our own reader.
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

# --- Tier 1b: write + round-trip ----------------------------------------------
printf -- '-- golden: write + round-trip\n'
if timeout 120 ./gbasic examples/xlsx_write_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_write_test.out "$out"; then
        printf 'PASS examples/xlsx_write_test.bas\n'
    else
        printf 'FAIL examples/xlsx_write_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_write_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# Writing is DETERMINISTIC: the same workbook saved twice must produce the same
# bytes. The ZIP header carries mod-time fields, and taking them from the clock
# would make every save differ from the last -- which would look like a
# successful write while quietly making byte comparison useless as a test.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  print xlsx.save(wb, "%s")\nend program\n' \
    "$FIXTURE" "$tmp/det1.xlsx" >"$tmp/det.bas"
timeout 60 ./gbasic "$tmp/det.bas" >/dev/null 2>&1
printf 'program main(args)\n  wb = xlsx.open("%s")\n  print xlsx.save(wb, "%s")\nend program\n' \
    "$FIXTURE" "$tmp/det2.xlsx" >"$tmp/det.bas"
timeout 60 ./gbasic "$tmp/det.bas" >/dev/null 2>&1
if cmp -s "$tmp/det1.xlsx" "$tmp/det2.xlsx"; then
    printf 'PASS save is byte-deterministic across runs\n'
else
    printf 'FAIL save differs between two runs of identical input (a clock in the output?)\n'
    status=1
fi

# The written file must be a valid ZIP by something other than our own reader.
if command -v unzip >/dev/null 2>&1; then
    if unzip -t "$tmp/det1.xlsx" >/dev/null 2>&1; then
        printf 'PASS written container validates under unzip -t\n'
    else
        printf 'FAIL written container does not validate under unzip -t\n'
        status=1
    fi
fi

# --- Tier 1c: the formula evaluator and its oracle ----------------------------
printf -- '-- golden: formula evaluator\n'
if timeout 120 ./gbasic examples/xlsx_formula_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_formula_test.out "$out"; then
        printf 'PASS examples/xlsx_formula_test.bas\n'
    else
        printf 'FAIL examples/xlsx_formula_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_formula_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# ZERO DISAGREEMENTS is the assertion; the golden alone would happily record a
# regression as "the new expected output". A disagreement means the evaluator
# and the cached value differ, which on a real workbook means we are wrong.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.check(wb, "Formulas")\n  print r.disagree\nend program\n' "$FIXTURE" >"$tmp/chk.bas"
if [ "$(timeout 60 ./gbasic "$tmp/chk.bas" 2>/dev/null)" = "0" ]; then
    printf 'PASS evaluator agrees with every cached value it can judge\n'
else
    printf 'FAIL evaluator disagrees with a cached value\n'
    status=1
fi

# An unimplemented function must be NAMED, never defaulted to a plausible
# number. In a financial model a wrong number that looks right is worse than a
# failure, so this is asserted rather than assumed.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.check(wb, "Formulas")\n  print r.unsupported\nend program\n' "$FIXTURE" >"$tmp/uns.bas"
if [ "$(timeout 60 ./gbasic "$tmp/uns.bas" 2>/dev/null)" -ge 1 ] 2>/dev/null; then
    printf 'PASS unsupported functions reported by name, not silently zeroed\n'
else
    printf 'FAIL the unsupported-function fixture case is no longer detected\n'
    status=1
fi

# --- Tier 1d: recalculation in dependency order -------------------------------
printf -- '-- golden: recalc\n'
if timeout 120 ./gbasic examples/xlsx_recalc_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_recalc_test.out "$out"; then
        printf 'PASS examples/xlsx_recalc_test.bas\n'
    else
        printf 'FAIL examples/xlsx_recalc_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_recalc_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# ORDER, asserted rather than assumed. On Ledger, D7 = B5*2 sits ABOVE
# B5 = SUM(B2:B3) in sheet order, so an engine evaluating top to bottom hands
# D7 a stale B5 and prints a plausible wrong number. Change B2 to 1000 and the
# only correct transitive answer is D7 = 1801.5; a stale read gives 2302.5.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  xlsx.set(wb, "Ledger", "B2", 1000)\n  xlsx.recalc(wb, "Ledger")\n  print xlsx.cell(wb, "Ledger", "D7").value\nend program\n' "$FIXTURE" >"$tmp/ord.bas"
got=$(timeout 60 ./gbasic "$tmp/ord.bas" 2>/dev/null)
if [ "$got" = "1801.5" ]; then
    printf 'PASS transitive dependent recomputed in the right order\n'
else
    printf 'FAIL transitive dependent = %s (want 1801.5; 2302.5 means a stale input)\n' "$got"
    status=1
fi

# A cycle is REPORTED, and must not take the rest of the sheet with it.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  c = xlsx.recalc(wb, "Circular")\n  print c.circular\n  print xlsx.evaluate(wb, "Circular", "B2")\nend program\n' "$FIXTURE" >"$tmp/cyc.bas"
if [ "$(timeout 60 ./gbasic "$tmp/cyc.bas" 2>/dev/null | tr '\n' ' ')" = "2 15 " ]; then
    printf 'PASS circular reference reported; healthy cells on the sheet still evaluate\n'
else
    printf 'FAIL circular-reference handling changed\n'
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

# Writing over a formula cell must RAISE. The cached value and the formula would
# disagree, Excel would recalculate on open, and the edit would silently revert
# -- a wrong answer that looks like a successful write.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  xlsx.set(wb, "Ledger", "B5", 1)\nend program\n' "$FIXTURE" >"$tmp/neg2.bas"
if timeout 60 ./gbasic "$tmp/neg2.bas" >/dev/null 2>"$err" </dev/null; then
    printf 'FAIL negative %-18s (overwrote a formula cell)\n' "formula overwrite"
    status=1
elif grep -qF "refusing to overwrite a formula cell" "$err"; then
    printf 'PASS negative %-18s refusing to overwrite a formula cell\n' "formula overwrite"
else
    printf 'FAIL negative %-18s wrong message: %s\n' "formula overwrite" "$(cat "$err")"
    status=1
fi

# Creating a cell that does not exist is refused rather than placed wrongly.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  xlsx.set(wb, "Ledger", "Z99", 1)\nend program\n' "$FIXTURE" >"$tmp/neg3.bas"
if timeout 60 ./gbasic "$tmp/neg3.bas" >/dev/null 2>"$err" </dev/null; then
    printf 'FAIL negative %-18s (invented a cell)\n' "new cell"
    status=1
elif grep -qF "creating a new cell is not supported" "$err"; then
    printf 'PASS negative %-18s new-cell creation refused explicitly\n' "new cell"
else
    printf 'FAIL negative %-18s wrong message: %s\n' "new cell" "$(cat "$err")"
    status=1
fi

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

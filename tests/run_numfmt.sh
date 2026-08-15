#!/usr/bin/env bash
# PLAT-NUMFMT: `print` shows the number the program actually holds.
#
# `print`, `string()` and `quote()` all render a number through one function
# (format_number in src/eval.c). It emitted `%g` -- SIX significant digits --
# so a program could compute a value it had no way to display:
#
#     265550.75      printed as   265551
#     23750.25       printed as   23750.2
#     46237.5674884  printed as   46237.6
#
# That is not a cosmetic gap. Every test written for the xlsx pipeline asserts
# by COMPARISON rather than by printed text because of it (see the run_dbframe
# and run_consolidate notes, and DOGFOOD 2026-08-12 (b)), and the NOW/TODAY tier
# in run_xlsx.sh splits a date serial into day and seconds-of-day for no reason
# other than that a whole serial would not survive being printed.
#
# It now emits the SHORTEST decimal that reads back as the same double.
#
# The tiers, and why these:
#
#   1. GOLDEN     -- tests/numfmt_test.bas against its .out. Pins SHORTNESS,
#      which round-trip alone does not: %.17g also round-trips and would render
#      0.1 as 0.10000000000000001.
#   2. ROUND-TRIP -- the fixture's own property tier, checked separately from
#      the golden: number(string(x)) must equal x for every value in a 1420-
#      value generated battery. This is the DEFINING property of the format, it
#      is checkable inside the language, and unlike a list of expected digit
#      strings it cannot rot -- it stays true however many digits any particular
#      value turns out to need.
#   3. ORACLE     -- an INDEPENDENT reimplementation of the rule, in awk, run
#      over ~3000 generated values. This is the tier that would catch a
#      minimality search that is self-consistently wrong: the golden records
#      whatever we print, and round-trip accepts any faithful rendering, so
#      neither can tell "shortest" from "merely correct". Honest about what it
#      shares: awk uses the same libc printf and strtod, so this checks the
#      SEARCH and the branch rule, not printf itself. What it would catch is a
#      wrong loop bound, an off-by-one in the precision, a buffer that truncates
#      a long value, or the integer branch applied at the wrong threshold.
#   4. WIDTH      -- the change made output LONGER, into fixed 32-byte buffers.
#      The widest possible rendering is asserted in the fixture, by length.
#   5. VALGRIND   -- the fixture, for the buffer question tier 4 reasons about.
#
# Headless, GI-independent, no python3. Never skips (bar valgrind).
set -u

cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_numfmt: build failed\n'
    exit 1
fi

out=$(mktemp)
err=$(mktemp)
work=$(mktemp -d)
trap 'rm -f "$out" "$err"; rm -rf "$work"' EXIT

status=0

# --- Tier 1: golden -----------------------------------------------------------
if timeout 300 ./gbasic tests/numfmt_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u tests/numfmt_test.out "$out"; then
        printf 'PASS tests/numfmt_test.bas (golden)\n'
    else
        printf 'FAIL tests/numfmt_test.bas -- number RENDERING moved\n'
        status=1
    fi
else
    printf 'FAIL tests/numfmt_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# --- Tier 2: the property, checked independently of the golden ----------------
# A golden diff would also catch these, but it would report them as "output
# moved" rather than as what they are. Named separately so the failure says
# which property broke.
if grep -q 'MISMATCH' "$out"; then
    printf 'FAIL rendering -- a check reported MISMATCH:\n'
    grep 'MISMATCH' "$out"
    status=1
fi
if grep -q 'ROUNDTRIP LOST\|^LOST ' "$out"; then
    printf 'FAIL round-trip -- number(string(x)) != x for:\n'
    grep 'ROUNDTRIP LOST\|^LOST ' "$out"
    status=1
fi
if grep -q 'roundtrip battery: .* 0 lost' "$out"; then
    printf 'PASS round-trip (%s)\n' "$(grep -o 'roundtrip battery: .*' "$out")"
else
    printf 'FAIL round-trip battery did not report 0 lost\n'
    grep 'roundtrip battery' "$out" || printf '  (battery line absent -- fixture did not reach it)\n'
    status=1
fi

# --- Tier 3: independent oracle -----------------------------------------------
# awk reimplements the rule from scratch: integer-valued and below 2^53 renders
# as %.0f in full, anything else as the shortest %.*g that reads back equal.
# Every line of the dump must match what awk would have produced.
if ! timeout 300 ./gbasic tests/numfmt_dump.bas >"$work/dump.txt" 2>"$err" </dev/null; then
    printf 'FAIL tests/numfmt_dump.bas (exit)\n'
    cat "$err"
    status=1
elif [ ! -s "$work/dump.txt" ]; then
    printf 'FAIL tests/numfmt_dump.bas produced no output\n'
    status=1
else
    awk -F'\t' '
    function shortest(v,   p, s) {
        # the rule, reimplemented: integers below 2^53 in full ...
        if (v == int(v) && (v < 0 ? -v : v) < 9007199254740992) {
            return sprintf("%.0f", v)
        }
        # ... everything else the shortest round-tripping decimal
        for (p = 1; p <= 17; p++) {
            s = sprintf("%.*g", p, v)
            if (s + 0 == v) return s
        }
        return sprintf("%.17g", v)
    }
    # Recompute the value from the recipe, in awk arithmetic, WITHOUT looking at
    # the rendering. Each case is one correctly-rounded IEEE primitive (or a
    # fixed sequence of them), so awk lands on the same double gBASIC did.
    function compute(recipe,   f, op, a, b, v, k) {
        f = split(recipe, t, " ")
        op = t[1]; a = t[2] + 0; b = t[3] + 0
        if (op == "div")   return a / b
        if (op == "ndiv")  return 0 - a / b
        if (op == "sqrt")  return sqrt(a)
        if (op == "recip") return 1 / a
        if (op == "harm")  { v = 0; for (k = 1; k <= a; k++) v = v + 1 / k; return v }
        if (op == "down7") { v = 1; for (k = 1; k <= a; k++) v = v / 7;     return v }
        if (op == "up7")   { v = 1; for (k = 1; k <= a; k++) v = v * 7;     return v }
        if (op == "lit")   return a
        unknown++
        return "NA"
    }
    {
        n++
        want = shortest(compute($1))
        if (want != $2) {
            bad++
            if (bad <= 10) printf "  %-22s gbasic [%s]  awk [%s]\n", $1, $2, want
        }
    }
    END {
        # An unrecognised recipe must not read as agreement -- it would silently
        # shrink the population to whatever the checker still understands.
        printf "%d %d %d\n", n, bad+0, unknown+0 > "/dev/stderr"
    }
    ' "$work/dump.txt" >"$work/oracle.txt" 2>"$work/oracle.count"
    read -r ocount obad ounknown <"$work/oracle.count"
    if [ "${ounknown:-1}" != "0" ]; then
        printf 'FAIL oracle -- %s recipes the checker does not understand (population silently shrunk)\n' \
               "$ounknown"
        status=1
    elif [ "${obad:-1}" = "0" ] && [ "${ocount:-0}" -gt 2000 ]; then
        printf 'PASS oracle (%s values recomputed independently in awk, all agree)\n' "$ocount"
    else
        printf 'FAIL oracle -- %s of %s values disagree with the independent implementation:\n' \
               "${obad:-?}" "${ocount:-?}"
        cat "$work/oracle.txt"
        status=1
    fi
fi

# --- Tier 4: width ------------------------------------------------------------
# format_number writes into char[32] at two of its four call sites. The widest
# rendering the format can produce is a negative 17-digit mantissa with a
# three-digit negative exponent: -1.2345678901234567e-308, 24 characters. The
# fixture asserts that value and reports the length; this tier reads the length
# back and fails if it ever approaches the buffer.
w=$(grep -o 'widest is [0-9]* chars' "$out" | grep -o '[0-9]*')
if [ -z "$w" ]; then
    printf 'FAIL width: the fixture did not report a widest rendering\n'
    status=1
elif [ "$w" -lt 32 ]; then
    printf 'PASS width (widest rendering %s chars, buffer 32)\n' "$w"
else
    printf 'FAIL width: widest rendering %s chars does not fit the 32-byte buffer\n' "$w"
    status=1
fi

# --- Tier 5: valgrind ---------------------------------------------------------
if command -v valgrind >/dev/null 2>&1; then
    if valgrind --error-exitcode=9 --leak-check=full \
                --errors-for-leak-kinds=definite \
                ./gbasic tests/numfmt_test.bas >"$out" 2>"$err" </dev/null; then
        if diff -q tests/numfmt_test.out "$out" >/dev/null; then
            printf 'PASS valgrind tests/numfmt_test.bas\n'
        else
            printf 'FAIL valgrind tests/numfmt_test.bas (output differs under valgrind)\n'
            status=1
        fi
    else
        printf 'FAIL valgrind tests/numfmt_test.bas\n'
        cat "$err"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit "$status"

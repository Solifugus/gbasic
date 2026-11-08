#!/usr/bin/env bash
# EXACT DECIMALS (docs/exact_decimal_design.md increment 1, book gap G50): a
# number written or read with a decimal point is exact, `+ - *` stay exact,
# division and functions are the double, and exact against exact compares
# mathematically while exact against inexact compares as doubles.
#
# TIER semantics -- tests/exact_decimal/decimal_test.bas, self-checking, every
#                   answer by hand, and it must write nothing to stderr (a
#                   product past 18 places must NOT warn).
# TIER oracle    -- python3's decimal module recomputes 1500 random `+ - *` and
#                   600 comparisons at random scales, digit for digit; nothing
#                   gBASIC printed is read back as the answer. Skips without
#                   python3.
# TIER control   -- the same cases done in doubles disagree with the oracle,
#                   arithmetic and comparison each, so the oracle can tell
#                   exact from approximate.
# TIER warning   -- a decimal that does not fit, at 2^53 or past it, warns
#                   2112 as integer overflow does (whole digits may go); a
#                   loss of far fractional digits does not.
# TIER gsql      -- increment 2: tests/exact_decimal/gsql_test.bas, a decimal
#                   through serialize (byte 2), a G-SQL number column (read
#                   back, SQL arithmetic and sum, seeks and ranges through an
#                   index keyed by the nearest double), and dump/restore
#                   keeping a decimal a decimal and a double a double (the
#                   same serialize bytes after). Skips without G-SQL.
# TIER valgrind  -- bounded.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }
status=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

echo "--- TIER semantics ---"
out="$(timeout -k 5 60 ./gbasic tests/exact_decimal/decimal_test.bas 2>"$tmp/sem.err" </dev/null)"
printf '%s\n' "$out" | grep '^MISMATCH' | sed 's/^/  /'
oks="$(printf '%s\n' "$out" | grep -c '^ok ')"
if printf '%s\n' "$out" | grep -q '^MISMATCH'; then
    printf 'FAIL semantics\n'; status=1
elif [ "$oks" -lt 44 ]; then
    printf 'FAIL semantics (only %s checks ran)\n' "$oks"; status=1
elif [ -s "$tmp/sem.err" ]; then
    sed 's/^/  /' "$tmp/sem.err" | head -5; printf 'FAIL semantics (the fixture wrote to stderr)\n'; status=1
else
    printf 'PASS semantics (%s checks)\n' "$oks"
fi

echo "--- TIER oracle ---"
if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP oracle (no python3: decimal arithmetic was NOT checked from outside)\n'
else
    python3 - "$tmp" <<'PY'
import random, sys
from decimal import Decimal, getcontext
getcontext().prec = 60
tmp = sys.argv[1]
rng = random.Random(20261108)
def operand():
    scale = rng.randint(0, 8)
    m = rng.randint(-10**9 + 1, 10**9 - 1)
    if scale == 0:
        return str(m)
    s = str(abs(m)).rjust(scale + 1, "0")
    text = s[:-scale] + "." + s[-scale:]
    return ("-" if m < 0 else "") + text
def show(d):
    if d == 0:
        return "0"
    t = format(d.normalize(), "f")
    return t
bas, want, dbl = [], [], []
for i in range(1500):
    a, b = operand(), operand()
    op = rng.choice("+-*")
    da, db = Decimal(a), Decimal(b)
    r = da + db if op == "+" else da - db if op == "-" else da * db
    bas.append(f'print string({a} {op} {b})')
    want.append(show(r))
    fa, fb = float(a), float(b)
    fr = fa + fb if op == "+" else fa - fb if op == "-" else fa * fb
    dbl.append(repr(fr).removesuffix(".0") if fr != int(fr) or abs(fr) < 2**53 else repr(fr))
for i in range(600):
    a, b = operand(), operand()
    if rng.random() < 0.3:
        b = a + ("0" * rng.randint(1, 3) if "." in a else "")
    elif rng.random() < 0.5:
        # CLOSE PAIRS, differing past a double's 17 digits, where comparing as
        # doubles answers equal: without them this tier could not tell
        # (a perturbation comparing decimals as doubles left it green).
        m = rng.randint(10**16, 10**17 - 1)
        a = "0." + str(m).rjust(18, "0")
        b = "0." + str(m + rng.choice([-1, 0, 1])).rjust(18, "0")
    op = rng.choice(["=", "<", ">", "!="])
    da, db = Decimal(a), Decimal(b)
    r = {"=": da == db, "<": da < db, ">": da > db, "!=": da != db}[op]
    bas.append(f'print string({a} {op} {b})')
    want.append("true" if r else "false")
    fa, fb = float(a), float(b)
    fr = {"=": fa == fb, "<": fa < fb, ">": fa > fb, "!=": fa != fb}[op]
    dbl.append("true" if fr else "false")
open(f"{tmp}/oracle.bas", "w").write("\n".join(bas) + "\n")
open(f"{tmp}/oracle.want", "w").write("\n".join(want) + "\n")
open(f"{tmp}/oracle.dbl", "w").write("\n".join(dbl) + "\n")
PY
    timeout -k 5 60 ./gbasic "$tmp/oracle.bas" > "$tmp/oracle.got" 2> "$tmp/oracle.err"
    bad="$(paste -d'|' "$tmp/oracle.got" "$tmp/oracle.want" | awk -F'|' '$1 != $2' | wc -l)"
    lines="$(wc -l < "$tmp/oracle.got")"
    if [ "$lines" != 2100 ] || [ "$bad" != 0 ] || [ -s "$tmp/oracle.err" ]; then
        paste -d'|' "$tmp/oracle.bas" "$tmp/oracle.got" "$tmp/oracle.want" | awk -F'|' '$2 != $3' | head -5 | sed 's/^/  /'
        head -3 "$tmp/oracle.err" | sed 's/^/  /'
        printf 'FAIL oracle (%s lines, %s disagree with python decimal)\n' "$lines" "$bad"; status=1
    else
        printf 'PASS oracle (2100 values agree with python decimal, digit for digit)\n'
    fi
    echo "--- TIER control ---"
    dbad="$(paste -d'|' "$tmp/oracle.dbl" "$tmp/oracle.want" | head -1500 | awk -F'|' '$1 != $2' | wc -l)"
    cbad="$(paste -d'|' "$tmp/oracle.dbl" "$tmp/oracle.want" | tail -600 | awk -F'|' '$1 != $2' | wc -l)"
    if [ "$dbad" -gt 50 ] && [ "$cbad" -gt 20 ]; then
        printf 'PASS control (in doubles, %s sums and %s comparisons disagree with the oracle)\n' "$dbad" "$cbad"
    else
        printf 'FAIL control (doubles disagree on only %s sums and %s comparisons, so the oracle cannot tell)\n' "$dbad" "$cbad"; status=1
    fi
fi

echo "--- TIER warning ---"
printf 'x = 1.5 * 1000000000000000000\nprint x > 1e18\n' > "$tmp/loud.bas"
printf 'x = 0.005 * pow(1.005, 12)\ny = 0.123456789 * 0.123456789123\nprint x > 0\n' > "$tmp/quiet.bas"
loud="$(./gbasic "$tmp/loud.bas" 2>&1)"; quiet="$(./gbasic "$tmp/quiet.bas" 2>&1)"
if printf '%s' "$loud" | grep -q '\[2112\]' && [ "$quiet" = true ]; then
    printf 'PASS warning (1.5 * 10^18 warns 2112; a far fractional loss is silent)\n'
else
    printf '  loud: %s\n  quiet: %s\n' "$loud" "$quiet"
    printf 'FAIL warning\n'; status=1
fi

echo "--- TIER gsql ---"
mkdir -p "$tmp/g"
gout="$(timeout -k 5 60 ./gbasic tests/exact_decimal/gsql_test.bas "$tmp/g" 2>"$tmp/g.err" </dev/null)"
if grep -q 'library not loaded: gsql\|no G-SQL' "$tmp/g.err"; then
    printf 'SKIP gsql (no G-SQL in this build: decimals in G-SQL were NOT checked)\n'
else
    printf '%s\n' "$gout" | grep '^MISMATCH' | sed 's/^/  /'
    gok="$(printf '%s\n' "$gout" | grep -c '^ok ')"
    if printf '%s\n' "$gout" | grep -q '^MISMATCH' || [ "$gok" -lt 18 ] || [ -s "$tmp/g.err" ]; then
        head -5 "$tmp/g.err" | sed 's/^/  /'
        printf 'FAIL gsql (%s checks passed)\n' "$gok"; status=1
    else
        printf 'PASS gsql (%s checks: serialize, a number column, its index, dump and restore)\n' "$gok"
    fi
fi

echo "--- TIER valgrind ---"
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    timeout -k 5 240 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/exact_decimal/decimal_test.bas' \
        >/dev/null 2>"$tmp/vg.err" </dev/null
    rc=$?
    if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        printf 'FAIL valgrind (did not finish in 240 s)\n'; status=1
    elif [ "$rc" = "$VG_EXIT" ]; then
        cat "$tmp/vg.err"; printf 'FAIL valgrind\n'; status=1
    else
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

[ "$status" = 0 ] && echo "run_exact_decimal: all checks passed"
exit $status

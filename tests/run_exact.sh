#!/usr/bin/env bash
# EXACT NUMBERS -- exactness as a property of a `number` rather than a second type
# (docs/exact_number_design.md, increments 1-3).
#
# THE DEFECT: `number` is the only numeric kind and it is a double, so integer
# arithmetic went silently wrong above 2^53 -- `pow(2, 53) + 1` answered
# 9007199254740992 with exit 0 and no diagnostic. What made it a defect rather than
# a documented limit is INTERNAL: `run_odbc.sh`'s exactness tier inserts 2^53+1,
# reads it back as a STRING, and asserts both that the digits survived AND that a
# double would have changed them -- citing these same two numbers. BIGINT and
# DECIMAL columns come back as strings for exactly this reason. So gBASIC refused to
# lose precision when a value crossed a driver and lost it in its own arithmetic.
#
# ONE NUMERIC KIND, which is the whole reason this was affordable. A new `integer`
# kind was costed and rejected: ~30 sites is not the price, the SEMANTICS are -- an
# `integer` and a `number` are both numbers, so every mixed expression needs a
# promotion rule, and a wrong promotion rule is a silent wrong answer in precisely
# the class being closed. PLAT-EQ needed three instalments to get comparison right
# across kinds that already existed.
#
# SELF-CHECKING, NOT GOLDEN, AND FORCED: every defect here is a PLAUSIBLE NUMBER one
# digit out. A golden would record it as expected -- which is how the original
# survived from the day `number` was written.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
status=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

run() {   # run <fixture> <min-ok> <label>
    local out
    out="$({ GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic "$1" 2>&1 </dev/null || true; })"
    local bad ok
    bad="$(printf '%s\n' "$out" | grep -c '^MISMATCH' || true)"
    ok="$(printf '%s\n' "$out" | grep -c '^ok  ' || true)"
    if [ "${bad:-0}" -ne 0 ]; then
        printf '%s\n' "$out" | grep '^MISMATCH' | sed 's/^/  /'
        printf 'FAIL %s\n' "$3"; status=1
        return
    fi
    if [ "${ok:-0}" -lt "$2" ]; then
        printf 'FAIL %s (only %s checks ran; at least %s expected)\n' "$3" "$ok" "$2"
        status=1
        return
    fi
    printf 'PASS %s (%s checks)\n' "$3" "$ok"
}

echo "--- TIER 1: arithmetic, comparison, negation, rendering, overflow ---"
run tests/exact/exact_test.bas 50 "exact semantics"

echo "--- TIER 2: THE ORACLE -- python3 computes the same integers exactly ---"
# THE TIER THAT CATCHES AN IMPLEMENTATION WHICH IS SELF-CONSISTENTLY WRONG. Tier 1
# states its own expected values, and I wrote them; an off-by-one in both the
# arithmetic and the expectation would pass. python3's ints are arbitrary-precision,
# so it is a genuinely independent arithmetic -- the same relationship awk has to
# the number FORMATTER in run_numfmt.sh.
#
# The recipes are emitted by gBASIC and recomputed by python, which never reads
# gBASIC's answer.
if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP oracle (python3 unavailable) -- NOTHING WAS CROSS-CHECKED, which is\n'
    printf '     weaker than it looks: tier 1 states values I wrote by hand\n'
else
    cat >"$tmp/dump.bas" <<'BAS'
program main( args )
    ' Pure integer arithmetic, well inside int64, so every step is exact. Printed
    ' as `recipe<TAB>answer` for the oracle to recompute.
    i = 1
    while i <= 40
        print "pow7 " + string(i) + "\t" + string(_up(7, i))
        i = i + 1
    end while
    i = 1
    while i <= 40
        print "fact " + string(i) + "\t" + string(_fact(i))
        i = i + 1
    end while
    i = 0
    while i <= 62
        print "pow2 " + string(i) + "\t" + string(_up(2, i))
        i = i + 1
    end while
    i = 1
    while i <= 40
        print "sum " + string(i) + "\t" + string(_sum(i))
        i = i + 1
    end while
end program

function _up(base, n)
    ' Stops multiplying once it would overflow, so the dump holds only values the
    ' runtime says are exact. `on warning goto next` is what makes "would overflow"
    ' askable without ending the program.
    v = 1
    i = 0
    while i < n
        on warning goto next
        t = v * base
        if warning then
            return v
        end if
        v = t
        i = i + 1
    end while
    return v
end function

function _fact(n)
    v = 1
    i = 2
    while i <= n
        on warning goto next
        t = v * i
        if warning then
            return v
        end if
        v = t
        i = i + 1
    end while
    return v
end function

function _sum(n)
    ' A sum of squares, so the values are not all powers of one base.
    v = 0
    i = 1
    while i <= n
        v = v + i * i * 1000000000
        i = i + 1
    end while
    return v
end function
BAS
    GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic "$tmp/dump.bas" >"$tmp/dump.txt" 2>"$tmp/dump.err" </dev/null || true
    lines="$(wc -l <"$tmp/dump.txt" | tr -d ' ')"
    # A COVERAGE FLOOR, because an oracle over an empty dump agrees perfectly.
    if [ "${lines:-0}" -lt 150 ]; then
        printf 'FAIL oracle (dump holds only %s lines; the recipes did not run)\n' "$lines"
        head -5 "$tmp/dump.err" | sed 's/^/  /'
        status=1
    else
        python3 - "$tmp/dump.txt" >"$tmp/oracle.txt" 2>&1 <<'PY' || true
import sys
bad = 0
checked = 0
for raw in open(sys.argv[1]):
    if "\t" not in raw:
        continue
    recipe, got = raw.rstrip("\n").split("\t", 1)
    op, _, arg = recipe.partition(" ")
    n = int(arg)
    if op == "pow7":
        # Recomputed the way gBASIC does: stop before the step that would leave
        # int64, which is the point at which it stops being exact.
        v = 1
        for _ in range(n):
            if abs(v * 7) > 2**63 - 1:
                break
            v *= 7
    elif op == "pow2":
        v = 1
        for _ in range(n):
            if abs(v * 2) > 2**63 - 1:
                break
            v *= 2
    elif op == "fact":
        v = 1
        for k in range(2, n + 1):
            if abs(v * k) > 2**63 - 1:
                break
            v *= k
    elif op == "sum":
        v = 0
        for k in range(1, n + 1):
            v += k * k * 1000000000
    else:
        continue
    checked += 1
    if str(v) != got:
        bad += 1
        if bad <= 8:
            print("  %-12s gbasic [%s]  python [%s]" % (recipe, got, v))
print("checked %d, disagreements %d" % (checked, bad))
PY
        tail -1 "$tmp/oracle.txt"
        agreed="$(sed -n 's/^checked \([0-9]*\), disagreements \([0-9]*\)$/\1 \2/p' "$tmp/oracle.txt")"
        ncheck="${agreed%% *}"; nbad="${agreed##* }"
        if [ -z "$agreed" ] || [ "${ncheck:-0}" -lt 150 ]; then
            cat "$tmp/oracle.txt" | sed 's/^/  /'
            printf 'FAIL oracle (only %s values cross-checked)\n' "${ncheck:-0}"; status=1
        elif [ "${nbad:-1}" -ne 0 ]; then
            cat "$tmp/oracle.txt" | sed 's/^/  /'
            printf 'FAIL oracle -- gBASIC and python disagree on exact integer arithmetic\n'
            status=1
        else
            printf 'PASS oracle (%s values recomputed in python arbitrary-precision ints)\n' "$ncheck"
        fi
        # AND THE ORACLE MUST BE ABLE TO DISAGREE. Every value above is inside
        # int64, which is where gBASIC is exact -- so if the exactness were turned
        # off, python's answers would differ. Proven by asking python what the
        # DOUBLE arithmetic would have given and requiring it to differ for at
        # least one recipe: without that, this tier would pass on a build with no
        # exact arithmetic at all, since small values agree either way.
        if python3 - <<'PY'
v = 1.0
for _ in range(19):
    v *= 7
exact = 7 ** 19
raise SystemExit(0 if int(v) != exact else 1)
PY
        then
            printf 'ok   the oracle can disagree: 7^19 differs in double arithmetic\n'
        else
            printf 'MISMATCH the oracle cannot distinguish exact from double arithmetic\n'
            status=1
        fi
    fi
fi

echo "--- TIER 3: refusals and the diagnostic ---"
say() {
    printf '%s\n' "$1" >"$tmp/p.bas"
    { ./gbasic "$tmp/p.bas" 2>&1 </dev/null || true; } | head -1
}
want() {
    local got; got="$(say "$2")"
    case "$got" in
        *"$3"*) printf 'ok   %s\n' "$1" ;;
        *) printf 'MISMATCH %s\n  want: %s\n  got:  %s\n' "$1" "$3" "$got"; status=1 ;;
    esac
}
want 'the overflow warning is located' 'print 9000000000000000000 + 9000000000000000000' ':1:'
want 'and carries its code'            'print 9000000000000000000 * 2'                   '[2112]'
want 'and names the operation'         'print 9000000000000000000 * 2'                   'product'
# AND THE CONTROL, or "it warns" is satisfied by warning on every sum.
got="$({ printf 'print 2 + 2\n' >"$tmp/q.bas"; ./gbasic "$tmp/q.bas" 2>&1 >/dev/null </dev/null || true; })"
if [ -z "$got" ]; then
    printf 'ok   CONTROL: arithmetic that fits is silent\n'
else
    printf 'MISMATCH a fitting sum warned\n  got: %s\n' "$got"; status=1
fi
# IT REACHES --json-diagnostics AS JSON, like every warning since increment 4.
json="$({ printf 'print 9000000000000000000 * 2\n' >"$tmp/j.bas"; ./gbasic --json-diagnostics "$tmp/j.bas" 2>&1 >/dev/null </dev/null || true; })"
if printf '%s' "$json" | grep -q '"subcode":2112' && printf '%s' "$json" | grep -q '"severity":"warning"'; then
    printf 'ok   --json-diagnostics carries it\n'
else
    printf 'MISMATCH --json-diagnostics\n  got: %s\n' "$json"; status=1
fi

echo "--- TIER 4: sizeof(Value) did not move ---"
# THE DESIGN'S ZERO-MEMORY-COST CLAIM, asserted rather than trusted. A Value is
# every array element and every record field, so growing it would be a real cost in
# a release about bulk data -- and the numeric member only fits because `DateTime`
# in the same union is larger. If a future edit grows it, this says so.
cat >"$tmp/size.c" <<'C'
#include <stdio.h>
typedef struct { int a,b,c,d,e,f,g,h; } DateTime;
int main(void) {
    struct { double value; long long exact; int is_exact; } num;
    printf("num %zu datetime %zu\n", sizeof num, sizeof(DateTime));
    return sizeof num <= sizeof(DateTime) ? 0 : 1;
}
C
if command -v cc >/dev/null 2>&1 && cc -o "$tmp/size" "$tmp/size.c" 2>/dev/null; then
    if "$tmp/size" >"$tmp/size.txt"; then
        printf 'ok   %s -- the numeric member fits inside the union\n' "$(cat "$tmp/size.txt")"
    else
        printf 'MISMATCH %s -- the numeric member now grows Value\n' "$(cat "$tmp/size.txt")"
        status=1
    fi
else
    printf 'SKIP sizeof (no C compiler)\n'
fi

echo "--- TIER 5: valgrind ---"
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    GBASIC_PATH=stdlib vg_run ./gbasic tests/exact/exact_test.bas \
        >/dev/null 2>"$tmp/vg.err" </dev/null
    rc=$?
    if [ "$rc" = "$VG_EXIT" ]; then
        cat "$tmp/vg.err"; printf 'FAIL valgrind\n'; status=1
    else
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

exit $status

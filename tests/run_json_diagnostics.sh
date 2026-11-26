#!/usr/bin/env bash
# --json-diagnostics golden test (PLAN.md Phase L, Deliverable 2).
#
# `gbasic --json-diagnostics FILE` emits collected diagnostics as JSON lines on
# stderr; the default (no-flag) path must stay byte-exact legacy format (that is
# guarded by the negative suite — spot-checked here too).
set -u

cd "$(dirname "$0")/.."

if [ ! -x ./gbasic ]; then
    if ! make gbasic >/dev/null 2>&1; then
        echo "FAIL run_json_diagnostics: could not build gbasic"
        exit 1
    fi
fi

status=0
BAS=tests/json_diagnostics_test.bas
GOLD=tests/json_diagnostics_test.jsongolden

# JSON-lines output on stderr matches the golden.
got="$(./gbasic --json-diagnostics "$BAS" 2>&1 >/dev/null)"
exp="$(cat "$GOLD")"
if [ "$got" = "$exp" ]; then
    echo "PASS json-diagnostics output"
else
    echo "FAIL json-diagnostics output"
    echo "  expected: $exp"
    echo "  actual:   $got"
    status=1
fi

# Default path stays legacy (byte-exact), NOT JSON.
default="$(./gbasic "$BAS" 2>&1 >/dev/null)"
legacy="syntax error at $BAS:1:5: unexpected \`)\`"
if [ "$default" = "$legacy" ]; then
    echo "PASS json-diagnostics default-unchanged"
else
    echo "FAIL json-diagnostics default-unchanged"
    echo "  expected: $legacy"
    echo "  actual:   $default"
    status=1
fi

# --- PLAT-DEBT 2: program arguments -------------------------------------------
#
# --json-diagnostics RUNS the program (it reaches eval_program, unlike --ast /
# --tokens / --add-loads which only inspect it), so program arguments are
# meaningful and must be accepted. They were rejected until PLAT-DEBT 2 because
# the dispatch matched `argc == 3`, while the usage line already promised
# `FILE [args...]` -- code and documentation stating opposite things.
#
# The composition cases matter as much as the feature. PLAT-STREAM's
# extract_flag() lifts --line-buffered out of argv BEFORE mode dispatch and stops
# at the first non-flag argument, which is what makes a flag-looking argument
# after FILE reach the program instead of the interpreter. That behaviour is load-
# bearing and is asserted here in both modes.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/args.bas" <<'EOF'
program main(args)
  print "count=" + count(args)
  for each a in args
    print "arg=" + a
  end for
end program
EOF

check() { # label expected-stdout  command...
    local label=$1 expected=$2; shift 2
    local got
    got="$("$@" 2>/dev/null)"
    if [ "$got" = "$expected" ]; then
        printf 'PASS %s\n' "$label"
    else
        printf 'FAIL %s\n  expected: %s\n  actual:   %s\n' "$label" "$expected" "$got"
        status=1
    fi
}

two=$'count=2\narg=one\narg=two'
check "args: run mode (unchanged)"        "$two" ./gbasic "$work/args.bas" one two
check "args: --json-diagnostics"          "$two" ./gbasic --json-diagnostics "$work/args.bas" one two
check "args: none is still none"          "count=0" ./gbasic --json-diagnostics "$work/args.bas"
check "args: --line-buffered then mode"   "$two" ./gbasic --line-buffered --json-diagnostics "$work/args.bas" one two
# A flag-looking token AFTER FILE belongs to the program, in both modes.
after=$'count=1\narg=--line-buffered'
check "args: flag after FILE, run mode"   "$after" ./gbasic "$work/args.bas" --line-buffered
check "args: flag after FILE, json mode"  "$after" ./gbasic --json-diagnostics "$work/args.bas" --line-buffered

# Passing arguments must not perturb the diagnostics themselves.
a="$(./gbasic --json-diagnostics "$BAS" 2>&1 >/dev/null)"
b="$(./gbasic --json-diagnostics "$BAS" one two 2>&1 >/dev/null)"
if [ "$a" = "$b" ] && [ "$a" = "$exp" ]; then
    echo "PASS args: diagnostics byte-identical with and without args"
else
    echo "FAIL args: diagnostics changed when program arguments were passed"
    status=1
fi

# The INSPECT-only modes never run the program, so an argument there would be
# silently ignored. They reject it instead, and the usage line now says so.
for mode in --ast --tokens --add-loads --add-uses; do
    if ./gbasic "$mode" "$work/args.bas" extra >/dev/null 2>&1; then
        printf 'FAIL args: %s accepted a program argument it cannot use\n' "$mode"
        status=1
    else
        printf 'PASS args: %s rejects program arguments\n' "$mode"
    fi
    if ./gbasic "$mode" "$work/args.bas" >/dev/null 2>&1; then
        printf 'PASS args: %s still works with FILE alone\n' "$mode"
    else
        printf 'FAIL args: %s broke for the plain FILE case\n' "$mode"
        status=1
    fi
done

# --- THE FIX FIELD (2026-11-26, an AI aid) -----------------------------------
#
# A diagnostic whose rewrite is certain carries "fix": the bytes to replace
# and the text to put there, so an agent can apply it unread. Each case below
# is held three ways: the fix as printed; the program with the fix APPLIED
# (by python3, from the printed numbers alone) running clean with the output
# given, which is what proves the range is the right bytes; and the cases
# where precedence or meaning could move, which must carry no fix at all.
fix_apply() {   # fix_apply <file> <json line>: the file with the fix applied, on stdout
    python3 - "$1" "$2" <<'PY'
import json, sys
src = open(sys.argv[1], "rb").read()
d = json.loads(sys.argv[2])
f = d["fix"]
lines = src.split(b"\n")
def offset(pos):
    return sum(len(l) + 1 for l in lines[:pos["line"] - 1]) + pos["column"] - 1
a, b = offset(f["start"]), offset(f["end"])
assert 0 <= a <= b <= len(src), (a, b)
sys.stdout.buffer.write(src[:a] + f["text"].encode() + src[b:])
PY
}
fixcase() {   # fixcase <label> <source> <expected fix json or none> <stdout once fixed>
    local label=$1 want=$3 out=$4 got line
    printf '%b' "$2" >"$work/fix.bas"
    line=$(./gbasic --json-diagnostics "$work/fix.bas" 2>&1 >/dev/null | head -1)
    got=$(printf '%s' "$line" | grep -o '"fix":.*}}$' | sed 's/}$//')
    [ -n "$got" ] || got=none
    if [ "$got" != "$want" ]; then
        printf 'FAIL fix %s\n  expected: %s\n  actual:   %s\n' "$label" "$want" "$got"
        status=1
        return
    fi
    if [ "$want" = none ]; then
        printf 'PASS fix %s: none offered\n' "$label"
        return
    fi
    if ! command -v python3 >/dev/null 2>&1; then
        printf 'PASS fix %s (applying it skipped: no python3)\n' "$label"
        return
    fi
    fix_apply "$work/fix.bas" "$line" >"$work/fixed.bas"
    local ran err
    ran=$(cd "$work" && GBASIC_PATH="$OLDPWD/stdlib" "$OLDPWD/gbasic" fixed.bas 2>"$work/fixed.err")
    err=$(cat "$work/fixed.err")
    if [ "$ran" = "$(printf '%b' "$out")" ] && [ -z "$err" ]; then
        printf 'PASS fix %s, and the fixed program runs\n' "$label"
    else
        printf 'FAIL fix %s: the fixed program\n%s\n  printed: %s\n  stderr:  %s\n' "$label" "$(cat "$work/fixed.bas")" "$ran" "$err"
        status=1
    fi
}
pos() { printf '{"start":{"line":%s,"column":%s},"end":{"line":%s,"column":%s},"text":"%s"}' "$@"; }
fixcase "<>" 'a = 1\nif a <> 2 then\n    print "x"\nend if\n' "\"fix\":$(pos 2 6 2 8 '!=')" 'x'
fixcase elseif 'a = 1\nif a = 2 then\n    print "x"\nelseif a = 1 then\n    print "y"\nend if\n' "\"fix\":$(pos 4 1 4 7 'else if')" 'y'
fixcase elsif 'a = 1\nif a = 2 then\n    print "x"\nelsif a = 1 then\n    print "y"\nend if\n' "\"fix\":$(pos 4 1 4 6 'else if')" 'y'
fixcase endif 'a = 1\nif a = 1 then\n    print "x"\nendif\n' "\"fix\":$(pos 4 1 4 6 'end if')" 'x'
fixcase let 'let x = 1\nprint x\n' "\"fix\":$(pos 1 1 1 5 '')" '1'
fixcase rem 'rem a note\nprint 1\n' "\"fix\":$(pos 1 1 1 4 "'")" '1'
fixcase "x:int" 'x:int = 5\nprint x\n' "\"fix\":$(pos 1 3 1 6 number)" '5'
fixcase "f(a:float)" 'function f(a:float)\n    return a * 2\nend function\nprint f("4")\n' "\"fix\":$(pos 1 14 1 19 number)" '8'
fixcase "sleep 0" "sleep 0   ' a pause\nprint 1\n" "\"fix\":$(pos 1 6 1 8 '(0)')" '1'
printf 'library helper\n    function twice(n)\n        return n * 2\n    end function\nend library\n' >"$work/helper.bas"
fixcase "load as from" 'load helper as h from "helper.bas"\nprint h.twice(4)\n' "\"fix\":$(pos 1 13 1 35 'from \"helper.bas\" as h')" '8'
fixcase "i mod 7" 'i = 9\nprint string(i mod 7)\n' "\"fix\":$(pos 2 14 2 21 'mod(i, 7)')" '2'
fixcase "if i mod 2 =" 'i = 9\nif i mod 2 = 1 then\n    print "odd"\nend if\n' "\"fix\":$(pos 2 4 2 11 'mod(i, 2)')" 'odd'
fixcase "a + b mod 7" 'a = 2\nb = 9\nx = a + b mod 7\nprint x\n' "\"fix\":$(pos 3 9 3 16 'mod(b, 7)')" '4'
fixcase "return n mod 10" 'function ones(n)\n    return n mod 10\nend function\nprint ones(123)\n' "\"fix\":$(pos 2 12 2 20 'mod(n, 10)')" '3'
# no fix where the rewrite could regroup or is not one the message names
fixcase "a * b mod 7" 'a = 2\nb = 9\nprint a * b mod 7\n' none ''
fixcase "a mod b * c" 'a = 2\nb = 9\nprint a mod b * 3\n' none ''
fixcase "-a mod 7" 'a = 2\nprint -a mod 7\n' none ''
fixcase "a mod f(x)" 'a = 2\nprint a mod abs(3)\n' none ''
fixcase "r.n mod 2" 'r = { n: 3 }\nprint r.n mod 2\n' none ''
fixcase "xor" 'a = 2\nb = 9\nx = a xor b\n' none ''
fixcase "^" 'print 2 ^ 3\n' none ''
fixcase "x:float(2)" 'x:float(2) = 5\n' none ''
fixcase "reserved name" 'each = 5\n' none ''

echo "=== run_json_diagnostics status=$status ==="
exit $status

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
legacy="parse error at $BAS:1:5: syntax error, unexpected RPAREN"
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

echo "=== run_json_diagnostics status=$status ==="
exit $status

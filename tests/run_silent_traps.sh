#!/usr/bin/env bash
# Failures that reported without a diagnostic, promoted to raises.
#
# Both cases here shared one signature, catalogued in
# docs/warning_model_design.md §7: an UNLOCATED bare line on stderr, a result of
# `nothing`, and EXIT CODE 0. `nothing` is a legitimate value, so a caller could
# not tell the failure from a real one, and CI saw success. Neither is a warning
# — they are raises that were never written:
#
#   goto <unknown label>   printed, then ABANDONED THE REST OF THE FUNCTION, so
#                          a typo'd label silently truncated it
#   a[99] (read)           printed and yielded `nothing` — while out-of-range
#                          ASSIGNMENT called runtime_error_raise properly, two
#                          lines away in the same source file
#
# Being raises, they are now located, fatal by default, and CATCHABLE, which a
# printed line never was.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fail() { printf 'FAIL %s\n' "$1"; exit 1; }

# --- fatal, located, nonzero ----------------------------------------------
fatal() { # file expected-fragment
    if ./gbasic "tests/silent_traps/$1" >"$scratch/out" 2>"$scratch/err"; then
        fail "$1 (expected a NONZERO exit; a silent trap is one that exits 0)"
    fi
    grep -qF "$2" "$scratch/err" \
        || fail "$1 (missing: $2; got: $(cat "$scratch/err"))"
    # Located: the old bare line carried no file:line, which is what made these
    # untraceable in a real program.
    grep -qE "^runtime error at .+:[0-9]+:[0-9]+:" "$scratch/err" \
        || fail "$1 (the diagnostic is not LOCATED: $(cat "$scratch/err"))"
    grep -q "unreachable" "$scratch/out" \
        && fail "$1 (execution continued past the failure)"
    printf 'PASS %s\n' "${1%.bas}"
}

fatal label_goto.bas "unknown label in function f: nowhere"
fatal label_gosub.bas "unknown label in function f: nowhere"
fatal index_read.bas "array index out of range"
fatal modifier_date.bas "date modifier expects an ISO-like date string"
fatal modifier_file.bas "file modifier expects a path string"
fatal watch_undefined.bas "undefined variable: nope"
fatal watch_body.bas "undefined variable: undefined_in_body"

# --- catchable, which the printed line never was --------------------------
for name in label_caught index_read_caught modifier_caught watch_caught; do
    ./gbasic "tests/silent_traps/$name.bas" >"$scratch/got" 2>"$scratch/err" \
        || fail "$name (exited nonzero: $(cat "$scratch/err"))"
    diff -u "tests/silent_traps/$name.out" "$scratch/got" \
        || fail "$name (output diverged)"
    printf 'PASS %s\n' "$name"
done

# --- the write path was always right; it must stay that way ---------------
printf 'program main( args )\n    a = [1]\n    a[9] = 2\nend program\n' >"$scratch/w.bas"
if ./gbasic "$scratch/w.bas" >/dev/null 2>"$scratch/err"; then
    fail "write path (out-of-range assignment must still raise)"
fi
grep -qF "array index out of range" "$scratch/err" \
    || fail "write path (message changed: $(cat "$scratch/err"))"
printf 'PASS write_path_unchanged\n'

# --- the modifier that was ALWAYS right must stay that way ----------------
# `USD` raised from the start, four lines from `date` in the same dispatch
# function. That neighbouring inconsistency is what made this a bug rather
# than a policy.
printf 'program main( args )\n    m{USD} = "nope"\nend program\n' >"$scratch/m.bas"
if ./gbasic "$scratch/m.bas" >/dev/null 2>"$scratch/err"; then
    fail "USD (must still raise)"
fi
grep -qF "USD modifier expects a number" "$scratch/err" \
    || fail "USD (message changed: $(cat "$scratch/err"))"
printf 'PASS usd_unchanged\n'

printf 'run_silent_traps: 12 cases passed\n'

#!/usr/bin/env bash
# WP-CORE-1 decisive checks that golden-string comparison cannot express:
#   - sleep(seconds) actually blocks for at least the requested interval
#   - env(name) reads a set variable and reports unset as unknown
# The deterministic surface (return values, arity/type errors) is covered by the
# golden suites: examples/sleep_test.* + tests/negative_sleep_* (run_negative.sh)
# and examples/env_builtin_test.* + tests/negative_env_* .
set -euo pipefail

cd "$(dirname "$0")/.."

pass=0
fail=0

check() {
    # check <description> <condition-exit-status>
    if [[ "$2" -eq 0 ]]; then
        printf 'PASS %s\n' "$1"
        pass=$((pass + 1))
    else
        printf 'FAIL %s\n' "$1"
        fail=$((fail + 1))
    fi
}

# --- sleep: elapsed >= requested -------------------------------------------
# Request 0.30s; assert wall-clock elapsed is at least 300ms. nanosleep
# guarantees the full interval, so any shortfall is a real regression.
requested_ms=300
prog="$(mktemp --suffix=.bas)"
printf 'program m(args)\n    print(sleep(0.30))\nend program\n' >"$prog"
start_ns=$(date +%s%N)
./gbasic "$prog" >/dev/null
end_ns=$(date +%s%N)
rm -f "$prog"
elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
printf '  sleep(0.30): elapsed %dms (requested %dms)\n' "$elapsed_ms" "$requested_ms"
[[ "$elapsed_ms" -ge "$requested_ms" ]]
check "sleep elapsed >= requested" $?

# sleep(0) must return promptly and not error
zero_prog="$(mktemp --suffix=.bas)"
printf 'program m(args)\n    print(sleep(0))\nend program\n' >"$zero_prog"
[[ "$(./gbasic "$zero_prog")" == "0" ]]
zero_status=$?
rm -f "$zero_prog"
check "sleep(0) returns 0" "$zero_status"

# --- env: set vs unset ------------------------------------------------------
export GBASIC_CORE_TEST_SET=present
unset GBASIC_CORE_TEST_UNSET 2>/dev/null || true
env_prog="$(mktemp --suffix=.bas)"
cat >"$env_prog" <<'EOF'
program m(args)
    print(env("GBASIC_CORE_TEST_SET"))
    print(is_unknown(env("GBASIC_CORE_TEST_UNSET")))
end program
EOF
env_out="$(./gbasic "$env_prog")"
rm -f "$env_prog"
[[ "$env_out" == $'present\ntrue' ]]
check "env set -> value, unset -> unknown" $?

# --- the read-then-shadow warning -------------------------------------------
# A function that READS an enclosing variable and then assigns the same name is
# almost always trying to write it back; gBASIC creates a silent local instead
# (no closures). The warning must: appear on STDERR exactly once (deduplicated
# across calls), carry file:line:col, and change nothing -- stdout and exit
# status are those of the unwarned program. The fixture's stdout doubles as the
# proof of the underlying semantics: the global stays 5 however often bump()
# runs.
sw_out="$(mktemp)"; sw_err="$(mktemp)"
./gbasic tests/shadow_warning_test.bas >"$sw_out" 2>"$sw_err"
sw_status=$?
[[ "$sw_status" -eq 0 ]]
check "shadow warning: program still exits 0" $?
[[ "$(cat "$sw_out")" == $'first:  6\nsecond: 6\nglobal: 5' ]]
check "shadow warning: behavior unchanged (silent shadow semantics)" $?
[[ "$(grep -c "creates a new function-local" "$sw_err")" -eq 1 ]]
check "shadow warning: warned exactly once, not per call" $?
grep -q "shadow_warning_test.bas:5:3" "$sw_err"
check "shadow warning: names the file, line and column" $?
# ...and a local that merely SHARES a global's name, never read first, is silent.
sq_prog="$(mktemp --suffix=.bas)"
cat >"$sq_prog" <<'EOF'
function quiet()
    i = 0
    while i < 3
        i = i + 1
    end while
    return i
end function
program m(args)
    i = 99
    print(quiet())
end program
EOF
sq_err="$(mktemp)"
./gbasic "$sq_prog" >/dev/null 2>"$sq_err"
[[ ! -s "$sq_err" ]]
check "shadow warning: an unread same-name local stays silent" $?
rm -f "$sw_out" "$sw_err" "$sq_prog" "$sq_err"

printf 'core suite: %d passed / %d failed\n' "$pass" "$fail"
[[ "$fail" -eq 0 ]]

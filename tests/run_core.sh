#!/usr/bin/env bash
# WP-CORE-1 decisive checks that golden-string comparison cannot express:
#   - sleep(seconds) actually blocks for at least the requested interval
#   - env(name) reads a set variable and reports unset as unknown
# The deterministic surface (return values, arity/type errors) is covered by the
# golden suites: examples/sleep_test.* + tests/negative_sleep_* (run_negative.sh)
# and examples/env_builtin_test.* + tests/negative_env_* .
# NOT `set -e`. Every assertion here is `<condition>` followed by
# `check "..." $?`, and under errexit a FAILING condition aborts the script
# before `check` ever runs -- so a real regression truncated the suite instead
# of reporting FAIL, and the summary line simply never printed. A test suite
# must survive its own failures; the exit status comes from the fail counter at
# the bottom. (Found while red-proving the timezone tier: the deliberately
# broken binary produced no FAIL line at all.)
set -uo pipefail

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

# --- exit(n): the process really exits with that status ---------------------
# A golden cannot express an exit code, and the code IS the contract: anything
# that branches on a gBASIC tool's result -- a scheduler, CI, a shell -- reads
# $?. Before `exit`, a program could only ever say 0, or 1 by failing.
for want in 0 1 60 255; do
    ex_prog="$(mktemp --suffix=.bas)"
    printf 'print "ran"\nexit(%s)\nprint "UNREACHABLE"\n' "$want" > "$ex_prog"
    ex_out="$(./gbasic "$ex_prog" 2>/dev/null)"; got=$?
    [[ "$got" -eq "$want" ]]
    check "exit($want) leaves status $want" $?
    # ...and STOPS. The first implementation set the status and let the next
    # statement run, which is worse than not having exit at all: right code,
    # wrong work done.
    [[ "$ex_out" == "ran" ]]
    check "exit($want) stops execution" $?
    rm -f "$ex_prog"
done

# exit from inside a function inside a loop must unwind everything.
ex_deep="$(mktemp --suffix=.bas)"
cat > "$ex_deep" <<'EOF'
function bail()
    exit(7)
end function
for i = 1 to 3
    print "i=" + string(i)
    if i = 2 then
        x = bail()
    end if
next i
print "UNREACHABLE"
EOF
deep_out="$(./gbasic "$ex_deep" 2>/dev/null)"; deep_rc=$?
[[ "$deep_rc" -eq 7 && "$deep_out" == "i=1
i=2" ]]
check "exit unwinds out of a function inside a loop" $?
rm -f "$ex_deep"

# --- now(zone) and epoch(dt, zone) against the SYSTEM clock -----------------
# Facts about the world, which is why they live here: the assertion is against
# `date`, not against a string gBASIC printed. This is the check that would
# have caught the trap that prompted it -- `to_zone(now(), "UTC")` is a no-op,
# so a program could ask for UTC, be handed local time, and print it with a
# UTC label. On this machine that is a four-hour error in an audit trail.
tz_prog="$(mktemp --suffix=.bas)"
cat > "$tz_prog" <<'EOF'
print string(now("UTC"))
print string(epoch())
print string(epoch(now("UTC"), "UTC"))
print string(epoch(now()))
EOF
tz_out="$(./gbasic "$tz_prog" 2>/dev/null)"
gb_utc="$(printf '%s\n' "$tz_out" | sed -n 1p | cut -c1-16)"
sys_utc="$(date -u +'%Y-%m-%d %H:%M')"
[[ "$gb_utc" == "$sys_utc" ]]
check "now(\"UTC\") matches the system UTC clock ($gb_utc vs $sys_utc)" $?

# The three instants must agree: epoch() now, the UTC civil value read back as
# UTC, and the local civil value read back as local. Disagreement means one of
# them is silently applying an offset.
e_now="$(printf '%s\n' "$tz_out" | sed -n 2p)"
e_utc="$(printf '%s\n' "$tz_out" | sed -n 3p)"
e_loc="$(printf '%s\n' "$tz_out" | sed -n 4p)"
[[ "$e_now" == "$e_utc" && "$e_now" == "$e_loc" ]]
check "epoch(), epoch(utc,\"UTC\") and epoch(local) are the same instant" $?

# And that instant is the real one.
sys_epoch="$(date +%s)"
drift=$(( e_now > sys_epoch ? e_now - sys_epoch : sys_epoch - e_now ))
[[ "$drift" -le 2 ]]
check "epoch() matches date +%s (drift ${drift}s)" $?

# A named zone that is NOT the local one, so a no-op cannot pass: Asia/Tokyo
# has no DST and is never equal to a US local time.
tk_prog="$(mktemp --suffix=.bas)"
printf 'print string(now("Asia/Tokyo"))\n' > "$tk_prog"
gb_tokyo="$(./gbasic "$tk_prog" 2>/dev/null | cut -c1-16)"
sys_tokyo="$(TZ=Asia/Tokyo date +'%Y-%m-%d %H:%M')"
[[ "$gb_tokyo" == "$sys_tokyo" ]]
check "now(\"Asia/Tokyo\") matches that zone, not the local clock" $?
rm -f "$tz_prog" "$tk_prog"

# --- read is binary-safe ----------------------------------------------------
# `write` always was; `read` used strlen and stopped at the first NUL, so the
# pair was asymmetric and anything holding binary -- a `serialize` payload, an
# image, a compiled artifact -- could be written and then silently read back
# SHORT. No error, no short-write, just less data than went in.
#
# This lives in run_core because the assertion is about BYTES ON DISK rather
# than about anything the interpreter says: the fixture writes a known length,
# reads it back, and compares. A golden of the printed number would pass just
# as happily on the broken build, since 1 is a perfectly plausible length.
bin_prog="$(mktemp --suffix=.bas)"
cat >"$bin_prog" <<'BINEOF'
p {file}= env("GBASIC_BIN_PATH")
s = "a" + from_bytes([0]) + "b" + from_bytes([0, 255]) + "z"
write(p, s)
back = read(p)
print string(byte_count(s)) + " " + string(byte_count(back)) + " " + string(back = s)
BINEOF
bin_path="$(mktemp)"
bin_out="$(GBASIC_BIN_PATH="$bin_path" ./gbasic "$bin_prog" 2>/dev/null)"
[[ "$bin_out" == "6 6 true" ]]
check "read is binary-safe: interior NULs survive a file round trip" $?

# The control: the file really does hold those bytes, checked from OUTSIDE the
# interpreter, so a `read` that echoed its own `write` buffer could not pass.
[[ "$(wc -c <"$bin_path")" == "6" ]]
check "read is binary-safe: the file on disk is the full length" $?
rm -f "$bin_prog" "$bin_path"

# --- {file} and {dir} are idempotent ---------------------------------------
#
# `{file}` is how one ASSERTS a path is a file, and asserting it of something
# already a file should be a no-op. It matters most straight out of a listing:
# `list_files` yields FILE values, so `f {file}= entry` raised, and the error
# surfaced AT THE MODIFIER -- reading as "the modifier is broken" rather than
# "the listing returned a type you did not expect".
idem_out="$(GBASIC_PATH=stdlib ./gbasic tests/file_modifier_idempotent.bas 2>&1)"
printf '%s' "$idem_out" | grep -qx 'mismatches: 0'
check "{file}/{dir} idempotent, and still refuse a wrong type" $?

printf 'core suite: %d passed / %d failed\n' "$pass" "$fail"
[[ "$fail" -eq 0 ]]

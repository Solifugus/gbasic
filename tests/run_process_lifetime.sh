#!/usr/bin/env bash
# "Nothing this interpreter started outlives it" — under SIGKILL too.
#
# docs/reference.md states it in bold, and it was enforced entirely in
# userspace: a teardown pass at the end of eval_program that kills and reaps.
# That pass cannot run when the parent is SIGKILLed, which is precisely the case
# the DOGFOOD ledger recorded (item 4) — four gBASIC children found sleeping two
# days after the runs that started them, three with their working directory
# already deleted.
#
# A promise about what survives a kill can only be kept by the kernel, so both
# `process.*` fork sites now arm PR_SET_PDEATHSIG between fork and exec, as the
# actor path has since it was written.
#
# The suite is written from the observable side: start a child, kill the parent
# with an uncatchable signal, and look for the child.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fail() { printf 'FAIL %s\n' "$1"; exit 1; }

# A marker unique to this run, so pgrep cannot match a stray from another suite
# or another checkout running concurrently.
marker="gbasic-lifetime-$$-$(od -An -N2 -tu2 </dev/urandom | tr -d ' ')"

# The child is `sleep`, wrapped in a shell only to carry the marker in its argv.
child_cmd() { printf '%s' "sleep 300 # $marker"; }

alive() { pgrep -f "$marker" >/dev/null 2>&1; }

reap_strays() { pkill -9 -f "$marker" >/dev/null 2>&1 || true; }
trap 'reap_strays; rm -rf "$scratch"' EXIT

# Poll for the child to disappear. PDEATHSIG is delivered by the kernel at the
# moment the parent dies, so this is generous by two orders of magnitude; the
# budget exists so a failure reports rather than hangs.
gone_within() { # seconds
    local deadline=$(( SECONDS + $1 ))
    while [ "$SECONDS" -lt "$deadline" ]; do
        alive || return 0
        sleep 0.1
    done
    return 1
}

kill_parent_case() { # label program-body
    reap_strays
    printf '%s\n' "$2" >"$scratch/case.bas"
    ./gbasic "$scratch/case.bas" >"$scratch/out" 2>"$scratch/err" &
    local parent=$!

    # Wait for the child to actually exist before killing anything, or the test
    # proves nothing: a child that was never started is trivially "gone".
    local deadline=$(( SECONDS + 10 ))
    while ! alive; do
        [ "$SECONDS" -lt "$deadline" ] || fail "$1 (the child never started)"
        kill -0 "$parent" 2>/dev/null || fail "$1 (parent exited early: $(cat "$scratch/err"))"
        sleep 0.1
    done

    kill -9 "$parent" 2>/dev/null || true
    wait "$parent" 2>/dev/null || true

    gone_within 5 || {
        local info
        info="$(pgrep -af "$marker" || true)"
        reap_strays
        fail "$1 (child outlived a SIGKILLed parent: $info)"
    }
    printf 'PASS %s\n' "$1"
}

# --- process.start: the handle case the ledger caught ----------------------
kill_parent_case start "$(cat <<EOF
program main( args )
    h = process.start({ command: "sh", args: ["-c", "$(child_cmd)"] })
    while true
        sleep(0.05)
    end while
end program
EOF
)"

# --- process.start with the handle already dropped -------------------------
# A dropped handle is deliberately NOT lethal (reference: "handles are safe to
# abandon"), so the child is running with nothing holding it. That is the state
# the userspace sweep was built for, and the one it cannot reach under SIGKILL.
kill_parent_case abandoned "$(cat <<EOF
program main( args )
    h = process.start({ command: "sh", args: ["-c", "$(child_cmd)"] })
    h = nothing
    while true
        sleep(0.05)
    end while
end program
EOF
)"

# --- process.run: killed while the synchronous child is still going --------
kill_parent_case run "$(cat <<EOF
program main( args )
    r = process.run({ command: "sh", args: ["-c", "$(child_cmd)"] })
    print r.exit_code
end program
EOF
)"

# --- the control: a clean exit still reaps, and the program still works -----
reap_strays
cat >"$scratch/clean.bas" <<EOF
program main( args )
    r = process.run({ command: "sh", args: ["-c", "echo ran; exit 3"] })
    print r.stdout
    print string(r.exit_code)
end program
EOF
./gbasic "$scratch/clean.bas" >"$scratch/out" 2>"$scratch/err" \
    || fail "clean (exited nonzero: $(cat "$scratch/err"))"
printf 'ran\n\n3\n' >"$scratch/want"
diff -u "$scratch/want" "$scratch/out" || fail "clean (output diverged)"
printf 'PASS clean_exit\n'

# --- every fork site arms it, including one added tomorrow -----------------
# Counted against the fork sites, not grepped for the symbol: the failure this
# guards is a NEW fork site that forgets, and "is PR_SET_PDEATHSIG mentioned
# somewhere" cannot see that. One definition of the helper plus one call per
# fork site is the invariant.
forks=$(grep -c '^\s*pid_t pid = fork();' src/eval.c || true)
mentions=$(grep -c 'proc_arm_parent_death(' src/eval.c || true)
calls=$(( mentions - 1 ))   # less the definition
[ "$forks" -ge 3 ] || fail "tripwire (found $forks fork sites; the pattern moved)"
[ "$calls" -ge "$forks" ] \
    || fail "tripwire ($forks fork sites, $calls armed: a child can outlive a kill)"
printf 'PASS every_fork_arms (%d fork sites, %d armed)\n' "$forks" "$calls"

printf 'run_process_lifetime: 5 cases passed\n'

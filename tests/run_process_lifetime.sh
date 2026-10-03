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
nonce="$(od -An -N2 -tu2 </dev/urandom | tr -d ' ')"
marker="gbasic-lifetime-$$-$nonce"

# The child is `sleep`, wrapped in a shell only to carry the marker in its argv.
# The duration carries the nonce too, for the platform where the shell's argv
# is not visible (below); 300.N seconds is 300 seconds for every purpose here.
child_cmd() { printf '%s' "sleep 300.$nonce # $marker"; }

# WITHOUT pgrep (MSYS2 under Windows, where procps is not installed by default)
# the marker cannot simply be looked for another way: there, `sh -c "one
# command"` hands its MSYS pid to `sleep`, so NO process's argv carries the
# shell's comment -- only sleep's own, which is why the duration names the run.
# Scanning /proc for it is what pgrep -f does, minus the dependency.
proc_matches() { # print the pids whose argv contains $1
    local p
    for p in /proc/[0-9]*; do
        grep -qaF -- "$1" "$p/cmdline" 2>/dev/null && printf '%s\n' "${p#/proc/}"
    done
    return 0
}
if command -v pgrep >/dev/null 2>&1; then
    alive() { pgrep -f "$marker" >/dev/null 2>&1; }
    reap_strays() { pkill -9 -f "$marker" >/dev/null 2>&1 || true; }
    describe() { pgrep -af "$marker" || true; }
else
    alive() { [ -n "$(proc_matches "300.$nonce")" ]; }
    reap_strays() { local p; for p in $(proc_matches "300.$nonce"); do kill -9 "$p" 2>/dev/null || true; done; }
    describe() { proc_matches "300.$nonce"; }
fi
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

# Kill the parent the way a crash would, and NOTHING MORE. Under MSYS2 that is
# not `kill -9`: MSYS2's SIGKILL of a native process ends its descendants
# itself, so the children vanished whether or not gBASIC had arranged it --
# MEASURED, the tiers stayed green against a build with the Job Object's
# kill-on-close removed, and went red with a bare TerminateProcess. A test of
# "the interpreter's children die with it" cannot let the shell do the killing.
kill_hard() { # pid
    if [ -r "/proc/$1/winpid" ]; then
        taskkill //F //PID "$(cat "/proc/$1/winpid")" >/dev/null 2>&1 || true
    else
        kill -9 "$1" 2>/dev/null || true
    fi
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
        # A failure here must not leave the parent behind: it is still running
        # its loop, and nothing else in this suite would ever stop it.
        [ "$SECONDS" -lt "$deadline" ] || { kill -9 "$parent" 2>/dev/null || true
                                            reap_strays
                                            fail "$1 (the child never started)"; }
        kill -0 "$parent" 2>/dev/null || fail "$1 (parent exited early: $(cat "$scratch/err"))"
        sleep 0.1
    done

    kill_hard "$parent"
    wait "$parent" 2>/dev/null || true

    gone_within 5 || {
        local info
        info="$(describe)"
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

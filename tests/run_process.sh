#!/usr/bin/env bash
# General process API suite (docs/gbasic_native_app_platform_plan.md, NAP-6).
# Exercises `process.run`, a shell-injection-safe synchronous process runner.
#
# Unlike run_native_platform.sh / run_gi.sh, this suite has NO GObject-
# Introspection dependency: `process.run` is an unconditional built-in (fork +
# execvp + pipes, no GI), so it must run and be verified even on hosts without
# libgirepository or the GTK4/GtkSource typelibs. Keeping these cases here rather
# than in run_native_platform.sh is what prevents them from being falsely skipped
# behind that runner's GI gate.
#
# Fixtures that spawn helper scripts reference them by repo-root-relative path
# (e.g. tests/native_platform/helpers/sleep_long.sh), so the child's working
# directory must be the repo root — hence the cd below and running ./gbasic from
# there, exactly as run_native_platform.sh does.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."

make >/dev/null

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

# THESE FIXTURES ARE POSIX BY DESIGN -- nap6_which says so in as many words --
# and a platform without POSIX process semantics answers them CORRECTLY and
# differently: Windows executes no `#!` script, `cwd: "/tmp"` names C:\tmp,
# `which sh` is C:/msys64/usr/bin/sh.exe, and a missing file is "The system
# cannot find the file specified". Asked of the BINARY, by running a fixture
# that needs a `#!` script: where that is refused, the POSIX-shaped cases are
# skipped BY NAME and tests/run_windows_suite.sh holds process.run/start to the
# same shape with gBASIC itself as the child. The cases left run everywhere.
posix_only=""
probe_out="$(./gbasic tests/native_platform/nap6_streams.bas 2>&1 || true)"
if printf '%s' "$probe_out" | grep -q 'not a valid Win32 application'; then
    posix_only=" nap6_streams nap6_argv nap6_bigstreams nap6_binary nap6_cwd nap6_timeout nap6_which
                 plat_proc_basic plat_proc_exit plat_proc_big plat_proc_stop plat_proc_ignore
                 plat_proc_interleave plat_proc_bytes plat_proc_actor
                 negative_nap6_missing negative_plat_proc_missing "
    posix_only=" $(echo $posix_only) "   # one space between names, and around them
fi
posix_skip() { # name -> 0 (and says so) when the case is skipped here
    case "$posix_only" in
        *" $1 "*)
            printf 'SKIP tests/native_platform/%s.bas (POSIX process semantics; run_windows_suite.sh covers this platform)\n' "$1"
            return 0 ;;
    esac
    return 1
}

# --- Positive cases (byte-exact stdout vs sibling .out) --------------------
positive_cases=(
    nap6_basic
    nap6_streams
    nap6_argv
    nap6_bigstreams
    nap6_binary
    nap6_cwd
    nap6_timeout
    nap6_churn
    nap6_which
    nap6_launch_result
    # PLAT-PROC: live child control (process.start/poll/read/wait/stop/release).
    # Every case is deterministic by construction, not by timing: the fixtures that
    # need "output arrived while the child was still running" gate the child on a
    # file the parent creates, so the child provably cannot proceed or exit until
    # the parent says so -- true on a fast host, a loaded host, and under valgrind.
    plat_proc_basic
    plat_proc_exit
    plat_proc_big
    plat_proc_stop
    plat_proc_ignore
    plat_proc_interleave
    plat_proc_bytes
    plat_proc_actor
)

for name in "${positive_cases[@]}"; do
    posix_skip "$name" && continue
    source="tests/native_platform/$name.bas"
    expected="tests/native_platform/$name.out"
    : >"$stdout_file"
    : >"$stderr_file"

    # timeout guards a future regression where a child (or the timeout path
    # itself) hangs: the suite must fail loudly rather than hang forever.
    if timeout 60 ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
        if diff -u "$expected" "$stdout_file"; then
            printf 'PASS %s\n' "$source"
        else
            printf 'FAIL %s\n' "$source"
            exit 1
        fi
    else
        status=$?
        printf 'FAIL %s (exit %d)\n' "$source" "$status"
        cat "$stderr_file"
        exit 1
    fi
done

# --- Negative cases (byte-exact stderr vs sibling .err, nonzero exit) ------
negative_cases=(
    negative_nap6_missing
    negative_nap6_not_record
    negative_nap6_no_command
    negative_nap6_command_type
    negative_nap6_arg_type
    negative_nap6_launch_bad
    negative_plat_proc_unknown
    negative_plat_proc_handle
    negative_plat_proc_start_command
    negative_plat_proc_missing
)

for name in "${negative_cases[@]}"; do
    posix_skip "$name" && continue
    source="tests/native_platform/$name.bas"
    expected="tests/native_platform/$name.err"
    : >"$stdout_file"
    : >"$stderr_file"

    if timeout 60 ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
        printf 'FAIL %s\n' "$source"
        printf 'expected nonzero exit\n'
        exit 1
    fi

    actual_text="$(cat "$stderr_file")"
    expected_text="$(cat "$expected")"
    if [[ "$actual_text" == "$expected_text" ]]; then
        printf 'PASS %s\n' "$source"
    else
        printf 'FAIL %s\n' "$source"
        actual_norm="$(mktemp)"
        expected_norm="$(mktemp)"
        printf '%s\n' "$actual_text" >"$actual_norm"
        printf '%s\n' "$expected_text" >"$expected_norm"
        diff -u "$expected_norm" "$actual_norm" || true
        rm -f "$actual_norm" "$expected_norm"
        exit 1
    fi

    if [[ -s "$stdout_file" ]]; then
        printf 'FAIL %s\n' "$source"
        printf 'expected empty stdout\n'
        cat "$stdout_file"
        exit 1
    fi
done

# --- PLAT-PROC resource accounting (needs `ps` for the zombie count) -------
# Abandoning a handle -- dropping the last reference with no explicit release --
# must leak neither a descriptor nor a zombie, both when the child is already dead
# and when it is still running. The fixture measures the interpreter's own
# /proc/<pid>/fd and its zombie children from a child `sh` (for which gbasic is
# $PPID), as deltas against a baseline so the measuring process.run's own transient
# fds cancel out.
# The fixture counts descriptors by reading /proc/<pid>/fd FROM gBASIC, so this
# tier needs procfs as well as `ps`. It is asked rather than assumed, and SKIPS
# with the reason -- run_all.sh reports a suite that skipped separately from one
# that passed, so a tier that quietly measured nothing could not hide. macOS has
# no procfs and would need proc_pidinfo; a shim returning a plausible count is
# how a resource-accounting tier starts accounting for nothing.
if ! gb_have_proc; then
    printf 'SKIP plat_proc_abandon (no /proc: the fd audit has no portable mechanism yet)\n'
elif [ -n "$posix_only" ]; then
    # MSYS2 HAS a /proc, but it describes MSYS processes: a native interpreter's
    # descriptors are not in it, so the audit would count nothing and pass.
    printf 'SKIP plat_proc_abandon (POSIX process semantics: /proc cannot see a native process here)\n'
elif command -v ps >/dev/null 2>&1; then
    : >"$stdout_file"
    : >"$stderr_file"
    if timeout 120 ./gbasic tests/native_platform/plat_proc_abandon.bas \
            >"$stdout_file" 2>"$stderr_file"; then
        if diff -u tests/native_platform/plat_proc_abandon.out "$stdout_file"; then
            printf 'PASS tests/native_platform/plat_proc_abandon.bas\n'
        else
            printf 'FAIL tests/native_platform/plat_proc_abandon.bas\n'
            exit 1
        fi
    else
        printf 'FAIL tests/native_platform/plat_proc_abandon.bas (exit)\n'
        cat "$stderr_file"
        exit 1
    fi
    # ...and nothing this interpreter started may outlive it: the fixture abandons
    # 25 LIVE children, which teardown must kill rather than orphan onto the host.
    # Each child recorded its own pid, so this checks the exact processes with
    # `kill -0` rather than matching command-line text (which would also match this
    # runner). Retry briefly: teardown's SIGKILL and the reap are not instantaneous.
    pidfile=/tmp/gbasic_plat_proc_abandon.pids
    survivors=""
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        survivors=""
        while read -r pid; do
            [ -n "$pid" ] || continue
            if kill -0 "$pid" 2>/dev/null; then
                survivors="$survivors $pid"
            fi
        done <"$pidfile"
        [ -n "$survivors" ] || break
        sleep 0.2
    done
    if [ -n "$survivors" ]; then
        printf 'FAIL plat_proc_abandon (children survived interpreter exit:%s)\n' "$survivors"
        rm -f "$pidfile"
        exit 1
    fi
    # Loop A's children are signalled the instant they start, so most die before
    # they can record a pid; the file is therefore dominated by loop B -- the
    # LIVE-abandoned children, which are exactly the ones at risk of surviving.
    printf 'PASS plat_proc_abandon (%d recorded child pids, none survived)\n' \
        "$(wc -l <"$pidfile")"
    rm -f "$pidfile"
else
    printf 'SKIP tests/native_platform/plat_proc_abandon.bas (ps not installed)\n'
fi

# --- env, and refusing unknown options (PLAT-PROC, Transward report) --------
#
# The first DAMAGED A DESIGN: passing a password to OpenSSH needs SSH_ASKPASS,
# so with no env option the reporter had to generate a shell wrapper script per
# run -- putting a shell back into the exact code path whose stated principle
# is that nothing is ever parsed as shell syntax. The second is its companion:
# `env:` was silently DROPPED before it existed, so the mistake looked like the
# feature working until the child reported an empty variable. webserver.listen
# refuses unknown options by name for exactly this reason, and the edge is
# sharper here: an ignored option there leaves a server on loopback, here it
# leaves a credential unset.
env_out="$(GB_INHERITED=1 ./gbasic tests/process/env_options.bas 2>&1)"
if printf '%s' "$env_out" | grep -q MISMATCH; then
    printf 'FAIL env_options\n'
    printf '%s\n' "$env_out" | grep MISMATCH
    exit 1
fi
if ! printf '%s' "$env_out" | grep -qx 'mismatches: 0'; then
    printf 'FAIL env_options (did not finish)\n'
    printf '%s\n' "$env_out" | tail -3
    exit 1
fi
printf 'PASS env_options (%s checks: env merged, nothing unsets, unknown options refused by name)\n' \
    "$(printf '%s' "$env_out" | sed -n 's/^checks: //p')"

# --- PLAT-PROC-STDIN: a child you can TALK to ------------------------------
#
# `process.start` used to hand back a live child you could only LISTEN to --
# stdout and stderr were pipes, stdin was inherited -- so a program could start
# a subprocess and never say anything to it. That is why no MCP stdio client
# could be written: the transport is a conversation.
#
# The pipe is OPT-IN and the fixture's control is what keeps the default
# honest: a child has always inherited its parent's stdin, and an interactive
# tool launched by a gBASIC program would stop working if it were piped without
# anyone asking.
stdin_out="$(./gbasic tests/native_platform/plat_proc_stdin.bas 2>&1 || true)"
if printf '%s' "$stdin_out" | grep -q MISMATCH; then
    printf 'FAIL plat_proc_stdin\n'
    printf '%s\n' "$stdin_out" | grep MISMATCH
    exit 1
fi
if ! printf '%s' "$stdin_out" | grep -qx 'mismatches: 0'; then
    printf 'FAIL plat_proc_stdin (did not finish)\n'
    printf '%s\n' "$stdin_out" | tail -3
    exit 1
fi
printf 'PASS plat_proc_stdin (%s checks: a two-way conversation, and the default still inherits)\n' \
    "$(printf '%s' "$stdin_out" | sed -n 's/^checks: //p')"

# --- `process.wait(h, timeout)` ON A CHILD STILL PRODUCING ------------------
#
# The defect this pins was present from the day `process.wait` was written: the
# wait loop polled with a 20ms tick and then did `elapsed_ms += slice`, charging
# the full tick however long `poll` actually blocked -- and poll returns
# IMMEDIATELY when a pipe has data. A child producing continuously therefore cost
# microseconds of real time and 20ms of budget per iteration, so a SIXTY-SECOND
# timeout expired in a few milliseconds. Measured at v0.5.1: 2 MB to each stream,
# `wait(h, 60)` back in 0.02s with 568,000 of 2,000,000 bytes and `running: true`.
#
# WHY THIS SUITE DID NOT CATCH IT, which is the part worth keeping: the UNTIMED
# form was always correct, and the only Linux fixture that waits on a child at all
# (`tests/native_platform/plat_proc_basic.bas`) uses it. Nothing here waited WITH a
# timeout on a busy child. The Windows port's own `tests/windows/process_start.bas`
# was the first thing to do so -- a cross-platform fixture finding a Linux bug --
# and this is the Linux-side counterpart, so the coverage does not depend on a file
# named for another platform.
busy_out="$(./gbasic tests/process/wait_timeout_busy_child.bas 2>&1 || true)"
if printf '%s' "$busy_out" | grep -q MISMATCH; then
    printf 'FAIL wait_timeout_busy_child\n'
    printf '%s\n' "$busy_out" | grep MISMATCH
    exit 1
fi
if ! printf '%s' "$busy_out" | grep -qx 'mismatches: 0'; then
    printf 'FAIL wait_timeout_busy_child (did not finish)\n'
    printf '%s\n' "$busy_out" | tail -3
    exit 1
fi
printf 'PASS wait_timeout_busy_child (%s checks: a timed wait drains a 2MB producer, and still expires)\n' \
    "$(printf '%s' "$busy_out" | sed -n 's/^checks: //p')"

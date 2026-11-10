#!/usr/bin/env bash
# PLAT-STDERR: `print to error` -- a program's route to standard error.
#
# Before this, a gBASIC program had no way to write to fd 2: the runtime wrote its
# diagnostics there but the language offered no surface, so any command-line tool
# written in gBASIC had to interleave its progress and error messages into its data
# on stdout, which breaks ordinary shell composition.
#
# These cases prove the bytes land on fd 2 and nowhere else, that the rendering is
# identical to `print`'s across every value shape, that the stream is governed
# independently of --line-buffered, and that a program writing there leaves the
# runtime's own --json-diagnostics output untouched and still parseable.
#
# Determinism comes from redirection, from process exit, and from a gate file --
# never from a clock. Headless and GI-independent; nothing here needs a display.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -u

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_stderr: build failed\n'
    exit 1
fi

stdout_file=$(mktemp)
stderr_file=$(mktemp)
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

status=0

run_case() {
    src="tests/native_platform/$1.bas"
    exp="tests/native_platform/$1.out"
    : >"$stdout_file"
    : >"$stderr_file"
    if timeout 180 ./gbasic "$src" >"$stdout_file" 2>"$stderr_file" </dev/null; then
        if diff -u "$exp" "$stdout_file"; then
            printf 'PASS %s\n' "$src"
        else
            printf 'FAIL %s\n' "$src"
            status=1
        fi
    else
        printf 'FAIL %s (exit)\n' "$src"
        cat "$stderr_file"
        status=1
    fi
}

run_case plat_stderr_streams     # fd 2 yes, fd 1 no -- from every statement position
run_case plat_stderr_parity      # identical rendering to `print`, every value shape
run_case plat_stderr_edges       # empty argument, embedded newline, interior NUL, UTF-8
run_case plat_stderr_order       # interleaving when both streams share a destination
run_case plat_stderr_buffered    # --line-buffered governs stdout only
run_case plat_stderr_json        # --json-diagnostics unperturbed and still parseable

# --- Direct shell-level separation ---------------------------------------------
# The drivers above read the split through process.run. This asserts it at the
# shell, where a user actually meets it: redirect the two streams to two files and
# require each to hold only its own content.
: >"$stdout_file"
: >"$stderr_file"
./gbasic tests/native_platform/plat_stderr_mixed_child.bas >"$stdout_file" 2>"$stderr_file" </dev/null
if grep -q 'OUT-1' "$stdout_file" && ! grep -q 'ERR-' "$stdout_file" &&
   grep -q 'ERR-1' "$stderr_file" && ! grep -q 'OUT-' "$stderr_file"; then
    printf 'PASS plat_stderr_redirect (each stream holds only its own content)\n'
else
    printf 'FAIL plat_stderr_redirect\n'
    printf -- '--- fd 1 ---\n'; cat "$stdout_file"
    printf -- '--- fd 2 ---\n'; cat "$stderr_file"
    status=1
fi

# Discarding fd 2 must leave fd 1 whole, and vice versa: the shell's ordinary
# composition idioms have to work, since being able to use them is the point.
: >"$stdout_file"
./gbasic tests/native_platform/plat_stderr_mixed_child.bas 2>/dev/null >"$stdout_file" </dev/null
if grep -q 'OUT-last' "$stdout_file" && ! grep -q 'ERR-' "$stdout_file"; then
    printf 'PASS plat_stderr_discard (2>/dev/null keeps stdout intact)\n'
else
    printf 'FAIL plat_stderr_discard\n'
    status=1
fi

# A pipeline consumer must see the data stream only -- the case that motivated this.
piped=$(./gbasic tests/native_platform/plat_stderr_mixed_child.bas 2>/dev/null </dev/null | grep -c '^OUT-')
if [ "$piped" = "5" ]; then
    printf 'PASS plat_stderr_pipeline (a downstream reader sees data only)\n'
else
    printf 'FAIL plat_stderr_pipeline (expected 5 OUT- lines, got %s)\n' "$piped"
    status=1
fi

# --- Memory --------------------------------------------------------------------
if vg_available; then
    if vg_run ./gbasic tests/native_platform/plat_stderr_parity_child.bas \
            >/dev/null 2>"$stderr_file"; then
        printf 'PASS plat_stderr_memory (valgrind clean)\n'
    else
        printf 'FAIL plat_stderr_memory\n'
        tail -40 "$stderr_file"
        status=1
    fi
else
    printf 'SKIP plat_stderr_memory (valgrind not installed)\n'
fi

# A BROKEN PIPE KILLS ONLY THROUGH STDOUT (2026-11-09; DOGFOOD 2026-11-09). A
# diagnostic on a stderr whose reader had gone killed the program by SIGPIPE
# (measured: exit -13, the next statement never ran), which in a detached
# child is a note ending a run. Now the stderr write fails and the program
# goes on; stdout keeps the default, so a program whose stdout reader has
# gone (`| head`) still dies by SIGPIPE, quietly, as before. The shell cannot
# tell a death by signal from an exit of 128+n, so python3 reads the status
# (as run_lock_signal.sh does). A marker file is the evidence the program
# went on, since its stdout is the thing being cut.
if command -v python3 >/dev/null 2>&1; then
    bp=$(mktemp -d)
    printf 'sleep(1)\nprint to error "a note"\nwarning("a warning")\nwrite("%s/lived", "yes")\nprint "out"\n' "$bp" >"$bp/err.bas"
    printf 'sleep(1)\nfor i = 1 to 20000\n    print "line " + string(i)\nnext\nwrite("%s/lived2", "yes")\n' "$bp" >"$bp/out.bas"
    bp_res=$(python3 -I - "$PWD/gbasic" "$bp" <<'PY'
import subprocess, sys, os
g, d = sys.argv[1], sys.argv[2]
p = subprocess.Popen([g, d + "/err.bas"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
p.stderr.close()
out = p.stdout.read()
rc = p.wait()
print("err", rc, os.path.exists(d + "/lived"), out.decode().strip())
p = subprocess.Popen([g, d + "/out.bas"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
p.stdout.close()
rc = p.wait()
print("out", rc, os.path.exists(d + "/lived2"))
PY
)
    rm -rf "$bp"
    case "$bp_res" in
        "err 0 True out"*)
            printf 'PASS a broken stderr fails the write, and the program goes on\n' ;;
        *)
            printf 'FAIL a broken stderr: %s\n' "$(printf '%s' "$bp_res" | head -1)"; status=1 ;;
    esac
    case "$bp_res" in
        *"out -13 False"*)
            printf 'PASS a broken stdout still ends the program by SIGPIPE\n' ;;
        *)
            printf 'FAIL a broken stdout: %s\n' "$(printf '%s' "$bp_res" | tail -1)"; status=1 ;;
    esac
else
    printf 'SKIP broken pipes (no python3 to read a death by signal; nothing measured)\n'
fi

exit "$status"

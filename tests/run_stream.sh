#!/usr/bin/env bash
# PLAT-STREAM: --line-buffered, the opt-in prompt-stdout flag.
#
# When stdout is a pipe, stdio buffers it in blocks, so a gBASIC program that
# prints slowly looks silent to its reader until it exits. --line-buffered flushes
# at every completed line instead. These cases prove the flag does what it claims,
# does not change what the bytes are, and changes nothing when it is absent.
#
# Every case is decided by a gate file or by process exit -- never by a clock. The
# child fixtures publish a READY file only after the print under test has already
# executed, so a read at that instant is provably post-print however slow the host.
#
# Headless and GI-independent; nothing here needs a display. Runs everywhere.
set -u

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_stream: build failed\n'
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
    # stdin at EOF: the partial-line fixture calls `input`, which must not block.
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

run_case plat_stream_stream
run_case plat_stream_partial
run_case plat_stream_volume
run_case plat_stream_json
run_case plat_stream_signal

# --- The flag is opt-in: without it, behaviour is byte-for-byte what it was. ----
# Same program, same bytes, three ways -- and the flag must never reach the
# program as an argument.
: >"$stdout_file"
if ./gbasic tests/native_platform/plat_stream_bulk_child.bas 200 >"$stdout_file" 2>"$stderr_file" </dev/null &&
   ./gbasic --line-buffered tests/native_platform/plat_stream_bulk_child.bas 200 >"$stdout_file.lb" 2>/dev/null </dev/null &&
   cmp -s "$stdout_file" "$stdout_file.lb"; then
    printf 'PASS plat_stream_optin (flagged and unflagged output byte-identical)\n'
else
    printf 'FAIL plat_stream_optin\n'
    status=1
fi
rm -f "$stdout_file.lb"

# A flag-looking argument AFTER the file belongs to the program, not to us.
: >"$stdout_file"
if ./gbasic examples/args_test.bas --line-buffered >"$stdout_file" 2>/dev/null </dev/null &&
   grep -q -- '--line-buffered' "$stdout_file"; then
    printf 'PASS plat_stream_argpass (flag after FILE reaches the program)\n'
else
    printf 'FAIL plat_stream_argpass (flag after FILE was swallowed)\n'
    status=1
fi

# --- Memory: the flag must not change the interpreter's allocation behaviour ---
if vg_available; then
    if vg_run ./gbasic --line-buffered tests/native_platform/plat_stream_bulk_child.bas 500 \
            >/dev/null 2>"$stderr_file"; then
        printf 'PASS plat_stream_memory (valgrind clean with --line-buffered)\n'
    else
        printf 'FAIL plat_stream_memory\n'
        tail -30 "$stderr_file"
        status=1
    fi
else
    printf 'SKIP plat_stream_memory (valgrind not installed)\n'
fi

exit "$status"

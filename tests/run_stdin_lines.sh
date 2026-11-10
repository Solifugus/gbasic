#!/usr/bin/env bash
# `watch w(stdin.lines)` (Qt design §0.38 D3): lines from standard input
# delivered by the event loop, one per delivery, so a program waiting for its
# input keeps serving everything else -- under `qt.run()`, a window that keeps
# painting. Asked for by gBASIC Studio, whose `./studio mcp` could only run
# offscreen while it blocked on input().
#
# THE ORACLE IS THE DRIVER, tests/stdin_lines/drive.py, which writes the input
# and holds every echo to what it wrote, timing it from outside.
#
# Tiers:
#   PIPE  3,000+ lines at once (empty, a tab, UTF-8, NUL and 0xFF bytes, a
#         70,000-byte line, every seventh ended CRLF, the last unterminated):
#         every one back in order, CR removed; `closed` at the end; at most one
#         line per delivery; exit 0, stderr empty.
#   SLOW  a line every 0.4 s: each echo returns before the next is written,
#         under a second, while the input is still open (not all at the end).
#   QT    a window under qt.run(): a line sets a label and the program prints
#         what Qt then holds; a 0.1-second timer kept firing while a line was
#         awaited (at least 8 in 1.5 s); "quit" stops Qt's loop.
#   VALGRIND over the pipe fixture.
#
# Skips without python3; the Qt tier skips without Qt in the build.
. "$(dirname "$0")/portable.sh"
set -u
cd "$(dirname "$0")/.."
source tests/build_has.sh
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_stdin_lines: build failed\n'
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP run_stdin_lines (no python3: nothing drove the input)\n'
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0

tier() {   # name floor
    local name=$1 floor=$2 o n
    o=$(timeout -k 5 120 python3 -I tests/stdin_lines/drive.py "$name" ./gbasic 2>&1)
    n=$(printf '%s\n' "$o" | sed -n 's/^checks: //p')
    if printf '%s\n' "$o" | grep -q '^SKIP'; then
        printf 'SKIP %s (%s)\n' "$name" "$(printf '%s\n' "$o" | sed -n 's/^SKIP //p')"
    elif [ "$(printf '%s\n' "$o" | tail -1)" = "mismatches: 0" ] && [ "${n:-0}" -ge "$floor" ]; then
        printf 'PASS %s (%s checks)\n' "$name" "$n"
    else
        printf '%s\n' "$o" | head -12
        printf 'FAIL %s (floor %s checks)\n' "$name" "$floor"
        status=1
    fi
}

tier pipe 4
tier slow 8
if build_has qt; then
    tier qt 6
else
    printf 'SKIP qt (no Qt in this build)\n'
fi

if vg_available; then
    python3 -I -c 'import sys; sys.stdout.write("".join("line %d\n" % i for i in range(500)) + "end")' >"$work/in"
    if timeout 600 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/stdin_lines/echo.bas' <"$work/in" >"$work/vg.out" 2>"$work/vg"; then
        if [ "$(tail -1 "$work/vg.out")" = "closed; at most 1 line per delivery" ] && [ "$(wc -l <"$work/vg.out")" -eq 502 ]; then
            printf 'PASS valgrind tests/stdin_lines/echo.bas\n'
        else
            printf 'FAIL valgrind: the fixture did not finish under it\n'; status=1
        fi
    else
        printf 'FAIL valgrind\n'; head -20 "$work/vg"; status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status

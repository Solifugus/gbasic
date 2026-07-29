#!/usr/bin/env bash
# PLAT-STRIDX: string access that is not quadratic in the string's size.
#
# gBASIC strings are codepoint-indexed, so a codepoint index has to be
# translated to a byte offset. Two separate costs made that quadratic:
#
#   1. Reading a string VARIABLE deep-copied the whole buffer (env_get ->
#      value_copy -> string_new), so even byte_at(s, i) -- an O(1) operation --
#      cost O(n) per call. A 1 000 000-byte byte_at loop took 30 s.
#   2. mid/left/right/len each walked the entire string to count codepoints,
#      every call, so a per-character scan was O(n^2) on top of that. A
#      256 000-character forward scan took 249 s.
#
# Consequently ANY per-character loop written in gBASIC -- tokenizer, CSV
# reader, template engine -- was quadratic, invisible at example sizes and
# severe at real ones.
#
# Tiers:
#   1. CORRECTNESS -- tests/stridx_test.bas, a golden covering forward,
#      backward, random and alternating access; one-byte and multibyte content;
#      invalid UTF-8; interior NULs; rebinding between accesses; aliasing; and
#      the empty and single-unit cases. This golden was captured BEFORE the
#      change and must not move: the phase changes speed, not semantics.
#   2. SHAPE -- cost as a function of size. Asserts the RATIO across a 4x size
#      step, never an absolute time, with a margin wide enough to survive a
#      loaded machine. Quadratic is ~16x per 4x step; the gate is 8x.
#   3. CEILING -- one very generous absolute bound, sized so that the old
#      implementation could not pass it by any margin of machine speed.
#   4. VALGRIND -- correctness golden under valgrind, including the paths that
#      populate and re-populate the cache.
#
# Headless, GI-independent, no display. Runs everywhere.
set -u

cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_stridx: build failed\n'
    exit 1
fi

out=$(mktemp)
err=$(mktemp)
trap 'rm -f "$out" "$err"' EXIT

status=0

# --- Tier 1: correctness ------------------------------------------------------
if timeout 300 ./gbasic tests/stridx_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u tests/stridx_test.out "$out"; then
        printf 'PASS tests/stridx_test.bas (semantics unchanged)\n'
    else
        printf 'FAIL tests/stridx_test.bas -- string SEMANTICS moved, not just speed\n'
        status=1
    fi
else
    printf 'FAIL tests/stridx_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# --- timing helper ------------------------------------------------------------
# Echoes elapsed seconds (float). Fails loudly if the fixture did not actually
# run: a crashed or no-op run is fast, and must never read as a pass.
timed() { # units kind op -> seconds
    local t0 t1
    t0=$(date +%s.%N)
    if ! timeout 900 ./gbasic tests/stridx_perf.bas "$1" "$2" "$3" >"$out" 2>"$err" </dev/null; then
        printf 'FAIL stridx_perf %s %s %s (exit)\n' "$1" "$2" "$3" >&2
        cat "$err" >&2
        echo "-1"
        return
    fi
    t1=$(date +%s.%N)
    if ! grep -q "^checksum $3 $2 $1 " "$out"; then
        printf 'FAIL stridx_perf %s %s %s (no checksum -- did not run)\n' "$1" "$2" "$3" >&2
        echo "-1"
        return
    fi
    echo "$t1 $t0" | awk '{ printf "%.3f", $1 - $2 }'
}

# ratio gate: t(4n) must be under FACTOR x t(n). A floor keeps a sub-millisecond
# t(n) from making the ratio meaningless on a fast machine.
shape() { # label small large kind op factor
    local label=$1 small=$2 large=$3 kind=$4 op=$5 factor=$6
    local ts tl ratio
    ts=$(timed "$small" "$kind" "$op")
    tl=$(timed "$large" "$kind" "$op")
    if [ "$ts" = "-1" ] || [ "$tl" = "-1" ]; then
        status=1
        return
    fi
    ratio=$(echo "$ts $tl" | awk '{ f = ($1 < 0.25) ? 0.25 : $1; printf "%.2f", $2 / f }')
    if awk -v r="$ratio" -v f="$factor" 'BEGIN { exit !(r <= f) }'; then
        printf 'PASS shape %-22s %ss -> %ss  ratio %sx (gate %sx, quadratic ~16x)\n' \
               "$label" "$ts" "$tl" "$ratio" "$factor"
    else
        printf 'FAIL shape %-22s %ss -> %ss  ratio %sx exceeds %sx -- still superlinear\n' \
               "$label" "$ts" "$tl" "$ratio" "$factor"
        status=1
    fi
}

# --- Tier 2: shape ------------------------------------------------------------
# 4x size steps. A quadratic implementation lands near 16x and cannot pass 8x;
# a linear one lands near 4x and passes with room to spare on a busy machine.
printf -- '-- shape: cost across a 4x size step\n'
shape "forward scan ascii"  50000 200000 ascii scan  8
shape "forward scan multi"  50000 200000 multi scan  8
shape "backward scan ascii" 50000 200000 ascii rscan 8
# The two cases a forward-only cursor cannot serve: every lookup lands before it,
# so these are what the sparse codepoint index exists for. Multibyte on purpose --
# one-byte content is answered by arithmetic and would not exercise the index.
shape "backward scan multi" 50000 200000 multi rscan 8
shape "alternating multi"   50000 200000 multi alt   8
# Larger sizes for byte_at on purpose. The per-call copy this guards against was
# still cache-resident at 200 000 bytes -- the old interpreter scored 4.04x there
# and would have passed -- and only shows its true cost further out: 1.64 s at
# 256 000 against 30.26 s at 1 000 000, an 18x step.
shape "byte_at loop"        250000 1000000 ascii bytes 8
shape "while i < len(s)"    50000 200000 ascii len   8

# --- Tier 3: ceiling ----------------------------------------------------------
# One absolute bound, chosen so the old implementation is nowhere near it. The
# same loop measured 14.93 s at 64 000 characters and 249.33 s at 256 000 before
# this work -- 16x per 4x step -- and at 500 000 it exceeded this harness's own
# 900 s timeout without finishing. The gate is 120 s.
printf -- '-- ceiling: 500 000-character forward scan\n'
t=$(timed 500000 ascii scan)
if [ "$t" = "-1" ]; then
    status=1
elif awk -v t="$t" 'BEGIN { exit !(t <= 120) }'; then
    printf 'PASS ceiling 500000-char scan %ss (gate 120s)\n' "$t"
else
    printf 'FAIL ceiling 500000-char scan %ss exceeds 120s\n' "$t"
    status=1
fi

# --- Tier 4: valgrind ---------------------------------------------------------
if command -v valgrind >/dev/null 2>&1; then
    if valgrind --error-exitcode=9 --leak-check=full --track-fds=yes \
                --errors-for-leak-kinds=definite \
                ./gbasic tests/stridx_test.bas >"$out" 2>"$err" </dev/null; then
        if diff -q tests/stridx_test.out "$out" >/dev/null; then
            printf 'PASS valgrind tests/stridx_test.bas\n'
        else
            printf 'FAIL valgrind tests/stridx_test.bas (output differs under valgrind)\n'
            status=1
        fi
    else
        printf 'FAIL valgrind tests/stridx_test.bas\n'
        cat "$err"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit "$status"

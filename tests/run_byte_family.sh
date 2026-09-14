#!/usr/bin/env bash
# `byte_slice`, `byte_find`, `to_bytes` -- the byte-oriented family.
#
# `byte_at` and `byte_count` shipped alone, and every other string builtin is
# CODEPOINT-oriented, so THE TWO FAMILIES DID NOT COMPOSE. Measured: for a
# string beginning with a two-byte accented character, `find(s, "MARK")`
# answers 1 while the bytes of MARK start at 2 -- so
# `byte_at(s, find(s, "MARK"))` reads the tail of the accent. A binary-format
# reader gets the wrong bytes, silently, with nothing raised.
#
# THE LOAD-BEARING TIER IS THEREFORE COMPOSITION, not any one function: the
# index `byte_find` returns must be the index `byte_slice` and `byte_at` take,
# and the CONTROL beside it shows `find`'s index giving the wrong byte for the
# same string. Every value check in the file passes on a library where the two
# still disagree.
#
# SHAPE is measured because the reason these exist is partly cost: the
# workaround was a loop over `byte_at` reassembling with `from_bytes`, which is
# correct and QUADRATIC since every `+` copies the accumulator. Asserted as a
# RATIO across a 4x size step -- never an absolute time -- with the loop
# required to exceed it, so if the loop ever stops being quadratic this tier
# says so rather than quietly measuring nothing.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
status=0
fail() { printf 'FAIL %s\n' "$1"; status=1; }

# --- Tier 1: semantics --------------------------------------------------------
out="$work/sem.txt"
if ! ./gbasic tests/byte_family_test.bas >"$out" 2>&1; then
    cat "$out"; fail "byte_family_test.bas (exited nonzero)"
else
    ok=1
    if grep -q '^MISMATCH' "$out"; then grep '^MISMATCH' "$out"; fail "byte_family_test.bas"; ok=0; fi
    checks="$(grep -oE '^checks: [0-9]+' "$out" | grep -oE '[0-9]+' || echo 0)"
    if [ "${checks:-0}" -lt 33 ]; then
        fail "byte_family_test.bas (only $checks checks ran; at least 33 are expected)"; ok=0
    fi
    [ "$ok" = 1 ] && printf 'PASS semantics (%s checks)\n' "$checks"
fi

# --- Tier 2: shape ------------------------------------------------------------
cat > "$work/shape.bas" <<'BAS'
program main(args)
    f {file}= "docs/assets/mascot.png"
    raw = read(f)
    small = 32768
    big = 131072
    t0 = monotonic()
    a = ""
    i = 0
    while i < small
        a = a + from_bytes([ byte_at(raw, 100 + i) ])
        i = i + 1
    end while
    t1 = monotonic()
    b = ""
    i = 0
    while i < big
        b = b + from_bytes([ byte_at(raw, 100 + i) ])
        i = i + 1
    end while
    t2 = monotonic()
    loop_small = t1 - t0
    loop_big = t2 - t1
    ratio = 0
    if loop_small > 0 then ratio = loop_big / loop_small
    print "LOOP_RATIO " + string(round(ratio, 2))
    t3 = monotonic()
    c = byte_slice(raw, 100, big)
    t4 = monotonic()
    print "SLICE_SECONDS " + string(round(t4 - t3, 4))
    print "IDENTICAL " + string(c = b)
end program
BAS
if ! ./gbasic "$work/shape.bas" >"$work/shape.txt" 2>&1; then
    cat "$work/shape.txt"; fail "the shape fixture did not run"
else
    ratio="$(grep -oE 'LOOP_RATIO [0-9.]+' "$work/shape.txt" | awk '{print $2}')"
    slice_s="$(grep -oE 'SLICE_SECONDS [0-9.]+' "$work/shape.txt" | awk '{print $2}')"
    same="$(grep -oE 'IDENTICAL (true|false)' "$work/shape.txt" | awk '{print $2}')"
    [ "$same" = "true" ] || fail "byte_slice and the loop disagree about the bytes"
    # THE NEGATIVE CONTROL. A 4x size step on a quadratic loop costs ~16x; if
    # this ever drops to linear the tier is measuring nothing and should say so.
    shape_ok=1
    # THE NEGATIVE CONTROL, and the gate sits where the measurement supports
    # it: at 32K -> 128K the loop was 8.26x and 8.95x across runs, against 4x
    # for linear and 16x for perfectly quadratic. 6x separates the two without
    # sitting on either. Measured at SMALLER sizes the ratio is lower (4.8-5.3x
    # at 8K -> 32K), because the copying has not yet dominated -- which is why
    # the sizes are part of the assertion rather than incidental to it.
    awk -v r="$ratio" 'BEGIN { exit (r > 6) ? 0 : 1 }' \
        || { fail "the byte_at loop grew only ${ratio}x for 4x the bytes; it is no longer quadratic and this tier is vacuous"; shape_ok=0; }
    # And the slice must be nowhere near it.
    awk -v s="$slice_s" 'BEGIN { exit (s < 0.05) ? 0 : 1 }' \
        || { fail "byte_slice took ${slice_s}s for 128KB, which is not a slice"; shape_ok=0; }
    [ "$shape_ok" = 1 ] && printf 'PASS shape (the loop grows %sx for 4x the bytes; byte_slice takes %ss)\n' "$ratio" "$slice_s"
fi

# --- Tier 3: valgrind ---------------------------------------------------------
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    if vg_run ./gbasic tests/byte_family_test.bas >/dev/null 2>"$work/vg.err" </dev/null; then
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

exit $status

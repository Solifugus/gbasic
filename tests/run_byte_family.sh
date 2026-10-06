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
# workaround was a loop over `byte_at` reassembling with `from_bytes`. Asserted
# as the LOOP AGAINST THE SLICE on the same bytes in the same run -- never an
# absolute time and never the loop's growth alone, both of which were tried and
# are recorded at the gate below with what they measured instead.
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
    ' A LINEAR REFERENCE OVER THE SAME TWO SIZES, IN THE SAME PROCESS. `byte_at`
    ' is O(1) since PLAT-STRIDX, so a loop that reads and discards is linear --
    ' the quadratic cost is the `+` accumulation above, and nothing else.
    t5 = monotonic()
    n = 0
    i = 0
    while i < small
        n = n + byte_at(raw, 100 + i)
        i = i + 1
    end while
    t6 = monotonic()
    n2 = 0
    i = 0
    while i < big
        n2 = n2 + byte_at(raw, 100 + i)
        i = i + 1
    end while
    t7 = monotonic()
    loop_small = t1 - t0
    loop_big = t2 - t1
    ref_small = t6 - t5
    ref_big = t7 - t6
    ratio = 0
    if loop_small > 0 then ratio = loop_big / loop_small
    ref = 0
    if ref_small > 0 then ref = ref_big / ref_small
    print "LOOP_RATIO " + string(round(ratio, 2))
    print "REF_RATIO " + string(round(ref, 2))
    t3 = monotonic()
    c = byte_slice(raw, 100, big)
    t4 = monotonic()
    slice_s = t4 - t3
    ' THE FIXTURE COMPUTES THE RATIO, because the shell cannot safely parse the
    ' slice's own time: it is microseconds, `print` emits the shortest
    ' round-trip decimal (PLAT-NUMFMT), and `9.14e-06` matched by a `[0-9.]+`
    ' grep reads as 9.14 -- which is how the first version of this gate reported
    ' "byte_slice took 9.14s". The old gate escaped it only because `round(x, 4)`
    ' flattened the exponent away to 0.0.
    if slice_s > 0 then
        print "LOOP_OVER_SLICE " + string(round(loop_big / slice_s, 0))
    else
        print "LOOP_OVER_SLICE below-clock"
    end if
    print "SLICE_MICROS " + string(round(slice_s * 1000000, 1))
    print "IDENTICAL " + string(c = b)
end program
BAS
if ! ./gbasic "$work/shape.bas" >"$work/shape.txt" 2>&1; then
    cat "$work/shape.txt"; fail "the shape fixture did not run"
else
    ratio="$(grep -oE 'LOOP_RATIO [0-9.]+' "$work/shape.txt" | awk '{print $2}')"
    ref="$(grep -oE 'REF_RATIO [0-9.]+' "$work/shape.txt" | awk '{print $2}')"
    over="$(grep -oE 'LOOP_OVER_SLICE [0-9.]+' "$work/shape.txt" | awk '{print $2}')"
    micros="$(grep -oE 'SLICE_MICROS [0-9.]+' "$work/shape.txt" | awk '{print $2}')"
    same="$(grep -oE 'IDENTICAL (true|false)' "$work/shape.txt" | awk '{print $2}')"
    [ "$same" = "true" ] || fail "byte_slice and the loop disagree about the bytes"
    shape_ok=1
    # THE NEGATIVE CONTROL IS THE LOOP AGAINST THE SLICE, ON THE SAME DATA IN
    # THE SAME RUN -- a correction made 2026-10-03 after the old one produced a
    # FALSE RED in the gate and then turned out to be asserting something this
    # machine cannot see.
    #
    # IT USED TO ASSERT THE LOOP'S ABSOLUTE GROWTH: `> 6` for a 4x size step,
    # calibrated against 8.26x and 8.95x. Under gate load it measured 5.85x and
    # went red; standalone minutes later, 6.56x. So the gate sat INSIDE the
    # run-to-run band -- and the direction is what makes that unacceptable
    # rather than merely unlucky: load inflates the SMALL measurement more than
    # the big one, fixed overhead being a larger share of it, so a busy machine
    # COMPRESSES the ratio toward 1 and this fires exactly when somebody else is
    # building. That is the failure CLAUDE.md names, and the third tier in this
    # tree calibrated on an idle box (run_ari_discover's 180s against a measured
    # 356s; repl_pty's 0.2s silence window).
    #
    # AND MEASURING IT PROPERLY SHOWED THE CLAIM WAS NOT OBSERVABLE HERE AT ALL.
    # A linear reference loop over the same two sizes in the same process
    # (`byte_at` read and discarded, O(1) since PLAT-STRIDX) measures 4.2-4.4x,
    # correctly linear. The accumulating loop measures 5.2-5.7x -- about 1.3x
    # faster than linear, nowhere near the 16x of a quadratic. Across four
    # sizes it is 0.113s / 0.281s / 0.453s, i.e. 4.0x for 4x the bytes: LINEAR.
    #
    # The premise is not wrong, it is undersized. Plain `a = a + "x"` IS still
    # quadratic -- measured at 50K/100K/200K/400K the 2x steps cost 3.31x,
    # 2.67x and 3.79x, trending to a quadratic's 4x -- but in THIS loop the
    # per-iteration cost of `byte_at` plus an array literal plus `from_bytes`
    # dominates the copy until well past 128K, and the fixture's own file is
    # not large enough to reach 256K.
    #
    # SO THE TIER ASSERTS THE CLAIM THAT IS BOTH TRUE AND MEASURABLE: the loop
    # is enormously slower than the slice for the same bytes. Measured three
    # times: 41,717x / 51,968x / 54,777x. The gate is 1000x -- a 40x margin,
    # three orders of magnitude from anything load can do, and both sides scale
    # together so load cancels. The growth ratios are still REPORTED, because
    # they are the number to re-read if somebody makes `+` linear.
    awk -v o="${over:-0}" 'BEGIN { exit (o > 1000) ? 0 : 1 }' \
        || { fail "the loop is only ${over:-?}x the slice for the same 128KB -- under 1000x, so either the slice stopped being a slice or this tier is measuring nothing"; shape_ok=0; }
    # And the slice must be fast in absolute terms too, or "1000x a very slow
    # slice" would pass. 50,000 micros is 0.05s.
    awk -v m="${micros:-999999}" 'BEGIN { exit (m < 50000) ? 0 : 1 }' \
        || { fail "byte_slice took ${micros:-?} microseconds for 128KB, which is not a slice"; shape_ok=0; }
    [ "$shape_ok" = 1 ] && printf 'PASS shape (the loop is %sx the slice, which takes %s micros; growth %sx against a linear reference of %sx)\n' \
        "$over" "$micros" "$ratio" "$ref"
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

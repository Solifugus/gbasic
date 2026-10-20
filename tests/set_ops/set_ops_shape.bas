' THE COST IS THE PRODUCT OF THE TWO LENGTHS, stated as a measurement rather than
' as a comment: each element of the left is compared against the right until a
' match, so doubling BOTH sides is ~4x the work.
'
' WHY IT IS NOT BETTER THAN THAT: membership is `=`, which for records is DEEP
' equality, so there is no key to hash and no order to binary-search. `unique`
' pays the same price for the same reason, and the honest thing is to document it
' and let the caller narrow first.
'
' PRINTS A RATIO, never a time: an absolute bound would be a fact about this
' machine. The runner gates it.

function build(n, base)
    out = []
    i = 0
    while i < n
        append(out, base + i)
        i = i + 1
    end while
    return out
end function

' DISJOINT ON PURPOSE: membership short-circuits on a match, so overlapping data
' measures how early the matches happen rather than the shape. Nothing matches
' here, so every element of the left scans the whole right -- the worst case, and
' the one a caller hits when `excluding` is removing nothing.
function timed(n)
    a = build(n, 0)
    b = build(n, 1000000)
    t0 = monotonic()
    r = a excluding b
    t1 = monotonic()
    return t1 - t0
end function

' INTERLEAVED, AND THE MINIMUM OF THE REPEATS, which a FAILURE forced rather
' than caution: inside the parallel gate this fixture reported ratio 2.02 where
' it reports 4.06 alone, on the same binary, so the suite went red over the
' machine rather than over the cost it measures.
'
' A RATIO IS SELF-NORMALISING ONLY IF BOTH HALVES SEE THE SAME LOAD, and
' measured once each in sequence they need not. THE QUALIFIER IS MEASURED AND IT
' IS NARROWER THAN THE FIRST DIAGNOSIS: steady load does NOT reproduce it --
' under fourteen busy loops the old shape reported 3.6, 4.3, 4.38 and 4.0, all
' comfortably inside the band -- so what skews it is a load TRANSITION between
' the two samples, which is exactly what the end of a parallel gate is as its
' workers drain. The small run is 40ms, which one scheduler stall can double.
'
' SO THE SIZES ALTERNATE -- small, big, small, big -- which puts the two halves
' in the same load window however the load moves, and each size keeps its
' SMALLEST sample, which is the least-contended one and so the honest answer to
' "how much work is there". Measured: 4.01/4.04/4.04 idle, and 4.1/4.12/4.1/3.04
' under the same fourteen hogs. Whole fixture is ~1s.
program main( args )
    ' Warm, so the first allocation is not measured as the growth.
    x = timed(200)
    small = timed(800)
    big = timed(1600)
    reps = 4
    i = 0
    while i < reps
        s = timed(800)
        b = timed(1600)
        if s < small then
            small = s
        end if
        if b < big then
            big = b
        end if
        i = i + 1
    end while
    if small <= 0 then
        small = 0.000001
    end if
    print "small=" + string(round(small, 4)) + "s big=" + string(round(big, 4)) + "s ratio=" + string(round(big / small, 2))
end program

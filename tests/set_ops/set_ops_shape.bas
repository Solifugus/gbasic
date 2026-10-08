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

program main( args )
    ' Warm, so the first allocation is not measured as the growth.
    x = timed(200)
    small = timed(800)
    big = timed(1600)
    if small <= 0 then
        small = 0.000001
    end if
    print "small=" + string(round(small, 4)) + "s big=" + string(round(big, 4)) + "s ratio=" + string(round(big / small, 2))
end program

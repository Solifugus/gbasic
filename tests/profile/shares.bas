' Two functions with the same body, one called three times as often: their
' self times should be in a ratio near 3 (tests/run_profile.sh, TIER shares).
function hot(n)
    s = 0
    for i = 1 to n
        s = s + i
    next
    return s
end function

function cold(n)
    s = 0
    for i = 1 to n
        s = s + i
    next
    return s
end function

program main()
    x = warm()
    for r = 1 to 30
        x = x + hot(150000)
    next
    for r = 1 to 10
        x = x + cold(150000)
    next
    print x
end program

' Called first, and returns the 0 main starts from: under GBASIC_COMPILE the
' first call opens LLVM and starts the JIT before a statement of the callee
' runs, and that one-time cost is charged to the CALLING line (truthfully).
' Paid here, it lands on line 20 rather than in hot's call on line 22.
function warm()
    return 0
end function

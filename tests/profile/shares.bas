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
    x = 0
    for r = 1 to 30
        x = x + hot(40000)
    next
    for r = 1 to 10
        x = x + cold(40000)
    next
    print x
end program

' Recursion: fib is on the stack many times at once, but each sample counts it
' once, so its total stays within 100% (tests/run_profile.sh, TIER shares).
function fib(n)
    if n < 2 then return n
    return fib(n - 1) + fib(n - 2)
end function

program main()
    print fib(21)
end program

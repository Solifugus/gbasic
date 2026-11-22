' Arithmetic and comparisons in a loop, by a count given on the command line
' (tests/run_resolve.sh runs it at N and 2N).
program main()
    n = number(env("RESOLVE_N"))
    s = 0
    for i = 1 to n
        s = s + i * 2 - 1
        if s >= 0 and not (s < 0) then s = s + 0
    next
    print s
end program

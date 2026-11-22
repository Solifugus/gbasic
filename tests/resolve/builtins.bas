' Builtin calls, by a count given on the command line: names early in the
' builtin chain (lower), in the middle (starts_with, mid) and at its end (len,
' max, a native module's verb). tests/run_resolve.sh runs it twice, at N and
' 2N, so what startup and the first call at each site cost cancels out.
program main()
    n = number(env("RESOLVE_N"))
    xs = [3, 1, 2]
    hits = 0
    for i = 1 to n
        s = lower("ABC")
        if starts_with(s, "ab") then hits = hits + 1
        t = mid(s, 2, 1)
        hits = hits + len(t) + max(xs)
        if process.alive(1) then hits = hits + 1
    next
    print hits
end program

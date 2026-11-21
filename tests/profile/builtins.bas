' Builtins as rows of their own (tests/run_profile.sh, TIER builtins): a
' builtin (sort, mod), a native module's verb (process.alive) and a handle's
' method (h.wait(), which is process.wait), each counted at its calls beside
' the program's own counter. sort does the most work, so its self is the
' largest, and the line calling it holds that time too.
function build(n)
    xs = []
    for i = 1 to n
        append(xs, mod(i * 7919, 10007))
    next
    return xs
end function

program main()
    n_sort = 0
    n_alive = 0
    xs = build(100000)
    for r = 1 to 20
        ys = sort(xs)
        n_sort += 1
    next
    me = process.self().pid
    for r = 1 to 3
        a = process.alive(me)
        n_alive += 1
    next
    h = process.start({ command: "true" })
    w = h.wait()
    print "oracle mod " + string(len(xs))
    print "oracle sort " + string(n_sort)
    print "oracle process.alive " + string(n_alive)
    print "oracle process.wait 1"
end program

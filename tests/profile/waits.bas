' A little work, then a second asleep: the waiting row should hold nearly all
' of it, and the `sleep` line almost none (tests/run_profile.sh, TIER waiting).
program main()
    s = 0
    for i = 1 to 1000
        s = s + i
    next
    sleep(1)
    print s
end program

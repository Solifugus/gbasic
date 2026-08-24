' Being a raise, it is catchable -- and a watcher that registers cleanly still
' fires, which is the half that must not regress.
program main( args )
    on error goto next
    watch(nope)
        d = nope * 2
    end watch
    if error then
        print "caught: " + error.source
    end if

    a = 1
    watch(a)
        b = a * 2
    end watch
    print "watcher fired at registration: " + string(b)
    a = 5
    print "and on change: " + string(b)
end program

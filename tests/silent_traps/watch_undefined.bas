' A raise inside a watcher body was dropped by the drain loop: the watcher
' never ran, execution CONTINUED, and the diagnostic only surfaced at exit --
' so the program produced output built on a watcher that had not fired.
program main( args )
    watch(nope)
        d = nope * 2
    end watch
    print "unreachable"
end program

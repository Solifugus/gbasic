' Same when the watched variable exists but the BODY raises.
program main( args )
    a = 1
    watch(a)
        d = undefined_in_body * 2
    end watch
    print "unreachable"
end program

' tests/run_profile.sh: an uncaught raise, or exit(3) with an argument.
function boom(n)
    return 1 / n
end function

program main(args)
    print "before"
    if len(args) > 0 then exit(3)
    print boom(0)
end program

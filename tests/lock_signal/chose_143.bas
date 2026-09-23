' THE OTHER CONTROL, and the one that says WHY this matters: a program that
' really does exit 143 must still be distinguishable from one killed by
' SIGTERM. If the fix were "report every death as a signal" this would go red.
program main(args)
    print "holding"
    exit(143)
end program

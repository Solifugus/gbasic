' tests/run_profile.sh's shape fixture: the whole report, held byte for byte.
function twice(x)
    return x * 2
end function

program main(args)
    s = 0
    for i = 1 to 3
        s = s + twice(i)
    next
    print s
end program

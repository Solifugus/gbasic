' Re-arming is executing `on error goto` again, deliberately.
function rearm()
    on error goto h1
    x = 1 / 0
    return "no"
h1:
    print "first fire"
    on error goto h2
    y = 1 / 0
    return "no2"
h2:
    print "second fire"
    return "done"
end function

program main( args )
    print rearm()
end program

' `print f()` where f exits: the print that called it prints nothing
function f()
    exit(70)
    return 1
end function
print "before"
print f()
print to error f()
print "AFTER main"

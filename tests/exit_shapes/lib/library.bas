load lx
function outer()
    lx.stop_here()
    print "AFTER outer"
end function
print "before"
outer()
print "AFTER main"

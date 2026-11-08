o = { n: 1 }
function o.stop_here()
    exit(70)
    print "AFTER method"
end function
function outer()
    o.stop_here()
    print "AFTER outer"
end function
print "before"
outer()
print "AFTER main"

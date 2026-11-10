function inner()
    on error goto next
    exit(70)
    print "AFTER inner"
end function
function outer()
    inner()
    print "AFTER outer"
end function
print "before"
outer()
print "AFTER main"

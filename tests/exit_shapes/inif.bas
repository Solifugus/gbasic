function inner()
    if true then
        exit(70)
    end if
    print "AFTER inner"
end function
function outer()
    v = inner()
    print "AFTER outer " + string(v)
end function
print "before"
outer()
print "AFTER main"

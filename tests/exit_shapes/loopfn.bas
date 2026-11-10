function outer()
    for i = 1 to 3
        if i = 2 then
            exit(70)
        end if
        print "AFTER i=" + string(i)
    next i
    print "AFTER loop"
end function
print "before"
x = outer()
print "AFTER main"

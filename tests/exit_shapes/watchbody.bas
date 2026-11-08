function deep()
    exit(70)
    print "AFTER deep"
end function
x = 0
watch(x)
    if x = 1 then
        deep()
        print "AFTER in body"
    end if
end watch
print "before"
x = 1
print "AFTER main"

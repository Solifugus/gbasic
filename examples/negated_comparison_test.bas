modifier pass through for compare
    return compare(left, operator, right)
end modifier

if 3 !>= 4 then
    print "3 !>= 4 true"
end if

if 4 !>= 4 then
    print "4 !>= 4 true"
end if

if 5 !>= 4 then
    print "5 !>= 4 true"
end if

if 3 !<= 4 then
    print "3 !<= 4 true"
end if

if 4 !<= 4 then
    print "4 !<= 4 true"
end if

if 5 !<= 4 then
    print "5 !<= 4 true"
end if

n = 3
m = 5

if n(pass through)!>= 4 then
    print "modifier !>= true"
end if

if m(pass through)!<= 4 then
    print "modifier !<= true"
end if

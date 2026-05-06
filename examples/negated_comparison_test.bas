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

if 3(pass through)!>= 4 then
    print "modifier !>= true"
end if

if 5(pass through)!<= 4 then
    print "modifier !<= true"
end if

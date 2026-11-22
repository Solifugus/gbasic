' Every operator the parser makes, each on a case worked by hand, so a wrong
' code in op_code (src/eval.c) answers wrongly here. One line each:
' the operator, then what it gave.
program main()
    a = 7
    b = 3
    print "+ " + string(a + b)
    print "- " + string(a - b)
    print "* " + string(a * b)
    print "/ " + string(12 / b)
    print "neg " + string(-a)
    print "= " + string(a = 7) + " " + string(a = b)
    print "!= " + string(a != b) + " " + string(a != 7)
    print "> " + string(a > b) + " " + string(b > a)
    print "< " + string(a < b) + " " + string(b < a)
    print ">= " + string(a >= 7) + " " + string(b >= a)
    print "<= " + string(a <= 7) + " " + string(a <= b)
    print "!> " + string(a !> b) + " " + string(b !> a)
    print "!< " + string(a !< b) + " " + string(b !< a)
    print "!>= " + string(a !>= 7) + " " + string(b !>= a)
    print "!<= " + string(a !<= 7) + " " + string(a !<= b)
    print "and " + string(true and false) + " " + string(true and true)
    print "or " + string(false or false) + " " + string(false or true)
    print "not " + string(not false)
    print "excluding " + string([1, 2, 3, 2] excluding [2])
    print "intersecting " + string([1, 2, 3] intersecting [3, 1])
    print "try " + string(try 1 / 1)
    print "text + " + "ab" + "cd"
end program

' The counted loop: `for i = a to b [step c] ... end for`.
'
' gBASIC had only `while` and `for each` until this landed, so every counted
' loop was written as a hand-rolled counter -- an idiom that appears 22 times in
' shipped code, including the standard library. This test pins the semantics,
' which are the classic BASIC ones and differ from a C-style `for` in ways that
' are easy to get wrong:
'
'   * `to` is INCLUSIVE.
'   * bounds and step are evaluated ONCE, at loop entry, so changing them inside
'     the body does not move the finish line.
'   * a negative step counts down; the direction decides which comparison ends
'     the loop, so `for i = 5 to 1` with no step simply does not run.
'   * the counter is an ordinary variable and keeps its last value afterwards.

program main(args)
    out = ""
    for i = 1 to 5
        out = out + i + " "
    end for
    print "ascending      : " + out

    out = ""
    for i = 0 to 10 step 2
        out = out + i + " "
    end for
    print "step 2         : " + out

    out = ""
    for i = 5 to 1 step -1
        out = out + i + " "
    end for
    print "step -1        : " + out

    out = ""
    for x = 0 to 1 step 0.25
        out = out + x + " "
    end for
    print "fractional step: " + out

    ' A limit already passed means the body never runs -- it is not a
    ' do-while, and it does not silently count the other way.
    ran = "no"
    for i = 5 to 1
        ran = "yes"
    end for
    print "5 to 1 ran     : " + ran

    ' Bounds are read once. Moving the limit inside the body has no effect,
    ' which is what keeps the loop from depending on its own side effects.
    limit = 3
    out = ""
    for i = 1 to limit
        limit = 99
        out = out + i + " "
    end for
    print "limit fixed    : " + out

    ' The counter is a normal variable: it survives, holding the last value the
    ' body actually saw.
    for i = 1 to 3
    end for
    print "counter after  : " + i

    out = ""
    for i = 1 to 10
        if i = 3 then
            continue
        end if
        if i > 5 then
            break
        end if
        out = out + i + " "
    end for
    print "break/continue : " + out

    total = 0
    for i = 1 to 3
        for j = 1 to i
            total = total + 1
        end for
    end for
    print "nested total   : " + total

    ' Expressions, not just literals, on both ends.
    n = 2
    out = ""
    for i = n - 1 to n * 2
        out = out + i + " "
    end for
    print "expr bounds    : " + out

    ' `for each` is untouched and still means iteration over an array.
    out = ""
    for each v in [10, 20, 30]
        out = out + v + " "
    end for
    print "for each       : " + out
end program

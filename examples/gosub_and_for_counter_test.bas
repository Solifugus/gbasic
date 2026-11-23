' A gosub comes back to the statement after it, wherever it is (DOGFOOD 10),
' and assigning a for counter in the body moves the loop (DOGFOOD 11). Both
' fixed 2026-11-23. Each line printed is one the old interpreter skipped or
' got wrong.

' inside an if: the rest of the branch runs
function in_if()
    if true then
        gosub s
        print "inside the if, after the gosub"
    end if
    print "after the if"
    return 0
s:
    print "in s"
    return
end function

' inside a while: the loop keeps going (it ended after one pass)
function in_while()
    n = 0
    while n < 3
        gosub bump
        print "pass " + string(n)
    end while
    return n
bump:
    n = n + 1
    return
end function

' inside a for, and nested: the subroutine sees the counter
function in_for()
    t = 0
    for k = 1 to 3
        gosub add
    next
    return t
add:
    gosub add_once
    return
add_once:
    t = t + k
    return
end function

' what did not change: a value returned from the subroutine returns from
' the function, a goto stays in the body, running off the end ends it
function returns_value()
    if true then
        gosub s
        print "not printed"
    end if
    return 1
s:
    return 42
end function

function goes_to()
    n = 0
    if true then
        gosub s
        n = n + 100
    end if
    return n
s:
    n = n + 1
    goto back
    n = n + 1000
back:
    n = n + 10
    return
end function

function runs_off()
    if true then
        gosub tail
        print "not printed"
    end if
    return 1
tail:
    print "in tail"
end function

' a break in a subroutine is not a break of the loop around the gosub
function breaks()
    on error goto next
    for i = 1 to 2
        gosub s
        if error then
            print "pass " + string(i) + " refused: " + error.message
        end if
    next
    return i
s:
    break
end function

' the counter moved ahead: passes 1, 5, 9
function skip_ahead()
    for i = 1 to 10
        i = i + 3
        print i
    next
    return i
end function

' and back: the second pass runs three times
function go_back()
    tries = 0
    for i = 1 to 3
        tries = tries + 1
        if i = 2 and tries < 4 then
            i = i - 1
        end if
    next
    return tries
end function

' moved by a continue's step too, and counting down
function down()
    for i = 10 to 1 step -1
        if i = 8 then
            i = 4
            continue
        end if
        print i
    next
    return i
end function

' an exact decimal counter stays exact, at the finer scale
function decimal()
    for x = 0 to 1 step 0.25
        if x = 0.5 then
            x = 0.625
        end if
        print x
    next
    return x = 0.875
end function

' a counter the body made text is refused at the step
function to_text()
    on error goto next
    for i = 1 to 3
        i = "two"
    next
    if error then
        print "refused: " + error.message
    end if
    return 0
end function

program main()
    x = in_if()
    print in_while()
    print in_for()
    print returns_value()
    print goes_to()
    print runs_off()
    print breaks()
    print skip_ahead()
    print go_back()
    print down()
    print decimal()
    print to_text()
end program

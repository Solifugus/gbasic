' Every path out of a statement that invoke_function's loop decides
' (gbrt_after, include/gb_jit.h), and from step 2 every path out of a
' lowered `if` or `while` and the lists inside them (gbrt_list_after): each
' function below takes one, and the runner holds the output of
' GBASIC_COMPILE=all to the interpreter's and to flow.out.

function fact(n)
    if n <= 1 then
        return 1
    end if
    return n * fact(n - 1)
end function

' a goto backwards is a loop; forwards skips
function count_to(n)
    i = 0
again:
    i = i + 1
    if i < n then
        goto again
    end if
    goto finish
    i = -1
finish:
    return i
end function

' gosub twice, and a gosub from inside a gosub
function subs()
    total = 0
    gosub add_one
    gosub add_two
    return total
add_one:
    total = total + 1
    return
add_two:
    gosub add_one
    total = total + 10
    return
end function

' a raise absorbed: the next statement runs
function absorbed()
    on error goto next
    q = 1 / 0
    if error then
        q = -1
    end if
    return q
end function

' a raise sent to a label (the jump acknowledges it, so bare `error` is
' false there and error.message still says what happened)
function handled(x)
    on error goto oops
    y = 10 / x
    return y
oops:
    return "handled: " + error.message
end function

' a goto to a label that does not exist raises, in the caller's view
function lost()
    goto nowhere
    return "not reached"
end function

' an empty body, and one that runs off its end
function empty()
end function

function off_the_end()
    x = 1
end function

' --- step 2: `if` and `while` lowered, and the lists inside them ---

' else if is an `if` in the else list; a while with break and continue
function branches(n)
    total = 0
    i = 0
    while i < n
        i = i + 1
        if i = 2 then
            continue
        else if i = 5 then
            break
        else
            total = total + i
        end if
    end while
    return total
end function

' a return from inside a while inside an if
function find_first(limit)
    if limit > 0 then
        k = 0
        while true
            k = k + 1
            if k * k > limit then
                return k
            end if
        end while
    end if
    return -1
end function

' a goto from a nested list to a body label, and a label inside a nested
' list reached from that list
function nested_goto()
    out = ""
    j = 0
    while j < 3
        j = j + 1
        if j = 2 then
            goto inner
            out = out + "skipped"
inner:
            out = out + "i"
        end if
        if j = 3 then
            goto outer
        end if
        out = out + string(j)
    end while
    out = out + "never"
outer:
    return out
end function

' a raise inside a nested list absorbed there; one sent to a body label
function nested_errors(x)
    on error goto next
    t = 0
    while t < 2
        t = t + 1
        q = 1 / 0
        if error then
            t = t + 10
        end if
    end while
    on error goto caught
    if x then
        q = 1 / 0
    end if
    return t
caught:
    return "caught: " + string(t)
end function

' a named break from a while inside a for leaves the for
function named_break()
    hits = 0
    for i = 1 to 5
        w = 0
        while w < 10
            w = w + 1
            hits = hits + 1
            if i = 2 and w = 3 then
                break i
            end if
        end while
    end for
    return hits
end function

' a named break reaching a lowered while names no loop around it: it
' travels out of the while (gbrt_while_next) and out of the function, and is
' raised at the call
function stray_break()
    n = 0
    while n < 5
        n = n + 1
        if n = 2 then
            break nosuch
        end if
    end while
    return n
end function

' an unknown condition is refused, not false: the else does not run
function refused()
    on error goto next
    marks = ""
    if unknown then
        marks = marks + "then"
    else
        marks = marks + "else"
    end if
    if error then
        marks = marks + "refused"
    end if
    return marks
end function

' a gosub from inside a while: its return resumes AFTER the while, not in
' the loop (DOGFOOD 2026-11-22, open), so this answers 1; held here as the
' interpreter does it, which is all the compiled code may do
function loop_gosub()
    n = 0
    while n < 3
        gosub bump
    end while
    return n
bump:
    n = n + 1
    return
end function

' exit() leaves every frame, here from inside a while inside an if; nothing
' after it runs
function leave_now()
    print "leaving"
    if true then
        while true
            exit(3)
            print "not reached"
        end while
    end if
    print "not reached"
end function

program main()
    print fact(10)
    print count_to(5)
    print subs()
    print absorbed()
    print handled(2)
    print handled(0)
    on error goto next
    r = lost()
    if error then
        print "lost: " + error.message
    end if
    print empty()
    print off_the_end()
    print branches(10)
    print find_first(30)
    print find_first(0)
    print nested_goto()
    print nested_errors(0)
    print nested_errors(1)
    print named_break()
    s = stray_break()
    if error then
        print "stray: " + error.message
    end if
    print refused()
    print loop_gosub()
    leave_now()
    print "not reached"
end program

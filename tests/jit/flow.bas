' Every path out of a statement that invoke_function's loop decides
' (gbrt_after, include/gb_jit.h): each function below takes one, and the
' runner holds the output of GBASIC_COMPILE=all to the interpreter's and to
' flow.out.

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

' exit() leaves every frame; nothing after it runs
function leave_now()
    print "leaving"
    exit(3)
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
    leave_now()
    print "not reached"
end program

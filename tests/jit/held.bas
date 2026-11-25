' Increment 3 (docs/llvm_backend_design.md §4): numbers held in a compiled
' frame. Each function below holds a local the way a compiled body does
' (gbrt_slot_names) and takes one way a held number could part from its
' variable: read before it is set, read by name in the middle of a loop,
' past int64, a decimal, a lock, a watch elsewhere, a raise absorbed, a
' goto, recursion, the counter moved. The runner holds GBASIC_COMPILE=all's
' output, stderr and status to the interpreter's and to held.out.

g = 5
big = 9223372036854775000

' the plain loop: s and i held, i * 2 and s + done on two exact integers
function sum_loop(n)
    s = 0
    for i = 1 to n
        s = s + i * 2
    end for
    return s
end function

' a held name read by an interpreted statement (print) and an interpreted
' expression (a call's argument) in the middle of a loop
function show(x)
    return x * 10
end function

function read_by_name()
    t = 0
    for k = 1 to 3
        t = t + k
        print "t is " + string(t) + ", shown " + string(show(t))
    end for
    return t
end function

' read before it is set: the global g is read, then the local shadows it
' (warning 2104, as interpreted)
function shadowed()
    x = g + 1
    g = x * 2
    return g
end function

' read before it is set and there is nothing: an undefined name raised
function undefined_first()
    on error goto next
    u = u + 1
    if error then
        u = 7
    end if
    return u
end function

' past int64: the sum degrades to the double with 2112, then stays one
function overflowing()
    v = 9223372036854775000
    for j = 1 to 3
        v = v + 500
    end for
    return v
end function

' a product past int64 likewise, and a subtraction below it
function overflow_mul_sub()
    p = 3037000500
    p = p * p
    m = -9223372036854775000
    m = m - 1000
    return string(p) + " " + string(m)
end function

' an exact decimal held: its arithmetic is the runtime's
function decimals()
    d = 0
    for j = 1 to 10
        d = d + 0.1
    end for
    return d = 1
end function

' division makes the double; a held name keeps it
function divided()
    q = 10
    q = q / 4
    q = q * 2
    return q
end function

' unary minus and a comparison chain in conditions
function compared()
    a = 3
    b = -a
    c = 0
    if a > b then
        c = c + 1
    end if
    if a >= 3 then
        c = c + 10
    end if
    if b < -2 then
        c = c + 100
    end if
    if a <= 2 then
        c = c + 1000
    end if
    if a = 3 then
        c = c + 10000
    end if
    if a != 3 then
        c = c + 100000
    end if
    if a !> 3 then
        c = c + 1000000
    end if
    return c
end function

' a while over a held counter, with break and continue
function while_held()
    w = 0
    hits = 0
    while w < 20
        w = w + 1
        if w = 4 then
            continue
        end if
        if w > 9 then
            break
        end if
        hits = hits + w
    end while
    return string(w) + " " + string(hits)
end function

' the counter moved inside the loop, ahead and back, and given a decimal
function counter_moved()
    seen = ""
    for i = 1 to 12
        seen = seen + string(i) + " "
        if i = 2 then
            i = i + 3
        end if
    end for
    for x = 1 to 3
        if x = 2 then
            x = 2.5
        end if
        seen = seen + string(x) + " "
    end for
    return seen + "| " + string(i) + " " + string(x)
end function

' a raise between held writes, absorbed: the write after it is kept
function absorbed_between()
    on error goto next
    r = 1
    r = r + 1
    z = 1 / 0
    if error then
        r = r + 100
    end if
    r = r + 10
    return r
end function

' goto backwards over a held name
function goto_loop()
    n = 0
again:
    n = n + 1
    if n < 5 then
        goto again
    end if
    return n
end function

' recursion: each frame its own slots
function rec(n)
    acc = n
    if n > 0 then
        acc = acc + rec(n - 1)
    end if
    return acc
end function

' a held name written just before it bounds a `for`: the bounds are read
' by name
function bound_held()
    n = 1
    n = n + 2
    t = 0
    for k = 1 to n
        t = t + k
    end for
    return t
end function

' a `while` ended by its condition is no longer a loop: a `break` after it
' is refused
function while_then_break()
    on error goto next
    w = 0
    while w < 3
        w = w + 1
    end while
    break
    if error then
        return "refused: " + error.message
    end if
    return "not refused"
end function

' a parameter assigned is held, read first from the frame's variable
function param_held(n)
    while n > 0
        n = n - 1
    end while
    return n
end function

' a typed parameter keeps its lock: held, since a number lock admits a number,
' and a write the compiled code cannot do itself converts
function typed_param(a:number)
    a = a + 1
    a = a * 2
    return a
end function

' a local declared `:number` is held: the declaration is interpreted (it
' converts "12"), and the slot takes the variable after it
function typed_local()
    y:number = "12"
    for k = 1 to 3
        y = y + k
    end for
    y:number = 100
    y = y * 2
    return y
end function

' compound assignments are interpreted, and the slot takes the variable
' after each
function compound()
    c = 0
    for k = 1 to 4
        c += k
        c = c * 2
        c -= 1
    end for
    return c
end function

' held names written in a `for each`'s and a `do`'s bodies, read back after
' them: run whole by the interpreter until both were lowered, when they had
' to be taken again (stats.logrank's sums, found by the compiled lane)
function interpreted_bodies()
    total = 0
    count = 0
    for each v in [3, 4, 5]
        total = total + v
        count = count + 1
    end for
    d = 0
    do
        d = d + 2
        total = total + 1
    until d >= 6
    return string(total) + " " + string(count) + " " + string(d)
end function

' a name assigned text is not held, and nor is one built from it
function texty()
    t = "a"
    t = t + "b"
    n = 1
    n = n + 1
    return t + string(n)
end function

' the frame's names as reflect sees them
function reflected()
    h = 1
    h = h + 1
    return join(reflect.variables(), ",")
end function

' exit from inside a loop over held names ends the program at once
function exits()
    e2 = 0
    for k = 1 to 5
        e2 = e2 + k
        if e2 > 5 then
            print "leaving at " + string(e2)
            exit(3)
        end if
    end for
    return e2
end function

print sum_loop(1000)
print read_by_name()
print shadowed()
print g
print undefined_first()
print overflowing()
print overflow_mul_sub()
print decimals()
print divided()
print compared()
print while_held()
print counter_moved()
print absorbed_between()
print goto_loop()
print rec(10)
print bound_held()
print while_then_break()
print param_held(5)
print typed_param(4)
print interpreted_bodies()
print typed_local()
print compound()
print texty()
print reflected()

' a watch anywhere: a held write goes the interpreter's way
total = 0
watch total
    print "total is now " + string(total)
end watch
function watched_elsewhere()
    s = 1
    s = s + 1
    return s
end function
print watched_elsewhere()
total = 1

print exits()
print "not reached"

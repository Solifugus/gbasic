' Increment 3, step 3 (docs/llvm_backend_design.md §4): `do ... until`,
' `for each` and unary minus lowered. A compiled `do` makes the calls the
' interpreted one makes (gbrt_while_enter, gbrt_while_next, gbrt_do_truth),
' and reads a false condition inline; a compiled `for each` makes
' gbrt_each_enter, gbrt_each_test and gbrt_each_next, the interpreted
' loop's own; a compiled `-x` negates an exact integer itself and hands
' anything else to gbrt_negate. Each function below takes one way that
' could part from the interpreter. The runner holds GBASIC_COMPILE=all's
' output, stderr and status to the interpreter's and to loops.out.

function noisy(tag, v)
    print "evaluated " + tag
    return v
end function

' a held counter and a held sum, the condition read inline while false
function do_counts()
    i = 0
    s = 0
    do
        i = i + 1
        s = s + i * 2
    until i >= 5
    return string(i) + " " + string(s)
end function

' the body runs once before the condition is asked
function do_once()
    n = 10
    do
        n = n + 1
    until true
    return n
end function

' continue goes to the condition; break leaves with nothing more asked
function do_flow()
    i = 0
    seen = ""
    do
        i = i + 1
        if i = 2 then
            continue
        end if
        if i = 5 then
            break
        end if
        seen = seen + string(i)
    until noisy("until " + string(i), i >= 9)
    return seen + " at " + string(i)
end function

' a return from inside the body leaves the function
function do_returns()
    k = 0
    do
        k = k + 3
        if k > 7 then
            return "returned at " + string(k)
        end if
    until false
    return "not reached"
end function

' a named flow travels through a `do`, which has no name to match
function do_named()
    out = ""
    for x = 1 to 3
        d = 0
        do
            d = d + 1
            if x = 2 then
                continue x
            end if
            if x = 3 and d = 2 then
                break x
            end if
            out = out + string(x) + string(d) + " "
        until d >= 2
    end for
    return out + "after " + string(x)
end function

' a condition that raises, and one that is unknown, each caught
function do_refused()
    on error goto next
    a = 0
    do
        a = a + 1
    until a / 0 + undefined_name
    if error then
        print "condition raised: " + error.message
    end if
    b = 0
    do
        b = b + 1
    until unknown
    if error then
        print "unknown refused: " + error.message
    end if
    return string(a) + " " + string(b)
end function

' a `do` ended by its condition leaves no loop open behind it: a `break`
' after it is outside any loop
function do_depth()
    on error goto next
    i = 0
    do
        i = i + 1
    until i = 2
    break
    if error then
        print "after the do: " + error.message
    end if
    return i
end function

' a condition that is text, read as truth by the runtime
function do_text()
    t = ""
    do
        t = t + "x"
    until len(t) = 3 and "yes"
    return t
end function

' a sum over an array, the element by name, the total held
function each_sum()
    total = 0
    for each v in [3, 4, 5]
        total = total + v
    end for
    return total
end function

' the index, and flow from the body
function each_flow()
    out = ""
    for each w, i in ["a", "b", "c", "d", "e"]
        if i = 1 then
            continue
        end if
        if w = "d" then
            break
        end if
        out = out + w + string(i)
    end for
    return out
end function

' named flow to the outer loop, which frees the inner array on its way
function each_named()
    out = ""
    for each row in [[1, 2], [3, 4], [5, 6]]
        for each cell in row
            if cell = 2 then
                continue row
            end if
            if cell = 5 then
                break row
            end if
            out = out + string(cell)
        end for
    end for
    return out
end function

' a return from inside, twice deep
function each_returns(target)
    for each r in [[1, 2], [3, 4]]
        for each c in r
            if c = target then
                return "found " + string(c)
            end if
        end for
    end for
    return "none"
end function

' the walk is over the array as it began, though the body appends
function each_snapshot()
    xs = [1, 2, 3]
    n = 0
    for each x in xs
        append(xs, x * 10)
        n = n + 1
    end for
    return string(n) + " " + string(len(xs))
end function

' nothing to walk
function each_empty()
    n = 0
    for each e in []
        n = n + 1
    end for
    return n
end function

' what is walked is evaluated by name, after a held name is written
function each_held_bound()
    k = 2
    k = k + 1
    count = 0
    for each q in [k, k * 2]
        count = count + q
    end for
    return count
end function

' not something to walk, and a locked element, each refused and caught
function each_refused()
    on error goto next
    for each v in 5
        print "not reached"
    end for
    if error then
        print "refused: " + error.message
    end if
    z:text = "a"
    for each z in [1, 2]
        print "not reached"
    end for
    if error then
        print "refused: " + error.message
    end if
    return "went on"
end function

' a raise in the body, caught by the function's handler
function each_body_raises()
    on error goto next
    n = 0
    for each v in [1, 0, 2]
        n = n + 10 / v
    end for
    if error then
        print "body raised: " + error.message
    end if
    return n
end function

' a file walked line by line, the line numbers counted from 0, and a
' break part way that must close it
function each_file(path)
    out = ""
    for each line, at in {file}path
        out = out + string(at) + ":" + line + " "
    end for
    for each line in {file}path
        if line = "two" then
            break
        end if
        out = out + "[" + line + "]"
    end for
    return out
end function

' a csv source, each record with where it began
function each_csv()
    out = ""
    for each rec, where in csv.parse("a,b" + chr(10) + "1,2" + chr(10) + "3,4" + chr(10))
        out = out + rec.a + rec.b + "@" + string(where.line) + " "
    end for
    return out
end function

' a typed element and the index held, and the total they feed
function each_typed()
    total = 0
    for each x:number, i in [3, "4", 5]
        total = total + x * i
    end for
    return total
end function

' `-x` on every kind it takes
function negations()
    n = 7
    m = -9223372036854775807 - 1
    big = -m
    d = 0.5
    print -n
    print - -n
    print -(n * 3)
    print -m
    print big = 9223372036854775808
    print -d
    print -1e0 * 2.5
    print -{USD}"2.50"
    print -(90 minutes)
    print is_unknown(-unknown)
    print -noisy("operand", 4)
    return -n + 1
end function

function negation_refused()
    on error goto next
    v = 0
    v = -"text"
    if error then
        print "refused: " + error.message
    end if
    return v
end function

print do_counts()
print do_once()
print do_flow()
print do_returns()
print do_named()
print do_refused()
print do_depth()
print do_text()
print each_sum()
print each_flow()
print each_named()
print each_returns(3)
print each_returns(9)
print each_snapshot()
print each_empty()
print each_held_bound()
print each_refused()
print each_body_raises()
p = temp_file()
write(p, "one" + chr(10) + "two" + chr(10) + "three" + chr(10))
print each_file(p)
delete(p)
print each_csv()
print each_typed()
print negations()
print negation_refused()

' a `do` left by an uncaught raise in its condition
function never()
    c = 0
    do
        c = c + 1
    until c / (c - 2) > 5
    return c
end function
print never()
print "not reached"

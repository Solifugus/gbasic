' Increment 3 (docs/llvm_backend_design.md §4): a plain call lowered. A
' compiled body evaluates a call's arguments itself, by shape, and enters
' the function at the count's check (gbrt_call_enter) and invoke_function's
' frame (gbrt_call_invoke); a parameter the body never assigns is held
' from its first read. Each function below takes one way that could part
' from the interpreter. The runner holds GBASIC_COMPILE=all's output,
' stderr and status to the interpreter's and to calls.out.

function add3(a, b, c)
    return a + b + c
end function

function noisy(tag, v)
    print "evaluated " + tag
    return v
end function

function with_default(a, b = 10)
    return a + b
end function

function typed(n:number)
    return n * 2
end function

function texts(s)
    return s + s
end function

function first_of(xs)
    return xs[0]
end function

function changes(xs)
    xs[0] = 99
    return xs[0]
end function

function fails(x)
    return x / 0 + undefined_name
end function

function deep(n)
    return deep(n + 1)
end function

function leaves(n)
    print "leaving with " + string(n)
    exit(4)
end function

function fib(n)
    if n < 2 then
        return n
    end if
    return fib(n - 1) + fib(n - 2)
end function

' plain calls, nested, as operands and as arguments of calls
function nested()
    t = add3(1, 2, 3)
    t = t + add3(t, add3(1, 1, 1), fib(10))
    return t
end function

' the count refused: too few, too many; absorbed, the call is nothing
function counts()
    on error goto next
    r = 0
    r = add3(1, 2)
    if error then
        print "few: " + error.message
    end if
    r = add3(1, 2, 3, 4)
    if error then
        print "many: " + error.message
    end if
    return r
end function

' an argument raising: the later ones are not evaluated, the body not run
function argument_raises()
    on error goto next
    r = 0
    r = add3(noisy("a", 1), 1 / 0 + undefined_name, noisy("c", 3))
    if error then
        print "abandoned: " + error.message
    end if
    return r
end function

' arguments evaluated left to right, each once
function in_order()
    return add3(noisy("first", 1), noisy("second", 2), noisy("third", 3))
end function

' a default filled, and given
function defaults()
    return string(with_default(1)) + " " + string(with_default(1, 2))
end function

' a parameter's lock converts at the call, or refuses there
function locks()
    on error goto next
    b = 0
    a = typed("21")
    b = typed("x")
    if error then
        print "refused: " + error.message
    end if
    return a
end function

' a read-only parameter holding text, a decimal, an array: not numbers
' held, read as the frame's variable
function not_numbers()
    d = 0.1
    xs = [5, 6]
    r = texts("ab") + string(add3(d, d, d)) + string(first_of(xs))
    y = changes(xs)
    return r + " " + string(y) + " " + string(xs[0])
end function

' builtins and function values are the runtime's
function builtins()
    s = "hello"
    n = len(s) + abs(0 - 3)
    f = add3
    return n + f(1, 1, 1)
end function

' a raise in the callee, uncaught there: re-raised at the call, with the
' callee's frame in the trace
function callee_raises()
    on error goto next
    v = 5
    w = 0
    w = fails(v) + 1
    if error then
        print "raised: " + error.message
        for each frame in error.trace
            print "  at " + frame.name + " line " + string(frame.line)
        end for
    end if
    return v
end function

' too deep: refused, not a crash
function too_deep()
    on error goto next
    x = 0
    x = deep(1)
    if error then
        print "deep: " + string(error.code)
    end if
    return "survived"
end function

' a held number passed and the callee's change not seen
function passes_held()
    k = 0
    m = 0
    for i = 1 to 5
        k = k + add3(i, i, 0)
        m = m - i
    end for
    ' m is written in its slot: a builtin's argument reads it by name
    return string(k) + " " + string(abs(m))
end function

' a parameter the body loops over with `for each` is written by the
' interpreter: never held as one only read
function param_reused(p)
    q = p + 1
    for each p in [10, 20]
        q = q + p
    end for
    return q + p
end function

' an argument registering a function moves the function table: the call
' reads its function's statement before the arguments run
function registers(n)
    r = {}
    function r.m()
        return 1
    end function
    return n
end function

function table_moves()
    t = add3(1, registers(2), 3)
    return t + add3(registers(4), 0, 0)
end function

print nested()
print table_moves()
print add3(1, registers(5), 1)
print counts()
print argument_raises()
print in_order()
print defaults()
print locks()
print not_numbers()
print builtins()
print callee_raises()
print too_deep()
print passes_held()
print param_reused(1)

' a function value shadowed by a builtin's name, called: the builtin, and
' warning 2102 once
function shadow_warned()
    abs = add3
    return abs(0 - 7)
end function
print shadow_warned()

function exits_inside()
    t = 1
    t = t + leaves(t)
    return t
end function
print exits_inside()
print "not reached"

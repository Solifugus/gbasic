' Increment 3, step 4 (docs/llvm_backend_design.md §4): a call through a
' function value, a record's method and a qualified call into a gBASIC
' library are lowered as a plain call is, gbrt_call_enter finding the
' function by eval_call_rest's own steps (call_method_target,
' call_value_target, qualified_call_function). A call the interpreter has not
' yet run past the builtins or the native modules is still evaluated whole,
' and so is a module's verb. Each function below takes one way the compiled
' call could part from the interpreted one. The runner holds
' GBASIC_COMPILE=all's output, stderr and status to the interpreter's and to
' values.out.

load callee from "callee.bas"
load callee from "callee.bas" as cal

function noisy(tag, v)
    print "evaluated " + tag
    return v
end function

function double(x)
    return x * 2
end function

function add_one(x)
    return x + 1
end function

function three(a, b, c)
    return a + b + c
end function

function price(b, ctx = nothing)
    if ctx = nothing then
        return b
    end if
    return ctx.cost * b
end function

' a value called many times, the first run by the interpreter
function v_plain()
    f = double
    s = 0
    for i = 1 to 5
        s = s + f(i)
    end for
    return s
end function

' the variable changes between runs of the same call
function v_reassigned()
    out = ""
    f = double
    for i = 1 to 4
        out = out + string(f(i)) + " "
        if f = double then
            f = add_one
        else
            f = double
        end if
    end for
    return out
end function

' a bound context, copied when bound and appended after the arguments
function v_bound()
    c = { cost: 3 }
    cheap = bound(price, c)
    c.cost = 99
    s = 0
    for i = 1 to 3
        s = s + cheap(i)
    end for
    return string(s) + " " + string(price(5))
end function

' the count refused, plain and bound, the arguments never evaluated; each
' site twice, since a site's first run is the interpreter's
function v_arity()
    on error goto next
    f = three
    g = bound(three, 1)
    for pass = 1 to 2
        r = f(1, noisy("not run", 2))
        if error then
            print "refused: " + error.message
        end if
        r = g(1, 2, 3)
        if error then
            print "refused: " + error.message
        end if
    end for
    return f(1, 2, 3) + g(1, 2)
end function

' the same site holding a number, then a function, then a number
function v_changes_kind()
    on error goto next
    out = ""
    for i = 1 to 3
        if i = 2 then
            h = double
        else
            h = 5
        end if
        r = h(i)
        if error then
            out = out + "[" + error.message + "] "
        else
            out = out + string(r) + " "
        end if
    end for
    return out
end function

' a value wins over a builtin of its name (Matthew, 2026-11-27), each pass
function v_shadowed()
    first = double
    total = 0
    for i = 1 to 2
        total = total + first(10)
    end for
    return total
end function

' an argument raising: the function is not run, the handler goes on
function v_arg_raises()
    on error goto next
    f = noisy
    for pass = 1 to 2
        r = f("never", 1 / 0 + undefined_name)
        if error then
            print "argument raised: " + error.message
        end if
    end for
    return f("then", 7)
end function

' a value passed in, called by the parameter's name, recursively
function apply_n(g, n, x)
    if n = 0 then
        return x
    end if
    return apply_n(g, n - 1, g(x))
end function

function v_param()
    return string(apply_n(double, 5, 1)) + " " + string(apply_n(add_one, 3, 1))
end function

' a library's function as a value
function v_library_value()
    f = callee.twice
    s = 0
    for pass = 1 to 2
        s = s + f(21)
    end for
    return s
end function

' a raise in the callee, caught by the caller's handler
function v_callee_raises()
    on error goto next
    f = callee.refuses
    for pass = 1 to 2
        r = f(1)
        if error then
            print "callee raised: " + error.message
        end if
    end for
    return "went on"
end function

' a method changing `this`, called in a loop
function m_basic()
    account = { balance: 0 }
    function account.deposit(amount)
        this.balance = this.balance + amount
        return this.balance
    end function
    last = 0
    for i = 1 to 4
        last = account.deposit(i)
    end for
    return string(last) + " " + string(account.balance)
end function

' a method calling its sibling through `this`
function m_sibling()
    shape = { w: 3, h: 4 }
    function shape.area()
        return this.w * this.h
    end function
    function shape.twice_area()
        return this.area() * 2
    end function
    return shape.twice_area()
end function

' the method replaced between runs of the same call
function m_replaced()
    r = { n: 1 }
    function r.step()
        return this.n + 10
    end function
    out = ""
    for i = 1 to 3
        out = out + string(r.step()) + " "
        if i = 1 then
            function r.step()
                return this.n + 100
            end function
        end if
        if i = 2 then
            r.step = bound(price, { cost: 1000 })
        end if
    end for
    return out
end function

' an inherited method, `this` the instance
function m_inherited()
    proto = { tag: "P" }
    function proto.who()
        return this.tag
    end function
    a = new proto
    b = new proto with { tag: "B" }
    return a.who() + b.who() + proto.who()
end function

' the count refused, and a raise inside the method
function m_refused()
    on error goto next
    r = { k: 2 }
    function r.scale(x)
        return this.k * x / 0 + undefined_name
    end function
    v = r.scale(1, 2)
    if error then
        print "refused: " + error.message
    end if
    v = r.scale(5)
    if error then
        print "method raised: " + error.message
    end if
    return "went on"
end function

' a record without the field, then with it, at one site
function m_gains_field()
    on error goto next
    out = ""
    r = { v: 7 }
    for i = 1 to 3
        if i = 3 then
            function r.get()
                return this.v
            end function
        end if
        x = r.get()
        if error then
            out = out + "[" + error.message + "] "
        else
            out = out + string(x) + " "
        end if
    end for
    return out
end function

' a handle's method is its module's verb, which the interpreter runs
function m_handle()
    out = ""
    for i = 1 to 2
        h = process.start({ command: "true", args: [] })
        s = h.wait()
        out = out + string(s.exit_code) + " "
        h.release()
    end for
    return out
end function

' a library's function, in a loop, calling its own functions
function q_basic()
    s = 0
    for i = 1 to 3
        s = s + callee.twice(i) + callee.via_own(i) + callee.via_value(i)
    end for
    return s
end function

' an alias, and the library's own name, reach one library
function q_alias()
    s = 0
    for pass = 1 to 2
        s = s + cal.twice(4) + callee.twice(4)
    end for
    return s
end function

' a record variable named as the library: its method wins; without one, the
' library is reached
function q_record_named_callee()
    callee = { n: 5 }
    first = 0
    for pass = 1 to 2
        first = first + callee.twice(1)
    end for
    function callee.twice(x)
        return this.n * x * 100
    end function
    return string(first) + " " + string(callee.twice(1))
end function

' the same site with a record named as the library, which reaches the
' library, then a handle, whose verb wins over the library's function
function q_handle_named_callee()
    out = ""
    for i = 1 to 3
        if i = 2 then
            callee = process.start({ command: "true", args: [] })
            s = callee.wait()
        else
            callee = { n: i }
        end if
        r = callee.poll()
        if i = 2 then
            out = out + "handle running " + string(r.running) + "; "
            callee.release()
        else
            out = out + r + "; "
        end if
    end for
    return out
end function

' a function the library does not define, and a native module's verb
function q_refused()
    on error goto next
    for i = 1 to 2
        r = callee.nonesuch(i)
        if error then
            print "refused: " + error.message
        end if
    end for
    for pass = 1 to 2
        r = callee.twice()
        if error then
            print "refused: " + error.message
        end if
    end for
    return csv.line(["a", "b,c"])
end function

' `this` outside a method
function q_this_outside()
    on error goto next
    for pass = 1 to 2
        r = this.thing()
        if error then
            print "refused: " + error.message
        end if
    end for
    return "went on"
end function

print v_plain()
print v_reassigned()
print v_bound()
print v_arity()
print v_changes_kind()
print v_shadowed()
print v_arg_raises()
print v_param()
print v_library_value()
print v_callee_raises()
print m_basic()
print m_sibling()
print m_replaced()
print m_inherited()
print m_refused()
print m_gains_field()
print m_handle()
print q_basic()
print q_alias()
print q_record_named_callee()
print q_handle_named_callee()
print q_refused()
print q_this_outside()

' a method left by an uncaught raise
function never()
    r = { d: 0 }
    function r.divide(x)
        return x / this.d + undefined_name
    end function
    return r.divide(1)
end function
print never()
print "not reached"

' `bound(fn, context)` -- a function value that carries an explicit context.
'
' WRITTEN BEFORE THE IMPLEMENTATION, from the design, as PLAT-ERR's fixtures
' were. Until `bound` exists every check here fails, which is the point: a
' fixture that passes against a missing feature is testing nothing.
'
' WHAT IT IS FOR. gBASIC has no closures -- deliberately, since a captured
' environment admits reference cycles in a refcounted runtime and cannot cross
' `spawn`'s fork+exec -- so a callback could not carry state. Measured, that
' cost two workarounds in this tree:
'
'   examples/automation_lab/08_what_price.bas   "the 10.00 is written in", so
'       pricing a second product means a second identical function
'   stdlib/datagrid.bas   the GTK handler reaches its grid through a
'       program-global `_DATAGRID` registry, which the LIBRARY cannot create,
'       so every caller must declare it
'
' AND IT IS NOT A CLOSURE. The context is NAMED, and COPIED BY VALUE at bind
' time. A closure captures whatever a name happens to mean, by reference; this
' captures a record, and a gBASIC record is a value, so nothing cyclic is
' constructible. That is the property the no-closures rule exists to protect,
' and it is why this is affordable where a closure is not.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' The model shape from recipe 10, with the constant lifted out of the body.
function price_star(b, ctx)
    return ctx.cost * b / (1 + b)
end function

' A plain one-argument function, for the control that nothing else moved.
function double_it(x)
    return x * 2
end function

' A local higher-order function that knows NOTHING about contexts -- it calls
' its callback with one argument, exactly as `decision.quantity` does. This is
' the generality claim: a library written before `bound` existed must work with
' a bound value and need no change.
function greet(who, ctx)
    return ctx.greeting + ", " + who
end function

function apply_at(f, x)
    return f(x)
end function

program main( args )
    print "-- the context arrives, and the callee is unchanged"
    cheap = bound(price_star, { cost: 10.0 })
    check("bound through a plain higher-order function", apply_at(cheap, 0 - 2), 20)

    print "-- THE LOAD-BEARING DIFFERENCE: two contexts, two answers"
    ' Without this, "it works" is satisfied by a mechanism that accepts a
    ' context and ignores it -- every other check would still pass.
    dear = bound(price_star, { cost: 22.5 })
    a = apply_at(cheap, 0 - 2)
    b = apply_at(dear, 0 - 2)
    check("same function, cheap context", a, 20)
    check("same function, dear context", b, 45)
    if a = b then
        print "MISMATCH the context is being ignored: both gave " + string(a)
    else
        print "ok   the two contexts give different answers"
    end if

    print "-- THE CONTEXT IS COPIED AT BIND TIME, not referenced"
    ' Someone will expect reference semantics and mutate the original. This
    ' pins the answer rather than leaving it to be discovered.
    c = { cost: 10.0 }
    held = bound(price_star, c)
    c.cost = 99.0
    check("mutating the original is not seen", apply_at(held, 0 - 2), 20)

    print "-- A BOUND FUNCTION IN A RECORD FIELD, CALLED AS A METHOD"
    ' This path routes through the same bound-aware call, so it carries the
    ' context -- and refusing here would be arbitrary when a plain call works.
    ' TWO OBJECTS, ONE FUNCTION, DIFFERENT CONTEXTS is the shape worth having:
    ' a configured object without closures.
    o = { say: bound(greet, { greeting: "Hello" }) }
    q = { say: bound(greet, { greeting: "Hi" }) }
    check("bound method, first context", o.say("world"), "Hello, world")
    check("bound method, second context", q.say("world"), "Hi, world")

    print "-- CONTROLS: nothing else moved"
    check("a plain function value still works", apply_at(double_it, 21), 42)
    check("a bound value is still a function", type(cheap), "function")
    check("a plain value is still a function", type(double_it), "function")

    print "-- REFUSALS"
    on error goto next
    x = bound(double_it)
    if error then
        print "ok   refused: no context"
    else
        print "MISMATCH bound with no context was accepted"
    end if
    error.clear()
    y = bound(42, { a: 1 })
    if error then
        print "ok   refused: first argument is not a function"
    else
        print "MISMATCH bound(42, ...) was accepted"
    end if
    error.clear()
    ' ONE CONTEXT, NAMED. Appending two silently would make the arity depend on
    ' how many times a value had been bound, which nothing at the call site shows.
    z = bound(cheap, { cost: 1.0 })
    if error then
        print "ok   refused: re-binding an already bound function"
    else
        print "MISMATCH bound(bound(...)) was accepted"
    end if
    error.clear()
    ' encode REFUSES a function value, and a bound one is a function value --
    ' asserted so the new kind of value cannot quietly become encodable.
    e = encode({ fn: cheap })
    if error then
        print "ok   encode still refuses a bound function"
    else
        print "MISMATCH a bound function became encodable"
    end if
    error.clear()
    ' `serialize` IS DIFFERENT FROM `encode` HERE and the difference was
    ' measured, not assumed: `encode` refuses a function value outright, while
    ' `serialize` ACCEPTS one and writes it by NAME (35 bytes for
    ' `serialize({f: dbl})`). So a bound value raises a question `encode` never
    ' poses -- the name travels, but does the context?
    '
    ' REFUSED FOR NOW, with the reason, rather than guessed: a context may hold
    ' anything, including values that cannot be serialized at all, and silently
    ' dropping it would hand back a function that had quietly lost what made it
    ' different from the unbound one. Carrying an encodable context is a later
    ' increment; refusing is the answer that cannot be wrong.
    sb = serialize({ fn: cheap })
    if error then
        print "ok   serialize refuses a bound function, for now"
    else
        print "MISMATCH serialize silently accepted a bound function"
    end if
    error.clear()
    ' THE CONTROL: a PLAIN function value must still serialize, or "bound is
    ' refused" would be satisfied by breaking function values generally.
    sp = serialize({ fn: double_it })
    if error then
        print "MISMATCH serialize stopped accepting a plain function: " + error.message
    else
        print "ok   CONTROL a plain function value still serializes"
    end if
end program

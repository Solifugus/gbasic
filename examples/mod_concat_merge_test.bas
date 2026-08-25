' mod / concat / merge — DOGFOOD ledger items 1, 3 and 10.
'
' All three are BUILTINS rather than operators. `%`, array `+` and record `+`
' are separate decisions the ledger deliberately kept apart: `%` is lexer work,
' and `+` on a container is a semantics question (concatenate, or add
' element-wise?) that should not be settled as a side effect of adding a
' convenience.
program main( args )
    print "-- mod: FLOORED, so the result takes the sign of the DIVISOR"
    print string(mod(7, 3)) + " " + string(mod(-7, 3)) + " " + string(mod(7, -3)) + " " + string(mod(-7, -3))
    print "cyclic indexing is the point: " + string(mod(-1, 5))
    print "reals: " + string(mod(7.5, 2))
    print "matches the hand-rolled idiom: " + string(mod(-7, 3) = -7 - floor(-7 / 3) * 3)

    print ""
    print "-- concat: arrays, variadic, a NEW array"
    a = [1, 2]
    b = [3]
    c = concat(a, b, [4, 5])
    print string(c)
    print "sources untouched: " + string(a) + string(b)
    print "empty pieces are fine: " + string(concat([], [1], []))
    print "one argument is a copy: " + string(concat(a))

    print ""
    print "-- merge: records, variadic, LATER WINS, a NEW record"
    x = { a: 1, b: 2 }
    y = { b: 20, c: 3 }
    m = merge(x, y)
    print "a=" + string(m.a) + " b=" + string(m.b) + " c=" + string(m.c)
    print "sources untouched: b=" + string(x.b)
    print "three-way: " + string(merge({ k: 1 }, { k: 2 }, { k: 3 }).k)

    print ""
    print "-- and the thing merge was logged for: composing onto a library result"
    base = { status: 200, body: "ok" }
    full = merge(base, { id: 7 })
    print "status=" + string(full.status) + " id=" + string(full.id)
end program

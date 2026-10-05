' `key(a, b, ...)` -- ONE STRING THAT CANNOT COLLIDE.
'
' SELF-CHECKING, NOT GOLDEN, and here that is forced: every defect this
' function prevents is TWO KEYS THAT LOOK FINE AND ARE EQUAL. A golden records
' whatever came out, so it would happily pin a collision as the expected
' answer -- which is precisely how the defect survived in `insight` and
' `fundamentals`.
'
' EACH CHECK IS A DIFFERENCE, never a value: what matters is that two distinct
' inputs produce two distinct keys, and asserting the exact encoding would pin
' an implementation detail while proving nothing about the property.
'
' THE CONTROLS ARE LOAD-BEARING. "distinct inputs give distinct keys" is
' satisfied by a function returning a fresh random string every call, which
' would be useless -- so equal inputs must give EQUAL keys, and absences must
' group TOGETHER, which is what SQL's GROUP BY does with NULL and what
' `fundamentals` was relying on without saying so.

function same(label, a, b)
    if a = b then
        print "COLLISION " + label
    else
        print "ok   distinct: " + label
    end if
    return nothing
end function
function eq(label, a, b)
    if a = b then
        print "ok   " + label
    else
        print "WRONG " + label
    end if
    return nothing
end function

program main( args )
    print "-- the separator-in-data case that merged two insight cells"
    same("(North|East, A) vs (North, East|A)", key("North|East", "A"), key("North", "East|A"))
    print "-- the absence case that made fundamentals group by accident"
    same("absent vs the string 'nothing'", key("2023-12-31", nothing), key("2023-12-31", "nothing"))
    same("unknown vs nothing", key(unknown), key(nothing))
    print "-- the kind tag, without which true and \"true\" collide"
    same("true vs \"true\"", key(true), key("true"))
    same("1 vs \"1\"", key(1), key("1"))
    print "-- prefix ambiguity"
    same("(ab, c) vs (a, bc)", key("ab", "c"), key("a", "bc"))
    same("(a, '') vs (a)", key("a", ""), key("a"))
    print "-- CONTROLS: equal inputs must give equal keys"
    eq("stable", key("a", 1, true), key("a", 1, true))
    eq("absences group together", key(nothing), key(nothing))
    print "-- a value containing the ENCODING'S OWN SYNTAX"
    ' THIS CASE TESTS THE LENGTH PREFIX AND NOTHING ELSE DOES, found by
    ' perturbation: with the kind tags kept but the lengths removed, every
    ' other check in this file still passed, because `s:a` + `s:b` and
    ' `s:` + `as:b` differ. They stop differing when a component CONTAINS the
    ' tag syntax -- `key("a","b")` and `key("as:b")` both become `s:as:b`.
    ' A separator is only safe until the data contains it, which is the whole
    ' argument for length-delimiting instead.
    same("(a, b) vs (as:b)", key("a", "b"), key("as:b"))
    same("(x) vs (s1:x)", key("s1:x"), key("x"))
    print "-- a NUL survives (PLAT-NUL)"
    same("a<NUL>b vs ab", key("a" + chr(0) + "b"), key("ab"))
    print "-- shape"
    print "key(\"North\", 7) = " + quote(key("North", 7))
end program

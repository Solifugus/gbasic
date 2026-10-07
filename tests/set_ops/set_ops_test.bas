' `a excluding b` and `a intersecting b` -- set difference and intersection as
' INFIX WORD OPERATORS, plus `last(a)` and `slice(a, at [, count])`.
'
' WHY AN OPERATOR AND NOT A FUNCTION: the spelling was decided by MEASUREMENT,
' because this project rejected `IDENT expression` as a statement form over FOUR
' shift/reduce conflicts and the design offered `excluding(a, b)` as the fallback
' if the operator was expensive. Measured: the operator costs **ZERO** conflicts
' at its own precedence level between comparison and additive. So the English
' reading was free and the function form was not needed.
'
' MEMBERSHIP IS `array_find_index`, the same authority `contains`, `find` and
' `remove_value` use. PLAT-EQ's sweep established that six routes ask "is this
' value present" and must agree; these are the seventh and eighth, and they agree
' BY CONSTRUCTION -- which is the only way that property survives an edit. The
' ORACLE below asserts it anyway, through `contains`, because "by construction"
' is a claim about today's source.
'
' SELF-CHECKING, NOT GOLDEN, and forced: every defect here is a PLAUSIBLE LIST.
' An operation that deduplicated, or reordered, or compared records by identity
' instead of by value, returns something that still looks like the answer.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' AN ORACLE THROUGH A DIFFERENT ROUTE: `contains`, which is the membership
' question asked by the one API that already existed. A second implementation,
' not a second call into the operator.
function oracle_excluding(a, b)
    out = []
    for each x in a
        if not contains(b, x) then
            append(out, x)
        end if
    end for
    return out
end function

function oracle_intersecting(a, b)
    out = []
    for each x in a
        if contains(b, x) then
            append(out, x)
        end if
    end for
    return out
end function

program main( args )
    a = [1, 2, 3, 2, 4]
    b = [2, 4]

    print "-- the two operations"
    check("excluding                ", a excluding b, [1, 3])
    check("intersecting             ", a intersecting b, [2, 2, 4])

    print "-- the LEFT side's order and duplicates survive"
    ' These are FILTERS over `a`, not set-theoretic sets: `unique` exists
    ' separately and composes, so folding it in would take away the choice.
    check("duplicates kept          ", [3, 1, 3] intersecting [3], [3, 3])
    check("order is a's             ", [9, 1, 5] excluding [1], [9, 5])
    check("and unique composes      ", unique([3, 1, 3] intersecting [3]), [3])

    print "-- membership is BY VALUE, which PLAT-EQ made true of records"
    check("records by value         ", [{x:1},{x:2}] excluding [{x:2}], [{x:1}])
    check("a nested record          ", [{p:{q:1}}] intersecting [{p:{q:1}}], [{p:{q:1}}])
    check("arrays as elements       ", [[1,2],[3]] excluding [[3]], [[1,2]])
    ' AND THE ABSENCES, which `find`/`contains` were taught to match in PLAT-EQ.
    check("unknown is a value       ", [1, unknown, 2] excluding [unknown], [1, 2])
    check("nothing is a value       ", [1, nothing] intersecting [nothing], [nothing])
    ' A NUMBER IS NOT A STRING, the defect PLAT-EQ's scalar instalment fixed.
    check("0 is not \"stop\"          ", [0] excluding ["stop"], [0])

    print "-- THE ORACLE: `contains` must agree, being the seventh route"
    check("excluding vs contains    ", a excluding b, oracle_excluding(a, b))
    check("intersecting vs contains ", a intersecting b, oracle_intersecting(a, b))
    recs = [{x:1},{x:2},{x:3},{x:2}]
    check("records vs contains      ", recs excluding [{x:2}], oracle_excluding(recs, [{x:2}]))
    ' A WIDER BODY, so agreement is not luck on five elements.
    wide = []
    half = []
    i = 0
    while i < 60
        append(wide, mod(i * 7, 23))
        if mod(i, 3) = 0 then
            append(half, mod(i, 23))
        end if
        i = i + 1
    end while
    check("60 elements vs contains  ", wide excluding half, oracle_excluding(wide, half))
    check("60 intersect vs contains ", wide intersecting half, oracle_intersecting(wide, half))

    print "-- empty operands"
    check("nothing to exclude       ", a excluding [], a)
    check("exclude from nothing     ", [] excluding b, [])
    check("intersect with nothing   ", a intersecting [], [])
    check("everything excluded      ", [2, 4] excluding b, [])

    print "-- chaining and precedence"
    check("chained excluding        ", [1,2,3,4,5] excluding [2] excluding [4], [1, 3, 5])
    check("mixed operations         ", [1,2,3] intersecting [2,3] excluding [3], [2])
    ' BINDS TIGHTER THAN COMPARISON, so the whole set expression is the operand.
    check("tighter than `=`         ", a excluding b = [1, 3], true)
    ' AND LOOSER THAN `+`, so arithmetic on the operands happens first.
    check("looser than `+`          ", [1, 2] excluding [1 + 1], [1])

    print "-- `last(a)`, which simply did not exist"
    ' `first` has existed since arrays did; reading the END meant
    ' `a[count(a) - 1]` -- arithmetic on a length, where an off-by-one lives, and
    ' an out-of-range read on an empty list rather than an answer.
    check("last                     ", last(a), 4)
    check("last of one              ", last([7]), 7)
    check("last of empty is nothing ", last([]), nothing)
    check("and `first` agrees       ", first([]), nothing)
    check("last of records          ", last([{n:1},{n:2}]).n, 2)
    ' `take_last` POPS and is a different thing; nothing read the end without
    ' removing it. Asserted as a DIFFERENCE, or `last` would be a second spelling.
    popme = [1, 2, 3]
    gone = take_last(popme)
    check("take_last removes        ", popme, [1, 2])
    keep = [1, 2, 3]
    held = last(keep)
    check("last does not            ", keep, [1, 2, 3])

    print "-- `slice(a, at [, count])`, with byte_slice's own conventions"
    ' THE SAME SHAPE AND THE SAME 0-BASED `at` as `byte_slice(s, at [, count])`,
    ' deliberately: a second convention for the same idea one type along is how an
    ' off-by-one gets written.
    check("slice at, count          ", slice(a, 1, 2), [2, 3])
    check("slice to the end         ", slice(a, 3), [2, 4])
    check("0-based like byte_slice  ", slice([10,20,30], 1, 2), [20, 30])
    check("and byte_slice agrees    ", byte_slice("hello", 1, 2), "el")
    ' CLAMPED, NOT REFUSED: a window legitimately runs past the end, which is what
    ' the last page of a report is.
    check("count past the end       ", slice(a, 3, 99), [2, 4])
    check("start at the end         ", slice(a, 5, 2), [])
    check("start past the end       ", slice(a, 99, 2), [])
    check("a zero count             ", slice(a, 1, 0), [])
    check("the whole list           ", slice(a, 0), a)
    check("and it is a COPY         ", count(slice(a, 0)), count(a))
    sliced = slice(a, 0)
    sliced[0] = 99
    check("writing the copy         ", a[0], 1)

    print "-- REFUSALS"
    on error goto next
    x = a excluding 5
    check("a number on the right    ", contains(error.message, "`excluding` works on two lists, got an array and a number"), true)
    error.clear()
    x = 5 intersecting a
    check("a number on the left     ", contains(error.message, "`intersecting` works on two lists, got a number and an array"), true)
    error.clear()
    ' TEXT IS REFUSED RATHER THAN TREATED AS A LIST OF CHARACTERS: `contains` on a
    ' string asks a different question with the same shape, and guessing which was
    ' meant is how a set operation silently becomes a substring search.
    x = "ab" excluding "b"
    check("text is not a list       ", contains(error.message, "works on two lists, got a string and a string"), true)
    error.clear()
    x = slice("abc", 0, 1)
    check("slice names the remedy   ", contains(error.message, "use byte_slice or mid for text"), true)
    error.clear()
    x = slice(a, -1)
    check("a negative start         ", contains(error.message, "whole numbers and not negative"), true)
    error.clear()
    x = slice(a, 1.5)
    check("a fractional start       ", contains(error.message, "whole numbers and not negative"), true)
    error.clear()
    x = slice(a, 0, -2)
    check("a negative count         ", contains(error.message, "whole numbers and not negative"), true)
    error.clear()
    x = last(5)
    check("last of a number         ", contains(error.message, "last expects an array"), true)
    error.clear()
    x = slice(a)
    check("slice needs a start      ", contains(error.message, "a list, a start and an optional count"), true)
    error.clear()

    print "-- the reserved words cost two names, and only two"
    ' `excluding` and `intersecting` are keywords now. MEASURED before taking
    ' them: no identifier in this tree was named either (three matches, all in
    ' prose). What is claimed is the name of a variable, parameter or function --
    ' and NOT a record field, because the keyword-field rules already cover that,
    ' which is what makes the price affordable.
    r = { excluding: 1, intersecting: 2 }
    check("a field may be named so  ", string(r.excluding) + string(r.intersecting), "12")
    check("and read by subscript    ", r["excluding"], 1)
    check("and keys() lists it      ", contains(keys(r), "excluding"), true)
end program

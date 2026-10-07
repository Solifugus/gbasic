' §2a and §2b: WHAT AN AGGREGATE DOES WITH AN ABSENCE, `present(a)`, and the
' three defects `mode` shipped with.
'
' EVERY AGGREGATE USED TO REFUSE a list with a hole in it (`sum expects a numeric
' array`), which made §2's projection useless on the first ragged row -- and real
' business data IS ragged.
'
' SQL'S ANSWER, WITH SQL'S WARNING, and both halves are a standard rather than a
' convention: SUM ignores NULLs, AVG divides by the NON-NULL count, and the SQL
' standard raises SQLSTATE 01003, "null value eliminated in set function", class
' 01 being the warning class. PostgreSQL does not emit it. gBASIC does, as 2111.
'
' SELF-CHECKING, NOT GOLDEN, AND FORCED: every defect here is a PLAUSIBLE NUMBER.
' A `mean` that divides by the full count is silently too low on every average; a
' `mode` that returns the first element of a price list looks exactly like a real
' mode; an all-absent total reported as 0 reads as "they bought nothing".

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main( args )
    print "-- SKIP, which is SQL's answer"
    on warning ignore
    check("sum ignores the absence  ", sum([10, unknown, 7]), 17)
    check("and `nothing` alike      ", sum([10, nothing, 7]), 17)
    check("and both at once         ", sum([10, nothing, 7, unknown]), 17)
    check("max                      ", max([10, unknown, 7]), 10)
    check("min                      ", min([10, unknown, 7]), 7)
    check("median over the present  ", median([10, unknown, 7, 1]), 7)
    check("stdev runs               ", round(stdev([2, unknown, 4, 6]), 4), 2)

    print "-- `mean` DIVIDES BY THE PRESENT COUNT, where a wrong choice is silent"
    ' 17 / 2, not 17 / 3. Skip the absences and divide by the full count and every
    ' average is quietly too low -- which is why this is asserted by value rather
    ' than assumed from SQL.
    check("mean is 17 / 2           ", mean([10, unknown, 7]), 8.5)
    check("and not 17 / 3           ", mean([10, unknown, 7]) != round(17 / 3, 10), true)

    print "-- ALL-ABSENT IS `unknown`, NOT ZERO"
    ' "There was nothing to add" is not "the total is zero". A 0 here reads as a
    ' real figure and nothing downstream can tell them apart.
    check("sum of all absent        ", sum([unknown, nothing]), unknown)
    check("mean of all absent       ", mean([unknown]), unknown)
    check("max of all absent        ", max([unknown, unknown]), unknown)
    ' AND THE ASYMMETRY IS DELIBERATE: `sum([])` still RAISES. An empty list is a
    ' programming mistake; all-absent is a fact about the data.
    on warning print
    on error goto next
    x = sum([])
    check("but an empty list raises ", contains(error.message, "non-empty array"), true)
    error.clear()

    print "-- `count` AND `len` KEEP COUNTING ROWS, which is SQL's COUNT(*)"
    ' THESE TWO ARE ANSWERED ON A DIFFERENT PATH -- their own branches in
    ' `eval_call`, before the aggregates -- so no perturbation of the absence
    ' filter can move them, and a `len` exemption written into that filter was
    ' DEAD CODE: a perturbation removing it left this check green, which is how it
    ' was found. What is load-bearing here is the DIFFERENCE between the two
    ' counts, §2a's third detail: SQL has COUNT(*) and COUNT(col) and gBASIC had
    ' one spelling, so the second got its own rather than silently moving the first.
    on warning ignore
    check("count is every row       ", count([10, unknown, 7]), 3)
    check("len is every row         ", len([10, unknown, 7]), 3)
    check("count(present(..)) is 2  ", count(present([10, unknown, 7])), 2)
    check("and the two DIFFER       ", count([10, unknown, 7]) != count(present([10, unknown, 7])), true)

    print "-- `present(a)`: the list without its absences"
    check("present drops both kinds ", present([1, unknown, 2, nothing, 3]), [1, 2, 3])
    check("present of all absent    ", present([unknown, nothing]), [])
    check("present of none absent   ", present([1, 2]), [1, 2])
    check("present of empty         ", present([]), [])
    check("present keeps order      ", present([3, unknown, 1]), [3, 1])
    check("present over records     ", present([{a:1}, unknown]), [{a:1}])

    print "-- THE WARNING IS THE POINT: skipping SILENTLY is the dangerous thing"
    on warning goto next
    t = sum([10, unknown, 7])
    check("a warning was raised     ", warning != false, true)
    error.clear()
    on warning goto next
    t2 = sum([10, unknown, 7])
    check("it carries code 2111     ", warning.code, 2111)
    on warning goto next
    t3 = sum([10, unknown, 7])
    check("and source `absence`     ", warning.source, "absence")
    on warning goto next
    t4 = sum([10, unknown, 7, nothing])
    check("it counts them           ", contains(warning.message, "skipped 2 absent values of 4"), true)
    on warning goto next
    t5 = sum([10, unknown, 7])
    check("singular at one          ", contains(warning.message, "skipped 1 absent value of 3"), true)
    on warning goto next
    t6 = sum([10, unknown, 7])
    check("and names the remedy     ", contains(warning.message, "present()"), true)
    on warning goto next
    t7 = sum([unknown])
    check("all-absent warns too     ", contains(warning.message, "all 1 values are absent"), true)

    print "-- `present()` IS THE OPT-OUT, and needs no special case to be one"
    ' An aggregate over `present(x)` has nothing absent to skip, so it is silent
    ' because there is nothing to say. That matters because `on warning ignore` is
    ' the only other way to quiet it and it turns off EVERY warning in the frame.
    ' DRAIN FIRST. `warning.message` READS without claiming, so every assertion
    ' above left its warning pending -- and a stale one makes "is it silent?"
    ' answer about the previous line. Bare `warning` claims it. (This is the trap
    ' the reference names: reading mirrors `error`, and only the bare form claims.)
    drained = warning
    on warning goto next
    q = sum(present([10, unknown, 7]))
    check("present() is silent      ", warning = false, true)
    check("and gives the same answer", q, 17)
    ' THE CONTROL: a list with NO absence is silent too, or "it warns" would be
    ' satisfied by a rule that fires on every aggregate.
    drained = warning
    on warning goto next
    q2 = sum([10, 7])
    check("no absence, no warning   ", warning = false, true)

    print "-- `on warning stop` turns it into a failure, which is what a test run wants"
    on warning stop
    on error goto next
    z = sum([10, unknown, 7])
    check("escalates under stop     ", contains(error.message, "skipped 1 absent value"), true)
    error.clear()
    on warning ignore

    print "-- §2b: `mode` answered the FIRST ELEMENT on continuous data"
    ' THE SHARP ONE. On money, measurements, any real price list every value is
    ' unique, so `mode` returned element 0 and ALWAYS LOOKED LIKE AN ANSWER.
    check("no mode on a price list  ", mode([19.95, 32.50, 7.00, 4.25, 88.00]), unknown)
    check("no mode when none repeats", mode([1, 2, 3]), unknown)

    print "-- a TIE is reported, not resolved by source order"
    ' `mode([1,1,2,2])` answered 1 and `mode([2,2,1,1])` answered 2 -- the same
    ' multiset, two answers, decided by how the data happened to be sorted.
    check("every tied value         ", mode([1, 1, 2, 2]), [1, 2])
    check("the same multiset again  ", mode([2, 2, 1, 1]), [1, 2])
    check("one mode is still a list ", mode([1, 1, 2]), [1])
    check("and `first` takes one    ", first(mode([1, 1, 2])), 1)

    print "-- `mode` ACCEPTS TEXT, which is the case it is actually wanted for"
    ' The most frequent category, city or status code. Numeric-only made the one
    ' aggregate where text is the COMMON case nearly useless.
    check("text                     ", mode(["ca", "ny", "ca", "tx"]), ["ca"])
    check("a text tie, sorted       ", mode(["ny", "ca", "ca", "ny"]), ["ca", "ny"])
    check("dates                    ", string(mode([{date}"2026-01-01", {date}"2026-01-01", {date}"2025-01-01"])), "[2026-01-01]")
    check("money                    ", string(mode([{USD}"1.00", {USD}"1.00", {USD}"2.00"])), "[1.00]")
    check("booleans                 ", mode([true, false, true]), [true])
    ' RECORDS: equality is `=`, so an identical record counts as a repeat. The tie
    ' list is in first-seen order there, because `sort` refuses to order records
    ' and inventing an order is what it refuses for.
    check("records                  ", mode([{a:1}, {a:1}, {a:2}]), [{a:1}])

    print "-- §2b: `median` takes the statistical convention on an even count"
    check("median of four           ", median([4, 1, 2, 3]), 2.5)
    check("median of three          ", median([4, 1, 2]), 2)

    print "-- ABSENCES SORT LAST (ruled 2026-10-07), and are not ordered against each other"
    check("scalar sort              ", sort([3, unknown, 1]), [1, 3, unknown])
    check("both kinds               ", sort([3, nothing, 1, unknown]), [1, 3, nothing, unknown])
    check("entry order among them   ", sort([unknown, nothing, 1]), [1, unknown, nothing])
    check("and the other way        ", sort([nothing, unknown, 1]), [1, nothing, unknown])
    check("by a field               ", sort([{x:3},{x:unknown},{x:1}], { by: "x" }).x, [1, 3, unknown])
    ' DESCENDING PUTS THEM FIRST, which is the consequence of negating the
    ' comparison rather than a second rule -- and is what PostgreSQL does too.
    check("descending reverses it   ", sort([3, unknown, 1], { descending: true }), [unknown, 3, 1])

    print "-- REFUSALS"
    error.clear()
    x = present(5)
    check("present of a number      ", contains(error.message, "present expects a list"), true)
    error.clear()
    x = mode([])
    check("mode of an empty list    ", contains(error.message, "non-empty array"), true)
    error.clear()
    ' A WRONG KIND IS STILL A WRONG KIND: only an ABSENCE is skipped, and a string
    ' in a numeric list is a different fault.
    x = sum([1, "two", 3])
    check("text in a numeric list   ", contains(error.message, "element 1 is a string"), true)
    error.clear()
end program

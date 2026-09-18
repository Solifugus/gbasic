' EXECUTABLE PROBES FOR docs/ari_limitations.md.
'
' EVERY CHECK HERE IS A NEGATIVE CONTROL. It asserts a limitation STILL HOLDS
' and goes RED WHEN THE LIMITATION IS FIXED, naming the entry to strike. A
' register nobody runs rots -- measured in this tree already: five of fourteen
' entries in DOGFOOD.md were FALSE, fixed by shipped work, still cited as design
' justification, and not catchable by reading.
'
' THE CONTROLS ARE WHAT MAKE THE PROBES MEAN ANYTHING. Several assert that
' something is `unknown`, which is equally satisfied by a build where `as money`
' is broken entirely -- so every class-B probe sits beside a form that MUST
' still work, and the class-A probes assert the specific WRONG VALUE rather than
' merely "not the right one".

load ari

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print ("ok   " + label)
    else
        tally.mismatches = tally.mismatches + 1
        print ("MISMATCH " + label + ": got " + string(got) + ", want " + string(want))
    end if
    return nothing
end function

function money_spec()
    sp = []
    append(sp, "section report:")
    append(sp, "    field v: right of \"X\" as money")
    return join(sp, "\n")
end function

function date_spec()
    sp = []
    append(sp, "section report:")
    append(sp, "    field v: right of \"X\" as date")
    return join(sp, "\n")
end function

function read_money(t)
    r = ari.parse("X " + t, money_spec())
    return r.value.v
end function

function read_date(t)
    r = ari.parse("X " + t, date_spec())
    return r.value.v
end function

function why_of(t, spec)
    r = ari.parse("X " + t, spec)
    out = ""
    for each d in r.diagnostics
        out = d.reason
    end for
    return out
end function

' ===========================================================================
print "-- CLASS A: silent wrong answers (these matter most)"
' ===========================================================================
' Asserted as THE SPECIFIC WRONG VALUE, not as "not 1234.56". A probe that only
' said "the answer is wrong" would stay green if the answer became a DIFFERENT
' wrong value, and would tell a reader nothing about what to expect.

' CLASS A IS EMPTY. All four entries were discharged by the 2026-09-17 sweep,
' so every probe here is INVERTED: it asserts the defect is provably GONE. A
' struck entry with nothing behind it is just a deleted one.

' --- A3: the DR/CR sense is a CONVENTION, and it runs both ways -------------
' The register recorded `DR` as "should be -1234.56", which is the statement
' reading asserted as the truth. The real defect was the other way round: on a
' CUSTOMER STATEMENT a credit increases the balance, and `ari` read every one as
' negative -- silently, for half the report population. Asserted as a DIFFERENCE
' between the two declared conventions, because either alone is satisfied by a
' library that hardcodes it.
function money_using(t, using_line)
    sp = []
    append(sp, "section report:")
    if using_line != "" then
        append(sp, "    " + using_line)
    end if
    append(sp, "    field v: right of \"X\" as money")
    r = ari.parse("X " + t, join(sp, "\n"))
    if not r.ok then
        return "spec refused"
    end if
    return r.value.v
end function

check("A3 struck: the ledger reading has DR positive",
      money_using("1,234.56 DR", "using money: ledger"), 1234.56)
check("A3 struck: and CR negative",
      money_using("1,234.56 CR", "using money: ledger"), -1234.56)
check("A3 struck: the statement reading inverts both -- DR",
      money_using("1,234.56 DR", "using money: statement"), -1234.56)
check("A3 struck: and CR",
      money_using("1,234.56 CR", "using money: statement"), 1234.56)
check("A3 the default is the ledger reading, so no existing spec moves",
      read_money("1,234.56 CR"), -1234.56)
check("A3 and DR is now READ rather than ignored",
      read_money("1,234.56 DR"), 1234.56)

' THE CONTROL that keeps the convention from being "flip everything": a
' trailing minus, parentheses and angle brackets are NOTATION, not a DR/CR
' sense, and the declaration must not touch them.
check("A3 control: a trailing minus is notation, not a sense",
      money_using("1,234.56-", "using money: statement"), -1234.56)
check("A3 control: parentheses too",
      money_using("(1,234.56)", "using money: statement"), -1234.56)
check("A3 control: and an unsuffixed amount is unaffected",
      money_using("1,234.56", "using money: statement"), 1234.56)

' --- A4: a date is checked against the LENGTH OF ITS MONTH ------------------
check("A4 struck: 31-FEB is refused", read_date("31-FEB-2026"), unknown)
check("A4 struck: and so is the numeric spelling, so the two paths still agree",
      read_date("31/02/2026"), unknown)
check("A4 struck: it says why", why_of("31-FEB-2026", date_spec()), "invalid-date")
check("A4 30-APR is valid and 31-APR is not", read_date("30-APR-2026"), "2026-04-30")
check("A4 31-APR is refused", read_date("31-APR-2026"), unknown)
check("A4 31-JAN is a real date", read_date("31-JAN-2026"), "2026-01-31")

' THE LEAP RULE IN FULL. The shortcut that gets 2000 wrong is the commonest date
' bug there is, so the two century cases are asserted rather than assumed.
check("A4 29 February is refused in a common year", read_date("29-FEB-2026"), unknown)
check("A4 and accepted in a leap year", read_date("29-FEB-2024"), "2024-02-29")
check("A4 1900 was not a leap year", read_date("29-FEB-1900"), unknown)
check("A4 2000 was", read_date("29-FEB-2000"), "2000-02-29")

' ===========================================================================
print ""
print "-- STRUCK: A1 and A2, which were ONE defect. Now controls."
' ===========================================================================
' A struck entry with nothing behind it is just a deleted one, so the probes are
' inverted rather than removed: each must be provably GONE. This is the shape
' run_limitations.sh uses for the language ledger and for the same reason.
'
' Both entries said "or `unknown`", and `unknown` is what these now answer.
' Reading these forms is B10/B11 and is a different question.
check("A1 struck: European grouping is no longer read as 1.23",
      read_money("1.234,56"), unknown)
check("A2 struck: three decimals are no longer truncated to two",
      read_money("1,234.567"), unknown)

' TWO FORMS THE REGISTER NEVER RECORDED, found by measuring rather than reading.
' A1 was recorded at ONE separator; the error grows with every further group,
' and the sign is lost along with it.
check("a millionfold error is gone too", read_money("12.345.678,90"), unknown)
check("and the sign is no longer lost with it", read_money("1.234,56-"), unknown)

' THE CONTROLS THAT KEEP THE FIX FROM BEING "REFUSE EVERYTHING", which is the
' failure mode a boundary rule invites. Every form the recognizer read before
' must still read, and the last one is the one the rule was written around:
' ordinary punctuation after a value is not a longer number.
check("plain grouped money still reads", read_money("1,234.56"), 1234.56)
check("a dollar sign still reads", read_money("$1,234.56"), 1234.56)
check("parentheses still read as negative", read_money("(1,234.56)"), -1234.56)
check("a trailing minus still reads as negative", read_money("1,234.56-"), -1234.56)
check("an ungrouped amount still reads", read_money("1234.56"), 1234.56)
check("a small amount still reads", read_money("0.99"), 0.99)
check("and a value ending a sentence is not read as a longer number",
      read_money("1,234.56."), 1234.56)

' THE CASE THE DISCOVERY CORPUS CAUGHT AND run_ari's OWN GOLDENS DID NOT.
' A money pattern carries decoration -- `\$?[ ]*`, a leading `<`, `(` or `-` --
' so its match can BEGIN ON A SPACE, and the first version of the boundary test
' looked at the character before the MATCH rather than before the NUMBER. Any
' earlier digit on the line then read as "this number continues", and the sign
' was dropped: 18 of 230 planted amounts came back positive, each an ordinary
' number. The two lines below differ only in whether what precedes the amount
' contains a digit.
check("a trailing minus survives a digit earlier on the line",
      read_money("00147454 MERCER, ALICE 03/19/2026 1,384.82-"), -1384.82)
check("and its control, the same line with no digits before it",
      read_money("MERCER, ALICE 1,384.82-"), -1384.82)
check("parentheses too", read_money("00147454 ALICE (1,384.82)"), -1384.82)
check("and a dollar sign", read_money("00147454 ALICE $1,384.82"), 1384.82)

' ===========================================================================
print ""
print "-- CLASS B: honest misses, each beside a form that must still work"
' ===========================================================================
check("B1 whole amounts with no cents are still refused", read_money("1,234"), unknown)
check("B1 control: two decimals still work", read_money("1,234.00"), "1234.00")
check("B2 one decimal place is still refused", read_money("1,234.5"), unknown)
check("B3 space grouping is still refused", read_money("1 234,56"), unknown)
check("B3 control: comma grouping still works", read_money("1,234.56"), 1234.56)

check("B4 a two-digit year is still refused", read_date("10/16/26"), unknown)
check("B4 control: a four-digit year still works", read_date("10/16/2026"), "2026-10-16")
check("B5 a two-digit year with an alphabetic month is still refused",
      read_date("16-OCT-26"), unknown)
check("B5 control: the four-digit form still works", read_date("16-OCT-2026"), "2026-10-16")
check("B6 compact YYYYMMDD is still refused", read_date("20261016"), unknown)
check("B7 a full month name is still refused", read_date("OCTOBER 16, 2026"), unknown)
check("B8 ISO order with slashes is still refused", read_date("2026/10/16"), unknown)
check("B10 European grouping is not READ, only refused", read_money("1.234,56"), unknown)
check("B10 control: the same digits in the other convention read", read_money("1,234.56"), 1234.56)
check("B11 more than two decimals is not read", read_money("1,234.567"), unknown)
check("B11 control: exactly two decimals read", read_money("1,234.56"), 1234.56)

' The diagnostics are part of the contract: a refusal that said nothing would be
' a different and worse limitation than the one recorded.
check("B1 says why", why_of("1,234", money_spec()), "malformed-money")
check("B4 says why", why_of("10/16/26", date_spec()), "no-date-found")

' B9 -- a custom DATE type whose rule CAPTURES its components. Found while
' writing tests/ari_using_test.bas, and recorded rather than fixed because the
' register's sweep rule is Class A first and on its own.
'
' With no replacement and at least one capture, `_convert` takes groups[0] as
' the value -- which is right for money, where a capture is how the digits are
' pulled out of the symbols and the sign, and never right for a date, where the
' captures are the components and the first of them is a two-digit day. The
' control beside it is the SAME rule without captures, which works: the
' difference is the parentheses and nothing else.
function typed_date(rule)
    sp = []
    append(sp, "type d1:")
    append(sp, "    " + rule)
    append(sp, "    output: date")
    append(sp, "section report:")
    append(sp, "    using date: d1")
    append(sp, "    field v: right of \"X\" as date")
    r = ari.parse("X 16/10/2026", join(sp, "\n"))
    if not r.ok then
        return "spec refused"
    end if
    return r.value.v
end function

check("B9 a date rule that captures its components is not read",
      typed_date("/([0-9]{2})\\/([0-9]{2})\\/([0-9]{4})/ -> dmy"), unknown)
check("B9 control: the same rule without captures works",
      typed_date("/[0-9]{2}\\/[0-9]{2}\\/[0-9]{4}/ -> dmy"), "2026-10-16")

' ===========================================================================
print ""
print "-- CLASS C: capability gaps"
' ===========================================================================
' C1 -- no span-level claimed/unclaimed surface. Probed STRUCTURALLY, by asking
' what `parse` returns, because there is no behaviour to observe for something
' that does not exist. The two things that DO exist are asserted beside it, so
' this probe cannot be read as "ari reports nothing".
r = ari.parse("X 1,234.56", money_spec())
check("C1 parse reports no claimed spans", has(r, "claimed"), false)
check("C1 parse reports no unclaimed lines", has(r, "unclaimed"), false)
check("C1 control: diagnostics DO exist", has(r, "diagnostics"), true)
i = ari.inspect("X 1,234.56", money_spec())
check("C1 control: inspect DOES summarise findings", has(i, "findings"), true)

' ===========================================================================
print ""
print "-- WHAT WAS ADDED, tested where `ari` is tested rather than only in"
print "   the discovery suite (see the register's note on teaching to the test)"
' ===========================================================================
' These are POSITIVE checks, not limitation probes: the alphabetic-month forms
' now work, and belong in ari's own suite rather than only in run_ari_discover.
check("DD-MMM-YYYY", read_date("16-OCT-2026"), "2026-10-16")
check("MMM-DD-YYYY", read_date("OCT-16-2026"), "2026-10-16")
check("space separated", read_date("16 OCT 2026"), "2026-10-16")
check("mixed case", read_date("16-Oct-2026"), "2026-10-16")
check("slash separated", read_date("16/OCT/2026"), "2026-10-16")
check("an unknown month name is refused BY NAME, not guessed",
      why_of("16-XYZ-2026", date_spec()), "unknown-month-name")
' AN ALPHABETIC MONTH NEEDS NO DIALECT and is the one date form that can never
' be ambiguous -- asserted as a DIFFERENCE against the numeric form, which for
' the same day/month pair IS ambiguous and correctly refuses.
check("an alphabetic month is unambiguous with no dialect declared",
      read_date("04-MAR-2026"), "2026-03-04")
check("the numeric form of the same date is NOT, and says so",
      why_of("04/03/2026", date_spec()), "ambiguous-date")

print ""
print ("checks: " + string(tally.checks))
print ("mismatches: " + string(tally.mismatches))

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

' --- `as integer` and `as decimal` read a WHOLE token ----------------------
' The same defect as A1/A2 one level over, and NO ENTRY IN THE REGISTER HAD EVER
' PROBED IT, because every entry was written about `as money`. Measured before
' the fix:
'
'     1,234      as integer -> 1        as decimal -> 1
'     1,234.56   as integer -> 1
'     1.234,56   as decimal -> 1.234
'
' A count of 1,234 reading as 1 is the same silent, plausible shape -- and
' `as integer` is the commonest conversion a generated spec contains, since it
' is what a section's own number is read with.
function read_int(t)
    r = ari.parse("X " + t, join([ "section report:",
                                   "    field v: right of \"X\" as integer" ], "\n"))
    return r.value.v
end function

function read_dec(t)
    r = ari.parse("X " + t, join([ "section report:",
                                   "    field v: right of \"X\" as decimal" ], "\n"))
    return r.value.v
end function

check("a grouped integer is no longer read as its first group", read_int("1,234"), 1234)
check("and the continental grouping too", read_int("1.234"), 1234)
check("a grouped decimal likewise", read_dec("1,234"), 1234)
check("and a continental decimal reads correctly", read_dec("1.234,56"), 1234.56)

' A GROUPED INTEGER IS UNAMBIGUOUS where a grouped decimal is not: an integer
' has no decimal part, so `1,234` and `1.234` are both 1234 whatever convention
' the report uses. That is why this admits a separator the money core refuses to
' guess at, and the control is that a token WITH a decimal part is refused
' rather than truncated.
check("a value with a decimal part is not an integer", read_int("1.23"), unknown)
check("nor is a grouped one", read_int("1,234.56"), unknown)
check("and a mixed-separator token is refused rather than stripped",
      read_int("1,234.567"), unknown)
check("control: a plain integer still reads", read_int("42"), 42)
check("control: a negative one too", read_int("-42"), -42)
check("control: one inside prose still reads", read_int("PAGE 7"), 7)
check("control: and a deeply grouped one", read_int("1,234,567"), 1234567)

' --- a bad cell is `unknown`, NEVER a raise that sinks the import -----------
' §8's contract, written verbatim above `_to_amount` and not held: `number()`
' RAISES on a string it cannot convert rather than answering `unknown`, so the
' guard there was dead from the day it was written. Reachable from the CUSTOM
' TYPE path, where the captured text is whatever the author's own regex took.
' Found by PERTURBING the last-separator rule, not by reading -- the built-in
' patterns never produce a string `number` rejects.
function custom_money(text, rule)
    sp = []
    append(sp, "type odd_money:")
    append(sp, "    " + rule)
    append(sp, "    output: money")
    append(sp, "section report:")
    append(sp, "    using money: odd_money")
    append(sp, "    field v: right of \"X\" as money")
    on error goto next
    r = ari.parse("X " + text, join(sp, "\n"))
    if error then
        error.clear()
        on error stop
        return "RAISED"
    end if
    on error stop
    return r.value.v
end function

check("a custom type capturing unconvertible text answers unknown",
      custom_money("12.345.678", "/([0-9.,]+)/ -> as decimal"), unknown)
check("it does not raise and sink the parse",
      custom_money("12.345.678", "/([0-9.,]+)/ -> as decimal") = "RAISED", false)
check("control: a custom type capturing a real amount still converts",
      string(custom_money("1,234.56", "/([0-9.,]+)/ -> as decimal")), "1234.56")

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
' A1 is discharged TWICE OVER: the silent 1.23 went first, and the form is read
' correctly now -- which is what the entry asked for in the first place
' ("`1234.56`, or `unknown`"). Both separators appear, so the LAST is the
' decimal mark, which is a fact about the two notations rather than a guess
' about this report.
check("A1 struck: European grouping reads correctly", read_money("1.234,56"), 1234.56)
' Asserted as TEXT, not against a number literal: `money` renders its minor
' units so this is "12345678.90", while the gBASIC number 12345678.90 renders as
' "12345678.9". Comparing them would fail on a correct answer.
check("A1 struck: and so does a deeply grouped one",
      string(read_money("12.345.678,90")), "12345678.90")
check("A1 struck: with the sign kept", read_money("1.234,56-"), -1234.56)
check("A2 struck: three decimals are no longer truncated to two",
      read_money("1,234.567"), unknown)

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
check("B12 a SINGLE separator stays ambiguous and is refused", read_money("1.234"), unknown)
check("B12 control: two separators settle it", read_money("1.234,56"), 1234.56)
check("B12 control: and the other convention too", read_money("1,234.56"), 1234.56)
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
' CLASS C IS EMPTY OF CAPABILITY GAPS. C1 was struck 2026-09-18 when `ari.trace`
' shipped, so this probe is INVERTED: it asserts the surface is provably THERE.
' A struck entry with nothing behind it is just a deleted one.
'
' The behaviour lives in tests/ari_trace_test.bas; what is asserted here is only
' that the entry may stay struck -- the span surface exists, answers, and points
' at the source.
r = ari.parse("X 1,234.56", money_spec())
check("C1 control: `parse` itself is unchanged, no claims", has(r, "claims"), false)
t = ari.trace("X 1,234.56", money_spec())
check("C1 struck: trace reports claimed spans", has(t, "claims"), true)
check("C1 struck: and what stayed unclaimed", has(t, "unclaimed"), true)
check("C1 struck: and a content coverage figure", is_unknown(t.content_coverage), false)
check("C1 struck: and where two rules collided", has(t, "collisions"), true)
' The claim must point AT THE SOURCE, not merely exist: `1,234.56` begins at
' column 2 of `X 1,234.56`, which is a fact about the string and not about ari.
found = false
for each c in t.claims
    if c.kind = "field" then
        if c.line = 1 then
            if c.start = 2 then
                if c.text = "1,234.56" then
                    found = true
                end if
            end if
        end if
    end if
end for
check("C1 struck: and the span is where the value really is", found, true)
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

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

check("A1 European grouping still reads 1.234,56 as 1.23", read_money("1.234,56"), 1.23)
check("A2 three decimals are still truncated to two", read_money("1,234.567"), 1234.56)
check("A3 a DR suffix is still read as POSITIVE", read_money("1,234.56 DR"), 1234.56)
check("A4 31-FEB is still accepted", read_date("31-FEB-2026"), "2026-02-31")
check("A4 and the numeric path agrees, so it is consistent rather than a regression",
      read_date("31/02/2026"), "2026-02-31")

' THE CONTROL for A3, and it is the whole reason A3 is a defect rather than a
' gap: the OTHER half of the pair IS recognised, so a report using DR/CR gets
' half its signs right.
check("A3 control: CR is recognised as negative", read_money("1,234.56CR"), -1234.56)

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

' The diagnostics are part of the contract: a refusal that said nothing would be
' a different and worse limitation than the one recorded.
check("B1 says why", why_of("1,234", money_spec()), "malformed-money")
check("B4 says why", why_of("10/16/26", date_spec()), "no-date-found")

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

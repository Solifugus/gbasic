' ARI gaps G24-G28 from the gBASIC books (volume 3's gap ledger, Chapter 7:
' a purchase-order spool read as data).
'
'   G24  `last money` reached back past an amount printed as asterisks and
'        returned the unit price, silently
'   G25  `last money within columns a-b` came back as text, not money
'   G26  a section with `ends` and no `starts` ignored its `ends`
'   G27  a wrapped description could not be joined to its record
'   G28  two-digit years, with a declared century and a declared "no date"
'
' Money is compared as its printed text with its cents, "125.00", so a check
' cannot pass on a number that only looks like the amount.
'
' SELF-CHECKING. Every defect here produced a plausible value: a real amount
' from the wrong column, the right digits as text, a row that was a heading.
' Each answer below is written by hand from the report text beside it.

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

function reasons(r)
    out = []
    for each d in r.diagnostics
        append(out, d.path + " " + d.reason)
    end for
    return join(out, "; ")
end function

function lines_to_text(ls)
    return join(ls, "\n")
end function

' =========================================================== G24 and G25
print "-- G24: `last <type>` is the last token, or nothing"
' The second line's open amount overflowed its picture: 1,305,000.00 is
' printed as asterisks, and 1,450.00 (the unit price) is the last money left.
po = lines_to_text([
    "PO 4470  VALVE             10      12.50        125.00",
    "PO 4471  CONTROLLER       900   1,450.00  ************",
    "PO 4472  GASKET             4       3.25         13.00 CR" ])
sp = lines_to_text([
    "section report:",
    "    rows:",
    "        field po: columns 0-7",
    "        field open: last money" ])
r = ari.parse(po, sp)
check("an ordinary line reads its last amount", r.value.rows.open[0], "125.00")
check("and it is money", type(r.value.rows.open[0]), "money")
check("the starred line is refused, not read as the unit price", r.value.rows.open[1], unknown)
check("and says why, by name", reasons(r), "report.rows[1].open overflow-after-money")
check("a suffix that belongs to the amount is part of it", r.value.rows.open[2], "-13.00")
' CONTROLS: text after the last amount that is NOT an overflow. The first
' version refused these, and the delinquency fixture's days-past-due column
' (`24,368.82     44`) is why that was wrong.
ok_after = lines_to_text([
    "LN 24,368.82     44",
    "LN 125.00 *",
    "LN 125.00 **" ])
sp = lines_to_text([
    "section report:",
    "    rows:",
    "        field open: last money" ])
r = ari.parse(ok_after, sp)
check("a number of another column after the amount is fine", r.value.rows.open[0], 24368.82)
check("a single-star flag is fine", r.value.rows.open[1], "125.00")
check("two stars are a flag too", r.value.rows.open[2], "125.00")
check("none of them diagnosed", reasons(r), "")

print "-- G25: a qualifier narrows where, not what"
sp = lines_to_text([
    "section report:",
    "    rows:",
    "        field open: last money within columns 40-60" ])
r = ari.parse(po, sp)
check("`within columns` keeps the type", type(r.value.rows.open[0]), "money")
check("and the value", r.value.rows.open[0], "125.00")
check("and `last`: the window holds one amount, the right one", r.value.rows.open[2], "-13.00")
check("the stars are inside the window too, so still refused", r.value.rows.open[1], unknown)
' the bound that Chapter 7 wrote, a window holding only the open amount
sp = lines_to_text([
    "section report:",
    "    rows:",
    "        field open: last money within columns 47-60" ])
r = ari.parse(po, sp)
check("a window past the unit price: the stars hold no money", r.value.rows.open[1], unknown)
check("and the reason is that no money is there", reasons(r), "report.rows[1].open malformed-money")
' `first` through a window: before the fix it lost `first` and the type
sp = lines_to_text([
    "section report:",
    "    rows:",
    "        field price: first money within columns 20-60" ])
r = ari.parse(po, sp)
check("`first money within columns` is the first amount, as money", r.value.rows.price[1], "1450.00")
check("as money", type(r.value.rows.price[1]), "money")
' a vertical locator's inner `last` too
tot = lines_to_text([ "SUMMARY", "  lines 3   12.50   125.00", "TOTAL DUE" ])
sp = lines_to_text([
    "section report:",
    "    field due: up 1 of \"TOTAL DUE\" last money" ])
r = ari.parse(tot, sp)
check("`up 1 of ... last money` is the last amount", r.value.due, "125.00")
check("as money", type(r.value.due), "money")

' ================================================================== G26
print "-- G26: `ends` without `starts`"
ven = lines_to_text([
    "VENDOR 100 ACME",
    "  ITEM-A        1.00",
    "  ITEM-B        2.00",
    "VENDOR TOTAL    3.00",
    "VENDOR 200 BETA",
    "  ITEM-C        5.00",
    "VENDOR TOTAL    5.00" ])
sp = lines_to_text([
    "section report:",
    "    section vendors repeats starts(/^VENDOR [0-9]/):",
    "        field id: right of \"VENDOR\" as integer",
    "        field total: right of \"VENDOR TOTAL\" as money",
    "        section detail ends(/^VENDOR TOTAL/):",
    "            rows:",
    "                field item: columns 2-8",
    "                field amount: last money" ])
r = ari.parse(ven, sp)
v = r.value.vendors
check("two vendors", count(v), 2)
check("the first vendor's rows stop at its total", v[0].detail.rows.item, [ "ITEM-A", "ITEM-B" ])
check("and the heading is not a row", v[0].detail.rows.amount, "[1.00,2.00]")
check("the second vendor's too", v[1].detail.rows.item, [ "ITEM-C" ])
check("the parent still reads its own lines", v[1].total, "5.00")
check("nothing was diagnosed", reasons(r), "")
' repeating with no `starts`: each instance ends at an `ends` line
blocks = lines_to_text([
    "A 1.00", "B 2.00", "----",
    "C 3.00", "----",
    "D 4.00" ])
sp = lines_to_text([
    "section report:",
    "    section batch repeats ends(/^----/):",
    "        rows:",
    "            field amount: last money" ])
r = ari.parse(blocks, sp)
check("`repeats ends(...)` cuts an instance at every `ends`", count(r.value.batch), 3)
check("the first instance", r.value.batch[0].rows.amount, "[1.00,2.00]")
check("the second", r.value.batch[1].rows.amount, "[3.00]")
check("what follows the last `ends` is the last instance", r.value.batch[2].rows.amount, "[4.00]")

' ================================================================== G27
print "-- G27: a field that wraps onto continuation lines"
wrap = lines_to_text([
    "LN1  WIDGET, BLUE, LARGE        10.00",
    "     SIZE 12, BOXED",
    "LN2  BOLT                        2.00",
    "LN3  WASHER, ZINC                0.50",
    "     PLATED" ])
sp = lines_to_text([
    "section report:",
    "    rows continue(/^     [A-Z]/):",
    "        field line: columns 0-3",
    "        field description: columns 5-28 wraps",
    "        field amount: last money" ])
r = ari.parse(wrap, sp)
check("three records from five lines", count(r.value.rows.line), 3)
check("a wrapped description is joined, with one space",
      r.value.rows.description[0], "WIDGET, BLUE, LARGE SIZE 12, BOXED")
check("one that does not wrap is untouched", r.value.rows.description[1], "BOLT")
check("the last record wraps too", r.value.rows.description[2], "WASHER, ZINC PLATED")
check("a field without `wraps` still reads its first line only", r.value.rows.line[0], "LN1")
check("the amount on the first line is the record's", r.value.rows.amount[2], "0.50")
tr = ari.trace(wrap, sp)
parts = []
for each c in tr.claims
    if c.path = "report.rows[0].description" then
        append(parts, string(c.line) + ":" + string(c.start) + ":" + c.text)
    end if
end for
check("the joined value claims every physical line it came from", parts,
      [ "1:5:WIDGET, BLUE, LARGE", "2:5:SIZE 12, BOXED" ])
' `wraps` without `continue`: a record is one line, so it is the plain field
sp = lines_to_text([
    "section report:",
    "    rows:",
    "        field description: columns 5-28 wraps" ])
r = ari.parse(wrap, sp)
check("without `continue` every line is a record, wrapped or not", r.value.rows.description[1], "SIZE 12, BOXED")

' ================================================================== G28
print "-- G28: two-digit years, declared"
erp = lines_to_text([
    "ROW 11/15/26",
    "ROW 00/00/00",
    "ROW 02/03/2027",
    "ROW 16-OCT-26" ])
sp = lines_to_text([
    "section report:",
    "    using date: mdy",
    "    using century: 20",
    "    using no date: 00/00/00",
    "    rows:",
    "        field d: right of \"ROW\" as date" ])
r = ari.parse(erp, sp)
check("the spec is accepted", r.ok, true)
check("a two-digit year takes the declared century", r.value.rows.d[0], {date}"2026-11-15")
check("as a date, the same kind a four-digit year gives", type(r.value.rows.d[0]), type(r.value.rows.d[2]))
check("the declared no-date is nothing, not a guess", r.value.rows.d[1], nothing)
check("a four-digit year is not touched by the century", r.value.rows.d[2], {date}"2027-02-03")
check("an alphabetic month with a two-digit year", r.value.rows.d[3], {date}"2026-10-16")
check("nothing diagnosed: the no-date was declared", reasons(r), "")
' with no century declared, the reason names what to declare
sp = lines_to_text([
    "section report:",
    "    using date: mdy",
    "    rows:",
    "        field d: right of \"ROW\" as date" ])
r = ari.parse(erp, sp)
check("no century: refused", r.value.rows.d[0], unknown)
check("and the reason says it is the year",
      reasons(r), "report.rows[0].d two-digit-year; report.rows[1].d two-digit-year; report.rows[3].d two-digit-year")
insp = ari.inspect(erp, sp)
hint = ""
for each f in insp.findings
    if contains(f.what, "two-digit-year") then
        hint = f.hint
    end if
end for
check("inspect names the declaration", hint, "declare `using century: 20` (or 19) on the enclosing section")
' a century but no no-date: 00/00/00 is not a date and says so
sp = lines_to_text([
    "section report:",
    "    using date: mdy",
    "    using century: 20",
    "    rows:",
    "        field d: right of \"ROW\" as date" ])
r = ari.parse(erp, sp)
check("an undeclared 00/00/00 is an invalid date", reasons(r), "report.rows[1].d invalid-date")
' refusals at load time
sp = lines_to_text([
    "section report:",
    "    using century: 2O",
    "    field d: right of \"ROW\" as date" ])
r = ari.parse(erp, sp)
check("a century that is not two digits is refused", r.ok, false)
check("by name", contains(r.message, "using century: 2O"), true)
sp = lines_to_text([
    "using century: 20",
    "section report:",
    "    field d: right of \"ROW\" as date" ])
r = ari.parse(erp, sp)
check("outside a section it is refused like any `using`", r.ok, false)

print ""
print ("checks: " + string(tally.checks))
print ("mismatches: " + string(tally.mismatches))

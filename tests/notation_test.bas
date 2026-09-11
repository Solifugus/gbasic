' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' notation -- a TEXTUAL form that keeps every gBASIC type.
' See docs/text_serialization_design.md.
'
' SELF-CHECKING RATHER THAN GOLDEN, and forced: a serializer's defects are all
' PLAUSIBLE DOCUMENTS. A money value rounded to the minor unit still reads like
' money, a date decoded as a string still reads like a date, an array whose
' default was dropped still reads like an array. A golden would record any of
' them as expected and defend it.
'
' THE LOAD-BEARING PROPERTY IS THE ROUND TRIP, and it needs TWO CONTROLS or it
' is satisfied by `serialize`, which already round-trips everything and is
' opaque binary. So the tiers also assert the output is LEGIBLE -- it carries
' newlines, indentation and the value's own text -- and that a defect which
' round-trips is still caught: money's is the sub-cent case, where `string`
' would give 3.46 for 3.459 and the round trip through it would be
' self-consistently wrong.

load notation

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function rt(label, v)
    t = notation.to_text(v)
    on error goto next
    b = notation.from_text(t)
    if error then
        m = error.message
        error.clear()
        check("round trip: " + label, "RAISED " + m, "the same value")
        return nothing
    end if
    on error stop
    if b = v then
        check("round trip: " + label, true, true)
    else
        check("round trip: " + label, t, "the same value")
    end if
    return nothing
end function

dt {date}= "2026-03-15"
pt {datetime}= "2026-03-15 14:30:05"
tt {time}= "14:30:05"
usd {USD}= "19.99"
sub {USD}= "3.459"
neg {USD}= "-0.01"
jpy {JPY}= "500"
kwd {KWD}= "12.345"
fl {file}= "/etc/hostname"
dr {dir}= "/tmp"
dur = 2 days + 3 hours + 15 minutes

print "-- every type gBASIC can hold survives the trip"
rt("empty record", {})
rt("empty array", [])
rt("scalars", { n: 1, f: 0.1, big: number("1e20"), t: true, no: false, s: "hi", z: nothing, u: unknown })
rt("date / datetime / time", { a: dt, b: pt, c: tt })
rt("money at four precisions", { a: usd, b: sub, c: neg, d: jpy, e: kwd })
rt("file, dir, duration", { a: fl, b: dr, c: dur })
rt("nesting", { a: { b: { c: [ 1, [ 2, [ 3 ] ] ] } } })
rt("array of records", { rows: [ { a: 1 }, { a: 2 } ] })
rt("a matrix of dates", { m: [ [ dt, dt ], [ dt ] ] })
rt("arrays with different tags", { m: [ [ dt, dt ], [ usd, usd ] ] })
rt("a genuinely mixed array", { m: [ dt, usd, "plain", 7 ] })
rt("a default with an override", { p: [ usd, sub, jpy ] })
rt("root array", [ 1, 2, 3 ])
rt("root array of dates", [ dt, dt ])

print ""
print "-- strings, including the byte gBASIC can hold and cannot spell"
rt("quotes, backslash, newline, tab", { q: "she said \"go\"", b: "C:\\logs", n: "a\nb", t: "x\ty" })
rt("unicode", { u: "cafe 日本語 😀" })
' A FIELD NAME MAY CONTAIN A NUL since rc9, so the notation must carry one --
' and gBASIC REFUSES \u{0} in a source literal, which is why this is the one
' place the format parts from the language.
rt("interior NUL in a value", { s: "a" + chr(0) + "b" })
' A RAW NUL ROUND-TRIPS PERFECTLY and makes the file hostile to the editor this
' format exists to be opened in, so correctness is not the whole requirement:
' the byte must arrive as a visible ESCAPE.
nultext = notation.to_text({ s: "a" + chr(0) + "b" })
check("the NUL is written as an escape", contains(nultext, "u{0}"), true)
check("and no raw NUL reaches the file", contains(nultext, chr(0)), false)
ctltext = notation.to_text({ s: "a" + chr(7) + "b" + chr(27) + "c" })
check("every control byte is escaped", contains(ctltext, "u{07}") and contains(ctltext, "u{1B}"), true)
rt("other control bytes", { s: "a" + chr(7) + "b" + chr(27) + "c" })
nulkey = {}
nulkey["a" + chr(0) + "b"] = 1
nulkey["a" + chr(0) + "z"] = 2
rt("interior NUL in a KEY", nulkey)
check("and the two keys stay two", count(keys(notation.from_text(notation.to_text(nulkey)))), 2)
awkward = {}
awkward["total volume"] = 1
awkward["end"] = 2
awkward[""] = 3
awkward["rate (%)"] = 4
rt("keys an identifier cannot spell", awkward)

print ""
print "-- CONTROL: the output is LEGIBLE, not merely correct"
' Without this the round-trip tier is satisfied by `serialize`, which already
' keeps every type and is opaque binary.
big = { id: 4471, issued: dt, customer: { name: "Ada Lovelace & Co.", account: "ACME-0042" },
        lines: [ { sku: "GB-100", qty: 2, unit: usd }, { sku: "GB-205", qty: 1, unit: sub } ],
        total: usd, terms: dur }
text = notation.to_text(big)
check("it breaks into lines", count(split(text, chr(10))) > 5, true)
check("and indents them", contains(text, chr(10) + "  "), true)
check("a date appears as a date", contains(text, "2026-03-15"), true)
check("money appears at face value", contains(text, "19.99"), true)
check("the tag sits on the KEY side", contains(text, "issued {date}:"), true)
check("and a nested record reads as one", contains(text, "customer: {"), true)

print ""
print "-- CONTROL: a defect that still round-trips"
' `string` on money ROUNDS TO THE MINOR UNIT, so a sub-cent price renders 3.46
' and an encoder using it would round-trip 3.46 to 3.46 -- self-consistently
' wrong. Only comparing against the ORIGINAL catches it.
subtext = notation.to_text({ p: sub })
check("a sub-cent price is written exactly", contains(subtext, "3.459"), true)
check("and string() would have lost it", string(sub), "3.46")
check("the value that comes back is the value that went in", notation.from_text(subtext).p = sub, true)
' The currency travels too, which `string` also drops.
check("JPY stays JPY", money.currency(notation.from_text(notation.to_text({ a: jpy })).a), "JPY")

print ""
print "-- the default cascades, and an element may override it"
casc = notation.from_text("{ m {date}: [ [ \"2026-03-15\", \"2026-03-16\" ] ] }")
check("a default reaches a nested array", type(casc.m[0][0]), "datetime")
over = notation.from_text("{ p {USD}: [ \"1.00\", {JPY}: \"500\" ] }")
check("an override wins", money.currency(over.p[1]), "JPY")
check("and the default still applies beside it", money.currency(over.p[0]), "USD")
' IT STOPS AT A RECORD, because a record's fields are named and each can tag
' itself -- cascading in would try to turn a name into money.
stopped = notation.from_text("{ p {USD}: [ { name: \"Ada\" } ] }")
check("a default does NOT reach into a record", type(stopped.p[0].name), "string")

print ""
print "-- comments are read and DROPPED (version 1)"
commented = notation.from_text("' a note about this record" + chr(10) + "{ a: 1, ' about a" + chr(10) + "  b: 2 }")
check("a comment does not disturb the value", string(count(keys(commented))), "2")
check("and an apostrophe inside a string is content", notation.from_text("{ s: \"don't\" }").s, "don't")

print ""
print "-- HAND-EDITED: whitespace is not load-bearing"
' The file exists to be opened and changed. A person reflows a line, adds a
' comment, or pastes something back on one line -- and the value must be the
' value either way.
orig = { id: 4471, issued: dt, total: usd, rows: [ { a: 1 }, { a: 2 } ] }
pretty = notation.to_text(orig)
flat = replace(replace(pretty, chr(10), " "), "  ", " ")
check("the same document on one line decodes the same", notation.from_text(flat) = orig, true)
spaced = replace(pretty, ",", " ,  ")
check("and with space around the commas", notation.from_text(spaced) = orig, true)
with_notes = "' the March invoice" + chr(10) + pretty
check("and with a comment above it", notation.from_text(with_notes) = orig, true)
' AND AN EDIT IS AN EDIT: changing a value by hand changes the value.
edited = replace(pretty, "4471", "4472")
check("CONTROL: editing a number changes it", notation.from_text(edited).id, 4472)
check("and the rest of the record is untouched", notation.from_text(edited).total = usd, true)

check("the document ends with a newline", ends_with(pretty, chr(10)), true)

print ""
print "-- DETERMINISM: the same value writes the same bytes"
check("two encodings are identical", notation.to_text(big) = notation.to_text(big), true)

print ""
print "-- refusals, each beside its nearest legal neighbour"
on error goto next
notation.to_text(5)
check("a bare number at the root is refused", contains(error.message, "must be a record or an array"), true)
error.clear()
notation.to_text(dt)
check("so is a bare typed value", contains(error.message, "must be a record or an array"), true)
error.clear()
check("CONTROL: a record root is accepted", len(notation.to_text({ a: 1 })) > 0, true)
check("CONTROL: an array root is accepted", len(notation.to_text([ 1 ])) > 0, true)
notation.from_text("{ a {nosuch}: \"1\" }")
check("an unknown tag is refused by name", contains(error.message, "unknown type tag 'nosuch'"), true)
error.clear()
notation.from_text("{ a {date}: \"not-a-date\" }")
check("a value the tag cannot parse is refused", contains(error.message, "date string"), true)
check("and the refusal names the line", contains(error.message, "line 1"), true)
error.clear()
notation.from_text("{ a: 1")
check("an unclosed brace is refused", contains(error.message, "expected"), true)
error.clear()
' A MISSING SEPARATOR IS AT LEAST AS LIKELY AS A MISSING BRACKET, so the
' message names both rather than sending a reader to hunt for a brace that is
' exactly where they left it.
notation.from_text("{" + chr(10) + "  a: 1" + chr(10) + "  b: 2" + chr(10) + "}")
check("a missing comma names the comma too", contains(error.message, "expected ',' or '}'"), true)
error.clear()
notation.from_text("{ a: [ 1 2 ] }")
check("and the same inside an array", contains(error.message, "expected ',' or ']'"), true)
error.clear()
' NAME THE COMMA, not what follows it: "expected a field name" sends a reader
' looking for a missing value where the mistake is punctuation they can see.
notation.from_text("{ a: 1, }")
check("a trailing comma is named as one", contains(error.message, "trailing comma"), true)
check("and located at the COMMA, not at the closer", contains(error.message, "column 7"), true)
error.clear()
notation.from_text("{ a: [ 1, ] }")
check("a trailing comma in an array too", contains(error.message, "trailing comma before ']'"), true)
error.clear()
notation.from_text("{ a: 1 } trailing")
check("trailing content is refused", contains(error.message, "trailing content"), true)
error.clear()
notation.from_text("")
check("an empty document is refused", contains(error.message, "empty"), true)
error.clear()
notation.from_text("5")
check("a bare scalar document is refused", contains(error.message, "must be a record or an array"), true)
error.clear()
on error stop

print ""
print "-- try_from_text answers with a VALUE, for a file a person just edited"
bad = notation.try_from_text("{ a {nosuch}: \"1\" }")
check("it does not raise", bad.ok, false)
check("and it says why", contains(bad.message, "nosuch"), true)
good = notation.try_from_text("{ a: 1 }")
check("CONTROL: a good document answers ok", good.ok and good.value.a = 1, true)

print ""
print "-- the message names a LINE, because this file is hand-edited"
on error goto next
notation.from_text("{" + chr(10) + "  a: 1," + chr(10) + "  b {nosuch}: \"2\"" + chr(10) + "}")
check("the line number is reported", contains(error.message, "line 3"), true)
error.clear()
on error stop

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

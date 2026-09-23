' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `{file}path` -- the assignment clause, inline, wherever an expression may go.
'
' WHY IT EXISTS. A modifier could only be applied by an ASSIGNMENT, so a value
' wanted once had to be given a name and a line to be given it on. MEASURED
' 2026-09-22 across stdlib, examples and tests: 1,034 one-word assignment
' clauses, and 510 of them bind a name that is read EXACTLY ONCE afterwards --
' a value named only so that it could be passed.
'
' WHY IT LIVES IN THE LEXER. `{a}` and `{a: 1}` differ at their THIRD token,
' which LALR(1) cannot see from the brace: the grammar route costs a
' shift/reduce conflict against a project standard of zero, and the fuller
' route that would also admit `{split ","}` inline costs NINETEEN -- both
' measured, 2026-09-22. The lexer may look as far ahead as it likes, so
' recognising the exact shape `{ IDENT }` there costs none. The price is that
' the inline form takes a ONE-WORD name only; `{end of month}` and
' `{split ","}` still need the assignment clause.
'
' SELF-CHECKING rather than golden, and here that is forced: every defect this
' can have produces an ORDINARY-LOOKING VALUE. A modifier silently not applied
' leaves the subject string, which prints as text a golden would record as
' expected; a precedence error turns `{number}"12" + 1` into 121, which is a
' perfectly good number.

tally = { checks: 0, mismatches: 0 }

' A PARITY CHECK IS NOT A STRING COMPARISON, and that was MEASURED rather than
' reasoned: with the inline form deliberately broken so it returns its subject
' untouched, SEVEN of the ten pairs below still passed a `string(got) =
' string(want)` check -- because `string(a datetime)` and the text it was
' parsed from are the same characters. That is the finio OFX defect exactly,
' where amounts shipped as TEXT for the adapter's whole life because every
' assertion about them went through `string`. So parity is asserted on the
' TYPE, on the VALUE (PLAT-EQ's `=`, which for these kinds is not a numeric
' coercion), and on the rendering -- all three, because each alone is
' satisfied by something that is not this.
function check_same(label, got, want)
    tally.checks = tally.checks + 1
    if type(got) != type(want) then
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": type " + type(got) + ", want " + type(want)
    else if not (got = want) then
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": values differ (" + string(got) + " vs " + string(want) + ")"
    else if string(got) != string(want) then
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": renders as " + string(got) + ", want " + string(want)
    else
        print "ok   " + label
    end if
    return nothing
end function

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

library housestyle
    export modifier shout for assign
        return upper(value)
    end modifier
end library

load housestyle
load dates

print "-- THE LOAD-BEARING TIER: the inline form and the clause form agree"
' They must not be able to disagree about what a modifier MEANS, so there is
' one implementation and the inline form is a second CALLER of it rather than
' a second copy. Asserted per modifier kind, because a copy would most likely
' diverge on the kinds it forgot.
'
' Each pair binds the clause form to a name and compares it against the inline
' form of the same modifier over the same subject. A tier asserting only that
' the inline form "produced something" passes on one that produced anything.

a {date}= "2026-09-22"
check_same("date agrees", {date}"2026-09-22", a)

b {datetime}= "2026-09-22 14:30:00"
check_same("datetime agrees", {datetime}"2026-09-22 14:30:00", b)

c {time}= "14:30:00"
check_same("time agrees", {time}"14:30:00", c)

d {USD}= 19.99
check_same("USD agrees", {USD}19.99, d)

e {JPY}= 1200
check_same("JPY agrees", {JPY}1200, e)

f {file}= "/etc/hostname"
check_same("file agrees", {file}"/etc/hostname", f)

g {dir}= "/etc"
check_same("dir agrees", {dir}"/etc", g)

h {trimmed}= "  spaced  "
check_same("a stdlib modifier agrees", {trimmed}"  spaced  ", h)

i {shout}= "quiet"
check_same("a user modifier agrees", {shout}"quiet", i)

j {housestyle.shout}= "quiet"
check_same("a LIBRARY-qualified modifier agrees", {housestyle.shout}"quiet", j)

print ""
print "-- and they agree on the TYPE, not merely on how it renders"
' string(money) and the decimal text it was parsed from are the same
' characters, which is how finio's OFX adapter shipped amounts as TEXT for its
' whole life. So the kind is asserted separately from the value.
check("inline {USD} is money", type({USD}19.99), "money")
check("inline {file} is a file", type({file}"/etc/hostname"), "file")
check("inline {date} is a datetime", type({date}"2026-09-22"), "datetime")

print ""
print "-- PRECEDENCE: it binds like unary minus, tighter than any operator"
' The discriminating case. `{number}"12" + 1` is 13 if the modifier took the
' string alone and 121 if it took the whole sum -- both ordinary numbers, and
' only one of them is this language.
check("the modifier takes its operand, not the expression", {number}"12" + 1, 13)
check("a parenthesised subject is still available", {number}("12" + "1"), 121)

print ""
print "-- POSITIONS: everywhere an expression may go"
check("as an argument", bytes({file}"/etc/hostname") > 0, true)
arr = [ {USD}1.50, {USD}2.50 ]
check("as an array element", string(arr[0] + arr[1]), "4.00")
rec = { due: {date}"2026-01-31" }
check("as a record field value", rec.due.month, 1)
check("as a field receiver", ({date}"2026-09-22").year, 2026)
check("as an operand", ({USD}1.00) + ({USD}2.00), {USD}3.00)
if ({date}"2026-09-22").year > 2000 then
    check("as a condition", true, true)
else
    check("as a condition", false, true)
end if
check("applied to a computed subject", type({file}("/etc" + "/hostname")), "file")
check("applied to a variable", type({file}("/etc/hostname")), "file")

print ""
print "-- CONTROL: the clause forms the lexer does NOT claim still work"
' Without this the change is satisfied by a lexer that ate every brace. These
' three shapes fall through to the LBRACE/lens-content path exactly as before.
k {split ","}= "a,b,c"
check("a clause WITH ARGUMENTS still parses", k[1], "b")

m {end of month}= {date}"2026-02-10"
check("a MULTI-WORD clause still parses", m.day, 28)

print ""
print "-- CONTROL: record literals are untouched"
' `{IDENT}` was a parse error before this change, so nothing can have changed
' meaning -- but a lexer claiming one brace too many would take a record with
' it, and every one of these still has to read as a record.
empty = {}
check("the empty record", type(empty), "record")
spaced = { }
check("a record of spaces", type(spaced), "record")
one = { a: 1 }
check("a one-field record", one.a, 1)
named = { date: "not a modifier" }
check("a field NAMED like a modifier", named.date, "not a modifier")
multi = {
    a: 1,
    b: 2
}
check("a record across lines", multi.b, 2)
nested = { inner: { n: 7 } }
check("a nested record", nested.inner.n, 7)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

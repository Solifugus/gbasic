' A keyword used as a FIELD NAME is a name, not a keyword.
'
' Until 2026-08-31 the lexer classified it as a keyword and the grammar mapped
' every keyword token to a canonical LOWERCASE spelling, so `{ OR: 1 }` stored
' the key "or", `r["OR"]` missed, and `{ OR: 1, or: 2 }` silently produced a
' record with two fields both called "or" -- two spellings the language
' otherwise treats as distinct, collapsed into one.
'
' SELF-CHECKING, because the defect produced a perfectly ordinary record. A
' golden would have recorded `or,or` as the expected keys.

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

' ------------------------------------------------------- case is preserved
r = { OR: "upper", or: "lower" }
check("a keyword key keeps its case", join(sort(keys(r)), ","), "OR,or")
check("and the two are distinct keys", r["OR"], "upper")
check("each holding its own value", r["or"], "lower")

' THE CONTROL: an ordinary identifier already behaved this way, and must still.
n = { Name: 1, name: 2 }
check("a non-keyword name is unchanged", join(sort(keys(n)), ","), "Name,name")

' -------------------------------------------------- dot access agrees
' Without this the literal and the access disagree, which is worse than the
' original defect: two keys stored, one reachable.
check("dot access reads the upper-case field", r.OR, "upper")
check("and the lower-case one", r.or, "lower")

' ------------------------------------------------ keywords still work as keys
' rc6's guarantee, unchanged: the keyword list is large and these are the ones
' most likely to be wanted as field names.
k = { on: 1, to: 2, in: 3, as: 4, end: 5, error: 6, from: 7, step: 8 }
check("eight keyword keys construct", count(keys(k)), 8)
check("and read back by dot", string(k.on) + string(k.to) + string(k.in) + string(k.as), "1234")
check("including ones that end a block", string(k.end) + string(k.error), "56")

' ------------------------------------------------ nothing else moved
' A brace MODIFIER contains no colon, which is why the context test is exact
' rather than heuristic -- but assert it, since the fix keys on `{`.
m {USD}= "5.00"
check("a currency modifier still works", string(m), "5.00")
t {trimmed}= "  hi  "
check("a string modifier still works", t, "hi")

' Nested braces, and a keyword key inside one.
deep = { a: 1, b: { to: 2, c: [3, 4] } }
check("a keyword key nested inside another record", deep.b.to, 2)
check("and an array beside it", deep.b.c[1], 4)

' Control flow is untouched: these keywords are NOT followed by a colon.
total = 0
for i = 1 to 3
    if i != 2 then
        total = total + i
    end if
next
check("for/if/then/next still parse and run", total, 4)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

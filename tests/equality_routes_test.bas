' PLAT-EQ: ONE DEFINITION OF EQUALITY, asked through every route.
' Self-checking; run by tests/run_equality.sh.
'
' The three PLAT-EQ fixes each corrected ONE site and each left the others to
' be discovered later: compounds (August), strings (2026-09-05), and every kind
' with no branch at all (2026-09-06, found by adding a thirteenth value kind).
' The lesson those three share is not about any of the defects -- it is that
' `=` is not the only thing in this language that decides whether two values
' are the same, and fixing the operator says nothing about the rest.
'
' FIVE ROUTES ASK THE QUESTION. `=`, `contains`, `find`, `remove_value` and
' `consider` must give the SAME answer about the same pair. Four of them reach
' `eval_comparison`, so they agree by construction -- and THAT IS THE PROPERTY
' UNDER TEST, not an excuse to skip it: `unique` had its own switch and
' disagreed, so `unique([0, false])` returned BOTH elements and `contains` then
' reported each of them present, an array `unique` had just called
' duplicate-free holding what `contains` calls a duplicate.
'
' THE WATCHER IS DELIBERATELY NOT IN THAT SET, and the last tier says why. It
' answers a DIFFERENT QUESTION -- did the stored value change -- and on the
' language's one real coercion the two answers must differ: `1 = true`, but a
' variable holding 1 that is assigned `true` HAS changed, and a watcher that
' stayed silent would leave the program holding a boolean while reporting
' nothing. Asserting "every route agrees" without that exception would be
' asserting something false.

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

' Ask all five and require one answer. `want` is stated by the caller, so this
' cannot pass by having every route agree on the WRONG answer.
function routes(label, a, b, want)
    op = (a = b)
    has = contains([a], b)
    idx = contains([b], a)
    gone = count(remove_value([a], b)) = 0
    con = false
    consider a
    if b then
        con = true
    else
        con = false
    end consider
    check(label + " -- operator", op, want)
    check(label + " -- contains", has, want)
    check(label + " -- contains, reversed", idx, want)
    check(label + " -- remove_value removed it", gone, want)
    check(label + " -- consider matched", con, want)
    return nothing
end function

d1{datetime}= "2026-01-01"
d2{datetime}= "2026-01-01 00:00:00"
d3{datetime}= "2026-01-02"
m1{USD}= "1.00"
m2{EUR}= "1.00"
m3{USD}= "1.00"
f1{file}= "/etc/hostname"
f2{file}= "/etc/passwd"

routes("equal strings", "x", "x", true)
routes("different strings", "x", "y", false)
routes("a number and a string", 0, "", false)
routes("a number and a word", 0, "stop", false)
routes("the number 0 and false", 0, false, true)
routes("the number 1 and true", 1, true, true)
routes("equal arrays", [1, 2], [1, 2], true)
routes("different arrays", [1, 2], [3], false)
routes("equal records", { a: 1 }, { a: 1 }, true)
routes("different records", { a: 1 }, { b: 2 }, false)
routes("same instant, different precision", d1, d2, true)
routes("different instants", d1, d3, false)
routes("same currency and amount", m1, m3, true)
routes("same amount, different currency", m1, m2, false)
routes("two different files", f1, f2, false)
routes("nothing and nothing", nothing, nothing, true)
routes("nothing and unknown", nothing, unknown, false)

' --- unique is the route that had its own answer ----------------------------
' It used to require both kinds to match, so it kept values every other route
' calls equal. What counts as a duplicate is not `unique`'s to decide.
check("unique folds 0 and false, as contains says it must",
      count(unique([0, false])), 1)
check("unique folds 1 and true", count(unique([1, true])), 1)
check("unique folds two spellings of one instant", count(unique([d1, d2])), 1)
check("unique keeps genuinely different values", count(unique([1, 2, 3])), 3)
check("unique keeps different instants", count(unique([d1, d3])), 2)
check("and the array it returns holds no duplicate by contains",
      contains(remove_value(unique([0, false]), 0), false), false)

' --- THE WATCHER ANSWERS A DIFFERENT QUESTION -------------------------------
' Not a route in the set above. `1 = true` is the language's one coercion, and
' a variable holding 1 that is assigned `true` HAS changed -- the watcher must
' fire, and the operator must still say equal. Both halves are asserted,
' because either alone reads as an oversight.
coerce_fired = 0
coerce = { v: 1 }
watch(coerce.v)
    coerce_fired = coerce_fired + 1
end watch
before = coerce_fired
coerce.v = true
check("the operator calls 1 and true equal", 1 = true, true)
check("...and the watcher still fires, because the stored value changed",
      coerce_fired > before, true)

' The control: where the two questions have the same answer, they agree. An
' equal COMPOUND assigned over itself is not a change, and the watcher is
' silent -- which is what says the tier above is about the coercion and not
' about watchers firing on everything.
same_fired = 0
same = { v: [1, 2] }
watch(same.v)
    same_fired = same_fired + 1
end watch
before2 = same_fired
same.v = [1, 2]
check("an equal array is not a change", same_fired > before2, false)

changed_fired = 0
changed = { v: [1, 2] }
watch(changed.v)
    changed_fired = changed_fired + 1
end watch
before3 = changed_fired
changed.v = [1, 3]
check("a different array is a change", changed_fired > before3, true)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

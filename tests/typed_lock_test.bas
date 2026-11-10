' TYPED VARIABLES: `x:number = e` LOCKS x (docs/typed_variables_design.md).
' Self-checking; run by tests/run_typed_locks.sh.
'
' Each behaviour is checked beside its CONTROL, the same statement on a
' variable with no lock, so a lock that quietly did nothing would show as a
' mismatch rather than pass: an unlocked `y = "5"` stays text, a locked one is
' 5; an unlocked `y += "1"` is 421, a locked one is 43.

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if type(got) = type(want) and string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + " (" + type(got) + "), want " + string(want) + " (" + type(want) + ")"
    end if
    return nothing
end function

' What a body raised, or ANSWERED and its value when it did not. The line a
' lock was declared on is taken out, so moving a check does not move every
' expected message; one check below holds the line itself.
function refusal(f)
    on error goto handle
    r = f()
    return "ANSWERED " + string(r)
handle:
    m = error.message
    error.clear()
    return replace(m, regex(" at line [0-9]+,"), ",")
end function

function lock_a_time_of_day()
    k:date = "2026-01-01"
    k = {datetime}"2026-01-02T10:30:00"
    return k
end function

function raw_refusal(f)
    on error goto handle
    r = f()
    return "ANSWERED " + string(r)
handle:
    m = error.message
    error.clear()
    return m
end function

print "-- THE LOCK CONVERTS"
x:number = "42"
check("x:number = \"42\" is the number 42", x, 42)
x = "5"
check("  and a later plain = converts too", x, 5)
y = "5"
check("  control: with no lock, = keeps the text", y, "5")
x = " 7 "
check("  the conversion is {number}'s, which trims", x, 7)
s:string = 3.5
check("x:string = 3.5 is the text \"3.5\"", s, "3.5")
t:text = 12
check("text is the same lock as string", t, "12")
m:money(USD) = "1.25"
check("money(USD) from text", m, {USD}"1.25")
m = 2
check("  and from a number", m, {USD}2)
d:date = "2026-01-02"
check("date from text", d, {date}"2026-01-02")
dt {datetime}= "2026-01-02T10:30:00"
' a datetime VALUE with a time of day is refused, as its text is (book gap G40,
' Matthew's ruling 2026-11-08): it used to convert to its date in silence
check("  a datetime value with a time of day is refused", contains(refusal(lock_a_time_of_day), "it has a time of day (10:30:00), which a date would lose"), true)
d = {datetime}"2026-01-02T00:00:00"
check("  a datetime at midnight converts to its date", d, {date}"2026-01-02")
d = {date}dt
check("  and {date} takes the date when that is meant", d, {date}"2026-01-02")
tm:time = dt
check("time from a datetime value", tm, {time}"10:30:00")
when:datetime = "2026-01-02"
check("datetime from a date's text", when, {datetime}"2026-01-02T00:00:00")
b:boolean = " TRUE "
check("boolean from the text true, any case, trimmed", b, true)
b = "false"
check("  and false", b, false)
du:duration = 3 days
check("duration holds a duration", du, 3 days)
f:file = "/etc/hostname"
check("file from text", type(f), "file")
g:dir = "/tmp"
check("dir from text", type(g), "directory")

print ""
print "-- ABSENCE IS ALLOWED"
x = nothing
check("a locked number may hold nothing", x, nothing)
x = unknown
check("  and unknown", is_unknown(x), true)
x = "8"
check("  and the lock is still there after", x, 8)

print ""
print "-- RELOCKING IS WRITTEN OUT"
x:text = "abc"
check("x:text = \"abc\" relocks", x, "abc")
x = 9
check("  and later values convert to text", x, "9")
x:number = "1"
check("relocking back to number", x, 1)
z = "free"
z:number = "3"
check("locking a variable that had no lock", z, 3)

' A declaration of the same name is a redeclaration too: it replaces the lock
' rather than being refused by it, and a later = follows the new value.
watched = 0
wname:string = "text"
watch wname(watched)
    seen = 1
end watch
check("a watch declaration replaces the lock with its handle", type(wname), "watcher")
wname = 5
check("  so a later = is not converted to text", wname, 5)

print ""
print "-- A COMPOUND ASSIGNMENT CONVERTS TEXT FIRST"
n:number = 42
n += "1"
check("n:number then n += \"1\" is 43", n, 43)
u = 42
u += "1"
check("  control: with no lock it is 421", u, "421")
n -= "3"
check("  -= converts too", n, 40)
cost:money(USD) = "1.00"
cost += "0.25"
check("money += text is money", cost, {USD}"1.25")
day:date = "2026-01-02"
day += 3 days
check("date += a duration is date arithmetic, not a conversion", day, {date}"2026-01-05")
words:string = "a"
words += 1
check("a string lock still concatenates", words, "a1")

print ""
print "-- A COMPARISON READS TEXT AS THE LOCKED TYPE"
q:number = 1
check("q:number, q = \"1\"", q = "1", true)
check("  either side", "1" = q, true)
check("  ordering too", q < "2", true)
check("  != \"1.0\" is false: 1.0 is 1", q != "1.0", false)
free = 1
check("  control: with no lock, 1 = \"1\" stays false", free = "1", false)
on_day:date = "2026-01-02"
check("a locked date against its text", on_day < "2026-02-01", true)
price:money(USD) = "1.25"
check("a locked money against its text", price = "1.25", true)

function reads_global()
    return q = "1"
end function
check("a function reading the locked global reads text the same way", reads_global(), true)

print ""
print "-- THE LOCK BELONGS TO THE VARIABLE IN ITS OWN SCOPE"
k:number = 1
function local_k()
    k = "local text"
    return k
end function
check("a function's own k is a different variable, with no lock", local_k(), "local text")
check("  and the global k is untouched", k, 1)

function takes(v)
    v = "now text"
    return v
end function
check("passing a locked variable passes its value, not its lock", takes(k), "now text")

print ""
print "-- REFUSALS NAME THE LOCK"
function bad_text()
    y:number = 1
    y = "abc"
    return y
end function
check("text that is no number", refusal(bad_text),
      "y is locked to number by y:number, and this value does not convert: number modifier expects a valid number string")

check("  the refusal names the line the lock was declared on",
      contains(raw_refusal(bad_text), "by y:number at line " + "183" + ","), true)

function bad_array()
    y:number = 1
    y = [1]
    return y
end function
check("an array", refusal(bad_array),
      "y is locked to number by y:number, and this value does not convert: a number lock cannot hold an array")

function bad_currency()
    y:money(USD) = 1
    y = {EUR}2
    return y
end function
check("money in another currency", refusal(bad_currency),
      "y is locked to money by y:money(USD), and this value does not convert: this is money in another currency; convert it at a rate first")

function bad_boolean()
    y:boolean = 1
    return y
end function
check("a number is not a boolean", refusal(bad_boolean),
      "y is locked to boolean by y:boolean, and this value does not convert: a boolean is true or false, or the text \"true\" or \"false\"")

function bad_duration()
    y:duration = "3 days"
    return y
end function
check("text is not a duration", refusal(bad_duration),
      "y is locked to duration by y:duration, and this value does not convert: only a duration converts to a duration (write 3 days, 90 minutes)")

function bad_unknown_currency()
    y:money(XQZ) = 1
    return y
end function
check("a currency gBASIC does not know", refusal(bad_unknown_currency),
      "money(XQZ): XQZ is not a currency gBASIC knows (register it with money.register first)")

function bad_compare()
    y:number = 1
    return y = "abc"
end function
check("comparing with text that does not convert", refusal(bad_compare),
      "y is locked to number by y:number, and this value does not convert: number modifier expects a valid number string")

function kept_after_refusal()
    on error goto next
    y:number = 1
    y = "abc"
    error.clear()
    return y
end function
check("a refused assignment leaves the old value", kept_after_refusal(), 1)

function bad_loop()
    y:string = "a"
    for each y in ["b"]
        print "the loop ran"
    end for
    return y
end function
check("a loop cannot bind a locked variable", refusal(bad_loop),
      "y is locked to string by y:string, and a loop cannot bind a locked variable; use another name for the loop")

i:number = 0
for i = 1 to 3
    total = i
end for
check("  except a number lock on a counter, which only holds numbers", i, 3)
i = "9"
check("  and the counter keeps its lock after the loop", i, 9)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

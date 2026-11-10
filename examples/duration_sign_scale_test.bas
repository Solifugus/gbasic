' A DURATION'S SIGN, AND SCALING IT TO WHOLE SECONDS (DOGFOOD rows 13 and 14,
' 2026-11-08). Each refusal sits beside the neighbour that is accepted.
'
' Row 13: `-(2 hours)` was refused as not a number while `0 seconds - 2 hours`
' answered. Negation now takes a duration, field by field, so a literal keeps
' its shape, and it equals the subtraction from zero, which normalises.
' Row 14: `(1 second) * 2.5` answered 3 seconds with no word. Seconds are the
' resolution, so a result between two whole seconds is refused, as a month
' scaled to a fraction already was.

' --- row 13: negation -------------------------------------------------------
print -(2 hours)
print -(90 minutes)
print -(1 year + 2 days)
print -(1 month 3 days 4 hours 5 minutes 6 seconds)
print -(0 seconds)
print -(-(3 weeks))
d = 45 minutes
print -d
print 1 day + -(6 hours)
' equal to the subtraction from zero, for every shape below
shapes = [2 hours, 90 minutes, 1 year + 2 days, 1 month, 3 weeks, 0 seconds, 7 seconds - 1 minute]
all_equal = true
for each s in shapes
    if -s != 0 seconds - s then
        all_equal = false
        print "differs: " + string(s)
    end if
    if -(-s) != s then
        all_equal = false
        print "twice differs: " + string(s)
    end if
end for
print "-d = 0 seconds - d and -(-d) = d for each: " + string(all_equal)
print is_unknown(-(unknown))
day {date}= "2026-03-10"
print day + -(1 day)
' money still negates as it did
print -({USD}"2.50")

' --- row 14: scaling to whole seconds ---------------------------------------
print (1 hour) * 0.5
print (1 hour) / 3
print (10 seconds) * 0.1
print (90 seconds) * 2
print 3 * (20 minutes)
print (1 minute) * 1.5
print (1 month) * 2
print (1 day) / 4
on error goto next
x = (1 second) * 2.5
print "(1 second) * 2.5: " + error.message
error.clear()
x = (1 hour) / 7
print "(1 hour) / 7: " + error.message
error.clear()
x = (1 minute) * 0.001
print "(1 minute) * 0.001: " + error.message
error.clear()
x = (1 month) * 2.5
print "(1 month) * 2.5: " + error.message
error.clear()
' the way round it, with the seconds rounded where the reader can see it
n = 3600 / 7
print (1 second) * round(n)

' `{duration}`: A DURATION FROM A NUMBER OF SECONDS OR FROM ITS TEXT (DOGFOOD
' 2026-10-16, live-list row 11). The typed-modifier family had `{date}`,
' `{datetime}`, `{time}` and `{USD}` and no way in for a duration, which a
' program stores as a number (strict JSON refuses a duration) and must turn
' back. `(1 second) * n` worked and nobody would guess it. Each refusal sits
' beside the neighbour that is accepted.

' --- a number of seconds: the same value `(1 second) * n` makes -------------
n = 15552000
d {duration}= n
print d
print d = (1 second) * n
print d.total_seconds = n
print {duration}(90)
print {duration}(-90)
print {duration}(0)

' --- text: a duration as gBASIC prints it, read field for field -------------
print {duration}"2 hours 30 minutes"
print ({duration}"2 hours 30 minutes").total_seconds
print {duration}"24 hours"
print {duration}"1 month 3 days"
' the round trip: what print shows reads back as the same value
x = 1 year + 2 weeks + 5 seconds
print {duration}string(x) = x

' --- a duration is itself ----------------------------------------------------
print {duration}(45 minutes)

' --- refused, each naming why --------------------------------------------------
on error goto next
y = {duration}2.5
print "2.5: " + error.message
error.clear()
y = {duration}"2 parsecs"
print "2 parsecs: " + error.message
error.clear()
y = {duration}"1.5 hours"
print "1.5 hours: " + error.message
error.clear()
y = {duration}true
print "true: " + error.message
error.clear()
' the way round a fraction, rounded where the reader can see it
print {duration}round(2.5)

' The (datetime)= assignment modifier builds a full second-precision timestamp
' from an ISO-like string. A date-only string fills 00:00:00; a full timestamp is
' preserved. This matches the value now() produces, and is distinct from (date)=,
' which infers precision from the string.

a (datetime)= "2026-05-15"
b (datetime)= "2026-05-15 12:05:03"

print string(a)
print string(b)
print type(a)

if a {day}= b then
    print "same day via lens"
end if

if a != b then
    print "different instant"
end if

' A bare (date)= of the same day is day-precise, so an exact compare differs.
c (date)= "2026-05-15"
if a != c then
    print "datetime is second-precise, date-only is day-precise"
end if

print string(a + 2 hours)

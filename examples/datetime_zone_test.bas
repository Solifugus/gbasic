' Zones, instants, and the trap between them.
'
' A gBASIC datetime carries NO ZONE, so the language fixes one: a datetime that
' stands for a moment is UTC (since 2026-11-10). now(), from_epoch() and file
' times answer UTC; epoch(dt) and number(dt) read UTC; and the zone is chosen
' at the edge that shows a person a time -- to_zone(dt, zone), now(zone), with
' "local" naming the system's own. Before, now()/epoch(dt)/number(dt) were
' LOCAL while to_zone read its input as UTC, so a program could ask for UTC,
' be handed local time, and label it UTC.
'
' This fixture pins the shapes that are checkable WITHOUT knowing the machine's
' zone -- relationships, not wall-clock digits. tests/run_utc_clock.sh runs a
' fuller program under five TZ settings.

results = []

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

' `epoch()` is the instant, and now() read back is that instant: no zone
' needed, because now() is UTC and epoch(dt) reads UTC.
e = epoch()
u = now()
l = now("local")
append(results, check("epoch(now()) is the current instant", true, abs(epoch(u) - e) <= 2))
append(results, check("epoch(now(\"UTC\"), \"UTC\") is the SAME instant", true, abs(epoch(now("UTC"), "UTC") - e) <= 2))

' `number` and `epoch` read a datetime the same way, and that way is UTC: the
' zone form naming "UTC" agrees exactly. (Before 2026-11-10 both were off by
' the local offset here.)
append(results, check("number(dt) and epoch(dt) read a value the same way", number(u), epoch(u)))
append(results, check("...and both read it as UTC", 0, epoch(u) - epoch(u, "UTC")))

' The local wall clock is an edge: now("local") read back in "local" is the
' same instant, and to_zone(..., "local") is the conversion to it.
append(results, check("now(\"local\") read in \"local\" is the instant", true, abs(epoch(l, "local") - e) <= 2))
append(results, check("to_zone(now(), \"local\") read in \"local\" is now()", epoch(u), epoch(to_zone(u, "local"), "local")))

' Two named zones that are never equal to each other: Tokyo has no DST and is
' always ahead of UTC by 9 hours.
t = now("Asia/Tokyo")
' Tokyo's wall clock is 9 hours ahead, so reading its digits AS UTC lands 9
' hours later than the same moment's UTC digits. No DST there, so this holds
' year-round -- which is why Tokyo and not a European zone.
append(results, check("Tokyo civil read as UTC is 9 hours later", 32400, epoch(t, "UTC") - epoch(u, "UTC")))

' A zone name that does not exist is refused, not guessed.
on error goto next
bad = now("Mars/Olympus")
if error then
    append(results, check("an unknown zone is refused", true, contains(error.message, "unknown timezone")))
    error.clear()
end if

' `exit` refuses a status the kernel would truncate.
on error goto next
exit(256)
if error then
    append(results, check("exit(256) is refused, not truncated to 0", true, contains(error.message, "0..255")))
    error.clear()
end if

bad_count = 0
for each v in results
    if not v then
        bad_count += 1
    end if
next v

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad_count)

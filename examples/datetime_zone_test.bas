' Zones, instants, and the trap between them.
'
' A gBASIC datetime carries NO ZONE -- it is civil wall-clock text. So every
' conversion between a datetime and an instant has to get a zone from
' somewhere, and the two halves of the API used to get it from different
' places: `to_zone` reads its input as already-UTC, while `number`/`epoch` read
' it as local. Both are defensible alone; together they let a program ask for
' UTC, be handed local time, and label it UTC.
'
' This fixture pins the shapes that are checkable WITHOUT knowing the machine's
' zone -- relationships, not wall-clock digits. tests/run_core.sh asserts the
' actual clock against `date`, which is the part only the world can answer.

results = []

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

' `epoch()` is the instant. Reading the LOCAL civil value back as local, and
' the UTC civil value back as UTC, must land on that same instant -- that is
' the whole consistency claim, and it holds wherever this runs.
e = epoch()
l = now()
u = now("UTC")
append(results, check("epoch(local) is the current instant", true, abs(epoch(l) - e) <= 2))
append(results, check("epoch(utc, UTC) is the SAME instant", true, abs(epoch(u, "UTC") - e) <= 2))

' The trap, stated so it cannot quietly change. `number` and `epoch` read a
' datetime the same way -- as LOCAL -- so on the UTC value they agree with each
' other and BOTH disagree with the truth by exactly the local UTC offset. On a
' UTC machine the offset is zero and these are equal; anywhere else the gap is
' the bug, and it is the same gap either way you measure it.
append(results, check("number(dt) and epoch(dt) read a value the same way", number(u), epoch(u)))
append(results, check("...and both are off by the local offset", epoch(u) - epoch(u, "UTC"), 0 - (epoch(l, "UTC") - epoch(l))))

' `to_zone` is not a conversion FROM local. Feeding it a local value returns
' that value unchanged -- the no-op that reads like a conversion.
append(results, check("to_zone(local, UTC) returns the input unchanged", string(l), string(to_zone(l, "UTC"))))

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

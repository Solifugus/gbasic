' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `timer` -- periodic work on the event loop. See docs/timer_design.md.
'
' SELF-CHECKING RATHER THAN GOLDEN, and forced: every defect here is a
' PLAUSIBLE NUMBER OF TICKS. A timer firing twice as often, half as often, or
' one that silently caught up after a stall all produce output that reads
' exactly like a working timer, and a golden would record whichever count came
' out and defend it.

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

print "-- the descriptor says what was asked for"
a = timer.every(0.5)
b = timer.after(1.5)
check("every is repeating", a.repeating, true)
check("after is not", b.repeating, false)
check("the interval comes back", a.interval, 0.5)
check("ids differ", a.id != b.id, true)
check("cancel a live timer answers true", timer.cancel(a), true)
' AN ORDINARY OUTCOME, NOT A MISTAKE: a timer going away twice is what a
' program that cancels defensively does, unlike the refusals below.
check("cancelling it again answers false", timer.cancel(a), false)
check("and so does the other one", timer.cancel(b), true)

print ""
print "-- refusals, each beside its nearest legal neighbour"
on error goto next
timer.every(0)
check("a zero interval is refused", contains(error.message, "greater than zero"), true)
error.clear()
timer.every(0 - 1)
check("so is a negative one", contains(error.message, "greater than zero"), true)
error.clear()
timer.every("half")
check("a non-number is refused", contains(error.message, "expects a number of seconds"), true)
error.clear()
timer.cancel(5)
check("cancelling a non-timer is refused", contains(error.message, "expects a timer"), true)
error.clear()
timer.nosuch(1)
check("an unknown function is named", contains(error.message, "invalid function call: timer.nosuch"), true)
error.clear()
on error stop
' CONTROL: without these the refusal tier is satisfied by a module that
' refuses everything.
tiny = timer.every(0.001)
check("CONTROL: a very small interval is accepted", tiny.interval, 0.001)
c = timer.cancel(tiny)
big = timer.after(3600)
check("CONTROL: a long one too", big.repeating, false)
c = timer.cancel(big)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

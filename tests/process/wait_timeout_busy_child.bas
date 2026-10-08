' `process.wait(h, timeout)` ON A CHILD THAT IS STILL PRODUCING.
'
' THE DEFECT THIS PINS, present from the day `process.wait` was written until
' 0.6.1: the wait loop polled with a 20ms tick and then did `elapsed_ms += slice`,
' charging the full tick however long `poll` ACTUALLY blocked -- and `poll` returns
' IMMEDIATELY when a pipe has data. So a child producing output continuously cost
' microseconds of real time and 20ms of budget per iteration, and a SIXTY-SECOND
' timeout was exhausted in a few milliseconds.
'
' MEASURED at v0.5.1: 2 MB to each of stdout and stderr, `process.wait(h, 60)`
' returned in 0.02s having read 568,000 bytes of 2,000,000, with `running: true`.
'
' THE SYMPTOM IS INDISTINGUISHABLE FROM A REAL TIMEOUT, which is why it is worth a
' fixture rather than a note: `running: true` IS this call's contract for "the wait
' expired", so a caller sees a plausible timeout and partial output, and the bytes
' it never read are gone from its side of the pipe.
'
' WHY NOTHING HERE CAUGHT IT: the UNTIMED form was always correct, and
' `tests/native_platform/plat_proc_basic.bas` -- the only Linux fixture that waits
' on a child at all -- calls `process.wait(h)` with no timeout. Nothing on this
' platform waited WITH a timeout on a busy child. The Windows port's
' `tests/windows/process_start.bas` was the first thing to do it, on a machine
' nobody expected to find a Linux bug on; this fixture is the Linux-side
' counterpart, so the coverage no longer depends on a file named for another
' platform.

' A RECORD, not two scalars: a function cannot rebind an outer scalar (gBASIC has
' no closures), so a plain `checks = checks + 1` inside `ok` would silently write a
' function-local and the tally would stay at zero -- which is run_core.sh's
' read-then-shadow rule, and the reason this file has no `program` block: a
' top-level statement beside one never runs.
tally = { checks: 0, bad: 0 }

function ok(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print("ok   " + label)
    else
        tally.bad = tally.bad + 1
        print("MISMATCH " + label + ": got [" + string(got) + "] want [" + string(want) + "]")
    end if
    return nothing
end function

me_i = process.self()
child = replace(me_i.script, "wait_timeout_busy_child.bas", "wait_timeout_busy_child_child.bas")
print "-- a busy child, waited WITH a timeout --"
' 20000 lines of 99 characters plus a newline, to BOTH streams: 2,000,000
' bytes each, far past any pipe buffer, so a wait that does not drain while it
' waits would deadlock and one that mis-bills its budget returns early.
h = process.start({ command: me_i.interpreter,
                    args: ["--line-buffered", child, "20000"] })
s = process.wait(h, 60)
c = process.read(h)
ok("the child finished       ", s.running, false)
ok("every byte of stdout     ", len(c.stdout), 2000000)
ok("every byte of stderr     ", len(c.stderr), 2000000)
ok("and it exited cleanly    ", s.exit_code, 0)
process.release(h)

print "-- the UNTIMED form, which was always right --"
' The control that says the fix did not simply make both forms do what the
' untimed one did by accident: they must agree, and that agreement is the
' claim. Before the fix these two answered differently on identical input.
h2 = process.start({ command: me_i.interpreter,
                     args: ["--line-buffered", child, "20000"] })
s2 = process.wait(h2)
c2 = process.read(h2)
ok("timed and untimed agree  ", string(s2.running) + "/" + string(len(c2.stdout)),
                                string(s.running) + "/" + string(len(c.stdout)))
process.release(h2)

print "-- AND THE TIMEOUT STILL EXPIRES, which is the other half --"
' Without this the fix is satisfied by a wait that never times out at all --
' the over-correction, and the more dangerous one, since a program waiting on a
' hung child would hang with it. A 2-second sleeper, waited 0.5s.
h3 = process.start({ command: me_i.interpreter, args: [child, "sleep"] })
t0 = monotonic()
s3 = process.wait(h3, 0.5)
waited = monotonic() - t0
ok("it expired               ", s3.running, true)
ok("after about the timeout  ", waited >= 0.4 and waited < 2.0, true)
' MEASURED AS A RANGE, never an instant: an exact figure would be a fact about
' this machine. The upper bound is what matters -- 2.0 is the sleeper's own
' length, so exceeding it means the wait ran to completion instead of expiring.
process.stop(h3, { force_after: 1 })
process.release(h3)

print("")
print("checks: " + string(tally.checks))
print("mismatches: " + string(tally.bad))

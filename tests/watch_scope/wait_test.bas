' `wait until <condition> [for <limit>]` (docs/watch_scope_design.md §4,
' Matthew's rulings 2026-11-07): the event loop runs from inside a function,
' delivering to every live frame's watches, until the condition is true. Every
' answer is written by hand. Run by tests/run_watch_scope.sh.

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if type(got) = type(want) then
        if got = want then
            return nothing
        end if
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + type(got) + " " + string(got) + ", want " + type(want) + " " + string(want)
    return nothing
end function

function message_of(e)
    if e then
        return e.message
    end if
    return "no error"
end function

' a function watches the timer into its own local and waits
function count_ticks(n)
    seen = 0
    t = timer.every(0.02)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            seen = seen + 1
        end while
    end watch
    wait until seen >= n for 10 seconds
    timer.cancel(t)
    return seen
end function

' a condition already true does not run the loop at all
function already_true()
    x = 1
    wait until x = 1
    return "returned"
end function

' the limit: raised when it passes, the condition still false
function times_out()
    t = timer.every(0.02)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
        end while
    end watch
    on error goto next
    wait until false for 0.2
    e = error
    timer.cancel(t)
    return message_of(e)
end function

' nothing left that could end it: raised, not a hang
function nothing_to_wait_for()
    on error goto next
    wait until false
    e = error
    return message_of(e)
end function

' a limit that is no duration
function bad_limit(limit)
    on error goto next
    wait until true for limit
    e = error
    return message_of(e)
end function

' an actor's reply delivered to a waiting function's watch
function echo_worker()
    m = receive()
    send(m.from, m.text + "!")
end function

function ask_worker(text)
    reply = nothing
    watch(inbox.messages)
        while count(inbox.messages) > 0
            reply = take_first(inbox.messages)
        end while
    end watch
    a = spawn echo_worker()
    send(a, { from: self(), text: text })
    wait until not is_nothing(reply) for 10 seconds
    return reply
end function

' nesting: a wait inside a watch body, inside a wait
function inner_wait(log)
    t = timer.after(0.05)
    fired = false
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            fired = true
        end while
    end watch
    wait until fired for 10 seconds
    append(log, "inner done")
    return log
end function

function outer_wait()
    log = []
    x = 0
    watch(x)
        if x = 1 then
            log = inner_wait(log)
        end if
    end watch
    x = 1
    append(log, "outer done")
    return log
end function

' past the cap: each level's watch body calls the next level, which waits,
' so the waits nest one inside another's watch body until the 17th is refused
function level(k)
    t = timer.after(0.01)
    res = ""
    watch(timer.ticks)
        if count(timer.ticks) > 0 then
            while count(timer.ticks) > 0
                ev = take_first(timer.ticks)
            end while
            if res = "" then
                res = level(k + 1)
            end if
        end if
    end watch
    wait until res != "" for 10 seconds
    return res
end function

function runaway()
    on error goto next
    r = level(1)
    e = error
    return message_of(e)
end function

check("a function waits for its own watch's local", count_ticks(3) >= 3, true)
check("no watch is left behind", count(watchers()), 0)
check("a condition already true returns at once", already_true(), "returned")
check("a limit that passes raises", times_out(), "wait until: the limit of 0.2 seconds passed and the condition is still false")
check("no source left raises rather than hangs", left(nothing_to_wait_for(), 66),
      "wait until: no event source is left that could make the condition ")
check("a duration as the limit", bad_limit(2 seconds), "no error")
check("a number of seconds as the limit", bad_limit(2), "no error")
check("a month is refused as a limit", bad_limit(1 months),
      "wait until ... for: a month has no fixed length; give the limit in days or less")
check("text is refused as a limit", bad_limit("soon"),
      "wait until ... for: the limit is a duration or a number of seconds, at least 0")
check("an actor's reply reaches a waiting function", ask_worker("hi"), "hi!")
check("a wait inside a watch body, inside a function", outer_wait(), ["inner done", "outer done"])
check("and nothing is left behind", count(watchers()), 0)
check("waits nested past the cap are refused", left(runaway(), 31), "wait until: nested 16 deep, ins")
check("and the unwinding leaves no watch", count(watchers()), 0)

' every live frame's watches are delivered to during a wait: a top-level
' watch keeps counting while a function waits on its own
bg = { ticks: 0 }
bt = timer.every(0.02)
watch(timer.ticks)
    bg.ticks = bg.ticks + 1
end watch
check("a function's wait, with a top-level watch beside it", count_ticks(3) >= 3, true)
check("and the top-level watch was delivered to as well", bg.ticks >= 3, true)
timer.cancel(bt)

' a top-level wait
done = { ticks: 0 }
t = timer.every(0.02)
watch(timer.ticks)
    while count(timer.ticks) > 0
        ev = take_first(timer.ticks)
        done.ticks = done.ticks + 1
    end while
end watch
wait until done.ticks >= 2 for 10 seconds
timer.cancel(t)
check("a top-level wait", done.ticks >= 2, true)

' `wait` is still a name everywhere `until` does not follow it
wait = 3
r = { wait: wait * 2 }
check("wait is still a variable", wait, 3)
check("and a record key", r.wait, 6)

print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"

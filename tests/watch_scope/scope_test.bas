' A watch belongs to the frame that declares it (docs/watch_scope_design.md
' §3, Matthew's ruling 2026-11-07): in a function it watches that function's
' own variables, its body runs in that frame, and it ends when the frame does.
' Every answer below is written by hand. Run by tests/run_watch_scope.sh.

load webserver

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

' fires on registration and on each assignment, and the body writes the
' function's own locals
function tally(items)
    total = 0
    seen = []
    watch(total)
        append(seen, total)
    end watch
    for each it in items
        total = total + it
    end for
    return seen
end function

' a callee's local of the same name is the callee's
function assigns_its_own_x()
    x = 100
    x = 200
    return nothing
end function

function caller_watching_x()
    x = 1
    fires = 0
    watch(x)
        fires = fires + 1
    end watch
    assigns_its_own_x()
    x = 2
    return fires
end function

' each level of a recursion has its own watch, on its own n
function depth(n, log)
    level = n
    watch(level)
        append(log, "L" + string(n) + "=" + string(level))
    end watch
    if n > 1 then
        log = depth(n - 1, log)
    end if
    level = level * 10
    return log
end function

' a raise unwinding through the function ends its watch
function raises_after_watching()
    v = 0
    watch(v)
        v = v
    end watch
    error "planned"
end function

function catches()
    on error goto next
    raises_after_watching()
    e = error
    return e.message
end function

' the named form: unwatch inside, and a handle that outlives its frame
function named_inside()
    y = 0
    fires = 0
    watch w(y)
        fires = fires + 1
    end watch
    y = 1
    unwatch(w)
    y = 2
    return fires
end function

function returns_handle()
    z = 0
    watch h(z)
        z = z
    end watch
    return h
end function

function name_of(h)
    on error goto next
    n = h.name
    if error then
        return "refused: " + error.message
    end if
    return n
end function

' refusals, each beside an accepted neighbour
glob = 5
function watches_a_global()
    watch(glob)
        print "never"
    end watch
    return nothing
end function

function watches_a_local_copy()
    mine = glob
    fired = 0
    watch(mine)
        fired = fired + 1
    end watch
    return fired
end function

function watches_the_timer()
    watch(timer.ticks)
        print "never"
    end watch
    return nothing
end function

function watches_unassigned()
    watch(later)
        print "never"
    end watch
    later = 1
    return nothing
end function

function refusal(which)
    on error goto next
    if which = "global" then
        watches_a_global()
        e = error
    else if which = "timer" then
        watches_the_timer()
        e = error
    else
        watches_unassigned()
        e = error
    end if
    if e then
        return string(e.code) + " " + e.message
    end if
    return "accepted"
end function

' a local server: its requests come from the loop, after main
function watches_a_server()
    server = webserver.listen(0)
    on error goto next
    watch(server.requests)
        print "never"
    end watch
    e = error
    webserver.close(server)
    if e then
        return string(e.code) + " " + e.message
    end if
    return "accepted"
end function

    check("fires on registration and each assignment, writing a local", tally([5, 7]), [0, 5, 12])
    check("a second call is its own watch", tally([1]), [0, 1])
    check("no watch is left once its functions return", count(watchers()), 0)
    check("a callee's x does not fire the caller's watch on x", caller_watching_x(), 2)
    check("each recursion level watches its own variable",
          depth(3, []), ["L3=3", "L2=2", "L1=1", "L1=10", "L2=20", "L3=30"])
    check("a raise is caught as usual", catches(), "planned")
    check("and the watch of the frame it unwound is gone", count(watchers()), 0)
    check("a named watch in a function, unwatched there", named_inside(), 2)
    h = returns_handle()
    check("a handle that outlived its frame names an ended watch", left(name_of(h), 39), "refused: watcher handle is not valid: i")
    unwatch(h)
    check("and unwatching it does nothing", count(watchers()), 0)
    check("a watch on a global from a function is refused", contains(refusal("global"),
          "1003 a watch in a function watches that function's own variables, and `glob` is not one"), true)
    check("a local copy of it is watched", watches_a_local_copy(), 1)
    check("a watch on an event source from a function is refused", contains(refusal("timer"),
          "and `timer` is not one"), true)
    check("and on a local server's requests", contains(watches_a_server(),
          "1003 a watch in a function cannot watch `server.requests`: its events arrive from the event loop"), true)
    check("a variable assigned only after the watch is refused", contains(refusal("later"),
          "and `later` is not one (assign it before the watch)"), true)
    ' the top level is unchanged
    top = 1
    top_fires = { n: 0 }
    watch(top)
        top_fires.n = top_fires.n + 1
    end watch
    top = 2
    check("a top-level watch fires as before", top_fires.n, 2)
    print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"

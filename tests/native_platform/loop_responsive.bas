' NAP-3 (WI-4) — THE load-bearing experiment for the whole Studio bet: a spawned
' actor does slow work while the GTK/GLib main loop stays responsive, and its result
' reaches the "UI" safely via an event source on the actor inbox fd. No freeze, no
' cross-thread call (the callback runs in the loop thread; the worker is a separate
' process). The worker sleeps 100ms in its own process; meanwhile the parent's 5ms
' timeout keeps firing (proving the loop is live, not blocked in receive()). When the
' result arrives, watch_mailbox delivers it and we confirm ticks happened during the
' wait. Deterministic: with a 100ms sleep vs a 5ms tick, ticks >= 1 always holds.
function worker(parent)
    sleep(0.1)
    send(parent, "result-42")
end function

function on_tick()
    s.ticks = s.ticks + 1
    return true
end function

function on_result(frame)
    if s.ticks >= 1 then
        print "responsive=true"
    else
        print "responsive=false"
    end if
    print "result=" + frame
    gi.quit()
    return false
end function

program main(args)
    load gi
    gi.require("GLib", "2.0")
    ' Handler state lives in a record set up here in program main (which runs in the
    ' global scope, so the top-level handlers see `s`); a spawned actor program needs
    ' the program-main entry, and top-level statements don't run when it is present.
    s = {}
    s.ticks = 0
    me = self()
    w = spawn worker(me)
    gi.timeout(5, on_tick)
    gi.watch_mailbox(on_result)
    gi.main()
end program

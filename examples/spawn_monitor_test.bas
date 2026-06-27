' Multiprocessing Phase 3a: death notification (docs/multiprocessing_design.md §7.1).
' monitor(handle) asks to be told when the actor behind `handle` dies. The death
' arrives as an ordinary tagged message ["down", handle, reason] in the monitor's
' mailbox, so receive("down") selects it. Because main spawns the worker it is the
' worker's OS parent, so the reason is accurate (from the exit status): a clean
' return is "normal", an unhandled error is "error".
'
' Single worker handled to completion at a time, so the transcript is deterministic.
function worker()
    consider receive()
    if "stop" then
        return                           ' clean exit  -> reason "normal"
    if "boom" then
        error("kaboom")                  ' crash       -> reason "error"
    end consider
end function

program main(args)
    ' Clean shutdown.
    w = spawn worker()
    m = monitor(w)
    send(w, "stop")
    d = receive("down")                  ' d is ["down", handle, reason]
    print("reason: " + d[2])

    ' Crash.
    w2 = spawn worker()
    monitor(w2)
    send(w2, "boom")
    d2 = receive("down")
    print("reason: " + d2[2])

    ' demonitor before death: no "down" is delivered, so the timed receive
    ' falls through to its timeout and returns nothing.
    w3 = spawn worker()
    m3 = monitor(w3)
    demonitor(m3)
    send(w3, "stop")
    leftover = receive("down", 1 seconds)
    print("after demonitor: " + is_nothing(leftover))
end program

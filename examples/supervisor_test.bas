' Multiprocessing Phase 3b: the canonical crash-and-restart supervisor
' (docs/multiprocessing_design.md §7.1). Supervision is NOT a runtime construct --
' it is an ordinary actor that spawns a worker, monitor()s it, and re-spawns it on
' a death message. The root program is itself an actor, so it plays the supervisor
' here. Because it spawns the worker it is the worker's OS parent, so the death
' reason is accurate ("error" for an unhandled crash).
'
' The worker crashes deterministically on "work", and the supervisor handles one
' worker to completion before spawning the next, so the transcript is fixed with no
' timing margins: three restarts, then it gives up.
function flaky()
    consider receive()
    if "work" then
        error("boom")          ' unhandled -> this actor exits nonzero -> reason "error"
    end consider
end function

program main(args)
    restarts = 0
    done = false
    w = spawn flaky()
    monitor(w)                 ' tell me when it dies
    send(w, "work")            ' provoke the crash
    while done = false
        msg = receive("down")  ' selective: handle only deaths here; msg is ["down", handle, reason]
        reason = msg[2]
        if restarts < 3 then
            restarts = restarts + 1
            print("restart " + string(restarts) + " (" + reason + ")")
            w = spawn flaky()  ' restart == re-spawn the same entry
            monitor(w)
            send(w, "work")
        else
            print("giving up after " + string(restarts) + " restarts")
            done = true
        end if
    end while
end program

' FINDING 36: a bare `receive()` in a watcher body, with the mailbox DRAINED.
'
' A watcher body runs once when `watch` registers it -- during `main`, before
' the event loop exists -- so a blocking receive there waits for a loop that
' has not started, the `unwatch` below is unreachable, and the program stops
' with NO DIAGNOSTIC AT ALL (measured: exit 124 under `timeout`).
'
' The first receive is the ORDINARY case and must stay silent: the child's
' reply is already in the mailbox, so it returns at once. Only the second one,
' past a drained mailbox, is the hazard -- which is why the warning is issued
' where the runtime knows it is about to block rather than at the call.
function kid(parent)
    send(parent, { n: 1 })
    return 0
end function

program main(args)
    me = self()
    k = spawn kid(me)
    watch reader(inbox.messages)
        r = receive()
        print("first: " + string(r))
        print("now a bare receive, mailbox empty")
        r2 = receive()
        print("second: " + string(r2))
        unwatch reader
    end watch
    print("main returning")
end program

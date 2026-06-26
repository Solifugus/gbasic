' Multiprocessing Phase 2: receive timeout (docs/multiprocessing_design.md §4).
' A duration argument to receive is a deadline: receive(d) returns nothing if no
' message arrives within d. This is deterministic by outcome -- an already-queued
' message returns immediately, and a mailbox no one will send to always times out.
program main(args)
    me = self()

    ' A queued message returns at once; the timeout is never reached.
    send(me, "ready")
    print(receive(5 seconds))

    ' Nothing will ever arrive, so this times out and yields nothing.
    answer = receive(1 seconds)
    consider answer
    if nothing then
        print("no reply within the deadline")
    else
        print("unexpected: " + answer)
    end consider
end program

' `watch(inbox.messages)` -- an actor reply delivered by the event loop.
'
' `receive()` BLOCKS, which is right for a sequential program and fatal in a
' handler: the watcher IS the event loop, so a pool whose replies are collected
' with `receive` has moved the tool body off the loop and then made the loop
' wait for it. The inbox therefore joins the loop's own `poll` set, the way an
' http transfer does.
'
' It is SMALLER than the http case because an interpreter has exactly ONE
' inbox: one descriptor, and one readable event on a SOCK_SEQPACKET socket is
' exactly one whole frame -- not an assumption, but what the GI bridge's
' mailbox source has relied on since it was written.
'
' A MAILBOX HAS NO COMPLETION, unlike a transfer, so the program says when it
' is finished: `unwatch` ends the delivery and the loop exits. Without that a
' program would run forever waiting for a message that may never come -- which
' is what the first version did, and a hang is not a failure.
'
' Self-checking: every defect here is a MISSING message or a message delivered
' twice, and both look like ordinary output.

function worker(back, n)
    send(back, { n: n, doubled: n * 2 })
    return nothing
end function

expected = 5
seen = []
sum = 0

watch reader(inbox.messages)
    while count(inbox.messages) > 0
        m = take_first(inbox.messages)
        append(seen, m.n)
        sum = sum + m.doubled
    end while
    if count(seen) >= expected then
        unwatch reader
    end if
end watch

me = self()
for i = 1 to expected
    w = spawn worker(me, i)
end for
print "main returns with " + string(expected) + " replies outstanding"

' Runs after the loop drains, because the program does not end until the loop
' does -- which is itself the first thing being asserted.
watch(seen)
    if count(seen) >= expected then
        print "delivered: " + string(count(seen))
        print "every reply arrived exactly once: " + string(count(unique(seen)) = expected)
        print "and carried the worker's own answer: " + string(sum = 30)
    end if
end watch

' NAP-3 (WI-4): the actor -> GLib-loop seam. A spawned worker (a separate process)
' sends one message to its parent; gi.watch_mailbox wires the parent's actor inbox
' fd into the main loop, so when the frame arrives the loop dispatches on_msg(frame)
' with the deserialized value — no blocking receive(), no thread. The mailbox is a
' SOCK_SEQPACKET pair, so a readable fd means exactly one whole frame is ready.
' (`load gi` lives inside `program main`: a top-level load does not carry into the
' program block's scope, but once loaded there gi is visible to the handlers too.)
function worker(parent)
    send(parent, "from worker")
end function

function on_msg(frame)
    print "got: " + frame
    gi.quit()
    return false
end function

program main(args)
    load gi
    gi.require("GLib", "2.0")
    me = self()
    w = spawn worker(me)
    gi.watch_mailbox(on_msg)
    gi.main()
    print "mailbox done"
end program

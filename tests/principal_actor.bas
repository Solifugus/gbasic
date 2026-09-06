' A principal DOES NOT CROSS `spawn`, and the control says why that is a
' feature rather than a gap.
'
' An actor is fork+exec, so a child begins with an empty stack -- the isolation
' falls out of the process model rather than being enforced. That is the right
' default: an identity that travelled implicitly would be an identity nobody
' wrote down, and a pool worker holding its own connections would inherit
' whoever happened to spawn it.
'
' THE CONTROL IS THE OTHER HALF. A worker that SHOULD act for someone re-enters
' the scope from the message it received, and that is the designed handoff.
' Without it, "the actor sees nothing" is equally satisfied by a `principal()`
' that never works anywhere.

function worker()
    ' What the child sees with nothing handed to it.
    reply_to = receive()
    send(reply_to, "inherited:" + string(principal() = nothing))

    ' And what it sees once the caller hands its identity over explicitly.
    who = receive()
    with principal(who)
        send(reply_to, "explicit:" + principal().user)
    end with
    send(reply_to, "after:" + string(principal() = nothing))
    return nothing
end function

program main(args)
    me = self()
    with principal({ user: "gwen" })
        w = spawn worker()
        send(w, me)
        print receive()
        send(w, principal())
        print receive()
        print receive()
        print "parent still: " + principal().user
    end with
end program

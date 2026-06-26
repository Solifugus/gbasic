' Multiprocessing Phase 2: selective receive (docs/multiprocessing_design.md §9.2).
' receive(tag) returns the next message whose tag matches -- the message itself if
' a string, or its first element if an array -- leaving non-matching messages
' queued in arrival order for later receives. A plain receive() then drains them
' oldest-first, so FIFO order is preserved across the two forms.
'
' Single sender (main), so arrival order is fixed and the result is deterministic.
function worker(parent)
    ' "go" is sent last; this must retain the earlier messages, then match it.
    send(parent, "selected: " + receive("go"))
    ' the retained messages now drain in their original order
    send(parent, "drain: " + receive())
    a = receive()
    send(parent, "drain: " + a[0] + "/" + a[1])
end function

program main(args)
    me = self()
    w = spawn worker(me)
    send(w, "wait")
    send(w, ["data", "x"])
    send(w, "go")
    print(receive())
    print(receive())
    print(receive())
end program

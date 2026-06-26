' Multiprocessing Phase 2: runtime handle passing
' (docs/multiprocessing_design.md §4.1). An actor handle can be sent inside a
' message to an already-running actor, giving it a channel to a third actor --
' the fd travels as SCM_RIGHTS ancillary data, not as serialized data.
'
' The pipeline forwards a handle twice and has exactly one possible sender at
' each receive, so it is deterministic: main hands the worker's handle to the
' hub; the hub forwards main's handle to the worker; the worker replies to main.
function hub(parent)
    target = receive()
    send(target, parent)
end function

function worker()
    back = receive()
    send(back, "worker reached via a forwarded handle")
end function

program main(args)
    me = self()
    w = spawn worker()
    h = spawn hub(me)
    send(h, w)
    print(receive())
end program

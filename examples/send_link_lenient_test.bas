' Multiprocessing Phase 3c: sending a record with a live PBI `link` field
' (docs/multiprocessing_design.md §6). By default send() is total: the boundary
' copies everything, so a `link` silently degrades to an independent copy -- the
' value crosses intact, only its shared write-through identity does not. (Strict
' mode, send(handle, message, true), instead diagnoses the live link; see
' tests/negative_send_strict_link.bas.)
function worker(boss)
    m = receive()
    send(boss, "got: " + m.box)   ' the value survived the boundary as a plain copy
end function

program main(args)
    me = self()
    rec = { box (link): "shared" }
    w = spawn worker(me)
    send(w, rec)                   ' lenient (default): link degrades to a copy, no error
    print(receive())
end program

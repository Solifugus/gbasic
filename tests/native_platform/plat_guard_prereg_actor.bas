' PLAT-GUARD: the ACTOR half of the pre-registration tripwire.
'
' A spawned actor is fork+exec: the child re-parses the source and runs the named
' function directly, never entering the `program` block. So a library the worker
' needs has to be registered by the same position-blind pass the parent runs, and
' BOTH POSITIONS ARE EXERCISED HERE ON PURPOSE -- one `load` at the top level and
' one inside `program` -- because they used to fail in opposite processes:
'
'                  top-level `load`      `load` inside `program`
'   parent         never ran (warned)    ran, as a statement
'   child          ran                   never seen
'
' Following the parent's warning ("move it inside `program`") therefore broke the
' child, and the symptom was a HANG: the worker died on its first qualified call
' while the parent sat in receive() forever. A test using only one position would
' pass on the broken build.
'
' THE TWO LIBRARIES ARE INDEPENDENT ON PURPOSE. The first version of this fixture
' used `alias_host` and `alias_dep`, and `alias_host` LOADS `alias_dep` -- so the
' child got the block-position library transitively, removing the block hoist
' changed nothing, and the tier passed against a deliberately broken build. Each
' position now has a library nothing else reaches.
load prereg_top from "../libs/prereg_top.bas"

program main(args)
    load prereg_block from "../libs/prereg_block.bas"

    me = self()
    w = spawn worker()
    send(w, me)
    print("child=" + receive())

    ' The parent must reach both of them too.
    print("parent-top=" + prereg_top.tag())
    print("parent-block=" + prereg_block.tag())
end program

function worker()
    back = receive()
    send(back, prereg_top.tag() + "/" + prereg_block.tag())
end function

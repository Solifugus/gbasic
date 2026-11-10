' process.self().pid is this process's id, and a spawned actor -- a process of
' its own -- answers its own. The runner execs this file from a shell that
' prints $$ first, so the kernel's answer is the oracle, not ours.
function child()
    m = receive()
    send(m.from, process.self().pid)
end function

program main()
    me = process.self().pid
    print "pid " + string(me)
    a = spawn child()
    send(a, { from: self() })
    c = receive()
    if c = me then
        print "actor SAME"
    else
        print "actor differs"
    end if
end program

' process.self().pid is this process's id, and a spawned actor -- a process of
' its own -- answers its own. The runner execs this file from a shell that
' prints $$ first, so the kernel's answer is the oracle, not ours.
'
' An optional gate file: the program stays alive until it exists, so a runner
' under MSYS2 can read the Windows pid of the exec-ed process from outside
' (/proc/PID/winpid), $$ there being MSYS's own number.
function child()
    m = receive()
    send(m.from, process.self().pid)
end function

program main(args)
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
    if count(args) > 0 then
        gate {file}= args[0]
        while not exists(gate)
            sleep(0.02)
        end while
    end if
end program

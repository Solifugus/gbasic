' Multiprocessing Phase 1c: a long-lived worker that dispatches messages with
' consider/receive and ends on "stop" (docs/multiprocessing_design.md §2). The
' consider branches align with the consider keyword's column, as gBASIC requires.
function worker(name, parent)
    while true
    consider receive()
    if "ping" then
        send(parent, "pong from " + name)
    if "stop" then
        return
    end consider
    end while
end function

program main(args)
    me = self()
    a = spawn worker("a", me)
    send(a, "ping")
    print(receive())
    send(a, "ping")
    print(receive())
    send(a, "stop")
end program

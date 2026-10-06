' CONTROL for finding 36: a bare `receive()` in a watcher body that GETS a
' message must be silent. This is the shape the books session's own chapter 14
' uses and it works; a warning at the CALL rather than at the block would fire
' on it and be wrong.
function kid(parent)
    send(parent, { n: 1 })
    return 0
end function

program main(args)
    me = self()
    k = spawn kid(me)
    watch reader(inbox.messages)
        r = receive()
        print("got " + string(r))
        unwatch reader
    end watch
    print("main returning")
end program

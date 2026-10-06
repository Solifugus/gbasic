' THE CONTROL THAT MAKES THE PREDICATE LOAD-BEARING: a child that is SLOW but
' alive. The watcher body blocks at registration for longer than the
' diagnostic's 250ms slice, so a rule keyed on "it blocked" fires here -- on a
' program with nothing wrong with it, waiting on a peer that is about to
' answer. Only "nothing remains to send" tells the two apart.
'
' Measured: without the predicate this warns; with it, silent.
function kid(parent)
    sleep(0.8)
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

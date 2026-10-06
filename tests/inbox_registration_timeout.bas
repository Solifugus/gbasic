' CONTROL for finding 36: THE REMEDY MUST BE SILENT. The same shape with a
' timeout is exactly what the warning tells the author to write, so warning
' here would make the advice unfollowable.
function kid(parent)
    send(parent, { n: 1 })
    return 0
end function

program main(args)
    me = self()
    k = spawn kid(me)
    watch reader(inbox.messages)
        r = receive()
        print("first: " + string(r))
        r2 = receive(1 second)
        print("second: " + string(r2))
        unwatch reader
    end watch
    print("main returning")
end program

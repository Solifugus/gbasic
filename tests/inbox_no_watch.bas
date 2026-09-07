' THE CONTROL for inbox_test.bas: actors, replies in flight, and NO watcher.
' The loop must not be entered, so the program exits at once having delivered
' nothing -- which is what says the WATCH is what admits the mailbox to the
' loop, rather than the loop running for any program that ever spawned.
function worker(back)
    sleep(2)
    send(back, { late: true })
    return nothing
end function

me = self()
w = spawn worker(me)
print "spawned, no watcher, returning"

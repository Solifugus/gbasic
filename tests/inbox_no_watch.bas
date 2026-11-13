' THE CONTROL for inbox_test.bas: actors, replies in flight, and NO watcher.
' The loop must not be entered, so the program exits at once having delivered
' nothing -- which is what says the WATCH is what admits the mailbox to the
' loop, rather than the loop running for any program that ever spawned.
function worker(back, late)
    sleep(late)
    send(back, { late: true })
    return nothing
end function

me = self()
late = env("INBOX_LATE_S")   ' run_inbox.sh's; 2 when run by hand
if late = "" then
    late = "2"
end if
w = spawn worker(me, number(late))
print "spawned, no watcher, returning"

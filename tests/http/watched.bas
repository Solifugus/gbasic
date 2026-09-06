' WAITED OR WATCHED, NEVER BOTH -- asserted as a DIFFERENCE inside ONE program.
'
' A handle the program blocked on must NOT also be reported by the event loop,
' and a handle it merely started MUST be. Asserting only the second half passes
' on a module with no claiming rule at all; asserting only the first passes on
' one that delivers nothing. Both together do not.
'
' The program also has NO SERVER BOUND. Reaching the watcher at all is the
' other half of what this file tests: the loop now runs after `main` while
' there is anything to deliver, where a program holding only handles used to
' exit immediately.
load http

watch(http.events)
    while count(http.events) > 0
        e = take_first(http.events)
        if e.kind = "done" then
            print "delivered " + string(e.id) + " " + string(e.status)
        end if
    end while
end watch

port = env("HTTP_FIXTURE_PORT")

waited = http.start({ url: "http://127.0.0.1:" + port + "/" })
ws = http.wait(waited)
print "waited id " + string(waited.id) + " status " + string(ws.status)

first = http.start({ url: "http://127.0.0.1:" + port + "/" })
second = http.start({ url: "http://127.0.0.1:" + port + "/boom" })
print "watched ids " + string(first.id) + " " + string(second.id)
print "main returns"

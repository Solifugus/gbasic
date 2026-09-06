' `http.wait` inside a watcher blocks the event loop -- no other request, no
' other stream, no other client makes progress until it returns. That is the
' same mistake as looping after `serve`, and it WARNS in the same words rather
' than being refused: a short wait inside a handler is a defensible thing to
' write, and the author is the one who can price it.
'
' The program must still WORK. A warning that changed the answer would be a
' refusal wearing the wrong label.
load http

watch(http.events)
    while count(http.events) > 0
        e = take_first(http.events)
        if e.kind = "done" then
            s = http.wait(e.handle)
            print "waited inside the watcher: " + string(s.status)
        end if
    end while
end watch

h = http.start({ url: "http://127.0.0.1:" + env("HTTP_FIXTURE_PORT") + "/" })

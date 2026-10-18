' DIRECTION A IN THE PRODUCT: Qt owns the loop and gBASIC'S OWN SOURCES ARE
' STILL SERVICED. docs/qt_gui_design.md §5.1 measured both arrangements in
' isolation and ruled one out; this is the same claim made by the interpreter
' rather than by a probe, which is the only place it can be wrong for a reason
' the probe could not see.
'
' The webserver is the source because it is the one with a client that can
' ASK. While `qt.run()` holds the thread, a request must still be answered --
' and under the other arrangement it would not be, for as long as any nested
' Qt loop was up.
load webserver

window app( title: "App" )
    column
        button go( text: "Go" )
    end column
end window

server = webserver.listen(number(env("PORT")))
print "listening on " + string(server.port)

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        if req.path = "/quit" then
            append(server.responses, { id: req.id, body: "bye" })
            webserver.close(server)
            qt.stop()
        else
            append(server.responses, { id: req.id, body: "served while Qt owned the loop" })
        end if
    end while
end watch

ok = qt.open(app)
print "window open"
rc = qt.run()
print "loop ended"

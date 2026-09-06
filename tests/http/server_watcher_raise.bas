' The same silence one queue over, which is where it came from: this defect was
' PRE-EXISTING in `watch(server.requests)` and was found building the second
' caller of the watcher path. Both are asserted because the fix is shared, and
' a fix proven on only the new caller would say nothing about the old one.
load webserver

server = webserver.listen(number(env("HTTP_SERVER_PORT")))
watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        error { message: "the request watcher raised", code: 42 }
    end while
end watch

' `receive()` inside a watcher blocks the event loop, and warns for the same
' reason and in the same words as `http.wait`. The program must still WORK: a
' warning that changed the answer would be a refusal wearing the wrong label.
load webserver

function worker(back)
    send(back, "worker replied")
    return nothing
end function

server = webserver.listen(number(env("PORT")))
me = self()

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        w = spawn worker(me)
        answer = receive()
        append(server.responses, { id: req.id, body: answer })
    end while
end watch

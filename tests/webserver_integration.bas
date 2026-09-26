load webserver

port_file{file}= "tests/tmp_webserver_port.txt"
if exists(port_file) then delete(port_file)

server = webserver.listen(0)
write(port_file, string(server.port))

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)

        if req.path = "/inspect" then
            body = req.method + "|" + req.path + "|" + req.query.name + "|" + req.query.empty + "|" + req.headers["x-test"] + "|" + req.remote_ip + "|" + string(req.remote_port > 0) + "|" + string(is_string(req.timestamp))
            append(server.responses, {
                id:req.id,
                body:body
            })
            continue
        end if

        if req.path = "/redirect-default" then
            append(server.responses, webserver.redirect(req, "/redirected"))
            continue
        end if

        if req.path = "/redirect-permanent" then
            append(server.responses, webserver.redirect(req, "/moved", 308))
            continue
        end if

        if req.path = "/cookies" then
            append(server.responses, {
                id:req.id,
                body:req.cookies.session + "|" + req.cookies.theme + "|" + string(is_unknown(req.cookies["missing"]))
            })
            continue
        end if

        if req.path = "/set-cookies" then
            append(server.responses, {
                id:req.id,
                cookies:[
                    "session=abc123; HttpOnly; SameSite=Lax; Path=/",
                    "theme=light; Max-Age=3600; Path=/"
                ],
                body:"cookies set"
            })
            continue
        end if

        if req.path = "/json" then
            if is_unknown(req["json"]) then
                append(server.responses, {
                    id:req.id,
                    status:400,
                    body:"missing json"
                })
            else
                headers = {}
                headers["content-type"] = "application/json"
                append(server.responses, {
                    id:req.id,
                    status:201,
                    headers:headers,
                    body:encode({
                        name:req.json.name,
                        active:req.json.active
                    })
                })
            end if
            continue
        end if

        if req.path = "/invalid-json" then
            append(server.responses, {
                id:req.id,
                body:string(is_unknown(req["json"])) + "|" + req.body
            })
            continue
        end if

        if req.path = "/defaults" then
            append(server.responses, {id:req.id})
            continue
        end if

        if req.path = "/timeout" then
            ignored = true
            continue
        end if

        ' --- a body is BYTES, in both directions (PLAT-NUL, 2026-09-25) ---
        '
        ' OUT: a handler returning "a" + chr(0) + "b" used to send
        ' `Content-Length: 1` and one byte -- the count and the bytes agreeing
        ' with each other, so nothing downstream could notice the response had
        ' been shortened.
        if req.path = "/nul-out" then
            append(server.responses, {
                id:req.id,
                body:"a" + chr(0) + "b"
            })
            continue
        end if

        ' IN: a request body carrying the byte was refused with 415 Unsupported
        ' Media Type -- a refusal naming the one thing that is NOT the problem,
        ' since changing the media type is the only remedy a 415 suggests and it
        ' cannot help. Reported as hex, so the golden holds no raw NUL.
        if req.path = "/nul-in" then
            append(server.responses, {
                id:req.id,
                body:"len=" + string(len(req.body)) + " hex=" + hex_encode(req.body)
            })
            continue
        end if

        if req.path = "/shutdown" then
            append(server.responses, {
                id:req.id,
                body:"bye"
            })
            webserver.close(server)
            continue
        end if

        append(server.responses, {
            id:req.id,
            status:404,
            body:"not found"
        })
    end while
end watch

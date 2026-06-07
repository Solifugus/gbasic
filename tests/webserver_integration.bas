load webserver

port_file(file)= "tests/tmp_webserver_port.txt"
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
        else
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
            else
                if req.path = "/invalid-json" then
                    append(server.responses, {
                        id:req.id,
                        body:string(is_unknown(req["json"])) + "|" + req.body
                    })
                else
                    if req.path = "/defaults" then
                        append(server.responses, {id:req.id})
                    else
                        if req.path = "/timeout" then
                            ignored = true
                        else
                            if req.path = "/shutdown" then
                                append(server.responses, {
                                    id:req.id,
                                    body:"bye"
                                })
                                webserver.close(server)
                            else
                                append(server.responses, {
                                    id:req.id,
                                    status:404,
                                    body:"not found"
                                })
                            end if
                        end if
                    end if
                end if
            end if
        end if
    end while
end watch

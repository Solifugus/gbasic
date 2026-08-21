' PLAT-WEB-1 Gap A -- the bind-address fixture.
'
' The runner sets GBASIC_WEB_BIND to the address to request, or leaves it
' unset to exercise the DEFAULT. Either way this reports the address the
' KERNEL says the socket is bound to (server.address is read back from
' getsockname, like server.port already is), never the one we asked for --
' otherwise the test would only prove we can echo our own argument back.
'
' The body carries req.remote_ip so the dual-stack tier can see how a v4
' peer is reported on a v6 listener.

load webserver

out_file(file)= "tests/tmp_web_bind.txt"
if exists(out_file) then delete(out_file)

requested = env("GBASIC_WEB_BIND")
if is_unknown(requested) then
    server = webserver.listen(0)
else
    server = webserver.listen(0, { address: requested })
end if

write(out_file, string(server.port) + " " + server.address)

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)

        if req.path = "/quit" then
            append(server.responses, {
                id:req.id,
                body:"bye"
            })
            webserver.close(server)
            continue
        end if

        append(server.responses, {
            id:req.id,
            body:"ok " + req.remote_ip
        })
    end while
end watch

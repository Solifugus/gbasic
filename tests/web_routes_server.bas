' PLAT-WEB-1 integration fixture: the route table in front of a real socket.
'
' The other two fixtures prove the routing rules with no server anywhere. This
' one proves the join: that what `web.dispatch` returns is a response record
' the webserver accepts VERBATIM -- status, headers and body reaching a real
' client over TCP -- and that captures survive a path that arrived off the
' wire rather than out of a literal.

load web
load webserver

function home(req)
    return { body: "home" }
end function

function product(req)
    return {
        status: 201,
        headers: { "content-type": "text/plain", "x-route": "product" },
        body: "product " + req.params.id
    }
end function

function bye(req)
    return { body: "bye" }
end function

routes = web.routes([
    { method: "get",  path: "/",              handler: home },
    { method: "get",  path: "/products/{id}", handler: product },
    { method: "post", path: "/products",      handler: home },
    { method: "get",  path: "/quit",          handler: bye }
])

port_file(file)= "tests/tmp_web_routes_port.txt"
if exists(port_file) then delete(port_file)

server = webserver.listen(0)
write(port_file, string(server.port))

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        append(server.responses, web.dispatch(routes, req))
        if req.path = "/quit" then
            webserver.close(server)
        end if
    end while
end watch

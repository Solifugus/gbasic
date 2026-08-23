' §9: the block with no socket anywhere in sight. web.routes(edge) is the
' route table as data; web.dispatch(edge, req) runs one request through the
' whole pipeline -- host dispatch, routes, static fallback -- and a stream
' route answers with its opening head. source_outline shows the block, its
' sites and its routes as structure.
server edge( port: 8080 )

    root "tests/web_server_block/pub"

    get "/"( req )
        return { body: "home" }
    end get

    stream "/events"( req )
        return 0
    end stream

    web api( host: "api.example" )
        post "/orders"( req )
            return { status: 201, body: "ordered" }
        end post
    end web

end server

program main( args )
    print "-- routes as data"
    for each r in web.routes(edge)
        line = r.method + " " + r.path
        if r.stream then
            line = "stream " + r.path
        end if
        if r.host != "" then
            line = line + " @ " + r.host
        end if
        print line
    end for

    print ""
    print "-- dispatch with no socket"
    res = web.dispatch(edge, { id: 7, method: "GET", path: "/", headers: {} })
    print "GET / -> " + string(res.status) + " <" + res.body + "> id=" + string(res.id)
    res = web.dispatch(edge, { id: 8, method: "POST", path: "/orders", headers: { host: "api.example" } })
    print "POST /orders @ api -> " + string(res.status)
    res = web.dispatch(edge, { id: 9, method: "POST", path: "/orders", headers: {} })
    print "POST /orders @ default -> " + string(res.status)
    res = web.dispatch(edge, { id: 10, method: "GET", path: "/events", headers: {} })
    print "GET /events -> stream=" + string(res.stream)
    res = web.dispatch(edge, { id: 11, method: "GET", path: "/page.txt", headers: {} })
    print "GET /page.txt -> " + string(res.status) + " file=" + string(has(res, "file"))
    res = web.dispatch(edge, { id: 12, method: "POST", path: "/", headers: {} })
    print "POST / -> " + string(res.status) + " allow=" + res.headers["allow"]

    print ""
    print "-- the outline sees the block"
    f(file) = "tests/web_server_block/block_offline.bas"
    src = read(f)
    o = source_outline(src)
    for each n in o.nodes
        interesting = n.kind = "server" or n.kind = "handler" or n.kind = "web" or n.kind = "hook" or n.kind = "directive"
        if interesting then
            print n.kind + " " + string(n.name)
        end if
    end for
end program

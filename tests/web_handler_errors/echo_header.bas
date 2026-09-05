' The adversarial case, and the reason the matrix beside it matters.
'
' Echoing a query parameter into a response header is an ordinary thing for a
' handler to do. `webserver_validate_response_value` refuses a CRLF in a
' header value -- correctly, because that is response splitting -- but until
' that refusal was made reportable it RAISED, and a raise from the event loop
' ended the listener. So on the default `workers: 1` this handler was a remote
' kill switch: one request with %0d%0a in it and the server was gone.
'
' The refusal is the security control. Surviving the refusal is what keeps it
' from being a denial of service in its own right.
server echoer( port: 0 )

    get "/ok"( req )
        return { body: "fine" }
    end get

    get "/echo"( req )
        v = ""
        if has(req.query, "v") then
            v = req.query.v
        end if
        h = {}
        h["x-echo"] = v
        return { headers: h, body: "echoed" }
    end get

end server

program main( args )
    h = web.serve(echoer)
    print "PORT " + string(h.port)
end program

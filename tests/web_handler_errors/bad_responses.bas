' Every way a handler's RETURNED RECORD can be malformed, one route each.
'
' These are not language errors -- the program is well-formed and the handler
' ran to completion. They are the response-shape checks in
' `webserver_validate_response_value`, reached through the WEB-5 block path.
'
' web.bas already refuses a non-record return, names the route on stderr and
' keeps serving ("a raise here would take the whole listener down over one bad
' handler"). The record-shape checks live in C and were written for the
' `append(server.responses, ...)` path, where raising IS right: the program
' made the mistake in its own frame, at a line that can be pointed at. Reached
' from a handler there is no such frame, and the same raise ends a listener
' that the reference manual promises will keep serving.
'
' So each route below is followed by /ok in the driver. The interesting
' assertion is not the 500 -- it is that the NEXT request is answered.
server bad( port: 0 )

    get "/ok"( req )
        return { body: "fine" }
    end get

    ' -- the body ----------------------------------------------------------
    get "/body-type"( req )
        return { body: 42 }
    end get

    ' -- the status --------------------------------------------------------
    get "/status-type"( req )
        return { status: "ok", body: "x" }
    end get

    get "/status-range"( req )
        return { status: 99, body: "x" }
    end get

    ' -- the id (stamped by web.dispatch only when the handler omits it) ----
    get "/id-type"( req )
        return { id: "abc", body: "x" }
    end get

    get "/id-unknown"( req )
        return { id: 999999, body: "x" }
    end get

    ' -- stream / file exclusivity ------------------------------------------
    get "/stream-type"( req )
        return { stream: "yes", body: "x" }
    end get

    get "/file-type"( req )
        return { file: 42 }
    end get

    get "/body-and-file"( req )
        return { body: "x", file: "tests/web_handler_errors/pub/page.txt" }
    end get

    get "/stream-body"( req )
        return { stream: true, body: "x" }
    end get

    ' -- headers ------------------------------------------------------------
    get "/headers-type"( req )
        return { headers: "x", body: "y" }
    end get

    get "/header-name"( req )
        h = {}
        h["bad name"] = "v"
        return { headers: h, body: "y" }
    end get

    get "/header-value-type"( req )
        h = {}
        h["x-a"] = 42
        return { headers: h, body: "y" }
    end get

    ' Response splitting: a CRLF in a header value is what turns one response
    ' into two on the wire. Refusing it is right; dying over it means anything
    ' a handler echoes into a header is a remote kill switch.
    get "/header-crlf"( req )
        h = {}
        h["x-a"] = "v" + chr(13) + chr(10) + "evil: 1"
        return { headers: h, body: "y" }
    end get

    ' -- cookies -------------------------------------------------------------
    get "/cookies-type"( req )
        return { cookies: "x", body: "y" }
    end get

    get "/cookie-type"( req )
        return { cookies: [42], body: "y" }
    end get

    get "/cookie-crlf"( req )
        return { cookies: ["a=b" + chr(13) + chr(10) + "evil=1"], body: "y" }
    end get

    ' -- the control: already non-fatal, in web.bas rather than in C ---------
    get "/not-a-record"( req )
        return 7
    end get

end server

program main( args )
    h = serve(bad)
    print "PORT " + string(h.port)
end program

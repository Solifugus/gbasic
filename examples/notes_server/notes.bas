' A notes service in one declaration: a static root, three routes, a live
' event stream, and a second site on another host.
'
' Run it from THIS directory (root "public" is relative to the process cwd):
'
'   cd examples/notes_server
'   GBASIC_PATH=/usr/local/share/gbasic/stdlib gbasic --line-buffered notes.bas
'
' It prints the OS-assigned port. Then, against that port:
'
'   curl -X POST -d 'buy milk' localhost:PORT/notes   -> 201 "note 1"
'   curl localhost:PORT/notes                         -> the list
'   curl localhost:PORT/notes/1                       -> one note, 404 past the end
'   curl -N localhost:PORT/feed                       -> SSE; each POST pushes to it
'   curl -H 'Host: admin.localhost' localhost:PORT/   -> the second site
'   curl localhost:PORT/index.html                    -> from public/, via root
'   curl -X DELETE localhost:PORT/notes               -> 405, Allow: GET, POST
'
' `/` is a 404: web.static answers a directory with 404 rather than a listing
' or an implicit index, deliberately -- publishing an index is the caller's
' decision to make out loud.
'
' Handlers are functions and cannot rebind caller state, so the shared list
' lives in FIELDS of the program global G -- the one mutation a function can
' make visible.
server notes( port: 0 )

    root "public"

    get "/notes"( req )
        out = ""
        for each n in G.notes
            out = out + n + "\n"
        end for
        return { body: out }
    end get

    post "/notes"( req )
        append(G.notes, req.body)
        for each s in G.feeds
            ok = web.emit(s, web.sse_named("note", req.body))
        end for
        return { status: 201, body: "note " + string(count(G.notes)) }
    end post

    get "/notes/{id}"( req )
        i = number(req.params.id)
        if i < 1 or i > count(G.notes) then
            return { status: 404, body: "no such note" }
        end if
        return { body: G.notes[i - 1] }
    end get

    stream "/feed"( req )
        e = web.emit(req, web.sse_event("watching"))
        append(G.feeds, req)
        return 0
    end stream

    web admin( host: "admin.localhost" )
        get "/"( req )
            return { body: "admin site: " + string(count(G.notes)) + " notes" }
        end get
    end web

end server

program main( args )
    G = { notes: [], feeds: [] }
    h = web.serve(notes)
    print "PORT " + string(h.port)
end program

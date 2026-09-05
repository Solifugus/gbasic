' notesd -- the smallest honest gBASIC service.
'
' This exists to prove the SHIPPING PATH, not to be useful: source -> lean
' interpreter -> .deb -> systemd -> a loopback service answering requests. If
' this cannot be installed and started on a clean machine, no larger
' application can either, and you would be debugging the packaging and the
' product at the same time.
'
' It is deliberately the shape a real internal tool has:
'   * binds LOOPBACK only, because network exposure should be opt-in
'   * reads configuration from a file the package owns
'   * keeps state in /var/lib, which is the only place it may write
'   * answers a health endpoint, which is what systemd and a load balancer ask
'
' EVERY STDLIB LIBRARY IS LOADED BY ABSOLUTE PATH, written as @RUNTIME@ and
' substituted by packaging/build-deb.sh at package time. That is not a style
' choice, and the token is what keeps it from also being unrelocatable. A bare
' `load NAME` searches the SOURCE FILE'S OWN DIRECTORY TREE first, recursively,
' ahead of both GBASIC_PATH and the compiled-in stdlib -- so any file named
' `NAME.bas` anywhere beneath the application directory silently replaces the
' shipped library, and the warning you get says the REAL one was "ignored".
' See docs/shipping_applications.md, "How a library is found".

program main( args )
    ' `sqlite` is a NATIVE MODULE compiled into the interpreter, not a stdlib
    ' .bas file -- so it takes a bare `load` with no path, and none of the
    ' search-path hazard below applies to it. The same is true of `pg`,
    ' `webclient`, `webserver`, `xml`, `gui` and `gi`. Only stdlib LIBRARIES
    ' (stats, web, chart, dates, crypto, ...) are files, and only those need
    ' the @RUNTIME@ form.
    load sqlite

    ' Where the configuration lives, in order: argv, then NOTESD_CONF, then the
    ' packaged default. A service whose config path is hardcoded cannot be
    ' smoke-tested before install and cannot run twice on one host, and both of
    ' those are things an operator legitimately wants.
    conf = _read_conf(_conf_path(args))
    port = number(_conf_get(conf, "port", "8099"))
    store = _conf_get(conf, "database", "/var/lib/notesd/notes.db")

    db = sqlite.connect(store)
    sqlite.exec(db, "create table if not exists notes (id integer primary key autoincrement, body text not null, created text not null)", [])

    ' A `server` block's head takes LITERALS only -- that restriction is what
    ' makes every load-time check decidable -- so the configured port cannot be
    ' written there. It CAN be applied here: the block binds a plain record and
    ' `serve` reads `options`, so overriding one before serving is enough, and
    ' the declarative block is kept. Without this the service prints the
    ' configured port and listens on the declared one, which is worse than
    ' either, and is what tests/run_packaging.sh caught.
    notesd.options.port = port
    ' Bound, not bare. A bare call discards a non-nothing return, so the
    ' unused-result warning would fire on every service start and land in the
    ' journal -- and `running.port` is how a caller learns the port when the
    ' block binds `port: 0`.
    running = web.serve(notesd)
    ' Reported from the LIVE server, never from the configuration: with
    ' `port: 0` the kernel chooses, and an operator needs the real number.
    print to error "notesd: listening on 127.0.0.1:" + string(running.port) + ", store " + store
end program

' Configuration is `key = value`, one per line, `#` comments. Deliberately not
' JSON: an operator edits this file by hand at 3am, and a missing brace should
' not be able to stop the service starting.
function _read_conf( path )
    out = {}
    f{file} = path
    if file_type(path) != "file" then
        return out
    end if
    for each line in split(read(f), "\n")
        t = trim(line)
        if t = "" or starts_with(t, "#") then
            continue
        end if
        eq = find(t, "=")
        if is_number(eq) then
            out[trim(left(t, eq))] = trim(mid(t, eq + 1, len(t)))
        end if
    next line
    return out
end function

function _conf_get( conf, key, fallback )
    if has(conf, key) then
        return conf[key]
    end if
    return fallback
end function

server notesd( port: 8099, address: "127.0.0.1" )

    ' What systemd, a reverse proxy and a monitoring probe all ask for. Cheap,
    ' and it must not touch the database -- a health check that depends on the
    ' store reports "unhealthy" for a full disk as loudly as for a crash.
    get "/health"( req )
        return { body: "ok\n", headers: { "content-type": "text/plain" } }
    end get

    get "/"( req )
        db = sqlite.connect(_store())
        rows = sqlite.query(db, "select id, body, created from notes order by id desc limit 50", [])
        sqlite.close(db)
        html = "<!doctype html><meta charset=utf-8><title>notesd</title>"
        html = html + "<h1>notes</h1><form method=post action=/notes>"
        html = html + "<input name=body size=60 autofocus><button>add</button></form><ul>"
        for each r in rows
            html = html + "<li>" + _escape(r.body) + " <small>" + _escape(r.created) + "</small></li>"
        next r
        return { body: html + "</ul>" }
    end get

    post "/notes"( req )
        body = trim(_form_value(req.body, "body"))
        if body = "" then
            return { status: 400, body: "a note needs a body\n" }
        end if
        db = sqlite.connect(_store())
        sqlite.exec(db, "insert into notes (body, created) values (?, ?)", [body, string(now())])
        sqlite.close(db)
        return { status: 303, headers: { location: "/" } }
    end post

end server

function _conf_path( args )
    if count(args) > 0 then
        return args[0]
    end if
    e = env("NOTESD_CONF")
    if not is_unknown(e) then
        return e
    end if
    return "/etc/notesd/notesd.conf"
end function

' Handlers run per request and do not see `main`'s locals, so the store is
' resolved from the same place each time rather than captured -- gBASIC has no
' closures, and a handler that referenced an enclosing variable would silently
' get a fresh local instead.
function _store()
    return _conf_get(_read_conf(_conf_path([])), "database", "/var/lib/notesd/notes.db")
end function

' Minimal urlencoded form decoding -- enough for one field. A real application
' would use a shared helper; this one stays self-contained on purpose so the
' package has exactly one moving part.
function _form_value( raw, name )
    if is_unknown(raw) then
        return ""
    end if
    for each pair in split(raw, "&")
        eq = find(pair, "=")
        if is_number(eq) then
            if left(pair, eq) = name then
                return _urldecode(mid(pair, eq + 1, len(pair)))
            end if
        end if
    next pair
    return ""
end function

function _urldecode( s )
    out = ""
    i = 0
    while i < len(s)
        c = mid(s, i, 1)
        ' gBASIC has no `else if`; `consider true` is the condition chain.
        consider true
        if c = "+" then
            out = out + " "
        if c = "%" and i + 2 < len(s) then
            out = out + from_bytes(hex_decode(mid(s, i + 1, 2)))
            i = i + 2
        else
            out = out + c
        end consider
        i = i + 1
    end while
    return out
end function

function _escape( s )
    t = replace(string(s), "&", "&amp;")
    t = replace(t, "<", "&lt;")
    return replace(t, ">", "&gt;")
end function

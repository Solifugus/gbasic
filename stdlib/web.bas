' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' web.bas — a route table as data, and dispatch over it.
'
' The library half of PLAT-WEB (docs/plat-web-design-draft.md §2, and step 2 of
' the build order in docs/plat-web-lowering-study.md §5): everything the
' `server` block would eventually be sugar FOR, written as ordinary values so
' it can be built, inspected and tested with no socket anywhere in sight.
'
'   load web
'   load webserver
'
'   function home(req)
'       return { status: 200, body: "hello" }
'   end function
'
'   function product(req)
'       return { body: "product " + req.params.id }
'   end function
'
'   routes = web.routes([
'       { method: "get", path: "/",              handler: home },
'       { method: "get", path: "/products/{id}", handler: product }
'   ])
'
'   server = webserver.listen(8080)
'   watch(server.requests)
'       while count(server.requests) > 0
'           req = take_first(server.requests)
'           append(server.responses, web.dispatch(routes, req))
'       end while
'   end watch
'
' THE TABLE IS CHECKED WHEN IT IS BUILT, not when a request arrives. An unknown
' verb, a malformed pattern, a duplicate route, a handler that is not a
' function: all raise from `web.routes`, at startup, where a mistake is a crash
' with a message naming it — rather than at 3am as a 404 nobody can explain.
' That is the library stand-in for the load-time errors the block grammar is
' meant to give (design §1).
'
' MATCHING IS ORDER-INDEPENDENT. Specificity decides, not table position:
' comparing two routes segment by segment, the first place they differ is
' settled by static > {param} > {rest...}. So "/products/new" beats
' "/products/{id}" however the table is written — and two routes that differ
' ONLY in their capture names are a tie, which `web.routes` REFUSES rather than
' letting dispatch flip a coin at runtime (design §4: "ties are a load-time
' error rather than a runtime coin flip").
'
' Three things this layer deliberately does NOT do, each because the obvious
' convenience would silently change what a client asked for:
'
'   * It does not decode percent-escapes in captures. `req.path` is raw, so
'     `{id}` hands back exactly the bytes between the slashes. Decoding here
'     would make %2F indistinguishable from a real separator, which is a
'     path-traversal bug waiting for a handler that joins the capture onto a
'     directory.
'   * It does not normalise a trailing slash. "/cart" and "/cart/" are
'     different paths and the second 404s. Rewriting one into the other is a
'     policy — one that turns a POST into a redirect — and policy belongs with
'     the caller, one line before dispatch.
'   * It does not answer HEAD from a GET route, or OPTIONS from the table.
'     Declare them if you want them.
library web

    ' PLAT-WEB-5: serve()/emit()/finish() call into the native webserver, and
    ' a library that calls into another loads it (house rule) -- before this,
    ' web.bas was pure data over tables and the caller loaded webserver.
    load webserver

    ' ---- small helpers --------------------------------------------------

    ' Path -> segments, dropping the empty piece before the leading "/".
    ' "/" -> [], "/a/b" -> ["a", "b"], "/a/" -> ["a", ""] (which matches
    ' nothing, because a capture never accepts an empty segment).
    function _segments(path)
        if path = "/" then
            return []
        end if
        parts = split(path, "/")
        out = []
        n = count(parts)
        i = 1
        while i < n
            append(out, parts[i])
            i = i + 1
        end while
        return out
    end function

    ' An identifier a caller can actually reach as `req.params.<name>`.
    ' Written with `contains` over a literal alphabet rather than `>=` range
    ' tests, which would be a comparison between strings.
    function _is_name(s)
        n = len(s)
        if n = 0 then
            return false
        end if
        head = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_"
        tail = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789"
        i = 0
        while i < n
            c = mid(s, i, 1)
            if i = 0 then
                ok = contains(head, c)
            else
                ok = contains(tail, c)
            end if
            if not ok then
                return false
            end if
            i = i + 1
        end while
        return true
    end function

    function _verb(raw, index)
        if not is_string(raw) then
            error "web.routes: route " + string(index) + " needs a string method"
        end if
        verb = upper(raw)
        known = ["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"]
        ok = contains(known, verb)
        if not ok then
            error "web.routes: route " + string(index) + " has unknown method '" + raw + "' (GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS)"
        end if
        return verb
    end function

    ' One path segment -> a descriptor carrying its specificity rank:
    '   { kind: "static", text: ..., rank: 2 }
    '   { kind: "param",  name: ..., rank: 1 }    one segment
    '   { kind: "rest",   name: ..., rank: 0 }    one or more, final only
    function _segment_of(text, path, index)
        opens = starts_with(text, "{")
        closes = ends_with(text, "}")
        if opens then
            if not closes then
                error "web.routes: route " + string(index) + " path '" + path + "' has an unclosed pattern in segment '" + text + "'"
            end if
        end if
        if not opens then
            ' A brace anywhere else is refused rather than taken literally: a
            ' half-written pattern that quietly became a static segment would
            ' match nothing and look like a routing bug for as long as it took
            ' someone to reread the string.
            stray_open = contains(text, "{")
            stray_close = contains(text, "}")
            if stray_open or stray_close then
                error "web.routes: route " + string(index) + " path '" + path + "' has a partial pattern in segment '" + text + "'; braces must wrap a WHOLE segment"
            end if
            if len(text) = 0 then
                error "web.routes: route " + string(index) + " path '" + path + "' has an empty segment"
            end if
            return { kind: "static", text: text, rank: 2 }
        end if

        body = mid(text, 1, len(text) - 2)
        greedy = ends_with(body, "...")
        if greedy then
            name = left(body, len(body) - 3)
        else
            name = body
        end if
        named = _is_name(name)
        if not named then
            error "web.routes: route " + string(index) + " path '" + path + "' has an invalid capture name in segment '" + text + "'; use letters, digits and _ starting with a letter or _"
        end if
        if greedy then
            return { kind: "rest", name: name, rank: 0 }
        end if
        return { kind: "param", name: name, rank: 1 }
    end function

    ' Two routes are indistinguishable when every position agrees in kind and,
    ' where static, in text. That — and only that — is a tie: everything else
    ' is settled by _more_specific below, whatever order the table is in.
    function _same_shape(a, b)
        an = count(a.segments)
        bn = count(b.segments)
        if an != bn then
            return false
        end if
        i = 0
        while i < an
            x = a.segments[i]
            y = b.segments[i]
            if x.rank != y.rank then
                return false
            end if
            if x.kind = "static" then
                if x.text != y.text then
                    return false
                end if
            end if
            i = i + 1
        end while
        return true
    end function

    ' True when a is more specific than b: the first position whose ranks
    ' differ decides, and a longer route wins an otherwise-exhausted tie.
    function _more_specific(a, b)
        an = count(a.segments)
        bn = count(b.segments)
        limit = an
        if bn < limit then
            limit = bn
        end if
        i = 0
        while i < limit
            ar = a.segments[i].rank
            br = b.segments[i].rank
            if ar != br then
                return ar > br
            end if
            i = i + 1
        end while
        return an > bn
    end function

    ' Match one prepared route against already-split request segments.
    ' -> { ok: true, params: {...} } or { ok: false }
    function _match_route(route, segs)
        rn = count(route.segments)
        pn = count(segs)
        params = {}
        i = 0
        while i < rn
            r = route.segments[i]

            if r.kind = "rest" then
                if pn <= i then
                    return { ok: false }
                end if
                tail = ""
                j = i
                while j < pn
                    if j > i then
                        tail = tail + "/"
                    end if
                    tail = tail + segs[j]
                    j = j + 1
                end while
                params[r.name] = tail
                return { ok: true, params: params }
            end if

            if pn <= i then
                return { ok: false }
            end if
            got = segs[i]

            if r.kind = "static" then
                if got != r.text then
                    return { ok: false }
                end if
            else
                if len(got) = 0 then
                    return { ok: false }
                end if
                params[r.name] = got
            end if

            i = i + 1
        end while

        if pn != rn then
            return { ok: false }
        end if
        return { ok: true, params: params }
    end function

    function _refuse_ties(prepared)
        n = count(prepared)
        i = 0
        while i < n
            j = i + 1
            while j < n
                a = prepared[i]
                b = prepared[j]
                if a.method = b.method then
                    tied = _same_shape(a, b)
                    if tied then
                        error "web.routes: '" + a.method + " " + a.path + "' and '" + b.method + " " + b.path + "' can never be told apart; dispatch would have to guess"
                    end if
                end if
                j = j + 1
            end while
            i = i + 1
        end while
        return true
    end function

    function _response(req, status, body, headers)
        out = { status: status, body: body, headers: headers }
        carries_id = has(req, "id")
        if carries_id then
            out.id = req.id
        end if
        return out
    end function

    ' ---- the surface ----------------------------------------------------

    ' Validate a list of { method, path, handler } records and return the
    ' prepared table. Raises on anything a request could not later explain.
    '
    ' PLAT-WEB-5: given a server DECLARATION instead (the value a `server`
    ' block binds), returns its route table as flat data -- one record per
    ' route with site, host, method, path and the stream flag -- so a block's
    ' routing is golden-testable with no socket (§9).
    function _is_declaration(v)
        shaped = is_record(v)
        if shaped then
            shaped = has(v, "kind")
        end if
        if shaped then
            shaped = v.kind = "server"
        end if
        return shaped
    end function

    function routes(list)
        is_declaration = _is_declaration(list)
        if is_declaration then
            ctx = _prepare_server(list)
            out = []
            for each s in ctx.sites
                for each r in s.routes
                    append(out, { site: s.name, host: s.host, method: r.method,
                                  path: r.path, stream: r.stream })
                end for
            end for
            return out
        end if
        if not is_array(list) then
            error "web.routes expects an array of route records"
        end if
        prepared = []
        n = count(list)
        i = 0
        while i < n
            entry = list[i]
            if not is_record(entry) then
                error "web.routes: route " + string(i) + " is not a record"
            end if
            has_method = has(entry, "method")
            has_path = has(entry, "path")
            has_handler = has(entry, "handler")
            if not has_method or not has_path or not has_handler then
                error "web.routes: route " + string(i) + " needs method, path and handler"
            end if

            verb = _verb(entry.method, i)
            path = entry.path
            if not is_string(path) then
                error "web.routes: route " + string(i) + " needs a string path"
            end if
            kind = type(entry.handler)
            if kind != "function" then
                error "web.routes: route " + string(i) + " (" + verb + " " + path + ") has a " + kind + " handler, not a function"
            end if
            rooted = starts_with(path, "/")
            if not rooted then
                error "web.routes: route " + string(i) + " path '" + path + "' must start with /"
            end if
            if len(path) > 1 then
                trailing = ends_with(path, "/")
                if trailing then
                    error "web.routes: route " + string(i) + " path '" + path + "' must not end with / ('" + path + "' and '" + left(path, len(path) - 1) + "' are different paths and this layer rewrites neither)"
                end if
            end if

            raw = _segments(path)
            segments = []
            seen = {}
            sn = count(raw)
            k = 0
            while k < sn
                seg = _segment_of(raw[k], path, i)
                if seg.kind = "rest" then
                    if k != sn - 1 then
                        error "web.routes: route " + string(i) + " path '" + path + "' puts {" + seg.name + "...} before the end; a greedy capture may only be the LAST segment"
                    end if
                end if
                if seg.kind != "static" then
                    repeated = has(seen, seg.name)
                    if repeated then
                        error "web.routes: route " + string(i) + " path '" + path + "' repeats the capture name '" + seg.name + "'"
                    end if
                    seen[seg.name] = true
                end if
                append(segments, seg)
                k = k + 1
            end while

            append(prepared, {
                method: verb,
                path: path,
                handler: entry.handler,
                segments: segments
            })
            i = i + 1
        end while
        _refuse_ties(prepared)
        return prepared
    end function

    ' The pure matcher: no handler is called and no response is built, so a
    ' whole routing scheme is testable as arithmetic on strings.
    '
    '   { ok: true,  route: <prepared route>, params: {...}, allow: [...] }
    '   { ok: false, params: {}, allow: [...] }
    '
    ' `allow` lists the methods declared for a path that DID match, which is
    ' what separates "no such page" from "not that verb".
    function resolve(prepared, method, path)
        verb = upper(method)
        segs = _segments(path)
        best = unknown
        best_params = {}
        allow = []
        n = count(prepared)
        i = 0
        while i < n
            route = prepared[i]
            hit = _match_route(route, segs)
            if hit.ok then
                already = contains(allow, route.method)
                if not already then
                    append(allow, route.method)
                end if
                if route.method = verb then
                    if is_unknown(best) then
                        best = route
                        best_params = hit.params
                    else
                        wins = _more_specific(route, best)
                        if wins then
                            best = route
                            best_params = hit.params
                        end if
                    end if
                end if
            end if
            i = i + 1
        end while
        allow = sort(allow)
        if is_unknown(best) then
            return { ok: false, params: {}, allow: allow }
        end if
        return { ok: true, route: best, params: best_params, allow: allow }
    end function

    ' Match, call, and shape the answer into a response record the webserver
    ' can take verbatim. Captures arrive as `req.params`.
    '
    ' PLAT-WEB-5: given a server DECLARATION as `prepared`, runs one request
    ' through the block's whole pipeline -- host dispatch, routes, static
    ' fallback -- with no socket (§9). A stream route answers with its opening
    ' head ({ stream: true }); the handler body needs a live connection and is
    ' not run here.
    function dispatch(prepared, req)
        is_declaration = _is_declaration(prepared)
        if is_declaration then
            out = _serve_request(_prepare_server(prepared), req)
            opens_stream = has(out, "stream_open")
            if opens_stream then
                return out.stream_open
            end if
            return out
        end if
        found = resolve(prepared, req.method, req.path)
        plain = { "content-type": "text/plain; charset=utf-8" }

        if not found.ok then
            allowed = count(found.allow)
            if allowed > 0 then
                ' A wrong verb on a real path is not a missing page, and
                ' saying 404 here sends the caller looking for the wrong bug.
                heads = plain
                heads["allow"] = join(found.allow, ", ")
                return _response(req, 405, "Method Not Allowed", heads)
            end if
            return _response(req, 404, "Not Found", plain)
        end if

        call_req = req
        call_req.params = found.params
        handler = found.route.handler
        answer = handler(call_req)

        if not is_record(answer) then
            ' Report it and keep serving. The design's error model is
            ' let-it-crash under a supervisor (§7b), but there is no worker
            ' pool yet, so a raise here would take the whole listener down
            ' over one bad handler. The 500 is not silent: it names the route
            ' on standard error.
            print to error "web.dispatch: handler for " + found.route.method + " " + found.route.path + " returned a " + type(answer) + ", not a response record"
            return _response(req, 500, "Internal Server Error", plain)
        end if

        ' Fill in what the handler left out, so the answer is a COMPLETE
        ' response record. These are the webserver's own defaults, so nothing
        ' changes on the wire; what changes is that the value describes itself
        ' — a caller testing dispatch with no server running can read
        ' `res.status` without having to know a downstream default.
        out = answer
        carries_status = has(out, "status")
        if not carries_status then
            out.status = 200
        end if
        carries_headers = has(out, "headers")
        if not carries_headers then
            out.headers = {}
        end if
        carries_id = has(out, "id")
        if not carries_id then
            req_has_id = has(req, "id")
            if req_has_id then
                out.id = req.id
            end if
        end if
        return out
    end function

    ' ---- PLAT-WEB-5: the server block's engine ---------------------------
    '
    ' A `server` declaration binds its name to a plain record (built by the
    ' interpreter, no socket touched); `serve(edge)` is the binding act. The
    ' whole dispatch pipeline below is ordinary library code over the WEB-1..4
    ' runtime -- which is the §10 test passing in public: the block really is
    ' sugar over what already works.

    ' Shape-check + per-site route preparation. Every table goes through
    ' routes(), so the block's routes are validated by the SAME code the
    ' library path uses -- one validator, not two drifting ones.
    function _prepare_server(sv)
        sites = []
        for each site in sv.sites
            entries = []
            i = 0
            while i < count(site.routes)
                r = site.routes[i]
                append(entries, { method: r.method, path: r.path, handler: r.handler })
                i = i + 1
            end while
            prepared = routes(entries)
            i = 0
            while i < count(prepared)
                prepared[i].stream = site.routes[i].stream
                i = i + 1
            end while
            append(sites, { name: site.name, host: site.host, root: site.root,
                            cert: site.cert, key: site.key, routes: prepared })
        end for
        return { name: sv.name, sites: sites, proxies: sv.trust_proxy,
                 options: sv.options, hooks: sv.hooks }
    end function

    ' The site a request lands on: exact host match wins, the default site
    ' (host "") answers everything else. The Host header carries a port in
    ' most requests; strip it, minding the bracketed IPv6 form.
    function _site_host(req)
        h = ""
        carries = has(req.headers, "host")
        if carries then
            h = req.headers["host"]
        end if
        bracket = find(h, "]")
        if bracket != nothing then
            return left(h, bracket + 1)
        end if
        colon = find(h, ":")
        if colon != nothing then
            return left(h, colon)
        end if
        return h
    end function

    function _site_for(ctx, req)
        h = _site_host(req)
        for each s in ctx.sites
            if s.host != "" then
                if s.host = h then
                    return s
                end if
            end if
        end for
        return ctx.sites[0]
    end function

    ' One request through the block: proxy trust, host dispatch, the route
    ' table, and the static root as the 404 fallback -- so routes win over
    ' files on overlap, which §6 states so no golden can enshrine the
    ' accident. Called natively per finished request (webserver.on_request);
    ' the return value IS the response. A stream route answers with the
    ' three-part protocol the native side sequences: head first, then the
    ' handler runs with the stream already open.
    function _serve_request(ctx, req)
        r = req
        if count(ctx.proxies) > 0 then
            r = trust_proxy(r, ctx.proxies)
        end if
        site = _site_for(ctx, r)
        found = resolve(site.routes, r.method, r.path)
        if found.ok then
            if found.route.stream then
                r.params = found.params
                h = {}
                h["content-type"] = "text/event-stream"
                h["cache-control"] = "no-cache"
                return { stream_open: { id: r.id, stream: true, headers: h },
                         handler: found.route.handler, req: r }
            end if
        end if
        out = dispatch(site.routes, r)
        if out.status = 404 then
            if site.root != "" then
                serves_files = r.method = "GET" or r.method = "HEAD"
                if serves_files then
                    rel = ""
                    if len(r.path) > 1 then
                        rel = mid(r.path, 1, len(r.path) - 1)
                    end if
                    s = static(rel, site.root)
                    s.id = r.id
                    return s
                end if
            end if
        end if
        return out
    end function

    ' Stream-handler verbs, spelled the way the draft writes them: emit to
    ' the request's own connection, finish it deliberately. The request
    ' carries everything the native side needs to find the server again.
    function emit(req, text)
        return webserver.emit(req.server, req.id, text)
    end function

    function finish(req)
        return webserver.finish(req.server, req.id)
    end function

    ' TLS derived from where the certs were WRITTEN: `cert:` beside each host
    ' (the SNI table is derived, not authored), the head's own cert as the
    ' default pair. Returns { enabled, options }.
    function _tls_options(sv)
        certs = []
        for each site in sv.sites
            if site.cert != "" then
                if site.host != "" then
                    append(certs, { host: site.host, cert: site.cert, key: site.key })
                end if
            end if
        end for
        out = {}
        enabled = false
        default_site = sv.sites[0]
        if default_site.cert != "" then
            out.cert = default_site.cert
            out.key = default_site.key
            enabled = true
        end if
        if count(certs) > 0 then
            out.certs = certs
            enabled = true
        end if
        return { enabled: enabled, options: out }
    end function

    ' Options for a socket THIS process binds (listen) versus one it adopted
    ' (inherited): an inherited socket is already bound, so address stays out.
    function _listen_options(sv, tls, adopting)
        out = {}
        if not adopting then
            if has(sv.options, "address") then
                out.address = sv.options.address
            end if
        end if
        if has(sv.options, "timeout") then
            out.timeout = sv.options.timeout
        end if
        if tls.enabled then
            out.tls = tls.options
        end if
        return out
    end function

    function _attach(s, ctx, sv)
        webserver.on_request(s, _serve_request, ctx)
        wants_drain = has(sv.hooks, "drain")
        if wants_drain then
            webserver.on_drain(s, sv.hooks.drain, ctx)
        end if
        return s
    end function

    ' The supervisor path (`workers: N`, N > 1): bind once with hold:, spawn
    ' N copies of THIS PROGRAM over the §7 pool, tick until asked to stop,
    ' then drain the workers. The worker side is serve() itself: a spawned
    ' copy finds inherited sockets, adopts them, prints the pool's READY
    ' marker and serves -- one code path for systemd and the supervisor.
    function _serve_pool(sv, ctx, tls)
        lopts = { hold: true }
        if has(sv.options, "address") then
            lopts.address = sv.options.address
        end if
        super = webserver.listen(sv.options.port, lopts)
        ' The supervisor serves nothing itself, so its stdout is free to
        ' announce the bound port -- which is the only way a caller can learn
        ' it when the declaration said `port: 0` (serve blocks here until the
        ' pool drains, so the caller's own print never runs while serving).
        print "PORT " + string(super.port)
        me = process.self()
        margs = ["--line-buffered", me.script]
        for each a in me.args
            append(margs, a)
        end for
        p = pool({ listener: super, command: me.interpreter, args: margs,
                   count: sv.options.workers })
        r = pool_start(p)
        if not r.ok then
            error "serve: worker pool failed to start: " + r.why
        end if
        ' The pool is a VALUE, and a function cannot write back into its
        ' caller's copy: pool_start and pool_tick RETURN the updated pool and
        ' dropping either return leaves `p.workers` empty forever. A supervisor
        ' holding an empty pool polls nothing, so it reports no death and
        ' respawns no worker -- `workers: N` would start N processes and then
        ' supervise none of them, which is the whole point of the pool.
        p = r.pool
        while not webserver.stopping()
            t = pool_tick(p)
            p = t.pool
            sleep(0.5)
        end while
        pool_stop(p)
        webserver.close(super)
        return super
    end function

    ' Bind and run a declared server. Blocking-by-consequence, not by call:
    ' serve() installs the native dispatch and returns the live server record
    ' (port readable off it); the process then serves for as long as a
    ' listener is active, and a drain (SIGTERM, or server.draining = true)
    ' lets it finish and exit. With `workers: N` this call IS the supervisor
    ' loop and returns after the pool drains.
    function serve(sv)
        shaped = _is_declaration(sv)
        if not shaped then
            error "serve expects a server declaration (a `server name(...)` block)"
        end if
        ctx = _prepare_server(sv)
        tls = _tls_options(sv)

        ' A spawned worker (or a systemd-activated process) adopts its
        ' inherited sockets and serves; nothing inherited means this process
        ' binds for itself -- the local-development path.
        inherited = webserver.inherited(_listen_options(sv, tls, true))
        if count(inherited) > 0 then
            s = inherited[0]
            print "READY"
            return _attach(s, ctx, sv)
        end if

        wants_port = has(sv.options, "port")
        if not wants_port then
            error "serve: server '" + sv.name + "' declares no port"
        end if

        workers = 1
        if has(sv.options, "workers") then
            workers = sv.options.workers
        end if
        if workers > 1 then
            return _serve_pool(sv, ctx, tls)
        end if

        s = webserver.listen(sv.options.port, _listen_options(sv, tls, false))
        return _attach(s, ctx, sv)
    end function

    ' ---- static files ---------------------------------------------------

    ' Extension -> content type. Deliberately a short list of what a site
    ' actually serves; anything else is served as bytes rather than guessed at,
    ' because a wrong content type is how a text file becomes a script.
    function content_type(name)
        ext = lower(extension(name))
        types = {
            html: "text/html; charset=utf-8",
            htm: "text/html; charset=utf-8",
            css: "text/css; charset=utf-8",
            js: "text/javascript; charset=utf-8",
            mjs: "text/javascript; charset=utf-8",
            json: "application/json",
            map: "application/json",
            xml: "application/xml",
            txt: "text/plain; charset=utf-8",
            md: "text/plain; charset=utf-8",
            csv: "text/csv; charset=utf-8",
            svg: "image/svg+xml",
            png: "image/png",
            jpg: "image/jpeg",
            jpeg: "image/jpeg",
            gif: "image/gif",
            webp: "image/webp",
            avif: "image/avif",
            ico: "image/vnd.microsoft.icon",
            woff: "font/woff",
            woff2: "font/woff2",
            ttf: "font/ttf",
            otf: "font/otf",
            pdf: "application/pdf",
            wasm: "application/wasm",
            zip: "application/zip",
            gz: "application/gzip"
        }
        known = has(types, ext)
        if known then
            return types[ext]
        end if
        return "application/octet-stream"
    end function

    ' Every answer web.static produces itself is plain text; the literal was
    ' repeated eight times before this existed.
    function _plain(status, body)
        return {
            status: status,
            headers: { "content-type": "text/plain; charset=utf-8" },
            body: body
        }
    end function

    ' Serve one file from under `root`. Returns a response record; `id` is left
    ' off, so `web.dispatch` fills it in from the request.
    '
    '   function assets(req)
    '       return web.static(req.params.path, "public")
    '   end function
    '
    '   { method: "get", path: "/assets/{path...}", handler: assets }
    '
    ' CANONICALIZE, THEN CHECK -- in that order, which is the whole point. A
    ' "does it start with the root" test applied to the path a CLIENT SENT can
    ' be walked out of with `..`, and cannot see a symlink at all. So the path
    ' is resolved by the kernel first (`real_path` follows every symlink and
    ' removes every `..`) and the containment test is applied to the answer.
    ' A path that resolves outside the root is refused even though the file is
    ' really there, and refused as 403 rather than 404: the request was
    ' well-formed and understood, and answering "not found" about a file that
    ' exists is a lie this layer would then have to keep telling.
    function static(relative, root)
        if not is_string(relative) then
            return _plain(404, "Not Found")
        end if

        ' Pre-validated, because `real_path` RAISES on an interior NUL. Catching
        ' that is possible since PLAT-ERR, and this check stays anyway: a
        ' REMOTE party controls this string, so the safe posture is that it can
        ' never reach the raising builtin at all, rather than that we remember
        ' to be armed when it does.
        if contains(relative, chr(0)) then
            return _plain(404, "Not Found")
        end if

        root_abs = real_path(root)
        if is_unknown(root_abs) then
            print to error "web.static: the root '" + string(root) + "' does not exist"
            return _plain(500, "Internal Server Error")
        end if
        root_kind = file_type(root_abs)
        if root_kind != "folder" then
            print to error "web.static: the root '" + string(root) + "' is a " + string(root_kind) + ", not a folder"
            return _plain(500, "Internal Server Error")
        end if

        resolved = real_path(root_abs + "/" + relative)
        if is_unknown(resolved) then
            return _plain(404, "Not Found")
        end if

        ' Containment on SEGMENT boundaries: a plain prefix test would let a
        ' root of /srv/pub match /srv/public-secret.
        if root_abs = "/" then
            prefix = "/"
        else
            prefix = root_abs + "/"
        end if
        inside = starts_with(resolved, prefix)
        if resolved = root_abs then
            inside = true
        end if
        if not inside then
            return _plain(403, "Forbidden")
        end if

        ' A directory is 404, not a listing: an index of what is on the disk is
        ' a disclosure, and choosing to publish one is the caller's decision to
        ' make explicitly.
        kind = file_type(resolved)
        if kind != "file" then
            return _plain(404, "Not Found")
        end if

        ' PLAT-WEB-4: answered as a FILE response, which the native side streams
        ' in bounded chunks with Content-Length from fstat -- so a gigabyte
        ' download costs a 64K buffer, not a gigabyte gBASIC string. This was
        ' the "non-slurping reads" half of the static-serving gap, parked here
        ' until the response model could stream.
        return {
            status: 200,
            headers: { "content-type": content_type(resolved) },
            file: resolved
        }
    end function

    ' ---- PLAT-WEB-4: server-sent events -------------------------------------
    '
    ' The stream response and the wire format, so a handler reads as intent:
    '
    '     return web.sse(req)                      ' open the stream
    '     webserver.emit(server, id, web.sse_event("tick"))
    '     webserver.emit(server, id, web.sse_named("update", payload))
    '
    ' ECONOMICS, stated where the functions are: in the worker-pool model a
    ' live stream PINS its worker between emits only if the handler loops; a
    ' PARKED stream (opened, then emitted to from other requests' handlers)
    ' costs nothing while quiet. But a worker emitting in a loop serves
    ' nothing else, so eight looping streams on workers: 4 is an outage.
    ' Budget workers for the streams you loop, and prefer the parked shape.
    function sse(req)
        h = {}
        h["content-type"] = "text/event-stream"
        h["cache-control"] = "no-cache"
        return { id: req.id, stream: true, headers: h }
    end function

    ' One SSE data event. Multi-line payloads become one event of several
    ' data: lines, which is the format's own spelling for them.
    function sse_event(data)
        out = ""
        for each line in split(data, "\n")
            out = out + "data: " + line + "\n"
        end for
        return out + "\n"
    end function

    function sse_named(name, data)
        return "event: " + name + "\n" + web.sse_event(data)
    end function

    ' Introspection: the table as sorted "METHOD /path" lines. The static
    ' route list the design wants a tool to be able to show (§1), available
    ' now without running a request through anything.
    ' ---- PLAT-WEB-3: proxy trust --------------------------------------------
    '
    ' Behind a reverse proxy, req.remote_ip is the proxy, and the client is in
    ' X-Forwarded-For. Rewriting blindly is an open invitation — ANY client can
    ' send that header — so the rule has two halves:
    '
    '   1. Rewrite only when the DIRECT peer is one of the named proxies.
    '   2. Take the RIGHTMOST address in X-Forwarded-For that is not itself a
    '      trusted proxy.
    '
    ' The second half corrects the design draft, which said "first hop". The
    ' header reads client, proxy1, proxy2 — each hop APPENDS the peer it saw —
    ' so the leftmost value is whatever the CLIENT chose to write, and taking
    ' it first hands spoofing back to the attacker the first rule just shut
    ' out. Walking from the right, the first address that is not one of your
    ' own proxies is the real peer of your outermost proxy: the client, as
    ' seen by infrastructure you trust.
    '
    ' Returns the request with remote_ip (and scheme, from X-Forwarded-Proto)
    ' rewritten, plus `forwarded: true` and the proxy's own address in
    ' `proxy_ip` — the evidence is moved aside, never destroyed.
    function trust_proxy(req, proxies)
        if not is_array(proxies) then
            error "web.trust_proxy expects an array of proxy addresses"
        end if
        if not contains(proxies, req.remote_ip) then
            return req
        end if
        chain = ""
        if has(req.headers, "x-forwarded-for") then
            chain = req.headers["x-forwarded-for"]
        end if
        if trim(chain) = "" then
            return req
        end if
        hops = []
        for each hop in split(chain, ",")
            t = trim(hop)
            if t != "" then
                hops = append(hops, t)
            end if
        end for
        if count(hops) = 0 then
            return req
        end if
        client = ""
        i = count(hops) - 1
        while i >= 0
            if not contains(proxies, hops[i]) then
                client = hops[i]
                break
            end if
            i = i - 1
        end while
        if client = "" then
            ' Every hop is one of our own proxies: header exhausted without an
            ' outside address. Leave the request as the socket saw it rather
            ' than inventing a peer.
            return req
        end if
        req.proxy_ip = req.remote_ip
        req.remote_ip = client
        req.forwarded = true
        if has(req.headers, "x-forwarded-proto") then
            proto = lower(trim(req.headers["x-forwarded-proto"]))
            if proto = "https" or proto = "http" then
                req.scheme = proto
            end if
        end if
        return req
    end function

    ' ---- PLAT-WEB-2: the worker pool ---------------------------------------
    '
    ' A supervisor holds the listener (webserver.listen with hold: true) and
    ' workers inherit it (process.start listen_fds: -> webserver.inherited() in
    ' the worker). This section is the POLICY over those primitives: keep N
    ' workers alive, replace them one at a time on reload, and never retire a
    ' worker on faith.
    '
    ' THE ROLLING RULE (design §7): a replacement must parse its source, come
    ' up, and print its ready marker before the old worker is drained — and it
    ' must then SURVIVE A PROBATION window. A deploy carrying a syntax error is
    ' a failed deploy and zero downtime, which is the actual guarantee wanted.
    ' This is also why workers are PROCESSES and not actors: an actor is spawned
    ' from the image already loaded, so it can never pick up edited source, and
    ' "must parse its source" is the load-bearing half of the rule.
    '
    ' DRAIN is two different verbs on purpose:
    '   process.stop(h)                    polite TERM; returns AT ONCE. The
    '                                      worker stops accepting, finishes
    '                                      in-flight requests, exits 0 itself.
    '   process.stop(h, {force_after:...}) BLOCKS until death or deadline, then
    '                                      SIGKILLs. Correct for "end this now",
    '                                      wrong as the first move of a drain —
    '                                      the point of draining is that work
    '                                      continues after the signal.
    ' The pool sends the polite form, grants drain_timeout through process.wait,
    ' and only then escalates.

    ' Build the pool spec, checked the way `routes` is checked: at build time,
    ' where a mistake is a message naming itself.
    '   { listener, command, args, count, ready, ready_timeout, probation,
    '     drain_timeout }
    function pool(spec)
        if not is_record(spec) then
            error "web.pool expects a spec record"
        end if
        if not has(spec, "listener") then
            error "web.pool: spec.listener must be a held server from webserver.listen"
        end if
        if not has(spec, "command") then
            error "web.pool: spec.command names the worker executable"
        end if
        out = { listener: spec.listener, command: spec.command,
                args: [], count: 1, ready: "READY",
                ready_timeout: 10, probation: 1, drain_timeout: 30,
                workers: [], restarts: 0 }
        if has(spec, "args") then
            out.args = spec.args
        end if
        if has(spec, "count") then
            if not is_number(spec.count) then
                error "web.pool: count must be a number"
            end if
            if spec.count < 1 then
                error "web.pool: count must be at least 1"
            end if
            out.count = spec.count
        end if
        for each k in ["ready", "ready_timeout", "probation", "drain_timeout"]
            if has(spec, k) then
                out[k] = spec[k]
            end if
        end for
        return out
    end function

    ' Start one worker and wait for its ready marker on stdout. Returns
    ' { ok, handle, why }. A worker that dies before the marker — a parse
    ' error in freshly deployed source being the canonical case — reports its
    ' stderr, which is where the interpreter put the reason.
    function _spawn(p)
        h = process.start({ command: p.command, args: p.args,
                            listen_fds: [p.listener] })
        waited = 0
        seen = ""
        errs = ""
        while waited < p.ready_timeout
            c = process.read(h)
            seen = seen + c.stdout
            errs = errs + c.stderr
            if find(seen, p.ready) != nothing then
                return { ok: true, handle: h, why: "" }
            end if
            st = process.poll(h)
            if not st.running then
                process.release(h)
                return { ok: false, handle: nothing,
                         why: "worker exited before ready (" + string(st.exit_code) + "): " + trim(errs) }
            end if
            sleep(0.05)
            waited = waited + 0.05
        end while
        process.stop(h, { force_after: 1 })
        process.release(h)
        return { ok: false, handle: nothing,
                 why: "worker did not report ready within " + string(p.ready_timeout) + "s" }
    end function

    ' Bring the pool to strength. Returns { ok, pool, why }; on failure the
    ' workers that DID start keep running (a partial pool serves — the failure
    ' is reported, not amplified).
    function pool_start(p)
        while count(p.workers) < p.count
            r = web._spawn(p)
            if not r.ok then
                return { ok: false, pool: p, why: r.why }
            end if
            p.workers = append(p.workers, r.handle)
        end while
        return { ok: true, pool: p, why: "" }
    end function

    ' One supervision pass: respawn anything that died. Call it from the
    ' supervisor's own loop; how often is the supervisor's business. Returns
    ' { pool, respawned, failed }.
    function pool_tick(p)
        respawned = 0
        failed = 0
        kept = []
        for each h in p.workers
            st = process.poll(h)
            if st.running then
                kept = append(kept, h)
            else
                print to error "web.pool: worker died (exit " + string(st.exit_code) + ", signal " + string(st.signal) + "); restarting"
                process.release(h)
                r = web._spawn(p)
                if r.ok then
                    kept = append(kept, r.handle)
                    respawned = respawned + 1
                    p.restarts = p.restarts + 1
                else
                    print to error "web.pool: restart failed: " + r.why
                    failed = failed + 1
                end if
            end if
        end for
        p.workers = kept
        return { pool: p, respawned: respawned, failed: failed }
    end function

    ' Drain one worker: polite TERM, a bounded wait for it to finish what it
    ' owes and exit itself, force only past the deadline.
    function _drain(p, h)
        process.stop(h)
        st = process.wait(h, p.drain_timeout)
        if st.running then
            print to error "web.pool: worker did not drain within " + string(p.drain_timeout) + "s; forcing"
            process.stop(h, { force_after: 1 })
        end if
        process.release(h)
        return nothing
    end function

    ' Rolling reload: replace workers ONE AT A TIME, never retiring an old one
    ' until its replacement is ready and has survived probation. On the first
    ' failure the roll STOPS and what is serving keeps serving. Returns
    ' { ok, pool, rolled, why }.
    function pool_reload(p)
        rolled = 0
        i = 0
        while i < count(p.workers)
            r = web._spawn(p)
            if not r.ok then
                return { ok: false, pool: p, rolled: rolled,
                         why: "deploy failed, old workers kept: " + r.why }
            end if
            ' Probation: a replacement that comes up and promptly dies must not
            ' cost the worker it was replacing.
            waited = 0
            alive = true
            while waited < p.probation
                st = process.poll(r.handle)
                if not st.running then
                    alive = false
                    break
                end if
                sleep(0.05)
                waited = waited + 0.05
            end while
            if not alive then
                process.release(r.handle)
                return { ok: false, pool: p, rolled: rolled,
                         why: "replacement died during probation; old workers kept" }
            end if
            web._drain(p, p.workers[i])
            p.workers[i] = r.handle
            rolled = rolled + 1
            i = i + 1
        end while
        return { ok: true, pool: p, rolled: rolled, why: "" }
    end function

    ' Stop everything, draining each worker. The pool record comes back empty
    ' rather than being a live thing that outlived its workers.
    function pool_stop(p)
        for each h in p.workers
            web._drain(p, h)
        end for
        p.workers = []
        return p
    end function

    function paths(prepared)
        out = []
        n = count(prepared)
        i = 0
        while i < n
            append(out, prepared[i].method + " " + prepared[i].path)
            i = i + 1
        end while
        return sort(out)
    end function

end library

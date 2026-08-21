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
    function routes(list)
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
    function dispatch(prepared, req)
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

        ' Pre-validated, because `real_path` RAISES on an interior NUL and a
        ' raise cannot be caught: an untrusted path must never be able to end
        ' the process.
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

        target (file)= resolved
        return {
            status: 200,
            headers: { "content-type": content_type(resolved) },
            body: read(target)
        }
    end function

    ' Introspection: the table as sorted "METHOD /path" lines. The static
    ' route list the design wants a tool to be able to show (§1), available
    ' now without running a request through anything.
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

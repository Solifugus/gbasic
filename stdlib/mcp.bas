' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' mcp -- publishing a toolset over the Model Context Protocol.
'
' docs/gbasic_ai_reference_and_primitives.md §1.11, step 6. Two transports and
' ONE dispatcher: `mcp.handle` turns a JSON-RPC request into a reply and does no
' I/O, so both the stdio loop and an HTTP `server` block are thin wrappers over
' the same function -- and the protocol is testable with neither transport
' present, which is the run_web_routes lesson one library over.
'
' THE STDIO TRANSPORT MUST RUN UNDER `--line-buffered`. A desktop client
' launches the server as a subprocess and waits for a reply on its stdout;
' block-buffered, that reply sits in the pipe buffer until the process exits,
' and the process is waiting for the client's next request. That is a DEADLOCK,
' not slowness, and tests/run_mcp.sh demonstrates it both ways rather than
' asserting it.
'
' THE MAPPED PRINCIPAL IS THE LEAK SURFACE. A read-only tool published over MCP
' is answered by whatever principal the configuration maps the calling agent to
' -- a service account standing in for everybody is exactly the god-view this
' design exists to avoid. So a `principal` may only be declared alongside a
' `max_groups` ceiling, and the server REFUSES TO START when it is exceeded.
' Serving with no principal at all is allowed and is the narrow state: tools see
' `principal() = nothing` and whatever they do about that is their own rule.
library mcp

    load tools from "tools.bas"
    load webclient

    function _err(id, code, message)
        e = {}
        e["code"] = code
        e["message"] = message
        r = {}
        r["jsonrpc"] = "2.0"
        r["id"] = id
        r["error"] = e
        return json_encode(r)
    end function

    function _ok(id, result)
        r = {}
        r["jsonrpc"] = "2.0"
        r["id"] = id
        r["result"] = result
        return json_encode(r)
    end function

    ' The options a server is started with. Checked once, at start, because a
    ' mapping that is too wide must stop the server rather than surface on the
    ' first call that uses it.
    function check_options(opts)
        if not is_record(opts) then
            error "mcp: expects an options record with name and version"
        end if
        ' origins and the session_* are handle_http's, checked there
        allowed = ["name", "version", "principal", "max_groups", "origins", "session_secret", "session_seconds"]
        for each k in keys(opts)
            if not contains(allowed, k) then
                error "mcp: no option '" + k + "'; it takes " + join(allowed, ", ")
            end if
        end for
        if not has(opts, "name") then
            error "mcp: a server needs a name -- it is what the client displays"
        end if
        if not is_string(opts.name) then
            error "mcp: the server name must be a string"
        end if
        if not has(opts, "version") then
            error "mcp: a server needs a version"
        end if
        if not is_string(opts.version) then
            error "mcp: the server version must be a string"
        end if
        if not has(opts, "principal") then
            ' No mapping at all. Tools see no principal, which is the narrowest
            ' state available and needs no ceiling to justify it.
            if has(opts, "max_groups") then
                error "mcp: max_groups was declared with no principal to bound"
            end if
            return nothing
        end if
        if not is_record(opts.principal) then
            error "mcp: principal must be a record describing who calls are answered for"
        end if
        if not has(opts, "max_groups") then
            error "mcp: a mapped principal needs a max_groups ceiling -- the mapping is the leak surface, and a service account standing in for everybody is exactly what it must not be"
        end if
        if not is_number(opts.max_groups) then
            error "mcp: max_groups must be a number"
        end if
        groups = []
        if has(opts.principal, "groups") then
            groups = opts.principal.groups
        end if
        if not is_array(groups) then
            error "mcp: the mapped principal's groups must be an array"
        end if
        if count(groups) > opts.max_groups then
            error "mcp: the mapped principal is in " + string(count(groups)) + " groups but the ceiling is " + string(opts.max_groups) + "; a mapping must be as narrow as the least-privileged person it stands in for, never a union"
        end if
        return nothing
    end function

    ' tools/list, in MCP's own spelling. `tools.schema` calls the parameter
    ' schema `parameters`; MCP calls it `inputSchema`, so the rename happens
    ' here and nowhere else.
    function tool_list(ts)
        out = []
        for each t in tools.schema(ts)
            e = {}
            e["name"] = t.name
            e["description"] = t.description
            e["inputSchema"] = t.parameters
            append(out, e)
        end for
        return { tools: out }
    end function

    function _call_result(part)
        block = {}
        block["type"] = "text"
        block["text"] = string(part.content)
        r = {}
        r["content"] = [ block ]
        r["isError"] = part.is_error
        return r
    end function

    ' ================= resources and prompts (Transward §7.1) =================
    '
    ' mcp.server(ts, { resources, templates, prompts }) -> a server declaration,
    ' which `mcp.handle` takes where it takes a toolset. ONE DECLARATION that
    ' the */list answers derive from and */read, */get check against, so the
    ' two cannot drift, as `tools.define` is for tools. Checked here, once:
    ' a malformed entry stops the program at start, not at the first read.
    '
    '   resources  [{ uri, name, describe, mime, fn }]   fn({}) answers the text
    '   templates  [{ uri, name, describe, mime, fn }]   uri holds {name} parts;
    '              fn({ name: "..." }) gets each part of the URI it matched
    '   prompts    [{ name, describe, arguments, fn }]   arguments are
    '              [{ name, describe, required }]; fn(args) answers the text of
    '              one user message, or [{ role, text }] for several
    '
    ' Every fn is called as a METHOD of its entry, so `this` is the entry, and
    ' under the request's principal, as a tool is. An answer that is not text
    ' is sent as JSON. A {name} matches one or more characters up to the next
    ' literal part, never a `/`, and is handed over as written (not
    ' percent-decoded).

    function _fields(rec, allowed, what)
        if not is_record(rec) then
            error "mcp: " + what + " must be a record"
        end if
        for each k in keys(rec)
            if not contains(allowed, k) then
                error "mcp: " + what + " has no field '" + k + "'; it takes " + join(allowed, ", ")
            end if
        end for
        return nothing
    end function

    function _need_text(rec, field, what)
        if not has(rec, field) then
            error "mcp: " + what + " needs " + field
        end if
        if not is_string(rec[field]) then
            error "mcp: " + what + "'s " + field + " must be a string"
        end if
        if rec[field] = "" then
            error "mcp: " + what + "'s " + field + " is empty"
        end if
        return nothing
    end function

    function _need_fn(rec, what)
        if not has(rec, "fn") then
            error "mcp: " + what + " needs fn, the function that answers it"
        end if
        if reflect.kind(rec.fn) != "function" then
            error "mcp: " + what + "'s fn must be a function value"
        end if
        return nothing
    end function

    ' A template's parts: [{ text }] for literal text, [{ name }] for a {name}.
    function _template_parts(uri, what)
        parts = []
        rest = uri
        while rest != ""
            lb = find(rest, "{")
            if lb = nothing then
                append(parts, { text: rest })
                rest = ""
            else
                if lb > 0 then
                    append(parts, { text: mid(rest, 0, lb) })
                end if
                after = mid(rest, lb + 1, len(rest) - lb - 1)
                rb = find(after, "}")
                if rb = nothing then
                    error "mcp: " + what + " has a { with no }"
                end if
                name = mid(after, 0, rb)
                if name = "" or find(name, "{") != nothing then
                    error "mcp: " + what + " has an empty or nested {...}"
                end if
                if count(parts) > 0 then
                    if has(parts[count(parts) - 1], "name") then
                        error "mcp: " + what + " has two {...} parts with nothing between them, so where one ends cannot be told"
                    end if
                end if
                append(parts, { name: name })
                rest = mid(after, rb + 1, len(after) - rb - 1)
            end if
        end while
        named = 0
        for each p in parts
            if has(p, "name") then named = named + 1
        end for
        if named = 0 then
            error "mcp: " + what + " has no {name} part; a URI with none is a resource, not a template"
        end if
        return parts
    end function

    ' The template's parts matched against a URI: { ok, vars }.
    function _template_match(parts, uri)
        vars = {}
        rest = uri
        i = 0
        while i < count(parts)
            p = parts[i]
            if has(p, "text") then
                if not starts_with(rest, p.text) then
                    return { ok: false, vars: {} }
                end if
                rest = mid(rest, len(p.text), len(rest) - len(p.text))
            else
                stop_at = len(rest)
                if i + 1 < count(parts) then
                    at = find(rest, parts[i + 1].text)
                    if at = nothing then
                        return { ok: false, vars: {} }
                    end if
                    stop_at = at
                end if
                value = mid(rest, 0, stop_at)
                if value = "" or find(value, "/") != nothing then
                    return { ok: false, vars: {} }
                end if
                vars[p.name] = value
                rest = mid(rest, stop_at, len(rest) - stop_at)
            end if
            i = i + 1
        end while
        return { ok: rest = "", vars: vars }
    end function

    function server(ts, decl)
        if not is_record(ts) then
            error "mcp.server expects a toolset (tools.define) and a declaration record"
        end if
        if not has(ts, "entries") then
            error "mcp.server expects a toolset (tools.define) and a declaration record"
        end if
        _fields(decl, ["resources", "templates", "prompts"], "the declaration")
        out = { kind: "mcp_server", tools: ts, resources: [], templates: [], prompts: [] }
        uris = []
        names = []
        for each field in ["resources", "templates"]
            if has(decl, field) then
                if not is_array(decl[field]) then
                    error "mcp: " + field + " must be an array of entries"
                end if
                for each e in decl[field]
                    what = "a " + mid(field, 0, len(field) - 1)
                    _fields(e, ["uri", "name", "describe", "mime", "fn"], what)
                    _need_text(e, "uri", what)
                    what = what + " '" + e.uri + "'"
                    _need_text(e, "name", what)
                    _need_fn(e, what)
                    if has(e, "describe") then
                        if not is_string(e.describe) then
                            error "mcp: " + what + "'s describe must be a string"
                        end if
                    end if
                    if has(e, "mime") then
                        if not is_string(e.mime) then
                            error "mcp: " + what + "'s mime must be a string"
                        end if
                    end if
                    if contains(uris, e.uri) then
                        error "mcp: " + what + " is declared twice"
                    end if
                    append(uris, e.uri)
                    if field = "templates" then
                        e["parts"] = _template_parts(e.uri, what)
                    else if find(e.uri, "{") != nothing then
                        error "mcp: " + what + " holds a {...} part; declare it under templates"
                    end if
                    rows = out[field]
                    append(rows, e)
                    out[field] = rows
                end for
            end if
        end for
        if has(decl, "prompts") then
            if not is_array(decl.prompts) then
                error "mcp: prompts must be an array of entries"
            end if
            for each e in decl.prompts
                _fields(e, ["name", "describe", "arguments", "fn"], "a prompt")
                _need_text(e, "name", "a prompt")
                what = "prompt '" + e.name + "'"
                _need_fn(e, what)
                if contains(names, e.name) then
                    error "mcp: " + what + " is declared twice"
                end if
                append(names, e.name)
                args = []
                if has(e, "arguments") then
                    args = e.arguments
                end if
                if not is_array(args) then
                    error "mcp: " + what + "'s arguments must be an array"
                end if
                seen = []
                for each a in args
                    _fields(a, ["name", "describe", "required"], what + "'s argument")
                    _need_text(a, "name", what + "'s argument")
                    if contains(seen, a.name) then
                        error "mcp: " + what + " declares the argument '" + a.name + "' twice"
                    end if
                    append(seen, a.name)
                    if has(a, "required") then
                        if type(a.required) != "boolean" then
                            error "mcp: " + what + "'s argument '" + a.name + "': required must be true or false"
                        end if
                    end if
                end for
                e["arguments"] = args
                rows = out.prompts
                append(rows, e)
                out.prompts = rows
            end for
        end if
        return out
    end function

    ' A server declaration from what handle was given: a declaration as it
    ' is, or a bare toolset with nothing else.
    function _server_of(x)
        if is_record(x) then
            if has(x, "kind") then
                if x.kind = "mcp_server" then
                    return x
                end if
            end if
        end if
        return { kind: "mcp_server", tools: x, resources: [], templates: [], prompts: [] }
    end function

    ' fn of entry e, called as its method, a raise caught: { ok, value, why }
    function _call_entry(e, arg)
        on error goto next
        v = e.fn(arg)
        if error then
            why = error.message
            error.clear()
            return { ok: false, value: nothing, why: why }
        end if
        return { ok: true, value: v, why: "" }
    end function

    ' ...under the request's principal, as a tool runs
    function _under(opts, caller, e, arg)
        if caller != nothing then
            with principal(caller)
                got = _call_entry(e, arg)
            end with
            return got
        end if
        if has(opts, "principal") then
            with principal(opts.principal)
                got = _call_entry(e, arg)
            end with
            return got
        end if
        return _call_entry(e, arg)
    end function

    function _as_text(v)
        if is_string(v) then
            return v
        end if
        return json_encode(v)
    end function

    function _described(out, e, key)
        if has(e, "describe") then
            out[key] = e.describe
        end if
        return out
    end function

    function resource_list(srv)
        out = []
        for each e in srv.resources
            r = {}
            r["uri"] = e.uri
            r["name"] = e.name
            r = _described(r, e, "description")
            if has(e, "mime") then
                r["mimeType"] = e.mime
            end if
            append(out, r)
        end for
        return { resources: out }
    end function

    function template_list(srv)
        out = []
        for each e in srv.templates
            r = {}
            r["uriTemplate"] = e.uri
            r["name"] = e.name
            r = _described(r, e, "description")
            if has(e, "mime") then
                r["mimeType"] = e.mime
            end if
            append(out, r)
        end for
        out_rec = {}
        out_rec["resourceTemplates"] = out
        return out_rec
    end function

    function prompt_list(srv)
        out = []
        for each e in srv.prompts
            r = {}
            r["name"] = e.name
            r = _described(r, e, "description")
            args = []
            for each a in e.arguments
                x = {}
                x["name"] = a.name
                x = _described(x, a, "description")
                req = false
                if has(a, "required") then
                    req = a.required
                end if
                x["required"] = req
                append(args, x)
            end for
            r["arguments"] = args
            append(out, r)
        end for
        return { prompts: out }
    end function

    ' resources/read: a declared resource, else the first template that
    ' matches. { reply } with a result or a JSON-RPC error already made.
    function _read(srv, opts, caller, id, params)
        if not is_record(params) then
            return _err(id, -32602, "params must be an object")
        end if
        if not has(params, "uri") then
            return _err(id, -32602, "resources/read needs a uri")
        end if
        if not is_string(params.uri) then
            return _err(id, -32602, "resources/read needs a uri")
        end if
        uri = params.uri
        hit = nothing
        vars = {}
        for each e in srv.resources
            if hit = nothing and e.uri = uri then
                hit = e
            end if
        end for
        for each e in srv.templates
            if hit = nothing then
                m = _template_match(e.parts, uri)
                if m.ok then
                    hit = e
                    vars = m.vars
                end if
            end if
        end for
        if hit = nothing then
            ' -32002 is MCP's "resource not found"
            return _err(id, -32002, "no resource " + uri)
        end if
        got = _under(opts, caller, hit, vars)
        if not got.ok then
            return _err(id, -32603, "the resource failed: " + got.why)
        end if
        c = {}
        c["uri"] = uri
        if has(hit, "mime") then
            c["mimeType"] = hit.mime
        else if not is_string(got.value) then
            c["mimeType"] = "application/json"
        end if
        c["text"] = _as_text(got.value)
        return _ok(id, { contents: [ c ] })
    end function

    ' prompts/get: its arguments checked against the declaration, as a
    ' tool's are.
    function _get_prompt(srv, opts, caller, id, params)
        if not is_record(params) then
            return _err(id, -32602, "params must be an object")
        end if
        if not has(params, "name") then
            return _err(id, -32602, "prompts/get needs a prompt name")
        end if
        hit = nothing
        for each e in srv.prompts
            if hit = nothing and e.name = params.name then
                hit = e
            end if
        end for
        if hit = nothing then
            return _err(id, -32602, "no prompt named " + string(params.name))
        end if
        args = {}
        if has(params, "arguments") then
            args = params.arguments
        end if
        if not is_record(args) then
            return _err(id, -32602, "a prompt's arguments must be an object")
        end if
        declared = []
        for each a in hit.arguments
            append(declared, a.name)
            req = false
            if has(a, "required") then
                req = a.required
            end if
            if req and not has(args, a.name) then
                return _err(id, -32602, "prompt '" + hit.name + "' needs the argument '" + a.name + "'")
            end if
        end for
        for each k in keys(args)
            if not contains(declared, k) then
                return _err(id, -32602, "prompt '" + hit.name + "' has no argument '" + k + "'")
            end if
            if not is_string(args[k]) then
                return _err(id, -32602, "prompt '" + hit.name + "': the argument '" + k + "' must be a string")
            end if
        end for
        got = _under(opts, caller, hit, args)
        if not got.ok then
            return _err(id, -32603, "the prompt failed: " + got.why)
        end if
        said = got.value
        if is_string(said) then
            said = [ { role: "user", text: said } ]
        end if
        if not is_array(said) then
            return _err(id, -32603, "the prompt answered " + type(said) + "; it answers text, or [{ role, text }]")
        end if
        messages = []
        for each m in said
            ok_shape = is_record(m)
            if ok_shape then
                ok_shape = has(m, "role") and has(m, "text")
            end if
            if not ok_shape then
                return _err(id, -32603, "a prompt's message is a record { role, text }")
            end if
            if m.role != "user" and m.role != "assistant" then
                return _err(id, -32603, "a prompt's message role is \"user\" or \"assistant\", not " + string(m.role))
            end if
            block = {}
            block["type"] = "text"
            block["text"] = _as_text(m.text)
            msg = {}
            msg["role"] = m.role
            msg["content"] = block
            append(messages, msg)
        end for
        res = {}
        if has(hit, "describe") then
            res["description"] = hit.describe
        end if
        res["messages"] = messages
        return _ok(id, res)
    end function

    ' ================= Streamable HTTP (Transward §7.4) =======================
    '
    ' mcp.handle_http(srv, opts, req [, caller]) -> a response record, for a
    ' `server` block route (`post`, `get` and `delete` on the one path all
    ' reach it). The transport is the protocol's work, so it is written once
    ' here rather than by every application:
    '
    '   - ORIGIN, checked on every request: one a browser sends from an origin
    '     not in `opts.origins` is refused 403, which is what stops a page on
    '     another site reaching the server through the user's browser (DNS
    '     rebinding). A request with no Origin is not a browser's.
    '   - POST of ONE message. A notification or a response is answered 202
    '     with no body; a request is answered application/json. A batch is
    '     refused. GET is 405: there is no server-to-client stream.
    '   - SESSIONS ARE SIGNED, NOT STORED. `initialize` issues Mcp-Session-Id,
    '     which carries the client's name, the protocol version and an expiry,
    '     signed with `opts.session_secret`; later requests must send it, 400
    '     without, 404 for one that is forged or expired (the client then
    '     initializes again). A table would live in one worker's memory, and a
    '     server block with `workers` would issue a session in one and refuse
    '     it in the next. The cost: a session cannot be ended early, so DELETE
    '     is 405, which the specification allows.
    '   - MCP-Protocol-Version, when sent, must be one this library speaks
    '     (400 otherwise).
    '
    ' mcp.session_of(opts, req) -> { client, version } from a valid session,
    ' or nothing: what an application records about who is calling.

    function _versions()
        return ["2025-06-18", "2025-03-26", "2024-11-05"]
    end function

    function _http_options(opts)
        check_options(opts)
        if not has(opts, "origins") then
            error "mcp.handle_http: opts needs origins, the browser origins allowed to call (use [] to allow none)"
        end if
        if not is_array(opts.origins) then
            error "mcp.handle_http: origins must be an array of origins such as \"https://transward.example\""
        end if
        for each o in opts.origins
            if not is_string(o) then
                error "mcp.handle_http: every origin must be a string"
            end if
        end for
        if not has(opts, "session_secret") then
            error "mcp.handle_http: opts needs session_secret, the key sessions are signed with, the same in every worker"
        end if
        if not is_string(opts.session_secret) then
            error "mcp.handle_http: session_secret must be a string of at least 32 bytes"
        end if
        ' BYTES, not codepoints: 32 random bytes often hold sequences that
        ' read as multi-byte characters, and `len` counted those as fewer, so
        ' about one random key in three was refused (Transward, 2026-11-06)
        if byte_count(opts.session_secret) < 32 then
            error "mcp.handle_http: session_secret must be a string of at least 32 bytes"
        end if
        if has(opts, "session_seconds") then
            if not is_number(opts.session_seconds) then
                error "mcp.handle_http: session_seconds must be a number of seconds"
            end if
            if opts.session_seconds <= 0 then
                error "mcp.handle_http: session_seconds must be a number of seconds"
            end if
        end if
        return nothing
    end function

    function _header(req, name)
        if not is_record(req.headers) then
            return nothing
        end if
        if not has(req.headers, name) then
            return nothing
        end if
        return req.headers[name]
    end function

    function _respond(status, body)
        h = {}
        if body != "" then
            h["Content-Type"] = "application/json"
        end if
        return { status: status, headers: h, body: body }
    end function

    function _session_issue(opts, client, version)
        seconds = 86400
        if has(opts, "session_seconds") then
            seconds = opts.session_seconds
        end if
        claims = { c: client, v: version, e: epoch() + seconds, n: hex_encode(random_bytes(8)) }
        payload = base64url_encode(json_encode(claims))
        return payload + "." + base64url_encode(hmac_sha256(opts.session_secret, payload))
    end function

    ' The session's claims, or nothing for one that is forged, malformed or
    ' expired. Anything may arrive in a header, so nothing here raises.
    function _session_check(opts, sid)
        if not is_string(sid) then
            return nothing
        end if
        parts = split(sid, ".")
        if count(parts) != 2 then
            return nothing
        end if
        expected = base64url_encode(hmac_sha256(opts.session_secret, parts[0]))
        if not bytes_equal(expected, parts[1]) then
            return nothing
        end if
        on error goto next
        claims = try_decode(base64url_decode(parts[0]))
        if error then
            error.clear()
            return nothing
        end if
        on error stop
        if not claims.ok then
            return nothing
        end if
        c = claims.value
        if not is_record(c) then
            return nothing
        end if
        if not has(c, "e") then
            return nothing
        end if
        if not is_number(c.e) then
            return nothing
        end if
        if c.e <= epoch() then
            return nothing
        end if
        return c
    end function

    function session_of(opts, req)
        _http_options(opts)
        c = _session_check(opts, _header(req, "mcp-session-id"))
        if c = nothing then
            return nothing
        end if
        return { client: c.c, version: c.v }
    end function

    function handle_http(srv, opts, req, caller = nothing)
        _http_options(opts)
        origin = _header(req, "origin")
        if origin != nothing then
            if not contains(opts.origins, origin) then
                return _respond(403, _err(nothing, -32600, "the origin " + string(origin) + " is not allowed to call this server"))
            end if
        end if
        if req.method != "POST" then
            r = _respond(405, "")
            r.headers["Allow"] = "POST"
            return r
        end if
        parsed = try_decode(req.body)
        if not parsed.ok then
            return _respond(400, _err(nothing, -32700, "parse error: " + parsed.message))
        end if
        msg = parsed.value
        if is_array(msg) then
            return _respond(400, _err(nothing, -32600, "a batch is not supported; send one message per request"))
        end if
        if not is_record(msg) then
            return _respond(400, _err(nothing, -32600, "a message must be an object"))
        end if
        is_request = has(msg, "method") and has(msg, "id")
        if has(msg, "method") then
            if msg.method = "initialize" then
                if not is_request then
                    return _respond(400, _err(nothing, -32600, "initialize is a request, with an id"))
                end if
                reply = handle(srv, opts, req.body, caller)
                answer = decode(reply)
                if not has(answer, "result") then
                    return _respond(200, reply)
                end if
                client = ""
                if has(msg, "params") then
                    if is_record(msg.params) then
                        if has(msg.params, "clientInfo") then
                            if is_record(msg.params.clientInfo) then
                                if has(msg.params.clientInfo, "name") then
                                    client = string(msg.params.clientInfo.name)
                                end if
                            end if
                        end if
                    end if
                end if
                r = _respond(200, reply)
                r.headers["Mcp-Session-Id"] = _session_issue(opts, client, answer.result.protocolVersion)
                return r
            end if
        end if
        sid = _header(req, "mcp-session-id")
        if sid = nothing then
            return _respond(400, _err(nothing, -32600, "Mcp-Session-Id is required after initialize"))
        end if
        if _session_check(opts, sid) = nothing then
            ' 404 tells the client to initialize again
            return _respond(404, _err(nothing, -32001, "no such session; initialize again"))
        end if
        pv = _header(req, "mcp-protocol-version")
        if pv != nothing then
            if not contains(_versions(), pv) then
                return _respond(400, _err(nothing, -32600, "MCP-Protocol-Version " + string(pv) + " is not one this server speaks (" + join(_versions(), ", ") + ")"))
            end if
        end if
        if not is_request then
            ' a notification, or a client's response: run, never answered
            if has(msg, "method") then
                ignored = handle(srv, opts, req.body, caller)
            end if
            return _respond(202, "")
        end if
        return _respond(200, handle(srv, opts, req.body, caller))
    end function

    ' mcp.handle(ts, opts, line [, caller]) -> the reply line, or "" for a
    ' notification.
    '
    ' NO I/O. This is the whole protocol, so both transports are wrappers and
    ' the protocol can be tested with neither of them present.
    '
    ' `caller` IS THIS REQUEST'S PRINCIPAL, for a transport that authenticates each
    ' request (over HTTP every request carries its own bearer token): a record
    ' the caller has already verified, which the tool runs under and which
    ' passes through untouched -- an application's own fields (Transward's
    ' kind, actor, role, via, ...) included. It takes the place of
    ' `opts.principal` for this request. The `max_groups` ceiling stays with
    ' `opts.principal`, the static mapping it was written for: a principal
    ' established per request from a verified token stands for one caller, not
    ' for everybody a configuration maps (Transward §7.3, 2026-11-06).
    function handle(ts, opts, line, caller = nothing)
        if caller != nothing and not is_record(caller) then
            error "mcp.handle: the caller is a record of who this request is for, not " + type(caller)
        end if
        ' a server declaration (mcp.server), or a bare toolset
        srv = _server_of(ts)
        ts = srv.tools
        parsed = try_decode(line)
        if not parsed.ok then
            return _err(nothing, -32700, "parse error: " + parsed.message)
        end if
        req = parsed.value
        if not is_record(req) then
            return _err(nothing, -32600, "a request must be an object")
        end if
        if not has(req, "method") then
            return _err(nothing, -32600, "a request needs a method")
        end if
        id = nothing
        if has(req, "id") then
            id = req.id
        end if
        ' A NOTIFICATION HAS NO id AND GETS NO REPLY. Answering one is the
        ' commonest way a JSON-RPC server confuses a client, because the client
        ' is not waiting for it and the reply arrives as the answer to whatever
        ' it asks next.
        is_notification = not has(req, "id")

        if req.method = "initialize" then
            if is_notification then return ""
            caps = {}
            caps["tools"] = {}
            if count(srv.resources) > 0 or count(srv.templates) > 0 then
                caps["resources"] = {}
            end if
            if count(srv.prompts) > 0 then
                caps["prompts"] = {}
            end if
            info = {}
            info["name"] = opts.name
            info["version"] = opts.version
            ' THE CLIENT'S VERSION WHEN THIS LIBRARY SPEAKS IT, else the newest
            ' it does, which the specification has the client decide about
            ' (Transward §7.4). Before 2026-11-06 this was always 2024-11-05.
            version = _versions()[0]
            if has(req, "params") then
                if is_record(req.params) then
                    if has(req.params, "protocolVersion") then
                        if contains(_versions(), req.params.protocolVersion) then
                            version = req.params.protocolVersion
                        end if
                    end if
                end if
            end if
            res = {}
            res["protocolVersion"] = version
            res["capabilities"] = caps
            res["serverInfo"] = info
            return _ok(id, res)
        end if

        if req.method = "tools/list" then
            if is_notification then return ""
            return _ok(id, tool_list(ts))
        end if

        params = {}
        if has(req, "params") then
            params = req.params
        end if
        if req.method = "resources/list" then
            if is_notification then return ""
            return _ok(id, resource_list(srv))
        end if
        if req.method = "resources/templates/list" then
            if is_notification then return ""
            return _ok(id, template_list(srv))
        end if
        if req.method = "resources/read" then
            if is_notification then return ""
            return _read(srv, opts, caller, id, params)
        end if
        if req.method = "prompts/list" then
            if is_notification then return ""
            return _ok(id, prompt_list(srv))
        end if
        if req.method = "prompts/get" then
            if is_notification then return ""
            return _get_prompt(srv, opts, caller, id, params)
        end if

        if req.method = "tools/call" then
            if is_notification then return ""
            params = {}
            if has(req, "params") then
                params = req.params
            end if
            if not is_record(params) then
                return _err(id, -32602, "params must be an object")
            end if
            if not has(params, "name") then
                return _err(id, -32602, "tools/call needs a tool name")
            end if
            args = {}
            if has(params, "arguments") then
                args = params.arguments
            end if
            ' Dispatch under the mapped principal, so a tool asking
            ' `principal()` gets the identity the configuration named and not
            ' whatever the server process happens to be.
            if caller != nothing then
                with principal(caller)
                    part = tools.dispatch(ts, params.name, args)
                end with
            else if has(opts, "principal") then
                with principal(opts.principal)
                    part = tools.dispatch(ts, params.name, args)
                end with
            else
                part = tools.dispatch(ts, params.name, args)
            end if
            ' A tool that failed is a RESULT with isError, not a JSON-RPC
            ' error: the model asked for something reasonable and got an
            ' answer it can react to. A protocol error means the CLIENT is
            ' malformed, which is a different thing and the client cannot fix
            ' it by trying again differently.
            return _ok(id, _call_result(part))
        end if

        if is_notification then
            return ""
        end if
        return _err(id, -32601, "no such method: " + string(req.method))
    end function

    ' The stdio transport: newline-delimited JSON-RPC on stdin and stdout.
    '
    ' RUN THIS UNDER `--line-buffered`. Without it the reply sits in a block
    ' buffer while the client waits for it and this loop waits for the client:
    ' a deadlock, not slowness.
    '
    ' A BLANK LINE ENDS THE SESSION, because gBASIC's `input()` returns "" for
    ' both a blank line and end of input and offers no way to tell them apart.
    ' JSON-RPC never sends a blank line, so this is correct for the protocol --
    ' but it is the language deciding, not this library (see DOGFOOD.md).
    function serve_stdio(ts, opts)
        check_options(opts)
        while true
            line = input()
            if line = "" then
                return nothing
            end if
            reply = handle(ts, opts, line)
            if reply != "" then
                print reply
            end if
        end while
        return nothing
    end function


    ' ================= consuming: gBASIC as an MCP client ====================
    '
    ' The other direction. Publishing hands a toolset to somebody else's agent;
    ' consuming lets ours call somebody else's tools.
    '
    ' TWO TRANSPORTS AGAIN, and the stdio one is why `process.write` had to
    ' exist: `process.start` used to hand back a live child you could only
    ' LISTEN to, and a stdio transport is a conversation.
    '
    ' THE DECLARATION IS CHECKED AGAINST THE SERVER AT CONNECT TIME. A spec may
    ' name the tools it expects, and connecting to a server that does not
    ' advertise one of them FAILS THERE -- not at the first call, hours later,
    ' inside whatever the agent happened to be doing. §1.11 asks for load-time
    ' and connect-time failures to be distinct diagnostics, and they are: a
    ' malformed spec is refused by `connect` before anything is launched, and a
    ' server that disagrees with the declaration is refused after.
    '
    ' `via: { mcp: ... }` IN A TOOL ENTRY IS DELIBERATELY NOT BUILT, and this
    ' is a decision rather than a deferral. It would couple `tools` to `mcp` --
    ' `tools.dispatch` would have to know how to reach a server, so `tools`
    ' would load `mcp` and every program using a toolset would carry the client
    ' whether or not it consumed anything. The same thing is two lines in the
    ' application, where the coupling belongs:
    '
    '     function read_file(args)
    '         return mcp.call(FILESHARE, "read_file", args).content
    '     end function

    ' `process` needs no `load` -- it is an unconditional module, like `money`
    ' and `reflect`. `webclient` does, and is loaded lazily where the http
    ' transport is used, so a program consuming over stdio does not drag
    ' libcurl in behind it.
    function _spec_fields(spec)
        if not is_record(spec) then
            error "mcp: connect expects a spec record"
        end if
        allowed = ["transport", "command", "args", "url", "expect", "headers", "client"]
        for each k in keys(spec)
            if not contains(allowed, k) then
                error "mcp: connect: no spec field '" + k + "'; it takes " + join(allowed, ", ")
            end if
        end for
        if not has(spec, "transport") then
            error "mcp: connect needs a transport: \"stdio\" or \"http\""
        end if
        if has(spec, "client") then
            if not is_string(spec.client) then
                error "mcp: client is the name this client gives the server, a string"
            end if
        end if
        if spec.transport = "stdio" then
            if has(spec, "headers") then
                error "mcp: headers are for the http transport; a stdio server is a child process"
            end if
            if not has(spec, "command") then
                error "mcp: a stdio server needs a command to launch"
            end if
            if not is_string(spec.command) then
                error "mcp: the command must be a string"
            end if
            return nothing
        end if
        if spec.transport = "http" then
            if not has(spec, "url") then
                error "mcp: an http server needs a url"
            end if
            if not is_string(spec.url) then
                error "mcp: the url must be a string"
            end if
            ' HEADERS COME FROM THE SPEC, so a bearer token never has to be in
            ' a command line, where any user on the machine can read it
            ' (Transward §7.5)
            if has(spec, "headers") then
                if not is_record(spec.headers) then
                    error "mcp: headers must be a record of header names and string values"
                end if
                for each k in keys(spec.headers)
                    if not is_string(spec.headers[k]) then
                        error "mcp: the header " + k + " must be a string"
                    end if
                end for
            end if
            return nothing
        end if
        error "mcp: unknown transport '" + string(spec.transport) + "'; it takes \"stdio\" or \"http\""
    end function

    ' One reply line from a stdio server. Bounded, because a server that never
    ' answers must produce a diagnostic rather than a hang -- and a hang is not
    ' a failure, it is a suite that never reports.
    function _read_line(child, seconds)
        buf = ""
        waited = 0
        while waited < seconds
            chunk = process.read(child)
            buf = buf + chunk.stdout
            nl = find(buf, "\n")
            if nl != nothing then
                return mid(buf, 0, nl)
            end if
            st = process.poll(child)
            if not st.running then
                ' One last read: what it wrote before exiting is still ours.
                chunk = process.read(child)
                buf = buf + chunk.stdout
                nl = find(buf, "\n")
                if nl != nothing then
                    return mid(buf, 0, nl)
                end if
                error "mcp: the server exited without answering"
            end if
            sleep(0.02)
            waited = waited + 0.02
        end while
        error "mcp: the server did not answer within " + string(seconds) + " seconds"
    end function

    ' A REQUEST ID per call. The handle is a value, so a counter in it would
    ' not advance across calls; ids need only be unique within the session,
    ' which a random one is (Matthew, 2026-11-06). It was always 3.
    function _new_id()
        return random_int(1, 2147483647)
    end function

    ' The HTTP request, with the spec's headers, the session's id and the
    ' version agreed at initialize (Streamable HTTP, Transward §7.5).
    function _http_post(h, line)
        hd = {}
        for each k in keys(h.headers)
            hd[k] = h.headers[k]
        end for
        hd["Content-Type"] = "application/json"
        hd["Accept"] = "application/json, text/event-stream"
        if h.session != "" then
            hd["Mcp-Session-Id"] = h.session
        end if
        if h.version != "" then
            hd["MCP-Protocol-Version"] = h.version
        end if
        return webclient.request({ method: "POST", url: h.url, headers: hd, body: line })
    end function

    ' A JSON-RPC response from a reply that is application/json or, as the
    ' specification also allows, an event stream: the first event whose data
    ' is the response.
    function _http_body(resp)
        ctype = ""
        if has(resp.headers, "content-type") then
            ctype = resp.headers["content-type"]
        end if
        if not starts_with(ctype, "text/event-stream") then
            return resp.body
        end if
        data = ""
        for each ln in split(replace(resp.body, "\r", ""), "\n")
            if starts_with(ln, "data:") then
                data = data + trim(mid(ln, 5, len(ln) - 5))
            else if ln = "" and data != "" then
                return data
            end if
        end for
        return data
    end function

    ' A notification: sent, and no reply is waited for (HTTP answers 202).
    function _notify(h, method)
        req = {}
        req["jsonrpc"] = "2.0"
        req["method"] = method
        line = json_encode(req)
        if h.transport = "stdio" then
            sent = process.write(h.child, line + "\n")
            return nothing
        end if
        resp = _http_post(h, line)
        if resp.status != 202 and resp.status != 200 then
            error "mcp: the server answered HTTP " + string(resp.status) + " to " + method
        end if
        return nothing
    end function

    function _rpc_reply(h, method, params, id)
        req = {}
        req["jsonrpc"] = "2.0"
        req["id"] = id
        req["method"] = method
        if is_record(params) then
            req["params"] = params
        end if
        line = json_encode(req)
        if h.transport = "stdio" then
            sent = process.write(h.child, line + "\n")
            if sent = 0 then
                error "mcp: could not write to the server (it has closed its input)"
            end if
            raw = _read_line(h.child, 10)
            resp = nothing
        else
            resp = _http_post(h, line)
            if resp.status = 404 and h.session != "" then
                error "mcp: the server no longer knows this session (HTTP 404); connect again"
            end if
            if resp.status != 200 then
                error "mcp: the server answered HTTP " + string(resp.status)
            end if
            raw = _http_body(resp)
        end if
        parsed = try_decode(raw)
        if not parsed.ok then
            error "mcp: the server's reply was not JSON: " + parsed.message
        end if
        return { value: parsed.value, response: resp }
    end function

    function _rpc(h, method, params, id)
        return _rpc_reply(h, method, params, id).value
    end function

    ' mcp.connect(spec) -> handle
    function connect(spec)
        _spec_fields(spec)
        headers = {}
        if has(spec, "headers") then
            headers = spec.headers
        end if
        h = { transport: spec.transport, child: unknown, url: unknown, tools: [], headers: headers,
              session: "", version: "" }
        if spec.transport = "stdio" then
            a = []
            if has(spec, "args") then
                a = spec.args
            end if
            ' `stdin: "pipe"` is the whole reason process.write exists: a stdio
            ' transport is a conversation, and a child you can only listen to
            ' cannot hold one.
            h.child = process.start({ command: spec.command, args: a, stdin: "pipe" })
        else
            h.url = spec.url
        end if

        ' initialize as the specification has it: the version asked for, this
        ' client's name, and the `initialized` notification after
        info = {}
        info["name"] = "gbasic"
        if has(spec, "client") then
            info["name"] = spec.client
        end if
        info["version"] = "1"
        ip = {}
        ip["protocolVersion"] = _versions()[0]
        ip["capabilities"] = {}
        ip["clientInfo"] = info
        got = _rpc_reply(h, "initialize", ip, _new_id())
        init = got.value
        if not is_record(init) then
            error "mcp: the server did not answer initialize"
        end if
        if has(init, "error") then
            error "mcp: the server refused initialize: " + string(init["error"].message)
        end if
        if h.transport = "http" then
            if has(got.response.headers, "mcp-session-id") then
                h.session = got.response.headers["mcp-session-id"]
            end if
            if is_record(init.result) then
                if has(init.result, "protocolVersion") then
                    h.version = string(init.result.protocolVersion)
                end if
            end if
        end if
        _notify(h, "notifications/initialized")

        listed = _rpc(h, "tools/list", {}, _new_id())
        if has(listed, "error") then
            error "mcp: the server refused tools/list: " + string(listed["error"].message)
        end if
        advertised = []
        if is_record(listed.result) then
            if is_array(listed.result.tools) then
                advertised = listed.result.tools
            end if
        end if
        h.tools = advertised

        ' THE DECLARATION MEETS THE SERVER, HERE. A tool the caller declared and
        ' the server does not advertise is a connect-time failure, named -- not
        ' a surprise at the first call, hours later, inside whatever the agent
        ' was doing.
        if has(spec, "expect") then
            if not is_array(spec.expect) then
                error "mcp: `expect` must be an array of tool names"
            end if
            have = []
            for each t in advertised
                append(have, t.name)
            end for
            for each want in spec.expect
                if not contains(have, want) then
                    error "mcp: the server does not advertise '" + string(want) + "'; it offers " + join(have, ", ")
                end if
            end for
        end if
        return h
    end function

    ' The advertised tools, as the server described them.
    function tools_of(h)
        return h.tools
    end function

    ' mcp.call(h, name, args) -> a tool-result part, THE SAME SHAPE
    ' `tools.dispatch` returns -- so a remote tool and a local one are the same
    ' thing to whatever consumes the result.
    function call(h, name, args)
        if not is_string(name) then
            error "mcp: call expects a tool name"
        end if
        p = {}
        p["name"] = name
        if is_record(args) then
            p["arguments"] = args
        end if
        reply = _rpc(h, "tools/call", p, _new_id())
        r = {}
        r["type"] = "tool_result"
        r["name"] = name
        if has(reply, "error") then
            ' A PROTOCOL error becomes an error RESULT here, because from the
            ' caller's side "that call did not work" is one outcome however the
            ' far end chose to phrase it. The message says which it was.
            r["content"] = "the server refused the call: " + string(reply["error"].message)
            r["is_error"] = true
            return r
        end if
        text = ""
        failed = false
        if is_record(reply.result) then
            if is_array(reply.result.content) then
                for each block in reply.result.content
                    if is_record(block) then
                        if has(block, "text") then
                            text = text + string(block.text)
                        end if
                    end if
                end for
            end if
            if has(reply.result, "isError") then
                failed = reply.result.isError
            end if
        end if
        r["content"] = text
        r["is_error"] = failed
        return r
    end function

    function disconnect(h)
        if h.transport = "stdio" then
            if not is_unknown(h.child) then
                x = process.close_stdin(h.child)
                y = process.stop(h.child)
                z = process.release(h.child)
            end if
        end if
        return nothing
    end function

end library

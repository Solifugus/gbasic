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
        allowed = ["name", "version", "principal", "max_groups"]
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

    ' mcp.handle(ts, opts, line) -> the reply line, or "" for a notification.
    '
    ' NO I/O. This is the whole protocol, so both transports are wrappers and
    ' the protocol can be tested with neither of them present.
    function handle(ts, opts, line)
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
            info = {}
            info["name"] = opts.name
            info["version"] = opts.version
            res = {}
            res["protocolVersion"] = "2024-11-05"
            res["capabilities"] = caps
            res["serverInfo"] = info
            return _ok(id, res)
        end if

        if req.method = "tools/list" then
            if is_notification then return ""
            return _ok(id, tool_list(ts))
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
            if has(opts, "principal") then
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
        allowed = ["transport", "command", "args", "url", "expect"]
        for each k in keys(spec)
            if not contains(allowed, k) then
                error "mcp: connect: no spec field '" + k + "'; it takes " + join(allowed, ", ")
            end if
        end for
        if not has(spec, "transport") then
            error "mcp: connect needs a transport: \"stdio\" or \"http\""
        end if
        if spec.transport = "stdio" then
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

    function _rpc(h, method, params, id)
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
        else
            resp = webclient.post(h.url, line)
            if resp.status != 200 then
                error "mcp: the server answered HTTP " + string(resp.status)
            end if
            raw = resp.body
        end if
        parsed = try_decode(raw)
        if not parsed.ok then
            error "mcp: the server's reply was not JSON: " + parsed.message
        end if
        return parsed.value
    end function

    ' mcp.connect(spec) -> handle
    function connect(spec)
        _spec_fields(spec)
        h = { transport: spec.transport, child: unknown, url: unknown, tools: [] }
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

        init = _rpc(h, "initialize", {}, 1)
        if not is_record(init) then
            error "mcp: the server did not answer initialize"
        end if
        if has(init, "error") then
            error "mcp: the server refused initialize: " + string(init["error"].message)
        end if

        listed = _rpc(h, "tools/list", {}, 2)
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
        reply = _rpc(h, "tools/call", p, 3)
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

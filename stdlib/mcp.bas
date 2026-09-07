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

end library

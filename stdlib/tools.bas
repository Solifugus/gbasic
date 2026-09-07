' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' tools -- one declaration of what a model may call, and the safe way to call it.
'
' docs/gbasic_ai_reference_and_primitives.md, step 3. This is the EXTRACTION of
' what `llm.bas` already does, not a rival to it, and it exists because the
' existing shape cannot do three things.
'
' ONE DECLARATION, TWO CONSUMERS. `llm.tool` takes a hand-written JSON-Schema
' record AND a function, and nothing checks that they agree: the schema tells
' the model `id` is required and the function may cheerfully read `customer_id`.
' Here `params` is the declaration, `tools.schema` derives what the model is
' told, and `tools.dispatch` validates against the same thing -- so the two
' cannot drift, which is a property rather than a discipline.
'
' A RAISING TOOL IS SURVIVABLE. `llm.bas` says a tool "must not raise", which
' was true when it was written: a raise could not be caught at all. PLAT-ERR
' changed that and the comment outlived it -- measured, a tool that raises
' still ends the whole run today. `dispatch` runs the body under `on error` and
' turns ANY failure into a result the model can react to, which is the
' difference between a bad tool call and a dead agent.
'
' AND EFFECTS ARE DECLARED. `reads` and `mutates` name what a tool touches, so
' an approval gate can ask "does this change anything" without reading the
' body. The library does not gate -- that is a decision, and decisions belong
' to whoever is accountable for them -- it only makes the question answerable.
library tools

    ' The parameter types a tool may declare. Deliberately the JSON types that
    ' gBASIC also has a predicate for, so what the model is told and what
    ' dispatch checks are the same set with no mapping in between.
    function _types()
        return ["string", "number", "boolean", "array", "record"]
    end function

    function _type_ok(v, want)
        if want = "string" then return is_string(v)
        if want = "number" then return is_number(v)
        if want = "boolean" then return is_boolean(v)
        if want = "array" then return is_array(v)
        if want = "record" then return is_record(v)
        return false
    end function

    ' Refuse an unrecognised field BY NAME. A misspelled `describe` is otherwise
    ' indistinguishable from a deliberate omission, and the tool ships with no
    ' description and nothing said -- the rule `webserver.listen` follows for
    ' its options and `reasoning.check_context` for a Context.
    function _check_fields(rec, allowed, what)
        for each k in keys(rec)
            if not contains(allowed, k) then
                error "tools: " + what + " has no field '" + k + "'; it takes " + join(allowed, ", ")
            end if
        end for
        return nothing
    end function

    function _check_param(p, tool_name, seen)
        if not is_record(p) then
            error "tools: tool '" + tool_name + "' has a parameter that is not a record"
        end if
        _check_fields(p, ["name", "type", "describe", "required", "default"],
                      "a parameter of tool '" + tool_name + "'")
        ' Every read is guarded: reading a field a record does not have RAISES
        ' in gBASIC, so an unguarded `p.type` reports "unknown record field:
        ' type" instead of the message written for it -- true, located in the
        ' library, and useless to the author of the declaration.
        if not has(p, "name") then
            error "tools: tool '" + tool_name + "' has a parameter with no name"
        end if
        if not is_string(p.name) then
            error "tools: tool '" + tool_name + "' has a parameter with no name"
        end if
        if contains(seen, p.name) then
            error "tools: tool '" + tool_name + "' declares parameter '" + p.name + "' twice"
        end if
        if not has(p, "type") then
            error "tools: parameter '" + p.name + "' of tool '" + tool_name + "' needs a type"
        end if
        if not is_string(p.type) then
            error "tools: parameter '" + p.name + "' of tool '" + tool_name + "' needs a type"
        end if
        if not contains(_types(), p.type) then
            error "tools: parameter '" + p.name + "' of tool '" + tool_name + "' has unknown type '" + p.type + "'; use " + join(_types(), ", ")
        end if
        if not has(p, "describe") then
            error "tools: parameter '" + p.name + "' of tool '" + tool_name + "' needs a describe"
        end if
        if not is_string(p.describe) then
            error "tools: parameter '" + p.name + "' of tool '" + tool_name + "' needs a describe"
        end if
        if not has(p, "required") then
            error "tools: parameter '" + p.name + "' of tool '" + tool_name + "' must say required: true or false"
        end if
        if not is_boolean(p.required) then
            error "tools: parameter '" + p.name + "' of tool '" + tool_name + "' must say required: true or false"
        end if
        ' A default on a REQUIRED parameter is a contradiction: the model is
        ' told it must supply one, and the value is then ignored.
        if p.required and has(p, "default") then
            error "tools: parameter '" + p.name + "' of tool '" + tool_name + "' is required and also has a default"
        end if
        if has(p, "default") then
            if not _type_ok(p.default, p.type) then
                error "tools: the default for '" + p.name + "' of tool '" + tool_name + "' is not a " + p.type
            end if
        end if
        return nothing
    end function

    function _check_effects(v, field, tool_name)
        if not is_array(v) then
            error "tools: tool '" + tool_name + "' needs " + field + " as an array of names (use [] for none)"
        end if
        for each e in v
            if not is_string(e) then
                error "tools: tool '" + tool_name + "' has a non-string entry in " + field
            end if
        end for
        return nothing
    end function

    function _check_entry(e, seen)
        if not is_record(e) then
            error "tools: every entry must be a record"
        end if
        _check_fields(e, ["name", "describe", "fn", "params", "reads", "mutates"],
                      "a tool entry")
        if not has(e, "name") then
            error "tools: every entry needs a string name"
        end if
        if not is_string(e.name) then
            error "tools: every entry needs a string name"
        end if
        if contains(seen, e.name) then
            error "tools: '" + e.name + "' is defined twice in one toolset"
        end if
        if not has(e, "describe") then
            error "tools: tool '" + e.name + "' needs a describe -- it is what the model reads to decide whether to call it"
        end if
        if not is_string(e.describe) then
            error "tools: tool '" + e.name + "' needs a describe -- it is what the model reads to decide whether to call it"
        end if
        if not has(e, "fn") then
            error "tools: tool '" + e.name + "' needs fn to be a function value"
        end if
        if reflect.kind(e.fn) != "function" then
            error "tools: tool '" + e.name + "' needs fn to be a function value"
        end if
        if not has(e, "params") then
            error "tools: tool '" + e.name + "' needs params as an array (use [] for none)"
        end if
        if not is_array(e.params) then
            error "tools: tool '" + e.name + "' needs params as an array (use [] for none)"
        end if
        pseen = []
        for each p in e.params
            _check_param(p, e.name, pseen)
            append(pseen, p.name)
        end for
        ' Effects are REQUIRED, not defaulted to empty. "This tool changes
        ' nothing" is a claim somebody should have to make.
        if not has(e, "reads") then
            error "tools: tool '" + e.name + "' needs reads as an array of names (use [] for none)"
        end if
        if not has(e, "mutates") then
            error "tools: tool '" + e.name + "' needs mutates as an array of names (use [] for none)"
        end if
        _check_effects(e.reads, "reads", e.name)
        _check_effects(e.mutates, "mutates", e.name)
        return nothing
    end function

    ' tools.define(name, entries) -> toolset
    function define(name, entries)
        if not is_string(name) then
            error "tools: define expects a toolset name"
        end if
        if not is_array(entries) then
            error "tools: define expects an array of tool entries"
        end if
        seen = []
        for each e in entries
            _check_entry(e, seen)
            append(seen, e.name)
        end for
        return { name: name, entries: entries }
    end function

    function names(ts)
        out = []
        for each e in ts.entries
            append(out, e.name)
        end for
        return out
    end function

    ' The entry, or `unknown`. `find` on a miss returns `nothing`, and
    ' is_unknown(nothing) is FALSE -- so callers check with is_unknown here.
    function entry(ts, name)
        for each e in ts.entries
            if e.name = name then return e
        end for
        return unknown
    end function

    ' What a tool declares it changes. Empty means "nothing", which is a claim
    ' the author had to make: `mutates` is required at define time.
    function mutates(ts, name)
        e = entry(ts, name)
        if is_unknown(e) then
            error "tools: no such tool '" + string(name) + "'"
        end if
        return e.mutates
    end function

    function reads(ts, name)
        e = entry(ts, name)
        if is_unknown(e) then
            error "tools: no such tool '" + string(name) + "'"
        end if
        return e.reads
    end function

    ' ---- one declaration, derived twice ------------------------------------

    ' The JSON-Schema subset for one tool's parameters. Derived from `params`,
    ' which is the same thing `dispatch` validates against -- that is the whole
    ' point, and tests/run_tools.sh drives dispatch FROM this output to prove
    ' the two cannot disagree.
    function param_schema(e)
        props = {}
        req = []
        for each p in e.params
            spec = {}
            spec["type"] = p.type
            spec["description"] = p.describe
            props[p.name] = spec
            if p.required then
                append(req, p.name)
            end if
        end for
        s = {}
        s["type"] = "object"
        s["properties"] = props
        s["required"] = req
        return s
    end function

    ' The toolset as records: name, description, parameters, reads, mutates.
    ' This is what an MCP server publishes and what a provider adapter shapes.
    ' It is not reflection -- the entries ARE records, so this returns what
    ' `define` was given, which is what makes "publishing is `server` plus
    ' `tools`" true rather than aspirational.
    function schema(ts)
        out = []
        for each e in ts.entries
            r = {}
            r["name"] = e.name
            r["description"] = e.describe
            r["parameters"] = param_schema(e)
            r["reads"] = e.reads
            r["mutates"] = e.mutates
            append(out, r)
        end for
        return out
    end function

    ' llm.tool-shaped records, so a toolset can drive the existing chat loop.
    ' The evidence that this layer is an EXTRACTION rather than a second
    ' vocabulary: the same declaration feeds both.
    function as_llm_tools(ts)
        out = []
        for each e in ts.entries
            append(out, {
                name: e.name,
                description: e.describe,
                schema: param_schema(e),
                fn: e.fn
            })
        end for
        return out
    end function

    ' ---- dispatch ----------------------------------------------------------

    function _result(name, content, failed)
        r = {}
        r["type"] = "tool_result"
        r["name"] = name
        r["content"] = content
        r["is_error"] = failed
        return r
    end function

    ' Fill in declared defaults and check what the model actually sent against
    ' the SAME declaration the model was shown.
    '
    ' RETURNS the prepared arguments rather than filling in an out-parameter: a
    ' gBASIC record is a VALUE, so a record passed in and mutated is mutated in
    ' a local copy and the caller sees nothing. Written the other way first,
    ' and every tool then ran with an EMPTY argument record -- which failed
    ' loudly here only because the tool read a field that was missing.
    function _prepare(e, args)
        out = {}
        for each p in e.params
            present = has(args, p.name)
            if present then
                if not _type_ok(args[p.name], p.type) then
                    return { why: "parameter '" + p.name + "' should be a " + p.type, args: out }
                end if
                out[p.name] = args[p.name]
            else
                if p.required then
                    return { why: "missing required parameter '" + p.name + "'", args: out }
                end if
                if has(p, "default") then
                    out[p.name] = p.default
                end if
            end if
        end for
        for each k in keys(args)
            found = false
            for each p in e.params
                if p.name = k then found = true
            end for
            if not found then
                return { why: "unknown parameter '" + k + "'", args: out }
            end if
        end for
        return { why: "", args: out }
    end function

    ' tools.dispatch(ts, name, args) -> a tool-result part.
    '
    ' NEVER RAISES, and that is the feature. A model sends whatever it likes and
    ' a tool body is ordinary code that can fail; both must come back as
    ' something the model can read and retry, because the alternative is an
    ' agent that dies on its first bad argument. Any failure -- unknown tool,
    ' bad arguments, a raise inside the body -- is a result with is_error true.
    function dispatch(ts, name, args)
        if not is_string(name) then
            return _result(string(name), "tool name must be a string", true)
        end if
        e = entry(ts, name)
        if is_unknown(e) then
            return _result(name, "unknown tool '" + name + "'", true)
        end if
        if not is_record(args) then
            return _result(name, "arguments must be a record", true)
        end if
        checked = _prepare(e, args)
        if checked.why != "" then
            return _result(name, checked.why, true)
        end if
        prepared = checked.args

        ' The body runs under `on error`. Before PLAT-ERR this could not be
        ' written at all, which is why the older layer documents "a tool must
        ' not raise" -- a rule the language now makes unnecessary.
        on error goto next
        fn = e.fn
        v = fn(prepared)
        if error then
            why = error.message
            error.clear()
            return _result(name, "the tool failed: " + why, true)
        end if

        ' A tool may also report failure by RETURNING an error, which is how a
        ' tool says "no such customer" without treating it as a defect.
        if is_record(v) then
            reported = v["error"]
            if is_string(reported) then
                return _result(name, reported, true)
            end if
        end if
        return _result(name, v, false)
    end function


    ' ---- the worker pool ---------------------------------------------------
    '
    ' WHY A POOL AT ALL. A tool body is ordinary code: it queries a database,
    ' calls a service, reads a file. Run on the event loop's thread -- which is
    ' where a request handler runs -- every tool call freezes every other
    ' client for its whole duration. Pre-spawned workers move the body
    ' somewhere it is allowed to block, and the reply comes back through the
    ' loop's own `poll` as an `inbox.messages` event.
    '
    ' The numbers decide the shape: spawning an actor per call is 50-78ms of
    ' fork+exec before any work happens, and a `pg` connection cannot cross
    ' `spawn` so each call would reconnect; once a worker exists a message
    ' round trip is 0.015ms and it keeps its own connections. So the pool is
    ' built once at startup.
    '
    ' THE CALLER SPAWNS, NOT THIS LIBRARY, and that is the language rather than
    ' a preference: `spawn` resolves a bare function NAME, so it takes neither
    ' a library function nor a function value. The worker LOOP still lives
    ' here, so the protocol has one definition:
    '
    '     function tool_worker(back, ts)      ' in your program
    '         tools.serve(back, ts)
    '         return nothing
    '     end function
    '
    '     handles = []
    '     for i = 1 to 4
    '         append(handles, spawn tool_worker(self(), ts))
    '     end for
    '     p = tools.pool(handles)

    ' The worker loop. Runs in the child until told to stop.
    function serve(back, ts)
        while true
            m = receive()
            if not is_record(m) then
                send(back, { kind: "tool_result", run_id: unknown, call_id: unknown,
                             result: _result("?", "worker received a non-record message", true) })
            else
                if has(m, "kind") then
                    if m.kind = "stop" then
                        return nothing
                    end if
                end if
                ' The principal travels as ORDINARY DATA in the message and the
                ' worker's first act is to re-enter the scope. It does not cross
                ' `spawn` implicitly -- an identity that arrived without anyone
                ' writing it down is an identity nobody can audit -- so this is
                ' the handoff, made explicit.
                if has(m, "principal") and is_record(m.principal) then
                    with principal(m.principal)
                        r = dispatch(ts, m.name, m.args)
                    end with
                else
                    r = dispatch(ts, m.name, m.args)
                end if
                send(back, { kind: "tool_result", run_id: m.run_id,
                             call_id: m.call_id, result: r })
            end if
        end while
        return nothing
    end function

    ' Wrap already-spawned worker handles. `next` is round-robin position.
    function pool(handles)
        if not is_array(handles) then
            error "tools: pool expects an array of actor handles from spawn"
        end if
        if count(handles) = 0 then
            error "tools: a pool needs at least one worker"
        end if
        for each h in handles
            if reflect.kind(h) != "actor" then
                error "tools: pool expects actor handles; got a " + reflect.kind(h)
            end if
        end for
        return { workers: handles, next: 0 }
    end function

    ' One call to send. `who` may be `nothing` when nobody is named.
    function request(who, run_id, call_id, name, args)
        return { kind: "call", principal: who, run_id: run_id,
                 call_id: call_id, name: name, args: args }
    end function

    ' Send to the next worker and RETURN THE ADVANCED POOL. A gBASIC record is
    ' a value, so a pool mutated inside this function would be mutated in a
    ' copy -- the same reason `accounting.post` returns the new ledger.
    function send_call(p, req)
        i = p.next
        send(p.workers[i], req)
        n = i + 1
        if n >= count(p.workers) then
            n = 0
        end if
        return { workers: p.workers, next: n }
    end function

    ' Is this inbox message a reply from a worker?
    function is_result(m)
        if not is_record(m) then return false
        if not has(m, "kind") then return false
        return m.kind = "tool_result"
    end function

    ' Named `shutdown` rather than `stop`: `stop` is a reserved word in gBASIC
    ' and cannot be a function name. (The MESSAGE is still "stop" -- a string
    ' is not a keyword.)
    function shutdown(p)
        for each w in p.workers
            send(w, { kind: "stop" })
        end for
        return nothing
    end function

end library

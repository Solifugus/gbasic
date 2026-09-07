' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' agent -- a conversation in progress is a VALUE, and the loop is a pure step.
'
' docs/gbasic_ai_reference_and_primitives.md §1.8, step 5. The naive design is
' a loop: send the transcript, take the reply, dispatch any tool call, re-send,
' until done. It cannot be written that way, and the reason is the approval
' gate: when the model asks to do something that changes the world, a person
' has to say yes, they may take a minute, the wait spans HTTP requests, and the
' server is single-threaded and watcher-driven. Nothing may block.
'
' So the conversation is a RECORD and `step` is a pure function of (run, event)
' returning the new run plus the ACTIONS its caller must perform. The caller
' does the I/O and feeds results back as events. Between two events the run can
' be written to a database and read back, which is what makes an approval that
' arrives ten minutes later on a different request possible at all.
'
' WHAT THE RUN MAY CONTAIN IS DECIDED BY `encode`, and two things follow that
' the design did not say:
'
'   - THE TOOLSET IS NOT IN THE RUN. A toolset holds function values and
'     `encode` refuses them, so the run keeps `tools.schema(ts)` -- ordinary
'     records, and they carry `mutates`, which is exactly what the approval
'     decision needs. The caller keeps the toolset for dispatch. This is the
'     same rule §1.8 already states for the live model handle, arrived at from
'     the same place: a run that cannot be stored is not a run.
'   - `expires_at` IS A NUMBER. `encode` refuses a datetime too, so the run
'     carries epoch seconds and `agent.is_expired` compares them.
library agent

    ' Its own dependencies, loaded here rather than left to the caller: a
    ' program that says `load agent` gets a working agent. Written without
    ' these first, and every fixture passed -- because each one also loaded
    ' `llm` and `tools` for its own use, so nothing could tell the difference.
    ' tests/agent_test.bas now checks `load agent` ALONE.
    load llm from "llm.bas"
    load tools from "tools.bas"

    ' ---- stages -----------------------------------------------------------
    ' idle -> calling_model -> (awaiting_tool | awaiting_approval)* -> done

    function _stages()
        return ["idle", "calling_model", "awaiting_tool", "awaiting_approval", "done"]
    end function

    ' ---- events (constructed, so a malformed one is refused where it is
    ' written rather than three stages later) --------------------------------

    function user_event(text)
        if not is_string(text) then
            error "agent: a user event needs text"
        end if
        return { kind: "user", text: text }
    end function

    ' `message` is a canonical assistant message (llm.message shape).
    function model_event(msg, usage)
        if not is_record(msg) then
            error "agent: a model event needs a canonical assistant message"
        end if
        if not has(msg, "parts") then
            error "agent: a model event's message needs parts (see llm.message)"
        end if
        return { kind: "model", message: msg, usage: usage }
    end function

    function tool_result_event(call_id, result)
        if not is_string(call_id) then
            error "agent: a tool result needs the call id it answers"
        end if
        if not is_record(result) then
            error "agent: a tool result needs the result part from tools.dispatch"
        end if
        return { kind: "tool_result", call_id: call_id, result: result }
    end function

    function approval_event(call_id, granted)
        if not is_string(call_id) then
            error "agent: an approval needs the call id it answers"
        end if
        if not is_boolean(granted) then
            error "agent: an approval must be granted true or false"
        end if
        return { kind: "approval", call_id: call_id, granted: granted }
    end function

    function cancel_event(why)
        return { kind: "cancel", why: why }
    end function

    ' ---- the run ----------------------------------------------------------

    ' agent.begin(ctx, system, toolset) -> run
    '
    ' Named `begin` because `new` is a keyword and cannot be a function name --
    ' the same reason chart has `chart.spec`.
    function begin(ctx, system, toolset)
        if not is_record(ctx) then
            error "agent: begin expects a context record (who this run is for)"
        end if
        if not is_string(system) then
            error "agent: begin expects a system prompt string"
        end if
        published = []
        if is_record(toolset) then
            published = tools.schema(toolset)
        end if
        return {
            id: hex_encode(random_bytes(16)),
            ctx: ctx,
            system: system,
            tools: published,
            transcript: [],
            step: 0,
            stage: "idle",
            pending: [],
            results: [],
            usage: { input: 0, output: 0 },
            expires_at: epoch(now()) + 3600
        }
    end function

    ' Epoch seconds, so this is a comparison and not a clock read -- `step` and
    ' everything near it stays pure, and a test can name the moment.
    function is_expired(run, now_seconds)
        if not is_number(now_seconds) then
            error "agent: is_expired expects the current time in epoch seconds"
        end if
        return now_seconds > run.expires_at
    end function

    ' A mutating call needs a person to say yes. The default is TRUE because
    ' the safe default is the one you have to turn off deliberately; a context
    ' saying `approve_mutations: false` turns it off for that run.
    function _needs_approval(run, name)
        gate = true
        if has(run.ctx, "approve_mutations") then
            gate = run.ctx.approve_mutations
        end if
        if not gate then
            return false
        end if
        for each t in run.tools
            if t.name = name then
                return count(t.mutates) > 0
            end if
        end for
        ' A call naming no declared tool is not approvable -- it is a mistake,
        ' and dispatch will report it as one.
        return false
    end function

    ' The key a mutating tool uses to recognise its own earlier effect after a
    ' restart. CONCATENATED rather than hashed: both halves are already opaque
    ' ids, so a hash would add no uniqueness and would cost the reader the
    ' ability to see which run and which call an effect belongs to.
    function idempotency_key(run, call_id)
        return run.id + ":" + call_id
    end function

    ' ---- the step ---------------------------------------------------------

    function _act(kind, body)
        out = {}
        out["kind"] = kind
        for each k in keys(body)
            out[k] = body[k]
        end for
        return out
    end function

    function _start_model(run)
        return _act("start_model", { system: run.system, transcript: run.transcript,
                                     tools: run.tools })
    end function

    ' Every tool call in an assistant turn, in order.
    function _calls_in(msg)
        out = []
        for each p in msg.parts
            if p.kind = "tool_call" then
                append(out, p)
            end if
        end for
        return out
    end function

    function _text_in(msg)
        s = ""
        for each p in msg.parts
            if p.kind = "text" then
                s = s + p.text
            end if
        end for
        return s
    end function

    function _without(list, call_id)
        out = []
        for each p in list
            if p.call_id != call_id then
                append(out, p)
            end if
        end for
        return out
    end function

    function _find_pending(run, call_id)
        for each p in run.pending
            if p.call_id = call_id then
                return p
            end if
        end for
        return unknown
    end function

    ' When nothing is outstanding, the collected results become one user turn
    ' and the model is asked again.
    function _resume_if_ready(run, actions)
        if count(run.pending) > 0 then
            return { run: run, actions: actions }
        end if
        if count(run.results) = 0 then
            return { run: run, actions: actions }
        end if
        parts = []
        for each r in run.results
            append(parts, llm.tool_result(r.call_id, r.content, r.is_error))
        end for
        run.transcript = append(run.transcript, llm.message("user", parts))
        run.results = []
        run.stage = "calling_model"
        append(actions, _start_model(run))
        return { run: run, actions: actions }
    end function

    ' agent.apply(run, event) -> { run, actions }
    '
    ' Named `apply` for two reasons. `step` is a KEYWORD -- `for i = 1 to 10
    ' step 2` -- so it cannot be a function name, the same wall `new` and `stop`
    ' put up elsewhere in this stdlib. And `apply` is what this library already
    ' calls folding an event into a state: `lending.apply` and `credit.apply`
    ' are the same shape over a loan and a portfolio.
    '
    ' PURE. It performs no I/O, reads no clock and generates no id; everything
    ' it needs is in the run or the event. That is what lets the caller store
    ' the run between two HTTP requests and step it again on a different
    ' process -- and it is why the same (run, event) always gives the same
    ' answer, which tests/run_agent.sh asserts directly.
    function apply(run, event)
        if not is_record(event) then
            error "agent: step expects an event record"
        end if
        if not has(event, "kind") then
            error "agent: an event needs a kind"
        end if
        actions = []
        run.step = run.step + 1

        if event.kind = "cancel" then
            run.stage = "done"
            run.pending = []
            append(actions, _act("finish", { reason: "cancelled" }))
            return { run: run, actions: actions }
        end if

        if run.stage = "done" then
            error "agent: this run is finished; start another"
        end if

        if event.kind = "user" then
            run.transcript = append(run.transcript,
                                    llm.message("user", [ llm.text(event.text) ]))
            run.stage = "calling_model"
            append(actions, _start_model(run))
            return { run: run, actions: actions }
        end if

        if event.kind = "model" then
            if run.stage != "calling_model" then
                error "agent: a model reply arrived while the run was " + run.stage
            end if
            msg = event.message
            run.transcript = append(run.transcript, msg)
            if is_record(event.usage) then
                u = run.usage
                if has(event.usage, "input") then
                    u.input = u.input + event.usage.input
                end if
                if has(event.usage, "output") then
                    u.output = u.output + event.usage.output
                end if
                run.usage = u
            end if

            calls = _calls_in(msg)
            said = _text_in(msg)
            if said != "" then
                append(actions, _act("emit_text", { text: said }))
            end if
            if count(calls) = 0 then
                run.stage = "done"
                append(actions, _act("finish", { reason: "complete" }))
                return { run: run, actions: actions }
            end if

            wants_approval = false
            pend = []
            for each c in calls
                needs = _needs_approval(run, c.name)
                append(pend, { call_id: c.id, name: c.name, args: c.args,
                               approved: not needs })
                if needs then
                    wants_approval = true
                    append(actions, _act("ask_approval",
                        { call_id: c.id, name: c.name, args: c.args }))
                else
                    append(actions, _act("dispatch_tool",
                        { call_id: c.id, name: c.name, args: c.args,
                          idempotency: idempotency_key(run, c.id) }))
                end if
            end for
            run.pending = pend
            if wants_approval then
                run.stage = "awaiting_approval"
            else
                run.stage = "awaiting_tool"
            end if
            return { run: run, actions: actions }
        end if

        if event.kind = "approval" then
            p = _find_pending(run, event.call_id)
            if is_unknown(p) then
                error "agent: no call '" + event.call_id + "' is awaiting approval"
            end if
            if event.granted then
                append(actions, _act("dispatch_tool",
                    { call_id: p.call_id, name: p.name, args: p.args,
                      idempotency: idempotency_key(run, p.call_id) }))
                marked = []
                for each q in run.pending
                    if q.call_id = p.call_id then
                        append(marked, { call_id: q.call_id, name: q.name,
                                         args: q.args, approved: true })
                    else
                        append(marked, q)
                    end if
                end for
                run.pending = marked
            else
                ' A refusal is an ANSWER, not an error: the model is told, in
                ' the transcript, that a person declined -- which is what lets
                ' it say so or propose something else.
                run.pending = _without(run.pending, p.call_id)
                run.results = append(run.results,
                    { call_id: p.call_id, content: "declined by " + _who(run),
                      is_error: true })
            end if
            still_waiting = false
            for each q in run.pending
                if not q.approved then
                    still_waiting = true
                end if
            end for
            if still_waiting then
                run.stage = "awaiting_approval"
            else
                if count(run.pending) > 0 then
                    run.stage = "awaiting_tool"
                end if
            end if
            return _resume_if_ready(run, actions)
        end if

        if event.kind = "tool_result" then
            p = _find_pending(run, event.call_id)
            if is_unknown(p) then
                error "agent: no call '" + event.call_id + "' is outstanding"
            end if
            run.pending = _without(run.pending, event.call_id)
            content = event.result.content
            failed = false
            if has(event.result, "is_error") then
                failed = event.result.is_error
            end if
            run.results = append(run.results,
                { call_id: event.call_id, content: content, is_error: failed })
            return _resume_if_ready(run, actions)
        end if

        error "agent: unknown event kind '" + string(event.kind) + "'"
    end function

    function _who(run)
        if has(run.ctx, "user") then
            return string(run.ctx.user)
        end if
        return "the operator"
    end function

end library

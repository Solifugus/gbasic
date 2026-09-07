' Steward -- the reference application's first slice, and the SKELETON the
' proposal's Part 3 item 13 asked for before more phase documents were written:
' one route, one model call answered from a replay fixture, one tool through the
' pool, and the approval gate.
'
' docs/gbasic_ai_reference_and_primitives.md step 8. It is deliberately small,
' and its job is to find what the layers get wrong WHEN JOINED -- which is not
' what any of their own suites can tell you.
'
' THE SHAPE. A handler runs on the event loop's thread, so it may not block. It
' begins a run, asks `agent.apply` what to do, and SENDS the work to a worker.
' The worker's reply arrives as an `inbox.messages` event, which drives the next
' `agent.apply`, and so on until the run is done. Nothing waits.
'
' ONE POOL FOR BOTH KINDS OF WORK, which is the thing this skeleton settled: a
' model call is simply another thing that blocks, and the pool exists for things
' that block. It needs no `llm.start`/`poll`/`read` at all for the non-streaming
' case. `tools.serve` handles only tool calls, so the worker loop is written
' here -- over the SAME `tools.dispatch`, so the dispatching still has one
' definition even though the loop does not.
'
' RUNS ARE STORED AS TEXT. `encode(run)` goes into the store and `decode` comes
' back out on every step, so the property `agent` was designed for is exercised
' by the ordinary path rather than only in a unit test. A run that stopped
' surviving that would break here immediately.
load agent
load tools
load llm
load web
load webserver

' ---- the tools -------------------------------------------------------------

function get_balance(args)
    return "42.00"
end function

function freeze_card(args)
    ' A mutating tool receives its idempotency key, so a replay after a restart
    ' can find its own earlier effect rather than freezing the card twice.
    return "frozen " + args.id
end function

' ---- the worker: one loop, two kinds of work -------------------------------

function steward_worker(back, ts, replay_dir)
    m = llm.with_volatile(llm.anthropic("claude-test", "k"), ["system"])
    m = llm.replay(m, replay_dir)
    while true
        job = receive()
        if not is_record(job) then
            return nothing
        end if
        if job.kind = "stop" then
            return nothing
        end if
        if job.kind = "model" then
            ' The model call BLOCKS, and that is exactly why it is here.
            r = llm.chat(m, job.system, job.transcript)
            send(back, { kind: "model_reply", run_id: job.run_id,
                         message: llm.from_response(m, r),
                         usage: { input: 0, output: 0 } })
        else
            if is_record(job.principal) then
                with principal(job.principal)
                    part = tools.dispatch(ts, job.name, job.args)
                end with
            else
                part = tools.dispatch(ts, job.name, job.args)
            end if
            send(back, { kind: "tool_reply", run_id: job.run_id,
                         call_id: job.call_id, result: part })
        end if
    end while
    return nothing
end function

' ---- the routes ------------------------------------------------------------

server steward( port: 0 )

    post "/ask"( req )
        ' `has`, and then a length check. A MISSING header reads back as
        ' `unknown`, and `unknown = ""` is FALSE -- so the obvious guard
        ' `if req.headers["x-user"] = ""` lets an unauthenticated request
        ' through and creates a run for nobody. Written that way first, and the
        ' skeleton answered 202 to a request with no identity at all. This is
        ' the shape of a real authentication bypass, and no unit test in any of
        ' the libraries below could have shown it.
        if not has(req.headers, "x-user") then
            return { status: 401, body: "who are you?" }
        end if
        who = req.headers["x-user"]
        if not is_string(who) or len(who) = 0 then
            return { status: 401, body: "who are you?" }
        end if
        run = agent.begin({ user: who }, SYSTEM, TOOLSET)
        out = agent.apply(run, agent.user_event(req.body))
        _perform(out)
        return { status: 202, body: out.run.id }
    end post

    post "/approve/{id}"( req )
        ' `has`, not is_nothing: a missing record key reads back as `unknown`,
        ' and is_nothing() is FALSE for that -- which is how the first draft of
        ' this file fell through to decode() with an unknown in its hand.
        if not has(RUNS, req.params.id) then
            return { status: 404, body: "no such run" }
        end if
        run = decode(RUNS[req.params.id])
        granted = req.body = "yes"
        out = agent.apply(run, agent.approval_event(PENDING[req.params.id], granted))
        _perform(out)
        return { status: 202, body: "recorded" }
    end post

    get "/run/{id}"( req )
        if not has(RUNS, req.params.id) then
            return { status: 404, body: "no such run" }
        end if
        run = decode(RUNS[req.params.id])
        said = ""
        if has(SAID, req.params.id) then
            said = SAID[req.params.id]
        end if
        return { body: run.stage + "|" + said }
    end get

end server

' ---- performing what `agent.apply` asked for -------------------------------
'
' The whole of the I/O, in one place. `agent.apply` decided; this does. Nothing
' here blocks: every action either sends to a worker or writes a record.

function _perform(out)
    run = out.run
    RUNS[run.id] = encode(run)
    for each a in out.actions
        if a.kind = "start_model" then
            ' A function CANNOT rebind an outer scalar (no closures), so the
            ' pool lives in a record field and is mutated through it -- which is
            ' what the interpreter's own warning tells you to do. Written as
            ' `POOL = ...` first, and the round-robin then never advanced.
            STATE.pool = tools.send_call(STATE.pool, { kind: "model", run_id: run.id,
                                                       system: a.system, transcript: a.transcript })
        end if
        if a.kind = "dispatch_tool" then
            STATE.pool = tools.send_call(STATE.pool, { kind: "call", run_id: run.id,
                                                       call_id: a.call_id, name: a.name,
                                                       args: a.args, principal: run.ctx,
                                                       idempotency: a.idempotency })
        end if
        if a.kind = "ask_approval" then
            ' Parked. A person answers on another request, minutes later, and
            ' the run is text in RUNS until they do.
            PENDING[run.id] = a.call_id
            print to error "APPROVAL NEEDED " + run.id + " " + a.name
        end if
        if a.kind = "emit_text" then
            prior = ""
            if has(SAID, run.id) then
                prior = SAID[run.id]
            end if
            SAID[run.id] = prior + a.text
        end if
        if a.kind = "finish" then
            print to error "FINISHED " + run.id + " " + a.reason
        end if
    end for
    return nothing
end function

program main( args )
    SYSTEM = "You are a bank assistant."
    RUNS = {}
    SAID = {}
    PENDING = {}
    TOOLSET = tools.define("bank", [
        { name: "get_balance", describe: "Read an account balance.",
          params: [ { name: "id", type: "string", describe: "the account", required: true } ],
          reads: ["accounts"], mutates: [], fn: get_balance },
        { name: "freeze_card", describe: "Freeze a card.",
          params: [ { name: "id", type: "string", describe: "the card", required: true } ],
          reads: [], mutates: ["cards"], fn: freeze_card }
    ])
    me = self()
    handles = []
    for i = 1 to 2
        append(handles, spawn steward_worker(me, TOOLSET, env("STEWARD_REPLAY")))
    end for
    STATE = { pool: tools.pool(handles) }

    ' ---- replies from the pool drive the next step -------------------------
    '
    ' INSIDE the program block, and that is not a style choice: a top-level
    ' statement does not run when a `program` block exists, so a `watch` up
    ' there registers NOTHING and says nothing about it. `watch` cannot be
    ' hoisted the way `load` is either -- it FIRES on registration, so hoisting
    ' would run its body before anything it reads has been assigned.
    watch(inbox.messages)
        while count(inbox.messages) > 0
            msg = take_first(inbox.messages)
            if is_record(msg) and has(msg, "run_id") then
                if has(RUNS, msg.run_id) then
                    run = decode(RUNS[msg.run_id])
                    if msg.kind = "model_reply" then
                        _perform(agent.apply(run, agent.model_event(msg.message, msg.usage)))
                    end if
                    if msg.kind = "tool_reply" then
                        _perform(agent.apply(run, agent.tool_result_event(msg.call_id, msg.result)))
                    end if
                end if
            end if
        end while
    end watch

    cfg = web.configure(steward, { port: number(env("STEWARD_PORT")) })
    h = web.serve(cfg)
end program

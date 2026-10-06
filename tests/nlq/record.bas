' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Record llm replay fixtures for the NLQ benchmark. NOT part of the gate: this
' is the only thing that touches a live model, and it is run deliberately.
'
' WHY RECORDING EXISTS AT ALL. Measured on this machine, one question costs 24s
' warm and over 110s under load, so 16 questions is 10-30 minutes and the figure
' depends on what else is running. A gate cannot rest on that, and a gate that
' needs a working GPU driver is a gate that goes quiet when the driver breaks --
' which it had, silently, while this was being built.
'
' The transport seam is llm.with_transport: `fingerprint` and `canonical` ride
' along on the request, so the recorder saves each response under the name the
' replay path will look for. gBASIC has NO CLOSURES, so the output directory is
' a program global -- the documented idiom, not a workaround.

program main( args )
    ' AN INTENTION NOBODY ENFORCES IS NOT A GUARD. The header above has always
    ' said this is "run deliberately, with an API key, by somebody who means to"
    ' -- and on 2026-10-02 an instrumentation sweep that ran every `.bas` in the
    ' tree ran THIS, made a live paid call, and overwrote a committed recording.
    ' The file was restored from git; the lesson is that the fixture had no way
    ' to tell a deliberate run from an accidental one, and its own comment was
    ' the only thing standing between a glob and somebody's API bill.
    '
    ' So it is opt-in, the way every other expensive or destructive tier in this
    ' tree is (RUN_FINIO_COST, GBASIC_POSTGRES_TEST, NAP_FS_STRESS,
    ' LIBCURL_FLOOR_BUILD). Checked FIRST, before any load, any request or any
    ' write, so a refusal costs nothing and changes nothing.
    if is_unknown(env("GBASIC_RECORD_FIXTURES")) then
        print to error "refusing to record: this makes LIVE, PAID model calls and"
        print to error "OVERWRITES committed fixtures. Set GBASIC_RECORD_FIXTURES=1"
        print to error "if that is what you want."
        exit(2)
    end if
    load nlq
    load llm
    load webclient

    G = { dir: "tests/nlq/replay", n: 0, saved: 0 }
    fixture = "tests/nlq/estate_demo_v.json"
    if count(args) > 0 then
        fixture = args[0]
    end if
    if count(args) > 1 then
        G.dir = args[1]
    end if

    f {file}= fixture
    c = decode(read(f))
    cat = { tables: c.tables, columns: c.columns }
    vocab = nlq.vocabulary(c.values, {})
    syns = { report: [ "rpt" ], general: [ "gl" ], ledger: [ "gl" ],
             staged: [ "stg" ], counterparty: [ "ctp" ] }

    ' `qwen3-nlq` IS `qwen3:4b` WITH num_ctx STATED AT 8192, created with
    ' ollama create. The design says num_ctx is a deployment decision to be
    ' stated rather than a default to inherit, and this is that decision made:
    ' the stock model is served at 4096, of which a THINKING model spends most
    ' on reasoning before it answers -- measured, the first question truncated
    ' at max_tokens 3000 and returned empty content. The model NAME carries the
    ' choice, so a fixture recorded against it cannot be confused with one
    ' recorded against the stock server.
    m = llm.local("http://127.0.0.1:11434", "qwen3-nlq")
    m.timeout = 1800
    m.temperature = 0
    ' A THINKING MODEL SPENDS ITS OUTPUT BUDGET BEFORE IT ANSWERS. llm defaults
    ' to max_tokens 1024 and qwen3 exceeded it on the FIRST question: the whole
    ' budget went into `reasoning`, `content` came back EMPTY and finish_reason
    ' was `length`. The recorder saved it anyway, which would have produced
    ' sixteen empty fixtures and a replay gate that tested nothing.
    m.max_tokens = 3000
    m = llm.with_transport(m, record_transport)

    for each q in c.questions
        g = nlq.ground(cat, q.text, { limit: 6, synonyms: syns })
        on error goto next
        p = nlq.prompt(g, cat, vocab, q.text, { budget_tokens: 3000 })
        if error then
            print ("SKIP " + q.id + ": " + error.message)
            error.clear()
        else
            G.n = G.n + 1
            r = llm.ask(m, p.system, p.user)
            if error then
                print ("ERROR " + q.id + ": " + error.message)
                error.clear()
            else
                print (q.id + " (" + string(p.estimated_tokens) + " tok) -> " + replace(string(r), chr(10), " "))
            end if
        end if
        on error stop
    end for
    print ("RECORDED " + string(G.saved) + " of " + string(G.n))
end program

' The live transport, with a copy kept. Returns exactly what webclient returned,
' so recording cannot change what the caller sees -- a recorder that altered the
' response would make the fixtures describe a different program.
function record_transport(m, req)
    resp = webclient.request({ method: "POST", url: req.url, headers: req.headers,
                               body: req.body, timeout: 1800 })
    if resp.status = 200 then
        ' REFUSE TO RECORD A FIXTURE THAT SAYS NOTHING. A 200 with empty content
        ' is what a truncated thinking model returns, and a fixture built from
        ' it is indistinguishable from a working one until the gate is asserting
        ' against silence. Checked here rather than by the caller, because this
        ' is the only place that sees the raw body.
        parsed = decode(resp.body)
        said = ""
        why = ""
        if has(parsed, "choices") and count(parsed.choices) > 0 then
            ch = parsed.choices[0]
            if has(ch, "message") and has(ch.message, "content") then
                said = string(ch.message.content)
            end if
            if has(ch, "finish_reason") then
                why = string(ch.finish_reason)
            end if
        end if
        if len(trim(said)) = 0 then
            print to error ("NOT RECORDED (empty content, finish_reason=" + why + "): " + string(req.fingerprint))
        else
            out {file}= G.dir + "/" + string(req.fingerprint) + "-" + string(req.attempt) + ".json"
            write(out, json_encode({ request: req.canonical, status: resp.status, body: parsed }))
            G.saved = G.saved + 1
        end if
    end if
    return resp
end function

' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Record llm replay fixtures for the ari_discover advisor. NOT part of the gate:
' this is the only thing here that touches a live model, and it is run
' deliberately, with an API key, by somebody who means to.
'
' WHY RECORD RATHER THAN CALL. A gate that needs credentials is a gate that goes
' quiet the day the key expires, and one that needs the network is a gate that
' fails for reasons that are not about gBASIC. Recording also fixes the ANSWER:
' a model asked the same question twice does not give the same words, so a live
' gate could only ever assert something vague, and vague is how a check stops
' discriminating.
'
' gBASIC has NO CLOSURES, so the output directory is a program global -- the
' documented idiom, not a workaround.

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
    load ari_discover
    load ari_advisor
    load llm
    load webclient

    G = { dir: "tests/ari_advisor/replay", saved: 0 }
    model = "gpt-4o-mini"
    if count(args) > 0 then
        model = args[0]
    end if

    srcs = []
    i = 1
    while i <= 8
        f {file}= "examples/fixtures/ari_discover/0" + string(i) + "_branch_activity.rpt"
        append(srcs, { id: string(i), text: read(f) })
        i = i + 1
    end while
    p = ari_discover.infer(srcs)

    ' TEMPERATURE 0 so a re-record is as close to reproducible as the provider
    ' allows. It is not a guarantee -- the fixture is the guarantee -- but a
    ' re-record that changes every name for no reason makes a rebaseline
    ' impossible to review.
    m = llm.openai(model, unknown)
    m.temperature = 0
    m = llm.with_transport(m, record_transport)

    r = ari_advisor.advise_names(p, m)
    print "answered=" + string(r.advisor.answered) + " suggestions=" + string(r.advisor.suggestions)
    print "saved " + string(G.saved) + " fixture(s) into " + G.dir
end program

function record_transport(m, req)
    resp = webclient.request({ method: "POST", url: req.url, headers: req.headers,
                               body: req.body, timeout: 300 })
    if resp.status = 200 then
        parsed = decode(resp.body)
        said = ""
        if has(parsed, "choices") and count(parsed.choices) > 0 then
            ch = parsed.choices[0]
            if has(ch, "message") and has(ch.message, "content") then
                said = string(ch.message.content)
            end if
        end if
        ' REFUSE TO RECORD SILENCE. A 200 carrying empty content is a fixture
        ' indistinguishable from a working one until the gate is asserting
        ' against nothing -- the defect the nlq recorder shipped once.
        if len(trim(said)) = 0 then
            print to error "NOT RECORDED (empty content): " + string(req.fingerprint)
        else
            out {file}= G.dir + "/" + string(req.fingerprint) + "-" + string(req.attempt) + ".json"
            write(out, json_encode({ request: req.canonical, status: resp.status, body: parsed }))
            G.saved = G.saved + 1
        end if
    end if
    return resp
end function

' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' R3 and R4 over the RECORDED fixtures: the SQL a real model actually wrote,
' replayed offline. No network, no GPU, no database.
'
' WHAT THIS CAN AND CANNOT SAY. It checks what the query NAMES and what it
' WOULD DO -- not whether its answer is right, which needs the estate and is the
' next increment. That split is deliberate: R3 and R4 are decidable from the
' text and belong in a gate; correctness is a value comparison and belongs with
' a database.

program main( args )
    load nlq
    load llm

    fixture = "tests/nlq/estate_demo_replay.json"
    dir = "tests/nlq/replay"
    f {file}= fixture
    c = decode(read(f))
    ' estateforge's exporter calls it `objects` (it carries views too); an older
    ' hand-rolled fixture called it `tables`. Accept both, since the recorded
    ' replay fixtures were keyed against prompts built from the older one.
    objs = c.tables
    if has(c, "objects") then
        objs = c.objects
    end if
    cat = { tables: objs, columns: c.columns }
    vocab = nlq.vocabulary(c.values, {})
    syns = { report: [ "rpt" ], general: [ "gl" ], ledger: [ "gl" ],
             staged: [ "stg" ], counterparty: [ "ctp" ] }

    m = llm.local("http://127.0.0.1:11434", "qwen3-nlq")
    m.max_tokens = 3000
    m.temperature = 0
    m = llm.replay(m, dir)

    n = 0
    replayed = 0
    clean = 0
    for each q in c.questions
        g = nlq.ground(cat, q.text, { limit: 6, synonyms: syns })
        on error goto next
        p = nlq.prompt(g, cat, vocab, q.text, { budget_tokens: 3000 })
        if error then
            error.clear()
        else
            n = n + 1
            sql = llm.ask(m, p.system, p.user)
            if error then
                ' No fixture: the three questions whose reasoning exhausted the
                ' output budget were never recorded, and that is an OUTCOME.
                print ("NOFIXTURE " + q.id)
                error.clear()
            else
                replayed = replayed + 1
                probs = nlq.check_sql(string(sql), g, {})
                if count(probs) = 0 then
                    clean = clean + 1
                    print ("ok    " + q.id)
                else
                    line = ""
                    for each x in probs
                        line = line + x.kind + "(" + x.detail + ") "
                    end for
                    print ("FLAG  " + q.id + ": " + line)
                end if
            end if
        end if
        on error stop
    end for
    print ""
    print ("REPLAYED " + string(replayed) + " of " + string(n))
    print ("CLEAN " + string(clean) + " of " + string(replayed))
end program

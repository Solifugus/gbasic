' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' THE VALUE TIER: run the SQL a model wrote against the real estate and compare
' the NUMBER to the answer estateforge computed from the plan and the rows.
'
' WHY THE VALUE AND NOT THE SQL. estateforge_design §6 settles it and this
' adopts it unchanged: many different correct queries answer one question, so
' text comparison rejects good SQL, and a query textually close to the reference
' can be semantically wrong. The answer is a number and the number is checkable.
'
' THE ANSWER KEY IS NOT OURS AND IS NOT THE REFERENCE SQL. estateforge folds it
' over the generated rows directly, so comparing to it is a second
' implementation rather than a second call into the thing under test. The plan
' is deterministic (demo_plan, seed 20260912), which is what makes the key match
' the database -- a different plan, scale or seed gives different rows and
' therefore different answers, WITH NO ERROR TO SAY SO.
'
' WHAT A DISAGREEMENT MEANS. Not that the model failed: it may have answered a
' DIFFERENT, REASONABLE question. `warehouse.stg_deal` and `trading.deal` both
' hold deal_status and the staging copy is filtered on load, so a count taken
' from it is a real number about a real table and not the one asked for. That is
' R2, and it is exactly what a value comparison sees and a query inspection
' cannot.

program main( args )
    load nlq
    load llm
    load odbc

    fixture = "tests/nlq/estate_demo_v.json"
    f {file}= fixture
    c = decode(read(f))
    cat = { tables: c.tables, columns: c.columns }
    vocab = nlq.vocabulary(c.values, {})
    syns = { report: [ "rpt" ], general: [ "gl" ], ledger: [ "gl" ],
             staged: [ "stg" ], counterparty: [ "ctp" ] }

    m = llm.local("http://127.0.0.1:11434", "qwen3-nlq")
    m.max_tokens = 3000
    m.temperature = 0
    m = llm.replay(m, "tests/nlq/replay")

    conn = odbc.connect(env("NLQ_ODBC_CONNECTION"))

    ' SQLITE HAS NO SCHEMAS, AND ATTACH GIVES IT ONE PER DATABASE FILE -- so
    ' `warehouse.fact_volume` resolves there exactly as it does on PostgreSQL
    ' and THE SAME RECORDED SQL RUNS UNCHANGED. That is what makes the two
    ' engines comparable at all: not a re-recording against a second dialect,
    ' which would vary the query as well as the database, but one query and two
    ' places to run it.
    attach = default(env("NLQ_ODBC_ATTACH"), "")
    if len(attach) > 0 then
        for each pair in split(attach, ";")
            if len(trim(pair)) > 0 then
                bits = split(pair, "=")
                a = odbc.exec(conn, "attach '" + bits[1] + "' as " + bits[0])
            end if
        end for
    end if

    scored = 0
    agreed = 0
    failed = 0
    for each q in c.questions
        g = nlq.ground(cat, q.text, { limit: 6, synonyms: syns })
        on error goto next
        p = nlq.prompt(g, cat, vocab, q.text, { budget_tokens: 3000 })
        if error then
            error.clear()
        else
            sql = llm.ask(m, p.system, p.user)
            if error then
                error.clear()
            else
                probs = nlq.check_sql(string(sql), g, {})
                if count(probs) > 0 then
                    print ("REFUSED " + q.id + " (R3/R4)")
                else
                    rows = odbc.query(conn, _one_statement(string(sql)))
                    if error then
                        failed = failed + 1
                        print ("SQLERROR " + q.id + ": " + replace(error.message, chr(10), " "))
                        error.clear()
                    else
                        scored = scored + 1
                        got = _first_value(rows)
                        if _same(got, q.answer) then
                            agreed = agreed + 1
                            print ("AGREE " + q.id + " = " + string(got))
                        else
                            print ("DIFFER " + q.id + ": model " + string(got) + ", key " + string(q.answer) + " [" + q.exercises + "]")
                        end if
                    end if
                end if
            end if
        end if
        on error stop
    end for
    x = odbc.close(conn)
    print ""
    print ("SCORED " + string(scored) + " AGREED " + string(agreed) + " SQLERROR " + string(failed))
end program

' The model sometimes ends with a semicolon; odbc wants one statement.
function _one_statement(sql)
    s = trim(sql)
    if ends_with(s, ";") then
        s = left(s, len(s) - 1)
    end if
    return s
end function

' The first column of the first row, whatever it is called -- the model names
' its output column freely and pinning a name would make this a text test.
function _first_value(rows)
    if count(rows) = 0 then
        return unknown
    end if
    for each k in keys(rows[0])
        return rows[0][k]
    end for
    return unknown
end function

' Money and counts arrive as text from the exactness rule, so compare as
' NUMBERS with a tolerance -- an exact string match would fail on 1922869.21
' against 1922869.2100.
function _same(got, want)
    if is_unknown(got) then
        return false
    end if
    a = number(string(got))
    b = number(string(want))
    d = a - b
    if d < 0 then
        d = 0 - d
    end if
    return d < 0.01
end function

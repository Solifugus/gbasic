' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Score grounding against an estateforge fixture in ITS OWN export shape.
'
' THREE KINDS OF QUESTION, AND NO FIXED DISPOSITION SCORES WELL -- which is the
' whole point of the set. An ordinary question must be answered; an AMBIGUOUS
' one has two defensible answers and a tool is scored on whether it NOTICED,
' not on which it picked; an UNANSWERABLE one must be declined. Declining an
' answerable question is wrong and answering an unanswerable one is wrong, so a
' tool that always declines and a tool that never declines both fail.
'
' PARTIAL IS REPORTED SEPARATELY AND NEVER FOLDED INTO RIGHT. "Answered
' defensibly without noticing" and "got it right" are different results, and a
' benchmark that adds them together cannot tell a careful tool from a lucky one.
'
' WHAT IS SCORED HERE IS RETRIEVAL AND DISCLOSURE, not the value -- no model and
' no database. For an ambiguous question that means: did the grounding surface
' BOTH objects the key names, and did it DISCLOSE the derivation between them?
' That is exactly what R2's `alternatives` exists to say, so this is the first
' thing that scores it.

program main( args )
    load nlq
    f {file}= args[0]
    c = decode(read(f))
    cat = { tables: c.objects, columns: c.columns }
    vrows = []
    for each v in c.values
        for each one in v.values
            append(vrows, { schema: v.schema, table: v.table, column: v.column, value: one })
        end for
    end for
    vocab = nlq.vocabulary(vrows, {})
    syns = { report: [ "rpt" ], general: [ "gl" ], ledger: [ "gl" ],
             staged: [ "stg" ], counterparty: [ "ctp" ] }
    lim = 8
    if count(args) > 1 then
        lim = number(args[1])
    end if

    right = 0
    partial = 0
    wrong = 0
    declined = 0
    unattempted = 0
    n = 0
    per = {}
    for each q in c.questions
        g = nlq.ground(cat, q.text, { limit: lim, synonyms: syns,
                                      derived_from: c.derivation })
        n = n + 1
        verdict = "wrong"

        if q.answer_kind = "unanswerable" then
            ' SCORED SEPARATELY AND NOT CREDITED, because NLQ does not detect
            ' this class and a pass here would be an accident. Both questions
            ' ask "which JOB reads X and what breaks if it is dropped" -- about
            ' jobs and dependencies, which a catalog does not model at all.
            ' MEASURED: `unresolved` does not separate them (f_ledger_sums_zero
            ' leaves 6 of 9 words unresolved and is perfectly answerable), and
            ' on the demo estate u_2 DOES get declined -- because the grounding
            ' happened to pull in an unrelated numbered-schema collision and R6
            ' fired on that. Crediting it would let an accident read as
            ' progress. Counted apart so a real fix shows up as a change.
            on error goto next
            ok2 = nlq.check_answerable(g)
            if error then
                declined = declined + 1
                error.clear()
            end if
            on error stop
            verdict = "unattempted"
        else
            if q.answer_kind = "ambiguous" then
                ' BOTH sides surfaced AND the ambiguity disclosed -> right.
                ' One side only -> partial: defensible, but it did not notice.
                vias = []
                for each side in q.answers
                    append(vias, side.via)
                end for
                have = 0
                for each via in vias
                    if contains(g.tables, via) then
                        have = have + 1
                    end if
                end for
                disclosed = count(g.alternatives) > 0
                if have = count(vias) and disclosed then
                    verdict = "right"
                else
                    if have > 0 then
                        verdict = "partial"
                    end if
                end if
            else
                hit = true
                for each t in q.touches
                    if not contains(g.tables, t) then
                        hit = false
                    end if
                end for
                ' AND IT MUST NOT DECLINE AN ANSWERABLE QUESTION. Without this
                ' a tool that refused everything would score on recall alone.
                on error goto next
                ok3 = nlq.check_answerable(g)
                if error then
                    hit = false
                    error.clear()
                end if
                on error stop
                if hit then
                    verdict = "right"
                end if
            end if
        end if

        if verdict = "unattempted" then
            unattempted = unattempted + 1
        else
        if verdict = "right" then
            right = right + 1
        else
            if verdict = "partial" then
                partial = partial + 1
            else
                wrong = wrong + 1
                print ("WRONG " + q.id + " [" + q.exercises + "/" + q.answer_kind + "] " + join(q.touches, "+"))
            end if
        end if
        end if
        cap = q.exercises
        if is_unknown(per[cap]) then
            per[cap] = { n: 0, ok: 0 }
        end if
        r = per[cap]
        r.n = r.n + 1
        if verdict = "right" then
            r.ok = r.ok + 1
        end if
        per[cap] = r
    end for
    print ("RIGHT " + string(right) + " PARTIAL " + string(partial) + " WRONG " + string(wrong) +
           " OF " + string(n - unattempted) + " attempted at limit " + string(lim))
    print ("UNATTEMPTED " + string(unattempted) + " unanswerable (declined " + string(declined) +
           ", INCIDENTALLY -- not detected from a catalog)")
    for each cap in sort(keys(per))
        print ("CAP " + cap + " " + string(per[cap].ok) + "/" + string(per[cap].n))
    end for
end program

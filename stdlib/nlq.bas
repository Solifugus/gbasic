' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' nlq -- a question, over an estate nobody can hold in their head.
' See docs/nlq_design.md. THIS IS THE FIRST INCREMENT: retrieval and grounding,
' with NO MODEL AND NO SQL.
'
' WHY THIS PART FIRST. discovery_design §2 already says schema retrieval must
' precede prompting, because 500 tables do not fit in a context window. That
' makes retrieval a SEARCH, and Recipe 1's finding applies unchanged: a search
' ALWAYS RETURNS A WINNER. Hand a model twenty tables chosen badly and it writes
' flawless SQL about the wrong ones -- and the SQL RUNS, because the tables it
' was given are real. The number that comes back has the right units and the
' right order of magnitude and nobody downstream can tell.
'
' So grounding carries its own SEARCH WIDTH and its NEAR MISSES, the rule
' `reasoning.finding` already imposes one domain over. "The answer is
' trading.deal" and "the answer is trading.deal, and trading.deal_archive scored
' one point behind it" are different claims, and only the second lets a reader
' see that a search had a close call.

library nlq

    ' Words that carry no schema meaning. `total`, `gross` and `net` are
    ' DELIBERATELY ABSENT: they are half the measure names in a warehouse.
    function stopwords()
        return [ "how", "many", "much", "what", "which", "who", "when", "where",
                 "is", "are", "was", "were", "be", "been", "has", "have", "had",
                 "do", "does", "did", "the", "a", "an", "of", "in", "on", "at",
                 "to", "for", "from", "by", "with", "and", "or", "that", "this",
                 "there", "their", "its", "it", "we", "our", "us", "you",
                 "all", "any", "each", "per", "across", "whole", "show", "me",
                 "list", "give", "get", "find", "tell", "count", "number" ]
    end function

    ' `deals` and `deal` are the same word to a schema. Crude on purpose: a
    ' stemmer is a SEARCH of its own and would need its own null model before
    ' anyone could say it helped.
    '
    ' IT ONLY HAS TO BE CONSISTENT, NOT CORRECT, because BOTH SIDES go through
    ' it: `status` becomes `statu`, which is not English -- and `deal_status`
    ' becomes `deal statu` too, so the question still reaches the column. What
    ' would break matching is applying it to one side only.
    function _singular(w)
        if len(w) > 3 and ends_with(w, "ies") then
            return left(w, len(w) - 3) + "y"
        end if
        if len(w) > 3 and ends_with(w, "ses") then
            return left(w, len(w) - 2)
        end if
        if len(w) > 3 and ends_with(w, "s") and not ends_with(w, "ss") then
            return left(w, len(w) - 1)
        end if
        return w
    end function

    function terms(text)
        cleaned = ""
        i = 0
        while i < len(text)
            ch = lower(mid(text, i, 1))
            if (ch >= "a" and ch <= "z") or (ch >= "0" and ch <= "9") then
                cleaned = cleaned + ch
            else
                cleaned = cleaned + " "
            end if
            i = i + 1
        end while
        stop_list = stopwords()
        out = []
        for each w in split(cleaned, " ")
            if len(w) > 1 and not contains(stop_list, w) then
                s = _singular(w)
                if not contains(out, s) then
                    append(out, s)
                end if
            end if
        end for
        return out
    end function

    ' An identifier is a bag of words: `gross_vol_mmbtu` is gross, vol, mmbtu.
    function _ident_terms(name)
        return terms(replace(name, "_", " "))
    end function

    ' --- declared synonyms, NOT inferred ones -------------------------------
    '
    ' MEASURED FIRST: lexical grounding alone recalls 9 of estateforge's 16
    ' questions, and every miss but one has the same cause -- `rpt_volume_gross`
    ' is not reached by the word "report" and `gl_entry` is not reached by
    ' "general ledger". Abbreviation, not ranking. The `unresolved` field is
    ' what says so, which is the first thing it has earned.
    '
    ' A synonym map is DECLARED BY WHOEVER OWNS THE ESTATE, never derived. That
    ' is the same line discovery draws between a declared fact and an inferred
    ' one: guessing that `rpt` means `report` is a search over the whole schema
    ' with no null model, and it would be right often enough to be trusted and
    ' wrong silently. `unresolved` tells an operator exactly which words to
    ' declare, so the loop is closed without anything being invented.
    function _expand(qterms, syn)
        if type(syn) != "record" then
            error "nlq.ground: `synonyms` is a record of term -> array of terms"
        end if
        out = []
        for each t in qterms
            if not contains(out, t) then
                append(out, t)
            end if
            alts = syn[t]
            if not is_unknown(alts) then
                if type(alts) != "array" then
                    error ("nlq.ground: synonyms['" + t + "'] must be an array of terms")
                end if
                for each a in alts
                    a2 = _singular(lower(a))
                    if not contains(out, a2) then
                        append(out, a2)
                    end if
                end for
            end if
        end for
        return out
    end function

    function check_catalog(cat)
        if type(cat) != "record" then
            error "nlq: a catalog is a record of { tables, columns }"
        end if
        for each field in [ "tables", "columns" ]
            if not has(cat, field) then
                error ("nlq: this catalog has no '" + field + "' -- `discovery.scan` produces one")
            end if
            if type(cat[field]) != "array" then
                error ("nlq: a catalog's '" + field + "' must be an array")
            end if
        end for
        return true
    end function

    function _id(schema, name)
        if len(string(schema)) = 0 then
            return name
        end if
        return schema + "." + name
    end function

    ' --- grounding ---------------------------------------------------------
    '
    ' A TABLE-NAME MATCH OUTWEIGHS A COLUMN-NAME MATCH, because a question names
    ' what it is about before it names what it wants from it -- and because
    ' every fact table in a warehouse carries the dimension words of every
    ' question, so column matches alone rank the widest table first regardless
    ' of the question.
    function ground(cat, question, options)
        ok = check_catalog(cat)
        opts = _options(options, [ "limit", "near_misses", "synonyms" ], "nlq.ground")
        limit = _default(opts, "limit", 8)
        near_n = _default(opts, "near_misses", 3)
        syn = _default(opts, "synonyms", {})
        asked = terms(question)
        qterms = _expand(asked, syn)
        if count(qterms) = 0 then
            error "nlq.ground: this question has no term a schema could match"
        end if

        ' index the columns by table, once
        by_table = {}
        for each c in cat.columns
            tid = _id(c.schema, c.table)
            if is_unknown(by_table[tid]) then
                by_table[tid] = []
            end if
            by_table[tid] = _append_to(by_table[tid], c)
        end for

        scored = []
        for each t in cat.tables
            tid = _id(t.schema, t.table)
            tname_terms = _ident_terms(t.table)
            sterms = _ident_terms(string(t.schema))
            hits = []
            score = 0
            for each qt in qterms
                if contains(tname_terms, qt) then
                    score = score + 3
                    append(hits, { term: qt, on: "table", what: tid })
                end if
                if contains(sterms, qt) then
                    score = score + 2
                    append(hits, { term: qt, on: "schema", what: string(t.schema) })
                end if
            end for
            cols = by_table[tid]
            matched_cols = []
            if not is_unknown(cols) then
                for each c in cols
                    cterms = _ident_terms(c.column)
                    for each qt in qterms
                        if contains(cterms, qt) then
                            score = score + 1
                            append(hits, { term: qt, on: "column", what: tid + "." + c.column })
                            if not contains(matched_cols, c.column) then
                                append(matched_cols, c.column)
                            end if
                        end if
                    end for
                end for
            end if
            if score > 0 then
                append(scored, { id: tid, score: score, why: hits, columns: matched_cols })
            end if
        end for

        ranked = _by_score(scored)
        selected = []
        near = []
        i = 0
        while i < count(ranked)
            if i < limit then
                append(selected, ranked[i])
            else
                if count(near) < near_n then
                    append(near, ranked[i])
                end if
            end if
            i = i + 1
        end while

        ' Which of the question's own words reached NOTHING. This is the half a
        ' ranked list cannot show: a term nothing matched is the most likely
        ' place the grounding is about to be confidently wrong.
        unresolved = []
        for each qt in _expand(asked, syn)
            seen = false
            for each s in scored
                for each h in s.why
                    if h.term = qt then
                        seen = true
                    end if
                end for
            end for
            if not seen then
                if contains(asked, qt) and not _resolved_via(qt, syn, scored) then
                    append(unresolved, qt)
                end if
            end if
        end for

        amb = _ambiguities(selected, ranked, limit, qterms, by_table)

        return { tables: _ids(selected),
                 ambiguous: amb,
                 detail: selected,
                 near_misses: _ids(near),
                 near_detail: near,
                 unresolved: unresolved,
                 terms: qterms,
                 search: { width: count(cat.tables),
                           matched: count(scored),
                           limit: limit,
                           cut: _cut_score(ranked, limit),
                           ' A row dropped with the SAME score as the last one kept was
                           ' dropped by the tie-break and not by the question. Reported
                           ' here rather than in `ambiguous`, because it does NOT block
                           ' an answer -- measured, it is true of 13 of 16 demo questions,
                           ' so gating on it would refuse almost everything while the
                           ' table actually needed was inside the cut all along.
                           cut_tied: _cut_tied(ranked, limit) } }
    end function

    ' Did any synonym DECLARED for this term reach something? If so the term is
    ' resolved, whatever word the hit was recorded under.
    function _resolved_via(qt, syn, scored)
        alts = syn[qt]
        if is_unknown(alts) then
            return false
        end if
        for each a in alts
            a2 = _singular(lower(a))
            for each sc in scored
                for each h in sc.why
                    if h.term = a2 then
                        return true
                    end if
                end for
            end for
        end for
        return false
    end function

    ' --- R1 and R6: what the catalog CANNOT settle ---------------------------
    '
    ' THE GROUNDING REPORTS, IT DOES NOT RESOLVE. Two ambiguities are visible
    ' from a catalog alone and both are live in estateforge's bank:
    '
    ' SAME NAME, DIFFERENT SCHEMA (R6). `staging.tmp_load_notes`,
    ' `staging_2.tmp_load_notes` and `staging_3.tmp_load_notes` are the planted
    ' NULL REGION, where the truth says any lineage reported is INVENTED.
    ' NOTHING IN THE CATALOG SAYS WHICH IS LIVE, so ranking one above the others
    ' is a guess wearing the clothes of a lookup. The same shape covers
    ' `trading.deal` against `archive.deal_2015`.
    '
    ' A TIE AT THE CUT. A row excluded with the SAME SCORE as the last row
    ' included was dropped by the tie-break, not by the question. That is an
    ' arbitrary boundary and a reader is entitled to see it.
    '
    ' A REFUSAL IS NOT A RAISE HERE, deliberately: a caller may want the
    ' grounding anyway -- to show a person the candidates and ask. The refusal
    ' belongs where an ANSWER would be produced, which is `check_answerable`.
    function _ambiguities(selected, ranked, limit, qterms, by_table)
        out = []
        ' same object name in different schemas, among the selected
        seen = []
        for each r in selected
            bare = _bare(r.id)
            if not contains(seen, bare) then
                append(seen, bare)
                same = []
                for each r2 in selected
                    if _bare(r2.id) = bare then
                        append(same, r2.id)
                    end if
                end for
                if count(same) > 1 and _indistinguishable(same) then
                    append(out, { kind: "same_name_different_schema",
                                  candidates: same,
                                  why: ("these schemas differ only by a number, so the catalog " +
                                        "does not say which is live; ranking one would be a guess") })
                end if
            end if
        end for
        return out
    end function

    ' MEASURED BEFORE THIS EXISTED: without it, 15 of estateforge's 16 demo
    ' questions carried an "ambiguity" and answering was refused for nearly all
    ' of them. A refusal that fires on everything is indistinguishable from
    ' having no tool -- the same failure the blind-shadow warning had at 287
    ' false positives before it was reverted.
    '
    ' THE DISCRIMINATOR IS WHETHER THE CATALOG OFFERS A DISTINCTION AT ALL.
    ' `trading.deal`, `trading_apac.deal` and `trading_emea.deal` are regional
    ' partitions and the schema names SAY SO -- apac and emea are words a
    ' question can use. `staging`, `staging_2` and `staging_3` differ by a
    ' NUMBER, which says nothing about which is live, and that is exactly what
    ' estateforge plants as the null region. So: ambiguous only when stripping
    ' the digits makes the schema names identical.
    function _indistinguishable(ids)
        base = ""
        for each id in ids
            parts = split(id, ".")
            stripped = _strip_digits(parts[0])
            if len(base) = 0 then
                base = stripped
            else
                if stripped != base then
                    return false
                end if
            end if
        end for
        return true
    end function

    function _strip_digits(name)
        out = ""
        i = 0
        while i < len(name)
            ch = mid(name, i, 1)
            if ch < "0" or ch > "9" then
                out = out + ch
            end if
            i = i + 1
        end while
        while len(out) > 1 and ends_with(out, "_")
            out = left(out, len(out) - 1)
        end while
        return out
    end function

    function _bare(id)
        parts = split(id, ".")
        return parts[count(parts) - 1]
    end function

    ' R1/R6 AS A REFUSAL, for the layer that produces an ANSWER. `ground`
    ' reports; something that is about to write SQL and hand back a number must
    ' not proceed on a grounding the catalog could not settle, because the
    ' number would be indistinguishable from a right one.
    function check_answerable(g)
        if type(g) != "record" or not has(g, "ambiguous") then
            error "nlq.check_answerable expects a grounding from nlq.ground"
        end if
        if count(g.ambiguous) > 0 then
            a = g.ambiguous[0]
            error ("nlq: this question cannot be answered from the catalog alone -- " +
                   a.kind + ": " + join(a.candidates, ", ") + " (" + a.why + ")")
        end if
        if count(g.tables) = 0 then
            error "nlq: nothing in this catalog matches the question"
        end if
        return true
    end function

    function _ids(rows)
        out = []
        for each r in rows
            append(out, r.id)
        end for
        return out
    end function

    ' The score of the LAST selected row -- what a near miss had to beat. A
    ' ranked list without it cannot say whether the cut was decisive or a
    ' coin toss.
    function _cut_tied(ranked, limit)
        if count(ranked) <= limit or limit <= 0 then
            return false
        end if
        return ranked[limit - 1].score = ranked[limit].score
    end function

    function _cut_score(ranked, limit)
        if count(ranked) = 0 then
            return 0
        end if
        last = limit - 1
        if last >= count(ranked) then
            last = count(ranked) - 1
        end if
        return ranked[last].score
    end function

    ' Insertion sort, descending by score then by id so the order is TOTAL: two
    ' tables on the same score must not rank by whichever the catalog listed
    ' first, or the answer depends on a driver's row order.
    function _by_score(rows)
        out = []
        for each r in rows
            placed = false
            i = 0
            next_out = []
            while i < count(out)
                if not placed then
                    if r.score > out[i].score or (r.score = out[i].score and r.id < out[i].id) then
                        append(next_out, r)
                        placed = true
                    end if
                end if
                append(next_out, out[i])
                i = i + 1
            end while
            if not placed then
                append(next_out, r)
            end if
            out = next_out
        end for
        return out
    end function

    function _append_to(arr, v)
        append(arr, v)
        return arr
    end function

    function _options(rec, known, label)
        if is_unknown(rec) or type(rec) = "nothing" then
            return {}
        end if
        if type(rec) != "record" then
            error (label + " expects an options record")
        end if
        for each k in keys(rec)
            if not contains(known, k) then
                error (label + ": unknown option '" + k + "' (known: " + join(known, ", ") + ")")
            end if
        end for
        return rec
    end function

    function _default(rec, field, fallback)
        if not has(rec, field) then
            return fallback
        end if
        return rec[field]
    end function

end library

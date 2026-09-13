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

    ' --- value vocabulary -----------------------------------------------------
    '
    ' MEASURED, AND IT IS WHY THIS EXISTS. The first real model call produced
    '     SELECT COUNT(*) FROM retail_banking.account WHERE account_status = 'open'
    ' against data that says 'OPEN'. The SQL is perfect -- right table, right
    ' column, right operator -- and it returns 0 with no error. SQL-text scoring
    ' rates it correct.
    '
    ' A CATALOG GIVES COLUMN NAMES AND NOT COLUMN VALUES, so the literal is a
    ' guess the model cannot avoid, and asking it to guess better is the wrong
    ' repair. For a LOW-CARDINALITY column the distinct values are IN THE
    ' DATABASE and can be read -- declared, certain and cheap, the class
    ' discovery's first increment is built from. A grounding that carries
    ' `account_status in (OPEN, CLOSED, FROZEN)` removes the guess instead of
    ' improving it.
    '
    ' A CEILING IS PART OF THE DEFINITION, not a tuning knob: a column with
    ' thousands of distinct values has no vocabulary worth carrying, and
    ' attaching one would blow a small model's context for no gain.
    function vocabulary(rows, options)
        opts = _options(options, [ "max_values" ], "nlq.vocabulary")
        cap = _default(opts, "max_values", 25)
        out = {}
        for each r in rows
            key = _id(r.schema, r.table) + "." + r.column
            if is_unknown(out[key]) then
                out[key] = []
            end if
            vals = out[key]
            v = string(r.value)
            if not contains(vals, v) and count(vals) <= cap then
                out[key] = _append_to(vals, v)
            end if
        end for
        ' A column that overflowed the cap has NO vocabulary rather than a
        ' truncated one: a partial list is worse than none, because a literal
        ' absent from it would be reported as unknown when it is merely unlisted.
        keep = {}
        for each k in keys(out)
            if count(out[k]) <= cap then
                keep[k] = out[k]
            end if
        end for
        return keep
    end function

    ' ONE sample value per column, for the columns `vocabulary` refuses because
    ' they have too many. The complement of a vocabulary rather than a fallback:
    ' a vocabulary says WHICH values exist, an exemplar says WHAT ONE LOOKS
    ' LIKE, and a question that turns on an identifier needs the second.
    '
    ' THE LONGEST VALUE IS CHOSEN, not the first, because a format is best shown
    ' by its fullest instance -- picking `'1'` from a column that also holds
    ' `'0000000001'` would teach exactly the wrong lesson.
    function exemplars(rows, options)
        opts = _options(options, [ "min_distinct" ], "nlq.exemplars")
        floor = _default(opts, "min_distinct", 2)
        best = {}
        seen = {}
        for each r in rows
            key = _id(r.schema, r.table) + "." + r.column
            v = string(r.value)
            if is_unknown(seen[key]) then
                seen[key] = []
            end if
            if not contains(seen[key], v) then
                seen[key] = _append_to(seen[key], v)
            end if
            if is_unknown(best[key]) or len(v) > len(string(best[key])) then
                best[key] = v
            end if
        end for
        out = {}
        for each k in keys(best)
            if count(seen[k]) >= floor then
                out[k] = best[k]
            end if
        end for
        return out
    end function

    ' Does the question name a literal this column cannot hold? The same shape
    ' as `unresolved`, one level down: reported, never corrected, because a
    ' catalog cannot know whether the person or the data is wrong.
    function check_literals(g, vocab, question)
        out = []
        qt = terms(question)
        for each tid in g.tables
            for each key in keys(vocab)
                if starts_with(key, tid + ".") then
                    known = vocab[key]
                    lowered = []
                    for each v in known
                        append(lowered, lower(v))
                    end for
                    for each t in qt
                        ' A question word that IS a value of this column, but in
                        ' the wrong case, is the measured failure exactly.
                        if contains(lowered, t) and not contains(known, t) then
                            append(out, { column: key, said: t,
                                          means: known[_index_of(lowered, t)],
                                          why: "the question's wording differs in case from the stored value" })
                        end if
                    end for
                end if
            end for
        end for
        return out
    end function

    function _index_of(arr, v)
        i = 0
        while i < count(arr)
            if arr[i] = v then
                return i
            end if
            i = i + 1
        end while
        return 0 - 1
    end function

    ' --- the prompt, and the budget it REFUSES to exceed -----------------------
    '
    ' MEASURED, AND IT IS WHY THE BUDGET IS A REFUSAL. Ollama serves qwen3:4b at
    ' a default num_ctx of 4096 while the model declares 262144, and a prompt
    ' over that is SILENTLY TRUNCATED -- prompt_eval_count pins at 4095 for 9KB,
    ' 28KB and 71KB alike, with nothing reported. Worse, it is the HEAD that
    ' goes: the question at the tail survives while the schema above it does
    ' not, so a model handed a large catalog answers with NO SCHEMA AT ALL and
    ' confidently invents table names that look exactly like the real ones.
    '
    ' A REFUSAL NAMES A BUDGET; A TRUNCATION NAMES NOTHING. So the builder
    ' measures what it is about to send and raises rather than letting the
    ' transport decide what to discard.
    '
    ' The estimate is deliberately crude -- characters over a declared ratio,
    ' measured at ~2.5 chars per token on this model. A tokenizer would be more
    ' accurate and is not worth a dependency: the budget is a SAFETY MARGIN, and
    ' being approximately right on the conservative side is the whole job.
    function prompt(g, cat, vocab, question, options)
        opts = _options(options, [ "budget_tokens", "chars_per_token", "dialect",
                                   "exemplars" ], "nlq.prompt")
        exemplars = _default(opts, "exemplars", {})
        budget = _default(opts, "budget_tokens", 3000)
        cpt = _default(opts, "chars_per_token", 2.5)
        dialect = _default(opts, "dialect", "standard SQL")

        by_table = {}
        for each c in cat.columns
            tid = _id(c.schema, c.table)
            if is_unknown(by_table[tid]) then
                by_table[tid] = []
            end if
            by_table[tid] = _append_to(by_table[tid], c.column)
        end for

        lines = []
        for each tid in g.tables
            cols = by_table[tid]
            if is_unknown(cols) then
                cols = []
            end if
            append(lines, tid + "(" + join(cols, ", ") + ")")
        end for

        vlines = []
        for each tid in g.tables
            for each key in keys(vocab)
                if starts_with(key, tid + ".") then
                    append(vlines, key + " in (" + join(vocab[key], ", ") + ")")
                end if
            end for
        end for

        ' EXEMPLARS, FOR THE COLUMNS A VOCABULARY CANNOT REACH. Measured: the
        ' question asked about "contract 1", the column stores '0000000001'
        ' zero-padded to ten characters, and the model wrote `contract_ref = 1`
        ' -- 0 rows where the answer is 4, silently, on SQLite. That is not a
        ' VALUE problem: contract_ref has hundreds of distinct values and
        ' enumerating them would blow the budget for no gain. It is a FORMAT
        ' problem, and ONE EXAMPLE fixes it -- '0000000001' says how to write a
        ' contract reference without listing another one.
        exlines = []
        for each tid in g.tables
            for each key in keys(exemplars)
                if starts_with(key, tid + ".") then
                    append(exlines, key + " looks like " + string(exemplars[key]))
                end if
            end for
        end for

        sys = ("You write one " + dialect + " SELECT statement that answers the question. " +
               "Use ONLY the tables and columns listed. Use the exact column values given. " +
               "Reply with SQL only: no markdown, no fence, no explanation.")
        user = "Tables:" + chr(10) + join(lines, chr(10))
        if count(vlines) > 0 then
            user = user + chr(10) + "Column values:" + chr(10) + join(vlines, chr(10))
        end if
        if count(exlines) > 0 then
            user = user + chr(10) + "Column formats:" + chr(10) + join(exlines, chr(10))
        end if
        user = user + chr(10) + chr(10) + "Question: " + question

        est = _estimate_tokens(len(sys) + len(user), cpt)
        if est > budget then
            error ("nlq.prompt: this prompt is about " + string(est) + " tokens against a budget of " +
                   string(budget) + " -- narrow the grounding (limit is " + string(g.search.limit) +
                   ", " + string(count(g.tables)) + " tables selected). Sending it would be truncated " +
                   "silently, and it is the SCHEMA that would be dropped, not the question")
        end if
        return { system: sys, user: user, estimated_tokens: est, budget: budget,
                 tables: g.tables }
    end function

    function _estimate_tokens(chars, cpt)
        n = chars / cpt
        i = 0
        while i < n
            i = i + 1
        end while
        return i
    end function

    ' --- R3 and R4: what the generated SQL may name, and may do ---------------
    '
    ' R4 IS STRUCTURAL, NOT A REQUEST. The system prompt asks for a SELECT; that
    ' is an instruction and instructions are not enforcement. A statement that
    ' writes is refused by inspection before anything executes it.
    '
    ' R3 IS THE ONE THAT CATCHES A CONFIDENT LIE. A generated query naming a
    ' table the grounding never surfaced is a hallucination that happens to be
    ' spelled correctly -- and it RUNS, if the name exists, which in an estate
    ' with `deal`, `deal_2015`, `stg_deal` and `dim_deal` it very well might.
    ' Checked against what the grounding ACTUALLY SELECTED rather than against
    ' the catalog, because the catalog contains every wrong answer too.
    '
    ' THE CHECK IS LEXICAL AND SAYS SO. It reads the identifiers after from and
    ' join; it is not a SQL parser, and a query that hides a table name inside a
    ' construct this does not model would pass. That is a stated limit rather
    ' than a claim -- the remedy is `discovery`'s own statement reader, which
    ' parses SQL properly, and wiring the two together is work this increment
    ' does not do.
    function check_sql(sql, g, options)
        opts = _options(options, [ "allow" ], "nlq.check_sql")
        extra = _default(opts, "allow", [])
        problems = []
        low = lower(sql)

        ' R4: read-only, by inspection.
        for each verb in [ "insert ", "update ", "delete ", "drop ", "alter ",
                           "create ", "truncate ", "grant ", "revoke ", "merge " ]
            if contains(low, verb) then
                append(problems, { kind: "not_read_only", detail: trim(verb),
                                   why: "this statement would change the database" })
            end if
        end for
        if not contains(low, "select") then
            append(problems, { kind: "not_a_query", detail: "",
                               why: "no select at all" })
        end if

        ' R3: every table named must be one the grounding surfaced.
        allowed = []
        for each t in g.tables
            append(allowed, lower(t))
        end for
        for each t in extra
            append(allowed, lower(t))
        end for
        for each named in _tables_named(low)
            if not contains(allowed, named) then
                append(problems, { kind: "ungrounded_table", detail: named,
                                   why: ("the grounding never surfaced it; the query names a table " +
                                         "nobody offered, which runs if the name happens to exist") })
            end if
        end for
        return problems
    end function

    ' The identifiers following `from` and `join`. Deliberately small: a
    ' qualified name, up to the first character that cannot be part of one.
    function _tables_named(low)
        out = []
        for each kw in [ "from ", "join " ]
            rest = low
            guard = 0
            while contains(rest, kw) and guard < 64
                guard = guard + 1
                at = find(rest, kw)
                if is_unknown(at) then
                    rest = ""
                else
                    rest = mid(rest, at + len(kw), len(rest))
                    name = _leading_identifier(rest)
                    if len(name) > 0 and not contains(out, name) then
                        ' `from (select ...)` names no table here, and a bare
                        ' alias after a subquery is not one either.
                        if not contains(name, "(") then
                            append(out, name)
                        end if
                    end if
                end if
            end while
        end for
        return out
    end function

    function _leading_identifier(text)
        out = ""
        i = 0
        while i < len(text)
            ch = mid(text, i, 1)
            ok = (ch >= "a" and ch <= "z") or (ch >= "0" and ch <= "9")
            ok = ok or ch = "_" or ch = "."
            if not ok then
                i = len(text)
            else
                out = out + ch
                i = i + 1
            end if
        end while
        return out
    end function

    ' --- the three steps an application drives --------------------------------
    '
    ' NONE OF THESE PERFORMS I/O, and that is the whole design. Every question
    ' varies in what it costs -- the model by a factor of three on the same
    ' hardware depending on load, the database by whatever the query turns out
    ' to be -- and A LIBRARY'S JOB IS TO LET AN APPLICATION BE GRACEFUL ABOUT
    ' THAT RATHER THAN BE GRACEFUL ON ITS BEHALF. One application shows a
    ' spinner, another returns at once and emails the answer, a third refuses
    ' what it estimates will take too long. Those are different products and
    ' none of them is this library's call.
    '
    ' So there is no `answer(catalog, conn, question)`. A single call that ran
    ' the model and the query would have made the decision BY HIDING IT: it
    ' blocks, and every consumer inherits blocking. The shape is `agent`'s,
    ' which solved the same problem here for the same reason -- an approval may
    ' take a minute and the wait spans HTTP requests.
    '
    '   plan    -> the application calls the model, however it likes
    '   interpret -> the application executes the SQL, however it likes
    '   settle
    function plan(cat, vocab, question, options)
        opts = _options(options, [ "limit", "near_misses", "synonyms",
                                   "budget_tokens", "chars_per_token", "dialect",
                                   "exemplars", "derived_from" ], "nlq.plan")
        g = ground(cat, question, { limit: _default(opts, "limit", 8),
                                    near_misses: _default(opts, "near_misses", 3),
                                    synonyms: _default(opts, "synonyms", {}),
                                    derived_from: _default(opts, "derived_from", {}) })
        ' R1/R6 stop here: a grounding the catalog could not settle is not a
        ' plan, and the refusal travels as a VALUE so an application can show a
        ' person the candidates and ask.
        if count(g.ambiguous) > 0 then
            return { ok: false, refused_because: g.ambiguous, grounding: g,
                     question: question, key: "", estimated_tokens: 0 }
        end if
        ex = _default(opts, "exemplars", {})
        p = prompt(g, cat, vocab, question, { budget_tokens: _default(opts, "budget_tokens", 3000),
                                              chars_per_token: _default(opts, "chars_per_token", 2.5),
                                              dialect: _default(opts, "dialect", "standard SQL"),
                                              exemplars: ex })
        return { ok: true,
                 refused_because: [],
                 grounding: g,
                 question: question,
                 system: p.system,
                 user: p.user,
                 ' WHAT IT IS ABOUT TO COST, as far as that is knowable. An
                 ' application that wants to say "this will take a while" needs
                 ' something to decide from, and the alternative is every
                 ' consumer re-deriving it from internals.
                 estimated_tokens: p.estimated_tokens,
                 tables: g.tables,
                 key: _plan_key(g, cat, vocab, question, _default(opts, "synonyms", {})) }
    end function

    ' THE CACHE KEY. NLQ facilitates a cache and does not own one: a question
    ' costs 17-60s of model time and a dashboard asks the same one every
    ' refresh, so not supplying a key would make this unusable while pretending
    ' to be neutral -- but WHERE the store lives, how long it keeps, and when it
    ' drops are the application's, and gdash's answer ("until the next import")
    ' names a moment its importer knows and this library cannot observe.
    '
    ' KEYING ON THE QUESTION TEXT ALONE IS THE TRAP. An import that renames a
    ' column, drops one, or adds a view that now answers better leaves cached
    ' SQL that STILL RUNS and quietly answers about the old shape. So the key
    ' covers the grounded objects WITH THEIR COLUMNS -- not the whole catalog,
    ' which would invalidate on every unrelated import, and not the names alone,
    ' which would survive a column being dropped -- plus the vocabulary those
    ' objects contributed and the synonyms in force.
    function _plan_key(g, cat, vocab, question, syn)
        parts = []
        append(parts, "q:" + join(terms(question), " "))
        for each tid in sort(g.tables)
            cols = []
            for each c in cat.columns
                if _id(c.schema, c.table) = tid then
                    append(cols, c.column)
                end if
            end for
            append(parts, "t:" + tid + "(" + join(sort(cols), ",") + ")")
            for each key in sort(keys(vocab))
                if starts_with(key, tid + ".") then
                    append(parts, "v:" + key + "=" + join(vocab[key], ","))
                end if
            end for
        end for
        for each sk in sort(keys(syn))
            append(parts, "s:" + sk + "=" + join(syn[sk], ","))
        end for
        return _fnv1a(join(parts, "|"))
    end function

    ' 32-bit FNV-1a, written here because `crypto` needs libcrypto and this
    ' library has no other dependency. The key NAMES a cache entry; a collision
    ' would serve another question's SQL, so an application that cares stores
    ' the plan beside it and compares -- the rule llm's keyed replay follows.
    function _fnv1a(text)
        h = 2166136261
        i = 0
        while i < len(text)
            h = _xor32(h, byte_at(text, i))
            h = _mul32(h, 16777619)
            i = i + 1
        end while
        return _hex32(h)
    end function

    ' 32-bit arithmetic on doubles. `h * 16777619` exceeds 2^53 and loses
    ' precision, so the multiply is SPLIT -- the technique llm.bas already uses
    ' for the same hash and the same reason. Copied rather than imported,
    ' because loading `llm` for a hash would give every program that grounds a
    ' question a dependency on an HTTP client.
    function _xor32(a, b)
        out = 0
        bit = 1
        i = 0
        while i < 32
            abit = floor(a / bit) - floor(a / (bit * 2)) * 2
            bbit = floor(b / bit) - floor(b / (bit * 2)) * 2
            if abit != bbit then
                out = out + bit
            end if
            bit = bit * 2
            i = i + 1
        end while
        return out
    end function

    function _mul32(a, b)
        lo = a - floor(a / 65536) * 65536
        hi = floor(a / 65536)
        p = lo * b + (hi * b - floor(hi * b / 65536) * 65536) * 65536
        return p - floor(p / 4294967296) * 4294967296
    end function

    function _hex32(h)
        digits = "0123456789abcdef"
        out = ""
        i = 7
        while i >= 0
            d = floor(h / _pow16(i))
            d = d - floor(d / 16) * 16
            out = out + mid(digits, d, 1)
            i = i - 1
        end while
        return out
    end function

    function _pow16(n)
        v = 1
        i = 0
        while i < n
            v = v * 16
            i = i + 1
        end while
        return v
    end function

    ' The model answered. Turn its text into SQL and check it (R3/R4). Pure.
    function interpret(p, model_text, options)
        opts = _options(options, [ "allow" ], "nlq.interpret")
        if not p.ok then
            error "nlq.interpret: this plan was refused; there is nothing to interpret"
        end if
        sql = _strip_fence(string(model_text))
        problems = check_sql(sql, p.grounding, { allow: _default(opts, "allow", []) })
        return { ok: count(problems) = 0, sql: sql, problems: problems,
                 tables: p.tables, question: p.question, key: p.key }
    end function

    ' A model asked for SQL only will still sometimes fence it. Stripping that
    ' is not leniency about the instruction -- it is refusing to fail on
    ' punctuation when the SQL itself is there.
    function _strip_fence(t)
        s2 = trim(t)
        if starts_with(s2, "```") then
            nl = find(s2, chr(10))
            if not is_unknown(nl) then
                s2 = mid(s2, nl + 1, len(s2))
            end if
            back = find(s2, "```")
            if not is_unknown(back) then
                s2 = left(s2, back)
            end if
        end if
        return trim(s2)
    end function

    ' The query ran. Give back the value WITH ITS PROVENANCE -- R5: an answer
    ' never travels without its query, because "where did this number come
    ' from" is the question a business asks second, immediately.
    function settle(reading, rows)
        if not reading.ok then
            error "nlq.settle: this reading was refused; there is no answer to settle"
        end if
        v = unknown
        col = ""
        if count(rows) > 0 then
            for each k in keys(rows[0])
                if len(col) = 0 then
                    col = k
                    v = rows[0][k]
                end if
            end for
        end if
        return { value: v, column: col, row_count: count(rows),
                 sql: reading.sql, tables: reading.tables,
                 question: reading.question, key: reading.key }
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
        opts = _options(options, [ "limit", "near_misses", "synonyms", "derived_from",
                                   "collapse_siblings" ], "nlq.ground")
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
        ' COLLAPSE NUMBERED SIBLINGS BEFORE THE CUT. MEASURED on a 517-object
        ' estate: recall fell to 10 of 16 at limit 8, and every miss had the
        ' same cause -- `archive.gl_account_2019`, `_002`, `_003`, `_004`,
        ' `_005` score IDENTICALLY, fill six of eight slots, and crowd out
        ' `finance.gl_entry` entirely. A ranked list that spends six slots
        ' saying one thing six times has not ranked badly; it has spent its
        ' budget on repetition.
        '
        ' ONLY A TRAILING NUMBER COLLAPSES. `fact_volume_daily`, `_hourly` and
        ' `_monthly` are granularity VARIANTS -- different tables answering
        ' different questions -- and folding those would lose a real candidate.
        ' The rule is narrow on purpose: same schema, same name but for a
        ' trailing `_NNN` or a trailing digit run.
        families = {}
        if _default(opts, "collapse_siblings", true) then
            deduped = []
            for each r in ranked
                fam = _family_of(r.id)
                if is_unknown(families[fam]) then
                    families[fam] = []
                    append(deduped, r)
                else
                    families[fam] = _append_to(families[fam], r.id)
                end if
            end for
            ranked = deduped
        end if
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
        ' DISCLOSURE, NOT A REFUSAL -- see `alternatives` below.
        alts = _derivation_conflicts(selected, qterms, by_table,
                                     _default(opts, "derived_from", {}))

        return { tables: _ids(selected),
                 ambiguous: amb,
                 ' R2 IS REPORTED, NOT REFUSED, and that is a correction. Built
                 ' first as a blocker, it refused 8 OF 16 benchmark questions --
                 ' the same failure R6 had at 15 of 16 before its discriminator
                 ' existed. The diagnosis is what settles the design: for
                 ' `t_total_volume` the two derivations differ by 36% and the
                 ' distinction is the whole answer; for `t_active_deals` they
                 ' agree exactly (315 either way) and it is noise. NLQ CANNOT
                 ' TELL THOSE APART WITHOUT RUNNING BOTH, which is expensive and
                 ' is the application's decision to make, not this library's.
                 ' So the fact travels and the choice does not -- the same split
                 ' as the latency and the cache.
                 alternatives: alts,
                 detail: selected,
                 near_misses: _ids(near),
                 ' The siblings a selected object stands for. Reported, because
                 ' a caller that wanted all five archive copies must be able to
                 ' see that four were folded away -- collapsing silently would
                 ' be the same class of defect as truncating a prompt.
                 siblings: _siblings_of(selected, families),
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

    ' --- R2: two lineages are two answers -------------------------------------
    '
    ' MEASURED, WITH A NUMBER. Asked for total volume, the model summed
    ' `gross_vol_mmbtu` from `warehouse.fact_volume` and returned 4,025,053
    ' where the question is about `trading.deal` and the answer is 6,285,487.
    ' The fact table is built by an ETL that joins `trading.ctp` and keeps only
    ' active counterparties, so it holds a SUBSET -- and the sum of a subset is
    ' a perfectly good number about a real table, 36% short of the one asked
    ' for, with nothing in the query to say so.
    '
    ' THE DISCRIMINATOR IS DERIVATION, NOT NAME OVERLAP. `trading.deal` and
    ' `trading_emea.deal` share every column and are PEERS -- a question may
    ' legitimately mean either. `trading.deal` and `warehouse.stg_deal` share
    ' every column and are a CHAIN, where one is built from the other and can
    ' hold fewer rows. Only derivation separates those, and a check that fired
    ' on shared columns alone would flag every regional partition in the estate.
    '
    ' DERIVATION IS DECLARED, NOT INFERRED, and by the party that knows it: an
    ' application that performed an import knows what it imported and from
    ' where, and `discovery.lineage` can produce it for anyone holding module
    ' bodies. NLQ takes it as input for the same reason it takes synonyms and
    ' vocabulary -- and so that grounding a question does not drag an ODBC
    ' catalog reader in behind it.
    function _derivation_conflicts(selected, qterms, by_table, derived)
        out = []
        if type(derived) != "record" then
            error "nlq: `derived_from` is a record of object -> the object it is built from"
        end if
        if count(keys(derived)) = 0 then
            return out
        end if
        ' ONE CONFLICT PER PAIR, NOT PER WORD. A question naming
        ' `gross_vol_mmbtu` matches on gross, vol AND mmbtu, and reporting the
        ' same pair three times is noise that buries the one fact.
        pairs = []
        measures = {}
        for each qt in qterms
            for each a in selected
                for each b in selected
                    if a.id != b.id and _derives_from(derived, a.id, b.id) then
                        if _holds(by_table, a, qt) and _holds(by_table, b, qt) then
                            tag = a.id + " <- " + b.id
                            if not contains(pairs, tag) then
                                append(pairs, tag)
                                measures[tag] = []
                            end if
                            if not contains(measures[tag], qt) then
                                measures[tag] = _append_to(measures[tag], qt)
                            end if
                        end if
                    end if
                end for
            end for
        end for
        for each tag in pairs
            bits = split(tag, " <- ")
            ' CANDIDATES ARE THE TWO IN THE RELATIONSHIP, not everything that
            ' happens to hold the column. `trading_apac.deal` shares every
            ' column with `trading.deal` and is derived from NOTHING -- naming
            ' it as a derived alternative would be false, and would make a
            ' regional partition look like a staging copy.
            append(out, { kind: "derived_alternatives",
                          candidates: [ bits[0], bits[1] ],
                          measure: join(measures[tag], ", "),
                          why: (bits[0] + " is built from " + bits[1] + ", so it can hold " +
                                "fewer rows and their totals can differ; the query cannot " +
                                "say which was meant") })
        end for
        return out
    end function

    function _holds(by_table, row, qt)
        if contains(row.columns, qt) then
            return true
        end if
        return _has_column_term(by_table, row.id, qt)
    end function

    function _has_column_term(by_table, tid, qt)
        cols = by_table[tid]
        if is_unknown(cols) then
            return false
        end if
        for each c in cols
            if contains(_ident_terms(c.column), qt) then
                return true
            end if
        end for
        return false
    end function

    ' Transitive: fact_volume is built from stg_deal is built from deal, and a
    ' question answered from the first is two hops from the one asked about.
    '
    ' A SOURCE IS A LIST, NOT ONE OBJECT, and that is the estate's correction
    ' rather than a generalisation: `p_build_fact` reads the staging table, a
    ' counterparty lookup AND the contract table, so naming one would have
    ' looked definite and been wrong for two of the three. Breadth-first over
    ' the whole set, with a visited guard because a restatement can read the
    ' very table it writes and this must not spin.
    function _derives_from(derived, a, b)
        seen = [ a ]
        frontier = [ a ]
        hops = 0
        while count(frontier) > 0 and hops < 12
            nxt = []
            for each cur in frontier
                srcs = _sources_of(derived, cur)
                for each sname in srcs
                    if sname = b then
                        return true
                    end if
                    if not contains(seen, sname) then
                        append(seen, sname)
                        append(nxt, sname)
                    end if
                end for
            end for
            frontier = nxt
            hops = hops + 1
        end while
        return false
    end function

    ' Tolerates both shapes: a list (what an estate with real ETL emits) and a
    ' bare name (what a caller writes by hand for one obvious case).
    function _sources_of(derived, id)
        v = derived[id]
        if is_unknown(v) then
            return []
        end if
        if type(v) = "array" then
            return v
        end if
        return [ string(v) ]
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

    ' `archive.gl_account_2019_002` and `archive.gl_account_2019` are one
    ' family; `warehouse.fact_volume_daily` and `warehouse.fact_volume` are not.
    function _family_of(id)
        parts = split(id, ".")
        last = parts[count(parts) - 1]
        base = _strip_numeric_suffix(last)
        out = ""
        i = 0
        while i < count(parts) - 1
            out = out + parts[i] + "."
            i = i + 1
        end while
        return out + base
    end function

    function _strip_numeric_suffix(name)
        s2 = name
        ' a trailing _NNN
        at = 0
        i = len(s2) - 1
        digits = 0
        while i >= 0
            ch = mid(s2, i, 1)
            if ch >= "0" and ch <= "9" then
                digits = digits + 1
                i = i - 1
            else
                if ch = "_" and digits > 0 then
                    return left(s2, i)
                end if
                i = 0 - 1
            end if
        end while
        return s2
    end function

    function _siblings_of(selected, families)
        out = {}
        for each r in selected
            fam = _family_of(r.id)
            kin = families[fam]
            if not is_unknown(kin) then
                if count(kin) > 0 then
                    out[r.id] = kin
                end if
            end if
        end for
        return out
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

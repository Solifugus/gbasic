' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' ari.bas — Anchor Relative Identification: parse messy, semi-structured
' reports declaratively and return a frame.
'
' Design: docs/text_design.md (§4-§5, §5.1, §13.H). Syntax: docs/ari_spec_language.md.
' Layer 1 of the text library — pure gBASIC over the Layer 0 regex core.
'
'   result = ari.parse(report_text, spec_text)
'   result = ari.import(path, spec_text)
'
' WHAT THIS IS FOR. Paginated print-image reports from mainframe-era systems:
' teller totals, trial balances, transaction registers. Those files are not
' consistent even with THEMSELVES, because different sections were written by
' different people across decades. A field is located by what surrounds it in
' the document, not by a rigid column position — which is what survives when a
' column heading has drifted 11 columns away from its own data.
'
' THREE THINGS THAT DRIVE THE DESIGN, each measured rather than assumed
' (docs/ari_spec_language.md §1):
'
'   * A column heading cannot locate its own column. Headings drift from data.
'   * A fixed column span cannot locate a value either: a trailing-minus
'     negative is one column WIDER than the positives above it, so any pinned
'     column truncates the sign or captures a space.
'   * What survives both is bounding by TYPE — "the last money-shaped token on
'     this row". So `as <type>` DELIMITS a value as well as converting it.
'
' PAGE FURNITURE IS STRIPPED FIRST. A page break can fall anywhere, including
' mid-section, because pagination follows lines-per-page and knows nothing about
' logical structure. So furniture removal is a preprocessing pass over the line
' grid, before any section is located, and every offset thereafter counts over
' the CLEAN grid rather than the physical file (§13.H).
'
' Depends on: nothing. Regex is a core builtin, not a `load`.
library ari

    ' ---------------------------------------------------------------- helpers
    '
    ' Several helpers exist only to hold a result in a variable before it is
    ' compared. `if _helper(x) = "y" then`, where x is a bare identifier and
    ' _helper comes from a loaded library, hits the modifier-clause collision
    ' documented in docs/ai/UNLEARN.md and fails at run time naming your own
    ' argument. Binding first is the documented workaround, not a style choice.

    function _indent_of(line)
        n = 0
        while n < len(line)
            c = mid(line, n, 1)
            if c != " " then
                return n
            end if
            n = n + 1
        end while
        return n
    end function

    function _is_blank(line)
        t = trim(line)
        return len(t) = 0
    end function

    ' Strip a matching pair of surrounding quotes, if present.
    function _unquote(s)
        t = trim(s)
        n = len(t)
        if n < 2 then
            return t
        end if
        first = mid(t, 0, 1)
        last = mid(t, n - 1, 1)
        if first = "\"" then
            if last = "\"" then
                return mid(t, 1, n - 2)
            end if
        end if
        if first = "/" then
            if last = "/" then
                return mid(t, 1, n - 2)
            end if
        end if
        return t
    end function

    ' Is this token a /regex/ rather than a "literal"?
    function _is_regex_token(s)
        t = trim(s)
        n = len(t)
        if n < 2 then
            return false
        end if
        a = mid(t, 0, 1)
        b = mid(t, n - 1, 1)
        if a = "/" then
            if b = "/" then
                return true
            end if
        end if
        return false
    end function

    ' Find `needle` in `hay` as a pattern token: /re/ matches as a regex,
    ' anything else literally. Returns the match record or unknown.
    function _find_token(hay, token)
        body = _unquote(token)
        isre = _is_regex_token(token)
        if isre then
            return match(hay, regex(body))
        end if
        idx = find(hay, body)
        ' `find` reports a miss as NOTHING while `match` reports it as UNKNOWN
        ' (see /DOGFOOD.md, 2026-08-01). Both are checked here so this helper can
        ' present one miss value to its callers regardless of which path ran.
        if is_nothing(idx) then
            return unknown
        end if
        if is_unknown(idx) then
            return unknown
        end if
        return { text: body, start: idx, length: len(body), groups: [] }
    end function

    ' ------------------------------------------------------- type recognizers
    '
    ' §5.1: one recognizer covering the UNION of forms, because a single report
    ' disagrees with itself between sections. The precedent is date parsing —
    ' many forms arrive and most can be deciphered without being told which.
    '
    ' Ordered most-specific first: the bracket, paren, CR and trailing-minus
    ' forms must be tried before the plain one, or the plain pattern matches
    ' their digits and drops the sign.
    function _money_patterns()
        return [
            { re: "<[ ]*\\$?[ ]*([0-9,]+\\.[0-9]{2})[ ]*>", neg: true },
            { re: "\\([ ]*\\$?[ ]*([0-9,]+\\.[0-9]{2})[ ]*\\)", neg: true },
            { re: "\\$?[ ]*([0-9,]+\\.[0-9]{2})[ ]*CR", neg: true },
            { re: "\\$?[ ]*([0-9,]+\\.[0-9]{2})[ ]*-", neg: true },
            { re: "-[ ]*\\$[ ]*([0-9,]+\\.[0-9]{2})", neg: true },
            { re: "\\$[ ]*-[ ]*([0-9,]+\\.[0-9]{2})", neg: true },
            { re: "-[ ]*([0-9,]+\\.[0-9]{2})", neg: true },
            { re: "\\$[ ]*([0-9,]+\\.[0-9]{2})", neg: false },
            { re: "([0-9,]+\\.[0-9]{2})", neg: false }
        ]
    end function

    ' Strip grouping separators and convert. Returns unknown when the digits do
    ' not form a well-shaped amount — §8: a bad cell becomes unknown, never a
    ' silent zero and never a raise that sinks the import.
    function _to_amount(digits, negate)
        clean = replace(digits, ",", "")
        n = number(clean)
        if is_unknown(n) then
            return unknown
        end if
        if negate then
            return 0 - n
        end if
        return n
    end function

    ' Scan `text` for a money value. `want_last` picks the rightmost rather than
    ' the leftmost, which is what a detail row needs (§1.3).
    function _money_in(text, want_last)
        pats = _money_patterns()
        chosen = []

        ' Claim spans most-specific-pattern first, rejecting any later match that
        ' OVERLAPS one already claimed.
        '
        ' This is what makes a trailing minus survive. In "...  $6,000.25-" the
        ' specific pattern matches "$6,000.25-" at column 69, and the generic
        ' one matches "6,000.25" at column 70 — further RIGHT, so a naive
        ' "rightmost wins" rule picks the generic reading and silently drops the
        ' sign, turning -6000.25 into 6000.25. Overlap rejection is the fix:
        ' the specific pattern claims the span first and the generic match
        ' inside it is not a second amount.
        for each p in pats
            ms = match_all(text, regex(p.re))
            for each m in ms
                a1 = m.start
                b1 = m.start + m.length
                clash = false
                for each c in chosen
                    if a1 < c.fin then
                        if c.beg < b1 then
                            clash = true
                        end if
                    end if
                end for
                if not clash then
                    v = _to_amount(m.groups[0], p.neg)
                    if not is_unknown(v) then
                        append(chosen, { beg: a1, fin: b1, val: v })
                    end if
                end if
            end for
        end for

        if count(chosen) = 0 then
            return unknown
        end if
        best = chosen[0]
        for each c in chosen
            if want_last then
                if c.beg > best.beg then
                    best = c
                end if
            else
                if c.beg < best.beg then
                    best = c
                end if
            end if
        end for
        return best.val
    end function

    function _integer_in(text, want_last)
        ms = match_all(text, regex("-?[0-9]+"))
        if count(ms) = 0 then
            return unknown
        end if
        idx = 0
        if want_last then
            idx = count(ms) - 1
        end if
        return number(ms[idx].text)
    end function

    function _decimal_in(text, want_last)
        ms = match_all(text, regex("-?[0-9,]+\\.[0-9]+"))
        if count(ms) = 0 then
            return _integer_in(text, want_last)
        end if
        idx = 0
        if want_last then
            idx = count(ms) - 1
        end if
        cleaned = replace(ms[idx].text, ",", "")
        return number(cleaned)
    end function

    function _pad2(n)
        if n < 10 then
            return "0" + string(n)
        end if
        return string(n)
    end function

    ' Native typed values. gBASIC builds money and datetime with ASSIGN
    ' MODIFIERS rather than functions or literals — `m(USD) = 12.34`,
    ' `d(date) = "2021-12-27"` — which is why a search for a `date(...)` builtin
    ' finds nothing (see /DOGFOOD.md 2026-08-01, correction entry).
    function _to_money(n)
        m(USD) = n
        return m
    end function

    ' The `date` modifier does NOT raise on a malformed string (measured
    ' 2026-08-23; this comment claimed otherwise for months). It prints an
    ' unlocated line and yields `nothing`, exit code unchanged -- so the
    ' range-check in _date_in below is not defending against a raise, it is
    ' supplying the diagnosis the modifier does not give.
    function _to_date(iso)
        d(date) = iso
        return d
    end function

    function _valid_ymd(y, mo, da)
        if mo < 1 then
            return false
        end if
        if mo > 12 then
            return false
        end if
        if da < 1 then
            return false
        end if
        if da > 31 then
            return false
        end if
        return true
    end function

    ' Dates. Returns { val, why } — `why` is "" on success, otherwise a reason
    ' code that becomes a diagnostic. `val` is a normalized ISO STRING; the
    ' caller turns it into a native datetime, so custom-type rules and the
    ' builtin path share one validated producer.
    '
    ' TWO-COMPONENT DATES ARE THE ONE CASE THE UNION CANNOT SETTLE (§5.1).
    ' 27/12/2021 is decided by the token itself: 27 cannot be a month. But
    ' 03/04/2026 is 3 April or 4 March and NOTHING in the value says which.
    '
    ' Per-token disambiguation is done here and needs no declaration, which
    ' covers most real dates — roughly 60% of a natural spread has a day above
    ' 12. What is deliberately NOT done is inferring the dialect from OTHER rows
    ' in the same column. That reads well and fails badly: inference is stable
    ' for one file but can differ between files of the same recurring report, so
    ' January's extract (containing a row dated the 27th) resolves correctly and
    ' February's (all days <= 12) falls back to a guess and reads EVERY date
    ' wrong, silently, with the same spec. Intermittent and invisible is the
    ' worst shape this bug can take.
    '
    ' So an ambiguous token with no declared dialect yields `unknown` plus a
    ' diagnostic naming the fix. Since most values resolve, an undeclared DD/MM
    ' column comes out mostly-converted with a few unknowns — a legible signal
    ' pointing straight at `using date: dmy`.
    '
    ' Output is a normalized ISO string, not a native datetime: gBASIC has no
    ' timezone-free runtime constructor from year/month/day (only `now` and a
    ' shifting `from_epoch`), so a native value could not be produced without
    ' inventing a timezone. See /DOGFOOD.md 2026-08-01.
    function _date_in(text, dialect)
        iso = match(text, regex("([0-9]{4})-([0-9]{2})-([0-9]{2})"))
        if not is_unknown(iso) then
            return { val: iso.groups[0] + "-" + iso.groups[1] + "-" + iso.groups[2], why: "" }
        end if

        m = match(text, regex("([0-9]{1,2})[/.-]([0-9]{1,2})[/.-]([0-9]{4})"))
        if is_unknown(m) then
            return { val: unknown, why: "no-date-found" }
        end if
        a = number(m.groups[0])
        b = number(m.groups[1])
        y = m.groups[2]

        if dialect = "dmy" then
            ok1 = _valid_ymd(y, b, a)
            if not ok1 then
                return { val: unknown, why: "invalid-date" }
            end if
            return { val: y + "-" + _pad2(b) + "-" + _pad2(a), why: "" }
        end if
        if dialect = "mdy" then
            ok2 = _valid_ymd(y, a, b)
            if not ok2 then
                return { val: unknown, why: "invalid-date" }
            end if
            return { val: y + "-" + _pad2(a) + "-" + _pad2(b), why: "" }
        end if

        ' No declared dialect: settle it from the token alone, or refuse.
        if a > 12 then
            ok3 = _valid_ymd(y, b, a)
            if not ok3 then
                return { val: unknown, why: "invalid-date" }
            end if
            return { val: y + "-" + _pad2(b) + "-" + _pad2(a), why: "" }
        end if
        if b > 12 then
            ok4 = _valid_ymd(y, a, b)
            if not ok4 then
                return { val: unknown, why: "invalid-date" }
            end if
            return { val: y + "-" + _pad2(a) + "-" + _pad2(b), why: "" }
        end if
        return { val: unknown, why: "ambiguous-date" }
    end function

    ' Convert an extracted span according to its declared type. `as <type>`
    ' DELIMITS as well as converts: the span handed in may carry neighbouring
    ' text, and the recognizer takes the token of that shape out of it.
    '
    ' `ctx` carries the spec's custom `type` declarations and the `using`
    ' bindings in scope, so a section can rebind what `money` or `date` means
    ' for itself and its children (§5.1).
    ' Returns { val, why }.
    function _convert(span, ty, want_last, ctx)
        if ty = "" then
            return { val: trim(span), why: "" }
        end if
        if ty = "text" then
            return { val: trim(span), why: "" }
        end if

        ' A `using <builtin>: <name>` binding in scope redirects the builtin to
        ' a custom type; a field naming a custom type directly beats any binding.
        eff = ty
        bound = ctx.usings[ty]
        if not is_unknown(bound) then
            eff = bound
        end if

        custom = ctx.types[eff]
        if not is_unknown(custom) then
            ' A custom type is an ordered rule list: first match wins, so the
            ' negative form must be declared before the general one.
            for each rule in custom.rules
                mm = match(span, regex(rule.re))
                if not is_unknown(mm) then
                    ' A rule need not capture: with no group, the whole match is
                    ' the value. Indexing groups[0] blindly would fail on every
                    ' uncaptured rule.
                    ' With a TRANSFORM the whole match is the input to the
                    ' rewrite; group references live in the replacement. Without
                    ' one, a captured group is the value — which is how the money
                    ' rules pull digits out of their symbols and signs. Feeding
                    ' groups[0] to a transform would hand it only the first
                    ' capture (a two-digit day, in the date case) and rewrite
                    ' that.
                    raw = mm.text
                    if rule.repl = "" then
                        if count(mm.groups) > 0 then
                            g0 = mm.groups[0]
                            if not is_unknown(g0) then
                                raw = g0
                            end if
                        end if
                    end if
                    ' `/re/repl/` — a rule may REWRITE as well as match, with
                    ' $1..$9 group references. That is what lets a spec
                    ' normalize a shape the recognizers do not know, e.g.
                    ' /(\d{2})\/(\d{2})\/(\d{4})/$3-$1-$2/ turning a slashed
                    ' date into ISO before the date conversion sees it.
                    if rule.repl != "" then
                        raw = replace(raw, regex(rule.re), rule.repl)
                    end if

                    if custom.base = "date" then
                        dr = _date_in(raw, rule.dialect)
                        if is_unknown(dr.val) then
                            return dr
                        end if
                        return { val: _to_date(dr.val), why: "" }
                    end if
                    v = _to_amount(raw, rule.neg)
                    if not is_unknown(v) then
                        if custom.base = "money" then
                            return { val: _to_money(v), why: "" }
                        end if
                        return { val: v, why: "" }
                    end if
                end if
            end for
            return { val: unknown, why: "no-rule-matched" }
        end if

        if eff = "money" then
            v = _money_in(span, want_last)
            if is_unknown(v) then
                return { val: unknown, why: "malformed-money" }
            end if
            ' A native money value, not a bare number: that is what §4 promised
            ' and what makes the output flow into frame/stats as designed. It
            ' also prints its cents correctly, which a number above $9,999.99
            ' does not (/DOGFOOD.md 2026-08-01).
            return { val: _to_money(v), why: "" }
        end if
        if eff = "integer" then
            v = _integer_in(span, want_last)
            if is_unknown(v) then
                return { val: unknown, why: "no-integer-found" }
            end if
            return { val: v, why: "" }
        end if
        if eff = "decimal" then
            v = _decimal_in(span, want_last)
            if is_unknown(v) then
                return { val: unknown, why: "no-decimal-found" }
            end if
            return { val: v, why: "" }
        end if
        if eff = "date" then
            dr = _date_in(span, "")
            if is_unknown(dr.val) then
                return dr
            end if
            return { val: _to_date(dr.val), why: "" }
        end if
        return { val: trim(span), why: "" }
    end function

    ' ------------------------------------------------------- the page-furniture
    ' pass (§13.H)
    '
    ' Runs BEFORE anything else. Produces the clean grid every later stage reads,
    ' each entry carrying the physical line number so diagnostics can point back
    ' into the original file.
    function _build_grid(text, page)
        raw = split(text, "\n")
        grid = []
        i = 0
        drop_left = 0
        while i < count(raw)
            line = raw[i]
            physical = i + 1

            if drop_left > 0 then
                drop_left = drop_left - 1
                i = i + 1
                continue
            end if

            is_break = false
            if page.kind = "formfeed" then
                has_ff = contains(line, chr(12))
                if has_ff then
                    is_break = true
                end if
            end if
            if page.kind = "regex" then
                hit = contains(line, regex(page.pattern))
                if hit then
                    is_break = true
                end if
            end if

            if is_break then
                ' `drop` counts from and including the break line.
                drop_left = page.drop - 1
                i = i + 1
                continue
            end if

            append(grid, { text: line, line: physical })
            i = i + 1
        end while
        return grid
    end function

    ' ----------------------------------------------------------- spec parsing
    '
    ' Indentation-defined blocks. Deliberately small: a spec is a short
    ' declarative document, and a heavyweight parser here would be the tail
    ' wagging the dog.

    function _new_section(name)
        return { name: name, repeats: false, starts: "", ends: "",
                 fields: [], sections: [], rows: [], has_rows: false,
                 row_continue: "", usings: { } }
    end function

    ' Split a type rule into its slash-delimited parts and its action:
    '
    '     /re/ -> as decimal          -> parts ["re"],         action "as decimal"
    '     /re/repl/ -> as date        -> parts ["re","repl"],  action "as date"
    '
    ' Hand-scanned rather than matched with a regex because a rule's own regex
    ' routinely contains ESCAPED SLASHES — a date pattern always does — and
    ' POSIX ERE has no lookbehind, so `/(.*)/([^/]*)/` cannot tell a delimiter
    ' from a `\/` inside the body. It silently mis-split `/[0-9]{2}\/[0-9]{2}\/
    ' [0-9]{4}/ -> dmy` into a transform whose pattern ended in a lone
    ' backslash, which regcomp then rejected.
    function _split_rule(rt)
        opens = starts_with(rt, "/")
        if not opens then
            return unknown
        end if
        parts = []
        cur = []
        i = 1
        n = len(rt)
        while i < n
            c = mid(rt, i, 1)
            if c = "\\" then
                if i + 1 < n then
                    append(cur, c + mid(rt, i + 1, 1))
                    i = i + 2
                    continue
                end if
            end if
            if c = "/" then
                append(parts, join(cur, ""))
                cur = []
                i = i + 1
                rest = trim(mid(rt, i, n - i))
                arrow = starts_with(rest, "->")
                if arrow then
                    return { parts: parts, action: trim(mid(rest, 2, len(rest) - 2)) }
                end if
                continue
            end if
            append(cur, c)
            i = i + 1
        end while
        return unknown
    end function

    function _parse_field(body)
        ' "<name>: <locator> [as <type>]"
        c = find(body, ":")
        if is_unknown(c) then
            return unknown
        end if
        fname = trim(mid(body, 0, c))
        rest = trim(mid(body, c + 1, len(body) - c - 1))
        ty = ""
        am = match(rest, regex("[ ]+as[ ]+([A-Za-z_][A-Za-z0-9_]*)[ ]*$"))
        if not is_unknown(am) then
            ty = am.groups[0]
            rest = trim(mid(rest, 0, am.start))
        end if
        return { name: fname, locator: rest, type: ty }
    end function

    ' `section <name> [repeats] [starts(<pat>)] [ends(<pat>)]:`
    function _parse_section_header(body)
        sec = _new_section("")
        rest = trim(body)
        nm = match(rest, regex("^section[ ]+([A-Za-z_][A-Za-z0-9_]*)"))
        if is_unknown(nm) then
            return unknown
        end if
        sec.name = nm.groups[0]
        has_rep = contains(rest, regex("[ ]repeats([ ]|:|$)"))
        if has_rep then
            sec.repeats = true
        end if
        sm = match(rest, regex("starts\\(([^)]*)\\)"))
        if not is_unknown(sm) then
            sec.starts = trim(sm.groups[0])
        end if
        em = match(rest, regex("ends\\(([^)]*)\\)"))
        if not is_unknown(em) then
            sec.ends = trim(em.groups[0])
        end if
        return sec
    end function

    ' Parse the lines of one indented block into `sec`, recursing for nested
    ' sections. Returns the populated section; `lines` is the whole spec and
    ' `bounds` gives [start, end) of this block.
    function _parse_block(sec, lines, start_at, stop_at, base_indent)
        i = start_at
        while i < stop_at
            line = lines[i]
            blank = _is_blank(line)
            if blank then
                i = i + 1
                continue
            end if
            t = trim(line)
            is_comment = starts_with(t, "'")
            if is_comment then
                i = i + 1
                continue
            end if
            ind = _indent_of(line)
            if ind < base_indent then
                return { sec: sec, next_at: i }
            end if

            ' Find the extent of any block this line opens.
            child_end = i + 1
            while child_end < stop_at
                cl = lines[child_end]
                cb = _is_blank(cl)
                if cb then
                    child_end = child_end + 1
                    continue
                end if
                ci = _indent_of(cl)
                if ci <= ind then
                    break
                end if
                child_end = child_end + 1
            end while

            if starts_with(t, "section ") then
                child = _parse_section_header(t)
                if not is_unknown(child) then
                    inner = _parse_block(child, lines, i + 1, child_end, ind + 1)
                    append(sec.sections, inner.sec)
                end if
                i = child_end
                continue
            end if

            if starts_with(t, "rows") then
                rm2 = match(t, regex("^rows[ ]*(continue\\(([^)]*)\\))?[ ]*:"))
                if not is_unknown(rm2) then
                    holder = _new_section("rows")
                    inner = _parse_block(holder, lines, i + 1, child_end, ind + 1)
                    sec.rows = inner.sec.fields
                    sec.has_rows = true
                    ' `rows continue(<pat>):` — a line matching <pat> is a
                    ' CONTINUATION of the record above it, not a new record.
                    ' Wrapped fields are ordinary in these reports and there was
                    ' previously no way to say so.
                    g1 = rm2.groups[1]
                    if not is_unknown(g1) then
                        sec.row_continue = trim(g1)
                    end if
                    i = child_end
                    continue
                end if
            end if

            if starts_with(t, "field ") then
                f = _parse_field(trim(mid(t, 6, len(t) - 6)))
                if not is_unknown(f) then
                    append(sec.fields, f)
                end if
                i = i + 1
                continue
            end if

            if starts_with(t, "using ") then
                um = match(t, regex("^using[ ]+([A-Za-z_]+)[ ]*:[ ]*([A-Za-z_][A-Za-z0-9_]*)"))
                if not is_unknown(um) then
                    sec.usings[um.groups[0]] = um.groups[1]
                end if
                i = i + 1
                continue
            end if

            i = i + 1
        end while
        return { sec: sec, next_at: i }
    end function

    ' Parse a whole spec: the optional `page:` block, then the root section.
    function ari_parse_spec(spec_text)
        lines = split(spec_text, "\n")
        page = { kind: "none", pattern: "", drop: 0 }
        types = { }
        root = unknown

        i = 0
        while i < count(lines)
            line = lines[i]
            blank = _is_blank(line)
            if blank then
                i = i + 1
                continue
            end if
            t = trim(line)
            is_comment = starts_with(t, "'")
            if is_comment then
                i = i + 1
                continue
            end if
            ind = _indent_of(line)

            block_end = i + 1
            while block_end < count(lines)
                cl = lines[block_end]
                cb = _is_blank(cl)
                if cb then
                    block_end = block_end + 1
                    continue
                end if
                ci = _indent_of(cl)
                if ci <= ind then
                    break
                end if
                block_end = block_end + 1
            end while

            if starts_with(t, "page:") then
                j = i + 1
                while j < block_end
                    pt = trim(lines[j])
                    bm = match(pt, regex("^break:[ ]*(.*)$"))
                    if not is_unknown(bm) then
                        v = trim(bm.groups[0])
                        if v = "formfeed" then
                            page.kind = "formfeed"
                        else
                            page.kind = "regex"
                            page.pattern = _unquote(v)
                        end if
                    end if
                    dm = match(pt, regex("^drop:[ ]*([0-9]+)"))
                    if not is_unknown(dm) then
                        page.drop = number(dm.groups[0])
                    end if
                    j = j + 1
                end while
                i = block_end
                continue
            end if

            ' `type <name>:` — an ordered rule list plus an output base type.
            ' Rules are tried in order and first match wins, so a negative form
            ' must be declared before the general one. This is the mechanism for
            ' a document that contradicts itself (§5.1), not a nicety.
            tym = match(t, regex("^type[ ]+([A-Za-z_][A-Za-z0-9_]*)[ ]*:"))
            if not is_unknown(tym) then
                tname = tym.groups[0]
                rules = []
                base = "text"
                j = i + 1
                while j < block_end
                    rt = trim(lines[j])
                    om = match(rt, regex("^output:[ ]*([A-Za-z_]+)"))
                    if not is_unknown(om) then
                        base = om.groups[0]
                    else
                        ' /regex/ -> [negate] [as <base>] | /regex/ -> dmy|mdy
                        ' Greedy up to the LAST slash before `->`: a rule's
                        ' regex may itself contain escaped slashes (a date
                        ' pattern almost always does), which a [^/]* body
                        ' cannot span.
                        sp2 = _split_rule(rt)
                        if not is_unknown(sp2) then
                            body = sp2.parts[0]
                            repl = ""
                            if count(sp2.parts) > 1 then
                                repl = sp2.parts[1]
                            end if
                            act = sp2.action
                            neg = contains(act, "negate")
                            dial = ""
                            if contains(act, "dmy") then
                                dial = "dmy"
                            end if
                            if contains(act, "mdy") then
                                dial = "mdy"
                            end if
                            append(rules, { re: body, neg: neg, dialect: dial, repl: repl })
                        end if
                    end if
                    j = j + 1
                end while
                types[tname] = { rules: rules, base: base }
                i = block_end
                continue
            end if

            if starts_with(t, "section ") then
                sec = _parse_section_header(t)
                if not is_unknown(sec) then
                    inner = _parse_block(sec, lines, i + 1, block_end, ind + 1)
                    root = inner.sec
                end if
                i = block_end
                continue
            end if

            i = i + 1
        end while

        return { page: page, root: root, types: types }
    end function

    ' -------------------------------------------------------------- locators
    '
    ' A locator answers "where is the value", relative to one line of the clean
    ' grid. Returns the raw span; `_convert` then delimits and converts it.

    function _apply_columns(line, a, b)
        n = len(line)
        if a >= n then
            return ""
        end if
        last_col = b
        if last_col >= n then
            last_col = n - 1
        end if
        return mid(line, a, last_col - a + 1)
    end function

    ' Returns { ok, span } for one locator against one line.
    function _locate_in_line(line, loc)
        work = line
        lo = 0

        ' `<locator> within columns a-b` narrows the search window first.
        wm = match(loc, regex("[ ]+within[ ]+columns[ ]+([0-9]+)-([0-9]+)[ ]*$"))
        if not is_unknown(wm) then
            lo = number(wm.groups[0])
            hi = number(wm.groups[1])
            work = _apply_columns(line, lo, hi)
            loc = trim(mid(loc, 0, wm.start))
        end if

        cm = match(loc, regex("^columns[ ]+([0-9]+)-([0-9]+)[ ]*$"))
        if not is_unknown(cm) then
            a = number(cm.groups[0])
            b = number(cm.groups[1])
            return { ok: true, span: _apply_columns(work, a, b) }
        end if

        rm = match(loc, regex("^right[ ]+of[ ]+(.*)$"))
        if not is_unknown(rm) then
            tok = trim(rm.groups[0])
            m = _find_token(work, tok)
            if is_unknown(m) then
                return { ok: false, span: "" }
            end if
            after = m.start + m.length
            return { ok: true, span: mid(work, after, len(work) - after) }
        end if

        lm = match(loc, regex("^left[ ]+of[ ]+(.*)$"))
        if not is_unknown(lm) then
            tok = trim(lm.groups[0])
            m = _find_token(work, tok)
            if is_unknown(m) then
                return { ok: false, span: "" }
            end if
            return { ok: true, span: mid(work, 0, m.start) }
        end if

        bm = match(loc, regex("^between[ ]+(.*)[ ]+and[ ]+(.*)$"))
        if not is_unknown(bm) then
            m1 = _find_token(work, trim(bm.groups[0]))
            if is_unknown(m1) then
                return { ok: false, span: "" }
            end if
            a = m1.start + m1.length
            tail = mid(work, a, len(work) - a)
            m2 = _find_token(tail, trim(bm.groups[1]))
            if is_unknown(m2) then
                return { ok: false, span: "" }
            end if
            return { ok: true, span: mid(tail, 0, m2.start) }
        end if

        ' `first <type>` / `last <type>` — the whole line is the span and the
        ' type recognizer does the delimiting (§1.3).
        fm = match(loc, regex("^(first|last)[ ]+([A-Za-z_]+)[ ]*$"))
        if not is_unknown(fm) then
            return { ok: true, span: work, scan: fm.groups[0], ty: fm.groups[1] }
        end if

        return { ok: false, span: "" }
    end function

    ' Parse a vertical locator: `down <dist> of <pat> [<inner locator>]`, where
    ' <dist> is an exact count (`1`), a range (`1-3`), an open range (`3-`,
    ' `-8`) or `flush` (to the block edge).
    '
    ' A RANGE is not a convenience. In the delinquency fixture the gap between
    ' `REMARKS:` and its note is 1, 2, 2 and 3 lines across four branches,
    ' because the generator emits a varying number of blank lines — which is
    ' what real reports do. An exact distance matches one branch and misses the
    ' rest, silently.
    function _parse_vertical(loc)
        vm = match(loc, regex("^(down|up)[ ]+([0-9]+-[0-9]+|[0-9]+-|-[0-9]+|[0-9]+|flush)[ ]+of[ ]+(.*)$"))
        if is_unknown(vm) then
            return unknown
        end if
        dir = vm.groups[0]
        dist = vm.groups[1]
        rest = trim(vm.groups[2])

        lo = 1
        hi = 1
        if dist = "flush" then
            lo = 1
            hi = 0 - 1                      ' to the block edge
        else
            rm = match(dist, regex("^([0-9]+)-([0-9]+)$"))
            if not is_unknown(rm) then
                lo = number(rm.groups[0])
                hi = number(rm.groups[1])
            else
                om = match(dist, regex("^([0-9]+)-$"))
                if not is_unknown(om) then
                    lo = number(om.groups[0])
                    hi = 0 - 1
                else
                    um = match(dist, regex("^-([0-9]+)$"))
                    if not is_unknown(um) then
                        lo = 1
                        hi = number(um.groups[0])
                    else
                        lo = number(dist)
                        hi = lo
                    end if
                end if
            end if
        end if

        ' The anchor token is the leading "literal" or /regex/; anything after it
        ' is an inner locator applied to the target line. With none, the whole
        ' target line is the span.
        inner = ""
        tm = match(rest, regex("^(\"[^\"]*\"|/[^/]*/)[ ]*(.*)$"))
        tok = rest
        if not is_unknown(tm) then
            tok = tm.groups[0]
            inner = trim(tm.groups[1])
        end if
        return { dir: dir, lo: lo, hi: hi, token: tok, inner: inner }
    end function

    ' Resolve one field against a block of grid lines. Returns { val, why }.
    function _resolve_field(block, f, ctx)
        ' `first`/`last <type>` carries its own type; otherwise use `as <type>`.
        want_last = false
        ty = f.type
        loc = f.locator

        ' `flush` reads naturally in a horizontal locator and means exactly what
        ' `right of` already does — to the end of the line. Accepted so specs can
        ' say it, then dropped.
        loc = replace(loc, "right flush of ", "right of ")

        sm = match(loc, regex("^(first|last)[ ]+([A-Za-z_]+)[ ]*$"))
        if not is_unknown(sm) then
            if sm.groups[0] = "last" then
                want_last = true
            end if
            if ty = "" then
                ty = sm.groups[1]
            end if
        end if

        v = _parse_vertical(loc)
        if not is_unknown(v) then
            ' Find the anchor line, then walk the offset window from it.
            i = 0
            while i < count(block)
                hit = _find_token(block[i].text, v.token)
                if not is_unknown(hit) then
                    last_off = v.hi
                    if last_off < 0 then
                        last_off = count(block)
                    end if
                    d = v.lo
                    while d <= last_off
                        j = i + d
                        if v.dir = "up" then
                            j = i - d
                        end if
                        if j >= 0 then
                            if j < count(block) then
                                span = block[j].text
                                if v.inner != "" then
                                    r2 = _locate_in_line(span, v.inner)
                                    if r2.ok then
                                        span = r2.span
                                    end if
                                end if
                                blank = _is_blank(span)
                                if not blank then
                                    got = _convert(span, ty, want_last, ctx)
                                    if not is_unknown(got.val) then
                                        return got
                                    end if
                                end if
                            end if
                        end if
                        d = d + 1
                    end while
                end if
                i = i + 1
            end while
            return { val: unknown, why: "anchor-not-found" }
        end if

        why = "not-found"
        for each row in block
            r = _locate_in_line(row.text, loc)
            if r.ok then
                got = _convert(r.span, ty, want_last, ctx)
                if not is_unknown(got.val) then
                    return got
                end if
                if got.why != "" then
                    why = got.why
                end if
            end if
        end for
        ' Not found anywhere in the block: unknown, never a guess (§8).
        return { val: unknown, why: why }
    end function

    ' ------------------------------------------------------------- the walker

    function _line_matches(line, token)
        if token = "" then
            return false
        end if
        m = _find_token(line, token)
        return not is_unknown(m)
    end function

    ' Find the extents of every instance of `sec` inside [lo, hi) of the grid.
    ' A repeats-section with no `ends` runs to the next occurrence of its own
    ' `starts` pattern, or the end of the parent — which is what makes `Branch:`
    ' and `Teller:` work, since neither carries a terminator.
    function _find_instances(grid, lo, hi, sec)
        spans = []
        if sec.starts = "" then
            append(spans, { lo: lo, hi: hi })
            return spans
        end if
        i = lo
        while i < hi
            hit = _line_matches(grid[i].text, sec.starts)
            if hit then
                j = i + 1
                while j < hi
                    halt = false
                    if sec.ends != "" then
                        e = _line_matches(grid[j].text, sec.ends)
                        if e then
                            halt = true
                        end if
                    else
                        s = _line_matches(grid[j].text, sec.starts)
                        if s then
                            halt = true
                        end if
                    end if
                    if halt then
                        break
                    end if
                    j = j + 1
                end while
                append(spans, { lo: i, hi: j })
                if not sec.repeats then
                    return spans
                end if
                i = j
                continue
            end if
            i = i + 1
        end while
        return spans
    end function

    function _slice(grid, lo, hi)
        out = []
        i = lo
        while i < hi
            append(out, grid[i])
            i = i + 1
        end while
        return out
    end function

    ' Merge a section's `using` bindings over those inherited from its parent.
    ' A binding applies to the declaring section and everything nested inside it,
    ' and an inner one overrides an outer (§5).
    function _extend_usings(inherited, own)
        merged = { }
        for each k in keys(inherited)
            merged[k] = inherited[k]
        end for
        for each k in keys(own)
            merged[k] = own[k]
        end for
        return merged
    end function

    ' Build one record for one instance of a section. Returns { rec, diags }.
    '
    ' Diagnostics are collected OUT OF BAND rather than attached to the value:
    ' gBASIC's `unknown` is a bare singleton with no payload, and giving it one
    ' would change equality and serialization for every existing user of the NA
    ' policy. The reason is a fact about the parse event, not about the datum —
    ' two cells unknown for different reasons must still behave identically.
    ' So the cell stays `unknown` and the why travels alongside with a path,
    ' which is also what makes the authoring-time `inspect` summary possible.
    function _build_record(grid, lo, hi, sec, ctx, path)
        rec = { }
        diags = []
        block = _slice(grid, lo, hi)

        local_ctx = { types: ctx.types, usings: _extend_usings(ctx.usings, sec.usings) }

        for each f in sec.fields
            got = _resolve_field(block, f, local_ctx)
            rec[f.name] = got.val
            if is_unknown(got.val) then
                append(diags, { path: path + "." + f.name, reason: got.why, line: grid[lo].line })
            end if
        end for

        ' `rows:` — one record per remaining line, emitted as a frame (columns
        ' of equal length), which is what frame.bas consumes.
        if sec.has_rows then
            cols = { }
            for each rf in sec.rows
                cols[rf.name] = []
            end for
            ' The section's own `starts` line belongs to the section and is not
            ' offered to its rows, or the column heading becomes a data row.
            k = lo
            if sec.starts != "" then
                k = lo + 1
            end if
            ridx = 0
            while k < hi
                line = grid[k].text
                blank = _is_blank(line)
                if blank then
                    k = k + 1
                    continue
                end if

                ' A logical row is a BLOCK of lines, not necessarily one. With no
                ' `continue` pattern that block is always a single line, so this
                ' is behaviour-identical to before; with one, wrapped lines are
                ' absorbed into the record above. `_resolve_field` already takes a
                ' block, so every locator — including the vertical ones — works
                ' inside a multi-line record with no further change.
                first_line = grid[k].line
                one = [grid[k]]
                k = k + 1
                if sec.row_continue != "" then
                    while k < hi
                        nxt = grid[k].text
                        nblank = _is_blank(nxt)
                        if nblank then
                            break
                        end if
                        iscont = _line_matches(nxt, sec.row_continue)
                        if not iscont then
                            break
                        end if
                        append(one, grid[k])
                        k = k + 1
                    end while
                end if

                for each rf in sec.rows
                    got = _resolve_field(one, rf, local_ctx)
                    append(cols[rf.name], got.val)
                    if is_unknown(got.val) then
                        rp = path + ".rows[" + ridx + "]." + rf.name
                        append(diags, { path: rp, reason: got.why, line: first_line })
                    end if
                end for
                ridx = ridx + 1
            end while
            rec["rows"] = cols
        end if

        for each child in sec.sections
            spans = _find_instances(grid, lo, hi, child)
            if child.repeats then
                items = []
                idx = 0
                for each sp in spans
                    cp = path + "." + child.name + "[" + idx + "]"
                    sub = _build_record(grid, sp.lo, sp.hi, child, local_ctx, cp)
                    append(items, sub.rec)
                    for each d in sub.diags
                        append(diags, d)
                    end for
                    idx = idx + 1
                end for
                rec[child.name] = items
            else
                if count(spans) = 0 then
                    rec[child.name] = unknown
                    np = path + "." + child.name
                    append(diags, { path: np, reason: "section-not-found", line: grid[lo].line })
                else
                    cp = path + "." + child.name
                    sub = _build_record(grid, spans[0].lo, spans[0].hi, child, local_ctx, cp)
                    rec[child.name] = sub.rec
                    for each d in sub.diags
                        append(diags, d)
                    end for
                end if
            end if
        end for

        return { rec: rec, diags: diags }
    end function

    ' ------------------------------------------------------------ public API

    ' Parse `report_text` against `spec_text`. Returns a record; a section with
    ' `repeats` becomes a list of records, and `rows:` becomes a frame.
    function parse(report_text, spec_text)
        spec = ari_parse_spec(spec_text)
        if is_unknown(spec.root) then
            return { ok: false, message: "spec has no root section", value: unknown, diagnostics: [] }
        end if
        grid = _build_grid(report_text, spec.page)
        spans = _find_instances(grid, 0, count(grid), spec.root)
        if count(spans) = 0 then
            return { ok: false, message: "root section not found in report", value: unknown, diagnostics: [] }
        end if
        ctx = { types: spec.types, usings: { } }
        out = _build_record(grid, spans[0].lo, spans[0].hi, spec.root, ctx, spec.root.name)
        return { ok: true, message: "", value: out.rec, lines: count(grid), diagnostics: out.diags }
    end function

    ' Authoring-time advisory: summarize the diagnostics by reason so a spec
    ' author can see WHAT to declare rather than guessing.
    '
    ' This is where cross-row inference belongs — looking across instances is
    ' exactly how a person settles a DD/MM column, and doing it here gives the
    ' benefit without letting a guess into the parse path (see _date_in). Run it
    ' once, read the suggestion, write the declaration into the spec.
    function inspect(report_text, spec_text)
        r = parse(report_text, spec_text)
        if not r.ok then
            return r
        end if
        tally = { }
        for each d in r.diagnostics
            ' Collapse instance indices so `branches[0].opened` and
            ' `branches[1].opened` report as one field, which is the level a
            ' declaration is written at.
            gen = replace(d.path, regex("\\[[0-9]+\\]"), "[]")
            k = gen + " :: " + d.reason
            prior = tally[k]
            if is_unknown(prior) then
                tally[k] = 1
            else
                tally[k] = prior + 1
            end if
        end for
        findings = []
        for each k in keys(tally)
            hint = ""
            amb = contains(k, "ambiguous-date")
            if amb then
                hint = "declare `using date: dmy` (or mdy) on the enclosing section"
            end if
            append(findings, { what: k, count: tally[k], hint: hint })
        end for
        return { ok: true, message: "", value: r.value, lines: r.lines, diagnostics: r.diagnostics, findings: findings }
    end function

    ' Read a file and parse it. The read is deliberately plain: a missing file
    ' raises, matching file-builtin behaviour (§8).
    function import(path, spec_text)
        p(file) = path
        body = read_lines(p)
        return parse(join(body, "\n"), spec_text)
    end function

    ' Expose the cleaned grid on its own — the page-furniture pass is useful
    ' (and testable) independently of any spec.
    function clean_grid(report_text, spec_text)
        spec = ari_parse_spec(spec_text)
        grid = _build_grid(report_text, spec.page)
        out = []
        for each g in grid
            append(out, g.text)
        end for
        return out
    end function

end library

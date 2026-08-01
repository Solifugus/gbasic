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

    ' Convert an extracted span according to its declared type. `as <type>`
    ' DELIMITS as well as converts: the span handed in may carry neighbouring
    ' text, and the recognizer takes the token of that shape out of it.
    function _convert(span, ty, want_last)
        if ty = "" then
            return trim(span)
        end if
        if ty = "text" then
            return trim(span)
        end if
        if ty = "money" then
            return _money_in(span, want_last)
        end if
        if ty = "integer" then
            return _integer_in(span, want_last)
        end if
        if ty = "decimal" then
            return _decimal_in(span, want_last)
        end if
        return trim(span)
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
                 usings: { } }
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

            if starts_with(t, "rows:") then
                holder = _new_section("rows")
                inner = _parse_block(holder, lines, i + 1, child_end, ind + 1)
                sec.rows = inner.sec.fields
                sec.has_rows = true
                i = child_end
                continue
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

        return { page: page, root: root }
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

    ' Resolve one field against a block of grid lines.
    function _resolve_field(block, f)
        ' `first`/`last <type>` carries its own type; otherwise use `as <type>`.
        want_last = false
        ty = f.type
        sm = match(f.locator, regex("^(first|last)[ ]+([A-Za-z_]+)[ ]*$"))
        if not is_unknown(sm) then
            if sm.groups[0] = "last" then
                want_last = true
            end if
            if ty = "" then
                ty = sm.groups[1]
            end if
        end if

        for each row in block
            r = _locate_in_line(row.text, f.locator)
            if r.ok then
                v = _convert(r.span, ty, want_last)
                if not is_unknown(v) then
                    return v
                end if
            end if
        end for
        ' Not found anywhere in the block: unknown, never a guess (§8).
        return unknown
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

    ' Build one record for one instance of a section.
    function _build_record(grid, lo, hi, sec)
        rec = { }
        block = _slice(grid, lo, hi)

        for each f in sec.fields
            rec[f.name] = _resolve_field(block, f)
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
            while k < hi
                line = grid[k].text
                blank = _is_blank(line)
                if not blank then
                    one = [grid[k]]
                    for each rf in sec.rows
                        v = _resolve_field(one, rf)
                        append(cols[rf.name], v)
                    end for
                end if
                k = k + 1
            end while
            rec["rows"] = cols
        end if

        for each child in sec.sections
            spans = _find_instances(grid, lo, hi, child)
            if child.repeats then
                items = []
                for each sp in spans
                    append(items, _build_record(grid, sp.lo, sp.hi, child))
                end for
                rec[child.name] = items
            else
                if count(spans) = 0 then
                    rec[child.name] = unknown
                else
                    rec[child.name] = _build_record(grid, spans[0].lo, spans[0].hi, child)
                end if
            end if
        end for

        return rec
    end function

    ' ------------------------------------------------------------ public API

    ' Parse `report_text` against `spec_text`. Returns a record; a section with
    ' `repeats` becomes a list of records, and `rows:` becomes a frame.
    function parse(report_text, spec_text)
        spec = ari_parse_spec(spec_text)
        if is_unknown(spec.root) then
            return { ok: false, message: "spec has no root section", value: unknown }
        end if
        grid = _build_grid(report_text, spec.page)
        spans = _find_instances(grid, 0, count(grid), spec.root)
        if count(spans) = 0 then
            return { ok: false, message: "root section not found in report", value: unknown }
        end if
        v = _build_record(grid, spans[0].lo, spans[0].hi, spec.root)
        return { ok: true, message: "", value: v, lines: count(grid) }
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

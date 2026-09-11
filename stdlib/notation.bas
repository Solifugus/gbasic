' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' notation -- a TEXTUAL form that keeps every gBASIC type.
' See docs/text_serialization_design.md.
'
' THE GAP IT FILLS. `encode` is readable and REFUSES a date, money, a duration
' or a file; `serialize` keeps every type and is OPAQUE BINARY. Until now there
' was no form a person could open in an editor, read, and hand-edit that
' survived a typed value -- which is most of what a business record is made of.
'
' THE TYPE TAG GOES ON THE KEY SIDE, and that is not a preference. Both brace
' positions already mean something in gBASIC and they do not mean the same
' thing: BEFORE the separator is `x {date}= "..."`, THIS VALUE IS OF THIS TYPE;
' AFTER the value is a comparison lens, `name {caseless} = "joe"`. So
' `issued {date}: "2026-03-15"` is the language's own typed-assignment syntax
' with a colon where the equals goes, and a reader who knows one needs to learn
' nothing. Measured, the key-side form also costs 0 grammar conflicts where the
' value-side form costs 1.
'
' THE OUTPUT IS NOT gBASIC SOURCE and this library parses it itself. That is
' what makes the array element form affordable: adding it to gBASIC's own
' grammar was built and measured, and it BREAKS `{ x: 1 }` everywhere. And
' pasteability was never reachable anyway -- gBASIC refuses `\u{0}` in a
' literal (DOGFOOD; the AST stores a literal as a C string, so a literal NUL
' would be truncated at evaluation) while a record FIELD NAME may now contain
' one. This notation can spell every string gBASIC can hold, which is what
' keeps the round trip total.
'
' VERSION 1 DISCARDS COMMENTS. `'` to end of line is read and dropped, so a
' read-modify-write cycle does not preserve a reviewer's note. That is a
' deliberate first-increment limit rather than an oversight: preserving them is
' an API fork, not a detail -- `from_text` could no longer answer with a plain
' record. The ordinary loop is generate, review, read back, and that loses
' nothing.
library notation

    ' ---------------------------------------------------------------- shared

    ' gBASIC's own string escapes, plus the one it refuses. `\u{0}` is the only
    ' place this notation parts from the language, and it has to: an interior
    ' NUL is a byte gBASIC can HOLD and cannot SPELL.
    ' THE ESCAPES ARE BUILT FROM CHARACTERS, NOT WRITTEN AS LITERALS, and the
    ' reason is the limitation this library exists to route around: gBASIC
    ' refuses `\u{0}` in a literal, so the writer of that escape cannot spell
    ' its own output. `bs` is one backslash.
    function _escape(s)
        bs = chr(92)
        q = chr(34)
        out = []
        i = 0
        n = byte_count(s)
        while i < n
            b = byte_at(s, i)
            if b = 0 then
                append(out, bs + "u{0}")
            else
                if b = 34 then
                    append(out, bs + q)
                else
                    if b = 92 then
                        append(out, bs + bs)
                    else
                        if b = 10 then
                            append(out, bs + "n")
                        else
                            if b = 9 then
                                append(out, bs + "t")
                            else
                                ' EVERY OTHER CONTROL BYTE IS ESCAPED TOO. A
                                ' raw one round-trips perfectly and makes the
                                ' file hostile to the editor this format exists
                                ' to be opened in -- correctness is not the
                                ' whole requirement here.
                                if b < 32 or b = 127 then
                                    append(out, bs + "u{" + _hex2(b) + "}")
                                else
                                    append(out, from_bytes([b]))
                                end if
                            end if
                        end if
                    end if
                end if
            end if
            i = i + 1
        end while
        return q + join(out, "") + q
    end function

    function _hex2(b)
        digits = "0123456789ABCDEF"
        return mid(digits, floor(b / 16), 1) + mid(digits, b - (floor(b / 16) * 16), 1)
    end function

    function _is_ident_byte(b, first)
        if b >= 65 and b <= 90 then
            return true
        end if
        if b >= 97 and b <= 122 then
            return true
        end if
        if b = 95 then
            return true
        end if
        if not first and b >= 48 and b <= 57 then
            return true
        end if
        return false
    end function

    ' A key is written bare when it is a valid identifier and quoted otherwise.
    ' A record used as a map therefore mixes the two, which is the price of
    ' reading like gBASIC one line at a time.
    function _key(name)
        n = byte_count(name)
        if n = 0 then
            return _escape(name)
        end if
        i = 0
        while i < n
            if not _is_ident_byte(byte_at(name, i), i = 0) then
                return _escape(name)
            end if
            i = i + 1
        end while
        return name
    end function

    ' The tag a value needs, or "" when it needs none.
    '
    ' DATE, DATETIME AND TIME ARE ONE gBASIC TYPE and are NOT one value: a bare
    ' date does not equal the same date at midnight (measured), and `string`
    ' renders them differently. So the tag is chosen from the RENDERED form,
    ' which is the only thing that distinguishes them.
    function _tag_of(v)
        t = type(v)
        if t = "money" then
            return money.currency(v)
        end if
        if t = "datetime" then
            s = string(v)
            if not contains(s, "-") then
                return "time"
            end if
            if contains(s, " ") then
                return "datetime"
            end if
            return "date"
        end if
        if t = "duration" then
            return "duration"
        end if
        if t = "file" then
            return "file"
        end if
        if t = "directory" then
            return "dir"
        end if
        return ""
    end function

    ' The text a tagged value carries. MONEY COMES FROM `money.text`, NOT
    ' `string`: `string` rounds to the minor unit, so a sub-cent price of 3.459
    ' renders as 3.46 and the round trip is silently lossy. Trailing zeros are
    ' trimmed because `money.text` pads to the storage scale and 1234.560000 is
    ' not what anyone wants to read -- and a trailing zero in decimal changes
    ' no value.
    function _tagged_text(v)
        if type(v) = "money" then
            t = money.text(v)
            if contains(t, ".") then
                while ends_with(t, "0")
                    t = left(t, len(t) - 1)
                end while
                if ends_with(t, ".") then
                    t = left(t, len(t) - 1)
                end if
            end if
            return t
        end if
        return string(v)
    end function

    ' ---------------------------------------------------------------- encode

    function _indent(depth)
        return repeat("  ", depth)
    end function

    function _scalar(v)
        t = type(v)
        if is_nothing(v) then
            return "nothing"
        end if
        if is_unknown(v) then
            return "unknown"
        end if
        if t = "string" then
            return _escape(v)
        end if
        if t = "boolean" then
            if v then
                return "true"
            end if
            return "false"
        end if
        if t = "number" then
            return string(v)
        end if
        return _escape(string(v))
    end function

    ' Does every element share one tag? Then the array carries it once and the
    ' elements go bare -- the common case written short, which is the whole
    ' reason a default exists.
    ' IT RECURSES THROUGH NESTED ARRAYS, because the default cascades and the
    ' writer has to look as deep as the reader will. Without this a matrix of
    ' dates found no common tag at the top -- every element being an array --
    ' and the tag was computed one level down and then DROPPED, so the values
    ' came back as plain strings. Round-tripping is what caught it.
    '
    ' An empty nested array is NEUTRAL rather than a disagreement: it carries
    ' no element to contradict anything.
    function _common_tag(items)
        found = ""
        for each it in items
            here = ""
            if type(it) = "array" then
                if count(it) = 0 then
                    here = found
                else
                    here = _common_tag(it)
                end if
            else
                here = _tag_of(it)
            end if
            if len(here) = 0 then
                return ""
            end if
            if len(found) = 0 then
                found = here
            else
                if here != found then
                    return ""
                end if
            end if
        end for
        return found
    end function

    ' A DEFAULT WITH OVERRIDES, for the same-but-one case: `[ {USD}, {USD},
    ' {JPY} ]` is written `{USD}: [ "19.99", "3.459", {JPY}: "500" ]`.
    '
    ' It is only usable when EVERY element is a tagged scalar. A default
    ' applies to every untagged element, so a plain string or number among them
    ' has no way to opt out and would silently become money -- which is the
    ' reading the decoder is required to take.
    function _default_tag(items)
        if count(items) < 2 then
            return ""
        end if
        tags = []
        for each it in items
            tg = _tag_of(it)
            if len(tg) = 0 then
                return ""
            end if
            append(tags, tg)
        end for
        best = ""
        best_n = 0
        for each tg in tags
            n = 0
            for each other in tags
                if other = tg then
                    n = n + 1
                end if
            end for
            if n > best_n then
                best_n = n
                best = tg
            end if
        end for
        if best_n < 2 then
            return ""
        end if
        return best
    end function

    ' A value as text. `carried` is the tag the enclosing array already
    ' declared, so an element matching it is written bare.
    function _value(v, depth, carried)
        t = type(v)
        if t = "array" then
            ' A nested array with a default of its own DECLARES it here, in
            ' element position. Computing it and not writing it is the defect
            ' the round-trip tier found.
            mine = _common_tag(v)
            if len(mine) = 0 then
                mine = _default_tag(v)
            end if
            if len(mine) > 0 and mine != carried then
                return "{" + mine + "}: " + _array(v, depth, mine)
            end if
            return _array(v, depth, carried)
        end if
        if t = "record" then
            return _record(v, depth)
        end if
        tag = _tag_of(v)
        if len(tag) > 0 then
            if tag = carried then
                return _escape(_tagged_text(v))
            end if
            return "{" + tag + "}: " + _escape(_tagged_text(v))
        end if
        return _scalar(v)
    end function

    function _array(items, depth, carried)
        if count(items) = 0 then
            return "[]"
        end if
        ' A default DECLARED HERE replaces whatever the enclosing array
        ' carried; the cascade is what makes a matrix of dates write once.
        mine = _common_tag(items)
        if len(mine) = 0 then
            mine = _default_tag(items)
        end if
        inner = carried
        if len(mine) > 0 then
            inner = mine
        end if
        parts = []
        for each it in items
            append(parts, _value(it, depth + 1, inner))
        end for
        one = "[ " + join(parts, ", ") + " ]"
        if not contains(one, chr(10)) and len(one) + (depth * 2) <= 72 then
            return one
        end if
        pad = _indent(depth + 1)
        return "[" + chr(10) + pad + join(parts, "," + chr(10) + pad) + chr(10) + _indent(depth) + "]"
    end function

    function _record(r, depth)
        ks = keys(r)
        if count(ks) = 0 then
            return "{}"
        end if
        parts = []
        for each k in ks
            v = r[k]
            head = _key(k)
            tv = type(v)
            if tv = "array" then
                ' The array's own default, hoisted onto the key so the elements
                ' need no tag at all.
                at = _common_tag(v)
                if len(at) = 0 then
                    at = _default_tag(v)
                end if
                if len(at) > 0 then
                    append(parts, head + " {" + at + "}: " + _array(v, depth + 1, at))
                else
                    append(parts, head + ": " + _array(v, depth + 1, ""))
                end if
            else
                if tv = "record" then
                    append(parts, head + ": " + _record(v, depth + 1))
                else
                    tag = _tag_of(v)
                    if len(tag) > 0 then
                        append(parts, head + " {" + tag + "}: " + _escape(_tagged_text(v)))
                    else
                        append(parts, head + ": " + _scalar(v))
                    end if
                end if
            end if
        end for
        one = "{ " + join(parts, ", ") + " }"
        if not contains(one, chr(10)) and len(one) + (depth * 2) <= 72 then
            return one
        end if
        pad = _indent(depth + 1)
        return "{" + chr(10) + pad + join(parts, "," + chr(10) + pad) + chr(10) + _indent(depth) + "}"
    end function

    ' ---------------------------------------------------------------- decode

    ' Tokens carry their LINE AND COLUMN because this file is hand-edited: a
    ' misspelled currency or an unclosed brace has to be reported where it is,
    ' not merely reported.
    function _tokens(text)
        out = []
        i = 0
        n = byte_count(text)
        line = 1
        col = 1
        while i < n
            b = byte_at(text, i)
            if b = 10 then
                line = line + 1
                col = 1
                i = i + 1
            else
                if b = 32 or b = 9 or b = 13 then
                    i = i + 1
                    col = col + 1
                else
                    if b = 39 then
                        ' A COMMENT IS READ AND DROPPED. Version 1 does not
                        ' preserve them; see the header.
                        while i < n and byte_at(text, i) != 10
                            i = i + 1
                            col = col + 1
                        end while
                    else
                        if b = 34 then
                            r = _read_string(text, i, line, col)
                            append(out, { kind: "string", text: r.text, line: line, col: col })
                            col = col + (r.next - i)
                            i = r.next
                        else
                            if contains(["{", "}", "[", "]", ":", ","], from_bytes([b])) then
                                append(out, { kind: "punct", text: from_bytes([b]), line: line, col: col })
                                i = i + 1
                                col = col + 1
                            else
                                r = _read_word(text, i)
                                if r.next = i then
                                    error "notation: unexpected character '" + from_bytes([b]) + "' at line " + string(line) + ", column " + string(col)
                                end if
                                kind = "word"
                                if _looks_numeric(r.text) then
                                    kind = "number"
                                end if
                                append(out, { kind: kind, text: r.text, line: line, col: col })
                                col = col + (r.next - i)
                                i = r.next
                            end if
                        end if
                    end if
                end if
            end if
        end while
        append(out, { kind: "end", text: "", line: line, col: col })
        return out
    end function

    function _looks_numeric(w)
        b = byte_at(w, 0)
        if b >= 48 and b <= 57 then
            return true
        end if
        if b = 45 and byte_count(w) > 1 then
            return true
        end if
        return false
    end function

    ' A word: anything up to whitespace or punctuation. Deliberately generous,
    ' so `nan`, `-inf` and a bare number all arrive as one token and the parser
    ' decides what they are.
    function _read_word(text, at)
        n = byte_count(text)
        i = at
        piece = []
        while i < n
            b = byte_at(text, i)
            if b = 32 or b = 9 or b = 10 or b = 13 then
                break
            end if
            if contains(["{", "}", "[", "]", ":", ",", chr(34), "'"], from_bytes([b])) then
                break
            end if
            append(piece, from_bytes([b]))
            i = i + 1
        end while
        return { text: join(piece, ""), next: i }
    end function

    ' gBASIC's escapes, plus the codepoint-zero one -- the byte the language can
    ' hold and
    ' cannot spell, which this notation must be able to carry.
    function _read_string(text, at, line, col)
        n = byte_count(text)
        i = at + 1
        piece = []
        while i < n
            b = byte_at(text, i)
            if b = 34 then
                return { text: join(piece, ""), next: i + 1 }
            end if
            if b = 92 then
                if i + 1 >= n then
                    error "notation: unterminated escape at line " + string(line) + ", column " + string(col)
                end if
                e = byte_at(text, i + 1)
                if e = 110 then
                    append(piece, chr(10))
                    i = i + 2
                else
                    if e = 116 then
                        append(piece, chr(9))
                        i = i + 2
                    else
                        if e = 34 or e = 92 then
                            append(piece, from_bytes([e]))
                            i = i + 2
                        else
                            if e = 117 then
                                r = _read_codepoint(text, i + 2, line, col)
                                append(piece, r.text)
                                i = r.next
                            else
                                error "notation: unknown escape at line " + string(line) + ", column " + string(col)
                            end if
                        end if
                    end if
                end if
            else
                append(piece, from_bytes([b]))
                i = i + 1
            end if
        end while
        error "notation: unterminated string at line " + string(line) + ", column " + string(col)
    end function

    function _read_codepoint(text, at, line, col)
        n = byte_count(text)
        if at >= n or byte_at(text, at) != 123 then
            error "notation: " + chr(92) + "u must be followed by { at line " + string(line) + ", column " + string(col)
        end if
        i = at + 1
        digits = []
        while i < n and byte_at(text, i) != 125
            append(digits, from_bytes([byte_at(text, i)]))
            i = i + 1
        end while
        if i >= n then
            error "notation: unterminated " + chr(92) + "u{...} at line " + string(line) + ", column " + string(col)
        end if
        cp = number("0x" + join(digits, ""))
        if is_unknown(cp) then
            error "notation: bad codepoint at line " + string(line) + ", column " + string(col)
        end if
        ' THE WHOLE POINT OF THE ONE DIVERGENCE: codepoint 0 is a byte, not a
        ' refusal.
        if cp = 0 then
            return { text: chr(0), next: i + 1 }
        end if
        return { text: chr(cp), next: i + 1 }
    end function

    ' notation.to_text(value) -> text
    '
    ' THE ROOT IS A RECORD OR AN ARRAY. A bare typed value there has no field
    ' name to hang a tag on, and inventing a spelling for a case nobody needs
    ' is worse than refusing. `encode(5)` accepts one, so this is a deliberate
    ' asymmetry with a stated reason rather than an oversight.
    function to_text(v)
        t = type(v)
        if t = "record" then
            return _record(v, 0)
        end if
        if t = "array" then
            at = _common_tag(v)
            if len(at) = 0 then
                at = _default_tag(v)
            end if
            if len(at) > 0 then
                return "{" + at + "}: " + _array(v, 0, at)
            end if
            return _array(v, 0, "")
        end if
        error "notation: the document root must be a record or an array; got " + t
    end function

    ' ---------------------------------------------------------- the parser
    '
    ' Recursive descent over the token list. gBASIC has no closures and no
    ' references, so the cursor is threaded: every parse function takes an
    ' index and answers `{ v, at }`. That is a language fact, not a style.

    function _want(toks, at, text)
        tk = toks[at]
        if tk.kind = "punct" and tk.text = text then
            return at + 1
        end if
        error "notation: expected '" + text + "' at line " + string(tk.line) + ", column " + string(tk.col)
    end function

    ' A tag applied to text. Types are checked BEFORE currencies, and the order
    ' is the rule rather than a tie-break: `dir` is three letters like a
    ' currency code, so anything keyed on shape would misread it.
    function _apply(tag, text, line, col)
        ' THE MODIFIER'S OWN MESSAGE LOSES THE PLACE. `{date}` raising "expects
        ' an ISO-like date string" is true and useless in a hand-edited file of
        ' two hundred lines, so the raise is caught and re-raised WITH the line
        ' and column -- which is the whole reason tokens carry them.
        on error goto next
        v = _apply_raw(tag, text, line, col)
        failed = false
        m = ""
        if error then
            failed = true
            m = error.message
            error.clear()
        end if
        ' DISARM BEFORE RE-RAISING. `on error goto next` is FRAME-scoped, so a
        ' raise issued while it is still armed is caught by this very frame and
        ' never reaches the caller -- the refusal would vanish.
        on error stop
        if failed then
            error "notation: " + m + " at line " + string(line) + ", column " + string(col)
        end if
        return v
    end function

    function _apply_raw(tag, text, line, col)
        if tag = "date" then
            v {date}= text
            return v
        end if
        if tag = "datetime" then
            v {datetime}= text
            return v
        end if
        if tag = "time" then
            v {time}= text
            return v
        end if
        if tag = "file" then
            v {file}= text
            return v
        end if
        if tag = "dir" then
            v {dir}= text
            return v
        end if
        if tag = "duration" then
            return _duration(text, line, col)
        end if
        if len(tag) = 3 and tag = upper(tag) then
            return money.of(tag, text)
        end if
        error "unknown type tag '" + tag + "'"
    end function

    ' `duration(text)` does not exist, so a duration is parsed into components
    ' and composed arithmetically. Checking that this was reachable at all is
    ' what settled whether this library could be written in gBASIC.
    function _duration(text, line, col)
        parts = split(trim(text), " ")
        total = 0 * (1 seconds)
        i = 0
        while i + 1 < count(parts)
            n = number(parts[i])
            unit = parts[i + 1]
            if is_unknown(n) then
                error "'" + text + "' is not a duration"
            end if
            if unit = "second" or unit = "seconds" then
                total = total + (n * (1 seconds))
            else
                if unit = "minute" or unit = "minutes" then
                    total = total + (n * (1 minutes))
                else
                    if unit = "hour" or unit = "hours" then
                        total = total + (n * (1 hours))
                    else
                        if unit = "day" or unit = "days" then
                            total = total + (n * (1 days))
                        else
                            error "unknown duration unit '" + unit + "'"
                        end if
                    end if
                end if
            end if
            i = i + 2
        end while
        return total
    end function

    ' A bare word: the values with no quotes of their own.
    function _word_value(tk)
        w = tk.text
        if w = "nothing" then
            return nothing
        end if
        if w = "unknown" then
            return unknown
        end if
        if w = "true" then
            return true
        end if
        if w = "false" then
            return false
        end if
        if w = "inf" or w = "-inf" or w = "nan" or w = "-nan" then
            return number(w)
        end if
        n = number(w)
        if is_unknown(n) then
            error "notation: '" + w + "' is not a value at line " + string(tk.line) + ", column " + string(tk.col)
        end if
        return n
    end function

    ' `carried` is the default a surrounding array declared. It CASCADES into a
    ' nested array and STOPS at a record, because a record's fields are named
    ' and each can tag itself -- cascading in would try to turn a `name` into
    ' money.
    function _parse_value(toks, at, carried)
        tk = toks[at]
        if tk.kind = "punct" and tk.text = "{" then
            ' `{tag}: value` -- an element tag. A record opens `{` then a key
            ' then `:`; the tag closes its brace first, which is the one token
            ' that tells them apart.
            if toks[at + 1].kind = "word" and toks[at + 2].kind = "punct" and toks[at + 2].text = "}" then
                tag = toks[at + 1].text
                nxt = _want(toks, at + 3, ":")
                inner = _parse_value(toks, nxt, tag)
                return { v: inner.v, at: inner.at }
            end if
            return _parse_record(toks, at)
        end if
        if tk.kind = "punct" and tk.text = "[" then
            return _parse_array(toks, at, carried)
        end if
        if tk.kind = "string" then
            if len(carried) > 0 then
                return { v: _apply(carried, tk.text, tk.line, tk.col), at: at + 1 }
            end if
            return { v: tk.text, at: at + 1 }
        end if
        if tk.kind = "word" or tk.kind = "number" then
            ' A DEFAULT APPLIES TO EVERY UNTAGGED ELEMENT WHATEVER ITS SHAPE,
            ' including a bare number: an author who wrote the default said
            ' what they meant, and exempting some elements would make the rule
            ' depend on the data.
            if len(carried) > 0 then
                return { v: _apply(carried, tk.text, tk.line, tk.col), at: at + 1 }
            end if
            return { v: _word_value(tk), at: at + 1 }
        end if
        error "notation: expected a value at line " + string(tk.line) + ", column " + string(tk.col)
    end function

    function _parse_array(toks, at, carried)
        i = _want(toks, at, "[")
        items = []
        if toks[i].kind = "punct" and toks[i].text = "]" then
            return { v: items, at: i + 1 }
        end if
        looping = true
        while looping
            r = _parse_value(toks, i, carried)
            append(items, r.v)
            i = r.at
            tk = toks[i]
            if tk.kind = "punct" and tk.text = "," then
                i = i + 1
            else
                looping = false
            end if
        end while
        i = _want(toks, i, "]")
        return { v: items, at: i }
    end function

    function _parse_record(toks, at)
        i = _want(toks, at, "{")
        rec = {}
        if toks[i].kind = "punct" and toks[i].text = "}" then
            return { v: rec, at: i + 1 }
        end if
        looping = true
        while looping
            tk = toks[i]
            if tk.kind != "word" and tk.kind != "string" and tk.kind != "number" then
                error "notation: expected a field name at line " + string(tk.line) + ", column " + string(tk.col)
            end if
            name = tk.text
            i = i + 1
            tag = ""
            if toks[i].kind = "punct" and toks[i].text = "{" then
                if toks[i + 1].kind != "word" then
                    error "notation: expected a type tag at line " + string(toks[i].line) + ", column " + string(toks[i].col)
                end if
                tag = toks[i + 1].text
                i = _want(toks, i + 2, "}")
            end if
            i = _want(toks, i, ":")
            r = _parse_value(toks, i, tag)
            rec[name] = r.v
            i = r.at
            tk2 = toks[i]
            if tk2.kind = "punct" and tk2.text = "," then
                i = i + 1
            else
                looping = false
            end if
        end while
        i = _want(toks, i, "}")
        return { v: rec, at: i }
    end function

    ' notation.from_text(text) -> the value, raising on malformed input.
    function from_text(text)
        toks = _tokens(text)
        tk = toks[0]
        if tk.kind = "end" then
            error "notation: the document is empty; the root must be a record or an array"
        end if
        ' A root array may carry its own default: `{date}: [ ... ]`.
        if tk.kind = "punct" and tk.text = "{" then
            if toks[1].kind = "word" and toks[2].kind = "punct" and toks[2].text = "}" then
                r = _parse_value(toks, 0, "")
                if type(r.v) != "array" then
                    error "notation: the document root must be a record or an array"
                end if
                if toks[r.at].kind != "end" then
                    error "notation: trailing content at line " + string(toks[r.at].line) + ", column " + string(toks[r.at].col)
                end if
                return r.v
            end if
        end if
        if not (tk.kind = "punct" and (tk.text = "{" or tk.text = "[")) then
            error "notation: the document root must be a record or an array, at line " + string(tk.line) + ", column " + string(tk.col)
        end if
        r = _parse_value(toks, 0, "")
        if toks[r.at].kind != "end" then
            error "notation: trailing content at line " + string(toks[r.at].line) + ", column " + string(toks[r.at].col)
        end if
        return r.v
    end function

    ' notation.try_from_text(text) -> { ok, value, message }
    '
    ' The pair `try_decode` already establishes for JSON, and for the same
    ' reason one level along: a program reading a file it did not write -- or
    ' one a person has just edited -- needs the failure as a VALUE.
    function try_from_text(text)
        on error goto next
        v = from_text(text)
        if error then
            m = error.message
            error.clear()
            return { ok: false, value: nothing, message: m }
        end if
        return { ok: true, value: v, message: "" }
    end function
end library

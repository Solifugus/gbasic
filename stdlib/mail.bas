' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' mail.bas -- compose an RFC 5322 message (docs/mail_design.md).
'
' The composer is deliberately separate from the `smtp` transport, and it is
' pure gBASIC, so the whole of it is testable with NO NETWORK -- the same split
' `stdlib/web.bas` has against `webserver`. It produces the message TEXT and
' the ENVELOPE; `smtp.send` frames and delivers them.
'
'   m = mail.compose({
'       from: "alerts@example.com",
'       to: ["ops@example.com"],
'       subject: "Nightly run failed",
'       body: "3 of 41 tapes were rejected."
'   })
'   smtp.send({ host: "smtp.example.com", port: 587, security: "starttls",
'               username: u, password: p }, m)
'
' compose returns { from, recipients, message_id, text }. `recipients` is the
' envelope -- to + cc + bcc -- and `bcc` appears NOWHERE in `text`, which is
' the whole point of a blind copy.
'
' EVERY refusal here is header injection or a near relative: a CR or LF in a
' header value lets whoever supplied that text write headers of their own. It
' is REFUSED, never escaped, because a header value cannot contain a newline
' and so there is nothing for an escape to mean.
library mail

    ' ---------------------------------------------------------------- helpers

    ' Pure ASCII iff every codepoint encoded to one byte, which in UTF-8 happens
    ' only below 0x80. O(1) -- no scan.
    function is_ascii(text)
        return byte_count(text) = len(text)
    end function

    function _reject_control(field, text)
        if contains(text, chr(13)) or contains(text, chr(10)) then
            error ("mail.compose: " + field + " contains a line break, which would "
                   + "let it write its own headers; remove it -- headers cannot span lines")
        end if
        if contains(text, chr(0)) then
            error "mail.compose: " + field + " contains a NUL byte"
        end if
    end function

    ' RFC 2047 encoded-word. Base64 rather than quoted-printable: one rule, and
    ' correct for every input rather than for most.
    function encode_word(text)
        if is_ascii(text) then
            return text
        end if
        return "=?utf-8?B?" + base64_encode(text) + "?="
    end function

    function wrap_base64(text)
        encoded = base64_encode(text)
        out = []
        i = 0
        while i < len(encoded)
            append(out, mid(encoded, i, 76))
            i = i + 76
        end while
        return join(out, "\n")
    end function

    ' An address may be "user@host" or "Name <user@host>". Only the angle form
    ' carries a display name, and only the display name may need encoding.
    function address_parts(field, address)
        _reject_control(field, address)
        text = trim(address)
        if text = "" then
            error "mail.compose: " + field + " is empty"
        end if
        open_at = find(text, "<")
        if open_at != nothing then
            if not ends_with(text, ">") then
                error "mail.compose: " + field + " has '<' without a closing '>': " + text
            end if
            name = trim(mid(text, 0, open_at))
            addr = trim(mid(text, open_at + 1, len(text) - open_at - 2))
        else
            name = ""
            addr = text
        end if
        if find(addr, "@") = nothing then
            error "mail.compose: " + field + " is not an address (no '@'): " + addr
        end if
        if contains(addr, " ") then
            error "mail.compose: " + field + " has a space inside the address: " + addr
        end if
        return { name: name, address: addr }
    end function

    function format_address(field, address)
        p = address_parts(field, address)
        if p.name = "" then
            return p.address
        end if
        return encode_word(p.name) + " <" + p.address + ">"
    end function

    ' Accepts a single address or an array of them; always answers an array.
    function _as_list(value)
        if is_unknown(value) or value = nothing then
            return []
        end if
        if type(value) = "array" then
            return value
        end if
        return [value]
    end function

    ' ------------------------------------------------------------------ date

    function _two(n)
        if n < 10 then
            return "0" + string(n)
        end if
        return string(n)
    end function

    ' RFC 5322 date-time. The offset is the datetime's own, so a message sent at
    ' 09:00 local says 09:00 and names the zone rather than silently shifting.
    function format_date(when)
        ' weekday is ISO: 1 = Monday .. 7 = Sunday. A Sunday-first table read
        ' with it is right for Saturday alone, which is exactly the kind of
        ' coincidence that hides an off-by-one until a Tuesday.
        days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
        months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
        offset = floor(zone_offset(when).total_seconds / 60)   ' minutes east of UTC
        sign = "+"
        if offset < 0 then
            sign = "-"
            offset = 0 - offset
        end if
        return (days[when.weekday - 1] + ", " + _two(when.day) + " "
                + months[when.month - 1] + " " + string(when.year) + " "
                + _two(when.hour) + ":" + _two(when.minute) + ":" + _two(when.second)
                + " " + sign + _two(floor(offset / 60))
                + _two(offset - floor(offset / 60) * 60))
    end function

    ' -------------------------------------------------------------- boundary

    ' Verified absent from every part before use. A boundary that occurs in the
    ' content splits the message at the wrong place and the result still parses,
    ' which is the worst kind of wrong.
    function _make_boundary(parts)
        tries = 0
        while tries < 8
            candidate = "=_gbasic_" + hex_encode(random_bytes(12)) + "_="
            clash = false
            for each part in parts
                if contains(part, candidate) then
                    clash = true
                end if
            next
            if not clash then
                return candidate
            end if
            tries = tries + 1
        end while
        error "mail.compose: could not find an unused MIME boundary"
    end function

    ' --------------------------------------------------------------- headers

    ' Headers gBASIC writes itself. A caller supplying one of these would produce
    ' a message with the field twice, which clients resolve differently.
    function _structural_header(name)
        lowered = lower(name)
        reserved = ["from", "to", "cc", "bcc", "subject", "date", "message-id",
                    "mime-version", "content-type", "content-transfer-encoding"]
        return contains(reserved, lowered)
    end function

    function _header_token(name)
        if name = "" then
            return false
        end if
        i = 0
        while i < len(name)
            c = byte_at(name, i)
            ok = ((c >= 48 and c <= 57) or (c >= 65 and c <= 90)
                  or (c >= 97 and c <= 122) or c = 45)
            if not ok then
                return false
            end if
            i = i + 1
        end while
        return true
    end function

    ' ------------------------------------------------------------------ body

    ' 7bit verbatim when it is safe, base64 otherwise. Keeping ordinary mail
    ' readable on the wire is deliberate: it is what makes a capture legible.
    function _needs_base64(text)
        if not is_ascii(text) then
            return true
        end if
        if contains(text, chr(0)) then
            return true
        end if
        for each line in split(text, "\n")
            if len(line) >= 900 then
                return true
            end if
        next
        return false
    end function

    function _text_part(content, mime_type)
        ' The final newline is transport FRAMING for 7bit (it terminates the
        ' last line, and the part join supplies it) but CONTENT for base64.
        ' Left alone, the same body decodes with a trailing newline in one
        ' encoding and without it in the other -- a difference the author
        ' never asked for, decided by a rule they never see. Normalize once,
        ' then hand each path the form it needs.
        if not ends_with(content, "\n") then
            content = content + "\n"
        end if
        if _needs_base64(content) then
            return ("Content-Type: " + mime_type + "; charset=\"utf-8\"\n"
                    + "Content-Transfer-Encoding: base64\n\n" + wrap_base64(content))
        end if
        return ("Content-Type: " + mime_type + "; charset=\"utf-8\"\n"
                + "Content-Transfer-Encoding: 7bit\n\n"
                + mid(content, 0, len(content) - 1))
    end function

    function _attachment_part(item)
        if type(item) != "record" then
            error "mail.compose: each attachment must be a record with name and content"
        end if
        if not has(item, "name") or not has(item, "content") then
            error "mail.compose: each attachment needs a name and content"
        end if
        if type(item.content) != "string" then
            error "mail.compose: attachment '" + string(item.name) + "' content must be a string"
        end if
        _reject_control("attachment name", item.name)
        if has(item, "type") then
            kind = item.type
            _reject_control("attachment type", kind)
        else
            kind = "application/octet-stream"
        end if
        return ("Content-Type: " + kind + "; name=\"" + item.name + "\"\n"
                + "Content-Transfer-Encoding: base64\n"
                + "Content-Disposition: attachment; filename=\"" + item.name + "\"\n\n"
                + wrap_base64(item.content))
    end function

    function _multipart(subtype, parts)
        boundary = _make_boundary(parts)
        out = ["Content-Type: multipart/" + subtype + "; boundary=\"" + boundary + "\"\n"]
        for each part in parts
            append(out, "--" + boundary + "\n" + part + "\n")
        next
        append(out, "--" + boundary + "--")
        return join(out, "\n")
    end function

    ' --------------------------------------------------------------- compose

    function compose(message)
        if type(message) != "record" then
            error "mail.compose expects a record"
        end if
        known = ["from", "to", "cc", "bcc", "reply_to", "subject", "body",
                 "html", "attachments", "headers", "date"]
        for each field in keys(message)
            if not contains(known, field) then
                error ("mail.compose: unknown field '" + field + "'; expected one of "
                       + join(known, ", "))
            end if
        next
        if not has(message, "from") then
            error "mail.compose: from is required"
        end if

        from_header = format_address("from", message.from)
        from_address = address_parts("from", message.from).address

        to_list = _as_list(message["to"])
        cc_list = []
        if has(message, "cc") then
            cc_list = _as_list(message.cc)
        end if
        bcc_list = []
        if has(message, "bcc") then
            bcc_list = _as_list(message.bcc)
        end if
        if len(to_list) + len(cc_list) + len(bcc_list) = 0 then
            error "mail.compose: no recipients (to, cc and bcc are all empty)"
        end if

        ' The envelope. bcc belongs here and NOWHERE in the headers.
        recipients = []
        to_headers = []
        cc_headers = []
        for each a in to_list
            append(to_headers, format_address("to", a))
            append(recipients, address_parts("to", a).address)
        next
        for each a in cc_list
            append(cc_headers, format_address("cc", a))
            append(recipients, address_parts("cc", a).address)
        next
        for each a in bcc_list
            append(recipients, address_parts("bcc", a).address)
        next

        subject = ""
        if has(message, "subject") then
            subject = message.subject
            _reject_control("subject", subject)
        end if

        body = ""
        if has(message, "body") then
            body = message.body
        end if
        html = unknown
        if has(message, "html") then
            html = message.html
        end if
        attachments = []
        if has(message, "attachments") then
            attachments = _as_list(message.attachments)
        end if

        when = now()
        if has(message, "date") then
            when = message.date
        end if

        message_id = ("<" + hex_encode(random_bytes(16)) + "@"
                      + mid(from_address, find(from_address, "@") + 1,
                            len(from_address) - find(from_address, "@") - 1) + ">")

        headers = ["From: " + from_header]
        if len(to_headers) > 0 then
            append(headers, "To: " + join(to_headers, ", "))
        end if
        if len(cc_headers) > 0 then
            append(headers, "Cc: " + join(cc_headers, ", "))
        end if
        if has(message, "reply_to") then
            append(headers, "Reply-To: " + format_address("reply_to", message.reply_to))
        end if
        append(headers, "Subject: " + encode_word(subject))
        append(headers, "Date: " + format_date(when))
        append(headers, "Message-ID: " + message_id)
        append(headers, "MIME-Version: 1.0")

        if has(message, "headers") then
            extra = message.headers
            if type(extra) != "record" then
                error "mail.compose: headers must be a record"
            end if
            for each name in keys(extra)
                if not _header_token(name) then
                    error ("mail.compose: '" + name + "' is not a valid header name "
                           + "-- letters, digits and '-' only")
                end if
                if _structural_header(name) then
                    error ("mail.compose: '" + name + "' is written by mail.compose; "
                           + "supplying it would put the field in the message twice")
                end if
                value = string(extra[name])
                _reject_control("header " + name, value)
                append(headers, name + ": " + value)
            next
        end if

        ' Structure: alternative when there is html, mixed when there are
        ' attachments, mixed wrapping alternative when there are both.
        if not is_unknown(html) then
            content = _multipart("alternative", [_text_part(body, "text/plain"),
                                                _text_part(html, "text/html")])
        else
            content = _text_part(body, "text/plain")
        end if
        if len(attachments) > 0 then
            parts = [content]
            for each item in attachments
                append(parts, _attachment_part(item))
            next
            content = _multipart("mixed", parts)
        end if

        return { from: from_address,
                 recipients: recipients,
                 message_id: message_id,
                 text: join(headers, "\n") + "\n" + content + "\n" }
    end function

    ' ================================================================= parse
    '
    ' mail.parse(raw) reads a received message (book gap G59): the headers
    ' unfolded and decoded, every part with its type and its body decoded, the
    ' text a person would read, and what could not be read, by name.
    '
    '   m = mail.parse(read(f))
    '   m.from.address, m.subject, m.date, m.text, m.attachments, m.problems
    '
    ' A received message is someone else's text, so a part that cannot be read
    ' is not a reason to lose the rest: parse raises only when given something
    ' that is not a message's bytes, and every other failure is a sentence in
    ' `problems` with the part it was in. A text that could not be decoded is
    ' `unknown`, never a guess; its bytes stay in the part's `body`.
    '
    ' Which part is "the text" is the rule Python's email.message.get_body
    ' applies (the first text/plain that is not an attachment, depth first, and
    ' only the start of a multipart/related), so the two agree by construction
    ' and run_mail_parse.sh holds them to it.

    ' One line ending, LF or CRLF, removed from the end of a line.
    function _chomp(line)
        n = byte_count(line)
        if n > 0 and byte_at(line, n - 1) = 13 then
            return byte_slice(line, 0, n - 1)
        end if
        return line
    end function

    function _wsp(c)
        return c = 32 or c = 9
    end function

    ' Python's headerRE: a run of printable ASCII other than the colon, then a
    ' colon. The run may be empty, as there.
    function _header_line(line)
        n = byte_count(line)
        i = 0
        while i < n
            c = byte_at(line, i)
            if c = 58 then
                return true
            end if
            if c < 33 or c > 126 then
                return false
            end if
            i = i + 1
        end while
        return false
    end function

    function _hexval(c)
        if c >= 48 and c <= 57 then
            return c - 48
        end if
        if c >= 65 and c <= 70 then
            return c - 55
        end if
        if c >= 97 and c <= 102 then
            return c - 87
        end if
        return -1
    end function

    ' Text with every "=XX" replaced by its byte; a "=" not followed by two
    ' hex digits is kept as written (quopri does the same).
    function _unhex_equals(text)
        at = byte_find(text, "=")
        if is_nothing(at) then
            return text
        end if
        out = []
        from = 0
        n = byte_count(text)
        while not is_nothing(at)
            append(out, byte_slice(text, from, at - from))
            if at + 2 < n + 0 and _hexval(byte_at(text, at + 1)) >= 0 and _hexval(byte_at(text, at + 2)) >= 0 then
                append(out, from_bytes([_hexval(byte_at(text, at + 1)) * 16 + _hexval(byte_at(text, at + 2))]))
                from = at + 3
            else
                append(out, "=")
                from = at + 1
            end if
            at = byte_find(text, "=", from)
        end while
        append(out, byte_slice(text, from))
        return join(out, "")
    end function

    ' Quoted-printable (RFC 2045 6.7): trailing white space on a line is the
    ' transport's and goes, a "=" ending a line joins it to the next. A hard
    ' line break is kept as it was sent, CRLF or LF. (Python's decoder keeps
    ' the white space, and so does not read "= " at a line's end as a soft
    ' break; the RFC is followed here, and run_mail_parse.sh says so.)
    function _unquote_printable(body)
        out = []
        lines = split(body, "\n")
        last = count(lines) - 1
        for each line, i in lines
            t = _chomp(line)
            eol = "\n"
            if byte_count(t) < byte_count(line) then
                eol = chr(13) + "\n"
            end if
            n = byte_count(t)
            while n > 0 and _wsp(byte_at(t, n - 1))
                n = n - 1
            end while
            t = byte_slice(t, 0, n)
            if n > 0 and byte_at(t, n - 1) = 61 then
                append(out, _unhex_equals(byte_slice(t, 0, n - 1)))
            else
                append(out, _unhex_equals(t))
                if i < last then
                    append(out, eol)
                end if
            end if
        next
        return join(out, "")
    end function

    ' base64 with the line breaks and white space a message carries. (The
    ' padding some writers leave off base64_decode does without.)
    ' { bytes, problem }
    function _unbase64(body)
        if not has_builtin("base64_decode") then
            return { bytes: unknown, problem: "its base64 cannot be decoded: this gBASIC was built without libcrypto, which base64 comes with" }
        end if
        s = replace(replace(replace(replace(body, chr(13), ""), chr(10), ""), " ", ""), chr(9), "")
        on error goto next
        out = base64_decode(s)
        if error then
            why = error.message
            error.clear()
            return { bytes: unknown, problem: "its base64 does not decode (" + why + ")" }
        end if
        return { bytes: out, problem: "" }
    end function

    ' The charsets this reads, by the names mail labels them with.
    function _charset_name(charset)
        c = lower(trim(charset))
        if c = "" or c = "us-ascii" or c = "ascii" or c = "ansi_x3.4-1968" then
            return "us-ascii"
        end if
        if c = "utf-8" or c = "utf8" then
            return "utf-8"
        end if
        if c = "iso-8859-1" or c = "iso8859-1" or c = "iso_8859-1" or c = "latin1" or c = "latin-1" or c = "l1" then
            return "latin-1"
        end if
        if c = "windows-1252" or c = "cp1252" then
            return "windows-1252"
        end if
        if c = "utf-16" or c = "utf-16le" or c = "utf-16be" then
            return c
        end if
        return ""
    end function

    ' Bytes in a charset, as UTF-8 text. { text, problem }; text is unknown
    ' when the bytes cannot be read as labelled, and nothing is guessed.
    function _to_utf8(data, charset)
        name = _charset_name(charset)
        if name = "" then
            return { text: unknown, problem: ("its charset '" + charset + "' is not one gBASIC reads "
                                              + "(us-ascii, utf-8, iso-8859-1, windows-1252, utf-16); "
                                              + "its bytes are in the part's body") }
        end if
        if name = "us-ascii" or name = "utf-8" then
            bad = first_invalid_utf8(data)
            if not is_nothing(bad) then
                return { text: unknown, problem: ("it is labelled " + name + " and byte " + string(bad)
                                                  + " is not UTF-8; its bytes are in the part's body") }
            end if
            if name = "us-ascii" and byte_count(data) != len(data) then
                return { text: data, problem: "it is labelled us-ascii and holds UTF-8; it was read as UTF-8" }
            end if
            return { text: data, problem: "" }
        end if
        on error goto next
        out = transcode(data, name, "utf-8")
        if error then
            why = error.message
            error.clear()
            return { text: unknown, problem: "it does not read as " + charset + " (" + why + ")" }
        end if
        return { text: out, problem: "" }
    end function

    ' ------------------------------------------------------- encoded words

    ' One RFC 2047 encoded word, or unknown when the token is not one.
    ' { text, problem }
    function _encoded_word(token)
        n = byte_count(token)
        if n < 8 or byte_slice(token, 0, 2) != "=?" or byte_slice(token, n - 2, 2) != "?=" then
            return unknown
        end if
        inner = byte_slice(token, 2, n - 4)
        q1 = byte_find(inner, "?")
        if is_nothing(q1) then
            return unknown
        end if
        q2 = byte_find(inner, "?", q1 + 1)
        if is_nothing(q2) or q2 != q1 + 2 then
            return unknown
        end if
        charset = byte_slice(inner, 0, q1)
        star = byte_find(charset, "*")          ' RFC 2231's language suffix
        if not is_nothing(star) then
            charset = byte_slice(charset, 0, star)
        end if
        how = upper(byte_slice(inner, q1 + 1, 1))
        data = byte_slice(inner, q2 + 1)
        if charset = "" or contains(data, "?") or contains(data, " ") then
            return unknown
        end if
        if how = "B" then
            d = _unbase64(data)
            if d.problem != "" then
                return { text: token, problem: "the encoded word " + token + ": " + d.problem }
            end if
            raw = d.bytes
        else if how = "Q" then
            raw = _unhex_equals(replace(data, "_", " "))
        else
            return unknown
        end if
        r = _to_utf8(raw, charset)
        if is_unknown(r.text) then
            return { text: token, problem: "the encoded word " + token + ": " + r.problem }
        end if
        return { text: r.text, problem: "" }
    end function

    ' Header text as bytes with no line breaks: a valid UTF-8 value stays as
    ' it is (RFC 6532 allows it), anything else is read as Latin-1 and said.
    function _header_bytes(value)
        if valid_utf8(value) then
            return { text: value, problem: "" }
        end if
        return { text: transcode(value, "latin-1", "utf-8"),
                 problem: "holds bytes that are not UTF-8; they were read as Latin-1" }
    end function

    ' Unstructured text with its encoded words decoded. White space between
    ' two encoded words is dropped (RFC 2047 6.2); every other run is kept.
    ' { text, problems }
    function _decode_words(value)
        problems = []
        if is_nothing(byte_find(value, "=?")) then
            h = _header_bytes(value)
            if h.problem != "" then
                append(problems, h.problem)
            end if
            return { text: h.text, problems: problems }
        end if
        ' tokens alternate: white space runs and the words between them
        out = []
        pending_space = ""
        last_encoded = false
        n = byte_count(value)
        i = 0
        while i < n
            j = i
            if _wsp(byte_at(value, i)) then
                while j < n and _wsp(byte_at(value, j))
                    j = j + 1
                end while
                pending_space = pending_space + byte_slice(value, i, j - i)
            else
                while j < n and not _wsp(byte_at(value, j))
                    j = j + 1
                end while
                token = byte_slice(value, i, j - i)
                w = _encoded_word(token)
                if not is_unknown(w) and w.problem = "" then
                    if not last_encoded then
                        append(out, pending_space)
                    end if
                    append(out, w.text)
                    last_encoded = true
                else
                    if not is_unknown(w) then
                        append(problems, w.problem)
                    end if
                    h = _header_bytes(token)
                    if h.problem != "" then
                        append(problems, h.problem)
                    end if
                    append(out, pending_space)
                    append(out, h.text)
                    last_encoded = false
                end if
                pending_space = ""
            end if
            i = j
        end while
        append(out, pending_space)
        return { text: join(out, ""), problems: problems }
    end function

    ' --------------------------------------------------------------- headers

    ' The header block of an entity and where its body begins.
    ' { headers: [{ name, raw }], body, problems }
    function _split_head(raw, top)
        headers = []
        problems = []
        n = byte_count(raw)
        pos = 0
        first = true
        body_at = n
        while pos < n
            nl = byte_find(raw, "\n", pos)
            if is_nothing(nl) then
                line = _chomp(byte_slice(raw, pos))
                next_pos = n
            else
                line = _chomp(byte_slice(raw, pos, nl - pos))
                next_pos = nl + 1
            end if
            if line = "" then
                body_at = next_pos
                break
            end if
            if first and top and starts_with(line, "From ") then
                ' an mbox envelope line, not a header
                first = false
                pos = next_pos
                continue
            end if
            first = false
            if _wsp(byte_at(line, 0)) and count(headers) > 0 then
                last = count(headers) - 1
                headers[last].raw = headers[last].raw + line
            else if _header_line(line) then
                c = byte_find(line, ":")
                append(headers, { name: byte_slice(line, 0, c), raw: byte_slice(line, c + 1) })
            else
                append(problems, "a line in the headers is not a header (" + _show(line)
                                 + "); the body is taken to begin there")
                body_at = pos
                break
            end if
            pos = next_pos
        end while
        if pos >= n then
            body_at = n
        end if
        return { headers: headers, body: byte_slice(raw, body_at), problems: problems }
    end function

    ' A line as a problem quotes it: short, and with no bytes that would let
    ' a received message write into whatever reads the problems.
    function _show(line)
        s = line
        if byte_count(s) > 60 then
            s = byte_slice(s, 0, 60) + "..."
        end if
        if not valid_utf8(s) then
            s = transcode(s, "latin-1", "utf-8")
        end if
        return "'" + replace(replace(s, chr(13), " "), chr(9), " ") + "'"
    end function

    ' A header's value as a reader wants it: unfolded, leading white space
    ' gone, trailing white space gone.
    function _value(raw)
        v = replace(replace(raw, chr(13), ""), chr(10), "")
        n = byte_count(v)
        i = 0
        while i < n and _wsp(byte_at(v, i))
            i = i + 1
        end while
        while n > i and _wsp(byte_at(v, n - 1))
            n = n - 1
        end while
        return byte_slice(v, i, n - i)
    end function

    ' The first header called name, unfolded but not decoded, or unknown.
    function _raw_header(headers, name)
        for each h in headers
            if lower(h.name) = name then
                return _value(h.raw)
            end if
        next
        return unknown
    end function

    ' mail.header(m, name): the first header of a message or part with that
    ' name (any case), decoded, or unknown when there is none.
    function header(m, name)
        want = lower(name)
        for each h in m.headers
            if lower(h.name) = want then
                return h.value
            end if
        next
        return unknown
    end function

    ' mail.headers_named(m, name): every header with that name, in order.
    function headers_named(m, name)
        want = lower(name)
        out = []
        for each h in m.headers
            if lower(h.name) = want then
                append(out, h.value)
            end if
        next
        return out
    end function

    ' ------------------------------------------------------------ parameters

    ' Content-Type and Content-Disposition: a value and its parameters, with
    ' RFC 2231's continuations and charsets joined. { value, params, problems }
    function _parameters(text)
        problems = []
        n = byte_count(text)
        semi = byte_find(text, ";")
        if is_nothing(semi) then
            return { value: lower(_value(text)), params: {}, problems: problems }
        end if
        value = lower(_value(byte_slice(text, 0, semi)))
        found = []
        i = semi + 1
        while i < n
            while i < n and (_wsp(byte_at(text, i)) or byte_at(text, i) = 59)
                i = i + 1
            end while
            if i >= n then
                break
            end if
            j = i
            while j < n and byte_at(text, j) != 61 and byte_at(text, j) != 59
                j = j + 1
            end while
            name = lower(_value(byte_slice(text, i, j - i)))
            if j >= n or byte_at(text, j) = 59 then
                ' a parameter with no value is skipped, as Python skips it
                i = j
                continue
            end if
            j = j + 1
            while j < n and _wsp(byte_at(text, j))
                j = j + 1
            end while
            quoted = false
            if j < n and byte_at(text, j) = 34 then
                quoted = true
                parts = []
                k = j + 1
                closed = false
                while k < n
                    c = byte_at(text, k)
                    if c = 92 then
                        ' an escape; one with nothing after it is dropped, as
                        ' Python drops it (and as the scan below needs: it
                        ' stops at every backslash)
                        append(parts, byte_slice(text, k + 1, 1))
                        k = k + 2
                    else if c = 34 then
                        closed = true
                        k = k + 1
                        break
                    else
                        e = k
                        while e < n and byte_at(text, e) != 92 and byte_at(text, e) != 34
                            e = e + 1
                        end while
                        append(parts, byte_slice(text, k, e - k))
                        k = e
                    end if
                end while
                if not closed then
                    append(problems, "the parameter " + name + " has no closing quote")
                end if
                v = join(parts, "")
                while k < n and byte_at(text, k) != 59
                    k = k + 1
                end while
                i = k
            else
                k = j
                while k < n and byte_at(text, k) != 59
                    k = k + 1
                end while
                v = _value(byte_slice(text, j, k - j))
                i = k
            end if
            append(found, { name: name, value: v, quoted: quoted })
        end while
        ' RFC 2231: name*=charset'lang'%XX, and name*0, name*1*, ... in order
        params = {}
        sections = {}
        for each p in found
            star = find(p.name, "*")
            if is_nothing(star) then
                if not has(params, p.name) then
                    params[p.name] = p.value
                end if
            else
                base = left(p.name, star)
                rest = mid(p.name, star + 1, len(p.name) - star - 1)
                extended = ends_with(rest, "*") or rest = ""
                if ends_with(rest, "*") then
                    rest = left(rest, len(rest) - 1)
                end if
                idx = 0
                if rest != "" then
                    if is_unknown(match(rest, regex("^[0-9]+$"))) then
                        append(problems, "the parameter " + p.name + " is not an RFC 2231 section")
                        continue
                    end if
                    idx = number(rest)
                end if
                if not has(sections, base) then
                    sections[base] = []
                end if
                list = sections[base]
                append(list, { number: idx, value: p.value, extended: extended })
                sections[base] = list
            end if
        next
        for each base in keys(sections)
            list = sort(sections[base], { by: "number" })
            charset = ""
            pieces = []
            for each s, k in list
                v = s.value
                if s.extended then
                    if k = 0 then
                        q1 = byte_find(v, "'")
                        q2 = unknown
                        if not is_nothing(q1) then
                            q2 = byte_find(v, "'", q1 + 1)
                        end if
                        if not is_nothing(q1) and not is_unknown(q2) and not is_nothing(q2) then
                            charset = byte_slice(v, 0, q1)
                            v = byte_slice(v, q2 + 1)
                        end if
                    end if
                    v = _unhex_equals(replace(v, "%", "="))
                end if
                append(pieces, v)
            next
            joined = join(pieces, "")
            if charset != "" then
                r = _to_utf8(joined, charset)
                if is_unknown(r.text) then
                    append(problems, "the parameter " + base + ": " + r.problem)
                    joined = transcode(joined, "latin-1", "utf-8")
                else
                    joined = r.text
                end if
            end if
            params[base] = joined
        next
        ' a value written as encoded words, which RFC 2047 forbids and mailers
        ' send anyway; Python decodes it, so this does
        for each k in keys(params)
            if not is_nothing(byte_find(params[k], "=?")) then
                d = _decode_words(params[k])
                params[k] = d.text
            else if not valid_utf8(params[k]) then
                params[k] = transcode(params[k], "latin-1", "utf-8")
            end if
        next
        return { value: value, params: params, problems: problems }
    end function

    ' ------------------------------------------------------------- addresses

    ' An address list (From, To, Cc, Reply-To), as [{ name, address }]: the
    ' display name decoded, comments dropped, groups opened, Python's rules.
    ' Words with no "@" and no angle brackets are not an address and are
    ' named in `skipped` rather than returned as one. { list, skipped }
    function _addresses(text)
        out = []
        skipped = []
        n = byte_count(text)
        words = []          ' the phrase being read, word by word
        bare = []           ' the same, as written, for a bare addr-spec
        angle = unknown
        i = 0
        while i <= n
            c = -1
            if i < n then
                c = byte_at(text, i)
            end if
            if c = -1 or c = 44 or c = 59 then
                ' end of a mailbox (a comma, a group's end, the end)
                if not is_unknown(angle) then
                    append(out, { name: _phrase(words), address: angle })
                else if count(bare) > 0 then
                    a = join(bare, "")
                    if contains(a, "@") then
                        append(out, { name: "", address: a })
                    else
                        append(skipped, a)
                    end if
                end if
                words = []
                bare = []
                angle = unknown
                i = i + 1
            else if _wsp(c) or c = 13 or c = 10 then
                i = i + 1
            else if c = 40 then
                ' a comment, which may nest
                depth = 1
                i = i + 1
                while i < n and depth > 0
                    d = byte_at(text, i)
                    if d = 92 then
                        i = i + 1
                    else if d = 40 then
                        depth = depth + 1
                    else if d = 41 then
                        depth = depth - 1
                    end if
                    i = i + 1
                end while
            else if c = 34 then
                k = i + 1
                parts = []
                while k < n and byte_at(text, k) != 34
                    if byte_at(text, k) = 92 and k + 1 < n then
                        k = k + 1
                    end if
                    append(parts, byte_slice(text, k, 1))
                    k = k + 1
                end while
                q = join(parts, "")
                append(words, { text: q, quoted: true })
                append(bare, "\"" + q + "\"")
                i = k + 1
            else if c = 60 then
                k = byte_find(text, ">", i)
                if is_nothing(k) then
                    k = n
                end if
                spec = byte_slice(text, i + 1, k - i - 1)
                angle = replace(replace(replace(spec, " ", ""), chr(9), ""), chr(13) + chr(10), "")
                ' an obsolete route (<@a,@b:x@y>) is dropped
                colon = byte_find(angle, ":")
                if starts_with(angle, "@") and not is_nothing(colon) then
                    angle = byte_slice(angle, colon + 1)
                end if
                i = k + 1
            else if c = 58 then
                ' a group's name ends here; its members follow
                words = []
                bare = []
                i = i + 1
            else
                k = i
                while k < n
                    d = byte_at(text, k)
                    if _wsp(d) or d = 44 or d = 59 or d = 40 or d = 34 or d = 60 or d = 58 or d = 13 or d = 10 then
                        break
                    end if
                    k = k + 1
                end while
                atom = byte_slice(text, i, k - i)
                append(words, { text: atom, quoted: false })
                append(bare, atom)
                i = k
            end if
        end while
        return { list: out, skipped: skipped }
    end function

    ' A display name: its words joined by one space, encoded words decoded,
    ' and adjacent encoded words joined with none.
    function _phrase(words)
        out = []
        last_encoded = false
        for each w in words
            decoded = unknown
            if not w.quoted then
                decoded = _encoded_word(w.text)
            end if
            if not is_unknown(decoded) and decoded.problem = "" then
                if count(out) > 0 and not last_encoded then
                    append(out, " ")
                end if
                append(out, decoded.text)
                last_encoded = true
            else
                if count(out) > 0 then
                    append(out, " ")
                end if
                append(out, _header_bytes(w.text).text)
                last_encoded = false
            end if
        next
        return join(out, "")
    end function

    ' Message-IDs in a header (In-Reply-To, References), in order.
    function _ids(text)
        out = []
        if is_unknown(text) then
            return out
        end if
        for each m in match_all(text, regex("<[^<>]*>"))
            append(out, replace(m.text, " ", ""))
        next
        return out
    end function

    ' ------------------------------------------------------------------ date

    function _month_number(name)
        months = ["jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"]
        for each m, i in months
            if lower(name) = m then
                return i + 1
            end if
        next
        return 0
    end function

    ' RFC 5322 date-time, with its obsolete forms (two-digit years, zone
    ' names, no seconds), as the instant in UTC (as {datetime} reads a text
    ' with an offset) and the writer's offset. { at, offset, problem }
    function _date(text)
        m = match(text, regex(("^\\s*(?:[A-Za-z]+\\s*,\\s*)?([0-9]{1,2})\\s+([A-Za-z]+)\\s+([0-9]{2,4})\\s+"
                               + "([0-9]{1,2}):([0-9]{2})(?::([0-9]{2}))?\\s*([+-][0-9]{4}|[A-Za-z]+)?\\s*(?:\\(.*\\))?\\s*$")))
        if is_unknown(m) then
            return { at: unknown, offset: unknown, problem: "the Date header is not an RFC 5322 date: " + _show(text) }
        end if
        g = m.groups
        month = _month_number(g[1])
        if month = 0 then
            return { at: unknown, offset: unknown, problem: "the Date header names no month: " + _show(text) }
        end if
        year = number(g[2])
        if len(g[2]) = 2 then
            if year < 50 then
                year = year + 2000
            else
                year = year + 1900
            end if
        else if len(g[2]) = 3 then
            year = year + 1900
        end if
        seconds = "00"
        if not is_unknown(g[5]) then
            seconds = g[5]
        end if
        zone = g[6]
        if is_unknown(zone) then
            return { at: unknown, offset: unknown, problem: "the Date header has no zone, so it names no instant: " + _show(text) }
        end if
        zones = { ut: "+0000", gmt: "+0000", z: "+0000", est: "-0500", edt: "-0400",
                  cst: "-0600", cdt: "-0500", mst: "-0700", mdt: "-0600", pst: "-0800", pdt: "-0700" }
        if not starts_with(zone, "+") and not starts_with(zone, "-") then
            if not has(zones, lower(zone)) then
                return { at: unknown, offset: unknown, problem: "the Date header's zone '" + zone + "' is not one RFC 5322 names" }
            end if
            zone = zones[lower(zone)]
        end if
        hour = number(g[3])
        t = (string(year) + "-" + _two(month) + "-" + _two(number(g[0])) + "T" + _two(hour) + ":"
             + g[4] + ":" + seconds + left(zone, 3) + ":" + right(zone, 2))
        at = try {datetime}t
        if is_nothing(at) then
            return { at: unknown, offset: unknown, problem: "the Date header is not a date that exists: " + _show(text) }
        end if
        minutes = number(mid(zone, 1, 2)) * 60 + number(right(zone, 2))
        if starts_with(zone, "-") then
            minutes = 0 - minutes
        end if
        return { at: at, offset: (1 minute) * minutes, problem: "" }
    end function

    ' ------------------------------------------------------------- entities

    ' The parts of a multipart body, split at its boundary (RFC 2046 5.1.1).
    ' The line break before a delimiter belongs to the delimiter. { parts,
    ' problems }
    function _split_parts(body, boundary)
        delim = "--" + boundary
        dn = byte_count(delim)
        n = byte_count(body)
        parts = []
        problems = []
        start = -1          ' where the current part's content begins
        closed = false
        at = byte_find(body, delim)
        while not is_nothing(at)
            ok = at = 0
            if not ok then
                ok = byte_at(body, at - 1) = 10
            end if
            eol = byte_find(body, "\n", at + dn)
            if is_nothing(eol) then
                eol = n
            end if
            rest = _chomp(byte_slice(body, at + dn, eol - at - dn))
            closing = starts_with(rest, "--")
            if closing then
                rest = byte_slice(rest, 2)
            end if
            if ok then
                k = 0
                while k < byte_count(rest)
                    if not _wsp(byte_at(rest, k)) then
                        ok = false
                        break
                    end if
                    k = k + 1
                end while
            end if
            if ok then
                if start >= 0 then
                    e = at
                    if e > start then
                        e = e - 1
                        if e > start and byte_at(body, e - 1) = 13 then
                            e = e - 1
                        end if
                    end if
                    append(parts, byte_slice(body, start, e - start))
                end if
                if closing then
                    closed = true
                    break
                end if
                start = eol + 1
            end if
            at = byte_find(body, delim, at + 1)
        end while
        if start < 0 then
            append(problems, "no line begins its boundary, so it has no parts")
        else if not closed then
            if start < n then
                append(parts, byte_slice(body, start))
            end if
            append(problems, "it ends without its closing boundary; the last part runs to the end")
        end if
        return { parts: parts, problems: problems }
    end function

    function _note(problems, path, list)
        for each p in list
            if path = "" then
                append(problems, p)
            else
                append(problems, "part " + path + ": " + p)
            end if
        next
        return problems
    end function

    ' One entity (a message or a part): its headers, its type, and either its
    ' children or its decoded body. depth bounds the nesting.
    function _entity(raw, depth, default_type, path, top)
        problems = []
        head = _split_head(raw, top)
        problems = _note(problems, path, head.problems)
        headers = []
        for each h in head.headers
            d = _decode_words(_value(h.raw))
            for each p in d.problems
                append(problems, _where(path) + "header " + h.name + ": " + p)
            next
            append(headers, { name: h.name, value: d.text })
        next
        e = { path: path, headers: headers, type: default_type, params: {}, charset: unknown,
              disposition: "", filename: unknown, encoding: "7bit", content_id: unknown,
              body: "", text: unknown, children: unknown, message: unknown,
              raw_headers: head.headers }
        ct = _raw_header(head.headers, "content-type")
        if not is_unknown(ct) then
            p = _parameters(ct)
            problems = _note(problems, path, p.problems)
            if is_unknown(match(p.value, regex("^[a-z0-9!#$&^_.+-]+/[a-z0-9!#$&^_.+-]+$"))) then
                append(problems, _where(path) + "its Content-Type " + _show(ct) + " is not a type; it is read as text/plain")
                e.type = "text/plain"
            else
                e.type = p.value
                e.params = p.params
            end if
        end if
        if has(e.params, "charset") then
            e.charset = lower(e.params.charset)
        end if
        cd = _raw_header(head.headers, "content-disposition")
        dparams = {}
        if not is_unknown(cd) then
            p = _parameters(cd)
            problems = _note(problems, path, p.problems)
            e.disposition = p.value
            dparams = p.params
        end if
        if has(dparams, "filename") then
            e.filename = dparams.filename
        else if has(e.params, "name") then
            e.filename = e.params.name
        end if
        te = _raw_header(head.headers, "content-transfer-encoding")
        if not is_unknown(te) then
            e.encoding = lower(te)
        end if
        cid = _raw_header(head.headers, "content-id")
        if not is_unknown(cid) then
            e.content_id = cid
        end if
        body = head.body
        e.body = body
        main = left(e.type, find(e.type, "/"))
        if main = "multipart" then
            if not has(e.params, "boundary") or e.params.boundary = "" then
                append(problems, _where(path) + "it is " + e.type + " with no boundary, so its parts cannot be found")
                e.children = []
                e.problems = problems
                return e
            end if
            if depth >= 32 then
                append(problems, _where(path) + "it is nested 32 deep; nothing inside it was read")
                e.children = []
                e.problems = problems
                return e
            end if
            pieces = _split_parts(body, e.params.boundary)
            problems = _note(problems, path, pieces.problems)
            child_type = "text/plain"
            if e.type = "multipart/digest" then
                child_type = "message/rfc822"
            end if
            children = []
            for each part, k in pieces.parts
                cpath = string(k + 1)
                if path != "" then
                    cpath = path + "." + cpath
                end if
                c = _entity(part, depth + 1, child_type, cpath, false)
                for each p in c.problems
                    append(problems, p)
                next
                c.problems = []
                append(children, c)
            next
            e.children = children
            e.problems = problems
            return e
        end if
        ' a leaf: undo the transfer encoding
        if e.encoding = "base64" then
            d = _unbase64(body)
            if d.problem != "" then
                append(problems, _where(path) + d.problem + "; its body is as sent")
            else
                e.body = d.bytes
            end if
        else if e.encoding = "quoted-printable" then
            e.body = _unquote_printable(body)
        else if e.encoding != "7bit" and e.encoding != "8bit" and e.encoding != "binary" then
            append(problems, _where(path) + "its Content-Transfer-Encoding '" + e.encoding
                             + "' is not one MIME defines; its body is as sent")
        end if
        if main = "text" then
            r = _to_utf8(e.body, default(e.charset, "us-ascii"))
            if r.problem != "" then
                append(problems, _where(path) + r.problem)
            end if
            if not is_unknown(r.text) then
                e.text = replace(r.text, chr(13) + chr(10), "\n")
            end if
        else if e.type = "message/rfc822" or e.type = "message/global" then
            if depth >= 32 then
                append(problems, _where(path) + "it is nested 32 deep; the message inside was not read")
            else
                inner = _entity(e.body, depth + 1, "text/plain", "", false)
                e.message = _message(inner)
                for each p in e.message.problems
                    append(problems, _where(path) + "the message inside: " + p)
                next
            end if
        end if
        e.problems = problems
        return e
    end function

    function _where(path)
        if path = "" then
            return ""
        end if
        return "part " + path + ": "
    end function

    ' The leaves of an entity, in order.
    function _leaves(e, out)
        if is_unknown(e.children) then
            append(out, _part(e))
            return out
        end if
        for each c in e.children
            out = _leaves(c, out)
        next
        return out
    end function

    ' A part as the caller sees it.
    function _part(e)
        path = e.path
        if path = "" then
            path = "1"
        end if
        return { path: path, type: e.type, params: e.params, charset: e.charset,
                 disposition: e.disposition, filename: e.filename, encoding: e.encoding,
                 content_id: e.content_id, headers: e.headers, body: e.body, text: e.text,
                 flowed: _flowed(e), message: e.message }
    end function

    function _flowed(e)
        f = e.params["format"]
        return e.type = "text/plain" and not is_unknown(f) and lower(f) = "flowed"
    end function

    ' The body a reader of `subtype` would be shown: Python's get_body.
    function _find_body(e, subtype)
        if e.disposition = "attachment" then
            return unknown
        end if
        slash = find(e.type, "/")
        main = left(e.type, slash)
        sub = mid(e.type, slash + 1, len(e.type) - slash - 1)
        if main = "text" then
            if sub = subtype then
                return e
            end if
            return unknown
        end if
        if main != "multipart" or is_unknown(e.children) then
            return unknown
        end if
        if sub != "related" then
            for each c in e.children
                r = _find_body(c, subtype)
                if not is_unknown(r) then
                    return r
                end if
            next
            return unknown
        end if
        if count(e.children) = 0 then
            return unknown
        end if
        candidate = e.children[0]
        start = e.params["start"]
        if not is_unknown(start) then
            for each c in e.children
                if not is_unknown(c.content_id) and c.content_id = start then
                    candidate = c
                    break
                end if
            next
        end if
        return _find_body(candidate, subtype)
    end function

    ' The record parse answers, for a message (or one inside a message).
    function _message(e)
        problems = e.problems
        hs = e.raw_headers
        from = unknown
        f = _raw_header(hs, "from")
        if not is_unknown(f) then
            got = _addresses(f)
            for each w in got.skipped
                append(problems, "the From header holds " + _show(w) + ", which is not an address")
            next
            if count(got.list) > 0 then
                from = got.list[0]
            else if count(got.skipped) = 0 then
                append(problems, "the From header holds no address: " + _show(f))
            end if
        end if
        lists = {}
        for each name in ["to", "cc", "reply-to"]
            lists[name] = []
            for each h in hs
                if lower(h.name) = name then
                    got = _addresses(_value(h.raw))
                    for each w in got.skipped
                        append(problems, "the " + h.name + " header holds " + _show(w) + ", which is not an address")
                    next
                    for each a in got.list
                        list = lists[name]
                        append(list, a)
                        lists[name] = list
                    next
                end if
            next
        next
        subject = default(header(e, "subject"), "")
        date = unknown
        date_offset = unknown
        dh = _raw_header(hs, "date")
        if not is_unknown(dh) then
            d = _date(dh)
            date = d.at
            date_offset = d.offset
            if d.problem != "" then
                append(problems, d.problem)
            end if
        end if
        plain = _find_body(e, "plain")
        text = unknown
        text_part = unknown
        if not is_unknown(plain) then
            text_part = default(_part(plain).path, "1")
            text = plain.text
            if not is_unknown(text) and _flowed(plain) then
                delsp = default(plain.params["delsp"], "no")
                text = unflow(text, lower(delsp) = "yes")
            end if
        end if
        rich = _find_body(e, "html")
        html = unknown
        html_part = unknown
        if not is_unknown(rich) then
            html_part = _part(rich).path
            html = rich.text
        end if
        parts = _leaves(e, [])
        attachments = []
        for each p in parts
            if p.disposition = "attachment" or not is_unknown(p.filename) then
                append(attachments, p)
            end if
        next
        return { headers: e.headers, from: from, to: lists["to"], cc: lists["cc"],
                 reply_to: lists["reply-to"], subject: subject, date: date, date_offset: date_offset,
                 message_id: default(_raw_header(hs, "message-id"), unknown),
                 in_reply_to: _ids(_raw_header(hs, "in-reply-to")),
                 references: _ids(_raw_header(hs, "references")),
                 type: e.type, parts: parts, text: text, text_part: text_part,
                 html: html, html_part: html_part, attachments: attachments,
                 problems: problems }
    end function

    ' mail.parse(raw): a received message's bytes (CRLF or LF) as a record.
    function parse(raw)
        if type(raw) != "string" then
            error "mail.parse expects the message's bytes, as a string; it was given " + type(raw)
        end if
        return _message(_entity(raw, 0, "text/plain", "", true))
    end function

    ' ---------------------------------------------------------------- flowed

    ' RFC 3676 format=flowed, read: a line ending in a space continues on the
    ' next line of the same quotation depth, space-stuffing is removed, and
    ' with DelSp=yes the space that marked the break goes too. Quoted lines
    ' come back as "> " repeated to their depth, then the text.
    function unflow(text, delsp = false)
        out = []
        cur = unknown
        cur_depth = 0
        for each line in split(text, "\n")
            depth = 0
            while depth < byte_count(line) and byte_at(line, depth) = 62
                depth = depth + 1
            end while
            body = byte_slice(line, depth)
            if starts_with(body, " ") then
                body = byte_slice(body, 1)
            end if
            soft = ends_with(body, " ") and body != "-- "
            if not is_unknown(cur) and depth != cur_depth then
                ' a quotation depth changed under a soft break: it ends here
                append(out, _quote_prefix(cur_depth) + cur)
                cur = unknown
            end if
            piece = body
            if soft and delsp then
                piece = byte_slice(body, 0, byte_count(body) - 1)
            end if
            if is_unknown(cur) then
                cur = piece
                cur_depth = depth
            else
                cur = cur + piece
            end if
            if not soft then
                append(out, _quote_prefix(cur_depth) + cur)
                cur = unknown
            end if
        next
        if not is_unknown(cur) then
            append(out, _quote_prefix(cur_depth) + cur)
        end if
        return join(out, "\n")
    end function

    function _quote_prefix(depth)
        if depth = 0 then
            return ""
        end if
        return repeat(">", depth) + " "
    end function

    ' ---------------------------------------------------------------- quotes

    ' mail.own_text(text): the text above the first line that begins a
    ' quotation, by one of three conventions: a line starting ">", an
    ' attribution ("On ... wrote:"), or "-----Original Message-----". It is
    ' an inference from convention, not a fact about who wrote what, so it
    ' says which convention it used, and `uncertain` names unquoted lines
    ' below the cut (a reply written between quoted lines), which this rule
    ' would lose.
    '   { text, quoted, by, line, uncertain }
    function own_text(text)
        lines = split(replace(text, chr(13) + chr(10), "\n"), "\n")
        cut = -1
        by = ""
        for each line, i in lines
            t = trim(line)
            if starts_with(line, ">") then
                by = "quote"
            else if t = "-----Original Message-----" then
                by = "original message"
            else if not is_unknown(match(t, regex("^On .+ wrote:$"))) then
                by = "attribution"
            end if
            if by != "" then
                cut = i
                break
            end if
        next
        if cut < 0 then
            return { text: _trim_end(lines), quoted: "", by: "", line: unknown, uncertain: "" }
        end if
        own = []
        for each line, i in lines
            if i < cut then
                append(own, line)
            end if
        next
        below = 0
        rest = []
        for each line, i in lines
            if i >= cut then
                append(rest, line)
                t = trim(line)
                if i > cut and t != "" and not starts_with(line, ">") then
                    if by != "original message" then
                        below = below + 1
                    end if
                end if
            end if
        next
        why = ""
        if below > 0 then
            why = string(below) + " unquoted line(s) below the quotation"
        end if
        return { text: _trim_end(own), quoted: join(rest, "\n"), by: by, line: cut + 1, uncertain: why }
    end function

    function _trim_end(lines)
        n = count(lines)
        while n > 0 and trim(lines[n - 1]) = ""
            n = n - 1
        end while
        out = []
        for each line, i in lines
            if i < n then
                append(out, line)
            end if
        next
        return join(out, "\n")
    end function
end library

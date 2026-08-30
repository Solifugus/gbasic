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
        if value = unknown or value = nothing then
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
        if html != unknown then
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
end library

' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' mail.parse over every .eml in args[0], in sorted order, a JSON line each,
' for tests/mail/check_mail.py to hold against Python's email package.
' Bodies are hex, since a part's body is bytes.

' json_encode refuses unknown, which parse uses for "none"; JSON's is null.
function nulls(v)
    if is_unknown(v) then
        return nothing
    end if
    if type(v) = "record" then
        for each k in keys(v)
            v[k] = nulls(v[k])
        next
    else if type(v) = "array" then
        for each x, i in v
            v[i] = nulls(x)
        next
    end if
    return v
end function

function pairs(list)
    out = []
    for each a in list
        append(out, [a.name, a.address])
    next
    return out
end function

function shape(m)
    hs = []
    for each h in m.headers
        append(hs, [h.name, h.value])
    next
    from = unknown
    if not is_unknown(m.from) then
        from = [m.from.name, m.from.address]
    end if
    date = unknown
    offset = unknown
    if not is_unknown(m.date) then
        date = string(m.date)
        offset = m.date_offset.total_seconds / 60
    end if
    parts = []
    for each p in m.parts
        inner = unknown
        if not is_unknown(p.message) then
            inner = shape(p.message)
        end if
        append(parts, { path: p.path, type: p.type, charset: p.charset, disposition: p.disposition,
                        filename: p.filename, encoding: p.encoding, body: hex_encode(p.body),
                        text: p.text, message: inner })
    next
    att = []
    for each a in m.attachments
        append(att, a.path)
    next
    return { headers: hs, from: from, to: pairs(m.to), cc: pairs(m.cc), reply_to: pairs(m.reply_to),
             subject: m.subject, date: date, date_offset: offset, message_id: m.message_id, in_reply_to: m.in_reply_to,
             references: m.references, parts: parts, text_part: m.text_part, html_part: m.html_part,
             text: m.text, attachments: att, problems: m.problems }
end function

program main(args)
    load mail
    d = args[0]
    for each e in files(d, "*.eml")
        name = file_name(e.path)
        f {file}= e.path
        s = shape(mail.parse(read(f)))
        s.file = name
        print json_encode(nulls(s))
    next
end program

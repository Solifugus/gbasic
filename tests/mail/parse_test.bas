' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' mail.parse, mail.unflow and mail.own_text by hand (book gap G59): every
' expected value here is written from the RFCs (2045, 2046, 2047, 2231, 3676,
' 5322) or worked out by hand, never read back from the library. What Python's
' email package says about thousands of generated messages is the runner's
' oracle tier (check_mail.py); this tier holds the rules where Python and the
' RFCs part, and every problem parse can report.

function check(label, got, want)
    G.checks = G.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        G.mismatches = G.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' Whether any problem says `text`.
function said(m, text)
    for each p in m.problems
        if contains(p, text) then
            return true
        end if
    next
    return false
end function

function msg(lines)
    return join(lines, "\n")
end function

function subject_of(value)
    return mail.parse(msg(["From: a@b.example", "Subject: " + value, "", "x"])).subject
end function

function date_of(value)
    return mail.parse(msg(["From: a@b.example", "Date: " + value, "", "x"]))
end function

program main(args)
    load mail
    G = { checks: 0, mismatches: 0 }

    print "-- encoded words (RFC 2047 section 8's own table)"
    check("one word", subject_of("=?ISO-8859-1?Q?a?="), "a")
    check("a word, then text", subject_of("=?ISO-8859-1?Q?a?= b"), "a b")
    check("two adjacent words join", subject_of("=?ISO-8859-1?Q?a?= =?ISO-8859-1?Q?b?="), "ab")
    check("however much space is between them", subject_of("=?ISO-8859-1?Q?a?=  =?ISO-8859-1?Q?b?="), "ab")
    check("or a fold",
          mail.parse(msg(["Subject: =?ISO-8859-1?Q?a?=", "    =?ISO-8859-1?Q?b?=", "", "x"])).subject, "ab")
    check("an underscore is a space", subject_of("=?ISO-8859-1?Q?a_b?="), "a b")
    check("across two charsets", subject_of("=?ISO-8859-1?Q?a?= =?ISO-8859-1?Q?_b?="), "a b")
    check("B, in UTF-8", subject_of("=?utf-8?B?xYHDs2TFug==?= order"), "Łódź order")
    check("Latin-1 by Q, hex in either case", subject_of("=?iso-8859-1?q?caf=e9?= =?iso-8859-1?Q?_na=EFve?="), "café naïve")
    check("windows-1252's euro", subject_of("=?windows-1252?Q?=809?="), "€9")
    check("a language suffix (RFC 2231) is read past", subject_of("=?utf-8*pl?Q?=C5=81?="), "Ł")
    check("text that only looks encoded is text", subject_of("price =? none"), "price =? none")
    m = mail.parse(msg(["Subject: =?x-unknown?Q?abc?= tail", "", "x"]))
    check("a charset gBASIC does not read leaves the word as written", m.subject, "=?x-unknown?Q?abc?= tail")
    check("and says so", said(m, "header Subject: the encoded word =?x-unknown?Q?abc?=: its charset 'x-unknown'"), true)
    m = mail.parse("Subject: caf" + from_bytes([0xE9]) + "\n\nx")
    check("raw 8-bit that is not UTF-8 is read as Latin-1", m.subject, "café")
    check("and says so", said(m, "header Subject: holds bytes that are not UTF-8; they were read as Latin-1"), true)
    check("raw UTF-8 (RFC 6532) is kept", subject_of("Łódź"), "Łódź")

    print ""
    print "-- headers"
    m = mail.parse(msg(["Received: from one", "Subject: folded", " over", "	two lines",
                        "Received: from two", "X-Empty:", "", "body"]))
    check("unfolding keeps the white space a fold began with", m.subject, "folded over	two lines")
    check("header() is the first, any case", mail.header(m, "RECEIVED"), "from one")
    check("headers_named() is every one, in order", mail.headers_named(m, "received"), ["from one", "from two"])
    check("an empty header is empty", mail.header(m, "x-empty"), "")
    check("a missing header is unknown", is_unknown(mail.header(m, "x-none")), true)
    check("every header is kept, in order", count(m.headers), 4)
    m = mail.parse(msg(["From a@b.example Tue Apr  6 07:52:00 2027", "From: x@y.example", "", "hi"]))
    check("an mbox envelope line is not a header", m.from.address, "x@y.example")
    check("and is no problem", m.problems, [])
    m = mail.parse(msg(["From: a@b.example", "this line is no header", "more", ""]))
    check("a line that is no header begins the body", m.text, "this line is no header\nmore\n")
    check("and is named", said(m, "a line in the headers is not a header ('this line is no header')"), true)
    m = mail.parse("From: a@b.example\nSubject: s")
    check("headers with no body", m.subject, "s")
    check("have empty text", m.text, "")

    print ""
    print "-- addresses"
    m = mail.parse(msg(["From: =?utf-8?q?Anna_Kowalsk?= =?utf-8?q?a?= <anna@b.example>",
                        "To: \"Desk, Help\" <desk@b.example>, plain@b.example, Anna   Maria <am@b.example>",
                        "Cc: Team: one@b.example, \"Last, First\" <two@b.example>;, three@b.example (a comment)",
                        "Reply-To: undisclosed-recipients:;", "", "x"]))
    check("a display name's adjacent encoded words join", m.from.name, "Anna Kowalska")
    check("a quoted name keeps its comma", m.to[0], { name: "Desk, Help", address: "desk@b.example" })
    check("a bare address has no name", m.to[1], { name: "", address: "plain@b.example" })
    check("a name's words are joined by one space", m.to[2].name, "Anna Maria")
    check("a group is opened", count(m.cc), 3)
    check("its members are addresses", m.cc[1], { name: "Last, First", address: "two@b.example" })
    check("a comment is not a name (Python's rule)", m.cc[2], { name: "", address: "three@b.example" })
    m = mail.parse(msg(["From: Anna (the desk) Maria <am@b.example>", "", "x"]))
    check("a comment inside a display name is not part of it", m.from.name, "Anna Maria")
    check("an empty group is no addresses", m.reply_to, [])
    check("none of it is a problem", m.problems, [])
    m = mail.parse(msg(["From: a@b.example", "To: Desk, d@b.example", "", "x"]))
    check("a word that is not an address is not returned as one", m.to, [{ name: "", address: "d@b.example" }])
    check("it is named", said(m, "the To header holds 'Desk', which is not an address"), true)
    m = mail.parse(msg(["From: <@relay.example:anna@b.example>", "", "x"]))
    check("an obsolete route is dropped", m.from.address, "anna@b.example")
    m = mail.parse(msg(["Subject: no from", "", "x"]))
    check("no From is unknown", is_unknown(m.from), true)
    m = mail.parse(msg(["In-Reply-To: <a@x.example>", "References: <r1@x.example>", " <r2@x.example>", "", "x"]))
    check("In-Reply-To", m.in_reply_to, ["<a@x.example>"])
    check("References, folded", m.references, ["<r1@x.example>", "<r2@x.example>"])

    print ""
    print "-- dates (RFC 5322 3.3 and 4.3)"
    check("with its zone", string(date_of("Tue, 6 Apr 2027 07:52:00 -0500").date), "2027-04-06 12:52:00")
    check("no weekday, no seconds, a zone name", string(date_of("6 Apr 2027 07:52 EST").date), "2027-04-06 12:52:00")
    check("a two-digit year below 50 is 20xx", string(date_of("6 Apr 27 07:52:00 +0000").date), "2027-04-06 07:52:00")
    ' RFC 5322 4.3: 50-99 are 1900s. Python's email.utils adds 2000 to 50-68.
    check("and 50 or above is 19xx (Python says 2055)", string(date_of("6 Apr 55 07:52:00 +0000").date), "1955-04-06 07:52:00")
    check("a three-digit year adds 1900", string(date_of("6 Apr 127 07:52:00 +0000").date), "2027-04-06 07:52:00")
    check("a trailing comment", string(date_of("Tue, 06 Apr 2027 07:52:00 +0000 (UTC)").date), "2027-04-06 07:52:00")
    check("a half-hour zone", string(date_of("6 Apr 2027 07:52:00 +0530").date), "2027-04-06 02:22:00")
    check("and its offset", date_of("6 Apr 2027 07:52:00 +0530").date_offset = 5 hours + 30 minutes, true)
    check("a western offset is negative", date_of("6 Apr 2027 07:52:00 -0330").date_offset = 0 seconds - (3 hours + 30 minutes), true)
    m = date_of("6 Apr 2027 07:52:00")
    check("no zone names no instant", is_unknown(m.date), true)
    check("and says so", said(m, "has no zone, so it names no instant"), true)
    m = date_of("30 Feb 2027 07:52:00 +0000")
    check("a date that does not exist", is_unknown(m.date) and said(m, "is not a date that exists"), true)
    m = date_of("6 Apr 2027 07:52:00 XYZ")
    check("a zone RFC 5322 does not name", said(m, "zone 'XYZ' is not one RFC 5322 names"), true)
    m = date_of("yesterday")
    check("text that is no date", said(m, "the Date header is not an RFC 5322 date: 'yesterday'"), true)

    print ""
    print "-- bodies and charsets"
    m = mail.parse(msg(["Content-Type: text/plain; charset=iso-8859-1", "Content-Transfer-Encoding: quoted-printable",
                        "", "a=3Db=", " c=  ", "d=G1 =e9", ""]))
    ' RFC 2045 6.7: trailing white space goes, so "=  " is a soft break.
    ' Python's decoder keeps the spaces and the "=", and writes "c=  \nd".
    check("quoted-printable: soft breaks, white space after one, hex in any case, a bare = kept",
          m.text, "a=b cd=G1 é\n")
    m = mail.parse("Content-Type: text/plain; charset=utf-8\nContent-Transfer-Encoding: quoted-printable\n\nx=\r\ny\r\nz\r\n")
    check("a hard CRLF line break is kept in the body", hex_encode(m.parts[0].body), hex_encode("xy" + chr(13) + "\nz" + chr(13) + "\n"))
    check("and read as LF in the text", m.text, "xy\nz\n")
    m = mail.parse(msg(["Content-Type: text/plain; charset=iso-8859-1", "", "caf" + from_bytes([0xE9])]))
    check("Latin-1 8bit", m.text, "café")
    m = mail.parse(msg(["Content-Type: text/plain; charset=\"windows-1252\"", "", from_bytes([0x80]) + "9"]))
    check("windows-1252, quoted", m.text, "€9")
    m = mail.parse(msg(["Content-Type: text/plain; charset=utf-16", "Content-Transfer-Encoding: base64", "", "//5BAA=="]))
    check("UTF-16 with its byte order mark", m.text, "A")
    m = mail.parse(msg(["Content-Type: text/plain; charset=koi8-r", "", from_bytes([0xF0, 0xD2])]))
    check("a charset gBASIC does not read gives no text", is_unknown(m.text), true)
    check("and says which", said(m, "its charset 'koi8-r' is not one gBASIC reads"), true)
    check("and keeps the bytes", hex_encode(m.parts[0].body), "f0d2")
    m = mail.parse(msg(["Content-Type: text/plain; charset=utf-8", "", "ab" + from_bytes([0xFF])]))
    check("bytes that are not the charset give no text", is_unknown(m.text), true)
    check("and name the byte", said(m, "labelled utf-8 and byte 2 is not UTF-8"), true)
    m = mail.parse(msg(["Content-Type: text/plain", "", "café"]))
    check("us-ascii holding UTF-8 is read as UTF-8", m.text, "café")
    check("and says so", said(m, "labelled us-ascii and holds UTF-8"), true)
    m = mail.parse(msg(["Subject: none", "", "plain"]))
    check("no Content-Type is text/plain", m.parts[0].type + " " + m.text, "text/plain plain")
    m = mail.parse(msg(["Content-Type: text", "", "plain"]))
    check("a Content-Type that is no type is read as text/plain", m.parts[0].type, "text/plain")
    check("and named", said(m, "its Content-Type 'text' is not a type"), true)
    m = mail.parse(msg(["Content-Type: application/octet-stream", "Content-Transfer-Encoding: base64", "", "@@@@"]))
    check("base64 that does not decode", said(m, "its base64 does not decode"), true)
    check("keeps the body as sent", m.parts[0].body, "@@@@")
    m = mail.parse(msg(["Content-Type: application/octet-stream", "Content-Transfer-Encoding: base64", "",
                        "AAH/", " QQ"]))
    check("base64 across lines, white space and missing padding", hex_encode(m.parts[0].body), "0001ff41")
    m = mail.parse(msg(["Content-Transfer-Encoding: x-uuencode", "", "begin"]))
    check("a transfer encoding MIME does not define", said(m, "'x-uuencode' is not one MIME defines"), true)

    print ""
    print "-- multipart (RFC 2046 5.1)"
    m = mail.parse(msg(["Content-Type: multipart/mixed; boundary=B", "", "preamble", "--B",
                        "Content-Type: text/plain", "", "one", "--Bx", "--B-- not the end", "text --B", "--B  ",
                        "Content-Type: text/plain", "", "two", "--B--", "epilogue"]))
    check("lines that only begin like the boundary are content", m.parts[0].text, "one\n--Bx\n--B-- not the end\ntext --B")
    check("transport padding after a delimiter", m.parts[1].text, "two")
    check("paths", m.parts[0].path + " " + m.parts[1].path, "1 2")
    check("well formed", m.problems, [])
    m = mail.parse(replace(msg(["Content-Type: multipart/mixed; boundary=B", "", "--B", "", "one", "--B", "", "", "--B--", ""]),
                           "\n", chr(13) + "\n"))
    check("with CRLF, the CRLF before a delimiter is the delimiter's", hex_encode(m.parts[0].body), hex_encode("one"))
    check("and an empty part is empty", m.parts[1].body, "")
    m = mail.parse(msg(["Content-Type: multipart/mixed; boundary=B", "", "--B", "", "one", "--B", "", "two", ""]))
    check("no closing boundary: the last part runs to the end", m.parts[1].text, "two\n")
    check("and says so", said(m, "ends without its closing boundary"), true)
    m = mail.parse(msg(["Content-Type: multipart/digest; boundary=D", "", "--D", "", "Subject: inside", "", "hello",
                        "--D", "Content-Type: text/plain", "", "not a message", "--D--"]))
    check("a digest's part with no Content-Type is a message (RFC 2046 5.1.5)",
          m.parts[0].type + " " + m.parts[0].message.subject + " " + m.parts[0].message.text, "message/rfc822 inside hello")
    check("and one with a type keeps it", m.parts[1].type, "text/plain")
    m = mail.parse(msg(["Content-Type: multipart/mixed", "", "x"]))
    check("no boundary parameter", said(m, "multipart/mixed with no boundary") and count(m.parts) = 0, true)
    m = mail.parse(msg(["Content-Type: multipart/mixed; boundary=B", "", "x"]))
    check("a boundary no line begins", said(m, "no line begins its boundary"), true)
    nest = "Content-Type: text/plain\n\ncore"
    for k = 1 to 40
        nest = "Content-Type: multipart/mixed; boundary=b" + string(k) + "\n\n--b" + string(k) + "\n" + nest + "\n--b" + string(k) + "--"
    next
    m = mail.parse(nest)
    check("nesting stops at 32", said(m, "nested 32 deep"), true)
    check("and nothing below it is read", is_unknown(m.text), true)

    print ""
    print "-- which part is the text (Python's get_body)"
    two = msg(["Content-Type: multipart/mixed; boundary=B", "", "--B",
               "Content-Type: text/plain; charset=utf-8", "Content-Disposition: attachment; filename=notes.txt", "",
               "attached notes", "--B", "Content-Type: multipart/alternative; boundary=C", "", "--C",
               "Content-Type: text/plain", "", "the letter", "--C", "Content-Type: text/html", "", "<p>the letter</p>",
               "--C--", "--B--"])
    m = mail.parse(two)
    check("an attachment is not the text", m.text, "the letter")
    check("the text is in part 2.1", m.text_part, "2.1")
    check("the html in 2.2", m.html_part + " " + m.html, "2.2 <p>the letter</p>")
    check("the attachment is listed", count(m.attachments) = 1 and m.attachments[0].filename = "notes.txt", true)
    m = mail.parse(msg(["Content-Type: text/html; charset=utf-8", "", "<p>only</p>"]))
    check("html only: no text", is_unknown(m.text) and m.html = "<p>only</p>", true)
    rel = msg(["Content-Type: multipart/related; boundary=R; start=\"<b@x>\"", "", "--R",
               "Content-Type: text/html", "Content-ID: <a@x>", "", "A", "--R",
               "Content-Type: text/html", "Content-ID: <b@x>", "", "B", "--R--"])
    check("multipart/related reads its start", mail.parse(rel).html, "B")
    check("and with no start, its first part",
          mail.parse(replace(rel, "; start=\"<b@x>\"", "")).html, "A")

    print ""
    print "-- parameters (RFC 2045 5.1, RFC 2231)"
    m = mail.parse(msg(["Content-Type: application/pdf; name=\"a \\\"b\\\"; c.pdf\"", "", "x"]))
    check("a quoted value with an escaped quote and a semicolon", m.parts[0].filename, "a \"b\"; c.pdf")
    m = mail.parse("Content-Type: application/pdf; name=\"abc\\")
    check("a quoted value cut short by a lone backslash ends (it once hung)", m.parts[0].filename, "abc")
    check("and its missing quote is named", said(m, "the parameter name has no closing quote"), true)
    m = mail.parse(msg(["Content-Type: application/pdf",
                        "Content-Disposition: attachment; filename*0*=utf-8''%C5%81%C3%B3;",
                        " filename*1*=d%C5%BA.pdf", "", "x"]))
    check("RFC 2231 sections, joined in order", m.parts[0].filename, "Łódź.pdf")
    m = mail.parse(msg(["Content-Type: application/pdf",
                        "Content-Disposition: attachment; filename*1=b.pdf; filename*0=a", "", "x"]))
    check("sections given out of order", m.parts[0].filename, "ab.pdf")
    m = mail.parse(msg(["Content-Type: application/pdf",
                        "Content-Disposition: attachment; filename*=iso-8859-1'fr'caf%E9.pdf", "", "x"]))
    check("RFC 2231 in Latin-1, with a language", m.parts[0].filename, "café.pdf")
    m = mail.parse(msg(["Content-Type: application/pdf", "Content-Disposition: attachment; filename=\"=?utf-8?b?xYHDs2TFui50eHQ=?=\"", "", "x"]))
    check("an encoded word as a filename (forbidden, and sent)", m.parts[0].filename, "Łódź.txt")
    check("the disposition", m.parts[0].disposition, "attachment")

    print ""
    print "-- format=flowed (RFC 3676)"
    flowed = "This is a flowed \nparagraph that \nends here.\n-- \nsig\n> quoted \n> flowed\n>> deeper\n \"stuffed\" line"
    check("soft breaks join, the signature line stays, depths stay apart, stuffing goes",
          mail.unflow(flowed), "This is a flowed paragraph that ends here.\n-- \nsig\n> quoted flowed\n>> deeper\n\"stuffed\" line")
    check("DelSp=yes removes the space that marked the break", mail.unflow("abc \ndef", true), "abcdef")
    check("and without it the space stays", mail.unflow("abc \ndef"), "abc def")
    m = mail.parse(msg(["Content-Type: text/plain; charset=utf-8; format=flowed; delsp=yes", "", "Dzie", "", "a b ", "c"]))
    check("parse unflows the text a person reads", m.text, "Dzie\n\na bc")
    check("and leaves the part's own text as sent", m.parts[0].text, "Dzie\n\na b \nc")
    check("and marks the part flowed", m.parts[0].flowed, true)

    print ""
    print "-- own text"
    o = mail.own_text("Thanks, will do.\n\nOn Tue, 6 Apr 2027, Anna wrote:\n> Can you ship?\n> Thanks")
    check("an attribution line", o.text + "|" + o.by + "|" + string(o.line) + "|" + o.uncertain,
          "Thanks, will do.|attribution|3|")
    check("what was cut", o.quoted, "On Tue, 6 Apr 2027, Anna wrote:\n> Can you ship?\n> Thanks")
    o = mail.own_text("Yes.\n> q1\nNo.\n> q2")
    check("a reply written between quotations is uncertain",
          o.text + "|" + o.by + "|" + o.uncertain, "Yes.|quote|1 unquoted line(s) below the quotation")
    o = mail.own_text("Done\r\n-----Original Message-----\r\nFrom: x\r\n\r\nold text")
    check("Outlook's original message", o.text + "|" + o.by + "|" + o.uncertain, "Done|original message|")
    o = mail.own_text("No quotation here.\n\n")
    check("no quotation", o.text + "|" + o.by + "|" + string(is_unknown(o.line)), "No quotation here.||true")

    print ""
    print "-- what mail.compose writes, read back"
    sent = mail.compose({ from: "Zakład Mechaniczny <biuro@zaklad.pl>", to: ["Anna <a@example.com>"],
                          cc: "c@example.com", bcc: "secret@example.com", subject: "Łódź order",
                          body: "Dzień dobry\nline two", html: "<p>Dzień</p>",
                          attachments: [{ name: "x.bin", content: from_bytes([0, 1, 255]), type: "application/octet-stream" }],
                          date: {datetime}"2027-04-06T07:52:00-05:00" })
    m = mail.parse(sent.text)
    check("from", m.from, { name: "Zakład Mechaniczny", address: "biuro@zaklad.pl" })
    check("to and cc", [m.to, m.cc], [[{ name: "Anna", address: "a@example.com" }], [{ name: "", address: "c@example.com" }]])
    check("bcc is nowhere", contains(sent.text, "secret"), false)
    check("subject", m.subject, "Łódź order")
    ' Both sides are UTC instants (since 2026-11-10; compose read `date` as
    ' local civil time before, and this came back shifted by the offset).
    sent_at = {datetime}"2027-04-06T07:52:00-05:00"
    check("date: the instant compose was given", m.date = sent_at, true)
    check("date_offset: the system zone's offset then", m.date_offset = zone_offset(sent_at), true)
    tokyo = mail.parse(mail.compose({ from: "a@b.example", to: "c@d.example", body: "x", date: sent_at,
                                     zone: "Asia/Tokyo" }).text)
    check("compose's zone: written as Tokyo's clock", mail.header(tokyo, "date"), "Tue, 06 Apr 2027 21:52:00 +0900")
    check("and the same instant", tokyo.date = sent_at, true)
    check("message id", m.message_id, sent.message_id)
    check("text (base64)", m.text, "Dzień dobry\nline two\n")
    check("html", m.html, "<p>Dzień</p>\n")
    check("the attachment's bytes, a NUL among them", hex_encode(m.attachments[0].body), "0001ff")
    check("its name", m.attachments[0].filename, "x.bin")
    check("no problems", m.problems, [])
    plain = mail.parse(mail.compose({ from: "a@b.example", to: "c@d.example", body: "plain\n.\nend" }).text)
    check("a 7bit body reads as base64's does", plain.text, "plain\n.\nend\n")
    crlf = mail.parse(replace(sent.text, "\n", chr(13) + "\n"))
    check("the same message with CRLF line ends", [crlf.subject, crlf.text, crlf.html, hex_encode(crlf.attachments[0].body)],
          [m.subject, m.text, m.html, hex_encode(m.attachments[0].body)])

    print ""
    print "-- refusals"
    on error goto next
    x = mail.parse(42)
    check("not bytes", contains(error.message, "mail.parse expects the message's bytes, as a string; it was given number"), true)
    error.clear()
    on error stop
    m = mail.parse("From: a@b.example\nbad" + chr(13) + "line" + chr(9) + "x\n\nbody")
    clean = true
    for each p in m.problems
        if contains(p, chr(13)) or contains(p, chr(10)) or contains(p, chr(9)) then
            clean = false
        end if
    next
    check("a problem quoting a line carries no control characters", clean and count(m.problems) = 1, true)

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program

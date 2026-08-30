' SELF-CHECKING, not a golden. Every check states the answer it expects and
' prints `ok` or a MISMATCH naming both sides, because a golden records
' whatever the composer emits AS the expected output -- and the defects this
' file is about (a body sent 7bit that needed base64, a Bcc reaching the
' headers, a boundary that occurs inside a part) all produce a message that
' still looks like a message.
program main( args )
    load mail

    checks = 0

    function want(label, actual, expected)
        if actual = expected then
            print "ok   " + label
        else
            print "MISMATCH " + label + ": got " + string(actual) + ", expected " + string(expected)
        end if
        return 1
    end function

    ' ---- transfer encoding is CHOSEN, and the choice is the interesting part
    plain = mail.compose({ from: "a@x.com", to: ["b@x.com"], body: "hello" })
    checks = checks + want("ascii body stays 7bit",
                           contains(plain.text, "Content-Transfer-Encoding: 7bit"), true)
    checks = checks + want("ascii body is readable on the wire",
                           contains(plain.text, "\n\nhello\n"), true)

    accent = mail.compose({ from: "a@x.com", to: ["b@x.com"], body: "café" })
    checks = checks + want("non-ascii body goes base64",
                           contains(accent.text, "Content-Transfer-Encoding: base64"), true)
    ' base64_encode("café\n"), not ("café"): the body is normalized to end
    ' with exactly one newline BEFORE encoding, because that newline is
    ' transport framing for 7bit and content for base64 -- without the
    ' normalization the same body decodes differently in the two encodings.
    checks = checks + want("non-ascii body round-trips",
                           contains(accent.text, base64_encode("café\n")), true)

    long = mail.compose({ from: "a@x.com", to: ["b@x.com"], body: repeat("x", 1200) })
    checks = checks + want("a 1200-char line goes base64 even though it is ascii",
                           contains(long.text, "Content-Transfer-Encoding: base64"), true)
    short = mail.compose({ from: "a@x.com", to: ["b@x.com"], body: repeat("x", 800) })
    checks = checks + want("an 800-char line does not (the control)",
                           contains(short.text, "Content-Transfer-Encoding: 7bit"), true)

    ' ---- the subject
    checks = checks + want("ascii subject is not encoded",
                           mail.encode_word("Nightly run"), "Nightly run")
    checks = checks + want("non-ascii subject is an encoded-word",
                           starts_with(mail.encode_word("café"), "=?utf-8?B?"), true)

    ' ---- bcc: the envelope, and NOWHERE else
    blind = mail.compose({ from: "a@x.com", to: ["b@x.com"], bcc: ["secret@x.com"], body: "hi" })
    checks = checks + want("bcc is in the envelope", contains(blind.recipients, "secret@x.com"), true)
    checks = checks + want("bcc is not in the message", contains(blind.text, "secret@x.com"), false)
    checks = checks + want("bcc has no header at all", contains(lower(blind.text), "bcc:"), false)

    ' ---- structure selection
    checks = checks + want("body only is a single part",
                           contains(plain.text, "multipart"), false)
    alt = mail.compose({ from: "a@x.com", to: ["b@x.com"], body: "t", html: "<p>t</p>" })
    checks = checks + want("body + html is alternative",
                           contains(alt.text, "multipart/alternative"), true)
    checks = checks + want("body + html is not mixed",
                           contains(alt.text, "multipart/mixed"), false)
    mixed = mail.compose({ from: "a@x.com", to: ["b@x.com"], body: "t",
                           attachments: [{ name: "a.txt", content: "z" }] })
    checks = checks + want("body + attachment is mixed",
                           contains(mixed.text, "multipart/mixed"), true)
    both = mail.compose({ from: "a@x.com", to: ["b@x.com"], body: "t", html: "<p>t</p>",
                          attachments: [{ name: "a.txt", content: "z" }] })
    checks = checks + want("body + html + attachment is mixed wrapping alternative",
                           contains(both.text, "multipart/mixed") and contains(both.text, "multipart/alternative"),
                           true)

    ' ---- the boundary must not occur inside a part. A boundary that does
    ' splits the message at the wrong place and the result still parses.
    hostile_body = "--=_gbasic_deadbeef_= not really a boundary"
    hostile = mail.compose({ from: "a@x.com", to: ["b@x.com"], body: hostile_body,
                             attachments: [{ name: "a.txt", content: "z" }] })
    marker = mid(hostile.text, find(hostile.text, "boundary=\"") + 10, 60)
    boundary = mid(marker, 0, find(marker, "\""))
    checks = checks + want("the chosen boundary does not occur in the body",
                           contains(hostile_body, boundary), false)

    ' ---- attachments
    wrapped = mail.wrap_base64(repeat("A", 300))
    widest = 0
    for each line in split(wrapped, "\n")
        if len(line) > widest then
            widest = len(line)
        end if
    next
    checks = checks + want("base64 wraps at 76 columns", widest, 76)
    checks = checks + want("attachment content survives",
                           contains(mixed.text, base64_encode("z")), true)
    checks = checks + want("attachment is marked as one",
                           contains(mixed.text, "Content-Disposition: attachment; filename=\"a.txt\""), true)

    ' ---- envelope and message-id
    checks = checks + want("envelope from strips the display name",
                           mail.compose({ from: "Bee <b@x.com>", to: ["c@x.com"], body: "z" }).from,
                           "b@x.com")
    checks = checks + want("message-id is at the sender's domain",
                           ends_with(plain.message_id, "@x.com>"), true)
    checks = checks + want("a single recipient may be given without an array",
                           mail.compose({ from: "a@x.com", to: "b@x.com", body: "z" }).recipients,
                           ["b@x.com"])

    ' A tier that stops running its checks otherwise passes by saying nothing.
    if checks < 23 then
        print "COVERAGE SHORTFALL: only " + string(checks) + " checks ran"
    end if
    print "checks: " + string(checks)
end program

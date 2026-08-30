' A composed message, rendered for a byte-exact golden.
'
' Three things in a real message are not deterministic -- the Message-ID, the
' MIME boundaries and the Date -- so the Date is pinned by the fixture and the
' other two are masked here. Everything a golden should be defending (header
' order, encodings, MIME nesting, which addresses appear where) survives the
' masking; nothing that varies per run does.
program main( args )
    load mail
    when{datetime} = "2026-03-01 09:30:00"

    m = mail.compose({
        from: "Système <noreply@example.com>",
        to: ["a@example.com", "Bee <b@example.com>"],
        cc: "c@example.com",
        bcc: ["hidden@example.com"],
        reply_to: "support@example.com",
        subject: "Rapport quotidien — 日本語",
        body: "Bonjour,\n\nCi-joint le rapport.",
        html: "<p>Bonjour</p>",
        headers: { "X-Run-Id": "run-91" },
        attachments: [{ name: "report.csv", type: "text/csv", content: "a,b\n1,2\n" }],
        date: when
    })

    print "envelope-from: " + m.from
    print "envelope-rcpt: " + join(m.recipients, ", ")
    print "---"
    text = replace(m.text, regex("=_gbasic_[0-9a-f]+_="), "BOUNDARY")
    text = replace(text, regex("<[0-9a-f]{32}@"), "<MESSAGEID@")
    print text
end program

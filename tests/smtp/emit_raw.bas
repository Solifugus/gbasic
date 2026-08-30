' The composed message and NOTHING else on stdout, so an independent parser
' can be handed it verbatim. Same message as compose_dump, unmasked.
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
    print m.text
end program

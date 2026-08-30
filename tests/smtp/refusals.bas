' Every refusal, each beside its NEAREST LEGAL NEIGHBOUR where one exists --
' a refusal suite with no control is satisfied by a composer that refuses
' everything. The injection cases are the point of the file: a CR or LF in a
' header value lets whoever supplied that text write headers of their own.
program main( args )
    load mail

    function attempt(label, message)
        on error goto failed
        m = mail.compose(message)
        print "ACCEPTED " + label
        return 1
failed:
        print "refused  " + label + ": " + error.message
        return 1
    end function

    ok = { from: "a@example.com", to: ["b@example.com"], subject: "s", body: "x" }

    print "-- injection"
    n = attempt("subject with a newline",
                { from: "a@example.com", to: ["b@example.com"], subject: "hi\nBcc: evil@example.com", body: "x" })
    n = attempt("subject without one (the control)", ok)
    n = attempt("recipient with a newline",
                { from: "a@example.com", to: ["b@example.com\nBcc: evil@example.com"], body: "x" })
    n = attempt("from with a CRLF",
                { from: "a@example.com" + chr(13) + chr(10) + "X-Evil: 1", to: ["b@example.com"], body: "x" })
    n = attempt("header value with a CRLF",
                { from: "a@example.com", to: ["b@example.com"], body: "x",
                  headers: { "X-Note": "a" + chr(13) + chr(10) + "Bcc: evil@example.com" } })
    n = attempt("header value without one (the control)",
                { from: "a@example.com", to: ["b@example.com"], body: "x",
                  headers: { "X-Note": "ordinary" } })
    n = attempt("attachment name with a newline",
                { from: "a@example.com", to: ["b@example.com"], body: "x",
                  attachments: [{ name: "a\nb.csv", content: "z" }] })

    print "-- headers"
    n = attempt("header name with a space",
                { from: "a@example.com", to: ["b@example.com"], body: "x", headers: { "X Note": "v" } })
    n = attempt("a header mail.compose writes itself",
                { from: "a@example.com", to: ["b@example.com"], body: "x", headers: { "Subject": "second" } })

    print "-- addresses"
    n = attempt("no from", { to: ["b@example.com"], body: "x" })
    n = attempt("no recipients", { from: "a@example.com", body: "x" })
    n = attempt("an address with no @", { from: "a@example.com", to: ["nobody"], body: "x" })
    n = attempt("a space inside the address", { from: "a@example.com", to: ["b b@example.com"], body: "x" })
    n = attempt("an unclosed angle bracket", { from: "a@example.com", to: ["Bee <b@example.com"], body: "x" })
    n = attempt("a closed one (the control)", { from: "a@example.com", to: ["Bee <b@example.com>"], body: "x" })

    print "-- shape"
    n = attempt("a misspelled field", { from: "a@example.com", to: ["b@example.com"], bodyy: "x" })
    n = attempt("an attachment with no content",
                { from: "a@example.com", to: ["b@example.com"], body: "x", attachments: [{ name: "a.csv" }] })
    n = attempt("an attachment with one (the control)",
                { from: "a@example.com", to: ["b@example.com"], body: "x",
                  attachments: [{ name: "a.csv", content: "z" }] })
end program

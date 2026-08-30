' smtp.send's OWN boundary: the envelope and the SMTP commands. These are
' checked before a socket is opened, so this fixture needs no relay.
'
' The control characters are the envelope's version of header injection: a CR
' in an address would let it write its own SMTP commands, one layer below
' where mail.compose can see.
program main( args )
    load mail
    load smtp

    good = mail.compose({ from: "a@example.com", to: ["b@example.com"], subject: "s", body: "x" })

    function attempt(label, config, message)
        on error goto failed
        r = smtp.send(config, message)
        print "ACCEPTED " + label
        return 1
failed:
        print "refused  " + label + ": " + error.message
        return 1
    end function

    cfg = { host: "127.0.0.1", port: 1, security: "plain", timeout: 1 }

    print "-- configuration"
    n = attempt("no host", { port: 25 }, good)
    n = attempt("an unknown option", { host: "h", nowhere: true }, good)
    n = attempt("an unknown security", { host: "h", security: "ssl" }, good)
    n = attempt("a port out of range", { host: "h", port: 70000 }, good)
    n = attempt("a fractional port", { host: "h", port: 25.5 }, good)
    n = attempt("a negative timeout", { host: "h", timeout: 0 }, good)
    n = attempt("verify as a string", { host: "h", verify: "yes" }, good)

    print "-- message"
    n = attempt("not the record compose returns", cfg, { subject: "s" })
    n = attempt("an unknown message field", cfg,
                { from: "a@x.com", recipients: ["b@x.com"], text: "hi", extra: 1 })
    n = attempt("no recipients", cfg, { from: "a@x.com", recipients: [], text: "hi" })
    n = attempt("empty text", cfg, { from: "a@x.com", recipients: ["b@x.com"], text: "" })
    n = attempt("a recipient that is not a string", cfg,
                { from: "a@x.com", recipients: [42], text: "hi" })
    n = attempt("a CR in the envelope from", cfg,
                { from: "a@x.com" + chr(13) + "MAIL FROM:<evil@x.com>", recipients: ["b@x.com"], text: "hi" })
    n = attempt("a LF in a recipient", cfg,
                { from: "a@x.com", recipients: ["b@x.com" + chr(10) + "RCPT TO:<evil@x.com>"], text: "hi" })
    n = attempt("an address already in angle brackets", cfg,
                { from: "<a@x.com>", recipients: ["b@x.com"], text: "hi" })

    print "-- the control"
    ' A refusal tier with no control is satisfied by a module that refuses
    ' everything. This one is well-formed, so it must get PAST validation and
    ' fail at the socket instead -- port 1 with nothing listening.
    on error goto no_listener
    r = smtp.send(cfg, good)
    print "ACCEPTED a send with nothing listening, which cannot have happened"
    return
no_listener:
    ' The connect error carries a duration in milliseconds, which is not the
    ' same on two machines, so what is asserted is the SHAPE: it failed at the
    ' socket rather than in validation.
    if contains(error.message, "connect") then
        print "refused  a well-formed send, at the socket rather than by validation"
    else
        print "refused  a well-formed send, but by VALIDATION: " + error.message
    end if
end program

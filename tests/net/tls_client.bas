' net, TLS, CLIENT: run by tests/run_net.sh against tests/net/tls_peer.py's
' servers (Python's ssl module, which net did not write).
'
'   gbasic tls_client.bas CERTDIR PORTS_FILE
'
' Self-checking. WAITED checks run first, in main; then a watcher is declared
' and the WATCHED ones run on the loop. `checks:` and `mismatches:` print when
' the watched connections have all closed, so a watched socket that never
' reports `closed` keeps the loop alive and the runner's timeout says so.
load net

function check(label, got, want)
    G.checks = G.checks + 1
    if type(got) = type(want) and string(got) = string(want) then
        print "ok   " + label
    else
        G.mismatches = G.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + " (" + type(got) + "), want " + string(want) + " (" + type(want) + ")"
    end if
    return nothing
end function

' What a failing call raised: { code, reason, message, number }, or ANSWERED.
function failure(f, a, b)
    on error goto handle
    r = f(a, b)
    return { code: "ANSWERED", reason: "", message: "", number: 0 }
handle:
    out = { code: "no code", reason: "", message: error.message, number: error.code }
    if has(error.details, "code") then
        out.code = error.details.code
    end if
    if has(error.details, "reason") then
        out.reason = error.details.reason
    end if
    error.clear()
    return out
end function

function try_connect(opts, unused)
    c = net.connect(opts)
    return c
end function

' A connect and one echoed line: TLS 1.3 reports a refused client certificate
' only after the client's handshake has finished, at the first read.
function try_exchange(opts, unused)
    c = net.connect(opts)
    c.write("ping" + chr(10))
    return c.read_line(10 seconds)
end function

' The same, with the refusal given time to arrive before the write: the write
' then finds the server gone (EPIPE), and the alert waiting behind it is what
' names the failure.
function try_exchange_late(opts, unused)
    c = net.connect(opts)
    sleep(0.5)
    c.write("ping" + chr(10))
    sleep(0.2)
    c.write("ping" + chr(10))
    return c.read_line(10 seconds)
end function

function try_listen(opts, unused)
    return net.listen(opts)
end function

function try_start_tls(c, opts)
    return c.start_tls(opts, 10 seconds)
end function

function tls_to(port, tls)
    return { host: "localhost", port: port, timeout: 10 seconds, tls: tls }
end function

program main(args)
    G = { checks: 0, mismatches: 0, finished: 0, seen: [], up: [], refused: [] }
    d = args[0]
    G.dir = d
    pf {file}= args[1]
    P = decode(read(pf))
    G.P = P
    ca = d + "/ca.pem"
    G.ca = ca
    fp {file}= d + "/server.sha256"
    fingerprint = trim(read(fp))

    ' --- a verified connection, and what it says about itself ---------------
    c = net.connect(tls_to(P.echo, { ca: ca }))
    check("a verified connection is TLS 1.2 or later", left(c.tls.version, 5), "TLSv1")
    check("  and not 1.0 or 1.1", c.tls.version = "TLSv1" or c.tls.version = "TLSv1.1", false)
    check("  the cipher is named", byte_count(c.tls.cipher) > 0, true)
    check("  the name asked for", c.tls.server_name, "localhost")
    check("  the peer's subject", c.tls.peer_certificate.subject, "CN=localhost")
    check("  its issuer", c.tls.peer_certificate.issuer, "CN=gBASIC Test CA")
    check("  its sha256 is openssl's fingerprint", c.tls.peer_certificate.sha256, fingerprint)
    check("  not_after is a datetime", type(c.tls.peer_certificate.not_after), "datetime")
    check("  not_before is before not_after", c.tls.peer_certificate.not_before < c.tls.peer_certificate.not_after, true)
    c.write("hello" + chr(10) + "wor")
    c.write("ld" + chr(0) + "!" + chr(10))
    check("a line round trip over TLS", c.read_line(10 seconds), "hello")
    check("  and one written in two pieces, NUL included", c.read_line(10 seconds), "world" + chr(0) + "!")
    c.close()
    check("closed", c.open, false)
    check("  and its facts outlive the close", c.tls.peer_certificate.subject, "CN=localhost")

    ' 5 MiB through the partial-write path: libssl writes a record at a time,
    ' and the queue moves and grows between a refused write and its retry.
    big = repeat("0123456789abcdef", 327680)
    c = net.connect(tls_to(P.echo, { ca: ca }))
    c.write(big)
    back = c.read_exactly(byte_count(big), 30 seconds)
    check("5 MiB echoed exactly over TLS", back = big, true)
    c.close()

    c = net.connect({ host: "127.0.0.1", port: P.echo, timeout: 10 seconds, tls: { ca: ca } })
    check("an address is checked against the certificate's IP name", c.tls.peer_certificate.subject, "CN=localhost")
    check("  and sends no SNI (a name is for names)", c.tls.server_name, unknown)
    c.close()

    ' --- verification is ON, and its failures are named ---------------------
    f = failure(try_connect, tls_to(P.echo, true), nothing)
    check("the system store does not trust a test CA", f.code, "tls_verify")
    check("  the reason", f.reason, "unable to get local issuer certificate")
    check("  the message says which side", contains(f.message, "server's certificate did not verify"), true)
    f = failure(try_connect, tls_to(P.echo, { ca: ca, server_name: "wrong.test" }), nothing)
    check("a name the certificate does not hold", f.code, "tls_verify")
    check("  the reason", f.reason, "hostname mismatch")
    f = failure(try_connect, tls_to(P.stranger, { ca: ca }), nothing)
    check("a self-signed certificate", f.code, "tls_verify")
    check("  the reason", contains(f.reason, "self") and contains(f.reason, "signed certificate"), true)
    c = net.connect(tls_to(P.stranger, { verify: false }))
    check("verify: false, spelled out, connects anyway", c.tls.peer_certificate.subject, "CN=stranger")
    c.write("x" + chr(10))
    check("  and works", c.read_line(10 seconds), "x")
    c.close()
    if has(P, "expired") then
        f = failure(try_connect, tls_to(P.expired, { ca: ca }), nothing)
        check("an expired certificate", f.code, "tls_verify")
        check("  the reason", f.reason, "certificate has expired")
    else
        print "SKIP expired: this openssl cannot backdate a certificate"
    end if

    f = failure(try_connect, { host: "localhost", port: P.silent, timeout: 1 seconds, tls: { ca: ca } }, nothing)
    check("a handshake the peer never answers times out within the connect timeout", f.code, "timeout")

    ' --- mutual TLS ---------------------------------------------------------
    f = failure(try_exchange, tls_to(P.mtls, { ca: ca }), nothing)
    check("no client certificate where one is required", f.code, "tls_failed")
    check("  the server's alert is the reason", contains(f.message, "certificate required"), true)
    f = failure(try_exchange_late, tls_to(P.mtls, { ca: ca }), nothing)
    check("  the same when the refusal is already waiting at the write", f.code, "tls_failed")
    check("    named the same", contains(f.message, "certificate required"), true)
    c = net.connect(tls_to(P.mtls, { ca: ca, cert: d + "/client.pem", key: d + "/client.key" }))
    check("a client certificate the server's CA signed", c.read_line(10 seconds), "client:gBASIC Client")
    c.close()
    f = failure(try_exchange, tls_to(P.mtls, { ca: ca, cert: d + "/stranger_client.pem", key: d + "/stranger_client.key" }), nothing)
    check("a client certificate from another issuer", f.code, "tls_failed")

    ' --- start_tls: an upgrade on an open connection -------------------------
    c = net.connect({ host: "localhost", port: P.starttls, timeout: 10 seconds })
    check("plain until upgraded", c.tls, nothing)
    c.write("STARTTLS" + chr(10))
    check("the server agrees", c.read_line(10 seconds), "OK")
    check("start_tls", c.start_tls({ ca: ca }, 10 seconds), true)
    check("  the name defaults to the host connected to", c.tls.server_name, "localhost")
    c.write("after" + chr(10))
    check("  the same connection, now TLS", c.read_line(10 seconds), "after")
    c.close()

    ' The STARTTLS injection: bytes sent ahead of the handshake must not be
    ' read as though they came over TLS.
    c = net.connect({ host: "localhost", port: P.starttls, timeout: 10 seconds })
    c.write("INJECT" + chr(10))
    check("the reply", c.read_line(10 seconds), "OK")
    check("  arrived with bytes behind it", c.buffered() > 0, true)
    f = failure(try_start_tls, c, { ca: ca })
    check("start_tls refuses bytes that arrived in plaintext", f.code, "unexpected_data")
    check("  and stays plain", c.tls, nothing)
    c.close()

    ' PostgreSQL's SSLRequest: 8 bytes, one byte back, then the handshake.
    c = net.connect({ host: "localhost", port: P.pg, timeout: 10 seconds })
    c.write(pack(">I I", [8, 80877103]))
    check("an SSLRequest is answered S", c.read_exactly(1, 10 seconds), "S")
    c.start_tls({ ca: ca }, 10 seconds)
    c.write("query" + chr(10))
    check("  then the protocol continues over TLS", c.read_line(10 seconds), "query")
    c.close()

    ' --- refusals -------------------------------------------------------------
    f = failure(try_connect, tls_to(P.echo, "yes"), nothing)
    check("tls must be true or a record", f.number, 1003)
    f = failure(try_connect, tls_to(P.echo, { ca: ca, verfy: false }), nothing)
    check("an unknown tls option is named", contains(f.message, "unknown option 'verfy'"), true)
    f = failure(try_connect, tls_to(P.echo, { cert: d + "/client.pem" }), nothing)
    check("a client certificate needs its key", contains(f.message, "needs both cert and key"), true)
    f = failure(try_connect, tls_to(P.echo, { ca: d + "/no-such.pem" }), nothing)
    check("a ca file that does not load", f.code, "tls_failed")
    f = failure(try_connect, { path: d + "/no.sock", tls: true }, nothing)
    check("a Unix socket has no name to check", contains(f.message, "give tls a server_name, or verify: false"), true)
    f = failure(try_listen, { port: 0, tls: true }, nothing)
    check("a server's tls is a record", contains(f.message, "must be a record with cert and key"), true)
    f = failure(try_listen, { port: 0, tls: { cert: "/no/such.pem", key: "/no/such.key" } }, nothing)
    check("a certificate that does not load is refused at listen", f.code, "tls_failed")
    check("  naming the file", contains(f.message, "could not load certificate '/no/such.pem'"), true)
    f = failure(try_listen, { port: 0, tls: { cert: d + "/server.pem", key: d + "/alt.key" } }, nothing)
    check("a key that is not the certificate's", f.code, "tls_failed")
    f = failure(try_listen, { port: 0, tls: { cert: d + "/server.pem", key: d + "/server.key", require_client_cert: true } }, nothing)
    check("require_client_cert needs client_ca", contains(f.message, "require_client_cert needs client_ca"), true)
    l = net.listen({ port: 0, tls: { cert: d + "/server.pem", key: d + "/server.key" } })
    check("a TLS listener says so", l.tls.client_certificate, "none")
    l.close()
    c = net.connect(tls_to(P.echo, { ca: ca }))
    f = failure(try_start_tls, c, { ca: ca })
    check("start_tls on a TLS connection", contains(f.message, "already TLS"), true)
    c.close()

    ' --- watched --------------------------------------------------------------
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            on_event(e)
        end while
    end watch
    ' by NAME: the lookup is on the resolver thread, then the handshake in the loop
    G.w = net.connect(tls_to(P.echo, { ca: ca }))
    check("a watched TLS connect returns at once, before its handshake", is_nothing(G.w.tls), true)
    G.bad = net.connect(tls_to(P.stranger, { ca: ca }))
    G.up_c = net.connect({ host: "localhost", port: P.starttls, timeout: 10 seconds })
    G.mute = net.connect({ host: "localhost", port: P.silent, timeout: 1 seconds, tls: { ca: ca } })
    G.muted = []
end program

function on_event(e)
    c = e.connection
    if c = G.w then
        append(G.seen, e.kind)
        if e.kind = "connected" then
            check("watched: connected comes after the handshake", c.tls.peer_certificate.subject, "CN=localhost")
            c.write("watched" + chr(10))
        end if
        if e.kind = "data" then
            line = c.take_line()
            if not is_unknown(line) then
                check("watched: a line over TLS", line, "watched")
                c.close()
            end if
        end if
        if e.kind = "closed" then
            check("watched: connected, data, closed", G.seen, ["connected", "data", "closed"])
            done()
        end if
    end if
    if c = G.bad then
        append(G.refused, e.kind + ":" + string(e.code))
        if e.kind = "closed" then
            check("watched: a failed verify is an error then closed", G.refused, ["error:tls_verify", "closed:tls_verify"])
            done()
        end if
    end if
    if c = G.mute then
        append(G.muted, e.kind + ":" + string(e.code))
        if e.kind = "closed" then
            check("watched: a handshake never answered is a timeout, then closed", G.muted, ["error:timeout", "closed:timeout"])
            done()
        end if
    end if
    if c = G.up_c then
        append(G.up, e.kind)
        if e.kind = "connected" then
            c.write("STARTTLS" + chr(10))
        end if
        if e.kind = "data" then
            line = c.take_line()
            while not is_unknown(line)
                if line = "OK" then
                    c.start_tls({ ca: G.ca })
                else
                    check("watched: after start_tls, the line came back over TLS", line, "upgraded")
                    check("  on a TLS connection", left(c.tls.version, 5), "TLSv1")
                    c.close()
                end if
                line = c.take_line()
            end while
        end if
        if e.kind = "tls" then
            c.write("upgraded" + chr(10))
        end if
        if e.kind = "closed" then
            check("watched: connected, data, tls, data, closed", G.up, ["connected", "data", "tls", "data", "closed"])
            done()
        end if
    end if
    return nothing
end function

function done()
    G.finished = G.finished + 1
    if G.finished = 4 then
        print "checks: " + string(G.checks)
        print "mismatches: " + string(G.mismatches)
    end if
    return nothing
end function

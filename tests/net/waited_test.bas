' net, WAITED: connections a program blocks on, with timeouts. Self-checking;
' run by tests/run_net.sh.
'
' Hermetic: a waited listener and a waited connect in ONE process, which works
' because the kernel completes a connect into the listen backlog before
' anything is accepted. Both ends are this module, so the cross-implementation
' checks are in run_net.sh's Python tiers; this fixture holds the framing,
' the refusals and the codes.
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

' The code a failing call raised, or ANSWERED.
function code_of(f, a, b)
    on error goto handle
    r = f(a, b)
    return "ANSWERED"
handle:
    c = "no code"
    if has(error.details, "code") then
        c = error.details.code
    end if
    error.clear()
    return c
end function

function try_connect(opts, unused)
    return net.connect(opts)
end function

function try_listen(opts, unused)
    return net.listen(opts)
end function

function try_read_exactly(c, n)
    return c.read_exactly(n, 0.2)
end function

function try_read_line(c, unused)
    return c.read_line(0.2)
end function

function try_write(c, data)
    return c.write(data)
end function

' A connected pair: the listener, the client end and the server end.
function tcp_pair()
    l = net.listen({ port: 0 })
    a = net.connect({ host: "127.0.0.1", port: l.port, timeout: 5 seconds })
    b = l.accept(5 seconds)
    return { listener: l, client: a, server: b }
end function

program main(args)
    G = { checks: 0, mismatches: 0 }
    print "-- A PAIR, AND WHAT IT SAYS ABOUT ITSELF --"
    p = tcp_pair()
    check("the listener is on loopback by default", p.listener.local.address, "127.0.0.1")
    check("and was given a port", p.listener.port > 0, true)
    check("a connection is a connection", p.client.kind, "connection")
    check("the client's peer is the listener's port", p.client.peer.port, p.listener.port)
    check("the server's peer is the client's local end", p.server.peer.port, p.client.local.port)
    check("waited, since no watcher on net.events is live", p.client.watched, false)
    check("open", p.client.open, true)
    check("the listener counts its connection", p.listener.connections, 1)
    check("no TLS in N1", p.client.tls, nothing)

    print ""
    print "-- FRAMING: exactly, until, line --"
    p.client.write("one" + chr(10) + "two" + chr(13) + chr(10) + "three|rest")
    check("read_line", p.server.read_line(5 seconds), "one")
    check("read_line drops a trailing CR", p.server.read_line(5 seconds), "two")
    check("read_until", p.server.read_until("|", 5 seconds), "three")
    check("read_exactly", p.server.read_exactly(4, 5 seconds), "rest")
    check("nothing is left", p.server.buffered(), 0)

    ' A LENGTH-PREFIXED FRAME, the shape SSH and PostgreSQL both use, with a
    ' NUL inside it: bytes, counted, never text.
    body = "a" + chr(0) + "b" + from_bytes([255])
    p.client.write(pack(">I", [byte_count(body)]) + body)
    n = unpack(">I", p.server.read_exactly(4, 5 seconds))[0]
    check("a length prefix", n, 4)
    got = p.server.read_exactly(n, 5 seconds)
    check("and the frame, NUL and 0xFF included", to_bytes(got), [97, 0, 98, 255])

    print ""
    print "-- THE TAKE FAMILY NEVER BLOCKS --"
    check("take_line with nothing buffered is unknown", is_unknown(p.server.take_line()), true)
    p.client.write("par")
    p.client.flush(5 seconds)
    r = p.server.read(5 seconds)
    check("read answers what arrived", r, "par")
    p.client.write("tial" + chr(10) + "next")
    p.client.flush(5 seconds)
    first = p.server.read_line(5 seconds)
    check("a line split across writes reassembles", first, "tial")
    check("peek_exactly leaves the bytes", p.server.peek_exactly(2), "ne")
    check("  so take_exactly still gets them", p.server.take_exactly(2), "ne")
    check("take_until with no delimiter yet is unknown", is_unknown(p.server.take_until("|")), true)
    check("take gives what is buffered", p.server.take(), "xt")

    print ""
    print "-- TIMEOUTS AND CLOSES ARE DIFFERENT FACTS --"
    check("a read with nothing coming times out", code_of(try_read_line, p.server, nothing), "timeout")
    p.client.write("half")
    p.client.flush(5 seconds)
    check("a partial frame times out too", code_of(try_read_exactly, p.server, 10), "timeout")
    check("  and what arrived is still buffered", p.server.buffered(), 4)
    p.client.close()
    check("a closed peer before the frame is complete is 'closed'",
          code_of(try_read_exactly, p.server, 10), "closed")
    check("read at end of stream answers empty", p.server.read(1 seconds), "")
    p.server.close()
    check("closing makes it not open", p.server.open, false)
    check("a write after close is refused as closed", code_of(try_write, p.server, "x"), "closed")
    p.listener.close()

    print ""
    print "-- UNIX SOCKETS, AND WHO IS ON THE OTHER END --"
    path = args[0]
    u = net.listen({ path: path, mode: "0600" })
    check("a Unix listener names its path", u.path, path)
    check("  and has no port", is_unknown(u.port), true)
    uc = net.connect({ path: path })
    us = u.accept(5 seconds)
    uc.write("over unix" + chr(10))
    check("bytes cross a Unix socket", us.read_line(5 seconds), "over unix")
    cred = us.peer_credentials
    check("peer credentials name this process's user", cred.uid, number(args[1]))
    check("  and a process", cred.pid > 0, true)
    q = tcp_pair()
    check("a TCP connection has none", is_unknown(q.server.peer_credentials), true)
    q.client.close()
    q.server.close()
    q.listener.close()
    check("a second listener on the same path is refused", code_of(try_listen, { path: path }, nothing), "address_in_use")
    us.close()
    uc.close()
    u.close()
    check("closing the listener removes its path", exists({file}path), false)

    print ""
    print "-- NAMED FAILURES --"
    closed = net.listen({ port: 0 })
    dead_port = closed.port
    closed.close()
    check("nothing listening: refused", code_of(try_connect, { host: "127.0.0.1", port: dead_port }, nothing), "refused")
    check("a name that does not resolve", code_of(try_connect, { host: "no-such-host.invalid", port: 80 }, nothing),
          "name_not_found")
    check("a Unix path nobody listens on", code_of(try_connect, { path: path + ".none" }, nothing), "refused")
    held = net.listen({ port: 0 })
    check("a port already held", code_of(try_listen, { port: held.port }, nothing), "address_in_use")
    held.close()
    small = net.listen({ port: 0, buffer: 1024 })
    sc = net.connect({ host: "127.0.0.1", port: small.port })
    ss = small.accept(5 seconds)
    check("a frame larger than the buffer is refused at the call", code_of(try_read_exactly, ss, 4096), "too_long")
    ss.close()
    sc.close()
    small.close()

    print ""
    print "-- MISUSE IS REFUSED BY NAME --"
    check("an unknown option", code_of(try_connect, { host: "x", port: 1, colour: 1 }, nothing), "no code")
    check("a host name is not a bind address", code_of(try_listen, { port: 0, address: "localhost" }, nothing), "no code")

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program

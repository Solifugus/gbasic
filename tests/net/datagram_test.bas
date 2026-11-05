' net, DATAGRAMS (N3), WAITED: UDP sockets a program blocks on. Self-checking;
' run by tests/run_net.sh.
'
'   gbasic datagram_test.bas [ECHO_PORT]
'
' Hermetic: both ends are datagram sockets in this one process, which works
' because a datagram sent to a bound socket waits in the kernel until it is
' received. With ECHO_PORT, a Python echo server (a peer this module did not
' write) answers too. The rule under test is the one TCP does not have: A
' DATAGRAM IS NEVER SPLIT OR JOINED, and one longer than max_datagram says so.
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

' Whether a failing call's message holds a phrase, or ANSWERED.
function says(f, a, b, phrase)
    on error goto handle
    r = f(a, b)
    return "ANSWERED"
handle:
    m = error.message
    error.clear()
    return contains(m, phrase)
end function

function try_datagram(opts, unused)
    return net.datagram(opts)
end function

function try_send(u, dest)
    return u.send_to("x", dest)
end function

function try_send_big(u, dest)
    return u.send_to(repeat("z", 65508), dest)
end function

function try_receive(u, unused)
    return u.receive(0.2)
end function

function try_take(u, unused)
    return u.take()
end function

function try_write(u, unused)
    return u.write("x")
end function

function try_queued(u, unused)
    return u.queued()
end function

function try_send_on_stream(c, port)
    return net.send_to(c, "x", { address: "127.0.0.1", port: port })
end function

function bytes_0_to_255()
    b = ""
    for i = 0 to 255
        b = b + pack(">B", [i])
    next
    return b
end function

program main(args)
    G = { checks: 0, mismatches: 0 }

    print "-- A SOCKET --"
    a = net.datagram({ port: 0 })
    b = net.datagram()
    check("its kind", a.kind, "datagram")
    check("a port names a server's socket, on loopback", a.local.address, "127.0.0.1")
    check("the system chose the port", a.port > 0, true)
    check("no port names a client's, on every address", b.local.address, "0.0.0.0")
    check("max_datagram defaults to 64 KiB", a.max_datagram, 65536)
    check("a datagram socket has no peer", is_unknown(a.peer), true)
    check("waited, with no watcher", a.watched, false)

    print ""
    print "-- ONE DATAGRAM IS ONE RECEIVE --"
    all = bytes_0_to_255()
    check("send_to answers the bytes sent", b.send_to(all, { address: "127.0.0.1", port: a.port }), 256)
    r = a.receive(2 seconds)
    check("every byte value, NUL included, arrives untouched", r.data = all, true)
    check("from names the sender's port", r.from.port, b.port)
    check("from names the sender's address", r.from.address, "127.0.0.1")
    check("not truncated", r.truncated, false)
    check("its size", r.size, 256)
    b.send_to("a", { address: "127.0.0.1", port: a.port })
    b.send_to("bb", { address: "127.0.0.1", port: a.port })
    b.send_to("ccc", { address: "127.0.0.1", port: a.port })
    got = [a.receive(2).data, a.receive(2).data, a.receive(2).data]
    check("three datagrams are three receives, never joined", got, ["a", "bb", "ccc"])
    b.send_to("", { address: "127.0.0.1", port: a.port })
    e = a.receive(2)
    check("an empty datagram is a datagram", [e.data, e.size], ["", 0])
    biggest = repeat("q", 65507)
    b.send_to(biggest, { address: "127.0.0.1", port: a.port })
    r = a.receive(2)
    check("the largest IPv4 datagram arrives whole", [r.data = biggest, r.truncated, r.size], [true, false, 65507])
    check("one byte more is refused: too_large", code_of(try_send_big, b, { address: "127.0.0.1", port: a.port }),
          "too_large")
    b.send_to("by name", { host: "localhost", port: a.port })
    check("host: resolves a name", a.receive(2).data, "by name")
    check("a received from is a send_to target (the reply idiom)", a.send_to("back", r.from) , 4)
    check("  and it comes back", b.receive(2).data, "back")

    print ""
    print "-- LONGER THAN max_datagram IS REPORTED, NEVER SILENTLY CUT --"
    small = net.datagram({ port: 0, max_datagram: 4 })
    b.send_to("abcdefgh", { address: "127.0.0.1", port: small.port })
    t = small.receive(2)
    check("the bytes that fit", t.data, "abcd")
    check("truncated", t.truncated, true)
    check("size is what was sent", t.size, 8)
    b.send_to("wxyz", { address: "127.0.0.1", port: small.port })
    t = small.receive(2)
    check("exactly max_datagram is not truncated", [t.data, t.truncated], ["wxyz", false])
    b.send_to("next", { address: "127.0.0.1", port: small.port })
    check("the rest of a cut datagram is gone, not the next one's start", small.receive(2).data, "next")
    small.close()

    print ""
    print "-- IPv6 --"
    if code_of(try_datagram, { address: "::1", port: 0 }, nothing) != "ANSWERED" then
        print "skip IPv6: ::1 cannot be bound here, so IPv6 was not measured"
    else
        v6 = net.datagram({ address: "::1", port: 0 })
        v6c = net.datagram({ address: "::1" })
        v6c.send_to("six", { address: "::1", port: v6.port })
        r = v6.receive(2)
        check("over ::1", [r.data, r.from.address], ["six", "::1"])
        v6.close()
        v6c.close()
        ' A socket on every IPv6 address reaches IPv4 too: the address is mapped.
        dual = net.datagram({ address: "::" })
        dual.send_to("mapped", { address: "127.0.0.1", port: a.port })
        r = a.receive(2)
        check("an IPv6 socket sends to an IPv4 address, mapped", [r.data, r.from.port], ["mapped", dual.port])
        a.send_to("reply", r.from)
        r = dual.receive(2)
        check("  and the reply arrives from the mapped address", [r.data, r.from.address], ["reply", "::ffff:127.0.0.1"])
        dual.close()
    end if
    check("an IPv4 socket refuses an IPv6 address, naming the fix",
          says(try_send, b, { address: "::1", port: 9 }, "make the socket with address: \"::\""), true)

    print ""
    print "-- NAMED FAILURES --"
    check("receive with nothing sent: timeout", code_of(try_receive, a, nothing), "timeout")
    check("a port already held: address_in_use", code_of(try_datagram, { port: a.port }, nothing), "address_in_use")
    check("a name that does not resolve", code_of(try_send, b, { host: "no-such-host.invalid", port: 9 }),
          "name_not_found")
    gone = net.datagram()
    gone.close()
    check("send_to on a closed socket: closed", code_of(try_send, gone, { address: "127.0.0.1", port: a.port }),
          "closed")
    check("receive on a closed socket: closed", code_of(try_receive, gone, nothing), "closed")
    check("a closed socket says so", gone.open, false)

    print ""
    print "-- MISUSE IS REFUSED BY NAME --"
    check("take: no stream", says(try_take, a, nothing, "a datagram socket has no stream"), true)
    check("write: no stream", says(try_write, a, nothing, "a datagram socket has no stream"), true)
    check("queued: nothing is queued", says(try_queued, a, nothing, "send_to hands one to the system"), true)
    l = net.listen({ port: 0 })
    c = net.connect({ host: "127.0.0.1", port: l.port })
    check("send_to on a connection", says(try_send_on_stream, c, a.port, "expects a datagram socket"), true)
    c.close()
    l.close()
    check("an unknown option", says(try_datagram, { port: 0, broadcast: true }, nothing, "unknown option 'broadcast'"),
          true)
    check("max_datagram 0", says(try_datagram, { max_datagram: 0 }, nothing, "max_datagram must be a whole number"),
          true)
    check("a name in address:", says(try_send, b, { address: "localhost", port: 9 }, "a name goes in host:"), true)
    check("no port to send to", says(try_send, b, { address: "127.0.0.1" }, "which port"), true)
    check("both address and host", says(try_send, b, { address: "127.0.0.1", host: "x", port: 9 }, "not both"), true)
    check("an unknown target field", says(try_send, b, { address: "127.0.0.1", port: 9, ttl: 1 }, "unknown option"),
          true)

    if count(args) > 0 then
        print ""
        print "-- AGAINST PYTHON'S ECHO SERVER --"
        echo = { address: "127.0.0.1", port: number(args[0]) }
        b.send_to(all, echo)
        r = b.receive(5 seconds)
        check("every byte value echoed by a peer this module did not write", r.data = all, true)
        check("  from the echo server's port", r.from.port, number(args[0]))
        for i = 1 to 50
            b.send_to(repeat(chr(64 + mod(i, 26)), i * 20), echo)
        next
        sizes = []
        for i = 1 to 50
            sizes = append(sizes, len(b.receive(5).data))
        next
        want = []
        for i = 1 to 50
            want = append(want, i * 20)
        next
        check("50 datagrams of 50 sizes come back as 50, each its own size", sizes, want)
        b.send_to(biggest, echo)
        check("the largest one, echoed whole", b.receive(5).data = biggest, true)
    end if
    a.close()
    b.close()

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program

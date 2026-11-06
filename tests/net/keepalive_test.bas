' TCP KEEPALIVE (Transward §3): `keepalive:` on net.connect and net.listen,
' and `c.keepalive` / `c.nodelay` READ BACK FROM THE SOCKET, so a check here
' is the system's answer and not the option echoed. The runner also reads the
' keepalive timer from outside the process (`ss -o`) while this one waits.
'
' args: [ready file, the system's keepalive time, interval, probes]

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

function refusal(opts, listening)
    on error goto handle
    if listening then
        r = net.listen(opts)
    else
        r = net.connect(opts)
    end if
    r.close()
    return "ACCEPTED"
handle:
    m = error.message
    error.clear()
    return m
end function

program main(args)
    G = { checks: 0, mismatches: 0 }
    system = { idle: number(args[1]), interval: number(args[2]), count: number(args[3]) }

    print "-- the default: no keepalive, and TCP_NODELAY on"
    l = net.listen({ port: 0 })
    a = net.connect({ host: "127.0.0.1", port: l.port, timeout: 5 seconds })
    b = l.accept(5 seconds)
    check("a connection has no keepalive unless asked", a.keepalive, false)
    check("nor does an accepted one", b.keepalive, false)
    check("TCP_NODELAY is on for a connection, always", a.nodelay, true)
    check("and for an accepted one", b.nodelay, true)
    a.close()
    b.close()
    l.close()

    print "-- keepalive: true takes the system's timings"
    l = net.listen({ port: 0 })
    a = net.connect({ host: "127.0.0.1", port: l.port, keepalive: true, timeout: 5 seconds })
    b = l.accept(5 seconds)
    check("the system's idle, interval and count", a.keepalive, system)
    check("a listener without keepalive gives its connections none", b.keepalive, false)
    a.close()
    b.close()
    l.close()

    print "-- timings asked for, on both sides"
    l = net.listen({ port: 0, keepalive: { idle: 7, interval: 3, count: 4 } })
    a = net.connect({ host: "127.0.0.1", port: l.port, timeout: 5 seconds,
                      keepalive: { idle: 11 seconds, interval: 2, count: 5 } })
    b = l.accept(5 seconds)
    check("the connection's, as the socket reports them", a.keepalive, { idle: 11, interval: 2, count: 5 })
    check("an accepted connection takes its listener's", b.keepalive, { idle: 7, interval: 3, count: 4 })
    c = net.connect({ host: "127.0.0.1", port: l.port, timeout: 5 seconds, keepalive: { idle: 2 minutes } })
    d = l.accept(5 seconds)
    check("a part left out is the system's", c.keepalive,
          { idle: 120, interval: system.interval, count: system.count })
    check("every connection a listener accepts", d.keepalive, { idle: 7, interval: 3, count: 4 })
    c.close()
    d.close()
    check("keepalive: false is none", refusal({ host: "127.0.0.1", port: l.port, keepalive: false }, false),
          "ACCEPTED")

    print "-- refusals, each beside a legal neighbour"
    check("a Unix socket", refusal({ path: args[0] + ".sock", keepalive: true }, true),
          "net.listen: keepalive is TCP's, and a Unix socket has no network to probe")
    check("connecting to one", refusal({ path: args[0] + ".sock", keepalive: true }, false),
          "net.connect: keepalive is TCP's, and a Unix socket has no network to probe")
    check("count 128 (127 is the most)", refusal({ port: 0, keepalive: { count: 128 } }, true),
          "net.listen keepalive: count must be a whole number from 1 to 127")
    check("count 127 is allowed", refusal({ port: 0, keepalive: { count: 127 } }, true), "ACCEPTED")
    check("count 0", refusal({ port: 0, keepalive: { count: 0 } }, true),
          "net.listen keepalive: count must be a whole number from 1 to 127")
    check("an idle of 0", refusal({ port: 0, keepalive: { idle: 0 } }, true),
          "net.listen: keepalive's idle and interval must be whole seconds from 1 to 32767")
    check("half a second", refusal({ port: 0, keepalive: { interval: 1.5 } }, true),
          "net.listen: keepalive's idle and interval must be whole seconds from 1 to 32767")
    check("32767 seconds is allowed", refusal({ port: 0, keepalive: { idle: 32767 } }, true), "ACCEPTED")
    check("32768 is not", refusal({ port: 0, keepalive: { idle: 32768 } }, true),
          "net.listen: keepalive's idle and interval must be whole seconds from 1 to 32767")
    check("an unknown part", refusal({ port: 0, keepalive: { idel: 5 } }, true),
          "net.listen keepalive: unknown option 'idel' (known: idle, interval, count)")
    check("not a record or a boolean", refusal({ port: 0, keepalive: "yes" }, true),
          "net.listen: keepalive must be true, false or a record { idle, interval, count }")

    ' the runner reads both sockets' keepalive timers with ss while this waits
    write(args[0], string(l.port))
    sleep(3)
    a.close()
    b.close()
    l.close()

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program

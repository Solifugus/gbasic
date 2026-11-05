' net, DATAGRAMS (N3), WATCHED: the events a datagram socket delivers.
' Self-checking; run by tests/run_net.sh.
'
' Two watched sockets in one process. `b` sends `a` 100 datagrams before the
' loop starts, more than the loop reads in one pass (64), so the order and the
' count are checked across passes; then a truncated one, then the closes. The
' program must END once both closed events are delivered: a datagram socket
' keeps the loop alive only while it is open.
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

function says(f, a, b, phrase)
    on error goto handle
    r = f(a, b)
    return "ANSWERED"
handle:
    m = error.message
    error.clear()
    return contains(m, phrase)
end function

function try_receive(u, unused)
    return u.receive(1)
end function

program main(args)
    G = { checks: 0, mismatches: 0, got: [], first: nothing, closed: [], small_seen: false }
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            on_event(e)
        end while
    end watch
    G.a = net.datagram({ port: 0 })
    G.b = net.datagram()
    G.small = net.datagram({ port: 0, max_datagram: 3 })
    check("made while a watcher is live: watched", G.a.watched, true)
    check("receive is refused on a watched socket", says(try_receive, G.a, nothing, "arrives as a datagram event"),
          true)
    for i = 1 to 100
        G.b.send_to("n" + string(i), { address: "127.0.0.1", port: G.a.port })
    next
end program

function on_event(e)
    if e.kind = "datagram" and e.socket = G.a then
        if is_nothing(G.first) then
            G.first = e
            check("the event names its socket", e.socket = G.a, true)
            check("  and no connection or listener", [is_nothing(e.connection), is_nothing(e.listener)], [true, true])
            check("  from is the sender", e.from.port, G.b.port)
            check("  not truncated, with its size", [e.truncated, e.size], [false, 2])
        end if
        G.got = append(G.got, e.data)
        if count(G.got) = 100 then
            want = []
            for i = 1 to 100
                want = append(want, "n" + string(i))
            next
            check("100 datagrams, 100 events, in the order sent", G.got = want, true)
            G.b.send_to("abcdef", { address: "127.0.0.1", port: G.small.port })
        end if
    else if e.kind = "datagram" and e.socket = G.small then
        check("a truncated datagram's event", [e.data, e.truncated, e.size], ["abc", true, 6])
        G.a.close()
        G.b.abort()
        G.small.close()
    else if e.kind = "closed" then
        G.closed = append(G.closed, e.code)
        check("a closed event names its socket", is_nothing(e.socket), false)
        if count(G.closed) = 3 then
            check("the codes: close is local, abort is aborted", sort(G.closed), ["aborted", "local", "local"])
            check("closed sockets say so", [G.a.open, G.b.open, G.small.open], [false, false, false])
            print ""
            print "checks: " + string(G.checks)
            print "mismatches: " + string(G.mismatches)
        end if
    else
        check("an unexpected event", e.kind, "none")
    end if
    return nothing
end function

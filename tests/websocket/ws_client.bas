' websocket, CLIENT: gBASIC's client against Python's `websockets` servers and
' raw ones (tests/websocket/ws_peer.py serve). Self-checking; run by
' tests/run_websocket.sh.
'
'   gbasic ws_client.bas WS_PORT RAW_PORT [WSS_PORT CA_FILE]
'
' Waited first (connect returns once the handshake is answered, receive
' blocks), then watched (the same conversations as events).
load websocket

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

' What a failing call raised: net's details.code, else websocket's details.reason; or ANSWERED.
function reason_of(f, a, b)
    on error goto handle
    r = f(a, b)
    return "ANSWERED"
handle:
    c = "no reason"
    if has(error.details, "code") then
        c = error.details.code
    else if has(error.details, "reason") then
        c = error.details.reason
    end if
    error.clear()
    return c
end function

function try_connect(url, options)
    return websocket.connect(url, options)
end function

function try_send(c, text)
    return websocket.send(c, text)
end function

function try_receive(c, unused)
    return websocket.receive(c, 1)
end function

function waited(args)
    ws = "ws://127.0.0.1:" + args[0]
    raw = "ws://127.0.0.1:" + args[1]

    print "-- WAITED --"
    c = websocket.connect(ws + "/echo")
    check("connect answers a connection, waited", [c.kind, c.watched], ["connection", false])
    websocket.send(c, "hello")
    m = websocket.receive(c, 5)
    check("a text message echoed", [m.kind, m.data, m.binary], ["message", "hello", false])
    all = ""
    for i = 0 to 255
        all = all + pack(">B", [i])
    end for
    websocket.send_binary(c, all)
    m = websocket.receive(c, 5)
    check("every byte value, as binary", [m.data = all, m.binary], [true, true])
    for each n in [0, 125, 126, 65535, 65536, 300000]
        websocket.send(c, repeat("a", n))
        m = websocket.receive(c, 10)
        check("a " + string(n) + "-byte message, each length form", byte_count(m.data), n)
    end for
    websocket.send(c, "héllo €")
    check("UTF-8 text", websocket.receive(c, 5).data, "héllo €")
    check("send refuses text that is not UTF-8, naming send_binary", reason_of(try_send, c, pack(">B", [255])),
          "not_utf8")
    websocket.close(c)
    check("close finishes the handshake and closes", c.open, false)
    check("send after close is refused", reason_of(try_send, c, "late"), "not_open")

    c = websocket.connect(ws + "/frag")
    m1 = websocket.receive(c, 5)
    m2 = websocket.receive(c, 5)
    check("a fragmented text message arrives whole", m1.data, "fragmented")
    check("a fragmented binary message arrives whole", [m2.data = pack(">B B B", [1, 2, 3]), m2.binary], [true, true])
    websocket.close(c)

    c = websocket.connect(ws + "/ping")
    check("a server's ping is answered while receive waits", websocket.receive(c, 5).data, "pong ok")
    websocket.close(c)

    c = websocket.connect(ws + "/bye")
    websocket.send(c, "anything")
    m = websocket.receive(c, 5)
    check("a server's close reaches receive with its code and reason", [m.kind, m.code, m.reason, m.clean],
          ["closed", 4001, "server bye", true])

    c = websocket.connect(ws + "/headers", { headers: { "X-Test": "given" }, protocols: ["a.v1", "b.v2"] })
    check("headers and protocols go with the handshake", websocket.receive(c, 5).data, "given|a.v1, b.v2")
    websocket.close(c)

    print ""
    print "-- WAITED: WHAT IS REFUSED --"
    check("a server that does not switch protocols", reason_of(try_connect, raw + "/noupgrade", nothing), "handshake")
    check("an accept that does not answer the key", reason_of(try_connect, raw + "/badaccept", nothing), "handshake")
    c = websocket.connect(raw + "/masked")
    m = websocket.receive(c, 5)
    check("a masked server frame closes with 1002", [m.kind, m.code], ["closed", 1002])
    c = websocket.connect(raw + "/drop")
    m = websocket.receive(c, 5)
    check("a message, then the server just goes", m.data, "then gone")
    m = websocket.receive(c, 5)
    check("  is a closed with 1006, not clean", [m.kind, m.code, m.clean], ["closed", 1006, false])
    check("a URL that is not ws:// or wss://", reason_of(try_connect, "http://127.0.0.1:1/", nothing), "bad_url")
    check("an unknown option", reason_of(try_connect, ws + "/echo", { colour: 1 }), "bad_option")
    check("nothing listening", reason_of(try_connect, "ws://127.0.0.1:1/", nothing), "refused")

    if count(args) > 2 then
        print ""
        print "-- WAITED: wss --"
        c = websocket.connect("wss://localhost:" + args[2] + "/echo", { tls: { ca: args[3] } })
        websocket.send(c, "over tls")
        check("wss: an echo over TLS, verified against the test CA", websocket.receive(c, 5).data, "over tls")
        check("  and c.tls says so", contains(c.tls.version, "TLS"), true)
        websocket.close(c)
        check("wss: an unverified server is refused (no ca)",
              reason_of(try_connect, "wss://localhost:" + args[2] + "/echo", nothing), "tls_verify")
    end if
    return nothing
end function

program main(args)
    G = { checks: 0, mismatches: 0, log: [], args: args, phase: 0 }
    waited(args)

    print ""
    print "-- WATCHED --"
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            for each w in websocket.events(e)
                on_ws(w)
            end for
        end while
    end watch
    G.a = websocket.connect("ws://127.0.0.1:" + args[0] + "/echo")
    check("made with a watcher live: watched", G.a.watched, true)
    check("receive is refused on a watched one", reason_of(try_receive, G.a, nothing), "watched")
    G.b = websocket.connect("ws://127.0.0.1:" + args[1] + "/badaccept")
end program

function on_ws(w)
    c = w.connection
    if c = G.a then
        append(G.log, w.kind)
        if w.kind = "open" then
            websocket.send(c, "one")
            websocket.send(c, "two")
            websocket.send_binary(c, pack(">B", [0]))
        else if w.kind = "message" then
            append(G.log, w.data)
            if w.binary then
                websocket.close(c, 1000, "done")
            end if
        else if w.kind = "closed" then
            check("watched: open, three messages in order, then a clean close", G.log,
                  ["open", "message", "one", "message", "two", "message", pack(">B", [0]), "closed"])
            check("  the close was 1000, clean", [w.code, w.clean], [1000, true])
            finish()
        end if
    else if c = G.b then
        if w.kind = "error" then
            check("watched: a bad accept is an error event", w.code, "handshake")
        else if w.kind = "closed" then
            check("  then closed, 1006", w.code, 1006)
            finish()
        else
            check("watched: a bad accept sends nothing else", w.kind, "error")
        end if
    end if
    return nothing
end function

function finish()
    G.phase = G.phase + 1
    if G.phase = 2 then
        print ""
        print "checks: " + string(G.checks)
        print "mismatches: " + string(G.mismatches)
    end if
    return nothing
end function

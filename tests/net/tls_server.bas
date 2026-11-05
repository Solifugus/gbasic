' net, TLS, WATCHED SERVER: run by tests/run_net.sh, with tests/net/tls_peer.py
' (Python's ssl module) as the client.
'
'   gbasic tls_server.bas CERTDIR READY_FILE
'
' Three listeners, their ports written to READY_FILE as "tls plain small":
'   tls    certs for localhost (the default) and alt.test (by SNI), and a
'          client certificate requested but not required. Lines: "who" answers
'          what the handshake established, "flood" writes 10 MiB and closes,
'          "quit" closes everything, anything else is echoed as "echo:" + line.
'   plain  no TLS until the client says STARTTLS; then start_tls, and lines are
'          echoed as "tls:" + line.
'   small  TLS with a 1 KiB receive buffer: echoes whatever arrives.
' "drip" is 10 MiB as 160 writes, made on `writable` events against a slow
' reader. The small listener's idle is 2 seconds, so a client that never
' handshakes is closed. A handshake that fails prints "ERROR <code> listener=<which>" -- the event
' names the listener, since the program was never handed the connection.
load net

program main(args)
    d = args[0]
    G = { dir: d }
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            on_event(e)
        end while
    end watch
    G.tls = net.listen({ port: 0, tls: { cert: d + "/server.pem", key: d + "/server.key",
        certs: [{ host: "alt.test", cert: d + "/alt.pem", key: d + "/alt.key" }],
        client_ca: d + "/ca.pem" } })
    G.plain = net.listen({ port: 0 })
    G.small = net.listen({ port: 0, buffer: 1024, idle: 2 seconds, tls: { cert: d + "/server.pem", key: d + "/server.key" } })
    print "hosts " + string(G.tls.tls.hosts) + " client " + G.tls.tls.client_certificate
    ready {file}= args[1]
    write(ready, string(G.tls.port) + " " + string(G.plain.port) + " " + string(G.small.port))
end program

function which(l)
    if l = G.tls then
        return "tls"
    end if
    if l = G.small then
        return "small"
    end if
    return "plain"
end function

function on_event(e)
    c = e.connection
    if e.kind = "error" and not is_nothing(e.listener) and not is_nothing(c) then
        print "ERROR " + e.code + " listener=" + which(e.listener)
        return nothing
    end if
    if e.kind = "writable" and has(G, "drip") and c = G.drip then
        drip_more(c)
        return nothing
    end if
    if e.kind = "tls" then
        print "TLS UP " + c.tls.version
        return nothing
    end if
    if e.kind = "accepted" and e.listener = G.tls then
        if is_nothing(c.tls) then
            print "MISMATCH an accepted TLS connection has no tls facts"
        end if
        return nothing
    end if
    if e.kind != "data" then
        return nothing
    end if
    if c.local.port = G.small.port then
        c.write(c.take())
        return nothing
    end if
    line = c.take_line()
    while not is_unknown(line)
        if c.local.port = G.plain.port then
            if is_nothing(c.tls) and line = "STARTTLS" then
                c.write("OK" + chr(10))
                c.start_tls({ cert: G.dir + "/server.pem", key: G.dir + "/server.key" })
            else
                c.write("tls:" + line + chr(10))
            end if
        else
            if line = "quit" then
                c.close()
                G.tls.close()
                G.plain.close()
                G.small.close()
                return nothing
            end if
            if line = "drip" then
                ' many writes, each made as the queue drains: the queue moves
                ' (compaction) between a write libssl could not finish and its retry
                G.drip = c
                G.dripped = 0
                drip_more(c)
                return nothing
            end if
            if line = "flood" then
                c.write(repeat("0123456789abcdef", 655360))
                c.close()
                return nothing
            end if
            if line = "who" then
                client = "none"
                if not is_nothing(c.tls.peer_certificate) then
                    client = c.tls.peer_certificate.subject
                end if
                c.write("sni=" + string(c.tls.server_name) + " client=" + client + chr(10))
            else
                c.write("echo:" + line + chr(10))
            end if
        end if
        line = c.take_line()
    end while
    return nothing
end function

' 160 writes of 64 KiB (10 MiB), each when the queue has drained below its
' low-water mark, then a close.
function drip_more(c)
    while G.dripped < 160 and c.queued() < 65536
        c.write(repeat("0123456789abcdef", 4096))
        G.dripped = G.dripped + 1
    end while
    if G.dripped >= 160 then
        c.close()
    end if
    return nothing
end function

' net, TLS, WAITED SERVER: a program that blocks in accept on a TLS listener.
' Run by tests/run_net.sh, with tests/net/tls_peer.py as the clients.
'
'   gbasic tls_waited_server.bas CERTDIR READY_FILE
'
' Writes its port to READY_FILE, then accepts two connections. The runner sends
' a plaintext client first, whose handshake fails, so the first accept must
' RAISE (tls_failed) rather than hand back a connection; the second is a real
' TLS client, whose line is echoed. Prints what each accept did.
load net

function try_accept(l, unused)
    on error goto handle
    c = l.accept(30 seconds)
    return c
handle:
    print "ACCEPT RAISED " + error.details.code
    error.clear()
    return nothing
end function

program main(args)
    d = args[0]
    l = net.listen({ port: 0, tls: { cert: d + "/server.pem", key: d + "/server.key" } })
    ready {file}= args[1]
    write(ready, string(l.port))
    first = try_accept(l, nothing)
    if not is_nothing(first) then
        print "MISMATCH a plaintext client was accepted"
    end if
    c = try_accept(l, nothing)
    if is_nothing(c) then
        print "MISMATCH the TLS client was not accepted"
        return
    end if
    print "ACCEPTED " + c.tls.version
    line = c.read_line(10 seconds)
    c.write("echo:" + line + chr(10))
    c.close()
    l.close()
end program

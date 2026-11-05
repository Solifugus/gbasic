' net, WATCHED CLIENT: connections whose progress arrives as events. Run by
' tests/run_net.sh against a Python echo server.
'
'   gbasic watched_client.bas ECHO_PORT DEAD_PORT
'
' Self-checking. Prints `checks:` and `mismatches:` when the loop has nothing
' left to do, which is itself a check: a watched socket that never reports
' `closed` keeps the loop alive, and the runner's timeout says so.
load net

program main(args)
    G = { checks: 0, mismatches: 0, finished: 0, seen: [], lines: [], refused: [], idle: [] }
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            on_event(e)
        end while
    end watch
    ' BY NAME, so the lookup runs on the resolver thread rather than the loop.
    G.echo = net.connect({ host: "localhost", port: number(args[0]), timeout: 5 seconds })
    G.dead = net.connect({ host: "127.0.0.1", port: number(args[1]), timeout: 5 seconds })
    G.quiet = net.connect({ host: "127.0.0.1", port: number(args[0]), idle: 1 seconds })
    check("a watched connect returns at once", G.echo.watched, true)
end program

function on_event(e)
    if e.connection = G.echo then
        append(G.seen, e.kind)
        if e.kind = "connected" then
            e.connection.write("first" + chr(10) + "sec")
            e.connection.write("ond" + chr(10))
        end if
        if e.kind = "data" then
            line = e.connection.take_line()
            while not is_unknown(line)
                append(G.lines, line)
                line = e.connection.take_line()
            end while
            if count(G.lines) = 2 then
                e.connection.close()
            end if
        end if
        if e.kind = "closed" then
            check("connected came first", G.seen[0], "connected")
            check("both lines came back, the second written in two pieces", G.lines, ["first", "second"])
            check("closed by this side", e.code, "local")
            done()
        end if
    end if
    if e.connection = G.dead then
        append(G.refused, e.kind)
        if e.kind = "closed" then
            check("a refused connect is an error event, then closed", G.refused, ["error", "closed"])
            check("  with the code", e.code, "refused")
            done()
        end if
    end if
    if e.connection = G.quiet then
        append(G.idle, e.kind)
        if e.kind = "closed" then
            check("an idle connection is reported idle, then closed", G.idle, ["connected", "idle", "closed"])
            check("  with the code idle", e.code, "idle")
            done()
        end if
    end if
    return nothing
end function

' The summary once all three connections have finished.
function done()
    G.finished = G.finished + 1
    if G.finished = 3 then
        print "checks: " + string(G.checks)
        print "mismatches: " + string(G.mismatches)
    end if
    return nothing
end function

function check(label, got, want)
    G.checks = G.checks + 1
    if type(got) = type(want) and string(got) = string(want) then
        print "ok   " + label
    else
        G.mismatches = G.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

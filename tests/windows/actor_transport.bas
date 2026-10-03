' Actors over each platform's TRANSPORT, checked by what arrives.
'
' Linux carries a message as one SOCK_SEQPACKET frame and a handle as a
' descriptor (SCM_RIGHTS). Windows has neither: its AF_UNIX is a STREAM, so
' src/actor.c writes a length prefix, gives every sending process its own
' connection, and sends a handle as its inbox's PATH. The examples prove the
' actor FEATURES on both; this file attacks what the Windows transport
' rebuilt, where a defect would look like an ordinary message:
'
'   - several senders at once, each payload distinct and self-describing, so a
'     frame split, spliced or delivered out of its sender's order cannot
'     reproduce what was sent;
'   - a handle passed AT RUNTIME to an actor that has never heard of its
'     target, which must then reach it;
'   - a message larger than the socket's buffer, which the stream must carry
'     whole (Windows) or refuse as too large (Linux's frame is one datagram);
'   - a send to an actor that has gone, which must raise rather than vanish
'     (Windows measured a send to a closed peer reporting SUCCESS once).
'
'     gbasic tests/windows/actor_transport.bas     (from the repository root)
'
' "mismatches: 0" is a pass.

tally = { checks: 0, bad: 0 }

function ok(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print("ok   " + label)
    else
        print("MISMATCH " + label + ": got [" + left(string(got), 200) + "] want [" + left(string(want), 200) + "]")
        tally.bad = tally.bad + 1
    end if
end function

' Distinct for every (sender, i) and of varying length, from ~2 KB to ~24 KB.
function payload(name, i)
    return repeat(name + ":" + string(i) + ";", 200 + i * 90)
end function

' Send, waiting out a FULL mailbox: a sender faster than its reader is
' ordinary, and the contract is that it is told (it raises) rather than
' that the message is dropped.
function send_patiently(target, msg)
    while true
        on error goto next
        send(target, msg)
        if error then
            if not contains(error.message, "full") then
                e = error
                error.clear()
                error e
            end if
            error.clear()
            sleep(0.01)
        else
            return nothing
        end if
    end while
end function

function sender(parent, name, count)
    for i = 1 to count
        send_patiently(parent, [name, i, payload(name, i)])
    next
end function

' Sends whatever it receives back to `parent`.
function echo(parent)
    while true
        m = receive()
        if m = "stop" then
            return nothing
        end if
        send(parent, m)
    end while
end function

' Receives a handle it has never seen, then uses it.
function relay()
    m = receive()
    send(m[0], ["via relay", m[1]])
end function

' Tells `parent` whatever reaches it.
function sink(parent)
    m = receive()
    send(parent, ["sink got", m])
end function

function quitter()
    return nothing
end function

function attempt_send(target, msg)
    on error goto next
    send(target, msg)
    if error then
        m = error.message
        error.clear()
        return "refused: " + m
    end if
    return "sent"
end function

on_windows = env("OS") = "Windows_NT"

print("-- self --")
send(self(), ["to", "me"])
ok("a message to self() comes back", receive(), ["to", "me"])

print("-- a handle in the spawn arguments --")
e = spawn echo(self())
send(e, "ping")
ok("the child replies through the handle it was given", receive(5 seconds), "ping")

print("-- four senders at once --")
names = ["a", "b", "c", "d"]
count = 25
senders = []
for each n in names
    h = spawn sender(self(), n, count)
    append(senders, h)
end for
seen = { a: 0, b: 0, c: 0, d: 0 }
received = 0
intact = 0
in_order = 0
for k = 1 to len(names) * count
    m = receive(30 seconds)
    if is_nothing(m) then
        break
    end if
    received = received + 1
    if m[2] = payload(m[0], m[1]) then
        intact = intact + 1
    end if
    if m[1] = seen[m[0]] + 1 then
        in_order = in_order + 1
    end if
    seen[m[0]] = m[1]
end for
ok("every message arrived", received, 4 * count)
ok("every payload arrived byte for byte", intact, 4 * count)
ok("each sender's messages arrived in the order it sent them", in_order, 4 * count)

print("-- a handle passed at runtime --")
r = spawn relay()
s = spawn sink(self())
send(r, [s, "hello"])
ok("an actor reaches a target it learned of in a message", receive(5 seconds), ["sink got", ["via relay", "hello"]])

print("-- larger than the socket buffer --")
big = repeat("0123456789abcdef", 65536)
ok("the large message is 1 MiB", len(big), 1048576)
got = attempt_send(e, big)
if on_windows then
    ok("Windows carries 1 MiB as one message", got, "sent")
    back = receive(10 seconds)
    ok("and it comes back whole", is_nothing(back) = false and back = big, true)
else
    ok("Linux refuses 1 MiB as one frame", contains(got, "too large"), true)
end if
huge = repeat(big, 16)
ok("16 MiB is refused everywhere, by size", contains(attempt_send(e, huge), "too large"), true)

print("-- a send to an actor that has gone --")
q = spawn quitter()
monitor(q)
d = receive("down", 10 seconds)
ok("the quitter is reported down, normally", d[2], "normal")
ok("a send to it raises instead of vanishing", contains(attempt_send(q, "anyone?"), "no longer reachable"), true)

send(e, "stop")

' 12 on Windows; 11 on Linux, where the 1 MiB message is refused (one check)
' rather than carried and echoed back (two).
if tally.checks < 11 then
    print("BROKEN: only " + string(tally.checks) + " checks ran")
else
    print("mismatches: " + string(tally.bad))
end if

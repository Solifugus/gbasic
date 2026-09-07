' THE WHOLE CHAIN: a request handler that calls a tool without blocking.
'
' This is the claim the pool exists to make good. A handler runs on the event
' loop's thread, so a tool body running there freezes every other client for
' its whole duration -- and moving the body to a worker helps only if the
' REPLY also arrives without blocking. It does: the worker's answer comes back
' through the interpreter's inbox, which is in the loop's own `poll` set, and
' the response is appended for a request that arrived earlier.
'
' The tool sleeps 0.8s deliberately. The runner asserts an ORDERING -- both
' handlers must have STARTED before either answer appears -- because that, and
' not the fact that both clients got a body, is what "the loop was never
' blocked" means.
load tools
load webserver

function slow_lookup(args)
    sleep(0.8)
    return "answer for " + args.q
end function

function tool_worker(back, ts)
    tools.serve(back, ts)
    return nothing
end function

ts = tools.define("m", [
    { name: "lookup", describe: "slow", params: [
        { name: "q", type: "string", describe: "the query", required: true }],
      reads: ["db"], mutates: [], fn: slow_lookup }
])

pending = {}
server = webserver.listen(number(env("PORT")))
me = self()
handles = []
for i = 1 to 2
    append(handles, spawn tool_worker(me, ts))
end for
p = tools.pool(handles)
seq = 0

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        seq = seq + 1
        cid = "c" + string(seq)
        pending[cid] = req.id
        p = tools.send_call(p, tools.request({ user: req.headers["x-user"] }, "run", cid, "lookup", { q: req.query.q }))
        print "started " + string(req.id)
    end while
end watch

watch(inbox.messages)
    while count(inbox.messages) > 0
        m = take_first(inbox.messages)
        if tools.is_result(m) then
            rid = pending[m.call_id]
            append(server.responses, { id: rid, body: string(m.result.content) })
            print "answered " + string(rid)
        end if
    end while
end watch

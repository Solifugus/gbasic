' THE POOL, from a sequential program.
'
' Three calls in flight across three workers. The replies must arrive OUT OF
' ORDER -- c3 has no sleep and is sent last -- which is what says the bodies
' ran in parallel rather than one after another; a test that only checked all
' three came back passes on a pool of one worker serving them in turn.
'
' It also asserts the PRINCIPAL HANDOFF: c3 carries `{ user: "iris" }` and the
' tool reports what `principal()` says inside the worker. A principal does not
' cross `spawn`, so the only way that reaches the body is the worker
' re-entering the scope from the message -- explicit, and auditable because it
' was written down.
'
' THE CALLER SPAWNS, not the library: `spawn` resolves a bare function NAME, so
' it takes neither a library function nor a function value. The worker loop
' still lives in `tools.serve`, so the protocol has one definition.
load tools

function slow_add(args)
    sleep(0.3)
    return args.a + args.b
end function

function whoami(args)
    p = principal()
    if p = nothing then return "nobody"
    return p.user
end function

function tool_worker(back, ts)
    tools.serve(back, ts)
    return nothing
end function

ts = tools.define("m", [
    { name: "slow_add", describe: "adds slowly", params: [
        { name: "a", type: "number", describe: "x", required: true },
        { name: "b", type: "number", describe: "y", required: true }],
      reads: [], mutates: [], fn: slow_add },
    { name: "whoami", describe: "reports the principal", params: [],
      reads: [], mutates: [], fn: whoami }
])

got = 0
watch reader(inbox.messages)
    while count(inbox.messages) > 0
        m = take_first(inbox.messages)
        if tools.is_result(m) then
            got = got + 1
            print "reply " + string(m.call_id) + ": " + string(m.result.content) + " is_error=" + string(m.result.is_error)
        end if
    end while
    if got >= 3 then
        print "all in"
        unwatch reader
    end if
end watch

me = self()
handles = []
for i = 1 to 3
    append(handles, spawn tool_worker(me, ts))
end for
p = tools.pool(handles)

p = tools.send_call(p, tools.request(nothing, "r1", "c1", "slow_add", { a: 1, b: 2 }))
p = tools.send_call(p, tools.request(nothing, "r1", "c2", "slow_add", { a: 10, b: 20 }))
p = tools.send_call(p, tools.request({ user: "iris" }, "r1", "c3", "whoami", {}))
print "three calls in flight, main returns"

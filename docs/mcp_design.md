# `mcp`: the Model Context Protocol, both directions

**Status:** Shipped (2026-09-07), publishing and consuming. `stdlib/mcp.bas`,
`tests/run_mcp.sh`. Step 6 of
[the AI reference proposal](gbasic_ai_reference_and_primitives.md) (§1.11).

## 1. Two transports, one dispatcher

`mcp.handle(ts, opts, line)` turns a JSON-RPC request into a reply and **does
no I/O**. The stdio loop and an HTTP `server` block are thin wrappers over it.

That factoring is the point rather than tidiness: the protocol is testable with
neither transport present, and the transports are then tested only for what a
transport can get wrong — buffering and framing, not semantics. The suite
asserts it directly, by requiring the same request to give a **byte-identical**
reply over both, which is not implied by both merely working.

```basic
' stdio: what a desktop client launches
mcp.serve_stdio(ts, { name: "bank-mcp", version: "1.0.0" })

' HTTP: your own server block, same dispatcher
post "/rpc"( req )
    return { body: mcp.handle(TOOLSET, OPTS, req.body),
             headers: { "Content-Type": "application/json" } }
end post
```

| Call | What it does |
| --- | --- |
| `mcp.handle(ts, opts, line)` | one JSON-RPC request → the reply, or `""` for a notification |
| `mcp.serve_stdio(ts, opts)` | the stdio loop |
| `mcp.tool_list(ts)` | the `tools/list` payload |
| `mcp.check_options(opts)` | validate a server's options, including the ceiling |

Implemented methods: `initialize`, `tools/list`, `tools/call`. Anything else is
`-32601`, and a notification (no `id`) is answered with silence.

## 2. Run stdio under `--line-buffered`

A desktop client launches the server as a subprocess and waits for a reply on
its stdout. Block-buffered, that reply sits in the pipe buffer until the process
exits — and the process is waiting for the client's next request. **That is a
deadlock, not slowness.**

`tests/run_mcp.sh` demonstrates it rather than asserting it: the same session is
driven twice, and without the flag every request times out. The tier also fails
if any request *is* answered, because that would mean the transport was never
buffer-bound and the tier had been proving nothing.

## 3. A failed tool is not a protocol error

This is the distinction the protocol tier exists for, and collapsing it either
way produces a well-formed reply that means the wrong thing.

- A tool that **raises**, or reports a miss, or gets bad arguments: a
  **result** with `isError: true`. The model asked for something reasonable and
  got an answer it can react to — pick another tool, ask the user, try
  different arguments.
- A method that **does not exist**, unparseable JSON, a request that is not an
  object: a **JSON-RPC error**. The *client* is malformed, and it cannot fix
  that by trying again differently.

An unknown *tool* is a result, not an error — the client is well-formed and the
model can choose again.

## 4. The mapped principal is the leak surface

A read-only tool published over MCP is answered by whatever principal the
configuration maps the calling agent to. A service account standing in for
everybody is exactly the god-view this design exists to avoid, so:

- a `principal` may only be declared alongside a **`max_groups` ceiling**, and
  the server **refuses to start** when it is exceeded;
- a ceiling with no principal to bound is refused too, since it would read as
  protection that is not protecting anything;
- **serving with no principal at all is legal, and is the narrowest state** —
  tools see `principal() = nothing`, and what they do about that is their own
  rule.

This is the argument the `ldap` module made for referral chasing: on a path that
carries authority, a convenience is an instruction to hand something to someone
the operator never named.

Dispatch happens inside `with principal(opts.principal)`, so a tool asking
`principal()` gets the configured identity rather than whatever the server
process happens to be.

## 5. A blank line ends a stdio session

gBASIC's `input()` returns `""` for both a blank line and end of input, and
offers no way to tell them apart. JSON-RPC never sends a blank line, so this is
correct for the protocol — but it is the language deciding rather than this
library, and it is recorded in `DOGFOOD.md`.

## 6. Consuming: gBASIC as an MCP client

The other direction. Publishing hands a toolset to somebody else's agent;
consuming lets ours call somebody else's tools. Same two transports, and the
stdio one is why `process.write` had to exist — `process.start` used to hand
back a live child you could only *listen* to, and a stdio transport is a
conversation.

```basic
fs = mcp.connect({ transport: "stdio", command: "gbasic",
                   args: ["--line-buffered", "fileserver.bas"],
                   expect: ["read_file"] })
part = mcp.call(fs, "read_file", { path: "/etc/hostname" })
x = mcp.disconnect(fs)
```

| Call | What it does |
| --- | --- |
| `mcp.connect(spec)` | launch or address a server, handshake, fetch its tool list |
| `mcp.tools_of(h)` | the advertised tools, as the server described them |
| `mcp.call(h, name, args)` | one `tools/call` → a tool-result part |
| `mcp.disconnect(h)` | end the session; for stdio, close stdin and reap the child |

A spec is `{ transport, command, args, url, expect }`; unknown fields are
refused **by name**, the rule `webserver.listen` already follows.

**The declaration is checked against the server at connect time.** `expect`
names the tools the caller relies on, and connecting to a server that does not
advertise one of them fails *there* — not at the first call, hours later, inside
whatever the agent happened to be doing. §1.11 asks for load-time and
connect-time failures to be distinct diagnostics, and they are: a malformed spec
is refused by `connect` before anything is launched, and a server that
disagrees with the declaration is refused after.

**A remote result is the same shape as a local one.** `mcp.call` returns the
tool-result part `tools.dispatch` returns, so whatever consumes it — an `agent`
transcript, a model turn — cannot tell a remote tool from a local one. A
protocol error from the far end becomes an error *result* here, because from
the caller's side "that call did not work" is one outcome however the server
chose to phrase it; the message says which it was.

**A server that never answers must diagnose, not hang.** `_read_line` is
bounded, and the reason is the standing one in this tree: a hang is not a
failure, it is a suite that never reports.

`process` needs no `load` — it is unconditional, like `money` and `reflect`.
`webclient` does, and is loaded lazily at the http transport, so a program
consuming over stdio does not drag libcurl in behind it.

### `via: { mcp: ... }` is deliberately not built

This is a decision rather than a deferral. It would couple `tools` to `mcp`:
`tools.dispatch` would have to know how to reach a server, so `tools` would load
`mcp`, and every program using a toolset would carry the client whether or not
it consumed anything. The same thing is two lines in the application, where the
coupling belongs:

```basic
function read_file(args)
    return mcp.call(FILESHARE, "read_file", args).content
end function
```

### What is tested, and what is not

`tests/run_mcp.sh` drives **gBASIC consuming gBASIC** over both transports —
our own publisher is the server, which needs nothing external and exercises the
whole round trip. It has never met a third-party MCP server, and the failure
that would find is a disagreement about the protocol that both halves share, so
the round trip cannot see it. That limit is real and is why the suite asserts
the *wire* (byte-identical replies across transports, JSON-RPC shapes) rather
than only that a call returned something.

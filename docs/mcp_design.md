# `mcp`: publishing a toolset over the Model Context Protocol

**Status:** Shipped (2026-09-07), publishing only. `stdlib/mcp.bas`,
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

## 6. Not built: consuming

A connect/list/call client surface, and the `via: { mcp: "..." }` tool binding
— gBASIC as an MCP *client*. Written without call syntax deliberately:
`run_stdlib_docs.sh` reads a documented call as a promise, and this page is
Partial rather than a Proposal, so naming them that way would claim they exist.

Deliberately a separate increment. It is the other direction, it needs `via:` in
`tools` (which was left out for its own reasons), and its failure modes are
different: a server whose advertised tool list disagrees with the declaration,
load-time versus connect-time diagnostics, and a remote failure that must not
look like a local one. Publishing and consuming share only `tools`.

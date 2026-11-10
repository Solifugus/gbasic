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
| `mcp.handle(ts, opts, line [, caller])` | one JSON-RPC request → the reply, or `""` for a notification. `ts` is a toolset or a declaration from `mcp.server`; `caller` is this request's principal (§4) |
| `mcp.server(ts, { resources, templates, prompts })` | a declaration with resources and prompts beside the tools (§7) |
| `mcp.serve_stdio(ts, opts)` | the stdio loop |
| `mcp.tool_list(ts)` | the `tools/list` payload |
| `mcp.resource_list(srv)`, `mcp.template_list(srv)`, `mcp.prompt_list(srv)` | the `resources/list`, `resources/templates/list` and `prompts/list` payloads of a declaration (§7) |
| `mcp.check_options(opts)` | validate a server's options, including the ceiling |
| `mcp.text(t)`, `mcp.image(bytes, mime_type)`, `mcp.content(blocks)` | a tool's result as content blocks, an image among them (below) |

Implemented methods: `initialize`, `tools/list`, `tools/call`, and with a
declaration `resources/list`, `resources/templates/list`, `resources/read`,
`prompts/list` and `prompts/get`. Anything else is `-32601`, and a
notification (no `id`) is answered with silence.

**Structured results** (2026-11-06, Transward §7.7). A record result is
answered as `structuredContent` beside its JSON text block, which stays for a
client that predates it; an error result or a text result carries none. A tool
that declares `output` (docs/tools_design.md) is published with that schema as
its `outputSchema`, and a result that does not match it becomes an error
result.

**Content blocks** (2026-11-10, Studio, docs/qt_gui_design.md §0.38 D2). A
tool answers an image as well as text by returning
`mcp.content([mcp.text("..."), mcp.image(bytes, "image/png")])`; the blocks go
out as MCP's own `content` array (an image as base64 `data` with its
`mimeType`) and no `structuredContent`. It is a wrapper rather than "a record
with a `content` field" so that a tool whose ordinary answer has such a field
is not misread; a tool that declares `output` and answers blocks is an error
result. On the client side `mcp.call` keeps `content` as the text blocks
joined and adds `blocks` -- every block, an image's `data` decoded to `bytes`
with its `mime_type` -- only when a block is not text, so a text-only result
keeps the shape `tools.dispatch` returns.

**A tool's function is called as a method of its entry** (since 2026-11-06,
Transward §7.2), so `this` is the entry and `this.name` says which tool is
being served. Function values are not closures, so this is how one function
serves many entries; a function that never reads `this` is called as before.

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

**A principal per request** (2026-11-06, Transward §7.3). Over HTTP every
request carries its own bearer token, so who a call is for is decided per
request, not per server. `mcp.handle(ts, opts, line, caller)` runs the tool
(and a resource or prompt) under `caller`, a record the application has
already verified, which passes through untouched: an application's own fields
reach `principal()` as given. It takes the place of `opts.principal` for that
request. The `max_groups` ceiling is NOT applied to it: the ceiling bounds a
static mapping that stands in for whoever calls, and a principal established
from a verified token stands for one caller. A `caller` that is not a record
is refused.

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

A spec is `{ transport, command, args, env, url, expect, headers, client }`;
unknown fields are refused **by name**, the rule `webserver.listen` already
follows. `headers` (http only) is a record of string values sent with every
request: a bearer token goes here, never in a command line, where any user on
the machine can read it. `client` is the name given to the server at
`initialize` (default `gbasic`). `env` (stdio only) is merged over this
process's environment for the child, as `process.start` merges it (`nothing`
unsets a name): how a parent hands a child a token without a command line.

**Over HTTP the client speaks Streamable HTTP** (2026-11-06, Transward §7.5):
`initialize` asks for the newest version this library speaks, sends
`notifications/initialized` after it, keeps the `Mcp-Session-Id` the server
issued and the version agreed, and sends both on every later request with
`Accept: application/json, text/event-stream`. A reply that comes back as an
event stream is read for its response. A 404 inside a session says the server
no longer knows it, and connecting again is the remedy. **Request ids are
random**, not increasing: the handle is a value, so a counter in it could not
advance across calls, and JSON-RPC needs ids unique within a session, which a
random one is (Matthew's ruling).

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

## 7. Resources and prompts (2026-11-06, Transward §7.1)

`mcp.server(ts, decl)` declares what a server publishes beside its tools, and
`mcp.handle` takes the declaration where it takes a toolset:

```basic
srv = mcp.server(ts, {
    resources: [ { uri: "app://health", name: "health", mime: "application/json", fn: health } ],
    templates: [ { uri: "app://runs/{run_id}/transcript", name: "transcript",
                   mime: "text/plain", fn: transcript } ],
    prompts:   [ { name: "diagnose", describe: "Diagnose a failed run.",
                   arguments: [ { name: "run_id", required: true } ], fn: diagnose } ]
})
```

- **One declaration**, as `tools.define` is for tools: the three `*/list`
  answers derive from it and `resources/read` and `prompts/get` check against
  it, so they cannot drift. It is checked once, when declared: closed field
  sets, a function for every entry, no URI or prompt twice, a template with at
  least one `{name}` part and a resource with none.
- **A template part** matches one or more characters up to the next literal
  text, never a `/`, and reaches the function as written (not
  percent-decoded): `fn({ run_id: "r-17" })`. Two parts with nothing between
  them are refused, since where one ends could not be told. A declared
  resource wins over a template; templates are tried in declared order.
- **A prompt's function** answers the text of one user message, or
  `[{ role, text }]` for several (`role` is `"user"` or `"assistant"`). Its
  arguments are strings, checked as a tool's are: a required one left out and
  an undeclared one are refused (`-32602`).
- **Every function** runs as a method of its entry, under the request's
  principal, exactly as a tool does. A text answer is sent as text; anything
  else as JSON, typed `application/json` unless the entry declares a `mime`.
- **Errors** are JSON-RPC errors, since a resource has no `isError` result: an
  unknown URI is MCP's `-32002`, a function that raises is `-32603` saying
  why.
- `initialize` advertises `resources` and `prompts` only when the declaration
  has some.

Tested by `tests/mcp_test.bas` (run_mcp.sh's protocol tier): listing,
reading through one and two parts, a part refused across a `/`, the request's
principal reaching a resource, the not-found and raising cases, both prompt
shapes, argument refusals, and seven declaration refusals. Perturbed, each red
alone: a part allowed to span `/`, a resource run without the caller's
principal, required arguments unchecked, the capability never advertised.

## 8. Streamable HTTP (2026-11-06, Transward §7.4)

`mcp.handle_http(srv, opts, req [, caller])` takes the request record a
`server` route receives and answers the response record it returns, so the
transport is written once, here, rather than by every application:

```basic
server rpc( port: 8080 )
    post "/mcp"( req )
        return answer(req)
    end post
    get "/mcp"( req )
        return answer(req)
    end get
    delete "/mcp"( req )
        return answer(req)
    end delete
end server

function answer(req)
    ' the application authenticates the request, and says who it is for
    caller = verify_bearer(req)
    if caller = nothing then
        return { status: 401, body: "" }
    end if
    s = mcp.session_of(OPTS, req)       ' { client, version }, or nothing
    return mcp.handle_http(SRV, OPTS, req, caller)
end function
```

`opts` adds `origins` (the browser origins allowed, `[]` for none),
`session_secret` (at least 32 bytes, counted as bytes, the same in every worker) and
`session_seconds` (default 86400). They are checked on every call.

- **Origin, on every request.** One a browser sends from an origin not listed
  is refused 403; that is what stops a page on another site reaching the server
  through the user's browser (DNS rebinding), and it matters most when MCP
  shares a listener with a web UI, as Transward's does. A request with no
  Origin is not a browser's and is served.
- **One message per POST.** A request is answered `application/json`; a
  notification or a client's response is answered 202 with no body; a batch is
  refused 400. GET is 405 (`Allow: POST`): there is no server-to-client stream.
- **Sessions are signed, not stored.** `initialize` issues `Mcp-Session-Id`:
  the client's name, the agreed version and an expiry, base64url-encoded and
  signed with HMAC-SHA256 under `session_secret`. Every later request must
  carry it: 400 without, 404 for one that is forged, signed with another
  secret, or expired, which tells the client to initialize again. **Why not a
  table:** a `server` block with `workers: N` is N processes, so a table in
  memory would issue a session in one worker and refuse it in the next. The
  run_mcp tier runs two workers and counts the distinct workers that served one
  session (two, after 2 to 45 calls in 18 runs here). **The cost:** a session
  cannot be ended before it expires, so DELETE is 405, which the specification
  allows ("the server does not allow clients to terminate sessions"). A
  session id is a bearer of nothing but the client's name and version; the
  application's own authentication still decides every request.
- **Versions.** `initialize` answers the client's requested version when this
  library speaks it (2025-06-18, 2025-03-26, 2024-11-05), else the newest; the
  stdio transport does the same, through `mcp.handle`. An
  `MCP-Protocol-Version` header naming a version not spoken is 400.
- **`mcp.session_of(opts, req)`** gives the application what a session says,
  `{ client, version }`, or `nothing`; Transward records the client's name in
  its audit.
- **What it needs:** `hmac_sha256` and `bytes_equal`, which are libcrypto's.
  `mcp` does not load `crypto`, so a build without libcrypto still loads `mcp`
  for stdio, and `handle_http` raises there.

Tested by tests/mcp_test.bas with request records and no socket (initialize,
a session used, missing, forged, from another secret, expired, a notification
and a response, an origin refused and allowed, GET, DELETE, a batch, a version
not spoken, the caller over HTTP, two option refusals), and by run_mcp.sh's
streamable tier over a real server block with two workers, curl and the
gBASIC client. Perturbed, each red alone: the signature unchecked, the origin
unchecked, the expiry ignored, the client's session id not sent, the spec's
headers not sent.


# Webserver Standard Module Design

## Status

**This document describes Phase 1, which is one of five phases now shipped.**
Read it as the record of how the queue/watch core was designed, not as the
current feature set — in particular, the "Explicitly Rejected For Phase 1"
section below rejects route tables, static file serving and middleware, and
the first two have since shipped.

What arrived after this document was written (PLAT-WEB-1..5, each with its own
suite):

| Phase | Brought | Suite |
|---|---|---|
| WEB-1 | bind address, the route table as data, `web.static` | `run_web_bind.sh`, `run_web_routes.sh` |
| WEB-2 | process worker pool, drain, rolling reload | `run_web_pool.sh` |
| WEB-3 | request/idle timeouts, smuggling-resistant framing, TLS with SNI | `run_web_hardening.sh`, `run_web_tls.sh` |
| WEB-4 | streaming responses and direct file serving | `run_web_stream.sh` |
| WEB-5 | the declarative `server` block | `run_web_server_block.sh` |

Current behaviour is documented in
[reference.md](reference.md#webserver-module). Still unsupported: chunked
request bodies, WebSockets, multipart uploads, templates and sessions.
See `docs/historical_development_archive.md` for the completed Phase 1
history.

## Core Principle

The Webserver module should use gBASIC watchers and live records instead of
callback-style event handlers.

Incoming requests are appended to a watched queue. Application code removes
requests from that queue and appends matching response records to a response
queue. The queue/watch model remains the foundation even if later phases add
routing helpers.

```basic
load webserver

server = webserver.listen(8080)

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)

        headers = {}
        headers["content-type"] = "text/plain"

        append(server.responses, {
            id:req.id,
            status:200,
            headers:headers,
            body:"Hello"
        })
    end while
end watch
```

The examples use the currently implemented `watch(...)` syntax.

## Goals

- Provide a small HTTP server suitable for local services and ordinary web
  APIs.
- Make requests and responses feel like normal gBASIC records and arrays.
- Integrate with existing watcher behavior instead of introducing callbacks.
- Keep application execution single-threaded and deterministic in Phase 1.
- Use runtime errors for invalid API calls and invalid response records.
- Bound request size and response wait time so stalled clients cannot consume
  resources indefinitely.
- Avoid becoming a framework in Phase 1.

## Recommended Phase 1 API

```basic
load webserver

server = webserver.listen(port)
webserver.close(server)
response = webserver.redirect(request, location)
response = webserver.redirect(request, location, status)
```

`webserver.listen(port)` should:

- require a whole number from 0 through 65535
- treat port `0` as a request for an operating-system-assigned ephemeral port
- bind to the requested port immediately
- begin listening before returning
- raise a runtime error if the address cannot be bound or listening fails
- return a live server record

`webserver.redirect(request, location[, status])` should:

- read the positive `id` from an ordinary request record,
- require a non-empty string location without CR/LF,
- default to HTTP `303`,
- accept only `301`, `302`, `303`, `307`, and `308`,
- return an ordinary response record with a `location` header and empty body.

The bind address is loopback by default. Public network binding needs to be
explicit because an accidental public listener is a security problem.
**Implemented 2026-08-21 (PLAT-WEB-1 Gap A):** the options-record form is
`webserver.listen(port, { address: "0.0.0.0" })`, the default is unchanged,
and `tests/run_web_bind.sh` holds that default in place by probing a second
loopback address that a `127.0.0.1`-bound socket must not answer on.

The returned value should expose:

```basic
{
    requests:[],
    responses:[],
    port:8080,
    running:true
}
```

`port` is the actual bound port. This matters when tests request port `0`, as
described in the test plan. `running` becomes `false` after shutdown.

The record is live: it is associated internally with native listener state.
It is not a plain record literal that can be copied to create another server.
Its public fields still use ordinary gBASIC values.

### Request Queue

`server.requests` should be an ordinary mutable array. Each entry is a new
ordinary record:

```basic
{
    id:1,
    method:"POST",
    path:"/messages",
    query:{
        draft:"true"
    },
    headers:{},
    cookies:{},
    body:"{\"message\":\"hello\"}",
    json:{
        message:"hello"
    }
}
```

Fields:

- `id`: unique positive number assigned by the server
- `method`: uppercase HTTP method string
- `path`: request path, retaining percent encoding in Phase 1
- `query`: record of percent-decoded query parameter names and string values;
  duplicate names use last-wins behavior in Phase 1
- `headers`: record with lowercase header names
- `cookies`: record parsed from the `Cookie` header; duplicate names use
  last-wins behavior in Phase 1
- `body`: request body as a string
- `json`: present only when a non-empty body parses successfully as JSON
- `remote_ip`: peer IP address as a string
- `remote_port`: peer port as a number
- `timestamp`: UTC request timestamp as an ISO 8601 string

`requests` remains application-owned as a queue. The module only appends new
request records. Application code normally consumes them with
`take_first(server.requests)`.

### Response Queue

`server.responses` should also be an ordinary mutable array. Application code
appends response records:

```basic
headers = {}
headers["content-type"] = "application/json"

append(server.responses, {
    id:req.id,
    status:201,
    headers:headers,
    body:encode({saved:true})
})
```

Fields:

- `id`: required request identifier
- `status`: optional numeric HTTP status, default `200`
- `headers`: optional record, default `{}`
- `cookies`: optional array of `Set-Cookie` strings, default `[]`
- `body`: optional string, default `""`

Header names and values must be strings. Cookie values must be strings without
newlines. Invalid status values, headers, cookies, bodies, missing IDs, unknown
IDs, duplicate responses, and responses for expired requests should raise a
runtime error at the append operation.

Redirect responses can be built with `webserver.redirect(req, "/path")`, which
returns the same ordinary response-record shape as handwritten responses.

The server consumes valid response records from `server.responses` after they
are appended. The array is an outbound queue, not a permanent response log.

### Matching Responses

Responses must be matched to pending requests by `id`.

FIFO position is insufficient because applications may finish requests out of
order. The ID is server-generated and opaque to application logic; code should
copy it unchanged from the request to the response.

Exactly one response is accepted for each request. Once a response is accepted
or a request expires, that ID is no longer valid.

## Execution Model

Phase 1 should use single-threaded application execution.

Native socket polling may use an internal platform polling API, but native code
must not execute a watcher or mutate gBASIC values concurrently with evaluator
execution. Network events should be transferred into `server.requests` only at
safe evaluator/event-pump points. Appending a request then triggers the normal
watcher queue.

This preserves normal watcher ordering and runtime error behavior. It also
avoids locks around arbitrary gBASIC records and arrays.

Registering a watcher should not cause the program to exit while a server is
running. After top-level statements finish, the interpreter should continue
the native event pump while at least one server remains open. It exits normally
after all servers are closed.

Slow application code blocks Phase 1 request processing. That limitation is
deliberate and should be documented rather than hidden behind concurrent
callback execution.

## Timeouts

A request should have 30 seconds to receive a valid response after it is
appended to `server.requests`.

If no response arrives:

- the server sends status `504`
- headers default to an empty record
- the body is a short plain-text timeout message
- the request ID expires
- the server continues running

The timeout is an HTTP result generated by the server, not a gBASIC runtime
error. This prevents one abandoned request from stopping the whole service.

A configurable timeout can be added through a future listen-options record.

## Body And JSON Rules

Request bodies are strings in Phase 1. Embedded-NUL or otherwise unsupported
binary bodies should receive `415 Unsupported Media Type` or
`400 Bad Request`, with the exact choice fixed during implementation.

`request.json` is additive, matching `webclient` response behavior:

- attempt strict JSON parsing for each non-empty textual body
- add `json` only when parsing succeeds
- preserve `body` unchanged
- do not reject a request merely because JSON parsing fails
- map JSON `null` to `nothing`

Response bodies are strings only. Applications use `encode(value)` explicitly
when returning JSON and set the corresponding content type themselves.

Phase 1 should enforce a fixed maximum request-body size, recommended as
8 MiB. Oversized requests should receive `413 Payload Too Large` without being
added to the application queue.

## Header Rules

Request header names should be lowercase ASCII.

Duplicate request headers should use last-wins behavior in Phase 1. This
matches the implemented WebClient Phase 1 representation and keeps header
values consistently string-typed. It does lose information for some headers,
so a later phase may introduce arrays for repeated values.

Response headers default to an empty record. Header names and values must be
strings and must reject carriage returns, newlines, and invalid header names.
The server should add protocol-required headers such as `Content-Length` when
the application did not supply them.

## Shutdown

Phase 1 should provide:

```basic
webserver.close(server)
```

Closing should:

- stop accepting new connections
- mark `server.running` as `false`
- send `503 Service Unavailable` for pending requests where practical
- discard unconsumed outbound response records
- release the listening socket and all native resources
- be idempotent

Closing from inside a watcher should be supported. The close operation should
complete after the current watcher statement reaches a safe evaluator point.

Interpreter cleanup should close any server still open, but applications
should call `webserver.close` explicitly for predictable shutdown.

## Example Programs

### Plain Text

```basic
load webserver

server = webserver.listen(8080)

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)

        append(server.responses, {
            id:req.id,
            body:"Hello from gBASIC"
        })
    end while
end watch
```

Status defaults to `200`, headers to `{}`, and body to `""` when omitted.

### JSON API

```basic
load webserver

server = webserver.listen(8080)

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)

        if req.method = "POST" and req.path = "/messages" then
            if is_unknown(req["json"]) then
                append(server.responses, {
                    id:req.id,
                    status:400,
                    body:"Expected JSON"
                })
            else
                headers = {}
                headers["content-type"] = "application/json"

                append(server.responses, {
                    id:req.id,
                    status:201,
                    headers:headers,
                    body:encode({
                        accepted:true,
                        message:req.json.message
                    })
                })
            end if
        else
            append(server.responses, {
                id:req.id,
                status:404,
                body:"Not found"
            })
        end if
    end while
end watch
```

### Explicit Shutdown

```basic
load webserver

server = webserver.listen(8080)

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)

        if req.path = "/shutdown" then
            append(server.responses, {
                id:req.id,
                body:"Shutting down"
            })
            webserver.close(server)
        else
            append(server.responses, {
                id:req.id,
                body:"Running"
            })
        end if
    end while
end watch
```

The implementation must flush an already accepted response before completing
shutdown when practical.

## Design Decisions

1. `webserver.listen(port)` immediately binds, listens, and returns a live
   server record. Binding failures are synchronous runtime errors.
2. `server.requests` and `server.responses` are ordinary mutable arrays exposed
   through a live server record.
3. Responses are matched by the server-generated request `id`.
4. A request waits 30 seconds for a response in Phase 1.
5. An unanswered request receives an automatic 504 response.
6. Request bodies are strings only.
7. `request.json` is added only after successful strict JSON parsing.
8. Response bodies are strings only.
9. An omitted response status defaults to 200.
10. Omitted response headers default to an empty record.
11. Request header names are lowercase.
12. Duplicate request headers use last-wins behavior in Phase 1.
13. Application and watcher processing is single-threaded in Phase 1.
14. `webserver.close(server)` provides explicit, idempotent shutdown.
15. Tests bind loopback port `0` and use `server.port` to discover the assigned
    port.

## Explicitly Rejected For Phase 1

> **Two of these were later accepted.** Route tables shipped in WEB-1 (as data,
> then as the `server` block in WEB-5) and static file serving shipped as
> `web.static`. The reasoning below is preserved because it is why they were
> built the way they were — outside the queue rather than replacing it, which
> is the shape the "future routing helpers" sentence anticipated. Middleware,
> sessions and templates remain rejected.

### Callback Handler Registration

APIs such as `webserver.on_request(handler)` are rejected. They introduce a
second event model, complicate function lifetime and error propagation, and do
not use gBASIC's existing watcher mechanism.

### Route Tables And Route Parameters

Registration APIs such as `server.get("/users/:id", handler)` are rejected.
Applications can inspect `req.method` and `req.path` directly. Future routing
helpers may consume and produce queue records without replacing the queue.

### Middleware

Middleware chains are rejected because ordering, continuation, and error
semantics add framework complexity. Ordinary functions may transform request
records in application code.

### Sessions, Cookies, And Templates

These are application or later helper concerns. Phase 1 exposes headers and
bodies without adding browser state or rendering behavior.

### Static File Serving

Rejected as a built-in Phase 1 feature. Applications can use existing file
operations for small text responses, but secure and efficient static serving
needs a separate design.

### Multipart Uploads

Rejected until binary values, upload limits, temporary-file ownership, and
streaming are designed.

### WebSockets And TLS

WebSockets are a different long-lived protocol model. TLS requires certificate
and private-key configuration plus security guidance. Neither belongs in the
initial plain HTTP server.

### Async Application Code

Rejected for Phase 1. Network polling may be nonblocking internally, but
gBASIC watcher bodies execute synchronously on one evaluator thread.

## Implementation Phases

### Phase 1: Queue-Based HTTP

- optional HTTP server library or small, well-bounded HTTP/1.1 integration
- `load webserver`
- immediate loopback listen
- live server record
- ordinary request and response arrays
- evaluator-safe event pump
- request IDs and response matching
- GET and ordinary request methods
- lowercase, last-wins request headers
- string request and response bodies
- additive JSON request decoding
- 8 MiB request-body limit
- 30-second response timeout and automatic 504
- default response status and headers
- explicit close and interpreter cleanup
- HTTP/1.1 connection handling sufficient for deterministic tests

### Phase 2: Listen And Resource Controls

- listen options record
- explicit bind address
- configurable response timeout
- configurable request-body and header limits
- connection and keep-alive limits
- graceful shutdown timeout
- structured server statistics
- documented overload behavior

### Phase 3: Queue-Based Routing Helpers

- path and method filtering helpers
- query-string parsing helper
- route matching that returns records
- route parameters represented as records

These helpers must operate on request records and preserve the underlying
request/response queues. They must not require callback registration.

### Phase 4: HTTP Features

- cookie parsing and `Set-Cookie` response emission
- form URL encoding
- multipart processing after binary/file-upload semantics exist
- static-file helper with traversal protection and range handling
- compression policy
- streaming bodies

### Phase 5: Secure And Long-Lived Protocols

- TLS listen options
- certificate reload behavior
- Server-Sent Events
- WebSockets, likely as a separate module or queue type

## Test Plan

Tests should use only loopback networking and must not depend on public
services.

To avoid port conflicts, tests should call `webserver.listen(0)`. Port `0`
should be accepted only as a request for an operating-system-assigned test or
ephemeral port, and the actual port should be exposed as `server.port`.

A native or Python test client should connect to that port and verify:

- successful listen and close
- GET request record fields
- POST method, path, raw query, headers, and body
- lowercase request headers
- duplicate-header last-wins behavior
- valid JSON adding `request.json`
- invalid JSON preserving body and omitting `json`
- JSON `null` mapping to `nothing`
- default status 200, empty headers, and empty body
- custom status, headers, and body
- response matching by ID
- out-of-order responses
- HTTP 404 produced by application logic
- automatic 504 after the response timeout
- oversized body rejection
- continued service after a malformed request
- shutdown from ordinary code and from a watcher
- interpreter exit after the final server closes

Negative tests should verify runtime errors for:

- using the module before `load webserver`
- missing, extra, nonnumeric, fractional, negative, and oversized port values
- bind failure
- closing a non-server value
- response without an ID
- unknown, duplicate, or expired response ID
- invalid status type or range
- non-record headers
- invalid header names or non-string values
- non-string response body
- mutation that replaces `server.requests` or `server.responses` with a
  non-array value

The integration runner should:

- build with and without the optional server dependency
- start no persistent external daemon
- impose hard process timeouts
- always close clients and listeners during cleanup
- skip clearly when the optional dependency is unavailable
- run concurrent client connections to verify out-of-order ID matching even
  though application execution remains single-threaded

Memory and sanitizer testing should cover disconnects, malformed requests,
timeouts, shutdown with pending requests, repeated listen/close cycles, and
client connection churn.

## Open Questions

- Which C library best supports a small nonblocking HTTP event pump without
  imposing callback semantics on gBASIC application code?
- ~~Should loopback-only remain the default when a future options-record form
  is added, with public binding always explicit?~~ **Answered yes, 2026-08-21
  (PLAT-WEB-1 Gap A).** `address` is the one option the record accepts; an
  unknown field is refused by name rather than ignored, and a hostname is
  refused rather than resolved.
- Is 30 seconds the right fixed Phase 1 response timeout?
- Is 8 MiB the right fixed Phase 1 request-body limit?
- Should unsupported binary request bodies receive 400 or 415?
- Should duplicate request headers remain last-wins for compatibility with
  WebClient Phase 1, or move both modules to duplicate-preserving arrays later?
- Should a future phase also expose the raw query string alongside the parsed
  `query` record?
- Should malformed HTTP be answered directly by native code without entering
  `server.requests`? The recommendation is yes.
- Should `server.requests` and `server.responses` be protected from wholesale
  reassignment while still permitting ordinary array mutation?
- Should shutdown reject new connections immediately but allow a short grace
  period for responses already queued?
- What stable error-code range should be reserved for Webserver failures?

## Recommended Summary

- Bind immediately and return a live server record.
- Expose ordinary mutable request and response arrays.
- Trigger ordinary watchers when native code appends incoming requests.
- Match exactly one response to each request by ID.
- Keep all gBASIC application execution single-threaded.
- Generate a 504 after a fixed 30-second response wait.
- Keep request and response bodies string-only.
- Add request JSON only when strict parsing succeeds.
- Default response status to 200 and headers to an empty record.
- Normalize request header names to lowercase and use last-wins duplicates.
- Shut down explicitly with `webserver.close(server)`.
- Use loopback port `0` in tests and expose the actual bound port.
- Keep callbacks, routing frameworks, middleware, sessions, cookies, templates,
  static files, multipart, WebSockets, TLS, and async application code out of
  Phase 1.

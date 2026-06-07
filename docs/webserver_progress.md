# WebServer Progress

Last verified: 2026-06-07

## Status

WebServer Phase 1 is implemented and complete.

Implemented compiled standard module:

```basic
load webserver
```

Implemented calls:

- `webserver.listen(port)`
- `webserver.close(server)`

The implementation uses a minimal POSIX socket listener and adds no external
HTTP dependency.

## Implemented Architecture

`webserver.listen(port)` binds immediately to `127.0.0.1` and returns a live
server record:

```basic
{
    port:8080,
    running:true,
    requests:[],
    responses:[]
}
```

Port `0` requests an operating-system-assigned ephemeral port. The actual port
is returned in `server.port`.

Incoming requests are appended to `server.requests` on the evaluator thread.
This triggers normal gBASIC watcher dispatch. Application code consumes request
records with `take_first(server.requests)` and appends response records to
`server.responses`.

Nested array mutators now operate on assignable record and array paths, so
`append(server.responses, value)` and `take_first(server.requests)` mutate the
live queues and trigger path watchers.

No callback or route-registration API was added.

## Request Records

Each request contains:

- `id`: positive server-generated request ID
- `method`: uppercase HTTP method
- `path`: request path
- `query`: record of percent-decoded string parameters
- `headers`: record with lowercase header names
- `body`: string
- `json`: present only when strict JSON parsing succeeds
- `remote_ip`: peer IP string
- `remote_port`: peer port number
- `timestamp`: UTC ISO 8601 string

Duplicate query parameters and duplicate request headers use last-wins
behavior. Invalid JSON preserves `body`, omits `json`, and does not raise an
error. JSON `null` maps to `nothing`.

## Response Records

Application responses support:

- required positive integer `id`
- optional integer `status`, default `200`
- optional record `headers`, default `{}`
- optional string `body`, default `""`

Response header names must be valid HTTP token strings. Header values and
response bodies must be strings. Responses are matched to pending clients by
request ID and consumed from `server.responses` after transmission.

The server supplies `Content-Length`, `Connection: close`, and a default
`Content-Type: text/plain` when the application omits a content type.

## Timeout Behavior

The default response timeout is 30 seconds, measured from the point at which
the request is appended to `server.requests`.

If no matching response arrives, the client receives:

```text
HTTP 504 Gateway Timeout
Gateway Timeout
```

The server continues processing later requests. Tests use the internal
`GBASIC_WEBSERVER_TIMEOUT` environment override to avoid a 30-second test delay;
this is not a language API.

## Shutdown Behavior

Both forms are supported:

```basic
webserver.close(server)
```

```basic
server.running = false
```

Queued responses are processed before shutdown where practical. Remaining
pending clients receive `503 Service Unavailable`. The listener and all client
sockets are closed during shutdown or interpreter cleanup. `webserver.close`
is safe to call from a request watcher.

## Files Changed

- `src/eval.c`
  - registers and evaluates the compiled `webserver` module
  - implements loopback listen, accept, request parsing, event pumping,
    response matching, timeout, and shutdown
  - constructs request and live server records
  - validates response records at queue append time
  - extends array mutators to nested lvalue paths with watcher triggering
- `tests/webserver_integration.bas`
  - queue/watch application used by the loopback integration test
- `tests/webserver_client.py`
  - deterministic local HTTP client
- `tests/webserver_integration.out`
  - expected client-visible results
- `tests/run_webserver.sh`
  - manages the local integration process and skips cleanly when unavailable
- `tests/negative_webserver_*.bas` and `.err`
  - cover required argument and response validation failures
- `tests/run_negative.sh`
  - registers WebServer negative tests
- `examples/nested_array_mutation_test.bas` and `.out`
  - verify live nested queue mutation and watcher triggering
- `tests/run_examples.sh`
  - registers the nested mutation regression
- `docs/webserver_design.md`
  - aligns request query and peer metadata with the implemented Phase 1 shape
- `docs/webserver_progress.md`
  - records implementation and verification status

## Tests Added

The loopback integration test verifies:

- starting a server on an ephemeral port
- GET requests entering `server.requests`
- watcher consumption and response queue append
- client receipt of response bodies
- lowercase request headers
- parsed query parameters
- peer address, peer port, and timestamp fields
- valid JSON adding `request.json`
- invalid JSON omitting `request.json`
- default response status, headers, and body
- response timeout producing 504
- explicit watcher-driven shutdown

Negative tests verify:

- invalid listen port
- response missing `id`
- non-string response body
- non-record response headers
- non-string response header value

## Verification Results

Verified on 2026-06-07:

- `make clean && make` - passed
- `./tests/run_examples.sh` - passed, including nested queue mutation
- `./tests/run_negative.sh` - passed, including WebServer validation cases
- `./tests/run_webserver.sh` - passed against the loopback integration fixture

In this sandbox, WebServer tests that create listener sockets require elevated
loopback permission. The runner itself binds only to `127.0.0.1` and skips
cleanly if it cannot start.

## Known Limitations

- loopback-only binding
- POSIX socket implementation
- single evaluator thread
- HTTP/1.1 request parsing only
- one request per connection; responses close the connection
- request bodies require `Content-Length`
- no chunked request bodies
- fixed 8 MiB request-body limit and 64 KiB header limit
- response bodies are strings only
- duplicate request headers and query parameters are last-wins
- path remains percent-encoded
- server records are expected to remain in top-level variables
- wholesale reassignment of `server.requests` or `server.responses` is not
  protected
- no routing, middleware, static files, templates, cookies, sessions,
  authentication, multipart, streaming, WebSockets, TLS, or asynchronous
  application code

## Next Recommended Phase

Phase 2 should add an options-record listen form while preserving the
queue/watch foundation:

- explicit bind address
- configurable response timeout
- configurable request and header limits
- connection and overload limits
- graceful shutdown timeout
- server statistics

Routing helpers should remain a later layer over request and response queues,
not callback registration.

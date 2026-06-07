# Webclient Standard Module Design

## Status

Design and development plan only. No webclient runtime implementation exists
yet.

## Goals

- Provide synchronous outgoing HTTP and HTTPS requests with a small BASIC-style
  API.
- Use ordinary gBASIC strings, records, arrays, numbers, booleans, and
  `nothing`.
- Treat HTTP responses as data, including 4xx and 5xx responses.
- Use the existing runtime error model for transport, TLS, validation, and
  system failures.
- Verify TLS certificates securely by default.
- Support common API clients without introducing browser behavior, HTML
  rendering, a DOM, JavaScript, or persistent page state.
- Keep Phase 1 bounded and leave streaming, binary data, cookies, and
  asynchronous operation for later phases.

## Recommended Module Shape

The compiled standard module should be explicitly loaded:

```basic
load webclient
```

Its public API should remain qualified:

```basic
response = webclient.get("https://example.com/status")
```

Unqualified aliases such as `http_get(...)` should not be added. The module
namespace is clear, avoids collisions, and follows the established `pg.*`
standard-module design.

## Recommended Phase 1 API

```basic
response = webclient.get(url)
response = webclient.get(url, headers)

response = webclient.post(url, body)
response = webclient.post(url, body, headers)

response = webclient.request(request_record)
```

All Phase 1 operations are synchronous.

### `webclient.get`

Accepted forms:

```basic
response = webclient.get(url)
response = webclient.get(url, headers)
```

- `url` must be a non-empty string containing an HTTP or HTTPS URL.
- `headers`, when present, must be a record.
- Header names and values must be strings.
- Header names are matched case-insensitively.
- Invalid arguments raise a runtime error before a request is attempted.

Optional headers are recommended. Requiring callers to construct a full request
record for a bearer token or an `Accept` header would make the common helper
unnecessarily awkward.

Example:

```basic
response = webclient.get(
    "https://api.example.com/users",
    {
        "accept":"application/json",
        "authorization":"Bearer " + token
    }
)
```

### `webclient.post`

Accepted forms:

```basic
response = webclient.post(url, body)
response = webclient.post(url, body, headers)
```

The body rules should be:

- A string is sent unchanged.
- A record or array is encoded as JSON.
- JSON encoding maps `nothing` to JSON `null`.
- JSON encoding accepts strings, numbers, booleans, `nothing`, arrays, and
  records recursively.
- `unknown`, file values, directory values, native handles, date/time values,
  money, and other unsupported values raise a runtime error.
- When a record or array is encoded automatically, the module adds
  `Content-Type: application/json` unless the caller supplied a content type.
- Automatic JSON encoding must not overwrite an explicit caller header.

The module should encode records and arrays automatically. This keeps ordinary
API calls concise and preserves the language's record/array emphasis.

```basic
response = webclient.post(
    "https://api.example.com/users",
    {
        name:"Ada",
        active:true
    }
)
```

This JSON encoder must be distinct from gBASIC source serialization where
necessary. In particular, wire JSON requires `null`, not the gBASIC token
`nothing`. Callers may still pass a pre-encoded string when exact control over
the request bytes is required.

String bodies should not receive an automatic content type. The caller may
provide `text/plain`, JSON, XML, or another appropriate type.

### `webclient.request`

The general request form should accept one record:

```basic
response = webclient.request({
    method:"POST",
    url:"https://api.example.com/jobs",
    headers:{
        "accept":"application/json"
    },
    body:{name:"nightly"},
    timeout:30
})
```

Recommended Phase 1 fields:

| Field | Required | Type | Behavior |
| --- | --- | --- | --- |
| `url` | yes | string | HTTP or HTTPS URL |
| `method` | no | string | Defaults to `"GET"` |
| `headers` | no | record | String header names and values |
| `body` | no | string, record, array, or `nothing` | Same encoding rules as `post` |
| `timeout` | no | positive number | Total request timeout in seconds |

Unknown request fields should be rejected. This catches misspellings instead of
silently ignoring configuration.

The method should be normalized to uppercase and validated as an HTTP token.
Phase 1 should permit methods beyond GET and POST through `request`, including
PUT, PATCH, DELETE, HEAD, and OPTIONS. Dedicated convenience helpers can be
added later without changing the general API.

`nothing` or an omitted body means no request body. A body is not prohibited
solely because of the selected method; the caller is responsible for choosing
semantically appropriate HTTP.

The single `timeout` value is a total transfer deadline. More detailed connect,
read-idle, or low-speed timeout controls belong in a later phase.

## Response Representation

Every completed HTTP exchange should return a record:

```basic
{
    status:200,
    reason:"OK",
    headers:{
        "content-type":"application/json"
    },
    body:"{\"name\":\"Ada\"}",
    json:{
        name:"Ada"
    }
}
```

Required fields:

- `status`: numeric HTTP status code.
- `reason`: response reason text, or `""` when unavailable.
- `headers`: record with lowercase header names.
- `body`: decoded response body as a string.

Conditional field:

- `json`: present only when a non-empty response body parses successfully as
  JSON.

The response record should not contain transport handles or libcurl objects.
All native request resources must be released before the record is returned.

### Status And Reason

HTTP status codes are response data:

- 2xx, 3xx, 4xx, and 5xx responses return normally.
- A 404 or 500 must not raise a runtime error.
- `status` is always the final response's numeric status after redirects.

Reason phrases are absent in HTTP/2 and HTTP/3 and are not semantically
reliable. `reason` should contain the phrase when the protocol provides one and
otherwise be an empty string. The field remains useful for HTTP/1.x without
inventing phrases from status-code tables.

### Headers

Response header names should be converted to lowercase ASCII.

For repeated header names:

- A single occurrence is stored as a string.
- Multiple occurrences are stored as an array of strings in arrival order.

Example:

```basic
response.headers["content-type"]   # string
response.headers["set-cookie"]     # array when repeated
```

Arrays preserve information that would be lost with last-wins behavior.
Blindly joining duplicate values is incorrect for headers such as
`Set-Cookie`. The string-or-array result does introduce a small type
distinction, but only for genuinely repeated headers and without changing the
simple common case.

Only headers from the final response should appear in the top-level response
record. Redirect-chain headers can be exposed by a future diagnostic API if
needed.

Trailer fields are deferred.

### Automatic JSON Decoding

Phase 1 should attempt JSON decoding for every non-empty textual response body,
regardless of `Content-Type`.

If decoding succeeds:

- add `response.json`
- map JSON objects to records
- map JSON arrays to arrays
- map JSON strings, numbers, and booleans to their gBASIC equivalents
- map JSON `null` to `nothing`

If decoding fails:

- leave `response.body` unchanged
- omit `response.json`
- do not raise an error

This handles servers with missing or incorrect content types while keeping
ordinary text responses unchanged. Applications that require JSON can check
for the field:

```basic
if is_unknown(response["json"]) then
    print("response was not JSON")
end if
```

JSON parse failure is not a transport failure and should not populate the
global error state.

### Binary Responses

Phase 1 is text-only because gBASIC currently has no binary or byte-array value.

The implementation should collect response bytes without treating them as C
strings during transfer. Before constructing `body`, it should reject content
containing an embedded NUL byte with a runtime error explaining that binary
responses are unsupported.

The module should not reject solely by `Content-Type`, because many useful text
responses use generic or incorrect media types. Non-UTF-8 bytes without NUL
cannot be fully validated by the current string model and may pass through as a
string in Phase 1. A native binary value should resolve this in a future phase.

To prevent accidental unbounded allocation, Phase 1 should enforce a documented
maximum in-memory response size. A recommended initial limit is 32 MiB.
Exceeding it is a runtime error. Streaming downloads and configurable limits
belong in a later phase.

## Defaults

### Timeout

The default total timeout should be 30 seconds.

This is long enough for ordinary API calls but prevents a request from hanging
indefinitely. A `request.timeout` value overrides it. The value should be a
positive finite number of seconds; fractional seconds may be accepted if
libcurl supports the required millisecond conversion safely.

An implementation may apply a shorter internal connection timeout, recommended
as at most 10 seconds, but the public Phase 1 contract is the total timeout.

### Redirects

Redirects should be followed automatically with a maximum of 10 redirects.

Recommended behavior:

- 307 and 308 preserve the method and body.
- 303 changes the follow-up request to GET.
- 301 and 302 use libcurl's conventional HTTP behavior unless a later option
  exposes stricter method preservation.
- Authentication headers must not be forwarded automatically to a different
  host.
- Redirects to unsupported schemes must fail.

The final response is returned. Redirect history is deferred.

### Compression

The module should ask libcurl to negotiate supported content encodings and
return the decompressed body. Response headers should remain the server's
headers; the module should not pretend decompression was performed by the
application.

### User Agent

Phase 1 should send a small default user agent such as:

```text
gBASIC/0.1 webclient
```

A caller-provided `User-Agent` header overrides it.

## Error Handling

The module should use the existing runtime error mechanism:

```basic
on error resume next
response = webclient.get("https://unavailable.example")

if error then
    print(error.message)
    print(error.source)
    error.clear()
end if
```

Recommended error behavior:

- `error.source` is `"webclient"`.
- The module receives a stable gBASIC error-code range separate from core file
  and PostgreSQL errors.
- Messages include useful libcurl diagnostics without exposing request bodies,
  authorization headers, cookies, client secrets, or full credential-bearing
  URLs.

Runtime errors include:

- DNS resolution failure
- connection refusal or reset
- TLS negotiation or certificate failure
- malformed or unsupported URL
- timeout
- redirect loop or redirect limit
- invalid argument or request-record shape
- invalid header names or values
- unsupported request body value
- unsupported binary response
- response-size limit exceeded
- memory or other system failure

HTTP status codes, including 401, 404, 429, and 500, are not runtime errors.

TLS peer and hostname verification must be enabled by default. Phase 1 should
not expose an option to disable certificate verification.

## Underlying C Library

libcurl is the recommended implementation library.

Reasons:

- mature HTTP and HTTPS support
- DNS, TLS, redirects, proxies, compression, and protocol negotiation
- portable C API
- clear timeout and callback mechanisms
- broad platform packaging
- future support for streaming and asynchronous multi-request operation

The build should detect libcurl through `pkg-config`, following the optional
dependency pattern used for libpq and GTK. gBASIC should continue to build when
libcurl development files are unavailable. In that build, `load webclient`
should raise a clear runtime error stating that webclient support is
unavailable.

Phase 1 should use libcurl's easy interface. The multi interface should be
deferred until asynchronous or concurrent requests are designed.

Each request should use a fresh easy handle in Phase 1 unless implementation
testing demonstrates that a private reusable handle can be safely reset
without leaking headers, credentials, cookies, or callbacks between calls.
Correct isolation is more important than premature connection-reuse
optimization. A future opaque client/session value can provide deliberate
reuse.

The module must use write and header callbacks that:

- grow buffers with overflow checks
- enforce the response-size limit during transfer
- preserve response bytes until text validation
- distinguish final response headers from redirect/intermediate headers
- release all libcurl lists, handles, buffers, and error strings on every exit
  path

## Rejected Alternatives

### Shelling Out To `curl`

Rejected because it introduces quoting and injection hazards, depends on an
external executable, complicates body and header handling, and cannot provide
reliable structured errors or resource ownership.

### HTTP Status As A Runtime Error

Rejected because 4xx and 5xx statuses are valid completed HTTP responses.
Applications need the status, headers, and body to make policy decisions.

### Response As An Opaque Handle

Rejected for Phase 1 because the complete response is already buffered and maps
naturally to a record. Opaque handles become appropriate for future streaming
responses.

### Always Returning A `json` Field

Rejected because `json:nothing` cannot distinguish valid JSON `null` from a
non-JSON response. Omitting the field gives the distinction without adding a
second flag.

### Requiring Explicit `encode()` For Records

Rejected because it makes common JSON APIs cumbersome and gBASIC serialization
is not necessarily identical to wire JSON. The webclient module needs explicit
JSON semantics, especially `nothing -> null`.

### Last-Wins Duplicate Headers

Rejected because it silently loses values, especially repeated `Set-Cookie`,
`Warning`, and similar headers.

### Joining Every Duplicate Header

Rejected because not every HTTP header can be safely comma-joined.

### Browser-Like Global State

Rejected for Phase 1. Automatic cookie jars, page history, cache behavior,
credential prompts, HTML parsing, and script execution would make the module
less predictable and move it toward a browser.

### Disabling TLS Verification For Convenience

Rejected for Phase 1 because an insecure default or easy bypass would make
application mistakes likely. Custom certificate trust belongs in a carefully
designed later phase.

### Custom HTTP Implementation

Rejected because correctly implementing HTTP versions, TLS, redirects,
compression, proxying, and platform networking would be a large security and
maintenance burden.

## Implementation Phases

### Phase 1: Core Synchronous Text Requests

- optional libcurl build detection
- `load webclient`
- `webclient.get`
- `webclient.post`
- `webclient.request`
- HTTP and HTTPS
- request headers
- string and automatic JSON request bodies
- 30-second default timeout and total-timeout override
- automatic redirects with a limit of 10
- decompression
- response records
- lowercase and duplicate-preserving response headers
- automatic optional JSON response field
- 32 MiB response limit
- runtime error integration
- strict TLS verification

### Phase 2: Request Controls And Additional Helpers

- `webclient.put`
- `webclient.patch`
- `webclient.delete`
- separate connect, total, and low-speed timeout controls
- configurable response-size limits
- redirect-policy controls
- explicit accepted encodings
- proxy configuration
- basic and bearer authentication helpers

The general `request` API already permits these HTTP methods; the helpers are
for readability and defaults.

### Phase 3: Encoded Forms And Uploads

- `application/x-www-form-urlencoded`
- multipart form data
- file upload
- explicit content-type helpers
- upload-size and timeout behavior

### Phase 4: Sessions, Cookies, And Certificate Options

- opaque client/session value
- connection reuse
- cookie jar and cookie persistence policy
- scoped default headers
- proxy credentials
- custom CA bundle or trust store
- optional client certificates

Certificate verification should remain enabled. Any exceptional insecure mode,
if ever added, must be explicit, conspicuous, and unsuitable as a default.

### Phase 5: Streaming And Binary Values

- native binary/byte value
- binary request and response bodies
- streaming downloads to file values
- streaming uploads
- progress callbacks or events
- configurable memory limits
- response trailers

### Phase 6: Concurrency

- asynchronous requests based on libcurl's multi interface
- cancellation
- bounded concurrency
- integration with a future gBASIC event-loop model

This phase should not be designed as callbacks alone until the language has
clear lifetime and error-propagation semantics for asynchronous work.

### Separate Or Later Protocol Modules

WebSocket and Server-Sent Events are long-lived message streams rather than
ordinary request/response calls.

- WebSocket should likely be a separate `websocket` module.
- SSE may be a streaming feature of a future webclient phase or a separate
  `sse` module.

Neither belongs in the synchronous Phase 1 response-record API.

## Test Plan

Tests should use a deterministic local HTTP server rather than public internet
services.

The test server should bind only to loopback, choose or receive a controlled
port, and expose fixed endpoints for:

- GET success
- POST string echo
- POST automatic JSON encoding
- request-record method, headers, and body
- lowercase response headers
- duplicate response headers
- valid JSON response
- invalid JSON response
- empty response body
- 204 response
- 404 and 500 responses returned normally
- redirect success
- redirect loop or limit
- delayed response timeout
- compressed response
- oversized response rejection
- embedded-NUL binary rejection

Positive tests should verify:

- exact response record fields
- numeric status
- reason text or documented empty fallback
- body preservation
- automatic JSON decoding and `null -> nothing`
- omission of `json` after parse failure
- caller headers reaching the server
- explicit content type overriding automatic JSON content type
- duplicate headers represented as arrays
- custom method behavior
- redirects returning the final response

Negative tests should verify:

- module use before `load webclient`
- missing and extra arguments
- non-string or empty URL
- unsupported URL scheme
- malformed URL
- non-record headers
- non-string header name/value behavior
- invalid method
- missing request URL
- unknown request-record field
- invalid timeout type or range
- unsupported body type
- DNS failure
- connection failure
- TLS verification failure where a deterministic local TLS fixture is
  available
- timeout
- redirect limit
- oversized response
- binary response

The normal example and negative runners should include tests that do not depend
on external networking. A dedicated `tests/run_webclient.sh` should manage the
local fixture server and integration cases. The fixture should have no access
to credentials and should not require the public internet.

Build verification should cover:

- libcurl available
- libcurl unavailable, with the interpreter still building
- leak and invalid-memory checks for success and failure paths
- repeated requests to detect stale headers, bodies, callbacks, or credentials

## Open Questions

- Is 32 MiB the appropriate initial hard response limit, or should Phase 1 use
  another fixed value?
- Should fractional timeout seconds be accepted, or should the initial contract
  require positive whole seconds?
- Should a valid JSON scalar response such as `true`, `42`, or `"text"` create
  `response.json`, or should the field initially be limited to records and
  arrays? The recommendation is to accept every valid JSON value.
- Should `HEAD` responses expose a guaranteed empty body even if a noncompliant
  server sends bytes?
- Should informational 1xx responses be discarded entirely or retained in a
  future response-history field?
- Should the default user agent include the exact interpreter version?
- Which stable numeric error-code range should be reserved for webclient
  failures?
- Should request header values support arrays in a later phase for deliberate
  repeated request headers?
- Should non-UTF-8 text pass through unchanged until a binary value exists, or
  should Phase 1 validate UTF-8 and reject invalid text?

## Recommended Decisions Summary

- Add optional headers to `get` and `post`.
- Automatically encode record and array request bodies as JSON.
- Support `method`, `url`, `headers`, `body`, and total `timeout` in
  `request_record`.
- Default to a 30-second total timeout.
- Follow redirects automatically, up to 10.
- Represent duplicate response headers as arrays; singular headers remain
  strings.
- Attempt JSON decoding automatically for every non-empty response body.
- Omit `response.json` when decoding fails.
- Keep Phase 1 text-only and reject embedded-NUL binary responses.
- Use libcurl's easy interface with strict TLS verification.

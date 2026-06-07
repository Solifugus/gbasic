# WebClient Progress

Last verified: 2026-06-07

## Status

WebClient Phase 1 is complete.

Implemented compiled standard module:

```basic
load webclient
```

Implemented calls:

- `webclient.get(url)`
- `webclient.post(url, body)`
- `webclient.request(request_record)`

The implementation uses libcurl when available. gBASIC still builds without
libcurl; attempting `load webclient` in that build raises a clear runtime
error.

## Implemented Behavior

### Requests

- HTTP and HTTPS URLs are supported.
- URL values must be strings using `http://` or `https://`.
- `get` accepts exactly one URL argument.
- `post` accepts a URL and string body.
- `request` accepts a record with:
  - required `url`
  - optional string `method`, defaulting to `GET`
  - optional record `headers`
  - optional string `body`
  - optional positive numeric `timeout`
- Unknown request-record fields are rejected.
- Header names must be valid HTTP token strings.
- Header values must be strings and cannot contain carriage returns or
  newlines.
- Records and arrays are not automatically encoded as request bodies.
  Applications must explicitly call `encode()` and pass the resulting string
  when desired.
- The default total timeout is 30 seconds.
- Connection timeout is capped at 10 seconds and never exceeds the total
  timeout.
- Redirects are followed using libcurl behavior, with HTTP/HTTPS-only redirect
  protocols and a maximum of 10 redirects.
- TLS certificate and hostname verification use libcurl's secure defaults.
- Supported response compression is negotiated through libcurl.
- Each request uses a fresh libcurl easy handle.

### Responses

Completed HTTP exchanges return:

```basic
{
    status:200,
    reason:"OK",
    headers:{
        "content-type":"application/json"
    },
    body:"...",
    json:{...}
}
```

- `status` is the numeric status from the final response.
- `reason` is the received reason phrase or an empty string.
- `headers` is a record with lowercase names.
- Duplicate response headers use last-wins behavior in Phase 1.
- `body` is always a string.
- `json` is added only when a non-empty body parses as strict JSON.
- JSON parse failure leaves `body` intact, omits `json`, and does not raise an
  error.
- JSON `null` maps to `nothing`.
- HTTP error statuses such as 404 and 500 return normal response records.

Network, DNS, connection, TLS, malformed-URL, timeout, redirect-limit, and
validation failures raise runtime errors with source `"webclient"` and code
`3001`.

Responses are buffered in memory with a 32 MiB limit. Embedded-NUL binary
responses are rejected because gBASIC does not yet have a binary value.

## Files Changed

- `Makefile`
  - detects libcurl through `pkg-config`
  - defines `HAVE_LIBCURL`
  - adds libcurl compiler and linker flags when available
- `src/eval.c`
  - registers the compiled `webclient` module
  - initializes and cleans up libcurl
  - validates request arguments and records
  - performs synchronous requests with libcurl
  - collects final response status, reason, headers, and body
  - performs additive strict JSON response decoding
  - maps transport and validation failures to runtime errors
- `tests/webclient_fixture.py`
  - provides a deterministic loopback HTTP fixture
- `tests/webclient_integration.bas`
  - covers Phase 1 positive behavior
- `tests/webclient_integration.out`
  - records expected integration output
- `tests/run_webclient.sh`
  - starts the fixture and runs the integration test
  - skips cleanly when disabled, Python is unavailable, or loopback networking
    cannot be started
- `tests/negative_webclient_*.bas` and matching `.err` files
  - cover required argument and malformed-URL failures
- `tests/run_negative.sh`
  - registers the WebClient negative tests
- `docs/webclient_design.md`
  - contains the original broader design and future-phase plan
- `docs/webclient_progress.md`
  - records the implemented Phase 1 contract and verification

## Tests Added

The live loopback integration test verifies:

- GET status, reason, body, and headers
- lowercase response header names
- last-wins duplicate response headers
- POST with a string body
- `request(record)` with method, URL, headers, body, and timeout
- request header delivery
- automatic additive JSON response decoding
- JSON `null` mapping to `nothing`
- invalid JSON omitting the `json` field without an error
- HTTP 404 returning a response record
- redirect following and final-response data

Negative tests verify:

- `get` with a non-string URL
- `post` with a non-string URL
- `post` with a non-string body
- `request` with a non-record argument
- `request` missing `url`
- `request` with a non-string method
- `request` with non-record headers
- `request` with a non-string header value
- `request` with a non-string body
- malformed URL

## Verification Results

Verified on 2026-06-07:

- `make clean && make` - passed with libcurl 8.14.1
- `./tests/run_examples.sh` - passed
- `./tests/run_negative.sh` - passed, including all WebClient negative cases
- `./tests/run_webclient.sh` - passed against the loopback fixture
- `make clean && make LIBCURL_AVAILABLE=0` - passed
- loading `webclient` in a no-libcurl build - produced the expected clear
  runtime error

## Known Limitations

- synchronous requests only
- no optional headers on the `get` or `post` helpers; use `request`
- string request and response bodies only
- no automatic JSON request encoding
- no cookies or persistent session
- no authentication helpers
- no form or multipart encoding
- no file upload
- no streaming
- no binary value support
- no proxy-specific API, though libcurl environment behavior may apply
- no custom certificate or client-certificate options
- no asynchronous requests or cancellation
- duplicate response headers are last-wins
- response and request history are not exposed
- no WebSocket, SSE, or webserver functionality

## Next Recommended Phase

Phase 2 should add request controls and convenience helpers without changing
the Phase 1 response shape:

- `webclient.put`
- `webclient.patch`
- `webclient.delete`
- helper-level optional headers if desired
- separate connection and total timeout controls
- configurable response-size limits
- redirect-policy controls
- proxy configuration
- basic and bearer authentication helpers

Multipart upload, cookies/sessions, streaming, binary values, and asynchronous
operation should remain separate later phases.

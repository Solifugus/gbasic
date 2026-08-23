# PLAT-WEB-0 — The Lowering Study

**Status:** complete, 2026-08-20. This is the deliverable §10 of
`plat-web-design-draft.md` calls for: both §2 examples hand-lowered onto the
web library that exists today, with the honest list of what could not be
expressed. A document, not code — nothing here ran, everything here was
checked against the real library surface (`docs/reference.md` §WebServer,
`examples/gbasic_site/site.bas`, and the module source).

**The verdict up front:** the lowering is clean where it matters. Routing,
handlers, host dispatch, proxy trust, drain, and the serve loop all lower
onto today's library with no contortions — which per §10 means **the shape
is right**. What cannot lower is a short, sharp list of six runtime
capabilities, and they map one-to-one onto the phases the draft already
planned. The study found no missing capability the phasing had not
predicted, and it found two it had *overestimated*.

---

## 1. The target: what the library actually is

Established by reading, not assumption:

- `webserver.listen(port)` → a **live server record** `{port, running,
  requests, responses}`. Binds **127.0.0.1 only** — loopback is not an
  option, it is the only behavior. Port 0 = ephemeral, real port readable
  back.
- Dispatch is a **watcher**: request arrival appends to `server.requests`;
  application code drains with `take_first` and appends `{id, status,
  headers, cookies, body}` records to `server.responses`. One response per
  request id; the server adds Content-Length, **closes the connection after
  each response**, and 504s a request unanswered for 30 s (hard-coded).
- `req` already carries `method`, `path`, `query` (decoded), `headers`
  (lowercase — including `host`), `cookies`, `body`, `json` when it parses,
  `remote_ip`/`remote_port`, `timestamp`.
- `webserver.close(server)` stops the listener; `webserver.redirect(req,
  location[, status])` builds a response record.
- **The runtime keeps the process alive while a listener runs** — the site
  example registers its watcher and simply ends; no keep-alive loop exists
  or is needed.
- No TLS, no static file serving, no path patterns, no host routing, no
  streaming, no bind-address option, no worker anything.

---

## 2. Lowering example A — the minimal server

The block:

```gbasic
server myapp( port: 8080 )
    root "public"
    get "/"( req )      ... end get
    post "/cart"( req ) ... end post
end server

program main( args )
    serve(myapp)
end program
```

The hand lowering, complete:

```gbasic
load webserver

' each handler block becomes one function; response by RETURN VALUE
function handle_get_root(req)
    return { id: req.id, status: 200,
             headers: { "content-type": "text/html" },
             body: "..." }
end function

function handle_post_cart(req)
    parsed = try_decode(req.body)
    if not parsed.ok then
        return { id: req.id, status: 400, body: "bad JSON: " + parsed.message }
    end if
    return { id: req.id, status: 201, body: encode({ ok: true }) }
end function

' `root "public"` -- APPROXIMATED, see gap B; this version is NOT safe
function serve_static(req)
    if contains(req.path, "..") then
        return { id: req.id, status: 403, body: "no" }
    end if
    f (file)= "public" + req.path
    if not exists(f) then
        return { id: req.id, status: 404, body: "not found" }
    end if
    return { id: req.id, status: 200, body: read(f) }
end function

' the block's generated dispatch
myapp = webserver.listen(8080)
watch(myapp.requests)
    while count(myapp.requests) > 0
        req = take_first(myapp.requests)
        consider req.method + " " + req.path
        if "GET /" then
            response = handle_get_root(req)
        if "POST /cart" then
            response = handle_post_cart(req)
        else
            response = serve_static(req)
        end consider
        append(myapp.responses, response)
    end while
end watch

program main( args )
    ' serve(myapp) lowers to: nothing. The listener keeps the process
    ' alive; registration was the whole job. (A serve() builtin still earns
    ' its place as the point where workers, drain and the handle attach.)
end program
```

**What this lowering established:**

- **Routing lowers to `consider` on `method + " " + path`** — clean, and
  pleasingly in-idiom. Exact-match routes cost one line each.
- **Response-by-return works with zero new machinery** — the handler
  returns the record, the dispatch appends it. This is strong evidence for
  resolving Open Question 1 in favor of return values: the `res`-record
  alternative would need dispatcher plumbing the lowering shows is
  unnecessary.
- **Path patterns (`{id}`) lower, verbosely**: split the path on `/`,
  compare static segments, bind the rest. Thirty lines of gBASIC per
  pattern route. Expressible — so patterns are *sugar*, not a runtime gap —
  but the verbosity is exactly the block's justification.
- **`root` does NOT lower safely.** The `..` substring check above is
  theater: percent-encoded traversal, symlink escapes, and platform path
  quirks need canonicalize-then-check (`realpath` semantics), which pure
  gBASIC cannot express — there is no path-canonicalization builtin. Also
  missing for real use: content-type-by-extension and non-slurping reads
  for large files. → **Gap B.**
- **`port: 8080` lowers; a reachable server does not.** `listen` binds
  loopback only. The block promises a server other machines can reach;
  today's library cannot produce one at any port. → **Gap A** (a
  bind-address option — the smallest gap on the list and the first thing to
  build).
- **Load-time checks do not lower and should not**: duplicate-route
  detection, pattern validation, unknown verbs — the lowering simply *is*
  whatever `consider` chain got written, wrong or not. This is the block's
  own value, computed from the AST, and no runtime work makes it happen.

---

## 3. Lowering example B — multi-host, TLS, stream, drain

The block (abridged from the draft §2):

```gbasic
server edge( port: 443, inherit: true, workers: 4 )
    trust_proxy "127.0.0.1"
    web store( host: "store.example.com", cert: "..." )  ... end web
    web api( host: "api.example.com", cert: "..." )      ... end web
    on drain ... end on
end server
```

What lowers cleanly:

- **Host routing**: `req.headers` already carries lowercase `host`, so the
  outer dispatch is one more `consider` level (strip an optional `:port`
  suffix first). Sites are just a two-level route table. **Lowers.**
- **`trust_proxy "127.0.0.1"`**: pure record rewriting — if
  `req.remote_ip` equals a trusted address, replace `remote_ip` from
  `x-forwarded-for` and note `x-forwarded-proto`. Ten lines of gBASIC.
  **Lowers, cleanly.** *(Correction, built 2026-08-22: the address taken is
  the RIGHTMOST hop not itself trusted — the "first hop" this paragraph
  originally named is the leftmost value, which the client controls: the
  spoof the trust check exists to prevent.)*
- **`on drain`, single-process**: `webserver.close(edge)` stops the
  listener, the queue drains through the existing watcher, the drain body
  runs after. **Lowers** for one process; the multi-worker version is part
  of Gap C, not a gap of its own.

What cannot lower, and why:

- **`workers: 4`** — the listener is created bound inside this process;
  there is no way to share it. Two possible primitives (draft §5): a
  socket handle value kind riding the existing spawn fd-transfer, or an
  `SO_REUSEPORT` option on `listen`. Either is runtime work. → **Gap C.**
- **`inherit: true`** — no `LISTEN_FDS`/inherited-fd path into
  `webserver.listen`. → **Gap F** (companion of C; same subsystem).
- **`cert:` / port 443 TLS** — the library has no TLS at all (verified:
  OpenSSL is linked for crypto builtins only). → **Gap D**, the subsystem
  the draft already scheduled as its own phase.
- **`stream "/events"`** — structurally impossible in the response model:
  one response record per request id, connection closed after it. Streaming
  needs a per-connection handle, an `emit` that writes without closing, and
  keep-alive. → **Gap E.**
- **Minor, found along the way:** ~~the 30-second response timeout is
  hard-coded~~ (a per-listener `timeout:` option since WEB-3, covering the
  idle-read slow-loris case too), and `listen` exposes no interface choice
  (folded into Gap A, closed).

---

## 4. The honest gap list

| Gap | What | Size | Phase (per the draft) |
|---|---|---|---|
| A | ~~Bind address on `listen` (loopback-only today)~~ **CLOSED 2026-08-21** — `webserver.listen(port, { address: "0.0.0.0" })`, default unchanged, `server.address` reported back from `getsockname`, IPv6 and dual-stack included, `tests/run_web_bind.sh` | small C | WEB-1 — done first, as planned |
| B | ~~Safe static serving: canonicalize-then-check, content types, non-slurping reads~~ **MOSTLY CLOSED 2026-08-21** — `real_path`/`file_type` builtins + `web.static` (canonicalize-then-check, content types by extension). ~~Non-slurping reads remain, and belong with E~~ — closed with E on 2026-08-22: `{ file: path }` responses stream in 64K chunks and `web.static` returns that shape. | small C | WEB-1, remainder WEB-4 — done |
| C | ~~Worker pool~~ **CLOSED 2026-08-22** — process workers; the supervisor holds (`hold: true`), workers adopt via `webserver.inherited()`, transfer is `process.start listen_fds:` speaking LISTEN_FDS. No socket value kind was needed: the live server record already names the listener, and the fd itself only ever moves at exec time. | the §5 decision | WEB-2 — done |
| D | ~~TLS/SNI~~ **CLOSED 2026-08-22** — `tls:` on listen/inherited, SNI by hostname, refused at listen time; rotation = rolling reload of the pool. | subsystem | WEB-3 — done |
| E | ~~Streaming: connection handle, `emit`, keep-alive~~ **CLOSED 2026-08-22** — the request `id` is the handle: `{ stream: true }` opens an EOF-framed connection, `webserver.emit` writes to it (`false` = client gone, reaped), `webserver.finish` closes it. No keep-alive was needed: close-after-response stays the rule, and a stream's close *is* its framing. `tests/run_web_stream.sh`. | new response mode | WEB-4 — done |
| F | ~~`LISTEN_FDS` / inherited socket~~ **CLOSED 2026-08-22** — the same protocol IS the worker transfer, so one mechanism serves systemd and the supervisor. | small, with C | WEB-2 — done |

Everything else in both examples — routing, patterns, handlers,
return-value responses, host dispatch, proxy trust, redirect, drain
(single-process), the serve loop — is **sugar over what exists**.

Two things the draft *overestimated*, found by lowering:

- `serve()` nearly vanishes: the runtime already keeps the process alive
  while a listener runs. The builtin still earns its place as where
  workers, drain, and the handle attach — but the minimal case needs
  nothing.
- Single-process drain is already expressible; only the pooled version is
  real work.

---

## 5. What this study decides

Per §10's own test: **the shape is right — the block can ship as a front
end over code that already works**, once Gap A exists. The recommended
build order inside PLAT-WEB-1 follows directly:

1. ~~**Gap A** (bind address) — smallest change, unblocks every real use.~~
   **Done 2026-08-21.** `webserver.listen(port, { address: })`; the default
   stays `127.0.0.1` and `tests/run_web_bind.sh` holds it there by probing
   127.0.0.2, which a loopback-bound socket must not answer. Two things the
   build produced beyond the gap as written: `server.address` (read back from
   the socket, which is what makes the tests non-vacuous) and IPv6 —
   `getaddrinfo` covers both families for the same code, and doing v6 later
   would have meant touching the accept path twice.
2. ~~The **library-level route table**: `web.routes` / `web.dispatch` as
   plain data + a dispatcher function generated the way Section 2's
   lowering was written by hand. Everything testable with no socket.~~
   **Done 2026-08-21** — `stdlib/web.bas`, `docs/web_routing.md`,
   `tests/run_web_routes.sh`. It went further than the lowering in three
   places, each for a reason the lowering exposed: patterns are matched by
   **specificity rather than table order** (the hand lowering's `consider`
   chain is first-match-wins, which makes `/products/new` depend on where it
   was written); the table is **validated when built**, which is where the
   block's load-time errors have to live in a library; and a wrong verb on a
   real path answers **405 with `allow`** rather than 404, which the lowering
   had no way to express because a `consider` on `method + " " + path` cannot
   tell "no such page" from "not that verb".
3. ~~**Gap B** static serving, in C, with the canonicalization rule from the
   draft §6 pinned by hostile-path negatives from day one.~~ **Done
   2026-08-21.** The split turned out to matter: only the part gBASIC
   *cannot express* went into C, as two general filesystem primitives —
   `real_path` (canonical absolute path; `.`, `..` and symlinks resolved by
   the kernel) and `file_type` ("file"/"folder"/"other", the only non-raising
   way to ask whether a path is a directory). The policy — containment,
   403-versus-404, content types, no directory listing — is `web.static` in
   gBASIC, where the owner of a site can read and change it. The hostile-path
   cases were written first and the tier was proven red twice: once with the
   containment check removed, and once with it applied BEFORE canonicalization,
   which is the ordering mistake §6 warns about and which serves `/etc/passwd`
   through a symlink while looking correct.

And one design confirmation the study produced as a side effect: **Open
Question 1 should resolve to response-by-return.** The lowering needed no
`res` plumbing anywhere, including for redirects and errors; the one case
that genuinely wants incremental output is `stream`, which has `emit` and
is its own kind.

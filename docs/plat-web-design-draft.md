# PLAT-WEB — Declarative Server Block for gBASIC

**Status:** design draft, nothing committed. Written to be argued with.
Revised 2026-08-20 against the actual runtime; enterprise-readiness review
folded in the same day (§7b–§7d: the let-it-crash error model, stream
economics, observability, and the scale-out-now / LLVM-later performance
posture) — the first draft was written
without code access and several of its assumptions have been corrected below
(each marked **[verified]** or **[corrected]**).

**Premise:** gBASIC already has a web library. This design is only justified
if a syntax form is meaningfully clearer than calls into that library.
Section 10 gives the test for deciding that.

---

## 1. What the block is for

A `server` block declares a listener, the sites it carries, and the handlers
those sites expose — in one place, in source order, readable top to bottom.

The concrete wins over a call-based API:

- **Load-time errors.** Duplicate routes, malformed path patterns, unknown
  verbs, and colliding hosts become parse/load failures instead of runtime
  surprises.
- **A static route table.** `source_outline` can show routes (the outline
  walker needs the new statement kinds added — mechanical); a future LSP can
  complete them; the shape is introspectable without running anything.
- **Locality.** The path and the code that answers it sit together.

Everything else the block might do is achievable with a library and does not
justify grammar.

---

## 2. Syntax

### Minimal — one site

```gbasic
server myapp( port: 8080 )

    root "public"

    get "/"( req )
        ...
    end get

    post "/cart"( req )
        ...
    end post

end server
```

There is no mention of "site." You never meet the concept until you have two.

### Multiple hosts

```gbasic
server edge( port: 443, inherit: true, workers: 4 )

    trust_proxy "127.0.0.1"

    web store( host: "store.example.com", cert: "/etc/certs/store.pem" )
        root "public"
        get "/"( req )              ... end get
        get "/products/{id}"( req ) ... end get
        stream "/events"( req )     ... end stream
    end web

    web api( host: "api.example.com", cert: "/etc/certs/api.pem" )
        post "/orders"( req ) ... end post
    end web

    on drain
        ...
    end on

end server
```

`cert` sits with the `host` it belongs to; the SNI table is derived, not
authored. `port` belongs to the listener. Bare entries directly inside
`server` form an implicit default site — that is the one special rule, and it
buys the entire minimal case.

### Running it

```gbasic
program main( args )
    serve(edge)
end program
```

Declaration is inert; the **`serve` builtin** is what binds. **[corrected]**
The first draft wrote `serve edge` as a statement, which would have been a
second reserved word after claiming there was exactly one. As an ordinary
builtin call it costs zero grammar, and it dissolves the old open question
about blocking-versus-handle: a builtin can block by default and still return
a handle value the caller may ignore (the named-watcher pattern: the handle
is how a supervisor written in gBASIC would drain or stop it).

---

## 3. Grammar: exactly one new keyword

`server` is the only reserved word this adds — checked against the tree:
no stdlib or example uses `server` as an identifier today. **[verified]**

Everything else — `web`, `root`, `get`, `post`, `stream`, `trust_proxy` — is
an ordinary `IDENT` in one of three generic productions:

```
directive   : IDENT expr_list NEWLINE
handler     : IDENT STRING "(" params ")" NEWLINE stmt_list END IDENT NEWLINE
sub_block   : IDENT IDENT "(" record_fields ")" NEWLINE server_body END IDENT NEWLINE
```

plus one small fourth, because `on` already lexes as the keyword `ON` and
cannot ride the IDENT productions: **[corrected]**

```
hook        : ON IDENT NEWLINE stmt_list END ON NEWLINE
```

That reuses a word the language already reserves, extends the `on error`
family naturally, and gives `on reload` (Section 7) the same shape for free.

Verbs are validated **semantically** at load time against a table the web
subsystem owns. Consequences:

- Adding PUT, DELETE, PATCH, HEAD, OPTIONS, or a websocket form never touches
  `parser.y`.
- `get` does not become a reserved word, so existing programs using `get` as
  an identifier keep working.
- The productions could later serve non-web declarative blocks.

`( port: 443, ... )` is honestly **new grammar**, not a reuse: record
literals require braces today. But it is a small head-only production
(`SERVER IDENT LPAREN record_fields RPAREN`) following the head shape the
named-watcher work just established (`watch name(targets)`), and it is still
a record underneath — inspectable data, and keywords are already legal as
record-literal keys, so no option name can collide. Every production
terminates in NEWLINE, consistent with the rest of the grammar.

House rule that applies here as everywhere: **probe every dotted and named
form against the real parser before committing** — this tree has renamed
four APIs for the keyword-after-dot trap, and the `chart.new` case proved a
form can parse at the call site yet be undefinable at the declaration.

---

## 4. Semantics

- A `server` declaration binds a name in the enclosing scope to an inert
  server value. Nothing opens a socket, reads a cert, or touches the
  filesystem at declaration time.
- Handlers are callable **only** by dispatch. They are not visible as
  functions and do not pollute the outer scope.
- `req` is an ordinary record: `req.method`, `req.path` (the raw path,
  a string), `req.headers`, `req.query`, `req.body`, and **`req.params`**
  for `{name}` pattern captures. **[corrected]** The first draft put the
  captures on `req.path.<name>`, which cannot work — `req.path` cannot be
  both the raw string and a record. (`params` is not a keyword; checked.)
- Handler parameters are `( req )`, full stop. The alternative — binding
  pattern segments directly, `get "/products/{id}"( id )` — is more magical
  than BASIC-obvious, and one rule has to win. Captures live in
  `req.params.id`.
- A handler produces a response by returning a record, or by assigning
  `res.status` / `res.headers` / `res.body`. (Which of the two — see Open
  Questions. Note the runtime constraint that shaped the OLD library:
  functions cannot mutate caller state, so a `res` record implies the
  dispatcher passes it in and reads it back — possible, but the return-value
  form has no such machinery.)
- `stream` handlers are a different kind, not a verb: the body runs for the
  life of the connection and emits with **`emit`**. **[corrected]** The
  first draft used `send`, which is the actor-messaging builtin; one verb
  meaning "actor message" or "HTTP chunk" by argument kind is exactly the
  double-meaning Section 12 rejects for `print`. Websockets, if added later,
  are the same kind.
  **Stream economics, stated plainly:** in the worker-pool model every live
  stream PINS a whole worker for the life of its connection. Eight SSE
  clients on `workers: 4` is an outage. This is fine for a handful of admin
  streams and structurally wrong for hundreds; the design accepts the former
  and names an event-driven stream subsystem as the known later answer if
  the latter is ever wanted. Budget workers accordingly, and say so in the
  user documentation rather than letting the first dashboard discover it.
- Route matching: exact segments beat pattern segments; longest static
  prefix wins; ties are a **load-time** error rather than a runtime coin
  flip.
- **Route paths are string LITERALS, by design** (2026-08-20, Matthew's
  question, answered deliberately): the grammar takes a STRING token, not an
  expression. Everything Section 1 promises — the load-time route table,
  duplicate detection, `web.routes`, LSP completion — exists because routes
  are static facts of the source; an expression route melts them all into
  "run it and see." Dynamic dispatch has two honest homes: a catch-all
  pattern (`get "/x/{rest}"`) with `consider` inside the handler, or the
  library layer beneath the sugar.
- Response headers use their REAL names: quoted record-literal keys shipped
  (`9e691bc`), so `headers: { "Content-Type": "image/svg+xml" }` is written
  as-is — no underscore-mapping rule needed.

---

## 5. Concurrency — the load-bearing decision

This is the part that determines whether the feature is real.

The interpreter is single-threaded and tree-walking. A handler can block on
file I/O, `process.run`, or an LLM call. One in-process event loop therefore
means one slow handler stalls every connection.

**[corrected — the first draft undersold the actor runtime.]** It claimed
actors cannot carry a listener because the spawn child closes fds ≥ 3. The
close loop in fact **exempts a transfer list**: handles passed as spawn
arguments survive into the child (`xfer.fds` in the spawn path), and
already-running actors receive fds at runtime over `SCM_RIGHTS` — that is
how actor-handle forwarding ships today (`spawn_handle_passing_test.bas`).
What is actually missing is smaller than the draft believed: a **socket
handle value kind** to wrap a listening fd, so it can ride the existing
transfer machinery.

**Proposal: a pre-fork worker pool.** A supervisor holds the listener; `workers: N`
workers each accept from it; the OS distributes connections. Three ways to
get the listener into workers:

| | actor pool + fd transfer | `process.start` + `fds:` | `SO_REUSEPORT` |
|---|---|---|---|
| Privilege | supervisor alone binds :443 | supervisor alone binds | every worker needs it |
| Drain | clean — supervisor owns the socket | clean | draining worker can reset pending connections |
| Runtime work | a socket value kind; spawn transfer **already exists** | `fds:` option on `process.start` (does not exist today) | none |
| Supervision | the actor model's own monitor/demonitor | PLAT-PROC poll/stop | external |

The actor-pool column is new in this revision and is the architecturally
consistent choice: the language already owns spawn, transfer, monitoring and
death notification for exactly this shape. `SO_REUSEPORT` remains the
acceptable v1 on high ports and behind a proxy. systemd socket activation is
fd passing where systemd is the privileged binder — supporting `LISTEN_FDS`
gets port 443 with no new privilege machinery at all, and composes with
either pool design.

One consequence to embrace rather than paper over: shared-nothing workers
mean **no cross-request in-memory state**. State lives in SQLite or
`persist` — which is crash-safe and roll-safe (Section 7), and should be
documented as the model, not discovered as a surprise.

---

## 6. Ports, TLS, and not needing nginx

**[answered]** The old open question — does the existing library do TLS —
is settled by reading the code: **the current webserver has none** (OpenSSL
is linked for the crypto builtins only; HTTPS exists solely client-side via
libcurl). So TLS termination here is a subsystem, not an addition: SNI cert
selection, renewal without restart (Let's Encrypt rotates ~60 days — this
needs `on reload` or equivalent), and the hardened HTTP parsing that
actually bites — header/body timeouts, request buffering so a slow client
cannot hold a worker, smuggling-resistant parsing.

That is exactly the "real native machinery" class that earns C — and it
argues for sequencing, not for skipping: **v1 ships HTTP behind a reverse
proxy** (`trust_proxy` plus `X-Forwarded-For`/`-Proto`, which is needed
either way and is cheap), with the `cert:` syntax parsed and reserved.
Direct TLS is its own hardening phase (PLAT-WEB-3).

`setcap cap_net_bind_service=+ep` on the `gbasic` binary is the option to
**avoid**: it grants the capability to the interpreter, so any `.bas` file
gains it. Prefer, in order: socket activation / fd passing from a privileged
supervisor; `net.ipv4.ip_unprivileged_port_start` where the host is
dedicated; `setcap` only on a purpose-built binary.

`root "public"` static serving carries the classic hole: the design commits
to canonicalize-then-check (no `..` escape, no symlink walking out of the
root — the resolved path must be inside the resolved root), and routes win
over files on overlap. Both stated now so no golden ever enshrines the
accident.

---

## 7. Reload without downtime

Trigger is a **signal** (SIGHUP by convention). An HTTP endpoint calling a
reload function is one caller of that mechanism, not the mechanism — so a
deploy script, systemd, and an API call all reach the same path.

In-process reload is impossible: the program epilogue tears down global
state and there is no eval-of-string. So reload is process-level, and with a
worker pool it is a **rolling restart**: replace one worker at a time, so
capacity never drops to zero and no full handoff dance is needed.

The rule that makes it safe: **never retire a worker on faith.** A
replacement must parse its source, come up, and report ready before the old
one is drained. Add a probation window — if the new worker dies within N
seconds, stop rolling and keep what is serving. A syntax error in a deploy
then produces a failed deploy and zero downtime, which is the actual
guarantee wanted.

`process.start` / `poll` / `stop(force_after:)` from PLAT-PROC already
provide the supervision primitives — and if the worker pool is actors,
`monitor`/`demonitor` death notification does too.

Two consequences to accept rather than paper over: in-memory session state
dies at every roll unless externalized (Section 5 already forces this), and
an HTTP-triggered reload is a remote-code-execution surface the moment
anything else can write to the source directory.

Logging splits cleanly today: **access log on stdout, errors on stderr via
`print to error`**. **[corrected]** The first draft believed gBASIC could
not write to stderr; PLAT-STDERR shipped it, suite-enforced, before this
design existed.

---

## 7b. When a handler fails: let it crash

gBASIC **cannot catch a raise** — settled doctrine, proven and suite-pinned
(`on error resume next` abandons the caller's statement; there is no
middleware position from which to turn an unexpected raise into a 500). So
this design does not pretend otherwise. The model is the Erlang one, forced
by the language and adopted deliberately:

- A handler that raises kills its worker. The request in flight is lost;
  the proxy reports 502 for it. Nothing else is harmed, because workers are
  shared-nothing and state lives in the store.
- The supervisor restarts the worker under the same never-on-faith rules as
  Section 7, with **restart-storm protection**: a budget of restarts per
  window; exhausting it stops the pool from thrashing and surfaces the
  failure loudly instead of burning CPU on a crash loop.
- Handlers that want graceful degradation use the language's own doctrine:
  **pre-validate, report failure as a value** — `try_decode` for hostile
  bodies, `has()` before field access, refusal-shaped library calls. The
  skeleton example already reads this way naturally; the design should show
  it as the house handler style.

This is a coherent enterprise posture — crash-only software with fast
supervised restarts — but only if it is DESIGNED: the restart budget, the
proxy behavior, and the handler style are all named here so none of them is
discovered in production by the first null-field access.

---

## 7c. Observability

An ops team's first question. Shared-nothing workers cannot increment a
shared counter, so metrics need a designed path, not an assumption:

- Each worker keeps its own counters (requests, status classes, latency
  buckets) as plain in-process state — cheap, no coordination.
- The supervisor aggregates: workers emit a stats line on a control channel
  (the supervision plumbing already exists), and a `/-/metrics`-style
  endpoint — served by the supervisor, not a worker — reports the merged
  view. A health endpoint is just a route.
- Logs are already right: access on stdout, errors on stderr
  (`print to error`), one structured line per event via `encode()` if the
  estate wants JSON logs.

Deliberately not designed now: tracing propagation beyond passing
`traceparent` headers through untouched (which costs nothing and should be
the default).

---

## 7d. Performance posture

Short term, the answer is **horizontal**: shared-nothing workers with state
in the store make multi-machine scale-out the same operation as multi-worker
scale-up — a load balancer in front of N hosts running the same program.
That is Matthew's stated position and the architecture already earns it: the
properties that make rolling restarts safe (no in-memory sessions, crash-safe
state) are precisely the ones that make a second server free.

Long term, Matthew's stated direction is an **LLVM backend** compiling
gBASIC to machine code. That is a language-level project far beyond this
document, but it is worth one observation here: the tree's test discipline
is quietly compiler-enabling. Hundreds of byte-exact goldens, hundreds of
pinned negatives, and an 18-million-cell external corpus are exactly the
differential-testing oracle a second execution engine needs — the
tree-walker's behavior IS the specification, and every golden is a
conformance test the compiled backend must match. Nothing about this server
design needs to change for that future; handlers are ordinary gBASIC and
get faster for free.

---

## 8. Load-time checks

Duplicate method+path within a site; unroutable ambiguity between patterns;
malformed `{...}` patterns; unknown verb identifiers; duplicate `host`
across sites in one server; unknown directives; a `stream` used where a
request handler is required; `root` declared twice. All of these are
diagnosable without I/O, which puts them in `--json-diagnostics` and
therefore in Studio's error attribution for free.

Deliberately runtime, not load-time: cert file existence, port availability,
static directory existence.

---

## 9. Testability

Because declaration is inert, two things become possible with no socket —
namespaced **`web.*`**, not `server.*`: **[corrected]** `server` becomes a
reserved word, and naming the introspection library after a keyword invites
the exact dotted-form trap this tree keeps a standing rule about. `web`
remains an ordinary identifier and is the word users already see in the
block.

- `web.routes( edge )` → the route table as data, so route sets can be
  golden-tested.
- `web.dispatch( edge, req_record )` → runs one handler and returns the
  response, so handlers unit-test as pure-ish functions.

Both fit the fixture-first discipline and both are worth having before any
of the network code exists. For live tiers: `port: 0` binds ephemeral, the
bound port is readable off the serve handle, and the loopback self-test
pattern `run_webserver.sh` already proves carries over.

---

## 10. The test that decides whether this is worth grammar

**Specify the block as pure sugar that lowers to the existing web library.**
Write the lowering by hand for the two examples in Section 2.

- If the lowering is clean, the shape is right and the block can ship as a
  front end over code that already works.
- Wherever the lowering *cannot* be expressed — and multi-site SNI,
  `stream`, worker pooling, and drain are the likely places (the fd-transfer
  finding in Section 5 shortens this list) — that is the honest list of what
  the runtime is actually missing. Those, not the syntax, are the real work.

This also sequences the project: library capability first, grammar last,
which keeps `parser.y` out of the experimental phase entirely.

---

## 11. Out of scope for a first version

Websockets, HTTP/2, ACME automation, sessions and cookie middleware,
templating, compression, rate limiting, per-route auth declarations. Each is
a plausible later directive; none should be designed in now.

---

## 12. Decided against, with reasons

- **`print` writing the response body.** Beautiful in the one-line case,
  wrong overall: handlers call library functions that print for logging, and
  in a worker model stdout *is* the access log. Making `print` mean two
  things depending on dynamic scope is exactly the mental inversion the
  block is supposed to remove. (The same reasoning renamed the stream verb
  from `send` to `emit` — Section 4.)
- **Sites and listeners as separate top-level declarations.** Rejected in
  favor of nesting: the binding becomes structural instead of a name
  reference that can drift, and `cert` lands next to `host`. Cost accepted:
  a site lives on one listener, and port 80 alongside 443 is handled as a
  listener property (`ports: [80, 443], redirect_http: true`) rather than a
  second site.
- **HTTP verbs as grammar productions.** Parameterization where unification
  is available. See Section 3.
- **Names on handlers and `root`.** A handler's identity is method plus
  path; nothing can reference it. Only the server (and each site, for
  diagnostics) needs a name.
- **`serve` as a statement.** A builtin call does the same job with zero
  grammar — Section 2.

---

## 13. Open questions

1. **RESOLVED by the lowering study (2026-08-20): response by return
   value.** The hand lowering needed no `res` plumbing anywhere — including
   redirects and errors — and the one case that wants incremental output is
   `stream`, which has `emit` and is its own kind.
2. The socket handle value kind (Section 5): its lifecycle, its display
   form, its refusals (encode? send to a non-worker actor?) — the full
   new-value-kind checklist the watcher work just walked, including the
   `eval_comparison` branch that three kinds in a row have forgotten.
3. Whether `workers:` defaults to 1 (simplest possible story: the minimal
   example serves without any pool machinery) or to a CPU-derived count.
   Leaning: 1 — production explicitness over magic.

Resolved since the first draft: stderr exists (`print to error`); the
current library has no TLS (Section 6); `serve` is a builtin (Section 2);
contextual identifiers need no lexer change (the IDENT productions of
Section 3, which was the preferred answer); pattern captures live on
`req.params` (Section 4); `(req)` beats segment-parameter binding
(Section 4).

---

## 14. Suggested phasing

| Phase | Content |
|---|---|
| PLAT-WEB-0 | Lowering study (Section 10). **DONE 2026-08-20 — [`plat-web-lowering-study.md`](plat-web-lowering-study.md).** Verdict: the shape is right; six gaps (bind address, safe static, worker pool, TLS, streaming, LISTEN_FDS) map onto the phases below; everything else is sugar over what exists. |
| PLAT-WEB-1 | **DONE 2026-08-21.** Gap A: `webserver.listen(port, { address: })`, default still loopback, `server.address` read back from the socket, IPv6 + dual-stack (`tests/run_web_bind.sh`). Route table as data: `stdlib/web.bas` — `web.routes` validating at build time, `{id}`/`{rest...}` patterns into `req.params`, specificity matching that does not depend on table order, `web.dispatch` returning a response record the server takes verbatim, `web.resolve` for the pure match, `web.paths` for introspection (`docs/web_routing.md`, `tests/run_web_routes.sh`). |
| PLAT-WEB-2 | Worker pool + drain + rolling reload. Decide the Section 5 column (actor pool with a socket value kind is the current lean); `LISTEN_FDS` support. |
| PLAT-WEB-3 | TLS/SNI, static root (canonicalize-then-check), `trust_proxy`, timeouts. |
| PLAT-WEB-4 | `stream` / SSE with `emit`. |
| PLAT-WEB-5 | Grammar. Last, and only if 0–4 proved the shape. |

# gBASIC AI Support: Reference Application and Revised Primitives

Status: design proposal, 2026-09-05. Nothing here is implemented.
Code sketches are illustrative gBASIC; exact syntax is deferred to the phase
documents. The term used here for the "blessed path" is **reference
application**, on the same footing as Studio: an application written in gBASIC
whose purpose is to prove the platform primitives are sufficient and to expose
every gap before a customer does.

Revised the same day against the tree at `69c3296`, twice. The first draft
was designed; the first revision *checked* it against `src/eval.c`, `stdlib/`
and the suites; a second review then found what the first revision had only
half-said, and that was checked too — with numbers where the decision turned
on one. Every place the design and the code disagree is in Part 3. Two are
load-bearing: the server's main loop as first drafted is the documented
anti-pattern that hangs by design, and tool bodies as first drafted ran on
the same thread, so every tool call would have frozen every client (fixed
with a worker pool, chosen by measuring 78 ms per spawn against 0.015 ms per
message). A third looked load-bearing on reading — the retrieval query
cannot be executed by `pg` as sketched — on *running* turned out to be a
schema choice, and by the end of the day was moot, because the missing
primitive was built; it is recorded because the reversal is the point.

The document has four parts:

1. The reference application, designed end to end.
2. What designing it forced: revisions to the primitives proposed earlier.
3. What checking it against the tree forced: corrections to Parts 1 and 2.
4. The revised primitive set, consolidated.

Part 2 is the point of the exercise and Part 3 is its control. Part 4 is
what should go into phase documents.

---

## Part 1: The reference application ("Steward")

### 1.1 What it is

An assistant that acts *on behalf of an employee* against an organization's
Postgres database, a document repository, and a mailbox, and that can be
consumed by other agents as an MCP server. It exists in the minimum form that
still exercises every hard requirement identified in the persona walk-through:

- caller identity carried through retrieval and tool execution
- streaming replies to a browser while streaming from a model
- tool calls, including one mutating tool behind a human approval gate
- an audit record for every turn and every tool call
- incremental indexing with per-chunk ACLs
- MCP in both directions
- record/replay so tests and evals run offline
- multi-tenant configuration as data

Anything not on that list is out of scope for the reference. In particular:
no rich UI, no real Exchange/SharePoint connector (a directory-backed
connector stands in), no auth beyond a verified header, no vector database.

### 1.2 Programs

Four programs, one shared library:

| Program | Role |
|---|---|
| `steward.bas` | web server: chat UI, SSE streaming, approval endpoint, audit |
| `indexer.bas` | incremental indexing of documents and mail into `pg` |
| `mcpserve.bas` | exposes the tool set over MCP (stdio and HTTP) |
| `evalrun.bas` | runs the fixture set in replay mode, gates on cost/latency |
| `steward_lib.bas` | the toolset (`tools.define`), the tool bodies, connectors, retrieval, config |

### 1.3 Configuration as data

One record per tenant, loaded from a file named by environment, never from
source literals:

```
config = decode(read(tenant_file))
' { name, db:{...}, model:{provider, model, key_env}, docs_root, mail_root,
'   budgets:{per_user_daily_usd, max_steps}, replay:{mode, path} }
```

The model key is read from the environment variable named in `key_env`. The
audit and replay stores are redacted of it by construction because it never
enters a request record.

### 1.4 Identity and principal

The server verifies a caller (for the reference, a signed header) and builds a
context record:

```
ctx = {
    user: "ada",
    groups: ["staff", "lending"],
    tenant: config.name,
    trace: hex_encode(random_bytes(16)),
    budget_usd: remaining_budget("ada")
}
```

(There is no `new_id()` builtin; `hex_encode(random_bytes(16))` is the
existing spelling and is what the crypto suite uses for nonces.)

Every request handler runs its body inside `with principal ctx`. (The first
draft said `context`; Part 3, item 3 explains the rename.) The retrieval
and dispatch libraries refuse to run outside a principal scope; this is what turns
permission filtering from a rule into a structural property.

### 1.5 The tool set

```
steward = tools.define("steward", [
    { fn: lookup_member,   reads: true,   describe: "Look up a member by member number.",
      params: [ { name: "member_id", modifier: "trimmed" } ] },
    { fn: recent_activity, reads: true,   describe: "Recent transactions for a member.",
      params: [ { name: "member_id", modifier: "trimmed" },
                { name: "days", kind: "number", default: 30 } ] },
    { fn: search_documents, reads: true,  describe: "Search policy and procedure documents.",
      params: [ { name: "query", modifier: "trimmed" },
                { name: "limit", kind: "number", default: 8 } ] },
    { fn: search_mail,     reads: true,   describe: "Search the caller's own mailbox.",
      params: [ { name: "query", modifier: "trimmed" },
                { name: "days", kind: "number", default: 90 } ] },
    { fn: freeze_card,     mutates: true, describe: "Freeze a card. Requires approval.",
      params: [ { name: "card_id", modifier: "trimmed" } ] }
])
```

The first draft wrote this as a `tools ... end tools` grammar block; Part 3
item 9 explains why the library form is the design and the block is at most
later sugar. `fn` is a function value, so a tool bound to a function that
does not exist fails on this line, at load. `params` builds the JSON schema
sent to the model and validates arguments at dispatch; the modifier named
there is applied to the argument exactly as `x{trimmed}= v` would. `reads`
and `mutates` drive approval, idempotency and replay (see 1.8). Tool bodies
are ordinary functions and run **in a worker pool**, not on the event loop
(Part 3, item 7).

Function bodies are ordinary gBASIC. `search_documents` is the only one that
matters for the design; the rest are straightforward `pg.query` calls.

### 1.6 Indexing

`indexer.bas` runs on a schedule. For each connector (documents, mail):

1. `changed_since(last_run)` yields items with an id, a content hash, and
   an ACL (list of groups, or a single user for mail).
2. Items whose hash is unchanged are skipped; deleted items are removed.
3. Text is extracted by `process.start` of an external tool (pdftotext,
   etc.). gBASIC parses nothing.
4. Text is chunked (a gBASIC function; chunk boundaries are a policy, not a
   primitive).
5. Chunks are embedded in batches through `llm.embed`.
6. Each chunk is stored in `pg` with its vector, source id, ACL, and hash.

The indexer is resumable at any point because every step is keyed by content
hash. A crash mid-run costs nothing but the batch in flight.

### 1.7 Retrieval

```
' Runs in a pool worker (Part 3, item 7), so the embed and the query may block.
function search_documents(query, limit)
    ctx = principal()
    qvec = llm.embed(client, [query])[0]
    rows = pg.query(db,
        "select id, text, source, vec <-> $1::vector as dist from chunks " +
        "where exists (select 1 from jsonb_array_elements_text(acl) a " +
        "              join jsonb_array_elements_text($2::jsonb) g on a = g) " +
        "order by dist limit $3",
        [encode(qvec), ctx.groups, limit])
    ...
end function
```

The ACL filter and the ranking are **one query**: the permission predicate
runs before the nearest-neighbour order, so a narrowly permitted user gets
their own top-k rather than zero results because the global top-k were all
restricted. That was the argument for a `rank` primitive with a candidate
mask (Part 2, item 5); pgvector does it in SQL and the primitive is deferred
(Part 3, item 10).

With native arrays in `pg` (Part 3, item 1 — built the same day the gap was
found) the ACL may equally be a `text[]` and the predicate the two-character
`acl && $2`; the `jsonb` join above is what ran on the module *before* that,
and it is kept because it still works and still reads as what it does. The
vector stays pgvector's own type: it is not a `float8[]` and never was.

### 1.8 The conversation loop, which is not a loop

The naive design is a loop: send transcript, stream reply, on tool call
dispatch and re-send, until done. The reference cannot be written that way,
because of the approval gate. When the model requests `freeze_card`, the
program must show the request to the user and wait. The user may take a
minute; the wait spans HTTP requests; the server is single-threaded and
watcher-driven. Nothing may block.

So a conversation in progress is a **value**, a run record:

```
run = {
    id:, ctx:, transcript: [...], step: 0,
    stage: "idle" | "calling_model" | "awaiting_tool" | "awaiting_approval" | "done",
    pending: [ {tool_call} ],
    usage: {input_tokens, output_tokens, usd},
    expires_at:
}
```

`expires_at` is what keeps a run whose user closed the tab from living in
`pg` forever; the approval endpoint refuses an expired run and a periodic
sweep deletes them. A mutating call's idempotency key is
`hash(run.id + tool_call.id)` — the model's own per-turn call id, already in
the transcript — passed to the tool as a named argument, so a `freeze_card`
replayed after a restart can find its own earlier effect (Part 3, item 11).

The live model-call handle is deliberately *not* a field of the run.
`encode` refuses a live value by design (`tests/negative_encode_typed.bas`,
`negative_json_strict_live.bas`) — a lossy token would yield text `decode`
cannot read back — so a run that carried its handle could not be stored
between requests, which is the whole reason the run is a record. The server
keeps an in-memory map from `run.id` to handle; the run stays pure data.

and the loop is a pure step function driven by events:

```
run = agent.step(run, event)
' event kinds: model_event (from llm.read), tool_result, approval, cancel
```

`agent.step` never performs I/O. It returns the new run plus an array of
*actions* the caller must perform: `start_model`, `dispatch_tool`,
`ask_approval`, `emit_text`, `finish`. The server performs actions and feeds
results back as events. Because the run is a plain record, it survives
`encode`/`decode`, can be stored in `pg` between HTTP requests, and can be
resumed after a restart.

The approval flow is then trivial: `step` returns `ask_approval`; the server
stores the run and renders the request; the approval endpoint loads the run
and calls `step` with an `approval` event; `step` returns `dispatch_tool`.

### 1.9 The server's main loop, which the program does not write

The server block handles incoming requests. But model calls and tool
dispatch are long-lived handles that need attention when *no* request is
arriving. The first draft of this section had the program wait on all of
them in a loop of its own:

```
' THE FIRST DRAFT. Do not write this -- it is the documented anti-pattern.
while server.running
    ready = wait_any(active_handles, 0.25)
    ...
end while
```

That is, line for line, the shape `docs/reference.md` warns "hangs by
design": a single-process server serves from the event loop that runs
**after `main` returns**, so a program that loops after `serve()` binds the
listener, accepts connections, and answers nothing. The interpreter now warns
about it. The reference application would have shipped its author the exact
bug the platform documents.

The correct shape has no loop in the program at all. Incoming requests are
already delivered by the platform's event loop into `server.requests` and
handled by a watcher. Model events are delivered the same way:

```
watch(llm.events)
    while count(llm.events) > 0
        ev = take_first(llm.events)
        run = runs[ev.run_id]
        apply(run, agent.step(run, {kind:"model_event", event:ev}))
    end while
end watch

program main(args)
    h = serve(steward)
end program
' main returns; the event loop runs; both watchers fire as things arrive.
```

Where `apply` performs returned actions: pushing text to the SSE response,
sending a `dispatch_tool` to a free pool worker, storing the run, appending
audit rows. **Nothing in `apply` blocks**: a tool body runs in the worker,
and its result comes back as an event through the same delivery as a model
event (Part 3, item 7). The platform's one `poll()` grows to include `http`
and `process` handles beside the listener and its clients (Part 3, item 2).
There is no `wait_any` for a program to call, because a program that is
waiting is a program that is not serving — and a tool that waits is a tool
that runs somewhere else.

### 1.10 Audit

One record per event of interest, appended to `pg`:

```
{ trace, user, tenant, at, kind:"model_call"|"tool_call"|"approval"|"reply",
  detail:{...}, usage:{...} }
```

The shape is deliberately the same family as Studio's result record. An
examiner asking "who froze this card and what did the model see" is served by
a single query on `trace`.

### 1.11 MCP, both directions

**Publishing.** `mcpserve.bas` obtains the schema list for `steward` via
`tools.schema("steward")` and serves it two ways: over stdio (JSON-RPC lines
on stdin/stdout, the transport desktop clients launch — and it must run under
`--line-buffered` or its replies sit in a block buffer until exit, which is
the standard pipe-buffering trap PLAT-STREAM exists for and a deadlock from
the client's side) and over HTTP using the `server` block. Both dispatch through the same `tools.dispatch`, inside the
same `with principal` discipline (the MCP client's identity is mapped to a
context by configuration).

**Consuming.** A tool entry may bind to an MCP server instead of a local
function:

```
    { name: "read_file", via: { mcp: "fileshare" }, reads: true,
      describe: "Read a file from the shared drive.",
      params: [ { name: "path", modifier: "trimmed" } ] }
```

**The mapped principal is the leak surface.** A `reads` tool published over
MCP is answered by whatever principal the configuration maps the calling
agent to, which is exactly the service-account god-view persona A's leak
came from. The mapping must name a principal as narrow as the
least-privileged human it stands in for — never a group union — and
`mcp.serve` refuses to start when the mapped principal's `groups` exceed a
configured ceiling (Part 3, item 11).

At load time the entry is syntactically checked; at `mcp.connect` time the
server's advertised tool list is checked against the declaration and a
connect-time error is raised on mismatch. Load-time and connect-time failures
are distinct diagnostics.

### 1.12 Evaluation

`evalrun.bas` loads a fixture set:

```
{ question:"Can a member have two share accounts?",
  as_user:"ada", expect:{mentions:["policy 4.2"], not_mentions:["HR"], max_usd:0.02, max_seconds:6, tools_called:["search_documents"]} }
```

runs each through `agent.step` with the client in replay mode, and gates.
Fixtures are recorded once against a live provider and committed. The
suite runs in CI with no key present. This is the `run_*.sh` pattern with a
different oracle.

---

## Part 2: What designing Steward forced

These are the revisions, in order of consequence. Several reverse positions
taken in the earlier primitive proposal.

### 1. A unified wait is required, or the server busy-polls

Steward has four handle families live at once: the server socket, model
calls (`http`), tool dispatch that spawns processes (`process`), and possibly
MCP subprocesses. A single-threaded program with no way to say "wake me when
any of these has data" either busy-polls or sleeps blindly. Neither is
acceptable for a responsive UI.

**Revision as first drafted:** add `wait_any(handles, seconds) ->
ready_handles` at the platform level, accepting a heterogeneous array of
process and http handles. Both families are fd-backed in C, so this is
`poll()` plus `curl_multi_fdset`.

**Revised again in Part 3, item 2.** The analysis stands — the program must
not busy-poll — but a *call the program makes* is the wrong shape for this
runtime. The platform already owns a `poll()` loop, it runs after `main`
returns, and a program blocked in `wait_any` never reaches it. Readiness has
to be delivered by that loop, as watchers, the way `server.requests` already
is. The diagnosis was right; the remedy was one level too high.

Corollary, unchanged: handles need a common identity field (`h.kind`, `h.id`)
so a delivered event can be routed back to the run that owns it.

### 2. The agent loop is a pure state machine, and it belongs in stdlib

The earlier proposal kept the tool loop out of the platform and in the
application. Building it shows why that is half right. It must not be in the
*platform* (it has policy in it), but it is identical for every application,
and it has a subtle correctness requirement (suspend across requests without
blocking) that every developer would get wrong the first time.

**Revision:** `agent.step(run, event) -> {run, actions}` as a stdlib library,
pure over a serializable run record. The platform still owns no loop; the
application still owns all I/O; but persona A does not write a state machine.
This is the same split as Studio versus the platform, applied one level down.

What this *replaces* should be named: `stdlib/llm.bas` already carries a tool
loop — `with_tools`, `tool(name, description, schema, fn)`, `tool_calls`,
`execute_tools`, `append_tool_results` and `run_tools(m, system, messages)`,
which drives the round-trips to completion. It is the blocking form, and it
is exactly what cannot suspend for an approval. `agent.step` is that loop
turned inside out, and `run_tools` becomes the trivial driver over it for
callers with no gate — so the existing suite for it becomes the first test
of the step function.

### 3. The transcript needs a canonical part-based message shape

A message is not `{role, content:string}`. After a tool call the assistant
message contains text *and* one or more tool-call parts, and the following
user message contains tool-result parts. Providers represent these
differently, and a transcript re-sent with the wrong shape is rejected.

**Revision:** the `llm` library defines
`{role, parts:[ {kind:"text", text} | {kind:"tool_call", id, name, args} | {kind:"tool_result", id, content} ]}`
as the canonical shape, translates per provider, and keeps a provider-specific
`raw` on each part for round-trip fidelity. `agent.step` builds transcripts
in this shape only.

### 4. Replay keys must exclude volatile fields and count occurrences

Two failures appear immediately in `evalrun`. First, a system prompt that
includes the date never matches a recorded key. Second, a retried request
(same bytes) must replay the *second* recorded response, not the first, or
retry logic is untestable.

**Revision:** the replay key is `hash(canonical(request) minus declared
volatile paths) + occurrence index within the run`. The library exposes
`llm.fingerprint(request)` so applications can see and override the key. The
`trace` id from context is never part of it.

Today's `llm.offline(m, dir)` is not keyed at all: it returns one fixed
`<format>_response.json` for *every* request. That is enough to test a single
turn offline and cannot replay a conversation, since every turn gets the same
answer. So this is a real advance and not a refinement, and the replay seam it
grows out of is the one `market.offline` and `edgar` also use.

### 5. `rank` needs a candidate mask

Filtering by ACL *after* ranking is wrong: a narrowly permitted user gets
zero results because the top-k were all restricted. Filtering *before*
ranking with the earlier signature means copying a filtered sub-array of
vectors on every query.

**Revision:** `rank(query, corpus, k, candidates)` where `candidates` is an
optional array of indices into `corpus`. The corpus stays resident and
unchanged; the permission filter produces a small index array. For the
`pg`-resident case the same effect comes from the SQL predicate, and the
library routes between the two on corpus size.

**Retired in Part 3, item 10.** The analysis about filtering before ranking
is right and it is what the SQL does; the primitive it argued for has no
consumer once pgvector answers against the module as it is.

### 6. Tool parameters need types, and dispatch must convert errors into results

The model will send malformed arguments. If the tool function raises, the run
dies. The earlier `tools` block carried only parameter names.

**Revision:** entries carry per-parameter modifiers and defaults. The same
declaration generates the JSON schema and validates at dispatch.
`tools.dispatch(name, args)` validates, runs the function under `on error`,
and returns *any* failure as a tool-result part the model can react to — and
it runs **in a pool worker**, never on the event loop (Part 3, item 7), which
is also what makes a tool that raises survivable: the worker's `on error`
turns the raise into a result part, and a worker that dies is respawned by
the pool without the loop noticing more than a late reply.
Missing required parameters and modifier failures are load-time checkable in
the sense that the schema is derived at load; the argument check happens at
dispatch by necessity.

### 7. The toolset must be reflectable

`mcpserve.bas` cannot publish what it cannot enumerate. The earlier proposal
treated the block as consumed only by the runtime.

**Revision:** `tools.schema(name)` returns the toolset's entries as records
(name, params with modifiers, reads/mutates, description, binding). In the
library form (Part 3, item 9) this is not reflection at all — the entries
*are* records, and `schema` returns what `define` was given — which is what
makes "publishing is just `server` plus `tools`" actually true.

### 8. MCP publishing needs a stdio transport, not only HTTP

Desktop MCP clients launch the server as a subprocess and speak JSON-RPC over
stdin/stdout. The `server` block does not cover that.

**Revision:** `mcp.serve_stdio(toolset)` in the `mcp` library, a small loop
over `input`/`print`. HTTP transport via the `server` block remains. Both
share `tools.dispatch`.

### 9. The principal is data that lives in the run, and `with principal` is the enforcement point

Because a run suspends across requests, the `with principal` scope that
started it has exited by the time an approval arrives. The principal must be
stored in the run and re-established on resume.

**Revision (clarification, not a signature change):** `with principal` takes a
record, which the run record already carries; libraries enforce presence of a
principal but do not own it. The reference documents the rule: *every* resume
path re-enters `with principal run.ctx`. A principal does not cross `spawn`; that
is stated rather than discovered.

### 10. Cancellation needs a client-disconnect signal from the server

When the user closes the tab mid-stream, Steward must `llm.stop(handle)` or
pay for tokens nobody reads. This requires the `server` block to surface
that the response's client has gone away.

**Answered, from the tree:** yes. `emit` on a stream whose client has gone
returns `false` and the stream is reaped — asserted by
`tests/run_web_stream.sh`'s `STREAM_DISCONNECT` tier, which kills the
listener and requires the next `emit` to say so. Steward's `apply` therefore
checks the result of every emit and issues `llm.stop(handle)` on the first
`false`. No server-side addition is needed.

### 11. Approval needs a human rendering, and the cheap answer is good enough for now

"Approve freeze_card({card_id:88213})?" is not a usable prompt. The full
answer is a preview binding per mutating tool. The cheap answer, which the
reference uses, is: show the declared description, the arguments, and the
model's own preceding text. A `preview` clause on `mutates` entries with a
load-time check that every mutating tool has one is the correct later
refinement, and it is deferred deliberately.

### 12. Batch embedding is the primitive; single embedding is the special case

Indexing 10,000 chunks one call at a time is unusable. `llm.embed` takes an
array of strings and returns an array of vectors. The single-string form is
the caller's one-element array.

### 13. Things the reference confirmed should *not* exist

No streaming JSON parser (tool-call arguments are buffered until the part
completes). No SSE framing in C — a gBASIC helper `sse.frame(buffer)` is
adequate, with one condition PLAT-STRIDX does not remove: indexing is linear
now, but `frame` returns `rest` as a new string, so each call copies what it
did not consume. That is fine while `buffer` is one `http.read` chunk and
becomes quadratic if a caller accumulates an unbounded buffer. The helper's
contract says so, and its suite carries a shape tier (ratio across a 4x step,
the `run_stridx` pattern) rather than a reading. No tokenizer. No connector
implementations in the stdlib. No vector database. No prompt DSL. All of
these were provisional exclusions before; Steward was built without them and
did not miss them.

---

## Part 3: What checking Steward against the tree forced

Part 2 is what *designing* the application found. This part is what
*reading the code it would run on* found, and it is kept separate because it
is a different kind of evidence: each item below is a measurement in
`src/eval.c`, `stdlib/` or a suite, not a design judgement. Two of them would
have stopped the reference on its first day; the others change names or
order.

### 1. `pg` could neither return nor accept a native array — and now does

The retrieval query in §1.7 selects a vector column and filters with
`acl && $1`. Both halves fail, and both were **run**, against PostgreSQL
17.10 with pgvector 0.7.2, once a database was provisioned
(`tools/setup_postgres_dev.sh`):

- **Array results raise.** `text[]` and `float8[]` columns both stop the
  program with `PostgreSQL array result types are not supported`
  (`src/eval.c:19265`, twenty-two array OIDs).
- **Array parameters are sent as JSON.** `[["staff","lending"]]` reaches
  Postgres as `["staff","lending"]`, and Postgres itself says why that is
  wrong: `malformed array literal: "["staff","lending"]" — "[" must introduce
  explicitly-specified array dimensions`.

The first draft of this item, written before the database existed, concluded
that native arrays were the missing primitive and placed them ahead of
everything in the build order. **Running the workarounds reversed that.** A
JSON parameter is exactly what a `jsonb` column wants, and pgvector's text
form is JSON-shaped:

```basic
' ACL stored as jsonb: "any of these groups" is a join, and it works.
rows = pg.query(db, "select id, text from chunks where exists (" +
    "select 1 from jsonb_array_elements_text(acl) a " +
    "join jsonb_array_elements_text($1::jsonb) g on a = g)", [ctx.groups])
' -> 2 rows, the right two.

' pgvector column: arrives as the string "[0.1,0.2,0.3]"; decode reads it.
' Sent back for a similarity search as encode(v) with a cast: nearest found.
rows = pg.query(db, "select id, text, vec <-> $1::vector as dist " +
    "from chunks order by dist limit 1", [encode(qvec)])
' -> "lending policy 4.2", dist 0
```

So the schema Steward needs was `acl jsonb, vec vector(n)` — which is also
the schema a pgvector-backed store would use anyway — and §1.7 ran on the
module as it was. Native array support was still a real gap (the `&&`
operator is the natural spelling, and a `text[]` column from an existing
schema was unreadable), so it was documented with the working alternative
and put on the DOGFOOD ledger, out of Steward's critical path.

**And then it was built, the same afternoon**, because the database was
there and the negative control was in place. Twenty-two array types read as
gBASIC arrays; an array parameter is rendered as a literal or as JSON *by the
type Postgres infers for its position* — the statement is prepared and
described first, which is the only place the answer to "is this JSON or an
array" exists — so every `jsonb` call keeps working and `acl && $1` over a
real `text[]` runs verbatim. The suite uses Postgres as the oracle for what a
parameter became. `docs/reference.md`, "PostgreSQL Module". Steward may use
either schema; the ACL as `text[]` with `&&` is now the simpler one.

Two lessons this item carries beyond its content. The limitation was
undocumented and untested, which is how a careful design walked into it: a
gap nobody has written down is a gap the next design will find the hard way.
And the first draft of this correction was itself wrong in its conclusion,
because it reasoned about the workaround instead of running it — one
`sudo` and forty lines of gBASIC later, "ahead of everything" became "not in
the critical path". That is the automation-recipe lesson a third time, in a
document about a different subject.

### 2. `wait_any` cannot be a call, because the program is not the one waiting

The first draft's main loop (§1.9, first form) is the shape
`docs/reference.md` documents under "**Let `main` return. Do not loop after
`serve`**": a single-process server serves from `webserver_run_event_loop`,
which runs only after `main` returns, and a program that loops instead binds
the listener, accepts connections and answers nothing. It is a known enough
trap that the interpreter warns about it.

So the platform *already has* the one `poll()` the design wanted —
`src/eval.c:15971`, over the listener and every client fd on a 50 ms tick —
and the addition is not a second wait beside it but a wider first one: `http`
and `process` handles register their fds into that set, and when one is
ready the loop delivers an event into a watched value, exactly as it appends
a request to `server.requests` and fires `watch(server.requests)`
(`src/eval.c:15118`). The program writes a watcher and returns from `main`.

This is also why `agent.step` being pure is not a nicety: the step function
runs *inside a watcher*, on the event loop's thread, and a step that blocked
would stall every client.

### 3. `context` already means something in this stdlib

`reasoning.context_fields()` defines a Context as `{objectives, thresholds,
authority, approval}` and `reasoning.check_context` refuses any other field by
name; `decision.evaluate` and `automation.execute` both consume it. The
proposal's context is caller identity — `{user, groups, tenant, trace,
budget_usd}`. Same word, no overlap in meaning, and one of these will be
loaded into a program that also loads the other the first time an assistant
is asked to explain a Finding.

**Revision:** the identity record is a `principal`. `with principal p ...
end with` and `principal()`. The word is the one the security literature
uses for exactly this — the identity on whose behalf an action is taken —
and it is unused in the tree.

### 4. `tools` is not a free word

PLAT-WEB-5 took `server` as a reserved word after checking the tree for it
and finding nothing. The same check on `tools` finds 24 uses, including
`with_tools(m, tools)` and `_tools_wire(m, tools)` in `stdlib/llm.bas`,
where it is a **parameter name**. A reserved word there is a parse error in
the library the block exists to serve.

**Revision:** recognise the block by *position* rather than reserving the
word — `tools` at statement start followed by an identifier and a newline —
which is the technique `server` used for its verbs and `on warning` used for
its whole syntax, and costs no reserved word. The grammar cost (shift/reduce
conflicts) must be measured, as `IDENT expression` was measured and rejected
at 4. If it is not zero, the block gets a different word.

### 5. `with principal` is cheaper than it reads

`WITH` is already a token. `with lock(expr) ... end with` is recognised by a
string comparison on the identifier after it (`src/parser.y`,
`with_lock_statement`). `with principal p` is the same rule one word over,
not a new construct.

### 6. The stdio transport needs `--line-buffered`

`mcp.serve_stdio` is "a small loop over `input`/`print`". `print` to a pipe
is block-buffered by stdio, so a reply sits in the buffer until 4 KB
accumulate or the process exits — and a JSON-RPC client waiting on that reply
waits forever. PLAT-STREAM added `--line-buffered` for precisely this, and the
desktop client launches the process, so the flag belongs in the launch
configuration the phase document ships. `run_stream.sh` already proves the
flag; the MCP suite must prove the *absence* of the flag deadlocks, or the
next author removes it as noise.

### 7. Tool bodies run on the event loop's thread, and the first draft pushed the stall down a level

A second review caught what item 2 above had only half-said. `agent.step`
must be pure because it runs inside a watcher — correct. But the
`dispatch_tool` action runs `tools.dispatch`, which runs the *tool body*, on
the same thread. `search_documents` as written does `llm.embed` and
`pg.query`; `pg` is synchronous libpq, and an embedding call is a wait inside
a watcher — the exact stall the design forbids one layer up. Every tool call
freezes every client for one provider round trip plus one query, and a tool
with a five-second report query freezes the whole server for five seconds.
This is structural: the reasoning that made `step` pure applies to every
tool body, and the first draft moved the problem rather than removing it.

**Revision: tools run in a pool of pre-spawned worker actors, not on the
loop.** Two shapes were on the table and the numbers decide between them:

- *Spawn an actor per tool call.* Measured: a `spawn` round trip is **50 ms**
  for a bare program and **78 ms** with four stdlib loads, because the child
  is fork+exec and re-parses the source. That is a floor under every tool
  call before it does any work, and a `pg` connection cannot cross `spawn`
  (`reflect.serializable` says `false`), so each call would also reconnect.
- *A pool spawned at startup.* Measured: once a worker exists, a message
  round trip is **0.015 ms**. Each worker opens its own `pg` connection once.
  This is the shape the web worker pool already has, and it is the one that
  removes the concurrency ceiling the first draft was silent about: a
  single-process loop with synchronous tools serves as many concurrent tool
  calls as it has workers, and that number is configuration.

So the `dispatch_tool` action becomes *send `{principal, run_id, call}` to a
free worker*, and the worker's reply arrives as an event through the same
delivery path as a model event. `tools.dispatch` runs **in the worker**,
under `with principal` re-entered from the message — which is the explicit
principal handoff item 9 of Part 2 said does not happen across `spawn`, now
designed rather than forbidden: the principal is ordinary data, it travels as
the message's first field, and the worker's first act is `with principal
m.principal`. The run record already makes that cheap.

The other shape — tool bodies that are themselves steppable — was rejected
because it turns tool authoring into state-machine authoring, which defeats
the point of persona A writing ordinary functions. In the pool, a tool body
*is* an ordinary function; it just runs somewhere that is allowed to block.

A cost worth stating: a tool's result must be serializable to cross back.
Tools return records and strings; a tool that wanted to return a live handle
could not, and that is a feature.

### 8. `wait` and watched delivery need one coexistence rule

The six `http` primitives keep `wait`, and `indexer.bas` and `evalrun.bas`
need it — they are sequential programs with no `serve`. But if `start`
registered every handle into the event loop's set, a program that `wait`s on
a handle and later returns from `main` would see it delivered twice, or an
event loop with nothing to do; and today the loop runs **only** while a server
is active (`src/eval.c:33241`, `if (!exit_status && webserver_any_active())`),
so a program with outstanding handles and no server simply exits.

**Rule, one sentence:** a handle is *waited* or *watched*, never both —
`start` registers nothing; a handle joins the loop's set when a `watch` is
declared on its event value, and leaves it on `wait`, `stop` or `release`. The
event loop runs after `main` while there is anything to deliver — a live
server *or* a watched handle — and exits when both are empty. `wait` inside a
watcher gets the interpreter warning "loop after `serve`" already gets, for
the same reason and in the same words.

### 9. `tools` may not need to be grammar at all, and the tree says why

The block's justification was that only the parser knows a function's
parameter names, so schema derivation needs a declaration. The second review
proposed the library form instead — `tools.define("steward", [{fn:
lookup_member, reads: true, describe: "..."}])` — on the premise that
parameters can carry modifiers today and `reflect.signature(fn)` exists.
**Neither is true**: `parameter_list` admits an identifier with an optional
literal default and nothing else (`src/parser.y:1375`), and `reflect` has
eleven functions, none of which sees a parameter name. So the library form as
proposed would need platform work too.

But the record form does not need reflection if it says what it means:

```
tools.define("steward", [
    { fn: lookup_member, reads: true,
      describe: "Look up a member by member number.",
      params: [ { name: "member_id", modifier: "trimmed" } ] },
    { fn: freeze_card, mutates: true, describe: "Freeze a card. Requires approval.",
      params: [ { name: "card_id", modifier: "trimmed" } ] }
])
```

Zero grammar, zero reserved-word question, zero conflict count, and every
load-time check the block promised is a runtime check at the `define` line:
`fn: lookup_member` is a function value, so a missing function fails *there*;
duplicate names are a loop; the schema is built from `params` rather than
derived. What is lost is that the parameter list is written twice — once in
the function, once in the spec — and nothing today can verify they agree. A
`reflect.signature(fn)` builtin (name and parameter names of a function
value) would close that and is general beyond AI.

**Revision:** the library form is the design, not the fallback. The block
form is retained in Part 4 as sugar to be added *only if* the conflict count
is measured at zero — and if it is not zero, it is not added.

### 10. `rank` has no second consumer, and the reference no longer needs a first

After item 1, Steward ranks in SQL (`vec <-> $1::vector … order by dist`)
with the ACL predicate in the same query, and pgvector does this against the
module as it is. `rank(query, corpus, k, candidates)` now serves only a
hypothetical small non-Postgres corpus that the reference does not have. By
the project's own promotion rule — no primitive without a second consumer —
it is deferred, and Part 2 item 5 is recorded as reasoning about a primitive
that turned out not to be needed.

### 11. Two omissions with a security edge

**Run lifecycle.** A run parked in `awaiting_approval` when the user closes
the tab lives in `pg` forever, and a run mid-`dispatch_tool` at restart needs
an idempotency key the first draft mentioned and never located. Located now:
a run carries `expires_at`, set from `config.budgets.approval_ttl`; the
approval endpoint refuses an expired run and a sweep (the indexer's schedule
is already periodic) deletes them. The idempotency key of a mutating call is
`hash(run.id + tool_call.id)` — the model's own per-turn call id, which the
transcript already stores — passed to the tool as a named argument, so a
`freeze_card` replayed after a restart can find its own earlier effect.

**MCP publishing maps the calling agent to a principal "by configuration."**
That sentence is the service-account god-view that persona A's leak came
from: a `reads` tool published over MCP is answered by whatever principal the
mapping names, so the mapping *is* the leak surface. Rule: the mapped
principal must be as narrow as the least-privileged human it stands in for,
never a group union, and the MCP server refuses to start with a mapping that
names a principal whose `groups` exceed a configured ceiling. This is the
same argument the LDAP module made for referral chasing — on an
authentication path, a convenience is an instruction to send something
somewhere the operator never named.

### 12. Handle delivery needs no new mechanism, and two constraints it does have

Item 2 said readiness must be *delivered* by the event loop rather than
waited on, and left the delivery itself as phase 1's engineering. Measured, it
is mostly already there.

**A global record may share a name with a native module qualifier.** This was
the open question — `watch(http.events)` needs a global named `http`, and
`http.start(...)` needs the module. Both work at once, measured:

```basic
process = { events: [] }        ' a global record named after the module
watch(process.events)
    print count(process.events)
end watch
r = process.run({ command: "echo", args: ["hi"] })   ' still dispatches natively
process.events = append(process.events, "e1")        ' watcher fires
```

The *call* path checks the native module name before anything else; the
*field* path checks `env_lookup_exists` before treating `x.y` as a library
reference. So the `http` module binds a global `http = { events: [] }` at load
and appends to it, firing `watcher_trigger_change("http.events")` on a fixed
path. No new machinery, no scan.

**Two constraints to design around, both measured:**

- **`watch` takes a dotted name path.** `watch(box.events)` parses;
  `watch(pool[0].events)` is a **parse error** — the grammar's
  `watch_target_path` is names and dots. So a pool of workers cannot each have
  their own watched path. They share one queue and the reader routes by `id`,
  which is what item 2's `h.kind`/`h.id` corollary already required; this is
  the reason it is required.
- **Fixed-path delivery is better than the precedent.** The webserver finds
  its queue by scanning `global_env` for the variable holding the server
  record (`webserver_find_record`) and building the path from that variable's
  *name* — O(globals) per request, and it only works because the program bound
  the record to a global. A module-owned global needs neither.

What remains genuinely new in phase 1 is therefore the `http` module itself on
libcurl's multi interface, plus registering handle fds into the loop's
existing `poll()` set, plus the waited-or-watched rule from item 8. The
delivery is a dozen lines.

### 12b. `0 = "stop"` was true, and `agent.step` is a string state machine

Not in the design, and it would have been under it: `agent.step` dispatches on
`event.kind` strings and compares tool results, and until 2026-09-05 a string
compared against a number went through a fallthrough where every string became
`0` — so `0 = "stop"` was **true** while `1 = "1"` was false, and `1 > "stop"`
answered rather than refusing. Found measuring the worker-pool round trip for
item 8: a worker whose sentinel was `"stop"` exited on the message `0` and the
parent hung in `receive()` forever, which is the shape of half the bugs this
architecture could have.

Fixed (`CHANGELOG.md`): equality answers, ordering refuses, number-versus-
boolean unchanged at 1,472 measured uses. Recorded here because it is a
prerequisite that was invisible from the design, and because the way it was
found — building the smallest measurement rather than reasoning about the
design — is the same argument item 13 makes for a skeleton.

### 13. Small corrections

- There is no `new_id()`; `hex_encode(random_bytes(16))` is the spelling.
- A run record must not carry its handle (§1.8): `encode` refuses live
  values by design, so a run that held one could not be stored.
- `llm.offline` today returns one fixed response per format for every
  request; §2.4's keyed replay is a replacement, not a refinement.
- `llm.run_tools` already exists as the blocking tool loop; `agent.step` is
  it turned inside out (§2.2), and its suite is the first test of the step
  function.
- `docs/README.md` must list this document as **Proposal**. That status is
  load-bearing for a test: `run_stdlib_docs.sh` exempts Proposal documents
  from its "documents a function no library defines" tier, and this one names
  `llm.fingerprint`, `llm.read` and `llm.stop`, none of which exist. Unindexed
  it fails two suites.

---

## Part 4: The revised primitive set

### 4.1 Platform (C)

**`pg` native arrays** — **built** (2026-09-05, same day). Array results
parsed by element type into gBASIC arrays, nested arrays parsed not
flattened; array parameters rendered as literals or JSON by the type the
described statement wants. Suite uses Postgres as the oracle.

**`http`** — unchanged: `start`, `poll`, `read`, `wait`, `stop`, `release`,
mirroring `process` in shape, status record, and the no-framing rule. libcurl
multi interface.

**Event delivery for handles** — new, replacing the first draft's
`wait_any`. A handle is *waited* or *watched*, never both: `start` registers
nothing; a `watch` on the handle's event value adds it to the event loop's
existing `poll()` set, and `wait`/`stop`/`release` remove it. Readiness is
delivered into the watched value (`llm.events`, `process.events`, and the
pool's replies) the way requests reach `server.requests`. The event loop runs
after `main` while a live server *or* a watched handle exists, and exits when
neither does. `wait` inside a watcher warns as "loop after `serve`" does.
Handles expose `kind` and `id`.

**`rank`** — deferred. No consumer once ranking is a pgvector query (Part
3, item 10). Revisit with a second consumer in hand.

**`reflect.signature(fn) -> {name, params}`** — wanted, not blocking: the
name and parameter names of a function value. It is what lets
`tools.define` verify that a spec's `params` agree with the function they
describe, and it is general beyond AI.

**Server block** — no change needed: `emit` already returns `false` for a
gone client (`run_web_stream.sh`, `STREAM_DISCONNECT`).

### 4.2 Grammar

**`tools <name> ... end tools`** — **sugar, and conditional.** The library
form (`tools.define`, §4.3) is the design. This block may be added later
*only if* its shift/reduce conflict count is measured at zero — `tools` is a
parameter name in `llm.bas`, so it cannot be reserved and must be recognised
by position — and if the count is not zero, it is not added. It would lower
to exactly the record `tools.define` takes.

**`with principal <record> ... end with`** and `principal()` — the identity
scope. Same rule as `with lock`, one word over. Dynamically scoped; does not
cross `spawn` *implicitly* — a pool worker re-enters it explicitly from the
message it received, which is the designed handoff (Part 3, item 7). (Was
`context`; renamed because `reasoning` already defines a Context and the two
do not overlap in meaning.)

### 4.3 Stdlib

**`llm`** — `client(config)`, `start(client, request) -> handle`, `poll`,
`read -> events`, `wait`, `stop`, `release`; `embed(client, texts) ->
vectors`; canonical part-based messages with `raw`; `fingerprint(request)`;
replay mode on the client keyed by fingerprint plus occurrence, with declared
volatile paths.

**`agent`** — `new(ctx, system, toolset) -> run`,
`step(run, event) -> {run, actions}`. Pure. Run record serializable.

**`tools`** — `define(name, entries) -> toolset` (the design; entries are
records naming `fn` or `via`, `reads`/`mutates`, `describe`, `params`),
`schema(name)`, and `dispatch(name, args) -> tool_result_part`, validating
and error-capturing, **run in a pool worker**. The pool itself is
`tools.pool(toolset, {workers: n})`: pre-spawned actors, each holding its own
connections, receiving `{principal, run_id, call}` and replying with the
result part. Measured: 50–78 ms to spawn one, 0.015 ms per message once it
exists, so the pool is built at startup and never per call.

**`mcp`** — `connect(spec) -> handle`, `tools(handle)`, `call(handle, name,
args)`, `serve_stdio(toolset)`; HTTP transport via `server` block.

**`sse`** — `frame(buffer) -> {events, rest}`.

**Connector interface** — `enumerate`, `fetch`, `changed_since`, `acl_of`.
Interface only; the reference ships a directory-backed implementation as the
example.

### 4.4 Order, revised

1. `http` (six primitives) and **handle event delivery through the event
   loop** together — one platform phase. Measured smaller than it looked
   (item 12): the delivery is a module-owned global record plus a fixed-path
   `watcher_trigger_change`, so what is actually new is the libcurl multi
   interface, registering handle fds into the loop's existing `poll()` set,
   and the waited-or-watched rule.
2. `with principal`.
3. `tools.define`, `tools.schema` and the **worker pool** — the library
   form. (The grammar block only after a zero conflict count, and only as
   sugar.)
4. `llm` with the canonical message shape and keyed replay from day one;
   `run_tools` re-expressed over `agent.step`.
5. `agent.step`.
6. `mcp`, both transports, the stdio one shipped with `--line-buffered` in
   its launch configuration and a deadlock tier proving why.
7. Retrieval, as a pgvector query with the ACL predicate in it. (`rank` is
   deferred: no consumer.)
8. Steward itself, then `evalrun`.
9. ~~`pg` native arrays~~ — built before step 1 after all, because the
   database was provisioned and the control was in place; it took an
   afternoon. The order above is otherwise unchanged.

The reference is built *during* steps 4–8, not after. Each step's phase
document should include the Steward code that consumes it, on the same
standing rule that a platform capability is not done until Studio (here,
Steward) has been rebuilt on it.

And one rule this revision adds, because Part 3 is the evidence for it
twice over: **the first slice built is the one that touches Postgres**,
before any phase document is written. One of the findings above would have
stopped the reference on its first day and was invisible to a design that
was written rather than run — and the *correction* to it was wrong too until
it was run, reversing its own build-order recommendation. The automation
recipes learned the same thing three times over: a design that has not been
executed is a design that has not been checked, and neither has its review.

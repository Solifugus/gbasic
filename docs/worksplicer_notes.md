# WorkSplicer → gBASIC notes

Append-only. The WorkSplicer session records friction, gaps and asks here; this
session triages them. One entry per point: what was being built, what was
awkward or impossible, and a concrete suggestion.

**The rule that makes this work:** WorkSplicer does not fix gBASIC. It records.
A platform change made from inside an application is a change made to suit one
caller, and this tree has the machinery — gates, perturbation proofs, roster
tripwires — precisely so that platform changes are made once and made properly.

The same loop already runs for [gdash](gbasic_dogfood_notes.md) and
[tedderland](tedderland_dogfood_notes.md), and it has produced real work: the
`ldap` module, `web.configure`, `req.form`, the `chart` categorical-x defect,
`money.currency`, PLAT-NUL's whole sweep. **Three of those were reported as one
thing and measured as something worse**, which is the argument for recording
rather than guessing.

---

## What WorkSplicer is likely to need, predicted 2026-09-21

Written before the application exists, so that a prediction can be compared with
what actually happens. Recorded as *predictions*, not as commitments — several
of these will be wrong, and which ones is the useful information.

- **Presence** — who is in a workspace, what they are looking at. Probably an
  application concern over `pg` plus SSE, but if several applications want it,
  it is a library.
- **A control/lease primitive** — grant, share, revoke, expire, pre-emptive.
  `presence-and-control.md` §4 requires revocation to be *server-side and
  pre-emptive*, which is easy to implement wrongly in a way that passes testing
  and fails under latency. If that turns out to be subtle, it is platform work.
- **Optimistic concurrency helpers** — a write carrying the version it was based
  on, refused *with what changed*. `pg` has what is needed; the pattern may be
  worth a library.
- **Semantic description of a rendered view** (`ws.describe`) — the vocabulary a
  model reasons over. Almost certainly application-shaped, but if it is not, it
  belongs near `tools`.
- **Long-lived SSE under a worker pool** — `run_web_stream` covers parking and
  poking a stream. A workspace open all day, across a rolling reload, is a
  longer-lived case than anything measured.
- **Per-asset resource limits** — an asset type runs in a spawned worker;
  timeouts and memory ceilings are possible through `process`, and what the
  numbers should be is unmeasured.

**Prediction about the predictions:** the real asks will mostly not be on this
list. They rarely are.

---

## Entries

### 2026-09-22 — no PDF reader exists, and intake needs one

**What was being built:** document intake for WorkSplicer. Asked whether gBASIC
can extract text and images from a PDF.

**What is there:** `gpdf` **generates** PDFs — `document`, `text`, `table`,
`image`, `render`, `save`. Nothing in the tree **reads** one. No text
extraction, no image extraction, no page inventory.

**Why it matters here:** intake means invoices, statements and contracts, and
most arrive as PDF. Two different cases hide behind the word:

- **Born-digital** — the file already contains real text. Extraction is reading
  the content stream: exact, cheap, no OCR. This is most business documents.
- **Scanned** — a page image in a PDF wrapper. Needs rendering to a raster,
  then `ocr`.

`ocr_design.md` §7 already says a born-digital PDF should never go near OCR
because it has real text — but that only holds if something can get the text
out, and nothing can. Note that `tesseract` accepts PDF input directly and would
OCR **both** cases, which works and is wrong for the first: it guesses at text
the file states exactly.

**Suggestion:** a born-digital text extractor is bounded work (content streams,
a handful of font encodings) and is probably the whole of what intake needs.
A general reader — forms, annotations, embedded files, encryption — is not, and
should not be started on this ask.

**Not yet measured, and it decides the shape:** what proportion of real intake
is born-digital versus scanned. Worth counting before building either.

---

*(further entries below)*

---

## 2026-09-22 — the skeleton and the three structural assets

WorkSplicer's critical path steps 1–4 are built: server, session, sign-in, one
page; the six tables; Instructions, Log and Communication as real asset types
running in their own processes; and contract v1 frozen against them
(`worksplicer/docs/asset-context-v1.md`). About 3,400 lines of gBASIC. Eight
test suites, all green.

Seven entries follow. The interesting one is the first.

### WS-1 — `load` takes only a string literal, so a plugin host cannot exist

**Building:** `src/ws_asset_host.bas`, the child process that runs one asset
type. An asset type is "a gBASIC library" that a partner writes and a customer
installs (`asset-types.md` §2, §5, §6), so the host has to load a library whose
path it learns at run time — from a database row, from an install directory.

**What happened:** there is no form of `load` that does that. Measured:

```basic
path = "lib_a.bas"
load lib_a from path
' parse error at p2.bas:3:21: syntax error, unexpected IDENT, expecting STRING
```

**The workaround, and what it costs:** the child has a hand-written table, one
branch per known type, each with its own literal `load`. That is honest for
phase 1, which ships exactly three asset types. It makes the marketplace in
`asset-types.md` §5 **impossible** — not awkward, impossible. A customer cannot
install a third-party asset type without editing a file in the product.

**Suggestion:** a builtin that imports at run time and returns a namespace
value, e.g. `lib = import("/path/to/asset_x.bas")` then `lib.describe()`, with a
failure that is a VALUE (`{ok, message}` or `unknown`) rather than a raise,
since "the customer installed a broken plugin" is ordinary input and not a bug
in the caller. It would want the same refusals `load` already has — a duplicate
declared name, a missing file — reported rather than raised.

Worth saying what is NOT being asked for: not `eval`, not a search path, not
loading from a string. A path to a file, checked, returning a value.

### WS-2 — a request handler cannot see argv, and nothing can set an environment variable

**Building:** `--root <dir>`, which collapses every path WorkSplicer writes
under one directory. `main` parses it out of `args`; every handler needs it.

**What happened:** handlers cannot see argv (gdash found this too — its
`gdash_app` reads `GDASH_ROOT`), and `env(name)` **reads only**. There is no
`setenv`/`putenv`, so `main` cannot put what it parsed where its own handlers
can read it.

**The workaround:** `ws_server.bas` refuses to start when `--root` is given and
`WS_ROOT` is not, printing the exact variable to set. The operator now has to
say the same thing twice, and the failure mode if they do not — a server
writing to `/var/lib/worksplicer` while the operator believes it is writing to a
test root — is the kind that is discovered late.

**Suggestion:** either a `set_env(name, value)` builtin affecting this process
only, or — better, because it removes the environment from the path entirely —
make the server declaration carry application values through to handlers, e.g.
`web.configure(app, { workers: 1, locals: { root: p.root } })` readable as
`req.locals.root`. The second is narrower and has no ambient-state cost.

This is the second application to hit it, which is why it is repeated here
rather than left in gdash's file.

### WS-3 — no file-permission builtin (second application, same workaround)

**Building:** the server's CSRF secret, and every asset job file, both of which
hold content that must not be world-readable.

**What happened:** no `chmod`, so `ws_paths.restrict` shells out
(`process.run({command: "chmod", args: ["600", path]})`) — the identical
workaround, function name and all, that `gdash_paths.restrict` already uses.

**Suggestion:** `set_permissions(path, mode)` / `permissions(path)`, or a mode
option on `write`. Two applications independently writing a `restrict` helper
around `/bin/chmod` is the signal; the security-relevant version of the problem
is that the file exists world-readable for the window between `write` and the
`chmod` returning, which no caller can close from gBASIC.

### WS-4 — UNLEARN.md and reference.md disagree about reserved words, and UNLEARN is wrong

**Building:** `ws_db.presence_set(db, state, until, note)` — "out until 08:35".

**What happened:**

```
parse error at src/ws_db.bas:272:38: syntax error, unexpected UNTIL, expecting IDENT
```

`docs/ai/UNLEARN.md` says four keywords are usable as ordinary names — "**`end`
and `next`** ... `loop` ... `until`" (its Recently-fixed section and the
reserved-words bullet both say so). `docs/reference.md` §Reserved words says
**two**: `end` and `next`, with `until` in the reserved list and `loop` not a
keyword at all. Measured: reference.md is right.

**Suggestion:** fix UNLEARN.md. It is the file agents are told to read *first*
and *before writing any gBASIC*, so a wrong entry there costs more than the
same wrong entry anywhere else. A doc gate asserting that UNLEARN's list and
reference.md's list agree would keep them from drifting again.

### WS-5 — a trailing `+` does not continue a line, and SQL is where that hurts

**Building:** every query in `ws_db.bas`.

**What happened:** the rule is documented and the error is located, so this is
friction rather than a defect — but it was hit five times in one file, because a
long SQL string is exactly the place a trailing `+` is the natural reach. The
error says only `syntax error, unexpected NEWLINE`.

**The workaround:** `join(["...", "...", "..."], " ")`, which reads better than
the parenthesised form anyway.

**Suggestion:** when a statement ends at a newline and the last token is a
binary operator, say so — the way the money-literal error already does
(`'$' is not a money literal; write p{USD}= 19.99`). Something like *"a trailing
operator does not continue a line; parenthesise the expression, or join the
pieces"*. The diagnostic is cheap and the rule is one people meet exactly once
per language.

### WS-6 — `pg` bigint arriving as a string is right, and still needs saying at every column

**Building:** every timestamp in WorkSplicer, which is an epoch `bigint`.

**What happened:** documented behaviour, and the correct one — precision must
not be lost. But `number()` is needed on every read, and forgetting it does not
raise: `"1790122592" > 1790000000` raises, while `x = 0` against a string is
quietly `false`. The failure is a plausible value, not an error.

**The workaround:** one `_num()` helper and one row-shaping function per table,
so a raw column never escapes `ws_db.bas`.

**No suggestion, only a data point** — the mitigation is small and the
alternative (guessing when a bigint is safe as a double) is worse. Recording it
because it is the second-commonest thing that made this code wrong before a
test caught it.

### WS-7 — what actually worked, which the predictions asked about

The file's prediction list is above, dated 2026-09-21, with the note that "the
real asks will mostly not be on this list". Scoring it after phase-1 step 4:

- **Presence** — predicted as maybe a library. Not needed: two columns on a
  table and a record in `context`. Not platform work.
- **A control/lease primitive** — predicted as possibly subtle platform work.
  Not reached. `control.may_act` is computed per render and enforced
  server-side at the action; the state machine and revocation are phase 3 and
  nothing about them has needed the platform **yet**. The prediction is still
  open, not wrong.
- **Optimistic concurrency helpers** — predicted "maybe a library". Not needed:
  a per-task `seq` with a unique constraint, and a refusal carrying the events
  since. About twelve lines. Do not build a library for this.
- **Semantic description of a rendered view** — not reached.
- **Long-lived SSE under a worker pool** — not reached. Nothing pushes yet, so
  there is deliberately no `stream` route; the reasoning gdash wrote into its
  own server block (a parked stream belongs to the worker that accepted it) was
  read and believed rather than rediscovered.
- **Per-asset resource limits** — half-reached. `process.run`'s `timeout` is
  enough for time and the numbers are now measured (11–15 ms per structural
  render, whole child process; timeout set to 5 s). **Memory has no equivalent**
  — `process.run` takes no memory ceiling — so that half is real and unstarted.

So: one of six was needed as predicted, three were not needed at all, one is
untested, and one is half-missing. And the biggest ask of this phase — WS-1,
run-time `load` — **is not on the list**, which is what the prediction about
the predictions said would happen.

**`with principal` deserves a line of its own.** It did exactly what design §3
hoped: the identity is established once per request and read by the enforcement
layer, `principal()` answers `nothing` rather than an empty record so the gate
can refuse "nobody said", and it does not cross into the asset child — which
turned the process boundary into the authority boundary for free. Nothing was
asked of it that it did not already do.

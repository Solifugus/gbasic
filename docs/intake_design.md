# `intake` — what arrived, what was taken, what to keep

**Status: Partial.** **Increment 1 — `open`, `arrived`, `taken`, `close`, `seen`,
`entry_for` — shipped 2026-10-15** (`stdlib/intake.bas`,
`tests/run_intake.sh`); `archive` and `purge` (§6) do not exist, and §13's open
questions are open. Designed against measurements taken the same day, every one
of which is quoted where it is used and repeated in §12; where this says
*measured* a command was run and the number is the number it printed. Three of
those measurements **contradicted what this document first said**, and each
correction is marked where it sits rather than quietly edited.

`docs/etl_files_design.md` §4 sketched this library in twenty lines. This is the
design, and writing it changed four things in that sketch — including the sketch
itself, which **does not parse**.

---

## 1. What it is, and what it is not

Two items of the brief behind the ETL work are not file operations:

> *determine if i already imported them and if not, import them*
> *after processing a file, I need to archive it or what I derived from it with
> purge criteria*

Both need **state**. Everything else in that brief became a primitive —
streaming, the recursive glob, `stem` — because it was a thing the language
could not do. These two are things a *program* could always do and had to write
itself, every time, differently. So `intake` is a library over the primitives,
in pure gBASIC, and it owns exactly one idea: **a file that arrived, and what
has happened to it since.**

What it is not:

- **Not a scheduler.** It does not decide when to run. `timer` exists.
- **Not a transform framework.** It does not touch the contents of a file. The
  libraries that read files — `finio`, `ari`, `xlsx`/`grid`, `frame` — are
  already strong, which `etl_files_design.md` §1 says in as many words.
- **Not a queue.** No locking protocol between workers beyond "one run at a
  time", for a reason measured in §7.

## 2. The worked example, which has been run

`etl_files_design.md` §4's example is this, and **two of its lines do not
parse**:

```
for each f in intake.arrived(ledger, {dir}"/data", "**/*.xlsx", identity: "content")
intake.archive(f, to: {dir}"/data/archive", keep: 180 days)
```

**gBASIC has no named arguments.** Measured: `probe("x", identity: "content")`
is `syntax error, unexpected COLON, expecting RPAREN`. An options **record** is
the shape — `{ identity: "content" }` — which the whole stdlib already uses
(`make_dir(p, { parents: true })`, `webserver.listen(port, { address: ... })`).
That is the third example in that document found not to run, after the one that
iterated a listing record as a file and the one that called `modified` missing;
see §11 on why that keeps happening and what stops it.

The shape that does parse, and that increment 1 implements:

```basic
load intake

ledger = intake.open({file}"/data/.intake.json",
                     { identity: "content", ready: "renamed" })

for each f in intake.arrived(ledger, {dir}"/data", "**/*.xlsx")
    rows = read_vendor(f.path)
    ledger = intake.taken(ledger, f, { rows: count(rows) })
end for
```

Three things to read in it.

**`identity:` moved to `open`, not `arrived`.** It is a property of the
*ledger* — of what this feed means by "the same file" — and a ledger whose
identity rule could change between calls records two kinds of fact under one
roof, which is an idempotency bug that reads as a working program. Declared
once, at the only place that can refuse it, and stored *in the ledger file* so a
later run cannot silently disagree with an earlier one.

**`intake.taken` returns the ledger.** A gBASIC record is a **value**, so a
function cannot mutate its caller's copy — measured long ago and recorded in
`accounting.post`, `lending.apply` and `credit.apply`, which all return their new
state for the same reason. A mutate-in-place API here would silently do nothing,
and "nothing" means every file is re-imported on the next run.

**The loop body sees `f.path`, not `f`.** `arrived` yields the *listing record*
the glob produces (`{name, path, type, size, modified}`) with fields added, not
a file reference — because the fields are what an incremental load filters on.

## 3. The ledger is a value; the file is a snapshot plus a journal

### What it holds

```
{
  version:  1,
  identity: "content",
  taken: {
     "<identity>": { at: <epoch>, path: "<path>", relative: "<rel>",
                     size: <bytes>, modified: <epoch>, meta: { ... } }
  }
}
```

**Every time is an epoch number, not a `datetime`.** Not a preference:
measured, strict JSON — which is what `persist.write_atomic` emits and what
`encode` produces — **refuses both a `datetime` and a `duration`**
(*"encode supports numbers, strings, booleans, nothing, unknown, arrays, and
records"*). `agent` reached the same wall from the same direction and its run
stores `expires_at` as a number for the same reason. A ledger that cannot be
written is not a ledger, so the type goes and `epoch()` is the conversion. The
library hands back datetimes where a caller wants one.

### Why not one file

The obvious design is one JSON snapshot rewritten by `persist.write_atomic`
after every file. It is crash-safe, it is three lines, and **it is quadratic**.
Measured against a ledger already holding 50,000 entries — an ordinary state for
a feed that has run for a year:

| files taken in the run | snapshot rewritten each time | one journal line each time |
|---|---|---|
| 100 | **3.07 s** | 0.11 s |
| 200 | **5.92 s** | 0.11 s |
| 400 | **11.90 s** | 0.11 s |

The journal numbers are the interpreter's own start-up; the appends do not
register. The rewrite is linear in *files × ledger size* and wrote 1.3 GB to
take 400 files. So:

- **`<ledger>.json`** — the snapshot, written by `persist.write_atomic`, which
  is already atomic temp-then-rename and already reports missing/corrupt/loaded
  as a **value** rather than raising.
- **`<ledger>.journal`** — one `encode`d record per line, appended by `taken`.
  `append` is a core builtin and an append of a short line is the cheapest
  durable write available.
- **`open` reads the snapshot and replays the journal**, then compacts when the
  journal is worth compacting. Measured: a 9 MB, **100,000-line journal replayed
  onto a 50,000-entry snapshot in 0.51 s at 171 MB resident**, giving 150,000
  entries — and the replay is `for each line in journal`, so this library's
  recovery path is the streaming increment that shipped two commits before it.

**Durability is therefore per file, which is what §5's crash row requires.**

**It does not cost nothing, and this paragraph is a correction to the sentence
that stood here first.** The table above compares the two *write strategies in
isolation*, and the library pays one cost both of them share: a ledger is a
**value**, so `taken(led, …)` receives a copy and writing one field of a
20,000-field record detaches the whole field array. Isolated with no intake
involved — 120 field assignments *through a function* cost ~0s at 2,000 fields
and ~1s at 20,000 — so **the per-file cost is linear in the ledger whichever
way it is made durable.** The alternative that would make it flat, keeping the
map in library-level state, **does not exist in the language**: a variable
assigned in one library function is not visible from another (`undefined
variable: _state`, measured).

So the realised difference, measured through the actual library at a
20,000-entry ledger, is narrower than the table and still decisive: **322 ms by
journal against 2041 ms by snapshot rewrite, 6.3×** — and the rewrite writes
**266 MB** (120 copies of a 2.27 MB snapshot) where the journal writes about
**13 KB**. `tests/run_intake.sh`'s shape tier asserts that as a difference
between the two, implementing the rewrite in the tier as a second
implementation, because the first two drafts of it each asserted something that
turned out to be false — first that the per-run cost was flat (it is not;
`open` and `close` are legitimately linear), then that the per-file cost was
(it is not either, for the reason above). The snapshot exists only to keep the
journal short.

### How big it gets, and why `purge` is not only about disk

Measured, opening a ledger costs about **1 MB resident per 1,000 entries** and
scales linearly: 50,000 entries is a 6.6 MB file, **0.12 s and 64 MB** to open,
with lookups flat inside it because a record above a size threshold carries a
hash index (PLAT-RECIDX). Extrapolated, a million entries is ~1 GB and ~2.4 s
*per run, before any work*.

So the ledger has a working size, and the thing that keeps it inside it is
`purge`. That is an argument the sketch did not contain: purge's first job is
retention of **files**, and its second is keeping the **ledger openable**.

## 4. `identity:` is required, and each mode has a measured price

It declares what makes two files the same file. Guessing re-imports silently or
skips silently, and in ETL both look exactly like success — so it follows the
rule `ldap`'s `security` and `lending`'s accrual basis already follow: **the
argument with consequences may not be defaulted.**

| mode | what it compares | cost, measured |
|---|---|---|
| `"path"` | the path alone | nothing; no read, no stat |
| `"stamp"` | path + size + mtime | nothing; the glob **already** carries both as fields |
| `"content"` | `sha256` of the bytes | **loads the whole file**: 200 MB costs **403 MB resident** and 0.27 s |

Each is correct for a different source and wrong for the others: `"path"` for a
source that never reuses a name, `"stamp"` for a file too large to hash,
`"content"` for a vendor who re-drops the same filename with new contents.

**`"stamp"` is free, and that is new information.** The glob's entry record
carries `size` and `modified` already — added 2026-10-15 for exactly this kind
of filter — so stamp identity needs no call per candidate at all. The sketch
described it as "cheap"; it is *free*.

**`"content"` does not scale, and the design says so rather than discovering
it.** `sha256` takes a string, so hashing a file means `sha256(read(f))`, which
is the whole-file read the streaming work exists to avoid: measured at **just
over 2× the file in resident memory**, so a 10 GB extract needs ~20 GB of RAM.
`"content"` is for files that fit, and `intake.open` **refuses it with that
sentence** when the file it is about to hash is larger than a declared bound
(`max_hash:`, default 256 MB) rather than inviting the host's OOM killer.

**A bounded prefix hash was considered and is not expressible.** The obvious
repair — hash the first 64 KB plus the size — needs a bounded read, and the only
bounded reader in the language is line-oriented: measured, taking 64 KB of a
200 MB binary file costs 14.5 MB resident and 0.01 s, but a 50 MB file **with no
newline byte in it yields one 50 MB "line"** at 110 MB resident, because the
`break` happens after the line is materialised. So the bound is the first
newline, not the byte count the author asked for. Filed as a platform gap
(a bounded `read(f, n)`, or an incremental digest); until one exists there are
three identity modes and not four.

**And `"content"` may be unavailable at all.** `sha256` is a builtin behind
libcrypto, so `intake` joins the `crypto`/`otp`/`gpdf` category that
`run_library_tiers.sh` names — a library that *loads* on a lean build and whose
functions then fail. The remedy is this library's own: `open` asks
`has_builtin("sha256")` and refuses `identity: "content"` **at open time**,
naming the build, rather than failing at the first file hours into a run.

## 5. Readiness: a declared rule, because the obvious one is a race

This was `etl_files_design.md` §5's only row with no precedent in the tree, and
it is settled by measurement rather than by choice. Against a writer appending
207 bytes every 250 ms, with a reader sampling `file_size` and `file_mtime` every
200 ms, **the size was unchanged between consecutive samples three times while
the file was still being written** — and second-resolution `mtime` cannot break
the tie, four consecutive samples carrying one value. A reader applying
"unchanged since the last look, so take it" would have taken a 40%-complete file
and reported a successful load.

So a stability window is not a check, it is a race, and `ready:` is **declared**
on `open` with no default that guesses:

| `ready:` | the rule | sound? |
|---|---|---|
| `"sentinel"` | take `x.csv` only when `x.csv.done` exists | yes |
| `"renamed"` | appearing *is* the signal; the writer built it elsewhere | yes |
| `{ stable_for: 30 seconds }` | unchanged for that long | **a probability, not a guarantee** |

`"renamed"` needs nothing from this library but a sentence: the writer's half is
`atomic_replace`, which already exists, and a rename into place is atomic, so a
file that is visible is whole. `"sentinel"` is one `write` on the writer's side
and one `exists` on ours. `{ stable_for: ... }` is the fallback for a
counterparty who will not cooperate — a real situation, an SFTP drop — and it is
offered **with its race named in the reference**, because a window must be
longer than the writer's longest pause and the reader cannot know that.

A fourth is deliberately not offered. **Detecting an open writer without any
cooperation** would be the sound answer, and the mechanism exists and cannot be
used: an advisory lock really is visible across processes — measured, with the
holder gate-file synchronised, a reader's `lock(f)` **blocked for 2.91 seconds**
and acquired only on release — but `lock` has **no non-blocking form**, so
asking the question means committing to wait for the answer, and a scan of a
thousand dropped files would stall on the first one still arriving. Filed.
Reading `/proc/*/fd` is the other route and is not portable, needs the writer to
be the same user, and would be the only thing here that cannot work on Windows.

## 6. `archive` and `purge` — increment 2, designed here

<!--fragment: increment 2; these two functions do not exist yet-->
```basic
ledger = intake.archive(ledger, f, { to: {dir}"/data/archive", keep: 180 days })
report = intake.purge(ledger, { dry_run: true })
```

**`keep: 180 days` is native syntax, and that is measured**: a duration literal
is a legal record option value, `type()` answers `duration`, and `now() - keep`
is a `datetime`. So retention is unit-safe in a way `days_retention=180` is
not — there is no unit to misread and no arithmetic to get wrong. It is stored
in the ledger as a number of seconds, because strict JSON refuses a duration
(§3), and converted back on the way out.

**Archive mirrors the source tree, using a field the platform does not have.**
`DOGFOOD.md` item 6 filed that a listing record has no path *relative* to the
directory listed — `name` collides across subdirectories (`2026/Q1/vendor.xlsx`
and `2026/Q2/vendor.xlsx` are both `vendor.xlsx`) and `path` is absolute — and
left it until a consumer existed. This is the consumer, and **the answer is that
the field belongs here and not in the platform**: the library knows the root it
was given, and `directory_name(join_path(root, "x"))` normalises any spelling of
it — measured over `/data`, `/data/`, `/data//`, `.`, `/` and a `{dir}`
reference. `arrived` therefore attaches `relative`, derived once, and the
platform record stays as it is. A field added to the listing would be a guess at
what a consumer wants; this is the consumer, and what it wants is the path under
the root *including* the filename.

**`purge` reports by default and deletes when told.** Deleting data is the one
operation here that cannot be undone, so `dry_run` defaults to **true** and the
answer is a record naming what would go. Two rules it may not break:

- nothing is deleted that the ledger does not record as **archived** — the
  site's `pinned:` rule, *"a published release a book is based on is never
  removed"*, in another costume;
- the ledger entry outlives the file it describes, marked `purged`, because an
  entry removed with its file makes the file **newly arrived** the next time a
  counterparty re-drops it.

That second rule is why purge shrinks the ledger less than it shrinks the disk,
and it bounds how much: a purged entry keeps its identity and drops its `meta`.

## 7. Two loads at once

§5's last row asks that "one waits or refuses". Measured, **it waits and there
is no refuse**: `lock` blocks with no non-blocking form. So increment 1 takes
the lock around the ledger's read-modify-write and documents the consequence —
a second run started while the first is working **waits for it**, and a run that
dies without releasing is survivable because `flock` is released by the kernel
when the process dies (which `run_lock_signal.sh` already measured for its own
purposes).

What this means for the design: **`intake` is one run at a time, by
construction, and says so.** A queue of workers sharing one ledger is not
expressible without the non-blocking lock, and pretending otherwise would be a
protocol that deadlocks under load.

## 8. Robustness is the test plan

`etl_files_design.md` §5's nine rows, with what each is now known to need:

| failure | required behaviour | how, measured |
|---|---|---|
| a file is still being **written** | not imported | §5's declared `ready:`; the window is a probability and says so |
| the run **crashes** mid-import | the ledger says what completed | per-file journal append (§3), replayed on open |
| the same file is **re-dropped** | idempotent under the declared identity | §4, asserted as a **difference** between `"path"` and `"content"` |
| a file **vanishes mid-scan** | the walk continues and reports it | measured: `file_size`/`file_mtime`/`read` all **raise** with a named reason, while `exists` answers `false` and `file_type` answers `unknown` — so the check is a **value**, not a caught raise |
| a folder is **unreadable** | not an empty folder | the glob warns (2113) and continues; fixed 2026-10-13 |
| a stream is **truncated** mid-read | raise | the streaming loop already raises rather than reporting EOF |
| **archive** fails halfway | the original is never lost before the copy is durable | copy, verify, then delete — never `move` first |
| **purge** runs | nothing goes that is not recorded archived | §6 |
| two loads start **at once** | one waits | §7, and "or refuses" is not available |

Every one of those is a tier in `tests/run_intake.sh`, and the suite is
**self-checking rather than golden**, forced: every defect this library can have
produces **an ordinary-looking successful run**. A ledger that silently records
nothing, an identity that always matches, a readiness check that always says
yes — each prints the same transcript as a working import, and a golden would
record one as expected and defend it. The two load-bearing tiers:

- **IDENTITY is a difference.** The same filename with new contents: `"path"`
  must skip and `"content"` must take. Either alone passes on a ledger that
  always answers one way.
- **CRASH is a second process.** The run is killed with `SIGKILL` mid-import —
  not a caught error, which proves nothing about a half-written file — and a
  second `open` must report exactly the files that completed. Durability is a
  claim about what survives a death, so the test has to perform one.

## 9. Refusals, each beside its nearest legal neighbour

A refusal suite with no controls is satisfied by a library that refuses
everything, so every one of these ships with the call that must still work:

| refused | because | control |
|---|---|---|
| `open` with no `identity:` | §4 | a declared one is accepted |
| an unknown `identity:` value | a typo must not silently become a default | all three valid ones accepted |
| `identity: "content"` without libcrypto | §4; refuse at open, not at the first file | accepted where `has_builtin("sha256")` |
| an unknown option field, **by name** | the rule `webserver.listen` follows | a well-formed options record |
| `open` on a ledger whose stored `identity` differs from the declared one | two kinds of fact under one roof (§2) | re-opening with the same one |
| `open` on a ledger of a **later `version`** | a newer intake wrote it; guessing at a format is how a ledger gets corrupted | the same and older versions |
| `taken` for a file `arrived` did not yield | the identity would have to be recomputed, and `taken` is where an unhashed file would silently become one | an entry straight from `arrived` |
| `purge` with no `keep:` anywhere | "delete everything older than nothing" | a declared retention |
| a corrupt ledger | `persist.read_status` reports it as a **value**; intake refuses to start rather than rebuild, because a rebuilt ledger re-imports the year | the caller may pass `{ rebuild: true }` and say so |

## 10. Increment 1 is `open`, `arrived`, `taken`

Deliberately not everything:

- **In:** the ledger (snapshot + journal + compaction), all three identity modes,
  `ready:`, the lock, and the refusals above that apply to them.
- **Out, to increment 2:** `archive` and `purge`. `copy`, `move` and `delete`
  already work unaided, so the value there is the retention rule and the
  never-lose-the-original ordering — and `purge` **deletes**, which earns its own
  suite and its own dry-run decision rather than riding in on another increment.

The split is where the value is: *don't re-import* has no substitute in the
language, and *move a file afterwards* has three.

## 11. Why three examples in the parent document did not run

This is the fourth correction to `etl_files_design.md` in a fortnight, and they
are all one shape: a design document's code is a **claim about the tree**, and
`run_doc_examples.sh` ran every ```basic block in exactly four pages —
`tutorial.md`, `README.md`, `reference.md` and `webclient_design.md`. No design
doc was among them, which is precisely why §4's example named arguments the
language does not have, §3's iterated a record as a file, and §1 called two
shipped builtins missing.

So **this page is wired into `run_doc_examples.sh`** (`:parse`, the mode the
reference uses, since a worked ETL loop names functions of the reader's own that
no page can supply), and §6's block carries the `fragment:` marker that suite
requires for code describing something not yet built.

**Wiring it in immediately had work to do**, which is the argument rather than a
footnote: §2's example was written before `ready:` was made required two hours
later, so by the time increment 1 shipped the worked example in this document
*would have been refused* — `intake.open: ready: is required, with no default`.
That is the fourth instance of the defect this section is about, inside the
section about it, found by the mechanism this section asked for.

## 12. The measurements

All 2026-10-15, on this machine, with the command's own output quoted.

| # | claim | measured |
|---|---|---|
| 1 | `keep: 180 days` is a legal option value | `type()` answers `duration`; `now() - keep` answers `datetime` |
| 2 | strict JSON refuses typed times | `encode({seen: now()})` and `encode({keep: 180 days})` both refuse; numbers accepted |
| 3 | content identity loads the file | 200 MB file → **403 MB resident**, 0.27 s |
| 4 | a bounded prefix read is not expressible | 64 KB of a 200 MB binary: 14.5 MB / 0.01 s; a 50 MB file with no newline: **one 50 MB line**, 110 MB resident |
| 5 | a vanished file is a value question, not a raise question | `file_size`/`file_mtime`/`read` raise, named; `exists` → `false`; `file_type` → `unknown` |
| 6 | ledger open cost | 1,000 → 0.04 s / 15 MB; 10,000 → 0.06 s / 24 MB; 50,000 (6.6 MB) → **0.12 s / 64 MB**; lookups flat |
| 7 | journal beats snapshot rewrite, as WRITE STRATEGIES | 100 files 3.07 s vs 0.11 s; 200 → 5.92 vs 0.11; 400 → **11.90 vs 0.11** (a 50,000-entry ledger) |
| 7b | and through the real library, where both pay the record copy | 120 files into 20,000 entries: **322 ms journal / 2041 ms rewrite, 6.3×**; 13 KB written against 266 MB |
| 7c | why the per-file cost is linear either way | 120 field assignments through a function: ~0 s at 2,000 fields, ~1 s at 20,000 — a record is a value, so the write detaches the array |
| 7d | and why it cannot be made flat here | library-level state does not exist: a variable assigned in one library function is `undefined variable` in another |
| 8 | journal replay scales | 100,000 lines (9 MB) onto 50,000 entries → **0.51 s / 171 MB**, 150,000 entries |
| 9 | named arguments do not exist | `probe("x", identity: "content")` → `syntax error, unexpected COLON, expecting RPAREN` |
| 10 | the library can normalise any root | `directory_name(join_path(root, "x"))` over `/data`, `/data/`, `/data//`, `.`, `/` and a `{dir}` reference |
| 11 | a stability window is a race | size unchanged across consecutive samples **3 times** mid-write; 4 samples share one second-resolution mtime |
| 12 | a writer's lock is visible and blocking | reader blocked **2.91 s** against a 3.0 s holder; no non-blocking form |
| 13 | every planned function name is legal | `open`, `arrived`, `taken`, `archive`, `purge`, `ready` all resolve in a library; none collides with any stdlib name |

## 13. Open questions

- **The name.** `intake` describes arrival, not archival or purge, and §7 of the
  parent document listed `ingest`, `pipeline` and `etl` as candidates. Settled
  here as **`intake`**, on the grounds that archive and purge are the *afterwards
  of an intake* rather than separate subjects; `ingest` is a synonym with no
  advantage, and `pipeline` and `etl` both claim far more than this does. Noted
  rather than closed, since the second increment is the one that would feel the
  strain.
- **Compaction policy.** The journal is compacted into the snapshot on `open`,
  and *when* is unmeasured: a line count, a byte size, or a ratio to the
  snapshot. Increment 1 will pick one and record the number it was chosen from.
- **Does `arrived` hash eagerly?** Under `identity: "content"` it must hash to
  answer at all, so a scan of a thousand new 200 MB files pays 400 GB of reads
  before the loop body runs once. A lazy identity would need a handle the record
  cannot hold. Increment 1 hashes eagerly and **says so in the reference**; the
  remedy a caller has today is `"stamp"`.

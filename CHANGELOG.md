# Changelog

All notable changes to gBASIC are recorded here.

This project uses [semantic versioning](https://semver.org/). Until 1.0.0 the
language surface may still change between releases.

---

## Unreleased

### Fixed — a watched transfer was cancelled when the program rebound its variable

Dropping the last reference to an `http` handle cancels the transfer. In
*waited* mode that is right — nobody could read the answer again. In *watched*
mode it is wrong: the event loop will deliver the completion and the event
carries the handle back, so the loop is an interested party.

Without a reference of its own, the natural deferred handler lost transfers
**silently**. `h = http.start(...)` inside a request watcher rebinds one global,
so the second request cancelled the first. Measured: two concurrent requests,
both handlers ran, **one answer ever arrived**, and nothing was reported. The
loop now takes a reference when a watcher on `http.events` is live and releases
it once `done` has been delivered.

### Fixed — `unique` had its own definition of equality

`unique([0, false])` returned **both** elements, and `contains` then reported
each of them present: an array `unique` had just called duplicate-free holding
what `contains` calls a duplicate. It had a separate switch requiring both kinds
to match, so it disagreed with every other route on the language's one real
coercion. What counts as a duplicate is not `unique`'s to decide differently
from `=`; it delegates now.

Found by a sweep after the third PLAT-EQ fix, on the standing rule that a defect
found in one place is evidence about every place that does the same thing.
`tests/equality_routes_test.bas` (95 checks) now asks the same question through
`=`, `contains`, `find`, `remove_value`, `consider` and `unique` and requires one
answer, with the expected answer stated by the fixture so it cannot pass by
having every route agree on the wrong one.

**And one claim was too strong.** `CLAUDE.md` said the operator and change
detection "cannot disagree". They must, on number-versus-boolean: `1 = true`,
yet a variable holding `1` that is assigned `true` **has** changed, and a silent
watcher would leave the program holding a boolean while reporting nothing. The
watcher answers a different question. Corrected in place, with the one point of
divergence and a control asserted.

### Added — `http`: requests that do not block the program

`webclient` performs one request and returns when it has finished. `http` is
the same request over libcurl's **multi** interface, so a request is a handle
that makes progress while the program does something else — the six verbs
`process` gives a child, meaning the same things: `start`, `poll`, `read`,
`wait`, `stop`, `release`. Measured, four 400 ms requests take **515 ms
together against 1,717 ms one at a time**.

**Transport is not HTTP, and the field names say so.** `transport_ok` says the
bytes arrived; `status` says what the server thought of the request. A 500 is a
transfer that **succeeded** and carries status 500; a refused connection has no
status at all. A field called `success` beside a status code gets read as "the
request worked", once, quietly — so the suite asserts the pair as a difference,
and a perturbation conflating them is caught by exactly one check.

**Readiness is delivered, not waited on.** A handle is *waited* or *watched*,
never both: `start` registers nothing, a watcher on `http.events` puts libcurl's
descriptors into the event loop's existing `poll()` — the same one the webserver
runs — and `wait` *claims* a handle so the loop does not also report it. The
loop now runs after `main` while there is anything to deliver, a live server
**or** a watched handle, where a program holding only handles used to exit. An
event carries the handle itself, so a watcher needs no table to map an id back
to a transfer.

`timeout` has no default, unlike `webclient`'s 30 seconds: a handle is the shape
a stream arrives in, and a stream meant to stay open must not be killed by a
default nobody wrote. A 30-second connect timeout always applies.

### Fixed — a raise inside a watcher was reported nowhere

A `watch(server.requests)` body that raised ended the run with **exit 1 and
nothing on stderr** — no message, no location, no way to tell a failed program
from a finished one. The fatal report runs when `main` returns, and a watcher
fires after that. Pre-existing since the watcher path was written; found
building the second caller of it. Both queues are asserted, because a fix proven
only on the new one would say nothing about the old.

### Fixed — every value kind without a comparison branch was equal to every other

```
{file}"/etc/hostname" = {file}"/etc/passwd"   ->  TRUE
{dir}"/etc" = {dir}"/tmp"                     ->  TRUE
two separately started child processes        ->  TRUE
a file = a directory = the number 0           ->  TRUE
a file > a directory                          ->  ANSWERED
```

PLAT-EQ routed arrays and records away from `eval_comparison`'s numeric
fallthrough in August; the scalar half routed strings away from it the day
before this. **Neither closed the door.** It stayed an open catch-all, and
`value_number_or_zero` returns 0.0 for every kind that is not a number or a
boolean — so every kind with no branch of its own was still inside, `file` and
`dir` among them, which are ordinary program values and not exotic handles.

**Found by adding a thirteenth kind.** The new `http` module's own fixture
asserts that two transfers are two handles, and it failed. That is the argument
for closing the fallthrough rather than adding a fourteenth branch: a new value
kind opted into the coercion silently, and did.

**Measured before changing it**, whole gate instrumented: exactly **eight**
comparisons reach the fallthrough with a non-numeric kind — 4 http, 3 workbook,
1 actor — and all eight are identity comparisons that `value_storage_equal`,
the function watchers already use to decide whether a value changed, answers
correctly. Nothing depended on the coercion, which is why it survived. Equality
now routes there and ordering refuses, naming both kinds.
`0 = false` is unchanged at 1,472 measured uses, and is the control.

### Fixed — a string is never equal to a number, and ordering across kinds refuses

`0 = "stop"` was **true**. So were `0 = ""` and `0 = "abc"`, while `1 = "1"`
was false and `1 > "stop"` *answered*. Every string became `0` at the end of
`eval_comparison`'s chain, where `value_number_or_zero` returns 0.0 for any
kind that is not a number or a boolean — so only the number zero equalled a
string, which is not a coercion anyone designed.

**It was a missing case in a uniform chain.** Every rich kind has two
branches: `array && array` beside `array || array`, and the same for record,
function, watcher, regex, gobject, gboxed, money, datetime, duration and
nothing/unknown. Eleven pairs. String had the `both` branch and no `mixed`
one. The fix is the twelfth, written in the shape of the other eleven:
equality answers, ordering raises naming both kinds. This is PLAT-EQ's defect
exactly — it routed compounds away from that fallthrough in August and left
the scalars in it.

**Measured before fixing**, the whole gate instrumented: 1,500 mixed-kind
comparisons reach the fallthrough. **1,472 are number-versus-boolean** — a
real coercion (`0 = false`), kept unchanged, and now the control that catches
an over-correction. The other **28 are number-versus-string at two sites, and
one was already wrong**: `run_xlsx.sh` counts corrupted cells with
`c.value = "#VALUE!"`, and a cell holding the number `0` compared equal to that
string and would have been counted as corrupt. It passes today only because no
cell in the fixture is zero.

Found measuring an actor pool for the AI reference proposal: a worker whose
stop sentinel was `"stop"` exited on the message `0`, and the parent hung in
`receive()` forever. The shape to watch for is `x = 0` as a test on something
that might be text — `if input("n: ") = 0` was true for any non-numeric answer.

`tests/equality_scalar_test.bas`, 26 checks, in PLAT-EQ's suite. Three
perturbations red: the fix reverted, the fix over-applied to number/boolean,
and the ordering refusal dropped. No golden moved.

### Added — `pg` reads and writes native Postgres arrays

A result column of any built-in array type arrives as a gBASIC array —
twenty-two types, each element converted by its own type under the bare
column's rules (`int8[]` and `numeric[]` elements are strings for exactness,
`NULL` is `nothing`, a 2-D array is nested arrays). An array parameter is
rendered as a Postgres literal when the statement wants an array and as JSON
when it wants JSON.

**The statement decides, because nothing else can.** `[["staff","lending"]]`
is `{"staff","lending"}` for `acl && $1` and `["staff","lending"]` for
`$1::jsonb`, and the wire text differs. So a call that passes an array
prepares and *describes* the statement first, and each array parameter is
rendered by the type Postgres inferred for its position. Every call that
worked before still works — `jsonb` columns still get JSON, an untyped `$1`
is still JSON text, records are always JSON — and the array contexts start
working, with no syntax for the caller to restate a type already written
into the SQL. Cost: 1.3× a trivial local query, only when an array is passed.

`tests/postgres_arrays.bas`, 50 checks, **with Postgres as the oracle**: an
array parameter is read back through `array_length`/`unnest`/`array_dims`, so
what is asserted is what the server parsed rather than what our reader makes
of our writer — a round trip alone passes on a matched pair of bugs. Proven
red on the pair that matters: with the backslash unescaped, `back\slash`
arrives as `backslash`, a plausible string that only the oracle and the round
trip can see. Also proven red on arrays-as-JSON-regardless (the old
behaviour) and on a bare `NULL` element read as text. Valgrind clean.

This is the item the entry below documented as a limitation, on the same
afternoon: the negative control in that suite went red the moment arrays
landed, which is how the reference paragraph got rewritten instead of
rotting. The AI reference proposal's retrieval query, `acl && $1` over a
`text[]`, now runs verbatim.

### Documented — what `pg` does with a set-valued column, measured

Postgres's native array types are **unsupported in both directions**: a
`text[]` or `float8[]` result column raises, and an array parameter is sent as
JSON text, which Postgres refuses as `malformed array literal`. This was true
since the module was written and appeared in no document and no suite. It is
in `docs/reference.md` now, beside the alternative that works on the module as
it is: a `jsonb` column takes the JSON a parameter already is ("any of these"
is a join over `jsonb_array_elements_text`), and a pgvector column arrives as
text that `decode` reads and `encode` sends back for a `<->` search.

`tests/postgres_arrays.bas` holds the reference to that, and its two "raises"
checks are a **negative control** that goes red when native arrays are
implemented — the signal to rewrite the paragraph, not the test.

Found checking the AI reference proposal
(`docs/gbasic_ai_reference_and_primitives.md`) against the tree, and worth
recording for how it went: the first reading concluded native arrays blocked
the design and put them first in the build order; running the workaround
against a real PostgreSQL 17.10 reversed that within the hour. The design's
retrieval query fails as sketched and runs with a `jsonb`/`vector` schema —
which is the schema a pgvector-backed store would use anyway.

`tools/setup_postgres_dev.sh` provisions the role and two databases for the
opt-in suites (peer auth; no `pg_hba.conf` edits; idempotent). Running them for
the first time on this machine found `run_gbasic_site_postgres.sh` **red since
PLAT-WARN shipped**: `record_post_event` returned a boolean nobody read, and
the unused-result warning failed the suite's clean-stderr check. It returns
`nothing` now. Both opt-in suites pass.

### Changed — `load` is a declaration, and the warning about it is gone

Write `load` at the top level or inside the `program` block; both are registered
before anything runs. Until now a top-level `load` never ran when a program block
existed — `load` was an executable statement and those statements are not walked
— and it warned so.

**The warning was true about the parent process and blind to a spawned actor.**
An actor is fork+exec: the child re-parses the source and runs the entry
function, never entering the `program` block. Parent and child ran *separate*
registration passes over *different* sets, and their blind spots were exactly
complementary:

| | top-level `load` | `load` inside `program` |
|---|---|---|
| parent | never ran → **warned** | ran, as a statement |
| child | ran | **never seen** |

So whichever position an author picked, one of the two processes was missing the
import — and following the warning's advice ("move it inside `program`") made the
worker die on its first qualified call while the parent waited in `receive()`.
The symptom was a **hang**, pointing nowhere near the `load`.

They now run **one shared pass**, which is the actual fix; making `load`
position-blind falls out of it. `tests/run_pre_registration.sh` pins that they
share it, structurally — the property is *there is only one pass*, and no single
program can demonstrate that, since a behavioural test proves only that two
passes agree about the case it exercises.

Three measured things make hoisting safe rather than a guess. Importing runs no
user code: `library_import_from_block` handles `USE`, `FUNCTION` and `MODIFIER`
and ignores every other statement kind, so a `library` block registers names and
executes nothing. Hoisting is idempotent: a program-body `load` is hoisted *and*
still reached as a statement, and the second import returns early at the
`used_pairs` guard. And it is **direct children only** — `load` is an ordinary
statement, so `if false then load nosuchlib` parses today
(`examples/inline_if_test.bas`) and must keep importing nothing.

One behaviour change worth naming: a top-level `load pg` on a build without
libpq used to be dead and now raises at startup. That is fail-fast rather than
`invalid function call` later, but it is a change.

The pre-registration set gains `AST_STMT_USE`, so **gBASIC Studio's STU-4B
declaration-hoisting rule must gain `load`** — the tripwire says so by name.

Four perturbations proven red. The fourth is the one worth recording: the first
version of the actor fixture used `alias_host` and `alias_dep`, and `alias_host`
*loads* `alias_dep` — so the child received the block-position library
transitively, removing that hoist changed nothing, and the tier passed against a
deliberately broken build. Each position now has a library nothing else reaches.
Red-proofing also found that the warning-model tier failed *silently* under
`set -e`, since the fixture exits nonzero when it regresses; it names itself now.

### Changed — documentation: gBASIC is no longer described as experimental

The word carried an implication the tree stopped earning: 107 test suites, 40
standard libraries, thirteen cookbooks whose every code and output block is
executed and compared byte for byte, and an `xlsx` engine measured against
15,871 real workbooks. `0.1.0` is **early** and pre-1.0 — the surface may still
move — and that is what the README, tutorial and reference now say.

**Two things keep the word, and now keep it alone**: the
business-automation-reasoning libraries (`reasoning`, `insight`, `decision`,
`automation`), which are a design laboratory whose every measurement is on
generated data, and the GTK 3 `gui` module, a proof of concept superseded by
`gi`.

The sweep also found rot the existing tripwires could not see and closed it:

- The README claimed `run_all.sh` discovers **75 suites** against a tree holding
  **107** — understating the gate by a third. The roster tripwire beside it
  reads only the standard-library section, so nothing could see it.
- Six of the thirteen cookbooks — money, finance, accounting, lending, deposits
  and credit — were named in the standard-library section and **missing from
  the documentation section**, which is where a reader actually looks.
- `run_docs_gate.sh` now counts both from the tree: the suite count and the
  cookbook list, each red-proofed.
- `docs/reference.md` gained the fields a caller could not find: `grid.extract`'s
  whole spec (`header_rows`, `drop_totals`, `drop_matching`, `starts`/`ends`
  and the rest), `insight`'s `max_causes`, `repetitions`, `versus_last_year`
  and `search.detectable`, and the self-describing `agreement_is`,
  `assurance_is` and `amplification_is`.

### Added — `load NAME as ALIAS`

A file may choose the name it calls a library by:

```basic
load statistics_helpers as st
print(st.zscore(2, 1, 1))
```

The alias works wherever the library's own name would — qualified calls,
qualified modifiers, function values — and both spellings of `load` take one
(`load toolkit from "vendor/a/toolkit.bas" as tools_a`).

**The case it exists for** is two libraries whose own declared name is the same.
Before this they could not coexist in one program: the second import failed with
*"function 'describe' is defined twice in library 'toolkit'"* — a message that
names a function neither file defined twice and never mentions that there are
two files. The loading file had nothing it could say about it even when it had
named both by full path.

An alias is an **import identity**, not a lookup-time rename: registration keys
on the effective name, which is what lets one declared name arrive from two
paths at once. A scoped rename would have bought the typing convenience and left
the collision exactly where it was.

**An alias replaces the declared name rather than adding to it.** Two names for
one import is the ambiguity aliasing removes. A call to the replaced name says
which name the library *was* loaded under, since a rename that missed a call
site is the mistake this makes easiest to write. (The alias adds no second name
of its own; a library name is available when *something* loaded the library
under it, so if another library loads `matrix` plainly while your file loads it
`as m`, both resolve. Refusing that would let any library decide which names its
consumers may pick.)

**A library does not know it was renamed** — its own unqualified calls still
reach its own functions, and the libraries *it* loads keep their own names.

**Collisions are refused.** Effective library names are a flat namespace: an
alias may not be another loaded library's name, another alias, or a built-in
module (`sqlite`, `pg`, `gi`, …), and a built-in module may not be aliased,
since those qualifiers are recognized before user functions are resolved. Two
libraries claiming one declared name are now refused by **one message that names
both files and spells the `as` that resolves it**, in place of the duplicate-
function error that used to surface instead.

`tests/run_alias.sh`, four perturbations proven red.

### Changed — a function from another library must now be qualified

An unqualified name resolves in exactly two places: **the library whose code is
running**, and **the root program's own functions**. `load stats` followed by
`ols(...)` is now an error.

The error is written to be the edit — *`invalid function call: ols — library
'stats' defines it, and a function from another library must be qualified.
Write stats.ols(...)`* — and where more than one loaded library defines the
name it offers each rather than choosing.

**What it replaced** was a backward scan over every imported function, newest
registration first. It was a real convenience, and it decided *which library a
call meant* by **load order** — something the author of the call neither
controls nor can see — so adding a `load` at the top of a file could silently
move a call in the middle of it.

Measured before removing it, by instrumenting the resolver and running the whole
suite: 4,311 resolutions went through that scan; **3,931 were one library
reaching another** (`stats.bas` → `matrix`, five names); and of the 177 distinct
`library.function` pairs root programs reached, **exactly one name was defined
by more than one library**. The scan was almost never choosing between
candidates — it was resolving a unique name by a rule that could have chosen
wrongly.

**A library still calls its own functions with no prefix**, and that now holds
in every shape a library takes: in its own file, declared in the *same file* as
the program that loads it, and inside a `modifier` body. The same-file case
**never worked** — the own-first rule was keyed on the source path and a
same-file library has none — and nobody could tell, because the cross-library
scan caught the call on the way past.

Two other things fell out. The `x(mod) = v` clause ambiguity documented in
`docs/ai/UNLEARN.md` **can no longer occur**: `if kind(x) = "record"` with
`kind` from a library used to parse as a modifier clause and fail with
`compare modifier not found: x`, naming your own argument; it is now an error
that names the library. And the library-override warning is **gone**, because
there is no longer an override to warn about; `library_collisions()` still
reports shared names.

Migration: ~400 call sites across the tree, the two statistics cookbooks, and
the `server` block's `serve`/`emit`/`finish`, are all qualified.

### Changed — defining the same function twice in one scope is refused

It was silent: the second definition overwrote the first and the first became
unreachable. Scope is the file for a local definition and the **library** for an
imported one — a local shadowing a library function, two libraries sharing a
name, and a local shadowing a builtin are all still legal. The message names the
line of the definition being lost.

---

## 0.1.0 — 2026-09-05

**The first release.** gBASIC has been developed through eight release
candidates; this is the point at which the ladder stops saying anything a ninth
would say. `0.1.0` is *early* and the version number says so — but it is not a
sketch, and it is not *beta* either, since beta implies feature-complete and
stabilising and parts of this are neither by design.

**What that means concretely.** Until 1.0.0 the language surface may change
between releases. Semantic versioning permits it; this note is here so nobody
has to infer it. Where a change would move a program's behaviour rather than
extend it, the entry below says so.

**What is solid.** The core language and runtime — records, first-class
functions, watchers, shared-nothing actors, frame-scoped error handling, and
typed values for dates, durations and money — are covered by a gate of 105
suites that runs on every change, over a CI matrix of three configurations
(every optional module, none of them, and the previous LTS) built after a real
build failure that a 500-case suite could not see. The platform modules
(SQLite, PostgreSQL, ODBC, XML, xlsx, WebClient, WebServer, Mail, LDAP,
cryptography, process control) each carry their own suite and skip cleanly
when their library is absent; the interpreter always builds. The `xlsx` engine
is measured against 15,871 real Excel workbooks, agreeing on 97.38% of formula
cells. The finance and accounting libraries have cookbooks on a harness that
fails if the page and the code disagree.

**What is experimental within it** — these carry the word, the release as a
whole no longer does:

- **`reasoning` / `insight` / `decision` / `automation`** — a design
  laboratory rather than a toolkit. The architecture is closed end to end and
  is one function or so per layer; the default significance threshold is
  measurably anti-conservative (0.10–0.14 against a requested 0.05 on
  lognormal data); the correction covers one search rather than a campaign;
  and every measurement behind it is on generated data. The caveats are stated
  in the reference and in `docs/automation_reasoning_design.md`.
- **`gui`** (GTK 3) — a proof of concept, superseded by `gi` and the GTK 4
  libraries built on it.
- **GUI testing is manual.** It needs a display, so the display-gated tiers do
  not run in CI.

**What is not here.** WebSockets and chunked request bodies in the WebServer.
Optimisation (`MAXIMIZE … SUBJECT TO`). A `business.bas` facade over the
reasoning layers, deliberately deferred until their boundaries have been
tested. Reject inference, monotonic binning and adverse-action reason codes in
`scoring`.

**Platform.** Linux. CI builds and runs the suite on Ubuntu 24.04 LTS and
current Ubuntu, on x86-64. **riscv64** is also a supported target and the suite
runs there on Ubuntu 24.04, with the valgrind tiers skipping because valgrind
has no riscv64 port. No macOS or Windows support is claimed — neither is
tested, and neither should be assumed to work.

**Everything below** is the development record of the eight release candidates
that became this release, newest first.

---

- **ODBC verified against SQL Server and MariaDB — four fixes.** The module
  shipped with tests against the SQLite3 ODBC driver only. Pointed at MariaDB
  11.8 and SQL Server 2025 — the same fixtures, one connection string apart —
  it turned up three defects **invisible to SQLite**, plus one introduced while
  fixing them.

  **Booleans were bound as the character `"1"`.** A real `BIT` column refuses
  it; MariaDB answered *"Data too long for column"*. Now `SQL_C_BIT`.

  **Booleans were read as text and compared to `'1'`.** MariaDB returns the
  byte `0x01`, so **a column holding `true` read back as `false`** — no error,
  just the wrong answer. Now read as `SQL_C_BIT`.

  **Non-Latin-1 text vanished into SQL Server, silently.** Parameters were
  declared `SQL_VARCHAR`, so FreeTDS routed them through a single-byte charset:
  `é` and an em-dash survived while `日本語` and `☃` arrived as an empty
  string — the CP1252 repertoire exactly. Now `SQL_WVARCHAR`, and
  `SQL_WLONGVARCHAR` past 4000 bytes, because `nvarchar`'s non-max limit is
  4000 characters and the first version of the fix truncated a 5000-character
  value to 4000.

  **`odbc.connect` now warns about a driver that needs `ClientCharset=UTF-8`.**
  That failure has no error to hang a hint on: FreeTDS without it stores UTF-8
  bytes one per character, and it *round-trips* through gBASIC because our
  reader reverses the same mangling — correct to us, mojibake to every other
  client. The warning asks the driver its own name, and a control tier asserts
  it goes silent once the option is supplied.

  `tests/odbc.supp` carries driver-internal valgrind defects, each isolated
  first by reproducing it from plain C with no gBASIC in the stack.

  The standing lesson is in `DOGFOOD.md`: **SQLite is dynamically typed, so a
  suite that runs only against it cannot see a type error at all.** It accepted
  a boolean written as text and a 5000-character value in `varchar(200)`, and
  has no wide character types to exercise.

- **A money cookbook, and the defect it found on its first day.**
  `docs/money_cookbook.md` is nine worked recipes over `money` and `finance`,
  on the same cannot-lie harness as the xlsx and chart cookbooks: the page owns
  neither the code nor the output it shows, and `tests/run_money_cookbook.sh`
  fails while any of them disagree.

  **Writing recipe 7 — an ordinary amortization schedule, not a stress case —
  surfaced a silent wrong answer in `money * scalar`.** A double's shortest
  decimal can need more fractional places than a power of ten fits in `int64`:
  `0.005 * 1.005^12` is `0.0053083890593224915`, nineteen places. The code
  treated any such scalar as negligible and returned `0.00`. It is not
  negligible — in exactly those cases the mantissa is large too, so the value
  is ordinary. Every payment in a 12-month schedule came out zero, while a
  360-month one worked, because its scalar happened to land on eighteen places.

  The unit fixtures had all used short scalars (`2`, `3`, `1.08`, `0.5`), so
  only realistic arithmetic produced one long enough to trip it. That is the
  argument for worked recipes in one example: a page of them exercises shapes a
  unit suite does not think to. Fixed by trimming the insignificant low digits
  rather than discarding the scalar; the regression is pinned in
  `money_arithmetic_test.bas` and red-proofed against the pre-fix source.

  Every figure on the page is independently verified — NPV, IRR and
  declining-balance recomputed in Python, and `pmt` checked against Excel.

- **Allocation, and the time value of money (PLAT-MONEY phase 4).** Two
  additions that finish the money work and answer what a line-of-business
  application actually computes.

  `money.allocate(amount, parts)` splits money into **payable** amounts —
  `[33.34, 33.33, 33.33]` for 100.00 into three — taking either a count or an
  array of whole-number weights. Division and allocation are different problems
  and guard digits only solved the first: `(100.00/3)*3` comes back whole
  because a third of a dollar has somewhere to live, but three *payments*
  cannot each be 33.3333, since an invoice or a payroll line has to be a whole
  number of minor units. Allocation works at the minor unit and distributes the
  remainder one unit at a time, so **the parts sum back exactly** — never three
  of 33.33, which loses a cent, nor three of 33.34, which invents one.

  **`stdlib/finance.bas`** is the time value of money, which gBASIC had never
  had: `pmt`, `pv`, `fv`, `nper`, `npv`, `irr`, an amortization `schedule`, and
  `sln`/`syd`/`ddb` depreciation. The statistics library covers *securities
  analytics*; this is the other half — what a loan payment is, what a lease is
  worth today, whether a project earns its cost of capital.

  Amounts are money and rates are plain numbers **per period**: `0.06 / 12` is
  the caller's arithmetic, because compounding conventions vary by product and
  jurisdiction and a library that guessed would be wrong somewhere without
  saying so. Sign follows the spreadsheet convention, since that is what the
  answer gets checked against.

  The schedule's **final payment is adjusted so the balance lands exactly on
  zero**, which is what lenders do — every payment is whole minor units, those
  roundings accumulate, and one figure throughout would end owing a few cents.
  Asserted arithmetically (principal parts sum to the loan; final balance is
  zero), not as a golden. Expected values are external: `pmt` of 250,000 at
  0.5%/month over 360 is `-1498.88` in Excel and LibreOffice too.

  Two bugs the fixtures caught: `irr` overflowed the money type, because
  bisection visits rates near −100% where the discount factor is ~1e-12 and
  dividing money by it exceeds the range — the rate search now works in plain
  numbers, which is right anyway since a rate is a ratio. And a test of mine
  assumed a single cash flow cannot break even; it can, at −59.8%, so that is
  now a control beside the genuine refusal.

- **Exchange rates, and they are dated (PLAT-MONEY phase 3).** Converting
  without an as-of date produces a number nobody can reproduce: re-run last
  quarter's report and you silently get today's rate, and the figure that comes
  out looks perfectly defensible. That is an audit problem rather than an
  arithmetic one, so the date is required rather than defaulted.

  `money.rate(from, to, rate, as_of)` records one; `money.convert(m, to, on)`
  applies the rate **effective** on a date — the latest whose as-of is on or
  before it, so a report run for March sees March's rate;
  `money.rate_on(from, to, on)` reports the rate *and the date it came from*,
  which is the point of dating them. Registering the same pair and date again
  corrects it.

  **A rate is decimal text, not a number** — FX rates routinely carry eight
  significant figures and a double would round them, the same reasoning that
  drove exact construction in phase 0. Conversion moves between storage scales
  in one integer operation, so USD (two places) to JPY (none) or KWD (three)
  loses nothing.

  **Inversion is refused.** Given USD→EUR, the EUR→USD rate is not its
  reciprocal — the two sides of a quote differ by a spread, and inverting would
  invent money. The refusal names the rate that *does* exist so the author can
  decide. Converting to the same currency is identity and needs no rate, which
  is the control tier: without it, generic code converting a mixed list into
  one reporting currency would fail on the entries already in it.

  Expected values are computed in exact decimal arithmetic outside gBASIC, not
  recorded from a run — a conversion wrong in its last digit is exactly what a
  golden would enshrine.

- **`money` carries its currency, with guard digits (PLAT-MONEY phase 2).**
  `{USD}` was a single hardcoded `strcmp` in the modifier dispatch and cents
  were hardcoded everywhere else, so JPY (no minor unit) and KWD (three) had no
  correct representation at all. `{USD}` is now one table lookup, and **all 178
  ISO 4217 currencies arrived with it**, each with its own minor-unit exponent.

  A money value knows its currency. `USD + EUR` raises, ordering them raises,
  and `USD 19.95 = EUR 19.95` answers **false** — equality is a real question,
  ordering is not one without a rate. Applying a different currency's modifier
  to existing money is refused rather than treated as a re-tag, because
  re-tagging and converting are different operations and picking one silently
  would invent a rate of 1.

  **Storage carries four guard digits below the minor unit**, so intermediates
  survive a multi-step calculation and round once at display: `(100.00/3)*3` is
  `100.00`, where cents gave `99.99`. That closed a gap phase 1 had pinned as
  open. **The cost is range, and it is real**: USD's ceiling fell 10,000×, from
  ~$92 quadrillion to ~$9.22 trillion. It broke the phase 0 and 1 fixtures,
  which is the most concrete demonstration of the trade available.

  `money.register(code, exponent)`, `money.retire(code)` and
  `money.currencies()` extend the table at run time — an internal scrip,
  loyalty points, or a withdrawn currency, since **the built-in table is the
  *current* ISO list** and ITL, DEM and FRF are simply not in it. There is no
  removal: `retire` marks a currency historical (new values refused, existing
  ones still read), because removing one does not unmake the values that exist
  and archived data is what money is for.

  `SER_VERSION` is 2, carrying units, currency **and** exponent so a value is
  self-describing — actors are fork+exec, so a currency registered in the
  parent is not registered in the child. v1 payloads still deserialize,
  rescaled and assumed USD; the test reads a payload the phase-1 binary
  actually wrote.

  **A claim in the design doc turned out to be wrong and is corrected rather
  than quietly dropped.** It justified the migration partly on payloads
  "sitting in files right now" — they cannot be: `read()` truncates at the
  first NUL and `bytes()` returns a file's size, so gBASIC has **no
  binary-safe file read at all** and a serialized value can only travel in
  memory or hex-encoded. Recorded in `DOGFOOD.md` as a missing capability.

  The currency table is generated from the system's `iso-codes` by
  `tools/make_currency_table.py` into a **committed** `src/currency_table.h`,
  so a build never depends on that package — a currency appearing or vanishing
  with the build machine would be a correctness problem, not a packaging one.

- **`money` arithmetic stays in integers (PLAT-MONEY phase 1).** `money * n`
  and `money / n` computed `(double)cents * n` and then `round_to_cents(amount
  / 100.0)` — a divide and a multiply by 100 in floating point, for an
  operation needing neither. It corrupted a value the caller had already got
  right, with no error.

  **The defect does not reproduce at ordinary magnitudes**, which is why it
  survived: below 2^53 units a double has precision to spare, so `x * 3` on a
  few billion dollars is exactly right. Above that it is silently wrong. Every
  expected value in the new fixtures is therefore above 2^53 units and computed
  by integer arithmetic outside gBASIC — against the phase-0 binary,
  `92233720368547.75 * 2` returns `184467440737095.52` where the answer is
  `...095.50`.

  A scalar is now decomposed into `num × 10^-dexp` through the same
  shortest-round-trip decimal that construction uses, then applied as
  `units × mul / den` over a 128-bit intermediate with half-even rounding. An
  **integral** scalar takes the exact path with no rounding decision at all.

  **A fifth defect, found while doing this and not in the original report:
  `+` and `-` were unchecked signed overflow** — undefined behaviour rather
  than a wrap. `int64max + 0.01` returned the most *negative* money value, a
  sign flip. Unreachable until phase 0 made such a value constructible. Both
  now raise.

  `round_to_cents`, which *was* the defect, is deleted, and the suite asserts
  its absence — dead code that still compiles is how a retired construct comes
  back.

  This had to land **before** guard digits (phase 2), not after: guard digits
  multiply the unit count by 10^4, dropping the range where the double path is
  safe from ~$90tn to ~$9bn, which would have moved a defect nobody could reach
  into the range where real money lives.

- **`money` can finally hold an exact value (PLAT-MONEY phase 0).** The
  storage was always right — an exact int64 of cents, so `0.01` accumulated a
  thousand times is exactly `10.00`, which a double cannot do. What was wrong
  was that **nothing could put an exact value in**: `{USD}` took a number,
  already a double by the time the modifier saw it, and refused text outright.
  So `92233720368547.75` became `...76`, silently, and the type's own int64
  range was unreachable through its own constructor. Reported by the gdash
  session, the type's first real consumer; re-verified here against the source.

  `{USD}` is now **reflective** — it takes whatever it is given and does the
  most accurate conversion available. Both routes end in the same exact integer
  parse of decimal text and differ only in what excess precision *means*:
  authored text (`"1.23456789"`) is **refused**, because the author wrote a
  value money cannot hold, while a computed number is **rounded**, because
  `price * 1.08` carries seventeen digits as a matter of course and refusing
  that would make the type unusable for arithmetic.

  A number is rendered to its shortest round-trip decimal (PLAT-NUMFMT) *before*
  parsing, which is what makes an ordinary literal exact without touching the
  parser: the double for `92233720368547.75` renders back to that same text.

  **This dissolved a fourth defect nobody had filed.** Rounding at the `.5`
  boundary was not well defined — `0.125` gave `0.13` while `0.145` gave `0.14`,
  which looks like banker's rounding and was not: `round_to_cents` was
  half-away-from-zero applied to a *double*, and `0.145` as a double is
  `0.14499999999999999001`. The rule depended on the binary representation of
  the literal rather than the text the author wrote. Parsing decimal text leaves
  no binary representation to be ambiguous about, so ties now resolve
  **half-even** every time: `0.125` → `0.12`, `0.155` → `0.16`.

  Two bugs found by the fixtures rather than by reading, both from int64's
  **asymmetric range** (the most negative value is one greater in magnitude than
  the most positive): the first parser built the magnitude signed and negated at
  the end, refusing `-92233720368547758.08`, a value the type can hold; and the
  renderer printed it as `--92233720368547758.-8`, because negating `LLONG_MIN`
  overflows. That second one was pre-existing and simply unreachable until
  exact construction made the value constructible.

  Red-proofed against the pre-change binary on the number path, where the
  difference is a wrong *answer* rather than a crash: same source, `...76` vs
  `...75` and `0.13` vs `0.12`. Zero goldens moved.

  Phases 1–4 (integer-preserving `*` and `/`, currency identity with guard
  digits, dated FX, and a `finance` library) are specified in
  `docs/money_design.md`. Note the ordering there is a **constraint**: guard
  digits cut the range where the double path is safe by 10,000×, from $90tn to
  $9bn, so the arithmetic fix must land first.

- **ODBC: one module, every database with a driver.** gBASIC could reach
  PostgreSQL and SQLite and nothing else. The gap was not four missing
  modules, it was one — SQL Server, Oracle and DB2 have no free, packaged C
  client, so shipping a native module for each would mean shipping (or
  requiring) a proprietary library per backend. ODBC moves that to where it
  belongs: the *operator* installs the driver, and `odbc` speaks to whatever
  they installed. `load odbc`, then `connect`/`close`/`query`/`exec`/
  `begin`/`commit`/`rollback`, spelled exactly as `sqlite`'s and `pg`'s are so
  the three are interchangeable, plus `odbc.drivers()` and `odbc.sources()` so
  a program can tell "no driver of that name is installed" from "the server
  refused you".

  **`BIGINT`, `DECIMAL` and `NUMERIC` come back as STRINGS.** A gBASIC number
  is a double: `DECIMAL(19,4)` narrowed to one loses the cents it exists to
  protect, and any integer past 2^53 comes back off by one — silently, as a
  value nobody would look at twice. `pg` already answers oids 20 and 1700 this
  way; `odbc` follows it. Money *parameters* bind as exact decimal text for
  the same reason, so the write side does not reintroduce the loss the read
  side avoids.

  Parameters are bound through `SQLBindParameter`, never interpolated. The
  suite proves that by **executing** the claim rather than asserting it — and
  proves the proof non-vacuous, which matters more than it sounds: the obvious
  payload (`'); drop table x;--`) passes against a module that pastes every
  parameter, because the SQLite3 ODBC driver refuses multiple statements
  anyway. Measured, not assumed. So the load-bearing payload is a tautology
  that subverts a *single* statement, run both ways: pasted it matches every
  row, bound it matches only the row whose value really is that text.

  Text parameters carry their real byte length rather than `SQL_NTS`. Bound
  the obvious way, a gBASIC string holding an interior NUL was handed to the
  driver as `strlen()` and the prefix went silently to the database — no
  error, no short write. The tier that pins it asserts from the *database's*
  side (`hex()` over the stored bytes), so a reader that guesses cannot
  satisfy it, and it records honestly that the round trip still does not
  recover the value: the SQLite3 driver returns text `strlen`-truncated, which
  is the driver's limit, not ours.

  `docs/odbc_cookbook.md` is the task-oriented tour, on the same cannot-lie
  harness as the xlsx and chart cookbooks: the page owns neither the code nor
  the output it shows, `tests/run_odbc_cookbook.sh` fails while any of them
  disagree, and its CODE and OUTPUT tiers were proven red in isolation. It
  states plainly, at the top, that the module has been exercised against the
  SQLite3 driver only.

  `tests/run_odbc.sh` is hermetic by default over the SQLite3 ODBC driver — a
  real driver-manager round trip, not a stub — and the same fixtures run
  against SQL Server or MySQL by setting `GBASIC_ODBC_CONNECTION`. Fixtures
  are self-checking rather than golden, because the failure this module exists
  to prevent is an ordinary-looking number wrong in its last digits and a
  golden would record the damaged value as expected. Red-proofed on the
  exactness and duplicate-column tiers; valgrind caught the one real defect
  (a cleanup walk over an array the parameter-count refusal never allocated),
  which produced no wrong value, only a segfault.

- **The variable form of the builtin collision warns too.** Precedence is
  builtin → user function → function-valued variable, and the last step was
  silent: `first = my_fn` then `first(xs)` ran the **builtin** `first` and
  returned an element of `xs` — a plausible value from the wrong function, no
  error, nothing to see. The library form has warned for a long time; the
  variable form had nothing.

  **It warns at the CALL, not at the assignment.** `list = [1, 2]` is a
  perfectly good variable and only *calling* it is the mistake, so warning on
  assignment would have fired constantly on innocent code — which is how a
  warning channel dies. Firing on the call cannot false-positive on the common
  case. Verified silent on all four harmless shapes: a builtin-named variable
  holding a non-function, an ordinary builtin call while one exists, a function
  value under a non-builtin name, and a record field (`r.first(2)` is not a
  bare name, so it cannot collide). Once per call site, since the trap lives in
  loops and handlers.

  `tests/warning_model/builtin_shadow_value.bas` *claims* the warning rather
  than observing that stderr was non-empty, so it asserts **which** warning
  fired. Red-proofed.

- **`lib.fn` is a function value.** It worked only in call position:
  `lib.fn(x)` ran, `f = lib.fn` raised `undefined variable: lib`. So passing a
  library function as a callback had no direct form, and the workaround — a
  record carrying state and function together, invoked as a method — drags
  otherwise-private wiring into the caller purely for reachability.

  The machinery was already complete: a bare name has evaluated to a function
  value since first-class functions landed, and `value_function` has carried a
  library all along. The qualified spelling simply never reached it. Field
  evaluation now falls back to resolving `receiver.field` as a library function
  when the receiver is **not** a variable — the same shape the soft names
  `warning` and `error` use one branch above — so a variable named `heartbeat`
  still shadows the library. It adds a fallback and takes nothing.

  Also fixed the diagnostic, which blamed the receiver for the field's mistake:
  a loaded library with no such function said `undefined variable: heartbeat`,
  sending the reader to look for a variable they never wrote. Now
  `library 'heartbeat' has no function 'nosuch'`, matching what the call
  position always said.

  Two adjacent limits found while testing and deliberately left: `table[0](7)`
  does not parse (bind it first; a field call is fine), and a variable holding a
  function value cannot be called if its name matches a builtin — the same
  collision hazard fixed for library functions above, in its variable form.

- **The builtin-collision warning was consulting the wrong list.** A library
  function whose name matches a builtin is a silent trap: an *unqualified* call
  reaches the **builtin**, and whatever fails next carries the builtin's own
  message, naming neither the library nor the collision. The `override` warning
  is the only thing standing between an author and that — and it did not know
  about eleven reachable builtins.

  `builtins.c` holds two lists: the 166 registered names, and `dispatch_only`
  for the file and directory families (`exists`, `read`, `write`, `bytes`,
  `lines`, `chars`, `lock`, `unlock`, `list`, `files`, `folders`) that `eval.c`
  dispatches at top level without registering. Those eleven are every bit as
  callable, and shadowing one warned **nothing**: a library defining `exists`
  then failed with `exists expects a file reference` — the builtin's message,
  from a call the author believed was theirs.

  The check now uses `gbasic_has_builtin`, the same predicate `has_builtin()`
  answers with, so one maintenance rule covers both lists.

  Pinned by a new tier in `run_warning_model.sh` that does not check a list —
  it **asks the interpreter** which names it considers builtins and requires
  each to be un-shadowable in silence, accepting either a warning or a parse
  refusal (a keyword like `watchers` cannot be a function name at all, which is
  stronger). 177 names covered, so a twelfth entry in either list is covered
  the day it lands. Red-proofed: reverting the predicate names all eleven.

  Prompted by a session that hit this with `audit.record` and was saved by the
  warning — `record` happening to be in the list that was checked.

- **`exit(code)` — a program can set its own exit status.** There was no way
  at all: a gBASIC program could report 0, or 1 by failing, and nothing else.
  That put its most externally-visible contract out of reach, since anything
  branching on a tool's result — a scheduler, CI, a shell — reads `$?`; the
  workaround was printing a sentinel line for a wrapper script to exit with,
  which moves the contract into the shell.

  It unwinds through `runtime_stopped`, the path `stop` already uses, so every
  frame, watcher and lock tears down exactly as before — `exit` is `stop` that
  also names a status. **0–255, and wider is refused rather than truncated**,
  because the kernel keeps only the low byte and `exit(256)` would report
  success from a program that meant to fail.

  The first implementation set the right status and **did not stop** — the
  statement after `exit` still ran. `runtime_stopped` was only consulted on
  paths that already produced a stop result, and a bare call is not one. The
  check now sits once in `eval_stmt_list`, so it holds for every statement kind.

- **`now(zone)` and `epoch(dt, zone)` — UTC is reachable, and correct.**
  Reported from another session building on gBASIC, reproduced here, and worse
  than reported.

  `to_zone(now(), "UTC")` is a **no-op**: `to_zone` reads its input as *already
  UTC* and renders it in the target, so a local value comes back unchanged —
  measured at 06:56 local while UTC was 10:56, a four-hour error wearing a UTC
  label. And the two halves of the API take their zone from different places:
  `number(dt)` / `epoch(dt)` read a datetime as **local**, so the *documented*
  route to UTC — `from_zone(now(), yourzone)` — yields a value whose epoch is
  wrong by the offset. Verified 14400 seconds out. An audit trail built that
  way stores timestamps hours in the future and nothing reports it.

  **Fixed by addition, not by changing either contract** — both are
  self-consistent, and moving either would silently change every existing
  program's answers. What was missing was any way to say which zone a value is
  *in*: `now(zone)` gives the current civil time in a named zone, and
  `epoch(dt, zone)` places a civil value on the timeline as civil-in-that-zone.
  `epoch(dt)` keeps the local reading. `epoch()`, `epoch(now())` and
  `epoch(now("UTC"), "UTC")` now agree to the second and match `date +%s`.

  Tested in `run_core.sh`, whose remit is checks a golden cannot express: the
  assertions are against the **system clock**, because only the world can tell
  a conversion from a no-op. `Asia/Tokyo` is the non-local zone, since it has
  no DST and so cannot pass by accident half the year. Both tiers red-proofed.

- **`run_core.sh` could not report a failure.** Under `set -e` a failing
  assertion aborted the script before `check` ran, so a real regression
  truncated the suite and the summary line never printed. Found because a
  deliberately broken binary produced no `FAIL` output at all. Errexit is off;
  the exit status comes from the fail counter, which is what a test suite's
  status should mean.

- **GUI documentation: a tutorial and a cookbook, neither of which can lie.**
  `docs/gui_tutorial.md` is the guided version; `docs/gui_cookbook.md` is eight
  recipes on the same harness as the xlsx/chart/datetime cookbooks —
  `tests/run_gui_cookbook.sh`, 24 checks, all three tiers red-proofed in
  isolation. It skips honestly without a display or the GTK 4 typelib.

  **What makes a GUI cookbook checkable at all:** `gtk.init()` needs a display
  but *showing* a window does not, so every recipe builds real widgets and
  interrogates them with nothing on screen — the technique `run_gtkui.sh` and
  `run_datagrid.sh` already use.

  **The order is evidence-led, and it is not the order I first proposed.**
  gBASIC Studio — the largest application written in gBASIC — uses `gtk` + `gi`
  imperatively: 86 `gtk.` calls, 72 `gi.`, and **zero** `gtkui`, despite its own
  plan naming the reconciler. So the tutorial teaches `gtk` constructors and
  `gi.connect` first, presents `gtkui` for changing keyed lists rather than as
  the front door, and treats `gi.new` as the ordinary escape hatch it is.

  The centrepiece is recipe 2: **state in a callback must live in a record.**
  gBASIC has no closures, GUI code is nothing but callbacks, and an outer scalar
  assigned inside a handler silently becomes a function-local — a counter stuck
  at 1, a loop that never quits. Hit while writing the recipe, which is why it
  is second and has its own tutorial section.

  Writing the recipes by running them produced four corrections to what I had
  assumed: `gi.new` takes flat name/value pairs rather than a record; every
  `datagrid` update returns the new handle and must be reassigned (the
  unused-result warning is what says so); a missing `gtkui` key is `nothing`,
  not `unknown`; and `gtk.scrolled(x).get_child()` is a `GtkViewport`, not `x`.

- **`gtk.connect` is now the alias it was documented to be.** It called
  `gi.connect` without **returning** the handler id, so anything wired through
  it could never be disconnected — `gi.disconnect` needs that id and the only
  copy was discarded. Found by a cookbook recipe asserting that the two calls
  behave the same.

- **`tests/run_packaging.sh` — the shipping path is now tested, not just
  demonstrated.** The packaging work was verified by hand and nothing re-ran
  it; a build script that has produced a working service exactly once, on the
  machine where it was written, is a hypothesis. Six tiers, no root needed
  (`build-deb.sh` takes a `RUNTIME` override so the package can be built
  rooted somewhere writable and actually run): BUILD (and that the lean build
  really is lean — under 20 shared libraries against ~48 full), STRUCTURE
  (control, conffiles, a world-traversable package root), SUBSTITUTE (no
  `@RUNTIME@` survives), RUN (the service starts, `/health` answers, a POST is
  stored and read back), QUIET (no warning on startup), and SHADOW — which
  asserts **both halves** of the library hazard: a stray `stats.bas` under an
  app really does beat the shipped one, *and* loading by absolute path is
  immune. A defence nobody proves is a comment.

  **It caught a real bug on its first run.** The example printed
  `listening on 8399` and listened on `8099`: a `server` block's head takes
  literals only, so the configured port was read and silently ignored. The
  service looked correct in its own log and answered on the wrong port.

  **And then the suite exposed a defect in itself.** It used a FIXED port, so
  a previous run's process still holding that port answered `/health` while
  its database had already been deleted — the suite was talking to a stranger
  and reporting a 500 that had nothing to do with the package. It now binds
  `port: 0` and reads the assigned port back from the service's own startup
  line, which is the lesson `run_web_pool` already recorded, and waits for the
  process to actually exit rather than assuming `kill` is synchronous. Three
  consecutive runs pass; before the fix, every second run failed.

- **A `server` block can take a computed option after all.** The fix for the
  above is worth stating because the reference implied the opposite
  ("computed configuration belongs to `webserver.listen`"): the declaration
  binds a plain record and `serve` reads `options` off it, so
  `app.options.port = number(conf.port)` before `serve(app)` applies a
  run-time value **and keeps the declarative block**. Verified end to end —
  the overridden port answers and the declared one does not. Report the port
  from the live record (`h.port`), never from the configuration, because with
  `port: 0` the kernel chooses. Documented in `reference.md` and
  `shipping_applications.md`.

- **`run_docs_gate.sh` now sees `packaging/` paths.** Its reference regex
  matched only `examples/` and `tests/`, so the new COOKBOOK entry pointed at
  a file the gate could not check — the exact hole that gate exists to close.

- Packaging and `else if` reached the docs that were still silent on them:
  `README.md` (feature list and a Shipping section) and `docs/ai/COOKBOOK.md`
  (a Shipping group, the `else if` entry, and the two rules a shipped app must
  follow — absolute-path loads and binding `serve`).

- **`else if`.** A condition chain closed by a **single** `end if`:

  ```basic
  if esc = "n" then
      out = out + "\n"
  else if esc = "t" then
      out = out + "\t"
  else
      out = out + esc
  end if
  ```

  gBASIC had none, and nothing said so — `docs/reference.md` used one in its
  path-containment **security** example, excused by a `fragment` marker
  claiming the block was "an API shape" when it was a program teaching a
  construct that did not parse. The alternative the language already had,
  `consider true` with an `if` per branch, appeared in **zero** files anywhere
  in the tree, which is why 50 sites in `stdlib` are nested staircases.

  **0 new grammar conflicts**, measured, for the block and inline forms both.
  It desugars to the nested form rather than adding an AST node, so `--ast`
  shows the nesting that is really there and the evaluator is untouched; the
  tail *recurses* instead of nesting a whole `if_statement`, which is what
  makes one `end if` close the whole chain rather than one per rung. Rungs may
  be inline or blocks and the two mix; a trailing `else` is optional; chains
  nest inside chains.

  **The old nested form is untouched** — this is an addition, not a migration,
  and none of those 50 sites were rewritten. `consider` remains the better tool
  when every branch tests the same subject, because it names the subject once;
  `examples/else_if_test.bas` pins that alongside the 18 chain cases.

- **`serve` is documented as bound, not bare.** `serve(myapp)` unassigned was
  promised to work, and does — but it discards a non-`nothing` return, so the
  `unused-result` warning fired on **every service start** and landed in an
  operator's journal. Checked before changing anything: all ten call sites in
  the tree already assign it, and they assign it because they *use* it —
  `h.port` is how you learn the port after binding `port: 0`. So the return is
  load-bearing (returning `nothing` would be wrong) and the documented bare
  form was a shape nothing in the tree uses. Fixed in the documentation rather
  than by exempting a name or inventing a discard marker.

- **A gBASIC application can be shipped as a `.deb`.** `packaging/build-deb.sh`
  turns an application directory into an installable package;
  `packaging/example-app` is a working loopback service that proves the path,
  and `docs/shipping_applications.md` is the guide. Built, extracted and RUN
  end to end: health endpoint, a POST, the value read back.

  **The model is a vendored runtime with system libraries.** The package
  carries its own interpreter and stdlib under `/usr/lib/<app>/` and does not
  depend on a system `gbasic`. gBASIC's optional modules are compile-time
  gated, so a server build drops GTK, GObject-introspection, PostgreSQL,
  libxml2 and libcurl — measured, **48 shared libraries to 10**, 1.7 MB to
  1.1 MB — while `libssl`, `libcrypto`, `libsqlite3` and `zlib` stay the
  distribution's, patched by the distribution. That is why "one static binary"
  is the wrong answer for a security product: an auditor can see there is no
  frozen OpenSSL inside. No Makefile change was needed; the `*_AVAILABLE=0`
  overrides already worked.

  **Documented for the first time: how `load NAME` actually resolves, and the
  hazard in it.** A bare `load` searches the source file's own directory
  **recursively, and first** — ahead of `GBASIC_PATH` and the compiled-in
  stdlib. A three-line fake `stats.bas` buried three directories under an
  application silently replaced the real one and returned a wrong number, and
  the warning emitted says the *correct* library was "ignored". Two defences
  are stated and both are used by the example: load stdlib libraries by
  absolute path via an `@RUNTIME@` token the packager substitutes (and fails
  the build if any survives), and keep the application directory root-owned
  and containing only the application. Native modules — `sqlite`, `pg`,
  `webclient`, `webserver`, `xml`, `gui`, `gi` — are compiled in and exempt.

- **`--tokens` printed `UNKNOWN` for `do` and `until`.** They were the only two
  token kinds missing from `token_type_name`, in an interface `docs/TOKENS.md`
  calls the source of truth for external syntax highlighters. Both named; no
  "not handled in switch" warnings remain for tokens.

- **gBASIC has no `else if`, and now says so.** `docs/reference.md` used one in
  its **path-containment security example**, excused by a `fragment` marker
  claiming the block was "an API shape" when it was a program teaching a
  construct that does not parse. The idiom is `consider true` with an `if` per
  branch — which appeared in **zero** files, documentation or code, anywhere in
  the tree. Now documented in the reference and taught in the tutorial, and the
  example is a real, checked block rather than an exempted one.

- **Every public stdlib function is documented, and a gate keeps it that way.**
  64 were not, across 14 libraries, and nothing could tell. The design documents
  explain *why* each library exists and the cookbooks show recipes; neither is a
  per-function reference, so a function could ship, be tested, be correct, and
  be undiscoverable. `matrix` was the extreme — eight public functions, the
  primitives every regression in `stats` is built on, and **zero** mentions in
  any document. `stats` had 32 undocumented distribution functions.

  New: `docs/statistics_design.md` §8b, an API reference for the distribution
  families (pdf/pmf, cdf, quantile for ten continuous and three discrete) and
  for `matrix`. Every signature in it was checked against the source rather than
  written from memory. It also states two things that would otherwise surprise:
  gamma and exponential take a **rate** while Weibull takes a **scale**, and the
  discrete quantiles spell one of their two `p` arguments differently because
  the probability being inverted and the distribution's own success probability
  collide.

  `tests/run_stdlib_docs.sh` now fails when a public function is undocumented.
  "Public" is the only thing gBASIC enforces — a name without a leading
  underscore — so a helper that should not be called is not an exception, it is
  a function that wants renaming, and doing that is the other way to pass.

  **The second tier caught three documentation bugs that would fail if copied:**
  `chart.new(...)` in a `chart_design.md` example, in the same document whose
  next paragraph explains a library *cannot define* `new` (it is `chart.spec`);
  `dates.from_zone` / `to_zone` / `zone_offset` in `datetime_design.md`, which
  are **core builtins**, not `dates` calls; and `stats.mean(...)` in
  `text_design.md`, where `mean` is a builtin. All four verified failing before
  the fix. Three remaining named-but-absent mentions are allowlisted with a
  stated reason each, because an allowlist without one is where a real defect
  goes to be forgotten.

- **`dates.dayname` is O(1) instead of O(days).** It walked one day at a time
  from a hardcoded Monday — 45 ms for a date twenty thousand days out, behind a
  name that reads as constant time. It predated `d.dayname`, a core field on
  every date value, and now delegates to it. Same answers, and the two
  day-name-stepping helpers went with the loop. Found while documenting it:
  writing down what a function does is a good way to notice it should not exist.

  Rebaseline: six `negative_dates_*.err` goldens, all by the same 53 lines and
  none by a single character of message — they pin `stdlib/dates.bas` LINE
  NUMBERS, the hazard `CLAUDE.md` already records for `chart`. Removing two
  helper functions moved every diagnostic below them.

- **BREAKING: the post-test loop is `do … until c`.** `loop` is gone from the
  syntax, and the continue-condition form `do … loop while c` is removed.

  ```basic
  do                          do
      tries += 1     →            tries += 1
  loop until tries >= 3       until tries >= 3
  ```

  Two objections to `loop` turned out to be one answer. It *reads* redundant —
  and it was, on the `until` side. But it was **load-bearing on the `while`
  side**, and not removably so: `do … while c` cannot be distinguished from a
  body whose next statement is a nested `while c … end while`, because both
  readings are complete programs and the `end while` that separates them can be
  arbitrarily far ahead. Measured at **32 reduce/reduce conflicts**, and —
  the part worth recording — *dropping the `until` form does not help*, because
  the ambiguity is with the nested statement, not with the other terminator.

  So `loop` could only be deleted by keeping `until` and dropping `while`.
  Which is fine, because the `while` form was always redundant: it means
  `until not c`, and `!<`/`!>` cover the single-comparison case without a `not`
  (`loop while j < 3` is now `until j !< 3`). For a compound condition, negate
  the whole thing rather than applying De Morgan by hand. Evidence it was not
  wanted: outside its own test, `loop while` had **zero** uses in the tree, and
  `loop until` had one.

  **The keyword ledger, verified against the binary rather than counted by
  hand:** 47 keywords → **46**. `loop` stops being a keyword in *any* position
  and is an ordinary identifier again (it is a label in `stdlib/dates.bas`).
  `until` goes the other way: `do … until c` makes it statement-initial, so it
  can no longer be a variable — it collided with `until[0] = 5` and
  `until{USD} = 9.95` on the `[`/`{` lookahead, which is exactly where the two
  shift/reduce conflicts landed when this was measured. Words usable as
  ordinary names go from four to two: `end` and `next`.

  Zero grammar conflicts. The `until` flag is removed from the AST node rather
  than left always-true, because dead machinery that still parses is how a
  retired construct comes back.

  Rebaselined: `examples/do_loop_test`, `loop_syntax_test`,
  `keyword_stability_test`. New negatives pin both retirements —
  `negative_until_as_name`, `negative_do_loop_until`.

- **Two loose ends closed, and the second was found by measuring rather than
  reading.**

  **`dim` is refused as a statement, and only there.** It was refused in
  `yylex` at token delivery, so the refusal fired in every position rather than
  the one it was written for: `{ dim: 7 }` and `r.dim` were both rejected with
  "`dim` is not a gBASIC statement" at a column where no statement is possible.
  Every other keyword is a legal field name — `dim` was the sole exception and
  nothing chose that. The token is delivered now and the grammar decides, which
  is the difference between asking *what* the word was and asking *where* it
  appeared. Zero new grammar conflicts, measured. The message is byte-identical
  and stated in **both** statement positions, because a `dim` inside a
  `consider` body used to get the advice too and losing it there would be a
  regression dressed as a fix.

  **The directory family was still failing silently.** `DOGFOOD.md`'s `round`
  entry named an undone follow-up — sweep the remaining builtins for the same
  "coerces where its siblings refuse" shape. Done by *probing all 176 builtins*
  with wrong-typed arguments in the first and second positions rather than by
  reading for the pattern. The coercion class came back **clean**: `round` was
  genuinely the sole outlier. But the probe surfaced a different and worse
  one — `list`, `files` and `folders` each carried two bare `fprintf` refusals
  (wrong arity, non-directory argument) that returned `nothing` with **exit
  code 0** and nothing catchable by `on error`. That is exactly the signature
  `tests/run_silent_traps.sh` exists for; the 2026-08-23 sweep promoted
  `goto`/`gosub` and out-of-range reads and missed this family. All three now
  raise: located, fatal, catchable. They were the only three left, which the
  sweep makes a measurement rather than a hope.

  **Two hardcoded test counts made real.** `run_parse_exit.sh` printed a
  literal `7` and `run_silent_traps.sh` a literal `12` — the latter was
  *already wrong*, running 13 cases while claiming 12. A gate that reports a
  number it does not measure can shrink without saying so.

- **Documentation sweep (2026-08-26)** — the statistics field expansion and
  `market` existed only in `reference.md` and this file. Now in the two
  task-first stats cookbooks (event studies, causal inference and price history
  in econometrics & finance; survival/Cox, meta-analysis and factor analysis in
  social & behavioral), in `README.md`'s feature surface, in `docs/ai/COOKBOOK.md`,
  and in `docs/project_state.md`. Every new recipe is executed by
  `examples/cookbook_econ_test.bas` / `examples/cookbook_social_test.bas`.

  **Reserved words are documented for the first time.** There was no list in any
  document — they were named one at a time, scattered across pages. Measured
  against the binary rather than read: 47 keywords, of which 43 cannot be a
  variable name and exactly four (`end`, `loop`, `next`, `until`) can; `server`,
  `warning`, `default`, `resume` and `from` are not reserved. `reference.md`'s
  old list was explicitly partial and omitted eleven.

  **Three stale claims retired**, each contradicting shipped behaviour: "a raise
  cannot be caught" in `reference.md` (frame-scoped `on error` shipped in
  PLAT-ERR, and the sentence cited `docs/ai/ERRORS.md`, which already said so);
  "records are an association list with linear field lookup" in
  `project_state.md` (hash-indexed since PLAT-RECIDX); and the same phrasing in
  `xlsx_design.md`, kept as history but dated so it cannot be read as current.

  Audited by execution, not by reading — the lesson of the three reading sweeps
  that each missed 163 lines of `#` comments. Every function named in both stats
  cookbooks exists, and every documented `record.field` exists on the record its
  function actually returns, checked across both cookbooks and `reference.md`.
  That audit found four wrong field names, all in the new text: `agg.p_value`
  (it is `agg.p`) and `km.std_errors` / `km.ci_low` / `km.ci_high` (they are
  `se` / `lower` / `upper`).

  Also fixed: `tests/run_doc_examples.sh` was briefly pointed at both stats
  cookbooks, which caught a reserved-word bug in the new `market` recipe (`to`
  as a variable) before backing out — those pages are API catalogues whose
  field-listing lines are not valid gBASIC, and gating them would take 61
  exemption markers or a rewrite that damages them. Recorded rather than
  papered over.

- **Causal inference in `stats`** — `did` and `pre_trends`
  (difference-in-differences) and `iv_2sls` (instrumental variables), with
  cluster-robust (CR1) and HC0–HC3 covariance available to both.

  **Both estimators can be right in the coefficient and wrong in the standard
  error**, which is the reason the suite is shaped the way it is: nothing about
  the output looks off, and a golden would record the wrong standard error as
  the expected value and defend it forever.

  2SLS run as two ordinary regressions — fit *x* on *z*, then *y* on *x̂* —
  produces the identical point estimate and measures its residuals against
  *x̂*. The model's residuals are `y - X*beta`, against the original *x*. The
  fixture performs the naive version alongside and pins that the coefficients
  agree to ten digits while the errors do not: on two datasets differing only
  in the sign of the confounding, the naive error is 1.78× too large and 2.7×
  too small. It is not conservative.

  A DiD on serially correlated panel data understates its own uncertainty
  (Bertrand, Duflo & Mullainathan 2004). In the test panel — thirty units,
  twenty periods, a persistent post-period shift per unit — the conventional
  error is 3.2× too small and reports *p* < 0.001 where clustering reports
  *p* > 0.10, on an estimate identical to twelve digits.

  So almost every numeric claim is derived a **second way inside the fixture**
  rather than recorded: the DiD estimate against the four cell means; CR1
  against `ols_robust`'s HC1, which it must equal exactly when every cluster
  holds one observation; the 2SLS estimate against the Wald ratio (four means,
  no matrix algebra); its standard error against σ²/Σ(x̂−x̄̂)²; the first-stage
  F against t² from an ordinary `ols`; Sargan's J against *n*·R²; Wu-Hausman's
  and the pre-trend F against two residual sums of squares.

  Eleven red proofs; two came back green and drove real coverage. Both were
  the same blind spot: the main IV fixture has no exogenous controls, so
  restricting the wrong block in `_f_drop` — for the first-stage F and again
  for Wu-Hausman — was a no-op there. Only a fixture *with* a control can tell
  a test of the excluded instruments from a joint test over everything.

  `pre_trends` reports what it is. Parallel trends cannot be tested — it is a
  claim about what the treated group *would* have done — so a large *p*-value
  is the absence of evidence against it over however many pre-periods exist,
  and the returned `note` says that in words. One fixture check deliberately
  asserts the *opposite* of the easy lesson: dropping an exogenous control
  orthogonal to the instrument does not bias 2SLS, it only widens the interval.

  Also fixed, in seven runners: a `diff … | head -N` under `set -euo pipefail`
  aborts the script, so a failing suite reported only its first failure and
  skipped every remaining tier.

- **Exploratory factor analysis in `stats`** — `factor_analysis`, principal-axis
  factoring with iterated communalities and a varimax rotation implemented
  without trigonometry (gBASIC has none; the quarter-angle comes from two
  half-angle identities and a square root).

  **It is not PCA.** PCA explains total variance, factor analysis explains
  common variance, and the whole difference is 1s versus communalities on the
  diagonal. On half-noise data that is 0.60 against 0.40 — using PCA where a
  latent construct is meant overstates what the factors explain by half.
  Rotation cannot improve fit, and Heywood cases are reported rather than
  clamped.

- **Survival analysis in `stats`** — `kaplan_meier` (with Greenwood standard
  errors and bands), `survival_at`, `logrank`. Verified against the *published*
  results of the Freireich 1963 leukaemia trial rather than against itself:
  median remission 23 weeks versus 8 on placebo, S(10) = 0.7529, S(23) =
  0.4482, log-rank χ² = 16.79.

  **Censoring is the subject.** Both ways of avoiding it are wrong and neither
  announces itself — on that same trial, dropping censored subjects gives a
  median of 10 and counting them as events gives 16, where the answer is 23.
  The event indicator is therefore required, not inferred. A median that the
  curve never reaches is `unknown`, not the largest observed time.

  **`cox_ph`** completes it: the proportional-hazards model, fitted through the
  partial likelihood so the baseline hazard cancels, reproducing the published
  fit of that same trial to four decimals (β = 1.5092, HR = 4.523, SE = 0.4096,
  p = 0.00023). `hr_per` reports the ratio over a stated interval, because a
  hazard ratio is per unit and a covariate in dollars otherwise reads as no
  effect.

- **Meta-analysis in `stats`** — `meta_analysis`, `smd_variance`,
  `eggers_test`. Fixed-effect and random-effects (DerSimonian–Laird) pooling,
  always reported beside Cochran's Q, I² and τ², because a pooled estimate over
  wildly heterogeneous studies is a precise summary of nothing.

  **Ratio measures pool on the log scale.** An odds, risk or hazard ratio is
  multiplicative: 0.5 and 2.0 are the same effect in opposite directions, so
  the true pooled effect is *none*, yet averaged as plain numbers they give
  1.25 — a 25% apparent harm. Nothing can detect the mistake from the values,
  so `scale: "ratio"` is explicit and back-transforms the estimate and its
  interval.

- **Event studies in `stats`** — `event_window`, `abnormal_returns` and
  `event_study`. The method that turns an EDGAR filing date into a testable
  claim: estimate a normal-return model (market, market-adjusted or mean)
  before an event, take the residuals across the event window as abnormal
  returns, cumulate to CAR, and aggregate across events to CAAR with a t-test.

  Four traps are refused rather than left to the caller, each of which yields a
  plausible *number* rather than an error: windows count **trading days**, not
  calendar days; an event on a day the market was shut moves to the next
  trading day and reports that it moved; an estimation window overlapping its
  own event window is refused (look-ahead); and a CAAR over unequal windows is
  refused. A fifth — **contaminated estimation windows**, where clustered
  events sit inside each other's baselines — is *reported* rather than refused,
  since clustering is sometimes unavoidable; on a constructed pair whose true
  CAAR is exactly 0.025 it produces 0.02455, close enough to read as noise.

- **`market` — daily price history** (`stdlib/market.bas`). The finance stack
  was complete except for its input: `stats.simple_returns`, `sharpe_ratio`,
  `max_drawdown`, `value_at_risk`, `capm` and
  `forensics.altman_classic(facts, prices)` all take prices as an *argument*,
  and nothing produced them — EDGAR serves filings, not quotes. `market.daily`
  returns `{ok, frame, adjusted, message}`, and the frame is the shape both
  consumers already want (`forensics` indexes by column; `frame["close"]` is
  the flat array `stats` takes). Providers are pluggable — Stooq needs no key,
  Tiingo is adjusted — behind the `offline`/`with_transport` seams `llm` and
  `edgar` use, so tests never reach the network.

  Two guarantees, because both failures produce a plausible *number* rather
  than an error: rows are always sorted **ascending by date** (a reversed
  series yields negated returns, which looks like ordinary market data), and
  **`adjusted` reports what the provider supplies** rather than being assumed
  (returns across a split from unadjusted prices read as a −50% day).

  **An adjusted provider's adjustment applies to every price column**, not just
  the close. Tiingo serves `adjOpen`/`adjHigh`/`adjLow`/`adjVolume` beside
  `adjClose`; taking the adjusted close while leaving the rest raw puts the
  columns on different scales either side of a split or dividend. Measured
  against six months of real AAPL data, that produced a close **below its own
  low on 89 of 124 rows** — and no single number looks wrong, which is why it
  survived until the live wire format was read.

  **Provider reality, checked live rather than assumed:** Tiingo is **verified
  working** (2026-08-26, real free-tier key). Keyless daily equity data has
  largely gone. Stooq answers any HTTP client with a JavaScript
  anti-bot challenge (HTTP 200, an HTML body, no data, regardless of
  user-agent) and Yahoo's chart endpoint returned 429. A keyed provider is the
  reliable path. `daily` names a challenge page and a rate limit for what they
  are, instead of reporting "no rows" and sending you to look for a bad symbol.

- **Fixed: `round` coerced where every other numeric builtin refuses.** It ran
  its arguments through a zero-defaulting conversion, so `round(record, 2)`,
  `round(array, 2)` and `round("3.14", 2)` all answered **0** — silently. Every
  neighbour (`sqrt`, `abs`, `floor`, `ceil`, `exp`, `log`, `log10`, `erf`,
  `erfc`, `lgamma`, `sign`, `pow`) raises `<fn> expects a number` and refuses
  numeric strings and booleans too; measured across all twelve, `round` was the
  sole outlier, which makes it a bug rather than a policy. Found by dogfooding:
  `round(stats.max_drawdown(prices), 6)` printed `0`, reading as "this series
  never fell" — `max_drawdown` returns a record. Now raises
  `round expects a number` / `round places must be a number`; four pinned
  negatives; no test in the tree depended on the coercion.


- **Compound assignment — `+=`, `-=`, `*=`, `/=`.** `x op= e` means exactly
  `x = x op e`, so it inherits every type rule and every refusal the operator
  already has: it works on numbers, strings, `date + duration`, money and
  durations, through record fields and array indexes, and `list += [1]` raises
  precisely as `list = list + [1]` does. With a modifier the modifier applies to
  the folded result (`name{upper} += "cd"` on `"ab"` gives `"ABCD"`).
  Statement-level only — there is no `y = (x += 1)`.

- **`for` loops may close with `next`.** `next`, `next <name>` and `end for` are
  the same statement; all existing `end for` code is untouched. A named
  terminator must name the loop it closes — classic BASIC let `next x` close an
  inner `y` loop by implicitly closing both, so a one-letter typo silently
  restructured the program, and that is refused here at load time. Costs no
  reserved word: `next` remains usable as an ordinary variable, as do `loop`
  and `until`.

- **`break` and `continue` may name a loop.** `continue x` abandons the inner
  loop and takes the next iteration of the loop over `x`; `break x` leaves it
  entirely. The name is a loop variable, so a named flow passes straight
  through any `while` or `do` in between. Naming a loop that does not enclose
  the statement is a located runtime error (`break: no enclosing loop named
  'zzz'`) — previously a break reaching the top level set a nonzero exit and
  printed **nothing**, a silent path that was unreachable before named flows
  existed and is now closed.

- **`default(value, fallback)`** — the value, unless there isn't one. Returns
  `fallback` when `value` is `unknown` *or* `nothing`, because the two commonest
  producers of an absent result split across them (`env` yields `unknown` when
  unset, `find` yields `nothing` on a miss). Tests presence, not truthiness:
  `false`, `0` and `""` are values and come back unchanged.

## 0.1.0-rc8 — 2026-08-25

**The DOGFOOD ledger's "worth fixing" list is now empty.** The last four items,
closed together.

One **breaking** change: `crypto.json_encode` is removed (see below). Everything
else is additive.

### Password-based key derivation

`pbkdf2_sha256(password, salt, iterations, length)`, `pbkdf2_sha512(...)` and
`scrypt(password, salt, n, r, p, length)` — RFC 8018 and RFC 7914, returning raw
key bytes ready for `aes_gcm_encrypt`. `crypto` had hashing, HMAC and AEAD but no
KDF, so a **passphrase** could not safely become a key, and gBASIC Studio
declined to offer passphrase-protected secrets rather than ship a single-round
hash that looks like one.

Verified against INDEPENDENT implementations — python3 `hashlib.pbkdf2_hmac`, and
RFC 7914 §12 — never against gBASIC itself. A KDF that agrees only with itself
proves nothing: a shared bug still round-trips, and the derived key is simply
weak.

Two decisions worth knowing. **An empty salt is refused**, though RFC 8018
permits one: it turns a KDF into a plain iterated hash and nothing about the
result looks different. **The cost parameters are not floored**, because RFC 6070
and RFC 7914 publish vectors with deliberately tiny costs and a floor would make
the implementation untestable against the vectors that prove it right — so the
recommended values are in `docs/reference.md` where a reader sees them.

### `xlsx.try_open`

`xlsx.try_open(path)` → `{ok, workbook, message}`, the `try_decode` shape. One
malformed workbook used to end an entire corpus scan, which is why the
15,871-workbook Enron scan ran one process per file.

`open` and `try_open` share **one** code path. A `try_` twin that accepts a file
its raising sibling rejects — or reports a different reason for the same file —
invites you to trust a verdict the real function does not share. A non-path
*argument* still raises from both: that is a bug in the caller, not a bad
workbook.

### Removed: `crypto.json_encode`

Use the core `json_encode` — which is what an unqualified call already reached.
Once `json_encode` became a builtin, the library's flat copy was unreachable
except when spelled `crypto.json_encode`, and the runtime warned on every
`load crypto` that it was being shadowed. `jwt_encode` now preflights with
`json_encodable` and calls the builtin, so a claim JSON cannot represent is
refused rather than quietly signed as `null`.

`crypto.json_decode` **stays**, and not for symmetry: it reads attacker-supplied
token payloads and accepts RFC 8259 only, where `try_decode` deliberately speaks
the permissive gBASIC dialect.

### Fixed: `crypto.json_decode` raised on malformed input

Its contract is `unknown` for anything out of domain, and on attacker-supplied
input a raise is a denial of service rather than a rejection. Value dispatch fell
through to a number for every character that is not `"`, `t`, `f` or `n`, so
`{"a":inf}` reached `number("")` and ended the program. It now scans RFC 8259's
number grammar, which also refuses `+1`, `1.2.3`, `1e`, `01` and a magnitude no
double can hold. 32 hostile payloads are pinned in
`examples/crypto_json_hostile_test.bas`.

### Documentation

The three ledger doc-gaps — typed-value construction, the
library-dependency-inside-the-block rule, and `gtk.application`'s single-instance
default — are written into `docs/reference.md`, along with `list` / `list_files`
and the recursive-walk idiom, none of which were documented at all.

The first turned out to be the small part of a bigger problem: the **Modifiers**
section was stale from rc6 in three ways. It described the paren spelling as
merely deprecated, said parenthesized *assignment* modifiers were not deprecated
(they were removed), and said modifiers do not apply to call results (they do).
Every claim there is now checked against a running program.

## 0.1.0-rc7 — 2026-08-24

Additive: no compatibility break. Four fixes, three of them from the DOGFOOD
ledger, and each closes a failure that was **silent** — a wrong answer, a
truncated program, or a file that could not be read back, with nothing on stderr
and nothing in the exit code to say so.

### Fixed: a child no longer outlives a killed interpreter

`docs/reference.md` promises in bold that nothing the interpreter starts
outlives it, and that promise was kept by a teardown pass at the end of the
program — which does not run when the interpreter is `SIGKILL`ed. The DOGFOOD
ledger recorded the consequence: four gBASIC children found sleeping two days
after the runs that started them, three with their working directory already
deleted.

Every child now arms a **parent-death signal** in the kernel between fork and
exec, so it receives `SIGTERM` the moment the interpreter dies, however it dies.
Spawned actors have always done this; `process.start` and `process.run` children
never did.

**Behaviour change:** `process.start` was never a documented way to launch a
process that outlives the program, and it is now definitively not one. A child
that should survive belongs to a service manager. (A child that ignores
`SIGTERM` still survives, as with `process.stop`; a set-user-ID executable loses
the armed signal at `exec`.)

Also fixed by the same change: a spawned actor exited immediately at startup
when the interpreter itself was pid 1 — a container entry point — because the
race check it used, `getppid() == 1`, cannot tell "my parent is gone" from "my
parent is init". Arming before the exec compares against the spawner's recorded
pid instead. `tests/run_process_lifetime.sh`.

### Fixed: `encode`/`decode` round-trips non-finite numbers

The `encode`/`decode` dialect has one promise — an exact gBASIC-to-gBASIC round
trip — and it did not hold for the values IEEE arithmetic produces. `encode`
wrote bare `inf` / `nan`; its own `decode` refused them. A program could write a
file it could not read back, with no diagnostic on either side, and ordinary
overflow reaches that state quietly: `number("1e308") * 10` is infinity.

`decode` now accepts the four spellings `encode` emits (`inf`, `-inf`, `nan`,
`-nan`), which are the same text `print` and `string` show and the same values
`serialize`/`deserialize` already round-tripped. `encode`'s output is byte-for-
byte unchanged.

**The wire parser is untouched and stays strict.** A JSON request or response
body still cannot carry `inf`, `nan`, `nothing` or `unknown` — RFC 8259 has no
syntax for them, and `json_encode` / `json_encodable` still refuse non-finite
values. `tests/webserver_client.py` posts each of them to a live server to prove
it.

(`-inf` decoded correctly the whole time, by accident: `strtod` parses it and a
leading `-` entered the number branch. One spelling of four working is what made
this a bug rather than a policy.)

### Fixed: a reported parse error no longer exits 0

A token the grammar has no place for — `dim x`, or any byte the lexer cannot
read — was signalled to the parser as **end of file**, and the parser cannot
tell a synthetic EOF from a real one. Inside a `program` block that produced a
syntax error, so the defect was invisible there. At **top level** it did not:
the grammar allows a program to end, so the file was accepted as whatever
preceded the bad token. The statements before it ran, everything after it
silently disappeared, and the process **exited 0**.

Both halves are fixed. Every token diagnostic now goes through the diagnostics
sink — located, and carried by `--json-diagnostics`, which previously received
a bare non-JSON line in the middle of a JSON stream — and a parse that reported
a diagnostic fails even when the parser accepted.

`dim` keeps its reserved status for exactly one purpose: it is now a located
parse error that says to assign instead, which is what a reader arriving from
QBasic needs to be told. `tests/run_parse_exit.sh`.

### `mod`, `concat` and `merge`

Three of the oldest DOGFOOD ledger items, closed together.

- **`mod(a, b)`** — the remainder, **floored**: the result takes the sign of
  the divisor, so `mod(-7, 3)` is `2`. **This differs from QBasic's `MOD`**,
  which truncates, and the divergence is deliberate. gBASIC has had no modulo,
  so the documented workaround was `a - floor(a/b)*b` — which is floored — and
  the libraries written against that advice depend on it;
  `stdlib/forensics.bas`'s civil-date algorithm is correct for negative years
  only under floored semantics. Shipping truncated would have silently
  disagreed with every workaround the builtin replaces. `mod(a, 0)` raises.

- **`concat(a, b, …)`** — one new array with the elements of each, in order.
  Variadic; sources untouched.

- **`merge(a, b, …)`** — one new record with the fields of each, **later
  winning** on a duplicate key, so `merge(defaults, overrides)` reads the way
  it looks. Shallow; sources untouched. This is the answer to composing onto a
  library's return value, which previously required binding and field-assigning
  in three lines.

All three are **builtins**. The infix `%`, array `+` and record `+` remain
separate decisions: `%` is lexer work, and whether `+` on a container
concatenates or adds element-wise should not be settled as a side effect of
adding a convenience.

## 0.1.0-rc6 — 2026-08-24

Two syntax changes, both aimed at the same thing: the language was spending
ambiguity and vocabulary it did not need to spend.

### Breaking: modifier clauses are written in braces

- **`x{USD} = 19.95`**, not `x(USD)= 19.95`. Every modifier position moves:
  assignment, comparison, library-qualified (`name{text.caseless}`), and with
  arguments (`s{join ", "}`). The brace form already existed for comparisons;
  this finishes it and retires the paren spelling.

- **Why.** `name(caseless) = "joe"` and `kind(x) = "record"` were the same
  tokens in the same order, so the parser had to GUESS which was a clause and
  which was a call — ninety lines of lookahead whose own comment admitted the
  identifier-argument case could not be closed at token delivery
  (`docs/gbasic_clause_recognition.md` §9). It did not fail cleanly: it parsed,
  ran, and died with `compare modifier not found: x`, naming the caller's own
  argument as a missing modifier. A brace cannot open a call, so there is
  nothing to guess. The guesser and the `MOD_LPAREN`/`MOD_CONTENT` tokens are
  deleted, and `(` means a call or grouping and nothing else.

- **Also fixed by the move:** a modifier on a call result
  (`getname(){caseless} = "joe"`) was refused by the paren form and is
  meaningful in the brace form, matching the lens-on-any-operand rule that
  always applied to literals. `tests/negative_function_result_modifier.*`
  retired.

- **Migration:** 699 clauses across both repositories, driven by the closed set
  of modifier NAMES rather than by punctuation — an ordinary call comparison
  must not be touched. `tests/run_brace_modifiers.sh`.

### Keywords may be field names after a dot

- `r = { end: 1, on: 2 }` and now `r.end`, `r.on`. A field name is a closed
  context, so a keyword there is unambiguous. Until now the language could
  build a field the dot form could not read, which had forced four renames in
  shipped designs.

## 0.1.0-rc5 — 2026-08-24

Everything since **rc2**. The rc3 and rc4 tags were cut without CHANGELOG
sections, so their content is folded in here rather than reconstructed from
memory; `git log v0.1.0-rc2..v0.1.0-rc4` is the authority on which of the
entries below shipped when.

The theme of rc5 itself is **failures that were not saying so**: the error model
was rebuilt so a function can catch one, a warning channel was added so advice
can be suppressed or made fatal, and four failures that reported without a
diagnostic became raises.

### Silent failures promoted to raises

- **An out-of-range array read** now raises (`error.source` `"indexing"`),
  matching the assignment path, which always did. It used to print an
  **unlocated** line, yield `nothing`, and leave the exit code at 0 — and since
  `nothing` is a legitimate value, callers could not tell the failure from a
  real one and CI saw success.

- **`goto` / `gosub` to a label that does not exist** now raises
  (`"invalid control flow"`). It used to print and then abandon the rest of the
  function, so a typo'd label silently truncated it.

- **The `date`, `datetime`, `time`, `file` and `dir` modifiers** now raise when
  they cannot construct a value (`error.source` `"datetime"` or `"modifier"`),
  matching `USD`, which raised four lines away in the same dispatch function.
  They used to print and assign `nothing` — so `d(date) = user_input` silently
  produced a `nothing` that flowed onward. `docs/text_design.md` and
  `stdlib/ari.bas` had both *claimed* these raised for months; the claim was
  measured, found false, and made true rather than weakened.

- **A raise inside a watcher body** now stops the drain instead of being
  dropped. Previously the watcher never fired, draining continued, and the
  program produced results built on a watcher that had not run — with the
  diagnostic surfacing only at exit.

All are now located, fatal by default, and catchable with `on error goto next`
— which a printed line never was. `tests/run_silent_traps.sh`.

### The warning channel

- **`on warning print | ignore | goto next | stop`** — a second diagnostic
  channel for advice, read with `if warning then` and `warning.message` exactly
  as errors are. `on warning stop` is the `-Werror` of a language with no build
  step: put it in `main` and every warning becomes a raise. `on warning ignore`
  is the opt-out that makes aggressive diagnostics possible at all.

- **Two deliberate differences from errors.** The anti-silence rules do NOT
  apply — an unacknowledged warning dies with its frame, because advice that
  must be acknowledged is not advice. And mode lookup is **dynamic**, outward to
  the nearest explicit setting, rather than frame-local: *a failure is the
  callee's business; the noise budget is the caller's.*

- **`warning` is not a reserved word.** It is a soft name, resolved only when no
  variable of that name is in scope, so `warning = 1`, `r.warning` and
  `{ warning: … }` keep working. Raise one with `warning("msg")` or
  `warning({ message: "…", extra: x })`. Note a typo'd variable called
  `warning` therefore reads `false` rather than raising.

- **New diagnostic: `unused-result`.** Discarding a non-`nothing` return from a
  gBASIC-defined function now warns. A function cannot change its caller, so
  every update API returns the new value and calling one for effect does
  nothing — the mechanism that let a worker pool supervise nobody through a
  tagged release. Builtins are exempt (`append` mutates in place by design) and
  `return nothing`, the void convention, is exempt by value. Turning it on
  found three real sites in the standard library.

### Breaking: `on error` is frame-scoped, and `on error resume next` is gone

- **`on error` now governs only the frame that executed it** — one function
  invocation, or the top level. A function you call starts in the default state
  whatever you armed, and your arming dies with your frame. The consequence is
  the point: **a function can catch a raise and return a clean fallback**, which
  the old process-global mode provably could not do (the caller's statement was
  abandoned by a generation check regardless of what the callee returned, and
  `error.clear()` did not rescue it).

  ```basic
  function safe_div(a, b)
      on error goto next
      q = a / b
      if error then
          return -1          ' the caller never knows
      end if
      return q
  end function
  ```

- **`on error resume next` is removed**; `resume` is an ordinary identifier
  again. Migrate to `on error goto next` — the checks you already wrote keep
  working, under semantics that no longer poison the caller. `on error goto
  <label>` and `on error stop` are unchanged in spelling, frame-scoped in
  meaning. Net keyword count: −1.

- **Two rules make deferred checking safe.** A second raise arriving while one
  is still unacknowledged *escapes* the frame rather than shadowing the first;
  and returning — or ending the program — with an unacknowledged error re-raises
  at the call site. Together, no raise can vanish: forgetting a check produces
  noise, never silence.

- **Bare `error` acknowledges; `error.field` does not.** `if error then` is true
  exactly once per raise (so no stale-state trap), and `e = error` acknowledges
  and snapshots in one move, while `error.message` and friends read without
  claiming — which is what lets the block body describe what it caught.

- **Structured raises and traces.** `error { message: "...", balance: b }` raises
  with the extra fields on `error.details`, so a library can ship error *data*
  instead of a string to match on. `error e` re-raises a snapshot, preserving its
  original location and `error.trace` — an array of `{name, path, line, column}`,
  innermost first.

- The fatal stderr line is **byte-identical** to before, which is why all 333
  negative-suite cases pass this change unmodified. Design:
  `docs/error_model_design.md`; proof: `tests/run_error_model.sh` (17 cases) and
  `examples/on_error_goto_next_test.bas`.

- **`real_path(p)` and `file_type(p)`** — two filesystem questions gBASIC could
  not ask. `real_path` returns the canonical absolute path with `.`, `..` and
  every symlink resolved by the kernel, or `unknown` when the path does not
  exist; a path containing an interior NUL is refused rather than truncated.
  `file_type` returns `"file"`, `"folder"` or `"other"` (or `unknown`), and is
  the only way to ask whether a path is a directory **without raising** —
  `file_size` on a directory raises, and a raise cannot be caught, so code
  holding an untrusted path had no safe way to ask. Together they are what a
  containment check needs: a "starts with the root" test on the path a client
  sent can be walked out of with `..` and cannot see a symlink at all; the same
  test on the resolved path cannot.

- **`web.static(relative, root)`** — serve one file from under a root, with
  canonicalize-then-check: the path is resolved first and containment tested on
  the answer, on a separator boundary so a root of `pub` does not match
  `pub-secret`. A path resolving outside the root is 403 even when the file is
  really there; a directory is 404 rather than a listing; unknown extensions
  are served as `application/octet-stream` rather than guessed at. The body is
  read whole, so this is for pages and assets, not large downloads.

- **`web` — a route table as data** (`stdlib/web.bas`, `docs/web_routing.md`).
  Routes are `{ method, path, handler }` records validated when the table is
  built, so an unknown verb, a malformed pattern, an uncallable handler or two
  routes that can never be told apart raise at startup rather than becoming a
  404 at 3am. `{id}` captures one segment and `{rest...}` the remainder, both
  reaching the handler as `req.params`. Matching is decided by specificity —
  static beats `{id}` beats `{rest...}` — so `/products/new` wins over
  `/products/{id}` however the table is ordered. `web.dispatch` returns a
  response record the WebServer takes verbatim, answering 404 for an unknown
  path and **405 with an `allow` header** for a known path and the wrong verb.
  `web.resolve` is the same matching with no handler called, and `web.paths`
  reports the table as sorted `"METHOD /path"` lines.

- **`webserver.listen` can bind an address** — `webserver.listen(8080,
  { address: "0.0.0.0" })`. Omitting the option still binds `127.0.0.1`, so a
  server stays private until its author publishes it deliberately. `address`
  takes a numeric IPv4 or IPv6 address; a hostname is refused rather than
  resolved (no name lookup at bind time), and an unknown option field is
  refused by name rather than ignored. The returned record gains `address`,
  reported by the socket itself the way `port` already is. A dual-stack
  listener (`"::"`) reports IPv4 peers in `request.remote_ip` as ordinary
  dotted quads rather than `::ffff:`-mapped, so address comparisons behave the
  same on either kind of listener.

- **Named, first-class watchers** — `watch recalc(a, b) … end watch` registers
  the watcher and binds `recalc` to a watcher value: `unwatch recalc` turns it
  off (a quiet no-op on an already-off handle), `watchers()` returns the live
  handles, `.name`/`.targets` identify one, and re-declaring a bound name
  **replaces** the old registration so setup code is safe to re-run. Handles
  compare by identity (`=`/`!=` only) and are refused by `encode` and actor
  `send`; named declarations are top-level only. The anonymous `watch(...)`
  form is unchanged. `unwatch` is a new reserved word.

---

## 0.1.0-rc2 — 2026-08-20

Five days after rc1: the datetime/duration redesign in full, two loop
constructs, and an xlsx correctness campaign measured against 15,871 real
Enron workbooks. Still a release **candidate** — the CLA question is open and
0.1.0 final waits on it.

### Language

- **Counted `for`** — `for i = a to b [step c] … end for`. The counter keeps
  its last value after the loop (this differs from QBasic).
- **Post-test loops** — `do … loop until <expr>` and `do … loop while <expr>`.
  `loop` and `until` remain usable as variable names and as `goto` labels.
- Modifier verbs accept the base spelling alongside the participle:
  `(upper)=`, `(lower)=`, `(trim)=` now work like `(uppered)=` and kin.
- `p = $19.99` now fails with a teaching error — money is a modifier
  (`p(USD)= 19.99`), not a literal, and the message says so.
- A runtime error inside a `load`ed library now names that library and the
  line inside it, instead of pointing at the caller's `load` line.

### Datetime and duration (the redesign — breaking changes)

The whole layer was redesigned; `docs/datetime_design.md` is the contract and
`docs/datetime_cookbook.md` (12 executable, suite-enforced recipes) the tour.

- **Month arithmetic uses the accountant's rule**: `jan31 + 1 month` is
  Feb 28, not Mar 3 — years/months clamp the day, then exact parts apply.
  Round-trips deliberately do not hold at month-end.
- **Durations are a (months, seconds) pair, never blurred**: `1 month =
  30 days` is now *false* (it was true — and simultaneously true for
  31 days). Ordering month-bearing durations against exact ones refuses.
  Signed durations; `datetime − datetime` yields a signed exact duration;
  `×`/`÷` with canonical results.
- **Dot extraction** — `d.year`, `d.month`, `d.day`, `d.weekday` (ISO:
  Monday=1…Sunday=7), `d.time` (exact duration since midnight), and kin.
  Reading a field finer than the value's declared precision yields `unknown`.
- **Business calendars as data** (`stdlib/dates.bas`) — `dates.calendar(spec)`
  with `weekend:`, `holidays:` (user-supplied data by design; gBASIC ships no
  national packs), `hours:`, `observe: "nearest"|"forward"` for observed
  holidays; `is_business_day`, `next/previous_business_day`,
  `add_business_days`, `business_days_between`, `dates.merge` (mutual
  working days obey the conjunction law), `dates.between`.
- **Recurrence as data** — `dates.matches(d, rule)`, `dates.select(rule,
  range)`, `dates.series(rule, bounds)`: `nth:`/`weekday:`/`day:`/`month:`/
  `when:`/`except:`/`roll:` vocabulary ("every third Thursday", "first
  Tuesday after the 15th", RRULE BYDAY/BYSETPOS/BYMONTH shapes). A miss is
  `unknown`; a malformed rule raises.
- **Business-hours arithmetic** — `add_business_hours` (an SLA clock that
  pauses overnight, over weekends and holidays), `business_hours_between`,
  `is_business_time`, with the round-trip law tested.
- **Timezones at the edges** — `to_zone`/`from_zone`/`zone_offset`/
  `zone_resolve` over IANA names; UTC timeline, civil calendar. DST policy
  matches Temporal's "compatible"; unknown zones are refused rather than
  silently UTC; all-day values are refused (no instant).
- **Scheduling** (`stdlib/schedule.bas`) — `slots` (a physician-style
  appointment grid) and `layout` (sessions packed into business days,
  bumping over breaks; the oversized are reported by name in `unplaced:`).

### xlsx

Measured against the full 15,871-workbook Enron corpus, cells with formulas
judged against Excel's own cached results: **disagreeing cells fell from
461,578 to 64,227 and fully-agreeing workbooks rose from 91.1% to 95.7%**
(`docs/xlsx_design.md` §13.Z–§13.AE record every step and its measurement).

- **Defined names** — `<definedNames>` resolve by lexer-level splice,
  including sheet-qualified scope (`Sheet!name`) and local-over-global
  shadowing; names for ranges flatten correctly in argument positions.
- **Implicit intersection** — a range in a scalar slot takes its element on
  the formula's own row/column, per Excel's pre-dynamic-array rule.
- **Coercion fixed to Excel's rules** — the empty *string* does not coerce
  to a number (`""+1` is `#VALUE!`) while the empty *cell* is 0; text dates
  in `DD-MMM-YYYY` coerce (English month names, deliberately locale-narrow).
- **Lookup/criteria empty-cell rules** — an empty-cell lookup key and an
  empty-cell criteria are 0 (`VLOOKUP`/`HLOOKUP`/`MATCH`, the IF-criteria
  family); the empty *string* stays text in both places.
- **Deleted-reference literals** — `Sheet!#REF!` in formula text evaluates
  to `#REF!` instead of failing to tokenize.
- **SUMIF-family error handling** — errors on non-matching rows are skipped;
  a matched cell's error still propagates. Empty arguments (`SUM(1,,2)`,
  trailing commas) contribute empty, not `#VALUE!`.
- **Honest refusals, priced by name** — CSE array formulas (`t="array"`),
  external-workbook references *and* external-workbook defined names
  (`[1]!Name`) are reported unavailable rather than answered plausibly wrong;
  recalc never overwrites a cached value it cannot recompute.
- Corpus instruments committed: `tools/xlsx_corpus_*.sh` (check / report /
  blockers / disagree), frozen-binary + per-worker-file methodology.

### Documentation

- `docs/datetime_cookbook.md` — 12 recipes, executable and suite-enforced
  like the xlsx cookbook.
- A first-twenty-minutes on-ramp for newcomers (tutorial + UNLEARN "names
  that nearly work").
- `docs/xlsx_design.md` §13.Z–AE — the corpus campaign, each fix measured.

---

## 0.1.0-rc1 — 2026-08-15

The first tagged release. gBASIC has been developed since 2026-05-02 (366
commits) without a prior tag, so this entry describes the shipped surface by
subsystem rather than diffing against a previous version.

A release **candidate** rather than 0.1.0: three defects that prevented the
project from building at all on current Ubuntu were found and fixed on the day
this was cut (see *Portability* below), and none of them were caught by the test
suite. That is a statement about how little exposure the build has had outside
one developer machine, and an rc gives the packaging configurations a chance to
be exercised by someone else first.

### Language and runtime

- Tree-walking interpreter for a BASIC-family language, in C11 with no required
  third-party dependencies. `gbasic` lexes, parses and evaluates `.bas`/`.gb`.
- Values: numbers, strings (binary-safe, UTF-8 aware), booleans, arrays,
  records, dates/times, durations, money, files, functions, regexes, plus
  `unknown` and `nothing` as distinct absences.
- Records and arrays are shared, refcounted and copy-on-write, preserving value
  semantics without copying on every read.
- Modifiers (`(...)=` clauses), watchers, `consider` blocks, locks, structured
  errors, `on error resume next`.
- **Policy-Based Inheritance (PBI)** — `copy`/`link`/`reset`/`exclude` field
  policies with `new` derivation.
- **First-class functions** — function values, methods via `this`, dotted-def
  attachment, `constructor`. (Closures are *not* implemented.)
- **Multiprocessing** — shared-nothing actors over fork+exec, `spawn`/`send`/
  `receive`/`self`, selective receive with timeout, handle passing over
  `SCM_RIGHTS`, and `monitor`/`demonitor` death notification.
- **Unicode** — codepoint operations, byte builtins, `\u{}` escapes.
- **Regex as a value kind**, overloading `contains`/`replace`/`split`, with
  `match`/`match_all` for the cases a literal API cannot express.
- Bitwise builtins (`band`/`bor`/`bxor`/`bnot`/`shl`/`shr`/`rotl`/`rotr`).

### Language additions since 0.1.0-rc1

- **Counted `for`** — `for i = a to b [step c] … end for`. gBASIC previously had
  only `while` and `for each`, so every counted loop was a hand-rolled counter;
  that idiom appeared 22 times in shipped code, including the standard library.
  `to` is inclusive, `step` defaults to 1 and may be negative or fractional,
  bounds are evaluated once at entry, and `step 0` raises rather than hanging.
- **Post-test loop** — `do … loop until c` and `do … loop while c`, for the
  "run at least once, then decide" shape `while` cannot express. There is no
  pre-test `do while … loop`, because `while` already is one, and no
  `repeat … until`, because `repeat` is a string builtin. `loop` and `until`
  never begin a statement and so remain usable as variable names and as labels;
  `do` does, and is reserved like `while` and `for`.

### Datetime and duration arithmetic (docs/datetime_design.md §4)

The floor of the datetime redesign, and three genuine bug fixes:

- **`Jan 31 + 1 month` is now `Feb 28`**, not `Mar 3` — the accountant's rule:
  years and months are added first, the day is clamped to the resulting month,
  then exact parts (weeks/days/hours/minutes/seconds) are added as elapsed
  time. The old behaviour added "the number of days in the starting month",
  which is right everywhere except month-end — where invoices live.
- **Duration comparison worked in no direction and now works in every one.**
  Durations fell through to numeric coercion (the PLAT-EQ defect, fixed for
  arrays and records, missed for durations), so every equality was true and
  every ordering false — `(1 month) = (30 days)` *and* `= (31 days)` were both
  true. Now: equality compares (months, seconds) pairs (`1 year = 12 months`,
  `1 week = 7 days`, `1 month = 30 days` is **false**); ordering is a total
  order on exact durations, and ordering a month-bearing duration is refused —
  a month has no fixed length.
- **The missing arithmetic exists**: `datetime − datetime` → signed exact
  duration; `duration ± duration`; `duration × n` and `/ n` (months scale only
  by integers; seconds round to the whole second). Results are canonical:
  `(45 minutes) * 4` is `3 hours`.

### Datetime component extraction (docs/datetime_design.md §3)

`d.year`, `d.month`, `d.day`, `d.hour`, `d.minute`, `d.second`, `d.weekday`
(ISO Monday=1…Sunday=7), `d.dayname`, `d.day_of_year`, `d.precision`, and
`d.time` (an exact duration since midnight). A field finer than the value's
precision reads as `unknown`; an unknown field *name* raises. Durations answer
their stored components and `total_seconds`, which is refused for
month-bearing durations. Previously there was no way to get 2026 out of a
datetime as a number short of slicing its string.

### Business calendars (docs/datetime_design.md §5, `stdlib/dates.bas`)

Calendars are data — `dates.calendar({ weekend:, holidays:, hours: })`, passed
explicitly to `is_business_day`, `next`/`previous_business_day`,
`add_business_days`, and `business_days_between` (counted over `(a, b]`,
signed, convention stated because half-open intervals are where calendar bugs
live). Holidays are normalised to day precision at construction, so a holiday
supplied as a full timestamp still blocks the day. `dates.merge(cals)` unions
constraints — weekend ∪, holidays ∪, hours intersected — with the tested law
`is_business_day(d, merge([a,b])) = is_business_day(d,a) and
is_business_day(d,b)`, which is why finding mutual meeting days needs no new
search machinery. `dates.between(a, b, "days"|"months"|"years")` answers the
calendar difference, consistent with clamping by construction (Jan 31 → Feb 28
is 1 month, exactly as Jan 31 + 1 month is Feb 28). An empty calendar makes
lookups fail fast rather than hang.

### Date selectors (docs/datetime_design.md §7, `stdlib/dates.bas`)

One spec-record vocabulary, three verbs: `dates.matches(d, spec, cal)`,
`dates.select(spec, anchor, cal)` (the one day, or `unknown` on a miss),
`dates.series(spec, bounds, cal)`. "Third Thursday of the month", "first
Tuesday after the 15th", "first business day before a deadline", "every third
Thursday at 14:00 all year", "payroll every 2 weeks rolled off holidays" are
all one-liners, and every series element satisfies `matches` with the same
rule — the two verbs verify each other in the tests. Strictness lives in the
anchor names (`after` vs `on_or_after`); roll conventions include the finance
`modified` rule; monthly stepping is multiplicative from the start, so
Jan 31 → Feb 28 → **Mar 31**, not Feb-28-forever. The series sub-rule is
`when:` and bounds are `{from:, through:|count:}` — `on` and `to` are keywords
that cannot follow a dot.

### Scheduling (`stdlib/schedule.bas`)

`schedule.slots(day, spec, cal)` cuts a working day into appointment slots
(the physician grid); `schedule.layout(plan, days, cal)` packs ordered
sessions into business days around immovable breaks — sessions keep their
order, one that misses the day end moves **whole** to the next day, and one
that fits nowhere is reported in `unplaced:` rather than dropped. With this,
every planned v1 layer of the datetime redesign is built.

### Two ergonomic debts cleared

- The string modifiers accept both spellings: `(upper)=` beside `(uppered)=`,
  likewise `lower`/`lowered` and `trim`/`trimmed`. The near-miss
  (`assign modifier not found: upper`) was the most-hit trap in UNLEARN; the
  modifier namespace is separate from builtins, so `(upper)=` and the function
  `upper()` never collide.
- `p = $19.99` now fails with a teaching error — `'$' is not a money literal;
  write p(USD)= 19.99` — instead of `unexpected token`. Sigils privilege one
  currency and change over time; money stays a modifier from a plain number.

### Observed holidays and month constraints (`stdlib/dates.bas`)

`dates.calendar({ …, observe: "nearest" | "forward" })` moves a weekend
holiday's day off to a working day — nearest free weekday with ties forward
(the US federal rule; July 4 2026 is a Saturday, observed Friday July 3), or
always forward (the UK substitute-day style). Chained weekend holidays take
consecutive weekdays. Computed once at construction, so every downstream verb
inherits it. Specs also gain `month:` (a number or list — RRULE's BYMONTH),
so "the 15th of January and July" is one rule.

### Business-hours arithmetic (`stdlib/dates.bas`)

`add_business_hours` (signed), `business_hours_between` (signed), and
`is_business_time` — working time that pauses overnight, across weekends and
holidays. The clock starts at the next open; a deadline exhausting exactly at
close is due at close (rolling would silently extend an SLA); the window is
half-open; only exact durations are accepted. The round-trip law
`between(a, add(a, n)) = n` is tested over mixed durations.

### Timezones (docs/datetime_design.md §9)

`to_zone` / `from_zone` / `zone_offset` / `zone_resolve`, core builtins in the
epoch family over the system IANA database. UTC for the timeline, civil time
for the calendar, zone names at the edges — no zone field on datetimes, no new
kind. DST edges are named, never guessed: the compatible default (ambiguous →
earlier, gap → shifted forward) with `zone_resolve` exposing both instants.
Unknown zones and all-day values are refused — glibc's silent UTC fallback on
a bad `TZ` is exactly the plausible-wrong-answer class this design refuses.
Safe with actors (processes, not threads); `TZ` is saved and restored around
every call.

### Recurrence extension

`when:` without `nth:` in a series emits **every** matching day in the period
— `{ every: "week", when: { weekday: ["monday","wednesday","friday"] } }` is
the Mon/Wed/Fri standup as one rule. This closes the main expressiveness gap
against iCalendar RRULE's `BYDAY` lists; gBASIC's `nth`-over-candidates
already covered `BYSETPOS`. The timezone *position* is now recorded in the
design doc's §9: UTC for the timeline, civil time for the calendar, zone
names at the edges — intentions stored as rule + zone, never as future UTC
instants.

### Documentation (datetime)

`docs/datetime_cookbook.md` — 10 recipes covering the whole datetime surface
(precision, extraction, duration algebra, deadlines and ages, business
calendars, date expressions, recurring schedules, mutual calendars, convention
layout, appointment slots), enforced by `tests/run_datetime_cookbook.sh` with
the same cannot-drift harness as the xlsx cookbook (one shared sync tool). The
tutorial and reference gained the arithmetic rules and the calendar/selector
surface, and the keyword-after-dot trap is recorded in UNLEARN.

### Platform

- `--tokens`, `--ast`, `--add-loads`, `--json-diagnostics`, `--line-buffered`.
- `print to error` — the program's route to standard error.
- `try_decode(text)` — JSON decode that reports failure as a value rather than
  raising, sharing the parser with `decode` so both accept the same dialect.
- `source_outline(text)` — in-process structural outline over a reentrant parser.
- `process.*` — run a child, or start one and poll/read/wait/stop it.
- Filesystem metadata and `atomic_replace`.
- `gbasic-lsp`, a language server (built by `make dev`, not by `make`).

### Performance

Three per-element access patterns that were quadratic are now linear, each by
adding an index behind an unchanged API:

- **Strings** — reading a string variable no longer deep-copies; `len`/`mid`/
  `left`/`right` memoize the codepoint count and keep a sparse index, so
  backward scans are no longer quadratic.
- **Arrays** — shared refcounted storage with copy-on-write.
- **Records** — a hash index from name to slot for records above a size
  threshold, plus the same copy-on-write sharing.

Repeated string concatenation with `+` remains quadratic and is deliberately
used as the negative control in the complexity test tiers.

### Correctness fixes worth calling out

- `print` and `string()` now share **one** renderer. `print` previously emitted
  `[?, ?]` for a string array and the literal `{record}` for a record — a record
  could not be displayed at all. Display is now total and never raises.
- Numbers render as the **shortest decimal that reads back identically**, bare
  or nested. `265550.75` used to print as `265551`.
- `=` on arrays and records is deep and structural. Both sides previously fell
  through to a numeric coercion where any two compound values compared equal,
  which silently affected `contains`, `find`, `remove_value` and `consider`.

### Spreadsheet pipeline (xlsx)

Requires zlib and libxml2.

- Reads and writes `.xlsx` through a hand-written ZIP container and a part tree
  that **discards nothing**, so an existing workbook can be edited and saved
  with every unmodelled part preserved byte-for-byte. Saves are deterministic.
- A formula evaluator validated against Excel's own cached values via
  `xlsx.check`, dependency-ordered recalculation across sheets, shared formulas,
  cross-sheet and external references, and the text/math/lookup/clock families.
- Measured against a 15,871-workbook corpus of real Excel files: **97.38%
  cell-level agreement**, zero read errors, 91.1% of workbooks with no
  disagreement at all.
- Layered libraries above it: `grid` (a messy sheet into clean frames),
  `consolidate` (many differently-shaped sources into one frame), `dbframe`
  (a frame into a SQLite table), and `xlsx.to_sql` / `xlsx.apply` (a column
  formula compiled to SQL or applied vectorised over a frame).

### Statistics

`stdlib/stats.bas` and friends, in pure gBASIC: distributions, matrix toolkit,
OLS, seedable RNG and resampling, data frames, inferential tests, GLMs,
clustering and PCA, time series through ARIMA/GARCH on a shared MLE optimizer,
power analysis, robust standard errors, mediation/moderation, and econometric
diagnostics. Field cookbooks for the social/behavioral and econometrics/finance
clusters.

### EDGAR suite

`stdlib/edgar.bas` plus `fundamentals`, `forensics`, `insiders`, `ownership`,
`mdna`, `screener` and `llm`. Built against real SEC data captured under an
authorized identity (see `examples/fixtures/edgar/MANIFEST.md`). All 33 work
packages in `docs/edgar_suite_development_plan.md` are complete.

Deliberately **not** included: the network forms of `report_13f` and 13D/G
full-text search, grants/exercises, and full-market acceptance against bulk
data. No test performs network access.

### GUI

- `gi` — a generic GObject-Introspection bridge (GTK 4 path), plus `gtk.bas`,
  `sourceeditor`, `gtkui` (a declarative reconciler) and `datagrid`.
- `gui` — the older GTK 3 record-driven module, still an experimental proof of
  concept. Prefer `gi` for new work; the two cannot share a process.

### Other modules

`sqlite`, `pg` (PostgreSQL), `webclient`, `webserver`, `xml` (tree and
streaming), and libcrypto-backed crypto builtins with `stdlib/crypto.bas`.

### License

gBASIC is **dual-licensed**. `LICENSING.md` is the map, and every file declares
its own license with an SPDX identifier.

- **Apache-2.0** (`LICENSE`, verbatim, md5 `3b83ef96387f14655fc854ddc3c6bd57`) —
  the language, the interpreter, every C module compiled into it *including the
  whole xlsx engine*, and 14 of the 24 standard libraries.
- **AGPL-3.0-or-later** (`LICENSE.AGPL-3.0`, verbatim from gnu.org, md5
  `eb1e647870add0502f8f010b19de32af`) — the spreadsheet-to-database pipeline
  (`grid`, `consolidate`, `dbframe`) and the EDGAR securities-analysis suite
  (`edgar`, `fundamentals`, `forensics`, `insiders`, `ownership`, `mdna`,
  `screener`). A commercial license for these is available.

Writing gBASIC programs, or embedding the interpreter, is Apache-2.0 and
unrestricted. The xlsx *engine* is Apache because it compiles into the binary
and could not carry a different license without making the whole interpreter
AGPL; what is AGPL is the layer built on top of it.

No Apache-licensed file depends on an AGPL one — the dependency graph was
checked, and the AGPL libraries are leaves. The docs gate enforces that every
stdlib library declares a license and that it matches `LICENSING.md`.

The repository previously carried no license at all, which meant default
copyright applied and nobody had permission to use it. `make install` places
both license texts, `NOTICE` and `LICENSING.md` under `$PREFIX/share/doc/gbasic`.

### Packaging

- **`make install PREFIX=...` installed a binary that looked somewhere else.**
  The stdlib path is compiled in (`GBASIC_DEFAULT_STDLIB`), but make cannot see a
  changed `-D`, so `make && make install PREFIX=$HOME/.local` — the sequence the
  Makefile itself recommends — installed an already-built binary still pointing
  at `/usr/local`. Nothing errored; `load` simply failed later, or silently
  resolved against a different gBASIC's stdlib. A stamp now invalidates the two
  objects that carry the path, and only those.
- `make install-lsp` installs `gbasic-lsp`, which previously had no supported
  route to a `PATH`. Kept separate from `make install` so a plain install stays
  lean; `make uninstall` removes both, plus the doc directory.

### Portability

- **riscv64** is a supported target; the suite runs on Ubuntu 24.04 riscv64.
- Fixed: `gi_repository_dup_default` does not exist in girepository-2.0 before
  ~2.88 (absent in 2.80.0 and 2.84.1). The build enabled `HAVE_GIR` on
  `pkg-config --exists` with no version floor and then failed to **link**,
  taking the whole binary with it, on Ubuntu 24.04 LTS and 25.04. Now uses
  `gi_repository_new()`.
- Fixed: libxml2's structured-error handler gained a `const` in 2.12.0. Against
  2.9.14 that is a warning under GCC 13 and an **error** under GCC 14, so
  Ubuntu 25.04 could not compile. The signature is now selected on
  `LIBXML_VERSION`.
- Fixed: the GTK 3 `gui` module had not compiled since 2026-07-23, still using
  the array layout that copy-on-write replaced.
- Fixed: `tools/check-deps.sh` named two packages that do not exist on
  Debian/Ubuntu (`libxcrypt-dev`, and `libgirepository1.0-dev` for a
  `girepository-2.0` module). Because `--install` issues a single `apt-get`,
  one bad name meant nothing was installed.
- The example and negative suites now **skip** cases whose module is compiled
  out instead of failing them. A build with no optional dependencies previously
  failed 34 of 182 examples for behaving exactly as documented.

### Documentation

`docs/README.md` indexes every document and marks each **Shipped**, **Proposal**,
**Partial** or **Record**, so a design for unbuilt work cannot be mistaken for a
description of working behaviour. `tests/run_docs_gate.sh` fails if a document is
missing from the index or if the index links to something that does not exist.

Six stale status lines were corrected — `xml`, `pbi`, `ari`, `statistics`,
`edgar` and `llm` all claimed unbuilt what ships with passing goldens. The xml
one had caused a working module to be filed as a release blocker.

`docs/xlsx_cookbook.md` is a 12-recipe tutorial for the spreadsheet library,
covering all fifteen `xlsx.*` calls and the `grid`/`consolidate`/`dbframe`
layers above them. Every code block and every output block on the page is
checked byte-for-byte against a real file in `examples/xlsx_cookbook/` and its
recorded output, so the page cannot drift from the product:
`tools/sync_xlsx_cookbook.sh` copies both in, and `tests/run_xlsx_cookbook.sh`
fails while any of them disagree — including the case a run-only suite would
wave through, where a comment-only edit leaves the output identical.

`CONTRIBUTING.md` covers building, running the suites, and the house rules —
and states plainly that code contributions are not being merged yet, pending a
Contributor License Agreement.

### Testing

CI (`.github/workflows/ci.yml`) builds three configurations on every push: all
optional modules enabled, none enabled, and install-to-a-prefix-then-run-from-it.
Each is a configuration that was genuinely broken and invisible from a developer
machine — which is the failure class tests cannot reach, since a `#if` guard and
a `pkg-config --exists` check are both blind to the configuration they did not
select.

216 example goldens, 303 negative cases and 45 suite runners. Goldens are
compared byte-for-byte. Optional-dependency suites skip cleanly when their
library is absent.

### Known limits

- Not stable. The language surface may change before 1.0.0.
- No closures, no exponent literals (`1e20` lexes as a duration — use
  `number("1e20")`), and repeated string `+` is quadratic.
- `valgrind` has no riscv64 port, so the memory tiers can only skip there;
  ASan/UBSan work but report use-after-free with degraded diagnostics.
- GUI suites need a display and skip without one.
- `use`/`--add-uses` is legacy; prefer `load`/`--add-loads`.
- Many documents in `docs/` are design proposals, not descriptions of shipped
  behavior. Check the status line at the top of each.

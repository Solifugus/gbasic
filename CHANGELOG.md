# Changelog

All notable changes to gBASIC are recorded here.

This project uses [semantic versioning](https://semver.org/). Until 1.0.0 the
language surface may still change between releases.

---

## Unreleased

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

# Changelog

All notable changes to gBASIC are recorded here.

This project uses [semantic versioning](https://semver.org/). Until 1.0.0 the
language surface may still change between releases.

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

gBASIC is released under the **Apache License, Version 2.0** — see `LICENSE`
(verbatim, md5 `3b83ef96387f14655fc854ddc3c6bd57`) and `NOTICE`. The repository
previously carried no license at all, which meant default copyright applied and
nobody had permission to use it. `make install` now places both under
`$PREFIX/share/doc/gbasic`, since Apache-2.0 requires the license to travel with
the work.

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

### Testing

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

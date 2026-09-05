# gBASIC

![Byte Beaver, the gBASIC mascot](docs/assets/mascot.png)

gBASIC is an experimental BASIC-family language for readable, practical
programs: familiar control flow, plus records, first-class functions, watchers,
shared-nothing actors, and typed values for dates, durations and money. Around
it sits a working platform — databases, a hardened web server, spreadsheets,
statistics, charts and native GUI — so a gBASIC program can be a real
application rather than a demonstration.

This repository holds the C implementation of gBASIC `0.1.0` — the **first
release, and an experimental one**. Until 1.0.0 the language surface may change
between releases; [CHANGELOG.md](CHANGELOG.md) says what is solid, what is
experimental within it, and what is not here yet.

**Platform: Linux.** CI builds and runs the full suite on Ubuntu 24.04 LTS and
current Ubuntu on x86-64, and riscv64 is a supported target. macOS and Windows
are **not** tested and no support for them is claimed.

**New here?** [Tutorial](docs/tutorial.md) to learn it ·
[Reference](docs/reference.md) for the details ·
[Documentation index](docs/README.md) for everything else, with each document
marked Shipped, Partial, Proposal or Record.

---

## A taste

```basic
people = [
    {name:"Ada", active:true},
    {name:"Grace", active:false}
]

for each person in people
    if not person.active then continue
    print(person.name + " is active")
end for

if count(people) = 2 then print("Two people loaded")
```

Values carry meaning, and modifiers validate or convert on assignment and
comparison:

<!--needs-context-->
```basic
price {USD}= 19.95
due   {date}= "2026-05-15"
name  {trimmed}= input("Name: ")

if command {caseless}= "quit" then print("Goodbye")
```

Watchers are reactive blocks that fire the moment a stored value actually
changes:

```basic
total = 0

watch(total)
    print(total)
end watch

total = 1
```

The [tutorial](docs/tutorial.md) covers the language properly; the
[reference](docs/reference.md) documents every construct and builtin.

---

## Status

### The language

Implemented: a hand-written lexer, Bison parser, AST and tree-walking
evaluator; strict expressions; compound assignment (`+=` `-=` `*=` `/=`);
`if`/`else if`/`else`, `consider`, `while`, `do…until`; statements that
continue across a line break inside an unclosed `(`, `[` or `{`; `for each` with
`break`/`continue` that may name their loop; arrays, records and nested
assignment; functions, programs, libraries and `load`; first-class function
values; **prototypal objects** with per-property inheritance policies;
assignment and comparison modifiers; watchers and locks; frame-scoped
`on error` and a suppressible warning channel; distinct `nothing` and
`unknown`; date, duration, money, file and directory values; binary-safe
Unicode-aware strings; regular expressions as a value kind; bitwise builtins;
and strict RFC 8259 JSON alongside a round-tripping gBASIC dialect.

Notable absences: **no closures** (a function cannot rebind an enclosing
scalar — see [UNLEARN.md](docs/ai/UNLEARN.md)), no user-defined types beyond
records, and no module system beyond `load`.

### The platform

| Area | State | Needs | Details |
|---|---|---|---|
| **SQLite** | Shipped | sqlite3 | [reference](docs/reference.md#sqlite-module) |
| **PostgreSQL** | Shipped | libpq | [reference](docs/reference.md#postgresql-module) |
| **ODBC** — SQL Server, MySQL, Oracle, DB2, … | Shipped | unixODBC | [cookbook](docs/odbc_cookbook.md) · [reference](docs/reference.md#odbc-module) |
| **WebClient** — synchronous HTTP/HTTPS | Shipped | libcurl | [reference](docs/reference.md#webclient-module) |
| **Mail** — RFC 5322 composition, SMTP with TLS and auth | Shipped | libcurl | [design](docs/mail_design.md) |
| **WebServer** — TLS, routing, static files, streaming, worker pool | Shipped | libssl for TLS | [reference](docs/reference.md#webserver-module) |
| **XML** — tree, lenient HTML, constant-memory streaming | Shipped | libxml2 | [reference](docs/reference.md#xml-module) |
| **xlsx** — reads, edits and recalculates real workbooks | Shipped | zlib + libxml2 | [cookbook](docs/xlsx_cookbook.md) |
| **Process** — run a child, or drive a live one | Shipped | — | [reference](docs/reference.md#process-module) |
| **Actors** — shared-nothing multiprocessing | Shipped | — | [design](docs/multiprocessing_design.md) |
| **Cryptography** — hashing, HMAC, AES-GCM, Ed25519, JWT | Shipped | libcrypto | [reference](docs/reference.md#cryptography) |
| **`gi`** — GObject-Introspection bridge (GTK 4) | Shipped | libgirepository | [tutorial](docs/gui_tutorial.md) · [cookbook](docs/gui_cookbook.md) |
| **`gui`** — GTK 3 declarative windows | Proof of concept | GTK 3 | [reference](docs/reference.md#gui-gtk-3-module--experimental) |

Every optional dependency is detected at build time. **The interpreter always
builds**; a missing library turns its module into a clean runtime error rather
than a build failure.

Two of these deserve a sentence more than a table row:

- **xlsx** *edits* existing spreadsheets rather than only generating new ones.
  The reader keeps every part of a workbook, including parts it does not model,
  so a save does not destroy what it did not understand. It carries a formula
  evaluator, dependency-ordered recalculation across sheets, and `xlsx.check`,
  which scores the engine against the results Excel itself cached in the file.
  Measured against 15,871 real Excel workbooks: **97.38%** of formula cells
  agree, with no disagreement at all in 91.1% of workbooks.

- **WebServer** began as a queue-based toy and is no longer one. It now has
  TLS with SNI, a declarative `server` block with routing and captures, static
  file serving, streaming responses, a process worker pool with rolling reload,
  and request/idle timeouts with smuggling-resistant framing. It still does not
  do WebSockets or chunked request bodies, and it binds loopback unless asked
  otherwise.

### The standard library

Forty pure-gBASIC libraries in `stdlib/`. Each bullet says what backs it,
because they are not at the same maturity and a uniform list would imply they
are.

- **statistics** (`stats`) — descriptive and inferential statistics, regression
  and the GLM suite, mediation/moderation, time series, econometric
  diagnostics, finance metrics, survival analysis, meta-analysis, factor
  analysis, event studies, and causal inference (DiD, IV/2SLS).
  [Social & behavioral cookbook](docs/cookbook_social_behavioral.md) ·
  [econometrics & finance](docs/cookbook_econometrics_finance.md)
- **finance and accounting** (`finance`, `accounting`, `lending`, `deposits`,
  `credit`, `scoring`) — time value of money in Excel's argument order,
  double-entry books that refuse to balance wrongly, loan servicing as an
  auditable fold, deposit interest and certificates, portfolio roll rates and
  vintage curves, and credit scorecards. Five cookbooks, each on a harness that
  fails if the page and the code disagree: [finance](docs/finance_cookbook.md) ·
  [money](docs/money_cookbook.md) · [accounting](docs/accounting_cookbook.md) ·
  [lending](docs/lending_cookbook.md) · [deposits](docs/deposits_cookbook.md) ·
  [credit](docs/credit_cookbook.md)
- **charts** (`chart`) — line, scatter, area, bar, histogram, pie, heatmap and
  sparkline as deterministic SVG text, in pure gBASIC.
  [Cookbook](docs/chart_cookbook.md)
- **spreadsheet-to-database pipeline** (`grid`, `consolidate`, `dbframe`) —
  `grid` turns a messy worksheet into clean frames and reports how confident it
  is, `consolidate` merges differently-shaped sources onto one schema, and
  `dbframe` loads the result into SQLite with column types inferred from every
  value rather than the first.
- **dates and scheduling** (`dates`, `schedule`) — business calendars, date
  expressions, convention layout, appointment slots.
  [Cookbook](docs/datetime_cookbook.md)
- **EDGAR securities analysis** (`edgar`, `fundamentals`, `forensics`,
  `insiders`, `mdna`, `ownership`, `screener`) — SEC data acquisition,
  fundamentals, a forensic scorecard, ownership and insider analysis, MD&A
  extraction with an LLM analyst panel, a market screener and a watcher
  monitor. [Tutorial](docs/edgar_tutorial.md)
- **market data** (`market`) — daily price history as a frame: the input the
  finance and event-study code always needed and nothing produced.
- **fabricated data** (`fake`) — realistic populations that are pure functions
  of (seed, index), so a fixture cannot silently rot, plus `fake.plant` for a
  clean population with a known defect in a known place.
- **GUI toolkits** (`gtk`, `gtkui`, `datagrid`, `sourceeditor`, `gui`) — a
  declarative widget-tree reconciler, a virtualized data grid, and a
  syntax-highlighting editor over `gi`; `gui` is the older GTK 3 proof of
  concept. [Tutorial](docs/gui_tutorial.md)
- **business automation reasoning** (`reasoning`, `insight`, `decision`,
  `automation`) — **experimental, and more so than anything else here.** A
  design laboratory rather than a toolkit: *did anything happen, does it mean
  anything, what should be done about it, and may the software do it*. The
  architecture is closed end to end and is one function or so per layer, its
  default significance threshold is measurably anti-conservative, and every
  measurement behind it is on generated data. Read the
  [caveats in the reference](docs/reference.md) before depending on it.
  [Design](docs/automation_reasoning_design.md)
- **odds and ends** — `web` (routing), `frame` (data frames), `matrix`,
  `persist` (crash-safe versioned storage), `filetree`, `crypto`, `mail`, `llm`,
  `ari` (anchor-relative report parsing).

Two larger example programs live in the repository:
[Adventure](examples/adventure/adventure.bas), a small text adventure, and
[BAG](examples/bag/README.md), a menu-driven BASIC Adventure Generator.

---

## Quick start

On Linux, with a C11 compiler, `make` and `bison`. Everything else is optional:

| Dependency | Enables |
| --- | --- |
| sqlite3 | `load sqlite` |
| libpq | `load pg` |
| unixODBC | `load odbc` |
| libcurl | `load webclient`, `load smtp` |
| libxml2 | `load xml` (and, with zlib, `xlsx`) |
| zlib | `xlsx` |
| libcrypto (OpenSSL) | cryptography builtins, `load crypto` |
| libssl | TLS in the WebServer |
| libxcrypt | `password_hash` / `password_verify` |
| GTK 3 | `load gui` |
| libgirepository-2.0 (GLib ≥ 2.80) | `load gi`, and the GTK 4 libraries above |

```sh
make                                    # build ./gbasic
./gbasic examples/adventure/adventure.bas
```

Install it so `gbasic` is on your `PATH` and finds its standard library with no
environment setup:

```sh
sudo make install                       # /usr/local/bin + /usr/local/share/gbasic/stdlib
make install PREFIX=$HOME/.local        # or without root
```

The install path is baked in at build time, so changing `PREFIX` rebuilds only
the two objects that carry it — no `make clean` needed. `GBASIC_PATH` still
takes precedence when you want a different stdlib, which is how you work in the
source tree: `GBASIC_PATH=stdlib ./gbasic program.bas`. Remove with
`sudo make uninstall`.

### Shipping an application

`make install` is for a developer. To hand an application to someone else,
`packaging/build-deb.sh <app-dir>` builds a `.deb` carrying **its own**
interpreter and standard library, built lean — a server application drops GTK,
GObject-introspection, PostgreSQL, libxml2 and libcurl, taking the interpreter
from 48 shared libraries to 10 — while using the system's OpenSSL, SQLite and
zlib rather than vendoring them, so an auditor can see them.
`packaging/example-app` is a working loopback service you can build and run.

**[docs/shipping_applications.md](docs/shipping_applications.md)** is the
guide, and it opens with the hazard worth knowing before you start: a bare
`load NAME` searches the program's own directory tree *first*, so a stray
`stats.bas` under your application silently replaces the real one.

---

## Command line

```sh
./gbasic program.bas
./gbasic --tokens program.bas        # dump lexer tokens
./gbasic --ast program.bas           # dump the parsed AST
./gbasic --add-loads program.bas     # print source with suggested `load` lines
./gbasic --json-diagnostics prog.bas # diagnostics as JSON on stderr
./gbasic --line-buffered program.bas # flush stdout per completed line
./gbasic --version
```

`--line-buffered` exists for the standard Unix pipe-buffering surprise, not a
gBASIC quirk: stdio line-buffers a terminal but *block*-buffers a pipe, so a
program printing a line every few seconds looks silent until it exits, and one
killed before exiting loses whatever was still buffered. Pass the flag when
something is reading your output as it is produced. It costs roughly one
`write` syscall per line.

Flags are order-independent and imply nothing about each other. A flag-looking
argument *after* the file belongs to the program, so
`./gbasic prog.bas --line-buffered` passes that text to `program main(args)`.

The older `use` / `--add-uses` syntax remains supported for compatibility;
prefer `load` / `--add-loads`.

A separate diagnostics language server, `gbasic-lsp`, speaks LSP over stdio and
publishes diagnostics on document sync. It is deliberately kept out of the
default target: build it with `make dev` and install it with
`sudo make install-lsp`.

---

## Tests

```sh
./tests/run_all.sh                  # the gate: every suite, discovered by glob
./tests/run_all.sh web              # or filter by substring
```

**Use `run_all.sh` rather than naming suites.** It discovers all 75 suites by
glob, and that is the whole point: a hand-maintained list is a gate that
silently shrinks. Four suites in this repository sat broken across two releases
because every list anyone ran happened not to name them. It reports a suite
that skipped *entirely* separately from one that passed, since a green line
that ran no assertions is the other way a gate quietly narrows.

Suites needing an optional dependency, a display or a service skip cleanly
rather than fail. Two are opt-in because they need a live PostgreSQL:

```sh
GBASIC_POSTGRES_TEST=1 PGDATABASE=my_test_db ./tests/run_postgres.sh
```

Tests are golden-file based — a source file beside an `.out` holding expected
stdout, compared byte for byte — except where a golden would be the wrong
instrument. Where a plausible-but-wrong answer is the failure mode, fixtures
state their own expected values and report a mismatch, because a golden records
whatever the binary said *as* the expectation and then defends it.

GUI testing beyond the headless tiers is manual, because it needs a display.

---

## Documentation

**[Full documentation index →](docs/README.md)** — every document, each marked
**Shipped**, **Proposal**, **Partial** or **Record**, so a description of
working behaviour is distinguishable from a design for something that does not
exist yet. `tests/run_docs_gate.sh` fails if a document is missing from that
index, or if the index links to something that is not there.

**Learn the language**

- [Tutorial](docs/tutorial.md) — learn gBASIC by writing programs
- [Reference](docs/reference.md) — syntax, semantics and every builtin
- [Language design](docs/gbasic-design.md) — the reasoning behind the language

**Cookbooks** — task-oriented, with runnable examples. Every code and output
block on these pages is verified by executing it, so a page cannot drift from
the product without a test failing:

- [Spreadsheets](docs/xlsx_cookbook.md) — read, edit and save real workbooks
- [Databases over ODBC](docs/odbc_cookbook.md) — SQL Server, MySQL, Oracle and
  the rest through one module
- [Charts](docs/chart_cookbook.md) — charts as deterministic SVG text
- [Dates, durations and scheduling](docs/datetime_cookbook.md)
- [GUI](docs/gui_tutorial.md) and its [cookbook](docs/gui_cookbook.md) — native
  GTK 4 applications
- [EDGAR securities analysis](docs/edgar_tutorial.md) — build a forensic
  dossier on a filer
- Statistics: [social & behavioral](docs/cookbook_social_behavioral.md) ·
  [econometrics & finance](docs/cookbook_econometrics_finance.md)

**Ship it**

- [Shipping applications](docs/shipping_applications.md) — turning a `.bas`
  program into a `.deb` a customer installs

**Writing gBASIC with an AI agent**

gBASIC diverges from QBasic/VB intuition in ways that fail *silently*. Start at
[docs/ai/START-HERE.md](docs/ai/START-HERE.md), then
[UNLEARN.md](docs/ai/UNLEARN.md).

**What shipped, and what bit us**

- [CHANGELOG.md](CHANGELOG.md) — what is in each release
- [DOGFOOD.md](DOGFOOD.md) — every limitation and surprise hit while actually
  using gBASIC, with the workaround

---

## Limitations

gBASIC remains an experimental interpreter:

- the evaluator is tree-walking and not optimized
- there are no closures; a function cannot rebind a variable in an enclosing
  scope, and doing so silently creates a local instead (it warns)
- diagnostics and tooling are still developing
- optional modules depend on platform libraries
- the GTK 3 `gui` module is a proof of concept; `gi` is the supported path
- the WebServer does not do WebSockets or chunked request bodies
- module APIs and language details may change before a stable release

Check a document's status in the [index](docs/README.md) before relying on a
design proposal as an available feature.

---

## Version

```sh
./gbasic --version        # gBASIC 0.1.0
```

## Contributing

Issues, bug reports and questions are welcome. **Code contributions are not
being merged yet** — a Contributor License Agreement is being prepared, and
merging code before it is in place would permanently remove the option to
dual-license later. See [CONTRIBUTING.md](CONTRIBUTING.md) for the details, how
to build and test, and the house rules the codebase is held to.

## License

gBASIC is **dual-licensed**, and every file declares which applies in its own
header. Full map: **[LICENSING.md](LICENSING.md)**.

| | License |
|---|---|
| The language, the interpreter, and most of the standard library | **[Apache-2.0](LICENSE)** |
| The EDGAR suite and the spreadsheet-to-database layers | **[AGPL-3.0-or-later](LICENSE.AGPL-3.0)** |

**If you are writing gBASIC programs or embedding the interpreter, you are under
Apache-2.0** and nothing here restricts you. That includes the entire xlsx engine
— reading, writing, evaluating and recalculating spreadsheets — because it is
compiled into the binary.

Ten standard libraries are AGPL: `grid` `consolidate` `dbframe` (the pipeline
that turns messy sheets into queryable tables) and `edgar` `fundamentals`
`forensics` `insiders` `ownership` `mdna` `screener` (securities analysis).
Build on those and distribute, or run them as a network service, and the AGPL
asks you to release your source too. **A commercial license is available** if
that does not suit you — contact matthewct@gmail.com.

    Copyright 2026 Matthew C. Tedder

See [NOTICE](NOTICE). Contributions will be accepted under both licenses, plus a
CLA granting the right to sublicense — which is what keeps the commercial option
possible.

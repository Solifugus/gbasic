# gBASIC

![Byte Beaver, the gBASIC mascot](docs/assets/mascot.png)

gBASIC is an experimental BASIC-family language for readable, practical
programs. It combines familiar control flow with arrays, records, modifiers,
watchers, files, dates, money, SQLite, PostgreSQL, HTTP clients, and a
queue-based HTTP server.

This repository contains the C implementation of gBASIC
`0.1.0-rc8`. The language and runtime are under active development and are not
yet production-stable.

## Current Status

Implemented language features include:

- a hand-written lexer, Bison parser, AST, and tree-walking evaluator
- variables, strict expressions, assignment (with `+=`, `-=`, `*=`, `/=`),
  `print`, `print to error`, and `input`
- multiline and short inline `if`/`else`, `consider`, and `while`
- array iteration with `for each item in items` and `for item in items`;
  loops close with `end for`, `next`, or `next <name>`
- `break` and `continue`, each optionally naming the loop it means
  (`break x`, `continue x`)
- arrays, records, nested assignment, and dynamic record access
- functions, programs, libraries, `load`, and labels with `goto`/`gosub`
  (supported inside functions; using them at the top level is a runtime error)
- first-class function values (references) that can be stored, passed, and called
- shared-nothing actors (multiprocessing) over `spawn`/`send`/`receive`
- assignment and comparison modifiers
- watchers, locks, and runtime error handling
- distinct `nothing` and `unknown` values
- date/time, duration, money, file, and directory values
- binary-safe, Unicode-aware strings and codepoint operations
- regular expressions as a **value kind**, overloading `contains`/`replace`/
  `split`, plus `match`/`match_all` (always available; no optional library)
- bitwise builtins (`band`/`bor`/`bxor`/`bnot`/`shl`/`shr`/`rotl`/`rotr`)
- strict RFC 8259 JSON with `json_encode` for anything leaving gBASIC, and a
  round-tripping `encode`/`decode` dialect for gBASIC-to-gBASIC data, plus
  `try_decode`, which reports a malformed document as a value instead of raising

Implemented runtime and module features include:

- core file management, directory management, path utilities, and
  `read_lines`
- synchronous SQLite access through the optional sqlite3-backed `sqlite` module
- synchronous PostgreSQL access through the optional libpq-backed `pg` module
- synchronous HTTP and HTTPS requests through the optional libcurl-backed
  `webclient` module
- a built-in HTTP server using live request and response queues, listening on
  loopback unless asked for a public address
- an XML module (optional libxml2-backed) for tree parsing, navigation,
  encoding, lenient HTML, and constant-memory streaming
- an **xlsx module** (optional, needs zlib and libxml2) that *edits existing*
  spreadsheets rather than only generating new ones — the reader keeps every
  part of the workbook, including parts it does not model, so a save does not
  destroy what it did not understand. It carries a formula evaluator,
  dependency-ordered recalculation across sheets, and `xlsx.check`, which
  scores our engine against the results Excel itself cached in the file.
  Measured against 15,871 real Excel workbooks: **97.38%** of formula cells
  agree, with no disagreement at all in 91.1% of workbooks. See the
  [cookbook](docs/xlsx_cookbook.md).
- a general process API — `process.run` for a child, and
  `process.start`/`poll`/`read`/`wait`/`stop` to drive a live one without
  blocking; every child is tied to the interpreter's lifetime by the kernel, so
  none of them survives it, even a `kill -9`
- a cryptography library (optional libcrypto-backed): hashing, HMAC, AES-GCM,
  Ed25519, JWT/HS256, and signed cookies
- an optional GTK 3 GUI proof of concept through Stage 6A
- a generic GObject-Introspection bridge (the optional libgirepository-backed
  `gi` module) for native GObject libraries, with GTK 4 as the first target

The standard library also ships three large pure-gBASIC toolkits:

- a **spreadsheet-to-database pipeline** over the xlsx module — `grid` turns a
  messy worksheet into clean frames (guessing where it can, and reporting how
  confident it is), `consolidate` merges differently-shaped sources onto one
  schema, and `dbframe` loads the result into SQLite with column types inferred
  from every value rather than the first
- a **statistics** library — descriptive and inferential statistics,
  regression and the GLM suite, mediation/moderation, time-series and
  econometric diagnostics, and finance metrics
- an **EDGAR securities-analysis suite** — SEC data acquisition, fundamentals,
  the forensic scorecard (accruals, Beneish, Piotroski, Altman, dilution,
  composite flags), ownership and insider analysis, MD&A extraction with an
  LLM analyst panel, a whole-market screener, and a watcher monitor

The repository also contains two larger language exercises:

- [Adventure](examples/adventure/adventure.bas), a small text adventure
- [BAG](examples/bag/README.md), a menu-driven BASIC Adventure Generator

## Quick Start

Required build tools:

- a C11 compiler
- `make`
- `bison`

Optional dependencies are detected through `pkg-config`:

| Dependency | Enables |
| --- | --- |
| sqlite3 | `load sqlite` |
| libpq | `load pg` |
| libcurl | `load webclient` |
| libxml2 | `load xml` |
| libcrypto (OpenSSL) | cryptography builtins / `load crypto` |
| libxcrypt | `password_hash` / `password_verify` |
| GTK 3 | `load gui` (GUI proof of concept) |
| libgirepository-2.0 (GLib ≥ 2.80) | `load gi` (GObject-Introspection bridge, e.g. GTK 4) |

Build the interpreter:

```sh
make
```

Run a program:

```sh
./gbasic examples/adventure/adventure.bas
```

Clean and rebuild:

```sh
make clean
make
```

Install it system-wide (so `gbasic` is on your `PATH` and finds its standard
library with no environment setup):

```sh
sudo make install              # -> /usr/local/bin/gbasic + /usr/local/share/gbasic/stdlib
# or install without root into your home directory:
make install PREFIX=$HOME/.local
```

After installing you can run `gbasic program.bas` from anywhere, and unqualified
`load` of a standard library (e.g. `load dates`) resolves automatically. The
install path is baked in at build time, so changing `PREFIX` rebuilds the two
objects that carry it — you do not need `make clean` in between. `GBASIC_PATH`
still works and takes precedence when you want to point at a different stdlib
(e.g. during development: `GBASIC_PATH=stdlib ./gbasic program.bas`). `LICENSE`
and `NOTICE` are installed alongside, under `$PREFIX/share/doc/gbasic`. Remove
with `sudo make uninstall`.

The interpreter still builds when an optional dependency is unavailable.
Loading or calling the affected feature then produces a clear runtime error.
The built-in WebServer uses POSIX sockets and has no external HTTP dependency.

## Command Line

```sh
./gbasic program.bas
./gbasic --tokens program.bas
./gbasic --ast program.bas
./gbasic --add-loads program.bas
./gbasic --json-diagnostics program.bas
./gbasic --line-buffered program.bas
./gbasic --version
```

`--add-loads` analyzes unresolved calls and modifiers, then prints source with
suggested `load` statements. The older `use` syntax and `--add-uses` option
remain temporarily supported for compatibility. `--json-diagnostics` emits
parse/runtime diagnostics as JSON on stderr (the same model the language server
uses) while leaving normal program output untouched.

`--line-buffered` flushes stdout at every completed line. This is the standard
Unix pipe-buffering surprise, not a gBASIC quirk: when stdout is a **terminal**,
stdio line-buffers it and output appears as it is printed, but when stdout is a
**pipe** — `gbasic prog.bas | less`, a log collector, an editor running your
program — stdio switches to block buffering and holds output until roughly 4 KB
have accumulated. A program that prints a line every few seconds therefore looks
silent until it exits, and a program killed before it exits loses whatever was
still buffered. Pass `--line-buffered` when something is reading your output as
it is produced. It is orthogonal: no other flag implies it, it implies nothing,
and it may be combined with any of the above in either order. Without it,
behavior is exactly what it has always been. The cost is roughly one `write`
syscall per printed line — measured at about +60% on a program that does nothing
but print, and unnoticeable on anything else.

A flag-looking argument *after* the file belongs to the program, so
`./gbasic prog.bas --line-buffered` passes the text through to `program main(args)`
rather than enabling the flag.

A separate diagnostics language server, `gbasic-lsp`, is built by `make dev`
(it is kept out of the default `make` target). It speaks LSP over stdio and
publishes diagnostics on document sync. Install it with `sudo make install-lsp`,
which is deliberately separate from `make install` so a plain install stays
lean; `make uninstall` removes both.

## Language Example

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

Multiline conditionals remain available:

```basic
if ready then
    print("ready")
else
    print("waiting")
end if
```

Short conditionals omit `end if` when their final branch is inline:

```basic
if ready then print("ready")
else print("waiting")
```

See [docs/tutorial.md](docs/tutorial.md) for a guided introduction and
[docs/reference.md](docs/reference.md) for the detailed language and runtime
reference.

## Watchers

Watchers are immediate reactive blocks over stored paths:

```basic
total = 0

watch(total)
    print(total)
end watch

total = 1
```

A watcher runs once when it is registered. After that, mutations trigger
watchers synchronously, before execution continues past the mutating statement,
but only when the stored value actually changes. Equal-value writes do not
trigger watchers.

Watcher path matching is symmetric at dot boundaries. `watch(state)` observes
changes to `state.value`, and `watch(state.value)` observes wholesale
replacement of `state`. Array indexes are tracked at the containing array path.

A **named** watcher is a first-class value with an off-switch:

```basic
watch recalc(a, b)
    c = a + b
end watch

unwatch recalc           ' turns it off
print(recalc.name)       ' recalc
print(recalc.targets)    ' ["a","b"]
print(count(watchers())) ' the live handles
```

Re-declaring `watch recalc(...)` while `recalc` holds a live watcher replaces
it rather than stacking a second one, so setup code is safe to re-run; and
`watchers()` recovers any handle whose variable was lost, so nothing can fire
with its off-switch out of reach.

During one active watcher drain, a watcher already pending is not enqueued
again; it runs once against the latest state. Cycles and runaway cascades raise
runtime error code `1005` from source `watcher` after the execution cap is
reached.

Collection mutators such as `append`, `prepend`, `insert`, `remove`,
`take_first`, `take_last`, `reverse`, `sort`, and `unique` notify watchers
once, after the stored collection mutation completes.

## Objects (Policy-Based Inheritance)

gBASIC's object model is **prototypal**: any record is a prototype, and `new`
derives an instance from it.

```basic
account = {
    owner: "unnamed",
    balance: 0
}

a = new account with { owner: "Ada", balance: 100 }
print(a.owner)        ' Ada
print(account.owner)  ' unnamed  — the prototype is untouched
```

Each property may carry a **policy** in parentheses that controls how it is
inherited. The policy clause uses `:` (not `=`):

```basic
bank = {
    name   (copy):  "unnamed",         ' default — an independent copy
    branch (link):  "Main Street",     ' shared identity, write-through
    id     (reset next_id()): 0,       ' fresh value evaluated at each `new`
    scratch (exclude): "temp"          ' not inherited at all
}
```

- **`copy`** (the default) — the instance gets its own independent value. Writing
  it never affects the prototype or sibling instances. Internally this is
  copy-on-write: the storage is shared until first written, so `new` is cheap.
- **`link`** — the instance shares one cell with the prototype. A write through
  the prototype, the instance, or any sibling is visible to all of them.
- **`reset <expr>`** — the expression is re-evaluated at each `new` (in the
  global/program scope), giving every instance a fresh value such as an id or
  timestamp. The prototype keeps its literal.
- **`exclude`** — the property stays on the prototype but is omitted from
  instances (use `has(instance, "name")` to confirm).

A `with { … }` block overrides values, re-annotates policy, and adds new fields;
its entries win over `reset`. Derivation is **recursive** — a `copy` property
whose value is itself an instance is re-derived, so nested `reset`s re-fire and
nested instances stay independent. Policies persist on instances, so an instance
can itself serve as a prototype.

See [docs/pbi_design.md](docs/pbi_design.md) for the full design and rationale.

## Values And Modifiers

Modifiers validate or transform values during assignment and comparison:

```basic
price{USD}= 19.95
due{date}= "2026-05-15"
command{trimmed}= input(">")
age{number}= input("Age: ")

if command{caseless}= "quit" then print("Goodbye")
```

Arithmetic is strict. `-`, `*`, and `/` require numbers. `+` performs numeric
addition unless either operand is a string, in which case canonical string
conversion and concatenation are used.

`nothing` represents deliberate absence. `unknown` represents an unavailable
or not-yet-known value and is also returned by missing dynamic record reads:

```basic
customer = {name:"Ada"}
middle_name = customer["middle_name"]

if is_unknown(middle_name) then print("Middle name is unknown")
```

## Strings And Unicode

Strings are **binary-safe byte sequences**, UTF-8 by convention, carrying an
explicit length — so any byte, including NUL via `chr(0)`, is valid content.
Character operations are **codepoint-aware**; raw bytes are reachable through
explicit builtins:

```basic
len("café")                    # 4   codepoints (not bytes)
byte_count("café")             # 5   UTF-8 bytes
mid("café", 3, 1)              # "é" never splits a codepoint
chr(128512)                    # "😀"  (codepoint U+1F600)
code("é")                      # 233
from_bytes([72, 105])          # "Hi"  (build from raw bytes 0..255)
print("\u{1F600}")             # 😀   (\u{...} codepoint escape)
```

`upper`, `lower`, and the `{caseless}` comparison fold **ASCII only** — non-ASCII
characters pass through unchanged (`upper("café")` is `"CAFÉ"`). Full Unicode case
folding, normalization, and grapheme clusters are future work.

**Walking a string is linear.** Because a character position is a codepoint index
rather than a byte offset, it has to be translated — and a naive implementation
re-walks the string on every access, which makes a per-character loop quadratic
and turns real-world text processing into something that appears to hang. gBASIC
does not do that: a string remembers its own codepoint count, and remembers where
it last looked, so scanning is O(n) whichever direction you go and whatever mix of
ASCII and multibyte content the string holds.

```basic
i = 0
while i < len(text)              ' len is O(1) after the first call
  ch = mid(text, i, 1)           ' O(1); the scan as a whole is O(n)
  i = i + 1
end while
```

Concretely, on a 256 000-character string: a forward scan takes 0.30 s where the
re-walking implementation this replaced took 249 s, and a backward scan of
multibyte content takes 0.31 s against 152 s. A 1 000 000-character forward scan
takes 1.2 s. Strings are immutable values, so the bookkeeping is invisible: it can
never disagree with the content it describes. The one operation that stays
proportional to the string is building a new one — `s = s + x` in a loop copies,
so accumulate into an array and `join` it when the pieces are many.

## Actors (Multiprocessing)

Concurrent work runs as **actors**: isolated processes that share no memory and
communicate only by copying messages. `spawn worker(args…)` starts a function as a
new actor and returns a handle; `send(handle, value)` posts a message, `receive()`
blocks for the next one, and `self()` is an actor's own handle. An actor runs
until its body returns.

```basic
function squarer(parent, n)
    send(parent, n * n)
end function

program main(args)
    me = self()
    a = spawn squarer(me, 3)
    b = spawn squarer(me, 4)
    print(receive() + receive())   # 25
end program
```

Each actor is a fresh interpreter (fork+exec), so a crash is isolated to that
actor and no live database/GUI connection is shared. Messages are snapshots:
mutating a received value cannot fire the sender's watchers. A message may carry
**actor handles**, so a running actor can be handed a channel to a third actor
and topologies form freely. `receive(tag)` does **selective receive** — take the
next message whose tag matches, leaving the rest queued — and a duration argument
(`receive(5 seconds)`) is a **timeout** that returns `nothing` if no message
arrives in time. `monitor(handle)` / `demonitor(handle)` register for death
notification: when the monitored actor exits, the monitor receives a
`["down", handle, reason]` message, which is the basis for the copyable
supervisor pattern (`docs/multiprocessing_design.md`).

## Files And Paths

File and directory values use assignment modifiers:

```basic
input_file{file}= "input.txt"
output_dir(directory)= "output"
```

Core file operations include:

- `read`, `write`, `append`, `overwrite`, `bytes`, and `exists`
- `delete`, `copy`, and `move`
- `list`, `files`, `folders`, and `list_files`
- `make_dir` and non-recursive `remove_dir`
- `join_path`, `file_name`, `directory_name`, and `extension`
- `read_lines`, which returns an array of strings suitable for `for each`

Example:

```basic
log{file}= "app.log"
write(log, "started\nready\n")

for each line in read_lines(log)
    print(line)
end for
```

Filesystem failures are runtime errors. `remove_dir` removes only empty
directories; recursive file operations are not implemented.

## SQLite

SQLite support is synchronous and available when gBASIC is built with
sqlite3:

```basic
load sqlite

db = sqlite.connect("app.db")

sqlite.exec(db, "create table if not exists users (id integer primary key, name text, active integer)")
sqlite.exec(db, "insert into users (name, active) values (?, ?)", ["Ada", true])
rows = sqlite.query(db, "select id, name from users where active = ?", [true])

sqlite.close(db)
```

The module provides `sqlite.connect`, `sqlite.close`, `sqlite.query`,
`sqlite.exec`, `sqlite.begin`, `sqlite.commit`, `sqlite.rollback`, and
`sqlite.last_insert_rowid`.
Parameters are arrays bound through sqlite3 positional placeholders. Query
results are arrays of records, SQL `NULL` maps to `nothing`, and duplicate
column names are errors. Blob values are not currently supported.

SQLite value mapping is intentionally direct:

| SQLite storage class | gBASIC value | Notes |
| --- | --- | --- |
| `NULL` | `nothing` | Database null is absence |
| `INTEGER` | number | Large integers may lose precision in gBASIC's floating-point number type |
| `REAL` | number | Stored as a gBASIC number |
| `TEXT` | string | No date/time guessing is applied |
| `BLOB` | runtime error | Native blob values are future work |

Boolean parameters bind as integer `1` or `0` because SQLite has no separate
boolean storage class.

`sqlite.last_insert_rowid(db)` returns SQLite's last inserted rowid for that
connection as a number.

## PostgreSQL

PostgreSQL support is synchronous and available when gBASIC is built with
libpq:

```basic
load pg

db = pg.connect({
    host:"localhost",
    database:"worksplicer",
    user:"app",
    password:"secret"
})

rows = pg.query(db, "select id, name from users where active = $1", [true])
result = pg.exec(db, "update users set active = false where id = $1", [10])

pg.close(db)
```

The module provides `pg.connect`, `pg.close`, `pg.query`, `pg.exec`,
`pg.begin`, `pg.commit`, and `pg.rollback`. Parameters are arrays bound through
libpq. Query results are arrays of records, SQL `NULL` maps to `nothing`, and
PostgreSQL errors include SQLSTATE information when available.

`bigint` and `numeric` results currently remain strings to avoid silent
floating-point precision loss. Prepared statements, pooling, asynchronous
queries, `COPY`, and `LISTEN`/`NOTIFY` are not implemented.

## WebClient

WebClient performs synchronous outgoing HTTP and HTTPS requests when libcurl
is available:

```basic
load webclient

response = webclient.get("https://example.com")
print(response.status)
print(response.body)
```

The Phase 1 API is:

- `webclient.get(url)`
- `webclient.post(url, body)`
- `webclient.request(request_record)`

Request bodies must be strings. Encode structured JSON explicitly:

```basic
headers = {}
headers["Content-Type"] = "application/json"

response = webclient.request({
    method:"POST",
    url:"https://api.example.com/events",
    headers:headers,
    body:encode({name:"launch", active:true}),
    timeout:10
})
```

Responses contain `status`, `reason`, lowercase `headers`, and string `body`.
When JSON parsing succeeds, the response also contains `json`:

```basic
if not is_unknown(response["json"]) then print(response.json)
```

HTTP statuses such as 404 and 500 return response records. DNS, connection,
TLS, malformed-URL, and timeout failures are runtime errors.

## WebServer

WebServer Phase 1 uses watchers and live queues instead of callback handlers:

```basic
load webserver

server = webserver.listen(8080)

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        append(server.responses, {
            id:req.id,
            body:"Hello World"
        })
    end while
end watch
```

`webserver.listen(port)` binds immediately to `127.0.0.1`. Port `0` selects an
available ephemeral port. Incoming request records are appended to
`server.requests`; application responses are appended to `server.responses`
and matched by request `id`. These queues are ordinary watched arrays, so
`take_first(server.requests)` and `append(server.responses, ...)` use the same
watcher rules as other collection mutations.

Requests include method, path, parsed query parameters, lowercase headers,
string body, peer information, timestamp, and optional decoded `json`.
Response status defaults to 200, headers to `{}`, and body to `""`.
Unanswered requests receive HTTP 504 after 30 seconds.

Shut down with either:

```basic
webserver.close(server)
server.running = false
```

The server binds `127.0.0.1` unless an options record says otherwise:

```basic
server = webserver.listen(8080, { address: "0.0.0.0" })
```

`address` takes a numeric IPv4 or IPv6 address; omitting it keeps the listener
private to the machine. It is otherwise a single-threaded HTTP/1.1 server,
with no TLS, routing, middleware, static files, streaming, chunked request
bodies, or WebSockets.

## GUI

gBASIC has two independent GUI paths. They target different GTK major versions
and **cannot be used in the same process** (a runtime guard enforces this).

### `gui` — GTK 3 proof of concept

When GTK 3 development files are available, the build enables the declarative
`gui` module. It supports record-defined windows, addressable widgets,
GUI-originated watcher updates, and watcher-driven refresh of existing
widgets. GUI event batching is local to the GUI event loop; once a
GUI-originated mutation is applied to gBASIC storage, ordinary watcher behavior
applies. Dynamic widget-tree mutation is not implemented.

Run the demos with the standard-library path configured:

```sh
GBASIC_PATH=stdlib ./gbasic examples/gui/demo.bas
GBASIC_PATH=stdlib ./gbasic examples/gui/watch_demo.bas
GBASIC_PATH=stdlib ./gbasic examples/gui/calculator.bas
```

See [examples/gui/README.md](examples/gui/README.md) for the implemented scope
and manual verification steps.

### `gi` — GObject-Introspection bridge (GTK 4 and beyond)

When libgirepository-2.0 (GLib ≥ 2.80) is available, the build enables the `gi`
module: a generic, imperative bridge to any GObject-based library through its
introspection typelib. It is not GTK-specific — it drives GTK 4, GLib, Gio, and
others — with `gi.new`/`gi.get`/`gi.set`/`gi.call`/`gi.invoke`/`gi.connect`
mapping onto GObject construction, properties, methods, free functions, and
signals. Native objects are opaque `gobject` values with stable identity.

```sh
sudo apt-get install gir1.2-gtk-4.0 libgtk-4-1     # Debian/Ubuntu runtime + typelib
./gbasic examples/gi/gtk4_hello.bas
./gbasic examples/gi/calculator.bas
```

See the [GObject-Introspection (GUI) Module](docs/reference.md#gobject-introspection-gui-module)
reference and [examples/gi/README.md](examples/gi/README.md).

The **Native Application Platform** work (making gBASIC able to build sophisticated
native GTK 4 applications through `gi`; see
[docs/gbasic_native_app_platform_plan.md](docs/gbasic_native_app_platform_plan.md))
drives GTK 4 and GtkSourceView 5 entirely through this bridge — neither is linked.
Its only extra runtime requirement over the base `gi` module is that their
introspection typelibs resolve:

```sh
# Debian/Ubuntu runtime typelibs (no build/link change; loaded via gi)
sudo apt-get install gir1.2-gtk-4.0 libgtk-4-1 gir1.2-gtksource-5 libgtksourceview-5-0
```

## Tests

Build and run the baseline suites:

```sh
make clean && make
./tests/run_examples.sh
./tests/run_negative.sh
```

Run module and application integration tests:

```sh
./tests/run_webclient.sh
./tests/run_webserver.sh
./tests/run_web_bind.sh
GBASIC_PATH=stdlib ./tests/run_web_routes.sh
./tests/run_sqlite.sh
./tests/run_gbasic_site.sh
GBASIC_SITE_POSTGRES_TEST=1 ./tests/run_gbasic_site_postgres.sh
bash tests/run_bag_smoke.sh
```

The `gi` bridge and Native Application Platform suites are headless (display-free)
and skip cleanly when their introspection typelibs are unavailable:

```sh
./tests/run_gi.sh                # gi.* bridge; skips if libgirepository-2.0 absent
./tests/run_native_platform.sh   # native GTK4/GtkSource platform; skips if typelibs absent
```

The SQLite runner skips when sqlite3 development files are unavailable.
The WebClient, WebServer, and gBASIC site runners use local loopback fixtures and skip
cleanly when their environment is unavailable.

The PostgreSQL integration test is opt-in and uses normal libpq environment
variables:

```sh
GBASIC_POSTGRES_TEST=1 PGDATABASE=my_test_database ./tests/run_postgres.sh
```

GUI testing remains manual because it requires a display.

## Documentation

**[Full documentation index →](docs/README.md)** — every document, each marked
**Shipped**, **Proposal**, **Partial** or **Record**, so you can tell a
description of working behaviour from a design for something that does not
exist. `tests/run_docs_gate.sh` fails if a document is missing from that index
or if the index links to something that is not there.

### Learn the language

- [Tutorial](docs/tutorial.md) — learn gBASIC by writing programs
- [Reference](docs/reference.md) — syntax, semantics and every builtin
- [Language design](docs/gbasic-design.md) — the reasoning behind the language

### Cookbooks — task-oriented, with runnable examples

- [Spreadsheets (xlsx)](docs/xlsx_cookbook.md) — read, edit and save real
  workbooks; evaluate formulas; check the engine against Excel's own cached
  answers; turn messy sheets into queryable tables. Every code and output block
  on that page is verified against a real file that the suite runs.
- [EDGAR securities analysis](docs/edgar_tutorial.md) — build a forensic
  dossier on a filer
- [Statistics: social & behavioral](docs/cookbook_social_behavioral.md) and
  [econometrics & finance](docs/cookbook_econometrics_finance.md)

### Writing gBASIC with an AI agent

gBASIC diverges from QBasic/VB intuition in ways that fail *silently*.
Start at [docs/ai/START-HERE.md](docs/ai/START-HERE.md), then
[UNLEARN.md](docs/ai/UNLEARN.md).

### What shipped, and what bit us

- [CHANGELOG.md](CHANGELOG.md) — what is in each release
- [DOGFOOD.md](DOGFOOD.md) — every limitation and surprise hit while actually
  using gBASIC, with the workaround

A design document may describe unimplemented work. Rather than asking you to go
and verify that elsewhere, the [index](docs/README.md) states each document's
status next to its link.

## Project Limitations

gBASIC remains an experimental interpreter:

- the evaluator is tree-walking and not optimized
- diagnostics and tooling are still developing
- optional modules depend on platform libraries
- the WebServer is intentionally limited to local development use
- the GUI is a proof of concept rather than a complete toolkit
- module APIs and language details may change before a stable release

Check the implementation status documents before relying on a design proposal
as an available feature.

## Version

```sh
./gbasic --version
```

Current output:

```text
gBASIC 0.1.0-rc8
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

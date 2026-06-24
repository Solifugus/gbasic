# gBASIC

![Byte Beaver, the gBASIC mascot](docs/assets/mascot.png)

gBASIC is an experimental BASIC-family language for readable, practical
programs. It combines familiar control flow with arrays, records, modifiers,
watchers, files, dates, money, SQLite, PostgreSQL, HTTP clients, and a
queue-based HTTP server.

This repository contains the C implementation of gBASIC
`0.1.0-dev`. The language and runtime are under active development and are not
yet production-stable.

## Current Status

Implemented language features include:

- a hand-written lexer, Bison parser, AST, and tree-walking evaluator
- variables, strict expressions, assignment, `print`, and `input`
- multiline and short inline `if`/`else`, `consider`, and `while`
- array iteration with `for each item in items` and `for item in items`
- `break` and `continue`
- arrays, records, nested assignment, and dynamic record access
- functions, programs, libraries, `load`, labels, `goto`, and `gosub`
- assignment and comparison modifiers
- watchers, locks, and runtime error handling
- distinct `nothing` and `unknown` values
- date/time, duration, money, file, and directory values
- JSON-like serialization with `encode` and `decode`

Implemented runtime and module features include:

- core file management, directory management, path utilities, and
  `read_lines`
- synchronous SQLite access through the optional sqlite3-backed `sqlite` module
- synchronous PostgreSQL access through the optional libpq-backed `pg` module
- synchronous HTTP and HTTPS requests through the optional libcurl-backed
  `webclient` module
- a built-in loopback HTTP server using live request and response queues
- an optional GTK 3 GUI proof of concept through Stage 6A

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
| GTK 3 | GUI proof of concept |
| libpq | `load pg` |
| libcurl | `load webclient` |

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

The interpreter still builds when an optional dependency is unavailable.
Loading or calling the affected feature then produces a clear runtime error.
The built-in WebServer uses POSIX sockets and has no external HTTP dependency.

## Command Line

```sh
./gbasic program.bas
./gbasic --tokens program.bas
./gbasic --ast program.bas
./gbasic --add-loads program.bas
./gbasic --version
```

`--add-loads` analyzes unresolved calls and modifiers, then prints source with
suggested `load` statements. The older `use` syntax and `--add-uses` option
remain temporarily supported for compatibility.

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
price(USD)= 19.95
due(date)= "2026-05-15"
command(trimmed)= input(">")
age(number)= input("Age: ")

if command(caseless)= "quit" then print("Goodbye")
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

## Files And Paths

File and directory values use assignment modifiers:

```basic
input_file(file)= "input.txt"
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
log(file)= "app.log"
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

Phase 1 is a single-threaded, loopback-only HTTP/1.1 server. It does not
provide public binding, TLS, routing, middleware, static files, streaming,
chunked request bodies, or WebSockets.

## GUI

When GTK 3 development files are available, the build enables the current GUI
proof of concept. It supports record-defined windows, addressable widgets,
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
./tests/run_sqlite.sh
./tests/run_gbasic_site.sh
GBASIC_SITE_POSTGRES_TEST=1 ./tests/run_gbasic_site_postgres.sh
bash tests/run_bag_smoke.sh
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

- [Language design](docs/gbasic-design.md) — consolidated design of the language
  and core runtime
- [Tutorial](docs/tutorial.md)
- [Language and runtime reference](docs/reference.md)
- [Policy-Based Inheritance (objects)](docs/pbi_design.md)

Library/module designs (each kept separate):

- [SQLite design](docs/sqlite_design.md)
- [PostgreSQL design and implementation status](docs/postgres_design.md)
- [WebClient design](docs/webclient_design.md)
- [WebServer design](docs/webserver_design.md)
- [GUI design](docs/gui_design.md)

Other:

- [Completed development history](docs/historical_development_archive.md)

Design documents may discuss unimplemented future work. The historical archive
summarizes completed phases without keeping each temporary tracker active.

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
gBASIC 0.1.0-dev
```

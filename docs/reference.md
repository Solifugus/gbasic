# gBASIC Reference

This reference describes the implemented v0.1 language surface. gBASIC is experimental; behavior may change before a stable release.

This is a reference, not a tutorial: it states what each construct and module does, not how to learn the language. For a guided introduction see `docs/tutorial.md`; for runnable programs see `examples/`. AI agents writing gBASIC should start at `docs/ai/START-HERE.md`, which distills the surprises and idioms this document records in full.

## Lexical Rules

Source files are plain text.

Tokens:

- Identifiers: alphabetic or underscore start, followed by letters, digits, or underscores.
- Numbers: decimal numeric literals.
- Strings: double-quoted string literals. Literal newlines may appear inside strings.
- Booleans: `true`, `false`.
- Newlines terminate most statements.
- Apostrophe comments start with `'` and continue to end of line.

Supported string escapes:

- `\n` newline
- `\t` tab
- `\\` backslash
- `\"` double quote
- `\u{...}` Unicode codepoint by hex value, e.g. `\u{1F600}` → `😀` (range
  `1..0x10FFFF`, excluding surrogates; for a literal NUL use `chr(0)`)

Unknown escapes and unterminated strings are lexer errors and exit nonzero.

```basic
description = "You are in a stone hall.
Water drips from the ceiling.
A passage leads north."
```

Keywords include:

```text
if then else consider end print for to step while break continue function return dim as
watch without watchers modifier goto gosub with and or not in
program library load use export on error resume next stop
```

Keywords are matched case-insensitively by the lexer. Some keyword tokens may be accepted as identifiers in specific grammar positions, such as `end` and `next` variable names.

Operators and punctuation:

```text
= != > < >= <= !> !< !>= !<=
+ - * /
( ) [ ] { } , . :
```

## Statements

Assignment:

```basic
x = expression
x(modifier)= expression
x(modifier args)= expression
```

Assignment targets may be variables, array elements, record fields, or nested paths combining indexes and fields:

```basic
customer.name = "Grace"
inventory[0] = "lamp"
items[i].location = "inventory"
world.rooms[index].visited = true
player.inventory[slot].name = "key"
```

Print:

```basic
print(expression)
```

Compatibility note: statement-style `print expression` still parses for now.

If:

```basic
if expression then
    statement
else
    statement
end if
```

One simple statement may follow `then` or `else` on the same line:

```basic
if x = y then print("matches")

if x = y then
    print("matches")
else print("no match")

if x = y then print("matches")
else print("no match")
```

`end if` is not required when the final branch is an inline statement. If the
final branch starts on the line after `then` or `else`, it is a block and still
requires `end if`:

```basic
if x = y then print("matches")
else
    print("no match")
end if
```

Inline branches accept assignment, `print`, function calls, `load`, `on error`,
`error`, `return`, `goto`, `gosub`, `break`, and `continue`. Compound statements
such as nested `if`, `while`, `for`, `consider`, functions, watchers, and
`with lock` remain multiline. An `else` is associated with the nearest
unmatched inline `if`.

While:

```basic
while condition
    statement
end while
```

`break` exits the nearest enclosing `while`. `continue` skips to the next iteration.

Consider:

```basic
consider command
if "look" then
    look()
if "inventory" then
    show_inventory()
else
    print("I do not understand.")
end consider
```

`consider expression` evaluates the subject once. Each branch `if expression then` at the same indentation as the `consider` line compares the subject with that expression using `=` and executes only the first matching branch. `else` is optional and runs only when no branch matched. `break` inside a consider branch exits the consider block; if a consider block is inside a loop, that break does not exit the loop. `continue` is not special for consider, so inside a loop it keeps its loop meaning.

Normal `if` blocks are separate boolean control flow outside a consider block. Inside consider, top-level `if expression then` starts a match branch.

For-each:

```basic
for item in items
    print(item.name)
end for
```

Function:

```basic
function name(a, b)
    return a + b
end function
```

Return:

```basic
return
return expression
```

Labels and jumps inside functions:

```basic
label:
goto label
gosub label
```

`goto` and `gosub` are supported only inside functions; using either at the top
level is a runtime error.

Watchers:

```basic
watch(a, b)
    c = a + b
end watch

without watchers
    a = 10
end without
```

`watch(...)` registers a watcher over one or more stored paths. The watcher
body runs once immediately at registration. Later, watcher triggering is
immediate and synchronous: a storage-changing mutation runs matching watchers
before execution continues past the mutating statement.

Watcher notifications use storage-change detection. Equal-value writes do not
trigger watchers. Arrays and records are compared deeply for this internal
watcher guard; this does not change language comparison semantics for `=`.

Watcher path matching is symmetric at dot boundaries:

- `watch(state)` sees `state.value` changes
- `watch(state.value)` sees wholesale replacement of `state`
- unrelated paths and false string prefixes such as `state` and `statement`
  do not match

Array indexes are tracked at the containing array path rather than as
index-specific watcher identities.

During one active watcher-drain cycle, a watcher that is already pending is
not enqueued again. It executes once and reads the latest live state. Separate
top-level mutations still produce separate synchronous watcher drains.

Runaway watcher cascades keep the execution cap and now raise a structured
runtime error if the cap is reached:

- `error.code = 1005`
- `error.source = "watcher"`
- message: `watcher cycle exceeded 10000 executions in one drain cycle`

`without watchers` suppresses watcher triggering for mutations inside the
block, then restores the previous watcher state when the block exits.

Lock block:

```basic
with lock(f)
    write(f, "text")
end with
```

Program block:

```basic
program demo(args)
    print("hello")
end program
```

A program block is the entry point. Its first declared parameter — `args` by
convention — binds to the command-line arguments after the script path, as a
0-based array of strings, empty when none are passed. A file with no program
block runs its top-level statements directly, with no argument binding.

Library block:

```basic
library math
    function add(a, b)
        return a + b
    end function
end library
```

Load:

```basic
load math
load tools from "libs/tools.bas"
```

Compatibility note: `use` remains a temporary alias for `load`.

Error handling:

```basic
on error stop
on error resume next
on error goto label
error "message"
```

## Expressions

Primary expressions:

- Numbers
- Strings
- Booleans
- Identifiers
- Parenthesized expressions
- Array literals
- Record literals
- Function calls

Arithmetic:

```basic
a + b
a - b
a * b
a / b
```

String concatenation uses `+`:

```basic
full = first + " " + last
```

`+` concatenates when either operand is a string; `-`, `*`, and `/` are strict
numeric arithmetic. There is no implicit numeric/string coercion — conversions
are explicit (`string(...)`, `number(...)`). No f-string syntax is implemented.

Comparisons:

```basic
a = b
a != b
a > b
a < b
a >= b
a <= b
a !> b
a !< b
a !>= b
a !<= b
```

Date/time bare comparisons are exact. Two date/time values are equal only when
their stored date/time fields and stored precision match. For precision-aware
comparison, use an explicit comparison lens:

```basic
d(date)= "2026-05-15"
t(date)= "2026-05-15 12:05:03"

if d != t then print("different precision")
if d {day}= t then print("same day")
```

Bare date/time ordering compares the represented start instant first, then
uses stored precision as a tie-breaker with less precise values ordered before
more precise values at the same start instant.

Logical operators:

```basic
not ready
a and b
a or b
```

Precedence, high to low:

1. `not`, unary `-`
2. `*`, `/`
3. `+`, `-`
4. comparisons
5. `and`
6. `or`

Assignment is not an expression.

Array indexing:

```basic
scores[0]
```

Record field access:

```basic
customer.name
customer[field]
```

`customer.name` uses a static field name. `customer[field]` evaluates `field` at runtime and requires the left side to be a record and the key to be a string. Reading a missing dynamic field returns `unknown`; assigning one creates or updates that field:

```basic
field = "nickname"
print(customer[field])
customer[field] = "Ada"
```

Bracket access keeps its array behavior when the left side is an array and the index is numeric. Arrays do not accept string keys, and records do not accept numeric keys.

Function calls:

```basic
add(2, 3)
math.add(2, 3)
```

Function calls may appear in comparisons:

```basic
if len(words) = 0 then
    print("empty")
end if
```

Function calls are expressions, not lvalues. This is invalid:

```basic
len(words) = 0
foo() = 1
items()[0] = 1
get_player().name = "Bob"
```

Nested lvalues may combine static fields, array indexes, and dynamic record keys:

```basic
areas[i][direction] = 4
world.rooms[i][direction] = 3
```

Durations:

```basic
1 hour
20 minutes
1 hour 20 minutes 2 seconds
2 days 3 hours
```

Supported duration units:

- `years`
- `months`
- `weeks`
- `days`
- `hours`
- `minutes`
- `seconds`

Singular unit spellings are accepted by the evaluator.

## Types

Runtime values:

- Number
- String
- Boolean
- Null/nothing
- Unknown
- Array
- Record
- Date/time
- Duration
- Money
- File reference
- Directory reference

Arrays currently work best with numeric aggregate built-ins. Records hold string keys and values of normal runtime types.

File and directory references are typed paths, not open handles.

Money created by `USD` is stored as integer cents.

### Strings and Unicode

A gBASIC string is a **binary-safe sequence of bytes**, UTF-8 by convention. It
carries an explicit length, so any byte — including NUL (`chr(0)`) — is valid
content; strings are not NUL-terminated from the program's point of view.

String operations split into two families:

- **Character-oriented (Unicode codepoints).** `len`, `left`, `right`, `mid`,
  `reverse`, `find`, `chr`, and `code` count and slice by **codepoint**, never
  splitting a multibyte character. `len("café")` is `4`. Indexing is 0-based,
  matching `mid` and arrays. A malformed UTF-8 byte degrades gracefully to one
  unit (these operations never error on arbitrary bytes).
- **Byte-oriented (raw).** `byte_count`, `byte_at` (0-based), and `from_bytes`
  work on raw bytes for binary and protocol work.

```basic
len("café")                    # 4   codepoints
byte_count("café")             # 5   UTF-8 bytes
mid("café", 3, 1)              # "é" never splits a codepoint
from_bytes([0, 255])           # a two-byte binary string
```

**Comparison** is by byte sequence (binary-safe, and correct codepoint order for
valid UTF-8). **Case folding** (`upper`, `lower`, and the `{caseless}` comparison
modifier) is **ASCII-only**: `A–Z` ↔ `a–z` fold and every other byte, including
all non-ASCII characters, is left exactly as-is (`upper("café")` is `"CAFÉ"` with
the `é` unchanged). Full Unicode case folding, normalization, and grapheme
clusters are future work.

**Literals** are UTF-8 from the source file. The escape `\u{...}` inserts a
codepoint by hex value (`"\u{1F600}"` is `"😀"`); for a literal NUL use `chr(0)`.

## First-Class Functions

A bare function name evaluates to a **function value** — a reference to the named
function, not a closure over its defining scope:

```basic
function double(n)
    return n * 2
end function

d = double        ' a function value
print type(d)     ' function
print d(21)       ' 42
```

Function values can be stored in variables, records, and arrays, passed as
arguments, returned, and called. Equality is same-reference (`d = double` is
`true`); a function value is truthy; `string(d)` gives a debug representation.
Function values are actor-sendable, so a spawned actor can receive one and call it.

A function value held in a **record field is a method**. Reaching it through the
record binds the receiver to `this` at the call site; dispatch follows whichever
function value the field holds:

```basic
function deposit(amount)
    this.balance = this.balance + amount   ' writes through, honoring PBI policies
    return this.balance
end function

account = { name: "checking", balance: 0, deposit: deposit }
print account.deposit(100)   ' 100
```

The dotted-def statement `function account.deposit(amount)` is sugar that defines
a function and attaches it to the record variable `account` in one step.

A field named `constructor` is auto-invoked by `new … with { … }` after
derivation, with `this` bound to the new instance; its inputs arrive through the
`with` block and are read from `this`, and its return value is ignored (see
[Objects](#objects-policy-based-inheritance)).

There are no closures, bound methods, or inline lambdas; a method reads its
receiver only through `this`, bound per call.

## Objects (Policy-Based Inheritance)

gBASIC objects are prototypal: a record is a prototype, and `new <prototype>`
derives an instance. An optional `with { … }` block customizes the instance.

```basic
account = { owner: "unnamed", balance: 0 }
a = new account with { owner: "Ada", balance: 100 }
```

`new` evaluates its operand to a record (a runtime error code `1003` is raised
otherwise) and produces a derived record. The prototype is never mutated by
derivation.

### Field policies

Each property may declare a policy in parentheses before its `:`. The clause is
`:`-only; `name (copy)= …` is a syntax error by design (the `)=` form is the
assignment-modifier syntax). Policy names are contextual — they are ordinary
identifiers everywhere except this position.

| Policy | Meaning |
| --- | --- |
| `copy` (default) | Instance gets an independent value; writes never affect the prototype or siblings. Implemented as copy-on-write — storage is shared until first written. |
| `link` | Instance shares one storage cell with the prototype; a write through the prototype, instance, or any sibling is visible to all. |
| `reset <expr>` | `<expr>` is re-evaluated at each `new` in the global/program scope (top-level and loaded functions, builtins, globals — not defining-scope locals or `with` arguments). The prototype keeps its literal value. |
| `exclude` | Property remains on the prototype but is omitted from instances. Confirm with `has(instance, "name")`. |

```basic
record_proto = {
    name    (copy):  "unnamed",
    shared  (link):  "Main",
    id      (reset next_id()): 0,
    scratch (exclude): "temp"
}
```

### Derivation rules

- A `with { … }` block may override a value, re-annotate a property's policy
  (affecting that instance's own descendants), and add new properties. It may not
  remove an inherited property. Override entries win over `reset`.
- Derivation is **recursive**: a `copy` property whose value is itself an
  instance is re-derived, so nested `reset`s re-fire and nested instances stay
  independent. (Records nested inside arrays are copied, not re-derived.)
- Policies persist on instances, so an instance can itself serve as a prototype;
  re-derivation re-applies its policies.

See [pbi_design.md](pbi_design.md) for the full design and rationale.

## Modifiers

Modifier use:

```basic
x(USD)= 19.95
name {caseless}= "joe"
a {rounded 2}= b
a {math.rounded to 2}= b
```

Assignment modifiers transform assigned values. Comparison modifiers transform or implement comparisons.

Modifiers apply only in assignment and comparison contexts. In v0.1, modifiers apply to variables, record fields, and array elements, not to function-call results.

Comparison modifiers use brace lens syntax:

```basic
left {modifier}= right
left {modifier args}< right
left {modifier args}>= right
```

The older parenthesized comparison syntax remains temporarily supported during
the migration:

```basic
name(caseless)= "joe"
a(rounded 2)= b
```

That comparison form is deprecated and will be removed in a later phase.
Parenthesized assignment modifiers are not deprecated.

Define an assignment modifier:

```basic
modifier NAME for assign
    return value
end modifier
```

Parameterized assignment modifier:

```basic
modifier NAME(n) for assign
    return value
end modifier
```

Define a comparison modifier:

```basic
modifier caseless for compare
    return compare(lower(left), operator, lower(right))
end modifier
```

Comparison modifier variables:

- `left`
- `right`
- `operator`

Assignment modifier variable:

- `value`

Exported library modifiers:

```basic
export modifier shout for assign
    return upper(value)
end modifier
```

Resolution:

- Qualified modifiers resolve only from the named library.
- Unqualified modifiers resolve local definitions first, then loaded libraries, then built-ins.
- Local modifiers override imported modifiers with a warning.
- Later `load` imports can override earlier imported modifiers with a warning.
- Private library modifiers are not imported.

Built-in/core modifiers include:

- Assignment: `USD`, `date`, `time`, `datetime`, `year`, `month`, `day`, `hour`, `minute`, `second`, `file`, `dir`, `trimmed`, `lowered`, `uppered`, `split`, `join`, `length`, `number`, `string`
- Comparison: `caseless`, date/time lens comparisons

`date` infers precision from the string (`"2026-05-15"` is day-precise,
`"2026-05-15 12:05:03"` is second-precise). `datetime` always yields a full
second-precision timestamp, filling missing time components with `00:00:00` — the
same value `now()` produces. The `year`/`month`/`day`/`hour`/`minute`/`second`
modifiers are truncation lenses usable in both assignment and comparison.

## Libraries

A file may contain libraries and a program.

```basic
library text
    export modifier shout for assign
        return upper(value)
    end modifier
end library

program demo(args)
    load text
    msg(shout)= "hello"
    print(msg)
end program
```

If a `program` block exists, only that program executes. If no `program` block exists, top-level statements are treated as an implicit program.

Same-file load:

```basic
load math
```

Load from a specific file:

```basic
load tools from "libs/tools.bas"
```

Unqualified `load NAME` search order:

1. Same-file libraries
2. Current directory file named `NAME.bas`
3. Subdirectories for files named `NAME.bas`
4. `GBASIC_PATH` directories for files named `NAME.bas`
5. If no named match is found, other `.bas` files containing `library NAME`

`GBASIC_PATH` is colon-separated.

Functions in loaded libraries are imported by default for now. Library modifiers must be marked `export modifier` to be imported.

Qualified function calls:

```basic
math.add(2, 3)
```

Qualified modifier calls:

```basic
name {text.caseless}= "joe"
```

## Actors and Multiprocessing

gBASIC runs concurrent work as **actors**: isolated processes that share no
memory and communicate only by copying messages. A small set of primitives
carries the model (`docs/multiprocessing_design.md`):

- `spawn worker(args…)` — start a new actor running the named function `worker`
  and return a **handle** to it. `worker` must be a `function` declared in the
  program, and the program must be loaded from a file (the child re-execs it). The
  arguments are copied to the child as its first message; a handle among them —
  including `self()` — is passed through so the child can message that actor.
- `send(handle, value)` — copy `value` into the target actor's mailbox as one
  message. Non-blocking: if the mailbox is full or the value is too large for one
  frame, a structured `actor` error is raised rather than blocking. Per-sender
  ordering is FIFO.
- `receive()` — block until a message is in this actor's mailbox; remove and
  return it. Pairs naturally with `consider` for dispatch.
- `receive(tag)` — **selective receive**: return the next message whose *tag*
  matches `tag` (the message itself if it is a string, or its first element if it
  is an array), leaving non-matching messages queued in arrival order for later
  receives. A plain `receive()` drains those oldest-first, so FIFO order holds
  across both forms.
- `receive(<duration>)` — **timeout**: return the next message, or `nothing` if
  none arrives within the duration (e.g. `receive(5 seconds)`). `receive(tag,
  <duration>)` combines a selector with a deadline. A queued message returns
  immediately. (A `nothing` message is indistinguishable from a timeout; wrap
  messages if you must tell them apart.)
- `self()` — this actor's own handle, so it can be handed to others.
- `monitor(handle)` / `demonitor(handle)` — register (or cancel) death
  notification for another actor. When a monitored actor exits, the monitor
  receives a `["down", handle, reason]` message. This is the basis for the
  copyable supervisor pattern.

An actor runs until its body returns. A worker that handles many messages loops
and leaves on a sentinel:

```basic
function worker(name, parent)
    while true
    consider receive()
    if "ping" then
        send(parent, "pong from " + name)
    if "stop" then
        return
    end consider
    end while
end function

program main(args)
    me = self()
    a = spawn worker("a", me)
    send(a, "ping")
    print(receive())          # "pong from a"
    send(a, "stop")
end program
```

Anything `serialize` accepts can be sent; live database connections and GUI
widgets cannot cross a boundary. Messages are snapshots — mutating a received
value cannot fire the sender's watchers, and a record's `link` fields arrive as
independent copies ("watcher boundaries are concurrency boundaries").

A message may itself contain **actor handles** (a value `serialize` rejects on its
own): sending one to a running actor hands it a channel to a third actor, so
actors can introduce each other and form arbitrary topologies. Up to 32 handles
may travel in a single message.

## Errors

Default mode:

```basic
on error stop
```

Unhandled runtime errors stop execution and produce a nonzero process exit status.

Resume next:

```basic
on error resume next
```

Runtime errors set error state and execution continues at the next statement.

Single-use goto:

```basic
on error goto failed
```

The handler clears itself after firing. Error state persists until `error.clear()`.

Explicit error:

```basic
error "message"
```

Error state:

```basic
if error then
    print(error.message)
    print(error.line)
    print(error.column)
    print(error.code)
    print(error.source)
    error.clear()
end if
```

Errors propagate out of functions. `with lock` unlocks on error, and `without watchers` restores watcher behavior after its block.

Diagnostic positions — `error.line`/`error.column` and the `line:column` in error text — are 1-based and counted in **bytes** (not Unicode codepoints or UTF-16 units): a multi-byte UTF-8 character advances the column by its byte count, a tab counts as one column, only `\n` starts a new line, and spans are inclusive-start/exclusive-end (the authoritative definition lives in `include/diagnostics.h`). For the diagnostic-code and error-domain catalog, see `docs/ai/ERRORS.md`.

## SQLite Module

SQLite support is available when gBASIC is built with sqlite3. Load the
compiled standard module before using its qualified API:

```basic
load sqlite

db = sqlite.connect("app.db")

sqlite.exec(db, "create table if not exists users (id integer primary key, name text, active integer)")
sqlite.exec(db, "insert into users (name, active) values (?, ?)", ["Ada", true])
rows = sqlite.query(db, "select id, name from users where active = ?", [true])

sqlite.close(db)
```

Connections are opaque `sqlite_connection` values. They close explicitly with
`sqlite.close` and automatically during interpreter cleanup.

The module provides:

- `sqlite.connect(path_string)`
- `sqlite.close(connection)`
- `sqlite.query(connection, sql[, params])`
- `sqlite.exec(connection, sql[, params])`
- `sqlite.begin(connection)`
- `sqlite.commit(connection)`
- `sqlite.rollback(connection)`
- `sqlite.last_insert_rowid(connection)`

Parameters are arrays and are bound separately through sqlite3. Use SQLite
positional placeholders such as `?`. SQL `NULL` maps to `nothing`. Query
results are arrays of records; duplicate column names are errors. SQLite blob
parameters and results are not currently supported. SQLite errors use
`error.source = "sqlite"`.

SQLite type mapping:

| SQLite storage class | gBASIC value | Notes |
| --- | --- | --- |
| `NULL` | `nothing` | Database null is absence |
| `INTEGER` | number | Large integers may lose precision in gBASIC's floating-point number type |
| `REAL` | number | Stored as a gBASIC number |
| `TEXT` | string | No date/time guessing is applied |
| `BLOB` | runtime error | Native blob values are future work |

Parameters support `nothing`, booleans, numbers, strings, and date/time
values. Booleans bind as integer `1` or `0` because SQLite has no separate
boolean storage class. Date/time values bind as ISO-like text.

`sqlite.last_insert_rowid(connection)` returns SQLite's last inserted rowid for
that connection as a number.

## PostgreSQL Module

PostgreSQL support is available when gBASIC is built with libpq. Load the
compiled standard module before using its qualified API:

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

Connections are opaque `postgres_connection` values. They close explicitly
with `pg.close` and automatically during interpreter cleanup.

The module provides:

- `pg.connect(config_record)`
- `pg.close(connection)`
- `pg.query(connection, sql[, params])`
- `pg.exec(connection, sql[, params])`
- `pg.begin(connection)`
- `pg.commit(connection)`
- `pg.rollback(connection)`

Parameters are arrays and are bound separately through libpq. SQL `NULL`
maps to `nothing`. Query results are arrays of records; duplicate column names
are errors. `bigint` and `numeric` results remain strings to avoid precision
loss. JSON results decode to normal arrays, records, and scalar values.
PostgreSQL errors use `error.source = "postgres"` and include SQLSTATE when
available.

## WebClient Module

WebClient support is available when gBASIC is built with libcurl. It performs
synchronous outgoing HTTP and HTTPS requests:

```basic
load webclient

response = webclient.get("https://example.com")
print(response.status)
print(response.body)
```

The module provides:

- `webclient.get(url)`
- `webclient.post(url, body)`
- `webclient.request(request_record)`

URLs and request bodies must be strings. `webclient.request` accepts a record
with a required string `url` and these optional fields:

- `method`: string, default `GET`
- `headers`: record whose keys and values are strings
- `body`: string
- `timeout`: positive number of seconds

Records and arrays are not automatically converted to JSON request bodies.
Call `encode(value)` explicitly:

```basic
response = webclient.post(
    "https://api.example.com/events",
    encode({name:"launch", active:true})
)
```

Use `webclient.request` for custom headers:

```basic
headers = {}
headers["Content-Type"] = "application/json"
headers["X-Client"] = "gbasic"

response = webclient.request({
    method:"POST",
    url:"https://api.example.com/events",
    headers:headers,
    body:encode({name:"launch"})
})
```

Every completed HTTP exchange returns a response record with:

- `status`: numeric HTTP status code
- `reason`: reason text, or an empty string when unavailable
- `headers`: record with lowercase header names
- `body`: response body as a string
- `json`: decoded JSON value, present only when parsing succeeds

Test for the optional JSON field through dynamic record access:

```basic
response = webclient.get("https://api.example.com/events/1")
if not is_unknown(response["json"]) then
    print(response.json.name)
end if
```

Invalid JSON does not raise an error; `body` remains available and `json` is
omitted. HTTP statuses such as 404 and 500 also return normal response records.
Network, DNS, connection, TLS, malformed-URL, and timeout failures are runtime
errors with `error.source = "webclient"`.

libcurl is optional at build time. A build without it remains usable, but
`load webclient` reports that the module is unavailable. WebClient is for
outgoing requests only. Incoming requests use the separate WebServer module.

## WebServer Module

WebServer Phase 1 provides a loopback HTTP/1.1 server using live records,
ordinary arrays, and watchers:

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

The module provides:

- `webserver.listen(port)`
- `webserver.close(server)`
- `webserver.redirect(request, location)`
- `webserver.redirect(request, location, status)`

`port` must be an integer from 0 through 65535. Phase 1 binds to
`127.0.0.1`. Port `0` requests an operating-system-assigned ephemeral port,
which is exposed through `server.port`.

The returned live server record contains:

- `port`: actual bound port
- `running`: boolean listener state
- `requests`: incoming request queue
- `responses`: outgoing response queue

`server.requests` and `server.responses` use ordinary watcher behavior. Native
request arrival appends to `server.requests`; application code usually consumes
requests with `take_first(server.requests)` and queues replies with
`append(server.responses, ...)`. Those language-level queue mutations notify
watchers once after the stored array mutation completes.

Each request record contains:

- `id`: positive server-generated request ID
- `method`: uppercase method
- `path`: request path
- `query`: record of percent-decoded string parameters
- `headers`: record with lowercase names
- `cookies`: record parsed from the `Cookie` header
- `body`: request body string
- `json`: decoded JSON value, present only when parsing succeeds
- `remote_ip`: peer IP string
- `remote_port`: peer port number
- `timestamp`: UTC ISO 8601 string

Invalid JSON leaves `body` unchanged and omits `json`. Duplicate query
parameters and request headers use last-wins behavior.

Application code appends response records to `server.responses`:

```basic
headers = {}
headers["content-type"] = "application/json"

append(server.responses, {
    id:req.id,
    status:201,
    headers:headers,
    body:encode({saved:true})
})
```

`webserver.redirect(req, location)` returns a normal response record with
status `303`, a `location` header, and an empty body. The optional status must
be one of `301`, `302`, `303`, `307`, or `308`.

Response fields:

- `id`: required positive integer matching a pending request
- `status`: optional HTTP status, default `200`
- `headers`: optional record of string values, default `{}`
- `cookies`: optional array of `Set-Cookie` strings, default `[]`
- `body`: optional string, default `""`

The server supplies `Content-Length`, closes the connection after the response,
emits one `Set-Cookie` header per `cookies` item, and defaults the content type
to `text/plain`. If no matching response is queued within 30 seconds, the
client receives HTTP 504.

Shutdown forms:

```basic
webserver.close(server)
server.running = false
```

WebServer is single-threaded in Phase 1. It supports one request per connection
with `Content-Length` request bodies and explicit cookie parsing/emission. It
does not support chunked requests, public binding, routing APIs, middleware,
static files, templates, sessions, multipart uploads, streaming, WebSockets,
TLS, or asynchronous application code.

## XML Module

XML support is available when gBASIC is built with libxml2. It parses XML (and,
leniently, HTML) into an in-memory node tree and navigates it by path:

```basic
load xml

doc = xml.parse("<order id='A1'><item>Widget</item><item>Gadget</item></order>")
print(xml.attr(doc, "id"))              # A1
print(xml.text(xml.find(doc, "item")))  # Widget
```

Parsing:

- `xml.parse(text[, keep_whitespace])` — parse an XML string; pass `true` to keep
  inter-element whitespace text nodes (default drops them).
- `xml.parse_file(path)` — parse from a file path.
- `xml.parse_html(text)` — lenient HTML parse for real-world markup.

Navigation (a *path* is a `/`-separated chain of child element names):

- `xml.find(node, path)` — the first matching descendant, or `unknown`.
- `xml.find_all(node, path)` — an array of all matches (empty when none).
- `xml.attr(node, name[, default])` — an attribute value as a string; returns
  `default` (or `unknown`) when the attribute is absent.
- `xml.text(node)` — the node's text content.
- `xml.encode(node[, pretty])` — serialize a node back to an XML string; pass
  `true` to pretty-print.

Streaming reader, for documents too large to hold as a tree — it walks the file
forward without materializing the whole document:

- `xml.reader(path)` — open a streaming reader over a file.
- `xml.read(reader)` — advance to and return the next element, or `unknown` at end.
- `xml.skip_to(reader, name)` — advance until an element named `name`.
- `xml.subtree(reader)` — materialize the reader's current element as a node tree.
- `xml.close(reader)` — release the reader.

Parse failures and misuse (bad argument types, a closed reader, a non-element
subtree) raise structured errors. See `docs/xml_design.md` and the `examples/xml_*`
programs for the full surface and worked SEC-filing examples.

## GUI (GTK 3) Module — experimental

`gui` is an **experimental proof of concept** (Stage 6A), enabled only when
gBASIC is built with GTK 3 development files. It renders a record-defined window
tree, exposes widgets as addressable fields, and routes GTK-originated changes
back through ordinary watchers:

```basic
GBASIC_PATH=stdlib ./gbasic examples/gui/demo.bas
```

Because it is a POC on GTK 3 and its surface is likely to evolve, it is
documented by its design notes rather than pinned here: see
`docs/gui_design.md` and `examples/gui/README.md` for the implemented scope and
manual verification steps. For new GUI work prefer the `gi` bridge below, which
is the conventional route and targets current GTK (4). The two cannot be used in
the same process (see the note in the `gi` section).

## GObject-Introspection (GUI) Module

The `gi` module is a generic bridge to any GObject-based library — GTK 4, GLib,
Gio, and so on — driven at runtime by the library's GObject-Introspection
typelib. It is the path to native graphical applications; GTK 4 is the first
target toolkit, but nothing GTK is linked directly. It is available when gBASIC
is built with libgirepository-2.0 (GLib >= 2.80); otherwise `load gi` raises a
clean runtime error (`error.source = "gi"`), the same way the other optional
modules degrade.

```basic
load gi
gi.require("Gio", "2.0")
gi.require("Gtk", "4.0")

function on_click(source)
    gi.set(source, "label", "Clicked!")
end function

function on_activate(app)
    win = gi.new("Gtk.ApplicationWindow", "application", app)
    gi.set(win, "title", "Hello from gBASIC")
    button = gi.new("Gtk.Button")
    gi.set(button, "label", "Click me")
    gi.connect(button, "clicked", on_click)
    gi.call(win, "set_child", button)
    gi.call(win, "present")
end function

app = gi.new("Gtk.Application", "application-id", "org.example.Hello")
gi.connect(app, "activate", on_activate)
gi.call(app, "run", 0, nothing)
```

Native objects are opaque `gobject` values. Each underlying GObject maps to
exactly one wrapper, so identity is stable: `a = b` is true when they wrap the
same object. A wrapper's reference is released during interpreter cleanup;
freshly constructed objects have their floating reference sunk, and objects an
external owner already adopted at construction (for example a window whose
construct-time `application` adds it to the `Gtk.Application`) are ref-counted so
the program does not destroy them out from under the toolkit.

The module provides:

- `gi.require(namespace[, version])` — load a typelib namespace (for example
  `gi.require("Gtk", "4.0")`) so its types resolve. Must precede any use of the
  namespace.
- `gi.new(type_name[, prop, value, ...])` — construct an object by qualified
  type name (`"Gtk.Button"`). Trailing name/value pairs set construct-time
  properties, including construct-only ones such as
  `gi.new("Gtk.ApplicationWindow", "application", app)`.
- `gi.get(object, property)` / `gi.set(object, property, value)` — read and
  write a GObject property.
- `gi.call(object, method[, args...])` — call an instance method, resolved by
  walking the class hierarchy and implemented interfaces. Array/list parameters
  accept `nothing` for a NULL pointer (for example `gi.call(app, "run", 0, nothing)`).
- `gi.invoke("Namespace.function"[, args...])` — call a namespace-level free
  function that has no receiver, such as `gi.invoke("Gtk.init")` or
  `gi.invoke("GLib.markup_escape_text", "<a>", -1)`.
- `gi.connect(object, signal, function)` — connect a gBASIC function to a
  signal; returns a numeric handler id. The handler is called with the signal's
  arguments (the emitter, then any signal parameters).
- `gi.disconnect(object, handler_id)` — disconnect a previously connected
  handler.
- `gi.enum("Namespace.Enum.MEMBER")` — resolve an enum or flags member to its
  numeric value, for example `gi.enum("Gtk.Orientation.VERTICAL")`.
- `gi.is_a(object, type_name)` — true when the object is, or derives from, the
  named type.
- `gi.type_name(object)` — the object's GType name as a string.
- `gi.main()` / `gi.quit()` — run and stop a toolkit-agnostic GLib main loop
  owned by the program; an alternative to driving `GApplication.run` yourself.

Value mapping covers booleans, numbers (every integer and floating GValue type,
plus enums and flags as numbers), UTF-8 strings, objects (`gobject`), and
`nothing`/NULL. Unsupported types raise rather than silently mis-convert.
Container marshalling (arrays, lists, hashes) is not implemented beyond passing
`nothing` for a NULL pointer, and `out`/`inout` method parameters are not
supported. A signal handler that raises does not corrupt the outer program: the
error is surfaced, any running `gi.main()` loop ends, and the program's
error/line state is restored.

The GTK 3 `gui` module and the GTK 4 `gi` bridge cannot be used in the same
process; loading one after the other has taken the opposite GTK version raises a
structured error. Errors from the module use `error.source = "gi"`.

## GTK 4 helpers and SourceEditor

Two **pure-gBASIC** stdlib libraries make native GTK 4 code readable over the raw
`gi` bridge. They add no native code and hide no GTK semantics — every wrapper
returns the underlying GObject, so you drop to `gi` (`obj.method(...)`,
`obj.prop = ...`, `gi.connect`) for anything not wrapped. Load them with
`GBASIC_PATH` pointing at the dev tree's `stdlib` (or the installed path):

```sh
GBASIC_PATH=stdlib ./gbasic examples/native_editor/editor_demo.bas
```

Both require the GTK 4 / GtkSource 5 introspection typelibs at runtime
(`gir1.2-gtk-4.0`, `gir1.2-gtksource-5`), loaded through `gi` (not linked).

### `gtk` (`load gtk`)

Thin GTK 4 constructors, each returning the GTK GObject:

- `gtk.init()` — initialize GTK (needed before building widgets; needs a display).
- `gtk.application(app_id)` / `gtk.application_window(app)` / `gtk.window()`.
- `gtk.box(orientation, spacing)` / `gtk.paned(orientation)` — `orientation` is
  `"h"`/`"horizontal"` or `"v"`/`"vertical"`.
- `gtk.scrolled(child)` / `gtk.stack()` / `gtk.notebook()` / `gtk.listbox()`.
- `gtk.button(label)` / `gtk.label(text)`.
- `gtk.connect(widget, signal, handler)` — alias for `gi.connect` (named
  `connect`, not `on`, because `on` is a reserved keyword).
- `gtk.enum("Gtk.Orientation.VERTICAL")` — resolve an enum/flags member to its int.

This is a thin convenience layer, **not** a new widget framework and not the
declarative reconciler planned for a later phase.

### `sourceeditor` (`load sourceeditor`)

A reusable source/text editor over GtkSourceView 5. General-purpose — nothing is
named after or specific to any application. An editor is a plain gBASIC record
holding its `GtkSourceBuffer` plus methods; the `GtkSourceView` **widget** is
created lazily on first `view()`/`scroll_to()`/`add_inline()`, so the text,
language, cursor, mark, and highlight operations work **headlessly** (a buffer is
not a widget) and only the view-dependent operations need an initialized GTK and a
display.

- `sourceeditor.create()` — a new editor (buffer only until a view is needed).
- `sourceeditor.language(id)` / `sourceeditor.language_manager()` — resolve a
  `GtkSourceLanguage` / a search-path'd `GtkSourceLanguageManager` (headless).
- editor methods (call on the returned record):
  - `.get_text()` / `.set_text(s)`
  - `.set_language(id)` — assign a language (e.g. `"gbasic"`); raises if unknown.
  - `.cursor()` → `{line, column}` (0-based) / `.set_cursor(line, column)`
  - `.mark(line, category)` → a `GtkSourceMark` (the category string's meaning is
    the caller's — bookmark, diagnostic, breakpoint, …; not fixed by the library).
  - `.highlight(start_line, end_line, color)` → a `GtkTextTag` (whole-line
    background highlight) / `.unhighlight(tag)`.
  - `.on_change(fn)` — call `fn` on every buffer edit.
  - `.view()` — the `GtkSourceView` widget (created on first use; needs a display).
  - `.scroll_to(line)` / `.add_inline(line, widget)` — scroll to a line; anchor a
    generic GTK widget at a source line via `GtkTextChildAnchor` (needs a display).

### `gbasic.lang` (syntax definition)

gBASIC syntax highlighting is a GtkSourceView language definition shipped at
`stdlib/gtksourceview/gbasic.lang` (grounded in `docs/TOKENS.md`: comments,
strings with escapes, decimal/hex numbers, the case-insensitive keyword set,
`true`/`false` and `nothing`/`unknown` literals, declarations). `sourceeditor`
locates it by adding the stdlib's `gtksourceview/` directory to a
`GtkSourceLanguageManager` search path at runtime — derived from `GBASIC_PATH`
(dev tree) plus the documented install location
(`/usr/local/share/gbasic/stdlib/gtksourceview` by default). After `make install`
the `.lang` installs alongside the rest of the stdlib.

**SourceEditor vs. gBASIC Studio:** `sourceeditor` is a reusable, general editor
component. It is deliberately free of Studio concepts (projects, execution
boundaries, branches, inspector); a future gBASIC Studio would *use* this library,
not the other way around.

## Cryptography

When gBASIC is built with libcrypto (OpenSSL), a family of cryptographic builtins
is always available — no `load` is required. They operate on strings; hash, HMAC,
AEAD, and random outputs are **binary strings** (raw bytes), which you render with
`hex_encode`/`base64_encode` when you need text. `secure_token`, `password_hash`,
and `password_verify` are documented under [Core Builtin Functions](#core-builtin-functions).

Encoding:

- `base64_encode(s)` / `base64_decode(s)` — standard Base64.
- `base64url_encode(s)` / `base64url_decode(s)` — URL-safe Base64 (no padding).
- `hex_encode(s)` / `hex_decode(s)` — lowercase hexadecimal.

Random and comparison:

- `random_bytes(n)` — `n` cryptographically random bytes.
- `bytes_equal(a, b)` — constant-time string comparison (use for secrets/MACs).

Hashing and HMAC:

- `sha256(s)`, `sha512(s)`, `sha1(s)` — message digests.
- `hmac_sha256(key, message)`, `hmac_sha512(key, message)` — keyed MACs.

Authenticated encryption and signatures:

- `aes_gcm_encrypt(key, nonce, plaintext, aad)` — AES-GCM; returns ciphertext with
  its authentication tag. `key` is 16 or 32 bytes; `nonce` is 12 bytes.
- `aes_gcm_decrypt(key, nonce, blob, aad)` — verifies and decrypts; a wrong key,
  nonce, `aad`, or tampered blob raises rather than returning garbage.
- `ed25519_keypair()` — a record `{ private, public }`.
- `ed25519_sign(private, message)` — a signature.
- `ed25519_verify(public, message, signature)` — `true` / `false`.

Bad argument types, wrong key/nonce sizes, and failed authentication raise
structured errors — these builtins pre-validate rather than degrade.

A higher-level pure-gBASIC layer ships as `load crypto` (`stdlib/crypto.bas`),
built on the above: `sha256_hex`/`sha512_hex`, `random_hex`/`random_token`,
`sign_cookie`/`verify_cookie`, `csrf_token`/`csrf_check`, `encrypt`/`decrypt`,
a flat `json_encode`/`json_decode`, and `jwt_encode`/`jwt_verify` (HS256). See
`docs/crypto_design.md`.

## Process Module

`process.run` runs an external program synchronously and returns its result. It is
an **unconditional builtin** — no `load` is required. Arguments are passed directly
to the executable (via `execvp`); **no shell is invoked**, so spaces, quotes, `$`,
`;`, `*`, and backticks in arguments reach the child literally and are never
expanded or split.

```basic
r = process.run({ command: "git", args: ["status", "--porcelain"] })
print r.exit_code    ' 0
print r.stdout       ' captured standard output
print r.stderr       ' captured standard error
```

Options record:

- `command` (string, **required**) — the program to run. Looked up on `PATH` when it
  contains no `/`, otherwise run as a literal (relative or absolute) path.
- `args` (array of strings, optional) — the arguments after `command`. Default none.
- `cwd` (string, optional) — a directory to switch into before running the child.
- `timeout` (number, optional) — seconds; when the child outlives it, its whole
  process group is killed. Absent or `<= 0` means no limit.

Result record:

- `exit_code` (number) — the child's exit status when it exited normally; `-1` if it
  was terminated by a signal or killed by `timeout`.
- `stdout`, `stderr` (strings) — captured output, **binary-safe** (arbitrary bytes,
  including interior NULs, are preserved; use `byte_count`/`byte_at` for raw bytes).
- `success` (boolean) — `true` only when the child exited normally with code `0` and
  was not timed out.
- `signal` (number) — the terminating signal number, or `0` if none.
- `timed_out` (boolean) — `true` when the child was killed for exceeding `timeout`.

Semantics:

- **Nonzero exit is a normal result** — a child that runs and exits non-zero returns
  a record with that `exit_code`; it does **not** raise. This is what a caller
  driving `git`, a compiler, or a test runner needs.
- **Launch failure raises** — a missing/inaccessible executable, a `cwd` that cannot
  be entered, or an internal pipe/fork failure raises a runtime error
  (`error.source = "process"`), distinct from a child that ran and exited `127`.
- **Environment** is inherited from the interpreter (no per-call environment override
  in this version).
- **Blocking** — `process.run` waits for the child to finish and captures its output
  fully into memory (no streaming; large output uses proportional memory). To keep a
  GUI responsive, run it inside a spawned actor and deliver the result to the main
  loop via `gi.watch_mailbox` (the actor + event-loop pattern).

## Reflection Module

`reflect.*` is a general runtime reflection facility — an **unconditional builtin**
(no `load`) for debuggers, IDE variable inspectors, AI/Agent tools, serializers, and
testing tools. It exposes small **composable** primitives so a consumer can explore a
value **lazily**, rather than one recursive dump that would copy a huge or deeply
nested graph. **Reflectable is broader than serializable**: a live GObject is
reflectable (its kind and type) but not serializable.

Environment:

- `reflect.variables()` → a sorted string array of the **current scope's own**
  variable names (globals at top level; a function's own locals inside a function).
- `reflect.get(name)` → the value of a named variable (a copy); raises if unknown.

Value description (each takes one value):

- `reflect.kind(v)` → the value kind: `"number"`, `"string"`, `"boolean"`,
  `"nothing"`, `"unknown"`, `"array"`, `"record"`, `"function"`, `"gobject"`,
  `"gboxed"`, `"gvariant"`, `"datetime"`, `"duration"`, `"money"`, `"file"`,
  `"directory"`, `"actor"`, database-connection kinds, …
- `reflect.type(v)` → a refined type: a **GType name** for a live gobject/boxed value
  (e.g. `"GtkButton"`, `"GVariant"`), otherwise the kind. Never a raw pointer.
- `reflect.category(v)` → `"scalar"`, `"container"`, `"function"`, or `"foreign"`.
- `reflect.serializable(v)` → boolean; **true** iff the value (recursively, for
  arrays/records) can be `serialize`d. Foreign/live handles (gobject, gboxed, actor,
  DB connections) are `false`.
- `reflect.count(v)` → element count (array), field count (record), or byte length
  (string). Raises otherwise.
- `reflect.inspect(v)` → a **shallow** descriptor record
  `{ kind, type, category, serializable, count }`. Deliberately non-recursive — deep
  traversal is caller-driven via the traversal primitives, so inspection never
  auto-copies a large graph and never loops.

Traversal (composable, lazy — each returns a copy of one child):

- `reflect.fields(v)` → array of a record's field names (declaration order).
- `reflect.field(v, name)` → the value of a record field; raises if unknown.
- `reflect.element(v, index)` → the value of an array element; raises out of range.

Scope and semantics:

- **Environment scope**: v1 reflects the current evaluator's scope only. Enumerating
  **paused call frames**, other interpreters, or the full enclosing chain is deferred
  to a future interpreter-context refactor. Because builtins run in the caller's
  environment, `reflect.variables()` inside a function does see that function's own
  locals.
- **Ordering**: `variables()` is **sorted by name** (stable/deterministic);
  `fields()` follows the record's declaration order (matching `keys()`).
- **Records**: reflection sees a record's **own materialized fields**. PBI derivation
  copies inherited fields into the instance at construction, so derived fields appear
  as own fields; per-field derivation *policy* metadata is not exposed in v1.
- **Foreign values** are identified safely (kind + GType name) without dereferencing
  internals or exposing pointers, and report `serializable = false`.
- **Cycles**: gBASIC's value/copy semantics prevent records/arrays from forming true
  cycles, and `inspect` is shallow, so reflection cannot loop; the recursive
  `serializable` predicate is additionally depth-guarded.
- **Identity**: a stable cross-read identity token (for cycle-detection or
  change-tracking caches) is **not** provided in v1 — under copy semantics a value read
  from a variable is a fresh copy, so no stable record/array identity exists to expose.
- **Errors** are explicit and recoverable (`error.source = "reflect"`): unknown
  variable, `count`/`fields`/`field`/`element` on the wrong kind, index out of range,
  unknown field, or an unknown `reflect.*` function.
- **Security**: the runtime API permits inspection of any in-scope value; controlling
  *who* may reflect (e.g. an Agent/MCP permission layer) is a higher-level concern, not
  part of this module.

**Reflectable vs. serializable**: reflection describes a value's shape and identity of
kind without requiring it to be encodable; `serialize` encodes a value's data. A
gobject is `reflect.inspect`-able as `kind=gobject, type=GtkButton, serializable=false`
without exposing its internals.

## Core Builtin Functions

gBASIC includes always-available core functions that don't require loading libraries. These maintain strict type checking and provide clear error messages.

### Environment

**`env(name)`** - Reads an operating-system environment variable. `name` must
be a string. If the variable is set, `env(name)` returns its string value. If
the variable is not set, it returns `unknown`.

```basic
port = env("GBASIC_SITE_PORT")
if is_unknown(port) then
    port = "8080"
end if
```

### Timing

**`sleep(seconds)`** - Blocks the current program for at least `seconds`, then
returns `seconds`. `seconds` must be a non-negative number; fractional values
are honored to sub-second resolution (e.g. `sleep(0.25)` pauses a quarter
second). `sleep(0)` returns immediately. The pause resumes across signal
interruptions, so the full requested interval always elapses — the property
polling loops such as a filing monitor rely on.

```basic
poll_interval = 60          ' seconds between checks
while watching
    check_for_new_filings()
    sleep(poll_interval)
end while
```

### Secure Tokens

**`secure_token(length)`** - Returns a cryptographically random URL-safe token
using the characters `A-Z`, `a-z`, `0-9`, `-`, and `_`. `length` must be an
integer from `1` through `4096`. The return value is text, so it is suitable for
session ids, CSRF tokens, invite codes, and similar application secrets.

```basic
session_id = secure_token(43)
csrf_token = secure_token(43)
```

### Current Time

**`now()`** - Returns the current local time as a second-precision `datetime`
value. It takes no arguments. Because `datetime` values support duration
arithmetic and exact comparison, `now()` is the primitive for expiration and
deadline logic; everything else (a date-only "today", relative windows) derives
from it via durations and the date/time lenses.

```basic
deadline = now() + 8 hours
expired = now() > deadline          ' false until the deadline passes
today(day)= now()                   ' truncate to date precision via the day lens
```

There is intentionally no `today()` builtin: `today` is too common an
identifier to reserve, and the date is derivable from `now()` and the `(day)=`
truncation lens as shown above.

**`epoch(datetime)`** converts a `datetime` to a number of seconds since the
Unix epoch, and **`from_epoch(number)`** converts such a number back to a
`datetime`. These bridge to systems that speak epoch seconds (for example JWT
`exp` claims). `number(datetime)` is equivalent to `epoch(datetime)`.

```basic
issued = now()
exp = epoch(issued + 1 hour)     ' seconds since 1970 for the token deadline
when = from_epoch(exp)           ' back to a datetime
```

### Password Hashing

**`password_hash(password)`** - Hashes a password string using the platform's
preferred libxcrypt password-hashing method. The returned string contains the
algorithm, parameters, salt, and hash, so it can be stored directly in an
application user table.

**`password_verify(password, hash)`** - Returns `true` when the password string
matches a stored hash from `password_hash()`, and `false` otherwise. Verification
uses the algorithm and parameters embedded in the stored hash.

```basic
stored_hash = password_hash("correct horse battery staple")
if password_verify("candidate password", stored_hash) then
    print("ok")
end if
```

### Type Inspection

**`type(value)`** - Returns the type of a value as a string:
```basic
type(42)         # "number"  
type("hello")    # "string"
type(true)       # "boolean"
type([1, 2])     # "array"
type({x:1})      # "record"
type(nothing)    # "nothing"
type(unknown)    # "unknown"
```

**Type predicates** - Return `true` or `false`:
- `is_string(value)` - checks if value is a string
- `is_number(value)` - checks if value is a number  
- `is_boolean(value)` - checks if value is a boolean
- `is_array(value)` - checks if value is an array
- `is_record(value)` - checks if value is a record
- `is_nothing(value)` - checks if value is `nothing`
- `is_unknown(value)` - checks if value is `unknown`

### Strict Conversion

**`string(value)`** - Converts values to strings using canonical string representation.

**`number(value)`** - Converts strings to numbers:
```basic
number("42")     # 42
number("3.14")   # 3.14
number("abc")    # runtime error
```

**`boolean(value)`** - Converts strings to booleans:
```basic
boolean("true")   # true
boolean("false")  # false
boolean("maybe")  # runtime error
```

**`array(value)`** - Decodes JSON strings to arrays, passes arrays unchanged:
```basic
array("[1, 2, 3]")  # [1, 2, 3]
array([4, 5, 6])    # [4, 5, 6]
```

**`record(value)`** - Decodes JSON strings to records, passes records unchanged:
```basic
record("{\"x\":1, \"y\":2}")  # {x:1, y:2}
record({a:1, b:2})            # {a:1, b:2}
```

### String Helpers

**`replace(text, from, to)`** - Replaces all occurrences of `from` with `to`:
```basic
replace("hello", "l", "x")     # "hexxo"
replace("hello world", "o", "0")  # "hell0 w0rld"
```

**`starts_with(text, prefix)`** - Returns `true` if text starts with prefix:
```basic
starts_with("hello", "he")     # true
starts_with("hello", "lo")     # false
```

**`ends_with(text, suffix)`** - Returns `true` if text ends with suffix:
```basic
ends_with("hello", "lo")       # true
ends_with("hello", "he")       # false
```

**`repeat(text, count)`** - Repeats text count times:
```basic
repeat("ha", 3)                # "hahaha"
repeat("x", 0)                 # ""
```

**`chr(code)`** - Returns the string for a Unicode **codepoint** in the range
`0 .. 0x10FFFF` (excluding the surrogate range `0xD800..0xDFFF`), UTF-8 encoded.
`chr(0)` produces a one-byte binary-safe NUL string (gBASIC strings are
binary-safe — see *Strings and Unicode*):
```basic
chr(110)                       # "n"
chr(233)                       # "é"  (one codepoint, 2 UTF-8 bytes)
chr(128512)                    # "😀" (one codepoint, 4 UTF-8 bytes)
chr(0)                         # a one-byte NUL string
```

**`code(text)`** - Returns the **codepoint** value of the first character of a
non-empty string; the inverse of `chr`:
```basic
code("n")                      # 110
code("é")                      # 233
code(chr(128512))             # 128512
```

**`byte_count(text)`** - Number of raw bytes in a string (`len` counts
codepoints):
```basic
byte_count("café")             # 5   (len("café") is 4)
```

**`byte_at(text, index)`** - The byte (0–255) at a 0-based byte index:
```basic
byte_at("ABC", 0)              # 65
```

**`from_bytes(numbers)`** - Builds a binary-safe string from an array of byte
values `0..255`:
```basic
from_bytes([72, 105])          # "Hi"
from_bytes([0, 255])           # a two-byte binary string
```

### Record Helpers

**`keys(record)`** - Returns array of key strings:
```basic
keys({x:1, y:2})               # ["x", "y"]
keys({})                       # []
```

**`values(record)`** - Returns array of record values:
```basic
values({x:1, y:2})             # [1, 2]
values({})                     # []
```

**`has(record, key)`** - Returns `true` if record contains key:
```basic
has({x:1, y:2}, "x")           # true
has({x:1, y:2}, "z")           # false
```

**`remove_key(record, key)`** - Returns new record without the key (immutable):
```basic
rec = {x:1, y:2}
new_rec = remove_key(rec, "x")  # {y:2}
# rec is unchanged: {x:1, y:2}
remove_key(rec, "z")            # {x:1, y:2} (copy when key missing)
```

### Counting

**`count(value)`** - Returns count/length for strings, arrays, and records:
```basic
count("hello")                 # 5 (string length)
count([1, 2, 3])               # 3 (array elements)
count({x:1, y:2})              # 2 (record fields)
count("")                      # 0
count([])                      # 0
count({})                      # 0
```

### Core vs Library Functions

**Core functions** (`string`, `number`, `type`, etc.) are always available and don't require `load`.

**Conversion function differences:**
- `string(value)` - canonical string conversion for any value
- `encode(value)` - JSON serialization for structured data
- `decode(text)` - JSON parsing to recreate values
- `quote(value)` - gBASIC source code literal with escaping
- `serialize(value)` - exact binary round-trip serialization (see below)
- `deserialize(bytes)` - reconstruct a value from `serialize` output

**`serialize` / `deserialize` vs `encode` / `decode`.** `encode`/`decode` use
JSON — human-readable, lossy for gBASIC's typed values (a date or money round-trips
as a string or number). `serialize` produces an opaque **binary-safe string** that
`deserialize` turns back into an *exact* copy, preserving type and binary content
(including interior NUL bytes), across numbers, strings, booleans, `nothing`,
`unknown`, arrays, records (nested), dates/times, durations, money, and file/
directory references:

```basic
cost(USD)= 9.99
deserialize(serialize({when: now(), cost: cost}))   # exact copy, types intact
```

(Build typed values before placing them in a record literal: inside a literal the
`name (…)` prefix is reserved for PBI field policies, so `cost(USD): 9.99` is not a
money field — see *Objects (Policy-Based Inheritance)*.)

Live database connections cannot be serialized (a structured error is raised), and
corrupt or truncated input to `deserialize` raises a structured error rather than
returning a partial value. A record's PBI policies are not preserved — a
deserialized snapshot is plain `copy` — which is the same degradation a value
undergoes when sent across an actor boundary (see Actors, below). `serialize` is
useful for deep-copying and persisting values, and is the foundation the actor
message transport is built on.

### Strings and Text

- `lower(text)` / `upper(text)` — ASCII case folding (see the string type for the Unicode caveat).
- `left(value, count)` / `right(value, count)` — leading / trailing characters.
- `mid(value, start, count)` — a substring; `start` is **0-based**.
- `mid(value, start, count, replacement)` — substring replacement.
- `reverse(text)` — reverse a string.
- `trim(text)` — strip surrounding whitespace.
- `split(text)` / `split(text, separator)` — split into an array.
- `join(array)` / `join(array, separator)` — join string elements.
- `join_from(array, start_index, separator)` — join from `start_index` to the end; returns `""` when the start is out of range.
- `chr(code)` / `code(text)` — codepoint ↔ character.
- `byte_count(text)` / `byte_at(text, index)` / `from_bytes(numbers)` — byte-level access to a string's UTF-8 bytes.
- `find(value, target)` — index of a substring in a string, or of an element in an array; `-1` / `nothing` when absent.

### Arrays

Access and search:

- `contains(array, value)` — true when a matching element is present.
- `remove_value(array, value)` — remove the first matching value and return the resulting array; when the first argument is an assignable path, the array is updated in place.
- `find_by(records, field_name, value)` — the first matching record index, or `nothing`.
- `first(array)` — the first element, or `nothing`.
- `rest(array)` — a new array without the first element.

Mutation:

- `append(array, value)` / `prepend(array, value)`
- `insert(array, index, value)` / `remove(array, index)`
- `take_first(array)` / `take_last(array)`
- `reverse(array)` / `unique(array)` / `sort(array)`

When `append`, `prepend`, `insert`, `remove`, `remove_value`, `take_first`,
`take_last`, `reverse`, `sort`, or `unique` mutates a stored array through an
assignable path, matching watchers are notified once after the completed
mutation. Mutators that leave the stored array unchanged, such as no-match
`remove_value`, already-sorted `sort`, already-unique `unique`, or a no-op
`reverse`, do not notify watchers.

`sort()` and `unique()` support scalar arrays. Date/time values use exact
date/time equality and ordering, not same-day or same-month comparison.

Aggregates:

- `len(value)` — length of an array or string.
- `sum(array)` / `mean(array)` / `median(array)` / `mode(array)` — numeric aggregates.
- `min(array)` / `max(array)` — extremes.

### Bitwise

Bitwise functions operate on 32-bit unsigned integers and raise on non-integer
or out-of-range input:

- `band(a, b)` / `bor(a, b)` / `bxor(a, b)` — bitwise AND / OR / XOR.
- `bnot(a)` — bitwise NOT (32-bit complement).
- `shl(value, count)` / `shr(value, count)` — logical left / right shift.
- `rotl(value, count)` / `rotr(value, count)` — 32-bit rotate left / right.

### Numbers and Comparison

- `round(number, places)` — round to a number of decimal places.
- `compare(a, operator, b)` — compare two values using an operator named as a
  string (the general form behind the comparison operators and modifiers).

### Input

- `input(prompt)` — read a line; always returns a **string**. Wrap it with
  `(number)` when you want numeric input. Arithmetic operand rules are in
  [Expressions](#expressions).

### Serialization and Display

`string(value)` and the `(string)` modifier use the same canonical conversion:

```basic
text = string(project)
label(string)= count
line = "You were born in " + birth_year + "."
```

Numbers print as normal numbers, strings stay unquoted, booleans become `true`
or `false`, `nothing` becomes `nothing`, `unknown` becomes `unknown`, and arrays
and records use the same textual shape as `encode(value)`.

`encode`/`decode` are serialization meant to round-trip through `decode`:

```basic
text = encode(project)
loaded = decode(text)
```

`encode` supports numbers, strings, booleans, `nothing`, `unknown`, arrays, and
records. Strings escape quotes, backslashes, tabs, carriage returns, and
newlines. Records are encoded as JSON-like objects with quoted field names.
`decode` reads the same format back into gBASIC values, accepts JSON `null` as
`nothing`, supports standard JSON string escapes, and raises a runtime error for
malformed text. `string(value)` may look the same as `encode(value)` for arrays
and records, but `string` is canonical display text while `encode` is
serialization.

`quote(value)` returns a complete gBASIC string literal, surrounding double
quotes included:

```basic
line = "description = " + quote(description)
```

String contents escape `"`, `\`, tab, carriage return, and newline (newlines as
`\n`, not literal breaks). Scalar non-string values use the same conversion as
`string(value)`, so `quote(42)` is `"42"` and `quote(nothing)` is `"nothing"`.
Arrays, records, files, directories, and other non-scalar values raise. Because
the output is decode-compatible, `decode(quote(text))` round-trips strings.

### Files and Directories

File functions (see also the file/directory value types):

- `exists(f)` — whether the path exists.
- `read(f)` / `write(f, text)` / `append(f, text)` — whole-file text I/O.
- `bytes(f)` / `lines(f)` / `chars(f)` — read as bytes, lines, or characters.
- `lock(f)` / `unlock(f)` — advisory locks (see `with lock`).

Directory functions:

- `list(folder)` / `files(folder)` / `folders(folder)` — directory entries.

Directory entries are records with `name`, `path`, and `type`.

## Standard Library

Beyond the native modules and core builtins, gBASIC ships a set of **pure-gBASIC**
toolkits under `stdlib/*.bas`, loaded by name (`load stats`) with `GBASIC_PATH`
pointing at the library directory (or resolved from the install path after
`make install`). This is a catalog; each toolkit's full API lives in its own
document, which is the single source of truth for it — the entries below only say
what each is and where to read it.

General-purpose:

- `dates` — calendar/date-arithmetic helpers over the built-in date/time values.
- `matrix` — minimal vector/matrix primitives (`docs/statistics_design.md` §8).
- `frame` — a structural data-frame layer (`docs/statistics_design.md` §4).
- `stats` — higher-level statistical compositions built on `matrix`/`frame`
  (`docs/statistics_design.md`; worked walk-throughs in
  `docs/cookbook_social_behavioral.md` and `docs/cookbook_econometrics_finance.md`).
- `crypto` — ergonomic cryptography compositions over the crypto builtins
  (see [Cryptography](#cryptography) and `docs/crypto_design.md`).
- `llm` — a chat-completion client (`docs/llm_design.md`).
- `gui` — the declarative layer for the experimental GTK 3 `gui` module
  (`docs/gui_design.md`).
- `gtk` — thin ergonomic GTK 4 constructors over the `gi` bridge (see
  [GTK 4 helpers and SourceEditor](#gtk-4-helpers-and-sourceeditor)).
- `sourceeditor` — a reusable GtkSourceView 5 source/text editor (same section).

EDGAR / SEC-filings suite (governed by `docs/edgar_design.md`, with
`docs/edgar_reference.md` and `docs/edgar_tutorial.md`):

- `edgar` — EDGAR acquisition core.
- `fundamentals` — 10-K/10-Q numerics over EDGAR companyfacts.
- `forensics` — accounting-honesty metrics (Beneish and related).
- `insiders` — Form 4 insider transactions over the `xml` module.
- `ownership` — 13F holdings and quarter-over-quarter deltas.
- `screener` — the whole-market bulk tier.
- `mdna` — MD&A / risk-factor extraction from 10-K/10-Q HTML.

## Living Examples

- `examples/adventure/adventure.bas` is a small text adventure using current input, print, modifiers, arrays, functions, `if`/`else`, `while`, and `break`.
- `examples/adventure/NOTES.md` records design-friction notes from that example.

## CLI Flags

Run a program, optionally passing it arguments:

```sh
gbasic FILE [args...]
```

Any arguments after `FILE` bind to the program block's first parameter as a
0-based string array.

Show help:

```sh
gbasic --help
```

Show version:

```sh
gbasic --version
```

Print lexer tokens:

```sh
gbasic --tokens FILE
```

Print AST:

```sh
gbasic --ast FILE
```

Suggest explicit `load` statements:

```sh
gbasic --add-loads FILE
```

`--add-loads` prints modified source to stdout. It does not overwrite the input file.
Compatibility note: `--add-uses` remains an alias that emits `use` statements.

Emit diagnostics as JSON:

```sh
gbasic --json-diagnostics FILE
```

`--json-diagnostics` runs the program but reports parse and runtime diagnostics
as JSON on stderr (the model the language server consumes); normal program
output on stdout is unchanged. A separate `gbasic-lsp` binary (built by
`make dev`) serves the same diagnostics over LSP on document sync.

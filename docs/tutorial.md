# gBASIC Tutorial

gBASIC is an experimental BASIC-family language for readable, business-style
programs. It keeps BASIC's plain syntax but adds modern ideas you don't usually
find together: domain-aware **modifiers/lenses**, reactive **watchers**,
prototypal objects with **Policy-Based Inheritance**, shared-nothing **actors**,
and first-class **functions and methods** — over a small, dependency-light C
interpreter.

This tutorial teaches the current v0.1 implementation through short, runnable
examples. Every example here runs on the interpreter as written. For exhaustive
detail on any topic, see the companion [`reference.md`](reference.md); for
worked programs, see `examples/`.

> **How to read this.** Sections marked **Theory** explain the idea *behind* a
> feature — why it exists and how to think about it. They are the parts most
> worth reading slowly, because several gBASIC features look familiar but behave
> differently from their lookalikes in other languages.

## Your first twenty minutes

Each of these is covered properly further down. They are collected here because
they are the ones people hit *before* they get that far — cases where the
obvious guess is close enough to look right and fails on a detail.

| You'll probably write | It actually is | Why |
|---|---|---|
| `x (upper)= "ada"` | `x (uppered)= "ada"` | `upper` is a **builtin** you call; `uppered` is the **modifier** applied at assignment. Same for `lower`/`lowered`, `trim`/`trimmed`. → [Modifiers](#modifiers-and-lenses) |
| `p = $19.99` | `p(USD)= 19.99` | There is no money *literal*; a modifier gives the value its kind. The result is a real `money`. → [Money & dates](#money-dates-times-and-durations) |
| `d = today()` | `d(day)= now()` | There is deliberately no `today()`; derive a date from `now()` with the `(day)` lens. → [Money & dates](#money-dates-times-and-durations) |
| `w = spawn worker` | `w = spawn worker(self())` | `spawn` needs the call form even when you pass nothing of your own — otherwise it is a *parse* error. → [Actors](#actors-and-concurrency) |
| `watch(a)` before `a` exists | declare `a` first, then `watch(a)` | A watcher reads its variables immediately. Written first it raises `undefined variable`, **carries on**, and the value you expected reads as `nothing` later. → [Watchers](#watchers) |

Two more worth knowing before you start:

- **`!=`, not `<>`**, for "not equal".
- **`mid` is 0-based and strings are not indexable** — `s[0]` raises; use
  `mid(s, 0, 1)`.

## Contents

- [Running programs](#running-programs)
- [Variables](#variables)
- [Strings, Unicode, and bytes](#strings-unicode-and-bytes)
- [Modifiers and lenses](#modifiers-and-lenses)
- [Arrays and records](#arrays-and-records)
- [Nothing and unknown](#nothing-and-unknown)
- [Control flow](#control-flow)
- [Functions](#functions)
- [First-class functions and methods](#first-class-functions-and-methods)
- [Objects: Policy-Based Inheritance](#objects-policy-based-inheritance)
- [Watchers](#watchers)
- [Actors and concurrency](#actors-and-concurrency)
- [Errors](#errors)
- [Money, dates, times, and durations](#money-dates-times-and-durations)
- [Files and locks](#files-and-locks)
- [Serialization](#serialization)
- [Core builtin functions](#core-builtin-functions)
- [Libraries and load](#libraries-and-load)
- [Databases: SQLite and PostgreSQL](#databases-sqlite-and-postgresql)
- [Calling web APIs (WebClient)](#calling-web-apis-webclient)
- [Serving HTTP requests (WebServer)](#serving-http-requests-webserver)
- [Security helpers: passwords and tokens](#security-helpers-passwords-and-tokens)
- [Larger example and where to go next](#larger-example-and-where-to-go-next)

## Running programs

```basic
print("hello, world")
```

Run it:

```sh
./gbasic hello.bas
```

Programs may be wrapped explicitly in a `program` block:

```basic
program demo(args)
    print("hello from program")
end program
```

If a file has no `program` block, the whole file is treated as an implicit
program. `args` is the array of command-line arguments.

`print` writes to standard output — the program's data. Anything that is *not*
data (progress, warnings, usage text) belongs on standard error, which is what
`print to error` is for:

```basic
print to error "reading input..."
print("the answer is 42")
```

Keeping them apart is what lets your program be used in a pipeline: the reader
downstream gets the data alone, and the messages still reach the terminal.

```sh
./gbasic report.bas > data.csv       # messages on screen, data in the file
./gbasic report.bas 2>/dev/null      # or hide the messages entirely
```

Useful CLI flags:
`./gbasic --tokens file.bas` (dump tokens), `--ast` (dump the parse tree),
`--add-loads` (suggest `load` lines), `--line-buffered` (flush stdout at every
line, so output is visible as it is printed when you pipe it somewhere),
`--version`.

## Variables

Assign with `=`. Types are dynamic; values are typed.

```basic
name = "Ada"
age = 37
ready = true

print(name)
print(age + 1)
print(ready)
```

Identifiers are case-sensitive. Strings use double quotes and may contain literal
newlines:

```basic
description = "You are in a stone hall.
Water drips from the ceiling."
print(description)
```

Escapes `\n`, `\t`, `\"`, `\\`, and `\u{...}` work. An unknown escape, or a
string that reaches end-of-file before its closing quote, is an error.

## Strings, Unicode, and bytes

A gBASIC string is a **binary-safe sequence of bytes**, UTF-8 by convention. It
carries an explicit length, so *any* byte — including a NUL (`chr(0)`) — is valid
content. Strings are not NUL-terminated from your program's point of view.

String operations come in two families:

**Character-oriented (Unicode codepoints).** `len`, `left`, `right`, `mid`,
`reverse`, `find`, `chr`, and `code` count and slice by **codepoint**, so they
never split a multibyte character:

```basic
len("café")            # 4   codepoints
mid("café", 3, 1)      # "é"  (0-based; never splits a codepoint)
chr(233)               # "é"  one codepoint, 2 UTF-8 bytes
chr(128512)            # "😀" one codepoint, 4 UTF-8 bytes
code("é")              # 233
"\u{1F600}"            # "😀"  codepoint escape by hex value
```

**Byte-oriented (raw).** `byte_count`, `byte_at` (0-based), and `from_bytes`
work on raw bytes, for binary and protocol work:

```basic
byte_count("café")     # 5   UTF-8 bytes (len is 4)
byte_at("ABC", 0)      # 65
from_bytes([72, 105])  # "Hi"
from_bytes([0, 255])   # a two-byte binary string
```

> **Theory — why two families.** Most languages force one model: either a string
> is "characters" (and binary data is awkward) or it is "bytes" (and Unicode is
> awkward). gBASIC keeps both views over the *same* binary-safe storage. Text
> code stays codepoint-correct without a separate type, while protocol/file code
> can drop to bytes when it needs to. Comparison is by byte sequence (which is
> also correct codepoint order for valid UTF-8). Case folding (`upper`, `lower`,
> `{caseless}`) is **ASCII-only** by design in v0.1: `A–Z`↔`a–z` fold and every
> other byte is left exactly as-is, so `upper("café")` is `"CAFÉ"` with `é`
> unchanged. Full Unicode case folding and normalization are future work.

## Modifiers and lenses

A **modifier** attaches domain meaning to an *assignment* or a *comparison*. It
is gBASIC's most distinctive everyday feature.

Assignment modifiers transform the value being stored. They use parentheses:

```basic
price(USD)= 19.95          # money value, stored as cents
due(date)= "2026-05-15"    # a date value
command(trimmed)= input(">")
command(lowered)= command
code(uppered)= "abc"
words(split)= "apple banana orange"   # -> ["apple","banana","orange"]
line(join ", ")= words                # -> "apple, banana, orange"
n(length)= line
age(number)= "42"
text(string)= age
```

Comparison modifiers (also called **lenses**) transform or implement a
comparison. They use **brace** syntax `{ ... }` between the two sides:

```basic
if "Joe Barnes"{caseless}= "joe barnes" then
    print("match")
end if
```

You can define your own. An assignment modifier receives `value`; a comparison
modifier receives `left`, `right`, and `operator`:

```basic
modifier shout for assign
    return upper(value)
end modifier

modifier rounded(n) for compare
    return compare(round(left, n), operator, round(right, n))
end modifier

a = 1.234
b = 1.235
if a {rounded 2}= b then
    print("rounded match")
end if
```

> **Theory — meaning at the boundary.** A value's *interpretation* often lives
> at the moment you store or compare it, not in a separate type declaration.
> "These are US dollars," "compare these case-insensitively," "truncate this
> timestamp to a day." gBASIC lets you name that interpretation exactly where it
> applies — the assignment or the comparison — instead of wrapping everything in
> classes or helper calls. Built-in modifiers (`USD`, `date`, `trimmed`,
> `caseless`, the date/time lenses, …) cover common cases; libraries can export
> more (`export modifier …`). Note the syntactic split: `(...)=` is **assignment**,
> `{...}=` is **comparison**. The older parenthesized comparison form
> (`name(caseless)= …`) still works but is deprecated in favor of braces.

## Arrays and records

Arrays use brackets and are 0-indexed:

```basic
scores = [88, 91, 91, 74, 100]
print(scores[0])
print(len(scores))
print(mean(scores))
print(mode(scores))
```

Array helpers modify or return arrays:

```basic
items = ["banana"]
append(items, "orange")
prepend(items, "apple")
print(join(items, ", "))

if contains(items, "banana") then
    print("has banana")
end if
```

`remove_value(items, value)` removes the first match; `first(items)` /
`rest(items)` split head and tail; `find_by(records, field, value)` returns the
first matching index or `nothing`.

Records use braces. Fields may be written `name = value` or `name: value`:

```basic
customer = {
    name = "Ada",
    age = 37,
    balance = 120.00
}

print(customer.name)
customer.name = "Grace"

field = "age"
print(customer[field])          # dynamic access; key must be a string
print(customer["nickname"])     # missing dynamic field reads as unknown
customer["nickname"] = "Amazing Grace"   # assignment creates it

items = [{name = "lamp", location = "cellar"}]
items[0].location = "inventory"
```

Static field access uses a literal name (`customer.name`); dynamic access
(`customer[field]`) evaluates the key at runtime. Arrays take numeric indexes;
records take string keys.

## Nothing and unknown

gBASIC has **two** absence values, and the distinction matters:

```basic
x = nothing      # deliberate absence — "there is none"
y = unknown      # exists, but the value is not known yet

if x != y then
    print("distinct")
end if
```

> **Theory — absence vs. ignorance.** A single `null` conflates two different
> facts: "this has no value" and "we don't know this value." gBASIC separates
> them. `nothing` is a chosen, final absence (an empty result, a removed field).
> `unknown` is a placeholder for information that exists but hasn't arrived —
> a missing dynamic record field reads as `unknown`, and `env(name)` returns
> `unknown` for an unset variable. Keeping them distinct lets you branch on
> "not set yet" without mistaking it for "set to empty." Test with `is_nothing`
> and `is_unknown`.

## Control flow

`if`/`else`:

```basic
if ready then
    print("ready")
else
    print("waiting")
end if
```

A single statement may follow `then`/`else` on the same line; an inline final
branch ends the conditional without `end if`:

```basic
if ready then print("ready")
else print("waiting")
```

When the final branch starts on the next line, keep `end if`. Inline branches
hold one simple statement (assignment, `print`, a call, `return`, `goto`,
`break`, `continue`); use the multiline form for nested blocks.

`while` with `break`/`continue`:

```basic
i = 0
while i < 5
    i = i + 1
    if i = 3 then continue
    print(i)
end while
```

`for each` iterates arrays (and `keys(...)`/`values(...)` of records). The
`each` word is optional — `for v in xs` is the same as `for each v in xs`:

```basic
for each item in ["lamp", "key", "coin"]
    print(item)
end for

player = {name: "Ada", score: 10}
for each k in keys(player)
    print(k + ": " + string(player[k]))
end for
```

`consider` is a clean multi-way dispatch on one subject:

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

The subject after `consider` is evaluated once. Each `if expr then` branch at the
same indentation as `consider` compares the subject with `=`; only the first
match runs. `else` runs if nothing matched. `break` exits the `consider`.

## Functions

```basic
function add(a, b)
    return a + b
end function

print(add(2, 3))
```

Parameters and other assignments inside a function are local; globals remain
visible unless shadowed. Function calls are expressions (so they can appear in
comparisons) but **not** assignment targets — `len(words) = 0` is invalid.
Labels with `goto`/`gosub` are available inside functions.

## First-class functions and methods

A bare function name is a **value**: you can store it, pass it, compare it, and
call it.

```basic
function greet(name)
    return "hello, " + name
end function
function double(n)
    return n * 2
end function

g = greet                  # a function value
print(type(g))             # "function"
print(g("world"))          # "hello, world"

function apply_twice(f, x)
    return f(f(x))
end function
print(apply_twice(double, 5))   # 20

print(g = greet)           # true  (same-reference equality)
print(g != double)         # true
```

A function value stored in a **record field** becomes a **method**. When you
call it through the object, `this` is bound to that object at the call site:

```basic
function deposit(amount)
    this.balance = this.balance + amount
    return this.balance
end function
function describe()
    return this.name + ": " + string(this.balance)
end function

account = { name: "checking", balance: 0, deposit: deposit, describe: describe }
print(account.deposit(100))    # 100
print(account.deposit(50))     # 150
print(account.describe())      # "checking: 150"
print(account.balance)         # 150  — the write persisted
```

Dispatch falls out naturally: two records with the same field name holding
different function values do different things.

```basic
function loud()
    return upper(this.text)
end function
function soft()
    return lower(this.text)
end function
a = { text: "Hello", say: loud }
b = { text: "Hello", say: soft }
print(a.say())     # "HELLO"
print(b.say())     # "hello"
```

There is **define-and-attach** sugar for adding a method to an existing record.
`function obj.name(...)` is an executable statement — it is *not* hoisted, so
`obj` must already be a record when the line runs:

```basic
account = { name: "checking", balance: 0 }

function account.deposit(amount)
    this.balance = this.balance + amount
    return this.balance
end function

print(account.deposit(200))    # 200
print(type(account.deposit))   # "function"
```

> **Theory — references, not closures (yet).** A gBASIC function value is a
> *reference to a function*, not a closure that captures surrounding variables.
> Its behavior is fixed; its *receiver* is not. Methods get their object from the
> **call site** (`this` is whatever you called the function *through*), which is
> why the same function can serve as a method on many different records, and why
> dynamic dispatch needs no class hierarchy — just "which function value is in
> this field." Closures, bound methods, and inline lambdas are deliberately
> deferred. (`this.field = …` writes through the object and honors PBI policies —
> see the next section.)

## Objects: Policy-Based Inheritance

gBASIC objects are **prototypal**: a record is a prototype, and `new <prototype>`
derives an instance. An optional `with { … }` block customizes it.

```basic
account = { owner: "unnamed", balance: 0 }
a = new account with { owner: "Ada", balance: 100 }
print(a.owner)     # "Ada"
```

What makes inheritance interesting in gBASIC is that each field can declare a
**policy** — written in parentheses before the `:` — that controls how it behaves
on derivation:

| Policy | Meaning |
| --- | --- |
| `copy` (default) | Instance gets an independent value; writes never affect the prototype or siblings. (Copy-on-write under the hood.) |
| `link` | Instance shares one storage cell with the prototype; a write through any alias is visible to all. |
| `reset <expr>` | `<expr>` is re-evaluated at each `new`, so every instance gets a fresh value. |
| `exclude` | Field stays on the prototype but is omitted from instances. |

```basic
function next_id()
    return secure_token(6)
end function

widget = {
    name   (copy):  "unnamed",   # private per instance
    pool   (link):  "main",      # shared across instances
    id     (reset next_id()): "",# fresh per instance
    scratch (exclude): "tmp"     # not present on instances
}

a = new widget with { name: "A" }
b = new widget with { name: "B" }
print(a.id != b.id)          # true  — reset fired twice
print(has(a, "scratch"))     # false — excluded
```

A field named `constructor` is special: `new` calls it automatically after
derivation, with `this` set to the new instance. Inputs arrive via `with { … }`
and are read off `this`:

```basic
function constructor()
    this.balance = this.opening
    this.label = "acct:" + this.name
end function

Account = { name: "", opening: 0, balance: 0, label: "", constructor: constructor }
a = new Account with { name: "alice", opening: 100 }
print(a.balance)   # 100
print(a.label)     # "acct:alice"
```

> **Theory — inheritance as a per-field policy.** Classical OO bundles one
> inheritance rule for a whole class (and you fight it with overrides, mixins,
> `static`, deep-copy helpers). PBI turns the inheritance *rule itself* into data
> attached to each field. Want shared state? `link`. Want a fresh id, clock, or
> RNG seed per instance? `reset`. Want private-by-default value semantics? `copy`
> (the default). Derivation is recursive — a `copy` field that holds an instance
> is re-derived, so nested `reset`s re-fire — and instances keep their policies,
> so an instance can serve as a prototype for further derivation. Because
> policies are per field, you compose behavior without a class tree. See
> [pbi_design.md](pbi_design.md) for the full rationale.

## Watchers

A **watcher** is a reactive block over one or more stored paths. It runs once
when registered, then again — synchronously — after any later change to a
matching path.

```basic
a = 10
b = 20

watch(a, b)
    c = a + b
end watch

print(c)     # 30
a = 15
print(c)     # 35  — the watcher re-ran when `a` changed
```

Equal-value writes do not trigger watchers, so there are no redundant runs:

```basic
count = 0
watch(count)
    print(count)
end watch
count = 0    # no run (unchanged)
count = 1    # runs
```

Suppress triggering for a region with `without watchers`:

```basic
without watchers
    a = 100
end without
```

> **Theory — reactivity built into storage.** A watcher is not an event callback
> you wire up and tear down; it is a standing relationship over *storage paths*.
> Triggering is **immediate and synchronous**: a mutation runs its matching
> watchers before execution continues past the mutating statement, so dependent
> values are consistent the moment you read them. Matching is symmetric at dot
> boundaries — `watch(state)` sees `state.value` change, and `watch(state.value)`
> sees `state` replaced wholesale — and array element changes register at the
> containing array path. Within one trigger cascade a pending watcher runs at
> most once (and sees the latest state), and a runaway cascade is capped with a
> structured error (`error.code = 1005`, `error.source = "watcher"`). This same
> model powers WebServer: you `watch` a request queue rather than registering
> handlers.

## Actors and concurrency

gBASIC runs concurrent work as **actors**: isolated OS processes that share no
memory and communicate only by **copying messages**. Five primitives carry the
whole model.

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

- `spawn worker(args…)` starts a new actor and returns a **handle**. `worker`
  must be a `function` in the program, and the program must run from a file (the
  child re-execs it). The args are copied to the child; a handle among them
  (including `self()`) is passed through so the child can message back.
- `send(handle, value)` copies `value` into the target's mailbox (non-blocking;
  per-sender FIFO).
- `receive()` blocks for the next message.
- `receive(tag)` is **selective**: it returns the next message whose tag matches
  (the string itself, or an array's first element), leaving others queued.
- `receive(<duration>)` is a **timeout**: returns the message, or `nothing` if
  none arrives in time (e.g. `receive(5 seconds)`).
- `self()` is this actor's own handle.

```basic
send(me, ["data", 42])
msg = receive("data")          # selective by tag
print(msg[1])                  # 42
t = receive(1 seconds)         # timeout
print(is_nothing(t))           # true
```

You can also `monitor(handle)` to be told when another actor dies — the death
arrives as an ordinary tagged message `["down", handle, reason]` — and
`demonitor(handle)` to stop.

> **Theory — watcher boundaries are concurrency boundaries.** Actors share
> nothing: every message is a deep, typed snapshot (built on `serialize`), so a
> received value is fully independent of the sender's. Mutating it cannot fire
> the sender's watchers, and a record's `link` fields arrive as plain copies —
> the reactive graph stops exactly at the process boundary. That is the whole
> safety story: no shared mutable state means no locks, no data races, no
> "spooky action." Messages can themselves carry **actor handles**, so actors can
> introduce one another and form arbitrary topologies (supervisors, pipelines,
> rings). The trade-off is that live, unsendable things — database connections,
> GUI widgets — cannot cross a boundary; you keep those inside one actor. See
> [multiprocessing_design.md](multiprocessing_design.md).

## Errors

The default is `on error stop`: a runtime error stops execution with a nonzero
exit status. Switch to inspect-and-continue with `on error resume next`:

```basic
on error resume next
print(missing_value)
if error then
    print(error.message)
    print(error.line)
    print(error.column)
    print(error.code)
    print(error.source)
    error.clear()
end if
on error stop
```

`on error goto label` installs a single-use handler; `error "message"` raises
one explicitly:

```basic
on error goto failed
error "explicit failure"
print("not reached")

failed:
print(error.message)
error.clear()
```

Module errors carry a `source` (`"watcher"`, `"sqlite"`, `"postgres"`, `"actor"`,
…) and a numeric `code` so you can branch on the origin.

## Money, dates, times, and durations

Money, dates, times, and durations are **first-class value types**, not just
formatted numbers and strings.

`USD` creates money, stored internally as integer cents:

```basic
price(USD)= 19.95
tax = price * 0.08          # money × number
total = price + tax         # money + money
if total > price then print("greater")
```

Dates and times come from the `(date)=` / `(time)=` modifiers; precision is
inferred from the string (a date-only string is day-precise, a full timestamp is
second-precise):

```basic
d(date)= "2026-05-15"
t(date)= "2026-05-15 12:05:03"
```

Durations are first-class and compose with date arithmetic:

```basic
start(date)= "2026-05-15 13:10:00"
print(start + 1 hour 20 minutes)    # 2026-05-15 14:30:00
print(start + 2 days)
```

Units: `years months weeks days hours minutes seconds` (singular spellings work
too). Pull or truncate parts with **lenses** — `year month day hour minute
second` — as assignment modifiers or comparison lenses:

```basic
y(year)= t                 # 2026
if d {day}= t then print("same day")     # compare at day precision
```

Bare date/time `=`/`!=` is *exact* (values match only if their fields **and**
precision match); use a `{…}` lens when you want precision-tolerant comparison.

The `now()` builtin returns the current local time as a second-precision
`datetime` — the primitive for deadlines and expiry:

```basic
deadline = now() + 8 hours
expired = now() > deadline           # false until the deadline passes
today(day)= now()                    # truncate to a date via the day lens
```

> **Theory — domain types, not stringly-typed data.** Money as cents, dates with
> explicit precision, and durations as values mean the language enforces what
> would otherwise be convention: you can't accidentally add two timestamps as
> floats, lose cents to floating point, or compare `"2026-05-15"` and
> `"2026-05-15 00:00:00"` as if they were the same fact. The modifier/lens system
> is what makes these ergonomic — the type is chosen at the assignment, and parts
> are read through lenses rather than string surgery. (There is intentionally no
> `today()` builtin — derive a date from `now()` via the `(day)=` lens. To build a
> timestamp from a string, `(datetime)=` always yields a full second-precision
> value, while `(date)=` infers precision from the string.)

## Files and locks

A file reference is a typed path, created with the `(file)=` modifier:

```basic
f(file)= "examples/tmp_file_test.txt"

write(f, "line one\nline two\n")
print(exists(f))            # true
print(bytes(f))             # byte length
print(lines(f))             # line count
print(read(f))              # whole file as a string
append(f, "line three\n")

for each line in read_lines(f)
    print(line)
end for
```

`list_files(dir)` returns the files in a directory; `make_dir`, `join_path`,
`file_name`, `directory_name`, and `extension` handle paths. Advisory locking
uses a block:

```basic
with lock(f)
    write(f, "locked write\n")
    append(f, "still locked\n")
end with
```

The lock releases when the block ends.

## Serialization

gBASIC has two ways to turn values into text/bytes and back, for different jobs:

- **`encode(value)` / `decode(text)`** — JSON. Human-readable and portable, but
  lossy for gBASIC's typed values (a date or money round-trips as a string or
  number). Use it for APIs and config.

  ```basic
  project = { title = "The Lantern Room", items = ["lamp", "key"] }
  text = encode(project)
  copy = decode(text)
  print(copy.title)
  ```

- **`serialize(value)` / `deserialize(bytes)`** — an exact, binary-safe
  round-trip that preserves type and content (including interior NUL bytes)
  across numbers, strings, booleans, `nothing`, `unknown`, arrays, nested
  records, dates/times, durations, money, and file references:

  ```basic
  cost(USD)= 9.99
  exact = deserialize(serialize({ when: now(), cost: cost }))   # money + datetime, types intact
  ```

`serialize` is the foundation actor messages are built on. (PBI policies are not
preserved — a deserialized value is plain `copy` — which is the same degradation
a value undergoes when sent to another actor.) Two more helpers: `string(value)`
gives canonical display text, and `quote(value)` produces a gBASIC source literal
with escaping (handy for code generation).

## Core builtin functions

These are always available — no `load` required — and stay strictly typed with
clear error messages. A working selection (see [`reference.md`](reference.md) for
the full catalog):

- **Types:** `type`, `is_string`, `is_number`, `is_boolean`, `is_array`,
  `is_record`, `is_nothing`, `is_unknown`
- **Convert:** `string`, `number`, `boolean`, `array`, `record`
- **Strings:** `replace`, `starts_with`, `ends_with`, `repeat`, `lower`, `upper`,
  `trim`, `split`, `join`, `left`, `right`, `mid`, `find`, `reverse`; Unicode/byte
  ops `chr`, `code`, `byte_count`, `byte_at`, `from_bytes`
- **Records:** `keys`, `values`, `has`, `remove_key`
- **Arrays:** `append`, `prepend`, `insert`, `remove`, `contains`,
  `remove_value`, `find_by`, `first`, `rest`, `take_first`, `take_last`,
  `reverse`, `unique`, `sort`; aggregates `count`, `len`, `sum`, `mean`,
  `median`, `mode`, `min`, `max`
- **Misc:** `count`, `env`, `now`, `round`, `input`, `compare`

```basic
person = {name: "Alice", age: 30}
if has(person, "email") then print(person.email)
print(count([1, 2, 3]))       # 3
print(replace("hello", "l", "x"))   # "hexxo"
```

## Libraries and load

A library collects functions and exported modifiers. Functions in a loaded
library are imported by default; modifiers must be marked `export modifier`.

```basic
library text
    function add(a, b)
        return a + b
    end function

    export modifier shout for assign
        return upper(value)
    end modifier
end library

program demo(args)
    load text
    print(add(2, 3))
    msg(shout)= "hello"
    print(msg)              # "HELLO"
    print(text.add(2, 3))  # qualified call bypasses override lookup
end program
```

Load from another file with `load NAME from "path"`:

```basic
load tools from "libs/tools.bas"
```

Unqualified `load NAME` searches same-file libraries, then `NAME.bas` in the
current directory and subdirectories, then `GBASIC_PATH` (colon-separated), and
finally any `.bas` containing `library NAME`. (`use` is a deprecated alias for
`load`.)

## Databases: SQLite and PostgreSQL

Both modules are optional at build time and load on demand. Parameters are bound
separately (no string interpolation into SQL), results are arrays of records, and
SQL `NULL` maps to `nothing`.

**SQLite:**

```basic
load sqlite

db = sqlite.connect("app.db")
sqlite.exec(db, "create table if not exists users (id integer primary key, name text, active integer)")
sqlite.exec(db, "insert into users (name, active) values (?, ?)", ["Ada", true])
rows = sqlite.query(db, "select id, name from users where active = ?", [true])
for each row in rows
    print(row.name)
end for
sqlite.close(db)
```

Also: `sqlite.begin/commit/rollback`, `sqlite.last_insert_rowid`. Placeholders are
positional `?`. Errors carry `error.source = "sqlite"`.

**PostgreSQL** uses a connection record and `$1`-style placeholders:

```basic
load pg

db = pg.connect({ host:"localhost", database:"app", user:"app", password:"secret" })
rows = pg.query(db, "select id, name from users where active = $1", [true])
result = pg.exec(db, "update users set active = false where id = $1", [10])
pg.close(db)
```

Also: `pg.begin/commit/rollback`. `bigint`/`numeric` come back as strings to
avoid precision loss; JSON results decode to gBASIC values. Errors carry
`error.source = "postgres"` (with SQLSTATE when available).

## Calling web APIs (WebClient)

With libcurl available, `load webclient` for synchronous outgoing HTTP/HTTPS.

```basic
load webclient

response = webclient.get("https://example.com")
print(response.status)      # number
print(response.reason)      # string
print(response.body)        # string
```

The response record always has numeric `status`, string `reason`, record
`headers`, and string `body`. HTTP 404/500 are *responses*, not errors.
`webclient.post(url, body)` takes a string body — `encode(...)` records for JSON.
For full control use `webclient.request({method, url, headers, body, timeout})`.
When a body is valid JSON, WebClient adds a decoded `json` field:

```basic
response = webclient.get("https://api.example.com/users/1")
if not is_unknown(response["json"]) then
    print(response.json.name)
else
    print(response.body)
end if
```

Network, DNS, TLS, and timeout failures are runtime errors.

## Serving HTTP requests (WebServer)

WebServer follows the **watcher** model rather than callbacks: start a server,
watch its request queue, and append replies to its response queue.

```basic
load webserver

server = webserver.listen(8080)        # port 0 lets the OS choose

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        append(server.responses, {
            id: req.id,
            body: "Hello World"
        })
    end while
end watch
```

Request records expose `method`, `path`, `query` (a record of decoded strings),
`headers` (lowercase names), `body`, `remote_ip`, `remote_port`, `timestamp`, and
a decoded `json` field when the body is JSON. In a response, only `id` is
required; `status` defaults to 200, `headers` to `{}`, `body` to `""`. Build JSON
replies with `encode(...)` and a content-type header. Stop with
`webserver.close(server)` (or set `server.running = false`). v0.1 binds to
`127.0.0.1` and handles one HTTP/1.1 request per connection.

> **Theory — the same reactivity, end to end.** Notice WebServer reuses watchers
> rather than introducing a new callback mechanism. A request queue is just
> watched storage; `take_first`/`append` notify watchers once after the mutation
> completes. One reactive model serves local state and network I/O alike.

## Security helpers: passwords and tokens

Two builtins cover the common application-security primitives:

```basic
hash = password_hash("correct horse battery staple")   # store this
if password_verify("candidate", hash) then
    print("ok")
end if

session_id = secure_token(43)   # cryptographically random, URL-safe
csrf_token = secure_token(43)
```

`password_hash` uses the platform's preferred libxcrypt method and embeds the
algorithm, parameters, and salt in the returned string, so it stores directly in
a user table. `secure_token(length)` returns a random URL-safe string suitable
for session ids, CSRF tokens, and invite codes.

## Larger example and where to go next

See `examples/adventure/adventure.bas` for an Infocom-style text adventure that
ties together `input`, `print`, modifiers (`trimmed`, `lowered`, `split`),
arrays, functions, `if`/`else`, `while`, and `consider`. Its companion
`examples/adventure/NOTES.md` records design-friction notes from writing it.

Further reading:

- [`reference.md`](reference.md) — the complete language reference (every
  statement, expression, builtin, and module).
- `examples/` — runnable programs for each feature, including
  `first_class_function_test.bas`, `method_test.bas`, `constructor_test.bas`,
  the `spawn_*` actor tests, and the SQLite/PostgreSQL/WebClient/WebServer demos.
- The design docs for the deepest topics:
  [pbi_design.md](pbi_design.md),
  [multiprocessing_design.md](multiprocessing_design.md),
  [first_class_functions_design.md](first_class_functions_design.md),
  [unicode_design.md](unicode_design.md).
```

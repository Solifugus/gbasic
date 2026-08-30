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
- Newlines terminate most statements, EXCEPT inside an unclosed `(`, `[` or
  `{` -- see Line continuation below.
- Apostrophe comments start with `'` and continue to end of line.

Supported string escapes:

- `\n` newline
- `\t` tab
- `\\` backslash
- `\"` double quote
- `\u{...}` Unicode codepoint by hex value, e.g. `\u{1F600}` → `😀` (range
  `1..0x10FFFF`, excluding surrogates; for a literal NUL use `chr(0)`)

Unknown escapes and unterminated strings are lexer errors and exit nonzero.

### Line continuation

A statement continues across a line break whenever a `(`, `[` or `{` is still
open. There is no continuation character: the brackets already written decide
where the statement ends, so there is no trailing marker to forget and no way
for the marker and the brackets to disagree.

```basic
sql = join([
    "create table loans (",
    "  id integer primary key,",
    "  balance real",
    ")"
], " ")

person = {
    name: "ada",
    tags: [
        "one",
        "two"
    ]
}

function add(a,
             b,
             c)
    return a + b + c
end function
```

Blank lines and end-of-line comments inside the brackets are ignored the same
way, so a long argument list can be annotated per line. Continuation is purely
lexical: the token stream a continued statement produces is identical to the
one-line form, so nothing downstream -- the grammar, the evaluator, the outline
-- sees a difference.

A bracket inside a string or a comment is text, not depth. If a bracket is
still open at end of file the error names which one and the line it opened on:

```
runtime error at report.bas:41:1: unclosed '[' opened on line 12 --
a line break inside brackets continues the statement
```

```basic
description = "You are in a stone hall.
Water drips from the ceiling.
A passage leads north."
```

### Reserved words

Keywords are matched **case-insensitively**. There are 46, and 44 of them
cannot be used as a variable, parameter or function name:

```text
and       as        break     consider  continue  dim       do        each
else      error     export    false     for       function  gosub     goto
if        in        library   load      modifier  new       not       nothing
on        or        print     program   return    spawn     step      stop
then      to        true      unknown   until     unwatch   use       watch
watchers  while     with      without
```

The remaining two — **`end` and `next`** — are recovered as identifiers by the
grammar and *may* be used as ordinary names. That is why closing a `for` with
`next` costs no reserved word (see Statements). `loop` is not in either list:
it stopped being a keyword entirely when `do … loop while` was removed, and is
now an ordinary identifier.

Words you might expect here and that are **not** reserved: `server`, `warning`,
`default`, `resume`, `from`, and the `server` block's verbs. They are resolved
by position or after the environment walk, so `default(a, b)` is an ordinary
builtin call and `board.warning` is an ordinary field.

**Every keyword is legal as a record field**, both in a literal and after a dot
— `{ error: "none" }` and `r.error` both work, and since 2026-08-27 that
includes `dim`. `dim` is still refused where a *statement* was expected, which
is what its advice is about:

```
`dim` is not a gBASIC statement; assign to create a variable (x = 0)
```

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
x{modifier}= expression
x{modifier args}= expression
```

**Compound assignment** (*since 0.1.0-rc9*) — `+=`, `-=`, `*=`, `/=`:

```basic
count += 1
total -= discount
scale *= 2
half  /= 2
```

`x op= e` means **exactly** `x = x op e`. It inherits every type rule and every
refusal the operator already has — so it works wherever the operator does:

```basic
tally  += 1              ' number
line   += "\n"           ' string
due    += 3 days         ' date + duration
owed   += payment        ' money + money
window *= 2              ' duration * number
rec.count += 1           ' through a field
arr[0]    += 1           ' through an index
```

and fails where the operator does: `list += [1]` raises exactly as
`list = list + [1]` does. With a modifier, the modifier applies to the folded
result, as in a plain modified assignment — `name{upper} += "cd"` on `"ab"`
gives `"ABCD"`.

Because it is defined as the desugaring, the target is read and then written,
the same two touches `x = x + 1` makes.

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

Print to standard error:

```basic
print to error expression
print to error ("could not open " + path)
```

`print to error` is `print` with a destination. It renders its argument through the
same code path — every value shape, every separator, the same terminating newline —
and differs only in which stream the bytes go to. `error` is the only destination
keyword; there is no file-handle or redirect form.

Use it for anything that is not the program's data: progress messages, warnings,
usage text, diagnostics. That is what makes a gBASIC program compose in a shell
pipeline, because a downstream reader then receives the data alone:

```basic
print to error "reading " + count(files) + " files..."
for each row in rows
    print row.id + "," + row.name
end for
```

```sh
gbasic report.bas > data.csv        # progress on the terminal, data in the file
gbasic report.bas 2>/dev/null | wc  # or discard it entirely
```

Buffering differs between the two streams, and it matters when both are pointed at
one destination. stderr is unbuffered, so `print to error` leaves the process
immediately. stdout is line-buffered on a terminal but **block**-buffered on a pipe
or file, so under `gbasic prog.bas > log 2>&1` the stderr lines appear first and the
stdout lines arrive in a batch when the program exits. Pass
[`--line-buffered`](#why---line-buffered-exists) to get source order:

```sh
gbasic prog.bas > log 2>&1                   # stderr first, stdout at exit
gbasic --line-buffered prog.bas > log 2>&1   # interleaved in source order
```

The flag governs stdout only — it calls `setvbuf` on that one stream. `print to
error` is prompt with or without it.

The runtime also writes to standard error: runtime errors, and the JSON lines
emitted by `--json-diagnostics`. A program's own writes do not disturb them. The
two are separated by line, which is the contract `--json-diagnostics` already had,
so a consumer that reads whole lines and parses the ones beginning with `{` keeps
working unchanged.

If:

<!--fragment: an API shape or a deliberately invalid form, not a program-->
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

<!--fragment: an API shape or a deliberately invalid form, not a program-->
```basic
while condition
    statement
end while
```

`break` exits the nearest enclosing loop. `continue` skips to the next
iteration.

Either may **name the loop it means**, which is how you leave or restart an
outer loop from inside an inner one:

```basic
for x = 1 to 4
    for y = 4 to 1 step -1
        if y = x then continue x    ' abandon the y loop, take the next x
        if y = 0 then break x       ' leave the x loop entirely
    next y
next x
```

The name is a loop **variable**, so it selects a `for` — `while` and `do`
loops have no variable and a named flow travels straight past them. Naming a
loop that does not enclose the statement is a runtime error that says so
(`break: no enclosing loop named 'zzz'`) rather than failing silently.

Counted `for`:

```basic
for i = 1 to 5
    print i             ' 1 2 3 4 5 — `to` is INCLUSIVE
end for

for i = 0 to 10 step 2  ' 0 2 4 6 8 10
end for

for i = 5 to 1 step -1  ' counts down
end for
```

A `for` loop may close with **`end for`**, with **`next`**, or with **`next
<name>`** — the three are the same statement:

<!--fragment: an API shape or a deliberately invalid form, not a program-->
```basic
for i = 1 to 3
    ...
next i          ' or `next`, or `end for`
```

`next <name>` must name the loop it closes. Classic BASIC let `next x` close
an inner `y` loop by implicitly closing both, so a one-letter typo silently
restructured the program; here the mismatch is refused at load time. Spelling
the terminator this way costs no reserved word — `next` remains usable as an
ordinary variable, as do `loop` and `until`.

- **`to` is inclusive**, as in every BASIC.
- **`step` defaults to 1** and may be negative or fractional. A negative step
  counts down; the sign decides which comparison ends the loop, so
  `for i = 5 to 1` with no step never runs its body.
- **Bounds and step are evaluated once**, at loop entry. Changing them inside
  the body does not move the finish line.
- **The counter is an ordinary variable.** It keeps its last value after the
  loop — the last value the body actually saw, not one past it.
- `step 0` raises (`for step cannot be zero`) rather than looping forever, and
  non-numeric bounds raise rather than coercing.
- `break` and `continue` work as they do in `while`, and may name this loop's
  variable to act on it from inside a nested loop (`break i`, `continue i`).

Post-test loop — the body always runs at least once:

```basic
do
    line = input()
until line != ""

do
    attempts = attempts + 1
until attempts !< 3
```

`while … end while` tests before the body, so `do … until` exists only for the
"run it once, then decide" shape; there is deliberately no pre-test
`do while … loop`. `break` and `continue` behave as elsewhere.

**`until` is a stop condition, and it is the only form.** A continue-condition
spelling (`do … loop while c`) existed until 0.1.0-rc8 and was removed, for two
reasons that are one reason. It is redundant — it means `until not c`, and
`!<` / `!>` cover the single-comparison case without a `not` (the second
example above is the old `loop while attempts < 3`). And it *required* the
`loop` keyword: `do … while c` cannot be distinguished from a body whose next
statement is a nested `while c … end while`, because both readings are complete
programs and the `end while` that separates them can be arbitrarily far ahead —
measured at 32 reduce/reduce conflicts, and dropping the `until` form does not
help, because the ambiguity is with the nested statement rather than with the
other terminator. `until` never begins anything else, so it needs no opening
keyword.

For a compound condition, negate the whole thing rather than applying De Morgan
by hand — `until not (a < 10 and b < 10)` is the old `loop while a < 10 and
b < 10`, and flipping `and` to `or` yourself is where the bugs are.

There is no `repeat … until`: `repeat` is a string builtin, and reserving it
would break existing programs. `until` begins a statement now, so unlike `end`
and `next` it is reserved; `loop` is no longer a keyword in any position and is
an ordinary name again.

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

For-each (`next` closes it too, naming the element variable):

```basic
for item in items
    print(item.name)
end for

for each item in items
    print(item.name)
next item
```

Function:

```basic
function name(a, b)
    return a + b
end function
```

Calling a function with the wrong number of arguments is a **runtime error**
(*since 0.1.0-rc3*) — located at the call, fatal like any other, and subject to
`on error` like any other. (Earlier builds printed one unlocated line to stderr
and kept running with a `nothing` result, so the failure surfaced later,
somewhere else, as `expected number but got nothing`.)

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

Named watchers are first-class values:

```basic
watch recalc(a, b)
    c = a + b
end watch

print recalc.name        ' recalc
print recalc.targets     ' ["a","b"]
unwatch recalc           ' turns it off
print count(watchers())  ' the live handles, in registration order
```

`watch name(...)` registers the watcher **and** binds `name` to a watcher
value in scope — storable, passable, comparable by identity (`=`/`!=` only;
copies of one handle are equal, two registrations never are). `unwatch`
takes any expression yielding a watcher value and turns that registration
off; unwatching an already-off handle is a quiet no-op. Re-declaring
`watch name(...)` while `name` is bound to a live watcher **replaces** it —
setup code is safe to re-run without stacking duplicate watchers.
`watchers()` returns the live handles, so a watcher is recoverable even when
no variable holds it any more. Named watchers may only be declared at top
level; watcher values cannot be encoded or sent to another actor. The
anonymous `watch(...)` form is unchanged.

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
on error stop           ' propagate (the default), restorable mid-frame
on error goto next      ' absorb: check with `if error then`
on error goto label     ' absorb: disarm and jump
error "message"
error { message: "...", detail: x }
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

<!--fragment: an API shape or a deliberately invalid form, not a program-->
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

<!--fragment: an API shape or a deliberately invalid form, not a program-->
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
d{date}= "2026-05-15"
t{date}= "2026-05-15 12:05:03"

if d != t then print("different precision")
if d {day}= t then print("same day")
```

Bare date/time ordering compares the represented start instant first, then
uses stored precision as a tie-breaker with less precise values ordered before
more precise values at the same start instant.

Logical operators:

<!--fragment: an API shape or a deliberately invalid form, not a program-->
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

<!--fragment: an API shape or a deliberately invalid form, not a program-->
```basic
scores[0]
```

Record field access:

<!--fragment: an API shape or a deliberately invalid form, not a program-->
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

<!--fragment: an API shape or a deliberately invalid form, not a program-->
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

<!--fragment: an API shape or a deliberately invalid form, not a program-->
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

Arrays currently work best with numeric aggregate built-ins. Records hold string keys and values of normal runtime types. A record-literal key may be a plain identifier, a keyword, or a **quoted string** — the quoted form admits keys an identifier cannot spell (`{ "content-type": "text/html" }`), matching what `decode` produces from JSON; read them back with `r["content-type"]`.

File and directory references are typed paths, not open handles.

Money is stored as an **exact integer** count of units — never a
floating-point value, so accumulating `0.01` a thousand times is exactly
`10.00`. A value carries its currency, and the scale is that currency's minor
unit plus four guard digits. See [money_design.md](money_design.md) for the
representation.

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
len("café")                    ' 4   codepoints
byte_count("café")             ' 5   UTF-8 bytes
mid("café", 3, 1)              ' "é" never splits a codepoint
from_bytes([0, 255])           ' a two-byte binary string
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

**Scope, stated plainly:** a function body reads enclosing variables but cannot
rebind them — an assignment to a name that is not already local creates a **new
function-local**, and the outer variable keeps its value. To share mutable
state with a function or a handler, keep it in a record and mutate its fields
(`state.count = state.count + 1`), which persists because records are reached
by reference. Because the broken form *looks* right and fails silently, the
interpreter warns when a function **reads a name from an enclosing scope and
then assigns it** — the read-compute-store-back shape is almost always an
attempt to write the outer variable:

```
warning: 'total' was read from an enclosing scope, but this assignment creates
a new function-local of that name at prog.bas:5:3; the outer variable is
unchanged (functions cannot rebind outer scalars -- mutate a field of a shared
record instead)
```

The warning appears once per name, changes nothing about execution, and stays
silent for an ordinary local that merely shares an outer name without reading
it first. (Older interpreters simply do not warn; no program can or should
depend on it.)

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

**A qualified name works the same way**: `lib.fn` is a function value, so a
library function can be passed as a callback directly.

```basic
load heartbeat
beat = heartbeat.tick        ' a function value
monitor(heartbeat.tick)      ' passed straight in
```

A variable of the library's name still shadows it — this is a fallback
consulted only when the receiver is not a variable, exactly as `warning` and
`error` are. A loaded library with no such function says so by name
(`library 'heartbeat' has no function 'nosuch'`) rather than blaming the
receiver.

Calling a function value straight out of a **subscript** does not parse —
`table[0](7)` — so bind it first. A field call (`reg.beat(3)`) is fine.

**Do not name such a variable after a builtin.** Precedence is builtin → user
function → function-valued variable, so `first = my_fn` followed by `first(xs)`
runs the **builtin** `first` and returns an element of `xs` — a plausible value
from the wrong function. That warns now, at the call site, once per site.
Holding a builtin-named variable is fine (`list = [1, 2]`); only *calling* it
is the mistake, which is why the warning fires on the call and not on the
assignment. A record field is immune — `r.first(2)` is not a bare name.

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

The **receiver of a method call may be any expression**, not only a single
variable — a field access, an index, or another call:

<!--fragment: an API shape or a deliberately invalid form, not a program-->
```basic
outer.inner.method()        ' field-access receiver
widgets[0].present()        ' index receiver
make_widget().show()        ' call-result receiver (in expression position)
this.helper()               ' the current receiver
```

The receiver is evaluated **once**, then dispatched by runtime kind (a record
method binds `this`; a GObject/boxed value routes through `gi`). An lvalue receiver
(a variable/field/index chain) binds `this` to the live cell, so a record method's
`this.x = …` writes through exactly as for `obj.method()`; a call-result receiver
binds `this` to the temporary. Chained method calls never form an lvalue, so
`a.b().c = x` remains invalid. (One narrow limitation: a method call on a
call-result receiver as a *bare statement* — `make_widget().show()` with the result
discarded — does not parse; use it in expression position, or bind the result to a
variable first.)

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
x{USD}= 19.95
name {caseless}= "joe"
a {rounded 2}= b
a {math.rounded to 2}= b
```

Assignment modifiers transform assigned values. Comparison modifiers transform or
implement comparisons. Both are written in **braces**; the parenthesized spelling
was removed in 0.1.0-rc6 and is now a parse error (see the CHANGELOG for the
migration).

Modifiers apply only in assignment and comparison contexts. An assignment
modifier's target may be a variable, a record field, or an array element:

```basic
r.when {date}= "2026-01-01"
a[0] {USD}= 5
```

A comparison lens applies to any operand, including a call result:

<!--fragment: an API shape or a deliberately invalid form, not a program-->
```basic
if getname(){caseless}= "joe" then      ' works
```

Comparison modifiers use brace lens syntax:

<!--fragment: an API shape or a deliberately invalid form, not a program-->
```basic
left {modifier}= right
left {modifier args}< right
left {modifier args}>= right
```

### Constructing typed values

`{file}` and `{dir}` are **idempotent** (*since 0.1.0-rc9*): applying one to a
value that already has that type passes it through unchanged, rather than
raising. Asserting that a file is a file should be a no-op — and it matters
most straight out of a listing, since `list_files` yields file values, so
`f {file}= entry` used to raise with the error surfacing *at the modifier*,
reading as "the modifier is broken" rather than "the listing returned a type
you did not expect". Crossing the two is still refused: a directory is not a
file.

This is the **only** way to build a date, a time, a money amount, a file or a
directory value: there are no literals for them, and `2026-12-25` written bare
parses as arithmetic. An assignment modifier takes a string (or, for `USD`,
either a number or decimal text) and produces the typed value:

```basic
d {date}     = "2026-05-15"            ' a date
t {datetime} = "2026-05-15 12:05:03"   ' a timestamp
h {time}     = "12:05:03"              ' a time of day
m {USD}      = 19.95                   ' money, exact
f {file}     = "notes.txt"             ' a file reference
p {dir}      = "/tmp"                  ' a directory reference
```

The precision you supply is the precision the value keeps, and it decides how the
value renders and how bare comparisons behave — `{date}= "2026"` is a *year*, not
January 1st. Each of these **raises** when the string cannot be read as the type
asked for, rather than yielding `nothing`: validate first if a bad value is
expected rather than exceptional.

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

**Functions resolve the same way, and a qualified call always reaches the
library** (*fixed in 0.1.0-rc9*). A local function of the same name keeps
*unqualified* calls and warns that it does so, while `lib.name(...)` reaches
the library's. Until rc9 the import skipped registering a function whose name
matched a local, so the qualified call — the very thing one reaches for when a
name collides — failed with `invalid function call`.

**A `load` outside a `program` block never runs**, because `load` is an
executable statement and the top-level statements in such a file are not
walked. This now warns, because the symptom is otherwise misdirecting: for a
native module it is `library not loaded: xml`, but for a `.bas` library it is
`invalid function call: lib.name`, which points at the call rather than at the
import that never happened. Put the `load` inside the block.

Built-in/core modifiers include:

#### Currencies (*since 0.1.0-rc9*)

**Every ISO 4217 code is an assignment modifier** — `{USD}`, `{EUR}`, `{JPY}`,
`{KWD}` — and each carries its own minor-unit exponent, so JPY has no decimal
places and KWD has three. A money value knows its currency:

```basic
u {USD}= "19.95"
e {EUR}= "19.95"

total = u + e          ' raises: cannot add money in different currencies
if u < e then          ' raises: no order exists without a rate
    print "unreachable"
end if
print u = e            ' false -- a real question, answered
```

Equality *answers* while ordering *refuses*, following the same idiom as
compound values: "is USD 19.95 equal to EUR 19.95" is a question whose answer
is no; "is it less than" is not a question at all without an exchange rate.
Applying a different currency's modifier to existing money is refused for the
same reason — re-tagging and converting are different operations, and picking
one silently would invent a rate of 1.

**Storage carries four guard digits below the minor unit**, so intermediates
below the cent survive a multi-step calculation and are rounded once, at
display: `(100.00 / 3) * 3` is `100.00`, where a cents-only representation
gives `99.99`. The cost is range — USD spans about ±$9.22 trillion, JPY about
±¥922 trillion, KWD about ±922 billion. Beyond that, arithmetic **raises**
rather than wrapping.

`string(m)` renders at the currency's minor unit, which is what you want on
screen and lossy in a file. **`money.text(amount [, places])`** is the exit
that keeps the guard digits:

```basic
rate {USD}= "0.10432"
print string(rate)              ' 0.10       <- display
print money.text(rate)          ' 0.104320   <- the whole value
print money.text(rate, 2)       ' 0.10       <- an explicit width
back {USD}= money.text(rate)    ' round-trips exactly
```

With no width it renders at the **storage scale**, so the text reads back
through `{USD}=` as the same value — which is what storing money as decimal
text in a database or a JSON document needs. An explicit width renders there
instead, rounding half-even. It raises on anything but money, and on a width
that is not a whole number from 0 to 18.

The built-in table is the **current** ISO 4217 list, so withdrawn currencies
(ITL, DEM, FRF) are not in it. Register what you need:

- `money.currencies()` → array of `{code, numeric, exponent, historical}`
- `money.register(code, exponent)` → the assigned numeric code. Codes are 2–6
  upper-case letters and exponents 0–8; registered codes sit above ISO's
  numeric range. Re-registering updates rather than duplicating, so setup code
  is safe to re-run.
- `money.retire(code)` → marks a currency **historical**: new values are
  refused, existing ones still read, compute and display. There is no removal,
  because removing a currency does not unmake the values that already exist
  and archived data is what money is for. Re-registering revives it.

A serialized money value carries its currency **and its exponent**, so it is
self-describing: actors are separate processes and a currency registered in
the parent is not registered in the child. `SER_VERSION` 1 payloads (a bare
integer of cents) still deserialize, rescaled and assumed USD.

#### Allocation (*since 0.1.0-rc9*)

`money.allocate(amount, parts)` splits money into **payable** amounts —
`parts` is either a count or an array of whole-number weights.

```basic
h {USD}= "100.00"
print money.allocate(h, 3)          ' [33.34, 33.33, 33.33]
print money.allocate(h, [1, 1, 2])  ' [25.00, 25.00, 50.00]
```

Division and allocation are different problems, and guard digits only solve
the first. `(100.00 / 3) * 3` comes back whole because a third of a dollar has
somewhere to live — but three *payments* cannot each be 33.3333: an invoice, a
payroll line or a dividend has to be a whole number of minor units. So
allocation works at the minor unit and distributes the remainder one unit at a
time, which is the only way **the parts sum back to the original exactly**.
Never three of 33.33, which loses a cent, and never three of 33.34, which
invents one — both would look perfectly reasonable.

A zero weight receives nothing, including no remainder unit. Currency is
respected: JPY splits into whole yen, KWD into thousandths.

#### Exchange rates (*since 0.1.0-rc9*)

**A rate is a dated fact.** Converting without an as-of date gives a number
nobody can reproduce — re-run last quarter's report and you silently get
today's rate. So the date is required, not defaulted.

```basic
jan {date}= "2026-01-01"
jun {date}= "2026-06-01"
money.rate("USD", "EUR", "0.92", jan)
money.rate("USD", "EUR", "0.95", jun)

u {USD}= "100.00"
print money.convert(u, "EUR", jan)      ' 92.00
print money.convert(u, "EUR", jun)      ' 95.00
```

- `money.rate(from, to, rate, as_of)` — record a rate. The rate is **decimal
  text**, not a number: FX rates routinely carry six or more significant
  figures and a double would round them. Registering the same pair and date
  again corrects it; the later registration wins.
- `money.convert(amount, to, on)` → money in the target currency, using the
  rate **effective** on that date — the latest whose as-of is on or before it,
  so a report run for March sees March's rate. Converting to the same currency
  is identity and needs no rate.
- `money.rate_on(from, to, on)` → `{rate, as_of}` — which rate *would* be
  applied, and the date it came from. Knowing that is the point of dating them.

The conversion moves between storage scales in one integer operation, so USD
(two places) to JPY (none) or KWD (three) loses nothing to a double.

**Inversion is refused.** Given a USD→EUR rate, the EUR→USD rate is not its
reciprocal — the two sides of a quote differ by a spread, and inverting would
invent money. The refusal says a rate exists in the other direction so the
author can decide.

#### `USD` conversion and rounding (*since 0.1.0-rc9*)

`USD` is **reflective**: it takes whatever it is given and does the most
accurate conversion available for that type. Both routes end in the same exact
integer parse of decimal text, and differ only in what an excess decimal means.

| Given | Converted by | Excess decimals |
|---|---|---|
| decimal text (`"19.95"`) | parsed directly to scaled units | **refused** — the author wrote a value money cannot hold |
| a number (`19.95`, or computed) | rendered to its shortest round-trip decimal, then parsed | **rounded**, half-even |
| money | returned unchanged (idempotent) | — |
| anything else | refused | — |

The split is what keeps the type both exact and usable: `price * 1.08` carries
seventeen digits as a matter of course, so refusing computed precision would
make money unusable for arithmetic, while `"1.23456789"` written by hand is a
bug in the input.

Because a number is rendered to text *before* parsing, an ordinary literal is
exact: the double for `92233720368547.75` renders back to that same decimal, so
the value survives. Before rc9 it became `...76`.

**Rounding is half-even** ("banker's rounding"): a tie goes to the even digit,
which is unbiased across many roundings where half-up drifts upward. So `0.125`
becomes `0.12` and `0.155` becomes `0.16`. Before rc9 the rule depended on the
*binary* representation of the literal rather than on the text — `0.125` gave
`0.13` while `0.145` gave `0.14` — which looked like half-even and was not.

Range is int64 at the storage scale, so it depends on the currency: about
±$9.22 trillion for USD, ±¥922 trillion for JPY. Beyond that, and on a
non-finite number, a currency modifier raises rather than wrapping or
saturating.

Both construction routes stop at the **storage scale** — six decimal places
for USD, seven for KWD, four for JPY — and differ only in what an excess digit
means. Text you wrote is **refused**, because you wrote something money cannot
hold; a number is **rounded** half-even, because `price * 1.08` carries
seventeen digits as a matter of course. So a sub-cent authored price is exact
(`{USD}= "3.459"` is a real fuel price, and `x * 10` gives `34.59`) while
`{USD}= "1.23456789"` raises, naming the limit.

- Assignment: `USD`, `date`, `time`, `datetime`, `year`, `month`, `day`, `hour`, `minute`, `second`, `file`, `dir`, `trimmed`/`trim`, `lowered`/`lower`, `uppered`/`upper` (both spellings accepted), `split`, `join`, `length`, `number`, `string`
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
    msg{shout}= "hello"
    print(msg)
end program
```

If a `program` block exists, only that program executes. If no `program` block exists, top-level statements are treated as an implicit program.

**`load` goes inside the `program` block.** `load` is an executable statement,
not a declaration, so the rule above applies to it: when a `program` block
exists, a `load` written at top level **never runs**, and the first qualified
call then fails with `library not loaded: NAME`. Note the example above puts
`load text` inside `program demo`, which is the idiom to follow — as do the
shipped `examples/xml_*_test.bas`.

```basic
load xml                  ' WRONG when a program block exists — never runs
program main()
    doc = xml.parse("<a/>")   ' runtime error: library not loaded: xml
end program

program main()
    load xml              ' RIGHT
    doc = xml.parse("<a/>")
end program
```

What *is* hoisted, and so may be written below `end program`, is exactly:
function declarations, modifier declarations, `library` declarations, and
dotted-def method bodies. That set is pinned by `tests/run_pre_registration.sh`.
Snippets elsewhere in this reference show `load` at top level because they have
no `program` block, where it is correct.

**A library's own dependencies are declared INSIDE its `library` block.** A
top-level `load` does not put the dependency in scope for the library's
functions:

```basic
load frame                ' WRONG — does not reach grid's functions
library grid
    function f()
        return frame.from_rows([])   ' invalid function call: frame.from_rows
    end function
end library

library grid
    load frame from "frame.bas"      ' RIGHT — the dependency is the library's
    function f()
        return frame.from_rows([])
    end function
end library
```

The wrong form is worth spelling out because of **how** it fails: it works
whenever the CALLER also happens to load the dependency. A library written that
way passes its own test suite — the tests load both — and breaks in the first
program that loads it alone, which is how every real user calls it. Declare what
you depend on; `stdlib/stats.bas` over `matrix.bas` is the pattern to copy.

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
    print(receive())          ' "pong from a"
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

Error handling is **frame-scoped** (*since 0.1.0-rc5*; design:
`docs/error_model_design.md`). `on error` governs the function that executed
it — or the top level — and nothing else: every function starts in the default
state whatever its caller set, and the setting dies with the frame. Nothing is
caught unless the `on error` line is visible in the same function.

**Default — propagate.** A raise in a frame with no active handler unwinds to
the nearest ancestor frame that has one. Reaching the top unhandled is fatal
with a nonzero exit. `on error stop` restores this state mid-frame.

**`on error goto next` — the checked-statement style.** A raise abandons the
statement, records the error, and execution continues at the next statement in
the same list:

```basic
cfg_text = read(path)
if error then
    print to error "config unreadable: " + error.message
    cfg_text = "{}"
end if
```

The abandoned assignment does not write its target — it stays unbound or keeps
its prior value — so assign the fallback **inside** the check block. Inside a
loop body, fall-through is to the next statement of the body, so a per-item
failure is checked per item and the loop continues.

Because the handler is frame-scoped, a function can catch a raise and return a
clean fallback — the caller never knows:

```basic
function safe_div(a, b)
    on error goto next
    q = a / b
    if error then
        return -1
    end if
    return q
end function
```

Two rules keep deferral honest, and no raise can vanish under them:

1. **One pending error at a time.** A raise arriving while an unacknowledged
   error is pending is *not* absorbed — it escapes the frame as if unhandled.
   Check promptly.
2. **Pending errors do not survive the frame.** Returning (or ending the
   program) with an unacknowledged error re-raises at the call site.

**`on error goto <label>`** jumps instead. Firing **disarms** the frame, so a
raise inside the handler propagates rather than looping; re-arm by executing
`on error goto` again. The jump *is* the acknowledgment — inside the handler
bare `error` is `false` while `error.message` still describes what happened, and
rules 1 and 2 do not apply.

**Reading `error`.** Bare `error` asks *"is there an unacknowledged error?"* and
claims it: it yields the error object (a record, truthy) once per raise and
`false` thereafter, so a second check is not a stale-state trap and
`e = error` acknowledges-and-snapshots in one move. `error.field` reads the
stored error **without** touching the flag — which is why `error.message` still
works inside the block after the condition consumed it. Fields: `message`,
`code`, `source`, `line`, `column`, `path`, `details`, `trace`.
`error.clear()` clears both the error and the flag.

**Raising.** `error "message"`, or a record for a structured raise:

```basic
error { message: "insufficient funds", balance: b, needed: amt }
```

`message` is required; `code` and `source` are honored if present; every other
field lands in `error.details`, so a library can ship error *data* instead of a
string for callers to match on. Because a snapshot carries `message`,
**re-raising is just `error e`**, and it preserves the original trace and
location — the interesting site is where it first went wrong, not the relay.

**`error.trace`** is an array of `{name, path, line, column}` records, innermost
first (the field is `name` because a keyword cannot follow a dot).

`with lock` unlocks on error, and `without watchers` restores watcher behavior
after its block.

## Warnings

A warning is advice, not a failure: the statement **completed**. The channel
exists so that advice can be *suppressed* where it is deliberate and
*escalated* where it must not be ignored — which is what makes an aggressive
diagnostic affordable at all (design: `docs/warning_model_design.md`).

```basic
on warning print       ' stderr, then continue -- the default
on warning ignore      ' suppress here: "I meant that"
on warning goto next   ' record it; check with `if warning then`
on warning stop        ' escalate: it becomes a raise at its own site
```

```basic
on warning goto next
r = pool_tick(p)              ' completes normally; the warning is recorded
if warning then
    print to error warning.message
end if
```

**Reading mirrors `error`.** Bare `warning` asks "is there an unacknowledged
warning?" and claims it — the object once, `false` after, and `w = warning`
snapshots. `warning.field` reads without claiming (`message`, `code`, `source`,
`line`, `column`, `path`, `details`).

**`warning` is not a reserved word.** It is a *soft name*: resolved only when no
variable of that name is in scope, so `warning = 1`, `r.warning` and
`{ warning: … }` all keep working — and a local of that name shadows the
diagnostic, the same rule bare function names follow. One consequence worth
knowing: a **typo'd** variable called `warning` reads `false` rather than
raising `undefined variable`.

**Rules 1 and 2 do not apply.** A warning may be replaced by a later one, and an
unacknowledged warning dies quietly with its frame. The anti-silence rules are
for failures; advice that must be acknowledged is not advice.

**Mode lookup is dynamic** — outward to the nearest frame with an explicit
setting, deliberately unlike error mode, which is frame-local. *A failure is the
callee's business; the noise budget is the caller's.* So `on warning ignore` in
your function silences a warning raised inside anything it calls, and
`on warning stop` in `main` makes the whole program's warnings fatal — the
`-Werror` of a language with no build step to put a flag on.

**Escalation** raises at the warning's own site, so `error.line` and
`error.trace` point at the offending statement; `error.severity` is `"warning"`
so a caught escalation can say what it started as. There is **no
`on warning goto <label>`**: a warning fires from a statement that succeeded, so
there would be nothing to jump away from.

**Raising one** is a builtin call, `warning("message")` or
`warning({ message: "…", extra: x })` — message required, extras become
`details`. (A call rather than a statement form because `IDENT expression` costs
four grammar conflicts, and this design adds no reserved word.)

### `unused-result`

The first diagnostic the channel carries. A bare call statement that discards a
non-`nothing` return from a **gBASIC-defined** function warns, because a
function cannot change its caller — so every update API returns the new value,
and calling one for effect is a bug that always compiles:

```basic
pool_tick(p)        ' warns: the updated pool is discarded
p = pool_tick(p).pool
```

Builtins and natives are exempt (`append` mutates in place by design), and
`return nothing` — the void convention — is exempt by value.

> **Removed in 0.1.0-rc5:** `on error resume next`. It was a process-global
> mode, and that is what made a function unable to catch a raise and return a
> fallback. Migrate to `on error goto next`, which is where the checks you
> already wrote keep working — under semantics that no longer poison the caller.
> `resume` is an ordinary identifier again.

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

## ODBC Module

ODBC support is available when gBASIC is built with unixODBC (or iODBC). A
task-oriented tour with runnable recipes is in
[odbc_cookbook.md](odbc_cookbook.md). It
reaches any database with an ODBC driver installed — SQL Server, MySQL,
MariaDB, Oracle, DB2, Snowflake, Access, and SQLite among others — through one
API. Load the compiled standard module before using its qualified API:

```basic
load odbc

db = odbc.connect("DSN=warehouse;UID=app;PWD=secret")

rows = odbc.query(db, "select id, name from users where active = ?", [true])
result = odbc.exec(db, "update users set active = ? where id = ?", [false, 10])

odbc.close(db)
```

**Verified against SQL Server 2025, MariaDB 11.8 and SQLite** (2026-08-29) —
the same fixtures, one connection string apart. See *Driver notes* below for
what each backend needs.

The connection string is passed to the driver manager unchanged. It may name a
DSN (`DSN=warehouse`) or a driver and its own options
(`Driver=ODBC Driver 18 for SQL Server;Server=host;Database=db;UID=..;PWD=..`).
The module deliberately knows nothing about DSN profiles or credential
storage: where connection details come from is an application's policy, not
the language's.

Connections are opaque `odbc_connection` values. They close explicitly with
`odbc.close` and automatically during interpreter cleanup.

The module provides:

- `odbc.connect(connection_string)`
- `odbc.close(connection)`
- `odbc.query(connection, sql[, params])` → array of records
- `odbc.exec(connection, sql[, params])` → `{command, rows_affected}`
- `odbc.begin(connection)`
- `odbc.commit(connection)`
- `odbc.rollback(connection)`
- `odbc.drivers()` → array of `{name, attributes}` — the drivers the driver
  manager can load
- `odbc.sources()` → array of `{name, description}` — the configured DSNs

Parameters are arrays and are bound through `SQLBindParameter`; use the ODBC
positional placeholder `?`. SQL `NULL` maps to `nothing`. Query results are
arrays of records; duplicate column names are errors, because a record would
silently keep only the last of them. `odbc.exec` refuses a statement that
returns rows — use `odbc.query`. ODBC errors use `error.source = "odbc"` and
carry the driver's own SQLSTATE and message.

`odbc.drivers()` and `odbc.sources()` exist so a program can tell "no driver
of that name is installed" apart from "the server refused you" — two failures
that otherwise both surface as a connection that did not open.

ODBC type mapping:

| SQL type | gBASIC value | Notes |
| --- | --- | --- |
| `NULL` (any column) | `nothing` | Database null is absence |
| `BIT` | boolean | Transferred as `SQL_C_BIT`, not as the character `'1'` — a real `BIT` column rejects the latter, and MariaDB returns the byte `0x01`, which is not `'1'` |
| `TINYINT`, `SMALLINT`, `INTEGER` | number | |
| `REAL`, `FLOAT`, `DOUBLE` | number | |
| `BIGINT`, `DECIMAL`, `NUMERIC` | **string** | See below |
| `CHAR`, `VARCHAR`, `LONGVARCHAR` (and the `W` forms) | string | Parameters are declared `SQL_WVARCHAR` so Unicode survives; over 4000 bytes they become `SQL_WLONGVARCHAR`, since `nvarchar`'s non-max limit is 4000 characters |
| `DATE` | datetime, day precision | |
| `TIME` | datetime, time only | |
| `TIMESTAMP` | datetime, second precision | Fractional seconds are dropped |
| `GUID` and anything else | string | The driver's own text |
| `BINARY`, `VARBINARY`, `LONGVARBINARY` | runtime error | Binary results are future work |

**Why `BIGINT`, `DECIMAL` and `NUMERIC` are strings.** A gBASIC number is a
double. `DECIMAL(19,4)` narrowed to a double loses the cents it exists to
protect, and any integer past 2^53 comes back off by one — both silently, both
producing a value that looks perfectly ordinary. Answering with the driver's
exact text refuses to make that trade on the program's behalf; call `number()`
on it where the loss is acceptable. `pg` already answers `bigint` and
`numeric` the same way.

Parameters accept `nothing`, booleans, numbers, strings, money and date/time
values. Money binds as exact decimal text (integer cents rendered as
`1234.56`), so the value that reaches the column is the value the program
held — routing it through a double would reintroduce exactly the loss the read
side avoids. Booleans bind as `1` or `0`. Date/time values bind as ISO-like
text, which every driver converts to its own column type.

Text parameters carry their real byte length, so a gBASIC string holding an
interior NUL is sent whole. Whether it comes *back* whole is the driver's
business: the ANSI ODBC interface is NUL-terminated, and a driver that
computes result lengths with `strlen` (the SQLite3 one does) returns the
prefix. Binary columns, the right home for such data, are not yet supported.

#### Driver notes

**SQL Server via FreeTDS needs `ClientCharset=UTF-8`.** gBASIC strings are
UTF-8; a driver that has not been told so converts parameters through a
single-byte charset. The failure is **silent, not an error** — `日本語`
reaches the server as UTF-8 bytes stored one per character, so `LEN()` on the
server reports 13 where the text is 5. It round-trips correctly through gBASIC,
because the reader reverses the same mangling, and is mojibake to every other
client.

Because nothing raises, there is no error to hang a hint on, so `odbc.connect`
**warns at connect time**: it asks the driver its own name and, if it is one
that needs the option and the connection string does not supply it, says so.
The warning is suppressible — a program storing only ASCII is unaffected.

```
Driver=FreeTDS;Server=host;Port=1433;UID=..;PWD=..;Database=..;TDS_Version=7.4;ClientCharset=UTF-8
Driver=MariaDB Unicode;Server=127.0.0.1;Port=3306;UID=..;PWD=..;Database=..
Driver=SQLite3;Database=/path/to/file.db
```

**Portability facts worth knowing before you write portable SQL:**

- SQL Server's `timestamp` is a **rowversion** — an auto-generated binary value
  you cannot insert into — not a date and time. `datetime` is the portable
  spelling.
- Bad SQL is rejected at **prepare** by SQLite and MariaDB, and at **execute**
  by SQL Server, so the message differs (`odbc prepare failed` versus
  `odbc query failed`). Both carry the driver's own diagnostic.
- SQLSTATE for an unknown column is `42S22` on MariaDB and SQL Server but
  `HY000` on the SQLite driver, so branch on `error.source` and the text
  rather than on a state code.
- SQLite is **dynamically typed** and will accept a 5000-character value into
  `varchar(200)`, or a boolean written as text. A real engine will not.

Transactions are `begin` / `commit` / `rollback`, spelled the same as
`sqlite`'s and `pg`'s so the three are interchangeable. Underneath, `begin`
turns the connection's autocommit off and `commit`/`rollback` turn it back on.

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
Call `json_encode(value)` explicitly:

<!--fragment: an API shape or a deliberately invalid form, not a program-->
```basic
response = webclient.post(
    "https://api.example.com/events",
    json_encode({name:"launch", active:true})
)
```

Use `json_encode`, **not** `encode`, for anything leaving gBASIC: `encode` emits a
gBASIC dialect (`nothing`/`unknown` instead of `null`) that other JSON parsers
reject. See "Three serializers, three jobs".

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

**Redirects are followed by default; `follow: false` declines** (*since
0.1.0-rc9*). A redirect is not always plumbing — declining returns the 3xx
itself, including its `location` and its `set-cookie`, which following discards
along with the rest of the intermediate response:

<!--needs-context-->
```basic
r = webclient.request({ method: "GET", url: start, follow: false })
print r.status                      ' 302
print r.headers["set-cookie"]       ' the session cookie, kept
```

Without it a program cannot be a client for OAuth, POST-redirect-GET or any API
that answers 302 and expects the caller to look, and cannot hold a session,
since the cookie establishing one usually arrives on the response being
redirected away from. libcurl's own default is *not* to follow; gBASIC's is,
and stays so, because changing it silently would break every existing caller.

libcurl is optional at build time. A build without it remains usable, but
`load webclient` reports that the module is unavailable. WebClient is for
outgoing requests only. Incoming requests use the separate WebServer module.

## Mail: composing and sending

Sending email is two pieces, and the split is deliberate: `mail` builds the
message and is ordinary gBASIC, `smtp` speaks to the relay and is native. See
[mail_design.md](mail_design.md) for why each refusal lives where it does.

```basic
program main( args )
    load mail
    load smtp

    m = mail.compose({
        from: "Alerts <alerts@example.com>",
        to: ["ops@example.com"],
        subject: "Nightly run failed",
        body: "3 of 41 tapes were rejected."
    })

    r = smtp.send({ host: "smtp.example.com", port: 587, security: "starttls",
                    username: "alerts", password: secret }, m)
    print "sent " + r.message_id
end program
```

### `mail.compose(message)`

Fields, all optional but `from` and at least one recipient:

| field | |
|---|---|
| `from` | `"a@x.com"` or `"Name <a@x.com>"` |
| `to`, `cc`, `bcc` | one address or an array of them |
| `reply_to` | one address |
| `subject`, `body`, `html` | text; `html` makes the message `multipart/alternative` |
| `attachments` | array of `{ name:, content:, type: }` |
| `headers` | a record of extra headers |
| `date` | a datetime; defaults to `now()` |

Returns `{ from, recipients, message_id, text }` — `recipients` is the
**envelope**, so it includes `bcc`, and `text` is the message, where `bcc`
appears nowhere.

Encoding is chosen, not configured. A non-ASCII subject or display name
becomes an RFC 2047 encoded-word; a body that is pure ASCII with no very long
line is sent `7bit` and stays readable on the wire, anything else is `base64`
with `charset="utf-8"`; attachments are always base64.

`compose` **raises** rather than escaping when a value cannot go where it was
asked to go. A CR or LF in a subject, an address, an attachment name or a
header value would let whoever supplied that text write headers of their own,
and a header value cannot contain a newline, so there is nothing for an escape
to mean. It also refuses an address with no `@` or with a space in it, a
header name that is not a token, a header `compose` writes itself (which would
put the field in the message twice), and an unknown field.

### The pieces, separately

`compose` is built from parts that are useful on their own and are public for
that reason:

| | |
|---|---|
| `mail.is_ascii(text)` | true when every codepoint is below 0x80. O(1) — it compares `byte_count` with `len` rather than scanning. |
| `mail.encode_word(text)` | the text unchanged if it is ASCII, an RFC 2047 `=?utf-8?B?…?=` encoded-word if not. |
| `mail.wrap_base64(text)` | base64, wrapped at 76 columns. |
| `mail.address_parts(field, address)` | `{ name, address }` from either spelling; raises, naming `field`, if it is not an address. |
| `mail.format_address(field, address)` | the header form, with the display name encoded if it needs it. |
| `mail.format_date(when)` | a datetime as an RFC 5322 `Date`, using the system's own offset. Byte-identical to python's `email.utils.format_datetime` across all seven weekdays. |

### `smtp.send(config, message)`

`config` takes `host` (required), `port` (default 587, or 465 for `tls`),
`security` (`"starttls"` — the default — `"tls"` or `"plain"`), `username`,
`password`, `timeout` (seconds, default 30) and `verify` (default `true`).
`message` is what `mail.compose` returned. Returns
`{ recipients, code, message_id }`.

Available when gBASIC is built with libcurl. Raises on a transport or relay
failure, carrying the server's own reply — so a rejected recipient names *which
one* and why. Any rejected recipient fails the whole send: partial delivery
reported as success is the failure mode that costs the most to discover later.

Two defaults are security-relevant. Certificate verification is **on**;
`verify: false` exists for a self-signed relay and has to be asked for. And
`starttls` against a relay that does not offer STARTTLS **fails** rather than
continuing in the clear.

Line endings are normalized to CRLF on the way out, so a body written with
plain `\n` is compliant without the author knowing the rule.

## WebServer Module

WebServer provides an HTTP/1.1 server using live records, ordinary arrays, and
watchers. It listens on loopback unless told otherwise:

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
- `webserver.listen(port, options)`
- `webserver.inherited()` (*since 0.1.0-rc4*)
- `webserver.close(server)`
- `webserver.redirect(request, location)`
- `webserver.redirect(request, location, status)`

`port` must be an integer from 0 through 65535. Port `0` requests an
operating-system-assigned ephemeral port, which is exposed through
`server.port`.

`options` is a record accepting `address` (the local address to bind), `hold`,
`timeout`, and `tls` (*all since 0.1.0-rc4*):

```basic
' loopback -- the default, reachable only from this machine
server = webserver.listen(8080)

' every IPv4 interface: reachable from the network
server = webserver.listen(8080, { address: "0.0.0.0" })

' one specific interface, or IPv6
server = webserver.listen(8080, { address: "192.168.1.20" })
server = webserver.listen(8080, { address: "::" })
```

```basic
' bind and listen but NEVER accept: the supervisor's grip on a port whose
' serving it delegates to workers (see the worker pool below)
super = webserver.listen(8080, { hold: true })

' the request/idle budget in seconds (default 30): an unanswered request is
' 504'd at the deadline, and a connection that never finishes SENDING a
' request is shed with 408 on the same budget -- a byte-a-minute client
' cannot hold a worker
server = webserver.listen(8080, { timeout: 5 })

' TLS termination: a default certificate, and per-hostname pairs selected by
' SNI. A bad path or a key that does not match its certificate is refused AT
' LISTEN TIME, naming the file -- not at the first handshake, hours later.
server = webserver.listen(8443, { tls: {
    cert: "site.crt", key: "site.key",
    certs: [ { host: "api.example.com", cert: "api.crt", key: "api.key" } ] } })
```

Requests carry `scheme` (`"https"` on a TLS listener, else `"http"`).
Certificates are loaded when the listener is made and never re-read:
**rotation is a rolling reload of the worker pool** — workers load their certs
at start (`webserver.inherited({ tls: {...} })` over the supervisor's plain
TCP socket), so replacing workers under the never-retire-on-faith rule
replaces certificates with zero downtime, and there is no second reload
mechanism to keep correct. TLS requires libssl at build time; without it the
`tls` option raises the usual "not available in this build".

The parser refuses what it cannot speak rather than guessing: a
`Transfer-Encoding` header is answered `501` (this server does not implement
chunked framing, and reading a chunked body as if it were length-framed
leaves attacker-written bytes in the buffer to be parsed as the *next*
request), and two `Content-Length` headers that disagree are answered `400`
(the classic desync lever; agreeing duplicates still parse).

Omitting `address` binds `127.0.0.1`, so a server is private until its author
says otherwise. `address` must be a **numeric** IPv4 or IPv6 address: a
hostname is refused rather than resolved, which keeps binding free of a name
lookup that could block, vary between runs, or answer with several addresses
and no rule for choosing among them. An unknown option field is refused by
name, because a misspelling that was ignored would leave a server the author
asked to publish sitting on loopback.

A listener bound to `::` accepts IPv4 peers where the platform allows it. Those
peers are reported in `request.remote_ip` as ordinary dotted quads
(`127.0.0.1`), not in the `::ffff:127.0.0.1` mapped form, so comparisons
against an address literal behave the same on either kind of listener.

The returned live server record contains:

- `port`: actual bound port
- `address`: actual bound address, as reported by the socket
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

### Draining, inherited listeners, and the worker pool (*since 0.1.0-rc4*)

**Soft stop.** Setting `server.draining = true` stops the listener accepting,
lets every in-flight request finish, closes idle connections, and then stops
the server — after which a program with nothing else to do exits. Setting
`server.running = false` remains the hard stop: whatever is still in flight is
answered `503`.

**SIGTERM means drain.** Once a process has a live server, `SIGTERM` triggers
the same soft stop on every server it holds, and the process exits by itself
when the last response is delivered. This is exactly what
`process.stop(handle)` sends, so a supervisor drains a worker with the polite
form — and escalates with `process.stop(handle, {force_after: seconds})`
**only if the grace period expires**, because the `force_after` form blocks
until death-or-deadline and then SIGKILLs. The polite form returns at once;
the point of a drain is that work continues after the signal.

**Inherited listeners.** `webserver.inherited()` adopts listening sockets this
process inherited under the `LISTEN_FDS` protocol (fds from 3 up, count in
`LISTEN_FDS`, `LISTEN_PID` naming this process) and returns them as an array
of ordinary server records. Nothing inherited is an empty array, never an
error — a worker falls back to binding for itself, which is the
local-development path. The same protocol serves systemd socket activation
and a gBASIC supervisor with one piece of code. The variables are consumed on
adoption, so this process's own children cannot mistake them for their own.

**Sharing a listener.** `process.start({command, args, listen_fds: [server]})`
hands the child the named servers' sockets at fds 3.., speaking that same
protocol. The elements must be live server records — listeners are marked
close-on-exec at creation, so the deliberate option is the *only* way one
reaches a child. A supervisor binds once with `hold: true` (keeping the
privilege of the port and none of the serving) and every worker accepts from
the shared socket; the kernel distributes connections.

**Behind a reverse proxy**, `web.trust_proxy(req, ["127.0.0.1"])` rewrites
`remote_ip`/`scheme` from `X-Forwarded-For`/`-Proto` — only when the direct
peer is one of the named proxies, and taking the **rightmost** forwarded
address that is not itself trusted. The leftmost is whatever the client chose
to write; each hop appends the peer it saw, so walking from the right stops
at the first address vouched for by infrastructure you own. The original
peer moves to `proxy_ip` and `forwarded: true` is set — evidence aside,
never destroyed.

**The pool, drain, and rolling reload** live in the `web` library:
`web.pool(spec)` / `web.pool_start` / `web.pool_tick` (respawn dead workers)
/ `web.pool_reload` (replace one at a time; a replacement must print its
ready marker and survive a probation window before the old worker is drained
— a deploy carrying a syntax error is a failed deploy and zero downtime) /
`web.pool_stop`. Workers are separate processes on purpose: a rolling reload
must **parse the new source**, which nothing inside an already-loaded image
can do. Shared-nothing workers mean no cross-request in-memory state — keep
state in SQLite or `persist`. `tests/run_web_pool.sh` holds all of it.

Response fields:

- `id`: required positive integer matching a pending request
- `status`: optional HTTP status, default `200`
- `headers`: optional record of string values, default `{}`
- `cookies`: optional array of `Set-Cookie` strings, default `[]`
- `body`: optional string, default `""`
- `file`: optional path — stream that file as the body (*since 0.1.0-rc4*)
- `stream`: optional `true` — open the connection for `webserver.emit`
  (*since 0.1.0-rc4*)

`body`, `file` and `stream` are mutually exclusive; a response naming two of
them is refused by name rather than guessed at.

The server supplies `Content-Length`, closes the connection after the response,
emits one `Set-Cookie` header per `cookies` item, and defaults the content type
to `text/plain`. If no matching response is queued within 30 seconds, the
client receives HTTP 504.

### Streaming and file responses (*since 0.1.0-rc4*)

**File responses.** `{ id: req.id, file: path }` sends the file's bytes in
64K chunks with `Content-Length` taken from the file itself, so a download is
never held in a gBASIC string. A path that does not name a readable regular
file is answered `500` and reported on stderr. `web.static` returns this
shape, which is what makes serving a large asset cost memory proportional to
a chunk, not to the asset.

**Streaming responses.** `{ id: req.id, stream: true, headers: {...} }`
answers the request's *head* and keeps the connection open. The body is
EOF-framed (no `Content-Length`; the connection closing *is* the end), and is
written with:

```basic
ok = webserver.emit(server, req.id, "data: tick\n\n")
webserver.finish(server, req.id)
```

`emit` returns `false` when the client is gone — the disconnected client is
reaped at that moment, so a broadcast loop that drops IDs whose emit returned
`false` is the whole liveness protocol. `finish` closes the stream
deliberately; a stream never finishes on its own.

A streaming connection is exempt from the listener's `timeout:` budget on
both sides — it is not a response the server is late producing, and it is not
an idle client stalling a request — so an SSE stream parked for an hour
coexists with a 1-second timeout for everything else. Draining (SIGTERM, or
`server.draining = true`) **ends** every parked stream: the client sees EOF
and the worker exits by itself, because a connection that never finishes
would otherwise hold the drain open forever.

The `web` library carries the SSE spellings: `web.sse(req)` builds the
stream-opening response with the `text/event-stream` headers,
`web.sse_event(data)` formats a data-only event (multi-line data becomes
multiple `data:` lines), and `web.sse_named(name, data)` adds the `event:`
field.

The economics worth knowing: a **parked** stream costs nothing while quiet —
it is one file descriptor the poll loop watches for disconnect. An
**in-handler emit loop** pins the worker for its whole duration, exactly like
any long handler, so a pool sized for N concurrent requests is a pool sized
for N concurrent emit loops. Park and poke where the data arrives from
elsewhere; loop in the handler only when the handler itself is the source.
`tests/run_web_stream.sh` holds all of it.

### The `server` block (*since 0.1.0-rc4*, PLAT-WEB-5)

A declarative front end over everything above — a listener, its sites and
their handlers in one place, in source order:

```basic
server myapp( port: 8080 )

    root "public"

    get "/"( req )
        return { body: "hello" }
    end get

    get "/products/{id}"( req )
        return { body: "product " + req.params.id }
    end get

    stream "/events"( req )
        e = emit(req, web.sse_event("hi"))
        return 0
    end stream

end server

program main( args )
    h = serve(myapp)
    print "listening on " + string(h.port)
end program
```

**Let `main` return. Do not loop after `serve`.** A single-process server
serves from the event loop that runs **after `main` returns**, so the shape
most service authors reach for —

<!--fragment: the anti-pattern this warning exists for; it hangs by design-->
```basic
h = serve(app)
while h.running        ' never reaches the event loop
    sleep(0.25)
end while
```

— binds the listener, prints the banner, sets `h.running` true and **accepts
connections**, while every request hangs forever with no response and nothing
on stderr. Every check short of an actual request says the service is healthy.
Sleeping while a server is bound but not yet serving now **warns**, because
reading the documentation is not much help when the symptom looks like a
working server.

A *held* listener (`webserver.listen(port, { hold: true })`) is the exception
and is not warned about: it is bound deliberately without being served, so a
supervisor can hand it to workers over `LISTEN_FDS` and poll them.

**Zero new reserved words.** `server`, the verbs, `web`, `root`,
`trust_proxy` and hook names are ordinary identifiers recognized by
position and validated at load time — `server = webserver.listen(0)` keeps
meaning what it always did. Adding a verb never touches the grammar.

**The declaration is inert and position-blind.** It binds its name to a
plain record — options, sites, route table, handler *function values* —
before the program block runs, exactly like a function declaration
(`tests/run_pre_registration.sh` pins the set; gBASIC Studio's hoisting rule
moves with it). Nothing opens a socket or reads a certificate until
`serve()`. Handlers are registered under internal names user code cannot
spell: callable only through the values the record carries, polluting no
scope. A server block implies `load web`.

**Head options are literals** — number, string, `true`/`false` — and that
restriction is what makes every load-time check statically decidable:
duplicate `method+path`, patterns that can never be told apart, malformed
`{captures}`, unknown verbs/directives/hooks, duplicate hosts, `root`
twice, a computed option. All of it is refused at load with file:line
through the same diagnostics as parse errors (`--json-diagnostics` code
`GB_DIAG_SERVER_BLOCK`). Deliberately runtime instead: certificate file
existence, port availability, static directory existence.

**A computed option does not require abandoning the block.** The declaration
binds a plain record and `serve` reads `options` off it, so a value known only
at run time — a port from `/etc`, an address from the environment — is applied
by overriding that record before serving:

```basic
app.options.port = number(conf.port)
h = serve(app)
print "listening on " + string(h.port)
```

Report the port from the **live** record (`h.port`), never from the
configuration: with `port: 0` the kernel chooses. A service that prints the
configured port while listening on the declared one is worse than either, and
is what `tests/run_packaging.sh` exists to catch. Configuration that changes
the *shape* of the server — routes, hosts — still belongs to
`webserver.listen` and `web.dispatch`.

Options: `port` (required unless sockets are inherited), `address`,
`timeout`, `workers`, `cert`/`key` (the default TLS pair), `inherit`.
Multi-host sites nest as `web name( host: "...", cert: "..." )` blocks —
the SNI table is derived from where the certs were written. Bare entries
form the implicit default site. `root` serves files only when no route
answered (routes win over files on overlap), scoped to the site that
declared it. `trust_proxy "ip", ...` applies `web.trust_proxy` to every
request. `on drain ... end on` runs once when draining begins.

**`serve(sv)`** (a `web` function, reachable unqualified) binds and runs:

- Inherited sockets (`LISTEN_FDS`) are adopted first and `READY` is printed
  — a spawned worker and a systemd-activated process are the same path.
- `workers: N` (default 1) makes the call the supervisor: it binds with
  `hold:`, prints `PORT <n>`, spawns N copies of **this program** via
  `process.self()` over `web.pool`, ticks until asked to stop, then drains.
- Otherwise it listens and returns the live server record. **Bind it** —
  `h = serve(myapp)` — for two reasons: `h.port` is how you learn the port when
  you bound `port: 0`, and a bare call discards a non-`nothing` return, so the
  `unused-result` warning fires on every start and lands in an operator's
  journal. Dispatch is installed natively (`webserver.on_request`) rather than
  through a watcher, so an unassigned call does still serve; it is just noisy
  and throws away the port.

Handlers take `(req)` and **return** the response record; captures are on
`req.params`. `stream` handlers write with `emit(req, text)` / close with
`finish(req)` and are SSE-headed. A raise inside a handler kills the worker
(let-it-crash, §7b of the design). A handler that returns a **non-record**, or
a record the response validator refuses (a non-string `body`, an out-of-range
`status`, a CRLF in a header value, `body` and `file` together — the full list
is below), is a 500 with the reason named on stderr, **and serving continues**:
a wrong response is the handler's mistake, not the listener's death. The same
refusals RAISE on the `append(server.responses, {...})` path, where the program
made the mistake in its own frame and the error can name that line. Handlers
cannot rebind caller state, so shared state (a broadcast list) lives in
**fields of a program global** — the one mutation a function makes visible.

That distinction matters most where it is least visible. Reflecting a query
parameter into a response header is ordinary handler code; the CRLF refusal is
what stops it becoming response splitting. While that refusal was fatal, any
such handler was a remote kill switch on the default `workers: 1`.
`tests/run_web_handler_errors.sh` holds every refusal, each asserting that the
next request is still answered.

**Testing with no socket** (`tests/run_web_server_block.sh` holds all of
this): `web.routes(myapp)` returns the route table as flat data;
`web.dispatch(myapp, req_record)` runs one request through host dispatch,
routes and static fallback (a stream route answers with its opening head);
`source_outline` shows the block, its sites and every route.

Three primitives shipped with the block and are general:
`process.self()` → `{ interpreter, script, args }` (how a program launches
another copy of itself); `webserver.on_request(server, fn, context)` /
`webserver.on_drain(server, fn, context)` (native per-request dispatch —
`fn(context, request)`, return value delivered as the response); and
`webserver.stopping()` (true once SIGTERM asked this process to stop
serving — the supervisor loop's exit condition).

Shutdown forms:

```basic
webserver.close(server)
server.running = false
```

**The paragraph that used to sit here described "Phase 1" and was left behind
by the four phases after it** — it denied TLS, routing, static files, streaming
and public binding, all of which the sections above document and the
`tests/run_web_*.sh` suites exercise. What follows is the current shape.

The queue API above is single-threaded and handles one request per connection,
with `Content-Length` request bodies and explicit cookie parsing/emission. The
worker pool, streaming, TLS, static files and the `server` block are documented
in the sections above and build on the same listener.

Still unsupported anywhere in the WebServer: **chunked request bodies**
(refused with 501 rather than guessed at — see §6 hardening), **WebSockets**,
multipart uploads, templates, and sessions. Public binding requires an explicit
`address` option; the default stays loopback.

## XML Module

XML support is available when gBASIC is built with libxml2. It parses XML (and,
leniently, HTML) into an in-memory node tree and navigates it by path:

```basic
load xml

doc = xml.parse("<order id='A1'><item>Widget</item><item>Gadget</item></order>")
print(xml.attr(doc, "id"))              ' A1
print(xml.text(xml.find(doc, "item")))  ' Widget
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

<!--fragment: an API shape or a deliberately invalid form, not a program-->
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

> Guided introduction: [`docs/gui_tutorial.md`](gui_tutorial.md). Task-by-task
> recipes, all executed by the suite: [`docs/gui_cookbook.md`](gui_cookbook.md).

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
  **`gtk.application` is SINGLE-INSTANCE**, because it builds a `Gtk.Application`
  with GTK's default flags and that is GTK's default. A second run of your
  program then does not open a second window: it registers as a remote, forwards
  `activate` to the process already running, and exits. The running process gets
  an *extra* `activate` — so if your handler builds a shell over globals, it
  builds a second one and every signal handler is now doubled. Both halves of
  that cost gBASIC Studio a day. If you want independent windows, build the
  application object by hand with `NON_UNIQUE`:

  ```basic
  gi.require("Gio", "2.0")
  solo = gi.enum("Gio.ApplicationFlags.NON_UNIQUE")
  app = gi.new("Gtk.Application", "application-id", "com.example.App", "flags", solo)
  ```

  `gtk` has no flags parameter on purpose — it wraps GTK rather than replacing
  it, so anything unwrapped is reached through `gi`.
- `gtk.box(orientation, spacing)` / `gtk.paned(orientation)` — `orientation` is
  `"h"`/`"horizontal"` or `"v"`/`"vertical"`.
- `gtk.scrolled(child)` / `gtk.stack()` / `gtk.notebook()` / `gtk.listbox()`.
- `gtk.button(label)` / `gtk.label(text)`.
- `gtk.connect(widget, signal, handler)` — alias for `gi.connect` (named
  `connect`, not `on`, because `on` is a reserved keyword).
- `gtk.enum("Gtk.Orientation.VERTICAL")` — resolve an enum/flags member to its int.
- `gtk.require()` — ensure the GTK 4 namespace is loaded. Safe to call
  repeatedly; every other `gtk.*` constructor calls it, so you only need it
  directly when reaching for `gi.*` before any `gtk.*` call.

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

### `gtkui` — declarative widget-tree reconciler (`load gtkui`)

A **pure-gBASIC** dynamic reconciler over the `gi` bridge (needs `load gi`; GTK 4
must be initialized before widgets are built). You describe a UI as a tree of
records; `gtkui` builds the real GTK 4 widget tree and, on each `update`, mutates
the *existing* widgets in place — changing only the properties that changed,
inserting/removing/reordering children, and replacing a widget only when its type
or identity changes. This is the dynamic tree mutation the old GTK 3 `gui` module
never had. It is **not** a closed widget system: every node maps to a real GTK
object you can reach and mix with hand-built widgets.

**Node schema** (a record; every field optional except one of `type`/`widget`):

- `type` — GI type name (`"Gtk.Button"`); built generically with `gi.new`, so any
  introspected widget works with no new code.
- `widget` — an already-created GObject: the **native escape hatch**. `gtkui`
  parents it and (optionally) connects its signals, but does not manage its props
  or children — use it to embed a manual `GtkGrid`, a `sourceeditor` view, or any
  other component the reconciler did not build.
- `key` — stable identity string within a sibling list.
- `props` — record of property → value (`gi.set`; enums as ints via `gi.enum`).
- `signals` — record of signal-name → handler function.
- `children` — array of child nodes.

**API:**

- `gtkui.mount(parent, desc)` → a handle; builds `desc` and attaches its root to
  the existing `parent` container.
- `gtkui.update(handle, desc)` → **a new handle**; reconciles and returns the
  updated handle (records are copy-on-write, so reassign: `h = gtkui.update(h, d)`).
- `gtkui.unmount(handle)` — detach the root and disconnect every handler.
- `gtkui.root(handle)` → the root GTK widget (raw, for interop).
- `gtkui.lookup(handle, key)` → the GTK widget for a keyed node, or `nothing`.

**Identity.** When *every* child in a sibling list carries a `key`, children are
matched by key — enabling insert, remove, and **reorder** with the same widget
instances preserved (and their cursor/scroll/selection/focus state with them).
Otherwise matching is positional by index. Text/labels are never used as identity.

**Reconciliation.** A node whose `type` (or native identity) is unchanged reuses
its widget; only changed properties are re-applied (unchanged ones are left alone,
so widget state is preserved). A property dropped from the description is **not**
reverted — set it explicitly to change it. A type/identity change under a slot
replaces the widget (old detached and destroyed, new created and inserted).

**Signals.** Each reconcile disconnects the handlers it previously connected (by
recorded id) and connects exactly those in the new description — so there is
always exactly one connection per declared signal, always the current handler,
never a duplicate across repeated `update`s.

**Container adapters** (extensible kind table): `Gtk.Box` (ordered multi-child:
append/remove/reorder), single-child containers (`Gtk.Window`,
`Gtk.ApplicationWindow`, `Gtk.ScrolledWindow`, `Gtk.Frame`: `set_child`), and
`Gtk.Paned` (two keyed slots, `"start"`/`"end"`). Other widget types are leaves
(no children). New container kinds are added by extending the table, not the core.

**Limitations (v1):** the widget subset above (other containers are one table
entry away); mixed keyed/unkeyed sibling lists fall back to positional matching;
dropped properties are not auto-reverted. See `examples/native_ui/dynamic_list.bas`
for a worked demo.

**SourceEditor vs. gBASIC Studio:** `sourceeditor` is a reusable, general editor
component. It is deliberately free of Studio concepts (projects, execution
boundaries, branches, inspector); a future gBASIC Studio would *use* this library,
not the other way around.

### `datagrid` — virtualized data grid (`load datagrid`)

A general, reusable table for large tabular datasets. It displays 10⁴–10⁶+ rows
in a `GtkColumnView` **without building one widget per row or per cell**: GTK
realizes cell widgets only for the rows on screen (plus recycling slack), so a
million-row grid costs a few hundred widgets, not a million. It is a general
component (ledgers, admin tables, database browsers, ETL monitors, scientific
data), not tied to any application.

**Why a native piece exists.** GTK4 virtualizes only when fed a `GListModel`,
which is a GObject *interface* — implementing it from gBASIC would require runtime
interface subclassing, which the platform avoids. So one small fixed C GType
(`rowmodel.*`, the `GbRowModel` adapter) supplies the row **count** and lazy
index-carrying row **proxies** to GTK, and nothing else. It holds NO row data and
never calls back into the interpreter. Everything above it — columns, factories,
formatting, selection, refresh — is gBASIC over `gi`. The only gBASIC re-entry is
the `GtkSignalListItemFactory` "bind" signal, on the GTK main-loop / interpreter
thread through the normal, error-contained `gi.connect` path. **Main-thread rule:**
like all of `gi`, a grid is built and updated only on the interpreter's thread.

**Setup (one line, at program scope):**

```basic
load datagrid
_DATAGRID = datagrid.new_registry()
```

The factory bind handler, which GTK calls with only `(factory, item)` and which
gBASIC (no closures) cannot give a captured grid, finds its grid through the
program-global registry `_DATAGRID`. It must be created at program scope.

**Source modes:**

- **Array-backed** — `datagrid.create(rows)` where `rows` is an array of records,
  of arrays, or of scalars. COW arrays make row access O(1), so the array is
  never copied into native storage. `create` takes a **COW snapshot**: it shares
  the backing store until either side mutates. Later mutation of your variable
  does *not* change the grid, and the grid never touches your variable; call
  `datagrid.set_rows` to show new data. A deliberate, predictable rule.
- **Virtual/generated** — `datagrid.create_virtual(count_fn, cell_fn)`. Nothing is
  materialized: `count_fn()` returns the logical row count and `cell_fn(row, col)`
  computes a cell on demand. This is the path for database cursors, generated
  data, or datasets too large to hold in memory, and scales to 10⁶+ logical rows.

**API:**

- `datagrid.new_registry()` → registry (assign to `_DATAGRID`).
- `datagrid.create(rows)` / `datagrid.create_virtual(count_fn, cell_fn)` → handle.
- `datagrid.add_column(handle, spec)` → handle. `spec`: `title`, `field` (record
  field) or `index` (element index; omit both for a scalar source), `resizable`
  (default true), `format` (optional `(value)->string`).
- `datagrid.widget(handle)` → the `GtkColumnView` (embed / customize directly).
- `datagrid.selection(handle)` → the `GtkSingleSelection`; `datagrid.selected(handle)`
  → selected logical row index, or −1 (GTK single-selection autoselects row 0 by
  default).
- `datagrid.cell(handle, row, col)` → the exact string a cell displays (the same
  path bind uses); reads grid values without a rendered window.
- `datagrid.row_count(handle)`; `datagrid.set_rows(handle, rows)` (array source);
  `datagrid.refresh(handle)`; `datagrid.set_count(handle, n)` (virtual source).
- `datagrid.destroy(handle)` / `datagrid.destroyed(handle)` — lifecycle. Exact
  semantics:
  - `destroy` releases the **registry-owned** state: the grid's `GtkColumnView`,
    selection model, native row model, columns, factories, and the callback-reachable
    source and `format` function values.
  - It does **not** destroy an enclosing window or container. Native widgets remain
    subject to ordinary GTK ownership: anything still parented is owned by its parent
    and goes away when that parent does. Destroy the window separately.
  - The registry **slot is tombstoned, not removed**, so grid ids stay stable — the
    bind handler resolves its grid by id (an index into `grids`), and removing an
    element would shift every later grid's id.
  - `destroy` is idempotent; `destroyed(handle)` reports the state. A late in-flight
    bind on a destroyed grid renders an empty cell rather than unwinding through GTK.
  - Other accessors (`widget`, `cell`, `refresh`, …) on a destroyed handle currently
    **raise** — but with an internal message (`unknown record field: view`) rather
    than a purposeful one. Check `destroyed(handle)` first. Tidying that message is
    deferred (it would change just-accepted behavior).
  - Tombstones are constant-size records holding no factories or callbacks, so
    create/destroy cycles retain nothing; the `grids` array itself still grows by
    the create count (deferred).
- `datagrid.model(handle)` — the native `GbRowModel` (a `GListModel`) behind the
  grid. Mainly for instrumentation: `rowmodel.item_requests(datagrid.model(h))`
  reports how many rows GTK actually realized, which is how the 1M-row
  virtualization claim is measured rather than asserted.
- `datagrid.accesses()` / `datagrid.setups()` / `datagrid.reset_accesses()` —
  bind and setup counters (instrumentation; `reset_accesses` zeroes both).

**Updates** go through the model's `items-changed`, so `GtkColumnView` re-binds
visible rows rather than rebuilding widgets.

**Sorting/filtering** are not built in for v1: sort/filter your source (or wrap
the model in `GtkSortListModel`/`GtkFilterListModel` via `gi` on
`datagrid.widget`) and `datagrid.refresh`. Kept out of v1 so they don't complicate
the virtualization core.

**Errors** in an array-backed lookup (out-of-range row, missing field) render as
empty text rather than raising. A virtual `cell_fn` that raises is contained by
the `gi` signal machinery (it does not crash GTK or unwind through C), so keep
`cell_fn` total where possible.

**Ownership:** the handle is the only thing you need to keep. The registry entry
retains the view, selection, native model, and every column's factory and format
function for as long as the grid lives, so factories stay internal — you never
need to hold one in a variable of your own for rendering to work. (An earlier
NAP-12 note claiming otherwise was a measurement artifact and has been retracted;
see DOGFOOD and `tests/datagrid/lifetime.bas`.)

**Measuring binds:** `GtkColumnView` realizes and binds its visible rows when the
view is first given a size — when it is added to a window — *not* on `present()`
and not when the main loop runs. Reset the counters before building the widget
tree; resetting afterwards zeroes work already done and makes a healthy grid look
inert. Worked demo: `examples/native_ui/datagrid_demo.bas`.

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

Password-based key derivation — turning a **passphrase** into key bytes:

- `pbkdf2_sha256(password, salt, iterations, length)` /
  `pbkdf2_sha512(...)` — PBKDF2-HMAC (RFC 8018). Returns exactly `length` raw
  bytes, ready for `aes_gcm_encrypt`.
- `scrypt(password, salt, n, r, p, length)` — scrypt (RFC 7914). `n` is the CPU
  and memory cost and must be a **power of two greater than 1**; `r` is the block
  size and `p` the parallelisation. Memory-hard, so prefer it when you can afford
  the memory.

```basic
salt = random_bytes(16)                                  ' store this beside the ciphertext
key  = pbkdf2_sha256(passphrase, salt, 600000, 32)       ' 32 bytes = AES-256
blob = aes_gcm_encrypt(key, random_bytes(12), secret, "")
```

**Choosing parameters.** As of 2026, OWASP suggests **600,000** iterations for
PBKDF2-HMAC-SHA256 and **210,000** for SHA-512; for scrypt, `n = 65536, r = 8,
p = 1`. gBASIC does **not** enforce a floor: RFC 6070 and RFC 7914 publish test
vectors with deliberately tiny costs, and a floor would make an implementation
untestable against the vectors that prove it correct. Choosing a cost is
therefore yours, and a low one is silently weak — which is why the recommended
values are written here rather than left implicit.

**The salt must be non-empty**, and both functions raise if it is. RFC 8018
permits an empty salt; it is always a mistake, because it turns a KDF into a
plain iterated hash that a precomputed table defeats, with nothing observable to
say so. Use `random_bytes(16)` and store it in the clear beside what it protects.

These are **not** `password_hash`. That one verifies a login and returns a hash
*string* with its parameters embedded; these return raw key *bytes* for
encryption. Use `password_hash` to check a password, a KDF to derive a key from
one.

Authenticated encryption and signatures:

- `aes_gcm_encrypt(key, nonce, plaintext, aad)` — AES-GCM; returns ciphertext with
  its authentication tag. `key` is 16 or 32 bytes; `nonce` is 12 bytes.
- `aes_gcm_decrypt(key, nonce, blob, aad)` — verifies and decrypts; a wrong key,
  nonce, `aad`, or tampered blob raises rather than returning garbage.
- `ed25519_keypair()` — a record `{ private, public }`.
- `ed25519_sign(private, message)` — a signature.
- `ed25519_verify(public, message, signature)` — `true` / `false`.

Bad argument types, wrong key/nonce sizes, and failed authentication raise
structured errors — these builtins pre-validate rather than degrade. So do
`base64_decode` and `hex_decode` on malformed input.

> **This page was right and the implementation was not, until 0.1.0-rc9.**
> `aes_gcm_encrypt`/`decrypt`, `base64_decode`, `hex_decode` and
> `ed25519_sign` returned `unknown` on failure instead of raising, while the
> KDFs in the same family raised as documented. That inconsistency is what
> marked it an oversight rather than a policy — and it was the most dangerous
> shape available, because a caller reading this page writes no check, and
> `string(unknown)` is the *word* `"unknown"`: a credential vault would hand
> its consumer that literal text and store it as though it were a secret.
> Reported by the Transward build, which trusted the "refuses rather than
> guesses" doctrine and therefore wrote no check.

**The library layer answers differently, on purpose.** `crypto.decrypt`,
`crypto.verify_cookie` and `crypto.jwt_verify` return `unknown` rather than
raising, because a blob that does not authenticate is *expected input* there —
an attacker can put anything in a cookie — rather than a bug in the caller.
The builtins raise; the verification layer catches and answers. Both contracts
are deliberate, and the distinction is which of the two you are calling.

A higher-level pure-gBASIC layer ships as `load crypto` (`stdlib/crypto.bas`),
built on the above: `sha256_hex`/`sha512_hex`, `random_hex`/`random_token`,
`sign_cookie`/`verify_cookie`, `csrf_token`/`csrf_check`, `encrypt`/`decrypt`,
a flat `json_decode`, and `jwt_encode`/`jwt_verify` (HS256). See
`docs/crypto_design.md`.

`crypto.json_encode` was **removed in 0.1.0-rc8** — use the core `json_encode`,
which is what an unqualified call already reached. `crypto.json_decode` stays:
it reads attacker-supplied token payloads and accepts RFC 8259 and nothing else,
where `try_decode` deliberately speaks the permissive gBASIC dialect.

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
- `env` (record, optional) — environment variables for the child, **merged over
  the inherited environment**: a name maps to a string, or to `nothing` to
  unset it. Merged rather than replacing, because a child that loses `PATH` and
  `HOME` to gain one variable is almost never what was meant. The parent's own
  environment is untouched.

  ```basic
  process.run({ command: "ssh", args: ["host", "true"],
                env: { SSH_ASKPASS: "/usr/lib/gb/askpass",
                       SSH_ASKPASS_REQUIRE: "force",
                       DISPLAY: nothing } })
  ```

  This is how a credential reaches a child that reads one from the
  environment — `SSH_ASKPASS`, `GIT_SSH_COMMAND`, `SSL_CERT_FILE`, `TZ`,
  `DOCKER_HOST`. Without it the only route was a generated shell wrapper
  script, which puts a shell back into a code path whose point is that nothing
  is parsed as shell syntax.

**Unknown options are refused by name** (*since 0.1.0-rc9*). They were
previously dropped in silence, so a misspelling looked exactly like the feature
working until the child reported an empty variable — the same argument
`webserver.listen` makes, with a sharper edge: an ignored option there leaves a
server on loopback, and here it leaves a credential unset.
- `timeout` (number, optional) — seconds; when the child outlives it, its whole
  process group is killed. Absent or `<= 0` means no limit.
- `launch_failure` (string, optional; *since 0.1.0-rc3*) — `"raise"` (the
  default) or `"result"`. Under `"result"`, a failed **launch** — a missing
  executable, an unenterable `cwd` — returns a record instead of raising, with
  two extra fields: `launch_failed` (boolean) and `why` (the message the raise
  would have carried). A successful run under this option carries
  `launch_failed: false`, so callers can branch without `has()`. The default
  record shape is unchanged for everyone who does not opt in. This was
  introduced when a raise could not be caught at all, which made attempting an
  *optional* external tool unsafe under the default; frame-scoped `on error`
  now offers a second route, and this option remains the direct one — the
  reason a launch failed arrives as data beside the rest of the result. See
  also `process.which`, and note a child that ran and exited `127` is still
  distinguishable (`launch_failed: false, exit_code: 127`).

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
  Opt out per call with `launch_failure: "result"` (above).

`process.which(name)` (*since 0.1.0-rc3*) answers "what would `execvp` run for
this name?" — the resolved path, or `unknown` when nothing runnable is found.
It mirrors exec's own rules: a name containing `/` is tried as a literal path
with no search; otherwise `$PATH` is walked (falling back to the system default
path when `PATH` is unset, as `execvp` does), an empty `PATH` component meaning
the current directory. A candidate must be a **regular file with execute
permission** — a directory named like the tool is not a hit, which is the
mistake hand-rolled `exists`-based PATH walks make. Not-found is `unknown`,
never an error: absence is the ordinary state this function exists to report.

```basic
git = process.which("git")
if is_unknown(git) then
    ' the optional feature stays quiet
end if
```

Between a `which` and a `run` the file can still vanish (the classic
check-then-use race); a caller who cares runs with `launch_failure: "result"`
and reads `launch_failed` instead of checking first.
- **Environment** is inherited from the interpreter (no per-call environment override
  in this version).
- **Blocking** — `process.run` waits for the child to finish and captures its output
  fully into memory (no streaming; large output uses proportional memory). To keep a
  GUI responsive, run it inside a spawned actor and deliver the result to the main
  loop via `gi.watch_mailbox` (the actor + event-loop pattern).

### Live child control

`process.run` decides everything before the child starts and hands back one record
after it is gone. When a program needs to **supervise** a child instead — watch it,
read it as it goes, and end it on its own terms — six primitives expose the same
machinery while the child is alive. They are additive: `process.run` is unchanged
and remains the right tool whenever you only want the finished result.

```basic
h = process.start({ command: "./long-job", args: ["--verbose"] })

while true
    c = process.read(h)          ' whatever has arrived; never blocks
    if byte_count(c.stdout) > 0 then
        print c.stdout
    end if
    s = process.poll(h)          ' never blocks
    if not s.running then
        break
    end if
    sleep(0.05)
end while

print "exit " + s.exit_code
process.release(h)
```

**`process.start(options)`** → a **process handle**. Same `command` / `args` /
`cwd` options as `process.run`, validated identically; `timeout` is rejected,
because bounding a run is now the caller's job (`process.wait` or `process.stop`).
Returns as soon as the child is launched. A child that cannot be launched **raises**
— exactly as with `process.run`, so it stays distinguishable from a child that ran
and exited nonzero. `type(h)` is `"process"`.

**`process.poll(handle)`** → `{running, exit_code, signal, success}`. Never blocks.

**`process.read(handle)`** → `{stdout, stderr}` — everything that has arrived since
the previous read, and nothing else. Never blocks.

**`process.wait(handle)`** / **`process.wait(handle, seconds)`** → the same status
record. Without a timeout it waits for the child to exit; with one it gives up
after that long. **A wait that expired is reported by `running` still being `true`**
— there is no separate `timed_out` field to consult. The timeout accepts a number
of seconds or a duration (`process.wait(h, 5 seconds)`).

**`process.stop(handle)`** / **`process.stop(handle, {force_after: seconds})`** →
the status record. See escalation below.

**`process.release(handle)`** → closes the pipes and reaps the child. Idempotent,
and never required for correctness — see abandonment below.

#### Reading is incremental and never blocks

The read pipes are non-blocking, so `process.read` returns immediately with
whatever the child has produced so far, which may be nothing. Concatenating every
read in order reproduces the child's output exactly: **each byte is delivered once
and once only**, with nothing lost at a boundary and nothing repeated.

`process.read` does **no line framing at all**. If the child has written half a
line, you get half a line; the remainder arrives on a later read. This is
deliberate — a reader that split on newlines would have to either buffer a partial
line invisibly or hand back a line that has not finished. Assembling lines is the
caller's business, and concatenation is all it takes. The same holds for multi-byte
UTF-8: a character split across two reads reassembles correctly, because gBASIC
strings are byte-exact.

`process.wait` keeps draining while it waits. That is not an optimization but a
requirement: a child writing more than a pipe buffer (~64 KB) would otherwise block
writing while you block waiting.

#### Stopping, and escalation

Escalation is always the **caller's** choice, never a hidden policy:

| Call | Behavior |
|---|---|
| `process.stop(h)` | Sends **SIGTERM** and returns at once. If the child ignores SIGTERM it keeps running, and the returned `running` says so truthfully. |
| `process.stop(h, {force_after: N})` | Sends SIGTERM, waits up to **N seconds**, and only then sends **SIGKILL** and waits for the child to actually go. |

There is no default grace period, because there is no default escalation: omitting
`force_after` means "ask politely and tell me what happened," full stop. Choose a
grace period that suits the child — long enough for it to flush and shut down
cleanly, short enough that you are not stuck waiting on a process that will never
comply.

Signals go to the child's **process group**, so a shell script's own children die
with it, exactly as `process.run`'s timeout does.

#### Handles are safe to abandon

A handle is a **reference**: copies share one child, so a `stop` through one copy is
visible through every other. When the **last** copy goes away — whether through
`process.release` or simply by the variable being reassigned or going out of scope —
the pipes are closed and the child is reaped. Dropping a handle therefore cannot
leak a descriptor or leave a zombie, and no `release` is strictly required.

Abandoning a handle does **not** kill a running child: letting a variable go out of
scope is not a decision to end a process, and a handle copy expiring inside a helper
function must not be lethal. Such children are reaped opportunistically on later
`process.*` calls, and **at program exit any still running are killed and reaped**,
so nothing this interpreter started outlives it.

**That holds under a kill, too.** The teardown pass above cannot run when the
interpreter is `SIGKILL`ed, so it is not what the guarantee rests on: every child
is launched with a **parent-death signal** armed in the kernel, and receives
`SIGTERM` the moment the interpreter dies, however it dies. This is the same
mechanism spawned actors have always used, and it cascades — a child that is
itself a gBASIC program passes it to its own children.

There is no opt-out, and `process.start` is therefore **not** a way to launch a
daemon that outlives the program: a child that should survive belongs to a
service manager, not to an interpreter that might be killed. (Two caveats, both
kernel-level: a child that ignores `SIGTERM` still survives — the same bargain
`process.stop` strikes before escalating — and a set-user-ID executable loses the
armed signal at `exec`.)

Handles are local capabilities, not data: `serialize`, `send`, and `json_encode`
reject them, and `reflect.serializable` reports `false`. Each interpreter process —
including each spawned actor — owns its own children; a handle is meaningful only in
the process that started it.

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
port = default(env("GBASIC_SITE_PORT"), "8080")
```

**`default(value, fallback)`** (*since 0.1.0-rc9*) - `value`, unless there
isn't one: returns `fallback` when `value` is `unknown` **or** `nothing`, and
`value` otherwise.

Both absences count, deliberately — the two commonest producers of "no result"
split across them (`env(name)` yields `unknown` when unset, `find(...)` yields
`nothing` on a miss), and a caller reaching for a fallback wants the same
answer from each. Use `is_unknown`/`is_nothing` where the difference is the
point.

It tests **presence, not truthiness**: `false`, `0` and `""` are values and are
returned unchanged.

```basic
port  = default(env("PORT"), "8080")
where = default(find(names, "zed"), -1)
flag  = default(false, true)              ' false — it is a value
```

`fallback` is evaluated eagerly, so keep it cheap (a literal, or a name).

**`has_builtin(name)`** (*since 0.1.0-rc3*) - Answers whether this interpreter
has an unqualified builtin of that name, so a program can degrade gracefully on
an older release instead of crashing with `undefined variable`. Names of module
functions (`process.which`, `reflect.inspect`, …) are **refused** rather than
answered: they dispatch inside each module and there is no unified table of
them, and a probe that can answer wrongly is worse than one that says it
cannot answer.

```basic
if has_builtin("file_type") then
    kind = file_type(path)          ' the precise check (newer builds)
else
    f{file} = path
    present = exists(f)             ' the portable fallback
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
today{day}= now()                   ' truncate to date precision via the day lens
```

There is intentionally no `today()` builtin: `today` is too common an
identifier to reserve, and the date is derivable from `now()` and the `{day}=`
truncation lens as shown above.

**`now(zone)`** returns the current civil time in a named IANA zone —
`now("UTC")`, `now("Asia/Tokyo")`. An unknown name is refused.

**A gBASIC datetime carries no zone.** It is civil wall-clock text, and every
conversion to an instant has to get a zone from somewhere. That makes one
mistake very easy to write and impossible to see:
`stamp = to_zone(now(), "UTC")` is **wrong** — it returns local time unchanged.
`to_zone` reads its input as *already UTC* and renders it in the target zone,
so handing it a local value gives that value straight back: a no-op that reads
like a conversion and is wrong by your offset. Use `now("UTC")`.

The same asymmetry bites the other way. `number(dt)` and `epoch(dt)` read a
datetime as **local**, so `number(from_zone(now(), zone))` — the documented
route to UTC — is wrong by the offset too, because a UTC civil value is being
read as local. State the zone: `epoch(dt, zone)`.

**`epoch()`** returns the current instant as Unix seconds. **`epoch(dt)`**
places a civil datetime on the timeline by reading it as **local**;
**`epoch(dt, zone)`** reads it as civil in that zone, which is the only correct
way to get an instant from a value that is not local:

```basic
utc   = now("UTC")
stamp = epoch(utc, "UTC")           ' == epoch(), the real instant
wrong = number(utc)                 ' off by your UTC offset
```

DST gaps and repeats resolve by the same policy `from_zone` uses, so both doors
give the same answer.

**`exit(code)`** ends the program with an exit status, unwinding out of any
function, loop or block — `stop` that also names a status. The code must be a
whole number 0–255; a wider value is **refused** rather than truncated, because
the kernel keeps only the low byte and `exit(256)` would silently report
success from a program that meant to fail.

**Datetime fields.** Components come out as *numbers* via dot access — the
lenses truncate, the fields extract, and no global names are spent on it:

<!--fragment: an API shape or a deliberately invalid form, not a program-->
```basic
d {date}= "2026-03-15 09:30:45"
d.year          ' 2026        d.hour    ' 9
d.month         ' 3           d.minute  ' 30
d.day           ' 15          d.second  ' 45
d.weekday       ' 7 — ISO 8601: Monday=1 … Sunday=7, so d.weekday <= 5 is the workday test
d.dayname       ' "Sunday"
d.day_of_year   ' 74
d.precision     ' "second" — one of year/month/day/hour/minute/second (or "time")
d.time          ' 9 hours 30 minutes 45 seconds — an exact duration since midnight
```

Reading a field **finer than the value's precision yields `unknown`** — a
month-precision value has no meaningful `.day`, and unknown is how gBASIC says
"absent", never a plausible zero. An unknown field *name* raises
(`datetime has no field 'yaer'`), so a typo is an error rather than a quiet
unknown. Note the ISO weekday numbering has no zero: Sunday=0 is the
C/JavaScript convention, Sunday=1 is Excel's; gBASIC follows ISO.

**Duration fields.** `dur.years`/`.months`/`.weeks`/`.days`/`.hours`/
`.minutes`/`.seconds` return the components as stored, and
`dur.total_seconds` returns the whole duration in seconds — raising if the
duration carries months or years, which have no fixed length.

**Datetime arithmetic follows the accountant's rule.** `d + duration` applies
years and months first, **clamps the day** into the resulting month, then adds
the exact parts as elapsed time:

<!--fragment: an API shape or a deliberately invalid form, not a program-->
```basic
jan31 {date}= "2026-01-31"
jan31 + 1 month           ' 2026-02-28   (clamped)
jan31 + 1 month + 1 day   ' 2026-03-01   (clamp first, THEN the day)
```

The round trip `(d + 1 month) - 1 month` does **not** hold at month-end —
clamping is lossy by design. `a - b` between two datetimes yields a signed
**exact** duration (days and smaller, never months); the calendar question
"how many months apart" is `dates.between(a, b, "months")`.

**Duration algebra and comparison.** Durations are signed and closed under
`+`, `-`, `× number` and `/ number`, with canonical results
(`(45 minutes) * 4` is `3 hours`). A duration has an exact part
(weeks/days/hours/minutes/seconds) and a calendar part (years/months), and the
two are never blurred: equality canonicalises within each family
(`1 week = 7 days`, `1 year = 12 months`, but `1 month = 30 days` is
**false**); ordering is defined only between exact durations — comparing a
month-bearing duration with `<`/`>` raises, as does scaling its months by a
non-integer. Refusals, not guesses: a month has no fixed length.

**Timezone edges** — `to_zone(dt, zone)`, `from_zone(dt, zone)`,
`zone_offset(dt, zone)`, `zone_offset(dt)`, `zone_resolve(dt, zone)`, with IANA names
(`"America/Chicago"`, `"UTC"`). The doctrine (design §9): UTC for the
timeline, civil time for the calendar, zone names at the edges — store future
intentions as a rule plus a zone name, never as UTC instants. `from_zone`
resolves DST edges by the "compatible" default (the repeated fall-back hour →
the earlier instant; the spring-forward gap → shifted forward);
`zone_resolve` returns `{kind, utc, earlier, later}` with `kind` one of
`unique`/`ambiguous`/`nonexistent` so callers can choose their own policy.

`zone_offset(dt)` with no zone answers the **system's** offset for that
wall-clock time, as a duration. The two-argument form asks "what if this were
in that zone"; naming the local zone is the one thing a caller cannot do for
itself, and without this a program can read the wall clock and has no way to
say what it is offset from — an RFC 5322 `Date`, an ISO 8601 offset and a
zone-stamped log line are all unwritable.
Unknown zones raise (a typo must not become quietly-UTC arithmetic), and
all-day values raise (`… an all-day value has no instant`). Worked example:
recipe 11 of `docs/datetime_cookbook.md`.

**`epoch(datetime)`** converts a `datetime` to a number of seconds since the
Unix epoch, and **`from_epoch(number)`** converts such a number back to a
`datetime`. These bridge to systems that speak epoch seconds (for example JWT
`exp` claims). `number(datetime)` is equivalent to `epoch(datetime)`.

```basic
issued = now()
exp = epoch(issued + 1 hour)     ' seconds since 1970 for the token deadline
when = from_epoch(exp)           ' back to a datetime
```

#### Measuring how long something took

**`monotonic()`** returns seconds as a floating-point number, from an
**unspecified origin**. Only the *difference* between two readings is meaningful:
subtract them to get an elapsed interval.

<!--fragment: an API shape or a deliberately invalid form, not a program-->
```basic
t0 = monotonic()
... the work ...
elapsed = monotonic() - t0       ' seconds, fractional
```

Use it instead of `epoch()` for any duration, for two reasons:

- **It cannot go backwards.** `epoch()` reads the wall clock, which NTP can step
  and a DST change can shift, so a duration computed by subtracting two
  wall-clock readings can come out *negative*. `monotonic()` is guaranteed
  non-decreasing.
- **It has sub-second resolution.** `epoch()` is whole seconds, so anything
  faster than a second measures as `0`.

Do not store it, print it as a date, or compare readings taken by different
processes or either side of a reboot — the origin is arbitrary and is not shared.
`epoch()` and `now()` remain the answer for "what time is it".

One caveat, stated because it is easy to be caught by: on Linux the monotonic
clock does not advance while the system is suspended. An interval spanning a
suspend therefore reads short — correct for measuring work, wrong for measuring
wall-clock elapsed.

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
type(42)         ' "number"  
type("hello")    ' "string"
type(true)       ' "boolean"
type([1, 2])     ' "array"
type({x:1})      ' "record"
type(nothing)    ' "nothing"
type(unknown)    ' "unknown"
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
number("42")     ' 42
number("3.14")   ' 3.14
number("abc")    ' runtime error
```

**`boolean(value)`** - Converts strings to booleans:
```basic
boolean("true")   ' true
boolean("false")  ' false
boolean("maybe")  ' runtime error
```

**`array(value)`** - Decodes JSON strings to arrays, passes arrays unchanged:
```basic
array("[1, 2, 3]")  ' [1, 2, 3]
array([4, 5, 6])    ' [4, 5, 6]
```

**`record(value)`** - Decodes JSON strings to records, passes records unchanged:
```basic
record("{\"x\":1, \"y\":2}")  ' {x:1, y:2}
record({a:1, b:2})            ' {a:1, b:2}
```

### String Helpers

**`replace(text, from, to)`** - Replaces all occurrences of `from` with `to`.
`from` may be a literal string or a `regex` value (see *Regular Expressions*):
```basic
replace("hello", "l", "x")     ' "hexxo"
replace("hello world", "o", "0")  ' "hell0 w0rld"
replace("a1b2", regex("[0-9]"), "#")  ' "a#b#"
```

**`starts_with(text, prefix)`** - Returns `true` if text starts with prefix:
```basic
starts_with("hello", "he")     ' true
starts_with("hello", "lo")     ' false
```

**`ends_with(text, suffix)`** - Returns `true` if text ends with suffix:
```basic
ends_with("hello", "lo")       ' true
ends_with("hello", "he")       ' false
```

**`repeat(text, count)`** - Repeats text count times:
```basic
repeat("ha", 3)                ' "hahaha"
repeat("x", 0)                 ' ""
```

**`chr(code)`** - Returns the string for a Unicode **codepoint** in the range
`0 .. 0x10FFFF` (excluding the surrogate range `0xD800..0xDFFF`), UTF-8 encoded.
`chr(0)` produces a one-byte binary-safe NUL string (gBASIC strings are
binary-safe — see *Strings and Unicode*):
```basic
chr(110)                       ' "n"
chr(233)                       ' "é"  (one codepoint, 2 UTF-8 bytes)
chr(128512)                    ' "😀" (one codepoint, 4 UTF-8 bytes)
chr(0)                         ' a one-byte NUL string
```

**`code(text)`** - Returns the **codepoint** value of the first character of a
non-empty string; the inverse of `chr`:
```basic
code("n")                      ' 110
code("é")                      ' 233
code(chr(128512))             ' 128512
```

**`byte_count(text)`** - Number of raw bytes in a string (`len` counts
codepoints):
```basic
byte_count("café")             ' 5   (len("café") is 4)
```

**`byte_at(text, index)`** - The byte (0–255) at a 0-based byte index:
```basic
byte_at("ABC", 0)              ' 65
```

**`from_bytes(numbers)`** - Builds a binary-safe string from an array of byte
values `0..255`:
```basic
from_bytes([72, 105])          ' "Hi"
from_bytes([0, 255])           ' a two-byte binary string
```

### Regular Expressions

Always available — the engine is the POSIX ERE support in libc, so there is no
optional dependency and no `load`. Design: `docs/text_design.md`.

**`regex(pattern [, flags])`** - Compiles a pattern into an immutable regex
value. Compiling once and reusing it is how you keep a pattern out of a loop's
inner cost:
```basic
digits = regex("[0-9]+")
print(contains("order 1500", digits))    ' true
```
Flags are a short string: `"i"` ignore case, `"m"` `^`/`$` match at line
boundaries, `"s"` `.` matches a newline. They are independent, and unknown
letters raise rather than being ignored.

**`match(text, pattern [, flags])`** - The first match as a record, or `unknown`
if there is none. **It scans** — it is Python's `re.search`, not `re.match`;
anchor with `^` if you want the start of the string:
```basic
m = match("order 1500 shipped", "[0-9]+")
print(m.text)                            ' "1500"
print(mid("order 1500 shipped", m.start, m.length))   ' "1500"
print(is_unknown(match("none", "[0-9]+")))            ' true
```
The record is `{text, start, length, groups}`. `start` and `length` are
**codepoint** measures, matching `find`, so they compose with `mid`/`left`/
`right`. The field is `length` rather than `end` because `end` is a reserved
word. `groups` is always a list (empty when the pattern has no captures); a
group that did not participate is `unknown`, distinct from one that matched the
empty string.

**`match_all(text, pattern [, flags])`** - Every non-overlapping match, left to
right, as a list of the same records:
```basic
for each m in match_all("a1 b22 c333", "[0-9]+")
    print(m.text)                        ' 1, 22, 333
end for
```

**Overloads.** Three verbs that already take a literal also accept a `regex`
value, and mean the pattern version when given one. A plain string argument
always stays **literal**:
```basic
contains("hello", "ell")                 ' true  (literal substring)
contains("hello", regex("h.llo"))        ' true  (pattern)
contains("hello", "h.llo")               ' false (literal — no dot in "hello")
split("a1b22c", regex("[0-9]+"))         ' ["a", "b", "c"]
```
`find` is **not** overloaded: it returns a single index, which cannot carry a
match record, so `match` exists under its own name instead.

**Supported syntax.** POSIX Extended Regular Expressions, plus a translation of
`\d \D \w \W \s \S` to their POSIX classes. Not supported: `\b` word
boundaries, lookaround, backreferences within a pattern, non-greedy
quantifiers, and named groups. `\D`, `\W` and `\S` are rejected *inside* a
`[...]` bracket expression, because POSIX offers no negated class there.

A pattern may not contain an interior NUL byte (`regcomp` cannot honor one, so
it raises rather than silently truncating). The **subject** has no such limit:
matching is binary-safe and searches the full byte length.

### Record Helpers

**`merge(a, b, …)`** — one new record with the fields of each. Variadic, one
argument or more; every argument must be a record; **later wins** on a
duplicate key, so `merge(defaults, overrides)` reads the way it looks. Shallow:
a nested record is copied, not merged into. The sources are untouched, which is
what makes it the answer to composing onto a library's return value:

```basic
response = merge(web.static(rel, root), { id: req.id })
```

There is no record `+`, for the same reason there is no array `+`.

**`keys(record)`** - Returns array of key strings:
```basic
keys({x:1, y:2})               ' ["x", "y"]
keys({})                       ' []
```

**`values(record)`** - Returns array of record values:
```basic
values({x:1, y:2})             ' [1, 2]
values({})                     ' []
```

**`has(record, key)`** - Returns `true` if record contains key:
```basic
has({x:1, y:2}, "x")           ' true
has({x:1, y:2}, "z")           ' false
```

**`remove_key(record, key)`** - Returns new record without the key (immutable):
```basic
rec = {x:1, y:2}
new_rec = remove_key(rec, "x")  ' {y:2}
' rec is unchanged: {x:1, y:2}
remove_key(rec, "z")            ' {x:1, y:2} (copy when key missing)
```

### Counting

**`count(value)`** - Returns count/length for strings, arrays, and records:
```basic
count("hello")                 ' 5 (string length)
count([1, 2, 3])               ' 3 (array elements)
count({x:1, y:2})              ' 2 (record fields)
count("")                      ' 0
count([])                      ' 0
count({})                      ' 0
```

### Core vs Library Functions

**Core functions** (`string`, `number`, `type`, etc.) are always available and don't require `load`.

**Conversion function differences:**
- `string(value)` - canonical string conversion for any value
- `encode(value)` - gBASIC's **JSON-like dialect** for structured data (see below)
- `decode(text)` - parses JSON *and* the dialect back into values; **raises** on
  malformed input
- `try_decode(text)` - the same parse, reporting failure as a **value** instead of
  raising (see below)
- `json_encode(value)` - **standards-compliant JSON (RFC 8259)** for external
  interchange: HTTP APIs, LLM providers, anything outside gBASIC
- `json_encodable(value)` - preflight predicate: would `json_encode` succeed?
- `quote(value)` - gBASIC source code literal with escaping
- `serialize(value)` - exact binary round-trip serialization (see below)
- `deserialize(bytes)` - reconstruct a value from `serialize` output

### Three serializers, three jobs

| Want | Use | Notes |
| --- | --- | --- |
| Send data to a non-gBASIC consumer | `json_encode` | Strict RFC 8259; refuses what JSON can't express |
| Human-readable gBASIC round-trip | `encode` / `decode` | Dialect: `nothing`/`unknown` survive |
| Exact typed/binary round-trip | `serialize` / `deserialize` | Dates, money, files, binary content |

**`encode` is a dialect, not standard JSON.** It writes gBASIC's spellings for the
empty values — `nothing` and `unknown` rather than `null` — and for the non-finite
numbers — `inf`, `-inf`, `nan`, `-nan`, the same text `print` and `string` show.
`decode` accepts all of them, so a value survives a gBASIC round-trip. No other
JSON parser does, so **never put `encode` output on the wire**; use `json_encode`.

Until 0.1.0-rc7 `decode` refused the non-finite spellings its own `encode`
produced, so a program could write a file it could not read back — and ordinary
overflow reaches that state quietly (`number("1e308") * 10` is infinity, with no
diagnostic). The dialect's one promise is an exact gBASIC-to-gBASIC round trip,
and it now holds for every number a program can hold.

The **wire** parser is unaffected and stays strict: a JSON request or response
body cannot carry `inf`, `nan`, `nothing` or `unknown`, because RFC 8259 has no
syntax for them. `json_encode` refuses non-finite values for the same reason,
and `json_encodable` answers `false` for them.

**`try_decode(text)` — decode that cannot raise.** `decode` raises on malformed
input, and gBASIC has no way to catch a raise, so any program reading a file it did
not write has to decide what to do *before* parsing. `try_decode` answers with a
record instead:

```basic
r = try_decode(text)
if r.ok then
    settings = r.value
else
    print to error "settings.json: " + r.message + " (line " + r.line + ")"
    settings = defaults()
end if
```

| field | on success | on failure |
| --- | --- | --- |
| `ok` | `true` | `false` |
| `value` | the decoded value | `nothing` |
| `message` | `""` | why it failed, and where (e.g. `expected ',' or '}' at byte 6`) |
| `offset` | `0` | 0-based **byte** offset of the failure |
| `line`, `column` | `0` | 1-based position, for a human reading a log |

It shares `decode`'s parser, so the two accept exactly the same dialect and
diagnose any given input identically — `try_decode` reports the same text `decode`
raises. `decode` itself is unchanged.

A *non-string* argument still raises (`try_decode expects a string`): that is a bug
in the caller, not malformed data. `try_decode` reports on the **content** of a
string; it is not a type-checking wrapper.

**Nesting is bounded.** Both entry points refuse documents nested deeper than
10 000 levels — `decode` raises, `try_decode` reports. Before this limit existed,
the parser's recursion overran the C stack and segfaulted at around 45 000 levels;
a non-raising decode whose failure mode is a crash would be worthless.

**Why it matters for performance.** Pre-validating in gBASIC is not merely
inconvenient, it is quadratic: `mid(s, i, 1)` is O(i) on codepoint-indexed strings,
so a per-character scan is O(n²). Measured — 16 KB: 1 s; 64 KB: 16 s; 128 KB: 69 s;
256 KB: 291 s. The C parser handles all of those in well under a second, so
`try_decode` replaces a scan that got dramatically worse with size.

**`json_encode(value)` — strict JSON.** Type mapping:

| gBASIC | JSON |
| --- | --- |
| `nothing` | `null` |
| boolean | `true` / `false` |
| number (finite) | number |
| string | string (control chars escaped `\u00XX`, UTF-8 preserved) |
| array | array |
| record | object |
| `unknown` | **refused** |
| NaN / ±infinity | **refused** |
| dates, money, durations, files, functions, live handles | **refused** |

Refusals RAISE rather than coerce: silently turning a value into something it is
not is worse than a clear failure. `nothing` and `unknown` are deliberately NOT
merged — `null` means "no value", while `unknown` is gBASIC's NA, "value not
known", which JSON cannot express. Omit the field or convert it explicitly.

A raise here *is* catchable — `on error` has been frame-scoped since PLAT-ERR
(see `docs/ai/ERRORS.md`) — but preflighting with **`json_encodable(value)`**,
a side-effect-free predicate with the same rules, still reads better than
recovering afterwards, because it lets you branch instead of unwind:

```basic
if json_encodable(payload) then
    body = json_encode(payload)
end if
```

`json_encodable` is distinct from `reflect.serializable`, which mirrors the binary
`serialize` and accepts dates, money and functions that have no JSON form.

Cycles cannot occur: gBASIC values are acyclic under copy semantics (`r.self = r`
stores a snapshot, not a reference), so strict encoding needs no cycle detection.
A depth guard is kept as cheap insurance.

**`serialize` / `deserialize` vs `encode` / `decode`.** `encode`/`decode` use the
JSON-like dialect above — human-readable, and it refuses gBASIC's typed values
outright (a date or money raises, it does not degrade to a string or number).
`serialize` produces an opaque **binary-safe string** that
`deserialize` turns back into an *exact* copy, preserving type and binary content
(including interior NUL bytes), across numbers, strings, booleans, `nothing`,
`unknown`, arrays, records (nested), dates/times, durations, money, and file/
directory references:

```basic
cost{USD}= 9.99
deserialize(serialize({when: now(), cost: cost}))   ' exact copy, types intact
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

- `concat(a, b, …)` — one new array with the elements of each, in order.
  Variadic, one argument or more; every argument must be an array; the sources
  are untouched. There is no array `+`: whether that should concatenate or add
  element-wise is a separate decision, not one to settle by adding a
  convenience.

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

#### Array value semantics

Arrays are **value types**. Assigning one array to another, passing an array to
a function, returning one, or storing one in a record all produce an independent
array — mutating one never affects any other:

```basic
a = [1, 2, 3]
b = a
b[0] = 99          ' a is still [1, 2, 3]; b is [99, 2, 3]
```

This holds all the way down: nested arrays, arrays of records, and arrays inside
records are each independent after a copy (`b[i][j] = x`, `b[i].field = x`, and
`r2.rows[i] = x` never disturb the original).

Internally arrays use **copy-on-write** so these guarantees are cheap: a copy
shares one reference-counted backing store until the first mutation, which then
detaches a private copy. This is purely an implementation optimization — it
changes performance, never observable behavior. Practical consequences:

- assignment, argument passing, and returning an array are O(1);
- reading an element (`a[i]`) is O(1);
- writing an element (`a[i] = x`) is O(1) for an unmodified array, and O(n) the
  first time you write to an array that is still sharing storage with a copy;
- building an array with a loop of `append` is O(n) overall (amortized O(1) per
  append), not O(n²).

`append`/`prepend`/`insert`/`remove`/etc. still mutate a stored array in place
when given an assignable path (and notify watchers, as above); the value they
return is the resulting array. Taking a copy first (`b = a`) and then appending
to `a` leaves `b` unchanged, as value semantics require.

### Bitwise

Bitwise functions operate on 32-bit unsigned integers and raise on non-integer
or out-of-range input:

- `band(a, b)` / `bor(a, b)` / `bxor(a, b)` — bitwise AND / OR / XOR.
- `bnot(a)` — bitwise NOT (32-bit complement).
- `shl(value, count)` / `shr(value, count)` — logical left / right shift.
- `rotl(value, count)` / `rotr(value, count)` — 32-bit rotate left / right.

### Numbers and Comparison

- `mod(a, b)` — the remainder, **floored**: the result takes the sign of the
  DIVISOR, so `mod(-7, 3)` is `2` and `mod(7, -3)` is `-2`. This differs from
  QBasic's `MOD`, which truncates toward zero, and the divergence is
  deliberate: gBASIC had no modulo, so the documented workaround was
  `a - floor(a/b)*b` — floored — and the libraries written against that advice
  depend on it (`stdlib/forensics.bas`'s civil-date algorithm is correct for
  negative years only under floored semantics). `mod(a, 0)` raises. There is no
  infix `%`; and `7 mod 2` is duration syntax, not modulo — `mod` is a call.
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
label{string}= count
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
- `read(f)` / `write(f, text)` / `append(f, text)` — whole-file I/O.
  **Binary-safe in both directions** (*since 0.1.0-rc9*): a gBASIC string may
  hold any byte including NUL, and `read` returns the file's full length rather
  than stopping at the first one. Before rc9 `write` was binary-safe and `read`
  was not, so a file holding binary — a `serialize` payload, an image — could
  be written and silently read back short.
- `bytes(f)` / `lines(f)` / `chars(f)` — the file's **size** in bytes, its line count, its character count. These are counts, not content; use `read` for the content.
- `lock(f)` / `unlock(f)` — advisory locks (see `with lock`).

Listing a directory — two functions, and the difference matters:

- `list_files(path)` — the **files** in one directory, as full paths. It does
  **not** recurse, and it does **not** report subdirectories at all, so it cannot
  by itself drive a walk: a directory is invisible to it.
- `list(d)` — every entry of a **directory reference**, as `{name, type}` records
  where `type` is `"file"` or `"folder"`. `name` is the bare entry name, not a
  path. This is the one to build a recursive walk on, because it is the one that
  can see a subdirectory:

  ```basic
  pending = [root]
  files = []
  while len(pending) > 0
      here = take_last(pending)
      d {dir} = here
      for each e in list(d)
          full = here + "/" + e.name
          if e.type = "folder" then
              pending = append(pending, full)
          else
              files = append(files, full)
          end if
      end for
  end while
  ```

  Order is the filesystem's, not sorted — `sort` it if you need a stable one.
  `stdlib/filetree.bas` is this walk with expand/collapse state on top.

File metadata and atomic replacement:

- `file_size(f)` — the file's size in bytes as a number, from `stat`
  (metadata only, not a read). This is the binary byte count, so it counts
  embedded NULs and is unaffected by text length; a zero-byte file returns `0`.
  Numbers hold integers exactly up to 2⁵³ (8 PiB), well beyond any real file, so
  there is no practical truncation. Requires a file reference. A directory has
  no content size and raises; a missing path raises with the OS reason.
- `file_mtime(f)` — the file's last-modification time as a `datetime`, from
  `stat`, in local time. **Second resolution:** gBASIC's `datetime` has no
  sub-second field, so any nanosecond part of the filesystem timestamp is
  dropped rather than fabricated. Requires a file reference; works for
  directories too (a directory's mtime is a real change signal). A missing path
  raises with the OS reason. There is no locale/string coercion — the value is a
  `datetime`, comparable and lens-able like any other.
- `atomic_replace(temp, dest)` — atomically move `temp` onto `dest` with a
  single `rename(2)`. On one filesystem this replaces an existing `dest`
  atomically: a concurrent reader that re-opens `dest` sees either the whole old
  file or the whole new file, never a partial write. It is **not** copy+delete.
  Both arguments may be file references or path strings.
  - **Same-filesystem requirement.** Unlike `move`, it does *not* fall back to
    copy+delete across filesystems: a cross-device attempt fails cleanly (the
    error names the same-filesystem requirement) rather than silently degrading
    to a non-atomic sequence.
  - **Failure safety.** On any failure `rename(2)` leaves *both* `temp` and
    `dest` untouched, so a failed replace never destroys the original `dest`.
  - **Atomicity vs. durability.** This guarantees atomic *visibility*, not
    crash *durability*: surviving a power loss would additionally require
    `fsync` of the file and its containing directory, which this primitive does
    not perform.
  - **Relationship to `move`.** `move(src, dest)` is unchanged: it still renames
    on the same filesystem and *does* fall back to copy+delete across
    filesystems (so it can cross devices, non-atomically). Use `atomic_replace`
    for the safe-write pattern (write a temp, then atomically swap it in) where
    an all-or-nothing replacement matters; use `move` when you want a best-effort
    relocation that may cross filesystems.

Path questions about what is really there:

- `real_path(p)` — the canonical absolute path: `.` and `..` removed, every
  symlink resolved by the kernel. Returns `unknown` when the path does not
  exist (all components must exist, as for `realpath(3)`); to canonicalize a
  path you are about to create, resolve `directory_name(p)` instead. A path
  containing an interior NUL byte is refused rather than silently truncated,
  since it would then be *checked* as one path and *opened* as another.
  - This is the primitive a containment check needs. Testing whether a
    client-supplied path "starts with" a root can be defeated by `..` and
    cannot see a symlink at all; testing the **resolved** path cannot. Resolve
    first, compare second, and compare on a separator boundary so a root of
    `/srv/pub` does not match `/srv/public-secret`.
**A keyword may be a field name** (*since 0.1.0-rc6*), both as a record-literal
key and after a dot: `r = { end: 1, on: 2 }` then `r.end`, `r.on`. A field name
is a closed context — nothing but a name can appear before `:` in a literal or
after `.` — so the keyword namespace no longer reaches into the data namespace.
Before this, a record could hold a field the dot form could never read.

**A raise inside a watcher body stops the drain** (*since 0.1.0-rc5*). It used
to be dropped: draining continued, the program ran on with a watcher that had
not fired, and the diagnostic surfaced only at exit.

**A typed-value modifier raises when it cannot construct the value** (*since
0.1.0-rc5*): `d{date} = "not-a-date"`, a `time` / `datetime` that will not
parse, or a `file` / `dir` given something that is not a path string.
`error.source` is `"datetime"` for the three temporal ones and `"modifier"` for
the two path ones, matching `USD`, which always raised. Until 0.1.0-rc5 these
five printed an unlocated line and assigned `nothing` with the process exiting
0 — so a bad date silently became a `nothing` that flowed onward.

**Indexing out of range raises**, on read as well as on write (*since
0.1.0-rc5*; `error.source` is `"indexing"` for a read and `"assignment"` for a
write). Until then a read printed an unlocated line and yielded `nothing` while
the process still exited 0 — and since `nothing` is a legitimate value, the
failure was indistinguishable from a real one. Use `count(a)` to test a bound,
or catch it with `on error goto next`.

- `file_type(p)` — `"file"`, `"folder"` or `"other"` (a device, socket, FIFO),
  or `unknown` when nothing is there. Follows symlinks. This is the only way to
  ask whether a path is a directory **without raising**: `file_size` on a
  directory raises, and when this was added a raise could not be caught, so
  code holding an untrusted path had no safe way to ask at all. Catching is
  possible now (see Errors), but asking remains the better shape — the answer
  is a fact about the path, not an exception.

<!--needs-context-->
```basic
root = real_path("public")
target = real_path(root + "/" + supplied)
if is_unknown(target) then
    ' nothing there
else if not starts_with(target, root + "/") then
    ' it resolved outside the root -- refuse
else if file_type(target) != "file" then
    ' a directory or a device, not a page
else
    ' safe to serve
end if
```

`else if` chains close with a **single** `end if`, and are exactly the nested
form desugared — `--ast` shows the nesting. When every branch tests the *same
subject*, `consider` says so once instead of repeating it:

```basic
consider file_type(target)
if "file" then
    ' serve it
if "folder" then
    ' a directory listing, maybe
else
    ' a device or a socket -- refuse
end consider
```

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

- `dates` — business calendars as data (`dates.calendar`, `is_business_day`,
  `add_business_days`, `business_days_between` over `(a, b]`), calendar merging
  as a union of constraints (`dates.merge`), calendar differences
  (`dates.between`), and the date-expression verbs `matches`/`select`/`series`
  over one spec-record vocabulary ("third Thursday of the month", "first
  business day before a deadline", "every 2 weeks rolled off holidays").
  `dates.dayname(d)` returns a weekday name, but prefer the core field
  `d.dayname`, which is what it now delegates to.
  Design: `docs/datetime_design.md`; worked recipes:
  `docs/datetime_cookbook.md`.
- `schedule` — packing events into working days: `schedule.slots` (appointment
  grids) and `schedule.layout` (ordered sessions into business days around
  immovable breaks, with the unplaceable reported by name). Same two documents.
- `matrix` — minimal vector/matrix primitives (`docs/statistics_design.md` §8).
- `frame` — a structural data-frame layer (`docs/statistics_design.md` §4).
- `stats` — higher-level statistical compositions built on `matrix`/`frame`
  (`docs/statistics_design.md`; worked walk-throughs in
  `docs/cookbook_social_behavioral.md` and `docs/cookbook_econometrics_finance.md`).
- `crypto` — ergonomic cryptography compositions over the crypto builtins
  (see [Cryptography](#cryptography) and `docs/crypto_design.md`).
- `stats` also carries **exploratory factor analysis** —
  `factor_analysis(cols, spec)`, principal-axis factoring with iterated
  communalities and an optional varimax rotation. **It is not PCA**, and the
  difference is the point: PCA summarises the observed variables and explains
  *total* variance, while factor analysis posits latent causes and explains
  *common* variance only — in the arithmetic, 1s versus communalities down the
  diagonal. On half-noise data that gap is 0.40 against 0.60, so using PCA
  where a latent construct is meant overstates by half. Rotation **cannot
  improve fit** (it reproduces the same communalities exactly; it only relabels
  the axes), and a communality reaching 1 is a Heywood case, reported rather
  than clamped.

- `stats` also carries **survival analysis** — `kaplan_meier(times, events)`
  with Greenwood standard errors and confidence bands, `survival_at(km, t)`,
  and `logrank` for comparing two groups. Time-to-event where for some subjects
  the event has not happened *yet*: medicine, reliability engineering, churn,
  credit default. **Censoring is not optional and not inferable** — every
  function takes durations *with* an event indicator, because dropping censored
  subjects understates survival (only failures remain) and counting them as
  events understates it differently (failures that never happened); on the
  standard leukaemia trial those two shortcuts give medians of 10 and 16 where
  the answer is 23. A subject censored at time *t* counts as at risk for the
  event at *t*, which is a real convention and a stated one. `median` is
  `unknown` when the curve never reaches 0.5, rather than the largest observed
  time.
  **`cox_ph(times, events, covars)`** models it rather than describing it,
  fitting the partial likelihood so the baseline hazard never has to be
  estimated, and returning hazard ratios with errors, z, p and intervals.
  A hazard ratio is **per unit**, so a covariate in dollars yields 1.0000-something
  and reads as nothing — `hr_per(fit, i, delta)` gives the ratio over a stated
  interval without re-fitting. Ties use Breslow's approximation, stated rather
  than silent. And the model assumes the ratio is **constant over time**: where
  hazards cross, one number describes neither period, which no p-value reveals
  and the Kaplan-Meier curves do.

- `stats` also carries **meta-analysis** — `meta_analysis(studies, spec)` with
  fixed-effect (inverse-variance) and random-effects (DerSimonian–Laird)
  pooling, always reported beside the heterogeneity (Cochran's Q, I², τ²),
  plus `smd_variance` to turn a reported *d* into the variance pooling needs
  and `eggers_test` for funnel asymmetry. **Ratio measures pool on the log
  scale** — an odds, risk or hazard ratio is multiplicative, so 0.5 and 2.0 are
  the same effect in opposite directions and averaging them as plain numbers
  gives 1.25, which reads as a 25% harm where the truth is none. Pass
  `scale: "ratio"` and the estimate and interval are back-transformed; nothing
  guesses, because a set of ratios and a set of raw differences are both just
  numbers.

- `stats` also carries **event studies** — `event_window(dates, event, pre,
  post)`, `abnormal_returns(asset_r, market_r, spec)` and
  `event_study(studies)`. The standard tool of empirical finance, and what
  turns an EDGAR filing date into a testable claim: fit a normal-return model
  before an event, measure the residual across it, aggregate to CAAR and test
  it. Windows count **trading days** (they index the dates the series has, so a
  weekend cannot shorten one); an event on a closed day moves to the next
  trading day and says so; an estimation window overlapping its event window is
  refused, as is a CAAR over unequal windows; and clustered events whose
  baselines contain each other are **reported** (`contaminated`, `note`) rather
  than refused, because clustering is sometimes unavoidable.

- `stats` also carries **causal inference** — `did(y, treated, post, spec)` and
  `pre_trends(y, treated, period, treat_start)` for difference-in-differences,
  and `iv_2sls(y, endog, instruments, spec)` for instrumental variables. Both
  estimators share a failure mode that no amount of eyeballing catches: the
  **coefficient is right and the standard error is wrong**, so the output looks
  like every other regression.
  * 2SLS run as two ordinary regressions — fit *x* on *z*, then *y* on *x̂* —
    gives the identical point estimate and measures its residuals against *x̂*.
    The model's residuals are `y - X*beta`, against the **original** *x*. On two
    datasets differing only in the *sign* of the confounding, the naive standard
    error comes out 1.78× too large and 2.7× too small. It is not conservative,
    and which way it errs depends on something unobservable.
  * A difference-in-differences on serially correlated panel data understates
    its own uncertainty (Bertrand, Duflo & Mullainathan 2004). Pass `cluster:`
    — a cluster id per row — for CR1 errors on *G*−1 df. In the test panel the
    conventional error is 3.2× too small: *p* < 0.001 becomes *p* > 0.10 on the
    same estimate. `hc:` takes `"HC0"`..`"HC3"` instead.
  * With no covariates the DiD coefficient **is** the four-cell arithmetic, and
    `means` / `diff_in_means` / `saturated` say so, so the estimate can be
    checked by hand. Indicators must be 0/1 or `true`/`false`; anything else is
    refused rather than coerced, and an empty cell is named.
  * `iv_2sls` reports `first_stage` (the F on the **excluded** instruments —
    below ~10 the instrument is weak, the estimate is biased toward OLS, and
    `weak` / `note` say so), `sargan` (only where there is something to test:
    exact identification has no overidentifying restriction, and reporting one
    would be reporting a tautology as evidence) and `wu_hausman` (whether the
    regressor was endogenous at all). Too few instruments is refused **by
    count**, not silently under-identified.
  * **Parallel trends is an assumption and `did` does not test it**, because it
    cannot be: it is a claim about what the treated group *would* have done.
    `pre_trends` tests the different and weaker question of whether the groups
    moved together *before* treatment, and its `note` says in words that a large
    *p*-value there is the absence of evidence against parallel trends over
    however many pre-periods the data happens to hold — not evidence for it.

- `web` — a route table as data over the WebServer module: `{method, path,
  handler}` records validated at build time, `{id}`/`{rest...}` patterns
  captured into `req.params`, order-independent matching by specificity, and
  `web.dispatch` returning a response record the server takes verbatim
  (`docs/web_routing.md`). `web.content_type(name)` maps a filename extension
  to a content type — deliberately a short list of what a site actually serves
  rather than an exhaustive MIME table.
- `market` — daily price history as a frame (`market.daily(m, symbol, from, to)`
  → `{ok, frame, adjusted, message}`), which is the shape the rest of the stack
  already wants: `forensics` indexes it by column, and `frame["close"]` is the
  flat array `stats.simple_returns` takes. Providers are pluggable (Stooq needs
  no key; Tiingo is adjusted), with the `offline`/`with_transport` seams
  `llm` and `edgar` use, so tests never touch the network. **Tiingo is verified live (2026-08-26)** against a real
  free-tier key. **Keyless data is the problem**: Stooq now serves a JavaScript
  anti-bot challenge to any HTTP client and Yahoo's chart endpoint answered
  429, so a keyed provider is the reliable path. Where a provider serves an
  adjusted series it is used for **every** price column, not just the close —
  mixing an adjusted close with raw highs and lows puts the columns on
  different scales, which on real data made the close fall below its own low on
  89 of 124 rows. A challenge
  page or a rate limit is reported as such rather than as an empty result. Rows are always
  sorted ascending by date — a reversed series yields **negated** returns, which
  looks like ordinary data — and `adjusted` reports what the provider actually
  supplies rather than being assumed, because returns computed across a split
  from unadjusted prices read as a −50% day.
  `market.closes(m, symbol, from, to)` is the convenience the stats verbs
  actually want — just the closing prices as a flat array — and
  `market.with_timeout(m, seconds)` sets the request timeout, alongside the
  `with_transport`/`offline` seams.
- `finance` — the time value of money, pure gBASIC. Amounts are `money` values
  and rates are plain numbers **per period**: a 6% annual loan paid monthly is
  `0.06 / 12`, and making that the caller's arithmetic rather than a hidden
  convention is deliberate, since compounding conventions vary by product and
  jurisdiction and a library that guessed would be wrong somewhere without
  saying so. Sign follows the spreadsheet convention — money received is
  positive, money paid is negative — because that is what the answer will be
  checked against.
  `finance.pmt(principal, rate, periods)` is the payment that repays a loan,
  `finance.pv(payment, rate, periods)` what a stream is worth today,
  `finance.fv(amount, rate, periods)` what an amount grows to, and
  `finance.nper(principal, payment, rate)` how many periods clear it.
  `finance.npv(rate, flows)` discounts an array of money one period apart, and
  `finance.irr(outlay, flows)` finds the rate that breaks even — by bisection,
  which cannot diverge, because a wrong IRR is a plausible percentage someone
  would act on. `finance.schedule(principal, rate, periods)` returns one record
  per period (`period`, `payment`, `interest`, `principal`, `balance`) whose
  **final payment is adjusted so the balance lands exactly on zero**: every
  payment is whole minor units, those roundings accumulate, and a schedule
  using one figure throughout would end owing a few cents. Depreciation is
  `finance.sln(cost, salvage, life)` straight-line,
  `finance.syd(cost, salvage, life, period)` sum-of-years-digits, and
  `finance.ddb(cost, salvage, life, period)` double-declining balance, floored
  at salvage so an asset is never written below what it is worth.
- `chart` — charts as deterministic SVG text, pure gBASIC: line, scatter, area,
  bar, histogram, pie, heatmap and sparkline (`docs/chart_design.md`; worked
  recipes in `docs/chart_cookbook.md`). `chart.area_xy(xs, ys)` and
  `chart.bar_xy(categories, values)` are the escape hatch from the frame API —
  plot two plain arrays without building a frame first.
- `persist` — crash-safe versioned persistence: an atomic temp-file-and-rename
  write, and a read that reports missing/corrupt/loaded as a value rather than
  raising (`docs/ai/COOKBOOK.md`). Also `persist.write_text_atomic(path, text)`
  for raw text through the same temp-then-rename dance (source files rather than
  records), and `persist.ensure_dir(path)`, which creates a path and its missing
  parents and is **idempotent** — bare `make_dir` raises on a directory that
  already exists.
- `filetree` — a directory as a value tree, with expand/collapse state carried
  in the nodes. `filetree.visible_count(nodes)` counts currently-visible rows,
  and `filetree.dump(nodes)` renders the visible tree as deterministic,
  **path-free** text (`v ` expanded dir, `> ` collapsed, two spaces per depth) —
  path-free so a golden does not encode the machine it ran on.
- `matrix` — vector and matrix primitives over arrays of row arrays:
  `mat_rows`, `mat_cols`, `mat_identity`, `mat_transpose`, `mat_mul`,
  `mat_vec`, `mat_inverse`, `vec_dot`. Shape mismatches and singular matrices
  return `unknown` rather than a truncated result — which is how every
  regression in `stats` decides whether a design is estimable. Full table:
  `docs/statistics_design.md` §8b.
- `grid` — a messy worksheet turned into clean frames: `grid.tables` guesses and
  reports a confidence with its reasons, `grid.extract` follows a spec record
  whose anchors match by CONTENT so inserting rows cannot break it.
  `grid.total_pattern()` returns the regex used to recognise a totals row —
  exposed rather than hidden precisely so a caller can see and change what is
  being trusted — and `grid.row_is_blank(g, r)` is the emptiness test the
  detector uses. Design: `docs/xlsx_design.md` §5.
- `consolidate` — many differently-shaped sources merged onto one schema
  (`docs/xlsx_design.md` §6). The value coercions are public because a caller
  often needs them alone: `to_money(v)` accepts the union a report actually
  uses (`$1,500.00`, `(1,200.00)`, `1,200.00-`, `9,000`) and yields `unknown`
  rather than 0 for anything else; `to_number(v)`, `to_text(v)`, and
  `to_percent(v, scale)`, which returns a **fraction** (0.0525 for 5.25%).
  `infer_percent_scale(values)` judges a whole column at once, because a single
  cell cannot distinguish 5.25 from 0.0525 and a column can.
  `normalize_name(s)` is the fuzzy header matcher (case, whitespace and
  punctuation removed), exposed so an alias list can be checked by hand.
- `dbframe` — a frame becomes a database table (`docs/xlsx_design.md` §7).
  `column_type(values)` decides one column's SQL type from EVERY value, never
  the first, and `safe_name(s)` is the identifier validator — identifiers cannot
  be bound as parameters, so they are **refused rather than escaped**.
- `ari` — the anchor-relative report parser (`docs/ari_spec_language.md`).
  `ari_parse_spec(spec_text)` parses a spec on its own, and
  `clean_grid(report_text, spec_text)` exposes the page-furniture pass
  independently of any spec — useful, and testable, without parsing anything.
- `llm` — a chat-completion client (`docs/llm_design.md`).
- `gui` — the declarative layer for the experimental GTK 3 `gui` module
  (`docs/gui_design.md`).
- `gtk` — thin ergonomic GTK 4 constructors over the `gi` bridge (see
  [GTK 4 helpers and SourceEditor](#gtk-4-helpers-and-sourceeditor)).
- `sourceeditor` — a reusable GtkSourceView 5 source/text editor (same section).
- `gtkui` — a dynamic declarative widget-tree reconciler over `gi` (state → record
  tree → in-place GTK 4 mutation; see [gtkui](#gtkui--declarative-widget-tree-reconciler-load-gtkui)).

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

Flush stdout at every completed line:

```sh
gbasic --line-buffered FILE
```

### Why `--line-buffered` exists

Nothing about gBASIC decides when your printed bytes leave the process — C stdio
does, and it decides by looking at what stdout is connected to:

| stdout is | stdio mode | what a reader sees |
|---|---|---|
| a terminal | line buffered | each line as it is printed |
| a pipe or a file | **block buffered** | nothing until ~4 KB accumulate, or exit |

So a program that behaves perfectly at a prompt appears to hang the moment you
pipe it — `gbasic prog.bas | less`, a log collector, an editor or supervisor
reading the program it just started. Worse, block-buffered output is *lost* if
the program is killed rather than allowed to exit: nothing runs stdio's cleanup
for a signalled process, so whatever was still in the buffer never existed as far
as the reader is concerned.

`--line-buffered` switches stdout to line buffering (`_IOLBF`) regardless of what
it is connected to, so every completed line is flushed as it is printed.

This is complete for gBASIC's output surface rather than merely helpful: every
`print` this runtime emits ends in a newline, so line buffering flushes every one
of them, and the single construct that writes a partial line — an `input` prompt —
is already flushed explicitly by the interpreter and was never affected. Unbuffered
mode (`_IONBF`) would therefore deliver nothing extra while splitting one `print`
into up to 18 `write` calls (an array print emits its brackets, elements and
separators separately); line buffering is exactly one `write` per line.

Properties worth relying on:

- **Opt-in.** No other flag implies it and it implies nothing. Without it, output
  behavior is byte-for-byte and timing-for-timing what it has always been.
- **Orthogonal.** Combine it with any other flag, in either order:
  `gbasic --json-diagnostics --line-buffered FILE` and
  `gbasic --line-buffered --json-diagnostics FILE` are the same run. It touches
  stdout buffering only; the diagnostic stream is unchanged.
- **Interpreter-side.** It must come *before* `FILE`. A flag-looking argument after
  `FILE` is a program argument: `gbasic prog.bas --line-buffered` passes the text
  through to `program main(args)`.
- **Nothing changes about the bytes.** Same content, same order, same total — only
  the moment they leave the process.
- **stderr needs nothing.** C requires stderr never to be fully buffered, glibc
  makes it unbuffered, and the interpreter never calls `setvbuf` on it, so
  diagnostics already appear as they are written. That covers a program's own
  `print to error` too — it is prompt whether or not this flag is set, so the flag
  changes the *relative* order of the two streams at a shared destination and
  nothing about stderr itself.

The cost is one `write` syscall per line instead of one per ~4 KB. On a program
that does nothing but print, that measures at roughly +60% wall clock
(200 000 lines piped: 0.149 s → 0.243 s, i.e. ~1.3 M lines/s → ~0.8 M lines/s); on
any program that also computes, it is not observable. Leave it off for bulk output
you are redirecting to a file, turn it on whenever something is reading along.

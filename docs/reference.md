# gBASIC Reference

This reference describes the implemented v0.1 language surface. gBASIC is experimental; behavior may change before a stable release.

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

Watchers:

```basic
watch(a, b)
    c = a + b
end watch

without watchers
    a = 10
end without
```

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

No f-string syntax is implemented.

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

## Modifiers

Modifier use:

```basic
x(USD)= 19.95
name(caseless)= "joe"
a(rounded 2)= b
a(math.rounded to 2)= b
```

Assignment modifiers transform assigned values. Comparison modifiers transform or implement comparisons.

Modifiers apply only in assignment and comparison contexts. In v0.1, modifiers apply to variables, record fields, and array elements, not to function-call results.

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
name(text.caseless)= "joe"
```

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
outgoing requests only. A webserver, if added later, will be a separate module.

## Core Builtin Functions

gBASIC includes always-available core functions that don't require loading libraries. These maintain strict type checking and provide clear error messages.

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

**Arithmetic behavior:**
- `+` performs string concatenation when either operand is a string
- `-`, `*`, `/` remain strict numeric arithmetic only
- All conversion functions are explicit and strict

### Other Built-Ins

Always-available helper functions:

- `compare(a, operator, b)`
- `lower(text)`
- `upper(text)`
- `round(number, places)`
- `input(prompt)`
- `encode(value)`
- `decode(text)`
- `quote(value)`
- `find(value, target)`
- `contains(array, value)`
- `remove_value(array, value)`
- `find_by(records, field_name, value)`
- `left(value, count)`
- `right(value, count)`
- `mid(value, start, count)`
- `mid(value, start, count, replacement)`
- `trim(text)`
- `split(text)`
- `split(text, separator)`
- `join(array)`
- `join(array, separator)`
- `join_from(array, start_index, separator)`
- `first(array)`
- `rest(array)`

Array helper functions:

- `append(array, value)`
- `prepend(array, value)`
- `insert(array, index, value)`
- `remove(array, index)`
- `remove_value(array, value)`
- `take_first(array)`
- `take_last(array)`
- `reverse(array)`
- `unique(array)`
- `sort(array)`

`contains(array, value)` returns true when the array contains a matching value. `remove_value(array, value)` removes the first matching value and returns the resulting array; when the first argument is a variable, that array is updated in place. `find_by(records, field_name, value)` returns the first matching record index or `nothing`. `join_from(array, start_index, separator)` joins string elements from `start_index` to the end and returns `""` when the start is out of range. `first(array)` returns the first element or `nothing`; `rest(array)` returns a new array without the first element.

Input and arithmetic rules:

- `input(...)` returns a string
- use `(number)` when you want numeric input
- `-`, `*`, and `/` require numbers
- `+` does numeric addition when neither operand is a string
- if either operand is a string, `+` converts both operands with canonical string conversion and concatenates them

Canonical string conversion:

```basic
text = string(project)
label(string)= count
line = "You were born in " + birth_year + "."
```

`string(value)` and the `(string)` modifier use the same canonical conversion. Numbers print as normal numbers, strings stay unquoted, booleans become `true` or `false`, `nothing` becomes `nothing`, `unknown` becomes `unknown`, and arrays and records use the same textual shape as `encode(value)`.

Serialization helpers:

```basic
text = encode(project)
loaded = decode(text)
```

`encode` supports numbers, strings, booleans, `nothing`, `unknown`, arrays, and records. Strings escape quotes, backslashes, tabs, carriage returns, and newlines. Records are encoded as JSON-like objects with quoted field names. `decode` reads the same format back into gBASIC values, accepts JSON `null` as `nothing`, supports standard JSON string escapes, and raises a runtime error for malformed text.

`string(value)` may look the same as `encode(value)` for arrays and records, but the concepts differ: `string(value)` is canonical display text, while `encode(value)` is serialization meant for `decode(...)`.

Source generation helper:

```basic
line = "description = " + quote(description)
```

`quote(value)` returns a complete gBASIC string literal, including surrounding double quotes. String contents escape `"`, `\`, tab, carriage return, and newline; newlines are emitted as `\n` instead of literal line breaks. Scalar non-string values use the same scalar conversion as `string(value)`, so `quote(42)` returns `"42"` and `quote(nothing)` returns `"nothing"`. Arrays, records, files, directories, and other non-scalar values raise a runtime error. Because the output is also a decode-compatible string value, `decode(quote(text))` round-trips strings.

Array aggregate functions:

- `len(value)`
- `sum(array)`
- `mean(array)`
- `median(array)`
- `mode(array)`
- `min(array)`
- `max(array)`

File functions:

- `exists(f)`
- `read(f)`
- `write(f, text)`
- `append(f, text)`
- `bytes(f)`
- `lines(f)`
- `chars(f)`
- `lock(f)`
- `unlock(f)`

Directory functions:

- `list(folder)`
- `files(folder)`
- `folders(folder)`

Directory entries are records with:

- `name`
- `path`
- `type`

## Living Examples

- `examples/adventure/adventure.bas` is a small text adventure using current input, print, modifiers, arrays, functions, `if`/`else`, `while`, and `break`.
- `examples/adventure/NOTES.md` records design-friction notes from that example.

## CLI Flags

Run a program:

```sh
gbasic FILE
```

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

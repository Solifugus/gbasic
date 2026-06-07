# gBASIC

gBASIC is an experimental BASIC-family language focused on readable business-style programs, explicit modifiers, small libraries, and practical runtime values such as dates, files, directories, money, arrays, and records.

This repository contains the v0.1 C implementation. It is intentionally small: a hand-written lexer, a Bison parser, an AST, and a minimal evaluator. The language and runtime are still experimental and should be treated as `0.1.0-dev`.

## Current Status

Implemented v0.1 pieces include:

- Lexer, parser, AST dump, and evaluator
- Variables, assignment, `print(...)`, `input(...)`, `if`/`else`, `consider`, `while`, `break`, `continue`, `for item in items`
- Arrays, records, indexing, field access
- User functions, labels, `goto`, `gosub`
- User-defined modifiers for assignment and comparison
- Program and library blocks with explicit `load`
- File, directory, date/time, duration, and money values
- Synchronous PostgreSQL access through the optional libpq-backed `pg` module
- Synchronous HTTP and HTTPS requests through the optional libcurl-backed `webclient` module
- Watchers and basic error handling
- Smoke and expected-output tests for examples

Notable limitations remain: the runtime is not optimized, the standard library is tiny, diagnostics are basic, and several documented future features are not implemented yet.

## Build

Requirements:

- C compiler with C11 support
- `make`
- `bison`
- optional: `pkg-config` and GTK 3 development headers for GUI support
- optional: `pkg-config` and libpq development headers for PostgreSQL support
- optional: `pkg-config` and libcurl development headers for WebClient support

Linux packages for the GTK proof of concept:

- Debian/Ubuntu: `libgtk-3-dev pkg-config`
- Fedora: `gtk3-devel pkgconf-pkg-config`
- Arch: `gtk3 pkgconf`

Build:

```sh
make
```

If GTK 3 development files are available, `make` enables the Stage 2 GUI proof of concept automatically. If GTK is unavailable, the interpreter still builds and runs non-GUI programs, but `gui.window(...)` and `gui.run(...)` fail with a clear runtime error.

If libpq development files are available, `make` enables `load pg`
automatically. Without libpq, the interpreter still builds and reports that
PostgreSQL support is unavailable when the module is loaded.

If libcurl development files are available, `make` enables `load webclient`
automatically. Without libcurl, the interpreter still builds and reports that
WebClient support is unavailable when the module is loaded.

Clean and rebuild:

```sh
make clean
make
```

## Run

Run a program:

```sh
./gbasic examples/parse_test.gb
```

Print tokens:

```sh
./gbasic --tokens examples/lexer_test.gb
```

Print the AST:

```sh
./gbasic --ast examples/parse_test.gb
```

Analyze unresolved calls/modifiers and print source with suggested `load` statements:

```sh
./gbasic --add-loads examples/add_uses_test.bas
```

Compatibility note: `use` and `--add-uses` still work for now, but new examples should prefer `load` and `--add-loads`.

See [examples/adventure/adventure.bas](examples/adventure/adventure.bas) for a small text-adventure example using current control flow, input, modifiers, arrays, and functions. [examples/adventure/NOTES.md](examples/adventure/NOTES.md) records design-friction notes found while writing it.

## Tests

Run all example smoke and expected-output tests:

```sh
./tests/run_examples.sh
```

Run negative lexer/parser diagnostic tests:

```sh
./tests/run_negative.sh
```

Run the live PostgreSQL integration test using standard libpq connection
environment variables:

```sh
GBASIC_POSTGRES_TEST=1 PGDATABASE=my_test_database ./tests/run_postgres.sh
```

Run the loopback WebClient integration test:

```sh
./tests/run_webclient.sh
```

Manual GUI proof-of-concept run:

```sh
GBASIC_PATH=stdlib ./gbasic examples/gui/demo.bas
```

See [examples/gui/README.md](examples/gui/README.md) for manual verification steps.

## Language Examples

Modifier assignment:

```basic
price(USD)= 19.95
due(date)= "2026-05-15"
command(trimmed)= input(">")
age(number)= input("Age: ")
command(lowered)= command
```

`input(...)` returns a string. Use the `(number)` modifier when you want numeric input, and keep arithmetic operands numeric. gBASIC no longer silently treats strings as `0` in arithmetic expressions.

`+` is split between arithmetic and concatenation:

- if neither operand is a string, `+` is numeric addition and both operands must be numbers
- if either operand is a string, both operands are converted with canonical string conversion and concatenated

This means `"You were born in " + birth_year + "."` works, while `1 + true` and `1 + nothing` still raise runtime errors.

Modifier comparison:

```basic
name = "Joe Barnes"
if name(caseless)= "joe barnes" then
    print("match")
end if
```

Function:

```basic
function add(a, b)
    return a + b
end function

print(add(2, 3))
```

## WebClient

Load `webclient` to make outgoing HTTP or HTTPS requests:

```basic
load webclient

response = webclient.get("https://example.com")
print(response.status)
print(response.body)
```

Request bodies must be strings. Use `encode(value)` explicitly when sending
JSON, and use `webclient.request` when custom headers are needed:

```basic
headers = {}
headers["Content-Type"] = "application/json"

response = webclient.request({
    method:"POST",
    url:"https://api.example.com/users",
    headers:headers,
    body:encode({name:"Ada", active:true})
})
```

`webclient.post(url, body)` is the shorter form when no custom headers are
needed:

```basic
response = webclient.post(
    "https://api.example.com/messages",
    encode({message:"hello"})
)
```

A response contains `status`, `reason`, `headers`, and string `body` fields.
When the body is valid JSON, it also contains a decoded `json` field:

```basic
response = webclient.get("https://api.example.com/users/1")
if not is_unknown(response["json"]) then
    print(response.json.name)
end if
```

HTTP statuses such as 404 and 500 return response records. Network, DNS, TLS,
malformed URL, and timeout failures are runtime errors. WebClient is for
outgoing requests only; a future webserver will be a separate module.

## Core Builtin Functions

gBASIC includes always-available core functions that don't require loading libraries:

**Type inspection:**
```basic
print(type("hello"))           # "string"
print(is_array([1, 2, 3]))     # true
print(is_record({x:1}))        # true
```

**Strict conversion:**
```basic
num = number("42")             # Convert string to number
text = string(123)             # Convert number to string
arr = array("[1, 2, 3]")       # Decode JSON string to array
```

**String helpers:**
```basic
print(replace("hello", "l", "x"))     # "hexxo"
print(starts_with("hello", "he"))     # true
print(repeat("ha", 3))                # "hahaha"
```

**Record helpers:**
```basic
rec = {name:"John", age:30}
print(keys(rec))                      # array of keys
print(values(rec))                    # array of values
print(has(rec, "name"))               # true
new_rec = remove_key(rec, "age")      # new record without age
```

**Counting:**
```basic
print(count("hello"))          # 5 (string length)
print(count([1, 2, 3]))        # 3 (array elements)
print(count({x:1, y:2}))       # 2 (record fields)
```

These functions maintain strict type checking and provide clear error messages for invalid arguments.

Control flow:

```basic
i = 0
while true
    if i = 3 then
        break
    else
        print(i)
    end if
    i = i + 1
end while

consider command
if "look" then
    look()
if "inventory" then
    show_inventory()
else
    print("I do not understand.")
end consider
```

Values and comparisons:

```basic
words(split)= "apple banana orange"

if len(words) != 0 then
    print(join(words, ", "))
else
    print(nothing)
end if

noun = join_from(words, 1, " ")
print(contains(words, "banana"))

saved = encode({title = "Demo", words = words})
loaded = decode(saved)
print(loaded.title)

line = "print(" + quote("hello \"BASIC\"") + ")"
print(line)

status = unknown
```

Canonical string conversion:

```basic
print("Age: " + 55)
print(string({name = "Grace", age = 35}))
```

`string(value)` and the `(string)` modifier convert values to display text. Arrays and records use the same textual shape as `encode(value)`, but `string(value)` is display-oriented while `encode(value)` is serialization. `quote(value)` remains separate and returns a valid gBASIC source literal, including surrounding quotes for strings.

Nested assignment works for variables, array elements, record fields, and combinations such as `items[i].location = "inventory"` or `world.rooms[index].visited = true`. Records support static field access with `record.field` and dynamic field access with `record[key]` when `key` is a string; missing dynamic reads return `unknown`, and dynamic assignment creates the field when needed.

Function calls are expressions, not assignment targets: `if len(words) = 0 then` is valid, but `len(words) = 0`, `foo() = 1`, and `get_player().name = "Bob"` are invalid.

Short conditionals may place one statement on the same line as `then` or
`else`. When the final branch is inline, `end if` is omitted:

```basic
if ready then print("ready")
else print("waiting")
```

A final branch whose body begins on the next line remains a block and requires
`end if`.

Library and `load`:

```basic
library math
    function double(x)
        return x * 2
    end function
end library

program demo(args)
    load math
    print(double(21))
end program
```

String `load` form for library names is also accepted:

```basic
load "gui"
```

Watcher:

```basic
a = 10
b = 20

watch(a, b)
    c = a + b
end watch

print(c)
a = 15
print(c)
```

Error handling:

```basic
on error resume next

print(missing_value)
if error then
    print(error.message)
    error.clear()
end if
```

## Version

```sh
./gbasic --version
```

prints:

```text
gBASIC 0.1.0-dev
```

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
- Watchers and basic error handling
- Smoke and expected-output tests for examples

Notable limitations remain: the runtime is not optimized, the standard library is tiny, diagnostics are basic, and several documented future features are not implemented yet.

## Build

Requirements:

- C compiler with C11 support
- `make`
- `bison`
- optional: `pkg-config` and GTK 3 development headers for GUI support

Linux packages for the GTK proof of concept:

- Debian/Ubuntu: `libgtk-3-dev pkg-config`
- Fedora: `gtk3-devel pkgconf-pkg-config`
- Arch: `gtk3 pkgconf`

Build:

```sh
make
```

If GTK 3 development files are available, `make` enables the Stage 2 GUI proof of concept automatically. If GTK is unavailable, the interpreter still builds and runs non-GUI programs, but `gui.window(...)` and `gui.run(...)` fail with a clear runtime error.

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
command(lowered)= command
```

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

Nested assignment works for variables, array elements, record fields, and combinations such as `items[i].location = "inventory"` or `world.rooms[index].visited = true`. Records support static field access with `record.field` and dynamic field access with `record[key]` when `key` is a string; missing dynamic reads return `unknown`, and dynamic assignment creates the field when needed.

Function calls are expressions, not assignment targets: `if len(words) = 0 then` is valid, but `len(words) = 0`, `foo() = 1`, and `get_player().name = "Bob"` are invalid.

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

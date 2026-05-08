# gBASIC Tutorial

gBASIC is an experimental BASIC-family language for readable business-style programs. This tutorial teaches the current v0.1 implementation through small examples.

## Hello World

```basic
print("hello, world")
```

Run it:

```sh
./gbasic hello.bas
```

Programs may also be wrapped explicitly:

```basic
program demo(args)
    print("hello from program")
end program
```

If a file has no `program` block, the whole file is treated as an implicit program.

## Variables

Assign values with `=`.

```basic
name = "Ada"
age = 37
ready = true

print(name)
print(age + 1)
print(ready)
```

Identifiers are case-sensitive in storage. Keyword-like names such as `end` and `next` are accepted in the limited places where the parser explicitly allows them as identifiers.

## Modifiers

Modifiers attach domain meaning to assignment or comparison.

Assignment modifiers:

```basic
price(USD)= 19.95
due(date)= "2026-05-15"

print(price)
print(due)
```

Comparison modifiers:

```basic
name = "Joe Barnes"

if name(caseless)= "joe barnes" then
    print("match")
end if
```

You can define modifiers:

```basic
modifier rounded(n) for compare
    return compare(round(left, n), operator, round(right, n))
end modifier

a = 1.234
b = 1.235

if a(rounded 2)= b then
    print("rounded match")
end if
```

Modifier bodies receive built-in variables:

- Assign modifiers receive `value`.
- Compare modifiers receive `left`, `right`, and `operator`.

Useful built-in assignment modifiers include:

```basic
command(trimmed)= input(">")
command(lowered)= command
code(uppered)= "abc"
words(split)= "apple banana orange"
line(join ", ")= words
n(length)= line
age(number)= "42"
text(string)= age
```

## Arrays And Records

Arrays use brackets:

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
```

Records use braces and `name = value` fields:

```basic
customer = {
    name = "Ada",
    age = 37,
    balance = 120.00
}

print(customer.name)

customer.name = "Grace"
print(customer.name)

field = "age"
print(customer[field])
```

## Functions

Functions use parameters and `return`.

```basic
function add(a, b)
    return a + b
end function

function greet(name)
    return "Hello, " + name
end function

print(add(2, 3))
print(greet("Ada"))
```

Function variables are local. Global variables remain visible unless shadowed by locals.

Labels, `goto`, and `gosub` are currently supported inside functions:

```basic
function normalize(x)
    if x < 0 then goto negative
    return x

negative:
    return 0
end function

print(normalize(-3))
```

Function calls are expressions, so they can be compared:

```basic
words = []
if len(words) = 0 then
    print("empty")
end if
```

Function calls are not assignment targets. `len(words) = 0` is invalid.

## Control Flow

Use `if`/`else` for branches:

```basic
if ready then
    print("ready")
else
    print("waiting")
end if
```

Use `while` for loops:

```basic
i = 0
while i < 3
    print(i)
    i = i + 1
end while
```

`break` exits the nearest `while`; `continue` skips to the next iteration:

```basic
i = 0
while i < 5
    i = i + 1
    if i = 3 then
        continue
    end if
    print(i)
end while
```

## Nothing And Unknown

`nothing` is deliberate absence. `unknown` is a distinct value for information that exists but is not known yet.

```basic
x = nothing
y = unknown

if x != y then
    print("distinct")
end if
```

## Errors

Default behavior is `on error stop`. Runtime errors stop execution and return a nonzero process exit status.

Use `on error resume next` to inspect and clear error state:

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

Use `on error goto label` for a single-use handler:

```basic
on error goto failed
error "explicit failure"
print("not reached")

failed:
print(error.message)
error.clear()
```

## Watchers

Watchers run after assignments to watched top-level variables.

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

Suppress watcher triggering with `without watchers`:

```basic
without watchers
    a = 100
end without
```

## Libraries And Load

Libraries collect functions and exported modifiers.

```basic
library math
    function add(a, b)
        return a + b
    end function
end library

program demo(args)
    load math
    print(add(2, 3))
end program
```

Only exported modifiers are imported:

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

Load a library from another file:

```basic
program demo(args)
    load tools from "libs/tools.bas"
    print(double(21))
end program
```

Compatibility note: `use` still works for now, but new code should prefer `load`.

Qualified calls and modifiers bypass normal override lookup:

```basic
print(math.add(2, 3))

if name(text.caseless)= "joe" then
    print("match")
end if
```

## File Operations

Create a file reference with `(file)`.

```basic
f(file)= "examples/tmp_file_test.txt"

write(f, "line one\nline two\n")
print(exists(f))
print(bytes(f))
print(lines(f))
print(read(f))

append(f, "line three\n")
print(lines(f))
```

Use advisory locking:

```basic
with lock(f)
    write(f, "locked write\n")
    append(f, "still locked\n")
end with
```

`with lock` unlocks after the block finishes.

## Date/Time Lenses

Core date/time support is based on primitive lenses. Convenience calendar behavior belongs in libraries.

```basic
d(date)= "2026-05-15 14:30:20"

y(year)= d
print(y)

if d(month)= "2026-05" then
    print("same month")
end if
```

Primitive date/time modifiers:

- `date`
- `time`
- `datetime`
- `year`
- `month`
- `day`
- `hour`
- `minute`
- `second`

Durations are first-class values:

```basic
start(date)= "2026-05-15 13:10:00"
d1 = 1 hour 20 minutes

print(start + d1)
print(start + 2 days)
```

The standard library can build convenience modifiers from primitives:

```basic
program demo(args)
    load dates from "../stdlib/dates.bas"

    today(date)= "2026-05-15"
    end(end of month)= today
    print(end)
end program
```

## Money

`USD` creates a money value stored internally as cents.

```basic
price(USD)= 19.95
tax = price * 0.08
total = price + tax

print(price)
print(tax)
print(total)

if total > price then
    print("greater")
end if
```

Money supports money-to-money addition, subtraction, and comparisons. Multiplication and division are supported with numbers.

## Larger Example

See `examples/adventure/adventure.bas` for a small Infocom-style text adventure using `input(">")`, `print(...)`, `(trimmed)`, `(lowered)`, `(split)`, `find`, arrays, functions, `if`/`else`, `while`, and `break`.

The companion `examples/adventure/NOTES.md` records design-friction notes discovered while writing the example.

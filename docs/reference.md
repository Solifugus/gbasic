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

Inline `goto` and `gosub` are also supported:

```basic
if x < 0 then goto negative
```

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

## Built-Ins

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

Serialization helpers:

```basic
text = encode(project)
loaded = decode(text)
```

`encode` supports numbers, strings, booleans, `nothing`, `unknown`, arrays, and records. Strings escape quotes, backslashes, tabs, carriage returns, and newlines. Records are encoded as JSON-like objects with quoted field names. `decode` reads the same format back into gBASIC values and raises a runtime error for malformed text.

Source generation helper:

```basic
line = "description = " + quote(description)
```

`quote(value)` returns a complete gBASIC string literal, including surrounding double quotes. String contents escape `"`, `\`, tab, carriage return, and newline; newlines are emitted as `\n` instead of literal line breaks. Scalar non-string values use the same conversion as the `(string)` modifier, so `quote(42)` returns `"42"` and `quote(nothing)` returns `"nothing"`. Arrays, records, files, directories, and other non-scalar values raise a runtime error. Because the output is also a decode-compatible string value, `decode(quote(text))` round-trips strings.

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

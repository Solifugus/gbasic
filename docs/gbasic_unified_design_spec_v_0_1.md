# gBASIC Unified Design Specification v0.1

## 1. Purpose

gBASIC is a modern BASIC-family language designed to preserve BASIC's immediate readability while adding modern data structures, contextual typing, reactive programming, and a path toward GCC-based compilation.

The long-term implementation goal is:

```text
gBASIC source
    -> lexer
    -> parser
    -> AST
    -> interpreter and/or C emitter
    -> GCC
    -> native executable
```

The first implementation should be written in C, using a hand-written lexer and a Bison parser.

---

# 2. Core Philosophy

## Meaning Through Context

The central design principle of gBASIC is:

```text
meaning is applied through context, not encoded in special literal glyphs
```

Rather than introducing many symbolic literal forms, gBASIC applies meaning through modifiers and contextual interpretation.

Instead of:

```basic
balance = $19.95
start = @2026-05-15
f = #"data.txt"
```

gBASIC uses:

```basic
balance(USD)= 19.95
start(date)= "2026-05-15"
f(file)= "data.txt"
```

## Architectural Principles

gBASIC should be:

- readable to non-specialists
- expressive without ceremony
- context-sensitive where that reduces syntax
- extensible without polluting the general namespace
- permissive enough to preserve BASIC's spirit
- structured enough to support serious software

## Identity of the Language

gBASIC is a minimal-core language where meaning is expressed through modifiers (semantic lenses), and higher-level behavior is built in the language itself through libraries.

Rule of thumb:

```text
Modifier -> meaning FROM a value
Function -> relationship BETWEEN values
Library -> convenience built from primitives
Core -> only what cannot be expressed cleanly in the language itself
```

---

# 3. Core Language Features

## Basic Constructs

- variables
- assignment
- expressions
- operators
- control flow
- functions
- arrays
- records
- file operations
- directory listing
- BASIC-style error handling
- modifiers
- libraries
- watchers

---

# 4. Comments

Single-line comments use apostrophe syntax:

```basic
' This is a comment
```

A future version may also support:

```basic
// comment
```

but apostrophe comments are preferred for v0.1.

---

# 5. Core Types

## Built-In Types

- Number
- String
- Boolean
- Array
- Record
- Money
- Date/Time
- Duration
- File Reference
- Directory Reference
- Nothing
- Unknown

Strings are stored as C strings. UTF-8 text is accepted, but current string length, indexing, and case conversion helpers are byte-oriented unless a helper states otherwise.

Booleans:

```basic
ok = true
done = false
```

## Nothing

`nothing` represents deliberate absence.

Comparison with `nothing` checks whether a value exists.

```basic
if x = nothing then
    print("x does not exist")
end if
```

Assignment to `nothing` currently stores the null/nothing value. Deletion or unset semantics are deferred.

```basic
x = nothing
```

This means the value is absent, not merely blank.

## Unknown

`unknown` represents a conceptually existing value that is not yet known.

```basic
customer.phone = unknown
```

This differs from:

```basic
customer.phone = nothing   ' no phone value exists
customer.phone = ""        ' known blank string
```

Errors are not converted into `unknown` automatically. Failed operations, such as division by zero, remain errors.

---

# 6. Assignment

## Standard Assignment

```basic
x = 10
name = "Ada"
```

## Typed or Interpreted Assignment

```basic
balance(USD)= 19.95
dob(date)= "1970-06-11"
config(file)= "/etc/app/config.txt"
```

Assignment targets may be:

- variables
- record fields
- array elements

Examples:

```basic
customer.name = "Ada"
scores[0] = 95
```

Function calls are not valid assignment targets.

Invalid:

```basic
getname() = "Joe"
```

---

# 7. Modifier System

## Philosophy

The modifier system is one of the central features of gBASIC.

Modifiers change the meaning of assignment or comparison without becoming normal global keywords and without polluting the ordinary variable/function namespace.

Examples:

```basic
balance(USD)= 19.95
name(caseless)= "joe barnes"
amount(rounded 2)= expected
amount(rounded to 2)= expected
```

## Spacing Rules

Spacing is not semantically important.

These forms are equivalent:

```basic
name(caseless)= "joe"
name (caseless)= "joe"
name(caseless) = "joe"
name (caseless) = "joe"
```

## Modifier Contexts

v0.1 supports:

```text
assign
compare
```

Possible future contexts:

```text
sort
format
parse
match
```

## Assignment Modifiers

Assignment modifiers transform or interpret assigned values.

Syntax:

```basic
target(modifier)= value
```

Conceptually:

```text
target = apply_assign_modifier(modifier, value)
```

Example:

```basic
modifier USD for assign
    return money(value, "USD")
end modifier
```

## Comparison Modifiers

Comparison modifiers transform comparison behavior.

Syntax:

```basic
left(modifier)= right
left(modifier)!= right
left(modifier)> right
left(modifier)< right
left(modifier)>= right
left(modifier)<= right
left(modifier)!> right
left(modifier)!< right
left(modifier)!>= right
left(modifier)!<= right
```

Inside a comparison modifier:

```basic
left
right
operator
```

are available.

Example:

```basic
modifier caseless for compare
    return compare(lower(left), operator, lower(right))
end modifier
```

Usage:

```basic
if name(caseless)= "joe barnes" then
    print("match")
end if
```

## Parameterized Modifiers

Modifiers may take parameters.

Definition:

```basic
modifier rounded(n) for compare
    return compare(round(left, n), operator, round(right, n))
end modifier
```

Usage:

```basic
if amount(rounded 2)= expected then
    print("close enough")
end if
```

## Multi-Word Modifiers

Modifiers may contain multiple words.

Example:

```basic
modifier rounded to(n) for compare
    return compare(round(left, n), operator, round(right, n))
end modifier
```

Usage:

```basic
if amount(rounded to 2)= expected then
    print("close enough")
end if
```

## Modifier Namespace

Modifier names live in a separate modifier namespace.

This is legal:

```basic
caseless = "ordinary variable"

if name(caseless)= "joe" then
    print("match")
end if
```

A term is treated as a modifier only when it appears inside parentheses immediately before an assignment or comparison operator.

## Modifier Scope

### Program Scope

A modifier defined inside a program is available inside that program.

### Library Scope

Modifiers defined inside libraries are private unless exported.

Example:

```basic
library text
    modifier internal trimcompare for compare
        ...
    end modifier
end library
```

## Exported Modifiers

Libraries may export modifiers.

```basic
library text
    export modifier caseless for compare
        return compare(lower(left), operator, lower(right))
    end modifier
end library
```

## Conflict Resolution

### Local Priority

Local modifiers override imported modifiers.

Compiler warnings should be emitted for shadowing.

### Explicit Load Order

Later imported libraries override earlier imported libraries.

Example:

```basic
load text
load stricttext
```

If both export `caseless`, `stricttext.caseless` wins.

### Library Qualification

Explicit qualification is supported:

```basic
if name(text.caseless)= "joe" then
    print("match")
end if
```

## Built-In and Core-Library Modifiers

### Assignment

```text
USD
date
time
datetime
year
month
day
hour
minute
second
file
dir
number
string
trimmed
lowered
uppered
split
join
length
```

Examples:

```basic
name(trimmed)= "  joe jones   "
name(lowered)= "Joe Jones"
code(uppered)= "abc"
fruit(split)= "apple banana orange"
fruit(split ",")= "apple,banana,orange"
line(join ", ")= fruit
numchars(length)= "big long text line"
age(number)= input("Enter your age: ")
```

`split` implies that the assigned variable receives an array.

`join` implies that the assigned variable receives a string.

`length` assigns the length of the incoming value.

`lowered` and `uppered` assign the lowercase or uppercase version of an incoming string.

These string and collection-oriented modifiers may live in a core library rather than in the irreducible language core. The core requirement is that the modifier system be powerful enough to define them.

### Comparison

```text
caseless
rounded n
```

Examples:

```basic
if name(caseless)= "joe" then
    print("match")
end if

if amount(rounded 2)= expected then
    print("close enough")
end if
```

## Modifier Error Behavior

If a modifier fails, it raises a runtime error.

Example:

```basic
balance(USD)= "banana"
```

Possible runtime message:

```text
Invalid value for USD modifier
```

---

# 8. Operators

## Comparison Operators

Supported operators:

```text
=   equal
!=  not equal
>   greater than
<   less than
>=  greater than or equal
<=  less than or equal
!>  not greater than
!<  not less than
!>= not greater than or equal
!<= not less than or equal
and
or
not
```

Parentheses may group conditions.

The `=` operator is context-sensitive.

```basic
x = 10
if x = 10 then
```

## String Concatenation

The `+` operator concatenates strings.

```basic
fullname = first + " " + last
```

The current implementation concatenates string values with `+`. Convert other scalar values explicitly with the `(string)` assignment modifier before concatenating.

This preserves simple BASIC-style expression writing without adding string interpolation in v0.1.

## No f-Strings in v0.1

Python-like f-strings are deferred.

For v0.1, string construction should use concatenation or library formatting functions.

```basic
print("Hello " + name)
```

---

# 9. Control Flow

## If

```basic
if x > 10 then
    print("large")
else
    print("small")
end if
```

## For Each

```basic
for score in scores
    print(score)
end for
```

Numeric `for i = ... to ... step ...` loops are not implemented in the current v0.1 interpreter.

## While

```basic
while x < 10
    x = x + 1
end while
```

`break` exits the nearest enclosing `while`. `continue` skips the rest of the current loop body and begins the next iteration.

```basic
while true
    command(lowered)= input(">")
    if command = "quit" then
        break
    end if
    if command = "" then
        continue
    end if
    print(command)
end while
```

---

# 10. Functions

## Definition

```basic
function add(a, b)
    return a + b
end function
```

## Call

```basic
x = add(2, 3)
```

## Scope

Functions have local scope with parent/global fallback.

Function argument expressions are evaluated in the caller scope before being passed into the function.

Functions are not assignable.

---

# 11. Labels, Goto, and Gosub

Labels are permitted.

```basic
start:
```

`goto` and `gosub` are allowed.

They are scoped to the current function.

They may not jump into or out of another function.

---

# 12. Arrays

## Literal

```basic
scores = [88, 91, 91, 74]
```

## Indexing

```basic
print(scores[0])
scores[1] = 95
```

## Aggregates

```basic
print(len(scores))
print(sum(scores))
print(mean(scores))
print(median(scores))
print(mode(scores))
print(min(scores))
print(max(scores))
```

---

# 13. Records

Records are named-field data objects.

Example:

```basic
customer = {
    name = "Ada",
    age = 37,
    balance = 120.00
}
```

Field access:

```basic
customer.name = "Ada"
print(customer.name)
```

Dynamic access:

```basic
field = "name"
print(customer[field])
```

---

# 14. Money

Money is created through assignment modifiers.

```basic
balance(USD)= 19.95
payment(USD)= 5.00
```

Internally stored as integer cents.

Supports arithmetic and comparisons.

Invalid mixed operations should raise errors.

v0.1 officially supports USD.

---

# 15. Date and Time

## Primitive Lenses

```basic
(date)
(time)
(datetime)
(year)
(month)
(day)
(hour)
(minute)
(second)
```

## Examples

```basic
d(date)= "2026-05-15"
t(time)= "14:30:00"
dt(datetime)= "2026-05-15 14:30"
```

## Precision-Aware Comparison

Comparisons use the lowest precision of the compared values.

Example:

```basic
d1(date)= "2026-05-15"
d2(date)= "2026-05-15 12:05:03"

if d1 = d2 then
    print("same day")
end if
```

## Core Scope Rule

The core intentionally excludes:

- end of month
- next monday
- previous friday
- business days
- day names

These belong in libraries.

## Standard Library Dates

`stdlib/dates.bas` may implement:

- end of month
- next monday
- dayname()
- days_between()

built purely from primitives.

---

# 16. Durations

Durations are first-class values.

```basic
delay = 1 hour 20 minutes 2 seconds
newtime = oldtime + delay
```

Supported units:

- years
- months
- weeks
- days
- hours
- minutes
- seconds

Calendar-relative durations must remain distinct from exact durations.

---

# 17. Files

Files are typed filesystem references.

```basic
path = "data.txt"
f(file)= path
```

A file reference is not an open handle.

## File Operations

```basic
text = read(f)
write(f, "new text")
append(f, "more text")
```

## Properties

```basic
f.path
f.name
f.parent
f.extension
f.created
f.modified
f.accessed
f.exists
f.type
```

## Locking

Explicit locking:

```basic
lock(f)
write(f, "safe update")
unlock(f)
```

Safe block locking:

```basic
with lock(f)
    write(f, "safe update")
end with
```

`with lock` guarantees unlock.

---

# 18. Directories

Directories may be separate types or file-reference-like values.

Example:

```basic
folder(dir)= "/home/matthew/docs"

for item in list(folder)
    print(item.name)
end for
```

Helpers:

```basic
list(folder)
files(folder)
folders(folder)
```

---

# 19. Watchers

Watchers are reactive code blocks.

Example:

```basic
watch(a, b)
    c = a + b
end watch
```

Assignments inside watchers may trigger other watchers.

Safe suppression:

```basic
without watchers
    a = 10
    b = 20
end without
```

## Use Cases

- reactive state
- self-healing values
- validation
- UI binding
- workflow automation

Example:

```basic
watch(balance)
    if balance < 0 then
        status = "overdrawn"
    else
        status = "ok"
    end if
end watch
```

---

# 20. Error Handling

## Philosophy

gBASIC uses a BASIC-style error handling model with a persistent error object.

## Default Behavior

```basic
on error stop
```

Runtime errors:

- print diagnostics
- stop execution

## Error Control Statements

```basic
on error goto label
on error resume next
on error stop
```

## on error goto

Single-use handler.

Behavior:

1. Error occurs
2. Error state is set
3. Control jumps to label
4. Handler is automatically cleared

Example:

```basic
on error goto failed

x = 1 / 0

failed:
print(error.message)
error.clear()
```

## on error resume next

Execution continues after an error.

```basic
on error resume next

x = 1 / 0

print("still running")
```

## Raising Errors

```basic
error "Something went wrong"
```

## Error Object

```basic
if error then
    print(error.message)
    print(error.line)
    print(error.column)
    print(error.code)
    print(error.source)
end if
```

## Clearing Errors

```basic
error.clear()
```

Errors persist until cleared.

## Error Replacement

If a new error occurs while another error is active, the new error replaces the old error.

## Propagation

Errors propagate across function boundaries.

## Interaction Rules

### Watchers

Errors inside watchers:

- set error state
- respect current error mode
- should not crash the program when `resume next` is active

### Functions

Handlers are scoped to the current function or program block.

### File Locks

All locks must release even during errors.

---

# 21. Libraries

## Philosophy

The core language remains intentionally small.

Higher-level behavior should be written in gBASIC libraries whenever practical.

## Definition

```basic
library math
    function add(a, b)
        return a + b
    end function
end library
```

Multiple libraries may exist in one file.

## Usage

```basic
load math
load logging from "libs/logging.bas"
```

## Export Behavior

Functions export by default.

Modifiers require explicit export.

## Auto-Discovery

Search order:

1. Current file
2. Explicitly used libraries
3. Current folder
4. Subfolders
5. GBASIC_PATH

## Ambiguity

First match wins.

Warnings should identify later matches.

## Qualified Access

```basic
math.add(2,3)
text.caseless
```

---

# 22. Built-Ins vs Standard Library

## Built-Ins

Always available:

```text
compare
lower
upper
round
len
find
left
right
mid
trim
split
join
sum
mean
median
mode
min
max
input
```

Built-ins cannot be silently overridden.

Local overrides emit warnings.

Ignored by `--add-loads`.
Compatibility note: `--add-uses` remains available for now.

## Parenthesized print and input

`print` and `input` use function-style parentheses in v0.1.

```basic
print("Hello")
name = input("Enter your name: ")
age(number)= input("Enter your age: ")
```

This keeps parsing simple and makes coercion through assignment modifiers natural.

## Finding Values

`find(collection, target)` searches strings and arrays.

```basic
pos = find(text, "banana")
index = find(users, "george")
```

If the target is found, `find` returns its position or index.

If not found, it returns `nothing`.

```basic
if find(users, "george") != nothing then
    print("found george")
end if
```

No `indexof` alias is included in v0.1. `find` is the standard vocabulary.

---

# 23. Standard Library Philosophy

Examples of likely standard libraries:

```text
stdlib/dates.bas
stdlib/strings.bas
stdlib/files.bas
stdlib/math.bas
stdlib/ui.bas
```

Libraries should be implemented primarily in gBASIC itself.

## String and Collection Core Library

The standard string and collection vocabulary should preserve BASIC familiarity while providing modern usefulness.

Primary names:

```basic
len(value)
find(value, target)

left(value, count)
right(value, count)
mid(value, start, count)
mid(value, start, count, replacement)

trim(value)
split(value)
split(value, separator)
join(values)
join(values, separator)
lower(value)
upper(value)
```

`len`, `left`, `right`, and `mid` preserve BASIC identity.

`mid(value, start, count)` extracts a substring or subarray.

`mid(value, start, count, replacement)` replaces the selected section and returns the modified string or array.

Examples:

```basic
part = mid(text, 5, 3)
text = mid(text, 5, 3, "XYZ")

some = mid(items, 2, 4)
items = mid(items, 2, 4, newitems)
```

No `slice`, `replace_slice`, `indexof`, `count`, or `countof` aliases are included in v0.1.

## Array Utility Vocabulary

Preferred array operation names should be intuitive rather than stack-oriented.

```basic
append(array, value)
prepend(array, value)
insert(array, index, value)
remove(array, index)
take_first(array)
take_last(array)
reverse(array)
unique(array)
sort(array)
```

Avoid `push` and `pop` as primary vocabulary in v0.1.

---

# 24. User Interface Philosophy

## Core UI

The core language should only include:

```basic
print(...)
input(...)
```

Possibly:

```basic
clear()
bell()
```

## Everything Else

Should be library territory:

- terminal UI
- GTK GUI
- PWA rendering
- vector UI
- compositor/window systems

---

# 25. CLI

```text
gbasic FILE
gbasic --tokens FILE
gbasic --ast FILE
gbasic --add-loads FILE
gbasic --help
gbasic --version
```

---

# 26. Compiler Assistance

## --add-loads

Behavior:

- scans unresolved functions/modifiers
- inserts load statements
- respects search priority
- ignores built-ins
- outputs modified source without overwriting original

Compatibility note: `--add-uses` remains available for now and emits `use` statements.

---

# 27. File Lock Safety

Requirements:

- all locks tracked in runtime registry
- unlock() removes registry entries
- with lock guarantees unlock
- all locks released on exit
- cleanup via atexit()
- signal cleanup for:
  - SIGINT
  - SIGTERM
  - SIGHUP

---

# 28. Testing

## Positive Tests

```text
tests/run_examples.sh
```

## Negative Tests

```text
tests/run_negative.sh
```

`.out` files define expected stdout.

Warnings on stderr are acceptable unless execution fails.

---

# 29. Grammar and Parsing

## Parser Strategy

The parser should not rely on spaces to distinguish modifiers from function calls.

These must parse identically:

```basic
name(caseless)= "joe"
name (caseless)= "joe"
name(caseless) = "joe"
name (caseless) = "joe"
```

## Disambiguation Rule

```text
If a parenthesized modifier term follows a valid assignment/comparison target and precedes an assignment or comparison operator, it is parsed as an operator modifier.

Otherwise it is parsed normally as a function call or grouped expression.
```

Function calls are expressions, not lvalues. They may appear in comparisons:

```basic
if len(words) = 0 then
    print("empty")
end if
```

They may not be assignment targets:

```basic
len(words) = 0   ' invalid
```

In v0.1, modifiers apply to variables, record fields, and array elements, not to function-call results:

```basic
if getname()(caseless)= "joe" then   ' invalid in v0.1
    print("match")
end if
```

## Lexer Direction

Initial implementation:

- hand-written lexer
- Bison parser
- AST construction
- interpreter runtime

---

# 30. Open Questions

## Remaining Design Questions

1. Should both apostrophe and `//` comments exist in v0.1?
2. Should files and directories share one type?
3. Should date parsing initially require strict ISO formats?
4. Should modifier chaining exist in v0.1?
5. Should user-defined modifiers compile into emitted C immediately?
6. Should `gosub` use a separate return stack?
7. Should watchers execute immediately or through queues?
8. Should watcher cycles be automatically detected?
9. Should records dynamically grow by default?

---

# 31. Implementation Priorities

Recommended order:

1. Error handling foundation
2. User-defined modifiers
3. Program/library wrappers
4. load and explicit library loading
5. Modifier export/import
6. Library-qualified modifiers
7. Auto-discovery
8. --add-loads
9. Real money type
10. C emitter

---

# 32. Current Stability Coverage

Currently stabilized:

- keyword/identifier ambiguity
- modifier/library override rules
- qualified vs unqualified resolution
- lexer error isolation
- error propagation behavior
- longest-match modifier resolution
- runtime modifier registry model

---

# 33. Near-Term Priorities

## High-Value Next Steps

- stdlib/strings.bas
- CLI args
- REPL

## Later Goals

- modifier chaining
- native libraries (dlopen/dlsym)
- VM or bytecode engine
- C emitter
- GCC frontend integration

---

# 34. v0.1 Target Program

```basic
name(trimmed)= input("Enter your name: ")
balance(USD)= 19.95
dob(date)= "1970-06-11"
config(file)= "data.txt"

if name(caseless)= "joe barnes" then
    print("matched")
    print(balance)
end if

scores = [88, 91, 91, 74, 100]
print(mean(scores))
print(mode(scores))

words(split)= "apple banana orange"
csv(split ",")= "apple,banana,orange"
line(join ", ")= words

if find(words, "banana") != nothing then
    print("banana found")
end if

part = mid(line, 1, 5)
line = mid(line, 1, 5, "fruit")
```

Living example:

- `examples/adventure/adventure.bas` exercises input, print, assignment modifiers, arrays, functions, `if`/`else`, `while`, `break`, and string helpers.
- `examples/adventure/NOTES.md` records design-friction notes from that example.

Watcher test:

```basic
a = 10
b = 20

watch(a, b)
    c = a + b
end watch

a = 15
print(c)
```

File reference test:

```basic
path = "data.txt"
f(file)= path

if exists(f) then
    print(lines(f))
    print(read(f))
end if
```

---

# 35. Final Identity Statement

gBASIC is a minimal-core BASIC-family language where semantic meaning is expressed through contextual modifiers, human-readable syntax, and libraries built largely in the language itself.

The language attempts to preserve the readability and spirit of classic BASIC while enabling modern extensibility, structured semantics, reactive behavior, and a long-term path toward serious native compilation.

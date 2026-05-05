# gBASIC v0.1 Design Addendum: Modifier System, Error Handling, Libraries, and Lock Safety

This addendum updates the gBASIC v0.1 design with the current decisions around the modifier system, BASIC-style error handling, library/use behavior, and file lock safety.

---

## 1. Modifier System

The modifier system is one of the central features of gBASIC.

A modifier changes the meaning of assignment or comparison without becoming a normal global keyword and without polluting the ordinary variable/function namespace.

Examples:

```basic
balance(USD)= 19.95
name(caseless)= "joe barnes"
amount(rounded 2)= expected
due(wednesday after)= today
```

Spacing is not semantically important:

```basic
name(caseless)= "joe"
name (caseless)= "joe"
name(caseless) = "joe"
name (caseless) = "joe"
```

These forms are equivalent when used immediately before an assignment or comparison operator.

---

## 2. Modifier Contexts

v0.1 supports only two modifier contexts:

```basic
for assign
for compare
```

Possible future contexts such as `format`, `sort`, `parse`, or `match` are reserved for later versions and should not be implemented in v0.1.

---

## 3. Assignment Modifiers

Assignment modifiers transform or interpret the value being assigned.

Syntax:

```basic
target(modifier)= value
```

Conceptual meaning:

```text
target = apply_assign_modifier(modifier, value)
```

Example:

```basic
modifier USD for assign
    return money(value, "USD")
end modifier

balance(USD)= 19.95
```

Inside an assignment modifier, the built-in variable:

```basic
value
```

contains the right-hand side value.

A future version may expose `target`, but v0.1 does not require it.

---

## 4. Comparison Modifiers

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
```

Inside a comparison modifier, these built-in variables are available:

```basic
left
right
operator
```

Example:

```basic
modifier caseless for compare
    return compare(lower(left), operator, lower(right))
end modifier
```

Use:

```basic
if name(caseless)= "joe barnes" then
    print "match"
end if
```

The helper function:

```basic
compare(a, operator, b)
```

should perform the ordinary comparison represented by `operator`.

---

## 5. Parameterized Modifiers

Modifiers may take parameters.

Definition:

```basic
modifier rounded(n) for compare
    return compare(round(left, n), operator, round(right, n))
end modifier
```

Use:

```basic
if amount(rounded 2)= expected then
    print "close enough"
end if
```

v0.1 supports one modifier phrase with optional parameters.

Modifier chaining is not part of v0.1.

---

## 6. Multi-Word Modifiers

Modifiers may contain multiple words.

Example:

```basic
modifier wednesday after for assign
    return nextweekday(value, "Wednesday")
end modifier
```

Use:

```basic
due(wednesday after)= today
```

Multi-word modifiers make the language more readable and support intent-like phrasing.

---

## 7. Modifier Namespace

Modifier names live in a separate modifier namespace.

This is legal:

```basic
caseless = "ordinary variable"

if name(caseless)= "joe" then
    print "match"
end if
```

The `caseless` in `name(caseless)=` is resolved as a comparison modifier.

The `caseless` variable remains an ordinary variable outside modifier position.

A term is treated as a modifier only when it appears inside parentheses immediately before an assignment or comparison operator.

---

## 8. Modifier Scope

Modifiers follow program/library scoping rules.

### Program Scope

A modifier defined inside a program is available inside that program.

```basic
program app(args)
    modifier caseless for compare
        return compare(lower(left), operator, lower(right))
    end modifier

    if name(caseless)= "joe" then
        print "match"
    end if
end program
```

### Library Scope

Modifiers defined inside a library are private to that library unless exported.

```basic
library text
    modifier internal trimcompare for compare
        ...
    end modifier
end library
```

The modifier above is usable inside `text`, but not by programs using `text`.

---

## 9. Exported Modifiers

Libraries may export modifiers.

```basic
library text
    export modifier caseless for compare
        return compare(lower(left), operator, lower(right))
    end modifier
end library
```

Then:

```basic
program app(args)
    use text

    if name(caseless)= "joe" then
        print "match"
    end if
end program
```

`export` is preferred over `public` because it describes library availability rather than object-oriented visibility.

---

## 10. Modifier Conflict Resolution

Conflicts are possible when multiple libraries export modifiers with the same name.

### Local Priority

Local program or library modifiers override imported modifiers.

If a local modifier shadows an imported modifier, the compiler should warn.

### Explicit Use Order

For explicitly used libraries:

```basic
use text
use stricttext
```

If both export `caseless`, the later library wins.

In this example:

```basic
stricttext.caseless
```

overrides:

```basic
text.caseless
```

The compiler should warn:

```text
Warning: modifier 'caseless' from stricttext overrides modifier from text.
```

This allows the programmer to control priority by use order.

### Library Qualification

To avoid ambiguity, a modifier may be qualified by library name:

```basic
if name(text.caseless)= "joe" then
    print "match"
end if

if name(stricttext.caseless)= "joe" then
    print "strict match"
end if
```

Library qualification is the preferred way to explicitly choose between conflicting exported modifiers.

### Built-In Modifiers

Built-in modifiers are lowest priority.

User-defined modifiers may shadow built-ins, but the compiler should warn.

---

## 11. Built-In Modifiers for v0.1

### Assignment

```basic
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
```

Core provides primitive lenses. Libraries provide human conveniences.

The core date/time modifiers are:

```basic
date
time
datetime
year
month
day
hour
minute
second
```

Do not put end of month, next monday, previous friday, day name, or business day into core. Those should be written later as gBASIC libraries using the primitive lenses.

### Comparison

```basic
caseless
rounded n
```

Date/time values already support precision-aware ordinary comparison.

---

## 12. Modifier Error Behavior

If a modifier fails, it raises a runtime error.

Example:

```basic
balance(USD)= "banana"
```

This should raise an error such as:

```text
Invalid value for USD modifier
```

Normal error handling rules then apply.

---

## 13. Error Handling

gBASIC uses BASIC-style error handling with a persistent error object.

### Default Behavior

By default:

```basic
on error stop
```

Any runtime error prints a diagnostic message and stops execution.

### Error Control Statements

```basic
on error goto label
on error resume next
on error stop
```

### on error goto

`on error goto` is single-use.

Behavior:

1. Error occurs.
2. Error state is set.
3. Control jumps to the label.
4. The active handler is cleared automatically.

Example:

```basic
on error goto failed

x = 1 / 0

failed:
print error.message
error.clear()
```

### on error resume next

Execution continues after an error.

```basic
on error resume next

x = missing_value

if error then
    print error.message
    error.clear()
end if
```

### on error stop

Restores default fatal error behavior.

```basic
on error stop
```

### Raising Errors

Errors may be raised explicitly:

```basic
error "I just dislike the programmer!"
```

This sets the error state and triggers the current error mode.

### Error Object

The current error is exposed as a built-in object.

```basic
if error then
    print error.message
    print error.line
    print error.column
    print error.code
    print error.source
end if
```

### Clearing Errors

Errors persist until cleared.

```basic
error.clear()
```

There is no `on error clear` statement.

### Error Replacement

If a new error occurs while an error is already active, the new error replaces the old error.

### Function Propagation

If a function exits while an error is active, the error propagates into the calling scope.

---

## 14. Error Handling Interactions

### Watchers

Errors inside watchers:

- set the error state
- respect the current error mode
- should not crash the program if `on error resume next` is active

### File Locks

All file locks must be released even if an error occurs.

`with lock(...)` must guarantee unlock.

### Functions

`on error goto` handlers are scoped to the current function or top-level program block.

Unhandled errors propagate outward.

---

## 15. Library System

gBASIC uses one source extension:

```text
.bas
```

A source file may contain a program, one or more libraries, or both.

### Program

```basic
program myprog(args)
    print "Hello"
end program
```

### Library

```basic
library dates
    function endofmonth(d)
        ...
    end function
end library
```

Multiple libraries may exist in the same `.bas` file.

---

## 16. Using Libraries

Explicit use:

```basic
use dates
```

Explicit file path:

```basic
use logging from "libs/logging.bas"
```

The library name inside the file does not necessarily need to match the file name when `from` is used.

---

## 17. Library Auto-Discovery

If an undefined function or modifier is found, the compiler may search for a matching library.

Search order:

1. Current file
2. Explicitly used libraries
3. Current folder
4. Subfolders
5. Standard library folder identified by `GBASIC_PATH`

For auto-discovery:

- first match wins
- later matches produce warnings
- warnings should identify all later candidates

This supports local override of standard libraries while still warning about shadowing.

---

## 18. Compiler Assistance for Uses

The compiler may support:

```bash
gbasic --add-uses myprog.bas
```

Behavior:

- scan unresolved functions and modifiers
- search local folder, subfolders, then `GBASIC_PATH`
- insert appropriate `use` statements near the top of the program block
- warn if later candidates exist
- avoid changing behavior when ambiguity cannot be resolved cleanly

Example:

Before:

```basic
program myprog(args)
    print endofmonth(today)
end program
```

After:

```basic
program myprog(args)
    use dates

    print endofmonth(today)
end program
```

---

## 19. File Lock Safety

File locks must be tracked by the runtime.

Requirements:

- every successful `lock(f)` registers the lock
- `unlock(f)` removes it from the registry
- `with lock(f)` unlocks when the block completes
- all remaining locks are released on normal program exit
- cleanup is registered with `atexit()`
- POSIX signal handlers should attempt cleanup for:
  - SIGINT
  - SIGTERM
  - SIGHUP

On Linux/POSIX systems, closing file descriptors releases advisory `flock` locks.

---

## 20. Implementation Priorities

Recommended order:

1. Error handling foundation
2. User-defined modifiers
3. Program/library wrappers
4. `use` and explicit library loading
5. Modifier export/import
6. Library-name-qualified modifiers
7. Auto-discovery
8. `--add-uses`
9. Real money type
10. C emitter

---

End of Addendum

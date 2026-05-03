# gBASIC v0.1 Core Design and Grammar Draft

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

## 2. Design Principles

gBASIC should be:

- readable to non-specialists
- expressive without ceremony
- context-sensitive where that reduces syntax
- extensible without polluting the general namespace
- permissive enough to preserve BASIC's spirit
- structured enough to support serious software

The key design idea is:

```text
meaning is applied through context, not encoded in special literal glyphs
```

Therefore v0.1 does **not** use special glyphs for money, dates, times, or file references.

Instead of:

```basic
balance = $19.95
start = @2026-05-15
f = #"data.txt"
```

gBASIC uses:

```basic
balance (USD)= 19.95
start (date)= "2026-05-15"
f (file)= "data.txt"
```

---

## 3. Core Syntax Style

A simple program:

```basic
name = "Joe Barnes"
balance (USD)= 19.95
start (date)= "2026-05-15"

if name(caseless)= "joe barnes" then
    print "Name matched"
    print balance
end if
```

Spacing around modifier parentheses is not semantically important.

These should be equivalent:

```basic
name(caseless)= "joe"
name (caseless)= "joe"
name(caseless) = "joe"
name (caseless) = "joe"
```

The parser resolves the construct based on context.

---

## 4. Comments

Single-line comments use an apostrophe:

```basic
' This is a comment
print "Hello"
```

A later version may also support:

```basic
// This is also a comment
```

but v0.1 should begin with the apostrophe form.

---

## 5. Values and Core Types

Initial built-in value types:

- number
- string
- boolean
- array
- record
- money
- date/time
- duration
- file reference

Strings are Unicode.

Booleans:

```basic
ok = true
done = false
```

---

## 6. Assignment

Basic assignment:

```basic
x = 10
name = "Ada"
```

Typed or interpreted assignment uses an operator modifier:

```basic
balance (USD)= 19.95
dob (date)= "1970-06-11"
config (file)= "/etc/app/config.txt"
```

Assignment targets may be:

```text
variable
record field
array element
```

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

## 7. Operator Modifiers

Operator modifiers are parenthesized terms that appear adjacent to an operator.

They modify the meaning of assignment or comparison.

Examples:

```basic
balance (USD)= 19.95
name (caseless)= "joe"
invoice.total (rounded 2)= expected
due (wednesday after)= today
```

### Modifier Namespace

Modifier terms do not pollute the general namespace.

This is legal:

```basic
caseless = "ordinary variable"

if name(caseless)= "joe" then
    print "match"
end if
```

Inside `(caseless)=`, `caseless` is resolved in the modifier namespace.

Outside that context, `caseless` is an ordinary identifier.

### Contexts

Modifiers may be defined for:

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

---

## 8. Custom Modifiers

Custom modifiers extend the language without adding new global keywords.

Comparison modifier:

```basic
modifier caseless for compare
    return lower(left) = lower(right)
end modifier
```

Assignment modifier:

```basic
modifier USD for assign
    return money(value, "USD")
end modifier
```

Date modifier:

```basic
modifier date for assign
    return parsedate(value)
end modifier
```

Weekday modifier:

```basic
modifier wednesday after for assign
    return nextweekday(value, "Wednesday")
end modifier
```

Parameterized modifier:

```basic
modifier rounded(n) for compare
    return round(left, n) = round(right, n)
end modifier
```

Use:

```basic
if amount(rounded 2)= expected then
    print "close enough"
end if
```

---

## 9. Comparison Operators

Initial comparison and logic operators:

```text
=   equal
!=  not equal
>   greater than
<   less than
>=  greater than or equal
<=  less than or equal
!>  not greater than
!<  not less than
and
or
not
```

Parentheses may group conditions:

```basic
if (x > 10 and y < 20) or not ready then
    print "condition met"
end if
```

The `=` operator is context-sensitive:

```basic
x = 10              ' assignment
if x = 10 then      ' comparison
```

---

## 10. Control Flow

### If

```basic
if x > 10 then
    print "large"
else
    print "small"
end if
```

### For

```basic
for i = 1 to 10
    print i
end for
```

Optional step:

```basic
for i = 10 to 1 step -1
    print i
end for
```

### While

```basic
while x < 10
    x = x + 1
end while
```

### For Each

```basic
scores = [88, 91, 91, 74]

for score in scores
    print score
end for
```

---

## 11. Functions

Function definition:

```basic
function add(a, b)
    return a + b
end function
```

Call:

```basic
x = add(2, 3)
```

Functions are not assignable.

---

## 12. Labels, Goto, and Gosub

Labels are permitted.

```basic
function example(x)
start:
    if x < 0 then goto finish

    print x
    return x

finish:
    print "finished"
    return 0
end function
```

`goto` and `gosub` are allowed, including for cleanup-style patterns.

They are scoped to the current function.

A `goto` or `gosub` may not jump into or out of another function.

Example:

```basic
function process(order)
    if not order.valid then goto cleanup

    gosub charge
    gosub ship

cleanup:
    print "ending process"
    return true

charge:
    print "charging"
    return

ship:
    print "shipping"
    return
end function
```

---

## 13. Arrays

Array literal:

```basic
scores = [88, 91, 91, 74, 100]
```

Indexing:

```basic
print scores[0]
scores[1] = 95
```

Length:

```basic
print len(scores)
```

Aggregates:

```basic
print sum(scores)
print mean(scores)
print median(scores)
print mode(scores)
print min(scores)
print max(scores)
```

No separate `count` function is needed because `len` covers this role.

---

## 14. Records

Records are named-field data objects.

Inline record:

```basic
customer = {
    name = "Ada",
    age = 37,
    balance = 120.00
}
```

Declared record shape:

```basic
dim customer as { name, age, balance, dob }
```

Field access:

```basic
customer.name = "Ada"
print customer.name
```

Dynamic access:

```basic
field = "name"
print customer[field]
```

Records should provide JavaScript/Python-like usability while retaining BASIC readability.

---

## 15. Money

Money is created through assignment modifiers.

```basic
balance (USD)= 19.95
payment (USD)= 5.00

balance = balance - payment
```

v0.1 supports USD.

Future versions may support:

```basic
price (EUR)= 19.95
amount (GBP)= 10.00
```

and possibly Unicode currency symbols through modifiers, not literal glyphs.

---

## 16. Date and Time

Dates and times are created through modifiers.

```basic
start (date)= "2026-05-15"
meeting (date)= "2026-05-15 15:30"
moment (time)= "15:30:02"
```

Date/time values should preserve precision.

A date with only year-month-day has day precision.

A date with hour and minute has minute precision.

### Precision-Aware Comparison

Comparisons use the lowest precision of the compared values.

Example:

```basic
d1 (date)= "2026-05-15"
d2 (date)= "2026-05-15 12:05:03"

if d1 = d2 then
    print "same day"
end if
```

This should match because the lowest precision is day.

Other examples:

```basic
a (date)= "2026-05"
b (date)= "2026-05-22"

if a = b then
    print "same month"
end if
```

### Date/Time Support Functions

Initial date/time functions may include:

```basic
dayname(d)
endofmonth(d)
startofweek(d)
endofweek(d)
daysbetween(a, b)
nextweekday(d, "Wednesday")
nextbusinessday(d, calendar)
```

---

## 17. Durations

Durations are first-class values.

```basic
delay = 1 hour 20 minutes 2 seconds
newtime = oldtime + delay
```

Durations may contain:

```text
years
months
weeks
days
hours
minutes
seconds
milliseconds
```

Calendar-relative durations must be distinguished from exact durations.

Example:

```basic
a = start + 1 month
b = start + 30 days
```

These are not always equivalent.

---

## 18. Files

Files are represented through file-reference values created by modifiers.

```basic
mypath = "/path/to/data.txt"
datafile (file)= mypath
otherfile (file)= "/path/to/otherfile.dat"
```

A file reference is not an open handle.

It is a typed reference to a filesystem location.

### File Operations

```basic
text = read(datafile)
write(datafile, "new text")
append(datafile, "more text")
```

### File Properties and Helpers

```basic
if exists(datafile) then
    print bytes(datafile)
    print chars(datafile)
    print lines(datafile)
end if
```

Possible properties:

```basic
datafile.path
datafile.name
datafile.parent
datafile.extension
datafile.created
datafile.modified
datafile.accessed
datafile.exists
datafile.type
```

### Locking

Both explicit and block locking should be supported.

Explicit:

```basic
lock(datafile)
write(datafile, "safe update")
unlock(datafile)
```

Safer block form:

```basic
with lock(datafile)
    write(datafile, "safe update")
end with
```

The block form should guarantee unlock even if an error occurs.

---

## 19. Directories

Directories may also be file-reference-like values or a separate directory type.

Possible v0.1 approach:

```basic
folder (dir)= "/home/matthew/docs"

for item in list(folder)
    print item.name
end for
```

Directory helpers:

```basic
list(folder)
files(folder)
folders(folder)
```

---

## 20. Watchers

Watchers are reactive code blocks that execute when watched variables change.

```basic
watch(a, b)
    c = a + b
end watch
```

Assignments inside watchers may trigger other watchers.

To suppress watcher execution safely:

```basic
without watchers
    a = 10
    b = 20
end without
```

This is preferred over manual on/off switches because the runtime can guarantee restoration.

### Watcher Use Cases

Watchers support:

- reactive state
- self-healing values
- validation
- UI binding
- workflow automation
- rule-like behavior

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

## 21. Standard Library v0.1

Initial functions:

### General

```basic
print
input
len
str
num
type
```

### Strings

```basic
lower
upper
trim
left
right
mid
replace
split
join
```

### Arrays

```basic
sum
mean
median
mode
min
max
sort
```

### Date/Time

```basic
dayname
endofmonth
startofweek
endofweek
daysbetween
nextweekday
nextbusinessday
```

### Files

```basic
read
write
append
exists
bytes
chars
lines
lock
unlock
list
files
folders
```

---

## 22. Implementation Plan

### Phase 1: Core Interpreter

- C implementation
- hand-written lexer
- Bison parser
- AST
- evaluator
- basic runtime values

Target program:

```basic
name = "Joe Barnes"
balance (USD)= 19.95

if name(caseless)= "joe barnes" then
    print balance
end if
```

### Phase 2: Runtime Types

- arrays
- records
- money
- date/time
- durations
- file references

### Phase 3: Watchers

- variable dependency tracking
- watcher registration
- watcher execution queue
- `without watchers` suppression

### Phase 4: C Emitter

Generate C from the AST.

```text
gBASIC -> AST -> C -> GCC -> executable
```

### Phase 5: GCC Integration

After the language and runtime stabilize, investigate direct GCC frontend integration.

---

## 23. Initial Grammar Draft

This grammar is intentionally preliminary. It is meant to guide the first Bison parser.

### Lexical Tokens

```text
IDENT
NUMBER
STRING

IF
THEN
ELSE
END
FOR
TO
STEP
WHILE
FUNCTION
RETURN
PRINT
DIM
AS
WATCH
WITHOUT
WATCHERS
MODIFIER
GOTO
GOSUB
WITH
TRUE
FALSE

OP_EQ        =
OP_NE        !=
OP_GT        >
OP_LT        <
OP_GE        >=
OP_LE        <=
OP_NGT       !>
OP_NLT       !<

PLUS         +
MINUS        -
STAR         *
SLASH        /
LPAREN       (
RPAREN       )
LBRACKET     [
RBRACKET     ]
LBRACE       {
RBRACE       }
COMMA        ,
DOT          .
COLON        :
NEWLINE
EOF
```

Keywords are context-sensitive where possible.

Modifier terms may be parsed from identifiers inside modifier parentheses.

---

## 24. Bison-Style Grammar Sketch

### Program

```text
program
    : statement_list EOF
    ;
```

### Statement Lists

```text
statement_list
    : statement
    | statement_list statement
    ;
```

### Statements

```text
statement
    : assignment NEWLINE
    | print_statement NEWLINE
    | if_statement
    | for_statement
    | while_statement
    | function_definition
    | return_statement NEWLINE
    | label_definition NEWLINE
    | goto_statement NEWLINE
    | gosub_statement NEWLINE
    | watch_statement
    | without_watchers_statement
    | modifier_definition
    ;
```

### Assignment

```text
assignment
    : lvalue OP_EQ expression
    | lvalue modifier OP_EQ expression
    ;
```

### LValues

```text
lvalue
    : IDENT
    | expression DOT IDENT
    | expression LBRACKET expression RBRACKET
    ;
```

A semantic check should reject function calls as assignment targets.

### Modifier

```text
modifier
    : LPAREN modifier_terms RPAREN
    ;
```

```text
modifier_terms
    : modifier_term
    | modifier_terms modifier_term
    ;
```

```text
modifier_term
    : IDENT
    | NUMBER
    | STRING
    ;
```

This allows:

```basic
name(caseless)= "joe"
amount(rounded 2)= expected
due(wednesday after)= today
```

### Expressions

```text
expression
    : primary
    | expression PLUS expression
    | expression MINUS expression
    | expression STAR expression
    | expression SLASH expression
    | expression comparison_operator expression
    | expression AND expression
    | expression OR expression
    | NOT expression
    | LPAREN expression RPAREN
    ;
```

### Comparison With Modifier

```text
expression
    : expression modifier comparison_operator expression
    ;
```

This supports:

```basic
if name(caseless)= "joe" then
```

### Comparison Operators

```text
comparison_operator
    : OP_EQ
    | OP_NE
    | OP_GT
    | OP_LT
    | OP_GE
    | OP_LE
    | OP_NGT
    | OP_NLT
    ;
```

### Primary Expressions

```text
primary
    : NUMBER
    | STRING
    | TRUE
    | FALSE
    | IDENT
    | function_call
    | array_literal
    | record_literal
    | primary DOT IDENT
    | primary LBRACKET expression RBRACKET
    ;
```

### Function Calls

```text
function_call
    : IDENT LPAREN argument_list_opt RPAREN
    ;
```

### Arguments

```text
argument_list_opt
    :
    | argument_list
    ;
```

```text
argument_list
    : expression
    | argument_list COMMA expression
    ;
```

### Arrays

```text
array_literal
    : LBRACKET argument_list_opt RBRACKET
    ;
```

### Records

```text
record_literal
    : LBRACE record_field_list_opt RBRACE
    ;
```

```text
record_field_list_opt
    :
    | record_field_list
    ;
```

```text
record_field_list
    : record_field
    | record_field_list COMMA record_field
    ;
```

```text
record_field
    : IDENT OP_EQ expression
    ;
```

### Print

```text
print_statement
    : PRINT expression
    ;
```

### If

```text
if_statement
    : IF expression THEN NEWLINE statement_list END IF NEWLINE
    | IF expression THEN NEWLINE statement_list ELSE NEWLINE statement_list END IF NEWLINE
    ;
```

### For

```text
for_statement
    : FOR IDENT OP_EQ expression TO expression NEWLINE statement_list END FOR NEWLINE
    | FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list END FOR NEWLINE
    | FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE
    ;
```

### While

```text
while_statement
    : WHILE expression NEWLINE statement_list END WHILE NEWLINE
    ;
```

### Function Definition

```text
function_definition
    : FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE
    ;
```

### Parameters

```text
parameter_list_opt
    :
    | parameter_list
    ;
```

```text
parameter_list
    : IDENT
    | parameter_list COMMA IDENT
    ;
```

### Return

```text
return_statement
    : RETURN
    | RETURN expression
    ;
```

### Labels

```text
label_definition
    : IDENT COLON
    ;
```

### Goto and Gosub

```text
goto_statement
    : GOTO IDENT
    ;
```

```text
gosub_statement
    : GOSUB IDENT
    ;
```

### Watch

```text
watch_statement
    : WATCH LPAREN watch_list RPAREN NEWLINE statement_list END WATCH NEWLINE
    ;
```

```text
watch_list
    : lvalue
    | watch_list COMMA lvalue
    ;
```

### Without Watchers

```text
without_watchers_statement
    : WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE
    ;
```

### Modifier Definition

```text
modifier_definition
    : MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE
    ;
```

```text
modifier_signature
    : IDENT
    | IDENT LPAREN parameter_list RPAREN
    | modifier_signature IDENT
    ;
```

This supports multi-word modifiers such as:

```basic
modifier wednesday after for assign
```

### Modifier Context

```text
modifier_context
    : IDENT
    ;
```

Expected initial context identifiers:

```text
assign
compare
```

---

## 25. Parser Notes

The parser should not rely on spaces to distinguish modifiers from function calls.

The following should parse identically:

```basic
name(caseless)= "joe"
name (caseless)= "joe"
name(caseless) = "joe"
name (caseless) = "joe"
```

The disambiguation rule:

```text
If a parenthesized term follows a valid lvalue or expression and precedes an assignment or comparison operator, it is parsed as an operator modifier.
Otherwise, it is parsed normally as a function call or grouped expression.
```

A semantic pass should verify that the modifier exists in the proper context.

---

## 26. Open Design Questions

1. Should comments support both apostrophe and `//` in v0.1?
2. Should file references and directory references be one type or separate types?
3. Should date/time parsing be strict ISO-like formats first?
4. Should modifier chaining be allowed in v0.1?
5. Should user-defined modifiers be interpreted only, or compiled into generated C in the first compiler phase?
6. Should `gosub` use a separate return stack from function calls?
7. Should watchers run immediately, at end of statement, or in a queued event loop?
8. Should watcher cycles be detected automatically?
9. Should record fields be declared or dynamically grow by default?

---

## 27. v0.1 Target Program

The first successful implementation should run this:

```basic
name = "Joe Barnes"
balance (USD)= 19.95
dob (date)= "1970-06-11"
config (file)= "data.txt"

if name(caseless)= "joe barnes" then
    print "matched"
    print balance
end if

scores = [88, 91, 91, 74, 100]
print mean(scores)
print mode(scores)
```

A second target should test watchers:

```basic
a = 10
b = 20

watch(a, b)
    c = a + b
end watch

a = 15
print c
```

A third target should test file references:

```basic
path = "data.txt"
f (file)= path

if exists(f) then
    print lines(f)
    print read(f)
end if
```

---

End of v0.1 Draft

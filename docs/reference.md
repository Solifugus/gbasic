# gBASIC Reference

This reference describes the implemented v0.1 language surface. gBASIC is pre-1.0: what is documented here works and is tested, and the surface may still change before 1.0.0 — the [CHANGELOG](../CHANGELOG.md) records what moves and why.

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
- `\r` carriage return, so a CRLF line ending is `"\r\n"` *(after 0.10.0)*
- `\f` form feed, the page break of a print-image report *(after 0.10.0)*
- `\\` backslash
- `\"` double quote
- `\u{...}` Unicode codepoint by hex value, e.g. `\u{1F600}` → `😀` (range
  `1..0x10FFFF`, excluding surrogates; for a literal NUL use `chr(0)`)

An unterminated string is a lexer error and exits nonzero. An unknown escape
(`\d`, say) keeps both characters and warns, so a regex can be written in an
ordinary string; write `\\d` to say the backslash is meant.

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

**A trailing operator does not continue a line.** Only an open bracket does, so
this is a parse error reported as `unexpected NEWLINE`:

<!--fragment: deliberately does not parse -- it is the mistake being described-->
```basic
message = "total: " + string(n) +
          " items"                    ' parse error
```

Parenthesise the expression instead — the open `(` is what carries it over:

```basic
message = ("total: " + string(n) +
           " items")
```

This is the commonest way to meet the rule, because a long string
concatenation is exactly where a continuation is wanted and a trailing `+` is
what most authors reach for first.

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

Keywords are matched **case-insensitively**. There are 49, and these 47 cannot
be used as a variable, parameter or function name:

```text
and       as        break     consider  continue  dim       do        each
else      error     excluding export    false     for       function
gosub     goto      if        in        intersecting        library   load
modifier  new       not       nothing   on        or        print
program   return    spawn     step      stop      then      to        true
try       unknown   until     unwatch   use       watch     watchers  while
with      without
```

`try` arrived after 0.10.0 as the expression form of catching (see *Error
handling*). It was measured first: in this tree two files used it as a name, a
local variable in `stdlib/gpdf.bas` and a helper function in the G-SQL cookbook's
constraints recipe, and both were renamed; no consumer project on the machine
used it. Like every keyword it is still a record field name (`r.try`).

`excluding` and `intersecting` arrived in 0.6.0 as the set operators, and the
price was measured before it was taken: no identifier anywhere in this tree was
named either (the only three matches were in prose comments), and the infix form
costs **zero** grammar conflicts, which is what decided it over a function form —
this project rejected `IDENT expression` as a statement form over four.

The other two of the 48 — **`end` and `next`** — are recovered as identifiers by
the grammar where a *name being read or assigned* is expected, so they may be
used as **variables**. That is why closing a `for` with `next` costs no reserved
word (see Statements).

**They may not be a parameter or a function name.** That sentence used to read
"may be used as ordinary names", which is the broader claim and is false.
Measured against 0.7.0, both words in all four positions:

| | variable | record field | parameter | function name |
|---|---|---|---|---|
| `end` | yes | yes | **no** | **no** |
| `next` | yes | yes | **no** | **no** |

`function end(a)` answers *`'end'` is a reserved word and cannot be used as a
name* — the page contradicted at the one place a reader would act on it. The
record-field column is not special to these two: every keyword is legal there,
as the paragraph after the next one says.

Both halves of this were wrong at 0.6.0 and 0.7.0, in opposite directions, and
each was found by a different route. The LIST had drifted: it held the 46 that
cannot be names from the day it was written (44 at 0.5.1, and `end`/`next`
absent), and 0.6.0's edit added **four** words for two new operators, so it
listed 48 under a sentence about 46. Found by the session re-pinning *Volume 2*,
whose appendix generator compares the count the page STATES against the words it
LISTS and refused to run — not by reading, because the sentence and the list are
each plausible alone. The SENTENCE about the exception was separately too broad,
which only measuring all four positions shows. `tests/run_docs_gate.sh` now
checks the list plus the two exceptions against `src/lexer.c` as a set, so
neither can drift again. `loop` is not in either list:
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

for each item, i in items          ' the element AND its position, 0-based
    items[i] = tagged(item)
end for

for each line in extract            ' a FILE: streamed, one line at a time
    take(line)
end for
```

#### Streaming a file — `for each line in f`

*(since 0.10.0)* When the thing iterated is a **file reference**, the loop
**streams** it: one line at a time, holding only the longest line rather than
the file. Measured on a 161 MB, 4-million-line CSV — **13.3 MB** resident
streaming it against **750 MB** for `read_lines` over the same file, and flat
across a 21× range of input where the whole-file route is proportional. A 10 GB
extract is ordinary rather than impossible.

```basic
for each line, n in extract         ' `n` is the LINE NUMBER, 0-based
    if not parses(line) then
        print to error "line " + string(n) + " will not parse"
    end if
end for
```

**The loop owns the handle**, which is why there is no `open`/`close` to pair up.
It is closed when the loop ends, on `break`, on a `break` or `continue` naming an
*outer* loop, on a `return` from inside it, and when a raise passes through — all
six asserted under `valgrind --track-fds`. And there is no end-of-input sentinel
to misread, which is a real trap in this language: `input()` answers `""` for
both a blank line and end of input with no way to tell them apart.

**A read error raises** rather than ending the loop. A truncated read reported as
end-of-file is the file-reading form of *"a directory that cannot be read is not
an empty one"*, and for an ETL job it is the difference between a failed load and
a silently short one. An absent file raises too.

**A line is the same line `read_lines` would give** — split on `\n`, one trailing
`\r` dropped so a CRLF file needs no mode, and a final line without a newline is
still a line. The two are independent implementations and
`tests/run_file_stream.sh` asserts they agree over twelve shapes, including an
interior NUL and a line longer than the read buffer.

Writing to the element is **not** reported here as it is for an array
(warning `2107`), because that warning's remedy is `list[i] = item` and a stream
has no `list` to write back to — advice that cannot be followed is worse than
silence.

#### Reading a delimited file — `for each item, at in csv.records(f)`

*(after 0.10.0)* **Never split a CSV line on commas.** A quoted field holds commas,
quotes and line breaks, so a record is not a line: `split` moves every later field
into the wrong column and makes two records out of one, with no error.
`csv.records(f, options)` reads the format properly (RFC 4180, plus what real files
do: CRLF, a lone CR, a UTF-8 byte order mark) and **streams**, holding one record
rather than the file — measured flat at 31 MB over 40,000 and 400,000 records.

```basic
for each item, at in csv.records({file}"catalog.csv")
    if not at.ok then
        print to error "line " + string(at.line) + ": " + at.problem
        continue
    end if
    take(item.sku, item.price)
end for
```

- **The element is a record of text** keyed by the header's names, as written
  (not trimmed, not lowered). With `header: false` it is an array of text. Nothing
  is typed: `"25"` stays text, and the program or G-SQL decides what it is.
- **The second name is where the record came from**: `{ line, offset, record, ok,
  problem }`. `line` is the line the record **starts** on counted from **1**, as an
  editor counts, since a record can span lines — unlike `n` in
  `for each line, n in f`, which counts from 0. `offset` is the byte, from 0;
  `record` the data record's number, from 1.
- **A record that does not fit raises by default**, naming its line:
  `csv.records: line 14 (record 13) has 4 fields where the header has 3`, with
  `error.details.line`, `.offset` and `.record`. A short record is never padded and
  a long one never dropped. Text after a closing quote (`"abc"def`) is the same kind
  of problem. With `problems: "deliver"` such a record reaches the loop instead,
  `at.ok` false, `at.problem` saying why and `at.fields` holding every field read;
  the element then lacks the missing names, so reading one raises.
- **An empty field is not a missing one**: `a,,c` has a middle field `""`.
- An unterminated quote always raises, naming the line it opened on, and so does a
  record over 64 MiB (almost always the same mistake).
- **Options**, a closed set: `delimiter` (one character, default `","`), `quote`
  (default `"\""`, or `""` for none), `header` (default `true`), `names` (for a
  file with no header line), `problems` (`"raise"` or `"deliver"`).
- **The loop owns the file**, closed on every exit, as for `for each line in f`.
- **`csv.parse(text, options)`** is the same over text in memory, such as a web
  response body. It is a separate name because a string is a path to every file
  builtin.
- **Writing**: `csv.line(values, options)` answers one record's text, line ending
  included, quoting a field only when it holds the delimiter, the quote or a line
  break. `csv.write(f, rows, options)` writes a header (the first record's names,
  or `names:`) and the rows; a record with different names is refused naming its
  row. Options: `delimiter`, `quote`, `names`, `line_end` (`"\n"` or `"\r\n"`).
  `unknown` and `nothing` are written as an empty field. Money keeps every digit
  it holds (`1250.7512`), where `print` and `string()` show the currency's
  places (`1250.75`).

The design and its evidence are `docs/csv_design.md`; the suite is
`tests/run_csv.sh`.

#### The members of a ZIP file — `zip.members(f)`, `zip.read(f, name)`, `zip.extract(f, folder)`, `zip.write(f, members)`

Needs no `load`, and only zlib in the build. Partners often send several files
in one ZIP:

```basic
for each m in zip.members({file}"partner.zip")
    if not m.folder and extension(m.name) = ".csv" then
        for each item, at in csv.parse(zip.read({file}"partner.zip", m.name))
            print item
        end for
    end if
end for
```

- **`zip.members(f)`** lists every member, in archive order, as `{name, size,
  packed, method, modified, folder, link, encrypted, crc}`: `size` the bytes it
  holds, `packed` the bytes it takes in the archive, `method` `"store"`,
  `"deflate"` or the name of another (`"bzip2"`), `modified` a `datetime` (the
  archive's own local time, to two seconds). A name is UTF-8 when the archive
  says so and otherwise read as code page 437, which is what the format
  defines. Only the central directory is read, so listing a large archive costs
  its directory.
- **`zip.read(f, name)`** answers a member's bytes as text (NUL included),
  checked against its recorded size and CRC-32. Refused: no such member, a
  folder, **two members of that name** (which one is meant cannot be told), an
  encrypted member, a method but store and deflate, ZIP64 (an archive or member
  past 4 GB or 65,535 members), a spanned archive, and a member whose bytes do
  not match its CRC.
- **`zip.extract(f, folder, options)`** writes every member under `folder`,
  which must exist, making the folders on the way, and answers the files it
  wrote. **Every name is checked before anything is written**, so one unsafe
  member writes nothing: an absolute name, a `..` component, a backslash, a
  drive letter, an empty component, a symbolic link, and two members of one
  name are refused, each naming the member. A file already there is refused
  unless `replace: true`.
- **`zip.write(f, members)`** writes a ZIP holding `members`, a list of
  `{ name, data }` in archive order, `data` being the bytes as text. A name
  ending in `/` is a folder and holds no data. Each member is deflated, or
  stored when deflating would not shrink it. **The same members give the same
  bytes**: every member is dated 1980-01-01 00:00 and carries no extra fields,
  so two archives can be compared. It writes only what `zip.extract` would
  take: an unsafe name, two members of one name, a name that is not UTF-8, and
  anything needing ZIP64 are refused before a byte is written. A file already
  at `f` is replaced. To change one part of a workbook, read every member,
  change the one, and write them all:

```basic
parts = []
for each m in zip.members({file}"report.xlsx")
    body = zip.read({file}"report.xlsx", m.name)
    if m.name = "xl/worksheets/sheet1.xml" then
        body = replace(body, "<v>41</v>", "<v>42</v>")
    end if
    append(parts, { name: m.name, data: body })
end for
zip.write({file}"report2.xlsx", parts)
```

The suite is `tests/run_zip.sh`, whose answers come from Python's `zipfile`.

**`for each item, i in list` gives you the index**, and it is what makes a
write-back expressible. gBASIC has no references, so the loop variable is a
**copy**: `item.x = 1` mutates that copy and is silently discarded. `list[i]`
is an lvalue *path*, and paths write in place — so `list[i] = item` is how a
single pass rewrites what it walks. The index is an ordinary number in the
enclosing scope and survives the loop, exactly as the element and the counted
loop's counter do. The element and the index may not share a name.

**A write the loop never reads is reported** (warning `2107`). Because the
element is a copy, `item.x = 1` with nothing reading `item` afterwards is
discarded when the iteration ends — silently, which is the one failure in this
language that produces no diagnostic. Writing to the element and then *using*
it is ordinary and stays silent: `row = normalise(row)` then `print row.name`
warns about nothing, and neither does the write-back idiom, since `list[i] =
item` reads `item`. `on warning ignore` covers a deliberate discard.

**Writing to the array while walking it is safe.** Iteration is over a
snapshot: the array expression is evaluated once, and a write detaches the
variable's store through copy-on-write. So the walk sees the values the array
had at loop entry, and **appending inside the loop does not extend it** — a
single pass that rewrites or grows what it walks terminates and skips nothing.
That is a stronger guarantee than Python (where mutating during iteration skips
elements) or JavaScript (where it can loop forever).

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
no variable holds it any more. Watcher values cannot be encoded or sent to
another actor.

**A watch belongs to the scope that declares it** (*since 0.11.0*;
`docs/watch_scope_design.md`). At the top level it lives for the program. In a
function it watches that function's own variables, its body runs in that call
and sees its parameters and locals, and it ends when the function returns, by
any exit, a raise included. Each call has its own watch, and a callee's local
of the same name is the callee's:

```basic
function tally(items)
    total = 0
    seen = []
    watch(total)
        append(seen, total)     ' writes this call's `seen`
    end watch
    for each it in items
        total = total + it
    end for
    return seen                 ' tally([5, 7]) is [0, 5, 12]
end function
```

In a function, a watch may also watch an event source (`timer.ticks`,
`inbox.messages`, `http.events`, `net.events`, or a server held in one of its
locals). Anything else is refused (`1003`, naming it): another global (a
function cannot assign one, and it changes only while no function runs), or a
variable not yet assigned. A named watcher's handle that outlives its function
names a watch that has ended: reading its fields raises, and `unwatch` of it
does nothing.

**`wait until <condition> [for <limit>]`** (*since 0.11.0*) runs the event loop
from where it is written until the condition is true, so a function can keep
its watches alive while events arrive. Events are delivered to the watches of
every scope still running (the program's, and each function's on the way down),
and the condition is checked again after each pass of the loop:

```basic
function count_ticks(n)
    seen = 0
    t = timer.every(0.1)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            seen = seen + 1
        end while
    end watch
    wait until seen >= n for 10 seconds
    timer.cancel(t)
    return seen
end function
```

- **`for <limit>`** is a duration or a number of seconds. When it passes with
  the condition still false, the wait raises (`1003`). A month is refused, having
  no fixed length.
- **A wait that nothing could end raises** rather than hanging: no server
  listening, no transfer, no live timer, and no watched inbox or socket.
- **A wait inside a watch body** works, so a function that waits may be called
  from one. Past 16 such waits inside one another it raises, since a wait cannot
  be skipped: the code after it relies on its condition.
- **`wait` is a keyword only before `until`**, so `wait` stays usable as a
  variable or a record key.
- At the top level a wait is the same, which is how a program can wait for a
  condition before going on rather than leaving everything to the loop after
  the `program` block.

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

**When a program block exists, the top level is read only for declarations.**
Functions, modifiers, libraries, `load`, function values and `server` blocks
are registered before the block runs; an ordinary *statement* out there does
not run at all, and gBASIC now says so — one warning at the first such
statement (`2106`, source `dead code`), silenceable with `on warning ignore`.

The program still behaves exactly as it did, so this is a warning rather than a
refusal. It exists because the silence was expensive: building the AI stack this
trap was hit three times in one day, and the worst of them was a top-level
`watch(inbox.messages)`, which registered nothing, so every worker reply went
undelivered and every conversation hung with the symptom pointing nowhere near
the cause. `watch` cannot simply be hoisted with the declarations either — a
watcher *fires* on registration, and would run its body before anything it reads
had been assigned. Put it inside `program`.

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
x = try f(a)            ' one expression: the value, or nothing; never re-raised
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

**Across kinds, equality answers and ordering refuses** (*since 0.1.0*). A
string that is no number is never equal to a number — `0 = "stop"` and `0 = ""`
are **false** — and `1 > "stop"` **raises**, naming both kinds. The same rule
already governs arrays, records, functions, watchers, regexes, money and
date/time: `=` and `!=` are questions with answers, ordering is not a question
at all.

Text that is a number is no exception: `1 = "1"` is **false**. Because that is
the usual way a form value fails to match a stored id, it **warns** *(after
0.10.0)*: warning `2122`, when `=` or `!=` compares a number with text that
reads as one — trimmed of spaces, tabs and line ends, then a sign, digits, an
optional fraction and exponent, the reader G-SQL uses — naming both sides and
the remedy, `number(text)` or `{number}`. Text that is no number (`0 = "stop"`,
`x = ""`) is silent, and so are the "same value" routes, `contains`, `find`,
`unique` and `consider`. G-SQL is different on purpose: a query's column
declares its kind, so `WHERE id = '7'` reads the text
(`docs/gsql_compare_conversion_design.md`). See
`docs/number_text_equality_design.md`.

**`unknown` is three-valued** *(after 0.10.0)*. `unknown` is gBASIC's SQL
`NULL` — a fact nobody has — and it follows SQL's rules:

| | `true` | `false` | `unknown` |
|---|---|---|---|
| `not` | `false` | `true` | `unknown` |
| `unknown and` … | `unknown` | `false` | `unknown` |
| `unknown or` … | `true` | `unknown` | `unknown` |

`and` and `or` are commutative, so `unknown and false` is `false` exactly as
`false and unknown` is, and the right operand is skipped only when the left
**decides** the answer (`false and …`, `true or …`). A comparison or an
arithmetic operation with an `unknown` operand answers `unknown`:
`unknown = 1`, `unknown > 1`, `unknown + 1` and `-unknown` are all `unknown`,
and so is **`unknown = unknown`**. Three things deliberately do not follow SQL:

- **`if`, `while` and `until` refuse an `unknown`** with *unknown cannot be used
  as a condition*. SQL's `WHERE` silently drops a `NULL` row; gBASIC says so.
  So `if r["b"] = 1` with no `b` in `r` **raises**, where through 0.10.0 it was
  quietly `false`. Ask first: `if not is_unknown(r["b"]) and r["b"] = 1`, which
  the table above makes safe, since `false and …` never looks at the right.
  The commonest place to meet it is `env("NAME") = "..."`, because `env`
  answers `unknown` for a variable that is not set.
- **String `+` still renders it**: `"total: " + unknown` is
  `"total: unknown"`, because `+` on a string builds a message.
- **`nothing` stays two-valued**: `nothing = nothing` is `true` and
  `not nothing` is `true`. It is "no value here", not a missing fact.
  `unknown = nothing` is `unknown`, because one side is.

The "same value" routes below (`contains`, `find`, `unique`, `consider`, a
compound's `=`) are unchanged: `unknown` is the same value as `unknown` there,
so `contains([unknown], unknown)` and `[unknown] = [unknown]` are `true`.
`compare(a, op, b)` follows the operator. See `docs/unknown_nan_design.md`.

Numbers and booleans are the exception and coerce as they always have: `0 =
false` and `1 = true` are true, and `1 > false` orders. That is a real
coercion the language uses — measured at 1,472 of the 1,500 mixed-kind
comparisons in the test suite — where a string against a number was, at every
one of the remaining sites, a mistake.

Until this release a string compared against a number went through the numeric
fallthrough, where every string became `0` — so `0 = "stop"` was **true** while
`1 = "1"` was false, and `1 > "stop"` answered. `if input("n: ") = 0` was true
for any answer that was not a number.

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

1. unary `-`
2. `*`, `/`
3. `+`, `-`
4. comparisons — `=` `!=` `<` `>` `<=` `>=` (and the negated spellings `!<` `!>` `!<=` `!>=`)
5. `not`
6. `and`
7. `or`

`not` binds **looser than a comparison**, so `not a = b` means `not (a = b)` —
the same as in every other BASIC, and in Python, SQL and Pascal. It binds
tighter than `and`, so `not a and b` means `(not a) and b`.

Because `not` is not a unary operator, `- not x` does not parse. Write
`not -x`, or parenthesise.

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

String operations split into four families, and **every string operation is
named in one of them.** Which family an operation belongs to used to be
something a caller discovered by experiment, and that gap is what allowed
`replace` and `trim` to stop at an interior NUL for years while `len` and `mid`
did not. `find` appears twice on purpose: it *matches* by byte and *answers* in
codepoints.

- **Character-oriented (Unicode codepoints).** `len`, `left`, `right`, `mid`,
  `reverse`, `find`, `chr`, and `code` count and slice by **codepoint**, never
  splitting a multibyte character. `len("café")` is `4`. Indexing is 0-based,
  matching `mid` and arrays. A malformed UTF-8 byte degrades gracefully to one
  unit (these operations never error on arbitrary bytes).
- **Matching (byte sequence, codepoint-exact on valid UTF-8).** `contains`,
  `starts_with`, `ends_with`, `find`, `replace`, `split` and `compare` locate a
  needle by **comparing bytes**. On valid UTF-8 this is exactly codepoint
  matching and needs no special handling, because UTF-8 is self-synchronizing:
  a valid multi-byte sequence can only ever match at a codepoint boundary. See
  *Matching and invalid UTF-8* below for the one case where that does not hold.
- **Byte-preserving (structural).** `trim`, `join`, `repeat`, `upper`, `lower`
  and concatenation move bytes without interpreting them, so multibyte
  characters pass through unchanged. `trim` treats **ASCII whitespace only** —
  `U+00A0` and other Unicode spaces are content, not padding.
- **Byte-oriented (raw).** `byte_count`, `byte_at` (0-based), `byte_slice`,
  `byte_find`, `to_bytes`, `from_bytes` and `hex_encode` work on raw bytes for
  binary and protocol work.

```basic
len("café")                    ' 4   codepoints
byte_count("café")             ' 5   UTF-8 bytes
mid("café", 3, 1)              ' "é" never splits a codepoint
from_bytes([0, 255])           ' a two-byte binary string
```

**The two families do not mix indexes, and that is the thing to know.** A
codepoint index and a byte index are different numbers the moment a string
holds anything above ASCII:

```basic
s = "é" + "MARK"
find(s, "MARK")                ' 1   a CODEPOINT index
byte_find(s, "MARK")           ' 2   a BYTE index
byte_at(s, find(s, "MARK"))    ' 169 — the tail of the é, not "M"
byte_at(s, byte_find(s, "MARK"))  ' 77 — "M"
```

So when you are reading a binary format, locate with `byte_find` and read with
`byte_slice` or `byte_at`; the index one returns is the index the others take.
Mixing them yields the wrong bytes with nothing raised, which is why the byte
family has its own search rather than sharing `find`.

- `byte_slice(s, at [, count])` — bytes from `at`, to the end if no count.
  Past the end is empty and an over-long count is trimmed, the same forgiving
  shape `mid` has; a negative start is refused, since that is a mistake rather
  than an edge. It is a **slice**, not a loop: building the same bytes with
  `byte_at` and `from_bytes` copies the accumulator each time and is quadratic
  — measured at 8.3× the cost for 4× the bytes, where this is flat
  (`tests/run_byte_family.sh`).
- `byte_find(s, needle [, from])` — the byte index of `needle`, or `nothing`
  on a miss, as `find` answers. An **empty needle is refused**: it matches
  everywhere and answering 0 invites a scan that never advances.
- `to_bytes(s)` — an array of byte values 0–255, the inverse of `from_bytes`.
- `bytes_xor(data, key)` — `data` with each byte XORed against `key`, the key
  repeated; `key` must be at least one byte. WebSocket masking is this with a
  4-byte key, and it is native because a per-byte loop over a megabyte frame is
  not.
- `valid_utf8(text)` — whether the bytes are well-formed UTF-8 as RFC 3629
  defines it: no overlong forms, no surrogates (`U+D800`–`U+DFFF`), nothing past
  `U+10FFFF`, no sequence cut short. The other string builtins read malformed
  bytes leniently; this is the strict answer a protocol that must refuse them
  needs. Checked against Python's decoder (`tests/run_websocket.sh`).
- `first_invalid_utf8(text)` — the byte offset of the first sequence that is
  not valid UTF-8 (by the same strict rule), or `nothing` when the text is
  valid: what a reject report names. `first_invalid_utf8("ab" +
  from_bytes([255]))` is 2.
- `transcode(text, from, to)` — the bytes of `text`, read in encoding `from`,
  written in encoding `to`. Encodings: `utf-8`, `latin-1` (`iso-8859-1`),
  `windows-1252` (`cp1252`), `utf-16`, `utf-16le`, `utf-16be`, matched without
  case, hyphens or underscores. **Strict both ways, and nothing is replaced**:
  bytes the source encoding does not define raise with `error.details.offset`
  (the byte), and a character the target cannot hold raises with
  `error.details.character` (its index); `details.reason` is `invalid`,
  `truncated` or `unmappable`. `utf-16` read with no byte-order mark is
  big-endian (RFC 2781; Python reads the machine's order there), and written
  is `FF FE` then little-endian. Windows-1252's five undefined bytes (`0x81`,
  `0x8D`, `0x8F`, `0x90`, `0x9D`) are refused, as Python's codec does. Checked
  against Python's codecs over 4120 conversions (`tests/run_text_encoding.sh`).

```basic
raw {file}= "supplier.csv"
text = read(raw)
if not valid_utf8(text) then
    text = transcode(text, "windows-1252", "utf-8")   ' or report first_invalid_utf8(text)
end if
```

**Comparison** is by byte sequence (binary-safe, and correct codepoint order for
valid UTF-8). **Case folding** (`upper`, `lower`, and the `{caseless}` comparison
modifier) is **ASCII-only**: `A–Z` ↔ `a–z` fold and every other byte, including
all non-ASCII characters, is left exactly as-is (`upper("café")` is `"CAFÉ"` with
the `é` unchanged). Full Unicode case folding, normalization, and grapheme
clusters are future work.

**Literals** are UTF-8 from the source file. The escape `\u{...}` inserts a
codepoint by hex value (`"\u{1F600}"` is `"😀"`); for a literal NUL use `chr(0)`.

**Binary safety covers the whole string family, subject *and* pattern.**
`replace`, `split`, `trim`, `join`, `repeat`, `starts_with` and `ends_with` are
length-aware like `len`, `mid`, `contains` and `find`, and a needle or separator
carrying a NUL is an ordinary needle:

```basic
s = "a" + chr(0) + "b"
len(replace(s, "b", "B"))      ' 3   the match after the NUL is found
ends_with(s, "b")              ' true
starts_with("xy", chr(0))      ' false — a NUL needle is not a wildcard
count(split(s, chr(0)))        ' 2   a NUL is a usable separator
```

**A record field NAME is a string too, and is counted.** A dynamic key is the
one place a program puts arbitrary bytes into a name, and it holds them:

```basic
r = {}
r["a" + chr(0) + "b"] = 1
r["a" + chr(0) + "z"] = 2
count(keys(r))                 ' 2   two names, not one
has(r, "a")                    ' false — the prefix is not the name
```

Until rc9 a field name was read as a C string, so those two keys **collapsed
into one field named `a` holding `2`** — the second silently overwrote the
first and both subscripts read back the survivor. `keys`, `has`, `remove_key`,
`merge`, `encode`/`decode` and the actor boundary all carry the whole name now.
Where a name reaches something that genuinely cannot hold a NUL — an HTTP
header name, a known option name — it is **refused rather than truncated**, so
the byte the caller wrote is never quietly dropped at the far end.

`replace` and `split` still refuse a needle of **length zero**, which has no
first occurrence; that is not the same as one that begins with a NUL. Until
rc9 the matching and byte-preserving families read their arguments as C strings
and stopped at the first NUL — silently returning a shorter string, or the
wrong boolean — so this paragraph describes a guarantee that is now enforced by
`tests/run_string_nul.sh` rather than one the reference merely asserted.

**Matching and invalid UTF-8.** Because matching compares bytes, a needle that
is **not itself valid UTF-8** can match in the middle of a character:

```basic
starts_with("日本語", from_bytes([230]))       ' true  — 0xE6 is 日's lead byte
find("日本語", from_bytes([156]))              ' 2     — a codepoint index for
                                               '         byte 4, which is not a
                                               '         codepoint boundary
replace("日本語", from_bytes([156]), "!")      ' invalid UTF-8 out of valid in
```

This cannot happen with a needle that is valid UTF-8, which is every needle a
program writes as a literal. It is reachable only by constructing a fragment
with `from_bytes` or by slicing bytes apart deliberately — which is byte work,
and byte semantics is the right answer there. But note the consequence: when a
match lands mid-codepoint, **`find`'s codepoint index does not name a
boundary**, and a `replace` can produce invalid UTF-8 from valid input. If you
are matching untrusted bytes rather than text, work in `byte_at`/`from_bytes`
and check validity yourself.

## First-Class Functions

**Scope, stated plainly:** a function body reads enclosing variables but cannot
rebind them — an assignment to a name that is not already local creates a **new
function-local**, and the outer variable keeps its value. To share mutable
state with a function or a handler, keep it in a record and mutate its fields
(`state.count = state.count + 1`), which persists because the record is reached
**by name** from the enclosing scope. This is about the *name*, not the record:
a record passed as an **argument** is a copy, so `function bump(r)` assigning
`r.count` changes the caller's record not at all. Return the modified record and
assign it, or reach the record by name.

**Arrays work the same way, and `append` is the common case.** Reached by name,
a mutation reaches the enclosing array; passed as an argument, it does not:

```basic
function grows()
    append(g, 3)          ' by NAME -- the enclosing g grows
    return count(g)
end function

function takes(xs)
    append(xs, 99)        ' an ARGUMENT -- a copy; the caller's array is untouched
    return count(xs)
end function
```

The boundary is *mutation versus assignment*, not arrays versus records.
`append(g, 3)` writes through the name and reaches the outer array; `g = [9]`
inside the same function creates a function-local and leaves the outer one
alone, exactly as it would for a number. Because the broken form *looks* right and fails silently, the
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
method binds `this`). An lvalue receiver
(a variable/field/index chain) binds `this` to the live cell, so a record method's
`this.x = …` writes through exactly as for `obj.method()`; a call-result receiver
binds `this` to the temporary. Chained method calls never form an lvalue, so
`a.b().c = x` remains invalid. (One narrow limitation: a method call on a
call-result receiver as a *bare statement* — `make_widget().show()` with the result
discarded — does not parse; use it in expression position, or bind the result to a
variable first.)

**A handle's methods are its module's verbs** (`docs/handle_methods_design.md`):
`h.verb(args)` is `module.verb(h, args)`, for every handle kind: a G-SQL
connection or `sql` block (`gsql`), a `window` (`qt`), a `sqlite`, `pg`,
`odbc` or `ldap` connection, an `xml` reader, a process, an `http` handle and
an `xlsx` workbook. A timer is a plain record and is not yet one:

<!--fragment: an API shape or a deliberately invalid form, not a program-->
```basic
r = add.exec(row)            ' gsql.exec(add, row)
for each s in shares.query()  ' gsql.query(shares)
b = db.begin()                ' gsql.begin(db)
t = app.read("who", "text")   ' qt.read(app, "who", "text")
s = p.wait()                  ' process.wait(p)
```

The module form keeps working; the two are one call. The methods are derived
from the module, not listed, so the module decides what each verb does and a
verb whose first argument is not the handle (`db.connect()`) is refused by that
verb. A record's own function field wins over a module verb of the same name,
and a variable named like a module (`gsql`, `qt`) is a receiver, not the module.

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

## Typed variables

*(added 2026-11-03; [typed_variables_design.md](typed_variables_design.md))*
A name followed by a colon and a data type **locks** the variable to that type.
Every later plain `=` converts into the type, or is refused:

```basic
qty:number = "42"         ' 42, and qty is locked to number
qty = " 5 "               ' 5: converted, as {number} converts
cost:money(USD) = "1.25"
when:date = "2026-01-02"
qty:text = "many"         ' redeclaring relocks; the change is written out
```

The types are `number`, `string` (or `text`), `boolean`, `date`, `time`,
`datetime`, `duration`, `money(USD)` (any currency code), `file` and `dir`.
One type, no chain: a lens such as `{trimmed}` stays in braces and applies
once. The conversion **is** the brace modifier of the same name, so `x:number`
and `{number}` cannot disagree. `boolean` takes `true`, `false`, or the text
`"true"`/`"false"` in any case; `duration` takes only a duration; a
`money(USD)` lock refuses money in another currency, since that conversion
needs a rate.

- **A refusal names the lock:** `qty is locked to number by qty:number at line
  1, and this value does not convert: ...`. The old value is kept.
- **`nothing` and `unknown` are allowed**, as in a typed G-SQL column.
- **A `date` lock refuses a time of day** (*since 0.11.0*), as a G-SQL `date`
  column does: `d:date = at` with `at` at 14:20 is refused, naming the time,
  where it used to take the date in silence. Midnight converts, and
  `{date}at` takes the date when that is what is meant.
- **A compound assignment converts a text operand first**, because `42 + "1"`
  is `421`: with `n:number = 42`, `n += "1"` is 43. Other operands go in as
  written, so `d += 3 days` on a locked date is date arithmetic.
- **A comparison reads text as the locked type**: with `x:number`, `x = "1"` is
  true and warning 2122 does not fire. Text that does not convert is refused.
  This is the case `1 = "1"` stays false for: a bare value declares no kind,
  and a lock does.
- **The lock belongs to the variable in its own scope.** A function's local of
  the same name is a different variable; passing `x` passes its value, not its
  lock.
- **A loop does not bind a locked variable**, except a number lock on a
  `for` counter or a `for each` index, which only ever hold numbers.
- **A misspelled or foreign type is a parse error with the gBASIC name**:
  `x:int` gives `'int' is not a gBASIC type; write x:number`, and likewise
  `str`, `bool`, `float`, `timestamp` and a bare `x:USD`.

**A parameter can carry its own lock, and a function can declare its return
type:**

```basic
function line_total(qty:number, price:money(USD) = "0.00"):money(USD)
    return price * qty
end function

print line_total("3", "1.25")     ' 3.75: each argument converts
```

- **An argument converts into its parameter** as an assignment would, and
  so does a default. A refusal is raised **at the call**, where the wrong
  value was passed. The parameter stays locked in the body.
- **Every `return` with a value converts into the return type**, and a
  refusal is raised **at that `return`**: `f returns number, declared by
  f(...):number at line 3, and this value does not convert: ...`. `nothing`,
  `unknown`, a bare `return` and falling off the end are all allowed. Both
  `function f(...)` and `function obj.method(...)` take one.
- **Only a function's parameters can be locked.** A modifier, `program`
  block or server handler parameter is supplied by the runtime, so a lock
  there is refused at load, as a default is.

## Modifiers

Modifier use:

```basic
x{USD}= 19.95
d {date}= "2026-03-15"
p {trimmed}= "  padded  "
```

Three of the four examples this block used to show **did not run** (corrected
2026-10-02, reported by the gbasic-books session, which had a drafted chapter
carrying a caution about it): `name {caseless}= "joe"` raises, because `caseless`
is a comparison **lens** rather than an assignment modifier and the interpreter
says so; and `rounded` names a modifier that exists **nowhere** — not in the
stdlib, not in the interpreter — so both `{rounded 2}` and
`{math.rounded to 2}` raise `assign modifier not found`.

Assignment modifiers transform assigned values. Comparison modifiers transform or
implement comparisons. Both are written in **braces**; the parenthesized spelling
was removed in 0.1.0-rc6 and is now a parse error (see the CHANGELOG for the
migration).

An assignment modifier's target may be a variable, a record field, or an array
element:

```basic
r.when {date}= "2026-01-01"
a[0] {USD}= 5
```

### Inline type modifiers

*(since 0.2.3)* A **one-word** assignment modifier may also be written as a
**prefix on an expression**, wherever an expression is allowed:

```basic
print bytes({file}"/etc/hostname")
total = {USD}19.99
due = { when: {date}"2026-01-31" }
```

That is the same modifier and the same implementation as the clause form —
`{USD}x` and `x {USD}= ...` cannot disagree about what `{USD}` means. Use the
clause when the value is being **stored** and the inline form when it is being
**used**; before this, a value wanted once had to be given a name and a line to
be given it on.

It binds like unary minus, tighter than any binary operator, so
`{number}"12" + 1` is `({number}"12") + 1` — 13, not 121.

**AND LIKE UNARY MINUS IT NESTS**, to any depth, innermost first — the grammar
rule is `MODIFIER_PREFIX <expression>` and its result is itself an expression,
so composition costs nothing and was never designed:

```basic
clean = {upper}{trimmed}raw            ' trim, then upcase
n = {number}{trimmed}field             ' the common one
total {USD}= {number}{trimmed}cell     ' a clause over an inline chain
```

Order matters where the two do not commute: `{number}{trimmed}"  42  "` is the
number `42`, while `{trimmed}{number}"  42  "` raises `trim expects a string`.
Documented and asserted 2026-10-02, after the question was asked — it worked
from the day the inline form shipped and there was not one `{a}{b}` anywhere in
the tree.

**A CLAUSE CHAINS WITH `;`** *(added 2026-10-03)*, applied left to right:

```basic
clean {trimmed; upper}= raw
n {trimmed; number}= field
csv {trimmed; split ","; join "-"}= line
```

The separator is a semicolon and **not** a comma because the comma is already
the **argument** separator — `{between "a", "b"}` supplies two arguments — and
arity cannot tell the two apart, optional arguments being ordinary, so
`{split ",", trimmed}` would be a second argument or a second stage with
nothing to choose between them. A `;` **inside an argument** is content, not a
separator: `{join "; "; upper}` is one argument holding a semicolon followed by
a second stage.

A stage that fails stops the chain. A stage **nobody recognises** names its
position — `{trimmed; nosuch; upper}` reports
`assign modifier not found: nosuch (stage 2 of this clause)`, and a one-stage
clause keeps its sentence unchanged. A stage that is found and then **fails on
its input** reports its own cause without a position: `{number; trimmed}` on
`"  42  "` says `trim expects a string`, which names the modifier and so
identifies the stage in any chain that does not repeat one.

**If a stage raises, the target is not assigned** — it keeps whatever it held,
and stays undefined if it held nothing. That is the ordinary rule for a failed
assignment rather than a new one: the raise unwinds before the store.

**AND THIS IS WHAT COMPARISON LENSES ARE FOR.** A comparison lens is a
**normalisation of both operands**, so chaining one does the work that
otherwise has to be written out on both sides in both ways:

```basic
name = "  Ada Lovelace  "
typed = "ada lovelace"
if name {trimmed; caseless}= typed then       ' not trim(lower(a)) = trim(lower(b))
    print("match")
end if
```

Until 2026-10-03 `caseless` was the only comparison lens, so `a {trimmed}= b`
reported `compare modifier not found: trimmed` — an assignment modifier could
not be used to compare at all. **Any of them now can**, alone or chained, and
it is applied to each side symmetrically: `"joe" {upper}= "JOE"` is true,
`"  b  " {trimmed}> "a"` orders. The datetime precision lenses (`{day}`,
`{month}`) were always this shape and are unchanged.

A lens that **answers** the comparison rather than transforming a value —
`caseless`, or one you declare `for compare`, whose body is handed
`left`/`right`/`operator` and returns the verdict — must be the **last** stage,
since nothing can follow a verdict. `{caseless; trimmed}=` is refused by name.

Because both sides get the same treatment, a chain only makes sense where both
operands can take it: `{split ";"}` compares two arrays, and against a number it
raises rather than guessing.

**TWO CONSEQUENCES OF "BOTH SIDES", worth knowing before you declare a
modifier of your own.**

**Each stage runs once per operand**, so a comparison through a three-stage
chain is six invocations of your code where the assignment form is three. A lens
reads like a pure test and is not free.

**The order is stage-major**: stage 1 on the left, stage 1 on the right, stage 2
on the left, stage 2 on the right — not the whole chain down one side and then
the other. For a stage that is a pure function of its argument the two orders
give the same answer and the difference is invisible, which is why it is
*stated*: the two calls to one stage are adjacent, so a stage that reads
anything outside its argument sees both operands at as nearly the same instant
as possible.

**Which is the reason a stage must be a pure function of its input.** One that
answers differently each call is handed the two operands separately, so **equal
values compare unequal**:

```basic
' A modifier that is not a pure function of its argument.
ticks = []
same = "same" {ticking}= "same"      ' FALSE: the left got "1", the right "2"
```

Nothing can detect that, so it is not refused — it is the one rule a modifier
you write has to keep. The assignment form has no such requirement, because
there only one value goes through.

**Every modifier shape works inline** *(since 2026-10-03)* — an argument
(`{split ","}line`), a multi-word name (`{end of month}invoice`), a chain
(`{trimmed; upper}raw`) and a library qualifier (`{housestyle.shout}s`). Until
then only the one-word form did, so those first three were parse errors in
expression position and the assignment clause was their only spelling.

The reason is the lexer rather than the grammar: admitting the general lens form
in expression position costs 19 shift/reduce conflicts (measured) against a
project standard of zero, while the lexer may look as far ahead as it likes and
costs none. **What it needs is to tell a modifier from a record literal, and the
decision is one token past the leading identifier**: a record always has `:`,
`=` or `(` there (`{ a: 1 }`, `{ a = 1 }`, `{ serial (reset 7): 0 }`) and a
modifier never does. A record keyed by a string (`{ "k": 1 }`) or an empty one
never reaches that test. And an inline modifier may not span a **newline**,
which a clause could not either, so a record literal written across lines is
out of its reach by construction.

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

**Given a datetime value instead of text, `{date}`, `{time}` and `{datetime}`
convert it** (docs/datetime_conversion_design.md):

```basic
at {datetime}= "2026-05-15 12:05:03"
d {date}= at        ' 2026-05-15        its date
h {time}= at        ' 12:05:03          its time of day, at its own precision
m {datetime}= d     ' 2026-05-15 00:00:00   a date at midnight
```

A value missing what the kind needs is refused, saying what it lacks: a time
of day has no date to take, and a date has no time of day. Text is read as
before, so `{date}= "2026-05-15 12:05"` still keeps its minutes; it is a
*value* given to `{date}` that is cut to its day. G-SQL's `CAST(x AS DATE)`,
`AS TIME` and `AS DATETIME` convert the same way, and `CAST` of text to
`DATE` also takes its day.

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

**`load` is position-blind** (*since 0.1.0*): write it at the top level or
inside the `program` block, and it is registered before anything runs either
way. Until this release a top-level `load` never ran when a program block
existed — `load` was an executable statement and those statements are not
walked — and it warned about it.

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
compound values and as a string against a number: "is USD 19.95 equal to
EUR 19.95" is a question whose answer is no; "is it less than" is not a
question at all without an exchange rate.
Applying a different currency's modifier to existing money is refused for the
same reason — re-tagging and converting are different operations, and picking
one silently would invent a rate of 1.

**Storage carries four guard digits below the minor unit**, so intermediates
below the cent survive a multi-step calculation and are rounded once, at
display: `(100.00 / 3) * 3` is `100.00`, where a cents-only representation
gives `99.99`. The cost is range — USD spans about ±$9.22 trillion, JPY about
±¥922 trillion, KWD about ±922 billion. Beyond that, arithmetic **raises**
rather than wrapping.

Text with more decimal places than the currency stores (six for USD) is
refused only when a digit past them is not zero: `{USD}"1100.00000000"`, the
way PostgreSQL and ODBC send a `numeric(19,8)`, is 1100 exactly, while
`{USD}"1.2345678"` is refused. The same rule holds for text stored into a
G-SQL `money` column.

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

**`money.of(code, text)`** builds money from a currency code held in a
**variable** — the mirror of `money.currency`. `{USD}=` is a modifier and needs
a *literal* code, so a program that learns its currency at runtime (from a
column, a config file, a loop over `money.currencies()`) could not construct a
value at all; the workaround was an `if` chain with one branch per currency.
Same parse path and same refusals as the modifier, so excess precision past the
storage scale is rejected exactly as `{USD}=` rejects it, and a historical
currency is refused for new values. **A refusal's `error.details` says which**
(*since 0.11.0*), for `money.of` and the modifier alike: `{ kind: "places",
allowed, found }`, `{ kind: "range" }`, or `{ kind: "text" }`, so a program can
turn "this price has seven decimals" into a data outcome without matching the
message. **Money negates** (`-amount`, *since 0.11.0*), exactly and in its own
currency.

```basic
m = money.of("KWD", "1234.5678")   ' three minor-unit digits, per KWD
```

**`money.currency(amount)`** returns the amount's ISO code as a string. Money
is otherwise the one typed value you cannot fully introspect — `type(m)` says
`money` and nothing says *which* — and grouping amounts by currency needs it.
A value deserialized from an actor whose currency this process never registered
answers with its numeric code as text, rather than losing information the value
still carries.

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
the parent is not registered in the child. Since `SER_VERSION` 3 it also
carries the currency's **code**, and the reader looks the code up first: a
registered currency's number depends on the order a program registered things
in, so the same number can name different currencies in two programs.
`SER_VERSION` 1 payloads (a bare integer of cents) still deserialize, rescaled
and assumed USD.

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

**A library's body is declarations**: `load`, `function`, `modifier`,
`database` and `sql` (*since 2026-11-09*). Loading a library runs none of its
code, so any other statement at the top of a `library` block is refused when
it is loaded, at its own line, before the program runs. That includes an
assignment, a `print`, a `watch`, and a `function obj.f()` attached to a
value. They used to be skipped in silence, so `counter = 5` there left a
function reading `counter` with "undefined variable". State a library needs
comes from a function that returns it.

**`load` is a declaration, and either position works** (*since 0.1.0*). Write it
at the top level or inside the `program` block; both are registered before
anything runs.

**That holds for a program and NOT for a library.** A dependency a *library*
uses goes **inside its own `library` block**; a top-level `load` in the same
file registers the name and leaves the functions out of that library's scope:

```basic
library outer
    load dep from "dep.bas"      ' inside: `dep.answer()` resolves
    function ask()
        return dep.answer()
    end function
end library
```

Measured, and the reason this is worth a paragraph rather than a footnote: with
the `load` at the top level instead, `outer.ask()` fails for a caller that did
**not** also load `dep`, and **works** for one that did. So the author tests it
from a program that loads both, sees it work, and ships a library that fails for
somebody else. The diagnostic compounds it — `undefined function: dep.answer --
'dep' does not define 'answer'` — which is **false**: `dep` defines `answer`
perfectly well, and the mistake is one line in `outer.bas` rather than anything
in `dep.bas` that the message sends you to read.

```basic
load xml                  ' both of these work
program main()
    load sqlite
    doc = xml.parse("<a/>")
end program
```

Until this release a top-level `load` never ran when a program block existed,
and warned "this `load` is outside the program block, so it never runs — move it
inside `program`". **The warning was true of the parent process and blind to a
spawned actor.** A `spawn`ed actor is fork+exec: the child re-parses the source
and runs the entry function, never entering the `program` block — so for the
child the top-level position was the only one that worked, while for the parent
it was the only one that did not. Taking the warning's advice made the worker
fail on its first qualified call while the parent waited in `receive()`, and the
symptom was a hang that pointed nowhere near the `load`. Parent and child now run
one shared registration pass, which `tests/run_pre_registration.sh` pins
structurally.

**Only a `load` written as a direct child of the file or of the `program` block
is hoisted.** `load` is an ordinary statement, so it may also appear inside
`if`, a loop or a function, and one written there still runs where it is
written — `if false then load nosuchlib` imports nothing.

What may be written below `end program` is exactly: function declarations,
modifier declarations, `library` declarations, `server` declarations, `load`,
and dotted-def method bodies. That set is pinned by
`tests/run_pre_registration.sh`, and gBASIC Studio's declaration-hoisting rule is
defined as the same set.

**Hoisting is what a `program` block buys, and a file without one does not get
it.** In script mode — statements at the top level, no `program` block — a
function must be *declared before it is called*, because the statements run in
the order they are written and a declaration is one of them:

```basic
print sq(3)             ' script mode: fails -- sq is not declared yet
function sq(n)
    return n * n
end function
```

The same two work in either order once the file has a `program` block — but
what is hoisted is the file's **top level**, not the inside of the block:

```basic
program main(args)
    print sq(3)             ' works: sq is declared below `end program`
end program

function sq(n)
    return n * n
end function
```

Move that `function` *inside* the block, above `end program`, and the call must
still come after it — a `program` block's body runs in order like any other.
(This paragraph said "inside a `program` block" until 2026-09-23, which is the
natural reading and was false.)

The diagnostic for the script-mode case is `undefined function: sq — no
function, library function or builtin of that name is in scope at this call`,
which names the scope rather than the ordering — if you see it for a function
you are certain exists, check whether it is declared *below* the line that
calls it. It says **in scope at this call** rather than "nothing defines that
name" for exactly this reason: here the file plainly does define it, and the
blunter sentence would be a lie in the one place a reader is most likely to be
confused. (It read `invalid function call: sq` until 2026-10-02, which was a
sentence about the call and sent readers to check their arguments.)

**A library's own dependencies are declared INSIDE its `library` block.**
Declaring one beside the library rather than inside it is not a scoping error —
it is a *packaging* one, and it fails only for someone else:

```basic
load frame                ' WRONG — grid does not declare what it needs
library grid
    function f()
        return frame.from_rows([])   ' works only while the CALLER loads frame too
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

### Loading a library under another name

`load NAME as ALIAS` gives the library the name **this file** wants to call it
by (*since 0.1.0*):

```basic
load statistics_helpers as st
print(st.zscore(2, 1, 1))
```

The alias works everywhere the library's own name would: qualified calls,
qualified modifiers (`x {st.rounded}= 1.5`), and function values (`f = st.mean`).
Both spellings of `load` take one — the deprecated `use` does not:

```basic
load toolkit from "vendor/a/toolkit.bas" as tools_a
```

**An alias replaces the library's declared name rather than adding to it.**
After `load toolkit ... as tools_a`, `toolkit.describe()` is an error — two
names for one import is the ambiguity aliasing exists to remove. The error says
which name the library *was* loaded under, because a rename that missed a call
site is the mistake this makes easiest to write.

(Precisely: the alias adds no second name of its own. A library name is
available when *something* in the program loaded the library under it, which
has always been true — so if another library loads `matrix` plainly while your
file loads it `as m`, both `matrix.` and `m.` resolve. That is deliberate:
refusing it would let any library decide which names its consumers may pick.)

**A library does not know it was renamed.** Its own unqualified calls still
reach its own functions, and the libraries *it* loads keep their own names — an
alias is the importing file's word for one library, not a renaming that reaches
into what that library loads.

**A library's own loads are its own** *(after 0.10.0)*. A name a `load` gives
belongs to the file that wrote it, the program or the library whose block holds
the `load`. Inside `stdlib/mcp.bas`, `tools.dispatch` means the `tools` mcp
loaded; in a program with its own `library tools`, `tools.x` means the
program's. So a library can load whatever it needs without claiming the name
for everyone, and a stdlib release that adds a load cannot break an
application. A name a file did not load still reaches what the program-wide
name is, as it always has, so a library still reaches a library its caller
loaded. Messages, traces and `library_collisions()` show a library's name as
written; when two libraries answer to one written name, the second is shown
with its file, `util (vendor/util.bas)`. Before this, mcp's own `load tools`
refused an application's `library tools` (Transward §7.6;
docs/library_private_loads_design.md).

**Within one file, a name answers for one library, and a collision is
refused.** An alias may not be the name of another library or alias this file
loaded, or a built-in module (`sqlite`, `pg`, `gi`, …), and a built-in module
may not be aliased: those qualifiers are recognized by name before user
functions are resolved, so an alias could never reach one.

The case that alias exists for is **two libraries whose own declared name is the
same**. Before aliasing they could not coexist: the second import failed with
*"function 'describe' is defined twice in library 'toolkit'"*, which names a
function neither file defined twice and never mentions that there are two files.
Now the collision is one refusal that names both sources and spells the fix, and
either file can be loaded under another name:

```basic
load toolkit from "vendor/a/toolkit.bas" as tools_a
load toolkit from "vendor/b/toolkit.bas" as tools_b
```

An alias is an **import identity**, not a lookup-time rename: registration keys
on the effective name, which is what lets one declared name arrive twice.

### Which function an unqualified name reaches

A name written without a `library.` prefix resolves in exactly two places, in
this order:

1. **The library whose code is running**, if any — its own function wins.
2. **The root program's own functions.**

and nothing else. **A function from another library must be qualified**
(*since 0.1.0*). Builtins are tried before either, outside a library body.

```basic
load stats
print(stats.ols(y, [x]))     ' correct
print(ols(y, [x]))           ' error: names the library and spells the call
```

The error is written to be the edit: *`invalid function call: ols — library
'stats' defines it, and a function from another library must be qualified.
Write stats.ols(...)`*. Where more than one loaded library defines the name it
offers each of them rather than choosing.

**Rule 1 holds in every shape a library takes** — one in its own file, one
declared in the *same* file as the program that loads it, and a `modifier`
body, which is library code invoked by a different path. A library calling a
name it does not define reaches the builtins, and calls from the root program
are unaffected.

There used to be a third step: a backward scan over every imported function,
newest registration first. It resolved `ols(...)` after `load stats`, which was
a real convenience — and it decided *which library a call meant* by **load
order**, something the author of the call neither controls nor can see, so
adding a `load` at the top of a file could silently move a call in the middle
of it. Measured before removing it: of 4,311 resolutions that went through the
scan across the whole test suite, 3,931 were one library reaching another, and
of the 177 distinct `library.function` pairs root programs reached, exactly one
name was defined by more than one library — so the scan was almost never
choosing between candidates, it was resolving a unique name by a rule that
could have chosen wrongly.

Library modifiers must be marked `export modifier` to be usable outside the
library; unlike functions they are used by name, with no prefix.

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
  arguments arrive as the entry function's **parameters**, and the mailbox starts
  **empty**; a handle among them — including `self()` — is passed through so the
  child can message that actor. (They do travel over the same channel, which is
  why they are bound by the same frame cap — but what the child *sees* is
  parameters. This page said "copied to the child as its first message" until
  2026-10-02, which described the transport rather than the program: measured, a
  child printing its parameters gets them, and a following `receive(1 seconds)`
  answers `nothing`. Arity is checked against the parameters, so
  `spawn kid0(5, "x")` against a zero-parameter entry is refused outright with
  `spawn: kid0 expects 0 arguments`.)
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

**`try` — one expression that may fail** *(after 0.10.0)*. `x = try expr` is the
value, or `nothing` when evaluating it raised. What it caught is readable by
the next check, like a raise under `on error goto next`, and unlike one, it is
**never raised again when the function returns**. `try` changes no `on error`
mode, so whatever the function's policy is stays in force around it:

```basic
x = try conn.write(b)        ' the value, or nothing if it raised
if error then
    print to error "write failed: " + error.message
end if
try conn.close()             ' best-effort: nothing is owed if it fails
```

`try` binds loosest, so `try f() + 1` covers the sum. A success clears what an
earlier `try` caught, so a check reads the latest; `error.clear()` clears it
too. `stop` and `exit` are not raises and end the program through it. There is
no `try … catch` block: `try` alone on a line, or `catch`, is a parse error
naming this form. Use `on error goto next` to check several statements, and
`on error goto label` for a whole function.

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

A third rule keeps the *cause* honest: **while a raise is unwinding, the first
one wins.** Nothing that happens on the way out can replace it, so the failure
you are shown is the one that started it. Before 0.2.3 this depended on whether
the enclosing caller happened to check, and the commonest shape got it wrong —
`decode(read(f))` on a missing file reported that `decode` wanted a string,
naming the only call in the line that was not at fault.

**Where an error points.** The line and column name the failing *expression*,
not the statement containing it, so `x = a[5]`, `print(a[5])` and
`print("x " + string(a[5]))` all point at the `[`. Operators carry their own
position too: `1 / 0` points at the `/`. A fault that really is about the whole
statement still reports column 1.

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

**`error.trace`** is an array of `{name, path, library, line, column}` records,
innermost first (the field is `name` because a keyword cannot follow a dot).

**`path` is the file the frame's function is written in**, and it is set for
every frame. Before 2026-10-05 it was stamped only for an *imported* function,
so a library frame said `./lib.bas` while the frame that **called** it said
nothing — the path was known all along and simply never asked for, which
defeats the one job a trace has.

**An uncaught raise prints the frames beneath the diagnostic**, innermost first:

```
runtime error at prog.bas:12:5: division by zero
  in compute  prog.bas:12:5
  in main     prog.bas:40:9
```

**What is NOT shown is the part to know.** A run of frames inside one library
collapses to a single `in library NAME` line — and is omitted entirely when the
error is already *reported* in that library, because the path and the message
both name it there already. So a `chart` refusal stays one line, as it always
has. `--full-trace` lists every frame. Frames are **not** included under
`--json-diagnostics`, where a `message` stays one sentence; a program wanting
them reads `error.trace`, which always holds every frame regardless of what the
report chooses to show.

**`library` is the library that owns the frame, or `""` for the root program.**
That is what lets a report name a library without listing every frame inside it
— `in library accounting` rather than four lines of its internals — while
`error.trace` keeps every frame, so collapsing is a presentation choice and
never a loss. Note `""` means *the root program*, a different claim from
"unknown"; while `path` was also empty the two were indistinguishable.

**Each frame's `line` is the CALL SITE, not the raise site** — where that frame
called the next one inward. That is the right choice, because it is what tells you
how the program got there, but nothing said so, and a reader who assumes the
other reading is one frame out in the direction that looks plausible. Added
2026-10-02 on a request from the gbasic-books session, whose Chapter 13 prints a
trace.

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
`on warning stop` in `main` makes the whole program's warnings fatal.

**It is not `-Werror`, and the rule above is why.** A compiler flag cannot be
overridden from inside the program; this is a *dynamic* lookup, so any frame
between `main` and the warning that sets a mode of its own wins — a library
that says `on warning print` silently un-escalates everything it raises, and
your program ends 0. That is the channel working as designed (the noise budget
belongs to the caller closest to the noise), but it means a top-level
`on warning stop` is a default, not a guarantee. If you need a guarantee, check
`warning` where it matters.

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

SQLite support is available when gBASIC is built with sqlite3, and **both
published downloads carry it** — sqlite 3.54.0, linked statically, so there is
nothing to install and no version to wonder about. The build options are
matched against what a distribution's own libsqlite3 enables, since this module
hands a program arbitrary SQL: FTS5, R-Tree, the SQL maths functions, JSON,
`soundex`, `delete ... limit`, `file:` URIs and the raised parameter limit all
work. The one option deliberately dropped is extension loading, which gBASIC
never enabled and which both builds refuse. Load the compiled standard module
before using its qualified API:

```basic
load sqlite

db = sqlite.connect("app.db")

db.exec("create table if not exists users (id integer primary key, name text, active integer)")
db.exec("insert into users (name, active) values (?, ?)", ["Ada", true])
rows = db.query("select id, name from users where active = ?", [true])

db.close()
```

Connections are opaque `sqlite_connection` values. They close explicitly with
`sqlite.close` and automatically during interpreter cleanup.

The module provides:

- `sqlite.connect(path_string)`
- `connection.close()` (`sqlite.close`)
- `connection.query(sql[, params])` (`sqlite.query`)
- `connection.exec(sql[, params])` (`sqlite.exec`)
- `connection.begin()` (`sqlite.begin`)
- `connection.commit()` (`sqlite.commit`)
- `connection.rollback()` (`sqlite.rollback`)
- `connection.last_insert_rowid()` (`sqlite.last_insert_rowid`)

Parameters are arrays and are bound separately through sqlite3. Use SQLite
positional placeholders such as `?`. SQL `NULL` maps to `nothing`. Query
results are arrays of records; duplicate column names are errors. SQLite blob
parameters and results are not currently supported. SQLite errors use
`error.source = "sqlite"`.

SQLite type mapping:

| SQLite storage class | gBASIC value | Notes |
| --- | --- | --- |
| `NULL` | `nothing` | Database null is absence |
| `INTEGER` | number | **exact**, all 64 bits (*since 0.11.0*; past 2^53 it came back as the nearest double, `9007199254740993` as `...992`) |
| `REAL` | number | Stored as a gBASIC number |
| `TEXT` | string | No date/time guessing is applied |
| `BLOB` | runtime error | Native blob values are future work |

Parameters support `nothing`, booleans, numbers, strings, and date/time
values. Booleans bind as integer `1` or `0` because SQLite has no separate
boolean storage class. Date/time values bind as ISO-like text.

`connection.last_insert_rowid()` returns SQLite's last inserted rowid for
that connection as a number.

## G-SQL Module

G-SQL is gBASIC's own database (`docs/gsql_design.md`), and it is part of
every build on Linux and other POSIX systems, with nothing to install. A
database is reached through its **owner**, one process that serves every
program using it; `gsql.connect` starts the owner when none is running. The
owner is not the program's child: it serves every program connected to it,
and once nobody has been connected for its **linger** (3 seconds unless the
starting connection said otherwise) it stops cleanly by itself
(`docs/gsql_owner_lifetime_design.md`). A database has **one owner on any
storage**: on a disk shared between machines whose file locks do not reach
across them, a second machine is refused with `ELSEWHERE`, naming the machine
that has it, and after a crash there another machine waits about 14 seconds
before taking it (`docs/gsql_lease_design.md`). What SQL it understands, and the
rules a program meets, are in `docs/gsql_overview.md`, which is kept current; the
designs behind them are `docs/gsql_sql_design.md` and the pages it leads to.
**To start, read `docs/gsql_cookbook.md`**: worked recipes from a first table
to the practices a production program needs, every one run by the gate.

```basic
load gsql

db = gsql.connect("shop.gsql", { create: true })
db.exec("create table item (id integer primary key, name string not null, price money)")
db.exec("insert into item (id, name, price) values (:id, :name, :price)", { id: 1, name: "apple", price: {USD}"0.50" })
rows = db.query("select id, name from item where price < $1", [{USD}"1.00"])
db.close()
```

The module provides:

- `gsql.connect(path[, { create: true, linger: seconds }])` — without `create`, a database that is not there is refused. `linger` is how long an owner this connection **starts** waits after its last connection closes before it stops, from 0 to 86400; 0 stops it at the last close. It is the starter's choice: when an owner is already running, a `linger` given here is ignored. A `database` block takes `linger:` too
- `connection.query(sql[, parameters])` (`gsql.query`) — an array of records, fields in select-list order
- `connection.exec(sql[, parameters])` (`gsql.exec`) — runs anything and answers `{ command, rows_affected }`, as `sqlite`, `pg` and `odbc` answer, plus **`rows`, every row, when the statement returns rows**; `rows_affected` is `nothing` for a statement with no count. `gsql.query` is the shortcut for the rows alone, and `for each` streams a large result
- `connection.columns(sql[, parameters])` (`gsql.columns`) *(after 0.10.0)* — a
  query's result columns, `[{ name, kind }]` in order, from its plan: **nothing
  runs**, no row is read and no parameter is needed, so it is cheap on a large
  table and safe on any query (one that would fail when run, `1 / 0`, is still
  described). `kind` is the SQL layer's word (`number`, `string`,
  `money(USD)`, `date`, ...), `any` for an expression the planner cannot type.
  A table's columns are `db.columns("select * from t")`. A statement that
  returns no rows is refused as `STATE`.
- `connection.insert(table, rows)` (`gsql.insert`) *(after 0.10.0)* — many rows
  in one call. `rows` is an array of records keyed by column name, as a frame's
  rows are; a column a row leaves out takes its default, and each value is
  converted and checked exactly as `INSERT` does it. It answers
  `{ command: "INSERT", rows_affected }`. **All or nothing**: one refused row
  undoes the whole call, and the error names the caller's row,
  `gsql.insert: rows[1234]: ...`, with `error.details.index` 1234. Inside a
  transaction the call is a savepoint, so only the call is undone. A row that
  is not a record, or names no column, is refused before anything is sent.
- `connection.begin(["read" | "write"])` (`gsql.begin`) — `"write"` by default
- `connection.commit()` (`gsql.commit`), `connection.rollback()` (`gsql.rollback`)
- **A `begin` inside a transaction nests** *(2026-11-05)*: in a write
  transaction it is a **savepoint**. Its matching `commit` keeps what was done
  since, inside the transaction, which goes on; its matching `rollback` undoes
  that and nothing before it. So a unit of work written as `begin` … `commit`
  (or `rollback` on failure) is one unit whether it runs alone or inside a
  caller's transaction:

  ```basic
  function store_robot(db, robot)
      db.begin()                      ' a transaction alone, a savepoint inside one
      on error goto undo
      for each part in robot.parts
          db.exec("insert into part values (:serial, :name)", part)
      end for
      db.commit()
      return true
  undo:
      error.clear()
      db.rollback()                   ' this robot's rows, and nothing earlier
      return false
  end function
  ```

  A read transaction nests with nothing to undo, and cannot start writing
  (`READ_ONLY`). Rolling back to a savepoint closes the transaction's open
  cursors. A multi-statement `sql` block run inside a transaction takes a
  savepoint of its own, so one refused part way undoes its earlier statements
  and nothing of the program's.
- **A statement refused part way is undone alone.** Inside a transaction a
  writing statement runs under a savepoint of its own, so row 5 of an `UPDATE`
  meeting a duplicate after rows 1 to 4 changed leaves no trace and the
  transaction goes on. (It used to fail the whole transaction.) The cost is
  measured: none for single-row statements, about 30% on one `UPDATE` of
  20 000 rows inside a transaction; autocommit pays nothing. The old rows a
  savepoint keeps are bounded (four times the cache, 256 MiB by default); a
  transaction past that still works, but rolling back to one of its savepoints
  ends the whole transaction (`FAILED`) rather than undoing half of it.
- **A library's blocks from outside** take the method spelling as the module
  one, `store.add.exec(row)` as `gsql.exec(store.add, row)`, and a program may
  declare its own block on a library's database: `sql mine on store.shop`.
- **`CREATE VIEW IF NOT EXISTS`** as tables take it: a view already there is
  kept as it is. With `OR REPLACE` it is refused, the two asking opposite
  things.
- A database in a folder that does not exist is refused naming the folder:
  G-SQL creates the database, not the folders above it.
- `connection.close()` (`gsql.close`)
- `gsql.wait_for_owner(path, seconds)` *(after 0.10.0)* — waits up to `seconds`
  (or a duration, at most 3600) for no owner to hold the database at `path`,
  and answers whether none does: the same question as `close({ wait })`, asked
  by any program, for an owner whose starters have exited (a test cleaning up
  after server workers and CLI calls). It reads the store's lock, which an owner
  releases after its final write, so `true` means nothing is writing. On this
  machine only: an owner on another machine sharing the storage is not seen. A
  path with no database answers `true`.
- `connection.close({ wait })` (`gsql.close`) *(after 0.10.0)* — closes, then
  waits up to `wait` (seconds, or a duration such as `10 seconds`, at most
  3600) for the database's owner process to exit, and answers **whether it
  did**. `true` means nothing is writing the database any more, so it can be
  deleted or its directory checked. `false` is an answer, not an error: another
  connection still uses the owner, or it is within its `linger`, or it is
  still finishing its last write.
- `connection.max_recursion` — an **attribute**, set and read like a field: the rounds a `WITH RECURSIVE` query may run on this connection before it is refused, 1 000 000 unless set. `bank.max_recursion = 5000000` holds for every later statement on the connection, through its `sql` blocks and SQL strings alike; `unknown` sets it back. It takes a whole number of one or more, and a connection has no other attribute (`docs/gsql_recursive_design.md` §3)
- `connection.backup(dir)` (`gsql.backup`) — a consistent copy as of now, made while the
  database stays in use (`docs/gsql_backup_design.md`). It answers
  `{ bytes, files, reused }`. Backing up into a copy of the same database
  **refreshes** it, keeping the files it already holds. A relative `dir` is the
  program's. It is refused as `STATE` when the copy is open (the live database
  itself included) and as `EXISTS` for a directory holding other files. The copy
  is an ordinary database; restore by using it, or by copying its directory back
  with no owner running.
- `connection.dump(file[, { schema_only: true }])` (`gsql.dump`) — the database as SQL
  text: tables, indexes, rows, serial counters, constraints and views, from one
  snapshot, written a slice at a time while the database stays in use
  (`docs/gsql_dump_design.md`). It answers `{ tables, rows, skipped, bytes }`;
  `skipped` counts what could not be written (a view that no longer runs, a
  foreign key whose parent is gone), each named by a `-- not written:` line. A
  record holding a date or money is refused as `TYPE`, naming its table and
  column. A relative `file` is the program's, and a dump that fails leaves no file.
- `connection.restore(file)` (`gsql.restore`) — runs a dump into a database, the rows ten
  statements a transaction, the file streamed. It answers `{ statements, rows }`.
  It runs only what a dump writes, and only onto tables it created itself, so a
  file cannot change data that was already there; anything else is refused as
  `STATE` before it runs, naming its line. A restore that fails part way keeps
  the batches before the failure.

- `gsql.space(dir)` (or `connection.space()`) — what a database's directory
  holds, in bytes: `{ total, logs, tables }`. `tables` is the sorted and blob
  files, `logs` the logs, and `total` every file the database owns (those, the
  manifest, and the lock and lease files). It is read from the directory, not
  asked of the owner, so it answers for a closed database without starting
  one. Sizes are file lengths, not blocks. **The bound:** while a database is
  open its logs can reach two memtables (64 MB at the default) beyond its
  tables; once it is closed and its owner has stopped, the logs are empty and
  `total` is the tables plus under a kilobyte. A directory with no manifest
  (`db`) is refused.

Parameters are a **record** for `:name` placeholders and an **array** for `$1`,
`$2`, … . Values cross unchanged in both directions: an exact integer stays
exact past 2^53, money keeps its currency, a date stays a date, and SQL `NULL`
reads back as `unknown`. A whole result is fetched before `query` answers.

**A warning inside SQL is a gBASIC warning** at the call that ran the statement
(`gsql.exec`, `gsql.query`, a `sql` block or its `for each`), with its own code:
an exact sum overflowing is `2112`, as it is in a program. So `on warning ignore`,
`goto next` and `stop` all apply. Under `stop`, inside `gsql.begin` ... a warning
becomes an error before anything is committed, so the program can roll back.
A site says each different warning once, so a loop is not flooded.

Every refusal raises with `error.source = "gsql"` and **`error.details.code`
naming G-SQL's code** — `SYNTAX`, `EXPRESSION`, `NOT_FOUND`, `DUPLICATE`,
`NOT_NULL`, `TYPE`, `READ_ONLY`, `STATE`, `LOST` and the rest — plus
`error.details.position`, a 1-based character offset, where the statement has
a place to point at. **`CAST(m AS string)` keeps every digit of money**
*(after 0.10.0)*: a cast moves data, so `1250.7512` stays `1250.7512`, never
fewer than the currency's places, where `string(m)` in a program shows
`1250.75`. **A refused row names what it is about** *(after 0.10.0)*:
`error.details.table`, `error.details.column` for `TYPE` and `NOT_NULL`, and
`error.details.constraint` for `CHECK` and `FOREIGN_KEY` (the constraint's name)
and `DUPLICATE` (the unique index's name, or `"primary key"`). A name that does
not apply is absent rather than empty, so a reject report routes rows by
`has(e.details, "column")` and the values, never by the message's words
(`docs/gsql_error_details_design.md`). `query` refuses a statement that returns no rows,
saying that it ran.

**A value stored into a typed column takes the column's kind.** In `INSERT`,
`UPDATE ... SET` and a `DEFAULT`, text into a `date`, `time`, `datetime` or
`money` column is read as that kind, and a number into a `money` column is
money in the column's currency: `values ('2026-11-01 04:31', 199.50)` stores
a date-time and money. Text that does not read as one is refused, naming the
column. **A comparison reads text as the other side's kind** *(after 0.10.0)*:
when the two sides differ and one is text -- a literal, a parameter, a string
column -- the text is read as the other side's number, money, date, time or
date-time with storing's readers, and a number against money is money in its
currency. So `where id = '7'`, `where day >= '2026-10-01'` and `where amount >
1000` work, and an index seeks on them. Text that does not read as the kind is
refused, naming it; a boolean against anything else, a date against a number and
an ordering across currencies stay refused. Arithmetic converts nothing (`'2' +
3` is refused; join text with `||`) (`docs/gsql_compare_conversion_design.md`).
An integer column takes text that is a whole
number written in digits *(after 0.10.0)*, with spaces around it trimmed:
`'25'`, `' 25 '`, `'-7'`, `'007'` (which is 7; keep the zeros in a string
column). It refuses a decimal in text (`'2.5'`, `' 2.5'`) or as a number, and
anything else, each with its reason. A number column takes text that is a number
written plainly, decimals and exponents included (`'2.5'`, `'1e3'`), trimmed the
same way (`docs/gsql_store_conversion_design.md` §1a). A `duration` column
*(after 0.10.0)* holds a duration as given, field for field (`24 hours` stays
`24 hours`), and reads text in the form a duration prints in (`'1 month 14
days'`, as `CAST(... AS DURATION)` does); a number is refused, having no unit.
It is never keyed: a primary key, `UNIQUE`, an index, `GROUP BY` and `DISTINCT`
refuse it, since `1 month` has no order against `31 days`, so keep its
`total_seconds` in a number column to find or sort by length
(`docs/gsql_duration_design.md`). If the owner exits, the
request in flight fails with `LOST` and nothing is retried; the next call
starts a new owner. An owner whose database another machine has taken writes
nothing more and exits, so its clients see `LOST`, and their next call is
refused with `ELSEWHERE`.

### The `database` and `sql` blocks

A program can declare its database and its statements instead of passing
strings:

```basic
load gsql

database shop( file: "shop.gsql", create: true )

sql cheap on shop
    select id, name, price
    from item
    where price < :limit
    order by id
end sql

sql add on shop
    insert into item (id, name, price) values (:id, :name, :price)
end sql

id = 1
name = "apple"
price = {USD}"0.50"
add.exec()

limit = {USD}"1.00"
for each row in cheap
    print row.name
next
```

- `database NAME( file: "path"[, create: true] )` binds `NAME` to a
  connection, the same kind `gsql.connect` answers, so every `gsql.` verb takes
  it. It **connects on first use**: declaring a database starts nothing. The
  options are literals; a database chosen at run time is `gsql.connect`. A
  relative `file:` is found from the current directory, as `gsql.connect`'s is.
- `sql NAME on DATABASE` … `end sql` binds `NAME` to one statement, a value of
  kind `gsql_statement`. The block ends at a line reading `end sql`, not `end`,
  because SQL's `CASE` has an `END` of its own.
- **Each `:name` parameter is read from the variable of that name where the
  block runs**, every time it runs: inside a function, that function's own
  variable. A parameter with no variable is refused (`PARAMETER`); a block
  cannot use `$1`, since nothing could supply it.
- `for each row in NAME` runs a `select` block and hands over **one row per
  pass**, fetching a batch at a time, so a result is never held whole. The loop
  owns the result: leaving it early by `break`, `return` or a raise ends it.
- `NAME.query()` (`gsql.query`) answers every row as an array, and `NAME.exec()` (`gsql.exec`)
  answers `{ command, rows_affected }`, with `rows` when it returns rows.
  A connection's verbs are methods the same way: `db.begin()`, `db.commit()`,
  `db.close()`, `db.exec(block)`.
- **A record supplies the values:** `NAME.exec(record)` (`gsql.exec`) and
  `NAME.query(record)` read each `:name` from the record's field of that
  name, and a name the record lacks from the variable in scope, so a row read
  from a file or another query goes straight in:
  `for each r in rows` / `add.exec(r)` / `next`.
- **A block may hold several statements**, separated by `;`, each checked at
  load: **the last one answers**, and a `select` may only be last, since rows
  anywhere else would go nowhere. `gsql.exec` answers the last statement's
  `{ command, rows_affected }` with `statements`, one per statement in order; a
  block ending in a `select` is a query, its changes kept before its rows
  stream. **It runs all of its statements or none:** it joins the program's
  transaction if one is open, and otherwise begins and commits its own
  (`docs/gsql_multi_statement_design.md`).
- **A database chosen at run time** (one per tenant, a directory the program is
  given) is declared as a role, `database NAME()` with no `file:`, and its
  blocks are given a connection when used: `connection.exec(block)` and
  `connection.query(block)`, connection first as with a string, or
  `connection.on(block)`, the block bound to that connection, which is
  what `for each` takes. Any block can be run elsewhere the same way; one on a
  database with a file still runs there by default. A role takes no `create:`
  or `linger:`, which are the options of the `gsql.connect` that makes its
  connection (`docs/gsql_block_connection_design.md`).

**Write SQL in blocks; use a string only for SQL assembled at run time.** A
block is fixed text in the program, so nothing a user types can become part of
a statement and injection is impossible by construction; its SQL is checked
when the program loads, sits beside the code that uses it, and needs no
quoting. A string is for the statement that really is built as the program
runs, a table name it chose, say, and then every value it carries is bound as
a parameter, never pasted in.

When the program loads, the statement is parsed, so **a mistake in the SQL is
reported at its line and column in the program**, before anything runs; so is
an `on` naming no `database` block, an unknown or non-literal option, or a
block inside a function. The blocks are declarations: at the top level, or
directly in `program`, or directly in a `library`. A program holding one must
`load gsql`, and a library holding one loads it itself.

**A library's blocks are its own** *(after 0.10.0)*. Its functions reach them by
bare name, exactly as a program's functions reach the program's (`make.exec()`,
`for each row in cheap`), and a library's own name comes first, so a program
with a `make` of its own cannot capture the library's. A block's `on shop` is
the library's `shop`; two libraries may each declare `database shop` and have
two databases. From outside, a block is qualified: `for each row in
store.cheap`, `gsql.exec(store.make)`. A loaded library file is checked as the
program's file is, so a mistake in one of its blocks is reported at its line in
that file before anything runs (`docs/gsql_library_blocks_design.md`).

On Windows, `load gsql` answers that G-SQL is not yet available there.

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

rows = db.query("select id, name from users where active = $1", [true])
result = db.exec("update users set active = false where id = $1", [10])

db.close()
```

Connections are opaque `postgres_connection` values. They close explicitly
with `pg.close` and automatically during interpreter cleanup.

The module provides:

- `pg.connect(config_record)`
- `connection.close()` (`pg.close`)
- `connection.query(sql[, params[, { max_rows: n }]])` (`pg.query`). With
  `max_rows` *(Unreleased; gdash's GDASH-16 B2)* the rows are read in libpq's
  single-row mode, and one past the cap **cancels the query and raises**,
  `details: { code: "MAX_ROWS", max_rows }`, rather than answering part of
  it: measured, a capped million-row query peaked at 33 MB where the whole
  one took 655. In a transaction the cancel aborts it, as any error does in
  PostgreSQL. Pass `[]` for no parameters.
- `connection.exec(sql[, params])` (`pg.exec`)
- `connection.begin()` (`pg.begin`)
- `connection.commit()` (`pg.commit`)
- `connection.rollback()` (`pg.rollback`)

Parameters are arrays and are bound separately through libpq. SQL `NULL`
maps to `nothing`. Query results are arrays of records; duplicate column names
are errors. `bigint` and `numeric` results remain strings to avoid precision
loss. JSON results decode to normal arrays, records, and scalar values.
PostgreSQL errors use `error.source = "postgres"` and include SQLSTATE when
available.

**Postgres array types work in both directions** (*since 0.1.0*, 2026-09-05).
A result column of any built-in array type — `text[]`, `int[]`, `float8[]`,
`bool[]`, `numeric[]`, `jsonb[]`, `uuid[]`, twenty-two in all — arrives as a
gBASIC array, with each element converted by its own type under the same
rules as a bare column: `int8[]` and `numeric[]` elements are **strings** for
exactness, a `NULL` element is `nothing`, and a multi-dimensional array is
nested arrays, parsed rather than flattened. Quoted elements, escaped quotes
and backslashes, the empty array, and the text `"NULL"` (which is a string,
unlike a bare `NULL`) are all read as Postgres wrote them.

An array **parameter** is rendered as a Postgres array literal when the
statement wants an array, and as JSON when it wants JSON — and the statement
decides. `[["staff","lending"]]` is the literal `{"staff","lending"}` for
`acl && $1` and the JSON `["staff","lending"]` for `$1::jsonb`, and the text
on the wire differs, so when a call passes an array the module **prepares and
describes** the statement first and renders each array parameter by the type
Postgres inferred for its position. Every call that worked before still
works: a `jsonb` column still receives JSON, and an array in an untyped
position (`select $1`) is JSON text, as it always was. Records are always
JSON. The describe costs about 1.3× a trivial local query and happens only
when an array is passed.

```basic
rows = db.query("select id from chunks where acl && $1", [ctx.groups])
rows = db.query("select id from orders where id = any($1)", [[3, 4, 99]])
db.exec("insert into t (tags, scores) values ($1, $2)", [["a", "b c"], [0.1, 0.2]])
```

Nested arrays are sent with their dimensions (`[[1,2],[3,4]]` arrives as
`[1:2][1:2]`), an empty array as `{}`, and `nothing` as `NULL` — never as
the text. An element Postgres cannot take for the column's type (`"two"` into
`int[]`) is refused by Postgres with its own message, not silently zeroed.
Arrays of user-defined types fall through to strings per element, as their
scalars do. `bytea[]` is refused as `bytea` is.

Until this release the module raised on any array result and sent every
array parameter as JSON, which Postgres refused for an array context as
`malformed array literal`; that was documented nowhere and found by a design
that needed an ACL as `text[]`. The `jsonb` alternative still works and is
still right when the column really is JSON — see `tests/postgres_arrays.bas`,
which asserts both, with Postgres itself as the oracle for what a parameter
became.

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

rows = db.query("select id, name from users where active = ?", [true])
result = db.exec("update users set active = ? where id = ?", [false, 10])

db.close()
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
- `connection.close()` (`odbc.close`)
- `connection.query(sql[, params[, { max_rows: n }]])` (`odbc.query`) → array of
  records. `max_rows` raises on the row past the cap, as `pg.query`'s does
  (`MAX_ROWS`), closing the cursor there. It saves gBASIC's own cost of the
  rows; a driver that buffers its whole result underneath still holds it
  (measured on the SQLite3 ODBC driver: 158 MB capped against 678 for a
  million rows, the rest being the driver's).
- `connection.exec(sql[, params])` (`odbc.exec`) → `{command, rows_affected}`
- `connection.begin()` (`odbc.begin`)
- `connection.commit()` (`odbc.commit`)
- `connection.rollback()` (`odbc.rollback`)
- `odbc.drivers()` → array of `{name, attributes}` — the drivers the driver
  manager can load
- `odbc.sources()` → array of `{name, description}` — the configured DSNs

The **catalog** — what the database says about itself:

- `connection.tables([{catalog, schema, table, types}])` (`odbc.tables`)
- `connection.columns([{catalog, schema, table, column}])` (`odbc.columns`)
- `connection.primary_keys({catalog, schema, table})` (`odbc.primary_keys`) — `table` required
- `connection.foreign_keys({catalog, schema, table, referenced_catalog, referenced_schema, referenced_table})` (`odbc.foreign_keys`)

Each returns an array of records, exactly as a query does. These read through
the **driver manager**, not through `information_schema`, and that is the
point: `information_schema` is a dialect matrix that Postgres, SQL Server and
MySQL each spell differently and SQLite does not have at all, while ODBC
normalises these four calls across every database with a driver. One
organisation often runs several databases at once, so a catalog read that only
works against one of them cannot describe the estate.

**The column names are the driver's, not ours.** ODBC specifies the result
columns for each call — `TABLE_CAT`, `TABLE_SCHEM` (no `A`), `TABLE_NAME`,
`COLUMN_NAME`, `ORDINAL_POSITION`, `FKCOLUMN_NAME`, `PKTABLE_NAME` and so on —
so the raw names *are* the portable interface, and renaming them here would add
a mapping that can drift while discarding whatever extra columns a driver
supplies. Friendly names belong to the library above this.

**An absent option means "any", not "empty".** ODBC distinguishes a `NULL`
pattern (match anything) from `""` (match only objects that have no catalog or
schema), and conflating them returns nothing at all on a database that
qualifies its objects. A field you do not supply is passed as `NULL`; an empty
string is passed through as written.

**An omitted pattern means "any" — except on SQL Server.** ODBC specifies that
an absent argument matches anything, and SQLite, MariaDB and PostgreSQL all
honour that. FreeTDS against SQL Server refuses `odbc.columns` with no table:
*"sp_columns expects parameter '@table_name', which was not supplied"*. Pass
`table: "%"`, which says the same thing and is accepted everywhere.

**The qualifier is not in the same place on every database.** Measured
2026-09-08 across four drivers with the same table:

| | `TABLE_CAT` | `TABLE_SCHEM` |
|---|---|---|
| SQLite | *nothing* | *nothing* |
| MariaDB | the database | *nothing* |
| PostgreSQL | the database | `public` |
| SQL Server | the database | `dbo` |

So code that builds a qualified name as `schema.table` produces
`nothing.orders` against MariaDB. Read both, and expect either to be absent.

**An interior NUL survives the bind and not always the read.** gBASIC strings
are counted, so `"a" + chr(0) + "b"` is three bytes everywhere in the language,
and `odbc.exec` binds all three — measured 2026-09-24 by asking the database
(`select hex(v)` answered `610062`). Reading it back through the SQLite3 ODBC
driver returns **one** byte: `SQL_C_CHAR` data is NUL-terminated by
construction, and that driver's length indicator agrees with the truncation.
Nothing in gBASIC can recover what the driver did not hand over.

Native `sqlite` carries such a value both ways; `pg` refuses it, because
PostgreSQL's `text` cannot hold the byte at all. If you need arbitrary bytes
through ODBC, encode them (`hex_encode`, `base64_encode`) rather than relying
on the driver.

**Type codes are the driver's opinion, not the SQL type's.** The same
`varchar(20)` is `DATA_TYPE = 12` (`SQL_VARCHAR`) on PostgreSQL, SQL Server and
SQLite, and `-9` (`SQL_WVARCHAR`) on MariaDB — and both PostgreSQL drivers,
ANSI and Unicode, agree on 12, so it is not an ANSI/Unicode split. `TYPE_NAME`
diverges further (`int4`, `int`, `INT`, `INTEGER` for the same column), and
SQLite embeds the size in it (`varchar(20)`). Comparing types *across* sources
needs its own mapping; neither field is portable on its own.

**`IS_NULLABLE` is empty on PostgreSQL** — psqlODBC returns `nothing` where the
others return `YES`/`NO`. Use the numeric `NULLABLE`, which all four populate.
And SQLite reports a primary key as `NULLABLE = 1`, which is wrong: nullability
from SQLite cannot be trusted.

`odbc.foreign_keys` answers two different questions and you must say which:
`table` gives the keys **defined on** that table, `referenced_table` gives the
keys **pointing at** it. Supplying neither is refused rather than guessed.

Parameters are arrays and are bound through `SQLBindParameter`; use the ODBC
positional placeholder `?`. SQL `NULL` maps to `nothing`. Query results are
arrays of records; duplicate column names are errors, because a record would
silently keep only the last of them. `odbc.exec` refuses a statement that
returns rows — use `odbc.query`. ODBC errors use `error.source = "odbc"` and
carry the driver's own SQLSTATE and message.

Two connection-string options are **warned about at connect time**, because
both fail silently and produce a plausible wrong answer rather than an error:

- **FreeTDS without `ClientCharset=UTF-8`** stores non-ASCII text one byte per
  character. It round-trips through gBASIC, so a write-then-read test passes
  while the database holds mojibake.
- **psqlODBC without `BoolsAsChar=0`** reports a `boolean` column as
  `SQL_VARCHAR`, so it arrives as the string `"1"` or `"0"`. **`"0"` is a
  non-empty string and therefore TRUE in gBASIC**, so `if row.is_active` is
  taken for a row the database says is inactive — a wrong answer in the
  permissive direction, with nothing raised. MariaDB and SQL Server report
  `bit` as `SQL_BIT` and are unaffected.

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
reject. See "Four serializers, four jobs".

A response whose body parses as JSON carries it as `json`. **When the server
says the body is JSON (its `content-type` names json) and it does not parse,
the response carries `json_error`** (*since 0.11.0*), the reason and the byte,
so the failure is seen where it happens rather than later as "the answer is
not what I expected".

Use `webclient.request` for custom headers:

```basic
headers = {}
headers["Content-Type"] = "application/json"
headers["X-Client"] = "gbasic"

response = webclient.request({
    method:"POST",
    url:"https://api.example.com/events",
    headers:headers,
    body:json_encode({name:"launch"})
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
| `mail.is_ascii(text)` | true when every codepoint is below 0x80 — it compares `byte_count` with `len` rather than scanning the text. |
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

## Actor replies in the event loop: `watch(inbox.messages)`

`receive()` blocks. That is right for a sequential program and fatal in a
request handler, because a watcher *is* the event loop — a worker pool whose
replies are collected with `receive` has moved the work off the loop and then
made the loop wait for it.

An interpreter has exactly one inbox, so it joins the loop's `poll` set and a
message arrives as an event:

```basic
watch reader(inbox.messages)
    while count(inbox.messages) > 0
        m = take_first(inbox.messages)
        print("worker said " + string(m.answer))
    end while
    if done then
        unwatch reader
    end if
end watch

me = self()
w = spawn worker(me)
send(w, { job: 1 })
```

`send`, `receive` and `spawn` are builtins with no `load`, so the global record
is bound when the **watcher is registered** — the watch is what declares that
this program wants delivery, and the binding follows from it.

**A mailbox has no completion**, unlike an http transfer, so the program says
when it is finished: `unwatch` ends the delivery and the loop exits. Without it
a program waits forever for a message that may never come.

`receive()` inside a watcher **warns**, for the same reason and in the same
words as `http.wait`.

## Identity: `with principal`

`with principal(p) ... end with` declares the identity on whose behalf the body
acts, and `principal()` reads it. The word is the one the security literature
uses for exactly this, and it is deliberately not `context` — `reasoning`
already defines a Context as `{objectives, thresholds, authority, approval}`,
which is a different thing that will be loaded into the same program.

```basic
with principal({ user: "alice", groups: ["staff"], tenant: "acme" })
    post_entry(ledger, entry)        ' every layer below sees who asked
end with
```

`principal()` answers `nothing` when no block is open — **never an empty
record**. "Nobody said" and "acting for a principal that happens to carry no
fields" are different claims, and the point of an enforcement layer is to be
able to refuse the first.

The block takes a **record**; anything else is refused, naming the kind. The
*fields* are yours: a deployment with tenants and one without do not carry the
same identity, so the language checks the shape and leaves the contents to
whatever enforces them.

**Dynamically scoped**, like `on warning`'s mode and unlike `on error`'s frame.
A function three levels down sees the caller's principal without any signature
carrying it, because "who is this being done for" is the caller's answer to
give. Nesting shadows; leaving restores — including on a `return`, a `goto` or
a raise, so a scope never outlives the work it was opened for.

`principal` is not a reserved word. It rides the `with lock(...)` production,
whose opener is recognised by position, so a variable, a field and a parameter
may all be called `principal`.

### Two places it deliberately does not reach

**An actor does not inherit it.** `spawn` is fork and exec, so a child begins
with no principal at all — the isolation falls out of the process model rather
than being enforced. A worker that should act for someone re-enters the scope
from the message it received:

```basic
function worker()
    job = receive()
    with principal(job.principal)
        handle(job)
    end with
end function
```

That is the designed handoff. An identity that travelled implicitly would be an
identity nobody wrote down.

**A watcher fired by the event loop does not inherit it either**, for the same
structural reason: the loop runs after `main`, outside every block the program
opened. So a request handler sees no principal and must establish one from the
request — which is where it should come from anyway, since a request is acted
on for whoever *sent* it, not for whatever the program was doing when it bound
the socket.

```basic
watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        with principal(identify(req))
            append(server.responses, handle(req))
        end with
    end while
end watch
```

## HTTP Module

`webclient` performs one request and returns when it has finished, which is the
right shape for a script and the wrong one for anything that also has to answer
a socket. `http` is the same request over libcurl's *multi* interface: a
request is a **handle** that makes progress while the program does something
else. It needs libcurl, like `webclient`.

The six verbs are the ones `process` gives a child, and they mean the same
things:

```basic
load http

h = http.start({ url: "https://example.com/feed" })
status = http.wait(h)
print(status.status)
print(h.read().body)
```

- `http.start(request) -> handle`
- `handle.poll() -> status` (`http.poll`) — never blocks
- `handle.read() -> { id, body }` (`http.read`) — the bytes that have arrived
- `handle.wait([seconds]) -> status` (`http.wait`) — blocks until it finishes
- `http.stop(handle) -> true` — cancels; what arrived stays readable
- `handle.release() -> true` (`http.release`) — explicit end of interest

The request record is `webclient.request`'s, with one difference: `timeout` has
**no default**. A handle is the shape a stream arrives in, and a stream meant to
stay open for an hour must not be killed by a default nobody wrote. A connect
timeout of 30 seconds always applies, because failing to reach the server is not
something anyone streams through, and `http.stop` is the general answer to a
transfer that has gone on long enough.

A handle answers `handle.id` and `handle.kind`. Everything that changes is on
the status record: `id`, `running`, `status`, `reason`, `headers`,
`transport_ok`, `error`, `bytes`.

**`transport_ok` is not `status`.** `transport_ok` says the bytes arrived;
`status` says what the server thought of the request. A 500 response is a
transfer that **succeeded** and carries status 500; a refused connection has no
status at all and carries an `error`. Reading one as the other is the mistake
this pair of names exists to prevent.

`http.read` frames nothing. A partial line, a partial SSE event and a partial
UTF-8 sequence are all possible, and reassembling them is the caller's job —
exactly as with `process.read`. Bytes leave a handle exactly once: nothing is
lost and nothing is repeated across reads.

### Readiness delivered to a watcher

A handle is **waited or watched, never both.**

`http.start` registers nothing. If the program declares a watcher on
`http.events`, the event loop advances every transfer and delivers readiness
there, the way a request reaches `server.requests` — and `http.wait` *claims* a
handle, so a transfer the program blocked on is not also reported.

```basic
load http

watch(http.events)
    while count(http.events) > 0
        e = take_first(http.events)
        if e.kind = "data" then
            print(http.read(e.handle).body)
        end if
        if e.kind = "done" then
            print("finished " + string(e.id) + " " + string(e.status))
        end if
    end while
end watch

h = http.start({ url: "https://example.com/feed" })
```

`load http` binds a global record named `http` holding `events`; the module
appends to it. An event carries `id`, `kind` (`"data"` or `"done"`), `status`
and `handle` — the handle itself, so a watcher needs no table to map an id back
to a transfer, and the id beside it because a pool routes replies by id.

**Let `main` return.** The event loop runs after `main` while there is anything
to deliver — a live server *or* a watched handle — and exits when there is not.
A program that loops instead is the trap described under WebServer, for the same
reason. `http.wait` inside a watcher blocks every other transfer and every other
client, and warns.

A `data` event is reported once for the bytes currently buffered and again only
after a `read`, so a program that does not want the body is told twice at most
rather than on every turn of the loop.

**Declare the watcher before starting anything.** Dropping the last reference to
a handle cancels its transfer — in waited mode that is right, since nobody could
read the answer again. In watched mode it is not: the loop will deliver the
completion and the event carries the handle back, so the loop holds a reference
of its own, taken when the transfer starts *if a watcher is already live*. This
is what lets the natural deferred handler work:

```basic
watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        h = http.start({ url: upstream })
        pending[string(h.id)] = req.id      ' remember the request, not the handle
    end while
end watch
```

`h` is rebound on every request, and without the loop's own reference the second
request would cancel the first — two handlers run, one answer arrives, and
nothing is reported.

## Net Module

*(added 2026-11-04; [networking_design.md](networking_design.md))* TCP and
Unix sockets, client and server: the layer under any protocol that is not HTTP.
Core, with no library to install, except TLS, which needs libssl in the build.
UDP is [Datagrams](#datagrams-udp); WebSockets follow (N4 in the design).

`load net` binds a global record `net` holding `events`, as `load http` binds
`http`. A **connection** or **listener** is waited or watched, never both,
decided when it is made: **watched** when a watcher on `net.events` is already
live, so the event loop drives it; **waited** otherwise, so the program blocks
on it, with timeouts.

```basic
load net

c = net.connect({ host: "127.0.0.1", port: 7000, timeout: 10 seconds })
c.write(pack(">I", [5]) + "hello")
n = unpack(">I", c.read_exactly(4, 30 seconds))[0]
reply = c.read_exactly(n, 30 seconds)
c.close()
```

```basic
load net

watch(net.events)
    while count(net.events) > 0
        e = take_first(net.events)
        if e.kind = "data" then
            line = e.connection.take_line()
            while not is_unknown(line)
                e.connection.write("you said " + line + chr(10))
                line = e.connection.take_line()
            end while
        end if
    end while
end watch

srv = net.listen({ port: 7000 })     ' 127.0.0.1 unless address: says otherwise
```

**Lengths are bytes.** A connection carries bytes, NUL included, and every
count here is bytes. `len` counts codepoints, so framing code uses
`byte_count`.

### Making connections and listeners

- `net.connect(options) -> connection`: `host` and `port`, or `path` for a Unix
  socket; `timeout` (connect, default 30 seconds); `idle` (close after this
  long with no traffic; none by default on a client); `buffer` (the receive
  ceiling, default 16 MiB); `keepalive` (below). `idle` is enforced by the
  event loop, so it applies to watched connections. Each resolved address is tried in turn within the
  one timeout. A **watched** connect returns at once and reports `connected`; a
  connect to a **name** then resolves on a helper thread, so the loop never
  stalls on a lookup.
- `net.listen(options) -> listener`: `port` (0 asks the system; read
  `listener.port`), or `path` and optionally `mode` (`"0600"`) for a Unix
  socket; `address` (default `127.0.0.1`; `"0.0.0.0"` or `"::"` to reach the
  network, numeric only); `backlog`; `max_connections` (default 1024);
  `idle` (default 5 minutes, for each accepted connection); `buffer`;
  `keepalive`, given to every connection it accepts. A Unix path that already
  exists is refused (`address_in_use`), and closing the listener removes the
  path it made.
- **`keepalive`** (*since 0.11.0*) asks the system to probe an idle TCP peer,
  so that the system ends a connection whose peer vanished without closing (a
  dropped network, a host that went down). That ending was not observed in
  gBASIC's tests, which can show the timings set but not a peer going silent
  (`docs/networking_design.md` §3). `true`
  uses the system's timings. A record `{ idle, interval, count }` sets them:
  probing begins after `idle` seconds with no traffic, repeats every
  `interval`, and gives up after `count` unanswered probes. Any part left out
  is the system's, `idle` and `interval` are whole seconds from 1 to 32767
  (durations too: `idle: 2 minutes`), and `count` runs from 1 to 127. Off by
  default, and refused on a Unix socket. It complements `idle`, which closes a
  connection with no traffic from either end, while keepalive is for a *peer*
  that stopped answering. **TCP_NODELAY
  is always on**: a write is sent at once rather than held to be joined with
  the next one.
- `listener.accept([timeout]) -> connection` (waited). Watched, connections
  arrive as `accepted` events instead.
- `net.resolve(host) -> [ { address, family } ]`, blocking.

An unknown option is refused by name, as everywhere.

### Reading

The **take** family never blocks, answering `unknown` when what was asked for
has not fully arrived. Use it in a watcher:

| call | answers |
|---|---|
| `c.take([max])` | whatever is buffered (up to `max` bytes), possibly `""` |
| `c.take_exactly(n)` | exactly `n` bytes, or `unknown` |
| `c.take_until(delimiter [, { include: true }])` | up to the delimiter, or `unknown` |
| `c.take_line()` | up to `\n`, a trailing `\r` removed, or `unknown` |
| `c.peek_exactly(n)` | as `take_exactly`, leaving the bytes buffered (for a length prefix) |
| `c.buffered()` | how many bytes are waiting |

The **read** family blocks, for waited connections: `c.read([max], [timeout])`,
`c.read_exactly(n, [timeout])`, `c.read_until(delimiter, [timeout])`,
`c.read_line([timeout])`. With no timeout a read waits without limit. A timeout raises `timeout` and leaves what arrived
buffered; the peer closing before the cut is complete raises `closed`, with the
partial bytes in `error.details.partial`. `read` at end of stream answers `""`.
A read on a watched connection is refused, naming the take family, and a read
inside a watcher warns (2105), as `http.wait` does.

A count or delimiter that cannot fit the receive buffer is refused at the call
(`too_long`), rather than waiting for a frame that can never arrive.

### Writing and closing

- `c.write(bytes) -> count` queues the bytes and sends what the socket will take
  now; the rest goes as the socket allows, so a partial send is never the
  program's to handle. Past the send ceiling (16 MiB queued) a write is refused
  (`backpressure`). A watched connection whose queue reached 64 KiB then reports
  `writable` when it drains below that, so "write while `c.queued()` is under
  64 KiB" always gets its event.
- `c.queued()`: bytes not yet sent. `c.flush([timeout])` (waited) blocks until
  they are.
- `c.shutdown_write()` half-closes. `c.close()` closes once the queue drains;
  `c.abort()` closes now and discards it. `listener.close()` stops accepting.

### Events

Each event in `net.events` is `{ kind, id, connection, listener, code, message }`:

| kind | when |
|---|---|
| `accepted` | a listener took a connection (`listener` is set); on a TLS listener, once its handshake is done. A connection the webserver handed over by an [upgrade](#websockets) has no listener and carries `request` |
| `connected` | a watched connect finished (with `tls:`, its handshake too) |
| `tls` | a watched `start_tls` finished its handshake |
| `data` | new bytes arrived since the last `data` event |
| `writable` | the send queue drained below 64 KiB after reaching it |
| `full` | the receive buffer reached its ceiling, and reading has paused until the program takes |
| `idle` | the idle timeout passed; `closed` follows |
| `error` | the connection failed (`code`, `message`); `closed` follows. On a listener, `code` `limit` means a connection was refused at `max_connections` |
| `closed` | `code` says why: `peer`, `local`, `aborted`, `idle`, or a failure's code |
| `datagram` | a datagram socket received one: `socket`, `data`, `from`, `truncated`, `size` (see [Datagrams](#datagrams-udp)) |

**A `data` event means new bytes, not unread bytes.** Take everything you can
use in the handler: bytes left buffered are not reported again until more
arrive. The loop holds a reference to every watched socket until its `closed`
event, so rebinding `c` in a handler does not close the previous connection,
and a program whose sockets have all closed lets the loop end.

### What a connection says about itself

`c.id`, `c.kind` (`"connection"`, `"listener"` or `"datagram"`), `c.open`, `c.watched`,
`c.peer` and `c.local` (`{ address, port }` or `{ path }`), `c.port`, `c.path`,
`c.tls` (see [TLS](#tls)), `c.state` (below), `listener.connections` (how many are open),
on a TCP connection `c.keepalive` (`false` or `{ idle, interval, count }`) and
`c.nodelay` (`true`), both **read from the socket**, so they are what the
system is doing rather than what was asked, and
on a Unix connection `c.peer_credentials`: `{ pid, uid, gid }` of the process
at the other end (`pid` is `unknown` on the BSDs and macOS). That is how a
local server authenticates a user by who they are.

**`c.set_state(name, value)`** keeps a named value with a connection, read
back as `c.state.name` (`c.state` is a record, `{}` at first). It is for a
library over `net` that needs to remember something between events, as
`websocket` keeps its handshake and a message's fragments under `"websocket"`;
the name keeps two libraries on one connection apart. A value that names the
connection itself keeps the connection alive, so do not store one.

### Failures

Raised with code 1004 and source `net`, and `error.details.code`:

| `details.code` | meaning |
|---|---|
| `refused` | nothing is listening there |
| `timeout` | the connect, read, flush or accept did not finish in time |
| `reset` | the peer reset the connection |
| `closed` | the peer closed before what was asked for arrived, or a write after close |
| `unreachable` | no route to the host |
| `name_not_found` | the host name does not resolve |
| `address_in_use` | the port or Unix path is already held |
| `permission` | a privileged port, or a path's permissions |
| `backpressure` | a write past the send ceiling |
| `too_long` | a count or delimiter past the receive ceiling |
| `too_large` | a datagram longer than one datagram can carry (see [Datagrams](#datagrams-udp)) |
| `tls_verify` | a certificate did not verify; libssl's reason is in `details.reason` |
| `tls_failed` | the handshake or the TLS connection failed otherwise, or a certificate or key did not load; the message says why |
| `unexpected_data` | `start_tls` with bytes already buffered (see [TLS](#tls)) |

A watched connection reports the same code in its `error` event. Misuse (a
missing option, a wrong kind of argument) is the ordinary 1003.

### TLS

*(added 2026-11-04, N2)* Encrypted connections, client and server, and an
upgrade of one already open. Optional: TLS needs libssl in the build, as
`webserver`'s does, and without it a `tls:` option is refused with a sentence
saying so. Everything above (the take and read families, backpressure, idle
timeouts, the events) works on a TLS connection unchanged.

<!--fragment: needs a TLS server on the network; tests/net/tls_client.bas is the runnable form-->
```basic
load net

c = net.connect({ host: "example.com", port: 443, tls: true })
c.write("HEAD / HTTP/1.0" + chr(13) + chr(10) + "Host: example.com" + chr(13) + chr(10) + chr(13) + chr(10))
print c.read_line(10 seconds)
print c.tls.version + " " + c.tls.peer_certificate.subject
c.close()
```

**A client verifies by default**: against the system's trusted certificates,
with the host name checked against the certificate (an address is checked
against its IP names, and sends no SNI). A failure raises `tls_verify` with
libssl's reason in `error.details.reason`, such as `hostname mismatch`,
`certificate has expired` or `unable to get local issuer certificate`. Not
verifying is spelled out: `tls: { verify: false }`.

`tls:` on `net.connect` is `true` or a record:

| option | meaning |
|---|---|
| `server_name` | the name checked, and sent as SNI (default: `host`). A Unix socket has no host, so it needs this or `verify: false` |
| `ca` | a file or directory of authorities to trust instead of the system's |
| `verify` | `false` accepts any certificate |
| `cert`, `key` | a client certificate, for a server that asks for one |

`tls:` on `net.listen` is a record, the one `webserver.listen` takes, loaded by
the same code:

| option | meaning |
|---|---|
| `cert`, `key` | the certificate chain and its private key, PEM files |
| `certs` | `[ { host, cert, key } ]`, chosen by the name the client asks for (SNI). A name not listed gets `cert` |
| `client_ca` | ask each client for a certificate from these authorities |
| `require_client_cert` | `true` refuses a client without one; needs `client_ca` |

Certificates load when the listener is made, so a file that does not load, or
a key that is not the certificate's, raises `tls_failed` at `listen` naming the
file. TLS 1.2 is the floor on both sides.

**The handshake is part of making the connection.** A waited `net.connect`
returns when it has finished, within the one `timeout`; a watched one reports
`connected` after it, and a TLS listener reports `accepted` after it, so
`c.tls` is ready in both handlers. A handshake that fails on a listener is an
`error` event naming the `connection` and its `listener`, then `closed`, with
no `accepted` (the program was never handed it); one that never finishes is
closed at the listener's `idle`. A waited `accept` on a TLS listener waits for
the handshake too, and raises if it fails, so a waited TLS server catches its
`accept`: one bad client would otherwise end it.

A TLS 1.3 server refuses a client certificate after the client's handshake has
finished, so a client learns of it at its next read or write, as `tls_failed`
naming the server's alert (`tlsv13 alert certificate required`). That is the
same whichever of the two comes first.

#### `start_tls`: upgrading an open connection

`c.start_tls(tls [, timeout])` begins the handshake on a connection that is
already open, as PostgreSQL's `SSLRequest` and the `STARTTLS` of SMTP, LDAP and
IMAP need. `tls` is what `connect` or `listen` would take, by which side the
connection is: one from a listener takes the server's options.

<!--fragment: needs a PostgreSQL server; tests/net/tls_peer.py's pg service is the runnable form-->
```basic
c = net.connect({ host: "db.example.com", port: 5432 })
c.write(pack(">I I", [8, 80877103]))          ' PostgreSQL's SSLRequest
if c.read_exactly(1, 10 seconds) = "S" then
    c.start_tls(true)
end if
```

Waited, it returns `true` once the handshake is done (within `timeout`,
default 30 seconds) and closes the connection if it fails. Watched, it returns
at once and a `tls` event reports the end. What was written before it is sent
in plaintext first.

**Bytes that arrived before the handshake are refused.** If any are buffered,
`start_tls` raises `unexpected_data`: they came in plaintext, and reading them
as part of the TLS stream is the STARTTLS injection. Take them first if the
protocol allows them.

#### What a TLS connection says about itself

`c.tls` is `nothing` on a plain connection and until the handshake finishes,
then `{ version, cipher, server_name, peer_certificate }`. `peer_certificate`
is `nothing` when the peer sent none (a client nobody asked), otherwise
`{ subject, issuer, not_before, not_after, sha256 }`: names as RFC 2253 text
(`CN=localhost`), times as UTC datetimes, and `sha256` the certificate's
fingerprint in lowercase hex, `openssl x509 -fingerprint -sha256` without the
colons, so a program can pin a certificate. The facts outlive the connection,
so a `closed` handler can read them. A listener's `l.tls` is
`{ hosts, client_certificate }`, the second `"none"`, `"requested"` or
`"required"`.

`close` sends TLS's `close_notify`, which is how the peer tells an end from a
truncation; `abort` does not. `shutdown_write` sends it too, and the peer may
still send. A peer that closes without one is reported as closed by the peer,
as most TLS stacks do; a protocol that frames its messages detects truncation
itself.

### Datagrams (UDP)

*(added 2026-11-04, N3)* `net.datagram(options)` makes a UDP socket. **A
datagram is never split or joined**: one `receive`, or one `datagram` event,
is one datagram, whatever arrived around it. Waited or watched, as a
connection is.

```basic
load net

u = net.datagram()                              ' a client's: any address, a port the system picks
u.send_to("<13>hello", { address: "127.0.0.1", port: 5140 })
r = u.receive(5 seconds)                        ' { data, from, truncated, size }
u.send_to("thanks", r.from)                     ' from is { address, port }: a reply goes straight back
u.close()
```

```basic
load net

watch(net.events)
    while count(net.events) > 0
        e = take_first(net.events)
        if e.kind = "datagram" then
            e.socket.send_to(e.data, e.from)    ' an echo server
        end if
    end while
end watch

srv = net.datagram({ port: 5140 })              ' 127.0.0.1 unless address: says otherwise
```

| option | meaning |
|---|---|
| `port` | the port to bind; 0 asks the system. A socket with a port is a server's and binds to `127.0.0.1` unless `address:` says otherwise |
| `address` | a numeric address to bind (`"0.0.0.0"`, `"::"`, `"::1"`). With neither, the socket is a client's and binds to `0.0.0.0` on a port the system picks, which is what an unbound UDP socket gets at its first send: bound to loopback, a client could reach nothing else |
| `max_datagram` | the longest datagram delivered whole, 1 to 65536 bytes (default 65536, more than UDP carries) |

- **`send_to(bytes, to [, timeout])`** sends one datagram and answers the
  bytes sent. `to` is `{ address:, port: }` (numeric) or `{ host:, port: }`
  (a name, resolved as `net.resolve` does, so it blocks). A received `from`
  is a valid `to`. There is **no send queue**: the system takes the datagram
  before `send_to` returns, and when it has no room, `send_to` waits for some
  within the timeout (default 10 seconds). An IPv4 address is reachable from
  a socket bound to `"::"`; a socket bound to IPv4 refuses an IPv6 address,
  naming the fix.
- **`receive([timeout])`**, waited only, answers `{ data, from, truncated,
  size }`. Without a timeout it waits until one arrives.
- **A datagram longer than `max_datagram` is reported, never silently cut**:
  `data` holds the bytes that fit, `truncated` is `true`, and `size` is the
  length that was sent (`unknown` off Linux, where the system does not say).
  The rest of it is gone; the next receive is the next datagram.
- **Watched**, each datagram is an event `{ kind: "datagram", socket, data,
  from, truncated, size }`. Datagram events name the socket as `socket`, and
  `connection` and `listener` are `nothing`; so do its `error` and `closed`.
  A watched datagram socket keeps the loop alive until it is closed.
- `close` and `abort` end it at once, since nothing is queued (`closed` says
  `local` or `aborted`). The stream verbs (`write`, the take and read
  families, `queued`, `flush`, `shutdown_write`) are refused on it, and
  `send_to` and `receive` on a connection.
- `u.kind` is `"datagram"`; `u.port`, `u.local`, `u.open`, `u.watched` and
  `u.max_datagram` are as for a connection, and `u.peer` is `unknown`.
- Failures add `too_large` (longer than one datagram can carry, 65507 bytes
  over IPv4) to the codes above; `timeout`, `closed`, `address_in_use` and
  `name_not_found` mean what they do for a connection.

Multicast and broadcast are not built (a later increment, §4 of the design);
`broadcast:` is refused as an unknown option. Unix datagram sockets are not
built either.

### WebSockets

*(added 2026-11-04, N4)* `load websocket`: RFC 6455, client and server, as a
gBASIC library over `net`. It needs libcrypto in the build (the handshake is
`sha1`). A watched WebSocket is an ordinary watched connection, and
**`websocket.events(e)` turns one `net` event into the WebSocket events it
means**, usually none or one, so a program keeps its one `watch(net.events)`:

```basic
load websocket

server app( port: 8080 )
    get "/chat"( req )
        return websocket.accept(req)            ' the 101, or a 400 if it is not a handshake
    end get
end server

program main( args )
    watch(net.events)
        while count(net.events) > 0
            e = take_first(net.events)
            for each w in websocket.events(e)
                if w.kind = "message" and not w.binary then
                    websocket.send(w.connection, "you said " + w.data)
                end if
            end for
        end while
    end watch
    h = web.serve(app)
end program
```

```basic
load websocket

c = websocket.connect("wss://example.com/feed")   ' waited: returns once the handshake is answered
websocket.send(c, "subscribe")
m = websocket.receive(c, 30 seconds)              ' { kind: "message", data, binary } or { kind: "closed", ... }
websocket.close(c)
```

| function | what it does |
|---|---|
| `connect(url [, options])` | `ws://` or `wss://`. Options: `headers`, `protocols` (offered), `timeout` (default 10 s), `tls` (as `net.connect`'s, default `true` for `wss`), `max_message`, `idle`. Waited, it returns once the server has answered; watched, the handshake is sent at once and an `open` event says it was answered |
| `accept(req [, options])` | the response a route returns: the 101 for a WebSocket handshake, a 400 for anything else, a 426 naming version 13 for another version, so `return websocket.accept(req)` is always right. Options: `protocols` (the server's, the first the client offered is chosen), `max_message`, `idle` |
| `is_request(req)` | whether a request asks to become a WebSocket |
| `events(e)` | the WebSocket events a `net` event means, as an array |
| `send(c, text)` | a text message; text that is not UTF-8 is refused here (`not_utf8`), naming `send_binary` |
| `send_binary(c, bytes)` | a binary message |
| `ping(c [, data])` | at most 125 bytes; the `pong` comes as an event |
| `close(c [, code, reason])` | begins the closing handshake (default 1000); the peer's close finishes it. Waited, it waits up to 5 seconds for that |
| `receive(c [, timeout])` | waited only: the next `message` or `closed`. A ping is answered and skipped, a pong skipped |

Events are `{ kind, connection, ... }`: `open` (`request`, the request the
route accepted, `nothing` on a client; `protocol`), `message` (`data`,
`binary`), `ping` and `pong` (`data`), `closed` (`code`, `reason`, `clean`)
and `error` (`code`, `message`). **A ping is answered for you**, and a close
from the peer is echoed and the connection closed, as the RFC requires; the
events report them. A connection that ends without a close frame is `closed`
with code 1006 and `clean: false`.

**What is refused closes the connection with the code that names it**: a
reserved bit set (no extension is negotiated), a client frame not masked or a
server frame masked, a fragmented or over-125-byte control frame, a
continuation with nothing to continue, a reserved opcode, or a close code no
endpoint may send (1002); text that is not UTF-8 (1007); a message past
`max_message`, default 8 MiB (1009). A message sent as fragments arrives whole.
Raises carry `error.details.reason`: `bad_url`, `bad_option`, `handshake`,
`not_open`, `not_websocket`, `not_utf8`, `watched`; a failure of the
connection underneath keeps `net`'s `details.code`.

An upgraded connection has **no idle timeout** unless `idle:` sets one, since
a quiet WebSocket is ordinary; a program that wants liveness pings. In a worker
pool a WebSocket belongs to the worker that accepted it. Extensions
(`permessage-deflate`) are not offered.

## Timer Module

Periodic work on the event loop. Needs no `load` — there is no optional
dependency, only clock arithmetic — and delivers through a watched global, like
`http.events` and `inbox.messages`. See `docs/timer_design.md`.

Every other source the loop polls fires because something arrived: a request, a
reply, a transfer. Nothing fired because **time passed**, so periodic work had
one spelling — `sleep` in a loop — and on the event loop that means the handler
never returns and its worker never comes back.

```basic
t = timer.every(0.5)

watch(timer.ticks)
    while count(timer.ticks) > 0
        ev = take_first(timer.ticks)
        poke_the_streams()
    end while
end watch
```

- `timer.every(seconds) -> timer` — repeating.
- `timer.after(seconds) -> timer` — one-shot; removes itself when it fires.
- `timer.cancel(timer) -> boolean` — `true` if it was live, `false` if it had
  already gone. A timer going away twice is an ordinary outcome, not an error.
- `timer.ticks` — the delivery queue.

A timer is an ordinary record, `{ id, interval, repeating }`. A tick is
`{ id, kind: "tick", count, skipped, timer }`.

**`skipped` is the field to read.** Ticks are **coalesced, never caught up**: at
most one per timer per loop iteration, and the next due time is computed from
the delivery, so a handler slower than the interval loses ticks instead of
producing a burst — a burst on the event loop is an unbounded queue, and its
symptom is a hang rather than a failure. What is lost is reported: `skipped` is
the number of scheduled intervals not delivered since the previous tick, `0`
while the loop keeps up. Intervals elapsed is `1 + skipped` per tick, so a
program measuring time by counting ticks has what it needs.

The clock is `CLOCK_MONOTONIC`. A wall-clock timer stalls on an NTP step
backwards and fires every interval it crossed on a step forwards, both silently.

**What keeps the program running, and what lets it stop.** The loop runs while a
watcher on `timer.ticks` exists *and* at least one timer is live. So a program
whose only timer is `timer.after(...)` **exits by itself** once it fires, and one
holding a repeating timer runs until `timer.cancel` or `unwatch` — the same
bargain `watch(inbox.messages)` makes, and for the same reason: a clock has no
completion of its own, so the program says when it is done.

A live timer with no `watch(timer.ticks)` anywhere delivers nothing, and is
reported (warning `2108`, at the timer's own creation site) when the program
would otherwise exit. A warning, not a refusal: the `watch` may legitimately be
registered later, and the program runs exactly as it did.

**It does not preempt.** A tick is delivered between loop iterations, so a
handler that blocks delays every timer; `skipped` is how a program sees that.
And it does not cross workers — `workers: N` is N processes, each running the
program and registering its own timers.

The shape this exists for is a parked stream poked on a schedule:

```basic
server app( port: 0 )
    stream "/events"( req )
        e = web.emit(req, web.sse_event("open"))
        append(G.streams, req)
        return 0
    end stream
end server

program main( args )
    G = { streams: [] }
    h = web.serve(app)
    t = timer.every(0.2)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            alive = []
            for each s in G.streams
                if web.emit(s, web.sse_named("tick", string(ev.count))) then
                    append(alive, s)
                end if
            end for
            G.streams = alive
        end while
    end watch
end program
```

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

`server.tls` is `true` when this listener terminates TLS and `false` when it
serves plain HTTP (*since 0.11.0*). It is read from the socket's own state,
not from the options asked for, so a program that must not run without TLS
can check it: `if not h.tls then error "refusing to serve without TLS"`.
`web.serve`'s answer is that record, so it carries the flag too. Under
`workers: N` the supervisor's held socket is plain (each worker terminates
TLS), and `serve` does not return while the pool runs.

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

**Client certificates** take `net.listen`'s two options: `client_ca`, a file of
the authorities a client's certificate must come from, and
`require_client_cert: true`, which refuses a client with no certificate in the
handshake. A certificate a client offers must verify either way. Any other name
in `tls` is refused by name. A `server` block takes both as head options, or
from `web.configure`. (Until 2026-11-07 `webserver.listen` dropped both, and
any misspelling, in silence.)

```basic
server = webserver.listen(8443, { tls: { cert: "site.crt", key: "site.key",
    client_ca: "clients-ca.crt", require_client_cert: true } })
```

Requests carry `scheme` (`"https"` on a TLS listener, else `"http"`). An
https request also carries `tls`, the handshake's facts as `net`'s `c.tls` has
them: `version`, `cipher`, `server_name` and `peer_certificate` (`{ subject,
issuer, not_before, not_after, sha256 }`, or `nothing` when the client sent
none). A websocket upgraded from it keeps the same session, so its
`connection.tls` names the same certificate.
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
- `form`: record of the body decoded as `application/x-www-form-urlencoded` —
  what an HTML form POSTs. It shares the `query` parser, because the two are
  the same grammar: pairs joined by `&`, percent-decoded, with `+` meaning
  space. So `pass=p%40ss%26word` gives `p@ss&word`, encoded separator and all.
  **Only a form content type is decoded**; a JSON, multipart or absent type
  gives an **empty** record rather than nonsense — splitting `{"a":1}` on `&`
  and `=` would otherwise yield a field named `{"a"` holding `1}`, which is a
  plausible-looking record and the wrong one. A `; charset=…` parameter is
  ignored. Multipart bodies (file uploads) are not decoded.
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
    body:json_encode({saved:true})
})
```

**An upgrade** (*added 2026-11-04, N4*): a response with `upgrade: true` and
status `101` switches protocols. The webserver writes `101 Switching
Protocols` with the response's headers and hands the socket, and its TLS
session, to the `net` module, where it arrives as an `accepted` event carrying
the response's `request` field (any value) and no listener. Bytes the client
sent after its request are the connection's first input. It needs a watcher on
`net.events` to arrive at, and is refused without one; it cannot carry a body,
file or stream. `idle` (seconds) sets the connection's idle timeout, none by
default. This is what `websocket.accept` returns; see
[WebSockets](#websockets).

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

**Head options are literals** — number, string, `true`/`false`. The binding
reason is *when* a block exists: `server` declarations are pre-registered
before anything runs, so a head option has nowhere to evaluate an expression —
`workers: n` would read `n` before it was assigned. (Measured, only a site's
`host` has its literal value read by a load-time check, to refuse two sites
claiming one name; for the rest the literal rule is about evaluation order, not
checkability.) The restriction is also what keeps every load-time check
statically decidable:
duplicate `method+path`, patterns that can never be told apart, malformed
`{captures}`, unknown verbs/directives/hooks, duplicate hosts, `root`
twice, a computed option. All of it is refused at load with file:line
through the same diagnostics as parse errors (`--json-diagnostics` code
`GB_DIAG_SERVER_BLOCK`). Deliberately runtime instead: certificate file
existence, port availability, static directory existence.

**A computed option does not require abandoning the block.**
`web.configure(sv, settings)` returns *another declaration* with deployment
settings merged in, so a value known only at run time — a port from `/etc`, a
worker count from the environment — reaches the runtime without the head giving
up its literals:

```basic
cfg = web.configure(app, {
    port:    number(conf.port),
    workers: number(env("WORKERS"))
})
h = web.serve(cfg)
print "listening on " + string(h.port)
```

Because the result is a declaration, `web.routes`, `web.dispatch` and the
outline all keep working on it, and configuring twice composes. Every head
option is accepted — `port`, `address`, `inherit`, `workers`, `timeout`,
`cert`, `key` — and nothing else: an unknown name is refused **by name** and a
wrong type by type. `host` is refused for its own reason, because a site's host
is the one option value a load-time check actually reads.

The record can also be written directly, `app.options.port = number(conf.port)`,
and that still works — but it validates nothing. `app.options.workesr = 4` is
accepted in silence, creates a field nothing reads, and leaves the server
running on the declared value while the author believes otherwise. Prefer
`configure`; it also leaves the declaration itself unmodified, which matters
once anything else reads it.

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

**`serve(sv)`** (a `web` function, so `web.serve(sv)`) binds and runs:

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

**A `stream` handler must return, like any other.** It is not held open for
the length of the stream: the connection outlives the handler, so the shape
that keeps a stream alive is to emit whatever opening event the client needs,
**park the request** in a program global, and return.

```basic
server app( port: 0 )
    stream "/events"( req )
        e = web.emit(req, web.sse_event("open"))
        append(G.streams, req)
        return 0
    end stream

    post "/notify"( req )
        alive = []
        for each s in G.streams
            if web.emit(s, web.sse_event(req.body)) then
                append(alive, s)
            end if
        end for
        G.streams = alive
        return { status: 200, body: "ok" }
    end post
end server

program main( args )
    G = { streams: [] }
    h = web.serve(app)
    print "PORT " + string(h.port)
end program
```

Two consequences follow, and both are properties a scale-minded reader will
want stated rather than inferred. **Parked streams do not occupy workers**:
each is a socket the event loop holds, so N open streams on one worker leave
that worker free to answer ordinary requests (`tests/run_web_stream.sh`'s
`SSE_BROADCAST` tier pokes a stream parked by one request from a *second*
connection, which is only possible because the first handler returned).
And **`web.emit` returns false when the client has gone**, which is the only
signal a departed reader gives — the reaping loop above is the idiom, and
without it a dead client holds a slot forever.

**A parked stream belongs to the worker that accepted it.** `workers: N` is
N separate processes, so `G.streams` is N separate lists and a poke reaches
only the streams parked in the process that handled the poke. Measured with
`workers: 2` and four streams open, where the accepts split 3/1: the first
`POST /notify` answered `poked 3` and reached three of the four clients, and
the next three answered `poked 1` and reached only the fourth. Nothing is
wrong there — it is the same process isolation that makes `spawn` a
fork+exec and stops a principal crossing it — but a broadcast that must
reach *every* viewer either runs at `workers: 1` or needs a channel between
the workers, which the pool does not provide.

What the runtime does **not** do is wake a parked stream on a schedule.
Every event-loop source is request-, reply- or transfer-driven
(`server.requests`, `inbox.messages`, `http.events`); there is no timer. A
stream whose content arrives on a clock rather than from another request has
to be poked by something — a second process, an actor, or a request from a
scheduler — because a handler that sleeps to wait for the next tick *is*
holding its worker, and that is the limit people meet and misattribute to
streams.

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
`process.self()` → `{ interpreter, script, args, pid, started, uid, user }` (how
a program launches another copy of itself, and *since 0.11.0* its own process
id, when it started as epoch seconds, and the user it acts as; a spawned actor
is a process of its own and answers its own); `process.alive(pid)` (whether a
process with that id exists now) and `process.info(pid)` → `{ pid, alive,
started, command }`, so a pid recorded by an earlier run can be told from a
reused one by its start time and command line (`unknown` where the platform
does not say; on Linux they come from `/proc`); `webserver.on_request(server, fn, context)` /
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
print(doc.attr("id"))              ' A1
print(xml.text(doc.find("item")))  ' Widget
```

Parsing:

- `xml.parse(text[, keep_whitespace | options])` — parse an XML string. The
  second argument is either the historical `keep_whitespace` boolean (`true`
  keeps inter-element whitespace text nodes; the default drops them) or an
  **options record** `{ keep_space:, positions: }`. An unknown option is refused
  by name.
- `xml.parse_file(path[, keep_whitespace | options])` — parse from a file path,
  with the same options.

**An interior NUL is refused, by name** (*since 0.2.2*). XML 1.0's `Char`
production excludes `#x0`, so a document carrying one cannot be parsed by
anything; libxml2 stops at the byte and used to report `Premature end of data
in tag …`, which says where it gave up rather than what is wrong. `xml.parse`
now names the byte. `xml.parse_html` is deliberately unchanged — HTML's own
handling of the byte differs and is not measured here.

**Positions are opt-in** (*since 0.2.2*). With `positions: true` every element
record carries a `line` — the 1-based line its start tag begins on:

```basic
doc = xml.parse(text, { positions: true })
print doc.line                        ' 3
print doc.find("total").line     ' 27
```

Absent unless asked for, so an existing parse is unchanged and `keys(node)` does
not grow. The reason is measured rather than stylistic: `finio`'s Phase 0 found
always-on per-value provenance cost **294×** the source and answered *more*
slowly, so a position every caller pays for and few read is the wrong default.

A `line` is exact per element — two elements on one line report the same line,
and a comment between them does not shift it.

Each element also carries **`byte_start` and `byte_end`**, a half-open range
over the source that spans the element's whole text, opening tag to closing tag:

```basic
doc = xml.parse(text, { positions: true })
t = doc.find("total")
print byte_slice(text, t.byte_start, t.byte_end - t.byte_start)
' <total ccy="EUR">1250.00</total>
```

**They are byte offsets, and `mid` is codepoint-indexed** — which is why the
names carry the unit. Use `byte_slice`. On an ASCII document the two agree and
`mid` appears to work; on the first accented character they part company and
`mid` returns a plausible slice of the wrong text, with nothing raised.

**`content_start` and `content_end`** (*since 0.11.0*) give the element's
**content**, between its tags (an empty range at the element's end when it is
self-closing), and **`attribute_bytes`** gives each attribute's **value**,
inside its quotes and as written, entities not decoded: `{ ccy: { byte_start,
byte_end } }`. So a value's lineage can name the bytes of the value itself
rather than of the markup around it. Every position is an exact integer, so a
G-SQL `integer` column takes it.

Ranges are collected by a second pass over the source, so they cost another
parse — paid only when `positions` is asked for.
- `xml.parse_html(text)` — lenient HTML parse for real-world markup.

Navigation (a *path* is a `/`-separated chain of child element names):

- `node.find(path)` (`xml.find`) — the first match, or `unknown` when nothing matched. Check a find that may miss with `is_unknown` before reading it.
- `node.find_all(path)` (`xml.find_all`) — every element the path reaches, in document order (empty when none). Each step is taken from every element the step before reached, so `find_all(d, "s/n")` is the `n` under each `s`, and `find` answers the first of them.
- `node.attr(name[, default])` (`xml.attr`) — an attribute value as a string; returns
  `default` (or `unknown`) when the attribute is absent.
- `node.text()` (`xml.text`) — the node's text content, trimmed. Given `unknown`, which is what a missed find answers, it raises rather than answering `""`. It also raises for anything that is not an element or a text child.
- `xml.encode(node[, pretty])` — serialize a node back to an XML string; pass
  `true` to pretty-print.
- `xml.canonicalize(text[, options])` — the canonical bytes of a document, or
  of one element of it, as XML-DSig signs them: Exclusive XML Canonicalization
  1.0 by default, Canonical XML 1.0 with `exclusive: false`. It takes the
  **source text**, never a tree from `xml.parse`, which has already dropped
  comments and merged CDATA. Options:
  - `id` — the element whose `ID` attribute (or the unprefixed attribute
    `id_attribute` names, such as `AssertionID`) is this value.
  - `element`, `namespace` — the element of that local name (in that
    namespace URI), inside the `id` element when both are given: how a
    signature's `ds:SignedInfo`, which has no ID, is reached.
  - `exclude_signature: true` — the enveloped-signature transform: the chosen
    element's own `ds:Signature` child is left out (not one further down).
  - `signed_info: true` *(after 0.10.0)* — the `ds:SignedInfo` of that same
    signature: the `id` element's own `ds:Signature` child (the root element's
    with no `id`). When a Response and its Assertion are both signed, the
    Response's subtree holds two SignedInfos and `element` refuses as
    ambiguous; this names the Response's. Refused when the element has no
    `ds:Signature` child or more than one, when that signature holds anything
    but one `ds:SignedInfo`, and beside `element` or `exclude_signature`.
  - `inclusive_prefixes` — the `InclusiveNamespaces PrefixList`, as an array or
    as its space-separated text; `#default` is the default namespace.
  - `comments` — keep comments (default `false`).

  Refused, each by name: a document with a DTD (SAML forbids one, and
  `xml.parse` drops a declared entity in silence, so the two could disagree
  about the signed text); an `id` held by more than one element, and an
  `element` matched more than once, which is how a signature is wrapped round
  other content; an element holding two `ds:Signature` children with
  `exclude_signature`; an unknown option. Verifying a SAML signature is then
  ordinary code: the digest is `sha256` of the canonical element with
  `exclude_signature`, and `rsa_verify` checks the canonical `SignedInfo`
  (`{ id: X, signed_info: true }`)
  against `x509_parse(cert).public_key` (`tests/run_xml_sign.sh`, TIER signed).

Streaming reader, for documents too large to hold as a tree — it walks the file
forward without materializing the whole document:

- `xml.reader(path)` — open a streaming reader over a file.
- `reader.read()` (`xml.read`) — advance to and return the next element, or `unknown` at end.
- `reader.skip_to(name)` (`xml.skip_to`) — advance until an element named `name`.
- `reader.subtree()` (`xml.subtree`) — materialize the reader's current element as a node tree.
- `reader.close()` (`xml.close`) — release the reader.

Each event carries a `line` — the line its element **starts** on. An `end` event
therefore reports where the element *opened*, not where its closing tag sits, so
`end book` says 2 for a `<book>` on line 2 whose `</book>` is on line 4: it names
the element rather than the token.

Until 0.2.2 this field was libxml2's **parser read position** rather than the
node's line — on a small document every event reported the same number, because
the whole file is buffered before the first node is handed back, and on a large
one it ran several lines ahead. It was a plausible wrong number with no way for
a caller to tell.

Parse failures and misuse (bad argument types, a closed reader, a non-element
subtree) raise structured errors. See `docs/xml_design.md` and the `examples/xml_*`
programs for the full surface and worked SEC-filing examples.

## Qt Module

The `qt` module renders a `window` declaration with Qt 6 Widgets. It is
available when gBASIC is built with Qt 6 and `moc`; otherwise every verb but
one raises a clean runtime error (`error.source = "qt"`), the way the other
optional modules degrade. Every verb that takes a window first is also the
window's method: `app.open()`, `app.read("who", "text")`, `app.close()`.

```basic
window customer( title: "Customer", width: 480 )
    column( margin: 8, spacing: 8 )
        form to edit cust
            field name( label: "Name" )
        end form
        row
            stretch()
            button save( text: "Save", default: true )
        end row
    end column

    on save.clicked
        print "saving " + customer.read("name")
    end on
end window

program main()
    cust = { name: "Ada Lovelace" }
    if qt.can_display() then
        qt.open(customer)
        qt.run()
        cust = customer.subject("cust")
        print "the person typed " + cust.name
    else
        print "no display -- nothing to open"
    end if
end program
```

| | |
|---|---|
| `qt.can_display()` | whether a window can be opened at all. **Answers rather than raising**, including on a build with no Qt — a program asking this is degrading gracefully, and "this build has no Qt" is one of the answers |
| `qt.open(win)` | build and show the declaration. Refuses, naming **a display**, when there is none, and runs `qt.check` first so a window nobody validated cannot open |
| `win.open_modal()` (`qt.open_modal`) | open it **modally and block** until somebody answers, which is what `qt.confirm` is not: two buttons is the whole of what a message box offers, so anything with a layout of its own comes through here. Answers **true** when accepted and **false** for a reject, a close, or the window being dismissed. It is the **same window** — one shared open path, so the declaration, the binding and the handlers are the same code — and it **closes the window before returning**, so the `closed` handler is where the form is read |
| `win.accept()` (`qt.accept`) / `win.reject()` (`qt.reject`) | say yes or no to a modally shown window, which is what a button handler calls. Refused on a window that was not opened modally, naming the verb that would have made it one. Not `qt.answer`, which drives a *dialog* from the program when nobody is there |
| `win.close()` (`qt.close`) | close it: the window is **removed from gBASIC's registry**, and every verb that needs an open one then refuses by name. Not a hide — Qt leaves a dismissed window alive and still readable, so a program would otherwise go on reading values off a screen nobody is looking at. **Refuses** when the window is not open, rather than being a silent no-op; `qt.is_open` is how a program asks without raising. Reopening builds a **fresh** window, so a form captures its subject again |
| `win.is_open()` (`qt.is_open`) | whether it is still there. **Answers rather than raising**, like `qt.can_display`, because a window can close *without the program asking* — once the person can dismiss one, every other verb starts refusing for a reason that is nobody's mistake |
| `qt.check(win)` | the declaration's own load-time rules, over a window record — **answers on a build with no Qt and needs no display**, because it is a check on a declaration. Answers `true` or refuses with the first problem and a count of the rest |
| `win.read(name)` (`qt.read`) | the text a part is showing (`"true"`/`"false"`for a check box) |
| `win.read(name, property)` (`qt.read`) | one named Qt property of that part, **in the kind Qt declares it** — `readOnly` answers a boolean, `maxLength` a number. A property the widget does not carry is refused by name, as it is when set |
| `win.write(name, value)` (`qt.write`) | show a value, rendered exactly as `print` would render it |
| `win.selected(table)` (`qt.selected`) | which row a person has chosen, as an **index** so it composes with the list the program holds; `unknown` when none is — not `-1` and not `0`, both of which are valid-looking indices |
| `win.select(table, row)` (`qt.select`) | choose a row from the program, for a window that opens with one highlighted; `unknown` chooses nothing |
| `win.cell(table, row, column)` (`qt.cell`) | one cell, addressed by the **column's own name** rather than its position |
| `win.alignment(table, row, column)` (`qt.alignment`) | where that cell's text sits, `"left"`, `"right"` or `"center"`, **asked of Qt** (an asking table's model, or the cell item) rather than of the declaration, so an `align:` declared and never applied cannot pass. Addressed and refused exactly as `qt.cell` is |
| `win.heading(table, column)` (`qt.heading`) | a column's caption, by its name **or** its position |
| `win.options(part, list)` (`qt.options`) | fill a `choice`'s list from data, answering how many items it put in. Clearing **resets the selection**, deliberately: a list that has changed may not contain what was chosen, and an index into a list that has moved is how a form reports a value nobody picked |
| `win.rows(table)` (`qt.rows`) / `win.rows(table, n)` (`qt.rows`) | how many rows a **`to ask`** table has, or tell it. A verb rather than part of the declaration because it changes — a results grid is refilled by every query — and setting it is also how *"the data changed"* is said, since it resets the model and Qt re-asks. A negative count is refused rather than shown as empty |
| `win.refresh(name, items)` (`qt.refresh`) | **reconcile a container's children** against `items`, the same array `items:` holds in the record `qt.open` read, answering how many it reconciled. A child whose **name and word** are unchanged is KEPT — with its text, its caret, its scroll position and its undo stack — a new one is BUILT through the same door `qt.open` uses, an absent one is DELETED and its name leaves the window, and a `tab`'s caption is re-read. A `tabs`, `column`, `row` or `group`; a `split` cannot be named and a `cells` grid has declared positions, so both are refused by name. An **unnamed** child is always rebuilt, there being nothing to match it by |
| `win.depth(name, row)` (`qt.depth`) | how deep that row sits in a **tree** — 0 for a top-level row — asked of the WIDGET by walking the item's parents, never of the record. A part that is not a tree and a row that is not there are refused separately, being different questions |
| `win.children(name)` (`qt.children`) | the **names** of that container's children, **in order** — `""` for one nobody named, which is a different answer from absent. The order oracle for `qt.refresh`: `qt.at` reads coordinates in a `cells` grid only, so a box's order is observable through nothing else |
| `win.tabs(name)` (`qt.tabs`) | the **captions** of a `tabs`, in order, as a list. `QTabWidget::tabText(int)` takes an argument and `qt.invoke` passes none, so without this a caption cannot be read back at all |
| `win.refresh(what)` (`qt.refresh`) | show a subject **as it is now**, answering how many rows or parts it drew. `what` is a **table's name** or a **form's subject** — a form has no name, so the subject is the only thing that addresses one, and a name meaning both is refused rather than resolved. A table grows, shrinks and redraws changed cells; a form fills every part, across **all** the forms carrying that subject. The symmetric half of `qt.subject`, which reads the widgets at the moment asked |
| `win.set(part, property, value)` (`qt.set`) | set one named Qt property at run time — the write half of `win.read(part, property)` (`qt.read`). The only other way to set one is the declaration, which is written before anything has happened. One name is not a Qt property: **`whitespace`** on a `source` or a `memo` shows tabs, spaces and line ends (Qt's `ShowTabsAndSpaces` and `ShowLineAndParagraphSeparators`), off by default, and `win.read(part, "whitespace")` reads it back from the document *(after 0.10.0)* |
| `win.pane(part)` (`qt.pane`) | which draggable or scrolled region a part sits in: `{ kind: "split", direction, panes, index, sizes }` or `{ kind: "scroll", resizable }`, and **`unknown`** when it is in neither — a record saying `panes: 0` would read as a splitter holding nothing. Asked of the **layout**, walking up from the part, and it reports the **nearest** region. This is the only way to observe a `split`, which spends its one slot on the direction and so cannot be named |
| `win.resize_panes(part, sizes)` (`qt.resize_panes`) | set the pane sizes of the split a part is in — how *"restore my layout"* is written, and the only route to them: `setSizes` takes a list, so it is neither a property nor invokable, and a `split` cannot be named. A **count mismatch is refused, naming both numbers**, because one size on a two-pane split *collapses* the second and three honour none of them, both silently. What you set is a **ratio** in effect: Qt adds the handle and the margins, and re-proportions on resize, so the same numbers do not come back |
| `win.at(part)` (`qt.at`) | where a part sits in its `cells`: `{ row, column, across, down }`, asked of the layout |
| `win.menus()` (`qt.menus`) / `win.menus(n)` (`qt.menus`) / `win.menus("name")` (`qt.menus`) | the menu bar's top-level captions, the entries of the menu at position `n`, or the entries of the **`popup`** of that name. A number is a position on the bar and text is a popup's name — a popup is not on the bar, so it *has* no position. A separator's entry is `unknown` — it *has* no caption, where an item with `label: ""` has an empty one |
| `win.popup(name [, under])` (`qt.popup`) | raise a `popup`, answering **whether anything was shown**. With a third argument it pops **under that part** rather than at the pointer, which is what a menu *button* wants — its menu belongs under the button, not wherever the mouse happens to be. A name that is not there is refused, since popping at the pointer instead would look like a bug in the program rather than a name that was wrong. **The menu stays up until something dismisses it** — a person clicks an item or presses Escape, and a program driving itself calls `win.invoke(menu, "hide")` (`qt.invoke`), because a menu left up goes on grabbing the mouse. `false` when every item is hidden — a row that offers nothing rather than an error, since Qt declines to show such a menu and an empty context menu reads as a control that failed. The count **recurses**, so a lone empty submenu is not something to show |
| `win.right_click(part [, row])` (`qt.right_click`) | right-click a part the way a mouse does, for a program driving itself. It sends a **press, a release and then the context event**, so the selection moves as it would for a person — without which a handler would read a stale row. A third argument aims at a **table row, a tree row or a tab** by its own rectangle; for a tab it aims through the tab *bar*, which is where a real right-click lands. Routed through the widget's **viewport** where there is one, which is what a `table`, `memo` and `source` answer to and nothing else can reach |
| `win.click(part [, row])` (`qt.click`) | a **left** click, aimed exactly as `qt.right_click` aims: a press and a release where a mouse would land, so Qt itself emits `clicked`, `itemClicked`, `cellClicked` and the selection change. It calls no handler directly, which is what makes a single-click-opens path testable *(after 0.10.0)* |
| `win.label(part)` (`qt.label`) | the caption shown **beside** a part, read from Qt (the label whose buddy is the part), or **`unknown`** when it has none. A `field` and a `choice` carry their `label:` as a caption in a `form` row, a `cells` grid, a `column` or a `row`; in a `form` a `memo` and a `source` do too. A `button`, a `check` and a `text` show their `label:` as their own text, so they have no caption *(after 0.10.0)* |
| `qt.which_tab()` | which tab the event **being handled** is about — a number, or **`unknown`** both outside a handler and when the click was not over a tab, which is an ordinary case since a right-click on a tab widget's *page* emits the same signal. It exists because a tab is the one case Qt does not answer by itself: a right-press **moves** a table's or a tree's current row, so `qt.selected` already says which was clicked, and a right-press on a tab bar correctly does **not** move the current index — so `currentIndex` answers the tab that was already active, and a menu acting on it closes the wrong document. It also answers the index `tabCloseRequested` carries, which is what makes `tabsClosable: true` usable at all |
| `win.scroll(part, notches [, modifier])` (`qt.scroll`) | turn the wheel on a part, for a program driving itself, answering **whether anything claimed the event**. `notches` may be **fractional** — a trackpad sends less than one, and that is the case the direction rounding exists for. The modifier is `"ctrl"`, `"shift"` or `"alt"`, refused by name, because one silently ignored sends a plain wheel while the author believes they sent a modified one |
| `win.key(part, keys)` (`qt.key`) | press one key combination on a part, for a program driving itself: a real key press and release, sent with the part focused and its window active, so **Qt's shortcut map sees it** and a bound item fires, while an unmodified key types into the widget. Answers **whether anything took the key**. One combination per call; a chord is refused. **The limit:** this is the key Qt *delivers*, not the physical keys, so it cannot reproduce a keyboard layout. On a real US keyboard Ctrl+Shift+= reaches a `Ctrl++` shortcut through the platform's key mapper; here that route is pressed as `"Ctrl++"` |
| `win.shortcuts(part)` (`qt.shortcuts`) | **every** shortcut a menu or toolbar item answers to, as a list of texts (a button's single one, too). `qt.read(..., "shortcut")` reports only the first, so this is how both of `["Ctrl++", "Ctrl+="]` are checked |
| `qt.wheel()` | the wheel event **being handled** — `{ dy, dx, dy_exact, dx_exact, ctrl, shift, alt }`, with `dy` in **notches** rather than Qt's 120-unit delta and **positive meaning away from you** (Qt's sign, and the opposite of GTK's). `dy` is whole notches, and a sub-notch turn is rounded **away from zero** so its sign survives; `dy_exact` is the same turn **unrounded** (a quarter-notch is `0.25`), for a program that accumulates a trackpad's small deltas rather than counting each as a notch. Refuses outside a wheel handler, since there is no sensible record to hand back and a program asking in the wrong place would otherwise read zeros and act on them |
| `qt.swallow()` | claim the event being handled, so the view does not also act on it. Not optional for a zoom: Ctrl+wheel scrolls a `source` *faster* than a plain wheel, so a handler that zooms without claiming leaves the editor jumping about while it resizes. Refuses when nothing is in flight rather than being a silent no-op |
| `win.capture(file)`, `win.capture(file, { part, scale })` (`qt.capture`) | writes the window, or one named `part:` of it, to a PNG file, drawn at `scale:` device pixels per pixel (above 0, at most 8; default 1). It works offscreen, and answers `{ file, width, height }` in pixels written. The file is a path string or a `{file}`, written as `write` writes. A focused editor's caret is drawn steady and pending layout is applied first, so the same window gives the same file. An unknown part is refused with the window's parts listed; so are a menu item and a part that is not showing. Byte-identical across machines is not promised (fonts differ) |
| `win.has_icon(part)` (`qt.has_icon`) | whether a part has an icon. It exists because a QIcon has **no text form** — measured, a button carrying a real image and one carrying nothing both read back as `""` — so `win.read(part, "icon")` (`qt.read`) **refuses** and names this instead, an empty answer for a property that *is* set being a wrong answer rather than no answer. A part that cannot carry one (a `text`, a `table`) is refused, naming its kind |
| `win.scrolled(part)` (`qt.scrolled`) | how far a part is scrolled, or **`unknown`** for one that does not scroll — a different claim from *scrolled to nowhere*, since a `button` has no position where a `source` at the top has position 0. A `QAbstractScrollArea` reaches its scrollbar by a method, so this is the only route to it |
| `win.scroll_to(part, position)` (`qt.scroll_to`) | put it back, which is how *"reopen the editor where I left it"* is written. A part that does not scroll is refused by name rather than succeeding and doing nothing; the range is Qt's own, so a position past the end is clamped |
| `qt.can_highlight()` | whether this build can highlight a `source` at all. **Answers rather than raising**, like `qt.can_display`, because a program asking is choosing what to offer — and a build without KSyntaxHighlighting still edits text perfectly well |
| `win.highlighting(part)` (`qt.highlighting`) | what is **actually** highlighting a `source`: `{ language, file }`, or `unknown` when nothing is. It reports the **file** as well as the language because a definition the user installed in their own home *shadows* the one gBASIC ships and reports the identical name — which of the two wins is the machine's business, but being unable to see it would not be. A missing definition is warning **2115**, never a raise: the editor still works, and an absent definition still colours every line with the theme's ordinary text, so there is nothing on screen to notice |
| `win.caret(part)` (`qt.caret`) | where the caret is in a `source` or a `memo` — `{ line, column, selecting }`, **1-based** — or **`unknown`** for a part that is not a text editor. The numbering is the one gBASIC's diagnostics, `source_outline` and `qt.mark` all use, so a `line:column` from any of them goes straight into `qt.caret_to` with no arithmetic. `selecting` is there because a caret inside a selection is one end of a range, and a bare line and column would be an ambiguous reading |
| `win.caret_to(part, line, column)` (`qt.caret_to`) | put it there, which is how *"stopped at line 7 — show me why"* is written. A **line past the end is refused**, naming the document's real length: a program placing the caret where a diagnostic said is working from a model of the document, and clamping would put it on the wrong line and look like it worked. A **column** past the end of its own line clamps, landing where End would — that case is ordinary rather than a stale model |
| `win.select_text(part, line, column, line2, column2)` (`qt.select_text`) | select a range in a `source` or a `memo`, which is how find shows the current match; the caret ends at the second position. Positions are **caret positions**, 1-based like `qt.caret`, so `1, 1` to `1, 4` covers three characters, and **columns count codepoints**, as `len` and `regex` do, so a match found in gBASIC goes straight in. A line past the end is refused with the real length, a column past its line clamps, and a range that ends before it starts is refused *(after 0.10.0)* |
| `win.selection(part)` (`qt.selection`) | the selection as Qt holds it, `{ line, column, line2, column2, text }`, or **`unknown`** when nothing is selected; lines in `text` are separated by a line feed, as `qt.read` answers *(after 0.10.0)* |
| `win.replace_text(part, edits)` (`qt.replace_text`) | replace ranges as **one undoable edit**: `edits` is one record `{ line, column, line2, column2, text }` or an array of them, and a single undo reverts them all, which is what makes Replace All undoable where `qt.write` clears the undo stack. Every position refers to the text **before** the call (they are applied last to first), an empty range inserts, and overlapping edits or one bad position refuse the whole call before anything changes *(after 0.10.0)* |
| `win.highlight_text(part, ranges)` (`qt.highlight_text`) | mark every match: the ranges `{ line, column, line2, column2 }` become Qt's extra selections, **replacing the whole list** each call as `qt.mark` replaces a category; `[]` clears it. Neither the text nor the selection changes *(after 0.10.0)* |
| `win.highlights(part)` (`qt.highlights`) | the marked ranges, read back from Qt's extra selections *(after 0.10.0)* |
| `win.mark(part, category, lines)` (`qt.mark`) | put a mark beside each of those lines in a `source`'s margin, **replacing that category's whole list** — which is the consumer's own shape, since a gutter is recomputed on every redraw and an add/remove pair would make the caller keep a record of what it had already drawn. An empty list clears it. The categories are `section`, `error`, `warning`, `note` and `break`, and an unknown one is **refused by name** with a near-miss hint: unlike a widget's properties the set *can* be closed, because a category decides a glyph and a priority and the backend has to know both. **Line 1 is the first line** — `source_outline` reports `start_line = 1` and a parse diagnostic carries a 1-based line, so there is no conversion anywhere — and **line 0 is refused** rather than dropped, a silently dropped mark reporting success while drawing nothing |
| `win.marks(part, category)` (`qt.marks`) | that category's lines, as a list. **What was set, never what is currently drawable**: a program may mark line 40 and then shorten the document, and a read filtered to the lines still in the file would disagree with the write for a reason the caller cannot see. The drawing is the widget's business; the list is the program's |
| `win.gutter(part)` (`qt.gutter`) | what that `source`'s margin is — `{ line_numbers, marks, width }` — or **`unknown`** for one that has none, since `width: 0` would say a margin exists and is empty. The `width` follows the **digit count**, so a thousand-line document reserves more than a nine-line one |
| `win.gutter_press(part, line)` (`qt.gutter_press`) | press the margin beside a line, which is what makes a `gutter` handler drivable without a person — `qt.right_click`'s and `qt.scroll`'s shape. A line that is **not on screen** is refused, because a mouse cannot land there |
| `qt.gutter_click()` | which line the margin click being handled was on: `{ line }`, or **`unknown`** outside a handler — a different claim from *line 0*. Takes no arguments, because a handler here takes none and the event is asked for instead, the rule `qt.wheel()` and `principal()` already follow |
| `win.dock(name)` (`qt.dock`) | what that dock pane is doing — `{ area, floating, visible, tabbed_with }` — or **`unknown`** for a name that is not a pane. The `area` is asked of the **window**, not of anything remembered when it was built, because a remembered position agrees with a placement that was wrong; `tabbed_with` counts the panes sharing its tab group, which only the window can answer |
| `win.layout()` (`qt.layout`) | the window's whole dock and toolbar arrangement as **text** to keep — which area each pane is in, their order, which tab is raised, what is floating and how big. **Base64**, because Qt's own blob is not valid UTF-8 and the obvious thing to do with it is store it, through `encode` or `persist`, both of which write JSON. It is Qt's format, not ours: keep it, do not read it — `qt.dock` is what answers questions |
| `win.restore_layout(text)` (`qt.restore_layout`) | put the panes back, answering whether it took. **Reported, never raised**: an arrangement saved before the window's declaration changed is an ordinary thing to find on startup, and losing it is not worth stopping a program for — the panes stay where the declaration put them |
| `qt.copy(text)` | put text on the clipboard, answering true. **No window argument**: `QGuiApplication::clipboard()` is a class static and there is one clipboard per application, so taking one would imply otherwise. It takes text or a **`{file}`/`{dir}` reference**, copying its path, which is the use it was built for. It copies **eagerly** — measured, because the GTK route this replaces reads its buffer *at paste time*, so a buffer going out of scope there gives a successful-looking copy and an empty paste. A **NUL is carried**, not truncated: the clipboard takes a counted string. A display is still needed, since the clipboard belongs to a QApplication, and the refusal says so rather than naming a window |
| `qt.clipboard()` | what is on it, or **`unknown`** when there is no text there — a different claim from `""`, which is what somebody copying an empty string put there. Qt distinguishes the two, so neither answer is a guess |
| `win.inform(text)` (`qt.inform`) | a message box with one button, parented to `win` so Qt centres it there and makes it modal to that window |
| `win.confirm(question)` (`qt.confirm`) | a message box with two, answering **`true` or `false`** rather than a button code — two buttons is the whole of what it offers, and anything richer is a window shown modally. **No is the default**, because a question answered by pressing Return without reading should not be the destructive one |
| `qt.answer(value)` | answer the dialog that is open, from the program rather than a person — a timeout, an unattended run, a suite. `true`/`false` for a message box; **a path** for a file chooser, and `false` there is a *cancel*. `true` on a chooser is **refused**, because it would have to mean "accept *what?*", and a path on a message box likewise — each refusal names the dialog that is actually open. **Refuses when nothing is open**, rather than being a silent no-op that would leave a program waiting for somebody who is not there |
| `win.choose_file([opts])` (`qt.choose_file`) | *which existing file?* Answers the path as **text**, or **`unknown`** if they cancelled |
| `win.choose_save([opts])` (`qt.choose_save`) | *what shall I call it?* — a name to write to, which need not exist yet |
| `win.choose_folder([opts])` (`qt.choose_folder`) | *which folder?* |
| `qt.dialog()` | the innermost open dialog as a record — `kind` (`"message"` or `"chooser"`), `title`, and `text` or `folder`/`filter` — or **`unknown`** when none is open, which is a different claim from a dialog carrying no fields |
| `qt.theme(css)` / `qt.theme()` | set or read **one stylesheet for the whole application**. Needs no display, and is remembered until there is a `QApplication` to put it on, so a program themes first and opens second |
| `win.subject(name)` (`qt.subject`) | the record a form is working on. For `form to edit` the widgets are read **at the moment you ask** and folded into the record the form captured when the window opened; for `form to show` the captured record comes back untouched |
| `win.invoke(name, method)` (`qt.invoke`) | call a widget method by name — `click`, `clear`, `setFocus`, or `close`/`showMaximized` on the **window itself** |
| `qt.run()` | hand the thread to Qt's event loop. gBASIC's own sources (a webserver, transfers, timers, the actor inbox) keep being serviced |
| `qt.stop()` | end that loop; ordinarily called from a handler. It means *give me the thread back* and **nothing more** — the windows stay open and readable, and no `closed` handler fires. (Qt's own `quit()` closes every top-level window; this does not use it.) |

**A window is addressed by its declaration**, not by a handle: `qt.open(customer)`
and `customer.read("name")` both take the record the `window` block bound,
the same way `web.routes(app)` takes a server record.

**And the window is a part of itself.** Its own name reaches it in the same
by-name table every part lives in, so `win.read("customer", "windowTitle")`,
`qt.set`, `win.invoke("customer", "close")` and a handler on the window all
work with no extra vocabulary. One consequence: **a part may not take the
window's name**, refused by name at load time, because the part would otherwise
register over the window and `on customer.closed` would reach a widget.

**`on <window>.closed` fires when the window closes** — by `qt.close`, by an
invoked `close`, or because the person pressed the button — and it fires
**before** the window stops being open, which is what makes it the one place to
read a form somebody has just dismissed:

<!--fragment: a handler block only exists inside a `window`, so it cannot parse alone-->
```basic
    on customer.closed
        saved = customer.subject("cust")     ' still readable, just
    end on
```

Afterwards `qt.read`, `qt.write` and `qt.subject` all refuse by name, and
`qt.is_open` answers `false`. That is deliberate: Qt leaves a closed window
alive and still holding its values, so answering would mean reading off a screen
nobody is looking at. `closed` is the window's own event and not a widget
signal, so `on save.closed` on a button is refused.

**A window shown modally is the same window**, and `on <window>.closed` is
where its form is read too — `qt.open_modal` closes the window before it
returns, so the four ways it can end leave one state rather than two:

| | `qt.open_modal` answers | `closed` fires |
|---|---|---|
| `win.accept()` (`qt.accept`) | **true** | yes |
| `win.reject()` (`qt.reject`) | false | yes |
| `win.close()` (`qt.close`) | false | yes |
| the window manager's button | false | yes |

**It does not starve the rest of the program.** A modal window opened from
`main` — *"show this form and wait"* — leaves gBASIC's own sources running: a
webserver answers, a `timer` fires, so a program can even answer its own
window. The limitation is the other case, and it is the one `qt.confirm` already
has: a modal window or dialog raised from **inside a handler** suspends every
source until it is answered, because what cannot happen is re-entering the
interpreter, not re-entering Qt's loop.

**And a program may build that record itself**, which is how you get a row per
invoice: a declaration cannot contain a loop, and `qt.open` asks only that the
record carry `kind: "window"`. So the block is one door and ordinary values are
the other, exactly as `server` and `web.routes` are two doors on one thing.

```basic
rows = []
for each line in invoice.lines
    rows = append(rows, { kind: "part", word: "field", name: line.id,
                          options: { label: line.description,
                                     text: string(line.amount) } })
end for
win = { kind: "window", name: "invoice",
        options: { title: "Invoice", width: 420 },
        items: [ { kind: "container", word: "column", options: { margin: 8 },
                   items: rows } ] }
qt.open(win)
```

A built window is the shape the declaration binds, so `print customer` shows
what to build: `kind` is `"window"`, `"container"`, `"part"` or `"handler"`;
a container carries `word`, `items` and optionally `name`, `label`, `subject`
and `direction`; a part carries `word`, `options` and usually `name`; a handler
carries `target`, `signal` and `handler` (a function value).

**It meets the same rules a declaration meets**, through `qt.check` — the same
closed vocabulary with its near-miss hint, the same placement rules, names
unique within the window, and a handler whose target really exists. The field
names are closed too, so `nmae:` is refused by name rather than leaving a part
silently unlabelled. A problem is reported with a **path**
(`column[0] > field a`) because a record has no line. Containers may not nest
more than 64 deep, which only a loop can get wrong.

**A form binds by name.** `form to edit cust` names its subject, and a field
named `email` takes `cust.email` — so there is nothing to look up and nothing to
wire. The subject is read from the frame that calls `qt.open`, so `cust` must be
in scope there; a form naming a subject that is not is refused rather than
opening quietly and showing nothing. A field the record does not have is left
empty rather than refused, which is what a form filling in a *new* record needs
— the fold adds it. The cost is that a **misspelled** part name is not caught:
the field the author meant keeps its captured value, so typing into the
misspelled part changes nothing.

`to show` and `to edit` differ in two ways. A `show` form's parts take no
typing — a field is read-only, a check box disabled — because the alternative is
a silent discard: someone types, nothing complains, and nothing ever reports it.
And only an `edit` form's record follows its widgets through `qt.subject`.

**A `tree` shows a list that has a SHAPE.** `tree files to show rows` is a
table's sibling: the same production, the same `field` columns, the same
`qt.cell`/`qt.heading`/`qt.select`/`qt.selected`, and one column more —

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
tree files to show rows
    field name(  label: "File" )
    field depth( depth: true )
end tree
```

**A column's options are a closed set:** `label` (the caption), `type` (how an
edited value folds back), `depth` (a tree's undrawn depth column) and **`align`**,
which is `"left"`, `"right"` or `"center"` and applies to every cell of that
column, in a held table, an asking table and a tree alike:
`field amount( label: "Amount", align: "right" )`. Anything else is refused at
load by name, with the near miss: a column's options are read by the backend
rather than handed to a widget, so before 2026-10-22 a misspelled one did nothing
at all. `win.alignment(table, row, column)` answers where a cell's text sits,
asked of Qt itself.

The column marked `depth: true` is **declared and not drawn**: it supplies the
nesting, so a flat list of records becomes a tree, each row attached under the
last row one level shallower. At most one column may carry it, and a tree with
none is a flat list — which is what a program that owns its own expand/collapse
state hands over.

**Not drawn means it takes no column**, so it may be declared first, last or in
the middle and the drawn columns keep their order either way; asking `qt.cell`
or `qt.heading` for it is refused by name, saying it carries the depth rather
than that there is no such column. (That is worth stating because it was wrong
until 2026-10-21: the name-to-column mapping counted the undrawn column, so a
tree declaring `depth` **first** read back as empty, and the message blamed the
row.)

A tree is **`to show`** and nothing else: its rows are read, and a parent row
has no value to write back. **An index is into the rows you gave it**, never
into what is on screen — Qt's own iterator counts a collapsed child, so there
is no "visible index" to be had — which makes `qt.selected` answer a number
that stays meaningful however the tree is expanded. `win.depth(name, row)`
answers how deep a row sits, asked of the widget rather than of the record, and
`win.refresh(name)` re-reads the subject when a directory expands.

Everything about how it **looks** is an ordinary property: `headerHidden`,
`rootIsDecorated`, `itemsExpandable`, `indentation`, `animated`,
`alternatingRowColors`, `uniformRowHeights`, `allColumnsShowFocus`,
`expandsOnDoubleClick` and `sortingEnabled` all read and write through
`qt.read`/`qt.set`, and a tree has no grid lines to turn off.

**A table shows a list.** `table orders to show lines` takes an array of
records; each `field` is a *column* read from every element, captioned by its
`label:`. A table is **named** where a form is not, because a form's parts are
addressed by their own names while a table shows many rows — so the table itself
is what a handler and `qt.selected` address. Its parts are therefore all
`field`, and anything else is refused.

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
table orders to show lines
    field placed( label: "Date" )
    field total(  label: "Total", type: {USD} )
end table
```

`to show` makes the cells uneditable while leaving the table **selectable** —
choosing a row is reading, not editing. An element that is not a record is
refused and the index named, the same way a projection reports it.

**The fold keeps each field's kind.** A widget holds text, so folding back
blindly would turn `{ id: 42 }` into `{ id: "42" }`. The kind comes from
`type:` where the field declares one, from the captured field otherwise, and
from a check box being a check box.

**A menu bar belongs to the window.** `menu "File"` sits at the window's top
level or inside another menu, and nowhere else — it is not laid out, so a `menu`
inside a `row` is refused rather than rendered as a bar in the middle of the
screen. Its parts are `item`, `separator` and nested `menu`:

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
menu "File"
    item open_file( label: "Open...", shortcut: "Ctrl+O" )
    menu "Recent"
        item recent_one( label: "ledger.bas" )
    end menu
    separator()
    item quit_app( label: "Quit", shortcut: "Ctrl+Q" )
end menu
```

A menu's **caption** is what a person reads and its **items** are what a handler
addresses — so a menu takes no name, and `on open_file.triggered` names the
item. An item is a `QAction`, so `shortcut:`, `enabled:`, `checkable:` and
`checked:` reach it through the same by-name property route every widget option
uses.

**An item may answer to several shortcuts.** `shortcut:` takes one text or a
**list of alternatives**: `item bigger( label: "Bigger", shortcut: ["Ctrl++",
"Ctrl+="] )`. Both are needed for zoom-in on a US layout, where plain Ctrl+=
delivers `=` and matches no `Ctrl++`. A comma *inside* one text is something
else, a chord pressed in turn (`"Ctrl+K, Ctrl+C"`). A misspelled key is
refused by name rather than bound to nothing, a list of several is refused on
a widget that takes one shortcut (a `button`), and `qt.set(win, part,
"shortcut", [...])` takes the same list. `qt.read(win, part, "shortcut")`
reports only the first; `qt.shortcuts` reports them all.

**A table can ask for its cells instead of being shown them.** `table x to show
<list>` holds every row; `table x to ask <function>` keeps none and asks for
each cell as it becomes visible, which is how a large result set is shown:

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
table results to ask cell_of
    field who(   label: "Customer" )
    field total( label: "Total" )
end table
```

The subject is a **function name**, not a list. It is called with a row and a
column (both counted from 0) and whatever it returns is rendered by the same
renderer `print` uses, so a number, a date or money arrives in the words the
language itself uses. Tell the table how many rows there are with `qt.rows`,
and tell it again when the data changes.

**The cost is the viewport, not the table.** Measured: a `QTableView` asks for
about 27 cells whether the table has a hundred rows or a million, and scrolling
to the end asks for a few dozen more rather than walking everything in between.

`qt.cell`, `qt.heading`, `qt.select` and `qt.selected` all work on either kind,
so a program need not know which it declared. A form may **not** `ask` — it
shows or edits one record whose fields it can read, so there is nothing for it
to ask for. Click-to-sort is off for the same reason it is off for a held
table, only more so: a sorted view would make `qt.selected`'s index mean a
different row than the data has, and the data is somewhere Qt never sees.

**A `separator` divides a menu *or* a layout.** One word, one meaning — a line
that divides — over two widgets, and **its orientation comes from the parent**:
a `column` is divided by a horizontal rule and a `row` by a vertical one, so
there is no option to get wrong.

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
column
    text heading( label: "Credit" )
    separator rule()
    row
        button ok( label: "OK" )
        separator()
        button cancel( label: "Cancel" )
    end row
end column
```

A rule in a layout **may be named**, where a menu's separator may not: it is a
real widget, so a name is how it is themed (`qt.set(win, "rule", "styleSheet",
...)`) and how its orientation is read back (`qt.read(win, "rule",
"frameShape")` answers `HLine` or `VLine`). It is refused where there is
nothing to divide — a splitter pane already has a draggable handle between its
panes, and a `cells` grid has no direction to take.

**`icon:` is the one option that is not an ordinary property.** Qt will not turn
a string into an icon, so this is the single name with a conversion behind it —
and it is the same conversion in the declaration and in `qt.set`, so the two
cannot disagree. There are three sources and they are told apart by facts:

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
item act_open( label: "Open", icon: "SP_DialogOpenButton" )
button brand( label: "Go", icon: "icons/go.png" )
button themed( label: "Save", icon: "document-save" )
```

A name beginning **`SP_`** is one of Qt's own 81 standard icons, which work
with no desktop icon theme installed — the only source that always does. A
string that **names an existing file** is that file. Anything else is a
**freedesktop theme name**.

The two kinds of failure are answered differently, because they are different
in kind. A path that is not there, a file that is not an image, and a
misspelled `SP_` name are all **refused** — each is a fact that can be
checked, and the intent is unambiguous. A theme name this machine's theme does
not have is warning **2116**, never a raise: that is a fact about the *machine*,
and refusing would stop a program running on a server with no icon theme at
all. `win.has_icon(part)` is how a program asks whether one arrived, and an
empty source clears it.

**A `popup` is a context menu**, and it spends its one slot the other way
round. A container carries a name *or* options and never both, so a `menu`
spends it on the caption a person reads on the bar — and a context menu has no
bar to be read on, so what it needs is a **name**:

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
popup row_actions
    item open_it( label: "Open" )
    item rename_it( label: "Rename..." )
    separator()
    item delete_it( label: "Delete" )
end popup
```

Both halves are refused, so neither word can be written the other's way: a
`popup` with a caption and a `menu` with a name each get a refusal naming the
remedy. A `popup` holds the same three things a `menu` does, nested `menu`
included, and it belongs to the **window** — a `QMenu` is placed by nothing, so
one written inside a `row` is a declaration the layout would silently ignore.

**Declaring the handler is what asks for the custom menu.** Every Qt widget
starts at `DefaultContextMenu`, and under that policy
`customContextMenuRequested` is never emitted — so writing

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
on files.customContextMenuRequested
    app.set("delete_it", "visible", true)
    shown = app.popup("row_actions")
end on
```

is enough: the policy is set because the handler exists. The cost is that this
*replaces* the menu Qt would have put up itself, so a `memo` or `field` with a
context handler loses its own cut/copy/paste — which is why a part with no
handler keeps Qt's.

**A wheel is an event, not a signal.** A Qt widget has no wheel signal — it
*receives* one — so `on <part>.wheel` is handled the way `on <window>.closed`
is, and the handler asks `qt.wheel()` for the event rather than taking
arguments:

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
on code.wheel
    ev = qt.wheel()
    if ev.ctrl then
        f = split(app.read("code", "font"), ",")
        app.set("code", "font", f[0] + "," + string(number(f[1]) + 2))
        qt.swallow()
    end if
end on
```

That is a working Ctrl+wheel zoom, and it needs **no zoom verb**: `font` is an
ordinary property that reads back as text, so the size is a number in a list.
`qt.swallow()` is what stops the editor also scrolling — without it a
Ctrl+wheel scrolls *faster* than a plain one and the view jumps about while the
text resizes. A handler that declines leaves the wheel alone, which is what
keeps ordinary scrolling working, and the filter is installed only on the parts
that declared a handler.

**The row that was right-clicked is `qt.selected`.** Qt moves the current row
on a right-press, so a handler asks the question it already asks for a left
click, and the signal's position is never needed. What a row offers is decided
by **hiding** what it does not: `visible` is an ordinary `QAction` property, so
one menu is built once and `qt.set` narrows it per row — and `qt.popup` answers
`false` when nothing in it is visible, because an empty context menu reads as a
control that failed and a row that offers nothing is an ordinary outcome. That
count **recurses**: a submenu counts for what it leads to, not for itself, so a
menu whose every item is hidden behind one empty submenu still answers `false`
— which Qt's own `isEmpty()` does not, it being happy to show an arrow leading
nowhere.

**`choice` is one of a list**, and its list comes through one of two doors. A
**declared** `options:` is for a fixed set — a direction, a status, a currency —
and the record the declaration binds carries it by value, so it is there before
the window opens. A set that comes from **data** is filled by `qt.options`
after it opens:

<!--fragment: the inside of a program, not a program on its own-->
```basic
shown = editor.options("leads_to", ["(nowhere)", "The Cellar", "The Garden"])
editor.write("leads_to", "The Garden")
```

`win.read(part)` answers the **chosen text**; `currentIndex` through the
by-name route answers its position. `qt.write` chooses an item by its text and
**refuses one that is not in the list** — Qt's own `setCurrentText` silently
does nothing on an uneditable box, so a program setting an absent value would
read back the old one with nothing said.

A `choice` and a `memo` are both **bound** by a form, the rule being *it holds a
value a person can change*: a description and a chosen value are data. A `text`
is not bound — it carries no such value, so folding it would be meaningless. A
form wanting wrapped prose from its subject uses a `memo` with `readOnly: true`.
Under `to show` a memo becomes read-only and a choice is **disabled**, a
`QComboBox` having no `readOnly`.

There is deliberately **no `list` word**: a one-column `table` is a picker,
`qt.selected` addresses it, and `qt.refresh` keeps it current.

**Nothing follows its subject on its own.** `table orders to show lines` reads
`lines` when the window opens and `form to edit cust` reads `cust` then;
changing either afterwards changes nothing on screen, which is right for a
declaration and useless for data that moves. `qt.refresh` re-reads the subject
from the frame that calls it and redraws:

<!--fragment: the inside of a program, not a program on its own-->
```basic
lines = append(lines, { placed: "2026-03-01", who: "Alan Turing" })
shown = orders.refresh("lines_view")      ' a table, by its NAME

cust = people[chosen]
drew = orders.refresh("cust")             ' a form, by its SUBJECT
```

A **form has no name** (§0.6 refuses one both ways), so its subject is the only
thing that addresses it — and a window where one word is both a table's name and
a form's subject is **refused**, naming both, rather than resolved by a
precedence nobody wrote down. A subject spread across **several** forms is
visited in all of them, because `qt.subject` already folds across all of them
and the two directions must agree about what a subject reaches.

A column's **caption** is not refreshed, and cannot be: it is set when the
widget is built and only the declaration names it. Only the rows follow.

**`text` is a sentence and `memo` is many.** A `field` is one line of entry, so
until these existed a heading, an instruction or a description had nowhere to
go — the leaf vocabulary reached a line edit, a button and a check box and no
further. A `text` **wraps by default**, because a label that does not wrap
silently drops the thing it exists to show; `wordWrap: false` asks for one line.
A `memo` is editable, scrolls, and holds plain text rather than markup, so what
goes in is what `qt.read` gives back.

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
column( margin: 8, spacing: 6 )
    text heading( label: "The Cellar" )
    text blurb( label: "A dank room. Water drips somewhere behind you." )
    memo log( label: "You are in the cellar." )
    field command( label: "Command" )
end column
```

Both are ordinary named parts: `qt.write` reaches them, `qt.set` reaches their
properties, and they place in a `cells` grid like anything else. A `text`
carries no `readOnly`, being no kind of entry, so asking for one is refused by
name — which is how a program tells the two apart, since Qt strips newlines only
from **typed** input and a `field` set programmatically stores them just as a
`memo` does.

**`source` is a code editor.** A `memo` holds prose; code is different in four
ways, none of which is a Qt property and so none of which a `memo` could be
talked into: it **does not wrap** (a wrapped line makes a line number stop
matching the file — the opposite default from `text`, where wrapping is what
stops a paragraph truncating), its **tab is four columns** rather than eight,
its **font is the system's monospaced one** (asked for, never named — a
hardcoded family is how a port acquires a fallback nobody chose), and it
carries a **highlighter**.

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
column( margin: 8 )
    source code( language: "gBASIC", theme: "dark" )
end column
```

`language:` names a **definition**, never a file extension, and that is
load-bearing rather than fussy: `*.bas` is claimed by FreeBASIC at the same
priority as gBASIC, so a lookup by extension picks FreeBASIC — with no error,
and plausibly, FreeBASIC being a near neighbour. `theme:` is `"light"` or
`"dark"`, and is **declared** rather than inferred because `qt.theme` sets a
stylesheet and a stylesheet does not move Qt's palette, so a dark application
with an inferred theme would get light highlighting on a dark background.
A `theme:` with **no `language:`** gives a plain editor the theme's background
and text colours and no highlighting, for a file with no syntax (a `.txt`, a
`LICENSE`), so it matches a themed window rather than staying light. On a build
without KSyntaxHighlighting, where themes come from, it warns (2115) and keeps
its own colours. `win.read(part, "palette")` answers the editor's colours as
`"#rrggbb on #rrggbb"`, the text then the background, since a palette has no
text form of its own.

A `source` **binds in a form** like a `field` or a `memo`, which is what makes
it useful: the text of whichever file is open is a record field like any other.

**Where the definition comes from.** KSyntaxHighlighting searches a `syntax`
directory holding the XML. gBASIC looks in `GBASIC_SYNTAX_PATH` first and then
in the path compiled in at build time — the same two steps `GBASIC_PATH` and
the installed stdlib already follow — so from the source tree:

```sh
GBASIC_SYNTAX_PATH=editors/kate ./gbasic examples/qt/editor.bas
```

After `make install` that is unnecessary. If no definition is found the editor
**still opens and still edits**, and warning **2115** says so, because an
absent definition colours every line with the theme's ordinary text and there
is nothing on screen to notice. `qt.highlighting` is how a program asks what it
actually got.

**The margin, and a mark per line.** A `source` can carry a margin, and it is
not a ruler:

<!--fragment: one line of a `window` declaration-->
```basic
source code( language: "gBASIC", line_numbers: true, marks: true )
```

```basic
win.mark("code", "section", [1, 12, 31])   ' ▶ where each section starts
win.mark("code", "error", [18])            ' where the parse failed
win.mark("code", "break", [12])            ' a breakpoint somebody set
win.mark("code", "error", [])              ' clears that category
```

The two options are independent — numbers alone, marks alone, both, or neither
— and both are **refused on anything but a `source`**, because a `memo` has no
margin and an accepted option that did nothing would be invisible. Each wants a
literal `true` or `false` rather than being read for truthiness, since every
non-empty string in gBASIC is true and `line_numbers: "no"` would turn them on.

**The five categories are derived, not chosen**, which is what lets the set be
closed: `section` and `break` were asked for, and `error`, `warning` and `note`
are exactly gBASIC's own diagnostic severities, so a program showing
diagnostics has one category per severity rather than flattening three into
one. **A line carries one glyph**, and which one is decided by two rules — a
mark the *person* placed outranks one the program *computed*, so `break` wins
everything (a breakpoint is the one mark somebody set deliberately and has to
see in order to remove it); among computed marks the more urgent wins, error
over warning over note, with `section` lowest but above `note` so the run
affordance is not hidden by the least urgent thing there is.

**The glyph is not configurable, and that is a measurement.** A theme icon name
resolves to nothing on a machine with no icon theme, which is why a consumer
ends up shipping its own image for an error marker. Four of the five come from
Qt's own standard pixmaps, which are part of Qt Widgets and need no theme, and
`break` is painted, needing neither a theme nor a font — so there is nothing
here that can fail to appear.

**Clicking the margin** is how a breakpoint is set, and it is an event rather
than a signal (the margin is a plain child widget and emits nothing):

<!--fragment: a handler block, which lives inside a `window` declaration-->
```basic
on code.gutter
    c = qt.gutter_click()
    b = win.marks("code", "break")
    if contains(b, c.line) then
        win.mark("code", "break", b excluding [c.line])
    else
        win.mark("code", "break", concat(b, [c.line]))
    end if
end on
```

The filter sits on the margin itself, so an ordinary press in the text is still
an ordinary caret move, and it is installed **only where a handler was
declared**. A `gutter` handler on a `source` with no margin is refused at load
time, naming the option that would give it one.

**Not there yet:** completion (the data exists in `source_outline` and
`gbasic-lsp`; the popup is a `QCompleter`, another object attached to a widget,
so it lands where the highlighter did).

**A modal dialog is modal to the whole program, not just the window.** Qt's own
loop survives the nested one a dialog opens — that is why the backend hands Qt
the thread at all — but gBASIC is not serviced while a **watcher body** is on
the stack:

| dialog raised | gBASIC's own sources |
|---|---|
| from `main`, outside any watcher | keep running — a `timer` fires, and can answer the box |
| **inside a watcher body** | **suspended** until it is answered |

The second is the normal case and it is wider than it looks: a `timer` body, a
webserver handler, an actor mailbox watcher, an `http` event watcher and a
button clicked from any of them all run inside one. So a `timer`, a webserver
and an actor mailbox all stop until the question is answered — defensible for a
desktop application and surprising for one that is also a server. Measured both
ways through the interpreter, and `tests/run_qt_dialog.sh` asserts it with a
real `server` on an OS-assigned port, because a limitation nobody can observe
is one nobody will believe. A dialog with no display refuses **naming the
display**, not the shut window.

**Two guards hold that, each sufficient alone** — Qt will not re-deliver the
one timer gBASIC's sources ride on while its handler is on the stack, and
`watcher_drain` declines to re-enter while a watcher body is running. Removing
either changes nothing; with both off it dissolves. **So the remedy is not a
timer change**: it needs watcher dispatch to become re-entrant, and that guard
is deliberate — a watcher body re-entered mid-statement is the hazard, not the
remedy. (This row used to say *"from a handler"*; a fixture with **no handler
in it** stalls identically, which is what corrected it.)

**Choosing a file** needs Qt's own dialog, not the desktop's, and that is
forced rather than offered: with the native chooser `selectFile` does nothing
and accepting does nothing, so a program could neither preselect a path nor
answer one — `qt.answer` would be a silent no-op. Cancelling answers
**`unknown`**, never `""`, because the empty string is the plausible wrong
answer: `if path = "" then` happens to work where `read({file}path)` does not.
`filter:` takes text or a **list**, joined with Qt's `;;` for you. The options
are `title:`, `folder:`, `name:` and `filter:`, closed and refused by name with
a near-miss hint; `qt.choose_folder` refuses `filter:` and `name:`, which Qt
would ignore.

**The containers, and why there are no alignment properties.** A `row` packs
left to right and a `column` top to bottom, each taking `margin:` and
`spacing:`; a `group "Caption"` is a titled box around whatever is inside it;
and a `form` lays its parts out as labelled rows, which is the whole of what
`QFormLayout` does.

**A `stretch` is pure space** (written `stretch()`), and it is the reason this vocabulary carries no
`align:` anywhere: a stretch absorbs whatever room is left over, so to push a
button to the right you put a stretch to its left. It is not a widget, so it
takes no name and nothing can be set on it — anything you try to set is
refused rather than quietly applied to the row it sits in.

A `button` carries its caption in `text:` rather than `label:`, which is Qt's
own spelling for it; `label:` on a `field` is the caption placed beside it, and
in a `cells` grid that caption **takes a cell of its own**, which is why a
labelled field occupies two.

**`tabs` holds `tab` pages.** A page is given a column layout, so a `field`, a
`row`, a `group` or a whole `table` works inside one with no further vocabulary:

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
tabs settings
    tab "General"
        field who( label: "Name" )
    end tab
    tab "Advanced"
        check deep( label: "Verbose" )
    end tab
end tabs
```

A `tabs` may carry a **name** (`tabs settings`) so `on settings.currentChanged`
and `win.read("settings", "currentIndex")` can find it. **A container takes a
name or options, not both** — `tabs settings( movable: true )` with a body costs
four grammar conflicts, measured — so a named container's properties are set
with `qt.set` instead.

**`split` is a row or column you can drag, and `scroll` is a region that
scrolls.** `row` and `column` are boxes; a `split` puts a draggable handle
between its panes, and `scroll` is for an ordinary column of widgets, since
`source`, `memo` and `table` already scroll themselves:

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
split across
    scroll files
        column( margin: 4 )
            text f1( label: "one.bas" )
        end column
    end scroll
    split down
        source code()
        table out to show rows
            field line( label: "Line" )
        end table
    end split
end split
```

**A split's direction is a word, not a name.** It is `across` or `down`,
refused by name with a near-miss hint if it is anything else — the one free
slot carries the direction, so a `split` **cannot be named** and `qt.pane` is
how you observe one. A `scroll` needs no direction, so it *may* be named, which
is what makes `widgetResizable` and the scrollbar policies readable by name.

**A split needs at least two panes.** With one there is no handle to drag
(n panes give n−1), so that is a `row` or a `column` and it is refused as one.
And a `field` or a `choice` carrying a `label:` as a *direct* pane is refused
too: a pane has no layout, and those two have their caption placed by the
parent — so it would render the widget and drop the word in silence.

**A window can have its own furniture: dock panes, a toolbar and a status
bar.** All three belong to the window rather than to a layout, the rule a `menu`
already follows and for the same measured reason — a window with any of them is
a `QMainWindow`, which places its dock areas, status bar and tool bars around
the content itself:

<!--fragment: the body of a `window` declaration-->
```basic
toolbar main
    item open( label: "Open" )
    separator()
    item save( label: "Save" )
end toolbar

docks right
    dock branches "Branches"
        text b( label: "main" )
    end dock
    dock results "Results"
        memo out()
    end dock
end docks

status
    text msg( label: "Ready" )
    stretch()
    text pos( label: "1:1" )
end status
```

**A `docks` block carries the area and its panes are tabbed together.** The area
is a word in the one free slot — `left`, `right`, `top` or `bottom`, refused by
name with a near-miss hint — which is `split across`'s arrangement. Panes in one
block become **tabs along that edge** rather than a stack down it, which is what
an IDE does with a column of panels; panes in a second block go to a second
edge. A block with no panes is refused, since it would reserve an edge and show
nothing.

**A `dock` carries a name *and* a caption** — `dock branches "Branches"` — the
one shape in this vocabulary that carries both. The caption is what a person
reads on the pane's title bar; the **name** is what `qt.set` and `qt.dock`
address, and what Qt keys a saved layout by. A pane is given a column, like a
`tab` and a `scroll`, so anything works inside one.

**A `toolbar` holds what a menu holds.** Its entries are the same `item`s and
`separator`s, with `shortcut:`, `checkable:` and `icon:` all working on them —
so a toolbar needed no part of its own. It carries a **name** rather than a
caption, because that is what a saved layout is keyed by.

**A `status` bar takes no name, and holds `text` and `stretch`.** There is one
per window, so a second is refused rather than silently merged into the first,
and what a program updates is the text *in* it: a `text` part there is an
ordinary widget whose `text` property `qt.set` reaches, which is why a status
bar needs no verb of its own. `stretch()` pushes what follows to the right.

**The caret needs a verb, and that is measured rather than assumed.**
`textCursor` is neither a property nor invokable by name — it answers a
`QTextCursor`, where the by-name route moves QVariants — so it is one of the
genuinely unreachable things, like the highlighter and the margin, rather than
something `qt.read` already does.

<!--fragment: inside a handler or a program that has opened the window-->
```basic
win.caret_to("code", 7, 5)      ' a diagnostic's own line:column
c = win.caret("code")           ' { line: 7, column: 5, selecting: false }
```

**A handler that changes what it is watching calls itself.** A selection
handler that calls `qt.refresh` alters the selection, which fires the handler
again — a feedback loop in the program rather than a fault in Qt, because every
round genuinely changes something. It is bounded at **16 deep** and reported as
warning **2117**, which names the handler and the shape; past that the handler
is simply not called again, so the program carries on. Sixteen is eight times
the deepest nesting any program in this tree reaches, which is two — a handler
that opens a modal window whose own button handler runs.

A refresh called from inside a handler is also **safe**, which is worth saying
because the obvious implementation is not: a tree's refresh deletes every item,
and the Qt call that fired the handler is still running on one of them. Items
taken out inside a handler are kept alive until the call returns, so a blunt
redraw is slow rather than fatal.

**A window with furniture cannot be shown modally** — those things belong to a
main window and a modal window is a dialog — so `qt.open_modal` refuses it
rather than opening a window whose declared panes are not there.

**`cells` holds rows of cells.** A grid *is* rows, so `row` is reused — refined
inside `cells` to mean a row whose columns line up with the rows above, where a
`row` elsewhere is just a box. `across:` and `down:` span columns and rows,
which is the thing a box layout cannot do:

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
cells( spacing: 6 )
    row
        field first( label: "First" )
        field last(  label: "Last" )
    end row
    row
        field notes( label: "Notes", across: 3 )
    end row
end cells
```

A `field`'s label takes a cell of its own, which is how a labelled grid lines
up; a `check` carries its own caption and takes none. A part written directly
inside `cells` is refused — it has no row to go in.

**Theming is Qt's stylesheet language, and most of it was already there.**
`styleSheet` is an ordinary Qt property, so any widget takes one through the
same by-name route as every other option:

<!--fragment: the inside of a `window` block, not a program on its own-->
```basic
button go( text: "Go",
           styleSheet: "QPushButton { background: #2d6cdf; color: white; }" )
```

`qt.theme` is the part that route cannot reach: **one sheet for the whole
application**. Qt resolves the cascade between the two, so a widget's own sheet
still wins. It takes *text* rather than a filename — `qt.theme(read({file}
"dark.qss"))` composes out of what the language already has — and it needs no
display, so a program sets its theme before asking `qt.can_display()`.

**No `load` is needed.** A `window` block implies no import, so neither does
rendering one.

**With no display, `qt.open` refuses and the program continues** — catchably,
so a program can fall back to a text interface. gBASIC itself runs completely on
a machine with no display: only opening a window needs one, and a `window` block
still parses, is validated and can be read back as a record with no Qt present.
Setting `QT_QPA_PLATFORM=offscreen` renders without a display, which is how a
GUI program can run on a headless server.

An option on a part that is not one of the layout words (`label`, `margin`,
`spacing`, `grow`) is set as a **Qt property by name**, so `text:` and
`enabled:` work on any widget that has them and a misspelling is refused naming
the widget's class. `type:`, `show:` and `edit:` are carried by the declaration
and not yet acted on; see `docs/qt_gui_design.md` §0.4.

## PDF documents (`gpdf`)

`load gpdf` — pure gBASIC, written from ISO 32000. The document, the core-14
fonts, measured text and word wrap; then tables that flow across pages
(`gpdf.table`), images copied rather than re-encoded (`gpdf.image`) and a
`chart` SVG placed as PDF vectors (`gpdf.svg`). Embedded fonts are not built.

```basic
when {datetime}= "2026-01-01 12:00:00"
d = gpdf.document({ size: "letter", title: "Invoice 1041", created: when })
d = gpdf.add_page(d)
d = gpdf.set_font(d, "Helvetica-Bold", 18)
d = gpdf.text(d, "Invoice 1041")
d = gpdf.set_font(d, "Helvetica", 11)
d = gpdf.paragraph(d, 400, long_text)     ' wraps, and adds pages as needed
gpdf.save(d, "invoice.pdf")
```

A document is a **value**: `add_page`, `set_font`, `text` and `paragraph` each
return a new one. A function cannot write back through its argument, so an API
that appeared to mutate would silently do nothing to the caller's document.

**Three rules shape everything else.**

**The output is byte-deterministic.** A PDF carries a `CreationDate` and an
`/ID`, and taking either from the clock would make a golden impossible — the
same trap the xlsx writer avoids by fixing ZIP mod-times. `created:` takes a
datetime; a document given none carries **no** `CreationDate` at all, which is
honest, where an invented one is not.

**An unrepresentable character is refused by name, never substituted.** Phase 1
is WinAnsi, so it cannot write Cyrillic or CJK. The alternative to refusing is
putting some *other* character on a document that has already been posted, and
the refusal names the character, its codepoint and its position. `representable(text)`
answers a boolean instead, so an application can check a customer's name where
it is entered rather than where a document is produced.

**Offsets are counted in bytes.** The cross-reference table stores the byte
offset of every object and `len` counts codepoints, so `byte_count` is used
throughout. Not hypothetical: the library that inspired the phase-1 shape emits
`startxref 753` for an xref that begins at byte 799, and every strict reader
has to repair it.

| function | what it does |
|---|---|
| `gpdf.document(options)` | `size` (a name from `gpdf.sizes()` or `{width,height}` in points), `margin`, `title`, `author`, `subject`, `creator`, `created`, `compress` |
| `gpdf.add_page(doc)` / `gpdf.page_count(doc)` | pages |
| `gpdf.set_font(doc, name, size)` | one of the core 14; anything else is refused by name |
| `gpdf.text(doc, s)` / `gpdf.text_at(doc, x, y, s)` | draw a line |
| `gpdf.paragraph(doc, width, s)` | wrapped text, adding pages as it runs out of room |
| `gpdf.wrap(font, size, width, s)` | the lines, without drawing them |
| `gpdf.text_width(font, size, s)` | points — what wrapping and alignment are built on |
| `gpdf.representable(s)` / `gpdf.to_winansi(s, whose)` | encoding |
| `gpdf.image(doc, path, x, y, options)` | a PNG or JPEG, copied not re-encoded — see below |
| `gpdf.svg(doc, svg, x, y, options)` | a `chart` SVG as PDF vectors — see below |
| `gpdf.table(doc, rows, spec)` | a table that flows across pages — see below |
| `gpdf.number_pages(doc, options)` | `Page 3 of 7` on every page |
| `gpdf.rule(doc, x1, y, x2, thickness)` | a horizontal line |
| `gpdf.sizes()` | the named page sizes |
| `gpdf.render(doc)` / `gpdf.save(doc, path)` | bytes, or `nothing` (the file is written; `file_size(path)` is its size) |

### Tables that flow across pages

The shape every business document takes — invoices, statements, ledgers,
reports. In a cursor API you write the page-break arithmetic by hand each time,
which is where the header stops repeating and a row lands half on each page.

```basic
spec = { columns: [ { name: "line",        heading: "#",           width: 30, align: "right" },
                    { name: "description", heading: "Description", width: 300 },
                    { name: "amount",      heading: "Amount",      width: 90, align: "right", total: true } ] }
d = gpdf.table(d, rows, spec)
d = gpdf.number_pages(d, {})
```

`rows` is an array of records **or a frame** (a record of column → array), so
the spreadsheet, accounting and statistics layers feed it directly. Spec
options: `columns`, `heading_font`, `heading_size`, `padding`, `rules`,
`totals_label`, `x`. A column takes `name`, `heading`, `width` (points, and
required), `align` (`left`/`right`/`center`), `format` (decimal places for a
number) and `total`.

**A row is never split.** A cell that wraps to three lines makes its row three
lines tall, and the whole row moves to the next page if it does not fit —
splitting would put a description on one page and its amount on the next, which
reads as two different transactions. The heading is redrawn after every break.

**Totals add with the value's own arithmetic**, so a column of `money` in two
currencies is refused by `money` itself rather than producing a number that
means nothing. The table never learns what a currency is.

**`gpdf.number_pages(doc, options)`** stamps `Page {n} of {total}` on every
page. That total is not known until the document is finished, which in a
streaming writer means a second pass or patching bytes; here the pages are
values in an array, so stamping them at the end is exact and costs nothing.
Options: `format` (with `{n}` and `{total}`), `font`, `size`, `y`, `align`.

**`gpdf.rule(doc, x1, y, x2, thickness)`** draws a horizontal line, for a
caller doing its own layout.

Wrapping is a **measurement**, not a character count: `iiiii iiiii` and
`MMMMM MMMMM` occupy very different widths at the same length. A word wider
than the whole column is **broken** rather than allowed to overflow into the
next one.

### Images

`gpdf.image(doc, path, x, y, options)` places a PNG or JPEG, with `x, y` the
bottom-left corner. `width`/`height` scale it; giving **one** keeps the aspect
ratio, and giving neither uses the pixel count as points.

```basic
d = gpdf.image(d, "logo.png", 72, 700, { width: 160 })
```

**The bytes are copied, not re-encoded.** A non-interlaced PNG's pixel data is
already a zlib stream and a JPEG is already DCT, and PDF speaks both —
`/FlateDecode` with a PNG predictor, and `/DCTDecode`. So nothing is decoded,
nothing is recompressed, and a photograph costs in the document what it costs
on disk. Palette PNGs carry their `PLTE` into an `/Indexed` colour space, which
is what makes the pass-through work for the commonest kind of logo.

Every `IDAT` chunk is concatenated: a PNG of any size has several — the
project's own mascot has six — and taking only the first yields a truncated
stream that still produces a structurally perfect PDF with the picture cut off.

The same file placed twice is stored **once**, so a logo on forty pages is one
stream.

Two things are **refused by name**: an **interlaced** PNG (Adam7 stores the
pixels in seven passes, which PDF cannot read directly) and one with an **alpha
channel** (carrying it means decoding the pixels and splitting the alpha into a
soft mask, which this phase does not do — and dropping it silently would put a
black box where the transparency was).

### Charts, as vectors

`chart` renders to SVG, so a report wants that chart *in* the PDF.
`gpdf.svg(doc, svg_text, x, y, options)` translates it — `x, y` is the
bottom-left corner, and `width`/`height` scale it.

```basic
s = chart.spec("line", frame)
s = chart.x(s, "quarter")
s = chart.y(s, ["revenue", "costs"])
d = gpdf.svg(d, chart.render(s), 72, 480, { width: 460 })
```

The result is **real vectors**: it scales, prints sharp, and its labels stay
*text* — searchable and selectable, where a rasterised chart is a picture of a
chart.

**This is not an SVG renderer and does not pretend to be one.** It translates
the subset `chart` emits — `line`, `rect`, `circle`, `path` (with `M`, `L`, `A`
and `Z`) and `text` — and **refuses anything else by name**. We own both ends,
so the day `chart` grows an element it says so rather than quietly dropping it;
that refusal is the only thing that makes a subset translator honest.

Two details worth knowing, because both are places a translation goes silently
wrong:

- **SVG's y grows downward, PDF's grows upward.** The flip is arithmetic per
  coordinate rather than a negative-y transform, because such a transform would
  mirror the *text* as well and every label would come out backwards.
- **PDF has no arc operator**, so a pie's arcs become cubic Béziers — computed
  with **no trigonometry**. A circular arc's control points follow from its
  endpoints through the half-angle identities, needing only `sqrt`, and a span
  wider than 90° is bisected by vector arithmetic. gBASIC has no trig builtins
  and `chart` carries its own Taylor sine; a second copy here would be a second
  thing to drift.

`gpdf_metrics` holds the core-14 widths, generated by
`tools/make_pdf_metrics.py` from the URW base-35 AFMs and **committed** — a PDF
naming Helvetica does not embed it, the reader supplies its own copy, so our
widths must be the ones the reader assumes or measured and drawn text disagree.
That table cannot depend on which fonts the build machine happens to have, the
same argument `src/currency_table.h` makes.

- `gpdf_metrics.fonts()` — the fourteen names, in the spelling a PDF uses
  (`Helvetica-BoldOblique`, `Times-Roman`). This is the list `gpdf.set_font`
  names back to you when it refuses one.
- `gpdf_metrics.widths(font)` — 256 widths in 1/1000 of the font size, indexed
  by **WinAnsi byte**, or `unknown` for a name that is not one of the fourteen.
  A width of `-1` marks a byte WinAnsi does not encode; `gpdf` refuses such a
  byte rather than drawing something else. Called directly only by something
  doing its own layout — `gpdf.text_width` is the ordinary way in.

`tests/run_gpdf.sh` validates output with **three independent readers** —
mupdf, ghostscript and poppler — because a PDF writer checked by its own reader
proves only self-consistency.

## Compression

Built with zlib (`HAVE_ZLIB`), two builtins — no `load` required. They take and
return **binary strings**, so every byte value including NUL is content.

- `compress(s [, { format:, level: }])`
- `uncompress(s [, { format:, max_bytes: }])`

**The container is declared, not assumed**, because the three zlib speaks are
not interchangeable and the difference is silent:

| `format` | what it is | who wants it |
|---|---|---|
| `zlib` (default) | RFC 1950 — two header bytes and an Adler-32 | PDF `/FlateDecode` |
| `raw` | RFC 1951 — deflate with no wrapper at all | a ZIP member |
| `gzip` | RFC 1952 — the `.gz` wrapper | a `.gz` file |

Handing a raw stream to a reader expecting zlib does not fail cleanly in C: it
reads the first byte as a compression method and goes wrong from there. Here it
is **refused by name** — `the input is not a valid zlib stream` — and the three
are distinguishable on the wire (`0x78`, `1f8b`, and raw being shortest for
carrying no header).

`level` is 0–9 (0 stores, so the output is slightly *larger* than the input);
the default is zlib's own. `level` is a `compress` option and `max_bytes` an
`uncompress` one, and passing either to the wrong side is refused, as is an
unknown option — **by name**, since a misspelled `fromat:` that was ignored
would quietly produce a stream in the wrong container.

**`uncompress` caps its output.** A compressed stream carries no trustworthy
statement of its own size and a few kilobytes can expand to gigabytes, so a
program reading a file it did not write gets a bound rather than an
out-of-memory. The default is 256 MB and `max_bytes:` raises it; the refusal
names the limit that was hit rather than reporting corruption.

```basic
z = compress(report_text)                  ' zlib, for PDF /FlateDecode
raw = compress(part, { format: "raw" })    ' for a ZIP member
back = uncompress(z)
```

zlib had been linked since the xlsx engine shipped and was reachable **only**
from inside it, so no gBASIC program could deflate a byte. Found while checking
whether a PDF writer was feasible. See `tests/run_compress.sh`.

## Cryptography

When gBASIC is built with libcrypto (OpenSSL), a family of cryptographic builtins
is always available — no `load` is required. They operate on strings; hash, HMAC,
AEAD, and random outputs are **binary strings** (raw bytes), which you render with
`hex_encode`/`base64_encode` when you need text. `secure_token`, `password_hash`,
and `password_verify` are documented under [Core Builtin Functions](#core-builtin-functions)
— and they are **not** part of this family: `secure_token` needs nothing beyond
the leanest build, and `password_hash` has its own backend (libxcrypt, or the
vendored yescrypt), so a build without libcrypto still has all three.

Encoding:

- `base64_encode(s)` / `base64_decode(s)` — standard Base64.
- `base64url_encode(s)` / `base64url_decode(s)` — URL-safe Base64 (no padding).
- `hex_encode(s)` / `hex_decode(s)` — lowercase hexadecimal.
- `base32_encode(s)` / `base32_decode(s)` — RFC 4648 Base32, the encoding a
  TOTP shared secret is stored and displayed in. **`base32_decode` answers
  `unknown` for input that is not Base32**, not `""` — "not Base32" and "empty"
  are different answers and a caller must be able to tell them apart. It
  accepts **lower case and whitespace**, deliberately: a secret is a thing
  people transcribe by hand and authenticator apps display in spaced groups, so
  case and spacing are the ordinary transcription noise and refusing them would
  turn a usability problem into a lockout. Anything outside the alphabet is
  still refused.

Random and comparison:

- `random_bytes(n)` — `n` cryptographically random bytes.
- `bytes_equal(a, b)` — constant-time string comparison (use for secrets/MACs).

Hashing and HMAC:

- `password_hash_cost()` -> `{ ms, prefix }` — what one password hash costs
  **on this machine**, measured by performing one, plus the algorithm and
  parameter field the backend embedded (`$y$j9T$` is yescrypt at its default
  cost, which is what both backends write). `password_hash` takes no options, so this is how a deployment states
  its KDF posture rather than assuming it. It takes as long as a hash — a
  diagnostic, not a per-request call.
- `sha256(s)`, `sha384(s)`, `sha512(s)`, `sha1(s)` — message digests.
- `md5(s)` — 16 raw bytes. Provided for **compatibility** — reading a checksum
  someone else produced, or a legacy format that specifies it. MD5 is broken
  for collision resistance, so never use it for signatures, password storage or
  deduplicating untrusted input; reach for `sha256`.
- `hmac_sha256(key, message)`, `hmac_sha384(key, message)`,
  `hmac_sha512(key, message)` — keyed MACs.
- `hmac_sha1(key, message)` — **exists for TOTP and for nothing else worth
  doing.** RFC 6238 permits SHA256 and SHA512, and authenticator apps in
  practice ignore the algorithm parameter and compute SHA1 anyway, so
  interoperating requires it. SHA-1 is broken for collision resistance; HMAC
  does not rest on that, which is why this remains sound *here* and is still
  the wrong reach for anything new.

Password-based key derivation — turning a **passphrase** into key bytes:

- `pbkdf2_sha256(password, salt, iterations, length)` /
  `pbkdf2_sha512(...)` — PBKDF2-HMAC (RFC 8018). Returns exactly `length` raw
  bytes, ready for `aes_gcm_encrypt`.
- `scrypt(password, salt, n, r, p, length)` — scrypt (RFC 7914). `n` is the CPU
  and memory cost and must be a **power of two greater than 1**; `r` is the block
  size and `p` the parallelisation. Memory-hard, so prefer it when you can afford
  the memory: it is allowed exactly what its cost needs, `128 × r × (n + p + 2)`
  bytes (64 MiB at the recommended `n = 65536, r = 8`), and a cost the machine
  cannot give is refused, naming how much it asked for, rather than derived
  with less.

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

For SSH and other protocols that carry their own key formats (since 0.11.0,
`docs/crypto_design.md` §9):

- `x25519_keypair()` — a record `{ public, private }`, 32 raw bytes each.
- `x25519_public(private)` — the 32-byte public key of a private one.
- `x25519_shared(private, peer_public)` — the 32-byte shared secret
  (`curve25519-sha256`). A peer key that gives an all-zero secret, a
  low-order point, **raises** (RFC 7748 §6.1); `on error` is how a server
  drops that connection.
- RSA keys are **records of big-endian byte strings**: `{ n, e }` public,
  `{ n, e, d, p, q, iqmp }` private, the six parts SSH and OpenSSH key files
  carry (an SSH `mpint` without its sign byte).
- `rsa_generate(bits)` — a private key; `bits` is a multiple of 8 from 2048 to
  16384.
- `rsa_public(key)` — `{ n, e }`.
- `rsa_sign(private, message, hash)` — PKCS#1 v1.5, `hash` `"sha256"` or
  `"sha512"` (`rsa-sha2-256`, `rsa-sha2-512`). SHA-1 is **refused** for
  signing. A key whose parts do not belong together is refused rather than
  signed with.
- `rsa_verify(key, message, signature, hash)` — `true` / `false`; `hash` may
  also be `"sha1"`, so a legacy `ssh-rsa` host key can be checked.
- `rsa_from_pem(text)` / `rsa_from_pem(text, passphrase)` — reads PKCS#1,
  PKCS#8 (encrypted too) and public keys as PKCS#1 or SubjectPublicKeyInfo.
- `rsa_to_pem(key)` — PKCS#8 for a private key, SubjectPublicKeyInfo for a
  public one, unencrypted.
- `x509_parse(cert)` — what an X.509 certificate says, from PEM text or DER
  bytes: `{ subject, issuer, not_before, not_after, serial, fingerprint_sha256,
  der, key_type, public_key }`. Names are RFC 2253 text, as `openssl x509
  -nameopt RFC2253` prints them; the dates are datetimes in UTC; `serial` and
  `fingerprint_sha256` are big-endian bytes (`hex_encode` for text). `key_type`
  is `rsa` (`public_key` is `{ n, e }`, as `rsa_verify` takes), `ec` (`{ curve,
  public }`, as `ecdsa_verify` takes), `ed25519` (the 32 bytes `ed25519_verify`
  takes), or `ec_other` or `other` with `public_key` unknown. It **checks
  nothing**: not the signature, the dates or a chain, which are the caller's
  decisions (a SAML identity provider's certificate is often self-signed and
  pinned by metadata). Refused: text with no certificate, and DER with bytes
  after the certificate.
- `aes_ctr(key, counter, data)` — AES in counter mode (`aes128-ctr`,
  `aes256-ctr`); `key` is 16, 24 or 32 bytes and `counter` a 16-byte block.
  Answers `{ data, counter }`, where `counter` is the next block's, so packets
  chained by it are one keystream. Encrypting and decrypting are the same call.
  A partial last block uses up its whole block.
- `bcrypt_pbkdf(passphrase, salt, rounds, length)` — the key derivation of a
  passphrase-protected OpenSSH private key (`openssh-key-v1`, kdf `bcrypt`):
  `length` bytes, 1 to 1024. For an `aes256-ctr` key file ask for 48, the
  32-byte key followed by the 16-byte counter. This is OpenSSH's own code (see
  LICENSING.md).
- `chacha20(key, iv, data)` — the raw ChaCha20 stream: `key` 32 bytes, `iv`
  16 bytes (the block counter, then the nonce, little-endian; the counter
  carries as a 64-bit one, OpenSSH's layout). Encrypting and decrypting are
  the same call. These are the parts of `chacha20-poly1305@openssh.com`, which
  is **not** RFC 8439's AEAD.
- `poly1305(key, message)` — the 16-byte one-time tag; `key` is 32 bytes and
  must never be used twice.
- EC keys are records `{ curve, public, private }`: `curve` is `"p256"`,
  `"p384"` or `"p521"`, `public` the uncompressed point (`04 || X || Y`),
  `private` the scalar, big-endian, padded to the curve's size.
- `ec_keypair(curve)`; `ec_public(curve, private)` — `{ curve, public }`.
- `ecdsa_sign(key, message)` — `{ r, s }`, hashed with the curve's own hash
  (SHA-256, SHA-384, SHA-512), as SSH fixes it.
- `ecdsa_verify(key, message, { r, s })` — `true` / `false`.
- `ecdh_shared(key, peer_point)` — the shared secret, the curve's size. A point
  not on the curve **raises**.
- `dh_keypair(p, g)` — `{ private, public }` for classic Diffie-Hellman over
  the group `p` (at least 2048 bits) and `g`, as big-endian bytes.
- `dh_shared(p, private, peer)` — the shared secret. A peer value of 1 or less,
  or of p − 1 or more, **raises** (RFC 4253 §8).
- `hkdf(hash, ikm, salt, info, length)` — RFC 5869; `hash` is `"sha256"`,
  `"sha384"` or `"sha512"`, `length` at most 255 times its size.
- `hash_start(alg)`, `hash_update(state, data)`, `hash_finish(state)` — a
  digest built in pieces (`alg` as for `hkdf`). **The state is a value**:
  `s = hash_update(s, chunk)`. It can be copied and continued two ways, and
  finishing does not consume it. A state belongs to the machine that made it;
  do not store it.

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
  GUI responsive, run it inside a spawned actor and receive the result with a
  `watch(inbox.messages)` watcher, which the event loop delivers (the actor +
  event-loop pattern).

### Live child control

`process.run` decides everything before the child starts and hands back one record
after it is gone. When a program needs to **supervise** a child instead — watch it,
read it as it goes, and end it on its own terms — six primitives expose the same
machinery while the child is alive. They are additive: `process.run` is unchanged
and remains the right tool whenever you only want the finished result.

```basic
h = process.start({ command: "./long-job", args: ["--verbose"] })

while true
    c = h.read()          ' whatever has arrived; never blocks
    if byte_count(c.stdout) > 0 then
        print c.stdout
    end if
    s = h.poll()          ' never blocks
    if not s.running then
        break
    end if
    sleep(0.05)
end while

print "exit " + s.exit_code
h.release()
```

**`process.start(options)`** → a **process handle**. Same `command` / `args` /
`cwd` options as `process.run`, validated identically; `timeout` is rejected,
because bounding a run is now the caller's job (`process.wait` or `process.stop`).
Returns as soon as the child is launched. A child that cannot be launched **raises**
— exactly as with `process.run`, so it stays distinguishable from a child that ran
and exited nonzero. `type(h)` is `"process"`.

**`stdout:` and `stderr:`** *(2026-11-09)* say where the child's output goes:
- **`"pipe"`** (the default): the parent reads it with `process.read`.
- **`"null"`**: thrown away.
- **`"inherit"`**: the parent's own stream.
- **a file**, such as `{file}"run.log"`: appended to, and created if absent.

A stream sent elsewhere reads as `""` from `process.read`. A bare string other
than the three words is refused, so a file named `null` is never mistaken for
the device. A file that cannot be opened raises naming it. On Windows only
`"pipe"` is taken.

This is how a detached child gets an output that cannot break. A child whose
stderr is a pipe nobody reads any more no longer dies writing to it. gBASIC
itself survives that write (below), but another program may not.

**A broken pipe kills a gBASIC program only through stdout** *(2026-11-09)*.
A write to stdout after its reader has gone still ends the program by
SIGPIPE, so `gbasic prog | head` ends quietly, as a command-line program
should. A write to any other pipe fails instead, and the program goes on:
stderr, where a warning or `print to error` lands, and a child's stdin. It used
to be killed there too. Measured on a detached child: one note on a broken
stderr ended it, exit -13, mid-run.

**`limits: { memory: bytes }`** *(Unreleased; gdash's GDASH-16 B1)* runs the child
under that memory limit, set before `exec` so it binds the program and not
the caller: POSIX `RLIMIT_DATA` (the heap and private anonymous memory, not the
address space, which for a gbasic linked with Qt is much larger than what it
uses), and no core dump, since a child killed by its own limit would write one
about the limit's size. A child that runs past it fails an allocation; what
it does then is its own runtime's, and **a gBASIC child dies by SIGABRT**
(`signal` 6). The kernel keeps no record that a limit was hit, so the status of
a limited child gains the evidence rather than a verdict: **`memory_limit`**,
as given, and **`peak_memory`**, its peak resident bytes when it was reaped
(`unknown` before). A child with no limit has the status it always had. An
unknown limit is refused by name; `limits` is refused on Windows. **A limit too
small to start the program in** fails differently: measured on a build linking
Qt (86 shared libraries), gbasic under 3.5 MB never runs, the dynamic loader
failing to map a library, and exits **127** with signal 0; from 4 MB it starts.
The floor depends on the build and its libraries, so a test forcing the
out-of-memory path wants a margin (16 MB and a child that grows), and checks
`signal = 6` rather than "not success".

```basic
h = process.start({ command: "gbasic", args: ["refresh.bas"], limits: { memory: 500000000 } })
s = process.wait(h)
if s.signal = 6 and s.peak_memory > s.memory_limit / 2 then
    print "the refresh ran out of its memory"
end if
```

**`handle.poll()`** → `{running, exit_code, signal, success}`. Never blocks.

**`handle.read()`** → `{stdout, stderr}` — everything that has arrived since
the previous read, and nothing else. Never blocks.

**`process.wait(handle)`** / **`process.wait(handle, seconds)`** → the same status
record. Without a timeout it waits for the child to exit; with one it gives up
after that long. **A wait that expired is reported by `running` still being `true`**
— there is no separate `timed_out` field to consult. The timeout accepts a number
of seconds or a duration (`process.wait(h, 5 seconds)`).

**`process.stop(handle)`** / **`process.stop(handle, {force_after: seconds})`** →
the status record. See escalation below.

**`handle.release()`** → closes the pipes and reaps the child. Idempotent,
and never required for correctness — see abandonment below.

**`handle.write(text)`** → the number of bytes written. Requires the
child to have been started with `stdin: "pipe"`; without it the child inherits
this program's stdin (the default, unchanged) and `write` raises rather than
silently writing somewhere surprising.

**`handle.close_stdin()`** → closes the child's stdin, which is how a
child that reads to end-of-input is told there is no more. Idempotent.

#### Writing to a child

`stdin: "pipe"` is opt-in because the default is inheritance and inheritance is
what a shelled-out command usually wants. A conversation is the case that needs
it: `mcp.connect({transport: "stdio", ...})` launches a server and then has to
*speak* to it, and a child you can only listen to cannot hold a conversation.

```basic
h = process.start({ command: "./server", args: [], stdin: "pipe" })
n = h.write(line + "\n")
reply = h.read()                ' whatever has arrived; never blocks
x = h.close_stdin()             ' "no more input"
```

The write pipe is **blocking**, unlike the read pipes: a partial write would
leave the caller holding half a message with no framing to resynchronise on, and
every caller would have to write the same retry loop. A child that never drains
its stdin can therefore stall a `write` — bound it the way you would bound
anything else, with `process.stop`.

**A child that has gone is a short write, not a raise.** If the child closed its
stdin or exited, `write` returns fewer bytes than it was given (often zero)
rather than raising: for a conversation that is an ordinary outcome, and the
caller learns it from the count and from `process.poll`, which can say *why*.


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

**One exception, by design:** a G-SQL database's owner, which `gsql.connect`
starts, outlives the program that started it while another program is using
the database, and for its linger after that (`docs/gsql_owner_lifetime_design.md`),
so one program ending never cuts another off. It stops by itself; nothing
else does.

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
nested graph. **Reflectable is broader than serializable**: a live process
handle is reflectable (its kind and type) but not serializable.

Environment:

- `reflect.variables()` → a sorted string array of the **current scope's own**
  variable names (globals at top level; a function's own locals inside a function).
- `reflect.get(name)` → the value of a named variable (a copy); raises if unknown.

Value description (each takes one value):

- `reflect.kind(v)` → the value kind: `"number"`, `"string"`, `"boolean"`,
  `"nothing"`, `"unknown"`, `"array"`, `"record"`, `"function"`,
  `"datetime"`, `"duration"`, `"money"`, `"file"`,
  `"directory"`, `"actor"`, database-connection kinds, …
- `reflect.type(v)` → the value's type, which today is its kind. Never a raw
  pointer.
- `reflect.category(v)` → `"scalar"`, `"container"`, `"function"`, or `"foreign"`.
- `reflect.serializable(v)` → boolean; **true** iff the value (recursively, for
  arrays/records) can be `serialize`d. Foreign/live handles (actors, processes,
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
process handle is `reflect.inspect`-able as `kind=process, category=foreign,
serializable=false` without exposing its internals.

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
`value` otherwise. **A missing field counts too** (*since 0.11.0*):
`default(r.candidates, [])` on a record without `candidates` answers `[]`,
where it used to raise `unknown record field`, since `default` is where a
caller says "this may be absent". That holds for a field of a name or of a
field chain (`r.a.b`); everywhere else `.field` stays strict, so a misspelling
still fails.

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

**AND `default` IS THE REMEDY FOR THE ONE DOOR THAT HAS NO OPINION.** Every
other operation on an absence either refuses or answers a question; `+`
**manufactures the word** (measured 2026-10-03, reported by the gbasic-books
session):

| | `nothing` | `unknown` |
|---|---|---|
| arithmetic, ordering | raises | raises |
| a condition | quietly **false** | raises |
| equality, `default` | answers | answers |
| **concatenation** | **`"a" + nothing` is `anothing`** | **`aunknown`** |

That falls out of two decisions each of which is right on its own — `+`
concatenates when either side is a string, and `string` is total — and the
result is that text meant for a person can contain the word `nothing` where a
number should be, with nothing raised and exit 0:

```basic
owed = nothing
print("Total owed: " + owed)      ' Total owed: nothing
print("Total owed: " + default(owed, "n/a"))
```

**There is deliberately no warning for it**, and the reason is worth knowing
because it is the same reason twice: showing that something is absent is
ordinary, so `print("find(zz)=" + find(f, "zz"))` is correct code of exactly the
shape above. Measured across every program in the tree, the coercion happens at
ten places and **eight are that idiom** — a rule firing on it would be wrong
about the language rather than merely noisy, which is why rows 6 and 7 of
`docs/warning_model_design.md` were built, measured and reverted. Use `default`
wherever the text is for a person.

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

**It answers for THIS BUILD, not for the version** *(corrected in 0.7.1)*.
A builtin that needs an optional dependency is in the table only when the
dependency was compiled in, so on a build without OpenSSL
`has_builtin("sha256")` is **false** and on one without zlib
`has_builtin("compress")` is **false**. That is the answer a program probing in
order to degrade needs: before 0.7.1 all three of `sha256`, `password_hash` and
`compress` answered **true** on published artifacts that cannot run them, so a
program which checked first and then called crashed on the branch it had
checked for.

The twenty names that move with the build are the hash, HMAC, AES-GCM, Ed25519
and key-derivation families (libcrypto), `password_hash`/`password_verify`/
`password_hash_cost` (libxcrypt or the vendored backend), and
`compress`/`uncompress` (zlib). Everything else answers the same on every
build. `base64_*`, `hex_*`, `base32_*`, `random_bytes` and `secure_token` are
deliberately **not** on that list — they are implemented here rather than
delegated, and work on the leanest build there is.

**A false answer and a refusal to run are two different things, and both are
honest.** Where the dependency is absent, `has_builtin` says `false` *and*
calling the function raises a message naming what to install —
*compression requires zlib; rebuild with it installed* — rather than
`undefined function`. `tests/run_capabilities.sh` asserts that pairing for
every one of the twenty, in both directions.

### Timing

**`sleep(seconds)`** - Blocks the current program for at least `seconds`, and
answers `nothing`. `seconds` must be a non-negative number; fractional values
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

**ISO 8601 INPUT** *(since 2026-10-03)*. A `datetime` reads the `T` separator
and a zone designator, so a timestamp off a web API parses as it stands:

```basic
a {datetime}= "2026-03-07T14:05:09"        ' 2026-03-07 14:05:09
b {datetime}= "2026-03-07T14:05:09Z"       ' 2026-03-07 14:05:09
c {datetime}= "2026-03-07T14:05:09+02:00"  ' 2026-03-07 12:05:09  — see below
```

`Z`, `+HH:MM`, `+HHMM` and `+HH` are all accepted, with their `-` forms.

**AN OFFSET IS HONOURED BY CONVERTING TO UTC**, and the type forces that rather
than taste. A gBASIC `datetime` is **civil** and carries no zone — which is why
`epoch(dt, zone)` exists — while `14:05:09+02:00` denotes an **instant**.
Turning an instant into a civil time needs a zone, and UTC is the only one the
text implies. Keeping the wall clock and dropping the offset would make
`14:05:09+02:00` and `14:05:09Z` the *same value* when they are two hours apart,
which is a wrong answer with nothing raised. `Z` and `+00:00` are therefore
no-ops, which is the common case; only a non-zero offset moves the digits, and
it moves them by **date arithmetic** — `2026-01-01T00:30:00+02:00` is
`2025-12-31 22:30:00`.

A zone with no time (`2026-03-07Z`), an impossible offset (`+99:00`) and a bare
sign are all refused. And a bare date is still a bare date: `2026-03-07` ends in
`-07`, so the suffix is looked for only **after** the date/time separator — a
scan from the right would read it as seven hours west.

### Date and time layouts

**`{string "YYYY-MM-DD hh:mm:ss"}d`** renders a `datetime` through a layout
written the way it reads — not `%Y-%m-%d %H:%M:%S`.

It is an **argument to `{string}`**, not a new verb, because every modifier is
named for what it *produces* and `{string}` already turned a value into text.
`{format "…"}` was the first proposal and reads as though text goes *in*.

```basic
d {datetime}= "2026-03-07 14:05:09"                   ' a Saturday
print({string "YYYY-MM-DD"}d)                         ' 2026-03-07
print({string "DDDD, D MMMM YYYY"}d)                  ' Saturday, 7 March 2026
print({string "DD/MM/YYYY"}d)                         ' 07/03/2026
print({string "h:mm pm"}d)                            ' 2:05 pm
print({string "YYYYMMDD"}d)                           ' 20260307
print("Posted " + {string "D MMM YYYY"}d)             ' Posted 7 Mar 2026
```

**Date parts are UPPERCASE, time parts are lowercase.** That one rule resolves
the collision every other scheme fumbles: `MM` is the month, `mm` is the
minutes.

**One or two letters is a number, three is a short name, four is a long name** —
the same for both `M` and `D`:

| | | | |
|---|---|---|---|
| `YYYY` 2026 | `YY` 26 | | |
| `M` 3 | `MM` 03 | `MMM` Mar | `MMMM` March |
| `D` 7 | `DD` 07 | `DDD` Sat | `DDDD` Saturday |
| `h` 14 | `hh` 14 | `m`/`mm` minutes | `s`/`ss` seconds |
| `am` / `pm` | the meridiem, in the case you wrote it | | |

(`DDD` is day-of-*year* in `strftime`; the obvious reading wins here over the
inherited one.)

**The 12-hour clock is implicit.** `hh` is 24-hour unless the layout also
carries `am` or `pm`. Midnight is `12:00 am` and noon is `12:00 pm`.

**Nothing but tokens and punctuation may appear**, and that is what makes the
notation safe rather than merely short. A letter run that is not a token is
**refused by name**, so prose goes *outside* the layout, where concatenation
already puts it. If prose passed through, `"Business hours: hh:mm"` would render
the `ss` in *Business* as seconds — measured against the system word list, 4,536
of 104,334 English words contain `ss` and 939 contain `mm`, about one word in
twenty-three, and they are exactly the words a caption uses: *business*,
*session*, *summary*, *assessment*, *comment*.

A **date-precision** value really is midnight, so `hh:mm:ss` renders
`00:00:00`. A **time-only** value has no date, so a date token is refused rather
than answered with a zero that looks like a year. A layout on anything that is
not a date or time is refused, and so is a second layout — a *list* is for
reading (see `{date "A", "B"}`), since there is no second way to render one
value.

### Reading a date through a layout

**`{date "DD/MM/YYYY"}cell`** reads text through the same notation, and takes a
**list** when one shape is not enough:

```basic
cell = "7 Mar 2026"
a {date "DD/MM/YYYY"}= "07/03/2026"                       ' 2026-03-07
b {date "YYYY-MM-DD", "DD/MM/YYYY", "D MMM YYYY"}= cell   ' tried in order
c {datetime "D MMM YYYY h:mm pm"}= "7 Mar 2026 2:05 pm"   ' 2026-03-07 14:05:00
d {time "hh:mm"}= "14:05"
```

`{date}`, `{datetime}` and `{time}` all take layouts; with no argument they stay
ISO-only, unchanged.

**THE ORDER IS THE DECLARATION**, which is what makes first-match-wins honest
here rather than a race. `03/07/2026` is 7 March or 3 July depending on where
the report came from, and writing `DD/MM/YYYY` ahead of `MM/DD/YYYY` is you
saying which:

```basic
print(string({date "DD/MM/YYYY", "MM/DD/YYYY"}"07/03/2026"))   ' 2026-03-07
print(string({date "MM/DD/YYYY", "DD/MM/YYYY"}"07/03/2026"))   ' 2026-07-03
```

**And a layout matches only if it also yields a valid date**, so a list
disambiguates itself where the data allows: `03/15/2026` skips the `DD/MM`
candidate rather than inventing month 15.

**A day name is checked, not ignored.** `DDDD` is redundant with the date, so
`"Sunday, 7 March 2026"` is **refused** — that day is a Saturday — rather than
admitting contradictory data.

A **two-digit year** takes the POSIX pivot: `00`–`68` is this century, `69`–`99`
the last, so a birthdate still reads.

When nothing fits, the message names **the text and every layout tried**,
because "could not parse" alone cannot tell you whether the data is wrong or
your list is short.

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
success from a program that meant to fail. It ends the program **from any
depth**: before 0.11.0, a bare `exit(70)` in a function two calls deep let that
function's next statements run, and its caller's, the status still 70 (book
gap G63), and a statement run that way could print a stray `nothing` (G48).

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
non-integer. Refusals, not guesses: a month has no fixed length. A duration
holds whole seconds, so a scaling that lands between two is refused as well:
`(1 second) * 2.5` raises, and `(1 second) * round(n)` is the way round it.
`(1 hour) / 3` is 20 minutes. `-d` negates each field, so `-(90 minutes)` is
`-90 minutes`, equal to `0 seconds - d`.

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

**`password_hash(password)`** - Hashes a password string at the backend's
default cost, writing `$y$j9T$` — yescrypt with N=4096, r=32, p=1 over 16 bytes
of salt. That is unconditional with the vendored backend, and was measured to be
libxcrypt's own default on every version from 4.4.27 to 4.5.2. The returned
string contains the algorithm, parameters, salt, and hash, so it can be stored
directly in an application user table. A password containing a NUL is refused rather than
truncated, because `crypt(3)` takes a C string.

**`password_verify(password, hash)`** - Returns `true` when the password string
matches a stored hash from `password_hash()`, and `false` otherwise. Verification
uses the algorithm and parameters embedded in the stored hash.

```basic
stored_hash = password_hash("correct horse battery staple")
if password_verify("candidate password", stored_hash) then
    print("ok")
end if
```

**There are two backends and one contract, and the contract is what matters:**
`crypt(3)` through libxcrypt where the build found it, and the vendored
yescrypt (`third_party/yescrypt`, BSD-2) otherwise — which is what **both
downloads and the Windows build** use. A hash is data, written by one machine
and checked by another, so both write `$y$j9T$` with 16 bytes of salt and
either reads the other's output. That is asserted rather than claimed:
`tests/windows/password_interop.bas` carries one hash made by each backend and
requires whichever is running to accept both, and the release build makes the
staged binary read a hash libxcrypt actually wrote.

**Where they differ is reading a hash gBASIC did not write.** libxcrypt also
reads the legacy `crypt(3)` schemes; the vendored backend reads `$y$` and `$7$`
only, and **refuses the rest by name** rather than answering `false`:

```
password_verify: this build verifies yescrypt ($y$) and scrypt ($7$) hashes; this one is $6$
```

A `false` there would say the *password* was wrong, locking out a real user with
nothing reported — so migrating an existing `$6$`, `$2b$` or `$1$` user table
onto a download means re-hashing on next login, and the refusal says so at the
point it matters. `has_builtin("password_hash")` answers for the build in hand,
and `password_hash_cost()` reports the posture it hashes at — both described
under `Environment` above.

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

**`type` answers for every value kind, and the list is longer than the seven
above.** Documented in full because it was not, and the gap is now held by a
tripwire in `tests/run_docs_gate.sh` that reads the answers out of
`builtin_type_name` in `src/eval.c` and requires every one of them to appear
here — in both directions, so an answer that is removed from the interpreter
cannot keep a paragraph on this page either.

<!--TYPEKINDS-->
| | `type` answers |
|---|---|
| core | `number`, `string`, `boolean`, `array`, `record`, `nothing`, `unknown` |
| typed values | `datetime`, `duration`, `money`, `file`, `directory` |
| language | `function`, `regex`, `watcher`, `actor`, `process` |
| handles | `postgres_connection`, `sqlite_connection`, `odbc_connection`, `ldap_connection`, `gsql_connection`, `gsql_statement`, `csv_source`, `xml_reader`, `http`, `net`, `workbook` |
<!--/TYPEKINDS-->

**Two answers are not the word you wrote**, and both read as a bug in your own
program rather than as a fact about the language:

```basic
type({date}"2026-10-02")   ' "datetime", NOT "date"
type({dir}"/tmp")          ' "directory", NOT "dir"
```

A `{date}` modifier builds a **datetime** whose time-of-day is midnight — there
is no separate date kind — and `{dir}` is the short spelling of a value `type`
calls `directory`. So `type(d) = "date"` is `false` with nothing wrong anywhere.

**Type predicates** - Return `true` or `false`:
- `is_string(value)` - checks if value is a string
- `is_number(value)` - checks if value is a number  
- `is_boolean(value)` - checks if value is a boolean
- `is_array(value)` - checks if value is an array
- `is_record(value)` - checks if value is a record
- `is_nothing(value)` - checks if value is `nothing`
- `is_unknown(value)` - checks if value is `unknown`

**THERE ARE SEVEN PREDICATES AND MANY MORE KINDS, SO `type` IS THE GENERAL TEST.**
`is_money`, `is_datetime`, `is_file` and the rest **do not exist** — the seven
that do are exactly the kinds with a literal form, which is the same reasoning
that ruled against `is_function`. Compare the string instead:

```basic
amount = {USD}"19.99"
if type(amount) = "money" then            ' the portable test
    print("that is money")
end if
' `if is_money(amount) then` -- there is no such function
```

Calling one of the absent ones reports `undefined function: is_money`, and
`has_builtin("is_money")` answers `false` without raising, which is the cheaper
way to ask. Reported by the gbasic-books session 2026-10-02, whose filing noted
that the diagnostic used to say `invalid function call: is_money` — a sentence
about the **call**, which sends a reader to check their arguments when the
problem is that the name does not exist.

### Strict Conversion

**THE CONVERSIONS ARE DELIBERATELY STRICTER THAN THE OPERATORS, and the two pages
need reading together.** `1 = true` is **true** and `1 > false` **orders**, because
a boolean compares as 0 or 1 — the one real coercion the comparison chain keeps,
measured at 1,472 of 1,500 mixed-kind comparisons in this tree and argued under
*Expressions*. But `number(true)` and `boolean(1)` both raise
`unsupported type`.

Both choices are defensible and they are not in tension: comparing is answering a
question about two values, while converting is asserting that one value *is*
another kind. Stated here because nothing connected them, so a reader who learned
`1 = true` from one page could not predict what `number(flag)` does from another.
Added 2026-10-02, reported by the gbasic-books session.

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
replace("John Smith", regex("(\\w+) (\\w+)"), "$2, $1")   ' "Smith, John"
```
In a regex replacement, `$1` and `\1` are the text of group 1 (`$0` the whole
match), `${12}` reaches past nine, `${name}` a named group, and `$$` is a
literal `$`. A reference to a group or name the pattern does not have
**raises**, naming `$$` — so `"$5.00"` is refused rather than quietly becoming
`".00"`. A `$` or `\` before anything else is literal.

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

Always available — the engine is **PCRE2**, a required part of the build like
bison, so there is no optional dependency and no `load` *(PCRE2 after 0.10.0;
POSIX ERE before)*. Design: `docs/text_design.md`, `docs/regex_pcre2_design.md`.

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

**Supported syntax.** Perl-compatible, as PCRE2 reads it: `\b` and `\B`, lazy
quantifiers (`*?`, `+?`, `??`), lookahead and lookbehind, backreferences
(`\1`), named groups `(?<name>...)`, inline flags such as `(?i)`, and the
`\d \w \s` classes inside or outside a bracket. **`\d`, `\w`, `\s` and `\b`
are ASCII**: an Arabic-Indic digit is not `\d`. Case-insensitive matching (`"i"`)
folds beyond ASCII (`É` matches `é`).

**The first alternative that matches wins**, not the longest: `a|ab` on `"ab"`
matches `"a"`. Write the longer alternative first when one is a prefix of
another. (POSIX took the longest; that was the one silent change in the move,
and one place in the standard library depended on it.)

**A named group** is also in the match record by name: a pattern with names
gives `m.named`, a record from each name to its text (`unknown` when it did not
take part). `groups` is the positional list either way.

**Backtracking is bounded.** A pattern that backtracks without end on its text
— `(a+)+$` against thirty `a`s and a `b` is 2³⁰ steps — **raises** after ten
million steps instead of hanging.

Matching is **binary-safe**: a NUL is content in the subject and in the
pattern, and a subject that is not valid UTF-8 still matches where it is, never
across the invalid bytes.

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

**`bound(fn, context)`** — a function value that **carries a context**, passed
as the **last** argument on every call. gBASIC has no closures, so a callback
could not otherwise carry state.

```basic
function price(b, ctx)
    return ctx.cost * b / (1 + b)
end function

cheap = bound(price, { cost: 10.00 })
dear  = bound(price, { cost: 22.50 })
```

**The function being called need not know about contexts**, which is the point:
a library that calls `f(x)` works with a bound value unchanged, so no callback
API has to grow a parameter. The context goes last so one function serves both
uses given a literal default — `function f(x, ctx = nothing)`.

**It is not a closure.** A closure captures an *environment*, implicitly and by
reference; `bound` captures **one named value, copied at bind time**. So
mutating the original afterwards is *not* seen by the callback:

```basic
c = { cost: 10.00 }
held = bound(price, c)
c.cost = 99.00                  ' held still uses 10.00
```

That is why it is available where closures are not: a gBASIC record is a value,
so no reference cycle is constructible, and `encode` totality and actor
sendability are untouched.

Re-binding is refused (`bound(bound(f, a), b)`) — two contexts would make a
function's arity depend on how often the value had been bound, which nothing at
the call site shows. A bound value is still `type()` `"function"`, and a record
field holding one works as a method, so two objects can share one function with
different configuration.

**Where a context still cannot travel it is refused, not dropped.**
`webserver.on_request` keeps only the function's *name*, so it raises rather than
silently losing the context. `encode` refuses a function value as it always has;
`serialize` accepts a plain one and refuses a bound one, because the name would
travel and the context would not.

**`key(a, b, …)`** — one string that cannot collide, for use as a **composite
record key**. Variadic, one argument or more; every argument must be a scalar
(an array or record is refused by name).

A record is keyed by a *string*, so a composite key is otherwise built by
concatenation with a chosen separator — and that is wrong in two ways nothing
reports. **A separator inside the data merges two keys**: with `|`, the pairs
`("North|East", "A")` and `("North", "East|A")` produce the same key. **And an
absence renders as a word**, so `nothing` collides with the literal text
`"nothing"` and every absent value keys the same as every other.

```basic
k = key(row.start, row.end, row.period)      ' safe whatever they contain
k = row.start + "|" + row.end                ' merges if either holds a "|"
```

Both of those were live in this tree. In `insight` the separator case merged
two cells of a decomposition, which is worse than a wrong grouping because the
cell count feeds the significance threshold; in `fundamentals` the absence case
grouped correctly *by accident*, every absent value rendering identically.

**The result is opaque and unambiguous, not readable.** Each component is
length- and kind-tagged, so no two distinct inputs can produce one key and the
encoding is not a format to depend on — keep the readable values separately if
you need them. Absences **group together** (as SQL's `GROUP BY` does with
`NULL`) while never equalling a string. Picking a rarer separator is not an
alternative: it is the same defect with a longer fuse.

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
- `encode(value)` - gBASIC's **JSON-like dialect** for structured data (see below).
  **It is not JSON whenever a value is `nothing` or `unknown`**: it writes
  them as those bare words, which no JSON parser accepts. For anything another
  program reads, use `json_encode`, which writes `null` and refuses what JSON
  cannot hold.
- `json_decode(text)` (*since 0.11.0*) - **RFC 8259 and nothing else**, nested:
  `null` is `nothing`, and gBASIC's own notation (`{kind: "repair"}`,
  `nothing`, `unknown`, `inf`), a key written twice, text after the value and a
  NUL are refused, naming the byte. The reader for untrusted text that must be
  JSON, such as a model's reply; `decode` reads the dialect as well.
- `decode(text)` - parses JSON *and* the dialect back into values; **raises** on
  malformed input. An integer that fits 64 bits comes back **exact**
  *(0.8.1)*, so `decode(encode(x))` is `x`; before that `encode` wrote every
  digit and `decode` read a double, and the round trip lost the value above
  2^53. A fraction and an exponent stay inexact, by the same rules
  `number("…")` follows. **A key written twice in one object is refused**
  *(2026-11-04)*, at the second key's position (`{"a": 1, "a": 2}` is an error
  at byte 9). It used to be kept twice, `keys()` listing it twice and `.a`
  reading the first, where most JSON readers keep the last: one file meant
  different things to different programs. RFC 8259 leaves it open, so refusing
  is the answer that cannot disagree with anyone. Keys that differ only after
  an escaped NUL are different keys
- `try_decode(text)` - the same parse, reporting failure as a **value** instead of
  raising (see below)
- `json_encode(value)` - **standards-compliant JSON (RFC 8259)** for external
  interchange: HTTP APIs, LLM providers, anything outside gBASIC
- `json_encodable(value)` - preflight predicate: would `json_encode` succeed?
- `quote(value)` - gBASIC source code literal with escaping
- `serialize(value)` - exact binary round-trip serialization (see below).
  Every type survives and, since version 3, so does every digit: a `number`
  records whether it is exact, so `deserialize(serialize(9007199254740993))`
  is that integer and not 2^53. **The bytes are the same on every machine**
  (little-endian fixed fields, LEB128 lengths), so a payload written on one
  machine reads on another; versions 1 and 2 used the writing machine's byte
  order. The layout is specified in `docs/gsql_storage_design.md` §3.3, because
  G-SQL's rows and client protocol use the same encoding
- `deserialize(bytes)` - reconstruct a value from `serialize` output. It reads
  versions 1, 2 and 3, since older payloads are already in files. A truncated,
  overlong or otherwise malformed payload raises *corrupt or truncated
  serialized data*; it never answers a partial value

### Four serializers, four jobs

| Want | Use | Notes |
| --- | --- | --- |
| Send data to a non-gBASIC consumer | `json_encode` | Strict RFC 8259; refuses what JSON can't express |
| Human-readable gBASIC round-trip | `encode` / `decode` | Dialect: `nothing`/`unknown` survive; **refuses a date, money, a duration or a file** |
| Exact typed/binary round-trip | `serialize` / `deserialize` | Dates, money, files, binary content — **opaque binary** |
| **Human-readable AND typed** | `notation.to_text` / `notation.from_text` | A stdlib library, not a builtin. Open it in an editor, read it, edit it — and a date is still a date |

**`notation` fills the gap the first three leave.** `encode` is readable and
refuses the typed values a business record is mostly made of; `serialize` keeps
every type and cannot be read. Neither can be reviewed by eye:

```basic
load notation

write(f, notation.to_text(invoice))
invoice = notation.from_text(read(f))
```

```
{
  id: 4471,
  issued {date}: "2026-03-15",
  customer: { name: "Ada Lovelace & Co.", account: "ACME-0042" },
  prices {USD}: [ "19.99", "3.459", {JPY}: "500" ],
  total {USD}: "47.06",
  terms {duration}: "30 days",
  paid_at: nothing
}
```

**The type tag goes on the key side**, which is the language's own typed
assignment with a colon where the equals goes — `issued {date}= "…"` is gBASIC,
`issued {date}: "…"` is the notation. A tag on an **array** is a **default its
elements may override**; it cascades into nested arrays and stops at a record,
because a record's fields are named and each can tag itself.

**The document root is a record or an array.** A bare typed value there has no
field name to hang a tag on, so it is refused rather than given a spelling
nobody needs.

`notation.try_from_text(text)` answers `{ok, value, message}` instead of
raising — the shape `try_decode` already takes for JSON, and for the same
reason: a program reading a file a person has just edited needs the failure as
a value.

**Version 1 discards comments.** `'` to end of line is read and dropped, so a
read-modify-write does not preserve a reviewer's note. Deliberate: preserving
them is an API fork rather than a detail, and the ordinary loop — generate,
review, read back — loses nothing.

**The output is not gBASIC source**, and could not be: gBASIC refuses `\u{0}`
in a literal while a record field name may contain an interior NUL, so this
notation allows that one escape and can spell every string gBASIC can hold.
Every control byte is escaped, because a raw one round-trips perfectly and
makes the file hostile to the editor the format exists to be opened in.

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

**`try_decode(text)` — decode that reports failure as a value.** `decode` raises
on malformed input. That raise *is* catchable — see **Errors** — but a raise
tells you only that the JSON was bad; `try_decode` also tells you **where**,
which is what a program reading a file it did not write usually needs to report.
It answers with a record instead:

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

**Nesting is bounded, and the bound is a function of the stack** *(0.8.1)*. Both
entry points refuse documents nested deeper than **10 000 levels, or `ulimit -s`
× 2, whichever is smaller** — `decode` raises, `try_decode` reports. At the 8 MB
default that is 10 000, so the limit a program sees is unchanged; below about
2.5 MB it is lower. Before any limit existed the parser's recursion overran the C
stack and segfaulted; a non-raising decode whose failure mode is a crash would be
worthless.

**A value's own depth has a separate, larger limit, and it is the stack's too.**
Releasing a value recurses the same way building it does — roughly three C frames
per level — so a value nested past about **43,000 levels** segfaults *when it is
released*, which is the line that lets it go rather than the line that built it.
Measured at a pinned 8 MB stack, where records and arrays behave identically; at
64 MB a 60,000-deep value releases cleanly, so the number is a fact about
`ulimit -s` and not about gBASIC. Nothing a program *reads* can reach it — the
decode bound above, `serialize`/`deserialize` at 256 levels, `rules` at 100 — so
it takes a construction loop of your own (`v = [v]`, tens of thousands of times).
It is an accepted limitation, listed in `DOGFOOD.md` and checked by
`tests/run_limitations.sh`; the remedy if you need the depth is a larger
`ulimit -s`.

**Why the bound is derived rather than a constant** is a correction worth knowing
if you deploy on a small stack. It *was* a flat 10 000, justified as sitting far
enough below the crash to "hold on a stack a quarter the size" — and measured, it
did not: the margin was counted in *levels* while the thing conserved is *stack*,
and the two are linear at roughly 240 bytes per level. On a 2 MB stack the
deepest document that parses is about 8 500, so a 9 999-deep one crashed the
interpreter *before the limit could refuse it*. That is reachable from the
network, because a request body is decoded as the request record is built,
whether or not the handler reads `req.json`. A 2 MB `ulimit -s` is ordinary in
containers and on hardened hosts.

**Why it matters for performance.** Pre-validating in gBASIC was measured at
16 KB: 1 s; 64 KB: 16 s; 128 KB: 69 s; 256 KB: 291 s — plainly quadratic, and the
C parser handles all of those in well under a second (`tests/run_try_decode.sh`
records those figures as the reason this builtin exists). `try_decode` replaces a
scan that got dramatically worse with size.

**The REASON given here used to be wrong, and the wrong reason was the harmful
part** (corrected 2026-10-02, reported by the gbasic-books session). This
paragraph said `mid(s, i, 1)` is O(i) and therefore a per-character scan is O(n²).
**PLAT-STRIDX removed both costs** and nobody updated this page, so it went on
telling readers to avoid the thing that had become fast — and a reader who
believes `mid` is O(i) rewrites a linear loop into something worse.

Measured on the current binary: **`mid` is flat in the index** — 20 000 calls at
the far end of a 400 000-codepoint multibyte string cost 0.0218 s against 0.0201 s
for the same calls on a 100 000-codepoint string, a ratio of 1.09 where O(i) would
be about 4. And **a per-character scan is linear**: 50k/100k/200k/400k codepoints
at 0.058/0.114/0.229/0.461 s.

So the quadratic cost those timings record is **not** in `mid`. The likeliest
cause is accumulation with `+` inside the scan, which is separately measured and
genuinely quadratic — see *Performance traps*. The advice is unchanged either
way: use `try_decode` rather than pre-validating in gBASIC.

Asserted by `tests/run_stridx.sh`, whose SHAPE tier holds seven access patterns
to a ratio across a 4x size step rather than to an absolute time, and whose
CONTROL points the same harness at string concatenation and requires it to
EXCEED the gate — so if these costs ever become quadratic again the suite goes
red instead of this page going quietly stale, which is what happened the last
time.

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

Live database connections and open workbooks cannot be serialized (a structured
error is raised; for a workbook, send its path or the values read from it), and
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
- `last(array)` — the last element, or `nothing` *(since 0.6.0)*. `first` had
  existed since arrays did and this had not, so reading the end meant
  `a[count(a) - 1]` — arithmetic on a length, and an out-of-range read rather
  than an answer on an empty list. `take_last` **pops**; `last` only reads.
- `rest(array)` — a new array without the first element.
- `slice(array, at [, count])` — a new array of `count` elements from `at`, to
  the end if no count is given *(since 0.6.0)*. The same shape and the same
  **0-based** `at` as `byte_slice(s, at [, count])`, deliberately: a second
  convention for the same idea one type along is how an off-by-one gets written.
  **Clamped, not refused** — a window legitimately runs past the end, which is
  what the last page of a report is — while a negative `at` or `count` *is*
  refused, since it means a computed index is wrong and there is no
  from-the-end convention to read it as.

Set operations *(since 0.6.0)* — infix word operators, not functions:

- `a excluding b` — the elements of `a` not present in `b`.
- `a intersecting b` — the elements of `a` also present in `b`.

Both are **filters over the left side**, so its order and its duplicates survive;
`unique` exists separately and composes, so folding it in would take the choice
away. Membership is the same question `contains`, `find` and `remove_value` ask,
through the same code, so records compare **by value** as PLAT-EQ made them and
`unknown` and `nothing` are ordinary values to match. Two lists are required:
text is refused rather than treated as a list of characters, because `contains`
on a string asks a different question with the same shape and guessing which was
meant is how a set operation silently becomes a substring search.

They bind **tighter than comparison and looser than arithmetic**, so
`a excluding b = c` is `(a excluding b) = c` and `a excluding [1 + 1]` excludes
`2`. The cost is the **product of the two lengths** — each element of the left is
compared against the right until a match — because membership is `=`, which for
records is deep equality, so there is no key to hash and no order to
binary-search; `unique` pays the same price for the same reason. Measured as a
ratio by `tests/run_set_ops.sh`, which is bounded both ways, since a cost that
quietly got cheaper would leave this paragraph wrong.

Mutation:

- `append(array, value)` / `prepend(array, value)`
- `insert(array, index, value)` / `remove(array, index)`
- `take_first(array)` / `take_last(array)`
- `reverse(array)` / `unique(array)` / `sort(array [, options])`

When `append`, `prepend`, `insert`, `remove`, `remove_value`, `take_first`,
`take_last`, `reverse`, `sort`, or `unique` mutates a stored array through an
assignable path, matching watchers are notified once after the completed
mutation. Mutators that leave the stored array unchanged, such as no-match
`remove_value`, already-sorted `sort`, already-unique `unique`, or a no-op
`reverse`, do not notify watchers.

`sort()` and `unique()` support scalar arrays. Date/time values use exact
date/time equality and ordering, not same-day or same-month comparison.

`sort` orders two **exact** integers that round to the same double *(0.8.1)*.
Before that its comparator compared doubles, so `sort([9007199254740993,
9007199254740992])` left the array as it found it — the pair tied and the sort is
stable — while `a > b` on the same two values answered correctly. Both routes go
through one comparison now.

**NaN has one place, after `+inf` and before any absence** *(after 0.10.0)*, and
`min`, `max`, `median` and the percentile family use the same order: so
`sort([nan, 1, nan, 0])` is `[0, 1, nan, nan]`, `min([nan, 1])` is `1` and
`max([nan, 1])` is `nan`. Before that a NaN stopped the *other* elements sorting,
because a comparator answering "tie" for every pair touching NaN is not an order.

**Two equalities.** `contains`, `find`, `remove_value`, `unique`, the deep
comparison of an array or a record, and a watcher's change test ask whether two
values are **the same value**, SQL's "not distinct". That is the `=` operator's
answer for every pair but one: NaN is the same value as NaN there, while
`nan = nan` stays `false` and `nan < 1` stays `false`, IEEE's answer. So
`contains([nan], nan)` and `[nan] = [nan]` are `true`. The deep comparison is
also **exact** *(after 0.10.0)*: `[9007199254740993] = [9007199254740992]` is
`false`, as the scalar comparison always said (`docs/unknown_nan_design.md`).

#### Sorting records — `sort(rows, { by: … })`

*(since 0.6.0)* `sort` takes an optional second argument, an options record:

```basic
sort(rows, { by: "amount" })                          ' one field
sort(rows, { by: "amount", descending: true })        ' reversed
sort(rows, { by: ["last", "first"] })                 ' several fields, in order
sort(rows, { by: ["score", "id"], descending: ["score"] })   ' mixed direction
sort(xs, { descending: true })                        ' a plain list, reversed
```

- **`by`** names a field, or a list of fields compared in order. Every element
  must then be a record, and each key column goes through the **same ordering
  gate** the array itself goes through — so a key mixing two ordinary types, or
  money in two currencies, is refused in the gate's own words, naming the
  column. An element lacking the field sorts as an absence.
- **`descending`** is `true`/`false` for every key, **or a list of the field
  names that descend**. Naming a field `by` does not sort on is refused rather
  than ignored, that being the one mistake this shape makes easy. With no `by`,
  `descending: true` reverses a plain list — which previously had to be written
  `reverse(sort(xs))`, two passes to say one thing.
- An unknown option is refused **by name**, and the options are read **before
  the array is touched**, so a misspelled option cannot leave a half-sorted
  array behind.
- `sort` still **mutates in place** through an assignable path and answers the
  sorted array, exactly as the one-argument form does.

**The sort is stable**, and that is a portability requirement rather than a
nicety: `qsort` is not stable and its tie order differs between implementations,
so a golden pinning rows sorted by one field would read differently on glibc and
on a BSD libc. Stability also makes the one thing `descending` cannot express
affordable — sorting by the minor key and then by the major key gives mixed
directions in two declared passes, and `tests/run_sort_records.sh` asserts that
the two routes give the identical answer.

**There is no comparator-function form, and that is a decision rather than a
gap.** Fields, several fields and per-field direction cover every record sort
measured in this tree; a comparator is the one piece of code everyone gets wrong
(`a - b` against `a < b`, and returning a boolean silently half-sorts); and it
would cost an interpreted call at every comparison the merge makes, in a
tree-walking interpreter. (No complexity is cited here because there is nothing to
measure: the form does not exist.) A declared collation (`using: {trimmed; caseless}`) is likewise not
here: of 73 `sort` call sites in this tree none orders a lowered, trimmed or
naturally-collated key, and a comparison lens is a grammar construct rather than
a value, so nothing can pass one as an argument. If a real case appears that
`by` cannot express, that case is the argument.

**An absence sorts LAST**, and the two absences are **not ordered against each
other** *(ruled 2026-10-07)*. There is no universal convention to appeal to —
`ORDER BY x ASC` puts NULLs last in PostgreSQL and Oracle and first in MySQL and
SQLite — so this was a choice gBASIC had to make, and `frame.sort_by` had already
made it. Core `sort` ranked them first until 0.6.0, which is the disagreement that
suite found and pinned; `frame.sort_by` is now built on core `sort`, and the two
cannot drift again. Two absences compare **equal**, so with a stable sort they
keep the order they arrived in rather than being given an invented one. Under
`descending: true` they come first, which is the consequence of reversing the
comparison rather than a second rule — and is what PostgreSQL does too.

**What has an order is taken from the `<` operator** *(since 0.6.0)*, not
decided separately: numbers, strings, booleans, date/times, **money** and
**durations**. Before 0.6.0 the two disagreed on the last two — `a.total <
b.total` answered while `sort(invoices.total)` refused — so a sorter that
ordered what `<` refuses would be inventing an order, and one that refused what
`<` answers would be a second, narrower rule nobody had written down.

Two orders are **conditional**, and each is refused in the same words the
operator uses, because a comparison function cannot raise:

- money in **different currencies** — ordering it would invent an exchange rate;
- a duration carrying **months or years** — a month has no fixed length.

Records, arrays and other non-scalars are refused, and so is an array mixing two
ordinary types. An **absence** is the one deliberate divergence: `unknown < 1`
has no answer and raises, while `sort` has to put it somewhere and ranks
`nothing` then `unknown` below every ordinary value. Asserted in
`tests/run_projection.sh`, whose second tier uses the operator as the oracle.

Aggregates:

- `len(value)` / `count(value)` — length of an array, string or record.
- `sum(array)` / `mean(array)` — over numbers, or over **money** *(since
  0.6.0)*, where the fold refuses mixed currencies in the same words as `+` and
  is overflow-checked the same way. Over numbers `sum` is the fold of `+` and
  agrees with it about exactness *(0.8.1)*; `mean` divides, and so does not. `mean` keeps the result below the minor unit
  in money's guard digits, so the average of 32.50, 7.00 and 19.95 displays as
  `19.82` and multiplies back by 3 to exactly `59.45`.
- `median(array)` — the statistical convention on an even count: the mean of the
  two middle values, so `median([4,1,2,3])` is `2.5`. An **odd** count returns
  one of your own values and so keeps its exactness; an even one divides and does
  not.
- `mode(array)` — **every tied value, as a list**, or `unknown` when nothing
  repeats *(changed in 0.6.0; see below)*.
- The *Statistics* summaries below stay **numeric**. A variance of money is money
  squared, and the rest were not among the aggregates the loop survey measured;
  see §4 of `docs/bulk_data_design.md`.
- `min(array)` / `max(array)` — extremes, over **anything `sort` orders**
  *(since 0.6.0)*, so text, dates, money and durations as well as numbers. They
  return **the element**, not a recomputed one, so an exact integer comes back as
  you wrote it *(0.8.1; before that the numeric path recomputed a double and the
  text path did not, which is what made it a defect rather than a decision)*. An
  array containing an absence is still refused, consistently with `sum`, until
  the aggregate absence rules land.
- `any(array)` / `all(array)` — over **booleans**. `any([])` is `false` and
  `all([])` is `true`, the usual vacuous-truth convention. An **absence is not
  true**: a row that never said it was paid has not been paid, so
  `all(rows.paid)` is `false` when a row lacks the field.

#### An aggregate skips an absence, and says it did

*(since 0.6.0)* A list with a hole in it used to make every aggregate refuse
(`sum expects a numeric array`), which made projection useless on the first
ragged row — and real business data is ragged. The rule is **SQL's, with SQL's
warning**:

```basic
sum([10, unknown, 7])      ' 17        -- and warns, code 2111
mean([10, unknown, 7])     ' 8.5       -- 17 / 2, the PRESENT count
max([10, unknown, 7])      ' 10
count([10, unknown, 7])    ' 3         -- COUNT(*): rows, not values
count(present([10, unknown, 7]))  ' 2  -- COUNT(amount)
sum([unknown, nothing])    ' unknown   -- not 0
sum([])                    ' RAISES    -- an empty list is a mistake
```

- **`mean` divides by the number present**, not the length. Skipping the
  absences and dividing by the full count makes every average quietly too low.
- **All-absent answers `unknown`, not `0`.** "There was nothing to add" is not
  "the total is zero". `sum([])` still *raises*, and the asymmetry is deliberate:
  an empty list is a programming mistake, all-absent is a fact about the data.
- **`nothing` and `unknown` are treated alike here.** `unknown` is the close
  match for SQL's `NULL` and `nothing` is the program's own data saying there is
  no value, but the distinction does not help an aggregate and two rules would be
  a trap people hit once a year. (`=` still tells them apart, so `unique` keeps
  them as two values.)
- **`present(array)`** — the list without its absences. It gives SQL's second
  count a spelling of its own instead of silently moving `count`, and it is the
  warning's **opt-out**: an aggregate over `present(x)` has nothing to skip, so
  it is silent because there is nothing to say. That matters because
  `on warning ignore` is the only other way to quiet it and it covers every
  warning in the frame.
- **The warning is the point.** Skipping *silently* is the dangerous thing;
  skipping with a warning is not silent, and `on warning stop` turns it into a
  failure for a test run while production keeps the ergonomics. The SQL standard
  raises exactly this as SQLSTATE `01003`, *"null value eliminated in set
  function"* — class `01` being the warning class. Some engines emit it;
  PostgreSQL does not.

#### `mode` answers a list

*(changed in 0.6.0)* `mode` used to return **one** value, chosen by source order,
and that was wrong three ways at once — measured:

| | answered | now |
|---|---|---|
| `mode([1,1,2,2])` | `1` | `[1, 2]` |
| `mode([2,2,1,1])` | `2` | `[1, 2]` |
| `mode([1,2,3])` | `1` | `unknown` |
| `mode([19.95, 32.50, 7.00])` | `19.95` | `unknown` |

The last row is the sharp one: **on continuous data — money, measurements, any
real price list — every value is unique, so `mode` returned the first element and
always looked like an answer**, with nothing downstream able to tell it from a
real mode. SQL:2003's `MODE()` is implementation-defined on ties; this is the
higher bar.

So it answers **every tied value, as a list** — the only aggregate that answers a
list, because a caller cannot otherwise tell a tie from a single mode, and
returning one of several because it came first is the thing being stopped.
`first(mode(x))` is the explicit "give me one". Nothing repeating means
**`unknown`**, not the first element. **Text is accepted**, which is the one
aggregate where text is the *common* case (the most frequent category, city or
status code); equality is `=`, so an identical record counts as a repeat. The tie
list is **sorted** where its values are orderable, so the same multiset gives the
same answer whatever order it arrived in; where they are not (records, arrays) it
is in first-seen order, since inventing an order over records is what `sort`
itself refuses to do.

#### Projection — one field across every row

*(since 0.6.0)* A dotted access on an **array of records** answers the array of
that field from every element:

```basic
rows = [ { item: "hammer", amount: 19.95, paid: true },
         { item: "saw",    amount: 32.50, paid: false },
         { item: "nails",  amount:  7.00, paid: true } ]

print sum(rows.amount)      ' 59.45
print max(rows.amount)      ' 32.5
print rows.item             ' [hammer, saw, nails]
print any(rows.paid)        ' true
print all(rows.paid)        ' false
```

This is the smallest thing that makes the aggregates reach business data.
Measured across this tree before it was built: of 813 `for each` loops, 22 are a
sum over the loop variable and **every one** of the 22 is
`total = total + r.amount` — summing a *field*. `sum`, `mean`, `count`, `min`
and `max` all existed already and simply could not be pointed at data, because
nothing in the language could name a field across a list.

- **A missing field is `unknown` for that element**, and the element is **kept**
  — so the projection is always the same length as the array. Raising instead
  would make a projection unusable on real business data, where a row
  legitimately lacks a value; what an aggregate then does with the absence is
  the aggregate's rule.

  **This is not what a scalar read does, and the difference is deliberate.**
  A scalar `r.amont` *raises* `unknown record field` — naming one field of one
  record is a specific claim about a specific record, so being wrong about it
  is a mistake, where reading one field across a thousand rows is bulk access
  over data that legitimately has holes. (This bullet claimed the two matched
  until 2026-10-18, when the claim was measured while building Qt form
  binding, which had copied it.)
- **A non-record element raises and names the index** — that is a shape error
  rather than missing data, so a list accidentally built from numbers says which
  element broke it.
- **Nesting falls out of applying the rule twice**: `rows.customer.name`.
- **A projection is a value, not a place.** It is computed from every element, so
  there is nothing to store into: `append`, `prepend`, `insert`, `remove` and
  `remove_value` refuse it and name both remedies — change the array itself, or
  assign the projection to a name first. `sort`, `reverse` and `unique` *do*
  work on one, answering a new array rather than mutating anything.
- It is a **copy**: writing to the projection leaves the rows alone, and writing
  to a row leaves an already-taken projection alone.

A typo yields an array of absences rather than an error, which is the cost of
matching record behaviour. The mitigation is the aggregate absence rules rather
than a different syntax.

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

Asserted by `tests/run_arridx.sh` — SHAPE checks seven patterns as a ratio
across a 4x step, and its CONTROL requires still-quadratic string concatenation
to exceed the same gate, so a tier that stopped measuring anything would be
visible.

`append`/`prepend`/`insert`/`remove`/etc. still mutate a stored array in place
when given an assignable path (and notify watchers, as above); the value they
return is the resulting array. Taking a copy first (`b = a`) and then appending
to `a` leaves `b` unchanged, as value semantics require.

### Packing integers into bytes

*(added 2026-11-04; [networking_design.md](networking_design.md) §8)* Binary
protocols and file formats are integers at fixed widths in a fixed byte order.

```basic
frame = pack(">B I", [81, 9]) + "select 1"
fields = unpack(">B I", frame)        ' [81, 9]
fields = unpack(">I", frame, 1)       ' [9], from byte offset 1
size = pack_size(">B I")              ' 5
```

- **Format letters:** `B b` (8-bit unsigned and signed), `H h` (16), `I i`
  (32), `Q q` (64, exact), `Ns` (a string of exactly N bytes), `x` (a zero pad
  byte). A count repeats a letter: `3B` is three bytes.
- **The byte order has no default:** begin with `>` (big-endian, network
  order) or `<` (little-endian), and it may change mid-format. A format
  without one is refused, because a default order is a silent bug in half the
  protocols.
- **A value that does not fit its field is refused**, never wrapped: 300 in a
  `B`, -1 in an `H`. An `s` value must be exactly its length.
- `pack(format, values)` answers the bytes; `pack_size(format)` how many.
- `unpack(format, bytes [, offset])` reads from a 0-based byte offset and
  refuses when too few bytes remain. A `Q` above 2^63-1 cannot be held exactly
  and is refused; read it as two `I` fields.

### Bitwise

Bitwise functions operate on 32-bit unsigned integers and raise on non-integer
or out-of-range input:

- `band(a, b)` / `bor(a, b)` / `bxor(a, b)` — bitwise AND / OR / XOR.
- `bnot(a)` — bitwise NOT (32-bit complement).
- `shl(value, count)` / `shr(value, count)` — logical left / right shift.
- `rotl(value, count)` / `rotr(value, count)` — 32-bit rotate left / right.

### Numbers and Comparison

#### Exact integers

*(since 0.6.0)* A `number` is **exact** when it is an integer the runtime holds
exactly, and **inexact** otherwise. There is still only one numeric kind —
`type()` answers `number` either way — and this is Scheme's exactness model and
Lua 5.3's rather than an invention.

```basic
print 9007199254740992 + 1                      ' 9007199254740993
print 9007199254740993 = 9007199254740992.0     ' false
print 0.1 + 0.2                                 ' 0.3, an exact decimal (below)
print 6 / 3                                     ' 2, inexact
```

Before this, `number` was a double and integer arithmetic went silently wrong
above 2^53 — `pow(2, 53) + 1` answered `9007199254740992` with no diagnostic. The
inconsistency that made it a defect is internal: `BIGINT` and `DECIMAL` columns
come back from `odbc` and `pg` as **strings** precisely so their digits are not
lost, so gBASIC refused to lose precision crossing a driver and lost it in its own
arithmetic.

**Where exactness comes from:** a **decimal or hexadecimal integer literal** whose
value fits a 64-bit integer; *(since 0.7.1)* **`number("…")` and `{number}`
on decimal integer text** that fits one; and *(since 0.8.1)* the **counting
builtins** — `len`, `count`, `byte_count`, a found index from `find`, and
`epoch`, `file_size`, and an error's or warning's `code`, `line` and `column` (and a trace frame's) — whose answers are integers by nature, plus **`decode`, `try_decode`
and a JSON HTTP body**, so `decode(encode(x))` is `x`. That round trip did not
hold before 0.8.1: `encode` wrote all the digits and `decode` read back a double,
so a value could be written to a file and could not be read from it.

**How it travels, and the rule is one sentence** *(completed in 0.8.1)*: a
function that **picks** a value preserves its exactness, and one that
**calculates** is exact when both operands are exact and the answer is an integer
the runtime holds. An inexact operand yields an exact answer only from the three
functions whose answer is whole: `floor`, `ceil`, and `round` to no places, which
are exact up to 2⁵³ *(2026-11-09)*.

| | |
|---|---|
| picks | `min`, `max`, `median` of an odd count, `percentile`/`quantile` landing on a data point, and `sort`, which now **orders** two exact integers that round to the same double |
| calculates, exactly | `+` `-` `*`, unary minus, `sum` (the fold of `+`), `pow` with a non-negative exponent, `mod` |
| the identity on an integer | `abs`, `floor`, `ceil`, `round`, `sign` |
| whole, so exact from any number | `floor`, `ceil`, `round` to no places (to 2⁵³) |
| never exact | `/`, `mean`, an even `median`, an interpolated `percentile` — all of them divide — and the roots, logarithms and transcendentals |

Text is judged the way a literal is, with one difference worth knowing: only
**decimal** text becomes exact, so `number("0x10")` is inexact although the
literal `0x10` is exact. A hex literal is a token the lexer can inspect; hex
reaching `number()` relies on a C library quirk that nothing here documents.

**What is inexact, each by decision rather than omission:**

- **a literal in exponent notation** — `1e16`
  is integral and exactly representable, and the notation still says
  *approximately this magnitude*; `number("10000000000000000")` asks for the
  integer. The same reading applies to *text*, so `number("1e16")` is inexact
  too: the notation means what it means whichever way the value arrives;
- any **mixed** expression, because `exact + inexact` has no exact answer and
  promoting the inexact side would invent precision it never had. This is also
  why there is no promotion matrix to get wrong;
- **anything that divides**, since integer division is not integer-valued and a
  rational kind is a different language: `/` itself — even `6 / 3` — and with it
  `mean`, the **even** case of `median` (which is the mean of the two middle
  values), and a `percentile` landing *between* two data points. So
  `mean([x, x])` is inexact where `sum([x, x])` is exact, which is the division
  rule rather than an oversight;
- **roots, logarithms and the transcendentals** — `sqrt(4)` is `2` and detecting
  a perfect square is a different function, and `pow(x, 0.5)` is already a root.

`pow` and `mod` were a later increment and arrived in **0.8.1**, so the design's
own headline example, `pow(2, 53) + 1`, answers `9007199254740993` from that
release and answered `9007199254740992` before it. Measured in 0.8.0, with
`b = 9007199254740993`: `min`, `max`, `sum`, `median`, `abs`, `floor`, `ceil`,
`round` and `pow` each dropped the last digit, and **`mod(b, 2)` answered `0`** —
not lost precision but a wrong answer, the exact odd value having become an even
double before `mod` saw it. `sort([b, b - 1])` did not sort at all, the comparator
having compared doubles so that the two tied.

**Comparison is mathematical, not by rounding.**
`9007199254740993 = 9007199254740992.0` is `false`. The obvious implementation
converts the exact side to a double first and answers `true`, reintroducing the
defect inside the comparison; the double is moved into the integer domain
instead. A boolean is never exact, so `0 = false` keeps its own path unchanged.

**Overflow degrades rather than refusing**, with warning **2112**. The answer is
the one the program used to get — refusing would stop a program that answers today
— and what is new is that the loss is loud. `on warning stop` turns it into a
failure for a test run.

Memory cost is zero and `tests/run_exact.sh` asserts it: the numeric member is 24
bytes and `DateTime` in the same union is 32, so `sizeof(Value)` does not move —
which matters because a `Value` is every array element and every record field.

#### Exact decimals

*(Unreleased; `docs/exact_decimal_design.md`)* A number **written or read with
a decimal point** is exact too: its digits, held as an integer and a count of
places (up to 18), where it used to be the nearest double.

```basic
print 6331.475 - 2182.855                       ' 4148.62 (was 4148.620000000001)
print 0.1 + 0.2 = 0.3                           ' true (was false)
print number("412.34") - number("412.3350")     ' 0.005
print sqrt(0.01) = 0.1                          ' true, still: see comparison
```

- **Where it comes from:** a literal with a point and no exponent (`0.1`,
  `6331.475`, `2.0`), and decimal text through `number("…")`, `{number}`, a
  typed `x:number` lock, and `decode`/`json_decode`. That text door is where a
  `pg` or `odbc` `numeric` arrives, so `number(row.qty)` is exact now.
- **`+`, `-` and `*` stay exact**, and so do `sum`, `abs`, `round` (in the
  digits, half away from zero: `round(2.675, 2)` is `2.68`, where the double
  rounded down), `floor`, `ceil`, `pow` to a whole power (`pow(1.05, 3)` is
  `1.157625`), the picks (`min`, `max`, `sort`) and a `for` counter with a
  decimal step: `for x = 0 to 1 step 0.1` ends on exactly `1`.
- **Division and functions are the double**, as `6 / 3` is for integers.
- **A decimal never becomes the exact integer by itself.** `2.0` and `1.5 * 2`
  are exact and print as `2` and `3`, and they equal `2` and `3`, but every
  path that asks for an *integer* — integer overflow, a typed integer column —
  treats them as it treated `2.0` before. That keeps `x * 1.0` from changing
  what those paths answer. To ask for a double, write an exponent: `x * 1e0`.
- **Comparison:** exact against exact (integer or decimal) is mathematical, so
  `0.1 < 0.10000000000000001` is `true`. **Exact against inexact compares as
  doubles**, so `sqrt(0.01) = 0.1` stays `true`: the root is the nearest double
  to 0.1, and comparing it exactly against an exact 0.1 would change every
  computed-against-literal test a program makes.
- **When the digits do not fit** (past 18 places, or past an `int64` of
  digits) the answer is the double it always was. That is **silent** when only
  far fractional digits go — a compounding product needs more places with
  every step — and warns **2112** when the value is at 2^53 or past it, where
  whole digits can go too, as integer overflow does.
- **Printing** shows the digits (`4148.62`); a decimal and the double it used to
  be print alike up to 15 significant digits.
- **Sent to a database as its digits** *(increment 3)*: a decimal or an exact
  integer **parameter** reaches `pg` as its own digits (it was `%.17g` of the
  double, so `0.1` arrived in a `numeric` as `0.10000000000000001`), `odbc`
  declares a decimal `SQL_DECIMAL` with its scale as it does money, and
  `sqlite` binds an exact integer as an integer (it bound a double, so 2^53 + 1
  was stored as 2^53). A decimal into SQLite is a REAL, as SQLite has no
  decimal; pass `string(x)` to a TEXT column to keep the digits. Reading is
  unchanged: `numeric` and `DECIMAL` still arrive as exact text, and
  `number(row.qty)` is exact.
- **Saved and stored as its digits** *(increment 2)*: `serialize` and the actor
  wire carry a decimal exactly (exactness byte 2, its scale and mantissa), a
  G-SQL `number` column holds one, SQL literals with a point are decimals, and
  `dump` writes a decimal as its digits and a double with an exponent
  (`1e-1`), so a restore gives back each as it was. An integer column still
  refuses `2.0`. A G-SQL **index** keys a decimal by its nearest double, so
  two decimals that differ only past 17 digits share a key: answers through
  the index stay right, but a UNIQUE index refuses the pair (DOGFOOD live row
  9). `-0.0` stays a double so its sign survives.

**Number literals.** Decimal (`42`, `3.14`), hexadecimal (`0xFF`), and
**scientific notation** *(since 0.2.3)* — `1e20`, `6.02e23`, `1.5e-3`, `2E10`,
with an optional sign on the exponent. At least one digit must follow the `e`,
so `1e` is the number 1 beside an identifier `e` and not an exponent.

Before 0.2.3 there was no exponent literal, and the failure was worse than a
missing feature: `1e20` lexed as the number 1 beside the *duration unit* `e20`,
and an unknown unit printed an unlocated line, answered `0 seconds` and exited
0. An unknown duration unit is a located parse error now, naming the seven
units that exist. `number("1e20")` still builds a value from text.

- `mod(a, b)` — the remainder, **floored**: the result takes the sign of the
  DIVISOR, so `mod(-7, 3)` is `2` and `mod(7, -3)` is `-2`. This differs from
  QBasic's `MOD`, which truncates toward zero, and the divergence is
  deliberate: gBASIC had no modulo, so the documented workaround was
  `a - floor(a/b)*b` — floored — and the libraries written against that advice
  depend on it (`stdlib/forensics.bas`'s civil-date algorithm is correct for
  negative years only under floored semantics). Two exact operands give an exact
  remainder *(0.8.1; before that `mod(9007199254740993, 2)` answered `0`, the
  exact odd value having become an even double first)*. `mod(a, 0)` raises. There is no
  infix `%`; and `7 mod 2` is duration syntax, not modulo — `mod` is a call.
- `round(number [, places])` — round to a number of decimal places. `places`
  is optional and defaults to 0, so `round(3.7)` is `4`. Rounding an exact
  integer to any number of decimal places is that integer *(0.8.1)*. Halves round away from
  zero: `round(2.5)` is `3` and `round(-2.5)` is `-3`. To no places the answer is
  an exact integer, as `floor`'s and `ceil`'s always are, up to 2⁵³, beyond which a
  double is not every integer *(2026-11-09)*. So `round(x)` is the way to make a
  whole inexact number exact, as a G-SQL `integer` column requires.
- `compare(a, operator, b)` — compare two values using an operator named as a
  string (the general form behind the comparison operators and modifiers).

Rounding and sign:

- Each of these is the **identity** on an exact integer *(0.8.1)* — an exact
  integer is already its own floor — so the digits survive; on an inexact one they
  answer as they always did. `abs` of int64's minimum has no exact magnitude,
  int64 being asymmetric, so that one value degrades and warns (2112).
- `abs(x)` — magnitude. `floor(x)` — largest integer **not greater** than `x`,
  so `floor(-2.1)` is `-3`. `ceil(x)` — smallest integer not less than `x`, so
  `ceil(-2.1)` is `-2`. Note `floor` rounds toward negative infinity while
  `int`-style truncation would round toward zero; they differ on negatives.
- `sign(x)` — `-1`, `0` or `1`.

**WHAT RAISES AND WHAT REACHES `inf` IS A LINE WORTH KNOWING, because the two
answers look nothing alike and the language draws it in one place.** A result
that has **no value at all** raises: `1 / 0` is a runtime error, `sqrt(-1)` is a
runtime error. A result that **overflows a double** does not — `number("1e308") *
10` is `inf`, with no diagnostic, and it travels through everything downstream
including `encode` (see *Encoding*, where the dialect writes `inf` and
`json_encode` refuses it).

That is deliberate rather than an oversight, and the distinction is between
*undefined* and *too large*: zero division and a negative square root have no
answer in any arithmetic, while an overflow has an IEEE answer that keeps
propagating correctly — `inf > x` is true for every finite `x`, and a product
that overflows and is then divided back can land on a real number again.
Raising there would break ordinary numerics for the sake of a diagnostic.
Recorded 2026-10-02, reported by the gbasic-books session as an inconsistency;
it is a line, and nothing had drawn it.

Powers, roots and logarithms. Each raises rather than returning `nan` or
`inf`, so a domain error stops at the call that made it:

- `sqrt(x)` — square root; raises `sqrt of a negative number`.
- `pow(base, exponent)` — `pow(2, 10)` is `1024`, and a fractional exponent is
  a root: `pow(2, 0.5)` is √2. Two exact integers with a **non-negative**
  exponent give an exact answer, by repeated multiplication *(0.8.1)*, so
  `pow(2, 53) + 1` is `9007199254740993`; a negative exponent is a fraction and
  is not exact.
- `log(x)` — **natural** logarithm; `log10(x)` — base 10. Both raise
  `log of a non-positive number` for `x <= 0`.
- `erf(x)` / `erfc(x)` — the error function and its complement.
  `lgamma(x)` — the natural log of the absolute value of the gamma function.

Randomness — not cryptographic. For anything security-bearing use
`random_bytes` from the crypto builtins:

- `random()` — a number in `[0, 1)`.
- `random_int(low, high)` — a whole number in `[low, high]`, **inclusive** at
  both ends.
- `seed(n)` — set the generator's seed, making a run repeatable. Two runs
  seeded alike produce the same sequence, which is what makes a test that uses
  randomness deterministic. Answers `nothing`: it acts, and handing `n` back
  would not be telling you anything you did not just say.

### Statistics

Summaries over an array of numbers. The sample/population distinction is
explicit in the names rather than an argument, because getting it wrong
produces a plausible number rather than an error:

- `variance(a)` / `stdev(a)` — **sample** (divide by *n−1*).
- `pvariance(a)` / `pstdev(a)` — **population** (divide by *n*).
- `range(a)` — largest minus smallest. Takes an **array**, not two bounds.
- `quantile(a, p)` — `p` from **0 to 1**; `percentile(a, p)` — `p` from **0 to
  100**. Both interpolate linearly between the two neighbouring values, using
  position `p × (n−1)` in the sorted data, which is the common default (numpy's
  `linear`). `quantile(a, 0.5)` is the median.
- `iqr(a)` — the interquartile range, `quantile(a, 0.75) - quantile(a, 0.25)`.
- `skewness(a)` — **population** skewness, `m₃ / m₂^1.5`. This is *not* the
  sample-adjusted G₁ that some packages report by default; on the same data the
  two differ noticeably.
- `kurtosis(a)` — **excess** kurtosis, `m₄ / m₂² − 3`, so a normal distribution
  gives `0` rather than `3`.
- `correlation(a, b)` / `covariance(a, b)` — over two equal-length arrays.

`stdlib/stats.bas` builds on these with distributions, regressions and tests;
these are the primitives.

### Optional parameters

A function parameter may carry a **default**, used when the caller omits it:

```basic
function greet(name, greeting = "Hello", punct = "!")
    return greeting + ", " + name + punct
end function

print greet("Ada")              ' Hello, Ada!
print greet("Ada", "Hi")        ' Hi, Ada!
print greet("Ada", "Hi", "?")   ' Hi, Ada?
```

**A default must be a literal** — a number (optionally signed), a string,
`true`, `false`, `nothing` or `unknown`. Not an expression, not another
variable, not a call. The restriction is deliberate rather than temporary: an
expression would have to be evaluated in some scope, and gBASIC has no
closures, so a default reading an enclosing variable would inherit the
read-then-shadow rule, and one reading an earlier parameter would need a
defined evaluation order. A literal has nothing to see.

It also removes the mutable-default trap other languages have: an array or
record default is a parse error, and gBASIC strings are immutable, so no
default is a value that could be mutated and carried into the next call.

**Optional parameters must come last.** `function f(a = 1, b)` is refused where
it is declared — a caller writing `f(5)` would mean `a` positionally, leaving
`b` unset while the parameter that *has* a default took the argument.

Arity errors name the range: `greet expects 1 to 3 arguments, got 0`. A
function with no optional parameters keeps the exact-count message.

Defaults are for **functions only**. A modifier's parameters come from the
clause that invokes it, a `program` block's from the command line, and a server
handler's from the URL pattern — in each case the runtime supplies them, so a
default could never take effect and is refused by name rather than ignored.

Defaults apply wherever the function is called from, including through a
function value, and including an actor: `spawn worker(1)` sends only the
argument the call supplied and the child fills the rest itself.

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

**Every file function takes a path string or a `{file}` reference**
(*since 0.11.0*), and a string is taken exactly as `{file}` would take it, an
interior NUL refused the same way. So
`read(join_path(directory_name(program_file()), "spec.txt"))` needs no
conversion, though `join_path` answers text. Before 0.11.0 the whole-file
verbs (`read`, `write`, `exists`, `delete`, `read_lines`, `bytes`, `lines`,
`chars`, `overwrite`, `read_at`, `file_size`, `file_mtime`, `lock`, `unlock`,
`copy` and `move`'s source, and `with lock`) refused a string while every
path function beside them took one.

**`append` is the one exception.** It is also the array verb, and a string
there is far more often a slip than a file name: `append(name, "-2026")`
would otherwise create a file called whatever `name` held. It refuses a
string and says to write `{file}path`. It does append to a `{file}` held
anywhere, a variable, a record field (`append(cfg.log, line)`) or an inline
`append({file}"run.log", line)`; before 0.11.0 only a bare variable worked
and the other two were taken for the array verb and refused.

- `exists(f)` — whether the path exists.
- `read(f)` / `write(f, text)` / `append(f, text)` — whole-file I/O.
  `append` adds bytes to the end of a file (`append({file}path, chunk)`), and
  with `read_at` below is how a file of fixed-size encrypted chunks is written
  and read back (`tests/read_at_chunks_test.bas`).
- `fsync(f)` (*since 0.11.0*) — what was written to `f` is on stable storage
  before it returns; `true`, and it **raises** if the system refuses. `f` may be a
  directory, which syncs its entries: a file just created or renamed into a
  directory survives a crash only once the directory is synced too. Durable
  replacement is therefore `write(temp, data)`, `fsync(temp)`,
  `atomic_replace(temp, dest)`, `fsync(directory_name(dest))`. On Windows a
  directory answers `true` having done nothing, since NTFS journals its
  entries.
  **Binary-safe in both directions** (*since 0.1.0-rc9*): a gBASIC string may
  hold any byte including NUL, and `read` returns the file's full length rather
  than stopping at the first one. Before rc9 `write` was binary-safe and `read`
  was not, so a file holding binary — a `serialize` payload, an image — could
  be written and silently read back short.
- `bytes(f)` / `lines(f)` / `chars(f)` — the file's **size** in bytes, its line
  count, and its count of **codepoints**. These are counts, not content; use
  `read` for the content. `bytes` is answered by `stat` on a regular file, so it
  costs nothing on a large one; `lines` and `chars` read it.

  **`chars` counted BYTES until 2026-10-02**, sharing a branch with `bytes`
  under a `TODO` in the source, so the two verbs returned the identical number
  on every file — right for ASCII and wrong for everything else, in the
  direction that looks like a working answer. It had no test coverage in the
  tree, which is how the `TODO` reached a release. `chars(f)` now agrees with
  `len(read(f))`, which was correct all along, and the two routes to the same
  question no longer disagree. Asserted in `examples/file_counts_test.bas`,
  whose load-bearing check is that `chars` and `bytes` **differ** on a
  multibyte file — every value check in the file is also satisfied by a build
  where they are one function.
- `read_lines(f)` — the file's lines as an array of **strings**, without their
  line endings. `lines(f)` counts them; this returns them. It holds the whole
  file: for a large one use `for each line in f`, which streams. A line is
  **counted bytes**, so an interior NUL is content *(fixed in 0.9.1; before that
  each line was built as a C string and truncated at the first NUL, while
  `read(f)` on the same file returned every byte — two routes to one question
  disagreeing)*.
- `overwrite(f, text, position)` — write `text` **in place** at a byte offset,
  leaving the rest of the file alone. Three arguments, not two: it is not a
  whole-file replace, which is what `write` does. `overwrite(f, "XYZ", 3)` over
  `abcdefghij` gives `abcXYZghij`.
- `read_at(f, position, count)` — read up to `count` bytes starting at byte
  `position` (*since 0.11.0*). The other half of `overwrite`'s offset: until
  this, a program could rewrite the middle of a file it had no way to inspect,
  and the only reader was whole-file.

  **A count past the end is clamped, and the allocation follows the file rather
  than the argument** — so `read_at(f, 0, 1000000)` on a ten-byte file answers
  ten bytes and allocates eleven, which is what lets a caller pass a generous
  chunk size without asking the file's size first. **At or past the end the
  answer is `""`**, because that is how a reader terminates; `overwrite`
  refuses past the end for the opposite and equally good reason — writing there
  would have to invent the bytes in between.

  This is how a **binary** file is read in bounded memory, which `for each line`
  cannot do: a line is unbounded, so a file with no newline byte in it is read
  whole however the loop is written. Measured on 50 MB of such a file, **14.8 MB
  resident chunked against 115 MB by line**, and flat across a 4× change in
  file size.

  ```basic
  pos = 0
  while true
      chunk = read_at(f, pos, 65536)
      if chunk = "" then break
      handle(chunk)
      pos = pos + byte_count(chunk)      ' NOT len(chunk) -- see below
  end while
  ```

  **Advance by `byte_count`, never `len`.** A position is **bytes** and `len` is
  **codepoints**. On text they agree; on anything else the loop advances short,
  re-reads what it already had, and reassembles a longer, corrupted copy in
  silence — measured, a 65,536-byte chunk of random bytes has `len` 62,800.

  It is **positional and stateless**, so it opens the file per call — which is
  what makes it safe to interleave with anything else touching that file, and
  what makes a very small chunk size wasteful: measured over 32 MB, 8,192 calls
  of 4 KB cost 46 ms against 20 ms for 512 calls of 64 KB. 64 KB or more is the
  advice. A directory is refused, like the whole-file readers.
- `delete(f)` — remove a file. `remove_dir(d)` — remove a directory;
  `make_dir(d)` creates one.
- `make_dir(d, { parents: true })` *(since 0.2.3)* — create `d` **and any
  missing parents**, treating a directory that already exists as success. A
  component of the path that is taken by something that is **not** a directory
  is refused, naming it; an unknown option is refused by name.

  Bare `make_dir(d)` is deliberately **not** idempotent and will not become so:
  `mkdir` is atomic, so "did I create it" is how a program takes a lock across
  processes without one, and an existing directory has to stay an error for
  that to keep working. The two spellings answer two different questions —
  *create this* and *make sure this exists*.
- `remove_tree(d)` — remove a directory **and everything in it**
  *(since 0.11.0)*. `remove_dir` removes only an empty one, so until this
  nothing in the language could remove a tree it had just built.

  **Its own name rather than an option on `remove_dir`**, because the option it
  would be offered is a recursive delete and that belongs to a decision nobody
  should make by passing a flag. The two calls are two different acts.

  - **A symlink inside the tree is unlinked, never followed.** A recursive
    delete that descends a link removes somebody else's data while reporting
    success, so the link goes and whatever it points at is left alone — the
    load-bearing property of the whole call, and the reason its suite builds a
    tree containing a link to a directory outside it.
  - **A symlink given as the top path is refused, naming it.** `rm -rf` on a
    link removes the link and stops, which is safe and silent; a function
    called `remove_tree` answering "done" having removed no tree is worse than
    a refusal. Use `delete(p)` to remove a link.
  - **A file is refused** (use `delete`), and so is a missing path, with the
    OS reason.
  - **Depth is bounded at 256 directories**, the same bound the glob uses: a
    link-free tree can still be deep enough to exhaust the stack, and this walk
    holds an open directory handle at every level. Past that it refuses,
    naming the **top** of the tree and the bound — not the 512-byte path 256
    levels down, which says nothing a reader can act on — and nothing below
    the bound is removed.
  - **A failure may leave the tree partly removed**, which is why the message
    names the path it stopped at. By the time anything can go wrong it has
    already removed things, so a bare `false` would leave a half-removed tree
    indistinguishable from a removed one.
- `temp_file([options])` / `temp_dir([options])` — a temporary file or
  directory that **already exists** *(since 0.11.0)*. It is **created, not
  merely named**: a function that hands back an unused name hands back a race,
  where two programs asking at the same moment are told the same name and the
  second overwrites the first. Answers a **file** or **directory reference**,
  so `write`, `read`, `atomic_replace`, `files` and `remove_tree` compose with
  no modifier; `string(t)` gives the path.

  Options, both optional, each refused by name if misspelled:

  - `in:` — the directory to create it in; a directory reference, a file
    reference or a path string. The default is the system temporary directory
    (`TMPDIR` or `/tmp`; `GetTempPath` on Windows).
  - `prefix:` — a name hint, so a listing later says which program left it.
    It is a **name, not a path**: a path separator is refused, because a prefix
    that could climb out would put the file somewhere the caller did not name,
    and an interior NUL is refused like any other path.

  **Use `in:` for the safe-write pattern**, and that is a measurement rather
  than a preference: `atomic_replace` is same-filesystem only, and the system
  temporary directory is routinely on another filesystem — measured here, `/tmp`
  is tmpfs while the repository is on ext4, so a temp file in `/tmp` **cannot**
  be swapped onto a destination beside the program's own data. It fails
  cleanly, naming the requirement, rather than degrading to a copy.

  ```basic
  here {dir}= directory_name(dest)
  t = temp_file({ in: here, prefix: "stage-" })
  write(t, text)
  atomic_replace(t, dest)          ' the temp file BECOMES the destination
  ```

  On POSIX the file arrives with mode **0600** and the directory **0700**,
  which is what lets a program hold a secret in one. **On Windows the same
  promise is kept by ACL**: both are created granting the current user alone,
  rather than inheriting the folder's permissions — measured, an inherited ACL
  let any signed-in user read and change the file once `TEMP` pointed at a
  shared folder. What `permissions` *reports* on Windows still differs (`0666`),
  because it reads the file's attributes, not its ACL.

  **Nothing is removed automatically**, deliberately: in the pattern above the
  temp file is *meant* to outlive the call that made it, since it becomes the
  destination. A program that made one in the system directory is the only
  thing that knows when it is finished with it — `delete(t)` for a file,
  `remove_tree(d)` for a directory, which is why the two shipped together.
- `exists(ref)` accepts a **directory** reference as well as a file one
  *(since 0.2.3)*, because it asks about a **path** rather than about a file's
  contents. `read`, `lock` and the rest of that family still take a file
  reference only. It answers by `stat`, so a file that exists but is not
  readable reports as **present**.
- Every reader — `read`, `read_lines`, `lines`, `chars`, `bytes` — **refuses a
  directory** (*since 0.10.0*), reporting `could not read file: <path> (Is a
  directory)`. Before that it **aborted the interpreter**: on Linux `fopen` on a
  directory succeeds, `ftell` answers `LONG_MAX`, and the allocation of that
  fails, so the process died with `SIGABRT` and a core dump — which takes
  block-buffered stdout with it, so a program printing its own progress printed
  nothing at all and the failure read as line 1. `for each line in f` was never
  affected: a read error there has always raised rather than looking like
  end-of-file.
- `lock(f)` / `unlock(f)` — advisory locks (see `with lock`). **`lock` waits.**
  If another process holds the lock the call blocks until that process releases
  it or dies — measured at 2.91 seconds against a holder that held for three.
  **`lock(f, { wait: false })` never waits** (*since 0.11.0*): it answers `true`
  if it took the lock and `false` if another process holds it, so a scheduler
  steps around a running job rather than queueing behind it, and the lock still
  goes when its holder dies. Without it, a lock held by something that never
  finishes is a hang with no diagnostic. Reading is
  unaffected: the lock is advisory, so `read` ignores it. Taking a lock
  installs a cleanup handler for `SIGINT`, `SIGTERM` and `SIGHUP`, and
  **that does not change how the process dies**: after releasing, it re-raises
  the signal with the default disposition, so a supervisor still sees a death
  by signal rather than an exit code that resembles one. Before 0.2.3 it ended
  the process with `_exit(128 + signo)`, so a program that had ever taken a
  lock reported `exit 143` where an unlocked one reports `signal 15` — and a
  supervisor could not tell it from a program that chose to exit 143.

**What a file says about itself, and changing it** *(since 0.11.0)*:

- `disk_space(p)` -> `{ total, free, available }`, the filesystem holding `p`
  (a file or a folder, as a path, `{file}` or `{dir}`), in exact integer bytes.
  **`available` is what an unprivileged writer may still use, and is the one
  that fills first for a service account.** `free` also counts the space a
  filesystem keeps back for root (ext4 keeps 5%), so on such a filesystem
  `available` is below `free`. A path that is not there raises, naming it.
  POSIX reads `statvfs(3)`; Windows `GetDiskFreeSpaceExW`, whose `available`
  is what this user's quota leaves. The suite is `tests/run_disk_space.sh`,
  held against Python's `os.statvfs` and `df`.
- `permissions(p)` -> `{ mode, readable, writable, executable, owner, owner_name,
  group, group_name }`. `owner` and `group` (*since 0.11.0*) are the numeric ids
  and their names, `unknown` where the platform does not say (Windows) or an id
  has no name here; set beside `process.self().uid`, they say whose `0600` it
  is. **Two
  different questions, both answered, because conflating them is the classic
  bug.** `mode` is what the **file** says — the permission bits as text,
  `"0640"`. `readable` / `writable` / `executable` are what **this process** can
  do, from `access(2)`: root reads a file whose mode is `0000`, and a `0644`
  file on a read-only mount is not writable. A program asking *"can I read
  this?"* wants the second; one auditing a credential store wants the first.

  **The mode is text and that is forced, not a preference**: gBASIC has no
  octal literal — measured, `0o600` is a parse error (the lexer reads it as a
  duration unit) and `0600` is the number six hundred — so a numeric mode could
  not be written correctly even if it were offered. `"0640"` is also the
  spelling every author already knows from `chmod(1)`.

  `executable` answers **`unknown`**, never `false`, on a platform that has no
  execute bit: Windows decides executability by the file's extension, so
  `false` would be a claim nothing supports. There, `mode` is what `_stat`
  synthesises — `0444` when the read-only attribute is set, `0666` otherwise.
- `chmod(f, "0640")` — set the permission bits. Up to four octal digits, so
  `"755"` and `"0755"` are the same mode.

  **A number is refused by name**, with the arithmetic in the message:
  `chmod(f, 0600)` is the mistake every author makes once, and six hundred is a
  perfectly good `mode_t` — it would set bits nobody asked for **and succeed**.

  **On Windows only the owner write bit is expressible** (it becomes
  `FILE_ATTRIBUTE_READONLY`, which applies to everybody), and the call **warns**
  — `2114` — naming what it could not do rather than refusing outright, so a
  program hardening a file still gets the one bit Windows has. No ACL is
  attempted.
- `set_mtime(f, when)` — stamp a file's last-modification time from a
  `datetime`. The write half of `file_mtime`, and `copy` does not preserve a
  timestamp, so an archive step that keeps the source's own time had no
  spelling. The **access** time is left alone: rewriting it as a side effect
  would destroy information the caller never offered.

  **A number is refused**, naming `from_epoch(n)`. A ledger that stores epochs
  (strict JSON refuses a `datetime`, so several do) would otherwise pass one and
  the call would work — until the day somebody passed milliseconds and the file
  was stamped in the year 56000. A **time-only** `datetime` is refused too: it
  names no instant.
- `is_symlink(p)` — is this path a symlink? **`file_type` cannot answer it**,
  because `file_type` *follows* a link: measured, a link to a file answers
  `file` and a link to a directory answers `folder`. On Windows this is true of
  any reparse point — a symlink, a directory junction or a mount point.
- `symlink(target, path)` — create a symlink at `path` pointing at `target`.
  **The target need not exist**: a dangling link is a legitimate thing to make
  (it is how a link is written before the file it points at arrives), and
  refusing one would make the commonest ordering impossible.

  On Windows this needs `SeCreateSymbolicLinkPrivilege` — Developer Mode or an
  elevated process — and the refusal **names the privilege**, because a bare
  "permission denied" sends an author to look at the filesystem, where there is
  nothing to find: this is a policy decision, not a file permission.

**A correction that came with them** *(0.11.0)*: a listing's `type` now answers
what `file_type` answers — `file`, `folder`, `other`, and `unknown` when the
path cannot be `stat`ed. It used to fall back to `"file"` for anything that was
not a directory, so a **dangling symlink, a FIFO, a socket and a device all
arrived labelled `file`** while `file_type` on the same path said `unknown` or
`other` — two routes to one question disagreeing, and the listing held the
wrong one, there being no file there. `if e.type = "file" then read(e.path)`
raises on the first and **blocks forever** on the second. The entries are still
**reported** — `files(d, pattern)` excludes folders rather than requiring the
exact word `file`, because a file withdrawn between the listing and the read is
a real event and dropping it silently is what `intake`'s vanished-file report
exists to prevent — so only the label changed.

**Taking a path apart.** These work on a file or directory reference and on a
plain string alike, so a value straight out of a listing needs no conversion:

- `file_name(p)` — the last component. `file_name("a/b/c.tar.gz")` is
  `"c.tar.gz"`; a trailing slash is tolerated, so `file_name("a/b/")` is `"b"`.
- `directory_name(p)` — everything before it. `directory_name("a/b/c")` is
  `"a/b"`, and a bare name gives `"."`.
- `extension(p)` — the last suffix, **without** the dot: `extension("c.tar.gz")`
  is `"gz"`, not `"tar.gz"`. A name with no dot gives `""`, and so does a
  dotfile like `".hidden"` — a leading dot is not an extension — and so does a
  **trailing** dot, since `"a."` has nothing after it to be a suffix.
- `stem(p)` — the last component with that suffix taken off (*since 0.10.0*):
  `stem("/data/vendor_2026-03.csv")` is `"vendor_2026-03"`, which is usually the
  business key a filename carries. It is the other half of one cut, so it and
  `extension` always agree about where the boundary is:

  ```
  extension(p) = ""  →  stem(p) is file_name(p)
  otherwise          →  stem(p) + "." + extension(p) is file_name(p)
  ```

  Everything that is not an extension therefore stays: `stem(".bashrc")` is
  `".bashrc"`, `stem("..")` is `".."`, `stem("a.")` is `"a."`, and
  `stem("archive.tar.gz")` is `"archive.tar"` — the *last* suffix comes off, not
  the first. Python's `splitext` differs on one of those, answering `"a"` for
  `"a."`, because its extension keeps the dot and so can be `"."` alone; ours
  cannot, and the invariant above is what decides the rest.

  To change a file's suffix, which is what this exists for:

  ```basic
  done = join_path(directory_name(p), stem(p) + ".done")
  ```
- `join_path(a, b)` — joins exactly **two** components with a single separator,
  whatever slashes they already carry. Note it always joins: `join_path("a",
  "/b")` is `"a/b"`, not `"/b"`, so an absolute second argument does not replace
  the first the way some languages' equivalents do.
- `real_path(p)` — the canonical absolute path, resolving `.`, `..` and
  symlinks. The path must exist. To validate a path you are about to *create*,
  resolve `directory_name(p)` instead.
- `file_type(p)` — `"file"`, `"folder"` or `"other"`, and `unknown` when
  nothing is there.
- `program_file()` — the running program's own file, as an absolute file
  reference with links resolved, so a program can find a file beside itself
  whatever the working directory (*after 0.10.0*). `read` resolves a relative
  path against the working directory, unlike `load`, which resolves beside the
  loading file. It answers the program that was run even when a library asks,
  and `unknown` at the prompt:

  ```basic
  here = directory_name(program_file())
  spec = read({file}join_path(here, "open_po.ari"))
  ```

The seven that take a path **refuse a path containing an interior NUL byte** rather than
splitting it, because a path crosses into the system as a NUL-terminated C
string: a name split out of `"/a/b" + chr(0) + "x/c.txt"` describes a path that
cannot be opened, and every answer here is used to build another path. Before
0.9.1 only `real_path` and `file_type` refused, and the other five truncated —
`file_name` of that string answered `"b"`, the basename of `/a/b`.

Listing a directory — two functions, and the difference matters:

- `list_files(path)` — the **files** in one directory, as **file references**
  (not strings, though one renders as its full path). Use `file_name` on an
  entry for the bare name and `extension` for its suffix; `{file}` is an
  idempotent passthrough, so an entry also flows straight into `read`, `bytes`
  and the rest. It does **not** recurse, and it does **not** report
  subdirectories at all, so it cannot by itself drive a walk: a directory is
  invisible to it.
- `list(d [, pattern])` — every entry of a directory, as
  `{name, path, type, size, modified}` records where `type` is `"file"` or
  `"folder"`. `name` is the bare entry name and `path` is the path to it.
  Takes a **plain string path or a `{dir}` reference** (*since 0.1.0-rc9*), so
  the `d {dir}= p` line in front of a listing is no longer needed — `{dir}=`
  labels a path, it does not check one, and it accepts a missing path or a
  file's path just the same.

  `size` is the entry's byte count, and is **`unknown` for a folder** — never
  `0` — because a directory's stat size is the platform's own bookkeeping and
  not the bytes under it, the same reason `file_size` refuses one outright.
  `modified` is its mtime as a `datetime`, exactly what `file_mtime` answers.
  Both are `unknown` when the entry could not be stat'd — a broken symlink, or
  an entry removed between the listing and the stat, which a long walk does
  meet. **Neither costs anything**: the listing already stat'd every entry to
  decide `type`, and used to throw the rest away. Reading `e.size` is a field
  read where `file_size({file}e.path)` is a call, which matters when the
  decision to process a file is a filter over thousands of them.

  Because `modified` is a `datetime`, **an entry does not go through `encode`**,
  which refuses typed values. Use `notation.to_text`, which keeps every type and
  round-trips a whole entry, or `epoch(e.modified)` for a number. (It is a
  `datetime` rather than an epoch number so that it agrees with `file_mtime`,
  and because `if e.modified > last_run` is what an incremental load asks.)

  **The entries come back sorted by `path`** (*since 0.10.0*). `readdir` order
  is the directory hash's — neither alphabetical nor stable across machines —
  so every caller had to sort, and a recursive walk interleaves depth-first in
  an order nobody can predict.

- `files(d [, pattern])` / `folders(d [, pattern])` — the same records,
  filtered to one kind. `files(d)` is `list(d)` minus the subdirectories.

  **With no pattern all three list one level.** With one they walk, and the
  pattern is matched against the path **relative to `d`**:

  ```basic
  files(d)                            ' one level, every file
  files(d, "*.xlsx")                  ' one level, matching
  files(d, "**/*.xlsx")               ' any depth
  files(d, "2026/*/vendor_*.xlsx")    ' a year, every quarter under it
  files(d, "**/vendor_*.xlsx")        ' and when the folder shape changes
  ```

  The language is **glob**, as a shell with `globstar` means it:

  - `*` matches any run of characters **within one segment**, `?` matches one,
    and `[abc]` / `[a-z]` / `[!a-z]` are character classes. An unclosed `[` is
    a literal `[`, not an error, because a filename may contain one.
  - `**` matches **zero or more whole segments**, so a leading `**` finds a
    file at the top level as well as one three deep. It is the one thing that
    descends, and it is what makes a pattern indifferent to folder structures
    that change.
  - **A leading `.` must be matched explicitly.** `*.json` does not return
    `.intake.json` and `**` does not walk into `.git`; `.*` returns them. With
    **no** pattern, every entry including dotfiles comes back, which is `ls`
    against `ls *`. For a job importing a directory this is the rule that stops
    a glob feeding an import ledger kept beside the data back to itself.
  - **A pattern that matches nothing is an empty array, never an error** — a
    run with no new files is a normal day, and the alternative puts a handler
    around the ordinary case.
  - The walk is **pruned by the pattern**: `files(d, "2026/*/*.xlsx")` opens
    that year's folders, not the estate.

  A pattern that cannot be *meant* is refused rather than left to match
  nothing, because a pattern usually arrives from configuration and "found
  nothing" hides a typo: an empty pattern, one beginning with `/` (it is
  relative to `d`), one with an empty segment (`a//b`, `a/`), and one
  containing `.` or `..`.

  **The walk does not follow symlinks to directories.** A symlink can point at
  its own ancestor, and `ln -s .. loop` would make a recursive walk run
  forever — a hang reports nothing at all. So a `/data/current -> /data/2026-Q2`
  arrangement is reached by naming the link **as the directory**
  (`files({dir}"/data/current", "*.csv")`, which does follow it) or by
  resolving it with `real_path`, and not by globbing through it. A symlink to a
  **file** is an ordinary entry.

  **An unreadable directory raises at the top level and warns below it.** If
  the directory you *named* cannot be read you got none of what you asked for,
  so `list`, `files` and `folders` raise (`could not read directory: <path>
  (<reason>)`), naming the OS reason because ENOENT, EACCES and ENOTDIR want
  different fixes. A subdirectory reached by a recursive pattern is different:
  one locked folder must not throw away a scan of ten thousand files, so it
  emits **warning `2113`** naming the path and the reason and the walk
  continues. `on warning ignore` silences it for a walk that expects it, and
  `on warning stop` turns it back into an error for a job that needs
  completeness.

  A caller that wants "empty when I cannot read it" says so in one line:

  ```basic
  entries = []
  on error goto next
  entries = list(d)
  if error then error.clear()
  ```

  `stdlib/filetree.bas` is a tree browser built this way, with expand/collapse
  state on top.

File metadata and atomic replacement:

- `file_size(f)` — the file's size in bytes as an exact integer, from `stat`
  (metadata only, not a read). This is the binary byte count, so it counts
  embedded NULs and is unaffected by text length; a zero-byte file returns `0`.
  A size is a count, so it is exact as `len` and `byte_count` are, and a G-SQL
  `integer` column takes it *(2026-11-09; it was inexact)*. Takes a path string or a file reference. A directory has
  no content size and raises; a missing path raises with the OS reason.
- `file_mtime(f)` — the file's last-modification time as a `datetime`, from
  `stat`, in local time. **Second resolution:** gBASIC's `datetime` has no
  sub-second field, so any nanosecond part of the filesystem timestamp is
  dropped rather than fabricated. Takes a path string or a file reference; works for
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
    not perform: call `fsync` on both (see `fsync` above).
  - **Relationship to `move`.** `move(src, dest)` is unchanged: it still renames
    on the same filesystem and *does* fall back to copy+delete across
    filesystems (so it can cross devices, non-atomically). Use `atomic_replace`
    for the safe-write pattern (write a temp, then atomically swap it in) where
    an all-or-nothing replacement matters; use `move` when you want a best-effort
    relocation that may cross filesystems.

Path questions about what is really there:

- `runtime()` -> `{ version, build, stdlib }` (*since 0.11.0*) — which gBASIC is
  running this: `version` and `build` are what `gbasic --version` prints, split
  (`build` is empty for a release), and `stdlib` is where a `load` looks first.
  So a program recording what read each file can say so without being handed
  `--version` in an environment variable.
- `platform()` -> `"linux"`, `"windows"`, `"macos"` or `"posix"`
  *(since 0.11.0)* — which platform this binary was **built** for. It answers
  what the build knows: there is one body per platform file and each returns its
  own name, so it cannot drift from the bodies it describes.

  It exists because **a few behaviours genuinely differ and a program may have
  to ask**. The path splitters are the first: a backslash separates on Windows
  and is an ordinary filename character on POSIX, so a test that runs on both
  platforms — which is how the Windows suite is written — otherwise has to
  choose between asserting something false on one side and asserting nothing on
  either. Prefer a question about the *thing* where one exists (`is_symlink`,
  `permissions`, `has_builtin`); reach for this when the difference really is
  the platform.
- `library_collisions()` -> array of `{ name, libraries }` — every public name
  defined by **more than one loaded library**. The override warning cannot
  answer this: it lives in the unqualified-lookup path, so it is
  *call-triggered* and stays silent while every call is qualified — a collision
  is invisible until someone writes the first bare call, which is exactly when
  it is too late to be told.

  **It is not an error condition and you should not assert it is empty.**
  `stdlib` alone has seven benign shared names, so a program loading `dates`
  and `frame` legitimately reports `select`. The useful shapes are an allowlist
  or a baseline:

  ```basic
  load dates
  load frame
  accepted = ["select"]
  for each c in library_collisions()
      if not contains(accepted, c.name) then
          print to error "namespace: " + c.name + " from " + join(c.libraries, ", ")
      end if
  next
  ```

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

- `list(folder [, pattern])` / `files(folder [, pattern])` /
  `folders(folder [, pattern])` — directory entries. With a glob pattern they
  walk; see the listing section above for the pattern language.

Directory entries are records with `name`, `path`, `type`, `size` and
`modified`, sorted by `path`.

## Standard Library

Beyond the native modules and core builtins, gBASIC ships a set of **pure-gBASIC**
toolkits under `stdlib/*.bas`, loaded by name (`load stats`) with `GBASIC_PATH`
pointing at the library directory (or resolved from the install path after
`make install`). This is a catalog; each toolkit's full API lives in its own
document, which is the single source of truth for it — the entries below only say
what each is and where to read it.

### Which libraries need something from the build

**A library is shipped whether or not the binary beside it can run it**, and
`load` says so at once rather than failing at the first call:

```text
load ownership
-> XML support is not available in this build
```

That is the right moment to be told and the message names the cause. What was
missing until 0.7.1 was anywhere to look it up *before* reaching it. These
21 of the 66 standard libraries need an optional dependency; every
other one is pure gBASIC over the core and works on any build.

<!--LIBNEEDS:loads-->
| library | needs installed |
| --- | --- |
| `agent` | libcurl |
| `ari_advisor` | libcurl |
| `dbframe` | libsqlite3 |
| `discovery` | unixODBC |
| `edgar` | libcurl, libsqlite3 |
| `finio_all` | libxml2 |
| `finio_camt` | libxml2 |
| `finio_iso20022` | libxml2 |
| `finio_pain001` | libxml2 |
| `finio_rates` | libcurl |
| `insiders` | libcurl, libsqlite3, libxml2 |
| `llm` | libcurl |
| `market` | libcurl |
| `mcp` | libcurl |
| `mdna` | libcurl, libxml2 |
| `ownership` | libxml2 |
| `pdf` | libxml2 |
| `retrieval` | libpq |
| `screener` | libsqlite3 |
<!--LIBNEEDS:end-->

**The column is what to install, not what the error says.** The modules refuse
in their own words, and *WebClient*, *HTTP* and *SMTP* are three faces of
libcurl — a table in those terms would tell a reader three things are missing
where one is.

**The dependency is transitive, which is why some rows are surprising.**
`finio_all` contains no XML of its own; it loads `finio_camt`, which loads
`xml`. `ari_advisor` needs libcurl because it loads `llm`. A scan of either
library's own `load` lines reports it as needing nothing.

**This table is derived, not maintained.** `tools/library-needs.sh` computes it
from the `load` lines in `stdlib/*.bas`, `tests/run_library_tiers.sh` requires
the two to agree in both directions, and `tools/build-release-tarball.sh`
refuses to publish an archive whose unloadable set is not exactly what the
derivation predicts. So a new library, a new dependency and a change of build
tier each move all three together.

### And some that load, then fail

**A library can `load` perfectly and still not work**, which is the half the
table above misses and the worse half, because the failure is deferred to the
call. These 6 call a **builtin** whose availability moves with the build, so
nothing in their `load` lines gives them away:

<!--LIBNEEDS:calls-->
| library | loads anywhere, but its functions need |
| --- | --- |
| `crypto` | libcrypto |
| `gpdf` | zlib |
| `intake` | libcrypto |
| `mcp` | libcrypto |
| `otp` | libcrypto |
| `websocket` | libcrypto |
<!--LIBNEEDS:end-->

Measured on the 0.7.0 lean archive: `load crypto` and `load otp` both succeed,
and then every function of either answers *HMAC requires OpenSSL (libcrypto);
rebuild with it installed*. Both work on `-full`. **`gpdf` works on both archives the
release build now produces**, zlib having become static — it is listed here
because the dependency is real and a build without zlib still has it.

This list is derived too — from `src/builtins.c`'s own `#if` guards, so a
builtin that becomes optional, or stops being, moves it without an edit.

**What the downloads carry.** `tools/build-release-tarball.sh` links **libxml2,
zlib and sqlite3 statically** into both tiers, so every library in the libxml2
and libsqlite3 rows works on *either* archive, and so do `xlsx`,
`compress`/`uncompress` and `sqlite`. A dependency-free download therefore
carries a real embedded SQL database, with FTS5 and R-Tree, which is what takes
`dbframe` and `screener` onto the lean tier; `edgar` and `insiders` stay off it
because they also need libcurl. The cost is size: the lean tarball is 2.1 MB
where 0.7.0's was 828 KB. Static linking leaves no soname to vary and no
library to be absent, which is why it
reaches a case `dlopen` cannot — the XML reader depends on libxml2's struct
layout, and a soname bump is precisely the announcement that a layout moved.

**Password hashing is in both downloads too, by a third route.**
`password_hash`/`password_verify`/`password_hash_cost` were in *neither* 0.7.0
archive, because libxcrypt's soname is `libcrypt.so.1` on the Debian family and
`libcrypt.so.2` on the RHEL family — so the downloads ship the **vendored
yescrypt** (`third_party/yescrypt`, BSD-2) instead, which the Windows build
already used and which writes the same `$y$j9T$` hashes libxcrypt's default
writes. It adds 44 KB and no `DT_NEEDED` entry. The one difference is reading a
hash gBASIC did not write: legacy `crypt(3)` schemes (`$6$`, `$2b$`, `$1$`) are
**refused by name** rather than answered `false` — see
[Password Hashing](#password-hashing).

| | can load | cannot |
| --- | --- | --- |
| lean (`libc`, `libm` only) | 52 of 62 | **10** — the libcurl, libpq and unixODBC rows |
| `-full` (+ libcurl, libssl, libcrypto) | 60 of 62 | **2** — `discovery` (unixODBC) and `retrieval` (libpq) |

*(Derived from the table above after GTK and gi were removed on 2026-10-22,
which took four libraries off each tier's "cannot" list; the release build
re-measures these and refuses to publish if they differ.)*

**The 0.7.0 archives predate this** and carry 44 and 52 of 65 respectively,
with libxml2 in neither; the figures above describe what the release build
produces now. Both numbers are asserted by the build itself, which sweeps every
library against the binary it just made and refuses to publish an archive whose
unloadable set is not what the derivation above predicts.

What is left needs a build from source, and that is a measured decision rather
than an oversight. libpq and unixODBC are **not installed on an ordinary
machine**, and a linked binary that cannot find one does not start at all — a
library refusing with a message naming what to install is strictly better. The
reasoning and the measurements are in `docs/dependency_linking_design.md`.

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
- `rules` — **business rules as data**, first increment. Typed conditions and
  **ordered overrides** over **named targets**: *when this condition holds,
  replace this target with this expression; otherwise keep its preceding
  result* — which is what lets a contract amendment replace one component (a
  cap, a rate) rather than a whole decision. A stack can be `condense`d into
  one expression per target and `expand`ed back, and the two must decide
  identically.
  **Its absence semantics deliberately differ from the core language**, and
  anyone reading rule output needs to know which they are looking at: a missing
  fact propagates and the result reports what it is waiting for, where gBASIC
  raises; equality against a missing fact is unresolved, where gBASIC answers
  `false`. What does *not* differ is invalidity — a mixed-currency comparison
  is still an error, and is reported as one rather than as a missing fact.
  Both `validate` and `evaluate` answer as **values**, never raises. A rule can
  call only `rules.registry()`'s whitelist and read only the context it was
  handed, so it cannot name anything in gBASIC.
  Proposal and full API: `docs/gbasic-rules-library-proposal.md`.
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
  **Argument order is Excel's** (`docs/finance_design.md` §2), because the
  answers get checked in a spreadsheet: `finance.pmt(rate, nper, pv)` is the
  payment that repays a loan, `finance.pv(rate, nper, pmt)` what a stream is
  worth today, `finance.fv(rate, nper, pmt, pv)` what it grows to,
  `finance.nper(rate, pmt, pv)` how many periods clear it, and
  `finance.rate(nper, pmt, pv)` the rate that balances them. Each takes an
  optional tail — `fv = 0` for a balloon balance and `timing = "end"` or
  `"begin"` — and either amount may be written as a bare `0`, taking its
  currency from the other, so a lump sum is `finance.fv(0.05, 10, 0, cost)`
  exactly as a spreadsheet writes it.
  `finance.npv(rate, flows)` discounts an array of money one period apart, and
  `finance.irr(values)` finds the rate that breaks even — `values[0]` is the
  period-0 flow, normally a negative outlay, as a spreadsheet lays them out.
  For flows on real dates rather than regular periods,
  `finance.xnpv(rate, values, dates)` and `finance.xirr(values, dates)`
  discount Actual/365 from the first date, which is Excel's definition.
  All three searches **scan for a bracket before bisecting**: testing only the
  endpoints assumes the curve crosses zero an odd number of times, and
  `[-1000, 5000, -6000]` has roots at 100% *and* 200% while being negative at
  both ends. Bisection rather than Newton, because it cannot diverge and a
  wrong rate is a plausible percentage someone would act on. Flows that change
  sign **more than once** admit more than one rate, so `irr` and `xirr` emit a
  **warning** naming the count — the answer is a real root, just not
  necessarily the only one; `on warning ignore` silences it once you have
  thought about it.
  **Rate conversion**, so "the rate for one period" is never a guess:
  `finance.periodic(nominal, m)` is one period of a nominal annual rate
  (division, which is what nominal means); `finance.effective(nominal, m)` is
  what it actually earns in a year — the **APY** of a quoted rate — and
  `finance.nominal(effective, m)` is its exact inverse;
  `finance.continuous(effective)` and `finance.from_continuous(r)` cross to and
  from continuously compounded rates. "12% compounded monthly" earns 12.6825%
  a year, so the two annual rates are different numbers and which one a quote
  means is not answerable from the number alone.
  `finance.accrue(balance, annual_rate, from, to, convention)` is simple interest
  on a balance for the days between two dates — the piece `lending` and
  `deposits` share, which lives here so neither owns it. The rate is **annual**,
  because the day count already carries the year.
  `finance.year_fraction(from, to, convention)` is the day count, with
  `"actual/360"`, `"actual/365"`, `"actual/actual"` (ISDA, weighting each day
  by the length of its own year) and `"30/360"` (US bond basis, including the
  end-of-February rules). **There is no default convention**: they disagree by
  enough to matter — the same two dates are `0.163889` of a year under
  Actual/360 and `0.166667` under 30/360 — and no one of them is dominant
  across products. `finance.schedule(rate, nper, pv)` returns one record
  per period (`period`, `payment`, `interest`, `principal`, `balance`) whose
  **final payment is adjusted so the balance lands exactly on zero**: every
  payment is whole minor units, those roundings accumulate, and a schedule
  using one figure throughout would end owing a few cents. Depreciation is
  `finance.sln(cost, salvage, life)` straight-line,
  `finance.syd(cost, salvage, life, period)` sum-of-years-digits, and
  `finance.ddb(cost, salvage, life, period)` double-declining balance, floored
  at salvage so an asset is never written below what it is worth.
- `fake` — fabricated but realistic data, pure gBASIC
  (`docs/fake_data_design.md`). **Every generator is a pure function of
  `(seed, index)`** — there is no stream object, because a record is a value
  and `s.n = s.n + 1` inside a function mutates a local copy. That makes
  generation **order-independent**: row 47 is the same row whether you asked
  for it directly or reached it by walking, so adding a field to one generator
  cannot shift everything produced after it. The RNG is gBASIC's own
  (xoshiro256, not libc), so output is identical across machines and builds.
  Values: `fake.person`, `fake.company`, `fake.address`, `fake.phone`,
  `fake.between`, `fake.pick`, `fake.weighted`, `fake.lognormal`,
  `fake.date_between`, `fake.business_date`, `fake.amount`. Datasets:
  `fake.customers(seed, n)` and
  `fake.invoices(seed, customers, n, from, to, currency)`, which are
  **consistent** rather than merely plausible — an invoice names a customer
  that exists, is dated on a business day inside its window, and has a total
  that is the *sum* of its lines rather than an independent draw. Amounts are
  **lognormal**, which is the shape real invoice values take and the reason
  they satisfy Benford's law; a uniform column cannot exercise a Benford
  detector at all. Names, emails and phone numbers are structurally incapable
  of naming a real person: RFC 2606 reserved domains and the 555-01xx range
  reserved for fiction.

  **`fake.plant(rows, spec)`** takes a clean population and puts a *known*
  defect in a *known* place — the one thing a hand-written fixture cannot give
  you at scale. It returns `{ rows, planted }`: the population, and one report
  record per anomaly (`anomaly`, `id`, `index`, `was`, `now`, `source`).

  ```basic
  rigged = fake.plant(ledger, { anomaly: "round_dollar", count: 12, at: 7 })
  ```

  The spec takes `anomaly`, `count` and `at` (the seed that decides which rows
  are chosen — required, since a plant nobody can reproduce is not a fixture),
  optionally `amount_field` / `date_field` / `id_field` (defaulting to `total`,
  `on_date` and `id`, which is what `fake.invoices` produces), `threshold` for
  `just_under`, and `avoid` — a list of row indices a second plant must stay
  off, so two anomaly kinds can be composed without the later overwriting the
  earlier.

  The five kinds: `round_dollar` (an amount replaced by the round figure a
  human reaches for at that size), `just_under` (an amount put strictly below
  an approval limit, the way a $4,950 invoice sits under a $5,000 one),
  `weekend` (an entry dated on a day the business was shut — plantable
  precisely because `fake.business_date` never produces one), `duplicate` (the
  same money to the same party a few days later under its own document
  number), and `sequence_gap` (rows simply removed, so the ids around them
  still run in order and nothing in the surviving data is wrong).

  **The planted rows carry no marker.** The report comes back separately and
  the rows stay ordinary rows of their neighbours' shape, because a marker
  field is a back door a detector could read — and a detector tested against
  data that labels its own anomalies has not been tested. For the same reason a
  planted duplicate gets an id *continuing* the population's own sequence, and
  a population whose ids do not end in digits is **refused** rather than given
  one that stands out.

  Like `accounting.post`, `plant` returns a new array rather than mutating: a
  mutate-in-place API would silently do nothing to the caller's value, since
  inside a function `append` and `remove` act on a local copy.

  **`fake.sample(seed, n, k)`** is the primitive underneath it: `k` distinct
  indices out of `n`, in row order, decided by the seed alone. Public because
  choosing a reproducible subset *without marking it* is exactly what a test
  needs to say "these twelve accounts are the bad ones".

  **`fake.portfolio(seed, n, spec)`** is the third Layer-2 dataset, after
  customers and invoices: a book of loan terms
  (`{ id, opened, principal, rate, term }`) over a `{ from, to, currency }`
  window, with lognormal principals, business-day originations and rates
  rounded to a real quotation step. It returns loan **specs**, not
  `lending.loan` values, which keeps `fake` free of a dependency on the finance
  stack — a test-data library that had to load `lending` to fabricate anything
  could not be used to fabricate input *for* it. The caller passes each spec to
  `lending.loan`, which is where the declared basis, waterfall and day count
  belong: they are the lender's policy, not a property of the borrower.
- `deposits` — deposit interest, crediting and certificates, pure gBASIC
  (`docs/lending_design.md` §7). A **separate library from `lending`** on
  purpose: their vocabularies barely overlap, and what they do share — simple
  interest over a day count — lives in `finance.accrue`, since a deposit does
  not borrow. **The account declares its `balance_method`** — `"daily"`,
  `"average_daily"` or `"minimum"` — which give different interest on the same
  activity; a large late withdrawal earns 28.77 under daily and 4.11 under
  minimum. (Daily and average-daily *agree* at a constant rate, and that is
  arithmetic rather than a bug: simple interest is linear in the balance. They
  part company under tiers.) **Compounding and crediting are separate**:
  interest is computed per crediting period and added at the end of it, and
  interest since the last crediting date is reported apart from the balance,
  because the holder has earned it and not been paid it.
  `deposits.tiered_rate(tiers, balance, "whole")` gives the single rate a
  balance earns; `deposits.tiered_interest(tiers, balance, from, to,
  convention)` is the per-portion product, where each slice earns its own tier
  like a tax bracket. They differ sharply at a boundary, which is why the mode
  is declared. `deposits.certificate(spec)` adds `term_days` and
  `penalty_days`, `deposits.matures(cert)` is its maturity date, and
  `deposits.redeem(cert, as_of)` returns
  `{ principal, interest, penalty, early, proceeds, principal_reduced }` —
  where **a penalty larger than the interest earned reduces principal** rather
  than being clamped, because clamping would report proceeds the holder will
  not receive.
- `lending` — loans, servicing and payoff, pure gBASIC over `finance` and
  `accounting` (`docs/lending_design.md`). `finance` answers what the payment
  *is*; this answers what happens next. **A loan declares its conventions and
  the library never assumes**, because each changes the answer and none has a
  dominant default: `basis` is `"amortized"` (one period's interest per period,
  whatever the days) or `"daily_simple"` (the actual balance for the actual
  days) — *different loans*, where paying five days early saves five days on
  one and nothing on the other; `waterfall` is
  `"fees_interest_principal"` or `"interest_principal_fees"`; `day_count` is
  any `finance.year_fraction` convention. `lending.apply(loan, events, history)`
  is a **fold** over `payment`/`fee`/`rate_change` events, so state is derived
  from the record of what happened and *why is this balance what it is* is
  answerable by replaying it; `history` is opt-in. `lending.payoff` adds
  per-diem, and `lending.period_rate(loan)` is the rate it accrues at, which is
  the annual rate divided by its own frequency. Underwriting is `ltv`, `dti`, `dscr` and `payment_to_income` — a
  **missing input returns `unknown`, never zero**, since these feed credit
  decisions and an absent income that became zero would make every ratio look
  perfect. `lending.entries(chart, loan, events, accounts)` **emits** journal
  entries and never posts them: the caller owns the ledger.
- `credit` — credit analytics over a **portfolio**, pure gBASIC
  (`docs/credit_analytics_design.md`). `lending` answers questions about one
  loan; this answers questions about a book — vintage curves, roll rates,
  migration, charge-off and recovery — none of which is derivable from a single
  loan or is arithmetic about balances. They are questions about **states over
  time**.

  **The input is a status table, not a list of loans**, and everything else
  follows from that: one row per loan per observation date,
  `{ id, opened, as_of, status, balance }`. Real portfolio data arrives as a
  monthly performance record per loan — the shape published loan-level datasets
  and any servicer's extract take — so a library that could only read our own
  `lending` loans could not be pointed at a real book.
  `credit.observe(book, dates, method)` is the **bridge** from `lending`, which
  makes our own machinery a producer of the table rather than a special case.

  **Delinquency is declared and the industry disagrees about it.**
  `credit.bucket(unpaid, as_of, method)` takes `"mba"` — 30 days delinquent the
  day after one payment is missed, counting *payments missed* — or `"ots"` —
  30 days delinquent when the oldest unpaid instalment is 30 days old, counting
  *days past due*. On a monthly loan they run about a month apart for the whole
  life of a delinquency, so a book reported one way is not comparable with one
  reported the other, and the method is required rather than defaulted.
  `credit.methods()` names the two; `credit.buckets()` is the declared ladder
  (`current`, `dpd_30`, `dpd_60`, `dpd_90`, `dpd_120_plus`, `paid_off`,
  `charged_off`) and a status outside it is refused rather than becoming a
  population of one; `credit.absorbing()` names the two states nothing rolls
  out of.

  **Attrition is a state, not a hole.** `paid_off` and `charged_off` are
  buckets in the matrix and nothing rolls out of them; a loan present at *t* and
  missing at *t+1* is reported as `unobserved` rather than dropped, because "we
  stopped seeing it" is a fact about the data, not the borrower. Dropping it
  makes a book look like it is curing — an ordinary-looking percentage that is
  wrong, with nothing raised. That gives the invariant the library rests on, the
  counterpart of the balance-sheet identity: **every loan observed at *t* is
  accounted for at *t+1***, row totals plus `unobserved` equalling the starting
  population bucket by bucket. It is never enforced anywhere; it falls out of
  correct bucketing.

  `credit.migration(table, from, to)` returns counts (plus `entered`, the new
  lending that is *not* part of the migration); `credit.roll_rates` divides by
  the **whole** starting bucket, and a bucket nobody was in reports `unknown`
  rather than zero. `credit.vintage(table, spec)` plots a cohort's **cumulative**
  bad rate against **age in months on book**, never calendar month — indexed by
  calendar, every cohort's curve starts at a different age and the result is a
  smooth, meaningless line that does not look like a mistake. A cohort's curve
  stops at the age it has reached, so an unlived age reads as absent rather than
  as 0%. `basis` is `"original"` or `"outstanding"` and is required, since they
  are different curves. `credit.losses` **reads** `charged_off` from the record
  rather than inferring it from days past due — a charge-off is a decision, and
  inferring it produces a loss figure the servicer's own books disagree with —
  and reports gross and recoveries separately rather than netting them silently.
Inside a library, an unqualified call resolves to **that library's own
function first**, before any other library, the root program, or a built-in.
Outside it, the library's functions need qualification (`lib.name`) and an
unqualified name reaches the built-in as usual. So a library that defines
`lines` calls its own from inside and does not change what anyone else means by
`lines`. A function whose name collides with a built-in produces a **note** —
not a warning — naming both sides of that boundary, because it describes a rule
rather than a hazard; `on warning ignore` silences it, and `on warning stop`
does not escalate it. `library_collisions()` reports the same information for a
whole program without needing a call site.

- `ldap` — bind and search against a directory (`docs/ldap_design.md`). A
  native module, like `sqlite` and `smtp`; `load ldap` raises cleanly in a
  build without libldap. Scope is deliberately two operations: this is
  authentication, not directory administration, and modify/add/delete,
  referral chasing, SASL, pooling and paged results are all out.

  `ldap.connect(config)` takes `host`, `port` (389 or 636 by default), and
  **`security`, which is required and has no default** — `"ldaps"`,
  `"starttls"` or `"plain"`. A default would make a cleartext bind either
  silent or accidental; requiring it means a cleartext bind is a word somebody
  typed and is greppable in a configuration review. `verify` is **true** unless
  explicitly set false, and `ca_file` names an intranet CA, which is the normal
  case here rather than an exotic one. **Referral chasing is disabled and is
  not configurable**: a referral is the server telling the client to go and ask
  a different server, which on an authentication path is an instruction to send
  credentials somewhere the operator never named.

  `connection.bind(dn, password)` **returns a value, and does not raise**
  for an authentication outcome: `{ ok, reason, code, message }` where `reason`
  is `""`, `"invalid_credentials"`, `"empty_password"`, `"unreachable"`,
  `"tls_failed"`, `"timeout"` or `"server_error"` — and **every operational
  failure arrives this way in every security mode**, including a StartTLS
  failure, which `connect` records rather than raising. *Wrong password* and *the directory is
  unreachable* are both ordinary on this path and must never be the same
  answer — the first is shown to a user, the second is an operator's problem
  and must not reach a viewer as a bad password — so a caller has to read
  `reason` to learn anything and cannot conflate them by accident. An **empty
  password is refused** rather than sent: LDAP treats it as an unauthenticated
  bind, the directory answers *success*, and a caller that did not know would
  have logged the user in.

  `connection.search(spec)` takes `base`, `scope` (`"base"`, `"one"` or
  `"sub"`), `filter`, an optional `attributes` list (empty or absent means all,
  as LDAP defines it — bound as an administrator that includes `userPassword`,
  so name the attributes you want) and an optional `limit`. It returns an array of
  `{ dn, attributes }`, and **attribute values are always an array**, even for
  a single value: `memberOf` is multi-valued and `cn` is not, and a caller that
  special-cases the two is correct until the day a user joins a second group.
  A search matching nothing is an **empty array, not an error**.
  `connection.close()` is idempotent.

- `reasoning` / `insight` / `decision` / `automation` — Business Automation
  Reasoning (`docs/automation_reasoning_design.md`). `credit` and friends answer
  questions about a domain; these answer *did anything happen, does it mean
  anything, what should be done about it, and whether the software may do it*.
  The architecture is closed end to end — a Finding becomes a Decision becomes
  a gated Action becomes a measured Outcome — but it is one function or so per
  layer.

  > **Experimental, and more so than anything else in this reference.** These
  > four are a **design laboratory** (`docs/automation_reasoning_design.md`
  > §15). Their twelve worked examples in `examples/automation_lab/` are
  > *experiments* whose job is to make a design claim measurable, not recipes
  > to copy; three of them refuted the design document that commissioned them.
  > Four things a caller should know before depending on this:
  >
  > 1. **The default null is anti-conservative.** `null: "siblings"` takes its
  >    threshold from the *t* distribution, which is exact only for
  >    light-tailed cell changes. Measured on lognormal revenue it delivers a
  >    family-wise false-positive rate of **0.10–0.14 against a requested
  >    0.05**, worsening as the search widens. `null.calibration` on every
  >    Finding says so. `null: "siblings_permuted"` measures 0.05–0.085 and
  >    costs about **60x** the runtime.
  > 2. **The correction covers one search, not a campaign.** A process that
  >    asks the same question every month runs twelve families and pays for
  >    one: measured, **0.725** that an ordinary year with nothing wrong in it
  >    raises a finding. Declare `repetitions:` and it falls to 0.175. Even a
  >    perfectly calibrated 0.05 per run is 0.46 over twelve.
  > 3. **Every measurement behind these libraries is on generated data.** They
  >    have never been pointed at a real business.
  > 4. **The value shapes will move.** This is the surface most likely to
  >    change between releases.
  >
  > What is *not* provisional is the refusals. Eighteen of them
  > (`automation_reasoning_design.md` §8) exist because a measurement
  > contradicted an expectation, and each prevents a specific plausible-looking
  > wrong answer rather than a crash.

  `reasoning` owns the shared value model and has no behaviour beyond
  construction and validation, because all three planned layers construct and
  inspect these values and none should depend on another to do it.
  `reasoning.finding(spec)` builds and **validates** a Finding, and its
  refusals are where the design's corrections live rather than in prose: a
  Finding may not carry `materiality` (it needs objectives, objectives belong
  to the decision layer, and putting it here would force `insight` to know the
  business's goals), may not carry a `cause` (it carries `associations`; a
  hypothesis becomes an explanation only by passing a recorded test), may not
  carry `assurance` (that is the decision layer's), and **must** carry
  `search.cells` and `search.width`, since the significance cut is a function
  of how wide the search was and a Finding that cannot state it cannot be
  judged.

  `reasoning.nulls()` and `reasoning.comparisons()` are the declared choices —
  what counts as *ordinary*, and what this period is being compared against —
  neither of which is inferred, because each changes every answer downstream.
  `comparisons()` returns `period_over_period` and `versus_last_year`, and the
  second is not decoration: compared December to January over cells with
  ordinary seasonal profiles, a run with a real 45% collapse and a run with
  **nothing wrong** give the same leader and the same *z* to two decimals — a
  shift shared by every cell inflates the spread each cell is judged against in
  proportion to itself, so seasonality causes *blindness*, not false alarms.
  On the same data and threshold, `versus_last_year` recovers the planted cell
  at *z* −4.91, ranked first. The remedy is the comparison, not the null.
  `reasoning.confidence_kinds()` names three quantities that are easy to
  confuse: `confidence` (how well a quantity is estimated), `support` (how well
  a hypothesis accounts for the evidence) and `assurance` (how sure we are an
  action is right). `reasoning.compare_confidence` **refuses** to compare two
  of different kinds rather than coercing them — they are all numbers in 0..1
  and nothing else would complain. `reasoning.provenance(spec)` is deliberately
  clock-free, so a Finding is reproducible; a caller who wants the run stamped
  passes `as_of`. `reasoning.provenance_complete(finding)` returns what is
  missing, so completeness is checkable structurally rather than by reading.

  `reasoning.context_fields()` names the fields a `Context` carries
  (`objectives`, `thresholds`, `authority`, `approval`) and
  `reasoning.check_context(context, who)` raises unless a context uses only
  those — which `decision.evaluate` and `automation` both call, so the two
  layers that read one context cannot disagree about what a field is called. An
  unrecognised name is **refused rather than ignored**, and the reason is
  measured: a context written with the singular `objective` and `threshold`
  produced a decision with `direction: "unstated"` and `materiality: unknown`
  and raised nothing — and `materiality: unknown` is the *designed* honest
  answer when no threshold was declared, so a typo was indistinguishable from a
  deliberate omission. The decision layer accepts `approval` even though only
  `automation` reads it, because one context serves both.

  `insight.weigh(finding, hypotheses)` is §4's third rung, and is built so it
  cannot reach the fifth: every `reasoning.hypothesis` carries
  `explains: false` for its whole life, because only a recorded test could
  promote one. A hypothesis declares which cells it **predicts** (a record of
  dimension → value constraints — declarative rather than a function value, so
  the prediction can be written into provenance and audited later) and a
  **discriminator**, which is required: a hypothesis nobody can imagine an
  observation for is a story, and a story scores against a pattern exactly as
  well as an explanation does. `weigh` compares each prediction with the cells
  that actually cleared and reports a contingency — `predicted`, `hit`,
  `over_predicted`, `unexplained` — plus set agreement, which travels with its
  own definition in `agreement_is` and is explicitly **not** a probability that
  the hypothesis is true. Parsimony is not imposed: a hypothesis predicting fifteen cells and
  explaining one has over-predicted fourteen, and agreement says so. Two
  hypotheses that predict **the same cells** are reported in
  `indistinguishable` and are not ordered against each other, since ranking
  them would invent a preference the evidence does not support; `next_test`
  then names both discriminators. Weighing against a finding where nothing
  cleared is refused — there is no pattern to explain, and ranking against
  nothing would order stories by how little they claim.

  `decision.quantity(spec)` is the decision layer's **second shape**.
  `evaluate` chooses among a list; a price, a reorder level or a staffing
  number has no list — it has a model mapping an estimated parameter to the
  answer, and the model carries the parameter's interval through in a way that
  is usually not proportional. `spec` names the `model` (required, because that
  is what a reader has to be able to argue with), a `parameter` with
  `estimate`/`low`/`high`, and a `map` function value returning the quantity or
  `unknown` where the model does not apply. It **walks** the interval rather
  than checking its endpoints, since a break anywhere is a break, and where one
  is found there is no recommendation at all — plus
  `point_estimate_would_say`, because that number is the most
  confident-looking wrong answer available. Otherwise it reports the answer's
  own interval and `amplification`, with `amplification_is` beside it: how much
  the model magnifies the parameter's uncertainty, where 1 is proportional and
  below 1 means it damps. Measured on a pricing model, `p* = cost*b/(1+b)`
  divides by `1+b`, so an elasticity interval a shade over 1.5x wide becomes a
  price interval **13.5x wide** — amplification 8.85 — while a point estimate
  names a single price with a straight face.
  Note that `map` cannot capture anything from its caller — gBASIC has no
  closures — so a caller needs one function per model *instance*.

  `decision.calibrate(evidences)` closes the cycle: it takes
  `reasoning.as_evidence` results — and **nothing else**, so R10 has already
  refused anything uncontrolled — and returns an *interval*, not a number. One
  observation is refused: an interval needs two, and a single measurement
  offers no way to tell a real effect from the one time it happened to work.
  Passing the result to `decision.evaluate` as `calibration` replaces the
  `sensitivity_range` a caller would otherwise have to invent, so `assurance`
  becomes the share of the range **the evidence supports** over which the
  recommendation survives. That also answers *how much evidence is enough*,
  which has no answer in the abstract and a precise one relative to a decision:
  enough is when the interval stops straddling the point at which the
  recommendation changes. It is a fact about distance from that boundary rather
  than about sample size — two observations can settle a clear-cut case and
  thirty can leave a marginal one open.

  `automation` is **the only layer that changes external state**, and it changes
  nothing itself: the executor is a function value the caller supplies, and it
  is called only past the gate. `automation.execute(decision, context,
  rehearsal, executor)` enforces two refusals. A process that has never been
  replayed against history may not act at all — `automation.rehearsal(spec)`
  records a replay and `authority.min_rehearsal_periods` says how much is
  required — and this is checked **before** authority, because a human asked to
  approve an unrehearsed process has nothing to approve on. Then authority, the
  half `decision` only *states*: a recommendation beyond the delegated limit
  needs a named `approval` in the context or it is refused.
  `automation.would(decision, context, rehearsal)` is the dry run; it takes no
  executor, so it cannot act by construction, and it **shares one gate** with
  `execute` — if the two could disagree, a rehearsal would describe a different
  program than the one that runs. `automation.observe(action, measured)`
  returns the action with its outcome attached (a record is a value, so
  mutating the caller's copy would silently do nothing).

  Two refusals govern what an outcome may then be used for.
  `reasoning.as_evidence(action)` rejects an action whose outcome was never
  measured — *we did this before and it worked* is otherwise a memory entering
  the system as a fact — and rejects a measured outcome that carries **no
  comparison**, because an action is taken precisely when a measure is extreme
  and extremes revert on their own: an intervention with a true effect of
  exactly zero was measured showing a 48.7% apparent "recovery". Supply
  `holdout` in the measurement and the outcome carries `controlled: true` and
  an `effect` that is the *difference*, or read it with
  `reasoning.as_observation(action)`, which returns it labelled
  `uncontrolled: true` with the caveat in the value rather than in a comment.
  `automation.assign(key, holdout_rate)` is how a holdout is produced at all —
  the concept of deliberately **not** acting, without which a system that
  always acts when it should can never learn whether acting helps. It is
  deterministic in the key rather than random, so a replay makes the same
  assignments as the live run and an operator asking *why was this one held
  back* gets an answer; a zero rate is allowed but says what it costs, and
  holding everything back is refused. `reasoning.action` and
  `reasoning.outcome` validate the two values.

  `decision.evaluate(finding, context, alternatives, spec)` is the whole of
  `decision` so far. It scores **every** alternative — not just the affordable
  ones — picks the best by expected value, and states the authority that choice
  needs in `authority_required` without enforcing it, because enforcement
  belongs at the action: a layer that quietly recommended the best *affordable*
  option would hide the only choice a human needs to make. Materiality is
  computed here, from the context's thresholds, and answers `unknown` rather
  than `false` when no threshold is declared. `assurance` is a **sensitivity**,
  not a feeling: the share of a declared `sensitivity_range` (a multiplier on
  the assumed recoveries, which must bracket 1) over which the recommendation
  survives, with `sensitivities` naming where it flips.

  Because a bare number in 0..1 printed beside a recommendation reads as *the
  probability that the recommendation is right*, `assurance` never travels
  alone: `assurance_is` carries its `definition`, what was `swept`, the range it
  was swept `over`, **where that range came from** (a calibration naming how
  much evidence it rests on, or a span the caller declared — assurance 1 over
  those two is not the same claim, and nothing else distinguished them), the
  `steps`, what was `held_fixed` *together with the sized-off quantity's value*,
  and an `is_not` saying outright that it is not a probability. Writing that
  definition down is what exposed its limit: the sweep moves the recovery
  assumption and holds everything else at a point estimate, so an answer can be
  `assurance 1` and still flip if the quantity it is a fraction *of* is a third
  smaller. The remedy chosen was **disclosure, not a wider sweep** — the cell's
  change is observed rather than estimated, and inventing an interval for it
  would be inventing a statistic. `decision.sizings()`
  names what a decision may be sized off — `aggregate` or `leading_cell` — and
  sizing off one the Finding did **not** establish is refused, since the same
  intervention over the same data picks opposite actions depending on which is
  taken. `reasoning.decision(spec)` validates the result and refuses a
  `confidence` (that is the insight layer's) or an `authorized` field (a
  decision does not decide its own permission).

  `insight.explain_change(frame, spec)` is the whole of `insight` so far. It
  decomposes a change in a measure across an ordered list of dimensions and
  returns a Finding. Three things make it more than a group-by. The
  **reference distribution is the sibling cells** — the other cells of the same
  decomposition are a sample of ordinary movement, so no external baseline is
  needed. The **threshold is derived from the search width**, as a two-sided
  family-wise quantile for a declared `alpha` (0.05 unless given), because a
  drill-down is a *search* and a search always returns a winner. And a
  **contribution share is withheld** when the net change is not distinguishable
  from zero, with `shares_withheld_because` saying so — a share of a change
  that was never established is not a number, however computable it is.
  `associations` names other measures that moved with this one, and each is
  reported as `moved with` and `explains: false`.

  Three fields of its spec are choices a caller has to make rather than
  defaults to accept.

  `search.width` is **required** — the significance cut is a function of how
  wide the search was, so a decomposition that cannot state its width cannot set
  its own threshold. `repetitions` (default 1) declares how many times this same
  search will be run: the correction is family-wise over the **cells of one
  search**, so a monthly process runs twelve families and pays for one.
  `max_causes` (default 1) says how many cells may be wrong at once, and it
  excludes the other candidates from the **reference** — not blessing them as
  findings, merely declining to let them define *ordinary*. Without it the test
  finds a problem only while it is nearly the only problem: measured, holding
  one cell literally constant and varying only how many unrelated cells
  collapsed beside it, its *z* fell −5.70 → −3.65 → −2.83 → −1.86 and its
  verdict went from found to nothing, because both the reference mean and the
  reference spread move at once. Trimming holds it flat at about −5.5. It is
  **not free**: the bar rises 4.35 → 4.84 → 6.46, so by five causes in
  twenty-four cells it has outrun the evidence. Above 1 it **requires**
  `null: "siblings_permuted"`, because trimming changes the statistic and the
  *t* quantile is a formula for the untrimmed one.

  Every Finding carries `search.detectable` — `change`, `typical_cell` and
  `share` — **whether or not anything cleared**, and that is the point:
  *nothing cleared* is precisely when a reader cannot tell an incapable search
  from a healthy business. Measured over one business of fixed size cut 24 /
  240 / 1200 ways, the threshold moves only 3.49 → 3.77 → 4.11 (paying for
  multiplicity is nearly free) while the smallest detectable change goes 18% →
  53% → **147% of a typical cell** — so at the finest cut a cell could fall to
  zero and the answer would still be `within ordinary variation`, in exactly the
  words used when a business is healthy. What dominates a wide search is
  support, not multiplicity.

  A share of a change is refused when the movement is **common to the
  population**, decided by a sign test, which assumes less than the *t* test
  beside it: 22 of 24 cells moving the same way is one-in-thirty-thousand under
  a null where a cell is as likely to rise as fall. Without that refusal an
  ordinary January produces a confident attribution to whichever cell sells the
  most toys. It is deliberately **not** a seasonality detector, and the message
  says so — a real company-wide collapse and a broken data feed move 22 of 24
  cells down alike, and what is established is one level up: the decomposition
  has not located this.

- `scoring` — credit scorecards, pure gBASIC over `stats`
  (`docs/scoring_design.md`). `credit` measures what a book has already done;
  this turns a population into a model that **ranks** risk and then into the
  artefact a credit committee approves: a table of attributes and points. It is
  deliberately the credit-specific half only — `stats.logistic_regression` is
  the engine and is not reimplemented here.

  **One coding is fixed and everything else is declared:** an outcome of `1`
  means **bad** (the event being predicted). Fixed rather than declared because
  it appears in every call, and it is validated, so a column meaning the
  opposite fails loudly instead of inverting the model.

  `scoring.bin_numeric(values, outcomes, cuts)` cuts a predictor at declared
  ascending upper edges; an unknown value gets **its own bin** rather than being
  dropped, since missing is predictive and dropping it also silently changes
  every other bin's denominator. `scoring.bin_categorical(values, outcomes,
  groups)` groups a categorical predictor and **refuses** a value in no group
  rather than sweeping it into an "other" bin — an unlisted category absorbed
  is a category nobody decided about.

  `scoring.woe_table(bins, spec)` computes Weight of Evidence and Information
  Value. `spec.orientation` is required and `scoring.orientations()` names the
  two: `"good_bad"` gives higher WOE to better risk, `"bad_good"` is its
  negative. **They differ by sign and nothing else**, so the wrong one produces
  a scorecard with the same discrimination ranking perfectly backwards while
  looking entirely normal. IV is orientation-independent, which falls out of the
  formula rather than being arranged. A bin with no goods or no bads has an
  infinite WOE and is **refused by name**; `spec.smoothing` accepts it, but is
  the caller's declaration because smoothing **invents evidence** in proportion
  to how small the bin is, and the output does not show that.
  `scoring.woe_of(table, label)` looks a bin up, and refuses a label the table
  never saw.

  `scoring.auc(scores, outcomes)` uses the Mann–Whitney rank identity with ties
  taking half credit, so it is exact rather than a trapezoid over a sampled
  curve. Scores run the scorecard way — **higher means lower risk**. An **AUC
  below 0.5 means the model is backwards, not weak**: it is returned as it
  stands with `reversed: true` rather than flipped, because flipping silently
  turns the most consequential error in this work into a mediocre-looking
  result that gets deployed. `gini` is `2 * auc - 1`.
  `scoring.ks(scores, outcomes)` is a **different** quantity — the largest gap
  between the cumulative good and bad distributions — so two models can rank one
  way on AUC and the other on KS; it is reported separately, never as "the"
  statistic.

  `scoring.scaling({ base_score, base_odds, pdo })` builds the point scale,
  where `pdo` is *points to double the odds* — the number the business agrees
  to. All three are required; a default would silently redefine every cut-off
  downstream. `scoring.points_of(scaling, log_odds)` and
  `scoring.log_odds_of(scaling, points)` are exact inverses, and `pdo` more
  points doubles the odds exactly.

  `scoring.psi(expected, actual)` is the Population Stability Index over
  matching bands — the number that says the model has not changed but the
  applicants have. Its customary thresholds are returned as a **label and a
  stated rule of thumb**, never as a verdict, and a band empty in one population
  is refused rather than contributing an infinite index.
- `nlq` — a question over an estate nobody can hold in their head
  (`docs/nlq_design.md`). **First increment: retrieval and grounding, with no
  model and no SQL**, mirroring `discovery`'s own "declared facts only" choice.
  `nlq.ground(catalog, question [, options])` takes a catalog of
  `{ tables, columns }` — what `discovery.scan` produces — and returns the
  objects the question is about: `tables` (ranked, cut at `limit`, default 8),
  `detail` (each with its `score` and the `why` that earned it),
  `near_misses`, `unresolved`, and a `search` record carrying `width`,
  `matched`, `limit` and `cut`.
  **It reports its own width because it is a search**, and a search always
  returns a winner: hand a model twenty badly-chosen tables and it writes
  flawless SQL about the wrong ones, and the SQL runs. `near_misses` is what
  lets a reader see a close call, and `unresolved` — the question's own words
  that matched nothing — is the likeliest place a grounding is about to be
  confidently wrong.
  `options.synonyms` is a record of term → array of terms, **declared by
  whoever owns the estate and never derived**: guessing that `rpt` means report
  is a search over the whole schema with no null model, right often enough to
  be trusted and wrong silently. `unresolved` names exactly which words to
  declare, so the loop closes without anything being invented.
  `nlq.terms(text)` is the tokenizer (stopwords dropped, crude singularisation
  applied to **both** sides so it need only be consistent, not correct);
  `nlq.stopwords()` is that list — `total`, `gross` and `net` are deliberately
  absent, being half the measure names in a warehouse. `nlq.check_catalog(cat)`
  refuses a catalog missing `tables` or `columns`.
  `nlq.from_discovery(cat)` converts a catalog from `discovery.scan` or
  `discovery.estate` — which keys its objects **by id** — into the rows this
  library takes, carrying any `notes` across. Conversion is explicit rather
  than `ground` accepting either shape, because a function that guesses which
  of two shapes it was handed will one day guess wrong. **The id is preserved
  exactly**, which is the whole difficulty: a discovery id is
  `source.catalog.schema.table` with empty parts dropped, so its depth varies
  by driver, and a note written by `discovery.annotate` against that id must be
  findable under the name a grounding produces, or the disclosure it exists for
  silently stops appearing.
  `ground` also returns **`ambiguous`** — what the catalog *cannot settle*, which
  it reports rather than resolves — and `nlq.check_answerable(g)` **refuses** on
  it. **`plan` refuses on both ambiguity and a grounding that found nothing**,
  so `plan.ok` means *worth asking a model*: it used to answer `ok` with a
  prompt naming no tables, which fails safe (`check_sql` refuses whatever the
  model invents) but fails **expensively**, after the model was paid for, and
  reports a SQL problem for something that was never about SQL. A refused plan
  carries the same fields as an accepted one, so a caller can read `tables`
  without checking `ok` first. The split is deliberate: a caller may want the grounding in order to show
  a person the candidates and ask; what must not happen is a number produced
  from it. The case that fires is **the same object name in schemas that differ
  only by a number** (`staging` / `staging_2` / `staging_3`), where nothing in
  the catalog says which is live. `trading.deal` versus `trading_apac.deal` does
  **not** fire, because `apac` is a word a question can use — without that
  discriminator the refusal fired on 15 of 16 benchmark questions, which is
  indistinguishable from having no tool. A tie at the cut is disclosure
  (`search.cut_tied`), not a gate, since the needed table is usually inside it.
  **Value vocabulary** answers the one thing a catalog cannot:
  `nlq.vocabulary(rows, { max_values: 25 })` folds `{schema, table, column,
  value}` rows into the distinct values of each low-cardinality column, and
  `nlq.check_literals(g, vocab, question)` reports where the question's wording
  differs in case from what the column holds. That is a measured failure, not a
  hypothetical: the first real model call produced
  `WHERE account_status = 'open'` against data that says `'OPEN'` — perfect SQL
  returning 0 rows with no error — and supplying the vocabulary is what fixes
  it, measured on the same model at temperature 0: without it `'open'`, with it
  `'OPEN'`. A column that **overflows** `max_values` gets
  no vocabulary rather than a truncated one, because a partial list would report
  a real value as unknown when it is merely unlisted.
  **The three steps an application drives** — `nlq.plan(cat, vocab, question,
  options)`, `nlq.interpret(plan, model_text, options)`,
  `nlq.settle(reading, rows)`. **None performs I/O**, and that is the design:
  a question costs seventeen to sixty seconds of model time plus whatever the
  query turns out to be, and a library's job is to let an application be
  graceful about that rather than be graceful on its behalf. A single
  `answer(catalog, conn, question)` would have made the decision by hiding it —
  it blocks, and every consumer inherits blocking. The shape is `agent`'s,
  which solved the same problem here for the same reason. The application calls
  the model between `plan` and `interpret`, and runs the SQL between
  `interpret` and `settle`, however it likes: blocking, on a worker, parked on
  a stream, or emailed tomorrow.
  A plan carries `estimated_tokens` so an application can warn before
  committing, **survives `encode`** so one that answers later can store it, and
  carries a **`key`** so one that wants a cache has something to key on — NLQ
  facilitates a cache and does not own one. The key covers the grounded objects
  **with their columns**, the vocabulary they contributed and the synonyms in
  force, because keying on the question alone leaves cached SQL that still
  *runs* after an import drops a column and quietly answers about the old
  shape. A refused grounding yields a plan with `ok: false` and
  `refused_because` — a value, not a raise, so a caller can show a person the
  candidates and ask.
  `nlq.options_from(cat, options)` reads `synonyms`, `derived_from`,
  `not_modelled` and **`notes`** (built from each object's `means` and `unit`)
  **from an annotated `discovery` catalog**, so a caller using
  `discovery` keeps one copy of facts that belong to the estate rather than to
  any one question. An **explicit option always wins** — a caller passing one
  has said something more specific than a standing note. A catalog with no notes
  adds nothing. The options below still work unchanged for a caller who has
  facts and no catalog machinery.
  **`options.notes`** is a record of object id → a sentence about what a column
  *means*, rendered into the prompt under *"What the columns mean"*. It exists
  because neither a vocabulary nor an exemplar can state a **unit**. Reported by
  `gdash` with a measurement: money materialised as integer minor units, so
  `amount` holds `125075` meaning `$1250.75`, and *"orders over 1000 dollars"*
  produced valid read-only SQL against the right table that was **wrong by a
  factor of a hundred**. Vocabulary shows `125075` is a real value; an exemplar
  shows what an integer looks like; neither says the scale — and on SQLite there
  is no type error to catch it. This is R2's shape by another road: not two
  objects that both answer, but **one object answering in units nobody stated**.
  `discovery.annotate`'s `means` and `unit` are the place to write it, so the
  note also serves documentation and a lineage walk.

  `options.not_modelled` is an array of terms the estate holds nothing about —
  `job`, `schedule`, `owner`. A question naming one is refused as
  `not_in_the_catalog`, which is R6's other half: some facts are not in the
  database at all, and no query can supply them. **Declared**, because there is
  no catalog-only signal — measured, counting unmatched words does not separate
  the cases, since an answerable question can leave more of them unmatched than
  an unanswerable one. **The guard matters as much as the list**: a term counts
  only when it also reaches nothing, so an estate with a real `job` column
  models jobs whatever the list says. Fires on 2 of 2 unanswerable benchmark
  questions and 0 of 17 answerable ones.
  `options.collapse_siblings` (default **true**) folds objects whose names differ
  only by a trailing `_NNN` into one candidate, reporting the rest under
  `siblings`. Measured on a 517-object estate: without it, recall at `limit: 8`
  is 10 of 16, because `archive.gl_account_2019` and its four numbered copies
  score identically and fill six of eight slots; with it, 13 of 16 — the same a
  121-object estate gets at the same limit. The rule is narrow on purpose:
  `fact_volume_daily` and `_hourly` are granularity *variants*, different tables
  answering different questions, and folding those would lose a real candidate.
  `options.derived_from` is a record of object → the object it is built from,
  and produces **`alternatives`** on the grounding: where two grounded objects
  hold the same measure and one is *derived* from the other, their totals can
  differ and the query cannot say which was meant. Measured, that is a 36% gap
  on one benchmark question. **It discloses and does not refuse** — built first
  as a blocker it stopped 8 of 16 questions, because for one the two
  derivations differ and the distinction *is* the answer while for another they
  agree exactly, and NLQ cannot tell those apart without running both. Like
  everything else here, derivation is **declared**: an application that
  performed an import knows what it imported and from where, and
  `discovery.lineage` can produce it for anyone holding module bodies — so
  grounding a question does not drag an ODBC catalog reader in behind it. The
  discriminator is derivation and **not** shared columns: `trading.deal` and
  `trading_emea.deal` share every column and are peers, so they are never named.
  `nlq.exemplars(rows, { min_distinct: 2 })` gives **one sample value** for the
  columns `vocabulary` refuses as too many — its complement, not its fallback: a
  vocabulary says which values exist, an exemplar says what one *looks like*.
  The **longest** value is chosen, because a format is best shown by its fullest
  instance; picking `'1'` from a column that also holds `'0000000001'` teaches
  exactly the wrong lesson, which is the measured failure it exists for.
  `nlq.prompt(g, cat, vocab, question, { budget_tokens: 3000 })` builds the
  messages for a model — the grounded tables with their columns, the value
  vocabulary for those tables, and the question — and **refuses** when the
  estimate exceeds the budget. That refusal is the whole point: Ollama serves
  a model at its own `num_ctx` and silently truncates beyond it, discarding
  the **head**, so an over-budget prompt loses its schema while keeping its
  question and the model invents table names. A refusal names a budget; a
  truncation names nothing. The estimate is characters over a declared
  `chars_per_token` (2.5 by default, measured) — deliberately crude, because
  the budget is a safety margin and being approximately right on the
  conservative side is the whole job.
  `nlq.check_sql(sql, g, { allow: [...] })` enforces **R3 and R4** on generated
  SQL and returns the problems rather than raising: `not_read_only` for anything
  that would change the database (structural — the prompt *asks* for a SELECT,
  and an instruction is not enforcement), and `ungrounded_table` for a query
  naming a table the grounding never surfaced. That last one is a hallucination
  that happens to be spelled correctly, and it **runs** if the name exists —
  which in an estate holding `deal`, `deal_2015`, `stg_deal` and `dim_deal` it
  very well might. The check is **lexical and says so**: it reads the
  identifiers after `from` and `join` and is not a SQL parser.
  A table-name match outweighs a schema-name match, which outweighs a
  column-name match; ranking is **total** (score, then id), so a driver's row
  order cannot decide the answer. Scored against `estateforge`'s independently
  computed `touches[]`: 13 of 16 questions lexically, 14 with declared
  synonyms — see `tests/run_nlq.sh`, which asserts recall **and** a ceiling on
  how much was selected, because recall alone is maximised by returning
  everything.
- `finio` — financial-format adapters (`docs/financial_adapters_design.md`;
  worked recipes in `docs/finio_cookbook.md`).
  **Phase 0 fixed the value model and the format registry; Phase 1 is the
  framework — adapters, recognition, resolution, validation and loss — and
  `finio_nacha` below is the first adapter it carries.**
  `finio.open_text(text, { format:, revision: [, origin:, framing:,
  record_length: ] })` **retains the source** (Axiom 1) and records where each
  record begins. **Offsets and slices are BYTES, never codepoints**, which is a
  correction to Phase 0 rather than a refinement of it: Phase 0 accumulated
  offsets with `len` and sliced with `mid`, so the field it called
  `byte_offset` stopped being one as soon as a record carried a non-ASCII byte,
  and in a fixed-width format the extraction itself moved — one UTF-8 É inside
  a 22-byte name field shifts every later field one place left, and a 15-digit
  trace number comes back as an ordinary-looking 13-digit one. Its own suite
  could not see it, because its fixture is pure ASCII.
  **`framing` is evidence, not a setting.** `"lines"` (the default) splits on LF
  and remembers whether each record ended CRLF or LF; `"terminated"` with a one-byte
  `record_terminator` ends a record at that byte **or** at a newline, whichever
  comes first — BAI2's slash, where a line may carry two whole records and a
  text-bearing record may carry no terminator at all. `"fixed"` with a
  `record_length` cuts a source with **no separator at all** — how a great many
  real fixed-width files arrive off a mainframe, and which a newline-assuming
  reader sees as one enormous record. Each record's terminator is retained, so
  `finio.source_text(src)` gives back exactly what arrived and a writer can
  reproduce the file it read rather than a normalised version (§17).
  `finio.layout(fields)` validates a fixed-width layout of
  `{ concept:, offset:, length: }` (offsets are **0-based**; published format
  guides number the same positions from 1, so a layout transcribed straight out
  of one is off by one everywhere), held **once per format** rather than per
  record, and `finio.concepts(lay)` lists its names. `finio.render(lay,
  values)` (*since 0.11.0*) writes one record from the same layout: every
  concept given as text of exactly its field's width, a value too short or too
  long **refused, never padded or cut**, so a payer originating its own file
  uses the layouts reading does. `finio_nacha.addenda_layout_for(raw)` picks
  the addenda layout by type code (05, 98 for a notification of change, 99 for
  a return), and return and NOC transaction codes (21, 26, 31, 36, 41, 46, 51,
  56) have a direction. A camt entry reads `debtor`, `creditor`, `remittance`
  (the `Ustrd` lines), `referred_documents` and `account_servicer_ref`, its
  `counterparty` is chosen by direction (the debtor on a credit), and
  `TxDtls` content it does not interpret is a loss note.
  **A location is a value with a `kind`**, and that is a correction Phase 1
  shipped without: §4 asked for "a generic source-location abstraction" from the
  beginning, and every location `finio` could make was
  `{ record, byte_offset, byte_length }`, because the only adapter was
  fixed-width and nothing pushed back. `finio.location(kind, detail)` builds one
  over §4's own list — `finio.location_kinds()` gives `fixed_width`,
  `delimited`, `spreadsheet`, `xml` and `json` — and **checks the fields per
  kind**, because a location whose shape depends on who built it is one a
  consumer has to guess at. An `xml` location requires a `path` **and an
  `occurrence`**: the path alone names every element at it. A `line` is optional
  there — the streaming reader always carries one, and the DOM parser does when
  asked (`positions: true`, *since 0.2.2*; before that it could not) — and an
  adapter without one is not made to invent it. `finio.describe_location(loc)`
  renders any of them, so a report mixing representations reads consistently
  rather than each adapter inventing a phrasing.
  `finio.coverage(lay, record_length)` reports the `gaps` and `overlaps` a
  layout leaves — **a dropped field is silent loss (Axiom 8) and looks like
  nothing**, since the other fields still read correctly and the record still
  validates; it shows up here as arithmetic.
  `finio.source_value(src, lay, record, concept)` computes a field's provenance
  on demand — `{ raw, location { record, byte_offset, byte_length }, format,
  revision }` — and `finio.semantic_value(concept, value, sources,
  transformations)` is §5's normalized value, whose `sources` is **plural and
  required**, because a revision may compose one concept from several fields and
  a value with no source has no provenance.
  **Computed, not stored, and that was measured rather than preferred**: holding
  provenance per value costs 2.79 GB for a 9.5 MB file and is also the slowest
  to answer, while retaining the source costs 54 MB and about two microseconds a
  query more than the per-record alternative (§21 records the table).
  `finio.read_field(src, lay, record, concept, kind)` — kind `"text"` or
  `"digits"` — answers `{ status, value, raw, source }` where **`status` is
  `"ok"`, `"unknown"` or `"invalid"` and those are three different answers**
  (Axiom 7): a blank field is unknown, a present field that fails its own rule
  is invalid, and an unknown's `value` is `unknown` and never zero.

  **An adapter is a value, and so is the set of them.**
  `finio.adapter({ id:, revisions:, recognise:, read: [, validate:, write: ],
  registry_entry:, byte_fidelity: [, semantic_fidelity: ] })` checks the shape
  — `recognise` and `read` must be **function values** (`lib.fn` is one), at
  least one revision must be named, the registry entry is checked where it is
  declared rather than where it is first doubted, and **`byte_fidelity` must be
  stated** because §17 says every adapter documents which round-trip guarantee
  it can provide and leaving it unsaid is how a consumer comes to assume the
  stronger one. `finio.registry(adapters)` is the set, and
  `finio.find_adapter(reg, id)` names what the registry holds when it misses.
  **The registry is a value the caller holds, not a global** — gBASIC's actors
  are fork+exec, so a registration performed in a parent is not the child's, and
  a global would work in a script and quietly hold nothing in a worker; a value
  also lets two coexist, one pinned to the adapters an archive was imported
  under and one carrying today's. The cost is that §7's one-argument
  read takes a registry argument, and that is the whole cost.
  `finio.identify(reg, text)` reports `{ classification, candidates, why }`
  where `finio.classifications()` gives §7's ordinal vocabulary — `exact`,
  `strong`, `possible`, `ambiguous`, `unknown`. **Deliberately not numeric**:
  where the evidence is a fingerprint a file either carries or does not,
  dressing that as a confidence claims a measurement nobody made. **Two
  adapters claiming one source is `ambiguous`, never the stronger claim** —
  they disagree about a fact and the resolution is evidence the framework does
  not have.
  `finio.read_text(reg, text, options)` and `finio.read_file(reg, path,
  options)` resolve in §7's order — an explicit `adapter`, then recognition —
  and **raise where they cannot**, because a document is what every consumer
  downstream trusts and a guessed one is indistinguishable from a read one. The
  document carries the `adapter`, the `revision`, the `classification`, the
  `reasons` it was resolved that way, the retained `source`, the `records`, the
  adapter's `entities`, and `loss`. **An adapter may honestly not know its
  revision**: §7's diagram assumes one can always be settled from evidence, and
  for a format whose record layout has outlived twenty rule books it cannot —
  the adapter's first declared revision is used and the reason says the source
  carried none, rather than a year being invented that would then travel in
  every document written. (`read_file`, not `read`: `read` is a built-in, and a
  library function of that name shadows it for every unqualified call inside
  the library, so `finio.read` would call itself and `scan` could not read a
  file at all — `discovery` made the same choice for the same reason.)
  `finio.validate(reg, doc)` is **a different operation from reading** (§15).
  Reading preserves and explains; validation judges. A file whose control
  totals disagree with its entries is the most operationally important thing an
  ACH shop can be told and is **not** a reason to refuse to read it. An issue
  carries `code`, `severity` and `message`, and severity is `error`, `warning`
  or `note` — **a format-conformance vocabulary with no "critical" in it**,
  because whether a mismatch is worth stopping a payment run for is the
  consuming application's judgement (Axiom 9).
  `finio.write_text(reg, doc)` and `finio.write_file(reg, doc, path)` dispatch
  the other way, so a consumer holding a document **never has to name the
  adapter it happens to be holding**. The result carries the `text`, the
  `loss`, and the `byte_fidelity` the adapter declared — travelling *with* the
  output rather than sitting somewhere a caller may not think to look. An
  adapter with no writer is **refused**, because handing back the bytes that
  arrived would be a perfect round trip that serialised nothing; an adapter
  with no validator is refused by `validate` for the same reason, since an
  empty issue list is the strongest conformance claim there is.
  `finio.loss_note(kind, detail)` records Axiom 8's explicit loss, where
  `finio.loss_kinds()` are the three ways a transformation actually loses:
  `uninterpreted` (it did not claim the bytes), `unrepresentable` (it cannot
  hold the value at all) and `narrowed` (it holds less of it than arrived).
  `finio.scan(reg, directory)` is §8's archive report — `{ examined, files,
  counts, unknown, ambiguous, unreadable }`, where **a file nothing claims and
  a file an adapter claimed and could not read are different facts** about an
  archive.
  `finio.registry_states()` and `finio.acquisition_classes()` return §9's five
  states and §10's six classes; `finio.check_registry_entry(entry)` refuses an
  unknown field **by name**, an invented state or class, a state at or beyond
  `spec_obtained` that names no `specification_sources`, and an implemented or
  verified format without `implementation_allowed` (Axiom 12) — finding that a
  format exists is not the same as being allowed to implement it.
  `finio.adapter_rule(rule)` is §12's chain (`behavior`, `rule`, `evidence`,
  `spec_revision`, `authority`, `retrieved`, all required), which answers a
  **different question** from data provenance: not where a value came from but
  why the adapter believes that element has that meaning.
- `finio_nacha` — the ACH file format, and the **first `finio` adapter**
  (§20's proving set). 94-byte fixed-width records: a file header, batches of
  entry details with optional addenda, and control records.
  **A NACHA file checks itself, and that is why it was chosen first.** Every
  batch ends with a control record stating its entry count, a hash of the
  routing numbers it touched and its debit and credit totals, and the file ends
  with one saying the same across batches — numbers computed by whoever
  produced the file, so a reader can be held to arithmetic it did not supply.
  `finio_nacha.adapter()` is the value to register;
  `finio_nacha.registry_entry()` is its §9 entry, which says **`researched`,
  not `verified`, and `DE_FACTO`, not `OPEN`**: the layouts come from the ACH
  origination guides US banks publish to their own originating customers, the
  Nacha Operating Rules themselves are a paid publication that was not held,
  and **this adapter has never met a file produced by a bank** — everything it
  has been run against was written here.
  `finio_nacha.recognise(text)` uses deterministic evidence only: every record
  94 bytes, a valid record-type sequence, and the file header's record size
  `094`, blocking factor `10` and format code `1` — that triple is a
  fingerprint, so its presence is `exact` and its absence is `strong` **by
  name** rather than by a number. It also decides `framing`, because a file
  with no line feed whose length divides by 94 is a blocked file and that is a
  fact about the source rather than a setting.
  `finio_nacha.read_source(src, revision)` preserves and explains (§15): an
  unrecognised record type is kept with its bytes and reported as
  `uninterpreted` loss rather than dropped, and a batch missing its control
  record is kept for `validate_doc` to report. **Padding is not a file control
  record** — both are type 9 and they are told apart by content, and reading 94
  nines as a file control gives a batch count of 999999 and a hash of nines,
  every number ordinary-looking and wrong.
  `finio_nacha.validate_doc(doc)` checks the file against its own arithmetic —
  record lengths, blocking, the record-type **sequence**, the header's own
  declarations, and every batch and file control's count, hash and totals. The
  sequence is checked here *as well as* in `recognise`, and that is not
  duplication: recognition asks whether a file is plausibly ACH and answers
  `possible` for one whose records are the right width and whose types run in
  an impossible order — which is right, since it may well be an ACH file that
  was damaged, and refusing to read it destroys the only thing an operator can
  work from. Validation asks §15's different question, *does this source
  conform*; without it such a file reads, validates clean, and reports nothing
  about the one thing wrong with it. `finio_nacha.batch_totals(records,
  batch)` is that recomputation on its own, which is what a writer needs. **The
  entry hash keeps only the rightmost ten digits** of the sum of the routing
  numbers; keeping the whole sum agrees with the file for every small batch and
  parts company exactly when a file is large.
  `finio_nacha.credit_codes()`, `finio_nacha.debit_codes()` and
  `finio_nacha.direction_of(code)` are **an explicit table, not a rule about
  the second digit**. The digit rule — 2, 3 and 4 credit, 7, 8 and 9 debit —
  holds for checking, savings and general ledger and **breaks for loans, where
  55 is a debit**; a reader that derived the direction would put loan debits on
  the credit side and the file's own control totals would then disagree with it
  for a reason nothing names. A code the table does not hold has direction
  `unknown`, is left out of both totals, and is **reported** (Axiom 7), never
  defaulted to a side.
  `finio_nacha.write_doc(doc)` is **re-emission, not origination** — the
  registry entry's `write_status` says so, because "implemented" would read as
  *you can originate ACH files with this*, and building one from nothing means
  deciding a company identification, an ODFI, an effective entry date and a
  service class, which are the consumer's judgements rather than the format's
  (Axiom 9). It re-emits each record's retained bytes joined by
  the separator that record actually carried, so **an unchanged document
  reproduces its source exactly** — including CRLF where the source had CRLF
  and no separator where the source was blocked. It is reconstructed rather
  than returned, which is what makes the guarantee checkable.
  `finio_nacha.set_field(doc, record, concept, text)` rewrites one field in
  place and **refuses a value of the wrong width rather than truncating**,
  because a truncated account number or amount is a perfectly well-formed
  record that means something else.
  `finio_nacha.layout_for(kind)` gives the layout for a
  `finio_nacha.record_kinds()` member, and the six are also reachable directly
  as `finio_nacha.layout_file_header()`, `layout_batch_header()`,
  `layout_entry_detail()`, `layout_addenda()`, `layout_batch_control()` and
  `layout_file_control()`; `finio_nacha.record_length()` is 94. Each covers all
  94 bytes with no gap and no overlap, which `finio.coverage` asserts rather
  than the addition being trusted.
  An addenda is read by its type code (*since 0.11.0*):
  `finio_nacha.addenda_layout_for(raw)` answers `layout_addenda()` for 05,
  `layout_addenda_return()` for 99 (return reason, original trace, date of
  death, original receiving DFI, information, trace) and `layout_addenda_noc()`
  for 98 (change code, original trace, original receiving DFI, corrected data,
  trace), and `unknown` for any other, which is then a loss note rather than a
  record read with another type's layout. `finio_nacha.return_codes()` lists
  the return and notification-of-change transaction codes (21, 26, 31, 36, 41,
  46, 51, 56), which are in `credit_codes()` or `debit_codes()` by direction.
  **One revision, named `unresolved`, and that is the honest answer.** The
  Operating Rules are revised every year; the record layout has outlived twenty
  of those revisions and a file does not say which produced it, so naming a
  year would put it in every document the adapter ever wrote on no evidence at
  all.
- `finio_watch` — the **monitoring mechanism** (`docs/financial_adapters_design.md`
  §13 *"The monitoring mechanism"*, and §9's ObservationLog). §13 said what to
  **record** and what to do **once a revision is discovered**, and never said
  what a watch source *is*, what checking one *does*, what **triggers** a check
  or what it **emits** — without which "continually maintained" is an intention.
  Measured before this shipped: **all five of §13's own maintenance fields were
  refused by name** by `finio.check_registry_entry`, exactly as §9's five had
  been one section along. They are accepted now, and
  `finio.maintenance_priorities()` gives §13's three cadences — `active`,
  `periodic`, `dormant`.
  **It detects; it never updates.** That is the load-bearing decision and it
  follows from Axiom 4 and §13's step 7: a process that modified an adapter
  would silently change how a file written in 2019 is read, which is the one
  thing this framework exists to prevent. A check produces **work** — an entry
  in a review queue naming what moved and what says so — never a patch, and
  there is deliberately no function here that returns a modified adapter. A
  source tripwire asserts it, because a function returning a patched adapter
  would look exactly like one returning a finding until the day it was used.
  **And it performs no I/O.** `finio_watch.check(source, observed, today)` is a
  pure function of what the **caller** fetched — the shape `agent.apply` and
  `nlq`'s three steps already use — which is what lets a maintenance process be
  tested with no network. A gate that needs the internet goes red when somebody
  else's site is down, and then gets turned off.
  `finio_watch.source(kind, { reference, watching })` declares one, over
  `finio_watch.source_kinds()` — `version_catalogue`, `document`, `changelog`,
  `registry_page`. **`watching` is required and is not decoration**: a
  catalogue page changes when its footer year changes, and recording what a
  change would *mean* is what separates a signal from a diff. A finding is one
  of `finio_watch.finding_kinds()` — `first_sight`, `unchanged`, `changed`,
  `unreachable` — and **`unreachable` is its own answer, not a quiet
  `unchanged`**: a source that has 403'd for six months is not a stable format,
  it is a watch that stopped working, and reporting it as "no change" is how a
  monitoring process comes to assert the world is still by observing nothing.
  That is not hypothetical — Nacha's own developer guide returned 403 to an
  automated fetch during the first survey. What is retained between checks is a
  **fingerprint, not the content**: keeping the page would make the registry a
  cache of other people's documents, which is a redistribution question as well
  as a storage one.
  `finio_watch.observe(log, observation)` is §9's **ObservationLog**, the
  bottom-up half: top-down watching finds a revision when a standards body
  publishes it, observation finds one when files start arriving the adapter
  cannot fully explain — usually sooner, and always more specific. The adapters
  already emit `loss_note("uninterpreted", …)` on every read; **nothing counted
  them**, and §9 is explicit that *"preserving an unknown value is only half the
  benefit; counting it is what turns it into work"*. Three hundred sightings of
  one token are **one entry with a count** and a **bounded sample** of where,
  not three hundred paths. `finio_watch.observations_for(log, adapter)` reads back
  what one adapter has been surprised by; `finio_watch.observation_kinds()`
  names the five kinds, and **privacy is enforced rather than asked for**: an observation
  records a token and a location, the permitted fields carry no amount, name or
  account, and a `detail` longer than `finio_watch.max_detail_bytes()` is
  refused — because a whole record passed as a "token" is a customer record in
  a log.
  `finio_watch.due(entries, today)` makes staleness **derivable, not
  remembered**, and a `dormant` format is never due however old its date —
  §13's *"a historical format whose final revision is decades old may require
  little or no routine monitoring"*. Without that the queue fills with formats
  nobody expects to move and the ones that do are buried.
  `finio_watch.review_queue(entries, log, today)` is the two halves together,
  which is §13's own claim and what nothing implemented: *"an adapter with old
  evidence and no observations may be perfectly healthy… an adapter with recent
  evidence and a rising observation count is the interesting case, and only the
  two together say so."* A not-due adapter that production keeps surprising is
  in the queue; a dormant one nothing has surprised is not.
- `finio_bai2` — **BAI2** cash management balance reporting, the **third**
  `finio` adapter and the third structural shape. NACHA is fixed-width, camt is
  hierarchical XML, and this is **delimited and variable-length with a logical
  record that can span several physical ones** — the first caller of §4's
  `delimited` location kind, which had sat in the enum with nothing using it.
  **The foreign corpus was read before a line of the adapter was written**,
  which is the correction from the NACHA work where real files arrived last and
  found three defects that had already shipped. Four facts came out of that
  reading, none of which a fixture written here would have contained: **the
  record separator is a slash, not a newline** (one real file packs two whole
  records onto a line); a record whose last field is free text often has **no
  terminator at all**, running to end of line (102 of 116 records in one
  sample); **that text contains slashes**, which shatters a reader splitting on
  each one; and **an `88` continues the previous record's *field list*, not its
  text** — the defining feature of the format, and the thing this adapter got
  wrong first. `finio.open_text` gained a `"terminated"` framing with a
  `record_terminator` for the first; the fold that reassembles text containing
  the terminator is format knowledge and lives in the adapter, which restores
  the slash to any fragment that does not begin with a record code it knows.
  Only a fragment that came from a slash split can be folded — one that begins
  a **line** is a record however odd its code, since one real sample carries a
  line of fifteen 1s as block padding.
  `finio_bai2.record_codes()` and `finio_bai2.record_kind(code)` map the eight
  codes; `finio_bai2.read_source` merges each record's continuations into one
  field list before reading it, and `finio_bai2.account_total(records, account)`
  is the arithmetic on its own — **the summary groups plus the transactions**,
  which is the one sum a reader can get wrong in a way that still looks like a
  number.
  **A summary group is not four fields**, and neither is the tail of a
  transaction: the funds type carries its own fields after it — `S` three
  availability amounts, `V` a value date and time, `D` a count and that many
  pairs — so stepping a fixed four lands the next group on a value date, reads
  it as a type code, and displaces everything after it. One rule, written once,
  used by both.
  `finio_bai2.validate_doc` holds the file to **a three-level control total**,
  the strongest self-checking property of the three formats here: each account
  trailer sums its own account, each group trailer its accounts, the file
  trailer its groups, and every level also states a record count. A record whose
  code the format does not define is **reported once and left out of those
  counts** — counting it turns one cause into four findings, three of them
  pointing away from it.
  `finio_bai2.write_doc` re-emits each record with the terminator it actually
  carried — a slash, a newline, or **nothing at all** where a text field ran to
  the end of the file — so `byte_fidelity` is `true` here where camt's is
  `false`. `finio_bai2.registry_entry()` records the format as **OPEN**: the
  BAI specifications are no longer charged for and several banks publish
  field-level guides.
  Verified against **the specification's own worked example**, which
  `moov-io/bai2` ships and whose arithmetic can be checked by hand —
  500000 + 70000000 + 1500000 = 72000000, exactly what its account trailer
  declares. Three of the six foreign files validate completely clean; of the
  rest, one carries block padding and two are **internally inconsistent**,
  which an independent Python reading confirms — it reaches the same totals this
  adapter does, where both disagree with the file.
- `finio_ofx` — **OFX (Open Financial Exchange)**, the fourth `finio` adapter
  and **§20's schema-drift case occurring inside a single format**. OFX 1.x is
  SGML-like and *not well-formed XML* — a leaf is `<CODE>0` with no closing tag
  — while 2.x is proper XML. Same element tree, two serializations, and a file
  of each kind is ordinary; until this, schema drift had only arrived *between*
  formats.
  **One reader serves both, and conversion was rejected.** Inserting the missing
  closing tags and handing the result to `xml.parse` is what more than one
  public tool does, and it would put every **location** into text the bank never
  sent — Axiom 2 says a value is traceable to its source, and a byte offset into
  a document this library invented is not provenance. `finio_ofx.scan(text)`
  walks the tag stream of the original bytes; the single rule that makes one
  reader possible is that **a closing tag whose name does not match the
  innermost open aggregate is a leaf's**, which 2.x writes and 1.x does not.
  Both dialects close *aggregates*, so the difference is about leaves alone.
  `finio_ofx.recognise` reports the `dialect` (`sgml` or `xml`) alongside the
  classification, reading the version from an `OFXHEADER:` block or an
  `<?OFX?>` processing instruction. Nothing depends on the header being first:
  one real fixture opens with **twelve blank lines**.
  `finio_ofx.dialects()` names the two, `finio_ofx.revisions()` the versions
  actually read (102, 200, 203, 211 — a file declaring another is refused by
  name rather than read under the nearest), and `finio_ofx.read_source` builds
  the same document shape the other three adapters produce. Containment is an
  **index range, not close order**: a transaction closes *before* the statement
  holding it, so a single pass attaching each record to "the current statement"
  attaches every transaction to nothing — which the first draft did, reading
  ten real files with **zero transactions and reporting every one of them
  clean**.
  **OFX states no control total**, and that is worth naming because the three
  formats before it all did — NACHA states hashes and totals, camt an opening
  and closing balance, BAI2 a three-level checksum. An OFX ledger balance is a
  *balance*, not a sum of anything in the file. So `finio_ofx.validate_doc`
  cannot reconcile arithmetic and checks what consumers actually depend on
  instead: that the response is **not an error**, and that **FITIDs are
  unique**. The FITID is the whole of OFX deduplication — every piece of
  accounting software that imports OFX keys on it, so a duplicate silently
  drops or doubles a transaction. The status check cannot stop at the first
  status either: one corpus file carries a zero at signon and a `2000` inside.
  **A transaction belonging to no statement is reported**, which catches a
  structure the adapter does not model rather than a file that is wrong: one
  fixture carries four transactions inside an `INVSTMTRS` investment statement,
  and without the check it reads as a clean bank statement with no activity —
  indistinguishable from an account that had none.
  `finio_ofx.write_doc` re-emits the **retained source**, so a 1.x document
  stays 1.x; re-serializing a parsed one would produce 2.x and hand a
  downstream reader something the bank did not send.
  `finio_ofx.registry_entry()` is the only entry in this registry whose
  specification grants an **explicit royalty-free, worldwide, perpetual
  implementation licence** — Axiom 12 answered in writing rather than inferred.
- `finio_all` — **every `finio` adapter this build carries, in one list**.
  `finio_all.adapters()`, `finio_all.registry()` and `finio_all.ids()`. It
  exists for a measured reason rather than convenience: the adapter list was
  built by hand at every call site and **was missed three times in one day**
  when a new adapter arrived — the registry fixture, the registry runner's two
  embedded programs, and the watch runner's — each failing later and separately
  and each reporting a count rather than a cause. A tripwire counts what it
  lists against the adapter libraries on disk, so a fifth adapter that is not
  wired in goes red at once.
  **It is also what an application wants**: §8's archive sweep asks which of a
  directory's files are recognisable, and that needs *every* adapter — naming
  them by hand is friction at exactly the call site where the answer depends on
  forgetting none. **The coupling is real and is why there is a choice**:
  loading this loads every adapter, so a program that reads only ACH files
  should `load finio_nacha` and build its own one-element registry, which is
  the same argument `tools` made for refusing `via: {mcp:}`.
- `finio_pain001` — ISO 20022 **pain.001** customer credit transfer initiation:
  the fifth adapter, and **the first for a file you send**. Every adapter before
  it reads a report and says some version of *"re-emission only"* for
  `write_status`. A pain.001 is an **instruction** — the message a business
  sends its bank to say *make these payments* — and that inverts where the risk
  lives: reading a statement wrongly gives a wrong number on a screen; writing
  a pain.001 wrongly gives a payment run the bank rejects, or executes.
  **Which is why §16 had never been implemented.** Four read-only adapters put
  no weight on *"writing requires stronger guarantees than reading… classify
  the requested conversion as representable, lossy or impossible… the default
  should favor refusal when semantic information would be silently lost"*.
  `finio.classify_write(reg, doc)` now asks that question **before** anything
  is serialized, over `finio.write_classifications()`; an adapter that can
  answer declares `classify_write`, and one that cannot is `representable`,
  which is the honest reading of *this adapter knows no reason the target
  cannot hold it*. `finio.write_text(reg, doc, allow_lossy = false)` **refuses
  a lossy write by default** and refuses an impossible one **with no
  override** — the difference being that one is a cost a caller may accept
  having been told, and the other is a fact about the target format.
  **The limits `finio_pain001.classify_write` checks are the scheme's, not the
  schema's**, and that distinction is the whole point: an XSD accepts a
  200-character creditor name, the SEPA credit transfer scheme carries 70
  (`finio_pain001.scheme_limits()`), and a file that *validates* is exactly the
  one that gets sent and comes back refused. A non-EUR amount is `impossible`
  rather than lossy, because no truncation makes it representable.
  `finio_pain001.validate_doc` holds the message to **two levels of control
  total** — `NbOfTxs` and `CtrlSum` per payment block and again for the
  message. For a file you receive those are somebody else's arithmetic to
  check; for one you send they are arithmetic **you must get right**, and a
  bank that finds them disagreeing rejects the file.
  `finio_pain001.block_total(records, block)` is that sum on its own. **A
  control sum carries no currency of its own**, so it is meaningful only if
  every amount it totals is in one — a block mixing EUR and USD is **reported**
  and excluded rather than allowed to raise, since `money` refuses the addition
  and §15 says a malformed source is described rather than died on.
  `finio_pain001.revisions()` names the one version implemented
  (`pain.001.001.03`, the one the corpus carries) and a document declaring
  another is refused by name; `finio_pain001.read_source` builds the same
  document shape the other four adapters produce; `finio_pain001.write_doc`
  re-emits the retained source after classifying, and refuses an impossible
  document **again** at the adapter — a second guard so a caller reaching the
  writer directly cannot get past §16, the same treatment `reasoning`'s R9
  gets. `finio_pain001.registry_entry()` records the format as **OPEN** and the
  adapter as `researched`: no schema was downloaded and no bank has ever
  accepted a file it wrote, which is what `verified` would mean.
  A **pain.008 direct debit** is a sibling in the same family and the same
  namespace scheme, so *is this pain.001* cannot be answered by finding an ISO
  20022 namespace; it is refused by name rather than read as a credit transfer
  with no transactions.
- `finio_iso20022` — the mechanics every ISO 20022 adapter needs, **extracted
  at the second caller and not the first**: `declared_message(text)` reads the
  version out of the namespace without parsing, `is_family(message, family)`
  tells pain.001 from pain.008, and `text_field` / `number_field` /
  `amount_field` / `decimal_field` carry Axiom 7's absent-versus-invalid
  distinction and the `Ccy`-attribute rule. `finio_camt` held all of it
  privately while it was alone, which was right; the same argument says **not**
  to extract `finio_ofx`'s tag scanner, which still has one caller.
  `finio_iso20022.namespace_prefix()` is the URN every ISO 20022 message shares,
  `is_decimal(s)` and `all_digits(s)` are the two shape tests those fields need,
  and `money_of(ccy, text)` builds money from a currency held in a variable
  without raising on one this build does not know — answering `unknown` instead,
  so a reader can report an unrecognised currency rather than dying on it.
- `finio_registry` — the **format registry** as §9 describes it: not a list of
  what has been built but a record of what **exists**, how its specification can
  lawfully be obtained, and therefore what could be built next. §9's own words:
  *"The registry should include formats even when implementation is presently
  impossible. Such entries become a research and acquisition queue."*
  Until it was written the registry held **two** entries and both lived inside
  the adapter that implemented them — so it recorded only what had already been
  done, which is the one thing a registry is not for. Worse, and measured: five
  of the fields §9 itself specifies were **refused by name** by
  `finio.check_registry_entry`, including `source_url_or_reference` and
  `date_retrieved`, so a discovery pass could not have recorded what it found.
  **A test vector carries its rights, and a prohibited one is refused.**
  `test_vectors[]` was an array of bare strings: it recorded that a file was
  used and nothing about where it came from or whether it may be — and a vector
  is the thing most likely to be somebody else's property. `finio.check_test_vector`
  requires a source, a retrieval date, and a `usage` and `redistribution` drawn
  from `finio.usage_permissions()`. **The rule has three arms, not two**: an
  explicit prohibition on use is **refused by name** (removed, never recorded
  with a flag a caller can forget to read); an explicit permission is used, and
  redistributed only if it says so; and **silence** — the common case, a bank
  publishing a sample in its developer documentation having granted nothing in
  writing — means the file may be read and **must not be committed**.
  Redistribution may not exceed usage.
  A specification source is now a **record**, not a sentence —
  `finio.source_types()` names the seven kinds a survey can honestly cite, and
  `finio.check_specification_source` requires a reference **and a retrieval
  date**, since a URL with no date is a claim about a page as it is today and
  §13's whole maintenance story is that specifications move.
  `finio_registry.queue()` is the formats **nobody has built**, each classified
  and evidenced; `finio_registry.all(adapters)` merges it with each adapter's
  own entry and **refuses an id that appears in both**, because two records of
  one format is where a registry starts lying and it is cheaper to make that
  unrepresentable than to test for it. `finio_registry.implementable(entries)`
  answers the question the registry exists for — what could be built without
  asking anyone for anything — and is deliberately **not** the same as "what is
  public": a format can be publicly documented and still be one nobody may
  implement (Axiom 12), and one whose specification costs money may be
  perfectly lawful to implement from de-facto evidence.
  `finio_registry.blocked(entries)` is §10's stopping point *with content* —
  an `INSUFFICIENT` or `HUMAN_REQUIRED` entry must name what is missing, which
  the validator enforces, because *"Format discovered. Implementation blocked.
  Human acquisition required."* is a shrug otherwise.
  `finio_registry.by_acquisition_class(entries)` groups them, and
  `finio_registry.coverage(entries)` **reports the gap as a value**:
  `finio_registry.design_domains()` is §14's list of twenty-two candidate
  domains, and the note says outright that formats absent from the registry are
  absent *because nobody has looked*, not because they were ruled out.
  **What the first tranche found**, and it is the shape of the answer rather
  than its length: the formats a small business most needs are **open** — BAI2
  is no longer charged for and several banks publish field-level guides, OFX
  carries an explicit royalty-free implementation licence, FIX is free from its
  own standards body, and the whole ISO 20022 family downloads without
  registration — while the ones behind a licence are the **interchange**
  standards: the X12 transaction sets are licensed per tier, ISO 8583 must be
  purchased and its useful content is per-scheme anyway, and Swift's MT
  reference guide is not freely published (though public implementation
  documentation for MT940 is abundant enough to be `DE_FACTO`). A suite asserts
  that split, so a later tranche that reverses it has to say so.
  **Verified against files this project did not write.** Ten ACH files from
  `moov-io/ach` (Apache-2.0, redistributed with its licence and a provenance
  manifest) found three things no fixture written here could, because our
  generator shares our misunderstandings: **four of the ten were refused
  outright** by a strict 94-byte rule, their producer having stripped trailing
  blanks so a file header arrives at 75 bytes and a file control at exactly 55
  — nothing lost, the missing tail being the blank part, and a field past the
  end now reads `unknown` under Axiom 7 rather than blank; **one carries
  non-ASCII**, 94 codepoints in 95 bytes, which the specification does not
  permit and which real producers emit anyway; and **`block_count` was computed
  as a floor** where the field counts the blocks the file occupies, which is a
  ceiling — the two agree on every conforming file, and every fixture here is
  conforming. Two of the ten now validate completely clean, which is what moved
  `recognition_status` and `read_status` to `verified` while `state` stays
  `researched`: those are different axes, and no specification was ever
  obtained. `validation_status` stays `implemented`, because ADV batches
  (service class 280) use an entry layout this adapter does not implement and
  several real transaction codes are not in its direction table — both
  **reported rather than guessed**, which is correct and is not the same as
  being able to validate those files.
- `finio_camt` — ISO 20022 **camt.053** bank statements, and the **second**
  `finio` adapter (§20's proving set). The first of a different
  *representation*, which is the whole reason it was built second: §20 argues
  that an abstraction surviving only one representation is a generalized parser
  rather than a description of the problem, and **BAI2 would not have asked the
  question** — it is record-oriented and legacy-heavy, the same family as
  NACHA, and would have exercised the identical layout-and-record path.
  **It pushed back immediately, in the place that mattered.** A hierarchical
  source has no byte range: `xml.parse` builds nodes carrying a name, a
  namespace, attributes and children and **no position**, and the streaming
  reader carries a *line*, not an offset. So `finio.source_value`'s
  `{ record, byte_offset, byte_length }` could not serve it, and §4's "a
  universal byte-offset model is too representation-specific" stopped being a
  sentence and became `finio.location(kind, detail)`. What this adapter supplies
  is an **xml** location — a `path` and **which `occurrence`** of it, because a
  statement with four hundred entries has four hundred elements at the same
  path and *"it came from Ntry/Amt"* identifies none of them.
  **The revision is the opposite answer from NACHA's**, and that is why a
  second adapter was needed to test §7's resolution at all.
  `finio_camt.recognise(text)` reads the version out of the document namespace
  (`finio_camt.namespace_prefix()` plus digits), so the revision is *determined
  from the source*; `finio_camt.revisions()` names the one implemented, and a
  document declaring another is **refused by name** rather than read under the
  nearest. Recognition performs **no parse** — an archive sweep would otherwise
  pay a full parse per file to discover it is a purchase order — and the root
  element is the second half of the fingerprint, so a document merely
  *mentioning* the namespace is `possible`, never `exact`.
  `finio_camt.read_source(src, revision)` produces the **same document shape**
  `finio_nacha` does — a flat `records` list of `{ kind, fields }` and an
  `entities` hierarchy of indices — which is §20's actual question answered.
  What does *not* survive is the record: a fixed-width record carries `raw` and
  a byte range because it **has** them, and a re-serialization put there under
  that name would be a lie about where those bytes came from, so a camt record
  carries its `path` and `occurrence` instead. **The uniform part is the field
  and its location; the record is representation-shaped.**
  `finio_camt.validate_doc(doc)` holds the statement to **its own arithmetic**:
  the opening balance plus what was credited less what was debited is the
  closing balance, per statement and **never across** — the fixture carries a
  second statement in another currency precisely because a reader totalling
  across them produces the sum of dollars and euros, which `money` refuses to
  compute. The optional `<TxsSummry>` is a second statement of the same facts
  and is checked too, so a reader that summed the entries wrongly and a file
  whose summary is wrong are told apart. `finio_camt.entry_totals(records,
  statement)` and `finio_camt.balance_of(records, statement, code)` are those
  two readings on their own.
  **Two amount-shaped things, denominated differently**, which a fixed-width
  format cannot express: an `<Amt>` carries its currency in a `Ccy` **attribute**
  and one without is a number in no currency, which is a defect; a summary
  `<Sum>` carries no attribute and is not meant to, its currency being the
  account's by definition of the element. Reading the second with the first's
  rule reports every well-formed summary as unreadable. An entry in a currency
  the account is not held in is **reported and excluded from the totals, never
  added** — `money` would refuse the addition, and a validator that let that
  refusal propagate would die on the malformed statement instead of describing
  it (§15).
  `finio_camt.write_doc(doc)` declares **`byte_fidelity: false`**, the first
  honest `false` in this tree and what makes that field a distinction rather
  than a constant: re-serializing a parsed XML document cannot reproduce its
  bytes, so §17's weaker guarantee is the one available and the adapter has to
  earn it instead — reparsing the output must yield equivalent financial
  meaning, and the loss is reported as `narrowed`. It re-parses from the
  **retained source** rather than from a tree the document carried, because the
  framework keeps only `records`, `entities` and `loss` and is right to: a live
  parse tree is a handle, not a value, and **a document that cannot be stored
  is not a document** — the rule `agent` reached from the other direction.
  `finio_camt.registry_entry()` says **`OPEN`** and `spec_obtained` where NACHA
  had to say `DE_FACTO` and `researched`, because ISO 20022 publishes its
  message definitions without charge, so there is a specification to name. It
  does **not** say `verified`: this adapter has never met a statement produced
  by a bank.
- `accounting` — double-entry bookkeeping, pure gBASIC over exact `money`
  (`docs/accounting_design.md`). `accounting.chart(accounts)` validates a chart
  of accounts and fixes each one's normal balance side from its `kind`
  (`asset`, `liability`, `equity`, `revenue`, `expense`);
  `accounting.normal_side(kind)` is that rule on its own — `"debit"` for assets
  and expenses, `"credit"` for the rest — written once and read everywhere,
  since it decides both statement placement and the sign a reader expects.
  `accounting.entry(chart, date, memo, lines)` builds **and validates** one
  entry — a line carries `debit` **or** `credit`, never a signed amount,
  because "is a negative debit a credit" is a question that produces statements
  which balance and are backwards. **Every entry must balance, in every
  currency**, and an unbalanced one is refused at that point rather than
  discovered later by a report that cannot say which entry did it; so is a line
  naming an account absent from the chart. `accounting.ledger()` makes an empty one and
  `accounting.post(ledger, entry)`
  **returns** the new ledger (inside a function `append` mutates a local copy,
  so a mutate-in-place API would silently do nothing).
  `accounting.balances`, `.trial_balance`, `.balance_sheet` and
  `.income_statement` report; `.close(chart, ledger, through, equity)` moves
  revenue and expenses into equity and seals the period — **refused** on a
  period already closed, because running a close twice doubles equity and
  leaves the ledger balanced while doing it.
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
  parents and is **idempotent**. Since 0.2.3 that is one call to
  `make_dir(path, { parents: true })` rather than a hand-rolled segment walk;
  bare `make_dir` still raises on a directory that already exists, deliberately.
- `intake` — what arrived, what was taken, what to keep: the half of an ETL
  cycle that needs **state** (`docs/intake_design.md`). `intake.open(file,
  options)` yields a ledger, `intake.arrived(ledger, dir, pattern)` lists every
  file matching the glob that this ledger has not recorded *and* that is
  finished arriving, and `intake.taken(ledger, entry, meta)` records one —
  **returning the new ledger**, like `accounting.post`, because a record is a
  value and a mutate-in-place API here would silently import everything twice.
  `intake.close(ledger)` folds and unlocks.

  Two options are **required with no default**, because each is correct for one
  kind of source and wrong for the others, and being wrong looks exactly like
  success. `identity:` says what makes two files the same file — `"path"`,
  `"stamp"` (path, size and modified time, which the listing already carries, so
  it costs nothing) or `"content"` (sha256, which **loads the whole file**:
  measured at just over twice the file in resident memory, refused above
  `max_hash:` and refused outright on a build without libcrypto), or
  `"path+content"` (*since 0.11.0*): sameness by path, with the file's sha256
  recorded when it is taken, so `repeats` gives each repeat `same_content`,
  `true` for the same bytes sent again and `false` for the name re-sent with
  different bytes. That is the rule for extracts named by their period, where
  `"content"` makes two empty days one file and `"path"` cannot tell a resend
  from a re-extract. `ready:` says
  when a file has finished arriving — `"renamed"`, `"sentinel"` (take `x` once
  `x.done` appears) or `{ stable_for: 30 seconds }`, which is **a probability
  and not a guarantee**: measured, a file's size held still across consecutive
  samples three times while it was still being written.

  `intake.repeats(ledger, dir, pattern)` *(after 0.10.0)* lists what `arrived`
  leaves out: files finished arriving whose identity the ledger has **already
  taken**, such as a supplier sending the same catalog twice. Each is an
  `arrived` entry plus `repeats`, the ledger's record of the file it repeats
  (`entry_for`'s shape). The file the ledger took, still at the path it was taken
  from and not yet archived, is the original, so it is never listed.
  `intake.set_aside(entry, { to: dir })` moves a repeat out of the drop folder
  with the same checked move `archive` uses (`on_collision:` too), answering where
  it went, and **does not change the ledger**: a repeat was never taken.

  `intake.archive(ledger, entry, { to: dir, keep: 180 days })` moves a taken
  file into an archive, **mirroring the tree it came from** — `2026/Q1/vendor.xlsx`
  lands under `2026/Q1`, which is what `relative` is for, since two files in
  different quarters have the same `name`. On one filesystem it is a single
  rename (asserted by inode, so the optimisation cannot quietly stop); across
  filesystems it copies, **checks the byte count**, and only then deletes, so a
  copy that reported success and wrote short leaves the original where it was.
  `move` is deliberately not used: its cross-device fallback is copy-then-unlink
  with no check between. Nothing here `fsync`s, so this is not a durability
  claim — the same caveat `atomic_replace` carries. An **existing target is
  refused**, naming both paths, because `copy` overwrites in silence and a
  corrected re-drop has the same relative path as the file it corrects; pass
  `{ on_collision: "replace" }` to say that is what you want. `keep:` must be a
  duration with a **fixed length**, so `6 months` is refused and `180 days` is
  not — months and years vary, and a retention stored as seconds cannot carry
  one.

  `intake.purge(ledger)` **reports and deletes nothing**: deleting data is the
  one thing here that cannot be undone, so `dry_run` defaults to true and the
  answer is `{ dry_run, considered, due, purged, bytes, items, ledger }`.
  `{ dry_run: false }` does it, and the report's **`ledger` field is the new
  ledger** — rebind it, as with `taken` and `archive`. Two rules it will not
  break: nothing is deleted that the ledger does not record as **archived**,
  however old it is; and the **entry outlives the file**, marked purged with its
  `meta` dropped, because an entry removed with its file makes that file newly
  arrived the next time it is re-dropped. `{ as_of: datetime }` asks what a
  purge would do at another moment. Empty directories are left behind: purge
  removes files, not the folders that held them.

  Also `intake.seen(ledger, entry)` for the same question about one file,
  `intake.count_taken(ledger)`, and `intake.entry_for(ledger, identity)`, which
  returns the recorded entry **with its times as datetimes** — the ledger file
  has to store epochs, because strict JSON refuses a `datetime`, and the library
  converts at its edge so a caller never sees the number.

  Durability is per file, not per run: `taken` appends one line to a journal
  beside the snapshot and `open` replays it, because rewriting the snapshot each
  time is quadratic in the ledger. Measured through the library at a
  20,000-entry ledger, taking 120 files costs 322 ms by journal against 2041 ms
  by rewrite — 6.3×, and 13 KB written against 266 MB; `tests/run_intake.sh`
  asserts that difference, implementing the rewrite in the tier as a second
  implementation. The per-file cost is still **linear in the ledger** whichever
  way it is written, because a ledger is a value and `taken` writes to a copy —
  which is what `purge` bounds. One run at a time: `open` takes a lock that
  waits, so a second run **waits** for the first (`lock(f, { wait: false })` is
  there for a caller that would rather step around one).
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
- `grid` — a messy worksheet turned into clean frames. The contract is
  *automatic when safe, spec when not, and it tells you which*:
  `grid.tables(g)` guesses and returns each candidate with a **confidence** and
  the reasons behind it, so a messy sheet reports `low` rather than handing back
  a plausible wrong frame; `grid.extract(g, spec)` then does exactly what it is
  told. A spec that matches nothing returns `ok: false` rather than an empty
  frame, because an empty frame reads as *no data*.

  Every spec field is optional. Anchors match by **content**, not row number, so
  inserting rows above a table cannot break a spec: `starts` (text or regex the
  label column must match to begin), `ends` (exclusive), `header_row`,
  `header_rows` (how many rows the header spans — a parent heading is joined to
  its children with a space, and spans exactly what its **merge** spans, read
  from the sheet's `<mergeCells>`; a sheet that declares no merges carries a
  parent across blank cells to the next heading, unless `carry_parents: false`),
  `first_row` / `last_row` (explicit, overriding detection),
  `break_on_blank` (default true), `drop_totals` (drop rows whose label matches
  `total_pattern()`), `drop_matching` (a regex), `label_col` (default the first;
  the drop checks read it too) and `columns` (explicit names, overriding the
  header). `xlsx.grid(sheet)` answers `merges`, a list of `{first_row,
  first_col, last_row, last_col}`.

  The result says what it did with every row: `rows` (the sheet row behind
  each frame row), `dropped` (each row left out, as `{row, label, why}` with
  `why` one of `blank`, `total` or `drop_matching`), `header` (`{name, col}`
  per frame column) and `notes`. A heading written twice is two columns, the
  second named `"<heading> (2)"`, and a note says so.

  `grid.total_pattern()` returns the regex used to recognise a totals row (a
  label beginning with *total*, *subtotal* or *grand total*, or ending with
  *total* or *subtotal*, so `Garza subtotal` is one) —
  exposed rather than hidden precisely so a caller can see and change what is
  being trusted — and `grid.row_is_blank(g, r)` is the emptiness test the
  detector uses. What remains after reading a sheet is **policy** — which row is
  a header, which is a total — which is why this is pure gBASIC and lives where
  the report's owner can change it. Design: `docs/xlsx_design.md` §5.
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

  `merge(sources, spec)` answers `{ok, frame, accepted, rejected, notes,
  mapping, refused}`. `mapping` names the source column behind each canonical
  one, per source (`{CU_A: {balance: "Bal $"}}`), so a fuzzy match is visible.
  `refused` lists every cell that held something its kind could not read
  (`"TBD"` as money) as `{source, row, column, canon, value, why}`, `row` from
  0 in the source frame: its merged value is `unknown`, as an empty cell's is,
  and this is how the two are told apart. A rule's names are its `from`
  aliases and then the canonical name, compared under `normalize_name`;
  `exact: true` compares them as written and drops the canonical name when
  aliases are declared, so a declared alias is the whole mapping. A source in
  which **two columns match one rule** is rejected, naming both, rather than
  resolved by a pick.
- `dbframe` — a frame becomes a database table (`docs/xlsx_design.md` §7).
  `column_type(values)` decides one column's SQL type from EVERY value, never
  the first, and `safe_name(s)` is the identifier validator — identifiers cannot
  be bound as parameters, so they are **refused rather than escaped**.
  `to_table(df, db, table, options)` creates and loads a SQLite table.
  *After 0.10.0:* `schema(df)` gives each column `{ name, column, type, kind }`,
  where `kind` is the gBASIC kind every value shares, in G-SQL's words (`date`,
  `datetime`, `time`, `duration`, `money(USD)`, `money` for several currencies,
  `boolean`, `number`, `text`; mixed or empty is `text`; `column_kind(values)`
  decides it for one column and `value_kind(v)` for one value, `other` for a
  value no typed column holds), and
  `gsql_create_sql(table, df)` writes a G-SQL `CREATE TABLE` from it, with no
  key, to read and change before running. A whole number is `number`, not
  `integer`, because a G-SQL integer column refuses an inexact value even when
  it is whole. `type`, the SQLite type, is unchanged.
  `to_gsql(df, db, table, options)` loads an **existing** G-SQL table, whose
  declared types and constraints are the contract, and answers `{ok, message,
  table, rows, refused, columns, sql}`: each row the table refuses is listed
  as `{row, code, message, values}` and undone alone, and the rest commit.
  `atomic: true` loads nothing if any row is refused; `columns: {"Frame name":
  "table_column"}` names the columns, and only those are loaded. Every value is
  bound. Inside a caller's transaction the load is a savepoint.
- `ari` — the anchor-relative report parser (`docs/ari_spec_language.md`).
  *After 0.10.0* (gaps G24–G28 from the gBASIC books): `last money` refuses
  to read past an amount printed as asterisks (`overflow-after-money`); a
  `within columns` qualifier keeps the type; a section with `ends` and no
  `starts` ends there; a `wraps` field joins its text across a
  `rows continue(...)` record; and `using century: 20` with
  `using no date: 00/00/00` reads two-digit years and a report's empty date.
  `ari_parse_spec(spec_text)` parses a spec on its own, and
  `clean_grid(report_text, spec_text)` exposes the page-furniture pass
  independently of any spec — useful, and testable, without parsing anything.
  `inspect(report_text, spec_text)` is `parse` plus `findings`: the parse
  diagnostics grouped by reason, each with a field path and a remedial hint,
  and instance indices collapsed so `branches[0].opened` and
  `branches[1].opened` report as one field — an authoring-time summary, so a
  guess never enters the parse path.

  **The recognizer tables are public**, and that is a decision rather than an
  accident of scope: `money_patterns()` and `date_patterns()` are the same
  tables `_money_in` and `_date_in` consume, and `ari_discover` reads them
  rather than keeping its own. Two copies of *what money looks like* drift, and
  the failure would be silent — a profile reporting a `date` in a column the
  engine answers `no-date-found` for. The split is that the **tables** are
  shared and the **functions** are not: `_money_in` and `_date_in` are built
  for conversion and throw their spans away, while discovery needs location and
  no value. `date_patterns()` is ordered most-specific first and **the order is
  semantic**; each entry carries `needs_dialect`, true only for the numeric
  form, which is the residue `using date:` exists for.

  **Dates**: ISO `YYYY-MM-DD`, the numeric `d/m/y` family, and — added
  2026-09-16 — the alphabetic-month forms `DD-MMM-YYYY` and `MMM-DD-YYYY`,
  separated by `-`, `/`, `.` or a space, in any case. **An alphabetic month
  needs no dialect**: `16-OCT-2026` cannot be misread, so it is the one date
  form that can never produce an `ambiguous-date` diagnostic. The gap was found
  by building something else — the ARI Discover corpus emits that format because
  it is the classic mainframe print-image date, and `ari` answered
  `no-date-found` for every one of them.

- `ari_discover` — **Phase 0: profiling** (`docs/ari_discover_design.md`).
  Measures what repeats, varies and collides in a corpus of print-image
  reports, and **proposes nothing**: there is no specification generation here,
  because a layer that also guessed would make the guess impossible to evaluate
  apart from the measurement under it. It depends on `ari`; `ari` never depends
  on it, so a caller with a known specification pays for none of this.

  `default_options()` is the options record as a value —
  `minimum_support`, `minimum_confidence`, `maximum_section_depth`,
  `allow_fixed_columns`, `redact_examples`, `llm` — and `option_names()` lists
  the field names. Every entry point takes `options = nothing` and substitutes
  the defaults, so the record is never a default parameter value (gBASIC
  defaults are literals). An unrecognised field is **refused by name**.

  `profile(report_text [, options])` and
  `profile_source({id:, text:} [, options])` return one source's profile:
  `physical_lines`, `bytes`, `blank_lines`, `content_lines`, `furniture`,
  `families` and the `grid`. `profile_corpus(sources [, options])` adds
  `shared_signatures`, `single_source_signatures` and a `support_warning` —
  because below about ten sources `minimum_support` cannot be calibrated at
  all: with three, `0.80` *means* "3 of 3", since 2/3 is 0.67.
  `explain(profile [, options])` renders a profile as text.

  `grid(source_id, report_text)` is the source grid, one row per physical line,
  carrying **both index spaces**: `byte_start`/`byte_length` into the file for
  provenance, and `cp_length`/`indent` in codepoints. `spans(line_text)`
  returns the typed spans of one line with `cp_start`/`cp_length` **and**
  `byte_start`/`byte_length`, plus `alternatives` and `needs_dialect`.
  `token_kinds()` lists what a span may be.

  **The two index spaces are never mixed.** `ari` locates in codepoints — its
  `columns` rule, `len`, `mid` — so anything that could become part of a
  generated rule is in codepoints, while provenance back to the file is in
  bytes. Every position field says which; there is no bare `offset` or `start`.
  This is the defect `finio` Phase 0 shipped one library over, and an ASCII
  fixture cannot see it, because the two coincide exactly until a multi-byte
  character appears.

  `signature(line_text)` replaces typed spans with placeholders and keeps
  literals; `shape(line_text)` blanks the literals too. `families(rows,
  exclude_lines [, options])` groups lines and decides **from the group** which
  words are literals: a word holding `minimum_support` of its group is an
  anchor, anything else is `<TEXT>`. One line cannot decide that, and taking
  every word as a literal makes each detail row its own family.

  **Phase 1** — `infer(sources [, options])` proposes a specification for the
  dominant repeating family and **has `ari` judge it**: the returned
  `scorecard` comes from running the candidate through `ari.parse`, never from
  the model that produced it. The proposal carries `spec`, `fields` (each with
  its locator and the evidence for it), `questions`, `built_from` (the source
  the column boundaries and heading were read from — without it a positional
  rule cannot be audited), and `family_share`. `validate(sources, spec_text
  [, options])` scores any specification the same way.

  **The dominant family must be dominant, not merely recurring.** A candidate
  has to hold `minimum_family_share` (default 0.15) of its source's content
  lines *and* appear in `minimum_support` of the sources. Measured: the real
  corpus's detail family holds 55%, and requiring recurrence alone made `infer`
  propose a specification for **structureless** text — a one-line
  `<MONEY> <DATE>` family recurring in 11 of 12 null sources by chance.

  `gutters(rows, lines)` finds a fixed-width family's column boundaries from
  whitespace present on **every** row, and a run of one space is not a gutter.
  `infer_fields(rows, family, furniture_lines [, options])` makes one column one
  field, and `spec_text(family, inferred [, options])` renders them as ARI
  source — with the evidence for each rule as a comment beside it, because a
  person has to maintain the result **without** `ari_discover` (design
  principle 8). `anchor_stability(sources, signature [, options])` is the measure
  without which the scorecard lies: a `columns` rule built from one source
  parses every other one, produces the right row count and no unknowns, **and
  reads the wrong columns**. It is computed without an answer key, from whether
  the family's column structure is the same in every source.

- `finio_rates` — **reference and benchmark rates**, from the institutions that
  publish them. `sources()` lists what is known and `source_ids()` just their ids; `fetch(id [, options])` returns
  an observation series; `on_date(series, d)` gives the rate **effective** on a
  date — the latest on or before it, the rule `money.rate_on` follows for FX,
  because a report run for March must see March's rate rather than today's.
  `latest` and `revisions` read a series.

  Five keyless sources: SOFR, EFFR and OBFR from the New York Fed, US
  Treasury average interest rates from FiscalData, and the ECB's euro exchange
  reference rates (below). **Keyless is a promise, not a
  coincidence** — a source needing an account stops working when somebody's key
  expires, in a library whose point is that a number can be reproduced years
  later. FRED's 800,000 series need a key and are deliberately absent.

  **A rate is kept as the publisher's own decimal text**, with the number beside
  it. Measured: Treasury sends `"3.788"` as a *string* and the NY Fed sends
  `3.85` as a *float*, and one recording carries `3.490` — which a double
  renders as `3.49`. A basis point is 0.0001; on a hundred million of notional
  that is ten thousand dollars. Use `value` for arithmetic and `rate` to
  reproduce what was published.

  **`revised` is three-valued.** `true` and `false` where the publisher reports
  revisions, and `unknown` where it does not — because *"this publisher does not
  say"* and *"this value was not revised"* are different claims, and a consumer
  pricing off the second deserves to know which it has.

  **Exchange rates** *(added 2026-11-03)*: the source `ecb_fx` is the European
  Central Bank's euro reference rates, about thirty currencies fixed daily at
  14:15 CET, keyless, with history to 1999. `fetch("ecb_fx", { from: d1, to: d2 })`
  fetches a **date window** (the last `limit` days without one); never the last
  N observations, which the ECB answers with each series' last N *whenever they
  were* (measured: the rouble's March 2022 fix, the drachma's from 2000). The
  four *indicative* series the ECB stopped in 2020 are left out and named in
  `excluded`.

  `fx_on(series, from, to, d [, { max_age: 7 days }])` (the options are
  `default_fx_options()`) gives the rate effective
  on `d`: the latest fix on or before it that carries both currencies, and no
  older than `max_age`, so a series that has stopped (the lev, when Bulgaria
  joined the euro) answers `unknown` within a week rather than for years. The
  answer is `{ from, to, on, date, age, rate, value, derived, how, via }`.

  **Published and derived rates are labelled.** The ECB publishes euro to a
  currency (`EUR` to `USD`, 1.1642), and `derived` is `false`. A currency to the
  euro is the inverse, and between two other currencies it is a cross through
  the euro; both are `derived: true`, with `how` saying which and `via` holding
  the published observations, each located in the response. Deriving is sound
  here because a reference rate is a single middle rate with no buy/sell spread,
  which is also why `money.rate` still refuses to invert a rate a program records
  itself. A derived rate is one division written to ten significant figures; the
  published rates carry four to six.

  `load_fx(series, [{ from: "USD", to: "GBP" }, ...])` records each pair for
  every publication date with `money.rate`, so `money.convert` can use them, and
  answers `{ loaded, pairs, skipped }`. A pair is skipped with its reason, never
  silently, when money does not know a currency (the lev) or the series never
  carries it. **`money.convert` has no age limit**: a window loaded for January
  answers for June with January's rate, so load the window the conversions are
  for, and use `fx_on` where a stale rate must be refused.

  Loading `finio_rates` needs libcurl (its transport, `webclient`), like
  `market`; the tests replay recorded responses and need no network.

  Every observation carries a `finio.location` naming the element it came from.

- `ocr` — **reading text out of an image** (`docs/ocr_design.md`). Pure gBASIC
  over the `tesseract` CLI, so it works anywhere that is installed rather than
  needing a build dependency the lean release tarball would not carry.

  `ocr.available()` and `ocr.languages()` say what this machine can do.
  `ocr.read_page(path [, options])` returns a **page**, not a string: `words`
  (each with `text`, `confidence` and a box), plus `rotation`,
  `rotation_confidence`, `script`, `script_confidence` and `skew`. Options are
  `{ languages:, psm:, min_confidence: }` and an unknown one is refused by name.
  `ocr.grid(page [, { max_skew: }])` lays the page out as text, always with a
  space between words (*since 0.11.0*, so two words the engine split are not
  run back into one); `ocr.grid_lines(page)` gives the same lines as the words
  placed on them, each with its `index` into `page.words` and its `column`, so a
  value can be attributed to the words it is made of. `ocr.skew_of(words)` and
  `ocr.max_skew()` are the measure and the default threshold it refuses past;
  it also refuses a page whose skew, across the words' span, drifts more than
  half its median line pitch, where a row starts to carry the next row's
  figures (measured on real invoices at 2.2 degrees).

  **It never returns a bare string, never silently drops a low-confidence word,
  and corrects nothing.** Each is a refusal with a measurement behind it. Plain
  OCR of a columnar report recognises every word at 90–96% confidence and
  *reorders* them into separate blocks, so an account number and its amount end
  up twelve lines apart — nothing missing, the association destroyed, and the
  text still reading like a document. A dropped word is indistinguishable from
  a word that was not there. And every plausible correction — “0 looks like O in
  a numeric column” — turns a *visible* misread into an *invisible* one.

  **A language that is not installed is refused by name**, because that failure
  says nothing otherwise: reading Korean with English data returns 39–82 words
  at 30–45% confidence rather than an error or an empty page. Measured —
  installing the language took the same pages from 3 to 117 high-confidence
  words. Languages **combine** (`"kor+eng"`); the wrong language alone is worse
  than none.

  **Orientation is settled before anything else**, because it is the dominant
  real failure: of seven hand-held photographs, five were a quarter turn or more
  out, and a 90° error reads as *87° of skew at 30% confidence* rather than as a
  rotation. EXIF is not enough — a page can be sideways inside a correctly
  oriented photograph. How confidently it was settled travels with the page.

  **`grid` refuses past `max_skew()`.** Beyond about 3° the layout does not
  break, it **misattributes** — a well-formed row carrying another row's money,
  at the same confidence as a clean page. `read_page` is unaffected; what may
  not be claimed is that a row belongs together.

  What `grid` produces is a **print-image report**, which is what `ari` parses
  and `ari_discover` infers a specification from — so this is a front door to
  machinery that already exists rather than a new pipeline.

- `pdf` — **reading a PDF** (`docs/pdf_read_design.md`, *since 2026-11-09*).
  Pure gBASIC over poppler's programs (`pdfinfo`, `pdftotext`, `pdftohtml`,
  `pdfimages`, the poppler-utils package), which are run rather than linked:
  poppler is GPL. `pdf.available()` says whether they are installed, and every
  other verb raises naming the package without them.

  `pdf.info(path)` is `{ ok, why, pages, title, producer, creator, encrypted,
  sizes }`, with one `{ width, height, rotation }` per page in points.
  `pdf.read_page(path, n [, options])` reads page `n` (from 1) as a **page**:
  `{ ok, why, n, how, width, height, rotation, words, images, text_layer,
  text_layer_set_aside, skew }`. Words are `ocr.read_page`'s shape (`text`,
  `left`, `top`, `width`, `height`, `block`, `paragraph`, `line`,
  `confidence`), so **`ocr.grid` lays out a PDF page as it lays out a scan**.
  Words and `images` (`left`, `top`, `width`, `height`, `pixels_wide`,
  `pixels_high`) are in points from the top-left corner. `pdf.read(path [,
  options])` is `{ ok, why, pages }`, every page in order.
  `pdf.text(path, n)` is the page's own text laid out as printed.
  `pdf.image(path, n, index, dir)` writes that page's `index`-th picture
  (from 0) into `dir` as it is stored, a JPEG staying a JPEG, and answers its
  path. Its order is the PDF's, which may differ from `images`'.
  `pdf.option_names()` is `["ocr", "languages"]`, and an unknown option is
  refused by name.

  **Every page says how it was read** in `how`:
  - `text`: the PDF's own text. Its `confidence` is `unknown`, since nothing
    measured it.
  - `scan`: one picture the size of the page, read only when asked.
  - `ocr`: a scan read with `{ ocr: true }`, through `ocr`, its words scaled
    to the page's points with the engine's confidence and `skew`.
  - `blank`.

  **OCR is never automatic.** A producer's text and an engine's guess are
  different evidence, and OCR costs seconds a page.

  **The awkward pages:**
  - A **searchable scan** (a page picture with a text layer over it) answers
    the layer's words with `text_layer: true`. Read with `ocr: true`, it has
    `text_layer_set_aside: true`, the layer's origin being unknown.
  - **Small pictures** on a text page (a logo, a stamp) are listed and never
    read; take one out with `pdf.image` and give it to `ocr`.
  - **Two page-sized pictures**, or small pictures with no text, are refused
    as a value (`ok: false`, `why`).
  - **A damaged file** is `ok: false` with poppler's own words, such as
    *"Couldn't find trailer dictionary"*, so a batch meets it as an ordinary
    case. A page number out of range raises.

  Not measured, so not promised: an encrypted file, a page rotated by
  `/Rotate`, and a font with no Unicode map, whose wrong text poppler gives
  and this passes on.

- `ari_advisor` — **Phase 4: the optional LLM advisor** (design §11). A
  *separate* library, because `llm` needs libcurl and a program that discovers a
  specification should not carry an HTTP client it never calls. Its prompt
  names no domain of its own (*since 0.11.0*, `prompt_version: ari-advisor-2`;
  it called every report a banking report), and the option `domain:` is a
  sentence saying what the report is. `adopt` rewrites the field's
  declaration in `proposal.spec` as well as in `fields`.

  `evidence(proposal [, options])` builds the bounded package that leaves the
  machine. It is built from the **proposal**, never the report, so a value the
  engine did not conclude cannot travel; the row family goes as its signature
  and count rather than whole, since the record carries every line number its
  members were found on. `mask(text)` is the redaction — digits to `9`, letters
  to `X`/`x` — and it is **structural rather than removal**, because the shape
  is the evidence a name is inferred from and `[redacted]` would be private and
  useless. `modes()` lists them: `off`, `local`, `redacted` (the default) and `full`.

  `advise_names(proposal, model [, options])` returns the proposal with an
  `advice` record beside any field the advisor spoke about, plus `advisor`
  provenance. **The field's own name is not touched.** `adopt(proposal, name)`
  applies one suggestion, one at a time; there is deliberately no `adopt_all`.

  **This corrects the design's §11**, which says every suggestion is "translated
  into a deterministic candidate, executed by ARI, and scored". That holds for a
  *rule* — coverage and collisions judge it. It does not hold for a *name*:
  `posted` and `posted_date` parse the same corpus to the same rows with the
  same coverage and the same collisions, so nothing can prefer either. A name is
  therefore proposed and never adopted, and a person decides.

  Advice is refused for a proposal the engine **refused** (`ok: false`) — the
  rule `decision.evaluate` follows for a quantity a Finding declined to
  establish. A model asked to name the columns of a rejected specification will
  name them fluently, and those names would be the most convincing part of a
  result already thrown out.

  **Measured over 8 sources carrying 230 planted rows**: an anchor-relative
  field recovers **230/230**; a positional one recovers **147/230** and is
  silent about the rest. That is the design's "relative structure before
  columns" as a number, and it is why `allow_fixed_columns` defaults to false
  and what it cannot locate becomes a question rather than a column.

  **Phase 2** nests it. `sections(rows, families, detail [, options])` finds the
  family that heads each run of detail rows and the one that closes it —
  decided by **position relative to the runs**, not by what the text says, since
  keying on the word "TOTAL" works on one corpus and no report that says
  "SUMMARY". Candidates are grouped by **literal prefix** rather than by family,
  because that is what a `starts(...)` pattern is built from and a two-word
  branch name otherwise forms a different family.
  `furniture_directive(furniture)` turns Phase 0's measurement into the
  `page:` / `break:` / `drop:` block, and without it the page header
  `BRANCH ACTIVITY REGISTER` matches `^BRANCH ` and becomes a section.
  `spec_text_nested(...)` renders the result.

  `region_coverage(sources, spec, signature [, options, profiles])` is the
  measure `source_coverage` cannot substitute for: it compares the sections the
  specification found against the runs Phase 0 measured, needing no answer key.
  Built from one source it read **1.0 against 0.125** — parsing every source
  without error while structurally wrong in seven of eight.

  **Multi-source refinement** closes that, and each half is a measured
  difference. `corpus_furniture(profiles)` picks the pagination directive the
  whole corpus supports: `break: formfeed` describes the source the rules came
  from and **0 of 8 others**, while a pattern on the header line — which every
  source carries, including the ones that also have a form feed — describes
  **8 of 8**. `corpus_labels(profiles, signature [, options])` collects each
  label the corpus uses for one concept and `label_token(literals)` renders them
  as a regex alternation, because `ari` takes a regex in a locator: with one
  source's wording, `right of "TOTAL FOR BRANCH"` recovered **19 of 37** branch
  totals and returned `unknown` for the rest. Both patterns join words with
  `[ ]+`, since a print-image label is column-aligned and a signature's gaps are
  normalised away — joined with single spaces the break matched nothing, and
  silently.

  A **gap of furniture does not break a run**: a section whose rows straddle a
  page boundary is one section, because `page:` strips the header before any
  section is located. Getting that wrong made the measure disagree with the
  engine it measures.

  **Measured after refinement**: over all 24 sources, one generated
  specification recovers **121/121** branch numbers, totals and row counts —
  including the 16 that inference never read. `options.holdout: n` reserves the
  last *n* sources and scores them separately (§8.2); reserved from the end
  rather than at random, because §17 criterion 9 requires the same proposal from
  the same ordered corpus.

  `page_starts(rows [, options])` reports where pages begin —
  stated by form feeds, or inferred from a repeating block that **page one
  begins with**. `furniture(rows [, options])` returns the header lines,
  `pages`, the `evidence`, and `why` when it **refuses**: a single-page report's
  header is indistinguishable from its first heading, so one page is not
  evidence of furniture and the honest answer names what is missing.
- `llm` — a chat-completion client (`docs/llm_design.md`).

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

## Platforms

gBASIC runs on **Linux** and, since 0.7.0, on **Windows**. CI builds on Ubuntu
24.04 and latest, with every optional module and with none; the Windows build is
made and tested by hand rather than in CI, which is a weaker guarantee and is
said plainly for that reason. `tests/run_windows_suite.sh` runs its fixtures on
*every* platform, so a Windows-only assumption fails on Linux too.

Deliberately absent on Windows, each refusing by name when called: POSIX signals
(`process.stop` ends a child with a console control event instead), and the prompt's
line editor. `docs/windows_port_status.md` records what was
measured.

**macOS is not supported, and the honest word is now *measured* rather than
*untested*** — this section said "nobody has built it there" until 2026-10-10,
and by then the on-demand `macos-exploration` job had been run and read.
Everything compiles except the actor transport. Of the three blockers it
exposed, two are fixed:

- `MSG_CMSG_CLOEXEC` does not exist outside Linux. The flag is 0 there and each
  received descriptor is marked immediately afterwards — see
  `include/platform.h` for the residual race that difference leaves.
- `recv(fd, NULL, 0, MSG_PEEK | MSG_TRUNC)`, which sizes the next frame, is a
  **Linux extension**: POSIX defines `MSG_TRUNC` only as an output flag. macOS
  defines the constant, so that call compiles there and **answers 0** — which
  the actor loop reads as "the peer closed", because a real frame always carries
  a 4-byte header. Actors would have delivered nothing at all, silently. It is
  `gb_channel_peek_len` now, and `tests/channel_peek_probe.c` compiles both
  branches and requires them to agree.

What remains is the one this section used to get backwards. `AF_UNIX` has no
`SOCK_SEQPACKET` on macOS, and the text here said `SOCK_DGRAM` "keeps the
message boundaries the code relies on" — it keeps the boundaries and **has no
end-of-file**, which the code relies on just as much. Measured on Linux, where
both types exist:

```text
SOCK_SEQPACKET   peer closes -> recv returns 0
SOCK_DGRAM       peer closes -> recv BLOCKS FOREVER
```

So an actor whose peer exits would hang the reader rather than report a
shutdown. The remedy is the one the Windows port needed: an explicit length
prefix over a stream socket in the channel layer. A rework, not a flag.

The other two Linux-specific mechanisms are unchanged: `/proc/self/exe` for the
re-exec path (`_NSGetExecutablePath` on macOS), and `PR_SET_PDEATHSIG` for
"nothing this interpreter started outlives it", which has no macOS equivalent
and would need `kqueue`'s `NOTE_EXIT`. The event loop already uses `poll()`
rather than `epoll`, so the usual blocker is absent. On Windows, WSL2 runs the
Linux build unchanged if you prefer it to the native one.

## The Prompt

`gbasic` with no arguments (or `gbasic --repl`) opens an interactive prompt.

The commands below — `list`, `run`, `new`, `delete`, `vars`, `cls`, `save`,
`load`, `help`, `quit`, and `?` — are **not gBASIC statements**. They are read by
the prompt before the text reaches the parser; the grammar does not contain them,
and every one of them is a syntax error in a `.bas` file. That is also why a
command name only counts as a command when it stands as a whole word. It
is the same interpreter with its lifetime turned inside out: a file opens an
environment, runs once and releases it, while the prompt opens one environment
and runs a root per line against it. A variable assigned on one line is there on
the next; so is a function, a modifier and a library.

```text
$ gbasic
gBASIC. `help` for prompt commands, `quit` to leave.
> x = 5
> print x
5
```

### A line that is not a statement is a question

`1 + 2` is not a gBASIC statement, and at the prompt it does not have to be.
Anything that is an expression rather than an instruction is evaluated and its
answer shown:

```text
> 1 + 2
3
> len("abc")
3
> x
5
```

A call whose answer is `nothing` shows nothing — it did something rather than
answered something, and printing the word would be noise:

```text
> setup()
>
```

(`write(f, "hello")` was the example here until 2026-09-23 and it was wrong:
`write` answers `true`. It is in the section on the resident program now,
where the point is that it acts as well.)

**A NAME or LITERAL holding `nothing` DOES show it, and that asymmetry is
deliberate.** The rule is about the *call*, not about the value:

```text
> n = nothing
> n
nothing
> nothing
nothing
> gives_nothing()        ' a function whose body is `return nothing`
>
```

`return nothing` is gBASIC's void convention — the unused-result warning exempts
it by value for exactly that reason — so a call answering `nothing` is
overwhelmingly a command, and echoing the word would put a line of noise under
every command you type. Asking a *name* is never a command, so there is nothing
to keep quiet about. `unknown` echoes from both positions, because nothing
returns it as a convention. Stated here 2026-10-02 after the gbasic-books
session measured the gap: the rule above was documented and the contrast was
not, so exploring absence at the prompt — the obvious way to learn this part of
the language — reads as though the call did not run.

`?` asks the same thing explicitly, and it is a **question**, not shorthand for
`print`. The difference shows when a name has been taken by a command: `? list`
reads back a variable called `list`, where a bare `list` shows the program.
Because `?` declares the line a question, a comparison after it is read as one:

```text
> x = 1
> ? x = 5
false
```

Without the `?`, `x = 5` is an ordinary assignment and the prompt performs it.

**A question reports its errors where you typed them.** A question that is not
a statement cannot be run as written, so the prompt wraps it; that wrapping is
invisible in diagnostics, which name the line and column of your own text:

```text
> age > 12
runtime error at <prompt>:1:5: undefined variable: age
```

### Unfinished lines

A block, an open bracket or an open quote asks for the rest of itself. The
decision is the parser's, not a list of keywords, so it is right for every
block form the language has:

```text
> for i = 1 to 3
... print i
... next
1
2
3
```

A string literal may run across several lines in a file, and it may at the
prompt for the same reason:

```text
> rules = "Think of an animal,
... and I will try to guess what it is."
> print rules
Think of an animal,
and I will try to guess what it is.
```

A blank line submits what you have anyway, which is how you get the diagnostic
out of a line that merely *looks* unfinished — a quote you forgot rather than
meant. The cost of having that way out is that a blank line inside a multi-line
string ends the line instead of becoming a paragraph break; write the blank
line as `\n` inside the string, or put the text in a file.

### Editing and history

At a terminal the prompt gives you a line editor. It is written into gBASIC
rather than linked, so it is there on every system with no extra package.

| key | what it does |
| --- | --- |
| ← → | move by one character |
| ↑ ↓ | walk back and forward through history |
| Home / End, Ctrl-A / Ctrl-E | start and end of the line |
| Backspace, Delete | remove a character (one character, not one byte) |
| Ctrl-K / Ctrl-U | delete to the end / to the start |
| Ctrl-W | delete the word before the cursor |
| Ctrl-L | clear the screen, keep the line |
| Tab | two spaces, for indenting a block |
| Ctrl-C | throw this line away |
| Ctrl-D | end the session, when the line is empty |

History is kept between sessions in `~/.gbasic_history` (on Windows,
`%LOCALAPPDATA%\gbasic\history`), readable only by you
because a session can contain a connection string. `GBASIC_HISTORY` names a
different file; setting it to an empty string turns the file off without turning
editing off.

History holds one line at a time, so recalling a multi-line block walks it a
line at a time. To bring a whole function back, `list` it and type it again —
which is the resident program's job, and re-entering a declaration replaces it.

### Nothing you type is lost

What you type is written to a **session cache** as you go, so there is always a
file holding your program whether or not you have saved it. Two things follow.

A session that is **killed**, or **left without saving**, can be brought back:

```text
$ gbasic
gBASIC. `help` for prompt commands, `quit` to leave.
A previous session ended without saving. `recover` brings it back, `discard` forgets it.
> recover
recovered; `list` to see it, `run` to run it
```

`recover` restores the **program** — as the entries it was, so `list` numbers
them and `delete` can name one — and it does not run it. `run` is still what
makes a program live, exactly as for one you typed — a prompt that executed
yesterday's half-finished work on your behalf would be a surprise, and could be
a destructive one.

The cache holds the program, not the transcript — the same rule `list` and
`save` follow, so a question you asked is not in it. The literal transcript is
what history is for, and that persists too.

It is a **safety net, not a substitute for `save`**: it holds one program, `new`
replaces it, and saving clears it. The file lives under `$XDG_STATE_HOME/gbasic`
(or `~/.local/state/gbasic`), readable only by you; `GBASIC_SESSION_DIR` names a
different place. On Windows, where a terminal has no `HOME`, it lives under
`%LOCALAPPDATA%\gbasic` — and when gBASIC was installed as a package (MSIX),
Windows keeps that folder inside the package's own storage, under
`%LOCALAPPDATA%\Packages\<package>\LocalCache\Local\gbasic`.

And because there is always a file, **`spawn` works at the prompt**. A spawned
actor is fork+exec and the child re-parses the source file; the cache is that
file. On a machine where no cache can be written, `spawn` refuses and says why.

### Interrupting

Ctrl-C ends the line that is running and gives the prompt back; it does not end
the session, and the resident program and every variable survive. The
interruption travels the ordinary failure path, so locks are released and open
blocks unwound exactly as a real error unwinds them:

```text
> while true
... end while
^C
runtime error at <prompt>:1:1: interrupted
> print "still here"
still here
```

**Everything the loop had done is still there**, which is the reassuring half:
variables set before it, and the ones it was changing, hold the values they had
when you interrupted — so you can look at where it got to rather than starting
again.

```text
> n = 0
> while true
... n = n + 1
... end while
^C
runtime error at <prompt>:1:1: interrupted
> print n
343027
```

Ctrl-C at an idle prompt does nothing. This is the prompt's behaviour only — for
a program run from a file, Ctrl-C still ends the process, as it always has.

### The resident program

Every line that **acts** is kept, in the order typed; every line that merely
**answers** is not, because a question you asked is not part of your program.
Re-entering a declaration replaces the earlier one **in place**, which is how
you fix a function and try again without line numbers to manage:

```text
> function sq(n)
... return n * n
... end function
> sq(3)
9
> function sq(n)
... return n * n * n
... end function
> sq(3)
27
> list
  1  function sq(n)
     return n * n * n
     end function
```

Note that `sq(3)` is not in the listing: it was a question. `print sq(3)` would
be, because printing is something the program does.

**Merely** is the load-bearing word. A call that acts *and* answers is kept,
and the answer is still shown:

```text
> nodes = []
> append(nodes, "a dog")
["a dog"]
> count(nodes)
1
> list
  1  nodes = []
  2  append(nodes, "a dog")
```

`append` changed something and is in the program; `count` only reported and is
not. The question is asked of the run, not of the shape of the line — nothing
in `name(args)` tells one from the other — so a function of your own that
writes a file is kept even though it answers, and one that merely builds an
array of its own to return is not, even though it called `append` to build it.

Two rules follow from not being able to see inside:

* Anything in the **file** family that is not plainly a question — `write`,
  `copy`, `delete`, `make_dir`, `lock` — is kept. `read`, `exists`,
  `file_size` and their neighbours are not.
* A call into a **built-in module** (`sqlite`, `pg`, `http`, `xlsx`, `gi`, …)
  is kept, whether it read or wrote, because nothing at the call site says
  which. Write `? db.query(sql)` when you only want to look: `?`
  declares a line a question.

If a line you did not want turns up in `list`, `delete` its number.

Redefining a function is refused in a *file* — the first of two definitions
becomes unreachable and you cannot see which one runs — and accepted at the
prompt, where there is nothing left to be unreachable and typing it again is
how you edit it.

### Prompt commands

| command | what it does |
| --- | --- |
| `? expr` | show the value of `expr` |
| `list [name]` | show the resident program, or one declaration |
| `delete n\|name` | remove entry `n` (the number `list` shows), or a declaration |
| `run` | start a fresh session and run the resident program |
| `new` | forget the resident program and the session |
| `vars` | show the names this session holds |
| `cls` | clear the screen |
| `save "file"` | write the resident program to a file, exactly as typed; says how many lines |
| `load "file"` | read a file into the prompt and run it |
| `help` | the command list |
| `quit`, `bye` | leave (so does end-of-input, and `exit(n)`) |

A session that saw a failure exits with status 1, so a piped session can be used
in a script and a run that reported three errors is not mistaken for success. An
explicit `exit(n)` still wins.

A command name is only a command when it stands as a **whole word**: `run()`
still calls a function you wrote, `listing` is still a variable you named, and
`list = 5` is still an assignment. `? name` is how you read back a variable
whose name a command has taken.

`list` numbers its lines so `delete` has something to name. Those numbers shift
the text, so a listing is for reading — `save` writes the program itself.
`delete` edits the program, not the session: what you already defined is still
defined, and `run` is what starts clean.

`load` is the one that needs care, and the rule is the argument's shape: a
**quoted path** is a file the prompt reads, a **bare name** is still gBASIC's
own `load` statement. `load "report.bas"` opens a file; `load sqlite` loads the
module.

`run` starts a fresh session, so the resident program begins exactly as it would
from a file. A program that only works because of something you typed earlier
fails at the prompt, which is where you want to find out.

### Piped input

With input coming from a pipe rather than a terminal, the banner and the prompts
are suppressed and the output is exactly the program's — so a prompt session can
be scripted and its output compared:

```sh
printf 'x = 2\nprint x * 3\nquit\n' | gbasic --repl
```

Diagnostics go to stderr and are reported as they happen, so a failure on one
line does not end the session:

```text
> print nosuchname
runtime error at <prompt>:1:1: undefined variable: nosuchname
> print "still here"
still here
```

## CLI Flags

Run a program, optionally passing it arguments:

```sh
gbasic FILE [args...]
```

Any arguments after `FILE` bind to the program block's first parameter as a
0-based string array.

Open the interactive prompt (see [The Prompt](#the-prompt)); this is also what
`gbasic` with no arguments does:

```sh
gbasic --repl
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

It reads both shapes a call can take. A **qualified** call names its library
outright (`sqlite.connect(...)`, `stats.zscore(...)`), so the `load` follows from the
qualifier; this is the ordinary shape, because a call into another library must
be qualified. An **unqualified** call is resolved by searching the libraries on
the path for one that provides that function, which is what a library's calls to
its own helpers look like.

What it deliberately does **not** add:

- a library already loaded, under its own name or under an `as` alias;
- a native module that needs no `load` at all (`money`, `process`, `reflect`,
  `rowmodel`, `timer`, `this`);
- anything for a qualifier it cannot find. An unknown qualifier is reported on
  stderr (`warning: unresolved library: wibble`) and nothing is inserted — it is
  far more often a typo or a record field than a library nobody installed.

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

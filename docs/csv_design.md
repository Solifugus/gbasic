# csv: reading and writing delimited files

**Status:** Proposal, 2026-11-02. Asked for by the gBASIC books' volume 3
(`gbasic-books/docs/volume-3/gap-ledger.md`, G1), which measured the gap as
silent corruption: `frame.read_csv` over a three-record catalog with a quoted
comma, a quoted line break and a doubled quote answered **four** records, with
prices in the wrong rows and no error.

## 1. What is there today, measured by reading the source

- `frame.read_csv(path)` (`stdlib/frame.bas`) splits each line on `,`. Its own
  comment says quoted fields are not handled. A quoted comma moves every later
  field one column right; a quoted line break makes two records out of one.
- `market.bas` `_rows_from_csv` splits the same way and **drops** a row whose
  field count differs from the header's, with no warning. A description holding
  a comma loses its row.
- Nothing in `src/` parses CSV. `split(line, ",")` is the only tool a program
  has, and it is the classic integration mistake.

## 2. What is written

```basic
for each item, at in csv.records({file}"catalog.csv")
    print(string(at.line) + ": " + item.sku + " " + item.price)
end for
```

`csv` is a native module like `money` and `process`: no `load`, no optional
dependency, always in the build.

- **`csv.records(f, options)`** is a source; `for each` streams it a record at a
  time, holding one record rather than the file, as `for each line in f` holds
  one line. `f` is a file reference or a path, as for every file builtin. **The
  loop owns the file**, so every exit closes it (end, `break`, a `break` naming
  an outer loop, `return`, a raise), for the reasons `for each line in f`
  gives.
- **`csv.parse(text, options)`** is the same source over text already in
  memory: a body from `webclient`, a field from a database. A separate name
  because a string is already a path to every file builtin, so
  `csv.records("a,b\n1,2")` would quietly look for a file called that.
- **The element is the record.** With a header (the default), it is a record
  of text fields keyed by the header's names, in the header's order. With
  `header: false` it is an array of text.
- **The second loop name is where the record came from**: a record
  `{ line, offset, record, problem }`. `line` is the line the record **starts
  on, counted from 1** as an editor and a reject report count, since a record
  can span lines; `offset` is the byte where it starts, counted from 0;
  `record` is the data record's number, counted from 1, not counting the
  header; `problem` is `unknown` for a well-formed record (§4).

`at.line` counts from 1 while `n` in `for each line, n in f` counts from 0, and
that difference is deliberate: `n` is an index into a sequence of lines, while
`at.line` is a place in a file that a person will look up. A reject report
saying "line 0" is a defect. The reference says this beside both.

## 3. The format read (RFC 4180, and what real files do beyond it)

- A field is quoted when it **starts** with the quote character. Inside quotes,
  the delimiter and line breaks are content and `""` is one `"`. A quoted field
  ends at a quote followed by the delimiter, a line break or end of input.
- **Line breaks.** A record ends at `\r\n`, `\n` or a lone `\r` outside quotes.
  A line break inside quotes is kept **as written**: the reader never alters
  text. Every one of the three counts as one line for `at.line`.
- **A UTF-8 byte order mark** at the very start of the input is not part of the
  first field. Anywhere else it is content.
- **Text is bytes.** Fields are counted bytes, so a NUL is content, as
  everywhere in gBASIC. No encoding is converted or guessed.
- **A blank line** (nothing between two line breaks, outside quotes) is skipped,
  and still counted for `at.line`. Python's reader does the same. A line of only
  delimiters (`,,`) is not blank: it is a record of empty fields.
- **A quote inside an unquoted field** (`5" pipe`) is kept as text. Catalogs
  write inches that way, and a refusal would refuse real files for nothing.
- **Text after a closing quote** (`"abc"def,`) is a problem (§4): the field is
  ambiguous, and a guess is how prices land in the wrong row.
- **An unquoted field is not trimmed.** `a, b` has a second field ` b`. A
  program that wants trimming says so with `trim`.
- **An unterminated quote at end of input always raises**, naming the line the
  quote opened on. Nothing after an open quote can be read with confidence, so
  this is not a deliverable problem.

## 4. A record that does not fit

A record with **fewer fields** than the header, or **more**, is reported on
that record, never dropped and never padded. So is text after a closing quote.

- **By default it raises**, naming the line, the record and the counts:
  `csv: line 14 (record 13) has 4 fields where the header has 3`. The error's
  `details` carry `line`, `offset` and `record`. A load that stops is better
  than one that stores wrong data.
- **With `problems: "deliver"`** the record is handed to the loop instead, with
  `at.problem` saying what is wrong and `at.fields` holding **every** field read,
  in order, as an array of text. The element itself holds the fields that have
  a name: a short record lacks the missing names, so reading one raises
  (`unknown record field`), and a long record's extra fields are only in
  `at.fields`. This is the mode for a load that sets bad rows aside with their
  line numbers and continues.
- **An empty field is not a missing one.** `a,,c` has a middle field `""`. A
  record `a,b` under a three-column header is missing its third field. The
  first is data; the second is a problem.

## 5. Options

A closed set; an unknown option is refused, naming the ones there are.

| option | default | |
|---|---|---|
| `delimiter` | `","` | one byte, not the quote, not `\r` or `\n`: `"\t"`, `"\|"`, `";"` |
| `quote` | `"\""` | one byte, or `""` for a file with no quoting at all |
| `header` | `true` | `false`: no header line, and each element is an array |
| `names` | none | the field names, for a file with no header line; refused beside `header: true` |
| `problems` | `"raise"` | or `"deliver"` (§4) |

A header with an empty name or the same name twice is refused before the first
record, naming the column. The names are taken as written, not trimmed or
lowered, so `item.Price` and `item.price` stay different fields.

## 6. Writing

- **`csv.line(values, options)`** answers one record as text, ending with the line
  ending. `values` is an array, or a record written in its own field order. A
  field is quoted **only when it needs it**: when it holds the delimiter, the
  quote, `\r` or `\n`. A quote inside is doubled. So the output survives the
  next reader's naive `split`, wherever it can.
- **`csv.write(f, rows, options)`** writes a header and the rows to a file,
  replacing it. The header is the first record's field names, or `names:`;
  every later record must have exactly those names, or it is refused naming
  the row, since a column that shifts is the defect this module exists to end.
- A value that is not text is written as `string()` writes it: numbers as the
  shortest decimal that reads back, money and dates in their text forms.
  `unknown` and `nothing` are written as an empty field. An array or record
  inside a field is refused.
- Options: `delimiter`, `quote`, `names`, and `line_end` (default `"\n"`; a
  program writing for a system that wants CRLF says `line_end: "\r\n"`).

For a file too large to build in memory, a program writes the header with
`write(f, csv.line(names))` and each record with `append(f, csv.line(row))`.

## 7. Not done, and why

- **No type guessing.** Every field is text. Typing is the program's decision,
  or G-SQL's on store (`gsql_store_conversion_design.md`). `frame.read_csv`
  keeps its documented inference, now over correctly split fields.
- **No formula-injection escaping** (`=cmd` in a cell a spreadsheet will open).
  It changes the data, and only a program knows whether its reader is a
  spreadsheet.
- **No encoding conversion.** A Latin-1 file reads as its bytes.

## 8. The callers

- `frame.read_csv` reads through `csv.records`, so its quoted-field gap closes
  and its comment goes.
- `market`'s `_rows_from_csv` reads through `csv.parse`. A record that does not
  fit makes the fetch fail, as a value, as every other market failure does,
  naming the line, instead of losing the row.

## 9. Evidence

`tests/run_csv.sh`:

- **Goldens** for every rule in §3 and §4, one fixture each.
- **A differential against Python's `csv` module**, over generated files mixing
  quoted delimiters, quoted line breaks, doubled quotes, empty fields, CRLF and
  lone CR, every delimiter, and no quoting. Python's reader is an independent
  implementation, and it is the oracle only where the two specify the same thing:
  the generator never writes text after a closing quote, where Python is
  lenient. Line numbers are checked against the generator's own record of where
  each record started, not against either reader.
- **Writing round trips** both ways: `csv.line` output read by Python, and
  Python's `csv.writer` output read by `csv.records`, including fields that
  need quoting and fields that do not.
- **Bounded memory**: peak resident memory streaming a file, flat across a range
  of file sizes where reading the whole file grows.
- **The loop owns the file**: every exit, under `valgrind --track-fds`.
- **The callers**: the books' three-record catalog through `frame.read_csv`, and
  `market` failing on a long record, naming its line.

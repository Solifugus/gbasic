# Shapes: a record's declared form

Status: **ruled 2026-11-26; increments 0 to 3 built 2026-11-26**
(tests/run_reserved_forms.sh, tests/run_shapes.sh): the block, instances,
fixed fields, writes, methods and shaped fields, the shape as a lock (§5),
and the boundaries (§6.1-§6.3: CSV, `json_schema`, G-SQL rows and
`gsql.expect`). `llm.ask` with a shape (§6.5) goes with the AI-engineering
work; the compiler (§7) is increment 4. The concept
was agreed by Matthew on 2026-11-25 ("It feels like a more intuitive and
higher level C struct"), with three rulings: the word is worth having (as a
contextual keyword, ruled the same day), fixed means fixed (§4.2), and a shape
never reaches the wire (§4.5). The five questions this note asked were ruled
on 2026-11-26 (§11), one of them refined: a missing field WITH a default takes
it silently (§5). §9 sketches where
contracts would go after 1.0, positions only, so that nothing before 1.0
takes them. The examples are not runnable yet; each becomes a fixture in the
increment that builds it (§10). Every claim about TODAY'S grammar was
measured against the 0.12.0 tree on 2026-11-26 and is marked so.

## 1. What a shape is

A shape is a record whose fields are declared: their names, and for each, a
data type, another shape, or nothing in particular. It is written once and
then describes the same data everywhere it appears:

```basic
shape invoice
    number:text
    customer:text
    issued:date
    amount:money(USD)
    paid:boolean = false
end shape

inv = new invoice with { number: "A-17", customer: "Ann", issued: "2026-11-02", amount: "12.50" }
for each row:invoice in csv.records(f, invoice)      ' a csv file, each record converted
for each row:invoice in shop.invoice[.paid]          ' G-SQL rows
inv:invoice = decode(body)                           ' JSON from a request
r:invoice = llm.ask(m, prompt, invoice)              ' a model's structured answer
```

Today each of those has its own way of being checked, or none: csv fields are
text, JSON is whatever came, a model's answer is a string to be hoped about.
A shape makes them one thing: **converted where the data enters, refused by
field name and line where it does not fit**, with the messages a typed
variable already gives. The rest of the program then works with values whose
form it knows, and the compiler gets fixed field positions it can use (§7).

It builds on two things that exist: **typed variables** (every field line is
written as a typed variable is, `name:type = default`, and converts and refuses
exactly as a lock does; that design's rule 6 left record fields "out of the
first version", and this is where they come in) and **PBI** (the shape's name is an ordinary record,
the prototype, so `new`, `with` and the field policies work unchanged).

## 2. Syntax

### 2.1 The block

```basic
shape account
    owner:text = "unnamed"          ' typed, with a default
    balance:money(USD) = "0.00"     ' the default converts, as a lock's value does
    opened:date                     ' typed, no default: starts as nothing
    id (reset next_id()):number     ' a PBI policy, where PBI puts it: before the colon
    tags = []                       ' untyped: no colon, holds any value
    holder:person                   ' shaped: another shape
    function deposit(amount)        ' a method, declared inside (§2.3)
        this.balance = this.balance + amount
        return this.balance
    end function
end shape
```

- One field per line. A line is a typed assignment (`name:type`, optionally
  `= default`), an untyped one (`name = value`), or a `function`. Nothing
  else: a statement inside a shape (`print`, an `if`) is refused at load, as
  a statement at a library's top level is today.
- **Defaults are literals or constant expressions**, evaluated once when the
  shape is declared, as a record literal's values are. A default that does not
  convert to its field's type is refused at the declaration, naming the
  field. `reset <expr>` keeps PBI's meaning: re-evaluated at each `new`.
- **No default means `nothing`**, as `x:number` with no value does today. A
  typed field holding `nothing` or `unknown` is legal, as a lock allows
  (typed_variables_design.md, rule 3).
- Field names follow record keys, case-sensitive; a keyword is a legal field
  name (`next:node`, built: run_shapes), as it is in a record literal today
  (tests/run_keyword_fields.sh).

**What building increment 1 settled** (2026-11-26):
- **Defaults are literals, enforced at parse time.** A shape is hoisted, and
  the hoist runs no user code (run_pre_registration's contract, which gBASIC
  Studio's hoisting mirrors), so a default calling a function would run it
  before the program began. `reset` covers a computed value.
- **Methods are the shape's, found through the instance**, never fields
  (§2.3); a `constructor` is refused until it is designed.
- **A shaped field may name the shape itself** (`next:node = nothing`); any
  other shape must be declared above.
- **The keyword is a statement's first word**: `fam.shape then` at a line's
  end (stdlib/ari_discover.bas) is a field and a keyword, which run_check's
  stdlib tier caught.
- **Not yet**: shapes in libraries (refused, naming this), the shape as a
  lock, CSV/JSON/G-SQL boundaries, the compiler's field positions.

### 2.2 Field kinds

| Kind | Written | Holds | A write |
| --- | --- | --- | --- |
| typed | `amount:money(USD)` | that data type, or `nothing`/`unknown` | converts, or refuses naming the field and the shape's line |
| shaped | `holder:person` | an instance of that shape | a plain record converts into the shape field by field (§5) |
| untyped | `tags = []` | anything: an array, a record of no shape, a function value | stored as is |

**No `array of T`, on purpose.** Typed arrays were rejected for typed
variables because every `append` and element write would have to check, and
the cost lands on every array operation in the language. The element type is
named where the array is walked instead: `for each l:line in order.lines`,
the typed `for each` shipped 2026-11-25. Revisit only if programs show the
need.

### 2.3 Methods

In PBI a method is a field holding a function value, attached after the fact
by `function account.deposit(...)`. On a fixed shape that would be adding a
field, which §4.2 refuses. So a shape's methods are declared inside its block,
with `this` bound per call exactly as today. `function account.deposit(...)`
outside the block, on a shape, is refused, naming the block to put it in.

**A method belongs to the shape, not to the instance.** It is not a field:
`a.deposit(...)` finds it through the instance's shape, and `print`,
`encode`, `=` and `has` see only the fields. That is what keeps §4.5 true
for a shape with methods (a PBI method is a field holding a function value,
which `encode` refuses), and it makes `a.deposit = 0` simply a write to a
field the shape does not declare, refused as one, naming the method.

### 2.4 Derivation between shapes

**Ruled (Q1): left out of the first version.** A shape built from another
(`shape savings from account`, adding `rate:number`) is the obvious next
want, and PBI's `new` already derives one record from another, but Matthew:
"Get one complete, dependable shape model working before adding
inheritance." Every shape is declared whole. It can be added later without breaking anything,
since `shape NAME from` is a syntax error under this design's first grammar.

## 3. The word: a contextual keyword

`shape` cannot be reserved: `frame.shape(df)` is used ten times in the tree,
`stats.bas` has a `shape` parameter (`gamma(shape, rate)`), and test programs
use it as a variable. It is a keyword **only at the start of a statement,
followed by a name and the end of the line** (the position `server` and the
PBI policy words already use: an ordinary identifier everywhere else).

Measured against today's grammar (2026-11-26, `gbasic --check`):

| Form | Today | Under this design |
| --- | --- | --- |
| `shape account` + NEWLINE | syntax error ("expecting `(`") | opens a shape block |
| `shape = 3`, `print shape` | parses | unchanged |
| `function shape(x)`, `shape(2)` | parses | unchanged |
| `function g(shape, rate)` | parses | unchanged |
| `frame.shape(df)` | parses | unchanged |
| `shape (x)` | parses (a call) | unchanged: a call |
| `a:account = ...` (a lock naming a shape) | syntax error ("not a data type a lock can name") | a shape lock |
| `function post(e:account)`, `for each r:invoice in ...` | syntax error (the same) | a shape lock |

So **every form this design gives meaning to is a syntax error today**, and no
program that runs today changes meaning. A tier holding this table (§10,
increment 0) is written before the grammar changes.

## 4. Instances

### 4.1 Making one

```basic
a = new account with { owner: "Ann", balance: "12.50" }
b = new account                      ' every field its default
```

`new` derives from the prototype as today; `with` overrides fields, each
converted as a write would be (§4.3). The prototype itself is the shape's
name, an ordinary record holding the defaults: `print account` prints them.

### 4.2 Fixed means fixed

- Writing a field the shape does not declare is refused: `a.nickname = "A"`
  says "account has no field nickname (account is declared at line 1)".
- `with { nickname: ... }` is refused the same way, at the `new`.
- `remove_key`, `merge` and the other builtins that build a NEW record from a
  shaped one answer a plain record, unshaped: they make a new value, they do
  not change the instance.
- PBI's `exclude` stays legal only in the declaration.

### 4.3 Writes and reads

A write to a typed field converts or refuses exactly as `x:T = e` does,
including text read as the type (`a.balance = "12.50"`). A write to a shaped
field converts a plain record into that shape (§5). A read is an ordinary
field read; a missing field cannot exist, so `a.f` never raises for a declared
`f`. Compound assignment (`a.balance += "1.00"`) converts the text to the field's
type first, as it does for a locked variable, so it adds money; note that
`a.balance = a.balance + "1.00"` is a different statement, since `+` joins
whenever one side is text (measured 2026-11-26 on a `money(USD)` lock: `+=`
gives 14.50, the spelled-out `+` is refused as "12.502.00").

### 4.4 Equality and printing

**Ruled (Q2): a shaped record compares by its fields**, exactly as a record
does today (`=` and the same-value routes ignore the shape), and prints as a
record does. The shape is about what may be written, not what the value is,
so `new account = { owner: "unnamed", ... }` with the same fields is `true`.
The alternative, a shape making two records unequal, makes `contains` and
`find` depend on where a value came from.

### 4.5 What leaves the program (ruled)

`encode`, `json_encode`, `print` and `string` write a shaped record as plain
data. Reading it back is `a:account = decode(text)`. Nothing outside gBASIC
has to know shapes exist. A field holding a typed value (a date, money) encodes as it
would in any record: `json_encode` refuses one today, and `notation` keeps
it; a shape changes nothing about that.

## 5. A shape as a lock

The shape's name may stand wherever a data type does in a lock:

```basic
a:account = decode(body)                 ' a variable
function post(e:account):receipt         ' a parameter and a return type
for each row:invoice in rows             ' a for each element
```

Converting a plain record into a shape, field by field:

1. **A declared field present in the record**: converted to its kind, or
   refused naming the field, the value, and the shape's line.
2. **A declared field missing from the record, with a default**: takes the
   default, SILENTLY. Declaring a default says absence was anticipated.
3. **A declared field missing, with no default**: `nothing`, and a warning.
4. **A field the record has and the shape does not**: dropped, and a warning.

Ruled 2026-11-26 (Q3, refined by Matthew), following his principle (robust
by default, 2026-11-25): refuse what was DECLARED (a type), tolerate what is
incidental (a JSON object with one more field, a CSV column nobody asked for)
**and say so** where nothing anticipated it. Each warning goes through the
warning channel, once per site and field; `on warning stop` already makes it
strict, and `on warning ignore` silences it.

An instance of the shape passes through a lock unchanged; an instance of a
DIFFERENT shape is converted as a plain record would be.

**What building increment 2 settled** (2026-11-26):

- The lock is a lock: `LOCK_SHAPE` beside the data types in the variable's
  `TypeLock`, so a parameter, a return, a `for each` element and a later
  plain `=` all go through the one `lock_convert`, and messages name it as
  they name a type (`a is locked to account by a:account at line 5`).
- **Unknown names are refused at parse time, not at run time.** A shape may
  be declared below its first use, so the parser keeps each lock name that is
  no data type and checks it once the file is parsed, against the shapes the
  file declares (and, at the prompt, those the runtime already has, through
  `gb_parse_shape_known`). `x:nubmer` therefore stays a syntax error, now
  with "did you mean number?" -- the one thing a shape lock could have cost.
- Without a `program` block nothing is hoisted, functions included, so a
  script locking above its shape is refused when the line runs, saying so.
- Under `on warning stop`, an escalated 2126/2127 keeps its own words rather
  than being wrapped as "does not convert".
- The prompt could not take a multi-line shape: the body's own message
  replaced bison's "unexpected end of file", which is what the prompt reads
  as "ask for more". Fixed by leaving the end of input to bison.

## 6. Where data enters and leaves

### 6.1 CSV

```basic
for each inv, at in csv.records(f, invoice)
    total = total + inv.amount           ' already money(USD)
end for
csv.write(f, invoices)                   ' header from the shape, in declared order
```

- **The header is matched once, at open**: by field name, then by the name
  with case and spaces and `_` ignored ("Invoice Date" finds `invoice_date`).
  A field no column matches follows §5's rules 2 and 3 (its default
  silently, else `nothing` and a warning naming it); a column no field
  matches is dropped, and the warning names it.
- Each record converts as §5 says; a bad cell is refused naming the LINE
  (`csv.records` already knows where each record starts) and the field.
- **A column named nothing like its field** ("Amt (USD)" for `amount`):
  **ruled (Q4) deferred** until the broader renaming decision (the braces
  question deferred in November). A spelling on the field, such as
  `amount (column "Amt (USD)"):money(USD)` in the policy slot, is one
  candidate. The first CSV increment has no aliases.

### 6.2 JSON

`x:shape = decode(text)` (§5). `try_decode` composes: decode first, then the
lock. A shape also generates its **JSON Schema**, `json_schema(invoice)`, the
same schema `tools` already writes for a tool's parameters (one generator,
not two).

### 6.3 G-SQL

- **A row read is converted into a shape**: `for each c:customer in shop.customer[.active]`.
- **A shape is checked against its table when the program connects**, by an
  explicit expectation the program states (**ruled (Q5)**: an explicit
  connection-time check; `gsql.expect(db, "customer", customer)` is
  reasonable, its final spelling open). A missing column or a different kind
  is reported at connect, naming both, not at the first write.
- **Shapes do not create tables.** Keys, indexes, foreign keys and required
  columns are storage, not form. Matthew (2026-11-25): schema from gBASIC is
  where this may grow, later; nothing here forecloses it.
- The kinds map one to one: number (an `integer` column too), text, boolean,
  date, time, datetime, duration, money, and a shaped field a `record`
  column. A field of a kind G-SQL lacks (`file`, `dir`) cannot describe a
  column, and `expect` says so. (This list first left out `time`, which G-SQL
  has: corrected when building it.)

### 6.4 Frames

`frame.read_csv(path, invoice)` types the columns by the shape instead of
guessing from the first cell, and `frame.to_rows(df)` answers invoices: a
shape as Pandas' `dtype=` or a pandera schema. Library work (frame.bas), after
the core.

### 6.5 A model's structured answer

`llm.ask(m, prompt, invoice)` sends `json_schema(invoice)` as the response
format and converts the reply into the shape, each field that does not fit
refused by name: the most common thing an AI engineer checks by hand. This is
the reason shapes come before the AI-engineering increments in the 1.0 order.

**What building increment 3 settled** (2026-11-26):

- **A shape is passed as a value.** A shape's name is bound to its prototype,
  a shaped record, so `csv.records(f, invoice)` needs no new grammar: a verb
  reads a shaped record in the argument after the data as the shape, and a
  plain record there as the options. An instance names its shape too.
- **CSV's warnings are said at open**, from the header, once however many
  records follow: per record they would be the same sentence a million times.
  A cell refused names its line, record and column (`error.details` too), the
  field's own words after; under `problems: "deliver"` it is the record's
  problem and its field `nothing`. An empty cell is `nothing` outside a text
  field, so what csv.write writes for `nothing` reads back as `nothing`.
  Ambiguity is refused at open: two columns for one field, one column for two
  fields, a column for a shaped field (a record in one cell).
- **WHAT THE SCHEMA ADMITS, THE LOCK CONVERTS.** That is the property
  `json_schema` is held to, by 3000 generated documents judged against the
  schema from the JSON Schema and RFC 3339 specs. Measuring it moved three
  things: RFC 3339's `date-time` admits fractional seconds and a lowercase
  `t`, its `time` requires an offset, and gBASIC's locks refuse all three, so
  `date` and `datetime` carry a pattern beside their format and `time` a
  pattern alone; and the duration lock refused all text, though `{duration}`
  has read it since abbbd3bb, a stale rule now gone (DOGFOOD 2026-11-26). The
  control reads the same schema without its patterns and must find answers
  the locks refuse; it finds dozens.
- **"One generator, not two" is not yet true.** `tools.param_schema` writes
  its own schema from a tool's `params` list, which is not a shape; the two
  agree on the keys' order and on JSON Schema's type words (`tools` wrote
  `"record"`, no JSON Schema type, until this increment). They become one when
  `tools` takes a shape for its parameters, with `llm.ask` (§6.5).
- **`gsql.expect` names every problem at once** and does not stop at the
  first: a schema mismatch is a list to fix, and a field with a default still
  needs its column (a default is for a record missing a field, not a table
  missing a column). A column the shape does not name is no problem, since a
  shape may read part of a row, but reading `select *` into such a shape
  warns 2126 at each site, as any extra field does.

## 7. The compiler

A shape fixes a record's field set and order, so a field's position is known
at load: `r.x` on a shaped `r` is an index, not a hash lookup, and a `number`
field can be held in a slot as a typed variable is (the increment-3 report's
B2). The interpreter keeps the record's hashed map; the compiler gains the
positions. Measured nothing yet: the report's estimate for B2 stands until an
increment measures it.

## 8. What shapes do not do

- No typed arrays or `array of T` (§2.2).
- No keys, indexes or constraints (§6.3), and no inheritance in the first
  version (§2.4, Q1).
- No shapes on the wire (§4.5).
- No change to a record that is not shaped: a program that never writes
  `shape` runs exactly as it does today.
- No field-level validation beyond the type (`balance >= 0`). That is a
  contract (§9), not a shape.

## 9. Contracts, after 1.0: where they would go

Matthew (2026-11-26): contracts and proofs are "a functionally different
thing" that may add syntax after 1.0, keeping backward compatibility, and
"we need to at least map out contracts and proofing before the 1.0 release".
This section maps **positions, not semantics**, so that no pre-1.0 feature
(shapes most of all) takes one.

### 9.1 The positions

```basic
function withdraw(a:account, amount:money(USD)):money(USD)
    requires amount > 0                          ' directly under the header
    requires amount <= a.balance
    ensures result >= 0                          ' `result`: the returned value
    a.balance = a.balance - amount
    return a.balance
end function

while i < n
    invariant i <= n                             ' directly under a loop header
    i += 1
end while

shape account
    balance:money(USD) = "0.00"
    invariant balance >= 0                       ' inside a shape: holds after every write
end shape

property "a deposit then a withdrawal of it leaves the balance"   ' top level
    ...
end property
```

### 9.2 Each is a syntax error today

Measured 2026-11-26 with `gbasic --check`:

| Form | Today |
| --- | --- |
| `requires n >= 1` under a function header | syntax error |
| `ensures result >= 0` under a function header | syntax error |
| `invariant i <= 3` under a `while` | syntax error |
| `property "name"` ... `end property` at the top level | syntax error |
| `requires`, `invariant`, `property` as variables or functions | parse, and must keep parsing |
| **`requires (n >= 1)`** | **PARSES: a call to a function named `requires`** |

The last row is the constraint. A contract word followed by `(` is a call
today, and must stay one: a 1.0 program may define `function requires(x)`.
So a contract's condition never begins with a parenthesis (write `requires n
>= 1`, or `requires not (a and b)`), and the contracts release keeps a line
`requires (...)` meaning the call. Every word stays contextual (`property`
occurs 115 times in the tree, `requires` 64, `invariant` 17, comments
included).

### 9.3 What 1.0 must do

- **A tier holding every row of §9.2**: each reserved form stays a syntax
  error and each name use keeps parsing, until the contracts release removes
  those cases deliberately. Built with this note (§10, increment 0).
- **The shape grammar refuses an `invariant` line** inside a shape block in
  1.0, as it refuses any non-field line (§2.1), and that tier covers it.
- Order after 1.0, as discussed: contracts checked at run time first, then
  properties tested over generated data (`fake.bas`), static proof only where
  it is decidable.

## 10. Increments

0. **The reserved-forms tier** (§9.2 and §3's table), before any grammar
   changes. Small; can go in with this note.
1. **The block, instances, fixed fields, writes** (§2, §4): the contextual
   keyword, typed/untyped/shaped fields, defaults, `new`/`with`, refusals by
   field and line, methods in the block, `--ast`, valgrind, zero grammar
   conflicts. Every example in §2 and §4 a fixture.
2. **The shape lock** (§5): variables, parameters, returns, `for each`; the
   two warnings.
3. **The boundaries** (§6): `csv.records(f, shape)` and `csv.write`;
   `json_schema`; G-SQL rows and `expect`; then `llm.ask` with a shape in the
   AI-engineering work.
4. **The compiler** (§7): field positions and held number fields, measured.
5. **Frames** (§6.4), library work.

## 11. Rulings (Matthew, 2026-11-26)

1. **Derivation**: left out initially. "Get one complete, dependable shape
   model working before adding inheritance."
2. **Equality**: by field values, the shape ignored. "That preserves normal
   record behavior."
3. **Missing and extra fields**: a missing field with a default takes it
   without a warning ("Declaring a default means absence is anticipated");
   one without a default is `nothing` with a warning; an extra field is
   dropped with a warning. "`on warning stop` already provides strict
   behavior."
4. **CSV aliases**: deferred until the broader renaming decision.
5. **G-SQL**: an explicit connection-time expectation check;
   `gsql.expect(...)` is reasonable, the final spelling can wait.

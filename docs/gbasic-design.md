# gBASIC Language Design

This is the single, current design document for the gBASIC **language and core
runtime**. It supersedes the original `gBASIC_v0_1_core_design_and_grammar.md`
draft and the per-feature plan/progress trackers for comparison lenses,
date/time comparison, and watcher hardening.

Scope and companion documents:

- **This document** — language philosophy, value model, syntax/grammar, and the
  semantics of expressions, modifiers/lenses, control flow, functions, watchers,
  and error handling.
- `reference.md` — the behavior manual: exhaustive builtin/operation reference.
- `tutorial.md` — a guided introduction.
- Library/module designs, each kept separate: `sqlite_design.md`,
  `postgres_design.md`, `webclient_design.md`, `webserver_design.md`,
  `gui_design.md`.
- `historical_development_archive.md` — completed-phase history.
- `gbasic_dogfood_notes.md` — running list of language/library friction found
  while building real programs.

Status: gBASIC is `0.1.0-dev`. Where this document describes something not yet
built, it is marked **(future)**.

---

## 1. Purpose and Philosophy

gBASIC is a modern BASIC-family language that preserves BASIC's immediate
readability while adding modern data structures, contextual typing, and reactive
programming.

Design principles:

- readable to non-specialists; expressive without ceremony
- context-sensitive where that reduces syntax
- extensible without polluting the global namespace
- permissive enough to keep BASIC's spirit, structured enough for serious software

The central idea:

```text
meaning is applied through context, not encoded in special literal glyphs
```

So gBASIC has no special glyphs for money, dates, or files. Instead of
`balance = $19.95` it uses an **operator modifier**: `balance(USD)= 19.95`.

### Implementation architecture

The shipping implementation is a tree-walking interpreter in C11:

```text
source -> hand-written lexer -> Bison parser -> AST -> tree-walking evaluator
```

A C-emitter path (`gBASIC -> AST -> C -> GCC -> native executable`) remains a
long-term goal **(future)**; nothing in the language design should preclude it.

---

## 2. Lexical Structure

- **Comments:** single-line, introduced by an apostrophe `'`. (`//` is reserved
  for a possible future form but is not part of v0.1.)
- **Strings** are sequences of bytes, conventionally UTF-8. Because strings are
  NUL-terminated internally, a string cannot contain a 0 byte; `chr(0)` is an
  error. True binary-safe strings (length + bytes) are **(future)** and would be
  required for raw binary data and to let `chr` cover the full 0–255 range.
- **Newlines are significant** as statement terminators.
- **Keywords are context-sensitive where possible.** Notably `=` is assignment
  or comparison depending on position, and a parenthesized term adjacent to an
  assignment/comparison operator is a modifier rather than a call (see §5).

Core token classes include identifiers, qualified identifiers
(`library.name`), numbers, strings, the modifier/lens content tokens, the
operators below, and structural punctuation `( ) [ ] { } , . :`.

---

## 3. Values and Types

Built-in value kinds:

- **number** — IEEE double. gBASIC has no exact-integer type yet; large integer
  precision can be lost, which is why some module mappings keep big integers as
  strings.
- **string** — byte sequence (UTF-8 by convention); see the NUL caveat above.
- **boolean** — `true` / `false`.
- **array** — ordered, heterogeneous: `[88, 91, 74]`.
- **record** — named-field object with colon syntax: `{name:"Ada", active:true}`.
  Fields may be accessed statically (`customer.name`) or dynamically
  (`customer[field]`). A missing dynamic read yields `unknown`.
- **money** — created by an assignment modifier (`balance(USD)= 19.95`); stored
  as exact cents. v0.1 supports USD; other currencies are **(future)**.
- **date/time** — first-class value carrying an explicit precision (see §6).
- **duration** — first-class: `1 hour 20 minutes 2 seconds`; may carry years,
  months, weeks, days, hours, minutes, seconds, milliseconds. Calendar-relative
  durations (`+ 1 month`) are distinct from exact durations (`+ 30 days`).
- **file / directory reference** — a typed reference to a filesystem location
  (not an open handle), created by the `file` / `directory` modifiers.

### `nothing` vs `unknown`

These are distinct:

- `nothing` represents deliberate absence.
- `unknown` represents an unavailable / not-yet-known value, and is what a
  missing dynamic record read returns. Test with `is_nothing()` / `is_unknown()`.

In ordering and collections, `nothing` and `unknown` rank ahead of ordinary
values.

---

## 4. Expressions and Operators

### Arithmetic (strict)

`-`, `*`, `/` require numbers and raise a runtime error otherwise. `+` performs
numeric addition unless either operand is a string, in which case both operands
are converted to their canonical string form and concatenated.

### Comparison and logic operators

```text
=   equal           !=  not equal
>   greater than    <   less than
>=  greater/equal    <=  less/equal
!>  not greater than !<  not less than
and   or   not
```

Parentheses group conditions. `=` is comparison in condition/expression
position and assignment in statement position.

Comparison between incompatible kinds (e.g. a date/time value and a string or
number) raises a runtime error rather than silently coercing.

---

## 5. Operator Modifiers and Comparison Lenses

A **modifier** is a parenthesized term adjacent to an operator that changes the
meaning of an assignment or comparison. Modifier terms live in their own
namespace and do not collide with ordinary variables: in `name(caseless)= "joe"`
the `caseless` token resolves as a modifier, while `caseless` elsewhere is an
ordinary identifier.

### Assignment modifiers — parenthesized (permanent syntax)

```basic
balance(USD)= 19.95
dob(date)= "1970-06-11"
config(file)= "data.txt"
age(number)= input("Age: ")
```

Spacing around the parentheses is not significant; all of `x(USD)=`,
`x (USD)=`, `x(USD) =`, `x (USD) =` are equivalent.

### Comparison lenses — brace syntax (canonical)

A comparison modifier is written as a brace-delimited lens placed between the
left operand and the operator:

```basic
if name {caseless}= "joe" then ...
if amount {rounded 2}= expected then ...
if d {day}= t then ...
```

The brace form is the **canonical** comparison-modifier syntax. The older
parenthesized comparison form (`name(caseless)= "joe"`) still parses and runs but
is **deprecated**; removing it and emitting deprecation diagnostics is **(future)**.

Lens content is captured as raw text by a dedicated lexer mode, so multi-word
and user-defined modifier names with textual arguments are preserved. The
runtime resolves a lens by choosing the longest declared modifier-name prefix
and treating the remainder as arguments. Resolution order is: local definitions,
then loaded libraries, then built-ins. Library-qualified lenses are allowed:
`name {text.caseless}= "joe"`. Exactly **one** lens may appear per comparison;
chaining is not supported. An unknown compare modifier raises
`compare modifier not found: <label>` (code 1003).

Built-in lenses: `caseless` (string), `rounded` (numeric; `{rounded 2}`,
`{rounded to 2}`, `{rounded places}`), and the date/time precision lenses
`year month day hour minute second` (see §6).

### Custom modifiers

Modifiers are user-definable, which extends the language without new global
keywords:

```basic
modifier caseless for compare
    return lower(left) = lower(right)
end modifier

modifier USD for assign
    return money(value, "USD")
end modifier

modifier rounded(n) for compare
    return round(left, n) = round(right, n)
end modifier
```

Compare modifiers see `left`, `right`, and `operator`; assign modifiers see
`value`. Contexts are `assign` and `compare` (future contexts such as `sort`,
`format`, `parse`, `match` are possible). Multi-word signatures are allowed
(`modifier rounded to for compare`).

### Disambiguation mechanism and a known limitation

Modifier-vs-call disambiguation is resolved at **tokenization time** by raw-source
lexical lookahead, not by the grammar alone: when an identifier is followed by
`(`, the lexer scans to a same-line `)`, rejects comma-separated content,
requires the next non-space character to begin an assignment/comparison
operator, and checks whether the identifier names a known builtin or a
`function` declared in the current source. Function and modifier namespaces may
therefore overlap and resolve by context.

**Known limitation (recorded):** an lvalue whose name is also a builtin or
declared function is biased toward call syntax, which can make its
modifier-assignment form unparseable — e.g. once `code` became a builtin,
`code(uppered)= "abc"` no longer parses (the `code(` binds as a call). The
declaration scan is also file-global, not program/library-scoped, and does not
resolve externally loaded/qualified functions. This argues for a documented
reserved-word policy or letting assignments shadow builtins; until decided, any
new builtin is a potential breaking change. (See `gbasic_dogfood_notes.md`.)

---

## 6. Date/Time and Precision-Aware Comparison

Date/time values carry a stored **precision** (`year` → `second`) and a domain
(calendar date/time vs time-only). Storage normalizes each precision to the
start of its period (e.g. `year` → `YYYY-01-01 00:00:00`).

### Bare comparison is exact

Bare operators compare **exactly**: two date/time values are equal only when
they share domain, represented start instant, **and** stored precision. This
makes bare `=` a true equivalence relation (the earlier "lower-of-two-precisions"
model was non-transitive and has been removed).

```basic
2026-05-15 = 2026-05-15            ' true
2026-05-15 = 2026-05-15 00:00:00   ' false (different precision)
2026-05 = 2026-05-01               ' false
14:30 = 14:30:00                   ' false
```

Bare ordering is a strict weak ordering via one shared comparator: domain rank
first (calendar before time-only), then represented start instant, then
less-precise-before-more-precise at the same instant
(`2026 < 2026-01 < 2026-01-01 < …`). `sort()`, `unique()`, and `=` all use this
single relation, so date/time values are valid in collections.

### Precision-aware comparison via lenses

A precision lens truncates **both** operands to the named precision, then applies
the exact relation — "truncate both, then compare exactly." This is the only
mechanism for same-period comparison; there is no "force exact" modifier because
exact is the default.

```basic
d(date)= "2026-05-15"
t(date)= "2026-05-15 12:05:03"

if d = t then ...        ' false (exact)
if d {day}= t then ...   ' true
if d {second}= t then ...' error: d lacks second precision
```

Lenses (`year month day hour minute second`) work with every comparison
operator. The lens path may parse date/time-like strings; bare comparison never
does. A lens that demands more precision than the source carries is a runtime
error, and `year/month/day` lenses on time-only values are rejected.

PostgreSQL mapping (unchanged by the comparison work): `date` → day precision,
`time` → time-only, `timestamp` → second precision, `timestamptz` → string
**(future)**. Consequently a PG `date` and a PG `timestamp` on the same day are
not bare-equal — use `{day}`.

Human conveniences (end-of-month, next weekday, business days, day names) are
**library territory**, built on these core lenses, not core features.

---

## 7. Control Flow

### If / else — inline and block

```basic
if x > 10 then
    print("large")
else
    print("small")
end if
```

A branch may instead hold one non-block statement on the same line as `then` /
`else`. `end if` is omitted when the final branch is inline; a branch that begins
after a newline is a block and needs `end if`:

```basic
if ready then print("ready")
else print("waiting")
```

Inline branches accept assignment, print, call, load, error-control, return,
goto/gosub, break, and continue. Nested conditionals, loops, functions, and
watchers remain block-only. An `else` associates with the nearest unmatched
inline `if`.

### Loops

```basic
for i = 1 to 10            ' optional: step -1
    print(i)
end for

for each item in items     ' `for item in items` is equivalent
    print(item)
end for

while x < 10
    x = x + 1
end while
```

`break` and `continue` are valid inside loops.

### consider

`consider` is a multi-branch match construct (see `reference.md` for full
semantics) with `consider`/`if`/`else` branches and an explicit end.

---

## 8. Functions, Programs, Libraries

```basic
function add(a, b)
    return a + b
end function
```

Functions are not assignable (a call is never an lvalue). User functions may
shadow builtins.

gBASIC also has **programs** and **libraries** as top-level units, with `load`
to bring in libraries/modules (the older `use` keyword is legacy, retained for
compatibility) and `export` to expose library members. The `--add-loads` CLI
mode analyzes unresolved calls/modifiers and suggests `load` statements.

### Labels, goto, gosub

Labels (`name:`), `goto`, and `gosub` are supported, including cleanup-style
patterns. They are scoped to the current function; a jump may not cross function
boundaries.

---

## 9. Watchers

A **watcher** is a reactive block registered against one or more value paths;
when a watched path's stored value changes, the body runs.

### Model and triggering

- Registered with `watch(path, …)`. Every watcher **runs once immediately at
  registration**.
- Triggering is **immediate and synchronous**: matching watchers run and complete
  before execution continues past the mutating statement. There is no batching,
  coalescing, or event-pump deferral.
- **Equal-value guard:** a write triggers watchers only when the stored value
  actually changes. Change detection uses an internal pure storage-equality
  predicate (`value_storage_equal()`), distinct from the language `=` operator —
  it never allocates or raises, compares kinds exactly, compares arrays/records
  deeply (records by field name, order-independent), and compares date/time by
  all stored fields **and** precision. New variables/fields always count as
  changed.
- **Path matching is symmetric at dot boundaries:** a watcher matches when the
  watched and changed paths are equal, or either is a dot-boundary prefix of the
  other. The boundary is mandatory (`state` does not match `statement`).
  `watch(state)` fires on a change to `state.name`; `watch(state.name)` fires
  when `state` is wholesale replaced. Array indexes and dynamic keys collapse to
  the **containing** array/record path — matching is not index/key-aware.

### Cascade, suppression, and runaway protection

- A watcher body may mutate watched state, appending more work to the same
  synchronous drain (fixpoint-like).
- **Pending-only dedup:** within one drain, an already-pending watcher is not
  re-enqueued; it runs once against the latest live state. The pending flag is
  cleared just before the body runs, so a watcher that already ran this drain can
  be legitimately re-enqueued by a later mutation.
- `without watchers … end without` truly suppresses notification for mutations
  inside it (not deferred).
- **Execution cap:** one drain is capped at **10000** watcher executions. Exceeding
  it raises a structured runtime error — code **1005**, source **`watcher`**,
  message `watcher cycle exceeded 10000 executions in one drain cycle` — stops the
  drain, discards remaining queue entries, and clears pending/draining flags.
  Mutations completed before the error remain. Normal `on error` policy applies.

### Collection-mutator notification

Array mutators notify watchers **exactly once after** the stored mutation
completes, via the containing path, but only when the first argument is an lvalue
(identifier/field/index). Functional use on temporary values does not notify.

- always notify: `append`, `prepend`, `insert`, `remove`, `take_first`, `take_last`
- conditional: `remove_value` (only if removed), `reverse`/`sort`/`unique` (only
  if the stored value actually changed)
- not mutators: `remove_key` (returns a new record), `first`/`rest`/`contains`/
  `find`/`find_by`/`join` (read-only), file `append` (I/O).

### Non-goals and limitations

Explicit non-goals: notification batching, transactional coalescing, event-pump
/ safe-point execution, glitch-free topological ordering, and removing
run-on-registration. Known limits: no index/key-aware watcher identities; the cap
is a fixed constant; `error.name` is not exposed (only code/source/message/line/
column). **User-created cyclic value graphs are not currently possible** under
copy semantics, so no recursion-depth guard exists.

GUI and WebServer integrate through this model: GUI batching is GUI-local and,
once a GUI-originated mutation reaches storage, ordinary watcher rules apply;
`server.requests` / `server.responses` are ordinary watched queues.

---

## 10. Error Handling

Runtime errors carry a message, a numeric `code`, and a `source`. Control is
provided by `on error goto <label>`, `on error resume next`, and
`on error stop`. A catchable error value exposes `error.message`, `error.code`,
`error.source`, and source line/column (there is no `error.name`). Filesystem,
type, arity, and the watcher-cycle (1005) failures all flow through this model.

---

## 11. Standard Library and Modules

The core standard library covers general (`print`, `input`, `len`, `count`,
`type`, conversions), strings (`lower`, `upper`, `trim`, `left`, `right`, `mid`,
`replace`, `split`, `join`, `chr`, `code`, …), arrays (`sum`, `mean`, `median`,
`mode`, `min`, `max`, `sort`, and the mutators in §9), records, files/paths,
serialization (`encode`/`decode`), and security helpers (`secure_token`,
`password_hash`, `password_verify`). See `reference.md` for the complete list and
exact behavior.

Optional native modules are loaded with `load` and documented in their own design
docs: `sqlite`, `pg` (PostgreSQL), `webclient`, `webserver`, and the GTK `gui`.
Each is compiled in only when its platform library is available and degrades to a
clear runtime error otherwise.

---

## 12. Grammar Sketch (Bison-style)

This sketch reflects the current parser at design altitude; `src/parser.y` is
authoritative.

```text
program        : statement_list EOF ;

statement      : assignment NEWLINE
               | print_statement NEWLINE
               | call_statement NEWLINE
               | if_statement | for_statement | while_statement
               | consider_statement
               | function_definition | program_definition | library_definition
               | return_statement NEWLINE
               | label_definition NEWLINE
               | goto_statement NEWLINE | gosub_statement NEWLINE
               | watch_statement | without_watchers_statement
               | modifier_definition
               | load_statement NEWLINE
               | break_statement NEWLINE | continue_statement NEWLINE
               | error_control_statement NEWLINE ;

assignment     : lvalue OP_EQ expression
               | lvalue modifier OP_EQ expression ;     ' assignment modifier (parens)

lvalue         : IDENT
               | expression DOT IDENT
               | expression LBRACKET expression RBRACKET ;
               ' a semantic check rejects function-call results as lvalues

modifier       : LPAREN modifier_terms RPAREN ;          ' raw-text content

comparison_expression
               : additive_expression
               | additive_expression comparison_operator additive_expression
               | additive_expression modifier comparison_operator additive_expression   ' deprecated
               | additive_expression comparison_lens comparison_operator additive_expression ;

comparison_lens: LBRACE LENS_CONTENT RBRACE ;            ' raw-text lens content

comparison_operator : OP_EQ | OP_NE | OP_GT | OP_LT | OP_GE | OP_LE | OP_NGT | OP_NLT ;

primary        : NUMBER | STRING | TRUE | FALSE | NOTHING | UNKNOWN
               | IDENT | function_call | array_literal | record_literal
               | primary DOT IDENT
               | primary LBRACKET expression RBRACKET ;

record_literal : LBRACE record_field_list_opt RBRACE ;   ' fields use IDENT COLON expression
```

Key invariant: a `{` that begins an operand is a **record literal**; a `{` that
follows a completed left-hand expression (and precedes a comparison operator) is
a **comparison lens**. This keeps the grammar LALR(1) with zero conflicts and
removes the need for lexer lookahead in comparison parsing (assignment-modifier
lookahead, §5, still exists).

---

## 13. Open Design Questions

- A reserved-word / shadowing policy for builtin names vs user identifiers (see
  the §5 limitation).
- Binary-safe strings (so `chr(0)` works and raw bytes are representable).
- An exact-integer numeric type (would resolve several module precision caveats).
- Whether modifier chaining should ever be allowed.
- Whether file and directory references should remain distinct types.
- Removal of the deprecated parenthesized comparison-modifier form and addition
  of deprecation diagnostics.
- The C-emitter / GCC compilation path.

---

End of gBASIC Language Design.

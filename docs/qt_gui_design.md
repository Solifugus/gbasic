# A Qt GUI for gBASIC — design

**Status: Proposal. None of this exists.** There is no `qt` module, no
`stdlib/qt.bas`, and no Qt backend for anything. Do not code against it.

**Provenance.** Where this says *measured* it means a command was run on
2026-10-14 and the output is quoted or summarised. Where it says *decided* it
means Matthew ruled on it in conversation that day. Everything else is argument.

---

## 0. TWO THINGS DECIDED 2026-10-17, before anything is built

**Qt SQL is NOT part of this, and the question was measured rather than
argued.** Asked whether Qt could give gBASIC better database connectivity than
it has: no, it would be a step backwards. Measured on the development machine,
Qt 6.10.2's own driver packages depend on the client libraries this tree
already links — `libqt6sql6-psql` → **libpq5** (what `pg` uses),
`-mysql` → libmysqlclient24, `-odbc` → **libodbc2** (what `odbc` uses),
`-ibase` → libfbclient2 — and the distribution ships exactly one driver,
`libqsqlite.so`. So Qt SQL is a plugin wrapper over the same dependencies: it
removes none of them, adds a C++ layer, and would give the language **two
incompatible ways to reach a database**, which is the shape this tree puts
tripwires against elsewhere. Oracle and DB2 are worse, those plugins not being
packaged at all.

What a "Go-like" connectivity story actually rests on is **pure wire-protocol
clients** — `pgx`, `go-sql-driver/mysql` and `go-mssqldb` speak the protocols
directly with no C client library, which is why they need nothing installed and
cross-compile; Oracle's `godror` needs the Instant Client and is the exception
that proves it. That is a separate piece of work, unscoped and unstarted, and it
would extend the property the lean build already has (sqlite, libxml2, zlib and
yescrypt are statically linked so the binary starts on a bare machine) to
databases, where `pg` needs libpq installed, `odbc` needs unixODBC plus a vendor
driver, and MySQL has no path except ODBC. **A GUI decision must not drag a
database layer in behind it.**

**And the licence cost this document did not state: Qt cannot be statically
linked.** §1's licence strategy establishes that the LGPL-3 set is usable and
which modules to avoid; what follows from LGPL-3 is that the toolkit must be
**dynamically linked** unless the relink obligation is met, so a gBASIC GUI
application always depends on an installed Qt. That is acceptable for a desktop
application — a desktop has a toolkit — and it is a genuine divergence from the
direction the Linux releases took, which was to link statically precisely so a
download starts on a machine with nothing installed. It belongs in the trade
rather than discovered during packaging.

---

## 0.1 THE DECLARED CONSUMER: gBASIC Studio (2026-10-17)

**Matthew: Studio is to be converted from GTK to this, once it is ready.** That
is a scope input and it changes what "ready" means, so it is recorded here
rather than discovered later.

Studio lives in its own project (`~/development/gbasic-studio`) and its surface
is **much larger than a `form`**: a source editor with syntax highlighting, a
data grid over a virtualised model, trees, and the `gi`/`gtk`/`sourceeditor`
libraries it consumes from this repo. So §2's `window`/`form` is the first
increment of this design and nowhere near the whole of what Studio needs.

**The editor was called the one piece with no Qt counterpart. MEASURED
2026-10-17, THAT WAS WRONG IN THE HELPFUL DIRECTION** — and the correction is
recorded rather than quietly edited, because this paragraph said the existing
language file "does not carry over" and it does.

GTK's answer is `GtkSourceView` plus a `.lang` XML file. The nearest Qt answer
is KDE's **KSyntaxHighlighting**, which *is* the extracted Kate highlighting
engine — and **this repo already ships `editors/kate/gbasic.xml`**, written for
Kate, whose own header says "Kate / KSyntaxHighlighting definition for gBASIC".
So the input format is already in hand. `tests/run_ksyntax.sh` over
`tests/qt/ksyntax_probe.cpp` keeps the measurement:

| | measured against libkf6syntaxhighlighting 6.24.0 |
|---|---|
| **Licence** | **MIT** for the project (`Files: *` in its own copyright). The GPL-2.0-only items are one bundled definition (nesC), a test fixture and the Debian packaging — so, unlike Qt itself, it is **statically linkable** and Apache-2.0 compatible |
| **Weight** | one 2 MB `.so`; runtime deps are libc, Qt6 Core/Gui/Network, libstdc++. No KDE desktop, no KParts. The `-data` package is **only translations** — definitions and themes are baked into the `.so` |
| **Our file** | **loads unchanged**: 20 formats over five lines, classifying Comment, Keyword, Number, String, Symbol |
| **Needs a widget?** | **No.** `AbstractHighlighter` is a pure callback interface, so highlighting is testable **headlessly and deterministically** — better than GtkSourceView, where the equivalent needs a widget |

**And a trap that does not announce itself:** `*.bas` is claimed by **two**
definitions at the same priority — the bundled FreeBASIC one and ours — and
lookup **by extension picks FreeBASIC**. There is no error; there is *plausible
but wrong* highlighting, FreeBASIC being a near neighbour (it classifies a
`Functions` kind where gBASIC classifies `Symbol`). **A host must ask by name**,
or `gbasic.xml` must declare a higher `priority`. The suite asserts both halves,
since "gBASIC was found" alone passes on an engine that happens to lack
FreeBASIC.

**That suite is live on the development machine**, the library having been
installed the same day. The figures above were *first* taken against an
extracted package through its `KSYNTAX_INCLUDE`/`KSYNTAX_LIB` override — which
exists so the live path could be RUN before being committed, a tier whose only
exercised branch is its skip being the green line this tree distrusts — and the
installed run then reported **the same numbers**, which is what makes that
method trustworthy rather than merely convenient. The override earned its place
immediately by exposing two defects in the suite's own parsing, one of which
reported the definition as *missing* on a correct run.

**The grid is the opposite case**: `datagrid` is already a reconciler over a
native row model, `QTableView`/`QTreeView` with a `QAbstractItemModel` is the
same architecture, and `run_datagrid.sh`'s 1M-row virtualisation proof is the
kind of claim that should transfer unchanged.

**Nothing here is blocked on Studio** — the increments stand on their own — and
the editor question, which was the one that looked like it could move a date, is
answered: the definition carries over, the engine is MIT and light, and the
highlighting is gateable without a display. What remains for Studio is widgets
and the data grid, which is ordinary work rather than an open question.

---

## 1. Why Qt, and what changed

Qt was rejected as a GUI backend on 2026-09-27 on measured grounds, and that
rejection **priced one route only**: hand-written per-widget bindings, the
PySide/shiboken model, which really is bindings forever.

**The by-name route it did not price works.** Measured — a 127-line C++ probe
drove real Qt 6.10.2 widgets entirely through `QMetaObject`: construction from a
five-line table (an unknown type refused), string/bool/int properties set and
read back by name (a misspelled property refused), a method invoked by name, a
signal connected by name to one generic `moc`-generated relay (a misspelled
signal refused), and children attached to a container and addressable in order.
Every check passed. The decisive figure: **`QPushButton` alone exposes 75
properties, 12 signals and 30 slots by name**, so the per-widget cost is one line
in a constructor table.

The old note is right that constructors and the value types are outside the
metaobject — `QWidget` subclasses are not `Q_INVOKABLE` — and that is exactly
and only what the table is for.

**This document is not a port of the GTK design.** `gtkui` describes a UI as a
record tree because GTK's `Box` *is* a widget holding children. Qt's layouts are
separate objects, which lets a declarative block nest the way BASIC wants to, and
Matthew's brief was explicit: the Qt library *"need not look like the GTK one…
in true BASIC form, make a GUI clear and easy to build and maintain."*

## 2. The shape: a `window` block

**The language already answered this once.** The `server` block (PLAT-WEB-5) is
how gBASIC declares a structured thing with handlers: exactly one reserved word,
directives recognised by **position** so `get` stays an ordinary identifier,
every mistake refused at load time with a located diagnostic that reaches
`--json-diagnostics`, and introspection that answers from the declaration with no
socket. A `window` block should be that same answer rather than a new idea.

```basic
window customer( title: "Customer", width: 480 )
    column( margin: 8, spacing: 8 )

        form to edit cust
            field id(    label: "Account", show: cust.id )
            field name(  label: "Name" )
            field email( label: "Email" )
        end form

        group "Credit"
            form to edit cust
                field limit( label: "Limit", type: {USD} )
                field terms( label: "Terms", type: {number} )
                check hold(  label: "On hold" )
            end form
        end group

        row
            stretch()
            button save(   text: "Save", default: true )
            button cancel( text: "Cancel" )
        end row

    end column

    on save.clicked
        customers.put(cust)
    end on
end window
```

**Two details above are forced by the grammar rather than chosen** — see §5.2,
where seven shapes were measured against bison. Options are **always
parenthesised**, including `column( margin: 8 )`, because an unparenthesised
field list has no closing token and the parser cannot tell where a value
expression ends. And `stretch()` carries its parentheses because a body-less
bare word would collide with `row`, a body-bearing one: **every head shape must
decide on its own whether a body follows**, which is the discipline that makes
the `server` block cost zero.

### 2.1 Nothing pollutes the namespace — and perhaps nothing is reserved

Only the **opener** could ever need reserving; everything inside (`row`,
`column`, `form`, `field`, `button`, `group`, `stretch`, `edit`, `show`) is
recognised in directive position and stays an ordinary identifier everywhere
else. That matters: measured, `form` already has **13** bare-identifier uses in
the tree and `grid` has 19 assignments plus 41 field uses, and
position-recognition makes all of them safe.

**The opener may not need reserving either.** `window customer( … )` is
`IDENT IDENT (` at statement start, and two juxtaposed identifiers are not legal
gBASIC today. The precedent is `with principal(p)`, where `principal` is
recognised by position and remains usable as a variable, field and parameter —
**zero new reserved words**, better than the `server` block managed. Measured
cost if it *were* reserved: `window` has 8 bare uses and 25 field/qualified uses.

**This has to be measured, not assumed.** This project rejected
`IDENT expression` as a statement form over **4 measured** bison conflicts, and
zero conflicts is the standard. The grammar work is to find out which shape costs
nothing.

### 2.2 Binding: `to edit` and `to show`

**Decided.** A form names its subject with an infinitive, and the verb carries
the direction:

| | |
|---|---|
| `form to show cust` | one-way — display only |
| `form to edit cust` | two-way — the record follows the widgets |

A field named `email` binds to `cust.email`, so **the binding is the name** and
there is nothing to look up. A widget overrides its form with the same word in
option position, and the override may name a different source:

```basic
field balance( label: "Balance", show: acct.balance )
```

**Why the infinitive**, four reasons: a declaration states *purpose*, and "form
edits cust" makes the form the actor, which is false — the person edits, through
it; the bare verb gives `show:`/`edit:` in option position, where the
third-person `shows:` read badly as a key; **`to` is already a hard keyword**
(measured — `to = 1` is a parse error naming it), so the construct is
`IDENT TO IDENT IDENT`, anchored on a token the lexer already emits, which
shrinks the conflict risk of three bare identifiers; and an infinitive never
conjugates, so it still reads when the subject or directive is plural
(`table to show invoices`).

Rejected on the way: `form bind: record` — `bind` is programmer jargon and
`record` is also a gBASIC *type* name, so it reads as binding to the idea of a
record.

**Binding needs no new runtime machinery.** Watchers already observe record
fields — measured, `watch(G.total)` fires on every write — so binding is a
watcher per field in each direction.

### 2.3 Types are gBASIC's own modifiers

**Decided.** A field's type is the modifier the language already uses to say what
a value is:

```basic
field limit( label: "Limit", type: {USD} )
field due(   label: "Due",   type: {date} )
```

Validation and parsing come free from the existing modifier, and it can say
*which currency*, which a `type: "money"` string cannot. **Measured cost:** a
bare `{USD}` is not an expression today (`t = {date}` is a parse error), so the
block head must accept a modifier token specially. The no-new-grammar alternative
is a string, consistent with what `type()` answers, at the price of currency.

### 2.4 Handlers

**Decided.** One shape: `on <name>.<signal>`.

```basic
on save.clicked
    customers.put(cust)
end on
```

A per-*kind* form (`on button.clicked( b )`, handed the widget that fired) was
considered and dropped: the only thing motivating it was a keypad of nineteen
buttons sharing a handler, and that is the data-driven case which belongs to the
builder (§4), not to a fixed declaration. A shared handler remains one line away
— each `on` body calls the same function.

### 2.5 Layout

**The important part is that most screens should not need one.** `form` is Qt's
`QFormLayout`: label/field rows, aligned, with no geometry decisions at all. That
covers the bulk of business UI. What remains is small:

| | |
|---|---|
| `column` / `row` | linear boxes; nest; `spacing:`, `margin:` |
| `stretch` | a spacer that absorbs the leftover space |
| `grow` | on a widget: this one takes the extra room (`grow: 2` to weight it) |
| `form to edit X` | label/field rows — the common case |
| `group "Title"` | a titled box (`QGroupBox`) |
| `tabs` / `tab "Name"` | `QTabWidget` |
| `cells` | genuinely tabular placement with spans; rarely needed |

**`stretch` earns its keep**: it is Qt's `addStretch()`, and it removes alignment
properties entirely — `stretch` followed by two buttons *is* "right-align the
buttons", and it reads as what it does. The same word centres (stretch on both
sides).

**What an author never writes is a coordinate.** No `Top`, `Left`, `Width`,
`Height` — which is what classic BASIC GUI building was, and why those screens
broke on a resize, a translation or a different font. Qt's layouts compute all of
it, so "easy to maintain" comes mostly from *absence*: there is nothing to keep
in sync.

Working rule: **more than two levels of nesting means `form`, `group` or `cells`
was wanted.** If that rule proves hard to follow on a real screen, the vocabulary
is wrong, and that is better found out on a real screen than in the abstract.

Named, not settled: `cells` is called that rather than `grid` because `grid` is
`stdlib/grid.bas`, the messy-worksheet library, and a reader should not have to
disambiguate. `column` is still overloaded — a layout column and a table's column
— and a `table` directive will want different words.

## 3. Licensing, and how a programmer knows

**Start inside the unencumbered set.** Measured across the 61 installed Qt 6.10.2
library packages (Debian's own `copyright` files):

| | count |
|---|---|
| LGPL-3 — fine in a closed commercial app, dynamically linked | **51** |
| GPL — would impose GPL on the application | **10** |

The GPL ten are not what a business application reaches for: `uitools`,
`designer`, `designercomponents`, `help` (the Designer/`.ui` family — not needed,
because the `window` block *is* the UI description); `quick3d`,
`quick3druntimerender`, `quick3dutils`, `shadertools` (3D); `virtualkeyboard`
(kiosk); `networkauth` (OAuth 2, which is HTTP rather than GUI and which gBASIC's
own `webclient`/`http`/`crypto` already cover).

**Everything this design needs is LGPL-3**: Core, Gui, Widgets — and so is
everything a business screen wants next: **Sql**, **PrintSupport** (printing
reports; people assume this one is encumbered), **Svg**, **Pdf**, **Network**,
**Xml**, **Concurrent**.

**The one GPL module people genuinely want is Charts, and gBASIC already answers
it.** `stdlib/chart.bas` is pure gBASIC producing deterministic SVG, and
**QtSvg is LGPL-3**, so a chart renders through an unencumbered path with no Qt
Charts at all. *(Not verified locally: Charts and DataVisualization are not
installed here, so that classification comes from Qt's published licensing and
should be confirmed before it is relied on.)*

### 3.1 The strategy: four layers, three of which already exist

1. **Do not wrap them.** The library exposes LGPL modules only, so an encumbered
   module is identifiable **by absence** — there is no directive for Charts, so it
   cannot be reached by accident.
2. **If one is ever wanted, one declared door** that names the consequence, and is
   **required with no default** — the rule `ldap` already follows, where
   `security` must be written so that a cleartext bind is "a word somebody
   typed", and `lending` follows for accrual basis. A licence consequence belongs
   in the source, not in someone's memory.
3. **Licence as a capability.** `gbasic --capabilities` already emits a derived
   table (`net`, `fs:read`, `db`) for scanners in other projects, asserted in both
   directions with a coverage tier. A `licence:gpl` label on an encumbered
   directive makes this visible through machinery that already exists and already
   cannot go stale.
4. **Derive it, do not maintain it.** `tools/library-needs.sh` already computes
   each library's dependencies from its own `load` lines, transitively, and the
   gate holds the reference table to it both ways. The same shape answers "which
   Qt modules does this program reach, and is any of them GPL?" — and the release
   build's third-party-notices generator already **refuses to publish** when a
   component has no notice, which is the precedent for refusing to package an
   application that touches a GPL module without declaring it.

### 3.2 When a commercial Qt licence is actually required

Only three cases, and they should be stated this plainly wherever an author can
see them:

- a **GPL-only module** is used (the ten above);
- Qt is **statically linked** — and `docs/dependency_linking_design.md` already
  establishes that static linking creates a redistribution obligation where
  dynamic linking does not;
- the product is **locked down** so a user cannot replace the Qt library, which
  is what LGPL's relinking freedom requires.

A dynamically linked, LGPL-only Qt inside a closed commercial application is
precisely the case LGPL exists to permit. **gBASIC itself stays Apache-2.0**
either way: it would link Qt dynamically and optionally behind `#if HAVE_QT`, the
same position it is in with GTK (LGPL-2.1) today, and the downloads carry no GUI
at all.

These are *Debian's* copyright files, describing what Debian ships. Upstream Qt's
licensing page is authoritative and per-file headers vary. As `LICENSING.md` puts
it, this is a description of intent and not legal advice.

## 4. The block is sugar over a builder

A declaration cannot contain a loop, so a `window` block cannot generate a row
per invoice. The `server` block has exactly this shape and the same answer: the
block is **sugar over a data API** (`web.routes` is the table; the block is a
nicer way to write it). So a Qt library wants both doors — the block for fixed
layouts, which is most dialogs, forms and toolbars; and a builder for
data-driven lists. The builder's surface is not designed here.

## 5. What must be measured before any code is written

**CORRECTED 2026-10-14, BEFORE ANY CODE WAS WRITTEN AGAINST IT.** This section
said the first experiment was "a Qt signal reaching a gBASIC function value,
unproven". That overstated the risk, and the evidence was already in the tree:
**`gi_signal_marshal` in `src/eval.c` does exactly this for GTK and has
shipped.** A native toolkit signal arrives, the handler is resolved
(`function_resolve`), the signal's own parameters are converted to gBASIC
`Value`s, `invoke_function` is called — and it handles the part this document had
not even listed, which is what happens when the handler **raises** at a native
boundary: the raise is reported, the outer error/stop state is restored, and the
loop that pumped the handler keeps running.

So re-entry into gBASIC from a native signal is a **solved problem with a
reusable pattern**, not an open question. Two things remain, and they are
narrower:

- **The handler is resolved BY NAME**, not held as a function value —
  `GiClosureData` stores a library and a name. That is fine for this design,
  whose `on save.clicked` handlers are named in the declaration. Carrying a
  function *value* instead is a separate, already-identified increment (the
  `bound()` work, which would also retire `datagrid`'s program-global registry).
- **Doing it from a C++ translation unit with `moc`** inside this build, which is
  the genuinely new part.

So the first experiment is the one that was ranked second:

1. ~~**Event-loop integration**~~ — **MEASURED AND ANSWERED 2026-10-17. See
   §5.1.** It was expected to be the hardest piece and it is settled: Qt owns
   the loop, and gBASIC's sources become `QSocketNotifier`s. The other shape
   fails, and fails only where nobody is watching.
2. ~~**The grammar cost** of `window NAME( … )`~~ — **MEASURED AND ANSWERED
   2026-10-17. See §5.2.** Zero is reachable, and getting there CHANGED TWO
   THINGS IN §2's example.
3. **A `QMetaType` marshaller** for method and signal arguments; the probe used
   compile-time `Q_ARG` and its relay carried no arguments.
4. **C++ and `moc` in a C11 build**, behind `#if HAVE_QT`, degrading to a clean
   runtime error when absent — the convention every optional module follows.

Testing has a free advantage: the installed Qt runtime carries
`libqoffscreen.so`, so `QT_QPA_PLATFORM=offscreen` runs real widgets with no
display — which is how the probe ran.

### 5.2 THE GRAMMAR, ANSWERED BY MEASUREMENT (2026-10-17)

Seven shapes were put through bison against the real `src/parser.y`, whose
baseline is **zero** conflicts. Reproduce any of them by copying the grammar,
adding the productions and running `bison -Wcounterexamples -d -o /tmp/x.c
copy.y`.

| | shape | conflicts |
|---|---|---|
| — | the baseline, `src/parser.y` untouched | **0** |
| A | a window body grammar of its own, sharing the server block's head | 5 S/R + 4 R/R |
| B | one head, the window's shapes added to `server_item` | 4 R/R, and one rule **useless in parser** |
| C | its own opener token, the body as §2 sketches it | 5 + 5 |
| D | C, with a container's body required to be non-empty | 7 + 1 |
| E | C, with the server block's own head discipline | 5 + 1 |
| **F** | **E, with options ALWAYS parenthesised** | **0** |
| G | F, but sharing the server's head instead of its own token | 2 R/R |
| H | F, plus `on save.clicked` — a DOTTED handler name, which the example uses and F had not covered | **0** |

**WHAT MAKES THE SERVER BLOCK FREE IS A DISCIPLINE, NOT LUCK**, and it is the
whole answer: every one of its item shapes is decided by **what follows the
first identifier** — a string list, a `STRING(`, or an `IDENT(` — so no two
shapes share a prefix while differing in **whether a body follows**. A window
must obey the same rule, and then its words need not be reserved and its
vocabulary can grow.

**TWO THINGS IN §2's EXAMPLE HAVE TO CHANGE, and neither is a preference.**

- **`column margin: 8` becomes `column( margin: 8 )`.** Every conflict in E was
  *inside a record field's value expression* — `• MINUS`, `• LPAREN`,
  `• LBRACKET` — and not in the body question at all: an unparenthesised field
  list has no closing token, so the parser cannot tell where a value ends.
  Parenthesising it costs two characters and matches the window head and the
  leaf form, so the rule becomes "options go in parens, everywhere".
- **`stretch` becomes `stretch()`.** A body-LESS bare word collides with `row`,
  a body-BEARING bare word, which is the discipline above stated in the
  negative. Two characters again.

**THE OPENER NEEDS ITS OWN TOKEN FROM THE LEXER, and that costs no reserved
word.** G measures the alternative: sharing the server block's `IDENT IDENT (`
head costs **2 reduce/reduce**, because after `IDENT IDENT ( … ) NEWLINE` the
parser cannot tell which body is coming — the counterexample names exactly that.
The lexer recognises `window` only in `window IDENT (` at statement start, which
is the technique that already won for keyword field names and for inline
modifiers, and `window` stays an ordinary identifier everywhere else (measured
in §2.1: 8 bare uses and 25 field or qualified ones).

**The shape that measures zero, in full:**

```
window_item
    : IDENT LPAREN RPAREN NEWLINE                                   /* stretch()            */
    | IDENT IDENT LPAREN record_field_list RPAREN NEWLINE           /* field id( … )        */
    | IDENT NEWLINE … END IDENT NEWLINE                             /* row                  */
    | IDENT LPAREN record_field_list RPAREN NEWLINE … END IDENT NEWLINE   /* column( … )    */
    | IDENT STRING NEWLINE … END IDENT NEWLINE                      /* group "Credit"       */
    | IDENT IDENT IDENT IDENT NEWLINE … END IDENT NEWLINE           /* form to edit cust    */
    | ON IDENT NEWLINE statement_list END ON NEWLINE                /* on save.clicked      */
    ;
```

**AND `type: {USD}` DOES NOT PARSE TODAY, which §5 item 2 flagged as the other
half of this question.** Measured: `r = { label: "Limit", type: {USD} }` is a
syntax error at the closing brace — a bare modifier is not a value, it needs
something to apply to (`{USD}0` answers `0.00`). So §2's example contained a
construct that could not be built, and the fix was priced rather than guessed:

| | cost |
|---|---|
| admit a bare `MODIFIER_PREFIX` as a primary expression | **4 S/R** |
| let the **lexer** decide, and give the bare form its own token | **0** |

Four is exactly the number that got `IDENT expression` rejected as a statement
form, and the cause is inherent: after `{USD}` the parser cannot tell a bare
modifier from one applied to what follows — `{USD} -5` is the discriminating
case. **The lexer can, and that is the technique which already won twice here**
(keyword field names, and the inline modifier itself): `{IDENT}` is already ONE
token, the lexer may look past the `}` as far as it likes, and what follows
settles it — a `,`, a `)` or a newline means the modifier is a VALUE, anything
else means it is applied to what comes next. Two distinct tokens, so the grammar
never has to choose.

**DECIDED 2026-10-17: the lexer token, and a DECLARATION-ONLY form.** Two quite
different changes hide behind the same syntax, and only the narrower one is
taken:

- **Not** a bare modifier as a *value* — assignable, passable, printable. That
  is a **28th value kind**, and this tree gates that: `type()`'s answers are
  checked against `docs/reference.md` in both directions, so every kind must
  decide what it renders as, what `encode` does with it and whether
  `{USD} = {USD}`. `docs/exact_number_design.md` rejected a second numeric type
  partly on that very argument — "a 28th kind and a promotion matrix to get
  wrong".
- **Yes** to the lexer emitting the distinct token, with the `window` block's
  option parser accepting it as a **type name recorded in the declaration**.
  No new value kind, nothing to print, nothing to compare. The precedent is in
  hand: the `server` block's head options are literals validated at load time,
  and §2 says the window head is the same.

**THE COST, STATED RATHER THAN DISCOVERED:** `type: {USD}` works inside a
`window` while `m = {USD}` still does not. That is the line the `server` block
already draws — **a declaration is not an expression context** — and the
refusal must say so in as many words rather than leaving an author to guess.

**It forecloses nothing.** The token exists either way, so admitting a bare
modifier as a primary is a one-line addition if a real case turns up — and the
case to watch for is a function that *takes* a type, which is a feature worth
choosing on its own merits rather than inheriting from a GUI option.

**WHY NOT A STRING**, which was the cheap alternative: a currency is `{USD}`
everywhere else in gBASIC, and a second spelling for one concept is the thing
this tree keeps finding drifts. Matthew's reasoning, and the one exception
argues the same way — currencies are never strings, but TYPE NAMES already are:
`consolidate` takes `kind: "money"` and `kind: "percent"`, and a cookbook recipe
teaches it. So `type: "USD"` would not have been a lone oddity, it would have
joined an existing wart. **`kind: {USD}` would improve `consolidate` too**, and
that is the natural follow-on if this form proves itself — noting that the
vocabulary does not map one-to-one, since `percent` is not a gBASIC modifier.

**This is a design measurement and not yet a gate.** When the grammar lands, the
zero becomes an assertion in its suite — the way `run_optparams`,
`run_for_each_index` and `run_principal` each assert bison still reports zero,
which is not decoration in a project that rejected `IDENT expression` as a
statement form over four measured conflicts.

### 5.1 THE LOOP, ANSWERED BY MEASUREMENT (2026-10-17)

`tests/run_qt_loop.sh` over `tests/qt/`, which **links no gBASIC code** — the
question is answerable in isolation, because gBASIC's loop is one `poll()` over
the webserver's descriptors, libcurl's descriptors and the root mailbox with
timers riding the timeout (`webserver_run_event_loop`), so every source is a
descriptor or a deadline and a stand-in needs only a real poll over a real
descriptor. Keeping the interpreter out means the answer cannot be confounded,
and the probe stays buildable as the interpreter changes.

There are exactly two shapes and **both were measured**, because the one that
fails does not fail in ordinary operation:

| | ordinary | with a modal dialog up |
|---|---|---|
| **B** — gBASIC owns, Qt pumped with `processEvents()` | 30 polls, 15 Qt ticks, worst gBASIC gap **52 ms** | 18 polls, worst gap **651 ms** over a 600 ms dialog |
| **A** — Qt owns, a gBASIC source is a `QSocketNotifier` | 4 of 4 source events, 15 Qt ticks | **4 of 4**, three of them *during* the dialog |

Same answer on the offscreen platform and on a real Wayland display.

**DIRECTION A IS THE ARCHITECTURE AND B IS RULED OUT BY MEASUREMENT.** Under B
both loops run at full fidelity right up until something opens a **nested** Qt
loop — and those are ordinary, not exotic: a modal dialog, `QMessageBox`,
`QMenu::exec()`, drag-and-drop, `QProgressDialog`. While one is up, gBASIC's
poll never runs: a webserver stops answering and every timer misses. A `form`
that validates its input will want a message box, so this is the common case
rather than a corner.

**A FINDING THIS SECTION DID NOT PREDICT, and it has to be a stated contract
rather than something each source rediscovers:** a `QSocketNotifier` is
**level-triggered**, so a handler that does not consume the bytes is re-entered
for as long as they sit there. The first version of the probe, which never read
its pipe, fired **545,066 times for one byte**. That is the same defect class
PLAT-HTTP already recorded — *"the first delivery loop re-reported undrained
bytes EVERY iteration ... a loop that never went idle"* — so **every gBASIC
source registered with Qt must be drained by its handler**, and the suite
asserts a ceiling on the activation count rather than merely that the source was
served.

**What this does NOT answer**, stated rather than implied:

- **Re-entry into the evaluator.** The probe proves a real Qt signal reaches a C
  callback through a `moc` translation unit, via one generic relay — the shape
  above. Calling `invoke_function` from there is still inferred from
  `gi_signal_marshal`, which ships it for GTK.
- **Windows.** `QSocketNotifier` there works on **sockets only**, not arbitrary
  handles, so direction A will need `QWinEventNotifier` for any source that is
  not a socket. Named now rather than discovered during the port.
- Threads, actors, and more than one window.

**What it costs the interpreter, which is a note and not a decision:** a program
with a `window` hands the thread to `QApplication::exec()`, so the post-`main`
loop becomes a *client* of Qt's rather than the owner. A program with no window
is untouched. Nothing in the language or in how programs are written changes —
**a GUI stays one optional part of a program**, which is the shape Matthew asked
for and the shape the measurement supports.

## 6. What to install

`qt6-base-dev` and `qt6-base-dev-tools` are already installed and supply Core,
Gui, Widgets, Network, Sql, PrintSupport, Xml, Concurrent and `moc`. Measured:
only one thing is missing.

```sh
sudo apt install qt6-svg-dev          # QtSvg + QtSvgWidgets (LGPL-3)
```

That is the whole list. `libqt6svg6` (the runtime), `qt6-wayland` and
`qt6-gtk-platformtheme` are already present, and no Qt SQL driver is wanted —
gBASIC has its own `sqlite`, `pg` and `odbc`.

**Do not install, because each is GPL and would put the encumbered headers within
reach of a careless `#include`:**

```
qt6-tools-dev              # QtUiTools / QUiLoader -- GPL-3
qt6-charts-dev             # use stdlib/chart.bas + QtSvg instead
qt6-datavis3d-dev
qt6-virtualkeyboard-dev
qt6-quick3d-dev
qt6-shadertools-dev
qt6-networkauth-dev        # OAuth: use gBASIC's own webclient/crypto
```

## 7. Open questions

**THREE WERE DECIDED 2026-10-17** and are struck below with the ruling.

### Decided: `field` keeps `type:`, and there is no directive per type

`field limit( label: "Limit", type: {USD} )`, not `money limit( … )`. One
directive for every field with the kind as an option, so a kind with no word of
its own still has somewhere to go, and the validation comes from gBASIC's own
modifiers — which is why a currency is sayable at all. The alternative reads
better in the common case and costs a new word per kind plus a second way to
say the same thing; rejected for that.

### Decided: the window may carry the subject, and a `form` shadows it

`window customer( … ) to edit cust` is allowed as a shorthand, because the
single-record dialog is the commonest window there is and repeating the subject
on its one form is noise.

**The ambiguity that shorthand creates is settled by a rule rather than left
open.** A window's subject is a **default**, not a binding:

- a `form` with **no** subject inherits the window's;
- a `form` **with** one shadows it, for that form only — the way a nested
  `with principal(p)` shadows and then restores;
- a **`table` always names its own** and never inherits. A table wants a list
  and a window's subject is one record, so inheriting it there would be a guess
  dressed as a shorthand, which is the one thing this design refuses everywhere
  else.
- A window with **no** subject and a bare `form` is refused at load time,
  naming both — the author wrote a form about nothing.

### Decided: layout keeps `column`, and a table's parts are `field`

`column` and `row` stay the layout pair, because layout appears in every window
and a table does not. A table's parts are therefore `field`, the same word a
form uses for the same idea — *one value of the subject* — which is a saving
rather than a workaround:

```basic
column margin: 8
    table orders to show lines
        field placed( label: "Date" )
        field total(  label: "Total", type: {USD} )
    end table
end column
```

### Decided: `type: {USD}` is a declaration-only form, from the lexer

See §5.2. `{USD}` is the spelling, because a currency is written that way
everywhere else in gBASIC and a second spelling for one concept drifts; the
lexer supplies the token at **zero** conflicts where the grammar would cost
four; and it is recorded as a **type name in the declaration** rather than
becoming a 28th value kind. `m = {USD}` still does not parse, which is the line
the `server` block's literal-only head already draws.

### Still open

- **`grow` as a bare option** is an option with no value, which is new grammar in
  the head; `grow: true` avoids it and reads slightly worse.

- **Qt Charts' licence** is unverified locally (§3).

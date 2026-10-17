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
    column margin: 8, spacing: 8

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
            stretch
            button save(   text: "Save", default: true )
            button cancel( text: "Cancel" )
        end row

    end column

    on save.clicked
        customers.put(cust)
    end on
end window
```

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

1. **Event-loop integration** — `QApplication::exec()` against gBASIC's own loop,
   which already serves `webserver`, `timer`, `http` and `inbox`. Expected to be
   the hardest piece; the probe says nothing about it, and `gi`'s answer does not
   transfer because GTK's loop is GLib's, which gBASIC's event sources already
   plug into.
2. **The grammar cost** of `window NAME( … )` and of a bare modifier in option
   position, in bison conflicts, against a standard of zero.
3. **A `QMetaType` marshaller** for method and signal arguments; the probe used
   compile-time `Q_ARG` and its relay carried no arguments.
4. **C++ and `moc` in a C11 build**, behind `#if HAVE_QT`, degrading to a clean
   runtime error when absent — the convention every optional module follows.

Testing has a free advantage: the installed Qt runtime carries
`libqoffscreen.so`, so `QT_QPA_PLATFORM=offscreen` runs real widgets with no
display — which is how the probe ran.

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

- **`column` is overloaded** — a layout column and a table's column. A `table`
  directive needs different words.
- **`grow` as a bare option** is an option with no value, which is new grammar in
  the head; `grow: true` avoids it and reads slightly worse.
- **Where the subject may be declared**: on the `form` (assumed here) or also on
  the `window` (`window customer( … ) to edit cust`), which reads well for a
  single-record dialog and badly for a screen showing one record and listing
  another.
- **Whether `field` should split by type** (`money limit( … )`) instead of
  carrying `type:`. Decided for `type:`, but a directive per type reads better
  and would make the modifier unnecessary.
- **Qt Charts' licence** is unverified locally (§3).

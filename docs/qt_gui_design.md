# A Qt GUI for gBASIC — design

**Status: largely shipped.** The `window` declaration (§0.2), the `qt` module
(§0.4), binding (§0.5), `table` (§0.6), menus and theming (§0.7),
`tabs`/`cells` (§0.8), the builder (§0.9), `text`/`memo` (§0.10) and
`qt.refresh` (§0.11), `choice` (§0.13), `source` (§0.14) and dialogs (§0.15) exist, with §0.12 a
whole application built on them, gated by
`tests/run_window.sh`, `tests/run_qt_backend.sh`,
`tests/run_qt_builder.sh`, `tests/run_qt_examples.sh` and
`tests/run_qt_source.sh` and `tests/run_qt_dialog.sh`. Still proposal: `stdlib/qt.bas`, `show:`/`edit:` in
option position, binding into a container nested inside a form, more than one
window, and threads. **This line has been wrong three times by being left
alone** — twice in one day claiming the declaration could not render, and then
again listing `tabs`/`cells`/`table` and the builder as absent after all four
had shipped — so the rule is that a section says its own state and this line
says which sections are which.

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
highlighting is gateable without a display. What remained for Studio was widgets
and the data grid, which was ordinary work rather than an open question — and
**as of 2026-10-19 both are built**: the widgets through §0.5–§0.26, and the data
grid through §0.25, which is the one thing the conversion could not express
otherwise (`datagrid.create_virtual` is GTK-only). The gaps still named are
completion (§0.24, measured and not built) and a line-number gutter, which Qt
has no widget for.

---

## 0.2 WHAT IS BUILT (2026-10-17)

**The declarative half, with no Qt.** `tests/run_window.sh`, 27 checks, headless
on every platform — because a declaration is **data**, and everything decidable
about it is decidable without the thing it eventually drives. That is the
`web.routes` bet: a route table proven with no socket.

- **Two lexer tokens and nothing reserved.** The opener fires only on
  `window IDENT (` (measured: 153 uses of the word in the tree, **zero** of that
  shape), and a bare `{IDENT}` followed by `,`, `)` or a newline is the
  type-naming token — so `x {date}= s` and `{USD}19.99` are untouched.
- **Zero bison conflicts**, now a standing assertion rather than a design note.
- **Fifteen load-time refusals** in the `server` block's own pass and sink, so
  they reach `--json-diagnostics` for nothing. The sharpest:
  `on cancel.clicked` with no `cancel` can never fire, and nothing at run time
  would ever say so.
- **The declaration binds its name** to an introspectable record — nested
  containers, each form's subject and direction, labels, parts and their
  options, and each handler as `{ target, signal, handler }` where `handler` is
  a **function value** — so a backend reads it with no new builtin, exactly as
  `web.routes` reads a server record, and can *call* `on save.clicked` without
  a registry or a name-mangling scheme of its own. **Hoisted**, so the block may
  sit below the program that names it.
- A **non-literal option is visible by name and not by value**: `show: cust.id`
  names a place to read when something renders, in the frame where the subject
  lives, so there is no value to report at load time. The head's options are all
  literals and never unknown.

**What did not exist at that point was anything that RENDERS it** — a missing
backend rather than a missing declaration. That is §0.4.

### 0.3 REQUIRED OF THE FIRST BACKEND COMMIT: "no display" is a sentence

**Decided 2026-10-17 and already measured**, after the question "will gBASIC
still run on a text-only computer?" — it will, completely: the lean release
builds with `GTK_AVAILABLE=0`, this repository's own test binary carries
`HAVE_GTK=0`, and a program with no `window` block is untouched by any of this.
Only *opening* a window needs a display, and a window block still **parses,
validates and introspects** with no Qt at all, which is why the declaration
shipped first.

But the hazard is worse than "it will not work", and `tests/run_qt_loop.sh`
measures all of it:

- **With no display, Qt calls `qFatal` and ABORTS** — SIGABRT, exit 134, core
  dumped, with *"no Qt platform plugin could be initialized"* on stderr.
  **`qFatal` cannot be caught**, so by the time the process knows, it is gone.
  And a crash takes block-buffered stdout with it, so a program printing its
  progress prints **nothing** and the failure reads as line 1. This is exactly
  the defect GTK produced and `run_gi.sh` records: a widget constructed before
  `gtk_init`, exit 139.
- **The environment cannot be read instead**, which was the obvious guess and is
  wrong: measured, Qt found Wayland through `XDG_RUNTIME_DIR` with
  `WAYLAND_DISPLAY` unset. Its own detection is richer than any reimplementation
  of it, and would drift as Qt changes.

**So the first backend commit must refuse before constructing anything**, by
asking Qt **in a forked child** — a process we can afford to lose — and reading
a status. The remedy is proven, not planned: the probe answers **no** where Qt
aborts, **yes** on a real display, **yes** under `QT_QPA_PLATFORM=offscreen`,
and the parent **exits cleanly in every case**, so a diagnostic can still be
printed. All four are asserted, because any one alone is satisfied by a probe
that always says the same thing.

Two consequences worth keeping:

- `QT_QPA_PLATFORM=offscreen` stays something **a user** may legitimately set,
  so a GUI program can render on a headless server.
- The refusal names what is missing — a display — rather than reporting a Qt
  plugin failure, which names the one thing the author cannot fix.

---

## 0.4 THE BACKEND (2026-10-17): the `qt` module

**Built, and `tests/run_qt_backend.sh` holds it.** Four things in it come from
measurement rather than from this document's arguments, and are not re-decided
in the code:

| | |
|---|---|
| **Qt owns the loop** | §5.1 direction A. gBASIC's event-loop body is now a function of its own (`webserver_loop_iterate`) with **two callers sharing it** — the ordinary `while`, and a Qt timer while a window is open. One body, because a second copy is a second thing that can disagree, and a disagreement here means a source serviced under one arrangement and starved under the other |
| **Every source is drained** | the contract §5.1 had to state after a probe that did not read its pipe fired **545,066 times for one byte**. The tick cannot have that defect *by construction* — it runs the real loop body, which reads — and the rule is written down because the next source added may not be a timer |
| **No display is a sentence** | §0.3, asked of Qt in a forked child. `gb_qt_begin` refuses as well as the caller, because it is the one place a `QApplication` can be constructed |
| **Construction is a table; everything else is by name** | §1. Eight words need a table because a `QWidget` constructor is not `Q_INVOKABLE`; properties, methods and signals go through `QMetaObject` by name. Eight table entries against the 75 properties `QPushButton` alone exposes **is** the argument for the by-name route |

**The address of a window is its declaration, not a handle.** `qt.open(customer)`
takes the record the block bound and so does `qt.read(customer, "name")` —
exactly as `web.routes(app)` takes a server record. So there is **no 28th value
kind**, which would owe answers for rendering, `encode`, comparison and copying;
PLAT-EQ's lesson is that a new kind silently opted into the numeric coercion and
was found *by adding one*.

The surface: `qt.can_display()`, `qt.open(win)`, `qt.read(win, name)`,
`qt.write(win, name, value)`, `qt.invoke(win, name, method)`, `qt.run()`,
`qt.stop()`. `qt.write` renders through **the one renderer** (PLAT-RENDER), so a
date, money or a duration reaches a widget in the words the language itself uses
rather than this module growing a second opinion. `qt.invoke` is the method-by-
name door the 2026-10-14 probe measured, and it is also what makes the signal
path testable **with no human in front of the screen**: invoking `click` on a
button emits the real `clicked` signal through the real connection.

**`qt.can_display()` ANSWERS where every other verb refuses**, including on a
build with no Qt at all — the shape `has_builtin` already has, and for the same
reason: a program asking whether it may open a window is *degrading gracefully*,
and "this build has no Qt" is one of the answers to that question rather than an
error.

**No `load`**, which is the one place this departs from the optional-module
convention and is deliberate twice over: a `window` block implies no import, so
neither does rendering one; and `load qt` on a build without Qt would raise at
the **import**, taking the whole program with it — the shrink
`run_discovery.sh`'s own note argues against. The refusal belongs at the call.

### What is declared and not yet acted on, said out loud

- **`show:` / `edit:` in OPTION position** — the per-widget override that may
  name a different source (`field balance( show: acct.balance )`) — is still
  recorded by name and not by value (§0.2), so it cannot be acted on. What
  shipped is the **form-level** binding, which needs no value from the
  declaration because the binding is the *name*.
- **The data-driven builder** (§4) is unstarted. (`table` shipped 2026-10-18 —
  §0.6; menus and theming the same day — §0.7; `tabs` and `cells` — §0.8.)
- **A nested container inside a form does not bind.** A `form` holding a `row`
  holding fields binds only the form's direct children, deliberately: binding
  through the layout would make the record's shape depend on how the form is
  arranged. The remedy, if a real case wants it, is already spelled — the inner
  container says whose subject it serves.

## 0.5 BINDING (2026-10-17): `form to show` and `form to edit`

Shipped, with `type:` and a property reader. Four things are worth recording
because three of them correct this document.

**§2.2's mechanism was half right.** It says binding is "a watcher per field in
each direction". The first direction needs no watcher at all — a form populates
once, at open. The second **cannot** be a watcher: a widget's signal fires
inside `QApplication::exec()` beneath `qt.run()`, so the frame current at signal
time is whichever one called `qt.run` and not reliably the one that called
`qt.open`. There is no frame to write back into, and carrying one would mean
capturing scope across the Qt queue.

What replaces it is smaller than a watcher and is the language's own rule: **a
record is a value**, so `qt.subject(win, "cust")` reads the widgets *at the
moment it is asked* and folds them into the record the form captured at open.
Nothing is tracked between the two, so no change signal can be missed, and the
caller's own record is untouched — the fold returns a new one.

**The fold has to keep each field's kind**, which the obvious implementation
gets wrong. A widget holds text, so folding back blindly turns `{ id: 42 }` into
`{ id: "42" }` — a record whose shape silently changed and which still prints
the same digits. The kind comes from `type:` where the author declared one, from
the captured field otherwise, and from a check box being a check box. This is
why `type: {USD}` was done here rather than later: it is the declaration's own
answer to that question, and applying it is one call into the machinery an
assignment clause already uses.

**A one-way form takes no typing**, and not for neatness. The alternative is a
silent discard — somebody types into a display form, nothing complains, and
`qt.subject` never reports it, which is the class warning 2107 exists for one
level up. Which property says so is a fact about the widget: a `QLineEdit` gets
`readOnly`, which still allows selecting and copying (what a reader of a display
form wants), while a `QCheckBox` has no such property and is disabled instead.

**The subject must exist.** `form to edit cust` has named a subject since the
declaration shipped and nothing required one to be there, so a form bound to
nothing opened quietly and could never populate — the "silently does nothing"
class the closed vocabulary exists to prevent one level up. It is read from the
frame that calls `qt.open`, which is what §0.2 means by "a place to read in the
frame where the subject lives", and a missing one is refused with a message
naming the form, the direction and where the subject is looked for.

### A property reads back, which is what made the by-name route assertable

`qt.read(win, part, property)` was built with binding because the tier arguing
the by-name property route works could not see half of its own claim: it
asserted that `text: "Go", enabled: false` was **accepted** and nothing more, so
a backend that took every option and applied none passed it. The kind comes from
the `QVariant` rather than from the caller, because every non-empty string is
true in gBASIC — a boolean answered as the text `"false"` would read *backwards*
in `if qt.read(f, "readOnly")`.

### What binding does NOT catch, and why the refusal was not written

A **misspelled part name inside a form** is not caught, and the consequence is
worse than "no diagnostic". A field whose name the record lacks is left empty
rather than refused, because for `to edit` the form's job is to *produce* the
record: a field absent at open is the ordinary state of a form being filled in,
and the fold adds it. A form filling in a **new** record has every field absent
until somebody types, so the refusal that would catch the typo is the one that
breaks that case.

But a misspelled part leaves the intended field holding its **captured** value
and adds a second field named after the typo — so typing into that part changes
nothing the author meant, and nothing says so. That is the same silent-discard
class `to show`'s read-only exists to prevent, arriving by another door, and it
is **pinned by a check** in `tests/qt_binding_test.bas` rather than left here as
prose, so anyone closing it is told what they are changing. What it wants is an
opt-in from the author — a form declaring its subject's fields fixed — not a
rule.

**A correction worth keeping**, because it was written into the code and the
docs before being measured: the quiet behaviour was justified as "the same
answer `r.missing` gives on a record". It is not — `r.missing` **raises**
`unknown record field`.

What it does match is a **projection**: `rows.amount` answers `unknown` for a
row that lacks the field, because raising would make projection unusable on real
business data, which legitimately has holes. A form populating a dozen fields is
that situation, not the scalar one. So the behaviour is right and the analogy
was simply pointed at the wrong half of the language.

**The same false sentence was already in the tree**, in `eval.c`'s own
projection comment and in `docs/reference.md`'s projection bullet — "matching
what `r.amont` already does on a record" — where it had sat since projection
shipped. Both corrected, and `run_projection.sh` now asserts the difference as a
pair, so it cannot drift back.

And the same measurement is why this increment's gather checks are guarded with
`has`: a fold that visited only the first of two forms leaves a field absent,
and reading it raises — killing the fixture and hiding the mismatch it had
already computed.

A `type:` that refuses what was typed reports **the modifier's own sentence**,
which is the cause, and that sentence **does not name the part** — so with
several `type:` fields in one form the author is told `money text is not a
number` and must work out which. Naming it means adding context to a raise
already in flight, which nothing in this tree does and which touches
`raise_in_flight`; getting that wrong makes an error *vanish*, the one failure
the error model exists to prevent. So it waits for a helper with its own tier.

### What the suite cannot assert, and why

**Valgrind.** Qt's platform plugins, font configuration and `QApplication`
statics are not freed by design, so a leak tier here would be a suppression file
*describing Qt* rather than a check on us — and `run_valgrind_policy.sh`'s whole
argument is that an access-only tier must not become "valgrind is off". The
gBASIC-side allocations this increment adds (the handler shells and their minted
names) **are** covered, by `run_window.sh`'s existing valgrind tier, which
parses every window fixture without opening one.

## 0.6 `table` (2026-10-18)

Shipped: `table orders to show lines`, §7's own spelling, with `qt.selected`,
`qt.select`, `qt.cell` and `qt.heading`.

**The spelling was priced before it was built**, which is what let the design's
wording be used rather than a nameless `table to show lines`. A *named*
container carrying a subject is new grammar and costs **zero conflicts** — `TO`
is a hard keyword, so after `IDENT IDENT` one token of lookahead separates it
from the named leaf's `LPAREN`. Measured before and after, against this
project's standing assertion of zero.

**A table is named and a form is not**, refused both ways. The difference is
real rather than a spelling: a form's parts are addressed by their own names,
where a table shows many rows, so `qt.read(win, "placed")` could not say which.
The table itself is what a handler and `qt.selected` address — which also meant
widening the load-time **name registry**, since only leaves registered a name
and `on orders.<signal>` on a perfectly real table was refused as naming
nothing in the window.

**A show table needs a third answer for display-only.** A `QTableWidget` has no
`readOnly`, and *disabling* it would stop a person choosing a row — which is
reading, not editing. `editTriggers` says exactly what is meant.

### The oracle, and the reversal that produced it

Two perturbations — "every column reads the first field" and "a cell ignores the
column name and reads column 0" — produced **identical output**, which is the
signature of checks that cannot separate two defects. Population and `qt.cell`
both map a column to a name, and every check read cells through the same by-name
route that wrote them; permute both the same way and every value sits under the
wrong heading with nothing to notice. *Agreement is not correctness.*

The caption is the independent oracle, because it comes from `label:` while a
cell's data comes from `name` — two different sources in the declaration. So
`qt.heading` exists, which **reverses a decision taken an hour earlier**: a
heading reader was rejected on the grounds that a program which set a caption
already knows it. That was the wrong test to apply.

It is only an oracle because it can be asked **by position as well as by name**.
Asked by name it goes through the same resolution `qt.cell` uses, so a permuted
resolution answers both consistently; asked by position it reads the header in
declaration order. Requiring the two to agree is what pins the mapping — and
with only the by-name form, the two perturbations above still gave identical
output.

**It caught a real defect on its first run.** A leaf's `label:` lives in
`options`, where the top-level `label` field carries the `group "Credit"` string
form instead, so the header extraction read the wrong place and silently fell
back to the field's *name*: the table was captioned `placed`/`who`/`total`
rather than `Date`/`Customer`/`Total`. A plausible header row, and every other
check passed.

### Known costs, stated

`QTableWidget` materialises every row, so this is not the path for the
million-row case `run_datagrid.sh` proves with a native row model — that is a
model behind `QTableView`, not a bigger version of this. Click-to-sort is off
deliberately: sorting the view while the subject keeps its own order would make
one row index mean two different rows. A row cannot be added or removed from the
view, so the fold writes over the capture row for row.

## 0.7 Menus and theming (2026-10-18)

Both asked for by name. Neither was in this document before.

### Menus need no grammar at all

**Measured before a line was written**, and it shaped the whole increment:
`menu "File"` is the production `group "Credit"` already uses, `item open(
label: "..." )` is the named leaf, `separator()` is the `stretch()` form, and a
submenu is the same production recursively. Every one already *parsed* — only
the vocabulary refused them — so bison reports the same zero conflicts before
and after. `table` needed new grammar for its name; menus needed none.

**An item is a `QAction`, not a widget**, which is the one structural fact here.
The parts registry now holds `QObject`, so an item is addressed by name for
`on open.triggered`, for `qt.read(win, "open", "enabled")` and for
`shortcut:`/`enabled:`/`checkable:` at construction — all through the same
by-name door every widget option uses. A `QAction`'s properties are listed
nowhere in this backend and needed no plumbing.

**A menu bar belongs to the window**, not to a layout, so a `menu` sits at the
top level or inside another menu and nowhere else. Refused rather than placed
somewhere surprising: Qt would put a `QMenuBar` wherever the layout said, so a
`menu` inside a `row` renders as a bar in the middle of the screen. The mirror
is refused at *load* time too — `item` and `separator` may not appear where
things *are* laid out, which would otherwise reach `gb_qt_part`, find no widget
for the word, and fail at run time for whoever has Qt in their build. This is
the first directive whose placement depends on its **surroundings**, so the
validator now carries the parent's word.

**A menu's caption is not a name** — the opposite of a table, and asserted so
the two cannot drift together: a table is named because it shows many rows,
where a menu's *items* are what a handler addresses.

### Two oracles, both found by perturbation

A by-name registry is flat and menus are a tree, so **the registry hid the
structure from its own tests**. An item keeps its name wherever its menu ends
up, so:

- hanging a submenu off the **bar** instead of off its parent produced **zero
  mismatches**;
- adding an item to the **wrong menu** was invisible for the same reason;
- and a **missing separator** was too.

`qt.menus(win)` answers the bar's captions and `qt.menus(win, n)` the entries of
one menu, both as lists — so they pin the count, the order, and that `Recent` is
an entry *of* File rather than a menu beside it.

**A separator answers `unknown`, not `""`.** Skipping the separator branch let
one *fall through* and be added as an action with empty text, which a
caption-based check could not tell from a real line — so Qt is asked
`isSeparator()`, and a separator *has* no caption where `item x( label: "" )`
has an empty one. The absent/empty distinction this language makes everywhere
else, load-bearing here.

### Theming: most of it already worked

**Measured first:** `styleSheet` is a `Q_PROPERTY` of `QWidget`, so
`button go( styleSheet: "QPushButton { ... }" )` has been reachable since the
by-name route shipped — that route paying off in a way this document did not
anticipate, which is the argument for a door over a table of bindings. What it
cannot reach is **one sheet for the whole application**, and that is `qt.theme`.

**It needs no display**, and that decided the implementation: a program will set
a theme before opening anything, so the sheet is *remembered* and applied when
the `QApplication` is constructed. Refusing until a window existed would force
the first window to appear unthemed and then change.

**It takes text, not a filename.** `qt.theme(read({file}"dark.qss"))` composes
out of what the language already has, where a file argument would put file
resolution, missing-file behaviour and a search path inside a GUI module with no
business owning them. Qt resolves the cascade between the app sheet and a
widget's own, so there is no precedence rule here to invent.

## 0.8 `tabs` and `cells` (2026-10-18)

The last two words in §2.5's layout table. Both cost **no grammar**; one
production was added for a different reason, and its shape was decided by
measurement.

### A container may carry a name or options, not both

`tabs settings` with a body costs **zero** conflicts. The same thing *with*
options — `tabs settings( movable: true )` — costs **four reduce/reduce**, and
bison reports the rule useless: after `IDENT IDENT ( ... ) NEWLINE` the parser
cannot tell a named *leaf* from the head of a container whose body is still to
come. That is the wall §5.2 records for the server block's head.

So the limit is stated rather than designed around, and **`qt.set` is what makes
it affordable** — a named container's properties are set at run time.

### `qt.set` was a gap, not a convenience

`qt.read(win, part, property)` shipped without a counterpart, and the *only*
other way to set a property is the declaration, which is written before anything
has happened. So enabling a button once a form validates, greying a menu item,
or making a field read-only on a condition had **no workaround at all**. It
refuses an unknown property by name, because the two halves of one route must
agree about what a property name is.

### `cells` needed no new word

A grid *is* rows of cells, so `row` is reused — refined inside `cells` to mean a
row whose columns **align** with the rows above, where a `row` elsewhere is a box
that does not. A new word per axis, or a position option on every part, was
unnecessary. `across:` and `down:` are the spans, which is the half a box layout
cannot do and the reason the directive exists.

### The oracle was built before the fixture, and immediately paid for itself

Third increment running, the same hole: every part reads back **by name**, so a
widget in the wrong cell or with the wrong span is invisible to every other
check — and placement is the entire point of `cells`. `qt.at(win, part)` answers
`{ row, column, across, down }` by asking **the layout**, walking up from the
widget rather than from any record of where we put it, because a remembered
position would agree with a placement that was wrong.

It found, within a minute, that **a field's label took no cell** — which is how
the four-increment `label:` defect was discovered (see below). The geometry is
now also the only way the *field* half of that fix is assertable, since a
field's label becomes a nameless `QLabel`: its **column** says it is there.

### `label:` had never reached any widget

Measured: `check hold( label: "On hold" )` rendered with **no text**, and every
`field` in a form got `addRow(QString(), w)` — an empty label. So every form row
in this backend was unlabelled from the first backend commit through four
increments.

`qt_apply_options` skips `label` as "consumed at construction", and construction
read the **top-level** `label` — which only a labelled *container* sets
(`group "Credit"`, `tab "General"`). A leaf's `label:` is an option, read by
nobody.

**And I had already seen it.** The same defect in a sibling path produced table
captions reading `placed`/`who`/`total` an hour earlier; I fixed that lookup and
did not sweep, which is exactly PLAT-NUL's standing lesson — *a defect found in
one place is evidence about every place that reads the same thing.* The menu
path, written later, happened to read `options` correctly, which hid the
pattern. No test caught it because every fixture read a value or some other
property, and an unlabelled widget raises nothing.

---

## 0.9 The builder (2026-10-18): §4, and the door that was already open

**Shipped.** `tests/run_qt_builder.sh`, `tests/qt_check_test.bas`,
`tests/qt_builder_test.bas`.

§4 called for a second door: the block for fixed layouts, and a builder for
data-driven lists, because a declaration cannot contain a loop and so cannot
generate a row per invoice. **Measured before anything was designed, that door
was already open and nobody knew.** `qt.open` reads the record a declaration
binds and asks only that `kind` is `"window"`, so this works:

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

It opens, renders, reads back through `qt.read`, places through `qt.at`, and
fires handlers — identically to a declared window. The surface *is* the record,
which is the `web.routes` shape §4 itself named. **No verb was needed.** That is
the third time in this design that measuring a gap before filling it changed
what got built: menus needed no grammar (§0.7), a named container with options
cost four conflicts (§0.8), and this needed no builder.

### What it lacked was every load-time check

A declared window is validated at load time (§0.2) and a built one was validated
not at all. **That is not a missing nicety — it is the defect `web.configure`
was built to close**, where the record could already be written directly and
that path validated nothing, so `app.options.workesr = 4` was accepted in
silence. Measured on the binary first:

| built record | what happened |
|---|---|
| a part with **no name** | opened quietly, unaddressable forever |
| **two parts sharing a name** | opened quietly, the second shadowing the first |
| `word: "feild"` | refused by the *backend* for having no widget wired — never *did you mean 'field'?* |
| `item` in a `column` | the right refusal with the wrong sentence |
| a handler naming nothing | refused for carrying no function — never *there is nothing called `nobody` in this window* |

The last is the refusal load-time validation exists for, so a builder without it
is a builder whose mistakes surface as a window that looks fine.

### One checker, two adapters — and no second copy

The rules are ~350 lines. A record walker would be a **second copy**, which is
the drift this tree puts tripwires against — and which this very vocabulary had
already suffered: it was written out three times before `win_nearest` was derived
from the tables, and the newest word was missing from the third.

So they moved to `src/window_check.c`, expressed against an **accessor model**:
`src/frontend.c` adapts the AST, `src/modules/qt.c` adapts a record, and neither
holds a rule. It is the arrangement `would` and `execute` share in `automation`,
for the reason stated there — *if the two could disagree, a check would describe
a different window from the one that opens.* A source tripwire asserts there is
no second vocabulary table and no second near-miss finder.

`qt.check(win)` is the verb, and it **needs no Qt and no display**: it is a check
on a *declaration*, which is the `web.routes` property that let the whole
declarative surface ship before any Qt existed. So it sits outside the
`#if HAVE_QT` guard — this project's own convention read the other way, that code
which is *not* optional must stay out of those guards.

**`qt.open` runs it too.** A validator nobody is obliged to call is necessary and
not sufficient: `rules.validate` refused a malformed stack all along while
`rules.evaluate` still answered `ok: true` for a string, because nothing obliged
a caller to validate first.

### The record's field names are closed as well

That is a question about the **representation** rather than about windows, so it
lives in the adapter and not in the rules: `{ kind: "part", word: "field",
nmae: "x" }` is refused *by name*, and an item with no `kind:` is told the three
it may have. Reported with a **path** (`column[0] > field a`) since a record has
no line — the nearest honest equivalent of the line a declaration's refusal
sends a reader to.

### Two defects of mine, both found by running it

**The child wrapper was a ring of sixteen**, under a comment claiming the checker
"holds at most a parent and a child at any moment". That was **false**, and the
sixteen was the giveaway: at the seventeenth *sibling* the ring recycled the
*parent's* slot while the walk was still iterating it. It segfaulted at nineteen
generated parts — which the fixture had, §2.4's rejected keypad being nineteen
buttons — and a crash takes block-buffered stdout with it, so the run printed
everything up to the tier and nothing after, reading exactly like a hang. The
same shape §0.3 describes for a display-less Qt abort, arriving from my own code.
One slot per **depth** now.

**And that exposed the second.** A declared window's nesting is bounded by its
source; a **built** one is bounded by nothing, and the walk recurses. Measured:
with the cap lifted it survives 5,000 and segfaults at 10,000, while building the
same 10,000-deep record and *not* checking it exits 0 — the control that makes
that a fact about this walk rather than about gBASIC, where a nested value around
22,000 deep is separately known to kill the interpreter at teardown. Capped at
**64**, some 80x short of the crash, with the margin recorded rather than the
cap merely asserted.

### Parity is the load-bearing tier, and its limit is demonstrated

"The builder works" is satisfied by a builder that renders something
**plausible**, which is this backend's whole failure class. So the same window is
written **both ways** and every observable must agree — properties, labels, and
the grid geometry `qt.at` reads back from the layout itself.

**Parity alone is not enough and the fixture says so.** Perturbing
`gb_qt_span` to ignore spans breaks *both* halves equally, so all six parity
checks stay green and only the companion assertion — *the span really is 2* —
catches it. That is agreement-is-not-correctness (PLAT-NUMFMT) one level up, and
it is why the companion check is there.

Beside it, **agreement** puts every one of the 23 `window` declarations in the
tree through the *record* adapter, lifted mechanically rather than listed: a
declaration passed the AST adapter at load time, so a refusal there means the two
have drifted, and it is the only tier that can see that.

### Three of this suite's own controls caught my scanner, not the tree

Each is a trap already written down somewhere in `tests/`. The `one_copy` scanner
first reported `src/modules/qt.c` for the sentence `never "did you mean
'field'?"` **inside the comment explaining why the finder lives elsewhere** — the
*argument* for a rule read as a breach of it, which is `run_libcurl_floor.sh`'s
`smtp.c` trap. Stripping comments with a sed **range** then deleted to end of
file whenever `/*` and `*/` sat on one line, so the scanner read nothing and
would have passed on anything; caught by the control asserting it keeps the code.
And `code_only | grep -q` fails the whole pipeline **when it matches**, grep
closing the pipe and awk dying of SIGPIPE — fifth instance in this tree, recorded
in `run_rules.sh`, which is where the helper was copied from.

### What is deliberately not built

**`stdlib/qt.bas` with builder helpers.** The record is buildable as it stands
and `qt.check` makes it safe to build; helpers would be sugar over sugar, and
whether they earn their place is a question a real consumer answers. Studio
(§0.1) is that consumer.

---

## 0.10 `text` and `memo` (2026-10-18): the toolkit could not display a sentence

**Shipped.** `tests/qt_text_test.bas`, a tier in `tests/run_qt_backend.sh`.

**Found by setting out to write one example per aspect**, which is the argument
for writing them. The leaf vocabulary constructed **`QLineEdit`,
`QPushButton`, `QCheckBox` and no more** — so a heading, an instruction, a room
description or a log had nowhere to go at all. Nothing in the tree said so,
because every fixture and every example happened to need only an entry, a box
and a button.

**The nearest substitute was measured and refused.** A read-only frameless
`QLineEdit` looks like a label — `readOnly: true, frame: false` both take — and
**cannot wrap**, and is still a text entry: focusable, single-line, selectable
as an input. A paragraph was not expressible, so this is a gap rather than a
convenience.

`text` → `QLabel`, **wrapping by default**, because a label that does not wrap
silently truncates the thing it exists to show. `memo` → `QPlainTextEdit`
rather than `QTextEdit`, because the subject is *text*: rich text would make
`qt.read` answer markup for something a program put in as a string, and nothing
in this vocabulary asks for formatting.

**Both cost no grammar** — they ride the named-leaf production, as `item` and
`separator` did.

### A `QPlainTextEdit` has no `text` property

It is `plainText`, and the reader's fallthrough asked for `text` — which answers
an *invalid* `QVariant`, which this backend reports as **an absent part**. So
`qt.read` on a perfectly real `memo` would have said the window has no such
thing, and `qt.write` would have been refused for an unknown property on the one
thing a memo is for. Both halves are named rather than left to the fallthrough.

### Three hardcoded copies of the vocabulary, found by adding to it

The *fourth* copy was the **"unknown directive" sentence**, which listed every
word in prose — so adding a word meant editing a list nothing checked, and a
reader who typed something nothing is close to would have been handed a list
missing the newest words. It is built from the tables now.

The *fifth* was in `run_window.sh`'s own typo tripwire — the loop that exists to
catch exactly this drift was itself a hardcoded list, **and `tab` was already
missing from it**, so the newest words could be added to the code while the
tripwire went on passing and testing fewer of them each time. A tripwire that
can drift is not one. It now asks the **binary** for its vocabulary by parsing
that derived sentence, which is asking the code; all 17 words are swept, and
the probe asserts it read a plausible list rather than looping over nothing.

### Two of my own assumptions, both wrong, both found by running it

A `QLineEdit` given three lines **keeps all three** — Qt strips newlines from
*typed* input, not from `setText` — so the difference between a `field` and a
`memo` is what a person *sees*, not what is stored, and a text comparison cannot
tell them apart. The discriminator is the class, asked through the same by-name
door: only a memo carries `plainText`.

And `qt.read(win, part, "readOnly")` on a `QLabel` **raises** rather than
answering `unknown`, which is this backend's documented contract for an absent
property. The refusal is the assertion.

### Still missing, and judged rather than listed

A **`choice`** (`QComboBox`) has no substitute — a dropdown in a form row, which
is what "this exit leads to which area" needs. A **`list`** probably does not
need a word: a single-column `table` already answers a picker, and `qt.selected`
and `qt.cell` already address it. Both are decided against what a real program
needs rather than against Qt's catalogue.

---

## 0.11 `qt.refresh` (2026-10-18): a table did not follow its list

**Shipped.** `tests/qt_refresh_test.bas`, a tier in `tests/run_qt_backend.sh`.

**Found by building a whole small application** — BAG in a window (§0.12) — and
it was a **gap rather than a limit**: a `table to show` captured its subject
when the window opened and **never followed it**, so appending to the list left
`rowCount` where it was and a program had no way to say otherwise. Measured
before the verb was written: `rowCount` 2, append, `rowCount` 2, and row 2
refused as absent.

For a game that is awkward. For anything **list-driven** — orders arriving, a
search result, a filtered view — it is fatal, and Studio's file tree is exactly
that shape, so this would have blocked the declared consumer.

**It is the symmetric half of `qt.subject`,** which is what settled the design.
§2.2's watcher pair could not do write-back, so a record is read from the
widgets **at the moment asked**. This is the same principle the other way: the
list is read from the frame at the moment asked, with nothing tracked in
between, so no change can be missed.

**The population is one piece of code.** It was extracted from `qt_bind_table`
rather than copied, because a second copy is a second thing that can disagree —
and a disagreement here means a table that renders one way at open and another
way afterwards. The arrangement `would`/`execute` share, and the one
`webserver_loop_iterate` follows for its two callers.

**The captured record moves with it.** A later `qt.subject` folds the widgets
back over what was captured at open, so leaving that stale would fold a
refreshed table into the **old** list — a plausible record assembled from two
different moments. Asserted: after refreshing a one-element capture to two
elements, the fold answers two.

### A form could not be re-shown either, and that was the same gap

**Found one increment later, writing BAG's editor** (§0.13's own motivation):
choosing a row has to *show that area*, and a form populates at open with
nothing to re-run it. So `qt.refresh` takes **a table's name or a form's
subject**.

**A form has no name** — §0.6 refused one both ways, a form's parts being
addressed by their own names where a table shows many rows — so the subject it
declares is the only thing that addresses it. One verb takes either rather than
growing a second.

**An ambiguity is refused, not resolved.** A table called `orders` beside a form
`to edit orders` is a window where the word means two things, and picking one
silently is how a program refreshes something other than what the author named.
Counted *before* anything is written, so a bad name is refused rather than
half-acted-on.

**And a subject spread across several forms is visited in all of them**, because
`qt.subject` already folds across all of them — §0.5's own fixture splits `cust`
across a column and a group — so the two directions would otherwise disagree
about what a subject reaches. The tier asserts the second form, which is what
separates visiting *the subject* from visiting *the first form that mentions it*.

**The populate half was extracted rather than copied**, as the table's was, for
the same reason: a form that renders one way at open and another afterwards is
the defect, not the saving.

**Headings are deliberately not refreshed.** A column's caption is set when the
widget is built and only the declaration names it, so there is nothing to
re-read. Only the rows follow.

The tier's load-bearing check is the **shrink**: a refresh that appended rather
than rewrote passes every grow-and-change check, and only requiring row 1 to be
*gone* separates them — a stale row below the new ones holds a perfectly
ordinary value under a correct heading.

---

## 0.12 BAG in a window (2026-10-18): the proof, and where state lives

**Shipped.** `examples/bag/gui.bas`, a tier in `tests/run_bag_smoke.sh`.

BAG — the Basic Adventure Generator — was the first real program written in the
earliest gBASIC. The game it writes is a **read–eval–print loop**: `print` the
room, `input(">")` a command, repeat. That is the one shape a GUI cannot have,
because **Qt owns the loop**. So this is the same game with its control flow
turned inside out, and it is a proof rather than a demo: if a whole small
application can be built on this surface, the surface works.

It plays itself in `check` mode through the same handlers a mouse would fire —
take the key, walk north, find the gate shut, open it, walk through — and the
gate is the part worth watching: **North is disabled in the Garden until the key
is used**, which is the world changing a widget through `qt.set`.

### Where the state lives, which is the thing to take from it

gBASIC has no closures, and a handler fires from inside Qt's loop beneath
`qt.run()`, so the frame it sees is the one that called `qt.run` — `main`.
**Measured, and the two halves differ:**

| | |
|---|---|
| a handler reading `main`'s locals | **works** |
| a handler rebinding a scalar | **silently makes a function-local** — warning 2104 fires |
| a handler writing `g.field` on a record | **reaches `main`'s record** |
| a *function* called from a handler reading `g` | **works**, and it can write widgets |

So the whole game state is **one record**, and every handler mutates its
fields. That is not a trick: it is warning 2104's own advice — *"mutate a field
of a shared record instead"* — and it is the pattern any stateful gBASIC GUI
wants, Studio included. A helper called from a handler must `return nothing`,
or warning 2101 fires for the discarded result.

### What it exercises in one screen

`text` for the room name and its description, `memo` for the transcript, a
`table` for what is here (with `qt.refresh` so it follows the room), `field`
plus `button` for typed commands, six buttons whose `enabled` is set per room,
a `menu` bar with a separator, `group`/`row`/`column` layout, and `qt.theme`.

### And the editor, which decided three additions rather than using them

`examples/bag/editor.bas` is BAG's other half: the game *plays* a world and the
editor *describes* one. The text version asks *"which area does north lead to?"*
and wants an id the author has to remember; a window answers with a dropdown.

**Writing it is what produced `memo` (§0.10), `choice` (§0.13) and both halves
of `qt.refresh` (§0.11).** None was reasoned to — each was the thing that
stopped the program being writable, and each was found by trying:

| | without it |
|---|---|
| `memo` | a description is a one-line entry |
| `choice` | you type area ids |
| `qt.refresh` on a table | the area list never grows |
| `qt.refresh` on a form | the editor shows the first area for ever |

The tier asserts the **round trip**, not the rendering: point an exit at another
area *by name*, keep it, and require the record to hold the right **id** and the
list to show the new **name**. A dropdown holding the right words while storing
the wrong id is the plausible wrong answer here, and only reading the record
back catches it.

### Still ahead of it

Teaching BAG to **emit** a `window` block, so the generated game is a GUI, is a
separate exercise: a program writing a declaration. And the editor covers areas;
items and triggers are the same shapes again.

---

## 0.13 `choice` (2026-10-18): the last widget with no substitute

**Shipped.** `tests/qt_choice_test.bas`, a tier in `tests/run_qt_backend.sh`.

**Decided by a real program rather than Qt's catalogue.** BAG's area editor
gives every area six exits and **each one names another area**; an item names its
location. A `field` can only answer that by typing an id, and a `table` is a grid
rather than a dropdown — so there was no substitute at all.

**And the same measurement said not to add a `list`.** A one-column `table`
already *is* a picker, `qt.selected` already addresses it, and since §0.11 it
follows a changing list. A `list` would be a second way to say that, which is
the shape this tree puts tripwires against.

### Two doors, because one program needs both

BAG's triggers fire on a **fixed** set of directions and its exits name a set
that is **not** fixed. So:

| | |
|---|---|
| `choice facing( options: ["north", "south"] )` | declared, knowable at load time, carried **by value** in the record |
| `qt.options(win, part, list)` | from data, filled after the window opens |

Both go through **one filler**, because a choice filled one way and refilled the
other must hold the same thing — the rule `qt_populate_table` follows for a
table's rows.

**A declared list needed the literal rule looked at again.** A window's options
are recorded by value only when literal, and the stated reason is what a
declaration can *know*: `show: cust.id` names a place to read later. An array
whose elements are all literals **is** knowable at load time, so it falls on the
knowable side of that very reason — it was excluded by the implementation rather
than by the argument. `window_literal_value` adds it and leaves the shared
server path untouched **by construction** rather than by a suite saying so. An
array with a non-literal inside it is still `nothing` and not a partial list:
half a list is worse than none, since the dropdown would offer some of the
options and look entirely ordinary.

### `qt.write` refuses a choice that is not there

Qt's own `setCurrentText` **silently does nothing** on an uneditable box, so a
program setting an absent value would read back the old one with nothing said —
a plausible wrong answer, which is this backend's whole failure class.

### Refilling resets the selection

Deliberately: a list that has changed may not contain what was chosen, and an
index into a list that has moved is how a form comes to report a value nobody
picked. A program that wants the old choice back asks for it with `qt.write`,
which refuses one the new list does not hold.

### Two gaps of mine, found by folding one back

`qt_bindable_word` admitted only `field` and `check`. So a `choice` **and a
`memo`** rendered perfectly and **bound not at all** — the widget plainly showed
a value and `qt.subject` answered an empty string. Adding words to the
vocabulary without adding them there leaves them half-wired: a window that looks
right and a record that never fills.

The rule is now stated rather than enumerated: **a part binds if it holds a
value a person can change.** `field`, `check`, `memo`, `choice` do; `text` does
not, so folding it would be meaningless and populating it alone would make
`to edit` and `to show` mean different things depending on the word. A form
wanting wrapped prose from its subject uses a `memo` with `readOnly: true` —
which binds *and* wraps, where the read-only `field` §0.10 ruled out could do
neither.

**And a `choice` whose list is not filled yet does not raise at open.** The
ordering makes it ordinary: a declared list is there at construction, but a
`qt.options` list can only arrive *after* the window is open, which is after the
binding pass. So the subject's value has nothing to match and the selection is
left alone — the same ruling §0.5 made one line up for a field the record does
not have. Every other word still raises, so a real refusal is not swallowed.

### And a double free the file already warned about

`value_array` **adopts** its buffer. The note on `window_items_value`, six
functions along in the same file, says so in as many words — *"freeing it here
is a double free, which is exactly what the first version did"* — and I did it
again anyway. The run said `free(): double free detected in tcache 2` and
aborted. Fourth instance this session of a correct paragraph nearby that the
author did not read.

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

**Binding needs no new runtime machinery** — which held, by a different
mechanism than this paragraph predicted. It said watchers: "`watch(G.total)`
fires on every write, so binding is a watcher per field in each direction".
**Measured when built, the second direction cannot work that way at all**: a
widget's signal fires inside `QApplication::exec()` beneath `qt.run()`, so there
is no caller's frame to write back into. A record being a *value* is what makes
it work instead — `qt.subject` reads the widgets on demand. See §0.5.

### 2.3 Types are gBASIC's own modifiers

**Decided.** A field's type is the modifier the language already uses to say what
a value is:

```basic
field limit( label: "Limit", type: {USD} )
field due(   label: "Due",   type: {date} )
```

Validation and parsing come free from the existing modifier, and it can say
*which currency*, which a `type: "money"` string cannot. **Shipped 2026-10-17**
(§0.5): the declaration records the modifier's *name*, so applying it is one
call into the machinery an assignment clause already uses — and it is what keeps
a folded-back value money rather than text. **Measured cost:** a
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

**Shipped 2026-10-18 — see §0.9, and the premise of this section was measured
and found already satisfied.**

A declaration cannot contain a loop, so a `window` block cannot generate a row
per invoice. The `server` block has exactly this shape and the same answer: the
block is **sugar over a data API** (`web.routes` is the table; the block is a
nicer way to write it). So a Qt library wants both doors — the block for fixed
layouts, which is most dialogs, forms and toolbars; and a builder for
data-driven lists.

This section said "the builder's surface is not designed here". **It did not
need designing: the surface is the record, and the door was already open.**
`qt.open` reads the record a declaration binds and asks only that `kind` is
`"window"`, so a record a program *builds* opens, renders, reads back and fires
handlers exactly as a declared one — measured before anything was written. What
was missing was not a verb but the validation a declaration gets, which is
§0.9's subject.

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
joined an existing wart. **`kind: {USD}` would improve `consolidate` too** — but
**not under this decision, and the first draft of this paragraph was wrong to
imply otherwise.** `consolidate`'s spec is an ordinary record a PROGRAM BUILDS
AT RUNTIME, not a declaration, so a declaration-only form can never reach it;
that improvement needs the bare modifier to be a real value. The benefit was
cited for the option that was NOT taken.

**SO WHY IS THIS STILL THE RIGHT FIRST STEP?** Not because a modifier value is
wrong — the objection above is weaker than it reads, since
`exact_number_design.md`'s "promotion matrix" argument came from a genuinely
harder case, two NUMERIC kinds meeting in arithmetic, and a modifier value is
**inert**: `type()` answers one more word, `print` shows `{USD}`, `encode`
refuses as it already does for a function, a regex and a file, and comparison is
by name. Four short answers, not a hazard.

The reason is that **this is a strict subset which the fuller form widens
without changing a single program.** `type: {USD}` parses identically either
way, so going from declaration-only to a real value later adds a production and
breaks nothing.

**AND THE CASE THAT WOULD DECIDE THE FULLER FORM ALREADY EXISTS**, which §5.2
first described as something to watch for: `frame`, `consolidate` and `dbframe`
all name column types as STRINGS today. That is a wart with a queue behind it
rather than a hypothetical, and typed columns — not a GUI option — are what it
should be chosen on. Note the vocabulary does not map one-to-one: `percent` is
not a gBASIC modifier.

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

- ~~**Re-entry into the evaluator.**~~ **MEASURED 2026-10-17 by the backend**
  (§0.4): a real `clicked` signal reaches a gBASIC handler through one generic
  relay, and the handler reads the live widget. `run_qt_backend.sh` asserts it
  on its own and with a control that it fired when the button was clicked and
  **not** when the window opened — which would be the registration-time firing
  `watch` has, and here would mean every button acting before anybody pressed
  one. What is still carried over from `gi_signal_marshal` rather than
  re-derived is the RAISE CONTAINMENT, which is the same code shape one toolkit
  over.
- **Windows.** `QSocketNotifier` there works on **sockets only**, not arbitrary
  handles, so direction A will need `QWinEventNotifier` for any source that is
  not a socket. Named now rather than discovered during the port.
- Threads and actors.
- ~~More than one window.~~ **Measured 2026-10-18 and it already worked** —
  two open at once, parts scoped per window, a dialog parenting to whichever is
  named. What was actually missing was the LIFECYCLE; see §0.17.

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

## 0.14 `source` (2026-10-18): a code editor, and a trap it nearly shipped with

**§0.1 called Studio's editor the one piece with no Qt counterpart, and the
2026-10-17 measurement corrected that in the helpful direction** — the nearest
answer is KDE's KSyntaxHighlighting, which *is* the extracted Kate engine, and
`editors/kate/gbasic.xml` was written for Kate, so the input format was already
in hand. This is the increment that uses it.

### Why it is a word and not a property

Everything else a widget carries goes through `QMetaObject` by name, and that
is the whole argument for how small this backend is: eight construction words
against the 75 properties `QPushButton` alone exposes. **A highlighter is not a
property.** It is an object you attach to a document, so `indexOfProperty` has
nothing to find however it is spelled — and the same is true of the
`QCompleter` Studio wants next, which is why the shape here is *attach a thing,
then ask what was attached* rather than a new option.

### A `source` is a `memo` with four differences

Each is a fact about the widget's **setup** rather than a `Q_PROPERTY`, which
is why none of them was reachable before:

| | and why |
|---|---|
| it does not wrap | a wrapped line of code makes a line number stop matching the file — the **opposite** default from `text`, where wrapping is what stops a paragraph truncating |
| a four-column tab | the width every gBASIC file in this tree is written at; Qt's default is eight |
| the system's monospaced font | **asked for**, never named: a hardcoded family is how a port acquires a fallback nobody chose |
| a highlighter | the part above |

They are asserted as differences **against a `memo` in the same window**, since
every value check also passes on a backend that built a plain memo and called
it a source.

### The trap, and why the probe came first

The probe that designed this reported the `gBASIC` definition as **found with
no search path set at all**, which read as the path plumbing being unnecessary. It
was a copy hand-installed into `~/.local/share/org.kde.syntax-highlighting` in
June, for Kate. A fresh machine finds nothing.

And what a fresh machine then *shows* is the sharp part: **an absent definition
still applies the theme's normal-text format to every line** — measured at 3
format ranges against 8 — so the editor is not blank, not plain and not
obviously wrong. It is an ordinary editor with ordinary text in it. That is the
libcurl floor, mawk's missing interval expressions and LDAP's GnuTLS
diagnostic for the fourth time: **invisible exactly where it was written.**

So the definition comes from a path the *build* knows, mirroring the stdlib
loader one line up in the Makefile: `GBASIC_SYNTAX_PATH` for the source tree,
a baked-in `$(DATADIR)/syntax-highlighting` otherwise, and `make` derives the
engine's `<dir>/syntax/` layout from the one definition the repo ships. A tier
runs with `XDG_DATA_HOME` moved aside and requires the file to be **ours**.

**It caught a second instance within the hour.** `run_qt_examples.sh` ran
`editor.bas` with no path set and reported clean stderr — on this machine only;
pointed at an empty `XDG_DATA_HOME` it fails. The suite exports the path now.

### A download is not an install

The path above was baked in at build time, and **that alone would have shipped
a download whose editor highlighted nothing.**
`tools/build-release-tarball.sh` says the tree is *relocatable by
construction* and runs where it is unpacked — which works because the stdlib is
resolved **relative to the binary**, not at the absolute path compiled in. So a
tarball would have looked in `/usr/local/share/gbasic/syntax-highlighting`, a
directory that is not there: **the file in the archive, unreachable by the
binary beside it**, which is the shape reported from outside this project three
times and the reason `run_library_tiers.sh` exists.

Measured by installing to a scratch `DESTDIR` and **moving** the tree somewhere
no build knew about, with no `GBASIC_SYNTAX_PATH` and an empty
`XDG_DATA_HOME`: `gBASIC` from its own path with the resolver, `applied=none`
without it. The walk is **shared** with the stdlib's rather than copied —
`gb_exe_relative_share(subdir)` — because two representations of one layout
drift. `run_qt_source.sh`'s `relocated` tier keeps it.

### "It highlights" is two claims in two places

Neither is sufficient alone, and the second is the one gBASIC cannot make:

- **Which definition was applied** — `qt.highlighting(win, part)` answers
  `{ language, file }`, or `unknown` when none is. It reports the **file** as
  well as the name because a definition the user installed in their own home
  *shadows* ours and reports the identical name (measured, 7 ranges against 8).
  That precedence is left alone deliberately — it is the rule `load` already
  follows for a library beside the program — but being unable to **see** it
  would not be acceptable.
- **Whether the text is really coloured** — gBASIC cannot read a
  `QTextLayout`'s format ranges, so `tests/qt/source_probe.cpp` does it in C++,
  with the control in the same process: 15 ranges against 5, **while both
  format every one of the five lines**. That last number is what makes a
  presence check vacuous.

### Lookup is by name, and that is recorded rather than preferred

`*.bas` is claimed by **FreeBASIC at the same priority as ours**, extension
lookup picks FreeBASIC with no error, and FreeBASIC is a near enough neighbour
that the result looks right. `editors/README.md` argues deliberately against
shipping a priority, since a higher one wins for *every* `.bas` file on the
machine including somebody's FreeBASIC. So by-name is the only route left. The
fixture asks for FreeBASIC and requires FreeBASIC, beside gBASIC giving gBASIC
— the pair without which every other check passes on extension lookup.

### An absent definition warns rather than raising

Warning **2115**. The editor is built, editable and useful without
highlighting, so stopping the program is a worse answer than saying so — the
position `odbc.connect` takes on a driver needing `ClientCharset`, and for the
identical reason, that the failure is *silent*. The message names the **part**
because it cannot name a position: a window record holds no line, so it is
reported at `qt.open`. Its control is that a run which found its definition is
silent.

`theme:` is **declared**, not always inferred, because `qt.theme` sets a
stylesheet and a stylesheet does not move Qt's palette — so a dark application
with an inferred theme gets light highlighting on a dark background, which is
less readable than none. `theme:` without `language:` is refused, since the
theme is applied with the definition and alone would do nothing.

### Valgrind objected, and it is PCRE2's JIT

Isolated the way `run_odbc.sh` isolates a driver defect — reproduced with **no
gBASIC in the stack** — then narrowed by measurement:

| | objections |
|---|---|
| a `QPlainTextEdit` with no highlighter | 0 |
| gBASIC's pre-existing `memo` fixture | 0 |
| a highlighted document | 3 |
| the same, `QT_ENABLE_REGEXP_JIT=0` | 0 |

The JIT compiles to **anonymous** executable memory, which is why valgrind can
name no object (`obj:*`, `???`) and its own glibc suppressions do not match,
and its matcher over-reads the subject in 16-byte steps by design. The tier
**turns the JIT off rather than suppressing**, on `tests/odbc.supp`'s own
argument: the only suppression available would be "anything in an object we
cannot name", which would also hide uninitialised data *we* passed in. The
premise is asserted — the fixture must produce the same checks either way — so
a setting that changed the program would fail rather than pass quietly.

### Still open, and named so the absence is not read as an oversight

- **A line-number gutter.** Qt has no widget for one; it is a painted side
  area, and real work. Studio needs it.
- **Completion.** The *data* already exists — `source_outline`, `src/lsp/` —
  and the popup is a `QCompleter`, another object attached to a widget, so it
  lands exactly where the highlighter did and for the same reason.
- **Which copy wins** when the user has their own definition is measured but
  not *chosen*: the engine decides, and `qt.highlighting` reports it.

## 0.15 Dialogs (2026-10-18): a question, and how far it reaches

**A program could not ask one.** No message box, no confirm — and the irony is
sharp: §5.1 chose direction A *because* modal dialogs starve the other
arrangement (a 651 ms gap over a 600 ms dialog, which is a webserver answering
nothing), and nothing could open one.

### Measured before designing, which decided the shape

A C++ probe in gBASIC's exact arrangement — a `QTimer` inside Qt's loop, which
is what `gb_qt_run` uses — shows the tick firing **during** a modal
`QMessageBox` (3 of 3) and **able to press its button**. So a program can
answer its own dialog, and a suite needs no human. A `QFileDialog` behaves the
same once `DontUseNativeDialog` asks for Qt's own rather than the desktop's.

### And the claim is narrower than §5.1 reads

§5.1 is about **Qt's** loop surviving the nested one. It does. What cannot
happen is **gBASIC being serviced while a watcher body is on the stack**:

| dialog raised | gBASIC's own sources |
|---|---|
| from `main`, outside any watcher | keep running — a `timer` fires and can answer the box |
| **inside a watcher body** | **suspended** until it is answered |

**The second is the normal case**, and it is wider than it looks: a `timer`
body, a webserver handler, an actor mailbox watcher, an `http` event watcher
and a button clicked from any of them all run inside one. So a modal dialog is
modal to the whole *program*. Measured both ways through the interpreter, and
`run_qt_dialog.sh` asserts it with a real `server` block on an OS-assigned
port: the route answers with no dialog up and does **not** answer during one,
with a third check that the dialog was still open so what was measured was a
stall and not a crash. **If that tier ever passes, the limitation is gone** and
this section, the reference and the cookbook all overstate it — which is
`run_limitations.sh`'s negative-control shape.

#### That row said "from a handler", and named the wrong cause (corrected 2026-10-18)

The measurement was right and the mechanism was not, which the chooser's own
work turned up: **there is no handler at all in
`tests/qt_dialog_pump_fixture.bas`** and the stall is identical. Two
independent guards hold it, **each sufficient alone**, and it took removing
them one at a time to tell:

| | what it does |
|---|---|
| **Qt's** | gBASIC's sources ride **one** reference-counted `QTimer`, and Qt will not re-deliver a timer whose handler is on the stack — measured in C++ at **0** pump ticks against **3** for a fresh timer during the same box |
| **gBASIC's** | `watcher_drain` returns at once while a watcher body is on the stack, so the loop polls, finds work and declines to dispatch it |

**Removing either changes nothing.** Given a timer of its own the pump reached
gBASIC's loop **397 times** during one dialog and the stall did not move; with
gBASIC's guard alone removed the pump still never ticked. With both off it
dissolves — the next tick arrives and answers the box, which is what proves
the pair rather than either.

**So the fix is not a timer change**, and that is the half worth recording,
because a timer change is what anybody would reach for first. It needs watcher
dispatch to become **re-entrant**, and that guard is deliberate and is the
right call: a watcher body re-entered mid-statement is the hazard, not the
remedy. A language decision, not this backend's to take.

That is defensible for a desktop application and surprising for one that is
also a server. The design answer for a program that must keep serving is a
*non-blocking* ask — a dialog that returns at once and delivers its answer to
a handler — and that is deliberately **not** built here: it is a second API
for the same thing, and nothing has needed it yet.

### Two defects found by running it

- **The pump lived inside `gb_qt_run`**, so a dialog opened *before* the
  program handed over the thread had no pump at all: `exec()` opened a nested
  loop with nothing servicing gBASIC, and the first `qt.confirm` written
  outside `qt.run()` **hung until a 30-second timeout**. It is reference-counted
  now and a dialog arms it too, so the promise holds wherever the box was
  opened from rather than only in the arrangement the pump was written for.
- **The no-display refusal named the wrong cause.** It said *"window `app` is
  not open"* — true, and the window is shut *because* there is nowhere to put
  it. It names the display now, with the control that an unopened window still
  gets the other sentence. Fifth instance of that class here.

### Decisions

`confirm` answers a **boolean**, not a button code: two buttons is the whole of
what it offers. **No is the default**, because a question answered by pressing
Return without reading should not be the destructive one. `qt.answer` **refuses
when nothing is open** rather than being a silent no-op — which would leave a
program waiting for somebody who is not there, and a hang is not a failure.
A `QMessageBox` is heap-allocated behind a `QPointer`, because a
stack-allocated `QObject` *given a parent* is a double free and the parent is
wanted so Qt centres the box on it.

### Still open

- ~~A file chooser.~~ **Built 2026-10-18 — see §0.16.**
- ~~A window shown modally,~~ which is what anything richer than two buttons
  wants. **Built 2026-10-19 — see §0.18.** The clause that used to follow this
  — "and which would also answer *more than one window*" — was **wrong**, and
  §0.17 measured it: more than one window already worked, so a modal window
  answered only its own question.
- **A dialog over another** is not refused — the backend keeps a stack — but
  nothing exercises it, and a claim nobody has run is not a claim.

## 0.16 A file chooser (2026-10-18): which file, and who may answer

**Three verbs, not one with a mode**, because they ask three genuinely
different questions — *which existing file?*, *what shall I call it?*, *which
folder?* — which is also how Qt splits them, and which lets each one refuse the
options that mean nothing to it instead of ignoring them:

```basic
path = qt.choose_file(app, { title: "Open", folder: here,
                             filter: ["gBASIC programs (*.bas *.gb)", "All files (*)"] })
if is_unknown(path) then return          ' they cancelled
text = read({file}path)
```

`qt.choose_save` names a file to write, which need not exist; `qt.choose_folder`
picks a directory. `qt.open_file` was **rejected as a name**: it reads as
though it opens the file, and `f = qt.open_file(app)` looks like it answers
contents or a handle. These verbs *choose*; the program does the reading.

### Four things were measured first, and three of them changed the design

**1. The desktop's own chooser cannot be driven.** With the native dialog — the
XDG portal, which is what Qt reaches for on Wayland — `selectFile` does
**nothing** (the selection is empty before `exec`) and accepting does nothing
either: 41 pump ticks, `rc` 0, no file. So a program could not preselect a path
and `qt.answer` would be a **silent no-op**, which is the failure class this
whole backend is tested against. `DontUseNativeDialog` is therefore set
unconditionally and is **not** offered as an option.

**2. And offscreen cannot tell the two apart.** With no portal to reach, Qt
falls back to its own widget dialog — so the native path measures as *drivable*
under `QT_QPA_PLATFORM=offscreen`, the one platform every suite here runs on.
Comfortable test data hiding the difference is a shape this tree has a name
for, so the forcing is held by a **source tripwire** with its own vacuity
control rather than by a tier that would pass either way.

**3. `selectFile` is ignored while the dialog is visible** and its name field
has focus — deliberately, so Qt never overwrites what somebody is typing. The
obvious implementation of `qt.answer(path)` therefore left the selection
**empty** and the accept silently refused. What works is what a person does:
**type into the name field, then press the button** — the rule `qt.answer`
already followed for a message box, and it pays again, because **Qt disables
that button** for a path the mode cannot take. So the refusal is the chooser's
own rule rather than a reimplementation of it, and the warning box a direct
`accept()` would have raised never appears. That box was a *hang* rather than a
failure: it opens a second nested loop from inside the first one's pump tick.

**4. A rejected `QFileDialog` still reports its selection**, so reading the
file list without checking the result code answers a path for a chooser the
person **cancelled**. Not an error — a wrong answer, and the worst kind.

### Decisions

**The answer is text**, and that was decided rather than defaulted. Both
`{file}` and `{dir}` accept a path that is not there (measured), so a typed
answer *was* available — but three sibling verbs would then answer two kinds,
and the rule this backend already follows is that a kind comes from the
**declaration** and not from Qt (§0.5: a widget holds text, so folding back
blindly turns `{ id: 42 }` into `{ id: "42" }`). A chooser has no declaration
to read a kind from, and what Qt holds is a path. So text, and
`{file}qt.choose_file(app)` where a typed value is wanted — one inline
modifier, which is the spelling 0.5.0 added for exactly this.

**Cancelling answers `unknown`, never `""`.** The empty string is the plausible
wrong answer: `if path = "" then` happens to work while `read({file}path)` does
not, so an author who forgot to check gets a puzzle instead of a raise.
`unknown` is what `qt.selected` already answers for "nothing is chosen", and
`if path then` on it **raises**, which is loud.

**`filter:` takes text or a list**, joined with Qt's own `;;` by the backend,
because two semicolons meaning "or" is a Qt-ism nobody should have to know. A
single string passes through, so somebody who does know it loses nothing.

**The option set is closed and refused by name**, with a near-miss hint — four
options is a vocabulary that *can* be closed, unlike the 75 properties a
`QPushButton` exposes. An ignored misspelling is a chooser that opens in the
wrong folder with the wrong filter and says nothing. `filter:` and `name:` are
refused on `qt.choose_folder`, where Qt would ignore both.

**`qt.answer` grew a third argument shape.** A path answers a chooser; `false`
is a **cancel**, which is what a timeout or a program giving up actually wants
to say; `true` is **refused**, because it would have to mean "accept *what?*"
and guessing which file somebody meant is the one thing this verb must not do.
A path handed to a message box is refused the other way round. The two kinds
are told apart in the backend so each refusal names the dialog that is actually
open.

### `qt.dialog()`, and why the oracle came first

`title:` and `filter:` would otherwise be **accepted and unobservable** — the
hole a leaf's `label:` sat in for four increments while every check passed
(§0.8). `qt.dialog()` answers the innermost open dialog as a record (`kind`,
`title`, and `text` for a message box or `folder`/`filter` for a chooser), or
`unknown` when none is open — the `principal()` distinction, since "nothing is
open" and "a dialog carrying no fields" are different claims. It is not a test
hook, on the argument `qt.highlighting` already won: being unable to see what
landed is being unable to tell an option that worked from one that did nothing.

### The near-miss matcher is extracted, not copied

The option names are the **second** closed vocabulary in this backend, so
`gb_nearest_word(word, set)` is now the matcher and
`gb_window_word_nearest` is three lines of it. That comment already recorded
the window vocabulary having been written out three times with the newest word
missing from the third; two copies of the *matcher* is the same drift one level
up.

### Nine perturbations proven red

Each caught by the tier written for it — and **the native one is caught by the
source tripwire alone**, every behavioural check staying green because
offscreen falls back, which is what says that tripwire earns its place. One
perturbation also found a **false claim in the fixture's own comment**: a
cancel with nothing preselected answers `unknown` either way, because the
selection is empty either way, so the check that claimed to catch a
result-code-ignoring backend did not and an unrelated tier caught it instead.
It opens the chooser **on a file** now, which is also the realistic shape.

### Still open

- **Choosing several files at once.** `QFileDialog` does it; the verb would be
  `qt.choose_files`, answering a list *always* — a verb whose return **kind**
  moves with an option is a shape this tree records as biting. Nothing has
  needed it, so it is one more verb that is not written.
- **The desktop's own chooser**, which is what a user on a real desktop
  expects (recent places, bookmarks, cloud). Measured undrivable above, so
  offering it would mean a mode in which `qt.answer` silently does nothing.

## 0.17 The window lifecycle (2026-10-18): closing one, and knowing it closed

**What was missing was not "a second window."** That was measured first and
turned out to work already: two windows open at once, parts scoped per window
(`qt.read(other, "who")` refuses by name), a dialog parenting to whichever
window is named, `qt.stop()` with both up — and opening the same declaration
twice already refused by name. Third time in this backend that measuring a gap
before filling it found it closed (menus needed no grammar, §0.7; the builder's
door was already open, §0.9).

What was missing was the **lifecycle**, and the four gaps were measured one
throwaway program at a time:

| asked for | answer before this |
|---|---|
| `qt.close(win)` | `undefined function: qt.close` |
| a method on the **window** | `qt.invoke: this window has no \`a_win\`` |
| `on a_win.closed` | `names nothing in this window` |
| what closing the last window does to `qt.run()` | unmeasurable — nothing could close one |

### Then six things were measured in C++, and the fifth decided the design

| | |
|---|---|
| `quitOnLastWindowClosed` | **true** (Qt's default; we never set it) |
| `close()` offscreen | delivers `QEvent::Close` to an event filter |
| closing one of two windows | `exec()` keeps running |
| closing the last one | `exec()` **returns** |
| **after `close()`** | **the widget is still alive and still readable** |
| `delete` right after `close()` | safe, `close()` having returned |

**The fifth row is why a close is bookkeeping and not a hide.** Qt does not set
`WA_DeleteOnClose`, so a window the person dismissed goes on answering
`qt.read` with the values it was showing — a program reading off a screen
nobody is looking at, raising nothing. That is the failure class this whole
backend is tested against, arriving from Qt's own defaults. So a close
**removes the window from gBASIC's registry**, and every verb that needs an
open one then refuses by name.

### Which makes `on <win>.closed` load-bearing rather than a notification

It fires **before** the registry forgets, so it is the one place a program can
still read what the person typed:

```basic
window app( title: "Customer", width: 360, height: 200 )
    column( margin: 10 )
        form to edit cust
            field who( label: "Customer" )
        end form
    end column

    on app.closed
        g.saved = qt.subject(app, "cust")      ' still readable, just
    end on                                     ' -- the next thing that happens
end window                                     '    is the registry forgetting
```

A program with nothing to save needs no handler; one that has something has
exactly one place to put it. Asserted **both ways** — the handler reads the
live widget, and the same read *after* the close refuses — because either half
alone passes on an implementation that got the order backwards.

### The window is a part of itself

One `parts.insert` and four doors open at once, none of them knowing anything
new: `qt.read(win, "app", "windowTitle")`, `qt.set`, `qt.invoke(win, "app",
"showMaximized")` and `on app.closed`. Measured before relying on it:
`windowTitle` is a `Q_PROPERTY` of `QWidget` (index 43) and `close()` and
`showMaximized()` are invokable by name (24 and 21), so nothing had to be added
for any of them. That is §1's by-name argument paying off a fourth time.

**The cost is a collision, and it is refused rather than silently won.** A part
called `app` would register *second*, over the root, so `on app.closed` would
connect to a button — and `qt.read(win, "app", "windowTitle")` would not fail
either, because `windowTitle` is a property of **every** `QWidget` and a
non-window one's is empty. So the answer would be `""`: a plausible value for a
window whose title is right there on screen, which is this backend's failure
class exactly. It gets its own sentence, because "declared twice" is the wrong
advice when one of the two is the window. The handler-target list is seeded with the window's name, which
is the whole of making it addressable at load time — and the same rule reaches
a **built** record, one checker and two adapters (§0.9).

`closed` is answered in `gb_qt_connect` before the by-name signal lookup,
because **a QWidget emits nothing when it closes — it receives an event**, so
that lookup has nothing to find and would refuse a perfectly well-formed
handler. The special case is narrow: `on save.closed` on a button falls through
and gets `a QPushButton emits no \`closed\` signal`, correctly.

### One path for all three ways in

`qt.close` calls `widget->close()` rather than doing the work itself, so the
program's verb, an invoked `close` and the window manager's own button are the
**same code** — the rule `would` and `execute` follow in `automation`, and for
the same reason: if they could disagree, a suite driving one would be
describing the other. The X button is the one that cannot be driven here
(offscreen has no window manager); it reaches `QWidget::close()` through Qt's
platform layer, so that much is construction rather than measurement, and is
said rather than implied.

### `qt.is_open` answers where the others refuse

Same argument `qt.can_display` and `qt.can_highlight` rest on: a question asked
*in order to choose* must not raise. It is needed here because **a window can
close without the program asking** — once the person can dismiss one, every
other verb starts raising for a reason that is nobody's mistake, and a program
with no way to ask would have to wrap each call in `on error` to learn
something it could simply be told. `qt.close` on a window that is not open
still **refuses**, which is `qt.answer`'s rule: a silent no-op would hide the
one case a program most needs to know about.

### A correction this increment exposed rather than introduced

`gb_qt_stop` called `QCoreApplication::quit()`, and **measured on Qt 6.10.2
that closes every top-level window** (it calls `closeAllWindows`), where
`exit(0)` ends the loop and leaves them alone. So `qt.stop()` — which means
*give me the thread back* and nothing more — was **dismissing the program's
windows**, and nothing could see it: they were merely hidden, every part stayed
readable, and no fixture asked whether the window was still up. It surfaced
only once a close became bookkeeping, when a program that reads its window
after `qt.run()` returned started being told it was not open.

**Measured rather than grepped, and the two answers differ.** Eight programs in
this tree read a window textually after `qt.run()`; exactly **one** actually
broke — the cookbook recipe whose subject *is* the event loop — because in the
others the reads sit inside handlers that run before the stop, or `qt.run()` is
not reached in the mode the suite drives. A grep would have reported eight
regressions and sent somebody looking in seven wrong places.

Both halves are now asserted, because each catches a different wrong
implementation: `is_open` catches a `stop` that runs the close bookkeeping, and
`visible` catches one that merely hides the window — which is what the defect
actually did, and which `is_open` alone would go on missing.

### A raising `closed` handler behaves like any other handler

Measured side by side with a raising `clicked` one and the two are **identical**:
the raise is reported, located, on stderr and then *contained* so the loop
survives, and the statement that provoked it is abandoned — so `ok =
qt.close(app)` leaves `ok` undefined while the close itself completes. That is
the raise containment §5 carries over from `gi_signal_marshal`, unchanged, and
the uniformity is the point: `closed` is not a special kind of handler, so it
must not fail in a special way. Nothing new was written for it and nothing is
claimed beyond that measurement.

### Reopening is bounded, and that had to be measured

Before this, a second `qt.open` met "already open", so open-and-close in a loop
could not be written — and the obvious implementation grows for ever, one node
per part per open. A `qt.close` deletes the widget where it stands; a close
noticed in the **event filter** cannot (the filter runs inside that widget's
own dispatch and `close()` goes on to hide it afterwards), so that one waits
and is deleted at the next `qt.open`. `deleteLater()` was the obvious answer
and is **not** relied on: a deferred delete needs a loop to spin, and the
commonest shape here closes the *last* window, which returns from `exec()`.

Measured over 200 and 2000 cycles, half closing by the verb and half by the
event:

| | 200 cycles | 2000 cycles | ratio |
|---|---|---|---|
| retired | 41.9 MB | 42.3 MB | **1.01** |
| not retired | 45.1 MB | 81.6 MB | **1.81** |

so the tier's gate at 1.25 separates them with room on both sides, as a ratio
rather than an absolute size.

### Eleven perturbations proven red, and one of them found a real defect

Each caught by the tier written for it. Two are worth recording.

**The re-entry guard was in the wrong place, and only a perturbation showed
it.** Written as `!n->widget` alone it caught the `qt.close` path — where
nulling the widget before closing already stops a second entry — so removing
the flag left the whole suite **green**. The case it misses is the realistic
one: the person clicks X, the handler tries to tidy up by closing the window,
and on that path the widget is still ours to delete, so deleting it destroys
the event filter currently executing, the node the outer frame is about to hand
to `g_closed`, and the widget both are parented to. It segfaulted with **no
output at all**, block-buffered stdout going with the crash — §0.3's shape
arriving from my own code. The fixture drives both paths now.

**The handler-less window is the tier to read first.** The bookkeeping must be
unconditional and the handler is the optional part, so a filter installed only
where `on app.closed` appears would leave exactly the programs that declared no
handler reading a dismissed window — and nothing else in that fixture could see
it, every other window there having a handler. Worse than a wrong answer: the
registry then holds an entry pointing at a window that has been deleted, which
is why that perturbation reports exit 139 after the mismatch that names it.

### The twelfth backtick, and a measured correction to the tripwire

This suite wrote three of them and `tools/scan_shell_backticks.py` caught
**two** — both on `pass`/`fail` lines — and missed the one on its own `refuse`
helper. Nearly every suite in this tree has such a helper and its first
argument *is* the message, so the four names were never the boundary of the
class, only the names that had been noticed. It reads every function **the file
itself defines** now, derived rather than listed, with that half asserted
separately in the selftest: with it disabled the `refuse` line goes unflagged.

### Still open

- ~~A window shown modally.~~ **Built 2026-10-19 — see §0.18**, and it needed
  this increment first: `qt.open_modal` closes the window before it returns, so
  the `closed` handler is where a modal form is read too.
- **No `qt.hide`.** `qt.invoke(win, name, "hide")` reaches it and is *not* a
  close, which is right (Qt sends no `QEvent::Close` for a hide) and means a
  hidden window still reports open. Nothing has needed the verb.
- **Nothing tells a program its window was MOVED or RESIZED.** Those are
  events like `closed`, so they would land the same way; nothing has asked.

## 0.18 A window shown modally (2026-10-19): the last of §0.15's list

**`qt.confirm` answers a boolean because two buttons is the whole of what a
message box offers.** Anything richer — a form to fill in, a list to choose
from, a thing with a layout of its own — is a window shown modally, which §0.15
named and left open:

```basic
window edit( title: "Customer", width: 360 )
    column( margin: 10 )
        form to edit cust
            field who( label: "Name" )
        end form
        row
            button save(   label: "Save" )
            button cancel( label: "Cancel" )
        end row
    end column

    on save.clicked    x = qt.accept(edit)   end on
    on cancel.clicked  x = qt.reject(edit)   end on
    on edit.closed
        g.typed = qt.subject(edit, "cust")        ' §0.17's rule, unchanged
    end on
end window

program main(args)
    cust = { who: "Ada Lovelace" }
    if qt.open_modal(edit) then
        print "saved " + g.typed.who
    else
        print "cancelled"
    end if
end program
```

### It is the same window, and that is held by one shared open path

`qt.open` and `qt.open_modal` differ in exactly two things: whether the root is
a `QDialog`, and what happens once it is built. Everything else — the record
check, the already-open refusal, the display refusal, starting Qt, the syntax
paths, the options, the build, the connect, the bind — is **the same code**
(`qt_open_declared`), because two copies of it would drift, which is the shape
this tree tripwires against (`would`/`execute` in `automation`; one checker and
two adapters in §0.9). So a modal window needs no vocabulary of its own, and
needing none is a measured fact rather than a hope.

That claim is structural, so it is held **two ways**: a tripwire counting the
places that build a window (exactly one), and a **parity** tier that opens one
declaration both ways and requires every observable to agree — the parts, the
window's own title, a column heading, a cell, the menu bar, and the grid
position `qt.at` reads back from the layout.

### A QDialog root, only when asked for

Measured before choosing it: everything this backend builds works on a QDialog
— a menu bar (which here is a plain `QMenuBar` in the root's box layout, not
`QMainWindow`'s), a form, tabs, a table, buttons — and `accept`, `reject` and
`done` are **invokable by name** on one while being **absent** from a `QWidget`.
So the answer mechanism is Qt's own. (On Qt 6.10.2 those are method indices 39,
40 and 38 against −1; the numbers are recorded because they are what was
measured, and only their *presence* is the claim — an index moves with the
version, which is exactly why the lookup is by name.)

Making *every* root a QDialog would also have worked and was rejected as a
wider change for nothing: with a QWidget root an ordinary window genuinely
*has* no `accept`, so refusing `qt.accept` on one is a fact rather than a
policy. A QDialog shown with `show()` is an ordinary window — `isModal` is
false until `exec()` runs — so nothing else moves.

### Four ways it can end, and the fourth row shaped the implementation

| | `qt.open_modal` answers | `closed` fires |
|---|---|---|
| `qt.accept(win)` | **true** | yes |
| `qt.reject(win)` | false | yes |
| `qt.close(win)` | false | yes |
| the window manager's button | false | yes |

**Accepting delivers no `QEvent::Close` at all.** Measured, and finer than the
guess that prompted the measurement: `close()` delivers one on a visible widget
**and** on a merely hidden one, and **nothing** on a QDialog that `done()` has
already finished with — `done` marks it closed, so a later `close()` is a
no-op. A backend whose bookkeeping hung off the event would therefore skip the
commonest ending in silence, and `qt.is_open` would answer `true` for a window
the program had finished with.

So the ending is **one function called from two places and idempotent**
(`node_closed`), not a side effect of an event: `gb_qt_close` does the
bookkeeping directly and the event filter becomes a no-op. The perturbation
that reverts it reports the handler never firing and the window still open.

### `qt.open_modal` closes the window before it returns

`accept()` leaves the widget hidden but alive and readable, while the window
manager's button closes it — so `false` would otherwise mean two different
states, and every `qt.close` after a modal window would need an `qt.is_open`
guard. Closing here makes the `closed` handler fire **exactly once however the
window ended**, so §0.17's single rule — the handler is where a dismissed form
is read — holds for a modal window with no exception. The cost is stated: the
subject is read in the handler, not after the call.

### Its extent is the dialog's, and the control is what makes that precise

§0.15 measured that a dialog raised **from a handler** suspends gBASIC's own
sources until it is answered — two guards, each sufficient alone. This is the
other case and it is the common one: *"show this form and wait"* is opened from
`main`, and there the sources **keep running**. Measured from another process:
the webserver answers `pong` while a modal window is up, and a `timer` fires
inside the modal loop — which is also what makes the window answerable without
a person, and therefore what makes any of this drivable.

Without that control, §0.15 reads as *"a nested Qt loop starves gBASIC"*, which
is false and would be the wrong lesson to carry into the Windows port. What
starves it is re-entering the **interpreter**, not the loop.

### `qt.accept` rather than `qt.answer`

`qt.answer` exists to drive a dialog **from the program** when nobody is there
— a timeout, an unattended run, a suite. This is the person's own answer
arriving through a button they pressed: a different thing wearing the same
word, so it gets its own verb rather than a third argument shape on that one.
Its refusal names the verb that would have made the window modal, because Qt's
own would say *"a QWidget emits no…"* about a method, which names the wrong
thing.

### Three defects of mine, each found by running it rather than reading it

**The helper was written outside `#if HAVE_QT`.** This file's own convention
note says code that is *not* optional must stay **out** of those guards —
`qt.check` is exactly that. The mirror is just as true and is the half I got
wrong: code that *is* optional must stay **in** them, and the shared open path
calls `gb_qt_window`, `gb_qt_begin` and `gb_qt_show`, none of which exists
without Qt. Written just after the `#endif`, it compiled fine here and broke
`make QT_AVAILABLE=0` with `QT_ERRBUF undeclared` — caught by
`run_platform.sh` and `run_packaging.sh`, the two suites that build lean, and
by **none** of the ten Qt suites, which all run on a full build. CLAUDE.md
names the cheap check for exactly this (`make QT_AVAILABLE=0` before trusting a
green gate) and I skipped it.

**The ending was hung off the event** and the accepted case silently did
nothing — found by the first program written against it, which reported
`returned true` beside `still open true`. Fixed by the `node_closed`
extraction, which is better than the original design rather than a patch on it.

**The tripwire's own control could not fail.** `grep -c` counts matching
**lines**, and the planted second call site went on the same line — which is
exactly how somebody would write a second path by hand (`modal ? a : b`). It
counts occurrences now, and the control is what found it.

### Still open

- **A modal window opened from inside a handler** is legal and is **not
  asserted**: §0.15's limit applies, so gBASIC's sources are suspended and
  nothing but a person can answer it — which is fine for a real user and
  undrivable in a suite. A claim nobody has run is not a claim.
- **A modal window over a modal window**, same reason.
- **No `qt.hide`.** `qt.invoke(win, name, "hide")` reaches it and is not a
  close, which is right; nothing has needed the verb.

## 0.19 `split` and `scroll` (2026-10-19): the two words Studio's shell needs

**Derived from gBASIC Studio's own source rather than guessed**, which is why
these two came first of the gaps §0.18 left. Its shell is `gtk.paned` **three
times nested** and `gtk.scrolled` **five times**, and nothing in this
vocabulary could express either: `row` and `column` are boxes nobody can drag,
and while `source`, `memo` and `table` scroll themselves, an ordinary column of
widgets does not. One increment, because they are one layout.

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

That is Studio's shell: a file list beside an editor above an output table.

### A measurement made the implementation small

| | |
|---|---|
| `QSplitter` has **no layout** | like a `QTabWidget`, so the layout guards had to learn about it |
| **parenting is enough** | `count()` went to 2 from two `new QLabel(…, sp)` calls — no `addWidget` anywhere |
| `orientation` is a Q_PROPERTY | Horizontal=1, Vertical=2, and it accepts the string `"Vertical"` too |
| `widgetResizable` defaults to **false** | the trap — see below |
| one pane has **zero** handles | n panes give n−1, so a one-pane split is a container nobody can drag |

The second row is why the backend change is three branches and a guard rather
than a child-management scheme: a widget created with a splitter as its Qt
parent *is* a pane, so the construction that already existed does the work.

**`widgetResizable` is the trap.** Left at Qt's default the inner widget keeps
its original size for ever, so content never reflows to the viewport — a window
that is resized shows a stripe of widgets beside a grey expanse, with nothing
raised. Measured working: 40 rows give an inner height of 932 against a 258
viewport, and the scrollbar appears.

**`sizes` is neither a property nor invokable** (it takes a `QList<int>`), so
saving and restoring pane widths needs its own verb and is **not** in this
increment. Stated rather than discovered.

### The direction spends the one free slot, so the split cannot be named

§0.8 measured that a container carries a **name or options, never both**
(`tabs settings( movable: true )` costs four reduce/reduce). A splitter with no
direction is not a splitter, where a name is something only some splitters
would ever want — so `split across` / `split down` puts the direction in that
slot, at **zero** grammar cost, and the validator refuses anything else by name
with a near-miss hint.

**The cost is real and stated:** a `split` cannot be named, so `on
x.splitterMoved` and `qt.set` cannot reach it. A `scroll` needs no direction,
so its slot *is* free — it may be named, and that is what makes
`widgetResizable` and the scrollbar policies readable through the ordinary
by-name route.

### Which left the split unobservable, so the oracle came first

Nothing could address a splitter, so nothing could say whether one was built,
which way round it is, or how many panes it got. `qt.pane(win, part)` answers
`{ kind, direction, panes, index }` for a part in a split and
`{ kind, resizable }` for one in a scroll, or **`unknown`** when it is in
neither — a record saying `panes: 0` would read as a splitter that holds
nothing, which is the rule `qt.dialog` and `principal()` already follow.

**Asked of the layout**, walking up from a part that *can* be named, which is
the rule `qt.at` already follows for a grid: a position we remembered would
agree with a placement that was wrong. It keeps the last widget it came
through, because a pane is usually a **container** and the part asked about is
inside it — a perturbation that asked the splitter for the named widget's own
index left every check passing, since every other part in the fixture happens
to *be* a pane.

The **nesting** check falls out of it: the outer split must report **two**
panes, so an inner split hung off the window instead of its parent is caught —
the shape the menu oracle had to learn the hard way (§0.7).

### Two things this increment found on the way

**A named leaf with no options was a parse error.** `field who()` and `source
code()` — and a *pane* needs no label, so that is exactly what an author
writes. Measured at **zero** conflicts and kept; not decoration in a project
that rejected `IDENT expression` as a statement form over four.

**A pane has nowhere to put a label, and the refusal is not in the validator.**
That is where it went first and it **could not work**: `label()` reads a
*top-level* label — what a `group "Credit"` carries — while a leaf's `label:`
is an **option**, so it read NULL for every leaf and the check silently never
fired. That is §0.8's own recorded defect reproduced exactly, one increment
later. It lives in `gb_qt_part` now, where the label is a parameter, and it is
narrow by derivation rather than by taste: a `button`, a `check`, a `text`, a
`memo` and a `source` all consume their own caption at construction, so only a
`field` and a `choice` — whose label the **parent layout** places — are
refused, with a `check` keeping its caption as a pane asserted beside it.

### Eleven perturbations proven red

Each caught by the tier written for it. The guard tripwire is the one worth
reading: a splitter has no layout, so **three** separate doors ask a parent for
one (`gb_qt_container`, `gb_qt_part`, `gb_qt_table`) and each was found by
running the thing — a table as a direct pane was refused with *"that container
cannot hold a table"*, a true sentence about the wrong thing. A fourth door
would refuse panes in silence, so the tripwire balances the parentheses of
every `if` mentioning `!p->layout` and asks whether that condition names a
splitter. Counting lines instead reported two perfectly well guarded
conditions, because they name the local the function already made rather than
an inline cast.

### Still open

- **Pane sizes.** `setSizes` takes a `QList<int>` and is neither a property nor
  invokable, so *"restore my layout"* needs a verb of its own.
- **`on <split>.splitterMoved`**, which needs a spelling that frees the slot.
- **A scrollbar policy as a word.** `qt.set(win, "files",
  "verticalScrollBarPolicy", ...)` reaches it today, which is a Qt-ism in a
  place that otherwise has none.

## 0.20 A context menu (2026-10-19): `popup`, and a Qt default that silences a handler

Studio's browser right-clicks a row and offers what that row can do, and
nothing in this vocabulary could express it. Derived from Studio's own source
rather than guessed, like `split` before it — and reading it changed the
increment twice, because two of the three things I expected to build were
already there.

### Measured first, and four of eight results changed what got built

| measured | result |
|---|---|
| `customContextMenuRequested` from a synthetic `QContextMenuEvent` | **fires** — an earlier probe reported 0 and was wrong, so the path is drivable |
| the same event sent to a `table`, `memo` or `source` | **0 times**, and it propagated to the *parent* |
| the same event sent to that widget's **viewport** | **1** — which is where a real right-click lands |
| `QMenu::popup` / `QMenu::exec` invokable by name | **no** (both −1, against `show` 18 and `hide` 19) |
| a right-**press** on a table row | Qt **moves `currentRow()`** to that row by itself |
| `visible` on a `QAction` | **Q_PROPERTY 14**, settable by name |
| an all-hidden menu | answers `isEmpty()` **true**, and `popup()` leaves it **invisible** |
| a `QMenu` parented to a widget that **has** a layout | layout count 2 before, 2 after — **not taken** |

So the increment shrank twice. **The row came free**: I expected to have to
deliver the signal's `QPoint` through a handler that takes no arguments, or to
select the row under the pointer as a backend policy, and Qt does it already —
`qt.selected` answers *which row was right-clicked* with nothing new. And
**Studio's per-row hiding came free**: its context menu is built once holding
every item and hides the ones a row does not offer, which is `qt.set(win,
"delete_it", "visible", false)` through the by-name route that already existed.

That keeps happening, and the instances are worth naming rather than counting —
§0.7 (menus needed no grammar), §0.9 (the builder's door was already open),
§0.17 (a second window already worked) and now both halves of this one. The
earlier sections each claimed an ordinal and they do **not** agree with each
other, because the count depends on whether "measuring changed the design"
(§0.8's four conflicts) belongs in the same series as "the gap was already
closed". Naming them is checkable where a number is not.

### The word, and the one slot

A `menu` could not be reused. It spends its one slot on the **caption** a
person reads on the bar — §0.8 measured that a container carries a name *or*
options, never both — and a context menu has no bar to be read on, so a caption
would name the thing nobody ever sees. What it needs is a **name**, so
`qt.popup(win, "row_actions")` can raise it:

```basic
popup row_actions
    item open_it( label: "Open" )
    item rename_it( label: "Rename..." )
    separator()
    item delete_it( label: "Delete" )
    menu "More"
        item info_it( label: "Properties" )
    end menu
end popup
```

The same trade `split across` makes in the other direction, and at **zero
grammar cost** — `popup row_actions` is the production `tabs settings` already
uses, and a submenu inside one is the same production recursing, exactly as
`menu "Recent"` inside `menu "File"` already was. Both halves of the slot rule
are refused, so neither word can be written the other's way: a `popup` with a
caption and a `menu` with a name are each named in a refusal.

Its **placement** is the bar's rule reached by a different route rather than by
analogy: a `QMenu` is placed by nothing (measured above), so a `popup` written
inside a `row` is a declaration the layout silently ignores — refused, which is
the class the closed vocabulary exists to prevent.

### Declaring the handler is what asks for the custom menu

This is the load-bearing half, and it is the silent class this backend is
tested against arriving from a **Qt default**. Every widget kind starts at
`DefaultContextMenu`, and under that policy `customContextMenuRequested` is
**never emitted** — so

```basic
on files.customContextMenuRequested
    ...
end on
```

without a second line setting the policy is a handler connected to a signal
nothing emits: the right-click does nothing, and nothing is raised. So
`gb_qt_connect` sets the policy when it sees that signal. The author has
already said what they want by naming the handler, and a vocabulary where the
request takes two lines is one where it gets written as one.

**The cost is stated rather than hidden**: this *replaces* the menu Qt would
have put up itself, so a `memo` or `field` with a context handler loses its own
cut/copy/paste. Which is why the policy is touched only where a handler exists
— asserted as a difference, with the control that a part with **no** handler
keeps Qt's. Without that control, *"the policy is set"* is satisfied by setting
it on every widget in the language, and the perturbation that does exactly that
is caught by the control and by nothing else.

### `qt.right_click` is the oracle, and it was built before the fixture

The discipline §0.8 arrived at by getting it wrong. A `table`, `memo` or
`source` answers only through its **viewport**, and gBASIC cannot name a
viewport — so without this verb the whole path is drivable only on `field` and
`button`, the two kinds that happen to have none, and every check would be
about the two cases that need no routing at all.

It sends a **press and a release** and then the context event, in the order a
mouse produces them, because the selection is part of the answer: a bare
context event would fire the handler over a stale selection, which is a verb
that drives half of what a mouse does and would make the row question look
unanswerable. `qt.right_click(win, part)` clicks the part; a third argument
aims at a table row's own rectangle, which is what makes *"right-click the
third file"* writable.

### `qt.popup` answers whether anything was shown

Not a convenience and not a refusal. Studio's own comment says an empty context
menu *"reads as a control that failed"*, and Qt agrees — an all-hidden menu is
`isEmpty()` and `popup()` leaves it invisible. A row that offers nothing is an
**ordinary outcome**, so a boolean is the truthful report where an error would
be about a program that did nothing wrong. Both halves asserted, since `true`
alone passes on a verb that always says yes.

**And the count recurses, which `examples/qt/browser.bas` found rather than any
reasoning.** Hiding every item *inside* a submenu does not hide the submenu:
measured, Qt then reports `isEmpty()` **false** and **shows** a 120×25 menu
holding nothing but an arrow leading nowhere — which is *"reads as a control
that failed"* one level in, and exactly what the boolean exists to prevent.
Qt's own test cannot see it; it declines only once the submenu's own action is
hidden too. A submenu is told by `menu()` being non-null (a plain item's is
nil), so it counts for **what it leads to** rather than for itself. Asserted
both ways, because a count that stopped treating a submenu as anything at all
satisfies the first half.

### The oracle a flat registry always needs

`qt.menus(win, "row_actions")` reads a popup's entries, taking **text where a
bar position is a number** — a `popup` is not on the bar, so it *has* no
position. It exists for the measurement the submenu oracle already rests on: an
`item` keeps its name wherever its menu ends up, so one built into the wrong
popup of two passes every by-name check there is, and that perturbation
produced **zero mismatches** when the same hole was open for submenus. It also
says a popup has **not** quietly been added to the bar, since
`gb_qt_menu_count` cannot see one.

One entry walk serves a bar menu, a submenu and a popup — two copies would
drift in the way that matters, a popup's items stopping agreeing with a menu's
about what `shortcut:` or a submenu means. Tripwired, with its vacuity control
counting **occurrences rather than lines**, which is how the equivalent control
in §0.18 was found unable to fail.

### Two defects of my own, both worth recording

The fixture's first draft counted into plain scalars in `main`, and every
counter read 0 while the handlers had plainly run — the selection had moved and
the item visibility had changed. That is warning 2104 and §0.12's own rule: a
handler cannot rebind an outer scalar. The state is one record whose fields the
handlers mutate, which is exactly what the cookbook's recipe 3 exists to teach,
and I walked into it anyway.

And **restoring a perturbation destroyed an hour of uncommitted work.** The
standing advice is to restore with `git checkout` rather than from a snapshot
copy, and that advice is incomplete: it is safe only when *everything else* in
the tree is committed too. Here the increment was committed and the docs, the
submenu fix and a backtick fix were not, so `git checkout -- .` took the lot.
The discipline is `git checkout -- <the one perturbed file>`, or commit before
each round.

### Still open

- **Adding to Qt's own menu rather than replacing it.**
  `createStandardContextMenu()` is how an editor keeps cut/copy/paste *and*
  offers its own, and it needs a `contextMenuEvent` override rather than the
  signal — so the honest position today is that a context handler on a `memo`
  or `source` replaces the standard menu, which this increment states and does
  not fix.
- **The signal's position.** A handler takes no arguments, so the `QPoint` is
  unreachable. Nothing needs it while Qt moves the selection, and a non-table
  part that wanted *where* was clicked would.
- **`popup` at the pointer only.** `QCursor::pos()` is right for a right-click
  and wrong for a menu raised from a keyboard shortcut.

## 0.21 The wheel (2026-10-19): an event, and a zoom that needed no verb

Studio uses a `Gtk.EventControllerScroll` for exactly one thing — **Ctrl +
wheel zooms the editor** — and its handler reads `dy`, calls one function and
returns `true` to mean handled. So the requirement is a direction, the
modifiers, and a way to say handled. Nothing more.

### Measured first, and three of five results shrank it

| measured | result |
|---|---|
| a synthetic `QWheelEvent` | **delivered** — a plain one moved the scrollbar 92 → 95 |
| does `QPlainTextEdit` zoom on Ctrl+wheel? | **no** — the font stayed 9.00, so the gap is real |
| Ctrl+wheel on a `QPlainTextEdit` | scrolls **faster** than a plain one (95 → 111 against 92 → 95) |
| can an event filter swallow it? | **yes** (102 → 102), and declining leaves plain scrolling intact (102 → 105) |
| `font` on a widget | **Q_PROPERTY 27**, and it reads back as **text** |

Two of those are worth stating plainly. **A synthetic wheel event being
delivered is better than GTK** — Studio records its scroll and key controllers
as its *only* untested controls precisely because `Gtk.EventControllerScroll`
has no emit, so `qt.scroll` closes an exception that project had to sanction.
And **the zoom needs no verb**: `font` reads back as
`"monospace,9,-1,5,400,..."`, so a program splits it, changes the number and
writes it back — asserted through the interpreter rather than taken from the
probe. That is why there is no `qt.zoom`, and it is a measurement rather than a
deferral, since `zoomIn`/`zoomOut` carry a defaulted argument `qt.invoke` could
not reach anyway. One more for §0.20's list of gaps that measuring found
already closed.

### What is genuinely missing is the event

A QWidget has no wheel **signal** — it *receives* a `QWheelEvent` — so
`on code.wheel` is the second thing here that has to be an event rather than a
signal, after `closed`, and it is answered the same way: before the by-name
lookup, which would otherwise refuse a well-formed handler with *"a
QPlainTextEdit emits no `wheel` signal"*.

**The filter goes on the viewport**, which is `mouse_target`'s third caller
now. An event filter sees only what is delivered to the object it sits on, so
one installed on the `QPlainTextEdit` itself would never fire — the same
measurement `qt.right_click` rests on, load-bearing here for a second reason.
And it is installed **only where a handler was declared**: one per window, or
one on `QApplication`, would see every wheel event in the program, and a filter
that claims is a filter that costs a `source` or `table` its ordinary
scrolling.

### `qt.swallow` is not optional

Ctrl+wheel scrolls *faster* than a plain wheel, so a handler that zooms without
claiming the event leaves the editor jumping about while it resizes. It refuses
when nothing is in flight rather than being a silent no-op — the rule
`qt.answer` already follows.

`dy` is in **notches**, not Qt's 120-unit angle delta: one click is what a
person did, and a vocabulary that made a program divide by 120 would leak the
wrong half of the abstraction. A sub-notch turn rounds **away from zero**,
because a trackpad sends deltas under 120 and integer division would report
that the wheel moved by nothing — in the one value whose *sign* is the whole of
what a zoom reads. Positive is away from the user, which is Qt's own sign and
the **opposite** of GTK's, so Studio's `if dy < 0 then zoom in` inverts when it
is ported.

### Two perturbations went green, and both added something

This is the part to read. `qt.scroll` answers whether anything **claimed** the
event, which is the shape `qt.popup` has — and a filter made to record the
claim and then return `false`, so the claim is reported while the view scrolls
anyway, was caught by **nothing**. That answer agrees with `qt.swallow` having
been called, whatever Qt then did with the event, so the whole of what
swallowing does was being asserted against the program's own word for it.
Agreement is not correctness, which is PLAT-NUMFMT's lesson arriving inside my
own increment.

The only honest observable is the position, and a program could not ask: a
`QAbstractScrollArea` reaches its scrollbar by a **method**, so the by-name
property route cannot see one. Hence **`qt.scrolled`** and **`qt.scroll_to`** —
both directions, because a reader without a writer is the shape `webclient`'s
read and write halves had. Not a test hook either: reopening an editor where
somebody left it is what Studio already does for its panes. It answers
`unknown` for a part that does not scroll, a different claim from *"scrolled to
nowhere"*.

The second green perturbation was the sub-notch rounding: with `qt.scroll`
taking whole notches it could not be driven at all, so that verb takes a
**fraction** now and a quarter-notch is asserted in both directions.

### A lesson about restoring a perturbation, the second in one day

The morning's increment lost an hour of uncommitted work to
`git checkout -- .`, and the remedy written down was to name the one file. That
is **also wrong when the uncommitted work spans files**: restoring the
perturbed `.cpp` reverted my own unfinished change in it while the header kept
the matching one, and the build failed with a conflicting declaration. The
discipline is to **commit first and then perturb** — neither a blanket
checkout nor a single-file one is safe over work in progress.

### Still open

- **A key handler** is still not needed and the reason is worth keeping: a
  `QAction` shortcut fires while a `QPlainTextEdit` has focus and the editor
  does not eat the key, so Studio's zoom keys are `item zoom_in( shortcut:
  "Ctrl++" )` in a menu and need no mechanism.
- **The horizontal wheel** is reported (`dx`) and nothing uses it, so the sign
  convention there is untested.
- **A mouse position** for a wheel event, which a chart zooming about the
  pointer would want.

## 0.22 Pane sizes (2026-10-19): §0.19's open item, and why it has no name

§0.19 left this open and both of its reasons still hold. `setSizes` takes a
`QList<int>`, so measured, `sizes` is **not** a property (−1) and `setSizes` is
**not** invokable by name (−1) — the by-name route this backend reaches
everything else through cannot see either. And a `split` **cannot be named**,
its one slot being the direction, so there is nothing to look up.

**So it is addressed by walking up from a part**, which is the rule `qt.pane`
already follows and is why this needed no new spelling: `sizes` joins what that
answers, and the write half is a verb taking the same part.

### Studio needs it, which is why it came before icons

Its shell reads `split.position` for **three** splits out of a saved session
and writes them back on exit, so a converted Studio without this forgets its
layout every time — a feature it already has. GTK's `position` is one number
because a `Gtk.Paned` has exactly two children; a `QSplitter` holds any number,
so this is a **list**.

### Three measurements, and the first is why nothing asserts an exact size

| measured | result |
|---|---|
| `setSizes` exactness | asked 260/540 of an 800-wide splitter, **got 252/522** — the handle and the margins |
| a **wrong count** | `[100]` on a two-pane split gave **`[774, 0]`**, collapsing the second; `[200,300,400]` gave `[310,464]`, honouring none |
| a window resize | 194/580 became **244/730** — Qt re-proportions |

So what a program stores is a **ratio** in effect, and nothing here promises to
hand the same numbers back. Every check is a proportion or a difference.

**A count mismatch is refused**, and that is measured rather than cautious:
both silent outcomes above are perfectly ordinary-looking layouts that are not
what was asked for, which is the class this backend is tested against. The
refusal names **both** numbers, because the author has to know which way they
were wrong. A negative size is refused too rather than clamped — Qt would
quietly treat it as a collapse, a layout that looks deliberate and is not.

### One walk, and the check that it finds the right splitter

The reader and the writer share `splitter_of`, because two walks would be two
things that can disagree about which split a part is in. **The tier that
matters is that resizing the inner split leaves the outer alone** — every size
check in the fixture passes on a walk that resized whichever splitter it met
first, and that is the one that does not.

It stops at the **nearest region**, like `qt.pane`: a part inside a `scroll`
that is itself a pane reports the scroll, not the split, because the scroll is
what immediately holds it. Asserted both ways, with the control that a `table`
which *is* a direct pane is still found — without which the refusal is
satisfied by a walk that refuses everything that is not a plain widget.

**Zero is legal**, which is how a shell hides a panel without forgetting it,
and a collapsed pane comes back. Three panes are asserted separately, because a
two-pane-only implementation passes everything else and a `Gtk.Paned` cannot
even have three.

### Still open

- **`on <split>.splitterMoved`**, which still needs a spelling that frees the
  slot — so a program can save the layout when it changes rather than only on
  exit.
- **A scrollbar policy as a word**, unchanged from §0.19.

## 0.23 Icons (2026-10-19): the one property the by-name route cannot carry

Studio uses `Gtk.IconTheme` and nothing in this vocabulary could express an
icon at all.

### Measured first, and four of six decided something

| measured | result |
|---|---|
| `setProperty("icon", QString(...))` | **false**, for a theme name *and* a file path, icon left null |
| `QIcon::themeName()` here | **empty**, and `hasThemeIcon("document-save")` **false** |
| `QStyle::StandardPixmap` | **81 keys**, and `standardIcon` works with **no theme** |
| `QIcon(path)` on a real PNG | non-null |
| a QIcon's `toString` | **empty** whether or not an icon is set |
| `icon` as a Q_PROPERTY | QPushButton 62, QAction 4 |

The first is what makes this an increment rather than a documentation line:
**`icon` is the single property the by-name route cannot carry**, because
QVariant will not convert a string to a QIcon. Its branch therefore lives in
`set_property` — the **one** door the declaration's options and `qt.set` both
go through — so the two cannot disagree about what an icon source is.
Tripwired, with its vacuity control.

### Three sources, told apart by facts rather than guesses

- **`SP_...`** — one of QStyle's 81 `StandardPixmap` keys. Qt's *own* namespace,
  so this is not a heuristic, and measured, these work with no icon theme
  installed: the only source that always does.
- **an existing file** — asked of the filesystem, so a fact rather than a shape.
  What an application shipping its own icons uses.
- **anything else** — a freedesktop theme name.

### And the failures are answered differently, which is the design

A **path that is not there** is a fact we can check and an intent that is
unambiguous, so it is **refused**, naming the path. So is a **`SP_` name Qt
does not know**: that is Qt's own namespace, so a name in it that Qt rejects is
a misspelling, not a machine without a theme.

A **theme name that resolves to nothing** is a fact about *the machine* —
measured, this one has no icon theme at all — so refusing would make
`icon: "document-save"` fail on exactly the headless machines the release
targets. It **warns** (2116), which is the position §0.14 takes for a missing
syntax definition and for the same reason: the program is perfectly usable
without the picture, and the failure is otherwise **invisible**, a button with
no icon looking exactly like one that was never meant to have one. The warning
tier also asserts the program **carried on**, which is the whole difference
from a refusal.

**Clearing is legal and silent.** A program swapping a play icon for a pause
one has to be able to take one away, and falling through would ask the theme
for an icon called `""`.

### `qt.has_icon` is the oracle, and it was not foreseen

It came from asking what a check could *observe*. A QIcon has no text form, and
measured, a button carrying a real PNG and one carrying nothing **both**
answered `""` through `qt.read` — so without this every check would pass on a
backend that resolved nothing. Worse, `qt.read` on an icon was answering a
**wrong** answer rather than no answer, which is the class this backend
refuses; it refuses now and names `qt.has_icon` instead.

That verb is also what a **program** needs, not only a test: an icon the
theme lacks is a warning, so falling back to a text-only button means being
able to ask.

### Still open

- **A size.** Qt picks one from the icon and the widget's `iconSize`, which is a
  property and so already reachable — but nothing asserts it.
- **An icon from bytes a program already holds**, which would need a verb
  rather than a path and has no caller yet.
- **A per-window icon** (`windowIcon`) is the same property on the root and
  should already work through the window's own name; unmeasured.
- **PLAT-NUL's sweep has never reached this backend**, and `icon:` is the first
  option where that matters. Every text option crosses the seam as a
  `const char *` (`gb_qt_set_part_text` and the option path both pass
  `v.as.string`), so a string with an interior NUL **truncates** — measured by
  reading, not by a probe: nothing in `src/modules/qt.c` or
  `qt_backend.cpp` checks for one. For a label that is meaningless; for an
  icon it is a **path**, and a truncated path silently becomes a theme lookup
  that finds nothing and warns about a name nobody wrote.

  The language's own answer for a path is to **refuse** one with a NUL — which
  is what `real_path`, `file_type`, `file_name`, `directory_name` and both
  arguments of `join_path` already do, because a path crosses into the OS as a
  NUL-terminated string and so cannot be opened anyway. But fixing the icon
  door alone would **move** the truncation rather than remove it, which is
  exactly the mistake PLAT-NUL records: the whole by-name property route is one
  door, and the sweep belongs to it rather than to this section. Recorded here
  because `icon:` is what made it reachable.

## 0.24 Completion: measured, not built (2026-10-19)

The next item on Studio's list, measured before being designed — and the
measurement says it is a bigger increment than §0.20–§0.23, so this records
what is known rather than guessing at a shape.

### What is measured

| measured | result |
|---|---|
| `completer` as a property | **−1** on both `QPlainTextEdit` and `QLineEdit`, and `setCompleter` is **not invokable** |
| a `QCompleter` over a word list | works — prefix `pro` gives `program`, `property`, `procedure` |
| `complete()` | makes the popup **visible** offscreen |
| the word under the cursor | findable: `WordUnderCursor` on a cursor at the end of `x = pro` gives `pro` |
| inserting a completion | works with no key handler — `x = pro` became `x = program` |
| replacing the model | works, so the word list can follow the data |
| **a Return key sent into the popup** | **fires nothing**, and the popup stays up |
| **a Return key sent into the editor** | fires nothing and inserts a newline |
| `activated` | two overloads, `QString` and `QModelIndex` |

### What those decide, and what they leave open

**It cannot ride the by-name route.** `completer` is not a property and
`setCompleter` is not invokable, so attaching one needs a verb — the same
position the highlighter is in, and for the same reason: it is an *object*
attached to a widget rather than a value on it.

**A `QLineEdit` handles completion itself; a `QPlainTextEdit` does not.** For
an editor the host must, on every keystroke, find the prefix, call
`setCompletionPrefix` and `complete()`, and then handle `activated` and insert
the text. gBASIC cannot do that — this backend has no key handler, deliberately
(§0.21: a `QAction` shortcut covered Studio's keys). So **the backend would have
to own a keystroke loop**, which is new machinery and the reason this is not a
small increment.

**And the popup is not drivable headless**, which is the sharper problem: a
Return into the popup fires nothing and leaves it up, so a suite cannot choose
an item. What *is* drivable is the list (a prefix gives rows) and the insertion
(a cursor's word can be replaced). So a testable design makes the **data**
primary — a verb supplying the words, and an oracle answering what would be
offered for the word under the cursor right now — and states the popup's own
appearance as unasserted, the way §0.15 states the limit on a dialog raised
from a handler.

**What is NOT yet decided**, and should be decided by measurement rather than
here: whether the backend drives the whole loop (so a program only supplies
words), or whether a key handler arrives first and the loop is written in
gBASIC. The first is smaller and less general. The second is
interesting because it would give a key handler its **first real caller** —
§0.21's open list says a key handler is *not needed*, and that is still true of
everything measured so far, so completion would be the thing that changes the
answer rather than a use for something already wanted.

## 0.25 A table that asks for its cells (2026-10-19)

§0.6 deferred this and `table x to show <list>` materialises every row into a
QTableWidget, so the large-result path is a **model** behind a `QTableView`
rather than a bigger version of what exists. gBASIC Studio is the consumer
rather than a hypothesis: its results grid switches to
`datagrid.create_virtual(count_fn, cell_fn)` above 500 rows, and `datagrid` is
GTK-only — so this was the one thing its Qt conversion could not express.

### The one number that decided whether it is possible at all

A model's `data()` has to call back into the interpreter, and per cell of the
whole table that would be unusable. Measured in C++ before anything was
designed:

| model rows | viewport | `data()` calls | highest row touched |
|---|---|---|---|
| 100 | 300px | **27** | 8 |
| 10,000 | 300px | **27** | 8 |
| 1,000,000 | 300px | **27** | 8 |
| 1,000,000 | 900px | **87** | 28 |
| 1,000,000, then `scrollToBottom` | 300px | **54 in total** | 999,999 |

**The cost is the viewport, not the table** — and `scrollToBottom` jumps
straight to the last row rather than touching anything on the way.

### And it needed no new seam, which corrects my own first assessment

I wrote that this needs a new callback shape across the C/gBASIC boundary,
because every handler here is a name invoked with no arguments and no result.
Reading rather than assuming showed otherwise: **`invoke_function(stmt, args,
argc, receiver)` already takes arguments and returns a `Value`** — the handler
path simply passes `NULL, 0, NULL` because handlers take none, and
`qt_invoke_handler` is already a caller. So this is one more callback beside
`GbQtInvokeFn`, not a new kind of crossing.

### `ask` is the third direction, at zero grammar cost

```basic
table results to ask cell_of
    field who(   label: "Customer" )
    field total( label: "Total" )
end table
```

`IDENT IDENT TO IDENT IDENT` is the production a named container with a subject
already uses, and the direction is recognised **by position** in the validator
rather than by a token — so `ask` joins `show` and `edit` for nothing. The
subject is a **function name** rather than a list, which is what the word says:
a table that *asks* for a cell instead of being shown one.

**A form is still refused one**, and that is a control rather than caution: a
form shows or edits *one* record whose fields it can read, so there is nothing
for it to ask *for*, and `form x to ask f` would be a declaration whose subject
could never be bound. Without that refusal, "ask is a direction" is satisfied
by letting every word that carries a subject take it.

**The row count is a verb**, `qt.rows(win, table, n)`, because it changes:
Studio's grid is refilled by every query, so a count fixed when the window
opened would be wrong the first time anybody ran a second one. Setting it
resets the model, which is how Qt is told to re-ask — so there is no
`qt.refresh` for an asking table, the count *is* the refresh. Reading it back
with no count is the only way to observe that a set took.

**Everything else answers through the model**, so a program never has to know
which kind of table it declared: `qt.cell`, `qt.heading`, `qt.select`,
`qt.selected` and the row count all take either. A failed callback draws an
**empty cell** rather than stopping the program, which is forced rather than
chosen — `data()` runs during a *paint*, with Qt's own frames between us and
anything that could unwind, so there is nowhere to raise to; the raise is
reported through the ordinary contained-raise channel instead.

### The oracle was free, for once

The callback is gBASIC code, so it **counts its own calls** — everything the
headline claim needs is a number the fixture already has. Asserted as a
difference across four orders of magnitude *and* as the other side of the same
claim (far fewer calls than rows), because a model that walked the table would
be correct and unusable and nothing else in the file would notice.

Asserting it in the language took one idea beyond the C++ measurement: nothing
paints offscreen without a real turn of Qt's loop, so `repaint` answered **0**
cells. The fixture hands the thread to `qt.run()` and takes it back with a
timer, which is the cookbook's event-loop pattern.

### Two things found on the way, and both were mine

**A green perturbation exposed the fixture, not the implementation.** A model
made to prefetch five hundred rows on every `qt.rows` passed every check —
because the fixture reset its counter *after* `qt.rows`, so cost incurred while
setting the count was invisible. The boundary is before it now, and what is
measured is the whole cost of showing N rows. Sixth time today that a green
perturbation meant a missing observable rather than a passing test.

**And a pre-existing defect that is not this increment's**, found because the
fixture needed two turns of Qt's loop and hung with no output. `qt.run()`
called from inside a **function** silently starves every loop-delivered source:
`watcher_trigger_change` returns at once unless the **global frame** is
current, so a function frame left current for the whole of Qt's loop means
every timer tick is appended to `timer.ticks` and never delivered. The timer's
own schedule advances, so it looks like it is working. Traced through
`pump_arm`, the loop's early return and `timer_loop_service` to that one guard;
it reaches timers, the actor inbox, `http` events and a webserver's requests,
all of which go through it. Recorded rather than fixed — the guard is there
because a watcher body must run in the global frame, so the repair is to make
the pump establish that frame for the drain, which is a change to scope
handling and its own increment.

### Still open

- **Click-to-sort** is off, as it is for a held table, and here the reason is
  stronger: a sorted view would make `qt.selected`'s index mean a different row
  than the data has, and the data is somewhere this side never sees.
- **`qt.refresh` on an asking table** does nothing; the count is the refresh.
  Whether it should be an alias is a question nobody has needed answered.
- **A per-row kind** — a cell answers whatever the callback returns, rendered by
  the one renderer, and nothing lets a column declare `type: {USD}` the way a
  form's field can.

## 0.26 A rule, and a menu under a button (2026-10-19)

The last two things Studio actually uses that the vocabulary could not express
— and they were found by **counting its whole widget surface** rather than
working from my gap list, which is the method §0.19 arrived at and which keeps
paying.

### The survey, because the gap list was wrong in both directions

Every `gtk.*(` and `gi.new("Gtk.*")` in Studio's `app/` and `lib/`:

| Studio | uses | Qt | status |
|---|---|---|---|
| `gtk.label` | 51 | `text` | §0.10 |
| `gtk.button` | 25 | `button` | ✓ |
| `gtk.box` | 25 | `row` / `column` | ✓ |
| `gtk.scrolled` | 5 | `scroll` | §0.19 |
| `gtk.application_window` | 5 | `window` | ✓ |
| `Gtk.CssProvider` | 4 | `qt.theme`, `styleSheet` | §0.7 |
| `gtk.paned` | 3 | `split` + `qt.resize_panes` | §0.19, §0.22 |
| `gtk.listbox` | 3 | `table` | §0.6 |
| `Gtk.StringList` | 3 | `choice` options / `qt.options` | §0.13 |
| `Gtk.Popover` | 3 | `popup` | §0.20 |
| `Gtk.Grid` | 3 | `cells` | §0.8 |
| `Gtk.DropDown` | 3 | `choice` | §0.13 |
| **`Gtk.Separator`** | **2** | — | **this section** |
| `Gtk.GestureClick` | 2 | the context signal + `qt.right_click` | §0.20 |
| `Gtk.Entry` | 2 | `field` | ✓ |
| `gtk.notebook` | 1 | `tabs` | §0.8 |
| `Gtk.TextBuffer` | 1 | `memo` / `source` | ✓ |
| **`Gtk.MenuButton`** | **1** | `popup` + a handler | **this section** |
| `Gtk.EventControllerScroll` | 1 | `on <part>.wheel` | §0.21 |
| `Gtk.EventControllerKey` | 1 | a `QAction` shortcut | §0.21, measured unnecessary |
| `Gtk.CheckButton` | 1 | `check` | ✓ |

§0.19 already recorded that Studio uses **none** of the toolbar, status bar,
progress bar, slider, spin box or date picker I had listed. Counting the whole
surface found the error in the other direction too: two things that are small,
**used**, and absent.

### A `separator` in a layout

The word already existed and the validator refused it outside a menu — so a
horizontal rule between panes, and a vertical divider between two groups of
toolbar buttons, could not be written. Studio has both (`studio_shell._rule`
and `._bar`), and its own comment says the horizontal one *"is the line that
says where one pane ends and the next begins"*, so it is a visual element
rather than decoration.

**One word, one meaning, two widgets.** A separator means *a line that
divides*, which is as true of a rule between sections as of a line in a menu.
The validator already carries the parent's word, so one word covers both and
the backend picks the widget.

**The orientation comes from the parent**, as `stretch` takes its axis from
one, and that is load-bearing rather than tidy:

| measured | result |
|---|---|
| `HLine` in a **column** | **378 × 3** — a rule |
| `VLine` in a **row** | **3 × 78** — a divider |
| `HLine` in a **row** (wrong way round) | **119 × 3** — a stub taking a share of the width |
| `QBoxLayout::direction()` | 2 for a column, 0 for a row — so the parent knows |

An option whose only correct value is the one the parent already knows would be
an option to get wrong. Refused where there is nothing to divide: a splitter
pane already has a draggable handle, and a `cells` grid has no direction.

**And it may be named**, where a menu's separator may not. Not a loosening for
symmetry: a rule is a real widget with real properties and **Studio styles its
own** (`studio_style.apply(sep, "rule")`), so one that could not be addressed
could not be themed. It is also the only way to observe the orientation —
`qt.read(win, name, "frameShape")` — which is the one thing that can be built
wrong.

### A menu under the button that opened it

`qt.popup` takes an optional third argument naming a part to pop beneath, which
closes §0.20's own open item (*"at the pointer only"*). Studio is the caller:
it builds a toolbar menu button, and a menu button's menu belongs under the
button rather than wherever the mouse happens to be. Measured, popping at a
widget's bottom-left lands exactly there.

**And the fifth green perturbation of the day produced its oracle.** Ignoring
the third argument and popping at the pointer left `qt.popup`'s boolean `true`
and was caught by nothing — the boolean says only that a menu appeared. A
menu's own `x` is in a different coordinate space from a button's, so comparing
them directly would be comparing two unrelated numbers; what is honest is a
**difference**: popping under one button and under another puts the menu in
different places, with the pointer as a third.

That makes five increments today where the question *"what would be observable
if this did nothing?"* decided a design detail — `qt.right_click`,
`qt.scrolled`, `qt.has_icon`, a nameable rule, and this.

**And a usage note the example found rather than any check.** A popped menu
**stays up** until something dismisses it, and a visible `QMenu` grabs the
mouse — so adding a menu button to `examples/qt/shell.bas` broke the Ctrl+wheel
check two tiers below it, with the menu still open and swallowing the wheel. A
person clicks an item or presses Escape; a program driving itself calls
`qt.invoke(win, menu, "hide")`. Auto-dismissing was considered and rejected
outright: a menu that vanished the instant it appeared would be no menu at
all.

### Still open

- **`grow` as a bare option** is an option with no value, which is new grammar in
  the head; `grow: true` avoids it and reads slightly worse.

- **Qt Charts' licence** is unverified locally (§3).
## 0.27 A container whose children change (2026-10-19)

**Asked for by the consumer, with measurements** — `docs/qt_dynamic_children_proposal.md`,
written by the gBASIC Studio side from converting its shell. Everything that
window needs existed except one thing: **a container whose set of children
changes while the window is up.** `qt.refresh` answers it for a `table` (§0.11,
written for Studio's file tree by name) and for a form's subject, and nothing
answered it for any other container. Studio's notebook is one tab per open
document, which is not a corner case — it is what the application is.

### What the proposal established, and what it left open

It measured the shape of the gap rather than describing it: `qt.read(win,
"book", "count")` answers 2 for a two-tab `tabs`, so the set is observable;
`qt.refresh(win, "book")` refuses, naming tables; and `qt.invoke` passes **no
arguments**, so `QTabWidget::removeTab(int)` and `setTabText(int, QString)` —
the two calls that would change a tab set or a caption in place — are out of
reach by construction rather than by omission. That last one is a deliberate
shape of `qt.invoke` and not something to patch there.

It also measured the **workaround and why it is not enough**: a generation loop
(close, rebuild the record, open, run again) carries the buffers, the active
tab, the geometry and the pane sizes, and loses two things — per-document undo
history, and **the window's position on screen**, since `x`/`y` are read-only
on a `QWidget` and the record takes `width`/`height` only. A window that jumps
across the screen when you open a file is worse than any feature it buys.

### Six measurements on Qt 6.10.2, and three decide the design

| asked | answer |
|---|---|
| does `removeTab` DELETE the page? | **no** — the widget is alive and still parented |
| a kept `QPlainTextEdit`'s text and undo across removeTab + addTab | **both survive** (`isUndoAvailable` 1) |
| `setTabText(0, ...)` | changes a caption **in place** |
| `tabBar()->moveTab(2, 0)` | reorders without rebuilding |
| a box layout's `removeWidget` | widget alive, **still parented to the box** |
| …and still **visible** | **YES — it floats** at its old geometry |

**The second is the whole point of the verb**: a child that is kept really does
keep what was typed into it, so a reconcile is worth building rather than a
rebuild being dressed up.

**The sixth is the trap, and it is this backend's own failure class arriving
from Qt's defaults.** A child dropped from a layout and not deleted STAYS ON
SCREEN — so "absent" cannot mean "removed from the layout", it has to mean
deleted, and the name has to leave `parts` with it or the registry is left
pointing at freed memory (§0.17's rule for a closed window, one level in).

### What was built

`qt.refresh(win, name, items)` — a third form of the verb, which is the
proposer's own recommendation and the rule §0.11 already took (*"One verb takes
either rather than growing a second"*). `items` is exactly what `items:` holds
in the record `qt.open` read, so a caller that already builds its window as a
record (§0.9) has nothing new to construct.

**The reconcile is by NAME and WORD.** A child whose name and word are
unchanged is KEPT, with its widget, its text, its caret and its undo stack; a
child that is new is BUILT through `qt_build_item`, the same path `qt.open`
uses, which is what stops a window rendering one way at open and another
afterwards; a child that is absent is DELETED and its name purged from the
parts registry; and a `tab`'s caption is re-read, because it is data in the
record.

**A `tab` carries a NAME in the record, which the declaration cannot spell**,
and that is the one rule this increment adds rather than reuses. §0.8's one-slot
rule is a fact about the GRAMMAR — a `tab` spends its slot on the caption — and
not about the record, which is what the builder already rests on. The reason a
name is needed at all is sharper than symmetry: **the caption is the thing that
changes.** Studio's tab labels carry a `* ` unsaved marker, so a reconcile keyed
on the caption would rebuild the editor on the first keystroke in any document,
which is precisely the cost the verb exists to remove. Keyed on `name` where the
record gives one, on the caption where it does not.

### What it does not do, said rather than discovered

- **It does not recurse into a kept child.** A kept child keeps everything,
  including its own children. A nested reconcile is a different verb's problem
  and nothing has asked for one.
- **It admits `tabs`, `column`, `row` and `group` only.** A `split` cannot be
  named at all (§0.19: its one slot is the direction), and a `cells` grid has
  DECLARED positions, so reconciling one would have to invent them. Both are
  refused by name with the reason, rather than silently doing nothing — which
  is what a verb that ignored an unknown container would do.
- **`qt.tabs(win, name)`** answers a `tabs`' captions as a list, because
  `QTabWidget::tabText(int)` takes an argument and `qt.invoke` passes none, so
  without it a caption is unobservable and every check about one would pass on
  an implementation that never set it. The `qt.menus` shape, for the reason
  recorded there.

### Three defects found by building it, none visible by reading

**A named `tab` never reached the registry.** `gb_qt_container`'s tab branch
returns early — deliberately, so a page is not also added to the parent's
layout — and that early return skipped the name registration twenty lines
below. Invisible until something asked a container for a child BY NAME: the
reconcile could not recognise any page, so it rebuilt every one, and the editor
came back holding the record's text instead of what had been typed into it —
precisely the defect the verb exists to prevent, arriving from the one door
that returns before the common path.

**A child replaced because its WORD changed was dropped twice.** The first drop
succeeds and nulls the node's widget; the prune pass then finds the same node
still in its "was there" list and refuses, with a message blaming the record. A
replaced child leaves that list now. Same shape as §0.18's idempotence guard,
where a close reached `node_closed` twice.

**And a use-after-free in `qt.read`, which is older than this increment.**
Reading a dropped child's name is the first thing in the tree that ever asked
`qt.read` for a name that is not there, and valgrind named it on the first run:
the refusal formats `which.as.string` AFTER `value_free(which)`. §0.22's own
defect, one door along — *"a use-after-free that showed up as `sizes` silently
never being set"* — and it survived because **a refusal path nothing exercises
is a refusal path nothing checks.** A sweep of the whole module found exactly
one other straight-line instance of the shape, already guarded by the comment
§0.22 left behind.

### Two perturbations went green, and that is the finding

Six of eight reddened the check written for them. The two that did not are
worth more than the six:

**Replacing the `delete` with a `hide()` leaves every check green**, because the
name is purged either way and a widget nobody can name is unobservable from the
language — the leak that would reveal it is excluded by the Qt valgrind policy
(access-only, Qt not freeing its own platform plugin or QApplication statics).
So that claim is held by a SOURCE TRIPWIRE with a vacuity control, which is the
arrangement §0.14 reached for the syntax-definition path and for the same
reason.

**Skipping the NAME PURGE also leaves every check green** — `qt.read` on a
dropped name happens to refuse — and valgrind reports **21 invalid reads**. That
half is covered after all, by the tier whose stated reason in the suite is
exactly this: a stale `QObject *` in the registry is a use-after-free that a
correct answer would not reveal. The two halves are proven by different tiers
rather than both resting on one.

### And the naming rule widened by its own justification

`qt.refresh` cannot address what cannot be named, and a `column`, `row` and
`group` could not be named — refused on the grounds that a box has no state
worth asking about. **The verb gives every one of them some: the set of
children.** So the rule did not change, its consequences did; the refusal
message now states the rule (*a container is named when something addresses
it*) rather than enumerating four words, which is the third time this
vocabulary has been written out by hand and the reason `win_nearest` derives
its suggestions from the tables.

A `form` is still refused a name (§0.6: its parts are addressed by their own
names), and so is `cells`, whose positions are declared.

## 0.28 A `tree` (2026-10-19)

**The user asked for this by name** — *"a better looking tree view on the
left"* — and it is the first of the two asks in
`docs/qt_studio_requests.md` that block a feature Studio already has. Its file
browser is the left third of its window and the thing a person looks at most
after the code; under GTK it is a `Gtk.ListBox` of hand-built rows (a glyph
label pinned to one character, a margin per depth level, an ellipsizing name),
and in this vocabulary it had to become a `table` — the only widget here whose
row count could change without rebuilding the window. That works, and it looks
like a spreadsheet pretending to be a file tree.

### Measured first, and it moved the work rather than the estimate

| asked of Qt 6.10.2 | answer |
|---|---|
| the chrome the request calls missing | **ten Q_PROPERTYs**: `headerHidden`, `rootIsDecorated`, `itemsExpandable`, `indentation`, `animated`, `alternatingRowColors`, `uniformRowHeights`, `expandsOnDoubleClick`, `allColumnsShowFocus`, `sortingEnabled` |
| `showGrid` on a tree | **absent** — there are no grid lines to turn off |
| a flat list with a depth column | builds the tree correctly; `src > lib > util.bas` reads back |
| the current item's flat index | recoverable by walking the iterator |
| after collapsing a parent, Qt's `NotHidden` iterator | **counts every row** — a collapsed child is not `isHidden()` |

**The request's own note is corrected in the helpful direction**: header
visibility *"is not a `Q_PROPERTY`"* is true of `headerVisible` and false of
`headerHidden`. So what `tree` adds is **the widget and the depth**; the look
was always one `qt.set` away, and the fixture asserts that rather than assuming
it.

**And the last row decided the semantics.** There is no free "visible index",
so an index has to mean something else: **the rows the program handed over**. A
pre-order walk reproduces that order by construction — each row is attached
under the last row one level shallower — so the index needs no bookkeeping of
ours to stay meaningful however the tree is expanded, and `qt.selected`,
`qt.cell` and `qt.depth` all speak it.

### The spelling costs no grammar

`tree files to show rows` is the production `table orders to show lines`
already uses (§0.6), so bison reports the same zero conflicts. The depth is a
**column that is not drawn**, declared `field depth( depth: true )` — the
alternative being a convention (*a field called `depth` is the depth*), which
is magic a reader cannot see where an option says it. At most one, because two
would be a declaration with two answers; **none is legal**, being a flat list,
which is exactly what a program that owns its own expansion hands over.

A tree is **`to show`** and nothing else. `to edit` would have to write a value
back from a row that may be a PARENT, and `to ask` is a table's third
direction for a million rows a program cannot hold, where a tree's shape comes
from depths it has in hand. Refused by naming the one it has, which is what
stops a direction spreading to every word that carries a subject because one of
them wanted it.

### `qt.depth` is the oracle, and it is the fourth of its kind

A populate that **ignored the depth** builds a flat tree whose every name,
index, selection, heading and refresh reads back correctly — so nothing else in
the fixture can see it. Proven: that perturbation reddens the three depth
checks and nothing else. It answers the depth **the widget has**, walked up the
item's parents, rather than the number the record carried; and it refuses "that
is not a tree" separately from "there is no such row", which is the distinction
`qt.selected` already rests on.

That makes four increments in a row where asking *what would be observable if
this did nothing?* produced a verb — §0.20's `qt.right_click`, §0.21's
`qt.scrolled`, §0.23's `qt.has_icon`, and this.

### One declaration rule needed a new accessor, and one perturbation needed a check

The checker deliberately does **not** police a leaf's options — they are widget
properties, and a QPushButton exposes 75 — so `depth:` is the first option a
DECLARATION RULE turns on, and the accessor model gained `option_true`,
answered by both adapters (§0.9's one checker, two adapters). It reports only a
literal `true`, because a rule can rest only on what is declared.

**And drawing the depth column changed nothing observable**, which a
perturbation showed: cells are read by field name and the first heading is
still `File`, so the only difference is that a second column exists. Asking for
one is now the check — and it exposed a refusal naming the wrong cause, since
`qt.heading` answered *"that column has no caption"* for a column that is not
there. `gb_qt_column_count` exists so the two can be told apart.

## 0.29 A margin on `source`, carrying a mark per line (2026-10-19)

The second of the two asks `docs/qt_studio_requests.md` names as blocking a
feature gBASIC Studio already has — §0.28's `tree` was the first, and that
document's own words are *"Two of these are gaps that block a feature Studio
already has."*

**It is not a ruler, and the user said so twice.** On first hearing the word
their instinct was *"it's not just line numbers"*, and on reading this section's
plan they named what they had originally left out: *"I didn't think the gutter
was essential because I was only thinking line numbers and forgot about break
points and other things."* That is the whole argument for the shape: a margin
that could only number lines would have to be rebuilt the first time anything
else wanted to sit in it.

### The category set is derived rather than chosen

Which is what lets it be **closed**, unlike a leaf's properties (75 on a
button, §1's own argument for the by-name route). A category decides a **glyph**
and a **priority**, and the backend has to know both, so an unknown one is
refused by name with a near-miss hint through the same matcher the chooser's
options use (§0.16).

| category | where it comes from | priority |
|---|---|---|
| `note` | `GB_SEVERITY_NOTE` | 1 |
| `section` | Studio's ask — `▶` at each runnable section's start | 2 |
| `warning` | `GB_SEVERITY_WARNING` | 3 |
| `error` | `GB_SEVERITY_ERROR` | 4 |
| `break` | the user's ask — a breakpoint | 5 (wins) |

Two were asked for and the other three **are** gBASIC's own diagnostic
severities: `gb_severity` is exactly `{error, warning, note}`, so a program
showing diagnostics has one category per severity instead of flattening three
into one. Nothing here is invented.

**The enum order IS the priority**, so there is no second table to drift out of
step with the first — and the ordering is two rules rather than a preference. A
mark the **person** placed outranks one the program **computed**, which is why
`break` wins everything: a breakpoint is the one mark somebody set deliberately
and must be able to see in order to remove, and hiding it behind a computed
error would make it unclickable for a reason nothing explains. Among computed
marks the more urgent wins, with `section` lowest but **above** `note` — which
preserves the priority Studio's own request states (section 1, error 2 "wins")
and keeps the run affordance from being hidden by the least urgent thing there
is.

### Line 1 is the first line, and that was measured

The ask's example reads `qt.mark(win, "code", "section", [0, 12, 31])`, which
looks 0-based. Measured, both of the consumer's own sources are **1-based**:
`source_outline` reports `start_line = 1` for the first line, and a parse
diagnostic carries a 1-based line. The gutter displays 1-based too, as every
editor does and as gBASIC's own `line:column` diagnostics already assume.

**So there is no conversion anywhere**, which is the point: a conversion is
exactly where an off-by-one would live, and an off-by-one here puts a mark one
line away from the error it is pointing at — which reads as a margin that
works. Line 0 is **refused** rather than dropped, because a 0-based caller is
the plausible mistake and a silently dropped mark would report success while
drawing nothing.

### Replace, never add or remove

Which is the consumer's shape rather than a simplification: Studio recomputes
the whole list on every redraw, and the GTK version removes every mark in the
range before adding. An add/remove pair would make it keep a record of what it
had already drawn — a cache to get wrong, and it already has two of those. An
empty list clears a category, which falls out of replacing rather than needing
a verb.

**`qt.marks` answers what was SET, never what is drawable.** A program may mark
line 40 and then shorten the document, and a read filtered to the lines still
in the file would disagree with the write for a reason the caller cannot see —
making `qt.mark` then `qt.marks` non-idempotent. The drawing is the widget's
business; the list is the program's.

### What was measured before any of it was written

| asked | answer |
|---|---|
| can the gutter be a plain child `QWidget`? | yes — `paintEvent` is a virtual |
| does it need `moc`? | no; the two connections are the functor form reaching a lambda. **And a moc pass already runs on this file**, which is the stronger fact — my earlier note said "no moc needed" |
| what must a subclass reach? | `firstVisibleBlock`, `blockBoundingGeometry`, `blockBoundingRect`, `contentOffset`, `setViewportMargins` — **all protected**, which is why Qt's own Code Editor example subclasses |
| is the margin really reserved? | yes — viewport x = 29 for a 28px margin |
| does scrolling drive it? | yes, through `updateRequest`, which fires on scroll as well as on an edit |
| does anything paint without a turn of the loop? | **no** — a bare `repaint()` painted 0 gutter lines; `update()` plus `processEvents()` painted 24 (§0.25's lesson, measured again) |
| are the glyphs available with no icon theme? | all five standard pixmaps measured non-null 16×16 on a machine with **no icon theme at all** |

**Two of those changed the implementation.** The protected members force the
subclass, which is why a `source` is now a `GbQtSourceEdit` and a `memo` is
still a bare `QPlainTextEdit` — not an oversight but what makes `qt.mark` on a
memo a **refusal** rather than a silent no-op. The refusal is made with
`dynamic_cast`, which knows the real type without a metaobject.

**And the reason first written down for that was wrong**, which a perturbation
established rather than review: the comment claimed that because the subclass
declares no `Q_OBJECT` its metaobject *is* `QPlainTextEdit`'s, so
`qobject_cast<GbQtSourceEdit *>` would silently succeed for a memo too.
Measured, **it does not compile** — Qt 6 answers with a static assertion,
*"qobject_cast requires the type to have a Q_OBJECT macro"*. So the wrong code
cannot ship, which is a better state of the world than the one described, and
the hazard was overstated. The refusal is still load-bearing: a memo and a
source are both `QPlainTextEdit`s, so an implementation that took the parts
entry at face value accepts a memo and quietly does nothing to it — the
perturbation that drops the check reddens the memo refusal and valgrind
together.

**That perturbation also exposed a trap in the perturbation workflow itself**,
which is worth more than the correction: with `qobject_cast` in place the build
FAILED, the suite's own `make` was unchecked, and it went on to run the
**previously built binary** and report green. `run_all.sh` guards its up-front
build (line 166), so the gate is safe; a suite run **by hand** had nothing, and
that is exactly how a perturbation is exercised. This suite checks the build
now.

### The glyph is not configurable, and that is the §0.23 measurement again

The ask proposed `SP_MessageBoxCritical` or a shipped file, *"both better than a
theme name here"*, and the reason it says so is worth keeping: Studio ships its
own PNG because `dialog-error` and `dialog-error-symbolic` are both standard
freedesktop names and **neither resolved** — this install's Breeze has the first
and not the second, its Adwaita the second and not the first, and what GTK drew
was a grey missing-icon disc wide enough to sit on top of the code.

So four of the five glyphs come from **QStyle standard pixmaps**, which are part
of Qt Widgets and need no theme, and `break` is **painted** — a filled circle,
which is what every debugger draws and which needs neither style nor font, the
one glyph here with no dependency at all. A font glyph like `U+25B6` was
rejected for the same class of reason: it needs a font that happens to carry
it, and Qt substitutes in silence.

**That removes the reason the ask mentioned icons at all.** A program cannot
change a glyph, which is a stated cost rather than an oversight; if a real need
appears, that need is the argument.

### The click, which is why `break` is in this increment

A breakpoint you cannot click to set is decorative. `on <part>.gutter` is the
**third event rather than a signal** here, after `closed` (§0.17) and `wheel`
(§0.21), and for the same reason: the gutter is a plain child widget and emits
nothing, so the by-name route would refuse a well-formed handler with *"a
QPlainTextEdit emits no `gutter` signal"*.

It needed **no new handler shape**, which is what made it affordable: §0.21
already established that a handler here takes no arguments and the event is
*asked for* instead, so `qt.gutter_click()` answers `{ line }` exactly as
`qt.wheel()` answers the turn. It answers **`unknown`** outside a handler,
which is a different claim from *line 0*.

**The filter sits on the gutter itself**, so an ordinary press in the text is
still an ordinary caret move — and it is installed **only where a handler was
declared**, the reach being the declaration's and nothing wider (§0.21's rule,
and the control that catches the over-correction is a `source` with no handler
firing none). The line is asked of the document through `cursorForPosition`
rather than computed from a y coordinate and a line height, because a hidden
block makes those two disagree; the x is pushed into the text area, since at the
gutter's own x the cursor would clamp to the first line whatever was clicked.

`qt.gutter_press(win, part, line)` is what makes any of it drivable — the
`qt.right_click` (§0.20) and `qt.scroll` (§0.21) shape, in the product surface
for the reason stated there rather than as a test hook. A line **not on screen**
is refused, because a mouse cannot land there.

### What a check could observe if this did nothing

The question that produced five verbs in §0.20–§0.26, and here it has an
uncomfortable answer: **almost nothing.** `qt.marks` answers what was set, so a
backend that stored five lists and painted nothing satisfies every check the
language can make — and a `QWidget`'s pixels are not reachable from gBASIC at
all, unlike a `QTextLayout`'s format ranges, which is what made §0.14's colour
claim checkable.

So the claim is split, and both halves are necessary:

- **`qt.gutter(win, part)`** answers `{ line_numbers, marks, width }`, or
  **`unknown`** for a `source` with no margin — the distinction `qt.pane`,
  `qt.dialog` and `principal()` all make, since `width: 0` would say a margin
  exists and is empty. The `width` follows the **digit count**, which is the one
  part of the reservation gBASIC can see: a 1200-line document reserves more
  than a 12-line one, and a gutter that ignored the count passes every presence
  check and fails that.
- **`tests/qt/gutter_probe.cpp`** makes the painting claim, and it **links the
  real backend object** rather than reimplementing the setup — the difference
  from `tests/qt/source_probe.cpp`, and strictly stronger, since what it
  measures is the painter the product runs. Three things make its numbers
  evidence rather than constants: the counters are **zero before any turn of the
  loop**, it paints **13 of 200** lines (so it walked real block geometry rather
  than looping the document), and six marked lines of which one carries **two**
  categories paint **six** glyphs — seven would mean the priority rule is not
  applied.

Its first run measured four glyphs rather than six, and the fixture was at
fault: the shared line sat at 20 with thirteen lines visible, so the priority
case was **below the fold**. A check whose subject is off-screen measures
nothing, which is recorded here because the number looked plausible.

**Two further perturbations went green, and each produced an observable** — the
seventh and eighth time in this backend that has happened, and both were
invisible for the same reason: *every check was taken at the top of the
document*, where a margin that never repaints still looks right.

- **The gutter never follows a scroll.** Removing the `updateRequest` connect
  leaves the numbers sitting still while the text moves under them, which is
  the whole reason that signal is the one that matters — and all 30 semantic
  checks and all four paint checks stayed green. What changes when the view
  moves is the first line painted, so the probe scrolls to the bottom and
  requires the margin to have repainted from line 190.
- **The display is 0-based.** Every number in the margin is one out, which is
  the same off-by-one the mark API was designed to have no room for. The
  recorded first line is therefore **the number the painter drew**, not a
  recomputation from `firstVisibleBlock()` — which would agree with a 0-based
  painter and say nothing.

Ten perturbations proven red in all, each caught by the check written for it.

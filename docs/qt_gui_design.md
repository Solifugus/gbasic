# A Qt GUI for gBASIC — design

**Status: largely shipped.** The `window` declaration (§0.2), the `qt` module
(§0.4), binding (§0.5), `table` (§0.6), menus and theming (§0.7),
`tabs`/`cells` (§0.8), the builder (§0.9), `text`/`memo` (§0.10) and
`qt.refresh` (§0.11) and `choice` (§0.13) exist, with §0.12 a whole application
built on them, gated by
`tests/run_window.sh`, `tests/run_qt_backend.sh` and
`tests/run_qt_builder.sh`. Still proposal: `stdlib/qt.bas`, `show:`/`edit:` in
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
highlighting is gateable without a display. What remains for Studio is widgets
and the data grid, which is ordinary work rather than an open question.

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

### Still ahead of it

BAG's **editor** — the menu-driven half — is form-heavy, and an area's six
exits each name another area while an item names its location. Those want a
**dropdown**, which is the one widget with no substitute (§0.10). And teaching
BAG to *emit* a `window` block, so the generated game is a GUI, is a separate
exercise: a program writing a declaration.

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

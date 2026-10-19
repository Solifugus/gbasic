# examples/qt

Qt GUI examples. These need gBASIC built with Qt 6 and `moc`; without them
every `qt.*` verb but `qt.can_display()` raises a clean runtime error, and each
program prints what is missing rather than failing.

**Every one takes `check`.** `./gbasic examples/qt/layout.bas check` prints what
the program can observe about its own window and exits, instead of waiting for
somebody to close it. That is how to verify a Qt install without looking at a
screen, and it is how `tests/run_qt_examples.sh` gates these — until it existed
they were only *parsed*, and an example can rot while every fixture passes.

The first seven take one aspect each; the last four combine them.

| | |
|---|---|
| `layout.bas` | **the four containers, and nothing else** — no data, no binding, no table, so what the screen shows is the layout. A `row` packs left to right and `stretch()` is pure space; a `cells` grid ALIGNS columns across rows and `across:` spans them, which a box cannot do. It also prints the honest limit: only a grid has coordinates to answer `qt.at` with, and it shows that a labelled `field` takes **two** cells — its label and itself |
| `tabs.bas` | **a book of pages.** A `tab` needs a caption and takes no name; the `tabs` IS named, having state a handler can ask about. A page gets a column layout, which is why a grid and a whole `table` work inside one with no further vocabulary — and why `movable:` is set with `qt.set` at run time: **a container carries a name or options, not both** |
| `properties.bas` | **the by-name route**, which is what makes this backend small: `QPushButton` alone exposes 75 properties, so anything the backend does not own goes to the widget's own `QMetaObject`. Three doors that must agree about what a name is — the declaration, `qt.read`, `qt.set` — with a misspelling refused by name at each, because `setProperty` on an unknown name SUCCEEDS in Qt and would silently do nothing |
| `theme.bas` | **one stylesheet for the application, and a widget that overrides it.** The theme half **needs no display at all** — a sheet set before any window exists is remembered and applied when there is a `QApplication` to put it on — so this is the one example whose first half runs on a text-only machine |
| `clock.bas` | **Qt owns the thread and gBASIC is still serviced** — §5.1's architectural claim as something you can watch: a `timer.every` keeps firing after `qt.run()` has taken the thread, and an ordinary gBASIC watcher writes into three widgets. It teaches two things learned by getting them wrong: the `watch` goes INSIDE `program` (at the top level beside one it registers nothing), and **`qt.stop()` does not cancel your timers** — leave a repeating one live and the program never exits |
| `menus.bas` | **a bar, submenus, separators, shortcuts, and items that know whether they can be used** — and the aspect that cost **no grammar at all**, measured before a line was written: `menu "File"` is `group "Credit"`'s own production, `item` is the named leaf, `separator()` is `stretch()`, and a submenu is the same production once more. An item is a `QAction` rather than a widget, so `shortcut:`, `checkable:` and `enabled:` arrive through the by-name door with nothing plumbed — and `qt.menus` is the only thing that can witness the **structure**, a by-name registry being flat while a bar is a tree |
| `editor.bas` | **a code editor** — the one piece gBASIC Studio needs that nothing else here provides, and a word rather than a property because a highlighter is an *object attached to a document*, which no by-name property lookup can reach. It prints the four differences from a `memo` beside a `memo` (no wrapping, a four-column tab, a monospaced font, a highlighter) so they are facts rather than claims; it loads its own source, so the text is real gBASIC; and it prints what is **not** there yet — a line-number gutter, which Qt has no widget for, and completion, whose data already exists in `source_outline` and `src/lsp/` while the popup is another attached object |
| `invoice.bas` | **a window built from data** — one row per invoice line, which is the one thing a `window` block cannot write, a declaration having no loop in it (§4). There is no second API: the block binds a RECORD, and `qt.open` asks only that a record carry `kind: "window"`, so building that record is the other door. `qt.check` gives it the declaration's own load-time rules, and needs no display |
| `customer.bas` | the form §2 of `docs/qt_gui_design.md` draws: a `form to edit` bound to a record **by name**, a titled group, and two buttons pushed right by a `stretch`. Nothing populates a widget and nothing reads one back part by part — the form takes `cust` from the frame that opens it, and the handler asks `qt.subject` for the whole record, with `limit` still money because the field declares `type: {USD}` |
| `orders.bas` | **a list and the record under it** — a `table` feeding a `form`, the commonest shape a business screen takes and the one §7 designed `table` for. Nothing wires a widget: the table reads `.total` from every element of `lines` and the form reads `chosen.who`, both by name, so the program only answers *which row* and refills |
| `dialogs.bas` | **the one you can sit in front of and click** — a statement, a question, all three file choosers and a window shown modally, each behind a button. It exists because everything dialog-shaped in this tree is driven by a TIMER so a suite can answer it with nobody there, and the consequence was that none of it could be LOOKED AT: the cookbook recipes answer themselves in about fifty milliseconds and exit. Its two modes raise their dialogs from different places, which is the measured limit rather than an accident — one opened from `main` leaves gBASIC's sources running so a `timer` can answer it (`check`), while one opened FROM A HANDLER suspends every source until a person answers (the buttons). So `check` deliberately does **not** exercise the buttons, and says so |
| `notes.bas` | **a menu bar, a theme and both of the above** — `menu "File"` with items, separators and a submenu; `qt.theme` applying one stylesheet to the whole application; and a `table` beside a `form`. The widest surface in one screen |

**Running them without a display.** The whole of this works on the offscreen
platform, which is what `tests/run_qt_backend.sh` uses:

```sh
QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/customer.bas
```

That is not a test-only arrangement — it is how a GUI gBASIC program renders on
a headless server.

**Nothing here is manual.** `tests/run_gui_parse.sh` parses every program in
this directory on every gate run, and `tests/run_qt_examples.sh` **runs** each
one offscreen in `check` mode, then again with no display at all — so these
examples cannot rot silently the way a display-gated one would. Parsing alone
catches a syntax error and nothing else, which is the gap that suite closed.

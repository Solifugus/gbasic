# examples/qt

Qt GUI examples. These need gBASIC built with Qt 6 and `moc`; without them
every `qt.*` verb but `qt.can_display()` raises a clean runtime error, and
`customer.bas` prints what is missing rather than failing.

| | |
|---|---|
| `invoice.bas` | **a window built from data** — one row per invoice line, which is the one thing a `window` block cannot write, a declaration having no loop in it (§4). There is no second API: the block binds a RECORD, and `qt.open` asks only that a record carry `kind: "window"`, so building that record is the other door. `qt.check` gives it the declaration's own load-time rules, and needs no display |
| `customer.bas` | the form §2 of `docs/qt_gui_design.md` draws: a `form to edit` bound to a record **by name**, a titled group, and two buttons pushed right by a `stretch`. Nothing populates a widget and nothing reads one back part by part — the form takes `cust` from the frame that opens it, and the handler asks `qt.subject` for the whole record, with `limit` still money because the field declares `type: {USD}` |
| `orders.bas` | **a list and the record under it** — a `table` feeding a `form`, the commonest shape a business screen takes and the one §7 designed `table` for. Nothing wires a widget: the table reads `.total` from every element of `lines` and the form reads `chosen.who`, both by name, so the program only answers *which row* and refills |
| `notes.bas` | **a menu bar, a theme and both of the above** — `menu "File"` with items, separators and a submenu; `qt.theme` applying one stylesheet to the whole application; and a `table` beside a `form`. The widest surface in one screen |

**Running them without a display.** The whole of this works on the offscreen
platform, which is what `tests/run_qt_backend.sh` uses:

```sh
QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/customer.bas
```

That is not a test-only arrangement — it is how a GUI gBASIC program renders on
a headless server.

**Nothing here is manual.** `tests/run_gui_parse.sh` parses every program in
this directory on every gate run, and `tests/run_qt_backend.sh` drives the
whole surface offscreen, so these examples cannot rot silently the way a
display-gated one would.

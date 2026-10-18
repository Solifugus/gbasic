# examples/qt

Qt GUI examples. These need gBASIC built with Qt 6 and `moc`; without them
every `qt.*` verb but `qt.can_display()` raises a clean runtime error, and
`customer.bas` prints what is missing rather than failing.

| | |
|---|---|
| `customer.bas` | the form §2 of `docs/qt_gui_design.md` draws: a `form to edit` bound to a record **by name**, a titled group, and two buttons pushed right by a `stretch`. Nothing populates a widget and nothing reads one back part by part — the form takes `cust` from the frame that opens it, and the handler asks `qt.subject` for the whole record, with `limit` still money because the field declares `type: {USD}` |

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

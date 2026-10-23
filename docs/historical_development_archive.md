# Historical Development Archive

This document consolidates completed development plans, progress trackers, and
superseded design notes that previously lived as separate files in `docs/`.
Git history retains the original documents when full implementation notes are
needed.

Current behavior is documented in:

- `README.md`
- `docs/project_state.md`
- `docs/reference.md`
- `docs/tutorial.md`
- the current module design documents

## GTK and the gi bridge, removed 2026-10-22

gBASIC's native GUI is Qt (the `qt` module and `window` declarations,
`docs/qt_gui_design.md`). The GTK stack it replaced was removed in one change on
2026-10-22, at Matthew's direction, once gBASIC Studio had moved entirely to Qt.
**The last commit containing it is `d997c94`**; check that out to read or run any
of it.

What went:
- the GTK 3 `gui` module and the `gi` GObject-Introspection bridge (GTK 4), with
  the `gobject`/`gboxed` value kinds and the `rowmodel` GListModel adapter;
- the `gtk`, `gtkui`, `gtkui_gtk`, `gui`, `sourceeditor` and `datagrid` libraries
  and `stdlib/gtksourceview/`;
- their examples, test suites, cookbook and tutorial, the native-application
  platform plan, and the `HAVE_GTK`/`HAVE_GIR`/`HAVE_GIO` build switches.

**Why `gi` went too, not just GTK:** measured across the tree and gBASIC Studio,
nothing used it for anything but GTK. Its 75 Gio/GLib test uses existed to test
the bridge headlessly. Its remaining reach (D-Bus, GStreamer and other Linux
GObject libraries) was potential, Linux-only and costly to keep correct; a
focused module, likely over Qt's own D-Bus or multimedia support, is the
recorded alternative if a real need appears.

## Early v0.1 Design Documents

The following early documents were superseded by the current core grammar,
reference, tutorial, and implementation:

- `gBASIC_design_updated.md`
- `gBASIC_v0_1_design_addendum_modifiers_errors_libraries.md`
- `gbasic_unified_design_spec_v_0_1.md`

They established the main direction for contextual modifiers, BASIC-style
errors, libraries, lock safety, arrays, records, dates, money, files, and
watchers. The current consolidated language design (which superseded the early
`gBASIC_v0_1_core_design_and_grammar.md` draft and the per-feature plan/progress
trackers) is `docs/gbasic-design.md`.

## Core Builtins

Completed phases added always-available strict core helpers:

- type inspection: `type` and `is_*`
- conversions: `number`, `boolean`, `array`, and `record`
- string helpers: `replace`, `starts_with`, `ends_with`, and `repeat`
- record helpers: `keys`, `values`, `has`, and `remove_key`
- `count`

Positive examples and exact negative diagnostics remain in the normal test
suites. Arithmetic coercion was not weakened.

Former tracker: `core_builtin_progress.md`.

## For-Each

Array iteration is complete for the current scope:

```basic
for each item in items
    ...
end for
```

The compatible `for item in items` form also remains supported. `continue`
advances to the next item, and `break` exits only the nearest loop. Direct
record iteration is rejected; callers can iterate `keys(record)` or
`values(record)`.

Former tracker: `for_each_progress.md`.

## File Operations

Five completed phases added:

- `delete`, `copy`, `move`, and `list_files`
- `make_dir` and non-recursive `remove_dir`
- non-truncating `overwrite`
- `join_path`, `file_name`, `directory_name`, and `extension`
- `read_lines`

The examples and negative suites retain coverage for argument validation,
missing files, invalid targets, non-empty directory removal, path extraction,
positioned writes, blank lines, and for-each iteration over file lines.

Former tracker: `file_operations_progress.md`.

## PostgreSQL

The synchronous core API is implemented:

- `pg.connect` and `pg.close`
- `pg.query` and `pg.exec`
- positional parameter arrays
- explicit transactions
- opaque connections
- row records and SQLSTATE diagnostics

Prepared statements, pooling, `COPY`, `LISTEN`/`NOTIFY`, and asynchronous
queries remain future work. Current details remain in
`docs/postgres_design.md`.

## WebClient Phase 1

Completed Phase 1 added the optional libcurl-backed module:

- `webclient.get`
- `webclient.post`
- `webclient.request`

It supports HTTP/HTTPS, explicit string request bodies, lowercase response
headers, additive JSON decoding, redirects, timeout handling, and transport
runtime errors. HTTP error statuses return normal response records.

The loopback integration runner is `tests/run_webclient.sh`.

Former tracker: `webclient_progress.md`.

## WebServer Phase 1

Completed Phase 1 added:

- `webserver.listen`
- `webserver.close`
- live `server.requests` and `server.responses` arrays
- watcher-driven queue processing
- request IDs and response matching
- query/header/body/JSON request records
- response defaults
- HTTP 504 timeout handling
- explicit and record-driven shutdown

The implementation is single-threaded, POSIX, and loopback-only. Current
details and future phases remain in `docs/webserver_design.md`; the integration
runner is `tests/run_webserver.sh`.

Former tracker: `webserver_progress.md`.

## GUI Watcher Integration

The Stage 5 review established deferred GUI mutation processing to avoid
running language watchers directly inside GTK callbacks. The implemented GUI
now queues GUI-originated changes, triggers normal watchers after GTK event
processing, and refreshes existing widgets after watcher execution.

Stage 6A is complete. Dynamic widget-tree mutation remains unimplemented.
Current status and future direction remain in `docs/gui_design.md` and
`examples/gui/README.md`.

Former review: `gui_stage5_review.md`.

## Verification History

The completed phases above were verified repeatedly with:

```sh
make clean && make
./tests/run_examples.sh
./tests/run_negative.sh
```

Module-specific loopback and PostgreSQL integration runners provide additional
coverage. Exact historical dates and file-by-file notes remain available in
Git history.

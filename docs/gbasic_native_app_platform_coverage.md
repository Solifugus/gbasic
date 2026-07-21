# gBASIC Native Application Platform — Coverage Survey

Status: **survey and capability matrix, not implemented.** Companion to
`docs/gbasic_studio_research.md` and `docs/gbasic_studio_gtk_requirements.md`.
Build-ready phasing lives in `docs/gbasic_native_app_platform_plan.md`.

## Framing

Architectural principle (given): **gBASIC Studio drives the completion of gBASIC's
*general* native application-development capability.** No Studio-specific native
infrastructure where a bounded, reusable gBASIC/GI/GTK capability solves the
problem. We do **not** require full PyGObject parity to begin — only the practical
generalized subset that lets gBASIC build sophisticated native GTK4 apps, of which
Studio is the flagship.

Test applied to every capability: *"Would this make sense for another sophisticated
gBASIC desktop app unrelated to Studio?"* If yes → generalized layer. If no →
Studio's own gBASIC code.

Classification buckets used in the matrices:

- **CA** — CURRENTLY AVAILABLE through the `gi` bridge / existing runtime.
- **BG** — AVAILABLE AFTER BOUNDED GENERIC GI WORK (the WI-* items below).
- **RW** — NEEDS A GENERAL REUSABLE WRAPPER/COMPONENT (library or one bounded native
  component), justified on technical grounds, **not** Studio-specific.
- **HD** — NEEDS A HARD/DEFERRED FFI CAPABILITY (callback marshalling, runtime
  subclassing). Avoided by design where possible.

The bounded generic GI work items (from the requirements doc, reused here by name):
**WI-1** boxed/struct values · **WI-2** out/inout args · **WI-3** signal-handler
return values · **WI-4** GLib event-source builtins · **WI-6** GVariant · **WI-7**
arrays/lists · **WI-8** struct field get/set · **WI-10** GError edges. Language
ergonomics: **LE-1** `.property`/`.method()` dispatch hook. Deferred-hard: **WI-5**
callback marshalling · **WI-9** runtime subclassing.

All `file:line` cites are `src/eval.c` unless noted. Verified this session:
`keys`/`values`/`has`/`type`/`is_*` builtins exist (`src/builtins.c`); the process
inbox is `root_mailbox` (7447) with a read fd not yet exposed; no mtime/size/rename
builtins exist.

---

## 1. Application / window infrastructure

| Requirement | GTK/GNOME API | Class | Unlocked by / note |
|---|---|---|---|
| Application lifecycle | `GtkApplication`, `run`, `activate` | **CA** | proven in `examples/gi/gtk4_hello.bas` |
| Windows | `GtkApplicationWindow`, `GtkWindow` | **CA** | construct-time `application` prop works |
| Confirm-on-close | `close-request` signal returning TRUE | **BG** | WI-3 (handler must return a value) |
| Modal/message dialogs | `GtkAlertDialog`(async) / `GtkWindow` modal | **BG** | modal window via signals: CA; `GtkAlertDialog` is async-callback → prefer signal-based modal window (BG), avoids WI-5 |
| File open/save | `GtkFileDialog` (async) vs `GtkFileChooserNative` (`response` signal) | **BG** | Use the **response-signal** route (BG via WI-3/WI-7 for the path list); the new async `GtkFileDialog` would need WI-5 — **not** required |
| Menus / actions | `GMenu`,`GMenuModel`,`GSimpleAction`,`GVariant` | **BG** | WI-6 (GVariant) + object/string calls |
| Keyboard accelerators | `gtk_application_set_accels_for_action` (string array) | **BG** | WI-7 (string array) |
| CSS / theming | `GtkCssProvider.load_from_string`, `add_css_class` | **CA** | string/object calls only — works today |
| Settings / preferences window | `GtkWindow`+widgets; optionally `GSettings` | **CA / BG** | a prefs window is ordinary widgets (CA); `GSettings` schema-based access needs GVariant (BG). App config in gBASIC (JSON/SQLite) is the simpler default |
| Clipboard | `Gdk.Clipboard`, `set`/`read` (GValue/async) | **BG*** | text clipboard via `gdk_clipboard_set`/`read_text_async`; **text-only is BG** with WI-2; rich async read leans WI-5 → keep to text for now |
| Drag and drop | `GtkDropTarget`/`GtkDragSource` + `GValue`/`GType` | **BG*** | value-typed DnD needs WI-1 (GValue boxed) + WI-3; feasible but **defer** unless a real need appears |

**Verdict:** the entire app/window/menu/theming surface is **CA or BG** — no native
component, no hard FFI. Clipboard beyond text and value-typed DnD are the only soft
spots and are deferrable.

---

## 2. Layout

| Requirement | GTK API | Class | Note |
|---|---|---|---|
| Split panes | `GtkPaned` `set_start_child`/`set_end_child` | **CA** | object args |
| Collapsible right inspector pane | `GtkPaned` + `GtkRevealer`/visibility | **CA** | toggle child visibility |
| Collapsible bottom console pane | `GtkPaned` (vertical) + visibility | **CA** | same |
| Boxes / grids | `GtkBox`, `GtkGrid` | **CA** | `Grid.attach` proven (`calculator.bas`) |
| Stacks | `GtkStack`, `GtkStackSwitcher` | **CA** | `add_named`(object,string) |
| Scrolled windows | `GtkScrolledWindow` | **CA** | object args |
| Horizontal scrolling | `GtkScrolledWindow` policy | **CA** | property set |
| Fixed Agent tab + scrollable file tabs | `GtkNotebook` (scrollable) or `GtkStack`+custom `GtkScrolledWindow` tab strip | **CA/BG** | `GtkNotebook.append_page`(object,object) CA; a fixed-left + scrollable strip is composed from box+scrolledwindow (CA). Reordering/close buttons: CA (properties/signals) |
| Project picker: searchable, filterable, scrolling list | `GtkListView`+`GtkFilterListModel`+`GtkStringFilter`, or a `GtkListBox` | **CA/BG/RW** | A **`GtkListBox`** of rows (CA) handles hundreds–thousands of projects fine; search/filter done in gBASIC by rebuilding rows (CA) or via `GtkFilterListModel` (BG/RW if model-backed). For a project picker, `GtkListBox` (CA) is sufficient — no virtualization needed |
| Dynamic add/remove widgets | `append`/`remove`/`insert_child_after` | **CA** | this is the capability the old `gui` module lacked; over `gi` it is ordinary method calls (CA). A gBASIC reconciler library (LE-3) makes it declarative |

**Verdict:** **all layout is CA** (a few conveniences BG). The project picker does
**not** need a virtualized list; `GtkListBox` suffices. Dynamic widget mutation —
the old `gui` module's fatal gap — is simply available over `gi`.

---

## 3. Source editing (GtkSourceView preferred)

Dependency: **GtkSourceView 5** typelib (`gir1.2-gtksource-5`), gated behind a new
`HAVE_GTKSOURCE`/typelib-present check. Driven entirely through `gi` — no bespoke
binding.

| Requirement | GtkSourceView/GTK mechanism | Class | Note |
|---|---|---|---|
| Editable source | `GtkSourceView`+`GtkSourceBuffer`; `set_text`/`get_text` | **BG** | `get_text` needs two iters (WI-2); `set_text` is string; `changed` signal CA |
| Syntax highlighting (gBASIC) | ship a **`gbasic.lang`** (GtkSourceLanguage XML) + `set_language` | **CA/BG** | **highlighting runs inside GtkSourceView in C** — gBASIC never marshals per-token iters. `LanguageManager.get_language`(string)→object, `set_language`(object): CA. Only cost is authoring `gbasic.lang` (data, not code). **This removes the entire hot-path performance concern.** |
| Undo / redo | `GtkTextBuffer` built-in undo (`set_enable_undo`,`undo`,`redo`) | **CA** | bool/void calls — free |
| Cursor / selection | `get_insert`,`get_selection_bounds`(out iters) | **BG** | WI-2 |
| Scroll to a source location | `scroll_to_mark`/`scroll_to_iter` | **BG** | WI-2 (iter) or CA via a mark (object) |
| Search / replace | `GtkSourceSearchContext`/`Settings` (out iters) | **BG** | WI-2; or gBASIC-side string search + scroll |
| Marks (data model) | `GtkSourceBuffer.create_source_mark`(name,category,iter)→`GtkSourceMark` | **BG** | WI-2 for the iter; mark is an object |
| Gutter indicators (line numbers, mark icons) | `set_show_line_numbers`; `GtkSourceMarkAttributes` + `set_mark_attributes` | **CA/BG** | **built-in mark renderer — no custom `GtkSourceGutterRenderer`, no subclassing.** Attributes take an icon-name/paintable (string/object): CA/BG |
| Breakpoint / execution-boundary gutter markers | source marks in distinct categories with attributes | **BG** | same as marks — WI-2 for placement |
| Source-range highlighting | `GtkTextTag` + `apply_tag`(name,startIter,endIter) | **BG** | WI-2; tags are objects |
| Agent-driven temporary highlight / pulse / focus | apply/remove a styled `GtkTextTag`; `grab_focus`; a CSS class toggle with a timeout | **BG** | WI-2 for tag range + WI-4 for the pulse timer + CSS (CA) |
| Diagnostics/errors at source locations | error `GtkTextTag` (squiggle) + gutter mark + tooltip | **BG** | WI-2. gBASIC already emits structured diagnostics (`--json-diagnostics`) to feed this |
| **Inline execution-boundary controls** | `GtkTextChildAnchor` via `create_child_anchor`(iter)→anchor; `gtk_text_view_add_child_at_anchor`(view,child,anchor) | **BG** | **anchor + child are GObjects; only the iter needs WI-2.** No native wrapper. |
| **Inline branch button/tab strips between source regions** | same child-anchor mechanism, child = a `GtkBox` of buttons | **BG** | identical mechanism; the strip is an ordinary widget inserted at an anchor |
| Overlay-style floating controls | `gtk_text_view_add_overlay`(child,x,y) | **BG** | alternative to anchors for non-reflowing overlays; object+int args |

**Critical re-evaluation (per instructions): does inline UI need a native wrapper?**
**No.** `GtkTextChildAnchor` insertion is object-typed; the only boxed value in the
whole editing surface is `GtkTextIter`, which WI-2/WI-1 handle. Syntax highlighting —
the one genuinely hot path — is done by GtkSourceView's own engine from the `.lang`
file, so gBASIC never touches it per-token. **The previously-proposed native SW-1
wrapper is therefore not necessary.** See §7 for the Option A/B/C decision (→ Option
B: a reusable gBASIC `SourceEditor` *library*, no native code).

---

## 4. Variable / value inspection

| Requirement | Mechanism | Class | Note |
|---|---|---|---|
| Expandable tree structures | `GtkTreeExpander`+list model, **or** `GtkExpander` rows, **or** a `GtkListBox`/`GtkBox` built recursively in gBASIC | **CA/RW** | For inspector-scale data (hundreds of visible nodes) a gBASIC-built expander tree is **CA**. A virtualized `GtkTreeListModel` is RW (see §5/DataGrid) only if nodes reach the tens of thousands |
| Lazy expansion | expand only on user toggle; gBASIC builds children on demand | **CA** | driven by the `notify::expanded`/`activate` signal |
| Scalars / nested records / arrays | recursive rendering over gBASIC values | **CA** | `keys`/`values`/`type`/`is_*` builtins already enumerate records/arrays — **no new runtime needed for known values** |
| Matrices | render `stdlib/matrix.bas` records as a grid | **CA/RW** | small: CA; huge: the DataGrid component |
| Large structures | truncate + lazy-load subtrees | **CA** | inspector shows summaries + `inspect(path,depth)` |
| Contextual viewer switching | pick a renderer by structural recognition | **CA** | pure gBASIC dispatch; library-registered viewers by convention |

**Verdict:** value inspection of *known* values is **CA today** — the `keys`/`values`/
`type`/`is_*` builtins already provide the reflection needed. The only inspection gap
is **enumerating an execution *environment's* variables** (not a value the app already
holds) — see §9 (a general `reflect` facility), needed for a debugger/Agent view, not
for rendering a value in hand.

---

## 5. Tabular data

GTK4's `GtkColumnView`/`GtkListView` are **virtualized** — GTK creates widgets only
for visible rows and recycles them. The catch: the data must be a **`GListModel` of
GObjects**, and the factory `bind` handler reads a GObject's columns.

| Requirement | Mechanism | Class | Note |
|---|---|---|---|
| Modest table (≤ ~a few thousand cells) | `GtkGrid` of labels, **or** `GtkColumnView` fed a small hand-built model | **CA/BG** | no virtualization needed; **the platform spike's "modest table" is reachable without any native component** |
| Sorting / filtering (modest) | rebuild rows in gBASIC, or `GtkSortListModel`/`GtkFilterListModel` | **CA/BG** | small data: gBASIC-side sort/filter (CA) |
| Column resizing | `GtkColumnView` columns | **CA** | properties |
| Selection / copy | `GtkSelectionModel` + clipboard text | **CA/BG** | selection CA; copy = text clipboard (BG, §1) |
| **Virtualized large grid (10⁴–10⁶+ rows), lazy retrieval, no widget-per-cell** | `GtkColumnView` + a **custom `GListModel` backed by a gBASIC array/cursor** + `GtkSignalListItemFactory` | **RW** | The custom `GListModel` requires implementing the `GListModel` **interface** — otherwise HD (runtime subclassing). Resolve with **one fixed, hand-written C GType** ("gBASIC array/cursor → GListModel adapter"), a **general reusable component**. Factory `setup`/`bind` are **signals (CA)**; the item GObject exposes columns via a generic accessor |

**Verdict:** modest tables are **CA/BG** — the spike does not need a native grid.
The *virtualized millions-of-rows* grid is the **one place a native component is
genuinely justified**, because a `GListModel` implementation is otherwise hard-FFI
(WI-9). It must be a **general `DataGrid` component** (a bounded C GListModel adapter
+ a gBASIC API), reusable by any data-heavy gBASIC app — **not** a Studio grid. See §8.

---

## 6. Console / results

| Requirement | Mechanism | Class | Note |
|---|---|---|---|
| Append-only output | `GtkTextView` (non-editable) + `insert` at end | **CA/BG** | append via `insert`(iter,text): WI-2 for the end iter, or keep an end mark (CA) |
| stdout/stderr distinction | two `GtkTextTag` styles | **BG** | WI-2 for tagged insert |
| Warnings / errors | tagged spans + gutter | **BG** | reuse diagnostic tags |
| Selectable / copyable text | `GtkTextView` selectable | **CA** | property |
| Contextual switching by section | swap buffers / `GtkStack` pages | **CA** | one buffer per section |
| Rich result objects (later) | embed widgets via child anchors | **BG** | same anchor mechanism as the editor |

**Verdict:** the console is a tagged `GtkTextView` — **CA/BG**, no native component.

---

## 7. Async / event behavior (the safety model)

**gBASIC's concurrency is shared-nothing OS-process actors** (`fork`+`execv` of the
same binary, mailbox over `AF_UNIX`/`SOCK_SEQPACKET`; `src/eval.c:8299-8360`,
`root_mailbox` 7447). The GTK loop blocks the calling process. The safe, thread-free
integration:

1. **Never run blocking work in the GTK process.** Long gBASIC execution, LLM calls,
   DB queries, and external processes are all **synchronous** in gBASIC today, so each
   must run in a **spawned actor** (a separate process), leaving the UI process free.
2. **Watch the mailbox on the main loop.** `gi.watch_fd(fd, fn)` (WI-4, wrapping
   `g_unix_fd_add`) registers the process inbox fd as a GLib source; when a result
   frame arrives, `fn` runs **on the GTK thread**, calls `receive` (non-blocking), and
   updates the UI. All UI mutation happens on the main thread — no locks, no interpreter
   threading.
3. **Progress updates** = intermediate mailbox messages → repeated `fn` invocations.
4. **Cancellation / kill** = terminate the child process (the general process API, §9,
   provides `kill`/`wait`; actors already have monitor + `PDEATHSIG`).

| Concern | Mechanism | Class | Note |
|---|---|---|---|
| Long-running gBASIC work | spawn actor + mailbox result | **CA/BG** | actors CA; loop integration needs WI-4 |
| Actor/process results on GTK thread | `gi.watch_fd(inbox_fd, fn)` | **BG** | WI-4 + **expose the inbox read fd** (small addition; `root_mailbox.read_fd` exists internally) |
| LLM / DB / git calls without freeze | run inside a spawned actor | **CA/BG** | they are synchronous; isolation is the answer |
| Cancellation / kill | kill the child process | **BG** | general process API (§9) |
| Progress updates | intermediate mailbox frames | **CA/BG** | WI-4 to receive on the loop |

**Verdict:** the async model needs exactly **WI-4 + one small "expose/watch the inbox
fd" hook**, riding the existing actor model. No threads, no new concurrency model.
This is a **general capability** — any responsive gBASIC GTK app needs it.

---

## 8. Reclassification of every previously-proposed item

Buckets: **A** generic GI bridge · **B** general GTK/gBASIC library · **B(rt)**
general language/runtime capability · **C** general reusable high-level component ·
**D** Studio-specific · **E** deferred/not-needed.

| Item | Was | Now | Rationale |
|---|---|---|---|
| WI-1 boxed/struct values | bridge | **A** | broadly useful for any introspected lib |
| WI-2 out/inout args | bridge | **A** | the key getter unlock |
| WI-3 signal-handler returns | bridge | **A** | event veto/consume, general |
| WI-4 GLib event sources | bridge | **A** | responsive apps generally |
| WI-6 GVariant | bridge | **A** | actions/menus/GSettings, general |
| WI-7 arrays/lists | bridge | **A** | general |
| WI-8 struct field get/set | bridge | **A** | general |
| WI-10 GError edges | bridge | **A** | fold into WI-2 |
| LE-1 `.property`/`.method()` dispatch | language | **B(rt)** | general ergonomics for all native-object code; a core language capability |
| LE-2 `gtk.bas` idiomatic wrappers | GTK lib | **B** | general GTK library |
| LE-3 declarative widget-tree/reconciler | GTK lib | **B/C** | general retained-mode UI library; reusable |
| **SW-1 SourceEditor** | **native wrapper** | **B (was over-native)** | **Reclassified: build as a gBASIC `SourceEditor` library over generic GI, no native code.** Highlighting via `.lang` keeps the hot path in C; inline widgets via child anchors are BG. *Flagged: previously unnecessarily leaned native.* |
| SW-2 DataGrid | native | **C** | virtualized large-data grid genuinely needs a `GListModel` adapter (else HD); provide as a **general** component, not Studio's |
| WI-5 callback marshalling | P1 | **E** | not needed — file dialogs via response-signal, drawing avoided; revisit only if custom drawing/async callbacks proliferate |
| WI-9 runtime subclassing | P2 | **E** | avoided — draw funcs + factory signals + the one fixed DataGrid GType replace it |
| LE-4 subprocess-exec | prereq | **B(rt)** → general **process API** (§9) | generalized, not a shell builtin |
| LE-4 file mtime/size | prereq | **B(rt)** filesystem builtins | general |
| LE-4 atomic rename | prereq | **B(rt)** filesystem builtin | general |
| LE-4 env-dump | prereq | **B(rt)** general **reflection** (§9) | generalized, not Studio env-dump |
| LE-4 llm tool-calling | prereq | **B** general AI library | `llm.bas` capability |

**Items flagged as unnecessarily Studio-specific in the prior spec:** only **SW-1**
(the native SourceEditor wrapper) — now downgraded to a general gBASIC library. Every
other item was already general or is generalized in §9. **No Studio-specific native
infrastructure remains.**

---

## 9. Generalized non-GTK prerequisites

### 9.1 Process execution — a general process API (not a shell-exec builtin)

Long-term shape (structured, shell-injection-safe by default):

```
process.run({ command, args:[...], stdin?, cwd?, env?, timeout? })
    -> { exit_code, stdout, stderr }          ' synchronous, the smallest step
process.start({ command, args:[...] }) -> handle ' streaming/async
handle.write(bytes) / handle.read() / handle.stderr()
handle.wait() -> exit_code / handle.kill()
```

- **Args are a structured array**, passed straight to `execv` — **no shell parsing**,
  so no injection. An explicit `process.shell(str)` may exist later but is never the
  default.
- **Smallest first implementation:** synchronous `process.run` with captured
  stdout/stderr/exit, reusing the existing fork+exec plumbing (`execv` at 8360,
  generalized to an arbitrary path + argv + pipes). It fits the long-term API as the
  degenerate "run to completion" case.
- **Async use** integrates with §7: `process.start` returns a handle whose stdout fd
  is watched via `gi.watch_fd`. Cancellation = `handle.kill`.
- Classification **B(rt)** — general runtime capability (git, tests, tools, any app).

### 9.2 Runtime inspection — a general reflection facility (not env-dump)

What exists **now** (verified): `keys`/`values`/`has`/`remove_key` (record fields),
`type`/`is_string`/`is_number`/`is_array`/`is_record`/`is_nothing`/`is_unknown`,
`count`. So **recursive inspection of any value the program holds is already
possible.** The gap is enumerating a *scope's variables*.

Proposed general facility:

```
reflect.variables()            ' -> array of { name, type } in the current scope
reflect.get(name)              ' -> value by name (current scope)
reflect.inspect(value, depth)  ' -> structured summary (recurses records/arrays)
```

- `reflect.variables()`/`reflect.get()` enumerate the **current** `Env`
  (`Env{Symbol*items}`, 359-363) — **implementable now**, no interpreter refactor.
- Supports debugger variable views, Agent inspection, and generic serializers — all
  general, none Studio-specific.
- **Changed-value detection** across runs, and enumerating **another/paused**
  interpreter's environment, depend on the future interpreter-context refactor
  (PLAN Phase 3, deferred). Within one run, diffing is `reflect.inspect` +
  `serialize`/`encode` comparison — available now.
- Classification **B(rt)**.

### 9.3 Filesystem — general metadata + atomic replace

```
file_mtime(path) -> datetime      file_size(path) -> number
replace(temp, dest)               ' atomic rename(2), not copy+delete
watch_file(path, fn)              ' optional; inotify (later)
```

- `move` today is copy+delete (4951); add a real `rename(2)`-backed `replace` for
  crash-safe saves. `mtime`/`size` from `stat` (used internally at 5047, not exposed).
- Classification **B(rt)** — general.

### 9.4 LLM tool/function-calling — a general AI-library capability

Add tool-calling to `stdlib/llm.bas`'s anthropic + openai adapters: a `tools`
parameter, and `tool_use`/`tool_result` message handling in `chat`. General to any
agentic gBASIC program; streaming stays deferred (needs `webclient` SSE).
Classification **B**.

---

## 10. The minimal generalized platform milestone

**Native Application Platform Spike** — a general demo (NOT Studio; no API named or
shaped around Studio) proving gBASIC can build a sophisticated native app. It must:

1. create a `GtkApplication` + window;
2. host a **GtkSourceView** editable editor with **gBASIC syntax highlighting** (via
   the shipped `gbasic.lang` + `SourceEditor` library);
3. use resizable **`GtkPaned`** panes;
4. show a **`GtkStack`/notebook** and a **scrolling list**;
5. **add/remove widgets dynamically** at runtime;
6. display an **expandable structured value** (a nested record → tree, built in gBASIC
   over `keys`/`values`);
7. display a **modest table** (no native grid);
8. launch **asynchronous background work** in a spawned actor **without freezing** the
   UI, and receive the result **safely on the GTK thread** (`gi.watch_fd`);
9. launch an **external process** and capture its output (`process.run`);
10. **highlight/focus a UI element programmatically** (CSS class + `grab_focus`);
11. insert **one inline widget at a source location** (a `GtkTextChildAnchor` child)
    to validate the execution-boundary / branch-selector concept.

Every capability exercised is **general**. Nothing native beyond the bounded GI
extensions is required to reach this proof point — in particular, **no DataGrid and no
hard-FFI** are on the spike's critical path.

---

## 11. Concise recommendation

**1. After the bounded generalized work, how much of Studio can be written directly in
gBASIC?** The **vast majority** — essentially all of it. UI (windows, panes, tabs,
menus, lists, console, editor, inline boundary/branch widgets, value inspector) is
gBASIC over the extended `gi` bridge; the section/replay engine, branch engine, agent
orchestration, project model, and persistence are already gBASIC-shaped. The only code
*not* in gBASIC is (a) the bounded C in the GI bridge (WI-1/2/3/4/6/7/8/10 + LE-1) and
(b) one optional native component (§2 below), both **general**, neither Studio-specific.

**2. Which reusable native components remain justified?** Exactly **one**, and only for
scale: a **general `DataGrid`** backed by a single fixed C `GListModel` adapter, needed
for virtualized 10⁴–10⁶-row views (otherwise hard-FFI). It is a general UI-library
component. The **SourceEditor is *not* native** — it is a gBASIC library over generic
GI (the earlier native proposal is withdrawn). Modest tables need no native component.

**3. Which expensive FFI capabilities can safely stay deferred?** **Callback
marshalling (WI-5)** and **runtime subclassing (WI-9)** — the two genuinely large,
open-ended features. Studio (and the platform) avoid them by using response-signals for
dialogs, GtkSourceView's `.lang` engine and built-in gutter for the editor, factory
*signals* + one fixed GType for grids, and no custom-drawn/custom-subclassed widgets.
Revisit only if custom drawing (minimap, canvas viewers) or async-callback APIs later
prove necessary.

**4. Smallest sequence to a convincing native-application proof point?** Bridge core
(WI-1 → WI-2 → WI-3 → WI-4 + inbox-fd) → GVariant/lists (WI-6/WI-7) → `.property`/
`.method()` dispatch (LE-1) → general `process.run` → the `SourceEditor` gBASIC library
(+ `gbasic.lang`) and minimal `gtk.bas` helpers → the **Native Application Platform
Spike**. Reflection, filesystem builtins, the reconciler, the DataGrid, and llm
tool-calling all come **after** the proof point. See
`docs/gbasic_native_app_platform_plan.md` for the phased build.

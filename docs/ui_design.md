# `ui.` — one GUI module, more than one backend

**Status: ruled on 2026-11-16; push A (§8 steps 1–3: the rename, the vocabulary, the theme record) built the same day, gated, not yet pushed. Steps 4–6 are not built.** Written 2026-11-16 against gBASIC
`4eafb557` (0.11.0) and gBASIC Studio `4aa0420`. It answers whisker's
proposal (`~/development/whisker/docs/gbasic-ui-proposal.md` §7) and ends
with Matthew's rulings (§9, 2026-11-16). Every count below is
a command run over those trees; the tier sort in §5 is a reading of every
GUI suite's source, and no suite was run for it.

---

## 1. What is being asked

whisker's proposal, in four steps:

1. rename the `qt` module to `ui`;
2. split `src/modules/qt.c` into a toolkit-neutral front over a table of
   backend functions, Qt's filled by today's `qt_backend.cpp`;
3. add `ui.can(...)`, so a backend may refuse a verb by name, plus a
   neutral theme record;
4. (whisker's step) a second backend, `ui_whisker.c`, proven by running
   gBASIC's own GUI suites under `GBASIC_UI=whisker`.

gBASIC agreed to 1–3 in principle on 2026-11-15, with this note first. The
aim, in Matthew's words: *"a gBASIC programmer [should] learn one way to
write GUI apps that works across both."*

## 2. The finding this note adds: the name is the small part

whisker counted `qt.` at every call and proposed renaming it. That is right
and cheap. But **renaming the module does not make a program portable**,
because three verbs carry the toolkit in their arguments:

    qt.read(win, "editor", "plainText")      ' a Q_PROPERTY name
    qt.invoke(win, "save", "click")          ' a Qt slot name
    on save.clicked                          ' a Qt signal name

The verbs `read`, `set` and `invoke`, and a handler's event name, reach
Qt's meta-object system by name (design §1's "by-name route"). That route
is why one table line was enough to add a Qt widget, and it is what makes
the program speak Qt.

Measured in Studio:

| | count |
|---|---|
| `qt.read` / `qt.set` / `qt.invoke` calls | 372 |
| of them, naming a literal Qt property or method | **365** |
| distinct names | 33 |
| calls covered by the ten most used names | **316 (87%)** |

The ten, in order: `trigger` 61, `plainText` 60, `text` 57+18, `click` 39,
`checked` 15+5, `visible` 13+9, `setFocus` 8, `windowTitle` 7,
`whitespace` 7, `hide` 7. (`whitespace` is gBASIC's own; the rest are
Qt's.) gBASIC's own tree uses 62 distinct names. Event names in Studio's
handlers: `triggered` 41, `clicked` 31, `activated` 9,
`itemSelectionChanged` 8, `currentIndexChanged` 3,
`customContextMenuRequested` 3, `textChanged` 2, `currentChanged` 2.

Events gBASIC invented are already neutral: `closed`, `wheel`, `gutter`,
`block`, `link`, `selected`.

The tier sort (§5) finds the same thing from the other side. **Of the GUI
suites' contract tiers, most are contract-with-a-Qt-quirk, and the quirk is
nearly always a Qt name passed through `read`/`set`/`invoke`/`on`.** No
verb fails to be neutral. The vocabulary under the verbs is the problem.

So there are three layers, not one:

| layer | what is Qt in it today | whisker's proposal |
|---|---|---|
| the module name | `qt.` at ~2,265 code call sites (1,601 gBASIC, 664 Studio; comment lines excluded) | §4.1, rename |
| the seam | 148 `gb_qt_*` functions in `include/qt_backend.h`, plus the loop contract (§4) | §4.2, backend table |
| **the vocabulary** | property, method and signal names; Qt class names in refusals; QKeySequence and QFont strings; QSS | not named |

Without the third layer, `ui.read(win, "editor", "plainText")` only looks
neutral. Whisker would have to emulate Qt's property names, which is §4.3's
trap, "the first backend's quirks become everybody's contract".

## 3. One vocabulary (Matthew, 2026-11-16: "I don't want to end up with two vocabularies")

**The ruling's shape.** gBASIC has one set of words for what a program
reads, sets, invokes, declares as an option, and handles. Every backend
translates from those words. **No Qt name passes through.** `plainText`,
`setFocus` and `clicked` are refused like any misspelling, with "did you
mean `text`?". The refusal teaches the word, so a Qt programmer's habit
costs one error, not a second dialect.

This rejects the first draft of this section, which resolved the neutral
word first and let anything else through to Qt by name. That made two
vocabularies, and a program could not tell which one it was writing in.

**It is possible because the vocabulary in use is small.** Measured over
gBASIC and Studio together (`qt.read`/`qt.set`/`qt.invoke` with a literal
name, Studio's `qt_on`, and `on x.y` handlers): **55 property and method
names** (three of them deliberate misspellings in refusal tiers) and **16
event names**. About 60 gBASIC words, then, cover every name any program
uses today. The mapping as built (the lists are `src/window_check.c`; Qt's map is `src/modules/qt_backend.cpp`; `tests/run_ui_vocabulary.sh` holds them and the reference's *Words* to each other):

| gBASIC word | Qt today |
|---|---|
| `text` | `text`, `plainText`, `markdown` (by part) |
| `visible`, `enabled`, `checked`, `checkable`, `icon`, `shortcut`, `whitespace` | same names (the last three are gBASIC's already) |
| `index` | `currentIndex` |
| `count` | `count`, `rowCount` (by part) |
| `columns` | `columnCount` |
| `title` | `windowTitle` |
| `read_only` | `readOnly`, `editTriggers` |
| `hint` | `placeholderText` |
| `max_length` | `maxLength` |
| `wrap` | `lineWrapMode`, `wordWrap` |
| `tab_width` | `tabStopDistance` |
| `font_size` | field 1 of `QFont::toString`; fractional points (Studio's zoom writes 11.5) |
| `x`, `y`, `width`, `height` | geometry, read only |
| `min_width`, `max_width`, `min_height`, `max_height` | the same Qt names, camel-cased; lifting a cap must work |
| `tip` | `toolTip` (option and property) |
| `colors` | read only: a record in the theme record's field names, what the part is drawn in (replaces reading `palette`) |
| `emphasis` | readable; the teaching cue, replacing a per-part `styleSheet` selected through `objectName` |
| `headings` | `headerHidden`, inverted |
| `striped` | `alternatingRowColors` |
| `sortable` | `sortingEnabled` |
| `indent`, `expanders` | `indentation`; `rootIsDecorated` with `itemsExpandable` |
| `tab_side`, `movable`, `closable` | `tabPosition`, `movable`, `tabsClosable` |
| `toolbar_shows` | `toolButtonStyle` (`"icons"`, `"text"`, `"both"`) |
| invoke `press` | `click`, `trigger`, `toggle` (by part) |
| invoke `focus`, `undo`, `redo`, `hide`, `raise`, `close` | `setFocus`, and the same names |
| invoke `select_all`, `fit_columns` | `selectAll`, `resizeColumnsToContents` |
| event `pressed` | `clicked`, `triggered` |
| event `changed` | `textChanged`, `currentIndexChanged`, `currentChanged`, `toggled`, `stateChanged` |
| event `chosen` | `itemSelectionChanged`: the selection moved, by click or by arrow key |
| event `activated` | `itemClicked`, `cellClicked`, `itemActivated`, `activated`: a row was clicked or activated |
| event `entered` | `returnPressed` |
| event `context` | `customContextMenuRequested` |
| events `closed`, `wheel`, `gutter`, `block`, `link` | gBASIC's already |
| `default` | a button's `default` (the one Enter presses) |
| `secret` | a field's `echoMode` Password or Normal |
| invoke `clear` | `clear`; a table's `clearContents` |
| `focused` | `focus`, read only |
| `fixed_width` | the resolved font's `fixedPitch`, read only |
| `shown_text` | a `markdown` part's rendered `plainText`, read only (`text` is the markdown written) |
| `direction` | a separator's `HLine`/`VLine`, a split's orientation, as `"across"`/`"down"`, read only |
| `bold` | the font's weight |

**Built, the words above grew by nine** where a fixture or an example needed
one: each was a real need the old by-name route had met with a Qt name. And
two tiers had asserted pure Qt state (`contextMenuPolicy`, `widgetResizable`);
the first became a source tripwire on the backend (`run_qt_context.sh`
`policy`), the second is the `ui.pane` oracle's `resizable`, already read from
Qt's own scroll area.

**Selection and activation are two events, never one.** Studio's check
found the first draft folding `itemClicked` into `chosen`. Studio opens a
file on a click and only selects it on an arrow key. Merged, arrowing down
the tree would open every file on the way. Studio's own rule says
selection and activation are separate, and the vocabulary keeps them
separate.

Studio's session checked this table against every literal name, handler
and option key it uses (2026-11-16). Its corrections are in the table: the
two events above, `tip`, `select_all`, `fit_columns`, the size caps,
fractional `font_size`, `colors` and a readable `emphasis`.

The rest of the measured names lose their reason to exist:
- `styleSheet`, `palette` and `objectName` go to the theme record (§7).
- `floating` goes to the `docking` group.
- `contextMenuPolicy`, `frameShape`, `widgetResizable`,
  `verticalScrollBarPolicy`, `orientation` and `clearContents` are read
  only by tiers asserting Qt's own state. They are Q tiers (§5) and leave
  the contract.

**Declaration options are the same vocabulary.** Today
`field name( placeholderText: "…" )` sets a Q_PROPERTY by name, the same
route as `qt.set`. Under the ruling, an option is a gBASIC word or a
load-time refusal. This is the closed-set check the parts' own options
(`align:`, `theme:`) already have. It also catches a misspelling at load
time rather than at `open`.

**What the ruling costs.** Design §1's by-name route made any Q_PROPERTY
usable with no work. Under one vocabulary, a property nobody has needed
yet is a word added: one line in the vocabulary table, one mapping line per
backend, and a tier. That is the price of one vocabulary. It is the right
price for a language whose principle is less to remember: a word is
designed once, and every backend and every reader then shares it. The
by-name route survives inside the Qt backend as how a word is implemented.
It stops being what a program says.

**The table is closed and tested from both ends**, the tripwire shape of
`run_web_configure.sh`'s admitted-options lists:
- every word has a mapping in every built backend;
- no Qt name is accepted;
- a Qt name or a misspelling gets "did you mean".

## 4. The seam is the event-loop contract, not only 148 functions

What gBASIC requires of any backend, taken from `qt_backend.h` and the
tiers that hold it:

1. **The backend owns the wait; gBASIC gets ticks.** The interface is
   `gb_qt_set_callbacks(invoke, tick, closed, cell)`. `tick` is one
   non-blocking iteration of gBASIC's loop, called every 50 ms, which is
   webserver_run_event_loop's own poll interval (`qt.c`, verb `run`). Qt
   calls it from a `QTimer`. For whisker, which owns no loop (`wsk_feed` is
   a function of one event), `ui_whisker.c` owns the loop and calls the same
   `tick`. Nothing in gBASIC changes.
2. **Ticks continue inside a modal.** A dialog or `open_modal` opened from
   `main` keeps gBASIC's sources served (`run_qt_modal` `extent`,
   `run_net` tier 5). One opened from inside a watcher body suspends them
   (`run_qt_dialog` `modal extent`). The second rule is held by gBASIC's
   `watcher_drain` guard alone (§0.15's "two guards, each sufficient"), so
   it is backend-free. The first is the backend's duty: Qt keeps a
   reference-counted pump armed. Whisker re-enters its own loop with another
   target.
3. **`can_display` is a query that cannot crash.** Qt aborts with no
   display, so it is asked in a forked child. Whisker's SDL host fails
   without aborting, and its browser host always can.
4. **Closing the last window ends `run`.** This is Qt's
   `quitOnLastWindowClosed`, and `run_qt_lifecycle` `loop` asserts it. It
   becomes a stated rule of the contract rather than a Qt default we
   happened to inherit.
5. **`stop` ends `run` and leaves the windows open and readable**
   (`run_qt_lifecycle` `stop`; the `exit(0)`-not-`quit()` correction).
6. **The four endings of a modal window** (`qt_backend.h` above
   `gb_qt_show_modal`): `closed` fires exactly once however it ended,
   because gBASIC closes the window before `open_modal` returns. That is
   front-end behaviour; a backend reports accept/reject and close.
7. **A handler may call back into the backend** (select then refresh inside
   a handler). Re-entry is capped at depth 16 with warning 2117
   (`run_qt_reentry` `bounded`, raised in `qt.c`).

A backend table that leaves out 1–7 has the right function names and
breaks programs.

## 5. The tier sort

Every tier of every GUI suite: the 28 `run_qt_*`, `run_window`, and the Qt
tiers of `run_net`, `run_stdin_lines`, `run_watch_scope`,
`run_handle_methods` and `run_bag_smoke`. Each is classed:

- **N**: needs no backend: grammar, declaration checks, `qt.check`,
  source tripwires on `qt.c`.
- **C**: the contract: a gBASIC program drives verbs, and what is
  asserted is what gBASIC observes.
- **C\***: the contract, but the expected output encodes a Qt fact.
- **Q**: Qt by construction: C++ probes linking `qt_backend`, PIL
  pixels of Qt's rendering, tripwires reading `qt_backend.cpp`,
  KSyntaxHighlighting, QSS, QMainWindow docking, valgrind with Qt's
  access-only policy.

| suite | N | C | C\* | Q | the C\* facts |
|---|---|---|---|---|---|
| run_window | 7 | | | | |
| run_qt_builder | 4 | 1 | 1 | 1 | `click`/`clicked` |
| run_qt_backend | | 1 | 13 | 2 | property names; refusals naming QPushButton/QLabel; `QT_QPA_PLATFORM` as the display switch |
| run_qt_lifecycle | 1 | 2 | 4 | 1 | `windowTitle`, `visible`; last-window-quits |
| run_qt_modal | 1 | 2 | 2 | 1 | `windowTitle`, `clicked` |
| run_qt_dialog | | 2 | 2 | 1 | `click` |
| run_qt_loop | | | | 7 | (a C++ probe of Qt itself) |
| run_qt_reentry | | 1 | 1 | 3 | `itemSelectionChanged`, `plainText` |
| run_qt_chooser | | 1 | 2 | 2 | the filter's `;;` join; Qt's dialog wording |
| run_qt_source | 1 | 1 | 2 | 4 | KSyntaxHighlighting names; `lineWrapMode` |
| run_qt_gutter | 2 | 1 | | 2 | |
| run_qt_find | | | 1 | 1 | invoke `undo`/`redo` |
| run_qt_notebook | | | 2 | 3 | "an SVG QtSvg can draw"; KSH themes; pixel heights |
| run_qt_capture | | | 1 | 2 | `styleSheet`, `setFocus`, "a QAction" |
| run_qt_clipboard | | 2 | | 2 | |
| run_qt_keys | | | 1 | 1 | QKeySequence spelling; class names in refusals |
| run_qt_icon | | | 2 | 2 | `SP_*` names; class names in refusals |
| run_qt_frame | 2 | | 1 | 2 | (semantics is QMainWindow docking, with a C core) |
| run_qt_align | 1 | 1 | | 1 | |
| run_qt_ask | 2 | | 1 | 1 | lazy viewport fetch (Qt model/view) |
| run_qt_children | 2 | | 1 | 2 | `count`, `checkable`, `click` |
| run_qt_context | 3 | | 1 | 1 | `contextMenuPolicy`, `frameShape`; QTableWidget/QAction in refusals |
| run_qt_panes | 1 | | 1 | 2 | sizes total 700–900 of 900 (Qt's handle loss) |
| run_qt_split | 2 | 1 | 1 | 2 | `widgetResizable`, `orientation`, scroll-bar policy |
| run_qt_tree | 2 | | 1 | 1 | `headerHidden`, `indentation` |
| run_qt_wheel | 3 | | 1 | 1 | the QFont string |
| run_qt_which | | | 1 | 2 | `tabPosition`, `currentChanged`, `toggled` |
| run_qt_cookbook | 3 | 17 recipes | 7 recipes | 2 recipes | 01 maxLength, 06 key text, 09 KSH, 13/22 run, 19 font, 21 SP_ |
| run_qt_examples | 2 | 1 | 1 | | stderr filter of Qt plugin noise |
| run_net / stdin_lines / watch_scope | | 3 | | | |
| run_handle_methods (qt) | | | 1 | | `enabled`, `click` |
| run_bag_smoke (GUI) | | | 2 | | a QSS in `theme`; `rowCount`, `returnPressed` |

Every valgrind tier is classed Q because it uses Qt's access-only policy
(Qt never frees its statics). The gBASIC program each one drives is
neutral, so under another backend it is the same tier with that backend's
leak policy.

**What the sort says:**

1. **No verb is reached only by a Q tier** except `theme` (its one real
   assertion is the QSS recipe 12) and `dock`, `layout`, `restore_layout`
   (QMainWindow docking: run_qt_frame and recipe 25). Everything else
   whisker's §4.3 listed has a C or C\* tier.
2. **Most C\* tiers become C once the neutral vocabulary of §3 exists**,
   by rewriting the fixture's Qt names into neutral words. That rewrite is
   the bulk of making the suites the contract. It is mechanical once the
   vocabulary is ruled.
3. **Some refusals are written in `qt_backend.cpp` and name Qt classes**:
   17 `className()` uses, such as "a QPushButton has no `texx` property",
   "a QLineEdit takes one shortcut", "a QWidget emits no `closed` signal"
   and "`SP_NoSuchThing` is not one of Qt's standard icon names". They
   should say gBASIC's own word ("a `button` has no …"), which is also
   better for a reader who never wrote Qt. The front end then owns the
   sentence and asks the backend only yes or no. That moves goldens and is
   a listed rebaseline.
4. **Some Q tiers are Qt's and stay Qt's**: the C++ probes (loop,
   gutter paint, blocks, clipboard), the `qt_backend.cpp` tripwires
   (children `drop`, panes `one walk`, split `guards`, which `slot choice`,
   chooser `native`, icon and reentry `one door`), the KSH tiers of
   `run_qt_source`, the PIL pixel tiers. They move into Qt-backend suites
   and are never run under another backend.
5. **One tier is mixed and should be split**: run_qt_notebook's
   "figures and themes" (`render_svg` is C\*, the KSH theme half is Q).
   Likewise run_qt_frame `semantics`: its status bar, toolbar labels and
   menu bar checks are C, and the docking is Q.

## 6. Verbs every backend answers, and capability groups

From the sort, not from names (this replaces whisker's first sorting in
§4.3):

**Every backend answers.** These are verbs with a C or C\* tier and no
toolkit fact under them once §3 exists:

- the window: `open` `open_modal` `close` `is_open` `run` `stop`
  `can_display` `check` `accept` `reject`
- reading and writing: `read` `write` `set` `invoke` `subject` `options`
- event synthesis, the test machinery: `click` `key` `right_click`
  `wheel` `swallow` `which_tab`
- rows: `select` `selected` `refresh` `rows` `cell` `heading` `depth`
  `alignment` `children` `tabs` `label` `at`
- menus: `menus` `popup`
- panes: `pane` `resize_panes` `scroll` `scroll_to` `scrolled`
- dialogs: `confirm` `inform` `answer`
- text: `caret` `caret_to` `select_text` `selection` `replace_text`
  `highlight_text` `highlights`
- the clipboard and keys: `copy` `clipboard` `shortcuts`
- gutter marks: `mark` `marks` `gutter` `gutter_press` `gutter_click`

`clipboard`, `shortcuts`, `right_click` and the gutter are on this list
because whisker has since built them (§5s and §5v of whisker's proposal).

**Capability groups**, asked with `ui.can("<group>")` and refused by name,
never silently:

| group | verbs / options | why a group |
|---|---|---|
| `highlighting` | `can_highlight` `highlighting`; `source( language:, theme: )` as names | KSyntaxHighlighting definitions and themes are KDE's catalogue |
| `docking` | `dock` `layout` `restore_layout`; `docks` / `dock` declarations | QMainWindow; `layout` is Qt's `saveState` base64 |
| `stylesheet` | `theme(css)`; the `styleSheet` property | QSS; the neutral theme is §7 |
| `figures` | `render_svg` `capture` | a renderer and a raster writer |
| `file_dialogs` | `choose_file` `choose_save` `choose_folder` `dialog` | whisker lacks them (its §5) |
| `notebook` | `blocks` `fold` `block_press` `block_event` `link_press` `link`; `markdown` part | Studio's notebook, built on QTextDocument |
| `icons` | `has_icon`; `icon:` by `SP_` name or theme name | QStyle and freedesktop catalogues; a file path stays every backend's |
| `windows` | a second simultaneous top-level window | whisker lacks it today |

`ui.can` with an unknown group is itself a refusal with "did you mean". A
group is all or nothing per backend, so a program asks once.

`ui.can_display` and `ui.can_highlight` stay as they are. They answer
questions about the machine, not the backend.

## 7. The theme record

Studio already has one. `studio_style.dark_palette()` and
`print_palette()` return

    { bg, base, ctl, hi, fg, faint, line, sel, on_sel }

plus four icon paths. `studio_style.sheet(p)` renders that record into a
QSS. That is a neutral record with one rendering, built because two
palettes needed one rule set. So the proposal is to adopt Studio's record
as gBASIC's:

| field | meaning |
|---|---|
| `bg` | window and dialog background |
| `base` | text surfaces (field, memo, source, table cells) |
| `ctl` | controls (buttons, headers, tabs) |
| `hi` | a control under the pointer |
| `fg` | ink |
| `faint` | secondary ink, disabled |
| `line` | borders, separators, splitter handles |
| `sel`, `on_sel` | selection and the ink on it; also a checked tool button |
| `arrow`, `tick`, `dock_close`, `dock_float` | icon files: a choice's drop-down arrow, a check box's tick (palette-painted under Qt, so it drew no box in dark), a dock's close and float buttons |

`ui.theme(record)` is every backend's. `ui.theme(text)` is a QSS and
belongs to the `stylesheet` group. On Qt the record is rendered to QSS
inside the backend. That is Studio's `sheet()` moved into gBASIC, including
its measured lesson: no bare `QWidget` rule, because content Qt paints from
the palette would vanish.

Studio's three per-part stylesheets are tree branch glyphs off, a bold
heading and the teaching cue's border. Each has a neutral home: a tree
option, a `text` option `bold: true`, and an `emphasis` property naming
the cue. These are for Q3, not decided here.

## 8. The order of work, each a gated increment

Once no Qt name passes through (§3), Studio cannot run on the rename alone.
Every `"plainText"`, `"clicked"` and per-part `styleSheet` it holds becomes
a refusal. So **everything a program sees changes in one push**, and Studio
migrates once rather than three times. What follows that push changes
nothing a program can see.

**Push A: what programs see** (three gated commits, pushed together; Studio
migrates the same day):

1. **The rename.** `qt` → `ui` everywhere it is a name:
   - the module dispatch and `library_is_native_qualifier`
   - the capabilities entry `{ "qt", "display" }`
   - the handle kind (`eval.c` returns `"qt"`)
   - warnings 2115–2117 and the error code's module name
   - `--capabilities` and `has_builtin`
   - the editor grammars (generated) and the LSP
   - docs, the cookbook and examples
   - every `qt.` call (1,601 here)

   With only names moved, the test is the full gate green. `ui` is free: no
   `.bas` in gBASIC, Studio, apps, programs, books or tedderland uses it as a
   name. Studio's `tests/drivers/ui.bas` is a program run directly, never
   loaded, so it does not collide.
2. **The vocabulary (§3)**, for `read`, `set`, `invoke`, declaration
   options and handler events. Qt names are refused with "did you mean",
   and the refusals say gBASIC's part words (Q4). The C\* fixtures are
   rewritten into the vocabulary, and most C\* tiers become C. That is a
   listed rebaseline, the bulk of the goldens that move.
3. **The theme record (§7)** and the per-part replacements for Studio's
   three stylesheets. Without them, step 2 leaves Studio's teaching cues
   with nothing to say.

**Then, invisible to programs:**

4. **The split.** `ui.c` is the front. The backend calls become a
   `GbUiBackend` table of function pointers, and Qt's table is
   `qt_backend.cpp` behind it, with the word-to-Qt mapping inside it. The
   loop contract of §4 is written into the table's header, and the
   backend's refusals move to the front.
5. **`ui.can` and the capability groups (§6).**
6. **The suites.** Contract tiers read `GBASIC_UI` and run under every
   built backend. Q tiers move to Qt-backend suites. Whisker's backend
   (`ui_whisker.c`, its own `HAVE_WHISKER`, whisker as a header) is
   whisker's step, measured by step 6's contract tiers.

**Coordination.**
- **The Windows session** is doing Qt work in this tree. Push A waits until
  its work is pushed and nothing of its is in flight.
- **Studio** moves the same day as push A, by Studio's session, against
  this note's §3 table.
- **A second application** is coming soon (Matthew, 2026-11-16). If it
  starts after push A it never meets `qt.`; if before, it migrates with
  Studio. That is a reason for push A to come early, before its call sites
  accumulate.

## 9. Rulings (Matthew, 2026-11-16)

"Good plan" accepted the recommendations. Q2 was ruled differently from
how it was asked.

**Q1. The alias.** *Hard rename, no alias*, with Studio migrated the same
day. Before 1.0, with every caller in trees we hold, an alias is one more
thing to remember and to remove later.

**Q2. The vocabulary.** *One vocabulary.* "I don't want to end up with two
vocabularies if reasonably possible to prevent." The draft asked for
neutral words with Qt names still passing through on Qt, which is two
vocabularies. §3 is rewritten: gBASIC's words only, Qt names refused with
"did you mean". It is reasonably possible because about 60 words cover
every name used in either tree today.

**Q3. The theme record.** *Studio's
`{ bg, base, ctl, hi, fg, faint, line, sel, on_sel }` as gBASIC's,* with its four icon fields (`arrow`, `tick`, `dock_close`, `dock_float`), which Studio's check showed the sheet cannot do without.
`font_size` stays a part word (§3). The three per-part sheets become a
tree option, a `text` option `bold: true`, and an `emphasis` word for the
teaching cue, fixed in push A's step 3.

**Q4. Refusals in gBASIC's words.** *Yes:* "a `button` has no `texx`
property", never "a QPushButton …". Goldens move, listed.

**Q5. Names of files.** *Yes.* The contract side is renamed to `ui`
(`examples/ui/`, `docs/ui_cookbook.md`, the contract suites). `qt` stays
for what is Qt's (the Q suites, `qt_backend.cpp`, `tests/qt.supp`).
`qt_gui_design.md` keeps its name as history, with a pointer here.

**Q6. Choosing a backend.** *As written.* The build says which backends it
has, the default is Qt, `GBASIC_UI` chooses at run time, `ui.backend()`
answers which, and a program never names a backend.

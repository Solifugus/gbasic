# A TUI: a terminal layer, then a third `ui` backend

**Status:** Partial. §7's increment 1, the `term` module on POSIX, is built
(2026-11-19, §2.4; `docs/reference.md`, *Term Module*; `tests/run_term.sh`),
and so are increment 3, the `tui` backend's layout and first leaves with
the selection rule, `needs:` and sizes in characters (2026-11-19, §3.5), and
increment 4, the rows, the furniture and the regions (2026-11-20, §3.6;
`docs/reference.md`, *The terminal backend*; `tests/run_ui_tui.sh`), and
increment 5, the dialogs, the file chooser and `open_modal` (2026-11-20,
§3.7), and increment 6, `memo` and `source` (2026-11-20, §3.8), after which
every GUI suite runs on this backend too, and increment 7, highlighting from
KDE's definitions (2026-11-20, §3.9). The Windows console (2) is not. §8 holds the questions for
Matthew. **Q1 ruled the same day: our own terminal layer, not
ncurses** (Matthew: "our own terminal layer sounds fine ... time to leave
that to the history books"). **Q2 ruled 2026-11-19: §4's selection rule** (Matthew: "The terminal is a
nice fallback"). **Q3 to Q6 ruled 2026-11-19 as recommended** (Matthew: "yes
to all"; §8.1 holds what each ruling means). Matthew, 2026-11-18:

> On the TUI. It is important to have one. We should design one. My first
> thought is to build a basic TUI system that can work across Linux, Mac, and
> Windows -- not sure if ncurses would work for that. Once this fundamental
> capability is built then we should build another option for the window block
> we use for QT and Whisker. ... I think we can at least keep a common core of
> capabilities that work across all three. Whisker will have capabilities
> beyond QT and the TUI will [have] limits, of course. But if one codes within
> those limits then one system could work across all three. Also, I wonder if,
> instead of a build option, there could be a switch that turns on which of
> the three of these the program uses... a switch in gbasic, I mean.

It replaces the old "one UI declaration rendered as GUI, TUI and MCP" track,
which never got past a roadmap line and was dropped on 2026-11-18. That
track's TUI half is this note, built on the `ui` seam that 0.12.0 shipped
(`docs/ui_design.md`).

## 1. Two layers

1. **A terminal layer**, `term`: a module of its own, useful without any
   `window`. It covers the full screen, keys, the mouse, resizing, colour and
   restoring the terminal however the program ends. It runs on Linux, macOS
   and Windows. Full-screen tools, progress displays and an interactive SSH
   client's raw mode all need it. That last one is listed as missing in
   `docs/networking_design.md` (the table of what SSH needs: "raw terminal
   mode and `SIGWINCH` ... **no** for programs").
2. **A `tui` backend** for `window` declarations, beside Qt and whisker. It is
   drawn with the terminal layer. Behind the backend table, a program cannot
   tell it from the others except through `ui.can`.

## 2. The terminal layer

### 2.1 Not ncurses

- **Windows.** ncurses is not native there. The usual stand-in, PDCurses, is
  a different library with differences of its own, so we would carry two
  terminal layers and test both.
- **Its reason to exist has mostly gone.** ncurses exists to drive many
  mutually incompatible terminals through terminfo. Today the terminals
  people use all speak the same xterm-style escape sequences. That includes
  Windows Terminal, and the Windows console since Windows 10 once virtual
  terminal processing is switched on. So do macOS's Terminal and iTerm2,
  every Linux terminal, tmux and screen, and SSH, which passes the
  sequences through.
- **A layer this small is ours to write.** It is the same reasoning that
  made the prompt's line editor our own (`src/lineedit.c`; readline is GPL-3
  and gBASIC is Apache-2.0). ncurses's licence would be fine, but the
  dependency buys little.

**The floor: an xterm-compatible terminal.** A terminal that is not one, such
as a serial console or `TERM=dumb`, is refused by name at open, as Qt is
refused without a display. `term.can()` answers first (§2.3).

### 2.2 What it has to do

| piece | POSIX (Linux, macOS) | Windows |
|---|---|---|
| raw mode, restored on every exit | `termios`; `src/lineedit.c` already does this for the prompt | `SetConsoleMode`: off `ENABLE_LINE_INPUT`/`ECHO_INPUT`/`PROCESSED_INPUT`, on `ENABLE_VIRTUAL_TERMINAL_INPUT`. **New**: the prompt's editor has no raw mode on Windows today (`src/lineedit.c`: "A console editor needs ReadConsoleInputW and virtual-terminal output and is its own piece of work") |
| output | escape sequences | the same, with `ENABLE_VIRTUAL_TERMINAL_PROCESSING` on stdout, and the UTF-8 code page gBASIC's manifest already sets |
| keys | decode escape sequences (CSI, SS3, and the modifier forms xterm sends) | with `ENABLE_VIRTUAL_TERMINAL_INPUT` the console sends the **same** sequences, so one decoder serves both |
| the mouse | SGR mouse reporting (`?1006`), asked for, never assumed | the same sequences under VT input |
| resizing | `SIGWINCH` | a `WINDOW_BUFFER_SIZE_EVENT` in the input queue |
| waiting on input beside gBASIC's other sources | stdin's descriptor in the event loop's `poll` | the console input handle is waitable; the event loop's Windows half needs it added |
| alternate screen, cursor shape, bracketed paste | `?1049`, DECSCUSR, `?2004` | the same |

**Above those, the layer keeps a screen buffer.** A cell is a character, its
width and a style. Each frame draws only the cells that changed, because an
SSH session over a slow link is the case that shows a careless redraw.

**Colour degrades.** A style asks for 24-bit colour. The layer draws it in
24-bit, 256 colours or 16, by what the terminal says it supports
(`COLORTERM`, then `TERM`), and a program can ask which.

**Character widths are a table we generate**, from the Unicode Character
Database's East Asian Width and emoji properties, pinned to one Unicode
version. Terminals disagree with each other on some emoji and on some
ambiguous-width characters. That is a limitation of terminals, and it
belongs in the reference, not hidden.

**Restoring the terminal is the requirement that matters most.** A program
that dies in raw mode leaves the user's shell unusable. The terminal is
restored:

- at a normal end, and on any raise that ends the program;
- on `SIGINT`, `SIGTERM` and `SIGHUP`. `SIGKILL` cannot be caught, so the
  reference will tell the reader to type `reset`;
- on Windows, at console control events.

Every one of these is a test (§6).

### 2.3 The `term` module, sketched

The design is for review, so the names are placeholders:

- `term.can()`: whether stdin and stdout are a terminal at the floor, and if
  not, why not, as text.
- `s = term.open({ mouse: true })` enters the alternate screen and raw mode.
  `s.close()` leaves both.
- `s.size()` returns `{ columns, rows }`.
- `s.put(column, row, text, style)`, where a style is a record
  (`{ fg, bg, bold, underline, reverse }`); `s.fill(...)`; `s.cursor(...)`.
- `s.show()` draws the changed cells.
- `watch(s.keys)` delivers `{ key, text, ctrl, alt, shift }` one at a time on
  the event loop, as `watch(stdin.lines)` delivers lines. It also delivers
  `s.mouse`, `s.resized` and `s.pasted`.

It is waited and watched in the same way as `net`, so it composes with
servers, timers and actors.

### 2.4 What increment 1 built, and where it moved from the sketch

Built 2026-11-19: `src/term.c` (plain C: decoding, the screen buffer and its
redraw, widths, raw mode and its restoration), `src/modules/term.c` (the
module), `src/term_widths.h`, generated by `tools/make_term_widths.py` from
the Unicode Character Database 16.0.0. Where it differs from §2.3's sketch:

- **No handle.** A process has one terminal, and raw mode, the alternate
  screen and the restoring handlers are process-wide, so the verbs are the
  module's: `term.put`, `term.show`. A handle would have offered a second
  screen that cannot exist.
- **One event queue, `term.events`,** as `net.events` and `http.events` are,
  rather than `s.keys`, `s.mouse` and `s.resized`. Each event has a `kind`:
  key, mouse, paste or resize. Waited (`term.wait`) or watched, never both.
- **`load term` is required.** It binds the global `term`, and `term` is an
  ordinary variable name (a loan's term), so binding it unasked would break
  programs.
- **Mouse reporting and bracketed paste are opt-in** (`term.open({ mouse:
  true })`). With mouse reporting on, a terminal stops selecting text with the
  mouse, which a program that has no use for the mouse should not cost its
  user.
- **Ctrl-C still interrupts** (ISIG is left on). A handler restores the
  terminal and the signal then does what it did before: the program dies by
  that signal, as `run_lock_signal.sh` holds for a `lock`. Ctrl-Z restores
  the terminal and stops, and SIGCONT puts the screen back and delivers a
  resize, so a program redraws.
- **Standard error is held while the screen is up**, in a temporary file
  replayed onto the terminal at the close, the program's end or a fatal
  signal. This was found by asking where an uncaught error's message goes: onto
  the alternate screen, which takes it away with it. It is held only when
  stderr is the terminal.
- **A lone ESC is the escape key after 30 ms** without what would follow it.
  A terminal writes a sequence in one write, so a sequence split across two
  writes 5 ms apart is still decoded as one key (tested).
- **A colour name is xterm's palette value.** `red` is `#cd0000`, so the
  16-colour rendering of a name is that name exactly.

**Measured.** Under valgrind the keys tier shows no invalid access and no
definite leak. An unchanged frame writes 26 bytes: the cursor hidden, the
style reset, the cursor put back. One changed cell writes 33 bytes. The width
table differs from glibc 2.43's `wcwidth` only where glibc follows a newer
Unicode, or makes its own choice:

- U+3164 HANGUL FILLER, which glibc gives 0;
- the interlinear annotation characters;
- the Egyptian hieroglyph format controls;
- U+3248-U+324F.

That comparison was a one-off in building, not a tier: glibc's table moves
with glibc.

**Not reached by any test:** the guard that stops a forked child restoring
its parent's terminal. An actor resets its signal handlers and leaves without
running atexit (measured), so removing the guard stayed green. It is kept as
defence in depth.

## 3. The `tui` backend

### 3.1 Every part, in cells

| part | in a terminal |
|---|---|
| `row`, `column`, `form`, `group`, `stretch`, `separator` | the same layout rules in cells; a `group` is a box with its title in the border |
| `field` | one-line editor; `secret` shows `•` |
| `button` | `[ Save ]`; the `default` one marked, Enter presses it |
| `check` | `[x] label` |
| `text` | a label |
| `choice` | a field that opens a list below it |
| `memo`, `source` | a multi-line editor: the biggest single piece of the backend |
| `table`, `tree` | a grid with headings and scrolling. `to ask` (rows asked of the program) is natural here: a terminal draws only the visible rows anyway |
| `menu`, `item`, `popup` | a menu bar (F10 or Alt+letter) and drop-down overlays |
| `tabs`, `tab` | a tab line; Ctrl+PgUp/PgDn |
| `split`, `scroll` | a divider moved with the mouse or a key; scrolling regions |
| `toolbar`, `status` | a row of buttons; the bottom line |
| `docks`, `dock` | not drawn: the `docking` group (§3.2) |
| `markdown` | not drawn: the `notebook` group |

Turbo Vision and its successors showed that every one of these works in a
terminal, mouse included.

### 3.2 Its groups, and so the common core

Of the eight groups in `docs/ui_design.md` §6:

| group | `tui` | why |
|---|---|---|
| `windows` | no | one screen; a modal is an overlay on it, so `open_modal` and the dialogs still work |
| `docking` | no | panes yes (`split`), floating docks no |
| `stylesheet` | no | QSS is Qt's; the theme **record** is every backend's (§3.3) |
| `figures` | no | no SVG or PNG; `ui.capture` has a text answer instead (§6) |
| `notebook` | no | blocks between lines and rendered markdown are QTextDocument's |
| `icons` | no | an icon from a file is every backend's, and draws as nothing; see Q4 |
| `highlighting` | **yes**, with libxml2 | the KSyntaxHighlighting definitions are XML, read here in C (§3.9); a build without libxml2 lacks the group |
| `file_dialogs` | **yes**, our own chooser | a terminal has no native dialog to defer to |

So the **common core is every verb and word no group holds**, plus
`file_dialogs` once the TUI has its chooser. That is what a program written
"within the limits" uses, and the test machinery for it already exists: every
GUI suite runs once with every group withheld (`tests/ui_backend.sh`), which
is exactly the TUI's position. A tier that passes withheld on Qt should pass
on the TUI.

### 3.3 Words whose meaning moves

- **Sizes.** `width`, `height`, `min_width` and the rest are pixels on Qt.
  A cell is not a pixel. Q3 asks how a size crosses over.
- **`font_size`** has no meaning in a terminal; the terminal's font is the
  user's.
- **The theme record** (`bg`, `base`, `ctl`, `hi`, `fg`, `faint`, `line`,
  `sel`, `on_sel`) maps onto cell colours directly. It degrades with the
  colour depth (§2.2). Its icon-file fields draw nothing.
- **`colors`** reads back the colours a part is drawn in, as Qt's reads its
  palette. Measured in building (§3.8), it reads the colours put in the
  screen's cells, before the redraw degrades them to the terminal's depth,
  and a colour left as the terminal's own is not in the answer, since this
  side cannot know it.

### 3.5 What increment 3 built, and what it decided

Built 2026-11-19: `src/modules/ui_tui.c`, a `GbUiBackend` table setting all
149 members (the 149th, `cell_size`, was added for sizes in characters, and
Qt answers it from its font). It covers the containers `row`, `column`,
`form` and `group`, the leaves `stretch`, `separator`, `field`, `button`,
`check` and `text`, form binding, and the events `pressed`, `changed`,
`entered`, `context` and `closed`. Every other part is refused, naming the
increment that draws it. Decisions made in building:

- **A retained model.** The backend keeps the tree, lays it out in cells
  (preferred sizes, then spare room by `grow:`, a stretch taking 1 and, in a
  row with no `grow:`, a field taking 1 as Qt's line edit does), draws it into
  term.c's screen buffer, and term.c writes only what changed.
- **Ctrl-Q closes the window**, as a window manager's button does: the
  `closed` handler once, then the front's bookkeeping. Ctrl-C still
  interrupts the program.
- **Enter on a check presses the default button**, as in a dialog. An early
  version pressed the check, which undid the Space before it: measured in the
  first live run.
- **The ordinary screen comes back when `ui.run` returns**, with the windows
  still open and readable (rule 5). The next `ui.run` draws them again. Output
  printed after a run would otherwise have vanished with the alternate
  screen.
- **No terminal, for a test**: `GBASIC_TUI_SCREEN=COLSxROWS` and
  `GBASIC_TUI_DUMP=PATH`, this backend's `offscreen`.
- **Left out of the other GUI suites' passes** (`tests/ui_backend.sh`,
  `UI_SUITES_SKIP_BACKENDS`) until it covers the core, since each `[C]` tier
  using a table or a dialog would fail for a reason already known. (Since
  increment 6 it is in them all, §3.8.)
  `tests/run_ui_tui.sh` measures it, with a fourth tier class, `[T]`, for this
  backend's own tiers.
- **The automatic choice reads `needs:` from every declared window**, noted
  as each declaration becomes a value, before `main`. The backend is chosen at
  the first `ui` verb, which is usually before the window it is for.
- **A size is a field's own.** A field's caption beside it is not counted,
  as Qt measures its line edit without the label. `ui.read(w, f, "width")`
  answers pixels at 8 a cell.

**Measured:**
- six perturbations each turned the suite red: a disabled part taking focus,
  Enter pressing a check, the rule never choosing the terminal, the rule
  ignoring `needs:`, the screen kept after a run, and `max_width` ignored;
- valgrind reports no invalid access, no definite leak and no descriptor left.

**Found in building:**
- the Makefile's dependency list for `eval.o` lacked `ui_tui.c`, so a rebuild
  kept a stale backend, and several probes went to code that was
  not running. It is listed now;
- a bare `stretch` or `separator` is reported seven lines later as
  "unexpected PROGRAM" (DOGFOOD).

### 3.6 What increment 4 built, and what it found

Built 2026-11-20 in `src/modules/ui_tui_parts.c`, included at the end of
`ui_tui.c`, whose core calls its `tuip_` hooks for measuring, laying out,
drawing, keys and the mouse. It covers:
- `table` (held and `to ask`), `tree` and `choice`;
- the furniture: `menu`, `item` and `popup`, `toolbar` and `status`;
- the regions: `tabs`, `split`, `scroll` and `cells`.

The keys are in `docs/reference.md`, *The terminal backend*. Decided in
building:

- **One overlay** serves a choice's list, a menu's entries and a popup. A
  submenu is a second overlay beside the first. An open overlay takes every
  key and every press until it closes.
- **The furniture is held by the root but not laid out with the content.**
  The title is the top line, then the menu bar, then the toolbar; a status
  bar is the last line.
- **An asking table is asked only for the rows on screen**, once a frame,
  before its columns are measured. Because of that, `show` no longer draws at
  once: as Qt paints only in its loop, the first frame is the run's first
  turn or the next `ui.key` or `ui.click`. A size read with `ui.read` lays
  out first.
- **A tree shows every row.** Folding waits; so does dragging a split's
  divider, whose proportions `ui.resize_panes` sets.
- **Every handler connected to an event runs**, in the order connected, as
  every Qt slot does. Increment 3 kept only the last, which
  `qt_which_test`'s two `hold.changed` handlers showed.

**Measured against the contract Qt is already held to.** Each `[C]` fixture
was run on `tui` with no terminal (`GBASIC_TUI_SCREEN`, which
`tests/ui_backend.sh` now sets for a `tui` pass).
- These pass outright: `run_ui_tree`, `run_ui_align`, `run_ui_split`,
  `run_ui_frame` and `run_ui_children` (except tiers needing `windows`), and
  in `run_ui_backend` the cells, tabs, menu, properties and loop tiers.
- Every other fixture stops at a `memo` or a `source`, which increment 6
  draws. With those swapped for fields in a scratch copy, the which, context,
  panes and ask fixtures pass every check, and so do choice's 24.
- So the backend stays out of the other suites' passes until increment 6.

**Found in building:**
- Two of Qt's refusals named a Qt class (`QTableWidget`, `QAction`), against
  the seam's rule that a refusal names gBASIC's words. Both name the part now
  (`table`, `item`).
- `qt_panes_test` held a split's sizes to a 900-pixel window. A terminal
  window is the screen, so the check reads the window's own width back.
- Every tier that unsets the display now also unsets `GBASIC_TUI_SCREEN`, or
  the terminal backend could still draw.
- A `separator` in a menu needs `()` too: the DOGFOOD entry on bare
  `stretch` covers it.

**Perturbed, each red:**
- the overlay stopping on a separator;
- `chosen` firing with no change;
- alignment ignored;
- F10 removed;
- only the first handler running;
- an overlay press one row off, which a live mouse press on a menu entry
  catches.

### 3.7 What increment 5 built, and what it decided

Built 2026-11-20 in `src/modules/ui_tui.c`: `ui.inform`, `ui.confirm`,
`ui.answer` and `ui.dialog()`; the file chooser (`ui.choose_file`,
`ui.choose_save`, `ui.choose_folder`), so the backend now declares
`file_dialogs`; and `ui.open_modal`, `ui.accept` and `ui.reject`. The keys
are in `docs/reference.md`, *The terminal backend*. Decided in building:

- **A dialog is a box drawn over the window, and a modal window is too.**
  A modal window is laid out by its content, centred, with its title in the
  top border. Each root now carries the frame it occupies, so a modal
  window's menu bar and status bar are drawn inside its box, not across the
  screen.
- **One loop, turned again inside** (`tui_loop`), as Qt's `exec()` turns
  its loop again: `ui.run` turns it until the windows are gone, and a box
  turns it until it has its answer. gBASIC's sources are served all the
  while, which is what lets a timer answer a box (`ui.answer`).
- **The loop does not re-enter gBASIC's tick.** A box raised inside a
  watcher body turns the loop with that tick still on the stack. The first
  build ran the tick again there, so the webserver's loop ran inside its own
  iteration, a path nothing exercises, and run_ui_dialog's `modal extent`
  tier saw the server answer where Qt's stalls. Qt never re-enters a timer
  whose slot is running, so a box raised there stalls every source on both
  backends now, which is `include/ui_backend.h` rule 2.
- **A stop ends every turning**, the innermost first, as Qt's `exit()`
  ends each nested loop: `ui.stop()`, or a tick answering 0. Found the hard
  way: a fixture whose watcher body raised stopped the program, but the box
  it had put up stayed for ever, because the nested loop ignored the tick's
  0. A box ended by a stop answers as a cancel.
- **`open_modal` needs no `windows`.** This note's §3.2 says a modal window
  is an overlay on the one screen; `docs/ui_design.md` §8 step 5 had counted
  it as a second window while Qt was the only backend. The front now asks
  for `windows` only for `ui.open` of a second window, and
  run_ui_vocabulary's `groups` tier holds the control: `open_modal` over an
  open window runs with `windows` withheld.
- **The chooser is our own**, as §3.2 said: the folder, its entries
  (folders first, `..` at the top, hidden files left out, as Qt's chooser
  leaves them), a name field, the filter and two buttons. Moving onto an
  entry puts its name in the field, as Qt's does. A person's Open and
  `ui.answer(path)` go through one rule (`tui_chooser_take`), so a name the
  box refuses is the name `ui.answer` refuses, in the same sentence Qt's
  answers with. In the box it is said shorter, `Not taken: ...`. A save
  chooser needs the file's folder to exist, and does not ask before
  replacing a file.
- **Paths are made absolute by their letters**, `.` and `..` taken out and
  links not followed, as Qt's `absolutePath()` gives them, so `folder:`
  reads back as the program wrote it.

**Found in building:** a handler that closed its own window freed the tree
while the press that fired it still read the part: a use-after-free from
increment 4, which valgrind found when run_ui_modal's fixture first ran on
`tui`. `include/ui_backend.h` rule 7 already said a close under a handler
waits for `retire_drain`, and the terminal's `retire_drain` was empty. It
holds such a window now, and frees it when no handler is on the stack.

**Measured against the contract.** run_ui_dialog, run_ui_chooser and
run_ui_modal pass on `tui` with every check Qt is held to, and with the five
suites increment 4 measured, they now run on it in the gate: a suite that
passes there says `UI_SUITE_ON_TUI=1` (`tests/ui_backend.sh`; replaced by
the opt-out in §3.8). A new
`[C]` tier in run_ui_dialog, `stop`, holds the stop rule on both backends:
`ui.stop()` inside a box answers it `false`, and a raise in a watcher body
ends the program with that raise. Qt already behaved so.

**Perturbed, each red:**
- No as the default answer made Yes (`dialogs`);
- hidden files listed (`dialogs`);
- Escape not rejecting a modal window (`dialogs`, by its bound);
- the tick re-entered (run_ui_dialog `modal extent`, on `tui`);
- a stop ignored inside a box (run_ui_dialog `stop`: both cases hang to
  their bound);
- a closed window freed under its handler (run_ui_modal `valgrind`, on
  `tui`);
- a cancelled chooser answering the name in its field (run_ui_chooser
  `semantics`, 2 mismatches);
- `open_modal` needing `windows` again (run_ui_vocabulary's control, and
  `dialogs`). That control first compared stderr too, and Qt's locale
  warning made it red unperturbed; it reads the answer alone now.

### 3.8 What increment 6 built, and what it found

Built 2026-11-20 in `src/modules/ui_tui_editor.c`: `memo` and `source`, with
every editor verb (`ui.caret`, `ui.caret_to`, `ui.select_text`,
`ui.selection`, `ui.replace_text`, `ui.highlight_text`, `ui.highlights`) and
a `source`'s margin (`line_numbers:`, `marks:`, `ui.mark`, `ui.marks`,
`ui.gutter`, `ui.gutter_press`, `ui.gutter_click`). Also built, because the
suites needed them once they could run here: `ui.copy` and `ui.clipboard`,
and `colors`. The keys are in `docs/reference.md`, *The terminal backend*.
Decided in building:

- **One editor, two setups**, as Qt's two are one QPlainTextEdit: a `memo`
  wraps after the last space that fits and its tab is 8 cells; a `source`
  does not wrap, the view following the caret sideways, and its tab is 4.
- **An editor has a frame**, since an empty text box in a terminal is
  otherwise blank cells. The frame is bold while the editor has the focus,
  and its right side carries a thumb showing how far down the view is.
- **The text is the node's text**, a line feed between lines, so
  `ui.read`, `ui.write` and a form's binding reach it as they reach a
  field's. Every position goes through one resolver with Qt's rules and
  sentences: 1-based lines and codepoint columns, a line past the end
  refused with the real length, a column past its line clamped.
- **Tab is the editor's**, as on Qt; Shift+Tab and Ctrl+Tab leave. A
  read-only editor lets Tab and Enter go to the window.
- **Undo keeps whole texts**, at most 400 steps. Typing joins the step
  before it; a move, a delete, a paste or anything a program does starts a
  new one. `ui.write` clears the history, as Qt's `setPlainText` does, and
  `ui.replace_text` is one step however many edits it carries.
- **A marked range moves with its text** when an edit comes before it, as a
  QTextCursor does. Marks in the margin do not: they are what the program
  set, unfiltered, as on Qt.
- **The label is Qt's rule**: in a form it is the row's caption; anywhere
  else it is the first text.
- **A mark is one glyph a line**, the most urgent: ● a breakpoint, `E` an
  error, `W` a warning, `i` a note, § a section.
- **`ui.scrolled` counts lines for an editor**, because Qt's does: a
  QPlainTextEdit's scrollbar counts lines, not pixels. Measured by
  run_ui_wheel and cookbook recipe 19, which agree with Qt only that way.
- **The clipboard is the program's own.** A terminal will not say what its
  clipboard holds, so `ui.clipboard` answers what this program last copied:
  `unknown` before any copy, `""` after copying `""`, NULs carried. A copy is
  also sent to the terminal as OSC 52, for the terminals that pass it on.
- **`colors` reads the colours given to the screen's cells**, before the
  redraw degrades them. A colour left as the terminal's own is not in the
  answer, since it is not known here. §3.3 said "after degrading", which
  nothing here can read; corrected there.

**Found in building:**
- **A program's text into a field fired no `changed`**, where Qt's
  `textChanged` fires for `ui.write`, `ui.set` and a form filling the field,
  as well as for typing. Cookbook recipe 06 found it on `tui`: its Save item
  stayed disabled after the program wrote a title. A check written by the
  program now fires `changed` too, as Qt's `toggled` does. The drive
  fixture's expected lines gained `name changed to [Bo]`, the form filling
  its field at open, which Qt prints too (measured).
- **An unset `max_length` read 0**, where Qt's QLineEdit reads 32767. It
  reads 32767 now (cookbook recipe 01).
- **run_ui_reentry's `depth` tier was Qt's own**, a trace in
  `qt_backend.cpp`, but classed `[C]`; on `tui` it measured nothing and
  failed saying so. It is `[Q]` now.
- **run_ui_modal killed processes by name**: any interpreter running
  `tests/qt_modal_test.bas`, which run_ui_reentry's depth tier also runs.
  With the two side by side, as the gate may run them, one could kill or
  count the other's. Seen once, in a run of every GUI suite four at a time:
  the modal suite's terminal pass reported "the valgrind tier left a gbasic
  process behind". Its runs now carry a mark of their own (`modal-suite-$$`),
  and its sweeps match only that. Separately, one run of the modal suite
  failed in its terminal pass with output I did not keep; twelve runs
  since have passed. Not explained.
- **Three cookbook recipes print facts about their backend**: a terminal's
  `font_size` reads 0 until a program sets one (its font is the user's); an
  odd number of cells cannot be split evenly; a viewport asks for the cells
  it shows, and a terminal's shows fewer. Each holds the terminal's lines in
  `<recipe>.tui.out` beside the `.out` the page quotes
  (`tests/cookbook_harness.sh`).
- **run_ui_keys stays off the terminal**, saying why: its refusals name
  Qt's classes (`QPushButton`, `QAction`, `QLineEdit`) where the
  vocabulary's words belong, and a `button` takes no shortcut here yet. The
  class names are a leak in Qt's own sentences, for the ergonomics sweep.

**Measured against the contract.**
- **Every GUI suite runs on this backend now.** `tests/ui_backend.sh` runs
  each on every built backend by default. A suite that cannot yet pass here
  names why in `UI_SUITE_OFF_TUI`, and its pass prints that reason as a
  SKIP. 28 of the 29 run here and pass; run_ui_keys is the one off.
- **The examples target is met.** Of the 14 in `examples/ui`, 10 run in check
  mode. `editor` (`highlighting`), `notes` and `theme` (`stylesheet`) and
  `shell` (`icons`) are refused by name for a group the terminal lacks.

**Not done here:** no copy, cut or paste keys inside an editor (a
terminal's own paste arrives as a bracketed paste and is taken), no find
box of its own (the verbs are there for a program's), wrapping only at
spaces, and no colours for code, which is increment 7.

**Perturbed, each red:**
- wrapping cut inside a word (`editor`: the golden, the hand-worked Up, the
  wrap check);
- typing never joining a step (`editor`: one Ctrl+Z took back one letter);
- `ui.replace_text` a step per edit (`editor`: one Ctrl+Z undid one of two);
- a marked range left where it was through an edit above it (`editor`);
- Tab not taken by the editor (`editor`);
- an editor never freed (run_ui_tui `valgrind`, a definite leak);
- a program's text into a field firing no `changed` (cookbook recipe 06, on
  `tui`);
- an editor's scroll answered in sixteenths, as a table's is (run_ui_wheel,
  on `tui`: 640 for 40);
- an empty clipboard answering `""` (run_ui_clipboard, on `tui`).

**Three went GREEN first**, and each is now a check: Up and Down keeping no
column (the fixture never crossed a shorter line; it now goes Up onto line
8 and back to column 50), a margin line off screen pressed anyway (nothing
pressed one; the refusals fixture now does, and Qt refuses it in the same
sentence), and a memo's label taken as a caption outside a form (nothing
read it back; the refusals fixture now reads `notes`). Each is red now.

### 3.9 What increment 7 built, and what it found

Built 2026-11-20: `src/ksyntax.c` (`include/ksyntax.h`), KDE's syntax
definitions read and applied in plain C, and the terminal's `source` coloured
from it, with the `highlighting` group declared. `language:`, `theme:`,
`ui.highlighting`, `ui.can_highlight` and `colors` answer as on Qt.

**Decided in building:**
- **Our own reader, KDE's rules.** The definitions are XML (libxml2 reads
  them) and the regular expressions are PCRE2's, which is what
  QRegularExpression runs. Everything between is KSyntaxHighlighting's
  behaviour, followed rule by rule: a rule with no attribute takes the
  attribute of the context it switches to, a keyword rule never looks behind
  itself, a regular expression is matched once for the line and its first
  match remembered, `#pop` cannot pop the first context, and so on. Where a
  rule was not known, KDE's output decided (below).
- **The oracle is KDE's own library.** `tests/ui_tui/ksh_probe.cpp` links
  KF6's KSyntaxHighlighting, extracts the 451 definitions and 30 themes it
  carries compiled in, and prints each character's format, colour,
  background and flags; `tests/ui_tui/ksyntax_dump.c` prints the same from
  our engine. Text for each definition is made from its own tokens by
  Python's XML parser (`tests/ui_tui/ksh_corpus.py`), and real files add the
  shapes made text lacks.
- **Definitions are files.** KDE's library carries its definitions compiled
  in, out of reach of anything but itself, so the terminal reads only files:
  the user's and the system's KDE data directories, then
  `GBASIC_SYNTAX_PATH`, the tree's and the installed one, in KDE's order (a
  name met first stays unless a later file has a higher version). A language
  on Qt and not on disk is warning `2115` here and a plain editor. gBASIC's
  own definition is always found. Shipping KDE's 451 (9.8 MB) was not done.
- **Themes are carried.** KDE's 30 themes are compiled into the binary,
  packed (`src/ksyntax_themes.h`, 64 KB, made by
  `tools/make_ksyntax_themes.py` from the probe's extraction; as JSON they
  were 250 KB against 3 MB of text). A theme file in a search path's
  `themes/` is read too. `"light"` and `"dark"` are Breeze Light and Breeze
  Dark, as on Qt; no theme is the one nearest the theme record's `base`, as
  Qt's themeForPalette chooses, or, with no `base`, Breeze Dark where
  `COLORFGBG` says the terminal's background is dark and Breeze Light
  otherwise.
- **A theme gives the editor its background and normal text**, as Qt's
  palette is set from it, and `colors` answers them. The margin, the
  selection and marked ranges keep the window's colours. Strike-through is
  a new cell attribute (SGR 9).
- **Each line's state after it is kept**, an edit forgetting the states from
  its line down and an undo or a whole new text forgetting all; a frame works
  out only as far as it shows.
- **Qt's JIT switch is read here too.** `QT_ENABLE_REGEXP_JIT=0` turns off
  this engine's JIT with Qt's, so one variable serves a valgrind tier: the
  JIT's code reads sixteen bytes at a time past a string's end, which
  valgrind reports though it is not wrong (measured). Output is identical
  either way.
- **It needs libxml2**, so a build without it has no `highlighting` group on
  the terminal (`src/ksyntax.c` is empty there).

**KDE's rules that this engine had wrong at first, each found by the
oracle**, in the order found, from 436 of 451 definitions identical to 451:
- **A guard against endless steps.** A lookahead into a context that falls
  straight back out (Elixir's `~`) never advances. KDE gives the line up
  after 1024 steps at one offset, the rest of it in the format of the moment.
- **A rule's own `additionalDeliminator` and `weakDeliminator`**, not only
  the definition's (ten definitions; Klipper's `Int` among them).
- **Colours by name**: `color="black"`, as QColor reads SVG's names
  (eighteen in use).
- **`context="A!B!C"` pushes three**, in the order written, the last on top
  (Mermaid; the order was taken from KDE's output, after a first guess had
  it reversed).
- **An empty line takes the line end's switch** when it has no empty-line
  switch of its own, so an empty line after a continued one ends what the
  continuation kept open (Gnuplot).
- **The skip cache and captures.** A dynamic regular expression meeting
  different captures clears every remembered skip, and at a line's start the
  captures it compares with are none (Elixir); a dynamic StringDetect or
  DetectChar records no skip and clears nothing (MIB, Elvish: clearing for
  those was a wrong first guess, which the oracle refused).
- **A letter is Unicode's**, from PCRE2's tables: a rough table of blocks put
  U+212F, a letter among the letterlike symbols, outside (Julia).
- **QRegularExpression's JIT stack**, 32 KB growing to 512 KB: with PCRE2's
  default a 12,000-character line's emphasis failed where KDE's matched
  (Markdown, docs/test_suite_history.md).

**Measured.**
- **Identical to KDE's library, character for character:** all 451
  definitions on made text under ten seeds and ten themes; gBASIC's tests
  (32,996 lines) under all 30 themes; C, C++, Python, Bash, Makefile,
  Markdown, XML, JSON, YAML, HTML, CSS and JavaScript files from this tree
  (the largest Markdown, 95,063 lines).
- **Speed:** within a factor of two of KDE's either way (the tree's C, 72,000
  lines: 0.33 s against KDE's 0.48 s; its Markdown, 95,000 lines: 1.67 s
  against 1.03 s).

**Found in building, beyond the engine:**
- **An undo left the colours of the lines below stale**: it restores the whole
  text without the splice that forgets states. The highlight fixture found it
  (an undone comment opener left line 2 plain); every whole-text change now
  forgets them.
- **A `choice` in a form drew no caption** on the terminal, the defect Qt
  once had, and `ui.label` answered any part's label, where Qt's answers only
  a caption (a check's or a button's label is its own text). run_ui_find's
  captions tier, which needs `highlighting` and so first ran here now, found
  both. One rule now, Qt's `label_placed_by_parent`.
- **`ui.click` missed a button below the screen**: the press went to its
  corner, which a 40-row screen did not have. Qt's click reaches the widget
  wherever it is; the terminal now presses a button or check directly when
  the point is not on it.
- **run_ui_source asked for KDE's FreeBASIC**, which only Qt's engine
  carries; its second language is now `tests/qt_source_other.xml`, installed
  by the suite where both engines read it, and its fixture holds one window
  at a time (the terminal has no `windows`). In a terminal a `memo`'s font is
  one width, and that is held there instead (cookbook recipe 09's terminal
  lines are in `09_code_editor.tui.out`).

**Perturbed, each red:** each of the eight engine rules above undone in turn
(the `syntax` tier: a definition, or Markdown's real files, differs from
KDE's); an undo, and an edit, forgetting no states (`highlight`: the line
below keeps its old colour); a theme giving no background, and a format's
bold dropped (`highlight`, the hand-worked colours); `"dark"` meaning Breeze
Light (`highlight`); a carried theme off by one revision (`syntax`: the
header is not the library's); `colors` ignoring a theme (run_ui_source, on
`tui`); a choice in a form uncaptioned (`layout`); `ui.label` answering any
label, and a click missing a part off screen (run_ui_find, on `tui`).
**One went GREEN first**: strike-through never written to the terminal,
which nothing read. run_term's unit tier now holds every attribute to its
SGR number (ECMA-48: 1, 2, 3, 4, 7, 9), and it is red now.

### 3.4 The event-loop contract

The seven rules in `docs/ui_design.md` §4, for this backend:

1. **The backend owns the wait.** It waits on the terminal and on gBASIC's
   sources together, and calls `tick`. It is the same shape as whisker's,
   whose backend owns its loop.
2. **Ticks continue inside a modal.** A modal is an overlay drawn by the same
   loop, so the loop never stops.
3. **`can_display`** is `term.can()`: a question that cannot crash.
4. **Closing the last window ends `run`.**
5. **`stop` ends `run`** and leaves the window state readable. The screen is
   restored.
6. **A modal's four endings** are front-end behaviour, as on Qt.
7. **Re-entry is capped**: the same front-end guard as on Qt.

## 4. Choosing a backend: a switch in gBASIC, not a build option

**Most of this already exists.** A program never names a backend. Where a
build has more than one, the environment variable `GBASIC_UI` chooses at run
time (`docs/reference.md`, `ui` section). Build options only decide what is
compiled in: Qt needs its libraries installed, and **the `tui` backend needs
nothing**, so every build has it, the Windows one included.

**Proposed: when nothing is named, choose by what is there.**

1. `GBASIC_UI`, or a new `--ui=<name>` flag, when given. A name the build
   lacks is refused, as now.
2. Otherwise, if a display is reachable (`ui.can_display()`), the first GUI
   backend the build has.
3. Otherwise, if stdin and stdout are a terminal, `tui`.
4. Otherwise, the refusal that exists today, naming why.

The same program then opens a window at a desk, and runs in the terminal
over SSH or Transward, with nothing changed.

**Proposed: a program may say what it needs, rather than which backend.**
For example, a `window` option `needs: ["docking"]`, or `ui.need(...)`
before the first open. A program that says so is never given a backend
lacking those groups. It gets the next one that has them, or a refusal
naming the group. Naming a toolkit in the program is what would break "one
program, three backends", so it stays impossible, as `docs/ui_design.md`
ruled.

## 5. Whisker

Whisker is to have capabilities beyond Qt's, and its hosts are SDL and the
browser. Two things need settling with the whisker session, before code:

- whether whisker means to draw into a terminal at all, which would make
  this backend partly its business;
- that its new groups are named in the same table (§3.2), so a program asks
  `ui.can` about whisker's extras as it asks about Qt's.

## 6. How it is tested

- **The `term` layer, through a pseudo-terminal.** `run_repl.sh` already
  drives the prompt through a pty with python3. The same harness sends key
  sequences, resizes the pty and reads back the screen by interpreting the
  escape sequences with an independent emulator: `pyte`, a Python terminal
  emulator, so the oracle is not our own code. It is packaged
  as `python3-pyte` (0.8.0 on Ubuntu; not installed here on 2026-11-18), and
  those tiers skip without it, as the python oracles elsewhere do. Every restoration path in
  §2.2 is a tier: after each ending the pty's `termios` must equal what it
  was before.
- **The backend, with no terminal at all.** `ui.capture` answers the screen
  as **text**, one line a row. That gives a golden for every `window`,
  readable in a diff, the TUI's counterpart to the PNG captures.
- **The common core.** Each GUI suite's `[C]` tiers (`docs/ui_design.md`
  §5) run on `tui` through `ui_each_backend`, as they now run on Qt with
  every group withheld.
- **Windows** by hand on native Windows 11, as the Windows port is tested
  (`docs/windows_port_status.md`), until CI has a Windows runner.

## 7. Increments

1. **`term` on POSIX**: raw mode and every restoration path, keys, resizing,
   the screen buffer and its redraw, colour depth, widths; the pty harness
   with `pyte`.
2. **`term` on Windows**: console modes, VT input, the waitable handle in the
   event loop. Measured by hand on Windows 11.
3. **The `tui` backend's first parts**: the layout containers, `field`,
   `button`, `check`, `text`, `separator`, `stretch`; forms bind; text
   capture; the selection rule of §4.
4. **The rows**: `table` (with `to ask`), `tree`, `choice`, `menu`,
   `popup`, `tabs`, `split`, `scroll`, `toolbar`, `status`.
5. **The dialogs**, the file chooser, and `open_modal` as an overlay.
6. **`memo` and `source`**: the editor, with caret, selection, find and
   replace, and gutter marks.
7. **Highlighting** from the KSyntaxHighlighting definitions, which then
   declares the group.

Each increment is gated; the examples in `examples/ui` that need no group
should run on `tui` by increment 5. **Measured at increment 5 (2026-11-20):
not yet, and the target was mis-sequenced.** Of the 14, `customer`,
`invoice`, `layout`, `orders` and `properties` run in check mode; `editor`,
`notes`, `theme` and `shell` are refused by name for a group the terminal
lacks (`highlighting`, `stylesheet`, `icons`), as they should be; and the
other five (`browser`, `clock`, `dialogs`, `menus`, `tabs`) stop at a
`memo`, which increment 6 draws. So the target moves to increment 6.
**Measured at increment 6 (2026-11-20): met.** Ten of the 14 run in check
mode; the same four are refused by name for their groups (§3.8). **At
increment 7, eleven:** `editor` runs too, highlighted (§3.9).

## 8. Questions for Matthew

- **Q1. Our own terminal layer instead of ncurses**, with an xterm-compatible
  terminal as the floor (§2.1)?
- **Q2. (Ruled 2026-11-19: yes.) The selection rule** (§4): GUI when a display is reachable, the
  terminal otherwise, `GBASIC_UI` or `--ui` to force one?
- **Q3. Sizes across backends** (§3.3). Three choices:
  - (a) Size words stay pixels, and the TUI converts at a nominal cell (8 by
    16 pixels).
  - (b) Sizes take a unit (`width: 40 columns`, `width: 300 pixels`), and
    each backend converts the other.
  - (c) Sizes are a GUI matter: the TUI ignores them and lays out by content.

  I lean toward (b): gBASIC already writes units into durations
  (`10 seconds`).
- **Q4. An icon from a file on the TUI:** draw nothing, or the item's text
  alone? Or should `icon:` take a text fallback (`icon: "save.png" or "💾"`)?
- **Q5. `needs:`** (§4): a way for a program to state the groups it requires,
  as a window option or a verb?
- **Q6. Where it sits:** 0.13.0 beside whisker, so the common core is defined
  against three backends at once? My recommendation.


### 8.1 The rulings (2026-11-19)

Matthew accepted all four recommendations as given:

- **Q3, sizes.** A bare number stays pixels, so every existing program keeps
  its meaning. A size may also be written in characters (`width: 40 columns`,
  `height: 12 rows`). The terminal converts pixels at a nominal cell of 8 by
  16, rounding up; Qt converts columns and rows by the font's metrics. The
  terminal lays out by content and treats a size as a minimum, and a window's
  own `width` and `height` mean nothing there, since the window is the screen.
  `font_size` is ignored in a terminal, and the reference says so. **The
  condition:** the `40 columns` spelling goes in only if it costs no grammar
  conflicts, as `10 seconds` did, measured first. Otherwise the spelling is a
  function, `columns(40)`.
- **Q4, an icon from a file.** No icon is drawn; the item shows its label, or
  its name if it has none. `ui.check` warns about an item with an icon and no
  label. There is no `icon: "a.png" or "💾"` fallback spelling.
- **Q5, `needs:`.** A `window` option, `needs: ["docking"]`. The automatic
  choice of backend (§4) skips a backend lacking a named group. `ui.open`
  refuses up front, naming the group and the backends that have it, and
  `ui.check` refuses without a display. It is built with the selection rule,
  in increment 3.
- **Q6, placement.** 0.13.0 carries increment 3 (layout, `field`, `button`,
  `check`, `text`, forms, the selection rule, `needs:`) beside whisker.
  Increments 4 to 7 follow in point releases, and 0.13.0 does not wait for the
  editor. The Windows console (increment 2) is built when a Windows machine is
  at hand; until then 0.13.0's notes say `term` is POSIX only.

§5's two whisker questions were answered from whisker's own repository on
2026-11-19, with no whisker session running. **Whisker does not draw into a
terminal:** its hosts are SDL and the browser, and the terminal its documents
name is RV-9's, used for text entry on a board with no keyboard
(`whisker/docs/decisions.md` D6). **Its groups are this table's:**
`whisker/docs/ui-backend-gap.md` §4 lists a whisker backend's groups as
`windows file_dialogs docking`, from the same eight. So this backend is
gBASIC's alone, and increment 3 waits on nothing.

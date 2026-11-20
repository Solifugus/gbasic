# A TUI: a terminal layer, then a third `ui` backend

**Status:** Partial. §7's increment 1, the `term` module on POSIX, is built
(2026-11-19, §2.4; `docs/reference.md`, *Term Module*; `tests/run_term.sh`).
The Windows console and the `tui` backend are not. §8 holds the questions for
Matthew. **Q1 ruled the same day: our own terminal layer, not
ncurses** (Matthew: "our own terminal layer sounds fine ... time to leave
that to the history books"). **Q2 ruled 2026-11-19: §4's selection rule** (Matthew: "The terminal is a
nice fallback"). Q3 to Q6 are open. Matthew, 2026-11-18:

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
| `highlighting` | **later** | the KSyntaxHighlighting definitions are XML, readable without KDE; a `source` editor can colour from them. It ships without the group, and declares it when that is built |
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
- **`colors`** reads back the colours actually drawn, after degrading, so a
  program can see what the user sees.

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
should run on `tui` by increment 5.

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

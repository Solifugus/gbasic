# A verb for a container whose CHILDREN change

**Proposed 2026-10-19, from converting gBASIC Studio's shell to the Qt
vocabulary.** Written by the Studio side, which is the declared consumer
(`qt_gui_design.md` §0.1), and derived from running the thing rather than from
reading the design.

## The short version

Everything Studio's window needs now exists except one thing: **a container
whose set of children changes while the window is up.** `qt.refresh` solves
exactly this for a `table` (§0.11, written for Studio's file tree by name) and
for a form's subject — and nothing solves it for any other container.

Studio's notebook is that case, and it is not a corner: a tab per open
document is what the application IS.

## What was measured, on Qt 6.10.2 through this backend

| asked | answer |
|---|---|
| `qt.read(win, "book", "count")` on a two-tab `tabs` | **2** — the set is observable |
| `qt.refresh(win, "book")` | refused: *"window `studio` declares no table named `book` and nothing bound to a subject of that name"* |
| `qt.invoke`'s argument count | **zero** (`src/modules/qt.c`: `argc != 3`, then `method` is the third) |
| so `QTabWidget::removeTab(int)` | unreachable by construction |
| so `QTabWidget::setTabText(int, QString)` | unreachable by construction |

The last two are the Qt calls that would change a tab set or a caption in
place. They are not missing from a binding table — there is no door for a
method that takes an argument, which is a deliberate shape of `qt.invoke` and
not an omission to patch there.

**So a caption is fixed at construction too**, which has a second cost worth
stating separately: Studio's tab labels carry a marker — `* ` unsaved, `~ `
changed underneath, `! ` gone — and under GTK that is a label set on every
redraw. Under Qt, using the caption for it would rebuild the window **on the
first keystroke in any document**.

## The workaround, measured end to end, and why it is not enough

A window can be rebuilt. §0.17 already records that closing the last window
returns from `qt.run()` and that reopening is bounded (1.01 memory ratio over
2000 cycles), so the natural shape is a **generation loop**: build the record,
open, run; when the loop returns because a handler closed the window, rebuild
with the new tab set and run again.

Measured working, in one process:

```
gen 1: 1 tabs
gen 2: 2 tabs
generations: 2
documents:   2
carried:     [x = 1]
```

and all four of the things that have to survive do: the buffers (`qt.write`),
the active tab (`qt.set … currentIndex`), the window geometry (read in the
`closed` handler, which §0.17 exists for), and the pane sizes
(`qt.resize_panes`, §0.22).

**Two things do not survive, and the second is the one that rules it out.**

1. **Per-document undo history.** A new `QPlainTextEdit` has an empty stack,
   and setting its text wholesale is not an edit anybody can undo past.
2. **The window's POSITION on screen.** `x` and `y` are read-only on a
   `QWidget` and the record takes `width`/`height` only, so a rebuilt window
   reappears wherever the window manager decides. Measured by reading the
   properties, not by probing a compositor — but it is the same fact either
   way: there is nothing to restore it with.

A window that jumps across the screen when you open a file is worse than any
feature it would be buying. So the generation loop is fine for something rare
— changing the whole workspace — and unusable for opening a file, which is the
commonest thing anybody does in an IDE.

## What is proposed

**One verb, over a NAMED container, taking the same `items` a record already
carries** — so it needs no new vocabulary, no new grammar, and no second
opinion about what a child is:

```basic
qt.rechildren(win, "book", pages)
```

where `pages` is exactly what `items:` holds in the record `qt.open` read.
The reconcile is by **name and word**:

- a child whose `name` and `word` are unchanged is **KEPT**, with its widget,
  its text, its caret, its undo stack and its scroll position intact;
- a child that is new is **built**, through `qt_build_items` — the same code
  `qt.open` uses, which is what stops a window rendering one way at open and
  another afterwards (the rule §0.11's own populate-extraction follows);
- a child that is absent is **deleted**, which `qt.close` already does for a
  window and so is not a new lifetime question;
- a `tab`'s caption is re-read, because it is data in the record and the one
  thing `setTabText` would be needed for.

Spelling is the proposer's weakest claim — `qt.rechildren` is ugly.
`qt.relayout`, or `qt.refresh(win, name, items)` as a third form of the verb
that already means *"re-read what this shows"*, both read better. The last is
probably right: a table's rows and a container's children are the same
question asked of two words, and §0.11 already took one verb over two for
precisely that reason (*"One verb takes either rather than growing a
second"*).

## Why this is the general case and not Studio's special pleading

The survey in §0.26 counted Studio's whole widget surface and found two gaps,
both of which were then built. It counted **widget types**, and this gap is
not a type — it is a property of a container. So the survey could not see it,
and neither could the gap list §0.19 superseded.

Three of Studio's containers have it, and only one has an answer today:

| Studio | container | dynamic because | answer today |
|---|---|---|---|
| the file tree | `gtk.listbox` → `table` | a directory expands | **`qt.refresh`** ✓ |
| the notebook | `gtk.notebook` → `tabs` | a document opens | none |
| the browser's context menu | `Gtk.Popover` → `popup` | the items depend on the row | built once, shown and hidden |

The third is how Studio already solves this under GTK and is worth reading as
the alternative: **build every item there is, and show or hide.** That works
for a menu because the vocabulary is closed and small. It does not work for
tabs, because the set is the user's open files and there is no largest number
of those.

And it generalises past Studio immediately: a list of invoice lines that can
be added to, a wizard whose steps depend on an answer, a dashboard whose
panels a person chooses. §0.9's builder made all of those *expressible* — the
record can hold any number of anything — and they are all frozen the moment
the window opens.

## What a check could observe if the verb did nothing

Offered because §0.26 records that this question decided five designs in one
day, and because the naive implementation here is one that **rebuilds
everything** — which passes any check that only counts children.

- `count` after the call pins the number, and nothing else.
- **A kept child must still hold what was typed into it.** Write text into a
  `source`, reconcile with that part present and one new part added, and read
  the text back: a rebuild answers `""` and a reconcile answers the text.
  That is the one check that separates the two implementations, and it is the
  whole point of the verb.
- **An absent child must be GONE, not merely hidden.** `qt.read` on its name
  must refuse — the rule §0.17 established for a closed window, where a
  widget that is still readable is *"a program reading off a screen nobody is
  looking at"*.
- **A caption must follow its data**, read by `qt.menus`' sibling for tabs if
  one exists, or by `count` plus a positional read.
- And the shrink, which §0.11 records as its own load-bearing check: a
  reconcile that appends rather than rewrites passes every grow-and-change
  check.

## What Studio will do in the meantime

Not block on it. The shell is a pure function to a record now
(`lib/studio_shell_qt.bas`), which is what made this gap findable before any
GTK was deleted, and the conversion proceeds in the order where the notebook
comes late:

1. the shell as a record — **done**, with the first tier in Studio's suite that
   asserts the window's STRUCTURE rather than its text;
2. the browser, the splits and the scrolls — all answered, and the browser is
   a `table` precisely because `qt.refresh` makes its row count free;
3. menus, which §0.7 made better than the GTK original rather than equal to
   it: real `QAction`s with `shortcut:` and `checkable:`, which retires
   Studio's ●/○ glyphs and the one control in it that has no test at all (a
   `Gtk.EventControllerKey` with no way to synthesise a key press);
4. the console, the run strip and the results — the run poller has a home,
   which was the other thing this spike had to measure: **a gBASIC timer fires
   while `qt.run()` holds the thread.** Eight ticks of a `timer.every(0.05)`
   were delivered inside the loop and each one wrote into the window, which is
   exactly the shape of Studio's 16-per-second run poller. §0.4's
   *"`webserver_loop_iterate`, two callers sharing it"* paying off in a way
   that document does not mention.

The notebook is the last thing, and by then this verb either exists or Studio
picks the fallback: one editor and an open-documents `table` as the tab strip,
which costs the per-document undo stack and changes how the window looks. That
is a real answer and not a threat — but it would be the first thing in this
whole conversion that made Studio worse rather than better, and it is worth one
small verb not to.

---

Measured against gBASIC 0.10.0 at `573fded`, Qt 6.10.2, `QT_QPA_PLATFORM=offscreen`.

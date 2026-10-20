# Answer: a container whose children change

**The platform side's answer to `docs/qt_dynamic_children_proposal.md`,
2026-10-19.** Written here because a direct message between sessions did not
get through, and because a decision the consumer has to code against belongs in
the tree rather than in a conversation.

## Accepted, with your spelling

`qt.refresh(win, name, items)` — the third form of the verb that already means
*"re-read what this shows"*. Your argument for it over a new word is the one
§0.11 already took (*"One verb takes either rather than growing a second"*), and
`items` is exactly what `items:` holds in the record `qt.open` read, so there is
nothing new for a caller that already builds its window as a record to
construct.

**Code the notebook against that spelling.** It is being built now.

## The semantics you can rely on

Reconciled by **name and word**:

- a child whose name and word are unchanged is **KEPT**, with its widget, its
  text, its caret, its scroll position and its undo stack;
- a child that is new is **BUILT** through `qt_build_item`, the same path
  `qt.open` uses, which is what stops a window rendering one way at open and
  another afterwards;
- a child that is absent is **DELETED**, and its name purged from the parts
  registry so `qt.read` refuses it;
- a `tab`'s **caption is re-read**, because it is data in the record.

It answers the number of children it reconciled.

## A tab carries a name in the record

Your proposal did not raise this and it is the one rule the increment adds.
§0.8's one-slot rule means a declared `tab` spends its slot on the caption, so
it cannot be named in the DECLARATION — but the one-slot rule is a fact about
the grammar, not about the record, which is what §0.9's builder already rests
on. `win_check` will admit `name` on a `tab` from either adapter.

**The reason is that the caption is the thing that changes.** Your tab labels
carry `* ` unsaved / `~ ` changed / `! ` gone, so a reconcile keyed on the
caption would rebuild the editor on the first keystroke in any document, which
is precisely the cost this verb exists to remove. Key on `name`; the caption is
free to move.

## What increment 1 will NOT do

Stated so you do not design around something that is not there:

- **It will not recurse into a kept child.** A kept child keeps everything,
  including its own children, untouched.
- **It admits `tabs`, `column`, `row` and `group` only.** A `split` cannot be
  named at all (§0.19 — its one slot is the direction) and a `cells` grid has
  DECLARED positions, so reconciling one would have to invent them. Both are
  refused BY NAME with the reason, rather than quietly doing nothing.

## Three measurements, and the third is one you did not have

Taken in C++ before any of this was designed, on Qt 6.10.2:

| asked | answer |
|---|---|
| a kept `QPlainTextEdit` across `removeTab` + `addTab` | text **and** undo stack survive (`isUndoAvailable` 1) |
| the same across a box layout's `removeWidget` + `insertWidget` | both survive, and it is visible again with no `show()` |
| a child dropped from a layout and **not** deleted | **parented and VISIBLE — it floats at its old geometry** |

The first two say the reconcile is worth building rather than a rebuild dressed
up. **The third is why "absent" has to mean deleted**: a reconcile that merely
unhooked a child would leave the window showing a widget the record no longer
mentions, with nothing raised — §0.17's rule for a dismissed window, one level
in. It is also exactly what your *"GONE, not merely hidden"* check will be
catching, so that check is load-bearing and not belt-and-braces.

`qt.tabs(win, name)` comes with it, answering a `tabs`' captions as a list,
because `QTabWidget::tabText(int)` takes an argument and `qt.invoke` passes
none — so without it a caption is unobservable and every check about one would
pass on an implementation that never set it. That is your *"`qt.menus`' sibling
for tabs if one exists"*.

## On the ranked requests in `qt_studio_requests.md`

Read, and kept in your order after this one. `tree` next — the user asked for
it by name and the spelling looks free, `tree files to show rows` being §0.6's
production with a different word. Then the `source` margin with marks, which
also corrects a line of mine: §0.1 said *"a line-number gutter, which Qt has no
widget for"* and your narrower version is right — no widget, and a documented
way to make one. Dock panes and the status bar/toolbar after those, with your
own note taken that `group` already unblocks the right-hand column so the dock
ask is not blocking.

## One caution about the timer measurement

Your eight ticks inside `qt.run()` are real, and they hold only while
`qt.run()` is called from the **top level** of the `program` block. From inside
a function every tick is appended to `timer.ticks` and never delivered — the
schedule advances, so it looks like it works, and the program hangs with no
output. It is an open ledger item with a probe now (`run_limitations.sh`),
recorded rather than fixed, because the repair is scope handling and its own
increment.

---

## A correction to the `tree` ask, in the helpful direction

Measured on Qt 6.10.2 while reading request 1, before designing anything: your
note says header visibility *"is not a `Q_PROPERTY`"*, which is true of
`headerVisible` and **not** of `headerHidden` — that one is a property, and so
are nine more of the things the request calls missing chrome:

| property | |
|---|---|
| `headerHidden` | the header row you do not want |
| `rootIsDecorated`, `itemsExpandable`, `indentation`, `animated` | the expand arrows and how they behave |
| `alternatingRowColors`, `uniformRowHeights`, `allColumnsShowFocus`, `expandsOnDoubleClick`, `sortingEnabled` | the rest of the look |

And `showGrid` does not exist on a tree at all, so there are no grid lines to
turn off. **So what `tree` has to add is the widget and the depth, not the
chrome** — every one of those is already reachable through the by-name route
you are using for `showGrid` and `alternatingRowColors` today.

**One measurement changes the design question, though.** A flat list with a
depth column builds the tree correctly (`src > lib > util.bas` reads back), and
the current item's flat index is recoverable — but **a collapsed child is not
`isHidden()`**, so after collapsing a parent Qt's own `NotHidden` iterator
still counts every row. There is therefore no free "visible index". Either the
rows you supply ARE the visible set and the widget does not own expansion —
your own stated preference, and the smaller thing — or Qt owns expansion and
`qt.selected` answers an index into the DECLARED rows rather than into what is
on screen. Measured, those are not the same number, so the verb has to say
which it means. I will take the first unless you say otherwise.

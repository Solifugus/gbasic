/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * THE C FACE OF THE QT BACKEND. Nothing here mentions Qt, and that is the
 * whole point: `src/eval.c` is C11 and Qt is C++ with a code generator, so
 * every other optional module's arrangement -- a `.c` file `#include`d into
 * eval.c -- is unavailable. This header is the seam. `src/modules/qt.c` is the
 * gBASIC-facing half and is `#include`d as usual; `src/modules/qt_backend.cpp`
 * is compiled by the C++ compiler, run through `moc`, and linked in beside it.
 *
 * WHO WALKS THE DECLARATION: the C side. A `window` block binds an
 * introspectable record (docs/qt_gui_design.md §0.2) and eval.c is where a
 * `Value` can be read, so the C side walks the tree and this interface takes
 * one widget at a time. The alternative -- handing C++ a `Value` -- would put
 * the record's representation in two languages, and the record is the thing
 * most likely to grow.
 *
 * EVERY CALL THAT CAN FAIL REPORTS A SENTENCE, never a bare code. These
 * failures are an author's mistakes -- a word with no Qt widget behind it, a
 * property Qt does not have, a signal that widget does not emit -- and the
 * name of the thing is the whole value of the message.
 */
#ifndef GBASIC_QT_BACKEND_H
#define GBASIC_QT_BACKEND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CAN THIS MACHINE OPEN A WINDOW AT ALL? Asked of QT ITSELF in a FORKED CHILD,
 * which is not caution but the only thing that works -- with no display Qt
 * calls `qFatal`, which ABORTS (measured: SIGABRT, exit 134, core dumped), and
 * `qFatal` cannot be caught, so by the time this process knows, it is gone. A
 * crash also takes block-buffered stdout with it, so a program printing its
 * progress prints NOTHING and the failure reads as line 1 -- exactly the
 * defect GTK produced and run_gi.sh records.
 *
 * AND THE ENVIRONMENT CANNOT BE READ INSTEAD, which was the obvious guess and
 * is wrong: measured, Qt found Wayland through XDG_RUNTIME_DIR with
 * WAYLAND_DISPLAY unset. Its own detection is richer than any reimplementation
 * and would drift as Qt changes.
 *
 * 1 yes, 0 no. One fork per process, cached by the caller.
 * docs/qt_gui_design.md §0.3; tests/run_qt_loop.sh asserts all four cases. */
int gb_qt_can_display(void);

/* A connected signal fired. `handler` is the function NAME the declaration
 * registered for it -- by name rather than as a value, which is what
 * `gi_signal_marshal` already does for GTK (§5). */
typedef void (*GbQtInvokeFn)(const char *handler);
/* WHAT IS IN ONE CELL, asked of gBASIC. Answers 1 and fills `*text` with a
 * malloc'd string the caller frees, or 0 if the function is gone or raised --
 * in which case the cell is drawn empty rather than the program stopped, the
 * position a model has to take because `data()` is called during a PAINT and
 * there is nothing sensible to unwind into. */
typedef int (*GbQtCellFn)(const char *fn, int row, int column, char **text);

/* A WINDOW HAS CLOSED, and gBASIC's own registry has to learn about it. The
 * backend cannot do that bookkeeping itself: which declaration a window came
 * from, and what a form captured when it opened, are facts the C side holds.
 *
 * CALLED FOR EVERY CLOSE, however it arrived -- `gb_qt_close`, a `close`
 * invoked by name, or the window manager's own button -- because all three
 * deliver the same QEvent::Close and one hook catches them all. That
 * uniformity is the point: a program must not be able to tell them apart,
 * since a person clicking X is the ordinary case and the other two exist to
 * be drivable. */
typedef void (*GbQtClosedFn)(void *window);

/* ONE ITERATION OF gBASIC'S OWN EVENT LOOP, NON-BLOCKING. Qt owns the wait
 * (direction A, measured in §5.1), so gBASIC's sources cannot have a `poll()`
 * of their own; they ride a Qt timer instead. Returns 0 to ask Qt's loop to
 * stop. */
typedef int (*GbQtTickFn)(void);

void gb_qt_set_callbacks(GbQtInvokeFn invoke, GbQtTickFn tick,
                         GbQtClosedFn closed, GbQtCellFn cell);

/* THE APPLICATION'S OWN STYLESHEET -- the half of theming the by-name property
 * route does NOT already give. Measured before this was written: `styleSheet`
 * is a Q_PROPERTY of QWidget, so `field name( styleSheet: "..." )` already
 * worked with no vocabulary of its own, which is that route (§1) paying off in
 * a way nobody designed. What it cannot reach is ONE sheet for the whole
 * application, which is what anybody means by a theme.
 *
 * IT MUST NOT NEED A DISPLAY. Setting a theme is harmless metadata and a
 * program will want to set it BEFORE opening anything, so the sheet is
 * REMEMBERED and applied when the QApplication is constructed -- and applied
 * at once if one already exists. Refusing until a window is open would force
 * the first window to appear unthemed and then change, which is the one visible
 * consequence worth avoiding.
 *
 * READ BACK FROM QT, not from the remembered copy, so what comes back is what
 * Qt holds rather than what we asked for. */
int gb_qt_theme_set(const char *css, char *err, size_t err_size);
char *gb_qt_theme_get(void);

/* Construct the QApplication. Separate from gb_qt_window because it must
 * happen exactly once and after gb_qt_can_display() has said yes. */
int gb_qt_begin(char *err, size_t err_size);

/* `name` IS THE DECLARATION'S OWN NAME, and it buys four doors for one line:
 * the root widget is registered in the same by-name table every part uses, so
 * `qt.read(win, "app", "windowTitle")`, `qt.set`, `qt.invoke(win, "app",
 * "showMaximized")` and `on app.closed` all reach the window itself through
 * machinery that already existed. MEASURED before relying on it: `windowTitle`
 * is a Q_PROPERTY of QWidget (index 43) and `close()` and `showMaximized()`
 * are both invokable by name (24 and 21), so nothing had to be added for any
 * of them -- which is note 4's by-name argument paying off a fourth time.
 *
 * Until this, `qt.invoke`/`qt.read`/`qt.set` reached PARTS only, so there was
 * no way to address the window at all: no close, no raise, no reading or
 * setting its own title or size.
 *
 * `modal` BUILDS A QDialog RATHER THAN A QWidget, and that is the whole of
 * what a modal window needs from construction. MEASURED BEFORE CHOOSING IT:
 * everything this backend builds works on a QDialog root -- a menu bar (which
 * is a plain QMenuBar in the root's box layout here, not QMainWindow's), a
 * form, tabs, a table, buttons -- and `accept` (39), `reject` (40) and `done`
 * (38) are all invokable BY NAME on one while being ABSENT from a QWidget
 * (-1). So the answer mechanism is Qt's own and the refusal on a non-modal
 * window is Qt's own too.
 *
 * ONLY WHEN ASKED FOR, deliberately. Making every root a QDialog would have
 * worked and would have been a wider change for nothing: a QWidget root then
 * genuinely HAS no `accept`, so `qt.accept` on an ordinary window is refused
 * by the same by-name door everything else goes through. */
void *gb_qt_window(const char *name, const char *title, int width, int height,
                   int modal);

/* A container word (`row`, `column`, `form`, `group`, `tabs`, `tab`). `title`
 * is the caption a `group` or a `tab` carries and is NULL for the others.
 *
 * `name` IS FOR A CONTAINER THAT IS ADDRESSABLE -- `tabs settings`, so
 * `on settings.currentChanged` and `qt.read(win, "settings", "currentIndex")`
 * can find it. NULL for the ones that are not; the load-time pass decides
 * which may carry one, and a `tab` never does (its CAPTION is what a person
 * reads, the rule a `menu` follows). */
void *gb_qt_container(void *parent, const char *word, const char *name,
                      const char *title, char *err, size_t err_size);

/* A leaf word (`field`, `button`, `check`, `stretch`). `name` is the
 * declaration's name, which is how `qt.read`/`qt.write` and a handler's
 * target find it again; `label` is the caption, which for a `field` is the
 * form row's label and for a `check` is the box's own text. */
void *gb_qt_part(void *parent, const char *word, const char *name,
                 const char *label, char *err, size_t err_size);

/* PROPERTIES BY NAME, through QMetaObject -- so a misspelling is refused and
 * naming the property is all a new widget kind needs. The three entry points
 * differ only in the gBASIC value kind they were given. */
int gb_qt_set_text(void *widget, const char *prop, const char *value,
                   char *err, size_t err_size);
int gb_qt_set_number(void *widget, const char *prop, double value,
                     char *err, size_t err_size);
int gb_qt_set_bool(void *widget, const char *prop, int value,
                   char *err, size_t err_size);

/* STRUCTURAL OPTIONS, which are NOT widget properties and so cannot go
 * through the same door: `margin:` and `spacing:` belong to the LAYOUT, and
 * `grow:` is a stretch factor the PARENT layout holds. Keeping them separate
 * is what lets the property path refuse an unknown name. */
int gb_qt_layout_number(void *container, const char *prop, long value,
                        char *err, size_t err_size);
int gb_qt_grow(void *widget, long weight, char *err, size_t err_size);

/* A CELL THAT COVERS MORE THAN ONE COLUMN OR ROW -- `across:` and `down:`
 * inside `cells`, which is the half of tabular placement a box layout cannot
 * do at all. Applied AFTER construction, like `grow:`, because the options are
 * read after the widget exists -- so the widget is re-added at the position it
 * already has with the span attached. Refused outside a `cells`, since in a box
 * layout there is nothing to span. */
int gb_qt_span(void *widget, long across, long down, char *err,
               size_t err_size);

/* WHERE A NAMED PART SITS IN ITS GRID. THE REASON IS AN ORACLE and it is built
 * before the fixture this time, the pattern having cost two increments: every
 * part reads back BY NAME, so a widget placed in the wrong cell -- or with the
 * wrong span, which is the whole point of `cells` -- is invisible to every
 * other check there is. The table's captions and the menu bar's entries were
 * the same problem; here it is the POSITION, which only the layout knows.
 * 1 when the part is in a grid, 0 when it is not. */
int gb_qt_cell_of(void *window, const char *name, int *row, int *col,
                  int *across, int *down);

/* WHICH DRAGGABLE OR SCROLLED REGION A NAMED PART SITS IN -- `split` and
 * `scroll`'s oracle, and it is built before their fixture because without it
 * NEITHER IS OBSERVABLE AT ALL.
 *
 * A `split` SPENDS ITS ONE SLOT ON THE DIRECTION (§0.8 measured that a
 * container carries a name OR options and not both), so it cannot be named --
 * which means `qt.read`, `qt.set` and a handler cannot reach it, and nothing
 * could say whether a splitter was built, which way round it is, or how many
 * panes it got. ASKED OF THE LAYOUT, walking up from a part that CAN be named,
 * which is the rule `gb_qt_cell_of` already follows for a grid: a position we
 * remembered would agree with a placement that was wrong.
 *
 * It also answers for a `scroll`, which CAN be named -- so that half is
 * reachable by the ordinary route too and this is the half that says a part is
 * INSIDE one rather than beside it.
 *
 * `kind` comes back "split" or "scroll", malloc'd. For a split, `direction` is
 * "across" or "down" (malloc'd), `panes` is how many it holds and `index` is
 * which one this part is. For a scroll, `resizable` reports
 * `widgetResizable` -- the one that defaults to FALSE and silently stops
 * content reflowing. 1 when the part is in one of the two, 0 when it is not,
 * which is a different answer from being absent. */
int gb_qt_pane_of(void *window, const char *name, char **kind,
                  char **direction, int *panes, int *index, int *resizable);

int gb_qt_connect(void *window, const char *target, const char *signal,
                  const char *handler, char *err, size_t err_size);

/* A METHOD BY NAME, no arguments -- `animateClick`, `clear`, `setFocus`. The
 * same QMetaObject door the properties go through, and the thing that makes
 * the SIGNAL path testable with no human in front of the screen: invoking
 * `click` on a button emits the real `clicked` signal through the real
 * connection. */
int gb_qt_invoke(void *window, const char *target, const char *method,
                 char *err, size_t err_size);

/* The value a part is showing, and setting it. malloc'd; NULL when the window
 * has no part of that name or that part holds no text. */
char *gb_qt_read(void *window, const char *name);

int gb_qt_write(void *window, const char *name, const char *text,
                char *err, size_t err_size);

/* SET ONE PROPERTY ON A NAMED PART -- the same QMetaObject door the
 * declaration's options go through, reached by NAME at run time rather than by
 * node at construction. Three entry points for the same reason the three
 * setters above have three: the gBASIC value kind decides the QVariant. */
int gb_qt_set_part_text(void *window, const char *name, const char *prop,
                        const char *value, char *err, size_t err_size);
int gb_qt_set_part_number(void *window, const char *name, const char *prop,
                          double value, char *err, size_t err_size);
int gb_qt_set_part_bool(void *window, const char *name, const char *prop,
                        int value, char *err, size_t err_size);

/* A PROPERTY BY NAME, READ BACK -- the other direction of the by-name route
 * the three setters take, and the thing that makes that route's central claim
 * assertable at all. Until this existed a suite could say an option was
 * ACCEPTED and not that it had any effect, so a backend that took every option
 * and applied none passed every check; measured, that is exactly what
 * run_qt_backend.sh's refusal tier could not see.
 *
 * `kind` comes back as 'b' bool, 'n' number or 't' text, DECIDED BY THE
 * QVariant rather than by the caller, so `readOnly` arrives as a gBASIC
 * boolean instead of the string "true" -- which matters because `if
 * qt.read(w, "f", "readOnly")` on the string "false" would be TRUE, every
 * non-empty string being true in this language. `text` is malloc'd and set
 * only for 't'. */
int gb_qt_get(void *window, const char *name, const char *prop, char *kind,
              double *num, char **text, char *err, size_t err_size);

/* A MENU. MEASURED FIRST: the whole shape needs NO NEW GRAMMAR -- `menu "File"`
 * is the production `group "Credit"` already uses, `item open( label: "..." )`
 * is the named leaf, `separator()` is the `stretch()` form, and a submenu is
 * the same production recursively. Only the vocabulary refused them.
 *
 * A MENU BAR BELONGS TO THE WINDOW, not to a layout, which makes this the
 * first directive placed by nothing: `gb_qt_menu` with a NULL parent menu
 * hangs the menu off the window's own QMenuBar, and with one hangs it off that
 * menu as a submenu.
 *
 * AN ITEM IS A QAction AND NOT A WIDGET, which is why the registry holds
 * QObject: an item is addressed by name for `on open.triggered`, for
 * `enabled:`/`shortcut:`/`checkable:` at construction, and for
 * `qt.read(win, "open", "enabled")`, and all three go through QObject. */
void *gb_qt_menu(void *window, void *parent_menu, const char *title, char *err,
                 size_t err_size);
void *gb_qt_menu_item(void *window, void *menu, const char *name,
                      const char *label, char *err, size_t err_size);
int gb_qt_menu_separator(void *menu, char *err, size_t err_size);

/* A TOP-LEVEL POPUP MENU. Its own constructor rather than a third case inside
 * `gb_qt_menu`, because what it takes is different in kind: that one takes a
 * TITLE (the caption on the bar, or on the parent menu for a submenu) and this
 * one takes a NAME. A submenu inside a popup needs nothing new -- it recurses
 * through `gb_qt_menu` with this menu as its parent, which is how `menu
 * "Recent"` inside `menu "File"` already works. */
void *gb_qt_popup_menu(void *window, const char *name, char *err,
                       size_t err_size);

/* THE BAR'S OWN TOP-LEVEL MENUS. THE REASON IS AN ORACLE: without it, SUBMENU
 * NESTING IS UNASSERTABLE. An item keeps its name wherever its menu ends up, so
 * a submenu hung off the BAR instead of off its parent passes every check there
 * is -- measured, that perturbation produced ZERO mismatches. The captions are
 * the only thing that can tell `File > Recent` from `File` and `Recent` side by
 * side, and they pin the ORDER too. Same shape as the table's heading oracle,
 * found the same way. */
int gb_qt_menu_count(void *window);
char *gb_qt_menu_title(void *window, int index);
/* THE ENTRIES OF ONE TOP-LEVEL MENU, by that menu's position on the bar. The
 * same oracle one level in, and it closes the case that is structurally worse
 * than a missing separator: an item added to the WRONG menu keeps its name, so
 * nothing else can see it. A separator's caption is empty, which is how a line
 * shows up in the list at all. */
int gb_qt_menu_entry_count(void *window, int index);
/* `is_separator` IS NOT THE SAME AS AN EMPTY CAPTION, and conflating them cost
 * a perturbation: skipping the separator branch let a separator FALL THROUGH
 * and be added as an action with empty text, which an empty-caption check
 * cannot tell from a real line. A separator HAS no caption where an
 * `item x( label: "" )` has an empty one -- the absent/empty distinction this
 * language makes everywhere else -- so the caller answers `unknown` for one
 * and "" for the other. */
char *gb_qt_menu_entry_title(void *window, int index, int entry,
                             int *is_separator);

/* THE SAME ORACLE FOR A POPUP, by name rather than by bar position. A `popup`
 * is not on the bar at all, so `gb_qt_menu_count` cannot see one -- which is
 * also the check that a popup has NOT quietly been added to the bar -- and
 * without this an item declared in the wrong popup of two keeps its name and
 * is invisible to every by-name check. Answers -1 for a name that is not a
 * popup, which is how the caller tells "no such menu" from "an empty one". */
int gb_qt_popup_entry_count(void *window, const char *name);
char *gb_qt_popup_entry_title(void *window, const char *name, int entry,
                              int *is_separator);

/* A CONTEXT MENU. `popup row_actions` is a menu that is NOT on the bar, and the
 * word exists because the slot does: §0.8 measured that a container carries a
 * NAME OR OPTIONS AND NOT BOTH, `menu "File"` spends its one slot on the
 * caption a person reads on the bar, and a context menu has no bar to be read
 * on -- what it needs is a NAME, so `qt.popup(win, "row_actions")` can address
 * it. The same trade `split across` makes in the other direction, where the
 * direction is what no splitter can do without.
 *
 * MEASURED BEFORE THE WORD WAS CHOSEN, and two results shaped it. A QMenu
 * parented to a widget that HAS a layout is NOT taken by that layout (the
 * layout's count was 2 before and 2 after), so a popup is placed by nothing,
 * exactly like the bar -- which is why it belongs to the window and is refused
 * inside a layout, for the same reason and not by analogy. And `QMenu::popup`
 * and `QMenu::exec` are NOT invokable by name (both -1 against `show` at 18 and
 * `hide` at 19), so `qt.invoke` cannot reach either and a verb is the only
 * route: `show()` would put the menu wherever it last was rather than where the
 * pointer is.
 *
 * IT ANSWERS WHETHER ANYTHING WAS SHOWN, and that is Studio's own question
 * rather than a convenience. Its context menu is built ONCE holding every item
 * there is and HIDES the ones a row does not offer (`visible` is Q_PROPERTY 14
 * of QAction, so `qt.set` already reaches it -- no new capability), and its own
 * comment says an empty menu "reads as a control that failed". Measured, Qt
 * agrees: an all-hidden menu answers `isEmpty()` true and `popup()` leaves it
 * INVISIBLE. So the honest answer is a boolean, not a refusal -- an empty
 * context menu is an ordinary outcome of a row that offers nothing. */
/* `under` NAMES A PART TO POP BENEATH, or is NULL for the pointer. §0.20 left
 * "popup at the pointer only" open, and Studio is the caller that closes it: it
 * builds a TOOLBAR MENU BUTTON (a Gtk.MenuButton with a `\u25be` glyph in the
 * label, its own comment recording that GTK's `always-show-arrow` drew nothing
 * on 4.22), and a menu button's menu belongs under the button rather than
 * wherever the mouse happens to be. Measured: popping at a widget's
 * bottom-left lands exactly there. */
int gb_qt_popup(void *window, const char *name, const char *under, int *shown,
                char *err, size_t err_size);

/* DRIVE A RIGHT-CLICK THE WAY A MOUSE DOES -- the oracle, built before the
 * fixture, which is the discipline §0.8 arrived at by getting it wrong.
 *
 * IT EXISTS BECAUSE OF ONE MEASUREMENT: a `table`, `memo` or `source` does NOT
 * answer a context-menu event sent to the widget itself. Delivered to the
 * QTableWidget it fired the signal 0 times and the event propagated to the
 * PARENT; delivered to that widget's VIEWPORT it fired 1. A real right-click
 * lands on the viewport, so the feature works for a person on all five kinds --
 * and a synthetic event aimed where gBASIC can name reaches nothing, so without
 * this verb the whole path is undrivable and every check would be about a
 * `field` and a `button`, the two kinds that happen not to have a viewport.
 *
 * IT SENDS A REAL PRESS AND RELEASE rather than a bare QContextMenuEvent,
 * because the selection is part of the answer: measured, Qt MOVES THE CURRENT
 * ROW on a right-press (row 3 under the pointer, `currentRow()` 3 afterwards),
 * so `qt.selected` already answers "which row was right-clicked" and the gap I
 * expected to have to close was closed by Qt. A bare event would fire the
 * signal and leave the selection where it was, which is a verb that drives
 * HALF of what a mouse does and would make the row question look unanswerable.
 *
 * `row` IS -1 FOR "WHEREVER THE PART IS", and a real index aims at that row's
 * own rectangle, which is what makes "right-click the third file" writable. */
int gb_qt_right_click(void *window, const char *name, int row, char *err,
                      size_t err_size);

/* THE WHEEL. `on <part>.wheel` is an EVENT and not a signal -- the second
 * thing in this backend that has to be, after `closed`, and for the same
 * reason: a QWidget has no wheel signal, it RECEIVES a QWheelEvent, so the
 * by-name signal route has nothing to find and would refuse a well-formed
 * handler with "a QPlainTextEdit emits no `wheel` signal", true and useless.
 *
 * DERIVED FROM STUDIO, which uses a Gtk.EventControllerScroll for exactly one
 * thing: Ctrl + wheel zooms the editor. Its handler reads `dy`, calls one
 * function and returns TRUE to mean handled -- so the requirement is a
 * direction, the modifiers, and a way to say handled. Nothing more.
 *
 * FIVE MEASUREMENTS, THREE OF WHICH SHRANK IT. A synthetic QWheelEvent IS
 * delivered (a plain one moved the scrollbar 92 -> 95), which is better than
 * GTK -- Studio records its scroll and key controllers as its ONLY untested
 * controls precisely because `Gtk.EventControllerScroll` has no emit, so
 * `gb_qt_scroll` closes an exception that project had to sanction. A
 * QPlainTextEdit does NOT zoom on Ctrl+wheel (the font stayed 9.00), so the
 * gap is real. Ctrl+wheel scrolls FASTER than a plain one (95 -> 111 against
 * 92 -> 95), which is why claiming the event is not optional: a handler that
 * zooms without claiming leaves the editor jumping about while it resizes. A
 * filter CAN swallow it (102 -> 102), and declining leaves plain scrolling
 * intact (102 -> 105). And `font` is Q_PROPERTY 27 reading back as TEXT, so
 * THE ZOOM NEEDS NO VERB -- split the text, change the number, write it back,
 * which is why there is no `qt.zoom` and that is a measurement rather than a
 * deferral: `zoomIn`/`zoomOut` carry a defaulted argument `qt.invoke` could
 * not reach anyway.
 *
 * `dy` IS IN NOTCHES, NOT QT'S 120-UNIT ANGLE DELTA. One click of the wheel is
 * what a person did; 120 is a Qt-ism, and a vocabulary that made a program
 * divide by it would leak the wrong half of the abstraction. Positive is away
 * from the user, which is Qt's own sign -- and the OPPOSITE of GTK's, so
 * Studio's `if dy < 0 then zoom in` inverts when it is ported. */
int gb_qt_wheel_event(int *dy, int *dx, int *ctrl, int *shift, int *alt);

/* CLAIM THE EVENT BEING HANDLED, so the view does not also act on it. Refuses
 * when nothing is in flight rather than being a silent no-op, the rule
 * `gb_qt_answer` already follows: a verb that quietly did nothing would leave
 * an author believing the editor had stopped scrolling for another reason. */
int gb_qt_swallow(char *err, size_t err_size);

/* TURN THE WHEEL ON A PART, the way a mouse does -- and it ANSWERS WHETHER
 * ANYTHING CLAIMED THE EVENT, which is what makes `gb_qt_swallow` assertable
 * at all: a QPlainTextEdit has no scroll position a program can read
 * (`verticalScrollBar` is a method, not a property), so without this the one
 * thing swallowing does would be unobservable. The shape `gb_qt_popup` has.
 *
 * ROUTED THROUGH THE VIEWPORT where there is one, for the reason
 * `gb_qt_right_click` records -- and here it is twice load-bearing, since that
 * is also where the FILTER has to sit: an event filter sees only what is
 * delivered to the object it is installed on. */
int gb_qt_scroll(void *window, const char *name, double notches, int mods,
                 int *claimed, char *err, size_t err_size);

/* WHERE A PART IS SCROLLED TO, and the write half beside it.
 *
 * IT EXISTS BECAUSE A PERTURBATION WENT GREEN. `gb_qt_scroll` answers whether
 * anything CLAIMED the event, and a filter made to record the claim and then
 * return FALSE -- so the claim is reported while the view scrolls anyway --
 * was caught by NOTHING: the answer agrees with `gb_qt_swallow` having been
 * called, whatever Qt then did with the event. That is agreement rather than
 * correctness (PLAT-NUMFMT's lesson), and the only honest observable is the
 * position itself. So the claim "swallowing stops the view" is now asserted
 * against the view.
 *
 * AND IT IS NOT A TEST HOOK. Reopening an editor where somebody left it is
 * what Studio already does for its panes, and a program could not ask: a
 * QAbstractScrollArea's scrollbar is reached by a METHOD, not a property, so
 * the by-name route cannot see it. BOTH DIRECTIONS, because a reader without a
 * writer is the shape `webclient`'s read and write halves had -- a door that
 * opens one way.
 *
 * ANSWERS `unknown` FOR A PART THAT DOES NOT SCROLL, which is a different
 * claim from "scrolled to nowhere": a `button` has no position, where a
 * `source` at the top has position 0. */
/* THE PANE SIZES OF THE SPLIT A PART IS IN, and the write half. §0.19 left
 * this open for two reasons and both still hold: `setSizes` takes a
 * QList<int>, so measured, `sizes` is NOT a property (-1) and `setSizes` is
 * NOT invokable by name (-1), which is the whole of the by-name route this
 * backend otherwise reaches everything through; and a `split` CANNOT BE NAMED,
 * its one slot being the direction, so there is nothing to address.
 *
 * SO IT IS ADDRESSED BY WALKING UP FROM A PART -- the rule `gb_qt_pane_of`
 * already follows, which is why this needed no new spelling.
 *
 * STUDIO NEEDS IT: its shell reads `split.position` for three splits out of a
 * saved session and writes them back on exit, so a converted Studio without
 * this forgets its layout every time -- a feature it already has.
 *
 * A LIST AND NOT A NUMBER, which is a difference from GTK rather than a
 * preference: a Gtk.Paned has exactly two children and one divider, where a
 * QSplitter holds any number.
 *
 * `count` IS AN IN-OUT. Called with *count 0 it only reports how many panes
 * there are; called with room it fills `out`. Two calls rather than an
 * allocation crossing the seam, which is the shape `gb_qt_menu_count` and
 * `gb_qt_menu_title` already have. */
int gb_qt_pane_sizes(void *window, const char *name, int *out, int *count,
                     char *err, size_t err_size);

/* SET THEM. A COUNT MISMATCH IS REFUSED, and that is measured rather than
 * cautious: one size on a two-pane splitter COLLAPSES the second (Qt answered
 * [774, 0]) and three sizes honour NONE of them ([310, 464]) -- both perfectly
 * ordinary-looking layouts that are not what was asked for, which is the
 * silent class this backend is tested against.
 *
 * WHAT IT SETS IS A RATIO IN EFFECT. `setSizes` is not exact -- asked for
 * 260/540 of an 800-wide splitter it gave 252/522, the difference being the
 * handle and the layout's margins -- and a window resize re-proportions
 * (194/580 became 244/730). So a caller stores numbers whose MEANING is their
 * proportion, and nothing here promises to hand the same ones back. */
int gb_qt_resize_panes(void *window, const char *name, const int *sizes,
                       int count, char *err, size_t err_size);

/* AN ICON FROM TEXT is done inside `set_property`, the ONE door the
 * declaration's options and `qt.set` both go through -- because measured,
 * `setProperty("icon", QString(...))` returns FALSE for both a theme name and
 * a file path and the icon stays null: QVariant will not convert a string to a
 * QIcon, so this is the single property the by-name route cannot carry by
 * itself. Three sources, told apart by facts rather than guesses (a `SP_` name
 * is one of QStyle's 81 StandardPixmap keys, an existing file is asked of the
 * filesystem, anything else is a freedesktop theme name), and the two failures
 * answered differently: a path that is not there is REFUSED and a theme name
 * this machine lacks WARNS, because measured, a headless machine has no icon
 * theme at all and refusing would break `icon: "document-save"` on exactly the
 * machines the release targets.
 *
 * WHETHER A PART HAS ONE is its own question, and it had to become one: a
 * QIcon's `toString` is EMPTY whether or not an icon is set -- measured, a
 * button carrying a real PNG and one carrying nothing both answered "" -- so
 * the by-name READ route cannot tell them apart. It refuses now, naming this,
 * which is also the only thing that makes a declared `icon:` observable at
 * all. And a program needs it: an icon the theme lacks is a warning, so
 * falling back to a text-only button means being able to ask. */
int gb_qt_has_icon(void *window, const char *name, int *has, char *err,
                   size_t err_size);

int gb_qt_scrolled(void *window, const char *name, double *at, char *err,
                   size_t err_size);
int gb_qt_scroll_to(void *window, const char *name, double at, char *err,
                    size_t err_size);

/* The modifier bits `gb_qt_scroll` takes, so a caller does not depend on Qt's
 * own values. */
#define GB_QT_MOD_CTRL  1
#define GB_QT_MOD_SHIFT 2
#define GB_QT_MOD_ALT   4

/* A TABLE. ITS PARTS ARE COLUMNS, NOT WIDGETS (§7), so it is built in ONE call
 * from the declaration's whole `field` list rather than by the part walk that
 * every other container uses -- a column has no widget of its own, and handing
 * each one to `gb_qt_part` would make a QLineEdit per column heading.
 *
 * `headers` is one caption per column, in declaration order, which is also the
 * order the cell calls below address. The table registers under `name` in the
 * same registry a part uses, so `qt.read(win, "orders", ...)`, a handler and
 * `gb_qt_set_display_only` all find it with no second lookup.
 *
 * ROW COUNT IS SET SEPARATELY from the cells because the subject's length is
 * not known until the bind pass reads it, which is after the window is built. */
void *gb_qt_table(void *parent, const char *name, const char *const *headers,
                  int columns, char *err, size_t err_size);
/* FILL A `choice`'S LIST. Two calls rather than one taking an array, because
 * the C seam carries no gBASIC types: clear it, then add each item. `clear`
 * also resets the selection, which is the honest behaviour -- a list that has
 * changed may not contain what was chosen. */
int gb_qt_choice_clear(void *window, const char *name, char *err,
                       size_t err_size);
int gb_qt_choice_add(void *window, const char *name, const char *item,
                     char *err, size_t err_size);

/* A TABLE WHOSE ROWS ARE ASKED FOR RATHER THAN HELD -- `table results to ask
 * cell_of`, which is §0.25 built. A `table ... to show <list>` materialises
 * every row into a QTableWidget, so §0.6 deferred the million-row path to a
 * MODEL behind a QTableView, and gBASIC Studio is the consumer: its results
 * grid switches to `datagrid.create_virtual(count_fn, cell_fn)` above 500
 * rows, and `datagrid` is GTK-only.
 *
 * THE ONE NUMBER THAT DECIDED WHETHER THIS IS POSSIBLE AT ALL. A model's
 * `data()` has to call back into the interpreter, and per cell of the whole
 * table that would be unusable. Measured, it is not:
 *
 *      100 rows,      300px tall  ->     27 data() calls, top row seen 8
 *      10,000 rows,   300px tall  ->     27 data() calls, top row seen 8
 *      1,000,000 rows,300px tall  ->     27 data() calls, top row seen 8
 *      1,000,000 rows,900px tall  ->     87 data() calls, top row seen 28
 *
 * THE COST IS THE VIEWPORT, NOT THE TABLE -- and `scrollToBottom` on a
 * million rows cost 54 calls IN TOTAL, jumping straight to row 999,999 rather
 * than touching anything on the way. So a callback per visible cell is ~30-90
 * per redraw.
 *
 * AND IT NEEDS NO NEW SEAM, which is a correction to §0.25's own first
 * assessment: `invoke_function(stmt, args, argc, receiver)` ALREADY takes
 * arguments and returns a Value -- the handler path simply passes
 * `NULL, 0, NULL` because handlers take none. So this is one more callback
 * beside `GbQtInvokeFn` rather than a new kind of crossing.
 *
 * `ask` IS THE THIRD DIRECTION WORD, beside `show` and `edit`, and it costs
 * ZERO grammar: `IDENT IDENT TO IDENT IDENT` is the production a named
 * container with a subject already uses, and the direction is recognised by
 * POSITION in the validator rather than by a token. The subject is a FUNCTION
 * NAME rather than a list, which is what the word says -- a table that ASKS
 * for a cell instead of being shown one.
 *
 * THE ROW COUNT IS A VERB AND NOT PART OF THE DECLARATION, because it changes:
 * Studio's grid is refilled by every query, so a count fixed when the window
 * opened would be wrong the first time anybody ran a second query. */
void *gb_qt_virtual_table(void *parent, const char *name,
                          const char *const *headers, int ncols,
                          const char *cell_fn, char *err, size_t err_size);

/* HOW MANY ROWS THE MODEL CLAIMS. Setting it resets the view, which is what
 * tells Qt to re-ask for everything -- so this is also how "the data changed"
 * is said. Reading it back is the only way to observe that the setting took,
 * and is the shape `gb_qt_menu_count` already has. */
int gb_qt_virtual_rows(void *window, const char *name, int rows, char *err,
                       size_t err_size);
int gb_qt_virtual_row_count(void *window, const char *name, int *rows,
                            char *err, size_t err_size);

int gb_qt_table_size(void *window, const char *name, int rows, char *err,
                     size_t err_size);
int gb_qt_table_set(void *window, const char *name, int row, int col,
                    const char *text, char *err, size_t err_size);
/* malloc'd; NULL for a cell that is not there. An EMPTY cell answers "" -- a
 * different thing from absent, and conflating them would make a blank the
 * caller typed indistinguishable from a row that does not exist. */
char *gb_qt_table_get(void *window, const char *name, int row, int col);
int gb_qt_table_rows(void *window, const char *name);
/* The row a person has selected, or -1 when none is. Not a boolean plus an
 * index: "nothing is selected" is an ordinary state and must be answerable. */
int gb_qt_table_selected(void *window, const char *name);
/* A COLUMN'S CAPTION. THE REASON IS AN ORACLE and this header first argued
 * against adding it: the caption comes from the declaration's `label:` while a
 * cell's data comes from its `name`, which are TWO DIFFERENT SOURCES -- so the
 * caption is the only thing that pins the column MAPPING independently.
 * Without it, population and the by-name cell reader both map column to name,
 * and if both were permuted the SAME way every check would pass while every
 * value sat under the wrong heading. Agreement is not correctness, which is
 * PLAT-NUMFMT's recorded lesson; two perturbations producing identical output
 * is what showed it here. malloc'd; NULL for a column that is not there. */
char *gb_qt_table_heading(void *window, const char *name, int col);

/* CHOOSE A ROW FROM THE PROGRAM. A real need rather than a test hook: a window
 * often opens with a row already highlighted, and a program that has just
 * added one wants it chosen. `row` below zero clears the selection, which is
 * how "nothing is chosen" is expressed on the way IN as well as out. */
int gb_qt_table_select(void *window, const char *name, int row, char *err,
                       size_t err_size);

/* A `form to show` IS DISPLAY ONLY, so its parts must not take typing -- not
 * for neatness but because the alternative is a SILENT DISCARD: someone types
 * into a one-way form, nothing complains, and `qt.subject` never reports it.
 * That is the class warning 2107 exists for one level up.
 *
 * WHICH PROPERTY IS A FACT ABOUT THE WIDGET and so is decided here rather than
 * by the caller: a QLineEdit has `readOnly`, which still allows selecting and
 * COPYING the text -- what a reader of a display form wants -- while a
 * QCheckBox has no such property at all, so it is disabled instead. Keeping
 * the policy in one place is what stops the C side guessing from the
 * declaration's word.
 */
int gb_qt_set_display_only(void *window, const char *name, int on, char *err,
                           size_t err_size);

/* ---- HIGHLIGHTING (docs/qt_gui_design.md §0.14) ----------------------------
 *
 * WHY THIS IS A SEPARATE SEAM RATHER THAN A PROPERTY: everything else a widget
 * carries goes through `QMetaObject` by name, which is what keeps this backend
 * small -- but a highlighter is an OBJECT you attach to a document, not a
 * property you set, so there is nothing for `indexOfProperty` to find. The
 * same is true of the completer Studio will want next, which is why the shape
 * here is "attach a thing and then ask what was attached".
 *
 * `gb_qt_has_highlighting` ANSWERS, like gb_qt_can_display, rather than
 * refusing: a build without KSyntaxHighlighting still renders a `source`
 * widget and still edits text, so a program needs to be able to ask instead of
 * being stopped. */
int gb_qt_has_highlighting(void);

/* A DIRECTORY TO SEARCH, in the engine's own layout: a `syntax`
 * subdirectory holding the XML. Called before the first `source` is constructed; later calls add further
 * paths. SAFE WITHOUT KSyntaxHighlighting, where it does nothing, so the
 * caller needs no guard of its own. */
void gb_qt_syntax_path(const char *dir);

/* ATTACH A HIGHLIGHTER to a `source` widget's document, with the definition
 * looked up BY NAME.
 *
 * BY NAME IS NOT A STYLE CHOICE. `*.bas` is claimed by TWO definitions at the
 * same priority -- the bundled FreeBASIC one and ours -- and lookup by
 * EXTENSION picks FreeBASIC with no error at all, FreeBASIC being a near
 * neighbour, so the result is plausible and wrong (run_ksyntax.sh asserts both
 * halves). `editors/README.md` argues deliberately against shipping a priority,
 * since a higher one would win for every `.bas` file on the machine including
 * somebody's FreeBASIC -- so by-name is the only route left, and that is a
 * decision, recorded.
 *
 * `theme` is "light", "dark", or NULL to ask Qt's own palette. It is DECLARED
 * rather than always inferred because `qt.theme` sets a STYLESHEET, which does
 * not move the palette -- so a dark application with an inferred theme gets
 * light highlighting on a dark background, which is unreadable and which
 * nothing could report.
 *
 * RETURNS 0 AND SETS `err` when the language is not known to the engine. That
 * matters more than it looks: an INVALID definition still applies the theme's
 * normal-text format to every line, so the widget looks like an ordinary
 * editor with text in it -- measured 2026-10-18, 3 ranges against 8 -- and
 * "highlighting ran" is therefore a vacuous assertion. */
int gb_qt_highlight(void *window, const char *name, const char *language,
                    const char *theme, char *err, size_t err_size);

/* WHAT WAS ACTUALLY APPLIED -- the oracle, and it needs both halves.
 *
 * The NAME catches the wrong language (FreeBASIC for gBASIC). The FILE catches
 * what the name cannot: a definition the user installed into their own
 * ~/.local/share/org.kde.syntax-highlighting SHADOWS ours and reports the
 * identical name -- measured 2026-10-18, 7 ranges against 8. That precedence
 * is left alone deliberately, being the same rule `load` follows in letting a
 * library beside the program override the standard one; what is not acceptable
 * is being unable to SEE it. Both malloc'd; NULL when no definition is
 * applied, which is a different answer from an empty string. */
/* ---- THE MARGIN ON A `source` (§0.29) ----------------------------------
 * `line_numbers:` and `marks:` are STRUCTURAL options, the shape `language:`
 * has: each builds a child widget rather than setting a property, so the
 * by-name route has nothing to find and they are applied through here.
 *
 * A CATEGORY IS LOOKED UP RATHER THAN SPELLED TWICE. The names and their
 * priority live in the backend, where the painter is, and the module asks --
 * two copies of a closed vocabulary is the drift this tree tripwires against,
 * and the window words were written out three times before being derived. */
int gb_qt_gutter_enable(void *window, const char *name, int numbers, int marks,
                        char *err, size_t err_size);
/* 1 described, 0 no gutter on that `source`, -1 refused (err set). */
int gb_qt_gutter_info(void *window, const char *name, int *numbers, int *marks,
                      int *width, char *err, size_t err_size);
int gb_qt_mark_kind(const char *category);
const char *gb_qt_mark_name(int kind);
int gb_qt_mark_count(void);
int gb_qt_mark_set(void *window, const char *name, int kind, const int *lines,
                   int count, char *err, size_t err_size);
/* The count, or -1 refused (err set). Writes at most `max` into `out`. */
int gb_qt_mark_get(void *window, const char *name, int kind, int *out, int max,
                   char *err, size_t err_size);
/* Press the margin beside a line -- what makes the click path drivable. */
int gb_qt_gutter_press(void *window, const char *name, int line, char *err,
                       size_t err_size);
/* 1 while a `gutter` handler is running, with the 1-based line clicked. */
int gb_qt_gutter_click(int *line);
/* What the LAST gutter paint drew -- the probe tier's oracle, since a painted
 * glyph is unobservable from gBASIC. */
void gb_qt_gutter_painted(int *lines, int *marks);

char *gb_qt_highlight_language(void *window, const char *name);
char *gb_qt_highlight_file(void *window, const char *name);

/* ---- DIALOGS (docs/qt_gui_design.md §0.15) ---------------------------------
 *
 * A DIALOG OPENS A NESTED EVENT LOOP, which is the entire reason this backend
 * is arranged the way it is. §5.1 measured both directions and ruled one out
 * ON THIS CASE: under direction B, gBASIC's own poll does not run for the
 * dialog's whole life -- a 651 ms gap over a 600 ms dialog, which is a
 * webserver answering nothing -- while under direction A the sources are served
 * throughout (4 of 4, three of them during it). So the hazard was priced before
 * any dialog existed, and `tests/run_qt_loop.sh` keeps that measurement live.
 *
 * BUT THE CLAIM IS NARROWER THAN IT READS, and this was measured rather than
 * assumed. §5.1 is about QT'S loop surviving the nested one, which it does: a
 * C++ probe in gBASIC's exact arrangement shows the tick firing during a modal
 * QMessageBox (3 of 3) and able to press its button. WHAT CANNOT HAPPEN is
 * gBASIC re-entering while the interpreter is suspended inside its own
 * handler. So:
 *
 *   dialog opened OUTSIDE a handler   gBASIC's sources keep running -- a
 *                                     `timer` fires and can answer the box
 *   dialog opened FROM a handler      they are SUSPENDED until it is answered
 *
 * MEASURED BOTH WAYS through the interpreter: a `timer.every` answers a
 * confirm raised from `main`, and a `server` block on an OS-assigned port
 * does NOT answer a request while a confirm raised from a button handler is
 * up. A modal dialog is therefore modal to the whole PROGRAM and not only to
 * the window -- which is defensible for a desktop application and surprising
 * for one that is also a server, so it is stated rather than left to be found.
 *
 * THE CONSEQUENCE FOR `gb_qt_answer` IS REAL: it can answer a dialog raised
 * outside a handler (a timeout, an unattended run, a suite) and CANNOT answer
 * one raised from a handler, where a person is the only answer.
 *
 * THE BOX IS PARENTED TO A WINDOW so Qt centres it there and treats it as modal
 * to that window, which is why every verb takes one.
 */
int gb_qt_dialog_depth(void);

/* A STATEMENT, with one button. Returns 0 and sets `err` only when it could
 * not be shown at all. */
int gb_qt_inform(void *window, const char *text, char *err, size_t err_size);

/* A QUESTION, with two. `*answer` is 1 for yes and 0 for no -- a BOOLEAN
 * rather than a button code, because two buttons is the whole of what this
 * verb offers and anything richer is a window shown modally, which is a
 * different thing with its own layout. */
int gb_qt_confirm(void *window, const char *text, int *answer,
                  char *err, size_t err_size);

/* ANSWER THE DIALOG THAT IS OPEN, from the program rather than from a person.
 *
 * NOT A TEST HOOK, though it is what lets a suite drive one: a dialog that
 * times out ("closing in ten seconds"), one dismissed because the data under
 * it changed, and an unattended run all need exactly this -- and `qt.invoke`
 * is already here on the same argument, exercising a real signal through the
 * real connection rather than around it.
 *
 * REFUSES WHEN NOTHING IS OPEN. The alternative is a silent no-op, which is
 * the failure class this whole backend is tested against. */
int gb_qt_answer(int value, char *err, size_t err_size);

/* WHAT IS OPEN, so a program driving its own dialogs is not driving blind.
 *
 * NOT A TEST HOOK, on the argument `qt.highlighting` already won: this backend
 * applies things a program asked for and a program that cannot SEE what landed
 * has no way to tell an option that worked from one that silently did nothing
 * -- which is exactly how a leaf's `label:` reached no widget for four
 * increments (§0.8). So the oracle is built BEFORE the fixture this time.
 *
 * `kind` is "message" or "chooser"; the rest are malloc'd and may be NULL.
 * Everything answers for the INNERMOST open dialog, and 0 when none is. */
int gb_qt_dialog_info(char **kind, char **title, char **text, char **folder,
                      char **filter);

/* ---- A FILE CHOOSER (docs/qt_gui_design.md §0.16) --------------------------
 *
 * THREE QUESTIONS, NOT ONE VERB WITH A MODE, because they are genuinely three:
 * "which existing file?", "what shall I call it?" and "which folder?" -- which
 * is also how Qt splits them, and each can then refuse the options that mean
 * nothing to it rather than ignoring them.
 *
 * QT'S OWN DIALOG IS FORCED, AND THAT WAS MEASURED RATHER THAN PREFERRED. With
 * the desktop's native chooser (the XDG portal, which is what Qt reaches for on
 * Wayland), on a real display:
 *
 *   selectFile()   does NOTHING -- selectedFiles() is EMPTY before exec()
 *   accept()       does NOTHING -- 41 pump ticks, rc=0, no file chosen
 *
 * so a program could not preselect a path and `gb_qt_answer_path` would be a
 * SILENT NO-OP, which is the failure class this whole backend is tested
 * against. `DontUseNativeDialog` is therefore set unconditionally and is not
 * offered as an option.
 *
 * AND OFFSCREEN CANNOT TELL THE TWO APART: with no portal to reach, Qt falls
 * back to its own widget dialog, so the native path MEASURES AS DRIVABLE under
 * QT_QPA_PLATFORM=offscreen -- the one platform every suite here runs on.
 * Comfortable test data hiding the difference is the shape this tree has a name
 * for, so the measurement is recorded here rather than asserted in a tier that
 * would pass either way.
 *
 * `kind` is one of the three below. `title`, `folder`, `name` and `filter` may
 * each be NULL for "no opinion"; `filter` is Qt's own `;;`-separated form, so a
 * caller handing over one string passes it through and a caller with a list
 * joins it.
 *
 * Answers a malloc'd absolute path, or NULL. `*cancelled` is set to 1 when the
 * person said no, WHICH IS A DIFFERENT ANSWER FROM A FAILURE -- and it has to
 * be asked rather than inferred from an empty selection, because measured, a
 * REJECTED QFileDialog STILL REPORTS ITS SELECTION: reading selectedFiles()
 * without checking the result code hands back a path for a dialog the person
 * cancelled, which is a plausible wrong answer and the worst kind. */
#define GB_QT_PICK_OPEN   0
#define GB_QT_PICK_SAVE   1
#define GB_QT_PICK_FOLDER 2

char *gb_qt_choose(void *window, int kind, const char *title,
                   const char *folder, const char *name, const char *filter,
                   int *cancelled, char *err, size_t err_size);

/* ANSWER THE OPEN CHOOSER WITH A PATH -- `qt.answer` for the dialog whose
 * answer is not yes or no.
 *
 * IT CHECKS THE PATH BEFORE ACCEPTING, and that is not defensiveness: measured,
 * QFileDialog::accept() in ExistingFile mode puts up its OWN QMessageBox when
 * the file is not there, and that box opens a SECOND nested loop from inside
 * the first one's pump tick -- which Qt will not re-enter -- so the program
 * HANGS with two dialogs up and no way to reach either. Refusing an absent path
 * here turns that hang into a sentence. */
int gb_qt_answer_path(const char *path, char *err, size_t err_size);

void gb_qt_show(void *window);

/* CLOSE A WINDOW. Measured first, and every answer shaped what is here:
 *
 *   quitOnLastWindowClosed       true (Qt's default, never set by us)
 *   close() offscreen            delivers QEvent::Close to an event filter
 *   closing one of two windows   exec() keeps running
 *   closing the last one         exec() RETURNS -- so `qt.run()` ends
 *   after close()                THE WIDGET IS STILL ALIVE AND READABLE
 *   delete immediately after     safe, close() having returned
 *
 * THE FIFTH ROW IS WHY A CLOSE IS BOOKKEEPING AND NOT A HIDE. Qt does not set
 * `WA_DeleteOnClose`, so a window the person dismissed goes on answering
 * `qt.read` with the values it was showing -- a program reading from a window
 * nobody can see, which is a plausible wrong answer and the class this backend
 * is tested against. So a close REMOVES the window from gBASIC's registry, and
 * every verb that needs an open one then refuses by name.
 *
 * WHICH MAKES THE `closed` HANDLER LOAD-BEARING RATHER THAN A NOTIFICATION:
 * it fires BEFORE the registry forgets, so reading the form in it is the one
 * place a program can still see what the person typed. A program with nothing
 * to save needs no handler; one that has something has exactly one place to
 * put it.
 *
 * ONE PATH FOR ALL THREE WAYS IN. `gb_qt_close` calls `widget->close()` rather
 * than doing the work itself, so the program's verb, an invoked `close` and
 * the window manager's button are the same code -- the rule `would` and
 * `execute` follow in `automation`, and for the same reason: if they could
 * disagree, a suite driving one would be describing the other. The X button is
 * the one that cannot be driven here (offscreen has no window manager), and it
 * reaches `QWidget::close()` through Qt's own platform layer, so it is the
 * same event by construction rather than by measurement -- stated, because the
 * difference matters. */
int gb_qt_close(void *window, char *err, size_t err_size);

/* ---- A WINDOW SHOWN MODALLY (docs/qt_gui_design.md §0.18) -----------------
 *
 * §0.15 named this as what `qt.confirm` is not: two buttons is the whole of
 * what a message box offers, and ANYTHING RICHER IS A WINDOW SHOWN MODALLY --
 * a form to fill in, a list to choose from, a thing with a layout of its own.
 *
 * IT IS THE SAME WINDOW. `qt.open` and `qt.open_modal` share one open path
 * (`qt_open_declared`), so the declaration, the validation, the binding and
 * the handlers are not merely similar -- they are the same code, which is why
 * a modal window needs no second vocabulary.
 *
 * FOUR WAYS IT CAN END, MEASURED, and the table is the design:
 *
 *   qt.accept(win)    exec -> true    `closed` does NOT fire   still readable
 *   qt.reject(win)    exec -> false   does not fire            still readable
 *   qt.close(win)     exec -> false   FIRES                    closed
 *   the X button      exec -> false   FIRES                    closed
 *
 * ACCEPTING IS NOT CLOSING -- `accept()` delivers no QEvent::Close at all
 * (measured) and leaves the widget hidden but alive. That asymmetry would be a
 * trap at the gBASIC level, where `false` would mean two different states and
 * a program would have to guard every `qt.close` with `qt.is_open`. So
 * `qt.open_modal` CLOSES THE WINDOW BEFORE IT RETURNS: the `closed` handler
 * then fires exactly once however the window ended, and §0.17's single rule --
 * that the handler is the one place a dismissed form can still be read --
 * extends to a modal window with no exception.
 *
 * ITS MODAL EXTENT IS THE DIALOG'S, measured in §0.15 and not re-argued:
 * `exec()` opens a nested loop, so gBASIC's sources run while it is up when it
 * was opened from `main` and are suspended when it was opened from inside a
 * watcher body. Two guards hold that, each sufficient alone. */
int gb_qt_show_modal(void *window, int *accepted, char *err, size_t err_size);

/* ANSWER A MODAL WINDOW -- what a button handler calls to say yes or no.
 *
 * NOT `qt.answer`, which exists to drive a dialog FROM THE PROGRAM when no
 * person is there (a timeout, an unattended run, a suite). This is the
 * person's answer arriving through a button they pressed, which is a different
 * thing wearing the same word, so it gets its own verb rather than a third
 * argument shape on that one. */
int gb_qt_answer_modal(void *window, int accept, char *err, size_t err_size);

/* Hands the thread to QApplication::exec(). `tick_ms` is how often gBASIC's
 * own loop gets one non-blocking iteration. */
int gb_qt_run(int tick_ms);
void gb_qt_stop(void);
void gb_qt_end(void);


/* A CONTAINER WHOSE CHILDREN CHANGE (§0.27) -- `qt.refresh(win, name, items)`.
 * The C side drives the walk and these are the parts it cannot do itself: the
 * container has to be resolved BY NAME (a position is the one thing a reorder
 * changes), a kept child has to be recognised and MOVED rather than rebuilt,
 * a caption has to be re-read, and an absent child has to be DELETED with
 * every name in its subtree -- measured, a child merely unhooked from a layout
 * stays parented and visible. */
void *gb_qt_container_node(void *window, const char *name,
                           char *err, size_t err_size);
const char *gb_qt_node_word(void *node);
void *gb_qt_child_named(void *container, const char *name);
int gb_qt_child_count(void *container);
void *gb_qt_child_at(void *container, int index);
int gb_qt_child_place(void *container, void *child, int position,
                      char *err, size_t err_size);
int gb_qt_child_caption(void *container, void *child, const char *text,
                        char *err, size_t err_size);
int gb_qt_child_drop(void *container, void *child, char *err, size_t err_size);

/* THE CAPTIONS OF A `tabs`, as a count plus a per-index read -- the `qt.menus`
 * shape, and for the same reason: `QTabWidget::tabText(int)` takes an argument
 * and `qt.invoke` passes none, so without this a caption is unobservable and
 * every check about one passes on an implementation that never set it. */
/* A `tree` (§0.28) -- rows with a DEPTH, drawn as a tree. The rows arrive one
 * at a time, each with its depth, and an INDEX IS INTO THE DECLARED ROWS: a
 * pre-order walk reproduces the order they were given in, because each row is
 * attached under the last row one level shallower, so nothing is remembered
 * between calls. `gb_qt_tree_depth` answers -2 for a part that is not a tree
 * and -1 for a row that is not there, which are different questions. */
/* HOW MANY COLUMNS A TABLE OR TREE HAS, -1 for anything else -- so a
 * heading's refusal can say WHICH of the two things is wrong. */
int gb_qt_column_count(void *window, const char *name);

void *gb_qt_tree(void *parent, const char *name, const char *const *headers,
                 int columns, char *err, size_t err_size);
int gb_qt_tree_clear(void *window, const char *name);
int gb_qt_tree_add(void *window, const char *name, int depth,
                   const char *const *cells, int columns);
int gb_qt_tree_depth(void *window, const char *name, int row);
int gb_qt_tree_rows(void *window, const char *name);
char *gb_qt_tree_get(void *window, const char *name, int row, int col);
char *gb_qt_tree_heading(void *window, const char *name, int col);
int gb_qt_tree_selected(void *window, const char *name);
int gb_qt_tree_select(void *window, const char *name, int row, char *err,
                      size_t err_size);

/* THE ORDER ORACLE: the name of the child at a position, or "" for one nobody
 * named. A box's order is observable through nothing else -- `qt.at` answers
 * coordinates for a `cells` grid only. */
char *gb_qt_child_name(void *container, int index);

int gb_qt_tab_count(void *window, const char *name);
char *gb_qt_tab_caption(void *window, const char *name, int index);
#ifdef __cplusplus
}
#endif

#endif

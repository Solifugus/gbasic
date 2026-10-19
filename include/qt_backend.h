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

/* ONE ITERATION OF gBASIC'S OWN EVENT LOOP, NON-BLOCKING. Qt owns the wait
 * (direction A, measured in §5.1), so gBASIC's sources cannot have a `poll()`
 * of their own; they ride a Qt timer instead. Returns 0 to ask Qt's loop to
 * stop. */
typedef int (*GbQtTickFn)(void);

void gb_qt_set_callbacks(GbQtInvokeFn invoke, GbQtTickFn tick);

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

void *gb_qt_window(const char *title, int width, int height);

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

/* Hands the thread to QApplication::exec(). `tick_ms` is how often gBASIC's
 * own loop gets one non-blocking iteration. */
int gb_qt_run(int tick_ms);
void gb_qt_stop(void);
void gb_qt_end(void);

#ifdef __cplusplus
}
#endif
#endif

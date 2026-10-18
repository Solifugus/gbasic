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

/* Construct the QApplication. Separate from gb_qt_window because it must
 * happen exactly once and after gb_qt_can_display() has said yes. */
int gb_qt_begin(char *err, size_t err_size);

void *gb_qt_window(const char *title, int width, int height);

/* A container word (`row`, `column`, `form`, `group`). `title` is the group
 * box's caption and is NULL for the others. */
void *gb_qt_container(void *parent, const char *word, const char *title,
                      char *err, size_t err_size);

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

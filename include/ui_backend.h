/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * WHAT A GUI BACKEND IS TO gBASIC (docs/ui_design.md §4, step 4 of §8). The
 * `ui` module's front, `src/modules/ui.c`, reads a program's windows, checks
 * its words and raises its refusals; everything that draws goes through ONE
 * `GbUiBackend` table, chosen once per process (the build, then GBASIC_UI).
 * Qt's table is `src/modules/ui_qt.c`, over `include/qt_backend.h`. A
 * program never names a backend.
 *
 * THE TABLE IS THE SMALL PART. The rules below are what programs rely on, and
 * a table with the right members that breaks one of them breaks programs that
 * never mention it. Each rule names the tier that holds it today.
 *
 *  1. THE BACKEND OWNS THE WAIT; gBASIC GETS TICKS. `set_callbacks` hands over
 *     `tick`, one non-blocking iteration of gBASIC's own loop, which the
 *     backend calls every 50 ms while `run` lasts -- the webserver loop's own
 *     poll interval -- and stops `run` when it answers 0. A backend with no
 *     loop of its own owns one for `run` and calls the same `tick`.
 *  2. TICKS CONTINUE INSIDE A MODAL. A dialog or `open_modal` opened from
 *     `main` keeps gBASIC's sources served (run_ui_modal `extent`, run_net
 *     tier 5). One opened inside a watcher body suspends them, and that half
 *     is gBASIC's `watcher_drain` guard, not the backend's (run_ui_dialog).
 *  3. `can_display` CANNOT CRASH. It is a question, asked before anything is
 *     opened; Qt aborts with no display, so Qt asks in a forked child.
 *  4. CLOSING THE LAST WINDOW ENDS `run` (run_ui_lifecycle `loop`).
 *  5. `stop` ENDS `run` AND LEAVES THE WINDOWS OPEN AND READABLE
 *     (run_ui_lifecycle `stop`).
 *  6. EVERY CLOSE IS REPORTED ONCE, however it arrived -- `close`, a `close`
 *     invoked by word, the window manager's button, a modal's accept or
 *     reject -- through the `closed` callback, so a program cannot tell them
 *     apart (run_ui_lifecycle, run_ui_modal).
 *  7. A HANDLER MAY CALL BACK INTO THE BACKEND (select, then refresh, inside a
 *     handler). The backend counts the depth (`handler_depth`); the front caps
 *     it at 16 with warning 2117 (run_ui_reentry `bounded`). Anything a
 *     re-entrant call could not free yet waits for `retire_drain`, which the
 *     front calls at the top of every verb, when nothing of the backend's is
 *     on the stack.
 *
 * WORDS, NOT A TOOLKIT'S NAMES. Every `prop`, `word`, `signal` and method name
 * a member receives is a word of src/window_check.c, already checked there;
 * mapping it to the toolkit is the backend's business (Qt's map is in
 * qt_backend.cpp). Refusals a backend writes into `err` name gBASIC's part
 * words too, never a class.
 *
 * EVERY MEMBER IS SET in every table (tests/run_ui_vocabulary.sh `seam`),
 * and what a backend cannot do is said by `groups` below, never by a NULL.
 */
#ifndef GBASIC_UI_BACKEND_H
#define GBASIC_UI_BACKEND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A connected signal fired. `handler` is the function NAME the declaration
 * registered for it -- by name rather than as a value, so a backend holds
 * no gBASIC `Value`. */
typedef void (*GbUiInvokeFn)(const char *handler);
/* WHAT IS IN ONE CELL, asked of gBASIC. Answers 1 and fills `*text` with a
 * malloc'd string the caller frees, or 0 if the function is gone or raised --
 * in which case the cell is drawn empty rather than the program stopped, the
 * position a model has to take because `data()` is called during a PAINT and
 * there is nothing sensible to unwind into. */
typedef int (*GbUiCellFn)(const char *fn, int row, int column, char **text);

/* A WINDOW HAS CLOSED, and gBASIC's own registry has to learn about it. The
 * backend cannot do that bookkeeping itself: which declaration a window came
 * from, and what a form captured when it opened, are facts the C side holds.
 *
 * CALLED FOR EVERY CLOSE, however it arrived -- `close`, a `close`
 * invoked by name, or the window manager's own button -- because all three
 * deliver the same QEvent::Close and one hook catches them all. That
 * uniformity is the point: a program must not be able to tell them apart,
 * since a person clicking X is the ordinary case and the other two exist to
 * be drivable. */
typedef void (*GbUiClosedFn)(void *window);

/* ONE ITERATION OF gBASIC'S OWN EVENT LOOP, NON-BLOCKING. The backend owns
 * the wait (Qt's direction A, measured in docs/qt_gui_design.md §5.1), so
 * gBASIC's sources cannot have a `poll()` of their own; they ride the
 * backend's timer instead (rule 1 below). Returns 0 to ask Qt's loop to
 * stop. */
typedef int (*GbUiTickFn)(void);

/* BLOCKS BETWEEN A `source`'s LINES, AND FOLDS (§0.38 A, B2). One item per
 * block; `text` holds a text or markdown block's text, an image's path, or
 * (image_bytes) the image's bytes. A table's `cells` are its column names and
 * then each row's cells, all as text. Set replaces the whole set, reconciled
 * by id; get answers what Qt holds now (strings caller-freed). */
typedef struct {
    const char *id;
    const char *kind;          /* "text" | "table" | "image" | "markdown" */
    const char *title;
    int line;                  /* 1-based */
    const char *text;
    long text_len;
    int image_bytes;
    const char *format;        /* "png" | "svg" | NULL */
    int columns, rows, more_rows;
    const char *const *cells;
    int max_height, collapsed, covers;
} GbUiBlockItem;

typedef struct {
    char *id;
    char *kind;
    int line, height, visible, collapsed, covers;
} GbUiBlockInfo;

/* The modifier bits `scroll` takes, so a caller does not depend on Qt's
 * own values. */
#define GB_UI_MOD_CTRL  1
#define GB_UI_MOD_SHIFT 2
#define GB_UI_MOD_ALT   4

/* The three kinds of `choose` (a file to open, a file to save, a folder). */
#define GB_UI_PICK_OPEN   0
#define GB_UI_PICK_SAVE   1
#define GB_UI_PICK_FOLDER 2

/* THE 149 MEMBERS, in qt_backend.h's order, each documented there above its
 * Qt function `gb_qt_<member>`: what it answers, what it refuses and what was
 * measured. */
typedef struct GbUiBackend {
    const char *name;          /* what GBASIC_UI says to choose it: "qt" */
    /* THE CAPABILITY GROUPS IT HAS (docs/ui_design.md §6), space-separated,
     * each one of src/modules/ui.c's `ui_groups`. A group is all or nothing:
     * the front asks it once, answers `ui.can` from it, and refuses a verb,
     * a part or an option of a group the backend lacks before calling any
     * member. So a member of a missing group is never called, though it
     * must still be set (to a function that refuses). */
    const char *groups;
    int (*can_display)(void);
    void (*set_callbacks)(GbUiInvokeFn invoke, GbUiTickFn tick,
                          GbUiClosedFn closed, GbUiCellFn cell);
    /* "" CLEARS the theme -- a record or a stylesheet -- and every backend
     * owes that, `stylesheet` group or not; other text is a stylesheet. */
    int (*theme_set)(const char *css, char *err, size_t err_size);
    int (*theme_record)(const char *const *names, const char *const *values,
                        size_t n, char *err, size_t err_size);
    char *(*theme_get)(void);
    int (*begin)(char *err, size_t err_size);
    void *(*window)(const char *name, const char *title, int width, int height,
                    int kind);
    void *(*container)(void *parent, const char *word, const char *name,
                       const char *title, char *err, size_t err_size);
    void *(*part)(void *parent, const char *word, const char *name,
                  const char *label, char *err, size_t err_size);
    int (*set_text)(void *widget, const char *prop, const char *value,
                    char *err, size_t err_size);
    int (*set_number)(void *widget, const char *prop, double value, char *err,
                      size_t err_size);
    int (*set_bool)(void *widget, const char *prop, int value, char *err,
                    size_t err_size);
    int (*layout_number)(void *container, const char *prop, long value,
                         char *err, size_t err_size);
    int (*grow)(void *widget, long weight, char *err, size_t err_size);
    int (*span)(void *widget, long across, long down, char *err,
                size_t err_size);
    int (*cell_of)(void *window, const char *name, int *row, int *col,
                   int *across, int *down);
    int (*pane_of)(void *window, const char *name, char **kind,
                   char **direction, int *panes, int *index, int *resizable);
    int (*connect)(void *window, const char *target, const char *signal,
                   const char *handler, char *err, size_t err_size);
    int (*invoke)(void *window, const char *target, const char *method,
                  char *err, size_t err_size);
    char *(*read)(void *window, const char *name);
    int (*write)(void *window, const char *name, const char *text, char *err,
                 size_t err_size);
    int (*set_text_list)(void *widget, const char *prop,
                         const char *const *values, size_t count, char *err,
                         size_t err_size);
    int (*set_part_text_list)(void *window, const char *name, const char *prop,
                              const char *const *values, size_t count,
                              char *err, size_t err_size);
    char *(*shortcuts)(void *window, const char *name, char *err,
                       size_t err_size);
    int (*key)(void *window, const char *name, const char *keys, int *handled,
               char *err, size_t err_size);
    int (*column_align)(void *table_node, int col, const char *align,
                        char *err, size_t err_size);
    const char *(*cell_alignment)(void *window, const char *name, int row,
                                  int col);
    int (*theme_plain)(void *window, const char *name, const char *theme,
                       char *err, size_t err_size);
    int (*set_part_text)(void *window, const char *name, const char *prop,
                         const char *value, char *err, size_t err_size);
    int (*set_part_number)(void *window, const char *name, const char *prop,
                           double value, char *err, size_t err_size);
    int (*set_part_bool)(void *window, const char *name, const char *prop,
                         int value, char *err, size_t err_size);
    int (*get)(void *window, const char *name, const char *prop, char *kind,
               double *num, char **text, char *err, size_t err_size);
    void *(*menu)(void *window, void *parent_menu, const char *title,
                  char *err, size_t err_size);
    void *(*menu_item)(void *window, void *menu, const char *name,
                       const char *label, char *err, size_t err_size);
    int (*menu_separator)(void *menu, char *err, size_t err_size);
    void *(*popup_menu)(void *window, const char *name, char *err,
                        size_t err_size);
    int (*menu_count)(void *window);
    char *(*menu_title)(void *window, int index);
    int (*menu_entry_count)(void *window, int index);
    char *(*menu_entry_title)(void *window, int index, int entry,
                              int *is_separator);
    int (*popup_entry_count)(void *window, const char *name);
    char *(*popup_entry_title)(void *window, const char *name, int entry,
                               int *is_separator);
    int (*popup)(void *window, const char *name, const char *under, int *shown,
                 char *err, size_t err_size);
    int (*right_click)(void *window, const char *name, int row, char *err,
                       size_t err_size);
    int (*which_tab)(void);
    int (*copy)(const char *text, long len, char *err, size_t err_size);
    int (*clipboard)(char **text, long *len, char *err, size_t err_size);
    int (*wheel_event)(int *dy, int *dx, int *ctrl, int *shift, int *alt);
    int (*wheel_exact)(double *dy, double *dx);
    int (*swallow)(char *err, size_t err_size);
    int (*scroll)(void *window, const char *name, double notches, int mods,
                  int *claimed, char *err, size_t err_size);
    int (*pane_sizes)(void *window, const char *name, int *out, int *count,
                      char *err, size_t err_size);
    int (*resize_panes)(void *window, const char *name, const int *sizes,
                        int count, char *err, size_t err_size);
    int (*has_icon)(void *window, const char *name, int *has, char *err,
                    size_t err_size);
    int (*capture)(void *window, const char *name, double scale, char **png,
                   long *len, int *width, int *height, char *err,
                   size_t err_size);
    int (*render_svg)(const char *svg, long svg_len, int width, int height,
                      double scale, char **png, long *len, int *out_width,
                      int *out_height, char *err, size_t err_size);
    int (*theme_known)(const char *name);
    char *(*theme_names)(void);
    int (*blocks_set)(void *window, const char *name,
                      const GbUiBlockItem *items, int count, char *err,
                      size_t err_size);
    int (*blocks_get)(void *window, const char *name, GbUiBlockInfo *out,
                      int max, char *err, size_t err_size);
    int (*fold_set)(void *window, const char *name, const int *from,
                    const int *to, int count, char *err, size_t err_size);
    int (*fold_get)(void *window, const char *name, int *from, int *to,
                    int max, char *err, size_t err_size);
    int (*block_event)(char **id, char **action, char **target);
    char *(*link_event)(void);
    int (*link_press)(void *window, const char *name, const char *target,
                      char *err, size_t err_size);
    int (*block_press)(void *window, const char *name, const char *id,
                       const char *action, const char *target, char *err,
                       size_t err_size);
    int (*scrolled)(void *window, const char *name, double *at, char *err,
                    size_t err_size);
    int (*scroll_to)(void *window, const char *name, double at, char *err,
                     size_t err_size);
    void *(*table)(void *parent, const char *name, const char *const *headers,
                   int columns, char *err, size_t err_size);
    int (*choice_clear)(void *window, const char *name, char *err,
                        size_t err_size);
    int (*choice_add)(void *window, const char *name, const char *item,
                      char *err, size_t err_size);
    void *(*virtual_table)(void *parent, const char *name,
                           const char *const *headers, int ncols,
                           const char *cell_fn, char *err, size_t err_size);
    int (*virtual_rows)(void *window, const char *name, int rows, char *err,
                        size_t err_size);
    int (*virtual_row_count)(void *window, const char *name, int *rows,
                             char *err, size_t err_size);
    int (*table_size)(void *window, const char *name, int rows, char *err,
                      size_t err_size);
    int (*table_set)(void *window, const char *name, int row, int col,
                     const char *text, char *err, size_t err_size);
    char *(*table_get)(void *window, const char *name, int row, int col);
    int (*table_rows)(void *window, const char *name);
    int (*table_selected)(void *window, const char *name);
    char *(*table_heading)(void *window, const char *name, int col);
    int (*table_select)(void *window, const char *name, int row, char *err,
                        size_t err_size);
    int (*set_display_only)(void *window, const char *name, int on, char *err,
                            size_t err_size);
    int (*has_highlighting)(void);
    void (*syntax_path)(const char *dir);
    int (*highlight)(void *window, const char *name, const char *language,
                     const char *theme, char *err, size_t err_size);
    void (*retire_drain)(void);
    int (*handler_depth)(void);
    int (*handler_max)(void);
    int (*dock_info)(void *window, const char *name, char *area,
                     size_t area_size, int *floating, int *tabbed,
                     int *visible, char *err, size_t err_size);
    char *(*layout_save)(void *window, char *err, size_t err_size);
    int (*layout_restore)(void *window, const char *text, char *err,
                          size_t err_size);
    int (*caret)(void *window, const char *name, int *line, int *column,
                 int *selecting, char *err, size_t err_size);
    int (*caret_to)(void *window, const char *name, int line, int column,
                    char *err, size_t err_size);
    int (*select_text)(void *window, const char *name, int line, int column,
                       int line2, int column2, char *err, size_t err_size);
    int (*selection)(void *window, const char *name, int *span, char **text,
                     char *err, size_t err_size);
    int (*replace_text)(void *window, const char *name, const int *spans,
                        const char *const *texts, size_t n, char *err,
                        size_t err_size);
    int (*highlight_text)(void *window, const char *name, const int *spans,
                          size_t n, char *err, size_t err_size);
    int (*highlights)(void *window, const char *name, int **spans, size_t *n,
                      char *err, size_t err_size);
    int (*label)(void *window, const char *name, char **text, char *err,
                 size_t err_size);
    int (*click)(void *window, const char *name, int row, char *err,
                 size_t err_size);
    int (*gutter_enable)(void *window, const char *name, int numbers,
                         int marks, char *err, size_t err_size);
    int (*gutter_info)(void *window, const char *name, int *numbers,
                       int *marks, int *width, char *err, size_t err_size);
    int (*mark_kind)(const char *category);
    const char *(*mark_name)(int kind);
    int (*mark_count)(void);
    int (*mark_set)(void *window, const char *name, int kind, const int *lines,
                    int count, char *err, size_t err_size);
    int (*mark_get)(void *window, const char *name, int kind, int *out,
                    int max, char *err, size_t err_size);
    int (*gutter_press)(void *window, const char *name, int line, char *err,
                        size_t err_size);
    int (*gutter_click)(int *line);
    void (*gutter_painted)(int *lines, int *marks, int *first);
    char *(*highlight_language)(void *window, const char *name);
    char *(*highlight_file)(void *window, const char *name);
    char *(*highlight_theme)(void *window, const char *name);
    int (*dialog_depth)(void);
    int (*inform)(void *window, const char *text, char *err, size_t err_size);
    int (*confirm)(void *window, const char *text, int *answer, char *err,
                   size_t err_size);
    int (*answer)(int value, char *err, size_t err_size);
    int (*dialog_info)(char **kind, char **title, char **text, char **folder,
                       char **filter);
    char *(*choose)(void *window, int kind, const char *title,
                    const char *folder, const char *name, const char *filter,
                    int *cancelled, char *err, size_t err_size);
    int (*answer_path)(const char *path, char *err, size_t err_size);
    void (*show)(void *window);
    int (*close)(void *window, char *err, size_t err_size);
    int (*show_modal)(void *window, int *accepted, char *err, size_t err_size);
    int (*answer_modal)(void *window, int accept, char *err, size_t err_size);
    int (*run)(int tick_ms);
    void (*stop)(void);
    void (*end)(void);
    void *(*container_node)(void *window, const char *name, char *err,
                            size_t err_size);
    const char *(*node_word)(void *node);
    void *(*child_named)(void *container, const char *name);
    int (*child_count)(void *container);
    void *(*child_at)(void *container, int index);
    int (*child_place)(void *container, void *child, int position, char *err,
                       size_t err_size);
    int (*child_caption)(void *container, void *child, const char *text,
                         char *err, size_t err_size);
    int (*child_drop)(void *container, void *child, char *err,
                      size_t err_size);
    int (*column_count)(void *window, const char *name);
    void *(*tree)(void *parent, const char *name, const char *const *headers,
                  int columns, char *err, size_t err_size);
    int (*tree_clear)(void *window, const char *name);
    int (*tree_add)(void *window, const char *name, int depth,
                    const char *const *cells, int columns);
    int (*tree_depth)(void *window, const char *name, int row);
    int (*tree_rows)(void *window, const char *name);
    char *(*tree_get)(void *window, const char *name, int row, int col);
    char *(*tree_heading)(void *window, const char *name, int col);
    int (*tree_selected)(void *window, const char *name);
    int (*tree_select)(void *window, const char *name, int row, char *err,
                       size_t err_size);
    char *(*child_name)(void *container, int index);
    int (*tab_count)(void *window, const char *name);
    char *(*tab_caption)(void *window, const char *name, int index);
    /* PIXELS PER COLUMN AND PER ROW (docs/tui_design.md §8.1, Q3): the front
     * turns a size in characters (`40 columns`, `12 rows`) into this
     * backend's pixels with it. Qt reads its font; the terminal answers its
     * nominal cell, 8 by 16, so its pixels convert back to cells exactly. */
    int (*cell_size)(int *width, int *height);
} GbUiBackend;

#ifdef __cplusplus
}
#endif

#endif


/* ONE CHECKER FOR A `window`, WHATEVER IT IS WRITTEN AS.
 *
 * The `window` block is a declaration, and §0.2 of docs/qt_gui_design.md
 * validates it at LOAD time: a closed vocabulary with a near-miss hint, the
 * placement rules a `menu`/`tab`/`item` depend on, names unique within the
 * window, and a handler whose target really exists. All of that lives in
 * `src/frontend.c` and walks the AST.
 *
 * §4 says the block is SUGAR OVER A BUILDER, because a declaration cannot
 * contain a loop and so cannot generate a row per invoice. MEASURED
 * 2026-10-18, THAT DOOR WAS ALREADY OPEN: `qt.open` reads the record the
 * declaration binds and checks only that `kind` is `"window"`, so a record a
 * program BUILDS opens, renders and reads back exactly as a declared one.
 * Nobody knew, because nothing said so and nothing tested it.
 *
 * WHAT IT LACKED WAS EVERY ONE OF THOSE CHECKS, and that is not a missing
 * nicety -- it is the shape `web.configure` was built to close. There, the
 * record could already be written directly (`app.options.port = ...`) and that
 * path validated NOTHING, so `app.options.workesr = 4` was accepted in
 * silence. Measured here before a line was written, the same held: a part with
 * NO NAME opened quietly and was unaddressable forever, TWO PARTS SHARING A
 * NAME opened quietly with the second shadowing the first, and a handler
 * naming a part that does not exist was refused by the backend for the wrong
 * reason -- never "there is nothing called `nobody` in this window", which is
 * the refusal load-time validation exists for.
 *
 * SO THE RULES MOVED HERE AND DID NOT GET A SECOND COPY. Two representations
 * of one declaration drift -- run_web_configure.sh's TABLES tripwire makes
 * that argument for the server block's head options, and the window
 * vocabulary had already been written out THREE times before `win_nearest`
 * was derived from the tables instead. The rules are therefore expressed
 * against an ACCESSOR MODEL: `src/frontend.c` supplies an adapter over the
 * AST, `src/modules/qt.c` one over a record, and neither holds a rule. It is
 * the arrangement `would` and `execute` share in `automation`, for the reason
 * stated there: if the two could disagree, a check would describe a different
 * window from the one that opens.
 *
 * NO ALLOCATION AND NO MATERIALISATION. The model is accessors rather than a
 * struct of values, so a child item is reached by asking for it; an AST node
 * and a record field are both already in memory and neither is copied. */
#ifndef GBASIC_WINDOW_CHECK_H
#define GBASIC_WINDOW_CHECK_H

#include <stddef.h>

enum {
    GB_WIN_CONTAINER = 0,
    GB_WIN_LEAF = 1,
    GB_WIN_HANDLER = 2
};

typedef struct GbWinModel GbWinModel;

/* Every question the rules ask. A string answer may be NULL, which always
 * means "absent" -- never "empty", a distinction the vocabulary depends on
 * (a `separator` HAS no caption where `item x( label: "" )` has an empty
 * one). */
struct GbWinModel {
    /* the window */
    const char *(*win_name)(const void *win);
    const char *(*win_subject)(const void *win);
    const char *(*win_direction)(const void *win);
    const char *(*win_close_word)(const void *win);
    size_t (*win_option_count)(const void *win);
    const char *(*win_option_name)(const void *win, size_t i);
    size_t (*win_item_count)(const void *win);
    const void *(*win_item)(const void *win, size_t i);

    /* one item */
    int (*kind)(const void *item);
    const char *(*word)(const void *item);
    const char *(*name)(const void *item);
    const char *(*label)(const void *item);
    const char *(*subject)(const void *item);
    const char *(*direction)(const void *item);
    const char *(*close_word)(const void *item);
    const char *(*target)(const void *item);
    const char *(*signal)(const void *item);
    /* Whether a handler carries something to call. A declared handler always
     * does, the grammar having given it a body; a BUILT one may not, and a
     * handler that cannot be called is the same defect as one that can never
     * fire. */
    int (*handler_callable)(const void *item);
    /* WHETHER AN ITEM DECLARES THIS OPTION AS TRUE. The checker deliberately
     * does NOT police a leaf's options -- they are widget properties, and Qt
     * exposes 75 on a QPushButton alone, so that set cannot be closed from
     * here -- and `depth:` on a tree's column is the first option a
     * DECLARATION RULE turns on: at most one column may carry it, because two
     * would be a declaration with two answers. May be NULL in a model that
     * cannot answer it; every caller tests before calling. */
    int (*option_true)(const void *item, const char *option);
    size_t (*entry_count)(const void *item);
    const void *(*entry)(const void *item, size_t i);

    /* reporting. `item` is NULL for a fault in the head. */
    void (*report)(void *ctx, const void *win, const void *item,
                   const char *message);
    void *ctx;
};

/* Returns the number of problems reported. */
int gb_window_check(const GbWinModel *model, const void *win);

/* Exposed because both adapters need them and because run_window.sh asserts
 * every word is reachable as a near-miss suggestion. */
int gb_window_word_known(const char *word);
int gb_window_word_is_container(const char *word);
const char *gb_window_word_nearest(const char *word);

/* THE NEAR-MISS MATCHER ITSELF, over ANY closed vocabulary -- which is what
 * `gb_window_word_nearest` is now three lines of. Extracted when the file
 * chooser's option set became the second closed vocabulary in this backend:
 * two copies of a matcher drift, and the comment on the window version already
 * records that THE VOCABULARY ITSELF had been written out three times and the
 * newest word was missing from the third. One matcher, two callers.
 *
 * Answers NULL when nothing is close, because a suggestion that is sometimes
 * nonsense is worse than none. `set` is NULL-terminated. */
const char *gb_nearest_word(const char *word, const char *const *set);

#endif /* GBASIC_WINDOW_CHECK_H */

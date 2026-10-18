/* ---- validation of a `window`, against whatever it is written as -----------
 *
 * See include/window_check.h for WHY these rules are here rather than in
 * src/frontend.c, where they were written and where they walked the AST
 * directly. The short version: §4 of docs/qt_gui_design.md says the block is
 * sugar over a builder, that door turned out to be already open, and a built
 * record was getting NONE of this. Two copies of a declaration's rules drift,
 * so there is one copy and two adapters.
 *
 * The same treatment the `server` block gets, in the same pass and the same
 * sink, which is what puts every refusal into --json-diagnostics and an
 * editor's error list for nothing. It is also what makes the whole declarative
 * surface testable WITH NO DISPLAY -- the property that let `web.routes` be
 * proven with no socket, and the reason the declaration shipped before any Qt.
 *
 * THE VOCABULARY IS CLOSED AND THAT IS THE POINT. A misspelled directive is a
 * widget nobody sees: it would lay out, draw nothing an author recognises, and
 * raise nothing. Closed, it is a located refusal naming the nearest real word.
 *
 * WHAT IS DELIBERATELY NOT CHECKED, said here rather than discovered: a LEAF's
 * options. Those are widget properties and Qt exposes 75 on a button alone, so
 * the set cannot be closed without Qt in the build -- the head's options can
 * and are. */

#include "window_check.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const GbWinModel *m;
    const void *win;
    int errors;
} WinRun;

static void win_error(WinRun *run, const void *item, const char *fmt, ...) {
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    run->m->report(run->m->ctx, run->win, item, message);
    run->errors++;
}

/* CONTAINERS TAKE A BODY AND LEAVES DO NOT, which is not a taxonomy but the
 * grammar's own discipline: every head shape decides its own body-ness, which
 * is what makes the block cost zero bison conflicts. A word is deliberately
 * ABSENT until it exists, so one written today is refused as unknown rather
 * than silently accepted and ignored. */
static const char *win_container_words[] = {"row", "column", "form", "group",
                                            "table", "menu", "tabs", "tab",
                                            "cells", NULL};
static const char *win_leaf_words[] = {"field", "button", "check", "text",
                                       "memo", "choice", "stretch", "item",
                                       "separator", NULL};

static int win_in(const char *const *set, const char *word) {
    for (size_t i = 0; set[i]; i++) {
        if (strcmp(set[i], word) == 0) {
            return 1;
        }
    }
    return 0;
}

/* "row, column, ... and cells" -- an English list, because the sentence it
 * goes into is read by somebody who has just mistyped a word. */
static void win_join(char *out, size_t cap, const char *const *set) {
    size_t n = 0;
    out[0] = '\0';
    while (set[n]) { n++; }
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
        const char *sep = (i == 0) ? "" : (i + 1 == n) ? " and " : ", ";
        int wrote = snprintf(out + used, cap - used, "%s%s", sep, set[i]);
        if (wrote < 0 || (size_t)wrote >= cap - used) {
            break;
        }
        used += (size_t)wrote;
    }
}

int gb_window_word_is_container(const char *word) {
    return word ? win_in(win_container_words, word) : 0;
}

int gb_window_word_known(const char *word) {
    if (!word) {
        return 0;
    }
    return win_in(win_container_words, word) || win_in(win_leaf_words, word);
}

/* The nearest real word: one substitution, one insertion, one deletion -- OR
 * ONE TRANSPOSITION, which is the correction that matters.
 *
 * `colunm` for `column` is a transposition, and so are `feild`, `recieve` and
 * most of what fingers actually do; under a plain one-edit rule it needs TWO
 * edits and no suggestion is offered. Measured by writing the test first and
 * watching it miss the exact example the comment claimed to catch -- the
 * commonest typo there is was the one case the first version could not see.
 *
 * Still short of a Levenshtein table nobody would maintain, and it answers
 * NULL when nothing is close, because a suggestion that is sometimes nonsense
 * is worse than none. */
const char *gb_window_word_nearest(const char *word) {
    /* DERIVED FROM THE TWO VOCABULARY TABLES, not a third copy of them. It WAS
     * a third copy, and `table` was added to the containers and to the
     * sentence that lists them and missed HERE -- so a reader who typed
     * `tabel` got the generic list instead of the one-word answer, which is
     * the defect the near-miss finder exists to prevent, for the newest word
     * in the language. Two representations of one list drift;
     * run_web_configure.sh's TABLES tripwire makes that argument for the
     * server block's own options, and run_window.sh now asserts every word
     * here is reachable. */
    const char *all[32];
    size_t n = 0;
    if (!word) {
        return NULL;
    }
    for (size_t i = 0; win_container_words[i] && n + 1 < 32; i++) {
        all[n++] = win_container_words[i];
    }
    for (size_t i = 0; win_leaf_words[i] && n + 1 < 32; i++) {
        all[n++] = win_leaf_words[i];
    }
    all[n] = NULL;
    size_t len = strlen(word);
    for (size_t i = 0; all[i]; i++) {
        size_t other = strlen(all[i]);
        if (other + 1 < len || len + 1 < other) {
            continue;
        }
        /* ONE TRANSPOSITION: equal length, and exactly two adjacent positions
         * swapped. Checked first because it is the common case. */
        if (other == len) {
            size_t diff = 0, first = 0;
            for (size_t k = 0; k < len; k++) {
                if (word[k] != all[i][k]) {
                    if (diff == 0) { first = k; }
                    diff++;
                }
            }
            if (diff == 0) {
                return all[i];
            }
            if (diff == 2 && first + 1 < len &&
                word[first] == all[i][first + 1] &&
                word[first + 1] == all[i][first] &&
                word[first + 1] != all[i][first + 1]) {
                return all[i];
            }
        }
        size_t a = 0, b = 0, edits = 0;
        while (a < len && b < other && edits <= 1) {
            if (word[a] == all[i][b]) {
                a++; b++;
                continue;
            }
            edits++;
            if (len > other)       { a++; }
            else if (other > len)  { b++; }
            else                   { a++; b++; }
        }
        edits += (len - a) + (other - b);
        if (edits <= 1) {
            return all[i];
        }
    }
    return NULL;
}

/* A WINDOW AND AN ITEM BOTH HOLD CHILDREN and the model asks for them
 * differently, the window's items not being an item's entries. One pair of
 * helpers rather than two walks, so a rule cannot hold at one level and not
 * the other. */
static size_t win_child_count(WinRun *run, const void *owner, int is_window) {
    return is_window ? run->m->win_item_count(owner) : run->m->entry_count(owner);
}

static const void *win_child(WinRun *run, const void *owner, int is_window,
                             size_t i) {
    return is_window ? run->m->win_item(owner, i) : run->m->entry(owner, i);
}

static void win_check_items(WinRun *run, const void *owner, int is_window,
                            const char *window_subject, const char *parent_word,
                            const char **names, size_t *name_count,
                            size_t name_cap);

/* One item, the window's subject so a bare `form` can inherit it, and THE
 * PARENT'S WORD, which `menu` is the first directive to need: a menu bar
 * belongs to the WINDOW rather than to a layout, so where a `menu` may appear
 * is a fact about its surroundings and not about itself. */
static void win_check_item(WinRun *run, const void *item,
                           const char *window_subject, const char *parent_word,
                           const char **names, size_t *name_count,
                           size_t name_cap) {
    const GbWinModel *M = run->m;
    if (M->kind(item) == GB_WIN_HANDLER) {
        /* Its target is checked once the whole window is known. WHETHER IT
         * CARRIES ANYTHING TO CALL is checked here, and only a BUILT window
         * can get that wrong -- a declared handler has a body because the
         * grammar gave it one. A handler that cannot be called is the same
         * defect as one that can never fire. */
        if (!M->handler_callable(item)) {
            win_error(run, item,
                      "window: `on %s.%s` carries nothing to call",
                      M->target(item) ? M->target(item) : "?",
                      M->signal(item) ? M->signal(item) : "?");
        }
        return;
    }
    const char *word = M->word(item) ? M->word(item) : "";
    const char *iname = M->name(item);
    int is_container = win_in(win_container_words, word);
    int is_leaf = win_in(win_leaf_words, word);

    if (!is_container && !is_leaf) {
        const char *near = gb_window_word_nearest(word);
        if (near) {
            win_error(run, item,
                      "window: unknown directive '%s' -- did you mean '%s'?",
                      word, near);
        } else {
            /* THE SENTENCE IS BUILT FROM THE TABLES, which is the same
             * correction `win_nearest` needed: this was a FOURTH hardcoded
             * copy of the vocabulary, in prose, so adding a word meant
             * editing a list nothing checked. A reader who typed something
             * nothing is close to would have been handed a list missing the
             * newest words -- the defect the closed vocabulary exists to
             * prevent, in the message that exists to explain it. */
            char containers[256], parts[256];
            win_join(containers, sizeof(containers), win_container_words);
            win_join(parts, sizeof(parts), win_leaf_words);
            win_error(run, item,
                      "window: unknown directive '%s'. The containers are %s; "
                      "the parts are %s", word, containers, parts);
        }
        return;
    }

    /* THE WORD AND THE SHAPE MUST AGREE. The grammar decides shape from the
     * head alone, so `field x` with a body parses perfectly as a container --
     * and would lay out a field containing other things, which is not a thing. */
    if (is_leaf && M->kind(item) == GB_WIN_CONTAINER) {
        win_error(run, item,
                  "window: '%s' takes no body -- write `%s name( ... )` on one line",
                  word, word);
        return;
    }
    if (is_container && M->kind(item) == GB_WIN_LEAF) {
        win_error(run, item,
                  "window: '%s' is a container and needs a body ending `end %s`",
                  word, word);
        return;
    }

    if (M->kind(item) == GB_WIN_CONTAINER) {
        const char *closer = M->close_word(item);
        if (closer && strcmp(closer, word) != 0) {
            win_error(run, item,
                      "window: '%s' is closed by `end %s`, not `end %s`",
                      word, word, closer);
        }
        /* A `form` NAMES ITS SUBJECT OR INHERITS THE WINDOW'S, and a window
         * with neither is a form about nothing. The window's subject is a
         * DEFAULT a form may shadow -- the rule a nested `with principal(p)`
         * already follows. */
        int carries_subject = (strcmp(word, "form") == 0 ||
                               strcmp(word, "table") == 0);
        if (carries_subject) {
            const char *dir = M->direction(item);
            if (dir && strcmp(dir, "edit") != 0 && strcmp(dir, "show") != 0) {
                win_error(run, item,
                          "window: a %s is `to edit` or `to show`, not `to %s`",
                          word, dir);
            }
            if (!M->subject(item) && !window_subject) {
                win_error(run, item,
                          "window: this %s names no subject and the window has "
                          "none to inherit -- write `%s to edit <record>`",
                          word, word);
            }
        } else if (M->subject(item)) {
            win_error(run, item,
                      "window: only a form or a table carries a subject, not '%s'",
                      word);
        }
        /* A MENU BAR BELONGS TO THE WINDOW, so a `menu` sits at the window's
         * top level or inside another menu, and nowhere else. Refused rather
         * than placed somewhere surprising: Qt would put a QMenuBar wherever
         * the layout said, so a `menu` inside a `row` would render as a bar in
         * the middle of the screen -- a plausible-looking widget nobody asked
         * for, which is the class the closed vocabulary exists to prevent. */
        if (strcmp(word, "menu") == 0) {
            if (!parent_word ||
                (strcmp(parent_word, "window") != 0 &&
                 strcmp(parent_word, "menu") != 0)) {
                win_error(run, item,
                          "window: a menu belongs to the window or to another "
                          "menu, not inside '%s' -- a menu bar is not laid out",
                          parent_word ? parent_word : "?");
            }
            if (!M->label(item)) {
                win_error(run, item,
                          "window: a menu needs a caption -- `menu \"File\"` "
                          "-- because that is what a person reads on the bar");
            }
            if (iname) {
                win_error(run, item,
                          "window: a menu takes no name; its ITEMS are what a "
                          "handler addresses");
            }
            size_t count = M->entry_count(item);
            for (size_t i = 0; i < count; i++) {
                const void *part = M->entry(item, i);
                const char *pw = M->word(part);
                if (M->kind(part) == GB_WIN_HANDLER || !pw) {
                    continue;
                }
                if (strcmp(pw, "item") != 0 && strcmp(pw, "separator") != 0 &&
                    strcmp(pw, "menu") != 0) {
                    win_error(run, part,
                              "window: a menu holds items, separators and "
                              "submenus -- '%s' is none of those", pw);
                }
            }
        }

        /* A TABLE IS NAMED AND A FORM IS NOT, which is a real difference and
         * not a spelling: a form's parts are addressed by their own names,
         * where a table shows MANY rows, so the table itself is what a handler
         * and `qt.selected` address. Refused both ways, so neither shape can
         * be written the other's way and quietly mean something else. */
        if (strcmp(word, "table") == 0 && !iname) {
            win_error(run, item,
                      "window: a table needs a name -- `table orders to show "
                      "<list>` -- because that is what a handler and "
                      "`qt.selected` address");
        }
        /* WHICH CONTAINERS MAY BE NAMED, and it is a short list on purpose: a
         * name is what a handler and a property read address, so it belongs to
         * a container that IS a widget with state of its own. A `table` shows
         * many rows and a `tabs` has a current page; a `row` is a box and a
         * `tab` is a PAGE whose CAPTION is what a person reads, the rule a
         * `menu` follows. */
        int may_be_named = (strcmp(word, "table") == 0 ||
                            strcmp(word, "tabs") == 0);
        if (!may_be_named && iname) {
            win_error(run, item,
                      "window: '%s' takes no name; a table and a `tabs` are "
                      "named, because they are the containers with state a "
                      "handler can ask about", word);
        }

        /* A `tab` BELONGS TO A `tabs` and needs a caption; a `tabs` holds
         * nothing else. Refused both ways, because a QTabWidget has NO LAYOUT
         * of its own -- its children are pages -- so anything else inside one
         * would be dropped silently rather than mislaid. */
        if (strcmp(word, "tabs") == 0) {
            size_t count = M->entry_count(item);
            for (size_t i = 0; i < count; i++) {
                const void *part = M->entry(item, i);
                const char *pw = M->word(part);
                if (M->kind(part) == GB_WIN_HANDLER || !pw) {
                    continue;
                }
                if (strcmp(pw, "tab") != 0) {
                    win_error(run, part,
                              "window: `tabs` holds `tab` pages -- '%s' is not "
                              "one; put it inside a tab", pw);
                }
            }
        }
        /* `cells` HOLDS ROWS OF CELLS, which is also why it needed no new
         * word: a grid IS rows, so `row` is reused -- refined inside `cells`
         * to mean a row whose columns ALIGN with the rows above, where a `row`
         * elsewhere is a box that does not. Anything else directly inside
         * `cells` has no row to go in, so it is refused rather than placed at
         * whatever position happened to be current. */
        if (strcmp(word, "cells") == 0) {
            size_t count = M->entry_count(item);
            for (size_t i = 0; i < count; i++) {
                const void *part = M->entry(item, i);
                const char *pw = M->word(part);
                if (M->kind(part) == GB_WIN_HANDLER || !pw) {
                    continue;
                }
                if (strcmp(pw, "row") != 0) {
                    win_error(run, part,
                              "window: `cells` holds `row`s of cells -- '%s' "
                              "is not one; put it inside a row", pw);
                }
            }
        }
        if (strcmp(word, "tab") == 0) {
            if (!parent_word || strcmp(parent_word, "tabs") != 0) {
                win_error(run, item,
                          "window: a `tab` belongs to a `tabs`, not to '%s'",
                          parent_word ? parent_word : "?");
            }
            if (!M->label(item)) {
                win_error(run, item,
                          "window: a tab needs a caption -- `tab \"General\"` "
                          "-- because that is what a person reads on it");
            }
        }
        /* A NAMED CONTAINER REGISTERS ITS NAME TOO. Only leaves did, so
         * `on orders.activated` on a perfectly real table would have been
         * refused as naming nothing in the window -- the refusal that exists
         * to catch a handler that can never fire, firing on one that can. */
        if (iname) {
            for (size_t i = 0; i < *name_count; i++) {
                if (strcmp(names[i], iname) == 0) {
                    win_error(run, item,
                              "window: '%s' is declared twice, so `on "
                              "%s.<signal>` could not say which", iname, iname);
                    break;
                }
            }
            if (*name_count < name_cap) {
                names[*name_count] = iname;
                *name_count = *name_count + 1;
            }
        }
        /* A TABLE'S PARTS ARE COLUMNS, so every one must be a `field` -- §7's
         * own decision, and the saving is that `field` already means "one value
         * of the subject". A `button` among them would be a widget in a place
         * that holds values, which lays out as nothing an author recognises.
         *
         * AND A COLUMN'S NAME BELONGS TO ITS TABLE, NOT TO THE WINDOW, which
         * is a correction the first runnable example produced immediately: a
         * screen listing orders and showing the chosen one is the commonest
         * shape there is, and BOTH want a `who` and a `total` -- the list's
         * are columns, the form's are widgets. With the columns registered
         * window-wide that example was refused as declaring `who` twice, which
         * is a true sentence about a program with nothing wrong with it.
         *
         * So the columns are checked HERE (named, unique within their own
         * table) and the recursion that would register them window-wide is
         * skipped. `qt.cell(win, "orders", r, "who")` addresses one, which is
         * a column of a named table rather than a thing in the window. */
        if (strcmp(word, "table") == 0) {
            const char *seen[64];
            size_t seen_count = 0;
            size_t count = M->entry_count(item);
            for (size_t i = 0; i < count; i++) {
                const void *part = M->entry(item, i);
                const char *pw = M->word(part);
                const char *pn = M->name(part);
                if (M->kind(part) == GB_WIN_HANDLER) {
                    continue;
                }
                if (!pw || strcmp(pw, "field") != 0) {
                    win_error(run, part,
                              "window: a table's parts are columns, so they are "
                              "all `field` -- '%s' is not one", pw ? pw : "?");
                    continue;
                }
                if (!pn) {
                    win_error(run, part,
                              "window: a table's column needs a name -- it is "
                              "the field of the subject this column shows");
                    continue;
                }
                for (size_t j = 0; j < seen_count; j++) {
                    if (strcmp(seen[j], pn) == 0) {
                        win_error(run, part,
                                  "window: table '%s' shows '%s' twice, so two "
                                  "columns would hold one field",
                                  iname ? iname : "?", pn);
                        break;
                    }
                }
                if (seen_count < 64) {
                    seen[seen_count++] = pn;
                }
            }
            return;   /* columns are not window-wide names */
        }
        win_check_items(run, item, 0, window_subject, word, names, name_count,
                        name_cap);
        return;
    }

    /* AN ITEM AND A SEPARATOR ARE MENU WORDS, and the mirror of the rule above:
     * a menu may not be laid out, and a menu's parts may not appear where
     * things ARE laid out. `item open( ... )` in a `column` would otherwise
     * reach `gb_qt_part`, which has no widget for the word and would refuse at
     * RUN time -- late, and only for somebody with Qt in their build. */
    if (strcmp(word, "item") == 0 || strcmp(word, "separator") == 0) {
        if (!parent_word || strcmp(parent_word, "menu") != 0) {
            win_error(run, item,
                      "window: '%s' belongs in a menu, not in '%s'", word,
                      parent_word ? parent_word : "?");
            return;
        }
    }
    if (strcmp(word, "separator") == 0) {
        /* A SEPARATOR IS A LINE and names nothing, the rule `stretch` follows
         * for being pure space. */
        if (iname) {
            win_error(run, item,
                      "window: a separator is a line and takes no name");
        }
        return;
    }

    /* A LEAF'S NAME IS WHAT A HANDLER AND A BINDING ADDRESS, so `field()` with
     * none is refused -- except `stretch`, which is pure space and names
     * nothing. */
    if (strcmp(word, "stretch") == 0) {
        if (iname) {
            win_error(run, item,
                      "window: stretch is space and takes no name");
        }
        return;
    }
    if (!iname) {
        win_error(run, item,
                  "window: '%s' needs a name -- `%s id( ... )` -- because that is "
                  "what a handler and a binding address", word, word);
        return;
    }
    for (size_t i = 0; i < *name_count; i++) {
        if (strcmp(names[i], iname) == 0) {
            win_error(run, item,
                      "window: '%s' is declared twice, so `on %s.<signal>` could "
                      "not say which", iname, iname);
            return;
        }
    }
    if (*name_count < name_cap) {
        names[*name_count] = iname;
        *name_count = *name_count + 1;
    }
}

static void win_check_items(WinRun *run, const void *owner, int is_window,
                            const char *window_subject, const char *parent_word,
                            const char **names, size_t *name_count,
                            size_t name_cap) {
    size_t count = win_child_count(run, owner, is_window);
    for (size_t i = 0; i < count; i++) {
        win_check_item(run, win_child(run, owner, is_window, i), window_subject,
                       parent_word, names, name_count, name_cap);
    }
}

/* Every handler in the tree, checked against the names the window declares.
 * THIS IS THE REFUSAL LOAD-TIME VALIDATION EXISTS FOR: `on save.clicked` with
 * no `button save` is a handler that can never fire, and nothing at run time
 * would ever say so -- the button is simply not there to connect. */
static void win_check_handlers(WinRun *run, const void *owner, int is_window,
                               const char **names, size_t name_count) {
    const GbWinModel *M = run->m;
    size_t count = win_child_count(run, owner, is_window);
    for (size_t i = 0; i < count; i++) {
        const void *item = win_child(run, owner, is_window, i);
        if (M->kind(item) == GB_WIN_HANDLER) {
            int found = 0;
            const char *target = M->target(item);
            for (size_t j = 0; j < name_count; j++) {
                if (target && strcmp(names[j], target) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                win_error(run, item,
                          "window: `on %s.%s` names nothing in this window, so it "
                          "could never fire", target ? target : "?",
                          M->signal(item) ? M->signal(item) : "?");
            }
        } else if (M->kind(item) == GB_WIN_CONTAINER) {
            win_check_handlers(run, item, 0, names, name_count);
        }
    }
}

int gb_window_check(const GbWinModel *model, const void *win) {
    WinRun run;
    run.m = model;
    run.win = win;
    run.errors = 0;

    /* THE HEAD'S OPTIONS ARE CLOSED, unlike a leaf's: these are the window's
     * own, not a widget's, so the set is knowable without Qt. A misspelling
     * that was ignored would leave an author believing they had asked for
     * something -- the rule `make_dir` and `webserver.listen` already follow. */
    static const char *head[] = {"title", "width", "height", NULL};
    const char *wname = model->win_name(win);
    size_t opts = model->win_option_count(win);
    for (size_t i = 0; i < opts; i++) {
        const char *on = model->win_option_name(win, i);
        if (!win_in(head, on ? on : "")) {
            win_error(&run, NULL,
                      "window '%s': unknown option '%s' (the options are title, "
                      "width and height)", wname ? wname : "?", on ? on : "?");
        }
    }
    const char *dir = model->win_direction(win);
    if (dir && strcmp(dir, "edit") != 0 && strcmp(dir, "show") != 0) {
        win_error(&run, NULL,
                  "window '%s': the shorthand is `to edit` or `to show`, not `to %s`",
                  wname ? wname : "?", dir);
    }
    const char *closer = model->win_close_word(win);
    if (closer && strcmp(closer, "window") != 0) {
        win_error(&run, NULL,
                  "window '%s' is closed by `end window`, not `end %s`",
                  wname ? wname : "?", closer);
    }

    const char *names[256];
    size_t name_count = 0;
    /* "window" IS THE PARENT AT THE TOP LEVEL, which is the one place a `menu`
     * may appear outside another menu -- a menu bar belongs to the window. */
    win_check_items(&run, win, 1, model->win_subject(win), "window", names,
                    &name_count, 256);
    win_check_handlers(&run, win, 1, names, name_count);
    return run.errors;
}

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
    /* THE WINDOW'S OWN NAME, carried because two rules below need it and
     * neither is where it was read. It is in the SAME namespace as the parts:
     * the backend registers the root widget in the one by-name table, so
     * `on app.closed` addresses the window exactly as `on save.clicked`
     * addresses a button -- which also means a part called `app` would shadow
     * it, and is refused by name rather than silently winning. */
    const char *window_name;
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
                                            "table", "menu", "popup", "tabs",
                                            "tab", "cells", "split", "scroll",
                                            "tree", "docks", "dock", "toolbar",
                                            "status", NULL};
static const char *win_leaf_words[] = {"field", "button", "check", "text",
                                       "memo", "choice", "source", "stretch",
                                       "item", "separator", NULL};

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

/* THE NEAR-MISS MATCHER, over any closed vocabulary. EXTRACTED rather than
 * copied when the file chooser's option names became the second closed set in
 * this backend (docs/qt_gui_design.md §0.16): the comment below already
 * records that the WINDOW VOCABULARY had been written out three times and the
 * newest word was missing from the third, which is the same drift one level
 * up. One matcher, two callers. */
const char *gb_nearest_word(const char *word, const char *const *set) {
    if (!word || !set) {
        return NULL;
    }
    size_t len = strlen(word);
    for (size_t i = 0; set[i]; i++) {
        size_t other = strlen(set[i]);
        if (other + 1 < len || len + 1 < other) {
            continue;
        }
        /* ONE TRANSPOSITION: equal length, and exactly two adjacent positions
         * swapped. Checked first because it is the common case. */
        if (other == len) {
            size_t diff = 0, first = 0;
            for (size_t k = 0; k < len; k++) {
                if (word[k] != set[i][k]) {
                    if (diff == 0) { first = k; }
                    diff++;
                }
            }
            if (diff == 0) {
                return set[i];
            }
            if (diff == 2 && first + 1 < len &&
                word[first] == set[i][first + 1] &&
                word[first + 1] == set[i][first] &&
                word[first + 1] != set[i][first + 1]) {
                return set[i];
            }
        }
        size_t a = 0, b = 0, edits = 0;
        while (a < len && b < other && edits <= 1) {
            if (word[a] == set[i][b]) {
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
            return set[i];
        }
    }
    return NULL;
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
    return gb_nearest_word(word, all);
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

/* A PART MAY NOT TAKE THE WINDOW'S OWN NAME. The window is addressable by it
 * -- `qt.read(win, "app", "windowTitle")`, `qt.invoke(win, "app", "close")`,
 * `on app.closed` -- because the backend registers the root widget in the one
 * by-name table every part uses, which is what makes the window reachable for
 * no new machinery at all. The cost of that is this collision, and the part
 * would quietly WIN it (registered second, over the root), so `on app.closed`
 * would connect to a button and the window's own title would be unreadable.
 *
 * SEPARATE FROM THE TWICE-DECLARED MESSAGE because the remedy is different:
 * nothing is declared twice here, and "rename one of them" is wrong advice
 * when one of the two is the window. */
static int win_shadows_window(WinRun *run, const void *item,
                              const char *iname) {
    if (!iname || !run->window_name || strcmp(iname, run->window_name) != 0) {
        return 0;
    }
    win_error(run, item,
              "window: the window itself is called '%s', so a part cannot be "
              "-- `on %s.closed` and `qt.read(win, \"%s\", ...)` address the "
              "window", iname, iname, iname);
    return 1;
}

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
        /* A `tree` CARRIES A SUBJECT LIKE A TABLE, and like a table its rows
         * are many -- the difference is that each one declares a DEPTH, so
         * what is drawn is a tree rather than a list. */
        int carries_subject = (strcmp(word, "form") == 0 ||
                               strcmp(word, "table") == 0 ||
                               strcmp(word, "tree") == 0);
        if (carries_subject) {
            const char *dir = M->direction(item);
            /* `ask` IS A TABLE'S THIRD DIRECTION AND A FORM'S HAS ONLY TWO.
             * A form shows or edits ONE record whose fields it can read; an
             * asking table's rows are wherever the program keeps them and this
             * side never sees them, so there is nothing for a form to ask FOR
             * -- and `form x to ask f` would be a declaration whose subject
             * could never be bound. Refused by naming the two a form has,
             * which is also what stops `ask` spreading to every word that
             * carries a subject just because one of them wanted it. */
            int table_word = strcmp(word, "table") == 0;
            /* A TREE IS READ, SO IT HAS ONE DIRECTION. `to edit` would mean
             * writing a value back from a tree of rows, which nothing has
             * asked for and which has no answer for a row that is a PARENT;
             * `to ask` is a table's third direction and exists because a
             * million rows cannot be held, where a tree's shape comes from
             * depths the program already has in hand. Refused by naming the
             * one it has, which is also what stops a direction spreading to
             * every word that carries a subject because one of them wanted
             * it. */
            int tree_word = strcmp(word, "tree") == 0;
            int known = dir && ((strcmp(dir, "show") == 0) ||
                                (!tree_word && strcmp(dir, "edit") == 0) ||
                                (table_word && strcmp(dir, "ask") == 0));
            if (dir && !known) {
                if (tree_word) {
                    win_error(run, item,
                              "window: a tree is `to show`, not `to %s` -- its "
                              "rows are read, and a parent row has no value to "
                              "write back", dir);
                } else if (table_word) {
                    win_error(run, item,
                              "window: a table is `to show`, `to edit` or "
                              "`to ask`, not `to %s`", dir);
                } else {
                    win_error(run, item,
                              "window: a %s is `to edit` or `to show`, not "
                              "`to %s`", word, dir);
                }
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
                 strcmp(parent_word, "menu") != 0 &&
                 /* A SUBMENU OF A CONTEXT MENU needs no vocabulary of its own:
                  * `menu "Recent"` inside a `popup` is the same production
                  * recursing, exactly as it does inside another `menu`. */
                 strcmp(parent_word, "popup") != 0)) {
                win_error(run, item,
                          "window: a menu belongs to the window, to another "
                          "menu or to a popup, not inside '%s' -- a menu bar "
                          "is not laid out",
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

        /* THE WINDOW'S OWN FURNITURE (§0.30) -- `docks`, `dock`, `toolbar`
         * and `status`. All four are QMainWindow features, which is the
         * finding that made them one increment rather than three: the
         * remaining asks in docs/qt_studio_requests.md are dock panes (§3), a
         * status bar and a toolbar (§4), and every one of them hangs off a
         * QMainWindow rather than needing a vocabulary of its own.
         *
         * THEY BELONG TO THE WINDOW, NOT TO A LAYOUT, which is the `menu`
         * rule reached by the same measurement one widget along: a QMainWindow
         * places its own dock areas, status bar and tool bars, and a
         * QDockWidget added to a box layout is laid out like any other widget
         * -- a titled frame in the middle of the screen with no dock
         * behaviour, which is a plausible-looking widget nobody asked for. */
        if (strcmp(word, "docks") == 0 || strcmp(word, "toolbar") == 0 ||
            strcmp(word, "status") == 0) {
            if (!parent_word || strcmp(parent_word, "window") != 0) {
                win_error(run, item,
                          "window: a `%s` belongs to the window, not inside "
                          "'%s' -- the window places its own furniture", word,
                          parent_word ? parent_word : "?");
            }
        }

        /* A `docks` BLOCK CARRIES THE AREA, and the area is a WORD in the one
         * slot -- `split across`'s arrangement exactly, and chosen for the
         * same reason: a container carries a name or options and not both
         * (§0.8, measured at four reduce/reduce), and an area whose only
         * correct values are four known words is not something to spell as a
         * string.
         *
         * GROUPING THEM IS WHAT THE ASK WANTS rather than a convenience:
         * Studio's right-hand column is five stacked panes, and five docks in
         * ONE area is what Qt tabs together -- *"five panes become five tabs
         * along one edge, which is what every professional IDE does with
         * exactly this content."* */
        if (strcmp(word, "docks") == 0) {
            static const char *areas[] = {"left", "right", "top", "bottom",
                                          NULL};
            if (M->label(item)) {
                win_error(run, item,
                          "window: a dock area is a word, not a string -- "
                          "`docks right`, without the quotes");
            } else if (!iname) {
                win_error(run, item,
                          "window: a `docks` block needs an area -- `docks "
                          "left`, `right`, `top` or `bottom`");
            } else if (!win_in(areas, iname)) {
                const char *near = gb_nearest_word(iname, areas);
                if (near) {
                    win_error(run, item,
                              "window: a dock area is `left`, `right`, `top` "
                              "or `bottom`, not '%s' -- did you mean `%s`?",
                              iname, near);
                } else {
                    win_error(run, item,
                              "window: a dock area is `left`, `right`, `top` "
                              "or `bottom`, not '%s' (the slot carries the "
                              "area, not a name)", iname);
                }
            }
            size_t count = M->entry_count(item);
            size_t panes = 0;
            for (size_t i = 0; i < count; i++) {
                const void *part = M->entry(item, i);
                const char *pw = M->word(part);
                if (M->kind(part) == GB_WIN_HANDLER || !pw) {
                    continue;
                }
                panes++;
                if (strcmp(pw, "dock") != 0) {
                    win_error(run, part,
                              "window: a `docks` block holds `dock` panes -- "
                              "'%s' is not one", pw);
                }
            }
            if (panes == 0) {
                win_error(run, item,
                          "window: this `docks` block holds no panes, so it "
                          "reserves an edge of the window and shows nothing");
            }
        }

        /* A `dock` IS A PANE OF A `docks` BLOCK and nowhere else -- the area
         * lives on the block, so a dock outside one has no area to go in and
         * nothing would place it. */
        if (strcmp(word, "dock") == 0) {
            if (!parent_word || strcmp(parent_word, "docks") != 0) {
                win_error(run, item,
                          "window: a `dock` is a pane of a `docks` block, not "
                          "of '%s' -- the block is what carries the area",
                          parent_word ? parent_word : "?");
            }
            /* IT NEEDS BOTH, which is the one thing in this vocabulary that
             * does -- and it is affordable because `dock branches "Branches"`
             * MEASURED AT ZERO CONFLICTS. The NAME is what `qt.set` and
             * `qt.dock` address and what Qt's own `saveState` keys the layout
             * by (a dock with no `objectName` makes saveState WARN and drop
             * it, measured); the CAPTION is what a person reads on the pane's
             * title bar. */
            if (!iname) {
                win_error(run, item,
                          "window: a dock needs a name -- `dock branches "
                          "\"Branches\"` -- because the name is how a program "
                          "shows, hides and finds it again");
            }
            if (!M->label(item)) {
                win_error(run, item,
                          "window: a dock needs a caption -- `dock branches "
                          "\"Branches\"` -- because that is what a person "
                          "reads on its title bar");
            }
        }

        /* A TOOLBAR HOLDS WHAT A MENU HOLDS, which is why it needed no new
         * part kind: measured, a QToolBar's entries are QActions, the same
         * objects §0.7 already builds for a menu, and `checkable:` and an
         * `icon:` both work on one in a toolbar. */
        if (strcmp(word, "toolbar") == 0) {
            if (!iname) {
                win_error(run, item,
                          "window: a toolbar needs a name -- `toolbar main` "
                          "-- because Qt keys a saved layout by it and a "
                          "program shows and hides it by it");
            }
            if (M->label(item)) {
                win_error(run, item,
                          "window: a toolbar's slot carries its NAME, not a "
                          "caption -- `toolbar main`, without the quotes");
            }
            size_t count = M->entry_count(item);
            for (size_t i = 0; i < count; i++) {
                const void *part = M->entry(item, i);
                const char *pw = M->word(part);
                if (M->kind(part) == GB_WIN_HANDLER || !pw) {
                    continue;
                }
                if (strcmp(pw, "item") != 0 && strcmp(pw, "separator") != 0) {
                    win_error(run, part,
                              "window: a toolbar holds items and separators "
                              "-- '%s' is neither", pw);
                }
            }
        }

        /* A STATUS BAR TAKES NO NAME, and that is the `menu` rule for the
         * same reason: there is exactly one per window, and what a program
         * updates is the TEXT IN IT rather than the bar.
         *
         * WHAT IT HOLDS WAS DECIDED BY A MEASUREMENT: a QStatusBar has NO
         * property for its text -- `message` and `currentMessage` are both
         * absent and `showMessage(QString)` takes an argument `qt.invoke`
         * cannot pass -- but a permanent QLabel inside it carries `text` as an
         * ordinary Q_PROPERTY. So a status bar holds NAMED `text` parts and
         * `qt.set(win, "msg", "text", ...)` reaches them through the by-name
         * route that already exists, which is §1's argument paying off again
         * rather than a verb of its own. */
        if (strcmp(word, "status") == 0) {
            if (iname) {
                win_error(run, item,
                          "window: a status bar takes no name; the `text` "
                          "parts IN it are what a program updates");
            }
            size_t count = M->entry_count(item);
            for (size_t i = 0; i < count; i++) {
                const void *part = M->entry(item, i);
                const char *pw = M->word(part);
                if (M->kind(part) == GB_WIN_HANDLER || !pw) {
                    continue;
                }
                if (strcmp(pw, "text") != 0 && strcmp(pw, "stretch") != 0) {
                    win_error(run, part,
                              "window: a status bar holds `text` and "
                              "`stretch` -- '%s' is neither, and a status bar "
                              "is not a layout", pw);
                }
            }
        }

        /* A CONTEXT MENU. The same placement rule as a menu bar and for the
         * same measured reason -- a QMenu is placed by nothing, so a `popup`
         * inside a `row` would be a declaration the layout silently ignores --
         * but the opposite rule about its one slot: a `menu` MUST have a
         * caption and MUST NOT have a name, and a `popup` is the mirror. A
         * caption on a popup would name the thing nobody ever reads, since a
         * context menu has no bar to appear on. */
        if (strcmp(word, "popup") == 0) {
            if (!parent_word || strcmp(parent_word, "window") != 0) {
                win_error(run, item,
                          "window: a popup menu belongs to the window, not "
                          "inside '%s' -- a menu is placed by nothing, so one "
                          "written here would never be laid out",
                          parent_word ? parent_word : "?");
            }
            if (!iname) {
                win_error(run, item,
                          "window: a popup menu needs a name -- `popup "
                          "row_actions` -- because `qt.popup` is the only way "
                          "to raise one");
            }
            if (M->label(item)) {
                win_error(run, item,
                          "window: a popup menu takes no caption; nobody reads "
                          "it, because it never appears on a bar -- give it a "
                          "NAME instead");
            }
            size_t pcount = M->entry_count(item);
            for (size_t i = 0; i < pcount; i++) {
                const void *part = M->entry(item, i);
                const char *pw = M->word(part);
                if (M->kind(part) == GB_WIN_HANDLER || !pw) {
                    continue;
                }
                if (strcmp(pw, "item") != 0 && strcmp(pw, "separator") != 0 &&
                    strcmp(pw, "menu") != 0) {
                    win_error(run, part,
                              "window: a popup menu holds items, separators "
                              "and submenus -- '%s' is none of those", pw);
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
        /* AND SO DOES A TREE, for the identical reason. */
        if (strcmp(word, "tree") == 0 && !iname) {
            win_error(run, item,
                      "window: a tree needs a name -- `tree files to show "
                      "<rows>` -- because that is what a handler and "
                      "`qt.selected` address");
        }
        /* A `split` IS A ROW OR COLUMN YOU CAN DRAG, and its one free slot
         * carries the DIRECTION rather than a name. That is forced rather than
         * chosen: §0.8 measured that a container may carry a name OR options
         * and not both (`tabs settings( movable: true )` costs four
         * reduce/reduce), so there is exactly one slot, and a splitter with no
         * direction is not a splitter -- where a name is something only some
         * splitters would ever want.
         *
         * THE COST IS STATED: a `split` cannot be named, so `on x.splitterMoved`
         * and `qt.set` cannot reach it. `qt.pane` is what makes it observable
         * anyway, asked of the layout by walking up from a part that CAN be
         * named -- the rule `qt.at` already follows for a grid. */
        if (strcmp(word, "split") == 0) {
            static const char *dirs[] = {"across", "down", NULL};
            if (M->label(item)) {
                win_error(run, item,
                          "window: a split's direction is a word, not a string "
                          "-- `split across`, without the quotes");
            } else if (!iname) {
                win_error(run, item,
                          "window: a split needs a direction -- `split across` "
                          "or `split down`");
            } else if (!win_in(dirs, iname)) {
                const char *near = gb_nearest_word(iname, dirs);
                if (near) {
                    win_error(run, item,
                              "window: a split is `across` or `down`, not '%s' "
                              "-- did you mean `%s`?", iname, near);
                } else {
                    win_error(run, item,
                              "window: a split is `across` or `down`, not '%s' "
                              "(the slot carries the direction, not a name)",
                              iname);
                }
            }
            /* AT LEAST TWO PANES, MEASURED RATHER THAN ASSUMED: a QSplitter
             * puts a draggable handle BETWEEN panes, so n panes give n-1
             * handles and ONE pane gives ZERO -- a container nobody can drag,
             * which is an author who meant `column`. */
            size_t panes = 0;
            size_t count = M->entry_count(item);
            for (size_t i = 0; i < count; i++) {
                if (M->kind(M->entry(item, i)) != GB_WIN_HANDLER) {
                    panes++;
                }
            }
            if (panes < 2) {
                win_error(run, item,
                          "window: a split needs at least two panes -- with %zu "
                          "there is nothing to drag, so you want a `row` or a "
                          "`column`", panes);
            }
        }

        /* WHICH CONTAINERS MAY BE NAMED, and the rule is "a container with
         * state of its own that something addresses", not a list anybody is
         * free to add to: a name is what a handler, a property read and now a
         * RECONCILE address. A `table` shows many rows and a `tabs` has a
         * current page.
         *
         * §0.27 WIDENED IT BY ITS OWN JUSTIFICATION RATHER THAN BY ANALOGY. A
         * `column`, a `row` and a `group` were refused on the grounds that a
         * box has no state worth asking about -- and `qt.refresh(win, name,
         * items)` gives every one of them some: THE SET OF CHILDREN. The verb
         * cannot address what cannot be named, so refusing the name refuses
         * the feature, which is the same argument that named a `scroll`.
         *
         * A `form` IS STILL REFUSED, deliberately (§0.6): its parts are
         * addressed by their own names, so there is nothing a form's name
         * would be for -- and a `cells` grid is refused too, since its
         * positions are DECLARED and a reconcile would have to invent them. */
        int may_be_named = (strcmp(word, "table") == 0 ||
                            strcmp(word, "tabs") == 0 ||
                            /* A `scroll` NEEDS NO DIRECTION, so its slot is
                             * free -- and naming it is what makes
                             * `widgetResizable` and the scrollbar policies
                             * readable through the ordinary by-name route,
                             * which is the whole of its observability. */
                            strcmp(word, "scroll") == 0 ||
                            /* A `popup` IS NAMED RATHER THAN CAPTIONED, which
                             * is the one slot spent differently from `menu`:
                             * a context menu is never read on a bar, so a
                             * caption would name nothing, and
                             * `qt.popup(win, "row_actions")` is how it is
                             * raised at all. Required, not merely allowed --
                             * checked below. */
                            strcmp(word, "popup") == 0 ||
                            /* A `tab` IS NAMED SO A RECONCILE HAS AN IDENTITY
                             * (§0.27), and the reason is sharper than symmetry
                             * with its siblings: `qt.refresh(win, name, items)`
                             * keeps a child whose name is unchanged, and THE
                             * CAPTION IS THE THING THAT CHANGES -- a notebook
                             * tab carries a `* ` unsaved marker -- so keying on
                             * the caption would rebuild the editor on the first
                             * keystroke in any document, which is the cost the
                             * verb exists to remove.
                             *
                             * THE DECLARATION STILL CANNOT SPELL BOTH, because
                             * §0.8's one slot is a fact about the GRAMMAR: a
                             * captioned `tab "One"` and a named `tab one` are
                             * two shapes of one production and neither carries
                             * the other. A RECORD has no such limit, which is
                             * what §0.9's builder already rests on, and the
                             * callers that need identity are the ones building
                             * a record. */
                            strcmp(word, "tab") == 0 ||
                            /* THE BOXES, for the reconcile above. A declared
                             * one still spends its one slot either way, so
                             * `column stack` carries no options and
                             * `column( margin: 6 )` carries no name -- §0.8's
                             * rule unchanged. */
                            strcmp(word, "column") == 0 ||
                            strcmp(word, "row") == 0 ||
                            strcmp(word, "group") == 0 ||
                            /* A `tree` IS A TABLE'S SIBLING HERE: its rows are
                             * many, so the tree is what addresses them. */
                            strcmp(word, "tree") == 0 ||
                            /* A `dock` AND A `toolbar` ARE ADDRESSED (§0.30):
                             * a program shows and hides them, and Qt's own
                             * `saveState` keys a stored layout by the
                             * `objectName` their name becomes -- measured, one
                             * without a name makes saveState WARN and drop it,
                             * which would make "restore my layout" silently
                             * partial. A `dock` is also the one thing here
                             * carrying a name AND a caption, which costs
                             * nothing: `dock branches "Branches"` measured at
                             * zero conflicts. */
                            strcmp(word, "dock") == 0 ||
                            strcmp(word, "toolbar") == 0);
        /* A `split` IS NEITHER: its slot is the direction, checked above, so it
         * must not reach this refusal NOR be registered as a name below. */
        if (strcmp(word, "split") == 0) {
            may_be_named = 1;
        }
        /* AND A `docks` BLOCK IS THE SAME: its slot is the AREA, checked
         * above, so it must not be refused here nor registered as a name. */
        if (strcmp(word, "docks") == 0) {
            may_be_named = 1;
        }
        if (!may_be_named && iname) {
            win_error(run, item,
                      "window: '%s' takes no name; a container is named "
                      "when something addresses it -- a table, a `tabs`, a "
                      "`scroll`, a `popup`, a `tab`, and a `column`, `row` or "
                      "`group` whose children `qt.refresh` reconciles", word);
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
        /* A `docks` BLOCK'S SLOT IS AN AREA, not a name, so it is not
         * registered -- `docks right` twice in one window is two blocks in one
         * area, not a name declared twice. A `dock` and a `toolbar` ARE
         * registered, which is the point of their names. */
        if (iname && strcmp(word, "split") != 0 &&
            strcmp(word, "docks") != 0) {
            if (win_shadows_window(run, item, iname)) {
                /* reported; not registered, so nothing after it is confused */
            } else {
            for (size_t i = 0; i < *name_count; i++) {
                if (strcmp(names[i], iname) == 0) {
                    win_error(run, item,
                              "window: '%s' is declared twice, so `on "
                              "%s.<signal>` could not say which", iname, iname);
                    break;
                }
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
        if (strcmp(word, "table") == 0 || strcmp(word, "tree") == 0) {
            const char *seen[64];
            size_t seen_count = 0;
            /* A TREE'S DEPTH IS A COLUMN THAT IS NOT DRAWN, declared
             * `field depth( depth: true )`. The alternative was a convention
             * -- a field CALLED depth is the depth -- which is magic a reader
             * cannot see, where an option says it. AT MOST ONE, because two
             * would be a declaration with two answers and one of them would
             * silently win; NONE is legal and useful, being a flat tree, which
             * is what a program that owns its own expansion hands over. */
            int depth_fields = 0;
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
                              "window: a %s's parts are columns, so they are "
                              "all `field` -- '%s' is not one", word,
                              pw ? pw : "?");
                    continue;
                }
                if (!pn) {
                    win_error(run, part,
                              "window: a %s's column needs a name -- it is "
                              "the field of the subject this column shows",
                              word);
                    continue;
                }
                if (M->option_true && M->option_true(part, "depth")) {
                    depth_fields++;
                    if (depth_fields > 1) {
                        win_error(run, part,
                                  "window: a tree takes one depth column and "
                                  "this is the %d%s -- two would be a "
                                  "declaration with two answers",
                                  depth_fields,
                                  depth_fields == 2 ? "nd" : "rd");
                    }
                    if (strcmp(word, "table") != 0) {
                        continue;   /* not a drawn column; name reuse is fine */
                    }
                    win_error(run, part,
                              "window: `depth:` belongs to a tree's column, "
                              "and this is a table -- a table has no depth to "
                              "draw");
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
    /* AN `item` BELONGS IN A MENU and nowhere else -- `item open( ... )` in a
     * `column` would otherwise reach `gb_qt_part`, which has no widget for the
     * word, and fail at RUN time: late, and only for somebody with Qt.
     *
     * A `separator` IS DIFFERENT NOW, and deliberately so: it means A LINE
     * THAT DIVIDES, which is as true of a rule between two panes as of a line
     * in a menu, so ONE WORD covers both and the backend picks the widget. It
     * was refused outside a menu until Studio's whole widget surface was
     * counted rather than reasoned about -- it uses two of them, one as the
     * rule between panes and one as the divider between groups of toolbar
     * buttons, and neither could be written. */
    if (strcmp(word, "item") == 0) {
        if (!parent_word || (strcmp(parent_word, "menu") != 0 &&
                             strcmp(parent_word, "popup") != 0 &&
                             /* A TOOLBAR TOO, since §0.30, and it needed no
                              * new part kind: measured, a QToolBar's entries
                              * are QActions -- the same objects a menu is
                              * built from -- and `checkable:` and `icon:`
                              * both work on one in a toolbar. */
                             strcmp(parent_word, "toolbar") != 0)) {
            win_error(run, item,
                      "window: 'item' belongs in a menu, a popup or a "
                      "toolbar, not in '%s'", parent_word ? parent_word : "?");
            return;
        }
    }
    if (strcmp(word, "separator") == 0) {
        /* WHERE A RULE CAN GO IS WHERE THERE IS SOMETHING TO DIVIDE: a `row`
         * or a `column`, or a container that gets one of its own. Refused in a
         * `cells` grid (no direction to take) and as a splitter pane (which
         * already has a draggable handle between its panes), so a line never
         * appears at an orientation nobody chose -- measured, the wrong way
         * round renders as a stubby stub taking a share of the width. */
        int in_menu = parent_word && (strcmp(parent_word, "menu") == 0 ||
                                      strcmp(parent_word, "popup") == 0 ||
                                      /* A TOOLBAR'S OWN DIVIDER, which is the
                                       * SECOND of the two uses Studio has for
                                       * a separator -- the comment above
                                       * already named it ("the divider between
                                       * groups of toolbar buttons") and there
                                       * was no toolbar to put one in. */
                                      strcmp(parent_word, "toolbar") == 0);
        int in_box = parent_word && (strcmp(parent_word, "row") == 0 ||
                                     strcmp(parent_word, "column") == 0 ||
                                     strcmp(parent_word, "group") == 0 ||
                                     strcmp(parent_word, "tab") == 0 ||
                                     strcmp(parent_word, "scroll") == 0 ||
                                     strcmp(parent_word, "window") == 0);
        if (!in_menu && !in_box) {
            win_error(run, item,
                      "window: a separator divides a menu or a row or a "
                      "column -- there is nothing for one to divide in '%s'",
                      parent_word ? parent_word : "?");
            return;
        }
    }
    if (strcmp(word, "separator") == 0) {
        /* A MENU'S SEPARATOR NAMES NOTHING -- it is a QAction with no
         * properties worth reading, the rule `stretch` follows for being pure
         * space. A LAYOUT'S RULE MAY BE NAMED, and that is not a loosening for
         * symmetry: it is a real widget with real properties, and STUDIO
         * STYLES ITS OWN (`studio_style.apply(sep, "rule")`), so a rule that
         * could not be addressed could not be themed. It is also the only way
         * to observe one -- `qt.read(win, name, "frameShape")` answers HLine
         * or VLine, and the orientation is the one thing that can be built
         * wrong: measured, the wrong way round renders as a stub taking a
         * share of the width rather than as a line. */
        int in_menu = parent_word && (strcmp(parent_word, "menu") == 0 ||
                                      strcmp(parent_word, "popup") == 0);
        if (iname && in_menu) {
            win_error(run, item,
                      "window: a menu's separator is a line and takes no "
                      "name; a rule in a row or a column may have one");
        }
        if (iname && !in_menu) {
            if (win_shadows_window(run, item, iname)) {
                /* reported; not registered */
            } else {
                for (size_t i = 0; i < *name_count; i++) {
                    if (strcmp(names[i], iname) == 0) {
                        win_error(run, item,
                                  "window: '%s' is declared twice, so `on "
                                  "%s.<signal>` could not say which", iname,
                                  iname);
                        break;
                    }
                }
            }
            if (*name_count < name_cap) {
                names[*name_count] = iname;
                *name_count = *name_count + 1;
            }
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
    /* A PANE HAS NOWHERE TO PUT A LABEL and that refusal is NOT HERE, which is
     * worth writing down because this is where I first put it and it could not
     * work. `M->label(item)` reads a TOP-LEVEL label -- which is what a
     * `group "Credit"` or a `menu "File"` carries -- while A LEAF'S `label:` IS
     * AN OPTION, so it reads NULL for every leaf and the check silently never
     * fired. That is §0.8's own recorded defect reproduced exactly: the table
     * heading extraction read the top-level label and fell back to the field's
     * name, captioning a table with its own column names.
     *
     * Deciding it here would need an option accessor on an ITEM, which the
     * model has only for the window HEAD -- and §0.8 already puts leaf-option
     * rules on the Qt side, a leaf's options being widget properties that
     * cannot be closed without Qt in the build. So `gb_qt_part` refuses it,
     * where the label is a parameter and the parent is in hand.
     */
    if (!iname) {
        win_error(run, item,
                  "window: '%s' needs a name -- `%s id( ... )` -- because that is "
                  "what a handler and a binding address", word, word);
        return;
    }
    if (win_shadows_window(run, item, iname)) {
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
    run.window_name = model->win_name(win);

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
    /* THE WINDOW IS A TARGET LIKE ANY PART, which is what makes `on
     * app.closed` a handler rather than a refusal: `win_check_handlers`
     * matches a target against this list, so seeding it is the whole of making
     * the window addressable at load time. It is also what `win_shadows_window`
     * keeps a part out of. */
    if (run.window_name) {
        names[name_count++] = run.window_name;
    }
    /* AT MOST ONE STATUS BAR (§0.30), which is a fact about QMainWindow rather
     * than a rule chosen here: `statusBar()` answers ONE bar, so a second
     * `status` block would have its parts added to the SAME bar and the two
     * declarations would silently merge -- a window with more text in its
     * status bar than either block asked for. Several `toolbar`s and several
     * `docks` blocks are legal, because Qt genuinely holds several of each. */
    {
        size_t top = model->win_item_count(win);
        size_t bars = 0;
        const void *second = NULL;
        for (size_t i = 0; i < top; i++) {
            const void *it = model->win_item(win, i);
            const char *iw = model->word(it);
            if (model->kind(it) == GB_WIN_HANDLER || !iw) {
                continue;
            }
            if (strcmp(iw, "status") == 0) {
                bars++;
                if (bars == 2) {
                    second = it;
                }
            }
        }
        if (bars > 1) {
            win_error(&run, second,
                      "window '%s' declares %zu status bars; a window has one, "
                      "so the second would add its text to the first",
                      wname ? wname : "?", bars);
        }
    }
    /* "window" IS THE PARENT AT THE TOP LEVEL, which is the one place a `menu`
     * may appear outside another menu -- a menu bar belongs to the window. */
    win_check_items(&run, win, 1, model->win_subject(win), "window", names,
                    &name_count, 256);
    win_check_handlers(&run, win, 1, names, name_count);
    return run.errors;
}

/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * THE TERMINAL'S `GbUiBackend` TABLE (include/ui_backend.h; docs/tui_design.md
 * §3 and §7 increment 3): a `window` drawn in cells, over src/term.c, the
 * layer the `term` module also stands on. `#include`d by the front under
 * `#if GBASIC_TERM`.
 *
 * A RETAINED MODEL. Qt keeps its own widgets; a terminal keeps nothing, so
 * this file holds the tree (TuiNode), lays it out in cells, draws it into
 * one screen buffer and lets term.c write only what changed.
 *
 * WHAT INCREMENT 3 DRAWS: the windows of the common core's layout (`row`,
 * `column`, `form`, `group`, `stretch`, `separator`) and its first leaves
 * (`field`, `button`, `check`, `text`), with forms bound, focus moved by Tab,
 * and the mouse. Every other part is refused BY NAME, naming the increment
 * that draws it, never built as something that silently does nothing.
 *
 * NO TERMINAL, NO PROBLEM, FOR A TEST. GBASIC_TUI_SCREEN=COLSxROWS draws into
 * the same buffer with no terminal at all (the `offscreen` of this backend),
 * and GBASIC_TUI_DUMP=PATH writes the screen there as text after each frame,
 * so a suite holds what was drawn without a pty. Keys then come from
 * `ui.key` and `ui.click`, which go through the same dispatch a terminal's
 * keys do. */

/* A refusal into the caller's `err`; answers 0, a member's "refused". */
static int tui_say(char *err, size_t err_size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_size, fmt, ap);
    va_end(ap);
    return 0;
}
#define TUI_ERR(...) tui_say(err, err_size, __VA_ARGS__)

#include <fnmatch.h>

typedef struct TuiNode TuiNode;
struct TuiNode {
    char word[16];          /* the part word, or "window" for a root */
    char *name, *label, *text, *hint, *tip, *title;
    int secret, read_only, enabled, visible, checked, max_length, is_default;
    int display_only, bold, wrap, font_size;
    long grow;
    int grow_set;
    int margin, spacing;    /* cells; -1 unset */
    int min_w, min_h, max_w, max_h, want_w, want_h;   /* cells; 0 unset */
    TuiNode *parent;
    TuiNode **kids;
    int nkids, cap;
    char *on_pressed, *on_changed, *on_entered, *on_context, *on_closed;
    size_t caret;           /* a field's caret, a byte offset into text */
    int scroll;             /* a field's first shown column */
    int x, y, w, h;         /* laid out, 0-based cells */
    int kind;               /* a root's: 0 plain, 1 modal, 2 main */
    int shown;              /* a root that is on screen */
    /* a root's frame, 0-based cells: its title row is fy, its furniture and
     * content the rows under it; the screen, or a modal window's box */
    int fx, fy, fw, fh;
    /* FURNITURE is held by the root but not laid out with its content: a
     * menu bar, the popups, a toolbar, a status bar (increment 4) */
    int furniture;
    /* a table or a tree: headers, cells row-major, the selection and the
     * first row shown; a tree's depth per row; an asking table's function
     * and row count */
    char **headers;
    char aligns[64];        /* per column: 'l', 'r', 'c' */
    int ncols;
    char **cells;
    int nrows, rows_cap;
    int *depths;
    int is_tree, is_virtual;
    char *cell_fn;
    long vrows;
    int selected, top;
    int headings, striped, sortable, expanders, indent;
    char *on_chosen, *on_activated, *on_wheel;
    /* a choice's list */
    char **items;
    int nitems, index;
    /* a menu item: its shortcut and whether it is a check */
    char *shortcut;
    int checkable;
    /* a tabs' page; a split's direction (0 across, 1 down) and pane sizes;
     * a scroll's offset */
    int direction;
    int sizes[16];
    int nsizes;
    int offset;
    int tab_side_bottom, movable, closable;
    /* a part in `cells`: its place and span */
    int grid_row, grid_col, across, down;
    char toolbar_shows[8];
    char emphasis[10];
};

/* ---- increment 4 (src/modules/ui_tui_parts.c, included at the end) ------------ */

static int tuip_measure(TuiNode *n, int *w, int *h);
static int tuip_arrange(TuiNode *n, int x, int y, int w, int h);
static int tuip_draw(TuiNode *n);
static int tuip_focusable(const TuiNode *n);
static int tuip_key(TuiNode *f, const TermEvent *ev);
static int tuip_mouse(TuiNode *hit, const TermEvent *ev);
static void tuip_content(TuiNode *root, int *top, int *bottom);
static void tuip_furniture_draw(TuiNode *root);
static int tuip_overlay_open(void);
static int tuip_overlay_key(const TermEvent *ev);
static int tuip_overlay_mouse(const TermEvent *ev);
static void tuip_overlay_draw(void);
static void tuip_overlay_close_all(void);
static int tuip_window_key(TuiNode *root, const TermEvent *ev);
static int tuip_window_mouse(TuiNode *root, const TermEvent *ev);
static void tuip_reveal(TuiNode *n);
static void *tuip_container(TuiNode *p, const char *word, const char *name, const char *title,
                            char *err, size_t err_size);
static int tuip_part(TuiNode *p, const char *word, TuiNode *n, char *err, size_t err_size);
static int tuip_set_text(TuiNode *n, const char *prop, const char *value, char *err, size_t err_size);
static int tuip_set_bool(TuiNode *n, const char *prop, int value, char *err, size_t err_size);
static int tuip_set_number(TuiNode *n, const char *prop, double value, char *err, size_t err_size);
static int tuip_get(TuiNode *n, const char *prop, char *kind, double *num, char **text);
static char **tuip_event_slot(TuiNode *n, const char *signal);
static int tuip_click_row(TuiNode *n, int row, int right, char *err, size_t err_size);
static void tuip_item_press(TuiNode *item);
static int tuip_is_grid(const TuiNode *n);

static GbUiInvokeFn tui_g_invoke = NULL;
static GbUiTickFn tui_g_tick = NULL;
static GbUiClosedFn tui_g_closed = NULL;
static GbUiCellFn tui_g_cell = NULL;
static int tui_depth = 0;
static int tui_begun = 0;
static int tui_headless = 0;        /* GBASIC_TUI_SCREEN: a buffer, no terminal */
static int tui_entered = 0;         /* the terminal is in raw mode and the alternate screen */
static int tui_stop_asked = 0;
static int tui_running = 0;
static TermScreen tui_screen;
static int tui_screen_ready = 0;
static TermDecoder tui_decoder;
static double tui_pending_since = 0;
static TuiNode *tui_roots[8];
static int tui_nroots = 0;
static TuiNode *tui_focus = NULL;
static TuiNode *tui_retired[64];    /* closed under a handler, freed by retire_drain */
static int tui_nretired = 0;
static int tui_dirty = 1;
/* the theme record's colours; TERM_DEFAULT where the terminal's own is kept */
static uint32_t tui_bg = TERM_DEFAULT, tui_fg = TERM_DEFAULT, tui_base = TERM_DEFAULT,
                tui_ctl = TERM_DEFAULT, tui_hi = TERM_DEFAULT, tui_faint = TERM_DEFAULT,
                tui_line = TERM_DEFAULT, tui_sel = TERM_DEFAULT, tui_on_sel = TERM_DEFAULT;

static const char *const tui_later[][2] = {
    { "memo", "6" }, { "source", "6" }, { "markdown", "6" },
    { NULL, NULL }
};

/* THE ONE SENTENCE for what this increment does not draw yet. */
static int tui_not_yet(const char *what, char *err, size_t err_size) {
    const char *inc = "a later increment";
    for (size_t i = 0; tui_later[i][0]; i++) {
        if (strcmp(tui_later[i][0], what) == 0) {
            inc = tui_later[i][1];
            break;
        }
    }
    if (strlen(inc) == 1) {
        snprintf(err, err_size, "the tui backend does not draw `%s` yet "
                 "(docs/tui_design.md §7, increment %s)", what, inc);
    } else {
        snprintf(err, err_size, "the tui backend does not do `%s` yet "
                 "(docs/tui_design.md §7)", what);
    }
    return 0;
}

static char *tui_dup(const char *s) {
    return s ? copy_string(s) : NULL;
}

static void tui_set(char **slot, const char *s) {
    free(*slot);
    *slot = tui_dup(s);
}

static TuiNode *tui_node_new(const char *word, const char *name, const char *label) {
    TuiNode *n = calloc(1, sizeof *n);
    if (!n) {
        abort();
    }
    snprintf(n->word, sizeof n->word, "%s", word);
    n->name = tui_dup(name && *name ? name : NULL);
    n->label = tui_dup(label);
    n->enabled = 1;
    n->visible = 1;
    n->margin = n->spacing = -1;
    n->selected = -1;
    n->headings = 1;
    n->expanders = 1;
    n->indent = 2;
    n->across = n->down = 1;
    n->grid_row = n->grid_col = -1;
    return n;
}

static void tui_add_kid(TuiNode *parent, TuiNode *kid, int at) {
    if (parent->nkids == parent->cap) {
        parent->cap = parent->cap ? parent->cap * 2 : 4;
        parent->kids = realloc(parent->kids, sizeof(TuiNode *) * (size_t)parent->cap);
        if (!parent->kids) {
            abort();
        }
    }
    if (at < 0 || at > parent->nkids) {
        at = parent->nkids;
    }
    memmove(parent->kids + at + 1, parent->kids + at, sizeof(TuiNode *) * (size_t)(parent->nkids - at));
    parent->kids[at] = kid;
    parent->nkids++;
    kid->parent = parent;
}

static void tui_unlink(TuiNode *kid) {
    TuiNode *p = kid->parent;
    if (!p) {
        return;
    }
    for (int i = 0; i < p->nkids; i++) {
        if (p->kids[i] == kid) {
            memmove(p->kids + i, p->kids + i + 1, sizeof(TuiNode *) * (size_t)(p->nkids - i - 1));
            p->nkids--;
            break;
        }
    }
    kid->parent = NULL;
}

static int tui_within(TuiNode *n, TuiNode *root) {
    for (; n; n = n->parent) {
        if (n == root) {
            return 1;
        }
    }
    return 0;
}

static void tui_node_free(TuiNode *n) {
    if (!n) {
        return;
    }
    for (int i = 0; i < n->nkids; i++) {
        tui_node_free(n->kids[i]);
    }
    if (tui_focus && tui_within(tui_focus, n)) {
        tui_focus = NULL;
    }
    free(n->kids);
    free(n->name); free(n->label); free(n->text); free(n->hint); free(n->tip); free(n->title);
    free(n->on_pressed); free(n->on_changed); free(n->on_entered); free(n->on_context);
    free(n->on_closed); free(n->on_chosen); free(n->on_activated); free(n->on_wheel);
    for (int i = 0; i < n->ncols; i++) {
        free(n->headers[i]);
    }
    free(n->headers);
    for (long i = 0; i < (long)n->nrows * n->ncols; i++) {
        free(n->cells[i]);
    }
    free(n->cells);
    free(n->depths);
    free(n->cell_fn);
    for (int i = 0; i < n->nitems; i++) {
        free(n->items[i]);
    }
    free(n->items);
    free(n->shortcut);
    free(n);
}

static TuiNode *tui_find(TuiNode *n, const char *name) {
    if (!n || !name) {
        return NULL;
    }
    if (n->name && strcmp(n->name, name) == 0) {
        return n;
    }
    for (int i = 0; i < n->nkids; i++) {
        TuiNode *f = tui_find(n->kids[i], name);
        if (f) {
            return f;
        }
    }
    return NULL;
}

static int tui_is(const TuiNode *n, const char *word) {
    return strcmp(n->word, word) == 0;
}

static int tui_focusable(const TuiNode *n) {
    return n->visible && n->enabled && !n->furniture &&
           (tui_is(n, "field") || tui_is(n, "button") || tui_is(n, "check") || tuip_focusable(n));
}

/* ---- the terminal ------------------------------------------------------------ */

static double tui_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* GBASIC_TUI_SCREEN: "COLSxROWS", for a run with no terminal. */
static int tui_headless_size(int *columns, int *rows) {
    const char *s = getenv("GBASIC_TUI_SCREEN");
    if (!s || !*s) {
        return 0;
    }
    int c = 0, r = 0;
    if (sscanf(s, "%dx%d", &c, &r) != 2 || c < 10 || r < 3 || c > 1000 || r > 1000) {
        return 0;
    }
    *columns = c;
    *rows = r;
    return 1;
}

static int tui_can_display(void) {
    int c, r;
    if (tui_headless_size(&c, &r)) {
        return 1;
    }
    return term_unusable() == NULL && !term_open_now;
}

static void tui_leave_screen(void) {
    if (tui_entered) {
        term_leave();
        term_release_stderr();
        tui_entered = 0;
        term_screen_owner = 0;
    }
}

/* The terminal is entered when the first window is shown and left when the
 * last one closes, so a program's own output before and after is on the
 * ordinary screen. */
static int tui_enter_screen(char *err, size_t err_size) {
    if (tui_headless || tui_entered) {
        return 1;
    }
    const char *why = term_unusable();
    if (why) {
        return TUI_ERR("%s", why);
    }
    if (term_open_now) {
        return TUI_ERR("the terminal is held by term.open; close it before opening a window");
    }
    int columns, rows;
    if (term_size(&columns, &rows) != 0) {
        return TUI_ERR("the terminal does not report its size");
    }
    fflush(stdout);
    int rc = term_enter(1);
    if (rc != 0) {
        return TUI_ERR("raw mode could not be set: %s", strerror(rc));
    }
    term_hold_stderr();
    term_screen_owner = 2;
    tui_entered = 1;
    term_screen_resize(&tui_screen, columns, rows);
    tui_dirty = 1;
    return 1;
}

static int tui_begin(char *err, size_t err_size) {
    if (tui_begun) {
        return 1;
    }
    int columns = 80, rows = 24;
    tui_headless = tui_headless_size(&columns, &rows);
    if (!tui_headless) {
        const char *why = term_unusable();
        if (why) {
            return TUI_ERR("%s", why);
        }
        if (term_size(&columns, &rows) != 0) {
            return TUI_ERR("the terminal does not report its size");
        }
    }
    term_screen_init(&tui_screen, columns, rows,
                     tui_headless ? TERM_DEPTH_24BIT
                                  : term_depth_from_env(getenv("COLORTERM"), getenv("TERM")));
    tui_screen_ready = 1;
    term_decoder_init(&tui_decoder);
    tui_begun = 1;
    return 1;
}

static void tui_end(void) {
    tui_leave_screen();
    for (int i = 0; i < tui_nretired; i++) {
        tui_node_free(tui_retired[i]);
    }
    tui_nretired = 0;
    for (int i = 0; i < tui_nroots; i++) {
        tui_node_free(tui_roots[i]);
    }
    tui_nroots = 0;
    if (tui_screen_ready) {
        term_screen_free(&tui_screen);
        term_decoder_free(&tui_decoder);
        tui_screen_ready = 0;
    }
    tui_begun = 0;
}

static void tui_set_callbacks(GbUiInvokeFn invoke, GbUiTickFn tick, GbUiClosedFn closed,
                              GbUiCellFn cell) {
    tui_g_invoke = invoke;
    tui_g_tick = tick;
    tui_g_closed = closed;
    tui_g_cell = cell;
}

/* ---- sizes ------------------------------------------------------------------- */

/* Pixels to cells (§8.1): a nominal cell of 8 by 16, rounded up. */
static int tui_cells_across(double px) {
    return px <= 0 ? 0 : (int)((px + 7) / 8);
}

static int tui_cells_down(double px) {
    return px <= 0 ? 0 : (int)((px + 15) / 16);
}

static int tui_cell_size(int *width, int *height) {
    *width = 8;
    *height = 16;
    return 1;
}

static int tui_text_cells(const char *s) {
    if (!s) {
        return 0;
    }
    long w = term_text_width(s, strlen(s), NULL);
    return w < 0 ? (int)strlen(s) : (int)w;
}

/* ---- layout --------------------------------------------------------------------- */

/* what holds laid-out parts: a box, a page of a `tabs`, a pane holder, a
 * scrolled region, a grid's row */
static int tui_box(const TuiNode *n) {
    return tui_is(n, "row") || tui_is(n, "column") || tui_is(n, "form") ||
           tui_is(n, "group") || tui_is(n, "window") || tui_is(n, "tab") ||
           tui_is(n, "split") || tui_is(n, "scroll") || tui_is(n, "cells") ||
           tui_is(n, "status") || tui_is(n, "tabs");
}

static int tui_spacing(const TuiNode *n) {
    if (n->spacing >= 0) {
        return n->spacing;
    }
    return tui_is(n, "row") ? 1 : 0;
}

static int tui_margin(const TuiNode *n) {
    return n->margin >= 0 ? n->margin : 0;
}

/* The caption a field takes beside it in a box (in a form, the form draws
 * its captions in a column of their own). */
static int tui_has_caption(const TuiNode *n) {
    return tui_is(n, "field") && n->label && *n->label && n->parent && !tui_is(n->parent, "form");
}

static void tui_measure(TuiNode *n, int *w, int *h);

static void tui_clamp(const TuiNode *n, int *w, int *h) {
    if (n->want_w > *w) *w = n->want_w;
    if (n->want_h > *h) *h = n->want_h;
    if (n->min_w > *w) *w = n->min_w;
    if (n->min_h > *h) *h = n->min_h;
    if (n->max_w && *w > n->max_w) *w = n->max_w;
    if (n->max_h && *h > n->max_h) *h = n->max_h;
}

static int tui_form_label_width(const TuiNode *n) {
    int lw = 0;
    for (int i = 0; i < n->nkids; i++) {
        TuiNode *k = n->kids[i];
        if (k->visible && k->label && tui_is(k, "field")) {
            int c = tui_text_cells(k->label) + 1;
            if (c > lw) {
                lw = c;
            }
        }
    }
    return lw;
}

static void tui_measure(TuiNode *n, int *w, int *h) {
    *w = *h = 0;
    if (!n->visible || n->furniture) {
        return;
    }
    if (tuip_measure(n, w, h)) {
        tui_clamp(n, w, h);
        return;
    }
    const char *word = n->word;
    if (tui_is(n, "text")) {
        *w = tui_text_cells(n->text ? n->text : n->label);
        *h = 1;
    } else if (tui_is(n, "field")) {
        /* a size is the field's own, as in Qt; its caption is beside it */
        *w = tui_text_cells(n->hint) + 1;
        if (*w < 20) {
            *w = 20;
        }
        *h = 1;
        tui_clamp(n, w, h);
        if (tui_has_caption(n)) {
            *w += tui_text_cells(n->label) + 1;
        }
        return;
    } else if (tui_is(n, "button")) {
        *w = tui_text_cells(n->text ? n->text : n->label) + 4;
        *h = 1;
    } else if (tui_is(n, "check")) {
        *w = tui_text_cells(n->text ? n->text : n->label) + 4;
        *h = 1;
    } else if (tui_is(n, "separator")) {
        *w = *h = 1;
    } else if (tui_is(n, "stretch")) {
        *w = *h = 0;
    } else if (strcmp(word, "row") == 0) {
        int sp = tui_spacing(n), shown = 0;
        for (int i = 0; i < n->nkids; i++) {
            int kw, kh;
            tui_measure(n->kids[i], &kw, &kh);
            if (!n->kids[i]->visible) {
                continue;
            }
            *w += kw + (shown++ ? sp : 0);
            if (kh > *h) *h = kh;
        }
        *w += 2 * tui_margin(n);
        *h += 2 * tui_margin(n);
    } else if (tui_is(n, "form")) {
        int lw = tui_form_label_width(n), shown = 0, sp = tui_spacing(n);
        for (int i = 0; i < n->nkids; i++) {
            int kw, kh;
            tui_measure(n->kids[i], &kw, &kh);
            if (!n->kids[i]->visible) {
                continue;
            }
            if (lw + kw > *w) *w = lw + kw;
            *h += kh + (shown++ ? sp : 0);
        }
        *w += 2 * tui_margin(n);
        *h += 2 * tui_margin(n);
    } else {   /* column, group, window */
        int sp = tui_spacing(n), shown = 0;
        for (int i = 0; i < n->nkids; i++) {
            int kw, kh;
            tui_measure(n->kids[i], &kw, &kh);
            if (!n->kids[i]->visible) {
                continue;
            }
            if (kw > *w) *w = kw;
            *h += kh + (shown++ ? sp : 0);
        }
        *w += 2 * tui_margin(n);
        *h += 2 * tui_margin(n);
        if (tui_is(n, "group")) {
            int tw = tui_text_cells(n->title ? n->title : n->label) + 4;
            *w += 2;
            *h += 2;
            if (tw > *w) *w = tw;
        }
    }
    tui_clamp(n, w, h);
}

/* How much of a box's spare room a child takes: its `grow:` when given; a
 * stretch 1; and in a row, with no `grow:` anywhere, a field 1, as Qt's line
 * edit expands along a row. */
static long tui_weight(const TuiNode *k, int along_row, int any_grow) {
    if (k->grow_set) {
        return k->grow;
    }
    if (tui_is(k, "stretch")) {
        return 1;
    }
    /* what holds many rows takes the spare room down a column, as Qt's views
     * and areas expand */
    if (!along_row && (tui_is(k, "table") || tui_is(k, "tree") || tui_is(k, "tabs") ||
                       tui_is(k, "split") || tui_is(k, "scroll"))) {
        return 1;
    }
    if (along_row && !any_grow && tui_is(k, "field")) {
        return 1;
    }
    return 0;
}

static void tui_arrange(TuiNode *n, int x, int y, int w, int h);

static void tui_arrange_line(TuiNode *n, int x, int y, int w, int h, int along_row) {
    int m = tui_margin(n), sp = tui_spacing(n);
    x += m; y += m; w -= 2 * m; h -= 2 * m;
    int count = n->nkids, used = 0, shown = 0, any_grow = 0;
    long weights = 0;
    int *sizes = calloc((size_t)(count ? count : 1), sizeof(int));
    if (!sizes) {
        abort();
    }
    for (int i = 0; i < count; i++) {
        any_grow |= n->kids[i]->grow_set && n->kids[i]->grow > 0;
    }
    for (int i = 0; i < count; i++) {
        TuiNode *k = n->kids[i];
        if (!k->visible || k->furniture) {
            continue;
        }
        int kw, kh;
        tui_measure(k, &kw, &kh);
        sizes[i] = along_row ? kw : kh;
        used += sizes[i] + (shown++ ? sp : 0);
        weights += tui_weight(k, along_row, any_grow);
    }
    int spare = (along_row ? w : h) - used;
    if (spare > 0 && weights > 0) {
        int given = 0, last = -1;
        for (int i = 0; i < count; i++) {
            long wt = n->kids[i]->visible ? tui_weight(n->kids[i], along_row, any_grow) : 0;
            if (wt > 0) {
                int add = (int)(spare * wt / weights);
                sizes[i] += add;
                given += add;
                last = i;
            }
        }
        if (last >= 0) {
            sizes[last] += spare - given;
        }
    }
    int at = along_row ? x : y;
    shown = 0;
    for (int i = 0; i < count; i++) {
        TuiNode *k = n->kids[i];
        if (!k->visible || k->furniture) {
            continue;
        }
        if (shown++) {
            at += sp;
        }
        int kw, kh;
        tui_measure(k, &kw, &kh);
        if (along_row) {
            int ch = tui_is(k, "separator") ? h : (kh < h ? kh : h);
            if (k->max_w && sizes[i] > kw) {
                sizes[i] = kw;      /* grown no wider than its max_width */
            }
            tui_arrange(k, at, y, sizes[i], ch);
            at += sizes[i];
        } else {
            /* a button or a check keeps its own width in a column, as Qt's do */
            int cw = (tui_is(k, "button") || tui_is(k, "check") || k->max_w) && kw < w ? kw : w;
            tui_arrange(k, x, at, cw, sizes[i]);
            at += sizes[i];
        }
    }
    free(sizes);
}

static void tui_arrange(TuiNode *n, int x, int y, int w, int h) {
    n->x = x; n->y = y; n->w = w < 0 ? 0 : w; n->h = h < 0 ? 0 : h;
    if (!n->visible || n->furniture) {
        return;
    }
    if (tuip_arrange(n, x, y, w, h)) {
        return;
    }
    if (tui_is(n, "row")) {
        tui_arrange_line(n, x, y, w, h, 1);
    } else if (tui_is(n, "column") || tui_is(n, "window")) {
        tui_arrange_line(n, x, y, w, h, 0);
    } else if (tui_is(n, "group")) {
        tui_arrange_line(n, x + 1, y + 1, w - 2, h - 2, 0);
    } else if (tui_is(n, "form")) {
        int m = tui_margin(n), sp = tui_spacing(n), lw = tui_form_label_width(n);
        int at = y + m, shown = 0;
        for (int i = 0; i < n->nkids; i++) {
            TuiNode *k = n->kids[i];
            if (!k->visible) {
                continue;
            }
            if (shown++) {
                at += sp;
            }
            int kw, kh;
            tui_measure(k, &kw, &kh);
            tui_arrange(k, x + m + lw, at, w - 2 * m - lw, kh);
            at += kh;
        }
    }
}

/* A MODAL WINDOW IS A BOX OVER THE WINDOW UNDER IT (increment 5), sized by
 * its content, as a dialog is: its title is in the top border. */
static void tui_modal_frame(TuiNode *root) {
    int w, h, top = 0, bottom = 0;
    tui_measure(root, &w, &h);
    tuip_content(root, &top, &bottom);
    int tw = tui_text_cells(root->title ? root->title : root->name) + 4;
    if (tw > w) {
        w = tw;
    }
    int bw = w + 4, bh = h + (top - bottom) + 2;     /* a cell inside each side */
    if (bw > tui_screen.columns) {
        bw = tui_screen.columns;
    }
    if (bh > tui_screen.rows) {
        bh = tui_screen.rows;
    }
    int bx = (tui_screen.columns - bw) / 2, by = (tui_screen.rows - bh) / 2;
    root->fx = bx + 1;
    root->fy = by;
    root->fw = bw - 2;
    root->fh = bh - 1;
}

static void tui_layout(TuiNode *root) {
    /* the top row is the window's title, then its menu bar and toolbar;
     * a status bar is the last row; the rest is its content */
    if (root->kind == 1) {
        tui_modal_frame(root);
    } else {
        root->fx = 0;
        root->fy = 0;
        root->fw = tui_screen.columns;
        root->fh = tui_screen.rows;
    }
    int top = root->fy + 1, bottom = root->fy + root->fh;
    tuip_content(root, &top, &bottom);
    int inset = root->kind == 1 ? 1 : 0;
    tui_arrange(root, root->fx + inset, top, root->fw - 2 * inset, bottom - top);
}

/* ---- drawing ---------------------------------------------------------------- */

static TermStyle tui_style(uint32_t fg, uint32_t bg, unsigned attrs) {
    TermStyle s = { fg, bg, attrs };
    return s;
}

/* What may be drawn on, in 0-based cells: the screen, or less while a
 * scrolled region, a pane or a table is drawn (increment 4). */
static int tui_clip_x0 = 0, tui_clip_y0 = 0, tui_clip_x1 = 100000, tui_clip_y1 = 100000;

static void tui_put_clipped(int x, int y, int w, const char *text, TermStyle st) {
    if (w <= 0 || !text || y < tui_clip_y0 || y >= tui_clip_y1) {
        return;
    }
    if (x + w > tui_clip_x1) {
        w = tui_clip_x1 - x;
    }
    if (x < tui_clip_x0) {
        /* a text starting left of the clip is cut at the clip; rare
         * (a pane narrower than its part), and drawn from its start */
        w -= tui_clip_x0 - x;
        x = tui_clip_x0;
        if (w <= 0) {
            return;
        }
    }
    /* write codepoint by codepoint up to w cells, so a long text is cut
     * rather than spilling over its neighbour */
    const unsigned char *p = (const unsigned char *)text;
    size_t len = strlen(text), i = 0;
    int used = 0;
    while (i < len) {
        size_t n = 1;
        while (i + n < len && (p[i + n] & 0xC0) == 0x80) {
            n++;
        }
        long cw = term_text_width((const char *)p + i, n, NULL);
        if (cw < 0) {
            i += n;
            continue;
        }
        if (used + cw > w) {
            break;
        }
        term_screen_put(&tui_screen, x + 1 + used, y + 1, (const char *)p + i, n, st, NULL);
        used += (int)cw;
        i += n;
    }
}

static void tui_fill(int x, int y, int w, int h, TermStyle st) {
    if (x < tui_clip_x0) {
        w -= tui_clip_x0 - x;
        x = tui_clip_x0;
    }
    if (x + w > tui_clip_x1) {
        w = tui_clip_x1 - x;
    }
    if (y < tui_clip_y0) {
        h -= tui_clip_y0 - y;
        y = tui_clip_y0;
    }
    if (y + h > tui_clip_y1) {
        h = tui_clip_y1 - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    char *spaces = malloc((size_t)w + 1);
    if (!spaces) {
        abort();
    }
    memset(spaces, ' ', (size_t)w);
    for (int r = y; r < y + h; r++) {
        term_screen_put(&tui_screen, x + 1, r + 1, spaces, (size_t)w, st, NULL);
    }
    free(spaces);
}

/* The field's shown text: the bullets of a secret, one per codepoint. */
static char *tui_field_shown(const TuiNode *n) {
    const char *t = n->text ? n->text : "";
    if (!n->secret) {
        return copy_string(t);
    }
    size_t cps = 0;
    for (const unsigned char *p = (const unsigned char *)t; *p; p++) {
        cps += (*p & 0xC0) != 0x80;
    }
    char *out = malloc(cps * 3 + 1);
    if (!out) {
        abort();
    }
    for (size_t i = 0; i < cps; i++) {
        memcpy(out + i * 3, "\xE2\x80\xA2", 3);
    }
    out[cps * 3] = 0;
    return out;
}

/* Cells from the start of a field's text to its caret. */
static int tui_caret_cells(const TuiNode *n) {
    const char *t = n->text ? n->text : "";
    if (n->secret) {
        int c = 0;
        for (size_t i = 0; i < n->caret; i++) {
            c += ((unsigned char)t[i] & 0xC0) != 0x80;
        }
        return c;
    }
    long w = term_text_width(t, n->caret, NULL);
    return w < 0 ? (int)n->caret : (int)w;
}

static void tui_draw(TuiNode *n) {
    if (!n->visible || n->w <= 0 || n->furniture) {
        return;
    }
    if (tuip_draw(n)) {
        return;
    }
    int focused = n == tui_focus;
    unsigned dim = n->enabled ? 0 : TERM_DIM;
    TermStyle plain = tui_style(tui_fg, tui_bg, dim | (n->bold ? TERM_BOLD : 0));
    if (tui_is(n, "text")) {
        tui_put_clipped(n->x, n->y, n->w, n->text ? n->text : n->label, plain);
    } else if (tui_is(n, "button")) {
        TermStyle st = focused ? tui_style(tui_on_sel, tui_sel, TERM_REVERSE * (tui_sel == TERM_DEFAULT))
                               : tui_style(tui_fg, tui_ctl, dim | (n->is_default ? TERM_BOLD : 0));
        const char *t = n->text ? n->text : (n->label ? n->label : "");
        size_t len = strlen(t) + 5;
        char *s = malloc(len);
        if (!s) {
            abort();
        }
        snprintf(s, len, "[ %s ]", t);
        tui_put_clipped(n->x, n->y, n->w, s, st);
        free(s);
    } else if (tui_is(n, "check")) {
        TermStyle st = focused ? tui_style(tui_on_sel, tui_sel, TERM_REVERSE * (tui_sel == TERM_DEFAULT)) : plain;
        const char *t = n->text ? n->text : (n->label ? n->label : "");
        size_t len = strlen(t) + 5;
        char *s = malloc(len);
        if (!s) {
            abort();
        }
        snprintf(s, len, "[%s] %s", n->checked ? "x" : " ", t);
        tui_put_clipped(n->x, n->y, n->w, s, st);
        free(s);
    } else if (tui_is(n, "field")) {
        int x = n->x, w = n->w;
        if (tui_has_caption(n)) {
            int lw = tui_text_cells(n->label) + 1;
            tui_put_clipped(x, n->y, lw, n->label, plain);
            x += lw;
            w -= lw;
        }
        unsigned attrs = dim | (tui_base == TERM_DEFAULT ? TERM_UNDERLINE : 0) | (focused ? TERM_BOLD : 0);
        TermStyle box = tui_style(tui_fg, tui_base, attrs);
        tui_fill(x, n->y, w, 1, box);
        char *shown = tui_field_shown(n);
        int caret = tui_caret_cells(n);
        if (caret < n->scroll) {
            n->scroll = caret;
        }
        if (w > 0 && caret - n->scroll >= w) {
            n->scroll = caret - w + 1;
        }
        if (!*shown && n->hint && !focused) {
            tui_put_clipped(x, n->y, w, n->hint, tui_style(tui_faint, tui_base, attrs | (tui_faint == TERM_DEFAULT ? TERM_DIM : 0)));
        } else {
            /* skip `scroll` cells of the shown text */
            const char *p = shown;
            int skipped = 0;
            while (*p && skipped < n->scroll) {
                size_t k = 1;
                while (p[k] && ((unsigned char)p[k] & 0xC0) == 0x80) {
                    k++;
                }
                long cw = term_text_width(p, k, NULL);
                skipped += cw < 0 ? 1 : (int)cw;
                p += k;
            }
            tui_put_clipped(x, n->y, w, p, box);
        }
        free(shown);
        if (focused) {
            tui_screen.cursor_column = x + 1 + caret - n->scroll;
            tui_screen.cursor_row = n->y + 1;
            tui_screen.cursor_visible = 1;
        }
    } else if (tui_is(n, "separator")) {
        TermStyle st = tui_style(tui_line, tui_bg, tui_line == TERM_DEFAULT ? TERM_DIM : 0);
        if (n->parent && tui_is(n->parent, "row")) {
            for (int r = 0; r < n->h; r++) {
                tui_put_clipped(n->x, n->y + r, 1, "\xE2\x94\x82", st);
            }
        } else {
            for (int c = 0; c < n->w; c++) {
                tui_put_clipped(n->x + c, n->y, 1, "\xE2\x94\x80", st);
            }
        }
    } else if (tui_is(n, "group")) {
        TermStyle st = tui_style(tui_line, tui_bg, tui_line == TERM_DEFAULT ? TERM_DIM : 0);
        if (n->w >= 2 && n->h >= 2) {
            for (int c = 1; c < n->w - 1; c++) {
                tui_put_clipped(n->x + c, n->y, 1, "\xE2\x94\x80", st);
                tui_put_clipped(n->x + c, n->y + n->h - 1, 1, "\xE2\x94\x80", st);
            }
            for (int r = 1; r < n->h - 1; r++) {
                tui_put_clipped(n->x, n->y + r, 1, "\xE2\x94\x82", st);
                tui_put_clipped(n->x + n->w - 1, n->y + r, 1, "\xE2\x94\x82", st);
            }
            tui_put_clipped(n->x, n->y, 1, "\xE2\x94\x8C", st);
            tui_put_clipped(n->x + n->w - 1, n->y, 1, "\xE2\x94\x90", st);
            tui_put_clipped(n->x, n->y + n->h - 1, 1, "\xE2\x94\x94", st);
            tui_put_clipped(n->x + n->w - 1, n->y + n->h - 1, 1, "\xE2\x94\x98", st);
            const char *t = n->title ? n->title : n->label;
            if (t && *t && n->w > 4) {
                size_t len = strlen(t) + 3;
                char *s = malloc(len);
                if (!s) {
                    abort();
                }
                snprintf(s, len, " %s ", t);
                tui_put_clipped(n->x + 1, n->y, n->w - 2, s, plain);
                free(s);
            }
        }
    } else if (tui_is(n, "form")) {
        for (int i = 0; i < n->nkids; i++) {
            TuiNode *k = n->kids[i];
            if (k->visible && k->label && tui_is(k, "field")) {
                tui_put_clipped(n->x + tui_margin(n), k->y, tui_form_label_width(n), k->label,
                                tui_style(tui_fg, tui_bg, k->enabled ? 0 : TERM_DIM));
            }
        }
    }
    for (int i = 0; i < n->nkids; i++) {
        tui_draw(n->kids[i]);
    }
}

/* The window under the boxes: the last shown that is not modal. */
static TuiNode *tui_under(void) {
    for (int i = tui_nroots - 1; i >= 0; i--) {
        if (tui_roots[i]->shown && tui_roots[i]->kind != 1) {
            return tui_roots[i];
        }
    }
    return NULL;
}

/* The window on top, which takes the keys: the last modal window shown, as a
 * modal window holds the input in Qt too; otherwise the last shown. */
static TuiNode *tui_top(void) {
    for (int i = tui_nroots - 1; i >= 0; i--) {
        if (tui_roots[i]->shown && tui_roots[i]->kind == 1) {
            return tui_roots[i];
        }
    }
    return tui_under();
}

static void tui_box_draw(int x, int y, int w, int h, const char *title, TermStyle st, TermStyle ttl);
static void tui_dialogs_draw(void);
static int tui_dialog_open(void);
static int tui_dialog_key(const TermEvent *ev);
static void tui_dialog_mouse(const TermEvent *ev);
static void tui_dialogs_end_for(TuiNode *root);
static int tui_answer_modal(void *window, int accept, char *err, size_t err_size);

static void tui_dump(void) {
    const char *path = getenv("GBASIC_TUI_DUMP");
    if (!path || !*path) {
        return;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }
    for (int r = 0; r < tui_screen.rows; r++) {
        TermCell *line = &tui_screen.want[(size_t)r * tui_screen.columns];
        int last = tui_screen.columns - 1;
        while (last >= 0 && line[last].width == 1 && strcmp(line[last].ch, " ") == 0) {
            last--;
        }
        for (int c = 0; c <= last; c++) {
            fputs(line[c].ch, f);
        }
        fputc('\n', f);
    }
    if (tui_screen.cursor_visible) {
        fprintf(f, "cursor %d,%d\n", tui_screen.cursor_column, tui_screen.cursor_row);
    }
    fclose(f);
}

static void tui_render(void) {
    if (!tui_screen_ready) {
        return;
    }
    if (!tui_headless && tui_entered && term_take_resized()) {
        int columns, rows;
        if (term_size(&columns, &rows) == 0) {
            term_screen_resize(&tui_screen, columns, rows);
        }
        tui_dirty = 1;
    }
    if (!tui_dirty) {
        return;
    }
    TuiNode *base = tui_under();
    term_screen_clear(&tui_screen, tui_style(tui_fg, tui_bg, 0));
    tui_screen.cursor_visible = 0;
    if (base) {
        tui_layout(base);
        const char *t = base->title ? base->title : (base->name ? base->name : "");
        TermStyle bar = tui_style(tui_on_sel, tui_sel, tui_sel == TERM_DEFAULT ? TERM_REVERSE : 0);
        tui_fill(0, 0, tui_screen.columns, 1, bar);
        tui_put_clipped(1, 0, tui_screen.columns - 2, t, bar);
        tui_draw(base);
        tuip_furniture_draw(base);
    }
    /* each modal window in the order shown, a box over what is under it */
    for (int i = 0; i < tui_nroots; i++) {
        TuiNode *m = tui_roots[i];
        if (!m->shown || m->kind != 1) {
            continue;
        }
        tui_layout(m);
        tui_box_draw(m->fx - 1, m->fy, m->fw + 2, m->fh + 1, m->title ? m->title : m->name,
                     tui_style(tui_fg, tui_bg, 0), tui_style(tui_fg, tui_bg, TERM_BOLD));
        tui_draw(m);
        tuip_furniture_draw(m);
    }
    tuip_overlay_draw();
    tui_dialogs_draw();
    tui_dump();
    if (tui_entered) {
        TermBuf b = { 0 };
        term_screen_render(&tui_screen, &b);
        term_write(b.p, b.n);
        term_buf_free(&b);
    }
    tui_dirty = 0;
}

/* ---- events -------------------------------------------------------------------- */

/* Every handler connected to an event, in the order they were connected:
 * a slot holds their names one to a line, as Qt runs every slot of a
 * signal. */
static void tui_fire(const char *handlers) {
    if (!handlers || !tui_g_invoke) {
        return;
    }
    for (const char *p = handlers; *p; ) {
        size_t len = strcspn(p, "\n");
        char *one = malloc(len + 1);
        if (!one) {
            abort();
        }
        memcpy(one, p, len);
        one[len] = 0;
        if (len) {
            tui_depth++;
            tui_g_invoke(one);
            tui_depth--;
        }
        free(one);
        p += len;
        if (*p == '\n') {
            p++;
        }
    }
    tui_dirty = 1;
}

static int tui_handler_depth(void) {
    return tui_depth;
}

static int tui_handler_max(void) {
    return 16;
}

static void tui_collect_focusable(TuiNode *n, TuiNode **out, int *count, int max) {
    if (!n->visible || n->furniture) {
        return;
    }
    if (tui_focusable(n) && *count < max) {
        out[(*count)++] = n;
    }
    for (int i = 0; i < n->nkids; i++) {
        /* only the page a `tabs` shows */
        if (tui_is(n, "tabs") && i != n->index) {
            continue;
        }
        tui_collect_focusable(n->kids[i], out, count, max);
    }
}

static void tui_focus_step(int direction) {
    TuiNode *top = tui_top();
    if (!top) {
        return;
    }
    TuiNode *list[512];
    int count = 0;
    tui_collect_focusable(top, list, &count, 512);
    if (!count) {
        tui_focus = NULL;
        return;
    }
    int at = -1;
    for (int i = 0; i < count; i++) {
        if (list[i] == tui_focus) {
            at = i;
        }
    }
    at = at < 0 ? (direction > 0 ? 0 : count - 1) : (at + direction + count) % count;
    tui_focus = list[at];
    if (tui_is(tui_focus, "field")) {
        tui_focus->caret = tui_focus->text ? strlen(tui_focus->text) : 0;
    }
    tuip_reveal(tui_focus);
    tui_dirty = 1;
}

static TuiNode *tui_default_button(TuiNode *n) {
    if (tui_is(n, "button") && n->is_default && n->visible && n->enabled) {
        return n;
    }
    for (int i = 0; i < n->nkids; i++) {
        TuiNode *d = tui_default_button(n->kids[i]);
        if (d) {
            return d;
        }
    }
    return NULL;
}

/* A window closes the one way every close goes (rule 6): the closed handler,
 * then the front's bookkeeping, then the node. */
static void tui_close_root(TuiNode *root) {
    if (!root || !root->shown) {
        return;
    }
    root->shown = 0;
    tui_dialogs_end_for(root);
    char *handler = root->on_closed ? copy_string(root->on_closed) : NULL;
    tui_fire(handler);
    free(handler);
    if (tui_g_closed) {
        tui_g_closed(root);
    }
    for (int i = 0; i < tui_nroots; i++) {
        if (tui_roots[i] == root) {
            memmove(tui_roots + i, tui_roots + i + 1, sizeof(TuiNode *) * (size_t)(tui_nroots - i - 1));
            tui_nroots--;
            break;
        }
    }
    if (tui_focus && tui_within(tui_focus, root)) {
        tui_focus = NULL;
    }
    tuip_overlay_close_all();
    /* NOT FREED UNDER A HANDLER (rule 7): a handler that closed its own
     * window returns into the press that fired it, which still reads its
     * part, so the tree waits for `retire_drain` (run_ui_modal valgrind) */
    if (tui_depth > 0 && tui_nretired < (int)(sizeof tui_retired / sizeof tui_retired[0])) {
        tui_retired[tui_nretired++] = root;
    } else {
        tui_node_free(root);
    }
    tui_dirty = 1;
    if (!tui_top()) {
        tui_render();
        tui_leave_screen();
    }
}

static void tui_press(TuiNode *n) {
    if (!n || !n->enabled || !n->visible) {
        return;
    }
    if (tui_is(n, "check")) {
        n->checked = !n->checked;
        tui_dirty = 1;
        char *p = n->on_pressed ? copy_string(n->on_pressed) : NULL;
        char *c = n->on_changed ? copy_string(n->on_changed) : NULL;
        tui_fire(p);
        tui_fire(c);
        free(p);
        free(c);
    } else if (tui_is(n, "button")) {
        char *p = n->on_pressed ? copy_string(n->on_pressed) : NULL;
        tui_fire(p);
        free(p);
    }
}

static void tui_field_changed(TuiNode *n) {
    tui_dirty = 1;
    char *c = n->on_changed ? copy_string(n->on_changed) : NULL;
    tui_fire(c);
    free(c);
}

static size_t tui_codepoints(const char *s) {
    size_t n = 0;
    for (; s && *s; s++) {
        n += ((unsigned char)*s & 0xC0) != 0x80;
    }
    return n;
}

/* One key on the focused part, or on the window. 1 when it was used. */
static int tui_key_event(const TermEvent *ev) {
    if (tui_dialog_open()) {
        return tui_dialog_key(ev);
    }
    TuiNode *top = tui_top();
    if (!top) {
        return 0;
    }
    if (tui_focus && !tui_within(tui_focus, top)) {
        tui_focus = NULL;
    }
    if (tuip_overlay_open()) {
        return tuip_overlay_key(ev);
    }
    if (tuip_window_key(top, ev)) {
        return 1;
    }
    if (tui_focus && tuip_key(tui_focus, ev)) {
        return 1;
    }
    const char *k = ev->key;
    unsigned m = ev->mods;
    if (strcmp(k, "tab") == 0) {
        tui_focus_step(m & TERM_MOD_SHIFT ? -1 : 1);
        return 1;
    }
    if ((m & TERM_MOD_CTRL) && strcmp(k, "q") == 0) {
        /* Ctrl-Q closes the window, the terminal's window-manager button */
        tui_close_root(top);
        return 1;
    }
    if (top->kind == 1 && strcmp(k, "escape") == 0) {
        /* Esc rejects a modal window, as it does a QDialog */
        char err[200];
        tui_answer_modal(top, 0, err, sizeof err);
        return 1;
    }
    TuiNode *f = tui_focus;
    if (!f) {
        if (strcmp(k, "enter") == 0) {
            TuiNode *d = tui_default_button(top);
            if (d) {
                tui_press(d);
                return 1;
            }
        }
        if (strcmp(k, "down") == 0 || strcmp(k, "right") == 0) {
            tui_focus_step(1);
            return 1;
        }
        return 0;
    }
    if (tui_is(f, "button") || tui_is(f, "check")) {
        /* Space presses what has the focus; Enter presses a button, and on a
         * check it is the form's Enter, the default button, as in a dialog */
        if (strcmp(k, "space") == 0 || (strcmp(k, "enter") == 0 && tui_is(f, "button"))) {
            tui_press(f);
            return 1;
        }
        if (strcmp(k, "enter") == 0) {
            TuiNode *d = tui_default_button(top);
            if (d) {
                tui_press(d);
                return 1;
            }
            return 0;
        }
        if (strcmp(k, "down") == 0 || strcmp(k, "right") == 0) {
            tui_focus_step(1);
            return 1;
        }
        if (strcmp(k, "up") == 0 || strcmp(k, "left") == 0) {
            tui_focus_step(-1);
            return 1;
        }
        return 0;
    }
    /* a field */
    char *t = f->text ? f->text : copy_string("");
    f->text = t;
    size_t len = strlen(t);
    if (f->caret > len) {
        f->caret = len;
    }
    if (strcmp(k, "enter") == 0) {
        if (f->on_entered) {
            char *e = copy_string(f->on_entered);
            tui_fire(e);
            free(e);
            return 1;
        }
        TuiNode *d = tui_default_button(top);
        if (d) {
            tui_press(d);
            return 1;
        }
        return 0;
    }
    if (strcmp(k, "up") == 0) {
        tui_focus_step(-1);
        return 1;
    }
    if (strcmp(k, "down") == 0) {
        tui_focus_step(1);
        return 1;
    }
    if (strcmp(k, "left") == 0 || (strcmp(k, "b") == 0 && (m & TERM_MOD_CTRL))) {
        while (f->caret > 0) {
            f->caret--;
            if (((unsigned char)t[f->caret] & 0xC0) != 0x80) {
                break;
            }
        }
        tui_dirty = 1;
        return 1;
    }
    if (strcmp(k, "right") == 0 || (strcmp(k, "f") == 0 && (m & TERM_MOD_CTRL))) {
        if (f->caret < len) {
            f->caret++;
            while (f->caret < len && ((unsigned char)t[f->caret] & 0xC0) == 0x80) {
                f->caret++;
            }
        }
        tui_dirty = 1;
        return 1;
    }
    if (strcmp(k, "home") == 0 || (strcmp(k, "a") == 0 && (m & TERM_MOD_CTRL))) {
        f->caret = 0;
        tui_dirty = 1;
        return 1;
    }
    if (strcmp(k, "end") == 0 || (strcmp(k, "e") == 0 && (m & TERM_MOD_CTRL))) {
        f->caret = len;
        tui_dirty = 1;
        return 1;
    }
    if (f->read_only || !f->enabled) {
        return 0;
    }
    if (strcmp(k, "backspace") == 0 || strcmp(k, "delete") == 0) {
        size_t from = f->caret, to = f->caret;
        if (strcmp(k, "backspace") == 0) {
            if (from == 0) {
                return 1;
            }
            do {
                from--;
            } while (from > 0 && ((unsigned char)t[from] & 0xC0) == 0x80);
        } else {
            if (to >= len) {
                return 1;
            }
            do {
                to++;
            } while (to < len && ((unsigned char)t[to] & 0xC0) == 0x80);
        }
        memmove(t + from, t + to, len - to + 1);
        f->caret = from;
        tui_field_changed(f);
        return 1;
    }
    if (strcmp(k, "u") == 0 && (m & TERM_MOD_CTRL)) {
        t[0] = 0;
        f->caret = 0;
        tui_field_changed(f);
        return 1;
    }
    if (ev->text[0] && !(m & (TERM_MOD_CTRL | TERM_MOD_ALT))) {
        if (f->max_length > 0 && (long)tui_codepoints(t) >= f->max_length) {
            return 1;
        }
        size_t add = strlen(ev->text);
        char *grown = realloc(t, len + add + 1);
        if (!grown) {
            abort();
        }
        memmove(grown + f->caret + add, grown + f->caret, len - f->caret + 1);
        memcpy(grown + f->caret, ev->text, add);
        f->text = grown;
        f->caret += add;
        tui_field_changed(f);
        return 1;
    }
    return 0;
}

/* A paste goes into the focused field, its line breaks dropped (a field
 * holds one line). */
static void tui_paste(const TermEvent *ev) {
    TuiNode *f = tui_focus;
    if (!tui_dialog_open() && (!f || !tui_is(f, "field") || f->read_only || !f->enabled)) {
        return;
    }
    for (size_t i = 0; i < ev->data_len;) {
        size_t n = 1;
        while (i + n < ev->data_len && ((unsigned char)ev->data[i + n] & 0xC0) == 0x80) {
            n++;
        }
        if (ev->data[i] != '\r' && ev->data[i] != '\n' && n < sizeof(((TermEvent *)0)->text)) {
            TermEvent one = { 0 };
            one.kind = TERM_EV_KEY;
            memcpy(one.text, ev->data + i, n);
            snprintf(one.key, sizeof one.key, "%s", one.text);
            tui_key_event(&one);
        }
        i += n;
    }
}

static TuiNode *tui_hit(TuiNode *n, int x, int y) {
    if (!n->visible || n->furniture || x < n->x || y < n->y || x >= n->x + n->w || y >= n->y + n->h) {
        return NULL;
    }
    for (int i = n->nkids - 1; i >= 0; i--) {
        if (tui_is(n, "tabs") && i != n->index) {
            continue;
        }
        TuiNode *h = tui_hit(n->kids[i], x, y);
        if (h) {
            return h;
        }
    }
    return n;
}

static void tui_mouse_event(const TermEvent *ev) {
    if (tui_dialog_open()) {
        tui_dialog_mouse(ev);
        return;
    }
    TuiNode *top = tui_top();
    if (!top) {
        return;
    }
    if (tuip_overlay_open()) {
        tuip_overlay_mouse(ev);
        return;
    }
    if (tuip_window_mouse(top, ev)) {
        return;
    }
    TuiNode *h = tui_hit(top, ev->column - 1, ev->row - 1);
    if (h && tuip_mouse(h, ev)) {
        return;
    }
    if (strcmp(ev->action, "press") != 0 || !h) {
        return;
    }
    if (ev->button == 3) {
        for (TuiNode *p = h; p; p = p->parent) {
            if (p->on_context) {
                char *c = copy_string(p->on_context);
                tui_fire(c);
                free(c);
                return;
            }
        }
        return;
    }
    if (ev->button != 1) {
        return;
    }
    if (tui_focusable(h)) {
        tui_focus = h;
        tui_dirty = 1;
        if (tui_is(h, "field")) {
            /* the caret to the clicked cell, or the end */
            int x0 = h->x + (tui_has_caption(h) ? tui_text_cells(h->label) + 1 : 0);
            int want = ev->column - 1 - x0 + h->scroll;
            const char *t = h->text ? h->text : "";
            size_t i = 0;
            int cells = 0;
            while (t[i] && cells < want) {
                size_t k = 1;
                while (t[i + k] && ((unsigned char)t[i + k] & 0xC0) == 0x80) {
                    k++;
                }
                long cw = h->secret ? 1 : term_text_width(t + i, k, NULL);
                cells += cw < 0 ? 1 : (int)cw;
                i += k;
            }
            h->caret = i;
        } else {
            tui_press(h);
        }
    }
}

static void tui_dispatch(const TermEvent *ev) {
    if (ev->kind == TERM_EV_KEY) {
        tui_key_event(ev);
    } else if (ev->kind == TERM_EV_MOUSE) {
        tui_mouse_event(ev);
    } else {
        tui_paste(ev);
    }
}

/* ---- the loop -------------------------------------------------------------------- */

static int tui_in_tick = 0;     /* gBASIC's tick is on the stack */

/* Whether the loop's turning is over: `ui.run`'s when no window is left; a
 * dialog's or a modal window's when it has its answer; every one when it is
 * stopped, as Qt's `exit()` ends each nested loop and the box with it. */
static int tui_loop_over(const int *done) {
    return tui_stop_asked || (done ? *done : !tui_top());
}

/* THE LOOP, ONE COPY. `ui.run` turns it, and a dialog or a modal window turns
 * it again inside, until `*done`, which is rule 2: gBASIC's sources are
 * served while a box is up, as Qt's pump serves them inside `exec()`.
 *
 * A STOP ENDS EVERY TURNING, the innermost first: `ui.stop()`, or a tick
 * answering 0 (gBASIC's loop is over: a raise in a watcher body stopped the
 * program). Measured: with a nested loop deaf to it, a fixture whose watcher
 * raised sat with its box up for ever. A box ended so is a cancel, `*done`
 * still 0; the terminal going away is one too. */
static void tui_loop(int *done, int tick_ms) {
    int stopped_outside = tui_stop_asked;
    if (done) {
        tui_stop_asked = 0;     /* a stop already acted on ends nothing new */
    }
    double next_tick = tui_now_ms() + tick_ms;
    while (!tui_loop_over(done)) {
        tui_render();
        double now = tui_now_ms();
        long wait = (long)(next_tick - now);
        if (wait < 0) {
            wait = 0;
        }
        if (tui_entered) {
            if (term_decoder_pending(&tui_decoder)) {
                long esc = (long)(tui_pending_since + 30 - now) + 1;
                if (esc < wait) {
                    wait = esc < 0 ? 0 : esc;
                }
            }
            struct pollfd pfd = { 0, POLLIN, 0 };
            int ready = poll(&pfd, 1, (int)wait);
            if (ready > 0) {
                unsigned char buf[4096];
                ssize_t got = read(0, buf, sizeof buf);
                if (got > 0) {
                    term_decoder_feed(&tui_decoder, buf, (size_t)got);
                    tui_pending_since = tui_now_ms();
                } else if (got == 0) {
                    break;      /* the terminal went away */
                }
            }
            int flush = term_decoder_pending(&tui_decoder) &&
                        tui_now_ms() - tui_pending_since >= 30;
            TermEvent ev;
            while (!tui_loop_over(done) && term_decoder_next(&tui_decoder, flush, &ev)) {
                tui_dispatch(&ev);
                term_event_free(&ev);
                tui_render();
            }
            if (term_take_resized()) {
                int columns, rows;
                if (term_size(&columns, &rows) == 0) {
                    term_screen_resize(&tui_screen, columns, rows);
                }
                tui_dirty = 1;
            }
        } else {
            poll(NULL, 0, (int)wait);
        }
        /* NOT RE-ENTERED: a dialog raised from inside a tick (a watcher body)
         * turns this loop with that tick still on the stack, and running
         * gBASIC's loop inside its own iteration is a path nothing holds.
         * Qt's timers are not re-entered either, so a box raised there stalls
         * every source on both backends alike (run_ui_dialog `modal
         * extent`, include/ui_backend.h rule 2). */
        if (tui_now_ms() >= next_tick && !tui_in_tick) {
            next_tick = tui_now_ms() + tick_ms;
            tui_in_tick = 1;
            int more = tui_g_tick ? tui_g_tick() : 1;
            tui_in_tick = 0;
            if (!more) {
                tui_stop_asked = 1;
            }
        }
    }
    if (done && stopped_outside) {
        tui_stop_asked = 1;
    }
}

static int tui_run(int tick_ms) {
    if (tick_ms < 1) {
        tick_ms = 50;
    }
    tui_stop_asked = 0;
    tui_running = 1;
    /* back on the terminal if a run before this one left it */
    if (tui_top()) {
        char err[200];
        if (!tui_enter_screen(err, sizeof err)) {
            tui_running = 0;
            return 1;
        }
        tui_dirty = 1;
    }
    tui_loop(NULL, tick_ms);
    tui_running = 0;
    tui_render();
    /* WHEN THE RUN ENDS, THE ORDINARY SCREEN COMES BACK, and the windows
     * stay open and readable (rule 5): what a program prints after `ui.run`
     * is its result, and on the alternate screen it would vanish. The next
     * `ui.run` draws them again. */
    tui_leave_screen();
    return 0;
}

static void tui_stop(void) {
    tui_stop_asked = 1;
}

static void tui_retire_drain(void) {
    if (tui_depth > 0) {
        return;
    }
    for (int i = 0; i < tui_nretired; i++) {
        tui_node_free(tui_retired[i]);
    }
    tui_nretired = 0;
}

/* ---- building --------------------------------------------------------------------- */

static void *tui_window(const char *name, const char *title, int width, int height, int kind) {
    (void)width;
    (void)height;   /* the window is the screen (§8.1) */
    TuiNode *root = tui_node_new("window", name, NULL);
    root->title = tui_dup(title);
    root->kind = kind;
    return root;
}

static void *tui_container(void *parent, const char *word, const char *name, const char *title,
                           char *err, size_t err_size) {
    TuiNode *p = parent;
    err[0] = 0;
    void *made = tuip_container(p, word, name, title, err, err_size);
    if (made || err[0]) {
        return made;
    }
    if (!(strcmp(word, "row") == 0 || strcmp(word, "column") == 0 || strcmp(word, "form") == 0 ||
          strcmp(word, "group") == 0)) {
        tui_not_yet(word, err, err_size);
        return NULL;
    }
    if (!tui_box(p)) {
        TUI_ERR("that container cannot hold anything");
        return NULL;
    }
    TuiNode *n = tui_node_new(word, name, NULL);
    n->title = tui_dup(title);
    tui_add_kid(p, n, -1);
    return n;
}

static void *tui_part(void *parent, const char *word, const char *name, const char *label,
                      char *err, size_t err_size) {
    TuiNode *p = parent;
    if (!(strcmp(word, "field") == 0 || strcmp(word, "button") == 0 || strcmp(word, "check") == 0 ||
          strcmp(word, "text") == 0 || strcmp(word, "stretch") == 0 || strcmp(word, "separator") == 0 ||
          strcmp(word, "choice") == 0)) {
        tui_not_yet(word, err, err_size);
        return NULL;
    }
    if (!tui_box(p)) {
        TUI_ERR("that container cannot hold anything");
        return NULL;
    }
    if ((strcmp(word, "stretch") == 0 || strcmp(word, "separator") == 0) &&
        !(tui_is(p, "row") || tui_is(p, "column") || tui_is(p, "group") || tui_is(p, "window") ||
          tui_is(p, "tab") || tui_is(p, "status"))) {
        TUI_ERR("`%s` needs a `row` or a `column` around it", word);
        return NULL;
    }
    TuiNode *n = tui_node_new(word, name, label);
    if (strcmp(word, "button") == 0 || strcmp(word, "check") == 0 || strcmp(word, "text") == 0) {
        /* no label: the name, so a part with only an icon still says what it
         * is here, where an icon draws nothing (§8.1, Q4) */
        n->text = tui_dup(label ? label : (strcmp(word, "text") != 0 && name ? name : ""));
    }
    tui_add_kid(p, n, -1);
    if (!tuip_part(p, word, n, err, err_size)) {
        tui_unlink(n);
        tui_node_free(n);
        return NULL;
    }
    return n;
}

/* ---- properties ---------------------------------------------------------------- */

static int tui_say_no(const TuiNode *n, const char *word, char *err, size_t err_size) {
    return TUI_ERR("a `%s` has no `%s`", tui_is(n, "window") ? "window" : n->word, word);
}

static int tui_set_text(void *widget, const char *prop, const char *value, char *err, size_t err_size) {
    TuiNode *n = widget;
    tui_dirty = 1;
    int own = tuip_set_text(n, prop, value, err, err_size);
    if (own != -1) {
        return own;
    }
    if (strcmp(prop, "text") == 0 && !tui_is(n, "window") && !tui_box(n)) {
        tui_set(&n->text, value);
        if (tui_is(n, "field")) {
            n->caret = strlen(value);
            n->scroll = 0;
        }
        return 1;
    }
    if (strcmp(prop, "title") == 0 && (tui_is(n, "window") || tui_is(n, "group"))) {
        tui_set(&n->title, value);
        return 1;
    }
    if (strcmp(prop, "hint") == 0 && tui_is(n, "field")) {
        tui_set(&n->hint, value);
        return 1;
    }
    if (strcmp(prop, "tip") == 0) {
        tui_set(&n->tip, value);
        return 1;
    }
    if (strcmp(prop, "icon") == 0) {
        return 1;   /* an icon file draws as nothing here; its label shows (§8.1, Q4) */
    }
    if (strcmp(prop, "emphasis") == 0) {
        if (strcmp(value, "none") && strcmp(value, "outline") && strcmp(value, "strong")) {
            return TUI_ERR("`emphasis:` is none, outline or strong");
        }
        n->bold = strcmp(value, "strong") == 0;
        snprintf(n->emphasis, sizeof n->emphasis, "%s", value);
        return 1;
    }
    return tui_say_no(n, prop, err, err_size);
}

static int tui_set_number(void *widget, const char *prop, double value, char *err, size_t err_size) {
    TuiNode *n = widget;
    tui_dirty = 1;
    int own = tuip_set_number(n, prop, value, err, err_size);
    if (own != -1) {
        return own;
    }
    if (strcmp(prop, "max_length") == 0 && tui_is(n, "field")) {
        n->max_length = (int)value;
        return 1;
    }
    if (strcmp(prop, "font_size") == 0) {
        n->font_size = (int)value;    /* the terminal's font is the user's (§8.1) */
        return 1;
    }
    int *slot = NULL, across = 0;
    if (strcmp(prop, "width") == 0) { slot = &n->want_w; across = 1; }
    else if (strcmp(prop, "height") == 0) { slot = &n->want_h; }
    else if (strcmp(prop, "min_width") == 0) { slot = &n->min_w; across = 1; }
    else if (strcmp(prop, "min_height") == 0) { slot = &n->min_h; }
    else if (strcmp(prop, "max_width") == 0) { slot = &n->max_w; across = 1; }
    else if (strcmp(prop, "max_height") == 0) { slot = &n->max_h; }
    else if (strcmp(prop, "x") == 0 || strcmp(prop, "y") == 0) {
        return 1;   /* a window is the screen; there is nowhere to move it */
    }
    if (slot) {
        *slot = across ? tui_cells_across(value) : tui_cells_down(value);
        return 1;
    }
    return tui_say_no(n, prop, err, err_size);
}

static int tui_set_bool(void *widget, const char *prop, int value, char *err, size_t err_size) {
    TuiNode *n = widget;
    tui_dirty = 1;
    int own = tuip_set_bool(n, prop, value, err, err_size);
    if (own != -1) {
        return own;
    }
    if (strcmp(prop, "visible") == 0) { n->visible = value; return 1; }
    if (strcmp(prop, "enabled") == 0) { n->enabled = value; return 1; }
    if (strcmp(prop, "bold") == 0) { n->bold = value; return 1; }
    if (strcmp(prop, "checked") == 0 && tui_is(n, "check")) { n->checked = value; return 1; }
    if (strcmp(prop, "secret") == 0 && tui_is(n, "field")) { n->secret = value; return 1; }
    if (strcmp(prop, "read_only") == 0 && tui_is(n, "field")) { n->read_only = value; return 1; }
    if (strcmp(prop, "default") == 0 && tui_is(n, "button")) { n->is_default = value; return 1; }
    if (strcmp(prop, "wrap") == 0 && tui_is(n, "text")) { n->wrap = value; return 1; }
    if (strcmp(prop, "focused") == 0) {
        if (value && tui_focusable(n)) {
            tui_focus = n;
        } else if (!value && tui_focus == n) {
            tui_focus = NULL;
        }
        return 1;
    }
    return tui_say_no(n, prop, err, err_size);
}

static int tui_set_text_list(void *widget, const char *prop, const char *const *values, size_t count,
                             char *err, size_t err_size) {
    if (strcmp(prop, "shortcut") == 0) {
        /* several shortcuts: the first is what an item shows */
        TuiNode *n = widget;
        if (count > 0) {
            tui_set(&n->shortcut, values[0]);
        }
        return 1;
    }
    return tui_say_no(widget, prop, err, err_size);
}

static int tui_layout_number(void *container, const char *prop, long value, char *err, size_t err_size) {
    TuiNode *n = container;
    tui_dirty = 1;
    /* pixels in Qt; here a margin or a spacing of a few pixels is no cell,
     * so it rounds DOWN, and 0 stays 0 */
    int cells = strcmp(prop, "margin") == 0 ? (int)(value / 8) : (int)(value / 8);
    if (strcmp(prop, "margin") == 0) {
        n->margin = cells;
        return 1;
    }
    if (strcmp(prop, "spacing") == 0) {
        n->spacing = tui_is(n, "row") ? cells : (int)(value / 16);
        return 1;
    }
    return tui_say_no(n, prop, err, err_size);
}

static int tui_grow(void *widget, long weight, char *err, size_t err_size) {
    (void)err;
    (void)err_size;
    TuiNode *n = widget;
    n->grow = weight;
    n->grow_set = 1;
    tui_dirty = 1;
    return 1;
}

static TuiNode *tui_named(void *window, const char *name, char *err, size_t err_size) {
    TuiNode *n = tui_find(window, name);
    if (!n && err) {
        snprintf(err, err_size, "this window has no `%s`", name ? name : "");
    }
    return n;
}

static int tui_set_part_text(void *window, const char *name, const char *prop, const char *value,
                             char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    return n ? tui_set_text(n, prop, value, err, err_size) : 0;
}

static int tui_set_part_number(void *window, const char *name, const char *prop, double value,
                               char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    return n ? tui_set_number(n, prop, value, err, err_size) : 0;
}

static int tui_set_part_bool(void *window, const char *name, const char *prop, int value,
                             char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    return n ? tui_set_bool(n, prop, value, err, err_size) : 0;
}

static int tui_set_part_text_list(void *window, const char *name, const char *prop,
                                  const char *const *values, size_t count, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    return n ? tui_set_text_list(n, prop, values, count, err, err_size) : 0;
}

static char *tui_read(void *window, const char *name) {
    TuiNode *n = tui_find(window, name);
    if (!n) {
        return NULL;
    }
    if (tui_is(n, "check")) {
        return copy_string(n->checked ? "true" : "false");
    }
    if (tuip_is_grid(n) || tui_is(n, "tabs")) {
        return NULL;    /* read by row, column or index, not as one text */
    }
    if (tui_is(n, "choice")) {
        return copy_string(n->index >= 0 && n->index < n->nitems ? n->items[n->index] : "");
    }
    if (tui_is(n, "window")) {
        return copy_string(n->title ? n->title : "");
    }
    return copy_string(n->text ? n->text : "");
}

static int tui_write(void *window, const char *name, const char *text, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    tui_dirty = 1;
    if (tui_is(n, "check")) {
        n->checked = strcmp(text, "true") == 0 || strcmp(text, "1") == 0;
        return 1;
    }
    if (tui_is(n, "choice")) {
        return tuip_set_text(n, "text", text, err, err_size);
    }
    if (tui_box(n) || tuip_is_grid(n)) {
        return tui_say_no(n, "text", err, err_size);
    }
    tui_set(&n->text, text);
    if (tui_is(n, "field")) {
        n->caret = strlen(text);
        n->scroll = 0;
    }
    return 1;
}

static int tui_get(void *window, const char *name, const char *prop, char *kind, double *num,
                   char **text, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    *kind = 't';
    *text = NULL;
    /* a size or a place is the layout's, so lay out first */
    if (tui_dirty && (strcmp(prop, "width") == 0 || strcmp(prop, "height") == 0 ||
                      strcmp(prop, "x") == 0 || strcmp(prop, "y") == 0)) {
        tui_render();
    }
    if (tuip_get(n, prop, kind, num, text) == 1) {
        return 1;
    }
#define TUI_B(v) do { *kind = 'b'; *num = (v); return 1; } while (0)
#define TUI_N(v) do { *kind = 'n'; *num = (v); return 1; } while (0)
#define TUI_T(v) do { *kind = 't'; *text = copy_string((v) ? (v) : ""); return 1; } while (0)
    if (strcmp(prop, "visible") == 0) TUI_B(n->visible);
    if (strcmp(prop, "enabled") == 0) TUI_B(n->enabled);
    if (strcmp(prop, "bold") == 0) TUI_B(n->bold);
    if (strcmp(prop, "focused") == 0) TUI_B(tui_focus == n);
    if (strcmp(prop, "font_size") == 0) TUI_N(n->font_size);
    if (strcmp(prop, "tip") == 0) TUI_T(n->tip);
    if (strcmp(prop, "emphasis") == 0) TUI_T(n->emphasis[0] ? n->emphasis : "none");
    /* a column is divided by a line running across, a row by one running down */
    if (tui_is(n, "separator") && strcmp(prop, "direction") == 0)
        TUI_T(n->parent && tui_is(n->parent, "row") ? "down" : "across");
    /* a field's size is its box's, without the caption beside it, as Qt's
     * line edit is measured without its label */
    if (strcmp(prop, "width") == 0) TUI_N((n->w - (tui_has_caption(n) ? tui_text_cells(n->label) + 1 : 0)) * 8);
    if (strcmp(prop, "height") == 0) TUI_N(n->h * 16);
    if (strcmp(prop, "x") == 0) TUI_N(n->x * 8);
    if (strcmp(prop, "y") == 0) TUI_N(n->y * 16);
    if (strcmp(prop, "min_width") == 0) TUI_N(n->min_w * 8);
    if (strcmp(prop, "min_height") == 0) TUI_N(n->min_h * 16);
    if (strcmp(prop, "max_width") == 0) TUI_N(n->max_w * 8);
    if (strcmp(prop, "max_height") == 0) TUI_N(n->max_h * 16);
    if (tui_is(n, "window") || tui_is(n, "group")) {
        if (strcmp(prop, "title") == 0) TUI_T(n->title);
    }
    if (tui_is(n, "check")) {
        if (strcmp(prop, "checked") == 0) TUI_B(n->checked);
        if (strcmp(prop, "text") == 0) TUI_T(n->text);
    }
    if (tui_is(n, "button")) {
        if (strcmp(prop, "text") == 0) TUI_T(n->text);
        if (strcmp(prop, "default") == 0) TUI_B(n->is_default);
    }
    if (tui_is(n, "text")) {
        if (strcmp(prop, "text") == 0) TUI_T(n->text);
        if (strcmp(prop, "wrap") == 0) TUI_B(n->wrap);
    }
    if (tui_is(n, "field")) {
        if (strcmp(prop, "text") == 0) TUI_T(n->text);
        if (strcmp(prop, "hint") == 0) TUI_T(n->hint);
        if (strcmp(prop, "secret") == 0) TUI_B(n->secret);
        if (strcmp(prop, "read_only") == 0) TUI_B(n->read_only);
        if (strcmp(prop, "max_length") == 0) TUI_N(n->max_length);
        if (strcmp(prop, "shown_text") == 0) {
            char *s = tui_field_shown(n);
            *kind = 't';
            *text = s;
            return 1;
        }
    }
#undef TUI_B
#undef TUI_N
#undef TUI_T
    return tui_say_no(n, prop, err, err_size);
}

static int tui_set_display_only(void *window, const char *name, int on, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    n->display_only = on;
    if (tui_is(n, "field")) {
        n->read_only = on;
    } else if (tui_is(n, "check") || tui_is(n, "choice")) {
        n->enabled = !on;
    }
    tui_dirty = 1;
    return 1;
}

/* ---- signals ---------------------------------------------------------------------- */

static int tui_connect(void *window, const char *target, const char *signal, const char *handler,
                       char *err, size_t err_size) {
    TuiNode *n = tui_named(window, target, err, err_size);
    if (!n) {
        return 0;
    }
    char **slot = NULL;
    if (strcmp(signal, "closed") == 0 && tui_is(n, "window")) {
        slot = &n->on_closed;
    } else if (strcmp(signal, "pressed") == 0 && (tui_is(n, "button") || tui_is(n, "check"))) {
        slot = &n->on_pressed;
    } else if (strcmp(signal, "changed") == 0 && (tui_is(n, "field") || tui_is(n, "check"))) {
        slot = &n->on_changed;
    } else if (strcmp(signal, "entered") == 0 && tui_is(n, "field")) {
        slot = &n->on_entered;
    } else if (strcmp(signal, "context") == 0) {
        slot = &n->on_context;
    } else {
        slot = tuip_event_slot(n, signal);
    }
    if (!slot) {
        return TUI_ERR("a `%s` has no `%s` event", tui_is(n, "window") ? "window" : n->word, signal);
    }
    /* another handler for the same event joins the ones there */
    if (*slot && **slot) {
        size_t len = strlen(*slot) + strlen(handler) + 2;
        char *both = malloc(len);
        if (!both) {
            abort();
        }
        snprintf(both, len, "%s\n%s", *slot, handler);
        free(*slot);
        *slot = both;
    } else {
        tui_set(slot, handler);
    }
    return 1;
}

static int tui_invoke(void *window, const char *target, const char *method, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, target, err, err_size);
    if (!n) {
        return 0;
    }
    const char *w = tui_is(n, "window") ? "window" : n->word;
    if (strcmp(method, "close") == 0) {
        if (!tui_is(n, "window")) {
            return TUI_ERR("a `%s` has no `close` to invoke", w);
        }
        tui_close_root(n);
        return 1;
    }
    if (strcmp(method, "press") == 0 && tui_is(n, "item")) {
        tuip_item_press(n);
        return 1;
    }
    if (strcmp(method, "clear") == 0 && tuip_is_grid(n) && !n->is_virtual) {
        for (long i = 0; i < (long)n->nrows * n->ncols; i++) {
            tui_set(&n->cells[i], "");
        }
        tui_dirty = 1;
        return 1;
    }
    if (strcmp(method, "fit_columns") == 0 && tuip_is_grid(n)) {
        return 1;   /* columns are fitted at every frame here */
    }
    if (strcmp(method, "press") == 0) {
        if (!(tui_is(n, "button") || tui_is(n, "check"))) {
            return TUI_ERR("a `%s` has no `press` to invoke", w);
        }
        tui_press(n);
        return 1;
    }
    if (strcmp(method, "focus") == 0) {
        if (!tui_focusable(n)) {
            return TUI_ERR("a `%s` has no `focus` to invoke", w);
        }
        tui_focus = n;
        tui_dirty = 1;
        return 1;
    }
    if (strcmp(method, "clear") == 0 && tui_is(n, "field")) {
        tui_set(&n->text, "");
        n->caret = 0;
        tui_field_changed(n);
        return 1;
    }
    if (strcmp(method, "hide") == 0) {
        n->visible = 0;
        tui_dirty = 1;
        return 1;
    }
    if (strcmp(method, "raise") == 0) {
        return 1;
    }
    if (strcmp(method, "select_all") == 0 || strcmp(method, "undo") == 0 || strcmp(method, "redo") == 0) {
        return tui_not_yet(method, err, err_size);
    }
    return TUI_ERR("a `%s` has no `%s` to invoke", w, method);
}

/* "Ctrl+S", "Shift+Tab", "Enter", "a": one combination, as ui.key takes it. */
static int tui_parse_keys(const char *keys, TermEvent *ev, char *err, size_t err_size) {
    memset(ev, 0, sizeof *ev);
    ev->kind = TERM_EV_KEY;
    char buf[64];
    snprintf(buf, sizeof buf, "%s", keys);
    char *part = buf, *plus;
    while ((plus = strchr(part, '+')) && plus[1]) {
        *plus = 0;
        if (strcasecmp(part, "ctrl") == 0) ev->mods |= TERM_MOD_CTRL;
        else if (strcasecmp(part, "shift") == 0) ev->mods |= TERM_MOD_SHIFT;
        else if (strcasecmp(part, "alt") == 0) ev->mods |= TERM_MOD_ALT;
        else return TUI_ERR("`%s` is not a modifier (Ctrl, Shift or Alt)", part);
        part = plus + 1;
    }
    static const char *const names[] = { "enter", "return", "tab", "backspace", "escape", "esc",
        "space", "up", "down", "left", "right", "home", "end", "page_up", "pageup", "page_down",
        "pagedown", "insert", "delete", "del", NULL };
    for (size_t i = 0; names[i]; i++) {
        if (strcasecmp(part, names[i]) == 0) {
            const char *k = names[i];
            if (strcmp(k, "return") == 0) k = "enter";
            else if (strcmp(k, "esc") == 0) k = "escape";
            else if (strcmp(k, "pageup") == 0) k = "page_up";
            else if (strcmp(k, "pagedown") == 0) k = "page_down";
            else if (strcmp(k, "del") == 0) k = "delete";
            snprintf(ev->key, sizeof ev->key, "%s", k);
            if (strcmp(k, "space") == 0 && !ev->mods) {
                snprintf(ev->text, sizeof ev->text, " ");
            }
            return 1;
        }
    }
    if ((part[0] == 'f' || part[0] == 'F') && part[1] >= '1' && part[1] <= '9') {
        snprintf(ev->key, sizeof ev->key, "f%s", part + 1);
        return 1;
    }
    size_t len = strlen(part);
    if (len == 0 || len >= sizeof ev->text || tui_codepoints(part) != 1) {
        return TUI_ERR("`%s` is not one key: give one combination, as \"Ctrl+S\", \"Tab\" or \"a\"", keys);
    }
    if (ev->mods & TERM_MOD_CTRL) {
        snprintf(ev->key, sizeof ev->key, "%c", (char)tolower((unsigned char)part[0]));
    } else {
        snprintf(ev->key, sizeof ev->key, "%s", part);
        if (!(ev->mods & TERM_MOD_ALT)) {
            snprintf(ev->text, sizeof ev->text, "%s", part);
        }
    }
    return 1;
}

static int tui_key(void *window, const char *name, const char *keys, int *handled, char *err,
                   size_t err_size) {
    TuiNode *n = name && *name ? tui_named(window, name, err, err_size) : window;
    if (!n) {
        return 0;
    }
    TermEvent ev;
    if (!tui_parse_keys(keys, &ev, err, err_size)) {
        return 0;
    }
    if (tui_focusable(n)) {
        tui_focus = n;
    }
    *handled = tui_key_event(&ev);
    tui_render();
    return 1;
}

static char *tui_shortcuts(void *window, const char *name, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    return n ? copy_string("") : NULL;
}

static int tui_click(void *window, const char *name, int row, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    if (row >= 0) {
        int done = tuip_click_row(n, row, 0, err, err_size);
        tui_render();
        return done;
    }
    if (tui_is(n, "item")) {
        return TUI_ERR("`%s` is an `item`, which has nothing to click", name);
    }
    tui_render();
    TermEvent ev = { 0 };
    ev.kind = TERM_EV_MOUSE;
    ev.button = 1;
    snprintf(ev.action, sizeof ev.action, "press");
    ev.column = n->x + 1 + (tui_has_caption(n) ? tui_text_cells(n->label) + 1 : 0);
    ev.row = n->y + 1;
    tui_mouse_event(&ev);
    tui_render();
    return 1;
}

static int tui_right_click(void *window, const char *name, int row, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    if (row >= 0) {
        int done = tuip_click_row(n, row, 1, err, err_size);
        tui_render();
        return done;
    }
    if (tui_is(n, "item")) {
        return TUI_ERR("`%s` is an `item`, which has nothing to click", name);
    }
    for (TuiNode *p = n; p; p = p->parent) {
        if (p->on_context) {
            char *c = copy_string(p->on_context);
            tui_fire(c);
            free(c);
            break;
        }
    }
    return 1;
}

static int tui_label(void *window, const char *name, char **text, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return -1;
    }
    if (!n->label) {
        return 0;
    }
    *text = copy_string(n->label);
    return 1;
}

/* ---- showing and closing -------------------------------------------------------- */

static void tui_show(void *window) {
    TuiNode *root = window;
    char err[200];
    if (!tui_enter_screen(err, sizeof err)) {
        return;     /* begin already asked; a terminal lost meanwhile draws nothing */
    }
    if (tui_nroots < (int)(sizeof tui_roots / sizeof tui_roots[0])) {
        tui_roots[tui_nroots++] = root;
    }
    root->shown = 1;
    tui_focus = NULL;
    tui_focus_step(1);
    /* drawn at the run's first turn, or by the next ui.key or ui.click, not
     * here: an asking table is asked for what it shows while the loop runs,
     * as Qt paints only in its loop (run_ui_ask holds that) */
    tui_dirty = 1;
}

static int tui_close(void *window, char *err, size_t err_size) {
    (void)err;
    (void)err_size;
    tui_close_root(window);
    return 1;
}

/* ---- the theme --------------------------------------------------------------------- */

static int tui_theme_set(const char *css, char *err, size_t err_size) {
    if (css && *css) {
        return TUI_ERR("a stylesheet is Qt's; the tui backend takes the theme record");
    }
    tui_bg = tui_fg = tui_base = tui_ctl = tui_hi = tui_faint = tui_line = tui_sel = tui_on_sel = TERM_DEFAULT;
    tui_dirty = 1;
    return 1;
}

static int tui_colour(const char *v, uint32_t *out) {
    if (v && v[0] == '#' && strlen(v) == 7 && strspn(v + 1, "0123456789abcdefABCDEF") == 6) {
        *out = (uint32_t)strtoul(v + 1, NULL, 16);
        return 1;
    }
    return 0;
}

static int tui_theme_record(const char *const *names, const char *const *values, size_t n, char *err,
                            size_t err_size) {
    for (size_t i = 0; i < n; i++) {
        uint32_t c;
        uint32_t *slot = NULL;
        if (strcmp(names[i], "bg") == 0) slot = &tui_bg;
        else if (strcmp(names[i], "fg") == 0) slot = &tui_fg;
        else if (strcmp(names[i], "base") == 0) slot = &tui_base;
        else if (strcmp(names[i], "ctl") == 0) slot = &tui_ctl;
        else if (strcmp(names[i], "hi") == 0) slot = &tui_hi;
        else if (strcmp(names[i], "faint") == 0) slot = &tui_faint;
        else if (strcmp(names[i], "line") == 0) slot = &tui_line;
        else if (strcmp(names[i], "sel") == 0) slot = &tui_sel;
        else if (strcmp(names[i], "on_sel") == 0) slot = &tui_on_sel;
        else continue;      /* icon-file fields draw nothing here (§3.3) */
        if (!tui_colour(values[i], &c)) {
            return TUI_ERR("`%s:` must be a colour as \"#rrggbb\"", names[i]);
        }
        *slot = c;
    }
    tui_dirty = 1;
    return 1;
}

static char *tui_theme_get(void) {
    return copy_string("");
}

static int tui_theme_known(const char *name) {
    (void)name;
    return 0;
}

static char *tui_theme_names(void) {
    return copy_string("");
}

static int tui_theme_plain(void *window, const char *name, const char *theme, char *err, size_t err_size) {
    (void)window; (void)name; (void)theme;
    return tui_not_yet("source", err, err_size);
}

/* ---- the children of a box (ui.refresh, ui.children) ---------------------------- */

static void *tui_container_node(void *window, const char *name, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return NULL;
    }
    if (!tui_box(n)) {
        TUI_ERR("a `%s` holds no parts", n->word);
        return NULL;
    }
    return n;
}

static const char *tui_node_word(void *node) {
    return ((TuiNode *)node)->word;
}

static void *tui_child_named(void *container, const char *name) {
    TuiNode *c = container;
    for (int i = 0; i < c->nkids; i++) {
        if (c->kids[i]->name && name && strcmp(c->kids[i]->name, name) == 0) {
            return c->kids[i];
        }
    }
    return NULL;
}

static int tui_child_count(void *container) {
    return ((TuiNode *)container)->nkids;
}

static void *tui_child_at(void *container, int index) {
    TuiNode *c = container;
    return index >= 0 && index < c->nkids ? c->kids[index] : NULL;
}

static int tui_child_place(void *container, void *child, int position, char *err, size_t err_size) {
    TuiNode *c = container, *k = child;
    if (k->parent != c) {
        return TUI_ERR("that part is not in this container");
    }
    tui_unlink(k);
    tui_add_kid(c, k, position);
    tui_dirty = 1;
    return 1;
}

static int tui_child_caption(void *container, void *child, const char *text, char *err, size_t err_size) {
    (void)container;
    (void)err;
    (void)err_size;
    TuiNode *k = child;
    if (tui_is(k, "tab")) {
        tui_set(&k->title, text);
        tui_dirty = 1;
        return 1;
    }
    tui_set(&k->label, text);
    if (tui_is(k, "button") || tui_is(k, "check") || tui_is(k, "text")) {
        tui_set(&k->text, text);
    }
    tui_dirty = 1;
    return 1;
}

static int tui_child_drop(void *container, void *child, char *err, size_t err_size) {
    TuiNode *c = container, *k = child;
    if (k->parent != c) {
        return TUI_ERR("that part is not in this container");
    }
    tui_unlink(k);
    tui_node_free(k);
    tui_dirty = 1;
    return 1;
}

static char *tui_child_name(void *container, int index) {
    TuiNode *k = tui_child_at(container, index);
    return copy_string(k && k->name ? k->name : "");
}

/* ---- what increment 3 does not draw: refused by name ----------------------------- */

#define TUI_REFUSE(what) tui_not_yet(what, err, err_size)

static int tui_copy(const char *text, long len, char *err, size_t err_size) {
    (void)text; (void)len;
    return TUI_REFUSE("copy");
}

static int tui_clipboard(char **text, long *len, char *err, size_t err_size) {
    (void)text; (void)len;
    return TUI_REFUSE("clipboard");
}

static int tui_has_icon(void *window, const char *name, int *has, char *err, size_t err_size) {
    (void)window; (void)name; (void)has;
    return TUI_ERR("the tui backend has no `icons` group");
}

static int tui_capture(void *window, const char *name, double scale, char **png, long *len, int *width,
                       int *height, char *err, size_t err_size) {
    (void)window; (void)name; (void)scale; (void)png; (void)len; (void)width; (void)height;
    return TUI_ERR("the tui backend has no `figures` group");
}

static int tui_render_svg(const char *svg, long svg_len, int width, int height, double scale, char **png,
                          long *len, int *out_width, int *out_height, char *err, size_t err_size) {
    (void)svg; (void)svg_len; (void)width; (void)height; (void)scale; (void)png; (void)len;
    (void)out_width; (void)out_height;
    return TUI_ERR("the tui backend has no `figures` group");
}

static int tui_blocks_set(void *window, const char *name, const GbUiBlockItem *items, int count, char *err,
                          size_t err_size) {
    (void)window; (void)name; (void)items; (void)count;
    return TUI_ERR("the tui backend has no `notebook` group");
}

static int tui_blocks_get(void *window, const char *name, GbUiBlockInfo *out, int max, char *err,
                          size_t err_size) {
    (void)window; (void)name; (void)out; (void)max;
    return TUI_ERR("the tui backend has no `notebook` group");
}

static int tui_fold_set(void *window, const char *name, const int *from, const int *to, int count,
                        char *err, size_t err_size) {
    (void)window; (void)name; (void)from; (void)to; (void)count;
    return TUI_ERR("the tui backend has no `notebook` group");
}

static int tui_fold_get(void *window, const char *name, int *from, int *to, int max, char *err,
                        size_t err_size) {
    (void)window; (void)name; (void)from; (void)to; (void)max;
    return TUI_ERR("the tui backend has no `notebook` group");
}

static int tui_block_event(char **id, char **action, char **target) {
    (void)id; (void)action; (void)target;
    return 0;
}

static char *tui_link_event(void) {
    return NULL;
}

static int tui_link_press(void *window, const char *name, const char *target, char *err, size_t err_size) {
    (void)window; (void)name; (void)target;
    return TUI_ERR("the tui backend has no `notebook` group");
}

static int tui_block_press(void *window, const char *name, const char *id, const char *action,
                           const char *target, char *err, size_t err_size) {
    (void)window; (void)name; (void)id; (void)action; (void)target;
    return TUI_ERR("the tui backend has no `notebook` group");
}

static int tui_has_highlighting(void) {
    return 0;
}

static void tui_syntax_path(const char *dir) {
    (void)dir;
}

static int tui_highlight(void *window, const char *name, const char *language, const char *theme, char *err,
                         size_t err_size) {
    (void)window; (void)name; (void)language; (void)theme;
    return TUI_ERR("the tui backend has no `highlighting` group");
}

static int tui_dock_info(void *window, const char *name, char *area, size_t area_size, int *floating,
                         int *tabbed, int *visible, char *err, size_t err_size) {
    (void)window; (void)name; (void)area; (void)area_size; (void)floating; (void)tabbed; (void)visible;
    return TUI_ERR("the tui backend has no `docking` group");
}

static char *tui_layout_save(void *window, char *err, size_t err_size) {
    (void)window;
    TUI_ERR("the tui backend has no `docking` group");
    return NULL;
}

static int tui_layout_restore(void *window, const char *text, char *err, size_t err_size) {
    (void)window; (void)text;
    return TUI_ERR("the tui backend has no `docking` group");
}

static int tui_caret(void *window, const char *name, int *line, int *column, int *selecting, char *err,
                     size_t err_size) {
    (void)window; (void)name; (void)line; (void)column; (void)selecting;
    return TUI_REFUSE("memo");
}

static int tui_caret_to(void *window, const char *name, int line, int column, char *err, size_t err_size) {
    (void)window; (void)name; (void)line; (void)column;
    return TUI_REFUSE("memo");
}

static int tui_select_text(void *window, const char *name, int line, int column, int line2, int column2,
                           char *err, size_t err_size) {
    (void)window; (void)name; (void)line; (void)column; (void)line2; (void)column2;
    return TUI_REFUSE("memo");
}

static int tui_selection(void *window, const char *name, int *span, char **text, char *err, size_t err_size) {
    (void)window; (void)name; (void)span; (void)text;
    TUI_REFUSE("memo");
    return -1;
}

static int tui_replace_text(void *window, const char *name, const int *spans, const char *const *texts,
                            size_t n, char *err, size_t err_size) {
    (void)window; (void)name; (void)spans; (void)texts; (void)n;
    return TUI_REFUSE("memo");
}

static int tui_highlight_text(void *window, const char *name, const int *spans, size_t n, char *err,
                              size_t err_size) {
    (void)window; (void)name; (void)spans; (void)n;
    return TUI_REFUSE("memo");
}

static int tui_highlights(void *window, const char *name, int **spans, size_t *n, char *err,
                          size_t err_size) {
    (void)window; (void)name; (void)spans; (void)n;
    return TUI_REFUSE("memo");
}

static int tui_gutter_enable(void *window, const char *name, int numbers, int marks, char *err,
                             size_t err_size) {
    (void)window; (void)name; (void)numbers; (void)marks;
    return TUI_REFUSE("source");
}

static int tui_gutter_info(void *window, const char *name, int *numbers, int *marks, int *width, char *err,
                           size_t err_size) {
    (void)window; (void)name; (void)numbers; (void)marks; (void)width;
    return TUI_REFUSE("source");
}

static int tui_mark_kind(const char *category) {
    static const char *const kinds[] = { "section", "break", "error", "warning", "note", NULL };
    for (int i = 0; kinds[i]; i++) {
        if (strcmp(kinds[i], category) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *tui_mark_name(int kind) {
    static const char *const kinds[] = { "section", "break", "error", "warning", "note" };
    return kind >= 0 && kind < 5 ? kinds[kind] : NULL;
}

static int tui_mark_count(void) {
    return 5;
}

static int tui_mark_set(void *window, const char *name, int kind, const int *lines, int count, char *err,
                        size_t err_size) {
    (void)window; (void)name; (void)kind; (void)lines; (void)count;
    return TUI_REFUSE("source");
}

static int tui_mark_get(void *window, const char *name, int kind, int *out, int max, char *err,
                        size_t err_size) {
    (void)window; (void)name; (void)kind; (void)out; (void)max;
    return TUI_REFUSE("source");
}

static int tui_gutter_press(void *window, const char *name, int line, char *err, size_t err_size) {
    (void)window; (void)name; (void)line;
    return TUI_REFUSE("source");
}

static int tui_gutter_click(int *line) {
    (void)line;
    return 0;
}

static void tui_gutter_painted(int *lines, int *marks, int *first) {
    *lines = *marks = *first = 0;
}

static char *tui_highlight_language(void *window, const char *name) {
    (void)window; (void)name;
    return NULL;
}

static char *tui_highlight_file(void *window, const char *name) {
    (void)window; (void)name;
    return NULL;
}

static char *tui_highlight_theme(void *window, const char *name) {
    (void)window; (void)name;
    return NULL;
}

/* ---- dialogs (increment 5) --------------------------------------------------------
 *
 * A MESSAGE BOX AND A FILE CHOOSER ARE BOXES DRAWN OVER THE WINDOW, and each
 * turns the loop again inside (tui_loop) until it has its answer, as Qt's
 * `exec()` does, so gBASIC's sources are served while one is up and a timer
 * can answer it (`ui.answer`). One stack, because a handler run inside the
 * first may ask its own question; the innermost takes the keys. */

typedef struct {
    int chooser;            /* 0 a message box, 1 a file chooser */
    TuiNode *window;        /* what it hangs off */
    char *title, *text;
    int ask;                /* a confirm: Yes and No, No the default */
    int done, yes;
    int button;             /* a message box's focused button: 0 the first */
    /* a chooser */
    int pick;               /* GB_UI_PICK_* */
    char *folder;           /* absolute */
    char **filters;
    int nfilters, filter;
    char **names;           /* the folder's entries, ".." first, then folders */
    unsigned char *dirs;
    int nnames, sel, top;
    char *name;             /* the name field */
    size_t caret;
    int focus;              /* 0 the list, 1 the name, 2 the filter, 3 accept, 4 cancel */
    char *chosen, *note;
    /* where it was drawn, 0-based cells, for the mouse */
    int x, y, w, h, list_y, list_h, name_y, filter_y, buttons_y;
    int b0x, b0w, b1x, b1w;
} TuiDialog;

static TuiDialog *tui_dialogs[8];
static int tui_ndialogs = 0;

/* A modal window's turn of the loop: what `ui.accept`/`ui.reject` answer and
 * what a close ends. */
typedef struct {
    TuiNode *root;
    int done, accepted;
} TuiModal;

static TuiModal *tui_modals[8];
static int tui_nmodals = 0;

static TuiDialog *tui_dialog_top(void) {
    return tui_ndialogs ? tui_dialogs[tui_ndialogs - 1] : NULL;
}

static int tui_dialog_open(void) {
    return tui_ndialogs > 0;
}

/* A window closing ends what hangs off it: its dialogs are cancelled, as Qt
 * deletes a box with its parent, and its own modal turn ends unanswered. */
static void tui_dialogs_end_for(TuiNode *root) {
    for (int i = 0; i < tui_ndialogs; i++) {
        if (tui_dialogs[i]->window == root) {
            tui_dialogs[i]->done = 1;
            tui_dialogs[i]->yes = 0;
        }
    }
    for (int i = 0; i < tui_nmodals; i++) {
        if (tui_modals[i]->root == root) {
            tui_modals[i]->done = 1;
        }
    }
}

static int tui_dialog_depth(void) {
    return tui_ndialogs;
}

/* The box's frame: a border with the title in its top edge. */
static void tui_box_draw(int x, int y, int w, int h, const char *title, TermStyle st, TermStyle ttl) {
    if (w < 2 || h < 2) {
        return;
    }
    tui_fill(x, y, w, h, st);
    for (int c = 1; c < w - 1; c++) {
        tui_put_clipped(x + c, y, 1, "\xE2\x94\x80", st);
        tui_put_clipped(x + c, y + h - 1, 1, "\xE2\x94\x80", st);
    }
    for (int r = 1; r < h - 1; r++) {
        tui_put_clipped(x, y + r, 1, "\xE2\x94\x82", st);
        tui_put_clipped(x + w - 1, y + r, 1, "\xE2\x94\x82", st);
    }
    tui_put_clipped(x, y, 1, "\xE2\x94\x8C", st);
    tui_put_clipped(x + w - 1, y, 1, "\xE2\x94\x90", st);
    tui_put_clipped(x, y + h - 1, 1, "\xE2\x94\x94", st);
    tui_put_clipped(x + w - 1, y + h - 1, 1, "\xE2\x94\x98", st);
    if (title && *title && w > 4) {
        size_t len = strlen(title) + 3;
        char *s = malloc(len);
        if (!s) {
            abort();
        }
        snprintf(s, len, " %s ", title);
        tui_put_clipped(x + 1, y, w - 2, s, ttl);
        free(s);
    }
}

/* A text in lines no wider than `width` cells, broken at spaces; a word
 * wider than the line is cut when drawn. Lines are malloc'd. */
static int tui_wrap(const char *text, int width, char ***out) {
    char **lines = NULL;
    int n = 0, cap = 0;
    const char *p = text ? text : "";
    for (;;) {
        size_t para = strcspn(p, "\n");
        const char *end = p + para;
        const char *q = p;
        do {
            /* as many words as fit */
            const char *line_end = q, *scan = q;
            while (scan < end) {
                const char *w_end = scan;
                while (w_end < end && *w_end != ' ') {
                    w_end++;
                }
                char *trial = malloc((size_t)(w_end - q) + 1);
                if (!trial) {
                    abort();
                }
                memcpy(trial, q, (size_t)(w_end - q));
                trial[w_end - q] = 0;
                int cells = tui_text_cells(trial);
                free(trial);
                if (cells > width && line_end > q) {
                    break;
                }
                line_end = w_end;
                scan = w_end;
                while (scan < end && *scan == ' ') {
                    scan++;
                }
                if (cells > width) {
                    break;
                }
            }
            if (n == cap) {
                cap = cap ? cap * 2 : 8;
                lines = realloc(lines, sizeof *lines * (size_t)cap);
                if (!lines) {
                    abort();
                }
            }
            lines[n] = malloc((size_t)(line_end - q) + 1);
            if (!lines[n]) {
                abort();
            }
            memcpy(lines[n], q, (size_t)(line_end - q));
            lines[n][line_end - q] = 0;
            n++;
            q = line_end;
            while (q < end && *q == ' ') {
                q++;
            }
        } while (q < end);
        if (!*end) {
            break;
        }
        p = end + 1;
    }
    *out = lines;
    return n;
}

static void tui_lines_free(char **lines, int n) {
    for (int i = 0; i < n; i++) {
        free(lines[i]);
    }
    free(lines);
}

static const char *tui_accept_word(const TuiDialog *d) {
    return d->pick == GB_UI_PICK_SAVE ? "Save" : (d->pick == GB_UI_PICK_FOLDER ? "Choose" : "Open");
}

/* Two buttons centred, or right-aligned in a chooser; their places kept for
 * the mouse. */
static void tui_dialog_buttons(TuiDialog *d, const char *b0, const char *b1, int focus0, int focus1,
                               int right) {
    TermStyle plain = tui_style(tui_fg, tui_bg, 0);
    TermStyle lit = tui_style(tui_on_sel, tui_sel, tui_sel == TERM_DEFAULT ? TERM_REVERSE : 0);
    char s0[48], s1[48];
    snprintf(s0, sizeof s0, "[ %s ]", b0);
    snprintf(s1, sizeof s1, "[ %s ]", b1 ? b1 : "");
    d->b0w = tui_text_cells(s0);
    d->b1w = b1 ? tui_text_cells(s1) : 0;
    int all = d->b0w + (b1 ? 2 + d->b1w : 0);
    int x0 = right ? d->x + d->w - 2 - all : d->x + (d->w - all) / 2;
    d->b0x = x0;
    d->b1x = x0 + d->b0w + 2;
    tui_put_clipped(d->b0x, d->buttons_y, d->b0w, s0, focus0 ? lit : plain);
    if (b1) {
        tui_put_clipped(d->b1x, d->buttons_y, d->b1w, s1, focus1 ? lit : plain);
    }
}

static void tui_message_draw(TuiDialog *d) {
    int width = tui_screen.columns - 8;
    if (width > 60) {
        width = 60;
    }
    if (width < 10) {
        width = 10;
    }
    char **lines;
    int n = tui_wrap(d->text, width, &lines);
    int inner = d->ask ? 18 : 6;
    for (int i = 0; i < n; i++) {
        int c = tui_text_cells(lines[i]);
        if (c > inner) {
            inner = c;
        }
    }
    int tw = tui_text_cells(d->title) + 2;
    if (tw > inner) {
        inner = tw;
    }
    d->w = inner + 4;
    d->h = n + 4;
    if (d->w > tui_screen.columns) {
        d->w = tui_screen.columns;
    }
    if (d->h > tui_screen.rows) {
        d->h = tui_screen.rows;
    }
    d->x = (tui_screen.columns - d->w) / 2;
    d->y = (tui_screen.rows - d->h) / 2;
    TermStyle st = tui_style(tui_fg, tui_bg, 0);
    tui_box_draw(d->x, d->y, d->w, d->h, d->title, st, tui_style(tui_fg, tui_bg, TERM_BOLD));
    for (int i = 0; i < n; i++) {
        tui_put_clipped(d->x + 2, d->y + 1 + i, d->w - 4, lines[i], st);
    }
    tui_lines_free(lines, n);
    d->buttons_y = d->y + d->h - 2;
    if (d->ask) {
        tui_dialog_buttons(d, "Yes", "No", d->button == 0, d->button == 1, 0);
    } else {
        tui_dialog_buttons(d, "OK", NULL, 1, 0, 0);
    }
}

static void tui_chooser_draw(TuiDialog *d) {
    d->w = tui_screen.columns - 4;
    if (d->w > 72) {
        d->w = 72;
    }
    d->h = tui_screen.rows - 2;
    if (d->h > 20) {
        d->h = 20;
    }
    if (d->w < 20) {
        d->w = tui_screen.columns;
    }
    if (d->h < 8) {
        d->h = tui_screen.rows;
    }
    d->x = (tui_screen.columns - d->w) / 2;
    d->y = (tui_screen.rows - d->h) / 2;
    TermStyle st = tui_style(tui_fg, tui_bg, 0);
    TermStyle faint = tui_style(tui_faint, tui_bg, tui_faint == TERM_DEFAULT ? TERM_DIM : 0);
    TermStyle lit = tui_style(tui_on_sel, tui_sel, tui_sel == TERM_DEFAULT ? TERM_REVERSE : 0);
    TermStyle field = tui_style(tui_fg, tui_base, tui_base == TERM_DEFAULT ? TERM_UNDERLINE : 0);
    tui_box_draw(d->x, d->y, d->w, d->h, d->title, st, tui_style(tui_fg, tui_bg, TERM_BOLD));
    int inner = d->w - 4, row = d->y + 1;
    /* the folder, its tail when it is too long to show whole */
    const char *f = d->folder;
    int lw = tui_text_cells("Folder: ");
    while (tui_text_cells(f) > inner - lw && strchr(f + 1, '/')) {
        f = strchr(f + 1, '/');
    }
    tui_put_clipped(d->x + 2, row, lw, "Folder: ", faint);
    tui_put_clipped(d->x + 2 + lw, row, inner - lw, f, st);
    row++;
    int below = 1 + (d->nfilters ? 1 : 0) + 1 + 1;     /* name, filter, note, buttons */
    d->list_y = row;
    d->list_h = d->y + d->h - 1 - row - below;
    if (d->list_h < 1) {
        d->list_h = 1;
    }
    if (d->sel < d->top) {
        d->top = d->sel;
    }
    if (d->sel >= d->top + d->list_h) {
        d->top = d->sel - d->list_h + 1;
    }
    for (int i = 0; i < d->list_h; i++) {
        int at = d->top + i;
        if (at >= d->nnames) {
            break;
        }
        char *s = malloc(strlen(d->names[at]) + 2);
        if (!s) {
            abort();
        }
        sprintf(s, "%s%s", d->names[at], d->dirs[at] ? "/" : "");
        TermStyle es = at == d->sel ? (d->focus == 0 ? lit : tui_style(tui_fg, tui_bg, TERM_BOLD)) : st;
        if (at == d->sel) {
            tui_fill(d->x + 2, row + i, inner, 1, es);
        }
        tui_put_clipped(d->x + 2, row + i, inner, s, es);
        free(s);
    }
    if (!d->nnames) {
        tui_put_clipped(d->x + 2, row, inner, "(nothing here)", faint);
    }
    row = d->list_y + d->list_h;
    d->name_y = row;
    int nw = tui_text_cells("Name: ");
    tui_put_clipped(d->x + 2, row, nw, "Name: ", faint);
    tui_fill(d->x + 2 + nw, row, inner - nw, 1, field);
    const char *name = d->name ? d->name : "";
    int caret_cells = 0;
    {
        char *upto = malloc(d->caret + 1);
        if (!upto) {
            abort();
        }
        memcpy(upto, name, d->caret);
        upto[d->caret] = 0;
        caret_cells = tui_text_cells(upto);
        free(upto);
    }
    int shown_from = caret_cells >= inner - nw ? caret_cells - (inner - nw) + 1 : 0;
    const char *from = name;
    for (int skip = 0; *from && skip < shown_from; ) {
        size_t k = 1;
        while (from[k] && ((unsigned char)from[k] & 0xC0) == 0x80) {
            k++;
        }
        char one[8] = { 0 };
        memcpy(one, from, k < 7 ? k : 7);
        skip += tui_text_cells(one);
        from += k;
    }
    tui_put_clipped(d->x + 2 + nw, row, inner - nw, from, field);
    if (d->focus == 1) {
        tui_screen.cursor_column = d->x + 2 + nw + caret_cells - shown_from + 1;
        tui_screen.cursor_row = row + 1;
        tui_screen.cursor_visible = 1;
    }
    row++;
    d->filter_y = -1;
    if (d->nfilters) {
        d->filter_y = row;
        int fw = tui_text_cells("Filter: ");
        tui_put_clipped(d->x + 2, row, fw, "Filter: ", faint);
        char *s = malloc(strlen(d->filters[d->filter]) + 8);
        if (!s) {
            abort();
        }
        sprintf(s, d->nfilters > 1 ? "< %s >" : "%s", d->filters[d->filter]);
        tui_put_clipped(d->x + 2 + fw, row, inner - fw, s, d->focus == 2 ? lit : st);
        free(s);
        row++;
    }
    if (d->note) {
        tui_put_clipped(d->x + 2, row, inner, d->note, tui_style(tui_fg, tui_bg, TERM_BOLD));
    }
    row++;
    d->buttons_y = row;
    tui_dialog_buttons(d, tui_accept_word(d), "Cancel", d->focus == 3, d->focus == 4, 1);
}

static void tui_dialogs_draw(void) {
    for (int i = 0; i < tui_ndialogs; i++) {
        TuiDialog *d = tui_dialogs[i];
        tui_screen.cursor_visible = 0;      /* only the innermost's caret shows */
        if (d->chooser) {
            tui_chooser_draw(d);
        } else {
            tui_message_draw(d);
        }
    }
}

/* ---- the chooser's folder ---------------------------------------------------------- */

/* An absolute path with `.`, `..` and doubled slashes taken out, by its
 * letters: a folder is shown as the program named it, links unresolved, as
 * Qt's `absolutePath()` gives it. */
static char *tui_path_clean(const char *path) {
    size_t len = strlen(path);
    char *out = malloc(len + 2);
    if (!out) {
        abort();
    }
    size_t o = 0;
    out[o++] = '/';
    const char *p = path;
    while (*p) {
        while (*p == '/') {
            p++;
        }
        size_t n = strcspn(p, "/");
        if (n == 0) {
            break;
        }
        if (n == 1 && p[0] == '.') {
            /* nothing */
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            if (o > 1) {
                o--;
                while (o > 1 && out[o - 1] != '/') {
                    o--;
                }
            }
        } else {
            if (o > 1) {
                out[o++] = '/';
            }
            memcpy(out + o, p, n);
            o += n;
        }
        p += n;
    }
    if (o > 1 && out[o - 1] == '/') {
        o--;
    }
    out[o] = 0;
    return out;
}

/* `name` in `folder`, or `name` itself when it is absolute; cleaned. */
static char *tui_path_in(const char *folder, const char *name) {
    if (name[0] == '/') {
        return tui_path_clean(name);
    }
    size_t len = strlen(folder) + strlen(name) + 2;
    char *joined = malloc(len);
    if (!joined) {
        abort();
    }
    snprintf(joined, len, "%s/%s", folder, name);
    char *out = tui_path_clean(joined);
    free(joined);
    return out;
}

static char *tui_cwd(void) {
    char buf[4096];
    return tui_path_clean(getcwd(buf, sizeof buf) ? buf : "/");
}

static int tui_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* A filter's patterns: those in its brackets, `Text (*.txt *.md)`, or the
 * whole of it when it has none, as Qt reads a name filter. */
static int tui_filter_matches(const char *filter, const char *name) {
    if (!filter) {
        return 1;
    }
    const char *open = strrchr(filter, '(');
    const char *p = open ? open + 1 : filter;
    const char *end = open && strchr(open, ')') ? strchr(open, ')') : p + strlen(p);
    while (p < end) {
        while (p < end && (*p == ' ' || *p == ';')) {
            p++;
        }
        size_t n = 0;
        while (p + n < end && p[n] != ' ' && p[n] != ';') {
            n++;
        }
        if (n) {
            char pat[256];
            snprintf(pat, sizeof pat, "%.*s", (int)n, p);
#ifdef FNM_CASEFOLD
            if (fnmatch(pat, name, FNM_CASEFOLD) == 0) {
#else
            if (fnmatch(pat, name, 0) == 0) {
#endif
                return 1;
            }
        }
        p += n;
    }
    return 0;
}

static int tui_name_order(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void tui_chooser_list(TuiDialog *d) {
    for (int i = 0; i < d->nnames; i++) {
        free(d->names[i]);
    }
    free(d->names);
    free(d->dirs);
    d->names = NULL;
    d->dirs = NULL;
    d->nnames = d->sel = d->top = 0;
    char **dirs = NULL, **files = NULL;
    int nd = 0, nf = 0, cd = 0, cf = 0;
    DIR *dir = opendir(d->folder);
    if (dir) {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (e->d_name[0] == '.') {
                continue;       /* hidden, as Qt's chooser hides them */
            }
            char *full = tui_path_in(d->folder, e->d_name);
            int is_dir = tui_is_dir(full);
            free(full);
            if (!is_dir && (d->pick == GB_UI_PICK_FOLDER ||
                            !tui_filter_matches(d->nfilters ? d->filters[d->filter] : NULL, e->d_name))) {
                continue;
            }
            char ***list = is_dir ? &dirs : &files;
            int *count = is_dir ? &nd : &nf, *cap = is_dir ? &cd : &cf;
            if (*count == *cap) {
                *cap = *cap ? *cap * 2 : 16;
                *list = realloc(*list, sizeof **list * (size_t)*cap);
                if (!*list) {
                    abort();
                }
            }
            (*list)[(*count)++] = tui_dup(e->d_name);
        }
        closedir(dir);
    }
    qsort(dirs, (size_t)nd, sizeof *dirs, tui_name_order);
    qsort(files, (size_t)nf, sizeof *files, tui_name_order);
    int up = strcmp(d->folder, "/") != 0;
    int total = up + nd + nf;
    d->names = malloc(sizeof *d->names * (size_t)(total ? total : 1));
    d->dirs = malloc((size_t)(total ? total : 1));
    if (!d->names || !d->dirs) {
        abort();
    }
    if (up) {
        d->names[d->nnames] = tui_dup("..");
        d->dirs[d->nnames++] = 1;
    }
    for (int i = 0; i < nd; i++) {
        d->names[d->nnames] = dirs[i];
        d->dirs[d->nnames++] = 1;
    }
    for (int i = 0; i < nf; i++) {
        d->names[d->nnames] = files[i];
        d->dirs[d->nnames++] = 0;
    }
    free(dirs);
    free(files);
    /* on the named file, when it is here */
    for (int i = 0; d->name && i < d->nnames; i++) {
        if (strcmp(d->names[i], d->name) == 0) {
            d->sel = i;
        }
    }
    tui_dirty = 1;
}

static void tui_chooser_name(TuiDialog *d, const char *name) {
    tui_set(&d->name, name);
    d->caret = d->name ? strlen(d->name) : 0;
}

static void tui_chooser_into(TuiDialog *d, const char *folder) {
    free(d->folder);
    d->folder = tui_path_clean(folder);
    if (d->pick != GB_UI_PICK_SAVE) {
        tui_chooser_name(d, "");
    }
    tui_chooser_list(d);
}

/* TAKE A NAME, the one rule for a person's Open and for `ui.answer(path)`:
 * 1 taken; 0 a folder, gone into (Qt's Open on a folder does that); -1 not
 * taken, with why. */
static int tui_chooser_take(TuiDialog *d, const char *typed, char *why, size_t why_size) {
    why[0] = 0;
    if (!typed || !*typed) {
        if (d->pick == GB_UI_PICK_FOLDER) {
            d->chosen = tui_dup(d->folder);
            d->yes = 1;
            d->done = 1;
            return 1;
        }
        snprintf(why, why_size, "type a name, or choose one from the list");
        return -1;
    }
    char *full = tui_path_in(d->folder, typed);
    struct stat st;
    int there = stat(full, &st) == 0;
    if (there && S_ISDIR(st.st_mode)) {
        if (d->pick == GB_UI_PICK_FOLDER) {
            d->chosen = full;
            d->yes = 1;
            d->done = 1;
            return 1;
        }
        tui_chooser_into(d, full);
        free(full);
        return 0;
    }
    if (d->pick == GB_UI_PICK_FOLDER || d->pick == GB_UI_PICK_OPEN) {
        if (!there) {
            snprintf(why, why_size, "this chooser will not take `%s`, which is not there", typed);
        } else if (d->pick == GB_UI_PICK_FOLDER) {
            snprintf(why, why_size, "this chooser will not take `%s`, which is not a folder", typed);
        } else if (!S_ISREG(st.st_mode)) {
            snprintf(why, why_size, "this chooser will not take `%s`", typed);
        }
        if (why[0]) {
            free(full);
            return -1;
        }
    } else {
        /* a file to save need not be there, but its folder must */
        char *slash = strrchr(full, '/');
        char *parent = slash == full ? tui_dup("/") : tui_dup(full);
        if (slash != full) {
            parent[slash - full] = 0;
        }
        int ok = tui_is_dir(parent);
        free(parent);
        if (!ok) {
            snprintf(why, why_size, "this chooser will not take `%s`, whose folder is not there", typed);
            free(full);
            return -1;
        }
    }
    d->chosen = full;
    d->yes = 1;
    d->done = 1;
    return 1;
}

/* A person's Open: a refusal is shown in the box, not raised. */
static void tui_chooser_go(TuiDialog *d, const char *typed) {
    char why[512];
    free(d->note);
    d->note = NULL;
    if (tui_chooser_take(d, typed, why, sizeof why) < 0) {
        /* the box has one line for it, so the sentence `ui.answer` raises
         * is said shorter here */
        static const char lead[] = "this chooser will not take ";
        if (strncmp(why, lead, sizeof lead - 1) == 0) {
            char shorter[sizeof why + 16];
            snprintf(shorter, sizeof shorter, "Not taken: %s", why + sizeof lead - 1);
            d->note = tui_dup(shorter);
        } else {
            d->note = tui_dup(why);
        }
    }
    tui_dirty = 1;
}

/* The list's entry under the selection, as a person moving onto it sees it
 * in the name field (Qt's chooser does the same). */
static void tui_chooser_pointed(TuiDialog *d) {
    if (d->sel < 0 || d->sel >= d->nnames) {
        return;
    }
    if (strcmp(d->names[d->sel], "..") == 0) {
        return;
    }
    if (d->dirs[d->sel] ? d->pick == GB_UI_PICK_FOLDER : d->pick != GB_UI_PICK_FOLDER) {
        tui_chooser_name(d, d->names[d->sel]);
    }
}

static void tui_chooser_enter_entry(TuiDialog *d) {
    if (d->sel < 0 || d->sel >= d->nnames) {
        return;
    }
    if (d->dirs[d->sel]) {
        char *to = tui_path_in(d->folder, d->names[d->sel]);
        tui_chooser_into(d, to);
        free(to);
        return;
    }
    tui_chooser_go(d, d->names[d->sel]);
}

/* The name field's editing: typing, deleting, the caret. 1 when used. */
static int tui_chooser_edit(TuiDialog *d, const TermEvent *ev) {
    const char *k = ev->key;
    size_t len = d->name ? strlen(d->name) : 0;
    if (ev->text[0] && !(ev->mods & (TERM_MOD_CTRL | TERM_MOD_ALT))) {
        size_t add = strlen(ev->text);
        char *s = malloc(len + add + 1);
        if (!s) {
            abort();
        }
        memcpy(s, d->name ? d->name : "", d->caret);
        memcpy(s + d->caret, ev->text, add);
        memcpy(s + d->caret + add, d->name ? d->name + d->caret : "", len - d->caret + 1);
        free(d->name);
        d->name = s;
        d->caret += add;
        return 1;
    }
    if (strcmp(k, "backspace") == 0 && d->caret > 0) {
        size_t from = d->caret - 1;
        while (from > 0 && ((unsigned char)d->name[from] & 0xC0) == 0x80) {
            from--;
        }
        memmove(d->name + from, d->name + d->caret, len - d->caret + 1);
        d->caret = from;
        return 1;
    }
    if (strcmp(k, "delete") == 0 && d->caret < len) {
        size_t to = d->caret + 1;
        while (to < len && ((unsigned char)d->name[to] & 0xC0) == 0x80) {
            to++;
        }
        memmove(d->name + d->caret, d->name + to, len - to + 1);
        return 1;
    }
    if (strcmp(k, "left") == 0 && d->caret > 0) {
        d->caret--;
        while (d->caret > 0 && ((unsigned char)d->name[d->caret] & 0xC0) == 0x80) {
            d->caret--;
        }
        return 1;
    }
    if (strcmp(k, "right") == 0 && d->caret < len) {
        d->caret++;
        while (d->caret < len && ((unsigned char)d->name[d->caret] & 0xC0) == 0x80) {
            d->caret++;
        }
        return 1;
    }
    if (strcmp(k, "home") == 0) {
        d->caret = 0;
        return 1;
    }
    if (strcmp(k, "end") == 0) {
        d->caret = len;
        return 1;
    }
    return 0;
}

static void tui_chooser_filter_step(TuiDialog *d, int by) {
    if (d->nfilters > 1) {
        d->filter = (d->filter + by + d->nfilters) % d->nfilters;
        tui_chooser_list(d);
    }
}

static int tui_chooser_key(TuiDialog *d, const TermEvent *ev) {
    const char *k = ev->key;
    if (strcmp(k, "escape") == 0 || ((ev->mods & TERM_MOD_CTRL) && strcmp(k, "q") == 0)) {
        d->done = 1;
        d->yes = 0;
        return 1;
    }
    if (strcmp(k, "tab") == 0) {
        int by = ev->mods & TERM_MOD_SHIFT ? -1 : 1;
        do {
            d->focus = (d->focus + by + 5) % 5;
        } while (d->focus == 2 && d->nfilters < 2);
        return 1;
    }
    switch (d->focus) {
    case 0:
        if (strcmp(k, "up") == 0 || strcmp(k, "down") == 0 || strcmp(k, "page_up") == 0 ||
            strcmp(k, "page_down") == 0 || strcmp(k, "home") == 0 || strcmp(k, "end") == 0) {
            int page = d->list_h > 1 ? d->list_h - 1 : 1;
            int to = d->sel;
            if (strcmp(k, "up") == 0) to--;
            else if (strcmp(k, "down") == 0) to++;
            else if (strcmp(k, "page_up") == 0) to -= page;
            else if (strcmp(k, "page_down") == 0) to += page;
            else if (strcmp(k, "home") == 0) to = 0;
            else to = d->nnames - 1;
            if (to >= d->nnames) to = d->nnames - 1;
            if (to < 0) to = 0;
            d->sel = to;
            tui_chooser_pointed(d);
            return 1;
        }
        if (strcmp(k, "enter") == 0) {
            tui_chooser_enter_entry(d);
            return 1;
        }
        if (strcmp(k, "backspace") == 0 && strcmp(d->folder, "/") != 0) {
            char *to = tui_path_in(d->folder, "..");
            tui_chooser_into(d, to);
            free(to);
            return 1;
        }
        if (ev->text[0] && !(ev->mods & (TERM_MOD_CTRL | TERM_MOD_ALT))) {
            /* typing goes to the name */
            d->focus = 1;
            tui_chooser_name(d, "");
            return tui_chooser_edit(d, ev);
        }
        return 0;
    case 1:
        if (strcmp(k, "enter") == 0) {
            char *typed = tui_dup(d->name ? d->name : "");
            tui_chooser_go(d, typed);
            free(typed);
            return 1;
        }
        if (strcmp(k, "up") == 0 || strcmp(k, "down") == 0) {
            d->focus = 0;
            return tui_chooser_key(d, ev);
        }
        return tui_chooser_edit(d, ev);
    case 2:
        if (strcmp(k, "left") == 0 || strcmp(k, "right") == 0 || strcmp(k, "space") == 0) {
            tui_chooser_filter_step(d, strcmp(k, "left") == 0 ? -1 : 1);
            return 1;
        }
        return 0;
    default:
        if (strcmp(k, "left") == 0 || strcmp(k, "right") == 0) {
            d->focus = d->focus == 3 ? 4 : 3;
            return 1;
        }
        if (strcmp(k, "enter") == 0 || strcmp(k, "space") == 0) {
            if (d->focus == 4) {
                d->done = 1;
                d->yes = 0;
            } else {
                char *typed = tui_dup(d->name ? d->name : "");
                tui_chooser_go(d, typed);
                free(typed);
            }
            return 1;
        }
        return 0;
    }
}

static int tui_message_key(TuiDialog *d, const TermEvent *ev) {
    const char *k = ev->key;
    if (strcmp(k, "escape") == 0 || ((ev->mods & TERM_MOD_CTRL) && strcmp(k, "q") == 0)) {
        d->yes = 0;
        d->done = 1;
        return 1;
    }
    if (d->ask && (strcmp(k, "left") == 0 || strcmp(k, "right") == 0 || strcmp(k, "tab") == 0)) {
        d->button = !d->button;
        return 1;
    }
    if (strcmp(k, "enter") == 0 || strcmp(k, "space") == 0) {
        d->yes = d->ask ? d->button == 0 : 1;
        d->done = 1;
        return 1;
    }
    if (d->ask && !(ev->mods & (TERM_MOD_CTRL | TERM_MOD_ALT)) &&
        (strcmp(k, "y") == 0 || strcmp(k, "n") == 0)) {
        d->yes = k[0] == 'y';
        d->done = 1;
        return 1;
    }
    return 0;
}

/* The keys while a dialog is up go to the innermost, as a box holds the
 * input; 1 when one was used. */
static int tui_dialog_key(const TermEvent *ev) {
    TuiDialog *d = tui_dialog_top();
    int used = d->chooser ? tui_chooser_key(d, ev) : tui_message_key(d, ev);
    tui_dirty = 1;
    return used;
}

static void tui_dialog_mouse(const TermEvent *ev) {
    TuiDialog *d = tui_dialog_top();
    int x = ev->column - 1, y = ev->row - 1;
    if (d->chooser && strcmp(ev->action, "wheel") == 0) {
        int to = d->sel + (ev->button == 4 ? -3 : 3);
        d->sel = to < 0 ? 0 : (to >= d->nnames ? d->nnames - 1 : to);
        tui_dirty = 1;
        return;
    }
    if (strcmp(ev->action, "press") != 0 || ev->button != 1) {
        return;
    }
    if (y == d->buttons_y && x >= d->b0x && x < d->b0x + d->b0w) {
        if (d->chooser) {
            d->focus = 3;
            char *typed = tui_dup(d->name ? d->name : "");
            tui_chooser_go(d, typed);
            free(typed);
        } else {
            d->yes = 1;
            d->done = 1;
        }
    } else if (d->b1w && y == d->buttons_y && x >= d->b1x && x < d->b1x + d->b1w) {
        d->yes = 0;
        d->done = 1;
    } else if (d->chooser && y >= d->list_y && y < d->list_y + d->list_h && x > d->x && x < d->x + d->w - 1) {
        int at = d->top + (y - d->list_y);
        if (at < d->nnames) {
            d->focus = 0;
            if (at == d->sel) {
                tui_chooser_enter_entry(d);   /* a second press opens it */
            } else {
                d->sel = at;
                tui_chooser_pointed(d);
            }
        }
    } else if (d->chooser && y == d->name_y && x > d->x && x < d->x + d->w - 1) {
        d->focus = 1;
    } else if (d->chooser && y == d->filter_y && x > d->x && x < d->x + d->w - 1) {
        d->focus = 2;
        tui_chooser_filter_step(d, 1);
    }
    tui_dirty = 1;
}

static void tui_dialog_free(TuiDialog *d) {
    free(d->title);
    free(d->text);
    free(d->folder);
    for (int i = 0; i < d->nfilters; i++) {
        free(d->filters[i]);
    }
    free(d->filters);
    for (int i = 0; i < d->nnames; i++) {
        free(d->names[i]);
    }
    free(d->names);
    free(d->dirs);
    free(d->name);
    free(d->chosen);
    free(d->note);
}

/* PUT A DIALOG UP AND TURN THE LOOP UNTIL IT HAS ITS ANSWER. The terminal
 * is entered for it if a run before left it (a dialog from `main` after
 * `ui.run` returned), and left again afterwards. */
static int tui_dialog_run(TuiDialog *d, char *err, size_t err_size) {
    if (tui_ndialogs >= (int)(sizeof tui_dialogs / sizeof tui_dialogs[0])) {
        return TUI_ERR("eight dialogs are open already, one over another");
    }
    int entered_here = !tui_headless && !tui_entered;
    if (!tui_enter_screen(err, err_size)) {
        return 0;
    }
    tui_dialogs[tui_ndialogs++] = d;
    tui_dirty = 1;
    tui_loop(&d->done, 50);
    tui_ndialogs--;
    tui_dirty = 1;
    tui_render();
    if (entered_here && !tui_running) {
        tui_leave_screen();
    }
    return 1;
}

static const char *tui_window_title(const TuiNode *root) {
    return root->title ? root->title : (root->name ? root->name : "");
}

static int tui_show_box(void *window, const char *text, int ask, int *answer, char *err, size_t err_size) {
    TuiNode *root = window;
    if (!root || !root->shown) {
        return TUI_ERR("that is not an open window");
    }
    TuiDialog d = { 0 };
    d.window = root;
    d.title = tui_dup(tui_window_title(root));
    d.text = tui_dup(text ? text : "");
    d.ask = ask;
    d.button = ask ? 1 : 0;     /* THE SAFE ONE IS THE DEFAULT, as in Qt */
    int ok = tui_dialog_run(&d, err, err_size);
    if (answer) {
        *answer = ok && d.done && d.yes;
    }
    tui_dialog_free(&d);
    return ok;
}

static int tui_inform(void *window, const char *text, char *err, size_t err_size) {
    return tui_show_box(window, text, 0, NULL, err, err_size);
}

static int tui_confirm(void *window, const char *text, int *answer, char *err, size_t err_size) {
    return tui_show_box(window, text, 1, answer, err, err_size);
}

static int tui_answer(int value, char *err, size_t err_size) {
    TuiDialog *d = tui_dialog_top();
    if (!d) {
        return TUI_ERR("no dialog is open");
    }
    if (d->chooser) {
        if (value) {
            return TUI_ERR("the open dialog is a file chooser, so `true` does not say which "
                           "file -- give ui.answer the path instead");
        }
        d->yes = 0;
        d->done = 1;
        return 1;
    }
    /* an `inform` has one button, so it is answered either way */
    d->yes = d->ask ? value != 0 : 1;
    d->done = 1;
    return 1;
}

static int tui_dialog_info(char **kind, char **title, char **text, char **folder, char **filter) {
    if (kind)   { *kind = NULL; }
    if (title)  { *title = NULL; }
    if (text)   { *text = NULL; }
    if (folder) { *folder = NULL; }
    if (filter) { *filter = NULL; }
    TuiDialog *d = tui_dialog_top();
    if (!d) {
        return 0;
    }
    if (title) {
        *title = copy_string(d->title ? d->title : "");
    }
    if (kind) {
        *kind = copy_string(d->chooser ? "chooser" : "message");
    }
    if (d->chooser) {
        if (folder) {
            *folder = copy_string(d->folder);
        }
        if (filter) {
            size_t len = 1;
            for (int i = 0; i < d->nfilters; i++) {
                len += strlen(d->filters[i]) + 2;
            }
            char *s = malloc(len);
            if (!s) {
                abort();
            }
            s[0] = 0;
            for (int i = 0; i < d->nfilters; i++) {
                if (i) {
                    strcat(s, ";;");
                }
                strcat(s, d->filters[i]);
            }
            *filter = s;
        }
    } else if (text) {
        *text = copy_string(d->text ? d->text : "");
    }
    return 1;
}

static char *tui_choose(void *window, int kind, const char *title, const char *folder, const char *name,
                        const char *filter, int *cancelled, char *err, size_t err_size) {
    if (cancelled) {
        *cancelled = 0;
    }
    TuiNode *root = window;
    if (!root || !root->shown) {
        TUI_ERR("that is not an open window");
        return NULL;
    }
    TuiDialog d = { 0 };
    d.chooser = 1;
    d.window = root;
    d.pick = kind;
    d.title = tui_dup(title && *title ? title : tui_window_title(root));
    if (folder && *folder) {
        char *cwd = tui_cwd();
        d.folder = tui_path_in(cwd, folder);
        free(cwd);
        if (!tui_is_dir(d.folder)) {
            free(d.folder);
            d.folder = NULL;
        }
    }
    if (!d.folder) {
        d.folder = tui_cwd();
    }
    /* `;;` IS THE FRONT'S JOIN, Qt's own separator */
    for (const char *p = filter; p && *p; ) {
        const char *sep = strstr(p, ";;");
        size_t n = sep ? (size_t)(sep - p) : strlen(p);
        if (n) {
            d.filters = realloc(d.filters, sizeof *d.filters * (size_t)(d.nfilters + 1));
            if (!d.filters) {
                abort();
            }
            d.filters[d.nfilters] = malloc(n + 1);
            if (!d.filters[d.nfilters]) {
                abort();
            }
            memcpy(d.filters[d.nfilters], p, n);
            d.filters[d.nfilters++][n] = 0;
        }
        p = sep ? sep + 2 : p + n;
    }
    tui_chooser_name(&d, name && *name ? name : "");
    tui_chooser_list(&d);
    d.focus = name && *name ? 1 : 0;
    char *out = NULL;
    if (tui_dialog_run(&d, err, err_size)) {
        /* THE ANSWER DECIDES, NOT THE NAME FIELD: a cancelled chooser that
         * still holds a name answers nothing (run_ui_chooser `cancel`) */
        if (d.done && d.yes && d.chosen) {
            out = d.chosen;
            d.chosen = NULL;
        } else if (cancelled) {
            *cancelled = 1;
        }
    }
    tui_dialog_free(&d);
    return out;
}

static int tui_answer_path(const char *path, char *err, size_t err_size) {
    TuiDialog *d = tui_dialog_top();
    if (!d) {
        return TUI_ERR("no dialog is open");
    }
    if (!d->chooser) {
        return TUI_ERR("the open dialog is a message box, which is answered with true or "
                       "false rather than with a path");
    }
    if (!path || !*path) {
        return TUI_ERR("ui.answer needs a path, or false to cancel");
    }
    /* THE CHOOSER'S OWN RULE, the one a person's Open goes through */
    char why[512];
    int took = tui_chooser_take(d, path, why, sizeof why);
    tui_dirty = 1;
    if (took < 0) {
        return TUI_ERR("%s", why);
    }
    if (took == 0) {
        return TUI_ERR("the chooser did not accept `%s`", path);
    }
    return 1;
}

/* ---- a modal window ---------------------------------------------------------------- */

static int tui_node_alive_in(const TuiNode *n, const TuiNode *at) {
    if (n == at) {
        return 1;
    }
    for (int i = 0; i < at->nkids; i++) {
        if (tui_node_alive_in(n, at->kids[i])) {
            return 1;
        }
    }
    return 0;
}

static int tui_node_alive(const TuiNode *n) {
    for (int i = 0; n && i < tui_nroots; i++) {
        if (tui_roots[i]->shown && tui_node_alive_in(n, tui_roots[i])) {
            return 1;
        }
    }
    return 0;
}

/* SHOWN AS A BOX OVER THE WINDOW UNDER IT, and the loop turned until
 * `ui.accept`, `ui.reject` or a close. The front closes it afterwards, so
 * its `closed` handler fires once however it ended (run_ui_modal). */
static int tui_show_modal(void *window, int *accepted, char *err, size_t err_size) {
    TuiNode *root = window;
    if (!root || root->kind != 1) {
        return TUI_ERR("that window was not opened modally");
    }
    if (tui_nmodals >= (int)(sizeof tui_modals / sizeof tui_modals[0])) {
        return TUI_ERR("eight modal windows are open already, one over another");
    }
    TuiNode *before = tui_focus;
    int entered_here = !tui_headless && !tui_entered;
    if (!tui_enter_screen(err, err_size)) {
        return 0;
    }
    tui_show(root);
    TuiModal m = { root, 0, 0 };
    tui_modals[tui_nmodals++] = &m;
    tui_loop(&m.done, 50);
    tui_nmodals--;
    if (accepted) {
        *accepted = m.accepted;
    }
    /* the focus back where it was, when that part is still there */
    tui_focus = tui_node_alive(before) ? before : NULL;
    tui_dirty = 1;
    if (entered_here && !tui_running) {
        tui_render();
        tui_leave_screen();
    }
    return 1;
}

static int tui_answer_modal(void *window, int accept, char *err, size_t err_size) {
    for (int i = tui_nmodals - 1; i >= 0; i--) {
        if (tui_modals[i]->root == window) {
            tui_modals[i]->accepted = accept != 0;
            tui_modals[i]->done = 1;
            return 1;
        }
    }
    return TUI_ERR("that window was not opened modally");
}

#include "ui_tui_parts.c"

static const GbUiBackend ui_tui_backend = {
    .name = "tui",
    /* `file_dialogs` with its chooser (increment 5); `highlighting` comes
     * with increment 7 (§3.2) */
    .groups = "file_dialogs",
    .can_display        = tui_can_display,
    .set_callbacks      = tui_set_callbacks,
    .theme_set          = tui_theme_set,
    .theme_record       = tui_theme_record,
    .theme_get          = tui_theme_get,
    .begin              = tui_begin,
    .window             = tui_window,
    .container          = tui_container,
    .part               = tui_part,
    .set_text           = tui_set_text,
    .set_number         = tui_set_number,
    .set_bool           = tui_set_bool,
    .layout_number      = tui_layout_number,
    .grow               = tui_grow,
    .span               = tui_span,
    .cell_of            = tui_cell_of,
    .pane_of            = tui_pane_of,
    .connect            = tui_connect,
    .invoke             = tui_invoke,
    .read               = tui_read,
    .write              = tui_write,
    .set_text_list      = tui_set_text_list,
    .set_part_text_list = tui_set_part_text_list,
    .shortcuts          = tui_shortcuts,
    .key                = tui_key,
    .column_align       = tui_column_align,
    .cell_alignment     = tui_cell_alignment,
    .theme_plain        = tui_theme_plain,
    .set_part_text      = tui_set_part_text,
    .set_part_number    = tui_set_part_number,
    .set_part_bool      = tui_set_part_bool,
    .get                = tui_get,
    .menu               = tui_menu,
    .menu_item          = tui_menu_item,
    .menu_separator     = tui_menu_separator,
    .popup_menu         = tui_popup_menu,
    .menu_count         = tui_menu_count,
    .menu_title         = tui_menu_title,
    .menu_entry_count   = tui_menu_entry_count,
    .menu_entry_title   = tui_menu_entry_title,
    .popup_entry_count  = tui_popup_entry_count,
    .popup_entry_title  = tui_popup_entry_title,
    .popup              = tui_popup,
    .right_click        = tui_right_click,
    .which_tab          = tui_which_tab,
    .copy               = tui_copy,
    .clipboard          = tui_clipboard,
    .wheel_event        = tui_wheel_event,
    .wheel_exact        = tui_wheel_exact,
    .swallow            = tui_swallow,
    .scroll             = tui_scroll,
    .pane_sizes         = tui_pane_sizes,
    .resize_panes       = tui_resize_panes,
    .has_icon           = tui_has_icon,
    .capture            = tui_capture,
    .render_svg         = tui_render_svg,
    .theme_known        = tui_theme_known,
    .theme_names        = tui_theme_names,
    .blocks_set         = tui_blocks_set,
    .blocks_get         = tui_blocks_get,
    .fold_set           = tui_fold_set,
    .fold_get           = tui_fold_get,
    .block_event        = tui_block_event,
    .link_event         = tui_link_event,
    .link_press         = tui_link_press,
    .block_press        = tui_block_press,
    .scrolled           = tui_scrolled,
    .scroll_to          = tui_scroll_to,
    .table              = tui_table,
    .choice_clear       = tui_choice_clear,
    .choice_add         = tui_choice_add,
    .virtual_table      = tui_virtual_table,
    .virtual_rows       = tui_virtual_rows,
    .virtual_row_count  = tui_virtual_row_count,
    .table_size         = tui_table_size,
    .table_set          = tui_table_set,
    .table_get          = tui_table_get,
    .table_rows         = tui_table_rows,
    .table_selected     = tui_table_selected,
    .table_heading      = tui_table_heading,
    .table_select       = tui_table_select,
    .set_display_only   = tui_set_display_only,
    .has_highlighting   = tui_has_highlighting,
    .syntax_path        = tui_syntax_path,
    .highlight          = tui_highlight,
    .retire_drain       = tui_retire_drain,
    .handler_depth      = tui_handler_depth,
    .handler_max        = tui_handler_max,
    .dock_info          = tui_dock_info,
    .layout_save        = tui_layout_save,
    .layout_restore     = tui_layout_restore,
    .caret              = tui_caret,
    .caret_to           = tui_caret_to,
    .select_text        = tui_select_text,
    .selection          = tui_selection,
    .replace_text       = tui_replace_text,
    .highlight_text     = tui_highlight_text,
    .highlights         = tui_highlights,
    .label              = tui_label,
    .click              = tui_click,
    .gutter_enable      = tui_gutter_enable,
    .gutter_info        = tui_gutter_info,
    .mark_kind          = tui_mark_kind,
    .mark_name          = tui_mark_name,
    .mark_count         = tui_mark_count,
    .mark_set           = tui_mark_set,
    .mark_get           = tui_mark_get,
    .gutter_press       = tui_gutter_press,
    .gutter_click       = tui_gutter_click,
    .gutter_painted     = tui_gutter_painted,
    .highlight_language = tui_highlight_language,
    .highlight_file     = tui_highlight_file,
    .highlight_theme    = tui_highlight_theme,
    .dialog_depth       = tui_dialog_depth,
    .inform             = tui_inform,
    .confirm            = tui_confirm,
    .answer             = tui_answer,
    .dialog_info        = tui_dialog_info,
    .choose             = tui_choose,
    .answer_path        = tui_answer_path,
    .show               = tui_show,
    .close              = tui_close,
    .show_modal         = tui_show_modal,
    .answer_modal       = tui_answer_modal,
    .run                = tui_run,
    .stop               = tui_stop,
    .end                = tui_end,
    .container_node     = tui_container_node,
    .node_word          = tui_node_word,
    .child_named        = tui_child_named,
    .child_count        = tui_child_count,
    .child_at           = tui_child_at,
    .child_place        = tui_child_place,
    .child_caption      = tui_child_caption,
    .child_drop         = tui_child_drop,
    .column_count       = tui_column_count,
    .tree               = tui_tree,
    .tree_clear         = tui_tree_clear,
    .tree_add           = tui_tree_add,
    .tree_depth         = tui_tree_depth,
    .tree_rows          = tui_tree_rows,
    .tree_get           = tui_tree_get,
    .tree_heading       = tui_tree_heading,
    .tree_selected      = tui_tree_selected,
    .tree_select        = tui_tree_select,
    .child_name         = tui_child_name,
    .tab_count          = tui_tab_count,
    .tab_caption        = tui_tab_caption,
    .cell_size          = tui_cell_size,
};

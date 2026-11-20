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
};

static GbUiInvokeFn tui_g_invoke = NULL;
static GbUiTickFn tui_g_tick = NULL;
static GbUiClosedFn tui_g_closed = NULL;
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
static int tui_dirty = 1;
/* the theme record's colours; TERM_DEFAULT where the terminal's own is kept */
static uint32_t tui_bg = TERM_DEFAULT, tui_fg = TERM_DEFAULT, tui_base = TERM_DEFAULT,
                tui_ctl = TERM_DEFAULT, tui_hi = TERM_DEFAULT, tui_faint = TERM_DEFAULT,
                tui_line = TERM_DEFAULT, tui_sel = TERM_DEFAULT, tui_on_sel = TERM_DEFAULT;

static const char *const tui_later[][2] = {
    { "table", "4" }, { "tree", "4" }, { "choice", "4" }, { "menu", "4" },
    { "popup", "4" }, { "item", "4" }, { "tabs", "4" }, { "tab", "4" },
    { "split", "4" }, { "scroll", "4" }, { "toolbar", "4" }, { "status", "4" },
    { "cells", "4" }, { "memo", "6" }, { "source", "6" },
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
    free(n->on_closed);
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
    return n->visible && n->enabled &&
           (tui_is(n, "field") || tui_is(n, "button") || tui_is(n, "check"));
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
    (void)cell;
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

static int tui_box(const TuiNode *n) {
    return tui_is(n, "row") || tui_is(n, "column") || tui_is(n, "form") ||
           tui_is(n, "group") || tui_is(n, "window");
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
    if (!n->visible) {
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
        if (!k->visible) {
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
        if (!k->visible) {
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
    if (!n->visible) {
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

static void tui_layout(TuiNode *root) {
    /* the top row is the window's title; the rest is its content */
    tui_arrange(root, 0, 1, tui_screen.columns, tui_screen.rows - 1);
}

/* ---- drawing ---------------------------------------------------------------- */

static TermStyle tui_style(uint32_t fg, uint32_t bg, unsigned attrs) {
    TermStyle s = { fg, bg, attrs };
    return s;
}

static void tui_put_clipped(int x, int y, int w, const char *text, TermStyle st) {
    if (w <= 0 || !text) {
        return;
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
    if (w <= 0) {
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
    if (!n->visible || n->w <= 0) {
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

/* The window on top: the last shown. */
static TuiNode *tui_top(void) {
    for (int i = tui_nroots - 1; i >= 0; i--) {
        if (tui_roots[i]->shown) {
            return tui_roots[i];
        }
    }
    return NULL;
}

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
    TuiNode *top = tui_top();
    term_screen_clear(&tui_screen, tui_style(tui_fg, tui_bg, 0));
    tui_screen.cursor_visible = 0;
    if (top) {
        tui_layout(top);
        const char *t = top->title ? top->title : (top->name ? top->name : "");
        TermStyle bar = tui_style(tui_on_sel, tui_sel, tui_sel == TERM_DEFAULT ? TERM_REVERSE : 0);
        tui_fill(0, 0, tui_screen.columns, 1, bar);
        tui_put_clipped(1, 0, tui_screen.columns - 2, t, bar);
        tui_draw(top);
    }
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

static void tui_fire(const char *handler) {
    if (!handler || !tui_g_invoke) {
        return;
    }
    tui_depth++;
    tui_g_invoke(handler);
    tui_depth--;
    tui_dirty = 1;
}

static int tui_handler_depth(void) {
    return tui_depth;
}

static int tui_handler_max(void) {
    return 16;
}

static void tui_collect_focusable(TuiNode *n, TuiNode **out, int *count, int max) {
    if (!n->visible) {
        return;
    }
    if (tui_focusable(n) && *count < max) {
        out[(*count)++] = n;
    }
    for (int i = 0; i < n->nkids; i++) {
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
    tui_node_free(root);
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
    TuiNode *top = tui_top();
    if (!top) {
        return 0;
    }
    if (tui_focus && !tui_within(tui_focus, top)) {
        tui_focus = NULL;
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
    if (!f || !tui_is(f, "field") || f->read_only || !f->enabled) {
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
    if (!n->visible || x < n->x || y < n->y || x >= n->x + n->w || y >= n->y + n->h) {
        return NULL;
    }
    for (int i = n->nkids - 1; i >= 0; i--) {
        TuiNode *h = tui_hit(n->kids[i], x, y);
        if (h) {
            return h;
        }
    }
    return n;
}

static void tui_mouse_event(const TermEvent *ev) {
    TuiNode *top = tui_top();
    if (!top || strcmp(ev->action, "press") != 0) {
        return;
    }
    TuiNode *h = tui_hit(top, ev->column - 1, ev->row - 1);
    if (!h) {
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
    double next_tick = tui_now_ms() + tick_ms;
    while (!tui_stop_asked && tui_top()) {
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
            while (!tui_stop_asked && tui_top() && term_decoder_next(&tui_decoder, flush, &ev)) {
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
        if (tui_now_ms() >= next_tick) {
            next_tick = tui_now_ms() + tick_ms;
            if (tui_g_tick && !tui_g_tick()) {
                break;
            }
        }
    }
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
          strcmp(word, "text") == 0 || strcmp(word, "stretch") == 0 || strcmp(word, "separator") == 0)) {
        tui_not_yet(word, err, err_size);
        return NULL;
    }
    if (!tui_box(p)) {
        TUI_ERR("that container cannot hold anything");
        return NULL;
    }
    if ((strcmp(word, "stretch") == 0 || strcmp(word, "separator") == 0) &&
        !(tui_is(p, "row") || tui_is(p, "column") || tui_is(p, "group") || tui_is(p, "window"))) {
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
    return n;
}

/* ---- properties ---------------------------------------------------------------- */

static int tui_say_no(const TuiNode *n, const char *word, char *err, size_t err_size) {
    return TUI_ERR("a `%s` has no `%s`", tui_is(n, "window") ? "window" : n->word, word);
}

static int tui_set_text(void *widget, const char *prop, const char *value, char *err, size_t err_size) {
    TuiNode *n = widget;
    tui_dirty = 1;
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
        return 1;
    }
    return tui_say_no(n, prop, err, err_size);
}

static int tui_set_number(void *widget, const char *prop, double value, char *err, size_t err_size) {
    TuiNode *n = widget;
    tui_dirty = 1;
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
    (void)values;
    (void)count;
    if (strcmp(prop, "shortcut") == 0) {
        return tui_not_yet("shortcut", err, err_size);
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

static int tui_span(void *widget, long across, long down, char *err, size_t err_size) {
    (void)widget; (void)across; (void)down;
    return tui_not_yet("cells", err, err_size);
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
    if (tui_box(n)) {
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
#define TUI_B(v) do { *kind = 'b'; *num = (v); return 1; } while (0)
#define TUI_N(v) do { *kind = 'n'; *num = (v); return 1; } while (0)
#define TUI_T(v) do { *kind = 't'; *text = copy_string((v) ? (v) : ""); return 1; } while (0)
    if (strcmp(prop, "visible") == 0) TUI_B(n->visible);
    if (strcmp(prop, "enabled") == 0) TUI_B(n->enabled);
    if (strcmp(prop, "bold") == 0) TUI_B(n->bold);
    if (strcmp(prop, "focused") == 0) TUI_B(tui_focus == n);
    if (strcmp(prop, "font_size") == 0) TUI_N(n->font_size);
    if (strcmp(prop, "tip") == 0) TUI_T(n->tip);
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
    } else if (tui_is(n, "check")) {
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
    }
    if (!slot) {
        return TUI_ERR("a `%s` has no `%s` event", tui_is(n, "window") ? "window" : n->word, signal);
    }
    tui_set(slot, handler);
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
        return TUI_ERR("a `%s` has no rows to click", n->word);
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
        return TUI_ERR("a `%s` has no rows to click", n->word);
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
    tui_dirty = 1;
    tui_render();
}

static int tui_close(void *window, char *err, size_t err_size) {
    (void)err;
    (void)err_size;
    tui_close_root(window);
    return 1;
}

static int tui_show_modal(void *window, int *accepted, char *err, size_t err_size) {
    (void)window; (void)accepted;
    return tui_not_yet("open_modal", err, err_size);
}

static int tui_answer_modal(void *window, int accept, char *err, size_t err_size) {
    (void)window; (void)accept;
    return tui_not_yet("open_modal", err, err_size);
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

static int tui_cell_of(void *window, const char *name, int *row, int *col, int *across, int *down) {
    (void)window; (void)name; (void)row; (void)col; (void)across; (void)down;
    return 0;
}

static int tui_pane_of(void *window, const char *name, char **kind, char **direction, int *panes,
                       int *index, int *resizable) {
    (void)window; (void)name; (void)kind; (void)direction; (void)panes; (void)index; (void)resizable;
    return 0;
}

static int tui_column_align(void *table_node, int col, const char *align, char *err, size_t err_size) {
    (void)table_node; (void)col; (void)align;
    return TUI_REFUSE("table");
}

static const char *tui_cell_alignment(void *window, const char *name, int row, int col) {
    (void)window; (void)name; (void)row; (void)col;
    return NULL;
}

static void *tui_menu(void *window, void *parent_menu, const char *title, char *err, size_t err_size) {
    (void)window; (void)parent_menu; (void)title;
    TUI_REFUSE("menu");
    return NULL;
}

static void *tui_menu_item(void *window, void *menu, const char *name, const char *label, char *err,
                           size_t err_size) {
    (void)window; (void)menu; (void)name; (void)label;
    TUI_REFUSE("item");
    return NULL;
}

static int tui_menu_separator(void *menu, char *err, size_t err_size) {
    (void)menu;
    return TUI_REFUSE("menu");
}

static void *tui_popup_menu(void *window, const char *name, char *err, size_t err_size) {
    (void)window; (void)name;
    TUI_REFUSE("popup");
    return NULL;
}

static int tui_menu_count(void *window) {
    (void)window;
    return 0;
}

static char *tui_menu_title(void *window, int index) {
    (void)window; (void)index;
    return NULL;
}

static int tui_menu_entry_count(void *window, int index) {
    (void)window; (void)index;
    return 0;
}

static char *tui_menu_entry_title(void *window, int index, int entry, int *is_separator) {
    (void)window; (void)index; (void)entry; (void)is_separator;
    return NULL;
}

static int tui_popup_entry_count(void *window, const char *name) {
    (void)window; (void)name;
    return -1;
}

static char *tui_popup_entry_title(void *window, const char *name, int entry, int *is_separator) {
    (void)window; (void)name; (void)entry; (void)is_separator;
    return NULL;
}

static int tui_popup(void *window, const char *name, const char *under, int *shown, char *err,
                     size_t err_size) {
    (void)window; (void)name; (void)under; (void)shown;
    return TUI_REFUSE("popup");
}

static int tui_which_tab(void) {
    return -1;
}

static int tui_copy(const char *text, long len, char *err, size_t err_size) {
    (void)text; (void)len;
    return TUI_REFUSE("copy");
}

static int tui_clipboard(char **text, long *len, char *err, size_t err_size) {
    (void)text; (void)len;
    return TUI_REFUSE("clipboard");
}

static int tui_wheel_event(int *dy, int *dx, int *ctrl, int *shift, int *alt) {
    (void)dy; (void)dx; (void)ctrl; (void)shift; (void)alt;
    return 0;
}

static int tui_wheel_exact(double *dy, double *dx) {
    (void)dy; (void)dx;
    return 0;
}

static int tui_swallow(char *err, size_t err_size) {
    return TUI_REFUSE("swallow");
}

static int tui_scroll(void *window, const char *name, double notches, int mods, int *claimed, char *err,
                      size_t err_size) {
    (void)window; (void)name; (void)notches; (void)mods; (void)claimed;
    return TUI_REFUSE("scroll");
}

static int tui_pane_sizes(void *window, const char *name, int *out, int *count, char *err, size_t err_size) {
    (void)window; (void)name; (void)out; (void)count;
    return TUI_REFUSE("split");
}

static int tui_resize_panes(void *window, const char *name, const int *sizes, int count, char *err,
                            size_t err_size) {
    (void)window; (void)name; (void)sizes; (void)count;
    return TUI_REFUSE("split");
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

static int tui_scrolled(void *window, const char *name, double *at, char *err, size_t err_size) {
    (void)window; (void)name; (void)at;
    return TUI_REFUSE("scroll");
}

static int tui_scroll_to(void *window, const char *name, double at, char *err, size_t err_size) {
    (void)window; (void)name; (void)at;
    return TUI_REFUSE("scroll");
}

static void *tui_table(void *parent, const char *name, const char *const *headers, int columns, char *err,
                       size_t err_size) {
    (void)parent; (void)name; (void)headers; (void)columns;
    TUI_REFUSE("table");
    return NULL;
}

static int tui_choice_clear(void *window, const char *name, char *err, size_t err_size) {
    (void)window; (void)name;
    return TUI_REFUSE("choice");
}

static int tui_choice_add(void *window, const char *name, const char *item, char *err, size_t err_size) {
    (void)window; (void)name; (void)item;
    return TUI_REFUSE("choice");
}

static void *tui_virtual_table(void *parent, const char *name, const char *const *headers, int ncols,
                               const char *cell_fn, char *err, size_t err_size) {
    (void)parent; (void)name; (void)headers; (void)ncols; (void)cell_fn;
    TUI_REFUSE("table");
    return NULL;
}

static int tui_virtual_rows(void *window, const char *name, int rows, char *err, size_t err_size) {
    (void)window; (void)name; (void)rows;
    return TUI_REFUSE("table");
}

static int tui_virtual_row_count(void *window, const char *name, int *rows, char *err, size_t err_size) {
    (void)window; (void)name; (void)rows;
    return TUI_REFUSE("table");
}

static int tui_table_size(void *window, const char *name, int rows, char *err, size_t err_size) {
    (void)window; (void)name; (void)rows;
    return TUI_REFUSE("table");
}

static int tui_table_set(void *window, const char *name, int row, int col, const char *text, char *err,
                         size_t err_size) {
    (void)window; (void)name; (void)row; (void)col; (void)text;
    return TUI_REFUSE("table");
}

static char *tui_table_get(void *window, const char *name, int row, int col) {
    (void)window; (void)name; (void)row; (void)col;
    return NULL;
}

static int tui_table_rows(void *window, const char *name) {
    (void)window; (void)name;
    return -1;
}

static int tui_table_selected(void *window, const char *name) {
    (void)window; (void)name;
    return -1;
}

static char *tui_table_heading(void *window, const char *name, int col) {
    (void)window; (void)name; (void)col;
    return NULL;
}

static int tui_table_select(void *window, const char *name, int row, char *err, size_t err_size) {
    (void)window; (void)name; (void)row;
    return TUI_REFUSE("table");
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

static int tui_dialog_depth(void) {
    return 0;
}

static int tui_inform(void *window, const char *text, char *err, size_t err_size) {
    (void)window; (void)text;
    return TUI_REFUSE("inform");
}

static int tui_confirm(void *window, const char *text, int *answer, char *err, size_t err_size) {
    (void)window; (void)text; (void)answer;
    return TUI_REFUSE("confirm");
}

static int tui_answer(int value, char *err, size_t err_size) {
    (void)value;
    return TUI_REFUSE("answer");
}

static int tui_dialog_info(char **kind, char **title, char **text, char **folder, char **filter) {
    (void)kind; (void)title; (void)text; (void)folder; (void)filter;
    return 0;
}

static char *tui_choose(void *window, int kind, const char *title, const char *folder, const char *name,
                        const char *filter, int *cancelled, char *err, size_t err_size) {
    (void)window; (void)kind; (void)title; (void)folder; (void)name; (void)filter; (void)cancelled;
    TUI_ERR("the tui backend has no `file_dialogs` group yet");
    return NULL;
}

static int tui_answer_path(const char *path, char *err, size_t err_size) {
    (void)path;
    return TUI_ERR("the tui backend has no `file_dialogs` group yet");
}

static int tui_column_count(void *window, const char *name) {
    (void)window; (void)name;
    return -1;
}

static void *tui_tree(void *parent, const char *name, const char *const *headers, int columns, char *err,
                      size_t err_size) {
    (void)parent; (void)name; (void)headers; (void)columns;
    TUI_REFUSE("tree");
    return NULL;
}

static int tui_tree_clear(void *window, const char *name) {
    (void)window; (void)name;
    return 0;
}

static int tui_tree_add(void *window, const char *name, int depth, const char *const *cells, int columns) {
    (void)window; (void)name; (void)depth; (void)cells; (void)columns;
    return 0;
}

static int tui_tree_depth(void *window, const char *name, int row) {
    (void)window; (void)name; (void)row;
    return -2;
}

static int tui_tree_rows(void *window, const char *name) {
    (void)window; (void)name;
    return -1;
}

static char *tui_tree_get(void *window, const char *name, int row, int col) {
    (void)window; (void)name; (void)row; (void)col;
    return NULL;
}

static char *tui_tree_heading(void *window, const char *name, int col) {
    (void)window; (void)name; (void)col;
    return NULL;
}

static int tui_tree_selected(void *window, const char *name) {
    (void)window; (void)name;
    return -1;
}

static int tui_tree_select(void *window, const char *name, int row, char *err, size_t err_size) {
    (void)window; (void)name; (void)row;
    return TUI_REFUSE("tree");
}

static int tui_tab_count(void *window, const char *name) {
    (void)window; (void)name;
    return -1;
}

static char *tui_tab_caption(void *window, const char *name, int index) {
    (void)window; (void)name; (void)index;
    return NULL;
}

static const GbUiBackend ui_tui_backend = {
    .name = "tui",
    /* NO GROUP YET: `file_dialogs` comes with its chooser (increment 5),
     * `highlighting` with increment 7 (§3.2) */
    .groups = "",
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

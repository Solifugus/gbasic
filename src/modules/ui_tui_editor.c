/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * THE TERMINAL BACKEND'S INCREMENT 6 (docs/tui_design.md §7): `memo` and
 * `source`, a multi-line editor in cells. `#include`d at the end of
 * src/modules/ui_tui.c, before ui_tui_parts.c, whose hooks call the `tuie_`
 * ones here.
 *
 * ONE EDITOR, TWO SETUPS, as on Qt (both are a QPlainTextEdit there): a
 * `memo` wraps at the box's width and its tab is 8 cells; a `source` does not
 * wrap, its tab is 4, and it may carry a margin of line numbers and marks.
 *
 * THE TEXT IS THE NODE'S `text`, one string with a line feed between lines,
 * so `ui.read`, `ui.write` and a form's binding reach it as they reach a
 * field's. The caret is a byte offset, as a field's is; the selection is the
 * caret and an anchor. Every position a program gives or reads is a 1-based
 * line and a 1-based column counted in codepoints, through one resolver, with
 * Qt's rules: a line past the end is refused, a column past its line clamps.
 *
 * UNDO KEEPS WHOLE TEXTS, which is crude and cannot be wrong: a terminal's
 * document is a person's typing, and a copy per step is cheaper to trust
 * than a diff. Typing joins the step before it, as Qt's does; a move, a
 * delete or anything a program does starts a new one. */

enum { TUIE_MARKS = 5, TUIE_UNDO_MAX = 400 };

typedef struct {
    char *text;
    size_t caret, anchor;
} TuieState;

typedef struct TuiEdit {
    int source;
    size_t anchor;          /* the selection's other end; the caret when none */
    int wrap, whitespace;
    int tab;                /* cells */
    int top, left;          /* the first row shown; the first cell shown when not wrapping */
    int goal;               /* the cell Up and Down keep to, -1 none */
    int reveal;             /* bring the caret into view at the next frame */
    int numbers, marks;     /* the margin */
    int *mk[TUIE_MARKS];
    int nmk[TUIE_MARKS];
    size_t *hl;             /* marked ranges, two byte offsets each */
    size_t nhl;
    TuieState *undo, *redo;
    int nundo, nredo, capundo, capredo;
    int typing;             /* the last edit was typing, which the next may join */
    char *on_gutter;
    /* where the last frame put the text and the margin */
    int tx, ty, tw, th, gx, gw;
} TuiEdit;

typedef struct {
    size_t start, end;      /* bytes; `end` is before the line feed */
    int line;               /* 0-based */
    int last;               /* the last row of its line */
} TuieRow;

static TuiNode *tuie_drag = NULL;
static int tuie_gclick_live = 0, tuie_gclick_line = 0;
static int tuie_painted_lines = 0, tuie_painted_marks = 0, tuie_painted_first = 0;

static int tuie_is(const TuiNode *n) {
    return n && n->ed != NULL;
}

static const char *tuie_text(TuiNode *n) {
    if (!n->text) {
        n->text = copy_string("");
    }
    return n->text;
}

static int tuie_editable(const TuiNode *n) {
    return !n->read_only && !n->display_only && n->enabled;
}

static void tuie_free(TuiNode *n) {
    TuiEdit *e = n->ed;
    if (!e) {
        return;
    }
    if (tuie_drag == n) {
        tuie_drag = NULL;
    }
    for (int k = 0; k < TUIE_MARKS; k++) {
        free(e->mk[k]);
    }
    free(e->hl);
    for (int i = 0; i < e->nundo; i++) {
        free(e->undo[i].text);
    }
    for (int i = 0; i < e->nredo; i++) {
        free(e->redo[i].text);
    }
    free(e->undo);
    free(e->redo);
    free(e->on_gutter);
    free(e);
    n->ed = NULL;
}

/* ---- codepoints, lines and positions ---------------------------------------------- */

static size_t tuie_cp(const char *t, size_t i, size_t end) {
    size_t k = 1;
    while (i + k < end && ((unsigned char)t[i + k] & 0xC0) == 0x80) {
        k++;
    }
    return k;
}

/* The cells one codepoint takes at cell `col` of its row: a tab runs to the
 * next stop, a control character or a broken byte shows as one cell. */
static int tuie_cells(const char *t, size_t i, size_t k, int col, int tab) {
    if (t[i] == '\t') {
        return tab - col % tab;
    }
    long w = term_text_width(t + i, k, NULL);
    return w < 0 ? 1 : (int)w;
}

static int tuie_line_count(const char *t) {
    int n = 1;
    for (; *t; t++) {
        n += *t == '\n';
    }
    return n;
}

/* The byte where 0-based line `line` starts, or -1 past the end. */
static long tuie_line_start(const char *t, int line) {
    size_t i = 0;
    for (int l = 0; l < line; l++) {
        const char *nl = strchr(t + i, '\n');
        if (!nl) {
            return -1;
        }
        i = (size_t)(nl - t) + 1;
    }
    return (long)i;
}

/* A 1-BASED (line, column) TO A BYTE: the one resolver every verb shares,
 * with Qt's sentences (gb_qt's `text_position`). */
static int tuie_position(TuiNode *n, int line, int column, size_t *at, char *err, size_t err_size) {
    const char *t = tuie_text(n);
    if (line < 1 || column < 1) {
        return TUI_ERR("a line and a column both start at 1 -- the same numbering a "
                       "diagnostic and `ui.mark` use");
    }
    long s = tuie_line_start(t, line - 1);
    if (s < 0) {
        return TUI_ERR("that line is past the end -- this editor holds %d", tuie_line_count(t));
    }
    size_t i = (size_t)s, end = i + strcspn(t + i, "\n");
    for (int c = 1; c < column && i < end; c++) {
        i += tuie_cp(t, i, end);
    }
    *at = i;
    return 1;
}

static void tuie_line_column(TuiNode *n, size_t at, int *line, int *column) {
    const char *t = tuie_text(n);
    int l = 1, c = 1;
    for (size_t i = 0; i < at && t[i]; i++) {
        if (t[i] == '\n') {
            l++;
            c = 1;
        } else if (((unsigned char)t[i] & 0xC0) != 0x80) {
            c++;
        }
    }
    *line = l;
    *column = c;
}

/* ---- rows: the lines as the box shows them ------------------------------------------ */

static void tuie_push_row(TuieRow **rows, int *count, int *cap, size_t s, size_t e, int line, int last) {
    if (*count == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *rows = realloc(*rows, sizeof(TuieRow) * (size_t)*cap);
        if (!*rows) {
            abort();
        }
    }
    (*rows)[*count] = (TuieRow){ s, e, line, last };
    (*count)++;
}

/* Every row, malloc'd. Not wrapping, a row is a line; wrapping, a line is cut
 * after the last space that fits, or where it stops fitting when no space
 * does, as Qt's WidgetWidth wrap cuts at words. */
static TuieRow *tuie_rows(TuiNode *n, int width, int *count) {
    TuiEdit *e = n->ed;
    const char *t = tuie_text(n);
    TuieRow *rows = NULL;
    int cap = 0, line = 0;
    *count = 0;
    if (width < 1) {
        width = 1;
    }
    size_t s = 0;
    for (;;) {
        size_t end = s + strcspn(t + s, "\n");
        if (!e->wrap || s == end) {
            tuie_push_row(&rows, count, &cap, s, end, line, 1);
        } else {
            size_t r = s;
            for (;;) {
                int col = 0;
                size_t i = r, space_end = 0;
                while (i < end) {
                    size_t k = tuie_cp(t, i, end);
                    int cw = tuie_cells(t, i, k, col, e->tab);
                    if (col > 0 && col + cw > width) {
                        break;
                    }
                    col += cw;
                    i += k;
                    if (t[i - k] == ' ' || t[i - k] == '\t') {
                        space_end = i;
                    }
                }
                if (i >= end) {
                    tuie_push_row(&rows, count, &cap, r, end, line, 1);
                    break;
                }
                size_t cut = space_end > r ? space_end : i;
                tuie_push_row(&rows, count, &cap, r, cut, line, 0);
                r = cut;
            }
        }
        if (!t[end]) {
            break;
        }
        s = end + 1;
        line++;
    }
    return rows;
}

/* The row the caret is on: at a cut it belongs to the row after, as Qt's. */
static int tuie_row_of(const TuieRow *rows, int count, size_t at) {
    for (int i = 0; i < count; i++) {
        if (at >= rows[i].start && (at < rows[i].end || (at == rows[i].end && rows[i].last))) {
            return i;
        }
    }
    return count - 1;
}

static int tuie_col_of(TuiNode *n, const TuieRow *row, size_t at) {
    const char *t = tuie_text(n);
    int col = 0;
    for (size_t i = row->start; i < at && i < row->end;) {
        size_t k = tuie_cp(t, i, row->end);
        col += tuie_cells(t, i, k, col, n->ed->tab);
        i += k;
    }
    return col;
}

/* The byte at cell `want` of a row: the codepoint covering it, or the row's
 * end. */
static size_t tuie_at_col(TuiNode *n, const TuieRow *row, int want) {
    const char *t = tuie_text(n);
    int col = 0;
    size_t i = row->start;
    while (i < row->end) {
        size_t k = tuie_cp(t, i, row->end);
        int cw = tuie_cells(t, i, k, col, n->ed->tab);
        if (col + (cw + 1) / 2 > want) {
            break;
        }
        col += cw;
        i += k;
    }
    /* a cut row's end is the next row's start: stay on this one */
    if (i == row->end && !row->last && i > row->start) {
        i -= 1;
        while (i > row->start && ((unsigned char)t[i] & 0xC0) == 0x80) {
            i--;
        }
    }
    return i;
}

static int tuie_text_width(TuiNode *n) {
    TuiEdit *e = n->ed;
    if (e->tw > 0) {
        return e->tw;
    }
    return n->w > 2 ? n->w - 2 : 1;
}

/* ---- editing ------------------------------------------------------------------------ */

static void tuie_stack_push(TuieState **stack, int *count, int *cap, TuiNode *n) {
    if (*count == TUIE_UNDO_MAX) {
        free((*stack)[0].text);
        memmove(*stack, *stack + 1, sizeof(TuieState) * (size_t)(*count - 1));
        (*count)--;
    }
    if (*count == *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *stack = realloc(*stack, sizeof(TuieState) * (size_t)*cap);
        if (!*stack) {
            abort();
        }
    }
    (*stack)[*count] = (TuieState){ copy_string(tuie_text(n)), n->caret, n->ed->anchor };
    (*count)++;
}

static void tuie_forget_history(TuiNode *n) {
    TuiEdit *e = n->ed;
    for (int i = 0; i < e->nundo; i++) {
        free(e->undo[i].text);
    }
    for (int i = 0; i < e->nredo; i++) {
        free(e->redo[i].text);
    }
    e->nundo = e->nredo = 0;
    e->typing = 0;
}

/* A position after bytes [from, to) became `len` bytes: inside the range it
 * goes to its start, after it moves by the difference. */
static size_t tuie_moved(size_t p, size_t from, size_t to, size_t len) {
    if (p >= to) {
        return p - (to - from) + len;
    }
    return p > from ? from : p;
}

/* Bytes [from, to) become `ins`. No history, no event: the callers decide. */
static void tuie_splice(TuiNode *n, size_t from, size_t to, const char *ins, size_t len) {
    TuiEdit *e = n->ed;
    const char *t = tuie_text(n);
    size_t old = strlen(t);
    char *grown = malloc(old - (to - from) + len + 1);
    if (!grown) {
        abort();
    }
    memcpy(grown, t, from);
    memcpy(grown + from, ins, len);
    memcpy(grown + from + len, t + to, old - to + 1);
    free(n->text);
    n->text = grown;
    for (size_t i = 0; i < 2 * e->nhl; i++) {
        e->hl[i] = tuie_moved(e->hl[i], from, to, len);
    }
    n->caret = tuie_moved(n->caret, from, to, len);
    e->anchor = tuie_moved(e->anchor, from, to, len);
    tui_dirty = 1;
}

static void tuie_selection_bytes(TuiNode *n, size_t *from, size_t *to) {
    size_t a = n->ed->anchor, c = n->caret;
    *from = a < c ? a : c;
    *to = a < c ? c : a;
}

/* A person's edit: the selection, or [from, to), becomes `ins`; one undo
 * step unless it is typing after typing; `changed` fires. */
static void tuie_edit(TuiNode *n, size_t from, size_t to, const char *ins, size_t len, int typing) {
    TuiEdit *e = n->ed;
    if (!(typing && e->typing)) {
        tuie_stack_push(&e->undo, &e->nundo, &e->capundo, n);
    }
    for (int i = 0; i < e->nredo; i++) {
        free(e->redo[i].text);
    }
    e->nredo = 0;
    e->typing = typing;
    tuie_splice(n, from, to, ins, len);
    n->caret = e->anchor = from + len;
    e->reveal = 1;
    e->goal = -1;
    tui_field_changed(n);
}

static void tuie_type(TuiNode *n, const char *ins, size_t len, int typing) {
    size_t from, to;
    tuie_selection_bytes(n, &from, &to);
    tuie_edit(n, from, to, ins, len, typing && from == to);
}

/* Undo (1) or redo (0): the other stack takes where we are. */
static int tuie_history(TuiNode *n, int undo) {
    TuiEdit *e = n->ed;
    TuieState **from = undo ? &e->undo : &e->redo;
    int *nfrom = undo ? &e->nundo : &e->nredo;
    if (*nfrom == 0) {
        return 1;
    }
    if (undo) {
        tuie_stack_push(&e->redo, &e->nredo, &e->capredo, n);
    } else {
        tuie_stack_push(&e->undo, &e->nundo, &e->capundo, n);
    }
    TuieState s = (*from)[--(*nfrom)];
    free(n->text);
    n->text = s.text;
    n->caret = s.caret;
    e->anchor = s.anchor;
    e->typing = 0;
    e->reveal = 1;
    e->goal = -1;
    e->nhl = 0;
    tui_field_changed(n);
    return 1;
}

/* The whole text set from outside (ui.write, ui.set, a form): the caret to
 * the start and the history gone, as Qt's setPlainText does. */
static void tuie_set_text(TuiNode *n, const char *text) {
    TuiEdit *e = n->ed;
    tui_set(&n->text, text ? text : "");
    n->caret = e->anchor = 0;
    e->top = e->left = 0;
    e->goal = -1;
    e->nhl = 0;
    tuie_forget_history(n);
    tui_field_changed(n);
}

/* ---- building, measuring, properties ------------------------------------------------ */

static void tuie_part(TuiNode *p, const char *word, TuiNode *n) {
    TuiEdit *e = calloc(1, sizeof *e);
    if (!e) {
        abort();
    }
    n->ed = e;
    e->source = strcmp(word, "source") == 0;
    e->wrap = !e->source;
    e->tab = e->source ? 4 : 8;
    e->goal = -1;
    /* in a form the label is the row's caption; elsewhere it is the first
     * text, as on Qt (`memo log( label: "You are in the cellar." )`) */
    if (!tui_is(p, "form") && n->label) {
        n->text = n->label;
        n->label = NULL;
    }
    tuie_text(n);
}

static void tuie_measure(TuiNode *n, int *w, int *h) {
    (void)n;
    *w = 32;
    *h = 8;
}

/* -1 when the property is not the editor's own */
static int tuie_set_text_prop(TuiNode *n, const char *prop, const char *value) {
    if (strcmp(prop, "text") == 0) {
        tuie_set_text(n, value);
        return 1;
    }
    if (strcmp(prop, "hint") == 0) {
        tui_set(&n->hint, value);
        return 1;
    }
    return -1;
}

static int tuie_set_bool(TuiNode *n, const char *prop, int value) {
    TuiEdit *e = n->ed;
    if (strcmp(prop, "read_only") == 0) { n->read_only = value; return 1; }
    if (strcmp(prop, "wrap") == 0) { e->wrap = value; e->left = 0; return 1; }
    if (strcmp(prop, "whitespace") == 0) { e->whitespace = value; return 1; }
    return -1;
}

static int tuie_set_number(TuiNode *n, const char *prop, double value) {
    if (strcmp(prop, "tab_width") == 0) {
        int cells = tui_cells_across(value);
        n->ed->tab = cells < 1 ? 1 : cells;
        return 1;
    }
    return -1;
}

static int tuie_get(TuiNode *n, const char *prop, char *kind, double *num, char **text) {
    TuiEdit *e = n->ed;
#define TUIE_B(v) do { *kind = 'b'; *num = (v); return 1; } while (0)
#define TUIE_N(v) do { *kind = 'n'; *num = (v); return 1; } while (0)
#define TUIE_T(v) do { *kind = 't'; *text = copy_string((v) ? (v) : ""); return 1; } while (0)
    if (strcmp(prop, "text") == 0) TUIE_T(tuie_text(n));
    if (strcmp(prop, "hint") == 0) TUIE_T(n->hint);
    if (strcmp(prop, "read_only") == 0) TUIE_B(n->read_only);
    if (strcmp(prop, "wrap") == 0) TUIE_B(e->wrap);
    if (strcmp(prop, "whitespace") == 0) TUIE_B(e->whitespace);
    if (strcmp(prop, "tab_width") == 0) TUIE_N(e->tab * 8);
#undef TUIE_B
#undef TUIE_N
#undef TUIE_T
    return 0;
}

/* ---- drawing ------------------------------------------------------------------------- */

static int tuie_digits(int v) {
    int d = 1;
    while (v >= 10) {
        v /= 10;
        d++;
    }
    return d;
}

static int tuie_marked(TuiEdit *e, int kind, int line) {
    for (int i = 0; i < e->nmk[kind]; i++) {
        if (e->mk[kind][i] == line) {
            return 1;
        }
    }
    return 0;
}

/* One glyph a line, the most urgent: a breakpoint, an error, a warning, a
 * note, a section. */
static const char *tuie_mark_glyph(TuiEdit *e, int line) {
    static const int order[] = { 1, 2, 3, 4, 0 };
    static const char *const glyph[] = { "\xC2\xA7", "\xE2\x97\x8F", "E", "W", "i" };
    for (int i = 0; i < 5; i++) {
        if (tuie_marked(e, order[i], line)) {
            return glyph[order[i]];
        }
    }
    return NULL;
}

static int tuie_in_ranges(const TuiEdit *e, size_t at) {
    for (size_t i = 0; i < e->nhl; i++) {
        if (at >= e->hl[2 * i] && at < e->hl[2 * i + 1]) {
            return 1;
        }
    }
    return 0;
}

static int tuie_gutter_width(TuiNode *n) {
    TuiEdit *e = n->ed;
    int gw = 0;
    if (e->marks) {
        gw += 1;
    }
    if (e->numbers) {
        gw += tuie_digits(tuie_line_count(tuie_text(n)));
    }
    return gw ? gw + 1 : 0;
}

static void tuie_draw(TuiNode *n) {
    TuiEdit *e = n->ed;
    const char *t = tuie_text(n);
    int focused = n == tui_focus;
    unsigned dim = n->enabled ? 0 : TERM_DIM;
    int framed = n->w >= 3 && n->h >= 3;
    int ix = n->x, iy = n->y, iw = n->w, ih = n->h;
    if (framed) {
        TermStyle st = focused ? tui_style(tui_fg, tui_bg, TERM_BOLD)
                               : tui_style(tui_line, tui_bg, tui_line == TERM_DEFAULT ? TERM_DIM : 0);
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
        ix++; iy++; iw -= 2; ih -= 2;
    }
    int gw = tuie_gutter_width(n);
    if (gw >= iw) {
        gw = 0;
    }
    e->gx = ix;
    e->gw = gw;
    e->tx = ix + gw;
    e->ty = iy;
    e->tw = iw - gw;
    e->th = ih;
    if (e->tw < 1 || e->th < 1) {
        return;
    }
    TermStyle box = tui_style(tui_fg, tui_base, dim);
    TermStyle sel = tui_style(tui_on_sel, tui_sel, tui_sel == TERM_DEFAULT ? TERM_REVERSE : 0);
    TermStyle hi = tui_hi == TERM_DEFAULT ? tui_style(tui_fg, tui_base, TERM_UNDERLINE | dim)
                                          : tui_style(tui_fg, tui_hi, dim);
    TermStyle faint = tui_style(tui_faint, tui_base, tui_faint == TERM_DEFAULT ? TERM_DIM : 0);
    TermStyle margin = tui_style(tui_faint, tui_bg, tui_faint == TERM_DEFAULT ? TERM_DIM : 0);
    tui_fill(ix, iy, iw, ih, box);
    if (gw) {
        tui_fill(ix, iy, gw, ih, margin);
    }
    int nr = 0;
    TuieRow *rows = tuie_rows(n, e->tw, &nr);
    int crow = tuie_row_of(rows, nr, n->caret);
    int ccol = tuie_col_of(n, &rows[crow], n->caret);
    if (e->reveal) {
        if (crow < e->top) {
            e->top = crow;
        } else if (crow >= e->top + e->th) {
            e->top = crow - e->th + 1;
        }
        if (!e->wrap) {
            if (ccol < e->left) {
                e->left = ccol;
            } else if (ccol >= e->left + e->tw) {
                e->left = ccol - e->tw + 1;
            }
        }
        e->reveal = 0;
    }
    if (e->top > nr - e->th) {
        e->top = nr - e->th;
    }
    if (e->top < 0) {
        e->top = 0;
    }
    if (e->wrap) {
        e->left = 0;
    }
    size_t s0, s1;
    tuie_selection_bytes(n, &s0, &s1);
    int painted_lines = 0, painted_marks = 0, painted_first = 0;
    int nlines = tuie_line_count(t);
    for (int r = 0; r < e->th; r++) {
        int idx = e->top + r;
        if (idx >= nr) {
            break;
        }
        const TuieRow *row = &rows[idx];
        int y = e->ty + r;
        int first_of_line = idx == 0 || rows[idx - 1].line != row->line;
        if (gw && first_of_line) {
            int at = ix;
            if (e->marks) {
                const char *g = tuie_mark_glyph(e, row->line + 1);
                if (g) {
                    tui_put_clipped(at, y, 1, g, tui_style(tui_fg, tui_bg, TERM_BOLD));
                    painted_marks++;
                }
                at++;
            }
            if (e->numbers) {
                char num[32];
                int d = tuie_digits(nlines);
                snprintf(num, sizeof num, "%*d", d > 12 ? 12 : d, row->line + 1);
                tui_put_clipped(at, y, d, num, margin);
                if (!painted_lines) {
                    painted_first = row->line + 1;
                }
                painted_lines++;
            }
        }
        int col = 0;
        for (size_t i = row->start; i < row->end;) {
            size_t k = tuie_cp(t, i, row->end);
            int cw = tuie_cells(t, i, k, col, e->tab);
            int sc = col - e->left;
            if (sc >= e->tw) {
                break;
            }
            if (sc >= 0 && sc + cw <= e->tw) {
                TermStyle st = (i >= s0 && i < s1) ? sel : tuie_in_ranges(e, i) ? hi : box;
                if (t[i] == '\t') {
                    tui_fill(e->tx + sc, y, cw, 1, st);
                    if (e->whitespace) {
                        tui_put_clipped(e->tx + sc, y, 1, "\xE2\x86\x92", tui_style(tui_faint, st.bg,
                                        (tui_faint == TERM_DEFAULT ? TERM_DIM : 0) | (st.attrs & TERM_REVERSE)));
                    }
                } else if (t[i] == ' ' && e->whitespace) {
                    tui_put_clipped(e->tx + sc, y, 1, "\xC2\xB7", tui_style(tui_faint, st.bg,
                                    (tui_faint == TERM_DEFAULT ? TERM_DIM : 0) | (st.attrs & TERM_REVERSE)));
                } else if (term_text_width(t + i, k, NULL) < 0) {
                    tui_put_clipped(e->tx + sc, y, 1, "?", tui_style(st.fg, st.bg, st.attrs | TERM_DIM));
                } else {
                    char one[8];
                    memcpy(one, t + i, k < sizeof one ? k : sizeof one - 1);
                    one[k < sizeof one ? k : sizeof one - 1] = 0;
                    tui_put_clipped(e->tx + sc, y, cw, one, st);
                }
            }
            col += cw;
            i += k;
        }
        /* the line's end: shaded when its line feed is selected, a pilcrow
         * when white space is shown */
        int sc = col - e->left;
        if (row->last && t[row->end] == '\n' && sc >= 0 && sc < e->tw) {
            int selected = row->end >= s0 && row->end < s1;
            if (e->whitespace) {
                tui_put_clipped(e->tx + sc, y, 1, "\xC2\xB6", selected ? sel : faint);
            } else if (selected) {
                tui_fill(e->tx + sc, y, 1, 1, sel);
            }
        }
    }
    if (!*t && n->hint) {
        tui_put_clipped(e->tx, e->ty, e->tw, n->hint, faint);
    }
    /* how far down: a thumb on the right side of the frame */
    if (framed && nr > e->th) {
        int len = e->th * e->th / nr;
        if (len < 1) {
            len = 1;
        }
        int at = (int)((long)e->top * e->th / nr);
        if (at + len > e->th) {
            at = e->th - len;
        }
        TermStyle st = tui_style(tui_line, tui_bg, 0);
        for (int r = 0; r < len; r++) {
            tui_put_clipped(n->x + n->w - 1, e->ty + at + r, 1, "\xE2\x94\x83", st);
        }
    }
    if (focused) {
        int sc = ccol - e->left, sr = crow - e->top;
        if (sc >= 0 && sc < e->tw && sr >= 0 && sr < e->th) {
            tui_screen.cursor_column = e->tx + 1 + sc;
            tui_screen.cursor_row = e->ty + 1 + sr;
            tui_screen.cursor_visible = 1;
        }
    }
    if (e->source && gw) {
        tuie_painted_lines = painted_lines;
        tuie_painted_marks = painted_marks;
        tuie_painted_first = painted_first;
    }
    free(rows);
}

/* ---- keys, the mouse, a paste ------------------------------------------------------------ */

static int tuie_word_byte(unsigned char c) {
    return c >= 0x80 || isalnum(c) || c == '_';
}

static size_t tuie_prev(const char *t, size_t at) {
    if (at == 0) {
        return 0;
    }
    do {
        at--;
    } while (at > 0 && ((unsigned char)t[at] & 0xC0) == 0x80);
    return at;
}

static size_t tuie_next(const char *t, size_t at) {
    size_t len = strlen(t);
    if (at >= len) {
        return len;
    }
    return at + tuie_cp(t, at, len);
}

static size_t tuie_word_left(const char *t, size_t at) {
    while (at > 0 && !tuie_word_byte((unsigned char)t[at - 1])) {
        at--;
    }
    while (at > 0 && tuie_word_byte((unsigned char)t[at - 1])) {
        at--;
    }
    return at;
}

static size_t tuie_word_right(const char *t, size_t at) {
    size_t len = strlen(t);
    while (at < len && tuie_word_byte((unsigned char)t[at])) {
        at++;
    }
    while (at < len && !tuie_word_byte((unsigned char)t[at])) {
        at++;
    }
    return at;
}

static int tuie_key(TuiNode *n, const TermEvent *ev) {
    TuiEdit *e = n->ed;
    const char *t = tuie_text(n);
    const char *k = ev->key;
    unsigned m = ev->mods;
    int shift = (m & TERM_MOD_SHIFT) != 0, ctrl = (m & TERM_MOD_CTRL) != 0;
    size_t len = strlen(t);
    if (n->caret > len) n->caret = len;
    if (e->anchor > len) e->anchor = len;
    size_t s0, s1;
    tuie_selection_bytes(n, &s0, &s1);
    int has_sel = s0 != s1;

    /* moving: the caret goes to `to`, and Shift keeps the anchor */
    long to = -1;
    int keep_goal = 0;
    if (strcmp(k, "left") == 0 && !(m & TERM_MOD_ALT)) {
        to = (long)(ctrl ? tuie_word_left(t, n->caret) : (!shift && has_sel) ? s0 : tuie_prev(t, n->caret));
    } else if (strcmp(k, "right") == 0 && !(m & TERM_MOD_ALT)) {
        to = (long)(ctrl ? tuie_word_right(t, n->caret) : (!shift && has_sel) ? s1 : tuie_next(t, n->caret));
    } else if (strcmp(k, "up") == 0 || strcmp(k, "down") == 0 || strcmp(k, "page_up") == 0 ||
               strcmp(k, "page_down") == 0 || strcmp(k, "home") == 0 || strcmp(k, "end") == 0) {
        if (ctrl && strcmp(k, "home") == 0) {
            to = 0;
        } else if (ctrl && strcmp(k, "end") == 0) {
            to = (long)len;
        } else if (!ctrl || strcmp(k, "up") == 0 || strcmp(k, "down") == 0) {
            int nr = 0;
            TuieRow *rows = tuie_rows(n, tuie_text_width(n), &nr);
            int r = tuie_row_of(rows, nr, n->caret);
            if (ctrl) {
                /* Ctrl+Up and Ctrl+Down scroll a row, leaving the caret */
                e->top += k[0] == 'u' ? -1 : 1;
                if (e->top < 0) e->top = 0;
                free(rows);
                tui_dirty = 1;
                return 1;
            }
            if (strcmp(k, "home") == 0) {
                to = (long)rows[r].start;
            } else if (strcmp(k, "end") == 0) {
                size_t at = rows[r].end;
                if (!rows[r].last && at > rows[r].start) {
                    at = tuie_prev(t, at);
                }
                to = (long)at;
            } else {
                int page = e->th > 1 ? e->th - 1 : 1;
                int by = strcmp(k, "up") == 0 ? -1 : strcmp(k, "down") == 0 ? 1
                       : strcmp(k, "page_up") == 0 ? -page : page;
                int goal = e->goal >= 0 ? e->goal : tuie_col_of(n, &rows[r], n->caret);
                int target = r + by;
                if (target < 0) {
                    to = by == -1 ? (long)n->caret : 0;
                    target = -1;
                } else if (target >= nr) {
                    to = by == 1 ? (long)n->caret : (long)len;
                    target = -1;
                }
                if (target >= 0) {
                    to = (long)tuie_at_col(n, &rows[target], goal);
                    if (k[0] == 'p') {
                        e->top += by;
                        if (e->top < 0) e->top = 0;
                    }
                }
                e->goal = goal;
                keep_goal = 1;
            }
            free(rows);
        }
    }
    if (to >= 0) {
        n->caret = (size_t)to;
        if (!shift) {
            e->anchor = n->caret;
        }
        if (!keep_goal) {
            e->goal = -1;
        }
        e->typing = 0;
        e->reveal = 1;
        tui_dirty = 1;
        return 1;
    }
    if (ctrl && !shift && strcmp(k, "a") == 0) {
        e->anchor = 0;
        n->caret = len;
        e->reveal = 1;
        tui_dirty = 1;
        return 1;
    }
    int editable = tuie_editable(n);
    if (ctrl && (strcmp(k, "z") == 0 || strcmp(k, "y") == 0)) {
        if (!editable) {
            return 0;
        }
        return tuie_history(n, k[0] == 'z' && !shift);
    }
    if (strcmp(k, "enter") == 0 && !ctrl && !(m & TERM_MOD_ALT)) {
        if (!editable) {
            return 0;
        }
        tuie_type(n, "\n", 1, 0);
        return 1;
    }
    if (strcmp(k, "tab") == 0) {
        /* an editor takes a Tab as Qt's does; Shift+Tab and Ctrl+Tab leave */
        if (!editable || (m & (TERM_MOD_CTRL | TERM_MOD_SHIFT | TERM_MOD_ALT))) {
            return 0;
        }
        tuie_type(n, "\t", 1, 1);
        return 1;
    }
    if (strcmp(k, "backspace") == 0 || strcmp(k, "delete") == 0) {
        if (!editable) {
            return 0;
        }
        if (has_sel) {
            tuie_edit(n, s0, s1, "", 0, 0);
            return 1;
        }
        size_t from = n->caret, upto = n->caret;
        if (k[0] == 'b') {
            from = ctrl ? tuie_word_left(t, from) : tuie_prev(t, from);
        } else {
            upto = ctrl ? tuie_word_right(t, upto) : tuie_next(t, upto);
        }
        if (from != upto) {
            tuie_edit(n, from, upto, "", 0, 0);
        }
        return 1;
    }
    if (ev->text[0] && !(m & (TERM_MOD_CTRL | TERM_MOD_ALT))) {
        if (!editable) {
            return 0;
        }
        tuie_type(n, ev->text, strlen(ev->text), 1);
        return 1;
    }
    return 0;
}

/* A paste is one edit: its line breaks kept, CR LF and CR made LF. */
static void tuie_paste(TuiNode *n, const char *data, size_t len) {
    if (!tuie_editable(n)) {
        return;
    }
    char *clean = malloc(len + 1);
    if (!clean) {
        abort();
    }
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\r') {
            clean[o++] = '\n';
            if (i + 1 < len && data[i + 1] == '\n') {
                i++;
            }
        } else if (data[i]) {
            clean[o++] = data[i];
        }
    }
    clean[o] = 0;
    tuie_type(n, clean, o, 0);
    free(clean);
}

/* The byte under a cell of the text area, clamped to the rows shown. */
static size_t tuie_at_cell(TuiNode *n, int x, int y) {
    TuiEdit *e = n->ed;
    int nr = 0;
    TuieRow *rows = tuie_rows(n, tuie_text_width(n), &nr);
    int r = e->top + (y - e->ty);
    size_t at;
    if (r < 0) {
        at = 0;
    } else if (r >= nr) {
        at = strlen(tuie_text(n));
    } else {
        int c = x - e->tx + e->left;
        at = tuie_at_col(n, &rows[r], c < 0 ? 0 : c);
    }
    free(rows);
    return at;
}

static void tuie_gutter_fire(TuiNode *n, int line) {
    tuie_gclick_line = line;
    tuie_gclick_live = 1;
    char *h = n->ed->on_gutter ? copy_string(n->ed->on_gutter) : NULL;
    tui_fire(h);
    free(h);
    tuie_gclick_live = 0;
}

/* 1 when the event was the editor's */
static int tuie_mouse(TuiNode *n, const TermEvent *ev) {
    TuiEdit *e = n->ed;
    int x = ev->column - 1, y = ev->row - 1;
    if (strcmp(ev->action, "release") == 0) {
        tuie_drag = NULL;
        return 0;
    }
    if (strcmp(ev->action, "press") != 0 || ev->button != 1 || !n->enabled) {
        return 0;
    }
    if (e->gw && x >= e->gx && x < e->gx + e->gw && y >= e->ty && y < e->ty + e->th) {
        int nr = 0;
        TuieRow *rows = tuie_rows(n, tuie_text_width(n), &nr);
        int r = e->top + (y - e->ty);
        int line = r < nr ? rows[r].line + 1 : 0;
        free(rows);
        if (line > 0) {
            tuie_gutter_fire(n, line);
        }
        return 1;
    }
    tui_focus = n;
    n->caret = tuie_at_cell(n, x, y);
    if (!(ev->mods & TERM_MOD_SHIFT)) {
        e->anchor = n->caret;
    }
    e->goal = -1;
    e->typing = 0;
    e->reveal = 1;
    tuie_drag = n;
    tui_dirty = 1;
    return 1;
}

/* a move with the button held, after a press in an editor */
static int tuie_drag_to(const TermEvent *ev) {
    TuiNode *n = tuie_drag;
    if (!n || strcmp(ev->action, "move") != 0 || ev->button != 1) {
        return 0;
    }
    n->caret = tuie_at_cell(n, ev->column - 1, ev->row - 1);
    n->ed->reveal = 1;
    tui_dirty = 1;
    return 1;
}

static void tuie_scroll_rows(TuiNode *n, int rows) {
    n->ed->top -= rows;
    if (n->ed->top < 0) {
        n->ed->top = 0;
    }
    tui_dirty = 1;
}

static int tuie_invoke(TuiNode *n, const char *method) {
    TuiEdit *e = n->ed;
    if (strcmp(method, "select_all") == 0) {
        e->anchor = 0;
        n->caret = strlen(tuie_text(n));
        tui_dirty = 1;
        return 1;
    }
    if (strcmp(method, "undo") == 0 || strcmp(method, "redo") == 0) {
        return tuie_history(n, method[0] == 'u');
    }
    if (strcmp(method, "clear") == 0) {
        /* Qt's clear() is setPlainText(""), history and all */
        tuie_set_text(n, "");
        return 1;
    }
    return -1;
}

/* ---- the verbs: caret, selection, replacing, marking (ui_backend.h) ----------------------- */

static TuiNode *tuie_named(void *window, const char *name, const char *what, char *err, size_t err_size) {
    TuiNode *n = tui_find(window, name);
    if (!n) {
        TUI_ERR("there is nothing called `%s` in this window", name ? name : "");
        return NULL;
    }
    if (!tuie_is(n)) {
        TUI_ERR("only a `source` or a `memo` %s, and `%s` is neither", what, name);
        return NULL;
    }
    return n;
}

static int tuie_range(TuiNode *n, const int *span, size_t *from, size_t *to, char *err, size_t err_size) {
    if (!tuie_position(n, span[0], span[1], from, err, err_size) ||
        !tuie_position(n, span[2], span[3], to, err, err_size)) {
        return 0;
    }
    if (*to < *from) {
        return TUI_ERR("a range ends before it starts -- give the earlier position first");
    }
    return 1;
}

static int tui_caret(void *window, const char *name, int *line, int *column, int *selecting, char *err,
                     size_t err_size) {
    TuiNode *n = tui_find(window, name);
    if (!n) {
        TUI_ERR("there is nothing called `%s` in this window", name ? name : "");
        return -1;
    }
    if (!tuie_is(n)) {
        return 0;
    }
    int l, c;
    tuie_line_column(n, n->caret, &l, &c);
    if (line) *line = l;
    if (column) *column = c;
    if (selecting) *selecting = n->caret != n->ed->anchor;
    return 1;
}

static int tui_caret_to(void *window, const char *name, int line, int column, char *err, size_t err_size) {
    TuiNode *n = tuie_named(window, name, "has a caret", err, err_size);
    size_t at;
    if (!n || !tuie_position(n, line, column, &at, err, err_size)) {
        return 0;
    }
    n->caret = n->ed->anchor = at;
    n->ed->reveal = 1;
    n->ed->goal = -1;
    n->ed->typing = 0;
    tui_dirty = 1;
    return 1;
}

static int tui_select_text(void *window, const char *name, int line, int column, int line2, int column2,
                           char *err, size_t err_size) {
    TuiNode *n = tuie_named(window, name, "holds text to select", err, err_size);
    if (!n) {
        return 0;
    }
    int span[4] = { line, column, line2, column2 };
    size_t from, to;
    if (!tuie_range(n, span, &from, &to, err, err_size)) {
        return 0;
    }
    n->ed->anchor = from;
    n->caret = to;
    n->ed->reveal = 1;
    n->ed->goal = -1;
    n->ed->typing = 0;
    tui_dirty = 1;
    return 1;
}

/* 1 a selection (*text malloc'd), 0 none, -1 refused */
static int tui_selection(void *window, const char *name, int *span, char **text, char *err, size_t err_size) {
    TuiNode *n = tuie_named(window, name, "holds a selection", err, err_size);
    if (!n) {
        return -1;
    }
    size_t from, to;
    tuie_selection_bytes(n, &from, &to);
    if (from == to) {
        return 0;
    }
    tuie_line_column(n, from, &span[0], &span[1]);
    tuie_line_column(n, to, &span[2], &span[3]);
    char *s = malloc(to - from + 1);
    if (!s) {
        abort();
    }
    memcpy(s, tuie_text(n) + from, to - from);
    s[to - from] = 0;
    *text = s;
    return 1;
}

typedef struct {
    size_t from, to, index;
} TuieEditAt;

static int tuie_edit_order(const void *a, const void *b) {
    const TuieEditAt *x = a, *y = b;
    if (x->from != y->from) {
        return x->from < y->from ? -1 : 1;
    }
    if (x->to != y->to) {
        return x->to < y->to ? -1 : 1;
    }
    return x->index < y->index ? -1 : (x->index > y->index);
}

/* EDITS AS ONE UNDO STEP, every span in the text before the call, refused
 * whole if any overlaps another, applied last to first (gb_qt_replace_text) */
static int tui_replace_text(void *window, const char *name, const int *spans, const char *const *texts,
                            size_t count, char *err, size_t err_size) {
    TuiNode *n = tuie_named(window, name, "holds text to replace", err, err_size);
    if (!n) {
        return 0;
    }
    if (n->read_only || n->display_only) {
        return TUI_ERR("`%s` is read-only, so its text cannot be replaced as an edit", name);
    }
    TuieEditAt *at = malloc(sizeof *at * (count ? count : 1));
    if (!at) {
        abort();
    }
    for (size_t i = 0; i < count; i++) {
        char why[256] = "";
        if (!tuie_range(n, spans + 4 * i, &at[i].from, &at[i].to, why, sizeof why)) {
            free(at);
            return TUI_ERR("edit %zu: %s", i + 1, why);
        }
        at[i].index = i;
    }
    qsort(at, count, sizeof *at, tuie_edit_order);
    for (size_t k = 1; k < count; k++) {
        if (at[k].from < at[k - 1].to || at[k].from == at[k - 1].from) {
            size_t a = at[k - 1].index + 1, b = at[k].index + 1;
            free(at);
            return TUI_ERR("edits %zu and %zu overlap, so the result would depend on their order -- "
                           "nothing was changed", a, b);
        }
    }
    TuiEdit *e = n->ed;
    tuie_stack_push(&e->undo, &e->nundo, &e->capundo, n);
    for (int i = 0; i < e->nredo; i++) {
        free(e->redo[i].text);
    }
    e->nredo = 0;
    e->typing = 0;
    for (size_t k = count; k-- > 0;) {
        const char *s = texts[at[k].index] ? texts[at[k].index] : "";
        tuie_splice(n, at[k].from, at[k].to, s, strlen(s));
    }
    free(at);
    tui_field_changed(n);
    return 1;
}

static int tui_highlight_text(void *window, const char *name, const int *spans, size_t count, char *err,
                              size_t err_size) {
    TuiNode *n = tuie_named(window, name, "can mark text", err, err_size);
    if (!n) {
        return 0;
    }
    size_t *hl = malloc(sizeof *hl * 2 * (count ? count : 1));
    if (!hl) {
        abort();
    }
    for (size_t i = 0; i < count; i++) {
        char why[256] = "";
        if (!tuie_range(n, spans + 4 * i, &hl[2 * i], &hl[2 * i + 1], why, sizeof why)) {
            free(hl);
            return TUI_ERR("range %zu: %s", i + 1, why);
        }
    }
    free(n->ed->hl);
    n->ed->hl = hl;
    n->ed->nhl = count;
    tui_dirty = 1;
    return 1;
}

static int tui_highlights(void *window, const char *name, int **spans, size_t *count, char *err,
                          size_t err_size) {
    TuiNode *n = tuie_named(window, name, "can mark text", err, err_size);
    if (!n) {
        return 0;
    }
    TuiEdit *e = n->ed;
    *count = e->nhl;
    *spans = malloc(sizeof(int) * 4 * (e->nhl ? e->nhl : 1));
    if (!*spans) {
        abort();
    }
    for (size_t i = 0; i < e->nhl; i++) {
        int *s = *spans + 4 * i;
        tuie_line_column(n, e->hl[2 * i], &s[0], &s[1]);
        tuie_line_column(n, e->hl[2 * i + 1], &s[2], &s[3]);
    }
    return 1;
}

/* ---- the margin: line numbers and marks (a `source` only) --------------------------------- */

static TuiNode *tuie_source_named(void *window, const char *name, char *err, size_t err_size) {
    TuiNode *n = tui_find(window, name);
    if (!n) {
        TUI_ERR("there is nothing called `%s` in this window", name ? name : "");
        return NULL;
    }
    if (!tuie_is(n) || !n->ed->source) {
        TUI_ERR("only a `source` has a margin, and `%s` is not one", name);
        return NULL;
    }
    return n;
}

static int tui_gutter_enable(void *window, const char *name, int numbers, int marks, char *err,
                             size_t err_size) {
    TuiNode *n = tuie_source_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    n->ed->numbers = numbers != 0;
    n->ed->marks = marks != 0;
    tui_dirty = 1;
    return 1;
}

static int tui_gutter_info(void *window, const char *name, int *numbers, int *marks, int *width, char *err,
                           size_t err_size) {
    TuiNode *n = tuie_source_named(window, name, err, err_size);
    if (!n) {
        return -1;
    }
    if (!n->ed->numbers && !n->ed->marks) {
        return 0;
    }
    if (numbers) *numbers = n->ed->numbers;
    if (marks) *marks = n->ed->marks;
    if (width) *width = tuie_gutter_width(n) * 8;
    return 1;
}

static int tui_mark_kind(const char *category) {
    static const char *const kinds[] = { "section", "break", "error", "warning", "note", NULL };
    for (int i = 0; category && kinds[i]; i++) {
        if (strcmp(kinds[i], category) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *tui_mark_name(int kind) {
    static const char *const kinds[] = { "section", "break", "error", "warning", "note" };
    return kind >= 0 && kind < TUIE_MARKS ? kinds[kind] : NULL;
}

static int tui_mark_count(void) {
    return TUIE_MARKS;
}

static int tui_mark_set(void *window, const char *name, int kind, const int *lines, int count, char *err,
                        size_t err_size) {
    TuiNode *n = tuie_source_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    if (!n->ed->marks) {
        /* the declaration claims the margin's room, as on Qt */
        return TUI_ERR("`source %s` does not carry marks; declare it `source %s( marks: true )` so the "
                       "margin reserves its space before the window is shown", name, name);
    }
    if (kind < 0 || kind >= TUIE_MARKS) {
        return TUI_ERR("there is no such kind of mark");
    }
    int *copy = malloc(sizeof(int) * (size_t)(count > 0 ? count : 1));
    if (!copy) {
        abort();
    }
    memcpy(copy, lines, sizeof(int) * (size_t)(count > 0 ? count : 0));
    free(n->ed->mk[kind]);
    n->ed->mk[kind] = copy;
    n->ed->nmk[kind] = count > 0 ? count : 0;
    tui_dirty = 1;
    return 1;
}

/* what was set, unfiltered, as on Qt */
static int tui_mark_get(void *window, const char *name, int kind, int *out, int max, char *err,
                        size_t err_size) {
    TuiNode *n = tuie_source_named(window, name, err, err_size);
    if (!n) {
        return -1;
    }
    if (kind < 0 || kind >= TUIE_MARKS) {
        return 0;
    }
    for (int i = 0; i < n->ed->nmk[kind] && i < max; i++) {
        out[i] = n->ed->mk[kind][i];
    }
    return n->ed->nmk[kind];
}

/* Press the margin beside a line: it must be on screen, as on Qt. */
static int tui_gutter_press(void *window, const char *name, int line, char *err, size_t err_size) {
    TuiNode *n = tuie_source_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    if (!n->ed->numbers && !n->ed->marks) {
        return TUI_ERR("`source %s` has no margin to press", name);
    }
    if (line < 1) {
        return TUI_ERR("line numbers start at 1");
    }
    tui_render();
    TuiEdit *e = n->ed;
    int nr = 0, shown = 0;
    TuieRow *rows = tuie_rows(n, tuie_text_width(n), &nr);
    for (int r = e->top; r < nr && r < e->top + e->th; r++) {
        if (rows[r].line + 1 == line) {
            shown = 1;
            break;
        }
    }
    free(rows);
    if (!shown || !n->visible || e->th < 1) {
        return TUI_ERR("`source %s` has no such line on screen", name);
    }
    tuie_gutter_fire(n, line);
    tui_render();
    return 1;
}

static int tui_gutter_click(int *line) {
    if (!tuie_gclick_live) {
        return 0;
    }
    if (line) {
        *line = tuie_gclick_line;
    }
    return 1;
}

static void tui_gutter_painted(int *lines, int *marks, int *first) {
    if (lines) *lines = tuie_painted_lines;
    if (marks) *marks = tuie_painted_marks;
    if (first) *first = tuie_painted_first;
}

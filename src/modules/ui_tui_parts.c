/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * THE TERMINAL BACKEND'S INCREMENT 4 (docs/tui_design.md §7): the parts that
 * hold rows or lists -- `table` (held, and `to ask`), `tree`, `choice` --
 * the furniture -- `menu` and its items, `popup`, `toolbar`, `status` --
 * and the regions -- `tabs`, `split`, `scroll`, `cells`. `#include`d at the
 * end of src/modules/ui_tui.c, whose core calls the `tuip_` hooks.
 *
 * ONE OVERLAY serves a choice's list, a menu's entries and a popup: a box
 * drawn over the window, taking every key and press until it closes. A menu
 * inside a menu is a second overlay beside the first.
 *
 * AN ASKING TABLE IS ASKED ONLY FOR WHAT IT SHOWS, once per frame: the cells
 * of its visible rows, collected before its columns are measured, so a
 * million rows cost a screenful (run_ui_ask holds the same of Qt). */

/* ---- small helpers ------------------------------------------------------------ */

static int tuip_is_grid(const TuiNode *n) {
    return tui_is(n, "table") || tui_is(n, "tree");
}

static long tuip_rows(const TuiNode *n) {
    return n->is_virtual ? n->vrows : n->nrows;
}

static void tuip_fire_copy(const char *handler) {
    char *h = handler ? copy_string(handler) : NULL;
    tui_fire(h);
    free(h);
}

/* the tab a handler is about (ui.which_tab), saved around nested handlers */
static int tuip_which = -1;

static void tuip_fire_tab(const char *handler, int tab) {
    int saved = tuip_which;
    tuip_which = tab;
    tuip_fire_copy(handler);
    tuip_which = saved;
}

/* the wheel event a handler is about (ui.wheel, ui.swallow) */
static int tuip_wheel_live = 0, tuip_wheel_swallowed = 0;
static double tuip_wheel_dy = 0, tuip_wheel_dx = 0;
static int tuip_wheel_ctrl = 0, tuip_wheel_shift = 0, tuip_wheel_alt = 0;

/* A cell's text: a held table's, or asked of the program for an asking one
 * (malloc'd; an empty cell when the function is gone or raised). */
static char *tuip_cell(TuiNode *t, long row, int col) {
    if (row < 0 || col < 0 || col >= t->ncols || row >= tuip_rows(t)) {
        return NULL;
    }
    if (t->is_virtual) {
        char *text = NULL;
        if (tui_g_cell && t->cell_fn && tui_g_cell(t->cell_fn, (int)row, col, &text)) {
            return text ? text : copy_string("");
        }
        return copy_string("");
    }
    const char *c = t->cells[(size_t)row * (size_t)t->ncols + (size_t)col];
    return copy_string(c ? c : "");
}

static void tuip_select(TuiNode *t, long row, int announce) {
    long rows = tuip_rows(t);
    if (row >= rows) {
        row = rows - 1;
    }
    if (row < -1) {
        row = -1;
    }
    int changed = t->selected != row;
    t->selected = (int)row;
    if (row >= 0) {
        int shown = t->h - (t->headings ? 1 : 0);
        if (shown < 1) {
            shown = 1;
        }
        if (row < t->top) {
            t->top = (int)row;
        } else if (row >= t->top + shown) {
            t->top = (int)row - shown + 1;
        }
    }
    tui_dirty = 1;
    if (changed && announce) {
        tuip_fire_copy(t->on_chosen);
    }
}

/* ---- the overlay ------------------------------------------------------------------ */

typedef struct {
    TuiNode *owner;     /* a choice, a menu or a popup */
    int x, y, w, h;
    int at;             /* the entry under the cursor */
    int bar_index;      /* the bar menu it hangs from, or -1 */
} TuiOverlay;

static TuiOverlay tuip_ov[6];
static int tuip_nov = 0;

static int tuip_overlay_open(void) {
    return tuip_nov > 0;
}

static void tuip_overlay_close_all(void) {
    tuip_nov = 0;
    tui_dirty = 1;
}

/* the entries an overlay lists: a choice's items, or a menu's kids */
static int tuip_entry_count(const TuiOverlay *o) {
    return tui_is(o->owner, "choice") ? o->owner->nitems : o->owner->nkids;
}

static int tuip_entry_usable(const TuiOverlay *o, int i) {
    if (tui_is(o->owner, "choice")) {
        return 1;
    }
    TuiNode *k = o->owner->kids[i];
    return k->visible && k->enabled && !tui_is(k, "separator");
}

static const char *tuip_entry_text(const TuiOverlay *o, int i) {
    if (tui_is(o->owner, "choice")) {
        return o->owner->items[i];
    }
    TuiNode *k = o->owner->kids[i];
    return k->title ? k->title : (k->text ? k->text : (k->label ? k->label : ""));
}

static int tuip_entry_visible(const TuiOverlay *o, int i) {
    return tui_is(o->owner, "choice") || o->owner->kids[i]->visible;
}

static void tuip_overlay_push(TuiNode *owner, int x, int y, int bar_index) {
    if (tuip_nov >= (int)(sizeof tuip_ov / sizeof tuip_ov[0])) {
        return;
    }
    TuiOverlay *o = &tuip_ov[tuip_nov];
    memset(o, 0, sizeof *o);
    o->owner = owner;
    o->bar_index = bar_index;
    int w = 4, h = 0;
    int n = tuip_entry_count(o);
    for (int i = 0; i < n; i++) {
        if (!tuip_entry_visible(o, i)) {
            continue;
        }
        int cw = tui_text_cells(tuip_entry_text(o, i)) + 4;
        if (!tui_is(owner, "choice")) {
            TuiNode *k = owner->kids[i];
            if (k->shortcut) {
                cw += tui_text_cells(k->shortcut) + 2;
            }
            if (k->checkable) {
                cw += 4;
            }
        }
        if (cw > w) {
            w = cw;
        }
        h++;
    }
    h += 2;
    if (x + w > tui_screen.columns) {
        x = tui_screen.columns - w;
    }
    if (y + h > tui_screen.rows) {
        y = tui_screen.rows - h;
    }
    o->x = x < 0 ? 0 : x;
    o->y = y < 0 ? 0 : y;
    o->w = w;
    o->h = h;
    o->at = -1;
    for (int i = 0; i < n; i++) {
        if (tuip_entry_visible(o, i) && tuip_entry_usable(o, i)) {
            o->at = i;
            break;
        }
    }
    if (tui_is(owner, "choice") && owner->index >= 0) {
        o->at = owner->index;
    }
    tuip_nov++;
    tui_dirty = 1;
}

static void tuip_overlay_draw(void) {
    for (int k = 0; k < tuip_nov; k++) {
        TuiOverlay *o = &tuip_ov[k];
        TermStyle box = tui_style(tui_fg, tui_ctl, tui_ctl == TERM_DEFAULT ? TERM_REVERSE : 0);
        TermStyle sel = tui_style(tui_on_sel, tui_sel, tui_sel == TERM_DEFAULT ? TERM_BOLD : 0);
        tui_fill(o->x, o->y, o->w, o->h, box);
        int row = o->y + 1, n = tuip_entry_count(o);
        for (int i = 0; i < n; i++) {
            if (!tuip_entry_visible(o, i)) {
                continue;
            }
            TermStyle st = i == o->at ? sel : box;
            if (!tuip_entry_usable(o, i) && !tui_is(o->owner, "choice") &&
                !tui_is(o->owner->kids[i], "separator")) {
                st.attrs |= TERM_DIM;
            }
            tui_fill(o->x + 1, row, o->w - 2, 1, st);
            if (!tui_is(o->owner, "choice") && tui_is(o->owner->kids[i], "separator")) {
                for (int c = 1; c < o->w - 1; c++) {
                    tui_put_clipped(o->x + c, row, 1, "\xE2\x94\x80", box);
                }
            } else {
                int x = o->x + 2;
                if (!tui_is(o->owner, "choice") && o->owner->kids[i]->checkable) {
                    tui_put_clipped(x, row, 4, o->owner->kids[i]->checked ? "[x]" : "[ ]", st);
                    x += 4;
                }
                tui_put_clipped(x, row, o->w - (x - o->x) - 1, tuip_entry_text(o, i), st);
                if (!tui_is(o->owner, "choice")) {
                    TuiNode *e = o->owner->kids[i];
                    const char *right = tui_is(e, "menu") ? "\xE2\x96\xB8" : e->shortcut;
                    if (right) {
                        int rw = tui_text_cells(right);
                        tui_put_clipped(o->x + o->w - 2 - rw, row, rw, right, st);
                    }
                }
            }
            row++;
        }
    }
}

static void tuip_item_press(TuiNode *item) {
    if (!item || !item->enabled || !item->visible) {
        return;
    }
    if (item->checkable) {
        item->checked = !item->checked;
    }
    tuip_overlay_close_all();
    tuip_fire_copy(item->on_pressed);
    if (item->checkable) {
        tuip_fire_copy(item->on_changed);
    }
}

static void tuip_choice_pick(TuiNode *c, int index) {
    if (index < 0 || index >= c->nitems) {
        return;
    }
    int changed = c->index != index;
    c->index = index;
    tui_dirty = 1;
    if (changed) {
        tuip_fire_copy(c->on_changed);
    }
    tuip_fire_copy(c->on_activated);
}

static void tuip_overlay_step(TuiOverlay *o, int dir) {
    int n = tuip_entry_count(o);
    for (int tries = 0; tries < n; tries++) {
        o->at = ((o->at < 0 ? (dir > 0 ? -1 : 0) : o->at) + dir + n) % n;
        if (tuip_entry_visible(o, o->at) && tuip_entry_usable(o, o->at)) {
            break;
        }
    }
    tui_dirty = 1;
}

static TuiNode *tuip_menubar(TuiNode *root);
static void tuip_open_bar_menu(TuiNode *root, int index);

static void tuip_overlay_choose(TuiOverlay *o) {
    if (o->at < 0) {
        return;
    }
    if (tui_is(o->owner, "choice")) {
        TuiNode *c = o->owner;
        int at = o->at;
        tuip_overlay_close_all();
        tuip_choice_pick(c, at);
        return;
    }
    TuiNode *e = o->owner->kids[o->at];
    if (tui_is(e, "menu")) {
        int row = o->y + 1;
        for (int i = 0; i < o->at; i++) {
            row += tuip_entry_visible(o, i);
        }
        tuip_overlay_push(e, o->x + o->w - 1, row - 1, -1);
        return;
    }
    tuip_item_press(e);
}

static int tuip_overlay_key(const TermEvent *ev) {
    TuiOverlay *o = &tuip_ov[tuip_nov - 1];
    const char *k = ev->key;
    if (strcmp(k, "escape") == 0) {
        tuip_nov--;
        tui_dirty = 1;
        return 1;
    }
    if (strcmp(k, "down") == 0 || (strcmp(k, "tab") == 0 && !(ev->mods & TERM_MOD_SHIFT))) {
        tuip_overlay_step(o, 1);
        return 1;
    }
    if (strcmp(k, "up") == 0 || strcmp(k, "tab") == 0) {
        tuip_overlay_step(o, -1);
        return 1;
    }
    if (strcmp(k, "enter") == 0 || strcmp(k, "space") == 0) {
        tuip_overlay_choose(o);
        return 1;
    }
    if (strcmp(k, "right") == 0) {
        if (o->at >= 0 && !tui_is(o->owner, "choice") && tui_is(o->owner->kids[o->at], "menu")) {
            tuip_overlay_choose(o);
        } else if (tuip_ov[0].bar_index >= 0) {
            TuiNode *root = tui_top();
            int next = tuip_ov[0].bar_index + 1;
            TuiNode *bar = tuip_menubar(root);
            if (bar && bar->nkids) {
                tuip_open_bar_menu(root, next % bar->nkids);
            }
        }
        return 1;
    }
    if (strcmp(k, "left") == 0) {
        if (tuip_nov > 1) {
            tuip_nov--;
            tui_dirty = 1;
        } else if (tuip_ov[0].bar_index >= 0) {
            TuiNode *root = tui_top();
            TuiNode *bar = tuip_menubar(root);
            if (bar && bar->nkids) {
                tuip_open_bar_menu(root, (tuip_ov[0].bar_index - 1 + bar->nkids) % bar->nkids);
            }
        }
        return 1;
    }
    return 1;     /* an open overlay takes every key */
}

static int tuip_overlay_mouse(const TermEvent *ev) {
    if (strcmp(ev->action, "press") != 0) {
        return 1;
    }
    int x = ev->column - 1, y = ev->row - 1;
    for (int k = tuip_nov - 1; k >= 0; k--) {
        TuiOverlay *o = &tuip_ov[k];
        if (x >= o->x && x < o->x + o->w && y > o->y && y < o->y + o->h - 1) {
            int row = o->y + 1, n = tuip_entry_count(o);
            for (int i = 0; i < n; i++) {
                if (!tuip_entry_visible(o, i)) {
                    continue;
                }
                if (row == y) {
                    tuip_nov = k + 1;
                    if (tuip_entry_usable(o, i)) {
                        o->at = i;
                        tuip_overlay_choose(o);
                    }
                    return 1;
                }
                row++;
            }
            return 1;
        }
    }
    tuip_overlay_close_all();
    return 1;
}

/* ---- the window's furniture: menu bar, toolbar, status --------------------------- */

static TuiNode *tuip_furniture(TuiNode *root, const char *word) {
    for (int i = 0; root && i < root->nkids; i++) {
        if (root->kids[i]->furniture && tui_is(root->kids[i], word)) {
            return root->kids[i];
        }
    }
    return NULL;
}

static TuiNode *tuip_menubar(TuiNode *root) {
    return tuip_furniture(root, "menubar");
}

static void tuip_content(TuiNode *root, int *top, int *bottom) {
    TuiNode *bar = tuip_menubar(root);
    if (bar && bar->nkids) {
        (*top)++;
    }
    TuiNode *tb = tuip_furniture(root, "toolbar");
    if (tb && tb->visible) {
        (*top)++;
    }
    TuiNode *st = tuip_furniture(root, "status");
    if (st && st->visible) {
        (*bottom)--;
    }
}

/* where each bar menu's title starts, 0-based */
static int tuip_bar_x(TuiNode *bar, int index) {
    int x = (bar->parent ? bar->parent->fx : 0) + 1;
    for (int i = 0; i < index && i < bar->nkids; i++) {
        x += tui_text_cells(bar->kids[i]->title) + 2;
    }
    return x;
}

static void tuip_open_bar_menu(TuiNode *root, int index) {
    TuiNode *bar = tuip_menubar(root);
    if (!bar || index < 0 || index >= bar->nkids) {
        return;
    }
    tuip_nov = 0;
    tuip_overlay_push(bar->kids[index], tuip_bar_x(bar, index) - 1, root->fy + 2, index);
}

static void tuip_furniture_draw(TuiNode *root) {
    int row = root->fy + 1;
    TuiNode *bar = tuip_menubar(root);
    TermStyle plain = tui_style(tui_fg, tui_bg, 0);
    TermStyle barst = tui_style(tui_fg, tui_ctl, tui_ctl == TERM_DEFAULT ? TERM_REVERSE : 0);
    if (bar && bar->nkids) {
        tui_fill(root->fx, row, root->fw, 1, barst);
        for (int i = 0; i < bar->nkids; i++) {
            TermStyle st = barst;
            if (tuip_nov && tuip_ov[0].bar_index == i) {
                st = tui_style(tui_on_sel, tui_sel, tui_sel == TERM_DEFAULT ? TERM_BOLD : 0);
            }
            tui_put_clipped(tuip_bar_x(bar, i), row, tui_text_cells(bar->kids[i]->title),
                            bar->kids[i]->title, st);
        }
        row++;
    }
    TuiNode *tb = tuip_furniture(root, "toolbar");
    if (tb && tb->visible) {
        int x = root->fx;
        for (int i = 0; i < tb->nkids; i++) {
            TuiNode *e = tb->kids[i];
            e->x = x;
            e->y = row;
            if (!e->visible) {
                e->w = 0;
                continue;
            }
            if (tui_is(e, "separator")) {
                tui_put_clipped(x, row, 1, "\xE2\x94\x82", plain);
                e->w = 1;
                x += 2;
                continue;
            }
            const char *t = e->text ? e->text : (e->label ? e->label : (e->name ? e->name : ""));
            size_t len = strlen(t) + 5;
            char *s = malloc(len);
            if (!s) {
                abort();
            }
            snprintf(s, len, e->checkable && e->checked ? "[*%s]" : "[ %s ]", t);
            e->w = tui_text_cells(s);
            tui_put_clipped(x, row, e->w, s, tui_style(tui_fg, tui_ctl, e->enabled ? 0 : TERM_DIM));
            free(s);
            x += e->w + 1;
        }
        row++;
    }
    TuiNode *st = tuip_furniture(root, "status");
    if (st && st->visible) {
        int y = root->fy + root->fh - 1;
        TermStyle sst = tui_style(tui_fg, tui_ctl, tui_ctl == TERM_DEFAULT ? TERM_REVERSE : 0);
        tui_fill(root->fx, y, root->fw, 1, sst);
        /* the status bar's texts, laid out as a row with its stretches */
        st->furniture = 0;
        tui_arrange_line(st, root->fx, y, root->fw, 1, 1);
        st->furniture = 1;
        for (int i = 0; i < st->nkids; i++) {
            TuiNode *k = st->kids[i];
            if (k->visible && tui_is(k, "text")) {
                tui_put_clipped(k->x, y, k->w, k->text, sst);
            }
        }
    }
}

static int tuip_window_key(TuiNode *root, const TermEvent *ev) {
    TuiNode *bar = tuip_menubar(root);
    if (bar && bar->nkids) {
        if (strcmp(ev->key, "f10") == 0) {
            tuip_open_bar_menu(root, 0);
            return 1;
        }
        if ((ev->mods & TERM_MOD_ALT) && ev->key[0] && !ev->key[1]) {
            for (int i = 0; i < bar->nkids; i++) {
                const char *t = bar->kids[i]->title ? bar->kids[i]->title : "";
                if (tolower((unsigned char)t[0]) == tolower((unsigned char)ev->key[0])) {
                    tuip_open_bar_menu(root, i);
                    return 1;
                }
            }
        }
    }
    /* Ctrl+PgUp and Ctrl+PgDn turn the page of the `tabs` holding the focus */
    if ((ev->mods & TERM_MOD_CTRL) && (strcmp(ev->key, "page_up") == 0 || strcmp(ev->key, "page_down") == 0)) {
        for (TuiNode *p = tui_focus; p; p = p->parent) {
            if (tui_is(p, "tabs") && p->nkids) {
                int to = (p->index + (strcmp(ev->key, "page_down") == 0 ? 1 : -1) + p->nkids) % p->nkids;
                p->index = to;
                tui_focus = p;
                tui_dirty = 1;
                tuip_fire_tab(p->on_changed, to);
                return 1;
            }
        }
    }
    return 0;
}

static int tuip_window_mouse(TuiNode *root, const TermEvent *ev) {
    if (strcmp(ev->action, "press") != 0 || ev->button != 1) {
        return 0;
    }
    int x = ev->column - 1, y = ev->row - 1;
    TuiNode *bar = tuip_menubar(root);
    int row = root->fy + 1;
    if (bar && bar->nkids) {
        if (y == row) {
            for (int i = 0; i < bar->nkids; i++) {
                int bx = tuip_bar_x(bar, i), bw = tui_text_cells(bar->kids[i]->title);
                if (x >= bx && x < bx + bw) {
                    tuip_open_bar_menu(root, i);
                    return 1;
                }
            }
            return 1;
        }
        row++;
    }
    TuiNode *tb = tuip_furniture(root, "toolbar");
    if (tb && tb->visible && y == row) {
        for (int i = 0; i < tb->nkids; i++) {
            TuiNode *e = tb->kids[i];
            if (e->visible && !tui_is(e, "separator") && x >= e->x && x < e->x + e->w) {
                tuip_item_press(e);
                return 1;
            }
        }
        return 1;
    }
    return 0;
}

/* ---- tables and trees: layout and drawing ------------------------------------------ */

/* Column widths from the headings and the rows shown; for an asking table
 * the cells shown are asked once, here, and kept in `shown`. */
static void tuip_grid_widths(TuiNode *t, char **shown, long first, int nshown, int *widths) {
    for (int c = 0; c < t->ncols; c++) {
        widths[c] = t->headings ? tui_text_cells(t->headers[c]) : 1;
        if (widths[c] < 3) {
            widths[c] = 3;
        }
    }
    for (int r = 0; r < nshown; r++) {
        for (int c = 0; c < t->ncols; c++) {
            const char *text = shown[(size_t)r * t->ncols + c];
            int w = tui_text_cells(text);
            if (c == 0 && t->is_tree && t->depths && first + r < t->nrows) {
                w += t->indent * t->depths[first + r] + (t->expanders ? 2 : 0);
            }
            if (w > widths[c]) {
                widths[c] = w;
            }
        }
    }
    for (int c = 0; c < t->ncols; c++) {
        if (widths[c] > 40) {
            widths[c] = 40;
        }
    }
}

static int tuip_measure(TuiNode *n, int *w, int *h) {
    if (tuip_is_grid(n)) {
        *w = 0;
        for (int c = 0; c < n->ncols; c++) {
            *w += (tui_text_cells(n->headers[c]) > 6 ? tui_text_cells(n->headers[c]) : 6) + 1;
        }
        long rows = tuip_rows(n);
        *h = (n->headings ? 1 : 0) + (int)(rows < 3 ? 3 : (rows > 8 ? 8 : rows));
        return 1;
    }
    if (tui_is(n, "choice")) {
        int widest = 6;
        for (int i = 0; i < n->nitems; i++) {
            int c = tui_text_cells(n->items[i]);
            if (c > widest) {
                widest = c;
            }
        }
        *w = widest + 4 + (n->label && *n->label && n->parent && !tui_is(n->parent, "form")
                             ? tui_text_cells(n->label) + 1 : 0);
        *h = 1;
        return 1;
    }
    if (tui_is(n, "tabs")) {
        int line = 0;
        *w = *h = 0;
        for (int i = 0; i < n->nkids; i++) {
            int kw, kh;
            line += tui_text_cells(n->kids[i]->title) + 3;
            n->kids[i]->visible = 1;
            tui_measure(n->kids[i], &kw, &kh);
            if (kw > *w) *w = kw;
            if (kh > *h) *h = kh;
        }
        if (line > *w) {
            *w = line;
        }
        *h += 1;
        return 1;
    }
    if (tui_is(n, "tab")) {
        /* a page is a column */
        return 0;
    }
    if (tui_is(n, "split")) {
        *w = *h = 0;
        int panes = 0;
        for (int i = 0; i < n->nkids; i++) {
            int kw, kh;
            tui_measure(n->kids[i], &kw, &kh);
            if (!n->kids[i]->visible) {
                continue;
            }
            if (n->direction == 0) {
                *w += kw;
                if (kh > *h) *h = kh;
            } else {
                *h += kh;
                if (kw > *w) *w = kw;
            }
            panes++;
        }
        if (panes > 1) {
            if (n->direction == 0) *w += panes - 1;
            else *h += panes - 1;
        }
        return 1;
    }
    if (tui_is(n, "scroll")) {
        int kw = 0, kh = 0;
        if (n->nkids) {
            tui_measure(n->kids[0], &kw, &kh);
        }
        *w = kw;
        *h = kh < 3 ? kh : 3;   /* it scrolls, so it asks for little */
        return 1;
    }
    if (tui_is(n, "cells")) {
        int colw[64] = { 0 }, rowh[64] = { 0 };
        for (int r = 0; r < n->nkids && r < 64; r++) {
            TuiNode *row = n->kids[r];
            for (int i = 0; i < row->nkids; i++) {
                TuiNode *k = row->kids[i];
                int kw, kh;
                tui_measure(k, &kw, &kh);
                int c = k->grid_col;
                if (c < 0 || c >= 64) {
                    continue;
                }
                if (tui_has_caption(k)) {
                    int lw = tui_text_cells(k->label) + 1;
                    if (c > 0 && lw > colw[c - 1]) colw[c - 1] = lw;
                    kw -= lw;
                }
                if (k->across == 1 && kw > colw[c]) colw[c] = kw;
                if (k->down == 1 && kh > rowh[r]) rowh[r] = kh;
            }
        }
        *w = *h = 0;
        for (int i = 0; i < 64; i++) {
            *w += colw[i] ? colw[i] + 1 : 0;
            *h += rowh[i];
        }
        return 1;
    }
    return 0;
}

static int tuip_arrange(TuiNode *n, int x, int y, int w, int h) {
    if (tui_is(n, "tabs")) {
        int py = n->tab_side_bottom ? y : y + 1;
        for (int i = 0; i < n->nkids; i++) {
            tui_arrange(n->kids[i], x, py, w, h - 1);
        }
        return 1;
    }
    if (tui_is(n, "tab")) {
        tui_arrange_line(n, x, y, w, h, 0);
        return 1;
    }
    if (tui_is(n, "split")) {
        int panes = 0;
        for (int i = 0; i < n->nkids; i++) {
            panes += n->kids[i]->visible;
        }
        if (!panes) {
            return 1;
        }
        int room = (n->direction == 0 ? w : h) - (panes - 1);
        long weights[16] = { 0 }, total = 0;
        for (int i = 0, p = 0; i < n->nkids && p < 16; i++) {
            if (!n->kids[i]->visible) {
                continue;
            }
            if (n->nsizes == panes) {
                weights[p] = n->sizes[p];
            } else {
                int kw, kh;
                tui_measure(n->kids[i], &kw, &kh);
                weights[p] = (n->direction == 0 ? kw : kh) + 1;
            }
            total += weights[p++];
        }
        int at = n->direction == 0 ? x : y, used = 0;
        for (int i = 0, p = 0; i < n->nkids; i++) {
            TuiNode *k = n->kids[i];
            if (!k->visible) {
                continue;
            }
            int size = p == panes - 1 ? room - used
                                      : (total ? (int)(room * weights[p] / total) : room / panes);
            if (size < 0) {
                size = 0;
            }
            if (n->direction == 0) {
                tui_arrange(k, at, y, size, h);
            } else {
                tui_arrange(k, x, at, w, size);
            }
            at += size + 1;
            used += size;
            p++;
        }
        return 1;
    }
    if (tui_is(n, "scroll")) {
        if (n->nkids) {
            int kw, kh;
            tui_measure(n->kids[0], &kw, &kh);
            int max_off = kh - h;
            if (n->offset > max_off) n->offset = max_off;
            if (n->offset < 0) n->offset = 0;
            tui_arrange(n->kids[0], x, y - n->offset, w - 1, kh > h ? kh : h);
        }
        return 1;
    }
    if (tui_is(n, "cells")) {
        int colw[64] = { 0 }, rowh[64] = { 0 };
        for (int r = 0; r < n->nkids && r < 64; r++) {
            TuiNode *row = n->kids[r];
            for (int i = 0; i < row->nkids; i++) {
                TuiNode *k = row->kids[i];
                int kw, kh;
                tui_measure(k, &kw, &kh);
                int c = k->grid_col;
                if (c < 0 || c >= 64) {
                    continue;
                }
                if (tui_has_caption(k)) {
                    int lw = tui_text_cells(k->label) + 1;
                    if (c > 0 && lw > colw[c - 1]) colw[c - 1] = lw;
                    kw -= lw;
                }
                if (k->across == 1 && kw > colw[c]) colw[c] = kw;
                if (k->down == 1 && kh > rowh[r]) rowh[r] = kh;
            }
            if (!rowh[r]) rowh[r] = 1;
        }
        int colx[65];
        colx[0] = x;
        for (int c = 0; c < 64; c++) {
            colx[c + 1] = colx[c] + (colw[c] ? colw[c] + 1 : 0);
        }
        int rowy[65];
        rowy[0] = y;
        for (int r = 0; r < 64; r++) {
            rowy[r + 1] = rowy[r] + rowh[r];
        }
        for (int r = 0; r < n->nkids && r < 64; r++) {
            TuiNode *row = n->kids[r];
            row->x = x; row->y = rowy[r]; row->w = w; row->h = rowh[r];
            for (int i = 0; i < row->nkids; i++) {
                TuiNode *k = row->kids[i];
                int c = k->grid_col;
                if (c < 0 || c >= 64) {
                    continue;
                }
                int c0 = tui_has_caption(k) && c > 0 ? c - 1 : c;
                int c1 = c + k->across;
                int r1 = r + k->down;
                if (c1 > 64) c1 = 64;
                if (r1 > 64) r1 = 64;
                tui_arrange(k, colx[c0], rowy[r], colx[c1] - colx[c0] - 1, rowy[r1] - rowy[r]);
            }
        }
        return 1;
    }
    return 0;
}

static const char *tuip_align_of(TuiNode *t, int c) {
    char a = c < (int)sizeof t->aligns ? t->aligns[c] : 0;
    return a == 'r' ? "right" : a == 'c' ? "center" : "left";
}

static void tuip_put_aligned(int x, int y, int w, const char *text, char align, TermStyle st) {
    int tw = tui_text_cells(text);
    int pad = tw >= w ? 0 : (align == 'r' ? w - tw : align == 'c' ? (w - tw) / 2 : 0);
    tui_put_clipped(x + pad, y, w - pad, text, st);
}

static void tuip_draw_grid(TuiNode *t) {
    int body = t->h - (t->headings ? 1 : 0);
    if (body < 0) {
        body = 0;
    }
    long rows = tuip_rows(t);
    if (t->top > rows - body) t->top = (int)(rows - body);
    if (t->top < 0) t->top = 0;
    int nshown = rows - t->top < body ? (int)(rows - t->top) : body;
    if (nshown < 0) nshown = 0;
    char **shown = calloc((size_t)(nshown ? nshown : 1) * (size_t)(t->ncols ? t->ncols : 1), sizeof(char *));
    if (!shown) {
        abort();
    }
    for (int r = 0; r < nshown; r++) {
        for (int c = 0; c < t->ncols; c++) {
            shown[(size_t)r * t->ncols + c] = tuip_cell(t, t->top + r, c);
        }
    }
    int widths[64] = { 0 };
    tuip_grid_widths(t, shown, t->top, nshown, widths);
    int focused = tui_focus == t;
    TermStyle head = tui_style(tui_fg, tui_bg, TERM_BOLD | TERM_UNDERLINE);
    int y = t->y;
    int sx0 = tui_clip_x0, sy0 = tui_clip_y0, sx1 = tui_clip_x1, sy1 = tui_clip_y1;
    if (t->x > tui_clip_x0) tui_clip_x0 = t->x;
    if (t->y > tui_clip_y0) tui_clip_y0 = t->y;
    if (t->x + t->w < tui_clip_x1) tui_clip_x1 = t->x + t->w;
    if (t->y + t->h < tui_clip_y1) tui_clip_y1 = t->y + t->h;
    if (t->headings) {
        int x = t->x;
        for (int c = 0; c < t->ncols && c < 64; c++) {
            tuip_put_aligned(x, y, widths[c], t->headers[c], t->aligns[c], head);
            x += widths[c] + 1;
        }
        y++;
    }
    for (int r = 0; r < nshown; r++) {
        long row = t->top + r;
        unsigned attrs = (t->enabled ? 0 : TERM_DIM) | (t->striped && row % 2 ? TERM_DIM : 0);
        TermStyle st = tui_style(tui_fg, tui_bg, attrs);
        if (row == t->selected) {
            st = tui_style(tui_on_sel, tui_sel, tui_sel == TERM_DEFAULT ? (focused ? TERM_REVERSE : TERM_BOLD | TERM_UNDERLINE) : 0);
            tui_fill(t->x, y, t->w, 1, st);
        }
        int x = t->x;
        for (int c = 0; c < t->ncols && c < 64; c++) {
            const char *text = shown[(size_t)r * t->ncols + c];
            int cx = x, cw = widths[c];
            if (c == 0 && t->is_tree && t->depths && row < t->nrows) {
                int d = t->depths[row] > 0 ? t->depths[row] : 0;
                cx += d * t->indent;
                cw -= d * t->indent;
                if (t->expanders) {
                    int parent = row + 1 < t->nrows && t->depths[row + 1] > t->depths[row];
                    tui_put_clipped(cx, y, 2, parent ? "\xE2\x96\xBE " : "  ", st);
                    cx += 2;
                    cw -= 2;
                }
            }
            tuip_put_aligned(cx, y, cw, text, t->aligns[c], st);
            x += widths[c] + 1;
        }
        y++;
    }
    tui_clip_x0 = sx0; tui_clip_y0 = sy0; tui_clip_x1 = sx1; tui_clip_y1 = sy1;
    for (long i = 0; i < (long)nshown * t->ncols; i++) {
        free(shown[i]);
    }
    free(shown);
}

static int tuip_draw(TuiNode *n) {
    if (tuip_is_grid(n)) {
        tuip_draw_grid(n);
        return 1;
    }
    if (tui_is(n, "choice")) {
        int x = n->x, w = n->w, focused = tui_focus == n;
        TermStyle plain = tui_style(tui_fg, tui_bg, n->enabled ? 0 : TERM_DIM);
        if (n->label && *n->label && n->parent && !tui_is(n->parent, "form")) {
            int lw = tui_text_cells(n->label) + 1;
            tui_put_clipped(x, n->y, lw, n->label, plain);
            x += lw;
            w -= lw;
        }
        TermStyle box = focused ? tui_style(tui_on_sel, tui_sel, tui_sel == TERM_DEFAULT ? TERM_REVERSE : 0)
                                : tui_style(tui_fg, tui_base, (tui_base == TERM_DEFAULT ? TERM_UNDERLINE : 0) |
                                                               (n->enabled ? 0 : TERM_DIM));
        tui_fill(x, n->y, w, 1, box);
        if (n->index >= 0 && n->index < n->nitems) {
            tui_put_clipped(x, n->y, w - 2, n->items[n->index], box);
        }
        tui_put_clipped(x + w - 1, n->y, 1, "\xE2\x96\xBE", box);
        return 1;
    }
    if (tui_is(n, "tabs")) {
        int ly = n->tab_side_bottom ? n->y + n->h - 1 : n->y, x = n->x;
        for (int i = 0; i < n->nkids; i++) {
            const char *t = n->kids[i]->title ? n->kids[i]->title : "";
            TermStyle st = i == n->index
                ? tui_style(tui_on_sel, tui_sel, tui_sel == TERM_DEFAULT ? (tui_focus == n ? TERM_REVERSE : TERM_BOLD | TERM_UNDERLINE) : 0)
                : tui_style(tui_fg, tui_bg, TERM_DIM);
            size_t len = strlen(t) + 3;
            char *s = malloc(len);
            if (!s) {
                abort();
            }
            snprintf(s, len, " %s ", t);
            int tw = tui_text_cells(s);
            tui_put_clipped(x, ly, tw, s, st);
            n->kids[i]->grid_col = x;      /* where its caption is, for a press */
            x += tw + 1;
            free(s);
        }
        if (n->index >= 0 && n->index < n->nkids) {
            tui_draw(n->kids[n->index]);
        }
        return 1;
    }
    if (tui_is(n, "split")) {
        TermStyle st = tui_style(tui_line, tui_bg, tui_line == TERM_DEFAULT ? TERM_DIM : 0);
        TuiNode *prev = NULL;
        for (int i = 0; i < n->nkids; i++) {
            TuiNode *k = n->kids[i];
            if (!k->visible) {
                continue;
            }
            if (prev) {
                if (n->direction == 0) {
                    for (int r = 0; r < n->h; r++) {
                        tui_put_clipped(k->x - 1, n->y + r, 1, "\xE2\x94\x82", st);
                    }
                } else {
                    for (int c = 0; c < n->w; c++) {
                        tui_put_clipped(n->x + c, k->y - 1, 1, "\xE2\x94\x80", st);
                    }
                }
            }
            int sx0 = tui_clip_x0, sy0 = tui_clip_y0, sx1 = tui_clip_x1, sy1 = tui_clip_y1;
            if (k->x > tui_clip_x0) tui_clip_x0 = k->x;
            if (k->y > tui_clip_y0) tui_clip_y0 = k->y;
            if (k->x + k->w < tui_clip_x1) tui_clip_x1 = k->x + k->w;
            if (k->y + k->h < tui_clip_y1) tui_clip_y1 = k->y + k->h;
            tui_draw(k);
            tui_clip_x0 = sx0; tui_clip_y0 = sy0; tui_clip_x1 = sx1; tui_clip_y1 = sy1;
            prev = k;
        }
        return 1;
    }
    if (tui_is(n, "scroll")) {
        int sx0 = tui_clip_x0, sy0 = tui_clip_y0, sx1 = tui_clip_x1, sy1 = tui_clip_y1;
        if (n->x > tui_clip_x0) tui_clip_x0 = n->x;
        if (n->y > tui_clip_y0) tui_clip_y0 = n->y;
        if (n->x + n->w < tui_clip_x1) tui_clip_x1 = n->x + n->w;
        if (n->y + n->h < tui_clip_y1) tui_clip_y1 = n->y + n->h;
        if (n->nkids) {
            tui_draw(n->kids[0]);
            int kw, kh;
            tui_measure(n->kids[0], &kw, &kh);
            if (kh > n->h && n->h > 0) {
                /* where the view is, in the right-hand column */
                int at = n->y + (int)((long)n->offset * (n->h - 1) / (kh - n->h > 0 ? kh - n->h : 1));
                for (int r = 0; r < n->h; r++) {
                    tui_put_clipped(n->x + n->w - 1, n->y + r, 1,
                                    n->y + r == at ? "\xE2\x96\x88" : "\xE2\x94\x82",
                                    tui_style(tui_line, tui_bg, TERM_DIM));
                }
            }
        }
        tui_clip_x0 = sx0; tui_clip_y0 = sy0; tui_clip_x1 = sx1; tui_clip_y1 = sy1;
        return 1;
    }
    if (tui_is(n, "cells")) {
        for (int r = 0; r < n->nkids; r++) {
            TuiNode *row = n->kids[r];
            for (int i = 0; i < row->nkids; i++) {
                tui_draw(row->kids[i]);
            }
        }
        return 1;
    }
    return 0;
}

/* A focused part inside a scrolled region is scrolled into view. */
static void tuip_reveal(TuiNode *n) {
    for (TuiNode *p = n ? n->parent : NULL; p; p = p->parent) {
        if (tui_is(p, "tabs")) {
            for (int i = 0; i < p->nkids; i++) {
                if (tui_within(n, p->kids[i])) {
                    p->index = i;
                }
            }
        }
        if (tui_is(p, "scroll") && p->h > 0) {
            if (n->y < p->y) {
                p->offset -= p->y - n->y;
            } else if (n->y + 1 > p->y + p->h) {
                p->offset += n->y + 1 - (p->y + p->h);
            }
            if (p->offset < 0) {
                p->offset = 0;
            }
        }
    }
}

/* ---- keys and the mouse on these parts ---------------------------------------------- */

static int tuip_focusable(const TuiNode *n) {
    return tuip_is_grid(n) || tui_is(n, "choice") || (tui_is(n, "tabs") && n->nkids > 0);
}

static int tuip_key(TuiNode *f, const TermEvent *ev) {
    const char *k = ev->key;
    if (tuip_is_grid(f)) {
        long rows = tuip_rows(f);
        int page = f->h - (f->headings ? 1 : 0);
        if (page < 1) page = 1;
        long to = f->selected;
        if (strcmp(k, "down") == 0) to = f->selected < 0 ? 0 : f->selected + 1;
        else if (strcmp(k, "up") == 0) to = f->selected <= 0 ? 0 : f->selected - 1;
        else if (strcmp(k, "page_down") == 0 && !ev->mods) to = (f->selected < 0 ? 0 : f->selected) + page;
        else if (strcmp(k, "page_up") == 0 && !ev->mods) to = f->selected - page;
        else if (strcmp(k, "home") == 0) to = 0;
        else if (strcmp(k, "end") == 0) to = rows - 1;
        else if (strcmp(k, "enter") == 0 || strcmp(k, "space") == 0) {
            if (f->selected >= 0) {
                tuip_fire_copy(f->on_activated);
            }
            return 1;
        } else {
            return 0;
        }
        if (rows <= 0) {
            return 1;
        }
        if (to < 0) to = 0;
        if (to >= rows) to = rows - 1;
        tuip_select(f, to, 1);
        return 1;
    }
    if (tui_is(f, "choice")) {
        if (strcmp(k, "enter") == 0 || strcmp(k, "space") == 0) {
            int lw = f->label && *f->label && f->parent && !tui_is(f->parent, "form") ? tui_text_cells(f->label) + 1 : 0;
            tuip_overlay_push(f, f->x + lw, f->y + 1, -1);
            return 1;
        }
        if (strcmp(k, "down") == 0 && f->index + 1 < f->nitems) {
            tuip_choice_pick(f, f->index + 1);
            return 1;
        }
        if (strcmp(k, "up") == 0 && f->index > 0) {
            tuip_choice_pick(f, f->index - 1);
            return 1;
        }
        return 0;
    }
    if (tui_is(f, "tabs")) {
        int to = f->index;
        if (strcmp(k, "right") == 0) to = (f->index + 1) % f->nkids;
        else if (strcmp(k, "left") == 0) to = (f->index - 1 + f->nkids) % f->nkids;
        else return 0;
        f->index = to;
        tui_dirty = 1;
        tuip_fire_tab(f->on_changed, to);
        return 1;
    }
    return 0;
}

static TuiNode *tuip_scrollable(TuiNode *n) {
    for (; n; n = n->parent) {
        if (tui_is(n, "scroll") || tuip_is_grid(n)) {
            return n;
        }
    }
    return NULL;
}

/* A wheel turn over a part: its `wheel` handler first, which may swallow it;
 * then the nearest scrolling region scrolls three rows a notch. Answers
 * whether the handler claimed it. */
static int tuip_wheel_at(TuiNode *n, double notches, int mods) {
    int claimed = 0;
    for (TuiNode *p = n; p; p = p->parent) {
        if (p->on_wheel) {
            tuip_wheel_live = 1;
            tuip_wheel_swallowed = 0;
            tuip_wheel_dy = notches;
            tuip_wheel_dx = 0;
            tuip_wheel_ctrl = (mods & GB_UI_MOD_CTRL) != 0;
            tuip_wheel_shift = (mods & GB_UI_MOD_SHIFT) != 0;
            tuip_wheel_alt = (mods & GB_UI_MOD_ALT) != 0;
            tuip_fire_copy(p->on_wheel);
            claimed = tuip_wheel_swallowed;
            tuip_wheel_live = 0;
            break;
        }
    }
    if (!claimed) {
        TuiNode *s = tuip_scrollable(n);
        int rows = (int)(notches > 0 ? notches * 3 + 0.5 : notches * 3 - 0.5);
        if (s && tui_is(s, "scroll")) {
            s->offset -= rows;
            if (s->offset < 0) s->offset = 0;
        } else if (s) {
            s->top -= rows;
            if (s->top < 0) s->top = 0;
        }
        tui_dirty = 1;
    }
    return claimed;
}

static int tuip_mouse(TuiNode *hit, const TermEvent *ev) {
    if (strcmp(ev->action, "wheel") == 0) {
        tuip_wheel_at(hit, ev->button == 4 ? 1 : -1, (ev->mods & TERM_MOD_CTRL ? GB_UI_MOD_CTRL : 0) |
                                                      (ev->mods & TERM_MOD_SHIFT ? GB_UI_MOD_SHIFT : 0) |
                                                      (ev->mods & TERM_MOD_ALT ? GB_UI_MOD_ALT : 0));
        return 1;
    }
    if (strcmp(ev->action, "press") != 0) {
        return 0;
    }
    int x = ev->column - 1, y = ev->row - 1;
    for (TuiNode *p = hit; p; p = p->parent) {
        if (tuip_is_grid(p)) {
            int body_y = p->y + (p->headings ? 1 : 0);
            if (y >= body_y) {
                long row = p->top + (y - body_y);
                if (row < tuip_rows(p)) {
                    tui_focus = p;
                    tuip_select(p, row, 1);
                    if (ev->button == 1) {
                        tuip_fire_copy(p->on_activated);
                    } else if (ev->button == 3) {
                        tuip_fire_copy(p->on_context);
                    }
                }
            }
            return 1;
        }
        if (tui_is(p, "tabs")) {
            int ly = p->tab_side_bottom ? p->y + p->h - 1 : p->y;
            if (y == ly) {
                for (int i = p->nkids - 1; i >= 0; i--) {
                    if (x >= p->kids[i]->grid_col) {
                        if (ev->button == 3) {
                            tuip_fire_tab(p->on_context, i);
                        } else if (p->index != i) {
                            p->index = i;
                            tui_focus = p;
                            tui_dirty = 1;
                            tuip_fire_tab(p->on_changed, i);
                        }
                        return 1;
                    }
                }
                return 1;
            }
            if (ev->button == 3 && p->on_context && hit == p) {
                tuip_fire_tab(p->on_context, -1);
                return 1;
            }
        }
        if (tui_is(p, "choice") && ev->button == 1) {
            tui_focus = p;
            int lw = p->label && *p->label && p->parent && !tui_is(p->parent, "form") ? tui_text_cells(p->label) + 1 : 0;
            tuip_overlay_push(p, p->x + lw, p->y + 1, -1);
            return 1;
        }
    }
    return 0;
}

/* ---- building ------------------------------------------------------------------------ */

/* the words a container may be, beyond increment 3's boxes; NULL when this
 * file does not build it */
static void *tuip_container(TuiNode *p, const char *word, const char *name, const char *title,
                            char *err, size_t err_size) {
    if (strcmp(word, "docks") == 0 || strcmp(word, "dock") == 0) {
        TUI_ERR("the tui backend has no `docking` group");
        return NULL;
    }
    if (strcmp(word, "toolbar") == 0 || strcmp(word, "status") == 0) {
        TuiNode *n = tui_node_new(word, name, NULL);
        n->furniture = 1;
        n->title = tui_dup(name);
        tui_add_kid(p, n, -1);
        return n;
    }
    if (strcmp(word, "tab") == 0) {
        if (!tui_is(p, "tabs")) {
            TUI_ERR("a `tab` belongs in a `tabs`");
            return NULL;
        }
        TuiNode *n = tui_node_new(word, name, NULL);
        n->title = tui_dup(title);
        tui_add_kid(p, n, -1);
        return n;
    }
    if (!(strcmp(word, "tabs") == 0 || strcmp(word, "split") == 0 || strcmp(word, "scroll") == 0 ||
          strcmp(word, "cells") == 0 || (strcmp(word, "row") == 0 && tui_is(p, "cells")))) {
        return NULL;
    }
    if (tui_is(p, "tabs")) {
        TUI_ERR("a `tabs` holds `tab`s");
        return NULL;
    }
    TuiNode *n = tui_node_new(word, strcmp(word, "split") == 0 ? NULL : name, NULL);
    if (strcmp(word, "split") == 0) {
        n->direction = title && strcmp(title, "down") == 0;
    }
    if (strcmp(word, "scroll") == 0) {
        /* the scrolled content is a column of its own */
        tui_add_kid(p, n, -1);
        TuiNode *inner = tui_node_new("column", NULL, NULL);
        tui_add_kid(n, inner, -1);
        return inner;
    }
    tui_add_kid(p, n, -1);
    return n;
}

/* A part's place in a `cells` row: the next column, a captioned field
 * taking two (its caption, then itself), as Qt's grid does. */
static void tuip_place_in_grid(TuiNode *row, TuiNode *n) {
    int col = 0;
    for (int i = 0; i < row->nkids; i++) {
        TuiNode *k = row->kids[i];
        if (k == n) {
            break;
        }
        int end = k->grid_col + k->across;
        if (end > col) {
            col = end;
        }
    }
    n->grid_col = col + (tui_has_caption(n) ? 1 : 0);
    TuiNode *cells = row->parent;
    for (int r = 0; cells && r < cells->nkids; r++) {
        if (cells->kids[r] == row) {
            n->grid_row = r;
        }
    }
}

/* the leaves this file adds: `choice`, and a status bar's `text`/`stretch` */
static int tuip_part(TuiNode *p, const char *word, TuiNode *n, char *err, size_t err_size) {
    if (tui_is(p, "status") && strcmp(word, "text") != 0 && strcmp(word, "stretch") != 0) {
        return TUI_ERR("a status bar holds `text` and `stretch`, and `%s` is neither", word);
    }
    if (tui_is(p, "split") && strcmp(word, "field") == 0 && n->label && *n->label) {
        return TUI_ERR("a `field` that is a pane of a `split` has nowhere to put a label -- "
                       "put it in a `row` or a `form` in that pane");
    }
    if (strcmp(word, "choice") == 0) {
        n->index = -1;
    }
    if (tui_is(p, "row") && p->parent && tui_is(p->parent, "cells")) {
        tuip_place_in_grid(p, n);
    }
    return 1;
}

static void *tuip_grid_new(void *parent, const char *word, const char *name, const char *const *headers,
                           int columns, char *err, size_t err_size) {
    TuiNode *p = parent;
    if (!tui_box(p)) {
        TUI_ERR("that container cannot hold anything");
        return NULL;
    }
    if (columns < 0 || columns > 64) {
        TUI_ERR("a table has at most 64 columns here");
        return NULL;
    }
    TuiNode *t = tui_node_new(word, name, NULL);
    t->ncols = columns;
    t->headers = calloc((size_t)(columns ? columns : 1), sizeof(char *));
    if (!t->headers) {
        abort();
    }
    for (int c = 0; c < columns; c++) {
        t->headers[c] = copy_string(headers && headers[c] ? headers[c] : "");
        t->aligns[c] = 'l';
    }
    t->is_tree = strcmp(word, "tree") == 0;
    tui_add_kid(p, t, -1);
    if (tui_is(p, "row") && p->parent && tui_is(p->parent, "cells")) {
        tuip_place_in_grid(p, t);
    }
    return t;
}

static void *tui_table(void *parent, const char *name, const char *const *headers, int columns,
                       char *err, size_t err_size) {
    return tuip_grid_new(parent, "table", name, headers, columns, err, err_size);
}

static void *tui_tree(void *parent, const char *name, const char *const *headers, int columns,
                      char *err, size_t err_size) {
    return tuip_grid_new(parent, "tree", name, headers, columns, err, err_size);
}

static void *tui_virtual_table(void *parent, const char *name, const char *const *headers, int ncols,
                               const char *cell_fn, char *err, size_t err_size) {
    TuiNode *t = tuip_grid_new(parent, "table", name, headers, ncols, err, err_size);
    if (t) {
        t->is_virtual = 1;
        t->cell_fn = tui_dup(cell_fn);
    }
    return t;
}

static TuiNode *tuip_grid_named(void *window, const char *name) {
    TuiNode *t = tui_find(window, name);
    return t && tuip_is_grid(t) ? t : NULL;
}

static int tui_virtual_rows(void *window, const char *name, int rows, char *err, size_t err_size) {
    TuiNode *t = tuip_grid_named(window, name);
    if (!t || !t->is_virtual) {
        return TUI_ERR("`%s` is not a table that asks for its rows", name);
    }
    t->vrows = rows;
    t->top = 0;
    if (t->selected >= rows) {
        t->selected = -1;
    }
    tui_dirty = 1;
    return 1;
}

static int tui_virtual_row_count(void *window, const char *name, int *rows, char *err, size_t err_size) {
    TuiNode *t = tuip_grid_named(window, name);
    if (!t || !t->is_virtual) {
        return TUI_ERR("`%s` is not a table that asks for its rows", name);
    }
    *rows = (int)t->vrows;
    return 1;
}

static int tui_table_size(void *window, const char *name, int rows, char *err, size_t err_size) {
    TuiNode *t = tuip_grid_named(window, name);
    if (!t || t->is_virtual || t->is_tree) {
        return TUI_ERR("`%s` is not a table here", name);
    }
    if (rows < 0) {
        return TUI_ERR("a table cannot have fewer than no rows");
    }
    size_t old = (size_t)t->nrows * (size_t)t->ncols, now = (size_t)rows * (size_t)t->ncols;
    for (size_t i = now; i < old; i++) {
        free(t->cells[i]);
    }
    char **cells = realloc(t->cells, (now ? now : 1) * sizeof(char *));
    if (!cells) {
        abort();
    }
    for (size_t i = old; i < now; i++) {
        cells[i] = copy_string("");
    }
    t->cells = cells;
    int lost = t->selected >= rows;
    t->nrows = rows;
    tui_dirty = 1;
    if (lost) {
        /* the selection went with the rows, and that is announced once the
         * rows are gone, as Qt does after a shrink */
        tuip_select(t, rows ? rows - 1 : -1, 1);
    }
    return 1;
}

static int tui_table_set(void *window, const char *name, int row, int col, const char *text, char *err,
                         size_t err_size) {
    TuiNode *t = tuip_grid_named(window, name);
    if (!t || t->is_virtual || t->is_tree) {
        return TUI_ERR("`%s` is not a table here", name);
    }
    if (row < 0 || col < 0 || row >= t->nrows || col >= t->ncols) {
        return TUI_ERR("that cell is outside the table");
    }
    tui_set(&t->cells[(size_t)row * t->ncols + col], text);
    tui_dirty = 1;
    return 1;
}

static char *tui_table_get(void *window, const char *name, int row, int col) {
    TuiNode *t = tuip_grid_named(window, name);
    return t ? tuip_cell(t, row, col) : NULL;
}

static int tui_table_rows(void *window, const char *name) {
    TuiNode *t = tuip_grid_named(window, name);
    return t ? (int)tuip_rows(t) : -1;
}

static int tui_table_selected(void *window, const char *name) {
    TuiNode *t = tuip_grid_named(window, name);
    return t ? t->selected : -1;
}

static char *tui_table_heading(void *window, const char *name, int col) {
    TuiNode *t = tuip_grid_named(window, name);
    if (!t || col < 0 || col >= t->ncols) {
        return NULL;
    }
    return copy_string(t->headers[col]);
}

static int tui_table_select(void *window, const char *name, int row, char *err, size_t err_size) {
    TuiNode *t = tuip_grid_named(window, name);
    if (!t) {
        return TUI_ERR("`%s` is not a table here", name);
    }
    if (row >= tuip_rows(t)) {
        return TUI_ERR(t->is_tree ? "that tree has no such row" : "that row is outside the table");
    }
    tuip_select(t, row < 0 ? -1 : row, 1);
    return 1;
}

static int tui_column_align(void *table_node, int col, const char *align, char *err, size_t err_size) {
    TuiNode *t = table_node;
    if (!tuip_is_grid(t) || col < 0 || col >= t->ncols) {
        return TUI_ERR("that column is outside the table");
    }
    t->aligns[col] = strcmp(align, "right") == 0 ? 'r' : strcmp(align, "center") == 0 ? 'c' : 'l';
    tui_dirty = 1;
    return 1;
}

static const char *tui_cell_alignment(void *window, const char *name, int row, int col) {
    TuiNode *t = tuip_grid_named(window, name);
    if (!t || row < 0 || row >= tuip_rows(t) || col < 0 || col >= t->ncols) {
        return NULL;
    }
    return tuip_align_of(t, col);
}

static int tui_column_count(void *window, const char *name) {
    TuiNode *t = tuip_grid_named(window, name);
    return t ? t->ncols : -1;
}

static int tui_tree_clear(void *window, const char *name) {
    TuiNode *t = tuip_grid_named(window, name);
    if (!t || !t->is_tree) {
        return 0;
    }
    for (long i = 0; i < (long)t->nrows * t->ncols; i++) {
        free(t->cells[i]);
    }
    t->nrows = 0;
    t->top = 0;
    tui_dirty = 1;
    if (t->selected >= 0) {
        tuip_select(t, -1, 1);
    }
    return 1;
}

static int tui_tree_add(void *window, const char *name, int depth, const char *const *cells, int columns) {
    TuiNode *t = tuip_grid_named(window, name);
    if (!t || !t->is_tree) {
        return 0;
    }
    /* deeper than one below the last row: under the deepest there is */
    int prev = t->nrows ? t->depths[t->nrows - 1] : -1;
    if (depth < 0) depth = 0;
    if (depth > prev + 1) depth = prev + 1;
    if (t->nrows == t->rows_cap) {
        t->rows_cap = t->rows_cap ? t->rows_cap * 2 : 16;
        t->depths = realloc(t->depths, sizeof(int) * (size_t)t->rows_cap);
        if (!t->depths) {
            abort();
        }
    }
    char **grown = realloc(t->cells, sizeof(char *) * (size_t)(t->nrows + 1) * (size_t)(t->ncols ? t->ncols : 1));
    if (!grown) {
        abort();
    }
    t->cells = grown;
    for (int c = 0; c < t->ncols; c++) {
        t->cells[(size_t)t->nrows * t->ncols + c] = copy_string(c < columns && cells[c] ? cells[c] : "");
    }
    t->depths[t->nrows++] = depth;
    tui_dirty = 1;
    return 1;
}

static int tui_tree_depth(void *window, const char *name, int row) {
    TuiNode *t = tuip_grid_named(window, name);
    if (!t || !t->is_tree) {
        return -2;
    }
    if (row < 0 || row >= t->nrows) {
        return -1;
    }
    return t->depths[row];
}

/* the tree members the front reaches through the table ones */
static int tui_tree_rows(void *window, const char *name) {
    return tui_table_rows(window, name);
}

static char *tui_tree_get(void *window, const char *name, int row, int col) {
    return tui_table_get(window, name, row, col);
}

static char *tui_tree_heading(void *window, const char *name, int col) {
    return tui_table_heading(window, name, col);
}

static int tui_tree_selected(void *window, const char *name) {
    return tui_table_selected(window, name);
}

static int tui_tree_select(void *window, const char *name, int row, char *err, size_t err_size) {
    return tui_table_select(window, name, row, err, err_size);
}

/* ---- choice ---------------------------------------------------------------------- */

static TuiNode *tuip_choice_named(void *window, const char *name, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (n && !tui_is(n, "choice")) {
        TUI_ERR("`%s` is not a `choice`, so it holds no list", name);
        return NULL;
    }
    return n;
}

static int tui_choice_clear(void *window, const char *name, char *err, size_t err_size) {
    TuiNode *c = tuip_choice_named(window, name, err, err_size);
    if (!c) {
        return 0;
    }
    for (int i = 0; i < c->nitems; i++) {
        free(c->items[i]);
    }
    c->nitems = 0;
    c->index = -1;
    tui_dirty = 1;
    return 1;
}

static int tui_choice_add(void *window, const char *name, const char *item, char *err, size_t err_size) {
    TuiNode *c = tuip_choice_named(window, name, err, err_size);
    if (!c) {
        return 0;
    }
    char **items = realloc(c->items, sizeof(char *) * (size_t)(c->nitems + 1));
    if (!items) {
        abort();
    }
    c->items = items;
    c->items[c->nitems++] = copy_string(item);
    if (c->index < 0) {
        c->index = 0;    /* the first item shows, as a combo box shows it */
    }
    tui_dirty = 1;
    return 1;
}

/* ---- menus and popups ---------------------------------------------------------------- */

static void *tui_menu(void *window, void *parent_menu, const char *title, char *err, size_t err_size) {
    TuiNode *root = window;
    TuiNode *parent = parent_menu;
    if (!parent) {
        parent = tuip_menubar(root);
        if (!parent) {
            parent = tui_node_new("menubar", NULL, NULL);
            parent->furniture = 1;
            tui_add_kid(root, parent, -1);
        }
    } else if (!(tui_is(parent, "menu") || tui_is(parent, "popup"))) {
        TUI_ERR("a menu belongs on the window, in a menu or in a popup");
        return NULL;
    }
    TuiNode *m = tui_node_new("menu", NULL, NULL);
    m->title = tui_dup(title);
    tui_add_kid(parent, m, -1);
    return m;
}

static void *tui_menu_item(void *window, void *menu, const char *name, const char *label, char *err,
                           size_t err_size) {
    (void)window;
    TuiNode *m = menu;
    if (!(tui_is(m, "menu") || tui_is(m, "popup") || tui_is(m, "toolbar"))) {
        TUI_ERR("an item belongs in a menu, a popup or a toolbar");
        return NULL;
    }
    TuiNode *it = tui_node_new("item", name, label);
    it->text = tui_dup(label ? label : "");
    tui_add_kid(m, it, -1);
    return it;
}

static int tui_menu_separator(void *menu, char *err, size_t err_size) {
    TuiNode *m = menu;
    if (!(tui_is(m, "menu") || tui_is(m, "popup") || tui_is(m, "toolbar"))) {
        return TUI_ERR("a separator here belongs in a menu, a popup or a toolbar");
    }
    tui_add_kid(m, tui_node_new("separator", NULL, NULL), -1);
    return 1;
}

static void *tui_popup_menu(void *window, const char *name, char *err, size_t err_size) {
    (void)err;
    (void)err_size;
    TuiNode *p = tui_node_new("popup", name, NULL);
    p->furniture = 1;
    tui_add_kid(window, p, -1);
    return p;
}

static int tui_menu_count(void *window) {
    TuiNode *bar = tuip_menubar(window);
    return bar ? bar->nkids : 0;
}

static char *tui_menu_title(void *window, int index) {
    TuiNode *bar = tuip_menubar(window);
    if (!bar || index < 0 || index >= bar->nkids) {
        return NULL;
    }
    return copy_string(bar->kids[index]->title ? bar->kids[index]->title : "");
}

static int tuip_entries(TuiNode *m) {
    return m ? m->nkids : -1;
}

static char *tuip_entry_title(TuiNode *m, int entry, int *is_separator) {
    if (!m || entry < 0 || entry >= m->nkids) {
        return NULL;
    }
    TuiNode *e = m->kids[entry];
    *is_separator = tui_is(e, "separator");
    if (*is_separator) {
        return NULL;
    }
    return copy_string(tui_is(e, "menu") ? (e->title ? e->title : "") : (e->text ? e->text : ""));
}

static int tui_menu_entry_count(void *window, int index) {
    TuiNode *bar = tuip_menubar(window);
    if (!bar || index < 0 || index >= bar->nkids) {
        return -1;
    }
    return tuip_entries(bar->kids[index]);
}

static char *tui_menu_entry_title(void *window, int index, int entry, int *is_separator) {
    TuiNode *bar = tuip_menubar(window);
    if (!bar || index < 0 || index >= bar->nkids) {
        return NULL;
    }
    return tuip_entry_title(bar->kids[index], entry, is_separator);
}

static TuiNode *tuip_popup_named(void *window, const char *name) {
    TuiNode *n = tui_find(window, name);
    return n && tui_is(n, "popup") ? n : NULL;
}

static int tui_popup_entry_count(void *window, const char *name) {
    TuiNode *p = tuip_popup_named(window, name);
    return p ? tuip_entries(p) : -1;
}

static char *tui_popup_entry_title(void *window, const char *name, int entry, int *is_separator) {
    return tuip_entry_title(tuip_popup_named(window, name), entry, is_separator);
}

/* whether a menu would show anything: a visible item, or a submenu that would */
static int tuip_shows_anything(TuiNode *m) {
    for (int i = 0; i < m->nkids; i++) {
        TuiNode *e = m->kids[i];
        if (!e->visible) {
            continue;
        }
        if (tui_is(e, "item") || (tui_is(e, "menu") && tuip_shows_anything(e))) {
            return 1;
        }
    }
    return 0;
}

static int tui_popup(void *window, const char *name, const char *under, int *shown, char *err,
                     size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    if (!tui_is(n, "popup")) {
        return TUI_ERR("`%s` is a `%s`, not a popup menu", name, tui_is(n, "window") ? "window" : n->word);
    }
    int x = 2, y = 2;
    if (under) {
        TuiNode *u = tui_find(window, under);
        if (!u) {
            return TUI_ERR("there is nothing called `%s` to pop under", under);
        }
        tui_render();
        x = u->x;
        y = u->y + 1;
    }
    *shown = tuip_shows_anything(n);
    n->x = x * 8;
    n->y = y * 16;
    if (*shown) {
        tuip_overlay_close_all();
        tuip_overlay_push(n, x, y, -1);
        tui_render();
    }
    return 1;
}

/* ---- tabs ------------------------------------------------------------------------- */

static int tui_which_tab(void) {
    return tuip_which;
}

static int tui_tab_count(void *window, const char *name) {
    TuiNode *n = tui_find(window, name);
    return n && tui_is(n, "tabs") ? n->nkids : -1;
}

static char *tui_tab_caption(void *window, const char *name, int index) {
    TuiNode *n = tui_find(window, name);
    if (!n || !tui_is(n, "tabs") || index < 0 || index >= n->nkids) {
        return NULL;
    }
    return copy_string(n->kids[index]->title ? n->kids[index]->title : "");
}

/* ---- split, scroll, cells -------------------------------------------------------------- */

/* The nearest split or scroll above a part, and which of its panes holds it. */
static TuiNode *tuip_region(TuiNode *n, int *index) {
    TuiNode *child = n;
    for (TuiNode *p = n->parent; p; child = p, p = p->parent) {
        if (tui_is(p, "split")) {
            *index = 0;
            for (int i = 0; i < p->nkids; i++) {
                if (p->kids[i] == child) {
                    *index = i;
                }
            }
            return p;
        }
        if (tui_is(p, "scroll")) {
            *index = 0;
            return p;
        }
    }
    return NULL;
}

static int tui_pane_of(void *window, const char *name, char **kind, char **direction, int *panes,
                       int *index, int *resizable) {
    TuiNode *n = tui_find(window, name);
    if (!n) {
        return 0;
    }
    int at = 0;
    TuiNode *r = tuip_region(n, &at);
    if (!r) {
        return 0;
    }
    if (tui_is(r, "split")) {
        *kind = copy_string("split");
        *direction = copy_string(r->direction ? "down" : "across");
        *panes = r->nkids;
        *index = at;
        *resizable = 1;
    } else {
        *kind = copy_string("scroll");
        *direction = NULL;
        *panes = 1;
        *index = 0;
        *resizable = 1;
    }
    return 1;
}

static int tui_pane_sizes(void *window, const char *name, int *out, int *count, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    int at;
    TuiNode *r = tuip_region(n, &at);
    if (!r || !tui_is(r, "split")) {
        return TUI_ERR("`%s` is not in a split", name);
    }
    if (*count == 0) {
        *count = r->nkids;
        return 1;
    }
    tui_render();
    for (int i = 0; i < r->nkids && i < *count; i++) {
        TuiNode *k = r->kids[i];
        out[i] = r->direction ? k->h * 16 : k->w * 8;
    }
    *count = r->nkids;
    return 1;
}

static int tui_resize_panes(void *window, const char *name, const int *sizes, int count, char *err,
                            size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    int at;
    TuiNode *r = tuip_region(n, &at);
    if (!r || !tui_is(r, "split")) {
        return TUI_ERR("`%s` is not in a split", name);
    }
    if (count != r->nkids) {
        return TUI_ERR("that split has %d panes and %d size%s given", r->nkids, count,
                       count == 1 ? " was" : "s were");
    }
    for (int i = 0; i < count; i++) {
        if (sizes[i] < 0) {
            return TUI_ERR("a pane cannot be %d %s", sizes[i], r->direction ? "high" : "wide");
        }
    }
    for (int i = 0; i < count && i < 16; i++) {
        r->sizes[i] = sizes[i];
    }
    r->nsizes = count;
    tui_dirty = 1;
    tui_render();
    return 1;
}

static int tui_scrolled(void *window, const char *name, double *at, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    if (tui_is(n, "column") && n->parent && tui_is(n->parent, "scroll")) {
        n = n->parent;
    }
    if (tui_is(n, "scroll")) {
        *at = n->offset * 16;
        return 2;
    }
    if (tuip_is_grid(n)) {
        *at = n->top * 16;
        return 2;
    }
    return 1;
}

static int tui_scroll_to(void *window, const char *name, double at, char *err, size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    if (tui_is(n, "scroll")) {
        n->offset = (int)(at / 16);
        tui_dirty = 1;
        tui_render();
        return 1;
    }
    if (tuip_is_grid(n)) {
        n->top = (int)(at / 16);
        tui_dirty = 1;
        tui_render();
        return 1;
    }
    return TUI_ERR("`%s` does not scroll", name);
}

static int tui_scroll(void *window, const char *name, double notches, int mods, int *claimed, char *err,
                      size_t err_size) {
    TuiNode *n = tui_named(window, name, err, err_size);
    if (!n) {
        return 0;
    }
    tui_render();
    *claimed = tuip_wheel_at(n, notches, mods);
    tui_render();
    return 1;
}

static int tui_wheel_event(int *dy, int *dx, int *ctrl, int *shift, int *alt) {
    if (!tuip_wheel_live) {
        return 0;
    }
    *dy = (int)(tuip_wheel_dy >= 0 ? ceil(tuip_wheel_dy) : floor(tuip_wheel_dy));
    *dx = (int)(tuip_wheel_dx >= 0 ? ceil(tuip_wheel_dx) : floor(tuip_wheel_dx));
    *ctrl = tuip_wheel_ctrl;
    *shift = tuip_wheel_shift;
    *alt = tuip_wheel_alt;
    return 1;
}

static int tui_wheel_exact(double *dy, double *dx) {
    if (!tuip_wheel_live) {
        return 0;
    }
    *dy = tuip_wheel_dy;
    *dx = tuip_wheel_dx;
    return 1;
}

static int tui_swallow(char *err, size_t err_size) {
    if (!tuip_wheel_live) {
        return TUI_ERR("no event is being handled");
    }
    tuip_wheel_swallowed = 1;
    return 1;
}

static int tui_span(void *widget, long across, long down, char *err, size_t err_size) {
    TuiNode *n = widget;
    if (!(n->parent && tui_is(n->parent, "row") && n->parent->parent && tui_is(n->parent->parent, "cells"))) {
        return TUI_ERR("`across:` and `down:` only mean something inside `cells`");
    }
    if (across < 1 || down < 1) {
        return TUI_ERR("a span covers at least one cell");
    }
    n->across = (int)across;
    n->down = (int)down;
    tui_dirty = 1;
    return 1;
}

static int tui_cell_of(void *window, const char *name, int *row, int *col, int *across, int *down) {
    TuiNode *n = tui_find(window, name);
    if (!n || n->grid_col < 0 || n->grid_row < 0) {
        return 0;
    }
    *row = n->grid_row;
    *col = n->grid_col;
    *across = n->across;
    *down = n->down;
    return 1;
}

/* ---- properties, events and drive verbs for these parts -------------------------------- */

/* Each answers -1 when the part or word is not this file's, so the core's
 * own answer stands. */
static int tuip_set_text(TuiNode *n, const char *prop, const char *value, char *err, size_t err_size) {
    if (tui_is(n, "item") && strcmp(prop, "text") == 0) {
        tui_set(&n->text, value);
        return 1;
    }
    if (tui_is(n, "item") && strcmp(prop, "shortcut") == 0) {
        tui_set(&n->shortcut, value);
        return 1;
    }
    if (tui_is(n, "item") && strcmp(prop, "icon") == 0) {
        return 1;
    }
    if (tui_is(n, "choice") && strcmp(prop, "text") == 0) {
        for (int i = 0; i < n->nitems; i++) {
            if (strcmp(n->items[i], value) == 0) {
                n->index = i;
                tui_dirty = 1;
                return 1;
            }
        }
        return TUI_ERR("`%s` has no choice `%s` -- fill its list with ui.options first",
                       n->name ? n->name : "choice", value);
    }
    if (tui_is(n, "tabs") && strcmp(prop, "tab_side") == 0) {
        if (strcmp(value, "top") && strcmp(value, "bottom")) {
            return TUI_ERR("`tab_side:` is top or bottom here");
        }
        n->tab_side_bottom = strcmp(value, "bottom") == 0;
        tui_dirty = 1;
        return 1;
    }
    if (tui_is(n, "toolbar") && strcmp(prop, "toolbar_shows") == 0) {
        snprintf(n->toolbar_shows, sizeof n->toolbar_shows, "%s", value);
        return 1;
    }
    if (tui_is(n, "toolbar") && strcmp(prop, "title") == 0) {
        tui_set(&n->title, value);
        return 1;
    }
    return -1;
}

static int tuip_set_bool(TuiNode *n, const char *prop, int value, char *err, size_t err_size) {
    (void)err;
    (void)err_size;
    int *slot = NULL;
    if (tuip_is_grid(n)) {
        if (strcmp(prop, "headings") == 0) slot = &n->headings;
        else if (strcmp(prop, "striped") == 0) slot = &n->striped;
        else if (strcmp(prop, "sortable") == 0) slot = &n->sortable;
        else if (strcmp(prop, "expanders") == 0 && n->is_tree) slot = &n->expanders;
        else if (strcmp(prop, "read_only") == 0) slot = &n->display_only;
    } else if (tui_is(n, "item")) {
        if (strcmp(prop, "checkable") == 0) slot = &n->checkable;
        else if (strcmp(prop, "checked") == 0) slot = &n->checked;
    } else if (tui_is(n, "tabs")) {
        if (strcmp(prop, "movable") == 0) slot = &n->movable;
        else if (strcmp(prop, "closable") == 0) slot = &n->closable;
    }
    if (!slot) {
        return -1;
    }
    *slot = value;
    tui_dirty = 1;
    return 1;
}

static int tuip_set_number(TuiNode *n, const char *prop, double value, char *err, size_t err_size) {
    if (tuip_is_grid(n) && n->is_tree && strcmp(prop, "indent") == 0) {
        n->indent = tui_cells_across(value);
        if (n->indent < 1) n->indent = 1;
        tui_dirty = 1;
        return 1;
    }
    if (tui_is(n, "tabs") && strcmp(prop, "index") == 0) {
        int to = (int)value;
        if (to < 0 || to >= n->nkids) {
            return TUI_ERR("that `tabs` has no tab %d", to);
        }
        int changed = to != n->index;
        n->index = to;
        tui_dirty = 1;
        if (changed) {
            tuip_fire_tab(n->on_changed, to);
        }
        return 1;
    }
    if (tui_is(n, "choice") && strcmp(prop, "index") == 0) {
        int to = (int)value;
        if (to < 0 || to >= n->nitems) {
            return TUI_ERR("that `choice` has no item %d", to);
        }
        int changed = to != n->index;
        n->index = to;
        tui_dirty = 1;
        if (changed) {
            tuip_fire_copy(n->on_changed);
        }
        return 1;
    }
    return -1;
}

static int tuip_get(TuiNode *n, const char *prop, char *kind, double *num, char **text) {
#define TUIP_B(v) do { *kind = 'b'; *num = (v); return 1; } while (0)
#define TUIP_N(v) do { *kind = 'n'; *num = (v); return 1; } while (0)
#define TUIP_T(v) do { *kind = 't'; *text = copy_string((v) ? (v) : ""); return 1; } while (0)
    if (tuip_is_grid(n)) {
        if (strcmp(prop, "columns") == 0) TUIP_N(n->ncols);
        if (strcmp(prop, "count") == 0) {
            if (n->is_tree) {
                int top = 0;
                for (int i = 0; i < n->nrows; i++) {
                    top += n->depths[i] == 0;
                }
                TUIP_N(top);
            }
            TUIP_N(tuip_rows(n));
        }
        if (strcmp(prop, "read_only") == 0) TUIP_B(n->display_only || n->is_virtual || n->is_tree);
        if (strcmp(prop, "headings") == 0) TUIP_B(n->headings);
        if (strcmp(prop, "striped") == 0) TUIP_B(n->striped);
        if (strcmp(prop, "sortable") == 0) TUIP_B(n->sortable);
        if (n->is_tree && strcmp(prop, "expanders") == 0) TUIP_B(n->expanders);
        if (n->is_tree && strcmp(prop, "indent") == 0) TUIP_N(n->indent * 8);
    }
    if (tui_is(n, "choice")) {
        if (strcmp(prop, "index") == 0) TUIP_N(n->index);
        if (strcmp(prop, "count") == 0) TUIP_N(n->nitems);
        if (strcmp(prop, "text") == 0) TUIP_T(n->index >= 0 && n->index < n->nitems ? n->items[n->index] : "");
    }
    if (tui_is(n, "item")) {
        if (strcmp(prop, "text") == 0) TUIP_T(n->text);
        if (strcmp(prop, "shortcut") == 0) TUIP_T(n->shortcut);
        if (strcmp(prop, "checkable") == 0) TUIP_B(n->checkable);
        if (strcmp(prop, "checked") == 0) TUIP_B(n->checked);
    }
    if (tui_is(n, "tabs")) {
        if (strcmp(prop, "count") == 0) TUIP_N(n->nkids);
        if (strcmp(prop, "index") == 0) TUIP_N(n->index);
        if (strcmp(prop, "movable") == 0) TUIP_B(n->movable);
        if (strcmp(prop, "closable") == 0) TUIP_B(n->closable);
        if (strcmp(prop, "tab_side") == 0) TUIP_T(n->tab_side_bottom ? "bottom" : "top");
    }
    if (tui_is(n, "toolbar")) {
        if (strcmp(prop, "title") == 0) TUIP_T(n->title);
        if (strcmp(prop, "toolbar_shows") == 0) TUIP_T(n->toolbar_shows[0] ? n->toolbar_shows : "icons");
    }
    if (tui_is(n, "popup")) {
        if (strcmp(prop, "x") == 0) TUIP_N(n->x);
        if (strcmp(prop, "y") == 0) TUIP_N(n->y);
    }
#undef TUIP_B
#undef TUIP_N
#undef TUIP_T
    return -1;
}

static char **tuip_event_slot(TuiNode *n, const char *signal) {
    if (strcmp(signal, "chosen") == 0 && tuip_is_grid(n)) return &n->on_chosen;
    if (strcmp(signal, "activated") == 0 && (tuip_is_grid(n) || tui_is(n, "choice"))) return &n->on_activated;
    if (strcmp(signal, "changed") == 0 && (tui_is(n, "choice") || tui_is(n, "tabs") || tui_is(n, "item"))) return &n->on_changed;
    if (strcmp(signal, "pressed") == 0 && tui_is(n, "item")) return &n->on_pressed;
    if (strcmp(signal, "wheel") == 0) return &n->on_wheel;
    return NULL;
}

/* ui.click and ui.right_click aimed at a row or a tab */
static int tuip_click_row(TuiNode *n, int row, int right, char *err, size_t err_size) {
    if (tuip_is_grid(n)) {
        if (row >= tuip_rows(n)) {
            return TUI_ERR(n->is_tree ? "that tree has no such row" : "that table has no such row");
        }
        tui_focus = n;
        tuip_select(n, row, 1);
        tuip_fire_copy(right ? n->on_context : n->on_activated);
        return 1;
    }
    if (tui_is(n, "tabs")) {
        if (row >= n->nkids) {
            return TUI_ERR("that has no such tab");
        }
        if (right) {
            tuip_fire_tab(n->on_context, row);
        } else if (n->index != row) {
            n->index = row;
            tui_dirty = 1;
            tuip_fire_tab(n->on_changed, row);
        }
        return 1;
    }
    if (tui_is(n, "item")) {
        return TUI_ERR("`%s` is an `item`, which has nothing to click", n->name ? n->name : "item");
    }
    return TUI_ERR("`%s` is not a table, a tree or a `tabs`, so it has no row or tab to click",
                   n->name ? n->name : n->word);
}

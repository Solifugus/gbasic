#include "btengine.h"

#include <stdlib.h>

void be_txn_init(BeTxn *t, BtPager *pg, uint64_t catalog, int write) {
    memset(t, 0, sizeof *t);
    t->pg = pg;
    t->catalog = catalog;
    t->write = write;
}

static void op_end(BeTxn *t) {
    if (t->after_op) t->after_op(t);
}

/* A status that leaves the tree half-changed: only abort is safe after it. */
static int poison(BeTxn *t, int rc) {
    if (rc != GS_OK && rc != GS_NOT_FOUND && rc != GS_EXISTS && rc != GS_ORDER && rc != GS_TOO_LARGE &&
        rc != GS_READ_ONLY && rc != GS_MISUSE && rc != GS_INVALIDATED && !t->failed)
        t->failed = rc;
    return rc;
}

static int find(BeTxn *t, GsBytes name, size_t *idx) {
    for (size_t i = 0; i < t->ntrees; i++)
        if (bytes_cmp(gsb(t->trees[i].name.p, t->trees[i].name.n), name) == 0) {
            *idx = i;
            return GS_OK;
        }
    if (name.n > bt_max_key(t->pg->page_size)) return GS_TOO_LARGE;
    if (t->ntrees == t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 8;
        BeTree *v = realloc(t->trees, cap * sizeof *v);
        if (!v) return GS_IO;
        t->trees = v;
        t->cap = cap;
    }
    BeTree *tr = &t->trees[t->ntrees];
    memset(tr, 0, sizeof *tr);
    if (!gsbuf_set(&tr->name, name.p, name.n)) return GS_IO;
    GsBuf v = {0};
    int rc = bt_get(t->pg, t->catalog, name, &v);
    if (rc == GS_OK) {
        if (v.n != BE_ENTRY) { gsbuf_free(&v); gsbuf_free(&tr->name); return GS_CORRUPT; }
        tr->root = get64(v.p);
        tr->id = get64(v.p + 8);
        tr->exists = 1;
    } else if (rc != GS_NOT_FOUND) {
        gsbuf_free(&v);
        gsbuf_free(&tr->name);
        return rc;
    }
    gsbuf_free(&v);
    *idx = t->ntrees++;
    return GS_OK;
}

static int existing(BeTxn *t, GsBytes name, BeTree **out) {
    size_t i;
    int rc = find(t, name, &i);
    if (rc) return rc;
    if (!t->trees[i].exists) return GS_NOT_FOUND;
    *out = &t->trees[i];
    return GS_OK;
}

int be_flush(BeTxn *t) {
    for (size_t i = 0; i < t->ntrees; i++) {
        BeTree *tr = &t->trees[i];
        if (!tr->dirty) continue;
        int rc;
        if (tr->exists) {
            unsigned char e[BE_ENTRY];
            put64(e, tr->root);
            put64(e + 8, tr->id);
            e[16] = 1;
            rc = bt_put(t->pg, &t->catalog, gsb(tr->name.p, tr->name.n), gsb(e, BE_ENTRY), 0);
        } else {
            rc = bt_del(t->pg, &t->catalog, gsb(tr->name.p, tr->name.n));
            if (rc == GS_NOT_FOUND) rc = GS_OK; /* created and dropped in this txn */
        }
        if (rc) return poison(t, rc);
        tr->dirty = 0;
    }
    return GS_OK;
}

void be_cursor_close(BeCursor *c) {
    if (!c) return;
    BeCursor **pp = &c->t->cursors;
    while (*pp && *pp != c) pp = &(*pp)->next;
    if (*pp) *pp = c->next;
    bt_cursor_free(&c->bc);
    gsbuf_free(&c->gone);
    free(c);
}

void be_txn_free(BeTxn *t) {
    while (t->cursors) be_cursor_close(t->cursors);
    for (size_t i = 0; i < t->ntrees; i++) gsbuf_free(&t->trees[i].name);
    free(t->trees);
    gsbuf_free(&t->value);
    t->trees = NULL;
    t->ntrees = t->cap = 0;
}

int be_roots(BtPager *pg, uint64_t catalog, int (*each)(void *, GsBytes, uint64_t), void *ctx) {
    BtCursor c;
    bt_cursor_init(&c, pg, catalog);
    int rc;
    GsBuf v = {0};
    for (rc = bt_first(&c); rc == GS_OK && c.positioned; rc = bt_next(&c)) {
        if ((rc = bt_cursor_value(&c))) break;
        if (c.value.n != BE_ENTRY) { rc = GS_CORRUPT; break; }
        if (each(ctx, gsb(c.key.p, c.key.n), get64(c.value.p))) break;
    }
    gsbuf_free(&v);
    bt_cursor_free(&c);
    return rc;
}

/* The next tree id: one more than any in the catalog or created in this txn. */
static int next_id(BeTxn *t, uint64_t *id) {
    uint64_t best = 0;
    BtCursor c;
    bt_cursor_init(&c, t->pg, t->catalog);
    int rc;
    for (rc = bt_first(&c); rc == GS_OK && c.positioned; rc = bt_next(&c)) {
        if ((rc = bt_cursor_value(&c))) break;
        if (c.value.n == BE_ENTRY && get64(c.value.p + 8) > best) best = get64(c.value.p + 8);
    }
    bt_cursor_free(&c);
    for (size_t i = 0; i < t->ntrees; i++)
        if (t->trees[i].id > best) best = t->trees[i].id;
    *id = best + 1;
    return rc;
}

int be_tree_create(BeTxn *t, GsBytes name) {
    if (!t->write) return GS_READ_ONLY;
    if (t->failed) return t->failed;
    size_t i;
    int rc = find(t, name, &i);
    if (rc) return rc;
    if (t->trees[i].exists) return GS_EXISTS;
    uint64_t root, id;
    if ((rc = next_id(t, &id))) return poison(t, rc);
    if ((rc = bt_create(t->pg, &root))) return poison(t, rc);
    BeTree *tr = &t->trees[i];
    tr->root = root;
    tr->id = id;
    tr->exists = tr->dirty = tr->created = 1;
    tr->dropped = 0;
    tr->mods++;
    op_end(t);
    return GS_OK;
}

int be_tree_drop(BeTxn *t, GsBytes name) {
    if (!t->write) return GS_READ_ONLY;
    if (t->failed) return t->failed;
    BeTree *tr;
    int rc = existing(t, name, &tr);
    if (rc) return rc;
    if ((rc = bt_drop(t->pg, tr->root))) return poison(t, rc);
    tr->exists = 0;
    tr->dirty = tr->dropped = 1;
    tr->mods++;
    op_end(t);
    return GS_OK;
}

int be_tree_replace(BeTxn *t, GsBytes target, GsBytes source) {
    if (!t->write) return GS_READ_ONLY;
    if (t->failed) return t->failed;
    if (bytes_cmp(target, source) == 0) return GS_MISUSE;
    size_t ti, si;
    int rc = find(t, target, &ti);
    if (rc) return rc;
    if ((rc = find(t, source, &si))) return rc;
    BeTree *tt = &t->trees[ti], *st = &t->trees[si]; /* find may realloc; take both after */
    if (!st->exists) return GS_NOT_FOUND;
    if (tt->exists && (rc = bt_drop(t->pg, tt->root))) return poison(t, rc);
    tt->root = st->root;
    tt->id = st->id;
    tt->exists = tt->dirty = 1;
    tt->mods++;
    st->exists = 0;
    st->dirty = 1;
    st->mods++;
    op_end(t);
    return GS_OK;
}

int be_tree_list(BeTxn *t, int (*each)(void *, GsBytes), void *ctx) {
    int rc = GS_OK;
    if (t->write && (rc = be_flush(t))) return rc;
    BtCursor c;
    bt_cursor_init(&c, t->pg, t->catalog);
    for (rc = bt_first(&c); rc == GS_OK && c.positioned; rc = bt_next(&c))
        if (each(ctx, gsb(c.key.p, c.key.n))) break;
    bt_cursor_free(&c);
    op_end(t);
    return rc;
}

int be_get(BeTxn *t, GsBytes tree, GsBytes key, GsBytes *value) {
    BeTree *tr;
    int rc = existing(t, tree, &tr);
    if (rc) return rc;
    rc = bt_get(t->pg, tr->root, key, &t->value);
    if (rc == GS_OK) *value = gsb(t->value.p, t->value.n);
    op_end(t);
    return rc;
}

static int modify_prelude(BeTxn *t, GsBytes tree, GsBytes key, BeTree **tr) {
    if (!t->write) return GS_READ_ONLY;
    if (t->failed) return t->failed;
    if (key.n > bt_max_key(t->pg->page_size)) return GS_TOO_LARGE;
    return existing(t, tree, tr);
}

int be_put(BeTxn *t, GsBytes tree, GsBytes key, GsBytes value, unsigned flags) {
    BeTree *tr;
    int rc = modify_prelude(t, tree, key, &tr);
    if (rc) return rc;
    uint64_t root = tr->root;
    rc = bt_put(t->pg, &root, key, value, flags);
    if (rc == GS_OK) {
        if (root != tr->root) tr->dirty = 1;
        tr->root = root;
        tr->mods++;
    }
    op_end(t);
    return poison(t, rc);
}

int be_del(BeTxn *t, GsBytes tree, GsBytes key) {
    BeTree *tr;
    int rc = modify_prelude(t, tree, key, &tr);
    if (rc) return rc;
    uint64_t root = tr->root;
    rc = bt_del(t->pg, &root, key);
    if (rc == GS_OK) {
        if (root != tr->root) tr->dirty = 1;
        tr->root = root;
        tr->mods++;
    }
    op_end(t);
    return poison(t, rc);
}

/* ---- cursors ------------------------------------------------------------ */

int be_cursor_open(BeTxn *t, GsBytes tree, BeCursor **out) {
    size_t i;
    int rc = find(t, tree, &i);
    if (rc) return rc;
    if (!t->trees[i].exists) return GS_NOT_FOUND;
    BeCursor *c = calloc(1, sizeof *c);
    if (!c) return GS_IO;
    c->t = t;
    c->tree = i;
    c->mods = t->trees[i].mods;
    bt_cursor_init(&c->bc, t->pg, t->trees[i].root);
    c->next = t->cursors;
    t->cursors = c;
    *out = c;
    return GS_OK;
}

static int live(BeCursor *c) {
    BeTree *tr = &c->t->trees[c->tree];
    if (!tr->exists || tr->mods != c->mods) return GS_INVALIDATED;
    c->bc.root = tr->root;
    return GS_OK;
}

static int done(BeCursor *c, int rc, int *pos) {
    if (pos) *pos = rc == GS_OK && c->bc.positioned;
    op_end(c->t);
    return rc;
}

int be_seek(BeCursor *c, GsBytes key, GsSeek mode, int *pos) {
    int rc = live(c);
    if (rc) return rc;
    c->between = 0;
    return done(c, bt_seek(&c->bc, key, mode), pos);
}

int be_first(BeCursor *c, int *pos) {
    int rc = live(c);
    if (rc) return rc;
    c->between = 0;
    return done(c, bt_first(&c->bc), pos);
}

int be_last(BeCursor *c, int *pos) {
    int rc = live(c);
    if (rc) return rc;
    c->between = 0;
    return done(c, bt_last(&c->bc), pos);
}

int be_next(BeCursor *c, int *pos) {
    int rc = live(c);
    if (rc) return rc;
    if (c->between) {
        c->between = 0;
        return done(c, bt_seek(&c->bc, gsb(c->gone.p, c->gone.n), GS_SEEK_GT), pos);
    }
    return done(c, bt_next(&c->bc), pos);
}

int be_prev(BeCursor *c, int *pos) {
    int rc = live(c);
    if (rc) return rc;
    if (c->between) {
        c->between = 0;
        return done(c, bt_seek(&c->bc, gsb(c->gone.p, c->gone.n), GS_SEEK_LT), pos);
    }
    return done(c, bt_prev(&c->bc), pos);
}

int be_key(BeCursor *c, GsBytes *key) {
    int rc = live(c);
    if (rc) return rc;
    if (!c->bc.positioned) return GS_NOT_FOUND;
    *key = gsb(c->bc.key.p, c->bc.key.n);
    return GS_OK;
}

int be_value(BeCursor *c, GsBytes *value) {
    int rc = live(c);
    if (rc) return rc;
    if (!c->bc.positioned) return GS_NOT_FOUND;
    rc = bt_cursor_value(&c->bc);
    if (rc == GS_OK) *value = gsb(c->bc.value.p, c->bc.value.n);
    op_end(c->t);
    return rc;
}

int be_cursor_put(BeCursor *c, GsBytes value) {
    BeTxn *t = c->t;
    if (!t->write) return GS_READ_ONLY;
    if (t->failed) return t->failed;
    int rc = live(c);
    if (rc) return rc;
    if (!c->bc.positioned) return GS_NOT_FOUND;
    BeTree *tr = &t->trees[c->tree];
    GsBuf k = {0};
    gsbuf_set(&k, c->bc.key.p, c->bc.key.n);
    uint64_t root = tr->root;
    rc = bt_put(t->pg, &root, gsb(k.p, k.n), value, 0);
    if (rc == GS_OK) {
        if (root != tr->root) tr->dirty = 1;
        tr->root = root;
        tr->mods++;
        c->mods = tr->mods; /* a write through the cursor keeps it valid */
        c->bc.root = root;
        rc = bt_seek(&c->bc, gsb(k.p, k.n), GS_SEEK_EXACT);
    }
    gsbuf_free(&k);
    op_end(t);
    return poison(t, rc);
}

int be_cursor_delete(BeCursor *c) {
    BeTxn *t = c->t;
    if (!t->write) return GS_READ_ONLY;
    if (t->failed) return t->failed;
    int rc = live(c);
    if (rc) return rc;
    if (!c->bc.positioned) return GS_NOT_FOUND;
    BeTree *tr = &t->trees[c->tree];
    gsbuf_set(&c->gone, c->bc.key.p, c->bc.key.n);
    uint64_t root = tr->root;
    rc = bt_del(t->pg, &root, gsb(c->gone.p, c->gone.n));
    if (rc == GS_OK) {
        if (root != tr->root) tr->dirty = 1;
        tr->root = root;
        tr->mods++;
        c->mods = tr->mods;
        c->bc.root = root;
        c->bc.positioned = 0;
        c->between = 1;
    }
    op_end(t);
    return poison(t, rc);
}

/* G-SQL's table layer (docs/gsql_table_design.md). The contract is
 * include/gsql_table.h; this file is the layout and the rules.
 *
 * EVERYTHING HERE IS DECIDED FROM `serialize` VERSION 3 BYTES. The parts:
 *   1. errors
 *   2-3. reading and writing a v3 value, which live in src/gsql/v3.c since
 *      the client protocol became their second user
 *   4. the schema: encoding, decoding, the handle
 *   5. the type rules (§3.1)
 *   6. keys, rows and index entries
 *   7. the operations
 *   8. the invariant check (§8) */

#include "gsql_table.h"
#include "gsql_key.h"
#include "serialize_tags.h"
#include "v3.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_FORMAT 1
#define SCHEMA_FORMAT 1
#define MAX_NAME 255
#define TREE_LEN 10

/* ==== 1. errors ========================================================= */

static const char *const code_names[] = {
    "OK", "NOT_FOUND", "EXISTS", "DUPLICATE", "NOT_NULL", "TYPE", "SCHEMA", "CORRUPT", "STORAGE", "MISUSE"
};

const char *gsql_code_name(int code) {
    if (code < 0 || code >= (int)(sizeof code_names / sizeof code_names[0])) {
        return "?";
    }
    return code_names[code];
}

static const char *const kind_names[] = {
    "?", "number", "integer", "string", "boolean", "money", "date", "time", "datetime", "record"
};

const char *gsql_kind_name(int kind) {
    if (kind < 1 || kind > GSQL_COL_RECORD) {
        return "?";
    }
    return kind_names[kind];
}

static int fail(GsqlError *err, int code, const char *fmt, ...) {
    if (err) {
        va_list ap;
        va_start(ap, fmt);
        err->code = code;
        vsnprintf(err->message, sizeof err->message, fmt, ap);
        va_end(ap);
    }
    return code;
}

static int ok(GsqlError *err) {
    if (err) {
        err->code = GSQL_OK;
        err->message[0] = 0;
    }
    return GSQL_OK;
}

static int storage(GsqlError *err, int st, const char *what) {
    return fail(err, GSQL_STORAGE, "%s: the engine answered %s", what, gs_status_name(st));
}

/* ==== 4. the schema ====================================================== */

struct GsqlTable {
    char *name;
    uint64_t id, version;
    size_t ncols;
    GsqlColumn *cols;          /* names and defaults owned here */
    size_t nkey;
    unsigned *key;
    size_t nidx;
    GsqlIndex *idx;            /* names and arrays owned here */
    unsigned char rows_tree[TREE_LEN];
    unsigned char (*idx_tree)[TREE_LEN];
};

static void tree_name(unsigned char out[TREE_LEN], char letter, uint64_t id) {
    out[0] = 0;
    out[1] = (unsigned char)letter;
    for (int i = 0; i < 8; i++) {
        out[2 + i] = (unsigned char)(id >> (56 - 8 * i));
    }
}

static void free_index(GsqlIndex *x) {
    free((char *)x->name);
    free((unsigned *)x->cols);
    free((int *)x->desc);
}

static void table_clear(GsqlTable *tb) {
    for (size_t i = 0; i < tb->ncols; i++) {
        free((char *)tb->cols[i].name);
        free((unsigned char *)tb->cols[i].def);
    }
    for (size_t i = 0; i < tb->nidx; i++) {
        free_index(&tb->idx[i]);
    }
    free(tb->name);
    free(tb->cols);
    free(tb->key);
    free(tb->idx);
    free(tb->idx_tree);
    memset(tb, 0, sizeof *tb);
}

void gsql_table_close(GsqlTable *tb) {
    if (tb) {
        table_clear(tb);
        free(tb);
    }
}

static char *dup_text(const char *s) {
    size_t n = strlen(s);
    char *d = malloc(n + 1);
    if (!d) {
        abort();
    }
    memcpy(d, s, n + 1);
    return d;
}

static void schema_encode(const GsqlTable *tb, GsqlBuf *b) {
    b->length = 0;
    v3_w_record(b, 6);
    v3_w_name(b, "format");
    v3_w_int(b, SCHEMA_FORMAT);
    v3_w_name(b, "id");
    v3_w_int(b, tb->id);
    v3_w_name(b, "version");
    v3_w_int(b, tb->version);
    v3_w_name(b, "columns");
    v3_w_array(b, tb->ncols);
    for (size_t i = 0; i < tb->ncols; i++) {
        const GsqlColumn *c = &tb->cols[i];
        v3_w_record(b, 5);
        v3_w_name(b, "name");
        v3_w_text(b, c->name);
        v3_w_name(b, "kind");
        v3_w_text(b, gsql_kind_name(c->kind));
        v3_w_name(b, "currency");
        v3_w_text(b, c->currency);
        v3_w_name(b, "not_null");
        v3_w_bool(b, c->not_null);
        v3_w_name(b, "default");
        if (c->def) {
            v3_put(b, c->def, c->def_len);
        } else {
            v3_w_unknown(b);
        }
    }
    v3_w_name(b, "key");
    v3_w_array(b, tb->nkey);
    for (size_t i = 0; i < tb->nkey; i++) {
        v3_w_int(b, tb->key[i]);
    }
    v3_w_name(b, "indexes");
    v3_w_array(b, tb->nidx);
    for (size_t i = 0; i < tb->nidx; i++) {
        const GsqlIndex *x = &tb->idx[i];
        v3_w_record(b, 5);
        v3_w_name(b, "name");
        v3_w_text(b, x->name);
        v3_w_name(b, "id");
        v3_w_int(b, x->id);
        v3_w_name(b, "columns");
        v3_w_array(b, x->ncols);
        for (size_t j = 0; j < x->ncols; j++) {
            v3_w_int(b, x->cols[j]);
        }
        v3_w_name(b, "descending");
        v3_w_array(b, x->ncols);
        for (size_t j = 0; j < x->ncols; j++) {
            v3_w_bool(b, x->desc[j]);
        }
        v3_w_name(b, "unique");
        v3_w_bool(b, x->unique);
    }
}

static int kind_from_name(const char *s) {
    for (int k = 1; k <= GSQL_COL_RECORD; k++) {
        if (strcmp(s, kind_names[k]) == 0) {
            return k;
        }
    }
    return 0;
}

static void set_trees(GsqlTable *tb) {
    tree_name(tb->rows_tree, 'r', tb->id);
    free(tb->idx_tree);
    tb->idx_tree = tb->nidx ? malloc(tb->nidx * TREE_LEN) : NULL;
    if (tb->nidx && !tb->idx_tree) {
        abort();
    }
    for (size_t i = 0; i < tb->nidx; i++) {
        tree_name(tb->idx_tree[i], 'i', tb->idx[i].id);
    }
}

#define MAX_ITEMS 4096

/* Builds `tb` from its stored schema. Answers 0 when the bytes do not parse
 * or do not make a consistent schema (a CORRUPT catalog). */
static int schema_decode(const char *name, const unsigned char *p, size_t n, GsqlTable *tb, int *newer) {
    V3Field f[16];
    *newer = 0;
    memset(tb, 0, sizeof *tb);
    long nf = v3_record_fields(p, n, f, 16);
    if (nf < 0) {
        return 0;
    }
    const V3Field *ff = v3_field_named(f, nf, "format");
    uint64_t format = 0;
    if (!ff || !v3_read_u64(ff->v, ff->vlen, &format)) {
        return 0;
    }
    if (format != SCHEMA_FORMAT) {
        *newer = 1;
        return 0;
    }
    const V3Field *fid = v3_field_named(f, nf, "id"), *fver = v3_field_named(f, nf, "version");
    const V3Field *fcols = v3_field_named(f, nf, "columns"), *fkey = v3_field_named(f, nf, "key");
    const V3Field *fidx = v3_field_named(f, nf, "indexes");
    if (!fid || !fver || !fcols || !fkey || !fidx || !v3_read_u64(fid->v, fid->vlen, &tb->id) ||
        !v3_read_u64(fver->v, fver->vlen, &tb->version)) {
        return 0;
    }
    tb->name = dup_text(name);
    GsBytes *items = malloc(MAX_ITEMS * sizeof *items);
    if (!items) {
        abort();
    }
    long nc = v3_array_items(fcols->v, fcols->vlen, items, MAX_ITEMS);
    if (nc <= 0) {
        goto bad;
    }
    tb->cols = calloc((size_t)nc, sizeof *tb->cols);
    if (!tb->cols) {
        abort();
    }
    for (long i = 0; i < nc; i++) {
        V3Field cf[8];
        long ncf = v3_record_fields(items[i].p, items[i].n, cf, 8);
        const V3Field *cn = ncf > 0 ? v3_field_named(cf, ncf, "name") : NULL;
        const V3Field *ck = ncf > 0 ? v3_field_named(cf, ncf, "kind") : NULL;
        const V3Field *cc = ncf > 0 ? v3_field_named(cf, ncf, "currency") : NULL;
        const V3Field *cnn = ncf > 0 ? v3_field_named(cf, ncf, "not_null") : NULL;
        const V3Field *cd = ncf > 0 ? v3_field_named(cf, ncf, "default") : NULL;
        if (!cn || !ck || !cc || !cnn || !cd) {
            goto bad;
        }
        GsqlColumn *c = &tb->cols[i];
        tb->ncols = (size_t)i + 1;
        c->name = v3_read_text(cn->v, cn->vlen);
        char *kind = v3_read_text(ck->v, ck->vlen);
        char *cur = v3_read_text(cc->v, cc->vlen);
        int nn = 0;
        int kd = kind ? kind_from_name(kind) : 0;
        free(kind);
        if (!c->name || !kd || !cur || strlen(cur) > 7 || !v3_read_bool(cnn->v, cnn->vlen, &nn)) {
            free(cur);
            goto bad;
        }
        c->kind = (GsqlColKind)kd;
        strcpy(c->currency, cur);
        free(cur);
        c->not_null = nn;
        if (!(cd->vlen == 1 && cd->v[0] == SER_UNKNOWN)) {
            unsigned char *d = malloc(cd->vlen);
            if (!d) {
                abort();
            }
            memcpy(d, cd->v, cd->vlen);
            c->def = d;
            c->def_len = cd->vlen;
        }
    }
    long nk = v3_array_items(fkey->v, fkey->vlen, items, MAX_ITEMS);
    if (nk < 0) {
        goto bad;
    }
    tb->key = nk ? malloc((size_t)nk * sizeof *tb->key) : NULL;
    tb->nkey = (size_t)nk;
    for (long i = 0; i < nk; i++) {
        uint64_t v;
        if (!v3_read_u64(items[i].p, items[i].n, &v) || v >= tb->ncols) {
            goto bad;
        }
        tb->key[i] = (unsigned)v;
    }
    long ni = v3_array_items(fidx->v, fidx->vlen, items, MAX_ITEMS);
    if (ni < 0) {
        goto bad;
    }
    tb->idx = ni ? calloc((size_t)ni, sizeof *tb->idx) : NULL;
    for (long i = 0; i < ni; i++) {
        V3Field xf[8];
        long nxf = v3_record_fields(items[i].p, items[i].n, xf, 8);
        const V3Field *xn = nxf > 0 ? v3_field_named(xf, nxf, "name") : NULL;
        const V3Field *xi = nxf > 0 ? v3_field_named(xf, nxf, "id") : NULL;
        const V3Field *xc = nxf > 0 ? v3_field_named(xf, nxf, "columns") : NULL;
        const V3Field *xd = nxf > 0 ? v3_field_named(xf, nxf, "descending") : NULL;
        const V3Field *xu = nxf > 0 ? v3_field_named(xf, nxf, "unique") : NULL;
        if (!xn || !xi || !xc || !xd || !xu) {
            goto bad;
        }
        GsqlIndex *x = &tb->idx[i];
        tb->nidx = (size_t)i + 1;
        x->name = v3_read_text(xn->v, xn->vlen);
        int u = 0;
        GsBytes cols[64], desc[64];
        long ncx = v3_array_items(xc->v, xc->vlen, cols, 64);
        long ndx = v3_array_items(xd->v, xd->vlen, desc, 64);
        if (!x->name || !v3_read_u64(xi->v, xi->vlen, &x->id) || !v3_read_bool(xu->v, xu->vlen, &u) || ncx <= 0 ||
            ncx != ndx) {
            goto bad;
        }
        x->unique = u;
        x->ncols = (size_t)ncx;
        unsigned *xcols = malloc((size_t)ncx * sizeof *xcols);
        int *xdesc = malloc((size_t)ncx * sizeof *xdesc);
        if (!xcols || !xdesc) {
            abort();
        }
        x->cols = xcols;
        x->desc = xdesc;
        for (long j = 0; j < ncx; j++) {
            uint64_t v;
            int d;
            if (!v3_read_u64(cols[j].p, cols[j].n, &v) || v >= tb->ncols || !v3_read_bool(desc[j].p, desc[j].n, &d)) {
                goto bad;
            }
            xcols[j] = (unsigned)v;
            xdesc[j] = d;
        }
    }
    free(items);
    set_trees(tb);
    return 1;
bad:
    free(items);
    table_clear(tb);
    return 0;
}

static const GsBytes CATALOG = { (const unsigned char *)"\0catalog", 8 };
static const GsBytes META = { (const unsigned char *)"\0meta", 5 };
static const GsBytes NEXT = { (const unsigned char *)"next", 4 };

static GsBytes name_bytes(const char *s) {
    GsBytes b = { (const unsigned char *)s, strlen(s) };
    return b;
}

/* Creates the layer's shared trees if they are not there. */
static int ensure_trees(const GsEngine *e, GsTxn *t, GsqlError *err) {
    int st = e->tree_create(t, CATALOG);
    if (st != GS_OK && st != GS_EXISTS) {
        return storage(err, st, "creating the catalog");
    }
    st = e->tree_create(t, META);
    if (st != GS_OK && st != GS_EXISTS) {
        return storage(err, st, "creating the meta tree");
    }
    return GSQL_OK;
}

static int meta_get(const GsEngine *e, GsTxn *t, GsBytes key, uint64_t *v, GsqlError *err) {
    GsBytes got;
    int st = e->get(t, META, key, &got);
    if (st == GS_NOT_FOUND) {
        *v = 1;
        return GSQL_OK;
    }
    if (st != GS_OK) {
        return storage(err, st, "reading the meta tree");
    }
    if (got.n != 8) {
        return fail(err, GSQL_CORRUPT, "a meta entry is %zu bytes, not 8", got.n);
    }
    uint64_t x = 0;
    for (int i = 0; i < 8; i++) {
        x |= (uint64_t)got.p[i] << (8 * i);
    }
    *v = x;
    return GSQL_OK;
}

static int meta_put(const GsEngine *e, GsTxn *t, GsBytes key, uint64_t v, GsqlError *err) {
    unsigned char b[8];
    for (int i = 0; i < 8; i++) {
        b[i] = (unsigned char)(v >> (8 * i));
    }
    GsBytes val = { b, 8 };
    int st = e->put(t, META, key, val, 0);
    return st == GS_OK ? GSQL_OK : storage(err, st, "writing the meta tree");
}

static int next_id(const GsEngine *e, GsTxn *t, uint64_t *id, GsqlError *err) {
    int rc = meta_get(e, t, NEXT, id, err);
    if (rc == GSQL_OK) {
        rc = meta_put(e, t, NEXT, *id + 1, err);
    }
    return rc;
}

static int schema_write(const GsEngine *e, GsTxn *t, const GsqlTable *tb, GsqlError *err) {
    GsqlBuf b = { 0 };
    schema_encode(tb, &b);
    GsBytes v = { b.bytes, b.length };
    int st = e->put(t, CATALOG, name_bytes(tb->name), v, 0);
    gsql_buf_free(&b);
    return st == GS_OK ? GSQL_OK : storage(err, st, "writing the catalog");
}

int gsql_table_open(const GsEngine *e, GsTxn *t, const char *name, GsqlTable **out, GsqlError *err) {
    GsBytes got;
    *out = NULL;
    int st = e->get(t, CATALOG, name_bytes(name), &got);
    if (st == GS_NOT_FOUND) {
        return fail(err, GSQL_NOT_FOUND, "there is no table %s", name);
    }
    if (st != GS_OK) {
        return storage(err, st, "reading the catalog");
    }
    GsqlTable *tb = malloc(sizeof *tb);
    if (!tb) {
        abort();
    }
    int newer = 0;
    if (!schema_decode(name, got.p, got.n, tb, &newer)) {
        free(tb);
        if (newer) {
            return fail(err, GSQL_CORRUPT, "table %s's schema is a newer format than this build reads", name);
        }
        return fail(err, GSQL_CORRUPT, "table %s's schema does not parse", name);
    }
    *out = tb;
    return ok(err);
}

typedef struct {
    int (*each)(void *ctx, const char *name);
    void *ctx;
    int stopped;
} ListCtx;

int gsql_table_list(const GsEngine *e, GsTxn *t, int (*each)(void *ctx, const char *name), void *ctx,
                    GsqlError *err) {
    GsCursor *c;
    int st = e->cursor_open(t, CATALOG, &c);
    if (st == GS_NOT_FOUND) {
        return ok(err);
    }
    if (st != GS_OK) {
        return storage(err, st, "opening the catalog");
    }
    int pos = 0;
    st = e->first(c, &pos);
    while (st == GS_OK && pos) {
        GsBytes k;
        st = e->key(c, &k);
        if (st != GS_OK) {
            break;
        }
        char name[MAX_NAME + 1];
        if (k.n > MAX_NAME) {
            e->cursor_close(c);
            return fail(err, GSQL_CORRUPT, "a catalog key of %zu bytes", k.n);
        }
        memcpy(name, k.p, k.n);
        name[k.n] = 0;
        if (each(ctx, name)) {
            break;
        }
        st = e->next(c, &pos);
    }
    e->cursor_close(c);
    return st == GS_OK ? ok(err) : storage(err, st, "reading the catalog");
}

const char *gsql_table_name(const GsqlTable *tb) { return tb->name; }
uint64_t gsql_table_id(const GsqlTable *tb) { return tb->id; }
uint64_t gsql_table_version(const GsqlTable *tb) { return tb->version; }
size_t gsql_table_ncols(const GsqlTable *tb) { return tb->ncols; }
const GsqlColumn *gsql_table_col(const GsqlTable *tb, size_t i) { return i < tb->ncols ? &tb->cols[i] : NULL; }
size_t gsql_table_nkey(const GsqlTable *tb) { return tb->nkey; }
const unsigned *gsql_table_key_cols(const GsqlTable *tb) { return tb->key; }
size_t gsql_table_nindexes(const GsqlTable *tb) { return tb->nidx; }
const GsqlIndex *gsql_table_index(const GsqlTable *tb, size_t i) { return i < tb->nidx ? &tb->idx[i] : NULL; }

long gsql_table_find(const GsqlTable *tb, const char *name) {
    for (size_t i = 0; i < tb->ncols; i++) {
        if (strcmp(tb->cols[i].name, name) == 0) {
            return (long)i;
        }
    }
    return -1;
}

GsBytes gsql_table_rows_tree(const GsqlTable *tb) {
    GsBytes b = { tb->rows_tree, TREE_LEN };
    return b;
}

GsBytes gsql_index_tree(const GsqlTable *tb, size_t index) {
    GsBytes b = { tb->idx_tree[index], TREE_LEN };
    return b;
}

/* ==== 5. the type rules (§3.1) =========================================== */

static int check_name(const char *what, const char *name, GsqlError *err) {
    size_t n = name ? strlen(name) : 0;
    if (n == 0) {
        return fail(err, GSQL_SCHEMA, "a %s needs a name", what);
    }
    if (n > MAX_NAME) {
        return fail(err, GSQL_SCHEMA, "a %s's name is %zu bytes, past the limit of %d", what, n, MAX_NAME);
    }
    return GSQL_OK;
}

static int value_fits(const char *table, const GsqlColumn *c, const unsigned char *p, size_t n, GsqlError *err) {
    V3Walk w;
    if (!v3_walk_whole(p, n, &w)) {
        return fail(err, GSQL_TYPE, "%s.%s: the value is not one well-formed value", table, c->name);
    }
    if (w.tag == SER_NULL || w.tag == SER_UNKNOWN) {
        if (c->not_null) {
            return fail(err, GSQL_NOT_NULL, "%s.%s is NOT NULL, and the value is unknown", table, c->name);
        }
        return GSQL_OK;
    }
    if (w.bad_tag) {
        return fail(err, GSQL_TYPE, "%s.%s: %s cannot be stored%s", table, c->name, v3_tag_name(w.bad_tag),
                    w.bad_tag == w.tag ? "" : ", and the record holds one");
    }
    const char *want = gsql_kind_name(c->kind);
    int good = 0;
    switch (c->kind) {
    case GSQL_COL_NUMBER:
        good = w.tag == SER_NUMBER;
        break;
    case GSQL_COL_INTEGER:
        if (w.tag == SER_NUMBER && !w.exact) {
            return fail(err, GSQL_TYPE,
                        "%s.%s is integer, and the number is not an exact integer (it is refused rather than "
                        "converted)", table, c->name);
        }
        good = w.tag == SER_NUMBER;
        break;
    case GSQL_COL_STRING:
        good = w.tag == SER_STRING;
        break;
    case GSQL_COL_BOOLEAN:
        good = w.tag == SER_BOOL;
        break;
    case GSQL_COL_MONEY:
        if (w.tag == SER_MONEY && c->currency[0] && strcmp(w.currency, c->currency) != 0) {
            return fail(err, GSQL_TYPE, "%s.%s is money(%s), and the money is in %s", table, c->name, c->currency,
                        w.currency[0] ? w.currency : "a currency with no code");
        }
        good = w.tag == SER_MONEY;
        break;
    case GSQL_COL_DATE:
        good = w.tag == SER_DATETIME && !w.time_only && w.precision == 3;
        break;
    case GSQL_COL_TIME:
        good = w.tag == SER_DATETIME && w.time_only;
        break;
    case GSQL_COL_DATETIME:
        good = w.tag == SER_DATETIME && !w.time_only && w.precision >= 4;
        break;
    case GSQL_COL_RECORD:
        good = w.tag == SER_RECORD;
        break;
    }
    if (!good) {
        if (w.tag == SER_DATETIME) {
            const char *is = w.time_only ? "a time" : w.precision == 3 ? "a date" : w.precision < 3 ? "a year or month"
                                                                                              : "a date and time";
            return fail(err, GSQL_TYPE, "%s.%s is %s, and the value is %s", table, c->name, want, is);
        }
        if (w.tag == SER_ARRAY) {
            return fail(err, GSQL_TYPE, "%s.%s is %s, and the value is an array (an array is stored inside a record)",
                        table, c->name, want);
        }
        return fail(err, GSQL_TYPE, "%s.%s is %s, and the value is %s", table, c->name, want, v3_tag_name(w.tag));
    }
    return GSQL_OK;
}

int gsql_value_fits(const GsqlColumn *c, const unsigned char *value, size_t n, GsqlError *err) {
    int rc = value_fits("?", c, value, n, err);
    return rc == GSQL_OK ? ok(err) : rc;
}

/* ==== 6. keys, rows and index entries ==================================== */

static const unsigned char UNKNOWN_VALUE[1] = { SER_UNKNOWN };

static unsigned key_flags(const GsqlColumn *c, int desc) {
    unsigned f = desc ? GSQL_KEY_DESC : 0;
    if (c->kind == GSQL_COL_MONEY && c->currency[0]) {
        f |= GSQL_KEY_MONEY_TYPED;
    }
    return f;
}

/* Appends one column's key component; 0 if the value has no index order. */
static int key_component(GsqlKey *k, const GsqlColumn *c, GsBytes v, int desc) {
    return gsql_key_append(k, v.p, v.n, NULL, key_flags(c, desc)) == GSQL_KEY_OK;
}

/* Raw key bytes (a row key) after the components. gsql_key_append takes a
 * value, not a key, so these are copied by hand. */
static void key_raw(GsqlKey *k, GsBytes b) {
    if (k->length + b.n > k->capacity) {
        size_t cap = k->capacity ? k->capacity : 32;
        while (k->length + b.n > cap) {
            cap *= 2;
        }
        unsigned char *g = realloc(k->bytes, cap);
        if (!g) {
            abort();
        }
        k->bytes = g;
        k->capacity = cap;
    }
    if (b.n) {
        memcpy(k->bytes + k->length, b.p, b.n);
    }
    k->length += b.n;
}

static int is_absent(GsBytes v) {
    return v.n == 1 && (v.p[0] == SER_UNKNOWN || v.p[0] == SER_NULL);
}

static void hidden_key(GsqlKey *k, uint64_t rowid) {
    unsigned char v[10];
    v[0] = SER_NUMBER;
    v[1] = 1;
    for (int i = 0; i < 8; i++) {
        v[2 + i] = (unsigned char)(rowid >> (8 * i));
    }
    gsql_key_append(k, v, sizeof v, NULL, 0);
}

static int primary_key(const GsqlTable *tb, const GsBytes *vals, GsqlKey *k, GsqlError *err) {
    for (size_t i = 0; i < tb->nkey; i++) {
        const GsqlColumn *c = &tb->cols[tb->key[i]];
        if (!key_component(k, c, vals[tb->key[i]], 0)) {
            return fail(err, GSQL_TYPE, "%s.%s: the value has no order, so it cannot be a key", tb->name, c->name);
        }
    }
    return GSQL_OK;
}

/* An index entry for a row. *unique_form: whether the entry is keyed by its
 * columns alone (a unique index with no absent column). */
static int index_entry(const GsqlTable *tb, const GsqlIndex *x, const GsBytes *vals, GsBytes rowkey, GsqlKey *k,
                       int *unique_form) {
    int absent = 0;
    for (size_t j = 0; j < x->ncols; j++) {
        GsBytes v = vals[x->cols[j]];
        absent |= is_absent(v);
        if (!key_component(k, &tb->cols[x->cols[j]], v, x->desc[j])) {
            return 0;
        }
    }
    *unique_form = x->unique && !absent;
    if (!*unique_form) {
        key_raw(k, rowkey);   /* the row key follows the columns */
    }
    return 1;
}

static void row_encode(const GsqlTable *tb, const GsBytes *vals, GsqlBuf *b) {
    b->length = 0;
    v3_u8(b, ROW_FORMAT);
    v3_uleb(b, tb->ncols);
    for (size_t i = 0; i < tb->ncols; i++) {
        if (vals[i].n == 1 && vals[i].p[0] == SER_NULL) {
            v3_u8(b, SER_UNKNOWN);   /* a bound `nothing` is stored as unknown (§3.1) */
        } else {
            v3_put(b, vals[i].p, vals[i].n);
        }
    }
}

/* Splits a stored row into one value per column of the CURRENT schema,
 * filling the columns the row predates with their defaults. */
static int row_decode(const GsqlTable *tb, const unsigned char *p, size_t n, GsBytes *vals, GsqlError *err) {
    V3In in = { p, n, 0, 1 };
    unsigned format = (unsigned)v3_in_le(&in, 1);
    uint64_t count = v3_in_uleb(&in);
    if (!in.ok || format != ROW_FORMAT) {
        return fail(err, GSQL_CORRUPT, "a row of %s has format %u", tb->name, format);
    }
    if (count > tb->ncols) {
        return fail(err, GSQL_CORRUPT, "a row of %s has %llu columns, more than its %zu", tb->name,
                    (unsigned long long)count, tb->ncols);
    }
    for (size_t i = 0; i < tb->ncols; i++) {
        if (i < count) {
            size_t at = in.pos;
            V3Walk w;
            memset(&w, 0, sizeof w);
            if (!v3_walk(&in, 0, &w)) {
                return fail(err, GSQL_CORRUPT, "a row of %s: column %zu does not parse", tb->name, i);
            }
            vals[i].p = p + at;
            vals[i].n = in.pos - at;
        } else if (tb->cols[i].def) {
            vals[i].p = tb->cols[i].def;
            vals[i].n = tb->cols[i].def_len;
        } else {
            vals[i].p = UNKNOWN_VALUE;
            vals[i].n = 1;
        }
    }
    if (in.pos != n) {
        return fail(err, GSQL_CORRUPT, "a row of %s has %zu bytes after its last column", tb->name, n - in.pos);
    }
    return GSQL_OK;
}

int gsql_row_value(const GsqlTable *tb, const unsigned char *row, size_t n, size_t col, GsBytes *out,
                   GsqlError *err) {
    if (col >= tb->ncols) {
        return fail(err, GSQL_MISUSE, "%s has no column %zu", tb->name, col);
    }
    GsBytes *vals = malloc(tb->ncols * sizeof *vals);
    if (!vals) {
        abort();
    }
    int rc = row_decode(tb, row, n, vals, err);
    if (rc == GSQL_OK) {
        *out = vals[col];
        ok(err);
    }
    free(vals);
    return rc;
}

int gsql_row_key(const GsqlTable *tb, const GsqlIn *vals, GsqlBuf *rowkey, GsqlError *err) {
    if (!tb->nkey) {
        return fail(err, GSQL_MISUSE, "%s has no primary key", tb->name);
    }
    GsqlKey k;
    gsql_key_init(&k);
    for (size_t i = 0; i < tb->nkey; i++) {
        const GsqlColumn *c = &tb->cols[tb->key[i]];
        GsBytes v = { vals[i].p, vals[i].n };
        if (!vals[i].present || !key_component(&k, c, v, 0)) {
            gsql_key_free(&k);
            return fail(err, GSQL_TYPE, "%s.%s: the value has no order, so it cannot be a key", tb->name, c->name);
        }
    }
    v3_set(rowkey, k.bytes, k.length);
    gsql_key_free(&k);
    return ok(err);
}

int gsql_index_rowkey(const GsqlTable *tb, size_t index, GsBytes entry_key, GsBytes entry_value, GsBytes *rowkey,
                      GsqlError *err) {
    if (index >= tb->nidx) {
        return fail(err, GSQL_MISUSE, "%s has no index %zu", tb->name, index);
    }
    const GsqlIndex *x = &tb->idx[index];
    size_t at = 0;
    int absent = 0;
    for (size_t j = 0; j < x->ncols; j++) {
        size_t used;
        unsigned flags = key_flags(&tb->cols[x->cols[j]], x->desc[j]);
        if (gsql_key_skip(entry_key.p + at, entry_key.n - at, flags, &used) != GSQL_KEY_OK) {
            return fail(err, GSQL_CORRUPT, "an entry of index %s does not parse", x->name);
        }
        unsigned first = entry_key.p[at] ^ (x->desc[j] ? 0xFF : 0x00);
        absent |= first == 0xF0;   /* KEY_ABSENT */
        at += used;
    }
    if (x->unique && !absent) {
        if (at != entry_key.n) {
            return fail(err, GSQL_CORRUPT, "an entry of unique index %s has bytes after its columns", x->name);
        }
        *rowkey = entry_value;
    } else {
        if (entry_value.n != 0) {
            return fail(err, GSQL_CORRUPT, "an entry of index %s carries a value", x->name);
        }
        rowkey->p = entry_key.p + at;
        rowkey->n = entry_key.n - at;
    }
    return ok(err);
}

/* ==== 7. the operations ================================================== */

static int check_column_decl(const char *table, const GsqlColumn *c, GsqlError *err) {
    int rc = check_name("column", c->name, err);
    if (rc) {
        return rc;
    }
    if (c->kind < GSQL_COL_NUMBER || c->kind > GSQL_COL_RECORD) {
        return fail(err, GSQL_SCHEMA, "%s.%s: kind %d is not a column kind", table, c->name, (int)c->kind);
    }
    if (c->currency[0] && c->kind != GSQL_COL_MONEY) {
        return fail(err, GSQL_SCHEMA, "%s.%s: only a money column names a currency", table, c->name);
    }
    if (memchr(c->currency, 0, sizeof c->currency) == NULL) {
        return fail(err, GSQL_SCHEMA, "%s.%s: a currency code is at most 7 letters", table, c->name);
    }
    if (c->def) {
        GsqlError e2;
        int fr = value_fits(table, c, c->def, c->def_len, &e2);
        if (fr != GSQL_OK) {
            return fail(err, GSQL_SCHEMA, "the default refused by its own column: %s", e2.message);
        }
        if (is_absent((GsBytes){ c->def, c->def_len })) {
            return fail(err, GSQL_SCHEMA, "%s.%s: a default of unknown is no default; leave it out", table, c->name);
        }
    }
    return GSQL_OK;
}

static void copy_column(GsqlColumn *dst, const GsqlColumn *src) {
    *dst = *src;
    dst->name = dup_text(src->name);
    if (src->def) {
        unsigned char *d = malloc(src->def_len);
        if (!d) {
            abort();
        }
        memcpy(d, src->def, src->def_len);
        dst->def = d;
    }
}

static int check_positions(const char *table, const char *what, const unsigned *pos, size_t n, size_t ncols,
                           GsqlError *err) {
    for (size_t i = 0; i < n; i++) {
        if (pos[i] >= ncols) {
            return fail(err, GSQL_SCHEMA, "%s: the %s names column %u, and there are %zu", table, what, pos[i], ncols);
        }
        for (size_t j = 0; j < i; j++) {
            if (pos[j] == pos[i]) {
                return fail(err, GSQL_SCHEMA, "%s: the %s names column %u twice", table, what, pos[i]);
            }
        }
    }
    return GSQL_OK;
}

int gsql_table_create(const GsEngine *e, GsTxn *t, const GsqlTableDecl *d, GsqlError *err) {
    int rc = check_name("table", d->name, err);
    if (rc) {
        return rc;
    }
    if (d->ncols == 0) {
        return fail(err, GSQL_SCHEMA, "%s has no columns", d->name);
    }
    if (d->ncols > MAX_ITEMS) {
        return fail(err, GSQL_SCHEMA, "%s has %zu columns, past the limit of %d", d->name, d->ncols, MAX_ITEMS);
    }
    for (size_t i = 0; i < d->ncols; i++) {
        if ((rc = check_column_decl(d->name, &d->cols[i], err))) {
            return rc;
        }
        for (size_t j = 0; j < i; j++) {
            if (strcmp(d->cols[i].name, d->cols[j].name) == 0) {
                return fail(err, GSQL_SCHEMA, "%s has two columns named %s", d->name, d->cols[i].name);
            }
        }
    }
    if ((rc = check_positions(d->name, "primary key", d->key, d->nkey, d->ncols, err))) {
        return rc;
    }
    for (size_t i = 0; i < d->nkey; i++) {
        if (d->cols[d->key[i]].kind == GSQL_COL_RECORD) {
            return fail(err, GSQL_SCHEMA, "%s.%s is a record, which has no order, so it cannot be in a key", d->name,
                        d->cols[d->key[i]].name);
        }
    }
    if ((rc = ensure_trees(e, t, err))) {
        return rc;
    }
    GsBytes got;
    int st = e->get(t, CATALOG, name_bytes(d->name), &got);
    if (st == GS_OK) {
        return fail(err, GSQL_EXISTS, "there is already a table %s", d->name);
    }
    if (st != GS_NOT_FOUND) {
        return storage(err, st, "reading the catalog");
    }
    GsqlTable tb;
    memset(&tb, 0, sizeof tb);
    if ((rc = next_id(e, t, &tb.id, err))) {
        return rc;
    }
    tb.name = dup_text(d->name);
    tb.version = 1;
    tb.ncols = d->ncols;
    tb.cols = calloc(d->ncols, sizeof *tb.cols);
    if (!tb.cols) {
        abort();
    }
    for (size_t i = 0; i < d->ncols; i++) {
        copy_column(&tb.cols[i], &d->cols[i]);
    }
    tb.nkey = d->nkey;
    tb.key = d->nkey ? malloc(d->nkey * sizeof *tb.key) : NULL;
    for (size_t i = 0; i < d->nkey; i++) {
        tb.key[i] = d->key[i];
        tb.cols[d->key[i]].not_null = 1;   /* a key column is NOT NULL, as SQL makes it */
    }
    set_trees(&tb);
    st = e->tree_create(t, gsql_table_rows_tree(&tb));
    if (st != GS_OK) {
        rc = st == GS_EXISTS ? fail(err, GSQL_CORRUPT, "the rows tree of a new table %s is already there", d->name)
                             : storage(err, st, "creating the rows tree");
    } else {
        rc = schema_write(e, t, &tb, err);
    }
    table_clear(&tb);
    return rc ? rc : ok(err);
}

int gsql_table_drop(const GsEngine *e, GsTxn *t, const char *name, GsqlError *err) {
    GsqlTable *tb;
    int rc = gsql_table_open(e, t, name, &tb, err);
    if (rc) {
        return rc;
    }
    int st = e->tree_drop(t, gsql_table_rows_tree(tb));
    for (size_t i = 0; st == GS_OK && i < tb->nidx; i++) {
        st = e->tree_drop(t, gsql_index_tree(tb, i));
    }
    if (st == GS_OK) {
        st = e->del(t, CATALOG, name_bytes(name));
    }
    if (st == GS_OK && !tb->nkey) {
        unsigned char k[8];
        for (int i = 0; i < 8; i++) {
            k[i] = (unsigned char)(tb->id >> (56 - 8 * i));
        }
        st = e->del(t, META, (GsBytes){ k, 8 });
        if (st == GS_NOT_FOUND) {
            st = GS_OK;
        }
    }
    gsql_table_close(tb);
    return st == GS_OK ? ok(err) : storage(err, st, "dropping a table");
}

/* Resolves the values a write will store: one per column, defaults applied. */
static int resolve(const GsqlTable *tb, const GsqlIn *vals, const GsBytes *old, GsBytes *out, GsqlError *err) {
    for (size_t i = 0; i < tb->ncols; i++) {
        const GsqlColumn *c = &tb->cols[i];
        if (vals[i].present) {
            out[i].p = vals[i].p;
            out[i].n = vals[i].n;
        } else if (old) {
            out[i] = old[i];
        } else if (c->def) {
            out[i].p = c->def;
            out[i].n = c->def_len;
        } else {
            out[i].p = UNKNOWN_VALUE;
            out[i].n = 1;
        }
        int rc = value_fits(tb->name, c, out[i].p, out[i].n, err);
        if (rc) {
            return rc;
        }
    }
    return GSQL_OK;
}

static void meta_table_key(const GsqlTable *tb, unsigned char k[8]) {
    for (int i = 0; i < 8; i++) {
        k[i] = (unsigned char)(tb->id >> (56 - 8 * i));
    }
}

/* The index entries a row has, one per index. */
typedef struct {
    GsqlKey *k;
    int *unique_form;
} Entries;

static int entries_build(const GsqlTable *tb, const GsBytes *vals, GsBytes rowkey, Entries *en, GsqlError *err) {
    en->k = calloc(tb->nidx ? tb->nidx : 1, sizeof *en->k);
    en->unique_form = calloc(tb->nidx ? tb->nidx : 1, sizeof *en->unique_form);
    if (!en->k || !en->unique_form) {
        abort();
    }
    for (size_t i = 0; i < tb->nidx; i++) {
        gsql_key_init(&en->k[i]);
        if (!index_entry(tb, &tb->idx[i], vals, rowkey, &en->k[i], &en->unique_form[i])) {
            return fail(err, GSQL_CORRUPT, "index %s holds a column with no order", tb->idx[i].name);
        }
    }
    return GSQL_OK;
}

static void entries_free(const GsqlTable *tb, Entries *en) {
    if (en->k) {
        for (size_t i = 0; i < tb->nidx; i++) {
            gsql_key_free(&en->k[i]);
        }
    }
    free(en->k);
    free(en->unique_form);
    en->k = NULL;
    en->unique_form = NULL;
}

static GsBytes key_bytes(const GsqlKey *k) {
    GsBytes b = { k->bytes, k->length };
    return b;
}

static int same_bytes(GsBytes a, GsBytes b) {
    return a.n == b.n && (a.n == 0 || memcmp(a.p, b.p, a.n) == 0);
}

/* A unique entry that another row already holds. `self` is the row allowed to
 * hold it (an update's own row), or NULL. */
static int unique_taken(const GsEngine *e, GsTxn *t, const GsqlTable *tb, size_t i, const Entries *en,
                        const GsBytes *self, GsqlError *err) {
    if (!en->unique_form[i]) {
        return GSQL_OK;
    }
    GsBytes got;
    int st = e->get(t, gsql_index_tree(tb, i), key_bytes(&en->k[i]), &got);
    if (st == GS_NOT_FOUND) {
        return GSQL_OK;
    }
    if (st != GS_OK) {
        return storage(err, st, "reading a unique index");
    }
    if (self && same_bytes(got, *self)) {
        return GSQL_OK;
    }
    return fail(err, GSQL_DUPLICATE, "%s: unique index %s already holds that value", tb->name, tb->idx[i].name);
}

static int put_entry(const GsEngine *e, GsTxn *t, const GsqlTable *tb, size_t i, const Entries *en, GsBytes rowkey,
                     GsqlError *err) {
    GsBytes empty = { NULL, 0 };
    int st = e->put(t, gsql_index_tree(tb, i), key_bytes(&en->k[i]), en->unique_form[i] ? rowkey : empty, 0);
    return st == GS_OK ? GSQL_OK : storage(err, st, "writing an index entry");
}

static int del_entry(const GsEngine *e, GsTxn *t, const GsqlTable *tb, size_t i, const Entries *en, GsqlError *err) {
    int st = e->del(t, gsql_index_tree(tb, i), key_bytes(&en->k[i]));
    if (st == GS_NOT_FOUND) {
        return fail(err, GSQL_CORRUPT, "%s: index %s was missing an entry for a row", tb->name, tb->idx[i].name);
    }
    return st == GS_OK ? GSQL_OK : storage(err, st, "removing an index entry");
}

int gsql_row_insert(const GsEngine *e, GsTxn *t, GsqlTable *tb, const GsqlIn *vals, GsqlBuf *rowkey, GsqlError *err) {
    GsBytes *v = malloc(tb->ncols * sizeof *v);
    if (!v) {
        abort();
    }
    GsqlKey k;
    gsql_key_init(&k);
    Entries en = { 0 };
    GsqlBuf row = { 0 };
    uint64_t rowid = 0;
    unsigned char mk[8];
    int rc = resolve(tb, vals, NULL, v, err);
    if (rc) {
        goto out;
    }
    if (tb->nkey) {
        if ((rc = primary_key(tb, v, &k, err))) {
            goto out;
        }
        GsBytes got;
        int st = e->get(t, gsql_table_rows_tree(tb), key_bytes(&k), &got);
        if (st == GS_OK) {
            rc = fail(err, GSQL_DUPLICATE, "%s: the primary key already holds that value", tb->name);
            goto out;
        }
        if (st != GS_NOT_FOUND) {
            rc = storage(err, st, "reading a row");
            goto out;
        }
    } else {
        meta_table_key(tb, mk);
        if ((rc = meta_get(e, t, (GsBytes){ mk, 8 }, &rowid, err))) {
            goto out;
        }
        hidden_key(&k, rowid);
    }
    if ((rc = entries_build(tb, v, key_bytes(&k), &en, err))) {
        goto out;
    }
    for (size_t i = 0; i < tb->nidx; i++) {
        if ((rc = unique_taken(e, t, tb, i, &en, NULL, err))) {
            goto out;
        }
    }
    /* Every check has passed: from here the write goes in whole. */
    row_encode(tb, v, &row);
    int st = e->put(t, gsql_table_rows_tree(tb), key_bytes(&k), (GsBytes){ row.bytes, row.length }, GS_NO_OVERWRITE);
    if (st != GS_OK) {
        rc = storage(err, st, "writing a row");
        goto out;
    }
    for (size_t i = 0; i < tb->nidx; i++) {
        if ((rc = put_entry(e, t, tb, i, &en, key_bytes(&k), err))) {
            goto out;
        }
    }
    if (!tb->nkey && (rc = meta_put(e, t, (GsBytes){ mk, 8 }, rowid + 1, err))) {
        goto out;
    }
    if (rowkey) {
        v3_set(rowkey, k.bytes, k.length);
    }
    rc = ok(err);
out:
    entries_free(tb, &en);
    gsql_key_free(&k);
    gsql_buf_free(&row);
    free(v);
    return rc;
}

int gsql_row_get(const GsEngine *e, GsTxn *t, const GsqlTable *tb, GsBytes rowkey, GsqlBuf *row, GsqlError *err) {
    GsBytes got;
    int st = e->get(t, gsql_table_rows_tree(tb), rowkey, &got);
    if (st == GS_NOT_FOUND) {
        return fail(err, GSQL_NOT_FOUND, "%s has no such row", tb->name);
    }
    if (st != GS_OK) {
        return storage(err, st, "reading a row");
    }
    v3_set(row, got.p, got.n);
    return ok(err);
}

int gsql_row_update(const GsEngine *e, GsTxn *t, GsqlTable *tb, GsBytes rowkey, const GsqlIn *vals, GsqlBuf *newkey,
                    GsqlError *err) {
    GsqlBuf oldrow = { 0 }, row = { 0 };
    GsBytes *oldv = malloc(tb->ncols * sizeof *oldv), *v = malloc(tb->ncols * sizeof *v);
    GsqlKey k;
    gsql_key_init(&k);
    Entries olde = { 0 }, newe = { 0 };
    GsBytes self = rowkey;
    if (!oldv || !v) {
        abort();
    }
    int rc = gsql_row_get(e, t, tb, rowkey, &oldrow, err);
    if (rc) {
        goto out;
    }
    if ((rc = row_decode(tb, oldrow.bytes, oldrow.length, oldv, err))) {
        goto out;
    }
    if ((rc = resolve(tb, vals, oldv, v, err))) {
        goto out;
    }
    if (tb->nkey) {
        if ((rc = primary_key(tb, v, &k, err))) {
            goto out;
        }
    } else {
        key_raw(&k, rowkey);   /* a hidden id never changes */
    }
    int moved = !same_bytes(key_bytes(&k), rowkey);
    if (moved) {
        GsBytes got;
        int st = e->get(t, gsql_table_rows_tree(tb), key_bytes(&k), &got);
        if (st == GS_OK) {
            rc = fail(err, GSQL_DUPLICATE, "%s: the primary key already holds that value", tb->name);
            goto out;
        }
        if (st != GS_NOT_FOUND) {
            rc = storage(err, st, "reading a row");
            goto out;
        }
    }
    if ((rc = entries_build(tb, oldv, rowkey, &olde, err)) || (rc = entries_build(tb, v, key_bytes(&k), &newe, err))) {
        goto out;
    }
    for (size_t i = 0; i < tb->nidx; i++) {
        if ((rc = unique_taken(e, t, tb, i, &newe, &self, err))) {
            goto out;
        }
    }
    row_encode(tb, v, &row);
    int st;
    if (moved) {
        st = e->del(t, gsql_table_rows_tree(tb), rowkey);
        if (st == GS_OK) {
            st = e->put(t, gsql_table_rows_tree(tb), key_bytes(&k), (GsBytes){ row.bytes, row.length },
                        GS_NO_OVERWRITE);
        }
    } else {
        st = e->put(t, gsql_table_rows_tree(tb), rowkey, (GsBytes){ row.bytes, row.length }, 0);
    }
    if (st != GS_OK) {
        rc = storage(err, st, "writing a row");
        goto out;
    }
    for (size_t i = 0; i < tb->nidx; i++) {
        if (!moved && same_bytes(key_bytes(&olde.k[i]), key_bytes(&newe.k[i])) &&
            olde.unique_form[i] == newe.unique_form[i]) {
            continue;
        }
        if ((rc = del_entry(e, t, tb, i, &olde, err)) || (rc = put_entry(e, t, tb, i, &newe, key_bytes(&k), err))) {
            goto out;
        }
    }
    if (newkey) {
        v3_set(newkey, k.bytes, k.length);
    }
    rc = ok(err);
out:
    entries_free(tb, &olde);
    entries_free(tb, &newe);
    gsql_key_free(&k);
    gsql_buf_free(&oldrow);
    gsql_buf_free(&row);
    free(oldv);
    free(v);
    return rc;
}

int gsql_row_delete(const GsEngine *e, GsTxn *t, GsqlTable *tb, GsBytes rowkey, GsqlError *err) {
    GsqlBuf oldrow = { 0 };
    GsBytes *v = malloc(tb->ncols * sizeof *v);
    Entries en = { 0 };
    if (!v) {
        abort();
    }
    int rc = gsql_row_get(e, t, tb, rowkey, &oldrow, err);
    if (!rc) {
        rc = row_decode(tb, oldrow.bytes, oldrow.length, v, err);
    }
    if (!rc) {
        rc = entries_build(tb, v, rowkey, &en, err);
    }
    for (size_t i = 0; !rc && i < tb->nidx; i++) {
        rc = del_entry(e, t, tb, i, &en, err);
    }
    if (!rc) {
        int st = e->del(t, gsql_table_rows_tree(tb), rowkey);
        rc = st == GS_OK ? ok(err) : storage(err, st, "removing a row");
    }
    entries_free(tb, &en);
    gsql_buf_free(&oldrow);
    free(v);
    return rc;
}

/* Puts the handle back as the catalog has it, after a schema change that was
 * refused part way. */
static void reload(const GsEngine *e, GsTxn *t, GsqlTable *tb) {
    GsqlTable *fresh;
    char *name = dup_text(tb->name);
    if (gsql_table_open(e, t, name, &fresh, NULL) == GSQL_OK) {
        table_clear(tb);
        *tb = *fresh;
        free(fresh);
    }
    free(name);
}

int gsql_index_create(const GsEngine *e, GsTxn *t, GsqlTable *tb, const GsqlIndexDecl *d, GsqlError *err) {
    int rc = check_name("index", d->name, err);
    if (rc) {
        return rc;
    }
    if (d->ncols == 0) {
        return fail(err, GSQL_SCHEMA, "index %s has no columns", d->name);
    }
    if (d->ncols > 64) {
        return fail(err, GSQL_SCHEMA, "index %s has %zu columns, past the limit of 64", d->name, d->ncols);
    }
    if ((rc = check_positions(tb->name, "index", d->cols, d->ncols, tb->ncols, err))) {
        return rc;
    }
    for (size_t i = 0; i < d->ncols; i++) {
        if (tb->cols[d->cols[i]].kind == GSQL_COL_RECORD) {
            return fail(err, GSQL_SCHEMA, "%s.%s is a record, which has no order, so index %s cannot hold it",
                        tb->name, tb->cols[d->cols[i]].name, d->name);
        }
    }
    for (size_t i = 0; i < tb->nidx; i++) {
        if (strcmp(tb->idx[i].name, d->name) == 0) {
            return fail(err, GSQL_EXISTS, "%s already has an index %s", tb->name, d->name);
        }
    }
    uint64_t id;
    if ((rc = next_id(e, t, &id, err))) {
        return rc;
    }
    GsqlIndex *grown = realloc(tb->idx, (tb->nidx + 1) * sizeof *grown);
    if (!grown) {
        abort();
    }
    tb->idx = grown;
    GsqlIndex *x = &tb->idx[tb->nidx];
    unsigned *cols = malloc(d->ncols * sizeof *cols);
    int *desc = malloc(d->ncols * sizeof *desc);
    if (!cols || !desc) {
        abort();
    }
    for (size_t i = 0; i < d->ncols; i++) {
        cols[i] = d->cols[i];
        desc[i] = d->desc ? (d->desc[i] != 0) : 0;
    }
    x->name = dup_text(d->name);
    x->id = id;
    x->ncols = d->ncols;
    x->cols = cols;
    x->desc = desc;
    x->unique = d->unique;
    tb->nidx++;
    tb->version++;
    set_trees(tb);
    size_t ix = tb->nidx - 1;
    GsBytes tree = gsql_index_tree(tb, ix);
    int st = e->tree_create(t, tree);
    if (st != GS_OK) {
        rc = storage(err, st, "creating an index tree");
        reload(e, t, tb);
        return rc;
    }
    /* Build over the rows already there. */
    GsCursor *c;
    st = e->cursor_open(t, gsql_table_rows_tree(tb), &c);
    if (st != GS_OK) {
        rc = storage(err, st, "opening the rows");
        goto undo;
    }
    GsBytes *v = malloc(tb->ncols * sizeof *v);
    if (!v) {
        abort();
    }
    int pos = 0;
    st = e->first(c, &pos);
    while (st == GS_OK && pos && !rc) {
        GsBytes rk, rv;
        if ((st = e->key(c, &rk)) != GS_OK || (st = e->value(c, &rv)) != GS_OK) {
            break;
        }
        GsqlKey k;
        gsql_key_init(&k);
        int uf = 0;
        rc = row_decode(tb, rv.p, rv.n, v, err);
        if (!rc && !index_entry(tb, x, v, rk, &k, &uf)) {
            rc = fail(err, GSQL_CORRUPT, "a row of %s has a value with no order", tb->name);
        }
        if (!rc) {
            GsBytes empty = { NULL, 0 };
            int ps = e->put(t, tree, key_bytes(&k), uf ? rk : empty, GS_NO_OVERWRITE);
            if (ps == GS_EXISTS) {
                rc = fail(err, GSQL_DUPLICATE, "%s: two rows already hold the same value, so index %s cannot be unique",
                          tb->name, d->name);
            } else if (ps != GS_OK) {
                rc = storage(err, ps, "writing an index entry");
            }
        }
        gsql_key_free(&k);
        if (!rc) {
            st = e->next(c, &pos);
        }
    }
    free(v);
    e->cursor_close(c);
    if (!rc && st != GS_OK) {
        rc = storage(err, st, "reading the rows");
    }
    if (!rc) {
        rc = schema_write(e, t, tb, err);
    }
    if (!rc) {
        return ok(err);
    }
undo:
    e->tree_drop(t, tree);
    reload(e, t, tb);
    return rc;
}

int gsql_index_drop(const GsEngine *e, GsTxn *t, GsqlTable *tb, const char *name, GsqlError *err) {
    size_t i;
    for (i = 0; i < tb->nidx; i++) {
        if (strcmp(tb->idx[i].name, name) == 0) {
            break;
        }
    }
    if (i == tb->nidx) {
        return fail(err, GSQL_NOT_FOUND, "%s has no index %s", tb->name, name);
    }
    int st = e->tree_drop(t, gsql_index_tree(tb, i));
    if (st != GS_OK) {
        return storage(err, st, "dropping an index tree");
    }
    free_index(&tb->idx[i]);
    memmove(&tb->idx[i], &tb->idx[i + 1], (tb->nidx - i - 1) * sizeof *tb->idx);
    tb->nidx--;
    tb->version++;
    set_trees(tb);
    int rc = schema_write(e, t, tb, err);
    if (rc) {
        reload(e, t, tb);
        return rc;
    }
    return ok(err);
}

int gsql_table_add_column(const GsEngine *e, GsTxn *t, GsqlTable *tb, const GsqlColumn *c, GsqlError *err) {
    int rc = check_column_decl(tb->name, c, err);
    if (rc) {
        return rc;
    }
    if (gsql_table_find(tb, c->name) >= 0) {
        return fail(err, GSQL_SCHEMA, "%s already has a column %s", tb->name, c->name);
    }
    if (c->not_null && !c->def) {
        return fail(err, GSQL_SCHEMA, "%s.%s is NOT NULL with no default, so the rows already there would break it",
                    tb->name, c->name);
    }
    if (tb->ncols >= MAX_ITEMS) {
        return fail(err, GSQL_SCHEMA, "%s has %zu columns, the limit", tb->name, tb->ncols);
    }
    GsqlColumn *grown = realloc(tb->cols, (tb->ncols + 1) * sizeof *grown);
    if (!grown) {
        abort();
    }
    tb->cols = grown;
    copy_column(&tb->cols[tb->ncols], c);
    tb->ncols++;
    tb->version++;
    rc = schema_write(e, t, tb, err);
    if (rc) {
        reload(e, t, tb);
        return rc;
    }
    return ok(err);
}

/* ==== 8. the invariant check (§8) ======================================== */

typedef struct {
    void (*report)(void *ctx, const char *line);
    void *ctx;
    long problems;
} Checker;

static void problem(Checker *ck, const char *fmt, ...) {
    char line[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    ck->problems++;
    if (ck->report) {
        ck->report(ck->ctx, line);
    }
}

typedef struct {
    char **names;
    size_t n, cap;
} Names;

static int collect(void *ctx, const char *name) {
    Names *ns = ctx;
    if (ns->n == ns->cap) {
        ns->cap = ns->cap ? ns->cap * 2 : 16;
        ns->names = realloc(ns->names, ns->cap * sizeof *ns->names);
        if (!ns->names) {
            abort();
        }
    }
    ns->names[ns->n++] = dup_text(name);
    return 0;
}

typedef struct {
    unsigned char (*trees)[TREE_LEN];
    size_t n, cap;
} Trees;

static int collect_tree(void *ctx, GsBytes name) {
    Trees *ts = ctx;
    if (name.n == TREE_LEN && name.p[0] == 0 && (name.p[1] == 'r' || name.p[1] == 'i')) {
        if (ts->n == ts->cap) {
            ts->cap = ts->cap ? ts->cap * 2 : 16;
            ts->trees = realloc(ts->trees, ts->cap * TREE_LEN);
            if (!ts->trees) {
                abort();
            }
        }
        memcpy(ts->trees[ts->n++], name.p, TREE_LEN);
    }
    return 0;
}

static void check_table(const GsEngine *e, GsTxn *t, const GsqlTable *tb, Checker *ck) {
    GsCursor *c;
    if (e->cursor_open(t, gsql_table_rows_tree(tb), &c) != GS_OK) {
        problem(ck, "%s: its rows tree is missing", tb->name);
        return;
    }
    GsBytes *v = malloc(tb->ncols * sizeof *v);
    if (!v) {
        abort();
    }
    uint64_t rows = 0;
    int pos = 0;
    int st = e->first(c, &pos);
    while (st == GS_OK && pos) {
        GsBytes rk, rv;
        e->key(c, &rk);
        e->value(c, &rv);
        rows++;
        GsqlError er;
        if (row_decode(tb, rv.p, rv.n, v, &er) != GSQL_OK) {
            problem(ck, "%s", er.message);
        } else {
            for (size_t i = 0; i < tb->ncols; i++) {
                if (value_fits(tb->name, &tb->cols[i], v[i].p, v[i].n, &er) != GSQL_OK) {
                    problem(ck, "a stored row: %s", er.message);
                }
                if (v[i].n == 1 && v[i].p[0] == SER_NULL) {
                    problem(ck, "%s.%s: a stored nothing, which is stored as unknown", tb->name, tb->cols[i].name);
                }
            }
            GsqlKey k;
            gsql_key_init(&k);
            if (tb->nkey) {
                if (primary_key(tb, v, &k, &er) != GSQL_OK || !same_bytes(key_bytes(&k), rk)) {
                    problem(ck, "%s: a row is stored under a key its columns do not make", tb->name);
                }
            } else {
                /* A hidden id is an exact positive integer key: 11 bytes. */
                if (rk.n != 11) {
                    problem(ck, "%s: a hidden row key of %zu bytes", tb->name, rk.n);
                }
            }
            gsql_key_free(&k);
            Entries en = { 0 };
            if (entries_build(tb, v, rk, &en, &er) != GSQL_OK) {
                problem(ck, "%s", er.message);
            } else {
                for (size_t i = 0; i < tb->nidx; i++) {
                    GsBytes got;
                    int gs = e->get(t, gsql_index_tree(tb, i), key_bytes(&en.k[i]), &got);
                    if (gs != GS_OK) {
                        problem(ck, "%s: index %s has no entry for a row", tb->name, tb->idx[i].name);
                    } else if (en.unique_form[i] ? !same_bytes(got, rk) : got.n != 0) {
                        problem(ck, "%s: index %s's entry for a row points elsewhere", tb->name, tb->idx[i].name);
                    }
                }
            }
            entries_free(tb, &en);
        }
        st = e->next(c, &pos);
    }
    e->cursor_close(c);
    free(v);
    if (st != GS_OK) {
        problem(ck, "%s: reading its rows answered %s", tb->name, gs_status_name(st));
    }
    /* Every index holds exactly one entry per row, so nothing else. */
    for (size_t i = 0; i < tb->nidx; i++) {
        GsCursor *ic;
        if (e->cursor_open(t, gsql_index_tree(tb, i), &ic) != GS_OK) {
            problem(ck, "%s: index %s's tree is missing", tb->name, tb->idx[i].name);
            continue;
        }
        uint64_t n = 0;
        int p2 = 0;
        int s2 = e->first(ic, &p2);
        while (s2 == GS_OK && p2) {
            GsBytes ek, ev, rk;
            e->key(ic, &ek);
            e->value(ic, &ev);
            GsqlError er;
            if (gsql_index_rowkey(tb, i, ek, ev, &rk, &er) != GSQL_OK) {
                problem(ck, "%s", er.message);
            } else {
                GsBytes got;
                if (e->get(t, gsql_table_rows_tree(tb), rk, &got) != GS_OK) {
                    problem(ck, "%s: index %s points at a row that is not there", tb->name, tb->idx[i].name);
                }
            }
            n++;
            s2 = e->next(ic, &p2);
        }
        e->cursor_close(ic);
        if (n != rows) {
            problem(ck, "%s: index %s holds %llu entries for %llu rows", tb->name, tb->idx[i].name,
                    (unsigned long long)n, (unsigned long long)rows);
        }
    }
}

int gsql_table_check(const GsEngine *e, GsTxn *t, void (*report)(void *ctx, const char *line), void *ctx,
                     GsqlError *err) {
    Checker ck = { report, ctx, 0 };
    Names ns = { 0 };
    int rc = gsql_table_list(e, t, collect, &ns, err);
    if (rc) {
        return rc;
    }
    Trees owned = { 0 };
    for (size_t i = 0; i < ns.n; i++) {
        GsqlTable *tb;
        GsqlError er;
        if (gsql_table_open(e, t, ns.names[i], &tb, &er) != GSQL_OK) {
            problem(&ck, "%s", er.message);
        } else {
            check_table(e, t, tb, &ck);
            collect_tree(&owned, gsql_table_rows_tree(tb));
            for (size_t j = 0; j < tb->nidx; j++) {
                collect_tree(&owned, gsql_index_tree(tb, j));
            }
            gsql_table_close(tb);
        }
        free(ns.names[i]);
    }
    free(ns.names);
    /* No rows or index tree that no table names: a drop that left one behind. */
    Trees all = { 0 };
    e->tree_list(t, collect_tree, &all);
    for (size_t i = 0; i < all.n; i++) {
        int found = 0;
        for (size_t j = 0; j < owned.n && !found; j++) {
            found = memcmp(all.trees[i], owned.trees[j], TREE_LEN) == 0;
        }
        if (!found) {
            problem(&ck, "a %s tree that no table names", all.trees[i][1] == 'r' ? "rows" : "index");
        }
    }
    free(all.trees);
    free(owned.trees);
    if (ck.problems) {
        return fail(err, GSQL_CORRUPT, "%ld problems", ck.problems);
    }
    return ok(err);
}

/* G-SQL's version store (docs/gsql_storage_design.md §4). See
 * include/gsql_version.h for the contract and the layout; this file is the
 * encoding and the two seeks.
 *
 * Checked by tests/gsql/version_test.c against a model holding every value
 * every row ever had, at every commit id and every second, across reopens. */

#include "gsql_version.h"
#include "store_common.h"

/* GsBuf's helpers answer 1 on success and 0 when memory ran out. */

#include <stdlib.h>
#include <string.h>

static const unsigned char HISTORY[] = "\0history";
static const unsigned char COMMITTIME[] = "\0committime";
#define HIST_NAME gsb(HISTORY, sizeof HISTORY - 1)
#define TIME_NAME gsb(COMMITTIME, sizeof COMMITTIME - 1)

enum { V_REMOVED = 1 };
#define V_HDR 13 /* flags, second, author */

void gsql_version_value_free(GsqlVersionValue *v) {
    free(v->bytes);
    v->bytes = NULL;
    v->length = v->capacity = 0;
}

static int value_set(GsqlVersionValue *v, const unsigned char *p, size_t n) {
    if (n > v->capacity) {
        unsigned char *b = realloc(v->bytes, n ? n : 1);
        if (!b) return GS_IO;
        v->bytes = b;
        v->capacity = n;
    }
    if (n) memcpy(v->bytes, p, n);
    v->length = n;
    return GS_OK;
}

/* The prefix that names one column of one row: injective and prefix-free, so
 * a key a floor seek lands on is this column's exactly when it begins with
 * this prefix and is 8 bytes longer. */
static int hist_prefix(GsBuf *k, GsBytes table, GsBytes row, uint32_t column) {
    unsigned char v[10];
    k->n = 0;
    if (!gsbuf_append(k, v, varint_put(v, table.n)) || !gsbuf_append(k, table.p, table.n) ||
        !gsbuf_append(k, v, varint_put(v, row.n)) || !gsbuf_append(k, row.p, row.n) ||
        !gsbuf_append(k, v, varint_put(v, column)))
        return GS_IO;
    return GS_OK;
}

static int hist_key(GsBuf *k, GsBytes table, GsBytes row, uint32_t column, uint64_t start) {
    unsigned char b[8];
    if (hist_prefix(k, table, row, column)) return GS_IO;
    put64be(b, start);
    return gsbuf_append(k, b, 8) ? GS_OK : GS_IO;
}

static int put_creating(const GsEngine *e, GsTxn *w, GsBytes tree, GsBytes key, GsBytes value) {
    int rc = e->put(w, tree, key, value, 0);
    if (rc == GS_NOT_FOUND) {
        rc = e->tree_create(w, tree);
        if (rc == GS_OK) rc = e->put(w, tree, key, value, 0);
    }
    return rc;
}

/* A floor seek: the greatest record at or before `key` in `tree`, copied
 * out. *found 0 when there is none, or the tree does not exist. */
static int floor_of(const GsEngine *e, GsTxn *t, GsBytes tree, GsBytes key, GsBuf *k, GsBuf *v, int *found) {
    GsCursor *c;
    *found = 0;
    int rc = e->cursor_open(t, tree, &c);
    if (rc == GS_NOT_FOUND) return GS_OK;
    if (rc) return rc;
    int pos = 0;
    rc = e->seek(c, key, GS_SEEK_LE, &pos);
    if (rc == GS_OK && pos) {
        GsBytes kb, vb;
        rc = e->key(c, &kb);
        if (rc == GS_OK) rc = e->value(c, &vb);
        if (rc == GS_OK && (!gsbuf_set(k, kb.p, kb.n) || !gsbuf_set(v, vb.p, vb.n))) rc = GS_IO;
        if (rc == GS_OK) *found = 1;
    }
    e->cursor_close(c);
    return rc;
}

int gsql_version_at(const GsEngine *e, GsTxn *t, uint64_t second, uint64_t *id) {
    unsigned char key[8];
    GsBuf k = {0}, v = {0};
    int found;
    put64be(key, second);
    *id = 0;
    int rc = floor_of(e, t, TIME_NAME, gsb(key, 8), &k, &v, &found);
    if (rc == GS_OK && found) {
        if (k.n != 8 || v.n != 8) rc = GS_CORRUPT;
        else *id = get64(v.p);
    }
    gsbuf_free(&k);
    gsbuf_free(&v);
    return rc;
}

int gsql_version_begin(const GsEngine *e, GsTxn *w, uint64_t second, uint32_t author, GsqlStamp *stamp) {
    if (!e->txn_id) return GS_MISUSE;
    uint64_t id;
    int rc = e->txn_id(w, &id);
    if (rc) return rc;
    /* the last second indexed: the floor of the greatest second there is */
    unsigned char key[8], val[8];
    GsBuf k = {0}, v = {0};
    int found;
    memset(key, 0xff, 8);
    rc = floor_of(e, w, TIME_NAME, gsb(key, 8), &k, &v, &found);
    if (rc == GS_OK && found) {
        if (k.n != 8) rc = GS_CORRUPT;
        else if (get64be(k.p) > second) second = get64be(k.p);
    }
    gsbuf_free(&k);
    gsbuf_free(&v);
    if (rc) return rc;
    put64be(key, second);
    put64(val, id);
    rc = put_creating(e, w, TIME_NAME, gsb(key, 8), gsb(val, 8));
    if (rc) return rc;
    /* so a read in this same transaction finds the history tree */
    GsCursor *c;
    rc = e->cursor_open(w, HIST_NAME, &c);
    if (rc == GS_NOT_FOUND) rc = e->tree_create(w, HIST_NAME);
    else if (rc == GS_OK) e->cursor_close(c);
    if (rc) return rc;
    stamp->start = id;
    stamp->second = second;
    stamp->author = author;
    return GS_OK;
}

static int put_version(const GsEngine *e, GsTxn *w, GsBytes table, GsBytes row, uint32_t column, uint64_t start,
                       unsigned flags, uint64_t second, uint32_t author, GsBytes value) {
    GsBuf k = {0}, v = {0};
    unsigned char h[V_HDR];
    h[0] = (unsigned char)flags;
    put64(h + 1, second);
    put32(h + 9, author);
    int rc = hist_key(&k, table, row, column, start);
    if (rc == GS_OK && (!gsbuf_append(&v, h, V_HDR) || !gsbuf_append(&v, value.p, value.n))) rc = GS_IO;
    if (rc == GS_OK) rc = put_creating(e, w, HIST_NAME, gsb(k.p, k.n), gsb(v.p, v.n));
    gsbuf_free(&k);
    gsbuf_free(&v);
    return rc;
}

int gsql_version_keep(const GsEngine *e, GsTxn *w, GsBytes table, GsBytes row, uint32_t column,
                      const GsqlStamp *was, GsBytes old, const GsqlStamp *now) {
    /* A value this transaction made current is not kept: no reader saw it.
     * This saves a write and nothing else, MEASURED: kept, it sits at this
     * transaction's own id, where a read below that id never reaches and a
     * read at or above it finds the current row or the removal written over
     * it, so the model check is green either way. */
    if (was->start >= now->start) return GS_OK;
    return put_version(e, w, table, row, column, was->start, 0, was->second, was->author, old);
}

int gsql_version_remove(const GsEngine *e, GsTxn *w, GsBytes table, GsBytes row, uint32_t column,
                        const GsqlStamp *now) {
    return put_version(e, w, table, row, column, now->start, V_REMOVED, now->second, now->author, gsb("", 0));
}

int gsql_version_asof(const GsEngine *e, GsTxn *t, GsBytes table, GsBytes row, uint32_t column, uint64_t asof,
                      const GsqlStamp *cur, GsBytes cur_value, GsqlStamp *stamp, GsqlVersionValue *value) {
    if (cur && cur->start <= asof) {
        *stamp = *cur;
        return value_set(value, cur_value.p, cur_value.n);
    }
    GsBuf key = {0}, k = {0}, v = {0};
    int found = 0;
    int rc = hist_key(&key, table, row, column, asof);
    if (rc == GS_OK) rc = floor_of(e, t, HIST_NAME, gsb(key.p, key.n), &k, &v, &found);
    if (rc == GS_OK) {
        /* another column's history, or before this column's first version */
        if (!found || k.n != key.n || memcmp(k.p, key.p, key.n - 8)) rc = GS_NOT_FOUND;
        else if (v.n < V_HDR) rc = GS_CORRUPT;
        else if (v.p[0] & V_REMOVED) rc = GS_NOT_FOUND;
        else {
            stamp->start = get64be(k.p + k.n - 8);
            stamp->second = get64(v.p + 1);
            stamp->author = get32(v.p + 9);
            rc = value_set(value, v.p + V_HDR, v.n - V_HDR);
        }
    }
    gsbuf_free(&key);
    gsbuf_free(&k);
    gsbuf_free(&v);
    return rc;
}

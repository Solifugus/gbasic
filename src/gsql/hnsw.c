/* An HNSW graph over a VECTOR(N) column, in the engine (src/gsql/hnsw.h;
 * docs/gsql_vector_design.md §9). Malkov and Yashunin's algorithm: search
 * descends greedily through the upper levels and widens to a beam of `ef` at
 * the bottom; insert does the same with ef_construction and links the node
 * to neighbours chosen by the paper's heuristic (Algorithm 4), shrinking a
 * neighbour's list the same way when it grows past its limit.
 *
 * One operation's nodes are read once each into a table of its own (Ctx), so
 * a node met twice costs one get, and the lists an insert changes are written
 * once, at its end. Ties are broken by row key everywhere, so the graph and
 * every answer are the same on every machine. */

#include "hnsw.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gb_vector.h"
#include "serialize_tags.h"

static int hfail(GsqlError *err, int code, const char *fmt, ...) {
    if (err) {
        err->code = code;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err->message, sizeof err->message, fmt, ap);
        va_end(ap);
    }
    return code;
}

static int hstorage(GsqlError *err, int st, const char *doing) {
    return hfail(err, GSQL_STORAGE, "the vector index failed %s: the engine answered %s", doing, gs_status_name(st));
}

/* ---- the bytes -------------------------------------------------------- */

static float f32_at(const unsigned char *p) {
    uint32_t bits = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static void f32_put(unsigned char *p, float f) {
    uint32_t bits;
    memcpy(&bits, &f, 4);
    p[0] = (unsigned char)bits;
    p[1] = (unsigned char)(bits >> 8);
    p[2] = (unsigned char)(bits >> 16);
    p[3] = (unsigned char)(bits >> 24);
}

int gsql_hnsw_floats(const unsigned char *p, size_t n, size_t dim, float **out, size_t *dim_out) {
    if (n < 2 || p[0] != SER_VECTOR) {
        return 0;
    }
    uint64_t d = 0;
    size_t at = 1;
    for (int shift = 0; at < n; shift += 7) {
        unsigned char b = p[at++];
        if (shift > 56) {
            return 0;
        }
        d |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            break;
        }
    }
    if (d == 0 || (dim && d != dim) || (n - at) != d * 4) {
        return 0;
    }
    float *f = malloc((size_t)d * sizeof *f);
    if (!f) {
        abort();
    }
    for (size_t i = 0; i < d; i++) {
        f[i] = f32_at(p + at + 4 * i);
    }
    *out = f;
    if (dim_out) {
        *dim_out = (size_t)d;
    }
    return 1;
}

/* a key in the tree: one letter, then the row key */
static GsBytes tagged(unsigned char tag, GsBytes rowkey, unsigned char *buf) {
    buf[0] = tag;
    memcpy(buf + 1, rowkey.p, rowkey.n);
    return (GsBytes){ buf, rowkey.n + 1 };
}

/* ---- one operation's nodes ------------------------------------------- */

typedef struct {
    size_t *v;
    size_t n, cap;
} IdxList;

typedef struct {
    unsigned char *key;
    size_t n;
    uint64_t hash;
    float *vec;          /* NULL until read */
    int missing;         /* read, and not there: a dangling edge */
    int have_lists;
    int level;
    IdxList *lists;      /* level + 1 of them */
    int dirty;           /* its lists changed: written at the end */
    unsigned epoch;      /* the search that last visited it */
    double d;            /* its distance to the query, for that search */
} Node;

typedef struct {
    const GsEngine *e;
    GsTxn *t;
    const GsqlHnsw *h;
    Node *nodes;
    size_t n, cap;
    size_t *slots;       /* open addressing over nodes, 0 empty, else index + 1 */
    size_t nslots;
    unsigned epoch;
    GsqlError *err;
} Ctx;

static uint64_t key_hash(const unsigned char *p, size_t n) {
    uint64_t h = 1469598103934665603ULL;   /* FNV-1a */
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void list_push(IdxList *l, size_t v) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = realloc(l->v, l->cap * sizeof *l->v);
        if (!l->v) {
            abort();
        }
    }
    l->v[l->n++] = v;
}

static void ctx_free(Ctx *c) {
    for (size_t i = 0; i < c->n; i++) {
        free(c->nodes[i].key);
        free(c->nodes[i].vec);
        if (c->nodes[i].lists) {
            for (int l = 0; l <= c->nodes[i].level; l++) {
                free(c->nodes[i].lists[l].v);
            }
        }
        free(c->nodes[i].lists);
    }
    free(c->nodes);
    free(c->slots);
}

static void slots_grow(Ctx *c) {
    size_t ns = c->nslots ? c->nslots * 2 : 256;
    size_t *s = calloc(ns, sizeof *s);
    if (!s) {
        abort();
    }
    for (size_t i = 0; i < c->n; i++) {
        size_t at = (size_t)(c->nodes[i].hash & (ns - 1));
        while (s[at]) {
            at = (at + 1) & (ns - 1);
        }
        s[at] = i + 1;
    }
    free(c->slots);
    c->slots = s;
    c->nslots = ns;
}

/* the node of this row key, made (unread) if the operation has not met it */
static size_t node_of(Ctx *c, const unsigned char *key, size_t n) {
    if (2 * (c->n + 1) > c->nslots) {
        slots_grow(c);
    }
    uint64_t h = key_hash(key, n);
    size_t at = (size_t)(h & (c->nslots - 1));
    while (c->slots[at]) {
        Node *x = &c->nodes[c->slots[at] - 1];
        if (x->hash == h && x->n == n && memcmp(x->key, key, n) == 0) {
            return c->slots[at] - 1;
        }
        at = (at + 1) & (c->nslots - 1);
    }
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 64;
        c->nodes = realloc(c->nodes, c->cap * sizeof *c->nodes);
        if (!c->nodes) {
            abort();
        }
    }
    Node *x = &c->nodes[c->n];
    memset(x, 0, sizeof *x);
    x->key = malloc(n ? n : 1);
    if (!x->key) {
        abort();
    }
    memcpy(x->key, key, n);
    x->n = n;
    x->hash = h;
    x->level = -1;
    c->slots[at] = c->n + 1;
    return c->n++;
}

/* the vector: 1, 0 when the node is gone (an edge to a deleted row), -1 failed */
static int node_vec(Ctx *c, size_t i) {
    Node *x = &c->nodes[i];
    if (x->vec) {
        return 1;
    }
    if (x->missing) {
        return 0;
    }
    unsigned char kb[GS_MAX_KEY + 1];
    GsBytes got;
    int st = c->e->get(c->t, c->h->tree, tagged('v', (GsBytes){ x->key, x->n }, kb), &got);
    if (st == GS_NOT_FOUND) {
        x->missing = 1;
        return 0;
    }
    if (st != GS_OK) {
        hstorage(c->err, st, "reading a vector");
        return -1;
    }
    if (got.n != c->h->dim * 4) {
        hfail(c->err, GSQL_CORRUPT, "a vector index holds a vector of the wrong length");
        return -1;
    }
    x = &c->nodes[i];
    x->vec = malloc(c->h->dim * sizeof *x->vec);
    if (!x->vec) {
        abort();
    }
    for (size_t k = 0; k < c->h->dim; k++) {
        x->vec[k] = f32_at(got.p + 4 * k);
    }
    return 1;
}

/* the neighbour lists: 1, 0 gone, -1 failed */
static int node_lists(Ctx *c, size_t i) {
    if (c->nodes[i].have_lists) {
        return 1;
    }
    if (c->nodes[i].missing) {
        return 0;
    }
    unsigned char kb[GS_MAX_KEY + 1];
    GsBytes got;
    int st = c->e->get(c->t, c->h->tree, tagged('n', (GsBytes){ c->nodes[i].key, c->nodes[i].n }, kb), &got);
    if (st == GS_NOT_FOUND) {
        c->nodes[i].missing = 1;
        return 0;
    }
    if (st != GS_OK) {
        hstorage(c->err, st, "reading a node");
        return -1;
    }
    /* copied out before node_of can call the engine again */
    unsigned char *copy = malloc(got.n ? got.n : 1);
    if (!copy) {
        abort();
    }
    memcpy(copy, got.p, got.n);
    size_t n = got.n, at = 0;
    int ok = n >= 1 && copy[0] <= GSQL_HNSW_MAX_LEVEL;
    int level = ok ? copy[0] : 0;
    at = 1;
    IdxList *lists = calloc((size_t)level + 1, sizeof *lists);
    if (!lists) {
        abort();
    }
    for (int l = 0; ok && l <= level; l++) {
        if (at + 2 > n) {
            ok = 0;
            break;
        }
        unsigned cnt = (unsigned)copy[at] | (unsigned)copy[at + 1] << 8;
        at += 2;
        for (unsigned j = 0; ok && j < cnt; j++) {
            if (at + 2 > n) {
                ok = 0;
                break;
            }
            size_t kl = (size_t)copy[at] | (size_t)copy[at + 1] << 8;
            at += 2;
            if (at + kl > n || kl == 0 || kl > GS_MAX_KEY) {
                ok = 0;
                break;
            }
            list_push(&lists[l], node_of(c, copy + at, kl));
            at += kl;
        }
    }
    free(copy);
    if (!ok || at != n) {
        for (int l = 0; l <= level; l++) {
            free(lists[l].v);
        }
        free(lists);
        hfail(c->err, GSQL_CORRUPT, "a vector index holds a node that does not parse");
        return -1;
    }
    Node *x = &c->nodes[i];
    x->lists = lists;
    x->level = level;
    x->have_lists = 1;
    return 1;
}

static int node_write(Ctx *c, size_t i) {
    Node *x = &c->nodes[i];
    size_t len = 1;
    for (int l = 0; l <= x->level; l++) {
        len += 2;
        for (size_t j = 0; j < x->lists[l].n; j++) {
            len += 2 + c->nodes[x->lists[l].v[j]].n;
        }
    }
    unsigned char *b = malloc(len);
    if (!b) {
        abort();
    }
    size_t at = 0;
    b[at++] = (unsigned char)x->level;
    for (int l = 0; l <= x->level; l++) {
        b[at++] = (unsigned char)x->lists[l].n;
        b[at++] = (unsigned char)(x->lists[l].n >> 8);
        for (size_t j = 0; j < x->lists[l].n; j++) {
            const Node *y = &c->nodes[x->lists[l].v[j]];
            b[at++] = (unsigned char)y->n;
            b[at++] = (unsigned char)(y->n >> 8);
            memcpy(b + at, y->key, y->n);
            at += y->n;
        }
    }
    unsigned char kb[GS_MAX_KEY + 1];
    int st = c->e->put(c->t, c->h->tree, tagged('n', (GsBytes){ x->key, x->n }, kb), (GsBytes){ b, len }, 0);
    free(b);
    return st == GS_OK ? GSQL_OK : hstorage(c->err, st, "writing a node");
}

/* ---- the entry point and the count ------------------------------------ */

static const unsigned char KEY_ENTRY[1] = { 'e' };
static const unsigned char KEY_COUNT[1] = { 'c' };

/* 1 with the entry's node and level, 0 an empty graph, -1 failed */
static int entry_get(Ctx *c, size_t *node, int *level) {
    GsBytes got;
    int st = c->e->get(c->t, c->h->tree, (GsBytes){ KEY_ENTRY, 1 }, &got);
    if (st == GS_NOT_FOUND) {
        return 0;
    }
    if (st != GS_OK) {
        hstorage(c->err, st, "reading its entry point");
        return -1;
    }
    if (got.n < 2 || got.p[0] > GSQL_HNSW_MAX_LEVEL) {
        hfail(c->err, GSQL_CORRUPT, "a vector index's entry point does not parse");
        return -1;
    }
    *level = got.p[0];
    *node = node_of(c, got.p + 1, got.n - 1);
    return 1;
}

static int entry_put(Ctx *c, size_t node, int level) {
    const Node *x = &c->nodes[node];
    unsigned char b[GS_MAX_KEY + 1];
    b[0] = (unsigned char)level;
    memcpy(b + 1, x->key, x->n);
    int st = c->e->put(c->t, c->h->tree, (GsBytes){ KEY_ENTRY, 1 }, (GsBytes){ b, x->n + 1 }, 0);
    return st == GS_OK ? GSQL_OK : hstorage(c->err, st, "writing its entry point");
}

int gsql_hnsw_count(const GsEngine *e, GsTxn *t, const GsqlHnsw *h, uint64_t *count, GsqlError *err) {
    GsBytes got;
    int st = e->get(t, h->tree, (GsBytes){ KEY_COUNT, 1 }, &got);
    *count = 0;
    if (st == GS_NOT_FOUND) {
        return GSQL_OK;
    }
    if (st != GS_OK) {
        return hstorage(err, st, "reading its count");
    }
    if (got.n != 8) {
        return hfail(err, GSQL_CORRUPT, "a vector index's count does not parse");
    }
    for (int k = 0; k < 8; k++) {
        *count |= (uint64_t)got.p[k] << (8 * k);
    }
    return GSQL_OK;
}

static int count_add(Ctx *c, int delta) {
    uint64_t n;
    int rc = gsql_hnsw_count(c->e, c->t, c->h, &n, c->err);
    if (rc != GSQL_OK) {
        return rc;
    }
    n = delta < 0 && n == 0 ? 0 : n + (uint64_t)(int64_t)delta;
    unsigned char b[8];
    for (int k = 0; k < 8; k++) {
        b[k] = (unsigned char)(n >> (8 * k));
    }
    int st = c->e->put(c->t, c->h->tree, (GsBytes){ KEY_COUNT, 1 }, (GsBytes){ b, 8 }, 0);
    return st == GS_OK ? GSQL_OK : hstorage(c->err, st, "writing its count");
}

/* ---- distances and the search ---------------------------------------- */

static double dist_to(Ctx *c, const float *q, size_t i) {
    return gb_vec_distance((GbVecMetric)c->h->metric, q, c->nodes[i].vec, c->h->dim);
}

/* a before b: nearer, then the smaller row key */
static int closer(const Ctx *c, double da, size_t a, double db, size_t b) {
    if (da != db) {
        return da < db;
    }
    const Node *x = &c->nodes[a], *y = &c->nodes[b];
    int k = memcmp(x->key, y->key, x->n < y->n ? x->n : y->n);
    return k ? k < 0 : x->n < y->n;
}

typedef struct {
    double d;
    size_t i;
} Item;

typedef struct {
    Item *v;
    size_t n, cap;
    int max;              /* a max-heap (the results) or a min-heap (the candidates) */
} Heap;

static int heap_before(const Ctx *c, const Heap *h, Item a, Item b) {
    return h->max ? closer(c, b.d, b.i, a.d, a.i) : closer(c, a.d, a.i, b.d, b.i);
}

static void heap_push(const Ctx *c, Heap *h, Item it) {
    if (h->n == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 64;
        h->v = realloc(h->v, h->cap * sizeof *h->v);
        if (!h->v) {
            abort();
        }
    }
    size_t at = h->n++;
    h->v[at] = it;
    while (at && heap_before(c, h, h->v[at], h->v[(at - 1) / 2])) {
        Item t = h->v[at];
        h->v[at] = h->v[(at - 1) / 2];
        h->v[(at - 1) / 2] = t;
        at = (at - 1) / 2;
    }
}

static Item heap_pop(const Ctx *c, Heap *h) {
    Item top = h->v[0];
    h->v[0] = h->v[--h->n];
    size_t at = 0;
    for (;;) {
        size_t l = 2 * at + 1, r = l + 1, best = at;
        if (l < h->n && heap_before(c, h, h->v[l], h->v[best])) {
            best = l;
        }
        if (r < h->n && heap_before(c, h, h->v[r], h->v[best])) {
            best = r;
        }
        if (best == at) {
            break;
        }
        Item t = h->v[at];
        h->v[at] = h->v[best];
        h->v[best] = t;
        at = best;
    }
    return top;
}

static Ctx *g_sort_ctx;   /* qsort has no context argument; one operation at a time */

static int item_order(const void *pa, const void *pb) {
    const Item *a = pa, *b = pb;
    if (closer(g_sort_ctx, a->d, a->i, b->d, b->i)) {
        return -1;
    }
    return closer(g_sort_ctx, b->d, b->i, a->d, a->i) ? 1 : 0;
}

static void items_sort(Ctx *c, Item *v, size_t n) {
    g_sort_ctx = c;
    if (n > 1) {
        qsort(v, n, sizeof *v, item_order);
    }
}

/* The beam search at one level from the entry points `ep`: the up to `ef`
 * nearest found, nearest first, into *out (owned by the caller). -1 failed. */
static int search_layer(Ctx *c, const float *q, const Item *ep, size_t nep, size_t ef, int level, Item **out,
                        size_t *nout) {
    c->epoch++;
    Heap cand = { NULL, 0, 0, 0 }, res = { NULL, 0, 0, 1 };
    for (size_t k = 0; k < nep; k++) {
        c->nodes[ep[k].i].epoch = c->epoch;
        heap_push(c, &cand, ep[k]);
        heap_push(c, &res, ep[k]);
        while (res.n > ef) {
            heap_pop(c, &res);
        }
    }
    int rc = 0;
    while (cand.n) {
        Item near = heap_pop(c, &cand);
        Item far = res.v[0];
        if (res.n >= ef && closer(c, far.d, far.i, near.d, near.i)) {
            break;
        }
        int got = node_lists(c, near.i);
        if (got < 0) {
            rc = -1;
            break;
        }
        if (got == 0 || c->nodes[near.i].level < level) {
            continue;
        }
        for (size_t j = 0; j < c->nodes[near.i].lists[level].n; j++) {
            size_t nb = c->nodes[near.i].lists[level].v[j];
            if (c->nodes[nb].epoch == c->epoch) {
                continue;
            }
            c->nodes[nb].epoch = c->epoch;
            int have = node_vec(c, nb);
            if (have < 0) {
                rc = -1;
                break;
            }
            if (have == 0) {
                continue;   /* an edge to a deleted row */
            }
            Item it = { dist_to(c, q, nb), nb };
            if (res.n < ef || closer(c, it.d, it.i, res.v[0].d, res.v[0].i)) {
                heap_push(c, &cand, it);
                heap_push(c, &res, it);
                if (res.n > ef) {
                    heap_pop(c, &res);
                }
            }
        }
        if (rc) {
            break;
        }
    }
    free(cand.v);
    if (rc) {
        free(res.v);
        return -1;
    }
    items_sort(c, res.v, res.n);
    *out = res.v;
    *nout = res.n;
    return 0;
}

/* From the entry point down to `to` + 1, the nearest node at each level, and
 * then the starting points for level `to`. 0 empty graph, 1 positioned, -1
 * failed. */
static int descend(Ctx *c, const float *q, int to, Item *start, int *top) {
    size_t ep;
    int got = entry_get(c, &ep, top);
    if (got <= 0) {
        return got;
    }
    int have = node_vec(c, ep);
    if (have < 0) {
        return -1;
    }
    if (have == 0) {
        hfail(c->err, GSQL_CORRUPT, "a vector index's entry point names a row it does not hold");
        return -1;
    }
    Item cur = { dist_to(c, q, ep), ep };
    for (int l = *top; l > to; l--) {
        Item *w;
        size_t nw;
        if (search_layer(c, q, &cur, 1, 1, l, &w, &nw) < 0) {
            return -1;
        }
        if (nw) {
            cur = w[0];
        }
        free(w);
    }
    *start = cur;
    return 1;
}

/* ---- choosing neighbours (Algorithm 4, keeping the pruned to fill) ----- */

/* `cand` nearest first by distance to `base`; the chosen into *out */
static int select_neighbours(Ctx *c, const Item *cand, size_t n, size_t m, IdxList *out) {
    out->n = 0;
    size_t *skipped = malloc((n ? n : 1) * sizeof *skipped);
    if (!skipped) {
        abort();
    }
    size_t ns = 0;
    for (size_t k = 0; k < n && out->n < m; k++) {
        size_t ci = cand[k].i;
        int good = 1;
        for (size_t j = 0; j < out->n; j++) {
            double d = gb_vec_distance((GbVecMetric)c->h->metric, c->nodes[ci].vec, c->nodes[out->v[j]].vec,
                                       c->h->dim);
            if (d < cand[k].d) {
                good = 0;
                break;
            }
        }
        if (good) {
            list_push(out, ci);
        } else {
            skipped[ns++] = ci;
        }
    }
    for (size_t k = 0; k < ns && out->n < m; k++) {
        list_push(out, skipped[k]);
    }
    free(skipped);
    return 0;
}

static size_t level_cap(const GsqlHnsw *h, int level) {
    return level == 0 ? 2 * (size_t)h->m : h->m;
}

/* node `x`'s list at `level` held to its cap, by the heuristic around x */
static int shrink(Ctx *c, size_t x, int level) {
    IdxList *l = &c->nodes[x].lists[level];
    size_t cap = level_cap(c->h, level);
    if (l->n <= cap) {
        return 0;
    }
    Item *cand = malloc(l->n * sizeof *cand);
    if (!cand) {
        abort();
    }
    size_t nc = 0;
    for (size_t j = 0; j < l->n; j++) {
        int have = node_vec(c, l->v[j]);
        if (have < 0) {
            free(cand);
            return -1;
        }
        if (have) {
            cand[nc].i = l->v[j];
            cand[nc].d = gb_vec_distance((GbVecMetric)c->h->metric, c->nodes[x].vec, c->nodes[l->v[j]].vec,
                                         c->h->dim);
            nc++;
        }
    }
    items_sort(c, cand, nc);
    IdxList keep = { NULL, 0, 0 };
    select_neighbours(c, cand, nc, cap, &keep);
    free(cand);
    l = &c->nodes[x].lists[level];
    free(l->v);
    *l = keep;
    return 0;
}

/* ---- the operations --------------------------------------------------- */

static int level_of(const GsqlHnsw *h, GsBytes rowkey) {
    uint64_t r = key_hash(rowkey.p, rowkey.n);
    r ^= r >> 33;                        /* a finaliser, so the low bits mix */
    r *= 0xff51afd7ed558ccdULL;
    r ^= r >> 33;
    double u = ((double)(r >> 11) + 1.0) / 9007199254740992.0;   /* (0, 1] */
    double ml = 1.0 / log((double)(h->m > 1 ? h->m : 2));
    int level = (int)floor(-log(u) * ml);
    return level > GSQL_HNSW_MAX_LEVEL ? GSQL_HNSW_MAX_LEVEL : level;
}

static void node_give_lists(Node *x, int level) {
    x->level = level;
    x->lists = calloc((size_t)level + 1, sizeof *x->lists);
    if (!x->lists) {
        abort();
    }
    x->have_lists = 1;
}

int gsql_hnsw_insert(const GsEngine *e, GsTxn *t, const GsqlHnsw *h, GsBytes rowkey, const float *v,
                     GsqlError *err) {
    if (rowkey.n == 0 || rowkey.n + 1 > GS_MAX_KEY) {
        return hfail(err, GSQL_MISUSE, "a vector index was given a row key it cannot hold");
    }
    Ctx c = { e, t, h, NULL, 0, 0, NULL, 0, 0, err };
    unsigned char kb[GS_MAX_KEY + 1];
    unsigned char *vb = malloc(h->dim * 4);
    if (!vb) {
        abort();
    }
    for (size_t k = 0; k < h->dim; k++) {
        f32_put(vb + 4 * k, v[k]);
    }
    int st = e->put(t, h->tree, tagged('v', rowkey, kb), (GsBytes){ vb, h->dim * 4 }, 0);
    free(vb);
    if (st != GS_OK) {
        return hstorage(err, st, "writing a vector");
    }
    int rc = GSQL_OK;
    size_t me = node_of(&c, rowkey.p, rowkey.n);
    c.nodes[me].vec = malloc(h->dim * sizeof(float));
    if (!c.nodes[me].vec) {
        abort();
    }
    memcpy(c.nodes[me].vec, v, h->dim * sizeof(float));
    int mylevel = level_of(h, rowkey);
    node_give_lists(&c.nodes[me], mylevel);
    c.nodes[me].dirty = 1;
    int top = -1;
    Item start;
    int got = descend(&c, v, mylevel, &start, &top);
    if (got < 0) {
        rc = err && err->code ? err->code : GSQL_STORAGE;
        goto out;
    }
    if (got > 0) {
        Item *ep = malloc(sizeof *ep);
        if (!ep) {
            abort();
        }
        ep[0] = start;
        size_t nep = 1;
        for (int l = mylevel < top ? mylevel : top; l >= 0; l--) {
            Item *w;
            size_t nw;
            if (search_layer(&c, v, ep, nep, h->ef_construction, l, &w, &nw) < 0) {
                free(ep);
                rc = err && err->code ? err->code : GSQL_STORAGE;
                goto out;
            }
            /* the node itself is never its own neighbour (an update writes
               the same row key again) */
            size_t keep = 0;
            for (size_t j = 0; j < nw; j++) {
                if (w[j].i != me) {
                    w[keep++] = w[j];
                }
            }
            nw = keep;
            select_neighbours(&c, w, nw, h->m, &c.nodes[me].lists[l]);
            for (size_t j = 0; j < c.nodes[me].lists[l].n; j++) {
                size_t nb = c.nodes[me].lists[l].v[j];
                if (node_lists(&c, nb) <= 0 || c.nodes[nb].level < l) {
                    continue;
                }
                list_push(&c.nodes[nb].lists[l], me);
                c.nodes[nb].dirty = 1;
                if (shrink(&c, nb, l) < 0) {
                    free(w);
                    free(ep);
                    rc = err && err->code ? err->code : GSQL_STORAGE;
                    goto out;
                }
            }
            free(ep);
            ep = w;
            nep = nw ? nw : 0;
            if (!nep) {
                ep = malloc(sizeof *ep);
                if (!ep) {
                    abort();
                }
                ep[0] = start;
                nep = 1;
            }
        }
        free(ep);
    }
    for (size_t i = 0; i < c.n && rc == GSQL_OK; i++) {
        if (c.nodes[i].dirty) {
            rc = node_write(&c, i);
        }
    }
    if (rc == GSQL_OK && mylevel > top) {
        rc = entry_put(&c, me, mylevel);
    }
    if (rc == GSQL_OK) {
        rc = count_add(&c, 1);
    }
out:
    ctx_free(&c);
    if (rc == GSQL_OK && err) {
        err->code = GSQL_OK;
    }
    return rc;
}

/* x's list at `level` made again from its live neighbours and `extra`, all
 * but `drop`, nearest to x first, chosen by the heuristic: -1 failed */
static int refill(Ctx *c, size_t x, int level, size_t drop, const Item *extra, size_t nextra) {
    IdxList *l = &c->nodes[x].lists[level];
    size_t cap = level_cap(c->h, level);
    Item *cand = malloc((l->n + nextra + 1) * sizeof *cand);
    if (!cand) {
        abort();
    }
    size_t nc = 0;
    c->epoch++;
    c->nodes[x].epoch = c->epoch;
    c->nodes[drop].epoch = c->epoch;
    for (size_t pass = 0; pass < 2; pass++) {
        size_t n = pass == 0 ? c->nodes[x].lists[level].n : nextra;
        for (size_t k = 0; k < n; k++) {
            size_t i = pass == 0 ? c->nodes[x].lists[level].v[k] : extra[k].i;
            if (c->nodes[i].epoch == c->epoch) {
                continue;
            }
            c->nodes[i].epoch = c->epoch;
            int have = node_vec(c, i);
            if (have < 0) {
                free(cand);
                return -1;
            }
            if (have) {
                cand[nc].i = i;
                cand[nc].d = gb_vec_distance((GbVecMetric)c->h->metric, c->nodes[x].vec, c->nodes[i].vec, c->h->dim);
                nc++;
            }
        }
    }
    items_sort(c, cand, nc);
    IdxList keep = { NULL, 0, 0 };
    select_neighbours(c, cand, nc, cap, &keep);
    free(cand);
    l = &c->nodes[x].lists[level];
    free(l->v);
    *l = keep;
    c->nodes[x].dirty = 1;
    return 0;
}

static int list_has(const IdxList *l, size_t v) {
    for (size_t k = 0; k < l->n; k++) {
        if (l->v[k] == v) {
            return 1;
        }
    }
    return 0;
}

/* A deleted node's neighbourhood repaired, as pgvector's vacuum repairs one:
 * a search around its own vector at each of its levels finds the nodes near
 * it, which are the ones that point at it, and each that does drops the edge
 * and is refilled from what the search found. Its own neighbours alone are not
 * enough: an edge need not run both ways (a shrink drops back-links), and with
 * three quarters of the rows deleted, recall@10 fell to 0.62 that way. */
int gsql_hnsw_delete(const GsEngine *e, GsTxn *t, const GsqlHnsw *h, GsBytes rowkey, GsqlError *err) {
    Ctx c = { e, t, h, NULL, 0, 0, NULL, 0, 0, err };
    int rc = GSQL_OK;
    float *q = NULL;
    size_t me = node_of(&c, rowkey.p, rowkey.n);
    int got = node_lists(&c, me);
    if (got <= 0) {
        ctx_free(&c);
        if (got == 0 && err) {
            err->code = GSQL_OK;
        }
        return got == 0 ? GSQL_OK : (err && err->code ? err->code : GSQL_STORAGE);
    }
    if (node_vec(&c, me) <= 0) {
        ctx_free(&c);
        return hfail(err, GSQL_CORRUPT, "a vector index holds a node with no vector");
    }
    q = malloc(h->dim * sizeof *q);
    if (!q) {
        abort();
    }
    memcpy(q, c.nodes[me].vec, h->dim * sizeof *q);
    free(c.nodes[me].vec);
    c.nodes[me].vec = NULL;
    c.nodes[me].missing = 1;   /* from here every edge to it is dangling */
    int mylevel = c.nodes[me].level;
    unsigned char kb[GS_MAX_KEY + 1];
    int st = e->del(t, h->tree, tagged('v', rowkey, kb));
    if (st == GS_OK) {
        st = e->del(t, h->tree, tagged('n', rowkey, kb));
    }
    if (st != GS_OK) {
        rc = hstorage(err, st, "removing a node");
        goto out;
    }
    /* a new entry point when this was it: the highest-level neighbour still
       there, else any node, else none */
    size_t ep;
    int eplevel;
    int had_entry = entry_get(&c, &ep, &eplevel);
    if (had_entry < 0) {
        rc = err && err->code ? err->code : GSQL_STORAGE;
        goto out;
    }
    if (had_entry && ep == me) {
        long best = -1;
        int best_level = -1;
        for (int l = mylevel; l >= 0 && best < 0; l--) {
            for (size_t j = 0; j < c.nodes[me].lists[l].n; j++) {
                size_t nb = c.nodes[me].lists[l].v[j];
                if (node_lists(&c, nb) > 0 && c.nodes[nb].level > best_level) {
                    best = (long)nb;
                    best_level = c.nodes[nb].level;
                }
            }
        }
        if (best < 0) {
            /* none of its neighbours is left: the first node the tree holds */
            GsCursor *cur;
            st = e->cursor_open(t, h->tree, &cur);
            if (st != GS_OK) {
                rc = hstorage(err, st, "opening a cursor");
                goto out;
            }
            int pos = 0;
            unsigned char n0 = 'n';
            st = e->seek(cur, (GsBytes){ &n0, 1 }, GS_SEEK_GE, &pos);
            GsBytes k;
            if (st == GS_OK && pos && e->key(cur, &k) == GS_OK && k.n > 1 && k.p[0] == 'n') {
                best = (long)node_of(&c, k.p + 1, k.n - 1);
            }
            e->cursor_close(cur);
            if (best >= 0 && node_lists(&c, (size_t)best) > 0) {
                best_level = c.nodes[best].level;
            } else {
                best = -1;
            }
        }
        if (best >= 0) {
            rc = entry_put(&c, (size_t)best, best_level);
        } else {
            st = e->del(t, h->tree, (GsBytes){ KEY_ENTRY, 1 });
            rc = st == GS_OK || st == GS_NOT_FOUND ? GSQL_OK : hstorage(err, st, "removing its entry point");
            had_entry = 0;
        }
        if (rc != GSQL_OK) {
            goto out;
        }
    }
    if (had_entry) {
        int top;
        Item start;
        int d = descend(&c, q, mylevel, &start, &top);
        if (d < 0) {
            rc = err && err->code ? err->code : GSQL_STORAGE;
            goto out;
        }
        Item *epv = malloc(sizeof *epv);
        if (!epv) {
            abort();
        }
        epv[0] = start;
        size_t nep = d > 0 ? 1 : 0;
        for (int l = mylevel < top ? mylevel : top; l >= 0 && nep; l--) {
            Item *w;
            size_t nw;
            if (search_layer(&c, q, epv, nep, h->ef_construction, l, &w, &nw) < 0) {
                free(epv);
                rc = err && err->code ? err->code : GSQL_STORAGE;
                goto out;
            }
            /* every node read so far that points at the deleted one at this
               level: its own neighbours were read here, and the search read
               the ones near it */
            for (size_t i = 0; i < c.n; i++) {
                if (i == me || !c.nodes[i].have_lists || c.nodes[i].missing || c.nodes[i].level < l ||
                    !list_has(&c.nodes[i].lists[l], me)) {
                    continue;
                }
                if (node_vec(&c, i) <= 0 || refill(&c, i, l, me, w, nw) < 0) {
                    free(w);
                    free(epv);
                    rc = err && err->code ? err->code : GSQL_CORRUPT;
                    goto out;
                }
            }
            free(epv);
            epv = w;
            nep = nw;
        }
        free(epv);
    }
    for (size_t i = 0; i < c.n && rc == GSQL_OK; i++) {
        if (c.nodes[i].dirty && i != me && !c.nodes[i].missing) {
            rc = node_write(&c, i);
        }
    }
    if (rc == GSQL_OK) {
        rc = count_add(&c, -1);
    }
out:
    free(q);
    ctx_free(&c);
    if (rc == GSQL_OK && err) {
        err->code = GSQL_OK;
    }
    return rc;
}

static int hits_from(Ctx *c, const Item *w, size_t nw, GsqlHnswHit **hits, size_t *nhits) {
    GsqlHnswHit *out = calloc(nw ? nw : 1, sizeof *out);
    if (!out) {
        abort();
    }
    for (size_t k = 0; k < nw; k++) {
        const Node *x = &c->nodes[w[k].i];
        out[k].key = malloc(x->n);
        if (!out[k].key) {
            abort();
        }
        memcpy(out[k].key, x->key, x->n);
        out[k].n = x->n;
        out[k].d = w[k].d;
    }
    *hits = out;
    *nhits = nw;
    return GSQL_OK;
}

int gsql_hnsw_search(const GsEngine *e, GsTxn *t, const GsqlHnsw *h, const float *q, size_t ef,
                     GsqlHnswHit **hits, size_t *nhits, GsqlError *err) {
    Ctx c = { e, t, h, NULL, 0, 0, NULL, 0, 0, err };
    *hits = NULL;
    *nhits = 0;
    int top;
    Item start;
    int got = descend(&c, q, 0, &start, &top);
    int rc = GSQL_OK;
    if (got < 0) {
        rc = err && err->code ? err->code : GSQL_STORAGE;
    } else if (got > 0) {
        Item *w;
        size_t nw;
        if (search_layer(&c, q, &start, 1, ef ? ef : 1, 0, &w, &nw) < 0) {
            rc = err && err->code ? err->code : GSQL_STORAGE;
        } else {
            hits_from(&c, w, nw, hits, nhits);
            free(w);
        }
    }
    ctx_free(&c);
    return rc;
}

int gsql_hnsw_every(const GsEngine *e, GsTxn *t, const GsqlHnsw *h, const float *q, GsqlHnswHit **hits,
                    size_t *nhits, GsqlError *err) {
    Ctx c = { e, t, h, NULL, 0, 0, NULL, 0, 0, err };
    *hits = NULL;
    *nhits = 0;
    GsCursor *cur;
    int st = e->cursor_open(t, h->tree, &cur);
    if (st != GS_OK) {
        return hstorage(err, st, "opening a cursor");
    }
    Item *w = NULL;
    size_t nw = 0, cap = 0;
    int pos = 0;
    unsigned char v0 = 'v';
    st = e->seek(cur, (GsBytes){ &v0, 1 }, GS_SEEK_GE, &pos);
    float *f = malloc((h->dim ? h->dim : 1) * sizeof *f);
    if (!f) {
        abort();
    }
    while (st == GS_OK && pos) {
        GsBytes k, val;
        if ((st = e->key(cur, &k)) != GS_OK || k.n < 2 || k.p[0] != 'v') {
            break;
        }
        if ((st = e->value(cur, &val)) != GS_OK) {
            break;
        }
        if (val.n != h->dim * 4) {
            free(f);
            free(w);
            e->cursor_close(cur);
            ctx_free(&c);
            return hfail(err, GSQL_CORRUPT, "a vector index holds a vector of the wrong length");
        }
        for (size_t i = 0; i < h->dim; i++) {
            f[i] = f32_at(val.p + 4 * i);
        }
        double d = gb_vec_distance((GbVecMetric)h->metric, q, f, h->dim);
        size_t i = node_of(&c, k.p + 1, k.n - 1);
        if (nw == cap) {
            cap = cap ? cap * 2 : 256;
            w = realloc(w, cap * sizeof *w);
            if (!w) {
                abort();
            }
        }
        w[nw].d = d;
        w[nw].i = i;
        nw++;
        st = e->next(cur, &pos);
    }
    free(f);
    e->cursor_close(cur);
    int rc = GSQL_OK;
    if (st != GS_OK) {
        rc = hstorage(err, st, "reading its vectors");
    } else {
        items_sort(&c, w, nw);
        hits_from(&c, w, nw, hits, nhits);
    }
    free(w);
    ctx_free(&c);
    return rc;
}

void gsql_hnsw_hits_free(GsqlHnswHit *hits, size_t n) {
    for (size_t k = 0; k < n; k++) {
        free(hits[k].key);
    }
    free(hits);
}

/* The shared B+tree against a model, over an in-memory pager in both modes it
 * will be used in: copy-on-write (every write relocates a page not yet written
 * in the "transaction", frees deferred to commit) and in place (links between
 * leaves, frees immediate).
 *
 * The model is a sorted array. After every batch: the tree's structural check,
 * every key read back, full scans forwards and backwards, every seek mode at
 * random probes, and a page census -- pages allocated minus pages freed must
 * equal the pages the tree reaches, or it leaks (or double-frees). */

#include "btree.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures < 20) { printf("MISMATCH "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* ---- the pager ---- */

typedef struct {
    BtPager base;
    unsigned char **pages;
    unsigned char *dirty, *live;
    uint64_t n, cap;
    uint64_t *freelist, nfree;
    uint64_t *pending, npending; /* cow: freed at commit */
    int cow;
    uint64_t allocated, freed;
} MemPager;

static int mp_get(BtPager *b, uint64_t pgno, unsigned char **page) {
    MemPager *m = (MemPager *)b;
    if (pgno == 0 || pgno >= m->n || !m->live[pgno]) {
        printf("MISMATCH read of a dead page %llu\n", (unsigned long long)pgno);
        failures++;
        return GS_CORRUPT;
    }
    *page = m->pages[pgno];
    return GS_OK;
}

static int mp_alloc(BtPager *b, uint64_t *pgno, unsigned char **page) {
    MemPager *m = (MemPager *)b;
    uint64_t p;
    if (m->nfree) {
        p = m->freelist[--m->nfree];
    } else {
        if (m->n == m->cap) {
            m->cap = m->cap ? m->cap * 2 : 64;
            m->pages = realloc(m->pages, m->cap * sizeof *m->pages);
            m->dirty = realloc(m->dirty, m->cap);
            m->live = realloc(m->live, m->cap);
            m->freelist = realloc(m->freelist, m->cap * sizeof *m->freelist);
            m->pending = realloc(m->pending, m->cap * sizeof *m->pending);
        }
        if (m->n == 0) m->n = 1; /* page 0 is the header */
        p = m->n++;
        m->pages[p] = malloc(b->page_size);
    }
    memset(m->pages[p], 0, b->page_size);
    m->dirty[p] = 1;
    m->live[p] = 1;
    m->allocated++;
    *pgno = p;
    *page = m->pages[p];
    return GS_OK;
}

static int mp_free(BtPager *b, uint64_t pgno) {
    MemPager *m = (MemPager *)b;
    if (!m->live[pgno]) {
        printf("MISMATCH double free of page %llu\n", (unsigned long long)pgno);
        failures++;
        return GS_CORRUPT;
    }
    m->freed++;
    if (m->cow) {
        m->pending[m->npending++] = pgno; /* still readable until commit */
    } else {
        m->live[pgno] = 0;
        m->freelist[m->nfree++] = pgno;
    }
    return GS_OK;
}

static int mp_write(BtPager *b, uint64_t pgno, uint64_t *out, unsigned char **page) {
    MemPager *m = (MemPager *)b;
    if (!m->cow || m->dirty[pgno]) {
        m->dirty[pgno] = 1;
        *out = pgno;
        return mp_get(b, pgno, page);
    }
    uint64_t np;
    unsigned char *p;
    mp_alloc(b, &np, &p);
    memcpy(p, m->pages[pgno], b->page_size);
    mp_free(b, pgno);
    *out = np;
    *page = p;
    return GS_OK;
}

static void mp_commit(MemPager *m) {
    for (uint64_t i = 0; i < m->npending; i++) {
        m->live[m->pending[i]] = 0;
        m->freelist[m->nfree++] = m->pending[i];
    }
    m->npending = 0;
    memset(m->dirty, 0, m->n);
}

/* ---- the model ---- */

typedef struct {
    GsBuf k, v;
} Rec;
typedef struct {
    Rec *r;
    size_t n, cap;
} Model;

static size_t m_lb(Model *m, GsBytes key, int *found) {
    size_t lo = 0, hi = m->n;
    *found = 0;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = bytes_cmp(gsb(m->r[mid].k.p, m->r[mid].k.n), key);
        if (c < 0) lo = mid + 1;
        else { if (c == 0) *found = 1; hi = mid; }
    }
    return lo;
}
static void m_put(Model *m, GsBytes k, GsBytes v) {
    int f;
    size_t i = m_lb(m, k, &f);
    if (f) { gsbuf_set(&m->r[i].v, v.p, v.n); return; }
    if (m->n == m->cap) { m->cap = m->cap ? m->cap * 2 : 64; m->r = realloc(m->r, m->cap * sizeof *m->r); }
    memmove(&m->r[i + 1], &m->r[i], (m->n - i) * sizeof *m->r);
    memset(&m->r[i], 0, sizeof m->r[i]);
    gsbuf_set(&m->r[i].k, k.p, k.n);
    gsbuf_set(&m->r[i].v, v.p, v.n);
    m->n++;
}
static int m_del(Model *m, GsBytes k) {
    int f;
    size_t i = m_lb(m, k, &f);
    if (!f) return 0;
    gsbuf_free(&m->r[i].k);
    gsbuf_free(&m->r[i].v);
    memmove(&m->r[i], &m->r[i + 1], (m->n - i - 1) * sizeof *m->r);
    m->n--;
    return 1;
}

/* ---- the run ---- */

static uint64_t rng = 1;
static uint64_t rnd(void) {
    uint64_t z = (rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static size_t make_key(unsigned char *k, uint64_t id, size_t maxk) {
    /* variable length, sharing prefixes: the shapes that find off-by-ones in
     * separators */
    int n = snprintf((char *)k, 64, "key%06llu", (unsigned long long)id);
    size_t extra = (id * 7) % 23;
    for (size_t i = 0; i < extra && (size_t)n < maxk; i++) k[n++] = (unsigned char)('a' + (id + i) % 26);
    return (size_t)n;
}

static size_t make_value(unsigned char *v, uint64_t id, uint64_t ver) {
    size_t len;
    uint64_t r = rnd() % 100;
    if (r < 70) len = rnd() % 40;
    else if (r < 95) len = rnd() % 200;
    else len = 200 + rnd() % 3000; /* overflow */
    for (size_t i = 0; i < len; i++) v[i] = (unsigned char)(id * 31 + ver * 7 + i);
    return len;
}

static int census(void *ctx, uint64_t pgno) {
    (void)pgno;
    (*(uint64_t *)ctx)++;
    return 0;
}

static void verify(MemPager *mp, uint64_t root, Model *m, const char *mode, int round) {
    BtPager *pg = &mp->base;
    uint64_t recs = 0;
    int rc = bt_check(pg, root, NULL, NULL, &recs);
    CHECK(rc == GS_OK, "%s round %d: structural check failed", mode, round);
    CHECK(recs == m->n, "%s round %d: tree holds %llu records, model %zu", mode, round, (unsigned long long)recs, m->n);
    uint64_t reach = 0;
    bt_walk(pg, root, census, &reach);
    uint64_t live = mp->allocated - mp->freed;
    CHECK(reach == live, "%s round %d: %llu pages reachable, %llu allocated and not freed (a leak or a double free)",
          mode, round, (unsigned long long)reach, (unsigned long long)live);
    GsBuf v = {0};
    for (size_t i = 0; i < m->n; i++) {
        rc = bt_get(pg, root, gsb(m->r[i].k.p, m->r[i].k.n), &v);
        CHECK(rc == GS_OK && v.n == m->r[i].v.n && (!v.n || memcmp(v.p, m->r[i].v.p, v.n) == 0),
              "%s round %d: get of record %zu", mode, round, i);
    }
    gsbuf_free(&v);
    BtCursor c;
    bt_cursor_init(&c, pg, root);
    size_t i = 0;
    for (rc = bt_first(&c); rc == GS_OK && c.positioned; rc = bt_next(&c), i++) {
        if (i >= m->n || bytes_cmp(gsb(c.key.p, c.key.n), gsb(m->r[i].k.p, m->r[i].k.n)) != 0) break;
        if (i % 7 == 0) {
            bt_cursor_value(&c);
            CHECK(c.value.n == m->r[i].v.n && (!c.value.n || memcmp(c.value.p, m->r[i].v.p, c.value.n) == 0),
                  "%s round %d: cursor value at %zu", mode, round, i);
        }
    }
    CHECK(rc == GS_OK && i == m->n, "%s round %d: forward scan saw %zu of %zu", mode, round, i, m->n);
    i = m->n;
    for (rc = bt_last(&c); rc == GS_OK && c.positioned; rc = bt_prev(&c)) {
        if (i == 0 || bytes_cmp(gsb(c.key.p, c.key.n), gsb(m->r[i - 1].k.p, m->r[i - 1].k.n)) != 0) break;
        i--;
    }
    CHECK(rc == GS_OK && i == 0, "%s round %d: backward scan stopped %zu short", mode, round, i);
    /* every seek mode at random probes, including ones between keys */
    unsigned char kb[128];
    for (int probe = 0; probe < 60; probe++) {
        size_t kn = make_key(kb, rnd() % 2600, 60);
        if (probe % 3 == 0) kb[kn++] = (unsigned char)('a' + probe % 26);
        GsBytes key = gsb(kb, kn);
        int f;
        size_t lb = m_lb(m, key, &f);
        long want[5];
        want[GS_SEEK_GE] = lb < m->n ? (long)lb : -1;
        want[GS_SEEK_GT] = (f ? lb + 1 : lb) < m->n ? (long)(f ? lb + 1 : lb) : -1;
        want[GS_SEEK_LE] = f ? (long)lb : (long)lb - 1;
        want[GS_SEEK_LT] = (long)lb - 1;
        want[GS_SEEK_EXACT] = f ? (long)lb : -1;
        for (int mode2 = 0; mode2 < 5; mode2++) {
            rc = bt_seek(&c, key, (GsSeek)mode2);
            int ok = rc == GS_OK;
            if (want[mode2] < 0) ok = ok && !c.positioned;
            else ok = ok && c.positioned && bytes_cmp(gsb(c.key.p, c.key.n), gsb(m->r[want[mode2]].k.p, m->r[want[mode2]].k.n)) == 0;
            CHECK(ok, "%s round %d: seek mode %d probe %d", mode, round, mode2, probe);
            /* and a step either way from where it landed */
            if (ok && c.positioned && probe % 4 == 0) {
                long at = want[mode2];
                bt_next(&c);
                CHECK(at + 1 < (long)m->n ? c.positioned && bytes_cmp(gsb(c.key.p, c.key.n), gsb(m->r[at + 1].k.p, m->r[at + 1].k.n)) == 0 : !c.positioned,
                      "%s round %d: next after seek", mode, round);
                bt_seek(&c, key, (GsSeek)mode2);
                bt_prev(&c);
                CHECK(at - 1 >= 0 ? c.positioned && bytes_cmp(gsb(c.key.p, c.key.n), gsb(m->r[at - 1].k.p, m->r[at - 1].k.n)) == 0 : !c.positioned,
                      "%s round %d: prev after seek", mode, round);
            }
        }
    }
    bt_cursor_free(&c);
}

static void run(int cow, uint32_t ps, unsigned rounds, unsigned ops) {
    const char *mode = cow ? "cow" : "in-place";
    MemPager mp;
    memset(&mp, 0, sizeof mp);
    mp.base.page_size = ps;
    mp.base.link_leaves = !cow;
    mp.base.get = mp_get;
    mp.base.write = mp_write;
    mp.base.alloc = mp_alloc;
    mp.base.free = mp_free;
    mp.cow = cow;
    BtPager *pg = &mp.base;
    uint64_t root;
    bt_create(pg, &root);
    Model m = {0};
    unsigned char *vb = malloc(4000), kb[128];
    size_t maxk = bt_max_key(ps);
    uint64_t append_next = 900000;
    for (unsigned r = 0; r < rounds; r++) {
        /* shift the balance between growing and shrinking, so merges happen */
        unsigned put_pct = (r % 4 == 3) ? 25 : 65;
        for (unsigned o = 0; o < ops; o++) {
            uint64_t op = rnd() % 100;
            if (op < put_pct) {
                uint64_t id = rnd() % 2500;
                size_t kn = make_key(kb, id, maxk), vn = make_value(vb, id, r);
                unsigned flags = (rnd() % 10 == 0) ? GS_NO_OVERWRITE : 0;
                int f;
                m_lb(&m, gsb(kb, kn), &f);
                int rc = bt_put(pg, &root, gsb(kb, kn), gsb(vb, vn), flags);
                if (flags && f) CHECK(rc == GS_EXISTS, "%s: NO_OVERWRITE over an existing key answered %d", mode, rc);
                else {
                    CHECK(rc == GS_OK, "%s: put answered %s", mode, gs_status_name(rc));
                    m_put(&m, gsb(kb, kn), gsb(vb, vn));
                }
            } else if (op < put_pct + 5) {
                /* APPEND past the end, and once in a while a key that is not */
                int bad = rnd() % 5 == 0 && m.n;
                size_t kn;
                if (bad) { memcpy(kb, m.r[0].k.p, m.r[0].k.n); kn = m.r[0].k.n; }
                else kn = (size_t)snprintf((char *)kb, sizeof kb, "zzz%09llu", (unsigned long long)append_next++);
                size_t vn = make_value(vb, append_next, r);
                int rc = bt_put(pg, &root, gsb(kb, kn), gsb(vb, vn), GS_APPEND);
                if (bad) CHECK(rc == GS_ORDER, "%s: an APPEND not past the end answered %s", mode, gs_status_name(rc));
                else {
                    CHECK(rc == GS_OK, "%s: append answered %s", mode, gs_status_name(rc));
                    m_put(&m, gsb(kb, kn), gsb(vb, vn));
                }
            } else {
                GsBytes key;
                if (m.n && rnd() % 3) { size_t i = (size_t)(rnd() % m.n); memcpy(kb, m.r[i].k.p, m.r[i].k.n); key = gsb(kb, m.r[i].k.n); }
                else key = gsb(kb, make_key(kb, rnd() % 2500, maxk));
                int had = m_del(&m, key);
                int rc = bt_del(pg, &root, key);
                CHECK(rc == (had ? GS_OK : GS_NOT_FOUND), "%s: delete answered %s, model had=%d", mode, gs_status_name(rc), had);
            }
            if (cow && rnd() % 8 == 0) mp_commit(&mp);
        }
        if (cow) mp_commit(&mp);
        verify(&mp, root, &m, mode, (int)r);
    }
    /* empty it completely: every merge and the root collapse */
    while (m.n) {
        GsBytes key = gsb(m.r[m.n - 1].k.p, m.r[m.n - 1].k.n);
        GsBuf k = {0};
        gsbuf_set(&k, key.p, key.n);
        int rc = bt_del(pg, &root, gsb(k.p, k.n));
        CHECK(rc == GS_OK, "%s: draining delete answered %s", mode, gs_status_name(rc));
        m_del(&m, gsb(k.p, k.n));
        gsbuf_free(&k);
        if (cow) mp_commit(&mp);
    }
    verify(&mp, root, &m, mode, -1);
    CHECK(mp.allocated - mp.freed == 1, "%s: an empty tree holds %llu pages", mode, (unsigned long long)(mp.allocated - mp.freed));
    bt_drop(pg, root);
    if (cow) mp_commit(&mp);
    CHECK(mp.allocated == mp.freed, "%s: drop left %llu pages", mode, (unsigned long long)(mp.allocated - mp.freed));
    printf("%s ps=%u: %llu pages allocated over the run\n", mode, ps, (unsigned long long)mp.allocated);
    for (uint64_t i = 1; i < mp.n; i++) free(mp.pages[i]);
    free(mp.pages); free(mp.dirty); free(mp.live); free(mp.freelist); free(mp.pending);
    for (size_t i = 0; i < m.n; i++) { gsbuf_free(&m.r[i].k); gsbuf_free(&m.r[i].v); }
    free(m.r);
    free(vb);
}

int main(int argc, char **argv) {
    rng = argc > 1 ? strtoull(argv[1], NULL, 10) : 1;
    run(1, 512, 24, 400);
    run(0, 512, 24, 400);
    run(1, 4096, 8, 1500);
    run(0, 4096, 8, 1500);
    if (failures) { printf("%u of %u checks failed\n", failures, checks); return 1; }
    printf("ok %u checks\n", checks);
    return 0;
}

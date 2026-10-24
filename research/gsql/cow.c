/* §2.A: the copy-on-write B+tree (LMDB's and BoltDB's shape).
 *
 * A page is never overwritten while anything can see it. A commit writes every
 * changed page -- and every ancestor up to the root -- to a free location,
 * flushes, then writes a META page naming the new catalog root, and flushes
 * again. There are two meta pages, written alternately; recovery takes the
 * valid one with the higher transaction id, so it needs no log.
 *
 * File layout: page 0 the header (§3.1.1); pages 1 and 2 the two meta slots;
 * everything else tree, overflow and free-list pages.
 *
 * As built (recorded in the design):
 *   - the meta slots are whole PAGES 1 and 2, not slots inside page 0's root
 *     area: at a 512-byte page, page 0 is a single sector and two slots in it
 *     could not be written independently (rule 3), and writing a slot means
 *     writing whatever sector it sits in;
 *   - the free list is a blob written at each durable commit (BoltDB's
 *     shape), not a tree keyed by commit id: inline in the meta page when it
 *     fits, else a chain of pages taken from the end of the file so that
 *     writing it never consumes the list it is recording;
 *   - a commit with sync = false writes its pages but no meta. The meta that
 *     makes it durable is written by the next sync, or the next commit with
 *     sync = true, and covers every commit since (group commit, §5.4). A
 *     page freed by a commit is reusable only once that commit is durable AND
 *     no open snapshot predates it -- the first condition is what keeps a
 *     fallback to the older meta safe.
 */

#include "btengine.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define META_A 1
#define META_B 2
#define FIRST_DATA 3

/* meta page body */
#define M_TXN 40
#define M_CATALOG 48
#define M_HW 56
#define M_BLOB_LEN 64
#define M_BLOB_HEAD 72
#define M_INLINE 80

/* free-list chain page */
#define F_NEXT 40
#define F_USED 48
#define F_DATA 56

/* ---- the store ----------------------------------------------------------- */

typedef struct {
    uint64_t txn, catalog, hw;
} Root;

typedef struct {
    uint64_t freed_by; /* the commit id that freed them */
    U64s pages;
} Pending;

struct GsTxn;

struct GsStore {
    GsqlIo *io;
    int own_io;
    GsqlFile *f;
    char *path, *dir;
    uint32_t ps;
    uint64_t cache_bytes;
    Cache cache;
    Root durable, tip;
    int durable_slot;    /* META_A or META_B: which page holds `durable` */
    uint64_t hw;         /* pages [0, hw) exist in the format */
    U64s free_pages;     /* reusable now */
    Pending *pending;
    size_t npending, cappending;
    U64s blob_pages;     /* the chain holding the tip's free list */
    U64s readers;        /* snapshot txn ids, one per open snapshot */
    struct GsTxn *writer;
    int failed;
    int unsynced;        /* pages written since the last flush */
    GsStats st;
};

typedef struct {
    BtPager base;
    struct GsTxn *t;
} CowPager;

struct GsTxn {
    BeTxn be;
    GsStore *db;
    CowPager pager;
    uint64_t id;      /* write: the commit id it will get. read: the snapshot's txn */
    Root snap;        /* read: what it sees */
    Set fresh;        /* write: pages allocated by this txn */
    U64s freed;       /* write: pages this txn frees (live until it is durable) */
    U64s zombies;     /* fresh pages freed in the current operation */
};

static int io_err(GsStore *db, int rc) {
    (void)db;
    return gs_io_status(rc);
}

/* ---- reading and writing pages ------------------------------------------- */

static int read_page(GsStore *db, uint64_t pgno, unsigned char *buf) {
    size_t got;
    int rc = gsql_io_read_at(db->f, pgno * db->ps, buf, db->ps, &got);
    if (rc) return io_err(db, rc);
    db->st.pages_read++;
    if (got != db->ps) return GS_CORRUPT;
    return gs_page_verify(buf, db->ps, pgno);
}

static int write_page(GsStore *db, uint64_t pgno, unsigned char *buf, uint64_t commit) {
    gs_page_seal(buf, db->ps, pgno, commit);
    int rc = gsql_io_write_at(db->f, pgno * db->ps, buf, db->ps);
    if (rc) return io_err(db, rc);
    db->st.pages_written++;
    db->st.bytes_written += db->ps;
    db->unsynced = 1;
    return GS_OK;
}

static int flush(GsStore *db) {
    int rc = gsql_io_sync(db->f);
    db->st.flushes++;
    if (rc) {
        /* §1.3: never retried; the database is unusable until reopened */
        db->failed = 1;
        return GS_IO;
    }
    db->unsynced = 0;
    return GS_OK;
}

static int get_cached(GsStore *db, uint64_t pgno, CPage **out) {
    CPage *p = cache_find(&db->cache, pgno, 0);
    if (p) {
        db->st.cache_hits++;
        lru_unlink(p);
        lru_front(&db->cache, p);
        *out = p;
        return GS_OK;
    }
    unsigned char *buf = malloc(db->ps);
    if (!buf) return GS_IO;
    int rc = read_page(db, pgno, buf);
    if (rc) { free(buf); return rc; }
    p = cache_insert(&db->cache, pgno, 0, buf);
    if (!p) { free(buf); return GS_IO; }
    *out = p;
    return GS_OK;
}

/* ---- the pager a transaction sees ---------------------------------------- */

static int pg_get(BtPager *b, uint64_t pgno, unsigned char **page) {
    struct GsTxn *t = ((CowPager *)b)->t;
    if (pgno < FIRST_DATA || pgno >= t->db->hw) return GS_CORRUPT;
    CPage *p;
    int rc = get_cached(t->db, pgno, &p);
    if (rc) return rc;
    *page = p->buf;
    return GS_OK;
}

static int take_page(struct GsTxn *t, uint64_t *pgno) {
    GsStore *db = t->db;
    uint64_t p = db->free_pages.n ? db->free_pages.v[--db->free_pages.n] : db->hw++;
    if (!set_add(&t->fresh, p)) return GS_IO;
    *pgno = p;
    return GS_OK;
}

static int pg_alloc(BtPager *b, uint64_t *pgno, unsigned char **page) {
    struct GsTxn *t = ((CowPager *)b)->t;
    GsStore *db = t->db;
    int rc = take_page(t, pgno);
    if (rc) return rc;
    CPage *old = cache_find(&db->cache, *pgno, 0);
    if (old) cache_remove(&db->cache, old); /* a stale copy from before it was freed */
    unsigned char *buf = calloc(1, db->ps);
    CPage *p = buf ? cache_insert(&db->cache, *pgno, 0, buf) : NULL;
    if (!p) { free(buf); return GS_IO; }
    p->dirty = 1;
    *page = buf;
    return GS_OK;
}

static int pg_write(BtPager *b, uint64_t pgno, uint64_t *out, unsigned char **page) {
    struct GsTxn *t = ((CowPager *)b)->t;
    GsStore *db = t->db;
    CPage *p;
    int rc;
    if (set_has(&t->fresh, pgno)) {
        /* already this txn's own copy: write it in place (re-read if spilled) */
        if ((rc = get_cached(db, pgno, &p))) return rc;
        p->dirty = 1;
        *out = pgno;
        *page = p->buf;
        return GS_OK;
    }
    if ((rc = get_cached(db, pgno, &p))) return rc;
    unsigned char *src = p->buf, *dst;
    uint64_t np;
    if ((rc = pg_alloc(b, &np, &dst))) return rc;
    /* the cache may not have moved `src`: pg_alloc only inserted */
    memcpy(dst, src, db->ps);
    if (!u64_push(&t->freed, pgno)) return GS_IO;
    *out = np;
    *page = dst;
    return GS_OK;
}

static int pg_free(BtPager *b, uint64_t pgno) {
    struct GsTxn *t = ((CowPager *)b)->t;
    if (set_has(&t->fresh, pgno)) {
        /* never visible to anyone: reusable, but not until this operation
         * ends, since the tree code may still hold a pointer into it */
        return u64_push(&t->zombies, pgno) ? GS_OK : GS_IO;
    }
    return u64_push(&t->freed, pgno) ? GS_OK : GS_IO;
}

static void evict(GsStore *db, struct GsTxn *w) {
    uint64_t limit = db->cache_bytes / db->ps;
    if (limit < 16) limit = 16;
    CPage *p = db->cache.lru.prev;
    while (db->cache.count > limit && p != &db->cache.lru) {
        CPage *prev = p->prev;
        if (!p->dirty) {
            cache_remove(&db->cache, p);
        } else if (w && set_has(&w->fresh, p->pgno)) {
            /* G5: a transaction bigger than the cache spills its pages to their
             * final places early. They are unreferenced by any meta, so this is
             * safe; if touched again they are read back and rewritten. */
            if (write_page(db, p->pgno, p->buf, w->id) == GS_OK) cache_remove(&db->cache, p);
            else break;
        }
        p = prev;
    }
}

static void after_op(BeTxn *be) {
    struct GsTxn *t = (struct GsTxn *)be;
    GsStore *db = t->db;
    for (size_t i = 0; i < t->zombies.n; i++) {
        uint64_t pgno = t->zombies.v[i];
        CPage *p = cache_find(&db->cache, pgno, 0);
        if (p) cache_remove(&db->cache, p);
        set_del(&t->fresh, pgno);
        u64_push(&db->free_pages, pgno);
    }
    t->zombies.n = 0;
    evict(db, t->be.write ? t : db->writer);
}

/* ---- the free list blob --------------------------------------------------- */

static int blob_build(GsStore *db, GsBuf *out) {
    out->n = 0;
    unsigned char b[8];
    put64(b, db->free_pages.n);
    if (!gsbuf_append(out, b, 8)) return GS_IO;
    for (size_t i = 0; i < db->free_pages.n; i++) { put64(b, db->free_pages.v[i]); gsbuf_append(out, b, 8); }
    put64(b, db->npending);
    gsbuf_append(out, b, 8);
    for (size_t i = 0; i < db->npending; i++) {
        put64(b, db->pending[i].freed_by);
        gsbuf_append(out, b, 8);
        put64(b, db->pending[i].pages.n);
        gsbuf_append(out, b, 8);
        for (size_t j = 0; j < db->pending[i].pages.n; j++) { put64(b, db->pending[i].pages.v[j]); gsbuf_append(out, b, 8); }
    }
    return GS_OK;
}

static void pending_clear(GsStore *db) {
    for (size_t i = 0; i < db->npending; i++) u64_free(&db->pending[i].pages);
    db->npending = 0;
}

static int pending_add(GsStore *db, uint64_t freed_by, U64s *pages) {
    if (db->npending == db->cappending) {
        size_t cap = db->cappending ? db->cappending * 2 : 8;
        Pending *v = realloc(db->pending, cap * sizeof *v);
        if (!v) return GS_IO;
        db->pending = v;
        db->cappending = cap;
    }
    db->pending[db->npending].freed_by = freed_by;
    db->pending[db->npending].pages = *pages;
    memset(pages, 0, sizeof *pages);
    db->npending++;
    return GS_OK;
}

static int blob_parse(GsStore *db, const unsigned char *p, size_t n) {
    size_t at = 0;
#define NEED(k) do { if (at + (k) > n) return GS_CORRUPT; } while (0)
    NEED(8);
    uint64_t nf = get64(p);
    at = 8;
    if (nf > n / 8) return GS_CORRUPT;
    NEED(nf * 8);
    db->free_pages.n = 0;
    for (uint64_t i = 0; i < nf; i++, at += 8) u64_push(&db->free_pages, get64(p + at));
    NEED(8);
    uint64_t np = get64(p + at);
    at += 8;
    pending_clear(db);
    for (uint64_t i = 0; i < np; i++) {
        NEED(16);
        uint64_t by = get64(p + at), cnt = get64(p + at + 8);
        at += 16;
        if (cnt > n / 8) return GS_CORRUPT;
        NEED(cnt * 8);
        U64s pages = {0};
        for (uint64_t j = 0; j < cnt; j++, at += 8) u64_push(&pages, get64(p + at));
        int rc = pending_add(db, by, &pages);
        if (rc) return rc;
    }
#undef NEED
    return at == n ? GS_OK : GS_CORRUPT;
}

static uint64_t oldest_reader(GsStore *db) {
    uint64_t m = UINT64_MAX;
    for (size_t i = 0; i < db->readers.n; i++)
        if (db->readers.v[i] < m) m = db->readers.v[i];
    return m;
}

/* Pages freed by commit F become reusable once F is durable and no snapshot
 * older than F is open (a snapshot at S < F can still reach them). */
static void release_pending(GsStore *db) {
    uint64_t oldest = oldest_reader(db);
    size_t keep = 0;
    for (size_t i = 0; i < db->npending; i++) {
        Pending *pe = &db->pending[i];
        if (pe->freed_by <= db->durable.txn && pe->freed_by <= oldest) {
            for (size_t j = 0; j < pe->pages.n; j++) u64_push(&db->free_pages, pe->pages.v[j]);
            u64_free(&pe->pages);
        } else {
            db->pending[keep++] = *pe;
        }
    }
    db->npending = keep;
}

/* ---- meta pages ----------------------------------------------------------- */

static uint32_t inline_cap(uint32_t ps) { return ps - GS_PAGE_TAG - M_INLINE; }
static uint32_t chain_cap(uint32_t ps) { return ps - GS_PAGE_TAG - F_DATA; }

/* Write the free list (chain pages from the end of the file if it does not fit
 * inline) and then the meta page for `r` into the slot not holding durable. */
static int write_meta(GsStore *db, const Root *r) {
    /* The chain naming the previous durable free list is freed by THIS meta,
     * and the list being written must say so -- recorded only in memory, it
     * was lost at the next reopen and those pages leaked (found by check). It
     * is reusable once this meta is durable: until then, a torn write of this
     * meta falls back to the previous one, which still needs it. */
    size_t pending_before = db->npending;
    if (db->blob_pages.n) {
        U64s old = {0};
        for (size_t i = 0; i < db->blob_pages.n; i++) u64_push(&old, db->blob_pages.v[i]);
        if (pending_add(db, r->txn, &old)) return GS_IO;
    }
    GsBuf blob = {0};
    int rc = blob_build(db, &blob);
    unsigned char *page = calloc(1, db->ps);
    if (!page || rc) { free(page); gsbuf_free(&blob); return GS_IO; }
    uint64_t head = 0;
    U64s chain = {0};
    if (blob.n > inline_cap(db->ps)) {
        size_t cap = chain_cap(db->ps), pages = (blob.n + cap - 1) / cap;
        uint64_t first = db->hw;
        unsigned char *cp = calloc(1, db->ps);
        for (size_t i = 0; i < pages && rc == GS_OK; i++) {
            memset(cp, 0, db->ps);
            cp[0] = GS_PT_FREELIST;
            size_t take = blob.n - i * cap < cap ? blob.n - i * cap : cap;
            memcpy(cp + F_DATA, blob.p + i * cap, take);
            put32(cp + F_USED, (uint32_t)take);
            put64(cp + F_NEXT, i + 1 < pages ? first + i + 1 : 0);
            rc = write_page(db, first + i, cp, r->txn);
            u64_push(&chain, first + i);
        }
        free(cp);
        head = first;
        db->hw = first + pages;
        /* the chain must be on disk before the meta that names it */
        if (rc == GS_OK) rc = flush(db);
    }
    if (rc == GS_OK) {
        page[0] = GS_PT_META;
        put64(page + M_TXN, r->txn);
        put64(page + M_CATALOG, r->catalog);
        put64(page + M_HW, db->hw);
        put64(page + M_BLOB_LEN, blob.n);
        put64(page + M_BLOB_HEAD, head);
        if (!head) memcpy(page + M_INLINE, blob.p, blob.n);
        int slot = db->durable_slot == META_A ? META_B : META_A;
        rc = write_page(db, (uint64_t)slot, page, r->txn);
        if (rc == GS_OK) rc = flush(db);
        if (rc == GS_OK) {
            u64_free(&db->blob_pages);
            db->blob_pages = chain;
            memset(&chain, 0, sizeof chain);
            db->durable_slot = slot;
            db->durable = *r;
            db->durable.hw = db->hw;
        }
    }
    if (rc) while (db->npending > pending_before) u64_free(&db->pending[--db->npending].pages);
    u64_free(&chain);
    free(page);
    gsbuf_free(&blob);
    return rc;
}

static int read_meta(GsStore *db, uint64_t slot, Root *r, uint64_t *blob_len, uint64_t *blob_head, unsigned char *page) {
    size_t got;
    int rc = gsql_io_read_at(db->f, slot * db->ps, page, db->ps, &got);
    if (rc) return io_err(db, rc);
    if (got != db->ps || gs_page_verify(page, db->ps, slot) != GS_OK || page[0] != GS_PT_META) return GS_CORRUPT;
    r->txn = get64(page + M_TXN);
    r->catalog = get64(page + M_CATALOG);
    r->hw = get64(page + M_HW);
    *blob_len = get64(page + M_BLOB_LEN);
    *blob_head = get64(page + M_BLOB_HEAD);
    if (r->hw < FIRST_DATA + 1 || r->catalog < FIRST_DATA || r->catalog >= r->hw) return GS_CORRUPT;
    if (!*blob_head && *blob_len > inline_cap(db->ps)) return GS_CORRUPT;
    return GS_OK;
}

/* ---- open, create, recover ------------------------------------------------ */

static int create_fresh(GsStore *db) {
    unsigned char *page = calloc(1, db->ps);
    if (!page) return GS_IO;
    GsFileHeader h = {GS_FORMAT_MAJOR, 0, db->ps, {0}, 0, 0};
    gs_identity_new(h.identity);
    gs_header_write(page, &h);
    int rc = gsql_io_truncate(db->f, 0);
    if (rc == 0) rc = gsql_io_write_at(db->f, 0, page, db->ps);
    rc = io_err(db, rc);
    /* the catalog: an empty leaf at page 3 */
    if (rc == GS_OK) {
        memset(page, 0, db->ps);
        page[0] = GS_PT_LEAF;
        put16(page + 40, 0);
        put16(page + 42, 56);
        put16(page + 44, (uint16_t)(db->ps - GS_PAGE_TAG));
        rc = write_page(db, FIRST_DATA, page, 1);
    }
    db->hw = FIRST_DATA + 1;
    db->durable_slot = META_B; /* so the first meta goes to META_A */
    Root r = {1, FIRST_DATA, db->hw};
    if (rc == GS_OK) rc = flush(db);
    if (rc == GS_OK) rc = write_meta(db, &r);
    if (rc == GS_OK && db->dir) rc = io_err(db, gsql_io_sync_dir(db->io, db->dir));
    if (rc == GS_OK) db->tip = db->durable;
    free(page);
    return rc;
}

static int recover(GsStore *db) {
    unsigned char *page = malloc(db->ps);
    if (!page) return GS_IO;
    Root a, b;
    uint64_t al, ah, bl, bh;
    int va = read_meta(db, META_A, &a, &al, &ah, page) == GS_OK;
    unsigned char *pageb = malloc(db->ps);
    int vb = read_meta(db, META_B, &b, &bl, &bh, pageb) == GS_OK;
    if (!va && !vb) { free(page); free(pageb); return GS_CORRUPT; }
    int use_a = va && (!vb || a.txn > b.txn);
    Root r = use_a ? a : b;
    uint64_t len = use_a ? al : bl, head = use_a ? ah : bh;
    unsigned char *src = use_a ? page : pageb;
    db->durable_slot = use_a ? META_A : META_B;
    db->hw = r.hw;
    GsBuf blob = {0};
    int rc = GS_OK;
    u64_free(&db->blob_pages);
    if (!head) {
        gsbuf_set(&blob, src + M_INLINE, (size_t)len);
    } else {
        uint64_t pg = head;
        unsigned char *cp = malloc(db->ps);
        size_t guard = (size_t)(len / chain_cap(db->ps)) + 2;
        while (rc == GS_OK && blob.n < len) {
            if (!pg || pg >= r.hw || guard-- == 0) { rc = GS_CORRUPT; break; }
            if ((rc = read_page(db, pg, cp))) break;
            if (cp[0] != GS_PT_FREELIST) { rc = GS_CORRUPT; break; }
            uint32_t used = get32(cp + F_USED);
            if (used > chain_cap(db->ps) || blob.n + used > len) { rc = GS_CORRUPT; break; }
            gsbuf_append(&blob, cp + F_DATA, used);
            u64_push(&db->blob_pages, pg);
            pg = get64(cp + F_NEXT);
        }
        free(cp);
    }
    if (rc == GS_OK) rc = blob_parse(db, blob.p, blob.n);
    gsbuf_free(&blob);
    free(page);
    free(pageb);
    if (rc) return rc;
    db->durable = db->tip = r;
    release_pending(db);
    return GS_OK;
}

static void store_free(GsStore *db) {
    cache_free(&db->cache);
    u64_free(&db->free_pages);
    pending_clear(db);
    free(db->pending);
    u64_free(&db->blob_pages);
    u64_free(&db->readers);
    if (db->own_io) gsql_io_free(db->io);
    free(db->path);
    free(db->dir);
    free(db);
}

static int cow_open(const char *path, const GsOptions *opt, GsStore **out) {
    GsStore *db = calloc(1, sizeof *db);
    if (!db) return GS_IO;
    db->io = opt && opt->io ? opt->io : NULL;
    if (!db->io) { db->io = gsql_io_real(); db->own_io = 1; }
    db->path = gs_path_with(path, "");
    db->dir = gs_dirname(path);
    db->ps = opt && opt->page_size ? opt->page_size : 4096;
    db->cache_bytes = opt && opt->cache_bytes ? opt->cache_bytes : 64u << 20;
    cache_init(&db->cache);
    int created = 0;
    int rc = gsql_io_open(db->io, path, 0, &db->f);
    if (rc == ENOENT && opt && opt->create) {
        rc = gsql_io_open(db->io, path, GSQL_IO_CREATE, &db->f);
        created = 1;
    }
    rc = rc == ENOENT ? GS_NOT_FOUND : io_err(db, rc);
    if (rc == GS_OK) rc = io_err(db, gsql_io_lock(db->f, 1));
    if (rc == GS_OK) {
        unsigned char hdr[GS_HEADER_FIXED];
        size_t got = 0;
        rc = io_err(db, gsql_io_read_at(db->f, 0, hdr, sizeof hdr, &got));
        GsFileHeader h;
        int hrc = rc ? rc : gs_header_read(hdr, got, &h);
        if (rc == GS_OK && hrc == GS_OK) {
            db->ps = h.page_size;
            rc = recover(db);
        } else {
            rc = rc ? rc : hrc;
        }
        /* A header or both metas missing in a file of at most four pages means
         * creation never finished: no commit is possible before the open that
         * creates the file returns, and the first commit grows the file. Start
         * again when asked to create; anything larger is damage, not reported
         * as an empty database. */
        if ((rc == GS_CORRUPT || rc == GS_FORMAT) && opt && opt->create) {
            uint64_t sz = 0;
            uint32_t ps = opt->page_size ? opt->page_size : 4096;
            gsql_io_size(db->f, &sz);
            if (created || sz <= (uint64_t)ps * (FIRST_DATA + 1)) {
                db->ps = ps;
                pending_clear(db);
                db->free_pages.n = 0;
                rc = create_fresh(db);
            }
        }
    }
    if (rc) {
        if (db->f) gsql_io_close(db->f);
        db->f = NULL;
        store_free(db);
        return rc;
    }
    *out = db;
    return GS_OK;
}

static int cow_sync(GsStore *db, uint64_t *durable);

static int cow_close(GsStore *db) {
    if (db->writer || db->readers.n) return GS_MISUSE;
    if (!db->failed) cow_sync(db, NULL); /* a clean close makes every commit durable */
    gsql_io_close(db->f);
    store_free(db);
    return GS_OK;
}

/* ---- transactions -------------------------------------------------------- */

static struct GsTxn *txn_new(GsStore *db, int write) {
    (void)write;
    struct GsTxn *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->db = db;
    t->pager.base.page_size = db->ps;
    t->pager.base.link_leaves = 0;
    t->pager.base.get = pg_get;
    t->pager.base.write = pg_write;
    t->pager.base.alloc = pg_alloc;
    t->pager.base.free = pg_free;
    t->pager.t = t;
    return t;
}

static int cow_begin_read(GsStore *db, struct GsTxn **out) {
    if (db->failed) return GS_FAILED;
    struct GsTxn *t = txn_new(db, 0);
    if (!t) return GS_IO;
    t->snap = db->durable; /* §1.3: only what is durable is visible */
    t->id = t->snap.txn;
    if (!u64_push(&db->readers, t->id)) { free(t); return GS_IO; }
    be_txn_init(&t->be, &t->pager.base, t->snap.catalog, 0);
    t->be.after_op = after_op;
    *out = t;
    return GS_OK;
}

static int cow_begin_write(GsStore *db, struct GsTxn **out) {
    if (db->failed) return GS_FAILED;
    if (db->writer) return GS_BUSY;
    release_pending(db);
    struct GsTxn *t = txn_new(db, 1);
    if (!t) return GS_IO;
    t->id = db->tip.txn + 1;
    be_txn_init(&t->be, &t->pager.base, db->tip.catalog, 1);
    t->be.after_op = after_op;
    db->writer = t;
    *out = t;
    return GS_OK;
}

static void txn_release(struct GsTxn *t) {
    be_txn_free(&t->be);
    set_clear(&t->fresh);
    u64_free(&t->freed);
    u64_free(&t->zombies);
    free(t);
}

static void cow_abort(struct GsTxn *t) {
    GsStore *db = t->db;
    if (!t->be.write) {
        for (size_t i = 0; i < db->readers.n; i++)
            if (db->readers.v[i] == t->id) { db->readers.v[i] = db->readers.v[--db->readers.n]; break; }
        txn_release(t);
        return;
    }
    /* every page this txn allocated goes back; the ones it freed stay live */
    after_op(&t->be);
    for (size_t i = 0; i < t->fresh.cap; i++) {
        uint64_t p = t->fresh.slot[i];
        if (!p || p == UINT64_MAX) continue;
        CPage *c = cache_find(&db->cache, p, 0);
        if (c) cache_remove(&db->cache, c);
        u64_push(&db->free_pages, p);
    }
    db->writer = NULL;
    txn_release(t);
}

static int cow_commit(struct GsTxn *t, int sync, uint64_t *commit_id) {
    GsStore *db = t->db;
    if (!t->be.write) return GS_MISUSE;
    if (db->failed) { cow_abort(t); return GS_FAILED; }
    if (t->be.failed) { int rc = t->be.failed; cow_abort(t); return rc; }
    int rc = be_flush(&t->be);
    after_op(&t->be);
    if (rc) { cow_abort(t); return rc; }
    /* every dirty page in the cache is this txn's (nothing else writes) */
    for (CPage *p = db->cache.lru.next; p != &db->cache.lru && rc == GS_OK; p = p->next)
        if (p->dirty) rc = write_page(db, p->pgno, p->buf, t->id);
    if (rc) {
        /* FULL: nothing is named by a meta yet, so the database is as it was */
        cow_abort(t);
        return rc;
    }
    Root r = {t->id, t->be.catalog, db->hw};
    size_t pending_before = db->npending;
    U64s freed = t->freed;
    memset(&t->freed, 0, sizeof t->freed);
    if (freed.n && pending_add(db, t->id, &freed)) { u64_free(&freed); cow_abort(t); return GS_IO; }
    u64_free(&freed);
    if (sync) {
        /* pages first, then the meta that names them: two flushes */
        rc = flush(db);
        if (rc == GS_OK) rc = write_meta(db, &r);
        if (rc) {
            /* not durable and not visible. FULL: roll back, the database stays
             * usable. IO: a flush failed, so it is not (§1.3). */
            while (db->npending > pending_before) u64_free(&db->pending[--db->npending].pages);
            cow_abort(t);
            if (rc != GS_FULL) db->failed = 1;
            return rc == GS_FULL ? GS_FULL : GS_IO;
        }
    }
    for (CPage *p = db->cache.lru.next; p != &db->cache.lru; p = p->next) p->dirty = 0;
    db->tip = r;
    db->writer = NULL;
    set_clear(&t->fresh); /* committed pages are ordinary pages now */
    txn_release(t);
    if (commit_id) *commit_id = r.txn;
    return GS_OK;
}

static int cow_sync(GsStore *db, uint64_t *durable) {
    if (db->failed) return GS_FAILED;
    int rc = GS_OK;
    if (db->tip.txn != db->durable.txn) {
        rc = flush(db);
        if (rc == GS_OK) rc = write_meta(db, &db->tip);
        if (rc && rc != GS_FULL) { db->failed = 1; rc = GS_IO; }
    }
    if (durable) *durable = db->durable.txn;
    return rc;
}

/* ---- the record and tree operations: the shared engine -------------------- */

#define BE(t) (&(t)->be)
static int cow_tree_create(struct GsTxn *t, GsBytes n) { return t->db->failed ? GS_FAILED : be_tree_create(BE(t), n); }
static int cow_tree_drop(struct GsTxn *t, GsBytes n) { return t->db->failed ? GS_FAILED : be_tree_drop(BE(t), n); }
static int cow_tree_list(struct GsTxn *t, int (*e)(void *, GsBytes), void *c) { return be_tree_list(BE(t), e, c); }
static int cow_tree_replace(struct GsTxn *t, GsBytes a, GsBytes b) { return t->db->failed ? GS_FAILED : be_tree_replace(BE(t), a, b); }
static int cow_get(struct GsTxn *t, GsBytes tr, GsBytes k, GsBytes *v) { return t->db->failed ? GS_FAILED : be_get(BE(t), tr, k, v); }
static int cow_put(struct GsTxn *t, GsBytes tr, GsBytes k, GsBytes v, unsigned f) { return t->db->failed ? GS_FAILED : be_put(BE(t), tr, k, v, f); }
static int cow_del(struct GsTxn *t, GsBytes tr, GsBytes k) { return t->db->failed ? GS_FAILED : be_del(BE(t), tr, k); }
static int cow_cursor_open(struct GsTxn *t, GsBytes tr, GsCursor **c) { return be_cursor_open(BE(t), tr, (BeCursor **)c); }
static int cow_seek(GsCursor *c, GsBytes k, GsSeek m, int *p) { return be_seek((BeCursor *)c, k, m, p); }
static int cow_first(GsCursor *c, int *p) { return be_first((BeCursor *)c, p); }
static int cow_last(GsCursor *c, int *p) { return be_last((BeCursor *)c, p); }
static int cow_next(GsCursor *c, int *p) { return be_next((BeCursor *)c, p); }
static int cow_prev(GsCursor *c, int *p) { return be_prev((BeCursor *)c, p); }
static int cow_key(GsCursor *c, GsBytes *k) { return be_key((BeCursor *)c, k); }
static int cow_value(GsCursor *c, GsBytes *v) { return be_value((BeCursor *)c, v); }
static int cow_cursor_put(GsCursor *c, GsBytes v) { return be_cursor_put((BeCursor *)c, v); }
static int cow_cursor_delete(GsCursor *c) { return be_cursor_delete((BeCursor *)c); }
static void cow_cursor_close(GsCursor *c) { be_cursor_close((BeCursor *)c); }

/* ---- check, backup, stats ------------------------------------------------- */

typedef struct {
    struct GsTxn *t;
    unsigned char *seen; /* per page: 0 unseen, 1 reachable, 2 free */
    uint64_t hw;
    void (*report)(void *, const char *);
    void *ctx;
    int bad;
    GsBuf names;
} CheckCtx;

static void say(CheckCtx *k, const char *fmt, uint64_t a, uint64_t b) {
    k->bad = 1;
    if (k->report) {
        char line[160];
        snprintf(line, sizeof line, fmt, (unsigned long long)a, (unsigned long long)b);
        k->report(k->ctx, line);
    }
}

static int mark_live(void *ctx, uint64_t pgno) {
    CheckCtx *k = ctx;
    if (pgno >= k->hw) { say(k, "page %llu is past the end (%llu)", pgno, k->hw); return 0; }
    if (k->seen[pgno]) say(k, "page %llu is reached twice (kind %llu)", pgno, k->seen[pgno]);
    k->seen[pgno] = 1;
    return 0;
}

static void mark_free(CheckCtx *k, uint64_t pgno, unsigned kind) {
    if (pgno < FIRST_DATA || pgno >= k->hw) { say(k, "free page %llu is out of range (%llu)", pgno, k->hw); return; }
    if (k->seen[pgno]) say(k, "page %llu is both live and free (kind %llu)", pgno, k->seen[pgno]);
    k->seen[pgno] = (unsigned char)kind;
}

static void forward_report(void *ctx, const char *line) {
    CheckCtx *k = ctx;
    k->bad = 1;
    if (k->report) k->report(k->ctx, line);
}

static int check_tree(void *ctx, GsBytes name, uint64_t root) {
    CheckCtx *k = ctx;
    (void)name;
    if (bt_check(&k->t->pager.base, root, forward_report, k, NULL) != GS_OK) k->bad = 1;
    bt_walk(&k->t->pager.base, root, mark_live, k);
    return 0;
}

static int cow_check(GsStore *db, void (*report)(void *, const char *), void *ctx) {
    if (db->writer) return GS_MISUSE;
    struct GsTxn *t = txn_new(db, 0);
    if (!t) return GS_IO;
    CheckCtx k = {t, calloc(db->hw + 1, 1), db->hw, report, ctx, 0, {0}};
    unsigned char *page = malloc(db->ps);
    Root r;
    uint64_t l, h;
    int va = read_meta(db, META_A, &r, &l, &h, page) == GS_OK, vb = read_meta(db, META_B, &r, &l, &h, page) == GS_OK;
    if (!va && !vb) say(&k, "neither meta page is valid (%llu%llu)", 0, 0);
    free(page);
    /* the tip: the catalog tree, then every tree it names */
    if (bt_check(&t->pager.base, db->tip.catalog, forward_report, &k, NULL) != GS_OK) k.bad = 1;
    bt_walk(&t->pager.base, db->tip.catalog, mark_live, &k);
    be_roots(&t->pager.base, db->tip.catalog, check_tree, &k);
    for (size_t i = 0; i < db->free_pages.n; i++) mark_free(&k, db->free_pages.v[i], 2);
    for (size_t i = 0; i < db->npending; i++)
        for (size_t j = 0; j < db->pending[i].pages.n; j++) {
            /* pending pages are still reachable from older snapshots, never from the tip */
            uint64_t p = db->pending[i].pages.v[j];
            if (p < k.hw && k.seen[p] == 1) say(&k, "page %llu is pending free yet reachable (%llu)", p, 0);
            else if (p < k.hw) k.seen[p] = 3;
        }
    for (size_t i = 0; i < db->blob_pages.n; i++) mark_free(&k, db->blob_pages.v[i], 4);
    for (uint64_t p = FIRST_DATA; p < k.hw; p++)
        if (!k.seen[p]) say(&k, "page %llu is neither reachable nor free (a leak, of %llu)", p, k.hw);
    free(k.seen);
    txn_release(t);
    return k.bad ? GS_CORRUPT : GS_OK;
}

typedef struct {
    struct GsTxn *t;
    GsqlFile *dst;
    unsigned char *seen;
    uint64_t hw;
    int rc;
} CopyCtx;

static int copy_page(void *ctx, uint64_t pgno) {
    CopyCtx *k = ctx;
    if (k->rc) return 1;
    unsigned char *p;
    int rc = pg_get(&k->t->pager.base, pgno, &p);
    if (rc == GS_OK) rc = gs_io_status(gsql_io_write_at(k->dst, pgno * k->t->db->ps, p, k->t->db->ps));
    if (rc == GS_OK && pgno < k->hw) k->seen[pgno] = 1;
    k->rc = rc;
    return rc != GS_OK;
}

static int copy_tree(void *ctx, GsBytes name, uint64_t root) {
    (void)name;
    CopyCtx *k = ctx;
    bt_walk(&k->t->pager.base, root, copy_page, k);
    return k->rc != GS_OK;
}

static int cow_backup(struct GsTxn *snap, const char *dest) {
    GsStore *db = snap->db;
    if (snap->be.write) return GS_MISUSE;
    char *tmp = gs_path_with(dest, ".tmp");
    char *ddir = gs_dirname(dest);
    gsql_io_remove(db->io, tmp);
    GsqlFile *dst = NULL;
    int rc = gs_io_status(gsql_io_open(db->io, tmp, GSQL_IO_CREATE | GSQL_IO_EXCLUSIVE, &dst));
    CopyCtx k = {snap, dst, calloc(snap->snap.hw + 1, 1), snap->snap.hw, GS_OK};
    unsigned char *page = calloc(1, db->ps);
    if (rc == GS_OK) {
        /* page 0: the same header, the same identity -- it is this database */
        size_t got;
        rc = gs_io_status(gsql_io_read_at(db->f, 0, page, db->ps, &got));
        if (rc == GS_OK) rc = gs_io_status(gsql_io_write_at(dst, 0, page, db->ps));
    }
    if (rc == GS_OK) {
        bt_walk(&snap->pager.base, snap->snap.catalog, copy_page, &k);
        if (k.rc == GS_OK) be_roots(&snap->pager.base, snap->snap.catalog, copy_tree, &k);
        rc = k.rc;
    }
    if (rc == GS_OK) {
        /* every page the snapshot does not reach is free in the copy */
        GsBuf blob = {0};
        uint64_t nfree = 0;
        unsigned char b[8];
        gsbuf_append(&blob, b, 8);
        for (uint64_t p = FIRST_DATA; p < k.hw; p++)
            if (!k.seen[p]) { put64(b, p); gsbuf_append(&blob, b, 8); nfree++; }
        put64(blob.p, nfree);
        put64(b, 0);
        gsbuf_append(&blob, b, 8);
        uint64_t head = 0, hw = k.hw;
        size_t cap = chain_cap(db->ps);
        if (blob.n > inline_cap(db->ps)) {
            size_t pages = (blob.n + cap - 1) / cap;
            head = hw;
            for (size_t i = 0; i < pages && rc == GS_OK; i++) {
                memset(page, 0, db->ps);
                page[0] = GS_PT_FREELIST;
                size_t take = blob.n - i * cap < cap ? blob.n - i * cap : cap;
                memcpy(page + F_DATA, blob.p + i * cap, take);
                put32(page + F_USED, (uint32_t)take);
                put64(page + F_NEXT, i + 1 < pages ? head + i + 1 : 0);
                gs_page_seal(page, db->ps, head + i, snap->snap.txn);
                rc = gs_io_status(gsql_io_write_at(dst, (head + i) * db->ps, page, db->ps));
            }
            hw += pages;
        }
        memset(page, 0, db->ps);
        page[0] = GS_PT_META;
        put64(page + M_TXN, snap->snap.txn);
        put64(page + M_CATALOG, snap->snap.catalog);
        put64(page + M_HW, hw);
        put64(page + M_BLOB_LEN, blob.n);
        put64(page + M_BLOB_HEAD, head);
        if (!head) memcpy(page + M_INLINE, blob.p, blob.n);
        gs_page_seal(page, db->ps, META_A, snap->snap.txn);
        if (rc == GS_OK) rc = gs_io_status(gsql_io_write_at(dst, (uint64_t)META_A * db->ps, page, db->ps));
        gsbuf_free(&blob);
    }
    /* flushed, then renamed into place, then the directory flushed: an
     * interrupted backup never leaves a file that looks complete (§1.1) */
    if (rc == GS_OK) rc = gs_io_status(gsql_io_sync(dst));
    if (dst) gsql_io_close(dst);
    if (rc == GS_OK) rc = gs_io_status(gsql_io_rename(db->io, tmp, dest));
    if (rc == GS_OK) rc = gs_io_status(gsql_io_sync_dir(db->io, ddir));
    if (rc) gsql_io_remove(db->io, tmp);
    free(k.seen);
    free(page);
    free(tmp);
    free(ddir);
    return rc;
}

static void cow_stats(GsStore *db, GsStats *out) {
    *out = db->st;
    uint64_t sz = 0;
    gsql_io_size(db->f, &sz);
    out->file_bytes = sz;
}

const GsEngine gs_engine_cow = {
    "cow",
    cow_open, cow_close,
    cow_begin_read, cow_begin_write, cow_commit, cow_sync, cow_abort,
    cow_tree_create, cow_tree_drop, cow_tree_list, cow_tree_replace,
    cow_get, cow_put, cow_del,
    cow_cursor_open, cow_seek, cow_first, cow_last, cow_next, cow_prev, cow_key, cow_value,
    cow_cursor_put, cow_cursor_delete, cow_cursor_close,
    cow_backup, cow_check, cow_stats,
};

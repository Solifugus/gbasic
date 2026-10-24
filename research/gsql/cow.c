/* §2.A: the copy-on-write B+tree (LMDB's and BoltDB's shape).
 *
 * A page is never overwritten while anything can see it. A commit writes every
 * changed page -- and every ancestor up to the root -- to a free location,
 * flushes, then writes a META page naming the new catalog root, and flushes
 * again. There are two meta pages, written alternately; recovery takes the
 * valid one with the higher transaction id, so it needs no log.
 *
 * File layout: page 0 the header (§3.1.1); pages 1 and 2 the two meta slots;
 * everything else tree, overflow and free-tree pages.
 *
 * As built (recorded in the design):
 *   - the meta slots are whole PAGES 1 and 2, not slots inside page 0's root
 *     area: at a 512-byte page, page 0 is a single sector and two slots in it
 *     could not be written independently (rule 3);
 *   - the FREE TREE is a B+tree keyed by the commit id that freed each page,
 *     LMDB's shape and the design's (§3.1.4). A commit adds ONE record, its
 *     freed pages. A write transaction takes reusable records out at its
 *     start; pages it took and did not use go back under key 0. The first
 *     version kept the whole free list as a blob rewritten at every durable
 *     commit (BoltDB's shape), written to pages from the end of the file: its
 *     cost grew with the list, the list grew with its own old copies, and a
 *     20,000-commit benchmark wrote 419 GB before it was stopped;
 *   - a commit with sync = false writes its pages but no meta; the next sync,
 *     or synced commit, writes one meta covering every commit since (group
 *     commit, §5.4). A page freed by commit F is reusable only once F is
 *     durable AND no open snapshot predates F -- the first condition keeps a
 *     fall-back to the older meta safe.
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
#define M_FREE 64

/* a write transaction takes out at least this many reusable pages at its start */
#define TAKE_PAGES 64

/* ---- the store ----------------------------------------------------------- */

typedef struct {
    uint64_t txn, catalog, hw, free;
} Root;

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
    uint64_t file_pages; /* pages the file physically holds: grown in zeroed chunks */
    U64s readers;        /* snapshot txn ids, one per open snapshot */
    struct GsTxn *writer;
    int failed;
    int unsynced;
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
    U64s avail;       /* write: reusable pages taken out of the free tree */
    uint64_t avail_used; /* write: pages handed out of `avail`, ever */
    uint64_t free;    /* write: the free tree's root in this txn */
    int no_refill;    /* write: the free tree is being written; never take from it */
    int end_only;     /* write: allocate from the end of the file (the settle loop's later rounds) */
    uint64_t limit;   /* write: records freed by commits up to this are reusable */
    int exhausted;    /* write: no reusable record is left to take */
};

static int take_free(struct GsTxn *t);

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

/* Grow the file in zero-filled chunks ahead of use, so a commit writes over
 * bytes that exist and its flush does not also commit a change of file size
 * (250 us against 1 ms on this machine). A zero page is never read: only pages
 * below a meta's high-water mark are, and every one of those was written. */
static int grow(GsStore *db, uint64_t need) {
    if (need <= db->file_pages) return GS_OK;
    uint64_t target = db->file_pages + (db->file_pages / 8 > 64 ? db->file_pages / 8 : 64);
    if (target < need) target = need;
    static unsigned char zero[65536];
    uint64_t from = db->file_pages * db->ps, to = target * db->ps;
    for (uint64_t at = from; at < to;) {
        size_t n = to - at < sizeof zero ? (size_t)(to - at) : sizeof zero;
        int rc = gsql_io_write_at(db->f, at, zero, n);
        if (rc) return gs_io_status(rc);
        db->st.bytes_written += n;
        at += n;
    }
    db->file_pages = target;
    return GS_OK;
}

static int write_page(GsStore *db, uint64_t pgno, unsigned char *buf, uint64_t commit) {
    if (pgno >= db->file_pages) {
        int g = grow(db, pgno + 1);
        if (g) return g;
    }
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
    /* reusable pages this txn took out of the free tree, else the end of the
     * file. While the free tree itself is being written it is never REFILLED
     * from -- a nested take would edit the tree mid-edit -- but pages already
     * taken out of it are used. The first version used the end of the file
     * for every free-tree write, so each commit grew the file by the tree's
     * own path copy and handed as many pages back to a list that only grew:
     * 9.4x the data after 40,000 updates of 20,000 records, synced or not, and
     * past the 20 GB cap on a million. The commit's settle loop rewrites key 0
     * until `avail` stops moving, so a page consumed here leaves no list. */
    if (!t->no_refill && !t->avail.n && !t->exhausted) {
        /* Refill now, as LMDB does: take the next reusable records out of the
         * free tree. Taking only at the start left a transaction that needed
         * more growing the file while free pages piled up (space 5.6x on YCSB
         * A). Nested inside another tree's operation, which is safe: nothing
         * is evicted mid-operation, a page taken here was freed by an earlier
         * commit so the outer operation cannot hold it, and the nested change
         * allocates from the end of the file, so it cannot recurse. */
        int rc = take_free(t);
        if (rc) return rc;
    }
    uint64_t p = (!t->end_only && t->avail.n) ? (t->avail_used++, t->avail.v[--t->avail.n]) : db->hw++;
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
        u64_push(&t->avail, pgno);
    }
    t->zombies.n = 0;
    evict(db, t->be.write ? t : db->writer);
}


/* ---- the free tree ---------------------------------------------------------- */

static uint64_t oldest_reader(GsStore *db) {
    uint64_t m = UINT64_MAX;
    for (size_t i = 0; i < db->readers.n; i++)
        if (db->readers.v[i] < m) m = db->readers.v[i];
    return m;
}

static int encode_pages(const U64s *pages, GsBuf *out) {
    out->n = 0;
    if (!gsbuf_reserve(out, pages->n * 8 + 1)) return GS_IO;
    for (size_t i = 0; i < pages->n; i++) put64(out->p + 8 * i, pages->v[i]);
    out->n = pages->n * 8;
    return GS_OK;
}

/* Page numbers per record: few enough that a record is always stored inline.
 * An overflowing value is rewritten by freeing its chain and writing a new
 * one, and in the commit's settle loop those freed pages become part of the
 * list being recorded -- so the record grew every round and never settled
 * (found at the 51st commit of a debugging run). Inline, a rewrite frees
 * nothing. */
static size_t chunk_pages(uint32_t ps) {
    size_t cell = (ps - GS_PAGE_TAG - 56) / 4 - 2;   /* the tree's largest inline cell */
    return (cell - (1 + 1 + 1 + 16)) / 8;            /* less the cell's own overhead */
}

static void fkey(unsigned char k[16], uint64_t id, uint64_t chunk) { put64be(k, id); put64be(k + 8, chunk); }

/* Replace every record of commit `id` with `pages`, in inline chunks: each
 * chunk is replaced in place and only surplus ones are deleted, so a rewrite
 * that changes little changes no structure. (Deleting the lot and inserting
 * again merged and split nodes every round of the settle loop, and the nodes
 * it freed grew the list: it never settled.) */
static int free_put(struct GsTxn *t, uint64_t id, const U64s *pages) {
    unsigned char k[16];
    int rc = GS_OK;
    size_t per = chunk_pages(t->db->ps), chunks = (pages->n + per - 1) / per;
    GsBuf v = {0};
    for (size_t at = 0, chunk = 0; rc == GS_OK && at < pages->n; at += per, chunk++) {
        U64s part = {pages->v + at, pages->n - at < per ? pages->n - at : per, 0};
        rc = encode_pages(&part, &v);
        fkey(k, id, chunk);
        if (rc == GS_OK) rc = bt_put(&t->pager.base, &t->free, gsb(k, 16), gsb(v.p, v.n), 0);
    }
    gsbuf_free(&v);
    for (uint64_t chunk = chunks; rc == GS_OK; chunk++) {
        fkey(k, id, chunk);
        rc = bt_del(&t->pager.base, &t->free, gsb(k, 16));
        if (rc == GS_NOT_FOUND) { rc = GS_OK; break; }
    }
    return rc;
}

/* At a write transaction's start: take reusable records out of the free tree
 * -- freed by a commit that is durable and older than every open snapshot, or
 * filed under key 0 as reusable already -- until it holds TAKE_PAGES. They
 * leave the tree in THIS transaction, so if it aborts they are still there. */
static int take_free_inner(struct GsTxn *t);

/* No refill while the free tree is being taken from: otherwise deleting the
 * records just loaded allocates, finds the list empty, and a nested take
 * loads the same records a second time. */
static int take_free(struct GsTxn *t) {
    int saved = t->no_refill;
    t->no_refill = 1;
    int rc = take_free_inner(t);
    t->no_refill = saved;
    return rc;
}

static int take_free_inner(struct GsTxn *t) {
    uint64_t limit = t->limit;
    BtCursor c;
    bt_cursor_init(&c, &t->pager.base, t->free);
    U64s keys = {0};
    int rc;
    for (rc = bt_first(&c); rc == GS_OK && c.positioned; rc = bt_next(&c)) {
        if (c.key.n != 16) { rc = GS_CORRUPT; break; }
        uint64_t f = get64be(c.key.p);
        if (f > limit) break;
        /* ALL of key 0, always: the commit rewrites key 0 from chunk 0 up,
         * so a chunk left behind would be overwritten with its pages in no
         * list at all (found as leaks by check) */
        if (f != 0 && t->avail.n >= TAKE_PAGES) break;
        if ((rc = bt_cursor_value(&c))) break;
        if (c.value.n % 8) { rc = GS_CORRUPT; break; }
        for (size_t i = 0; i < c.value.n; i += 8) u64_push(&t->avail, get64(c.value.p + i));
        u64_push(&keys, f);
        u64_push(&keys, get64be(c.key.p + 8));
    }
    if (rc == GS_OK && !c.positioned) t->exhausted = 1;
    if (rc == GS_OK && c.positioned && c.key.n == 16 && get64be(c.key.p) > limit) t->exhausted = 1;
    bt_cursor_free(&c);
    for (size_t i = 0; rc == GS_OK && i < keys.n; i += 2) {
        unsigned char k[16];
        fkey(k, keys.v[i], keys.v[i + 1]);
        rc = bt_del(&t->pager.base, &t->free, gsb(k, 16));
    }
    u64_free(&keys);
    return rc;
}

/* At commit: record what this transaction freed under its own id, and put
 * back what it took out and did not use, under key 0. Writing the tree frees
 * and allocates pages of its own, so repeat until nothing moves (see the
 * loop for why only the first round takes from `avail`). */
static int record_free(struct GsTxn *t) {
    t->no_refill = 1;
    int rc = GS_OK;
    for (int round = 0; rc == GS_OK && round < 32; round++) {
        /* Round 0 may take pages from `avail`, which is nearly all the reuse
         * there is (the free tree's path copy). Later rounds allocate from the
         * end, so `avail` can then only GROW, and the loop must stop. Letting
         * every round consume it does not terminate, and that was measured:
         * key 0 at 28 pages needs a second chunk, its write takes two pages,
         * at 26 the surplus chunk is deleted, the merge frees two, and it is
         * 28 again -- LMDB's freelist oscillation, which the store tier met
         * as a commit answering CORRUPT after 32 rounds. */
        t->end_only = round > 0;
        size_t nf = t->freed.n, na = t->avail.n;
        uint64_t used = t->avail_used;
        rc = free_put(t, t->id, &t->freed);
        if (rc == GS_OK) rc = free_put(t, 0, &t->avail);
        /* a fresh page the tree freed in passing is reusable: into avail */
        for (size_t i = 0; i < t->zombies.n; i++) {
            uint64_t pgno = t->zombies.v[i];
            CPage *p = cache_find(&t->db->cache, pgno, 0);
            if (p) cache_remove(&t->db->cache, p);
            set_del(&t->fresh, pgno);
            u64_push(&t->avail, pgno);
        }
        t->zombies.n = 0;
        /* Settled only when nothing MOVED, not when the lengths match: a round
         * can take page X out of `avail` for the tree's own write while a page
         * it freed in passing comes back, leaving the length the same and key
         * 0 listing X -- now live -- as free (found by the store tier and the
         * crash gate, the first time the free tree could use `avail`). */
        if (t->freed.n == nf && t->avail.n == na && t->avail_used == used) { t->no_refill = 0; t->end_only = 0; return rc; }
    }
    t->no_refill = 0;
    t->end_only = 0;
    return rc ? rc : GS_CORRUPT; /* did not settle: never seen, and not hidden */
}

/* ---- meta pages ----------------------------------------------------------- */

static int write_meta(GsStore *db, const Root *r) {
    unsigned char *page = calloc(1, db->ps);
    if (!page) return GS_IO;
    page[0] = GS_PT_META;
    put64(page + M_TXN, r->txn);
    put64(page + M_CATALOG, r->catalog);
    put64(page + M_HW, r->hw);
    put64(page + M_FREE, r->free);
    int slot = db->durable_slot == META_A ? META_B : META_A;
    int rc = write_page(db, (uint64_t)slot, page, r->txn);
    if (rc == GS_OK) rc = flush(db);
    if (rc == GS_OK) {
        db->durable_slot = slot;
        db->durable = *r;
    }
    free(page);
    return rc;
}

static int read_meta(GsStore *db, uint64_t slot, Root *r, unsigned char *page) {
    size_t got;
    int rc = gsql_io_read_at(db->f, slot * db->ps, page, db->ps, &got);
    if (rc) return io_err(db, rc);
    if (got != db->ps || gs_page_verify(page, db->ps, slot) != GS_OK || page[0] != GS_PT_META) return GS_CORRUPT;
    r->txn = get64(page + M_TXN);
    r->catalog = get64(page + M_CATALOG);
    r->hw = get64(page + M_HW);
    r->free = get64(page + M_FREE);
    if (r->hw < FIRST_DATA + 2 || r->catalog < FIRST_DATA || r->catalog >= r->hw || r->free < FIRST_DATA || r->free >= r->hw)
        return GS_CORRUPT;
    return GS_OK;
}

/* ---- open, create, recover ------------------------------------------------ */

static void empty_leaf(unsigned char *page, uint32_t ps) {
    memset(page, 0, ps);
    page[0] = GS_PT_LEAF;
    put16(page + 42, 56);
    put16(page + 44, (uint16_t)(ps - GS_PAGE_TAG));
}

static int create_fresh(GsStore *db) {
    unsigned char *page = calloc(1, db->ps);
    if (!page) return GS_IO;
    GsFileHeader h = {GS_FORMAT_MAJOR, 0, db->ps, {0}, 0, 0};
    gs_identity_new(db->io, h.identity);
    gs_header_write(page, &h);
    int rc = gsql_io_truncate(db->f, 0);
    if (rc == 0) rc = gsql_io_write_at(db->f, 0, page, db->ps);
    rc = io_err(db, rc);
    /* the header holds page 0: growth starts after it, or the first chunk of
     * zeros would be written over it */
    db->file_pages = 1;
    /* the catalog at page 3 and the free tree at page 4, both empty leaves */
    if (rc == GS_OK) { empty_leaf(page, db->ps); rc = write_page(db, FIRST_DATA, page, 1); }
    if (rc == GS_OK) { empty_leaf(page, db->ps); rc = write_page(db, FIRST_DATA + 1, page, 1); }
    db->hw = FIRST_DATA + 2;
    db->durable_slot = META_B; /* so the first meta goes to META_A */
    Root r = {1, FIRST_DATA, db->hw, FIRST_DATA + 1};
    if (rc == GS_OK) rc = flush(db);
    if (rc == GS_OK) rc = write_meta(db, &r);
    if (rc == GS_OK && db->dir) rc = io_err(db, gsql_io_sync_dir(db->io, db->dir));
    if (rc == GS_OK) db->tip = db->durable;
    free(page);
    return rc;
}

/* Recovery reads two pages: no log, no free list to load. */
static int recover(GsStore *db) {
    unsigned char *page = malloc(db->ps);
    if (!page) return GS_IO;
    Root a, b;
    int va = read_meta(db, META_A, &a, page) == GS_OK;
    int vb = read_meta(db, META_B, &b, page) == GS_OK;
    free(page);
    if (!va && !vb) return GS_CORRUPT;
    int use_a = va && (!vb || a.txn > b.txn);
    Root r = use_a ? a : b;
    db->durable_slot = use_a ? META_A : META_B;
    db->hw = r.hw;
    db->durable = db->tip = r;
    return GS_OK;
}

static void store_free(GsStore *db) {
    cache_free(&db->cache);
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
    int hdr_ok = 0;
    int rc = gsql_io_open(db->io, path, 0, &db->f);
    if (rc == ENOENT && opt && opt->create) rc = gsql_io_open(db->io, path, GSQL_IO_CREATE, &db->f);
    rc = rc == ENOENT ? GS_NOT_FOUND : io_err(db, rc);
    if (rc == GS_OK) rc = io_err(db, gsql_io_lock(db->f, 1));
    if (rc == GS_OK) {
        unsigned char hdr[GS_HEADER_FIXED];
        size_t got = 0;
        rc = io_err(db, gsql_io_read_at(db->f, 0, hdr, sizeof hdr, &got));
        GsFileHeader h;
        int hrc = rc ? rc : gs_header_read(hdr, got, &h);
        hdr_ok = rc == GS_OK && hrc == GS_OK;
        if (hdr_ok) {
            db->ps = h.page_size;
            rc = recover(db);
        } else {
            rc = rc ? rc : hrc;
        }
        /* Creation never finished when no meta names a commit: creation writes
         * meta 1 and every commit a higher one, so with no valid meta above 1
         * nothing was ever committed. (This was a file-size test, which growth
         * in chunks broke.) Asked with the caller's page size, since an
         * unfinished header may not say one. */
        if ((rc == GS_CORRUPT || rc == GS_FORMAT) && opt && opt->create) {
            uint32_t ps = opt->page_size ? opt->page_size : 4096;
            uint32_t keep = db->ps;
            db->ps = hdr_ok ? db->ps : ps;
            unsigned char *pg = malloc(db->ps);
            Root r;
            int committed = (read_meta(db, META_A, &r, pg) == GS_OK && r.txn > 1) ||
                            (read_meta(db, META_B, &r, pg) == GS_OK && r.txn > 1);
            free(pg);
            db->ps = keep;
            if (!committed) {
                db->ps = ps;
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
    uint64_t sz = 0;
    gsql_io_size(db->f, &sz);
    db->file_pages = sz / db->ps;
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

static struct GsTxn *txn_new(GsStore *db) {
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

static void txn_release(struct GsTxn *t) {
    be_txn_free(&t->be);
    set_clear(&t->fresh);
    u64_free(&t->freed);
    u64_free(&t->zombies);
    u64_free(&t->avail);
    free(t);
}

static int cow_begin_read(GsStore *db, struct GsTxn **out) {
    if (db->failed) return GS_FAILED;
    struct GsTxn *t = txn_new(db);
    if (!t) return GS_IO;
    t->snap = db->durable; /* §1.3: only what is durable is visible */
    t->id = t->snap.txn;
    if (!u64_push(&db->readers, t->id)) { free(t); return GS_IO; }
    be_txn_init(&t->be, &t->pager.base, t->snap.catalog, 0);
    t->be.after_op = after_op;
    *out = t;
    return GS_OK;
}

static void cow_abort(struct GsTxn *t);

static int cow_begin_write(GsStore *db, struct GsTxn **out) {
    if (db->failed) return GS_FAILED;
    if (db->writer) return GS_BUSY;
    struct GsTxn *t = txn_new(db);
    if (!t) return GS_IO;
    t->id = db->tip.txn + 1;
    t->free = db->tip.free;
    t->limit = db->durable.txn;
    uint64_t o = oldest_reader(db);
    if (o < t->limit) t->limit = o;
    be_txn_init(&t->be, &t->pager.base, db->tip.catalog, 1);
    t->be.after_op = after_op;
    db->writer = t;
    int rc = take_free(t);
    if (rc) { cow_abort(t); return rc; }
    *out = t;
    return GS_OK;
}

static void cow_abort(struct GsTxn *t) {
    GsStore *db = t->db;
    if (!t->be.write) {
        for (size_t i = 0; i < db->readers.n; i++)
            if (db->readers.v[i] == t->id) { db->readers.v[i] = db->readers.v[--db->readers.n]; break; }
        txn_release(t);
        return;
    }
    /* Everything this txn allocated is unreferenced: the pages it took from
     * the free tree are still in the tree as the tip has it, and the pages
     * past the tip's end go with the end. */
    for (size_t i = 0; i < t->fresh.cap; i++) {
        uint64_t p = t->fresh.slot[i];
        if (!p || p == UINT64_MAX) continue;
        CPage *c = cache_find(&db->cache, p, 0);
        if (c) cache_remove(&db->cache, c);
    }
    for (size_t i = 0; i < t->zombies.n; i++) {
        CPage *c = cache_find(&db->cache, t->zombies.v[i], 0);
        if (c) cache_remove(&db->cache, c);
    }
    db->hw = db->tip.hw;
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
    if (rc == GS_OK) rc = record_free(t);
    if (rc) { cow_abort(t); return rc; }
    /* every dirty page in the cache is this txn's (nothing else writes) */
    for (CPage *p = db->cache.lru.next; p != &db->cache.lru && rc == GS_OK; p = p->next)
        if (p->dirty) rc = write_page(db, p->pgno, p->buf, t->id);
    if (rc) {
        /* FULL: nothing is named by a meta yet, so the database is as it was */
        cow_abort(t);
        return rc;
    }
    Root r = {t->id, t->be.catalog, db->hw, t->free};
    if (sync) {
        /* pages first, then the meta that names them: two flushes */
        rc = flush(db);
        if (rc == GS_OK) rc = write_meta(db, &r);
        if (rc) {
            /* not durable and not visible. FULL: roll back, the database stays
             * usable. IO: a flush failed, so it is not (§1.3). */
            cow_abort(t);
            if (rc != GS_FULL) db->failed = 1;
            return rc == GS_FULL ? GS_FULL : GS_IO;
        }
    }
    for (CPage *p = db->cache.lru.next; p != &db->cache.lru; p = p->next) p->dirty = 0;
    db->tip = r;
    db->writer = NULL;
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
    unsigned char *seen; /* per page: 0 unseen, 1 reachable, 2 listed free */
    uint64_t hw;
    void (*report)(void *, const char *);
    void *ctx;
    int bad;
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

/* Every page below the end is exactly one of: reachable from the catalog, a
 * tree or the free tree; or listed in the free tree. */
static int cow_check(GsStore *db, void (*report)(void *, const char *), void *ctx) {
    if (db->writer) return GS_MISUSE;
    struct GsTxn *t = txn_new(db);
    if (!t) return GS_IO;
    CheckCtx k = {t, calloc(db->hw + 1, 1), db->hw, report, ctx, 0};
    unsigned char *page = malloc(db->ps);
    Root r;
    int va = read_meta(db, META_A, &r, page) == GS_OK, vb = read_meta(db, META_B, &r, page) == GS_OK;
    if (!va && !vb) say(&k, "neither meta page is valid (%llu%llu)", 0, 0);
    free(page);
    check_tree(&k, gss("catalog"), db->tip.catalog);
    be_roots(&t->pager.base, db->tip.catalog, check_tree, &k);
    check_tree(&k, gss("free"), db->tip.free);
    BtCursor c;
    bt_cursor_init(&c, &t->pager.base, db->tip.free);
    int rc;
    for (rc = bt_first(&c); rc == GS_OK && c.positioned; rc = bt_next(&c)) {
        if (bt_cursor_value(&c) || c.value.n % 8) { say(&k, "a free-tree record is unreadable (%llu%llu)", 0, 0); break; }
        for (size_t i = 0; i < c.value.n; i += 8) {
            uint64_t p = get64(c.value.p + i);
            if (p < FIRST_DATA || p >= k.hw) say(&k, "free page %llu is out of range (%llu)", p, k.hw);
            else if (k.seen[p]) say(&k, "page %llu is listed free and is also %llu", p, k.seen[p]);
            else k.seen[p] = 2;
        }
    }
    bt_cursor_free(&c);
    for (uint64_t p = FIRST_DATA; p < k.hw; p++)
        if (!k.seen[p]) say(&k, "page %llu is neither reachable nor free (a leak, of %llu)", p, k.hw);
    free(k.seen);
    txn_release(t);
    return k.bad ? GS_CORRUPT : GS_OK;
}

/* A pager over the backup's own pages: what building its free tree needs. */
typedef struct {
    BtPager base;
    unsigned char **pages;
    uint64_t first, n;
} DestPager;

static int dp_get(BtPager *b, uint64_t pgno, unsigned char **page) {
    DestPager *d = (DestPager *)b;
    if (pgno < d->first || pgno >= d->first + d->n) return GS_CORRUPT;
    *page = d->pages[pgno - d->first];
    return GS_OK;
}
static int dp_alloc(BtPager *b, uint64_t *pgno, unsigned char **page) {
    DestPager *d = (DestPager *)b;
    unsigned char **v = realloc(d->pages, (d->n + 1) * sizeof *v);
    if (!v) return GS_IO;
    d->pages = v;
    d->pages[d->n] = calloc(1, b->page_size);
    *pgno = d->first + d->n;
    *page = d->pages[d->n++];
    return GS_OK;
}
static int dp_write(BtPager *b, uint64_t pgno, uint64_t *out, unsigned char **page) { *out = pgno; return dp_get(b, pgno, page); }
static int dp_free(BtPager *b, uint64_t pgno) { (void)b; (void)pgno; return GS_OK; }

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

/* The pages the snapshot reaches, at their own numbers, and a new free tree
 * listing every other page below its end (one record, key 0). */
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
        size_t got;
        rc = gs_io_status(gsql_io_read_at(db->f, 0, page, db->ps, &got));
        if (rc == GS_OK) rc = gs_io_status(gsql_io_write_at(dst, 0, page, db->ps));
    }
    if (rc == GS_OK) {
        bt_walk(&snap->pager.base, snap->snap.catalog, copy_page, &k);
        if (k.rc == GS_OK) be_roots(&snap->pager.base, snap->snap.catalog, copy_tree, &k);
        rc = k.rc;
    }
    DestPager dp = {{db->ps, 0, dp_get, dp_write, dp_alloc, dp_free}, NULL, k.hw, 0};
    uint64_t froot = 0;
    if (rc == GS_OK) {
        U64s spare = {0};
        for (uint64_t p = FIRST_DATA; p < k.hw; p++) if (!k.seen[p]) u64_push(&spare, p);
        rc = bt_create(&dp.base, &froot);
        size_t per = chunk_pages(db->ps);
        GsBuf v = {0};
        for (size_t at = 0, chunk = 0; rc == GS_OK && at < spare.n; at += per, chunk++) {
            U64s part = {spare.v + at, spare.n - at < per ? spare.n - at : per, 0};
            encode_pages(&part, &v);
            unsigned char key[16];
            fkey(key, 0, chunk);
            rc = bt_put(&dp.base, &froot, gsb(key, 16), gsb(v.p, v.n), 0);
        }
        gsbuf_free(&v);
        u64_free(&spare);
        for (uint64_t i = 0; rc == GS_OK && i < dp.n; i++) {
            gs_page_seal(dp.pages[i], db->ps, dp.first + i, snap->snap.txn);
            rc = gs_io_status(gsql_io_write_at(dst, (dp.first + i) * db->ps, dp.pages[i], db->ps));
        }
    }
    if (rc == GS_OK) {
        memset(page, 0, db->ps);
        page[0] = GS_PT_META;
        put64(page + M_TXN, snap->snap.txn);
        put64(page + M_CATALOG, snap->snap.catalog);
        put64(page + M_HW, k.hw + dp.n);
        put64(page + M_FREE, froot);
        gs_page_seal(page, db->ps, META_A, snap->snap.txn);
        rc = gs_io_status(gsql_io_write_at(dst, (uint64_t)META_A * db->ps, page, db->ps));
    }
    for (uint64_t i = 0; i < dp.n; i++) free(dp.pages[i]);
    free(dp.pages);
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

/* §2.B: the B+tree with a write-ahead log (SQLite's WAL mode).
 *
 * Pages are updated in place, never before the change is in the log. A commit
 * appends full page images to the log -- each frame checksummed in a chain
 * seeded by the log's salt, the last one marked as the commit -- and flushes
 * ONCE. A checkpoint later copies log pages back into the main file. Readers
 * see, per page, the newest frame before their snapshot's end, otherwise the
 * main file.
 *
 * Files: `path` (main) and `path-wal` (the log).
 *   main: page 0 the header (§3.1.1), page 1 the STATE page (catalog root,
 *         free-list head, page count, last commit id) -- an ordinary page,
 *         updated through the log like any other -- then everything else.
 *   log:  a 512-byte header (magic, page size, salts, epoch, checksum: one
 *         sector, so its write is atomic by rule 3), then frames of a 40-byte
 *         frame header and a page image.
 *
 * As built (recorded in the design):
 *   - a checkpoint copies only DURABLE frames, and none past the oldest open
 *     reader: copying a frame of a commit not yet durable would put a page in
 *     the main file that a crash could leave without its log;
 *   - the log is reset (new salt, new epoch, truncated) only when every frame
 *     is in the main file AND no reader sees less than the whole log, since a
 *     reader from before a reset reads the main file alone;
 *   - free pages are a trunk list in the file, updated in place through the
 *     log, and reusable at once: an older reader still sees the old version
 *     of a reused page in the log. What a long reader costs here is the log
 *     growing, not the file (§2.B, against §2.A's pinned pages);
 *   - pages a transaction spills (G5) go to the log as frames without a commit
 *     marker, and are read back into the transaction's own dirty set.
 */

#include "btengine.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define STATE_PG 1
#define FIRST_DATA 2
/* state page body */
#define S_CATALOG 40
#define S_FREE_HEAD 48
#define S_FREE_COUNT 56
#define S_PAGES 64
#define S_COMMIT 72
/* free-list trunk page */
#define T_NEXT 40
#define T_COUNT 48
#define T_ENTRIES 56

#define LOG_HDR 512
#define FH 40
static const unsigned char LOG_MAGIC[8] = {'G', 'S', 'Q', 'L', 'W', 'A', 'L', 1};

/* ---- the frame index: page -> its frames, ascending ---------------------- */

typedef struct IdxEnt {
    uint64_t pgno;
    U64s frames;
    struct IdxEnt *next;
} IdxEnt;

typedef struct {
    IdxEnt **b;
    size_t nb, n;
} Index;

static void idx_init(Index *x) { x->nb = 1024; x->b = calloc(x->nb, sizeof *x->b); x->n = 0; }
static IdxEnt *idx_get(Index *x, uint64_t pgno, int make) {
    size_t h = set_hash(pgno, x->nb);
    for (IdxEnt *e = x->b[h]; e; e = e->next)
        if (e->pgno == pgno) return e;
    if (!make) return NULL;
    if (x->n > x->nb) {
        size_t nb = x->nb * 2;
        IdxEnt **b = calloc(nb, sizeof *b);
        for (size_t i = 0; i < x->nb; i++)
            for (IdxEnt *e = x->b[i], *n; e; e = n) { n = e->next; size_t k = set_hash(e->pgno, nb); e->next = b[k]; b[k] = e; }
        free(x->b);
        x->b = b;
        x->nb = nb;
        h = set_hash(pgno, x->nb);
    }
    IdxEnt *e = calloc(1, sizeof *e);
    e->pgno = pgno;
    e->next = x->b[h];
    x->b[h] = e;
    x->n++;
    return e;
}
static void idx_clear(Index *x) {
    for (size_t i = 0; i < x->nb; i++)
        for (IdxEnt *e = x->b[i], *n; e; e = n) { n = e->next; u64_free(&e->frames); free(e); }
    memset(x->b, 0, x->nb * sizeof *x->b);
    x->n = 0;
}
static void idx_free(Index *x) { idx_clear(x); free(x->b); }

/* the newest frame of `pgno` below `end`, or UINT64_MAX */
static uint64_t idx_find(Index *x, uint64_t pgno, uint64_t end) {
    IdxEnt *e = idx_get(x, pgno, 0);
    if (!e || !e->frames.n) return UINT64_MAX;
    size_t lo = 0, hi = e->frames.n;
    while (lo < hi) { size_t m = (lo + hi) / 2; if (e->frames.v[m] < end) lo = m + 1; else hi = m; }
    return lo ? e->frames.v[lo - 1] : UINT64_MAX;
}

/* ---- the store ------------------------------------------------------------ */

typedef struct {
    uint64_t epoch, end; /* frames [0, end) of that epoch */
} View;

typedef struct {
    View v;
    uint64_t commit;
} Reader;

struct GsTxn;

struct GsStore {
    GsqlIo *io;
    int own_io;
    GsqlFile *f, *log;
    char *path, *logpath, *dir;
    uint32_t ps;
    uint64_t cache_bytes, ckpt_frames;
    Cache cache;     /* committed versions: ver 0 = the main file, ver f+1 = log frame f */
    uint32_t salt1, salt2;
    uint64_t epoch;
    uint32_t seed;   /* the chain's starting value for this epoch */
    uint64_t nframes;     /* committed frames in the log (including not yet durable) */
    uint32_t end_chk;     /* the chain value after frame nframes-1 */
    uint64_t durable_end; /* frames flushed */
    uint64_t durable_commit, tip_commit;
    uint64_t backfilled;  /* every frame below this is in the main file */
    Index idx;
    Reader *readers;
    size_t nreaders, capreaders;
    struct GsTxn *writer;
    int failed;
    GsStats st;
};

typedef struct {
    BtPager base;
    struct GsTxn *t;
} WalPager;

struct GsTxn {
    BeTxn be;
    GsStore *db;
    WalPager pager;
    View v;               /* read: what it sees. write: the committed end */
    uint64_t commit;
    /* write only */
    Cache dirty;          /* pages this txn changed (ver 0) */
    uint64_t dirty_bytes;
    Index spilled;        /* page -> the frames this txn spilled */
    uint64_t spill_end;   /* frames written past nframes by this txn */
    uint32_t spill_chk;
    U64s zombies;         /* pages freed in the current operation */
};

/* ---- the log --------------------------------------------------------------- */

static uint64_t frame_off(GsStore *db, uint64_t i) { return LOG_HDR + i * (uint64_t)(FH + db->ps); }

static uint32_t header_seed(const unsigned char *h) { return get32(h + 28); }

static int write_log_header(GsStore *db) {
    unsigned char h[LOG_HDR];
    memset(h, 0, sizeof h);
    memcpy(h, LOG_MAGIC, 8);
    put32(h + 8, db->ps);
    put32(h + 12, db->salt1);
    put32(h + 16, db->salt2);
    put64(h + 20, db->epoch);
    put32(h + 28, crc32c(h, 28));
    db->seed = header_seed(h);
    int rc = gsql_io_write_at(db->log, 0, h, LOG_HDR);
    if (rc) return gs_io_status(rc);
    db->st.bytes_written += LOG_HDR;
    return GS_OK;
}

static int flush_file(GsStore *db, GsqlFile *f) {
    int rc = gsql_io_sync(f);
    db->st.flushes++;
    if (rc) { db->failed = 1; return GS_IO; } /* §1.3: never retried */
    return GS_OK;
}

/* Build one frame into `out` (FH + ps bytes); answers the new chain value. */
static uint32_t make_frame(GsStore *db, unsigned char *out, uint64_t pgno, const unsigned char *page,
                           uint64_t commit, uint64_t pages, uint32_t prev, uint64_t commit_id_for_seal) {
    memcpy(out + FH, page, db->ps);
    gs_page_seal(out + FH, db->ps, pgno, commit_id_for_seal);
    put64(out, pgno);
    put64(out + 8, commit);
    put64(out + 16, pages);
    put32(out + 24, db->salt1);
    put32(out + 28, db->salt2);
    uint32_t c = crc32c_extend(prev, out, 32);
    c = crc32c_extend(c, out + FH, db->ps);
    put32(out + 32, c);
    put32(out + 36, 0);
    return c;
}

/* ---- page reads ------------------------------------------------------------ */

static int read_main(GsStore *db, uint64_t pgno, unsigned char *buf) {
    size_t got;
    int rc = gsql_io_read_at(db->f, pgno * db->ps, buf, db->ps, &got);
    if (rc) return gs_io_status(rc);
    db->st.pages_read++;
    if (got != db->ps) return GS_CORRUPT;
    return gs_page_verify(buf, db->ps, pgno);
}

static int read_frame(GsStore *db, uint64_t frame, uint64_t pgno, unsigned char *buf) {
    size_t got;
    int rc = gsql_io_read_at(db->log, frame_off(db, frame) + FH, buf, db->ps, &got);
    if (rc) return gs_io_status(rc);
    db->st.pages_read++;
    if (got != db->ps) return GS_CORRUPT;
    return gs_page_verify(buf, db->ps, pgno);
}

/* The committed version of `pgno` a view sees: from the cache, the log or the main file. */
static int committed_page(GsStore *db, const View *v, uint64_t pgno, unsigned char **page) {
    uint64_t f = v->epoch == db->epoch ? idx_find(&db->idx, pgno, v->end) : UINT64_MAX;
    uint64_t ver = f == UINT64_MAX ? 0 : f + 1;
    CPage *p = cache_find(&db->cache, pgno, ver);
    if (p) {
        db->st.cache_hits++;
        lru_unlink(p);
        lru_front(&db->cache, p);
        *page = p->buf;
        return GS_OK;
    }
    unsigned char *buf = malloc(db->ps);
    if (!buf) return GS_IO;
    int rc = ver ? read_frame(db, f, pgno, buf) : read_main(db, pgno, buf);
    if (rc) { free(buf); return rc; }
    if (!(p = cache_insert(&db->cache, pgno, ver, buf))) { free(buf); return GS_IO; }
    *page = buf;
    return GS_OK;
}

/* ---- the pager -------------------------------------------------------------- */

static uint64_t state_pages(GsStore *db, struct GsTxn *t);

static int pg_get(BtPager *b, uint64_t pgno, unsigned char **page) {
    struct GsTxn *t = ((WalPager *)b)->t;
    GsStore *db = t->db;
    if (pgno == 0) return GS_CORRUPT;
    if (t->be.write) {
        CPage *d = cache_find(&t->dirty, pgno, 0);
        if (d) { *page = d->buf; return GS_OK; }
        IdxEnt *s = idx_get(&t->spilled, pgno, 0);
        if (s && s->frames.n) {
            /* spilled and touched again: back into the dirty set */
            unsigned char *buf = malloc(db->ps);
            if (!buf) return GS_IO;
            int rc = read_frame(db, s->frames.v[s->frames.n - 1], pgno, buf);
            if (rc) { free(buf); return rc; }
            d = cache_insert(&t->dirty, pgno, 0, buf);
            d->dirty = 1;
            t->dirty_bytes += db->ps;
            *page = buf;
            return GS_OK;
        }
    }
    return committed_page(db, &t->v, pgno, page);
}

static int make_dirty(struct GsTxn *t, uint64_t pgno, unsigned char **page, int zero) {
    GsStore *db = t->db;
    CPage *d = cache_find(&t->dirty, pgno, 0);
    if (d) { if (zero) memset(d->buf, 0, db->ps); *page = d->buf; return GS_OK; }
    unsigned char *src = NULL;
    if (!zero) {
        int rc = pg_get(&t->pager.base, pgno, &src);
        if (rc) return rc;
        d = cache_find(&t->dirty, pgno, 0); /* pg_get may have reloaded a spilled page */
        if (d) { *page = d->buf; return GS_OK; }
    }
    unsigned char *buf = zero ? calloc(1, db->ps) : malloc(db->ps);
    if (!buf) return GS_IO;
    if (!zero) memcpy(buf, src, db->ps);
    d = cache_insert(&t->dirty, pgno, 0, buf);
    if (!d) { free(buf); return GS_IO; }
    d->dirty = 1;
    t->dirty_bytes += db->ps;
    *page = buf;
    return GS_OK;
}

static int pg_write(BtPager *b, uint64_t pgno, uint64_t *out, unsigned char **page) {
    *out = pgno; /* in place */
    return make_dirty(((WalPager *)b)->t, pgno, page, 0);
}

static int state(struct GsTxn *t, unsigned char **s) { return make_dirty(t, STATE_PG, s, 0); }

static uint32_t trunk_cap(uint32_t ps) { return (ps - GS_PAGE_TAG - T_ENTRIES) / 8; }

static int pg_alloc(BtPager *b, uint64_t *pgno, unsigned char **page) {
    struct GsTxn *t = ((WalPager *)b)->t;
    unsigned char *s;
    int rc = state(t, &s);
    if (rc) return rc;
    uint64_t head = get64(s + S_FREE_HEAD);
    if (head) {
        unsigned char *tr;
        if ((rc = make_dirty(t, head, &tr, 0))) return rc;
        if ((rc = state(t, &s))) return rc;
        uint32_t n = get32(tr + T_COUNT);
        if (n) {
            *pgno = get64(tr + T_ENTRIES + 8 * (n - 1));
            put32(tr + T_COUNT, n - 1);
        } else {
            *pgno = head; /* the empty trunk itself */
            put64(s + S_FREE_HEAD, get64(tr + T_NEXT));
        }
        put64(s + S_FREE_COUNT, get64(s + S_FREE_COUNT) - 1);
    } else {
        *pgno = get64(s + S_PAGES);
        put64(s + S_PAGES, *pgno + 1);
    }
    return make_dirty(t, *pgno, page, 1);
}

static int pg_free(BtPager *b, uint64_t pgno) {
    struct GsTxn *t = ((WalPager *)b)->t;
    /* not until the operation ends: the tree code may still hold a pointer */
    return u64_push(&t->zombies, pgno) ? GS_OK : GS_IO;
}

static int free_now(struct GsTxn *t, uint64_t pgno) {
    unsigned char *s, *tr;
    int rc = state(t, &s);
    if (rc) return rc;
    uint64_t head = get64(s + S_FREE_HEAD);
    if (head) {
        if ((rc = make_dirty(t, head, &tr, 0))) return rc;
        uint32_t n = get32(tr + T_COUNT);
        if (n < trunk_cap(t->db->ps)) {
            put64(tr + T_ENTRIES + 8 * n, pgno);
            put32(tr + T_COUNT, n + 1);
            if ((rc = state(t, &s))) return rc;
            put64(s + S_FREE_COUNT, get64(s + S_FREE_COUNT) + 1);
            return GS_OK;
        }
    }
    /* the freed page becomes the new head trunk */
    if ((rc = make_dirty(t, pgno, &tr, 1))) return rc;
    tr[0] = GS_PT_FREELIST;
    put64(tr + T_NEXT, head);
    put32(tr + T_COUNT, 0);
    if ((rc = state(t, &s))) return rc;
    put64(s + S_FREE_HEAD, pgno);
    put64(s + S_FREE_COUNT, get64(s + S_FREE_COUNT) + 1);
    return GS_OK;
}

static int spill(struct GsTxn *t);

static void after_op(BeTxn *be) {
    struct GsTxn *t = (struct GsTxn *)be;
    GsStore *db = t->db;
    if (be->write) {
        for (size_t i = 0; i < t->zombies.n; i++) {
            int rc = free_now(t, t->zombies.v[i]);
            if (rc && !be->failed) be->failed = rc;
        }
        t->zombies.n = 0;
        if (t->dirty_bytes > db->cache_bytes / 2) spill(t);
    }
    uint64_t limit = db->cache_bytes / db->ps;
    if (limit < 16) limit = 16;
    while (db->cache.count > limit && db->cache.lru.prev != &db->cache.lru) cache_remove(&db->cache, db->cache.lru.prev);
}

/* G5: write the oldest dirty pages to the log as frames with no commit marker.
 * Spilled pages are read back into the dirty set if touched again. */
static int spill(struct GsTxn *t) {
    GsStore *db = t->db;
    unsigned char *frame = malloc(FH + db->ps);
    if (!frame) return GS_IO;
    int rc = GS_OK;
    if (!t->spill_end) { t->spill_end = db->nframes; t->spill_chk = db->end_chk; }
    while (t->dirty_bytes > db->cache_bytes / 4 && t->dirty.lru.prev != &t->dirty.lru) {
        CPage *p = t->dirty.lru.prev;
        if (p->pgno == STATE_PG) { lru_unlink(p); lru_front(&t->dirty, p); if (t->dirty.count == 1) break; continue; }
        t->spill_chk = make_frame(db, frame, p->pgno, p->buf, 0, 0, t->spill_chk, t->commit);
        rc = gs_io_status(gsql_io_write_at(db->log, frame_off(db, t->spill_end), frame, FH + db->ps));
        if (rc) break;
        db->st.pages_written++;
        db->st.bytes_written += FH + db->ps;
        IdxEnt *e = idx_get(&t->spilled, p->pgno, 1);
        u64_push(&e->frames, t->spill_end);
        t->spill_end++;
        cache_remove(&t->dirty, p);
        t->dirty_bytes -= db->ps;
    }
    free(frame);
    if (rc && !t->be.failed) t->be.failed = rc;
    return rc;
}

/* ---- reading the state page --------------------------------------------- */

static int view_state(GsStore *db, struct GsTxn *t, unsigned char **s) {
    (void)db;
    return pg_get(&t->pager.base, STATE_PG, s);
}

static uint64_t state_pages(GsStore *db, struct GsTxn *t) {
    unsigned char *s;
    if (view_state(db, t, &s)) return 0;
    return get64(s + S_PAGES);
}

/* ---- checkpoint --------------------------------------------------------- */

static int checkpoint(GsStore *db) {
    uint64_t limit = db->durable_end;
    for (size_t i = 0; i < db->nreaders; i++) {
        if (db->readers[i].v.epoch != db->epoch) return GS_OK; /* it reads the main file alone */
        if (db->readers[i].v.end < limit) limit = db->readers[i].v.end;
    }
    int rc = GS_OK;
    if (limit > db->backfilled) {
        unsigned char *buf = malloc(db->ps);
        int wrote = 0;
        for (size_t b = 0; b < db->idx.nb && rc == GS_OK; b++)
            for (IdxEnt *e = db->idx.b[b]; e && rc == GS_OK; e = e->next) {
                uint64_t f = idx_find(&db->idx, e->pgno, limit);
                if (f == UINT64_MAX || f < db->backfilled) continue;
                if ((rc = read_frame(db, f, e->pgno, buf))) break;
                rc = gs_io_status(gsql_io_write_at(db->f, e->pgno * db->ps, buf, db->ps));
                if (rc) break;
                CPage *old = cache_find(&db->cache, e->pgno, 0);
                if (old) cache_remove(&db->cache, old);
                db->st.pages_written++;
                db->st.bytes_written += db->ps;
                wrote = 1;
            }
        free(buf);
        if (rc == GS_OK && wrote) rc = flush_file(db, db->f);
        if (rc) return rc;
        db->backfilled = limit;
    }
    /* reset when the main file holds everything and no reader needs the log */
    if (db->backfilled == db->nframes && db->nframes > 0 && db->durable_end == db->nframes) {
        for (size_t i = 0; i < db->nreaders; i++)
            if (db->readers[i].v.epoch == db->epoch && db->readers[i].v.end != db->nframes) return GS_OK;
        /* salt2 is RANDOM: computed from the old salt, a reset whose header
         * a crash lost would be repeated with the same salt, and the frames
         * written in the lost epoch -- no longer truncated away -- would read
         * as valid again (found by the crash harness: a continuation came back
         * with a commit it never reached) */
        unsigned char rnd[16];
        gs_identity_new(db->io, rnd);
        db->salt1++;
        db->salt2 = get32(rnd);
        db->epoch++;
        if ((rc = write_log_header(db))) return rc;
        /* The log is NOT truncated: frames past the new end carry the old salt
         * and are refused by recovery, and writing over existing bytes keeps a
         * commit's flush from also committing a change of file size -- on this
         * machine 250 us against 1 ms (SQLite reuses its WAL the same way). It
         * is truncated only when it has grown past four checkpoints' worth. */
        uint64_t sz = 0;
        gsql_io_size(db->log, &sz);
        if (sz > LOG_HDR + 4 * db->ckpt_frames * (uint64_t)(FH + db->ps) &&
            (rc = gs_io_status(gsql_io_truncate(db->log, LOG_HDR))))
            return rc;
        if ((rc = flush_file(db, db->log))) return rc;
        idx_clear(&db->idx);
        db->nframes = db->durable_end = db->backfilled = 0;
        db->end_chk = db->seed;
        /* log versions are gone: drop them from the cache */
        for (CPage *p = db->cache.lru.next, *n; p != &db->cache.lru; p = n) {
            n = p->next;
            if (p->ver) cache_remove(&db->cache, p);
        }
    }
    return GS_OK;
}

/* ---- open, create, recover ----------------------------------------------- */

/* The state page and the catalog first, flushed, and only then the header:
 * so a valid header means creation finished. Written the other way round, a
 * power loss could keep the header and lose the pages after it, and recovery
 * found a database it could not read (found by the crash harness -- first the
 * state page, then, once that was checked, the catalog: the fix is the order,
 * not more checks). */
static int create_main(GsStore *db) {
    unsigned char *page = calloc(1, db->ps);
    int rc = gs_io_status(gsql_io_truncate(db->f, 0));
    page[0] = GS_PT_META;
    put64(page + S_CATALOG, 2);
    put64(page + S_PAGES, 3);
    put64(page + S_COMMIT, 1);
    gs_page_seal(page, db->ps, STATE_PG, 1);
    if (rc == GS_OK) rc = gs_io_status(gsql_io_write_at(db->f, (uint64_t)STATE_PG * db->ps, page, db->ps));
    memset(page, 0, db->ps);
    page[0] = GS_PT_LEAF;
    put16(page + 42, 56);
    put16(page + 44, (uint16_t)(db->ps - GS_PAGE_TAG));
    gs_page_seal(page, db->ps, 2, 1);
    if (rc == GS_OK) rc = gs_io_status(gsql_io_write_at(db->f, 2 * (uint64_t)db->ps, page, db->ps));
    if (rc == GS_OK) rc = flush_file(db, db->f);
    memset(page, 0, db->ps);
    GsFileHeader h = {GS_FORMAT_MAJOR, 0, db->ps, {0}, 0, 0};
    gs_identity_new(db->io, h.identity);
    gs_header_write(page, &h);
    if (rc == GS_OK) rc = gs_io_status(gsql_io_write_at(db->f, 0, page, db->ps));
    if (rc == GS_OK) rc = flush_file(db, db->f);
    free(page);
    return rc;
}

static int new_log(GsStore *db) {
    db->epoch++;
    db->salt1 = (uint32_t)(db->epoch * 2654435761u);
    unsigned char id[16];
    gs_identity_new(db->io, id);
    db->salt2 = get32(id);
    int rc = gs_io_status(gsql_io_truncate(db->log, 0));
    if (rc == GS_OK) rc = write_log_header(db);
    if (rc == GS_OK) rc = flush_file(db, db->log);
    db->nframes = db->durable_end = db->backfilled = 0;
    db->end_chk = db->seed;
    idx_clear(&db->idx);
    return rc;
}

/* Replay: every frame whose salts and chain hold, up to the last commit marker. */
static int recover_log(GsStore *db) {
    unsigned char h[LOG_HDR];
    size_t got = 0;
    int rc = gs_io_status(gsql_io_read_at(db->log, 0, h, LOG_HDR, &got));
    if (rc) return rc;
    if (got < 32 || memcmp(h, LOG_MAGIC, 8) || get32(h + 28) != crc32c(h, 28) || get32(h + 8) != db->ps) {
        /* no valid header: the log was being created or reset. A reset follows
         * a flushed checkpoint of every frame, so nothing committed is lost. */
        return new_log(db);
    }
    db->salt1 = get32(h + 12);
    db->salt2 = get32(h + 16);
    db->epoch = get64(h + 20);
    db->seed = header_seed(h);
    uint32_t chk = db->seed;
    unsigned char *frame = malloc(FH + db->ps);
    U64s pend_pg = {0}, pend_fr = {0};
    uint64_t i = 0;
    db->nframes = 0;
    db->end_chk = db->seed;
    idx_clear(&db->idx);
    for (;; i++) {
        rc = gs_io_status(gsql_io_read_at(db->log, frame_off(db, i), frame, FH + db->ps, &got));
        if (rc) break;
        if (got != FH + db->ps) break;
        if (get32(frame + 24) != db->salt1 || get32(frame + 28) != db->salt2) break;
        uint32_t c = crc32c_extend(chk, frame, 32);
        c = crc32c_extend(c, frame + FH, db->ps);
        if (c != get32(frame + 32)) break;
        uint64_t pgno = get64(frame);
        if (pgno == 0 || gs_page_verify(frame + FH, db->ps, pgno) != GS_OK) break;
        chk = c;
        u64_push(&pend_pg, pgno);
        u64_push(&pend_fr, i);
        if (get64(frame + 8)) {
            for (size_t k = 0; k < pend_pg.n; k++) u64_push(&idx_get(&db->idx, pend_pg.v[k], 1)->frames, pend_fr.v[k]);
            pend_pg.n = pend_fr.n = 0;
            db->nframes = i + 1;
            db->end_chk = c;
            db->tip_commit = get64(frame + 8);
        }
    }
    /* Truncate at the committed end. Frames past it belong to commits a
     * crash lost, and they can sit ANYWHERE beyond -- behind a torn frame as
     * well as right after the end. Left there, a new commit that writes the
     * SAME bytes at the same positions (the same transaction run again)
     * continues their checksum chain, and the lost commit after it comes back
     * (found by the crash harness's continuation stage; a first fix that
     * erased frames up to the first salt mismatch stopped at the torn frame
     * and missed the ones behind it). Recovery is rare, so the log regrowing
     * by appends until its next reset is the cheap part. */
    uint64_t lsz = 0;
    gsql_io_size(db->log, &lsz);
    if (rc == GS_OK && lsz > frame_off(db, db->nframes)) {
        rc = gs_io_status(gsql_io_truncate(db->log, frame_off(db, db->nframes)));
        if (rc == GS_OK) rc = flush_file(db, db->log);
    }
    free(frame);
    u64_free(&pend_pg);
    u64_free(&pend_fr);
    db->durable_end = db->nframes;
    db->durable_commit = db->tip_commit;
    db->backfilled = 0;
    return rc;
}

static void store_free(GsStore *db) {
    cache_free(&db->cache);
    idx_free(&db->idx);
    free(db->readers);
    if (db->own_io) gsql_io_free(db->io);
    free(db->path);
    free(db->logpath);
    free(db->dir);
    free(db);
}

static int wal_open(const char *path, const GsOptions *opt, GsStore **out) {
    GsStore *db = calloc(1, sizeof *db);
    if (!db) return GS_IO;
    db->io = opt && opt->io ? opt->io : NULL;
    if (!db->io) { db->io = gsql_io_real(); db->own_io = 1; }
    db->path = gs_path_with(path, "");
    db->logpath = gs_path_with(path, "-wal");
    db->dir = gs_dirname(path);
    db->ps = opt && opt->page_size ? opt->page_size : 4096;
    db->cache_bytes = opt && opt->cache_bytes ? opt->cache_bytes : 64u << 20;
    db->ckpt_frames = opt && opt->tune_a ? opt->tune_a : 1000;
    cache_init(&db->cache);
    idx_init(&db->idx);
    int create = opt && opt->create, created = 0;
    int rc = gsql_io_open(db->io, path, 0, &db->f);
    if (rc == ENOENT && create) { rc = gsql_io_open(db->io, path, GSQL_IO_CREATE, &db->f); created = 1; }
    rc = rc == ENOENT ? GS_NOT_FOUND : gs_io_status(rc);
    if (rc == GS_OK) rc = gs_io_status(gsql_io_lock(db->f, 1));
    if (rc == GS_OK) {
        unsigned char hdr[GS_HEADER_FIXED];
        size_t got = 0;
        GsFileHeader h;
        rc = gs_io_status(gsql_io_read_at(db->f, 0, hdr, sizeof hdr, &got));
        int hrc = rc ? rc : gs_header_read(hdr, got, &h);
        if (rc == GS_OK && hrc == GS_OK) {
            db->ps = h.page_size;
        } else if (rc == GS_OK && create) {
            /* creation never finished (§2.A's argument: no commit is possible
             * before the creating open returns); a main file this small holds
             * nothing but what creation writes */
            uint64_t sz = 0;
            gsql_io_size(db->f, &sz);
            if (created || sz <= (uint64_t)db->ps * 3) rc = create_main(db), created = 1;
            else rc = hrc;
        } else if (rc == GS_OK) {
            rc = hrc;
        }
    }
    if (rc == GS_OK) {
        int lrc = gsql_io_open(db->io, db->logpath, 0, &db->log);
        int log_created = 0;
        if (lrc == ENOENT) { lrc = gsql_io_open(db->io, db->logpath, GSQL_IO_CREATE, &db->log); log_created = 1; }
        rc = gs_io_status(lrc);
        /* a main file just created has no history, whatever a stale log says */
        if (rc == GS_OK) rc = (log_created || created) ? new_log(db) : recover_log(db);
        /* both names must survive a power loss before anything is committed */
        if (rc == GS_OK && (created || log_created)) rc = gs_io_status(gsql_io_sync_dir(db->io, db->dir));
    }
    if (rc == GS_OK) {
        /* the last commit id comes from the state page as the log sees it */
        struct GsTxn tmp;
        memset(&tmp, 0, sizeof tmp);
        tmp.db = db;
        tmp.v.epoch = db->epoch;
        tmp.v.end = db->nframes;
        tmp.pager.t = &tmp;
        tmp.pager.base.page_size = db->ps;
        unsigned char *s;
        rc = committed_page(db, &tmp.v, STATE_PG, &s);
        if (rc == GS_OK) db->tip_commit = db->durable_commit = get64(s + S_COMMIT);
    }
    if (rc) {
        if (db->log) gsql_io_close(db->log);
        if (db->f) gsql_io_close(db->f);
        store_free(db);
        return rc;
    }
    *out = db;
    return GS_OK;
}

static int wal_sync(GsStore *db, uint64_t *durable);

static int wal_close(GsStore *db) {
    if (db->writer || db->nreaders) return GS_MISUSE;
    if (!db->failed) {
        wal_sync(db, NULL);
        if (!db->failed) checkpoint(db);
    }
    gsql_io_close(db->log);
    gsql_io_close(db->f);
    store_free(db);
    return GS_OK;
}

/* ---- transactions ---------------------------------------------------------- */

static struct GsTxn *txn_new(GsStore *db) {
    struct GsTxn *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->db = db;
    t->pager.base.page_size = db->ps;
    t->pager.base.link_leaves = 1;
    t->pager.base.get = pg_get;
    t->pager.base.write = pg_write;
    t->pager.base.alloc = pg_alloc;
    t->pager.base.free = pg_free;
    t->pager.t = t;
    return t;
}

static int begin(GsStore *db, struct GsTxn *t, int write) {
    unsigned char *s;
    int rc = view_state(db, t, &s);
    if (rc) return rc;
    be_txn_init(&t->be, &t->pager.base, get64(s + S_CATALOG), write);
    t->be.after_op = after_op;
    return GS_OK;
}

static int wal_begin_read(GsStore *db, struct GsTxn **out) {
    if (db->failed) return GS_FAILED;
    struct GsTxn *t = txn_new(db);
    if (!t) return GS_IO;
    t->v.epoch = db->epoch;
    t->v.end = db->durable_end; /* §1.3: only what is durable is visible */
    t->commit = db->durable_commit;
    int rc = begin(db, t, 0);
    if (rc) { free(t); return rc; }
    if (db->nreaders == db->capreaders) {
        db->capreaders = db->capreaders ? db->capreaders * 2 : 8;
        db->readers = realloc(db->readers, db->capreaders * sizeof *db->readers);
    }
    db->readers[db->nreaders].v = t->v;
    db->readers[db->nreaders].commit = t->commit;
    db->nreaders++;
    *out = t;
    return GS_OK;
}

static int wal_begin_write(GsStore *db, struct GsTxn **out) {
    if (db->failed) return GS_FAILED;
    if (db->writer) return GS_BUSY;
    struct GsTxn *t = txn_new(db);
    if (!t) return GS_IO;
    t->v.epoch = db->epoch;
    t->v.end = db->nframes;
    t->commit = db->tip_commit + 1;
    cache_init(&t->dirty);
    idx_init(&t->spilled);
    int rc = begin(db, t, 1);
    if (rc) { cache_free(&t->dirty); idx_free(&t->spilled); free(t); return rc; }
    db->writer = t;
    *out = t;
    return GS_OK;
}

static void txn_release(struct GsTxn *t) {
    be_txn_free(&t->be);
    if (t->be.write) {
        cache_free(&t->dirty);
        idx_free(&t->spilled);
        u64_free(&t->zombies);
    }
    free(t);
}

static void wal_abort(struct GsTxn *t) {
    GsStore *db = t->db;
    if (!t->be.write) {
        for (size_t i = 0; i < db->nreaders; i++)
            if (db->readers[i].v.epoch == t->v.epoch && db->readers[i].v.end == t->v.end) {
                db->readers[i] = db->readers[--db->nreaders];
                break;
            }
        txn_release(t);
        return;
    }
    /* spilled frames are past the committed end with no marker: the next
     * commit writes over them, and recovery would never accept them */
    db->writer = NULL;
    txn_release(t);
}

static int wal_commit(struct GsTxn *t, int sync, uint64_t *commit_id) {
    GsStore *db = t->db;
    if (!t->be.write) return GS_MISUSE;
    if (db->failed) { wal_abort(t); return GS_FAILED; }
    if (t->be.failed) { int rc = t->be.failed; wal_abort(t); return rc; }
    int rc = be_flush(&t->be);
    if (rc == GS_OK) { after_op(&t->be); rc = t->be.failed; }
    unsigned char *s;
    if (rc == GS_OK) rc = state(t, &s);
    if (rc) { wal_abort(t); return rc; }
    put64(s + S_CATALOG, t->be.catalog);
    put64(s + S_COMMIT, t->commit);
    uint64_t pages = get64(s + S_PAGES);

    /* every dirty page becomes a frame, the state page last with the marker;
     * written in batches so a large commit is a few large writes */
    uint64_t at = t->spill_end ? t->spill_end : db->nframes;
    uint32_t chk = t->spill_end ? t->spill_chk : db->end_chk;
    size_t batch = 32, nb = 0;
    unsigned char *buf = malloc(batch * (FH + db->ps));
    if (!buf) { wal_abort(t); return GS_IO; }
    uint64_t batch_start = at;
    U64s pgs = {0}, frs = {0};
    CPage *statepg = cache_find(&t->dirty, STATE_PG, 0);
    for (CPage *p = t->dirty.lru.next;; p = p->next) {
        int last = p == &t->dirty.lru;
        CPage *q = last ? statepg : p;
        if (!last && q == statepg) continue;
        chk = make_frame(db, buf + nb * (FH + db->ps), q->pgno, q->buf, last ? t->commit : 0, last ? pages : 0, chk, t->commit);
        u64_push(&pgs, q->pgno);
        u64_push(&frs, at);
        at++;
        nb++;
        if (nb == batch || last) {
            rc = gs_io_status(gsql_io_write_at(db->log, frame_off(db, batch_start), buf, nb * (FH + db->ps)));
            db->st.pages_written += nb;
            db->st.bytes_written += nb * (FH + db->ps);
            nb = 0;
            batch_start = at;
            if (rc) break;
        }
        if (last) break;
    }
    free(buf);
    if (rc == GS_OK && sync) rc = flush_file(db, db->log);
    if (rc) {
        /* FULL: no marker reached the disk, so recovery would never take
         * these frames and the next commit writes over them. IO: a flush
         * failed, so the store is unusable (§1.3). */
        u64_free(&pgs);
        u64_free(&frs);
        wal_abort(t);
        return rc == GS_FULL ? GS_FULL : GS_IO;
    }
    /* the index takes the spilled frames and the new ones, in frame order */
    for (size_t b = 0; b < t->spilled.nb; b++)
        for (IdxEnt *e = t->spilled.b[b]; e; e = e->next)
            for (size_t k = 0; k < e->frames.n; k++) u64_push(&idx_get(&db->idx, e->pgno, 1)->frames, e->frames.v[k]);
    for (size_t k = 0; k < pgs.n; k++) {
        IdxEnt *e = idx_get(&db->idx, pgs.v[k], 1);
        u64_push(&e->frames, frs.v[k]);
    }
    /* frames arrive in ascending order -- earlier commits, this txn's spills,
     * then its final frames -- so every page's list stays sorted */
    u64_free(&pgs);
    u64_free(&frs);
    db->nframes = at;
    db->end_chk = chk;
    db->tip_commit = t->commit;
    if (sync) { db->durable_end = db->nframes; db->durable_commit = db->tip_commit; }
    if (commit_id) *commit_id = t->commit;
    db->writer = NULL;
    txn_release(t);
    if (db->nframes - db->backfilled >= db->ckpt_frames) {
        rc = checkpoint(db);
        /* The commit itself may be durable, but the checkpoint after it failed
         * a flush and the store is now unusable (§1.3). Answering OK hid that
         * (found by the crash harness); IO says it, and is safe, since a commit
         * reported as failed may be either kept or lost. */
        if (rc == GS_IO || db->failed) return GS_IO;
    }
    return GS_OK;
}

static int wal_sync(GsStore *db, uint64_t *durable) {
    if (db->failed) return GS_FAILED;
    int rc = GS_OK;
    if (db->durable_end != db->nframes) {
        rc = flush_file(db, db->log);
        if (rc == GS_OK) { db->durable_end = db->nframes; db->durable_commit = db->tip_commit; }
    }
    /* The log resets only when every frame is durable, and under group commit
     * the commit that triggers a checkpoint is almost never synced -- so it was
     * attempted only where it could not finish, and a million group-64 updates
     * grew the log to 166x the data. A sync is the one moment the condition
     * holds, so a checkpoint runs here once the LOG is a checkpoint long --
     * not once that much is un-backfilled, which the commit-time checkpoints
     * keep below the threshold by backfilling every durable frame (measured:
     * that test alone left the growth exactly as it was). */
    if (rc == GS_OK && db->nframes >= db->ckpt_frames) {
        rc = checkpoint(db);
        if (rc == GS_IO || db->failed) rc = GS_IO;
    }
    if (durable) *durable = db->durable_commit;
    return rc;
}

/* ---- the shared engine ----------------------------------------------------- */

#define BE(t) (&(t)->be)
#define LIVE(t) if ((t)->db->failed) return GS_FAILED
static int wal_tree_create(struct GsTxn *t, GsBytes n) { LIVE(t); return be_tree_create(BE(t), n); }
static int wal_tree_drop(struct GsTxn *t, GsBytes n) { LIVE(t); return be_tree_drop(BE(t), n); }
static int wal_tree_list(struct GsTxn *t, int (*e)(void *, GsBytes), void *c) { return be_tree_list(BE(t), e, c); }
static int wal_tree_replace(struct GsTxn *t, GsBytes a, GsBytes b) { LIVE(t); return be_tree_replace(BE(t), a, b); }
static int wal_get(struct GsTxn *t, GsBytes tr, GsBytes k, GsBytes *v) { LIVE(t); return be_get(BE(t), tr, k, v); }
static int wal_put(struct GsTxn *t, GsBytes tr, GsBytes k, GsBytes v, unsigned f) { LIVE(t); return be_put(BE(t), tr, k, v, f); }
static int wal_del(struct GsTxn *t, GsBytes tr, GsBytes k) { LIVE(t); return be_del(BE(t), tr, k); }
static int wal_cursor_open(struct GsTxn *t, GsBytes tr, GsCursor **c) { return be_cursor_open(BE(t), tr, (BeCursor **)c); }
static int wal_seek(GsCursor *c, GsBytes k, GsSeek m, int *p) { return be_seek((BeCursor *)c, k, m, p); }
static int wal_first(GsCursor *c, int *p) { return be_first((BeCursor *)c, p); }
static int wal_last(GsCursor *c, int *p) { return be_last((BeCursor *)c, p); }
static int wal_next(GsCursor *c, int *p) { return be_next((BeCursor *)c, p); }
static int wal_prev(GsCursor *c, int *p) { return be_prev((BeCursor *)c, p); }
static int wal_key(GsCursor *c, GsBytes *k) { return be_key((BeCursor *)c, k); }
static int wal_value(GsCursor *c, GsBytes *v) { return be_value((BeCursor *)c, v); }
static int wal_cursor_put(GsCursor *c, GsBytes v) { return be_cursor_put((BeCursor *)c, v); }
static int wal_cursor_delete(GsCursor *c) { return be_cursor_delete((BeCursor *)c); }
static void wal_cursor_close(GsCursor *c) { be_cursor_close((BeCursor *)c); }

/* ---- check, backup, stats -------------------------------------------------- */

typedef struct {
    struct GsTxn *t;
    unsigned char *seen;
    uint64_t pages;
    void (*report)(void *, const char *);
    void *ctx;
    int bad;
} Chk;

static void say(Chk *k, const char *fmt, uint64_t a, uint64_t b) {
    k->bad = 1;
    if (k->report) {
        char line[160];
        snprintf(line, sizeof line, fmt, (unsigned long long)a, (unsigned long long)b);
        k->report(k->ctx, line);
    }
}
static int mark(void *ctx, uint64_t pgno) {
    Chk *k = ctx;
    if (pgno < FIRST_DATA || pgno >= k->pages) say(k, "page %llu is out of range (%llu pages)", pgno, k->pages);
    else if (k->seen[pgno]) say(k, "page %llu is used twice (%llu)", pgno, k->seen[pgno]);
    else k->seen[pgno] = 1;
    return 0;
}
static void fwd(void *ctx, const char *line) {
    Chk *k = ctx;
    k->bad = 1;
    if (k->report) k->report(k->ctx, line);
}
static int each_tree(void *ctx, GsBytes name, uint64_t root) {
    Chk *k = ctx;
    (void)name;
    if (bt_check(&k->t->pager.base, root, fwd, k, NULL) != GS_OK) k->bad = 1;
    bt_walk(&k->t->pager.base, root, mark, k);
    return 0;
}

static int wal_check(GsStore *db, void (*report)(void *, const char *), void *ctx) {
    if (db->writer) return GS_MISUSE;
    struct GsTxn *t = txn_new(db);
    t->v.epoch = db->epoch;
    t->v.end = db->nframes;
    unsigned char *s;
    int rc = view_state(db, t, &s);
    Chk k = {t, NULL, 0, report, ctx, 0};
    if (rc) { say(&k, "the state page is unreadable (%llu%llu)", 0, 0); free(t); return GS_CORRUPT; }
    k.pages = get64(s + S_PAGES);
    uint64_t catalog = get64(s + S_CATALOG), head = get64(s + S_FREE_HEAD), nfree = get64(s + S_FREE_COUNT);
    k.seen = calloc(k.pages + 1, 1);
    if (bt_check(&t->pager.base, catalog, fwd, &k, NULL) != GS_OK) k.bad = 1;
    bt_walk(&t->pager.base, catalog, mark, &k);
    be_roots(&t->pager.base, catalog, each_tree, &k);
    uint64_t counted = 0, guard = k.pages + 1;
    while (head && guard--) {
        unsigned char *tr;
        if (pg_get(&t->pager.base, head, &tr) || tr[0] != GS_PT_FREELIST) { say(&k, "free trunk %llu unreadable (%llu)", head, 0); break; }
        mark(&k, head);
        counted++;
        uint32_t n = get32(tr + T_COUNT);
        for (uint32_t i = 0; i < n && i < trunk_cap(db->ps); i++) { mark(&k, get64(tr + T_ENTRIES + 8 * i)); counted++; }
        head = get64(tr + T_NEXT);
    }
    if (counted != nfree) say(&k, "the free list holds %llu pages, the state page says %llu", counted, nfree);
    for (uint64_t p = FIRST_DATA; p < k.pages; p++)
        if (!k.seen[p]) say(&k, "page %llu is neither reachable nor free (a leak, of %llu)", p, k.pages);
    free(k.seen);
    be_txn_free(&t->be);
    free(t);
    return k.bad ? GS_CORRUPT : GS_OK;
}

static int wal_backup(struct GsTxn *snap, const char *dest) {
    GsStore *db = snap->db;
    if (snap->be.write) return GS_MISUSE;
    char *tmp = gs_path_with(dest, ".tmp"), *ddir = gs_dirname(dest), *dlog = gs_path_with(dest, "-wal");
    gsql_io_remove(db->io, tmp);
    GsqlFile *dst = NULL;
    int rc = gs_io_status(gsql_io_open(db->io, tmp, GSQL_IO_CREATE | GSQL_IO_EXCLUSIVE, &dst));
    unsigned char *page = malloc(db->ps);
    if (rc == GS_OK) {
        size_t got;
        rc = gs_io_status(gsql_io_read_at(db->f, 0, page, db->ps, &got));
        if (rc == GS_OK) rc = gs_io_status(gsql_io_write_at(dst, 0, page, db->ps));
    }
    /* every page as the snapshot sees it, into a main file with no log */
    uint64_t pages = rc == GS_OK ? state_pages(db, snap) : 0;
    for (uint64_t p = 1; p < pages && rc == GS_OK; p++) {
        unsigned char *src;
        rc = pg_get(&snap->pager.base, p, &src);
        if (rc == GS_OK) rc = gs_io_status(gsql_io_write_at(dst, p * db->ps, src, db->ps));
    }
    if (rc == GS_OK) rc = gs_io_status(gsql_io_sync(dst));
    if (dst) gsql_io_close(dst);
    gsql_io_remove(db->io, dlog); /* a stale log beside the destination would be replayed into it */
    if (rc == GS_OK) rc = gs_io_status(gsql_io_rename(db->io, tmp, dest));
    if (rc == GS_OK) rc = gs_io_status(gsql_io_sync_dir(db->io, ddir));
    if (rc) gsql_io_remove(db->io, tmp);
    free(page);
    free(tmp);
    free(ddir);
    free(dlog);
    return rc;
}

static void wal_stats(GsStore *db, GsStats *out) {
    *out = db->st;
    uint64_t a = 0, b = 0;
    gsql_io_size(db->f, &a);
    gsql_io_size(db->log, &b);
    out->file_bytes = a + b;
}

const GsEngine gs_engine_wal = {
    "wal",
    wal_open, wal_close,
    wal_begin_read, wal_begin_write, wal_commit, wal_sync, wal_abort,
    wal_tree_create, wal_tree_drop, wal_tree_list, wal_tree_replace,
    wal_get, wal_put, wal_del,
    wal_cursor_open, wal_seek, wal_first, wal_last, wal_next, wal_prev, wal_key, wal_value,
    wal_cursor_put, wal_cursor_delete, wal_cursor_close,
    wal_backup, wal_check, wal_stats,
};

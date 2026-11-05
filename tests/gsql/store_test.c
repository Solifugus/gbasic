/* The storage interface (store.h) against a model, for any candidate.
 *
 *   test_store ENGINE SEED [real DIR]
 *
 * The model is a set of named trees, each a sorted map. A write transaction
 * works on a copy; commit replaces the committed state; a read snapshot holds a
 * copy of the DURABLE state at the moment it began (§1.3: only what is durable
 * is visible). Random transactions run against both, and everything the
 * interface promises is compared:
 *   - get, cursors in every seek mode, scans both ways, the cursor that rests
 *     between its neighbours after cursor_delete;
 *   - snapshots held across many later commits, compared in full when closed;
 *   - tree_create/drop/list/replace, with a reader keeping the old contents;
 *   - INVALIDATED for a cursor whose tree changed under it, READ_ONLY for a
 *     write through a snapshot (G4), BUSY for a second writer and a second
 *     opener, MISUSE for close with a transaction open, TOO_LARGE for a key;
 *   - check after every commit, backup from a snapshot (reopened and compared),
 *     reopen, and the disk a process crash leaves (simulated layer only);
 *   - a transaction many times larger than the cache (G5).
 * Prints "ok N checks" or MISMATCH lines. */

#include "store_common.h"

/* The research candidates, when research/gsql/run.sh builds this beside them
 * (the gate builds the chosen engine alone). */
#ifdef HAVE_WAL
extern const GsEngine gs_engine_wal;
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

static unsigned checks, failures;
int quiet;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (!quiet && failures <= 25) { printf("MISMATCH "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint64_t rng;
static uint64_t rnd(void) {
    uint64_t z = (rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* A tree dropped by a commit that is not yet durable is still what a NEW
 * reader sees (§1.3), whole, until a sync or a flush makes the drop durable.
 * Its own store, with a memtable big enough that commits do not each flush:
 * that is the window. 2.C's first dead-file sweep dropped such a tree's files
 * as soon as the drop was before the last commit, and a new reader saw 78 of
 * 300 records (Test 3, 2026-10-26). */
static const GsEngine *E;
static void print_report(void *ctx, const char *line);
static void put_one(GsStore *db, const char *tree, unsigned i, int sync) {
    GsTxn *w;
    char k[32], v[64];
    E->begin_write(db, &w);
    int kn = snprintf(k, sizeof k, "k%06u", i);
    int vn = snprintf(v, sizeof v, "value-%06u-padding-padding", i);
    if (E->put(w, gss(tree), gsb(k, (size_t)kn), gsb(v, (size_t)vn), 0) == GS_NOT_FOUND) {
        E->tree_create(w, gss(tree));
        E->put(w, gss(tree), gsb(k, (size_t)kn), gsb(v, (size_t)vn), 0);
    }
    E->commit(w, sync, NULL);
}
static void dropped_not_durable(const char *base, GsqlIo *io) {
    char path[600];
    snprintf(path, sizeof path, "%s.drop", base);
    GsOptions o = {1, 512, 0, io, 2000, 2, 0, 0, NULL};
    GsStore *db;
    int rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "opening the drop store answered %s", gs_status_name(rc));
    if (rc) return;
    for (unsigned i = 0; i < 300; i++) put_one(db, "t", i, 1);
    for (unsigned i = 0; i < 400; i++) put_one(db, "f", i, 1);
    GsTxn *w;
    E->begin_write(db, &w);
    E->tree_drop(w, gss("t"));
    E->commit(w, 0, NULL);
    unsigned worst = 300, seen = 0;
    for (unsigned j = 0; j < 400; j++) {
        GsTxn *x;
        char k[8];
        E->begin_write(db, &x);
        int kn = snprintf(k, sizeof k, "z%03u", j % 3);
        E->put(x, gss("f"), gsb(k, (size_t)kn), gss("x"), 0);
        E->commit(x, 0, NULL);
        GsTxn *r;
        E->begin_read(db, &r);
        GsCursor *c;
        unsigned n = 0;
        int pos, gone = E->cursor_open(r, gss("t"), &c) != GS_OK;
        if (!gone) {
            for (rc = E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos)) n++;
            E->cursor_close(c);
        }
        E->abort(r);
        if (gone) break; /* the drop became durable: a flush, or a checkpoint */
        seen++;
        if (n < worst) worst = n;
    }
    CHECK(worst == 300, "a new reader saw %u of 300 records of a tree whose drop was not durable", worst);
    CHECK(E->sync(db, NULL) == GS_OK, "sync after the drop");
    GsTxn *r;
    GsCursor *c;
    E->begin_read(db, &r);
    CHECK(E->cursor_open(r, gss("t"), &c) == GS_NOT_FOUND, "after a sync, the dropped tree is gone");
    E->abort(r);
    CHECK(E->check(db, print_report, NULL) == GS_OK, "check after the drop");
    E->close(db);
    (void)seen;
}

/* Measured 1.00 with gc_start and 1.58 without it (the 70%-dead blob files
 * left whole). The general bound is about 2x, from GC_DEAD_PCT. */
#define BLOB_SPACE_BOUND 1.25

typedef struct { GsqlIo *io; const char *dir; uint64_t bytes; unsigned files; } BlobSum;
static int blob_sum(void *ctx, const char *name) {
    BlobSum *b = ctx;
    size_t n = strlen(name);
    if (n < 5 || strcmp(name + n - 5, ".blob")) return 0;
    char p[900];
    snprintf(p, sizeof p, "%s/%s", b->dir, name);
    GsqlFile *f;
    uint64_t sz = 0;
    if (gsql_io_open(b->io, p, GSQL_IO_READ_ONLY, &f) == 0) {
        gsql_io_size(f, &sz);
        gsql_io_close(f);
    }
    b->bytes += sz;
    b->files++;
    return 0;
}

/* Long values that die where no compaction will pass again: tree "b" is
 * loaded, 70% of it overwritten, and then only tree "s" is written, small
 * values whose key range overlaps nothing of b's. b's blob files are then
 * mostly dead and only a compaction begun for their sake gets the space
 * back -- relocating only from the oldest quarter of the blob files, and
 * only when a compaction happened by, left W8 at 2.91x its live data.
 * Nothing else checks space at all, so a trigger that never fired would pass
 * every crash tier. A reader held from the load must still read the load at
 * the end, since its version keeps the blob files the current one has
 * relocated out of. lsm only. */
static void blob_space(const char *base, GsqlIo *io) {
    if (strcmp(E->name, "lsm")) return;
    char path[600];
    snprintf(path, sizeof path, "%s.blobspace", base);
    /* a memtable that holds the end of the load and all of the overwrite, so
     * the values die inside one level-0 merge, whose relocation set was fixed
     * before they did: from then on only gc_start can reach them */
    GsOptions o = {1, 512, 0, io, 350000, 2, 64, 0, NULL};
    GsStore *db;
    int rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "opening the blob-space store answered %s", gs_status_name(rc));
    if (rc) return;
    enum { KEYS = 400, VAL = 1000, SMALL = 3000 };
    unsigned char v[VAL];
    GsTxn *held = NULL;
    for (unsigned round = 1; round <= 2 && rc == GS_OK; round++)
        for (unsigned k = 0; k < KEYS && rc == GS_OK; k++) {
            if (round == 2 && k % 10 >= 7) continue; /* 70% overwritten */
            char kb[16];
            int kn = snprintf(kb, sizeof kb, "k%04u", k);
            memset(v, 'a' + (int)round, sizeof v);
            snprintf((char *)v, 16, "%04u-%04u", round, k);
            GsTxn *w;
            E->begin_write(db, &w);
            if (round == 1 && k == 0) { E->tree_create(w, gss("b")); E->tree_create(w, gss("s")); }
            E->put(w, gss("b"), gsb(kb, (size_t)kn), gsb(v, sizeof v), 0);
            rc = E->commit(w, k % 10 == 9, NULL);
            if (round == 1 && k == KEYS - 1 && rc == GS_OK) E->begin_read(db, &held);
        }
    for (unsigned i = 0; i < SMALL && rc == GS_OK; i++) {
        char kb[16];
        int kn = snprintf(kb, sizeof kb, "s%05u", i % 500);
        GsTxn *w;
        E->begin_write(db, &w);
        E->put(w, gss("s"), gsb(kb, (size_t)kn), gss("x"), 0);
        rc = E->commit(w, i % 10 == 9, NULL);
        if (i == SMALL / 2 && held) {
            unsigned wrong = 0;
            for (unsigned k = 0; k < KEYS; k++) {
                char key[16], want[16];
                int kn2 = snprintf(key, sizeof key, "k%04u", k);
                snprintf(want, sizeof want, "%04u-%04u", 1u, k);
                GsBytes got;
                if (E->get(held, gss("b"), gsb(key, (size_t)kn2), &got) != GS_OK || got.n != VAL || memcmp(got.p, want, 9)) wrong++;
            }
            CHECK(wrong == 0, "a reader held from the load read %u of %u of its values wrong", wrong, (unsigned)KEYS);
            E->abort(held);
            held = NULL;
        }
    }
    CHECK(rc == GS_OK, "a commit answered %s", gs_status_name(rc));
    CHECK(E->sync(db, NULL) == GS_OK, "sync after the overwrites");
    unsigned wrong = 0;
    GsTxn *r;
    E->begin_read(db, &r);
    for (unsigned k = 0; k < KEYS; k++) {
        char key[16], want[16];
        int kn = snprintf(key, sizeof key, "k%04u", k);
        snprintf(want, sizeof want, "%04u-%04u", k % 10 >= 7 ? 1u : 2u, k);
        GsBytes got;
        if (E->get(r, gss("b"), gsb(key, (size_t)kn), &got) != GS_OK || got.n != VAL || memcmp(got.p, want, 9)) wrong++;
    }
    E->abort(r);
    CHECK(wrong == 0, "after the collection %u of %u values read wrong", wrong, (unsigned)KEYS);
    /* the blob files on disk, asked of the directory rather than the store */
    BlobSum bs = {io, path, 0, 0};
    CHECK(gsql_io_list(io, path, blob_sum, &bs) == 0, "listing the blob-space store");
    double amp = (double)bs.bytes / (KEYS * VAL);
    if (getenv("GS_SHOW_SPACE")) fprintf(stderr, "blob_space: %u blob files, %llu bytes, %.2fx the live values\n", bs.files, (unsigned long long)bs.bytes, amp);
    CHECK(amp <= BLOB_SPACE_BOUND, "with 70%% of its long values dead the blob files are %.2fx the live values, over %.2f", amp,
          BLOB_SPACE_BOUND);
    GsStats st;
    E->stats(db, &st);
    CHECK(st.file_bytes >= bs.bytes, "stats counts %llu file bytes, under the %llu in blob files alone",
          (unsigned long long)st.file_bytes, (unsigned long long)bs.bytes);
    CHECK(E->check(db, print_report, NULL) == GS_OK, "check after the overwrites");
    E->close(db);
}

/* More sorted files than the process may hold open, every one of them read.
 * 2.C kept a file open from its first read until it was deleted, so a store
 * of N files needed N descriptors: about 16,000 at 1 TB against a default
 * limit of 1,024 (Test 4). Here two hundred level-0 files, all overlapping,
 * and reads that touch each of them, with the store's cap at 8; in the real
 * layer the process's own limit is lowered to 40 descriptors above what it
 * already holds, so a store that ignored the cap fails to open a file. Every
 * read is checked, then a full scan, `check` (which reads every file) and a
 * reopen. */
static void many_files(const char *base, GsqlIo *io, int real) {
    char path[600];
    snprintf(path, sizeof path, "%s.many", base);
    GsOptions o = {1, 512, 0, io, 4096, 1000, 0, 8, NULL};
    struct rlimit was;
    int lowered = 0;
    if (real && getrlimit(RLIMIT_NOFILE, &was) == 0) {
        int probe = dup(0);
        if (probe >= 0) {
            close(probe);
            struct rlimit lim = was;
            lim.rlim_cur = (rlim_t)probe + 40;
            if (lim.rlim_cur < was.rlim_cur) lowered = setrlimit(RLIMIT_NOFILE, &lim) == 0;
        }
        CHECK(lowered, "could not lower the descriptor limit, so the cap was not measured");
    }
    GsStore *db;
    int rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "opening the many-file store answered %s", gs_status_name(rc));
    if (rc) goto out;
    enum { ROUNDS = 200, PER = 70, KEYS = 997 };
    unsigned *last = calloc(KEYS, sizeof *last);
    for (unsigned i = 0; i < ROUNDS * PER; i++) {
        unsigned k = (unsigned)((i * 7919u) % KEYS);
        GsTxn *w;
        char kb[32], v[64];
        int kn = snprintf(kb, sizeof kb, "k%06u", k), vn = snprintf(v, sizeof v, "value-%06u-padding-padding", i + 1);
        E->begin_write(db, &w);
        if (i == 0) E->tree_create(w, gss("m"));
        E->put(w, gss("m"), gsb(kb, (size_t)kn), gsb(v, (size_t)vn), 0);
        rc = E->commit(w, i % PER == PER - 1, NULL);
        if (rc) break;
        last[k] = i + 1;
    }
    CHECK(rc == GS_OK, "a commit among %u answered %s", ROUNDS * PER, gs_status_name(rc));
    GsStats st;
    E->stats(db, &st);
    for (int pass = 0; pass < 2; pass++) {
        unsigned wrong = 0, errs = 0;
        int first_err = GS_OK;
        GsTxn *r;
        E->begin_read(db, &r);
        for (unsigned q = 0; q < KEYS; q++) {
            GsBytes got;
            char kb[32], v[64];
            int kn = snprintf(kb, sizeof kb, "k%06u", q), vn = snprintf(v, sizeof v, "value-%06u-padding-padding", last[q]);
            int g = E->get(r, gss("m"), gsb(kb, (size_t)kn), &got);
            if (!last[q]) { if (g != GS_NOT_FOUND) wrong++; continue; }
            if (g != GS_OK) { if (!errs++) first_err = g; continue; }
            if (got.n != (size_t)vn || memcmp(got.p, v, (size_t)vn)) wrong++;
        }
        unsigned n = 0;
        GsCursor *c;
        int pos;
        if (E->cursor_open(r, gss("m"), &c) == GS_OK) {
            for (rc = E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos)) n++;
            if (rc && !errs++) first_err = rc;
            E->cursor_close(c);
        }
        E->abort(r);
        CHECK(errs == 0, "pass %d: %u reads failed, the first with %s", pass, errs, gs_status_name(first_err));
        CHECK(wrong == 0, "pass %d: %u reads returned the wrong value", pass, wrong);
        CHECK(n == KEYS, "pass %d: a scan saw %u of %u keys", pass, n, (unsigned)KEYS);
    }
    CHECK(E->check(db, print_report, NULL) == GS_OK, "check over the many files");
    E->close(db);
    rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "reopening the many-file store answered %s", gs_status_name(rc));
    if (rc == GS_OK) {
        GsTxn *r;
        unsigned wrong = 0;
        E->begin_read(db, &r);
        for (unsigned q = 0; q < KEYS; q++) {
            GsBytes got;
            char kb[32], v[64];
            int kn = snprintf(kb, sizeof kb, "k%06u", q), vn = snprintf(v, sizeof v, "value-%06u-padding-padding", last[q]);
            if (E->get(r, gss("m"), gsb(kb, (size_t)kn), &got) != GS_OK || got.n != (size_t)vn || memcmp(got.p, v, (size_t)vn)) wrong++;
        }
        E->abort(r);
        CHECK(wrong == 0, "after a reopen, %u reads went wrong", wrong);
        E->close(db);
    }
    /* With the cap lifted past the limit, reads run out of descriptors: that
     * is the I/O layer's failure, and must not be reported as a damaged store
     * (every failure to open a sorted file used to be CORRUPT). */
    if (lowered && E == &gs_engine_lsm) {
        o.create = 0;
        o.tune_d = 100000;
        rc = E->open(path, &o, &db);
        CHECK(rc == GS_OK, "reopening with no cap answered %s", gs_status_name(rc));
        if (rc == GS_OK) {
            GsTxn *r;
            int first = GS_OK;
            E->begin_read(db, &r);
            /* a scan, not gets: a get stops at the newest file holding its
             * key, and here that is never far down; a scan merges them all */
            GsCursor *c;
            int pos;
            first = E->cursor_open(r, gss("m"), &c);
            if (first == GS_OK) {
                for (first = E->first(c, &pos); first == GS_OK && pos; first = E->next(c, &pos)) {}
                E->cursor_close(c);
            }
            E->abort(r);
            CHECK(first == GS_IO, "out of descriptors, a read answered %s, not IO", gs_status_name(first));
            E->close(db);
        }
    }
    free(last);
out:
    if (lowered) setrlimit(RLIMIT_NOFILE, &was);
}

/* Many versions of a few keys, so a memtable holds several versions of one
 * key when it flushes and compaction meets them across files, with a snapshot
 * held from the start: every read is checked as it happens, the snapshot
 * must still read the first values at the end, and so must a reopen read the
 * last. The random workload above never put two versions of a key in one
 * memtable, so a flush that kept the OLDEST version passed it and the crash
 * gate alike while a direct read went stale after 25 commits (Test 3). */
static void hot_keys(const char *base, GsqlIo *io) {
    char path[600];
    snprintf(path, sizeof path, "%s.hot", base);
    GsOptions o = {1, 512, 0, io, 2000, 2, 0, 0, NULL};
    GsStore *db;
    int rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "opening the hot-key store answered %s", gs_status_name(rc));
    if (rc) return;
    enum { KEYS = 5, ROUNDS = 3000 };
    unsigned last[KEYS];
    for (unsigned k = 0; k < KEYS; k++) { put_one(db, "h", k, 1); last[k] = k; }
    GsTxn *held;
    E->begin_read(db, &held);
    unsigned stale = 0;
    for (unsigned i = 0; i < ROUNDS; i++) {
        unsigned k = (unsigned)(rnd() % KEYS), v = KEYS + i;
        GsTxn *w;
        char kb[32], vb2[64];
        int kn = snprintf(kb, sizeof kb, "k%06u", k);
        int vn = snprintf(vb2, sizeof vb2, "value-%06u-padding-padding", v);
        E->begin_write(db, &w);
        E->put(w, gss("h"), gsb(kb, (size_t)kn), gsb(vb2, (size_t)vn), 0);
        E->commit(w, i % 8 == 7, NULL); /* a group of eight per flush */
        last[k] = v;
        if (i % 8 != 7) continue;
        GsTxn *r;
        E->begin_read(db, &r);
        for (unsigned q = 0; q < KEYS; q++) {
            GsBytes got;
            kn = snprintf(kb, sizeof kb, "k%06u", q);
            vn = snprintf(vb2, sizeof vb2, "value-%06u-padding-padding", last[q]);
            if (E->get(r, gss("h"), gsb(kb, (size_t)kn), &got) != GS_OK || got.n != (size_t)vn || memcmp(got.p, vb2, (size_t)vn)) stale++;
        }
        E->abort(r);
    }
    CHECK(stale == 0, "%u reads of a hot key returned something other than its last durable value", stale);
    unsigned moved = 0;
    for (unsigned q = 0; q < KEYS; q++) {
        GsBytes got;
        char kb[32], vb2[64];
        int kn = snprintf(kb, sizeof kb, "k%06u", q), vn = snprintf(vb2, sizeof vb2, "value-%06u-padding-padding", q);
        if (E->get(held, gss("h"), gsb(kb, (size_t)kn), &got) != GS_OK || got.n != (size_t)vn || memcmp(got.p, vb2, (size_t)vn)) moved++;
    }
    CHECK(moved == 0, "a snapshot held across %u commits saw %u of its %u keys change", (unsigned)ROUNDS, moved, (unsigned)KEYS);
    E->abort(held);
    CHECK(E->check(db, print_report, NULL) == GS_OK, "check after the hot keys");
    E->close(db);
    rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "reopening the hot-key store answered %s", gs_status_name(rc));
    if (rc) return;
    GsTxn *r;
    E->begin_read(db, &r);
    unsigned wrong = 0;
    for (unsigned q = 0; q < KEYS; q++) {
        GsBytes got;
        char kb[32], vb2[64];
        int kn = snprintf(kb, sizeof kb, "k%06u", q), vn = snprintf(vb2, sizeof vb2, "value-%06u-padding-padding", last[q]);
        if (E->get(r, gss("h"), gsb(kb, (size_t)kn), &got) != GS_OK || got.n != (size_t)vn || memcmp(got.p, vb2, (size_t)vn)) wrong++;
    }
    CHECK(wrong == 0, "after reopening, %u hot keys did not read their last value", wrong);
    E->abort(r);
    E->close(db);
}

/* The sliced backup (store.h): a snapshot, then updates and a tree dropped
 * after it, then the backup in small slices with a commit between every two.
 * The copy must hold exactly the snapshot -- including the tree dropped
 * after it, which must STAY whole in the copy once the copy's own commits
 * pass the drop's sequence (2.C's logical backup copied every retirement,
 * including later ones, so the copy's compactions would have dropped a tree
 * its own catalog called live). Then an abandoned backup must leave nothing
 * that opens, and a backup into a place an earlier one used must pass check
 * (no file left that nothing names). */
static void put_gen(GsStore *db, const char *tree, unsigned i, unsigned gen, int sync) {
    GsTxn *w;
    char k[32], v[80];
    E->begin_write(db, &w);
    int kn = snprintf(k, sizeof k, "k%06u", i);
    int vn = snprintf(v, sizeof v, "g%06u-value-%06u-padding-padding-padding", gen, i);
    if (E->put(w, gss(tree), gsb(k, (size_t)kn), gsb(v, (size_t)vn), 0) == GS_NOT_FOUND) {
        E->tree_create(w, gss(tree));
        E->put(w, gss(tree), gsb(k, (size_t)kn), gsb(v, (size_t)vn), 0);
    }
    E->commit(w, sync, NULL);
}
/* how many records the tree holds, and how many of them are generation gen */
static unsigned count_gen(GsTxn *t, const char *tree, unsigned gen, unsigned *match) {
    GsCursor *c;
    unsigned n = 0;
    *match = 0;
    if (E->cursor_open(t, gss(tree), &c) != GS_OK) return 0;
    int pos;
    char want[16];
    snprintf(want, sizeof want, "g%06u-", gen);
    for (int rc = E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos)) {
        GsBytes v;
        E->value(c, &v);
        n++;
        if (v.n > 8 && memcmp(v.p, want, 8) == 0) (*match)++;
    }
    E->cursor_close(c);
    return n;
}
/* how many of the tree's keys hold exactly the generation the model says */
static unsigned match_model(GsTxn *t, const char *tree, const unsigned *gen, unsigned n) {
    unsigned ok = 0;
    for (unsigned i = 0; i < n; i++) {
        char k[32], want[16];
        int kn = snprintf(k, sizeof k, "k%06u", i);
        snprintf(want, sizeof want, "g%06u-", gen[i]);
        GsBytes v;
        if (E->get(t, gss(tree), gsb(k, (size_t)kn), &v) == GS_OK && v.n > 8 && memcmp(v.p, want, 8) == 0) ok++;
    }
    return ok;
}
/* how many keys a scan finds holding exactly the generation the model says,
 * and how many it finds at all */
static unsigned scan_model(GsTxn *t, const char *tree, const unsigned *gen, unsigned n, unsigned *seen) {
    GsCursor *c;
    unsigned ok = 0;
    *seen = 0;
    if (E->cursor_open(t, gss(tree), &c) != GS_OK) return 0;
    int pos;
    for (int rc = E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos)) {
        GsBytes k, v;
        E->key(c, &k);
        E->value(c, &v);
        (*seen)++;
        unsigned i;
        char want[16], kz[8];
        if (k.n != 7) continue;
        /* a key is counted bytes, not a C string: sscanf on it ran strlen
         * past its seven bytes (valgrind, uninitialised reads) */
        memcpy(kz, k.p, 7);
        kz[7] = 0;
        if (sscanf(kz + 1, "%6u", &i) != 1 || i >= n) continue;
        snprintf(want, sizeof want, "g%06u-", gen[i]);
        if (v.n > 8 && memcmp(v.p, want, 8) == 0) ok++;
    }
    E->cursor_close(c);
    return ok;
}

/* Reads while a frozen memtable is being flushed. 2.C freezes a full
 * memtable and writes it out a slice per commit; meanwhile it is read
 * between the live memtable and level 0, by point reads, cursors and the
 * backup. The crash gate never sees one: it reads only after recovery,
 * which has none. With a 3,000-byte memtable and every commit synced, a
 * freeze comes every few dozen commits and its flush takes about half as
 * many again, so about half of these reads run with one present. Every key
 * is read back every ten commits and scanned every fifty, and a backup is
 * taken every 97 commits and compared with the model as it stood. Level 0
 * is left to grow (a trigger of 1,000), so no compaction moves the data. */
/* A commit a crash lost can leave a later one on disk behind it: power loss
 * keeps any subset of the unsynced writes. Recovery stops at the gap, and the
 * next commit lands in it. If that commit is byte for byte the one lost --
 * the same sequence, the same contents, which is exactly what a program that
 * retries after a crash writes -- its checksum is the lost one's too, and the
 * record behind it chains to it. The value drawn at every open is the only
 * thing that tells them apart, and the crash gate never builds this case
 * (measured: with that value constant every tier stays green). So it is built
 * here, on the log's bytes: three commits, the middle one's record zeroed. Its
 * twin is committed after the reopen; the third must not come back. lsm only. */
static int put_kv(GsStore *db, const char *k, const char *v, int create, int sync) {
    GsTxn *w;
    int rc = E->begin_write(db, &w);
    if (rc) return rc;
    if (create) E->tree_create(w, gss("t"));
    E->put(w, gss("t"), gss(k), gss(v), 0);
    return E->commit(w, sync, NULL);
}

static int has_key(GsStore *db, const char *k) {
    GsTxn *r;
    GsBytes got;
    E->begin_read(db, &r);
    int rc = E->get(r, gss("t"), gss(k), &got);
    E->abort(r);
    return rc == GS_OK;
}

/* `flush` (step 5): a planned handoff that replays nothing. Asserted as a
 * DIFFERENCE, since "nothing was replayed" alone is satisfied by an open that
 * never replays and loses the memtable: the same commits closed plainly must
 * replay, and both stores must hold every commit afterwards, including the
 * unsynced ones close or flush made durable, and go on taking commits that
 * survive a further reopen. */
static int count_keys(GsStore *db) {
    GsTxn *r;
    GsCursor *c;
    int pos, n = 0;
    if (E->begin_read(db, &r)) return -1;
    if (E->cursor_open(r, gss("t"), &c) == GS_OK) {
        for (int rc = E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos)) n++;
        E->cursor_close(c);
    }
    E->abort(r);
    return n;
}
/* Power losses of the store's disk at this moment, several seeds: each must
 * open, pass check, and hold `want` keys. */
static void power_check(GsqlIo *io, const char *path, int want, const char *when) {
    for (uint64_t seed = 1; seed <= 6; seed++) {
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, seed);
        GsOptions o = {0, 512, 0, img, 1 << 20, 0, 0, 0, NULL};
        GsStore *db;
        int rc = E->open(path, &o, &db);
        CHECK(rc == GS_OK, "%s, power loss %llu: open answered %s", when, (unsigned long long)seed, gs_status_name(rc));
        if (rc == GS_OK) {
            int n = count_keys(db);
            CHECK(n == want, "%s, power loss %llu: %d of %d commits", when, (unsigned long long)seed, n, want);
            CHECK(E->check(db, print_report, NULL) == GS_OK, "%s, power loss %llu: check", when, (unsigned long long)seed);
            E->close(db);
        }
        gsql_io_free(img);
    }
}
/* CRC32C: whatever crc32c uses (the processor's instruction where it has
 * one) against the portable table-driven version, at every alignment and
 * many lengths, and both against the standard check value. Every checksum
 * on disk goes through this, so a wrong fast path would make every store
 * unreadable by a build without it -- or worse, readable and agreeing only
 * with itself. */
static void crc_agreement(void) {
    CHECK(crc32c("123456789", 9) == 0xE3069283u, "CRC32C(\"123456789\") is %08x, want e3069283", crc32c("123456789", 9));
    CHECK(crc32c_extend_portable(0, "123456789", 9) == 0xE3069283u, "the portable CRC32C of the check string");
    unsigned char buf[4200];
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = (unsigned char)rnd();
    unsigned bad = 0, n = 0;
    for (size_t off = 0; off < 16; off++)
        for (size_t len = 0; len + off <= sizeof buf; len += 1 + len / 3) {
            uint32_t seed = (uint32_t)rnd();
            if (crc32c_extend(seed, buf + off, len) != crc32c_extend_portable(seed, buf + off, len)) bad++;
            n++;
        }
    CHECK(bad == 0, "CRC32C: %u of %u lengths and alignments disagree with the portable version", bad, n);
}
/* An EMPTY database's size on disk, at the default memtable. Its logs were
 * zero-filled to twice the memtable when made, so a database holding ten rows
 * took 134 MB (2026-10-29), which one owner per tenant multiplies by the
 * tenants. The zeros now grow ahead of the log a step at a time. Correctness
 * never rested on them -- replay stops at the end of a file as well as at
 * zeros -- so this check, and the latency the prefill buys, are all that can
 * see the prefill go wrong. */
static void empty_footprint(const char *base, GsqlIo *io) {
    if (strcmp(E->name, "lsm")) return;
    char path[600], k[16];
    snprintf(path, sizeof path, "%s.small", base);
    GsOptions o = {1, 0, 0, io, 0, 0, 0, 0, NULL}; /* every size at its default */
    GsStore *db;
    int rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "opening the small store answered %s", gs_status_name(rc));
    if (rc) return;
    for (int i = 0; i < 10 && rc == GS_OK; i++) {
        snprintf(k, sizeof k, "k%d", i);
        rc = put_kv(db, k, "v", i == 0, 1);
    }
    if (E->flush) E->flush(db);
    E->close(db);
    o.create = 0;
    rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "reopening the small store answered %s", gs_status_name(rc));
    if (rc) return;
    GsStats st;
    E->stats(db, &st);
    CHECK(st.file_bytes < (4u << 20), "a ten-row database takes %llu bytes, over 4 MB", (unsigned long long)st.file_bytes);
    CHECK(count_keys(db) == 10, "the small store holds %d of 10 rows", count_keys(db));
    E->close(db);
}
/* A sorted load into a new tree beside a large one: the transaction spills
 * runs, and its commit must not rewrite them, or the tree beside them, to
 * put them in place (gsql_spill_design.md §7). Level 0 was compacted as one
 * block, whose span ran from the catalog's tree to the new one and so
 * across every file of the tree between: a CREATE INDEX at 10^7 rows
 * merged 715 MB in its commit. Asserted on the bytes the commit writes,
 * against what the load itself wrote. */
/* The planner's estimate of a tree's entries (docs/gsql_rowcount_design.md §2)
 * against the true count: in the memtable alone, and once a small memtable
 * has flushed the three interleaved trees into sorted files that each hold
 * all three, and after a reopen. Within a factor of two each time, and the
 * smallest tree estimated below the largest, which is what the planner asks. */
static void check_estimates(GsStore *db, const char *when, const unsigned *want) {
    static const char *const names[3] = { "ea", "eb", "ec" };
    GsTxn *r;
    if (E->begin_read(db, &r) != GS_OK) return;
    uint64_t got[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++) {
        int rc = E->estimate(r, gss(names[k]), &got[k]);
        CHECK(rc == GS_OK, "estimate of %s %s answered %s", names[k], when, gs_status_name(rc));
        CHECK(got[k] * 2 >= want[k] && got[k] <= 2 * (uint64_t)want[k],
              "estimate of %s %s: %llu for %u entries", names[k], when, (unsigned long long)got[k], want[k]);
    }
    CHECK(got[1] < got[0] && got[0] < got[2], "estimates %s keep the trees' order: %llu, %llu, %llu", when,
          (unsigned long long)got[0], (unsigned long long)got[1], (unsigned long long)got[2]);
    GsBytes v;
    (void)v;
    uint64_t none = 7;
    CHECK(E->estimate(r, gss("no-such-tree"), &none) == GS_NOT_FOUND, "an estimate of a missing tree is NOT_FOUND");
    E->abort(r);
}

static void estimates(const char *base, GsqlIo *io) {
    static const unsigned want[3] = { 400, 40, 2000 };
    for (int flushing = 0; flushing < 2; flushing++) {
        char path[600];
        snprintf(path, sizeof path, "%s.est%d", base, flushing);
        /* a 4 KB memtable flushes a file every few commits; a 1 MB one never */
        GsOptions o = {1, 512, 0, io, flushing ? 4096 : (1 << 20), 4, 0, 0, NULL};
        GsStore *db;
        int rc = E->open(path, &o, &db);
        CHECK(rc == GS_OK, "opening the estimate store answered %s", gs_status_name(rc));
        if (rc) return;
        unsigned done[3] = { 0, 0, 0 };
        static const char *const names[3] = { "ea", "eb", "ec" };
        while (done[0] < want[0] || done[1] < want[1] || done[2] < want[2]) {
            GsTxn *w;
            if (E->begin_write(db, &w) != GS_OK) break;
            for (int k = 0; k < 3; k++) {
                if (!done[k]) E->tree_create(w, gss(names[k]));
                /* the trees interleaved, so a flushed file holds all three */
                for (unsigned i = 0; i < 10 && done[k] < want[k]; i++, done[k]++) {
                    char key[16], val[32];
                    int kn = snprintf(key, sizeof key, "%06u", done[k]);
                    int vn = snprintf(val, sizeof val, "v%u", done[k]);
                    E->put(w, gss(names[k]), gsb(key, (size_t)kn), gsb(val, (size_t)vn), 0);
                }
            }
            E->commit(w, 0, NULL);
        }
        /* a reader sees what is durable */
        CHECK(E->sync(db, NULL) == GS_OK, "sync before the estimate");
        check_estimates(db, flushing ? "across sorted files" : "in the memtable alone", want);
        if (flushing) {
            CHECK(E->flush(db) == GS_OK, "flush before the estimate");
            check_estimates(db, "after a flush", want);
        }
        E->close(db);
        rc = E->open(path, &o, &db);
        CHECK(rc == GS_OK, "reopening the estimate store answered %s", gs_status_name(rc));
        if (rc) return;
        check_estimates(db, flushing ? "after a reopen, from files" : "after a reopen, replayed", want);
        E->close(db);
    }
}

static void sorted_load(const char *base, GsqlIo *io) {
    if (strcmp(E->name, "lsm")) return;
    char path[600], k[32], v[48];
    snprintf(path, sizeof path, "%s.sorted", base);
    /* a 64 KB memtable, a write buffer that spills past 128 KB, and level 0
       compacted at two files, so the first tree reaches level 1 */
    GsOptions o = {1, 512, 256 * 1024, io, 64 * 1024, 2, 0, 0, NULL};
    GsStore *db;
    int rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "opening the sorted-load store answered %s", gs_status_name(rc));
    if (rc) return;
    GsTxn *w;
    for (int b = 0; b < 40 && rc == GS_OK; b++) {
        rc = E->begin_write(db, &w);
        if (rc) break;
        if (b == 0) {
            /* made first, so its keys sort below the rows' */
            E->tree_create(w, gss("meta"));
            E->tree_create(w, gss("rows"));
        }
        for (int i = 0; i < 500; i++) {
            int n = b * 500 + i;
            /* in key order, so it leaves no compaction owed: what the
               commit below writes is then its own work, not a backlog */
            int kn = snprintf(k, sizeof k, "r%08d", n);
            int vn = snprintf(v, sizeof v, "row-%06d-padding-padding-padding", n);
            E->put(w, gss("rows"), gsb(k, (size_t)kn), gsb(v, (size_t)vn), 0);
        }
        rc = E->commit(w, 1, NULL);
    }
    if (E->flush) E->flush(db);
    GsStats before, mid, after;
    E->stats(db, &before);
    rc = E->begin_write(db, &w);
    CHECK(rc == GS_OK, "the load's transaction did not begin: %s", gs_status_name(rc));
    if (rc) { E->close(db); return; }
    E->tree_create(w, gss("sorted"));
    for (int i = 0; i < 20000; i++) {
        int kn = snprintf(k, sizeof k, "s%08d", i);
        int vn = snprintf(v, sizeof v, "entry-%06d-padding", i);
        E->put(w, gss("sorted"), gsb(k, (size_t)kn), gsb(v, (size_t)vn), 0);
    }
    /* and one entry in a tree below the rows, as CREATE INDEX writes its
       schema last: a run holding it and the load's tail spans the rows */
    E->put(w, gss("meta"), gss("schema"), gss("v2"), 0);
    E->stats(db, &mid);
    rc = E->commit(w, 1, NULL);
    CHECK(rc == GS_OK, "the sorted load's commit answered %s", gs_status_name(rc));
    E->stats(db, &after);
    uint64_t load = mid.bytes_written - before.bytes_written, commit = after.bytes_written - mid.bytes_written;
    CHECK(load > 400 * 1024, "the load wrote only %llu bytes before its commit: it never spilled", (unsigned long long)load);
    CHECK(commit < load / 4, "the sorted load's commit wrote %llu bytes against the %llu the load spilled: it rewrote "
          "the runs, or the tree beside them, to put them in place", (unsigned long long)commit, (unsigned long long)load);
    GsTxn *r;
    GsCursor *c;
    int pos, n = 0;
    E->begin_read(db, &r);
    {
        /* A key sorting below a level's files is in none of them, and a
         * level >= 1's files do not overlap, so a lookup asks at most one
         * file per level. The walk went on through the whole level instead,
         * rejecting each on its range: a point read's catalog lookups did
         * that at every level, 937 rejections a read at 10^8 rows
         * (gsql_profile_design.md). Counted on the engine's own stats. */
        GsStats a, b;
        GsBytes got;
        E->stats(db, &a);
        /* a key that is not there, so the lookup asks every level: `schema`
         * itself is found in level 0 and never reaches them */
        int grc = E->get(r, gss("meta"), gss("absent"), &got);
        E->stats(db, &b);
        CHECK(grc == GS_NOT_FOUND, "a missing meta key answered %s", gs_status_name(grc));
        if (getenv("STORE_TEST_SKIPS")) fprintf(stderr, "skips %llu\n", (unsigned long long)(b.lookup_range_skips - a.lookup_range_skips));
        uint64_t skips = b.lookup_range_skips - a.lookup_range_skips;
        /* a get is two lookups (its tree's name in the catalog, then the
         * key); measured 2 skips between them with the walk stopping, 42
         * with it walking on, so the bound sits between with room */
        CHECK(skips <= 8, "a get below every row skipped %llu files on range: the level walk did not stop at "
              "the one file the search found", (unsigned long long)skips);
    }
    if (E->cursor_open(r, gss("sorted"), &c) == GS_OK) {
        for (int st = E->first(c, &pos); st == GS_OK && pos; st = E->next(c, &pos)) n++;
        E->cursor_close(c);
    }
    E->abort(r);
    CHECK(n == 20000, "the sorted tree holds %d of 20000 entries", n);
    E->close(db);
}

static int real_fs;
static void flushed_handoff(const char *base, GsqlIo *io) {
    if (!E->flush) return;
    int real = real_fs;
    for (int flushed = 0; flushed < 2; flushed++) {
        char path[600], k[16];
        snprintf(path, sizeof path, "%s.handoff%d", base, flushed);
        /* a memtable large enough that nothing flushes on its own */
        GsOptions o = {1, 512, 0, io, 1 << 20, 0, 0, 0, NULL};
        GsStore *db;
        int rc = E->open(path, &o, &db);
        CHECK(rc == GS_OK, "opening the handoff store answered %s", gs_status_name(rc));
        if (rc) return;
        for (int i = 0; i < 250 && rc == GS_OK; i++) {
            snprintf(k, sizeof k, "k%04d", i);
            rc = put_kv(db, k, "value", i == 0, i < 200);
        }
        CHECK(rc == GS_OK, "the handoff commits answered %s", gs_status_name(rc));
        if (flushed) {
            GsTxn *w;
            E->begin_write(db, &w);
            CHECK(E->flush(db) == GS_MISUSE, "flush is refused while a write transaction is open");
            E->abort(w);
            CHECK(E->flush(db) == GS_OK, "flush");
            CHECK(E->flush(db) == GS_OK, "a second flush, with nothing to move");
            /* With 250 small commits the spare log is not yet filled, so the
             * flush freezes into an EMPTY one (spare_now), the one path the
             * crash gate's workloads never reach: their spare is always ready
             * by the time they flush (measured with a probe). The flush then
             * moves its writing to the frozen log it recycled at full size,
             * so the commits after it land there. Power losses are built here,
             * after the flush and again after commits made after it, and every
             * durable commit must survive: with that log's name not made
             * durable first, 25 of 275 were lost. */
            if (!real) {
                for (int i = 250; i < 300 && rc == GS_OK; i++) {
                    if (i == 275) power_check(io, path, 275, "after the flush and 25 commits");
                    snprintf(k, sizeof k, "k%04d", i);
                    rc = put_kv(db, k, "value", 0, 1);
                }
                power_check(io, path, 300, "after the flush and 50 commits");
                CHECK(E->flush(db) == GS_OK, "flush again");
            }
        }
        int total = flushed && !real ? 300 : 250;
        CHECK(E->close(db) == GS_OK, "close the handoff store");
        rc = E->open(path, &o, &db);
        CHECK(rc == GS_OK, "reopening the handoff store answered %s", gs_status_name(rc));
        if (rc) return;
        GsStats st;
        E->stats(db, &st);
        if (flushed) CHECK(st.replayed == 0, "after flush the open replayed %llu records", (unsigned long long)st.replayed);
        else CHECK(st.replayed >= 250, "after a plain close the open replayed %llu records, want at least 250",
                   (unsigned long long)st.replayed);
        CHECK(count_keys(db) == total, "%s: %d of %d commits after the reopen", flushed ? "flushed" : "plain", count_keys(db), total);
        rc = put_kv(db, "after", "x", 0, 1);
        CHECK(rc == GS_OK, "a commit after the reopen answered %s", gs_status_name(rc));
        CHECK(E->check(db, print_report, NULL) == GS_OK, "check after the handoff");
        E->close(db);
        rc = E->open(path, &o, &db);
        CHECK(rc == GS_OK && count_keys(db) == total + 1 && has_key(db, "after"), "%s: the commit after the handoff survives",
              flushed ? "flushed" : "plain");
        if (rc == GS_OK) E->close(db);
    }
}

static void lost_twin(const char *base, GsqlIo *io) {
    if (strcmp(E->name, "lsm")) return;
    char path[600], logp[700];
    snprintf(path, sizeof path, "%s.twin", base);
    snprintf(logp, sizeof logp, "%s/db.log.1", path);
    GsOptions o = {1, 512, 0, io, 0, 0, 0, 0, NULL};
    GsStore *db;
    int rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "opening the twin store answered %s", gs_status_name(rc));
    if (rc) return;
    rc = put_kv(db, "a", "1", 1, 1);
    if (rc == GS_OK) rc = put_kv(db, "b", "2", 0, 0);
    if (rc == GS_OK) rc = put_kv(db, "c", "3", 0, 0);
    CHECK(rc == GS_OK, "the three commits answered %s", gs_status_name(rc));
    E->close(db);
    /* the second record's bytes, lost; the third's kept */
    GsqlFile *f;
    CHECK(gsql_io_open(io, logp, 0, &f) == 0, "opening %s", logp);
    uint64_t at = 0;
    for (int i = 0; i < 2; i++) {
        unsigned char h[4];
        size_t got = 0;
        gsql_io_read_at(f, at, h, 4, &got);
        uint32_t len = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
        if (i == 1) {
            unsigned char z[4096] = {0};
            CHECK(len && len + 8 <= sizeof z, "the second record is %u bytes", len);
            gsql_io_write_at(f, at, z, len + 8);
        }
        at += 8 + len;
    }
    gsql_io_sync(f);
    gsql_io_close(f);
    o.create = 0;
    rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "reopening the twin store answered %s", gs_status_name(rc));
    if (rc) return;
    CHECK(has_key(db, "a") && !has_key(db, "b") && !has_key(db, "c"), "after the loss only the first commit stands");
    CHECK(put_kv(db, "b", "2", 0, 1) == GS_OK, "the lost commit's twin");
    E->close(db);
    rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "the last reopen answered %s", gs_status_name(rc));
    if (rc) return;
    CHECK(has_key(db, "a") && has_key(db, "b"), "the first commit and the twin stand");
    CHECK(!has_key(db, "c"), "a commit lost before the reopen came back behind its predecessor's twin");
    E->close(db);
}

static void frozen_reads(const char *base, GsqlIo *io) {
    char path[600];
    snprintf(path, sizeof path, "%s.frozen", base);
    GsOptions o = {1, 512, 0, io, 3000, 1000, 0, 0, NULL};
    GsStore *db;
    int rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "opening the frozen-read store answered %s", gs_status_name(rc));
    if (rc) return;
    enum { KEYS = 120, COMMITS = 1200 };
    unsigned gen[KEYS] = {0}, wrong = 0, scans = 0, bad_scans = 0, backups = 0, bad_backups = 0;
    for (unsigned c = 1; c <= COMMITS; c++) {
        unsigned k = (c * 37u) % KEYS;
        put_gen(db, "f", k, c, 1);
        gen[k] = c;
        GsTxn *r;
        if (c % 10 == 0) {
            E->begin_read(db, &r);
            unsigned live = 0;
            for (unsigned i = 0; i < KEYS; i++) live += gen[i] != 0;
            unsigned ok = match_model(r, "f", gen, KEYS);
            if (ok != live) wrong++;
            if (c % 50 == 0) {
                unsigned seen, sok = scan_model(r, "f", gen, KEYS, &seen);
                scans++;
                if (seen != live || sok != live) bad_scans++;
            }
            E->abort(r);
        }
        if (c % 97 == 0) {
            char dest[700];
            snprintf(dest, sizeof dest, "%s.frozen-copy%u", base, c);
            E->begin_read(db, &r);
            rc = E->backup(r, dest);
            E->abort(r);
            backups++;
            GsOptions bo = o;
            bo.create = 0;
            GsStore *b = NULL;
            if (rc == GS_OK) rc = E->open(dest, &bo, &b);
            if (rc == GS_OK) {
                E->begin_read(b, &r);
                unsigned live = 0;
                for (unsigned i = 0; i < KEYS; i++) live += gen[i] != 0;
                unsigned seen, sok = scan_model(r, "f", gen, KEYS, &seen);
                if (seen != live || sok != live || match_model(r, "f", gen, KEYS) != live) bad_backups++;
                E->abort(r);
                if (E->check(b, print_report, NULL) != GS_OK) bad_backups++;
                E->close(b);
            } else bad_backups++;
        }
    }
    CHECK(wrong == 0, "%u of %u rounds of point reads disagreed with the model", wrong, (unsigned)COMMITS / 10);
    CHECK(bad_scans == 0, "%u of %u scans disagreed with the model", bad_scans, scans);
    CHECK(bad_backups == 0, "%u of %u backups disagreed with the model", bad_backups, backups);
    CHECK(E->check(db, print_report, NULL) == GS_OK, "check after the frozen reads");
    E->close(db);
}

static void sliced_backup(const char *base, GsqlIo *io) {
    unsigned ga[300];
    for (unsigned i = 0; i < 300; i++) ga[i] = 1;
    char path[600], dest[600], dest2[600];
    snprintf(path, sizeof path, "%s.bk", base);
    snprintf(dest, sizeof dest, "%s.bk-copy", base);
    snprintf(dest2, sizeof dest2, "%s.bk-copy2", base);
    GsOptions o = {1, 512, 0, io, 2000, 2, 32, 0, NULL};
    GsStore *db;
    int rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "opening the backup store answered %s", gs_status_name(rc));
    if (rc) return;
    for (unsigned i = 0; i < 300; i++) put_gen(db, "a", i, 1, i % 16 == 15);
    for (unsigned i = 0; i < 200; i++) put_gen(db, "x", i, 1, i % 16 == 15);
    E->sync(db, NULL);
    GsTxn *snap;
    E->begin_read(db, &snap);
    for (unsigned i = 0; i < 150; i++) { put_gen(db, "a", i, 2, i % 8 == 7); ga[i] = 2; }
    GsTxn *w;
    E->begin_write(db, &w);
    E->tree_drop(w, gss("x"));
    E->commit(w, 1, NULL);
    GsBackup *bk = NULL;
    rc = E->backup_begin(snap, dest, &bk);
    CHECK(rc == GS_OK, "backup_begin answered %s", gs_status_name(rc));
    int done = 0;
    unsigned steps = 0;
    while (rc == GS_OK && !done && steps < 100000) {
        rc = E->backup_step(bk, 4096, &done);
        steps++;
        put_gen(db, "a", steps % 300, 3, steps % 8 == 0); /* writers carry on between slices */
        ga[steps % 300] = 3;
    }
    CHECK(rc == GS_OK && done, "the sliced backup finished (%s after %u steps)", gs_status_name(rc), steps);
    E->backup_end(bk);
    E->abort(snap);
    GsOptions bo = o;
    bo.create = 0;
    GsStore *b;
    rc = E->open(dest, &bo, &b);
    CHECK(rc == GS_OK, "opening the sliced backup answered %s", gs_status_name(rc));
    if (rc == GS_OK) {
        CHECK(E->check(b, print_report, NULL) == GS_OK, "the sliced backup passes check");
        GsTxn *r;
        unsigned m, n;
        E->begin_read(b, &r);
        n = count_gen(r, "a", 1, &m);
        CHECK(n == 300 && m == 300, "the backup's tree a: %u records, %u of the snapshot's", n, m);
        n = count_gen(r, "x", 1, &m);
        CHECK(n == 200 && m == 200, "the backup's tree x (dropped after the snapshot): %u records, %u of them", n, m);
        E->abort(r);
        /* the copy lives on: enough commits to pass the source's drop */
        for (unsigned i = 0; i < 1500; i++) put_gen(b, "f", i % 50, 9, 1);
        E->begin_read(b, &r);
        n = count_gen(r, "x", 1, &m);
        CHECK(n == 200 && m == 200, "after 1,500 commits in the copy, its tree x holds %u records, %u of them", n, m);
        E->abort(r);
        CHECK(E->check(b, print_report, NULL) == GS_OK, "the copy passes check after its own commits");
        E->close(b);
    }
    /* abandoned part way: nothing at dest2 opens. A few unsynced updates
     * first, so the snapshot's memtable holds versions newer than it */
    E->sync(db, NULL);
    unsigned gs2[300];
    memcpy(gs2, ga, sizeof gs2);
    GsTxn *s2;
    E->begin_read(db, &s2);
    for (unsigned i = 0; i < 4; i++) put_gen(db, "a", i, 4, 0);
    bk = NULL;
    rc = E->backup_begin(s2, dest2, &bk);
    for (int i = 0; rc == GS_OK && i < 3; i++) rc = E->backup_step(bk, 4096, &done);
    E->backup_end(bk);
    GsStore *gone = NULL;
    rc = E->open(dest2, &bo, &gone);
    if (rc == GS_OK && gone) E->close(gone);
    CHECK(rc != GS_OK || done, "an abandoned backup left a store that opens");
    /* and into both places again, over what was there */
    rc = E->backup(s2, dest2);
    CHECK(rc == GS_OK, "a backup over an abandoned one answered %s", gs_status_name(rc));
    rc = E->backup(s2, dest);
    CHECK(rc == GS_OK, "a backup over a finished one answered %s", gs_status_name(rc));
    E->abort(s2);
    const char *places[2] = {dest, dest2};
    for (int p = 0; p < 2; p++) {
        rc = E->open(places[p], &bo, &b);
        CHECK(rc == GS_OK, "reopening backup %d answered %s", p, gs_status_name(rc));
        if (rc) continue;
        CHECK(E->check(b, print_report, NULL) == GS_OK, "a backup made over another passes check (%d)", p);
        GsTxn *r;
        unsigned m;
        E->begin_read(b, &r);
        (void)m;
        unsigned ok = match_model(r, "a", gs2, 300);
        CHECK(ok == 300, "backup %d: %u of tree a's 300 keys hold the snapshot's version", p, ok);
        CHECK(E->cursor_open(r, gss("x"), &(GsCursor *){NULL}) == GS_NOT_FOUND, "backup %d no longer holds tree x", p);
        E->abort(r);
        E->close(b);
    }
    E->close(db);
}

/* Refreshing a copy (docs/gsql_backup_design.md): a backup into an existing
 * copy of the same database keeps the files it already holds and copies the
 * rest. Three claims, each of which a plausible implementation gets wrong:
 * the refresh copies LESS than a fresh backup and still holds exactly the
 * snapshot; every numbered file in a copy is either one the source has or
 * from the range the source never allocates (the unflushed changes' file,
 * which under the source's next number a later refresh would keep in place
 * of the source's own, different file of that number); and a copy that an
 * owner holds -- the live database itself included -- is refused BUSY. */
typedef struct { GsqlIo *io; const char *srcdir; unsigned bad; unsigned seen; } NumScan;
static int num_one(void *ctx, const char *name) {
    NumScan *ns = ctx;
    const char *dot = strchr(name, '.');
    unsigned long long num;
    char ext[8];
    if (!dot || sscanf(dot, ".%llu.%7s", &num, ext) != 2) return 0;
    ns->seen++;
    if (num >= (1ull << 62)) return 0;
    char p[700];
    snprintf(p, sizeof p, "%s/%s", ns->srcdir, name);
    GsqlFile *f = NULL;
    if (gsql_io_open(ns->io, p, GSQL_IO_READ_ONLY, &f)) {
        ns->bad++;
        printf("    the copy holds %s, which the source does not\n", name);
        return 0;
    }
    gsql_io_close(f);
    return 0;
}
static unsigned foreign_numbers(GsqlIo *io, const char *copy, const char *src, unsigned *seen) {
    NumScan ns = { io, src, 0, 0 };
    gsql_io_list(io, copy, num_one, &ns);
    *seen = ns.seen;
    return ns.bad;
}
static void refreshed_backup(const char *base, GsqlIo *io) {
    char path[600], dest[600], busy[600];
    snprintf(path, sizeof path, "%s.rf", base);
    snprintf(dest, sizeof dest, "%s.rf-copy", base);
    snprintf(busy, sizeof busy, "%s.rf-busy", base);
    GsOptions o = {1, 512, 0, io, 2000, 1000, 32, 0, NULL};
    GsStore *db;
    if (E->open(path, &o, &db) != GS_OK) {
        CHECK(0, "opening the refresh store");
        return;
    }
    unsigned gen[400];
    for (unsigned i = 0; i < 400; i++) { put_gen(db, "a", i, 1, i % 16 == 15); gen[i] = 1; }
    /* unflushed changes, so the backup writes a memtable file */
    for (unsigned i = 0; i < 5; i++) { put_gen(db, "a", i, 2, 0); gen[i] = 2; }
    GsTxn *snap;
    GsBackup *bk = NULL;
    uint64_t copied1, files1, reused1, copied2, files2, reused2;
    E->begin_read(db, &snap);
    int rc = E->backup_begin(snap, dest, &bk);
    int done = 0;
    while (rc == GS_OK && !done) rc = E->backup_step(bk, 8192, &done);
    E->backup_progress(bk, &copied1, &files1, &reused1);
    E->backup_end(bk);
    E->abort(snap);
    CHECK(rc == GS_OK && done, "the first backup answered %s", gs_status_name(rc));
    CHECK(reused1 == 0 && copied1 > 0, "a first backup reuses nothing: copied %llu, reused %llu of %llu files",
          (unsigned long long)copied1, (unsigned long long)reused1, (unsigned long long)files1);
    unsigned seen;
    CHECK(foreign_numbers(io, dest, path, &seen) == 0, "every numbered file of the copy is the source's, or reserved");
    CHECK(seen > 0, "the copy's files were looked at (%u)", seen);
    /* the source moves on: its next files take the numbers after the snapshot */
    for (unsigned i = 0; i < 400; i += 7) { put_gen(db, "a", i, 3, i % 4 == 0); gen[i] = 3; }
    E->sync(db, NULL);
    E->begin_read(db, &snap);
    bk = NULL;
    rc = E->backup_begin(snap, dest, &bk);
    done = 0;
    while (rc == GS_OK && !done) rc = E->backup_step(bk, 8192, &done);
    E->backup_progress(bk, &copied2, &files2, &reused2);
    E->backup_end(bk);
    E->abort(snap);
    CHECK(rc == GS_OK && done, "the refresh answered %s", gs_status_name(rc));
    CHECK(reused2 > 0, "the refresh kept files the copy already held: %llu of %llu",
          (unsigned long long)reused2, (unsigned long long)files2);
    CHECK(copied2 < copied1 + copied1 / 2, "the refresh copied %llu bytes against the first's %llu",
          (unsigned long long)copied2, (unsigned long long)copied1);
    CHECK(foreign_numbers(io, dest, path, &seen) == 0, "after the refresh, too, every numbered file is the source's, or reserved");
    GsOptions bo = o;
    bo.create = 0;
    GsStore *b;
    rc = E->open(dest, &bo, &b);
    CHECK(rc == GS_OK, "opening the refreshed copy answered %s", gs_status_name(rc));
    if (rc == GS_OK) {
        CHECK(E->check(b, print_report, NULL) == GS_OK, "the refreshed copy passes check");
        GsTxn *r;
        E->begin_read(b, &r);
        unsigned ok = match_model(r, "a", gen, 400);
        CHECK(ok == 400, "the refreshed copy: %u of 400 keys hold the snapshot's version", ok);
        E->abort(r);
        /* held open: a backup into it is refused, and nothing in it changes */
        E->begin_read(db, &snap);
        bk = NULL;
        rc = E->backup_begin(snap, dest, &bk);
        CHECK(rc == GS_BUSY, "a backup into a copy that is open answered %s, not BUSY", gs_status_name(rc));
        E->backup_end(bk);
        E->begin_read(b, &r);
        ok = match_model(r, "a", gen, 400);
        CHECK(ok == 400, "the open copy still holds the snapshot after the refused backup: %u of 400", ok);
        E->abort(r);
        E->abort(snap);
        E->close(b);
    }
    /* the live database itself as the destination */
    E->begin_read(db, &snap);
    bk = NULL;
    rc = E->backup_begin(snap, path, &bk);
    CHECK(rc == GS_BUSY, "a backup into the live database answered %s, not BUSY", gs_status_name(rc));
    E->backup_end(bk);
    E->abort(snap);
    E->close(db);
    /* untouched, asked of a REOPEN: the process that is open reads through
       handles it already holds, so damage to its files shows only here */
    rc = E->open(path, &bo, &db);
    CHECK(rc == GS_OK, "reopening the live database after the refused backup answered %s", gs_status_name(rc));
    if (rc == GS_OK) {
        GsTxn *r;
        E->begin_read(db, &r);
        CHECK(match_model(r, "a", gen, 400) == 400, "the live database is untouched by the refused backup");
        E->abort(r);
        E->close(db);
    }

    /* Another database's file of the same number and the same size is not
     * this one's: two stores given identical writes have files that match
     * in both, and differ only in the identity their headers name. A backup
     * of one into a copy of the other must copy, not keep, or the copy holds
     * the wrong database's data under this one's manifest. */
    char px[600], py[600];
    snprintf(px, sizeof px, "%s.twin-x", base);
    snprintf(py, sizeof py, "%s.twin-y", base);
    GsStore *dx, *dy;
    int okx = E->open(px, &o, &dx) == GS_OK, oky = E->open(py, &o, &dy) == GS_OK;
    CHECK(okx && oky, "opening the twin stores");
    if (okx && oky) {
        for (unsigned i = 0; i < 400; i++) {
            put_gen(dx, "a", i, 5, i % 16 == 15);
            put_gen(dy, "a", i, 6, i % 16 == 15);
        }
        E->sync(dx, NULL);
        E->sync(dy, NULL);
        GsTxn *sx, *sy;
        E->begin_read(dx, &sx);
        rc = E->backup(sx, busy);
        E->abort(sx);
        CHECK(rc == GS_OK, "backing up twin x answered %s", gs_status_name(rc));
        E->begin_read(dy, &sy);
        bk = NULL;
        rc = E->backup_begin(sy, busy, &bk);
        done = 0;
        while (rc == GS_OK && !done) rc = E->backup_step(bk, 8192, &done);
        uint64_t c3, f3, r3;
        E->backup_progress(bk, &c3, &f3, &r3);
        E->backup_end(bk);
        E->abort(sy);
        CHECK(rc == GS_OK && done, "backing up twin y over x's copy answered %s", gs_status_name(rc));
        CHECK(r3 == 0, "twin y's backup kept %llu of x's files", (unsigned long long)r3);
        rc = E->open(busy, &bo, &b);
        CHECK(rc == GS_OK, "opening y's backup made over x's copy answered %s", gs_status_name(rc));
        if (rc == GS_OK) {
            unsigned want[400];
            for (unsigned i = 0; i < 400; i++) want[i] = 6;
            GsTxn *r;
            E->begin_read(b, &r);
            CHECK(match_model(r, "a", want, 400) == 400, "y's backup over x's copy holds y's data");
            E->abort(r);
            E->close(b);
        }
    }
    if (okx) E->close(dx);
    if (oky) E->close(dy);
}

/* A database is a directory (§7 question 6). Refused: a directory holding a
 * file no engine writes -- the database would delete it, since it owns
 * everything in its directory -- and a path that is a plain file. 2.C deletes
 * at open whatever store-shaped file it does not name, and its check reports
 * one while it is open. */
static void touch(GsqlIo *io, const char *path) {
    GsqlFile *f = NULL;
    if (gsql_io_open(io, path, GSQL_IO_CREATE, &f) == 0) { gsql_io_write_at(f, 0, "x", 1); gsql_io_sync(f); gsql_io_close(f); }
}
static int present(GsqlIo *io, const char *path) {
    GsqlFile *f = NULL;
    if (gsql_io_open(io, path, GSQL_IO_READ_ONLY, &f)) return 0;
    gsql_io_close(f);
    return 1;
}
static void count_reports(void *ctx, const char *line) { (void)line; (*(unsigned *)ctx)++; }
static void directory_rules(const char *base, GsqlIo *io) {
    char dir[600], stranger[700], plain[600];
    snprintf(dir, sizeof dir, "%s.foreign", base);
    snprintf(stranger, sizeof stranger, "%s/notes.txt", dir);
    gsql_io_mkdir(io, dir);
    touch(io, stranger);
    GsOptions o = {1, 512, 0, io, 2000, 2, 0, 0, NULL};
    GsStore *db = NULL;
    int rc = E->open(dir, &o, &db);
    if (rc == GS_OK) E->close(db);
    CHECK(rc == GS_FORMAT, "a directory holding a stranger's file opened (%s), not refused with FORMAT", gs_status_name(rc));
    CHECK(present(io, stranger), "the stranger's file was touched");
    snprintf(plain, sizeof plain, "%s.plain", base);
    touch(io, plain);
    rc = E->open(plain, &o, &db);
    if (rc == GS_OK) E->close(db);
    CHECK(rc == GS_FORMAT, "a plain file as the path answered %s, not FORMAT", gs_status_name(rc));

    snprintf(dir, sizeof dir, "%s.own", base);
    rc = E->open(dir, &o, &db);
    CHECK(rc == GS_OK, "creating a store in a new directory answered %s", gs_status_name(rc));
    if (rc) return;
    for (unsigned i = 0; i < 200; i++) put_one(db, "t", i, 1);
    if (strcmp(E->name, "lsm") == 0) {
        const char *strays[] = {"db.999.sst", "db.998.blob", "db.log.997", "db.tmp"};
        char path[800];
        for (int i = 0; i < 4; i++) { snprintf(path, sizeof path, "%s/%s", dir, strays[i]); touch(io, path); }
        unsigned reports = 0;
        CHECK(E->check(db, count_reports, &reports) == GS_CORRUPT && reports == 4,
              "check reported %u of 4 store-shaped files nothing names", reports);
        E->close(db);
        rc = E->open(dir, &o, &db);
        CHECK(rc == GS_OK, "reopening after strays answered %s", gs_status_name(rc));
        if (rc) return;
        unsigned left = 0;
        for (int i = 0; i < 4; i++) { snprintf(path, sizeof path, "%s/%s", dir, strays[i]); left += present(io, path); }
        CHECK(left == 0, "the open left %u of 4 stray files in place", left);
        CHECK(E->check(db, print_report, NULL) == GS_OK, "check after the sweep");
    }
    GsTxn *r;
    unsigned m;
    E->begin_read(db, &r);
    CHECK(count_gen(r, "t", 0, &m) == 200, "the store kept its 200 records through all of it");
    E->abort(r);
    E->close(db);
}

/* ---- the model ---- */

typedef struct { GsBuf k, v; } Rec;
typedef struct { GsBuf name; Rec *r; size_t n, cap; } MTree;
typedef struct { MTree *t; size_t n, cap; } MState;

static void mt_free(MTree *t) {
    for (size_t i = 0; i < t->n; i++) { gsbuf_free(&t->r[i].k); gsbuf_free(&t->r[i].v); }
    free(t->r);
    gsbuf_free(&t->name);
}
static void ms_free(MState *s) {
    for (size_t i = 0; i < s->n; i++) mt_free(&s->t[i]);
    free(s->t);
    memset(s, 0, sizeof *s);
}
static void ms_copy(MState *d, const MState *s) {
    memset(d, 0, sizeof *d);
    d->cap = s->n ? s->n : 1;
    d->t = calloc(d->cap, sizeof *d->t);
    d->n = s->n;
    for (size_t i = 0; i < s->n; i++) {
        MTree *a = &d->t[i];
        const MTree *b = &s->t[i];
        gsbuf_set(&a->name, b->name.p, b->name.n);
        a->cap = b->n ? b->n : 1;
        a->r = calloc(a->cap, sizeof *a->r);
        a->n = b->n;
        for (size_t j = 0; j < b->n; j++) {
            gsbuf_set(&a->r[j].k, b->r[j].k.p, b->r[j].k.n);
            gsbuf_set(&a->r[j].v, b->r[j].v.p, b->r[j].v.n);
        }
    }
}
static MTree *ms_find(MState *s, GsBytes name) {
    for (size_t i = 0; i < s->n; i++)
        if (bytes_cmp(gsb(s->t[i].name.p, s->t[i].name.n), name) == 0) return &s->t[i];
    return NULL;
}
static MTree *ms_create(MState *s, GsBytes name) {
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 4; s->t = realloc(s->t, s->cap * sizeof *s->t); }
    MTree *t = &s->t[s->n++];
    memset(t, 0, sizeof *t);
    gsbuf_set(&t->name, name.p, name.n);
    return t;
}
static void ms_drop(MState *s, GsBytes name) {
    for (size_t i = 0; i < s->n; i++)
        if (bytes_cmp(gsb(s->t[i].name.p, s->t[i].name.n), name) == 0) {
            mt_free(&s->t[i]);
            s->t[i] = s->t[--s->n];
            return;
        }
}
static size_t mt_lb(const MTree *t, GsBytes k, int *found) {
    size_t lo = 0, hi = t->n;
    *found = 0;
    while (lo < hi) {
        size_t m = (lo + hi) / 2;
        int c = bytes_cmp(gsb(t->r[m].k.p, t->r[m].k.n), k);
        if (c < 0) lo = m + 1;
        else { if (!c) *found = 1; hi = m; }
    }
    return lo;
}
static void mt_put(MTree *t, GsBytes k, GsBytes v) {
    int f;
    size_t i = mt_lb(t, k, &f);
    if (f) { gsbuf_set(&t->r[i].v, v.p, v.n); return; }
    if (t->n == t->cap) { t->cap = t->cap ? t->cap * 2 : 16; t->r = realloc(t->r, t->cap * sizeof *t->r); }
    memmove(&t->r[i + 1], &t->r[i], (t->n - i) * sizeof *t->r);
    memset(&t->r[i], 0, sizeof t->r[i]);
    gsbuf_set(&t->r[i].k, k.p, k.n);
    gsbuf_set(&t->r[i].v, v.p, v.n);
    t->n++;
}
static int mt_del(MTree *t, GsBytes k) {
    int f;
    size_t i = mt_lb(t, k, &f);
    if (!f) return 0;
    gsbuf_free(&t->r[i].k);
    gsbuf_free(&t->r[i].v);
    memmove(&t->r[i], &t->r[i + 1], (t->n - i - 1) * sizeof *t->r);
    t->n--;
    return 1;
}

/* ---- comparing a whole view against a model state ---- */


typedef struct { MState *want; int bad; const char *what; } ListCtx;
static size_t list_i;
static int list_each(void *ctx, GsBytes name) {
    ListCtx *k = ctx;
    (void)name;
    list_i++;
    if (!ms_find(k->want, name)) k->bad = 1;
    return 0;
}

void compare_view(GsTxn *t, MState *want, const char *what);

/* Silent: does the view hold exactly `want`? (the comparison, run with its
 * reports suppressed and its counts put back) */
static int view_is(GsTxn *t, MState *want) {
    unsigned f0 = failures, c0 = checks;
    quiet = 1;
    compare_view(t, want, "probe");
    quiet = 0;
    int same = failures == f0;
    failures = f0;
    checks = c0;
    return same;
}

/* A snapshot sees what is durable. An engine may make a commit durable on its
 * own (§2.C's memtable flush writes every commit so far to a flushed file), so
 * what it sees is the durable state OR the committed one; seeing the second
 * moves the model's durable state up to it. */
static void settle_durable(GsTxn *t, MState *durable, MState *committed);

void compare_view(GsTxn *t, MState *want, const char *what) {
    ListCtx k = {want, 0, what};
    list_i = 0;
    int rc = E->tree_list(t, list_each, &k);
    CHECK(rc == GS_OK && !k.bad && list_i == want->n, "%s: tree_list (rc %s, %zu listed, %zu wanted)", what,
          gs_status_name(rc), list_i, want->n);
    for (size_t i = 0; i < want->n; i++) {
        MTree *mt = &want->t[i];
        GsCursor *c;
        rc = E->cursor_open(t, gsb(mt->name.p, mt->name.n), &c);
        CHECK(rc == GS_OK, "%s: cursor_open on tree %zu answered %s", what, i, gs_status_name(rc));
        if (rc) continue;
        size_t j = 0;
        int pos;
        for (rc = E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos), j++) {
            GsBytes key, val;
            E->key(c, &key);
            if (j >= mt->n || bytes_cmp(key, gsb(mt->r[j].k.p, mt->r[j].k.n)) != 0) break;
            if (E->value(c, &val) != GS_OK || bytes_cmp(val, gsb(mt->r[j].v.p, mt->r[j].v.n)) != 0) {
                CHECK(0, "%s: tree %zu record %zu value differs", what, i, j);
                break;
            }
        }
        CHECK(rc == GS_OK && j == mt->n, "%s: tree %zu scan matched %zu of %zu (rc %s)", what, i, j, mt->n, gs_status_name(rc));
        E->cursor_close(c);
    }
}

static void settle_durable(GsTxn *t, MState *durable, MState *committed) {
    if (view_is(t, durable)) return;
    if (view_is(t, committed)) { ms_free(durable); ms_copy(durable, committed); }
}

/* ---- the run ---- */

static int reports;
static void print_report(void *ctx, const char *line) {
    (void)ctx;
    if (reports++ < 8) printf("  check: %s\n", line);
}

static GsBytes tname(int i, char *buf) { int n = snprintf(buf, 16, "tree%d", i); return gsb(buf, (size_t)n); }

static size_t key_for(unsigned char *kb, uint64_t id) {
    int n = snprintf((char *)kb, 64, "k%05llu", (unsigned long long)id);
    for (uint64_t i = 0; i < id % 13; i++) kb[n++] = (unsigned char)('a' + i);
    return (size_t)n;
}
static size_t value_for(unsigned char *vb, uint64_t id, uint64_t ver) {
    uint64_t r = rnd() % 100;
    size_t len = r < 70 ? rnd() % 30 : r < 96 ? rnd() % 300 : 300 + rnd() % 9000;
    for (size_t i = 0; i < len; i++) vb[i] = (unsigned char)(id * 13 + ver * 5 + i * 3);
    return len;
}

typedef struct { GsTxn *t; MState m; int live; } Reader;
#define NREADERS 3

/* Savepoints (store.h; gap G8 from the gBASIC books) against the model. Each
 * write transaction runs random puts, deletes, tree creates, drops and
 * replaces, with savepoints taken, rolled back to and released at random; the
 * model keeps a copy per open savepoint, and a rollback must leave the view
 * EXACTLY the copy (every tree, every record, the tree list). The cache is
 * small and some values are large, so the buffer spills to runs under open
 * savepoints and a rollback has to shadow records that are already in a run.
 * Each transaction then commits or aborts, and the committed state is checked
 * through a fresh snapshot, and once more after reopening: a DROP undone by a
 * savepoint must not have deleted the tree's data at commit. */
static void savepoints(const char *base, GsqlIo *io) {
    char path[600];
    snprintf(path, sizeof path, "%s.sp", base);
    GsOptions o = {1, 512, 32 * 1024, io, 4096, 2, 64, 0, NULL};  /* spills past 16 KB */
    GsStore *db;
    int rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "opening the savepoint store answered %s", gs_status_name(rc));
    if (rc) return;
    CHECK(E->savepoint && E->rollback_savepoint && E->release_savepoint, "the engine has savepoints");
    if (!E->savepoint) { E->close(db); return; }
    MState committed = {0};
    unsigned char kb[128], *vb = malloc(10000);
    char nb[16];
    uint64_t ver = 0;
    unsigned rollbacks = 0, releases = 0, deep = 0, refused = 0, wrong = 0;
    for (int round = 0; round < 120; round++) {
        GsTxn *w;
        rc = E->begin_write(db, &w);
        CHECK(rc == GS_OK, "begin_write answered %s", gs_status_name(rc));
        if (rc) break;
        CHECK(E->rollback_savepoint(w) == GS_MISUSE && E->release_savepoint(w) == GS_MISUSE,
              "with no savepoint open, rollback and release are refused with MISUSE");
        MState work;
        ms_copy(&work, &committed);
        MState stack[8];
        int depth = 0;
        int ops = 20 + (int)(rnd() % 120);
        for (int op_i = 0; op_i < ops; op_i++) {
            uint64_t op = rnd() % 100;
            GsBytes tn = tname((int)(rnd() % 4), nb);
            MTree *mt = ms_find(&work, tn);
            ver++;
            if (op < 8 && depth < 8) {
                rc = E->savepoint(w);
                CHECK(rc == GS_OK, "savepoint answered %s", gs_status_name(rc));
                ms_copy(&stack[depth++], &work);
                if (depth > 2) deep++;
            } else if (op < 13 && depth > 0) {
                rc = E->rollback_savepoint(w);
                CHECK(rc == GS_OK, "rollback_savepoint answered %s", gs_status_name(rc));
                ms_free(&work);
                work = stack[--depth];
                rollbacks++;
                unsigned f0 = failures;
                compare_view(w, &work, "after a rollback to a savepoint");
                if (failures != f0) wrong++;
            } else if (op < 16 && depth > 0) {
                rc = E->release_savepoint(w);
                CHECK(rc == GS_OK, "release_savepoint answered %s", gs_status_name(rc));
                ms_free(&stack[--depth]);
                releases++;
            } else if (op < 20) {
                rc = E->tree_create(w, tn);
                CHECK(rc == (mt ? GS_EXISTS : GS_OK), "tree_create answered %s", gs_status_name(rc));
                if (!mt && rc == GS_OK) ms_create(&work, tn);
            } else if (op < 23) {
                rc = E->tree_drop(w, tn);
                CHECK(rc == (mt ? GS_OK : GS_NOT_FOUND), "tree_drop answered %s", gs_status_name(rc));
                if (mt) ms_drop(&work, tn);
            } else if (op < 25) {
                char nb2[16];
                GsBytes src = tname((int)(rnd() % 4), nb2);
                MTree *ms = ms_find(&work, src);
                if (bytes_cmp(src, tn) == 0) continue;
                rc = E->tree_replace(w, tn, src);
                CHECK(rc == (ms ? GS_OK : GS_NOT_FOUND), "tree_replace answered %s", gs_status_name(rc));
                if (ms && rc == GS_OK) {
                    MTree *tgt = ms_find(&work, tn);
                    if (tgt) ms_drop(&work, tn);
                    ms = ms_find(&work, src);
                    gsbuf_free(&ms->name);
                    gsbuf_set(&ms->name, tn.p, tn.n);
                }
            } else if (mt && op < 75) {
                uint64_t id = rnd() % 60;
                size_t kn = key_for(kb, id);
                size_t vn = value_for(vb, id, ver);
                rc = E->put(w, tn, gsb(kb, kn), gsb(vb, vn), 0);
                CHECK(rc == GS_OK, "put answered %s", gs_status_name(rc));
                if (rc == GS_OK) mt_put(mt, gsb(kb, kn), gsb(vb, vn));
            } else if (mt) {
                uint64_t id = rnd() % 60;
                size_t kn = key_for(kb, id);
                rc = E->del(w, tn, gsb(kb, kn));
                int had = mt_del(mt, gsb(kb, kn));
                CHECK(rc == (had ? GS_OK : GS_NOT_FOUND), "del answered %s", gs_status_name(rc));
            }
        }
        compare_view(w, &work, "the transaction's own view, savepoints open or not");
        while (depth > 0) ms_free(&stack[--depth]);
        if (rnd() % 5 == 0) {
            E->abort(w);
            ms_free(&work);
        } else {
            rc = E->commit(w, 1, NULL);
            CHECK(rc == GS_OK, "commit answered %s", gs_status_name(rc));
            ms_free(&committed);
            committed = work;
        }
        GsTxn *r;
        E->begin_read(db, &r);
        compare_view(r, &committed, "a snapshot after the transaction ended");
        E->abort(r);
    }
    /* a cursor on a tree the savepoint did not know cannot survive the
     * rollback, and is refused rather than left pointing at nothing */
    GsTxn *w;
    E->begin_write(db, &w);
    E->savepoint(w);
    char fresh[] = "fresh-tree";
    E->tree_create(w, gss(fresh));
    E->put(w, gss(fresh), gss("k"), gss("v"), 0);
    GsCursor *c;
    E->cursor_open(w, gss(fresh), &c);
    if (E->rollback_savepoint(w) == GS_MISUSE) refused++;
    E->cursor_close(c);
    rc = E->rollback_savepoint(w);
    CHECK(rc == GS_OK, "once the cursor is closed the rollback goes ahead (%s)", gs_status_name(rc));
    GsBytes got;
    CHECK(E->get(w, gss(fresh), gss("k"), &got) == GS_NOT_FOUND, "the tree made after the savepoint is gone");
    E->abort(w);
    CHECK(refused == 1, "a rollback with a cursor open on a tree made after the savepoint is refused with MISUSE");

    /* the before-images are bounded (4 MiB at this cache): past it a
     * rollback is refused with TOO_LARGE rather than half done, the
     * transaction still takes writes, and a release still keeps everything */
    unsigned char *huge = malloc(9000);
    memset(huge, 'h', 9000);
    for (int keep_it = 0; keep_it < 2; keep_it++) {
        E->begin_write(db, &w);
        E->tree_create(w, gss("bulk"));
        E->savepoint(w);
        int put_rc = GS_OK;
        for (unsigned i = 0; i < 600 && put_rc == GS_OK; i++) {
            char k[16];
            int kn = snprintf(k, sizeof k, "b%05u", i);
            put_rc = E->put(w, gss("bulk"), gsb(k, (size_t)kn), gsb(huge, 9000), 0);
            put_rc = E->put(w, gss("bulk"), gsb(k, (size_t)kn), gsb(huge, 8999), 0);
        }
        CHECK(put_rc == GS_OK, "5 MB of changes under a savepoint are taken (%s)", gs_status_name(put_rc));
        if (!keep_it) {
            rc = E->rollback_savepoint(w);
            CHECK(rc == GS_TOO_LARGE, "past the bound, a rollback is refused with TOO_LARGE (%s)", gs_status_name(rc));
            CHECK(E->put(w, gss("bulk"), gss("after"), gss("x"), 0) == GS_OK, "and the transaction still takes a write");
            E->abort(w);
        } else {
            CHECK(E->release_savepoint(w) == GS_OK, "past the bound, a release keeps everything");
            CHECK(E->commit(w, 1, NULL) == GS_OK, "and the transaction commits");
            GsTxn *r2;
            E->begin_read(db, &r2);
            GsBytes v;
            CHECK(E->get(r2, gss("bulk"), gss("b00599"), &v) == GS_OK && v.n == 8999, "with its last value");
            E->abort(r2);
            E->begin_write(db, &w);   /* gone again, so the model below still holds */
            E->tree_drop(w, gss("bulk"));
            E->commit(w, 1, NULL);
        }
    }
    free(huge);
    CHECK(wrong == 0, "%u of %u rollbacks left a view other than the model's", wrong, rollbacks);
    CHECK(rollbacks > 100 && releases > 50 && deep > 20, "the run exercised it: %u rollbacks, %u releases, %u savepoints "
          "three deep", rollbacks, releases, deep);
    CHECK(E->check(db, print_report, NULL) == GS_OK, "check after the savepoints");
    E->close(db);
    rc = E->open(path, &o, &db);
    CHECK(rc == GS_OK, "reopening the savepoint store answered %s", gs_status_name(rc));
    if (rc == GS_OK) {
        GsTxn *r;
        E->begin_read(db, &r);
        compare_view(r, &committed, "after reopening: what committed, and no tree an undone DROP retired");
        E->abort(r);
        E->close(db);
    }
    ms_free(&committed);
    free(vb);
    printf("  savepoints: %u rollbacks, %u releases, %u three deep\n", rollbacks, releases, deep);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: test_store ENGINE SEED [real DIR]\n"); return 2; }
    if (!strcmp(argv[1], "lsm")) E = &gs_engine_lsm;
#ifdef HAVE_WAL
    else if (!strcmp(argv[1], "wal")) E = &gs_engine_wal;
#endif
    else { fprintf(stderr, "unknown engine\n"); return 2; }
    rng = strtoull(argv[2], NULL, 10);
    int real = argc >= 5 && !strcmp(argv[3], "real");
    char path[512], bpath[512];
    GsqlIo *io = real ? gsql_io_real() : gsql_io_sim();
    snprintf(path, sizeof path, "%s/db", real ? argv[4] : "d");
    snprintf(bpath, sizeof bpath, "%s/backup", real ? argv[4] : "d");
    /* §2.C: values of 64 bytes or more go to blob files, about a third here */
    GsOptions opt = {1, 1024, 256 * 1024, io, 40, 3, 64, 0, NULL};
    GsStore *db;
    int rc = E->open(path, &opt, &db);
    CHECK(rc == GS_OK, "open answered %s", gs_status_name(rc));
    if (rc) return 1;
    GsStore *db2;
    CHECK(E->open(path, &opt, &db2) == GS_BUSY, "a second opener is refused with BUSY");

    MState committed = {0}, durable = {0};
    Reader rd[NREADERS];
    memset(rd, 0, sizeof rd);
    unsigned char kb[128], *vb = malloc(10000), big[2000];
    memset(big, 'x', sizeof big);
    char nb[16];
    uint64_t ver = 0;
    int unsynced = 0;
    for (int round = 0; round < 300; round++) {
        ver++;
        /* readers come and go, held across many commits */
        for (int r = 0; r < NREADERS; r++) {
            if (rd[r].live && rnd() % 6 == 0) {
                compare_view(rd[r].t, &rd[r].m, "a long-held snapshot");
                GsBytes v;
                CHECK(E->put(rd[r].t, gss("tree0"), gss("x"), gss("y"), 0) == GS_READ_ONLY ||
                          !ms_find(&rd[r].m, gss("tree0")),
                      "a write through a snapshot is refused with READ_ONLY (G4)");
                (void)v;
                E->abort(rd[r].t);
                ms_free(&rd[r].m);
                rd[r].live = 0;
            } else if (!rd[r].live && rnd() % 4 == 0) {
                rc = E->begin_read(db, &rd[r].t);
                CHECK(rc == GS_OK, "begin_read answered %s", gs_status_name(rc));
                if (rc == GS_OK) { settle_durable(rd[r].t, &durable, &committed); ms_copy(&rd[r].m, &durable); rd[r].live = 1; }
            }
        }
        GsTxn *w;
        rc = E->begin_write(db, &w);
        CHECK(rc == GS_OK, "begin_write answered %s", gs_status_name(rc));
        if (rc) break;
        GsTxn *w2;
        CHECK(E->begin_write(db, &w2) == GS_BUSY, "a second writer is refused with BUSY");
        MState work;
        ms_copy(&work, &committed);
        int ops = 1 + (int)(rnd() % 40);
        for (int o = 0; o < ops; o++) {
            uint64_t op = rnd() % 100;
            GsBytes tn = tname((int)(rnd() % 4), nb);
            MTree *mt = ms_find(&work, tn);
            if (op < 6) {
                rc = E->tree_create(w, tn);
                CHECK(rc == (mt ? GS_EXISTS : GS_OK), "tree_create answered %s", gs_status_name(rc));
                if (!mt && rc == GS_OK) ms_create(&work, tn);
            } else if (op < 8) {
                rc = E->tree_drop(w, tn);
                CHECK(rc == (mt ? GS_OK : GS_NOT_FOUND), "tree_drop answered %s", gs_status_name(rc));
                if (mt) ms_drop(&work, tn);
            } else if (op < 10) {
                char nb2[16];
                GsBytes src = tname((int)(rnd() % 4), nb2);
                MTree *ms = ms_find(&work, src);
                if (bytes_cmp(src, tn) == 0) continue;
                rc = E->tree_replace(w, tn, src);
                CHECK(rc == (ms ? GS_OK : GS_NOT_FOUND), "tree_replace answered %s", gs_status_name(rc));
                if (ms && rc == GS_OK) {
                    /* the target takes the source's contents; the source is gone */
                    MState tmp = {0};
                    ms_copy(&tmp, &work);
                    MTree *s2 = ms_find(&tmp, src);
                    if (mt) ms_drop(&work, tn);
                    MTree *nt = ms_create(&work, tn);
                    for (size_t j = 0; j < s2->n; j++) mt_put(nt, gsb(s2->r[j].k.p, s2->r[j].k.n), gsb(s2->r[j].v.p, s2->r[j].v.n));
                    ms_drop(&work, src);
                    ms_free(&tmp);
                }
            } else if (!mt) {
                GsBytes v;
                rc = E->get(w, tn, gss("k"), &v);
                CHECK(rc == GS_NOT_FOUND, "get on a missing tree answered %s", gs_status_name(rc));
            } else if (op < 60) {
                uint64_t id = rnd() % 600;
                size_t kn = key_for(kb, id), vn = value_for(vb, id, ver);
                rc = E->put(w, tn, gsb(kb, kn), gsb(vb, vn), 0);
                CHECK(rc == GS_OK, "put answered %s", gs_status_name(rc));
                if (rc == GS_OK) mt_put(mt, gsb(kb, kn), gsb(vb, vn));
            } else if (op < 75) {
                uint64_t id = rnd() % 600;
                size_t kn = key_for(kb, id);
                int had = mt_del(mt, gsb(kb, kn));
                rc = E->del(w, tn, gsb(kb, kn));
                CHECK(rc == (had ? GS_OK : GS_NOT_FOUND), "del answered %s (had %d)", gs_status_name(rc), had);
            } else if (op < 82) {
                uint64_t id = rnd() % 600;
                size_t kn = key_for(kb, id);
                int f;
                size_t i = mt_lb(mt, gsb(kb, kn), &f);
                GsBytes v;
                rc = E->get(w, tn, gsb(kb, kn), &v);
                if (f) CHECK(rc == GS_OK && bytes_cmp(v, gsb(mt->r[i].v.p, mt->r[i].v.n)) == 0, "get of a present key");
                else CHECK(rc == GS_NOT_FOUND, "get of an absent key answered %s", gs_status_name(rc));
            } else if (op < 92) {
                /* a cursor walk from a seek, writing through it as it goes */
                GsCursor *c;
                rc = E->cursor_open(w, tn, &c);
                CHECK(rc == GS_OK, "cursor_open answered %s", gs_status_name(rc));
                if (rc) continue;
                size_t kn = key_for(kb, rnd() % 600);
                GsSeek mode = (GsSeek)(rnd() % 5);
                int pos, f;
                size_t lb = mt_lb(mt, gsb(kb, kn), &f);
                long want = mode == GS_SEEK_GE ? (lb < mt->n ? (long)lb : -1)
                          : mode == GS_SEEK_GT ? ((f ? lb + 1 : lb) < mt->n ? (long)(f ? lb + 1 : lb) : -1)
                          : mode == GS_SEEK_LE ? (f ? (long)lb : (long)lb - 1)
                          : mode == GS_SEEK_LT ? (long)lb - 1
                          : (f ? (long)lb : -1);
                rc = E->seek(c, gsb(kb, kn), mode, &pos);
                GsBytes key;
                CHECK(rc == GS_OK && (want < 0 ? !pos : pos && E->key(c, &key) == GS_OK &&
                                                       bytes_cmp(key, gsb(mt->r[want].k.p, mt->r[want].k.n)) == 0),
                      "seek mode %d (want %ld)", (int)mode, want);
                long at = want;
                int forward = rnd() % 2;
                for (int step = 0; pos && step < 12 && at >= 0; step++) {
                    uint64_t what = rnd() % 10;
                    if (what == 0) {
                        size_t vn = value_for(vb, (uint64_t)at, ver + 1000);
                        rc = E->cursor_put(c, gsb(vb, vn));
                        CHECK(rc == GS_OK, "cursor_put answered %s", gs_status_name(rc));
                        mt_put(mt, gsb(mt->r[at].k.p, mt->r[at].k.n), gsb(vb, vn));
                        GsBytes v;
                        CHECK(E->value(c, &v) == GS_OK && bytes_cmp(v, gsb(vb, vn)) == 0, "the cursor reads its own write");
                    } else if (what == 1) {
                        rc = E->cursor_delete(c);
                        CHECK(rc == GS_OK, "cursor_delete answered %s", gs_status_name(rc));
                        mt_del(mt, gsb(mt->r[at].k.p, mt->r[at].k.n));
                        /* between neighbours: next is the one after, prev the one before */
                        if (!forward) at--;
                        rc = forward ? E->next(c, &pos) : E->prev(c, &pos);
                        int okpos = at >= 0 && at < (long)mt->n;
                        CHECK(rc == GS_OK && pos == okpos && (!pos || (E->key(c, &key) == GS_OK &&
                                                                bytes_cmp(key, gsb(mt->r[at].k.p, mt->r[at].k.n)) == 0)),
                              "after cursor_delete the cursor rests between its neighbours (forward %d)", forward);
                        continue;
                    }
                    rc = forward ? E->next(c, &pos) : E->prev(c, &pos);
                    at += forward ? 1 : -1;
                    int okpos = at >= 0 && at < (long)mt->n;
                    CHECK(rc == GS_OK && pos == okpos && (!pos || (E->key(c, &key) == GS_OK &&
                                                            bytes_cmp(key, gsb(mt->r[at].k.p, mt->r[at].k.n)) == 0)),
                          "cursor step (forward %d)", forward);
                }
                /* a change made other than through it invalidates it */
                if (rnd() % 3 == 0) {
                    uint64_t id = rnd() % 600;
                    size_t kn2 = key_for(kb, id), vn = value_for(vb, id, ver);
                    if (E->put(w, tn, gsb(kb, kn2), gsb(vb, vn), 0) == GS_OK) mt_put(mt, gsb(kb, kn2), gsb(vb, vn));
                    rc = E->next(c, &pos);
                    CHECK(rc == GS_INVALIDATED, "a cursor whose tree changed under it answered %s", gs_status_name(rc));
                }
                E->cursor_close(c);
            } else if (op < 94) {
                unsigned char longk[1100];
                memset(longk, 'L', sizeof longk);
                rc = E->put(w, tn, gsb(longk, sizeof longk), gss("v"), 0);
                CHECK(rc == GS_TOO_LARGE, "a 1,100-byte key answered %s", gs_status_name(rc));
            } else {
                /* NO_OVERWRITE on a key we know is present */
                if (mt->n) {
                    Rec *r = &mt->r[rnd() % mt->n];
                    rc = E->put(w, tn, gsb(r->k.p, r->k.n), gss("no"), GS_NO_OVERWRITE);
                    CHECK(rc == GS_EXISTS, "NO_OVERWRITE over a present key answered %s", gs_status_name(rc));
                }
            }
        }
        uint64_t choice = rnd() % 10;
        if (choice == 0) {
            E->abort(w);
            ms_free(&work);
        } else {
            int sync = choice < 7;
            uint64_t id = 0;
            rc = E->commit(w, sync, &id);
            CHECK(rc == GS_OK, "commit answered %s", gs_status_name(rc));
            ms_free(&committed);
            committed = work;
            unsynced = !sync;
            if (sync) { ms_free(&durable); ms_copy(&durable, &committed); }
            else if (rnd() % 3 == 0) {
                uint64_t d;
                rc = E->sync(db, &d);
                CHECK(rc == GS_OK && d == id, "sync answered %s, durable %llu, want %llu", gs_status_name(rc),
                      (unsigned long long)d, (unsigned long long)id);
                ms_free(&durable);
                ms_copy(&durable, &committed);
                unsynced = 0;
            }
        }
        /* a snapshot begun now sees exactly what is durable */
        GsTxn *r;
        if (E->begin_read(db, &r) == GS_OK) {
            settle_durable(r, &durable, &committed);
            if (round % 5 == 0) compare_view(r, &durable, "a fresh snapshot");
            if (round % 37 == 0) {
                rc = E->backup(r, bpath);
                CHECK(rc == GS_OK, "backup answered %s", gs_status_name(rc));
            }
            E->abort(r);
        }
        if (round % 37 == 0 && rc == GS_OK) {
            GsStore *b;
            GsOptions bo = opt;
            bo.create = 0;
            rc = E->open(bpath, &bo, &b);
            CHECK(rc == GS_OK, "opening a backup answered %s", gs_status_name(rc));
            if (rc == GS_OK) {
                CHECK(E->check(b, print_report, NULL) == GS_OK, "a backup passes check");
                GsTxn *br;
                E->begin_read(b, &br);
                compare_view(br, &durable, "a backup");
                E->abort(br);
                E->close(b);
            }
        }
        if (round % 10 == 0) {
            int any = 0;
            for (int i = 0; i < NREADERS; i++) any |= rd[i].live;
            if (!any) {
                CHECK(E->check(db, print_report, NULL) == GS_OK, "check after round %d", round);
            }
        }
        if (round % 50 == 49) {
            /* reopen: a clean close makes every commit durable */
            for (int i = 0; i < NREADERS; i++)
                if (rd[i].live) { compare_view(rd[i].t, &rd[i].m, "a snapshot before close"); E->abort(rd[i].t); ms_free(&rd[i].m); rd[i].live = 0; }
            GsTxn *x;
            E->begin_read(db, &x);
            CHECK(E->close(db) == GS_MISUSE, "close with a snapshot open answers MISUSE");
            E->abort(x);
            CHECK(E->close(db) == GS_OK, "close");
            if (!real) {
                /* and the disk a process crash would have left is the same */
                GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_PROCESS, 0);
                GsOptions io2 = opt;
                io2.io = img;
                io2.create = 0;
                GsStore *c;
                rc = E->open(path, &io2, &c);
                CHECK(rc == GS_OK, "opening a process-crash image answered %s", gs_status_name(rc));
                if (rc == GS_OK) {
                    GsTxn *cr;
                    E->begin_read(c, &cr);
                    compare_view(cr, &committed, "a process-crash image after a clean close");
                    E->abort(cr);
                    E->close(c);
                }
                gsql_io_free(img);
            }
            rc = E->open(path, &opt, &db);
            CHECK(rc == GS_OK, "reopen answered %s", gs_status_name(rc));
            if (rc) break;
            ms_free(&durable);
            ms_copy(&durable, &committed);
            unsynced = 0;
            GsTxn *x2;
            E->begin_read(db, &x2);
            compare_view(x2, &committed, "after reopening");
            E->abort(x2);
        }
    }
    (void)unsynced;
    for (int i = 0; i < NREADERS; i++)
        if (rd[i].live) { compare_view(rd[i].t, &rd[i].m, "a final snapshot"); E->abort(rd[i].t); ms_free(&rd[i].m); }

    /* G5: one transaction many times the cache */
    GsTxn *w;
    E->begin_write(db, &w);
    E->tree_create(w, gss("bulk"));
    int bulk_ok = 1;
    for (uint64_t i = 0; i < 6000 && bulk_ok; i++) {
        char k[32];
        int n = snprintf(k, sizeof k, "b%08llu", (unsigned long long)i);
        bulk_ok = E->put(w, gss("bulk"), gsb(k, (size_t)n), gsb(big, 200), GS_APPEND) == GS_OK;
    }
    CHECK(bulk_ok, "a 6,000-row append into a 256 KiB cache");
    rc = E->commit(w, 1, NULL);
    CHECK(rc == GS_OK, "committing the bulk txn answered %s", gs_status_name(rc));
    GsTxn *r;
    E->begin_read(db, &r);
    GsCursor *c;
    E->cursor_open(r, gss("bulk"), &c);
    int pos;
    size_t n = 0;
    for (rc = E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos)) n++;
    CHECK(n == 6000, "the bulk tree holds %zu of 6000", n);
    E->cursor_close(c);
    E->abort(r);
    CHECK(E->check(db, print_report, NULL) == GS_OK, "check after the bulk txn");
    GsStats st;
    E->stats(db, &st);
    E->close(db);
    dropped_not_durable(path, io);
    hot_keys(path, io);
    sliced_backup(path, io);
    refreshed_backup(path, io);
    directory_rules(path, io);
    many_files(path, io, real);
    blob_space(path, io);
    frozen_reads(path, io);
    lost_twin(path, io);
    real_fs = real;
    flushed_handoff(path, io);
    crc_agreement();
    empty_footprint(path, io);
    sorted_load(path, io);
    estimates(path, io);
    savepoints(path, io);
    ms_free(&committed);
    ms_free(&durable);
    free(vb);
    gsql_io_free(io);
    if (failures) { printf("%u of %u checks failed\n", failures, checks); return 1; }
    printf("ok %u checks (pages read %llu, written %llu, flushes %llu)\n", checks,
           (unsigned long long)st.pages_read, (unsigned long long)st.pages_written, (unsigned long long)st.flushes);
    return 0;
}

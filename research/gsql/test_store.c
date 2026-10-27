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

#include "common.h"

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
    GsOptions o = {1, 512, 0, io, 2000, 2, 0, 0};
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
    GsOptions o = {1, 512, 0, io, 4096, 1000, 0, 8};
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
    GsOptions o = {1, 512, 0, io, 2000, 2, 0, 0};
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
static void sliced_backup(const char *base, GsqlIo *io) {
    unsigned ga[300];
    for (unsigned i = 0; i < 300; i++) ga[i] = 1;
    char path[600], dest[600], dest2[600];
    snprintf(path, sizeof path, "%s.bk", base);
    snprintf(dest, sizeof dest, "%s.bk-copy", base);
    snprintf(dest2, sizeof dest2, "%s.bk-copy2", base);
    GsOptions o = {1, 512, 0, io, 2000, 2, 32, 0};
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
    GsOptions o = {1, 512, 0, io, 2000, 2, 0, 0};
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

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: test_store ENGINE SEED [real DIR]\n"); return 2; }
    if (!strcmp(argv[1], "cow")) E = &gs_engine_cow;
#ifdef HAVE_WAL
    else if (!strcmp(argv[1], "wal")) E = &gs_engine_wal;
#endif
#ifdef HAVE_LSM
    else if (!strcmp(argv[1], "lsm")) E = &gs_engine_lsm;
#endif
    else { fprintf(stderr, "unknown engine\n"); return 2; }
    rng = strtoull(argv[2], NULL, 10);
    int real = argc >= 5 && !strcmp(argv[3], "real");
    char path[512], bpath[512];
    GsqlIo *io = real ? gsql_io_real() : gsql_io_sim();
    snprintf(path, sizeof path, "%s/db", real ? argv[4] : "d");
    snprintf(bpath, sizeof bpath, "%s/backup", real ? argv[4] : "d");
    /* §2.C: values of 64 bytes or more go to blob files, about a third here */
    GsOptions opt = {1, 1024, 256 * 1024, io, 40, 3, 64, 0};
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
    directory_rules(path, io);
    many_files(path, io, real);
    ms_free(&committed);
    ms_free(&durable);
    free(vb);
    gsql_io_free(io);
    if (failures) { printf("%u of %u checks failed\n", failures, checks); return 1; }
    printf("ok %u checks (pages read %llu, written %llu, flushes %llu)\n", checks,
           (unsigned long long)st.pages_read, (unsigned long long)st.pages_written, (unsigned long long)st.flushes);
    return 0;
}

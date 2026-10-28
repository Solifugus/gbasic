/* The Phase 0a workload harness (docs/gsql_storage_design.md §5.1, §5.2):
 * one program drives every candidate and every reference engine through one
 * small key-value interface, so a number differs because an engine differs.
 *
 *   bench ENGINE WORKLOAD DIR [RECORDS [OPS]]
 *
 * ENGINE: cow, wal, lsm (the candidates), sqlite (and lmdb, rocksdb when
 * built with them). WORKLOAD: ycsb-load, ycsb-a ... ycsb-f, w1 ... (see
 * main), tpcc and ssb1 (below). Prints ONE JSON line of metrics on stdout. Each run is meant to be a
 * fresh process on a fresh directory, so peak memory is the run's own.
 *
 * Measurement rules, stated once:
 *   - durability matches across engines: every write transaction commits with
 *     sync; SQLite runs WAL with synchronous=FULL (§5.1.0);
 *   - bytes written are the process's own write(2) bytes (/proc/self/io
 *     wchar), the same measure for every engine; flushes are counted where the
 *     engine can say (the candidates), and reported as -1 where it cannot;
 *   - latency is per operation, wall clock (CLOCK_MONOTONIC);
 *   - the OS page cache is NOT dropped between runs (it needs root), so "data
 *     larger than the cache" means larger than the ENGINE's cache only. */

#define _POSIX_C_SOURCE 200809L
#include "common.h"

#include <dirent.h>
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#ifdef HAVE_SQLITE
#include <sqlite3.h>
#endif
#ifdef HAVE_LMDB
#include <lmdb.h>
#endif
#ifdef HAVE_ROCKSDB
#include <rocksdb/c.h>
#endif

/* ---- a tiny key-value interface over every engine ------------------------- */

typedef struct BKV BKV;
typedef void (*ScanFn)(void *arg, const unsigned char *v, size_t vn);
typedef struct {
    const char *name;
    BKV *(*open)(const char *dir, uint64_t cache, uint32_t page);
    void (*close)(BKV *);
    int (*begin)(BKV *, int write);
    int (*put)(BKV *, const void *k, size_t kn, const void *v, size_t vn);
    int (*get)(BKV *, const void *k, size_t kn, size_t *vn);   /* 0 found, 1 absent, <0 error */
    int (*del)(BKV *, const void *k, size_t kn);
    /* `fn`, when given, is handed each value in key order (SSB filters on them) */
    int (*scan)(BKV *, const void *k, size_t kn, int count, int *got, uint64_t *bytes, ScanFn fn, void *arg);
    int (*end)(BKV *, int commit, int sync);                   /* commit or abort/release */
    void (*counters)(BKV *, uint64_t *flushes, uint64_t *pages_read);
    uint64_t (*file_bytes)(BKV *);
    int (*sync)(BKV *);        /* make every commit so far durable (group commit) */
    int (*hold)(BKV *);        /* open a long-lived read snapshot (W6) */
    void (*release)(BKV *);
    /* The version store's one read (§4): the key exactly, or with `floor`
     * the greatest key <= k. Copies the key found (up to 64 bytes) and up to
     * `vcap` bytes of its value; *von is the value's whole length. 0 found,
     * 1 none, <0 error. Inside a transaction. */
    int (*fetch)(BKV *, const void *k, size_t kn, int floor, unsigned char *ko, size_t *kon, unsigned char *vo, size_t vcap,
                 size_t *von);
} BEngine;

static void fetch_copy(const void *k, size_t kn, const void *v, size_t vn, unsigned char *ko, size_t *kon, unsigned char *vo,
                       size_t vcap, size_t *von) {
    *kon = kn < 64 ? kn : 64;
    memcpy(ko, k, *kon);
    *von = vn;
    memcpy(vo, v, vn < vcap ? vn : vcap);
}

/* ---- the candidates, through store.h -------------------------------------- */

struct BKV {
    const GsEngine *e;
    GsStore *db;
    GsTxn *t, *held;
    int write;
    char dir[512];
#ifdef HAVE_SQLITE
    sqlite3 *sq, *sq2;
    int sq_sync; /* 0 unknown, 1 OFF, 2 FULL */
    sqlite3_stmt *s_put, *s_get, *s_del, *s_scan, *s_begin_r, *s_begin_w, *s_commit, *s_rollback, *s_floor;
#endif
#ifdef HAVE_LMDB
    MDB_env *env;
    MDB_dbi dbi;
    MDB_txn *lt, *lheld;
#endif
#ifdef HAVE_ROCKSDB
    rocksdb_t *rdb;
    rocksdb_options_t *ropt;
    rocksdb_writeoptions_t *wsync, *wnosync;
    rocksdb_readoptions_t *ropts;
    rocksdb_writebatch_t *batch;
    const rocksdb_snapshot_t *rsnap;
#endif
};

static const GsEngine *cand;

static BKV *c_open(const char *dir, uint64_t cache, uint32_t page) {
    BKV *b = calloc(1, sizeof *b);
    b->e = cand;
    snprintf(b->dir, sizeof b->dir, "%s", dir);
    char path[600];
    snprintf(path, sizeof path, "%s/db", dir);
    /* BENCH_TUNE_A / BENCH_TUNE_B: the candidates' own knobs (store.h) */
    const char *ta = getenv("BENCH_TUNE_A"), *tb = getenv("BENCH_TUNE_B"), *tc = getenv("BENCH_TUNE_C");
    GsOptions o = {1, page, cache, NULL, ta ? strtoull(ta, NULL, 10) : 0, tb ? strtoull(tb, NULL, 10) : 0,
                   tc ? strtoull(tc, NULL, 10) : 0,
                   getenv("BENCH_TUNE_D") ? strtoull(getenv("BENCH_TUNE_D"), NULL, 10) : 0};
    if (b->e->open(path, &o, &b->db)) { free(b); return NULL; }
    GsTxn *t;
    b->e->begin_write(b->db, &t);
    b->e->tree_create(t, gss("t"));
    b->e->commit(t, 1, NULL);
    return b;
}
static void c_close(BKV *b) { b->e->close(b->db); free(b); }
static int c_begin(BKV *b, int write) {
    b->write = write;
    return write ? b->e->begin_write(b->db, &b->t) : b->e->begin_read(b->db, &b->t);
}
static int c_put(BKV *b, const void *k, size_t kn, const void *v, size_t vn) { return b->e->put(b->t, gss("t"), gsb(k, kn), gsb(v, vn), 0); }
static int c_get(BKV *b, const void *k, size_t kn, size_t *vn) {
    GsBytes v;
    int rc = b->e->get(b->t, gss("t"), gsb(k, kn), &v);
    if (rc == GS_NOT_FOUND) return 1;
    if (rc) return -rc;
    *vn = v.n;
    return 0;
}
static int c_del(BKV *b, const void *k, size_t kn) { int rc = b->e->del(b->t, gss("t"), gsb(k, kn)); return rc == GS_NOT_FOUND ? 1 : rc ? -rc : 0; }
static int c_scan(BKV *b, const void *k, size_t kn, int count, int *got, uint64_t *bytes, ScanFn fn, void *arg) {
    GsCursor *c;
    int rc = b->e->cursor_open(b->t, gss("t"), &c);
    if (rc) return -rc;
    int pos;
    *got = 0;
    for (rc = b->e->seek(c, gsb(k, kn), GS_SEEK_GE, &pos); rc == GS_OK && pos && *got < count; rc = b->e->next(c, &pos)) {
        GsBytes v;
        b->e->value(c, &v);
        if (fn) fn(arg, v.p, v.n);
        *bytes += v.n;
        (*got)++;
    }
    b->e->cursor_close(c);
    return rc ? -rc : 0;
}
static int c_end(BKV *b, int commit, int sync) {
    if (b->write && commit) return b->e->commit(b->t, sync, NULL);
    b->e->abort(b->t);
    return 0;
}
static void c_counters(BKV *b, uint64_t *fl, uint64_t *pr) {
    GsStats s;
    b->e->stats(b->db, &s);
    *fl = s.flushes;
    *pr = s.pages_read;
}
static uint64_t dir_bytes(const char *dir) {
    DIR *d = opendir(dir);
    uint64_t total = 0;
    if (!d) return 0;
    struct dirent *de;
    char p[1024];
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(p, &st) != 0) continue;
        /* a candidate's store is a directory (§7 question 6): one level down */
        if (S_ISDIR(st.st_mode)) total += dir_bytes(p);
        else if (S_ISREG(st.st_mode)) total += (uint64_t)st.st_size;
    }
    closedir(d);
    return total;
}
static uint64_t c_file_bytes(BKV *b) { return dir_bytes(b->dir); }
static int c_sync(BKV *b) { return b->e->sync(b->db, NULL); }
static int c_hold(BKV *b) { return b->e->begin_read(b->db, &b->held); }
static void c_release(BKV *b) { if (b->held) b->e->abort(b->held); b->held = NULL; }

static int c_fetch(BKV *b, const void *k, size_t kn, int fl, unsigned char *ko, size_t *kon, unsigned char *vo, size_t vcap,
                   size_t *von) {
    if (!fl) {
        GsBytes v;
        int rc = b->e->get(b->t, gss("t"), gsb(k, kn), &v);
        if (rc == GS_NOT_FOUND) return 1;
        if (rc) return -rc;
        fetch_copy(k, kn, v.p, v.n, ko, kon, vo, vcap, von);
        return 0;
    }
    GsCursor *c;
    int rc = b->e->cursor_open(b->t, gss("t"), &c), pos = 0;
    if (rc) return -rc;
    rc = b->e->seek(c, gsb(k, kn), GS_SEEK_LE, &pos);
    if (!rc && pos) {
        GsBytes K, V;
        b->e->key(c, &K);
        b->e->value(c, &V);
        fetch_copy(K.p, K.n, V.p, V.n, ko, kon, vo, vcap, von);
    }
    b->e->cursor_close(c);
    return rc ? -rc : pos ? 0 : 1;
}
static const BEngine cand_engine = {"candidate", c_open, c_close, c_begin, c_put, c_get, c_del, c_scan, c_end, c_counters, c_file_bytes,
                                    c_sync, c_hold, c_release, c_fetch};

/* ---- SQLite, the reference that is installed ------------------------------ */

#ifdef HAVE_SQLITE
static BKV *q_open(const char *dir, uint64_t cache, uint32_t page) {
    BKV *b = calloc(1, sizeof *b);
    snprintf(b->dir, sizeof b->dir, "%s", dir);
    char path[600];
    snprintf(path, sizeof path, "%s/db.sqlite", dir);
    if (sqlite3_open(path, &b->sq) != SQLITE_OK) { free(b); return NULL; }
    char sql[256];
    snprintf(sql, sizeof sql, "PRAGMA page_size=%u; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA cache_size=-%llu;",
             page, (unsigned long long)(cache / 1024));
    sqlite3_exec(b->sq, sql, NULL, NULL, NULL);
    sqlite3_exec(b->sq, "CREATE TABLE IF NOT EXISTS kv(k BLOB PRIMARY KEY, v BLOB) WITHOUT ROWID", NULL, NULL, NULL);
    sqlite3_prepare_v2(b->sq, "INSERT OR REPLACE INTO kv VALUES(?,?)", -1, &b->s_put, NULL);
    sqlite3_prepare_v2(b->sq, "SELECT v FROM kv WHERE k=?", -1, &b->s_get, NULL);
    sqlite3_prepare_v2(b->sq, "DELETE FROM kv WHERE k=?", -1, &b->s_del, NULL);
    sqlite3_prepare_v2(b->sq, "SELECT v FROM kv WHERE k>=? ORDER BY k LIMIT ?", -1, &b->s_scan, NULL);
    sqlite3_prepare_v2(b->sq, "SELECT k, v FROM kv WHERE k<=? ORDER BY k DESC LIMIT 1", -1, &b->s_floor, NULL);
    sqlite3_prepare_v2(b->sq, "BEGIN", -1, &b->s_begin_r, NULL);
    sqlite3_prepare_v2(b->sq, "BEGIN IMMEDIATE", -1, &b->s_begin_w, NULL);
    sqlite3_prepare_v2(b->sq, "COMMIT", -1, &b->s_commit, NULL);
    sqlite3_prepare_v2(b->sq, "ROLLBACK", -1, &b->s_rollback, NULL);
    return b;
}
static void q_close(BKV *b) {
    sqlite3_stmt *all[] = {b->s_put, b->s_get, b->s_del, b->s_scan, b->s_begin_r, b->s_begin_w, b->s_commit, b->s_rollback, b->s_floor};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) sqlite3_finalize(all[i]);
    sqlite3_close(b->sq);
    free(b);
}
static int q_step(sqlite3_stmt *s) { int rc = sqlite3_step(s); sqlite3_reset(s); return rc == SQLITE_DONE || rc == SQLITE_ROW ? 0 : -1; }
static int q_begin(BKV *b, int write) { b->write = write; return q_step(write ? b->s_begin_w : b->s_begin_r); }
/* OFF writes the WAL without fsync, which is what an unsynced commit means;
 * q_sync then fsyncs the WAL file itself. SQLite IGNORES the pragma inside an
 * open transaction (measured: one fdatasync per commit when it was issued
 * there), so it is set after a commit, for the next one */
static int q_sync(BKV *b);
static void q_set_sync(BKV *b, int full) {
    if (b->sq_sync == full + 1) return;
    sqlite3_exec(b->sq, full ? "PRAGMA synchronous=FULL" : "PRAGMA synchronous=OFF", NULL, NULL, NULL);
    b->sq_sync = full + 1;
}
static int q_put(BKV *b, const void *k, size_t kn, const void *v, size_t vn) {
    sqlite3_bind_blob(b->s_put, 1, k, (int)kn, SQLITE_STATIC);
    sqlite3_bind_blob(b->s_put, 2, v, (int)vn, SQLITE_STATIC);
    return q_step(b->s_put);
}
static int q_get(BKV *b, const void *k, size_t kn, size_t *vn) {
    sqlite3_bind_blob(b->s_get, 1, k, (int)kn, SQLITE_STATIC);
    int rc = sqlite3_step(b->s_get), out = 1;
    if (rc == SQLITE_ROW) { *vn = (size_t)sqlite3_column_bytes(b->s_get, 0); out = 0; }
    else if (rc != SQLITE_DONE) out = -1;
    sqlite3_reset(b->s_get);
    return out;
}
static int q_del(BKV *b, const void *k, size_t kn) {
    sqlite3_bind_blob(b->s_del, 1, k, (int)kn, SQLITE_STATIC);
    int rc = q_step(b->s_del);
    return rc ? rc : sqlite3_changes(b->sq) ? 0 : 1;
}
static int q_scan(BKV *b, const void *k, size_t kn, int count, int *got, uint64_t *bytes, ScanFn fn, void *arg) {
    sqlite3_bind_blob(b->s_scan, 1, k, (int)kn, SQLITE_STATIC);
    sqlite3_bind_int(b->s_scan, 2, count);
    *got = 0;
    int rc;
    while ((rc = sqlite3_step(b->s_scan)) == SQLITE_ROW) {
        const unsigned char *v = sqlite3_column_blob(b->s_scan, 0);
        size_t vn = (size_t)sqlite3_column_bytes(b->s_scan, 0);
        if (fn) fn(arg, v, vn);
        *bytes += vn;
        (*got)++;
    }
    sqlite3_reset(b->s_scan);
    return rc == SQLITE_DONE ? 0 : -1;
}
static int q_end(BKV *b, int commit, int sync) {
    int rc = q_step(commit && b->write ? b->s_commit : (b->write ? b->s_rollback : b->s_commit));
    if (b->write && commit && !rc) {
        if (sync && b->sq_sync == 1 && q_sync(b)) rc = -1; /* committed under OFF: make it durable now */
        q_set_sync(b, sync);
    }
    return rc;
}
static void q_counters(BKV *b, uint64_t *fl, uint64_t *pr) { (void)b; *fl = (uint64_t)-1; *pr = (uint64_t)-1; }
static uint64_t q_file_bytes(BKV *b) { return dir_bytes(b->dir); }
static int q_sync(BKV *b) {
    sqlite3_file *wal = NULL;
    if (sqlite3_file_control(b->sq, "main", SQLITE_FCNTL_JOURNAL_POINTER, &wal) != SQLITE_OK || !wal || !wal->pMethods) return -1;
    return wal->pMethods->xSync(wal, SQLITE_SYNC_FULL) == SQLITE_OK ? 0 : -1;
}
static int q_hold(BKV *b) {
    char path[600];
    snprintf(path, sizeof path, "%s/db.sqlite", b->dir);
    if (sqlite3_open(path, &b->sq2) != SQLITE_OK) return -1;
    /* a read transaction that has read something holds its snapshot */
    sqlite3_exec(b->sq2, "BEGIN; SELECT count(*) FROM (SELECT 1 FROM kv LIMIT 1);", NULL, NULL, NULL);
    return 0;
}
static void q_release(BKV *b) { if (b->sq2) { sqlite3_exec(b->sq2, "COMMIT", NULL, NULL, NULL); sqlite3_close(b->sq2); b->sq2 = NULL; } }
static int q_fetch(BKV *b, const void *k, size_t kn, int fl, unsigned char *ko, size_t *kon, unsigned char *vo, size_t vcap,
                   size_t *von) {
    sqlite3_stmt *s = fl ? b->s_floor : b->s_get;
    sqlite3_bind_blob(s, 1, k, (int)kn, SQLITE_STATIC);
    int rc = sqlite3_step(s), out = 1;
    if (rc == SQLITE_ROW) {
        if (fl) fetch_copy(sqlite3_column_blob(s, 0), (size_t)sqlite3_column_bytes(s, 0), sqlite3_column_blob(s, 1),
                           (size_t)sqlite3_column_bytes(s, 1), ko, kon, vo, vcap, von);
        else fetch_copy(k, kn, sqlite3_column_blob(s, 0), (size_t)sqlite3_column_bytes(s, 0), ko, kon, vo, vcap, von);
        out = 0;
    } else if (rc != SQLITE_DONE) out = -1;
    sqlite3_reset(s);
    return out;
}
static const BEngine sqlite_engine = {"sqlite", q_open, q_close, q_begin, q_put, q_get, q_del, q_scan, q_end, q_counters, q_file_bytes,
                                      q_sync, q_hold, q_release, q_fetch};
#endif

/* ---- LMDB: the copy-on-write reference ------------------------------------ */

#ifdef HAVE_LMDB
static BKV *l_open(const char *dir, uint64_t cache, uint32_t page) {
    (void)cache; (void)page; /* LMDB uses the OS page cache and the system page size */
    BKV *b = calloc(1, sizeof *b);
    snprintf(b->dir, sizeof b->dir, "%s", dir);
    mdb_env_create(&b->env);
    mdb_env_set_mapsize(b->env, 64ull << 30);
    /* default flags: every commit is durable (two fsyncs, as 2.A) */
    if (mdb_env_open(b->env, dir, MDB_NOTLS, 0644)) { free(b); return NULL; }
    MDB_txn *t;
    mdb_txn_begin(b->env, NULL, 0, &t);
    mdb_dbi_open(t, NULL, 0, &b->dbi);
    mdb_txn_commit(t);
    return b;
}
static void l_close(BKV *b) { mdb_env_close(b->env); free(b); }
static int l_begin(BKV *b, int write) { b->write = write; return mdb_txn_begin(b->env, NULL, write ? 0 : MDB_RDONLY, &b->lt) ? -1 : 0; }
static int l_put(BKV *b, const void *k, size_t kn, const void *v, size_t vn) {
    MDB_val K = {kn, (void *)k}, V = {vn, (void *)v};
    return mdb_put(b->lt, b->dbi, &K, &V, 0) ? -1 : 0;
}
static int l_get(BKV *b, const void *k, size_t kn, size_t *vn) {
    MDB_val K = {kn, (void *)k}, V;
    int rc = mdb_get(b->lt, b->dbi, &K, &V);
    if (rc == MDB_NOTFOUND) return 1;
    if (rc) return -1;
    *vn = V.mv_size;
    return 0;
}
static int l_del(BKV *b, const void *k, size_t kn) {
    MDB_val K = {kn, (void *)k};
    int rc = mdb_del(b->lt, b->dbi, &K, NULL);
    return rc == MDB_NOTFOUND ? 1 : rc ? -1 : 0;
}
static int l_scan(BKV *b, const void *k, size_t kn, int count, int *got, uint64_t *bytes, ScanFn fn, void *arg) {
    MDB_cursor *c;
    if (mdb_cursor_open(b->lt, b->dbi, &c)) return -1;
    MDB_val K = {kn, (void *)k}, V;
    *got = 0;
    for (int rc = mdb_cursor_get(c, &K, &V, MDB_SET_RANGE); rc == 0 && *got < count; rc = mdb_cursor_get(c, &K, &V, MDB_NEXT)) {
        if (fn) fn(arg, V.mv_data, V.mv_size);
        *bytes += V.mv_size;
        (*got)++;
    }
    mdb_cursor_close(c);
    return 0;
}
static int l_end(BKV *b, int commit, int sync) {
    if (b->write && commit) {
        /* MDB_NOSYNC may be toggled between transactions; an unsynced commit
         * writes its pages and the meta page without fsync */
        mdb_env_set_flags(b->env, MDB_NOSYNC, !sync);
        return mdb_txn_commit(b->lt) ? -1 : 0;
    }
    mdb_txn_abort(b->lt);
    return 0;
}
static void l_counters(BKV *b, uint64_t *fl, uint64_t *pr) { (void)b; *fl = (uint64_t)-1; *pr = (uint64_t)-1; }
static uint64_t l_file_bytes(BKV *b) { return dir_bytes(b->dir); }
static int l_sync(BKV *b) { return mdb_env_sync(b->env, 1) ? -1 : 0; }
static int l_hold(BKV *b) { return mdb_txn_begin(b->env, NULL, MDB_RDONLY, &b->lheld) ? -1 : 0; }
static void l_release(BKV *b) { if (b->lheld) mdb_txn_abort(b->lheld); b->lheld = NULL; }
static int l_fetch(BKV *b, const void *k, size_t kn, int fl, unsigned char *ko, size_t *kon, unsigned char *vo, size_t vcap,
                   size_t *von) {
    MDB_val K = {kn, (void *)k}, V;
    if (!fl) {
        int rc = mdb_get(b->lt, b->dbi, &K, &V);
        if (rc == MDB_NOTFOUND) return 1;
        if (rc) return -1;
        fetch_copy(k, kn, V.mv_data, V.mv_size, ko, kon, vo, vcap, von);
        return 0;
    }
    MDB_cursor *c;
    if (mdb_cursor_open(b->lt, b->dbi, &c)) return -1;
    /* the first key >= k; then back one unless it is k itself */
    int rc = mdb_cursor_get(c, &K, &V, MDB_SET_RANGE);
    if (rc == MDB_NOTFOUND) rc = mdb_cursor_get(c, &K, &V, MDB_LAST);
    else if (rc == 0 && (K.mv_size != kn || memcmp(K.mv_data, k, kn))) rc = mdb_cursor_get(c, &K, &V, MDB_PREV);
    if (rc == 0) fetch_copy(K.mv_data, K.mv_size, V.mv_data, V.mv_size, ko, kon, vo, vcap, von);
    mdb_cursor_close(c);
    return rc == MDB_NOTFOUND ? 1 : rc ? -1 : 0;
}
static const BEngine lmdb_engine = {"lmdb", l_open, l_close, l_begin, l_put, l_get, l_del, l_scan, l_end, l_counters, l_file_bytes,
                                    l_sync, l_hold, l_release, l_fetch};
#endif

/* ---- RocksDB: the LSM reference ------------------------------------------- */

#ifdef HAVE_ROCKSDB
static BKV *r_open(const char *dir, uint64_t cache, uint32_t page) {
    BKV *b = calloc(1, sizeof *b);
    snprintf(b->dir, sizeof b->dir, "%s", dir);
    b->ropt = rocksdb_options_create();
    rocksdb_options_set_create_if_missing(b->ropt, 1);
    rocksdb_block_based_table_options_t *t = rocksdb_block_based_options_create();
    rocksdb_block_based_options_set_block_size(t, page);
    rocksdb_cache_t *c = rocksdb_cache_create_lru(cache);
    rocksdb_block_based_options_set_block_cache(t, c);
    rocksdb_block_based_options_set_filter_policy(t, rocksdb_filterpolicy_create_bloom(10));
    rocksdb_options_set_block_based_table_factory(b->ropt, t);
    char *err = NULL;
    b->rdb = rocksdb_open(b->ropt, dir, &err);
    if (err) { fprintf(stderr, "rocksdb: %s\n", err); free(b); return NULL; }
    b->wsync = rocksdb_writeoptions_create();
    rocksdb_writeoptions_set_sync(b->wsync, 1); /* every commit durable, as the others */
    b->wnosync = rocksdb_writeoptions_create();
    b->ropts = rocksdb_readoptions_create();
    b->batch = rocksdb_writebatch_create();
    return b;
}
static void r_close(BKV *b) { rocksdb_close(b->rdb); free(b); }
static int r_begin(BKV *b, int write) { b->write = write; rocksdb_writebatch_clear(b->batch); return 0; }
static int r_put(BKV *b, const void *k, size_t kn, const void *v, size_t vn) { rocksdb_writebatch_put(b->batch, k, kn, v, vn); return 0; }
static int r_get(BKV *b, const void *k, size_t kn, size_t *vn) {
    char *err = NULL;
    char *v = rocksdb_get(b->rdb, b->ropts, k, kn, vn, &err);
    if (err) return -1;
    if (!v) return 1;
    rocksdb_free(v);
    return 0;
}
static int r_del(BKV *b, const void *k, size_t kn) { rocksdb_writebatch_delete(b->batch, k, kn); return 0; }
static int r_scan(BKV *b, const void *k, size_t kn, int count, int *got, uint64_t *bytes, ScanFn fn, void *arg) {
    rocksdb_iterator_t *it = rocksdb_create_iterator(b->rdb, b->ropts);
    *got = 0;
    for (rocksdb_iter_seek(it, k, kn); rocksdb_iter_valid(it) && *got < count; rocksdb_iter_next(it)) {
        size_t vn;
        const char *v = rocksdb_iter_value(it, &vn);
        if (fn) fn(arg, (const unsigned char *)v, vn);
        *bytes += vn;
        (*got)++;
    }
    rocksdb_iter_destroy(it);
    return 0;
}
static int r_end(BKV *b, int commit, int sync) {
    if (!b->write || !commit) return 0;
    char *err = NULL;
    rocksdb_write(b->rdb, sync ? b->wsync : b->wnosync, b->batch, &err);
    rocksdb_writebatch_clear(b->batch);
    return err ? -1 : 0;
}
static void r_counters(BKV *b, uint64_t *fl, uint64_t *pr) { (void)b; *fl = (uint64_t)-1; *pr = (uint64_t)-1; }
static uint64_t r_file_bytes(BKV *b) { return dir_bytes(b->dir); }
static int r_sync(BKV *b) { char *err = NULL; rocksdb_flush_wal(b->rdb, 1, &err); return err ? -1 : 0; }
static int r_hold(BKV *b) { b->rsnap = rocksdb_create_snapshot(b->rdb); return 0; }
static void r_release(BKV *b) { if (b->rsnap) rocksdb_release_snapshot(b->rdb, b->rsnap); b->rsnap = NULL; }
static int r_fetch(BKV *b, const void *k, size_t kn, int fl, unsigned char *ko, size_t *kon, unsigned char *vo, size_t vcap,
                   size_t *von) {
    /* reads the committed store, not the open batch, as r_get does: every
     * caller here reads a key before its transaction writes it */
    if (!fl) {
        char *err = NULL;
        size_t vn;
        char *v = rocksdb_get(b->rdb, b->ropts, k, kn, &vn, &err);
        if (err) return -1;
        if (!v) return 1;
        fetch_copy(k, kn, v, vn, ko, kon, vo, vcap, von);
        rocksdb_free(v);
        return 0;
    }
    rocksdb_iterator_t *it = rocksdb_create_iterator(b->rdb, b->ropts);
    rocksdb_iter_seek_for_prev(it, k, kn);
    int out = 1;
    if (rocksdb_iter_valid(it)) {
        size_t K, V;
        const char *kp = rocksdb_iter_key(it, &K), *vp = rocksdb_iter_value(it, &V);
        fetch_copy(kp, K, vp, V, ko, kon, vo, vcap, von);
        out = 0;
    }
    rocksdb_iter_destroy(it);
    return out;
}
static const BEngine rocksdb_engine = {"rocksdb", r_open, r_close, r_begin, r_put, r_get, r_del, r_scan, r_end, r_counters, r_file_bytes,
                                       r_sync, r_hold, r_release, r_fetch};
#endif

/* ---- measurement ----------------------------------------------------------- */

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

typedef struct {
    double *v;
    size_t n, cap;
} Lat;
static void lat_add(Lat *l, double x) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 4096; l->v = realloc(l->v, l->cap * sizeof *l->v); }
    l->v[l->n++] = x;
}
static int dcmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static double lat_pct(Lat *l, double p) {
    if (!l->n) return 0;
    size_t i = (size_t)(p / 100.0 * (double)(l->n - 1) + 0.5);
    return l->v[i];
}

static uint64_t proc_io(const char *field) {
    FILE *f = fopen("/proc/self/io", "r");
    if (!f) return 0;
    char line[128];
    uint64_t v = 0;
    size_t fl = strlen(field);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, field, fl) && line[fl] == ':') { v = strtoull(line + fl + 1, NULL, 10); break; }
    fclose(f);
    return v;
}
static uint64_t peak_rss_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[128];
    uint64_t v = 0;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "VmHWM:", 6)) { v = strtoull(line + 6, NULL, 10); break; }
    fclose(f);
    return v;
}

/* ---- YCSB's key and request distributions ---------------------------------- */

static uint64_t fnv64(uint64_t x) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (int i = 0; i < 8; i++) { h ^= x & 0xFF; h *= 1099511628211ULL; x >>= 8; }
    return h;
}

/* "user" + the FNV hash of the record number, as YCSB builds keys: the load
 * inserts in record order, which is random order in key space */
static int sorted_keys; /* w4-sorted: key order is insertion order */
static size_t ycsb_key(char *out, uint64_t keynum) {
    uint64_t n = sorted_keys ? keynum : fnv64(keynum) % 10000000000000000000ULL;
    return (size_t)snprintf(out, 32, "user%019llu", (unsigned long long)n);
}

static uint64_t rng_state = 0x853c49e6748fea9bULL;
static uint64_t rnd(void) {
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static double rnd01(void) { return (double)(rnd() >> 11) * (1.0 / 9007199254740992.0); }

/* Gray et al.'s Zipfian generator, as YCSB's ZipfianGenerator, theta 0.99 */
typedef struct {
    uint64_t items;
    double theta, zetan, alpha, eta, zeta2;
} Zipf;
static double zeta(uint64_t n, double theta) {
    double s = 0;
    for (uint64_t i = 1; i <= n; i++) s += 1.0 / pow(i, theta);
    return s;
}
static void zipf_init(Zipf *z, uint64_t items) {
    z->items = items;
    z->theta = 0.99;
    z->zeta2 = zeta(2, z->theta);
    z->zetan = zeta(items, z->theta);
    z->alpha = 1.0 / (1.0 - z->theta);
    z->eta = (1 - pow(2.0 / (double)items, 1 - z->theta)) / (1 - z->zeta2 / z->zetan);
}
static uint64_t zipf_next(Zipf *z) {
    double u = rnd01(), uz = u * z->zetan;
    if (uz < 1.0) return 0;
    if (uz < 1.0 + pow(0.5, z->theta)) return 1;
    return (uint64_t)((double)z->items * pow(z->eta * u - z->eta + 1, z->alpha));
}
/* scrambled: popular items spread over the key space, as YCSB's default */
static uint64_t scrambled(Zipf *z) { return fnv64(zipf_next(z)) % z->items; }

/* ---- the run --------------------------------------------------------------- */

#define VALUE 1000 /* YCSB: ten fields of 100 bytes */

typedef struct {
    const BEngine *e;
    BKV *kv;
    Lat read, write, scan;
    uint64_t logical_written, ops, errors, notfound;
    uint64_t records;
} Run;

static size_t vsize = VALUE;       /* W8 sets 100 KiB */
static unsigned char *value;

/* Random printable characters, as YCSB's RandomByteIterator. The first
 * version wrote (seed * 31 + i * 7), a pattern with period 256 that Snappy
 * compresses to almost nothing -- so RocksDB, the one engine here that
 * compresses, reported a space amplification of 0.44 and wrote almost no
 * SST bytes, figures it had not earned. */
static void fill_random(unsigned char *out, size_t n, uint64_t seed) {
    uint64_t x = seed * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL;
    for (size_t i = 0; i < n; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        out[i] = (unsigned char)(' ' + (x >> 32) % 95);
    }
}
static void fill_value(uint64_t seed) { fill_random(value, vsize, seed); }

/* ---- the version store (§4) -------------------------------------------------
 *
 * One implementation over the interface above, so every engine pays for the
 * same layout and a difference between engines is the engine's. A table is a
 * leading byte of the key here, as the application workloads below do, so the
 * version store's two key spaces sort apart from the rows ("user...") without
 * a tree of their own:
 *
 *   row         key -> header(start id, commit second, author) + the row
 *   history     'h' key col start-id -> the old header + the old column
 *   commit time 'c' second -> the last commit id of that second
 *
 * The current value lives in the row, so an ordinary read never touches
 * history. An update moves the old temporal column, with the header that
 * says which commit made it, to history under the id it STARTED at; the row
 * keeps no pointer. AS OF commit A is then the row if its start is <= A, and
 * otherwise the greatest history key <= (row, col, A) -- a version found
 * there started at or before A, and the next one started after A, so it is
 * the one that was current. AS OF a second maps through the commit-time
 * index: commit ids and commit seconds both only grow, so the floor of a
 * second is the last commit made by then. One entry per SECOND, not per
 * commit, because the clock is second-resolution and that is the most the
 * index can say; it is overwritten by every commit in that second.
 *
 * Commit ids are the version store's own here: the reference engines have
 * none, and a candidate's id is known only after commit while the version
 * must be written inside the transaction. With one writer the next id is
 * known at begin, which is what the engine will hand the version store when
 * it moves in (step 5). The counter is recovered at open from the
 * commit-time index's last entry, so it needs no key of its own. Id 0 is the
 * load. Field 0 of the YCSB row is the temporal column (W9: "W2 with a
 * temporal column"); the rest of the row is not versioned. */

#define VS_HDR 20   /* start id (8), commit second (8), author (4) */
#define VS_FIELD 100
static int temporal;          /* the w9 workloads: rows carry a header */
static uint64_t vs_last;      /* the last commit id handed out */
static uint64_t vs_clock;     /* nonzero: the commit second, set by the check */
static unsigned char *vs_row; /* VS_HDR + vsize */

static void vs_be64(unsigned char *p, uint64_t x) { for (int i = 7; i >= 0; i--, x >>= 8) p[i] = (unsigned char)x; }
static uint64_t vs_rd64(const unsigned char *p) { uint64_t x = 0; for (int i = 0; i < 8; i++) x = x << 8 | p[i]; return x; }
static uint64_t vs_now(void) { return vs_clock ? vs_clock : (uint64_t)time(NULL); }
static size_t vs_hkey(unsigned char *o, const char *k, size_t kn, int col, uint64_t start) {
    o[0] = 'h';
    memcpy(o + 1, k, kn);
    o[1 + kn] = (unsigned char)col;
    vs_be64(o + 2 + kn, start);
    return kn + 10;
}
static size_t vs_ckey(unsigned char *o, uint64_t sec) { o[0] = 'c'; vs_be64(o + 1, sec); return 9; }

/* writes the row (header + `value`), first moving its old temporal column to
 * history; inside a write transaction */
static int vs_write(Run *r, const char *k, size_t kn, uint64_t id, uint64_t sec, int archive) {
    unsigned char ko[64], old[VS_HDR + VS_FIELD], hk[64];
    size_t kon, von;
    if (archive) {
        int rc = r->e->fetch(r->kv, k, kn, 0, ko, &kon, old, sizeof old, &von);
        if (rc < 0) return rc;
        if (rc == 0) {
            if (von < sizeof old) return -1;
            size_t hn = vs_hkey(hk, k, kn, 0, vs_rd64(old));
            if (r->e->put(r->kv, hk, hn, old, sizeof old)) return -1;
            r->logical_written += hn + sizeof old;
        }
    }
    vs_be64(vs_row, id);
    vs_be64(vs_row + 8, sec);
    vs_row[16] = 0, vs_row[17] = 0, vs_row[18] = 0, vs_row[19] = 1; /* one author: the bench's principal */
    memcpy(vs_row + VS_HDR, value, vsize);
    return r->e->put(r->kv, k, kn, vs_row, VS_HDR + vsize);
}
/* records that commit `id` happened in second `sec` */
static int vs_mark(Run *r, uint64_t id, uint64_t sec) {
    unsigned char ck[9], v[8];
    vs_be64(v, id);
    return r->e->put(r->kv, ck, vs_ckey(ck, sec), v, 8);
}
/* the last commit id made by the end of second `sec`; 0 (the load) if none */
static int vs_id_at(Run *r, uint64_t sec, uint64_t *id) {
    unsigned char ck[9], ko[64], v[8];
    size_t kon, von;
    int rc = r->e->fetch(r->kv, ck, vs_ckey(ck, sec), 1, ko, &kon, v, sizeof v, &von);
    if (rc < 0) return rc;
    *id = rc == 0 && kon == 9 && ko[0] == 'c' && von == 8 ? vs_rd64(v) : 0;
    return 0;
}
/* the counter, from the commit-time index's last entry */
static int vs_recover(Run *r) {
    if (r->e->begin(r->kv, 0)) return -1;
    int rc = vs_id_at(r, UINT64_MAX, &vs_last);
    r->e->end(r->kv, 0, 0);
    return rc;
}
/* the temporal column (with its header) as of commit `asof`: 0 found, 1 the
 * row did not exist then, <0 error */
static int vs_read_asof(Run *r, const char *k, size_t kn, uint64_t asof, unsigned char out[VS_HDR + VS_FIELD]) {
    unsigned char ko[64], hk[64];
    size_t kon, von;
    int rc = r->e->fetch(r->kv, k, kn, 0, ko, &kon, out, VS_HDR + VS_FIELD, &von);
    if (rc < 0) return rc;
    if (rc == 0 && von >= VS_HDR + VS_FIELD && vs_rd64(out) <= asof) return 0;
    size_t hn = vs_hkey(hk, k, kn, 0, asof);
    rc = r->e->fetch(r->kv, hk, hn, 1, ko, &kon, out, VS_HDR + VS_FIELD, &von);
    if (rc) return rc;
    /* another row's history, or before this row's first version */
    if (kon != hn || memcmp(ko, hk, hn - 8) || von != VS_HDR + VS_FIELD) return 1;
    return 0;
}

/* W9: W2's durable single-row update, with the temporal column versioned */
static void op_update_temporal(Run *r, uint64_t keynum) {
    char k[32];
    size_t kn = ycsb_key(k, keynum);
    fill_value(keynum + r->ops);
    double t0 = now_us();
    r->e->begin(r->kv, 1);
    uint64_t id = ++vs_last, sec = vs_now();
    if (vs_write(r, k, kn, id, sec, 1) || vs_mark(r, id, sec)) r->errors++;
    if (r->e->end(r->kv, 1, 1)) r->errors++;
    lat_add(&r->write, now_us() - t0);
    r->logical_written += kn + vsize; /* the row only: history counts as overhead */
    r->ops++;
}

/* w9-asof-D: give the first K rows D versions each (unmeasured, in
 * transactions of K updates, synced once at the end), then time AS OF reads
 * at a commit id chosen uniformly over the whole history */
static int build_history(Run *r, uint64_t k_rows, int depth) {
    char k[32];
    for (int d = 0; d < depth; d++) {
        if (r->e->begin(r->kv, 1)) return -1;
        uint64_t id = ++vs_last, sec = vs_now();
        for (uint64_t j = 0; j < k_rows; j++) {
            size_t kn = ycsb_key(k, j);
            fill_value(j * 7919 + (uint64_t)d);
            if (vs_write(r, k, kn, id, sec, 1)) return -1;
        }
        if (vs_mark(r, id, sec) || r->e->end(r->kv, 1, 0)) return -1;
    }
    return r->e->sync(r->kv);
}
static void op_read_asof(Run *r, uint64_t keynum, uint64_t asof) {
    char k[32];
    unsigned char out[VS_HDR + VS_FIELD];
    size_t kn = ycsb_key(k, keynum);
    double t0 = now_us();
    r->e->begin(r->kv, 0);
    int rc = vs_read_asof(r, k, kn, asof, out);
    if (rc < 0) r->errors++;
    else if (rc == 1) r->notfound++;
    else if (vs_rd64(out) > asof) r->errors++;
    r->e->end(r->kv, 0, 0);
    lat_add(&r->read, now_us() - t0);
    r->ops++;
}

static int do_insert_batch(Run *r, uint64_t from, uint64_t to, size_t vn) {
    char k[32];
    if (r->e->begin(r->kv, 1)) return -1;
    for (uint64_t i = from; i < to; i++) {
        size_t kn = ycsb_key(k, i);
        fill_value(i);
        if (temporal ? vs_write(r, k, kn, 0, vs_now(), 0) : r->e->put(r->kv, k, kn, value, vn)) r->errors++;
        r->logical_written += kn + vn;
    }
    return r->e->end(r->kv, 1, 1);
}

static void op_read(Run *r, uint64_t keynum) {
    char k[32];
    size_t kn = ycsb_key(k, keynum), vn;
    double t0 = now_us();
    r->e->begin(r->kv, 0);
    int g = r->e->get(r->kv, k, kn, &vn);
    r->e->end(r->kv, 0, 0);
    lat_add(&r->read, now_us() - t0);
    if (g < 0) r->errors++;
    if (g == 1) r->notfound++;
    r->ops++;
}

static int group_every; /* group-N: commit without sync, sync every N commits */

static void op_update(Run *r, uint64_t keynum, int rmw) {
    char k[32];
    size_t kn = ycsb_key(k, keynum), vn;
    fill_value(keynum + r->ops);
    double t0 = now_us();
    r->e->begin(r->kv, 1);
    if (rmw) r->e->get(r->kv, k, kn, &vn);
    if (r->e->put(r->kv, k, kn, value, vsize)) r->errors++;
    int sync = !group_every;
    if (r->e->end(r->kv, 1, sync)) r->errors++;
    if (group_every && (r->ops + 1) % (uint64_t)group_every == 0 && r->e->sync(r->kv)) r->errors++;
    lat_add(&r->write, now_us() - t0);
    r->logical_written += kn + vsize;
    r->ops++;
}

/* W3: one durable transaction of `n` updates at random keys; the latency
 * recorded is the whole transaction's, and each row counts as one op */
static void op_batch(Run *r, uint64_t records, int n) {
    char k[32];
    double t0 = now_us();
    r->e->begin(r->kv, 1);
    for (int j = 0; j < n; j++) {
        uint64_t keynum = rnd() % records;
        size_t kn = ycsb_key(k, keynum);
        fill_value(keynum + r->ops);
        if (r->e->put(r->kv, k, kn, value, vsize)) r->errors++;
        r->logical_written += kn + vsize;
        r->ops++;
    }
    if (r->e->end(r->kv, 1, 1)) r->errors++;
    lat_add(&r->write, now_us() - t0);
}

static void op_scan(Run *r, uint64_t keynum, int len) {
    char k[32];
    size_t kn = ycsb_key(k, keynum);
    int got;
    uint64_t bytes = 0;
    double t0 = now_us();
    r->e->begin(r->kv, 0);
    if (r->e->scan(r->kv, k, kn, len, &got, &bytes, NULL, NULL)) r->errors++;
    r->e->end(r->kv, 0, 0);
    lat_add(&r->scan, now_us() - t0);
    r->ops++;
}

static void print_lat(const char *name, Lat *l) {
    if (!l->n) return;
    qsort(l->v, l->n, sizeof *l->v, dcmp);
    double sum = 0;
    for (size_t i = 0; i < l->n; i++) sum += l->v[i];
    printf(", \"%s\": {\"n\": %zu, \"mean_us\": %.1f, \"p50_us\": %.1f, \"p99_us\": %.1f, \"p999_us\": %.1f}", name, l->n,
           sum / (double)l->n, lat_pct(l, 50), lat_pct(l, 99), lat_pct(l, 99.9));
}

/* ---- the application workloads: TPC-C-like and SSB flight 1 (§5.1.0) ------
 *
 * Both run over the same one-tree interface as YCSB: a table is a leading
 * byte of the key, then fixed-width big-endian integers, so key order is
 * (table, columns...) order and every engine sees identical keys.
 *
 * TPC-C-like: the five transactions in the standard mix, each one write (or
 * read) transaction with a synced commit, no think times -- so its rate is NOT
 * a tpmC and is never reported as one. The interface's `get` answers a size
 * and not the bytes, so the few values a transaction must read back (a
 * district's next order id, a stock quantity, an order's line count) are kept
 * in the harness's memory; every row is still read and written through the
 * engine with its real size, which is what a storage measurement needs.
 *
 * SSB flight 1: the lineorder fact table and the date dimension, generated
 * with dbgen's distributions; Q1.1-Q1.3 run as a hash join (build from a scan
 * of date, probe with a full scan of lineorder) summing extendedprice x
 * discount. The harness computes each answer while generating the data, and
 * every engine's answer is checked against it. */

static void be32(unsigned char *p, uint32_t x) { p[0] = (unsigned char)(x >> 24); p[1] = (unsigned char)(x >> 16); p[2] = (unsigned char)(x >> 8); p[3] = (unsigned char)x; }
static void le32(unsigned char *p, uint32_t x) { p[0] = (unsigned char)x; p[1] = (unsigned char)(x >> 8); p[2] = (unsigned char)(x >> 16); p[3] = (unsigned char)(x >> 24); }
static uint32_t rd_le32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t urand(uint64_t lo, uint64_t hi) { return lo + rnd() % (hi - lo + 1); }

/* a key: table byte then up to four big-endian u32 columns */
static size_t tkey(unsigned char *k, char table, int ncols, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t col[4] = {a, b, c, d};
    k[0] = (unsigned char)table;
    for (int i = 0; i < ncols; i++) be32(k + 1 + 4 * i, col[i]);
    return 1 + 4 * (size_t)ncols;
}

static unsigned char rowbuf[1024];
static void fill_row(size_t n, uint64_t seed) { fill_random(rowbuf, n, seed); }

/* the load: puts batched 1000 to a synced commit, as the YCSB load */
typedef struct { Run *r; int n; } Loader;
static int ld_put(Loader *l, const unsigned char *k, size_t kn, const unsigned char *v, size_t vn) {
    if (l->n == 0 && l->r->e->begin(l->r->kv, 1)) return -1;
    if (l->r->e->put(l->r->kv, k, kn, v, vn)) l->r->errors++;
    l->r->logical_written += kn + vn;
    if (++l->n == 1000) { l->n = 0; return l->r->e->end(l->r->kv, 1, 1); }
    return 0;
}
static int ld_done(Loader *l) { if (!l->n) return 0; l->n = 0; return l->r->e->end(l->r->kv, 1, 1); }

/* a put inside a measured transaction */
static void tput(Run *r, const unsigned char *k, size_t kn, size_t vn, uint64_t seed) {
    fill_row(vn, seed);
    if (r->e->put(r->kv, k, kn, rowbuf, vn)) r->errors++;
    r->logical_written += kn + vn;
}
static int tget(Run *r, const unsigned char *k, size_t kn) {
    size_t vn;
    int g = r->e->get(r->kv, k, kn, &vn);
    if (g < 0) r->errors++;
    if (g == 1) r->notfound++;
    return g;
}

/* ---- TPC-C-like ---- */

enum { T_ITEMS = 100000, T_DIST = 10, T_CUST = 3000, T_ORD0 = 3000, T_NEWORD0 = 900 };
enum { S_WARE = 89, S_DIST = 95, S_CUST = 655, S_HIST = 46, S_ORD = 24, S_NEWORD = 12, S_OL = 54, S_ITEM = 82, S_STOCK = 306 };

static uint32_t nurand_c255 = 157, nurand_c1023 = 223, nurand_c8191 = 4111;
static uint32_t nurand(uint32_t a, uint32_t x, uint32_t y, uint32_t c) { return (uint32_t)(((urand(0, a) | urand(x, y)) + c) % (y - x + 1) + x); }
/* a last name as a number (spec: three syllables chosen by its digits) */
static uint32_t lastname_num(uint32_t n) { return n % 1000; }

typedef struct {
    uint32_t W;
    uint32_t *next_o;          /* [w*10+d] next order id */
    uint32_t *oldest_new;      /* [w*10+d] oldest undelivered order id */
    uint8_t **ol_cnt;          /* [w*10+d][o] line count */
    uint32_t **ol_item;        /* [w*10+d][o*15+ol] item id (for Stock-Level) */
    uint32_t *cust_last;       /* [(w*10+d)*3000+c-1] last order id */
    uint16_t *cust_name;       /* [(w*10+d)*3000+c-1] last name number */
    int16_t *stock_qty;        /* [w*100000+i-1] */
    uint32_t cap;              /* orders per district held */
    uint64_t hist_seq;
} Tpcc;

static int tpcc_load(Run *r, Tpcc *t) {
    Loader l = {r, 0};
    unsigned char k[24], v[16];
    for (uint32_t i = 1; i <= T_ITEMS; i++) { fill_row(S_ITEM, i); if (ld_put(&l, k, tkey(k, 'I', 1, i, 0, 0, 0), rowbuf, S_ITEM)) return -1; }
    for (uint32_t w = 0; w < t->W; w++) {
        fill_row(S_WARE, w);
        if (ld_put(&l, k, tkey(k, 'W', 1, w, 0, 0, 0), rowbuf, S_WARE)) return -1;
        for (uint32_t i = 1; i <= T_ITEMS; i++) {
            t->stock_qty[w * T_ITEMS + i - 1] = (int16_t)urand(10, 100);
            fill_row(S_STOCK, i);
            if (ld_put(&l, k, tkey(k, 'S', 2, w, i, 0, 0), rowbuf, S_STOCK)) return -1;
        }
        for (uint32_t d = 1; d <= T_DIST; d++) {
            uint32_t wd = w * T_DIST + d - 1;
            fill_row(S_DIST, wd);
            if (ld_put(&l, k, tkey(k, 'D', 2, w, d, 0, 0), rowbuf, S_DIST)) return -1;
            for (uint32_t c = 1; c <= T_CUST; c++) {
                uint32_t name = c <= 1000 ? c - 1 : nurand(255, 0, 999, nurand_c255);
                t->cust_name[wd * T_CUST + c - 1] = (uint16_t)name;
                fill_row(S_CUST, c);
                if (ld_put(&l, k, tkey(k, 'C', 3, w, d, c, 0), rowbuf, S_CUST)) return -1;
                /* the last-name index: value carries the name and the id, so a
                 * scan can tell where one name's entries end */
                le32(v, name); le32(v + 4, c);
                if (ld_put(&l, k, tkey(k, 'N', 4, w, d, lastname_num(name), c), v, 8)) return -1;
                fill_row(S_HIST, c);
                if (ld_put(&l, k, tkey(k, 'H', 2, (uint32_t)(t->hist_seq >> 32), (uint32_t)t->hist_seq, 0, 0), rowbuf, S_HIST)) return -1;
                t->hist_seq++;
            }
            /* orders 1..3000, a random permutation of customers */
            uint32_t *perm = malloc(T_CUST * sizeof *perm);
            for (uint32_t c = 0; c < T_CUST; c++) perm[c] = c + 1;
            for (uint32_t c = T_CUST - 1; c > 0; c--) { uint32_t j = (uint32_t)(rnd() % (c + 1)), x = perm[c]; perm[c] = perm[j]; perm[j] = x; }
            for (uint32_t o = 1; o <= T_ORD0; o++) {
                uint32_t cnt = (uint32_t)urand(5, 15);
                t->ol_cnt[wd][o] = (uint8_t)cnt;
                t->cust_last[wd * T_CUST + perm[o - 1] - 1] = o;
                fill_row(S_ORD, o);
                if (ld_put(&l, k, tkey(k, 'O', 3, w, d, o, 0), rowbuf, S_ORD)) return -1;
                if (o > T_ORD0 - T_NEWORD0) {
                    le32(v, w); le32(v + 4, d); le32(v + 8, o);
                    if (ld_put(&l, k, tkey(k, 'E', 3, w, d, o, 0), v, S_NEWORD)) return -1;
                }
                for (uint32_t ol = 1; ol <= cnt; ol++) {
                    uint32_t item = (uint32_t)urand(1, T_ITEMS);
                    t->ol_item[wd][o * 15 + ol - 1] = item;
                    fill_row(S_OL, item);
                    le32(rowbuf, item); le32(rowbuf + 4, w); le32(rowbuf + 8, d); le32(rowbuf + 12, o);
                    if (ld_put(&l, k, tkey(k, 'L', 4, w, d, o, ol), rowbuf, S_OL)) return -1;
                }
            }
            free(perm);
            t->next_o[wd] = T_ORD0 + 1;
            t->oldest_new[wd] = T_ORD0 - T_NEWORD0 + 1;
        }
    }
    return ld_done(&l);
}

/* scan callbacks */
typedef struct { uint32_t name, ids[64]; int n; } NameScan;
static void cb_name(void *arg, const unsigned char *v, size_t vn) {
    NameScan *s = arg;
    if (vn >= 8 && rd_le32(v) == s->name && s->n < 64) s->ids[s->n++] = rd_le32(v + 4);
}
typedef struct { uint32_t w, d, o; int found; } NewOrdScan;
static void cb_neword(void *arg, const unsigned char *v, size_t vn) {
    NewOrdScan *s = arg;
    if (!s->found && vn >= 12 && rd_le32(v) == s->w && rd_le32(v + 4) == s->d) { s->o = rd_le32(v + 8); s->found = 1; }
}
typedef struct { uint32_t w, d, lo, hi; uint32_t items[512]; int n; } OlScan;
static void cb_ol(void *arg, const unsigned char *v, size_t vn) {
    OlScan *s = arg;
    if (vn < 16 || rd_le32(v + 4) != s->w || rd_le32(v + 8) != s->d) return;
    uint32_t o = rd_le32(v + 12);
    if (o >= s->lo && o < s->hi && s->n < 512) s->items[s->n++] = rd_le32(v);
}

/* the customer a transaction is about: 60% by last name (an index scan, the
 * middle match), otherwise by id */
static uint32_t tpcc_customer(Run *r, Tpcc *t, uint32_t w, uint32_t d) {
    (void)t;
    if (urand(1, 100) > 60) return nurand(1023, 1, T_CUST, nurand_c1023);
    NameScan s = {nurand(255, 0, 999, nurand_c255), {0}, 0};
    unsigned char k[24];
    int got;
    uint64_t bytes = 0;
    if (r->e->scan(r->kv, k, tkey(k, 'N', 3, w, d, lastname_num(s.name), 0), 64, &got, &bytes, cb_name, &s)) r->errors++;
    if (!s.n) return nurand(1023, 1, T_CUST, nurand_c1023); /* a name nobody has */
    return s.ids[(s.n - 1) / 2];
}

static Lat tx_lat[5];
static uint64_t tx_n[5], tx_rollback, tx_delivered;
static const char *tx_name[5] = {"new_order", "payment", "order_status", "delivery", "stock_level"};

static void tpcc_tx(Run *r, Tpcc *t) {
    unsigned char k[24];
    double u = rnd01(), t0 = now_us();
    uint32_t w = (uint32_t)(rnd() % t->W), d = (uint32_t)urand(1, T_DIST), wd = w * T_DIST + d - 1;
    int kind = u < 0.45 ? 0 : u < 0.88 ? 1 : u < 0.92 ? 2 : u < 0.96 ? 3 : 4;
    if (kind == 0) { /* New-Order */
        uint32_t c = nurand(1023, 1, T_CUST, nurand_c1023), cnt = (uint32_t)urand(5, 15), o = t->next_o[wd];
        int rollback = urand(1, 100) == 1;
        r->e->begin(r->kv, 1);
        tget(r, k, tkey(k, 'W', 1, w, 0, 0, 0));
        tget(r, k, tkey(k, 'D', 2, w, d, 0, 0));
        tput(r, k, tkey(k, 'D', 2, w, d, 0, 0), S_DIST, o);
        tget(r, k, tkey(k, 'C', 3, w, d, c, 0));
        tput(r, k, tkey(k, 'O', 3, w, d, o, 0), S_ORD, o);
        unsigned char v[12];
        le32(v, w); le32(v + 4, d); le32(v + 8, o);
        if (r->e->put(r->kv, k, tkey(k, 'E', 3, w, d, o, 0), v, S_NEWORD)) r->errors++;
        uint32_t items[15];
        int ok = 1;
        for (uint32_t ol = 1; ol <= cnt && ok; ol++) {
            uint32_t item = (rollback && ol == cnt) ? T_ITEMS + 1 : nurand(8191, 1, T_ITEMS, nurand_c8191);
            if (tget(r, k, tkey(k, 'I', 1, item, 0, 0, 0)) == 1) { ok = 0; break; } /* the 1% unused item: roll back */
            items[ol - 1] = item;
            tget(r, k, tkey(k, 'S', 2, w, item, 0, 0));
            tput(r, k, tkey(k, 'S', 2, w, item, 0, 0), S_STOCK, item + o);
            fill_row(S_OL, item);
            le32(rowbuf, item); le32(rowbuf + 4, w); le32(rowbuf + 8, d); le32(rowbuf + 12, o);
            if (r->e->put(r->kv, k, tkey(k, 'L', 4, w, d, o, ol), rowbuf, S_OL)) r->errors++;
            r->logical_written += S_OL + 17;
        }
        if (!ok) { r->notfound--; r->e->end(r->kv, 0, 0); tx_rollback++; }
        else {
            if (r->e->end(r->kv, 1, 1)) r->errors++;
            if (o < t->cap) {
                t->next_o[wd] = o + 1;
                t->ol_cnt[wd][o] = (uint8_t)cnt;
                for (uint32_t i = 0; i < cnt; i++) {
                    t->ol_item[wd][o * 15 + i] = items[i];
                    int16_t *q = &t->stock_qty[w * T_ITEMS + items[i] - 1];
                    uint32_t qty = (uint32_t)urand(1, 10);
                    *q = (int16_t)(*q >= (int16_t)qty + 10 ? *q - (int16_t)qty : *q - (int16_t)qty + 91);
                }
                t->cust_last[wd * T_CUST + c - 1] = o;
            }
        }
    } else if (kind == 1) { /* Payment */
        r->e->begin(r->kv, 1);
        tget(r, k, tkey(k, 'W', 1, w, 0, 0, 0));
        tput(r, k, tkey(k, 'W', 1, w, 0, 0, 0), S_WARE, r->ops);
        tget(r, k, tkey(k, 'D', 2, w, d, 0, 0));
        tput(r, k, tkey(k, 'D', 2, w, d, 0, 0), S_DIST, r->ops);
        uint32_t c = tpcc_customer(r, t, w, d);
        tget(r, k, tkey(k, 'C', 3, w, d, c, 0));
        tput(r, k, tkey(k, 'C', 3, w, d, c, 0), S_CUST, r->ops);
        tput(r, k, tkey(k, 'H', 2, (uint32_t)(t->hist_seq >> 32), (uint32_t)t->hist_seq, 0, 0), S_HIST, t->hist_seq);
        t->hist_seq++;
        if (r->e->end(r->kv, 1, 1)) r->errors++;
    } else if (kind == 2) { /* Order-Status, read only */
        r->e->begin(r->kv, 0);
        uint32_t c = tpcc_customer(r, t, w, d);
        tget(r, k, tkey(k, 'C', 3, w, d, c, 0));
        uint32_t o = t->cust_last[wd * T_CUST + c - 1];
        if (o) {
            tget(r, k, tkey(k, 'O', 3, w, d, o, 0));
            int got;
            uint64_t bytes = 0;
            if (r->e->scan(r->kv, k, tkey(k, 'L', 4, w, d, o, 1), t->ol_cnt[wd][o], &got, &bytes, NULL, NULL)) r->errors++;
        }
        r->e->end(r->kv, 0, 0);
    } else if (kind == 3) { /* Delivery: every district of the warehouse */
        r->e->begin(r->kv, 1);
        for (uint32_t dd = 1; dd <= T_DIST; dd++) {
            uint32_t wdd = w * T_DIST + dd - 1;
            NewOrdScan s = {w, dd, 0, 0};
            int got;
            uint64_t bytes = 0;
            if (r->e->scan(r->kv, k, tkey(k, 'E', 2, w, dd, 0, 0), 1, &got, &bytes, cb_neword, &s)) r->errors++;
            if (!s.found) continue;
            if (s.o != t->oldest_new[wdd]) r->errors++; /* the engine and the harness disagree about the queue */
            int del = r->e->del(r->kv, k, tkey(k, 'E', 3, w, dd, s.o, 0));
            if (del) r->errors++;
            tget(r, k, tkey(k, 'O', 3, w, dd, s.o, 0));
            tput(r, k, tkey(k, 'O', 3, w, dd, s.o, 0), S_ORD, s.o + 1);
            uint32_t cnt = t->ol_cnt[wdd][s.o];
            if (r->e->scan(r->kv, k, tkey(k, 'L', 4, w, dd, s.o, 1), (int)cnt, &got, &bytes, NULL, NULL)) r->errors++;
            for (uint32_t ol = 1; ol <= cnt; ol++) {
                uint32_t item = t->ol_item[wdd][s.o * 15 + ol - 1];
                fill_row(S_OL, item + 1);
                le32(rowbuf, item); le32(rowbuf + 4, w); le32(rowbuf + 8, dd); le32(rowbuf + 12, s.o);
                if (r->e->put(r->kv, k, tkey(k, 'L', 4, w, dd, s.o, ol), rowbuf, S_OL)) r->errors++;
                r->logical_written += S_OL + 17;
            }
            uint32_t c = 1 + (uint32_t)(rnd() % T_CUST); /* the order's customer; which one does not change the I/O */
            tget(r, k, tkey(k, 'C', 3, w, dd, c, 0));
            tput(r, k, tkey(k, 'C', 3, w, dd, c, 0), S_CUST, s.o);
            t->oldest_new[wdd]++;
            tx_delivered++;
        }
        if (r->e->end(r->kv, 1, 1)) r->errors++;
    } else { /* Stock-Level, read only: distinct items of the last 20 orders */
        r->e->begin(r->kv, 0);
        tget(r, k, tkey(k, 'D', 2, w, d, 0, 0));
        uint32_t hi = t->next_o[wd], lo = hi > 20 ? hi - 20 : 1;
        OlScan s;
        s.w = w; s.d = d; s.lo = lo; s.hi = hi; s.n = 0;
        int got;
        uint64_t bytes = 0;
        if (r->e->scan(r->kv, k, tkey(k, 'L', 4, w, d, lo, 1), 320, &got, &bytes, cb_ol, &s)) r->errors++;
        uint32_t threshold = (uint32_t)urand(10, 20), low = 0;
        for (int i = 0; i < s.n; i++) {
            int dup = 0;
            for (int j = 0; j < i && !dup; j++) dup = s.items[j] == s.items[i];
            if (dup) continue;
            tget(r, k, tkey(k, 'S', 2, w, s.items[i], 0, 0));
            if (t->stock_qty[w * T_ITEMS + s.items[i] - 1] < (int16_t)threshold) low++;
        }
        (void)low;
        r->e->end(r->kv, 0, 0);
    }
    lat_add(&tx_lat[kind], now_us() - t0);
    tx_n[kind]++;
    r->ops++;
}

static int run_tpcc(Run *r, const char *engine, uint64_t ops, uint32_t page, uint64_t cache) {
    Tpcc t;
    memset(&t, 0, sizeof t);
    const char *ws = getenv("BENCH_WAREHOUSES");
    t.W = ws ? (uint32_t)atoi(ws) : 1;
    t.cap = T_ORD0 + 1 + (uint32_t)ops; /* every New-Order could land in one district */
    uint32_t nd = t.W * T_DIST;
    t.next_o = calloc(nd, sizeof *t.next_o);
    t.oldest_new = calloc(nd, sizeof *t.oldest_new);
    t.ol_cnt = calloc(nd, sizeof *t.ol_cnt);
    t.ol_item = calloc(nd, sizeof *t.ol_item);
    for (uint32_t i = 0; i < nd; i++) { t.ol_cnt[i] = calloc(t.cap + 1, 1); t.ol_item[i] = calloc((size_t)(t.cap + 1) * 15, sizeof(uint32_t)); }
    t.cust_last = calloc((size_t)nd * T_CUST, sizeof *t.cust_last);
    t.cust_name = calloc((size_t)nd * T_CUST, sizeof *t.cust_name);
    t.stock_qty = calloc((size_t)t.W * T_ITEMS, sizeof *t.stock_qty);

    double tl = now_us();
    if (tpcc_load(r, &t)) { fprintf(stderr, "tpcc load failed\n"); return 1; }
    double load_s = (now_us() - tl) / 1e6;
    uint64_t file_before = r->e->file_bytes(r->kv), loaded_logical = r->logical_written;
    r->logical_written = 0;
    r->errors = 0;
    uint64_t w0 = proc_io("wchar"), fl0, pr0, fl1, pr1;
    r->e->counters(r->kv, &fl0, &pr0);
    double t0 = now_us();
    for (uint64_t i = 0; i < ops; i++) tpcc_tx(r, &t);
    double secs = (now_us() - t0) / 1e6;
    r->e->counters(r->kv, &fl1, &pr1);
    uint64_t wrote = proc_io("wchar") - w0, fbytes = r->e->file_bytes(r->kv);
    r->e->close(r->kv);

    printf("{\"engine\": \"%s\", \"workload\": \"tpcc\", \"warehouses\": %u, \"page\": %u, \"cache\": %llu, \"load_s\": %.3f",
           engine, t.W, page, (unsigned long long)cache, load_s);
    printf(", \"ops\": %llu, \"secs\": %.3f, \"ops_s\": %.0f, \"new_order_per_min\": %.0f, \"rollbacks\": %llu, \"delivered\": %llu",
           (unsigned long long)r->ops, secs, (double)r->ops / secs, (double)tx_n[0] / secs * 60.0, (unsigned long long)tx_rollback,
           (unsigned long long)tx_delivered);
    printf(", \"bytes_written\": %llu, \"logical_written\": %llu, \"write_amp\": %.2f", (unsigned long long)wrote,
           (unsigned long long)r->logical_written, r->logical_written ? (double)wrote / (double)r->logical_written : 0.0);
    if (fl1 != (uint64_t)-1) printf(", \"flushes\": %llu, \"pages_read\": %llu", (unsigned long long)(fl1 - fl0), (unsigned long long)(pr1 - pr0));
    printf(", \"file_before\": %llu, \"file_bytes\": %llu, \"space_amp\": %.2f, \"file_growth\": %.2f, \"peak_rss_kb\": %llu, \"errors\": %llu",
           (unsigned long long)file_before, (unsigned long long)fbytes, (double)file_before / (double)loaded_logical,
           (double)fbytes / (double)file_before, (unsigned long long)peak_rss_kb(), (unsigned long long)r->errors);
    for (int i = 0; i < 5; i++) print_lat(tx_name[i], &tx_lat[i]);
    printf("}\n");
    return r->errors ? 1 : 0;
}

/* ---- SSB flight 1 ---- */

enum { SSB_LO = 100, SSB_DATE = 100 };
static int is_leap(int y) { return (y % 4 == 0 && y % 100) || y % 400 == 0; }
static int mdays(int y, int m) { static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; return d[m - 1] + (m == 2 && is_leap(y)); }
/* day index from 1992-01-01 back to a date */
typedef struct { int y, m, d, doy; } Ymd;
static Ymd day_to_ymd(int idx) {
    Ymd r = {1992, 1, 1, 1};
    while (idx >= 365 + is_leap(r.y)) { idx -= 365 + is_leap(r.y); r.y++; }
    r.doy = idx + 1;
    while (idx >= mdays(r.y, r.m)) { idx -= mdays(r.y, r.m); r.m++; }
    r.d = idx + 1;
    return r;
}
static uint32_t datekey(Ymd a) { return (uint32_t)(a.y * 10000 + a.m * 100 + a.d); }

#define SSB_DAYS 2557 /* 1992-01-01 .. 1998-12-31 */
typedef struct { uint32_t key; int year, yearmonthnum, week; } SsbDate;
static SsbDate ssb_dates[SSB_DAYS];

typedef struct { int year, ym, week, dlo, dhi, qlo, qhi; } SsbQuery;
static const SsbQuery ssb_q[3] = {
    {1993, 0, 0, 1, 3, 1, 24},   /* Q1.1: d_year = 1993, discount 1-3, quantity < 25 */
    {0, 199401, 0, 4, 6, 26, 35}, /* Q1.2: d_yearmonthnum = 199401, discount 4-6, quantity 26-35 */
    {1994, 0, 6, 5, 7, 26, 35},  /* Q1.3: week 6 of 1994, discount 5-7, quantity 26-35 */
};
static int ssb_date_match(const SsbQuery *q, const SsbDate *d) {
    return (!q->year || d->year == q->year) && (!q->ym || d->yearmonthnum == q->ym) && (!q->week || d->week == q->week);
}
/* dense date index: datekey -> slot */
static int date_slot(uint32_t key) { int y = (int)(key / 10000), m = (int)(key / 100 % 100), d = (int)(key % 100); return (y - 1992) * 372 + (m - 1) * 31 + (d - 1); }

typedef struct { const SsbQuery *q; unsigned char want[7 * 372]; uint64_t sum, rows, matched; } ProbeScan;
static void cb_date(void *arg, const unsigned char *v, size_t vn) {
    ProbeScan *s = arg;
    if (vn < 13) return;
    SsbDate d = {rd_le32(v), (int)(v[4] | v[5] << 8), (int)rd_le32(v + 6), (int)v[10]};
    if (ssb_date_match(s->q, &d)) s->want[date_slot(d.key)] = 1;
}
static void cb_lineorder(void *arg, const unsigned char *v, size_t vn) {
    ProbeScan *s = arg;
    s->rows++;
    if (vn < 33) return;
    uint32_t od = rd_le32(v + 17);
    int qty = v[23], disc = v[32];
    if (disc < s->q->dlo || disc > s->q->dhi || qty < s->q->qlo || qty > s->q->qhi || !s->want[date_slot(od)]) return;
    s->sum += (uint64_t)rd_le32(v + 24) * (uint64_t)disc;
    s->matched++;
}

static int run_ssb(Run *r, const char *engine, uint32_t page, uint64_t cache) {
    const char *sfs = getenv("BENCH_SSB_SF");
    double sf = sfs ? atof(sfs) : 1.0;
    uint32_t orders = (uint32_t)(1500000 * sf);
    uint64_t expect[3] = {0, 0, 0}, nrows = 0;
    Loader l = {r, 0};
    unsigned char k[16], v[SSB_LO];
    double tl = now_us();
    for (int i = 0; i < SSB_DAYS; i++) {
        Ymd a = day_to_ymd(i);
        SsbDate *d = &ssb_dates[i];
        d->key = datekey(a); d->year = a.y; d->yearmonthnum = a.y * 100 + a.m; d->week = (a.doy - 1) / 7 + 1;
        fill_row(SSB_DATE, (uint64_t)i);
        memcpy(v, rowbuf, SSB_DATE);
        le32(v, d->key); v[4] = (unsigned char)a.y; v[5] = (unsigned char)(a.y >> 8); le32(v + 6, (uint32_t)d->yearmonthnum); v[10] = (unsigned char)d->week;
        if (ld_put(&l, k, tkey(k, 'D', 1, d->key, 0, 0, 0), v, SSB_DATE)) return 1;
    }
    /* lineorder, in order-key order as dbgen writes it */
    for (uint32_t o = 1; o <= orders; o++) {
        int lines = (int)urand(1, 7);
        int od = (int)urand(0, 2405); /* 1992-01-01 .. 1998-08-02 */
        const SsbDate *d = &ssb_dates[od];
        uint32_t cust = (uint32_t)urand(1, (uint64_t)(30000 * sf) + 1);
        for (int ln = 1; ln <= lines; ln++) {
            uint32_t part = (uint32_t)urand(1, 200000), supp = (uint32_t)urand(1, (uint64_t)(2000 * sf) + 1);
            int qty = (int)urand(1, 50), disc = (int)urand(0, 10), tax = (int)urand(0, 8);
            uint32_t retail = 90000 + (part / 10) % 20001 + 100 * (part % 1000); /* cents, dbgen's formula */
            uint32_t ext = retail * (uint32_t)qty;
            fill_row(SSB_LO, o + (uint64_t)ln);
            memcpy(v, rowbuf, SSB_LO);
            le32(v, o); v[4] = (unsigned char)ln; le32(v + 5, cust); le32(v + 9, part); le32(v + 13, supp);
            le32(v + 17, d->key); v[21] = (unsigned char)urand(1, 5); v[22] = 0; v[23] = (unsigned char)qty;
            le32(v + 24, ext); le32(v + 28, ext * (uint32_t)lines); v[32] = (unsigned char)disc;
            le32(v + 33, ext / 100 * (uint32_t)(100 - disc)); le32(v + 37, retail * 6 / 10); v[41] = (unsigned char)tax;
            int cd = od + (int)urand(30, 90);
            le32(v + 42, ssb_dates[cd < SSB_DAYS ? cd : SSB_DAYS - 1].key);
            if (ld_put(&l, k, tkey(k, 'L', 2, o, (uint32_t)ln, 0, 0), v, SSB_LO)) return 1;
            for (int q = 0; q < 3; q++)
                if (ssb_date_match(&ssb_q[q], d) && disc >= ssb_q[q].dlo && disc <= ssb_q[q].dhi && qty >= ssb_q[q].qlo && qty <= ssb_q[q].qhi)
                    expect[q] += (uint64_t)ext * (uint64_t)disc;
            nrows++;
        }
    }
    if (ld_done(&l)) return 1;
    double load_s = (now_us() - tl) / 1e6;
    uint64_t fbytes = r->e->file_bytes(r->kv), loaded = r->logical_written;

    /* each query three times; the first is the colder one */
    double ms[3][3];
    int ok = 1;
    uint64_t rd0 = proc_io("rchar");
    for (int rep = 0; rep < 3; rep++)
        for (int q = 0; q < 3; q++) {
            ProbeScan *s = calloc(1, sizeof *s);
            s->q = &ssb_q[q];
            int got;
            uint64_t bytes = 0;
            double t0 = now_us();
            r->e->begin(r->kv, 0);
            if (r->e->scan(r->kv, k, tkey(k, 'D', 0, 0, 0, 0, 0), SSB_DAYS, &got, &bytes, cb_date, s)) r->errors++;
            if (r->e->scan(r->kv, k, tkey(k, 'L', 0, 0, 0, 0, 0), (int)nrows, &got, &bytes, cb_lineorder, s)) r->errors++;
            r->e->end(r->kv, 0, 0);
            ms[q][rep] = (now_us() - t0) / 1e3;
            if (s->rows != nrows || s->sum != expect[q]) { ok = 0; fprintf(stderr, "Q1.%d: %llu rows, revenue %llu, want %llu rows, %llu\n", q + 1, (unsigned long long)s->rows, (unsigned long long)s->sum, (unsigned long long)nrows, (unsigned long long)expect[q]); }
            free(s);
        }
    uint64_t readb = proc_io("rchar") - rd0;
    r->e->close(r->kv);
    printf("{\"engine\": \"%s\", \"workload\": \"ssb1\", \"sf\": %.2f, \"rows\": %llu, \"page\": %u, \"cache\": %llu, \"load_s\": %.3f, \"load_rows_s\": %.0f",
           engine, sf, (unsigned long long)nrows, page, (unsigned long long)cache, load_s, (double)nrows / load_s);
    printf(", \"file_bytes\": %llu, \"space_amp\": %.2f, \"bytes_read\": %llu, \"answers_ok\": %s, \"peak_rss_kb\": %llu, \"errors\": %llu",
           (unsigned long long)fbytes, (double)fbytes / (double)loaded, (unsigned long long)readb, ok ? "true" : "false",
           (unsigned long long)peak_rss_kb(), (unsigned long long)r->errors);
    for (int q = 0; q < 3; q++) {
        double a = ms[q][0], b = ms[q][1], c = ms[q][2], lo = a < b ? (a < c ? a : c) : (b < c ? b : c);
        double med = a + b + c - lo - (a > b ? (a > c ? a : c) : (b > c ? b : c));
        printf(", \"q1_%d\": {\"first_ms\": %.1f, \"median_ms\": %.1f, \"rows_s\": %.0f, \"revenue\": %llu}", q + 1, a, med,
               (double)nrows / (med / 1e3), (unsigned long long)expect[q]);
    }
    printf("}\n");
    return ok && !r->errors ? 0 : 1;
}


/* w9-check: the version store against a model, on any engine. Commits of one
 * to three distinct rows on a simulated clock several commits to the second
 * with gaps, so the commit-time index is overwritten within a second and
 * floored across empty ones; the store is closed and reopened halfway, so
 * the counter's recovery is exercised; then every row is read AS OF every
 * commit id that matters to it (each version's id, one either side, the
 * load, the last) and every second is mapped, against the model. Rows that
 * never existed must read as absent, which is the case where the floor lands
 * in ANOTHER row's history. One JSON line; exits 1 on any mismatch. */
typedef struct { uint64_t id, seed; } VsVer;
static int run_vs_check(Run *r, const char *engine, const char *dir, uint64_t cache, uint32_t page, uint64_t ops) {
    uint64_t n = r->records, mismatches = 0, checks = 0, versions = n;
    VsVer **hist = calloc(n, sizeof *hist);
    size_t *cnt = calloc(n, sizeof *cnt), *cap = calloc(n, sizeof *cap);
    uint64_t *csec = calloc(ops + 1, sizeof *csec);
    csec[0] = vs_clock;
    for (uint64_t j = 0; j < n; j++) {
        cap[j] = 4;
        hist[j] = malloc(cap[j] * sizeof **hist);
        hist[j][cnt[j]++] = (VsVer){0, j};
    }
    char k[32];
    for (uint64_t i = 1; i <= ops; i++) {
        if (i == ops / 2) {
            uint64_t want = vs_last;
            if (r->e->sync(r->kv)) mismatches++;
            r->e->close(r->kv);
            r->kv = r->e->open(dir, cache, page);
            if (!r->kv) { fprintf(stderr, "reopen failed\n"); return 1; }
            vs_last = 0;
            checks++;
            if (vs_recover(r) || vs_last != want) {
                fprintf(stderr, "MISMATCH counter after reopen: %llu, want %llu\n", (unsigned long long)vs_last,
                        (unsigned long long)want);
                mismatches++;
                vs_last = want;
            }
        }
        vs_clock = 1000 + i / 5 + (i / 50) * 3;
        uint64_t id = ++vs_last, rows[3];
        int m = 1 + (int)(rnd() % 3);
        if (r->e->begin(r->kv, 1)) return 1;
        for (int q = 0; q < m; q++) {
            uint64_t j;
            int dup;
            do {
                j = rnd() % n;
                dup = 0;
                for (int z = 0; z < q; z++) dup |= rows[z] == j;
            } while (dup);
            rows[q] = j;
            uint64_t seed = rnd();
            fill_value(seed);
            size_t kn = ycsb_key(k, j);
            if (vs_write(r, k, kn, id, vs_clock, 1)) mismatches++;
            if (cnt[j] == cap[j]) hist[j] = realloc(hist[j], (cap[j] *= 2) * sizeof **hist);
            hist[j][cnt[j]++] = (VsVer){id, seed};
            versions++;
        }
        if (vs_mark(r, id, vs_clock) || r->e->end(r->kv, 1, i % 16 == 0)) mismatches++;
        csec[id] = vs_clock;
    }
    unsigned char out[VS_HDR + VS_FIELD], want[VS_FIELD];
    if (r->e->begin(r->kv, 0)) return 1;
    for (uint64_t j = 0; j < n; j++) {
        size_t kn = ycsb_key(k, j);
        for (size_t v = 0; v < cnt[j]; v++) {
            uint64_t probes[4] = {hist[j][v].id, hist[j][v].id + 1, hist[j][v].id ? hist[j][v].id - 1 : 0, vs_last};
            for (int p = 0; p < 4; p++) {
                uint64_t a = probes[p];
                size_t e = 0; /* the model: the last version started at or before a */
                while (e + 1 < cnt[j] && hist[j][e + 1].id <= a) e++;
                fill_random(want, VS_FIELD, hist[j][e].seed);
                int rc = vs_read_asof(r, k, kn, a, out);
                checks++;
                if (rc || vs_rd64(out) != hist[j][e].id || memcmp(out + VS_HDR, want, VS_FIELD)) {
                    if (mismatches < 5)
                        fprintf(stderr, "MISMATCH row %llu as of %llu: rc %d start %llu, want start %llu\n", (unsigned long long)j,
                                (unsigned long long)a, rc, rc ? 0ULL : (unsigned long long)vs_rd64(out),
                                (unsigned long long)hist[j][e].id);
                    mismatches++;
                }
            }
        }
    }
    for (uint64_t j = n; j < n + 50; j++) {
        size_t kn = ycsb_key(k, j);
        uint64_t a = rnd() % (vs_last + 1);
        checks++;
        if (vs_read_asof(r, k, kn, a, out) != 1) {
            if (mismatches < 5) fprintf(stderr, "MISMATCH row %llu never existed but read as present\n", (unsigned long long)j);
            mismatches++;
        }
    }
    for (uint64_t sec = 990; sec <= csec[vs_last] + 3; sec++) {
        uint64_t got, want_id = 0;
        for (uint64_t c = 1; c <= vs_last; c++) if (csec[c] <= sec) want_id = c;
        checks++;
        if (vs_id_at(r, sec, &got) || got != want_id) {
            if (mismatches < 5)
                fprintf(stderr, "MISMATCH second %llu: commit %llu, want %llu\n", (unsigned long long)sec, (unsigned long long)got,
                        (unsigned long long)want_id);
            mismatches++;
        }
    }
    r->e->end(r->kv, 0, 0);
    r->e->close(r->kv);
    printf("{\"engine\": \"%s\", \"workload\": \"w9-check\", \"records\": %llu, \"commits\": %llu, \"versions\": %llu, "
           "\"checks\": %llu, \"mismatches\": %llu}\n",
           engine, (unsigned long long)n, (unsigned long long)vs_last, (unsigned long long)versions, (unsigned long long)checks,
           (unsigned long long)mismatches);
    for (uint64_t j = 0; j < n; j++) free(hist[j]);
    free(hist), free(cnt), free(cap), free(csec);
    return mismatches ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: bench ENGINE WORKLOAD DIR [RECORDS [OPS]]\n"); return 2; }
    const char *engine = argv[1], *wl = argv[2], *dir = argv[3];
    uint64_t records = argc > 4 ? strtoull(argv[4], NULL, 10) : 100000;
    uint64_t ops = argc > 5 ? strtoull(argv[5], NULL, 10) : 20000;
    const char *pg = getenv("BENCH_PAGE"), *cs = getenv("BENCH_CACHE");
    uint32_t page = pg ? (uint32_t)atoi(pg) : 4096;
    uint64_t cache = cs ? strtoull(cs, NULL, 10) : 64ull << 20;
    if (!strcmp(wl, "w8")) { vsize = 100 * 1024; if (records > 2000) records = 2000; }
    if (!strncmp(wl, "group-", 6)) group_every = atoi(wl + 6);
    value = malloc(vsize);
    temporal = !strncmp(wl, "w9", 2);
    vs_row = malloc(VS_HDR + vsize);
    if (!strcmp(wl, "w9-check")) vs_clock = 1000;
    Run r;
    memset(&r, 0, sizeof r);
    r.records = records;
    if (!strcmp(engine, "cow")) { cand = &gs_engine_cow; r.e = &cand_engine; }
    else if (!strcmp(engine, "wal")) { cand = &gs_engine_wal; r.e = &cand_engine; }
    else if (!strcmp(engine, "lsm")) { cand = &gs_engine_lsm; r.e = &cand_engine; }
#ifdef HAVE_SQLITE
    else if (!strcmp(engine, "sqlite")) r.e = &sqlite_engine;
#endif
#ifdef HAVE_LMDB
    else if (!strcmp(engine, "lmdb")) r.e = &lmdb_engine;
#endif
#ifdef HAVE_ROCKSDB
    else if (!strcmp(engine, "rocksdb")) r.e = &rocksdb_engine;
#endif
    else { fprintf(stderr, "unknown engine %s\n", engine); return 2; }

    r.kv = r.e->open(dir, cache, page);
    if (!r.kv) { fprintf(stderr, "open failed\n"); return 1; }
    if (!strcmp(wl, "tpcc")) return run_tpcc(&r, engine, ops, page, cache);
    if (!strcmp(wl, "ssb1")) return run_ssb(&r, engine, page, cache);
    uint64_t w0 = proc_io("wchar"), rd0 = proc_io("rchar");
    uint64_t fl0, pr0;
    r.e->counters(r.kv, &fl0, &pr0);

    /* every workload but the load starts from a loaded store: that load is
     * not measured */
    double t_load0 = now_us();
    if (!strcmp(wl, "w4-sorted")) sorted_keys = 1;
    int is_load = !strcmp(wl, "ycsb-load") || !strncmp(wl, "w4-", 3);
    for (uint64_t i = 0; i < records; i += 1000)
        if (do_insert_batch(&r, i, i + 1000 < records ? i + 1000 : records, vsize)) { fprintf(stderr, "load failed\n"); return 1; }
    double load_s = (now_us() - t_load0) / 1e6;
    if (!strcmp(wl, "w9-check")) return run_vs_check(&r, engine, dir, cache, page, ops);
    int depth = !strncmp(wl, "w9-asof-", 8) ? atoi(wl + 8) : 0;
    uint64_t vs_rows = records < 1000 ? records : 1000;
    if (depth && build_history(&r, vs_rows, depth)) { fprintf(stderr, "history build failed\n"); return 1; }
    if (!is_load) {
        r.logical_written = 0;
        w0 = proc_io("wchar");
        rd0 = proc_io("rchar");
        r.e->counters(r.kv, &fl0, &pr0);
    }

    uint64_t file_before = r.e->file_bytes(r.kv);
    double recover_ms = -1;
    if (!strcmp(wl, "recovery")) {
        /* a child writes `ops` durable commits and dies without closing;
         * the parent times the reopen, which is the handoff cost (§5.5) */
        r.e->close(r.kv);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            BKV *kv = r.e->open(dir, cache, page);
            Run c = r;
            c.kv = kv;
            for (uint64_t i = 0; i < ops; i++) op_update(&c, rnd() % records, 0);
            _exit(0); /* no close: whatever recovery has to do, it does at the next open */
        }
        int st;
        waitpid(pid, &st, 0);
        double t1 = now_us();
        r.kv = r.e->open(dir, cache, page);
        size_t vn;
        char k[32];
        size_t kn = ycsb_key(k, 0);
        r.e->begin(r.kv, 0);
        r.e->get(r.kv, k, kn, &vn);
        r.e->end(r.kv, 0, 0);
        recover_ms = (now_us() - t1) / 1e3;
        ops = 0;
    }
    if (!strcmp(wl, "w6") && r.e->hold(r.kv)) { fprintf(stderr, "hold failed\n"); return 1; }
    double t0 = now_us();
    Zipf z;
    if (!is_load) zipf_init(&z, records);
    uint64_t inserted = records;
    for (uint64_t i = 0; !is_load && i < ops; i++) {
        double u = rnd01();
        if (!strcmp(wl, "ycsb-a")) { if (u < 0.5) op_read(&r, scrambled(&z)); else op_update(&r, scrambled(&z), 0); }
        else if (!strcmp(wl, "ycsb-b")) { if (u < 0.95) op_read(&r, scrambled(&z)); else op_update(&r, scrambled(&z), 0); }
        else if (!strcmp(wl, "ycsb-c")) op_read(&r, scrambled(&z));
        else if (!strcmp(wl, "ycsb-d")) {
            /* reads favour the newest records: latest distribution */
            if (u < 0.95) { uint64_t back = zipf_next(&z); op_read(&r, back < inserted ? inserted - 1 - back : 0); }
            else { op_update(&r, inserted++, 0); }
        } else if (!strcmp(wl, "ycsb-e")) {
            if (u < 0.95) op_scan(&r, scrambled(&z), 1 + (int)(rnd() % 100));
            else op_update(&r, inserted++, 0);
        } else if (!strcmp(wl, "ycsb-f")) { if (u < 0.5) op_read(&r, scrambled(&z)); else op_update(&r, scrambled(&z), 1); }
        else if (!strcmp(wl, "w1-uniform")) op_read(&r, rnd() % records);
        else if (!strcmp(wl, "w1-zipf")) op_read(&r, scrambled(&z));
        else if (!strcmp(wl, "w2")) op_update(&r, rnd() % records, 0);
        else if (!strcmp(wl, "w9")) op_update_temporal(&r, rnd() % records);
        else if (depth) op_read_asof(&r, rnd() % vs_rows, rnd() % (vs_last + 1));
        else if (!strcmp(wl, "w3")) { op_batch(&r, records, 1000); i = r.ops - 1; }
        else if (!strcmp(wl, "w5-1")) op_scan(&r, rnd() % records, 1);
        else if (!strcmp(wl, "w5-100")) op_scan(&r, rnd() % records, 100);
        else if (!strcmp(wl, "w5-10000")) { op_scan(&r, rnd() % records, 10000); if (i >= 200) break; }
        else if (!strcmp(wl, "w6")) { if (u < 0.8) op_read(&r, rnd() % records); else op_update(&r, rnd() % records, 0); }
        else if (!strcmp(wl, "w7")) op_update(&r, rnd() % (records < 1000 ? records : 1000), 0);
        else if (!strcmp(wl, "w8")) { if (u < 0.5) op_read(&r, rnd() % records); else op_update(&r, rnd() % records, 0); }
        else if (!strncmp(wl, "group-", 6)) op_update(&r, rnd() % records, 0);
        else if (!strcmp(wl, "recovery")) break;
        else { fprintf(stderr, "unknown workload %s\n", wl); return 2; }
    }
    double secs = (now_us() - t0) / 1e6;
    uint64_t fl1, pr1;
    r.e->counters(r.kv, &fl1, &pr1);
    uint64_t wrote = proc_io("wchar") - w0, readb = proc_io("rchar") - rd0;
    uint64_t fbytes = r.e->file_bytes(r.kv);
    if (!strcmp(wl, "w6")) r.e->release(r.kv);
    r.e->close(r.kv);

    printf("{\"engine\": \"%s\", \"workload\": \"%s\", \"records\": %llu, \"page\": %u, \"cache\": %llu", engine, wl,
           (unsigned long long)records, page, (unsigned long long)cache);
    if (is_load) {
        printf(", \"load_s\": %.3f, \"load_ops_s\": %.0f", load_s, (double)records / load_s);
    } else {
        printf(", \"ops\": %llu, \"secs\": %.3f, \"ops_s\": %.0f", (unsigned long long)r.ops, secs, (double)r.ops / secs);
    }
    printf(", \"bytes_written\": %llu, \"logical_written\": %llu, \"write_amp\": %.2f, \"bytes_read\": %llu",
           (unsigned long long)wrote, (unsigned long long)r.logical_written,
           r.logical_written ? (double)wrote / (double)r.logical_written : 0.0, (unsigned long long)readb);
    if (fl1 != (uint64_t)-1) printf(", \"flushes\": %llu, \"pages_read\": %llu", (unsigned long long)(fl1 - fl0), (unsigned long long)(pr1 - pr0));
    printf(", \"file_before\": %llu, \"file_growth\": %.2f", (unsigned long long)file_before,
           file_before ? (double)fbytes / (double)file_before : 0.0);
    if (recover_ms >= 0) printf(", \"recover_ms\": %.2f", recover_ms);
    if (temporal) printf(", \"commit_ids\": %llu, \"history_depth\": %d", (unsigned long long)vs_last, depth);
    printf(", \"file_bytes\": %llu, \"space_amp\": %.2f, \"peak_rss_kb\": %llu, \"errors\": %llu, \"notfound\": %llu",
           (unsigned long long)fbytes, (double)fbytes / (double)(inserted * (vsize + 23)), (unsigned long long)peak_rss_kb(),
           (unsigned long long)r.errors, (unsigned long long)r.notfound);
    print_lat("read", &r.read);
    print_lat("write", &r.write);
    print_lat("scan", &r.scan);
    printf("}\n");
    return 0;
}

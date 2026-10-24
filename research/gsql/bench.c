/* The Phase 0a workload harness (docs/gsql_storage_design.md §5.1, §5.2):
 * one program drives every candidate and every reference engine through one
 * small key-value interface, so a number differs because an engine differs.
 *
 *   bench ENGINE WORKLOAD DIR [RECORDS [OPS]]
 *
 * ENGINE: cow, wal, lsm (the candidates), sqlite (and lmdb, rocksdb when
 * built with them). WORKLOAD: ycsb-load, ycsb-a ... ycsb-f, w1 ... (see
 * main). Prints ONE JSON line of metrics on stdout. Each run is meant to be a
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
typedef struct {
    const char *name;
    BKV *(*open)(const char *dir, uint64_t cache, uint32_t page);
    void (*close)(BKV *);
    int (*begin)(BKV *, int write);
    int (*put)(BKV *, const void *k, size_t kn, const void *v, size_t vn);
    int (*get)(BKV *, const void *k, size_t kn, size_t *vn);   /* 0 found, 1 absent, <0 error */
    int (*del)(BKV *, const void *k, size_t kn);
    int (*scan)(BKV *, const void *k, size_t kn, int count, int *got, uint64_t *bytes);
    int (*end)(BKV *, int commit, int sync);                   /* commit or abort/release */
    void (*counters)(BKV *, uint64_t *flushes, uint64_t *pages_read);
    uint64_t (*file_bytes)(BKV *);
    int (*sync)(BKV *);        /* make every commit so far durable (group commit) */
    int (*hold)(BKV *);        /* open a long-lived read snapshot (W6) */
    void (*release)(BKV *);
} BEngine;

/* ---- the candidates, through store.h -------------------------------------- */

struct BKV {
    const GsEngine *e;
    GsStore *db;
    GsTxn *t, *held;
    int write;
    char dir[512];
#ifdef HAVE_SQLITE
    sqlite3 *sq, *sq2;
    sqlite3_stmt *s_put, *s_get, *s_del, *s_scan, *s_begin_r, *s_begin_w, *s_commit, *s_rollback;
#endif
#ifdef HAVE_LMDB
    MDB_env *env;
    MDB_dbi dbi;
    MDB_txn *lt, *lheld;
#endif
#ifdef HAVE_ROCKSDB
    rocksdb_t *rdb;
    rocksdb_options_t *ropt;
    rocksdb_writeoptions_t *wsync;
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
    GsOptions o = {1, page, cache, NULL, 0, 0};
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
static int c_scan(BKV *b, const void *k, size_t kn, int count, int *got, uint64_t *bytes) {
    GsCursor *c;
    int rc = b->e->cursor_open(b->t, gss("t"), &c);
    if (rc) return -rc;
    int pos;
    *got = 0;
    for (rc = b->e->seek(c, gsb(k, kn), GS_SEEK_GE, &pos); rc == GS_OK && pos && *got < count; rc = b->e->next(c, &pos)) {
        GsBytes v;
        b->e->value(c, &v);
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
        if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) total += (uint64_t)st.st_size;
    }
    closedir(d);
    return total;
}
static uint64_t c_file_bytes(BKV *b) { return dir_bytes(b->dir); }
static int c_sync(BKV *b) { return b->e->sync(b->db, NULL); }
static int c_hold(BKV *b) { return b->e->begin_read(b->db, &b->held); }
static void c_release(BKV *b) { if (b->held) b->e->abort(b->held); b->held = NULL; }

static const BEngine cand_engine = {"candidate", c_open, c_close, c_begin, c_put, c_get, c_del, c_scan, c_end, c_counters, c_file_bytes,
                                    c_sync, c_hold, c_release};

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
    sqlite3_prepare_v2(b->sq, "BEGIN", -1, &b->s_begin_r, NULL);
    sqlite3_prepare_v2(b->sq, "BEGIN IMMEDIATE", -1, &b->s_begin_w, NULL);
    sqlite3_prepare_v2(b->sq, "COMMIT", -1, &b->s_commit, NULL);
    sqlite3_prepare_v2(b->sq, "ROLLBACK", -1, &b->s_rollback, NULL);
    return b;
}
static void q_close(BKV *b) {
    sqlite3_stmt *all[] = {b->s_put, b->s_get, b->s_del, b->s_scan, b->s_begin_r, b->s_begin_w, b->s_commit, b->s_rollback};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) sqlite3_finalize(all[i]);
    sqlite3_close(b->sq);
    free(b);
}
static int q_step(sqlite3_stmt *s) { int rc = sqlite3_step(s); sqlite3_reset(s); return rc == SQLITE_DONE || rc == SQLITE_ROW ? 0 : -1; }
static int q_begin(BKV *b, int write) { b->write = write; return q_step(write ? b->s_begin_w : b->s_begin_r); }
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
static int q_scan(BKV *b, const void *k, size_t kn, int count, int *got, uint64_t *bytes) {
    sqlite3_bind_blob(b->s_scan, 1, k, (int)kn, SQLITE_STATIC);
    sqlite3_bind_int(b->s_scan, 2, count);
    *got = 0;
    int rc;
    while ((rc = sqlite3_step(b->s_scan)) == SQLITE_ROW) { (void)sqlite3_column_blob(b->s_scan, 0); *bytes += (uint64_t)sqlite3_column_bytes(b->s_scan, 0); (*got)++; }
    sqlite3_reset(b->s_scan);
    return rc == SQLITE_DONE ? 0 : -1;
}
static int q_end(BKV *b, int commit, int sync) {
    (void)sync; /* synchronous=FULL: every commit flushes */
    return q_step(commit && b->write ? b->s_commit : (b->write ? b->s_rollback : b->s_commit));
}
static void q_counters(BKV *b, uint64_t *fl, uint64_t *pr) { (void)b; *fl = (uint64_t)-1; *pr = (uint64_t)-1; }
static uint64_t q_file_bytes(BKV *b) { return dir_bytes(b->dir); }
static int q_sync(BKV *b) { (void)b; return 0; } /* synchronous=FULL: already durable */
static int q_hold(BKV *b) {
    char path[600];
    snprintf(path, sizeof path, "%s/db.sqlite", b->dir);
    if (sqlite3_open(path, &b->sq2) != SQLITE_OK) return -1;
    /* a read transaction that has read something holds its snapshot */
    sqlite3_exec(b->sq2, "BEGIN; SELECT count(*) FROM (SELECT 1 FROM kv LIMIT 1);", NULL, NULL, NULL);
    return 0;
}
static void q_release(BKV *b) { if (b->sq2) { sqlite3_exec(b->sq2, "COMMIT", NULL, NULL, NULL); sqlite3_close(b->sq2); b->sq2 = NULL; } }
static const BEngine sqlite_engine = {"sqlite", q_open, q_close, q_begin, q_put, q_get, q_del, q_scan, q_end, q_counters, q_file_bytes,
                                      q_sync, q_hold, q_release};
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
static int l_scan(BKV *b, const void *k, size_t kn, int count, int *got, uint64_t *bytes) {
    MDB_cursor *c;
    if (mdb_cursor_open(b->lt, b->dbi, &c)) return -1;
    MDB_val K = {kn, (void *)k}, V;
    *got = 0;
    for (int rc = mdb_cursor_get(c, &K, &V, MDB_SET_RANGE); rc == 0 && *got < count; rc = mdb_cursor_get(c, &K, &V, MDB_NEXT)) {
        *bytes += V.mv_size;
        (*got)++;
    }
    mdb_cursor_close(c);
    return 0;
}
static int l_end(BKV *b, int commit, int sync) {
    (void)sync;
    if (b->write && commit) return mdb_txn_commit(b->lt) ? -1 : 0;
    mdb_txn_abort(b->lt);
    return 0;
}
static void l_counters(BKV *b, uint64_t *fl, uint64_t *pr) { (void)b; *fl = (uint64_t)-1; *pr = (uint64_t)-1; }
static uint64_t l_file_bytes(BKV *b) { return dir_bytes(b->dir); }
static int l_sync(BKV *b) { return mdb_env_sync(b->env, 1) ? -1 : 0; }
static int l_hold(BKV *b) { return mdb_txn_begin(b->env, NULL, MDB_RDONLY, &b->lheld) ? -1 : 0; }
static void l_release(BKV *b) { if (b->lheld) mdb_txn_abort(b->lheld); b->lheld = NULL; }
static const BEngine lmdb_engine = {"lmdb", l_open, l_close, l_begin, l_put, l_get, l_del, l_scan, l_end, l_counters, l_file_bytes,
                                    l_sync, l_hold, l_release};
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
static int r_scan(BKV *b, const void *k, size_t kn, int count, int *got, uint64_t *bytes) {
    rocksdb_iterator_t *it = rocksdb_create_iterator(b->rdb, b->ropts);
    *got = 0;
    for (rocksdb_iter_seek(it, k, kn); rocksdb_iter_valid(it) && *got < count; rocksdb_iter_next(it)) {
        size_t vn;
        (void)rocksdb_iter_value(it, &vn);
        *bytes += vn;
        (*got)++;
    }
    rocksdb_iter_destroy(it);
    return 0;
}
static int r_end(BKV *b, int commit, int sync) {
    (void)sync;
    if (!b->write || !commit) return 0;
    char *err = NULL;
    rocksdb_write(b->rdb, b->wsync, b->batch, &err);
    rocksdb_writebatch_clear(b->batch);
    return err ? -1 : 0;
}
static void r_counters(BKV *b, uint64_t *fl, uint64_t *pr) { (void)b; *fl = (uint64_t)-1; *pr = (uint64_t)-1; }
static uint64_t r_file_bytes(BKV *b) { return dir_bytes(b->dir); }
static int r_sync(BKV *b) { char *err = NULL; rocksdb_flush_wal(b->rdb, 1, &err); return err ? -1 : 0; }
static int r_hold(BKV *b) { b->rsnap = rocksdb_create_snapshot(b->rdb); return 0; }
static void r_release(BKV *b) { if (b->rsnap) rocksdb_release_snapshot(b->rdb, b->rsnap); b->rsnap = NULL; }
static const BEngine rocksdb_engine = {"rocksdb", r_open, r_close, r_begin, r_put, r_get, r_del, r_scan, r_end, r_counters, r_file_bytes,
                                       r_sync, r_hold, r_release};
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
static size_t ycsb_key(char *out, uint64_t keynum) {
    return (size_t)snprintf(out, 32, "user%019llu", (unsigned long long)(fnv64(keynum) % 10000000000000000000ULL));
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

static void fill_value(uint64_t seed) {
    for (size_t i = 0; i < vsize; i++) value[i] = (unsigned char)(seed * 31 + i * 7);
}

static int do_insert_batch(Run *r, uint64_t from, uint64_t to, size_t vn) {
    char k[32];
    if (r->e->begin(r->kv, 1)) return -1;
    for (uint64_t i = from; i < to; i++) {
        size_t kn = ycsb_key(k, i);
        fill_value(i);
        if (r->e->put(r->kv, k, kn, value, vn)) { r->errors++; }
        (void)0;
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

static void op_scan(Run *r, uint64_t keynum, int len) {
    char k[32];
    size_t kn = ycsb_key(k, keynum);
    int got;
    uint64_t bytes = 0;
    double t0 = now_us();
    r->e->begin(r->kv, 0);
    if (r->e->scan(r->kv, k, kn, len, &got, &bytes)) r->errors++;
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
    uint64_t w0 = proc_io("wchar"), rd0 = proc_io("rchar");
    uint64_t fl0, pr0;
    r.e->counters(r.kv, &fl0, &pr0);

    /* every workload but the load starts from a loaded store: that load is
     * not measured */
    double t_load0 = now_us();
    int is_load = !strcmp(wl, "ycsb-load");
    for (uint64_t i = 0; i < records; i += 1000)
        if (do_insert_batch(&r, i, i + 1000 < records ? i + 1000 : records, vsize)) { fprintf(stderr, "load failed\n"); return 1; }
    double load_s = (now_us() - t_load0) / 1e6;
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
    printf(", \"file_bytes\": %llu, \"space_amp\": %.2f, \"peak_rss_kb\": %llu, \"errors\": %llu, \"notfound\": %llu",
           (unsigned long long)fbytes, (double)fbytes / (double)(inserted * (vsize + 23)), (unsigned long long)peak_rss_kb(),
           (unsigned long long)r.errors, (unsigned long long)r.notfound);
    print_lat("read", &r.read);
    print_lat("write", &r.write);
    print_lat("scan", &r.scan);
    printf("}\n");
    return 0;
}

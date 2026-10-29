/* Test 3 of docs/gsql_storage_design.md §6: the hard requirements of point 2,
 * checked against the candidates through the one interface (store.h).
 *
 *   hardreq g4       ENGINE DIR
 *   hardreq g1       ENGINE DIR ROWS
 *   hardreq backup   ENGINE DIR ROWS COMMITS_AFTER_SNAPSHOT
 *   hardreq held     ENGINE DIR COMMITS HOLD(0|1)
 *   hardreq recovery ENGINE DIR COMMITS HOLD(0|1)
 *   hardreq sbackup  ENGINE DIR ROWS BUDGET     (Test 4: the backup in slices)
 *   hardreq files    ENGINE DIR ROWS            (Test 4: open question 6)
 *   hardreq g2       ENGINE DIR ROWS            (G-Dash: group-by, idle and mid-refresh)
 *   hardreq g3       ENGINE DIR TREES ROWS      (G-Dash: per-request cost; G6 at >= 20)
 *   hardreq g5       ENGINE DIR ROWS RANDOM     (G-Dash: peak memory of one large write)
 *   hardreq handoff  ENGINE DIR COMMITS KILL    (§5.5: owner dies, a new one serves)
 *   hardreq gspace   ENGINE DIR K G             (space: K-row txns, synced every G)
 *
 * Each prints one JSON line. Every claim a line makes is CHECKED here, not
 * inferred: a G1 line saying the old snapshot kept the old data has scanned
 * that snapshot, and a backup line saying the copy matches has reopened it.
 * A failed check prints "ok":false with the reason and exits 1.
 *
 * Spike code, like everything in this directory: not linked into gbasic, not
 * run by the gate. */

#define _POSIX_C_SOURCE 200809L
#include "store.h"
#include "store_common.h"

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VLEN 100

static const GsEngine *E;
static char g_dir[512], g_path[600];
static int g_failed;
static char g_why[512];

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void fail(const char *fmt, const char *a, long long b) {
    if (g_failed) return;
    g_failed = 1;
    snprintf(g_why, sizeof g_why, fmt, a, b);
}
#define REQUIRE(cond, fmt, a, b) do { if (!(cond)) fail(fmt, a, (long long)(b)); } while (0)

/* every regular file under DIR, one level deep (the stores keep their files
 * beside the path they were opened with) */
static int is_log(const char *name) { return strstr(name, ".log.") != NULL || strstr(name, "-wal") != NULL; }
/* which=0 everything but logs, 1 logs only, 2 everything */
static uint64_t dir_bytes_of(const char *dir, int which) {
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
        /* a store is a directory now (§7 question 6), so its files are one
         * level down: counting only this level would read every store as 0 */
        if (S_ISDIR(st.st_mode)) { total += dir_bytes_of(p, which); continue; }
        if (which == 0 && is_log(de->d_name)) continue;
        if (which == 1 && !is_log(de->d_name)) continue;
        if (S_ISREG(st.st_mode)) total += (uint64_t)st.st_size;
    }
    closedir(d);
    return total;
}
/* data files: what the store keeps, without the log (2.C preallocates its
 * log, so a total would hide every change below 64 MB) */
static uint64_t dir_bytes(const char *dir) { return dir_bytes_of(dir, 0); }
static uint64_t log_bytes(const char *dir) { return dir_bytes_of(dir, 1); }

static GsStore *open_store(const char *path, int create) {
    /* HARDREQ_MEMTABLE sets 2.C's memtable bytes, for recovery against memtable size */
    const char *mt = getenv("HARDREQ_MEMTABLE");
    GsOptions o = {create, 4096, 0, NULL, mt ? strtoull(mt, NULL, 10) : 0, 0, 0, 0};
    GsStore *db = NULL;
    int rc = E->open(path, &o, &db);
    if (rc) { fail("open answered %s%lld", gs_status_name(rc), 0); return NULL; }
    return db;
}

static size_t key_of(char *k, uint64_t i) { return (size_t)snprintf(k, 32, "k%015llu", (unsigned long long)i); }
/* a value that says which generation and key wrote it, so a scan can tell an
 * old dataset from a new one record by record */
static void value_of(unsigned char *v, uint32_t gen, uint64_t i) {
    put32(v, gen);
    put64(v + 4, i);
    for (int j = 12; j < VLEN; j++) v[j] = (unsigned char)(i * 31 + j + gen);
}

/* Scan a tree in a txn: count, and how many records carry generation `gen`. */
static int scan_gen(GsTxn *t, const char *tree, uint32_t gen, uint64_t *count, uint64_t *match) {
    GsCursor *c;
    *count = *match = 0;
    int rc = E->cursor_open(t, gss(tree), &c);
    if (rc) return rc;
    int pos;
    for (rc = E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos)) {
        GsBytes v;
        E->value(c, &v);
        (*count)++;
        if (v.n == VLEN && get32(v.p) == gen) (*match)++;
    }
    E->cursor_close(c);
    return rc;
}

static int load(GsStore *db, const char *tree, uint64_t rows, uint32_t gen, int create) {
    GsTxn *w;
    int rc = E->begin_write(db, &w);
    if (rc) return rc;
    if (create && (rc = E->tree_create(w, gss(tree)))) { E->abort(w); return rc; }
    char k[32];
    unsigned char v[VLEN];
    for (uint64_t i = 0; i < rows && rc == GS_OK; i++) {
        size_t kn = key_of(k, i);
        value_of(v, gen, i);
        rc = E->put(w, gss(tree), gsb(k, kn), gsb(v, VLEN), GS_APPEND);
    }
    if (rc) { E->abort(w); return rc; }
    return E->commit(w, 1, NULL);
}

static int update_one(GsStore *db, const char *tree, uint64_t i, uint32_t gen, int sync) {
    GsTxn *w;
    int rc = E->begin_write(db, &w);
    if (rc) return rc;
    char k[32];
    unsigned char v[VLEN];
    size_t kn = key_of(k, i);
    value_of(v, gen, i);
    if ((rc = E->put(w, gss(tree), gsb(k, kn), gsb(v, VLEN), 0))) { E->abort(w); return rc; }
    return E->commit(w, sync, NULL);
}

static void setup(const char *engine, const char *dir) {
    if (!strcmp(engine, "cow")) E = &gs_engine_cow;
    else if (!strcmp(engine, "wal")) E = &gs_engine_wal;
    else if (!strcmp(engine, "lsm")) E = &gs_engine_lsm;
    else { fprintf(stderr, "unknown engine %s\n", engine); exit(2); }
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    snprintf(g_path, sizeof g_path, "%s/db", dir);
}

static int finish(const char *test, const char *fields) {
    printf("{\"test\":\"%s\",\"engine\":\"%s\",%s\"ok\":%s%s%s%s}\n", test, E->name, fields, g_failed ? "false" : "true",
           g_failed ? ",\"why\":\"" : "", g_failed ? g_why : "", g_failed ? "\"" : "");
    return g_failed;
}

/* ---- G4: a read-only transaction refuses every write at the engine -------- */

static int t_g4(void) {
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("g4", "");
    GsTxn *w;
    E->begin_write(db, &w);
    E->tree_create(w, gss("t"));
    E->tree_create(w, gss("t2"));
    E->put(w, gss("t"), gss("a"), gss("1"), 0);
    E->put(w, gss("t2"), gss("b"), gss("2"), 0);
    E->commit(w, 1, NULL);

    GsTxn *r;
    E->begin_read(db, &r);
    struct { const char *op; int rc; } a[9];
    int n = 0;
    a[n].op = "put"; a[n++].rc = E->put(r, gss("t"), gss("x"), gss("y"), 0);
    a[n].op = "put_existing"; a[n++].rc = E->put(r, gss("t"), gss("a"), gss("changed"), 0);
    a[n].op = "delete"; a[n++].rc = E->del(r, gss("t"), gss("a"));
    a[n].op = "tree_create"; a[n++].rc = E->tree_create(r, gss("t3"));
    a[n].op = "tree_drop"; a[n++].rc = E->tree_drop(r, gss("t2"));
    a[n].op = "tree_replace"; a[n++].rc = E->tree_replace(r, gss("t"), gss("t2"));
    GsCursor *c;
    int pos;
    int crc = E->cursor_open(r, gss("t"), &c);
    if (crc == GS_OK) {
        E->first(c, &pos);
        a[n].op = "cursor_put"; a[n++].rc = E->cursor_put(c, gss("changed"));
        a[n].op = "cursor_delete"; a[n++].rc = E->cursor_delete(c);
        E->cursor_close(c);
    }
    uint64_t id = 0;
    a[n].op = "commit"; a[n++].rc = E->commit(r, 1, &id);
    /* commit on a snapshot must not succeed; whether it also releases the
     * snapshot is not specified, so the snapshot is not used after it */

    char fields[1024];
    size_t off = (size_t)snprintf(fields, sizeof fields, "\"refused\":{");
    for (int i = 0; i < n; i++) {
        off += (size_t)snprintf(fields + off, sizeof fields - off, "%s\"%s\":\"%s\"", i ? "," : "", a[i].op, gs_status_name(a[i].rc));
        if (strcmp(a[i].op, "commit") == 0) REQUIRE(a[i].rc != GS_OK, "commit through a snapshot answered OK%s%lld", "", 0);
        else REQUIRE(a[i].rc == GS_READ_ONLY, "%s through a snapshot was not refused with READ_ONLY", a[i].op, 0);
    }
    snprintf(fields + off, sizeof fields - off, "},");
    if (a[n - 1].rc != GS_OK) E->abort(r);

    /* the refusals changed nothing: a fresh snapshot, and again after reopening */
    for (int pass = 0; pass < 2; pass++) {
        GsTxn *v;
        GsBytes val;
        E->begin_read(db, &v);
        REQUIRE(E->get(v, gss("t"), gss("a"), &val) == GS_OK && val.n == 1 && val.p[0] == '1', "t/a changed (pass %s%lld)", "", pass);
        REQUIRE(E->get(v, gss("t"), gss("x"), &val) == GS_NOT_FOUND, "t/x appeared (pass %s%lld)", "", pass);
        REQUIRE(E->get(v, gss("t2"), gss("b"), &val) == GS_OK, "t2 lost its record (pass %s%lld)", "", pass);
        REQUIRE(E->get(v, gss("t3"), gss("b"), &val) == GS_NOT_FOUND, "t3 exists (pass %s%lld)", "", pass);
        E->abort(v);
        if (pass == 0) { E->close(db); db = open_store(g_path, 0); if (!db) break; }
    }
    if (db) E->close(db);
    return finish("g4", fields);
}

/* ---- G1: replace a dataset atomically under a live reader ---------------- */

static int t_g1(uint64_t rows) {
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("g1", "");
    double t0 = now_ms();
    int rc = load(db, "ds", rows, 1, 1);
    double load1 = now_ms() - t0;
    REQUIRE(rc == GS_OK, "first load answered %s%lld", gs_status_name(rc), 0);
    uint64_t bytes_one = dir_bytes(g_dir);

    GsTxn *old;
    E->begin_read(db, &old);
    uint64_t cnt, m;

    /* the refresh: a new tree written in full, then swapped in, in ONE commit */
    t0 = now_ms();
    GsTxn *w;
    E->begin_write(db, &w);
    E->tree_create(w, gss("ds.new"));
    char k[32];
    unsigned char v[VLEN];
    for (uint64_t i = 0; i < rows && rc == GS_OK; i++) {
        size_t kn = key_of(k, i);
        value_of(v, 2, i);
        rc = E->put(w, gss("ds.new"), gsb(k, kn), gsb(v, VLEN), GS_APPEND);
    }
    double write2 = now_ms() - t0;
    t0 = now_ms();
    if (rc == GS_OK) rc = E->tree_replace(w, gss("ds"), gss("ds.new"));
    double swap_call = now_ms() - t0;
    if (rc == GS_OK) rc = E->commit(w, 1, NULL); else E->abort(w);
    double commit = now_ms() - t0;
    REQUIRE(rc == GS_OK, "the replacing commit answered %s%lld", gs_status_name(rc), 0);

    /* the old reader still sees all of the old dataset and none of the new */
    rc = scan_gen(old, "ds", 1, &cnt, &m);
    REQUIRE(rc == GS_OK && cnt == rows && m == rows, "the held snapshot saw %s%lld old records", "", (long long)m);
    /* a reader after the commit sees only the new one, and no source tree */
    GsTxn *nw;
    E->begin_read(db, &nw);
    rc = scan_gen(nw, "ds", 2, &cnt, &m);
    REQUIRE(rc == GS_OK && cnt == rows && m == rows, "a new snapshot saw %s%lld new records", "", (long long)m);
    GsCursor *c;
    REQUIRE(E->cursor_open(nw, gss("ds.new"), &c) == GS_NOT_FOUND, "the source tree is still there%s%lld", "", 0);
    E->abort(nw);
    uint64_t bytes_held = dir_bytes(g_dir);

    /* release the old reader; does the old dataset's space come back? Some
     * ordinary commits follow, since a store may reclaim only when it next
     * writes; the count is reported, not tuned */
    E->abort(old);
    int churn = 0;
    E->begin_write(db, &w);
    E->tree_create(w, gss("other"));
    E->commit(w, 1, NULL);
    for (; churn < 2000; churn++) update_one(db, "other", (uint64_t)churn % 100, 3, (churn % 50) == 49);
    E->sync(db, NULL);
    uint64_t bytes_after = dir_bytes(g_dir);
    E->close(db);
    db = open_store(g_path, 0);
    uint64_t bytes_reopened = dir_bytes(g_dir);
    if (db) {
        /* and the new dataset is still whole after all of that */
        GsTxn *r;
        E->begin_read(db, &r);
        rc = scan_gen(r, "ds", 2, &cnt, &m);
        REQUIRE(rc == GS_OK && cnt == rows && m == rows, "after reopening, %s%lld new records", "", (long long)m);
        E->abort(r);
        E->close(db);
    }

    char fields[600];
    snprintf(fields, sizeof fields,
             "\"rows\":%llu,\"load_ms\":%.1f,\"write_new_ms\":%.1f,\"swap_call_ms\":%.3f,\"swap_commit_ms\":%.2f,"
             "\"bytes_one\":%llu,\"bytes_held\":%llu,\"churn_commits\":%d,\"bytes_after_release\":%llu,\"bytes_reopened\":%llu,",
             (unsigned long long)rows, load1, write2, swap_call, commit, (unsigned long long)bytes_one,
             (unsigned long long)bytes_held, churn, (unsigned long long)bytes_after, (unsigned long long)bytes_reopened);
    return finish("g1", fields);
}

/* ---- online backup ------------------------------------------------------- */

static int t_backup(uint64_t rows, uint64_t after) {
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("backup", "");
    int rc = load(db, "t", rows, 1, 1);
    REQUIRE(rc == GS_OK, "load answered %s%lld", gs_status_name(rc), 0);
    GsTxn *snap;
    E->begin_read(db, &snap);
    /* writers carry on after the snapshot: the backup must not see them */
    double t0 = now_ms();
    for (uint64_t i = 0; i < after; i++) update_one(db, "t", (i * 7919) % rows, 2, 1);
    double writes_ms = now_ms() - t0;
    char bdir[700], bpath[720];
    snprintf(bdir, sizeof bdir, "%s/backup", g_dir);
    mkdir(bdir, 0700);
    snprintf(bpath, sizeof bpath, "%s/db", bdir);
    uint64_t store_bytes = dir_bytes(g_dir);
    t0 = now_ms();
    rc = E->backup(snap, bpath);
    double backup_ms = now_ms() - t0;
    REQUIRE(rc == GS_OK, "backup answered %s%lld", gs_status_name(rc), 0);
    E->abort(snap);
    /* and writers carry on after the backup, through the same store */
    for (uint64_t i = 0; i < 100; i++) update_one(db, "t", i, 3, i == 99);
    E->close(db);

    uint64_t backup_bytes = dir_bytes(bdir);
    t0 = now_ms();
    GsStore *b = open_store(bpath, 0);
    double open_ms = now_ms() - t0;
    uint64_t cnt = 0, m = 0;
    if (b) {
        GsTxn *r;
        E->begin_read(b, &r);
        rc = scan_gen(r, "t", 1, &cnt, &m);
        REQUIRE(rc == GS_OK && cnt == rows && m == rows, "the backup held %s%lld records of the snapshot", "", (long long)m);
        E->abort(r);
        REQUIRE(E->check(b, NULL, NULL) == GS_OK, "the backup fails check%s%lld", "", 0);
        E->close(b);
    }
    char fields[400];
    snprintf(fields, sizeof fields,
             "\"rows\":%llu,\"commits_after_snapshot\":%llu,\"writes_ms\":%.1f,\"store_bytes\":%llu,\"backup_ms\":%.1f,"
             "\"backup_bytes\":%llu,\"mb_per_s\":%.1f,\"backup_open_ms\":%.1f,",
             (unsigned long long)rows, (unsigned long long)after, writes_ms, (unsigned long long)store_bytes, backup_ms,
             (unsigned long long)backup_bytes, backup_ms > 0 ? backup_bytes / 1e3 / backup_ms : 0, open_ms);
    return finish("backup", fields);
}

/* ---- space while a snapshot is held -------------------------------------- */

#define HOT 1000
static int t_held(uint64_t commits, int hold) {
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("held", "");
    int rc = load(db, "t", 10000, 1, 1);
    REQUIRE(rc == GS_OK, "load answered %s%lld", gs_status_name(rc), 0);
    uint64_t base = dir_bytes(g_dir);
    GsTxn *h = NULL;
    if (hold) E->begin_read(db, &h);
    uint64_t peak = base, peak_log = log_bytes(g_dir);
    double t0 = now_ms();
    for (uint64_t i = 0; i < commits; i++) {
        /* group commit, 64 per flush: what a busy owner does */
        rc = update_one(db, "t", (i * 7919) % HOT, 2 + (uint32_t)(i / HOT), (i % 64) == 63);
        if (rc) { REQUIRE(0, "update answered %s%lld", gs_status_name(rc), 0); break; }
        if (i % 1000 == 999) {
            uint64_t b = dir_bytes(g_dir), l = log_bytes(g_dir);
            if (b > peak) peak = b;
            if (l > peak_log) peak_log = l;
        }
    }
    E->sync(db, NULL);
    double ms = now_ms() - t0;
    uint64_t at_end = dir_bytes(g_dir);
    if (at_end > peak) peak = at_end;
    if (h) {
        /* the held snapshot still reads the load's data */
        uint64_t cnt, m;
        rc = scan_gen(h, "t", 1, &cnt, &m);
        REQUIRE(rc == GS_OK && cnt == 10000 && m == 10000, "the held snapshot saw %s%lld load-time records", "", (long long)m);
        E->abort(h);
    }
    /* after the release, a tenth as many commits again: is the space returned? */
    for (uint64_t i = 0; i < commits / 10; i++) update_one(db, "t", (i * 7919) % HOT, 9000, (i % 64) == 63);
    E->sync(db, NULL);
    uint64_t after = dir_bytes(g_dir), after_log = log_bytes(g_dir);
    E->close(db);
    uint64_t closed = dir_bytes(g_dir), closed_log = log_bytes(g_dir);
    char fields[600];
    snprintf(fields, sizeof fields,
             "\"hold\":%d,\"commits\":%llu,\"ms\":%.0f,\"base_bytes\":%llu,\"peak_bytes\":%llu,\"end_bytes\":%llu,"
             "\"after_release_bytes\":%llu,\"closed_bytes\":%llu,\"peak_log\":%llu,\"after_release_log\":%llu,\"closed_log\":%llu,",
             hold, (unsigned long long)commits, ms, (unsigned long long)base, (unsigned long long)peak,
             (unsigned long long)at_end, (unsigned long long)after, (unsigned long long)closed,
             (unsigned long long)peak_log, (unsigned long long)after_log, (unsigned long long)closed_log);
    return finish("held", fields);
}

/* ---- recovery on a long log ---------------------------------------------- */

static int t_recovery(uint64_t commits, int hold) {
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("recovery", "");
    int rc = load(db, "t", 10000, 1, 1);
    REQUIRE(rc == GS_OK, "load answered %s%lld", gs_status_name(rc), 0);
    E->close(db);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        GsStore *c = open_store(g_path, 0);
        if (!c) _exit(3);
        GsTxn *h = NULL;
        if (hold) E->begin_read(c, &h);
        for (uint64_t i = 0; i < commits; i++)
            if (update_one(c, "t", (i * 7919) % 10000, 2, (i % 64) == 63 || i + 1 == commits)) _exit(4);
        _exit(0); /* no close, no release: the owner dies */
    }
    int st;
    waitpid(pid, &st, 0);
    REQUIRE(WIFEXITED(st) && WEXITSTATUS(st) == 0, "the writing child failed%s (%lld)", "", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    uint64_t crash_bytes = dir_bytes(g_dir), crash_log = log_bytes(g_dir);
    double t0 = now_ms();
    db = open_store(g_path, 0);
    double open_ms = now_ms() - t0;
    if (db) {
        /* every commit survived: the last one wrote key (commits-1)*7919 % 10000 */
        GsTxn *r;
        GsBytes v;
        E->begin_read(db, &r);
        char k[32];
        size_t kn = key_of(k, ((commits - 1) * 7919) % 10000);
        REQUIRE(E->get(r, gss("t"), gsb(k, kn), &v) == GS_OK && v.n == VLEN && get32(v.p) == 2,
                "the last durable commit was lost%s%lld", "", 0);
        E->abort(r);
        E->close(db);
    }
    char fields[300];
    snprintf(fields, sizeof fields, "\"hold\":%d,\"commits\":%llu,\"crash_bytes\":%llu,\"crash_log\":%llu,\"recover_ms\":%.1f,",
             hold, (unsigned long long)commits, (unsigned long long)crash_bytes, (unsigned long long)crash_log, open_ms);
    return finish("recovery", fields);
}

/* ---- the backup in slices, with a commit between every two ---------------- */

static int dcmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

static int t_sbackup(uint64_t rows, uint64_t budget) {
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("sbackup", "");
    int rc = load(db, "t", rows, 1, 1);
    REQUIRE(rc == GS_OK, "load answered %s%lld", gs_status_name(rc), 0);
    uint64_t base = dir_bytes(g_dir) + log_bytes(g_dir), peak = base;
    GsTxn *snap;
    E->begin_read(db, &snap);
    char bdir[700], bpath[720];
    snprintf(bdir, sizeof bdir, "%s/backup", g_dir);
    mkdir(bdir, 0700);
    snprintf(bpath, sizeof bpath, "%s/db", bdir);
    GsBackup *bk = NULL;
    double t0 = now_ms(), step_max = 0, commit_max = 0;
    rc = E->backup_begin(snap, bpath, &bk);
    double begin_ms = now_ms() - t0;
    int done = 0;
    uint64_t steps = 0, commits = 0;
    double *lat = NULL;
    size_t nlat = 0, caplat = 0;
    while (rc == GS_OK && !done) {
        double a = now_ms();
        rc = E->backup_step(bk, budget, &done);
        double d = now_ms() - a;
        if (d > step_max) step_max = d;
        if (nlat == caplat) { caplat = caplat ? caplat * 2 : 1024; lat = realloc(lat, caplat * sizeof *lat); }
        lat[nlat++] = d;
        steps++;
        a = now_ms();
        if (update_one(db, "t", (commits * 7919) % rows, 2, commits % 64 == 63)) break;
        d = now_ms() - a;
        if (d > commit_max) commit_max = d;
        commits++;
        if (steps % 64 == 0) { uint64_t b = dir_bytes(g_dir) + log_bytes(g_dir); if (b > peak) peak = b; }
    }
    double total = now_ms() - t0;
    qsort(lat, nlat, sizeof *lat, dcmp);
    double p50 = nlat ? lat[nlat / 2] : 0, p99 = nlat ? lat[(nlat * 99) / 100 < nlat ? (nlat * 99) / 100 : nlat - 1] : 0;
    free(lat);
    REQUIRE(rc == GS_OK && done, "the backup answered %s after %lld steps", gs_status_name(rc), steps);
    E->backup_end(bk);
    E->abort(snap);
    uint64_t at_end = dir_bytes(g_dir) + log_bytes(g_dir), backup_bytes = dir_bytes_of(bdir, 2);
    E->close(db);
    GsStore *b = open_store(bpath, 0);
    if (b) {
        GsTxn *r;
        uint64_t cnt, m;
        E->begin_read(b, &r);
        rc = scan_gen(r, "t", 1, &cnt, &m);
        REQUIRE(rc == GS_OK && cnt == rows && m == rows, "the backup held %s%lld records of the snapshot", "", (long long)m);
        E->abort(r);
        REQUIRE(E->check(b, NULL, NULL) == GS_OK, "the backup fails check%s%lld", "", 0);
        E->close(b);
    }
    char fields[600];
    snprintf(fields, sizeof fields,
             "\"rows\":%llu,\"budget\":%llu,\"begin_ms\":%.2f,\"steps\":%llu,\"step_p50_ms\":%.2f,\"step_p99_ms\":%.2f,\"step_max_ms\":%.2f,\"commits_during\":%llu,"
             "\"commit_max_ms\":%.2f,\"total_ms\":%.0f,\"base_bytes\":%llu,\"peak_bytes\":%llu,\"end_bytes\":%llu,\"backup_bytes\":%llu,",
             (unsigned long long)rows, (unsigned long long)budget, begin_ms, (unsigned long long)steps, p50, p99, step_max,
             (unsigned long long)commits, commit_max, total, (unsigned long long)base, (unsigned long long)peak,
             (unsigned long long)at_end, (unsigned long long)backup_bytes);
    return finish("sbackup", fields);
}

/* ---- open question 6: how many files, and how many held open ------------- */

static unsigned open_fds(void) {
    DIR *d = opendir("/proc/self/fd");
    unsigned n = 0;
    if (!d) return 0;
    struct dirent *de;
    while ((de = readdir(d))) if (de->d_name[0] != '.') n++;
    closedir(d);
    return n - 1; /* the listing's own */
}

static void count_kinds(const char *dir, unsigned *total, unsigned *sst, unsigned *blob, unsigned *logs, unsigned *other) {
    DIR *d = opendir(dir);
    *total = *sst = *blob = *logs = *other = 0;
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        (*total)++;
        if (strstr(de->d_name, ".sst")) (*sst)++;
        else if (strstr(de->d_name, ".blob")) (*blob)++;
        else if (is_log(de->d_name)) (*logs)++;
        else (*other)++;
    }
    closedir(d);
}

static uint64_t scramble(uint64_t i) { i ^= i >> 31; i *= 0x9E3779B97F4A7C15ULL; return i ^ (i >> 29); }

static int t_files(uint64_t rows) {
    unsigned fd0 = open_fds();
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("files", "");
    GsTxn *w;
    E->begin_write(db, &w);
    E->tree_create(w, gss("t"));
    E->commit(w, 1, NULL);
    char k[32];
    unsigned char v[VLEN];
    int rc = GS_OK;
    double t0 = now_ms();
    for (uint64_t i = 0; i < rows && rc == GS_OK;) {
        E->begin_write(db, &w);
        for (uint64_t j = 0; j < 10000 && i < rows && rc == GS_OK; j++, i++) {
            size_t kn = key_of(k, scramble(i) % (rows * 4));
            value_of(v, 1, i);
            rc = E->put(w, gss("t"), gsb(k, kn), gsb(v, VLEN), 0);
        }
        if (rc == GS_OK) rc = E->commit(w, 1, NULL); else E->abort(w);
    }
    double load_ms = now_ms() - t0;
    REQUIRE(rc == GS_OK, "the load answered %s%lld", gs_status_name(rc), 0);
    unsigned fd_loaded = open_fds() - fd0;
    E->close(db);
    unsigned tot, sst, blob, logs, other;
    count_kinds(g_path, &tot, &sst, &blob, &logs, &other); /* the store's own directory */
    t0 = now_ms();
    db = open_store(g_path, 0);
    double open_ms = now_ms() - t0;
    unsigned fd_open = open_fds() - fd0;
    if (db) {
        GsTxn *r;
        E->begin_read(db, &r);
        for (uint64_t i = 0; i < 2000; i++) {
            GsBytes val;
            size_t kn = key_of(k, scramble(i * 7919 % rows) % (rows * 4));
            E->get(r, gss("t"), gsb(k, kn), &val);
        }
        E->abort(r);
    }
    unsigned fd_read = open_fds() - fd0;
    if (db) E->close(db);
    char fields[500];
    snprintf(fields, sizeof fields,
             "\"rows\":%llu,\"load_ms\":%.0f,\"files\":%u,\"sst\":%u,\"blob\":%u,\"logs\":%u,\"other\":%u,"
             "\"bytes\":%llu,\"fds_after_load\":%u,\"fds_after_open\":%u,\"fds_after_reads\":%u,\"open_ms\":%.1f,",
             (unsigned long long)rows, load_ms, tot, sst, blob, logs, other, (unsigned long long)dir_bytes_of(g_dir, 2),
             fd_loaded, fd_open, fd_read, open_ms);
    return finish("files", fields);
}

/* ---- the G-Dash profile (§5.1.1) and the handoff (§5.5) ------------------- */

/* A G-Dash row: a group column (20 groups), a filter column (10 values) and
 * an amount, inside the same 100-byte value every other test here writes. The
 * expected answers are computed from i by a plain loop, not read back. */
#define GD_GROUPS 20
static unsigned gd_group(uint64_t i) { return (unsigned)((i * 7) % GD_GROUPS); }
static unsigned gd_flt(uint64_t i) { return (unsigned)(i % 10); }
static uint64_t gd_amount(uint64_t i, uint32_t gen) { return i % 997 + gen; }
static void gd_value(unsigned char *v, uint32_t gen, uint64_t i) {
    value_of(v, gen, i);
    v[12] = (unsigned char)gd_group(i);
    v[13] = (unsigned char)gd_flt(i);
    put64(v + 14, gd_amount(i, gen));
}
static int gd_load(GsStore *db, const char *tree, uint64_t rows, uint32_t gen) {
    GsTxn *w;
    int rc = E->begin_write(db, &w);
    if (rc) return rc;
    if ((rc = E->tree_create(w, gss(tree)))) { E->abort(w); return rc; }
    char k[32];
    unsigned char v[VLEN];
    for (uint64_t i = 0; i < rows && rc == GS_OK; i++) {
        size_t kn = key_of(k, i);
        gd_value(v, gen, i);
        rc = E->put(w, gss(tree), gsb(k, kn), gsb(v, VLEN), GS_APPEND);
    }
    if (rc) { E->abort(w); return rc; }
    return E->commit(w, 1, NULL);
}
/* sum(amount) group by group, optionally where flt = want; every row read */
static int gd_query(GsTxn *t, const char *tree, int want, uint64_t *sums, uint64_t *rows) {
    GsCursor *c;
    memset(sums, 0, GD_GROUPS * sizeof *sums);
    *rows = 0;
    int rc = E->cursor_open(t, gss(tree), &c);
    if (rc) return rc;
    int pos;
    for (rc = E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos)) {
        GsBytes v;
        E->value(c, &v);
        (*rows)++;
        if (v.n != VLEN || v.p[12] >= GD_GROUPS) return GS_CORRUPT;
        if (want >= 0 && v.p[13] != want) continue;
        sums[v.p[12]] += get64(v.p + 14);
    }
    E->cursor_close(c);
    return rc;
}
static int gd_expect(uint64_t rows, uint32_t gen, int want, const uint64_t *sums) {
    uint64_t e[GD_GROUPS] = {0};
    for (uint64_t i = 0; i < rows; i++)
        if (want < 0 || (int)gd_flt(i) == want) e[gd_group(i)] += gd_amount(i, gen);
    return memcmp(e, sums, sizeof e) == 0;
}
static double median5(double *x) { qsort(x, 5, sizeof *x, dcmp); return x[2]; }

/* G2: the dominant query, idle and with a refresh half written */
static int t_g2(uint64_t rows) {
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("g2", "");
    int rc = gd_load(db, "ds", rows, 1);
    REQUIRE(rc == GS_OK, "the load answered %s%lld", gs_status_name(rc), 0);
    uint64_t sums[GD_GROUPS], n;
    double t[2][2][5]; /* [during][filtered][rep] */
    for (int during = 0; during < 2 && !g_failed; during++) {
        GsTxn *w = NULL;
        if (during) {
            /* G1's refresh, half written and not committed: the query must
             * still answer from the live dataset, at its own cost */
            E->begin_write(db, &w);
            E->tree_create(w, gss("ds.new"));
            char k[32];
            unsigned char v[VLEN];
            for (uint64_t i = 0; i < rows / 2 && rc == GS_OK; i++) {
                size_t kn = key_of(k, i);
                gd_value(v, 2, i);
                rc = E->put(w, gss("ds.new"), gsb(k, kn), gsb(v, VLEN), GS_APPEND);
            }
            REQUIRE(rc == GS_OK, "the half refresh answered %s%lld", gs_status_name(rc), 0);
        }
        for (int f = 0; f < 2; f++)
            for (int r = 0; r < 5; r++) {
                GsTxn *rd;
                double t0 = now_ms();
                rc = E->begin_read(db, &rd);
                if (rc == GS_OK) { rc = gd_query(rd, "ds", f ? 3 : -1, sums, &n); E->abort(rd); }
                t[during][f][r] = now_ms() - t0;
                REQUIRE(rc == GS_OK && n == rows, "the query answered %s, %lld rows", gs_status_name(rc), n);
                REQUIRE(gd_expect(rows, 1, f ? 3 : -1, sums), "the sums were wrong%s (during=%lld)", "", during);
            }
        if (w) E->abort(w);
    }
    E->close(db);
    char fields[400];
    double a = median5(t[0][0]), b = median5(t[0][1]), c = median5(t[1][0]), d = median5(t[1][1]);
    snprintf(fields, sizeof fields,
             "\"rows\":%llu,\"query_ms\":%.3f,\"filtered_ms\":%.3f,\"during_refresh_ms\":%.3f,"
             "\"during_refresh_filtered_ms\":%.3f,\"rows_per_s\":%.0f,",
             (unsigned long long)rows, a, b, c, d, a > 0 ? rows / a * 1e3 : 0);
    return finish("g2", fields);
}

/* G3 and G6: the per-request cost against how many datasets the store holds,
 * and a query naming 2 of 20 against one naming all 20 */
static int t_g3(uint64_t trees, uint64_t rows) {
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("g3", "");
    char name[32];
    int rc = GS_OK;
    double t0 = now_ms();
    for (uint64_t d = 0; d < trees && rc == GS_OK; d++) {
        snprintf(name, sizeof name, "ds%05llu", (unsigned long long)d);
        rc = gd_load(db, name, rows, 1);
    }
    double load_ms = now_ms() - t0;
    REQUIRE(rc == GS_OK, "the load answered %s%lld", gs_status_name(rc), 0);
    /* the request: snapshot, one row from one dataset, release */
    enum { REQ = 20000 };
    static double lat[REQ];
    char k[32];
    for (int r = 0; r < REQ && !g_failed; r++) {
        uint64_t d = scramble((uint64_t)r) % trees, i = scramble((uint64_t)r + 77) % rows;
        snprintf(name, sizeof name, "ds%05llu", (unsigned long long)d);
        size_t kn = key_of(k, i);
        GsTxn *rd;
        GsBytes v;
        double s = now_ms();
        rc = E->begin_read(db, &rd);
        if (rc == GS_OK) { rc = E->get(rd, gss(name), gsb(k, kn), &v); if (rc == GS_OK && (v.n != VLEN || get64(v.p + 4) != i)) rc = GS_CORRUPT; E->abort(rd); }
        lat[r] = now_ms() - s;
        REQUIRE(rc == GS_OK, "a request answered %s%lld", gs_status_name(rc), 0);
    }
    double sum = 0;
    for (int r = 0; r < REQ; r++) sum += lat[r];
    qsort(lat, REQ, sizeof *lat, dcmp);
    /* G6, when there are at least 20: 2 named against all 20 */
    double two = 0, all = 0;
    if (trees >= 20 && !g_failed) {
        uint64_t sums[GD_GROUPS], n, tot[GD_GROUPS];
        for (int pass = 0; pass < 2; pass++) {
            int named = pass ? 20 : 2;
            double x[5];
            for (int r = 0; r < 5; r++) {
                GsTxn *rd;
                memset(tot, 0, sizeof tot);
                double s = now_ms();
                E->begin_read(db, &rd);
                for (int d = 0; d < named && rc == GS_OK; d++) {
                    snprintf(name, sizeof name, "ds%05d", d * (pass ? 1 : 7)); /* 0 and 7 */
                    rc = gd_query(rd, name, -1, sums, &n);
                    for (int g = 0; g < GD_GROUPS; g++) tot[g] += sums[g];
                }
                E->abort(rd);
                x[r] = now_ms() - s;
                REQUIRE(rc == GS_OK, "a G6 query answered %s%lld", gs_status_name(rc), 0);
                for (int g = 0; g < GD_GROUPS; g++) sums[g] = tot[g] / (uint64_t)named;
                REQUIRE(gd_expect(rows, 1, -1, sums) && tot[0] % (uint64_t)named == 0, "G6 sums were wrong%s%lld", "", named);
            }
            if (pass) all = median5(x); else two = median5(x);
        }
    }
    E->close(db);
    char fields[400];
    snprintf(fields, sizeof fields,
             "\"trees\":%llu,\"rows\":%llu,\"load_ms\":%.0f,\"request_mean_us\":%.2f,\"request_p50_us\":%.2f,"
             "\"request_p99_us\":%.2f,\"g6_two_ms\":%.3f,\"g6_all_ms\":%.3f,",
             (unsigned long long)trees, (unsigned long long)rows, load_ms, sum / REQ * 1e3, lat[REQ / 2] * 1e3,
             lat[REQ * 99 / 100] * 1e3, two, all);
    return finish("g3", fields);
}

/* G5: one transaction of ROWS rows, peak memory against the memory at open.
 * The bound is the engine's own cache and buffers, whatever ROWS is. */
static uint64_t status_kb(const char *field) {
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    uint64_t kb = 0;
    size_t fl = strlen(field);
    while (f && fgets(line, sizeof line, f))
        if (!strncmp(line, field, fl)) { kb = strtoull(line + fl + 1, NULL, 10); break; }
    if (f) fclose(f);
    return kb;
}
static int t_g5(uint64_t rows, int random) {
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("g5", "");
    uint64_t base = status_kb("VmRSS");
    GsTxn *w;
    E->begin_write(db, &w);
    int rc = E->tree_create(w, gss("ds"));
    char k[32];
    unsigned char v[VLEN];
    double t0 = now_ms();
    for (uint64_t i = 0; i < rows && rc == GS_OK; i++) {
        uint64_t key = random ? scramble(i) : i;
        size_t kn = key_of(k, key);
        gd_value(v, 1, key);
        rc = E->put(w, gss("ds"), gsb(k, kn), gsb(v, VLEN), random ? 0 : GS_APPEND);
    }
    if (rc == GS_OK) rc = E->commit(w, 1, NULL); else E->abort(w);
    double ms = now_ms() - t0;
    REQUIRE(rc == GS_OK, "the transaction answered %s%lld", gs_status_name(rc), 0);
    uint64_t peak = status_kb("VmHWM");
    /* the whole transaction is there */
    uint64_t sums[GD_GROUPS], n = 0;
    GsTxn *rd;
    E->begin_read(db, &rd);
    rc = gd_query(rd, "ds", -1, sums, &n);
    E->abort(rd);
    REQUIRE(rc == GS_OK && n == rows, "after the commit %s%lld rows", "", (long long)n);
    E->close(db);
    char fields[300];
    snprintf(fields, sizeof fields,
             "\"rows\":%llu,\"keys\":\"%s\",\"ms\":%.0f,\"rss_at_open_kb\":%llu,\"peak_kb\":%llu,\"peak_over_open_kb\":%llu,"
             "\"data_kb\":%llu,",
             (unsigned long long)rows, random ? "random" : "sequential", ms, (unsigned long long)base,
             (unsigned long long)peak, (unsigned long long)(peak - base), (unsigned long long)(dir_bytes_of(g_dir, 2) >> 10));
    return finish("g5", fields);
}

/* The handoff (§5.5): an owner serving writes dies, cleanly or by SIGKILL in
 * the middle of an unsynced stream, and a new process takes the store, opens
 * it and serves its first request. The lock is the store's own, so taking it
 * is inside the open. COMMITS sets how much log the dead owner leaves. */
static int t_handoff(uint64_t commits, int kill_it) {
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("handoff", "");
    int rc = load(db, "t", 10000, 1, 1);
    REQUIRE(rc == GS_OK, "load answered %s%lld", gs_status_name(rc), 0);
    E->close(db);
    int p[2];
    if (pipe(p)) { fail("pipe%s%lld", "", 0); return finish("handoff", ""); }
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        close(p[0]);
        GsStore *c = open_store(g_path, 0);
        if (!c) _exit(3);
        for (uint64_t i = 0; i < commits; i++)
            if (update_one(c, "t", (i * 7919) % 10000, 2 + (uint32_t)i, (i % 64) == 63 || i + 1 == commits)) _exit(4);
        if (kill_it != 1) {
            /* mode 2, a PLANNED handoff: the log emptied first (`flush`,
             * step 5), timed from the decision to hand over */
            double t_ho = now_ms();
            if (kill_it == 2 && (!E->flush || E->flush(c))) _exit(6);
            if (E->close(c)) _exit(5);
            double ho = now_ms() - t_ho;
            (void)!write(p[1], &ho, sizeof ho);
            _exit(0);
        }
        double none = 0;
        (void)!write(p[1], &none, sizeof none);
        /* unsynced writes to keys nobody checks, until the signal */
        for (uint64_t i = 0;; i++) update_one(c, "t", 10000 + i % 50000, 1, 0);
    }
    close(p[1]);
    double close_ms = 0;
    REQUIRE(read(p[0], &close_ms, sizeof close_ms) == sizeof close_ms, "the writing child failed%s%lld", "", 0);
    close(p[0]);
    double t_dead = 0;
    if (kill_it == 1) {
        struct timespec ts = {0, 20 * 1000000};
        nanosleep(&ts, NULL);
        kill(pid, SIGKILL);
    }
    int st;
    waitpid(pid, &st, 0);
    t_dead = now_ms(); /* the owner is gone: the handoff starts here */
    REQUIRE(kill_it == 1 ? (WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL) : (WIFEXITED(st) && WEXITSTATUS(st) == 0),
            "the child ended unexpectedly%s (%lld)", "", st);
    uint64_t log_at = log_bytes(g_dir);
    double t0 = now_ms();
    db = open_store(g_path, 0);
    double open_ms = now_ms() - t0, first_ms = 0;
    uint64_t kept = 0;
    if (db) {
        /* the first request, and the last durable commit is there */
        GsTxn *r;
        GsBytes v;
        char k[32];
        size_t kn = key_of(k, ((commits - 1) * 7919) % 10000);
        E->begin_read(db, &r);
        rc = E->get(r, gss("t"), gsb(k, kn), &v);
        first_ms = now_ms() - t_dead;
        REQUIRE(rc == GS_OK && v.n == VLEN && get32(v.p) == 2 + (uint32_t)(commits - 1),
                "the last durable commit was lost%s%lld", "", 0);
        /* informative: how many of the unsynced writes came through */
        for (uint64_t i = 0; i < 50000; i++) {
            kn = key_of(k, 10000 + i);
            if (E->get(r, gss("t"), gsb(k, kn), &v) == GS_OK) kept++;
        }
        E->abort(r);
        E->close(db);
    }
    char fields[360];
    snprintf(fields, sizeof fields,
             "\"mode\":\"%s\",\"commits\":%llu,\"log_bytes\":%llu,\"open_ms\":%.2f,\"to_first_read_ms\":%.2f,"
             "\"old_owner_close_ms\":%.2f,\"unsynced_keys_kept\":%llu,",
             kill_it == 1 ? "sigkill" : kill_it == 2 ? "flushed" : "clean", (unsigned long long)commits,
             (unsigned long long)log_at, open_ms, first_ms, close_ms, (unsigned long long)kept);
    return finish("handoff", fields);
}

/* Space under group commit: COMMITS transactions of K random updates each,
 * synced every G commits, over a 10^5-row table. 2.A frees a page only once
 * the commit that freed it is durable, so unsynced commits that each rewrite
 * many pages hold them all until the next sync (found building W9). */
static int t_gspace(uint64_t k, uint64_t g) {
    GsStore *db = open_store(g_path, 1);
    if (!db) return finish("gspace", "");
    const uint64_t rows = 100000, commits = 2000;
    int rc = load(db, "t", rows, 1, 1);
    REQUIRE(rc == GS_OK, "load answered %s%lld", gs_status_name(rc), 0);
    E->sync(db, NULL);
    uint64_t base = dir_bytes_of(g_dir, 2), peak = base;
    char kb[32];
    unsigned char v[VLEN];
    double t0 = now_ms();
    for (uint64_t c = 0; c < commits && rc == GS_OK; c++) {
        GsTxn *w;
        E->begin_write(db, &w);
        for (uint64_t j = 0; j < k && rc == GS_OK; j++) {
            uint64_t i = scramble(c * k + j) % rows;
            size_t kn = key_of(kb, i);
            value_of(v, 2, i);
            rc = E->put(w, gss("t"), gsb(kb, kn), gsb(v, VLEN), 0);
        }
        if (rc == GS_OK) rc = E->commit(w, 0, NULL); else E->abort(w);
        if (rc == GS_OK && (c + 1) % g == 0) rc = E->sync(db, NULL);
        if (c % 50 == 49) { uint64_t b = dir_bytes_of(g_dir, 2); if (b > peak) peak = b; }
    }
    if (rc == GS_OK) rc = E->sync(db, NULL);
    double ms = now_ms() - t0;
    REQUIRE(rc == GS_OK, "a commit answered %s%lld", gs_status_name(rc), 0);
    uint64_t end = dir_bytes_of(g_dir, 2), cnt, m;
    GsTxn *r;
    E->begin_read(db, &r);
    rc = scan_gen(r, "t", 1, &cnt, &m);
    E->abort(r);
    REQUIRE(rc == GS_OK && cnt == rows, "the table has %s%lld rows", "", (long long)cnt);
    E->close(db);
    char fields[300];
    snprintf(fields, sizeof fields,
             "\"rows_per_txn\":%llu,\"sync_every\":%llu,\"commits\":%llu,\"ms\":%.0f,\"base_bytes\":%llu,"
             "\"peak_bytes\":%llu,\"end_bytes\":%llu,\"peak_over_base\":%.2f,",
             (unsigned long long)k, (unsigned long long)g, (unsigned long long)commits, ms, (unsigned long long)base,
             (unsigned long long)peak, (unsigned long long)end, (double)peak / (double)base);
    return finish("gspace", fields);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: hardreq g4|g1|backup|held|recovery ENGINE DIR [ARGS]\n");
        return 2;
    }
    setup(argv[2], argv[3]);
    const char *t = argv[1];
    uint64_t a = argc > 4 ? strtoull(argv[4], NULL, 10) : 0, b = argc > 5 ? strtoull(argv[5], NULL, 10) : 0;
    if (!strcmp(t, "g4")) return t_g4();
    if (!strcmp(t, "g1")) return t_g1(a);
    if (!strcmp(t, "backup")) return t_backup(a, b);
    if (!strcmp(t, "held")) return t_held(a, (int)b);
    if (!strcmp(t, "recovery")) return t_recovery(a, (int)b);
    if (!strcmp(t, "sbackup")) return t_sbackup(a, b);
    if (!strcmp(t, "files")) return t_files(a);
    if (!strcmp(t, "g2")) return t_g2(a);
    if (!strcmp(t, "g3")) return t_g3(a, b);
    if (!strcmp(t, "g5")) return t_g5(a, (int)b);
    if (!strcmp(t, "handoff")) return t_handoff(a, (int)b);
    if (!strcmp(t, "gspace")) return t_gspace(a, b);
    fprintf(stderr, "unknown test %s\n", t);
    return 2;
}

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
#include "common.h"

#include <dirent.h>
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
    GsOptions o = {create, 4096, 0, NULL, 0, 0, 0};
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
    fprintf(stderr, "unknown test %s\n", t);
    return 2;
}

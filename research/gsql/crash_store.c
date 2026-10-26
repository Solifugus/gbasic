/* A storage candidate under the crash harness (src/gsql/crash.c, §5.3).
 *
 *   crash_store ENGINE MODE SEEDS TXNS KEYS PUTS MAX_VALUE RECOVERY POINTS [PAGE]
 *
 * SEEDS is a number or a range "1-20". POINTS is how many crash points to
 * sample per workload (0 = every operation). The adapter maps the harness's
 * one-keyspace engine onto one tree, "t", created on first use. */

#include "common.h"
#include "gsql_crash.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static const GsEngine *E;
static uint32_t page_size = 512;

static int map(int rc) {
    switch (rc) {
    case GS_OK: return 0;
    case GS_FULL: return GSQL_E_FULL;
    case GS_FAILED: return GSQL_E_FAILED;
    case GS_CORRUPT: return GSQL_E_CORRUPT;
    case GS_IO: return EIO;
    default: return 300000 + rc;
    }
}

static void *a_open(GsqlIo *io, const char *dir, int *err) {
    char path[256];
    snprintf(path, sizeof path, "%s/db", dir);
    /* a small cache, so the bigger transactions spill (G5) inside the crash runs */
    /* GS_TUNE_A / GS_TUNE_B override the candidate knobs: §2.B's checkpoint
     * threshold decides how long stale frames stay in its log */
    const char *ta = getenv("GS_TUNE_A"), *tb = getenv("GS_TUNE_B"), *tc = getenv("GS_TUNE_C");
    /* defaults chosen to reach each candidate's background work often: §2.B
     * checkpoints every 24 frames, §2.C flushes a 2,000-byte memtable */
    uint64_t da = strcmp(E->name, "lsm") ? 24 : 2000;
    /* §2.C separates values of 24 bytes or more, so both kinds are crashed */
    GsOptions o = {1, page_size, 16 * page_size, io, ta ? strtoull(ta, NULL, 10) : da, tb ? strtoull(tb, NULL, 10) : 3,
                   tc ? strtoull(tc, NULL, 10) : 24};
    GsStore *db;
    int rc = E->open(path, &o, &db);
    if (rc) { *err = map(rc); return NULL; }
    /* every recovery is checked as well as read back: a store whose contents
     * are right can still have leaked files a later open never deletes,
     * which no read can see */
    if (E->check(db, NULL, NULL) != GS_OK) { E->close(db); *err = GSQL_E_CORRUPT; return NULL; }
    return db;
}

static const unsigned char TREE[] = "t";

/* Some commits are written the way a whole-dataset refresh (G1) writes: the
 * tree is copied into a new one, the puts applied there, and the copy put in
 * place with tree_replace -- the same contents by a different route, so the
 * harness's model is unchanged while retiring a tree, and whatever reclaims a
 * retired tree, is crashed like everything else. Chosen from the transaction
 * itself, so a replay of the workload chooses the same. GS_CRASH_REPLACE=N
 * takes one in N (default 4; 0 never). */
static int replace_this(const GsqlCrashTxn *txn) {
    const char *e = getenv("GS_CRASH_REPLACE");
    unsigned n = e ? (unsigned)atoi(e) : 4;
    if (!n || !txn->count) return 0;
    uint32_t h = crc32c(txn->puts[0].key, txn->puts[0].key_len) + (uint32_t)txn->count;
    return h % n == 0;
}

static int copy_tree(GsTxn *t, GsBytes from, GsBytes to) {
    int rc = E->tree_create(t, to);
    if (rc) return rc;
    GsCursor *c;
    rc = E->cursor_open(t, from, &c);
    if (rc == GS_NOT_FOUND) return GS_OK;
    if (rc) return rc;
    int pos;
    for (rc = E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos)) {
        GsBytes k, v;
        if ((rc = E->key(c, &k)) || (rc = E->value(c, &v))) break;
        if ((rc = E->put(t, to, k, v, GS_APPEND))) break;
    }
    E->cursor_close(c);
    return rc;
}

static int a_commit(void *dbp, const GsqlCrashTxn *txn, int sync) {
    GsStore *db = dbp;
    GsTxn *t;
    int rc = E->begin_write(db, &t);
    if (rc) return map(rc);
    GsBytes tree = gsb(TREE, 1);
    int via = replace_this(txn);
    static const unsigned char NEXT[] = "t.next";
    if (via) {
        rc = copy_tree(t, tree, gsb(NEXT, 6));
        tree = gsb(NEXT, 6);
    }
    for (size_t i = 0; i < txn->count && rc == GS_OK; i++) {
        const GsqlCrashPut *p = &txn->puts[i];
        GsBytes k = gsb(p->key, p->key_len);
        if (p->value) {
            rc = E->put(t, tree, k, gsb(p->value, p->value_len), 0);
            if (rc == GS_NOT_FOUND) {
                rc = E->tree_create(t, tree);
                if (rc == GS_OK) rc = E->put(t, tree, k, gsb(p->value, p->value_len), 0);
            }
        } else {
            rc = E->del(t, tree, k);
            if (rc == GS_NOT_FOUND) rc = GS_OK;
        }
    }
    if (via && rc == GS_OK) {
        rc = E->tree_replace(t, gsb(TREE, 1), tree);
        if (rc == GS_NOT_FOUND) { /* no "t" yet: the copy is simply renamed by being the first */
            rc = E->tree_create(t, gsb(TREE, 1));
            if (rc == GS_OK) rc = E->tree_replace(t, gsb(TREE, 1), tree);
        }
    }
    if (rc) { E->abort(t); return map(rc); }
    return map(E->commit(t, sync, NULL));
}

static int a_sync(void *db) { return map(E->sync(db, NULL)); }

static int a_read_all(void *db, GsqlCrashState *out) {
    GsTxn *t;
    int rc = E->begin_read(db, &t);
    if (rc) return map(rc);
    GsCursor *c;
    rc = E->cursor_open(t, gsb(TREE, 1), &c);
    if (rc == GS_NOT_FOUND) { E->abort(t); return 0; }
    int pos;
    for (rc = rc ? rc : E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos)) {
        GsBytes k, v;
        if ((rc = E->key(c, &k)) || (rc = E->value(c, &v))) break;
        if (gsql_crash_state_put(out, k.p, k.n, v.p, v.n)) { rc = GS_IO; break; }
    }
    E->abort(t);
    return map(rc);
}

static void a_close(void *db) {
    if (E->close(db) != GS_OK) fprintf(stderr, "close refused\n");
}

int main(int argc, char **argv) {
    if (argc < 10) {
        fprintf(stderr, "usage: crash_store ENGINE MODE SEEDS TXNS KEYS PUTS MAX_VALUE RECOVERY POINTS [PAGE]\n");
        return 2;
    }
    if (!strcmp(argv[1], "cow")) E = &gs_engine_cow;
#ifdef HAVE_WAL
    else if (!strcmp(argv[1], "wal")) E = &gs_engine_wal;
#endif
#ifdef HAVE_LSM
    else if (!strcmp(argv[1], "lsm")) E = &gs_engine_lsm;
#endif
    else { fprintf(stderr, "unknown engine\n"); return 2; }
    static const char *modes[] = {"", "process", "power", "torn", "failed-sync", "full-disk"};
    int mode = 0;
    for (int i = 1; i <= 5; i++) if (!strcmp(argv[2], modes[i])) mode = i;
    if (!mode) { fprintf(stderr, "unknown mode\n"); return 2; }
    if (argc > 10) page_size = (uint32_t)strtoul(argv[10], NULL, 10);
    unsigned long long lo = strtoull(argv[3], NULL, 10), hi = lo;
    const char *dash = strchr(argv[3], '-');
    if (dash) hi = strtoull(dash + 1, NULL, 10);
    GsqlCrashEngine ce = {E->name, a_open, a_commit, a_sync, a_read_all, a_close};
    GsqlCrashResult sum = {0};
    const char *reports = getenv("GSQL_CRASH_REPORTS");
    for (unsigned long long seed = lo; seed <= hi; seed++) {
        GsqlCrashConfig cfg = {0};
        cfg.workload_seed = seed;
        cfg.transactions = strtoul(argv[4], NULL, 10);
        cfg.keyspace = strtoul(argv[5], NULL, 10);
        cfg.max_puts = strtoul(argv[6], NULL, 10);
        cfg.max_value = strtoul(argv[7], NULL, 10);
        cfg.crash_recovery = atoi(argv[8]);
        cfg.sample_points = strtoull(argv[9], NULL, 10);
        cfg.sync_percent = seed % 2 ? 20 : 80;
        cfg.images_per_point = 2;
        cfg.max_reports = reports ? (unsigned)atoi(reports) : 5;
        GsqlCrashResult r = gsql_crash_run(&ce, (GsqlCrashCase)mode, &cfg);
        sum.crashes += r.crashes;
        sum.violations += r.violations;
        sum.durable_checked += r.durable_checked;
        sum.not_prefix_ok += r.not_prefix_ok;
        sum.prefix_dropped += r.prefix_dropped;
    }
    printf("crashes=%llu violations=%llu durable_checked=%llu kept_unsynced=%llu dropped_unsynced=%llu\n",
           (unsigned long long)sum.crashes, (unsigned long long)sum.violations,
           (unsigned long long)sum.durable_checked, (unsigned long long)sum.not_prefix_ok,
           (unsigned long long)sum.prefix_dropped);
    return 0;
}

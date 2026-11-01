/* What a slower sync costs a writer: commits per second against an added
 * delay on every file and directory sync, the shape of storage further away
 * than a local disk (a NAS: a network round trip, and RAID5's parity update).
 *
 *   syncbench DIR
 *
 * The real I/O layer with its sync replaced by "sleep, then the real sync",
 * which research code may do through io_internal.h. Each delay is run with 1,
 * 10, 100 and 1000 rows to a committed, synced transaction, through SQL text.
 * Prints one JSON line per case. */
#define _POSIX_C_SOURCE 200809L
#include "gsql_sql.h"
#include "gsql_store.h"
#include "io_internal.h"
#include "v3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const GsEngine *E = &gs_engine_lsm;
static long delay_us;
static GsqlIoOps slow_ops;
static const GsqlIoOps *real_ops;
static unsigned long syncs;

static void pause_for(void) {
    struct timespec ts = { delay_us / 1000000, (delay_us % 1000000) * 1000 };
    if (delay_us) {
        nanosleep(&ts, NULL);
    }
    syncs++;
}
static int slow_sync(GsqlFile *f) {
    pause_for();
    return real_ops->sync(f);
}
static int slow_sync_dir(GsqlIo *io, const char *dir) {
    pause_for();
    return real_ops->sync_dir(io, dir);
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void run(GsqlService *svc, GsTxn *t, const char *text) {
    GsqlBuf stmt = {0};
    v3_w_text(&stmt, text);
    GsqlStatement st;
    memset(&st, 0, sizeof st);
    st.statement = stmt.bytes;
    st.statement_length = stmt.length;
    st.batch = 100;
    GsqlResult res;
    memset(&res, 0, sizeof res);
    GsqlError err = { GSQL_OK, "", 0, 0 };
    if (svc->run(svc->ctx, E, t, &st, &res, &err) != GSQL_OK) {
        fprintf(stderr, "syncbench: %s: %s\n", text, err.message);
        exit(2);
    }
    gsql_buf_free(&res.rows);
    gsql_buf_free(&res.columns);
    gsql_buf_free(&res.warnings);
    gsql_buf_free(&stmt);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: syncbench DIR\n");
        return 2;
    }
    static const long delays[] = { 0, 500, 2000, 5000, 10000 };
    static const int per_txn[] = { 1, 10, 100, 1000 };
    GsqlSql *sql = gsql_sql_new(gb_gsql_host());
    GsqlService svc = gsql_sql_service(sql);
    for (size_t di = 0; di < sizeof delays / sizeof *delays; di++) {
        for (size_t bi = 0; bi < sizeof per_txn / sizeof *per_txn; bi++) {
            char dir[512];
            snprintf(dir, sizeof dir, "%s/d%zu-%zu", argv[1], di, bi);
            char cmd[1200];
            snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", dir, dir);
            if (system(cmd) != 0) {
                return 2;
            }
            GsqlIo *io = gsql_io_real();
            real_ops = io->ops;
            slow_ops = *real_ops;
            slow_ops.sync = slow_sync;
            slow_ops.sync_dir = slow_sync_dir;
            io->ops = &slow_ops;
            delay_us = delays[di];
            GsOptions opt;
            memset(&opt, 0, sizeof opt);
            opt.create = 1;
            opt.io = io;
            GsStore *db;
            if (E->open(dir, &opt, &db)) {
                return 2;
            }
            GsTxn *t;
            uint64_t id;
            E->begin_write(db, &t);
            run(&svc, t, "CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)");
            E->commit(t, 1, &id);
            /* enough commits for the slowest case to take a few seconds */
            int commits = delays[di] >= 5000 ? 300 : delays[di] ? 1000 : 3000;
            if (per_txn[bi] >= 100) {
                commits /= 10;
            }
            syncs = 0;
            uint64_t row = 0;
            char q[64 * 1024];
            double t0 = now();
            for (int c = 0; c < commits; c++) {
                E->begin_write(db, &t);
                size_t n = (size_t)snprintf(q, sizeof q, "INSERT INTO t VALUES ");
                for (int r = 0; r < per_txn[bi]; r++, row++) {
                    n += (size_t)snprintf(q + n, sizeof q - n, "%s(%llu, 'row')", r ? "," : "", (unsigned long long)row);
                }
                run(&svc, t, q);
                if (E->commit(t, 1, &id)) {
                    return 2;
                }
            }
            double s = now() - t0;
            printf("{\"sync_delay_us\": %ld, \"rows_per_commit\": %d, \"commits\": %d, \"commits_per_s\": %.0f, "
                   "\"rows_per_s\": %.0f, \"syncs_per_commit\": %.2f}\n",
                   delays[di], per_txn[bi], commits, commits / s, row / s, (double)syncs / commits);
            fflush(stdout);
            E->close(db);
            gsql_io_free(io);
            snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
            if (system(cmd) != 0) {
                return 2;
            }
        }
    }
    gsql_sql_free(sql);
    return 0;
}

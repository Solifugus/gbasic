/* Parallel readers by fork: does a long read forked off the database pay for
 * itself? (docs/gsql_parallel_readers_design.md §6, increment 1.)
 *
 *   fork_readers DIR ROWS
 *
 * Built from the repository root against the library the gbasic build makes:
 *   make libgbasic.a
 *   cc -std=c11 -O2 -Iinclude -Isrc/gsql -D_POSIX_C_SOURCE=200809L \
 *      -o fork_readers research/gsql/fork_readers.c libgbasic.a $(make -s print-ldlibs)
 * Results, 2026-11-18: docs/gsql_parallel_readers_design.md §5.
 *
 * Opens a store in process, loads ROWS rows, then times one long read --
 * `SELECT b, count(*), sum(c), min(d) FROM t GROUP BY b`, every row read
 * and folded by the SQL layer, whose operators gBASIC evaluates -- run N
 * times for N = 1, 2, 4, 8:
 *   serial    N runs one after another in this process, which is what one
 *             owner thread does with N reports however they are sliced;
 *   forked    N children forked at once, each running it once on the state
 *             it inherited, which is what option B does.
 * Also each fork's own cost at this process's resident size, and that every
 * child's answer is the parent's (the fork is a snapshot: rows inserted by the
 * parent after the fork must not appear).
 *
 * A child only reads and leaves by _exit: closing the store would write the
 * manifest and release the lease, which are the owner's. The lease is
 * renewed from the owner's loop (owner.c lease_keep), never from read I/O.
 * Prints one line per N and a verdict. */
#define _POSIX_C_SOURCE 200809L
#include "gsql_sql.h"
#include "gsql_store.h"
#include "gsql_wire.h"
#include "v3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const GsEngine *E = &gs_engine_lsm;
static GsStore *db;
static GsqlService svc;

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static long rss_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    long kb = -1;
    while (f && fgets(line, sizeof line, f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) kb = atol(line + 6);
    }
    if (f) fclose(f);
    return kb;
}

/* Runs one statement to the end; answers a checksum of every byte of every
 * row, so two runs over the same snapshot answer the same number. */
static unsigned long long run(const char *text) {
    GsqlBuf stmt = {0};
    v3_w_text(&stmt, text);
    GsqlStatement st;
    memset(&st, 0, sizeof st);
    st.statement = stmt.bytes;
    st.statement_length = stmt.length;
    st.batch = 1000;
    GsqlError err;
    err.code = GSQL_OK;
    int w = svc.writes(svc.ctx, &st, &err);
    GsTxn *t;
    if ((w ? E->begin_write(db, &t) : E->begin_read(db, &t))) { fprintf(stderr, "begin failed\n"); exit(2); }
    GsqlResult res;
    memset(&res, 0, sizeof res);
    int rc = svc.run(svc.ctx, E, t, &st, &res, &err);
    if (rc != GSQL_OK) { fprintf(stderr, "refused: %s: %s\n", text, err.message); exit(2); }
    unsigned long long sum = 1469598103934665603ULL;
    GsqlBuf *rows = &res.rows;
    void *cur = res.cursor;
    for (;;) {
        for (size_t i = 0; i < rows->length; i++) sum = (sum ^ (unsigned char)rows->bytes[i]) * 1099511628211ULL;
        if (!cur) break;
        GsqlResult more;
        memset(&more, 0, sizeof more);
        if (svc.fetch(svc.ctx, cur, 1000, &more, &err) != GSQL_OK) { fprintf(stderr, "fetch failed\n"); exit(2); }
        if (rows != &res.rows) gsql_buf_free(rows);
        static GsqlBuf hold;
        hold = more.rows;
        rows = &hold;
        cur = more.cursor;
        gsql_buf_free(&more.columns);
        gsql_buf_free(&more.warnings);
    }
    if (rows != &res.rows) gsql_buf_free(rows);
    gsql_buf_free(&res.rows);
    gsql_buf_free(&res.columns);
    gsql_buf_free(&res.warnings);
    if (w) {
        uint64_t id;
        if (E->commit(t, 1, &id)) { fprintf(stderr, "commit failed\n"); exit(2); }
    } else {
        E->abort(t);
    }
    gsql_buf_free(&stmt);
    return sum;
}

static void load(long rows, long from) {
    char *buf = malloc(64 * 1024);
    for (long at = from; at < from + rows;) {
        size_t n = (size_t)snprintf(buf, 64 * 1024, "INSERT INTO t VALUES ");
        for (int k = 0; k < 500 && at < from + rows; k++, at++) {
            n += (size_t)snprintf(buf + n, 64 * 1024 - n, "%s(%ld, %ld, %ld, %ld)", k ? ", " : "",
                                  at, at % 1000, (at * 7919) % 100003, (at * 104729) % 65521);
        }
        run(buf);
    }
    free(buf);
}

static const char *LONG_READ = "SELECT b, count(*), sum(c), min(d) FROM t GROUP BY b";

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: fork_readers DIR ROWS\n"); return 2; }
    long rows = atol(argv[2]);
    GsOptions opt;
    memset(&opt, 0, sizeof opt);
    opt.create = 1;
    if (E->open(argv[1], &opt, &db)) { fprintf(stderr, "open failed\n"); return 2; }
    GsqlSql *sql = gsql_sql_new(gb_gsql_host());
    svc = gsql_sql_service(sql);
    run("CREATE TABLE t (a number PRIMARY KEY, b number, c number, d number)");
    double t0 = now();
    load(rows, 0);
    printf("loaded %ld rows in %.1f s; resident %ld MB\n", rows, now() - t0, rss_kb() / 1024);

    unsigned long long want = run(LONG_READ);   /* warm, and the answer */
    double one = now();
    run(LONG_READ);
    one = now() - one;
    printf("one long read alone: %.3f s\n", one);

    int bad = 0;
    double ratio8 = 0;
    for (int n = 1; n <= 8; n *= 2) {
        double s0 = now();
        for (int i = 0; i < n; i++) {
            if (run(LONG_READ) != want) bad++;
        }
        double serial = now() - s0;

        int fds[8][2];
        pid_t kids[8];
        double fork_worst = 0;
        double f0 = now();
        for (int i = 0; i < n; i++) {
            if (pipe(fds[i])) { perror("pipe"); return 2; }
            double a = now();
            kids[i] = fork();
            if (kids[i] == 0) {
                close(fds[i][0]);
                unsigned long long got = run(LONG_READ);
                if (write(fds[i][1], &got, sizeof got) != (ssize_t)sizeof got) _exit(3);
                _exit(0);
            }
            double took = now() - a;
            if (took > fork_worst) fork_worst = took;
            close(fds[i][1]);
        }
        /* the parent writes while its readers run: they must not see it */
        load(1000, rows + (long)n * 1000000L);
        for (int i = 0; i < n; i++) {
            unsigned long long got = 0;
            if (read(fds[i][0], &got, sizeof got) != (ssize_t)sizeof got || got != want) bad++;
            close(fds[i][0]);
            int status;
            waitpid(kids[i], &status, 0);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) bad++;
        }
        double forked = now() - f0;
        want = run(LONG_READ);   /* the parent's own answer moved with its insert */
        printf("N=%d  serial %.3f s  forked %.3f s  speedup %.2fx  worst fork %.2f ms\n",
               n, serial, forked, serial / forked, fork_worst * 1e3);
        if (n == 8) ratio8 = serial / forked;
    }
    printf("resident at the end %ld MB\n", rss_kb() / 1024);
    printf("%s: %d wrong answers; 8 forked readers %.2fx the serial throughput\n",
           bad ? "FAIL" : "ok", bad, ratio8);
    return bad ? 1 : 0;
}

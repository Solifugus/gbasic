/* G-SQL at scale, through the whole stack: SQL text, the planner and executor,
 * the table layer and the engine, in one process (no owner, no socket).
 *
 *   scale DIR ROWS
 *
 * One table t (id INTEGER PRIMARY KEY, k INTEGER, v TEXT, amt NUMBER), loaded
 * through SQL text in multi-row VALUES, 10 000 rows to a synced transaction,
 * then each operation timed. Prints one JSON line. DIR must be on a real disk
 * (research/gsql/scale.sh refuses tmpfs and caps the files). */
#define _POSIX_C_SOURCE 200809L
#include "gsql_sql.h"
#include "gsql_store.h"
#include "v3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const GsEngine *E = &gs_engine_lsm;
static GsStore *db;
static GsqlSql *sql;
static GsqlService svc;

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static long peak_rss_kb(void) {
    struct rusage u;
    getrusage(RUSAGE_SELF, &u);
    return u.ru_maxrss;
}

static void die(const char *what, const GsqlError *err) {
    fprintf(stderr, "scale: %s: %s\n", what, err ? err->message : "");
    exit(2);
}

static void open_db(const char *dir) {
    GsOptions opt;
    memset(&opt, 0, sizeof opt);
    opt.create = 1;
    int rc = E->open(dir, &opt, &db);
    if (rc) {
        fprintf(stderr, "scale: open %s: %s\n", dir, gs_status_name(rc));
        exit(2);
    }
}

/* runs one statement in `t`; answers the first value of the first row as a
 * number when there is one (count(*)), else the rows it touched */
static uint64_t exec_in(GsTxn *t, const char *text) {
    GsqlBuf stmt = {0};
    v3_w_text(&stmt, text);
    GsqlStatement st;
    memset(&st, 0, sizeof st);
    st.statement = stmt.bytes;
    st.statement_length = stmt.length;
    st.batch = 1000;
    GsqlResult res;
    memset(&res, 0, sizeof res);
    GsqlError err = { GSQL_OK, "", 0, 0 };
    if (svc.run(svc.ctx, E, t, &st, &res, &err) != GSQL_OK) {
        fprintf(stderr, "scale: %.200s\n", text);
        die("a statement failed", &err);
    }
    uint64_t answer = res.affected;
    if (res.has_rows) {
        GsBytes rows[1], cols[1];
        uint64_t v;
        if (v3_array_items(res.rows.bytes, res.rows.length, rows, 1) == 1 &&
            v3_array_items(rows[0].p, rows[0].n, cols, 1) >= 1 && v3_read_u64(cols[0].p, cols[0].n, &v)) {
            answer = v;
        }
        void *cur = res.cursor;
        while (cur) {
            GsqlResult more;
            memset(&more, 0, sizeof more);
            if (svc.fetch(svc.ctx, cur, 1000, &more, &err) != GSQL_OK) {
                die("a fetch failed", &err);
            }
            cur = more.cursor;
            gsql_buf_free(&more.rows);
            gsql_buf_free(&more.columns);
            gsql_buf_free(&more.warnings);
        }
    }
    gsql_buf_free(&res.rows);
    gsql_buf_free(&res.columns);
    gsql_buf_free(&res.warnings);
    gsql_buf_free(&stmt);
    return answer;
}

static uint64_t write_stmt(const char *text) {
    GsTxn *t;
    if (E->begin_write(db, &t)) {
        die("begin_write", NULL);
    }
    uint64_t r = exec_in(t, text), id;
    if (E->commit(t, 1, &id)) {
        die("commit", NULL);
    }
    return r;
}

static uint64_t read_stmt(const char *text) {
    GsTxn *t;
    if (E->begin_read(db, &t)) {
        die("begin_read", NULL);
    }
    uint64_t r = exec_in(t, text);
    E->abort(t);
    return r;
}

static uint64_t rng = 88172645463325252ull;
static uint64_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

/* rows [from, to) as one multi-row INSERT per 1000, 10 per synced transaction */
static void load(uint64_t from, uint64_t to) {
    size_t cap = 1000 * 80 + 64;
    char *q = malloc(cap);
    if (!q) {
        abort();
    }
    for (uint64_t base = from; base < to;) {
        GsTxn *t;
        if (E->begin_write(db, &t)) {
            die("begin_write", NULL);
        }
        for (int s = 0; s < 10 && base < to; s++) {
            size_t n = (size_t)snprintf(q, cap, "INSERT INTO t VALUES ");
            for (int r = 0; r < 1000 && base < to; r++, base++) {
                n += (size_t)snprintf(q + n, cap - n, "%s(%llu, %llu, 'row-%llu', %llu.5)", r ? "," : "",
                                      (unsigned long long)base,
                                      (unsigned long long)((base * 2654435761ull) % 1000003ull),
                                      (unsigned long long)base, (unsigned long long)(base % 997));
            }
            exec_in(t, q);
        }
        uint64_t id;
        if (E->commit(t, 1, &id)) {
            die("commit", NULL);
        }
    }
    free(q);
}

/* The engine's own counters (docs/gsql_profile_design.md), as the difference
 * between two snapshots, one JSON line on stderr: stdout stays the one result
 * line scale.sh records, comparable with runs before the counters existed. */
static void prof_line(const char *what, uint64_t rows, double secs, const GsStats *a, const GsStats *b) {
#define D(f) ((unsigned long long)(b->f - a->f))
    fprintf(stderr,
            "{\"profile\": \"%s\", \"rows\": %llu, \"secs\": %.2f, \"rows_per_s\": %.0f, "
            "\"bytes_written\": %llu, \"flush_bytes\": %llu, \"comp_bytes\": [%llu, %llu, %llu, %llu, %llu, %llu, %llu], "
            "\"comp_read_bytes\": %llu, \"comp_started\": %llu, \"comp_moves\": %llu, \"stalls\": %llu, "
            "\"flush_s\": %.2f, \"comp_s\": %.2f, \"sync_s\": %.2f, \"syncs\": %llu, "
            "\"lookups\": %llu, \"lookup_files\": %llu, \"bloom_rejects\": %llu, \"range_skips\": %llu, "
            "\"pages_read\": %llu, \"cache_hits\": %llu, \"file_bytes\": %llu}\n",
            what, (unsigned long long)rows, secs, secs > 0 ? rows / secs : 0.0, D(bytes_written), D(flush_bytes),
            D(comp_bytes[0]), D(comp_bytes[1]), D(comp_bytes[2]), D(comp_bytes[3]), D(comp_bytes[4]),
            D(comp_bytes[5]), D(comp_bytes[6]), D(comp_read_bytes), D(comp_started), D(comp_moves), D(stalls),
            D(flush_us) / 1e6, D(comp_us) / 1e6, D(sync_us) / 1e6, D(flushes), D(lookups), D(lookup_files),
            D(lookup_bloom_rejects), D(lookup_range_skips), D(pages_read), D(cache_hits),
            (unsigned long long)b->file_bytes);
#undef D
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* `ops` statements, each from `make`: median and p99 in microseconds */
static void timed(int ops, void (*make)(char *, size_t, uint64_t), uint64_t n, double *med, double *p99) {
    double *lat = malloc(sizeof(double) * (size_t)ops);
    char q[256];
    for (int i = 0; i < ops; i++) {
        make(q, sizeof q, n);
        double t0 = now();
        read_stmt(q);
        lat[i] = (now() - t0) * 1e6;
    }
    qsort(lat, (size_t)ops, sizeof(double), cmp_d);
    *med = lat[ops / 2];
    *p99 = lat[(size_t)ops * 99 / 100];
    free(lat);
}

/* The point reads again, each with its own counters, so the slowest 1% can
 * be compared with the rest: files consulted, filters, blocks read. */
typedef struct { double us; GsStats d; } PRead;
static int cmp_pr(const void *a, const void *b) {
    double x = ((const PRead *)a)->us, y = ((const PRead *)b)->us;
    return x < y ? -1 : x > y;
}
static void q_point(char *q, size_t cap, uint64_t n);
static void point_profile(const char *pass, int ops, uint64_t n) {
    PRead *r = calloc((size_t)ops, sizeof *r);
    char q[256];
    for (int i = 0; i < ops; i++) {
        GsStats a, b;
        q_point(q, sizeof q, n);
        E->stats(db, &a);
        double t0 = now();
        read_stmt(q);
        r[i].us = (now() - t0) * 1e6;
        E->stats(db, &b);
        r[i].d.lookups = b.lookups - a.lookups;
        r[i].d.lookup_files = b.lookup_files - a.lookup_files;
        r[i].d.lookup_bloom_rejects = b.lookup_bloom_rejects - a.lookup_bloom_rejects;
        r[i].d.lookup_range_skips = b.lookup_range_skips - a.lookup_range_skips;
        r[i].d.pages_read = b.pages_read - a.pages_read;
        r[i].d.cache_hits = b.cache_hits - a.cache_hits;
        r[i].d.fd_opens = b.fd_opens - a.fd_opens;
        r[i].d.index_loads = b.index_loads - a.index_loads;
    }
    qsort(r, (size_t)ops, sizeof *r, cmp_pr);
    int cut = ops * 99 / 100;
    for (int part = 0; part < 2; part++) {
        int lo = part ? cut : 0, hi = part ? ops : cut;
        double us = 0, f = 0, rej = 0, sk = 0, pg = 0, ch = 0, lk = 0, fo = 0, il = 0;
        for (int i = lo; i < hi; i++) {
            us += r[i].us; lk += r[i].d.lookups; f += r[i].d.lookup_files; rej += r[i].d.lookup_bloom_rejects;
            sk += r[i].d.lookup_range_skips; pg += r[i].d.pages_read; ch += r[i].d.cache_hits;
            fo += r[i].d.fd_opens; il += r[i].d.index_loads;
        }
        double c = hi - lo;
        fprintf(stderr,
                "{\"profile\": \"point reads %s, %s\", \"reads\": %d, \"mean_us\": %.1f, \"lookups\": %.2f, "
                "\"files\": %.2f, \"bloom_rejects\": %.2f, \"range_skips\": %.2f, \"pages_read\": %.2f, "
                "\"cache_hits\": %.2f, \"fd_opens\": %.3f, \"index_loads\": %.3f}\n",
                pass, part ? "slowest 1%" : "other 99%", hi - lo, us / c, lk / c, f / c, rej / c, sk / c, pg / c, ch / c, fo / c, il / c);
    }
    free(r);
}

static void q_point(char *q, size_t cap, uint64_t n) {
    snprintf(q, cap, "SELECT v FROM t WHERE id = %llu", (unsigned long long)(rnd() % n));
}
static void q_range(char *q, size_t cap, uint64_t n) {
    uint64_t a = rnd() % (n > 100 ? n - 100 : 1);
    snprintf(q, cap, "SELECT count(*) FROM t WHERE id BETWEEN %llu AND %llu", (unsigned long long)a,
             (unsigned long long)(a + 99));
}
static void q_k(char *q, size_t cap, uint64_t n) {
    (void)n;
    snprintf(q, cap, "SELECT count(*) FROM t WHERE k = %llu", (unsigned long long)(rnd() % 1000003ull));
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: scale DIR ROWS\n");
        return 2;
    }
    const char *dir = argv[1];
    uint64_t n = strtoull(argv[2], NULL, 10);
    sql = gsql_sql_new(gb_gsql_host());
    svc = gsql_sql_service(sql);
    open_db(dir);
    write_stmt("CREATE TABLE t (id INTEGER PRIMARY KEY, k INTEGER, v TEXT, amt NUMBER)");

    double t0 = now();
    {
        GsStats a, b;
        E->stats(db, &a);
        for (int s = 0; s < 10; s++) {
            uint64_t from = n * (uint64_t)s / 10, to = n * (uint64_t)(s + 1) / 10;
            double s0 = now();
            load(from, to);
            E->stats(db, &b);
            char what[32];
            snprintf(what, sizeof what, "load %d/10", s + 1);
            prof_line(what, to - from, now() - s0, &a, &b);
            a = b;
        }
    }
    double load_s = now() - t0;
    long rss_load = peak_rss_kb();

    t0 = now();
    E->flush(db);
    double flush_s = now() - t0;
    t0 = now();
    E->close(db);
    double close_s = now() - t0;
    t0 = now();
    open_db(dir);
    double reopen_s = now() - t0;

    int ops = 20000;
    double pmed, pp99, rmed, rp99, kmed, kp99;
    /* SCALE_COLD_PROFILE=1 profiles the first, cold pass after the reopen
     * as well, which changes what `timed` then measures, so it is off for
     * the recorded runs */
    if (getenv("SCALE_COLD_PROFILE")) point_profile("cold", ops, n);
    timed(ops, q_point, n, &pmed, &pp99);
    point_profile("warm", ops, n);
    timed(2000, q_range, n, &rmed, &rp99);

    t0 = now();
    uint64_t counted = read_stmt("SELECT count(*) FROM t");
    double count_s = now() - t0;
    t0 = now();
    uint64_t filtered = read_stmt("SELECT count(*) FROM t WHERE amt > 500");
    double filter_s = now() - t0;

    long rss_before_index = peak_rss_kb();
    t0 = now();
    double index_commit_s;
    {
        /* its statement and its commit timed apart: a build in one
           transaction spills runs, which the commit installs */
        GsTxn *t;
        if (E->begin_write(db, &t)) {
            die("begin_write", NULL);
        }
        exec_in(t, "CREATE INDEX t_k ON t (k)");
        double c0 = now();
        uint64_t cid;
        if (E->commit(t, 1, &cid)) {
            die("commit", NULL);
        }
        index_commit_s = now() - c0;
    }
    double index_s = now() - t0;
    long rss_index = peak_rss_kb();
    timed(ops, q_k, n, &kmed, &kp99);

    GsStats st;
    E->stats(db, &st);
    uint64_t bytes = st.file_bytes;
    E->close(db);

    /* a crash: a child appends 200 000 committed rows and ends without closing */
    uint64_t extra = n / 10 < 200000 ? n / 10 : 200000;
    pid_t pid = fork();
    if (pid == 0) {
        open_db(dir);
        load(n, n + extra);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    t0 = now();
    open_db(dir);
    double recover_s = now() - t0;
    E->stats(db, &st);
    uint64_t after = read_stmt("SELECT count(*) FROM t");
    E->close(db);

    printf("{\"rows\": %llu, \"load_s\": %.2f, \"load_rows_per_s\": %.0f, \"flush_s\": %.2f, \"close_s\": %.2f, "
           "\"reopen_s\": %.3f, \"point_us_median\": %.1f, \"point_us_p99\": %.1f, \"range100_us_median\": %.1f, "
           "\"range100_us_p99\": %.1f, \"count_s\": %.2f, \"count_rows_per_s\": %.0f, \"filter_scan_s\": %.2f, "
           "\"index_build_s\": %.2f, \"index_commit_s\": %.2f, \"index_lookup_us_median\": %.1f, \"index_lookup_us_p99\": %.1f, "
           "\"file_bytes\": %llu, \"bytes_per_row\": %.1f, \"rss_load_mb\": %.0f, \"rss_index_delta_mb\": %.0f, "
           "\"crash_rows\": %llu, \"recover_s\": %.3f, \"replayed\": %llu, \"rows_after_recovery\": %llu, "
           "\"counted_ok\": %s, \"filtered\": %llu}\n",
           (unsigned long long)n, load_s, n / load_s, flush_s, close_s, reopen_s, pmed, pp99, rmed, rp99, count_s,
           counted / (count_s > 0 ? count_s : 1e-9), filter_s, index_s, index_commit_s, kmed, kp99, (unsigned long long)bytes,
           (double)bytes / n, rss_load / 1024.0, (rss_index - rss_before_index) / 1024.0,
           (unsigned long long)extra, recover_s, (unsigned long long)st.replayed, (unsigned long long)after,
           counted == n && after == n + extra ? "true" : "false", (unsigned long long)filtered);
    gsql_sql_free(sql);
    return 0;
}

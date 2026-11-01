/* How long a short query waits behind a long one, through the real owner.
 *
 *   stall GBASIC DIR ROWS
 *
 * Client A starts an owner (GBASIC --gsql-owner), loads ROWS rows, then runs
 * three long queries one after another with a quiet second between them: an
 * aggregate over every row, a GROUP BY, and a sort no index gives, fetched to
 * the end. Client B, a forked process with its own connection, does point
 * reads the whole time and records each one's latency and when it ended. The
 * owner is single-threaded (docs/gsql_owner_design.md §6.1), so the question
 * is how far B's latency stretches while each long query runs. Prints one JSON
 * line: each long query's own time, and B's median and worst inside it and in
 * the quiet stretches. */
#define _POSIX_C_SOURCE 200809L
#include "gsql_client.h"
#include "v3.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void die(const char *what, const GsqlError *e) {
    fprintf(stderr, "stall: %s: %s\n", what, e ? e->message : "");
    exit(2);
}

/* a request and its reply; FETCHes a cursor to the end. Answers the frame type. */
static unsigned request(GsqlClient *c, const char *sql) {
    GsqlBuf b = {0};
    v3_w_record(&b, 2);
    v3_w_name(&b, "statement");
    v3_w_text(&b, sql);
    v3_w_name(&b, "batch");
    v3_w_int(&b, 1000);
    GsqlFrame f;
    GsqlError e;
    if (gsql_client_call(c, GSQL_M_QUERY, &b, &f, &e)) {
        die(sql, &e);
    }
    gsql_buf_free(&b);
    unsigned type = f.type;
    if (type == GSQL_M_ERROR) {
        fprintf(stderr, "stall: refused: %.200s\n", sql);
        exit(2);
    }
    /* a ROWS reply with more carries a cursor id */
    for (;;) {
        V3Field fl[8];
        long nf = v3_record_fields(f.body, f.length, fl, 8);
        const V3Field *cur = nf > 0 ? v3_field_named(fl, nf, "cursor") : NULL;
        uint64_t id;
        if (f.type != GSQL_M_ROWS || !cur || !v3_read_u64(cur->v, cur->vlen, &id)) {
            break;
        }
        GsqlBuf fb = {0};
        v3_w_record(&fb, 2);
        v3_w_name(&fb, "cursor");
        v3_w_int(&fb, id);
        v3_w_name(&fb, "count");
        v3_w_int(&fb, 1000);
        if (gsql_client_call(c, GSQL_M_FETCH, &fb, &f, &e)) {
            die("fetch", &e);
        }
        gsql_buf_free(&fb);
    }
    return type;
}

static void simple(GsqlClient *c, unsigned type, const char *mode) {
    GsqlBuf b = {0};
    v3_w_record(&b, mode ? 1 : 0);
    if (mode) {
        v3_w_name(&b, "mode");
        v3_w_text(&b, mode);
    }
    GsqlFrame f;
    GsqlError e;
    if (gsql_client_call(c, type, &b, &f, &e) || f.type == GSQL_M_ERROR) {
        die("begin/commit", &e);
    }
    gsql_buf_free(&b);
}

static GsqlClient *connect_to(const char *gbasic, const char *db, int create) {
    const char *argv[] = { gbasic, "--gsql-owner", NULL };
    GsqlClientOptions o;
    memset(&o, 0, sizeof o);
    o.owner_argv = argv;
    o.create = create;
    GsqlClient *c;
    GsqlError e;
    if (gsql_client_open(db, &o, &c, &e)) {
        die("connect", &e);
    }
    return c;
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: stall GBASIC DIR ROWS\n");
        return 2;
    }
    char db[1024];
    snprintf(db, sizeof db, "%s/stall.gsql", argv[2]);
    long rows = atol(argv[3]);
    GsqlClient *a = connect_to(argv[1], db, 1);
    request(a, "CREATE TABLE t (id INTEGER PRIMARY KEY, g INTEGER, v TEXT)");
    char *q = malloc(1000 * 64 + 64);
    for (long base = 0; base < rows;) {
        simple(a, GSQL_M_BEGIN, "write");
        for (int s = 0; s < 10 && base < rows; s++) {
            size_t n = (size_t)sprintf(q, "INSERT INTO t VALUES ");
            for (int r = 0; r < 1000 && base < rows; r++, base++) {
                n += (size_t)sprintf(q + n, "%s(%ld, %ld, 'v%ld')", r ? "," : "", base, (base * 7919) % 1000,
                                     (base * 104729) % rows);
            }
            request(a, q);
        }
        simple(a, GSQL_M_COMMIT, NULL);
    }
    free(q);

    /* B: point reads, (end time, latency) pairs written to a FILE until told to
       stop -- a pipe nobody reads until the end fills at 4 096 records and
       blocks the reader, which measured nothing during the long queries */
    char recpath[1100];
    snprintf(recpath, sizeof recpath, "%s/b.rec", argv[2]);
    double t0 = now();
    pid_t pid = fork();
    if (pid == 0) {
        FILE *out = fopen(recpath, "wb");
        if (!out) {
            _exit(2);
        }
        GsqlClient *b = connect_to(argv[1], db, 0);
        unsigned long long x = 1;
        char sql[96];
        for (;;) {
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            snprintf(sql, sizeof sql, "SELECT v FROM t WHERE id = %ld", (long)((x >> 33) % (unsigned long long)rows));
            double s = now();
            request(b, sql);
            double rec[2] = { now() - t0, now() - s };
            fwrite(rec, sizeof rec, 1, out);
            fflush(out);
        }
    }

    static const char *const longq[3] = {
        "SELECT count(*) FROM t WHERE v LIKE '%7%'",
        "SELECT g, count(*), max(v) FROM t GROUP BY g",
        "SELECT id, v FROM t ORDER BY v",
    };
    static const char *const names[3] = { "aggregate", "group_by", "sort" };
    double from[3], to[3];
    struct timespec quiet = { 1, 0 };
    nanosleep(&quiet, NULL);
    for (int k = 0; k < 3; k++) {
        from[k] = now() - t0;
        request(a, longq[k]);
        to[k] = now() - t0;
        nanosleep(&quiet, NULL);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);

    /* B's records: inside each long query, and outside all of them */
    size_t cap = 1 << 22, n = 0;
    double *rec = malloc(cap * 2 * sizeof(double));
    FILE *in = fopen(recpath, "rb");
    if (in) {
        n = fread(rec, 2 * sizeof(double), cap, in);
        fclose(in);
    }
    printf("{\"rows\": %ld", rows);
    for (int k = -1; k < 3; k++) {
        double *lat = malloc(n * sizeof(double) + 8);
        size_t m = 0;
        for (size_t i = 0; i < n; i++) {
            double end = rec[2 * i], l = rec[2 * i + 1], start = end - l;
            int in = 0;
            for (int j = 0; j < 3; j++) {
                in |= start < to[j] && end > from[j];
            }
            int want = k < 0 ? !in : (start < to[k] && end > from[k]);
            if (want) {
                lat[m++] = l * 1e6;
            }
        }
        qsort(lat, m, sizeof(double), cmp_d);
        const char *label = k < 0 ? "quiet" : names[k];
        if (k >= 0) {
            printf(", \"%s_s\": %.2f", label, to[k] - from[k]);
        }
        printf(", \"%s_reads\": %zu, \"%s_median_us\": %.0f, \"%s_worst_us\": %.0f", label, m, label,
               m ? lat[m / 2] : 0, label, m ? lat[m - 1] : 0);
        free(lat);
    }
    printf("}\n");
    gsql_client_close(a);
    return 0;
}

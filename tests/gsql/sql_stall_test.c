/* A long read must not stall everyone else (docs/gsql_slicing_design.md §4).
 *
 *   sql_stall_test GBASIC DIR ROWS
 *
 * Through the real owner. Client A loads ROWS rows, then runs a GROUP BY and a
 * sort no index gives, each fetched to the end. Client B, a forked process with
 * its own connection, does point reads the whole time and records each one's
 * latency to a file. Asserted, as RATIOS so a uniformly slower machine changes
 * nothing:
 *
 *   - B read during each long query at all (a B that never read would pass);
 *   - B's worst point read during each is a small fraction of the query's own
 *     time. Unsliced, the worst equals the query's whole blocking step (the
 *     design's §1: 903 ms during a 0.90 s GROUP BY), so the ratio is near 1.
 *
 * Then CANCEL of a working request: A sends a GROUP BY and cancels it before
 * it can finish; the query is answered CANCELLED, the CANCEL says it hit, and
 * the connection still answers. The research copy is research/gsql/stall.c. */
#define _POSIX_C_SOURCE 200809L
#include "gsql_client.h"
#include "v3.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int checks, mismatches;

static void check(int ok, const char *fmt, ...) {
    checks++;
    if (!ok) {
        mismatches++;
        va_list ap;
        va_start(ap, fmt);
        printf("MISMATCH ");
        vprintf(fmt, ap);
        printf("\n");
        va_end(ap);
    }
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void die(const char *what, const GsqlError *e) {
    printf("MISMATCH %s: %s\n", what, e ? e->message : "");
    printf("ok %d checks, %d mismatches\n", checks, mismatches + 1);
    exit(1);
}

static void query_body(GsqlBuf *b, const char *sql) {
    v3_w_record(b, 2);
    v3_w_name(b, "statement");
    v3_w_text(b, sql);
    v3_w_name(b, "batch");
    v3_w_int(b, 1000);
}

/* a request and its reply, FETCHing a cursor to the end; answers the frame type */
static unsigned request(GsqlClient *c, const char *sql) {
    GsqlBuf b = {0};
    query_body(&b, sql);
    GsqlFrame f;
    GsqlError e;
    if (gsql_client_call(c, GSQL_M_QUERY, &b, &f, &e)) {
        die(sql, &e);
    }
    gsql_buf_free(&b);
    unsigned type = f.type;
    if (type == GSQL_M_ERROR) {
        printf("MISMATCH refused: %.200s\n", sql);
        exit(1);
    }
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

static char *text_field(const GsqlFrame *f, const char *name) {
    V3Field fl[8];
    long nf = v3_record_fields(f->body, f->length, fl, 8);
    const V3Field *v = nf > 0 ? v3_field_named(fl, nf, name) : NULL;
    return v ? v3_read_text(v->v, v->vlen) : NULL;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: sql_stall_test GBASIC DIR ROWS\n");
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

    /* B: (end, latency) pairs to a FILE -- a pipe nobody reads until the end
       fills and blocks B, which then measures nothing */
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

    static const char *const longq[2] = {
        "SELECT g, count(*), max(v) FROM t GROUP BY g",
        "SELECT id, v FROM t ORDER BY v",
    };
    static const char *const names[2] = { "GROUP BY", "sort" };
    double from[2], to[2];
    struct timespec quiet = { 0, 300 * 1000 * 1000 };
    nanosleep(&quiet, NULL);
    for (int k = 0; k < 2; k++) {
        from[k] = now() - t0;
        request(a, longq[k]);
        to[k] = now() - t0;
        nanosleep(&quiet, NULL);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);

    size_t cap = 1 << 22, n = 0;
    double *rec = malloc(cap * 2 * sizeof(double));
    FILE *in = fopen(recpath, "rb");
    if (in) {
        n = fread(rec, 2 * sizeof(double), cap, in);
        fclose(in);
    }
    for (int k = 0; k < 2; k++) {
        size_t m = 0;
        double worst = 0;
        for (size_t i = 0; i < n; i++) {
            double end = rec[2 * i], l = rec[2 * i + 1], start = end - l;
            if (start < to[k] && end > from[k]) {
                m++;
                worst = l > worst ? l : worst;
            }
        }
        double took = to[k] - from[k];
        printf("%s: %.3f s, %zu point reads during it, worst %.1f ms (%.3f of the query)\n", names[k], took, m,
               worst * 1e3, took > 0 ? worst / took : 0);
        check(m >= 10, "B read %zu times during the %s: it was not measured", m, names[k]);
        check(worst < 0.25 * took, "a point read waited %.1f ms during a %.3f s %s: the long read stalled it",
              worst * 1e3, took, names[k]);
    }
    free(rec);

    /* CANCEL of a working request. The CANCEL must arrive while the read is
       WORKING: read in the same pass as the query, before it started, it finds
       nothing to cancel and the query is answered whole -- legitimate, and a
       race, so the query is given a moment to start and the case retried */
    int hits = 0, attempts = 0;
    for (; attempts < 5 && !hits; attempts++) {
        GsqlBuf qb = {0};
        query_body(&qb, longq[1]);
        uint32_t qid, cid;
        GsqlError e;
        if (gsql_client_send(a, GSQL_M_QUERY, &qb, &qid, &e)) {
            die("send", &e);
        }
        gsql_buf_free(&qb);
        struct timespec settle = { 0, 50 * 1000 * 1000 };
        nanosleep(&settle, NULL);
        GsqlBuf cb = {0};
        v3_w_record(&cb, 1);
        v3_w_name(&cb, "request");
        v3_w_int(&cb, qid);
        if (gsql_client_send(a, GSQL_M_CANCEL, &cb, &cid, &e)) {
            die("send cancel", &e);
        }
        gsql_buf_free(&cb);
        GsqlFrame f;
        if (gsql_client_recv(a, &f, &e)) {
            die("recv", &e);
        }
        char *code = f.type == GSQL_M_ERROR ? text_field(&f, "code") : NULL;
        int cancelled = f.id == qid && code && !strcmp(code, "CANCELLED");
        free(code);
        if (gsql_client_recv(a, &f, &e)) {
            die("recv: the CANCEL was never answered", &e);
        }
        int hit = 0;
        V3Field fl[8];
        long nf = v3_record_fields(f.body, f.length, fl, 8);
        const V3Field *h = nf > 0 ? v3_field_named(fl, nf, "cancelled") : NULL;
        int said = f.id == cid && f.type == GSQL_M_DONE && h && v3_read_bool(h->v, h->vlen, &hit);
        check(said, "the CANCEL was not answered DONE with `cancelled`");
        /* the two halves must agree: a hit is a CANCELLED query, a miss a whole one */
        check(hit == cancelled, "the CANCEL said hit=%d while the query was %s", hit,
              cancelled ? "cancelled" : "answered");
        hits += hit && cancelled;
    }
    check(hits == 1, "no CANCEL of a working sort hit in %d attempts", attempts);
    check(request(a, "SELECT count(*) FROM t") == GSQL_M_ROWS, "after the CANCEL the connection did not answer");

    pid_t owner = gsql_client_started(a);
    gsql_client_close(a);
    if (owner > 0) {
        kill(owner, SIGTERM);
        waitpid(owner, NULL, 0);
    }
    printf("ok %d checks, %d mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}

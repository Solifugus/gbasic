/* The SQL layer end to end (docs/gsql_sql_design.md §7, the owner tier): a
 * client starts `gbasic --gsql-owner` as its child and speaks the protocol to
 * it. Linked against the client side only, so nothing here is the owner: what
 * answers is the real binary, its parser, its planner and its host.
 *
 *   sql_owner_test GBASIC DIR */

#include "gsql_client.h"
#include "gsql_wire.h"
#include "serialize_tags.h"
#include "../../src/gsql/v3.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int checks, mismatches;

static void check(int ok, const char *fmt, ...) {
    checks++;
    if (ok) {
        return;
    }
    mismatches++;
    printf("MISMATCH ");
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static GsqlFrame last;
static char got[4096];   /* the reply, rendered: DONE n, ROWS ..., ERROR CODE@pos message */

static void render_value(GsqlBuf *o, const unsigned char *v, size_t n) {
    char b[64];
    if (v[0] == SER_NUMBER && v[1] == 1) {
        uint64_t x = 0;
        for (int k = 0; k < 8; k++) {
            x |= (uint64_t)v[2 + k] << (8 * k);
        }
        snprintf(b, sizeof b, "%lld", (long long)x);
        v3_put(o, b, strlen(b));
    } else if (v[0] == SER_STRING) {
        char *t = v3_read_text(v, n);
        v3_put(o, "'", 1);
        v3_put(o, t, strlen(t));
        v3_put(o, "'", 1);
        free(t);
    } else if (v[0] == SER_UNKNOWN) {
        v3_put(o, "NULL", 4);
    } else if (v[0] == SER_BOOL) {
        v3_put(o, v[1] ? "TRUE" : "FALSE", v[1] ? 4 : 5);
    } else {
        v3_put(o, "?", 1);
    }
}

static char *field_text(const GsqlFrame *f, const char *name) {
    V3Field fl[16];
    long n = v3_record_fields(f->body, f->length, fl, 16);
    const V3Field *x = n > 0 ? v3_field_named(fl, n, name) : NULL;
    return x ? v3_read_text(x->v, x->vlen) : NULL;
}

static uint64_t field_u64(const GsqlFrame *f, const char *name, int *present) {
    V3Field fl[16];
    long n = v3_record_fields(f->body, f->length, fl, 16);
    const V3Field *x = n > 0 ? v3_field_named(fl, n, name) : NULL;
    uint64_t v = 0;
    *present = x && v3_read_u64(x->v, x->vlen, &v);
    return v;
}

static uint64_t cursor;

static void render_reply(void) {
    GsqlBuf o = {0};
    int present;
    cursor = 0;
    if (last.type == GSQL_M_ERROR) {
        char *code = field_text(&last, "code");
        char *msg = field_text(&last, "message");
        uint64_t pos = field_u64(&last, "position", &present);
        char b[700];
        snprintf(b, sizeof b, "ERROR %s@%llu %s", code ? code : "?", present ? (unsigned long long)pos : 0ull,
                 msg ? msg : "");
        v3_put(&o, b, strlen(b));
        free(code);
        free(msg);
    } else if (last.type == GSQL_M_DONE) {
        uint64_t rows = field_u64(&last, "rows", &present);
        char b[64];
        snprintf(b, sizeof b, "DONE %llu", present ? (unsigned long long)rows : 0ull);
        v3_put(&o, b, strlen(b));
    } else if (last.type == GSQL_M_ROWS) {
        v3_put(&o, "ROWS", 4);
        V3Field fl[16];
        long n = v3_record_fields(last.body, last.length, fl, 16);
        const V3Field *rows = v3_field_named(fl, n, "rows");
        const V3Field *cur = v3_field_named(fl, n, "cursor");
        if (cur) {
            v3_read_u64(cur->v, cur->vlen, &cursor);
            v3_put(&o, " [more]", 7);
        }
        GsBytes items[256];
        long nr = rows ? v3_array_items(rows->v, rows->vlen, items, 256) : 0;
        for (long r = 0; r < nr; r++) {
            v3_put(&o, r ? " | " : " ", r ? 3 : 1);
            GsBytes cols[64];
            long nc = v3_array_items(items[r].p, items[r].n, cols, 64);
            for (long c = 0; c < nc; c++) {
                if (c) {
                    v3_put(&o, ", ", 2);
                }
                render_value(&o, cols[c].p, cols[c].n);
            }
        }
    } else if (last.type == GSQL_M_PREPARED) {
        /* PREPARED is { description: the service's record } */
        V3Field fl[4];
        long nf = v3_record_fields(last.body, last.length, fl, 4);
        const V3Field *d = nf > 0 ? v3_field_named(fl, nf, "description") : NULL;
        GsqlFrame inner = last;
        if (d) {
            inner.body = d->v;
            inner.length = d->vlen;
        }
        uint64_t id = field_u64(&inner, "id", &present);
        char b[64];
        snprintf(b, sizeof b, "PREPARED %llu", (unsigned long long)id);
        v3_put(&o, b, strlen(b));
    } else {
        v3_put(&o, gsql_msg_name((int)last.type), strlen(gsql_msg_name((int)last.type)));
    }
    v3_put(&o, "", 1);
    snprintf(got, sizeof got, "%s", (const char *)o.bytes);
    gsql_buf_free(&o);
}

static const char *call(GsqlClient *c, unsigned type, const GsqlBuf *body) {
    GsqlError err;
    if (gsql_client_call(c, type, body, &last, &err)) {
        snprintf(got, sizeof got, "client %s %s", gsql_error_name(err.code), err.message);
        return got;
    }
    render_reply();
    return got;
}

/* params: NULL, or a v3 value already written */
static const char *query(GsqlClient *c, const char *sql, const GsqlBuf *params, uint32_t batch) {
    GsqlBuf b = {0};
    v3_w_record(&b, 1 + (params != NULL) + (batch != 0));
    v3_w_name(&b, "statement");
    v3_w_text(&b, sql);
    if (params) {
        v3_w_name(&b, "parameters");
        v3_put(&b, params->bytes, params->length);
    }
    if (batch) {
        v3_w_name(&b, "batch");
        v3_w_int(&b, batch);
    }
    const char *w = call(c, GSQL_M_QUERY, &b);
    gsql_buf_free(&b);
    return w;
}

static const char *simple(GsqlClient *c, unsigned type, const char *mode) {
    GsqlBuf b = {0};
    if (mode) {
        v3_w_record(&b, 1);
        v3_w_name(&b, "mode");
        v3_w_text(&b, mode);
    } else {
        v3_w_record(&b, 0);
    }
    const char *w = call(c, type, &b);
    gsql_buf_free(&b);
    return w;
}

static const char *fetch(GsqlClient *c, uint64_t id, uint64_t count) {
    GsqlBuf b = {0};
    v3_w_record(&b, 2);
    v3_w_name(&b, "cursor");
    v3_w_int(&b, id);
    v3_w_name(&b, "count");
    v3_w_int(&b, count);
    const char *w = call(c, GSQL_M_FETCH, &b);
    gsql_buf_free(&b);
    return w;
}

#define EXPECT(c, sql, want) do { const char *w_ = query((c), (sql), NULL, 0); \
    check(strcmp(w_, (want)) == 0, "%s\n  got:  %s\n  want: %s", (sql), w_, (want)); } while (0)
#define EXPECT_PREFIX(c, sql, want) do { const char *w_ = query((c), (sql), NULL, 0); \
    check(strncmp(w_, (want), strlen(want)) == 0, "%s\n  got:  %s\n  want: %s...", (sql), w_, (want)); } while (0)

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: sql_owner_test GBASIC DIR\n");
        return 2;
    }
    char db[1024];
    snprintf(db, sizeof db, "%s/shop.gsql", argv[2]);
    const char *owner_argv[] = { argv[1], "--gsql-owner", NULL };
    GsqlClientOptions o;
    memset(&o, 0, sizeof o);
    o.owner_argv = owner_argv;
    o.create = 1;
    GsqlClient *c = NULL;
    GsqlError err;
    if (gsql_client_open(db, &o, &c, &err)) {
        printf("MISMATCH the client could not reach a gbasic owner: %s: %s\n", gsql_error_name(err.code),
               err.message);
        printf("ok 0 checks, 1 mismatches\n");
        return 1;
    }
    check(gsql_client_started(c) != 0, "the client did not start the owner itself");

    EXPECT(c, "CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT UNIQUE, n INTEGER DEFAULT 0)", "DONE 0");
    EXPECT(c, "INSERT INTO t (id, v) VALUES (1, 'a'), (2, 'b'), (3, 'c')", "DONE 3");

    /* batches: two rows, then a cursor for the third */
    const char *w = query(c, "SELECT id, v FROM t ORDER BY id", NULL, 2);
    check(strcmp(w, "ROWS [more] 1, 'a' | 2, 'b'") == 0, "a first batch of two: %s", w);
    uint64_t cur = cursor;
    w = fetch(c, cur, 10);
    check(strcmp(w, "ROWS 3, 'c'") == 0, "the rest through FETCH: %s", w);

    /* a parse error carries its position across the wire */
    EXPECT_PREFIX(c, "SELECT id FROM t WHERE", "ERROR SYNTAX@23 expected a value, found the end");
    EXPECT_PREFIX(c, "SELECT nope FROM t", "ERROR NOT_FOUND@8 table t has no column `nope`");

    /* parameters: positional and by name */
    GsqlBuf p = {0};
    v3_w_array(&p, 1);
    v3_w_int(&p, 2);
    w = query(c, "SELECT v FROM t WHERE id = $1", &p, 0);
    check(strcmp(w, "ROWS 'b'") == 0, "a $1 parameter: %s", w);
    p.length = 0;
    v3_w_record(&p, 1);
    v3_w_name(&p, "id");
    v3_w_int(&p, 3);
    w = query(c, "SELECT v FROM t WHERE id = :id", &p, 0);
    check(strcmp(w, "ROWS 'c'") == 0, "a :name parameter: %s", w);

    /* an autocommit statement that fails changes nothing */
    EXPECT_PREFIX(c, "UPDATE t SET v = 'z'", "ERROR DUPLICATE@0");
    EXPECT(c, "SELECT v FROM t ORDER BY id", "ROWS 'a' | 'b' | 'c'");

    /* in a transaction: a refusal before any write leaves it usable ... */
    w = simple(c, GSQL_M_BEGIN, "write");
    check(strcmp(w, "DONE 0") == 0, "BEGIN WRITE: %s", w);
    EXPECT(c, "UPDATE t SET n = 5 WHERE id = 1", "DONE 1");
    EXPECT_PREFIX(c, "SELECT nope FROM t", "ERROR NOT_FOUND");
    EXPECT(c, "SELECT n FROM t WHERE id = 1", "ROWS 5");
    w = simple(c, GSQL_M_COMMIT, NULL);
    check(strncmp(w, "DONE", 4) == 0, "COMMIT: %s", w);
    EXPECT(c, "SELECT n FROM t WHERE id = 1", "ROWS 5");

    /* ... and one that fails part way fails it (§4) */
    w = simple(c, GSQL_M_BEGIN, "write");
    check(strcmp(w, "DONE 0") == 0, "BEGIN WRITE: %s", w);
    EXPECT_PREFIX(c, "UPDATE t SET v = 'z'", "ERROR DUPLICATE");
    EXPECT_PREFIX(c, "SELECT 1", "ERROR FAILED@0 an earlier statement failed part way");
    w = simple(c, GSQL_M_ROLLBACK, NULL);
    check(strcmp(w, "DONE 0") == 0, "ROLLBACK acknowledges it: %s", w);
    EXPECT(c, "SELECT v FROM t ORDER BY id", "ROWS 'a' | 'b' | 'c'");

    /* a write in a BEGIN READ */
    simple(c, GSQL_M_BEGIN, "read");
    EXPECT_PREFIX(c, "DELETE FROM t", "ERROR READ_ONLY");
    simple(c, GSQL_M_ROLLBACK, NULL);

    /* a prepared statement */
    GsqlBuf b = {0};
    v3_w_record(&b, 1);
    v3_w_name(&b, "statement");
    v3_w_text(&b, "SELECT v FROM t WHERE id = :id");
    w = call(c, GSQL_M_PREPARE, &b);
    check(strncmp(w, "PREPARED ", 9) == 0, "PREPARE: %s", w);
    uint64_t pid = strtoull(w + 9, NULL, 10);
    b.length = 0;
    v3_w_record(&b, 2);
    v3_w_name(&b, "statement");
    v3_w_int(&b, pid);
    v3_w_name(&b, "parameters");
    v3_put(&b, p.bytes, p.length);
    w = call(c, GSQL_M_QUERY, &b);
    check(strcmp(w, "ROWS 'c'") == 0, "a QUERY by prepared id: %s", w);

    EXPECT(c, "EXPLAIN SELECT v FROM t WHERE v = 'b'", "ROWS 'Point Read on t using t_v_key (v = 'b')'");
    EXPECT_PREFIX(c, "SELECT 1 FROM", "ERROR SYNTAX@14");
    EXPECT(c, "SELECT CAST('1.25' AS MONEY(USD)) = CAST('1.25' AS MONEY(USD)), 2 + 2", "ROWS TRUE, 4");

    /* the data outlives the connection: a second client finds the same owner */
    pid_t owner = gsql_client_started(c);
    GsqlClient *c2 = NULL;
    o.create = 0;
    if (gsql_client_open(db, &o, &c2, &err) == 0) {
        check(gsql_client_owner_pid(c2) == owner, "a second client found another owner");
        EXPECT(c2, "SELECT count FROM t", "ERROR NOT_FOUND@8 table t has no column `count`");
        EXPECT(c2, "SELECT id FROM t WHERE n = 5", "ROWS 1");
        gsql_client_close(c2);
    } else {
        check(0, "a second client could not connect: %s", err.message);
    }
    gsql_client_close(c);

    /* the owner is this process's child: end it the planned way, and the
     * next owner opens the same database */
    if (owner > 0) {
        kill(owner, SIGTERM);
        int st;
        waitpid(owner, &st, 0);
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "the owner did not stop cleanly on SIGTERM");
    }
    if (gsql_client_open(db, &o, &c, &err) == 0) {
        EXPECT(c, "SELECT id, v, n FROM t ORDER BY v DESC", "ROWS 3, 'c', 0 | 2, 'b', 0 | 1, 'a', 5");
        pid_t again = gsql_client_started(c);
        gsql_client_close(c);
        if (again > 0) {
            kill(again, SIGTERM);
            int st;
            waitpid(again, &st, 0);
        }
    } else {
        check(0, "a new owner could not open the database: %s", err.message);
    }
    gsql_buf_free(&p);
    gsql_buf_free(&b);
    printf("ok %d checks, %d mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}

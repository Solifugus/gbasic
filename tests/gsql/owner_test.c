/* G-SQL's owner and protocol under test (docs/gsql_owner_design.md §8;
 * tests/run_gsql_owner.sh).
 *
 *   owner_test owner DB FD MODE   an owner with the TEST service below (what
 *                                 a client starts; GSQL_TEST_NO_SERVICE=1 for
 *                                 none, GSQL_TEST_*_MS for the timeouts)
 *   owner_test wire               frames and their reassembly
 *   owner_test protocol DIR       the handshake, transactions, cursors,
 *                                 pipelining, CANCEL, refusals
 *   owner_test noservice DIR      an owner with no SQL layer
 *   owner_test timeouts DIR       idle writers, snapshots and cursors
 *   owner_test stall DIR          a half-sent frame and an unread backlog
 *   owner_test race DIR N         N clients find no owner at once
 *   owner_test paths DIR          symlinks, stale sockets, runtime dirs
 *   owner_test handoff DIR        the owner killed, and SIGTERM's handoff
 *   owner_test lifetime DIR       the client killed: its owner goes too
 *   owner_test cost DIR ITERS     the round trip against in process
 *
 * THE TEST SERVICE IS NOT SQL. A statement is a word -- put, dup, get, scan,
 * count, whoami, fail, poison -- with text parameters, over one tree. It
 * exists so every path the OWNER owns (transactions, the writer, cursors,
 * batches, timeouts, a dropped connection) is reached; what it computes is
 * deliberately trivial and checked by the test, not by itself.
 *
 * Prints `ok N checks` or MISMATCH lines. */

#define _POSIX_C_SOURCE 200809L
#include "gsql_client.h"
#include "gsql_owner.h"
#include "gsql_store.h"
#include "gsql_wire.h"
#include "serialize_tags.h"
#include "v3.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const GsEngine *E = &gs_engine_lsm;
static long checks, mismatches;

static void mismatch(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    mismatches++;
    if (mismatches <= 40) {
        printf("MISMATCH ");
        vprintf(fmt, ap);
        printf("\n");
        fflush(stdout);
    }
    va_end(ap);
}

#define EXPECT(cond, ...)       \
    do {                        \
        checks++;               \
        if (!(cond)) {          \
            mismatch(__VA_ARGS__); \
        }                       \
    } while (0)

static int finish(void) {
    printf("ok %ld checks, %ld mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

static void sleep_ms(long ms) {
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {
    }
}

/* ==== the test service =================================================== */

static const unsigned char TREE[] = "t";
static GsBytes tree(void) { GsBytes b = { TREE, 1 }; return b; }

typedef struct {
    char word[16];
    long n;
    GsBytes p[8];
} Stmt;

static int parse(const GsqlStatement *s, Stmt *st, GsqlError *err) {
    char *w = v3_read_text(s->statement, s->statement_length);
    if (!w || strlen(w) >= sizeof st->word) {
        free(w);
        err->code = GSQL_SCHEMA;
        snprintf(err->message, sizeof err->message, "the test service takes a short word as its statement");
        return -1;
    }
    strcpy(st->word, w);
    free(w);
    st->n = 0;
    if (s->parameters_length) {
        st->n = v3_array_items(s->parameters, s->parameters_length, st->p, 8);
        if (st->n < 0) {
            err->code = GSQL_SCHEMA;
            snprintf(err->message, sizeof err->message, "the parameters are not an array");
            return -1;
        }
    }
    return 0;
}

static int svc_writes(void *ctx, const GsqlStatement *s, GsqlError *err) {
    (void)ctx;
    Stmt st;
    if (parse(s, &st, err)) {
        return -1;
    }
    if (!strcmp(st.word, "put") || !strcmp(st.word, "dup") || !strcmp(st.word, "poison") ||
        !strcmp(st.word, "crash_once")) {
        return 1;
    }
    if (!strcmp(st.word, "get") || !strcmp(st.word, "scan") || !strcmp(st.word, "count") ||
        !strcmp(st.word, "whoami") || !strcmp(st.word, "fail")) {
        return 0;
    }
    err->code = GSQL_SCHEMA;
    snprintf(err->message, sizeof err->message, "the test service has no statement %s", st.word);
    return -1;
}

/* A parameter's text, as the key bytes. */
static int key_of(const Stmt *st, long i, GsBytes *k) {
    if (i >= st->n || st->p[i].n < 2 || st->p[i].p[0] != SER_STRING) {
        return 0;
    }
    /* the payload after the tag and the one-byte length (short keys only) */
    if (st->p[i].p[1] >= 0x80 || (size_t)st->p[i].p[1] + 2 != st->p[i].n) {
        return 0;
    }
    k->p = st->p[i].p + 2;
    k->n = st->p[i].p[1];
    return 1;
}

static void w_text_n(GsqlBuf *b, const unsigned char *p, size_t n) {
    v3_u8(b, SER_STRING);
    v3_uleb(b, n);
    v3_put(b, p, n);
}

typedef struct {
    GsCursor *cur;
    int positioned;
} Scan;

static int scan_rows(Scan *sc, uint32_t count, GsqlResult *out, GsqlError *err) {
    GsqlBuf rows = {0};
    size_t n = 0;
    GsqlBuf items = {0};
    int rc = GS_OK;
    while (sc->positioned && n < count) {
        GsBytes k, v;
        if ((rc = E->key(sc->cur, &k)) || (rc = E->value(sc->cur, &v))) {
            break;
        }
        v3_w_array(&items, 2);
        w_text_n(&items, k.p, k.n);
        v3_put(&items, v.p, v.n);
        n++;
        if ((rc = E->next(sc->cur, &sc->positioned))) {
            break;
        }
    }
    if (rc) {
        gsql_buf_free(&items);
        err->code = GSQL_STORAGE;
        snprintf(err->message, sizeof err->message, "scan: the engine answered %s", gs_status_name(rc));
        return GSQL_STORAGE;
    }
    v3_w_array(&rows, n);
    v3_put(&rows, items.bytes, items.length);
    gsql_buf_free(&items);
    out->has_rows = 1;
    v3_set(&out->rows, rows.bytes, rows.length);
    gsql_buf_free(&rows);
    return GSQL_OK;
}

static void columns(GsqlResult *out) {
    out->columns.length = 0;
    v3_w_array(&out->columns, 2);
    v3_w_text(&out->columns, "key");
    v3_w_text(&out->columns, "value");
}

static int svc_run(void *ctx, const GsEngine *e, GsTxn *t, const GsqlStatement *s, GsqlResult *out, GsqlError *err) {
    (void)ctx;
    Stmt st;
    if (parse(s, &st, err)) {
        return err->code;
    }
    GsBytes k;
    if (!strcmp(st.word, "put") || !strcmp(st.word, "dup")) {
        if (!key_of(&st, 0, &k) || st.n < 2) {
            err->code = GSQL_SCHEMA;
            snprintf(err->message, sizeof err->message, "%s takes a key and a value", st.word);
            return GSQL_SCHEMA;
        }
        int rc = e->tree_create(t, tree());
        if (rc && rc != GS_EXISTS) {
            snprintf(err->message, sizeof err->message, "tree_create: %s", gs_status_name(rc));
            return err->code = GSQL_STORAGE;
        }
        rc = e->put(t, tree(), k, st.p[1], !strcmp(st.word, "dup") ? GS_NO_OVERWRITE : 0);
        if (rc == GS_EXISTS) {
            snprintf(err->message, sizeof err->message, "the key %.*s is already there", (int)k.n, (const char *)k.p);
            return err->code = GSQL_DUPLICATE;
        }
        if (rc) {
            snprintf(err->message, sizeof err->message, "put: %s", gs_status_name(rc));
            return err->code = GSQL_STORAGE;
        }
        out->has_affected = 1;
        out->affected = 1;
        return GSQL_OK;
    }
    if (!strcmp(st.word, "crash_once")) {
        /* the owner dies HALFWAY THROUGH a request, with a write made and not
         * committed -- the first time; once its marker exists it succeeds,
         * so a client that quietly repeated it would get an answer */
        char *mark = st.n >= 1 ? v3_read_text(st.p[0].p, st.p[0].n) : NULL;
        if (!mark) {
            snprintf(err->message, sizeof err->message, "crash_once takes a marker path");
            return err->code = GSQL_SCHEMA;
        }
        struct stat ms;
        int first = stat(mark, &ms) != 0;
        if (first) {
            FILE *f = fopen(mark, "w");
            if (f) {
                fclose(f);
            }
        }
        free(mark);
        e->tree_create(t, tree());
        GsBytes kb = { (const unsigned char *)"crashed", 7 }, vb = { (const unsigned char *)"\x05\x01x", 3 };
        e->put(t, tree(), kb, vb, 0);
        if (first) {
            _exit(9);
        }
        out->has_affected = 1;
        out->affected = 1;
        return GSQL_OK;
    }
    if (!strcmp(st.word, "poison")) {
        snprintf(err->message, sizeof err->message, "a failure the test service made on purpose");
        return err->code = GSQL_STORAGE;
    }
    if (!strcmp(st.word, "fail")) {
        snprintf(err->message, sizeof err->message, "refused on purpose");
        return err->code = GSQL_TYPE;
    }
    if (!strcmp(st.word, "whoami")) {
        out->has_rows = 1;
        v3_w_array(&out->rows, 1);
        v3_w_array(&out->rows, 1);
        if (s->principal_length) {
            v3_put(&out->rows, s->principal, s->principal_length);
        } else {
            v3_w_unknown(&out->rows);
        }
        return GSQL_OK;
    }
    if (!strcmp(st.word, "get")) {
        if (!key_of(&st, 0, &k)) {
            snprintf(err->message, sizeof err->message, "get takes a key");
            return err->code = GSQL_SCHEMA;
        }
        GsBytes v;
        int rc = e->get(t, tree(), k, &v);
        columns(out);
        out->has_rows = 1;
        if (rc == GS_NOT_FOUND) {
            v3_w_array(&out->rows, 0);
            return GSQL_OK;
        }
        if (rc) {
            snprintf(err->message, sizeof err->message, "get: %s", gs_status_name(rc));
            return err->code = GSQL_STORAGE;
        }
        v3_w_array(&out->rows, 1);
        v3_w_array(&out->rows, 2);
        w_text_n(&out->rows, k.p, k.n);
        v3_put(&out->rows, v.p, v.n);
        return GSQL_OK;
    }
    if (!strcmp(st.word, "count") || !strcmp(st.word, "scan")) {
        GsCursor *c;
        int rc = e->cursor_open(t, tree(), &c);
        if (rc == GS_NOT_FOUND) {
            out->has_rows = 1;
            columns(out);
            v3_w_array(&out->rows, 0);
            return GSQL_OK;
        }
        if (rc) {
            snprintf(err->message, sizeof err->message, "cursor_open: %s", gs_status_name(rc));
            return err->code = GSQL_STORAGE;
        }
        Scan *sc = calloc(1, sizeof *sc);
        sc->cur = c;
        e->first(c, &sc->positioned);
        if (!strcmp(st.word, "count")) {
            uint64_t n = 0;
            while (sc->positioned) {
                n++;
                e->next(c, &sc->positioned);
            }
            e->cursor_close(c);
            free(sc);
            out->has_rows = 1;
            v3_w_array(&out->rows, 1);
            v3_w_array(&out->rows, 1);
            v3_w_int(&out->rows, n);
            return GSQL_OK;
        }
        columns(out);
        rc = scan_rows(sc, s->batch, out, err);
        if (rc || !sc->positioned) {
            e->cursor_close(c);
            free(sc);
            return rc;
        }
        out->cursor = sc;
        return GSQL_OK;
    }
    snprintf(err->message, sizeof err->message, "no statement %s", st.word);
    return err->code = GSQL_SCHEMA;
}

static int svc_fetch(void *ctx, void *cursor, uint32_t count, GsqlResult *out, GsqlError *err) {
    (void)ctx;
    Scan *sc = cursor;
    int rc = scan_rows(sc, count, out, err);
    if (rc == GSQL_OK && !sc->positioned) {
        E->cursor_close(sc->cur);
        free(sc);
        out->cursor = NULL;
    }
    return rc;
}

static void svc_close(void *ctx, void *cursor) {
    (void)ctx;
    Scan *sc = cursor;
    E->cursor_close(sc->cur);
    free(sc);
}

static const GsqlService SERVICE = { NULL, svc_writes, svc_run, svc_fetch, svc_close, NULL };

static uint32_t env_ms(const char *name) {
    const char *v = getenv(name);
    return v ? (uint32_t)strtoul(v, NULL, 10) : 0;
}

static int owner_main(const char *db, const char *fd, const char *mode) {
    GsqlOwnerOptions o;
    memset(&o, 0, sizeof o);
    o.create = !strcmp(mode, "create");
    o.ready_fd = atoi(fd);
    o.snapshot_idle_ms = env_ms("GSQL_TEST_SNAPSHOT_MS");
    o.writer_idle_ms = env_ms("GSQL_TEST_WRITER_MS");
    o.output_limit = env_ms("GSQL_TEST_OUTPUT_LIMIT");
    const char *ns = getenv("GSQL_TEST_NO_SERVICE");
    return gsql_owner_run(db, ns && *ns == '1' ? NULL : &SERVICE, &o);
}

/* ==== the client side's helpers ========================================== */

static const char *const OWNER_ARGV[] = { "/proc/self/exe", "owner", NULL };

static GsqlClient *open_db(const char *db, int create) {
    GsqlClientOptions o;
    memset(&o, 0, sizeof o);
    o.owner_argv = OWNER_ARGV;
    o.create = create;
    GsqlClient *c = NULL;
    GsqlError err;
    if (gsql_client_open(db, &o, &c, &err)) {
        mismatch("open %s: %s: %s", db, gsql_error_name(err.code), err.message);
        return NULL;
    }
    return c;
}

static void b_text_array(GsqlBuf *b, int n, const char *const *t) {
    v3_w_array(b, (size_t)n);
    for (int i = 0; i < n; i++) {
        v3_w_text(b, t[i]);
    }
}

static void b_query(GsqlBuf *b, const char *word, int n, const char *const *params, uint32_t batch) {
    b->length = 0;
    v3_w_record(b, batch ? 3 : 2);
    v3_w_name(b, "statement");
    v3_w_text(b, word);
    v3_w_name(b, "parameters");
    b_text_array(b, n, params);
    if (batch) {
        v3_w_name(b, "batch");
        v3_w_int(b, batch);
    }
}

static void b_one(GsqlBuf *b, const char *name, uint64_t v) {
    b->length = 0;
    v3_w_record(b, 1);
    v3_w_name(b, name);
    v3_w_int(b, v);
}

static void b_mode(GsqlBuf *b, const char *mode) {
    b->length = 0;
    v3_w_record(b, 1);
    v3_w_name(b, "mode");
    v3_w_text(b, mode);
}

static void b_fetch(GsqlBuf *b, uint64_t cursor, uint64_t count) {
    b->length = 0;
    v3_w_record(b, 2);
    v3_w_name(b, "cursor");
    v3_w_int(b, cursor);
    v3_w_name(b, "count");
    v3_w_int(b, count);
}

/* A reply as a word: DONE, ROWS, PONG, ERROR:CODE. */
static const char *word_of(const GsqlFrame *f, char *buf, size_t cap) {
    if (f->type == GSQL_M_ERROR || f->type == GSQL_M_REFUSED) {
        V3Field fl[16];
        long n = v3_record_fields(f->body, f->length, fl, 16);
        const V3Field *x = n > 0 ? v3_field_named(fl, n, "code") : NULL;
        char *code = x ? v3_read_text(x->v, x->vlen) : NULL;
        snprintf(buf, cap, "%s:%s", gsql_msg_name((int)f->type), code ? code : "?");
        free(code);
        return buf;
    }
    snprintf(buf, cap, "%s", gsql_msg_name((int)f->type));
    return buf;
}

static char last_message[600];

static void remember_message(const GsqlFrame *f) {
    last_message[0] = 0;
    V3Field fl[16];
    long n = v3_record_fields(f->body, f->length, fl, 16);
    const V3Field *x = n > 0 ? v3_field_named(fl, n, "message") : NULL;
    char *m = x ? v3_read_text(x->v, x->vlen) : NULL;
    if (m) {
        snprintf(last_message, sizeof last_message, "%s", m);
        free(m);
    }
}

static GsqlFrame last;
static char last_word[64];

/* One request and its reply, as a word; LOST and the like for a client error. */
static const char *call(GsqlClient *c, unsigned type, const GsqlBuf *body) {
    GsqlError err;
    if (gsql_client_call(c, type, body, &last, &err)) {
        snprintf(last_word, sizeof last_word, "client:%s", gsql_error_name(err.code));
        snprintf(last_message, sizeof last_message, "%s", err.message);
        return last_word;
    }
    remember_message(&last);
    return word_of(&last, last_word, sizeof last_word);
}

static const char *q(GsqlClient *c, const char *word, int n, const char *const *params) {
    GsqlBuf b = {0};
    b_query(&b, word, n, params, 0);
    const char *w = call(c, GSQL_M_QUERY, &b);
    gsql_buf_free(&b);
    return w;
}

static const char *begin(GsqlClient *c, const char *mode) {
    GsqlBuf b = {0};
    b_mode(&b, mode);
    const char *w = call(c, GSQL_M_BEGIN, &b);
    gsql_buf_free(&b);
    return w;
}

static int field_u64(const GsqlFrame *f, const char *name, uint64_t *v) {
    V3Field fl[16];
    long n = v3_record_fields(f->body, f->length, fl, 16);
    const V3Field *x = n > 0 ? v3_field_named(fl, n, name) : NULL;
    return x && v3_read_u64(x->v, x->vlen, v);
}

static int field_bool(const GsqlFrame *f, const char *name, int *v) {
    V3Field fl[16];
    long n = v3_record_fields(f->body, f->length, fl, 16);
    const V3Field *x = n > 0 ? v3_field_named(fl, n, name) : NULL;
    return x && v3_read_bool(x->v, x->vlen, v);
}

/* The rows of a ROWS reply, borrowed. Answers the count, or -1. */
static long rows_of(const GsqlFrame *f, GsBytes *rows, long max) {
    V3Field fl[16];
    long n = v3_record_fields(f->body, f->length, fl, 16);
    const V3Field *x = n > 0 ? v3_field_named(fl, n, "rows") : NULL;
    return x ? v3_array_items(x->v, x->vlen, rows, max) : -1;
}

/* Whether `get key` saw a row. */
static int has(GsqlClient *c, const char *key) {
    const char *p[] = { key };
    const char *w = q(c, "get", 1, p);
    if (strcmp(w, "ROWS")) {
        mismatch("get %s answered %s: %s", key, w, last_message);
        return -1;
    }
    GsBytes r[2];
    return rows_of(&last, r, 2) == 1;
}

static long count_rows(GsqlClient *c) {
    const char *w = q(c, "count", 0, NULL);
    GsBytes r[1], cells[1];
    uint64_t n;
    if (strcmp(w, "ROWS") || rows_of(&last, r, 1) != 1 || v3_array_items(r[0].p, r[0].n, cells, 1) != 1 ||
        !v3_read_u64(cells[0].p, cells[0].n, &n)) {
        mismatch("count answered %s", w);
        return -1;
    }
    return (long)n;
}

static const char *put(GsqlClient *c, const char *k, const char *v) {
    const char *p[] = { k, v };
    return q(c, "put", 2, p);
}

/* ==== raw connections, for what a well-behaved client never sends ======== */

static int raw_connect(const char *db) {
    char path[200];
    uint64_t d, i;
    GsqlError err;
    if (gsql_socket_path(db, path, sizeof path, &d, &i, &err)) {
        mismatch("socket path: %s", err.message);
        return -1;
    }
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    memcpy(a.sun_path, path, strlen(path) + 1);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connect(s, (struct sockaddr *)&a, sizeof a)) {
        mismatch("raw connect %s: %s", path, strerror(errno));
        close(s);
        return -1;
    }
    return s;
}

static void raw_bytes(int fd, const void *p, size_t n) {
    const unsigned char *b = p;
    while (n) {
        ssize_t w = send(fd, b, n, MSG_NOSIGNAL);
        if (w <= 0) {
            return;
        }
        b += w;
        n -= (size_t)w;
    }
}

static void raw_send(int fd, unsigned type, uint32_t id, const GsqlBuf *body) {
    GsqlBuf f = {0};
    gsql_frame_put(&f, type, id, body ? body->bytes : NULL, body ? body->length : 0);
    raw_bytes(fd, f.bytes, f.length);
    gsql_buf_free(&f);
}

/* 1 a frame, 0 nothing within `ms`, -1 the connection ended. */
static int raw_recv(int fd, GsqlReader *rd, GsqlFrame *f, int ms) {
    for (;;) {
        int st = gsql_reader_next(rd, f);
        if (st == GSQL_READ_FRAME) {
            return 1;
        }
        struct pollfd p = { fd, POLLIN, 0 };
        int r = poll(&p, 1, ms);
        if (r == 0) {
            return 0;
        }
        unsigned char buf[65536];
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) {
            return -1;
        }
        gsql_reader_feed(rd, buf, (size_t)n);
    }
}

static void raw_hello(int fd, GsqlReader *rd, uint64_t major, uint64_t minor, GsqlFrame *f) {
    GsqlBuf b = {0};
    v3_w_record(&b, 2);
    v3_w_name(&b, "major");
    v3_w_int(&b, major);
    v3_w_name(&b, "minor");
    v3_w_int(&b, minor);
    raw_send(fd, GSQL_M_HELLO, 7, &b);
    gsql_buf_free(&b);
    if (raw_recv(fd, rd, f, 5000) != 1) {
        mismatch("no answer to a raw HELLO %llu.%llu", (unsigned long long)major, (unsigned long long)minor);
        f->type = 0;
    }
}

/* ==== wire =============================================================== */

static int wire_main(void) {
    /* every code's name reads back as that code */
    for (int c = GSQL_OK; c <= GSQL_CODE_LAST; c++) {
        if (c > GSQL_MISUSE && c < GSQL_PROTOCOL) {
            continue;
        }
        EXPECT(gsql_error_code(gsql_error_name(c)) == c, "code %d's name %s reads back as %d", c, gsql_error_name(c),
               gsql_error_code(gsql_error_name(c)));
    }
    EXPECT(gsql_error_code("NO_SUCH_CODE") == -1, "an unknown name reads back as a code");

    /* a stream of frames, including an empty body, a large one and odd sizes */
    enum { NF = 40 };
    size_t lens[NF];
    GsqlBuf stream = {0};
    unsigned seed = 12345;
    for (int i = 0; i < NF; i++) {
        seed = seed * 1103515245u + 12345u;
        lens[i] = i == 0 ? 0 : i == 1 ? (1u << 20) + 3 : (seed >> 8) % 3000;
        unsigned char *body = malloc(lens[i] + 1);
        for (size_t j = 0; j < lens[i]; j++) {
            body[j] = (unsigned char)(i * 7 + (int)j);
        }
        gsql_frame_put(&stream, (unsigned)(1 + i % GSQL_M_LAST), (uint32_t)(1000 + i), body, lens[i]);
        free(body);
    }
    /* fed whole, byte by byte, and in uneven pieces: the same frames each time */
    for (int way = 0; way < 3; way++) {
        GsqlReader r = {0};
        size_t at = 0;
        int got = 0, bad = 0;
        seed = 99;
        while (at < stream.length || got < NF) {
            size_t piece = way == 0 ? stream.length : way == 1 ? 1 : 1 + (seed = seed * 1103515245u + 12345u) % 7001;
            if (at < stream.length) {
                if (piece > stream.length - at) {
                    piece = stream.length - at;
                }
                gsql_reader_feed(&r, stream.bytes + at, piece);
                at += piece;
            }
            GsqlFrame f;
            int st;
            while ((st = gsql_reader_next(&r, &f)) == GSQL_READ_FRAME) {
                int i = got++;
                if (i >= NF || f.id != (uint32_t)(1000 + i) || f.type != (unsigned)(1 + i % GSQL_M_LAST) ||
                    f.length != lens[i]) {
                    bad++;
                    continue;
                }
                for (size_t j = 0; j < f.length; j++) {
                    if (f.body[j] != (unsigned char)(i * 7 + (int)j)) {
                        bad++;
                        break;
                    }
                }
            }
            if (st != GSQL_READ_MORE) {
                bad++;
                break;
            }
            if (at >= stream.length && got < NF) {
                break;
            }
        }
        EXPECT(got == NF && bad == 0 && gsql_reader_pending(&r) == 0,
               "fed %s: %d frames of %d, %d wrong, %zu bytes left", way == 0 ? "whole" : way == 1 ? "a byte at a time" : "in pieces",
               got, NF, bad, gsql_reader_pending(&r));
        gsql_reader_free(&r);
    }
    /* every strict prefix of one frame is MORE, and the whole is the frame */
    GsqlBuf one = {0};
    gsql_frame_put(&one, GSQL_M_PING, 5, "abc", 3);
    for (size_t cut = 0; cut < one.length; cut++) {
        GsqlReader r = {0};
        GsqlFrame f;
        gsql_reader_feed(&r, one.bytes, cut);
        EXPECT(gsql_reader_next(&r, &f) == GSQL_READ_MORE, "a prefix of %zu bytes of %zu is not MORE", cut, one.length);
        gsql_reader_feed(&r, one.bytes + cut, one.length - cut);
        EXPECT(gsql_reader_next(&r, &f) == GSQL_READ_FRAME && f.id == 5 && f.length == 3 && !memcmp(f.body, "abc", 3),
               "split at %zu, the frame does not come back whole", cut);
        gsql_reader_free(&r);
    }
    gsql_buf_free(&one);
    /* the length is judged as soon as it is there */
    static const struct { uint32_t len; int want; const char *what; } L[] = {
        { 0, GSQL_READ_SHORT, "0" }, { 4, GSQL_READ_SHORT, "4" }, { 5, GSQL_READ_MORE, "5 (no body), header only" },
        { GSQL_FRAME_MAX, GSQL_READ_MORE, "the limit" }, { GSQL_FRAME_MAX + 1, GSQL_READ_TOO_LARGE, "one past the limit" },
        { 0xffffffffu, GSQL_READ_TOO_LARGE, "4 GiB" },
    };
    for (size_t i = 0; i < sizeof L / sizeof L[0]; i++) {
        unsigned char h[4] = { (unsigned char)L[i].len, (unsigned char)(L[i].len >> 8), (unsigned char)(L[i].len >> 16),
                               (unsigned char)(L[i].len >> 24) };
        GsqlReader r = {0};
        GsqlFrame f;
        gsql_reader_feed(&r, h, 4);
        int st = gsql_reader_next(&r, &f);
        EXPECT(st == L[i].want, "a length of %s answered %d, not %d", L[i].what, st, L[i].want);
        gsql_reader_free(&r);
    }
    /* a frame exactly at the limit is accepted */
    {
        GsqlBuf big = {0};
        unsigned char *body = calloc(GSQL_FRAME_MAX - 5, 1);
        gsql_frame_put(&big, GSQL_M_QUERY, 9, body, GSQL_FRAME_MAX - 5);
        free(body);
        GsqlReader r = {0};
        GsqlFrame f;
        gsql_reader_feed(&r, big.bytes, big.length);
        EXPECT(gsql_reader_next(&r, &f) == GSQL_READ_FRAME && f.length == GSQL_FRAME_MAX - 5,
               "a frame exactly at the limit was not accepted");
        gsql_reader_free(&r);
        gsql_buf_free(&big);
    }
    gsql_buf_free(&stream);
    return finish();
}

/* ==== protocol =========================================================== */

static int protocol_main(const char *dir) {
    char db[600];
    snprintf(db, sizeof db, "%s/proto", dir);
    GsqlClient *a = open_db(db, 1);
    GsqlClient *b = open_db(db, 0);
    if (!a || !b) {
        return finish();
    }
    EXPECT(gsql_client_started(a) > 0, "the first client did not start an owner");
    EXPECT(gsql_client_started(b) == 0, "the second client started an owner of its own (pid %ld)",
           (long)gsql_client_started(b));
    EXPECT(gsql_client_owner_pid(a) == gsql_client_owner_pid(b) && gsql_client_owner_pid(a) == gsql_client_started(a),
           "the two clients reached different owners: %ld and %ld", (long)gsql_client_owner_pid(a),
           (long)gsql_client_owner_pid(b));
    const char *w;

    /* ping, and the request id echoed */
    uint32_t id;
    GsqlError err;
    gsql_client_send(a, GSQL_M_PING, NULL, &id, &err);
    gsql_client_recv(a, &last, &err);
    EXPECT(last.type == GSQL_M_PONG && last.id == id, "PING %u answered %s %u", id, gsql_msg_name((int)last.type), last.id);

    /* autocommit */
    w = put(a, "k1", "one");
    uint64_t commit1 = 0;
    EXPECT(!strcmp(w, "DONE") && field_u64(&last, "commit", &commit1) && commit1 > 0,
           "an autocommit put answered %s without a commit id", w);
    uint64_t affected = 0;
    EXPECT(field_u64(&last, "rows", &affected) && affected == 1, "an autocommit put reported %llu rows",
           (unsigned long long)affected);
    EXPECT(has(b, "k1") == 1, "another client does not see a committed autocommit write");

    /* an open write is invisible until it commits */
    EXPECT(!strcmp(begin(a, "write"), "DONE"), "BEGIN WRITE answered %s", last_word);
    EXPECT(!strcmp(put(a, "k2", "two"), "DONE"), "a put in a transaction answered %s", last_word);
    uint64_t c;
    EXPECT(!field_u64(&last, "commit", &c), "a put inside a transaction reported a commit id");
    EXPECT(has(a, "k2") == 1, "a transaction does not see its own write");
    EXPECT(has(b, "k2") == 0, "an uncommitted write is visible to another connection");
    w = call(a, GSQL_M_COMMIT, NULL);
    uint64_t commit2 = 0;
    EXPECT(!strcmp(w, "DONE") && field_u64(&last, "commit", &commit2) && commit2 > commit1,
           "COMMIT answered %s, commit %llu after %llu", w, (unsigned long long)commit2, (unsigned long long)commit1);
    EXPECT(has(b, "k2") == 1, "a committed transaction is not visible");

    /* a snapshot holds while others commit */
    EXPECT(!strcmp(begin(b, "read"), "DONE"), "BEGIN READ answered %s", last_word);
    EXPECT(!strcmp(put(a, "k3", "three"), "DONE"), "an autocommit put beside a reader answered %s", last_word);
    EXPECT(has(b, "k3") == 0, "a BEGIN READ snapshot saw a later commit");
    EXPECT(!strcmp(put(b, "k4", "x"), "ERROR:READ_ONLY"), "a write in BEGIN READ answered %s", last_word);
    EXPECT(has(b, "k1") == 1, "a refused write ended the read transaction");
    EXPECT(!strcmp(call(b, GSQL_M_COMMIT, NULL), "DONE"), "COMMIT of a read answered %s", last_word);
    EXPECT(has(b, "k3") == 1, "after its snapshot ended a reader does not see a commit");

    /* state errors, beside the requests that are right */
    EXPECT(!strcmp(call(a, GSQL_M_COMMIT, NULL), "ERROR:STATE"), "COMMIT with nothing begun answered %s", last_word);
    EXPECT(!strcmp(call(a, GSQL_M_ROLLBACK, NULL), "ERROR:STATE"), "ROLLBACK with nothing begun answered %s", last_word);
    EXPECT(!strcmp(begin(a, "read"), "DONE"), "BEGIN READ answered %s", last_word);
    EXPECT(!strcmp(begin(a, "read"), "ERROR:STATE"), "BEGIN inside a transaction answered %s", last_word);
    EXPECT(!strcmp(call(a, GSQL_M_ROLLBACK, NULL), "DONE"), "ROLLBACK of a read answered %s", last_word);
    EXPECT(!strcmp(begin(a, "sideways"), "ERROR:PROTOCOL") && strstr(last_message, "sideways"),
           "BEGIN with a mode that is not one answered %s: %s", last_word, last_message);

    /* a refused statement leaves the transaction usable */
    EXPECT(!strcmp(begin(a, "write"), "DONE"), "BEGIN WRITE answered %s", last_word);
    const char *dup[] = { "k1", "again" };
    EXPECT(!strcmp(q(a, "dup", 2, dup), "ERROR:DUPLICATE") && strstr(last_message, "k1"),
           "a duplicate in a transaction answered %s: %s", last_word, last_message);
    EXPECT(!strcmp(put(a, "k5", "five"), "DONE"), "after a refusal the transaction refused a put: %s", last_word);
    EXPECT(!strcmp(call(a, GSQL_M_COMMIT, NULL), "DONE"), "COMMIT after a refusal answered %s", last_word);
    EXPECT(has(b, "k5") == 1, "the write after a refusal was lost");

    /* an engine failure ends it: FAILED until it is ended, and nothing kept */
    EXPECT(!strcmp(begin(a, "write"), "DONE"), "BEGIN WRITE answered %s", last_word);
    EXPECT(!strcmp(put(a, "k6", "six"), "DONE"), "put answered %s", last_word);
    EXPECT(!strcmp(q(a, "poison", 0, NULL), "ERROR:STORAGE"), "poison answered %s", last_word);
    const char *k1[] = { "k1" };
    EXPECT(!strcmp(q(a, "get", 1, k1), "ERROR:FAILED") && strstr(last_message, "ROLLBACK"),
           "a statement after an engine failure answered %s: %s", last_word, last_message);
    EXPECT(!strcmp(call(a, GSQL_M_COMMIT, NULL), "ERROR:FAILED"), "COMMIT of a failed transaction answered %s", last_word);
    EXPECT(has(b, "k6") == 0, "a failed transaction's write survived");
    EXPECT(!strcmp(begin(a, "write"), "DONE"), "after a failed transaction's COMMIT, BEGIN answered %s", last_word);
    EXPECT(!strcmp(call(a, GSQL_M_ROLLBACK, NULL), "DONE"), "ROLLBACK answered %s", last_word);
    /* and its control: ROLLBACK also ends a failed one */
    begin(a, "write");
    q(a, "poison", 0, NULL);
    EXPECT(!strcmp(call(a, GSQL_M_ROLLBACK, NULL), "DONE"), "ROLLBACK of a failed transaction answered %s", last_word);

    /* autocommit refusals change nothing */
    long before = count_rows(b);
    EXPECT(!strcmp(q(a, "dup", 2, dup), "ERROR:DUPLICATE"), "an autocommit duplicate answered %s", last_word);
    EXPECT(!strcmp(q(a, "poison", 0, NULL), "ERROR:STORAGE"), "an autocommit poison answered %s", last_word);
    EXPECT(!strcmp(q(a, "fail", 0, NULL), "ERROR:TYPE"), "an autocommit refused read answered %s", last_word);
    EXPECT(count_rows(b) == before, "a refused autocommit statement changed the row count");
    EXPECT(!strcmp(begin(a, "write"), "DONE") && !strcmp(call(a, GSQL_M_ROLLBACK, NULL), "DONE"),
           "after autocommit refusals the writer slot was not free");

    /* the writer queue: B's BEGIN WRITE waits for A's, and B's requests behind it wait too */
    EXPECT(!strcmp(begin(a, "write"), "DONE"), "BEGIN WRITE answered %s", last_word);
    GsqlBuf body = {0};
    b_mode(&body, "write");
    uint32_t bid, pid2;
    gsql_client_send(b, GSQL_M_BEGIN, &body, &bid, &err);
    gsql_client_send(b, GSQL_M_PING, NULL, &pid2, &err);
    EXPECT(!strcmp(call(a, GSQL_M_PING, NULL), "PONG"), "the writer was not served while another waited");
    struct pollfd pf = { gsql_client_fd(b), POLLIN, 0 };
    EXPECT(poll(&pf, 1, 150) == 0, "a waiting BEGIN WRITE was answered while another connection held the writer");
    EXPECT(!strcmp(put(a, "k7", "seven"), "DONE"), "put answered %s", last_word);
    EXPECT(!strcmp(call(a, GSQL_M_COMMIT, NULL), "DONE"), "COMMIT answered %s", last_word);
    gsql_client_recv(b, &last, &err);
    EXPECT(last.type == GSQL_M_DONE && last.id == bid, "the waiting BEGIN was answered %s %u, not DONE %u",
           gsql_msg_name((int)last.type), last.id, bid);
    gsql_client_recv(b, &last, &err);
    EXPECT(last.type == GSQL_M_PONG && last.id == pid2, "the PING behind it was answered %s %u",
           gsql_msg_name((int)last.type), last.id);
    EXPECT(has(b, "k7") == 1, "a write transaction begun after another's commit does not see it");
    /* and a reader is never made to wait */
    EXPECT(has(a, "k1") == 1, "a reader waited behind the writer");
    EXPECT(!strcmp(call(b, GSQL_M_ROLLBACK, NULL), "DONE"), "ROLLBACK answered %s", last_word);

    /* the writer queue is first come, first served: C, connected after B,
     * asks first and must be served first -- the owner visits connections in
     * the order they arrived, so without the queue B would win */
    {
        GsqlClient *c3 = open_db(db, 0);
        uint32_t c3id, b2id;
        EXPECT(!strcmp(begin(a, "write"), "DONE"), "BEGIN WRITE answered %s", last_word);
        gsql_client_send(c3, GSQL_M_BEGIN, &body, &c3id, &err);
        sleep_ms(50);
        gsql_client_send(b, GSQL_M_BEGIN, &body, &b2id, &err);
        sleep_ms(50);
        EXPECT(!strcmp(call(a, GSQL_M_COMMIT, NULL), "DONE"), "COMMIT answered %s", last_word);
        struct pollfd p3 = { gsql_client_fd(c3), POLLIN, 0 };
        EXPECT(poll(&p3, 1, 2000) == 1, "the first in the writer queue was not served when the writer committed");
        struct pollfd pb = { gsql_client_fd(b), POLLIN, 0 };
        EXPECT(poll(&pb, 1, 150) == 0, "the second in the writer queue was served before the first");
        gsql_client_recv(c3, &last, &err);
        EXPECT(last.id == c3id && last.type == GSQL_M_DONE, "the first waiter was answered %s", gsql_msg_name((int)last.type));
        EXPECT(!strcmp(call(c3, GSQL_M_ROLLBACK, NULL), "DONE"), "ROLLBACK answered %s", last_word);
        gsql_client_recv(b, &last, &err);
        EXPECT(last.id == b2id && last.type == GSQL_M_DONE, "the second waiter was answered %s",
               gsql_msg_name((int)last.type));
        EXPECT(!strcmp(call(b, GSQL_M_ROLLBACK, NULL), "DONE"), "ROLLBACK answered %s", last_word);
        gsql_client_close(c3);
    }

    /* CANCEL of a waiting request */
    EXPECT(!strcmp(begin(a, "write"), "DONE"), "BEGIN WRITE answered %s", last_word);
    gsql_client_send(b, GSQL_M_BEGIN, &body, &bid, &err);
    sleep_ms(50);
    GsqlBuf cb = {0};
    b_one(&cb, "request", bid);
    uint32_t cid;
    gsql_client_send(b, GSQL_M_CANCEL, &cb, &cid, &err);
    gsql_client_recv(b, &last, &err);
    EXPECT(last.id == bid && !strcmp(word_of(&last, last_word, sizeof last_word), "ERROR:CANCELLED"),
           "the cancelled BEGIN was answered %s for %u", last_word, last.id);
    gsql_client_recv(b, &last, &err);
    int hit = 0;
    EXPECT(last.id == cid && last.type == GSQL_M_DONE && field_bool(&last, "cancelled", &hit) && hit,
           "the CANCEL was answered %s, cancelled %d", gsql_msg_name((int)last.type), hit);
    b_one(&cb, "request", 424242);
    w = call(b, GSQL_M_CANCEL, &cb);
    EXPECT(!strcmp(w, "DONE") && field_bool(&last, "cancelled", &hit) && !hit,
           "a CANCEL of nothing answered %s, cancelled %d", w, hit);
    EXPECT(!strcmp(call(b, GSQL_M_PING, NULL), "PONG"), "after a CANCEL the connection was not usable");
    EXPECT(!strcmp(call(a, GSQL_M_ROLLBACK, NULL), "DONE"), "ROLLBACK answered %s", last_word);
    EXPECT(!strcmp(begin(b, "write"), "DONE") && !strcmp(call(b, GSQL_M_ROLLBACK, NULL), "DONE"),
           "after a cancelled wait the connection could not take the writer");
    gsql_buf_free(&cb);

    /* a dropped connection rolls back and frees the writer */
    {
        GsqlReader rd = {0};
        GsqlFrame f;
        int fd = raw_connect(db);
        raw_hello(fd, &rd, GSQL_PROTO_MAJOR, GSQL_PROTO_MINOR, &f);
        GsqlBuf bb = {0};
        b_mode(&bb, "write");
        raw_send(fd, GSQL_M_BEGIN, 1, &bb);
        const char *p[] = { "k8", "eight" };
        b_query(&bb, "put", 2, p, 0);
        raw_send(fd, GSQL_M_QUERY, 2, &bb);
        int r1 = raw_recv(fd, &rd, &f, 5000), r2 = raw_recv(fd, &rd, &f, 5000);
        EXPECT(r1 == 1 && r2 == 1 && f.type == GSQL_M_DONE, "the raw writer's put was not answered DONE");
        close(fd);
        gsql_reader_free(&rd);
        gsql_buf_free(&bb);
        double t0 = now_us();
        EXPECT(!strcmp(begin(a, "write"), "DONE"), "after a writer dropped its connection BEGIN answered %s", last_word);
        EXPECT(now_us() - t0 < 2e6, "the writer slot took %.0f ms to come back after a dropped connection",
               (now_us() - t0) / 1e3);
        EXPECT(has(a, "k8") == 0, "a dropped connection's uncommitted write survived");
        call(a, GSQL_M_ROLLBACK, NULL);
    }

    /* cursors: batches, the end, the snapshot they hold, and their ids */
    begin(a, "write");
    for (int i = 0; i < 250; i++) {
        char k[16];
        snprintf(k, sizeof k, "s%03d", i);
        put(a, k, "v");
    }
    call(a, GSQL_M_COMMIT, NULL);
    long total = count_rows(a);
    GsqlBuf qb = {0};
    b_query(&qb, "scan", 0, NULL, 100);
    w = call(b, GSQL_M_QUERY, &qb);
    uint64_t cur = 0;
    int more = 0;
    GsBytes rows[300];
    EXPECT(!strcmp(w, "ROWS") && field_u64(&last, "cursor", &cur) && field_bool(&last, "more", &more) && more &&
           rows_of(&last, rows, 300) == 100, "a scan of %ld rows by 100 answered %s, more %d, %ld rows", total, w, more,
           rows_of(&last, rows, 300));
    V3Field fl[16];
    long nf = v3_record_fields(last.body, last.length, fl, 16);
    EXPECT(nf > 0 && v3_field_named(fl, nf, "columns"), "the first batch carries no columns");
    EXPECT(!strcmp(put(a, "zzz", "late"), "DONE"), "a write beside an open cursor answered %s", last_word);
    long seen = 100;
    int saw_late = 0;
    GsqlBuf fb = {0};
    for (int i = 0; i < 10 && more; i++) {
        b_fetch(&fb, cur, 100);
        w = call(b, GSQL_M_FETCH, &fb);
        long n = rows_of(&last, rows, 300);
        if (strcmp(w, "ROWS") || n < 0) {
            mismatch("FETCH answered %s", w);
            break;
        }
        for (long j = 0; j < n; j++) {
            GsBytes cells[2];
            if (v3_array_items(rows[j].p, rows[j].n, cells, 2) == 2 && cells[0].n == 5 && !memcmp(cells[0].p + 2, "zzz", 3)) {
                saw_late = 1;
            }
        }
        seen += n;
        field_bool(&last, "more", &more);
        nf = v3_record_fields(last.body, last.length, fl, 16);
        if (i == 0) {
            EXPECT(nf > 0 && !v3_field_named(fl, nf, "columns"), "a later batch repeats the columns");
        }
        if (!more) {
            EXPECT(nf > 0 && !v3_field_named(fl, nf, "cursor"), "the last batch still names a cursor");
        }
    }
    EXPECT(seen == total && !more, "a cursor gave %ld rows of %ld", seen, total);
    EXPECT(!saw_late, "a cursor's snapshot saw a row committed after it opened");
    b_fetch(&fb, cur, 100);
    EXPECT(!strcmp(call(b, GSQL_M_FETCH, &fb), "ERROR:NOT_FOUND"), "a FETCH past the end answered %s", last_word);
    /* CLOSE, and the cursor is gone */
    call(b, GSQL_M_QUERY, &qb);
    field_u64(&last, "cursor", &cur);
    b_one(&fb, "cursor", cur);
    EXPECT(!strcmp(call(b, GSQL_M_CLOSE, &fb), "DONE"), "CLOSE answered %s", last_word);
    b_fetch(&fb, cur, 10);
    EXPECT(!strcmp(call(b, GSQL_M_FETCH, &fb), "ERROR:NOT_FOUND"), "a FETCH after CLOSE answered %s", last_word);
    /* a cursor opened in a transaction ends with it */
    begin(b, "read");
    call(b, GSQL_M_QUERY, &qb);
    EXPECT(field_u64(&last, "cursor", &cur), "a scan in BEGIN READ has no cursor");
    call(b, GSQL_M_COMMIT, NULL);
    b_fetch(&fb, cur, 10);
    EXPECT(!strcmp(call(b, GSQL_M_FETCH, &fb), "ERROR:NOT_FOUND"), "a cursor outlived its transaction: %s", last_word);
    /* a small read keeps no cursor */
    b_query(&qb, "scan", 0, NULL, 100000);
    w = call(b, GSQL_M_QUERY, &qb);
    EXPECT(!strcmp(w, "ROWS") && !field_u64(&last, "cursor", &cur) && field_bool(&last, "more", &more) && !more,
           "a scan that fits one batch answered %s with a cursor", w);
    gsql_buf_free(&qb);
    gsql_buf_free(&fb);

    /* the principal reaches the service as the client sent it */
    {
        GsqlBuf pb = {0};
        v3_w_record(&pb, 2);
        v3_w_name(&pb, "statement");
        v3_w_text(&pb, "whoami");
        v3_w_name(&pb, "principal");
        v3_w_record(&pb, 1);
        v3_w_name(&pb, "user");
        v3_w_text(&pb, "gwen");
        size_t plen = pb.length;
        GsqlBuf want = {0};
        v3_w_record(&want, 1);
        v3_w_name(&want, "user");
        v3_w_text(&want, "gwen");
        w = call(a, GSQL_M_QUERY, &pb);
        GsBytes r[1], cells[1];
        EXPECT(!strcmp(w, "ROWS") && rows_of(&last, r, 1) == 1 && v3_array_items(r[0].p, r[0].n, cells, 1) == 1 &&
               cells[0].n == want.length && !memcmp(cells[0].p, want.bytes, want.length),
               "the principal did not reach the service unchanged (%s, %zu bytes sent)", w, plen);
        gsql_buf_free(&pb);
        gsql_buf_free(&want);
    }

    /* pipelining: many requests, then their answers in order */
    {
        enum { NP = 60 };
        uint32_t ids[NP];
        for (int i = 0; i < NP; i++) {
            if (i % 3 == 0) {
                gsql_client_send(a, GSQL_M_PING, NULL, &ids[i], &err);
            } else {
                GsqlBuf gb = {0};
                const char *p[] = { "k1" };
                b_query(&gb, "get", 1, p, 0);
                gsql_client_send(a, GSQL_M_QUERY, &gb, &ids[i], &err);
                gsql_buf_free(&gb);
            }
        }
        int order = 1;
        for (int i = 0; i < NP; i++) {
            if (gsql_client_recv(a, &last, &err) || last.id != ids[i] ||
                last.type != (i % 3 == 0 ? GSQL_M_PONG : GSQL_M_ROWS)) {
                order = 0;
            }
        }
        EXPECT(order, "%d pipelined requests were not answered in order", NP);
    }

    /* PREPARE: this service has none, and says so by name */
    {
        GsqlBuf pb = {0};
        v3_w_record(&pb, 1);
        v3_w_name(&pb, "statement");
        v3_w_text(&pb, "get");
        EXPECT(!strcmp(call(a, GSQL_M_PREPARE, &pb), "ERROR:UNSUPPORTED") && strstr(last_message, "prepare"),
               "PREPARE without a prepare answered %s: %s", last_word, last_message);
        pb.length = 0;
        v3_w_record(&pb, 0);
        EXPECT(!strcmp(call(a, GSQL_M_QUERY, &pb), "ERROR:PROTOCOL") && strstr(last_message, "statement"),
               "a QUERY with no statement answered %s", last_word);
        gsql_buf_free(&pb);
    }

    /* the handshake's refusals, and a newer minor accepted */
    {
        GsqlReader rd = {0};
        GsqlFrame f;
        int fd = raw_connect(db);
        raw_send(fd, GSQL_M_PING, 3, NULL);
        int r = raw_recv(fd, &rd, &f, 5000);
        EXPECT(r == 1 && f.type == GSQL_M_REFUSED, "a first message that is not HELLO was not REFUSED");
        remember_message(&f);
        EXPECT(strstr(last_message, "HELLO") && strstr(last_message, "PING"), "the refusal does not say why: %s",
               last_message);
        EXPECT(raw_recv(fd, &rd, &f, 5000) == -1, "after refusing, the owner kept the connection open");
        close(fd);
        gsql_reader_free(&rd);

        fd = raw_connect(db);
        raw_hello(fd, &rd, 2, 0, &f);
        EXPECT(!strcmp(word_of(&f, last_word, sizeof last_word), "REFUSED:VERSION"), "a HELLO of major 2 answered %s",
               last_word);
        remember_message(&f);
        EXPECT(strstr(last_message, "2.0") && strstr(last_message, "1.0"), "the version refusal does not name both: %s",
               last_message);
        close(fd);
        gsql_reader_free(&rd);

        fd = raw_connect(db);
        raw_hello(fd, &rd, GSQL_PROTO_MAJOR, 7, &f);
        EXPECT(f.type == GSQL_M_WELCOME, "a HELLO of a newer minor was not welcomed");
        /* and on a working connection: an unknown type is refused and the
         * connection stays; a second HELLO is STATE */
        raw_send(fd, 99, 4, NULL);
        r = raw_recv(fd, &rd, &f, 5000);
        EXPECT(r == 1 && !strcmp(word_of(&f, last_word, sizeof last_word), "ERROR:PROTOCOL") && f.id == 4,
               "an unknown message type answered %s", last_word);
        raw_send(fd, GSQL_M_HELLO, 5, NULL);
        r = raw_recv(fd, &rd, &f, 5000);
        EXPECT(r == 1 && !strcmp(word_of(&f, last_word, sizeof last_word), "ERROR:STATE"), "a second HELLO answered %s",
               last_word);
        raw_send(fd, GSQL_M_PING, 6, NULL);
        EXPECT(raw_recv(fd, &rd, &f, 5000) == 1 && f.type == GSQL_M_PONG, "after refusals the connection was not usable");
        /* a length no frame can have: said, then closed */
        unsigned char shortlen[4] = { 3, 0, 0, 0 };
        raw_bytes(fd, shortlen, 4);
        r = raw_recv(fd, &rd, &f, 5000);
        EXPECT(r == 1 && !strcmp(word_of(&f, last_word, sizeof last_word), "ERROR:PROTOCOL"),
               "a frame length of 3 answered %s", last_word);
        EXPECT(raw_recv(fd, &rd, &f, 5000) == -1, "after a short length the connection stayed open");
        close(fd);
        gsql_reader_free(&rd);

        fd = raw_connect(db);
        raw_hello(fd, &rd, GSQL_PROTO_MAJOR, 0, &f);
        unsigned char huge[4] = { 0, 0, 0, 2 };    /* 32 MiB, and none of it sent */
        raw_bytes(fd, huge, 4);
        r = raw_recv(fd, &rd, &f, 5000);
        EXPECT(r == 1 && !strcmp(word_of(&f, last_word, sizeof last_word), "ERROR:TOO_LARGE"),
               "a 32 MiB frame length answered %s", last_word);
        EXPECT(raw_recv(fd, &rd, &f, 5000) == -1, "after a frame too large the connection stayed open");
        close(fd);
        gsql_reader_free(&rd);
    }
    /* the client's own refusal: a major this owner does not speak */
    {
        GsqlClientOptions o;
        memset(&o, 0, sizeof o);
        o.owner_argv = OWNER_ARGV;
        o.major = 2;
        GsqlClient *x = NULL;
        int rc = gsql_client_open(db, &o, &x, &err);
        EXPECT(rc == GSQL_VERSION && strstr(err.message, "2.0"), "a client of major 2 was answered %s: %s",
               gsql_error_name(rc), err.message);
        gsql_client_close(x);
    }
    gsql_buf_free(&body);
    gsql_client_close(b);
    gsql_client_close(a);
    return finish();
}

/* ==== no service ========================================================= */

static int noservice_main(const char *dir) {
    char db[600];
    snprintf(db, sizeof db, "%s/nosvc", dir);
    setenv("GSQL_TEST_NO_SERVICE", "1", 1);
    GsqlClient *a = open_db(db, 1);
    unsetenv("GSQL_TEST_NO_SERVICE");
    if (!a) {
        return finish();
    }
    EXPECT(!strcmp(call(a, GSQL_M_PING, NULL), "PONG"), "an owner with no service does not answer PING");
    EXPECT(!strcmp(put(a, "k", "v"), "ERROR:UNSUPPORTED") && strstr(last_message, "SQL layer"),
           "a statement with no service answered %s: %s", last_word, last_message);
    GsqlBuf pb = {0};
    v3_w_record(&pb, 1);
    v3_w_name(&pb, "statement");
    v3_w_text(&pb, "x");
    EXPECT(!strcmp(call(a, GSQL_M_PREPARE, &pb), "ERROR:UNSUPPORTED") && strstr(last_message, "SQL layer"),
           "PREPARE with no service answered %s: %s", last_word, last_message);
    gsql_buf_free(&pb);
    /* the owner's own requests do not need a service */
    EXPECT(!strcmp(begin(a, "write"), "DONE") && !strcmp(call(a, GSQL_M_COMMIT, NULL), "DONE"),
           "BEGIN and COMMIT with no service answered %s", last_word);
    gsql_client_close(a);
    return finish();
}

/* ==== timeouts =========================================================== */

static int timeouts_main(const char *dir) {
    char db[600];
    snprintf(db, sizeof db, "%s/idle", dir);
    setenv("GSQL_TEST_SNAPSHOT_MS", "300", 1);
    setenv("GSQL_TEST_WRITER_MS", "300", 1);
    GsqlClient *a = open_db(db, 1);
    GsqlClient *b = a ? open_db(db, 0) : NULL;
    if (!a || !b) {
        return finish();
    }
    for (int i = 0; i < 30; i++) {
        char k[16];
        snprintf(k, sizeof k, "r%02d", i);
        put(a, k, "v");
    }
    /* an idle writer is rolled back, and the next writer goes on */
    EXPECT(!strcmp(begin(a, "write"), "DONE"), "BEGIN WRITE answered %s", last_word);
    put(a, "lost", "x");
    GsqlBuf mb = {0};
    b_mode(&mb, "write");
    double t0 = now_us();
    EXPECT(!strcmp(call(b, GSQL_M_BEGIN, &mb), "DONE"), "the waiting writer was answered %s", last_word);
    double waited = (now_us() - t0) / 1e3;
    EXPECT(waited > 200 && waited < 3000, "the next writer waited %.0f ms for a 300 ms idle timeout", waited);
    call(b, GSQL_M_ROLLBACK, NULL);
    EXPECT(!strcmp(put(a, "after", "x"), "ERROR:TIMEOUT") && strstr(last_message, "idle"),
           "the timed-out writer's next statement answered %s: %s", last_word, last_message);
    EXPECT(!strcmp(begin(a, "read"), "ERROR:TIMEOUT"), "BEGIN after a timeout answered %s", last_word);
    EXPECT(!strcmp(call(a, GSQL_M_ROLLBACK, NULL), "DONE"), "ROLLBACK of a timed-out transaction answered %s", last_word);
    EXPECT(has(a, "lost") == 0, "a timed-out writer's write survived");
    EXPECT(!strcmp(begin(a, "read"), "DONE") && !strcmp(call(a, GSQL_M_COMMIT, NULL), "DONE"),
           "after ROLLBACK the connection could not begin again");
    /* the control: a writer that keeps talking is not idle */
    EXPECT(!strcmp(begin(a, "write"), "DONE"), "BEGIN WRITE answered %s", last_word);
    for (int i = 0; i < 6; i++) {
        sleep_ms(100);
        call(a, GSQL_M_PING, NULL);
    }
    EXPECT(!strcmp(put(a, "kept", "x"), "DONE") && !strcmp(call(a, GSQL_M_COMMIT, NULL), "DONE"),
           "a writer active every 100 ms was timed out: %s", last_word);
    EXPECT(has(b, "kept") == 1, "the active writer's commit is missing");
    /* an idle BEGIN READ */
    begin(b, "read");
    sleep_ms(600);
    EXPECT(!strcmp(q(b, "count", 0, NULL), "ERROR:TIMEOUT"), "an idle BEGIN READ's next statement answered %s", last_word);
    call(b, GSQL_M_ROLLBACK, NULL);
    /* an idle cursor, which says why it is gone */
    GsqlBuf qb = {0};
    b_query(&qb, "scan", 0, NULL, 5);
    uint64_t cur = 0;
    EXPECT(!strcmp(call(b, GSQL_M_QUERY, &qb), "ROWS") && field_u64(&last, "cursor", &cur), "a scan had no cursor");
    sleep_ms(600);
    GsqlBuf fb = {0};
    b_fetch(&fb, cur, 5);
    EXPECT(!strcmp(call(b, GSQL_M_FETCH, &fb), "ERROR:TIMEOUT") && strstr(last_message, "idle"),
           "a FETCH on an idle cursor answered %s: %s", last_word, last_message);
    /* and a cursor fetched often enough lives */
    call(b, GSQL_M_QUERY, &qb);
    field_u64(&last, "cursor", &cur);
    int ok_all = 1, more = 1;
    for (int i = 0; i < 4 && more; i++) {
        sleep_ms(150);
        b_fetch(&fb, cur, 5);
        if (strcmp(call(b, GSQL_M_FETCH, &fb), "ROWS")) {
            ok_all = 0;
        }
        field_bool(&last, "more", &more);
    }
    EXPECT(ok_all, "a cursor fetched every 150 ms was timed out: %s", last_word);
    gsql_buf_free(&qb);
    gsql_buf_free(&fb);
    gsql_buf_free(&mb);
    gsql_client_close(b);
    gsql_client_close(a);
    unsetenv("GSQL_TEST_SNAPSHOT_MS");
    unsetenv("GSQL_TEST_WRITER_MS");
    return finish();
}

/* ==== stall ============================================================== */

static int stall_main(const char *dir) {
    char db[600];
    snprintf(db, sizeof db, "%s/stall", dir);
    setenv("GSQL_TEST_OUTPUT_LIMIT", "65536", 1);
    GsqlClient *a = open_db(db, 1);
    unsetenv("GSQL_TEST_OUTPUT_LIMIT");
    if (!a) {
        return finish();
    }
    /* half a frame from one client: the others are still served */
    GsqlReader rd = {0};
    GsqlFrame f;
    int fd = raw_connect(db);
    raw_hello(fd, &rd, GSQL_PROTO_MAJOR, GSQL_PROTO_MINOR, &f);
    GsqlBuf one = {0};
    gsql_frame_put(&one, GSQL_M_PING, 77, NULL, 0);
    raw_bytes(fd, one.bytes, 6);
    double t0 = now_us();
    int served = 0;
    for (int i = 0; i < 200; i++) {
        if (!strcmp(call(a, GSQL_M_PING, NULL), "PONG")) {
            served++;
        }
    }
    EXPECT(served == 200 && now_us() - t0 < 5e6, "with half a frame outstanding, %d of 200 pings were served in %.0f ms",
           served, (now_us() - t0) / 1e3);
    raw_bytes(fd, one.bytes + 6, one.length - 6);
    EXPECT(raw_recv(fd, &rd, &f, 5000) == 1 && f.type == GSQL_M_PONG && f.id == 77,
           "the frame completed later was not answered");
    gsql_buf_free(&one);

    /* a client that never reads: the owner stops reading it (bounded), and
     * serves the rest meanwhile */
    int fl = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    GsqlBuf burst = {0};
    for (int i = 0; i < 2000; i++) {
        gsql_frame_put(&burst, GSQL_M_PING, (uint32_t)(1000 + i), NULL, 0);
    }
    long sent = 0, rounds = 0;
    int blocked = 0;
    while (rounds < 20000 && !blocked) {
        size_t at = 0;
        while (at < burst.length) {
            ssize_t w = send(fd, burst.bytes + at, burst.length - at, MSG_NOSIGNAL);
            if (w < 0) {
                blocked = errno == EAGAIN || errno == EWOULDBLOCK;
                break;
            }
            at += (size_t)w;
        }
        sent += (long)at;
        rounds++;
        if (!blocked) {
            /* keep the owner's attention elsewhere too */
            call(a, GSQL_M_PING, NULL);
        }
    }
    EXPECT(blocked, "a client that never read its replies could send %ld bytes: the owner never stopped reading it", sent);
    EXPECT(!strcmp(call(a, GSQL_M_PING, NULL), "PONG"), "while one client's backlog was full the others were not served");
    /* drain: every reply arrives, in order, none lost */
    fcntl(fd, F_SETFL, fl);
    long frames_sent = sent / 9, got = 0;
    int in_order = 1;
    int r;
    /* the bytes sent may end mid-frame; finish that one */
    if (sent % 9) {
        GsqlBuf tail = {0};
        gsql_frame_put(&tail, GSQL_M_PING, 1, NULL, 0);
        raw_bytes(fd, tail.bytes + sent % 9, 9 - (size_t)(sent % 9));
        frames_sent++;
        gsql_buf_free(&tail);
    }
    /* ids run 1000..2999 in every burst, so each reply's id is the next */
    while (got < frames_sent && (r = raw_recv(fd, &rd, &f, 5000)) == 1) {
        uint32_t want = (uint32_t)(1000 + got % 2000);
        if (f.type != GSQL_M_PONG || (got + 1 < frames_sent && f.id != want)) {
            in_order = 0;
        }
        got++;
    }
    EXPECT(got == frames_sent && in_order, "of %ld pipelined pings %ld were answered", frames_sent, got);
    gsql_buf_free(&burst);
    close(fd);
    gsql_reader_free(&rd);
    gsql_client_close(a);
    return finish();
}

/* ==== race =============================================================== */

static int race_main(const char *dir, int n) {
    if (n < 1 || n > 200) {
        fprintf(stderr, "race: between 1 and 200 clients\n");
        return 2;
    }
    char db[600];
    snprintf(db, sizeof db, "%s/race", dir);
    int go[2], rep[2];
    if (pipe(go) || pipe(rep)) {
        return 1;
    }
    pid_t *kids = calloc((size_t)n, sizeof *kids);
    for (int i = 0; i < n; i++) {
        kids[i] = fork();
        if (kids[i] == 0) {
            close(go[1]);
            close(rep[0]);
            char x;
            if (read(go[0], &x, 1) != 1) {
                _exit(2);
            }
            GsqlClientOptions o;
            memset(&o, 0, sizeof o);
            o.owner_argv = OWNER_ARGV;
            o.create = 1;
            GsqlClient *c = NULL;
            GsqlError err;
            long line[3] = { 0, 0, 0 };
            if (gsql_client_open(db, &o, &c, &err) == GSQL_OK) {
                line[0] = (long)gsql_client_owner_pid(c);
                line[1] = (long)gsql_client_started(c);
                line[2] = strcmp(call(c, GSQL_M_PING, NULL), "PONG") == 0;
            } else {
                fprintf(stderr, "race client: %s\n", err.message);
            }
            if (write(rep[1], line, sizeof line) != (ssize_t)sizeof line) {
                _exit(3);
            }
            /* hold the connection until the parent has read everyone */
            if (read(go[0], &x, 1) < 0) {
                _exit(4);
            }
            gsql_client_close(c);
            _exit(0);
        }
    }
    close(go[0]);
    close(rep[1]);
    char start[256];
    memset(start, 'g', sizeof start);
    /* everyone at once: one byte each */
    if (write(go[1], start, (size_t)n) != (ssize_t)n) {
        return 1;
    }
    long owner = -1;
    int same = 1, starters = 0, pinged = 0, reported = 0;
    for (int i = 0; i < n; i++) {
        long line[3];
        if (read(rep[0], line, sizeof line) != (ssize_t)sizeof line) {
            break;
        }
        reported++;
        if (owner < 0) {
            owner = line[0];
        }
        if (line[0] != owner || line[0] <= 0) {
            same = 0;
        }
        starters += line[1] > 0;
        pinged += (int)line[2];
    }
    EXPECT(reported == n && same, "%d clients racing reported %d answers, %s", n, reported,
           same ? "all one owner" : "MORE THAN ONE OWNER");
    EXPECT(starters == 1, "%d clients racing: %d of them hold a running owner they started (one should)", n, starters);
    EXPECT(pinged == n, "%d of %d racing clients were served", pinged, n);
    close(go[1]);   /* EOF: the children close and exit */
    for (int i = 0; i < n; i++) {
        waitpid(kids[i], NULL, 0);
    }
    free(kids);
    return finish();
}

/* ==== paths ============================================================== */

static int exists(const char *p) {
    struct stat st;
    return lstat(p, &st) == 0;
}

static int paths_main(const char *dir) {
    char real[600], lnk[600], sock[200];
    snprintf(real, sizeof real, "%s/real", dir);
    snprintf(lnk, sizeof lnk, "%s/link", dir);
    GsqlError err;
    uint64_t d, i;

    /* no database and no permission to create one */
    {
        GsqlClientOptions o;
        memset(&o, 0, sizeof o);
        o.owner_argv = OWNER_ARGV;
        GsqlClient *x = NULL;
        int rc = gsql_client_open(real, &o, &x, &err);
        EXPECT(rc == GSQL_NOT_FOUND && strstr(err.message, real), "opening an absent database answered %s: %s",
               gsql_error_name(rc), err.message);
    }
    GsqlClient *a = open_db(real, 1);
    if (!a) {
        return finish();
    }
    if (symlink(real, lnk) != 0) {
        mismatch("symlink: %s", strerror(errno));
    }
    GsqlClient *b = open_db(lnk, 0);
    EXPECT(b && gsql_client_owner_pid(b) == gsql_client_owner_pid(a) && gsql_client_started(b) == 0,
           "a symlink to the database reached another owner");
    gsql_client_close(b);

    /* a stale socket, left by an owner killed outright, is replaced */
    gsql_socket_path(real, sock, sizeof sock, &d, &i, &err);
    pid_t first = gsql_client_owner_pid(a);
    kill(first, SIGKILL);
    waitpid(first, NULL, 0);
    EXPECT(exists(sock), "a killed owner left no socket file, so this tier measures nothing");
    EXPECT(!strcmp(call(a, GSQL_M_PING, NULL), "client:LOST"), "after its owner was killed the client answered %s",
           last_word);
    EXPECT(!strcmp(call(a, GSQL_M_PING, NULL), "PONG"), "over a stale socket the client did not reach a new owner: %s %s",
           last_word, last_message);
    EXPECT(gsql_client_owner_pid(a) != first && gsql_client_started(a) == gsql_client_owner_pid(a),
           "the new owner is not one this client started");
    gsql_client_close(a);

    /* a socket leading to ANOTHER database's owner is refused: the WELCOME
     * names the database it holds, and the client compares */
    {
        char other[700], osock[200];
        snprintf(other, sizeof other, "%s/other", dir);
        GsqlClient *x = open_db(real, 0);
        GsqlClientOptions o;
        memset(&o, 0, sizeof o);
        o.owner_argv = OWNER_ARGV;
        o.create = 1;
        GsqlClient *y = NULL;
        gsql_client_open(other, &o, &y, &err);
        pid_t yowner = y ? gsql_client_owner_pid(y) : 0;
        gsql_client_close(y);
        if (yowner > 0) {
            kill(yowner, SIGTERM);
            waitpid(yowner, NULL, 0);
        }
        /* `other` has no owner now; its socket's name is made to lead to
         * `real`'s owner */
        gsql_socket_path(other, osock, sizeof osock, &d, &i, &err);
        gsql_socket_path(real, sock, sizeof sock, &d, &i, &err);
        if (x && link(sock, osock) != 0) {
            mismatch("link: %s", strerror(errno));
        }
        o.create = 0;
        o.owner_argv = NULL;   /* never start one: only the misdirected socket answers */
        int rc = gsql_client_open(other, &o, &y, &err);
        EXPECT(rc == GSQL_PROTOCOL && strstr(err.message, "different database"),
               "a socket leading to another database's owner answered %s: %s", gsql_error_name(rc), err.message);
        gsql_client_close(y);
        unlink(osock);
        gsql_client_close(x);
    }

    /* the runtime directory is checked, not trusted */
    /* beside the runtime directory in use, since that one is short enough for
     * a socket and the test directory need not be */
    const char *saved = getenv("XDG_RUNTIME_DIR");
    char keep[600];
    snprintf(keep, sizeof keep, "%s", saved ? saved : "/tmp");
    char rt[700], rtg[800];
    snprintf(rt, sizeof rt, "%s/r2", keep);
    snprintf(rtg, sizeof rtg, "%s/gbasic", rt);
    mkdir(rt, 0700);
    mkdir(rtg, 0755);
    chmod(rtg, 0755);
    setenv("XDG_RUNTIME_DIR", rt, 1);
    int rc = gsql_socket_path(real, sock, sizeof sock, &d, &i, &err);
    EXPECT(rc == GSQL_MISUSE && strstr(err.message, "open to other users") && strstr(err.message, rtg),
           "a runtime directory at 0755 answered %s: %s", gsql_error_name(rc), err.message);
    chmod(rtg, 0700);
    EXPECT(gsql_socket_path(real, sock, sizeof sock, &d, &i, &err) == GSQL_OK,
           "the same runtime directory at 0700 was refused: %s", err.message);
    rmdir(rtg);
    char elsewhere[800];
    snprintf(elsewhere, sizeof elsewhere, "%s/elsewhere", keep);
    mkdir(elsewhere, 0700);
    if (symlink(elsewhere, rtg) != 0) {
        mismatch("symlink: %s", strerror(errno));
    }
    rc = gsql_socket_path(real, sock, sizeof sock, &d, &i, &err);
    EXPECT(rc == GSQL_MISUSE && strstr(err.message, "symlink"), "a runtime directory that is a symlink answered %s: %s",
           gsql_error_name(rc), err.message);
    {
        GsqlClientOptions o;
        memset(&o, 0, sizeof o);
        o.owner_argv = OWNER_ARGV;
        GsqlClient *x = NULL;
        rc = gsql_client_open(real, &o, &x, &err);
        EXPECT(rc == GSQL_MISUSE && strstr(err.message, "symlink"), "a client over that runtime directory answered %s: %s",
               gsql_error_name(rc), err.message);
    }
    /* a path no local socket can hold */
    char longrt[400];
    snprintf(longrt, sizeof longrt, "%s/%0120d", dir, 0);
    mkdir(longrt, 0700);
    setenv("XDG_RUNTIME_DIR", longrt, 1);
    rc = gsql_socket_path(real, sock, sizeof sock, &d, &i, &err);
    EXPECT(rc == GSQL_MISUSE && strstr(err.message, "longer than a local socket allows"),
           "a runtime directory too long for a socket answered %s: %s", gsql_error_name(rc), err.message);
    setenv("XDG_RUNTIME_DIR", keep, 1);

    /* an owner that cannot start says why, through the client */
    {
        static const char *const nothing[] = { "/nonexistent/gsql-owner", NULL };
        GsqlClientOptions o;
        memset(&o, 0, sizeof o);
        o.owner_argv = nothing;
        o.create = 1;
        char other[700];
        snprintf(other, sizeof other, "%s/never", dir);
        GsqlClient *x = NULL;
        rc = gsql_client_open(other, &o, &x, &err);
        EXPECT(rc == GSQL_NO_OWNER && strstr(err.message, "127"), "an owner program that cannot run answered %s: %s",
               gsql_error_name(rc), err.message);
        /* a directory that is not a database: the owner's open is refused,
         * and the client hears the engine's word for it */
        char foreign[700], file[800];
        snprintf(foreign, sizeof foreign, "%s/foreign", dir);
        mkdir(foreign, 0700);
        snprintf(file, sizeof file, "%s/notes.txt", foreign);
        FILE *fp = fopen(file, "w");
        if (fp) {
            fputs("not a database\n", fp);
            fclose(fp);
        }
        o.owner_argv = OWNER_ARGV;
        o.create = 0;
        rc = gsql_client_open(foreign, &o, &x, &err);
        EXPECT(rc == GSQL_NO_OWNER && strstr(err.message, "FORMAT"), "a directory that is not a database answered %s: %s",
               gsql_error_name(rc), err.message);
    }
    return finish();
}

/* ==== handoff ============================================================ */

static int handoff_main(const char *dir) {
    char db[600];
    snprintf(db, sizeof db, "%s/hand", dir);
    GsqlClient *a = open_db(db, 1);
    GsqlClient *b = a ? open_db(db, 0) : NULL;
    if (!a || !b) {
        return finish();
    }
    EXPECT(!strcmp(put(a, "kept", "1"), "DONE"), "put answered %s", last_word);
    EXPECT(!strcmp(begin(a, "write"), "DONE") && !strcmp(put(a, "inflight", "2"), "DONE"), "the open transaction: %s",
           last_word);
    pid_t first = gsql_client_owner_pid(a);
    kill(first, SIGKILL);
    waitpid(first, NULL, 0);
    double t0 = now_us();
    EXPECT(!strcmp(put(a, "next", "3"), "client:LOST") && strstr(last_message, "rolled back") &&
           strstr(last_message, "nothing was retried"),
           "the request after the owner died answered %s: %s", last_word, last_message);
    EXPECT(has(a, "kept") == 1, "a committed row did not survive the owner's death");
    double handoff = (now_us() - t0) / 1e3;
    EXPECT(gsql_client_owner_pid(a) != first, "the client did not reach a new owner");
    EXPECT(has(a, "inflight") == 0, "the dead owner's open transaction survived");
    EXPECT(has(a, "next") == 0, "the failed request was retried silently");
    /* the other client sees the loss too, and then the same new owner */
    EXPECT(!strcmp(call(b, GSQL_M_PING, NULL), "client:LOST"), "the second client answered %s after the owner died",
           last_word);
    EXPECT(!strcmp(call(b, GSQL_M_PING, NULL), "PONG") && gsql_client_owner_pid(b) == gsql_client_owner_pid(a),
           "the second client reached owner %ld, the first %ld", (long)gsql_client_owner_pid(b),
           (long)gsql_client_owner_pid(a));
    printf("handoff_ms %.2f\n", handoff);

    /* the owner dying MID-REQUEST: the request is reported lost, its write is
     * not there, and it was not repeated -- the control beside it is that the
     * same statement, run again on purpose, succeeds, so a client that had
     * repeated it would have had an answer to give */
    {
        char mark[700];
        snprintf(mark, sizeof mark, "%s.crashed", db);
        const char *p[] = { mark };
        EXPECT(!strcmp(q(a, "crash_once", 1, p), "client:LOST") && strstr(last_message, "nothing was retried"),
               "a request the owner died in the middle of answered %s: %s", last_word, last_message);
        EXPECT(has(a, "crashed") == 0, "the write of a request the owner died during survived, or the request was repeated");
        EXPECT(!strcmp(q(a, "crash_once", 1, p), "DONE"), "the same statement run again on purpose answered %s", last_word);
        EXPECT(has(a, "crashed") == 1, "the statement run again on purpose did not write");
    }

    /* SIGTERM is the planned handoff: the next open replays nothing, where
     * a kill -9 leaves the log to replay */
    for (int i = 0; i < 50; i++) {
        char k[16];
        snprintf(k, sizeof k, "p%02d", i);
        put(a, k, "v");
    }
    pid_t second = gsql_client_owner_pid(a);
    gsql_client_close(b);
    gsql_client_close(a);
    kill(second, SIGTERM);
    int status = 0;
    waitpid(second, &status, 0);
    EXPECT(WIFEXITED(status) && WEXITSTATUS(status) == 0, "the owner did not exit 0 on SIGTERM (status %d)", status);
    char sock[200];
    uint64_t d, i;
    GsqlError err;
    gsql_socket_path(db, sock, sizeof sock, &d, &i, &err);
    EXPECT(!exists(sock), "the owner left its socket after a clean stop");
    GsOptions o;
    memset(&o, 0, sizeof o);
    GsStore *st;
    GsStats stats;
    if (E->open(db, &o, &st) == GS_OK) {
        E->stats(st, &stats);
        EXPECT(stats.replayed == 0, "after SIGTERM the next open replayed %llu records", (unsigned long long)stats.replayed);
        GsTxn *w;
        E->begin_write(st, &w);
        for (int k = 0; k < 50; k++) {
            char key[16];
            snprintf(key, sizeof key, "q%02d", k);
            GsBytes kb = { (const unsigned char *)key, strlen(key) }, vb = { (const unsigned char *)"v", 1 };
            E->put(w, tree(), kb, vb, 0);
        }
        E->commit(w, 1, NULL);
        E->close(st);
    } else {
        mismatch("the database could not be opened directly after the owner stopped");
    }
    /* the control: an owner killed outright leaves its commits to replay */
    a = open_db(db, 0);
    if (a) {
        put(a, "x1", "v");
        put(a, "x2", "v");
        pid_t third = gsql_client_owner_pid(a);
        kill(third, SIGKILL);
        waitpid(third, NULL, 0);
        gsql_client_close(a);
        if (E->open(db, &o, &st) == GS_OK) {
            E->stats(st, &stats);
            EXPECT(stats.replayed > 0, "after kill -9 the next open replayed nothing, so the SIGTERM check measures nothing");
            E->close(st);
        }
    }
    return finish();
}

/* ==== lifetime =========================================================== */

static int lifetime_main(const char *dir) {
    char db[600];
    snprintf(db, sizeof db, "%s/life", dir);
    int p[2];
    if (pipe(p)) {
        return 1;
    }
    pid_t kid = fork();
    if (kid == 0) {
        close(p[0]);
        GsqlClient *c = open_db(db, 1);
        long owner = c ? (long)gsql_client_owner_pid(c) : -1;
        if (write(p[1], &owner, sizeof owner) != (ssize_t)sizeof owner) {
            _exit(2);
        }
        for (;;) {
            pause();
        }
    }
    close(p[1]);
    long owner = -1;
    if (read(p[0], &owner, sizeof owner) != (ssize_t)sizeof owner || owner <= 0) {
        mismatch("the client never reported its owner");
        return finish();
    }
    char sock[200];
    uint64_t d, i;
    GsqlError err;
    gsql_socket_path(db, sock, sizeof sock, &d, &i, &err);
    EXPECT(kill((pid_t)owner, 0) == 0 && exists(sock), "the owner is not running while its client is");
    kill(kid, SIGKILL);
    waitpid(kid, NULL, 0);
    int gone = 0;
    for (int t = 0; t < 500 && !gone; t++) {
        char stat_path[64], buf[256];
        snprintf(stat_path, sizeof stat_path, "/proc/%ld/stat", owner);
        FILE *f = fopen(stat_path, "r");
        if (!f) {
            gone = 1;
            break;
        }
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        const char *rp = strrchr(buf, ')');
        if (rp && rp[1] == ' ' && rp[2] == 'Z') {
            gone = 1;
            break;
        }
        sleep_ms(10);
    }
    EXPECT(gone, "the owner (pid %ld) outlived the client that started it by 5 s", owner);
    /* and it went the planned way: SIGTERM, so its socket is gone too */
    int sock_gone = 0;
    for (int t = 0; t < 200 && !(sock_gone = !exists(sock)); t++) {
        sleep_ms(10);
    }
    EXPECT(sock_gone, "the owner died with its client but left its socket: it did not take the planned handoff");
    return finish();
}

/* ==== cost =============================================================== */

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int cost_main(const char *dir, long iters) {
    char db[600];
    snprintf(db, sizeof db, "%s/cost", dir);
    GsqlClient *a = open_db(db, 1);
    if (!a) {
        return finish();
    }
    begin(a, "write");
    for (int k = 0; k < 1000; k++) {
        char key[16];
        snprintf(key, sizeof key, "c%04d", k);
        put(a, key, "value-of-some-length");
    }
    call(a, GSQL_M_COMMIT, NULL);
    double *lat = malloc((size_t)iters * sizeof *lat);
    long bad = 0;
    GsqlBuf qb = {0};
    double p50[2] = { 0, 0 }, p99[2] = { 0, 0 };
    for (int kind = 0; kind < 2; kind++) {
        for (long n = 0; n < iters; n++) {
            char key[16];
            snprintf(key, sizeof key, "c%04ld", (n * 7919) % 1000);
            const char *p[] = { key };
            b_query(&qb, "get", 1, p, 0);
            double t0 = now_us();
            if (kind == 0) {
                if (strcmp(call(a, GSQL_M_PING, NULL), "PONG")) {
                    bad++;
                }
            } else {
                GsBytes r[1];
                if (strcmp(call(a, GSQL_M_QUERY, &qb), "ROWS") || rows_of(&last, r, 1) != 1) {
                    bad++;
                }
            }
            lat[n] = now_us() - t0;
        }
        qsort(lat, (size_t)iters, sizeof *lat, cmp_d);
        p50[kind] = lat[iters / 2];
        p99[kind] = lat[iters * 99 / 100];
    }
    /* the same point read in process: a snapshot, the service's run, a
     * release. The owner holds the lock while this process lives, so it is
     * stopped first. */
    pid_t op = gsql_client_owner_pid(a);
    gsql_client_close(a);
    kill(op, SIGTERM);
    waitpid(op, NULL, 0);
    GsOptions o;
    memset(&o, 0, sizeof o);
    GsStore *st = NULL;
    double inproc = 0;
    if (E->open(db, &o, &st) == GS_OK) {
        for (long n = 0; n < iters; n++) {
            char key[16];
            snprintf(key, sizeof key, "c%04ld", (n * 7919) % 1000);
            const char *p[] = { key };
            b_query(&qb, "get", 1, p, 0);
            V3Field fl[8];
            long nf = v3_record_fields(qb.bytes, qb.length, fl, 8);
            const V3Field *s1 = v3_field_named(fl, nf, "statement"), *s2 = v3_field_named(fl, nf, "parameters");
            GsqlStatement s;
            memset(&s, 0, sizeof s);
            s.statement = s1->v;
            s.statement_length = s1->vlen;
            s.parameters = s2->v;
            s.parameters_length = s2->vlen;
            s.batch = 100;
            double t0 = now_us();
            GsTxn *t;
            E->begin_read(st, &t);
            GsqlResult res;
            memset(&res, 0, sizeof res);
            GsqlError err;
            if (svc_run(NULL, E, t, &s, &res, &err) != GSQL_OK) {
                bad++;
            }
            E->abort(t);
            lat[n] = now_us() - t0;
            gsql_buf_free(&res.rows);
            gsql_buf_free(&res.columns);
        }
        qsort(lat, (size_t)iters, sizeof *lat, cmp_d);
        inproc = lat[iters / 2];
        E->close(st);
    } else {
        mismatch("the database could not be opened in process");
    }
    printf("cost empty_p50_us %.2f empty_p99_us %.2f point_p50_us %.2f point_p99_us %.2f inprocess_point_p50_us %.2f "
           "added_p50_us %.2f\n", p50[0], p99[0], p50[1], p99[1], inproc, p50[1] - inproc);
    EXPECT(bad == 0, "%ld replies in the cost run were wrong", bad);
    const char *strict = getenv("GSQL_OWNER_COST_STRICT");
    if (strict && *strict == '1') {
        EXPECT(p50[1] - inproc <= 20.0, "the owner adds %.2f us to a point read at the median, past the 20 us gate",
               p50[1] - inproc);
    }
    gsql_buf_free(&qb);
    free(lat);
    return finish();
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    if (argc >= 5 && !strcmp(argv[1], "owner")) {
        return owner_main(argv[2], argv[3], argv[4]);
    }
    if (argc >= 2 && !strcmp(argv[1], "wire")) {
        return wire_main();
    }
    if (argc >= 3 && !strcmp(argv[1], "protocol")) {
        return protocol_main(argv[2]);
    }
    if (argc >= 3 && !strcmp(argv[1], "noservice")) {
        return noservice_main(argv[2]);
    }
    if (argc >= 3 && !strcmp(argv[1], "timeouts")) {
        return timeouts_main(argv[2]);
    }
    if (argc >= 3 && !strcmp(argv[1], "stall")) {
        return stall_main(argv[2]);
    }
    if (argc >= 4 && !strcmp(argv[1], "race")) {
        return race_main(argv[2], atoi(argv[3]));
    }
    if (argc >= 3 && !strcmp(argv[1], "paths")) {
        return paths_main(argv[2]);
    }
    if (argc >= 3 && !strcmp(argv[1], "handoff")) {
        return handoff_main(argv[2]);
    }
    if (argc >= 3 && !strcmp(argv[1], "lifetime")) {
        return lifetime_main(argv[2]);
    }
    if (argc >= 4 && !strcmp(argv[1], "cost")) {
        return cost_main(argv[2], atol(argv[3]));
    }
    fprintf(stderr, "usage: owner_test owner DB FD MODE | wire | protocol DIR | noservice DIR | timeouts DIR | stall DIR "
                    "| race DIR N | paths DIR | handoff DIR | lifetime DIR | cost DIR ITERS\n");
    return 2;
}

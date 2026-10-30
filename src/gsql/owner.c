/* G-SQL's owner (include/gsql_owner.h, docs/gsql_owner_design.md). The
 * parts:
 *   1. small helpers: time, bodies, the runtime directory and the socket path
 *   2. connections: their queues, output and cleanup
 *   3. the writer slot and its FIFO
 *   4. cursors
 *   5. answering one request
 *   6. the loop: poll, read, pump, write, timeouts
 *   7. starting and stopping */

#define _POSIX_C_SOURCE 200809L
#include "gsql_owner.h"
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
#include <time.h>
#include <unistd.h>

#define DEFAULT_IDLE_MS 300000u
#define DEFAULT_OUTPUT_LIMIT (4u << 20)
#define MAX_FIELDS 16
#define TIMED_OUT_KEPT 16

/* ==== 1. helpers ========================================================= */

static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static int err_set(GsqlError *err, int code, const char *fmt, ...) {
    if (err) {
        va_list ap;
        va_start(ap, fmt);
        err->code = code;
        vsnprintf(err->message, sizeof err->message, fmt, ap);
        va_end(ap);
    }
    return code;
}

/* A request body's fields. Answers the count, or -1 when the body is not a
 * record (an empty body is an empty record). */
static long body_fields(const unsigned char *p, size_t n, V3Field *f) {
    if (n == 0) {
        return 0;
    }
    return v3_record_fields(p, n, f, MAX_FIELDS);
}

static int field_u64(const V3Field *f, long n, const char *name, uint64_t *v) {
    const V3Field *x = v3_field_named(f, n, name);
    return x && v3_read_u64(x->v, x->vlen, v);
}

static char *field_text(const V3Field *f, long n, const char *name) {
    const V3Field *x = v3_field_named(f, n, name);
    return x ? v3_read_text(x->v, x->vlen) : NULL;
}

int gsql_socket_path(const char *db, char *out, size_t cap, uint64_t *device, uint64_t *inode,
                     GsqlError *err) {
    struct stat st;
    if (stat(db, &st) != 0) {
        return err_set(err, errno == ENOENT ? GSQL_NOT_FOUND : GSQL_STORAGE,
                       "the database %s cannot be found: %s", db, strerror(errno));
    }
    char dir[512];
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg && *xdg) {
        snprintf(dir, sizeof dir, "%s/gbasic", xdg);
    } else {
        snprintf(dir, sizeof dir, "/tmp/gbasic-%lu", (unsigned long)getuid());
    }
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        return err_set(err, GSQL_STORAGE, "the runtime directory %s cannot be made: %s", dir, strerror(errno));
    }
    /* checked, not trusted: an owner believes every client because only this
     * user can reach the socket, and a directory somebody else made is the
     * way round that (§3) */
    struct stat d;
    if (lstat(dir, &d) != 0) {
        return err_set(err, GSQL_STORAGE, "the runtime directory %s cannot be examined: %s", dir, strerror(errno));
    }
    if (S_ISLNK(d.st_mode) || !S_ISDIR(d.st_mode)) {
        return err_set(err, GSQL_MISUSE, "the runtime directory %s is not a directory of its own (a symlink or a file)", dir);
    }
    if (d.st_uid != getuid()) {
        return err_set(err, GSQL_MISUSE, "the runtime directory %s belongs to another user (uid %lu), so a socket in it "
                       "could be anybody's", dir, (unsigned long)d.st_uid);
    }
    if (d.st_mode & 077) {
        return err_set(err, GSQL_MISUSE, "the runtime directory %s is open to other users (mode %03o; it must be 0700), "
                       "so a socket in it could be anybody's", dir, (unsigned)(d.st_mode & 0777));
    }
    int len = snprintf(out, cap, "%s/gsql-%llx-%llx.sock", dir, (unsigned long long)st.st_dev,
                       (unsigned long long)st.st_ino);
    struct sockaddr_un a;
    /* room for the owner's temporary name too, which is longer */
    if (len < 0 || (size_t)len + 24 >= sizeof a.sun_path || (size_t)len >= cap) {
        return err_set(err, GSQL_MISUSE, "the socket path %s is longer than a local socket allows (%zu bytes); "
                       "set XDG_RUNTIME_DIR to a shorter directory", out, sizeof a.sun_path - 1);
    }
    *device = (uint64_t)st.st_dev;
    *inode = (uint64_t)st.st_ino;
    return GSQL_OK;
}

/* ==== 2. connections ===================================================== */

typedef struct Req {
    struct Req *next;
    unsigned type;
    uint32_t id;
    int cancel_hit;     /* a CANCEL that found its request parked */
    size_t length;
    unsigned char body[];
} Req;

typedef struct Cursor {
    struct Cursor *next;
    uint64_t id;
    void *svc;
    GsTxn *own;         /* an autocommit read's snapshot; NULL inside a transaction */
    uint64_t last_ms;
} Cursor;

enum { TX_NONE, TX_READ, TX_WRITE, TX_FAILED, TX_TIMED_OUT };

typedef struct {
    int fd;
    int hello;          /* the handshake is done */
    int closing;        /* close once the output is written */
    int dead;           /* closed: freed at the end of the iteration */
    GsqlReader rd;
    GsqlBuf out;
    size_t out_pos;
    Req *head, *tail;
    int tx;
    GsTxn *txn;
    uint64_t tx_ms;     /* the last request inside the transaction */
    char why[300];      /* why a transaction is FAILED or TIMED_OUT */
    Cursor *cursors;
    uint64_t timed_out[TIMED_OUT_KEPT];
    unsigned ntimed;
    int queued;         /* in the writer FIFO */
} Conn;

typedef struct {
    const GsEngine *e;
    GsStore *db;
    const GsqlService *svc;
    GsqlOwnerOptions opt;
    int ls;
    Conn **conns;
    size_t nconns, cap;
    Conn *writer;       /* holds BEGIN WRITE */
    Conn **fifo;
    size_t nfifo, fifocap;
    uint64_t next_cursor;
    uint64_t device, inode;
} Owner;

static void out_frame(Conn *c, unsigned type, uint32_t id, const GsqlBuf *body) {
    gsql_frame_put(&c->out, type, id, body ? body->bytes : NULL, body ? body->length : 0);
}

/* `position` is where in the SQL text, 1-based; 0 for none, and then the
 * field is absent (§2.2) */
static void send_error_at(Conn *c, uint32_t id, int code, const char *message, uint32_t position) {
    GsqlBuf b = {0};
    v3_w_record(&b, position ? 3 : 2);
    v3_w_name(&b, "code");
    v3_w_text(&b, gsql_error_name(code));
    v3_w_name(&b, "message");
    v3_w_text(&b, message);
    if (position) {
        v3_w_name(&b, "position");
        v3_w_int(&b, position);
    }
    out_frame(c, GSQL_M_ERROR, id, &b);
    gsql_buf_free(&b);
}

static void send_error(Conn *c, uint32_t id, int code, const char *message) {
    send_error_at(c, id, code, message, 0);
}

static void send_errorf(Conn *c, uint32_t id, int code, const char *fmt, ...) {
    char m[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof m, fmt, ap);
    va_end(ap);
    send_error(c, id, code, m);
}

static void send_empty(Conn *c, unsigned type, uint32_t id) {
    GsqlBuf b = {0};
    v3_w_record(&b, 0);
    out_frame(c, type, id, &b);
    gsql_buf_free(&b);
}

/* Writes what the socket takes now; the rest waits for POLLOUT. */
static void flush_out(Conn *c) {
    while (c->out_pos < c->out.length) {
        ssize_t w = write(c->fd, c->out.bytes + c->out_pos, c->out.length - c->out_pos);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            c->dead = 1;   /* the client went away: a disconnect */
            return;
        }
        c->out_pos += (size_t)w;
    }
    c->out.length = c->out_pos = 0;
}

static size_t out_backlog(const Conn *c) { return c->out.length - c->out_pos; }

/* ==== 3. the writer slot ================================================= */

static void fifo_remove(Owner *o, Conn *c) {
    for (size_t i = 0; i < o->nfifo; i++) {
        if (o->fifo[i] == c) {
            memmove(o->fifo + i, o->fifo + i + 1, (o->nfifo - i - 1) * sizeof *o->fifo);
            o->nfifo--;
            break;
        }
    }
    c->queued = 0;
}

/* Whether `c` may write now. If not, it waits its turn in the FIFO. */
static int writer_take(Owner *o, Conn *c) {
    if (o->writer == c) {
        return 1;
    }
    if (!o->writer && (o->nfifo == 0 || o->fifo[0] == c)) {
        if (c->queued) {
            fifo_remove(o, c);
        }
        return 1;
    }
    if (!c->queued) {
        if (o->nfifo == o->fifocap) {
            o->fifocap = o->fifocap ? o->fifocap * 2 : 8;
            Conn **g = realloc(o->fifo, o->fifocap * sizeof *g);
            if (!g) {
                abort();
            }
            o->fifo = g;
        }
        o->fifo[o->nfifo++] = c;
        c->queued = 1;
    }
    return 0;
}

/* ==== 4. cursors ========================================================= */

static void cursor_free(Owner *o, Cursor *k) {
    if (o->svc && o->svc->close && k->svc) {
        o->svc->close(o->svc->ctx, k->svc);
    }
    if (k->own) {
        o->e->abort(k->own);
    }
    free(k);
}

/* Unlinks and frees one cursor. */
static void cursor_drop(Owner *o, Conn *c, Cursor *k) {
    for (Cursor **p = &c->cursors; *p; p = &(*p)->next) {
        if (*p == k) {
            *p = k->next;
            break;
        }
    }
    cursor_free(o, k);
}

/* The cursors opened inside the connection's transaction: they end with it. */
static void cursors_of_txn(Owner *o, Conn *c) {
    Cursor **p = &c->cursors;
    while (*p) {
        if (!(*p)->own) {
            Cursor *k = *p;
            *p = k->next;
            cursor_free(o, k);
        } else {
            p = &(*p)->next;
        }
    }
}

static Cursor *cursor_find(Conn *c, uint64_t id) {
    for (Cursor *k = c->cursors; k; k = k->next) {
        if (k->id == id) {
            return k;
        }
    }
    return NULL;
}

/* Ends the connection's transaction, whatever state it is in. */
static void txn_end(Owner *o, Conn *c) {
    cursors_of_txn(o, c);
    if (c->txn) {
        o->e->abort(c->txn);
        c->txn = NULL;
    }
    if (o->writer == c) {
        o->writer = NULL;
    }
    c->tx = TX_NONE;
    c->why[0] = 0;
}

static void conn_cleanup(Owner *o, Conn *c) {
    txn_end(o, c);
    while (c->cursors) {
        cursor_drop(o, c, c->cursors);
    }
    if (c->queued) {
        fifo_remove(o, c);
    }
    while (c->head) {
        Req *r = c->head;
        c->head = r->next;
        free(r);
    }
    c->tail = NULL;
    gsql_reader_free(&c->rd);
    gsql_buf_free(&c->out);
    if (c->fd >= 0) {
        close(c->fd);
    }
    c->fd = -1;
}

/* ==== 5. answering one request =========================================== */

enum { DONE_REQ, PARKED };

/* ROWS from a result: cursor (when more remain), more, columns, rows. */
static void send_rows(Owner *o, Conn *c, uint32_t id, GsqlResult *res, GsTxn *own) {
    uint64_t cid = 0;
    if (res->cursor) {
        Cursor *k = calloc(1, sizeof *k);
        if (!k) {
            abort();
        }
        k->id = cid = ++o->next_cursor;
        k->svc = res->cursor;
        k->own = own;
        k->last_ms = now_ms();
        k->next = c->cursors;
        c->cursors = k;
    } else if (own) {
        o->e->abort(own);
    }
    GsqlBuf b = {0};
    size_t n = 2 + (res->columns.length ? 1 : 0) + (cid ? 1 : 0);
    v3_w_record(&b, n);
    if (cid) {
        v3_w_name(&b, "cursor");
        v3_w_int(&b, cid);
    }
    v3_w_name(&b, "more");
    v3_w_bool(&b, cid != 0);
    if (res->columns.length) {
        v3_w_name(&b, "columns");
        v3_put(&b, res->columns.bytes, res->columns.length);
    }
    v3_w_name(&b, "rows");
    if (res->rows.length) {
        v3_put(&b, res->rows.bytes, res->rows.length);
    } else {
        v3_w_array(&b, 0);
    }
    out_frame(c, GSQL_M_ROWS, id, &b);
    gsql_buf_free(&b);
}

static void send_done(Conn *c, uint32_t id, const GsqlResult *res, int has_commit, uint64_t commit) {
    GsqlBuf b = {0};
    v3_w_record(&b, (res && res->has_affected ? 1u : 0u) + (has_commit ? 1u : 0u));
    if (res && res->has_affected) {
        v3_w_name(&b, "rows");
        v3_w_int(&b, res->affected);
    }
    if (has_commit) {
        v3_w_name(&b, "commit");
        v3_w_int(&b, commit);
    }
    out_frame(c, GSQL_M_DONE, id, &b);
    gsql_buf_free(&b);
}

static void result_free(GsqlResult *r) {
    gsql_buf_free(&r->columns);
    gsql_buf_free(&r->rows);
}

/* A transaction that cannot take a statement answers why, and true. */
static int txn_refuses(Conn *c, uint32_t id) {
    if (c->tx == TX_FAILED) {
        send_errorf(c, id, GSQL_FAILED, "%s; the transaction was rolled back, and ROLLBACK acknowledges it", c->why);
        return 1;
    }
    if (c->tx == TX_TIMED_OUT) {
        send_errorf(c, id, GSQL_TIMEOUT, "%s; ROLLBACK acknowledges it", c->why);
        return 1;
    }
    return 0;
}

static void txn_failed(Owner *o, Conn *c, const char *why) {
    /* the engine ended the transaction: everything after it must say so,
     * never carry on outside it (§5.1) */
    cursors_of_txn(o, c);
    if (c->txn) {
        o->e->abort(c->txn);
        c->txn = NULL;
    }
    if (o->writer == c) {
        o->writer = NULL;
    }
    c->tx = TX_FAILED;
    snprintf(c->why, sizeof c->why, "%s", why);
}

static void storage_failed(Owner *o, Conn *c, const char *what) {
    char why[sizeof ((Conn *)0)->why];
    snprintf(why, sizeof why, "an earlier statement failed in the engine (%.200s)", what);
    txn_failed(o, c, why);
}

static int do_query(Owner *o, Conn *c, Req *r) {
    V3Field f[MAX_FIELDS];
    long nf = body_fields(r->body, r->length, f);
    const V3Field *stmt = nf >= 0 ? v3_field_named(f, nf, "statement") : NULL;
    if (!stmt) {
        send_error(c, r->id, GSQL_PROTOCOL, "a QUERY needs a `statement` field");
        return DONE_REQ;
    }
    if (!o->svc) {
        send_error(c, r->id, GSQL_UNSUPPORTED, "this owner has no SQL layer: the statement cannot be run");
        return DONE_REQ;
    }
    if (txn_refuses(c, r->id)) {
        return DONE_REQ;
    }
    GsqlStatement s;
    memset(&s, 0, sizeof s);
    s.statement = stmt->v;
    s.statement_length = stmt->vlen;
    const V3Field *x = v3_field_named(f, nf, "parameters");
    if (x) {
        s.parameters = x->v;
        s.parameters_length = x->vlen;
    }
    x = v3_field_named(f, nf, "principal");
    if (x) {
        s.principal = x->v;
        s.principal_length = x->vlen;
    }
    uint64_t batch = 0;
    s.batch = field_u64(f, nf, "batch", &batch) && batch > 0 ? (batch > UINT32_MAX ? UINT32_MAX : (uint32_t)batch) : 100;

    GsqlError err = { GSQL_OK, "", 0 };
    int w = o->svc->writes(o->svc->ctx, &s, &err);
    if (w < 0) {
        send_error_at(c, r->id, err.code ? err.code : GSQL_PROTOCOL, err.message, err.position);
        return DONE_REQ;
    }
    if (w && c->tx == TX_READ) {
        send_error(c, r->id, GSQL_READ_ONLY, "the statement writes, and this transaction began READ");
        return DONE_REQ;
    }
    GsTxn *t = c->txn;
    int auto_write = 0;
    if (c->tx == TX_NONE) {
        if (w) {
            if (!writer_take(o, c)) {
                return PARKED;
            }
            int rc = o->e->begin_write(o->db, &t);
            if (rc) {
                send_errorf(c, r->id, GSQL_STORAGE, "a write transaction could not begin: the engine answered %s",
                            gs_status_name(rc));
                return DONE_REQ;
            }
            auto_write = 1;
            /* its transaction ends with the statement, so a cursor could not
             * outlive it: every row comes back in one batch */
            s.batch = UINT32_MAX;
        } else {
            int rc = o->e->begin_read(o->db, &t);
            if (rc) {
                send_errorf(c, r->id, GSQL_STORAGE, "a snapshot could not be taken: the engine answered %s",
                            gs_status_name(rc));
                return DONE_REQ;
            }
        }
    }
    GsqlResult res;
    memset(&res, 0, sizeof res);
    int rc = o->svc->run(o->svc->ctx, o->e, t, &s, &res, &err);
    if (rc != GSQL_OK) {
        if (c->tx == TX_NONE) {
            if (res.cursor && o->svc->close) {
                o->svc->close(o->svc->ctx, res.cursor);
            }
            o->e->abort(t);
        } else if (rc == GSQL_STORAGE) {
            storage_failed(o, c, err.message);
        } else if (res.failed_part_way) {
            /* it changed rows before it failed, and there is nothing to undo
             * them with but the whole transaction (gsql_sql_design.md §4) */
            char why[sizeof c->why];
            snprintf(why, sizeof why, "an earlier statement failed part way, after changing rows (%.200s)", err.message);
            txn_failed(o, c, why);
        }
        send_error_at(c, r->id, rc, err.message, err.position);
        result_free(&res);
        return DONE_REQ;
    }
    if (auto_write) {
        if (res.cursor && o->svc->close) {
            o->svc->close(o->svc->ctx, res.cursor);
            res.cursor = NULL;
        }
        uint64_t id = 0;
        int crc = o->e->commit(t, 1, &id);
        if (crc) {
            send_errorf(c, r->id, GSQL_STORAGE, "the commit failed: the engine answered %s, and nothing was written",
                        gs_status_name(crc));
        } else if (res.has_rows) {
            send_rows(o, c, r->id, &res, NULL);
        } else {
            send_done(c, r->id, &res, 1, id);
        }
    } else if (res.has_rows) {
        send_rows(o, c, r->id, &res, c->tx == TX_NONE ? t : NULL);
    } else {
        if (c->tx == TX_NONE) {
            o->e->abort(t);
        }
        send_done(c, r->id, &res, 0, 0);
    }
    result_free(&res);
    return DONE_REQ;
}

static int cursor_id(Conn *c, Req *r, uint64_t *id, uint64_t *count) {
    V3Field f[MAX_FIELDS];
    long nf = body_fields(r->body, r->length, f);
    if (nf < 0 || !field_u64(f, nf, "cursor", id)) {
        send_errorf(c, r->id, GSQL_PROTOCOL, "a %s needs a `cursor` field holding its id", gsql_msg_name((int)r->type));
        return 0;
    }
    if (count && !field_u64(f, nf, "count", count)) {
        *count = 100;
    }
    return 1;
}

static int cursor_missing(Conn *c, uint32_t req, uint64_t id) {
    for (unsigned i = 0; i < c->ntimed && i < TIMED_OUT_KEPT; i++) {
        if (c->timed_out[i] == id) {
            send_errorf(c, req, GSQL_TIMEOUT, "cursor %llu was closed after being idle past the timeout, "
                        "since its snapshot keeps replaced files from being deleted", (unsigned long long)id);
            return DONE_REQ;
        }
    }
    send_errorf(c, req, GSQL_NOT_FOUND, "there is no cursor %llu on this connection", (unsigned long long)id);
    return DONE_REQ;
}

static int do_fetch(Owner *o, Conn *c, Req *r) {
    uint64_t id, count;
    if (!cursor_id(c, r, &id, &count)) {
        return DONE_REQ;
    }
    Cursor *k = cursor_find(c, id);
    if (!k) {
        return cursor_missing(c, r->id, id);
    }
    GsqlResult res;
    memset(&res, 0, sizeof res);
    res.cursor = k->svc;
    GsqlError err = { GSQL_OK, "", 0 };
    int rc = o->svc->fetch(o->svc->ctx, k->svc, count == 0 ? 1 : (count > UINT32_MAX ? UINT32_MAX : (uint32_t)count),
                           &res, &err);
    if (rc != GSQL_OK) {
        /* a failed fetch ends the cursor, never the transaction */
        cursor_drop(o, c, k);
        send_error(c, r->id, rc, err.message);
        result_free(&res);
        return DONE_REQ;
    }
    k->last_ms = now_ms();
    GsqlBuf b = {0};
    int more = res.cursor != NULL;
    v3_w_record(&b, more ? 3 : 2);
    if (more) {
        v3_w_name(&b, "cursor");
        v3_w_int(&b, id);
    }
    v3_w_name(&b, "more");
    v3_w_bool(&b, more);
    v3_w_name(&b, "rows");
    if (res.rows.length) {
        v3_put(&b, res.rows.bytes, res.rows.length);
    } else {
        v3_w_array(&b, 0);
    }
    out_frame(c, GSQL_M_ROWS, r->id, &b);
    gsql_buf_free(&b);
    if (!more) {
        k->svc = NULL;     /* the service ended it */
        cursor_drop(o, c, k);
    }
    result_free(&res);
    return DONE_REQ;
}

static int do_begin(Owner *o, Conn *c, Req *r) {
    if (txn_refuses(c, r->id)) {
        return DONE_REQ;
    }
    if (c->tx != TX_NONE) {
        send_error(c, r->id, GSQL_STATE, "a transaction is already open on this connection");
        return DONE_REQ;
    }
    V3Field f[MAX_FIELDS];
    long nf = body_fields(r->body, r->length, f);
    char *mode = nf >= 0 ? field_text(f, nf, "mode") : NULL;
    int write = mode && strcmp(mode, "write") == 0;
    if (!mode || (!write && strcmp(mode, "read") != 0)) {
        send_errorf(c, r->id, GSQL_PROTOCOL, "BEGIN needs `mode`, \"read\" or \"write\"%s%s%s",
                    mode ? " (got \"" : "", mode ? mode : "", mode ? "\")" : "");
        free(mode);
        return DONE_REQ;
    }
    free(mode);
    int rc;
    if (write) {
        if (!writer_take(o, c)) {
            return PARKED;
        }
        rc = o->e->begin_write(o->db, &c->txn);
    } else {
        rc = o->e->begin_read(o->db, &c->txn);
    }
    if (rc) {
        c->txn = NULL;
        send_errorf(c, r->id, GSQL_STORAGE, "BEGIN %s failed: the engine answered %s", write ? "WRITE" : "READ",
                    gs_status_name(rc));
        return DONE_REQ;
    }
    if (write) {
        o->writer = c;
    }
    c->tx = write ? TX_WRITE : TX_READ;
    c->tx_ms = now_ms();
    send_done(c, r->id, NULL, 0, 0);
    return DONE_REQ;
}

static int do_commit(Owner *o, Conn *c, Req *r) {
    if (c->tx == TX_NONE) {
        send_error(c, r->id, GSQL_STATE, "COMMIT with no transaction open");
        return DONE_REQ;
    }
    if (c->tx == TX_FAILED || c->tx == TX_TIMED_OUT) {
        /* COMMIT ends it, as PostgreSQL's does, and says it was rolled back */
        txn_refuses(c, r->id);
        txn_end(o, c);
        return DONE_REQ;
    }
    cursors_of_txn(o, c);
    if (c->tx == TX_READ) {
        txn_end(o, c);
        send_done(c, r->id, NULL, 0, 0);
        return DONE_REQ;
    }
    uint64_t id = 0;
    int rc = o->e->commit(c->txn, 1, &id);
    c->txn = NULL;   /* commit ends the transaction whatever it answers */
    txn_end(o, c);
    if (rc) {
        send_errorf(c, r->id, GSQL_STORAGE, "the commit failed: the engine answered %s, and nothing was written",
                    gs_status_name(rc));
    } else {
        send_done(c, r->id, NULL, 1, id);
    }
    return DONE_REQ;
}

static int do_hello(Owner *o, Conn *c, Req *r) {
    V3Field f[MAX_FIELDS];
    long nf = body_fields(r->body, r->length, f);
    uint64_t major = 0, minor = 0;
    if (nf < 0 || !field_u64(f, nf, "major", &major) || !field_u64(f, nf, "minor", &minor)) {
        GsqlBuf b = {0};
        v3_w_record(&b, 2);
        v3_w_name(&b, "code");
        v3_w_text(&b, "PROTOCOL");
        v3_w_name(&b, "message");
        v3_w_text(&b, "HELLO needs `major` and `minor`, the protocol version the client speaks");
        out_frame(c, GSQL_M_REFUSED, r->id, &b);
        gsql_buf_free(&b);
        c->closing = 1;
        return DONE_REQ;
    }
    if (major != GSQL_PROTO_MAJOR) {
        char m[200];
        snprintf(m, sizeof m, "the client speaks protocol %llu.%llu and this owner speaks %d.%d; the major versions "
                 "must match", (unsigned long long)major, (unsigned long long)minor, GSQL_PROTO_MAJOR, GSQL_PROTO_MINOR);
        GsqlBuf b = {0};
        v3_w_record(&b, 2);
        v3_w_name(&b, "code");
        v3_w_text(&b, "VERSION");
        v3_w_name(&b, "message");
        v3_w_text(&b, m);
        out_frame(c, GSQL_M_REFUSED, r->id, &b);
        gsql_buf_free(&b);
        c->closing = 1;
        return DONE_REQ;
    }
    c->hello = 1;
    GsqlBuf b = {0};
    v3_w_record(&b, 6);
    v3_w_name(&b, "major");
    v3_w_int(&b, GSQL_PROTO_MAJOR);
    v3_w_name(&b, "minor");
    v3_w_int(&b, GSQL_PROTO_MINOR);
    v3_w_name(&b, "pid");
    v3_w_int(&b, (uint64_t)getpid());
    v3_w_name(&b, "device");
    v3_w_int(&b, o->device);
    v3_w_name(&b, "inode");
    v3_w_int(&b, o->inode);
    v3_w_name(&b, "format");
    v3_w_int(&b, 1);
    out_frame(c, GSQL_M_WELCOME, r->id, &b);
    gsql_buf_free(&b);
    return DONE_REQ;
}

static int handle(Owner *o, Conn *c, Req *r) {
    if (!c->hello) {
        if (r->type == GSQL_M_HELLO) {
            return do_hello(o, c, r);
        }
        GsqlBuf b = {0};
        v3_w_record(&b, 2);
        v3_w_name(&b, "code");
        v3_w_text(&b, "PROTOCOL");
        v3_w_name(&b, "message");
        char m[160];
        snprintf(m, sizeof m, "the first message must be HELLO, and this was %s", gsql_msg_name((int)r->type));
        v3_w_text(&b, m);
        out_frame(c, GSQL_M_REFUSED, r->id, &b);
        gsql_buf_free(&b);
        c->closing = 1;
        return DONE_REQ;
    }
    if (c->tx != TX_NONE) {
        c->tx_ms = now_ms();
    }
    switch (r->type) {
    case GSQL_M_PING:
        send_empty(c, GSQL_M_PONG, r->id);
        return DONE_REQ;
    case GSQL_M_BYE:
        c->closing = 1;
        return DONE_REQ;
    case GSQL_M_BEGIN:
        return do_begin(o, c, r);
    case GSQL_M_COMMIT:
        return do_commit(o, c, r);
    case GSQL_M_ROLLBACK:
        if (c->tx == TX_NONE) {
            send_error(c, r->id, GSQL_STATE, "ROLLBACK with no transaction open");
        } else {
            txn_end(o, c);
            send_done(c, r->id, NULL, 0, 0);
        }
        return DONE_REQ;
    case GSQL_M_QUERY:
        return do_query(o, c, r);
    case GSQL_M_FETCH:
        return do_fetch(o, c, r);
    case GSQL_M_CLOSE: {
        uint64_t id;
        if (!cursor_id(c, r, &id, NULL)) {
            return DONE_REQ;
        }
        Cursor *k = cursor_find(c, id);
        if (!k) {
            return cursor_missing(c, r->id, id);
        }
        cursor_drop(o, c, k);
        send_done(c, r->id, NULL, 0, 0);
        return DONE_REQ;
    }
    case GSQL_M_PREPARE: {
        if (!o->svc || !o->svc->prepare) {
            send_error(c, r->id, GSQL_UNSUPPORTED, o->svc ? "this owner's service does not prepare statements"
                                                         : "this owner has no SQL layer: nothing can be prepared");
            return DONE_REQ;
        }
        V3Field f[MAX_FIELDS];
        long nf = body_fields(r->body, r->length, f);
        const V3Field *x = nf >= 0 ? v3_field_named(f, nf, "statement") : NULL;
        if (!x) {
            send_error(c, r->id, GSQL_PROTOCOL, "a PREPARE needs a `statement` field");
            return DONE_REQ;
        }
        GsqlStatement s;
        memset(&s, 0, sizeof s);
        s.statement = x->v;
        s.statement_length = x->vlen;
        s.batch = 1;
        GsqlBuf d = {0};
        GsqlError err = { GSQL_OK, "", 0 };
        int rc = o->svc->prepare(o->svc->ctx, &s, &d, &err);
        if (rc) {
            send_error(c, r->id, rc, err.message);
        } else {
            GsqlBuf b = {0};
            v3_w_record(&b, 1);
            v3_w_name(&b, "description");
            v3_put(&b, d.bytes, d.length);
            out_frame(c, GSQL_M_PREPARED, r->id, &b);
            gsql_buf_free(&b);
        }
        gsql_buf_free(&d);
        return DONE_REQ;
    }
    case GSQL_M_CANCEL: {
        GsqlBuf b = {0};
        v3_w_record(&b, 1);
        v3_w_name(&b, "cancelled");
        v3_w_bool(&b, r->cancel_hit);
        out_frame(c, GSQL_M_DONE, r->id, &b);
        gsql_buf_free(&b);
        return DONE_REQ;
    }
    case GSQL_M_HELLO:
        send_error(c, r->id, GSQL_STATE, "HELLO was already answered on this connection");
        return DONE_REQ;
    default:
        send_errorf(c, r->id, GSQL_PROTOCOL, "%s (type %u) is not a request a client sends",
                    gsql_msg_name((int)r->type), r->type);
        return DONE_REQ;
    }
}

/* Answers a connection's requests in order until one parks. Answers whether
 * any was answered. */
static int process(Owner *o, Conn *c) {
    int progress = 0;
    while (c->head && !c->closing && !c->dead) {
        Req *r = c->head;
        if (handle(o, c, r) == PARKED) {
            break;
        }
        c->head = r->next;
        if (!c->head) {
            c->tail = NULL;
        }
        free(r);
        progress = 1;
    }
    return progress;
}

/* ==== 6. the loop ======================================================== */

static int g_signal_pipe[2] = { -1, -1 };

static unsigned g_stop_grace_s = 10;

static void on_term(int sig) {
    (void)sig;
    int saved = errno;
    if (g_signal_pipe[1] >= 0) {
        ssize_t w = write(g_signal_pipe[1], "t", 1);
        (void)w;
    }
    /* A clean stop is asked for through the loop, which only an owner that
     * is still turning will see. One stuck in a loop of its own -- a defect,
     * or a statement that never ends -- would ignore SIGTERM for ever, and
     * with it the PR_SET_PDEATHSIG that is meant to end it with its client,
     * holding the database's lock so that no other owner could start. So the
     * request carries a deadline: SIGALRM's default action ends the process
     * if the stop has not finished by then (§4). */
    alarm(g_stop_grace_s);
    errno = saved;
}

static int nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL);
    return fl < 0 ? -1 : fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static void enqueue(Owner *o, Conn *c, const GsqlFrame *f) {
    if (f->type == GSQL_M_CANCEL) {
        /* the one request read out of order (§4): it ends its target now if
         * that target is parked, and its own answer waits its turn */
        V3Field fl[MAX_FIELDS];
        long nf = body_fields(f->body, f->length, fl);
        uint64_t target;
        int hit = 0;
        if (nf >= 0 && field_u64(fl, nf, "request", &target) && c->head && c->queued && c->head->id == target) {
            Req *t = c->head;
            send_error(c, t->id, GSQL_CANCELLED, "cancelled while waiting for the writer");
            c->head = t->next;
            if (!c->head) {
                c->tail = NULL;
            }
            free(t);
            fifo_remove(o, c);
            hit = 1;
        }
        Req *r = calloc(1, sizeof *r);
        if (!r) {
            abort();
        }
        r->type = f->type;
        r->id = f->id;
        r->cancel_hit = hit;
        if (c->tail) {
            c->tail->next = r;
        } else {
            c->head = r;
        }
        c->tail = r;
        return;
    }
    Req *r = malloc(sizeof *r + f->length);
    if (!r) {
        abort();
    }
    r->next = NULL;
    r->type = f->type;
    r->id = f->id;
    r->cancel_hit = 0;
    r->length = f->length;
    if (f->length) {
        memcpy(r->body, f->body, f->length);
    }
    if (c->tail) {
        c->tail->next = r;
    } else {
        c->head = r;
    }
    c->tail = r;
}

static void read_conn(Owner *o, Conn *c) {
    unsigned char buf[65536];
    for (;;) {
        ssize_t n = read(c->fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                c->dead = 1;
            }
            break;
        }
        if (n == 0) {
            c->dead = 1;    /* a dropped connection: cleaned up below */
            break;
        }
        gsql_reader_feed(&c->rd, buf, (size_t)n);
        if ((size_t)n < sizeof buf) {
            break;
        }
    }
    GsqlFrame f;
    for (;;) {
        int st = gsql_reader_next(&c->rd, &f);
        if (st == GSQL_READ_MORE) {
            break;
        }
        if (st == GSQL_READ_SHORT || st == GSQL_READ_TOO_LARGE) {
            /* the stream cannot be resynchronised: say why, then close */
            if (st == GSQL_READ_SHORT) {
                send_error(c, 0, GSQL_PROTOCOL, "a frame's length was under 5 bytes, which no frame can be");
            } else {
                send_errorf(c, 0, GSQL_TOO_LARGE, "a frame was longer than the limit of %u bytes", GSQL_FRAME_MAX);
            }
            c->closing = 1;
            gsql_reader_free(&c->rd);
            break;
        }
        enqueue(o, c, &f);
    }
}

static void accept_all(Owner *o) {
    for (;;) {
        int fd = accept(o->ls, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (nonblock(fd) != 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
            close(fd);
            continue;
        }
        Conn *c = calloc(1, sizeof *c);
        if (!c) {
            abort();
        }
        c->fd = fd;
        if (o->nconns == o->cap) {
            o->cap = o->cap ? o->cap * 2 : 16;
            Conn **g = realloc(o->conns, o->cap * sizeof *g);
            if (!g) {
                abort();
            }
            o->conns = g;
        }
        o->conns[o->nconns++] = c;
    }
}

static uint32_t idle(uint32_t v) { return v ? v : DEFAULT_IDLE_MS; }

/* Ends what has been idle too long, setting *ended if anything was; answers
 * the milliseconds until the next deadline, or -1 for none. */
static int timeouts(Owner *o, int *ended) {
    uint64_t now = now_ms();
    uint64_t snap = idle(o->opt.snapshot_idle_ms), wr = idle(o->opt.writer_idle_ms);
    uint64_t next = UINT64_MAX;
    for (size_t i = 0; i < o->nconns; i++) {
        Conn *c = o->conns[i];
        if (c->dead) {
            continue;
        }
        if (c->tx == TX_WRITE || c->tx == TX_READ) {
            uint64_t limit = c->tx == TX_WRITE ? wr : snap;
            if (now - c->tx_ms >= limit) {
                int was_write = c->tx == TX_WRITE;
                txn_end(o, c);
                *ended = 1;
                c->tx = TX_TIMED_OUT;
                snprintf(c->why, sizeof c->why, "the %s transaction was idle for %llu ms and was rolled back%s",
                         was_write ? "write" : "read", (unsigned long long)limit,
                         was_write ? ", so other writers could go on" : "");
            } else if (c->tx_ms + limit < next) {
                next = c->tx_ms + limit;
            }
        }
        Cursor *k = c->cursors;
        while (k) {
            Cursor *nk = k->next;
            if (now - k->last_ms >= snap) {
                c->timed_out[c->ntimed++ % TIMED_OUT_KEPT] = k->id;
                cursor_drop(o, c, k);
                *ended = 1;
            } else if (k->last_ms + snap < next) {
                next = k->last_ms + snap;
            }
            k = nk;
        }
    }
    if (next == UINT64_MAX) {
        return -1;
    }
    now = now_ms();
    return next <= now ? 0 : (next - now > 60000 ? 60000 : (int)(next - now));
}

static void serve(Owner *o) {
    struct pollfd *fds = NULL;
    size_t fcap = 0;
    for (;;) {
        /* answer what can be answered: a writer slot freed by one connection
         * may let another's parked request go on */
        int progress;
        do {
            progress = 0;
            for (size_t i = 0; i < o->nconns; i++) {
                if (!o->conns[i]->dead) {
                    progress |= process(o, o->conns[i]);
                }
            }
        } while (progress);
        for (size_t i = 0; i < o->nconns; i++) {
            Conn *c = o->conns[i];
            if (!c->dead && out_backlog(c)) {
                flush_out(c);
            }
            if (!c->dead && c->closing && !out_backlog(c)) {
                c->dead = 1;
            }
        }
        /* reap the dead, which frees their transactions and the writer slot */
        size_t keep = 0;
        int freed = 0;
        for (size_t i = 0; i < o->nconns; i++) {
            Conn *c = o->conns[i];
            if (c->dead) {
                conn_cleanup(o, c);
                free(c);
                freed = 1;
            } else {
                o->conns[keep++] = c;
            }
        }
        o->nconns = keep;
        if (freed) {
            continue;   /* a freed slot may let somebody go on */
        }
        int ended = 0;
        int wait = timeouts(o, &ended);
        if (ended) {
            continue;   /* a writer timed out: the next may go on now */
        }
        if (o->nconns + 2 > fcap) {
            fcap = o->nconns + 16;
            struct pollfd *g = realloc(fds, fcap * sizeof *g);
            if (!g) {
                abort();
            }
            fds = g;
        }
        fds[0].fd = g_signal_pipe[0];
        fds[0].events = POLLIN;
        fds[1].fd = o->ls;
        fds[1].events = POLLIN;
        for (size_t i = 0; i < o->nconns; i++) {
            Conn *c = o->conns[i];
            fds[i + 2].fd = c->fd;
            /* a client that does not read its replies is not read from either,
             * so its backlog stays bounded (§2.1) */
            fds[i + 2].events = (short)((c->closing || out_backlog(c) > (o->opt.output_limit ? o->opt.output_limit
                                                                                              : DEFAULT_OUTPUT_LIMIT))
                                            ? 0 : POLLIN);
            if (out_backlog(c)) {
                fds[i + 2].events |= POLLOUT;
            }
            fds[i + 2].revents = 0;
        }
        int n = poll(fds, (nfds_t)(o->nconns + 2), wait);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (fds[0].revents & POLLIN) {
            break;      /* SIGTERM */
        }
        size_t before = o->nconns;
        for (size_t i = 0; i < before; i++) {
            Conn *c = o->conns[i];
            short ev = fds[i + 2].revents;
            if (ev & (POLLIN | POLLHUP | POLLERR)) {
                read_conn(o, c);
            }
            if ((ev & POLLOUT) && !c->dead) {
                flush_out(c);
            }
        }
        if (fds[1].revents & POLLIN) {
            accept_all(o);
        }
    }
    free(fds);
}

/* ==== 7. starting and stopping =========================================== */

static void ready_line(int fd, const char *fmt, ...) {
    if (fd < 0) {
        return;
    }
    char m[700];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(m, sizeof m - 1, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n > sizeof m - 2) {
        n = (int)sizeof m - 2;
    }
    for (int i = 0; i < n; i++) {
        if (m[i] == '\n') {
            m[i] = ' ';
        }
    }
    m[n++] = '\n';
    for (int at = 0; at < n;) {
        ssize_t w = write(fd, m + at, (size_t)(n - at));
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w <= 0) {
            break;
        }
        at += (int)w;
    }
}

static int listen_on(const char *path, char *why, size_t wcap) {
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    char tmp[256];
    int len = snprintf(tmp, sizeof tmp, "%s.%ld.tmp", path, (long)getpid());
    if (len < 0 || (size_t)len >= sizeof a.sun_path) {
        snprintf(why, wcap, "the socket's temporary name is longer than a local socket allows");
        return -1;
    }
    memcpy(a.sun_path, tmp, (size_t)len + 1);
    unlink(tmp);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0 || fcntl(s, F_SETFD, FD_CLOEXEC) != 0 || nonblock(s) != 0 ||
        bind(s, (struct sockaddr *)&a, sizeof a) != 0 || listen(s, 128) != 0) {
        snprintf(why, wcap, "the socket %s could not be made: %s", tmp, strerror(errno));
        if (s >= 0) {
            close(s);
        }
        unlink(tmp);
        return -1;
    }
    /* renamed over a stale one in one step: this process holds the lock, so
     * no other owner exists, and a client never finds the name missing (§3) */
    if (rename(tmp, path) != 0) {
        snprintf(why, wcap, "the socket %s could not be put in place: %s", path, strerror(errno));
        close(s);
        unlink(tmp);
        return -1;
    }
    return s;
}

int gsql_owner_run(const char *db, const GsqlService *service, const GsqlOwnerOptions *opt) {
    Owner o;
    memset(&o, 0, sizeof o);
    o.e = &gs_engine_lsm;
    o.svc = service;
    if (opt) {
        o.opt = *opt;
    } else {
        o.opt.ready_fd = -1;
    }
    signal(SIGPIPE, SIG_IGN);
    if (pipe(g_signal_pipe) != 0) {
        ready_line(o.opt.ready_fd, "error the owner could not make its signal pipe: %s", strerror(errno));
        return 1;
    }
    nonblock(g_signal_pipe[0]);
    nonblock(g_signal_pipe[1]);
    fcntl(g_signal_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(g_signal_pipe[1], F_SETFD, FD_CLOEXEC);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGALRM, SIG_DFL);   /* the stop's deadline must be able to end us */
    g_stop_grace_s = o.opt.stop_grace_s ? o.opt.stop_grace_s : 10;

    GsOptions so;
    memset(&so, 0, sizeof so);
    if (o.opt.store) {
        so = *o.opt.store;
    }
    so.create = o.opt.create;
    int status = 1;
    int rc = o.e->open(db, &so, &o.db);
    if (rc == GS_BUSY) {
        /* another owner won the race: the client connects to it */
        ready_line(o.opt.ready_fd, "busy");
        status = 0;
        goto out_pipe;
    }
    if (rc) {
        ready_line(o.opt.ready_fd, "error the database %s could not be opened: the engine answered %s%s", db,
                   gs_status_name(rc), rc == GS_NOT_FOUND ? " (it does not exist, and creating it was not asked for)" : "");
        fprintf(stderr, "gsql owner: the database %s could not be opened: %s\n", db, gs_status_name(rc));
        goto out_pipe;
    }
    char sock[200];
    GsqlError err;
    if (gsql_socket_path(db, sock, sizeof sock, &o.device, &o.inode, &err) != GSQL_OK) {
        ready_line(o.opt.ready_fd, "error %s", err.message);
        fprintf(stderr, "gsql owner: %s\n", err.message);
        goto out_store;
    }
    char why[400];
    o.ls = listen_on(sock, why, sizeof why);
    if (o.ls < 0) {
        ready_line(o.opt.ready_fd, "error %s", why);
        fprintf(stderr, "gsql owner: %s\n", why);
        goto out_store;
    }
    ready_line(o.opt.ready_fd, "ready");
    if (o.opt.ready_fd >= 0) {
        close(o.opt.ready_fd);
        o.opt.ready_fd = -1;
    }
    serve(&o);
    /* the planned handoff: every transaction rolled back, the log emptied, so
     * the next owner replays nothing (§4) */
    for (size_t i = 0; i < o.nconns; i++) {
        conn_cleanup(&o, o.conns[i]);
        free(o.conns[i]);
    }
    o.nconns = 0;
    if (o.e->flush) {
        o.e->flush(o.db);
    }
    unlink(sock);   /* before the lock goes with the store */
    close(o.ls);
    status = 0;
out_store:
    o.e->close(o.db);
out_pipe:
    if (o.opt.ready_fd >= 0) {
        close(o.opt.ready_fd);
    }
    free(o.conns);
    free(o.fifo);
    close(g_signal_pipe[0]);
    close(g_signal_pipe[1]);
    g_signal_pipe[0] = g_signal_pipe[1] = -1;
    return status;
}

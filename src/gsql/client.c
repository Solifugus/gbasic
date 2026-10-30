/* G-SQL's client side (include/gsql_client.h, docs/gsql_owner_design.md §6). */

#define _POSIX_C_SOURCE 200809L
#include "gsql_client.h"
#include "gsql_owner.h"
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
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#define DEFAULT_WAIT_MS 10000u
#define ATTEMPTS 5

struct GsqlClient {
    char *db;
    GsqlClientOptions opt;
    int fd;
    uint32_t next_id;
    GsqlReader rd;
    GsqlBuf out;
    pid_t owner_pid;     /* WELCOME's */
    pid_t started;       /* the owner this client started last */
    uint64_t device, inode;
};

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

static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static void pause_ms(long ms) {
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {
    }
}

/* send, not write: a write to a socket whose owner has gone raises SIGPIPE,
 * which would end the CLIENT's process for the owner's exit */
static int write_all(int fd, const unsigned char *p, size_t n) {
    while (n) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int connect_to(const char *path) {
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof a.sun_path) {
        return -1;
    }
    memcpy(a.sun_path, path, strlen(path) + 1);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        return -1;
    }
    fcntl(s, F_SETFD, FD_CLOEXEC);
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) {
        close(s);
        return -1;
    }
    return s;
}

/* Reaps an owner this client started that has since exited, so a handoff
 * leaves no zombie behind. */
static void reap(GsqlClient *c) {
    if (c->started > 0 && waitpid(c->started, NULL, WNOHANG) == c->started) {
        c->started = 0;
    }
}

/* Starts an owner and reads its one line. Answers 'r' ready, 'b' busy, or
 * 0 with err filled. */
static int start_owner(GsqlClient *c, GsqlError *err) {
    if (!c->opt.owner_argv || !c->opt.owner_argv[0]) {
        err_set(err, GSQL_NO_OWNER, "no owner is serving %s, and this client was not told how to start one", c->db);
        return 0;
    }
    int p[2];
    if (pipe(p) != 0) {
        err_set(err, GSQL_NO_OWNER, "an owner could not be started: pipe: %s", strerror(errno));
        return 0;
    }
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    size_t argc = 0;
    while (c->opt.owner_argv[argc]) {
        argc++;
    }
    char fdtext[16];
    snprintf(fdtext, sizeof fdtext, "%d", p[1]);
    char **argv = calloc(argc + 4, sizeof *argv);
    if (!argv) {
        abort();
    }
    for (size_t i = 0; i < argc; i++) {
        argv[i] = (char *)c->opt.owner_argv[i];
    }
    argv[argc] = c->db;
    argv[argc + 1] = fdtext;
    argv[argc + 2] = (char *)(c->opt.create ? "create" : "open");
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid == 0) {
#ifdef __linux__
        /* the owner is this client's child and dies with it (§6.1); SIGTERM,
         * so it takes the planned handoff on the way out */
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parent) {
            _exit(0);   /* the parent died before the line above */
        }
#endif
        execv(argv[0], argv);
        _exit(127);
    }
    free(argv);
    close(p[1]);
    if (pid < 0) {
        close(p[0]);
        err_set(err, GSQL_NO_OWNER, "an owner could not be started: fork: %s", strerror(errno));
        return 0;
    }
    reap(c);
    c->started = pid;
    char line[800];
    size_t n = 0;
    uint64_t deadline = now_ms() + (c->opt.wait_ms ? c->opt.wait_ms : DEFAULT_WAIT_MS);
    while (n < sizeof line - 1) {
        uint64_t now = now_ms();
        if (now >= deadline) {
            break;
        }
        struct pollfd pf = { p[0], POLLIN, 0 };
        int r = poll(&pf, 1, (int)(deadline - now));
        if (r < 0 && errno == EINTR) {
            continue;
        }
        if (r <= 0) {
            break;
        }
        ssize_t got = read(p[0], line + n, sizeof line - 1 - n);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            break;
        }
        n += (size_t)got;
        if (memchr(line, '\n', n)) {
            break;
        }
    }
    close(p[0]);
    line[n] = 0;
    char *nl = strchr(line, '\n');
    if (nl) {
        *nl = 0;
    }
    if (strcmp(line, "ready") == 0) {
        return 'r';
    }
    if (strcmp(line, "busy") == 0) {
        waitpid(pid, NULL, 0);
        c->started = 0;
        return 'b';
    }
    if (strncmp(line, "error ", 6) == 0) {
        waitpid(pid, NULL, 0);
        c->started = 0;
        err_set(err, GSQL_NO_OWNER, "the owner for %s did not start: %s", c->db, line + 6);
        return 0;
    }
    /* no line: it died, or it is too slow to wait for */
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == pid) {
        c->started = 0;
        if (WIFEXITED(status)) {
            err_set(err, GSQL_NO_OWNER, "the owner for %s exited with status %d before saying it was ready%s", c->db,
                    WEXITSTATUS(status), WEXITSTATUS(status) == 127 ? " (127: its program could not be run)" : "");
        } else {
            err_set(err, GSQL_NO_OWNER, "the owner for %s died of signal %d before saying it was ready", c->db,
                    WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        }
    } else {
        err_set(err, GSQL_NO_OWNER, "the owner for %s (pid %ld) did not say it was ready within %u ms", c->db,
                (long)pid, c->opt.wait_ms ? c->opt.wait_ms : DEFAULT_WAIT_MS);
    }
    return 0;
}

static int recv_frame(GsqlClient *c, GsqlFrame *f, GsqlError *err);

static int handshake(GsqlClient *c, GsqlError *err) {
    GsqlBuf b = {0};
    const char *name = c->opt.name ? c->opt.name : "gsql";
    v3_w_record(&b, 4);
    v3_w_name(&b, "major");
    v3_w_int(&b, c->opt.major || c->opt.minor ? c->opt.major : GSQL_PROTO_MAJOR);
    v3_w_name(&b, "minor");
    v3_w_int(&b, c->opt.major || c->opt.minor ? c->opt.minor : GSQL_PROTO_MINOR);
    v3_w_name(&b, "client");
    v3_w_text(&b, name);
    v3_w_name(&b, "pid");
    v3_w_int(&b, (uint64_t)getpid());
    c->out.length = 0;
    gsql_frame_put(&c->out, GSQL_M_HELLO, 0, b.bytes, b.length);
    gsql_buf_free(&b);
    if (write_all(c->fd, c->out.bytes, c->out.length) != 0) {
        return err_set(err, GSQL_LOST, "the owner closed the connection before the handshake");
    }
    GsqlFrame f;
    int rc = recv_frame(c, &f, err);
    if (rc) {
        return rc;
    }
    V3Field fl[16];
    long nf = f.length ? v3_record_fields(f.body, f.length, fl, 16) : 0;
    if (f.type == GSQL_M_REFUSED) {
        char *code = NULL, *msg = NULL;
        if (nf > 0) {
            const V3Field *x = v3_field_named(fl, nf, "code");
            code = x ? v3_read_text(x->v, x->vlen) : NULL;
            x = v3_field_named(fl, nf, "message");
            msg = x ? v3_read_text(x->v, x->vlen) : NULL;
        }
        int k = code ? gsql_error_code(code) : -1;
        err_set(err, k > 0 ? k : GSQL_PROTOCOL, "the owner refused the connection: %s", msg ? msg : "(no reason given)");
        free(code);
        free(msg);
        return err->code;
    }
    uint64_t pid = 0, dev = 0, ino = 0;
    const V3Field *x;
    if (f.type != GSQL_M_WELCOME || nf < 0 || !(x = v3_field_named(fl, nf, "pid")) || !v3_read_u64(x->v, x->vlen, &pid) ||
        !(x = v3_field_named(fl, nf, "device")) || !v3_read_u64(x->v, x->vlen, &dev) ||
        !(x = v3_field_named(fl, nf, "inode")) || !v3_read_u64(x->v, x->vlen, &ino)) {
        return err_set(err, GSQL_PROTOCOL, "the owner answered HELLO with %s, not a WELCOME this client can read",
                       gsql_msg_name((int)f.type));
    }
    /* the socket is named from the database, and the owner says which
     * database it holds: they must be the same one */
    if (dev != c->device || ino != c->inode) {
        return err_set(err, GSQL_PROTOCOL, "the owner on this socket holds a different database (device %llx inode %llx, "
                       "where %s is device %llx inode %llx)", (unsigned long long)dev, (unsigned long long)ino, c->db,
                       (unsigned long long)c->device, (unsigned long long)c->inode);
    }
    c->owner_pid = (pid_t)pid;
    return GSQL_OK;
}

static void drop(GsqlClient *c) {
    if (c->fd >= 0) {
        close(c->fd);
    }
    c->fd = -1;
    gsql_reader_free(&c->rd);
}

/* Reaches an owner: connect, or start one. Bounded in attempts and time. */
static int reach(GsqlClient *c, GsqlError *err) {
    GsqlError last = { GSQL_NO_OWNER, "" };
    for (int attempt = 0; attempt < ATTEMPTS; attempt++) {
        char sock[200];
        int rc = gsql_socket_path(c->db, sock, sizeof sock, &c->device, &c->inode, &last);
        int started = 0;
        if (rc == GSQL_NOT_FOUND) {
            if (!c->opt.create) {
                return err_set(err, GSQL_NOT_FOUND, "%s", last.message);
            }
            /* only the owner can make it: there is no identity to take yet */
            int st = start_owner(c, &last);
            if (!st) {
                return err_set(err, last.code, "%s", last.message);
            }
            started = 1;
            rc = gsql_socket_path(c->db, sock, sizeof sock, &c->device, &c->inode, &last);
        }
        if (rc != GSQL_OK) {
            return err_set(err, last.code, "%s", last.message);
        }
        c->fd = connect_to(sock);
        if (c->fd < 0 && !started) {
            int st = start_owner(c, &last);
            if (!st) {
                return err_set(err, last.code, "%s", last.message);
            }
        }
        /* a busy owner lost to one that may not have bound its socket yet */
        uint64_t deadline = now_ms() + (c->opt.wait_ms ? c->opt.wait_ms : DEFAULT_WAIT_MS);
        while (c->fd < 0 && now_ms() < deadline) {
            c->fd = connect_to(sock);
            if (c->fd < 0) {
                pause_ms(2);
            }
        }
        if (c->fd < 0) {
            snprintf(last.message, sizeof last.message, "no owner answered on %s", sock);
            continue;   /* the winner may itself have died: start again */
        }
        rc = handshake(c, &last);
        if (rc == GSQL_OK) {
            return GSQL_OK;
        }
        drop(c);
        if (rc != GSQL_LOST) {
            return err_set(err, last.code, "%s", last.message);
        }
    }
    return err_set(err, GSQL_NO_OWNER, "no owner could be reached for %s after %d attempts: %s", c->db, ATTEMPTS,
                   last.message);
}

int gsql_client_open(const char *db, const GsqlClientOptions *opt, GsqlClient **out, GsqlError *err) {
    *out = NULL;
    GsqlClient *c = calloc(1, sizeof *c);
    if (!c) {
        abort();
    }
    c->db = malloc(strlen(db) + 1);
    if (!c->db) {
        abort();
    }
    memcpy(c->db, db, strlen(db) + 1);
    if (opt) {
        c->opt = *opt;
    }
    c->fd = -1;
    c->next_id = 1;
    int rc = reach(c, err);
    if (rc) {
        gsql_client_close(c);
        return rc;
    }
    *out = c;
    return GSQL_OK;
}

void gsql_client_close(GsqlClient *c) {
    if (!c) {
        return;
    }
    if (c->fd >= 0) {
        GsqlBuf b = {0};
        v3_w_record(&b, 0);
        c->out.length = 0;
        gsql_frame_put(&c->out, GSQL_M_BYE, c->next_id++, b.bytes, b.length);
        gsql_buf_free(&b);
        write_all(c->fd, c->out.bytes, c->out.length);
    }
    drop(c);
    reap(c);
    gsql_buf_free(&c->out);
    free(c->db);
    free(c);
}

static int lost(GsqlClient *c, GsqlError *err) {
    long pid = (long)c->owner_pid;
    drop(c);
    reap(c);
    return err_set(err, GSQL_LOST, "the owner (pid %ld) exited: the request in flight failed, and any open "
                   "transaction was rolled back; nothing was retried", pid);
}

int gsql_client_send(GsqlClient *c, unsigned type, const GsqlBuf *body, uint32_t *id, GsqlError *err) {
    if (c->fd < 0) {
        int rc = reach(c, err);
        if (rc) {
            return rc;
        }
    }
    uint32_t rid = c->next_id++;
    if (rid == 0) {
        rid = c->next_id++;
    }
    c->out.length = 0;
    if (body) {
        gsql_frame_put(&c->out, type, rid, body->bytes, body->length);
    } else {
        GsqlBuf b = {0};
        v3_w_record(&b, 0);
        gsql_frame_put(&c->out, type, rid, b.bytes, b.length);
        gsql_buf_free(&b);
    }
    if (write_all(c->fd, c->out.bytes, c->out.length) != 0) {
        return lost(c, err);
    }
    if (id) {
        *id = rid;
    }
    return GSQL_OK;
}

static int recv_frame(GsqlClient *c, GsqlFrame *f, GsqlError *err) {
    for (;;) {
        int st = gsql_reader_next(&c->rd, f);
        if (st == GSQL_READ_FRAME) {
            return GSQL_OK;
        }
        if (st != GSQL_READ_MORE) {
            drop(c);
            return err_set(err, GSQL_PROTOCOL, "the owner sent a frame this client cannot read (%s)",
                           st == GSQL_READ_SHORT ? "a length under 5" : "longer than the limit");
        }
        unsigned char buf[65536];
        ssize_t n = read(c->fd, buf, sizeof buf);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return lost(c, err);
        }
        gsql_reader_feed(&c->rd, buf, (size_t)n);
    }
}

int gsql_client_recv(GsqlClient *c, GsqlFrame *reply, GsqlError *err) {
    if (c->fd < 0) {
        return err_set(err, GSQL_LOST, "there is no connection to receive on: it was lost, and the next send reconnects");
    }
    return recv_frame(c, reply, err);
}

int gsql_client_call(GsqlClient *c, unsigned type, const GsqlBuf *body, GsqlFrame *reply, GsqlError *err) {
    uint32_t id;
    int rc = gsql_client_send(c, type, body, &id, err);
    if (rc) {
        return rc;
    }
    if ((rc = gsql_client_recv(c, reply, err))) {
        return rc;
    }
    if (reply->id != id) {
        return err_set(err, GSQL_PROTOCOL, "the owner answered request %u where %u was waiting", reply->id, id);
    }
    return GSQL_OK;
}

pid_t gsql_client_owner_pid(const GsqlClient *c) { return c->owner_pid; }
pid_t gsql_client_started(const GsqlClient *c) { return c->started; }
int gsql_client_fd(const GsqlClient *c) { return c->fd; }

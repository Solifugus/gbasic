/* G-SQL's client side (include/gsql_client.h, docs/gsql_owner_design.md §6). */

#define _POSIX_C_SOURCE 200809L
#include "gsql_client.h"
#include "gsql_owner.h"
#include "platform.h"
#include "v3.h"

#include "gsql_sock.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/wait.h>
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
    pid_t stray;         /* an intermediate not yet reaped */
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
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {
    }
#endif
}

/* send, not write: a write to a socket whose owner has gone raises SIGPIPE,
 * which would end the CLIENT's process for the owner's exit */
static int write_all(int fd, const unsigned char *p, size_t n) {
    while (n) {
#ifdef _WIN32
        ssize_t w = gs_sk_write(fd, p, n);
#else
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
#endif
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

/* The owner's socket is in the database directory, or, on a filesystem that
 * holds none, in the runtime directory (gsql_owner.h); tried in that order. */
static int connect_to(const char *sock, const char *fallback) {
    int s = gsql_socket_connect(sock);
    int full = s < 0 && errno == EAGAIN;
    if (s < 0 && fallback[0]) {
        s = gsql_socket_connect(fallback);
        full = full || (s < 0 && errno == EAGAIN);
    }
    if (s < 0) {
        errno = full ? EAGAIN : ECONNREFUSED;
    }
    return s;
}

/* Reaps an intermediate left waiting on a slow owner. The owner itself is
 * nobody's child (gsql_owner_lifetime_design.md §2). */
static void reap(GsqlClient *c) {
#ifdef _WIN32
    (void)c;   /* nothing to reap: no intermediate, and Windows keeps no zombies */
#else
    if (c->stray > 0 && waitpid(c->stray, NULL, WNOHANG) == c->stray) {
        c->stray = 0;
    }
#endif
}

/* Starts an owner and reads its one line. Answers 'r' ready, 'b' busy, or
 * 0 with err filled. */
#ifdef _WIN32
static int start_owner_win(GsqlClient *c, GsqlError *err);
#endif
static int start_owner(GsqlClient *c, GsqlError *err) {
#ifdef _WIN32
    return start_owner_win(c, err);
#else
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
    pid_t pid = fork();
    if (pid == 0) {
        /* the intermediate. The owner is detached from this client: its own
         * session and nobody's child, so it outlives the program that started
         * it while anybody uses it, and stops on its own after its linger
         * (gsql_owner_lifetime_design.md §2). This process waits for the
         * owner's one line and relays it, so an owner that exits before it
         * is ready still has its status reported (§4). */
        int q[2];
        if (setsid() < 0 || pipe(q) != 0) {
            dprintf(p[1], "error the owner could not be detached: %s\n", strerror(errno));
            _exit(0);
        }
        snprintf(fdtext, sizeof fdtext, "%d", q[1]);
        pid_t owner = fork();
        if (owner == 0) {
            close(q[0]);
            close(p[1]);
            /* holding the program's streams would hold a $(...) capture open
             * for the owner's whole life; everything it says goes on the
             * ready line or over the protocol */
            int dn = open("/dev/null", O_RDWR);
            if (dn >= 0) {
                dup2(dn, 0);
                dup2(dn, 1);
                dup2(dn, 2);
                if (dn > 2) {
                    close(dn);
                }
            }
            execv(argv[0], argv);
            _exit(127);
        }
        close(q[1]);
        char rl[800];
        size_t m = 0;
        if (owner > 0) {
            while (m < sizeof rl - 1) {
                ssize_t got = read(q[0], rl + m, sizeof rl - 1 - m);
                if (got < 0 && errno == EINTR) {
                    continue;
                }
                if (got <= 0) {
                    break;
                }
                m += (size_t)got;
                if (memchr(rl, '\n', m)) {
                    break;
                }
            }
        }
        rl[m] = 0;
        if (owner < 0) {
            dprintf(p[1], "error the owner could not be started: fork: %s\n", strerror(errno));
        } else if (memchr(rl, '\n', m)) {
            if (write(p[1], rl, m) < 0) {
                _exit(1);
            }
        } else {
            int status = 0;
            waitpid(owner, &status, 0);
            if (WIFEXITED(status)) {
                dprintf(p[1], "error it exited with status %d before saying it was ready%s\n", WEXITSTATUS(status),
                        WEXITSTATUS(status) == 127 ? " (127: its program could not be run)" : "");
            } else {
                dprintf(p[1], "error it died of signal %d before saying it was ready\n",
                        WIFSIGNALED(status) ? WTERMSIG(status) : 0);
            }
        }
        _exit(0);
    }
    free(argv);
    close(p[1]);
    if (pid < 0) {
        close(p[0]);
        err_set(err, GSQL_NO_OWNER, "an owner could not be started: fork: %s", strerror(errno));
        return 0;
    }
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
    /* the intermediate has relayed the line, or is still waiting for it;
       either way it is ours to reap, and it exits once it has relayed */
    if (strncmp(line, "ready ", 6) == 0) {
        waitpid(pid, NULL, 0);
        c->started = (pid_t)atol(line + 6);
        return 'r';
    }
    if (strcmp(line, "busy") == 0) {
        waitpid(pid, NULL, 0);
        c->started = 0;
        return 'b';
    }
    if (strncmp(line, "elsewhere ", 10) == 0) {
        /* a socket on another machine never answers here, so this is an
           answer, not something to wait out (gsql_lease_design.md §7) */
        waitpid(pid, NULL, 0);
        err_set(err, GSQL_ELSEWHERE, "the database %s is open on another machine, %s; "
                "a database has one owner, so open it from that machine or close it there first", c->db, line + 10);
        return 0;
    }
    if (strncmp(line, "error ", 6) == 0) {
        waitpid(pid, NULL, 0);
        err_set(err, GSQL_NO_OWNER, "the owner for %s did not start: %s", c->db, line + 6);
        return 0;
    }
    /* no line within the wait: the intermediate is still waiting on a slow
       owner; it relays into a pipe nobody reads and exits, and is reaped
       next time */
    c->stray = pid;
    err_set(err, GSQL_NO_OWNER, "the owner for %s did not say it was ready within %u ms", c->db,
            c->opt.wait_ms ? c->opt.wait_ms : DEFAULT_WAIT_MS);
    return 0;
#endif
}

#ifdef _WIN32
/* An argument for CreateProcess's command line, quoted the way the CRT's
 * parser undoes: backslashes are literal except before a quote. */
static void cmd_append(char **cmd, size_t *len, size_t *cap, const char *arg) {
    size_t need = *len + strlen(arg) * 2 + 4;
    if (need > *cap) {
        *cap = need * 2;
        char *p = realloc(*cmd, *cap);
        if (!p) {
            abort();
        }
        *cmd = p;
    }
    char *o = *cmd + *len;
    if (*len) {
        *o++ = ' ';
    }
    *o++ = '"';
    for (const char *s = arg;; s++) {
        size_t bs = 0;
        while (*s == '\\') {
            bs++;
            s++;
        }
        if (!*s) {
            for (size_t i = 0; i < bs * 2; i++) *o++ = '\\';   /* before the closing quote */
            break;
        }
        if (*s == '"') {
            for (size_t i = 0; i < bs * 2 + 1; i++) *o++ = '\\';
        } else {
            for (size_t i = 0; i < bs; i++) *o++ = '\\';
        }
        *o++ = *s;
    }
    *o++ = '"';
    *o = 0;
    *len = (size_t)(o - *cmd);
}

/* Windows' start (docs/gsql_owner_lifetime_design.md §2). POSIX needs an
 * intermediate and setsid so the owner is nobody's child and outlives the
 * program that started it; on Windows a process is never anybody's zombie,
 * so the owner is started directly: DETACHED_PROCESS (no console to share),
 * its own process group, and OUTSIDE the caller's job where the job allows
 * it -- a job made to end the caller's children with it would otherwise take
 * the owner too, which the design means to outlive its starter. Exactly two
 * handles cross: the ready pipe's write end, whose number goes in argv as the
 * descriptor does on POSIX, and NUL for the standard streams, so an owner
 * never holds a $(...) capture open. The line is then read with the same
 * deadline; an owner that ends first has its exit status reported. */
static int start_owner_win(GsqlClient *c, GsqlError *err) {
    if (!c->opt.owner_argv || !c->opt.owner_argv[0]) {
        err_set(err, GSQL_NO_OWNER, "no owner is serving %s, and this client was not told how to start one", c->db);
        return 0;
    }
    SECURITY_ATTRIBUTES inherit = { sizeof inherit, NULL, TRUE };
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &inherit, 0)) {
        err_set(err, GSQL_NO_OWNER, "an owner could not be started: CreatePipe failed (%lu)", GetLastError());
        return 0;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             OPEN_EXISTING, 0, NULL);
    char fdtext[32];
    snprintf(fdtext, sizeof fdtext, "%llu", (unsigned long long)(uintptr_t)wr);
    char *cmd = NULL;
    size_t len = 0, cap = 0;
    for (size_t i = 0; c->opt.owner_argv[i]; i++) {
        cmd_append(&cmd, &len, &cap, c->opt.owner_argv[i]);
    }
    cmd_append(&cmd, &len, &cap, c->db);
    cmd_append(&cmd, &len, &cap, fdtext);
    cmd_append(&cmd, &len, &cap, c->opt.create ? "create" : "open");

    HANDLE list[2] = { wr, nul };
    SIZE_T asz = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &asz);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = malloc(asz);
    STARTUPINFOEXA si;
    memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = nul;
    int have_list = attrs && nul != INVALID_HANDLE_VALUE && InitializeProcThreadAttributeList(attrs, 1, 0, &asz) &&
                    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, list, sizeof list, NULL, NULL);
    si.lpAttributeList = have_list ? attrs : NULL;
    DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP |
                  (have_list ? EXTENDED_STARTUPINFO_PRESENT : 0);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, flags | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL,
                             &si.StartupInfo, &pi);
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED) {
        /* a job that does not allow breakaway: the owner then lives in it,
           and the next client starts another if the job ends this one */
        ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, flags, NULL, NULL, &si.StartupInfo, &pi);
    }
    DWORD create_error = ok ? 0 : GetLastError();
    if (have_list) {
        DeleteProcThreadAttributeList(attrs);
    }
    free(attrs);
    free(cmd);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }
    if (!ok) {
        CloseHandle(rd);
        err_set(err, GSQL_NO_OWNER, "an owner could not be started: %s could not be run (error %lu)",
                c->opt.owner_argv[0], create_error);
        return 0;
    }
    CloseHandle(pi.hThread);

    char line[800];
    size_t n = 0;
    int eof = 0;
    uint64_t deadline = now_ms() + (c->opt.wait_ms ? c->opt.wait_ms : DEFAULT_WAIT_MS);
    while (n < sizeof line - 1 && !memchr(line, '\n', n) && now_ms() < deadline) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL)) {
            eof = 1;   /* the owner has closed its end, or ended */
            break;
        }
        if (!avail) {
            Sleep(2);
            continue;
        }
        DWORD got = 0;
        DWORD want = (DWORD)(sizeof line - 1 - n);
        if (!ReadFile(rd, line + n, avail < want ? avail : want, &got, NULL) || got == 0) {
            eof = 1;
            break;
        }
        n += got;
    }
    CloseHandle(rd);
    line[n] = 0;
    char *nl = strchr(line, '\n');
    if (nl) {
        *nl = 0;
    }
    if (!nl && eof) {
        /* it ended, or closed the pipe, before saying it was ready */
        DWORD code = 0;
        WaitForSingleObject(pi.hProcess, 2000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        if (code == STILL_ACTIVE) {
            err_set(err, GSQL_NO_OWNER, "the owner for %s did not start: it closed its ready line without a word", c->db);
        } else {
            err_set(err, GSQL_NO_OWNER, "the owner for %s did not start: it exited with status %lu before saying it "
                    "was ready", c->db, (unsigned long)code);
        }
        return 0;
    }
    CloseHandle(pi.hProcess);
    if (strncmp(line, "ready ", 6) == 0) {
        c->started = (pid_t)atol(line + 6);
        return 'r';
    }
    if (strcmp(line, "busy") == 0) {
        c->started = 0;
        return 'b';
    }
    if (strncmp(line, "elsewhere ", 10) == 0) {
        err_set(err, GSQL_ELSEWHERE, "the database %s is open on another machine, %s; "
                "a database has one owner, so open it from that machine or close it there first", c->db, line + 10);
        return 0;
    }
    if (strncmp(line, "error ", 6) == 0) {
        err_set(err, GSQL_NO_OWNER, "the owner for %s did not start: %s", c->db, line + 6);
        return 0;
    }
    err_set(err, GSQL_NO_OWNER, "the owner for %s did not say it was ready within %u ms", c->db,
            c->opt.wait_ms ? c->opt.wait_ms : DEFAULT_WAIT_MS);
    return 0;
}
#endif

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
        gs_sk_close(c->fd);
    }
    c->fd = -1;
    gsql_reader_free(&c->rd);
}

/* Reaches an owner: connect, or start one. Bounded in time: ATTEMPTS tries
 * at once, then one every 20 ms until the client's wait has passed. Owners
 * that come and go at linger 0 can take several tries in a row from a client,
 * each lost in milliseconds, so a count alone gave up on a database that was
 * there (one open in 8000 under the churn tier). */
static int reach(GsqlClient *c, GsqlError *err) {
    GsqlError last = { GSQL_NO_OWNER, "", 0, 0, "", "", "", 0 };
    uint64_t until = now_ms() + (c->opt.wait_ms ? c->opt.wait_ms : DEFAULT_WAIT_MS);
    int attempt;
    for (attempt = 0; attempt < ATTEMPTS || now_ms() < until; attempt++) {
        if (attempt >= ATTEMPTS) {
            pause_ms(20);
        }
        char sock[4200], fallback[600];
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
        /* a runtime directory that is refused matters only to an owner that
           had to bind there, and that owner says so through start_owner */
        GsqlError ignored;
        if (gsql_socket_fallback(c->db, 0, fallback, sizeof fallback, &ignored) != GSQL_OK) {
            fallback[0] = 0;
        }
        c->fd = connect_to(sock, fallback);
        if (c->fd < 0 && !started) {
            int st = start_owner(c, &last);
            if (!st) {
                return err_set(err, last.code, "%s", last.message);
            }
        }
        /* An owner says ready once bound, and busy once the winner's socket
           answered, so only a socket too busy to accept (EAGAIN, its backlog
           full) is worth waiting for. Anything else is an owner that has
           gone: at linger 0 a winner leaves with its last client within
           milliseconds of answering, and waiting 10 s for it was gdash's
           G20-15 (docs/gsql_owner_lifetime_design.md §3). Start again. */
        uint64_t deadline = now_ms() + (c->opt.wait_ms ? c->opt.wait_ms : DEFAULT_WAIT_MS);
        if (c->fd < 0) {
            c->fd = connect_to(sock, fallback);
        }
        while (c->fd < 0 && errno == EAGAIN && now_ms() < deadline) {
            pause_ms(2);
            c->fd = connect_to(sock, fallback);
        }
        if (c->fd < 0) {
            snprintf(last.message, sizeof last.message, "no owner answered on %.400s", sock);
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
    return err_set(err, GSQL_NO_OWNER, "no owner could be reached for %s after %d attempts: %s", c->db, attempt,
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
        ssize_t n = gs_sk_read(c->fd, buf, sizeof buf);
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

/* §5.5's two remaining measurements: the OWNER ROUND TRIP, and MANY OWNERS
 * on one machine (docs/gsql_storage_design.md §5.5; the protocol is
 * docs/gsql_design.md §6.5). Measured over the chosen engine (src/gsql/lsm.c).
 *
 *   roundtrip owner DIR SOCKET          serve DIR's store on SOCKET until BYE
 *   roundtrip trip DIR ROWS ITERS       one owner, a client, and in-process
 *   roundtrip tenants ROOT N            N owners, one database each
 *
 * The transport is §6.5's: AF_UNIX SOCK_STREAM, frames of
 *   le32 length-of-the-rest, u8 type, le32 request id, body
 * and the owner is single-threaded, polling every connection and answering
 * each connection's requests in order. The bodies here are storage requests,
 * not SQL (there is no SQL layer yet): GET a key, SCAN n rows from a key. So
 * what is measured is the transport and the owner's dispatch, which is the
 * part the protocol adds; a query's own work is the same either way.
 *
 * A REQUEST IN PROCESS IS THE SAME WORK: a snapshot, the read, a release, as
 * G3 measured. The difference between the two columns is what the protocol
 * costs.
 *
 * Prints one JSON line per measurement. Every reply is checked: a GET answers
 * the value that key was loaded with, and a SCAN answers N rows in order
 * starting at the key asked for. */

#define _GNU_SOURCE /* accept4 */
#include "store_common.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

static const GsEngine *E = &gs_engine_lsm;

enum { T_PING = 1, T_PONG, T_GET, T_VALUE, T_SCAN, T_ROWS, T_ERROR, T_BYE };
#define MAX_FRAME (16u << 20)
#define VLEN 100

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
static size_t key_of(char *k, uint64_t i) { return (size_t)snprintf(k, 32, "k%015llu", (unsigned long long)i); }
static void value_of(unsigned char *v, uint64_t i) {
    for (int j = 0; j < VLEN; j++) v[j] = (unsigned char)(i * 31 + (uint64_t)j);
}

/* ---- frames -------------------------------------------------------------- */

static int write_all(int fd, const void *p, size_t n) {
    const unsigned char *b = p;
    while (n) {
        ssize_t w = write(fd, b, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        b += w, n -= (size_t)w;
    }
    return 0;
}
static int read_all(int fd, void *p, size_t n) {
    unsigned char *b = p;
    while (n) {
        ssize_t r = read(fd, b, n);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) return -1;
        b += r, n -= (size_t)r;
    }
    return 0;
}
static int send_frame(int fd, unsigned type, uint32_t id, const void *body, size_t n) {
    unsigned char h[9];
    put32(h, (uint32_t)(n + 5));
    h[4] = (unsigned char)type;
    put32(h + 5, id);
    /* one write for a small frame: two would cost a second system call and,
     * over a stream socket, nothing else */
    if (n <= 4096) {
        unsigned char buf[9 + 4096];
        memcpy(buf, h, 9);
        if (n) memcpy(buf + 9, body, n);
        return write_all(fd, buf, 9 + n);
    }
    return write_all(fd, h, 9) || write_all(fd, body, n) ? -1 : 0;
}
/* body into *buf (grown as needed); answers -1 on a closed or bad stream */
static int recv_frame(int fd, unsigned *type, uint32_t *id, GsBuf *buf) {
    unsigned char h[9];
    if (read_all(fd, h, 9)) return -1;
    uint32_t len = get32(h);
    if (len < 5 || len > MAX_FRAME) return -1;
    *type = h[4];
    *id = get32(h + 5);
    if (!gsbuf_reserve(buf, len - 5)) return -1;
    buf->n = len - 5;
    return buf->n ? read_all(fd, buf->p, buf->n) : 0;
}

/* ---- the owner ----------------------------------------------------------- */

static int listen_on(const char *path) {
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof a.sun_path) return -1;
    strcpy(a.sun_path, path);
    unlink(path); /* the lock decides (§6.5), and this process holds it */
    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 64)) return -1;
    return s;
}

static int serve(GsStore *db, unsigned type, const GsBuf *in, GsBuf *out, unsigned *rtype) {
    out->n = 0;
    if (type == T_PING) { *rtype = T_PONG; return 0; }
    GsTxn *r;
    if (E->begin_read(db, &r)) { *rtype = T_ERROR; return 0; }
    int rc = GS_OK;
    if (type == T_GET) {
        GsBytes v;
        rc = E->get(r, gss("t"), gsb(in->p, in->n), &v);
        if (rc == GS_OK && !gsbuf_set(out, v.p, v.n)) rc = GS_IO;
        *rtype = rc == GS_OK ? T_VALUE : T_ERROR;
    } else if (type == T_SCAN && in->n >= 4) {
        uint32_t want = get32(in->p);
        GsCursor *c;
        int pos = 0;
        rc = E->cursor_open(r, gss("t"), &c);
        if (rc == GS_OK) {
            rc = E->seek(c, gsb(in->p + 4, in->n - 4), GS_SEEK_GE, &pos);
            unsigned char lb[10];
            for (uint32_t i = 0; rc == GS_OK && pos && i < want; i++) {
                GsBytes k, v;
                if ((rc = E->key(c, &k)) || (rc = E->value(c, &v))) break;
                if (!gsbuf_append(out, lb, varint_put(lb, k.n)) || !gsbuf_append(out, k.p, k.n) ||
                    !gsbuf_append(out, lb, varint_put(lb, v.n)) || !gsbuf_append(out, v.p, v.n)) rc = GS_IO;
                if (rc == GS_OK) rc = E->next(c, &pos);
            }
            E->cursor_close(c);
        }
        *rtype = rc == GS_OK ? T_ROWS : T_ERROR;
    } else {
        *rtype = T_ERROR;
    }
    E->abort(r);
    return 0;
}

static int owner(const char *dir, const char *sock) {
    GsOptions o = {1, 0, 0, NULL, 0, 0, 0, 0};
    const char *ta = getenv("GS_TUNE_A");
    if (ta) o.tune_a = strtoull(ta, NULL, 10);
    GsStore *db;
    int rc = E->open(dir, &o, &db);
    if (rc) { fprintf(stderr, "owner: open %s answered %s\n", dir, gs_status_name(rc)); return 1; }
    int ls = listen_on(sock);
    if (ls < 0) { fprintf(stderr, "owner: cannot listen on %s\n", sock); return 1; }
    struct pollfd fds[256];
    GsBuf in = {0}, out = {0};
    int n = 1, done = 0;
    fds[0].fd = ls;
    fds[0].events = POLLIN;
    while (!done) {
        if (poll(fds, (nfds_t)n, -1) < 0) { if (errno == EINTR) continue; break; }
        if (fds[0].revents & POLLIN) {
            int c = accept4(ls, NULL, NULL, SOCK_CLOEXEC);
            if (c >= 0 && n < 256) { fds[n].fd = c; fds[n].events = POLLIN; fds[n].revents = 0; n++; }
            else if (c >= 0) close(c);
        }
        for (int i = 1; i < n; i++) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            unsigned type, rtype;
            uint32_t id;
            if (recv_frame(fds[i].fd, &type, &id, &in) || type == T_BYE) {
                if (type == T_BYE) done = 1;
                close(fds[i].fd);
                fds[i--] = fds[--n];
                continue;
            }
            serve(db, type, &in, &out, &rtype);
            if (send_frame(fds[i].fd, rtype, id, out.p, out.n)) { close(fds[i].fd); fds[i--] = fds[--n]; }
        }
    }
    for (int i = 1; i < n; i++) close(fds[i].fd);
    close(ls);
    unlink(sock);
    gsbuf_free(&in);
    gsbuf_free(&out);
    E->close(db);
    return 0;
}

/* ---- the client ---------------------------------------------------------- */

static int connect_to(const char *path) {
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, path);
    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) return -1;
    if (connect(s, (struct sockaddr *)&a, sizeof a)) { close(s); return -1; }
    return s;
}

static pid_t start_owner(const char *self, const char *dir, const char *sock) {
    pid_t p = fork();
    if (p == 0) {
        execl(self, self, "owner", dir, sock, (char *)NULL);
        _exit(127);
    }
    return p;
}

/* connects, retrying until the owner is listening; -1 after `ms` */
static int connect_wait(const char *sock, double ms) {
    double t0 = now_us();
    for (;;) {
        int s = connect_to(sock);
        if (s >= 0) return s;
        if (now_us() - t0 > ms * 1000) return -1;
        struct timespec ts = {0, 20000};
        nanosleep(&ts, NULL);
    }
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static void stats_line(const char *what, const char *where, double *lat, size_t n, unsigned long long bad) {
    qsort(lat, n, sizeof *lat, cmp_d);
    double sum = 0;
    for (size_t i = 0; i < n; i++) sum += lat[i];
    printf("{\"test\":\"roundtrip\",\"request\":\"%s\",\"path\":\"%s\",\"n\":%zu,\"mean_us\":%.2f,\"p50_us\":%.2f,"
           "\"p99_us\":%.2f,\"bad_replies\":%llu}\n",
           what, where, n, sum / (double)n, lat[n / 2], lat[n * 99 / 100], bad);
    fflush(stdout);
}

static int check_rows(const GsBuf *b, uint64_t first, uint32_t want) {
    size_t q = 0;
    for (uint32_t i = 0; i < want; i++) {
        uint64_t kn, vn;
        char k[32];
        unsigned char v[VLEN];
        size_t a = varint_get(b->p + q, b->n - q, &kn);
        if (!a || q + a + kn > b->n) return 0;
        size_t ekn = key_of(k, first + i);
        if (kn != ekn || memcmp(b->p + q + a, k, ekn)) return 0;
        q += a + kn;
        a = varint_get(b->p + q, b->n - q, &vn);
        if (!a || vn != VLEN) return 0;
        value_of(v, first + i);
        if (memcmp(b->p + q + a, v, VLEN)) return 0;
        q += a + vn;
    }
    return q == b->n;
}

static int load_rows(const char *dir, uint64_t rows) {
    GsOptions o = {1, 0, 0, NULL, 0, 0, 0, 0};
    const char *ta = getenv("GS_TUNE_A");
    if (ta) o.tune_a = strtoull(ta, NULL, 10);
    GsStore *db;
    int rc = E->open(dir, &o, &db);
    if (rc) return rc;
    GsTxn *w;
    E->begin_write(db, &w);
    E->tree_create(w, gss("t"));
    for (uint64_t i = 0; i < rows && rc == GS_OK; i++) {
        char k[32];
        unsigned char v[VLEN];
        size_t kn = key_of(k, i);
        value_of(v, i);
        rc = E->put(w, gss("t"), gsb(k, kn), gsb(v, VLEN), GS_APPEND);
    }
    if (rc == GS_OK) rc = E->commit(w, 1, NULL); else E->abort(w);
    /* the planned handoff: nothing for the owner to replay */
    if (rc == GS_OK && E->flush) rc = E->flush(db);
    E->close(db);
    return rc;
}

static uint64_t rnd_state = 88172645463325252ULL;
static uint64_t rnd(void) { rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 7; rnd_state ^= rnd_state << 17; return rnd_state; }

static int trip(const char *self, const char *dir, uint64_t rows, size_t iters) {
    if (load_rows(dir, rows)) { fprintf(stderr, "load failed\n"); return 1; }
    char sock[600];
    snprintf(sock, sizeof sock, "%s.sock", dir);
    double *lat = malloc(iters * sizeof *lat);
    GsBuf b = {0};
    unsigned long long bad;
    /* in process first, then the owner, so the two do not share a cache */
    {
        GsOptions o = {0, 0, 0, NULL, 0, 0, 0, 0};
        GsStore *db;
        if (E->open(dir, &o, &db)) return 1;
        GsBuf in = {0}, out = {0};
        unsigned rt;
        for (int kind = 0; kind < 3; kind++) {
            bad = 0;
            for (size_t i = 0; i < iters; i++) {
                uint64_t r = kind == 2 ? rnd() % (rows - 100) : rnd() % rows;
                char k[32];
                size_t kn = key_of(k, r);
                in.n = 0;
                unsigned char cnt[4];
                put32(cnt, 100);
                if (kind == 2) gsbuf_append(&in, cnt, 4);
                gsbuf_append(&in, k, kn);
                double t0 = now_us();
                serve(db, kind == 0 ? T_PING : kind == 1 ? T_GET : T_SCAN, &in, &out, &rt);
                lat[i] = now_us() - t0;
                if (kind == 1) { unsigned char v[VLEN]; value_of(v, r); if (rt != T_VALUE || out.n != VLEN || memcmp(out.p, v, VLEN)) bad++; }
                if (kind == 2 && (rt != T_ROWS || !check_rows(&out, r, 100))) bad++;
                if (kind == 0 && rt != T_PONG) bad++;
            }
            stats_line(kind == 0 ? "empty" : kind == 1 ? "point read" : "100 rows", "in process", lat, iters, bad);
        }
        gsbuf_free(&in);
        gsbuf_free(&out);
        E->close(db);
    }
    pid_t p = start_owner(self, dir, sock);
    int s = connect_wait(sock, 5000);
    if (s < 0) { fprintf(stderr, "no owner\n"); kill(p, SIGKILL); waitpid(p, NULL, 0); return 1; }
    for (int kind = 0; kind < 3; kind++) {
        bad = 0;
        for (size_t i = 0; i < iters; i++) {
            uint64_t r = kind == 2 ? rnd() % (rows - 100) : rnd() % rows;
            char k[32];
            unsigned char body[40];
            size_t kn = key_of(k, r), bn = 0;
            if (kind == 2) { put32(body, 100); bn = 4; }
            if (kind) { memcpy(body + bn, k, kn); bn += kn; }
            unsigned type;
            uint32_t id;
            double t0 = now_us();
            if (send_frame(s, kind == 0 ? T_PING : kind == 1 ? T_GET : T_SCAN, (uint32_t)i, body, bn) ||
                recv_frame(s, &type, &id, &b)) { bad++; break; }
            lat[i] = now_us() - t0;
            if (id != (uint32_t)i) bad++;
            if (kind == 0 && type != T_PONG) bad++;
            if (kind == 1) { unsigned char v[VLEN]; value_of(v, r); if (type != T_VALUE || b.n != VLEN || memcmp(b.p, v, VLEN)) bad++; }
            if (kind == 2 && (type != T_ROWS || !check_rows(&b, r, 100))) bad++;
        }
        stats_line(kind == 0 ? "empty" : kind == 1 ? "point read" : "100 rows", "owner", lat, iters, bad);
    }
    send_frame(s, T_BYE, 0, NULL, 0);
    close(s);
    waitpid(p, NULL, 0);
    gsbuf_free(&b);
    free(lat);
    return 0;
}

/* ---- many owners ---------------------------------------------------------- */

static long status_kb(pid_t p, const char *field) {
    char path[64], line[256];
    snprintf(path, sizeof path, "/proc/%d/status", (int)p);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long v = -1;
    size_t fl = strlen(field);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, field, fl)) { v = strtol(line + fl, NULL, 10); break; }
    fclose(f);
    return v;
}
static int fd_count(pid_t p) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/fd", (int)p);
    DIR *d = opendir(path);
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) if (e->d_name[0] != '.') n++;
    closedir(d);
    return n;
}
static unsigned long long dir_bytes(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    unsigned long long n = 0;
    struct dirent *e;
    char p[1200];
    struct stat st;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.') { snprintf(p, sizeof p, "%s/%s", dir, e->d_name); if (!stat(p, &st)) n += (unsigned long long)st.st_blocks * 512; }
    closedir(d);
    return n;
}

static int tenants(const char *self, const char *root, int n) {
    pid_t *pid = calloc((size_t)n, sizeof *pid);
    double *cold = calloc((size_t)n, sizeof *cold);
    char dir[600], sock[600];
    unsigned long long disk = 0;
    for (int i = 0; i < n; i++) {
        snprintf(dir, sizeof dir, "%s/t%04d", root, i);
        if (load_rows(dir, 100)) { fprintf(stderr, "tenant %d: load failed\n", i); return 1; }
        disk += dir_bytes(dir);
    }
    /* cold start: the client finds no owner, starts one, and is answered */
    unsigned long long bad = 0;
    int *conn = calloc((size_t)n, sizeof *conn);
    for (int i = 0; i < n; i++) {
        snprintf(dir, sizeof dir, "%s/t%04d", root, i);
        snprintf(sock, sizeof sock, "%s/t%04d.sock", root, i);
        double t0 = now_us();
        pid[i] = start_owner(self, dir, sock);
        int s = connect_wait(sock, 10000);
        GsBuf b = {0};
        unsigned type;
        uint32_t id;
        char k[32];
        size_t kn = key_of(k, 7);
        if (s < 0 || send_frame(s, T_GET, 1, k, kn) || recv_frame(s, &type, &id, &b) || type != T_VALUE || b.n != VLEN) bad++;
        cold[i] = (now_us() - t0) / 1000;
        gsbuf_free(&b);
        conn[i] = s;
    }
    /* every owner idle now, holding a store and a client */
    struct timespec ts = {0, 200 * 1000000};
    nanosleep(&ts, NULL);
    long rss = 0, rss_max = 0;
    int fds = 0;
    for (int i = 0; i < n; i++) {
        long r = status_kb(pid[i], "VmRSS:");
        int f = fd_count(pid[i]);
        rss += r;
        if (r > rss_max) rss_max = r;
        fds += f;
    }
    qsort(cold, (size_t)n, sizeof *cold, cmp_d);
    double sum = 0;
    for (int i = 0; i < n; i++) sum += cold[i];
    printf("{\"test\":\"tenants\",\"owners\":%d,\"idle_rss_kb_mean\":%ld,\"idle_rss_kb_max\":%ld,\"fds_per_owner\":%.1f,"
           "\"cold_start_ms_mean\":%.2f,\"cold_start_ms_p50\":%.2f,\"cold_start_ms_p99\":%.2f,\"disk_bytes_per_db\":%llu,"
           "\"bad_replies\":%llu}\n",
           n, rss / n, rss_max, (double)fds / n, sum / n, cold[n / 2], cold[(size_t)n * 99 / 100], disk / (unsigned long long)n, bad);
    fflush(stdout);
    for (int i = 0; i < n; i++) {
        if (conn[i] >= 0) { send_frame(conn[i], T_BYE, 0, NULL, 0); close(conn[i]); }
        waitpid(pid[i], NULL, 0);
    }
    free(pid);
    free(cold);
    free(conn);
    return 0;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    if (argc >= 4 && !strcmp(argv[1], "owner")) return owner(argv[2], argv[3]);
    if (argc >= 5 && !strcmp(argv[1], "trip")) return trip("/proc/self/exe", argv[2], strtoull(argv[3], NULL, 10), (size_t)strtoull(argv[4], NULL, 10));
    if (argc >= 4 && !strcmp(argv[1], "tenants")) return tenants("/proc/self/exe", argv[2], atoi(argv[3]));
    fprintf(stderr, "usage: roundtrip owner DIR SOCKET | trip DIR ROWS ITERS | tenants ROOT N\n");
    return 2;
}

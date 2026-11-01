#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
/* The lease (docs/gsql_lease_design.md): one 512-byte sector beside the
 * database, renewed by its owner once a second, watched by an opener on
 * another machine with that opener's own monotonic clock. No timestamp one
 * machine wrote is ever compared with another's clock. */

#include "lease.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "io_internal.h"
#include "store_common.h"

#define LEASE_SIZE GSQL_IO_SECTOR
#define LEASE_FORMAT 1u
#define ID_N 40   /* a machine-id is 32 hex digits, a boot id a 36-character uuid */
#define HOST_N 64

enum { L_ABSENT, L_UNREADABLE, L_READ };

typedef struct {
    unsigned char token[16];
    uint64_t counter, pid;
    char machine[ID_N + 1], boot[ID_N + 1], host[HOST_N + 1];
} Rec;

typedef struct FencedIo FencedIo;

struct GsLease {
    GsqlIo *io;                   /* the inner layer: the lease's own writes go here */
    char *path;
    uint32_t renew_ms, trust_ms, dead_ms, settle_ms, race_ms;
    Rec mine;
    uint64_t confirmed;           /* when our record was last read back as ours */
    int fenced;
    char holder[HOST_N + 48];
    uint64_t renewals, renew_us_max;
    FencedIo *fio;
};

static uint64_t now(const GsLease *l) { return gsql_io_now_ms(l->io); }
static void nap(const GsLease *l, uint64_t ms) { gsql_io_sleep_ms(l->io, ms); }

/* ---- who we are ------------------------------------------------------------ */

/* The first line of a small file, trimmed; "" where there is none. */
static void first_line(const char *path, char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\r\n")] = 0;
    fclose(f);
}

static void identify(Rec *r, const GsLeaseOptions *opt) {
    memset(r->machine, 0, sizeof r->machine);
    memset(r->boot, 0, sizeof r->boot);
    memset(r->host, 0, sizeof r->host);
#if !defined(_WIN32)
    gethostname(r->host, HOST_N);
    r->host[HOST_N] = 0;
    r->pid = (uint64_t)getpid();
#endif
    if (opt && opt->machine) {
        /* a test's machine: it is its own boot too */
        snprintf(r->machine, sizeof r->machine, "%s", opt->machine);
        snprintf(r->boot, sizeof r->boot, "%s", opt->machine);
        snprintf(r->host, sizeof r->host, "%s", opt->machine);
        return;
    }
    first_line("/etc/machine-id", r->machine, sizeof r->machine);
    if (!r->machine[0]) first_line("/var/lib/dbus/machine-id", r->machine, sizeof r->machine);
    if (!r->machine[0]) { memcpy(r->machine, "host:", 5); memcpy(r->machine + 5, r->host, ID_N - 5); r->machine[ID_N] = 0; }
    /* no boot id (outside Linux): the fast path is never taken, since an
     * empty boot never matches (same_boot) */
    first_line("/proc/sys/kernel/random/boot_id", r->boot, sizeof r->boot);
}

static int same_boot(const Rec *a, const Rec *b) {
    return a->boot[0] && !strcmp(a->machine, b->machine) && !strcmp(a->boot, b->boot);
}

static void describe(const Rec *r, char *out, size_t n) {
    snprintf(out, n, "host %s (pid %llu)", r->host[0] ? r->host : r->machine, (unsigned long long)r->pid);
}

/* ---- the sector ------------------------------------------------------------- */

static void put64le(unsigned char *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (unsigned char)(v >> (8 * i)); }
static uint64_t get64le(const unsigned char *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }

/* magic 8, format 4, flags 4, token 16, counter 8, pid 8, machine 41, boot
 * 41, host 65, zeros, CRC-32C of everything before it in the last 4 */
static void encode(const Rec *r, unsigned char *s) {
    memset(s, 0, LEASE_SIZE);
    memcpy(s, "GSQLLEAS", 8);
    put64le(s + 8, LEASE_FORMAT);
    memcpy(s + 16, r->token, 16);
    put64le(s + 32, r->counter);
    put64le(s + 40, r->pid);
    memcpy(s + 48, r->machine, ID_N + 1);
    memcpy(s + 48 + ID_N + 1, r->boot, ID_N + 1);
    memcpy(s + 48 + 2 * (ID_N + 1), r->host, HOST_N + 1);
    put64le(s + LEASE_SIZE - 8, crc32c(s, LEASE_SIZE - 8));
}

static int decode(const unsigned char *s, Rec *r) {
    if (memcmp(s, "GSQLLEAS", 8) || get64le(s + 8) != LEASE_FORMAT) return 0;
    if (get64le(s + LEASE_SIZE - 8) != crc32c(s, LEASE_SIZE - 8)) return 0;
    memcpy(r->token, s + 16, 16);
    r->counter = get64le(s + 32);
    r->pid = get64le(s + 40);
    memcpy(r->machine, s + 48, ID_N + 1);
    memcpy(r->boot, s + 48 + ID_N + 1, ID_N + 1);
    memcpy(r->host, s + 48 + 2 * (ID_N + 1), HOST_N + 1);
    r->machine[ID_N] = r->boot[ID_N] = r->host[HOST_N] = 0;
    return 1;
}

/* Opens the file afresh every time: an NFS client revalidates a file at open
 * (close-to-open consistency) and may serve a file it keeps open from a cache
 * for up to a minute, which would show a live owner's counter standing still.
 * `raw` gets the sector as read, so any change at all counts as movement. */
static int lease_read(GsLease *l, Rec *r, unsigned char *raw, int *state) {
    GsqlFile *f = NULL;
    int rc = gsql_io_open(l->io, l->path, GSQL_IO_READ_ONLY, &f);
    memset(raw, 0, LEASE_SIZE);
    if (rc == ENOENT) { *state = L_ABSENT; return 0; }
    if (rc) return rc;
    size_t got = 0;
    rc = gsql_io_read_at(f, 0, raw, LEASE_SIZE, &got);
    gsql_io_close(f);
    if (rc) return rc;
    *state = got == LEASE_SIZE && decode(raw, r) ? L_READ : L_UNREADABLE;
    return 0;
}

/* `create`: exclusively, for a lease that is absent; EEXIST says another
 * opener made it first. The name is made durable before anyone relies on it. */
static int lease_write(GsLease *l, int create) {
    unsigned char s[LEASE_SIZE];
    encode(&l->mine, s);
    GsqlFile *f = NULL;
    int rc = gsql_io_open(l->io, l->path, create ? GSQL_IO_CREATE | GSQL_IO_EXCLUSIVE : GSQL_IO_CREATE, &f);
    if (rc) return rc;
    rc = gsql_io_write_at(f, 0, s, LEASE_SIZE);
    if (rc == 0) rc = gsql_io_sync(f);
    int crc = gsql_io_close(f);
    if (rc == 0) rc = crc;
    if (rc == 0 && create) {
        char *dir = gs_dirname(l->path);
        rc = gsql_io_sync_dir(l->io, dir);
        free(dir);
    }
    return rc;
}

static int ours(const GsLease *l, const Rec *r) { return !memcmp(r->token, l->mine.token, 16); }

static void fence(GsLease *l, const Rec *r) {
    l->fenced = 1;
    describe(r, l->holder, sizeof l->holder);
}

/* ---- taking it ------------------------------------------------------------- */

char *gs_lease_lock_name(const char *dbpath, const GsLeaseOptions *opt) {
    if (!opt || !opt->lock_local || !opt->machine) return gs_path_with(dbpath, ".lock");
    char suffix[ID_N + 8];
    snprintf(suffix, sizeof suffix, ".lock.%s", opt->machine);
    return gs_path_with(dbpath, suffix);
}

static int refuse(const Rec *r, int state, char *why, size_t n) {
    if (why && n) {
        if (state == L_READ) describe(r, why, n);
        else snprintf(why, n, "another machine (its lease is being written)");
    }
    return GS_ELSEWHERE;
}

int gs_lease_take(GsqlIo *io, const char *dbpath, const GsLeaseOptions *opt, GsLease **out, char *why, size_t why_n) {
    GsLease *l = calloc(1, sizeof *l);
    if (!l) return GS_IO;
    l->io = io;
    l->path = gs_path_with(dbpath, ".lease");
    l->renew_ms = opt && opt->renew_ms ? opt->renew_ms : 1000;
    l->trust_ms = opt && opt->trust_ms ? opt->trust_ms : 4000;
    l->dead_ms = opt && opt->dead_ms ? opt->dead_ms : 12000;
    l->settle_ms = opt && opt->settle_ms ? opt->settle_ms : 2000;
    l->race_ms = opt ? opt->race_ms : 0;
    identify(&l->mine, opt);
    gsql_io_random(io, l->mine.token, sizeof l->mine.token);
    l->mine.counter = 1;

    Rec r = {0};
    unsigned char raw[LEASE_SIZE], again[LEASE_SIZE];
    int state = L_ABSENT, rc = lease_read(l, &r, raw, &state);
    if (rc) goto fail;
    if (state == L_ABSENT) {
        /* step 1: no lease, so no owner of this build: create it, exclusively */
        if (l->race_ms) nap(l, l->race_ms);
        rc = lease_write(l, 1);
        if (rc == 0) goto taken;
        if (rc != EEXIST) goto fail;
        /* another opener made it first: go on as if it had been there */
        if ((rc = lease_read(l, &r, raw, &state))) goto fail;
    }
    if (state == L_READ && same_boot(&r, &l->mine)) {
        /* step 2: our flock says nobody here is running: a restart after a crash */
        l->mine.counter = r.counter + 1;
        if ((rc = lease_write(l, 0))) goto fail;
        goto taken;
    }
    /* step 3: another machine's, or unreadable: watch it stand still */
    uint64_t start = now(l), step = l->renew_ms / 4 ? l->renew_ms / 4 : 1;
    while (now(l) - start < l->dead_ms) {
        nap(l, step);
        Rec r2 = {0};
        int st2 = L_ABSENT;
        if ((rc = lease_read(l, &r2, again, &st2))) goto fail;
        if (st2 == L_ABSENT || memcmp(raw, again, LEASE_SIZE)) {
            /* moved, or removed by a clean close and perhaps already re-taken:
             * either way not ours to take now */
            rc = refuse(st2 == L_READ ? &r2 : &r, st2 == L_READ ? L_READ : state, why, why_n);
            gs_lease_close(l, 0);
            return rc;
        }
    }
    /* step 4: write ours, re-reading just before, then settle */
    {
        Rec r2 = {0};
        int st2 = L_ABSENT;
        if ((rc = lease_read(l, &r2, again, &st2))) goto fail;
        if (st2 == L_ABSENT || memcmp(raw, again, LEASE_SIZE)) {
            rc = refuse(st2 == L_READ ? &r2 : &r, st2 == L_READ ? L_READ : state, why, why_n);
            gs_lease_close(l, 0);
            return rc;
        }
        if (state == L_READ) l->mine.counter = r.counter + 1;
        if (l->race_ms) nap(l, l->race_ms);
        if ((rc = lease_write(l, 0))) goto fail;
        nap(l, l->settle_ms);
        if ((rc = lease_read(l, &r2, again, &st2))) goto fail;
        if (st2 != L_READ || !ours(l, &r2)) {
            rc = refuse(&r2, st2, why, why_n);
            gs_lease_close(l, 0);
            return rc;
        }
    }
taken:
    l->confirmed = now(l);
    *out = l;
    return GS_OK;
fail:
    gs_lease_close(l, 0);
    return gs_io_status(rc);
}

/* ---- keeping it -------------------------------------------------------------- */

static int renew(GsLease *l) {
    if (l->fenced) return GS_FENCED;
    uint64_t t0 = now(l);
    struct timespec a = {0}, b = {0};
#if !defined(_WIN32)
    clock_gettime(CLOCK_MONOTONIC, &a);
#endif
    Rec r = {0};
    unsigned char raw[LEASE_SIZE];
    int state = L_ABSENT, rc = lease_read(l, &r, raw, &state);
    if (rc) return gs_io_status(rc);
    if (state == L_READ && !ours(l, &r)) { fence(l, &r); return GS_FENCED; }
    l->mine.counter++;
    if (state == L_ABSENT) {
        /* removed under us: whoever creates it now holds it */
        rc = lease_write(l, 1);
        if (rc == EEXIST) {
            if ((rc = lease_read(l, &r, raw, &state))) return gs_io_status(rc);
            if (state != L_READ || !ours(l, &r)) {
                if (state == L_READ) fence(l, &r);
                else { l->fenced = 1; snprintf(l->holder, sizeof l->holder, "another machine"); }
                return GS_FENCED;
            }
        }
    } else {
        /* ours, or unreadable (a torn write: ours, or a rival's that the
         * rival's own settle will see we overwrote) */
        rc = lease_write(l, 0);
    }
    if (rc) return gs_io_status(rc);
    l->confirmed = t0;
    l->renewals++;
#if !defined(_WIN32)
    clock_gettime(CLOCK_MONOTONIC, &b);
    uint64_t us = (uint64_t)(b.tv_sec - a.tv_sec) * 1000000u + (uint64_t)((b.tv_nsec - a.tv_nsec) / 1000);
    if (us > l->renew_us_max) l->renew_us_max = us;
#else
    (void)a; (void)b;
#endif
    return GS_OK;
}

int gs_lease_keep(GsLease *l, int force, uint64_t *due_ms) {
    int rc = GS_OK;
    if (l->fenced) rc = GS_FENCED;
    else if (force || now(l) - l->confirmed >= l->renew_ms) rc = renew(l);
    if (due_ms) {
        uint64_t age = now(l) - l->confirmed;
        *due_ms = age >= l->renew_ms ? 0 : l->renew_ms - age;
    }
    return rc;
}

/* §5's fence: renew first when the last confirmation is too old to trust */
static int fence_check(GsLease *l) {
    if (l->fenced) return GSQL_IO_FENCED;
    if (now(l) - l->confirmed < l->trust_ms) return 0;
    int rc = renew(l);
    if (rc == GS_FENCED) return GSQL_IO_FENCED;
    return rc == GS_OK ? 0 : EIO;
}

int gs_lease_fenced(const GsLease *l) { return l->fenced; }
const char *gs_lease_holder(const GsLease *l) { return l->holder; }

void gs_lease_stats(const GsLease *l, uint64_t *renewals, uint64_t *renew_us_max) {
    *renewals = l->renewals;
    *renew_us_max = l->renew_us_max;
}

/* ---- the fenced layer --------------------------------------------------------- */

struct FencedIo {
    GsqlIo base;
    GsqlIo *in;
    GsLease *l;
};

typedef struct {
    GsqlFile base;
    GsqlFile *in;
    GsLease *l;
} FencedFile;

#define FIN(f) (((FencedFile *)(f))->in)
#define FL(f) (((FencedFile *)(f))->l)
#define IOIN(io) (((FencedIo *)(io))->in)
#define IOL(io) (((FencedIo *)(io))->l)

static int f_open(GsqlIo *io, const char *path, int flags, GsqlFile **out) {
    int rc;
    if ((flags & GSQL_IO_CREATE) && (rc = fence_check(IOL(io)))) return rc;
    GsqlFile *in = NULL;
    if ((rc = gsql_io_open(IOIN(io), path, flags, &in))) return rc;
    FencedFile *f = calloc(1, sizeof *f);
    if (!f) { gsql_io_close(in); return ENOMEM; }
    f->base.io = io;
    f->in = in;
    f->l = IOL(io);
    *out = &f->base;
    return 0;
}
static int f_read_at(GsqlFile *f, uint64_t o, void *b, size_t n, size_t *got) { return gsql_io_read_at(FIN(f), o, b, n, got); }
static int f_write_at(GsqlFile *f, uint64_t o, const void *b, size_t n) {
    int rc = fence_check(FL(f));
    return rc ? rc : gsql_io_write_at(FIN(f), o, b, n);
}
static int f_sync(GsqlFile *f) { return gsql_io_sync(FIN(f)); }
static int f_size(GsqlFile *f, uint64_t *o) { return gsql_io_size(FIN(f), o); }
static int f_truncate(GsqlFile *f, uint64_t n) {
    int rc = fence_check(FL(f));
    return rc ? rc : gsql_io_truncate(FIN(f), n);
}
static int f_close(GsqlFile *f) {
    int rc = gsql_io_close(FIN(f));
    free(f);
    return rc;
}
static int f_rename(GsqlIo *io, const char *a, const char *b) {
    int rc = fence_check(IOL(io));
    return rc ? rc : gsql_io_rename(IOIN(io), a, b);
}
static int f_remove(GsqlIo *io, const char *p) {
    int rc = fence_check(IOL(io));
    return rc ? rc : gsql_io_remove(IOIN(io), p);
}
static int f_sync_dir(GsqlIo *io, const char *d) { return gsql_io_sync_dir(IOIN(io), d); }
static int f_lock(GsqlFile *f, int nb) { return gsql_io_lock(FIN(f), nb); }
static void f_free(GsqlIo *io) { (void)io; /* gs_lease_close owns it */ }
static void f_random(GsqlIo *io, void *o, size_t n) { gsql_io_random(IOIN(io), o, n); }
static int f_mkdir(GsqlIo *io, const char *p) {
    int rc = fence_check(IOL(io));
    return rc ? rc : gsql_io_mkdir(IOIN(io), p);
}
static int f_list(GsqlIo *io, const char *d, int (*each)(void *, const char *), void *ctx) {
    return gsql_io_list(IOIN(io), d, each, ctx);
}

static uint64_t f_now(GsqlIo *io) { return gsql_io_now_ms(IOIN(io)); }
static void f_sleep(GsqlIo *io, uint64_t ms) { gsql_io_sleep_ms(IOIN(io), ms); }

static const GsqlIoOps fenced_ops = {
    f_open, f_read_at, f_write_at, f_sync, f_size, f_truncate, f_close,
    f_rename, f_remove, f_sync_dir, f_lock, f_free, f_random, f_mkdir, f_list, f_now, f_sleep
};

GsqlIo *gs_lease_io(GsLease *l) {
    if (!l->fio) {
        l->fio = calloc(1, sizeof *l->fio);
        if (!l->fio) return NULL;
        l->fio->base.ops = &fenced_ops;
        l->fio->in = l->io;
        l->fio->l = l;
    }
    return &l->fio->base;
}

/* ---- letting go ---------------------------------------------------------------- */

void gs_lease_close(GsLease *l, int clean) {
    if (!l) return;
    if (clean && !l->fenced) {
        /* a clean close leaves no lease, so the next open anywhere takes it at
         * once -- but only ours: a store that was taken over and has not
         * noticed must not remove the new owner's */
        Rec r = {0};
        unsigned char raw[LEASE_SIZE];
        int state = L_ABSENT;
        if (lease_read(l, &r, raw, &state) == 0 && state == L_READ && ours(l, &r)) {
            if (gsql_io_remove(l->io, l->path) == 0) {
                char *dir = gs_dirname(l->path);
                gsql_io_sync_dir(l->io, dir);
                free(dir);
            }
        }
    }
    free(l->fio);
    free(l->path);
    free(l);
}

void gs_lease_describe(GsqlIo *io, const char *dir, char *out, size_t n) {
    GsLease l;
    memset(&l, 0, sizeof l);
    GsqlIo *own = io ? NULL : gsql_io_real();
    l.io = io ? io : own;
    l.path = malloc(strlen(dir) + sizeof "/" GS_DB_BASE ".lease");
    if (!l.path) { snprintf(out, n, "another machine"); gsql_io_free(own); return; }
    sprintf(l.path, "%s/%s.lease", dir, GS_DB_BASE);
    Rec r = {0};
    unsigned char raw[LEASE_SIZE];
    int state = L_ABSENT;
    if (lease_read(&l, &r, raw, &state) == 0 && state == L_READ) describe(&r, out, n);
    else snprintf(out, n, "another machine (its lease could not be read)");
    free(l.path);
    gsql_io_free(own);
}

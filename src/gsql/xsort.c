/* The external sorter (docs/gsql_spill_design.md §3): entries held in a
 * bounded arena, sorted and written as runs to a private temporary
 * directory when it fills, then merged through a heap of the runs' heads,
 * at most XSORT_FAN runs at a time: past that, consecutive runs are merged
 * into longer ones first, in passes, so the files open at once and their
 * buffers stay bounded however large the sort
 * (docs/gsql_operator_spill_design.md §10). Keys compare by memcmp, which is
 * gBASIC's order for index keys. */

#define _POSIX_C_SOURCE 200809L
#define _CRT_RAND_S   /* rand_s, before stdlib.h: Windows' mkdtemp below */
#include "xsort.h"

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#endif

#define DEFAULT_BUDGET ((size_t)64 << 20)
#define RUN_BUFFER (64 * 1024)
/* runs open at once: 128 files and 8 MB of their buffers */
#define XSORT_FAN 128

typedef struct {
    size_t off;          /* into the arena: key bytes, then value bytes */
    uint32_t kn, vn;
    uint64_t seq;        /* insertion order, so equal keys keep it */
} Ent;

typedef struct {
    FILE *f;
    unsigned char *buf;  /* the current entry */
    size_t cap;
    uint32_t kn, vn;
    int live;
} Run;

struct GsXsort {
    char dir[512];
    int made_dir;
    size_t budget;
    unsigned char *arena;
    size_t used, cap;
    Ent *ents;
    size_t nents, entcap;
    uint64_t seq;
    unsigned nruns;
    /* each run's file number, in run order; the numbers made so far (a pass
       makes new ones); the passes merged before the last */
    unsigned *ids;
    unsigned capids, nextid, passes;
    unsigned written;    /* runs the arena wrote, for the caller's count */
    unsigned nopen;      /* runs open now */
    /* reading */
    int finished;
    size_t at;           /* in memory: the next entry */
    Run *runs;
    unsigned *heap;
    unsigned nheap;
};

static const char *temp_base(void) {
    const char *t = getenv("TMPDIR");
#if defined(_WIN32)
    /* the user's own temporary directory, which is on disk and private */
    if (!t || !*t) t = getenv("TEMP");
    if (!t || !*t) t = getenv("TMP");
    return t && *t ? t : ".";
#else
    /* not /tmp: it is RAM on many systems, and a spill there is no spill */
    return t && *t ? t : "/var/tmp";
#endif
}

#if defined(_WIN32)
/* mkdtemp, which the UCRT lacks: the trailing XXXXXX from rand_s, and the
 * directory made with _mkdir, which fails if the name is taken, so two
 * sorters can never share one. It inherits the temporary directory's ACL,
 * this user's alone in a profile. */
static char *gs_mkdtemp(char *tmpl) {
    size_t n = strlen(tmpl);
    if (n < 6 || strcmp(tmpl + n - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return NULL;
    }
    static const char digits[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int attempt = 0; attempt < 100; attempt++) {
        for (size_t i = n - 6; i < n; i++) {
            unsigned int v = 0;
            rand_s(&v);
            tmpl[i] = digits[v % (sizeof digits - 1)];
        }
        if (_mkdir(tmpl) == 0) {
            return tmpl;
        }
        if (errno != EEXIST) {
            return NULL;
        }
    }
    errno = EEXIST;
    return NULL;
}
#define mkdtemp gs_mkdtemp

/* POSIX's `kill(pid, 0) == 0 || errno != ESRCH`: the process is there, or it
 * is not ours to ask about */
static int gs_pid_present(long pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) {
        return GetLastError() != ERROR_INVALID_PARAMETER;
    }
    int alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}
#endif

int gs_xsort_open(const char *tmpdir, size_t budget, GsXsort **out) {
    GsXsort *s = calloc(1, sizeof *s);
    if (!s) {
        return ENOMEM;
    }
    s->budget = budget ? budget : DEFAULT_BUDGET;
    /* named for this process, so a sweep can tell a crashed one's (gs_xsort_sweep) */
    if (snprintf(s->dir, sizeof s->dir, "%s/gsql-sort-%ld-XXXXXX", tmpdir ? tmpdir : temp_base(), (long)getpid()) >=
        (int)sizeof s->dir) {
        free(s);
        return ENAMETOOLONG;
    }
    *out = s;
    return 0;
}

static int ent_cmp(const void *a, const void *b, const unsigned char *arena) {
    const Ent *x = a, *y = b;
    size_t n = x->kn < y->kn ? x->kn : y->kn;
    int c = n ? memcmp(arena + x->off, arena + y->off, n) : 0;
    if (c) {
        return c;
    }
    if (x->kn != y->kn) {
        return x->kn < y->kn ? -1 : 1;
    }
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* qsort has no context argument in C11; the arena is the sorter's own */
static const unsigned char *g_arena;
static int ent_cmp_q(const void *a, const void *b) { return ent_cmp(a, b, g_arena); }

static void sort_arena(GsXsort *s) {
    if (!s->nents) {
        return;
    }
    g_arena = s->arena;
    qsort(s->ents, s->nents, sizeof *s->ents, ent_cmp_q);
}

static int put32le(FILE *f, uint32_t v) {
    unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
    return fwrite(b, 1, 4, f) == 4 ? 0 : EIO;
}

static void run_name(const GsXsort *s, unsigned n, char *out, size_t cap) {
    snprintf(out, cap, "%s/run-%u", s->dir, n);
}

/* a new run's file number, appended to the runs */
static int run_new(GsXsort *s, unsigned *id) {
    if (s->nruns == s->capids) {
        unsigned c = s->capids ? s->capids * 2 : 64;
        unsigned *g = realloc(s->ids, c * sizeof *g);
        if (!g) {
            return ENOMEM;
        }
        s->ids = g;
        s->capids = c;
    }
    *id = s->nextid++;
    s->ids[s->nruns] = *id;
    return 0;
}

/* The arena, sorted, as the next run; the arena is empty after. */
static int spill(GsXsort *s) {
    if (!s->made_dir) {
        if (!mkdtemp(s->dir)) {
            return errno;
        }
        s->made_dir = 1;
    }
    sort_arena(s);
    unsigned id;
    int nr = run_new(s, &id);
    if (nr) {
        return nr;
    }
    char name[600];
    run_name(s, id, name, sizeof name);
    FILE *f = fopen(name, "wb");
    if (!f) {
        return errno;
    }
    int rc = 0;
    for (size_t i = 0; i < s->nents && !rc; i++) {
        const Ent *e = &s->ents[i];
        rc = put32le(f, e->kn);
        if (!rc) rc = put32le(f, e->vn);
        if (!rc && e->kn + e->vn && fwrite(s->arena + e->off, 1, (size_t)e->kn + e->vn, f) != (size_t)e->kn + e->vn) {
            rc = EIO;
        }
    }
    if (fclose(f) && !rc) {
        rc = EIO;
    }
    if (rc) {
        return rc;
    }
    s->nruns++;
    s->written++;
    s->used = 0;
    s->nents = 0;
    return 0;
}

int gs_xsort_add(GsXsort *s, const void *k, size_t kn, const void *v, size_t vn) {
    if (s->finished || kn > UINT32_MAX || vn > UINT32_MAX) {
        return EINVAL;
    }
    size_t need = kn + vn;
    if (s->nents && s->used + need + s->nents * sizeof(Ent) > s->budget) {
        int rc = spill(s);
        if (rc) {
            return rc;
        }
    }
    if (s->used + need > s->cap) {
        size_t c = s->cap ? s->cap : 4096;
        while (c < s->used + need) {
            c *= 2;
        }
        unsigned char *g = realloc(s->arena, c);
        if (!g) {
            return ENOMEM;
        }
        s->arena = g;
        s->cap = c;
    }
    if (s->nents == s->entcap) {
        size_t c = s->entcap ? s->entcap * 2 : 1024;
        Ent *g = realloc(s->ents, c * sizeof *g);
        if (!g) {
            return ENOMEM;
        }
        s->ents = g;
        s->entcap = c;
    }
    Ent *e = &s->ents[s->nents++];
    e->off = s->used;
    e->kn = (uint32_t)kn;
    e->vn = (uint32_t)vn;
    e->seq = s->seq++;
    if (kn) memcpy(s->arena + s->used, k, kn);
    if (vn) memcpy(s->arena + s->used + kn, v, vn);
    s->used += need;
    return 0;
}

static int get32le(FILE *f, uint32_t *v) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) {
        return 0;
    }
    *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 1;
}

static int run_step(Run *r) {
    if (!get32le(r->f, &r->kn) || !get32le(r->f, &r->vn)) {
        r->live = 0;
        return ferror(r->f) ? EIO : 0;
    }
    size_t need = (size_t)r->kn + r->vn;
    if (need > r->cap) {
        unsigned char *g = realloc(r->buf, need);
        if (!g) {
            return ENOMEM;
        }
        r->buf = g;
        r->cap = need;
    }
    if (need && fread(r->buf, 1, need, r->f) != need) {
        r->live = 0;
        return EIO;   /* a run we wrote ends inside an entry */
    }
    r->live = 1;
    return 0;
}

/* Runs compare by their current key, then by run number: runs hold
 * insertion order, earlier runs earlier entries, so ties stay stable. */
static int run_less(const GsXsort *s, unsigned a, unsigned b) {
    const Run *x = &s->runs[a], *y = &s->runs[b];
    size_t n = x->kn < y->kn ? x->kn : y->kn;
    int c = n ? memcmp(x->buf, y->buf, n) : 0;
    if (c) return c < 0;
    if (x->kn != y->kn) return x->kn < y->kn;
    return a < b;
}

static void sift_down(GsXsort *s, unsigned i) {
    for (;;) {
        unsigned l = 2 * i + 1, r = l + 1, m = i;
        if (l < s->nheap && run_less(s, s->heap[l], s->heap[m])) m = l;
        if (r < s->nheap && run_less(s, s->heap[r], s->heap[m])) m = r;
        if (m == i) return;
        unsigned t = s->heap[i];
        s->heap[i] = s->heap[m];
        s->heap[m] = t;
        i = m;
    }
}

/* Runs `ids[0..n)` opened, each at its first entry, as the heap */
static int open_runs(GsXsort *s, const unsigned *ids, unsigned n) {
    s->runs = calloc(n ? n : 1, sizeof *s->runs);
    s->heap = calloc(n ? n : 1, sizeof *s->heap);
    s->nheap = 0;
    s->nopen = n;
    if (!s->runs || !s->heap) {
        return ENOMEM;
    }
    for (unsigned i = 0; i < n; i++) {
        char name[600];
        run_name(s, ids[i], name, sizeof name);
        Run *r = &s->runs[i];
        if (!(r->f = fopen(name, "rb"))) {
            return errno;
        }
        setvbuf(r->f, NULL, _IOFBF, RUN_BUFFER);
        int rc = run_step(r);
        if (rc) {
            return rc;
        }
        if (r->live) {
            s->heap[s->nheap++] = i;
        }
    }
    for (unsigned i = s->nheap / 2; i-- > 0;) {
        sift_down(s, i);
    }
    s->at = 0;   /* the heap's top is not yet handed out */
    return 0;
}

/* the runs open_runs opened, closed */
static void close_runs(GsXsort *s) {
    for (unsigned i = 0; s->runs && i < s->nopen; i++) {
        if (s->runs[i].f) {
            fclose(s->runs[i].f);
        }
        free(s->runs[i].buf);
    }
    free(s->runs);
    free(s->heap);
    s->runs = NULL;
    s->heap = NULL;
    s->nheap = 0;
    s->nopen = 0;
}

/* One pass: each XSORT_FAN consecutive runs merged into one, in order, so a
 * tie still goes to the earlier run and the sort stays stable; the merged
 * runs' files removed. */
static int merge_pass(GsXsort *s) {
    unsigned n = s->nruns;
    unsigned *old = s->ids;
    s->ids = NULL;
    s->capids = 0;
    s->nruns = 0;
    int rc = 0;
    for (unsigned first = 0; first < n && !rc; first += XSORT_FAN) {
        unsigned m = n - first < XSORT_FAN ? n - first : XSORT_FAN;
        unsigned id = 0;
        rc = open_runs(s, old + first, m);
        if (!rc) {
            rc = run_new(s, &id);
        }
        FILE *f = NULL;
        if (!rc) {
            char name[600];
            run_name(s, id, name, sizeof name);
            if (!(f = fopen(name, "wb"))) {
                rc = errno;
            } else {
                setvbuf(f, NULL, _IOFBF, RUN_BUFFER);
                s->nruns++;
            }
        }
        while (!rc && s->nheap) {
            Run *r = &s->runs[s->heap[0]];
            rc = put32le(f, r->kn);
            if (!rc) rc = put32le(f, r->vn);
            if (!rc && r->kn + r->vn && fwrite(r->buf, 1, (size_t)r->kn + r->vn, f) != (size_t)r->kn + r->vn) {
                rc = EIO;
            }
            if (!rc) rc = run_step(r);
            if (!rc && !r->live) {
                s->heap[0] = s->heap[--s->nheap];
            }
            if (!rc && s->nheap) {
                sift_down(s, 0);
            }
        }
        if (f && fclose(f) && !rc) {
            rc = EIO;
        }
        close_runs(s);
        for (unsigned i = 0; i < m; i++) {
            char name[600];
            run_name(s, old[first + i], name, sizeof name);
            unlink(name);
        }
    }
    free(old);
    s->passes++;
    return rc;
}

int gs_xsort_finish(GsXsort *s) {
    if (s->finished) {
        return EINVAL;
    }
    s->finished = 1;
    if (!s->nruns) {
        sort_arena(s);   /* everything fit: read the arena in order */
        return 0;
    }
    int rc = s->nents ? spill(s) : 0;
    if (rc) {
        return rc;
    }
    free(s->arena);
    s->arena = NULL;
    free(s->ents);
    s->ents = NULL;
    while (s->nruns > XSORT_FAN) {
        if ((rc = merge_pass(s))) {
            return rc;
        }
    }
    return open_runs(s, s->ids, s->nruns);
}

int gs_xsort_next(GsXsort *s, GsBytes *k, GsBytes *v, int *have) {
    *have = 0;
    if (!s->finished) {
        return EINVAL;
    }
    if (!s->nruns) {
        if (s->at >= s->nents) {
            return 0;
        }
        const Ent *e = &s->ents[s->at++];
        k->p = s->arena + e->off;
        k->n = e->kn;
        v->p = s->arena + e->off + e->kn;
        v->n = e->vn;
        *have = 1;
        return 0;
    }
    /* the entry handed out last time is still at the top: step past it */
    if (s->at) {
        Run *r = &s->runs[s->heap[0]];
        int rc = run_step(r);
        if (rc) {
            return rc;
        }
        if (!r->live) {
            s->heap[0] = s->heap[--s->nheap];
        }
        if (s->nheap) {
            sift_down(s, 0);
        }
    }
    s->at = 1;
    if (!s->nheap) {
        return 0;
    }
    Run *r = &s->runs[s->heap[0]];
    k->p = r->buf;
    k->n = r->kn;
    v->p = r->buf + r->kn;
    v->n = r->vn;
    *have = 1;
    return 0;
}

unsigned gs_xsort_runs(const GsXsort *s) { return s->written; }

unsigned gs_xsort_passes(const GsXsort *s) { return s->passes; }

void gs_xsort_close(GsXsort *s) {
    if (!s) {
        return;
    }
    close_runs(s);
    if (s->made_dir) {
        for (unsigned i = 0; i < s->nextid; i++) {
            char name[600];
            run_name(s, i, name, sizeof name);
            unlink(name);   /* every file a run or a pass made, whatever became of it */
        }
        rmdir(s->dir);
    }
    free(s->ids);
    free(s->heap);
    free(s->arena);
    free(s->ents);
    free(s);
}

/* ---- the row spool ------------------------------------------------------- */

struct GsSpool {
    char dir[512];
    char path[600];
    FILE *f;
    uint64_t end;        /* where the next entry is written */
};

int gs_spool_open(const char *tmpdir, GsSpool **out) {
    GsSpool *s = calloc(1, sizeof *s);
    if (!s) {
        return ENOMEM;
    }
    if (snprintf(s->dir, sizeof s->dir, "%s/gsql-spool-%ld-XXXXXX", tmpdir ? tmpdir : temp_base(), (long)getpid()) >=
        (int)sizeof s->dir) {
        free(s);
        return ENAMETOOLONG;
    }
    if (!mkdtemp(s->dir)) {
        int e = errno;
        free(s);
        return e;
    }
    snprintf(s->path, sizeof s->path, "%s/rows", s->dir);
    if (!(s->f = fopen(s->path, "w+b"))) {
        int e = errno;
        rmdir(s->dir);
        free(s);
        return e;
    }
    *out = s;
    return 0;
}

int gs_spool_add(GsSpool *s, const void *p, size_t n, uint64_t *offset) {
    unsigned char len[8];
    for (int k = 0; k < 8; k++) {
        len[k] = (unsigned char)((uint64_t)n >> (8 * k));
    }
    /* a write after a read needs a seek between them (C11 7.21.5.3) */
    if (fseeko(s->f, (off_t)s->end, SEEK_SET) || fwrite(len, 1, 8, s->f) != 8 || (n && fwrite(p, 1, n, s->f) != n)) {
        return EIO;
    }
    *offset = s->end;
    s->end += 8 + n;
    return 0;
}

int gs_spool_get(GsSpool *s, uint64_t offset, unsigned char **buf, size_t *cap, size_t *n) {
    unsigned char len[8];
    if (fseeko(s->f, (off_t)offset, SEEK_SET) || fread(len, 1, 8, s->f) != 8) {
        return EIO;
    }
    uint64_t m = 0;
    for (int k = 0; k < 8; k++) {
        m |= (uint64_t)len[k] << (8 * k);
    }
    if (m > s->end - offset - 8) {
        return EIO;
    }
    if (m > *cap) {
        unsigned char *g = realloc(*buf, m ? m : 1);
        if (!g) {
            return ENOMEM;
        }
        *buf = g;
        *cap = m;
    }
    if (m && fread(*buf, 1, m, s->f) != m) {
        return EIO;
    }
    *n = m;
    return 0;
}

uint64_t gs_spool_size(const GsSpool *s) { return s->end; }

void gs_spool_close(GsSpool *s) {
    if (!s) {
        return;
    }
    if (s->f) {
        fclose(s->f);
    }
    remove(s->path);
    rmdir(s->dir);
    free(s);
}

/* ---- leftovers of a crash ------------------------------------------------- */

/* the process a sort or spool directory's name says made it, or 0 */
static long dir_pid(const char *name) {
    const char *p = NULL;
    if (strncmp(name, "gsql-sort-", 10) == 0) {
        p = name + 10;
    } else if (strncmp(name, "gsql-spool-", 11) == 0) {
        p = name + 11;
    }
    if (!p || *p < '0' || *p > '9') {
        return 0;
    }
    long pid = 0;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (*p++ - '0');
        if (pid > 1L << 30) {
            return 0;
        }
    }
    return *p == '-' ? pid : 0;
}

unsigned gs_xsort_sweep(const char *tmpdir) {
    const char *base = tmpdir ? tmpdir : temp_base();
    DIR *d = opendir(base);
    if (!d) {
        return 0;
    }
    unsigned removed = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        long pid = dir_pid(e->d_name);
        /* a process still there, or one not ours to ask (EPERM), keeps it */
#if defined(_WIN32)
        if (pid <= 0 || pid == (long)getpid() || gs_pid_present(pid)) {
#else
        if (pid <= 0 || pid == (long)getpid() || kill((pid_t)pid, 0) == 0 || errno != ESRCH) {
#endif
            continue;
        }
        char path[1024];
        if (snprintf(path, sizeof path, "%s/%s", base, e->d_name) >= (int)sizeof path) {
            continue;
        }
        /* a directory of ours, not a link to one */
#if defined(_WIN32)
        DWORD attr = GetFileAttributesA(path);
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY) ||
            (attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
            continue;   /* in the user's own temporary directory, so ours */
        }
#else
        struct stat st;
        if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) {
            continue;
        }
#endif
        DIR *in = opendir(path);
        if (!in) {
            continue;
        }
        struct dirent *f;
        while ((f = readdir(in))) {
            if (strcmp(f->d_name, ".") == 0 || strcmp(f->d_name, "..") == 0) {
                continue;
            }
            char file[1300];
            if (snprintf(file, sizeof file, "%s/%s", path, f->d_name) < (int)sizeof file) {
                unlink(file);   /* a run or the rows: the sorter and the spool make files only */
            }
        }
        closedir(in);
        if (rmdir(path) == 0) {
            removed++;
        }
    }
    closedir(d);
    return removed;
}


/* G-SQL's SIMULATED I/O layer (docs/gsql_storage_design.md §3.4, §5.3): files
 * in memory, held as a DURABLE image plus the PENDING operations since each was
 * made durable, so the crash harness can stop the world at any operation and
 * ask what a crash there would leave on the disk.
 *
 * THE RISK IS THAT IT DIVERGES FROM A REAL DISK, which would make the harness
 * test a disk that does not exist. tests/run_gsql_io.sh therefore holds it to
 * the five rules in include/gsql_io.h -- each as a DIFFERENCE, since a layer
 * that always kept or always dropped a pending write passes any one-sided
 * check -- and runs the same seeded operation sequence through this layer and
 * the real one, requiring every answer and every byte to agree.
 *
 * Two places where it is deliberately HARSHER than a real filesystem, both
 * stated in §3.4: pending namespace operations may be lost as any subsequence
 * (a journalling filesystem loses a suffix), and pending data writes in any
 * subset and order. An engine correct here is correct there; the converse is
 * the failure this layer exists to prevent. */

#include "io_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *bytes;
    size_t len;
} Buf;

typedef struct {
    Buf durable;
    Buf view;            /* durable + every pending op on this inode, in order */
    struct SimFile *lock_holder;
} Inode;

typedef struct {
    char *path;
    size_t inode;          /* DIR_INODE for a directory made by mkdir */
} Name;

/* A directory MADE by mkdir is a name like a file's, created and made durable
 * the same way (rule 2, through its parent). A directory nobody made -- the
 * part of a path before its last '/' -- is implicit and always there, which is
 * how every existing caller uses this layer. */
#define DIR_INODE ((size_t)-1)

typedef struct {
    Name *items;
    size_t count, cap;
} Names;

typedef enum { P_WRITE, P_TRUNC, P_CREATE, P_RENAME, P_REMOVE } PendKind;

typedef struct {
    PendKind kind;
    size_t inode;          /* WRITE, TRUNC, CREATE */
    uint64_t offset;       /* WRITE; the new length for TRUNC */
    unsigned char *bytes;  /* WRITE */
    size_t n;              /* WRITE */
    char *path;            /* CREATE, REMOVE; RENAME's source */
    char *path2;           /* RENAME's target */
} Pend;

typedef struct {
    unsigned char *v;
    size_t n, cap;
} U8s;

enum { K_OPEN, K_READ, K_WRITE, K_SYNC, K_SIZE, K_TRUNC, K_CLOSE, K_RENAME, K_REMOVE, K_SYNC_DIR, K_LOCK, K_MKDIR, K_LIST };

typedef struct {
    GsqlIo base;
    Inode **inodes;
    size_t inode_count, inode_cap;
    Names durable_names, view_names;
    Names made;            /* every directory mkdir made, so a crash image can drop what was beneath a lost one */
    Pend *pend;
    size_t pend_count, pend_cap;
    uint64_t ops, crash_at;
    int crashed;
    U8s kinds;             /* the kind of every numbered operation, for the harness */
    unsigned fail_in[4];   /* indexed by GsqlIoFault: countdown, 0 = disarmed */
    uint64_t seed;
    uint64_t rng;          /* what gsql_io_random draws from */
} Sim;

typedef struct SimFile {
    GsqlFile base;
    size_t inode;
    int read_only;
} SimFile;

/* ---- small helpers ------------------------------------------------------ */

static uint64_t splitmix(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/* The directory of a path: the text before its last '/', or "" for none. */
static size_t dir_len(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? (size_t)(slash - path) : 0;
}

static int same_dir(const char *a, const char *b) {
    size_t la = dir_len(a), lb = dir_len(b);
    return la == lb && memcmp(a, b, la) == 0;
}

static int in_dir(const char *path, const char *dir) {
    size_t l = dir_len(path);
    size_t dl = strlen(dir);
    /* sync_dir("d/") and sync_dir("d") name the same directory */
    while (dl > 0 && dir[dl - 1] == '/') dl--;
    return l == dl && memcmp(path, dir, l) == 0;
}

static int buf_reserve(Buf *b, size_t n) {
    if (n <= b->len) return 1;
    unsigned char *p = realloc(b->bytes, n ? n : 1);
    if (!p) return 0;
    memset(p + b->len, 0, n - b->len);
    b->bytes = p;
    return 1;
}

static int buf_write(Buf *b, uint64_t offset, const unsigned char *src, size_t n) {
    if (n == 0) return 1;
    size_t end = (size_t)offset + n;
    if (end > b->len) {
        if (!buf_reserve(b, end)) return 0;
        b->len = end;
    }
    memcpy(b->bytes + offset, src, n);
    return 1;
}

static int buf_truncate(Buf *b, uint64_t n) {
    if (n > b->len) {
        if (!buf_reserve(b, (size_t)n)) return 0;
    }
    b->len = (size_t)n;
    return 1;
}

static int buf_copy(Buf *dst, const Buf *src) {
    dst->bytes = malloc(src->len ? src->len : 1);
    if (!dst->bytes) return 0;
    if (src->len) memcpy(dst->bytes, src->bytes, src->len);
    dst->len = src->len;
    return 1;
}

static long names_find(const Names *ns, const char *path) {
    for (size_t i = 0; i < ns->count; i++)
        if (strcmp(ns->items[i].path, path) == 0) return (long)i;
    return -1;
}

/* Bind `path` to `inode`, replacing whatever it named (rename's semantics). */
static int names_set(Names *ns, const char *path, size_t inode) {
    long at = names_find(ns, path);
    if (at >= 0) {
        ns->items[at].inode = inode;
        return 1;
    }
    if (ns->count == ns->cap) {
        size_t cap = ns->cap ? ns->cap * 2 : 8;
        Name *p = realloc(ns->items, cap * sizeof *p);
        if (!p) return 0;
        ns->items = p;
        ns->cap = cap;
    }
    ns->items[ns->count].path = dup_str(path);
    if (!ns->items[ns->count].path) return 0;
    ns->items[ns->count].inode = inode;
    ns->count++;
    return 1;
}

static void names_del(Names *ns, const char *path) {
    long at = names_find(ns, path);
    if (at < 0) return;
    free(ns->items[at].path);
    ns->items[at] = ns->items[--ns->count];
}

static void names_free(Names *ns) {
    for (size_t i = 0; i < ns->count; i++) free(ns->items[i].path);
    free(ns->items);
    ns->items = NULL;
    ns->count = ns->cap = 0;
}

static int names_copy(Names *dst, const Names *src) {
    memset(dst, 0, sizeof *dst);
    for (size_t i = 0; i < src->count; i++)
        if (!names_set(dst, src->items[i].path, src->items[i].inode)) return 0;
    return 1;
}

static long new_inode(Sim *s) {
    if (s->inode_count == s->inode_cap) {
        size_t cap = s->inode_cap ? s->inode_cap * 2 : 8;
        Inode **p = realloc(s->inodes, cap * sizeof *p);
        if (!p) return -1;
        s->inodes = p;
        s->inode_cap = cap;
    }
    Inode *in = calloc(1, sizeof *in);
    if (!in) return -1;
    s->inodes[s->inode_count] = in;
    return (long)s->inode_count++;
}

static Pend *pend_push(Sim *s, PendKind kind) {
    if (s->pend_count == s->pend_cap) {
        size_t cap = s->pend_cap ? s->pend_cap * 2 : 16;
        Pend *p = realloc(s->pend, cap * sizeof *p);
        if (!p) return NULL;
        s->pend = p;
        s->pend_cap = cap;
    }
    Pend *p = &s->pend[s->pend_count++];
    memset(p, 0, sizeof *p);
    p->kind = kind;
    return p;
}

static void pend_clear(Pend *p) {
    free(p->bytes);
    free(p->path);
    free(p->path2);
}

static int is_data(const Pend *p) { return p->kind == P_WRITE || p->kind == P_TRUNC; }

static const char *ns_dir_path(const Pend *p) {
    return p->kind == P_RENAME ? p->path2 : p->path;
}

/* Apply one data op to a buffer; for a torn WRITE, only its first `sectors`
 * sectors (counted at absolute 512-byte boundaries). (size_t)-1 = all. */
static int apply_data(Buf *b, const Pend *p, size_t sectors) {
    if (p->kind == P_TRUNC) return buf_truncate(b, p->offset);
    if (sectors == (size_t)-1) return buf_write(b, p->offset, p->bytes, p->n);
    uint64_t pos = p->offset, end = p->offset + p->n;
    for (size_t k = 0; k < sectors && pos < end; k++) {
        uint64_t boundary = (pos / GSQL_IO_SECTOR + 1) * GSQL_IO_SECTOR;
        uint64_t stop = boundary < end ? boundary : end;
        if (!buf_write(b, pos, p->bytes + (pos - p->offset), (size_t)(stop - pos))) return 0;
        pos = stop;
    }
    return 1;
}

static size_t sector_count(const Pend *p) {
    if (p->kind != P_WRITE || p->n == 0) return 0;
    uint64_t first = p->offset / GSQL_IO_SECTOR;
    uint64_t last = (p->offset + p->n - 1) / GSQL_IO_SECTOR;
    return (size_t)(last - first + 1);
}

static int apply_name(Names *ns, const Pend *p) {
    switch (p->kind) {
    case P_CREATE:
        /* A creation that a lost removal never undid is still a creation. */
        return names_find(ns, p->path) >= 0 ? 1 : names_set(ns, p->path, p->inode);
    case P_RENAME: {
        long at = names_find(ns, p->path);
        /* The source never reached this disk: the rename did nothing here. */
        if (at < 0) return 1;
        size_t inode = ns->items[at].inode;
        names_del(ns, p->path);
        return names_set(ns, p->path2, inode);
    }
    case P_REMOVE:
        names_del(ns, p->path);
        return 1;
    default:
        return 1;
    }
}

/* Every operation passes through here first: it is numbered, and from the
 * crash point on nothing happens. */
static int tick(Sim *s, unsigned kind) {
    s->ops++;
    if (s->kinds.n == s->kinds.cap) {
        size_t cap = s->kinds.cap ? s->kinds.cap * 2 : 1024;
        unsigned char *v = realloc(s->kinds.v, cap);
        if (v) { s->kinds.v = v; s->kinds.cap = cap; }
    }
    if (s->kinds.n < s->kinds.cap) s->kinds.v[s->kinds.n++] = (unsigned char)kind;
    if (s->crashed) return GSQL_IO_CRASHED;
    if (s->crash_at && s->ops >= s->crash_at) {
        s->crashed = 1;
        return GSQL_IO_CRASHED;
    }
    return 0;
}

static int fault_fires(Sim *s, GsqlIoFault f) {
    if (!s->fail_in[f]) return 0;
    return --s->fail_in[f] == 0;
}

/* ---- the operations ----------------------------------------------------- */

static int sim_open(GsqlIo *io, const char *path, int flags, GsqlFile **out) {
    Sim *s = (Sim *)io;
    int rc = tick(s, K_OPEN);
    if (rc) return rc;
    long at = names_find(&s->view_names, path);
    size_t inode;
    if (at >= 0 && s->view_names.items[at].inode == DIR_INODE) return EISDIR;
    if (at < 0) {
        if (!(flags & GSQL_IO_CREATE)) return ENOENT;
        long made = new_inode(s);
        if (made < 0) return ENOMEM;
        inode = (size_t)made;
        Pend *p = pend_push(s, P_CREATE);
        if (!p) return ENOMEM;
        p->inode = inode;
        p->path = dup_str(path);
        if (!p->path || !names_set(&s->view_names, path, inode)) return ENOMEM;
    } else {
        if ((flags & GSQL_IO_CREATE) && (flags & GSQL_IO_EXCLUSIVE)) return EEXIST;
        inode = s->view_names.items[at].inode;
    }
    SimFile *f = calloc(1, sizeof *f);
    if (!f) return ENOMEM;
    f->base.io = io;
    f->inode = inode;
    f->read_only = (flags & GSQL_IO_READ_ONLY) != 0;
    *out = &f->base;
    return 0;
}

static int sim_read_at(GsqlFile *base, uint64_t offset, void *buf, size_t n, size_t *got) {
    SimFile *f = (SimFile *)base;
    Sim *s = (Sim *)base->io;
    int rc = tick(s, K_READ);
    if (rc) return rc;
    if (fault_fires(s, GSQL_IO_FAIL_READ)) return EIO;
    const Buf *b = &s->inodes[f->inode]->view;
    if (offset >= b->len) return 0;
    size_t avail = b->len - (size_t)offset;
    size_t take = n < avail ? n : avail;
    memcpy(buf, b->bytes + offset, take);
    *got = take;
    return 0;
}

static int sim_write_at(GsqlFile *base, uint64_t offset, const void *buf, size_t n) {
    SimFile *f = (SimFile *)base;
    Sim *s = (Sim *)base->io;
    int rc = tick(s, K_WRITE);
    if (rc) return rc;
    if (f->read_only) return EBADF;
    if (fault_fires(s, GSQL_IO_FAIL_WRITE)) return ENOSPC;
    if (n == 0) return 0;
    Pend *p = pend_push(s, P_WRITE);
    if (!p) return ENOMEM;
    p->inode = f->inode;
    p->offset = offset;
    p->n = n;
    p->bytes = malloc(n);
    if (!p->bytes) return ENOMEM;
    memcpy(p->bytes, buf, n);
    return buf_write(&s->inodes[f->inode]->view, offset, buf, n) ? 0 : ENOMEM;
}

/* Remove pending entry i, keeping the order of the rest. */
static void pend_remove(Sim *s, size_t i) {
    pend_clear(&s->pend[i]);
    memmove(&s->pend[i], &s->pend[i + 1], (s->pend_count - i - 1) * sizeof *s->pend);
    s->pend_count--;
}

static int sim_sync(GsqlFile *base) {
    SimFile *f = (SimFile *)base;
    Sim *s = (Sim *)base->io;
    int rc = tick(s, K_SYNC);
    if (rc) return rc;
    int failed = fault_fires(s, GSQL_IO_FAIL_SYNC);
    Inode *in = s->inodes[f->inode];
    for (size_t i = 0; i < s->pend_count;) {
        Pend *p = &s->pend[i];
        if (!is_data(p) || p->inode != f->inode) {
            i++;
            continue;
        }
        /* Rule 5: after a failed flush each write either reached the disk or
         * was lost, and NEITHER is retried -- it leaves the pending list
         * either way, so a later sync that succeeds does not make a lost
         * write durable. The process's view keeps it (fsyncgate). */
        if (!failed || (splitmix(&s->seed) & 1))
            if (!apply_data(&in->durable, p, (size_t)-1)) return ENOMEM;
        pend_remove(s, i);
    }
    return failed ? EIO : 0;
}

static int sim_size(GsqlFile *base, uint64_t *out) {
    SimFile *f = (SimFile *)base;
    Sim *s = (Sim *)base->io;
    int rc = tick(s, K_SIZE);
    if (rc) return rc;
    *out = s->inodes[f->inode]->view.len;
    return 0;
}

static int sim_truncate(GsqlFile *base, uint64_t n) {
    SimFile *f = (SimFile *)base;
    Sim *s = (Sim *)base->io;
    int rc = tick(s, K_TRUNC);
    if (rc) return rc;
    if (f->read_only) return EBADF;
    Pend *p = pend_push(s, P_TRUNC);
    if (!p) return ENOMEM;
    p->inode = f->inode;
    p->offset = n;
    return buf_truncate(&s->inodes[f->inode]->view, n) ? 0 : ENOMEM;
}

static int sim_close(GsqlFile *base) {
    SimFile *f = (SimFile *)base;
    Sim *s = (Sim *)base->io;
    int rc = tick(s, K_CLOSE);
    if (s->inodes[f->inode]->lock_holder == f) s->inodes[f->inode]->lock_holder = NULL;
    free(f);
    return rc;
}

static int sim_rename(GsqlIo *io, const char *from, const char *to) {
    Sim *s = (Sim *)io;
    int rc = tick(s, K_RENAME);
    if (rc) return rc;
    /* Durability of a cross-directory rename needs both directories synced, a
     * case no candidate needs; refused rather than modelled loosely. */
    if (!same_dir(from, to)) return EXDEV;
    long at = names_find(&s->view_names, from);
    if (at < 0) return ENOENT;
    if (s->view_names.items[at].inode == DIR_INODE) return EISDIR; /* not modelled: no caller renames one */
    if (strcmp(from, to) == 0) return 0;
    size_t inode = s->view_names.items[at].inode;
    Pend *p = pend_push(s, P_RENAME);
    if (!p) return ENOMEM;
    p->path = dup_str(from);
    p->path2 = dup_str(to);
    if (!p->path || !p->path2) return ENOMEM;
    names_del(&s->view_names, from);
    return names_set(&s->view_names, to, inode) ? 0 : ENOMEM;
}

static int sim_remove(GsqlIo *io, const char *path) {
    Sim *s = (Sim *)io;
    int rc = tick(s, K_REMOVE);
    if (rc) return rc;
    long at = names_find(&s->view_names, path);
    if (at < 0) return ENOENT;
    if (s->view_names.items[at].inode == DIR_INODE) return EISDIR; /* unlink's answer */
    Pend *p = pend_push(s, P_REMOVE);
    if (!p) return ENOMEM;
    p->path = dup_str(path);
    if (!p->path) return ENOMEM;
    names_del(&s->view_names, path);
    return 0;
}

static int sim_sync_dir(GsqlIo *io, const char *dir) {
    Sim *s = (Sim *)io;
    int rc = tick(s, K_SYNC_DIR);
    if (rc) return rc;
    for (size_t i = 0; i < s->pend_count;) {
        Pend *p = &s->pend[i];
        if (is_data(p) || !in_dir(ns_dir_path(p), dir)) {
            i++;
            continue;
        }
        if (!apply_name(&s->durable_names, p)) return ENOMEM;
        pend_remove(s, i);
    }
    return 0;
}

static int sim_lock(GsqlFile *base, int nonblocking) {
    SimFile *f = (SimFile *)base;
    Sim *s = (Sim *)base->io;
    int rc = tick(s, K_LOCK);
    if (rc) return rc;
    Inode *in = s->inodes[f->inode];
    if (in->lock_holder && in->lock_holder != f)
        /* One thread: a blocking wait for ourselves would never end. */
        return nonblocking ? EWOULDBLOCK : EDEADLK;
    in->lock_holder = f;
    return 0;
}

/* Whether anything in the current view lies beneath `dir` (its length dl). */
static int has_beneath(const Sim *s, const char *dir, size_t dl) {
    for (size_t i = 0; i < s->view_names.count; i++) {
        const char *p = s->view_names.items[i].path;
        if (strncmp(p, dir, dl) == 0 && p[dl] == '/') return 1;
    }
    return 0;
}

static int sim_mkdir(GsqlIo *io, const char *path) {
    Sim *s = (Sim *)io;
    int rc = tick(s, K_MKDIR);
    if (rc) return rc;
    if (names_find(&s->view_names, path) >= 0 || has_beneath(s, path, strlen(path))) return EEXIST;
    Pend *p = pend_push(s, P_CREATE);
    if (!p) return ENOMEM;
    p->inode = DIR_INODE;
    p->path = dup_str(path);
    if (!p->path || !names_set(&s->view_names, path, DIR_INODE) || !names_set(&s->made, path, 0)) return ENOMEM;
    return 0;
}

static int sim_list(GsqlIo *io, const char *dir, int (*each)(void *, const char *), void *ctx) {
    Sim *s = (Sim *)io;
    int rc = tick(s, K_LIST);
    if (rc) return rc;
    size_t dl = strlen(dir);
    while (dl > 0 && dir[dl - 1] == '/') dl--;
    long self = -1;
    for (size_t i = 0; i < s->view_names.count; i++)
        if (strlen(s->view_names.items[i].path) == dl && strncmp(s->view_names.items[i].path, dir, dl) == 0) self = (long)i;
    if (self >= 0 && s->view_names.items[self].inode != DIR_INODE) return ENOTDIR;
    char **names = NULL;
    size_t n = 0, cap = 0;
    for (size_t i = 0; i < s->view_names.count; i++) {
        const char *p = s->view_names.items[i].path;
        if (dl ? (strncmp(p, dir, dl) != 0 || p[dl] != '/') : 0) continue;
        const char *rest = dl ? p + dl + 1 : p;
        const char *slash = strchr(rest, '/');
        size_t len = slash ? (size_t)(slash - rest) : strlen(rest);
        /* an implicit subdirectory appears once, as a real one would */
        int dup = 0;
        for (size_t j = 0; j < n && !dup; j++) dup = strlen(names[j]) == len && strncmp(names[j], rest, len) == 0;
        if (dup || !len) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            char **q = realloc(names, cap * sizeof *q);
            if (!q) { rc = ENOMEM; break; }
            names = q;
        }
        if (!(names[n] = malloc(len + 1))) { rc = ENOMEM; break; }
        memcpy(names[n], rest, len);
        names[n][len] = 0;
        n++;
    }
    if (!rc && !n && self < 0 && dl) rc = ENOENT;
    if (rc) {
        for (size_t i = 0; i < n; i++) free(names[i]);
        free(names);
        return rc;
    }
    return gsql_io_walk_names(names, n, each, ctx);
}

static void sim_free(GsqlIo *io) {
    Sim *s = (Sim *)io;
    for (size_t i = 0; i < s->inode_count; i++) {
        free(s->inodes[i]->durable.bytes);
        free(s->inodes[i]->view.bytes);
        free(s->inodes[i]);
    }
    free(s->inodes);
    names_free(&s->durable_names);
    names_free(&s->view_names);
    names_free(&s->made);
    for (size_t i = 0; i < s->pend_count; i++) pend_clear(&s->pend[i]);
    free(s->pend);
    free(s->kinds.v);
    free(s);
}

static void sim_random(GsqlIo *io, void *out, size_t n) {
    Sim *s = (Sim *)io;
    for (size_t i = 0; i < n; i += 8) {
        uint64_t r = splitmix(&s->rng);
        for (size_t j = 0; j < 8 && i + j < n; j++) ((unsigned char *)out)[i + j] = (unsigned char)(r >> (8 * j));
    }
}

static const GsqlIoOps sim_ops = {
    sim_open, sim_read_at, sim_write_at, sim_sync, sim_size, sim_truncate,
    sim_close, sim_rename, sim_remove, sim_sync_dir, sim_lock, sim_free, sim_random,
    sim_mkdir, sim_list
};

GsqlIo *gsql_io_sim(void) {
    Sim *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->base.ops = &sim_ops;
    s->seed = 1;
    s->rng = 0x6a09e667f3bcc908ULL;
    return &s->base;
}

void gsql_io_sim_crash_at(GsqlIo *io, uint64_t n) { ((Sim *)io)->crash_at = n; }
uint64_t gsql_io_sim_ops(const GsqlIo *io) { return ((const Sim *)io)->ops; }
int gsql_io_sim_crashed(const GsqlIo *io) { return ((const Sim *)io)->crashed; }

size_t gsql_io_sim_flush_points(const GsqlIo *io, uint64_t *out, size_t max) {
    const Sim *s = (const Sim *)io;
    size_t n = 0;
    for (size_t i = 0; i < s->kinds.n; i++)
        if (s->kinds.v[i] == K_SYNC || s->kinds.v[i] == K_SYNC_DIR) {
            if (n < max) out[n] = (uint64_t)i + 1;
            n++;
        }
    return n;
}
void gsql_io_sim_seed(GsqlIo *io, uint64_t seed) { ((Sim *)io)->seed = seed; }

void gsql_io_sim_fail(GsqlIo *io, GsqlIoFault fault, unsigned nth) {
    if (fault >= GSQL_IO_FAIL_SYNC && fault <= GSQL_IO_FAIL_READ)
        ((Sim *)io)->fail_in[fault] = nth;
}

int gsql_io_sim_fault_armed(const GsqlIo *io, GsqlIoFault fault) {
    if (fault < GSQL_IO_FAIL_SYNC || fault > GSQL_IO_FAIL_READ) return 0;
    return ((const Sim *)io)->fail_in[fault] != 0;
}

/* ---- the disk a crash leaves ------------------------------------------- */

GsqlIo *gsql_io_sim_crash_image(const GsqlIo *io, GsqlCrashMode mode, uint64_t seed) {
    const Sim *s = (const Sim *)io;
    Sim *t = (Sim *)gsql_io_sim();
    if (!t) return NULL;
    uint64_t rng = seed ^ 0xA0761D6478BD642FULL;
    int ok = 1;
    /* its own draws: never a replay of the parent's */
    t->rng = s->rng ^ (seed * 0x9E3779B97F4A7C15ULL) ^ 0xbb67ae8584caa73bULL;
    t->rng ^= (uint64_t)mode << 56;

    /* The same inode numbering, so names keep meaning the same files. */
    for (size_t i = 0; ok && i < s->inode_count; i++) {
        long made = new_inode(t);
        if (made < 0) { ok = 0; break; }
        const Inode *src = s->inodes[i];
        ok = buf_copy(&t->inodes[made]->durable,
                      mode == GSQL_CRASH_PROCESS ? &src->view : &src->durable);
    }
    if (ok) ok = names_copy(&t->durable_names,
                            mode == GSQL_CRASH_PROCESS ? &s->view_names : &s->durable_names);

    if (ok && mode != GSQL_CRASH_PROCESS) {
        /* Choose the survivors: each pending op independently. */
        size_t *data = malloc((s->pend_count + 1) * sizeof *data);
        size_t nd = 0;
        if (!data) ok = 0;
        for (size_t i = 0; ok && i < s->pend_count; i++) {
            const Pend *p = &s->pend[i];
            if (!(splitmix(&rng) & 1)) continue;
            if (is_data(p)) data[nd++] = i;
            else ok = apply_name(&t->durable_names, p); /* subsequence, in order */
        }
        /* Rule 4: the survivors reach the disk in any order. */
        for (size_t i = nd; ok && i > 1; i--) {
            size_t j = (size_t)(splitmix(&rng) % i);
            size_t tmp = data[i - 1]; data[i - 1] = data[j]; data[j] = tmp;
        }
        /* Rule 3: one multi-sector survivor tears. A single-sector write cannot
         * tear, so only those spanning two or more are candidates. */
        size_t torn = (size_t)-1, keep = (size_t)-1;
        if (ok && mode == GSQL_CRASH_TORN) {
            size_t candidates = 0;
            for (size_t i = 0; i < nd; i++)
                if (sector_count(&s->pend[data[i]]) >= 2) candidates++;
            if (candidates) {
                size_t pick = (size_t)(splitmix(&rng) % candidates);
                for (size_t i = 0; i < nd; i++)
                    if (sector_count(&s->pend[data[i]]) >= 2 && pick-- == 0) {
                        torn = i;
                        keep = (size_t)(splitmix(&rng) % sector_count(&s->pend[data[i]]));
                        break;
                    }
            }
        }
        for (size_t i = 0; ok && i < nd; i++) {
            const Pend *p = &s->pend[data[i]];
            ok = apply_data(&t->inodes[p->inode]->durable, p, i == torn ? keep : (size_t)-1);
        }
        free(data);
    }

    /* A directory mkdir made whose creation this disk lost takes everything
     * beneath it: a name under a missing directory is unreachable, whatever
     * was synced inside (rule 2). */
    for (size_t d = 0; ok && d < s->made.count; d++) {
        const char *dir = s->made.items[d].path;
        size_t dl = strlen(dir);
        if (names_find(&t->durable_names, dir) >= 0) { ok = names_set(&t->made, dir, 0); continue; }
        for (size_t i = 0; i < t->durable_names.count;) {
            const char *p = t->durable_names.items[i].path;
            if (strncmp(p, dir, dl) == 0 && p[dl] == '/') names_del(&t->durable_names, p);
            else i++;
        }
    }

    /* The new disk has nothing pending: its view IS its durable image. */
    for (size_t i = 0; ok && i < t->inode_count; i++)
        ok = buf_copy(&t->inodes[i]->view, &t->inodes[i]->durable);
    if (ok) ok = names_copy(&t->view_names, &t->durable_names);
    if (!ok) {
        sim_free(&t->base);
        return NULL;
    }
    return &t->base;
}

unsigned char *gsql_io_sim_contents(const GsqlIo *io, const char *path, size_t *len) {
    const Sim *s = (const Sim *)io;
    *len = 0;
    long at = names_find(&s->view_names, path);
    if (at < 0 || s->view_names.items[at].inode == DIR_INODE) return NULL;
    const Buf *b = &s->inodes[s->view_names.items[at].inode]->view;
    unsigned char *out = malloc(b->len ? b->len : 1);
    if (!out) return NULL;
    if (b->len) memcpy(out, b->bytes, b->len);
    *len = b->len;
    return out;
}

size_t gsql_io_sim_file_count(const GsqlIo *io) {
    const Sim *s = (const Sim *)io;
    size_t n = 0;
    for (size_t i = 0; i < s->view_names.count; i++) n += s->view_names.items[i].inode != DIR_INODE;
    return n;
}

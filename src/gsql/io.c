/* G-SQL's I/O layer: the dispatch, and the REAL implementation
 * (docs/gsql_storage_design.md §3.4). The simulated one is io_sim.c. See
 * include/gsql_io.h for the contract both obey. */

#define _GNU_SOURCE
#include "io_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int gsql_io_open(GsqlIo *io, const char *path, int flags, GsqlFile **out) {
    *out = NULL;
    return io->ops->open(io, path, flags, out);
}
int gsql_io_read_at(GsqlFile *f, uint64_t offset, void *buf, size_t n, size_t *got) {
    *got = 0;
    return f->io->ops->read_at(f, offset, buf, n, got);
}
int gsql_io_write_at(GsqlFile *f, uint64_t offset, const void *buf, size_t n) {
    return f->io->ops->write_at(f, offset, buf, n);
}
int gsql_io_sync(GsqlFile *f) { return f->io->ops->sync(f); }
int gsql_io_size(GsqlFile *f, uint64_t *out) {
    *out = 0;
    return f->io->ops->size(f, out);
}
int gsql_io_truncate(GsqlFile *f, uint64_t n) { return f->io->ops->truncate(f, n); }
int gsql_io_close(GsqlFile *f) { return f->io->ops->close(f); }
int gsql_io_rename(GsqlIo *io, const char *from, const char *to) {
    return io->ops->rename(io, from, to);
}
int gsql_io_remove(GsqlIo *io, const char *path) { return io->ops->remove(io, path); }
int gsql_io_sync_dir(GsqlIo *io, const char *dir) { return io->ops->sync_dir(io, dir); }
int gsql_io_lock(GsqlFile *f, int nonblocking) { return f->io->ops->lock(f, nonblocking); }
void gsql_io_free(GsqlIo *io) {
    if (io) io->ops->free(io);
}
void gsql_io_random(GsqlIo *io, void *out, size_t n) { io->ops->random(io, out, n); }
int gsql_io_mkdir(GsqlIo *io, const char *path) { return io->ops->mkdir(io, path); }
int gsql_io_list(GsqlIo *io, const char *dir, int (*each)(void *, const char *), void *ctx) {
    return io->ops->list(io, dir, each, ctx);
}

/* Shared by both layers: the names in byte order, then the walk, so the two
 * cannot disagree about order and a parity test compares like with like. */
static int name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
int gsql_io_walk_names(char **names, size_t n, int (*each)(void *, const char *), void *ctx) {
    qsort(names, n, sizeof *names, name_cmp);
    int rc = 0;
    for (size_t i = 0; i < n && !rc; i++) rc = each(ctx, names[i]);
    for (size_t i = 0; i < n; i++) free(names[i]);
    free(names);
    return rc;
}

static void urandom(GsqlIo *io, void *out, size_t n) {
    (void)io;
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got = f ? fread(out, 1, n, f) : 0;
    if (f) fclose(f);
    /* no urandom: the clock, which is weak and still not a constant */
    for (size_t i = got; i < n; i++) ((unsigned char *)out)[i] = (unsigned char)(clock() * 2654435761u >> (8 * (i % 4)));
}

#if defined(_WIN32)

/* Not written: a layer nobody here can run is a claim nobody has checked. The
 * route is FlushFileBuffers, MoveFileEx with MOVEFILE_WRITE_THROUGH and
 * LockFileEx, recorded in §3.4. */
static int win_open(GsqlIo *io, const char *p, int fl, GsqlFile **o) {
    (void)io; (void)p; (void)fl; (void)o;
    return ENOSYS;
}
static int win_unused_io(GsqlIo *io, const char *a) { (void)io; (void)a; return ENOSYS; }
static int win_unused_rename(GsqlIo *io, const char *a, const char *b) {
    (void)io; (void)a; (void)b;
    return ENOSYS;
}
static void win_free(GsqlIo *io) { free(io); }
static int win_list(GsqlIo *io, const char *d, int (*e)(void *, const char *), void *c) {
    (void)io; (void)d; (void)e; (void)c;
    return ENOSYS;
}
static const GsqlIoOps real_ops = {
    win_open, NULL, NULL, NULL, NULL, NULL, NULL,
    win_unused_rename, win_unused_io, win_unused_io, NULL, win_free, urandom, win_unused_io, win_list
};

#else

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    GsqlFile base;
    int fd;
} RealFile;

static int real_open(GsqlIo *io, const char *path, int flags, GsqlFile **out) {
    int oflags = (flags & GSQL_IO_READ_ONLY) ? O_RDONLY : O_RDWR;
    if (flags & GSQL_IO_CREATE) oflags |= O_CREAT;
    if (flags & GSQL_IO_EXCLUSIVE) oflags |= O_EXCL;
#ifdef O_CLOEXEC
    oflags |= O_CLOEXEC;
#endif
    int fd = open(path, oflags, 0644);
    if (fd < 0) return errno;
    RealFile *f = calloc(1, sizeof *f);
    if (!f) {
        close(fd);
        return ENOMEM;
    }
    f->base.io = io;
    f->fd = fd;
    *out = &f->base;
    return 0;
}

static int real_read_at(GsqlFile *base, uint64_t offset, void *buf, size_t n, size_t *got) {
    RealFile *f = (RealFile *)base;
    size_t done = 0;
    while (done < n) {
        ssize_t r = pread(f->fd, (char *)buf + done, n - done, (off_t)(offset + done));
        if (r < 0) {
            if (errno == EINTR) continue;
            *got = done;
            return errno;
        }
        if (r == 0) break; /* end of file: a short read, not an error */
        done += (size_t)r;
    }
    *got = done;
    return 0;
}

static int real_write_at(GsqlFile *base, uint64_t offset, const void *buf, size_t n) {
    RealFile *f = (RealFile *)base;
    size_t done = 0;
    while (done < n) {
        ssize_t r = pwrite(f->fd, (const char *)buf + done, n - done, (off_t)(offset + done));
        if (r < 0) {
            if (errno == EINTR) continue;
            return errno;
        }
        done += (size_t)r;
    }
    return 0;
}

static int flush_fd(int fd) {
#if defined(__APPLE__)
    if (fcntl(fd, F_FULLFSYNC) == 0) return 0;
    /* Some filesystems refuse F_FULLFSYNC; fsync is then the best there is. */
    return fsync(fd) == 0 ? 0 : errno;
#elif defined(__linux__)
    return fdatasync(fd) == 0 ? 0 : errno;
#else
    return fsync(fd) == 0 ? 0 : errno;
#endif
}

static int real_sync(GsqlFile *base) { return flush_fd(((RealFile *)base)->fd); }

static int real_size(GsqlFile *base, uint64_t *out) {
    struct stat st;
    if (fstat(((RealFile *)base)->fd, &st) != 0) return errno;
    *out = (uint64_t)st.st_size;
    return 0;
}

static int real_truncate(GsqlFile *base, uint64_t n) {
    return ftruncate(((RealFile *)base)->fd, (off_t)n) == 0 ? 0 : errno;
}

static int real_close(GsqlFile *base) {
    RealFile *f = (RealFile *)base;
    int rc = close(f->fd) == 0 ? 0 : errno;
    free(f);
    return rc;
}

static int real_rename(GsqlIo *io, const char *from, const char *to) {
    (void)io;
    return rename(from, to) == 0 ? 0 : errno;
}

static int real_remove(GsqlIo *io, const char *path) {
    (void)io;
    return unlink(path) == 0 ? 0 : errno;
}

static int real_sync_dir(GsqlIo *io, const char *dir) {
    (void)io;
    int fd = open(dir[0] ? dir : ".", O_RDONLY
#ifdef O_DIRECTORY
                  | O_DIRECTORY
#endif
    );
    if (fd < 0) return errno;
    int rc = fsync(fd) == 0 ? 0 : errno;
    close(fd);
    return rc;
}

static int real_lock(GsqlFile *base, int nonblocking) {
    RealFile *f = (RealFile *)base;
    if (flock(f->fd, LOCK_EX | (nonblocking ? LOCK_NB : 0)) == 0) return 0;
    return errno == EAGAIN ? EWOULDBLOCK : errno;
}

static void real_free(GsqlIo *io) { free(io); }

static int real_mkdir(GsqlIo *io, const char *path) {
    (void)io;
    return mkdir(path, 0700) == 0 ? 0 : errno;
}

static int real_list(GsqlIo *io, const char *dir, int (*each)(void *, const char *), void *ctx) {
    (void)io;
    DIR *d = opendir(dir[0] ? dir : ".");
    if (!d) return errno;
    char **names = NULL;
    size_t n = 0, cap = 0;
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            char **p = realloc(names, cap * sizeof *p);
            if (!p) { rc = ENOMEM; break; }
            names = p;
        }
        if (!(names[n] = strdup(de->d_name))) { rc = ENOMEM; break; }
        n++;
    }
    closedir(d);
    if (rc) {
        for (size_t i = 0; i < n; i++) free(names[i]);
        free(names);
        return rc;
    }
    return gsql_io_walk_names(names, n, each, ctx);
}

static const GsqlIoOps real_ops = {
    real_open, real_read_at, real_write_at, real_sync, real_size, real_truncate,
    real_close, real_rename, real_remove, real_sync_dir, real_lock, real_free, urandom,
    real_mkdir, real_list
};

#endif

GsqlIo *gsql_io_real(void) {
    GsqlIo *io = calloc(1, sizeof *io);
    if (io) io->ops = &real_ops;
    return io;
}

#ifndef GBASIC_GSQL_IO_H
#define GBASIC_GSQL_IO_H

#include <stddef.h>
#include <stdint.h>

/* G-SQL's I/O layer (docs/gsql_storage_design.md §3.4): every storage candidate
 * does ALL of its file I/O through this, in the role SQLite's VFS plays. Two
 * implementations sit behind one interface -- the real one, which performance
 * is measured on, and a simulated one, which the crash harness (§5.3) uses,
 * because a kill -9 never shows a lost or torn write: the page cache survives
 * the process and writes everything out.
 *
 * THE DURABILITY CONTRACT IS THE MINIMAL ONE POSIX GIVES, and the simulated
 * layer enforces exactly it, so an engine correct here assumes nothing more:
 *   1. a write is durable only after `sync` of that file returns success;
 *   2. a creation, rename or removal is durable only after `sync_dir` of its
 *      directory;
 *   3. a write is atomic only per 512-byte sector;
 *   4. unflushed writes may reach the disk in any order, and any subset may;
 *   5. after a failed `sync`, the state of every unflushed write of that file
 *      is unknown -- and a later successful `sync` does not make it known.
 *
 * Every call answers 0 or a positive errno. Paths are '/'-separated; the
 * simulated layer keeps one flat namespace keyed by the whole path, so its
 * directory is the text before the last '/'. */

#define GSQL_IO_SECTOR 512

/* Answered by the simulated layer for every operation at and after its crash
 * point: the world has stopped, and nothing done after it reaches any disk. Not
 * an errno, so it cannot be confused with one. */
#define GSQL_IO_CRASHED 100000

enum {
    GSQL_IO_CREATE = 1,    /* create if absent */
    GSQL_IO_EXCLUSIVE = 2, /* with CREATE: refuse an existing file (EEXIST) */
    GSQL_IO_READ_ONLY = 4
};

typedef struct GsqlIo GsqlIo;
typedef struct GsqlFile GsqlFile;

int gsql_io_open(GsqlIo *io, const char *path, int flags, GsqlFile **out);
/* A read past the end answers short (`*got` < n), not an error. */
int gsql_io_read_at(GsqlFile *f, uint64_t offset, void *buf, size_t n, size_t *got);
int gsql_io_write_at(GsqlFile *f, uint64_t offset, const void *buf, size_t n);
int gsql_io_sync(GsqlFile *f);
int gsql_io_size(GsqlFile *f, uint64_t *out);
int gsql_io_truncate(GsqlFile *f, uint64_t n);
/* Always releases the handle, whatever it answers. */
int gsql_io_close(GsqlFile *f);
int gsql_io_rename(GsqlIo *io, const char *from, const char *to);
int gsql_io_remove(GsqlIo *io, const char *path);
int gsql_io_sync_dir(GsqlIo *io, const char *dir);
/* An exclusive lock held until the handle closes; a second holder is refused
 * with EWOULDBLOCK when `nonblocking`. */
int gsql_io_lock(GsqlFile *f, int nonblocking);
void gsql_io_free(GsqlIo *io);
/* Random bytes, which are input from the environment like a file: the real
 * layer reads /dev/urandom, the simulated one draws from a seeded generator so
 * a crash run replays exactly. Each crash image draws from its own seed, so a
 * recovered disk does not repeat the draws its parent already made. */
void gsql_io_random(GsqlIo *io, void *out, size_t n);

/* ---- the real layer ---------------------------------------------------- */

/* pread/pwrite, fdatasync (F_FULLFSYNC on macOS, where plain fsync does not
 * reach the platter), flock, and a directory fsync for rule 2. Windows is not
 * implemented: its open answers ENOSYS rather than a layer nobody has run. */
GsqlIo *gsql_io_real(void);

/* ---- the simulated layer ----------------------------------------------- */

/* Files live in memory as a DURABLE image plus the PENDING operations since
 * each was last made durable. What the process reads is the durable image with
 * every pending operation applied in order (the page cache). Every operation is
 * numbered from 1 in the order it is called, reads included, which is what a
 * crash point names. */
GsqlIo *gsql_io_sim(void);

/* Stop the world at operation `n`: that operation and every one after it answer
 * GSQL_IO_CRASHED and have no effect. 0 disarms. */
void gsql_io_sim_crash_at(GsqlIo *io, uint64_t n);
/* How many operations have been numbered so far. */
uint64_t gsql_io_sim_ops(const GsqlIo *io);
int gsql_io_sim_crashed(const GsqlIo *io);
/* The numbers of every sync and sync_dir operation so far, up to `max` of
 * them; answers how many there were. The moments just before a flush are when
 * the most is pending, so a harness that samples crash points draws half of
 * them here: sampled uniformly, a run of thousands of operations rarely lands
 * on the one flush that makes a meta page durable. */
size_t gsql_io_sim_flush_points(const GsqlIo *io, uint64_t *out, size_t max);

typedef enum {
    /* The fault is armed for the `nth` future call of that operation (1 = the
     * next one); it fires once. */
    GSQL_IO_FAIL_SYNC = 1,   /* sync answers EIO, and rule 5 applies */
    GSQL_IO_FAIL_WRITE,      /* write_at answers ENOSPC and changes nothing */
    GSQL_IO_FAIL_READ        /* read_at answers EIO */
} GsqlIoFault;
void gsql_io_sim_fail(GsqlIo *io, GsqlIoFault fault, unsigned nth);
/* Whether that fault is still armed -- false once it has fired, which is how a
 * harness tells "the engine never reached its Nth sync" from "it did, and
 * swallowed the error". */
int gsql_io_sim_fault_armed(const GsqlIo *io, GsqlIoFault fault);
/* The seed that decides, after a failed sync, which of that file's pending
 * writes reached the disk and which were lost (rule 5). */
void gsql_io_sim_seed(GsqlIo *io, uint64_t seed);

typedef enum {
    /* Every operation so far, durable or pending, applied in order: the process
     * died and the page cache survived. */
    GSQL_CRASH_PROCESS = 1,
    /* The durable image plus a seed-chosen SUBSET of pending writes applied in a
     * seed-chosen ORDER (rule 4). Pending namespace operations -- create,
     * rename, remove -- are a seed-chosen subset kept in issue order. */
    GSQL_CRASH_POWER,
    /* As POWER, and one applied pending write keeps only a seed-chosen PREFIX of
     * its sectors, the rest old (rule 3). Sectors are 512 bytes at absolute
     * file offsets, so a write at offset 100 of 1024 bytes spans three. */
    GSQL_CRASH_TORN
} GsqlCrashMode;

/* A NEW simulated layer whose files are the disk this one's crash would leave:
 * everything durable, nothing pending, no locks, counters at zero. The same
 * mode and seed give the same disk every time. The original is untouched. */
GsqlIo *gsql_io_sim_crash_image(const GsqlIo *io, GsqlCrashMode mode, uint64_t seed);

/* The process's current view of one file, for a harness to inspect: a malloc'd
 * copy, or NULL with *len 0 when no such file exists. */
unsigned char *gsql_io_sim_contents(const GsqlIo *io, const char *path, size_t *len);
/* How many files the current view holds. */
size_t gsql_io_sim_file_count(const GsqlIo *io);

#endif

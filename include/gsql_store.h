#ifndef GSQL_STORE_H
#define GSQL_STORE_H

/* G-SQL's storage interface (docs/gsql_storage_design.md §1), and the engine
 * Phase 0a chose for it: §2.C, the log-structured merge tree (src/gsql/lsm.c,
 * chosen 2026-10-29 in §6's step 4).
 *
 * It stays a VTABLE although one engine implements it here, deliberately: the
 * research harness (research/gsql) still drives 2.B, the fallback, through the
 * same interface, and the functional test and the crash adapter in tests/gsql
 * take an engine by name. One interface held to by both engines is what keeps
 * "2.B is the fallback" a claim that can still be checked.
 *
 * Single-threaded by contract (§1): "concurrent readers" are several open
 * snapshots interleaved by the one owner thread. */

#include <stddef.h>
#include <stdint.h>

#include "gsql_io.h"

typedef enum {
    GS_OK = 0,
    GS_NOT_FOUND,   /* no such key, tree or record */
    GS_EXISTS,      /* NO_OVERWRITE met an existing key; tree_create of an existing name */
    GS_ORDER,       /* an APPEND key was not greater than the last */
    GS_BUSY,        /* a write transaction is open, or another process holds the file */
    GS_TOO_LARGE,   /* a key over the limit */
    GS_INVALIDATED, /* a cursor whose tree was modified other than through it */
    GS_CORRUPT,     /* a checksum or structural check failed */
    GS_IO,          /* an operating-system error */
    GS_FULL,        /* the disk is full; the transaction is aborted */
    GS_FAILED,      /* an earlier flush failed; reopen to recover */
    GS_FORMAT,      /* not a G-SQL file, or a newer format */
    /* Added in building (not in §1.4's closed set): a write through a read
     * snapshot. G4 requires the ENGINE to refuse it, and none of the eleven
     * says that. */
    GS_READ_ONLY,
    GS_MISUSE       /* the caller broke the interface: close with a txn open, etc. */
} GsStatus;

const char *gs_status_name(int status);

typedef struct {
    int create;
    uint32_t page_size;     /* at creation only; 0 = 4096 */
    uint64_t cache_bytes;   /* 0 = 64 MiB */
    GsqlIo *io;             /* NULL = the real layer */
    /* Candidate tuning, for tests that must reach a code path cheaply (a
     * checkpoint, a compaction) and for the measurements. 0 = the default. */
    uint64_t tune_a;        /* B: checkpoint after this many log frames. C: memtable bytes */
    uint64_t tune_b;        /* C: L0 files before compaction */
    uint64_t tune_c;        /* C: values at least this long go to a blob file */
    uint64_t tune_d;        /* C: read handles kept open (sorted and blob files) */
} GsOptions;

typedef struct GsStore GsStore;
typedef struct GsTxn GsTxn;
typedef struct GsCursor GsCursor;
typedef struct GsBackup GsBackup;

typedef struct {
    uint64_t pages_read, pages_written, bytes_written, flushes, cache_hits;
    uint64_t file_bytes;    /* every file the store owns */
    uint64_t replayed;      /* log records replayed by the open (0 where there is no log) */
} GsStats;

/* A borrowed byte string: valid until the next call on the same txn/cursor. */
typedef struct {
    const unsigned char *p;
    size_t n;
} GsBytes;

enum { GS_NO_OVERWRITE = 1, GS_APPEND = 2 };
typedef enum { GS_SEEK_GE, GS_SEEK_GT, GS_SEEK_LE, GS_SEEK_LT, GS_SEEK_EXACT } GsSeek;

#define GS_MAX_KEY 1024

typedef struct GsEngine {
    const char *name;
    int (*open)(const char *path, const GsOptions *opt, GsStore **out);
    int (*close)(GsStore *db);

    int (*begin_read)(GsStore *db, GsTxn **out);
    int (*begin_write)(GsStore *db, GsTxn **out);
    /* Ends a write txn; *commit_id gets its id. Not for read snapshots. */
    int (*commit)(GsTxn *t, int sync, uint64_t *commit_id);
    int (*sync)(GsStore *db, uint64_t *durable_id);
    void (*abort)(GsTxn *t);      /* also releases a read snapshot */

    int (*tree_create)(GsTxn *t, GsBytes name);
    int (*tree_drop)(GsTxn *t, GsBytes name);
    /* Calls `each` for every tree name in order; stops if it answers nonzero. */
    int (*tree_list)(GsTxn *t, int (*each)(void *ctx, GsBytes name), void *ctx);
    int (*tree_replace)(GsTxn *t, GsBytes target, GsBytes source);

    int (*get)(GsTxn *t, GsBytes tree, GsBytes key, GsBytes *value);
    int (*put)(GsTxn *t, GsBytes tree, GsBytes key, GsBytes value, unsigned flags);
    /* GS_OK if the key was there, GS_NOT_FOUND if not. */
    int (*del)(GsTxn *t, GsBytes tree, GsBytes key);

    int (*cursor_open)(GsTxn *t, GsBytes tree, GsCursor **out);
    /* *positioned: whether a record is under the cursor afterwards */
    int (*seek)(GsCursor *c, GsBytes key, GsSeek mode, int *positioned);
    int (*first)(GsCursor *c, int *positioned);
    int (*last)(GsCursor *c, int *positioned);
    int (*next)(GsCursor *c, int *positioned);
    int (*prev)(GsCursor *c, int *positioned);
    int (*key)(GsCursor *c, GsBytes *key);
    int (*value)(GsCursor *c, GsBytes *value);
    int (*cursor_put)(GsCursor *c, GsBytes value);  /* replaces the current record's value */
    int (*cursor_delete)(GsCursor *c);              /* the cursor rests between neighbours */
    void (*cursor_close)(GsCursor *c);

    int (*backup)(GsTxn *snapshot, const char *dest);
    /* The same backup in slices, so writers commit between them: the owner is
     * single-threaded (§1), and one call blocks every writer for the whole
     * copy (Test 3: 326 ms for 10^6 rows in 2.C). begin names the snapshot,
     * which must stay open until end; each step copies about `budget` bytes
     * and sets *done once the copy is complete and durable; end frees the
     * job, discarding the copy if it is not done. A copy is never left at
     * `dest` looking complete until it is. `backup` is begin, steps, end. */
    int (*backup_begin)(GsTxn *snapshot, const char *dest, GsBackup **out);
    int (*backup_step)(GsBackup *b, uint64_t budget, int *done);
    void (*backup_end)(GsBackup *b);
    /* Verifies every checksum and invariant; writes a report line per problem
     * to `report` (may be NULL) and answers GS_CORRUPT if any. */
    int (*check)(GsStore *db, void (*report)(void *ctx, const char *line), void *ctx);
    void (*stats)(GsStore *db, GsStats *out);
    /* The id a WRITE transaction is known by before it commits: greater than
     * every commit id answered before it began, and no greater than the id its
     * own commit will answer. What the version store (§4) keys a version by,
     * since a version is written inside the transaction that makes it, while
     * the commit id is known only once it commits. With one writer it is known
     * at begin. An engine that cannot say leaves this NULL (the research
     * B+trees, whose bench keeps a counter of its own). */
    int (*txn_id)(GsTxn *t, uint64_t *id);
    /* Makes every commit so far durable AND moves it out of the log, so the
     * next open has nothing to replay. Recovery replays the log (§1.1), and
     * in 2.C that grows with the memtable: up to about 100 ms for a full one
     * (§5.5's handoff, measured 2026-10-28). An owner handing a database to
     * another owner on purpose calls this first. It is NOT done by `close`,
     * which would cost every short session a sorted file of its own, so the
     * choice is the owner's. Refused while a write transaction is open.
     * NULL in an engine with no log to empty. */
    int (*flush)(GsStore *db);
} GsEngine;

extern const GsEngine gs_engine_lsm;  /* §2.C, the chosen engine */

#endif

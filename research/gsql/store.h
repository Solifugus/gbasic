#ifndef GSQL_RESEARCH_STORE_H
#define GSQL_RESEARCH_STORE_H

/* The storage interface of docs/gsql_storage_design.md §1, as the three Phase
 * 0a candidates implement it. One vtable, so one functional test, one crash
 * adapter and one workload harness drive all three.
 *
 * Spike code: nothing here is linked into gbasic or run by the gate (§1). A
 * written-off candidate is deleted with its numbers recorded in the design.
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
} GsOptions;

typedef struct GsStore GsStore;
typedef struct GsTxn GsTxn;
typedef struct GsCursor GsCursor;

typedef struct {
    uint64_t pages_read, pages_written, bytes_written, flushes, cache_hits;
    uint64_t file_bytes;    /* every file the store owns */
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
    /* Verifies every checksum and invariant; writes a report line per problem
     * to `report` (may be NULL) and answers GS_CORRUPT if any. */
    int (*check)(GsStore *db, void (*report)(void *ctx, const char *line), void *ctx);
    void (*stats)(GsStore *db, GsStats *out);
} GsEngine;

extern const GsEngine gs_engine_cow;  /* §2.A */
extern const GsEngine gs_engine_wal;  /* §2.B */
extern const GsEngine gs_engine_lsm;  /* §2.C */

#endif

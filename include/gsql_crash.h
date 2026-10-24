#ifndef GBASIC_GSQL_CRASH_H
#define GBASIC_GSQL_CRASH_H

#include <stddef.h>
#include <stdint.h>

#include "gsql_io.h"

/* G-SQL's crash harness (docs/gsql_storage_design.md §5.3). It drives an
 * engine under test over the SIMULATED I/O layer, stops the world at every
 * operation, builds the disk that crash would leave, reopens (which runs
 * recovery), and judges what came back against a MODEL of what was written --
 * never against the engine itself.
 *
 * THE ORACLE: the recovered state must equal the model after SOME PREFIX P of
 * the commits, where P includes every commit that returned durable and nothing
 * that never reached commit. Beyond that, recovery must be idempotent, survive
 * a crash of its own at every one of its operations, and leave a database that
 * can take more work.
 *
 * The engine interface here is the slice of §1 this needs: commits of puts and
 * deletes over one keyspace, and a full read. A Phase 0a candidate adapts its
 * own interface to it. */

typedef struct {
    const unsigned char *key;
    size_t key_len;
    const unsigned char *value; /* NULL: delete the key */
    size_t value_len;
} GsqlCrashPut;

typedef struct {
    const GsqlCrashPut *puts;
    size_t count;
} GsqlCrashTxn;

/* What `read_all` hands back: every live key with its value. The engine adds
 * pairs through gsql_crash_state_put, in any order. */
typedef struct GsqlCrashState GsqlCrashState;
int gsql_crash_state_put(GsqlCrashState *st, const unsigned char *key, size_t key_len,
                         const unsigned char *value, size_t value_len);

/* The engine's answers beyond an errno. */
enum {
    GSQL_E_FAILED = 200000, /* an earlier flush failed: reopen to recover (§1.3) */
    GSQL_E_FULL,            /* the disk is full; the transaction was aborted */
    GSQL_E_CORRUPT          /* a checksum or structure check failed */
};

typedef struct {
    const char *name;
    /* Open, or create, the database at `dir` on `io`, running recovery before
     * returning. NULL with *err set on failure. */
    void *(*open)(GsqlIo *io, const char *dir, int *err);
    /* Commit one transaction; with `sync`, durable on return. */
    int (*commit)(void *db, const GsqlCrashTxn *txn, int sync);
    /* Make every commit so far durable. */
    int (*sync)(void *db);
    int (*read_all)(void *db, GsqlCrashState *out);
    void (*close)(void *db);
} GsqlCrashEngine;

typedef enum {
    GSQL_CRASH_MODE_PROCESS = 1,
    GSQL_CRASH_MODE_POWER,
    GSQL_CRASH_MODE_TORN,
    GSQL_CRASH_MODE_FAILED_SYNC,
    GSQL_CRASH_MODE_FULL_DISK
} GsqlCrashCase;

typedef struct {
    uint64_t workload_seed;
    size_t transactions;   /* how many the workload generates */
    size_t max_value;      /* value sizes run 0..max_value bytes */
    /* The share of commits made with sync = true, in percent; 0 means 67. A
     * workload that rarely syncs leaves many writes pending at a crash, which
     * is the only place rules 3 and 4 can bite. */
    unsigned sync_percent;
    /* Crash every operation from 1 up to this many (0 = every operation the
     * uncrashed run performs). */
    uint64_t crash_limit;
    /* How many different disks to build from each crash point, each from its
     * own seed (0 means 1). A process crash leaves only one, so this counts for
     * the power-loss and torn modes. */
    unsigned images_per_point;
    /* Also crash the recovery itself at each of its operations. */
    int crash_recovery;
    /* Stop after this many violations, reporting each. */
    unsigned max_reports;
} GsqlCrashConfig;

typedef struct {
    uint64_t crashes;          /* injected crashes, including recovery crashes */
    uint64_t violations;
    uint64_t durable_checked;  /* runs where at least one commit had returned durable */
    uint64_t not_prefix_ok;    /* runs where recovery kept a commit that was not durable */
    uint64_t prefix_dropped;   /* runs where recovery dropped a commit that was not durable */
} GsqlCrashResult;

/* Run one crash mode over every crash point of one workload. Each violation is
 * printed as (candidate, workload, seed, operation, mode) -- the five things
 * that reproduce it -- followed by what was wrong. */
GsqlCrashResult gsql_crash_run(const GsqlCrashEngine *engine, GsqlCrashCase mode,
                               const GsqlCrashConfig *cfg);

const char *gsql_crash_mode_name(GsqlCrashCase mode);

#endif

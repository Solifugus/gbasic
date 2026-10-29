#ifndef GBASIC_GSQL_VERSION_H
#define GBASIC_GSQL_VERSION_H

/* G-SQL's version store (docs/gsql_storage_design.md §4): selective temporal
 * history, current-first and backward-linked, over any engine of
 * gsql_store.h that can name a write transaction before it commits
 * (`txn_id`). Built over the bench's interface in Phase 0a and measured on
 * six engines; this is the same layout over the storage interface itself,
 * with the commit id the ENGINE'S rather than a counter of its own (step 5).
 *
 * The CURRENT value of a temporal column lives in its row, with the stamp of
 * the commit that made it current, so an ordinary read never touches
 * history. The row layout is the relational layer's; this module asks only
 * for the stamp and the bytes. When a write transaction changes the value it
 * hands the old one here, and it is kept under the id it STARTED at, so `AS
 * OF A` is the current value if its start is at most A, and otherwise the
 * greatest kept version at or before A -- one floor seek.
 *
 * Two trees, named with a leading NUL byte so no table name can collide (the
 * relational catalog refuses such names):
 *
 *   "\0history"     lp(table) lp(row) varint(column) be64(start)
 *                   -> flags(1) second(le64) author(le32) value
 *   "\0committime"  be64(second) -> le64(id): the last versioning commit
 *                   made in that second
 *
 * The commit-time index holds one entry per SECOND, because the clock says
 * no more. Ids only grow, and a second is CLAMPED never to go back (a clock
 * stepped backwards would otherwise break the floor seek that maps a second
 * to an id), so the stamp records the second actually indexed.
 *
 * A removal is a version too: a value that stops existing at commit D is
 * kept with a REMOVED flag at D, so `AS OF` any id from D until a later
 * value answers NOT_FOUND rather than the value before the removal. */

#include <stddef.h>
#include <stdint.h>

#include "gsql_store.h"

/* What makes a value current: the commit's id as known at begin (`txn_id`),
 * its second, and its author (the principal, as AmorphDB records one per
 * instance). */
typedef struct {
    uint64_t start;
    uint64_t second;
    uint32_t author;
} GsqlStamp;

/* A value read out of history, owned by the caller: free with
 * gsql_version_value_free. */
typedef struct {
    unsigned char *bytes;
    size_t length;
    size_t capacity;
} GsqlVersionValue;

void gsql_version_value_free(GsqlVersionValue *v);

/* In a write transaction, before its first versioned change: the stamp its
 * new values carry. Records that the transaction happened in `second`
 * (clamped to the last second indexed), and creates the two trees if they
 * are not there. GS_MISUSE for an engine without txn_id or a read snapshot. */
int gsql_version_begin(const GsEngine *e, GsTxn *w, uint64_t second, uint32_t author, GsqlStamp *stamp);

/* Keeps the value a column held before transaction `now` replaces or
 * removes it. `was` is the stamp that value carried. A value `now` itself
 * made current is not kept, since no reader could ever see it (a saving, not
 * a correctness rule: see version.c). */
int gsql_version_keep(const GsEngine *e, GsTxn *w, GsBytes table, GsBytes row, uint32_t column,
                      const GsqlStamp *was, GsBytes old, const GsqlStamp *now);

/* Records that the column has no value from `now` on (a row deleted). Keep
 * the old value first. */
int gsql_version_remove(const GsEngine *e, GsTxn *w, GsBytes table, GsBytes row, uint32_t column,
                        const GsqlStamp *now);

/* The value the column held as of commit `asof`, in transaction or snapshot
 * `t`. `cur` is the stamp of the current value, or NULL when there is none
 * (the row is gone), and `cur_value` its bytes. GS_OK with *stamp and *value
 * filled, or GS_NOT_FOUND when the column had no value then. */
int gsql_version_asof(const GsEngine *e, GsTxn *t, GsBytes table, GsBytes row, uint32_t column, uint64_t asof,
                      const GsqlStamp *cur, GsBytes cur_value, GsqlStamp *stamp, GsqlVersionValue *value);

/* The id of the last versioning commit made by the end of `second`: 0 when
 * none was. `AS OF` a time is this, then gsql_version_asof. */
int gsql_version_at(const GsEngine *e, GsTxn *t, uint64_t second, uint64_t *id);

#endif

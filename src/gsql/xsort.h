#ifndef GBASIC_GSQL_XSORT_H
#define GBASIC_GSQL_XSORT_H

/* The external sorter (docs/gsql_spill_design.md): entries of bytes, sorted
 * by key with memcmp, in bounded memory, spilling sorted runs to a private
 * temporary directory. Ties keep the order they were added in. Every call
 * answers 0 or an errno. */

#include <stddef.h>
#include <stdint.h>

#include "gsql_store.h"   /* GsBytes */

typedef struct GsXsort GsXsort;

/* `tmpdir` NULL: $TMPDIR, else /var/tmp (not /tmp, often RAM). `budget` 0:
 * 64 MB of entries held before a run is written. Nothing is created until
 * the first run is. */
int gs_xsort_open(const char *tmpdir, size_t budget, GsXsort **out);
int gs_xsort_add(GsXsort *s, const void *key, size_t kn, const void *value, size_t vn);
/* No more adds: the merge is ready. */
int gs_xsort_finish(GsXsort *s);
/* The next entry in key order, *have 0 at the end. The bytes stay valid
 * until the next call. */
int gs_xsort_next(GsXsort *s, GsBytes *key, GsBytes *value, int *have);
/* How many runs were written: 0 when everything fit. */
unsigned gs_xsort_runs(const GsXsort *s);
/* How many merge passes ran before the last merge: 0 for up to 128 runs. */
unsigned gs_xsort_passes(const GsXsort *s);
/* Removes every file and the directory, whatever happened. */
void gs_xsort_close(GsXsort *s);

/* A row spool (docs/gsql_operator_spill_design.md §8): entries appended to one
 * file in a private temporary directory, and read back by the offset each
 * was given, any number of times and in any order. */
typedef struct GsSpool GsSpool;

int gs_spool_open(const char *tmpdir, GsSpool **out);
int gs_spool_add(GsSpool *s, const void *p, size_t n, uint64_t *offset);
/* The entry at `offset` into *buf (grown as needed, the caller's to free) */
int gs_spool_get(GsSpool *s, uint64_t offset, unsigned char **buf, size_t *cap, size_t *n);
/* How many bytes the spool has written */
uint64_t gs_spool_size(const GsSpool *s);
void gs_spool_close(GsSpool *s);

/* The sort and spool directories under `tmpdir` (NULL: $TMPDIR, else
 * /var/tmp) left by a process that is gone, a crash's leftovers, removed: each
 * directory is named for the process that made it, so one whose process still
 * runs, another owner's or program's, is left alone. Answers how many. */
unsigned gs_xsort_sweep(const char *tmpdir);

#endif

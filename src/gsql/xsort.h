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
/* Removes every file and the directory, whatever happened. */
void gs_xsort_close(GsXsort *s);

#endif

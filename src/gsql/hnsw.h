#ifndef GBASIC_GSQL_HNSW_H
#define GBASIC_GSQL_HNSW_H

/* An HNSW graph over one VECTOR(N) column (docs/gsql_vector_design.md §9),
 * stored in the engine as rows are, so it is written in the row's own
 * transaction and read at a reader's snapshot. Internal to src/gsql: the
 * table layer keeps it in step with the rows, and the SQL layer searches it.
 *
 * In the index's tree:
 *   'v' + row key   the vector, its float32s little-endian
 *   'n' + row key   the node: its level (one byte), then for each level from 0
 *                   up, a count (two bytes) and that many neighbours, each a
 *                   length (two bytes) and a row key
 *   "e"             the entry point: its level (one byte) and its row key
 *   "c"             how many nodes, eight bytes little-endian
 * Every distance is gb_vec_distance's, the double SQL's operators and gBASIC's
 * distance() answer. A node's level is drawn from a hash of its row key, so
 * the same rows inserted in the same order make the same graph everywhere. */

#include <stddef.h>
#include <stdint.h>

#include "gsql_store.h"
#include "gsql_table.h"

typedef struct {
    GsBytes tree;
    int metric;               /* GbVecMetric */
    unsigned m;               /* neighbours a node keeps above level 0; 2m at level 0 */
    unsigned ef_construction;
    size_t dim;
} GsqlHnsw;

typedef struct {
    unsigned char *key;       /* a row key, owned */
    size_t n;
    double d;
} GsqlHnswHit;

#define GSQL_HNSW_M_DEFAULT 16
#define GSQL_HNSW_EFC_DEFAULT 64
#define GSQL_HNSW_MAX_LEVEL 15

/* Adds a row's vector (dim float32s). The caller has refused a zero vector
 * under the cosine metric. */
int gsql_hnsw_insert(const GsEngine *e, GsTxn *t, const GsqlHnsw *h, GsBytes rowkey, const float *v,
                     GsqlError *err);
/* Removes a row's node and the edges to it; nothing to do when it has none. */
int gsql_hnsw_delete(const GsEngine *e, GsTxn *t, const GsqlHnsw *h, GsBytes rowkey, GsqlError *err);
/* The up to `ef` nearest nodes the graph finds, nearest first, ties by row
 * key. */
int gsql_hnsw_search(const GsEngine *e, GsTxn *t, const GsqlHnsw *h, const float *q, size_t ef,
                     GsqlHnswHit **hits, size_t *nhits, GsqlError *err);
/* Every node, nearest first: the exact answer, when a filter has kept fewer
 * than asked from everything the graph reached. */
int gsql_hnsw_every(const GsEngine *e, GsTxn *t, const GsqlHnsw *h, const float *q, GsqlHnswHit **hits,
                    size_t *nhits, GsqlError *err);
int gsql_hnsw_count(const GsEngine *e, GsTxn *t, const GsqlHnsw *h, uint64_t *count, GsqlError *err);
void gsql_hnsw_hits_free(GsqlHnswHit *hits, size_t n);

/* Index `i` of `tb`, an HNSW index, as the graph functions take it (table.c). */
void gsql_index_hnsw_of(const GsqlTable *tb, size_t i, GsqlHnsw *h);

/* A stored vector (one v3 value, tag 16) as float32s: 1, or 0 when the bytes
 * are not a vector of `dim` dimensions (dim 0: any, *dim_out set). */
int gsql_hnsw_floats(const unsigned char *p, size_t n, size_t dim, float **out, size_t *dim_out);

#endif

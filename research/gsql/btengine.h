#ifndef GSQL_RESEARCH_BTENGINE_H
#define GSQL_RESEARCH_BTENGINE_H

/* The part of a B+tree engine above the pager, shared by §2.A and §2.B: the
 * catalog of named trees (itself a tree, §3.1.4), a transaction's view of it,
 * records, cursors and cursor invalidation. Each engine supplies the pager, the
 * snapshot, commit, recovery, backup and check. */

#include "btree.h"

/* A catalog entry's value: root u64, id u64, kind u8. */
#define BE_ENTRY 17

typedef struct {
    GsBuf name;
    uint64_t root, id;
    int exists;      /* known to exist in this view */
    int dirty;       /* root changed: written to the catalog at flush */
    int created, dropped;
    uint64_t mods;   /* bumped by every modification, for cursor invalidation */
} BeTree;

typedef struct BeCursor BeCursor;

typedef struct BeTxn {
    BtPager *pg;
    int write;
    uint64_t catalog;
    BeTree *trees;
    size_t ntrees, cap;
    BeCursor *cursors;
    GsBuf value;           /* what get() hands back, borrowed until the next call */
    /* called after every operation: where an engine may evict or spill */
    void (*after_op)(struct BeTxn *t);
    /* A write failed part-way (FULL, IO, CORRUPT): the tree may be half
     * changed, so the txn can only abort. Holds the first such status. */
    int failed;
} BeTxn;

struct BeCursor {
    BeTxn *t;
    size_t tree;           /* index into t->trees */
    uint64_t mods;
    BtCursor bc;
    int between;           /* after cursor_delete: rests between neighbours of `gone` */
    GsBuf gone;
    BeCursor *next;
};

void be_txn_init(BeTxn *t, BtPager *pg, uint64_t catalog, int write);
/* Write every changed tree root back into the catalog; t->catalog moves. */
int be_flush(BeTxn *t);
/* Close every cursor and free the view. */
void be_txn_free(BeTxn *t);

int be_tree_create(BeTxn *t, GsBytes name);
int be_tree_drop(BeTxn *t, GsBytes name);
int be_tree_list(BeTxn *t, int (*each)(void *, GsBytes), void *ctx);
int be_tree_replace(BeTxn *t, GsBytes target, GsBytes source);
int be_get(BeTxn *t, GsBytes tree, GsBytes key, GsBytes *value);
int be_put(BeTxn *t, GsBytes tree, GsBytes key, GsBytes value, unsigned flags);
int be_del(BeTxn *t, GsBytes tree, GsBytes key);

int be_cursor_open(BeTxn *t, GsBytes tree, BeCursor **out);
int be_seek(BeCursor *c, GsBytes key, GsSeek mode, int *pos);
int be_first(BeCursor *c, int *pos);
int be_last(BeCursor *c, int *pos);
int be_next(BeCursor *c, int *pos);
int be_prev(BeCursor *c, int *pos);
int be_key(BeCursor *c, GsBytes *key);
int be_value(BeCursor *c, GsBytes *value);
int be_cursor_put(BeCursor *c, GsBytes value);
int be_cursor_delete(BeCursor *c);
void be_cursor_close(BeCursor *c);

/* For engines: every tree root in the catalog of `catalog` (and the catalog
 * itself), for walking all live pages. */
int be_roots(BtPager *pg, uint64_t catalog, int (*each)(void *ctx, GsBytes name, uint64_t root), void *ctx);

#endif

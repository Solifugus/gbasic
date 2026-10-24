#ifndef GSQL_RESEARCH_BTREE_H
#define GSQL_RESEARCH_BTREE_H

/* One B+tree algorithm over slotted pages (docs/gsql_storage_design.md
 * §3.1.3), shared by §2.A and §2.B so that what they are compared on is the
 * PAGER -- copy-on-write against write-ahead log -- and not two different
 * trees. The pager decides what "write this page" means:
 *
 *   copy-on-write: a page not yet written in this transaction is copied to a
 *     new page number, and the old one freed, so every ancestor must follow;
 *   write-ahead log: the page is updated in place, its number unchanged, so
 *     the climb stops as soon as nothing above needs to move.
 *
 * Page pointers handed out by a pager stay valid until the engine's operation
 * ends (the pager evicts only between operations). */

#include "common.h"

typedef struct BtPager BtPager;
struct BtPager {
    uint32_t page_size;
    /* Leaf pages carry a link to the next leaf (§2.B). Off under copy-on-write,
     * where a sibling pointer would force rewriting the neighbour on every
     * change (§2.A). */
    int link_leaves;
    int (*get)(BtPager *pg, uint64_t pgno, unsigned char **page);
    /* Make a page writable. *out_pgno is where it now lives (may differ). */
    int (*write)(BtPager *pg, uint64_t pgno, uint64_t *out_pgno, unsigned char **page);
    /* A new, zeroed, writable page. */
    int (*alloc)(BtPager *pg, uint64_t *pgno, unsigned char **page);
    int (*free)(BtPager *pg, uint64_t pgno);
};

/* The largest key the tree accepts at this page size: §1.2's 1,024, or less
 * where a quarter of the page body cannot hold it (every page must take four
 * cells). At 4 KiB that is below 1,024 -- recorded as built. */
size_t bt_max_key(uint32_t page_size);

int bt_create(BtPager *pg, uint64_t *root);
/* GS_OK with the value copied into *out, or GS_NOT_FOUND. */
int bt_get(BtPager *pg, uint64_t root, GsBytes key, GsBuf *out);
int bt_put(BtPager *pg, uint64_t *root, GsBytes key, GsBytes value, unsigned flags);
int bt_del(BtPager *pg, uint64_t *root, GsBytes key);
/* Free every page of the tree. */
int bt_drop(BtPager *pg, uint64_t root);
/* Call `each` for every page the tree owns, overflow pages included. */
int bt_walk(BtPager *pg, uint64_t root, int (*each)(void *ctx, uint64_t pgno), void *ctx);
/* Structural check; answers GS_CORRUPT and reports each problem. *records
 * gets the number of records if non-NULL. */
int bt_check(BtPager *pg, uint64_t root, void (*report)(void *ctx, const char *line), void *ctx,
             uint64_t *records);

#define BT_MAX_DEPTH 40

typedef struct {
    uint64_t pgno;
    int idx; /* in a leaf: the slot; in an interior page: the child taken, -1 = leftmost */
} BtLevel;

typedef struct {
    BtPager *pg;
    uint64_t root;
    BtLevel stack[BT_MAX_DEPTH];
    int depth;      /* levels in stack; the leaf is stack[depth-1] */
    int detached;   /* followed a leaf link, so the stack above the leaf is stale */
    int positioned;
    GsBuf key;      /* a copy of the current key */
    GsBuf value;    /* filled by bt_cursor_value */
} BtCursor;

void bt_cursor_init(BtCursor *c, BtPager *pg, uint64_t root);
void bt_cursor_free(BtCursor *c);
int bt_seek(BtCursor *c, GsBytes key, GsSeek mode);
int bt_first(BtCursor *c);
int bt_last(BtCursor *c);
int bt_next(BtCursor *c);
int bt_prev(BtCursor *c);
/* After any of the above, c->positioned says whether c->key holds a record's
 * key. The key is a copy, so it survives later writes to the tree; the value
 * is read only when asked for, since a scan that reads keys alone should not
 * pay for reassembling overflow chains. */
int bt_cursor_value(BtCursor *c);

#endif

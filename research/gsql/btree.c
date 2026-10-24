/* The shared B+tree (see btree.h). Slotted pages as §3.1.3 lays them out:
 *
 *   [0,40)    page header (§3.1.2), sealed by the pager
 *   [40,56)   body header: count u16, gap start u16, gap end u16,
 *             fragmented bytes u16, leftmost child / next leaf u64
 *   [56,..)   slots: u16 cell offsets in key order
 *   [..,ps-16) cells, growing down from the end
 *   [ps-16,ps) authentication tag (zero: encryption is off)
 *
 * Leaf cell:     varint klen, varint vlen, flags, key, value
 *                (flags bit 0: overflow; then the value is u64 total length
 *                and u64 first overflow page, and vlen is 16)
 * Interior cell: varint klen, key, u64 child holding keys >= key
 */

#include "btree.h"

#include <stdio.h>
#include <stdlib.h>

#define H_COUNT 40
#define H_GAPS 42
#define H_GAPE 44
#define H_FRAG 46
#define H_LINK 48
#define SLOTS 56
#define CELL_OVERFLOW 1

static uint32_t body_end(uint32_t ps) { return ps - GS_PAGE_TAG; }
/* Slots plus cells. Every page must hold four cells, so a cell may use a
 * quarter of this, less its slot. */
static uint32_t capacity(uint32_t ps) { return body_end(ps) - SLOTS; }
static uint32_t max_cell(uint32_t ps) { return capacity(ps) / 4 - 2; }

size_t bt_max_key(uint32_t ps) {
    /* the worst cell holding a key: a leaf whose value overflowed */
    size_t k = max_cell(ps) - (2 + 1 + 1 + 16);
    return k < GS_MAX_KEY ? k : GS_MAX_KEY;
}

static unsigned count(const unsigned char *p) { return get16(p + H_COUNT); }
static int is_leaf(const unsigned char *p) { return p[0] == GS_PT_LEAF; }
static unsigned char *cell_at(unsigned char *p, unsigned i) { return p + get16(p + SLOTS + 2 * i); }
static const unsigned char *ccell_at(const unsigned char *p, unsigned i) { return p + get16(p + SLOTS + 2 * i); }

typedef struct {
    GsBytes key;
    const unsigned char *payload; /* leaf: the value or the overflow reference */
    size_t payload_len;
    unsigned flags;
    uint64_t child;               /* interior */
    size_t len;                   /* the whole cell */
} Cell;

static int parse_cell(const unsigned char *page, uint32_t ps, const unsigned char *c, int leaf, Cell *out) {
    size_t avail = (size_t)(page + body_end(ps) - c);
    uint64_t klen, vlen = 0;
    size_t at = varint_get(c, avail, &klen);
    if (!at) return GS_CORRUPT;
    if (leaf) {
        size_t m = varint_get(c + at, avail - at, &vlen);
        if (!m) return GS_CORRUPT;
        at += m;
        if (at + 1 > avail) return GS_CORRUPT;
        out->flags = c[at++];
    } else {
        out->flags = 0;
    }
    if (klen > avail - at) return GS_CORRUPT;
    out->key = gsb(c + at, (size_t)klen);
    at += (size_t)klen;
    if (leaf) {
        if (vlen > avail - at) return GS_CORRUPT;
        if ((out->flags & CELL_OVERFLOW) && vlen != 16) return GS_CORRUPT;
        out->payload = c + at;
        out->payload_len = (size_t)vlen;
        at += (size_t)vlen;
        out->child = 0;
    } else {
        if (avail - at < 8) return GS_CORRUPT;
        out->child = get64(c + at);
        at += 8;
        out->payload = NULL;
        out->payload_len = 0;
    }
    out->len = at;
    return GS_OK;
}

static int cell_of(const unsigned char *page, uint32_t ps, unsigned i, Cell *out) {
    if (i >= count(page)) return GS_CORRUPT;
    uint16_t off = get16(page + SLOTS + 2 * i);
    if (off < SLOTS + 2 * count(page) || off >= body_end(ps)) return GS_CORRUPT;
    return parse_cell(page, ps, page + off, is_leaf(page), out);
}

/* First slot whose key is >= key (count if none); *found if equal. */
static int lower_bound(const unsigned char *page, uint32_t ps, GsBytes key, unsigned *idx, int *found) {
    unsigned lo = 0, hi = count(page);
    *found = 0;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        Cell c;
        int rc = cell_of(page, ps, mid, &c);
        if (rc) return rc;
        int cmp = bytes_cmp(c.key, key);
        if (cmp < 0) lo = mid + 1;
        else {
            if (cmp == 0) *found = 1;
            hi = mid;
        }
    }
    *idx = lo;
    return GS_OK;
}

/* The child of an interior page holding `key`: -1 for the leftmost. */
static int child_index(const unsigned char *page, uint32_t ps, GsBytes key, int *ci) {
    unsigned idx;
    int found;
    int rc = lower_bound(page, ps, key, &idx, &found);
    if (rc) return rc;
    /* keys >= cell i's key go to cell i's child */
    *ci = found ? (int)idx : (int)idx - 1;
    return GS_OK;
}

static uint64_t child_at(const unsigned char *page, uint32_t ps, int ci, int *bad) {
    if (ci < 0) return get64(page + H_LINK);
    Cell c;
    if (cell_of(page, ps, (unsigned)ci, &c)) {
        *bad = 1;
        return 0;
    }
    return c.child;
}

static void set_child(unsigned char *page, uint32_t ps, int ci, uint64_t child) {
    if (ci < 0) {
        put64(page + H_LINK, child);
        return;
    }
    Cell c;
    if (cell_of(page, ps, (unsigned)ci, &c) == GS_OK) put64(cell_at(page, (unsigned)ci) + c.len - 8, child);
}

static void page_init(unsigned char *p, uint32_t ps, unsigned type) {
    memset(p, 0, ps);
    p[0] = (unsigned char)type;
    put16(p + H_COUNT, 0);
    put16(p + H_GAPS, SLOTS);
    put16(p + H_GAPE, (uint16_t)body_end(ps));
    put16(p + H_FRAG, 0);
}

static uint32_t used_bytes(const unsigned char *p, uint32_t ps) {
    uint32_t free_gap = get16(p + H_GAPE) - get16(p + H_GAPS);
    return capacity(ps) - free_gap - get16(p + H_FRAG);
}

/* ---- building pages from cell lists ------------------------------------- */

typedef struct {
    unsigned char *bytes;
    size_t len;
} RawCell;

typedef struct {
    RawCell *v;
    size_t n, cap;
    GsBuf arena; /* the cells are copied here, since the page they came from is about to be rewritten */
    size_t *offs;
} CellList;

static void cl_free(CellList *l) {
    free(l->v);
    free(l->offs);
    gsbuf_free(&l->arena);
    memset(l, 0, sizeof *l);
}

static int cl_push(CellList *l, const unsigned char *bytes, size_t len) {
    if (l->n == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 64;
        RawCell *v = realloc(l->v, cap * sizeof *v);
        size_t *o = realloc(l->offs, cap * sizeof *o);
        if (!v || !o) { free(v == l->v ? NULL : v); return GS_IO; }
        l->v = v;
        l->offs = o;
        l->cap = cap;
    }
    l->offs[l->n] = l->arena.n;
    if (!gsbuf_append(&l->arena, bytes, len)) return GS_IO;
    l->v[l->n].len = len;
    l->n++;
    return GS_OK;
}

static void cl_fix(CellList *l) {
    for (size_t i = 0; i < l->n; i++) l->v[i].bytes = l->arena.p + l->offs[i];
}

static int cl_from_page(CellList *l, const unsigned char *page, uint32_t ps) {
    for (unsigned i = 0; i < count(page); i++) {
        Cell c;
        int rc = cell_of(page, ps, i, &c);
        if (rc) return rc;
        rc = cl_push(l, ccell_at(page, i), c.len);
        if (rc) return rc;
    }
    return GS_OK;
}

/* Rewrite `page` holding exactly cells[from, to), keeping type and link. */
static void build(unsigned char *page, uint32_t ps, unsigned type, uint64_t link, RawCell *cells, size_t from, size_t to) {
    page_init(page, ps, type);
    put64(page + H_LINK, link);
    uint32_t end = body_end(ps);
    unsigned n = 0;
    for (size_t i = from; i < to; i++) {
        end -= (uint32_t)cells[i].len;
        memcpy(page + end, cells[i].bytes, cells[i].len);
        put16(page + SLOTS + 2 * n, (uint16_t)end);
        n++;
    }
    put16(page + H_COUNT, (uint16_t)n);
    put16(page + H_GAPS, (uint16_t)(SLOTS + 2 * n));
    put16(page + H_GAPE, (uint16_t)end);
}

static size_t cells_size(RawCell *cells, size_t from, size_t to) {
    size_t s = 0;
    for (size_t i = from; i < to; i++) s += cells[i].len + 2;
    return s;
}

/* Insert a cell at slot i if it fits, compacting if fragmentation allows.
 * Answers 1 if done, 0 if the page must split. */
static int try_insert(unsigned char *page, uint32_t ps, unsigned i, const unsigned char *cell, size_t len) {
    uint32_t gs = get16(page + H_GAPS), ge = get16(page + H_GAPE);
    unsigned n = count(page);
    if (ge - gs < len + 2) {
        if (ge - gs + get16(page + H_FRAG) < len + 2) return 0;
        /* compact, then fall through to the ordinary insert */
        CellList l;
        memset(&l, 0, sizeof l);
        if (cl_from_page(&l, page, ps)) { cl_free(&l); return 0; }
        cl_fix(&l);
        build(page, ps, page[0], get64(page + H_LINK), l.v, 0, l.n);
        cl_free(&l);
        gs = get16(page + H_GAPS);
        ge = get16(page + H_GAPE);
    }
    ge -= (uint32_t)len;
    memcpy(page + ge, cell, len);
    memmove(page + SLOTS + 2 * (i + 1), page + SLOTS + 2 * i, 2 * (n - i));
    put16(page + SLOTS + 2 * i, (uint16_t)ge);
    put16(page + H_COUNT, (uint16_t)(n + 1));
    put16(page + H_GAPS, (uint16_t)(gs + 2));
    put16(page + H_GAPE, (uint16_t)ge);
    return 1;
}

static void remove_slot(unsigned char *page, uint32_t ps, unsigned i) {
    Cell c;
    unsigned n = count(page);
    if (cell_of(page, ps, i, &c)) return;
    uint16_t off = get16(page + SLOTS + 2 * i);
    if (off == get16(page + H_GAPE)) put16(page + H_GAPE, (uint16_t)(off + c.len));
    else put16(page + H_FRAG, (uint16_t)(get16(page + H_FRAG) + c.len));
    memmove(page + SLOTS + 2 * i, page + SLOTS + 2 * (i + 1), 2 * (n - i - 1));
    put16(page + H_COUNT, (uint16_t)(n - 1));
    put16(page + H_GAPS, (uint16_t)(get16(page + H_GAPS) - 2));
}

static size_t make_leaf_cell(unsigned char *out, GsBytes key, const unsigned char *payload, size_t plen, unsigned flags) {
    size_t at = varint_put(out, key.n);
    at += varint_put(out + at, plen);
    out[at++] = (unsigned char)flags;
    memcpy(out + at, key.p, key.n);
    at += key.n;
    memcpy(out + at, payload, plen);
    return at + plen;
}

static size_t make_interior_cell(unsigned char *out, GsBytes key, uint64_t child) {
    size_t at = varint_put(out, key.n);
    memcpy(out + at, key.p, key.n);
    at += key.n;
    put64(out + at, child);
    return at + 8;
}

/* ---- overflow chains ---------------------------------------------------- */

#define OV_NEXT 40
#define OV_USED 48
#define OV_DATA 56

static uint32_t ov_cap(uint32_t ps) { return body_end(ps) - OV_DATA; }

static int ov_write(BtPager *pg, GsBytes value, uint64_t *first) {
    uint32_t cap = ov_cap(pg->page_size);
    unsigned char *prev = NULL;
    size_t at = 0;
    *first = 0;
    while (at < value.n) {
        uint64_t pgno;
        unsigned char *p;
        int rc = pg->alloc(pg, &pgno, &p);
        if (rc) return rc;
        memset(p, 0, pg->page_size);
        p[0] = GS_PT_OVERFLOW;
        size_t take = value.n - at < cap ? value.n - at : cap;
        memcpy(p + OV_DATA, value.p + at, take);
        put32(p + OV_USED, (uint32_t)take);
        put64(p + OV_NEXT, 0);
        if (prev) put64(prev + OV_NEXT, pgno);
        else *first = pgno;
        prev = p;
        at += take;
    }
    return GS_OK;
}

static int ov_read(BtPager *pg, uint64_t first, uint64_t total, GsBuf *out) {
    out->n = 0;
    if (!gsbuf_reserve(out, (size_t)total + 1)) return GS_IO;
    uint64_t pgno = first;
    size_t guard = (size_t)(total / ov_cap(pg->page_size)) + 2;
    while (out->n < total) {
        if (!pgno || guard-- == 0) return GS_CORRUPT;
        unsigned char *p;
        int rc = pg->get(pg, pgno, &p);
        if (rc) return rc;
        if (p[0] != GS_PT_OVERFLOW) return GS_CORRUPT;
        uint32_t used = get32(p + OV_USED);
        if (used > ov_cap(pg->page_size) || out->n + used > total) return GS_CORRUPT;
        memcpy(out->p + out->n, p + OV_DATA, used);
        out->n += used;
        pgno = get64(p + OV_NEXT);
    }
    return GS_OK;
}

static int ov_free(BtPager *pg, uint64_t first, int (*each)(void *, uint64_t), void *ctx) {
    uint64_t pgno = first;
    size_t guard = 1u << 30;
    while (pgno && guard--) {
        unsigned char *p;
        int rc = pg->get(pg, pgno, &p);
        if (rc) return rc;
        if (p[0] != GS_PT_OVERFLOW) return GS_CORRUPT;
        uint64_t next = get64(p + OV_NEXT);
        rc = each ? each(ctx, pgno) : pg->free(pg, pgno);
        if (rc) return rc;
        pgno = next;
    }
    return GS_OK;
}

static int cell_value(BtPager *pg, const Cell *c, GsBuf *out) {
    if (c->flags & CELL_OVERFLOW) return ov_read(pg, get64(c->payload + 8), get64(c->payload), out);
    return gsbuf_set(out, c->payload, c->payload_len) ? GS_OK : GS_IO;
}

/* ---- create, get -------------------------------------------------------- */

int bt_create(BtPager *pg, uint64_t *root) {
    unsigned char *p;
    int rc = pg->alloc(pg, root, &p);
    if (rc) return rc;
    page_init(p, pg->page_size, GS_PT_LEAF);
    return GS_OK;
}

typedef struct {
    BtLevel lv[BT_MAX_DEPTH];
    int depth;
} Path;

static int descend(BtPager *pg, uint64_t root, GsBytes key, Path *path, unsigned char **leaf, unsigned *idx, int *found) {
    uint64_t pgno = root;
    path->depth = 0;
    for (;;) {
        if (path->depth >= BT_MAX_DEPTH) return GS_CORRUPT;
        unsigned char *p;
        int rc = pg->get(pg, pgno, &p);
        if (rc) return rc;
        if (p[0] == GS_PT_LEAF) {
            rc = lower_bound(p, pg->page_size, key, idx, found);
            if (rc) return rc;
            path->lv[path->depth].pgno = pgno;
            path->lv[path->depth].idx = (int)*idx;
            path->depth++;
            *leaf = p;
            return GS_OK;
        }
        if (p[0] != GS_PT_INTERIOR) return GS_CORRUPT;
        int ci, bad = 0;
        rc = child_index(p, pg->page_size, key, &ci);
        if (rc) return rc;
        path->lv[path->depth].pgno = pgno;
        path->lv[path->depth].idx = ci;
        path->depth++;
        pgno = child_at(p, pg->page_size, ci, &bad);
        if (bad || !pgno) return GS_CORRUPT;
    }
}

int bt_get(BtPager *pg, uint64_t root, GsBytes key, GsBuf *out) {
    Path path;
    unsigned char *leaf;
    unsigned idx;
    int found;
    int rc = descend(pg, root, key, &path, &leaf, &idx, &found);
    if (rc) return rc;
    if (!found) return GS_NOT_FOUND;
    Cell c;
    rc = cell_of(leaf, pg->page_size, idx, &c);
    if (rc) return rc;
    return cell_value(pg, &c, out);
}

/* ---- put ---------------------------------------------------------------- */

/* Split `page` (already writable, at `pgno`) after inserting `cell` at slot
 * `at`. The left half stays in `page`; *right is the new page; *sep is the key
 * the parent must hold for it. `append` splits at the end instead of the
 * middle, so an ordered load fills pages instead of leaving them half full. */
static int split(BtPager *pg, unsigned char *page, unsigned at, const unsigned char *cell, size_t len,
                 int append, uint64_t *right, GsBuf *sep) {
    uint32_t ps = pg->page_size;
    int leaf = is_leaf(page);
    CellList l;
    memset(&l, 0, sizeof l);
    int rc = GS_OK;
    for (unsigned i = 0; i <= count(page) && rc == GS_OK; i++) {
        if (i == at) rc = cl_push(&l, cell, len);
        if (i < count(page) && rc == GS_OK) {
            Cell c;
            rc = cell_of(page, ps, i, &c);
            if (rc == GS_OK) rc = cl_push(&l, ccell_at(page, i), c.len);
        }
    }
    if (rc) { cl_free(&l); return rc; }
    cl_fix(&l);
    size_t n = l.n, total = cells_size(l.v, 0, n), m;
    if (append) {
        m = leaf ? n - 1 : n - 1;
    } else {
        size_t acc = 0;
        m = 0;
        while (m < n - 1 && acc + l.v[m].len + 2 <= total / 2) acc += l.v[m++].len + 2;
        if (m == 0) m = 1;
    }
    unsigned char *rp;
    rc = pg->alloc(pg, right, &rp);
    if (rc) { cl_free(&l); return rc; }
    uint64_t link = get64(page + H_LINK);
    if (leaf) {
        uint64_t klen;
        size_t a = varint_get(l.v[m].bytes, l.v[m].len, &klen);
        uint64_t vl;
        a += varint_get(l.v[m].bytes + a, l.v[m].len - a, &vl);
        a += 1;
        if (!gsbuf_set(sep, l.v[m].bytes + a, (size_t)klen)) { cl_free(&l); return GS_IO; }
        build(rp, ps, GS_PT_LEAF, pg->link_leaves ? link : 0, l.v, m, n);
        build(page, ps, GS_PT_LEAF, pg->link_leaves ? *right : 0, l.v, 0, m);
    } else {
        uint64_t klen;
        size_t a = varint_get(l.v[m].bytes, l.v[m].len, &klen);
        if (!gsbuf_set(sep, l.v[m].bytes + a, (size_t)klen)) { cl_free(&l); return GS_IO; }
        uint64_t mid_child = get64(l.v[m].bytes + l.v[m].len - 8);
        build(rp, ps, GS_PT_INTERIOR, mid_child, l.v, m + 1, n);
        build(page, ps, GS_PT_INTERIOR, link, l.v, 0, m);
    }
    cl_free(&l);
    return GS_OK;
}

int bt_put(BtPager *pg, uint64_t *root, GsBytes key, GsBytes value, unsigned flags) {
    uint32_t ps = pg->page_size;
    if (key.n > bt_max_key(ps)) return GS_TOO_LARGE;
    Path path;
    unsigned char *leaf;
    unsigned idx;
    int found;
    int rc = descend(pg, *root, key, &path, &leaf, &idx, &found);
    if (rc) return rc;
    if (found && (flags & GS_NO_OVERWRITE)) return GS_EXISTS;
    int append = (flags & GS_APPEND) != 0;
    if (append) {
        /* Checked, not trusted: the key must land after every key in the tree,
         * which means the rightmost child at every level and the end of the leaf. */
        if (found || idx != count(leaf)) return GS_ORDER;
        for (int d = 0; d < path.depth - 1; d++) {
            unsigned char *p;
            rc = pg->get(pg, path.lv[d].pgno, &p);
            if (rc) return rc;
            if (path.lv[d].idx != (int)count(p) - 1) return GS_ORDER;
        }
    }

    /* The new cell, overflowing if it would not fit a quarter of the page. */
    unsigned char *cellbuf = malloc(ps);
    if (!cellbuf) return GS_IO;
    size_t inline_len = varint_len(key.n) + varint_len(value.n) + 1 + key.n + value.n;
    size_t clen;
    if (inline_len <= max_cell(ps)) {
        clen = make_leaf_cell(cellbuf, key, value.p, value.n, 0);
    } else {
        uint64_t first;
        rc = ov_write(pg, value, &first);
        if (rc) { free(cellbuf); return rc; }
        unsigned char ref[16];
        put64(ref, value.n);
        put64(ref + 8, first);
        clen = make_leaf_cell(cellbuf, key, ref, 16, CELL_OVERFLOW);
    }

    uint64_t np;
    unsigned char *p;
    int lvl = path.depth - 1;
    rc = pg->write(pg, path.lv[lvl].pgno, &np, &p);
    if (rc) { free(cellbuf); return rc; }
    if (found) {
        Cell old;
        rc = cell_of(p, ps, idx, &old);
        if (rc == GS_OK && (old.flags & CELL_OVERFLOW)) rc = ov_free(pg, get64(old.payload + 8), NULL, NULL);
        if (rc) { free(cellbuf); return rc; }
        remove_slot(p, ps, idx);
    }
    GsBuf sep = {0};
    uint64_t right = 0;
    int did_split = 0;
    if (!try_insert(p, ps, idx, cellbuf, clen)) {
        rc = split(pg, p, idx, cellbuf, clen, append, &right, &sep);
        did_split = 1;
    }
    free(cellbuf);
    uint64_t child_old = path.lv[lvl].pgno, child_new = np;

    /* Climb: each parent points at its child's new home, and takes a
     * separator if the child split. In place, nothing changed means stop. */
    unsigned char *ibuf = malloc(ps);
    while (rc == GS_OK && lvl > 0) {
        if (child_new == child_old && !did_split) break;
        lvl--;
        uint64_t pp;
        unsigned char *parent;
        rc = pg->write(pg, path.lv[lvl].pgno, &pp, &parent);
        if (rc) break;
        int ci = path.lv[lvl].idx;
        set_child(parent, ps, ci, child_new);
        child_old = path.lv[lvl].pgno;
        child_new = pp;
        if (did_split) {
            size_t ilen = make_interior_cell(ibuf, gsb(sep.p, sep.n), right);
            did_split = 0;
            if (!try_insert(parent, ps, (unsigned)(ci + 1), ibuf, ilen)) {
                GsBuf sep2 = {0};
                rc = split(pg, parent, (unsigned)(ci + 1), ibuf, ilen, append, &right, &sep2);
                gsbuf_free(&sep);
                sep = sep2;
                did_split = 1;
            }
        }
    }
    free(ibuf);
    if (rc == GS_OK) {
        if (lvl == 0) {
            if (did_split) {
                uint64_t nr;
                unsigned char *rp;
                rc = pg->alloc(pg, &nr, &rp);
                if (rc == GS_OK) {
                    page_init(rp, ps, GS_PT_INTERIOR);
                    put64(rp + H_LINK, child_new);
                    unsigned char cb[GS_MAX_KEY + 32];
                    size_t ilen = make_interior_cell(cb, gsb(sep.p, sep.n), right);
                    try_insert(rp, ps, 0, cb, ilen);
                    *root = nr;
                }
            } else {
                *root = child_new;
            }
        }
        /* lvl > 0: stopped early, nothing above moved, the root is unchanged */
    }
    gsbuf_free(&sep);
    return rc;
}

/* ---- delete ------------------------------------------------------------- */

/* Merge `right` (child ri of parent) into `left` (child ri-1). Both writable;
 * the parent must drop its separator ri afterwards. 0 if they do not fit. */
static int merge_fits(uint32_t ps, const unsigned char *left, const unsigned char *right, size_t sep_extra) {
    return used_bytes(left, ps) + used_bytes(right, ps) + sep_extra <= capacity(ps);
}

static int merge_into(BtPager *pg, unsigned char *left, const unsigned char *right, GsBytes sepkey) {
    uint32_t ps = pg->page_size;
    CellList l;
    memset(&l, 0, sizeof l);
    int rc = cl_from_page(&l, left, ps);
    if (rc == GS_OK && !is_leaf(left)) {
        unsigned char cb[GS_MAX_KEY + 32];
        size_t n = make_interior_cell(cb, sepkey, get64(right + H_LINK));
        rc = cl_push(&l, cb, n);
    }
    if (rc == GS_OK) rc = cl_from_page(&l, right, ps);
    if (rc) { cl_free(&l); return rc; }
    cl_fix(&l);
    uint64_t link = is_leaf(left) ? get64(right + H_LINK) : get64(left + H_LINK);
    build(left, ps, left[0], link, l.v, 0, l.n);
    cl_free(&l);
    return GS_OK;
}

int bt_del(BtPager *pg, uint64_t *root, GsBytes key) {
    uint32_t ps = pg->page_size;
    Path path;
    unsigned char *leaf;
    unsigned idx;
    int found;
    int rc = descend(pg, *root, key, &path, &leaf, &idx, &found);
    if (rc) return rc;
    if (!found) return GS_NOT_FOUND;
    int lvl = path.depth - 1;
    uint64_t np;
    unsigned char *p;
    rc = pg->write(pg, path.lv[lvl].pgno, &np, &p);
    if (rc) return rc;
    Cell c;
    rc = cell_of(p, ps, idx, &c);
    if (rc) return rc;
    if (c.flags & CELL_OVERFLOW) {
        rc = ov_free(pg, get64(c.payload + 8), NULL, NULL);
        if (rc) return rc;
    }
    remove_slot(p, ps, idx);

    uint64_t child_old = path.lv[lvl].pgno, child_new = np;
    unsigned char *child = p;
    int shrank = 1; /* the child lost a cell, so it may want merging */
    while (lvl > 0) {
        int underfull = shrank && used_bytes(child, ps) < capacity(ps) / 4;
        if (child_new == child_old && !underfull) break;
        lvl--;
        uint64_t pp;
        unsigned char *parent;
        rc = pg->write(pg, path.lv[lvl].pgno, &pp, &parent);
        if (rc) return rc;
        int ci = path.lv[lvl].idx;
        set_child(parent, ps, ci, child_new);
        shrank = 0;
        if (underfull && count(parent) > 0) {
            /* merge the right one of a sibling pair into the left one, so the
             * left keeps its identity and a leaf link pointing at it stays valid */
            int li = ci >= 0 ? ci - 1 : -1; /* left of the pair */
            int ri = li + 1;                /* right of the pair, as a cell index */
            int bad = 0;
            Cell sc;
            if (cell_of(parent, ps, (unsigned)ri, &sc)) return GS_CORRUPT;
            uint64_t lp = child_at(parent, ps, li, &bad), rpn = child_at(parent, ps, ri, &bad);
            if (bad) return GS_CORRUPT;
            unsigned char *lpage, *rpage;
            if ((rc = pg->get(pg, lp, &lpage)) || (rc = pg->get(pg, rpn, &rpage))) return rc;
            size_t extra = is_leaf(lpage) ? 0 : sc.key.n + 12 + 2;
            if (merge_fits(ps, lpage, rpage, extra)) {
                uint64_t nl;
                if ((rc = pg->write(pg, lp, &nl, &lpage))) return rc;
                GsBuf sk = {0};
                gsbuf_set(&sk, sc.key.p, sc.key.n);
                rc = merge_into(pg, lpage, rpage, gsb(sk.p, sk.n));
                gsbuf_free(&sk);
                if (rc) return rc;
                if ((rc = pg->free(pg, rpn))) return rc;
                remove_slot(parent, ps, (unsigned)ri);
                set_child(parent, ps, li, nl);
                shrank = 1;
            }
        }
        child = parent;
        child_old = path.lv[lvl].pgno;
        child_new = pp;
    }
    if (lvl == 0) {
        /* an interior root left with only its leftmost child gives way to it */
        while (!is_leaf(child) && count(child) == 0) {
            uint64_t only = get64(child + H_LINK);
            if ((rc = pg->free(pg, child_new))) return rc;
            child_new = only;
            if ((rc = pg->get(pg, only, &child))) return rc;
        }
        *root = child_new;
    }
    return GS_OK;
}

/* ---- walk, drop, check --------------------------------------------------- */

static int walk(BtPager *pg, uint64_t pgno, int depth, int (*each)(void *, uint64_t), void *ctx) {
    if (depth > BT_MAX_DEPTH) return GS_CORRUPT;
    unsigned char *p;
    int rc = pg->get(pg, pgno, &p);
    if (rc) return rc;
    uint32_t ps = pg->page_size;
    unsigned n = count(p);
    if (p[0] == GS_PT_LEAF) {
        for (unsigned i = 0; i < n; i++) {
            Cell c;
            if ((rc = cell_of(p, ps, i, &c))) return rc;
            if (c.flags & CELL_OVERFLOW) {
                uint64_t first = get64(c.payload + 8);
                if ((rc = ov_free(pg, first, each, ctx))) return rc;
                if ((rc = pg->get(pg, pgno, &p))) return rc;
            }
        }
    } else if (p[0] == GS_PT_INTERIOR) {
        /* copied out: the walk below may evict nothing mid-operation, but a
         * callback that frees pages must not see this page change under it */
        uint64_t *children = malloc((n + 1) * sizeof *children);
        if (!children) return GS_IO;
        for (int ci = -1; ci < (int)n; ci++) {
            int bad = 0;
            children[ci + 1] = child_at(p, ps, ci, &bad);
            if (bad) { free(children); return GS_CORRUPT; }
        }
        for (unsigned i = 0; i <= n && rc == GS_OK; i++) rc = walk(pg, children[i], depth + 1, each, ctx);
        free(children);
        if (rc) return rc;
    } else {
        return GS_CORRUPT;
    }
    return each(ctx, pgno);
}

int bt_walk(BtPager *pg, uint64_t root, int (*each)(void *, uint64_t), void *ctx) {
    return walk(pg, root, 0, each, ctx);
}

static int collect(void *ctx, uint64_t pgno) {
    unsigned char b[8];
    put64(b, pgno);
    return gsbuf_append(ctx, b, 8) ? GS_OK : GS_IO;
}

int bt_drop(BtPager *pg, uint64_t root) {
    /* Collect first, then free: freeing while walking would let a pager reuse
     * a page the walk has yet to read. */
    GsBuf list = {0};
    int rc = bt_walk(pg, root, collect, &list);
    for (size_t i = 0; rc == GS_OK && i < list.n; i += 8) rc = pg->free(pg, get64(list.p + i));
    gsbuf_free(&list);
    return rc;
}

typedef struct {
    BtPager *pg;
    void (*report)(void *, const char *);
    void *ctx;
    int bad;
    int leaf_depth;
    uint64_t records;
    uint64_t prev_leaf;   /* for the link chain */
    uint64_t expect_link; /* the link the previous leaf named */
} Chk;

static void problem(Chk *k, uint64_t pgno, const char *what) {
    k->bad = 1;
    if (k->report) {
        char line[200];
        snprintf(line, sizeof line, "page %llu: %s", (unsigned long long)pgno, what);
        k->report(k->ctx, line);
    }
}

static void check_node(Chk *k, uint64_t pgno, int depth, const GsBuf *lo, const GsBuf *hi) {
    if (depth > BT_MAX_DEPTH) { problem(k, pgno, "tree deeper than the limit"); return; }
    unsigned char *p;
    uint32_t ps = k->pg->page_size;
    if (k->pg->get(k->pg, pgno, &p)) { problem(k, pgno, "unreadable (checksum or page number)"); return; }
    if (p[0] != GS_PT_LEAF && p[0] != GS_PT_INTERIOR) { problem(k, pgno, "not a tree page"); return; }
    unsigned n = count(p);
    if (get16(p + H_GAPS) != SLOTS + 2 * n || get16(p + H_GAPE) > body_end(ps) || get16(p + H_GAPS) > get16(p + H_GAPE)) {
        problem(k, pgno, "body header inconsistent");
        return;
    }
    GsBuf prev = {0};
    int have_prev = 0;
    uint64_t *children = malloc((n + 1) * sizeof *children);
    if (!children) { problem(k, pgno, "out of memory"); return; }
    children[0] = get64(p + H_LINK);
    for (unsigned i = 0; i < n; i++) {
        Cell c;
        if (cell_of(p, ps, i, &c)) { problem(k, pgno, "cell does not parse"); gsbuf_free(&prev); free(children); return; }
        if (have_prev && bytes_cmp(gsb(prev.p, prev.n), c.key) >= 0) problem(k, pgno, "keys out of order");
        if (lo && bytes_cmp(c.key, gsb(lo->p, lo->n)) < 0) problem(k, pgno, "key below its separator");
        if (hi && bytes_cmp(c.key, gsb(hi->p, hi->n)) >= 0) problem(k, pgno, "key at or above the next separator");
        gsbuf_set(&prev, c.key.p, c.key.n);
        have_prev = 1;
        if (p[0] == GS_PT_LEAF) {
            k->records++;
            if (c.flags & CELL_OVERFLOW) {
                GsBuf v = {0};
                if (ov_read(k->pg, get64(c.payload + 8), get64(c.payload), &v)) problem(k, pgno, "overflow chain broken");
                gsbuf_free(&v);
                k->pg->get(k->pg, pgno, &p);
            }
        } else {
            children[i + 1] = c.child;
        }
    }
    gsbuf_free(&prev);
    if (p[0] == GS_PT_LEAF) {
        if (k->leaf_depth < 0) k->leaf_depth = depth;
        else if (k->leaf_depth != depth) problem(k, pgno, "leaves at different depths");
        if (k->pg->link_leaves) {
            if (k->prev_leaf && k->expect_link != pgno) problem(k, k->prev_leaf, "leaf link skips or misorders a leaf");
            k->prev_leaf = pgno;
            k->expect_link = get64(p + H_LINK);
        }
        free(children);
        return;
    }
    /* children[0] is the leftmost; children[i+1] holds keys >= cell i's key */
    GsBuf *keys = calloc(n, sizeof *keys);
    for (unsigned i = 0; i < n; i++) {
        Cell c;
        cell_of(p, ps, i, &c);
        gsbuf_set(&keys[i], c.key.p, c.key.n);
    }
    for (unsigned i = 0; i <= n; i++) {
        const GsBuf *clo = i == 0 ? lo : &keys[i - 1];
        const GsBuf *chi = i == n ? hi : &keys[i];
        if (!children[i]) { problem(k, pgno, "null child"); continue; }
        check_node(k, children[i], depth + 1, clo, chi);
    }
    for (unsigned i = 0; i < n; i++) gsbuf_free(&keys[i]);
    free(keys);
    free(children);
}

int bt_check(BtPager *pg, uint64_t root, void (*report)(void *, const char *), void *ctx, uint64_t *records) {
    Chk k = {pg, report, ctx, 0, -1, 0, 0, 0};
    check_node(&k, root, 0, NULL, NULL);
    if (pg->link_leaves && k.prev_leaf && k.expect_link != 0) problem(&k, k.prev_leaf, "the last leaf links onward");
    if (records) *records = k.records;
    return k.bad ? GS_CORRUPT : GS_OK;
}

/* ---- cursors ------------------------------------------------------------ */

void bt_cursor_init(BtCursor *c, BtPager *pg, uint64_t root) {
    memset(c, 0, sizeof *c);
    c->pg = pg;
    c->root = root;
}

void bt_cursor_free(BtCursor *c) {
    gsbuf_free(&c->key);
    gsbuf_free(&c->value);
}

static int load_key(BtCursor *c) {
    unsigned char *p;
    BtLevel *l = &c->stack[c->depth - 1];
    int rc = c->pg->get(c->pg, l->pgno, &p);
    if (rc) return rc;
    Cell cell;
    rc = cell_of(p, c->pg->page_size, (unsigned)l->idx, &cell);
    if (rc) return rc;
    c->positioned = 1;
    c->value.n = 0;
    return gsbuf_set(&c->key, cell.key.p, cell.key.n) ? GS_OK : GS_IO;
}

int bt_cursor_value(BtCursor *c) {
    if (!c->positioned) return GS_NOT_FOUND;
    unsigned char *p;
    BtLevel *l = &c->stack[c->depth - 1];
    int rc = c->pg->get(c->pg, l->pgno, &p);
    if (rc) return rc;
    Cell cell;
    rc = cell_of(p, c->pg->page_size, (unsigned)l->idx, &cell);
    if (rc) return rc;
    return cell_value(c->pg, &cell, &c->value);
}

/* Descend from stack[depth-1] (an interior page, its idx already chosen) to a
 * leaf, taking the leftmost (or rightmost) child at each level below. */
static int descend_edge(BtCursor *c, int rightmost) {
    for (;;) {
        BtLevel *l = &c->stack[c->depth - 1];
        unsigned char *p;
        int rc = c->pg->get(c->pg, l->pgno, &p);
        if (rc) return rc;
        if (p[0] == GS_PT_LEAF) return GS_OK;
        int bad = 0;
        uint64_t ch = child_at(p, c->pg->page_size, l->idx, &bad);
        if (bad || !ch) return GS_CORRUPT;
        if (c->depth >= BT_MAX_DEPTH) return GS_CORRUPT;
        unsigned char *cp;
        if ((rc = c->pg->get(c->pg, ch, &cp))) return rc;
        BtLevel *n = &c->stack[c->depth++];
        n->pgno = ch;
        if (cp[0] == GS_PT_LEAF) n->idx = rightmost ? (int)count(cp) - 1 : 0;
        else n->idx = rightmost ? (int)count(cp) - 1 : -1;
    }
}

/* From a leaf position at or past its end, move forward to the next record. */
static int settle_forward(BtCursor *c) {
    for (;;) {
        BtLevel *leaf = &c->stack[c->depth - 1];
        unsigned char *p;
        int rc = c->pg->get(c->pg, leaf->pgno, &p);
        if (rc) return rc;
        if (leaf->idx < (int)count(p)) return load_key(c);
        if (c->pg->link_leaves) {
            uint64_t next = get64(p + H_LINK);
            if (!next) { c->positioned = 0; return GS_OK; }
            leaf->pgno = next;
            leaf->idx = 0;
            c->detached = 1;
            continue;
        }
        /* climb to the first ancestor with a child further right */
        int d = c->depth - 2;
        while (d >= 0) {
            unsigned char *ap;
            if ((rc = c->pg->get(c->pg, c->stack[d].pgno, &ap))) return rc;
            if (c->stack[d].idx + 1 < (int)count(ap)) break;
            d--;
        }
        if (d < 0) { c->positioned = 0; return GS_OK; }
        c->stack[d].idx++;
        c->depth = d + 1;
        if ((rc = descend_edge(c, 0))) return rc;
    }
}

static int reattach(BtCursor *c, GsBytes key) {
    /* re-descend to `key`'s leaf so the stack above it is current again */
    Path path;
    unsigned char *leaf;
    unsigned idx;
    int found;
    int rc = descend(c->pg, c->root, key, &path, &leaf, &idx, &found);
    if (rc) return rc;
    memcpy(c->stack, path.lv, sizeof(BtLevel) * (size_t)path.depth);
    c->depth = path.depth;
    c->stack[c->depth - 1].idx = (int)idx;
    c->detached = 0;
    return GS_OK;
}

/* From a leaf position before its start (idx < 0), move back to the previous record. */
static int settle_back(BtCursor *c) {
    for (;;) {
        BtLevel *leaf = &c->stack[c->depth - 1];
        unsigned char *p;
        int rc = c->pg->get(c->pg, leaf->pgno, &p);
        if (rc) return rc;
        if (leaf->idx >= 0 && leaf->idx < (int)count(p)) return load_key(c);
        if (leaf->idx >= (int)count(p)) { leaf->idx = (int)count(p) - 1; continue; }
        if (c->detached) {
            /* the stack is stale: find this leaf again from the root, by its first key */
            if (count(p) == 0) { c->positioned = 0; return GS_OK; }
            Cell first;
            if ((rc = cell_of(p, c->pg->page_size, 0, &first))) return rc;
            GsBuf k = {0};
            gsbuf_set(&k, first.key.p, first.key.n);
            rc = reattach(c, gsb(k.p, k.n));
            gsbuf_free(&k);
            if (rc) return rc;
            c->stack[c->depth - 1].idx = -1;
        }
        int d = c->depth - 2;
        while (d >= 0 && c->stack[d].idx < 0) d--;
        if (d < 0) { c->positioned = 0; return GS_OK; }
        c->stack[d].idx--;
        c->depth = d + 1;
        if ((rc = descend_edge(c, 1))) return rc;
    }
}

int bt_seek(BtCursor *c, GsBytes key, GsSeek mode) {
    c->positioned = 0;
    int rc = reattach(c, key);
    if (rc) return rc;
    unsigned char *p;
    BtLevel *leaf = &c->stack[c->depth - 1];
    if ((rc = c->pg->get(c->pg, leaf->pgno, &p))) return rc;
    int found = 0;
    if (leaf->idx < (int)count(p)) {
        Cell cell;
        if ((rc = cell_of(p, c->pg->page_size, (unsigned)leaf->idx, &cell))) return rc;
        found = bytes_cmp(cell.key, key) == 0;
    }
    switch (mode) {
    case GS_SEEK_EXACT:
        if (!found) return GS_OK;
        return load_key(c);
    case GS_SEEK_GE:
        return settle_forward(c);
    case GS_SEEK_GT:
        if (found) leaf->idx++;
        return settle_forward(c);
    case GS_SEEK_LT:
        leaf->idx--;
        return settle_back(c);
    case GS_SEEK_LE:
        if (found) return load_key(c);
        leaf->idx--;
        return settle_back(c);
    }
    return GS_MISUSE;
}

int bt_first(BtCursor *c) {
    c->positioned = 0;
    c->detached = 0;
    c->depth = 1;
    c->stack[0].pgno = c->root;
    unsigned char *p;
    int rc = c->pg->get(c->pg, c->root, &p);
    if (rc) return rc;
    c->stack[0].idx = p[0] == GS_PT_LEAF ? 0 : -1;
    if ((rc = descend_edge(c, 0))) return rc;
    return settle_forward(c);
}

int bt_last(BtCursor *c) {
    c->positioned = 0;
    c->detached = 0;
    c->depth = 1;
    c->stack[0].pgno = c->root;
    unsigned char *p;
    int rc = c->pg->get(c->pg, c->root, &p);
    if (rc) return rc;
    c->stack[0].idx = (int)count(p) - 1;
    if ((rc = descend_edge(c, 1))) return rc;
    return settle_back(c);
}

int bt_next(BtCursor *c) {
    if (!c->positioned) return GS_OK;
    c->stack[c->depth - 1].idx++;
    c->positioned = 0;
    return settle_forward(c);
}

int bt_prev(BtCursor *c) {
    if (!c->positioned) return GS_OK;
    c->stack[c->depth - 1].idx--;
    c->positioned = 0;
    return settle_back(c);
}

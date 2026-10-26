/* §2.C: the log-structured merge tree (RocksDB's and LevelDB's shape).
 *
 * Writes go to a log and an in-memory sorted table (a skiplist). When that
 * fills it is written out as an immutable sorted file at level 0, and
 * compaction merges files level by level. A snapshot is a sequence number;
 * compaction keeps every version a live snapshot needs.
 *
 * Keys. Every tree's records share one keyspace: the internal key is the tree
 * id (8 bytes, big-endian) followed by the user's key, and each version
 * carries a sequence number. Order: key ascending, sequence DESCENDING, so the
 * newest version of a key comes first. The catalog is tree 0: name -> id.
 * Dropping or replacing a tree retires its id; compaction reclaims that id's
 * data once no snapshot older than the retirement is open.
 *
 * Files, named from the path the caller opens:
 *   path              the MANIFEST: header (§3.1.1), the files per level, the
 *                     log number, retired ids. Rewritten whole: written to
 *                     path.tmp, flushed, renamed, directory flushed.
 *   path.log.K        the log for the memtable; K counts up, so recovery finds
 *                     every log by probing K, K+1, ... (the I/O layer has no
 *                     directory listing)
 *   path.N.sst        sorted files: data blocks, an index block, a bloom
 *                     filter and a footer, each block carrying a CRC32C.
 *
 * As built (recorded in the design):
 *   - SEVERAL FILES, not "one file plus at most a log" (§7 question 6):
 *     folding sorted files into one would need an extent allocator the other
 *     two candidates do not have;
 *   - compaction runs in the foreground, inside the commit that triggers it,
 *     because the owner is single-threaded: its cost shows up as commit tail
 *     latency, which is where it belongs in the comparison;
 *   - a write transaction's buffer is held in memory: G5 (a transaction
 *     bigger than the cache) is NOT met by this version;
 *   - backup is logical -- the snapshot's view written as one sorted file --
 *     since a physical copy of the files would carry versions newer than the
 *     snapshot;
 *   - recovery truncates the last log at its last valid record and flushes,
 *     so a record a lost commit left behind can never follow a new one.
 */

#include "common.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define TYPE_DEL 0
#define TYPE_PUT 1
/* A value kept in a blob file; the entry's value is a pointer to it:
 * u64 blob file number, u64 offset, u32 length, u32 crc32c of the value. */
#define TYPE_BLOB 2
#define BLOB_PTR 24
#define ID_LEN 8
/* sequences a write transaction gives its own spilled runs until it commits:
 * above every real sequence and below the buffer's (UINT64_MAX) */
#define TXN_SEQ_BASE (UINT64_MAX - (1ULL << 32))
#define CATALOG_ID 0
#define FIRST_TREE_ID 2

/* ---- an entry -------------------------------------------------------------- */

typedef struct {
    const unsigned char *k; /* tree id + key */
    size_t kn;
    uint64_t seq;
    int type;
    const unsigned char *v;
    size_t vn;
} Ent;

/* internal order: key ascending, sequence descending */
static int ent_cmp(const unsigned char *ak, size_t an, uint64_t as, const unsigned char *bk, size_t bn, uint64_t bs) {
    int c = bytes_cmp(gsb(ak, an), gsb(bk, bn));
    if (c) return c;
    return as > bs ? -1 : as < bs;
}

/* ---- the skiplist (memtable, and a write txn's buffer) -------------------- */

#define SK_MAX 16

typedef struct SkNode {
    unsigned char *k, *v;
    size_t kn, vn;
    uint64_t seq;
    int type;
    int height;
    struct SkNode *next[];
} SkNode;

typedef struct {
    SkNode *head;
    int height;
    uint64_t rng;
    size_t bytes, count;
} Skip;

static void sk_init(Skip *s) {
    s->head = calloc(1, sizeof(SkNode) + SK_MAX * sizeof(SkNode *));
    s->head->height = SK_MAX;
    s->height = 1;
    s->rng = 0x2545F4914F6CDD1DULL;
    s->bytes = s->count = 0;
}

static void sk_free(Skip *s) {
    SkNode *n = s->head->next[0];
    while (n) {
        SkNode *x = n->next[0];
        free(n->k);
        free(n->v);
        free(n);
        n = x;
    }
    free(s->head);
    s->head = NULL;
}


/* the last node strictly before (k, seq) at each level */
static void sk_path(Skip *s, const unsigned char *k, size_t kn, uint64_t seq, SkNode **prev) {
    SkNode *x = s->head;
    for (int l = SK_MAX - 1; l >= 0; l--) {
        while (l < x->height && x->next[l] && ent_cmp(x->next[l]->k, x->next[l]->kn, x->next[l]->seq, k, kn, seq) < 0)
            x = x->next[l];
        prev[l] = x;
    }
}

/* first node >= (k, seq) */
static SkNode *sk_ge(Skip *s, const unsigned char *k, size_t kn, uint64_t seq) {
    SkNode *prev[SK_MAX];
    sk_path(s, k, kn, seq, prev);
    return prev[0]->next[0];
}

/* last node < (k, seq); NULL if none */
static SkNode *sk_lt(Skip *s, const unsigned char *k, size_t kn, uint64_t seq) {
    SkNode *prev[SK_MAX];
    sk_path(s, k, kn, seq, prev);
    return prev[0] == s->head ? NULL : prev[0];
}

/* Insert; with `replace`, an existing node with the same key AND sequence is
 * overwritten (a txn buffer holds one version per key, at sequence MAX). */
static int sk_insert(Skip *s, const unsigned char *k, size_t kn, uint64_t seq, int type, const unsigned char *v, size_t vn, int replace) {
    SkNode *prev[SK_MAX];
    sk_path(s, k, kn, seq, prev);
    SkNode *at = prev[0]->next[0];
    if (replace && at && at->seq == seq && at->kn == kn && !memcmp(at->k, k, kn)) {
        unsigned char *nv = malloc(vn ? vn : 1);
        if (!nv) return GS_IO;
        if (vn) memcpy(nv, v, vn);
        s->bytes = s->bytes - at->vn + vn;
        free(at->v);
        at->v = nv;
        at->vn = vn;
        at->type = type;
        return GS_OK;
    }
    int h = 1;
    while (h < SK_MAX && ((s->rng = s->rng * 6364136223846793005ULL + 1442695040888963407ULL) >> 62) == 0) h++;
    SkNode *n = calloc(1, sizeof(SkNode) + (size_t)h * sizeof(SkNode *));
    if (!n) return GS_IO;
    n->k = malloc(kn ? kn : 1);
    n->v = malloc(vn ? vn : 1);
    if (!n->k || !n->v) { free(n->k); free(n->v); free(n); return GS_IO; }
    memcpy(n->k, k, kn);
    if (vn) memcpy(n->v, v, vn);
    n->kn = kn;
    n->vn = vn;
    n->seq = seq;
    n->type = type;
    n->height = h;
    if (h > s->height) s->height = h;
    for (int l = 0; l < h; l++) {
        n->next[l] = prev[l]->next[l];
        prev[l]->next[l] = n;
    }
    s->bytes += kn + vn + 48 + (size_t)h * 8;
    s->count++;
    return GS_OK;
}

static Ent sk_ent(const SkNode *n) {
    Ent e = {n->k, n->kn, n->seq, n->type, n->v, n->vn};
    return e;
}

/* ---- sorted files ---------------------------------------------------------- */

#define SST_MAGIC 0x4C534D53514C5347ULL /* "GSQLSMSL" */
#define FOOTER 48

typedef struct {
    unsigned char *last; size_t lastn;
    uint64_t lastseq, off;
    uint32_t size;
} IdxEntry;

typedef struct FileMeta {
    uint64_t num, size, entries;
    GsBuf smallest, largest;   /* internal keys (tree id + key) */
    uint64_t small_seq, large_seq;
    /* Every entry's sequence, when nonzero: a write transaction's spilled run
     * is written before its commit has a sequence, so the run's entries carry
     * 0 and the commit gives the whole file one (its keys are distinct). */
    uint64_t seq_over;
    /* The blob files this file points into, its own among them. Each one is
     * counted for as long as this record lives, so a version a snapshot still
     * holds keeps its blobs: a blob file goes when the last record pointing
     * into it is freed. */
    U64s blobs;
    uint64_t own_blob;         /* the blob file written with it, or 0 */
    struct GsStore *db;
    int refs;                  /* versions holding it */
    int obsolete;              /* dropped by a compaction; delete at refs 0 */
    /* opened lazily */
    GsqlFile *f;
    IdxEntry *index;
    size_t nindex;
    unsigned char *bloom;
    size_t bloom_bits;
    unsigned bloom_k;
} FileMeta;

#define LEVELS 7

/* The memtable belongs to a version: a flush makes a new version with an
 * empty one, and readers that began before keep the old memtable AND the old
 * files, which hold the same data between them. (A first draft cleared the
 * memtable in place, freeing nodes an open reader was pointing at.) */
typedef struct {
    Skip sk;
    int refs;
} Mem;

typedef struct Version {
    FileMeta **files[LEVELS];
    size_t nfiles[LEVELS];
    Mem *mem;
    int refs;
} Version;

/* ---- the store ------------------------------------------------------------- */

typedef struct {
    uint64_t id, death; /* retired at commit `death` */
} Dead;

struct GsTxn;

struct GsStore {
    GsqlIo *io;
    int own_io;
    char *path, *dir;
    uint32_t block;           /* data block target size (the page size option) */
    uint64_t memtable_limit, l0_trigger;
    uint64_t vsep;            /* values this long or longer go to blob files; 0: none */
    struct Blob *blob;        /* every blob file a live record points into */
    size_t nblob, capblob;
    uint64_t cache_bytes, bcache_bytes;
    Cache bcache;             /* decoded blocks, keyed (file number, block + 1) */
    GsqlFile *log, *lockf;
    uint64_t log_num, log_end;
    /* The oldest log whose records the memtable still holds, which is where
     * the next open must begin replaying. Not always log_num: an open replays
     * every log from the manifest's onward into one memtable, and before
     * this the manifest recorded the last of them -- so a manifest written
     * before the next flush (a compaction installing, which the incremental
     * compaction made common) dropped the earlier logs' commits from every
     * later recovery. Found by the crash gate as a durable commit lost. */
    uint64_t log_first;
    uint64_t next_file;
    uint64_t last_seq;        /* the last sequence number handed out */
    uint64_t durable_seq;     /* the last one on disk */
    Version *cur;
    Dead *dead;
    size_t ndead, capdead;
    U64s garbage;             /* obsolete file numbers not yet known deleted */
    uint64_t *snaps;          /* open snapshots' sequences */
    size_t nsnaps, capsnaps;
    struct GsTxn *writer;
    uint64_t compact_ptr[LEVELS];
    struct Comp *comp;        /* a compaction in progress, or NULL */
    int failed;
    unsigned char identity[16];
    GsStats st;
};

/* ---- a few helpers ---------------------------------------------------------- */

static int ioerr(int rc) { return gs_io_status(rc); }

static char *file_name(const GsStore *db, uint64_t num) {
    char suf[48];
    snprintf(suf, sizeof suf, ".%llu.sst", (unsigned long long)num);
    return gs_path_with(db->path, suf);
}

static char *blob_name(const GsStore *db, uint64_t num) {
    char suf[48];
    snprintf(suf, sizeof suf, ".%llu.blob", (unsigned long long)num);
    return gs_path_with(db->path, suf);
}

/* ---- blob files ----------------------------------------------------------- */

/* Long values live in blob files written beside the sorted file they were
 * flushed with (WiscKey's separation, in the shape RocksDB's integrated blob
 * files take): a compaction copies the 24-byte pointer and never the value,
 * which is what made 100 KiB values the one workload where this design was
 * the slowest engine measured. */
typedef struct Blob {
    uint64_t num;
    uint64_t refs;  /* file records pointing into it */
    GsqlFile *f;    /* opened at the first read */
} Blob;

static int u64_cmp(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static int u64_has(const U64s *u, uint64_t v) {
    for (size_t i = 0; i < u->n; i++) if (u->v[i] == v) return 1;
    return 0;
}

static Blob *blob_find(GsStore *db, uint64_t num) {
    for (size_t i = 0; i < db->nblob; i++) if (db->blob[i].num == num) return &db->blob[i];
    return NULL;
}

static void blob_ref(GsStore *db, uint64_t num) {
    Blob *b = blob_find(db, num);
    if (!b) {
        if (db->nblob == db->capblob) { db->capblob = db->capblob ? db->capblob * 2 : 16; db->blob = realloc(db->blob, db->capblob * sizeof *db->blob); }
        b = &db->blob[db->nblob++];
        memset(b, 0, sizeof *b);
        b->num = num;
    }
    b->refs++;
}

/* A record pointing into it is freed. `del`: the record's sorted file was
 * itself deleted (obsolete), so if it was the last, the blob file goes too.
 * A record freed because the store is closing deletes nothing (an early
 * draft did, which would have deleted every blob at close). If a deletion
 * is lost to a crash, the next open finds the blob in the manifest's list
 * of live blobs with nothing pointing into it, and deletes it then. */
static void blob_unref(GsStore *db, uint64_t num, int del) {
    Blob *b = blob_find(db, num);
    if (!b || --b->refs > 0) return;
    if (b->f) gsql_io_close(b->f);
    if (del) {
        char *name = blob_name(db, num);
        gsql_io_remove(db->io, name);
        free(name);
    }
    *b = db->blob[--db->nblob];
}

static int blob_read(GsStore *db, const unsigned char *ptr, size_t pn, GsBuf *out) {
    if (pn != BLOB_PTR) return GS_CORRUPT;
    uint64_t num = get64(ptr), off = get64(ptr + 8);
    uint32_t len = get32(ptr + 16), crc = get32(ptr + 20);
    Blob *b = blob_find(db, num);
    if (!b) return GS_CORRUPT;
    if (!b->f) {
        char *name = blob_name(db, num);
        int rc = gsql_io_open(db->io, name, 0, &b->f);
        free(name);
        if (rc) { b->f = NULL; return rc == ENOENT ? GS_CORRUPT : ioerr(rc); }
    }
    gsbuf_reserve(out, len + 1);
    size_t got = 0;
    int rc = ioerr(gsql_io_read_at(b->f, off, out->p, len, &got));
    if (rc) return rc;
    if (got != len || crc32c(out->p, len) != crc) return GS_CORRUPT;
    out->n = len;
    db->st.pages_read++;
    return GS_OK;
}

/* An entry's value into `out`, following a blob pointer. */
static int val_copy(GsStore *db, int type, const unsigned char *v, size_t vn, GsBuf *out) {
    if (type == TYPE_BLOB) return blob_read(db, v, vn, out);
    gsbuf_set(out, v, vn);
    return GS_OK;
}

static char *log_name(const char *path, uint64_t num) {
    char suf[48];
    snprintf(suf, sizeof suf, ".log.%llu", (unsigned long long)num);
    return gs_path_with(path, suf);
}

static int flush_file(GsStore *db, GsqlFile *f) {
    int rc = gsql_io_sync(f);
    db->st.flushes++;
    if (rc) { db->failed = 1; return GS_IO; } /* §1.3 */
    return GS_OK;
}

static void put_ukey(unsigned char *out, uint64_t id, GsBytes key) {
    put64be(out, id);
    if (key.n) memcpy(out + ID_LEN, key.p, key.n);
}

static uint64_t ukey_id(const unsigned char *k) { return get64be(k); }

/* ---- writing a sorted file -------------------------------------------------- */

typedef struct {
    GsStore *db;
    GsqlFile *f;
    uint64_t num, off, entries;
    GsBuf block, index;
    uint32_t nblock_ents;
    GsBuf last;
    uint64_t lastseq;
    U64s hashes;            /* bloom key hashes */
    GsBuf smallest;
    uint64_t small_seq;
    int rc;
    /* the blob file for this file's long values, opened at the first one */
    GsqlFile *bf;
    uint64_t bnum, boff;
    U64s blobs;             /* every blob file an entry points into */
    U64s *track;            /* where to note bnum when it is taken (or NULL) */
} Builder;

static uint64_t key_hash64(const unsigned char *k, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= k[i]; h *= 1099511628211ULL; }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return h;
}

static int bld_open(GsStore *db, Builder *b) {
    memset(b, 0, sizeof *b);
    b->db = db;
    b->num = db->next_file++;
    /* A number may be taken again after a crash, and sorted files and blob
     * files share the numbers: a blob file left from before must not stay
     * beside the sorted file that took its number (it did, unnamed). */
    char *other = blob_name(db, b->num);
    gsql_io_remove(db->io, other);
    free(other);
    char *name = file_name(db, b->num);
    /* CREATE without EXCLUSIVE, then truncate: a crash between writing a file
     * and naming it in the manifest leaves an orphan whose number the next
     * flush reuses */
    int rc = ioerr(gsql_io_open(db->io, name, GSQL_IO_CREATE, &b->f));
    free(name);
    if (rc == GS_OK) rc = ioerr(gsql_io_truncate(b->f, 0));
    if (rc && b->f) {
        /* created and then refused (a full disk): named nowhere, so it goes */
        gsql_io_close(b->f);
        b->f = NULL;
        char *nm = file_name(db, b->num);
        gsql_io_remove(db->io, nm);
        free(nm);
    }
    b->rc = rc;
    return rc;
}

static void bld_write(Builder *b, const void *p, size_t n) {
    if (b->rc) return;
    int rc = gsql_io_write_at(b->f, b->off, p, n);
    if (rc) { b->rc = ioerr(rc); return; }
    b->off += n;
    b->db->st.bytes_written += n;
    b->db->st.pages_written++;
}

static void seal_block(GsBuf *blk, uint32_t count) {
    unsigned char t[8];
    put32(t, count);
    gsbuf_append(blk, t, 4);
    put32(t, crc32c(blk->p, blk->n));
    gsbuf_append(blk, t, 4);
}

static void bld_finish_block(Builder *b) {
    if (!b->nblock_ents) return;
    seal_block(&b->block, b->nblock_ents);
    unsigned char hdr[32];
    size_t at = varint_put(hdr, b->last.n);
    gsbuf_append(&b->index, hdr, at);
    gsbuf_append(&b->index, b->last.p, b->last.n);
    unsigned char t[20];
    put64(t, b->lastseq);
    put64(t + 8, b->off);
    put32(t + 16, (uint32_t)b->block.n);
    gsbuf_append(&b->index, t, 20);
    bld_write(b, b->block.p, b->block.n);
    b->block.n = 0;
    b->nblock_ents = 0;
}

/* Write a long value to this builder's blob file; `ptr` gets the pointer. */
static int bld_blob(Builder *b, const unsigned char *v, size_t vn, unsigned char *ptr) {
    GsStore *db = b->db;
    if (!b->bf) {
        b->bnum = db->next_file++;
        if (b->track) u64_push(b->track, b->bnum); /* garbage until named */
        char *name = file_name(db, b->bnum); /* the other kind, as bld_open does */
        gsql_io_remove(db->io, name);
        free(name);
        name = blob_name(db, b->bnum);
        int rc = ioerr(gsql_io_open(db->io, name, GSQL_IO_CREATE, &b->bf));
        if (rc == GS_OK) rc = ioerr(gsql_io_truncate(b->bf, 0));
        if (rc) {
            if (b->bf) gsql_io_close(b->bf);
            b->bf = NULL;
            gsql_io_remove(db->io, name);
            free(name);
            return rc;
        }
        free(name);
        b->boff = 0;
        u64_push(&b->blobs, b->bnum);
    }
    int rc = ioerr(gsql_io_write_at(b->bf, b->boff, v, vn));
    if (rc) return rc;
    db->st.bytes_written += vn;
    put64(ptr, b->bnum);
    put64(ptr + 8, b->boff);
    put32(ptr + 16, (uint32_t)vn);
    put32(ptr + 20, crc32c(v, vn));
    b->boff += vn;
    return GS_OK;
}

static void bld_add(Builder *b, const Ent *in) {
    if (b->rc) return;
    Ent sep;
    const Ent *e = in;
    unsigned char ptr[BLOB_PTR];
    if (in->type == TYPE_PUT && b->db->vsep && in->vn >= b->db->vsep) {
        if ((b->rc = bld_blob(b, in->v, in->vn, ptr))) return;
        sep = *in;
        sep.type = TYPE_BLOB;
        sep.v = ptr;
        sep.vn = BLOB_PTR;
        e = &sep;
    } else if (in->type == TYPE_BLOB && in->vn == BLOB_PTR) {
        uint64_t num = get64(in->v);
        if (!u64_has(&b->blobs, num)) u64_push(&b->blobs, num);
    }
    if (!b->entries) { gsbuf_set(&b->smallest, e->k, e->kn); b->small_seq = e->seq; }
    /* one hash per distinct key: a version of the same key adds nothing */
    if (!b->entries || b->last.n != e->kn || memcmp(b->last.p, e->k, e->kn)) u64_push(&b->hashes, key_hash64(e->k, e->kn));
    unsigned char hdr[32];
    size_t at = varint_put(hdr, e->kn);
    gsbuf_append(&b->block, hdr, at);
    gsbuf_append(&b->block, e->k, e->kn);
    unsigned char t[9];
    put64(t, e->seq);
    t[8] = (unsigned char)e->type;
    gsbuf_append(&b->block, t, 9);
    at = varint_put(hdr, e->vn);
    gsbuf_append(&b->block, hdr, at);
    gsbuf_append(&b->block, e->v, e->vn);
    gsbuf_set(&b->last, e->k, e->kn);
    b->lastseq = e->seq;
    b->nblock_ents++;
    b->entries++;
    if (b->block.n >= b->db->block) bld_finish_block(b);
}

/* Finish, flush and describe the file. The caller names it in a manifest. */
static int bld_close(Builder *b, FileMeta **out) {
    bld_finish_block(b);
    uint64_t index_off = b->off;
    unsigned char t[8];
    put32(t, (uint32_t)b->entries);
    gsbuf_append(&b->index, t, 4);
    put32(t, crc32c(b->index.p, b->index.n));
    gsbuf_append(&b->index, t, 4);
    bld_write(b, b->index.p, b->index.n);
    /* bloom: 10 bits a key, 7 probes (~1% false positives) */
    size_t bits = b->hashes.n * 10 + 64;
    GsBuf bloom = {0};
    gsbuf_reserve(&bloom, bits / 8 + 16);
    memset(bloom.p, 0, bits / 8 + 1);
    bloom.n = bits / 8 + 1;
    for (size_t i = 0; i < b->hashes.n; i++) {
        uint64_t h = b->hashes.v[i], d = (h >> 33) | (h << 31);
        for (unsigned j = 0; j < 7; j++) {
            size_t bit = (size_t)((h + j * d) % (bits));
            bloom.p[bit / 8] |= (unsigned char)(1u << (bit % 8));
        }
    }
    uint64_t bloom_off = b->off;
    put32(t, (uint32_t)bits);
    gsbuf_append(&bloom, t, 4);
    put32(t, crc32c(bloom.p, bloom.n));
    gsbuf_append(&bloom, t, 4);
    bld_write(b, bloom.p, bloom.n);
    unsigned char foot[FOOTER];
    memset(foot, 0, sizeof foot);
    put64(foot, index_off);
    put32(foot + 8, (uint32_t)b->index.n);
    put64(foot + 12, bloom_off);
    put32(foot + 20, (uint32_t)bloom.n);
    put64(foot + 24, b->entries);
    put64(foot + 32, SST_MAGIC);
    put32(foot + 44, crc32c(foot, 44));
    bld_write(b, foot, FOOTER);
    /* the blob file before the sorted file: nothing may point at a value
     * that a power loss could take */
    if (b->rc == GS_OK && b->bf) b->rc = flush_file(b->db, b->bf);
    if (b->rc == GS_OK) b->rc = flush_file(b->db, b->f);
    if (b->bf) { gsql_io_close(b->bf); b->bf = NULL; }
    gsbuf_free(&bloom);
    gsbuf_free(&b->block);
    gsbuf_free(&b->index);
    u64_free(&b->hashes);
    int rc = b->rc;
    if (rc == GS_OK && b->entries) {
        FileMeta *m = calloc(1, sizeof *m);
        m->num = b->num;
        m->size = b->off;
        m->entries = b->entries;
        m->smallest = b->smallest;
        m->small_seq = b->small_seq;
        gsbuf_set(&m->largest, b->last.p, b->last.n);
        m->large_seq = b->lastseq;
        m->f = b->f;
        m->db = b->db;
        m->blobs = b->blobs;
        m->own_blob = b->bnum;
        for (size_t i = 0; i < m->blobs.n; i++) blob_ref(b->db, m->blobs.v[i]);
        *out = m;
    } else {
        gsbuf_free(&b->smallest);
        gsql_io_close(b->f);
        *out = NULL;
        /* Nothing went in, or writing it failed: either way nothing names it,
         * so it goes. A failed one used to stay, and once a later manifest
         * carried a higher next-file number it stayed for good (found on a
         * full disk, where every retried flush left one more). */
        char *name = file_name(b->db, b->num);
        gsql_io_remove(b->db->io, name);
        free(name);
        if (b->bnum) { name = blob_name(b->db, b->bnum); gsql_io_remove(b->db->io, name); free(name); }
        u64_free(&b->blobs);
    }
    gsbuf_free(&b->last);
    return rc;
}

/* ---- reading a sorted file -------------------------------------------------- */

static void file_close(FileMeta *m) {
    if (m->f) gsql_io_close(m->f);
    m->f = NULL;
    for (size_t i = 0; i < m->nindex; i++) free(m->index[i].last);
    free(m->index);
    m->index = NULL;
    m->nindex = 0;
    free(m->bloom);
    m->bloom = NULL;
}

static void file_free(FileMeta *m) {
    file_close(m);
    if (m->db) for (size_t i = 0; i < m->blobs.n; i++) blob_unref(m->db, m->blobs.v[i], m->obsolete);
    u64_free(&m->blobs);
    gsbuf_free(&m->smallest);
    gsbuf_free(&m->largest);
    free(m);
}

static int read_exact(GsStore *db, GsqlFile *f, uint64_t off, unsigned char *buf, size_t n) {
    size_t got;
    int rc = ioerr(gsql_io_read_at(f, off, buf, n, &got));
    if (rc) return rc;
    db->st.pages_read++;
    return got == n ? GS_OK : GS_CORRUPT;
}

static int check_block(const unsigned char *b, size_t n) {
    if (n < 8) return GS_CORRUPT;
    return get32(b + n - 4) == crc32c(b, n - 4) ? GS_OK : GS_CORRUPT;
}

static int file_open(GsStore *db, FileMeta *m) {
    if (m->index) return GS_OK;
    int rc = GS_OK;
    if (!m->f) {
        char *name = file_name(db, m->num);
        rc = ioerr(gsql_io_open(db->io, name, GSQL_IO_READ_ONLY, &m->f));
        if (rc && getenv("GS_DEBUG")) fprintf(stderr, "lsm: %s is missing\n", name);
        free(name);
        if (rc) { m->f = NULL; return rc == GS_IO ? GS_CORRUPT : rc; } /* a named file that is gone */
    }
    uint64_t size = 0;
    gsql_io_size(m->f, &size);
    if (size < FOOTER) return GS_CORRUPT;
    unsigned char foot[FOOTER];
    if ((rc = read_exact(db, m->f, size - FOOTER, foot, FOOTER))) return rc;
    if (get64(foot + 32) != SST_MAGIC || get32(foot + 44) != crc32c(foot, 44)) return GS_CORRUPT;
    uint64_t ioff = get64(foot), boff = get64(foot + 12);
    uint32_t isz = get32(foot + 8), bsz = get32(foot + 20);
    if (ioff + isz > size || boff + bsz > size) return GS_CORRUPT;
    unsigned char *ib = malloc(isz ? isz : 1), *bb = malloc(bsz ? bsz : 1);
    if ((rc = read_exact(db, m->f, ioff, ib, isz)) || (rc = check_block(ib, isz)) ||
        (rc = read_exact(db, m->f, boff, bb, bsz)) || (rc = check_block(bb, bsz))) {
        free(ib); free(bb); return rc ? rc : GS_CORRUPT;
    }
    size_t at = 0, end = isz - 8, cap = 16;
    m->index = malloc(cap * sizeof *m->index);
    m->nindex = 0;
    while (at < end) {
        uint64_t kn;
        size_t a = varint_get(ib + at, end - at, &kn);
        if (!a || at + a + kn + 20 > end) { rc = GS_CORRUPT; break; }
        if (m->nindex == cap) { cap *= 2; m->index = realloc(m->index, cap * sizeof *m->index); }
        IdxEntry *e = &m->index[m->nindex++];
        e->lastn = (size_t)kn;
        e->last = malloc(kn ? kn : 1);
        memcpy(e->last, ib + at + a, kn);
        at += a + kn;
        e->lastseq = get64(ib + at);
        e->off = get64(ib + at + 8);
        e->size = get32(ib + at + 16);
        at += 20;
        if (e->off + e->size > ioff) { rc = GS_CORRUPT; break; }
    }
    free(ib);
    if (rc == GS_OK) {
        m->bloom_bits = get32(bb + bsz - 8);
        m->bloom = bb;
        if (m->bloom_bits / 8 + 1 != bsz - 8) rc = GS_CORRUPT;
    } else {
        free(bb);
    }
    if (rc) file_close(m);
    if (rc && getenv("GS_DEBUG")) fprintf(stderr, "lsm: file %llu does not open (%d)\n", (unsigned long long)m->num, rc);
    return rc;
}

static uint64_t eseq(const FileMeta *m, uint64_t s) { return m->seq_over ? m->seq_over : s; }

static int bloom_may(const FileMeta *m, const unsigned char *k, size_t kn) {
    uint64_t h = key_hash64(k, kn), d = (h >> 33) | (h << 31);
    for (unsigned j = 0; j < 7; j++) {
        size_t bit = (size_t)((h + j * d) % m->bloom_bits);
        if (!(m->bloom[bit / 8] & (1u << (bit % 8)))) return 0;
    }
    return 1;
}

/* A decoded data block: entries point into `raw`. */
typedef struct {
    unsigned char *raw; /* owned, or NULL for a view into the block cache */
    Ent *e;
    size_t n;
    size_t bytes;       /* in the cache: the whole allocation */
} Block;

static void block_free(Block *b) { free(b->raw); free(b->e); memset(b, 0, sizeof *b); }

static int block_read(GsStore *db, FileMeta *m, size_t bi, Block *out) {
    memset(out, 0, sizeof *out); /* an error path frees it */
    IdxEntry *ie = &m->index[bi];
    out->raw = malloc(ie->size ? ie->size : 1);
    int rc = read_exact(db, m->f, ie->off, out->raw, ie->size);
    if (!rc) rc = check_block(out->raw, ie->size);
    if (rc) { block_free(out); return rc ? rc : GS_CORRUPT; }
    uint32_t count = get32(out->raw + ie->size - 8);
    size_t end = ie->size - 8, at = 0;
    out->e = malloc((count + 1) * sizeof *out->e);
    out->n = 0;
    while (at < end && out->n < count) {
        uint64_t kn, vn;
        size_t a = varint_get(out->raw + at, end - at, &kn);
        if (!a || at + a + kn + 9 > end) { rc = GS_CORRUPT; break; }
        Ent *e = &out->e[out->n];
        e->k = out->raw + at + a;
        e->kn = (size_t)kn;
        at += a + kn;
        e->seq = get64(out->raw + at);
        e->type = out->raw[at + 8];
        at += 9;
        a = varint_get(out->raw + at, end - at, &vn);
        if (!a || at + a + vn > end) { rc = GS_CORRUPT; break; }
        e->v = out->raw + at + a;
        e->vn = (size_t)vn;
        at += a + vn;
        out->n++;
    }
    if (rc == GS_OK && (out->n != count || at != end)) rc = GS_CORRUPT;
    if (rc) block_free(out);
    return rc;
}

/* ---- the block cache -------------------------------------------------------
 * A decoded block is ONE allocation -- its entry table, then its bytes -- so
 * the cache can free it with free(). A block an iterator stands on is pinned
 * (the cache entry's `dirty` field counts pins) and is never evicted. */

static void bcache_evict(GsStore *db) {
    CPage *p = db->bcache.lru.prev;
    while (db->bcache_bytes > db->cache_bytes && p != &db->bcache.lru) {
        CPage *prev = p->prev;
        if (!p->dirty) {
            db->bcache_bytes -= ((Block *)p->buf)->bytes;
            cache_remove(&db->bcache, p);
        }
        p = prev;
    }
}

static int block_fetch(GsStore *db, FileMeta *m, size_t bi, CPage **pin, Block *view) {
    CPage *p = cache_find(&db->bcache, m->num, bi + 1);
    if (p) {
        db->st.cache_hits++;
        lru_unlink(p);
        lru_front(&db->bcache, p);
    } else {
        Block b;
        int rc = block_read(db, m, bi, &b);
        if (rc) return rc;
        size_t raw = m->index[bi].size, total = sizeof(Block) + b.n * sizeof(Ent) + raw;
        unsigned char *buf = malloc(total);
        if (!buf) { block_free(&b); return GS_IO; }
        Block *hb = (Block *)buf;
        Ent *ents = (Ent *)(buf + sizeof(Block));
        unsigned char *r = (unsigned char *)(ents + b.n);
        memcpy(r, b.raw, raw);
        for (size_t i = 0; i < b.n; i++) {
            ents[i] = b.e[i];
            ents[i].k = r + (b.e[i].k - b.raw);
            ents[i].v = r + (b.e[i].v - b.raw);
        }
        hb->raw = NULL;
        hb->e = ents;
        hb->n = b.n;
        hb->bytes = total;
        block_free(&b);
        p = cache_insert(&db->bcache, m->num, bi + 1, buf);
        if (!p) { free(buf); return GS_IO; }
        db->bcache_bytes += total;
    }
    p->dirty++;
    *pin = p;
    *view = *(Block *)p->buf;
    bcache_evict(db);
    return GS_OK;
}

/* ---- iterators over internal entries --------------------------------------- */

/* One source: the memtable, a txn buffer, one level-0 file, or a whole level
 * (its files are disjoint and sorted, so they read as one run). */
typedef struct {
    int kind;            /* 0 skiplist, 1 files */
    Skip *sk;
    SkNode *node;
    FileMeta **files;
    size_t nfiles, fi;   /* current file */
    size_t bi;           /* current block in it */
    Block blk;           /* a view of the pinned cached block */
    CPage *pin;
    int priv;            /* the writer's own buffer or spilled run: always visible */
    size_t ei;           /* current entry in it */
    int valid;
    GsStore *db;
    int rc;
} Src;

static void src_unpin(Src *s) {
    if (s->pin) s->pin->dirty--;
    s->pin = NULL;
    memset(&s->blk, 0, sizeof s->blk);
}

static void src_reset(Src *s) { src_unpin(s); s->valid = 0; }

static int src_load(Src *s, size_t fi, size_t bi) {
    src_unpin(s);
    FileMeta *m = s->files[fi];
    int rc = file_open(s->db, m);
    if (!rc) rc = block_fetch(s->db, m, bi, &s->pin, &s->blk);
    if (rc) { s->rc = rc; s->valid = 0; return rc; }
    s->fi = fi;
    s->bi = bi;
    return GS_OK;
}

static Ent src_ent(const Src *s) {
    if (s->kind == 0) return sk_ent(s->node);
    Ent e = s->blk.e[s->ei];
    e.seq = eseq(s->files[s->fi], e.seq);
    return e;
}

/* first entry >= (k, seq) */
static void src_seek(Src *s, const unsigned char *k, size_t kn, uint64_t seq) {
    s->valid = 0;
    if (s->kind == 0) {
        s->node = sk_ge(s->sk, k, kn, seq);
        s->valid = s->node != NULL;
        return;
    }
    src_unpin(s);
    /* the first file whose largest key is >= k */
    size_t lo = 0, hi = s->nfiles;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        FileMeta *m = s->files[mid];
        if (ent_cmp(m->largest.p, m->largest.n, eseq(m, m->large_seq), k, kn, seq) < 0) lo = mid + 1;
        else hi = mid;
    }
    for (size_t fi = lo; fi < s->nfiles; fi++) {
        FileMeta *m = s->files[fi];
        if (file_open(s->db, m)) { s->rc = GS_CORRUPT; return; }
        size_t blo = 0, bhi = m->nindex;
        while (blo < bhi) {
            size_t mid = (blo + bhi) / 2;
            IdxEntry *e = &m->index[mid];
            if (ent_cmp(e->last, e->lastn, eseq(m, e->lastseq), k, kn, seq) < 0) blo = mid + 1;
            else bhi = mid;
        }
        if (blo == m->nindex) continue;
        if (src_load(s, fi, blo)) return;
        size_t elo = 0, ehi = s->blk.n;
        while (elo < ehi) {
            size_t mid = (elo + ehi) / 2;
            Ent *e = &s->blk.e[mid];
            if (ent_cmp(e->k, e->kn, eseq(m, e->seq), k, kn, seq) < 0) elo = mid + 1;
            else ehi = mid;
        }
        if (elo < s->blk.n) { s->ei = elo; s->valid = 1; return; }
    }
}

static void src_next(Src *s) {
    if (!s->valid) return;
    if (s->kind == 0) { s->node = s->node->next[0]; s->valid = s->node != NULL; return; }
    if (++s->ei < s->blk.n) return;
    size_t fi = s->fi, bi = s->bi + 1;
    for (;;) {
        if (bi >= s->files[fi]->nindex) {
            if (++fi >= s->nfiles) { s->valid = 0; return; }
            if (file_open(s->db, s->files[fi])) { s->rc = GS_CORRUPT; s->valid = 0; return; }
            bi = 0;
            continue;
        }
        if (src_load(s, fi, bi)) return;
        if (s->blk.n) { s->ei = 0; return; }
        bi++;
    }
}

/* last entry < (k, seq) */
static void src_seek_lt(Src *s, const unsigned char *k, size_t kn, uint64_t seq) {
    s->valid = 0;
    if (s->kind == 0) {
        s->node = sk_lt(s->sk, k, kn, seq);
        s->valid = s->node != NULL;
        return;
    }
    src_seek(s, k, kn, seq);
    if (s->rc) return;
    if (s->valid) {
        if (s->ei > 0) { s->ei--; return; }
        /* step back a block (or a file) */
        size_t fi = s->fi, bi = s->bi;
        for (;;) {
            if (bi > 0) bi--;
            else {
                if (fi == 0) { s->valid = 0; src_unpin(s); return; }
                fi--;
                if (file_open(s->db, s->files[fi])) { s->rc = GS_CORRUPT; s->valid = 0; return; }
                if (!s->files[fi]->nindex) continue;
                bi = s->files[fi]->nindex - 1;
            }
            if (src_load(s, fi, bi)) return;
            if (s->blk.n) { s->ei = s->blk.n - 1; s->valid = 1; return; }
        }
    }
    /* nothing >= target: the very last entry, if any */
    for (size_t fi = s->nfiles; fi-- > 0;) {
        FileMeta *m = s->files[fi];
        if (file_open(s->db, m)) { s->rc = GS_CORRUPT; return; }
        for (size_t bi = m->nindex; bi-- > 0;) {
            if (src_load(s, fi, bi)) return;
            if (s->blk.n) { s->ei = s->blk.n - 1; s->valid = 1; return; }
        }
    }
}

/* ---- versions ---------------------------------------------------------------- */

static Mem *mem_new(void) {
    Mem *m = calloc(1, sizeof *m);
    sk_init(&m->sk);
    m->refs = 1;
    return m;
}

static void mem_unref(Mem *m) {
    if (m && --m->refs == 0) { sk_free(&m->sk); free(m); }
}

static Version *version_copy(const Version *v) {
    Version *n = calloc(1, sizeof *n);
    if (v) { n->mem = v->mem; n->mem->refs++; }
    else n->mem = mem_new();
    for (int l = 0; l < LEVELS; l++) {
        n->nfiles[l] = v ? v->nfiles[l] : 0;
        n->files[l] = malloc((n->nfiles[l] + 1) * sizeof *n->files[l]);
        for (size_t i = 0; i < n->nfiles[l]; i++) {
            n->files[l][i] = v->files[l][i];
            n->files[l][i]->refs++;
        }
    }
    n->refs = 1;
    return n;
}

static void file_drop_ref(GsStore *db, FileMeta *m) {
    if (--m->refs > 0) return;
    if (m->obsolete) {
        char *name = file_name(db, m->num);
        file_close(m);
        gsql_io_remove(db->io, name);
        /* No longer garbage. The deletion is not durable yet, and needs no
         * list of its own: the manifest that first omits this file flushes
         * the directory before its rename, which makes the deletion durable,
         * and until then the manifest on disk still lists it as garbage. */
        for (size_t i = 0; i < db->garbage.n; i++)
            if (db->garbage.v[i] == m->num) { db->garbage.v[i] = db->garbage.v[--db->garbage.n]; break; }
        free(name);
    }
    file_free(m);
}

static void version_unref(GsStore *db, Version *v) {
    if (!v || --v->refs > 0) return;
    for (int l = 0; l < LEVELS; l++) {
        for (size_t i = 0; i < v->nfiles[l]; i++) file_drop_ref(db, v->files[l][i]);
        free(v->files[l]);
    }
    mem_unref(v->mem);
    free(v);
}

static void version_add(Version *v, int level, FileMeta *m) {
    v->files[level] = realloc(v->files[level], (v->nfiles[level] + 2) * sizeof *v->files[level]);
    size_t i = v->nfiles[level];
    if (level == 0) {
        /* level 0: in creation order, oldest first */
        v->files[0][i] = m;
    } else {
        while (i > 0 && bytes_cmp(gsb(v->files[level][i - 1]->smallest.p, v->files[level][i - 1]->smallest.n),
                                  gsb(m->smallest.p, m->smallest.n)) > 0) {
            v->files[level][i] = v->files[level][i - 1];
            i--;
        }
        v->files[level][i] = m;
    }
    v->nfiles[level]++;
    m->refs++;
}

static void version_remove(GsStore *db, Version *v, int level, FileMeta *m) {
    for (size_t i = 0; i < v->nfiles[level]; i++)
        if (v->files[level][i] == m) {
            memmove(&v->files[level][i], &v->files[level][i + 1], (v->nfiles[level] - i - 1) * sizeof *v->files[level]);
            v->nfiles[level]--;
            file_drop_ref(db, m);
            return;
        }
}

/* ---- the manifest ------------------------------------------------------------ */

static void put_bytes(GsBuf *b, const unsigned char *p, size_t n) {
    unsigned char h[10];
    gsbuf_append(b, h, varint_put(h, n));
    gsbuf_append(b, p, n);
}
static void put_u64(GsBuf *b, uint64_t v) { unsigned char t[8]; put64(t, v); gsbuf_append(b, t, 8); }

/* A compaction in progress. It runs in slices, a little in each commit, so
 * that no one commit pays for a whole merge (measured before this: a 1.85 s
 * p99 on W3 larger than memory, against RocksDB's 35 ms). Only one runs at a
 * time; the only thing that changes the levels meanwhile is a flush, which
 * adds to level 0 and touches none of the inputs. */
typedef struct Comp {
    int level, next, bottom;
    FileMeta **inputs;
    size_t nin, first_below;
    Src *src;
    size_t ns;
    Builder b;
    int open;
    GsBuf cur;
    int have_cur, kept_visible;
    uint64_t keep_after, target;
    FileMeta **outs;
    size_t nouts;
    /* Every output's number, finished or not. They are named in no version
     * until the compaction installs, and a flush meanwhile writes a manifest
     * with a higher next-file number, so a crash would orphan them for good.
     * Every manifest written meanwhile lists them as garbage instead, and the
     * next open deletes them. */
    U64s pending;
    /* Blob space comes back by relocation: a value this compaction keeps
     * from a blob file numbered below gc_below is written again, into the
     * output's own blob, so the oldest files lose their pointers and go.
     * Without it a file stays whole while any one of its values is live. */
    uint64_t gc_below;
    GsBuf reloc;
} Comp;

static int manifest_write(GsStore *db) {
    GsBuf b = {0};
    unsigned char hdr[GS_HEADER_FIXED];
    GsFileHeader h = {GS_FORMAT_MAJOR, 0, db->block, {0}, 0, 0};
    memcpy(h.identity, db->identity, 16);
    gs_header_write(hdr, &h);
    gsbuf_append(&b, hdr, sizeof hdr);
    put_u64(&b, db->next_file);
    put_u64(&b, db->log_first);
    put_u64(&b, db->last_seq);
    for (int l = 0; l < LEVELS; l++) {
        put_u64(&b, db->cur->nfiles[l]);
        for (size_t i = 0; i < db->cur->nfiles[l]; i++) {
            FileMeta *m = db->cur->files[l][i];
            put_u64(&b, m->num);
            put_u64(&b, m->size);
            put_u64(&b, m->entries);
            put_bytes(&b, m->smallest.p, m->smallest.n);
            put_u64(&b, m->small_seq);
            put_bytes(&b, m->largest.p, m->largest.n);
            put_u64(&b, m->large_seq);
            put_u64(&b, m->seq_over);
            put_u64(&b, m->own_blob);
            put_u64(&b, m->blobs.n);
            for (size_t j = 0; j < m->blobs.n; j++) put_u64(&b, m->blobs.v[j]);
        }
    }
    /* every blob file alive now, so an open can delete one whose deletion a
     * crash undid: listed here, pointed into by nothing in the version */
    put_u64(&b, db->nblob);
    for (size_t i = 0; i < db->nblob; i++) put_u64(&b, db->blob[i].num);
    put_u64(&b, db->ndead);
    for (size_t i = 0; i < db->ndead; i++) { put_u64(&b, db->dead[i].id); put_u64(&b, db->dead[i].death); }
    size_t npend = db->comp ? db->comp->pending.n : 0;
    put_u64(&b, db->garbage.n + npend);
    for (size_t i = 0; i < db->garbage.n; i++) put_u64(&b, db->garbage.v[i]);
    for (size_t i = 0; i < npend; i++) put_u64(&b, db->comp->pending.v[i]);
    unsigned char t[4];
    put32(t, crc32c(b.p, b.n));
    gsbuf_append(&b, t, 4);
    char *tmp = gs_path_with(db->path, ".tmp");
    GsqlFile *f = NULL;
    gsql_io_remove(db->io, tmp);
    int rc = ioerr(gsql_io_open(db->io, tmp, GSQL_IO_CREATE | GSQL_IO_EXCLUSIVE, &f));
    if (rc == GS_OK) {
        rc = ioerr(gsql_io_write_at(f, 0, b.p, b.n));
        if (rc == GS_OK) { db->st.bytes_written += b.n; rc = flush_file(db, f); }
        gsql_io_close(f);
    }
    /* Rule 2, twice. The files this manifest names were created since the
     * last directory flush, and a creation is durable only after one: flushed
     * after the rename alone, a power loss may keep the rename and lose the
     * creations, and recovery reads a manifest naming files that are not
     * there (found by the crash harness: pending namespace operations survive
     * as any subsequence). So flush the directory, rename, flush it again. */
    if (rc == GS_OK) {
        rc = ioerr(gsql_io_sync_dir(db->io, db->dir));
        db->st.flushes++;
        if (rc) db->failed = 1;
    }
    if (rc == GS_OK) rc = ioerr(gsql_io_rename(db->io, tmp, db->path));
    if (rc == GS_OK) {
        rc = ioerr(gsql_io_sync_dir(db->io, db->dir));
        db->st.flushes++;
        if (rc) db->failed = 1;
    }
    if (rc && rc != GS_IO) gsql_io_remove(db->io, tmp);
    free(tmp);
    gsbuf_free(&b);
    return rc;
}

static int get_bytes(const unsigned char *p, size_t n, size_t *at, GsBuf *out) {
    uint64_t len;
    size_t a = varint_get(p + *at, n - *at, &len);
    if (!a || *at + a + len > n) return GS_CORRUPT;
    gsbuf_set(out, p + *at + a, (size_t)len);
    *at += a + (size_t)len;
    return GS_OK;
}
static int get_u64(const unsigned char *p, size_t n, size_t *at, uint64_t *v) {
    if (*at + 8 > n) return GS_CORRUPT;
    *v = get64(p + *at);
    *at += 8;
    return GS_OK;
}

static int manifest_read(GsStore *db, const unsigned char *p, size_t n) {
    if (n < GS_HEADER_FIXED + 4 || get32(p + n - 4) != crc32c(p, n - 4)) return GS_CORRUPT;
    GsFileHeader h;
    int rc = gs_header_read(p, n, &h);
    if (rc) return rc;
    db->block = h.page_size;
    memcpy(db->identity, h.identity, 16);
    size_t at = GS_HEADER_FIXED, end = n - 4;
    if ((rc = get_u64(p, end, &at, &db->next_file)) || (rc = get_u64(p, end, &at, &db->log_num)) ||
        (rc = get_u64(p, end, &at, &db->last_seq)))
        return rc;
    db->cur = version_copy(NULL);
    for (int l = 0; l < LEVELS; l++) {
        uint64_t cnt;
        if ((rc = get_u64(p, end, &at, &cnt))) return rc;
        if (cnt > end) return GS_CORRUPT;
        for (uint64_t i = 0; i < cnt; i++) {
            FileMeta *m = calloc(1, sizeof *m);
            if ((rc = get_u64(p, end, &at, &m->num)) || (rc = get_u64(p, end, &at, &m->size)) ||
                (rc = get_u64(p, end, &at, &m->entries)) || (rc = get_bytes(p, end, &at, &m->smallest)) ||
                (rc = get_u64(p, end, &at, &m->small_seq)) || (rc = get_bytes(p, end, &at, &m->largest)) ||
                (rc = get_u64(p, end, &at, &m->large_seq)) || (rc = get_u64(p, end, &at, &m->seq_over)) ||
                (rc = get_u64(p, end, &at, &m->own_blob))) {
                file_free(m);
                return rc;
            }
            uint64_t nb;
            if ((rc = get_u64(p, end, &at, &nb)) || nb > end) { file_free(m); return rc ? rc : GS_CORRUPT; }
            for (uint64_t j = 0; j < nb; j++) {
                uint64_t bn;
                if ((rc = get_u64(p, end, &at, &bn))) { file_free(m); return rc; }
                u64_push(&m->blobs, bn);
            }
            m->db = db;
            for (size_t j = 0; j < m->blobs.n; j++) blob_ref(db, m->blobs.v[j]);
            version_add(db->cur, l, m);
        }
    }
    uint64_t nlive;
    if ((rc = get_u64(p, end, &at, &nlive))) return rc;
    if (nlive > end) return GS_CORRUPT;
    for (uint64_t i = 0; i < nlive; i++) {
        uint64_t bn;
        if ((rc = get_u64(p, end, &at, &bn))) return rc;
        if (!blob_find(db, bn)) {
            /* listed alive, and nothing in the version points into it: its
             * last reference went and the deletion did not survive */
            char *name = blob_name(db, bn);
            gsql_io_remove(db->io, name);
            free(name);
        }
    }
    uint64_t nd;
    if ((rc = get_u64(p, end, &at, &nd))) return rc;
    if (nd > end) return GS_CORRUPT;
    for (uint64_t i = 0; i < nd; i++) {
        Dead d;
        if ((rc = get_u64(p, end, &at, &d.id)) || (rc = get_u64(p, end, &at, &d.death))) return rc;
        if (db->ndead == db->capdead) { db->capdead = db->capdead ? db->capdead * 2 : 8; db->dead = realloc(db->dead, db->capdead * sizeof *db->dead); }
        db->dead[db->ndead++] = d;
    }
    uint64_t ng;
    if ((rc = get_u64(p, end, &at, &ng))) return rc;
    if (ng > end) return GS_CORRUPT;
    for (uint64_t i = 0; i < ng; i++) {
        uint64_t g;
        if ((rc = get_u64(p, end, &at, &g))) return rc;
        u64_push(&db->garbage, g);
    }
    return at == end ? GS_OK : GS_CORRUPT;
}

/* ---- the log ------------------------------------------------------------------ */

/* record: u32 len, u32 crc, payload:
 *   u64 base seq, u32 count, count x (u8 type, varint klen, key, varint vlen, value),
 *   u32 retired, retired x u64 id */

static int dead_add(GsStore *db, uint64_t id, uint64_t death) {
    for (size_t i = 0; i < db->ndead; i++) if (db->dead[i].id == id) return GS_OK;
    if (db->ndead == db->capdead) {
        db->capdead = db->capdead ? db->capdead * 2 : 8;
        Dead *d = realloc(db->dead, db->capdead * sizeof *d);
        if (!d) return GS_IO;
        db->dead = d;
    }
    db->dead[db->ndead].id = id;
    db->dead[db->ndead].death = death;
    db->ndead++;
    return GS_OK;
}

/* Replay one log into the memtable; *end gets the offset after its last valid
 * record. */
static int replay_log(GsStore *db, GsqlFile *f, uint64_t *end) {
    uint64_t size = 0;
    gsql_io_size(f, &size);
    uint64_t at = 0;
    unsigned char h[8];
    GsBuf rec = {0};
    int rc = GS_OK;
    for (;;) {
        size_t got;
        if (at + 8 > size) break;
        if ((rc = ioerr(gsql_io_read_at(f, at, h, 8, &got))) || got != 8) break;
        uint32_t len = get32(h), crc = get32(h + 4);
        if (len < 16 || at + 8 + len > size) break;
        if (!gsbuf_reserve(&rec, len)) { rc = GS_IO; break; }
        if ((rc = ioerr(gsql_io_read_at(f, at + 8, rec.p, len, &got))) || got != len) break;
        if (crc32c(rec.p, len) != crc) break;
        const unsigned char *p = rec.p;
        size_t q = 12;
        uint64_t base = get64(p);
        uint32_t count = get32(p + 8);
        int bad = 0;
        for (uint32_t i = 0; i < count && !bad; i++) {
            uint64_t kn, vn;
            if (q + 1 > len) { bad = 1; break; }
            int type = p[q++];
            size_t a = varint_get(p + q, len - q, &kn);
            if (!a || q + a + kn > len) { bad = 1; break; }
            const unsigned char *k = p + q + a;
            q += a + kn;
            a = varint_get(p + q, len - q, &vn);
            if (!a || q + a + vn > len) { bad = 1; break; }
            if (sk_insert(&db->cur->mem->sk, k, (size_t)kn, base + i, type, p + q + a, (size_t)vn, 0)) { bad = 1; break; }
            q += a + vn;
        }
        if (bad || q + 4 > len) break;
        uint32_t nret = get32(p + q);
        q += 4;
        if (q + 8ull * nret != len) break;
        uint64_t commit = count ? base + count - 1 : base;
        for (uint32_t i = 0; i < nret; i++) dead_add(db, get64(p + q + 8 * i), commit);
        if (commit > db->last_seq) db->last_seq = commit;
        at += 8 + len;
    }
    gsbuf_free(&rec);
    *end = at;
    return rc;
}

/* ---- creation, recovery --------------------------------------------------------- */

/* A new log is filled with zeros up to twice the memtable's size, so commit
 * records overwrite existing bytes and a commit's flush does not also commit
 * a change of file size (250 us against 1 ms on this machine). Replay stops at
 * the first record whose length is zero, so the zeros read as the end. */
static int prefill_log(GsStore *db, GsqlFile *f, uint64_t from) {
    uint64_t size = db->memtable_limit * 2 + 65536;
    if (from >= size) return GS_OK;
    static const unsigned char zero[65536];
    for (uint64_t at = from; at < size;) {
        size_t n = size - at < sizeof zero ? (size_t)(size - at) : sizeof zero;
        int rc = ioerr(gsql_io_write_at(f, at, zero, n));
        if (rc) return rc;
        db->st.bytes_written += n;
        at += n;
    }
    return GS_OK;
}

/* After replay: whatever a lost commit left past the last valid record goes.
 * It can sit anywhere beyond -- behind a lost chunk as well as right after the
 * end -- and a record carries its own checksum, so a stale one is accepted
 * wherever a new record happens to end where it begins (2.B's crash run found
 * the same hole in its log: stopping at the first gap misses what is behind
 * it). Truncate, then refill with zeros. Recovery is rare; it pays the refill. */
static int zero_tail(GsStore *db, GsqlFile *f, uint64_t from) {
    int rc = ioerr(gsql_io_truncate(f, from));
    if (rc == GS_OK) rc = prefill_log(db, f, from);
    if (rc == GS_OK) rc = flush_file(db, f);
    return rc;
}

static int open_log(GsStore *db, uint64_t num, int create) {
    char *name = log_name(db->path, num);
    int rc = ioerr(gsql_io_open(db->io, name, create ? GSQL_IO_CREATE : 0, &db->log));
    free(name);
    if (rc) db->log = NULL;
    return rc;
}

static void comp_free(GsStore *db, int abandon);

static void store_free(GsStore *db) {
    comp_free(db, 1); /* an unfinished compaction's outputs are named nowhere */
    if (db->log) gsql_io_close(db->log);
    if (db->lockf) gsql_io_close(db->lockf);
    version_unref(db, db->cur);
    for (size_t i = 0; i < db->nblob; i++) if (db->blob[i].f) gsql_io_close(db->blob[i].f);
    free(db->blob);
    if (db->bcache.bucket) cache_free(&db->bcache);
    free(db->dead);
    u64_free(&db->garbage);
    free(db->snaps);
    if (db->own_io) gsql_io_free(db->io);
    free(db->path);
    free(db->dir);
    free(db);
}

static int create_db(GsStore *db) {
    db->next_file = 1;
    db->log_num = 1;
    db->log_first = 1;
    db->last_seq = 1;
    gs_identity_new(db->io, db->identity);
    db->cur = version_copy(NULL);
    /* the log first, then the manifest naming it; the manifest's directory
     * flush makes both names durable */
    char *name = log_name(db->path, 1);
    gsql_io_remove(db->io, name);
    free(name);
    int rc = open_log(db, 1, 1);
    if (rc == GS_OK) rc = ioerr(gsql_io_truncate(db->log, 0));
    if (rc == GS_OK) rc = prefill_log(db, db->log, 0);
    if (rc == GS_OK) rc = flush_file(db, db->log);
    if (rc == GS_OK) rc = manifest_write(db);
    db->log_end = 0;
    db->durable_seq = db->last_seq;
    return rc;
}

static int lsm_open(const char *path, const GsOptions *opt, GsStore **out) {
    GsStore *db = calloc(1, sizeof *db);
    if (!db) return GS_IO;
    db->io = opt && opt->io ? opt->io : NULL;
    if (!db->io) { db->io = gsql_io_real(); db->own_io = 1; }
    db->path = gs_path_with(path, "");
    db->dir = gs_dirname(path);
    db->block = opt && opt->page_size ? opt->page_size : 4096;
    /* 32 MB: level 1 is ten times this and each level ten times the last,
     * so at the first default (4 MB) a 10 GB store was five or six levels
     * deep and a random load rewrote every row at each (measured: 25.5x
     * write amplification at 10 M rows against RocksDB's 8.2x). The cost is
     * a longer log to replay on open, and a larger zero-filled log. */
    db->memtable_limit = opt && opt->tune_a ? opt->tune_a : 32u << 20;
    db->l0_trigger = opt && opt->tune_b ? opt->tune_b : 4;
    /* 4 KiB: a YCSB row (1,000 bytes) stays inline, a 100 KiB value does not */
    db->vsep = opt && opt->tune_c ? opt->tune_c : 4096;
    db->cache_bytes = opt && opt->cache_bytes ? opt->cache_bytes : 64u << 20;
    cache_init(&db->bcache);
    /* BUSY: the log and the manifest are both replaced over a store's life, so
     * the exclusive lock is on a file of its own */
    char *lockname = gs_path_with(path, ".lock");
    int lrc = gsql_io_open(db->io, lockname, GSQL_IO_CREATE, &db->lockf);
    free(lockname);
    if (lrc) { db->lockf = NULL; store_free(db); return lrc == ENOENT ? GS_NOT_FOUND : ioerr(lrc); }
    if ((lrc = gsql_io_lock(db->lockf, 1))) { store_free(db); return ioerr(lrc); }
    GsqlFile *mf = NULL;
    int rc = gsql_io_open(db->io, path, 0, &mf);
    if (rc == ENOENT && opt && opt->create) {
        /* no manifest: creation never finished, or never began. A log without
         * a manifest naming it holds nothing committed, since no commit is
         * possible before the creating open returns. */
        rc = create_db(db);
    } else if (rc == ENOENT) {
        rc = GS_NOT_FOUND;
    } else if (rc == 0) {
        uint64_t size = 0;
        gsql_io_size(mf, &size);
        unsigned char *p = malloc(size ? size : 1);
        size_t got = 0;
        rc = ioerr(gsql_io_read_at(mf, 0, p, (size_t)size, &got));
        if (rc == GS_OK) rc = got == size ? manifest_read(db, p, (size_t)size) : GS_CORRUPT;
        free(p);
        gsql_io_close(mf);
        /* finish deleting what the last manifest said was garbage */
        for (size_t i = 0; rc == GS_OK && i < db->garbage.n; i++) {
            /* one number space: a garbage number is a sorted file or a blob */
            char *name = file_name(db, db->garbage.v[i]);
            gsql_io_remove(db->io, name);
            free(name);
            name = blob_name(db, db->garbage.v[i]);
            gsql_io_remove(db->io, name);
            free(name);
        }
        db->garbage.n = 0;
        /* replay every log from the manifest's onward */
        uint64_t k = db->log_num;
        db->log_first = k;
        while (rc == GS_OK) {
            GsqlFile *lf = NULL;
            char *name = log_name(db->path, k);
            int orc = gsql_io_open(db->io, name, 0, &lf);
            free(name);
            if (orc == ENOENT) break;
            if (orc) { rc = ioerr(orc); break; }
            uint64_t end = 0;
            rc = replay_log(db, lf, &end);
            if (db->log) gsql_io_close(db->log);
            db->log = lf;
            db->log_num = k;
            db->log_end = end;
            k++;
        }
        if (rc == GS_OK && !db->log) rc = open_log(db, db->log_num, 1);
        /* truncate the live log at its last valid record and flush it, so a
         * record a lost commit left past that point can never follow a new one */
        if (rc == GS_OK) {
            uint64_t sz = 0;
            gsql_io_size(db->log, &sz);
            if (sz > db->log_end) rc = zero_tail(db, db->log, db->log_end);
        }
        db->durable_seq = db->last_seq;
    } else {
        rc = ioerr(rc);
    }
    if (rc) { store_free(db); return rc; }
    *out = db;
    return GS_OK;
}

/* ---- snapshots and the oldest one ------------------------------------------- */

static uint64_t oldest_snapshot(GsStore *db) {
    uint64_t m = db->last_seq;
    for (size_t i = 0; i < db->nsnaps; i++) if (db->snaps[i] < m) m = db->snaps[i];
    return m;
}

static void snap_add(GsStore *db, uint64_t s) {
    if (db->nsnaps == db->capsnaps) { db->capsnaps = db->capsnaps ? db->capsnaps * 2 : 8; db->snaps = realloc(db->snaps, db->capsnaps * sizeof *db->snaps); }
    db->snaps[db->nsnaps++] = s;
}
static void snap_del(GsStore *db, uint64_t s) {
    for (size_t i = 0; i < db->nsnaps; i++) if (db->snaps[i] == s) { db->snaps[i] = db->snaps[--db->nsnaps]; return; }
}

static uint64_t death_of(GsStore *db, uint64_t id) {
    for (size_t i = 0; i < db->ndead; i++) if (db->dead[i].id == id) return db->dead[i].death;
    return 0;
}

/* ---- flush and compaction ----------------------------------------------------- */

/* Every output of a merge: keep each version a live snapshot needs. */
typedef struct {
    Src *src;
    size_t n;
} Merge;

static int merge_min(Merge *m) {
    int best = -1;
    Ent be = {0};
    for (size_t i = 0; i < m->n; i++) {
        if (!m->src[i].valid) continue;
        Ent e = src_ent(&m->src[i]);
        if (best < 0 || ent_cmp(e.k, e.kn, e.seq, be.k, be.kn, be.seq) < 0) { best = (int)i; be = e; }
    }
    return best;
}

static uint64_t level_bytes(Version *v, int l) {
    uint64_t s = 0;
    for (size_t i = 0; i < v->nfiles[l]; i++) s += v->files[l][i]->size;
    return s;
}

static uint64_t level_limit(GsStore *db, int l) {
    uint64_t lim = db->memtable_limit * 10;
    for (int i = 1; i < l; i++) lim *= 10;
    return lim;
}

static int overlaps(FileMeta *m, const GsBuf *lo, const GsBuf *hi) {
    return bytes_cmp(gsb(m->largest.p, m->largest.n), gsb(lo->p, lo->n)) >= 0 &&
           bytes_cmp(gsb(m->smallest.p, m->smallest.n), gsb(hi->p, hi->n)) <= 0;
}

/* Install a new version: name it in a manifest, then let the old one go. */
static int install(GsStore *db, Version *nv, FileMeta **gone, size_t ngone) {
    for (size_t i = 0; i < ngone; i++) { gone[i]->obsolete = 1; u64_push(&db->garbage, gone[i]->num); }
    Version *old = db->cur;
    db->cur = nv;
    int rc = manifest_write(db);
    if (rc) {
        db->cur = old;
        for (size_t i = 0; i < ngone; i++) gone[i]->obsolete = 0;
        db->garbage.n -= ngone;
        version_unref(db, nv);
        return rc;
    }
    version_unref(db, old);
    return GS_OK;
}

/* Whether these files' key ranges are pairwise disjoint. */
static int moveable(FileMeta **in, size_t n) {
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            if (bytes_cmp(gsb(in[i]->largest.p, in[i]->largest.n), gsb(in[j]->smallest.p, in[j]->smallest.n)) >= 0 &&
                bytes_cmp(gsb(in[j]->largest.p, in[j]->largest.n), gsb(in[i]->smallest.p, in[i]->smallest.n)) >= 0)
                return 0;
    return 1;
}

/* Drop a compaction. `abandon`: its outputs are named nowhere, so they go;
 * otherwise the version that installed it owns them. */
static void comp_free(GsStore *db, int abandon) {
    Comp *c = db->comp;
    if (!c) return;
    for (size_t i = 0; i < c->ns; i++) src_reset(&c->src[i]);
    free(c->src);
    if (c->open) {
        FileMeta *fm = NULL;
        bld_close(&c->b, &fm);
        if (fm) { c->outs = realloc(c->outs, (c->nouts + 1) * sizeof *c->outs); c->outs[c->nouts++] = fm; }
    }
    if (abandon) {
        for (size_t i = 0; i < c->nouts; i++) file_free(c->outs[i]);
        for (size_t i = 0; i < c->pending.n; i++) {
            char *name = file_name(db, c->pending.v[i]);
            gsql_io_remove(db->io, name);
            free(name);
            name = blob_name(db, c->pending.v[i]);
            gsql_io_remove(db->io, name);
            free(name);
        }
    }
    free(c->outs);
    free(c->inputs);
    gsbuf_free(&c->cur);
    gsbuf_free(&c->reloc);
    u64_free(&c->pending);
    free(c);
    db->comp = NULL;
}

static int comp_open_output(GsStore *db, Comp *c) {
    int rc = bld_open(db, &c->b);
    u64_push(&c->pending, c->b.num); /* garbage from now until installed */
    c->b.track = &c->pending;        /* and so is its blob file, when it takes one */
    if (rc == GS_OK) c->open = 1;
    return rc;
}

static int comp_close_output(Comp *c) {
    FileMeta *fm = NULL;
    c->open = 0; /* closed whatever it answers: never close it twice */
    int rc = bld_close(&c->b, &fm);
    if (fm) { c->outs = realloc(c->outs, (c->nouts + 1) * sizeof *c->outs); c->outs[c->nouts++] = fm; }
    return rc;
}

/* Install a finished compaction over whatever version is current now: a
 * flush may have added to level 0 since it began, and none of that is an
 * input. */
static int comp_install(GsStore *db) {
    Comp *c = db->comp;
    Version *nv = version_copy(db->cur);
    for (size_t i = 0; i < c->first_below; i++) version_remove(db, nv, c->level, c->inputs[i]);
    for (size_t i = c->first_below; i < c->nin; i++) version_remove(db, nv, c->next, c->inputs[i]);
    for (size_t i = 0; i < c->nouts; i++) version_add(nv, c->next, c->outs[i]);
    /* the outputs are named by the version now, so no longer garbage; if the
     * manifest write fails they become garbage again */
    U64s pend = c->pending;
    memset(&c->pending, 0, sizeof c->pending);
    int rc = install(db, nv, c->inputs, c->nin);
    if (rc) {
        for (size_t i = 0; i < pend.n; i++) u64_push(&db->garbage, pend.v[i]);
    }
    u64_free(&pend);
    c->nouts = 0; /* the version took them, or dropped them as garbage */
    comp_free(db, 0);
    return rc;
}

/* Merge up to `budget` input bytes. `*done` reports whether it finished. Keep
 * each version a live snapshot needs; `bottom`: no level below holds data, so
 * a deletion no snapshot needs can go too. The snapshot boundary is the one
 * taken at the start, which stays safe as it runs: a snapshot opened since is
 * newer than every input entry, and needs only the newest of each key. */
static int comp_step(GsStore *db, uint64_t budget, uint64_t *used) {
    Comp *c = db->comp;
    Merge m = {c->src, c->ns};
    int rc = GS_OK;
    uint64_t work = 0;
    for (size_t i = 0; i < c->ns; i++) if (c->src[i].rc) rc = c->src[i].rc;
    while (rc == GS_OK && work < budget) {
        int i = merge_min(&m);
        if (i < 0) break;
        Ent e = src_ent(&c->src[i]);
        int new_key = !c->have_cur || c->cur.n != e.kn || memcmp(c->cur.p, e.k, e.kn);
        if (new_key) {
            if (c->open && c->b.off + c->b.block.n >= c->target && (rc = comp_close_output(c))) break;
            gsbuf_set(&c->cur, e.k, e.kn);
            c->have_cur = 1;
            c->kept_visible = 0;
        }
        int keep;
        uint64_t death = e.kn >= ID_LEN ? death_of(db, ukey_id(e.k)) : 0;
        if (death && death <= c->keep_after) keep = 0;        /* a retired tree no snapshot can see */
        else if (e.seq > c->keep_after) keep = 1;              /* newer than every snapshot */
        else if (!c->kept_visible) {                           /* what the oldest snapshot sees */
            c->kept_visible = 1;
            keep = !(c->bottom && e.type == TYPE_DEL);
        } else keep = 0;                                       /* shadowed for everyone */
        if (keep) {
            if (!c->open && (rc = comp_open_output(db, c))) break;
            if (e.type == TYPE_BLOB && e.vn == BLOB_PTR && get64(e.v) < c->gc_below) {
                if ((rc = blob_read(db, e.v, e.vn, &c->reloc))) break;
                e.type = TYPE_PUT;
                e.v = c->reloc.p;
                e.vn = c->reloc.n;
            }
            bld_add(&c->b, &e);
        }
        work += e.kn + e.vn + 16;
        src_next(&c->src[i]);
        if (c->src[i].rc) rc = c->src[i].rc;
    }
    if (used) *used = work;
    if (rc) { comp_free(db, 1); return rc; }
    if (merge_min(&m) >= 0) return GS_OK; /* paused: more next time */
    if (c->open && (rc = comp_close_output(c))) { comp_free(db, 1); return rc; }
    return comp_install(db);
}

/* Begin compacting `level` into the next. A trivial move finishes here and
 * leaves no compaction in progress. */
static int comp_start(GsStore *db, int level) {
    Version *v = db->cur;
    FileMeta **inputs = NULL;
    size_t nin = 0;
    GsBuf lo = {0}, hi = {0};
    if (level == 0) {
        inputs = malloc((v->nfiles[0] + v->nfiles[1] + 1) * sizeof *inputs);
        for (size_t i = 0; i < v->nfiles[0]; i++) {
            FileMeta *m = v->files[0][i];
            if (!lo.p || bytes_cmp(gsb(m->smallest.p, m->smallest.n), gsb(lo.p, lo.n)) < 0) gsbuf_set(&lo, m->smallest.p, m->smallest.n);
            if (!hi.p || bytes_cmp(gsb(m->largest.p, m->largest.n), gsb(hi.p, hi.n)) > 0) gsbuf_set(&hi, m->largest.p, m->largest.n);
            inputs[nin++] = m;
        }
    } else {
        /* one file, round-robin by key, and what it overlaps below */
        size_t pick = 0;
        for (size_t i = 0; i < v->nfiles[level]; i++)
            if (i >= db->compact_ptr[level]) { pick = i; break; }
        db->compact_ptr[level] = pick + 1 >= v->nfiles[level] ? 0 : pick + 1;
        FileMeta *m = v->files[level][pick];
        inputs = malloc((v->nfiles[level + 1] + 2) * sizeof *inputs);
        inputs[nin++] = m;
        gsbuf_set(&lo, m->smallest.p, m->smallest.n);
        gsbuf_set(&hi, m->largest.p, m->largest.n);
    }
    size_t first_below = nin;
    int next = level + 1;
    for (size_t i = 0; i < v->nfiles[next]; i++)
        if (overlaps(v->files[next][i], &lo, &hi)) {
            inputs = realloc(inputs, (nin + 1) * sizeof *inputs);
            inputs[nin++] = v->files[next][i];
        }
    gsbuf_free(&lo);
    gsbuf_free(&hi);
    /* A trivial move: nothing below overlaps, and the inputs do not overlap
     * each other, so the files go down a level as they are, unrewritten. A
     * sorted load is all such moves (RocksDB writes 2.0x on one; this wrote
     * 7.0x by rewriting at every level). Nothing is dropped on the way, which
     * is safe: a move only keeps what a merge might have discarded. */
    if (nin == first_below && moveable(inputs, nin)) {
        Version *nv = version_copy(v);
        for (size_t i = 0; i < nin; i++) {
            version_add(nv, next, inputs[i]);
            version_remove(db, nv, level, inputs[i]);
        }
        free(inputs);
        return install(db, nv, NULL, 0);
    }
    Comp *c = calloc(1, sizeof *c);
    c->level = level;
    c->next = next;
    c->inputs = inputs;
    c->nin = nin;
    c->first_below = first_below;
    c->bottom = 1;
    for (int l = next + 1; l < LEVELS; l++) if (v->nfiles[l]) c->bottom = 0;
    c->keep_after = oldest_snapshot(db);
    c->target = db->memtable_limit * 2;
    if (db->nblob >= 4) {
        /* the oldest quarter of the blob files, by number */
        uint64_t *nums = malloc(db->nblob * sizeof *nums);
        for (size_t i = 0; i < db->nblob; i++) nums[i] = db->blob[i].num;
        qsort(nums, db->nblob, sizeof *nums, u64_cmp);
        c->gc_below = nums[db->nblob / 4];
        free(nums);
    }
    /* sources: each upper input alone, the lower ones as one run */
    c->src = calloc(nin + 1, sizeof *c->src);
    for (size_t i = 0; i < first_below; i++) {
        Src *sr = &c->src[c->ns++];
        sr->kind = 1; sr->db = db; sr->files = &c->inputs[i]; sr->nfiles = 1;
        src_seek(sr, (const unsigned char *)"", 0, UINT64_MAX);
    }
    if (nin > first_below) {
        Src *sr = &c->src[c->ns++];
        sr->kind = 1; sr->db = db; sr->files = &c->inputs[first_below]; sr->nfiles = nin - first_below;
        src_seek(sr, (const unsigned char *)"", 0, UINT64_MAX);
    }
    db->comp = c;
    return GS_OK;
}

/* The level, if any, that needs compacting now. */
static int comp_wanted(GsStore *db) {
    if (db->cur->nfiles[0] >= db->l0_trigger) return 0;
    for (int l = 1; l < LEVELS - 1; l++)
        if (db->cur->nfiles[l] && level_bytes(db->cur, l) > level_limit(db, l)) return l;
    return -1;
}

/* Spend up to `budget` bytes of merge work, starting compactions as they are
 * wanted. The budget is a commit's share (COMP_CREDIT times what it wrote),
 * so the work keeps pace with the writing that creates it. If level 0 backs
 * up past COMP_STALL times its trigger anyway, the merge in progress is
 * finished outright: the writer waits, as RocksDB's does at its stall
 * threshold, and that bounds both the debt and the tail. */
#define COMP_CREDIT 32
#define COMP_STALL 3
static int maybe_compact(GsStore *db, uint64_t budget) {
    for (int guard = 0; guard < 64; guard++) {
        int rc;
        if (!db->comp) {
            int l = comp_wanted(db);
            if (l < 0) return GS_OK;
            if ((rc = comp_start(db, l))) return rc;
            if (!db->comp) continue; /* a trivial move */
        }
        int stall = db->cur->nfiles[0] >= db->l0_trigger * COMP_STALL;
        if (!stall && !budget) return GS_OK;
        uint64_t used = 0;
        if ((rc = comp_step(db, stall ? UINT64_MAX : budget, &used))) return rc;
        if (db->comp && !stall) return GS_OK; /* paused, the budget spent */
        if (!stall) budget = used >= budget ? 0 : budget - used;
    }
    return GS_OK;
}

/* Write the memtable to a level-0 file and start a new log. */
static int flush_memtable(GsStore *db) {
    Skip *mem = &db->cur->mem->sk;
    if (!mem->count) return GS_OK;
    /* the new log first: commits after this go to it */
    GsqlFile *oldlog = db->log;
    uint64_t oldnum = db->log_num;
    char *name = log_name(db->path, oldnum + 1);
    GsqlFile *nl = NULL;
    int rc = ioerr(gsql_io_open(db->io, name, GSQL_IO_CREATE, &nl));
    if (rc == GS_OK) rc = ioerr(gsql_io_truncate(nl, 0));
    if (rc == GS_OK) rc = prefill_log(db, nl, 0);
    if (rc == GS_OK) rc = flush_file(db, nl);
    free(name);
    if (rc) { if (nl) gsql_io_close(nl); return rc; }
    Builder b;
    rc = bld_open(db, &b);
    for (SkNode *n = mem->head->next[0]; n && rc == GS_OK; n = n->next[0]) {
        Ent e = sk_ent(n);
        bld_add(&b, &e);
        rc = b.rc;
    }
    FileMeta *fm = NULL;
    int r2 = rc == GS_OK || b.f ? bld_close(&b, &fm) : rc;
    if (rc == GS_OK) rc = r2;
    if (rc) { gsql_io_close(nl); if (fm) file_free(fm); return rc; }
    Version *nv = version_copy(db->cur);
    mem_unref(nv->mem);
    nv->mem = mem_new();
    if (fm) version_add(nv, 0, fm);
    db->log_num = oldnum + 1;
    uint64_t first_before = db->log_first;
    db->log_first = oldnum + 1; /* the memtable is in the file now */
    /* the manifest names the file and the new log; its directory flush makes
     * the new names durable before any commit lands in the new log */
    Version *old = db->cur;
    db->cur = nv;
    rc = manifest_write(db);
    if (rc) {
        db->cur = old;
        db->log_num = oldnum;
        db->log_first = first_before;
        /* the new file is named by no manifest: it goes (left, it stayed on
         * disk for good once a later manifest's next-file number passed it) */
        if (fm) fm->obsolete = 1;
        version_unref(db, nv);
        gsql_io_close(nl);
        return rc;
    }
    version_unref(db, old);
    gsql_io_close(oldlog);
    db->log = nl;
    db->log_end = 0;
    /* every log the memtable held, not only the last: an open may have
     * replayed several into it */
    for (uint64_t k = first_before; k <= oldnum; k++) {
        name = log_name(db->path, k);
        gsql_io_remove(db->io, name);
        free(name);
    }
    db->durable_seq = db->last_seq; /* everything is in a flushed file */
    return maybe_compact(db, 0);
}

/* ---- transactions ---------------------------------------------------------------- */

typedef struct {
    GsBuf name;
    uint64_t id;     /* 0: does not exist in this view */
    uint64_t mods;
} TreeRef;

struct GsCursor;

struct GsTxn {
    GsStore *db;
    int write;
    uint64_t snap;     /* sees sequences <= snap */
    Version *v;
    Skip buf;          /* write: this txn's changes, sequence MAX */
    U64s retired;      /* write: tree ids this txn retires */
    TreeRef *trees;
    size_t ntrees, cap;
    GsBuf value;
    struct GsCursor *cursors;
    uint64_t next_id;  /* write: the next tree id, computed once */
    int failed;
    /* write: the buffer spilled to sorted runs (G5), oldest first, owned by
     * the txn until its commit names them in a manifest */
    FileMeta **runs;
    size_t nruns;
    uint64_t spills;   /* bumped by each spill: cursors rebuild their sources */
};

static int txn_sources(struct GsTxn *t, Src *src, size_t *n) {
    size_t k = 0;
    if (t->write) {
        src[k].kind = 0; src[k].sk = &t->buf; src[k].priv = 1; k++;
        for (size_t r = t->nruns; r-- > 0;) {
            src[k].kind = 1; src[k].db = t->db; src[k].files = &t->runs[r]; src[k].nfiles = 1; src[k].priv = 1; k++;
        }
    }
    src[k].kind = 0; src[k].sk = &t->v->mem->sk; k++;
    Version *v = t->v;
    for (size_t i = v->nfiles[0]; i-- > 0;) {
        src[k].kind = 1; src[k].db = t->db; src[k].files = &v->files[0][i]; src[k].nfiles = 1; k++;
    }
    for (int l = 1; l < LEVELS; l++)
        if (v->nfiles[l]) { src[k].kind = 1; src[k].db = t->db; src[k].files = v->files[l]; src[k].nfiles = v->nfiles[l]; k++; }
    *n = k;
    return GS_OK;
}

static size_t max_sources(struct GsTxn *t) { return 2 + t->nruns + t->v->nfiles[0] + LEVELS; }

/* The newest version of `k` in one file, read at `seq`: 1 found (alive or a
 * deletion, *del says which), 0 not there, <0 an error. */
static int file_lookup(GsStore *db, FileMeta *m, const unsigned char *k, size_t kn, uint64_t seq, Ent *out, GsBuf *keep, int *del) {
    if (bytes_cmp(gsb(k, kn), gsb(m->smallest.p, m->smallest.n)) < 0 || bytes_cmp(gsb(k, kn), gsb(m->largest.p, m->largest.n)) > 0)
        return 0;
    int rc = file_open(db, m);
    if (rc) return -rc;
    if (!bloom_may(m, k, kn)) return 0;
    Src s = {0};
    s.kind = 1;
    s.db = db;
    s.files = &m;
    s.nfiles = 1;
    src_seek(&s, k, kn, seq);
    if (s.rc) { src_reset(&s); return -s.rc; }
    int found = 0;
    if (s.valid) {
        Ent e = src_ent(&s);
        if (e.kn == kn && !memcmp(e.k, k, kn)) {
            found = 1;
            *del = e.type == TYPE_DEL;
            if (!*del) {
                int vr = val_copy(db, e.type, e.v, e.vn, keep);
                if (vr) { src_reset(&s); return -vr; }
                *out = e; out->v = keep->p; out->vn = keep->n; out->k = k; out->type = TYPE_PUT;
            }
        }
    }
    src_reset(&s);
    return found;
}

/* The newest version of `k` visible to the txn: GS_OK with *e, GS_NOT_FOUND
 * if absent or deleted. */
static int lookup(struct GsTxn *t, const unsigned char *k, size_t kn, Ent *out, GsBuf *keep) {
    GsStore *db = t->db;
    if (t->write) {
        SkNode *n = sk_ge(&t->buf, k, kn, UINT64_MAX);
        if (n && n->kn == kn && !memcmp(n->k, k, kn)) {
            if (n->type == TYPE_DEL) return GS_NOT_FOUND;
            gsbuf_set(keep, n->v, n->vn);
            *out = sk_ent(n);
            out->v = keep->p;
            return GS_OK;
        }
        for (size_t r = t->nruns; r-- > 0;) {
            int del = 0, f = file_lookup(db, t->runs[r], k, kn, UINT64_MAX, out, keep, &del);
            if (f < 0) return -f;
            if (f) return del ? GS_NOT_FOUND : GS_OK;
        }
    }
    SkNode *n = sk_ge(&t->v->mem->sk, k, kn, t->snap);
    if (n && n->kn == kn && !memcmp(n->k, k, kn)) {
        if (n->type == TYPE_DEL) return GS_NOT_FOUND;
        gsbuf_set(keep, n->v, n->vn);
        *out = sk_ent(n);
        out->v = keep->p;
        return GS_OK;
    }
    Version *v = t->v;
    for (int l = 0; l < LEVELS; l++) {
        size_t cnt = v->nfiles[l];
        for (size_t j = 0; j < cnt; j++) {
            FileMeta *m = v->files[l][l == 0 ? cnt - 1 - j : j];
            if (bytes_cmp(gsb(k, kn), gsb(m->smallest.p, m->smallest.n)) < 0 ||
                bytes_cmp(gsb(k, kn), gsb(m->largest.p, m->largest.n)) > 0)
                continue;
            int rc = file_open(db, m);
            if (rc) return rc;
            if (!bloom_may(m, k, kn)) continue;
            Src s = {0};
            s.kind = 1;
            s.db = db;
            s.files = &m;
            s.nfiles = 1;
            src_seek(&s, k, kn, t->snap);
            if (s.rc) { src_reset(&s); return s.rc; }
            if (s.valid) {
                Ent e = src_ent(&s);
                if (e.kn == kn && !memcmp(e.k, k, kn)) {
                    int del = e.type == TYPE_DEL;
                    if (!del) {
                        int vr = val_copy(db, e.type, e.v, e.vn, keep);
                        if (vr) { src_reset(&s); return vr; }
                        *out = e; out->v = keep->p; out->vn = keep->n; out->k = k; out->type = TYPE_PUT;
                    }
                    src_reset(&s);
                    return del ? GS_NOT_FOUND : GS_OK;
                }
            }
            src_reset(&s);
            /* a level >= 1 holds a key in one file at most */
            if (l > 0) break;
        }
    }
    return GS_NOT_FOUND;
}

/* ---- trees ------------------------------------------------------------------------ */

static int tree_find(struct GsTxn *t, GsBytes name, size_t *idx) {
    for (size_t i = 0; i < t->ntrees; i++)
        if (bytes_cmp(gsb(t->trees[i].name.p, t->trees[i].name.n), name) == 0) { *idx = i; return GS_OK; }
    if (name.n > GS_MAX_KEY) return GS_TOO_LARGE;
    unsigned char *ck = malloc(ID_LEN + name.n);
    put_ukey(ck, CATALOG_ID, name);
    Ent e;
    GsBuf v = {0};
    int rc = lookup(t, ck, ID_LEN + name.n, &e, &v);
    free(ck);
    uint64_t id = 0;
    if (rc == GS_OK) { if (v.n != 8) { gsbuf_free(&v); return GS_CORRUPT; } id = get64(v.p); }
    else if (rc != GS_NOT_FOUND) { gsbuf_free(&v); return rc; }
    gsbuf_free(&v);
    if (t->ntrees == t->cap) { t->cap = t->cap ? t->cap * 2 : 8; t->trees = realloc(t->trees, t->cap * sizeof *t->trees); }
    TreeRef *tr = &t->trees[t->ntrees];
    memset(tr, 0, sizeof *tr);
    gsbuf_set(&tr->name, name.p, name.n);
    tr->id = id;
    *idx = t->ntrees++;
    return GS_OK;
}

static int buf_put(struct GsTxn *t, uint64_t id, GsBytes key, int type, GsBytes value) {
    unsigned char *k = malloc(ID_LEN + key.n);
    if (!k) return GS_IO;
    put_ukey(k, id, key);
    int rc = sk_insert(&t->buf, k, ID_LEN + key.n, UINT64_MAX, type, value.p, value.n, 1);
    free(k);
    return rc;
}

/* G5: a write transaction's memory is bounded. Past half the cache, the
 * buffer becomes a sorted run file -- written and flushed, but named in no
 * manifest until the commit does it -- and the buffer starts empty. */
static int spill_buffer(struct GsTxn *t) {
    GsStore *db = t->db;
    if (!t->buf.count) return GS_OK;
    Builder b;
    int rc = bld_open(db, &b);
    for (SkNode *n = t->buf.head->next[0]; n && rc == GS_OK; n = n->next[0]) {
        Ent e = sk_ent(n);
        e.seq = 0; /* the commit gives the whole run one sequence */
        bld_add(&b, &e);
        rc = b.rc;
    }
    FileMeta *fm = NULL;
    int r2 = b.f ? bld_close(&b, &fm) : rc;
    if (rc == GS_OK) rc = r2;
    if (rc) { if (fm) file_free(fm); return rc; }
    fm->seq_over = TXN_SEQ_BASE + t->nruns;
    FileMeta **v = realloc(t->runs, (t->nruns + 1) * sizeof *v);
    if (!v) { file_free(fm); return GS_IO; }
    t->runs = v;
    t->runs[t->nruns++] = fm;
    sk_free(&t->buf);
    sk_init(&t->buf);
    t->spills++;
    return GS_OK;
}

static int maybe_spill(struct GsTxn *t) {
    if (t->buf.bytes <= t->db->cache_bytes / 2) return GS_OK;
    int rc = spill_buffer(t);
    if (rc) t->failed = rc;
    return rc;
}

static int catalog_set(struct GsTxn *t, GsBytes name, uint64_t id) {
    unsigned char v[8];
    put64(v, id);
    return buf_put(t, CATALOG_ID, name, id ? TYPE_PUT : TYPE_DEL, gsb(v, id ? 8 : 0));
}

static int compute_next_id(struct GsTxn *t) {
    if (t->next_id) return GS_OK;
    /* one more than any id the catalog holds or retired */
    uint64_t best = FIRST_TREE_ID - 1;
    for (size_t i = 0; i < t->db->ndead; i++) if (t->db->dead[i].id > best) best = t->db->dead[i].id;
    Src *src = calloc(max_sources(t), sizeof *src);
    size_t ns;
    txn_sources(t, src, &ns);
    unsigned char lo[ID_LEN], hi[ID_LEN];
    put64be(lo, CATALOG_ID);
    put64be(hi, CATALOG_ID + 1);
    for (size_t i = 0; i < ns; i++) {
        src_seek(&src[i], lo, ID_LEN, UINT64_MAX);
        while (src[i].valid) {
            Ent e = src_ent(&src[i]);
            if (bytes_cmp(gsb(e.k, e.kn), gsb(hi, ID_LEN)) >= 0) break;
            if (e.type == TYPE_PUT && e.vn == 8 && get64(e.v) > best) best = get64(e.v);
            src_next(&src[i]);
        }
        src_reset(&src[i]);
    }
    for (size_t i = 0; i < t->ntrees; i++) if (t->trees[i].id > best) best = t->trees[i].id;
    free(src);
    t->next_id = best + 1;
    return GS_OK;
}

#define WRITABLE(t) do { if ((t)->db->failed) return GS_FAILED; if (!(t)->write) return GS_READ_ONLY; if ((t)->failed) return (t)->failed; } while (0)

static int lsm_tree_create(struct GsTxn *t, GsBytes name) {
    WRITABLE(t);
    size_t i;
    int rc = tree_find(t, name, &i);
    if (rc) return rc;
    if (t->trees[i].id) return GS_EXISTS;
    if ((rc = compute_next_id(t))) return rc;
    uint64_t id = t->next_id++;
    if ((rc = catalog_set(t, name, id))) return rc;
    t->trees[i].id = id;
    t->trees[i].mods++;
    return GS_OK;
}

static int lsm_tree_drop(struct GsTxn *t, GsBytes name) {
    WRITABLE(t);
    size_t i;
    int rc = tree_find(t, name, &i);
    if (rc) return rc;
    if (!t->trees[i].id) return GS_NOT_FOUND;
    if ((rc = catalog_set(t, name, 0))) return rc;
    u64_push(&t->retired, t->trees[i].id);
    t->trees[i].id = 0;
    t->trees[i].mods++;
    return GS_OK;
}

static int lsm_tree_replace(struct GsTxn *t, GsBytes target, GsBytes source) {
    WRITABLE(t);
    if (bytes_cmp(target, source) == 0) return GS_MISUSE;
    size_t ti, si;
    int rc = tree_find(t, target, &ti);
    if (rc || (rc = tree_find(t, source, &si))) return rc;
    if (!t->trees[si].id) return GS_NOT_FOUND;
    /* the target takes the source's id: no record moves */
    if (t->trees[ti].id) u64_push(&t->retired, t->trees[ti].id);
    uint64_t id = t->trees[si].id;
    if ((rc = catalog_set(t, target, id)) || (rc = catalog_set(t, source, 0))) return rc;
    t->trees[ti].id = id;
    t->trees[si].id = 0;
    t->trees[ti].mods++;
    t->trees[si].mods++;
    return GS_OK;
}

static int lsm_tree_list(struct GsTxn *t, int (*each)(void *, GsBytes), void *ctx);

static int lsm_get(struct GsTxn *t, GsBytes tree, GsBytes key, GsBytes *value) {
    if (t->db->failed) return GS_FAILED;
    size_t i;
    int rc = tree_find(t, tree, &i);
    if (rc) return rc;
    if (!t->trees[i].id) return GS_NOT_FOUND;
    if (key.n > GS_MAX_KEY) return GS_NOT_FOUND;
    unsigned char *k = malloc(ID_LEN + key.n);
    put_ukey(k, t->trees[i].id, key);
    Ent e;
    rc = lookup(t, k, ID_LEN + key.n, &e, &t->value);
    free(k);
    if (rc == GS_OK) *value = gsb(t->value.p, t->value.n);
    return rc;
}

static int last_key_of(struct GsTxn *t, uint64_t id, GsBuf *out, int *found);

static int lsm_put(struct GsTxn *t, GsBytes tree, GsBytes key, GsBytes value, unsigned flags) {
    WRITABLE(t);
    if (key.n > GS_MAX_KEY) return GS_TOO_LARGE;
    size_t i;
    int rc = tree_find(t, tree, &i);
    if (rc) return rc;
    if (!t->trees[i].id) return GS_NOT_FOUND;
    if (flags & (GS_NO_OVERWRITE | GS_APPEND)) {
        GsBytes v;
        rc = lsm_get(t, tree, key, &v);
        if (rc == GS_OK && (flags & GS_NO_OVERWRITE)) return GS_EXISTS;
        if (rc != GS_OK && rc != GS_NOT_FOUND) return rc;
        if (flags & GS_APPEND) {
            if (rc == GS_OK) return GS_ORDER;
            GsBuf last = {0};
            int found;
            rc = last_key_of(t, t->trees[i].id, &last, &found);
            if (rc == GS_OK && found && bytes_cmp(gsb(last.p, last.n), key) >= 0) rc = GS_ORDER;
            gsbuf_free(&last);
            if (rc) return rc;
        }
    }
    rc = buf_put(t, t->trees[i].id, key, TYPE_PUT, value);
    if (rc) { t->failed = rc; return rc; }
    t->trees[i].mods++;
    return maybe_spill(t);
}

static int lsm_del(struct GsTxn *t, GsBytes tree, GsBytes key) {
    WRITABLE(t);
    if (key.n > GS_MAX_KEY) return GS_TOO_LARGE;
    size_t i;
    int rc = tree_find(t, tree, &i);
    if (rc) return rc;
    if (!t->trees[i].id) return GS_NOT_FOUND;
    GsBytes v;
    rc = lsm_get(t, tree, key, &v);
    if (rc != GS_OK) return rc; /* NOT_FOUND: nothing to delete */
    rc = buf_put(t, t->trees[i].id, key, TYPE_DEL, gsb("", 0));
    if (rc) { t->failed = rc; return rc; }
    t->trees[i].mods++;
    return maybe_spill(t);
}

/* ---- the merging cursor ----------------------------------------------------------- */

typedef struct GsCursor {
    struct GsTxn *t;
    size_t tree;
    uint64_t mods, id;
    Src *src;
    size_t ns;
    int forward;          /* the sources sit just past `key` going forwards */
    int positioned;
    int between;          /* after cursor_delete: rests between the neighbours of `key` */
    uint64_t spills;      /* the txn's spill count when the sources were built */
    GsBuf key;            /* tree id + key */
    GsBuf value;          /* the entry's value as stored: a blob pointer if vblob */
    int vblob;
    GsBuf vres;           /* a blob pointer's value, read when asked for */
    unsigned char lo[ID_LEN], hi[ID_LEN];
    struct GsCursor *next;
} Cur;

static int visible(const Cur *c, int si, const Ent *e) {
    /* the writer's own buffer and runs are always visible to it */
    return c->src[si].priv || e->seq <= c->t->snap;
}

static void all_seek(Cur *c, const unsigned char *k, size_t kn, uint64_t seq) {
    for (size_t i = 0; i < c->ns; i++) src_seek(&c->src[i], k, kn, seq);
}

static int any_rc(Cur *c) {
    for (size_t i = 0; i < c->ns; i++) if (c->src[i].rc) return c->src[i].rc;
    return GS_OK;
}

/* From sources at or after a position, the first visible live key, if any. */
static int find_forward(Cur *c) {
    c->positioned = 0;
    c->forward = 1;
    for (;;) {
        int best = -1;
        Ent be = {0};
        for (size_t i = 0; i < c->ns; i++) {
            Src *s = &c->src[i];
            while (s->valid) {
                Ent e = src_ent(s);
                if (visible(c, (int)i, &e)) break;
                src_next(s); /* newer than the snapshot: not there for us */
            }
            if (!s->valid) continue;
            Ent e = src_ent(s);
            /* between sources at one key, the highest sequence: the buffer's is
             * UINT64_MAX and a spilled run's sits above every real one */
            if (best < 0 || ent_cmp(e.k, e.kn, e.seq, be.k, be.kn, be.seq) < 0) { best = (int)i; be = e; }
        }
        int rc = any_rc(c);
        if (rc) return rc;
        if (best < 0) return GS_OK;
        if (bytes_cmp(gsb(be.k, be.kn), gsb(c->hi, ID_LEN)) >= 0 || bytes_cmp(gsb(be.k, be.kn), gsb(c->lo, ID_LEN)) < 0)
            return GS_OK; /* past this tree */
        GsBuf k = {0};
        gsbuf_set(&k, be.k, be.kn);
        int live = be.type != TYPE_DEL;
        if (live) { gsbuf_set(&c->value, be.v, be.vn); c->vblob = be.type == TYPE_BLOB; }
        /* every source past this key */
        for (size_t i = 0; i < c->ns; i++) {
            Src *s = &c->src[i];
            while (s->valid) {
                Ent e = src_ent(s);
                if (e.kn != k.n || memcmp(e.k, k.p, k.n)) break;
                src_next(s);
            }
        }
        if (live) {
            gsbuf_free(&c->key);
            c->key = k;
            c->positioned = 1;
            return any_rc(c);
        }
        gsbuf_free(&k);
    }
}

/* The last visible live key strictly before `limit` (tree id + key), if any. */
static int find_backward(Cur *c, const unsigned char *limit, size_t ln) {
    c->positioned = 0;
    c->forward = 0;
    GsBuf lim = {0};
    gsbuf_set(&lim, limit, ln);
    for (int guard = 0;; guard++) {
        /* the largest key below the limit across sources */
        GsBuf best = {0};
        int have = 0;
        for (size_t i = 0; i < c->ns; i++) {
            src_seek_lt(&c->src[i], lim.p, lim.n, UINT64_MAX);
            if (c->src[i].valid) {
                Ent e = src_ent(&c->src[i]);
                if (!have || bytes_cmp(gsb(e.k, e.kn), gsb(best.p, best.n)) > 0) { gsbuf_set(&best, e.k, e.kn); have = 1; }
            }
        }
        int rc = any_rc(c);
        if (rc || !have || bytes_cmp(gsb(best.p, best.n), gsb(c->lo, ID_LEN)) < 0) { gsbuf_free(&best); gsbuf_free(&lim); return rc; }
        /* its newest visible version, across sources */
        int found = 0, live = 0;
        uint64_t bseq = 0;
        for (size_t i = 0; i < c->ns; i++) {
            int priv = c->src[i].priv;
            src_seek(&c->src[i], best.p, best.n, priv ? UINT64_MAX : c->t->snap);
            if (!c->src[i].valid) continue;
            Ent e = src_ent(&c->src[i]);
            if (e.kn != best.n || memcmp(e.k, best.p, best.n)) continue;
            uint64_t s = e.seq;
            if (!found || s > bseq) {
                found = 1;
                bseq = s;
                live = e.type != TYPE_DEL;
                if (live) { gsbuf_set(&c->value, e.v, e.vn); c->vblob = e.type == TYPE_BLOB; }
            }
        }
        if ((rc = any_rc(c))) { gsbuf_free(&best); gsbuf_free(&lim); return rc; }
        if (found && live) {
            gsbuf_free(&c->key);
            c->key = best;
            c->positioned = 1;
            gsbuf_free(&lim);
            return GS_OK;
        }
        gsbuf_set(&lim, best.p, best.n);
        gsbuf_free(&best);
    }
}

static int cur_live(Cur *c) {
    TreeRef *tr = &c->t->trees[c->tree];
    if (tr->mods != c->mods || tr->id != c->id || !tr->id) return GS_INVALIDATED;
    if (c->spills != c->t->spills) {
        /* the txn spilled its buffer (a change to ANOTHER tree may do it): the
         * sources are rebuilt and put back just past the current key, which is
         * where they stood -- transparent, since this tree did not change */
        for (size_t i = 0; i < c->ns; i++) src_reset(&c->src[i]);
        free(c->src);
        c->src = calloc(max_sources(c->t), sizeof *c->src);
        txn_sources(c->t, c->src, &c->ns);
        c->spills = c->t->spills;
        if (c->key.n) all_seek(c, c->key.p, c->key.n, 0);
        c->forward = 1;
    }
    return GS_OK;
}

static int lsm_cursor_open(struct GsTxn *t, GsBytes tree, GsCursor **out) {
    if (t->db->failed) return GS_FAILED;
    size_t i;
    int rc = tree_find(t, tree, &i);
    if (rc) return rc;
    if (!t->trees[i].id) return GS_NOT_FOUND;
    Cur *c = calloc(1, sizeof *c);
    c->t = t;
    c->tree = i;
    c->mods = t->trees[i].mods;
    c->id = t->trees[i].id;
    c->spills = t->spills;
    c->src = calloc(max_sources(t), sizeof *c->src);
    txn_sources(t, c->src, &c->ns);
    put64be(c->lo, c->id);
    put64be(c->hi, c->id + 1);
    c->next = t->cursors;
    t->cursors = c;
    *out = c;
    return GS_OK;
}

static int ukey_of(Cur *c, GsBytes key, GsBuf *out) {
    if (key.n > GS_MAX_KEY + 1) return GS_TOO_LARGE;
    gsbuf_reserve(out, ID_LEN + key.n + 1);
    put_ukey(out->p, c->id, key);
    out->n = ID_LEN + key.n;
    return GS_OK;
}

static int lsm_seek(GsCursor *c, GsBytes key, GsSeek mode, int *pos) {
    int rc = cur_live(c);
    if (rc) return rc;
    c->between = 0;
    GsBuf k = {0};
    if ((rc = ukey_of(c, key, &k))) { *pos = 0; return GS_OK; }
    switch (mode) {
    case GS_SEEK_GE:
    case GS_SEEK_EXACT:
        all_seek(c, k.p, k.n, UINT64_MAX);
        rc = find_forward(c);
        if (rc == GS_OK && mode == GS_SEEK_EXACT && c->positioned && (c->key.n != k.n || memcmp(c->key.p, k.p, k.n)))
            c->positioned = 0;
        break;
    case GS_SEEK_GT:
        all_seek(c, k.p, k.n, 0);
        rc = find_forward(c);
        break;
    case GS_SEEK_LE:
        k.p[k.n++] = 0; /* the smallest key after `key` */
        rc = find_backward(c, k.p, k.n);
        break;
    case GS_SEEK_LT:
        rc = find_backward(c, k.p, k.n);
        break;
    }
    gsbuf_free(&k);
    *pos = rc == GS_OK && c->positioned;
    return rc;
}

static int lsm_first(GsCursor *c, int *pos) {
    int rc = cur_live(c);
    if (rc) return rc;
    c->between = 0;
    all_seek(c, c->lo, ID_LEN, UINT64_MAX);
    rc = find_forward(c);
    *pos = rc == GS_OK && c->positioned;
    return rc;
}

static int lsm_last(GsCursor *c, int *pos) {
    int rc = cur_live(c);
    if (rc) return rc;
    c->between = 0;
    rc = find_backward(c, c->hi, ID_LEN);
    *pos = rc == GS_OK && c->positioned;
    return rc;
}

static int lsm_next(GsCursor *c, int *pos) {
    int rc = cur_live(c);
    if (rc) return rc;
    if (!c->positioned && !c->between) { *pos = 0; return GS_OK; }
    c->between = 0;
    if (!c->forward) all_seek(c, c->key.p, c->key.n, 0);
    rc = find_forward(c);
    *pos = rc == GS_OK && c->positioned;
    return rc;
}

static int lsm_prev(GsCursor *c, int *pos) {
    int rc = cur_live(c);
    if (rc) return rc;
    if (!c->positioned && !c->between) { *pos = 0; return GS_OK; }
    c->between = 0;
    GsBuf k = {0};
    gsbuf_set(&k, c->key.p, c->key.n);
    rc = find_backward(c, k.p, k.n);
    gsbuf_free(&k);
    *pos = rc == GS_OK && c->positioned;
    return rc;
}

static int lsm_key(GsCursor *c, GsBytes *key) {
    int rc = cur_live(c);
    if (rc) return rc;
    if (!c->positioned) return GS_NOT_FOUND;
    *key = gsb(c->key.p + ID_LEN, c->key.n - ID_LEN);
    return GS_OK;
}

static int lsm_value(GsCursor *c, GsBytes *value) {
    int rc = cur_live(c);
    if (rc) return rc;
    if (!c->positioned) return GS_NOT_FOUND;
    if (c->vblob) {
        /* read only when asked for: a scan that wants keys reads no blobs */
        if ((rc = blob_read(c->t->db, c->value.p, c->value.n, &c->vres))) return rc;
        *value = gsb(c->vres.p, c->vres.n);
        return GS_OK;
    }
    *value = gsb(c->value.p, c->value.n);
    return GS_OK;
}

static int lsm_cursor_put(GsCursor *c, GsBytes value) {
    struct GsTxn *t = c->t;
    WRITABLE(t);
    int rc = cur_live(c);
    if (rc) return rc;
    if (!c->positioned) return GS_NOT_FOUND;
    rc = sk_insert(&t->buf, c->key.p, c->key.n, UINT64_MAX, TYPE_PUT, value.p, value.n, 1);
    if (rc) { t->failed = rc; return rc; }
    t->trees[c->tree].mods++;
    c->mods = t->trees[c->tree].mods;
    gsbuf_set(&c->value, value.p, value.n);
    c->vblob = 0;
    /* the buffer changed under the sources: re-seek past this key */
    all_seek(c, c->key.p, c->key.n, 0);
    c->forward = 1;
    return GS_OK;
}

static int lsm_cursor_delete(GsCursor *c) {
    struct GsTxn *t = c->t;
    WRITABLE(t);
    int rc = cur_live(c);
    if (rc) return rc;
    if (!c->positioned) return GS_NOT_FOUND;
    rc = sk_insert(&t->buf, c->key.p, c->key.n, UINT64_MAX, TYPE_DEL, (const unsigned char *)"", 0, 1);
    if (rc) { t->failed = rc; return rc; }
    t->trees[c->tree].mods++;
    c->mods = t->trees[c->tree].mods;
    /* rests between neighbours: next and prev measure from the gone key */
    all_seek(c, c->key.p, c->key.n, 0);
    c->forward = 1;
    c->positioned = 0;
    c->between = 1;
    return GS_OK;
}

static void lsm_cursor_close(GsCursor *c) {
    if (!c) return;
    Cur **pp = &c->t->cursors;
    while (*pp && *pp != c) pp = &(*pp)->next;
    if (*pp) *pp = c->next;
    for (size_t i = 0; i < c->ns; i++) src_reset(&c->src[i]);
    free(c->src);
    gsbuf_free(&c->key);
    gsbuf_free(&c->value);
    gsbuf_free(&c->vres);
    free(c);
}

static int last_key_of(struct GsTxn *t, uint64_t id, GsBuf *out, int *found) {
    Cur c;
    memset(&c, 0, sizeof c);
    c.t = t;
    c.src = calloc(max_sources(t), sizeof *c.src);
    txn_sources(t, c.src, &c.ns);
    put64be(c.lo, id);
    put64be(c.hi, id + 1);
    int rc = find_backward(&c, c.hi, ID_LEN);
    *found = rc == GS_OK && c.positioned;
    if (*found) gsbuf_set(out, c.key.p + ID_LEN, c.key.n - ID_LEN);
    for (size_t i = 0; i < c.ns; i++) src_reset(&c.src[i]);
    free(c.src);
    gsbuf_free(&c.key);
    gsbuf_free(&c.value);
    return rc;
}

static int lsm_tree_list(struct GsTxn *t, int (*each)(void *, GsBytes), void *ctx) {
    if (t->db->failed) return GS_FAILED;
    Cur c;
    memset(&c, 0, sizeof c);
    c.t = t;
    c.src = calloc(max_sources(t), sizeof *c.src);
    txn_sources(t, c.src, &c.ns);
    put64be(c.lo, CATALOG_ID);
    put64be(c.hi, CATALOG_ID + 1);
    all_seek(&c, c.lo, ID_LEN, UINT64_MAX);
    int rc;
    for (rc = find_forward(&c); rc == GS_OK && c.positioned; rc = find_forward(&c))
        if (each(ctx, gsb(c.key.p + ID_LEN, c.key.n - ID_LEN))) break;
    for (size_t i = 0; i < c.ns; i++) src_reset(&c.src[i]);
    free(c.src);
    gsbuf_free(&c.key);
    gsbuf_free(&c.value);
    return rc;
}

/* ---- begin, commit, abort ------------------------------------------------------ */

static int lsm_begin_read(GsStore *db, struct GsTxn **out) {
    if (db->failed) return GS_FAILED;
    struct GsTxn *t = calloc(1, sizeof *t);
    t->db = db;
    t->snap = db->durable_seq; /* §1.3: only what is durable is visible */
    t->v = db->cur;
    t->v->refs++;
    snap_add(db, t->snap);
    *out = t;
    return GS_OK;
}

static int lsm_begin_write(GsStore *db, struct GsTxn **out) {
    if (db->failed) return GS_FAILED;
    if (db->writer) return GS_BUSY;
    struct GsTxn *t = calloc(1, sizeof *t);
    t->db = db;
    t->write = 1;
    t->snap = db->last_seq;
    t->v = db->cur;
    t->v->refs++;
    sk_init(&t->buf);
    snap_add(db, t->snap);
    db->writer = t;
    *out = t;
    return GS_OK;
}

static void txn_free(struct GsTxn *t) {
    while (t->cursors) lsm_cursor_close(t->cursors);
    /* runs still owned here were never committed: they go */
    for (size_t r = 0; r < t->nruns; r++) {
        char *name = file_name(t->db, t->runs[r]->num);
        file_close(t->runs[r]);
        gsql_io_remove(t->db->io, name);
        free(name);
        t->runs[r]->obsolete = 1; /* and its blob file, if nothing else points into it */
        file_free(t->runs[r]);
    }
    free(t->runs);
    snap_del(t->db, t->snap);
    version_unref(t->db, t->v);
    if (t->write) { sk_free(&t->buf); u64_free(&t->retired); }
    for (size_t i = 0; i < t->ntrees; i++) gsbuf_free(&t->trees[i].name);
    free(t->trees);
    gsbuf_free(&t->value);
    free(t);
}

static void lsm_abort(struct GsTxn *t) {
    if (t->write) t->db->writer = NULL;
    txn_free(t);
}

/* A transaction that spilled commits by naming its runs in ONE manifest: the
 * memtable is flushed first (so every older version sits in a file before the
 * runs in level 0), the rest of the buffer becomes the last run, each run gets
 * its sequence, and the manifest's rename is the commit. Nothing of it is in
 * the log, so no part of it can survive without the rest -- which a log record
 * for the buffer beside runs named earlier would have allowed. Always durable,
 * whatever `sync` said. */
static int commit_runs(struct GsTxn *t, uint64_t *commit_id) {
    GsStore *db = t->db;
    /* The flush writes a manifest whose next-file number is past the runs
     * already spilled, which nothing names until the commit's own manifest:
     * a crash between the two left them on disk for good (found by checking
     * every recovery for unnamed files). That manifest lists them as
     * garbage, and the commit's names them. */
    U64s mine = {0};
    for (size_t r = 0; r < t->nruns; r++) {
        u64_push(&mine, t->runs[r]->num);
        if (t->runs[r]->own_blob) u64_push(&mine, t->runs[r]->own_blob);
    }
    for (size_t i = 0; i < mine.n; i++) u64_push(&db->garbage, mine.v[i]);
    int rc = flush_memtable(db);
    for (size_t r = 0; r < mine.n; r++) /* by value: the flush may have added its own */
        for (size_t i = 0; i < db->garbage.n; i++)
            if (db->garbage.v[i] == mine.v[r]) { db->garbage.v[i] = db->garbage.v[--db->garbage.n]; break; }
    u64_free(&mine);
    if (rc == GS_OK) rc = spill_buffer(t);
    if (rc) { lsm_abort(t); return rc == GS_FULL ? GS_FULL : GS_IO; }
    uint64_t base = db->last_seq + 1, commit = base + t->nruns - 1;
    Version *nv = version_copy(db->cur);
    for (size_t r = 0; r < t->nruns; r++) {
        t->runs[r]->seq_over = base + r;
        version_add(nv, 0, t->runs[r]);
    }
    size_t ndead_before = db->ndead;
    for (size_t i = 0; i < t->retired.n; i++) dead_add(db, t->retired.v[i], commit);
    Version *old = db->cur;
    uint64_t last_before = db->last_seq;
    db->cur = nv;
    db->last_seq = commit;
    rc = manifest_write(db);
    if (rc) {
        db->cur = old;
        db->last_seq = last_before;
        db->ndead = ndead_before;
        /* the version took the runs: let it drop them, as garbage */
        for (size_t r = 0; r < t->nruns; r++) t->runs[r]->obsolete = 1;
        t->nruns = 0;
        version_unref(db, nv);
        lsm_abort(t);
        return rc == GS_FULL ? GS_FULL : GS_IO;
    }
    version_unref(db, old);
    uint64_t wrote = 0;
    for (size_t r = 0; r < t->nruns; r++) wrote += t->runs[r]->size;
    t->nruns = 0; /* the version owns them now */
    db->durable_seq = commit;
    if (commit_id) *commit_id = commit;
    db->writer = NULL;
    txn_free(t);
    rc = maybe_compact(db, wrote * COMP_CREDIT);
    return rc == GS_IO || db->failed ? GS_IO : GS_OK;
}

static int lsm_commit(struct GsTxn *t, int sync, uint64_t *commit_id) {
    GsStore *db = t->db;
    if (!t->write) return GS_MISUSE;
    if (db->failed) { lsm_abort(t); return GS_FAILED; }
    if (t->failed) { int rc = t->failed; lsm_abort(t); return rc; }
    if (t->nruns) return commit_runs(t, commit_id);
    /* one log record: the whole transaction, sequences from base */
    uint64_t base = db->last_seq + 1, count = t->buf.count;
    GsBuf rec = {0};
    unsigned char h[16] = {0};
    gsbuf_append(&rec, h, 8); /* len, crc */
    put64(h, base);
    put32(h + 8, (uint32_t)count);
    gsbuf_append(&rec, h, 12);
    for (SkNode *n = t->buf.head->next[0]; n; n = n->next[0]) {
        unsigned char b[12];
        b[0] = (unsigned char)n->type;
        gsbuf_append(&rec, b, 1);
        put_bytes(&rec, n->k, n->kn);
        put_bytes(&rec, n->v, n->vn);
    }
    put32(h, (uint32_t)t->retired.n);
    gsbuf_append(&rec, h, 4);
    for (size_t i = 0; i < t->retired.n; i++) put_u64(&rec, t->retired.v[i]);
    put32(rec.p, (uint32_t)(rec.n - 8));
    put32(rec.p + 4, crc32c(rec.p + 8, rec.n - 8));
    int rc = ioerr(gsql_io_write_at(db->log, db->log_end, rec.p, rec.n));
    if (rc == GS_OK) { db->st.bytes_written += rec.n; db->st.pages_written++; }
    if (rc == GS_OK && sync) rc = flush_file(db, db->log);
    if (rc) {
        /* FULL: nothing landed, the next commit writes over it. IO: a flush
         * failed (§1.3). */
        gsbuf_free(&rec);
        lsm_abort(t);
        return rc == GS_FULL ? GS_FULL : GS_IO;
    }
    db->log_end += rec.n;
    uint64_t wrote = rec.n;
    gsbuf_free(&rec);
    uint64_t seq = base;
    for (SkNode *n = t->buf.head->next[0]; n && rc == GS_OK; n = n->next[0]) rc = sk_insert(&db->cur->mem->sk, n->k, n->kn, seq++, n->type, n->v, n->vn, 0);
    uint64_t commit = count ? base + count - 1 : base;
    for (size_t i = 0; i < t->retired.n; i++) dead_add(db, t->retired.v[i], commit);
    db->last_seq = commit;
    if (sync) db->durable_seq = commit;
    if (commit_id) *commit_id = commit;
    db->writer = NULL;
    txn_free(t);
    if (rc) { db->failed = 1; return GS_IO; }
    if (db->cur->mem->sk.bytes >= db->memtable_limit) {
        rc = flush_memtable(db);
        /* the commit stands either way; a failed flush made the store unusable */
        if (rc == GS_IO || db->failed) return GS_IO;
    }
    /* this commit's share of the merge work its writing created */
    rc = maybe_compact(db, wrote * COMP_CREDIT);
    if (rc == GS_IO || db->failed) return GS_IO;
    return GS_OK;
}

static int lsm_sync(GsStore *db, uint64_t *durable) {
    if (db->failed) return GS_FAILED;
    int rc = GS_OK;
    if (db->durable_seq != db->last_seq) {
        rc = flush_file(db, db->log);
        if (rc == GS_OK) db->durable_seq = db->last_seq;
    }
    if (durable) *durable = db->durable_seq;
    return rc;
}

static int lsm_close(GsStore *db) {
    if (db->writer || db->nsnaps) return GS_MISUSE;
    if (!db->failed) lsm_sync(db, NULL);
    store_free(db);
    return GS_OK;
}

/* ---- check, backup, stats -------------------------------------------------------- */

typedef struct {
    void (*report)(void *, const char *);
    void *ctx;
    int bad;
} Rep;

static void rsay(Rep *r, const char *fmt, unsigned long long a, unsigned long long b) {
    r->bad = 1;
    if (r->report) { char line[160]; snprintf(line, sizeof line, fmt, a, b); r->report(r->ctx, line); }
}

static int lsm_check(GsStore *db, void (*report)(void *, const char *), void *ctx) {
    Rep r = {report, ctx, 0};
    Version *v = db->cur;
    for (int l = 0; l < LEVELS; l++) {
        for (size_t i = 0; i < v->nfiles[l]; i++) {
            FileMeta *m = v->files[l][i];
            if (file_open(db, m)) { rsay(&r, "file %llu (level %llu) does not open", m->num, (unsigned long long)l); continue; }
            GsBuf prev = {0};
            uint64_t pseq = 0, n = 0;
            for (size_t bi = 0; bi < m->nindex; bi++) {
                Block b;
                if (block_read(db, m, bi, &b)) { rsay(&r, "file %llu block %llu fails its checksum", m->num, bi); break; }
                for (size_t ei = 0; ei < b.n; ei++, n++) {
                    Ent *e = &b.e[ei];
                    if (n && ent_cmp(prev.p, prev.n, pseq, e->k, e->kn, e->seq) >= 0) rsay(&r, "file %llu: entries out of order at %llu", m->num, n);
                    if (!bloom_may(m, e->k, e->kn)) rsay(&r, "file %llu: the bloom filter misses entry %llu", m->num, n);
                    if (e->type == TYPE_BLOB) {
                        GsBuf bv = {0};
                        if (e->vn != BLOB_PTR || !u64_has(&m->blobs, get64(e->v)))
                            rsay(&r, "file %llu: entry %llu points into a blob file it does not list", m->num, n);
                        else if (blob_read(db, e->v, e->vn, &bv))
                            rsay(&r, "file %llu: entry %llu's blob does not read back", m->num, n);
                        gsbuf_free(&bv);
                    }
                    gsbuf_set(&prev, e->k, e->kn);
                    pseq = e->seq;
                }
                if (b.n && ent_cmp(b.e[b.n - 1].k, b.e[b.n - 1].kn, b.e[b.n - 1].seq, m->index[bi].last, m->index[bi].lastn, m->index[bi].lastseq))
                    rsay(&r, "file %llu: block %llu's index entry is not its last", m->num, bi);
                block_free(&b);
            }
            if (n != m->entries) rsay(&r, "file %llu holds %llu entries", m->num, n);
            gsbuf_free(&prev);
            if (l > 0 && i > 0 && bytes_cmp(gsb(v->files[l][i - 1]->largest.p, v->files[l][i - 1]->largest.n),
                                            gsb(m->smallest.p, m->smallest.n)) >= 0)
                rsay(&r, "level %llu: file %llu overlaps the one before it", (unsigned long long)l, m->num);
        }
    }
    /* Nothing on disk that nothing names. A file the version does not hold
     * must be garbage (deleted at the next open) or an unfinished
     * compaction's output; anything else grows the store for good, the
     * defect that grew 2.A and 2.B without bound. Data files are numbered
     * below next_file, so asking for each one needs no directory listing. */
    for (uint64_t num = 1; num < db->next_file; num++) {
        /* garbage and an unfinished compaction's outputs may be either kind */
        int either = 0, sorted = 0;
        for (size_t i = 0; i < db->garbage.n && !either; i++) either = db->garbage.v[i] == num;
        for (size_t i = 0; db->comp && i < db->comp->pending.n && !either; i++) either = db->comp->pending.v[i] == num;
        if (either) continue;
        for (int l = 0; l < LEVELS && !sorted; l++)
            for (size_t i = 0; i < v->nfiles[l] && !sorted; i++) sorted = v->files[l][i]->num == num;
        int blob = blob_find(db, num) != NULL;
        /* each name stands for itself: a sorted file 8 does not account
         * for a blob file 8 (it was exactly such a pair that leaked) */
        for (int kind = 0; kind < 2; kind++) {
            if (kind ? blob : sorted) continue;
            char *name = kind ? blob_name(db, num) : file_name(db, num);
            GsqlFile *f = NULL;
            if (gsql_io_open(db->io, name, 0, &f) == 0) {
                gsql_io_close(f);
                rsay(&r, kind ? "blob file %llu exists and nothing points into it (next file %llu)"
                              : "file %llu exists and nothing names it (next file %llu)",
                     (unsigned long long)num, (unsigned long long)db->next_file);
            }
            free(name);
        }
    }
    return r.bad ? GS_CORRUPT : GS_OK;
}

static int lsm_backup(struct GsTxn *snap, const char *dest) {
    GsStore *db = snap->db;
    if (snap->write) return GS_MISUSE;
    /* a fresh store at `dest` holding the snapshot's view as one file */
    GsStore tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.io = db->io;
    tmp.path = gs_path_with(dest, "");
    tmp.dir = gs_dirname(dest);
    tmp.block = db->block;
    tmp.memtable_limit = db->memtable_limit;
    tmp.vsep = db->vsep;
    memcpy(tmp.identity, db->identity, 16);
    tmp.next_file = 1;
    tmp.log_num = 1;
    tmp.log_first = 1;
    tmp.cur = version_copy(NULL);
    Builder b;
    int rc = bld_open(&tmp, &b);
    /* every visible live record of every tree, newest version only */
    Cur c;
    memset(&c, 0, sizeof c);
    c.t = snap;
    c.src = calloc(max_sources(snap), sizeof *c.src);
    txn_sources(snap, c.src, &c.ns);
    put64be(c.lo, 0);
    memset(c.hi, 0xFF, ID_LEN);
    all_seek(&c, c.lo, ID_LEN, UINT64_MAX);
    uint64_t seq = 1;
    for (int frc = find_forward(&c); rc == GS_OK && frc == GS_OK && c.positioned; frc = find_forward(&c)) {
        Ent e = {c.key.p, c.key.n, seq, TYPE_PUT, c.value.p, c.value.n};
        if (c.vblob) {
            /* the copy gets the value itself, and writes blobs of its own */
            if ((rc = blob_read(db, c.value.p, c.value.n, &c.vres))) break;
            e.v = c.vres.p;
            e.vn = c.vres.n;
        }
        bld_add(&b, &e);
        rc = b.rc;
    }
    gsbuf_free(&c.vres);
    for (size_t i = 0; i < c.ns; i++) src_reset(&c.src[i]);
    free(c.src);
    gsbuf_free(&c.key);
    gsbuf_free(&c.value);
    FileMeta *fm = NULL;
    int r2 = bld_close(&b, &fm);
    if (rc == GS_OK) rc = r2;
    if (fm) { version_add(tmp.cur, LEVELS - 1, fm); fm->f = NULL; gsql_io_close(b.f); }
    tmp.last_seq = seq;
    tmp.dead = db->dead; /* retired ids stay retired, or a new tree could reuse one */
    tmp.ndead = db->ndead;
    char *ln = log_name(dest, 1);
    GsqlFile *lf = NULL;
    gsql_io_remove(db->io, ln);
    if (rc == GS_OK) rc = ioerr(gsql_io_open(db->io, ln, GSQL_IO_CREATE, &lf));
    if (lf) gsql_io_close(lf);
    free(ln);
    /* the manifest last, through the same tmp-and-rename, so an interrupted
     * backup leaves no manifest at `dest` */
    if (rc == GS_OK) rc = manifest_write(&tmp);
    tmp.dead = NULL;
    tmp.ndead = 0;
    version_unref(&tmp, tmp.cur);
    for (size_t i = 0; i < tmp.nblob; i++) if (tmp.blob[i].f) gsql_io_close(tmp.blob[i].f);
    free(tmp.blob);
    u64_free(&tmp.garbage);
    free(tmp.path);
    free(tmp.dir);
    return rc;
}

static void lsm_stats(GsStore *db, GsStats *out) {
    *out = db->st;
    uint64_t total = 0, sz = 0;
    for (int l = 0; l < LEVELS; l++)
        for (size_t i = 0; i < db->cur->nfiles[l]; i++) total += db->cur->files[l][i]->size;
    if (db->log) { gsql_io_size(db->log, &sz); total += sz; }
    out->file_bytes = total;
}

const GsEngine gs_engine_lsm = {
    "lsm",
    lsm_open, lsm_close,
    lsm_begin_read, lsm_begin_write, lsm_commit, lsm_sync, lsm_abort,
    lsm_tree_create, lsm_tree_drop, lsm_tree_list, lsm_tree_replace,
    lsm_get, lsm_put, lsm_del,
    lsm_cursor_open, lsm_seek, lsm_first, lsm_last, lsm_next, lsm_prev, lsm_key, lsm_value,
    lsm_cursor_put, lsm_cursor_delete, lsm_cursor_close,
    lsm_backup, lsm_check, lsm_stats,
};

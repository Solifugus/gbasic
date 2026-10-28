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
 *   - backup is physical and sliced: the snapshot's pinned files copied as
 *     they are, and its memtable's view written as one more file (Test 4);
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
    /* blob_bytes[i]: the bytes its pointers into blobs[i] cover. The live
     * bytes of a blob file are the sum over the current version, so how much
     * of it is dead is derived rather than counted on the side, and lsm_check
     * recounts it from the pointers. */
    U64s blob_bytes;
    uint64_t own_blob;         /* the blob file written with it, or 0 */
    struct GsStore *db;
    int refs;                  /* versions holding it */
    int obsolete;              /* dropped by a compaction; delete at refs 0 */
    /* opened lazily; the index and bloom stay when the handle is closed */
    struct Hnd *h;
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
    /* A memtable frozen when it filled, being written to level 0 a slice per
     * commit (struct Flush), or NULL. Older than mem and newer than every
     * file, and read between them. */
    Mem *imm;
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
    /* Read handles on sorted and blob files, at most max_open of them open,
     * the least recently used closed to make room (hnd_get). */
    struct Hnd *lru_head, *lru_tail;
    size_t nopen, max_open;
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
    uint64_t vgen;            /* installs so far: what gc_start last looked at */
    struct Flush *fl;         /* the frozen memtable's flush in progress, or NULL */
    /* The next log, made ahead of time: log_num + 1, filled with zeros a
     * slice per commit and then synced, so a freeze only switches to it. */
    GsqlFile *spare;
    uint64_t spare_filled;
    int spare_ready;
    uint64_t gc_seen;         /* the vgen at which no blob file was worth collecting */
    int sweep;                /* a retired tree or a released snapshot may have freed whole files */
    int failed;
    unsigned char identity[16];
    GsStats st;
};

/* ---- a few helpers ---------------------------------------------------------- */

static int ioerr(int rc) { return gs_io_status(rc); }

/* ---- read handles ------------------------------------------------------------ */

/* A store keeps a sorted or blob file open between reads, but only up to
 * max_open of them. Before this every file stayed open from its first read,
 * or from the flush that wrote it, until it was deleted: a store needed a
 * descriptor per file, 22 at 10^7 rows and about 16,000 at 1 TB, against a
 * default limit of 1,024 per process. Under a lowered limit the commits
 * themselves failed, a flush being unable to open its next file (Test 4's
 * follow-up). A handle remembers its file's name, so it can be closed at any
 * moment and reopened at the next read; the owner is single-threaded and
 * every read takes its descriptor from hnd_get just before using it. */
typedef struct Hnd {
    GsqlFile *f;               /* NULL while closed */
    char *name;
    int flags;
    struct Hnd *prev, *next;   /* the store's list, most recent first; linked only while open */
} Hnd;

static Hnd *hnd_new(char *name, int flags) {
    Hnd *h = calloc(1, sizeof *h);
    h->name = name;
    h->flags = flags;
    return h;
}

static void hnd_unlink(GsStore *db, Hnd *h) {
    if (h->prev) h->prev->next = h->next; else db->lru_head = h->next;
    if (h->next) h->next->prev = h->prev; else db->lru_tail = h->prev;
    h->prev = h->next = NULL;
}

static void hnd_link(GsStore *db, Hnd *h) {
    h->prev = NULL;
    h->next = db->lru_head;
    if (db->lru_head) db->lru_head->prev = h; else db->lru_tail = h;
    db->lru_head = h;
}

static void hnd_close(GsStore *db, Hnd *h) {
    if (!h || !h->f) return;
    hnd_unlink(db, h);
    gsql_io_close(h->f);
    h->f = NULL;
    db->nopen--;
}

static void hnd_free(GsStore *db, Hnd *h) {
    if (!h) return;
    hnd_close(db, h);
    free(h->name);
    free(h);
}

static void hnd_make_room(GsStore *db) {
    while (db->nopen >= db->max_open && db->lru_tail) hnd_close(db, db->lru_tail);
}

/* An already open file, a flush's output, becomes the handle's. */
static void hnd_adopt(GsStore *db, Hnd *h, GsqlFile *f) {
    hnd_make_room(db);
    h->f = f;
    hnd_link(db, h);
    db->nopen++;
}

/* The handle's descriptor, opened if it was closed. An I/O layer code. */
static int hnd_get(GsStore *db, Hnd *h, GsqlFile **f) {
    if (h->f) {
        if (db->lru_head != h) { hnd_unlink(db, h); hnd_link(db, h); }
        *f = h->f;
        return 0;
    }
    hnd_make_room(db);
    int rc = gsql_io_open(db->io, h->name, h->flags, &h->f);
    if (rc) { h->f = NULL; return rc; }
    hnd_link(db, h);
    db->nopen++;
    *f = h->f;
    return 0;
}

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
    uint64_t total; /* its size, 0 until known: set when written, else read once */
    struct Hnd *h;  /* its read handle, made at the first read */
} Blob;

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
 * is lost to a crash, the next open finds a blob file the manifest does not
 * name, and deletes it then (sweep_dir). */
static void blob_unref(GsStore *db, uint64_t num, int del) {
    Blob *b = blob_find(db, num);
    if (!b || --b->refs > 0) return;
    hnd_free(db, b->h);
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
    if (!b->h) b->h = hnd_new(blob_name(db, num), 0);
    GsqlFile *f;
    int rc = hnd_get(db, b->h, &f);
    if (rc) return rc == ENOENT ? GS_CORRUPT : ioerr(rc);
    gsbuf_reserve(out, len + 1);
    size_t got = 0;
    rc = ioerr(gsql_io_read_at(f, off, out->p, len, &got));
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
    U64s blob_bytes;        /* and the bytes those entries cover in each */
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
     * files share the numbers. A stale file of the other kind used to be
     * removed here; the open's sweep now deletes every unnamed file before any
     * number can be reused, and with this removed the crash gate stays at 0
     * violations (Test 4). */
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

/* Note `n` bytes pointed at in blob file `num`. */
static void bld_ref(Builder *b, uint64_t num, uint64_t n) {
    for (size_t i = 0; i < b->blobs.n; i++)
        if (b->blobs.v[i] == num) { b->blob_bytes.v[i] += n; return; }
    u64_push(&b->blobs, num);
    u64_push(&b->blob_bytes, n);
}

/* Write a long value to this builder's blob file; `ptr` gets the pointer. */
static int bld_blob(Builder *b, const unsigned char *v, size_t vn, unsigned char *ptr) {
    GsStore *db = b->db;
    if (!b->bf) {
        b->bnum = db->next_file++;
        if (b->track) u64_push(b->track, b->bnum); /* garbage until named */
        char *name = blob_name(db, b->bnum);
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
    }
    int rc = ioerr(gsql_io_write_at(b->bf, b->boff, v, vn));
    if (rc) return rc;
    db->st.bytes_written += vn;
    put64(ptr, b->bnum);
    put64(ptr + 8, b->boff);
    put32(ptr + 16, (uint32_t)vn);
    put32(ptr + 20, crc32c(v, vn));
    b->boff += vn;
    bld_ref(b, b->bnum, vn);
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
        bld_ref(b, get64(in->v), get32(in->v + 16));
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
        m->db = b->db;
        m->h = hnd_new(file_name(b->db, b->num), GSQL_IO_READ_ONLY);
        hnd_adopt(b->db, m->h, b->f);
        m->blobs = b->blobs;
        m->blob_bytes = b->blob_bytes;
        m->own_blob = b->bnum;
        for (size_t i = 0; i < m->blobs.n; i++) blob_ref(b->db, m->blobs.v[i]);
        if (b->bnum) blob_find(b->db, b->bnum)->total = b->boff;
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
        u64_free(&b->blob_bytes);
    }
    gsbuf_free(&b->last);
    return rc;
}

/* ---- reading a sorted file -------------------------------------------------- */

static void file_close(FileMeta *m) {
    if (m->h) hnd_free(m->db, m->h);
    m->h = NULL;
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
    u64_free(&m->blob_bytes);
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

/* A sorted file's descriptor. A named file that is gone is CORRUPT; any
 * other failure is the I/O layer's own answer. (This used to make every
 * failure to open CORRUPT, so a process out of descriptors was told its
 * store was damaged.) */
static int file_fd(GsStore *db, FileMeta *m, GsqlFile **f) {
    GsStore *own = m->db ? m->db : db;
    if (!m->h) m->h = hnd_new(file_name(own, m->num), GSQL_IO_READ_ONLY);
    int rc = hnd_get(own, m->h, f);
    if (rc && getenv("GS_DEBUG")) fprintf(stderr, "lsm: %s does not open (%d)\n", m->h->name, rc);
    if (rc) return rc == ENOENT ? GS_CORRUPT : ioerr(rc);
    return GS_OK;
}

static int file_open(GsStore *db, FileMeta *m) {
    if (m->index) return GS_OK;
    GsqlFile *f;
    int rc = file_fd(db, m, &f);
    if (rc) return rc;
    uint64_t size = 0;
    gsql_io_size(f, &size);
    if (size < FOOTER) return GS_CORRUPT;
    unsigned char foot[FOOTER];
    if ((rc = read_exact(db, f, size - FOOTER, foot, FOOTER))) return rc;
    if (get64(foot + 32) != SST_MAGIC || get32(foot + 44) != crc32c(foot, 44)) return GS_CORRUPT;
    uint64_t ioff = get64(foot), boff = get64(foot + 12);
    uint32_t isz = get32(foot + 8), bsz = get32(foot + 20);
    if (ioff + isz > size || boff + bsz > size) return GS_CORRUPT;
    unsigned char *ib = malloc(isz ? isz : 1), *bb = malloc(bsz ? bsz : 1);
    if ((rc = read_exact(db, f, ioff, ib, isz)) || (rc = check_block(ib, isz)) ||
        (rc = read_exact(db, f, boff, bb, bsz)) || (rc = check_block(bb, bsz))) {
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
    GsqlFile *f;
    int rc = file_fd(db, m, &f);
    if (rc) return rc;
    out->raw = malloc(ie->size ? ie->size : 1);
    rc = read_exact(db, f, ie->off, out->raw, ie->size);
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
        if ((s->rc = file_open(s->db, m))) return;
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
            if ((s->rc = file_open(s->db, s->files[fi]))) { s->valid = 0; return; }
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
                if ((s->rc = file_open(s->db, s->files[fi]))) { s->valid = 0; return; }
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
        if ((s->rc = file_open(s->db, m))) return;
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
    if (v) { n->mem = v->mem; n->mem->refs++; n->imm = v->imm; if (n->imm) n->imm->refs++; }
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
        /* No longer garbage (the in-memory list `check` reads). The deletion
         * is not durable yet and needs nothing more: if a crash loses it, the
         * next open finds a file the manifest does not name and deletes it
         * (sweep_dir). */
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
    mem_unref(v->imm);
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
    /* Every output's number, finished or not: named in no version until the
     * compaction installs, so check must not call them leaks meanwhile. A
     * crash leaves them unnamed, and the next open's sweep deletes them (the
     * manifest once listed them as garbage for that). */
    U64s pending;
    /* Blob space comes back by relocation: a value this compaction keeps
     * from a blob file in `gc` -- one at least GC_DEAD_PCT dead when it
     * began -- is written again, into the output's own blob, so those files
     * lose their pointers and go. Without it a file stays whole while any
     * one of its values is live. */
    U64s gc;
    GsBuf reloc;
    int for_gc;           /* begun by gc_start: paced by GC_CREDIT, not COMP_CREDIT */
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
            for (size_t j = 0; j < m->blobs.n; j++) { put_u64(&b, m->blobs.v[j]); put_u64(&b, m->blob_bytes.v[j]); }
        }
    }
    /* No list of garbage and no list of live blob files: the store owns its
     * directory, and an open deletes whatever in it the version does not name
     * (sweep_dir). Both lists were here for what a crash strands -- a pausing
     * compaction's outputs, files whose deletion did not survive, a blob
     * nothing pointed into -- and measured redundant once the sweep existed:
     * with both removed and the sweep kept, the crash gate saw 0 violations in
     * 185,861 crashes; with the sweep removed too, every recovery checked
     * failed. */
    /* Only the deaths of DURABLE commits. A manifest is written whenever a
     * flush or a compaction installs, which is often between a commit and
     * the sync that makes it durable; written then, it recorded the death
     * of a tree a crash could still bring back -- the commit retiring it
     * lost, the manifest saying it was dead -- and the next compaction
     * dropped the live tree whole. A death not yet durable is in the log,
     * and replay restores it (found by the crash gate's hot-key power tier
     * once the sliced flush made such manifests common; compaction installs
     * could always write one). */
    uint64_t nd = 0;
    for (size_t i = 0; i < db->ndead; i++) nd += db->dead[i].death <= db->durable_seq;
    put_u64(&b, nd);
    for (size_t i = 0; i < db->ndead; i++)
        if (db->dead[i].death <= db->durable_seq) { put_u64(&b, db->dead[i].id); put_u64(&b, db->dead[i].death); }
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
                uint64_t bn, bb;
                if ((rc = get_u64(p, end, &at, &bn)) || (rc = get_u64(p, end, &at, &bb))) { file_free(m); return rc; }
                u64_push(&m->blobs, bn);
                u64_push(&m->blob_bytes, bb);
            }
            m->db = db;
            for (size_t j = 0; j < m->blobs.n; j++) blob_ref(db, m->blobs.v[j]);
            version_add(db->cur, l, m);
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
static void flush_drop(GsStore *db);

static void store_free(GsStore *db) {
    comp_free(db, 1); /* an unfinished compaction's outputs are named nowhere */
    flush_drop(db); /* and so are an unfinished flush's; its logs replay */
    /* a spare log stays: the next open replays it (zeros) and writes to it */
    if (db->spare) gsql_io_close(db->spare);
    if (db->log) gsql_io_close(db->log);
    if (db->lockf) gsql_io_close(db->lockf);
    version_unref(db, db->cur);
    for (size_t i = 0; i < db->nblob; i++) hnd_free(db, db->blob[i].h);
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

/* What a name in the database directory is: the store's own file kinds, or
 * not one of them. */
typedef enum { N_OTHER, N_MANIFEST, N_LOCK, N_LOG, N_SST, N_BLOB } NameKind;
static NameKind name_kind(const char *name, uint64_t *num) {
    unsigned long long v = 0;
    int end = 0;
    *num = 0;
    if (!strcmp(name, GS_DB_BASE)) return N_MANIFEST;
    if (!strcmp(name, GS_DB_BASE ".lock")) return N_LOCK;
    if (sscanf(name, GS_DB_BASE ".log.%llu%n", &v, &end) == 1 && name[end] == 0) { *num = v; return N_LOG; }
    if (sscanf(name, GS_DB_BASE ".%llu.sst%n", &v, &end) == 1 && name[end] == 0) { *num = v; return N_SST; }
    if (sscanf(name, GS_DB_BASE ".%llu.blob%n", &v, &end) == 1 && name[end] == 0) { *num = v; return N_BLOB; }
    return N_OTHER;
}

static int in_version(Version *v, uint64_t num) {
    for (int l = 0; l < LEVELS; l++)
        for (size_t i = 0; i < v->nfiles[l]; i++) if (v->files[l][i]->num == num) return 1;
    return 0;
}

/* Whether the store, as it stands, names this file. At open nothing else can
 * need one: no compaction is running and no transaction is open. */
static int named(GsStore *db, NameKind k, uint64_t num) {
    switch (k) {
    case N_MANIFEST: case N_LOCK: return 1;
    case N_LOG: return (num >= db->log_first && num <= db->log_num) || (db->spare && num == db->log_num + 1);
    case N_SST: return in_version(db->cur, num);
    case N_BLOB: return blob_find(db, num) != NULL;
    default: return 0;
    }
}

typedef struct { GsStore *db; char **names; size_t n; } Sweep;
static int sweep_one(void *ctx, const char *name) {
    Sweep *w = ctx;
    uint64_t num;
    NameKind k = name_kind(name, &num);
    if (named(w->db, k, num)) return 0;
    char *full = malloc(strlen(w->db->dir) + strlen(name) + 2);
    sprintf(full, "%s/%s", w->db->dir, name);
    w->names = realloc(w->names, (w->n + 1) * sizeof *w->names);
    w->names[w->n++] = full;
    return 0;
}

/* The directory is the store's own (gs_db_dir refused it otherwise), so
 * anything in it the store does not name is garbage: an interrupted
 * compaction's output, a flush whose manifest never landed, a blob whose
 * deletion a crash undid, the manifest's own temporary. One rule, in place of
 * tracking each file a crash might strand -- which is how four leaks were
 * found one at a time (the rebuild's check, and Test 3). Removals need no
 * flush: one a crash undoes is made again at the next open. */
static int sweep_dir(GsStore *db) {
    Sweep w = {db, NULL, 0};
    int rc = gsql_io_list(db->io, db->dir, sweep_one, &w);
    for (size_t i = 0; i < w.n; i++) {
        if (rc == 0) gsql_io_remove(db->io, w.names[i]);
        free(w.names[i]);
    }
    free(w.names);
    return ioerr(rc);
}

static int lsm_open_base(const char *path, const GsOptions *opt, GsStore **out) {
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
    /* 512: half the usual per-process limit, leaving the rest to the
     * application; RocksDB's equivalent (max_open_files) defaults to no cap */
    db->max_open = opt && opt->tune_d ? opt->tune_d : 512;
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
        /* The live log may be a spare made ahead of time, which no manifest
         * named and whose name was not yet durable when the crash came: the
         * disk can show it while the name stays pending, so a commit synced
         * into it after recovery was lost by the next power loss (the crash
         * gate's hot-key power tier). Make its name durable now. */
        if (rc == GS_OK) {
            rc = ioerr(gsql_io_sync_dir(db->io, db->dir));
            db->st.flushes++;
        }
        db->durable_seq = db->last_seq;
    } else {
        rc = ioerr(rc);
    }
    if (rc == GS_OK) rc = sweep_dir(db);
    if (rc) { store_free(db); return rc; }
    *out = db;
    return GS_OK;
}

/* ---- snapshots and the oldest one ------------------------------------------- */

/* What a new version must serve: a reader that has not begun yet. Every open
 * transaction pins the version it began with -- its files AND its memtable --
 * so a compaction's outputs, a flush's file and a swept file's absence are
 * seen only by readers that begin after them, and a reader begins at the last
 * DURABLE commit (§1.3). So that, not the oldest open snapshot, is the
 * horizon below which a shadowed version or a retired tree may go.
 *
 * The first version of this used the oldest snapshot, or the last commit when
 * none was open, and was wrong both ways (Test 3, 2026-10-26):
 *   - too much kept: a compaction under a held snapshot kept EVERY version
 *     written since it, which nobody could read -- the snapshot reads its own
 *     pinned inputs -- so one long reader cost space in proportion to writes,
 *     not to keys (2 M updates of 1,000 keys: 301 MB left after the release);
 *   - too little: a retired tree's records went once the retirement was
 *     before the last commit, while a new reader still began before it when
 *     that commit was not yet durable. The dead-file sweep, written with the
 *     same rule, showed it: a new reader saw 78 of 300 records of a tree whose
 *     drop was not yet durable. */
static uint64_t horizon(GsStore *db) { return db->durable_seq; }

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
    db->vgen++;
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
    u64_free(&c->gc);
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
            if (e.type == TYPE_BLOB && e.vn == BLOB_PTR && u64_has(&c->gc, get64(e.v))) {
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

/* ---- reclaiming blob space ------------------------------------------------- */

/* A blob file at least this much dead has its live values relocated by the
 * next compaction to pass through them, and gc_start begins one that does
 * when no other is wanted. It bounds a blob file's space at about twice its
 * live bytes. The rule it replaced -- relocate out of the oldest quarter of
 * the blob files, by number -- left W8 at 2.91x its live data: age is not
 * deadness, and nothing began a compaction for it. */
#define GC_DEAD_PCT 50

static uint64_t blob_total(GsStore *db, Blob *b) {
    if (!b->total) {
        if (!b->h) b->h = hnd_new(blob_name(db, b->num), 0);
        GsqlFile *f;
        uint64_t sz = 0;
        if (hnd_get(db, b->h, &f) == 0 && gsql_io_size(f, &sz) == 0) b->total = sz;
    }
    return b->total;
}

/* The blob files at least GC_DEAD_PCT dead, into `out`; and into `*deadest`
 * the deadest (by fraction) that the current version still points into, or
 * 0. A file only an older version points into is dead whole and needs no
 * collecting: it goes when that version is released. */
static void gc_collect(GsStore *db, U64s *out, uint64_t *deadest) {
    uint64_t *live = calloc(db->nblob + 1, sizeof *live);
    Version *v = db->cur;
    for (int l = 0; l < LEVELS; l++)
        for (size_t i = 0; i < v->nfiles[l]; i++) {
            FileMeta *m = v->files[l][i];
            for (size_t j = 0; j < m->blobs.n && j < m->blob_bytes.n; j++) {
                Blob *b = blob_find(db, m->blobs.v[j]);
                if (b) live[b - db->blob] += m->blob_bytes.v[j];
            }
        }
    uint64_t bd = 0, bt = 1;
    if (deadest) *deadest = 0;
    for (size_t i = 0; i < db->nblob; i++) {
        uint64_t tot = blob_total(db, &db->blob[i]);
        if (!tot || live[i] > tot) continue;
        uint64_t dead = tot - live[i];
        if (dead * 100 < tot * GC_DEAD_PCT) continue;
        if (out) u64_push(out, db->blob[i].num);
        /* dead/tot > bd/bt, without dividing */
        if (deadest && live[i] && (unsigned __int128)dead * bt > (unsigned __int128)bd * tot) {
            bd = dead;
            bt = tot;
            *deadest = db->blob[i].num;
        }
    }
    free(live);
}

/* A compaction of `inputs` (the first `first_below` from `level`, the rest
 * from `next`) into `next`, in progress. `level` may equal `next`: one file
 * rewritten where it is, for its blob space (gc_start). */
static int comp_make(GsStore *db, int level, int next, FileMeta **inputs, size_t nin, size_t first_below) {
    Version *v = db->cur;
    Comp *c = calloc(1, sizeof *c);
    c->level = level;
    c->next = next;
    c->inputs = inputs;
    c->nin = nin;
    c->first_below = first_below;
    c->bottom = 1;
    for (int l = next + 1; l < LEVELS; l++) if (v->nfiles[l]) c->bottom = 0;
    c->keep_after = horizon(db);
    c->target = db->memtable_limit * 2;
    gc_collect(db, &c->gc, NULL);
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
    return comp_make(db, level, next, inputs, nin, first_below);
}

/* Begin a compaction for blob space when no other is wanted: the file with
 * the most bytes in the deadest blob file, rewritten where it is if it is
 * below level 0 (one file, nothing merged), or level 0 compacted if only
 * level-0 files point there. Each one ends at least one file's pointers
 * into a collectable blob file, so it cannot repeat itself. The scan is
 * skipped until the version changes, since nothing else changes its answer. */
static int gc_start(GsStore *db) {
    if (db->gc_seen == db->vgen + 1) return GS_OK;
    uint64_t deadest;
    gc_collect(db, NULL, &deadest);
    if (!deadest) { db->gc_seen = db->vgen + 1; return GS_OK; }
    Version *v = db->cur;
    FileMeta *pick = NULL;
    int plevel = 0;
    uint64_t most = 0;
    for (int l = 1; l < LEVELS; l++)
        for (size_t i = 0; i < v->nfiles[l]; i++) {
            FileMeta *m = v->files[l][i];
            for (size_t j = 0; j < m->blobs.n && j < m->blob_bytes.n; j++)
                if (m->blobs.v[j] == deadest && m->blob_bytes.v[j] > most) { most = m->blob_bytes.v[j]; pick = m; plevel = l; }
        }
    if (!pick) {
        int rc = comp_start(db, 0);
        if (rc == GS_OK && db->comp) db->comp->for_gc = 1;
        return rc;
    }
    FileMeta **inputs = malloc(sizeof *inputs);
    inputs[0] = pick;
    int rc = comp_make(db, plevel, plevel, inputs, 1, 1);
    if (rc == GS_OK && db->comp) db->comp->for_gc = 1;
    return rc;
}

/* A file whose every record belongs to a tree retired at or before the last
 * durable commit (the horizon) is read by nobody new, and is dropped whole --
 * the readers already open keep their pinned copy -- rather than left until a
 * compaction happens to rewrite it. Without this a replaced dataset (G1) kept
 * its space for as long as no compaction reached its files -- measured, a 10^6
 * row dataset's 138 MB survived its replacement, its reader's release and
 * 2,000 further commits. The ids in the file's range must ALL be dead: ids
 * are handed out in order, so one in between is a tree, live or retired, and
 * a range spanning more than a few is left to compaction. Waits while a
 * compaction is paused, which may hold the same file as an input. */
static int file_all_dead(GsStore *db, FileMeta *m, uint64_t keep_after) {
    if (m->smallest.n < ID_LEN || m->largest.n < ID_LEN) return 0;
    uint64_t a = ukey_id(m->smallest.p), b = ukey_id(m->largest.p);
    if (b < a || b - a > 64) return 0;
    for (uint64_t id = a;; id++) {
        uint64_t death = death_of(db, id);
        if (!death || death > keep_after) return 0;
        if (id == b) return 1;
    }
}

static int sweep_dead(GsStore *db) {
    if (db->comp) return GS_OK; /* the flag stays: try again after it */
    uint64_t keep_after = horizon(db);
    /* a retirement not yet durable: try again at a later commit */
    db->sweep = 0;
    for (size_t i = 0; i < db->ndead; i++) if (db->dead[i].death > keep_after) db->sweep = 1;
    if (!db->ndead) return GS_OK;
    Version *v = db->cur, *nv = NULL;
    FileMeta **gone = NULL;
    size_t ngone = 0;
    for (int l = 0; l < LEVELS; l++)
        for (size_t i = 0; i < v->nfiles[l]; i++) {
            FileMeta *m = v->files[l][i];
            if (!file_all_dead(db, m, keep_after)) continue;
            if (!nv) nv = version_copy(v);
            gone = realloc(gone, (ngone + 1) * sizeof *gone);
            gone[ngone++] = m;
        }
    if (!nv) return GS_OK;
    for (size_t i = 0; i < ngone; i++)
        for (int l = 0; l < LEVELS; l++) version_remove(db, nv, l, gone[i]);
    int rc = install(db, nv, gone, ngone);
    free(gone);
    if (rc) db->sweep = 1;
    return rc;
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
/* A compaction for blob space only has to keep pace with the garbage being
 * made: past GC_DEAD_PCT dead, a byte relocated frees at least a byte. Spent
 * at COMP_CREDIT it tripled W8's p99 write (945 to 3,350 us), because the
 * credit a shaped tree leaves unspent became every commit's work. */
#define GC_CREDIT 4
static int sweep_dead(GsStore *db);
static int maybe_compact(GsStore *db, uint64_t budget) {
    if (db->sweep) { int rc = sweep_dead(db); if (rc) return rc; }
    for (int guard = 0; guard < 64; guard++) {
        int rc;
        if (!db->comp) {
            int l = comp_wanted(db);
            if (l < 0) {
                /* nothing the shape needs: spend what is left on blob space */
                if (!budget || (rc = gc_start(db))) return rc ? rc : GS_OK;
                if (!db->comp) return GS_OK;
            } else {
                if ((rc = comp_start(db, l))) return rc;
                if (!db->comp) continue; /* a trivial move */
            }
        }
        int stall = db->cur->nfiles[0] >= db->l0_trigger * COMP_STALL;
        if (!stall && !budget) return GS_OK;
        if (!stall && db->comp->for_gc && comp_wanted(db) < 0) {
            /* a commit's GC share, and no more after it this commit; once
             * the shape wants a compaction, this one is in its way and is
             * finished at the ordinary pace */
            uint64_t share = budget / COMP_CREDIT * GC_CREDIT;
            return comp_step(db, share ? share : 1, NULL);
        }
        uint64_t used = 0;
        if ((rc = comp_step(db, stall ? UINT64_MAX : budget, &used))) return rc;
        if (db->comp && !stall) return GS_OK; /* paused, the budget spent */
        if (!stall) budget = used >= budget ? 0 : budget - used;
    }
    return GS_OK;
}

/* ---- the memtable's flush, off the commit path --------------------------------- */

/* A flush used to run inside the commit that filled the memtable: ~75 ms on
 * W3 larger than memory, 26 of them preparing the next log (64 MB of zeros,
 * synced) and ~40 writing the level-0 file. Now the full memtable is FROZEN
 * -- it stays in the version as `imm`, read between the live memtable and
 * level 0, and commits go on into a new one -- and written out a slice per
 * commit. The next log is made ahead of time the same way. A freeze costs a
 * sync of the old log (so `durable_seq`, one high-water mark, stays true for
 * the frozen commits) and a version switch. If the new memtable fills before
 * the flush is done, the flush is finished outright: the writer waits, as it
 * does for compaction (COMP_STALL). Recovery needs nothing new: until the
 * flush installs, the manifest still replays every log from the frozen
 * memtable's first. */
typedef struct Flush {
    Builder b;
    int open;
    SkNode *at;            /* the next entry of imm to write */
    const SkNode *prev;
    uint64_t upto;         /* last_seq at the freeze: imm's newest commit */
    uint64_t first, last;  /* the logs imm's commits are in */
    U64s pending;          /* its file numbers, named nowhere until it installs */
} Flush;

/* Bytes of flush work, and of the spare log's zeros, a commit pays for each
 * byte it wrote: the flush finishes by the time the new memtable is half
 * full, and the spare's zeros (twice the memtable) by then too. */
#define FLUSH_CREDIT 2
#define SPARE_CREDIT 4

static uint64_t log_size(const GsStore *db) { return db->memtable_limit * 2 + 65536; }

/* Fill the spare log with zeros, up to `budget` bytes of it, each slice
 * synced as it goes so no single commit pays for the whole file. Ready once
 * full, synced and its name durable: a commit landing in a log whose name a
 * power loss could take would be lost with it. */
static int spare_step(GsStore *db, uint64_t budget) {
    if (db->spare_ready) return GS_OK;
    int rc = GS_OK;
    if (!db->spare) {
        char *name = log_name(db->path, db->log_num + 1);
        rc = ioerr(gsql_io_open(db->io, name, GSQL_IO_CREATE, &db->spare));
        free(name);
        if (rc == GS_OK) rc = ioerr(gsql_io_truncate(db->spare, 0));
        if (rc) { if (db->spare) gsql_io_close(db->spare); db->spare = NULL; return rc; }
        db->spare_filled = 0;
    }
    static const unsigned char zero[65536];
    uint64_t size = log_size(db), spent = 0;
    while (db->spare_filled < size && spent < budget) {
        size_t n = size - db->spare_filled < sizeof zero ? (size_t)(size - db->spare_filled) : sizeof zero;
        if ((rc = ioerr(gsql_io_write_at(db->spare, db->spare_filled, zero, n)))) return rc;
        db->st.bytes_written += n;
        db->spare_filled += n;
        spent += n;
    }
    if (spent && (rc = flush_file(db, db->spare))) return rc;
    if (db->spare_filled < size) return GS_OK;
    rc = ioerr(gsql_io_sync_dir(db->io, db->dir));
    db->st.flushes++;
    if (rc) return rc;
    db->spare_ready = 1;
    return GS_OK;
}

/* Drop a flush's file: its builder and anything it wrote. The frozen
 * memtable stays, to be flushed again from its start. */
static void flush_abandon(GsStore *db) {
    Flush *f = db->fl;
    if (!f) return;
    if (f->open) {
        FileMeta *fm = NULL;
        f->open = 0;
        bld_close(&f->b, &fm);
        if (fm) { fm->obsolete = 1; file_free(fm); }
    }
    for (size_t i = 0; i < f->pending.n; i++) {
        char *name = file_name(db, f->pending.v[i]);
        gsql_io_remove(db->io, name);
        free(name);
        name = blob_name(db, f->pending.v[i]);
        gsql_io_remove(db->io, name);
        free(name);
    }
    f->pending.n = 0;
    f->at = db->cur->imm ? db->cur->imm->sk.head->next[0] : NULL;
    f->prev = NULL;
}

static void flush_drop(GsStore *db) {
    if (!db->fl) return;
    flush_abandon(db);
    u64_free(&db->fl->pending);
    free(db->fl);
    db->fl = NULL;
}

/* Freeze the full memtable and switch to the spare log. */
static int freeze(GsStore *db) {
    if (db->fl || !db->cur->mem->sk.count) return GS_OK;
    int rc = spare_step(db, UINT64_MAX); /* not ready yet: finish it now */
    if (rc) return rc;
    /* the frozen commits not yet synced are in the old log, which nothing
     * will sync after this: sync it now, so durable_seq covers them */
    if (db->durable_seq != db->last_seq) {
        if ((rc = flush_file(db, db->log))) { db->failed = 1; return rc; }
        db->durable_seq = db->last_seq;
    }
    Version *nv = version_copy(db->cur);
    nv->imm = nv->mem; /* the copy's reference, moved */
    nv->mem = mem_new();
    Version *old = db->cur;
    db->cur = nv;
    version_unref(db, old);
    db->vgen++;
    Flush *f = calloc(1, sizeof *f);
    f->at = nv->imm->sk.head->next[0];
    f->upto = db->last_seq;
    f->first = db->log_first;
    f->last = db->log_num;
    db->fl = f;
    gsql_io_close(db->log);
    db->log = db->spare;
    db->log_num++;
    db->log_end = 0;
    db->spare = NULL;
    db->spare_ready = 0;
    db->spare_filled = 0;
    return GS_OK;
}

/* Install the finished flush: one manifest names the file and moves the
 * first log to replay past the frozen memtable's; then its logs go. */
static int flush_install(GsStore *db) {
    Flush *f = db->fl;
    FileMeta *fm = NULL;
    int rc = GS_OK;
    if (f->open) {
        f->open = 0;
        rc = bld_close(&f->b, &fm);
        if (rc) { flush_abandon(db); return rc; }
    }
    Version *nv = version_copy(db->cur);
    mem_unref(nv->imm);
    nv->imm = NULL;
    if (fm) version_add(nv, 0, fm);
    uint64_t first_before = db->log_first;
    db->log_first = f->last + 1;
    Version *old = db->cur;
    db->cur = nv;
    rc = manifest_write(db);
    if (rc) {
        db->cur = old;
        db->log_first = first_before;
        /* the file is named by no manifest: it goes, and the flush starts over */
        if (fm) fm->obsolete = 1;
        version_unref(db, nv);
        f->pending.n = 0; /* fm's own deletion covers its files */
        flush_abandon(db);
        return rc;
    }
    version_unref(db, old);
    db->vgen++;
    for (uint64_t k = f->first; k <= f->last; k++) {
        char *name = log_name(db->path, k);
        gsql_io_remove(db->io, name);
        free(name);
    }
    u64_free(&f->pending);
    free(f);
    db->fl = NULL;
    return GS_OK;
}

/* Write up to `budget` bytes of the frozen memtable; install when it is all
 * written. Only the newest version of each key, and nothing of a tree
 * retired by the freeze: the file is read only by readers that begin after
 * it installs, whose snapshots are past every frozen commit (the freeze made
 * them durable). Writing every version left level 0 holding ~100 MB of
 * shadowed versions for 1,000 hot keys. */
static int flush_step(GsStore *db, uint64_t budget) {
    Flush *f = db->fl;
    if (!f) return GS_OK;
    uint64_t spent = 0;
    int rc = GS_OK;
    while (f->at && spent < budget) {
        SkNode *n = f->at;
        f->at = n->next[0];
        int same = f->prev && f->prev->kn == n->kn && memcmp(f->prev->k, n->k, n->kn) == 0;
        f->prev = n;
        if (same) continue;
        Ent e = sk_ent(n);
        uint64_t death = e.kn >= ID_LEN ? death_of(db, ukey_id(e.k)) : 0;
        if (death && death <= f->upto) continue;
        if (!f->open) {
            if ((rc = bld_open(db, &f->b))) { flush_abandon(db); return rc; }
            u64_push(&f->pending, f->b.num);
            f->b.track = &f->pending;
            f->open = 1;
        }
        bld_add(&f->b, &e);
        if ((rc = f->b.rc)) { flush_abandon(db); return rc; }
        spent += n->kn + n->vn + 16;
    }
    if (f->at) return GS_OK;
    return flush_install(db);
}

/* Flush every memtable now, the frozen one and the live one: before a large
 * transaction's spilled runs go into level 0 above them. */
static int flush_memtable(GsStore *db) {
    int rc = flush_step(db, UINT64_MAX);
    if (rc == GS_OK && db->cur->mem->sk.count) rc = freeze(db);
    if (rc == GS_OK) rc = flush_step(db, UINT64_MAX);
    if (rc) return rc;
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
    if (t->v->imm) { src[k].kind = 0; src[k].sk = &t->v->imm->sk; k++; }
    Version *v = t->v;
    for (size_t i = v->nfiles[0]; i-- > 0;) {
        src[k].kind = 1; src[k].db = t->db; src[k].files = &v->files[0][i]; src[k].nfiles = 1; k++;
    }
    for (int l = 1; l < LEVELS; l++)
        if (v->nfiles[l]) { src[k].kind = 1; src[k].db = t->db; src[k].files = v->files[l]; src[k].nfiles = v->nfiles[l]; k++; }
    *n = k;
    return GS_OK;
}

static size_t max_sources(struct GsTxn *t) { return 3 + t->nruns + t->v->nfiles[0] + LEVELS; }

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
    for (int which = 0; which < 2; which++) {
        Mem *mm = which ? t->v->imm : t->v->mem;
        if (!mm) continue;
        SkNode *n = sk_ge(&mm->sk, k, kn, t->snap);
        if (n && n->kn == kn && !memcmp(n->k, k, kn)) {
            if (n->type == TYPE_DEL) return GS_NOT_FOUND;
            gsbuf_set(keep, n->v, n->vn);
            *out = sk_ent(n);
            out->v = keep->p;
            return GS_OK;
        }
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
     * every recovery for unnamed files). An open now deletes anything the
     * manifest does not name (sweep_dir), which covers that crash; while the
     * store stays open they are garbage, so `check` accepts them until the
     * commit's manifest names them. */
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
    if (t->retired.n) db->sweep = 1;
    Version *old = db->cur;
    uint64_t last_before = db->last_seq, durable_before = db->durable_seq;
    db->cur = nv;
    db->last_seq = commit;
    /* this manifest is what makes the commit durable, so it records the
     * commit's deaths (manifest_write writes only durable ones) */
    db->durable_seq = commit;
    rc = manifest_write(db);
    if (rc) {
        db->cur = old;
        db->last_seq = last_before;
        db->durable_seq = durable_before;
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
    if (t->retired.n) db->sweep = 1;
    db->last_seq = commit;
    if (sync) db->durable_seq = commit;
    if (commit_id) *commit_id = commit;
    db->writer = NULL;
    txn_free(t);
    if (rc) { db->failed = 1; return GS_IO; }
    /* the commit stands whatever these answer; a failed flush made the store
     * unusable, and anything else is tried again at a later commit */
    if (db->cur->mem->sk.bytes >= db->memtable_limit) {
        if (db->fl) rc = flush_step(db, UINT64_MAX); /* the last one is not done: wait for it */
        if (rc == GS_OK) rc = freeze(db);
        if (rc == GS_IO || db->failed) return GS_IO;
    }
    /* this commit's share of the frozen memtable's flush, and of the next log */
    rc = flush_step(db, wrote * FLUSH_CREDIT);
    if (rc == GS_IO || db->failed) return GS_IO;
    rc = spare_step(db, wrote * SPARE_CREDIT);
    if (rc == GS_IO || db->failed) return GS_IO;
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

typedef struct { GsStore *db; Rep *rep; } CheckDir;
static int check_one(void *ctx, const char *name) {
    CheckDir *cd = ctx;
    GsStore *db = cd->db;
    uint64_t num;
    NameKind k = name_kind(name, &num);
    if (named(db, k, num)) return 0;
    /* garbage and an unfinished compaction's outputs may be either kind */
    if (k == N_SST || k == N_BLOB) {
        for (size_t i = 0; i < db->garbage.n; i++) if (db->garbage.v[i] == num) return 0;
        for (size_t i = 0; db->comp && i < db->comp->pending.n; i++) if (db->comp->pending.v[i] == num) return 0;
        for (size_t i = 0; db->fl && i < db->fl->pending.n; i++) if (db->fl->pending.v[i] == num) return 0;
    }
    /* each name stands for itself: a sorted file 8 does not account for a
     * blob file 8 (it was exactly such a pair that leaked) */
    Rep *r = cd->rep;
    r->bad = 1;
    if (r->report) {
        char line[400];
        snprintf(line, sizeof line, "%s exists and nothing names it (next file %llu, logs %llu..%llu)", name,
                 (unsigned long long)db->next_file, (unsigned long long)db->log_first, (unsigned long long)db->log_num);
        r->report(r->ctx, line);
    }
    return 0;
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
            uint64_t *counted = calloc(m->blobs.n + 1, sizeof *counted);
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
                        for (size_t j = 0; e->vn == BLOB_PTR && j < m->blobs.n; j++)
                            if (m->blobs.v[j] == get64(e->v)) counted[j] += get32(e->v + 16);
                    }
                    gsbuf_set(&prev, e->k, e->kn);
                    pseq = e->seq;
                }
                if (b.n && ent_cmp(b.e[b.n - 1].k, b.e[b.n - 1].kn, b.e[b.n - 1].seq, m->index[bi].last, m->index[bi].lastn, m->index[bi].lastseq))
                    rsay(&r, "file %llu: block %llu's index entry is not its last", m->num, bi);
                block_free(&b);
            }
            if (n != m->entries) rsay(&r, "file %llu holds %llu entries", m->num, n);
            /* the bytes the dead-fraction trigger works from: a count that
             * drifted from the pointers would collect too early (losing
             * nothing, costing writes) or never (the space stays) */
            if (m->blob_bytes.n != m->blobs.n) rsay(&r, "file %llu: %llu blob byte counts for its blob files", m->num, m->blob_bytes.n);
            else
                for (size_t j = 0; j < m->blobs.n; j++)
                    if (counted[j] != m->blob_bytes.v[j] || !counted[j])
                        rsay(&r, "file %llu: its byte count for blob file %llu disagrees with its pointers", m->num, m->blobs.v[j]);
            free(counted);
            gsbuf_free(&prev);
            if (l > 0 && i > 0 && bytes_cmp(gsb(v->files[l][i - 1]->largest.p, v->files[l][i - 1]->largest.n),
                                            gsb(m->smallest.p, m->smallest.n)) >= 0)
                rsay(&r, "level %llu: file %llu overlaps the one before it", (unsigned long long)l, m->num);
        }
    }
    /* Nothing on disk that nothing names, asked of the DIRECTORY, which the
     * store owns: a file the version does not hold must be garbage (deleted
     * at the next open) or an unfinished compaction's output; anything else
     * grows the store for good, the defect that grew 2.A and 2.B without
     * bound. Before the store had a directory this probed every number below
     * next_file, which could not see a file outside that range or of a shape
     * it did not know -- a stale log, a stray temporary. */
    CheckDir cd = {db, &r};
    if (gsql_io_list(db->io, db->dir, check_one, &cd)) rsay(&r, "the store's directory does not list", 0, 0);
    return r.bad ? GS_CORRUPT : GS_OK;
}

/* A backup in slices, PHYSICAL: the snapshot's version is copied file by
 * file, byte for byte, and its memtable's view written as one more level-0
 * file. The files are immutable and the snapshot pins them, so they cannot
 * change or go while the copy runs -- what slicing costs here is only that
 * pinning, bounded by the data at the snapshot (Test 3, against 2.B's log).
 * The first backup was LOGICAL, the snapshot rewritten as one sorted file,
 * which cost writers 326 ms for 10^6 rows in a single call.
 *
 * Every file in the snapshot's version holds only commits at or before the
 * snapshot: a flush makes what it writes durable, and a reader begins at the
 * last durable commit, so a file is in a version a reader pins only if it was
 * there when the reader began. The memtable is shared with later commits, so
 * its entries are filtered by sequence.
 *
 * `dest` is a database directory like any (gs_db_dir: made if absent,
 * refused if it holds a stranger's file), so it is cleared by listing it:
 * everything in it is a database's. The manifest is written last, through the
 * store's own tmp-and-rename, so an interrupted backup leaves no manifest.
 * (Before the store had a directory, a durable `dest.pending` record of the
 * highest number the copy might use stood in for the listing.) */
typedef struct { char *src, *dst; uint64_t size, off; } CopyJob;
struct GsBackup {
    struct GsTxn *snap;
    GsStore *db;
    GsStore tmp;       /* the store being built at dest */
    char *dest;
    CopyJob *jobs;
    size_t njobs, at;
    GsqlFile *src, *dst;
    Builder b;
    int building;      /* the memtable's file is open */
    SkNode *mem;       /* the next memtable entry */
    SkNode *then;      /* the live memtable, when `mem` walks the frozen one */
    GsBuf lastkey;
    int havekey;
    int done;
    unsigned char *chunk;
};

#define BACKUP_CHUNK (1u << 20)

/* Everything in a backup's directory goes: it is a database's (gs_db_dir
 * said so), and an earlier backup, finished or not, is all it can hold. The
 * manifest first, so the place never holds a mix of two that looks like one. */
typedef struct { const char *dir; char **names; size_t n; } Clear;
static int clear_one(void *ctx, const char *name) {
    Clear *c = ctx;
    char *full = malloc(strlen(c->dir) + strlen(name) + 2);
    sprintf(full, "%s/%s", c->dir, name);
    c->names = realloc(c->names, (c->n + 1) * sizeof *c->names);
    c->names[c->n++] = full;
    return 0;
}
static int backup_clear(GsqlIo *io, const char *dir, const char *manifest) {
    Clear c = {dir, NULL, 0};
    int rc = ioerr(gsql_io_list(io, dir, clear_one, &c));
    gsql_io_remove(io, manifest);
    if (rc == GS_OK) rc = ioerr(gsql_io_sync_dir(io, dir));
    for (size_t i = 0; i < c.n; i++) {
        gsql_io_remove(io, c.names[i]);
        free(c.names[i]);
    }
    free(c.names);
    return rc;
}

static int lsm_backup_begin(struct GsTxn *snap, const char *dest, GsBackup **out) {
    GsStore *db = snap->db;
    *out = NULL;
    if (snap->write) return GS_MISUSE;
    if (db->failed) return GS_FAILED;
    char *base = NULL;
    int rc = gs_db_dir(db->io, dest, 1, &base);
    if (rc) return rc;
    GsBackup *b = calloc(1, sizeof *b);
    b->snap = snap;
    b->db = db;
    b->dest = base;
    b->chunk = malloc(BACKUP_CHUNK);
    *out = b;
    GsStore *t = &b->tmp;
    t->io = db->io;
    t->path = gs_path_with(base, "");
    t->dir = gs_dirname(base);
    rc = backup_clear(db->io, t->dir, base);
    t->block = db->block;
    t->memtable_limit = db->memtable_limit;
    t->max_open = db->max_open;
    t->vsep = db->vsep;
    memcpy(t->identity, db->identity, 16);
    t->next_file = db->next_file; /* the memtable's file: above every copied number */
    t->log_num = t->log_first = 1;
    t->last_seq = t->durable_seq = snap->snap;
    t->cur = version_copy(NULL);
    /* only retirements the snapshot sees: one after it names a tree that is
     * live in the copy's catalog, whose data the copy's own compactions would
     * otherwise drop once its sequences passed the retirement. The logical
     * backup copied them all. */
    for (size_t i = 0; i < db->ndead; i++)
        if (db->dead[i].death <= snap->snap) dead_add(t, db->dead[i].id, db->dead[i].death);
    /* the copy list: every sorted file of the version, and every blob file
     * one of them points into, each once */
    Version *v = snap->v;
    U64s seen = {0};
    for (int l = 0; rc == GS_OK && l < LEVELS; l++)
        for (size_t i = 0; i < v->nfiles[l]; i++) {
            FileMeta *m = v->files[l][i], *c = calloc(1, sizeof *c);
            c->num = m->num; c->size = m->size; c->entries = m->entries;
            gsbuf_set(&c->smallest, m->smallest.p, m->smallest.n);
            gsbuf_set(&c->largest, m->largest.p, m->largest.n);
            c->small_seq = m->small_seq; c->large_seq = m->large_seq; c->seq_over = m->seq_over;
            c->own_blob = m->own_blob;
            c->db = t;
            for (size_t j = 0; j < m->blobs.n; j++) {
                u64_push(&c->blobs, m->blobs.v[j]);
                u64_push(&c->blob_bytes, m->blob_bytes.v[j]);
                blob_ref(t, m->blobs.v[j]);
            }
            version_add(t->cur, l, c);
            b->jobs = realloc(b->jobs, (b->njobs + 1 + m->blobs.n) * sizeof *b->jobs);
            CopyJob *j = &b->jobs[b->njobs++];
            j->src = file_name(db, m->num); j->dst = file_name(t, m->num); j->size = m->size; j->off = 0;
            for (size_t k = 0; k < m->blobs.n; k++) {
                if (u64_has(&seen, m->blobs.v[k])) continue;
                u64_push(&seen, m->blobs.v[k]);
                j = &b->jobs[b->njobs++];
                j->src = blob_name(db, m->blobs.v[k]); j->dst = blob_name(t, m->blobs.v[k]); j->size = UINT64_MAX; j->off = 0;
            }
        }
    u64_free(&seen);
    /* the frozen memtable first, as the older level-0 file */
    if (v->imm) { b->mem = v->imm->sk.head->next[0]; b->then = v->mem->sk.head->next[0]; }
    else b->mem = v->mem->sk.head->next[0];
    return rc;
}

static int lsm_backup_step(GsBackup *bk, uint64_t budget, int *done) {
    GsStore *db = bk->db, *t = &bk->tmp;
    int rc = GS_OK;
    uint64_t spent = 0;
    *done = bk->done;
    if (bk->done) return GS_OK;
    if (db->failed) return GS_FAILED;
    /* the files, a chunk at a time */
    while (bk->at < bk->njobs && spent < budget) {
        CopyJob *j = &bk->jobs[bk->at];
        if (!bk->src) {
            if ((rc = ioerr(gsql_io_open(db->io, j->src, GSQL_IO_READ_ONLY, &bk->src)))) return rc;
            if (j->size == UINT64_MAX && (rc = ioerr(gsql_io_size(bk->src, &j->size)))) return rc;
            gsql_io_remove(db->io, j->dst);
            if ((rc = ioerr(gsql_io_open(db->io, j->dst, GSQL_IO_CREATE | GSQL_IO_EXCLUSIVE, &bk->dst)))) return rc;
        }
        while (j->off < j->size && spent < budget) {
            size_t want = j->size - j->off < BACKUP_CHUNK ? (size_t)(j->size - j->off) : BACKUP_CHUNK, got = 0;
            if (want > budget - spent) want = (size_t)(budget - spent); /* a slice is the budget, not a chunk */
            if ((rc = ioerr(gsql_io_read_at(bk->src, j->off, bk->chunk, want, &got)))) return rc;
            if (got != want) return GS_CORRUPT; /* a pinned immutable file is shorter than it was */
            if ((rc = ioerr(gsql_io_write_at(bk->dst, j->off, bk->chunk, want)))) return rc;
            j->off += want;
            spent += want;
        }
        /* each slice flushes what it wrote: a file synced only when finished
         * left its whole flush to one step (64 MB of dirty pages, ~6 ms) */
        rc = ioerr(gsql_io_sync(bk->dst));
        if (rc) return rc;
        if (j->off < j->size) return GS_OK;
        gsql_io_close(bk->dst);
        gsql_io_close(bk->src);
        bk->dst = bk->src = NULL;
        if (rc) return rc;
        bk->at++;
    }
    if (bk->at < bk->njobs) return GS_OK;
    /* the memtables' view: each key's newest version at or before the
     * snapshot, as one more level-0 file, newest of them all -- two, when a
     * frozen memtable was still being flushed, it first: a key's newest
     * version at the snapshot may be in either */
    for (;;) {
        while (bk->mem && spent < budget) {
            SkNode *n = bk->mem;
            bk->mem = n->next[0];
            if (n->seq > bk->snap->snap) continue;
            if (bk->havekey && bk->lastkey.n == n->kn && memcmp(bk->lastkey.p, n->k, n->kn) == 0) continue;
            gsbuf_set(&bk->lastkey, n->k, n->kn);
            bk->havekey = 1;
            if (!bk->building) { if ((rc = bld_open(t, &bk->b))) return rc; bk->building = 1; }
            Ent e = sk_ent(n);
            bld_add(&bk->b, &e);
            if ((rc = bk->b.rc)) return rc;
            spent += n->kn + n->vn + 16;
        }
        if (bk->mem) return GS_OK;
        if (bk->building) {
            FileMeta *fm = NULL;
            rc = bld_close(&bk->b, &fm);
            bk->building = 0;
            if (rc) return rc;
            if (fm) { version_add(t->cur, 0, fm); hnd_close(t, fm->h); }
        }
        if (!bk->then) break;
        bk->mem = bk->then;
        bk->then = NULL;
        bk->havekey = 0;
    }
    /* an empty log, then the manifest that makes it a store */
    char *ln = log_name(bk->dest, 1);
    GsqlFile *lf = NULL;
    gsql_io_remove(db->io, ln);
    rc = ioerr(gsql_io_open(db->io, ln, GSQL_IO_CREATE, &lf));
    if (lf) { if (rc == GS_OK) rc = ioerr(gsql_io_sync(lf)); gsql_io_close(lf); }
    free(ln);
    if (rc == GS_OK) rc = manifest_write(t);
    if (rc == GS_OK) bk->done = *done = 1;
    return rc;
}

static void lsm_backup_end(GsBackup *bk) {
    if (!bk) return;
    if (bk->src) gsql_io_close(bk->src);
    if (bk->dst) gsql_io_close(bk->dst);
    if (bk->building && bk->b.f) gsql_io_close(bk->b.f);
    GsStore *t = &bk->tmp;
    if (!bk->done && t->dir) backup_clear(bk->db->io, t->dir, t->path); /* a partial copy goes whole */
    for (size_t i = 0; i < bk->njobs; i++) { free(bk->jobs[i].src); free(bk->jobs[i].dst); }
    free(bk->jobs);
    if (t->cur) version_unref(t, t->cur);
    for (size_t i = 0; i < t->nblob; i++) hnd_free(t, t->blob[i].h);
    free(t->blob);
    free(t->dead);
    u64_free(&t->garbage);
    free(t->path);
    free(t->dir);
    gsbuf_free(&bk->lastkey);
    free(bk->chunk);
    free(bk->dest);
    free(bk);
}

static int lsm_backup(struct GsTxn *snap, const char *dest) {
    GsBackup *b = NULL;
    int done = 0, rc = lsm_backup_begin(snap, dest, &b);
    while (rc == GS_OK && !done) rc = lsm_backup_step(b, UINT64_MAX, &done);
    lsm_backup_end(b);
    return rc;
}

static void lsm_stats(GsStore *db, GsStats *out) {
    *out = db->st;
    uint64_t total = 0, sz = 0;
    for (int l = 0; l < LEVELS; l++)
        for (size_t i = 0; i < db->cur->nfiles[l]; i++) total += db->cur->files[l][i]->size;
    /* and the blob files, which this left out: a test bounding space through
     * it could not see the files blob reclamation is about */
    for (size_t i = 0; i < db->nblob; i++) total += blob_total(db, &db->blob[i]);
    if (db->log) { gsql_io_size(db->log, &sz); total += sz; }
    if (db->spare) { sz = 0; gsql_io_size(db->spare, &sz); total += sz; }
    out->file_bytes = total;
}


/* A database is a directory (§7 question 6): resolve it, then open the files
 * inside it by their old names. */
static int lsm_open(const char *path, const GsOptions *opt, GsStore **out) {
    GsqlIo *io = opt && opt->io ? opt->io : gsql_io_real();
    char *base = NULL;
    int rc = gs_db_dir(io, path, opt && opt->create, &base);
    if (!(opt && opt->io)) gsql_io_free(io);
    if (rc) return rc;
    rc = lsm_open_base(base, opt, out);
    free(base);
    return rc;
}

const GsEngine gs_engine_lsm = {
    "lsm",
    lsm_open, lsm_close,
    lsm_begin_read, lsm_begin_write, lsm_commit, lsm_sync, lsm_abort,
    lsm_tree_create, lsm_tree_drop, lsm_tree_list, lsm_tree_replace,
    lsm_get, lsm_put, lsm_del,
    lsm_cursor_open, lsm_seek, lsm_first, lsm_last, lsm_next, lsm_prev, lsm_key, lsm_value,
    lsm_cursor_put, lsm_cursor_delete, lsm_cursor_close,
    lsm_backup, lsm_backup_begin, lsm_backup_step, lsm_backup_end, lsm_check, lsm_stats,
};

#include "store_common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ---- CRC32C (Castagnoli), table-driven ---------------------------------- */

static uint32_t crc_table[8][256];
static int crc_ready;

static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0x82F63B78u & (0u - (c & 1u)));
        crc_table[0][i] = c;
    }
    for (uint32_t i = 0; i < 256; i++)
        for (int t = 1; t < 8; t++)
            crc_table[t][i] = (crc_table[t - 1][i] >> 8) ^ crc_table[0][crc_table[t - 1][i] & 0xFF];
    crc_ready = 1;
}

uint32_t crc32c_extend_portable(uint32_t crc, const void *data, size_t n) {
    if (!crc_ready) crc_init();
    const unsigned char *p = data;
    uint32_t c = ~crc;
    while (n >= 8) { /* slicing-by-8 */
        c ^= (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        c = crc_table[7][c & 0xFF] ^ crc_table[6][(c >> 8) & 0xFF] ^ crc_table[5][(c >> 16) & 0xFF] ^
            crc_table[4][c >> 24] ^ crc_table[3][p[4]] ^ crc_table[2][p[5]] ^ crc_table[1][p[6]] ^
            crc_table[0][p[7]];
        p += 8;
        n -= 8;
    }
    while (n--) c = (c >> 8) ^ crc_table[0][(c ^ *p++) & 0xFF];
    return ~c;
}

/* The processor's own CRC32C where there is one. A scan larger than the
 * block cache checks every block it reads, and table-driven CRC32C was about
 * 3 instructions a byte of it (measured with callgrind, 2026-10-29). x86-64
 * only so far, behind a runtime check, so a build for any x86-64 still runs
 * on a processor without SSE4.2; everywhere else, and when GSQL_CRC=portable
 * is set, the portable version above. tests/gsql/store_test.c holds the two
 * to the same answers. */
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("sse4.2"))) static uint32_t crc_hw(uint32_t crc, const void *data, size_t n) {
    const unsigned char *p = data;
    uint64_t c = (uint32_t)~crc;
    while (n && ((uintptr_t)p & 7)) { c = __builtin_ia32_crc32qi((uint32_t)c, *p++); n--; }
    while (n >= 8) {
        uint64_t w;
        memcpy(&w, p, 8); /* little-endian, the byte order the instruction folds in */
        c = __builtin_ia32_crc32di(c, w);
        p += 8;
        n -= 8;
    }
    while (n--) c = __builtin_ia32_crc32qi((uint32_t)c, *p++);
    return ~(uint32_t)c;
}
static int crc_mode; /* 0 not yet decided, 1 portable, 2 the instruction */
static int crc_decide(void) {
    const char *f = getenv("GSQL_CRC");
    __builtin_cpu_init();
    crc_mode = (f && !strcmp(f, "portable")) || !__builtin_cpu_supports("sse4.2") ? 1 : 2;
    return crc_mode;
}
uint32_t crc32c_extend(uint32_t crc, const void *data, size_t n) {
    int m = crc_mode ? crc_mode : crc_decide();
    return m == 2 ? crc_hw(crc, data, n) : crc32c_extend_portable(crc, data, n);
}
int crc32c_accelerated(void) { return (crc_mode ? crc_mode : crc_decide()) == 2; }
#else
uint32_t crc32c_extend(uint32_t crc, const void *data, size_t n) { return crc32c_extend_portable(crc, data, n); }
int crc32c_accelerated(void) { return 0; }
#endif

uint32_t crc32c(const void *data, size_t n) { return crc32c_extend(0, data, n); }

/* ---- varints ------------------------------------------------------------ */

size_t varint_put(unsigned char *p, uint64_t v) {
    size_t i = 0;
    while (v >= 0x80) {
        p[i++] = (unsigned char)(v | 0x80);
        v >>= 7;
    }
    p[i++] = (unsigned char)v;
    return i;
}

size_t varint_len(uint64_t v) {
    size_t n = 1;
    while (v >= 0x80) { v >>= 7; n++; }
    return n;
}

size_t varint_get(const unsigned char *p, size_t avail, uint64_t *v) {
    uint64_t r = 0;
    for (size_t i = 0; i < avail && i < 10; i++) {
        uint64_t b = p[i] & 0x7F;
        if (i == 9 && b > 1) return 0; /* would wrap past 64 bits */
        r |= b << (7 * i);
        if (!(p[i] & 0x80)) {
            *v = r;
            return i + 1;
        }
    }
    return 0;
}

int bytes_cmp(GsBytes a, GsBytes b) {
    size_t n = a.n < b.n ? a.n : b.n;
    int c = n ? memcmp(a.p, b.p, n) : 0;
    if (c) return c < 0 ? -1 : 1;
    return a.n < b.n ? -1 : a.n > b.n;
}

/* ---- the file header ---------------------------------------------------- */

void gs_header_write(unsigned char *p, const GsFileHeader *h) {
    memset(p, 0, GS_HEADER_FIXED);
    memcpy(p, GS_MAGIC, 8);
    put16(p + 8, h->major);
    put16(p + 10, h->minor);
    put32(p + 12, h->page_size);
    memcpy(p + 16, h->identity, 16);
    put64(p + 32, h->created);
    put32(p + 40, h->flags);
    put32(p + 76, crc32c(p, 76));
}

int gs_header_read(const unsigned char *p, size_t n, uint16_t major, GsFileHeader *h) {
    if (n < GS_HEADER_FIXED || memcmp(p, GS_MAGIC, 8) != 0) return GS_FORMAT;
    if (get32(p + 76) != crc32c(p, 76)) return GS_CORRUPT;
    h->major = get16(p + 8);
    h->minor = get16(p + 10);
    if (h->major != major) return GS_FORMAT;
    h->page_size = get32(p + 12);
    memcpy(h->identity, p + 16, 16);
    h->created = get64(p + 32);
    h->flags = get32(p + 40);
    if (h->page_size < 512 || h->page_size > 65536 || (h->page_size & (h->page_size - 1))) return GS_CORRUPT;
    return GS_OK;
}

/* Through the I/O layer, so the simulated disk makes it deterministic. */
void gs_identity_new(GsqlIo *io, unsigned char out[16]) { gsql_io_random(io, out, 16); }

/* ---- the page header ---------------------------------------------------- */

void gs_page_seal(unsigned char *page, uint32_t ps, uint64_t pgno, uint64_t commit_id) {
    put64(page + 8, pgno);
    put64(page + 16, commit_id);
    put32(page + 4, 0);
    put32(page + 4, crc32c(page, ps));
}

int gs_page_verify(const unsigned char *page, uint32_t ps, uint64_t pgno) {
    uint32_t want = get32(page + 4);
    /* Checksum the page with its checksum field zero, without copying it. */
    static const unsigned char zero4[4] = {0, 0, 0, 0};
    uint32_t c = crc32c_extend(0, page, 4);
    c = crc32c_extend(c, zero4, 4);
    c = crc32c_extend(c, page + 8, ps - 8);
    if (c != want) return GS_CORRUPT;
    if (page[0] == 0) return GS_CORRUPT;
    if (get64(page + 8) != pgno) return GS_CORRUPT;
    return GS_OK;
}

/* ---- buffers, paths, statuses ------------------------------------------- */

int gsbuf_reserve(GsBuf *b, size_t n) {
    if (n <= b->cap) return 1;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < n) cap *= 2;
    unsigned char *p = realloc(b->p, cap);
    if (!p) return 0;
    b->p = p;
    b->cap = cap;
    return 1;
}
int gsbuf_append(GsBuf *b, const void *p, size_t n) {
    if (!gsbuf_reserve(b, b->n + n)) return 0;
    if (n) memcpy(b->p + b->n, p, n);
    b->n += n;
    return 1;
}
int gsbuf_set(GsBuf *b, const void *p, size_t n) {
    b->n = 0;
    return gsbuf_append(b, p, n);
}
void gsbuf_free(GsBuf *b) {
    free(b->p);
    b->p = NULL;
    b->n = b->cap = 0;
}

char *gs_dirname(const char *path) {
    const char *s = strrchr(path, '/');
    size_t n = s ? (size_t)(s - path) : 0;
    char *d = malloc(n + 2);
    if (!d) return NULL;
    if (s && n == 0) { d[0] = '/'; d[1] = 0; return d; }
    memcpy(d, path, n);
    d[n] = 0;
    return d;
}

char *gs_path_with(const char *path, const char *suffix) {
    size_t a = strlen(path), b = strlen(suffix);
    char *p = malloc(a + b + 1);
    if (!p) return NULL;
    memcpy(p, path, a);
    memcpy(p + a, suffix, b + 1);
    return p;
}

int gs_db_owned(const char *name) {
    size_t b = strlen(GS_DB_BASE);
    if (strncmp(name, GS_DB_BASE, b) != 0) return 0;
    return name[b] == 0 || name[b] == '.' || name[b] == '-';
}

typedef struct { int foreign, any; } DirScan;
static int scan_one(void *ctx, const char *name) {
    DirScan *s = ctx;
    s->any = 1;
    if (!gs_db_owned(name)) s->foreign = 1;
    return 0;
}

int gs_db_dir(GsqlIo *io, const char *dir, int create, char **base) {
    *base = NULL;
    DirScan s = {0, 0};
    int rc = gsql_io_list(io, dir, scan_one, &s);
    if (rc == ENOENT) {
        if (!create) return GS_NOT_FOUND;
        if ((rc = gsql_io_mkdir(io, dir))) return gs_io_status(rc);
        char *parent = gs_dirname(dir);
        rc = gsql_io_sync_dir(io, parent);
        free(parent);
        if (rc) return gs_io_status(rc);
    } else if (rc) {
        return rc == ENOTDIR ? GS_FORMAT : gs_io_status(rc);
    } else if (s.foreign) {
        return GS_FORMAT; /* not ours: it holds a file no engine would write */
    }
    *base = malloc(strlen(dir) + strlen(GS_DB_BASE) + 2);
    if (!*base) return GS_IO;
    sprintf(*base, "%s/%s", dir, GS_DB_BASE);
    return GS_OK;
}

int gs_io_status(int rc) {
    if (rc == 0) return GS_OK;
    if (rc == ENOSPC) return GS_FULL;
    if (rc == EWOULDBLOCK) return GS_BUSY;
    return GS_IO;
}

const char *gs_status_name(int s) {
    static const char *names[] = {"OK", "NOT_FOUND", "EXISTS", "ORDER", "BUSY", "TOO_LARGE", "INVALIDATED",
                                  "CORRUPT", "IO", "FULL", "FAILED", "FORMAT", "READ_ONLY", "MISUSE"};
    if (s >= 0 && s <= GS_MISUSE) return names[s];
    return "?";
}

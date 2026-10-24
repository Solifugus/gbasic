#ifndef GSQL_RESEARCH_COMMON_H
#define GSQL_RESEARCH_COMMON_H

/* Shared by every candidate (docs/gsql_storage_design.md §3.1): little-endian
 * fields by declaration, CRC32C, LEB128 varints, the file header and the page
 * header. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "store.h"

static inline void put16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static inline void put32(unsigned char *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i));
}
static inline void put64(unsigned char *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (unsigned char)(v >> (8 * i));
}
static inline uint16_t get16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t get32(const unsigned char *p) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) v = v << 8 | p[i];
    return v;
}
static inline uint64_t get64(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}
/* Big-endian, for keys that must sort numerically under memcmp. */
static inline void put64be(unsigned char *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (unsigned char)(v >> (56 - 8 * i));
}
static inline uint64_t get64be(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = v << 8 | p[i];
    return v;
}

uint32_t crc32c(const void *data, size_t n);
uint32_t crc32c_extend(uint32_t crc, const void *data, size_t n);

/* LEB128. put answers the bytes written (<= 10); get answers bytes read, 0 on
 * a truncated or overlong encoding. */
size_t varint_put(unsigned char *p, uint64_t v);
size_t varint_get(const unsigned char *p, size_t avail, uint64_t *v);
size_t varint_len(uint64_t v);

int bytes_cmp(GsBytes a, GsBytes b);
static inline GsBytes gsb(const void *p, size_t n) { GsBytes b = {(const unsigned char *)p, n}; return b; }
static inline GsBytes gss(const char *s) { return gsb(s, strlen(s)); }

/* ---- §3.1.1 the file header (page 0, first 80 bytes) -------------------- */

#define GS_MAGIC "GSQLDB\0\x01"
#define GS_FORMAT_MAJOR 0   /* a spike format: no compatibility promised */
#define GS_HEADER_FIXED 80  /* the root area begins here */

typedef struct {
    uint16_t major, minor;
    uint32_t page_size;
    unsigned char identity[16];
    uint64_t created;
    uint32_t flags;
} GsFileHeader;

/* The design puts the header checksum in page 0's last 4 bytes, but each
 * candidate's root area (from byte 80) changes after creation, so a checksum
 * over the whole page would go stale on every commit. As built it covers the
 * fixed 76 bytes and sits at [76, 80), the end of the key-check block, which is
 * unused while encryption is off. */
void gs_header_write(unsigned char *p, const GsFileHeader *h);
int gs_header_read(const unsigned char *p, size_t n, GsFileHeader *h); /* GS_OK, GS_FORMAT, GS_CORRUPT */
void gs_identity_new(GsqlIo *io, unsigned char out[16]);

/* ---- §3.1.2 the page header (every page) -------------------------------- */

#define GS_PAGE_HEADER 40
#define GS_PAGE_TAG 16
enum {
    GS_PT_INTERIOR = 1, GS_PT_LEAF = 2, GS_PT_OVERFLOW = 3, GS_PT_FREELIST = 4,
    GS_PT_VERSION = 5, GS_PT_META = 6
};

/* Stamp type, page number and commit id, then the checksum over the page with
 * the checksum field zero. */
void gs_page_seal(unsigned char *page, uint32_t page_size, uint64_t pgno, uint64_t commit_id);
/* GS_OK, or GS_CORRUPT when the checksum fails or the page number is not the
 * one asked for (a page read from the wrong place is detected, not trusted).
 * An all-zero page is CORRUPT too: it was never written. */
int gs_page_verify(const unsigned char *page, uint32_t page_size, uint64_t pgno);
static inline unsigned gs_page_type(const unsigned char *page) { return page[0]; }
static inline uint64_t gs_page_commit(const unsigned char *page) { return get64(page + 16); }

/* ---- growable buffers ---------------------------------------------------- */

typedef struct {
    unsigned char *p;
    size_t n, cap;
} GsBuf;
int gsbuf_reserve(GsBuf *b, size_t n);
int gsbuf_append(GsBuf *b, const void *p, size_t n);
int gsbuf_set(GsBuf *b, const void *p, size_t n);
void gsbuf_free(GsBuf *b);

/* A path's directory, for sync_dir: "" if it has none. Caller frees. */
char *gs_dirname(const char *path);
/* path + suffix, malloc'd. */
char *gs_path_with(const char *path, const char *suffix);

/* Map an I/O layer errno to a status. */
int gs_io_status(int rc);

#endif

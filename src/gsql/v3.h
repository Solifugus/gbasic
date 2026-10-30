#ifndef GBASIC_GSQL_V3_H
#define GBASIC_GSQL_V3_H

/* Reading and writing `serialize` version 3 values, inside G-SQL. The table
 * layer (src/gsql/table.c) holds rows and schemas in this encoding and the
 * client protocol (src/gsql/wire.c) carries its bodies in it, so the two share
 * ONE reader and ONE writer: a second copy of the walker would be a second
 * thing that can disagree about what a well-formed value is.
 *
 * Internal to src/gsql. A value is one tag byte and its payload, no header;
 * the layout is gsql_storage_design.md §3.3. */

#include <stddef.h>
#include <stdint.h>

#include "gsql_table.h"   /* GsqlBuf, GsBytes */

#define V3_MAX_DEPTH 256

typedef struct {
    const unsigned char *p;
    size_t len, pos;
    int ok;
} V3In;

/* What a walk found that a column's rules turn on. */
typedef struct {
    unsigned tag;          /* the top-level tag */
    unsigned exact;        /* number: the exactness byte */
    unsigned time_only;    /* datetime */
    unsigned precision;    /* datetime: 1 year .. 6 second */
    char currency[8];      /* money: the alphabetic code ("" if the writer had none) */
    unsigned bad_tag;      /* a kind nothing may store, found at some depth (0: none) */
    int nested_array;      /* an array somewhere below the top */
} V3Walk;

/* A record's fields, borrowed from the bytes. */
typedef struct {
    const unsigned char *name;
    size_t nlen;
    const unsigned char *v;
    size_t vlen;
} V3Field;

/* buffers: grow by doubling, abort() on exhaustion */
void v3_put(GsqlBuf *b, const void *p, size_t n);
void v3_u8(GsqlBuf *b, unsigned v);
void v3_le(GsqlBuf *b, uint64_t v, int width);
void v3_uleb(GsqlBuf *b, uint64_t v);
void v3_set(GsqlBuf *b, const void *p, size_t n);

/* reading: a cursor over bytes that turns `ok` off at the first problem */
uint64_t v3_in_le(V3In *in, int width);
uint64_t v3_in_uleb(V3In *in);
void v3_in_skip(V3In *in, uint64_t n);
int v3_walk(V3In *in, int depth, V3Walk *w);
int v3_walk_whole(const unsigned char *p, size_t n, V3Walk *w);
const char *v3_tag_name(unsigned tag);
long v3_record_fields(const unsigned char *p, size_t n, V3Field *out, long max);
const V3Field *v3_field_named(const V3Field *f, long n, const char *name);
long v3_array_items(const unsigned char *p, size_t n, GsBytes *out, long max);
int v3_read_u64(const unsigned char *p, size_t n, uint64_t *v);
int v3_read_bool(const unsigned char *p, size_t n, int *v);
char *v3_read_text(const unsigned char *p, size_t n);

/* writing */
void v3_w_unknown(GsqlBuf *b);
void v3_w_bool(GsqlBuf *b, int v);
void v3_w_int(GsqlBuf *b, uint64_t v);
void v3_w_text(GsqlBuf *b, const char *s);
void v3_w_record(GsqlBuf *b, size_t nfields);
void v3_w_array(GsqlBuf *b, size_t n);
void v3_w_name(GsqlBuf *b, const char *s);
#endif

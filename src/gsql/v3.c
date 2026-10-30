/* The v3 codec G-SQL shares (src/gsql/v3.h). Moved out of src/gsql/table.c
 * unchanged when the client protocol became its second user. */

#include "v3.h"
#include "serialize_tags.h"

#include <stdlib.h>
#include <string.h>

void gsql_buf_free(GsqlBuf *b) {
    free(b->bytes);
    b->bytes = NULL;
    b->length = b->capacity = 0;
}

void v3_put(GsqlBuf *b, const void *p, size_t n) {
    if (b->length + n > b->capacity) {
        size_t cap = b->capacity ? b->capacity : 64;
        while (b->length + n > cap) {
            cap *= 2;
        }
        unsigned char *g = realloc(b->bytes, cap);
        if (!g) {
            abort();
        }
        b->bytes = g;
        b->capacity = cap;
    }
    if (n) {
        memcpy(b->bytes + b->length, p, n);
    }
    b->length += n;
}

void v3_u8(GsqlBuf *b, unsigned v) {
    unsigned char c = (unsigned char)v;
    v3_put(b, &c, 1);
}

void v3_le(GsqlBuf *b, uint64_t v, int width) {
    unsigned char t[8];
    for (int i = 0; i < width; i++) {
        t[i] = (unsigned char)(v >> (8 * i));
    }
    v3_put(b, t, (size_t)width);
}

void v3_uleb(GsqlBuf *b, uint64_t v) {
    do {
        unsigned c = v & 0x7f;
        v >>= 7;
        v3_u8(b, c | (v ? 0x80 : 0));
    } while (v);
}

void v3_set(GsqlBuf *b, const void *p, size_t n) {
    b->length = 0;
    v3_put(b, p, n);
}


/* ==== 2. reading a v3 value ============================================== */


uint64_t v3_in_le(V3In *in, int width) {
    if (!in->ok || in->pos + (size_t)width > in->len) {
        in->ok = 0;
        return 0;
    }
    uint64_t v = 0;
    for (int i = 0; i < width; i++) {
        v |= (uint64_t)in->p[in->pos + i] << (8 * i);
    }
    in->pos += (size_t)width;
    return v;
}

uint64_t v3_in_uleb(V3In *in) {
    uint64_t v = 0;
    for (int shift = 0; shift < 70; shift += 7) {
        unsigned byte = (unsigned)v3_in_le(in, 1);
        if (!in->ok || (shift == 63 && (byte & 0x7e))) {
            in->ok = 0;
            return 0;
        }
        v |= (uint64_t)(byte & 0x7f) << shift;
        if (!(byte & 0x80)) {
            return v;
        }
    }
    in->ok = 0;
    return 0;
}

void v3_in_skip(V3In *in, uint64_t n) {
    if (!in->ok || n > in->len - in->pos) {
        in->ok = 0;
        return;
    }
    in->pos += (size_t)n;
}


/* Validates ONE value at in->pos and advances past it. Answers 0 when the
 * bytes are not a well-formed value (or nest deeper than V3_MAX_DEPTH). */
int v3_walk(V3In *in, int depth, V3Walk *w) {
    if (depth > V3_MAX_DEPTH) {
        return 0;
    }
    unsigned tag = (unsigned)v3_in_le(in, 1);
    if (!in->ok) {
        return 0;
    }
    if (depth == 0) {
        w->tag = tag;
    }
    switch (tag) {
    case SER_NULL:
    case SER_UNKNOWN:
        return 1;
    case SER_BOOL: {
        unsigned b = (unsigned)v3_in_le(in, 1);
        return in->ok && b <= 1;
    }
    case SER_NUMBER: {
        unsigned exact = (unsigned)v3_in_le(in, 1);
        v3_in_le(in, 8);
        if (depth == 0) {
            w->exact = exact;
        }
        return in->ok && exact <= 1;
    }
    case SER_STRING:
        v3_in_skip(in, v3_in_uleb(in));
        return in->ok;
    case SER_MONEY: {
        v3_in_le(in, 8);
        v3_in_le(in, 1);
        v3_in_le(in, 2);
        unsigned alen = (unsigned)v3_in_le(in, 1);
        if (!in->ok || alen > 7 || in->pos + alen > in->len) {
            return 0;
        }
        if (depth == 0) {
            memcpy(w->currency, in->p + in->pos, alen);
            w->currency[alen] = 0;
        }
        in->pos += alen;
        return 1;
    }
    case SER_DATETIME: {
        for (int i = 0; i < 6; i++) {
            v3_in_le(in, 4);
        }
        unsigned time_only = (unsigned)v3_in_le(in, 1);
        unsigned precision = (unsigned)v3_in_le(in, 1);
        if (!in->ok || time_only > 1 || precision < 1 || precision > 6) {
            return 0;
        }
        if (depth == 0) {
            w->time_only = time_only;
            w->precision = precision;
        }
        return 1;
    }
    case SER_DURATION:
        for (int i = 0; i < 7; i++) {
            v3_in_le(in, 4);
        }
        if (!w->bad_tag) {
            w->bad_tag = tag;
        }
        return in->ok;
    case SER_ARRAY: {
        uint64_t n = v3_in_uleb(in);
        if (!in->ok || n > in->len - in->pos) {
            return 0;   /* every element is at least one byte */
        }
        if (depth > 0) {
            w->nested_array = 1;
        }
        for (uint64_t i = 0; i < n; i++) {
            if (!v3_walk(in, depth + 1, w)) {
                return 0;
            }
        }
        return 1;
    }
    case SER_RECORD: {
        uint64_t n = v3_in_uleb(in);
        if (!in->ok || n > in->len - in->pos) {
            return 0;
        }
        for (uint64_t i = 0; i < n; i++) {
            v3_in_skip(in, v3_in_uleb(in));
            if (!in->ok || !v3_walk(in, depth + 1, w)) {
                return 0;
            }
        }
        return 1;
    }
    case SER_FILE:
    case SER_DIR:
        v3_in_skip(in, v3_in_uleb(in));
        if (!w->bad_tag) {
            w->bad_tag = tag;
        }
        return in->ok;
    case SER_FUNCTION:
        v3_in_skip(in, v3_in_uleb(in));
        v3_in_skip(in, v3_in_uleb(in));
        if (!w->bad_tag) {
            w->bad_tag = tag;
        }
        return in->ok;
    case SER_REGEX:
        v3_in_skip(in, v3_in_uleb(in));
        v3_in_skip(in, v3_in_uleb(in));
        if (!w->bad_tag) {
            w->bad_tag = tag;
        }
        return in->ok;
    default:
        /* SER_ACTOR included: it is meaningful only in spawn arguments. */
        return 0;
    }
}

/* One whole value, exactly `n` bytes. */
int v3_walk_whole(const unsigned char *p, size_t n, V3Walk *w) {
    V3In in = { p, n, 0, 1 };
    memset(w, 0, sizeof *w);
    return v3_walk(&in, 0, w) && in.pos == n;
}

const char *v3_tag_name(unsigned tag) {
    switch (tag) {
    case SER_NULL: return "nothing";
    case SER_UNKNOWN: return "unknown";
    case SER_BOOL: return "a boolean";
    case SER_NUMBER: return "a number";
    case SER_STRING: return "a string";
    case SER_ARRAY: return "an array";
    case SER_RECORD: return "a record";
    case SER_DATETIME: return "a date-time";
    case SER_DURATION: return "a duration";
    case SER_MONEY: return "money";
    case SER_FILE: return "a file";
    case SER_DIR: return "a directory";
    case SER_FUNCTION: return "a function";
    case SER_REGEX: return "a regex";
    default: return "a value";
    }
}


/* Splits a record value into its fields. Answers the count, or -1. */
long v3_record_fields(const unsigned char *p, size_t n, V3Field *out, long max) {
    V3In in = { p, n, 0, 1 };
    if (v3_in_le(&in, 1) != SER_RECORD) {
        return -1;
    }
    uint64_t count = v3_in_uleb(&in);
    if (!in.ok || count > (uint64_t)max) {
        return -1;
    }
    for (uint64_t i = 0; i < count; i++) {
        uint64_t nl = v3_in_uleb(&in);
        size_t at = in.pos;
        v3_in_skip(&in, nl);
        if (!in.ok) {
            return -1;
        }
        size_t vstart = in.pos;
        V3Walk w;
        memset(&w, 0, sizeof w);
        if (!v3_walk(&in, 1, &w)) {
            return -1;
        }
        out[i].name = p + at;
        out[i].nlen = (size_t)nl;
        out[i].v = p + vstart;
        out[i].vlen = in.pos - vstart;
    }
    return in.pos == n ? (long)count : -1;
}

const V3Field *v3_field_named(const V3Field *f, long n, const char *name) {
    size_t len = strlen(name);
    for (long i = 0; i < n; i++) {
        if (f[i].nlen == len && memcmp(f[i].name, name, len) == 0) {
            return &f[i];
        }
    }
    return NULL;
}

/* Elements of an array value, borrowed. Answers the count, or -1. */
long v3_array_items(const unsigned char *p, size_t n, GsBytes *out, long max) {
    V3In in = { p, n, 0, 1 };
    if (v3_in_le(&in, 1) != SER_ARRAY) {
        return -1;
    }
    uint64_t count = v3_in_uleb(&in);
    if (!in.ok || count > (uint64_t)max) {
        return -1;
    }
    for (uint64_t i = 0; i < count; i++) {
        size_t at = in.pos;
        V3Walk w;
        memset(&w, 0, sizeof w);
        if (!v3_walk(&in, 1, &w)) {
            return -1;
        }
        out[i].p = p + at;
        out[i].n = in.pos - at;
    }
    return in.pos == n ? (long)count : -1;
}

int v3_read_u64(const unsigned char *p, size_t n, uint64_t *v) {
    if (n != 10 || p[0] != SER_NUMBER || p[1] != 1) {
        return 0;
    }
    uint64_t x = 0;
    for (int i = 0; i < 8; i++) {
        x |= (uint64_t)p[2 + i] << (8 * i);
    }
    if ((int64_t)x < 0) {
        return 0;
    }
    *v = x;
    return 1;
}

int v3_read_bool(const unsigned char *p, size_t n, int *v) {
    if (n != 2 || p[0] != SER_BOOL || p[1] > 1) {
        return 0;
    }
    *v = p[1];
    return 1;
}

/* A string value as a NUL-terminated copy, refusing an interior NUL. */
char *v3_read_text(const unsigned char *p, size_t n) {
    V3In in = { p, n, 0, 1 };
    if (v3_in_le(&in, 1) != SER_STRING) {
        return NULL;
    }
    uint64_t len = v3_in_uleb(&in);
    if (!in.ok || len != n - in.pos || memchr(p + in.pos, 0, (size_t)len)) {
        return NULL;
    }
    char *s = malloc((size_t)len + 1);
    if (!s) {
        abort();
    }
    memcpy(s, p + in.pos, (size_t)len);
    s[len] = 0;
    return s;
}

/* ==== 3. writing v3 values =============================================== */

void v3_w_unknown(GsqlBuf *b) { v3_u8(b, SER_UNKNOWN); }

void v3_w_bool(GsqlBuf *b, int v) {
    v3_u8(b, SER_BOOL);
    v3_u8(b, v ? 1 : 0);
}

void v3_w_int(GsqlBuf *b, uint64_t v) {
    v3_u8(b, SER_NUMBER);
    v3_u8(b, 1);
    v3_le(b, v, 8);
}

void v3_w_text(GsqlBuf *b, const char *s) {
    size_t n = strlen(s);
    v3_u8(b, SER_STRING);
    v3_uleb(b, n);
    v3_put(b, s, n);
}

void v3_w_record(GsqlBuf *b, size_t nfields) {
    v3_u8(b, SER_RECORD);
    v3_uleb(b, nfields);
}

void v3_w_array(GsqlBuf *b, size_t n) {
    v3_u8(b, SER_ARRAY);
    v3_uleb(b, n);
}

void v3_w_name(GsqlBuf *b, const char *s) {
    size_t n = strlen(s);
    v3_uleb(b, n);
    v3_put(b, s, n);
}


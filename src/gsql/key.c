/* G-SQL's index key encoding (docs/gsql_storage_design.md §3.2). See
 * include/gsql_key.h for the contract; this file is the layout.
 *
 * THE ORACLE IS gBASIC ITSELF: tests/run_gsql_key.sh hands this encoder values
 * gBASIC serialized, together with gBASIC's own answer to `<` and `=` for every
 * pair, and requires memcmp to give the same answer. Nothing here was accepted
 * on reasoning alone. */

#include "gsql_key.h"
#include "serialize_tags.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Component tags. Their ORDER only matters between kinds gBASIC refuses to
 * compare, so it is a storage detail -- except absence, which is highest so it
 * sorts LAST ascending and FIRST descending (the 2026-10-07 ruling). */
enum {
    KEY_BOOL = 0x10,
    KEY_NEG_INF = 0x21,
    KEY_NEGATIVE = 0x22,
    KEY_ZERO = 0x23,
    KEY_POSITIVE = 0x24,
    KEY_POS_INF = 0x25,
    KEY_NAN = 0x26,
    KEY_STRING = 0x30,
    KEY_MONEY_TYPED = 0x40,
    KEY_MONEY = 0x41,
    KEY_DATETIME = 0x52,
    KEY_ABSENT = 0xF0
};

#define EXPONENT_BIAS 1100

void gsql_key_init(GsqlKey *key) {
    key->bytes = NULL;
    key->length = 0;
    key->capacity = 0;
}

void gsql_key_free(GsqlKey *key) {
    free(key->bytes);
    gsql_key_init(key);
}

static void put(GsqlKey *key, const void *data, size_t n) {
    if (key->length + n > key->capacity) {
        size_t cap = key->capacity ? key->capacity : 32;
        while (key->length + n > cap) {
            cap *= 2;
        }
        unsigned char *grown = realloc(key->bytes, cap);
        if (!grown) {
            abort();
        }
        key->bytes = grown;
        key->capacity = cap;
    }
    memcpy(key->bytes + key->length, data, n);
    key->length += n;
}

static void put_u8(GsqlKey *key, unsigned v) {
    unsigned char b = (unsigned char)v;
    put(key, &b, 1);
}

static void put_be(GsqlKey *key, uint64_t v, int width) {
    unsigned char b[8];
    for (int i = 0; i < width; i++) {
        b[i] = (unsigned char)(v >> (8 * (width - 1 - i)));
    }
    put(key, b, (size_t)width);
}

/* A signed integer with its sign bit flipped, big-endian: unsigned byte order
 * is then signed numeric order. */
static void put_signed(GsqlKey *key, int64_t v, int width) {
    uint64_t bias = (uint64_t)1 << (8 * width - 1);
    put_be(key, (uint64_t)v + bias, width);
}

typedef struct {
    const unsigned char *p;
    size_t len;
    size_t pos;
    int ok;
} In;

static uint64_t in_le(In *in, int width) {
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

static int32_t in_i32(In *in) {
    return (int32_t)(uint32_t)in_le(in, 4);
}

static uint64_t in_uleb(In *in) {
    uint64_t v = 0;
    for (int shift = 0; shift < 70; shift += 7) {
        unsigned byte = (unsigned)in_le(in, 1);
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

/* A finite non-zero number as |x| = m * 2^e. Every int64 and every double is
 * one, so writing the normalised form 1.f * 2^E gives the SAME bytes for exact 5
 * and double 5.0 -- the key encodes the mathematical value, which is what
 * gBASIC's `5 = 5.0` says. A 63-bit fraction holds an int64's 63 bits and a
 * double's 52 without loss. */
static void put_magnitude(GsqlKey *key, int negative, uint64_t m, int e) {
    int top = 63;
    while (!(m >> top)) {
        top--;
    }
    int exponent = top + e;
    uint64_t fraction = (m << (63 - top)) << 1;   /* the leading 1 is implied */
    uint64_t biased = (uint64_t)(exponent + EXPONENT_BIAS);
    if (negative) {
        /* A larger magnitude is a SMALLER number, so both fields invert. */
        biased = ~biased & 0xffff;
        fraction = ~fraction;
    }
    put_u8(key, negative ? KEY_NEGATIVE : KEY_POSITIVE);
    put_be(key, biased, 2);
    put_be(key, fraction, 8);
}

static GsqlKeyStatus key_number(GsqlKey *key, In *in) {
    unsigned exact = (unsigned)in_le(in, 1);
    uint64_t bits = in_le(in, 8);
    if (!in->ok || exact > 1) {
        return GSQL_KEY_CORRUPT;
    }
    if (exact) {
        int64_t v = (int64_t)bits;
        if (v == 0) {
            put_u8(key, KEY_ZERO);
        } else if (v < 0) {
            /* -(v + 1) + 1 reaches 2^63 for INT64_MIN without overflow. */
            put_magnitude(key, 1, (uint64_t)(-(v + 1)) + 1, 0);
        } else {
            put_magnitude(key, 0, (uint64_t)v, 0);
        }
        return GSQL_KEY_OK;
    }
    int negative = (int)(bits >> 63);
    int field = (int)((bits >> 52) & 0x7ff);
    uint64_t frac = bits & 0xfffffffffffffULL;
    if (field == 0x7ff) {
        /* NaN has a fixed place after +inf and is equal to itself
         * (PostgreSQL's rule), §3.2: gBASIC gives it no order to agree with. */
        put_u8(key, frac ? KEY_NAN : (negative ? KEY_NEG_INF : KEY_POS_INF));
    } else if (field == 0 && frac == 0) {
        put_u8(key, KEY_ZERO);   /* -0 is keyed as zero, as `0.0 = -0.0` says */
    } else if (field == 0) {
        put_magnitude(key, negative, frac, -1074);
    } else {
        put_magnitude(key, negative, frac | (1ULL << 52), field - 1075);
    }
    return GSQL_KEY_OK;
}

/* UTF-8 bytes with 0x00 escaped as 00 FF and terminated by 00 00, which keeps
 * `"a" < "a" + chr(0) < "ab"` (measured, §3.2) and makes it self-delimiting. */
static GsqlKeyStatus key_string(GsqlKey *key, In *in) {
    uint64_t n = in_uleb(in);
    if (!in->ok || n > in->len - in->pos) {
        return GSQL_KEY_CORRUPT;
    }
    put_u8(key, KEY_STRING);
    const unsigned char *s = in->p + in->pos;
    size_t run = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == 0) {
            put(key, s + run, i - run);
            put_u8(key, 0x00);
            put_u8(key, 0xFF);
            run = i + 1;
        }
    }
    put(key, s + run, (size_t)n - run);
    put_u8(key, 0x00);
    put_u8(key, 0x00);
    in->pos += (size_t)n;
    return GSQL_KEY_OK;
}

static GsqlKeyStatus key_money(GsqlKey *key, In *in, unsigned flags) {
    int64_t units = (int64_t)in_le(in, 8);
    (void)in_le(in, 1);                           /* exponent: fixed per currency */
    unsigned numeric = (unsigned)in_le(in, 2);
    unsigned alen = (unsigned)in_le(in, 1);
    if (!in->ok || alen > 7 || in->pos + alen > in->len) {
        return GSQL_KEY_CORRUPT;
    }
    const unsigned char *alpha = in->p + in->pos;
    in->pos += alen;
    if (flags & GSQL_KEY_MONEY_TYPED) {
        put_u8(key, KEY_MONEY_TYPED);
    } else {
        /* Grouping by currency is a storage detail: gBASIC refuses to order two
         * currencies, so no answer ever depends on which group comes first. A
         * code is letters, so it never contains the 00 terminator, and a value
         * whose code was unknown to its writer is keyed by number behind 01,
         * which no letter is. */
        put_u8(key, KEY_MONEY);
        if (alen) {
            put(key, alpha, alen);
            put_u8(key, 0x00);
        } else {
            put_u8(key, 0x01);
            put_be(key, numeric, 2);
        }
    }
    put_signed(key, units, 8);
    return GSQL_KEY_OK;
}

/* gBASIC's datetime order is TOTAL -- time-only first, then the date (unless
 * time-only), then the time, then the precision, which also takes part in
 * equality -- so one tag covers dates, times and datetimes exactly. That
 * corrects §3.2's first draft, which gave each kind a tag and refused to mix
 * them on the belief that a date and a datetime had no total order. */
static GsqlKeyStatus key_datetime(GsqlKey *key, In *in) {
    int32_t f[6];
    for (int i = 0; i < 6; i++) {
        f[i] = in_i32(in);
    }
    unsigned time_only = (unsigned)in_le(in, 1);
    unsigned precision = (unsigned)in_le(in, 1);
    if (!in->ok || time_only > 1 || precision < 1 || precision > 6) {
        return GSQL_KEY_CORRUPT;
    }
    put_u8(key, KEY_DATETIME);
    put_u8(key, time_only);
    for (int i = time_only ? 3 : 0; i < 6; i++) {
        put_signed(key, f[i], 4);
    }
    put_u8(key, precision);
    return GSQL_KEY_OK;
}

GsqlKeyStatus gsql_key_append(GsqlKey *key, const unsigned char *value,
                              size_t length, size_t *used, unsigned flags) {
    In in = { value, length, 0, 1 };
    size_t start = key->length;
    unsigned tag = (unsigned)in_le(&in, 1);
    GsqlKeyStatus status = GSQL_KEY_OK;
    if (!in.ok) {
        return GSQL_KEY_CORRUPT;
    }
    switch (tag) {
    case SER_NULL:
    case SER_UNKNOWN:
        /* A bound `nothing` is an absence too (§3.2). */
        put_u8(key, KEY_ABSENT);
        break;
    case SER_BOOL: {
        unsigned b = (unsigned)in_le(&in, 1);
        if (!in.ok || b > 1) {
            status = GSQL_KEY_CORRUPT;
            break;
        }
        put_u8(key, KEY_BOOL);
        put_u8(key, b);
        break;
    }
    case SER_NUMBER:
        status = key_number(key, &in);
        break;
    case SER_STRING:
        status = key_string(key, &in);
        break;
    case SER_MONEY:
        status = key_money(key, &in, flags);
        break;
    case SER_DATETIME:
        status = key_datetime(key, &in);
        break;
    case SER_DURATION:
    case SER_ARRAY:
    case SER_RECORD:
    case SER_FILE:
    case SER_DIR:
    case SER_ACTOR:
    case SER_FUNCTION:
    case SER_REGEX:
        status = GSQL_KEY_NOT_INDEXABLE;
        break;
    default:
        status = GSQL_KEY_CORRUPT;
    }
    if (status != GSQL_KEY_OK) {
        key->length = start;
        return status;
    }
    if (flags & GSQL_KEY_DESC) {
        for (size_t i = start; i < key->length; i++) {
            key->bytes[i] = (unsigned char)~key->bytes[i];
        }
    }
    if (used) {
        *used = in.pos;
    }
    return GSQL_KEY_OK;
}

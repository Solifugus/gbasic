/* Holds src/gsql/key.c to gBASIC's own order (tests/run_gsql_key.sh).
 *
 * Reads tests/gsql/key_values.bas's output on stdin: values gBASIC serialized
 * and gBASIC's answer for every pair it will order. For each pair the key
 * memcmp must give that answer -- ascending, descending (the reverse), and for
 * money in a typed column as well -- and composite keys must order
 * lexicographically by their components. NaN, which gBASIC gives no order, is
 * held to the documented position instead: after +inf, equal to itself.
 *
 * Prints `MISMATCH ...` lines and a final `ok N checks, M mismatches`. */

#include "gsql_key.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VALUES 4096

static unsigned char *val[MAX_VALUES];
static size_t vlen[MAX_VALUES];
static int is_nan[MAX_VALUES];
static int nvalues;
static long checks, mismatches;

typedef struct { int i, j, answer; } Pair;
static Pair *pairs;
static size_t npairs, cap_pairs;

static void mismatch(const char *fmt, int a, int b, int got, int want) {
    mismatches++;
    if (mismatches <= 20) {
        printf("MISMATCH ");
        printf(fmt, a, b);
        printf(": memcmp says %d, gBASIC says %d\n", got, want);
    }
}

static unsigned char *unhex(const char *s, size_t *n) {
    size_t len = strlen(s) / 2;
    unsigned char *out = malloc(len ? len : 1);
    for (size_t i = 0; i < len; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (unsigned char)v;
    }
    *n = len;
    return out;
}

static int sign(int v) { return (v > 0) - (v < 0); }

static int keycmp(const GsqlKey *a, const GsqlKey *b) {
    size_t n = a->length < b->length ? a->length : b->length;
    int c = memcmp(a->bytes, b->bytes, n);
    if (c) return sign(c);
    return (a->length > b->length) - (a->length < b->length);
}

/* A value's bytes start after serialize's 4-byte header. */
static int encode(GsqlKey *k, int i, unsigned flags) {
    size_t used = 0;
    GsqlKeyStatus st = gsql_key_append(k, val[i] + 4, vlen[i] - 4, &used, flags);
    if (st != GSQL_KEY_OK) {
        mismatches++;
        printf("MISMATCH value %d was refused (status %d)\n", i, (int)st);
        return 0;
    }
    if (used != vlen[i] - 4) {
        mismatches++;
        printf("MISMATCH value %d used %zu of %zu bytes\n", i, used, vlen[i] - 4);
        return 0;
    }
    return 1;
}

static int both_money(int i, int j) {
    return val[i][4] == 10 && val[j][4] == 10;
}

int main(void) {
    static char line[1 << 16];
    int refusals = 0, refused = 0, pins = 0;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == 'V') {
            int idx;
            char hex[1 << 15];
            if (sscanf(line, "V %d %32767s", &idx, hex) != 2 || idx >= MAX_VALUES) continue;
            val[idx] = unhex(hex, &vlen[idx]);
            if (idx + 1 > nvalues) nvalues = idx + 1;
        } else if (line[0] == 'N') {
            is_nan[atoi(line + 2)] = 1;
        } else if (line[0] == 'C') {
            int i, j;
            char ans[8];
            if (sscanf(line, "C %d %d %7s", &i, &j, ans) != 3) continue;
            if (ans[0] == 'x') continue;
            if (npairs == cap_pairs) {
                cap_pairs = cap_pairs ? cap_pairs * 2 : 1024;
                pairs = realloc(pairs, cap_pairs * sizeof *pairs);
            }
            pairs[npairs++] = (Pair){ i, j, atoi(ans) };
        } else if (line[0] == 'P') {
            char vhex[4096], khex[4096];
            if (sscanf(line, "P %4095s %4095s", vhex, khex) != 2) continue;
            size_t n;
            unsigned char *b = unhex(vhex, &n);
            GsqlKey k;
            gsql_key_init(&k);
            pins++;
            char got[8192] = "";
            if (gsql_key_append(&k, b + 4, n - 4, NULL, 0) == GSQL_KEY_OK) {
                for (size_t i = 0; i < k.length && i < 4000; i++) {
                    sprintf(got + 2 * i, "%02x", k.bytes[i]);
                }
            }
            checks++;
            if (strcmp(got, khex) != 0) {
                mismatches++;
                printf("MISMATCH pinned key for %s: got %s, want %s\n", vhex, got, khex);
            }
            gsql_key_free(&k);
            free(b);
        } else if (line[0] == 'R') {
            size_t n;
            unsigned char *b = unhex(line + 2, &n);
            GsqlKey k;
            gsql_key_init(&k);
            refusals++;
            if (gsql_key_append(&k, b + 4, n - 4, NULL, 0) == GSQL_KEY_NOT_INDEXABLE
                && k.length == 0) {
                refused++;
            } else {
                mismatches++;
                printf("MISMATCH a value with no index order was keyed: %s\n", line + 2);
            }
            gsql_key_free(&k);
            free(b);
        }
    }

    /* Every pair gBASIC orders, ascending and descending. */
    for (size_t p = 0; p < npairs; p++) {
        int i = pairs[p].i, j = pairs[p].j, want = pairs[p].answer;
        GsqlKey a, b;
        gsql_key_init(&a);
        gsql_key_init(&b);
        if (encode(&a, i, 0) && encode(&b, j, 0)) {
            checks++;
            int got = keycmp(&a, &b);
            if (got != want) mismatch("%d vs %d ascending", i, j, got, want);
        }
        a.length = b.length = 0;
        if (encode(&a, i, GSQL_KEY_DESC) && encode(&b, j, GSQL_KEY_DESC)) {
            checks++;
            int got = keycmp(&a, &b);
            if (got != -want) mismatch("%d vs %d descending", i, j, got, -want);
        }
        if (both_money(i, j)) {
            a.length = b.length = 0;
            if (encode(&a, i, GSQL_KEY_MONEY_TYPED) && encode(&b, j, GSQL_KEY_MONEY_TYPED)) {
                checks++;
                int got = keycmp(&a, &b);
                if (got != want) mismatch("%d vs %d as typed money", i, j, got, want);
            }
        }
        gsql_key_free(&a);
        gsql_key_free(&b);
    }

    /* COMPOSITES: (x ASC, y DESC) must order by x, then by y reversed. Built
     * from pairs gBASIC answered, so the expectation is still gBASIC's. A
     * component that was not self-delimiting would let y's bytes decide a tie
     * in x wrongly, which is what this is for. */
    long composites = 0;
    for (size_t p = 0; p < npairs && composites < 200000; p += 7) {
        for (size_t q = 0; q < npairs && composites < 200000; q += 13) {
            int x1 = pairs[p].i, x2 = pairs[p].j, y1 = pairs[q].i, y2 = pairs[q].j;
            int want = pairs[p].answer ? pairs[p].answer : -pairs[q].answer;
            GsqlKey a, b;
            gsql_key_init(&a);
            gsql_key_init(&b);
            if (encode(&a, x1, 0) && encode(&a, y1, GSQL_KEY_DESC) &&
                encode(&b, x2, 0) && encode(&b, y2, GSQL_KEY_DESC)) {
                checks++;
                composites++;
                int got = keycmp(&a, &b);
                if (got != want) {
                    mismatches++;
                    if (mismatches <= 20) {
                        printf("MISMATCH composite (%d,%d desc) vs (%d,%d desc): memcmp says %d, want %d\n",
                               x1, y1, x2, y2, got, want);
                    }
                }
            }
            gsql_key_free(&a);
            gsql_key_free(&b);
        }
    }

    /* NaN: the stated exception. After +inf, equal to itself, below absence. */
    int nan_index = -1, inf_index = -1, absent_index = -1;
    for (int i = 0; i < nvalues; i++) {
        if (!val[i]) continue;
        if (is_nan[i]) nan_index = i;
        /* +inf: a number with an inexact flag and the bits 0x7ff0000000000000. */
        if (vlen[i] == 14 && val[i][4] == 4 && val[i][5] == 0 &&
            memcmp(val[i] + 6, "\x00\x00\x00\x00\x00\x00\xf0\x7f", 8) == 0) inf_index = i;
        if (val[i][4] == 2) absent_index = i;
    }
    if (nan_index < 0 || inf_index < 0 || absent_index < 0) {
        mismatches++;
        printf("MISMATCH the battery lacks a NaN, +inf or an absence (%d %d %d)\n",
               nan_index, inf_index, absent_index);
    } else {
        GsqlKey n1, n2, inf, ab;
        gsql_key_init(&n1); gsql_key_init(&n2); gsql_key_init(&inf); gsql_key_init(&ab);
        encode(&n1, nan_index, 0);
        encode(&n2, nan_index, 0);
        encode(&inf, inf_index, 0);
        encode(&ab, absent_index, 0);
        checks += 3;
        if (keycmp(&n1, &n2) != 0) mismatch("NaN %d vs NaN %d", nan_index, nan_index, keycmp(&n1, &n2), 0);
        if (keycmp(&n1, &inf) != 1) mismatch("NaN %d vs +inf %d", nan_index, inf_index, keycmp(&n1, &inf), 1);
        if (keycmp(&n1, &ab) != -1) mismatch("NaN %d vs absence %d", nan_index, absent_index, keycmp(&n1, &ab), -1);
        gsql_key_free(&n1); gsql_key_free(&n2); gsql_key_free(&inf); gsql_key_free(&ab);
    }

    /* CORRUPTION: every truncation of every value is refused and appends
     * nothing, so a short row cannot become a plausible key. */
    long truncations = 0, truncations_refused = 0;
    for (int i = 0; i < nvalues; i++) {
        if (!val[i]) continue;
        for (size_t n = 0; n < vlen[i] - 4; n++) {
            GsqlKey k;
            gsql_key_init(&k);
            truncations++;
            if (gsql_key_append(&k, val[i] + 4, n, NULL, 0) == GSQL_KEY_CORRUPT && k.length == 0) {
                truncations_refused++;
            }
            gsql_key_free(&k);
        }
    }
    checks += 2;
    if (truncations_refused != truncations) {
        mismatches++;
        printf("MISMATCH %ld of %ld truncated values were refused\n", truncations_refused, truncations);
    }
    if (pins < 10) {
        mismatches++;
        printf("MISMATCH only %d pinned keys were checked\n", pins);
    }
    if (refused != refusals || refusals < 5) {
        mismatches++;
        printf("MISMATCH %d of %d unindexable values were refused\n", refused, refusals);
    }

    printf("pairs %zu, composites %ld, truncations %ld\n", npairs, composites, truncations);
    printf("ok %ld checks, %ld mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}

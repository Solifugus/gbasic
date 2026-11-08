/* Exact decimal text (docs/exact_decimal_design.md): one parser for every door
 * a decimal comes through -- the lexer, the runtime's text doors, and G-SQL's
 * SQL literals and index keys, which are plain C beside the interpreter. So it
 * is header-only, as G-SQL's harnesses compile their files alone. */
#ifndef GB_DECIMAL_H
#define GB_DECIMAL_H

#include <stdio.h>
#include <stdlib.h>

/* EXACT DECIMALS (docs/exact_decimal_design.md §2). Digits only, one optional
 * point, an optional sign: an exponent stays inexact by the `1e20` ruling, and
 * whitespace is each door's own business, as for integer text. Trailing zeros
 * are removed so equal values have one representation. Text with no point
 * answers scale 0, an integer; callers ask only about text with one. */
static inline int gb_decimal_from_text(const char *text, long long *mantissa, int *scale) {
    if (!text || !mantissa || !scale) {
        return 0;
    }
    const char *c = text;
    int negative = 0;
    if (*c == '+' || *c == '-') {
        negative = *c == '-';
        c++;
    }
    unsigned long long m = 0;
    int digits = 0, s = 0, point = 0;
    for (; *c; c++) {
        if (*c == '.') {
            if (point) {
                return 0;
            }
            point = 1;
            continue;
        }
        if (*c < '0' || *c > '9') {
            return 0;
        }
        digits++;
        if (point) {
            s++;
        }
        unsigned d = (unsigned)(*c - '0');
        if (m > (9223372036854775807ULL - d) / 10ULL) {
            /* Past int64. Trailing zeros of the fraction are still free, since
             * they normalise away, so only a nonzero digit gives up here. */
            if (point && d == 0) {
                s--;
                continue;
            }
            return 0;
        }
        m = m * 10ULL + d;
    }
    if (digits == 0) {
        return 0;
    }
    /* A ZERO WITH A POINT (`0.0`, `-0.0`) stays the double it was, which is
     * what keeps a negative zero: the exact integer 0 has no sign. */
    if (point && m == 0) {
        return 0;
    }
    /* NORMALISED TO SCALE 1 AT LEAST, never to 0: a number written with a
     * point is a decimal and stays one, so `3.0` is exact without becoming
     * the exact INTEGER 3. That keeps every path that asks about integers --
     * integer overflow warnings, a typed integer column -- answering for
     * `1.0` what it always did, since `1.0` is how code has said "a double"
     * (numfmt_dump.bas and finance.bas both did). */
    while (s > 1 && m % 10ULL == 0) {
        m /= 10ULL;
        s--;
    }
    if (point && s == 0) {           /* `5.` */
        if (m > 9223372036854775807ULL / 10ULL) {
            return 0;
        }
        m *= 10ULL;
        s = 1;
    }
    if (s > 18) {
        return 0;
    }
    *mantissa = negative ? -(long long)m : (long long)m;
    *scale = s;
    return 1;
}

static inline double gb_decimal_to_double(long long mantissa, int scale) {
    static const double pow10[] = {
        1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10,
        1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
    };
    /* Both operands exact, so ONE IEEE division is correctly rounded. Past
     * 2^53 the mantissa is not a double, and strtod on the digits is. */
    if (scale >= 0 && scale <= 22 &&
        mantissa <= 9007199254740992LL && mantissa >= -9007199254740992LL) {
        return (double)mantissa / pow10[scale];
    }
    char buf[48];
    snprintf(buf, sizeof buf, "%llde-%d", mantissa, scale);
    return strtod(buf, NULL);
}

#endif

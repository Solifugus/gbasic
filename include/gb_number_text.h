#ifndef GB_NUMBER_TEXT_H
#define GB_NUMBER_TEXT_H

/* THE ONE READER OF "TEXT THAT IS A NUMBER", shared by G-SQL (storing text into
 * a numeric column, comparing text with a number) and gBASIC's own `=`
 * (docs/number_text_equality_design.md), so a program and a query cannot
 * disagree about what "7" is.
 *
 * Text is trimmed of spaces, tabs and line ends at both ends. For an INTEGER,
 * an optional sign and digits within 64 bits, so "2.5" and " 2.5" are refused
 * ("in most cases ... a mistake"); for a NUMBER, an optional sign, digits with
 * an optional fraction, and an optional exponent. Hex, inf, nan, digit
 * separators and a space INSIDE the number are refused either way. `t` is
 * trimmed in place; answers what is wrong, or NULL. STRICTER THAN CAST on
 * purpose: `{number}` also reads hex and inf. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static inline char *gb_number_text_trim(char *t) {
    while (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n') {
        t++;
    }
    size_t n = strlen(t);
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == '\n')) {
        t[--n] = '\0';
    }
    return t;
}

static inline const char *gb_number_text_problem(const char *t, int integer) {
    if (!t) {
        return "holds a NUL";
    }
    if (!*t) {
        return "is empty";
    }
    const char *c = t + (*t == '+' || *t == '-');
    if (!*c) {
        return "is a sign with no digits";
    }
    size_t digits = 0;
    while (*c >= '0' && *c <= '9') {
        c++;
        digits++;
    }
    if (*c == '.' || *c == ',') {
        if (integer || *c == ',') {
            return integer ? "is not a whole number" : "is not a number written plainly";
        }
        c++;
        while (*c >= '0' && *c <= '9') {
            c++;
            digits++;
        }
    }
    if (!digits) {
        return "is not a number written in digits";
    }
    if (!integer && (*c == 'e' || *c == 'E')) {
        c++;
        c += (*c == '+' || *c == '-');
        if (!(*c >= '0' && *c <= '9')) {
            return "is not a number written in digits";
        }
        while (*c >= '0' && *c <= '9') {
            c++;
        }
    }
    if (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n') {
        return "has a space inside the number";
    }
    if (*c) {
        return integer && (*c == 'e' || *c == 'E') ? "is not a whole number written in digits"
                                                   : "is not a number written in digits";
    }
    if (integer) {
        errno = 0;
        (void)strtoll(t, NULL, 10);
        if (errno == ERANGE) {
            return "is past the 64-bit integer range";
        }
    }
    return NULL;
}

#endif

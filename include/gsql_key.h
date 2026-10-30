#ifndef GBASIC_GSQL_KEY_H
#define GBASIC_GSQL_KEY_H

#include <stddef.h>

/* G-SQL's index key encoding (docs/gsql_storage_design.md §3.2): a value is
 * turned into bytes whose MEMCMP order is gBASIC's own order, so a B+tree (or
 * any of the Phase 0a candidates) can compare keys without knowing what is in
 * them.
 *
 * The input is ONE VALUE in `serialize` version 3's encoding with no header --
 * a tag byte and its payload -- which is what a G-SQL row holds for a column
 * (§3.3). So the encoder needs no interpreter and is tested against gBASIC by
 * handing it values gBASIC wrote.
 *
 * Every component is SELF-DELIMITING, so a composite key is the concatenation of
 * its components, and a descending component is the same bytes inverted. */

typedef struct {
    unsigned char *bytes;
    size_t length;
    size_t capacity;
} GsqlKey;

enum {
    /* The component sorts in reverse. */
    GSQL_KEY_DESC = 1,
    /* The column is declared with one currency (`MONEY(USD)`), so the key holds
     * the amount alone. Without it the key leads with the currency code, which
     * groups an untyped money column by currency. */
    GSQL_KEY_MONEY_TYPED = 2
};

typedef enum {
    GSQL_KEY_OK = 0,
    /* The bytes are not one well-formed version 3 value. */
    GSQL_KEY_CORRUPT,
    /* A kind with no index order: a duration (it orders by its total seconds and
     * is equal only field by field, which no single key order can agree with), a
     * duration carrying months, an array, a record, a path, a function, a regex,
     * an actor. */
    GSQL_KEY_NOT_INDEXABLE
} GsqlKeyStatus;

void gsql_key_init(GsqlKey *key);
void gsql_key_free(GsqlKey *key);

/* Append one component. `value` holds `length` bytes starting at a tag; on
 * success `*used` (if non-NULL) is how many of them the value occupied. On
 * failure nothing is appended. */
GsqlKeyStatus gsql_key_append(GsqlKey *key, const unsigned char *value,
                              size_t length, size_t *used, unsigned flags);

/* The length of the ONE component at the start of `bytes`, written with
 * `flags` (only GSQL_KEY_DESC matters: a descending component is read
 * inverted). How a non-unique index entry, which is its columns followed by
 * the row key, gives the row key back (docs/gsql_table_design.md §6).
 * GSQL_KEY_CORRUPT if the bytes are not one well-formed component. */
GsqlKeyStatus gsql_key_skip(const unsigned char *bytes, size_t length, unsigned flags, size_t *used);

#endif

#ifndef GBASIC_GSQL_TABLE_H
#define GBASIC_GSQL_TABLE_H

/* G-SQL's table layer (docs/gsql_table_design.md): a catalog, typed rows,
 * primary keys, unique and secondary indexes, NOT NULL and defaults, over the
 * storage interface (gsql_store.h).
 *
 * IT WORKS ON `serialize` VERSION 3 BYTES, NEVER ON INTERPRETER VALUES. A
 * value handed in or out is ONE v3 value with no header -- a tag byte and its
 * payload -- which is what a row holds for a column and what the key encoder
 * reads (gsql_key.h). Everything this layer decides can be decided from those
 * bytes, so it compiles alone and is tested with bytes gBASIC wrote.
 *
 * Every function takes the engine and an open transaction: a write
 * transaction for anything that changes the store, any for a read. A refused
 * write changes nothing, and the transaction stays usable. A schema change
 * (an index, a column) updates the handle it was made through; another handle
 * on the same table, or one held across an abort, must be reopened. */

#include <stddef.h>
#include <stdint.h>

#include "gsql_store.h"

typedef enum {
    GSQL_OK = 0,
    GSQL_NOT_FOUND,  /* no such table, row or index */
    GSQL_EXISTS,     /* a table or index of that name */
    GSQL_DUPLICATE,  /* a primary key or unique index already holds the value */
    GSQL_NOT_NULL,   /* unknown in a NOT NULL column */
    GSQL_TYPE,       /* the column's kind refuses the value */
    GSQL_SCHEMA,     /* a declaration refused */
    GSQL_CORRUPT,    /* stored bytes that do not parse or do not fit */
    GSQL_STORAGE,    /* the engine refused; the message names its status */
    GSQL_MISUSE      /* the caller broke the interface */
} GsqlCode;

const char *gsql_code_name(int code);

typedef struct {
    int code;
    char message[512];
} GsqlError;

typedef enum {
    GSQL_COL_NUMBER = 1,
    GSQL_COL_INTEGER,
    GSQL_COL_STRING,
    GSQL_COL_BOOLEAN,
    GSQL_COL_MONEY,
    GSQL_COL_DATE,
    GSQL_COL_TIME,
    GSQL_COL_DATETIME,
    GSQL_COL_RECORD
} GsqlColKind;

const char *gsql_kind_name(int kind);

/* A column. In a declaration the pointers are the caller's; in a loaded table
 * they are the table's. `def` is one v3 value (NULL: no default). */
typedef struct {
    const char *name;
    GsqlColKind kind;
    char currency[8];          /* MONEY(CUR): the alphabetic code; "" otherwise */
    int not_null;
    const unsigned char *def;
    size_t def_len;
} GsqlColumn;

typedef struct {
    const char *name;
    size_t ncols;
    const GsqlColumn *cols;
    size_t nkey;               /* 0: no primary key, a hidden row id */
    const unsigned *key;       /* column positions */
} GsqlTableDecl;

typedef struct {
    const char *name;
    size_t ncols;
    const unsigned *cols;      /* column positions */
    const int *desc;           /* one per column, NULL for all ascending */
    int unique;
} GsqlIndexDecl;

/* An index as the table knows it. */
typedef struct {
    const char *name;
    uint64_t id;
    size_t ncols;
    const unsigned *cols;
    const int *desc;
    int unique;
} GsqlIndex;

typedef struct GsqlTable GsqlTable;

/* Bytes the caller owns: free with gsql_buf_free. */
typedef struct {
    unsigned char *bytes;
    size_t length;
    size_t capacity;
} GsqlBuf;

void gsql_buf_free(GsqlBuf *b);

/* A column's value on the way in. `present` 0 means OMITTED: an insert uses
 * the column's default (unknown where there is none), an update keeps the
 * old value. An explicit unknown or nothing is present, and does NOT invoke
 * the default (gsql_design.md §3.1). */
typedef struct {
    int present;
    const unsigned char *p;
    size_t n;
} GsqlIn;

/* ---- tables ---------------------------------------------------------- */

int gsql_table_create(const GsEngine *e, GsTxn *t, const GsqlTableDecl *d, GsqlError *err);
int gsql_table_drop(const GsEngine *e, GsTxn *t, const char *name, GsqlError *err);
int gsql_table_open(const GsEngine *e, GsTxn *t, const char *name, GsqlTable **out, GsqlError *err);
void gsql_table_close(GsqlTable *tb);
/* Every table name in order; stops when `each` answers nonzero. */
int gsql_table_list(const GsEngine *e, GsTxn *t, int (*each)(void *ctx, const char *name), void *ctx,
                    GsqlError *err);

const char *gsql_table_name(const GsqlTable *tb);
uint64_t gsql_table_id(const GsqlTable *tb);
/* Incremented by every schema change: what a prepared plan is invalidated by. */
uint64_t gsql_table_version(const GsqlTable *tb);
size_t gsql_table_ncols(const GsqlTable *tb);
const GsqlColumn *gsql_table_col(const GsqlTable *tb, size_t i);
/* Position of a column by name, or -1. */
long gsql_table_find(const GsqlTable *tb, const char *name);
size_t gsql_table_nkey(const GsqlTable *tb);
const unsigned *gsql_table_key_cols(const GsqlTable *tb);
size_t gsql_table_nindexes(const GsqlTable *tb);
const GsqlIndex *gsql_table_index(const GsqlTable *tb, size_t i);

/* The trees, for a caller scanning with the engine's cursors. Each is 10
 * bytes: a NUL, a letter, and a big-endian id. */
GsBytes gsql_table_rows_tree(const GsqlTable *tb);
GsBytes gsql_index_tree(const GsqlTable *tb, size_t index);

/* ---- schema changes --------------------------------------------------- */

int gsql_index_create(const GsEngine *e, GsTxn *t, GsqlTable *tb, const GsqlIndexDecl *d, GsqlError *err);
int gsql_index_drop(const GsEngine *e, GsTxn *t, GsqlTable *tb, const char *name, GsqlError *err);
/* ADD COLUMN: rows already written are not touched, and read the column as
 * its default. A NOT NULL column needs a default. */
int gsql_table_add_column(const GsEngine *e, GsTxn *t, GsqlTable *tb, const GsqlColumn *c, GsqlError *err);

/* ---- rows ------------------------------------------------------------- */

/* `vals` holds one entry per column. *rowkey (may be NULL) gets the new row's
 * key. */
int gsql_row_insert(const GsEngine *e, GsTxn *t, GsqlTable *tb, const GsqlIn *vals, GsqlBuf *rowkey,
                    GsqlError *err);
/* Replaces the row at `rowkey`; an omitted column keeps its value. A change
 * to the primary key moves the row, and *newkey (may be NULL) gets where. */
int gsql_row_update(const GsEngine *e, GsTxn *t, GsqlTable *tb, GsBytes rowkey, const GsqlIn *vals,
                    GsqlBuf *newkey, GsqlError *err);
int gsql_row_delete(const GsEngine *e, GsTxn *t, GsqlTable *tb, GsBytes rowkey, GsqlError *err);
int gsql_row_get(const GsEngine *e, GsTxn *t, const GsqlTable *tb, GsBytes rowkey, GsqlBuf *row,
                 GsqlError *err);

/* The row key a primary-key value has: `vals` holds one value per key
 * column, in key order. GSQL_MISUSE for a table with no primary key. */
int gsql_row_key(const GsqlTable *tb, const GsqlIn *vals, GsqlBuf *rowkey, GsqlError *err);
/* One column of a stored row, borrowed from `row` (or from the schema, for a
 * column the row predates). */
int gsql_row_value(const GsqlTable *tb, const unsigned char *row, size_t n, size_t col, GsBytes *out,
                   GsqlError *err);
/* The row key an index entry points at. */
int gsql_index_rowkey(const GsqlTable *tb, size_t index, GsBytes entry_key, GsBytes entry_value,
                      GsBytes *rowkey, GsqlError *err);

/* Whether `value` (one v3 value) fits column `col`, as an insert would ask:
 * GSQL_OK, GSQL_TYPE or GSQL_NOT_NULL. */
int gsql_value_fits(const GsqlColumn *c, const unsigned char *value, size_t n, GsqlError *err);

/* ---- the invariant check (gsql_table_design.md §8) --------------------- */

/* Writes one line per problem to `report` (may be NULL) and answers
 * GSQL_CORRUPT if there was any. */
int gsql_table_check(const GsEngine *e, GsTxn *t, void (*report)(void *ctx, const char *line), void *ctx,
                     GsqlError *err);

#endif

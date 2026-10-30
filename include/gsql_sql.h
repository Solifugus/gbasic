#ifndef GBASIC_GSQL_SQL_H_PUBLIC
#define GBASIC_GSQL_SQL_H_PUBLIC

/* G-SQL's SQL layer (docs/gsql_sql_design.md): the owner's service, built over
 * a HOST that evaluates expressions.
 *
 * The SQL layer parses, binds, plans and runs statements itself, and never
 * decides what an operator means: `=`, `<`, `+`, `and` and the rest are the
 * host's (§2). The product's host is the interpreter (src/modules/gsql.c), so
 * SQL and gBASIC cannot disagree about NULL, exactness, money or dates.
 *
 * A host value is opaque. Every function that answers one hands the caller a
 * value it owns and must release; every argument is borrowed. A function that
 * fails answers NULL (or nonzero) with `err` filled. */

#include <stddef.h>

#include "gsql_owner.h"
#include "gsql_table.h"

typedef struct GsqlHv GsqlHv;

/* The operators a host applies. The numbering is the SQL layer's own. */
enum {
    GSQL_HOP_OR = 1, GSQL_HOP_AND,
    GSQL_HOP_EQ, GSQL_HOP_NE, GSQL_HOP_LT, GSQL_HOP_LE, GSQL_HOP_GT, GSQL_HOP_GE,
    GSQL_HOP_CONCAT_UNUSED,   /* || is the SQL layer's own (§2) */
    GSQL_HOP_ADD, GSQL_HOP_SUB, GSQL_HOP_MUL, GSQL_HOP_DIV, GSQL_HOP_MOD,
    GSQL_HOP_NOT, GSQL_HOP_NEG
};

typedef struct {
    void *ctx;
    /* one v3 value in, a host value out; and back */
    GsqlHv *(*load)(void *ctx, const unsigned char *v, size_t n, GsqlError *err);
    int (*store)(void *ctx, const GsqlHv *v, GsqlBuf *out, GsqlError *err);
    /* the value's kind, as a `serialize` tag (include/serialize_tags.h) */
    int (*kind)(void *ctx, const GsqlHv *v);
    /* 1 true, 0 false, -1 unknown, -2 not a boolean */
    int (*truth)(void *ctx, const GsqlHv *v);
    /* a binary operator (b is NULL for NOT and NEG), as gBASIC applies it */
    GsqlHv *(*op)(void *ctx, int op, const GsqlHv *a, const GsqlHv *b, GsqlError *err);
    /* a record's field; absent is unknown, as a missing column is */
    GsqlHv *(*field)(void *ctx, const GsqlHv *record, const char *name, GsqlError *err);
    /* a gBASIC builtin by name: lower, upper, len, abs, round, trim */
    GsqlHv *(*call)(void *ctx, const char *name, GsqlHv *const *args, size_t n, GsqlError *err);
    /* CAST(v AS kind), kind a GsqlColKind; currency for MONEY(CUR) */
    GsqlHv *(*cast)(void *ctx, const GsqlHv *v, int kind, const char *currency, GsqlError *err);
    /* gBASIC's sort order of two values neither of which is unknown */
    int (*order)(void *ctx, const GsqlHv *a, const GsqlHv *b, int *cmp, GsqlError *err);
    GsqlHv *(*copy)(void *ctx, const GsqlHv *v);
    void (*release)(void *ctx, GsqlHv *v);
} GsqlHost;

/* The service the owner is given (gsql_owner.h). `host` must outlive it. */
typedef struct GsqlSql GsqlSql;
GsqlSql *gsql_sql_new(const GsqlHost *host);
void gsql_sql_free(GsqlSql *q);
GsqlService gsql_sql_service(GsqlSql *q);

/* The interpreter's host (src/modules/gsql.c), in libgbasic. */
const GsqlHost *gb_gsql_host(void);

/* For tests: answer every SELECT, UPDATE and DELETE by scanning the whole
 * table, so an access path can be compared with the scan it narrows. */
void gsql_sql_force_scan(GsqlSql *q, int on);
/* For tests: how many statements used a seek or a point read, and a scan. */
void gsql_sql_counts(const GsqlSql *q, unsigned long *seeks, unsigned long *scans);

#endif

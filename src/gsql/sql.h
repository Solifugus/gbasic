#ifndef GBASIC_GSQL_SQL_H
#define GBASIC_GSQL_SQL_H

/* G-SQL's SQL front end (docs/gsql_sql_design.md), internal to src/gsql: the
 * statement tree the parser builds (sql_parse.c), the plan the binder makes of
 * it (sql_plan.c), and what the executor needs of the host that evaluates
 * expressions (sql_exec.c, include/gsql_sql.h).
 *
 * A statement and everything hanging from it lives in ONE arena, freed at
 * once, so no node is ever freed on its own and a parse that fails half way
 * leaks nothing. */

#include <stddef.h>
#include <stdint.h>

#include "gsql_table.h"

/* ---- the arena ------------------------------------------------------- */

typedef struct SqlArenaBlock SqlArenaBlock;
typedef struct {
    SqlArenaBlock *head;
} SqlArena;

void *sql_alloc(SqlArena *a, size_t n);          /* zeroed; abort() on exhaustion */
char *sql_strndup(SqlArena *a, const char *s, size_t n);
void sql_arena_free(SqlArena *a);

/* ---- expressions ----------------------------------------------------- */

typedef enum {
    SX_LIT = 1,   /* a constant: `lit` holds one v3 value */
    SX_PARAM,     /* :name (name) or $n (index, 1-based) */
    SX_NAME,      /* a dotted name: column, table.column, column.field ... */
    SX_BIN,       /* op: one of the SQL_OP_* below */
    SX_NOT,       /* a[0] */
    SX_NEG,       /* a[0] */
    SX_ISNULL,    /* a[0]; neg */
    SX_IN,        /* a[0] IN (a[1..n-1]); neg */
    SX_BETWEEN,   /* a[0] BETWEEN a[1] AND a[2]; neg */
    SX_LIKE,      /* a[0] LIKE a[1]; neg */
    SX_CASE,      /* a: WHEN/THEN pairs, then ELSE (or NULL); `operand` for the simple form */
    SX_CALL,      /* name(a...) */
    SX_CAST,      /* CAST(a[0] AS type) and DATE '...' */
    /* bound forms, made by the planner from SX_NAME */
    SX_COL,       /* column `col` of the statement's table, then `fields` */
} SqlExprKind;

typedef enum {
    SQL_OP_OR = 1, SQL_OP_AND,
    SQL_OP_EQ, SQL_OP_NE, SQL_OP_LT, SQL_OP_LE, SQL_OP_GT, SQL_OP_GE,
    SQL_OP_CONCAT,
    SQL_OP_ADD, SQL_OP_SUB, SQL_OP_MUL, SQL_OP_DIV, SQL_OP_MOD
} SqlOp;

const char *sql_op_text(int op);     /* "=", "<>", "AND", "||" ... as SQL spells it */

/* A column type as written: the table layer's kind plus a currency. */
typedef struct {
    GsqlColKind kind;
    char currency[8];
} SqlType;

typedef struct SqlExpr SqlExpr;
struct SqlExpr {
    SqlExprKind kind;
    uint32_t pos;            /* 1-based character offset into the text */
    int op;                  /* SX_BIN */
    int neg;                 /* NOT IN, NOT BETWEEN, NOT LIKE, IS NOT NULL */
    /* SX_LIT */
    unsigned char *lit;
    size_t litlen;
    /* SX_PARAM, SX_CALL */
    char *name;
    unsigned index;
    /* SX_NAME: the parts as written (folded unless quoted) */
    char **parts;
    size_t nparts;
    /* SX_COL */
    unsigned col;
    char **fields;
    size_t nfields;
    /* children */
    SqlExpr **a;
    size_t na;
    SqlExpr *operand;        /* SX_CASE's simple form */
    int has_else;            /* SX_CASE */
    SqlType type;            /* SX_CAST */
    /* the executor's: a constant's or a parameter's host value, loaded once
     * per execution and released with it */
    void *hv;
};

/* ---- statements ------------------------------------------------------ */

typedef enum {
    SQ_SELECT = 1,
    SQ_INSERT,
    SQ_UPDATE,
    SQ_DELETE,
    SQ_CREATE_TABLE,
    SQ_DROP_TABLE,
    SQ_ADD_COLUMN,
    SQ_CREATE_INDEX,
    SQ_DROP_INDEX
} SqlStmtKind;

typedef struct {
    SqlExpr *e;              /* NULL for * or t.* */
    char *star_table;        /* t.* : the qualifier; NULL for a bare * */
    char *alias;
    uint32_t pos;
} SqlItem;

typedef struct {
    SqlExpr *e;
    int desc;
    uint32_t pos;
} SqlOrder;

typedef struct {
    char *name;
    uint32_t pos;
    SqlType type;
    int not_null;
    int primary;             /* PRIMARY KEY on the column */
    int unique;
    SqlExpr *def;            /* DEFAULT, or NULL */
} SqlColDef;

typedef struct {
    char **cols;
    int *desc;
    size_t n;
    uint32_t pos;
} SqlNameList;

typedef struct {
    SqlStmtKind kind;
    int explain;
    uint32_t pos;
    char *table;             /* every statement but SELECT with no FROM */
    char *alias;             /* SELECT/UPDATE/DELETE: FROM t [AS] a */
    int if_exists;           /* DROP ... IF EXISTS, CREATE ... IF NOT EXISTS */
    /* SELECT */
    SqlItem *items;
    size_t nitems;
    SqlExpr *where;          /* SELECT, UPDATE, DELETE */
    SqlOrder *order;
    size_t norder;
    SqlExpr *limit, *offset;
    /* INSERT: cols may be empty (every column, in order) */
    SqlNameList cols;
    SqlExpr ***rows;         /* rows[r][c] */
    size_t nrows, rowlen;
    /* UPDATE: SET cols.cols[i] = sets[i] */
    SqlExpr **sets;
    /* CREATE TABLE / ADD COLUMN */
    SqlColDef *coldefs;
    size_t ncoldefs;
    SqlNameList primary;     /* table-level PRIMARY KEY (...) */
    SqlNameList *uniques;    /* table-level UNIQUE (...) */
    size_t nuniques;
    /* CREATE INDEX / DROP INDEX */
    char *index;
    int unique;
    SqlNameList index_cols;
    /* parameters, in order of first appearance */
    char **param_names;      /* :name parameters */
    size_t nparam_names;
    unsigned max_param_index;/* $n parameters: the largest n */
} SqlStmt;

typedef struct {
    SqlArena arena;
    SqlStmt *stmt;
} SqlParsed;

/* Parses ONE statement (a trailing `;` is allowed). On failure answers
 * GSQL_SYNTAX with err->position set; *out is then empty and needs no free. */
int sql_parse(const char *text, size_t length, SqlParsed *out, GsqlError *err);
void sql_parsed_free(SqlParsed *p);

/* The canonical form of a statement, for the parse tier and for EXPLAIN's
 * filter lines: every name folded, every operator spelled one way, every
 * subexpression parenthesised where a reader could misgroup it. */
void sql_print_stmt(GsqlBuf *out, const SqlStmt *s);
void sql_print_expr(GsqlBuf *out, const SqlExpr *e);
/* as sql_print_expr, naming a bound column by its table's name for it */
void sql_print_expr_tb(GsqlBuf *out, const SqlExpr *e, const GsqlTable *tb);

const char *sql_type_name(const SqlType *t, char *buf, size_t cap);

/* ---- the plan (sql_plan.c) ------------------------------------------- */

typedef enum {
    ACC_NONE = 0,    /* SELECT with no FROM: one row, no table */
    ACC_SCAN,        /* the rows tree, whole */
    ACC_PK,          /* a range of the rows tree: the primary key's columns */
    ACC_INDEX        /* a range of one index, then each row by its key */
} SqlAccess;

typedef struct {
    SqlStmt *s;
    const char *text;        /* the statement's text, for messages */
    GsqlTable *tb;           /* owned; NULL for ACC_NONE and schema statements */
    SqlAccess access;
    size_t index;            /* ACC_INDEX */
    size_t nkeycols;         /* the key's columns: the PK's or the index's */
    const unsigned *keycols;
    const int *keydesc;      /* NULL: all ascending */
    size_t neq;              /* equality on keycols[0..neq) */
    SqlExpr **eq;
    SqlExpr *lo, *hi;        /* a range on keycols[neq], either may be NULL */
    int point;               /* equality on every column of a unique key */
    int backward;            /* walk the key range from its end */
    int ordered;             /* the access yields ORDER BY's order: no sort */
    SqlExpr **conj;          /* WHERE's conjuncts */
    size_t nconj;
    int *consumed;           /* conj[i] narrowed the access path */
    /* SELECT's output */
    size_t nout;
    SqlExpr **out;
    const char **names;
    const char **kinds;      /* "integer", "money(USD)" ... or "any" */
    /* ORDER BY, bound: an output position (>= 0) or an expression */
    SqlExpr **order;
    long *order_out;
    /* UPDATE: column positions, and INSERT's: the column each value goes to */
    unsigned *targets;
    size_t ntargets;
} SqlPlan;

/* Binds `s` against the catalog and chooses an access path. `force_scan`
 * ignores every path but the scan. The plan borrows `s`; plan_free releases
 * the table it opened. */
int sql_plan(const GsEngine *e, GsTxn *t, SqlStmt *s, SqlArena *arena, const char *text, int force_scan,
             SqlPlan *out, GsqlError *err);
void sql_plan_free(SqlPlan *p);

/* EXPLAIN's lines, one per call to `line`. */
void sql_explain(const SqlPlan *p, void (*line)(void *ctx, const char *text), void *ctx);

/* whether an expression reads no column */
int sql_is_constant(const SqlExpr *e);

#endif

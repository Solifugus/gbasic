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
    SX_AGG,       /* an aggregate, made from SX_CALL: `op` an SQL_AGG_*, `index` its slot */
    /* subqueries (docs/gsql_subquery_design.md) */
    SX_SUBQ,      /* `sub`, as `op`: SQL_SUB_*; IN's left side is a[0], NOT IN is neg */
    SX_OUTER,     /* bound: column `col` of scan `tab` of the query `index` levels out */
    /* window functions (docs/gsql_window_design.md) */
    SX_WIN,       /* made from SX_CALL with `over`: `op` an SQL_AGG_* or SQL_WIN_*, `index` its slot */
} SqlExprKind;

typedef enum { SQL_SUB_SCALAR = 1, SQL_SUB_IN, SQL_SUB_EXISTS } SqlSub;

typedef enum {
    SQL_AGG_COUNT_STAR = 1, SQL_AGG_COUNT, SQL_AGG_SUM, SQL_AGG_AVG, SQL_AGG_MIN, SQL_AGG_MAX
} SqlAgg;

typedef enum {
    SQL_OP_OR = 1, SQL_OP_AND,
    SQL_OP_EQ, SQL_OP_NE, SQL_OP_LT, SQL_OP_LE, SQL_OP_GT, SQL_OP_GE,
    SQL_OP_CONCAT,
    SQL_OP_ADD, SQL_OP_SUB, SQL_OP_MUL, SQL_OP_DIV, SQL_OP_MOD
} SqlOp;

/* the window functions that are not aggregates; numbered apart from SqlAgg */
typedef enum {
    SQL_WIN_ROW_NUMBER = 100, SQL_WIN_RANK, SQL_WIN_DENSE_RANK, SQL_WIN_PERCENT_RANK, SQL_WIN_CUME_DIST,
    SQL_WIN_NTILE, SQL_WIN_LAG, SQL_WIN_LEAD, SQL_WIN_FIRST_VALUE, SQL_WIN_LAST_VALUE, SQL_WIN_NTH_VALUE
} SqlWinFn;

/* a window frame's bound */
typedef enum {
    SQL_BOUND_UNBOUNDED_PRECEDING = 1, SQL_BOUND_PRECEDING, SQL_BOUND_CURRENT, SQL_BOUND_FOLLOWING,
    SQL_BOUND_UNBOUNDED_FOLLOWING
} SqlBound;

/* a frame's unit: none written (RANGE UNBOUNDED PRECEDING .. CURRENT ROW), ROWS or RANGE */
typedef enum { SQL_FRAME_DEFAULT = 0, SQL_FRAME_ROWS, SQL_FRAME_RANGE } SqlFrame;

const char *sql_op_text(int op);     /* "=", "<>", "AND", "||" ... as SQL spells it */

/* A column type as written: the table layer's kind plus a currency. */
typedef struct {
    GsqlColKind kind;
    char currency[8];
} SqlType;

typedef struct SqlExpr SqlExpr;
typedef struct SqlWindow SqlWindow;
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
    /* SX_COL: column `col` of scan `tab` (a table of FROM, in order) */
    unsigned tab;
    unsigned col;
    char **fields;
    size_t nfields;
    /* children */
    SqlExpr **a;
    size_t na;
    SqlExpr *operand;        /* SX_CASE's simple form */
    int has_else;            /* SX_CASE */
    int star;                /* SX_CALL/SX_AGG: count(*) */
    int distinct;            /* SX_CALL/SX_AGG: count(DISTINCT e) */
    SqlType type;            /* SX_CAST */
    /* SX_SUBQ: the SELECT; the planner's plan of it, whether it reads any
     * enclosing query's row, and the columns of the query immediately
     * enclosing it that it reads (each an SX_COL bound there); its number
     * for EXPLAIN */
    struct SqlStmt *sub;
    struct SqlPlan *subplan;
    int correlated;
    SqlExpr **refs;
    size_t nrefs;
    unsigned subno;
    /* SX_OUTER: the scan's qualifier and table, for printing */
    const char *oqual;
    const struct SqlScan *oscan;
    /* SX_SUBQ IN, uncorrelated: the subquery's values, gathered once per
     * execution (the executor's, as hv) */
    void **set;
    size_t nset;
    int gathered;
    /* the executor's: a constant's or a parameter's host value, loaded once
     * per execution and released with it */
    void *hv;
    /* SX_CALL/SX_WIN: the OVER clause */
    SqlWindow *over;
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
    SQ_DROP_INDEX,
    SQ_RENAME_TABLE,         /* ALTER TABLE table RENAME TO rename_to */
    SQ_CREATE_VIEW,          /* CREATE [OR REPLACE] VIEW table [(view_cols)] AS view_q */
    SQ_DROP_VIEW,            /* DROP VIEW [IF EXISTS] table */
    SQ_ADD_FKEY,             /* ALTER TABLE table ADD fkeys[0] */
    SQ_DROP_CONSTRAINT,      /* ALTER TABLE table DROP CONSTRAINT constraint */
    SQ_ADD_CHECK,            /* ALTER TABLE table ADD checks[0] */
    SQ_BACKUP,               /* BACKUP TO backup_to (docs/gsql_backup_design.md) */
    SQ_DUMP,                 /* DUMP TO dump_to [SCHEMA ONLY] (docs/gsql_dump_design.md) */
    SQ_RESTART               /* ALTER TABLE table ALTER COLUMN restart_col RESTART WITH restart_with
                                [FOR (restart_for...)] (docs/gsql_dump_design.md §4) */
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

/* OVER ( [PARTITION BY ...] [ORDER BY ...] [frame] ) (docs/gsql_window_design.md) */
struct SqlWindow {
    uint32_t pos;
    SqlExpr **part;
    size_t npart;
    SqlOrder *order;
    size_t norder;
    int frame;               /* SQL_FRAME_* */
    uint32_t frame_pos;
    int start, end;          /* SQL_BOUND_*; a start alone ends at CURRENT ROW */
    SqlExpr *start_n, *end_n;/* n of n PRECEDING / n FOLLOWING */
    unsigned ordering;       /* the planner's: the ordering it shares with windows like it */
};

/* A table joined after the first (docs/gsql_join_design.md) */
typedef enum { SQL_JOIN_FIRST = 0, SQL_JOIN_INNER, SQL_JOIN_LEFT, SQL_JOIN_CROSS } SqlJoin;

/* UNION, INTERSECT, EXCEPT (docs/gsql_setop_design.md) */
typedef enum { SQL_SET_NONE = 0, SQL_SET_UNION, SQL_SET_INTERSECT, SQL_SET_EXCEPT } SqlSetOp;

typedef struct {
    char **cols;
    int *desc;
    size_t n;
    uint32_t pos;
} SqlNameList;

/* A table joined after the first; `sub` a derived table (docs/gsql_derived_design.md),
 * which then has `alias`, and `cols` renames a derived table's or WITH
 * query's columns */
typedef struct {
    SqlJoin join;
    char *table;
    char *alias;
    SqlExpr *on;             /* NULL for CROSS JOIN and the comma */
    uint32_t pos;
    struct SqlStmt *sub;
    SqlNameList cols;
} SqlFrom;

/* A foreign key (docs/gsql_fkey_design.md): name may be NULL, ref_cols empty
 * for the referenced table's primary key */
typedef struct {
    char *name;
    SqlNameList cols;
    char *ref;
    SqlNameList ref_cols;
    int not_enforced;
    uint32_t pos;
} SqlFkey;

/* A CHECK constraint (docs/gsql_check_design.md): `text` is the expression's
 * canonical print, made before anything binds it; `column` the column it was
 * written on, or NULL for a table's */
typedef struct {
    char *name;
    char *column;
    SqlExpr *e;
    char *text;
    uint32_t pos;
} SqlCheck;

/* WITH [RECURSIVE] name [(cols)] AS (q) [CYCLE c, ... SET mark USING route]
 * (docs/gsql_recursive_design.md) */
typedef struct {
    char *name;
    SqlNameList cols;
    struct SqlStmt *q;
    uint32_t pos;
    int recursive;           /* written in a WITH RECURSIVE list */
    char **cycle;            /* CYCLE's columns */
    size_t ncycle;
    char *cycle_mark, *cycle_route;
    uint32_t cycle_pos;
} SqlCte;

typedef struct {
    char *name;
    uint32_t pos;
    SqlType type;
    int not_null;
    int primary;             /* PRIMARY KEY on the column */
    int unique;
    SqlExpr *def;            /* DEFAULT, or NULL */
    SqlFkey *fk;             /* REFERENCES, or NULL */
    SqlCheck *check;         /* CHECK, or NULL */
    /* SERIAL [OF (...)] or RANDOM [(n [, 'chars'])] (docs/gsql_generated_design.md) */
    int gen;                 /* GSQL_GEN_* */
    uint32_t gen_pos;
    SqlExpr **scope;
    char **scope_text;       /* each printed before planning binds it */
    size_t nscope;
    uint32_t length;         /* 0: the default */
    char *alphabet;          /* NULL: the default */
} SqlColDef;


typedef struct SqlStmt {
    SqlStmtKind kind;
    int explain;
    uint32_t pos;
    char *table;             /* every statement but SELECT with no FROM */
    char *alias;             /* SELECT/UPDATE/DELETE: FROM t [AS] a */
    struct SqlStmt *table_sub; /* SELECT: the first table, when it is a derived table */
    SqlNameList table_cols;  /* and its columns renamed */
    SqlCte *ctes;            /* SELECT: its WITH queries, in order */
    size_t nctes;
    SqlFrom *joins;          /* SELECT: the tables after the first */
    size_t njoins;
    int if_exists;           /* DROP ... IF EXISTS, CREATE ... IF NOT EXISTS */
    /* SELECT */
    SqlItem *items;
    size_t nitems;
    SqlExpr *where;          /* SELECT, UPDATE, DELETE */
    SqlOrder *order;
    size_t norder;
    SqlExpr *limit, *offset;
    SqlExpr **group;         /* GROUP BY */
    size_t ngroup;
    SqlExpr *having;
    int distinct;            /* SELECT DISTINCT */
    /* a compound SELECT: `left op right`, each a SELECT; this statement then
     * holds only the ORDER BY, LIMIT and OFFSET, which apply to the result */
    SqlSetOp setop;
    int set_all;
    struct SqlStmt *left, *right;
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
    char *rename_to;         /* ALTER TABLE ... RENAME TO */
    SqlExpr *backup_to;      /* BACKUP TO: the copy's directory, a constant */
    SqlExpr *dump_to;        /* DUMP TO: the file, a constant */
    int dump_schema_only;
    char *restart_col;       /* RESTART: the serial column, its new high-water mark and scope */
    SqlExpr *restart_with;
    SqlExpr **restart_for;
    size_t nrestart_for;
    /* CREATE VIEW (docs/gsql_view_design.md) */
    struct SqlStmt *view_q;
    struct SqlStmt *ins_q;   /* INSERT ... SELECT (docs/gsql_insert_select_design.md) */
    char *view_text;         /* its canonical text, printed before planning binds it */
    SqlNameList view_cols;
    int or_replace;
    /* CREATE TABLE's foreign keys, column and table ones alike; ALTER TABLE
       ADD's one; DROP CONSTRAINT's name */
    SqlFkey *fkeys;
    size_t nfkeys;
    SqlCheck *checks;        /* CREATE TABLE's, column and table ones alike; ALTER TABLE ADD's one */
    size_t nchecks;
    char *constraint;
    int unique;
    SqlNameList index_cols;
    /* parameters, in order of first appearance */
    char **param_names;      /* :name parameters */
    size_t nparam_names;
    unsigned max_param_index;/* $n parameters: the largest n */
} SqlStmt;

typedef struct SqlParsed {
    SqlArena arena;
    SqlStmt *stmt;
} SqlParsed;

/* Parses ONE statement (a trailing `;` is allowed). On failure answers
 * GSQL_SYNTAX with err->position set; *out is then empty and needs no free. */
int sql_parse(const char *text, size_t length, SqlParsed *out, GsqlError *err);
void sql_parsed_free(SqlParsed *p);
/* Where one statement ends in a stream of them: 1 past its `;`, 0 the text
 * ended first (*empty: it held no token), -1 inside a string or comment. */
int sql_statement_end(const char *text, size_t length, size_t *end, int *empty);

/* The canonical form of a statement, for the parse tier and for EXPLAIN's
 * filter lines: every name folded, every operator spelled one way, every
 * subexpression parenthesised where a reader could misgroup it. */
void sql_print_stmt(GsqlBuf *out, const SqlStmt *s);
void sql_print_expr(GsqlBuf *out, const SqlExpr *e);
/* a name as SQL reads it back: quoted where the bare word would read otherwise */
void sql_print_name(GsqlBuf *out, const char *name);
/* One v3 value as a SQL literal reading back as the same value; 0 for a
 * value with none (docs/gsql_dump_design.md §3). */
int sql_print_value(GsqlBuf *out, const unsigned char *v, size_t n);
/* as sql_print_expr, naming a bound column by its table's name for it */
void sql_print_expr_tb(GsqlBuf *out, const SqlExpr *e, const GsqlTable *tb);

const char *sql_type_name(const SqlType *t, char *buf, size_t cap);

/* ---- the plan (sql_plan.c) ------------------------------------------- */

typedef enum {
    ACC_NONE = 0,    /* SELECT with no FROM: one row, no table */
    ACC_SCAN,        /* the rows tree, whole */
    ACC_PK,          /* a range of the rows tree: the primary key's columns */
    ACC_INDEX,       /* a range of one index, then each row by its key */
    ACC_DERIVED      /* a derived table or WITH query: its plan's rows, materialized */
} SqlAccess;

/* One table of a statement and how it is read: its access path, and the
 * conditions checked once a row of it is in hand. A single-table statement has
 * one scan; a join has one per table of FROM, read by nested loops in order
 * (docs/gsql_join_design.md §3). */
typedef struct SqlScan {
    GsqlTable *tb;           /* scan 0's is the plan's tb; the others are owned here */
    const char *qualifier;   /* the alias, or the table's name */
    SqlJoin join;
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
    SqlExpr **conj;          /* the conditions checked here: this join's ON, then WHERE's */
    size_t nconj;
    size_t non;              /* conj[0..non) are ON's, which decide a LEFT join's match */
    int *consumed;           /* conj[i] narrowed the access path */
    /* ACC_DERIVED: the plan whose output rows are this scan's rows, owned
       here unless the scan reads a WITH query; its columns' names and kinds */
    struct SqlPlan *derived;
    const char *cte;         /* the WITH query read, or NULL */
    const char *view;        /* the view read, or NULL: its parse is owned here */
    struct SqlParsed *vparse;
    char *vtext;
    size_t ndcols;
    const char **dnames;
    const char **dkinds;
    /* a hash join's inner side (docs/gsql_hashjoin_design.md): read whole
       once, grouped by column `hashcol`'s key, and probed with `hashexpr`,
       which reads only earlier scans */
    int hash;
    unsigned hashcol;
    SqlExpr *hashexpr;
    /* the engine's estimate of the table's rows (docs/gsql_rowcount_design.md),
       when it gave one */
    uint64_t est;
    int has_est;
    /* a recursive WITH query's step reading the query itself
       (docs/gsql_recursive_design.md §5): the round before's rows, set by the
       executor for each round */
    int self;
    const GsqlBuf *work;
    size_t nwork;
} SqlScan;

typedef struct SqlPlan {
    SqlStmt *s;
    const char *text;        /* the statement's text, for messages */
    /* a compound SELECT (docs/gsql_setop_design.md): its two sides, each a
     * plan of its own, owned; the outputs below are the result's */
    struct SqlPlan *left, *right;
    /* the subqueries in this query's expressions, in the order bound; each
     * node's subplan is owned here */
    SqlExpr **subs;
    size_t nsubs;
    /* the WITH queries this query defines, each planned once, owned here */
    struct SqlPlan **ctes;
    size_t nctes;
    GsqlTable *tb;           /* the first table, owned; NULL for ACC_NONE and schema statements */
    SqlScan *scans;
    size_t nscans;           /* 0 with no table */
    /* the order the scans are read in (docs/gsql_rowcount_design.md §3):
       exec[level] is a scan, rank[scan] its level; NULL is FROM's order */
    size_t *exec, *rank;
    /* SELECT's output */
    size_t nout;
    SqlExpr **out;
    const char **names;
    const char **kinds;      /* "integer", "money(USD)" ... or "any" */
    /* ORDER BY, bound: an output position (>= 0) or an expression */
    SqlExpr **order;
    long *order_out;
    /* aggregation (docs/gsql_aggregate_design.md): a grouped query is one with
     * GROUP BY, HAVING or an aggregate; `aggs` are its SX_AGG nodes by slot */
    int grouped;
    SqlExpr **aggs;
    size_t naggs;
    /* window functions (docs/gsql_window_design.md): the SX_WIN nodes by
       slot, and how many distinct orderings they share */
    SqlExpr **wins;
    size_t nwins;
    size_t norderings;
    /* UPDATE: column positions, and INSERT's: the column each value goes to */
    unsigned *targets;
    size_t ntargets;
    /* a recursive WITH query (docs/gsql_recursive_design.md): `left` its
       anchor, `right` its step, whose scan `rec_self` reads the query;
       UNION ALL or UNION; CYCLE's columns, by position, which add the mark
       and route columns after the query's own `rec_ncols` */
    int recursive;
    int rec_all;
    size_t rec_self;
    size_t rec_ncols;
    unsigned *cycle;
    size_t ncycle;
    const char *rec_name;
    uint32_t rec_pos;
} SqlPlan;

/* Binds `s` against the catalog and chooses an access path. `force_scan`
 * ignores every path but the scan. The plan borrows `s`; plan_free releases
 * the table it opened. */
int sql_plan(const GsEngine *e, GsTxn *t, SqlStmt *s, SqlArena *arena, const char *text, int force_scan,
             SqlPlan *out, GsqlError *err);
void sql_plan_free(SqlPlan *p);

/* the scan read at `level`, and the level a scan is read at */
static inline size_t sql_exec_scan(const SqlPlan *p, size_t level) {
    return p->exec ? p->exec[level] : level;
}
static inline size_t sql_scan_rank(const SqlPlan *p, size_t k) {
    return p->rank ? p->rank[k] : k;
}

/* EXPLAIN's lines, one per call to `line`. */
void sql_explain(const SqlPlan *p, void (*line)(void *ctx, const char *text), void *ctx);

/* whether an expression reads no column */
int sql_is_constant(const SqlExpr *e);

/* as sql_print_expr_tb, naming each column by its scan's table, and with its
 * table's alias or name in front where there are several */
void sql_print_expr_scans(GsqlBuf *out, const SqlExpr *e, const SqlScan *scans, size_t nscans);

/* a scan's columns, whether it reads a table or a derived table */
size_t sql_scan_ncols(const SqlScan *sc);
const char *sql_scan_col_name(const SqlScan *sc, size_t col);

#endif

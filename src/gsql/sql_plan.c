/* G-SQL's binder and planner (docs/gsql_sql_design.md §3, §5): names bound
 * against the catalog, and an access path chosen. Needs a transaction for the
 * catalog and no host: a plan is a structure and never holds a value, since
 * constants are evaluated when the statement runs. */

#include "sql.h"
#include "gsql_wire.h"
#include "v3.h"
#include "serialize_tags.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SqlPlan *p;
    SqlArena *arena;
    GsqlError *err;
    const char *qualifier;   /* the alias, or the table's name */
    /* how many of the plan's scans a name may bind to: an ON condition sees
       only the tables joined so far; 0 is all of them */
    size_t visible;
    /* aggregates: whether the clause being bound may hold one, and whether
       the binder is inside one already */
    int agg_ok;
    int in_agg;
    size_t capaggs;
} B;

static int bfail(B *b, uint32_t pos, int code, const char *fmt, ...) {
    if (b->err->code != GSQL_OK) {
        return 0;
    }
    b->err->code = code;
    b->err->position = pos;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b->err->message, sizeof b->err->message, fmt, ap);
    va_end(ap);
    return 0;
}

/* The functions a statement may call, and how many arguments each takes. */
static const struct {
    const char *name;
    int min, max;
} functions[] = {
    { "coalesce", 1, 64 }, { "nullif", 2, 2 }, { "lower", 1, 1 }, { "upper", 1, 1 },
    { "length", 1, 1 }, { "char_length", 1, 1 }, { "abs", 1, 1 }, { "round", 1, 2 },
    { "trim", 1, 1 }, { "mod", 2, 2 }, { NULL, 0, 0 }
};

static int bind(B *b, SqlExpr *e, int cols_allowed, const char *where);

static int bind_name(B *b, SqlExpr *e, int cols_allowed, const char *where) {
    SqlPlan *p = b->p;
    if (!cols_allowed) {
        return bfail(b, e->pos, GSQL_SYNTAX, "%s cannot read a column, and `%s` names one", where, e->parts[0]);
    }
    if (!p->nscans || !p->scans[0].tb) {
        return bfail(b, e->pos, GSQL_NOT_FOUND, "`%s` names a column, and there is no table here", e->parts[0]);
    }
    size_t vis = b->visible ? b->visible : p->nscans;
    long tab = -1;
    size_t first = 0;
    if (e->nparts >= 2) {
        for (size_t t = 0; t < p->nscans; t++) {
            if (strcmp(e->parts[0], p->scans[t].qualifier) == 0) {
                if (t >= vis) {
                    return bfail(b, e->pos, GSQL_SYNTAX, "%s cannot read `%s`: that table is joined after this one",
                                 where, e->parts[0]);
                }
                tab = (long)t;
                first = 1;
                break;
            }
        }
    }
    long col = -1;
    if (tab >= 0) {
        col = gsql_table_find(p->scans[tab].tb, e->parts[1]);
        if (col < 0) {
            return bfail(b, e->pos, GSQL_NOT_FOUND, "table %s has no column `%s`", gsql_table_name(p->scans[tab].tb),
                         e->parts[1]);
        }
    } else {
        /* a bare name: the one table that has it, or a refusal naming them */
        size_t found = 0;
        for (size_t t = 0; t < vis; t++) {
            long c = gsql_table_find(p->scans[t].tb, e->parts[0]);
            if (c < 0) {
                continue;
            }
            if (found++) {
                return bfail(b, e->pos, GSQL_SYNTAX, "column `%s` is in both %s and %s: write %s.%s or %s.%s",
                             e->parts[0], p->scans[tab].qualifier, p->scans[t].qualifier, p->scans[tab].qualifier,
                             e->parts[0], p->scans[t].qualifier, e->parts[0]);
            }
            tab = (long)t;
            col = c;
        }
        if (!found) {
            for (size_t t = 0; t < p->nscans && e->nparts >= 2; t++) {
                if (strcmp(p->scans[t].qualifier, gsql_table_name(p->scans[t].tb)) != 0 &&
                    strcmp(e->parts[0], gsql_table_name(p->scans[t].tb)) == 0) {
                    return bfail(b, e->pos, GSQL_NOT_FOUND,
                                 "the table is called `%s` in this statement, so `%s.%s` must be `%s.%s`",
                                 p->scans[t].qualifier, e->parts[0], e->parts[1], p->scans[t].qualifier, e->parts[1]);
                }
            }
            if (p->nscans == 1) {
                return bfail(b, e->pos, GSQL_NOT_FOUND, "table %s has no column `%s`", gsql_table_name(p->scans[0].tb),
                             e->parts[0]);
            }
            return bfail(b, e->pos, GSQL_NOT_FOUND, "no table %shas a column `%s`",
                         vis < p->nscans ? "joined so far " : "of this statement ", e->parts[0]);
        }
    }
    const GsqlColumn *c = gsql_table_col(p->scans[tab].tb, (size_t)col);
    if (e->nparts > first + 1 && c->kind != GSQL_COL_RECORD) {
        return bfail(b, e->pos, GSQL_TYPE, "column %s is %s, so it has no field `%s`: only a RECORD column has fields",
                     c->name, gsql_kind_name(c->kind), e->parts[first + 1]);
    }
    e->kind = SX_COL;
    e->tab = (unsigned)tab;
    e->col = (unsigned)col;
    e->fields = e->parts + first + 1;
    e->nfields = e->nparts - first - 1;
    return 1;
}

static int bind(B *b, SqlExpr *e, int cols_allowed, const char *where) {
    if (!e) {
        return 1;
    }
    switch (e->kind) {
    case SX_NAME:
        return bind_name(b, e, cols_allowed, where);
    case SX_CALL: {
        static const struct { const char *name; int agg; } aggregates[] = {
            { "count", SQL_AGG_COUNT }, { "sum", SQL_AGG_SUM }, { "avg", SQL_AGG_AVG }, { "min", SQL_AGG_MIN },
            { "max", SQL_AGG_MAX }, { NULL, 0 }
        };
        if (strcmp(e->name, "stddev") == 0 || strcmp(e->name, "variance") == 0) {
            return bfail(b, e->pos, GSQL_UNSUPPORTED, "%s is not in this release of G-SQL; count, sum, avg, min "
                         "and max are", e->name);
        }
        for (int k = 0; aggregates[k].name; k++) {
            if (strcmp(aggregates[k].name, e->name) != 0) {
                continue;
            }
            if (b->in_agg) {
                return bfail(b, e->pos, GSQL_SYNTAX, "an aggregate cannot hold another aggregate");
            }
            if (!b->agg_ok) {
                return bfail(b, e->pos, GSQL_SYNTAX, "%s cannot use an aggregate%s", where,
                             strcmp(where, "WHERE") == 0 ? ": it filters rows before they are grouped, and "
                                                           "HAVING filters the groups" : "");
            }
            if (!e->star && e->na != 1) {
                return bfail(b, e->pos, GSQL_SYNTAX, "%s takes 1 argument, and was given %zu", e->name, e->na);
            }
            e->kind = SX_AGG;
            e->op = e->star ? SQL_AGG_COUNT_STAR : aggregates[k].agg;
            SqlPlan *p = b->p;
            if (p->naggs == b->capaggs) {
                b->capaggs = b->capaggs ? b->capaggs * 2 : 8;
                SqlExpr **grown = sql_alloc(b->arena, sizeof(SqlExpr *) * b->capaggs);
                if (p->naggs) {
                    memcpy(grown, p->aggs, sizeof(SqlExpr *) * p->naggs);
                }
                p->aggs = grown;
            }
            e->index = (unsigned)p->naggs;
            p->aggs[p->naggs++] = e;
            b->in_agg = 1;
            int ok = 1;
            for (size_t j = 0; j < e->na && ok; j++) {
                ok = bind(b, e->a[j], cols_allowed, "an aggregate");
            }
            b->in_agg = 0;
            return ok;
        }
        if (e->distinct) {
            return bfail(b, e->pos, GSQL_SYNTAX, "only an aggregate takes DISTINCT, and %s is not one", e->name);
        }
        int known = 0;
        for (int k = 0; functions[k].name; k++) {
            if (strcmp(functions[k].name, e->name) == 0) {
                known = 1;
                if ((int)e->na < functions[k].min || (int)e->na > functions[k].max) {
                    if (functions[k].min == functions[k].max) {
                        return bfail(b, e->pos, GSQL_SYNTAX, "%s takes %d argument%s, and was given %zu", e->name,
                                     functions[k].min, functions[k].min == 1 ? "" : "s", e->na);
                    }
                    return bfail(b, e->pos, GSQL_SYNTAX, "%s takes %d to %d arguments, and was given %zu", e->name,
                                 functions[k].min, functions[k].max, e->na);
                }
            }
        }
        if (!known) {
            return bfail(b, e->pos, GSQL_NOT_FOUND,
                         "`%s` is not a G-SQL function: the functions are coalesce, nullif, lower, upper, length, "
                         "char_length, abs, round, trim and mod", e->name);
        }
        break;
    }
    default:
        break;
    }
    if (e->operand && !bind(b, e->operand, cols_allowed, where)) {
        return 0;
    }
    for (size_t k = 0; k < e->na; k++) {
        if (!bind(b, e->a[k], cols_allowed, where)) {
            return 0;
        }
    }
    return 1;
}

int sql_is_constant(const SqlExpr *e) {
    if (!e) {
        return 1;
    }
    if (e->kind == SX_COL || e->kind == SX_NAME) {
        return 0;
    }
    if (e->operand && !sql_is_constant(e->operand)) {
        return 0;
    }
    for (size_t k = 0; k < e->na; k++) {
        if (!sql_is_constant(e->a[k])) {
            return 0;
        }
    }
    return 1;
}

/* ---- WHERE into conjuncts --------------------------------------------- */

static void split(B *b, SqlExpr *e, SqlExpr ***v, size_t *n, size_t *cap) {
    if (e->kind == SX_BIN && e->op == SQL_OP_AND) {
        split(b, e->a[0], v, n, cap);
        split(b, e->a[1], v, n, cap);
        return;
    }
    if (*n == *cap) {
        size_t c = *cap ? *cap * 2 : 8;
        SqlExpr **g = sql_alloc(b->arena, sizeof(SqlExpr *) * c);
        if (*n) {
            memcpy(g, *v, sizeof(SqlExpr *) * *n);
        }
        *v = g;
        *cap = c;
    }
    (*v)[(*n)++] = e;
}

/* ---- access paths ----------------------------------------------------- */

/* whether `e` reads only tables read before scan `k`: a constant, from that
 * scan's point of view, since each of those has a row when it is opened */
static int const_before(const SqlExpr *e, size_t k) {
    if (!e) {
        return 1;
    }
    if (e->kind == SX_NAME || e->kind == SX_AGG) {
        return 0;
    }
    if (e->kind == SX_COL) {
        return e->tab < k;
    }
    if (e->operand && !const_before(e->operand, k)) {
        return 0;
    }
    for (size_t j = 0; j < e->na; j++) {
        if (!const_before(e->a[j], k)) {
            return 0;
        }
    }
    return 1;
}

static size_t plain_scan;   /* the scan whose access path is being chosen */

static int plain_col(const SqlExpr *e, unsigned col) {
    return e->kind == SX_COL && e->tab == plain_scan && e->col == col && e->nfields == 0;
}

/* whether column `c`'s keys are ordered as gBASIC's `<` orders its values
 * (§5): an untyped money column's are grouped by currency, and booleans are
 * not ordered by `<` at all */
static int rangeable(const GsqlColumn *c) {
    if (c->kind == GSQL_COL_MONEY && !c->currency[0]) {
        return 0;
    }
    return c->kind != GSQL_COL_BOOLEAN && c->kind != GSQL_COL_RECORD;
}

/* the constant `col = constant` (or `constant = col`) compares with, or NULL */
static SqlExpr *eq_bound(SqlExpr *e, unsigned col) {
    if (e->kind != SX_BIN || e->op != SQL_OP_EQ) {
        return NULL;
    }
    if (plain_col(e->a[0], col) && const_before(e->a[1], plain_scan)) {
        return e->a[1];
    }
    if (plain_col(e->a[1], col) && const_before(e->a[0], plain_scan)) {
        return e->a[0];
    }
    return NULL;
}

/* a range conjunct on `col`: fills *lo and/or *hi, answers whether it did */
static int range_bound(SqlExpr *e, unsigned col, SqlExpr **lo, SqlExpr **hi) {
    if (e->kind == SX_BETWEEN && !e->neg && plain_col(e->a[0], col) && const_before(e->a[1], plain_scan) &&
        const_before(e->a[2], plain_scan)) {
        if (*lo || *hi) {
            return 0;
        }
        *lo = e->a[1];
        *hi = e->a[2];
        return 1;
    }
    if (e->kind != SX_BIN || (e->op != SQL_OP_LT && e->op != SQL_OP_LE && e->op != SQL_OP_GT && e->op != SQL_OP_GE)) {
        return 0;
    }
    int lower;
    SqlExpr *bound;
    if (plain_col(e->a[0], col) && const_before(e->a[1], plain_scan)) {
        lower = e->op == SQL_OP_GT || e->op == SQL_OP_GE;
        bound = e->a[1];
    } else if (plain_col(e->a[1], col) && const_before(e->a[0], plain_scan)) {
        lower = e->op == SQL_OP_LT || e->op == SQL_OP_LE;
        bound = e->a[0];
    } else {
        return 0;
    }
    SqlExpr **slot = lower ? lo : hi;
    if (*slot) {
        return 0;
    }
    *slot = bound;
    return 1;
}

typedef struct {
    SqlAccess access;
    size_t index;
    size_t ncols;
    const unsigned *cols;
    const int *desc;
    int unique;
    size_t neq;
    SqlExpr **eq;
    SqlExpr *lo, *hi;
    int *consumed;
    int score;
} Cand;

static void consider(B *b, SqlScan *sc, Cand *c) {
    /* every condition here may narrow, a LEFT join's WHERE ones included:
       a condition a path can use is a plain column compared with a value,
       which is unknown on a null-extended row, so that row is dropped
       whether or not the path narrowed (docs/gsql_join_design.md §3) */
    size_t nn = sc->nconj;
    c->eq = sql_alloc(b->arena, sizeof(SqlExpr *) * (c->ncols ? c->ncols : 1));
    c->consumed = sql_alloc(b->arena, sizeof(int) * (sc->nconj ? sc->nconj : 1));
    c->neq = 0;
    for (size_t j = 0; j < c->ncols; j++) {
        SqlExpr *found = NULL;
        for (size_t k = 0; k < nn && !found; k++) {
            if (!c->consumed[k] && (found = eq_bound(sc->conj[k], c->cols[j]))) {
                c->consumed[k] = 1;
            }
        }
        if (!found) {
            break;
        }
        c->eq[c->neq++] = found;
    }
    if (c->neq < c->ncols && rangeable(gsql_table_col(sc->tb, c->cols[c->neq]))) {
        for (size_t k = 0; k < nn; k++) {
            if (!c->consumed[k] && range_bound(sc->conj[k], c->cols[c->neq], &c->lo, &c->hi)) {
                c->consumed[k] = 1;
            }
        }
    }
    c->score = (int)c->neq * 2 + (c->lo || c->hi ? 1 : 0);
}

/* Whether walking this key yields ORDER BY's order, forwards (1) or
 * backwards (-1); 0 if it does not. Equality columns may be skipped or named
 * in any direction, since every row the walk yields holds one value there. */
static int order_matches(const SqlPlan *p, const Cand *c) {
    if (p->s->norder == 0) {
        return 1;
    }
    if (p->nscans > 1) {
        /* a join's rows come from several tables: sorted after */
        return 0;
    }
    size_t j = 0;
    int dir = 0;
    for (size_t t = 0; t < p->s->norder; t++) {
        const SqlExpr *e = p->order[t];
        if (!e || e->kind != SX_COL || e->nfields || e->tab != 0) {
            return 0;
        }
        while (j < c->neq && c->cols[j] != e->col) {
            j++;
        }
        if (j < c->neq) {
            j++;
            continue;
        }
        if (j >= c->ncols || c->cols[j] != e->col) {
            return 0;
        }
        int keydesc = c->desc ? c->desc[j] : 0;
        int d = (p->s->order[t].desc ^ keydesc) ? -1 : 1;
        if (dir && d != dir) {
            return 0;
        }
        dir = d;
        j++;
    }
    return dir ? dir : 1;
}

static void choose(B *b, size_t k, int force_scan) {
    SqlPlan *p = b->p;
    SqlScan *sc = &p->scans[k];
    GsqlTable *tb = sc->tb;
    plain_scan = k;
    Cand best;
    memset(&best, 0, sizeof best);
    best.access = ACC_SCAN;
    best.consumed = sql_alloc(b->arena, sizeof(int) * (sc->nconj ? sc->nconj : 1));
    best.eq = NULL;
    if (gsql_table_nkey(tb)) {
        best.cols = gsql_table_key_cols(tb);
        best.ncols = gsql_table_nkey(tb);
        best.unique = 1;
    }
    int best_order = gsql_table_nkey(tb) ? order_matches(p, &best) : (p->s->norder == 0);
    if (!force_scan) {
        Cand cands[1 + 64];
        size_t n = 0;
        if (gsql_table_nkey(tb)) {
            memset(&cands[n], 0, sizeof cands[n]);
            cands[n].access = ACC_PK;
            cands[n].cols = gsql_table_key_cols(tb);
            cands[n].ncols = gsql_table_nkey(tb);
            cands[n].unique = 1;
            n++;
        }
        for (size_t i = 0; i < gsql_table_nindexes(tb) && n < 65; i++) {
            const GsqlIndex *x = gsql_table_index(tb, i);
            memset(&cands[n], 0, sizeof cands[n]);
            cands[n].access = ACC_INDEX;
            cands[n].index = i;
            cands[n].cols = x->cols;
            cands[n].ncols = x->ncols;
            cands[n].desc = x->desc;
            cands[n].unique = x->unique;
            n++;
        }
        int best_score = 0;
        for (size_t i = 0; i < n; i++) {
            consider(b, sc, &cands[i]);
            int ord = order_matches(p, &cands[i]);
            /* more consumed columns first; a path yielding the order breaks a
             * tie, then the primary key, which needs no second read */
            int better = cands[i].score > best_score ||
                         (cands[i].score == best_score && cands[i].score > 0 && ord && !best_order);
            if (better) {
                best = cands[i];
                best_score = cands[i].score;
                best_order = ord;
            }
        }
        if (best_score == 0 && !best_order && p->s->norder) {
            /* nothing narrows: an index whose order is ORDER BY's still saves
             * the sort */
            for (size_t i = 0; i < n; i++) {
                if (cands[i].access == ACC_INDEX) {
                    Cand c = cands[i];
                    c.neq = 0;
                    c.lo = c.hi = NULL;
                    memset(c.consumed, 0, sizeof(int) * (sc->nconj ? sc->nconj : 1));
                    int ord = order_matches(p, &c);
                    if (ord) {
                        best = c;
                        best_order = ord;
                        break;
                    }
                }
            }
        }
    }
    sc->access = best.access;
    sc->index = best.index;
    sc->nkeycols = best.ncols;
    sc->keycols = best.cols;
    sc->keydesc = best.desc;
    sc->neq = best.access == ACC_SCAN ? 0 : best.neq;
    sc->eq = best.eq;
    sc->lo = best.access == ACC_SCAN ? NULL : best.lo;
    sc->hi = best.access == ACC_SCAN ? NULL : best.hi;
    sc->consumed = best.consumed;
    sc->point = best.access != ACC_SCAN && best.unique && best.neq == best.ncols;
    sc->ordered = sc->point || best_order != 0;
    sc->backward = best_order < 0;
}

/* ---- statements -------------------------------------------------------- */

/* Opens table `k` of the statement as its scan. */
static int open_scan(B *b, const GsEngine *e, GsTxn *t, size_t k, const char *table, const char *alias,
                     uint32_t pos, SqlJoin join) {
    SqlPlan *p = b->p;
    SqlScan *sc = &p->scans[k];
    int rc = gsql_table_open(e, t, table, &sc->tb, b->err);
    if (rc != GSQL_OK) {
        sc->tb = NULL;
        if (rc == GSQL_NOT_FOUND) {
            b->err->code = GSQL_OK;
            return bfail(b, pos, GSQL_NOT_FOUND, "there is no table `%s`", table);
        }
        return 0;
    }
    if (k == 0) {
        p->tb = sc->tb;
    }
    sc->qualifier = alias ? alias : table;
    sc->join = join;
    for (size_t j = 0; j < k; j++) {
        if (strcmp(p->scans[j].qualifier, sc->qualifier) == 0) {
            return bfail(b, pos, GSQL_SYNTAX, "`%s` names two tables of this statement: give one of them another name "
                         "with AS", sc->qualifier);
        }
    }
    return 1;
}

static void scans_alloc(B *b, size_t n) {
    b->p->scans = sql_alloc(b->arena, sizeof(SqlScan) * n);
    b->p->nscans = n;
}

static int open_table(B *b, const GsEngine *e, GsTxn *t) {
    SqlPlan *p = b->p;
    scans_alloc(b, 1);
    if (!open_scan(b, e, t, 0, p->s->table, p->s->alias, p->s->pos, SQL_JOIN_FIRST)) {
        return 0;
    }
    b->qualifier = p->scans[0].qualifier;
    return 1;
}

static const char *kind_text(B *b, const GsqlColumn *c) {
    char buf[32];
    if (c->kind == GSQL_COL_MONEY && c->currency[0]) {
        snprintf(buf, sizeof buf, "money(%s)", c->currency);
    } else {
        snprintf(buf, sizeof buf, "%s", gsql_kind_name(c->kind));
    }
    return sql_strndup(b->arena, buf, strlen(buf));
}

static SqlExpr *col_node(B *b, unsigned tab, unsigned col, uint32_t pos) {
    SqlExpr *e = sql_alloc(b->arena, sizeof *e);
    e->kind = SX_COL;
    e->tab = tab;
    e->col = col;
    e->pos = pos;
    return e;
}

/* the last scan an expression reads, or -1 for none */
static long last_scan(const SqlExpr *e) {
    if (!e) {
        return -1;
    }
    long m = e->kind == SX_COL ? (long)e->tab : -1;
    if (e->operand) {
        long o = last_scan(e->operand);
        m = o > m ? o : m;
    }
    for (size_t k = 0; k < e->na; k++) {
        long o = last_scan(e->a[k]);
        m = o > m ? o : m;
    }
    return m;
}

static void scan_add(B *b, SqlScan *sc, SqlExpr *e) {
    SqlExpr **grown = sql_alloc(b->arena, sizeof(SqlExpr *) * (sc->nconj + 1));
    if (sc->nconj) {
        memcpy(grown, sc->conj, sizeof(SqlExpr *) * sc->nconj);
    }
    grown[sc->nconj++] = e;
    sc->conj = grown;
}

/* Each WHERE conjunct is checked at the first scan by which every table it
 * reads has a row (docs/gsql_join_design.md §3). */
static int where_clause(B *b) {
    SqlPlan *p = b->p;
    if (!p->s->where) {
        return 1;
    }
    b->visible = 0;
    if (!bind(b, p->s->where, 1, "WHERE")) {
        return 0;
    }
    SqlExpr **v = NULL;
    size_t n = 0, cap = 0;
    split(b, p->s->where, &v, &n, &cap);
    for (size_t k = 0; k < n; k++) {
        long at = last_scan(v[k]);
        scan_add(b, &p->scans[at < 0 ? 0 : at], v[k]);
    }
    return 1;
}

/* How many outputs a `*` item stands for. */
static size_t star_width(B *b, const SqlItem *it) {
    SqlPlan *p = b->p;
    size_t n = 0;
    for (size_t t = 0; t < p->nscans; t++) {
        if (!it->star_table || strcmp(it->star_table, p->scans[t].qualifier) == 0) {
            n += gsql_table_ncols(p->scans[t].tb);
        }
    }
    return n;
}

/* A grouped query reads a column only inside an aggregate or as part of an
 * expression that IS one of the GROUP BY expressions, compared by canonical
 * print (docs/gsql_aggregate_design.md §4). */
static int is_grouped_expr(B *b, const SqlExpr *e, char **groups) {
    GsqlBuf t = {0};
    sql_print_expr_scans(&t, e, b->p->scans, b->p->nscans);
    v3_put(&t, "", 1);
    int found = 0;
    for (size_t k = 0; k < b->p->s->ngroup && !found; k++) {
        found = strcmp((char *)t.bytes, groups[k]) == 0;
    }
    gsql_buf_free(&t);
    return found;
}

static int check_grouped(B *b, const SqlExpr *e, char **groups, const char *where) {
    if (!e || e->kind == SX_AGG || is_grouped_expr(b, e, groups)) {
        return 1;
    }
    if (e->kind == SX_COL) {
        const char *name = gsql_table_col(b->p->scans[e->tab].tb, e->col)->name;
        return bfail(b, e->pos, GSQL_SYNTAX, "%s reads column `%s`, which is neither grouped by nor inside an "
                     "aggregate: add it to GROUP BY, or use it in one, such as max(%s)", where, name, name);
    }
    if (e->operand && !check_grouped(b, e->operand, groups, where)) {
        return 0;
    }
    for (size_t k = 0; k < e->na; k++) {
        if (!check_grouped(b, e->a[k], groups, where)) {
            return 0;
        }
    }
    return 1;
}

static int plan_select(B *b, const GsEngine *e, GsTxn *t, int force_scan) {
    SqlPlan *p = b->p;
    SqlStmt *s = p->s;
    /* the scans: FROM's tables in order, or one with no table */
    scans_alloc(b, s->table ? 1 + s->njoins : 1);
    if (s->table) {
        if (!open_scan(b, e, t, 0, s->table, s->alias, s->pos, SQL_JOIN_FIRST)) {
            return 0;
        }
        for (size_t j = 0; j < s->njoins; j++) {
            const SqlFrom *f = &s->joins[j];
            if (!open_scan(b, e, t, j + 1, f->table, f->alias, f->pos, f->join)) {
                return 0;
            }
        }
        b->qualifier = p->scans[0].qualifier;
        /* each join's ON, which sees only the tables joined so far */
        for (size_t j = 0; j < s->njoins; j++) {
            SqlExpr *on = s->joins[j].on;
            if (!on) {
                continue;
            }
            b->visible = j + 2;
            int bound = bind(b, on, 1, "ON");
            b->visible = 0;
            if (!bound) {
                return 0;
            }
            SqlExpr **v = NULL;
            size_t n = 0, cap = 0;
            split(b, on, &v, &n, &cap);
            for (size_t k = 0; k < n; k++) {
                scan_add(b, &p->scans[j + 1], v[k]);
            }
            p->scans[j + 1].non = n;
        }
    } else {
        p->scans[0].access = ACC_NONE;
    }
    /* the output: * expanded to every column */
    size_t n = 0;
    for (size_t k = 0; k < s->nitems; k++) {
        if (s->items[k].e) {
            n++;
            continue;
        }
        if (!p->tb) {
            return bfail(b, s->items[k].pos, GSQL_SYNTAX, "`*` needs a table: there is no FROM");
        }
        size_t w = star_width(b, &s->items[k]);
        if (s->items[k].star_table && w == 0) {
            return bfail(b, s->items[k].pos, GSQL_NOT_FOUND, "`%s.*` names no table of this statement, which reads `%s`",
                         s->items[k].star_table, b->qualifier);
        }
        n += w;
    }
    p->out = sql_alloc(b->arena, sizeof(SqlExpr *) * (n ? n : 1));
    p->names = sql_alloc(b->arena, sizeof(char *) * (n ? n : 1));
    p->kinds = sql_alloc(b->arena, sizeof(char *) * (n ? n : 1));
    p->nout = 0;
    for (size_t k = 0; k < s->nitems; k++) {
        SqlItem *it = &s->items[k];
        if (!it->e) {
            for (size_t t = 0; t < p->nscans; t++) {
                if (it->star_table && strcmp(it->star_table, p->scans[t].qualifier) != 0) {
                    continue;
                }
                GsqlTable *tb = p->scans[t].tb;
                for (size_t c = 0; c < gsql_table_ncols(tb); c++) {
                    p->out[p->nout] = col_node(b, (unsigned)t, (unsigned)c, it->pos);
                    p->names[p->nout] = gsql_table_col(tb, c)->name;
                    p->kinds[p->nout] = kind_text(b, gsql_table_col(tb, c));
                    p->nout++;
                }
            }
            continue;
        }
        b->agg_ok = 1;
        int bound = bind(b, it->e, 1, "the select list");
        b->agg_ok = 0;
        if (!bound) {
            return 0;
        }
        const char *name = "?column?";
        const char *kind = "any";
        if (it->alias) {
            name = it->alias;
        } else if (it->e->kind == SX_COL) {
            name = it->e->nfields ? it->e->fields[it->e->nfields - 1]
                                  : gsql_table_col(p->scans[it->e->tab].tb, it->e->col)->name;
        } else if (it->e->kind == SX_CALL || it->e->kind == SX_AGG) {
            name = it->e->name;
        }
        if (it->e->kind == SX_COL && it->e->nfields == 0) {
            kind = kind_text(b, gsql_table_col(p->scans[it->e->tab].tb, it->e->col));
        } else if (it->e->kind == SX_AGG && (it->e->op == SQL_AGG_COUNT || it->e->op == SQL_AGG_COUNT_STAR)) {
            kind = "integer";
        }
        p->out[p->nout] = it->e;
        p->names[p->nout] = name;
        p->kinds[p->nout] = kind;
        p->nout++;
    }
    if (!where_clause(b)) {
        return 0;
    }
    p->order = sql_alloc(b->arena, sizeof(SqlExpr *) * (s->norder ? s->norder : 1));
    p->order_out = sql_alloc(b->arena, sizeof(long) * (s->norder ? s->norder : 1));
    for (size_t k = 0; k < s->norder; k++) {
        SqlExpr *oe = s->order[k].e;
        p->order_out[k] = -1;
        if (oe->kind == SX_LIT && oe->lit[0] == SER_NUMBER && oe->lit[1] == 1) {
            uint64_t v = 0;
            for (int i = 0; i < 8; i++) {
                v |= (uint64_t)oe->lit[2 + i] << (8 * i);
            }
            if (v < 1 || v > p->nout) {
                return bfail(b, oe->pos, GSQL_SYNTAX, "ORDER BY %llu names no column: the select list has %zu",
                             (unsigned long long)v, p->nout);
            }
            p->order_out[k] = (long)(v - 1);
            p->order[k] = p->out[v - 1];
            continue;
        }
        if (oe->kind == SX_NAME && oe->nparts == 1) {
            for (size_t o = 0; o < s->nitems && p->order_out[k] < 0; o++) {
                if (s->items[o].alias && strcmp(s->items[o].alias, oe->parts[0]) == 0) {
                    /* the item's position among the outputs */
                    size_t at = 0;
                    for (size_t q = 0; q < o; q++) {
                        at += s->items[q].e ? 1 : star_width(b, &s->items[q]);
                    }
                    p->order_out[k] = (long)at;
                    p->order[k] = p->out[at];
                }
            }
            if (p->order_out[k] >= 0) {
                continue;
            }
        }
        b->agg_ok = 1;
        int bound = bind(b, oe, 1, "ORDER BY");
        b->agg_ok = 0;
        if (!bound) {
            return 0;
        }
        p->order[k] = oe;
    }
    for (size_t k = 0; k < s->ngroup; k++) {
        if (!bind(b, s->group[k], 1, "GROUP BY")) {
            return 0;
        }
    }
    if (s->having) {
        b->agg_ok = 1;
        int bound = bind(b, s->having, 1, "HAVING");
        b->agg_ok = 0;
        if (!bound) {
            return 0;
        }
    }
    p->grouped = s->ngroup || s->having || p->naggs;
    if (p->grouped) {
        char **groups = sql_alloc(b->arena, sizeof(char *) * (s->ngroup ? s->ngroup : 1));
        for (size_t k = 0; k < s->ngroup; k++) {
            GsqlBuf g = {0};
            sql_print_expr_scans(&g, s->group[k], p->scans, p->nscans);
            groups[k] = sql_strndup(b->arena, (char *)g.bytes, g.length);
            gsql_buf_free(&g);
        }
        for (size_t k = 0; k < p->nout; k++) {
            if (!check_grouped(b, p->out[k], groups, "the select list")) {
                return 0;
            }
        }
        if (!check_grouped(b, s->having, groups, "HAVING")) {
            return 0;
        }
        for (size_t k = 0; k < s->norder; k++) {
            if (p->order_out[k] < 0 && !check_grouped(b, p->order[k], groups, "ORDER BY")) {
                return 0;
            }
        }
    }
    if (s->distinct) {
        /* an ORDER BY term must be an output (docs/gsql_distinct_design.md §4) */
        for (size_t k = 0; k < s->norder; k++) {
            if (p->order_out[k] >= 0) {
                continue;
            }
            GsqlBuf want = {0};
            sql_print_expr_scans(&want, p->order[k], p->scans, p->nscans);
            for (size_t o = 0; o < p->nout && p->order_out[k] < 0; o++) {
                GsqlBuf have = {0};
                sql_print_expr_scans(&have, p->out[o], p->scans, p->nscans);
                if (have.length == want.length && memcmp(have.bytes, want.bytes, want.length) == 0) {
                    p->order_out[k] = (long)o;
                    p->order[k] = p->out[o];
                }
                gsql_buf_free(&have);
            }
            if (p->order_out[k] < 0) {
                int rc = bfail(b, s->order[k].e->pos, GSQL_SYNTAX,
                               "with SELECT DISTINCT, ORDER BY may name only an output column, and `%.*s` is not one: "
                               "each output row stands for rows that may differ in it",
                               (int)want.length, (char *)want.bytes);
                gsql_buf_free(&want);
                return rc;
            }
            gsql_buf_free(&want);
        }
    }
    if (!bind(b, s->limit, 0, "LIMIT") || !bind(b, s->offset, 0, "OFFSET")) {
        return 0;
    }
    if (!p->tb) {
        /* a WHERE with no table filters the one row; ORDER BY of one row is
           that row */
        SqlScan *sc = &p->scans[0];
        sc->access = ACC_NONE;
        sc->ordered = 1;
        sc->consumed = sql_alloc(b->arena, sizeof(int) * (sc->nconj ? sc->nconj : 1));
        return 1;
    }
    for (size_t k = 0; k < p->nscans; k++) {
        choose(b, k, force_scan);
    }
    return 1;
}

static int plan_update_delete(B *b, const GsEngine *e, GsTxn *t, int force_scan) {
    SqlPlan *p = b->p;
    SqlStmt *s = p->s;
    if (!open_table(b, e, t)) {
        return 0;
    }
    if (s->kind == SQ_UPDATE) {
        p->targets = sql_alloc(b->arena, sizeof(unsigned) * s->cols.n);
        p->ntargets = s->cols.n;
        for (size_t k = 0; k < s->cols.n; k++) {
            long c = gsql_table_find(p->tb, s->cols.cols[k]);
            if (c < 0) {
                return bfail(b, s->cols.pos, GSQL_NOT_FOUND, "table %s has no column `%s` to SET", s->table,
                             s->cols.cols[k]);
            }
            for (size_t j = 0; j < k; j++) {
                if (p->targets[j] == (unsigned)c) {
                    return bfail(b, s->cols.pos, GSQL_SYNTAX, "SET assigns column %s twice", s->cols.cols[k]);
                }
            }
            p->targets[k] = (unsigned)c;
            if (!bind(b, s->sets[k], 1, "SET")) {
                return 0;
            }
        }
    }
    if (!where_clause(b)) {
        return 0;
    }
    p->order = NULL;
    choose(b, 0, force_scan);
    return 1;
}

static int plan_insert(B *b, const GsEngine *e, GsTxn *t) {
    SqlPlan *p = b->p;
    SqlStmt *s = p->s;
    if (!open_table(b, e, t)) {
        return 0;
    }
    size_t ncols = gsql_table_ncols(p->tb);
    if (s->cols.n) {
        p->ntargets = s->cols.n;
        p->targets = sql_alloc(b->arena, sizeof(unsigned) * s->cols.n);
        for (size_t k = 0; k < s->cols.n; k++) {
            long c = gsql_table_find(p->tb, s->cols.cols[k]);
            if (c < 0) {
                return bfail(b, s->cols.pos, GSQL_NOT_FOUND, "table %s has no column `%s`", s->table, s->cols.cols[k]);
            }
            for (size_t j = 0; j < k; j++) {
                if (p->targets[j] == (unsigned)c) {
                    return bfail(b, s->cols.pos, GSQL_SYNTAX, "the column list names %s twice", s->cols.cols[k]);
                }
            }
            p->targets[k] = (unsigned)c;
        }
    } else {
        p->ntargets = ncols;
        p->targets = sql_alloc(b->arena, sizeof(unsigned) * (ncols ? ncols : 1));
        for (size_t k = 0; k < ncols; k++) {
            p->targets[k] = (unsigned)k;
        }
    }
    if (s->rowlen != p->ntargets) {
        return bfail(b, s->pos, GSQL_SYNTAX, "INSERT has %zu value%s for %zu column%s", s->rowlen,
                     s->rowlen == 1 ? "" : "s", p->ntargets, p->ntargets == 1 ? "" : "s");
    }
    for (size_t r = 0; r < s->nrows; r++) {
        for (size_t c = 0; c < s->rowlen; c++) {
            if (!bind(b, s->rows[r][c], 0, "VALUES")) {
                return 0;
            }
        }
    }
    return 1;
}

/* A compound SELECT (docs/gsql_setop_design.md): each side planned as the
 * SELECT it is; the result has the first side's names, and an ORDER BY names
 * a column of the result, by name or by position. */
static int plan_compound(B *b, const GsEngine *e, GsTxn *t, int force_scan) {
    static const char *const words[] = { "", "UNION", "INTERSECT", "EXCEPT" };
    SqlPlan *p = b->p;
    SqlStmt *s = p->s;
    p->left = sql_alloc(b->arena, sizeof(SqlPlan));
    if (sql_plan(e, t, s->left, b->arena, p->text, force_scan, p->left, b->err) != GSQL_OK) {
        p->left = NULL;
        return 0;
    }
    p->right = sql_alloc(b->arena, sizeof(SqlPlan));
    if (sql_plan(e, t, s->right, b->arena, p->text, force_scan, p->right, b->err) != GSQL_OK) {
        p->right = NULL;
        return 0;
    }
    if (p->left->nout != p->right->nout) {
        return bfail(b, s->pos, GSQL_SYNTAX, "each side of a %s must have as many columns as the other: "
                     "the left has %zu and the right %zu", words[s->setop], p->left->nout, p->right->nout);
    }
    scans_alloc(b, 1);
    p->scans[0].access = ACC_NONE;
    p->nout = p->left->nout;
    p->out = p->left->out;
    p->names = p->left->names;
    p->kinds = sql_alloc(b->arena, sizeof(char *) * (p->nout ? p->nout : 1));
    for (size_t k = 0; k < p->nout; k++) {
        p->kinds[k] = strcmp(p->left->kinds[k], p->right->kinds[k]) == 0 ? p->left->kinds[k] : "any";
    }
    p->order = sql_alloc(b->arena, sizeof(SqlExpr *) * (s->norder ? s->norder : 1));
    p->order_out = sql_alloc(b->arena, sizeof(long) * (s->norder ? s->norder : 1));
    for (size_t k = 0; k < s->norder; k++) {
        SqlExpr *oe = s->order[k].e;
        long at = -1;
        if (oe->kind == SX_LIT && oe->lit[0] == SER_NUMBER && oe->lit[1] == 1) {
            uint64_t v = 0;
            for (int i = 0; i < 8; i++) {
                v |= (uint64_t)oe->lit[2 + i] << (8 * i);
            }
            if (v < 1 || v > p->nout) {
                return bfail(b, oe->pos, GSQL_SYNTAX, "ORDER BY %llu names no column: the result has %zu",
                             (unsigned long long)v, p->nout);
            }
            at = (long)(v - 1);
        } else if (oe->kind == SX_NAME && oe->nparts == 1) {
            for (size_t o = 0; o < p->nout && at < 0; o++) {
                if (strcmp(p->names[o], oe->parts[0]) == 0) {
                    at = (long)o;
                }
            }
        }
        if (at < 0) {
            GsqlBuf want = {0};
            sql_print_expr(&want, oe);
            int rc = bfail(b, oe->pos, GSQL_SYNTAX, "ORDER BY over a %s may name only a column of the result, by "
                           "name or by position, and `%.*s` is not one", words[s->setop], (int)want.length,
                           (char *)want.bytes);
            gsql_buf_free(&want);
            return rc;
        }
        p->order_out[k] = at;
        p->order[k] = p->out[at];
    }
    return bind(b, s->limit, 0, "LIMIT") && bind(b, s->offset, 0, "OFFSET");
}

int sql_plan(const GsEngine *e, GsTxn *t, SqlStmt *s, SqlArena *arena, const char *text, int force_scan,
             SqlPlan *out, GsqlError *err) {
    memset(out, 0, sizeof *out);
    out->s = s;
    out->text = text;
    B b = { out, arena, err, "", 0, 0, 0, 0 };
    err->code = GSQL_OK;
    err->message[0] = 0;
    err->position = 0;
    int ok = 1;
    switch (s->kind) {
    case SQ_SELECT:
        ok = s->setop ? plan_compound(&b, e, t, force_scan) : plan_select(&b, e, t, force_scan);
        break;
    case SQ_UPDATE:
    case SQ_DELETE:
        ok = plan_update_delete(&b, e, t, force_scan);
        break;
    case SQ_INSERT:
        ok = plan_insert(&b, e, t);
        break;
    case SQ_CREATE_TABLE:
    case SQ_ADD_COLUMN:
        /* a DEFAULT is a constant */
        for (size_t k = 0; k < s->ncoldefs && ok; k++) {
            ok = bind(&b, s->coldefs[k].def, 0, "DEFAULT");
        }
        break;
    default:
        break;
    }
    if (!ok || err->code != GSQL_OK) {
        sql_plan_free(out);
        if (err->code == GSQL_OK) {
            err->code = GSQL_MISUSE;
        }
        return err->code;
    }
    return GSQL_OK;
}

void sql_plan_free(SqlPlan *p) {
    if (p->left) {
        sql_plan_free(p->left);
        p->left = NULL;
    }
    if (p->right) {
        sql_plan_free(p->right);
        p->right = NULL;
    }
    for (size_t k = 1; k < p->nscans; k++) {
        if (p->scans[k].tb) {
            gsql_table_close(p->scans[k].tb);
            p->scans[k].tb = NULL;
        }
    }
    if (p->tb) {
        gsql_table_close(p->tb);
        p->tb = NULL;
    }
}

/* ---- EXPLAIN ------------------------------------------------------------ */

static void emit(void (*line)(void *, const char *), void *ctx, int indent, GsqlBuf *b) {
    char text[1024];
    snprintf(text, sizeof text, "%*s%.*s", indent * 2, "", (int)(b->length < 1000 ? b->length : 1000),
             b->bytes ? (const char *)b->bytes : "");
    line(ctx, text);
    b->length = 0;
}

static void put(GsqlBuf *b, const char *s) {
    v3_put(b, s, strlen(s));
}

static void access_line(const SqlPlan *p, const SqlScan *sc, GsqlBuf *b) {
    const GsqlTable *tb = sc->tb;
    const char *tname = gsql_table_name(tb);
    const char *iname = sc->access == ACC_INDEX ? gsql_table_index(tb, sc->index)->name : NULL;
    if (sc->access == ACC_SCAN) {
        put(b, "Scan on ");
    } else if (sc->point) {
        put(b, "Point Read on ");
    } else if (sc->neq || sc->lo || sc->hi) {
        put(b, sc->access == ACC_INDEX ? "Index Seek on " : "Seek on ");
    } else {
        put(b, "Index Scan on ");
    }
    put(b, tname);
    if (p->nscans > 1 && strcmp(sc->qualifier, tname) != 0) {
        put(b, " AS ");
        put(b, sc->qualifier);
    }
    if (sc->access != ACC_SCAN) {
        put(b, sc->access == ACC_INDEX ? " using " : " using the primary key");
        if (iname) {
            put(b, iname);
        }
        if (sc->neq || sc->lo || sc->hi) {
            put(b, " (");
            for (size_t k = 0; k < sc->neq; k++) {
                if (k) {
                    put(b, ", ");
                }
                put(b, gsql_table_col(tb, sc->keycols[k])->name);
                put(b, " = ");
                sql_print_expr_scans(b, sc->eq[k], p->scans, p->nscans);
            }
            if (sc->lo || sc->hi) {
                if (sc->neq) {
                    put(b, ", ");
                }
                const char *cname = gsql_table_col(tb, sc->keycols[sc->neq])->name;
                if (sc->lo) {
                    sql_print_expr_scans(b, sc->lo, p->scans, p->nscans);
                    put(b, " <= ");
                }
                put(b, cname);
                if (sc->hi) {
                    put(b, " <= ");
                    sql_print_expr_scans(b, sc->hi, p->scans, p->nscans);
                }
            }
            put(b, ")");
        }
    }
    if (sc->backward) {
        put(b, " backward");
    }
}

/* a scan's conditions the access path did not consume, as one Filter line */
static void filter_line(const SqlPlan *p, const SqlScan *sc, void (*line)(void *, const char *), void *ctx,
                        int indent, GsqlBuf *b) {
    int any = 0;
    for (size_t k = 0; k < sc->nconj; k++) {
        if (sc->consumed && sc->consumed[k]) {
            continue;
        }
        put(b, any ? " AND " : "Filter: ");
        sql_print_expr_scans(b, sc->conj[k], p->scans, p->nscans);
        any = 1;
    }
    if (any) {
        emit(line, ctx, indent, b);
    }
}

static void explain_at(const SqlPlan *p, void (*line)(void *, const char *), void *ctx, int base) {
    GsqlBuf b = {0};
    const SqlStmt *s = p->s;
    int indent = base;
    if (s->setop) {
        static const char *const words[] = { "", "Union", "Intersect", "Except" };
        put(&b, words[s->setop]);
        if (s->set_all) {
            put(&b, " All");
        }
        emit(line, ctx, indent, &b);
        explain_at(p->left, line, ctx, indent + 1);
        explain_at(p->right, line, ctx, indent + 1);
        if (s->norder) {
            put(&b, "Order: ");
            for (size_t k = 0; k < s->norder; k++) {
                if (k) {
                    put(&b, ", ");
                }
                put(&b, p->names[p->order_out[k]]);
                if (s->order[k].desc) {
                    put(&b, " DESC");
                }
            }
            put(&b, " (sorted)");
            emit(line, ctx, indent + 1, &b);
        }
        goto tail;
    }
    if (s->kind == SQ_UPDATE || s->kind == SQ_DELETE) {
        put(&b, s->kind == SQ_UPDATE ? "Update on " : "Delete on ");
        put(&b, gsql_table_name(p->tb));
        emit(line, ctx, 0, &b);
        indent = 1;
    }
    if (p->nscans > 1) {
        /* a join: one access line per table, in the order read */
        put(&b, "Nested Loop");
        emit(line, ctx, indent, &b);
        for (size_t k = 0; k < p->nscans; k++) {
            const SqlScan *sc = &p->scans[k];
            if (sc->join == SQL_JOIN_LEFT) {
                put(&b, "Left: ");
            }
            access_line(p, sc, &b);
            emit(line, ctx, indent + 1, &b);
            filter_line(p, sc, line, ctx, indent + 2, &b);
        }
    } else {
        const SqlScan *sc = &p->scans[0];
        if (sc->access == ACC_NONE) {
            put(&b, "Result");
        } else {
            access_line(p, sc, &b);
        }
        emit(line, ctx, indent, &b);
        filter_line(p, sc, line, ctx, indent + 1, &b);
    }
    if (s->kind == SQ_SELECT && p->grouped) {
        put(&b, s->ngroup ? "Aggregate: group by " : "Aggregate: one group");
        for (size_t k = 0; k < s->ngroup; k++) {
            if (k) {
                put(&b, ", ");
            }
            sql_print_expr_scans(&b, s->group[k], p->scans, p->nscans);
        }
        emit(line, ctx, indent + 1, &b);
        if (s->having) {
            put(&b, "Having: ");
            sql_print_expr_scans(&b, s->having, p->scans, p->nscans);
            emit(line, ctx, indent + 1, &b);
        }
    }
    if (s->kind == SQ_SELECT && s->distinct) {
        put(&b, "Distinct: ");
        for (size_t k = 0; k < p->nout; k++) {
            if (k) {
                put(&b, ", ");
            }
            sql_print_expr_scans(&b, p->out[k], p->scans, p->nscans);
        }
        emit(line, ctx, indent + 1, &b);
    }
    if (s->kind == SQ_SELECT && s->norder) {
        const SqlScan *sc = &p->scans[0];
        put(&b, "Order: ");
        for (size_t k = 0; k < s->norder; k++) {
            if (k) {
                put(&b, ", ");
            }
            sql_print_expr_scans(&b, p->order[k], p->scans, p->nscans);
            if (s->order[k].desc) {
                put(&b, " DESC");
            }
        }
        put(&b, p->grouped || p->nscans > 1 ? " (sorted)"
                : sc->access == ACC_NONE || sc->point ? " (one row)"
                : !sc->ordered ? " (sorted)"
                : sc->access == ACC_INDEX ? " (by the index)" : " (by the primary key)");
        emit(line, ctx, indent + 1, &b);
    }
tail:
    if (s->kind == SQ_SELECT && s->limit) {
        put(&b, "Limit: ");
        sql_print_expr_scans(&b, s->limit, p->scans, p->nscans);
        emit(line, ctx, indent + 1, &b);
    }
    if (s->kind == SQ_SELECT && s->offset) {
        put(&b, "Offset: ");
        sql_print_expr_scans(&b, s->offset, p->scans, p->nscans);
        emit(line, ctx, indent + 1, &b);
    }
    gsql_buf_free(&b);
}

void sql_explain(const SqlPlan *p, void (*line)(void *, const char *), void *ctx) {
    explain_at(p, line, ctx, 0);
}

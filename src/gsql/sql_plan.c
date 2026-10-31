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
    const GsqlTable *tb = b->p->tb;
    if (!cols_allowed) {
        return bfail(b, e->pos, GSQL_SYNTAX, "%s cannot read a column, and `%s` names one", where, e->parts[0]);
    }
    if (!tb) {
        return bfail(b, e->pos, GSQL_NOT_FOUND, "`%s` names a column, and there is no table here", e->parts[0]);
    }
    size_t first = 0;
    if (e->nparts >= 2 && strcmp(e->parts[0], b->qualifier) == 0) {
        first = 1;
    }
    long col = gsql_table_find(tb, e->parts[first]);
    if (col < 0) {
        if (first == 0 && e->nparts >= 2 && b->p->s->alias && strcmp(e->parts[0], gsql_table_name(tb)) == 0) {
            return bfail(b, e->pos, GSQL_NOT_FOUND,
                         "the table is called `%s` in this statement, so `%s.%s` must be `%s.%s`", b->qualifier,
                         e->parts[0], e->parts[1], b->qualifier, e->parts[1]);
        }
        return bfail(b, e->pos, GSQL_NOT_FOUND, "table %s has no column `%s`", gsql_table_name(tb), e->parts[first]);
    }
    const GsqlColumn *c = gsql_table_col(tb, (size_t)col);
    if (e->nparts > first + 1 && c->kind != GSQL_COL_RECORD) {
        return bfail(b, e->pos, GSQL_TYPE, "column %s is %s, so it has no field `%s`: only a RECORD column has fields",
                     c->name, gsql_kind_name(c->kind), e->parts[first + 1]);
    }
    e->kind = SX_COL;
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

static int plain_col(const SqlExpr *e, unsigned col) {
    return e->kind == SX_COL && e->col == col && e->nfields == 0;
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
    if (plain_col(e->a[0], col) && sql_is_constant(e->a[1])) {
        return e->a[1];
    }
    if (plain_col(e->a[1], col) && sql_is_constant(e->a[0])) {
        return e->a[0];
    }
    return NULL;
}

/* a range conjunct on `col`: fills *lo and/or *hi, answers whether it did */
static int range_bound(SqlExpr *e, unsigned col, SqlExpr **lo, SqlExpr **hi) {
    if (e->kind == SX_BETWEEN && !e->neg && plain_col(e->a[0], col) && sql_is_constant(e->a[1]) &&
        sql_is_constant(e->a[2])) {
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
    if (plain_col(e->a[0], col) && sql_is_constant(e->a[1])) {
        lower = e->op == SQL_OP_GT || e->op == SQL_OP_GE;
        bound = e->a[1];
    } else if (plain_col(e->a[1], col) && sql_is_constant(e->a[0])) {
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

static void consider(B *b, Cand *c) {
    SqlPlan *p = b->p;
    c->eq = sql_alloc(b->arena, sizeof(SqlExpr *) * (c->ncols ? c->ncols : 1));
    c->consumed = sql_alloc(b->arena, sizeof(int) * (p->nconj ? p->nconj : 1));
    c->neq = 0;
    for (size_t j = 0; j < c->ncols; j++) {
        SqlExpr *found = NULL;
        for (size_t k = 0; k < p->nconj && !found; k++) {
            if (!c->consumed[k] && (found = eq_bound(p->conj[k], c->cols[j]))) {
                c->consumed[k] = 1;
            }
        }
        if (!found) {
            break;
        }
        c->eq[c->neq++] = found;
    }
    if (c->neq < c->ncols && rangeable(gsql_table_col(p->tb, c->cols[c->neq]))) {
        for (size_t k = 0; k < p->nconj; k++) {
            if (!c->consumed[k] && range_bound(p->conj[k], c->cols[c->neq], &c->lo, &c->hi)) {
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
    size_t j = 0;
    int dir = 0;
    for (size_t t = 0; t < p->s->norder; t++) {
        const SqlExpr *e = p->order[t];
        if (!e || e->kind != SX_COL || e->nfields) {
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

static void choose(B *b, int force_scan) {
    SqlPlan *p = b->p;
    GsqlTable *tb = p->tb;
    Cand best;
    memset(&best, 0, sizeof best);
    best.access = ACC_SCAN;
    best.consumed = sql_alloc(b->arena, sizeof(int) * (p->nconj ? p->nconj : 1));
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
            consider(b, &cands[i]);
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
                    memset(c.consumed, 0, sizeof(int) * (p->nconj ? p->nconj : 1));
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
    p->access = best.access;
    p->index = best.index;
    p->nkeycols = best.ncols;
    p->keycols = best.cols;
    p->keydesc = best.desc;
    p->neq = best.access == ACC_SCAN ? 0 : best.neq;
    p->eq = best.eq;
    p->lo = best.access == ACC_SCAN ? NULL : best.lo;
    p->hi = best.access == ACC_SCAN ? NULL : best.hi;
    p->consumed = best.consumed;
    p->point = best.access != ACC_SCAN && best.unique && best.neq == best.ncols;
    p->ordered = p->point || best_order != 0;
    p->backward = best_order < 0;
}

/* ---- statements -------------------------------------------------------- */

static int open_table(B *b, const GsEngine *e, GsTxn *t) {
    SqlPlan *p = b->p;
    int rc = gsql_table_open(e, t, p->s->table, &p->tb, b->err);
    if (rc != GSQL_OK) {
        p->tb = NULL;
        if (rc == GSQL_NOT_FOUND) {
            b->err->code = GSQL_OK;
            return bfail(b, p->s->pos, GSQL_NOT_FOUND, "there is no table `%s`", p->s->table);
        }
        return 0;
    }
    b->qualifier = p->s->alias ? p->s->alias : p->s->table;
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

static SqlExpr *col_node(B *b, unsigned col, uint32_t pos) {
    SqlExpr *e = sql_alloc(b->arena, sizeof *e);
    e->kind = SX_COL;
    e->col = col;
    e->pos = pos;
    return e;
}

static int where_clause(B *b) {
    SqlPlan *p = b->p;
    if (!p->s->where) {
        return 1;
    }
    if (!bind(b, p->s->where, 1, "WHERE")) {
        return 0;
    }
    size_t cap = 0;
    split(b, p->s->where, &p->conj, &p->nconj, &cap);
    return 1;
}

/* A grouped query reads a column only inside an aggregate or as part of an
 * expression that IS one of the GROUP BY expressions, compared by canonical
 * print (docs/gsql_aggregate_design.md §4). */
static int is_grouped_expr(B *b, const SqlExpr *e, char **groups) {
    GsqlBuf t = {0};
    sql_print_expr_tb(&t, e, b->p->tb);
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
        const char *name = gsql_table_col(b->p->tb, e->col)->name;
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
    if (s->table && !open_table(b, e, t)) {
        return 0;
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
        if (s->items[k].star_table && strcmp(s->items[k].star_table, b->qualifier) != 0) {
            return bfail(b, s->items[k].pos, GSQL_NOT_FOUND, "`%s.*` names no table of this statement, which reads `%s`",
                         s->items[k].star_table, b->qualifier);
        }
        n += gsql_table_ncols(p->tb);
    }
    p->out = sql_alloc(b->arena, sizeof(SqlExpr *) * (n ? n : 1));
    p->names = sql_alloc(b->arena, sizeof(char *) * (n ? n : 1));
    p->kinds = sql_alloc(b->arena, sizeof(char *) * (n ? n : 1));
    p->nout = 0;
    for (size_t k = 0; k < s->nitems; k++) {
        SqlItem *it = &s->items[k];
        if (!it->e) {
            for (size_t c = 0; c < gsql_table_ncols(p->tb); c++) {
                p->out[p->nout] = col_node(b, (unsigned)c, it->pos);
                p->names[p->nout] = gsql_table_col(p->tb, c)->name;
                p->kinds[p->nout] = kind_text(b, gsql_table_col(p->tb, c));
                p->nout++;
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
            name = it->e->nfields ? it->e->fields[it->e->nfields - 1] : gsql_table_col(p->tb, it->e->col)->name;
        } else if (it->e->kind == SX_CALL || it->e->kind == SX_AGG) {
            name = it->e->name;
        }
        if (it->e->kind == SX_COL && it->e->nfields == 0) {
            kind = kind_text(b, gsql_table_col(p->tb, it->e->col));
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
                        at += s->items[q].e ? 1 : gsql_table_ncols(p->tb);
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
            sql_print_expr_tb(&g, s->group[k], p->tb);
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
    if (!bind(b, s->limit, 0, "LIMIT") || !bind(b, s->offset, 0, "OFFSET")) {
        return 0;
    }
    if (!p->tb) {
        p->access = ACC_NONE;
        p->ordered = 1;
        if (p->nconj || s->norder) {
            /* a WHERE with no table filters the one row; ORDER BY of one row
             * is that row */
        }
        p->consumed = sql_alloc(b->arena, sizeof(int) * (p->nconj ? p->nconj : 1));
        return 1;
    }
    choose(b, force_scan);
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
    choose(b, force_scan);
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

int sql_plan(const GsEngine *e, GsTxn *t, SqlStmt *s, SqlArena *arena, const char *text, int force_scan,
             SqlPlan *out, GsqlError *err) {
    memset(out, 0, sizeof *out);
    out->s = s;
    out->text = text;
    B b = { out, arena, err, "", 0, 0, 0 };
    err->code = GSQL_OK;
    err->message[0] = 0;
    err->position = 0;
    int ok = 1;
    switch (s->kind) {
    case SQ_SELECT:
        ok = plan_select(&b, e, t, force_scan);
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

static void access_line(const SqlPlan *p, GsqlBuf *b) {
    const GsqlTable *tb = p->tb;
    const char *tname = gsql_table_name(tb);
    const char *iname = p->access == ACC_INDEX ? gsql_table_index(tb, p->index)->name : NULL;
    if (p->access == ACC_SCAN) {
        put(b, "Scan on ");
        put(b, tname);
    } else {
        if (p->point) {
            put(b, "Point Read on ");
        } else if (p->neq || p->lo || p->hi) {
            put(b, p->access == ACC_INDEX ? "Index Seek on " : "Seek on ");
        } else {
            put(b, "Index Scan on ");
        }
        put(b, tname);
        put(b, p->access == ACC_INDEX ? " using " : " using the primary key");
        if (iname) {
            put(b, iname);
        }
        if (p->neq || p->lo || p->hi) {
            put(b, " (");
            for (size_t k = 0; k < p->neq; k++) {
                if (k) {
                    put(b, ", ");
                }
                put(b, gsql_table_col(tb, p->keycols[k])->name);
                put(b, " = ");
                sql_print_expr_tb(b, p->eq[k], tb);
            }
            if (p->lo || p->hi) {
                if (p->neq) {
                    put(b, ", ");
                }
                const char *cname = gsql_table_col(tb, p->keycols[p->neq])->name;
                if (p->lo) {
                    sql_print_expr_tb(b, p->lo, tb);
                    put(b, " <= ");
                }
                put(b, cname);
                if (p->hi) {
                    put(b, " <= ");
                    sql_print_expr_tb(b, p->hi, tb);
                }
            }
            put(b, ")");
        }
    }
    if (p->backward) {
        put(b, " backward");
    }
}

void sql_explain(const SqlPlan *p, void (*line)(void *, const char *), void *ctx) {
    GsqlBuf b = {0};
    const SqlStmt *s = p->s;
    int indent = 0;
    if (s->kind == SQ_UPDATE || s->kind == SQ_DELETE) {
        put(&b, s->kind == SQ_UPDATE ? "Update on " : "Delete on ");
        put(&b, gsql_table_name(p->tb));
        emit(line, ctx, 0, &b);
        indent = 1;
    }
    if (p->access == ACC_NONE) {
        put(&b, "Result");
    } else {
        access_line(p, &b);
    }
    emit(line, ctx, indent, &b);
    int any = 0;
    for (size_t k = 0; k < p->nconj; k++) {
        if (p->consumed && p->consumed[k]) {
            continue;
        }
        put(&b, any ? " AND " : "Filter: ");
        sql_print_expr_tb(&b, p->conj[k], p->tb);
        any = 1;
    }
    if (any) {
        emit(line, ctx, indent + 1, &b);
    }
    if (s->kind == SQ_SELECT && p->grouped) {
        put(&b, s->ngroup ? "Aggregate: group by " : "Aggregate: one group");
        for (size_t k = 0; k < s->ngroup; k++) {
            if (k) {
                put(&b, ", ");
            }
            sql_print_expr_tb(&b, s->group[k], p->tb);
        }
        emit(line, ctx, indent + 1, &b);
        if (s->having) {
            put(&b, "Having: ");
            sql_print_expr_tb(&b, s->having, p->tb);
            emit(line, ctx, indent + 1, &b);
        }
    }
    if (s->kind == SQ_SELECT && s->norder) {
        put(&b, "Order: ");
        for (size_t k = 0; k < s->norder; k++) {
            if (k) {
                put(&b, ", ");
            }
            sql_print_expr_tb(&b, p->order[k], p->tb);
            if (s->order[k].desc) {
                put(&b, " DESC");
            }
        }
        put(&b, p->grouped ? " (sorted)"
                : p->access == ACC_NONE || p->point ? " (one row)"
                : !p->ordered ? " (sorted)"
                : p->access == ACC_INDEX ? " (by the index)" : " (by the primary key)");
        emit(line, ctx, indent + 1, &b);
    }
    if (s->kind == SQ_SELECT && s->limit) {
        put(&b, "Limit: ");
        sql_print_expr_tb(&b, s->limit, p->tb);
        emit(line, ctx, indent + 1, &b);
    }
    if (s->kind == SQ_SELECT && s->offset) {
        put(&b, "Offset: ");
        sql_print_expr_tb(&b, s->offset, p->tb);
        emit(line, ctx, indent + 1, &b);
    }
    gsql_buf_free(&b);
}

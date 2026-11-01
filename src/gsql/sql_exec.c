/* G-SQL's executor and the owner's service (docs/gsql_sql_design.md §2-§6).
 *
 * Every value goes through the HOST (include/gsql_sql.h): this file decides
 * which operator applies to which operands and in what order, and never what
 * an operator means. What is SQL's own -- IS NULL, ||, IN, BETWEEN, LIKE,
 * CASE, COALESCE, NULLIF, WHERE keeping only what is true, and the refusal of
 * implicit casts -- is written here over the host's answers (§2). */

#include "gsql_sql.h"
#include "sql.h"
#include "v3.h"
#include "gsql_key.h"
#include "gsql_wire.h"
#include "serialize_tags.h"
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct GsqlSql {
    GsqlHost h;
    int force_scan;
    unsigned long seeks, scans;
    /* prepared statements: their texts, by id (1-based) */
    char **prepared;
    size_t *prepared_len;
    size_t nprepared;
    /* warnings the SQL layer raised itself, waiting for the statement's
       reply (docs/gsql_warning_design.md) */
    char **wmsg;
    int *wcode;
    size_t nw;
};

/* A warning of the SQL layer's own, for the statement now running. */
static void sql_warn(GsqlSql *q, int code, const char *message) {
    char **m = realloc(q->wmsg, sizeof *m * (q->nw + 1));
    int *c = realloc(q->wcode, sizeof *c * (q->nw + 1));
    if (!m || !c) {
        abort();
    }
    q->wmsg = m;
    q->wcode = c;
    size_t n = strlen(message);
    q->wmsg[q->nw] = malloc(n + 1);
    if (!q->wmsg[q->nw]) {
        abort();
    }
    memcpy(q->wmsg[q->nw], message, n + 1);
    q->wcode[q->nw++] = code;
}


/* ---- errors ------------------------------------------------------------ */

static int xfail(GsqlError *err, int code, uint32_t pos, const char *fmt, ...) {
    if (err->code != GSQL_OK) {
        return err->code;
    }
    err->code = code;
    err->position = pos;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof err->message, fmt, ap);
    va_end(ap);
    return code;
}

static const char *tag_word(int tag) {
    switch (tag) {
    case SER_NULL:
    case SER_UNKNOWN: return "NULL";
    case SER_BOOL: return "a boolean";
    case SER_NUMBER: return "a number";
    case SER_STRING: return "text";
    case SER_ARRAY: return "an array";
    case SER_RECORD: return "a record";
    case SER_DATETIME: return "a date-time";
    case SER_DURATION: return "a duration";
    case SER_MONEY: return "money";
    default: return "a value G-SQL does not store";
    }
}

/* ---- one execution ------------------------------------------------------ */

typedef struct X {
    GsqlSql *q;
    const GsqlHost *h;
    const GsEngine *e;
    GsTxn *t;
    SqlPlan *p;
    GsqlError *err;
    /* parameters: one v3 record (by name) or array (by position), or none */
    const unsigned char *params;
    size_t paramslen;
    /* each scan's current row (docs/gsql_join_design.md): one for a
       single-table statement, one per table of a join */
    struct XScan *sx;
    size_t nsx;
    /* nodes whose hv this execution set, released at its end */
    SqlExpr **cached;
    size_t ncached, capcached;
    /* a grouped query's current group: each aggregate's final value, by slot */
    GsqlHv **aggv;
    /* a subquery's execution: the enclosing query's, whose rows an outer
       reference reads (docs/gsql_subquery_design.md §4) */
    struct X *outer;
} X;

static GsqlHv *hv_bytes(X *x, const unsigned char *b, size_t n) {
    return x->h->load(x->h->ctx, b, n, x->err);
}

static GsqlHv *hv_bool(X *x, int v) {
    unsigned char b[2] = { SER_BOOL, (unsigned char)(v ? 1 : 0) };
    return hv_bytes(x, b, 2);
}

static GsqlHv *hv_unknown(X *x) {
    unsigned char b[1] = { SER_UNKNOWN };
    return hv_bytes(x, b, 1);
}

/* Kleene's three values as an answer: 1, 0, or -1 for unknown */
static GsqlHv *hv_tri(X *x, int v) {
    return v < 0 ? hv_unknown(x) : hv_bool(x, v);
}

static void rel(X *x, GsqlHv *v) {
    if (v) {
        x->h->release(x->h->ctx, v);
    }
}

static int kind_of(X *x, const GsqlHv *v) {
    return x->h->kind(x->h->ctx, v);
}

static int is_null(X *x, const GsqlHv *v) {
    int k = kind_of(x, v);
    return k == SER_UNKNOWN || k == SER_NULL;
}

/* a set of key byte strings, open addressing; values are 1-based slots */
typedef struct {
    size_t *slot;
    size_t cap, n;
} KeyMap;

/* A scan's row: the bytes, its key, and its columns as host values, loaded
 * on first use. `nulled` is a LEFT join's null-extended row, every column of
 * which reads NULL. */
typedef struct XScan {
    GsqlBuf row;
    GsqlBuf rowkey;
    int have_row;
    int nulled;
    GsqlHv **cols;
    size_t ncols;
    /* a derived scan's rows, materialized on its first opening in this
       execution (docs/gsql_derived_design.md §4) */
    GsqlBuf *drows;
    size_t ndrows;
    int dready;
    /* a CHECK's row (docs/gsql_check_design.md): the values about to be
       written, read by position instead of from stored row bytes */
    const GsBytes *direct;
    /* a hash join's inner side (docs/gsql_hashjoin_design.md §2): drows
       grouped by their hashed column's key, each group a chain of 1-based
       row numbers in the order read */
    int hready;
    KeyMap hmap;
    GsqlBuf *hkeys;
    size_t nhkeys;
    size_t *hfirst, *hlast, *hnext;
} XScan;

static void row_clear_scan(X *x, size_t k) {
    XScan *xs = &x->sx[k];
    for (size_t c = 0; c < xs->ncols; c++) {
        rel(x, xs->cols[c]);
        xs->cols[c] = NULL;
    }
    xs->have_row = 0;
    xs->nulled = 0;
}

static void row_clear(X *x) {
    for (size_t k = 0; k < x->nsx; k++) {
        row_clear_scan(x, k);
    }
}

static void x_init(X *x, GsqlSql *q, const GsEngine *e, GsTxn *t, SqlPlan *p, GsqlError *err) {
    memset(x, 0, sizeof *x);
    x->q = q;
    x->h = &q->h;
    x->e = e;
    x->t = t;
    x->p = p;
    x->err = err;
    x->nsx = p->nscans ? p->nscans : 1;
    x->sx = calloc(x->nsx, sizeof(XScan));
    if (!x->sx) {
        abort();
    }
    for (size_t k = 0; k < x->nsx; k++) {
        x->sx[k].ncols = p->nscans ? sql_scan_ncols(&p->scans[k]) : p->tb ? gsql_table_ncols(p->tb) : 0;
        x->sx[k].cols = calloc(x->sx[k].ncols ? x->sx[k].ncols : 1, sizeof(GsqlHv *));
        if (!x->sx[k].cols) {
            abort();
        }
    }
}

static void x_free(X *x) {
    row_clear(x);
    for (size_t k = 0; k < x->nsx; k++) {
        free(x->sx[k].cols);
        gsql_buf_free(&x->sx[k].row);
        gsql_buf_free(&x->sx[k].rowkey);
        for (size_t r = 0; r < x->sx[k].ndrows; r++) {
            gsql_buf_free(&x->sx[k].drows[r]);
        }
        free(x->sx[k].drows);
        for (size_t g = 0; g < x->sx[k].nhkeys; g++) {
            gsql_buf_free(&x->sx[k].hkeys[g]);
        }
        free(x->sx[k].hkeys);
        free(x->sx[k].hfirst);
        free(x->sx[k].hlast);
        free(x->sx[k].hnext);
        free(x->sx[k].hmap.slot);
    }
    free(x->sx);
    x->sx = NULL;
    x->nsx = 0;
    for (size_t k = 0; k < x->ncached; k++) {
        SqlExpr *e = x->cached[k];
        rel(x, e->hv);
        e->hv = NULL;
        for (size_t j = 0; j < e->nset; j++) {
            rel(x, e->set[j]);
        }
        free(e->set);
        e->set = NULL;
        e->nset = 0;
        e->gathered = 0;
    }
    free(x->cached);
    x->cached = NULL;
    x->ncached = 0;
}

static void remember(X *x, SqlExpr *e) {
    if (x->ncached == x->capcached) {
        x->capcached = x->capcached ? x->capcached * 2 : 16;
        x->cached = realloc(x->cached, sizeof(SqlExpr *) * x->capcached);
        if (!x->cached) {
            abort();
        }
    }
    x->cached[x->ncached++] = e;
}

/* a parameter's bytes, or NULL with err filled */
static int param_bytes(X *x, const SqlExpr *e, const unsigned char **v, size_t *n) {
    if (!x->params || x->paramslen == 0) {
        goto missing;
    }
    if (e->name) {
        if (x->params[0] != SER_RECORD) {
            return xfail(x->err, GSQL_NOT_FOUND, e->pos,
                         "the statement names its parameter :%s, so its parameters must be a record, not %s", e->name,
                         tag_word(x->params[0]));
        }
        V3Field f[1024];
        long nf = v3_record_fields(x->params, x->paramslen, f, 1024);
        const V3Field *hit = nf >= 0 ? v3_field_named(f, nf, e->name) : NULL;
        if (!hit) {
            goto missing;
        }
        *v = hit->v;
        *n = hit->vlen;
        return GSQL_OK;
    }
    if (x->params[0] != SER_ARRAY) {
        return xfail(x->err, GSQL_NOT_FOUND, e->pos,
                     "the statement numbers its parameter $%u, so its parameters must be an array, not %s", e->index,
                     tag_word(x->params[0]));
    }
    GsBytes items[1024];
    long ni = v3_array_items(x->params, x->paramslen, items, 1024);
    if (ni < (long)e->index) {
        goto missing;
    }
    *v = items[e->index - 1].p;
    *n = items[e->index - 1].n;
    return GSQL_OK;
missing:
    if (e->name) {
        return xfail(x->err, GSQL_NOT_FOUND, e->pos, "no value was given for the parameter :%s", e->name);
    }
    return xfail(x->err, GSQL_NOT_FOUND, e->pos, "no value was given for the parameter $%u", e->index);
}

static GsqlHv *eval(X *x, SqlExpr *e);

/* The host's failure, placed at the expression that caused it. */
static GsqlHv *placed(X *x, GsqlHv *v, const SqlExpr *e) {
    if (!v && x->err->position == 0) {
        x->err->position = e->pos;
    }
    if (!v && x->err->code == GSQL_OK) {
        xfail(x->err, GSQL_EXPRESSION, e->pos, "the expression could not be evaluated");
    }
    return v;
}

static GsqlHv *column(X *x, SqlExpr *e) {
    X *at = x;
    for (unsigned d = 0; e->kind == SX_OUTER && d < e->index && at; d++) {
        at = at->outer;
    }
    if (!at) {
        xfail(x->err, GSQL_MISUSE, e->pos, "an outer reference was read with no enclosing query");
        return NULL;
    }
    XScan *xs = e->tab < at->nsx ? &at->sx[e->tab] : NULL;
    if (xs && xs->nulled) {
        return hv_unknown(x);
    }
    if (!xs || !xs->have_row || e->col >= xs->ncols) {
        xfail(x->err, GSQL_MISUSE, e->pos, "a column was read with no row");
        return NULL;
    }
    if (!xs->cols[e->col]) {
        GsBytes v;
        const SqlScan *sc = &at->p->scans[e->tab];
        if (xs->direct) {
            v = xs->direct[e->col];
        } else if (sc->derived) {
            /* a derived table's row is its plan's output row: by position */
            GsBytes *items = malloc(sizeof(GsBytes) * (xs->ncols ? xs->ncols : 1));
            if (!items) {
                abort();
            }
            long n = v3_array_items(xs->row.bytes, xs->row.length, items, (long)xs->ncols);
            v = items[e->col];
            free(items);
            if (n != (long)xs->ncols) {
                xfail(x->err, GSQL_MISUSE, e->pos, "a derived table's row did not hold its columns");
                return NULL;
            }
        } else if (gsql_row_value(sc->tb, xs->row.bytes, xs->row.length, e->col, &v, x->err) != GSQL_OK) {
            return NULL;
        }
        xs->cols[e->col] = hv_bytes(x, v.p, v.n);
        if (!xs->cols[e->col]) {
            return NULL;
        }
    }
    GsqlHv *v = x->h->copy(x->h->ctx, xs->cols[e->col]);
    for (size_t k = 0; k < e->nfields && v; k++) {
        GsqlHv *f = x->h->field(x->h->ctx, v, e->fields[k], x->err);
        rel(x, v);
        v = placed(x, f, e);
    }
    return v;
}

/* the text inside a host string, copied; NULL if it is not one */
static char *text_of(X *x, const GsqlHv *v, size_t *len) {
    GsqlBuf b = {0};
    if (x->h->store(x->h->ctx, v, &b, x->err) != GSQL_OK) {
        gsql_buf_free(&b);
        return NULL;
    }
    char *out = NULL;
    if (b.length && b.bytes[0] == SER_STRING) {
        V3In in = { b.bytes, b.length, 1, 1 };
        uint64_t n = v3_in_uleb(&in);
        if (in.ok && in.pos + n <= b.length) {
            out = malloc(n + 1);
            if (!out) {
                abort();
            }
            memcpy(out, b.bytes + in.pos, n);
            out[n] = 0;
            *len = n;
        }
    }
    gsql_buf_free(&b);
    return out;
}

static GsqlHv *text_value(X *x, const char *s, size_t n) {
    GsqlBuf b = {0};
    v3_u8(&b, SER_STRING);
    v3_uleb(&b, n);
    v3_put(&b, s, n);
    GsqlHv *v = hv_bytes(x, b.bytes, b.length);
    gsql_buf_free(&b);
    return v;
}

/* §2's no-implicit-cast rule for a comparison: two values of one kind, or a
 * NULL, which the host answers unknown */
static int comparable(X *x, const GsqlHv *a, const GsqlHv *b, int op, const SqlExpr *e) {
    int ka = kind_of(x, a), kb = kind_of(x, b);
    if (ka == SER_UNKNOWN || ka == SER_NULL || kb == SER_UNKNOWN || kb == SER_NULL) {
        return 1;
    }
    if (ka != kb) {
        xfail(x->err, GSQL_EXPRESSION, e->pos,
              "%s compares %s with %s: G-SQL does not convert one kind to another, so CAST one side", sql_op_text(op),
              tag_word(ka), tag_word(kb));
        return 0;
    }
    return 1;
}

static int null_kind(int k) {
    return k == SER_UNKNOWN || k == SER_NULL;
}

/* §2: arithmetic is on numbers and money; text is joined with || */
static int arithmetic_ok(X *x, const GsqlHv *a, const GsqlHv *b, int op, const SqlExpr *e) {
    const GsqlHv *vs[2] = { a, b };
    for (int i = 0; i < 2; i++) {
        if (!vs[i]) {
            continue;
        }
        int k = kind_of(x, vs[i]);
        if (k == SER_STRING && op == SQL_OP_ADD) {
            xfail(x->err, GSQL_EXPRESSION, e->pos, "+ adds numbers and money, and one side is text: join text with ||");
            return 0;
        }
        if (k == SER_STRING || k == SER_BOOL || k == SER_RECORD || k == SER_ARRAY) {
            xfail(x->err, GSQL_EXPRESSION, e->pos, "%s works on numbers and money, and one side is %s",
                  op == 0 ? "-" : sql_op_text(op), tag_word(k));
            return 0;
        }
    }
    return 1;
}

/* a boolean operand of AND, OR or NOT: its truth, or -2 with err filled */
static int logical(X *x, const GsqlHv *v, const char *what, const SqlExpr *e) {
    int t = x->h->truth(x->h->ctx, v);
    if (t == -2) {
        xfail(x->err, GSQL_EXPRESSION, e->pos, "%s works on booleans, and was given %s", what, tag_word(kind_of(x, v)));
    }
    return t;
}

/* a comparison of two values already evaluated: Kleene's answer */
static int compare(X *x, int op, const GsqlHv *a, const GsqlHv *b, const SqlExpr *e) {
    if (!comparable(x, a, b, op, e)) {
        return -2;
    }
    GsqlHv *r = placed(x, x->h->op(x->h->ctx, op, a, b, x->err), e);
    if (!r) {
        return -2;
    }
    int t = x->h->truth(x->h->ctx, r);
    rel(x, r);
    if (t == -2) {
        xfail(x->err, GSQL_EXPRESSION, e->pos, "a comparison answered something other than a boolean");
    }
    return t;
}

static int kleene_and(int a, int b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    return a < 0 || b < 0 ? -1 : 1;
}

/* LIKE: % any run of characters, _ exactly one (a UTF-8 character) */
static size_t utf8_len(unsigned char c) {
    return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
}

static int like(const char *s, size_t sn, const char *p, size_t pn) {
    size_t si = 0, pi = 0, star_p = (size_t)-1, star_s = 0;
    while (si < sn) {
        if (pi < pn && p[pi] == '%') {
            star_p = pi++;
            star_s = si;
        } else if (pi < pn && p[pi] == '_') {
            si += utf8_len((unsigned char)s[si]);
            pi++;
        } else if (pi < pn && p[pi] == s[si]) {
            si++;
            pi++;
        } else if (star_p != (size_t)-1) {
            star_s += utf8_len((unsigned char)s[star_s]);
            si = star_s;
            pi = star_p + 1;
        } else {
            return 0;
        }
    }
    while (pi < pn && p[pi] == '%') {
        pi++;
    }
    return pi == pn;
}

static GsqlHv *eval_bin(X *x, SqlExpr *e) {
    int op = e->op;
    if (op == SQL_OP_AND || op == SQL_OP_OR) {
        GsqlHv *l = eval(x, e->a[0]);
        if (!l) {
            return NULL;
        }
        int tl = logical(x, l, op == SQL_OP_AND ? "AND" : "OR", e);
        if (tl == -2) {
            rel(x, l);
            return NULL;
        }
        /* the right side is evaluated only when the left does not decide */
        if ((op == SQL_OP_AND && tl == 0) || (op == SQL_OP_OR && tl == 1)) {
            return l;
        }
        GsqlHv *r = eval(x, e->a[1]);
        if (!r || logical(x, r, op == SQL_OP_AND ? "AND" : "OR", e) == -2) {
            rel(x, l);
            rel(x, r);
            return NULL;
        }
        GsqlHv *v = placed(x, x->h->op(x->h->ctx, op, l, r, x->err), e);
        rel(x, l);
        rel(x, r);
        return v;
    }
    GsqlHv *l = eval(x, e->a[0]);
    if (!l) {
        return NULL;
    }
    GsqlHv *r = eval(x, e->a[1]);
    if (!r) {
        rel(x, l);
        return NULL;
    }
    GsqlHv *v = NULL;
    if (op == SQL_OP_CONCAT) {
        int kl = kind_of(x, l), kr = kind_of(x, r);
        if (null_kind(kl) || null_kind(kr)) {
            v = hv_unknown(x);
        } else if (kl != SER_STRING || kr != SER_STRING) {
            xfail(x->err, GSQL_EXPRESSION, e->pos, "|| joins text, and one side is %s: CAST it to TEXT",
                  tag_word(kl != SER_STRING ? kl : kr));
        } else {
            size_t ln = 0, rn = 0;
            char *ls = text_of(x, l, &ln);
            char *rs = ls ? text_of(x, r, &rn) : NULL;
            if (ls && rs) {
                char *both = malloc(ln + rn + 1);
                if (!both) {
                    abort();
                }
                memcpy(both, ls, ln);
                memcpy(both + ln, rs, rn);
                v = text_value(x, both, ln + rn);
                free(both);
            }
            free(ls);
            free(rs);
        }
    } else if (op >= SQL_OP_EQ && op <= SQL_OP_GE) {
        if (comparable(x, l, r, op, e)) {
            v = placed(x, x->h->op(x->h->ctx, op, l, r, x->err), e);
        }
    } else if (arithmetic_ok(x, l, r, op, e)) {
        if (op == SQL_OP_MOD) {
            if (null_kind(kind_of(x, l)) || null_kind(kind_of(x, r))) {
                v = hv_unknown(x);
            } else {
                GsqlHv *args[2] = { l, r };
                v = placed(x, x->h->call(x->h->ctx, "mod", args, 2, x->err), e);
            }
        } else {
            v = placed(x, x->h->op(x->h->ctx, op, l, r, x->err), e);
        }
    }
    rel(x, l);
    rel(x, r);
    return v;
}

static GsqlHv *eval_call(X *x, SqlExpr *e) {
    if (strcmp(e->name, "coalesce") == 0) {
        for (size_t k = 0; k < e->na; k++) {
            GsqlHv *v = eval(x, e->a[k]);
            if (!v) {
                return NULL;
            }
            if (!is_null(x, v)) {
                return v;
            }
            rel(x, v);
        }
        return hv_unknown(x);
    }
    if (strcmp(e->name, "nullif") == 0) {
        GsqlHv *a = eval(x, e->a[0]);
        if (!a) {
            return NULL;
        }
        GsqlHv *b = eval(x, e->a[1]);
        if (!b) {
            rel(x, a);
            return NULL;
        }
        int t = compare(x, SQL_OP_EQ, a, b, e);
        rel(x, b);
        if (t == -2) {
            rel(x, a);
            return NULL;
        }
        if (t == 1) {
            rel(x, a);
            return hv_unknown(x);
        }
        return a;
    }
    /* the strict functions: NULL in, NULL out, as SQL's are */
    GsqlHv *args[2] = { NULL, NULL };
    int any_null = 0;
    for (size_t k = 0; k < e->na; k++) {
        args[k] = eval(x, e->a[k]);
        if (!args[k]) {
            for (size_t j = 0; j < k; j++) {
                rel(x, args[j]);
            }
            return NULL;
        }
        any_null |= is_null(x, args[k]);
    }
    GsqlHv *v;
    if (any_null) {
        v = hv_unknown(x);
    } else {
        const char *fn = e->name;
        if (strcmp(fn, "length") == 0 || strcmp(fn, "char_length") == 0) {
            fn = "len";
        }
        int want_text = strcmp(fn, "lower") == 0 || strcmp(fn, "upper") == 0 || strcmp(fn, "len") == 0 ||
                        strcmp(fn, "trim") == 0;
        int k0 = kind_of(x, args[0]);
        if (want_text && k0 != SER_STRING) {
            xfail(x->err, GSQL_EXPRESSION, e->pos, "%s works on text, and was given %s", e->name, tag_word(k0));
            v = NULL;
        } else if (!want_text && !arithmetic_ok(x, args[0], args[1], SQL_OP_ADD, e)) {
            v = NULL;
        } else {
            v = placed(x, x->h->call(x->h->ctx, fn, args, e->na, x->err), e);
        }
    }
    for (size_t k = 0; k < e->na; k++) {
        rel(x, args[k]);
    }
    return v;
}

static GsqlHv *eval_subquery(X *x, SqlExpr *e);

static GsqlHv *eval(X *x, SqlExpr *e) {
    switch (e->kind) {
    case SX_OUTER:
        return column(x, e);
    case SX_SUBQ:
        return eval_subquery(x, e);
    case SX_LIT:
        if (!e->hv) {
            e->hv = placed(x, hv_bytes(x, e->lit, e->litlen), e);
            if (!e->hv) {
                return NULL;
            }
            remember(x, e);
        }
        return x->h->copy(x->h->ctx, e->hv);
    case SX_PARAM:
        if (!e->hv) {
            const unsigned char *v;
            size_t n;
            if (param_bytes(x, e, &v, &n) != GSQL_OK) {
                return NULL;
            }
            e->hv = placed(x, hv_bytes(x, v, n), e);
            if (!e->hv) {
                return NULL;
            }
            remember(x, e);
        }
        return x->h->copy(x->h->ctx, e->hv);
    case SX_COL:
        return column(x, e);
    case SX_NAME:
        xfail(x->err, GSQL_MISUSE, e->pos, "an unbound name reached the executor");
        return NULL;
    case SX_BIN:
        return eval_bin(x, e);
    case SX_NOT: {
        GsqlHv *a = eval(x, e->a[0]);
        if (!a) {
            return NULL;
        }
        GsqlHv *v = NULL;
        if (logical(x, a, "NOT", e) != -2) {
            v = placed(x, x->h->op(x->h->ctx, GSQL_HOP_NOT, a, NULL, x->err), e);
        }
        rel(x, a);
        return v;
    }
    case SX_NEG: {
        GsqlHv *a = eval(x, e->a[0]);
        if (!a) {
            return NULL;
        }
        GsqlHv *v = NULL;
        if (arithmetic_ok(x, a, NULL, 0, e)) {
            v = placed(x, x->h->op(x->h->ctx, GSQL_HOP_NEG, a, NULL, x->err), e);
        }
        rel(x, a);
        return v;
    }
    case SX_ISNULL: {
        GsqlHv *a = eval(x, e->a[0]);
        if (!a) {
            return NULL;
        }
        int n = is_null(x, a);
        rel(x, a);
        return hv_bool(x, e->neg ? !n : n);
    }
    case SX_IN: {
        GsqlHv *a = eval(x, e->a[0]);
        if (!a) {
            return NULL;
        }
        int acc = 0;   /* false, until something is equal or unknown */
        for (size_t k = 1; k < e->na && acc != 1; k++) {
            GsqlHv *b = eval(x, e->a[k]);
            if (!b) {
                rel(x, a);
                return NULL;
            }
            int t = compare(x, SQL_OP_EQ, a, b, e);
            rel(x, b);
            if (t == -2) {
                rel(x, a);
                return NULL;
            }
            if (t == 1 || (t == -1 && acc == 0)) {
                acc = t;
            }
        }
        rel(x, a);
        if (e->neg && acc >= 0) {
            acc = !acc;
        }
        return hv_tri(x, acc);
    }
    case SX_BETWEEN: {
        GsqlHv *a = eval(x, e->a[0]);
        GsqlHv *lo = a ? eval(x, e->a[1]) : NULL;
        GsqlHv *hi = lo ? eval(x, e->a[2]) : NULL;
        int t = -2;
        if (hi) {
            int ge = compare(x, SQL_OP_GE, a, lo, e);
            int le = ge == -2 ? -2 : compare(x, SQL_OP_LE, a, hi, e);
            if (le != -2) {
                t = kleene_and(ge, le);
                if (e->neg && t >= 0) {
                    t = !t;
                }
            }
        }
        rel(x, a);
        rel(x, lo);
        rel(x, hi);
        return t == -2 ? NULL : hv_tri(x, t);
    }
    case SX_LIKE: {
        GsqlHv *a = eval(x, e->a[0]);
        GsqlHv *pat = a ? eval(x, e->a[1]) : NULL;
        GsqlHv *v = NULL;
        if (pat) {
            int ka = kind_of(x, a), kp = kind_of(x, pat);
            if (null_kind(ka) || null_kind(kp)) {
                v = hv_unknown(x);
            } else if (ka != SER_STRING || kp != SER_STRING) {
                xfail(x->err, GSQL_EXPRESSION, e->pos, "LIKE matches text, and was given %s",
                      tag_word(ka != SER_STRING ? ka : kp));
            } else {
                size_t sn = 0, pn = 0;
                char *s = text_of(x, a, &sn);
                char *p = s ? text_of(x, pat, &pn) : NULL;
                if (s && p) {
                    int m = like(s, sn, p, pn);
                    v = hv_bool(x, e->neg ? !m : m);
                }
                free(s);
                free(p);
            }
        }
        rel(x, a);
        rel(x, pat);
        return v;
    }
    case SX_CASE: {
        GsqlHv *operand = NULL;
        if (e->operand && !(operand = eval(x, e->operand))) {
            return NULL;
        }
        size_t pairs = (e->na - (e->has_else ? 1 : 0)) / 2;
        for (size_t k = 0; k < pairs; k++) {
            GsqlHv *w = eval(x, e->a[2 * k]);
            if (!w) {
                rel(x, operand);
                return NULL;
            }
            int t;
            if (operand) {
                t = compare(x, SQL_OP_EQ, operand, w, e->a[2 * k]);
            } else {
                t = logical(x, w, "WHEN", e->a[2 * k]);
            }
            rel(x, w);
            if (t == -2) {
                rel(x, operand);
                return NULL;
            }
            if (t == 1) {
                rel(x, operand);
                return eval(x, e->a[2 * k + 1]);
            }
        }
        rel(x, operand);
        return e->has_else ? eval(x, e->a[e->na - 1]) : hv_unknown(x);
    }
    case SX_CALL:
        return eval_call(x, e);
    case SX_AGG:
        if (!x->aggv || !x->aggv[e->index]) {
            xfail(x->err, GSQL_MISUSE, e->pos, "an aggregate was read outside its group");
            return NULL;
        }
        return x->h->copy(x->h->ctx, x->aggv[e->index]);
    case SX_CAST: {
        GsqlHv *a = eval(x, e->a[0]);
        if (!a) {
            return NULL;
        }
        GsqlHv *v = is_null(x, a) ? hv_unknown(x)
                                  : placed(x, x->h->cast(x->h->ctx, a, (int)e->type.kind, e->type.currency, x->err), e);
        rel(x, a);
        return v;
    }
    }
    return NULL;
}

/* An expression's value as v3 bytes, appended to `out`. */
static int eval_into(X *x, SqlExpr *e, GsqlBuf *out) {
    GsqlHv *v = eval(x, e);
    if (!v) {
        return x->err->code ? x->err->code : GSQL_EXPRESSION;
    }
    int rc = x->h->store(x->h->ctx, v, out, x->err);
    rel(x, v);
    return rc;
}

/* A scan's conditions conj[from..to): 1 all true, 0 one is not, -1 failed. */
static int check(X *x, const SqlScan *sc, size_t from, size_t to) {
    for (size_t k = from; k < to; k++) {
        GsqlHv *v = eval(x, sc->conj[k]);
        if (!v) {
            return -1;
        }
        int t = x->h->truth(x->h->ctx, v);
        int kind = kind_of(x, v);
        rel(x, v);
        if (t == -2) {
            xfail(x->err, GSQL_EXPRESSION, sc->conj[k]->pos, "%s needs a boolean, and this condition is %s",
                  k < sc->non ? "ON" : "WHERE", tag_word(kind));
            return -1;
        }
        if (t != 1) {
            return 0;
        }
    }
    return 1;
}

/* WHERE of a single-table statement: 1 keep the row, 0 drop it, -1 failed */
static int keep(X *x) {
    const SqlScan *sc = &x->p->scans[0];
    return check(x, sc, 0, sc->nconj);
}

/* LIMIT and OFFSET: a whole number, not negative */
static int count_value(X *x, SqlExpr *e, const char *what, uint64_t *out) {
    GsqlBuf b = {0};
    int rc = eval_into(x, e, &b);
    if (rc != GSQL_OK) {
        gsql_buf_free(&b);
        return rc;
    }
    int ok = b.length == 10 && b.bytes[0] == SER_NUMBER && b.bytes[1] == 1;
    uint64_t v = 0;
    if (ok) {
        for (int k = 0; k < 8; k++) {
            v |= (uint64_t)b.bytes[2 + k] << (8 * k);
        }
        ok = (int64_t)v >= 0;
    }
    if (b.length == 1 && b.bytes[0] == SER_UNKNOWN) {
        gsql_buf_free(&b);
        *out = UINT64_MAX;   /* LIMIT NULL is no limit, PostgreSQL's reading; OFFSET NULL is 0 below */
        return GSQL_OK;
    }
    gsql_buf_free(&b);
    if (!ok) {
        return xfail(x->err, GSQL_EXPRESSION, e->pos, "%s needs a whole number that is not negative", what);
    }
    *out = v;
    return GSQL_OK;
}

/* ---- access ------------------------------------------------------------- */

typedef struct {
    X *x;
    const SqlScan *sc;  /* the scan read */
    size_t k;           /* its position: which row of x it fills */
    GsCursor *cur;
    GsBytes tree;
    int index;          /* walking an index rather than the rows tree */
    int scan;           /* the whole tree, no bounds */
    int point;          /* one get on the rows tree */
    int backward;
    int started, done;
    int fell_back;      /* a bound did not fit its column: scanning instead */
    GsqlKey prefix;     /* the equality columns, key encoded */
    GsqlKey klo, khi;   /* the range, in key order (so swapped for DESC) */
    int has_lo, has_hi;
    unsigned range_flags;
    int derived;        /* a derived scan: its materialized rows, from dpos */
    size_t dpos;
    int hashing;        /* a hash scan's probe: the chain from hcur, or every row */
    int hall;
    size_t hcur;
} Iter;

static int derived_rows(X *x, size_t k);
static int hash_probe(Iter *it);

static unsigned flags_for(const SqlScan *p, size_t j) {
    const GsqlColumn *c = gsql_table_col(p->tb, p->keycols[j]);
    unsigned f = p->keydesc && p->keydesc[j] ? GSQL_KEY_DESC : 0;
    if (c->kind == GSQL_COL_MONEY && c->currency[0]) {
        f |= GSQL_KEY_MONEY_TYPED;
    }
    return f;
}

/* A bound's key component: 1 appended, 0 the bound does not fit the column
 * (so key order may not be `<`, §5), -1 failed */
static int bound_key(Iter *it, SqlExpr *e, size_t j, GsqlKey *key) {
    X *x = it->x;
    GsqlBuf b = {0};
    if (eval_into(x, e, &b) != GSQL_OK) {
        gsql_buf_free(&b);
        return -1;
    }
    GsqlColumn c = *gsql_table_col(it->sc->tb, it->sc->keycols[j]);
    c.not_null = 0;
    GsqlError ignored = { GSQL_OK, "", 0, 0 };
    int fits = b.bytes[0] != SER_NULL && gsql_value_fits(&c, b.bytes, b.length, &ignored) == GSQL_OK;
    if (fits) {
        fits = gsql_key_append(key, b.bytes, b.length, NULL, flags_for(it->sc, j)) == GSQL_KEY_OK;
    }
    gsql_buf_free(&b);
    return fits;
}

static int iter_open(Iter *it, X *x, size_t k) {
    memset(it, 0, sizeof *it);
    it->x = x;
    it->k = k;
    it->sc = &x->p->scans[k];
    const SqlScan *p = it->sc;
    gsql_key_init(&it->prefix);
    gsql_key_init(&it->klo);
    gsql_key_init(&it->khi);
    if (p->hash) {
        return hash_probe(it);
    }
    if (p->access == ACC_DERIVED) {
        it->derived = 1;
        return x->sx[k].dready ? GSQL_OK : derived_rows(x, k);
    }
    it->backward = p->backward;
    it->index = p->access == ACC_INDEX;
    it->scan = p->access == ACC_SCAN;
    if (!it->scan) {
        for (size_t j = 0; j < p->neq; j++) {
            int r = bound_key(it, p->eq[j], j, &it->prefix);
            if (r < 0) {
                return x->err->code;
            }
            if (r == 0) {
                it->fell_back = 1;
            }
        }
        if (!it->fell_back) {
            SqlExpr *lo = p->lo, *hi = p->hi;
            /* only a range has a column after the equalities: with equality on
             * every key column, keydesc[neq] is past the end */
            int desc = (lo || hi) && p->keydesc && p->keydesc[p->neq];
            if (lo) {
                int r = bound_key(it, lo, p->neq, desc ? &it->khi : &it->klo);
                if (r < 0) {
                    return x->err->code;
                }
                it->fell_back |= r == 0;
            }
            if (hi && !it->fell_back) {
                int r = bound_key(it, hi, p->neq, desc ? &it->klo : &it->khi);
                if (r < 0) {
                    return x->err->code;
                }
                it->fell_back |= r == 0;
            }
            it->has_lo = it->klo.length > 0;
            it->has_hi = it->khi.length > 0;
            it->range_flags = (lo || hi) ? flags_for(p, p->neq) : 0;
        }
        if (it->fell_back) {
            it->scan = 1;
            it->index = 0;
            it->backward = 0;
        } else {
            it->point = p->point && p->access == ACC_PK;
        }
    }
    if (it->scan || it->point) {
        x->q->scans += it->scan;
        x->q->seeks += it->point;
    } else {
        x->q->seeks++;
    }
    it->tree = it->index ? gsql_index_tree(p->tb, p->index) : gsql_table_rows_tree(p->tb);
    if (!it->point) {
        int rc = x->e->cursor_open(x->t, it->tree, &it->cur);
        if (rc) {
            it->cur = NULL;
            return xfail(x->err, GSQL_STORAGE, 0, "a cursor could not be opened: the engine answered %s",
                         gs_status_name(rc));
        }
    }
    return GSQL_OK;
}

static void iter_close(Iter *it) {
    if (it->cur) {
        it->x->e->cursor_close(it->cur);
        it->cur = NULL;
    }
    gsql_key_free(&it->prefix);
    gsql_key_free(&it->klo);
    gsql_key_free(&it->khi);
}

static int bytes_cmp(const unsigned char *a, size_t an, const unsigned char *b, size_t bn) {
    int c = memcmp(a, b, an < bn ? an : bn);
    if (c) {
        return c;
    }
    return an < bn ? -1 : an > bn;
}

/* the smallest key greater than every key beginning with `k`, or 0 if there
 * is none (k is all 0xFF) */
static int successor(const GsqlKey *k, GsqlBuf *out) {
    out->length = 0;
    v3_put(out, k->bytes, k->length);
    while (out->length && out->bytes[out->length - 1] == 0xFF) {
        out->length--;
    }
    if (!out->length) {
        return 0;
    }
    out->bytes[out->length - 1]++;
    return 1;
}

/* Positions on the next entry inside the range: 1 positioned, 0 done, -1
 * failed. */
static int iter_step(Iter *it) {
    X *x = it->x;
    int positioned = 0, rc;
    const GsEngine *e = x->e;
    if (!it->started) {
        it->started = 1;
        if (it->scan) {
            rc = it->backward ? e->last(it->cur, &positioned) : e->first(it->cur, &positioned);
        } else if (!it->backward) {
            GsqlBuf s = {0};
            v3_put(&s, it->prefix.bytes, it->prefix.length);
            if (it->has_lo) {
                v3_put(&s, it->klo.bytes, it->klo.length);
            }
            GsBytes k = { s.bytes, s.length };
            rc = s.length ? e->seek(it->cur, k, GS_SEEK_GE, &positioned) : e->first(it->cur, &positioned);
            gsql_buf_free(&s);
        } else {
            GsqlBuf s = {0};
            v3_put(&s, it->prefix.bytes, it->prefix.length);
            if (it->has_hi) {
                v3_put(&s, it->khi.bytes, it->khi.length);
            }
            GsqlKey whole = { s.bytes, s.length, s.capacity };
            GsqlBuf succ = {0};
            if (s.length && successor(&whole, &succ)) {
                GsBytes k = { succ.bytes, succ.length };
                rc = e->seek(it->cur, k, GS_SEEK_LT, &positioned);
            } else {
                rc = e->last(it->cur, &positioned);
            }
            gsql_buf_free(&succ);
            gsql_buf_free(&s);
        }
    } else {
        rc = it->backward ? e->prev(it->cur, &positioned) : e->next(it->cur, &positioned);
    }
    if (rc) {
        xfail(x->err, GSQL_STORAGE, 0, "a cursor failed: the engine answered %s", gs_status_name(rc));
        return -1;
    }
    if (!positioned) {
        return 0;
    }
    if (it->scan) {
        return 1;
    }
    GsBytes key;
    if ((rc = e->key(it->cur, &key)) != 0) {
        xfail(x->err, GSQL_STORAGE, 0, "a cursor failed: the engine answered %s", gs_status_name(rc));
        return -1;
    }
    if (key.n < it->prefix.length || memcmp(key.p, it->prefix.bytes, it->prefix.length) != 0) {
        return 0;
    }
    if (it->has_lo || it->has_hi) {
        size_t used;
        const unsigned char *comp = key.p + it->prefix.length;
        size_t room = key.n - it->prefix.length;
        if (gsql_key_skip(comp, room, it->range_flags, &used) != GSQL_KEY_OK) {
            xfail(x->err, GSQL_CORRUPT, 0, "a key of table %s does not parse", gsql_table_name(it->sc->tb));
            return -1;
        }
        if (!it->backward && it->has_hi && bytes_cmp(comp, used, it->khi.bytes, it->khi.length) > 0) {
            return 0;
        }
        if (it->backward && it->has_lo && bytes_cmp(comp, used, it->klo.bytes, it->klo.length) < 0) {
            return 0;
        }
    }
    return 1;
}

/* The next row the access yields, into its scan's row and key: 1, 0 at the
 * end, -1 failed. */
static int next_row(Iter *it) {
    X *x = it->x;
    XScan *xs = &x->sx[it->k];
    row_clear_scan(x, it->k);
    if (it->done) {
        return 0;
    }
    if (it->hashing && !it->hall) {
        if (!it->hcur) {
            it->done = 1;
            return 0;
        }
        const GsqlBuf *r = &xs->drows[it->hcur - 1];
        it->hcur = xs->hnext[it->hcur - 1];
        xs->row.length = 0;
        v3_put(&xs->row, r->bytes, r->length);
        xs->rowkey.length = 0;
        xs->have_row = 1;
        return 1;
    }
    if (it->derived || it->hashing) {
        if (it->dpos >= xs->ndrows) {
            it->done = 1;
            return 0;
        }
        const GsqlBuf *r = &xs->drows[it->dpos++];
        xs->row.length = 0;
        v3_put(&xs->row, r->bytes, r->length);
        xs->rowkey.length = 0;
        xs->have_row = 1;
        return 1;
    }
    const GsEngine *e = x->e;
    if (it->point) {
        it->done = 1;
        xs->rowkey.length = 0;
        v3_put(&xs->rowkey, it->prefix.bytes, it->prefix.length);
        GsBytes rk = { xs->rowkey.bytes, xs->rowkey.length };
        xs->row.length = 0;
        int rc = gsql_row_get(e, x->t, it->sc->tb, rk, &xs->row, x->err);
        if (rc == GSQL_NOT_FOUND) {
            x->err->code = GSQL_OK;
            x->err->message[0] = 0;
            return 0;
        }
        if (rc != GSQL_OK) {
            return -1;
        }
        xs->have_row = 1;
        return 1;
    }
    int r = iter_step(it);
    if (r <= 0) {
        it->done = 1;
        return r;
    }
    GsBytes key, val;
    int rc = e->key(it->cur, &key);
    if (!rc) {
        rc = e->value(it->cur, &val);
    }
    if (rc) {
        xfail(x->err, GSQL_STORAGE, 0, "a cursor failed: the engine answered %s", gs_status_name(rc));
        return -1;
    }
    if (it->index) {
        GsBytes rk;
        if (gsql_index_rowkey(it->sc->tb, it->sc->index, key, val, &rk, x->err) != GSQL_OK) {
            return -1;
        }
        xs->rowkey.length = 0;
        v3_put(&xs->rowkey, rk.p, rk.n);
        GsBytes rk2 = { xs->rowkey.bytes, xs->rowkey.length };
        xs->row.length = 0;
        if (gsql_row_get(e, x->t, it->sc->tb, rk2, &xs->row, x->err) != GSQL_OK) {
            if (x->err->code == GSQL_NOT_FOUND) {
                x->err->code = GSQL_CORRUPT;
            }
            return -1;
        }
    } else {
        xs->rowkey.length = 0;
        v3_put(&xs->rowkey, key.p, key.n);
        xs->row.length = 0;
        v3_put(&xs->row, val.p, val.n);
    }
    xs->have_row = 1;
    return 1;
}

/* The next row of a single scan that passes its conditions. */
static int next_kept(Iter *it) {
    for (;;) {
        int r = next_row(it);
        if (r <= 0) {
            return r;
        }
        int k = check(it->x, it->sc, 0, it->sc->nconj);
        if (k != 0) {
            return k;
        }
    }
}

/* ---- joins: nested loops in FROM order (docs/gsql_join_design.md §3) ----
 *
 * One iterator per scan. Scan k is opened afresh for each row of the scans
 * before it, so its access path's bounds -- which may read their columns --
 * are evaluated against that row. A LEFT join that finds no row its ON
 * accepts produces one null-extended row, checked against the WHERE
 * conditions placed there, as SQL requires. */
typedef struct {
    Iter *its;
    int *open;
    int *matched;
    size_t n;
    int yielded;
} Join;

static int join_open(Join *j, X *x, size_t k) {
    j->matched[k] = 0;
    x->sx[k].nulled = 0;
    int rc = iter_open(&j->its[k], x, k);
    j->open[k] = 1;
    return rc;
}

static void join_close(Join *j) {
    for (size_t k = 0; k < j->n; k++) {
        if (j->open[k]) {
            iter_close(&j->its[k]);
            j->open[k] = 0;
        }
    }
    free(j->its);
    free(j->open);
    free(j->matched);
    memset(j, 0, sizeof *j);
}

/* scan 0 open, nothing yielded */
static int join_start(Join *j, X *x) {
    j->n = x->p->nscans;
    j->its = calloc(j->n, sizeof(Iter));
    j->open = calloc(j->n, sizeof(int));
    j->matched = calloc(j->n, sizeof(int));
    if (!j->its || !j->open || !j->matched) {
        abort();
    }
    return join_open(j, x, 0);
}

/* the next accepted row of scan k: 1, 0 exhausted (and closed), -1 failed */
static int join_level(Join *j, X *x, size_t k) {
    const SqlScan *sc = &x->p->scans[k];
    for (;;) {
        if (x->sx[k].nulled) {
            x->sx[k].nulled = 0;
            return 0;
        }
        if (!j->open[k]) {
            return 0;
        }
        int r = next_row(&j->its[k]);
        if (r < 0) {
            return -1;
        }
        if (r == 1) {
            int t = check(x, sc, 0, sc->non);
            if (t < 0) {
                return -1;
            }
            if (t == 0) {
                continue;
            }
            j->matched[k] = 1;
            t = check(x, sc, sc->non, sc->nconj);
            if (t < 0) {
                return -1;
            }
            if (t == 0) {
                continue;
            }
            return 1;
        }
        iter_close(&j->its[k]);
        j->open[k] = 0;
        if (sc->join == SQL_JOIN_LEFT && !j->matched[k]) {
            j->matched[k] = 1;
            row_clear_scan(x, k);
            x->sx[k].nulled = 1;
            int t = check(x, sc, sc->non, sc->nconj);
            if (t < 0) {
                return -1;
            }
            if (t == 1) {
                return 1;
            }
            x->sx[k].nulled = 0;
        }
        return 0;
    }
}

/* the next joined row, every scan's row in hand: 1, 0 at the end, -1 failed */
static int join_next(Join *j, X *x) {
    size_t k = j->yielded ? j->n - 1 : 0;
    j->yielded = 1;
    for (;;) {
        int r = join_level(j, x, k);
        if (r < 0) {
            return -1;
        }
        if (r == 1) {
            if (k == j->n - 1) {
                return 1;
            }
            k++;
            if (join_open(j, x, k) != GSQL_OK) {
                return -1;
            }
            continue;
        }
        if (k == 0) {
            return 0;
        }
        k--;
    }
}

/* ---- aggregation (docs/gsql_aggregate_design.md) --------------------------
 *
 * A grouped query's rows are folded into GROUPS before anything is output: each
 * row's GROUP BY values are encoded as one composite INDEX KEY, so two rows are
 * in one group exactly when a unique index would call them one value (all
 * NULLs together, 3 with 3.0). A group keeps the FIRST ROW it saw, so the
 * select list, HAVING and ORDER BY read its group values back through the
 * ordinary column path; each aggregate folds through the host, so a sum is
 * gBASIC's `+` and an extreme is gBASIC's order. */

static uint64_t key_hash(const unsigned char *b, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t k = 0; k < n; k++) {
        h = (h ^ b[k]) * 1099511628211ull;
    }
    return h;
}

typedef struct {
    uint64_t count;      /* rows (count(*)) or values (the rest) folded */
    GsqlHv *v;           /* sum, min or max so far */
    KeyMap seen;         /* DISTINCT: the keys already folded */
    GsqlBuf *seen_keys;
    size_t nseen, capseen;
} Acc;

typedef struct {
    GsqlBuf key;
    GsqlBuf *reps;       /* the first row's bytes, one per scan */
    unsigned char *state;/* per scan: 0 no row, 1 a row, 2 null-extended */
    Acc *acc;
    GsqlHv **fin;        /* the aggregates' final values */
} Group;

typedef struct {
    Group *v;
    size_t n, cap;
    KeyMap map;
    size_t *order;       /* the groups in key order */
    size_t pos;
} Groups;

/* the slot (1-based) of `key` among `keys`, or 0; `insert` adds it as `n+1` */
static size_t keymap_find(KeyMap *m, const GsqlBuf *keys_of(void *, size_t), void *ctx, const unsigned char *b,
                          size_t len, size_t insert_as) {
    if (m->n * 2 >= m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 64;
        size_t *grown = calloc(cap, sizeof *grown);
        if (!grown) {
            abort();
        }
        for (size_t k = 0; k < m->cap; k++) {
            if (m->slot[k]) {
                const GsqlBuf *kb = keys_of(ctx, m->slot[k] - 1);
                size_t at = key_hash(kb->bytes, kb->length) & (cap - 1);
                while (grown[at]) {
                    at = (at + 1) & (cap - 1);
                }
                grown[at] = m->slot[k];
            }
        }
        free(m->slot);
        m->slot = grown;
        m->cap = cap;
    }
    size_t at = key_hash(b, len) & (m->cap - 1);
    while (m->slot[at]) {
        const GsqlBuf *kb = keys_of(ctx, m->slot[at] - 1);
        if (kb->length == len && memcmp(kb->bytes, b, len) == 0) {
            return m->slot[at];
        }
        at = (at + 1) & (m->cap - 1);
    }
    if (insert_as) {
        m->slot[at] = insert_as;
        m->n++;
    }
    return 0;
}

/* ---- hash joins (docs/gsql_hashjoin_design.md) ---------------------------- */

static const GsqlBuf *hash_key_of(void *ctx, size_t i) {
    return &((XScan *)ctx)->hkeys[i];
}

/* the bytes of column `col` of a held row of scan k */
static int held_value(X *x, size_t k, const GsqlBuf *row, GsBytes *v) {
    const SqlScan *sc = &x->p->scans[k];
    if (sc->derived) {
        size_t n = x->sx[k].ncols;
        GsBytes *items = malloc(sizeof(GsBytes) * (n ? n : 1));
        if (!items) {
            abort();
        }
        long got = v3_array_items(row->bytes, row->length, items, (long)n);
        *v = items[sc->hashcol];
        free(items);
        return got == (long)n ? GSQL_OK : xfail(x->err, GSQL_MISUSE, 0, "a derived table's row did not hold its columns");
    }
    return gsql_row_value(sc->tb, row->bytes, row->length, sc->hashcol, v, x->err);
}

/* Every row of scan k, read once, grouped by the hashed column's key. A
 * table's rows are read through an ordinary whole scan; a derived table's are
 * already held. A NULL is put in no group, since `=` is never true of one. */
static int hash_build(X *x, size_t k) {
    XScan *xs = &x->sx[k];
    const SqlScan *sc = &x->p->scans[k];
    int rc = GSQL_OK;
    if (sc->derived) {
        if (!xs->dready && (rc = derived_rows(x, k)) != GSQL_OK) {
            return rc;
        }
    } else {
        Iter it;
        memset(&it, 0, sizeof it);
        it.x = x;
        it.k = k;
        it.sc = sc;
        it.scan = 1;
        gsql_key_init(&it.prefix);
        gsql_key_init(&it.klo);
        gsql_key_init(&it.khi);
        x->q->scans++;
        it.tree = gsql_table_rows_tree(sc->tb);
        int erc = x->e->cursor_open(x->t, it.tree, &it.cur);
        if (erc) {
            it.cur = NULL;
            iter_close(&it);
            return xfail(x->err, GSQL_STORAGE, 0, "a cursor could not be opened: the engine answered %s",
                         gs_status_name(erc));
        }
        size_t cap = 0;
        int r;
        while ((r = next_row(&it)) == 1) {
            if (xs->ndrows == cap) {
                cap = cap ? cap * 2 : 64;
                xs->drows = realloc(xs->drows, sizeof(GsqlBuf) * cap);
                if (!xs->drows) {
                    abort();
                }
            }
            memset(&xs->drows[xs->ndrows], 0, sizeof(GsqlBuf));
            v3_put(&xs->drows[xs->ndrows], xs->row.bytes, xs->row.length);
            xs->ndrows++;
        }
        iter_close(&it);
        row_clear_scan(x, k);
        if (r < 0) {
            return x->err->code ? x->err->code : GSQL_STORAGE;
        }
        xs->dready = 1;
    }
    xs->hnext = calloc(xs->ndrows ? xs->ndrows : 1, sizeof(size_t));
    if (!xs->hnext) {
        abort();
    }
    size_t capkeys = 0;
    for (size_t i = 0; i < xs->ndrows; i++) {
        GsBytes v;
        if ((rc = held_value(x, k, &xs->drows[i], &v)) != GSQL_OK) {
            return rc;
        }
        if (v.n && (v.p[0] == SER_UNKNOWN || v.p[0] == SER_NULL)) {
            continue;
        }
        GsqlKey key;
        gsql_key_init(&key);
        if (gsql_key_append(&key, v.p, v.n, NULL, 0) != GSQL_KEY_OK) {
            /* a value with no key, a record: `=` is true of it only against
               another such value, whose probe reads every row anyway */
            gsql_key_free(&key);
            continue;
        }
        if (xs->nhkeys == capkeys) {
            capkeys = capkeys ? capkeys * 2 : 64;
            xs->hkeys = realloc(xs->hkeys, sizeof(GsqlBuf) * capkeys);
            xs->hfirst = realloc(xs->hfirst, sizeof(size_t) * capkeys);
            xs->hlast = realloc(xs->hlast, sizeof(size_t) * capkeys);
            if (!xs->hkeys || !xs->hfirst || !xs->hlast) {
                abort();
            }
        }
        /* the next group's key is put in place first, so the map may read it */
        memset(&xs->hkeys[xs->nhkeys], 0, sizeof(GsqlBuf));
        v3_put(&xs->hkeys[xs->nhkeys], key.bytes, key.length);
        size_t g = keymap_find(&xs->hmap, hash_key_of, xs, key.bytes, key.length, xs->nhkeys + 1);
        gsql_key_free(&key);
        if (!g) {
            g = ++xs->nhkeys;
            xs->hfirst[g - 1] = i + 1;
        } else {
            gsql_buf_free(&xs->hkeys[xs->nhkeys]);
            xs->hnext[xs->hlast[g - 1] - 1] = i + 1;
        }
        xs->hlast[g - 1] = i + 1;
    }
    xs->hready = 1;
    return GSQL_OK;
}

/* One opening of a hash scan: the probe's key, evaluated against the outer
 * row, picks the chain of rows whose hashed column has that key. */
static int hash_probe(Iter *it) {
    X *x = it->x;
    XScan *xs = &x->sx[it->k];
    it->hashing = 1;
    if (!xs->hready) {
        int rc = hash_build(x, it->k);
        if (rc != GSQL_OK) {
            return rc;
        }
    }
    x->q->seeks++;
    GsqlHv *v = eval(x, it->sc->hashexpr);
    if (!v) {
        return x->err->code ? x->err->code : GSQL_EXPRESSION;
    }
    if (is_null(x, v)) {
        rel(x, v);
        it->done = 1;
        return GSQL_OK;
    }
    GsqlBuf b = {0};
    int rc = x->h->store(x->h->ctx, v, &b, x->err);
    rel(x, v);
    if (rc != GSQL_OK) {
        gsql_buf_free(&b);
        return rc;
    }
    GsqlKey key;
    gsql_key_init(&key);
    if (gsql_key_append(&key, b.bytes, b.length, NULL, 0) != GSQL_KEY_OK) {
        it->hall = 1;   /* a probe with no key reads every row */
    } else {
        size_t g = xs->hmap.cap ? keymap_find(&xs->hmap, hash_key_of, xs, key.bytes, key.length, 0) : 0;
        it->hcur = g ? xs->hfirst[g - 1] : 0;
        if (!g) {
            it->done = 1;
        }
    }
    gsql_key_free(&key);
    gsql_buf_free(&b);
    return GSQL_OK;
}

static const GsqlBuf *group_key_of(void *ctx, size_t i) {
    return &((Groups *)ctx)->v[i].key;
}

static const GsqlBuf *seen_key_of(void *ctx, size_t i) {
    return &((Acc *)ctx)->seen_keys[i];
}

/* a value's key component, appended; GSQL_OK or a refusal at `e` */
static int key_of_value(X *x, const GsqlHv *v, const SqlExpr *e, GsqlKey *key) {
    GsqlBuf b = {0};
    int rc = x->h->store(x->h->ctx, v, &b, x->err);
    if (rc == GSQL_OK) {
        GsqlKeyStatus st = gsql_key_append(key, b.bytes, b.length, NULL, 0);
        if (st != GSQL_KEY_OK) {
            rc = xfail(x->err, GSQL_EXPRESSION, e->pos,
                       "%s cannot be grouped or made DISTINCT: it has no single equality G-SQL can key",
                       tag_word(b.length ? b.bytes[0] : 0));
        }
    }
    gsql_buf_free(&b);
    return rc;
}

static void groups_free(X *x, Groups *g) {
    for (size_t i = 0; i < g->n; i++) {
        Group *gr = &g->v[i];
        gsql_buf_free(&gr->key);
        for (size_t k = 0; gr->reps && k < x->nsx; k++) {
            gsql_buf_free(&gr->reps[k]);
        }
        free(gr->reps);
        free(gr->state);
        for (size_t a = 0; a < x->p->naggs; a++) {
            Acc *acc = &gr->acc[a];
            rel(x, acc->v);
            for (size_t k = 0; k < acc->nseen; k++) {
                gsql_buf_free(&acc->seen_keys[k]);
            }
            free(acc->seen_keys);
            free(acc->seen.slot);
            if (gr->fin) {
                rel(x, gr->fin[a]);
            }
        }
        free(gr->acc);
        free(gr->fin);
    }
    free(g->v);
    free(g->map.slot);
    free(g->order);
    memset(g, 0, sizeof *g);
}

/* Folds the current row into aggregate `a` of group `gr`. */
static int fold(X *x, Acc *acc, SqlExpr *e) {
    if (e->op == SQL_AGG_COUNT_STAR) {
        acc->count++;
        return GSQL_OK;
    }
    GsqlHv *v = eval(x, e->a[0]);
    if (!v) {
        return x->err->code ? x->err->code : GSQL_EXPRESSION;
    }
    if (is_null(x, v)) {
        rel(x, v);
        return GSQL_OK;
    }
    if (e->distinct) {
        GsqlKey key;
        gsql_key_init(&key);
        if (key_of_value(x, v, e, &key) != GSQL_OK) {
            gsql_key_free(&key);
            rel(x, v);
            return x->err->code;
        }
        if (keymap_find(&acc->seen, seen_key_of, acc, key.bytes, key.length, 0)) {
            gsql_key_free(&key);
            rel(x, v);
            return GSQL_OK;
        }
        if (acc->nseen == acc->capseen) {
            acc->capseen = acc->capseen ? acc->capseen * 2 : 16;
            acc->seen_keys = realloc(acc->seen_keys, sizeof(GsqlBuf) * acc->capseen);
            if (!acc->seen_keys) {
                abort();
            }
        }
        GsqlBuf *kb = &acc->seen_keys[acc->nseen];
        memset(kb, 0, sizeof *kb);
        v3_put(kb, key.bytes, key.length);
        acc->nseen++;
        keymap_find(&acc->seen, seen_key_of, acc, key.bytes, key.length, acc->nseen);
        gsql_key_free(&key);
    }
    acc->count++;
    if (e->op == SQL_AGG_COUNT) {
        rel(x, v);
        return GSQL_OK;
    }
    if (!acc->v) {
        if ((e->op == SQL_AGG_SUM || e->op == SQL_AGG_AVG) && !arithmetic_ok(x, v, NULL, SQL_OP_ADD, e)) {
            rel(x, v);
            return x->err->code;
        }
        acc->v = v;
        return GSQL_OK;
    }
    if (e->op == SQL_AGG_SUM || e->op == SQL_AGG_AVG) {
        if (!arithmetic_ok(x, acc->v, v, SQL_OP_ADD, e)) {
            rel(x, v);
            return x->err->code;
        }
        GsqlHv *sum = placed(x, x->h->op(x->h->ctx, SQL_OP_ADD, acc->v, v, x->err), e);
        rel(x, v);
        if (!sum) {
            return x->err->code;
        }
        rel(x, acc->v);
        acc->v = sum;
        return GSQL_OK;
    }
    /* min and max: gBASIC's order, with the kinds checked as `<` checks them */
    if (!comparable(x, acc->v, v, SQL_OP_LT, e)) {
        rel(x, v);
        return x->err->code;
    }
    int cmp;
    if (x->h->order(x->h->ctx, v, acc->v, &cmp, x->err) != GSQL_OK) {
        if (x->err->position == 0) {
            x->err->position = e->pos;
        }
        rel(x, v);
        return x->err->code;
    }
    if ((e->op == SQL_AGG_MIN && cmp < 0) || (e->op == SQL_AGG_MAX && cmp > 0)) {
        rel(x, acc->v);
        acc->v = v;
    } else {
        rel(x, v);
    }
    return GSQL_OK;
}

static GsqlHv *hv_count(X *x, uint64_t n) {
    GsqlBuf b = {0};
    v3_w_int(&b, (int64_t)n);
    GsqlHv *v = hv_bytes(x, b.bytes, b.length);
    gsql_buf_free(&b);
    return v;
}

/* an aggregate's answer once its group is complete */
static GsqlHv *finish(X *x, Acc *acc, SqlExpr *e) {
    if (e->op == SQL_AGG_COUNT_STAR || e->op == SQL_AGG_COUNT) {
        return hv_count(x, acc->count);
    }
    if (!acc->v) {
        return hv_unknown(x);
    }
    if (e->op == SQL_AGG_AVG) {
        GsqlHv *n = hv_count(x, acc->count);
        if (!n) {
            return NULL;
        }
        GsqlHv *v = placed(x, x->h->op(x->h->ctx, SQL_OP_DIV, acc->v, n, x->err), e);
        rel(x, n);
        return v;
    }
    return x->h->copy(x->h->ctx, acc->v);
}

static Group *group_new(X *x, Groups *g) {
    if (g->n == g->cap) {
        g->cap = g->cap ? g->cap * 2 : 16;
        g->v = realloc(g->v, sizeof(Group) * g->cap);
        if (!g->v) {
            abort();
        }
    }
    Group *gr = &g->v[g->n++];
    memset(gr, 0, sizeof *gr);
    gr->acc = calloc(x->p->naggs ? x->p->naggs : 1, sizeof(Acc));
    gr->reps = calloc(x->nsx, sizeof(GsqlBuf));
    gr->state = calloc(x->nsx, 1);
    if (!gr->acc || !gr->reps || !gr->state) {
        abort();
    }
    return gr;
}

static Groups *sort_ctx;

static int group_order_cmp(const void *a, const void *b) {
    const GsqlBuf *ka = &sort_ctx->v[*(const size_t *)a].key, *kb = &sort_ctx->v[*(const size_t *)b].key;
    size_t n = ka->length < kb->length ? ka->length : kb->length;
    int c = memcmp(ka->bytes, kb->bytes, n);
    if (c) {
        return c;
    }
    return ka->length < kb->length ? -1 : ka->length > kb->length;
}

/* ---- SELECT ------------------------------------------------------------- */

typedef struct {
    GsqlBuf out;        /* one v3 array: the output row */
    GsqlHv **keys;      /* ORDER BY's values */
} SortRow;

typedef struct {
    GsqlSql *q;
    SqlParsed parsed;   /* the cursor owns its statement */
    char *text;
    SqlPlan plan;
    X x;
    GsqlError err;
    Join j;
    int join_started;
    int sorted;
    /* BACKUP TO (docs/gsql_backup_design.md): one slice copied per fetch, the
       owner serving other clients between them, the cursor's own snapshot
       what is copied */
    GsBackup *backup;
    int backup_done;
    SortRow *rows;
    size_t nrows, pos;
    uint64_t offset, limit, produced;
    int explain;
    char **lines;
    size_t nlines;
    GsqlBuf pending;    /* a row read ahead, to know whether more remain */
    int have_pending;
    int ended;
    int grouped;        /* the rows are the groups (aggregate, below) */
    Groups groups;
    int borrowed;       /* a compound's branch: the plan, text and parse are its parent's */
    char **wseen;       /* the warnings this statement has said, once each */
    size_t nwseen;
    KeyMap seen;        /* SELECT DISTINCT: the output rows' keys so far */
    GsqlBuf *seen_keys;
    size_t nseen, capseen;
} SqlCursor;

static const GsqlBuf *row_key_of(void *ctx, size_t i) {
    return &((SqlCursor *)ctx)->seen_keys[i];
}

/* SELECT DISTINCT (docs/gsql_distinct_design.md): whether output row `row`
 * is the first of its kind, keyed as GROUP BY keys a group. 1 it is, 0 a row
 * the same as it came before, -1 failed. */
static int distinct_first(SqlCursor *c, const GsqlBuf *row) {
    if (!c->plan.s->distinct) {
        return 1;
    }
    X *x = &c->x;
    size_t n = c->plan.nout;
    GsBytes *items = malloc(sizeof(GsBytes) * (n ? n : 1));
    if (!items) {
        abort();
    }
    if (v3_array_items(row->bytes, row->length, items, n) != (long)n) {
        free(items);
        xfail(x->err, GSQL_MISUSE, 0, "an output row did not hold its columns");
        return -1;
    }
    GsqlKey key;
    gsql_key_init(&key);
    for (size_t k = 0; k < n; k++) {
        if (gsql_key_append(&key, items[k].p, items[k].n, NULL, 0) != GSQL_KEY_OK) {
            xfail(x->err, GSQL_EXPRESSION, c->plan.out[k]->pos,
                  "%s cannot be grouped or made DISTINCT: it has no single equality G-SQL can key",
                  tag_word(items[k].n ? items[k].p[0] : 0));
            gsql_key_free(&key);
            free(items);
            return -1;
        }
    }
    free(items);
    int first = !keymap_find(&c->seen, row_key_of, c, key.bytes, key.length, 0);
    if (first) {
        if (c->nseen == c->capseen) {
            c->capseen = c->capseen ? c->capseen * 2 : 64;
            c->seen_keys = realloc(c->seen_keys, sizeof(GsqlBuf) * c->capseen);
            if (!c->seen_keys) {
                abort();
            }
        }
        GsqlBuf *kb = &c->seen_keys[c->nseen];
        memset(kb, 0, sizeof *kb);
        v3_put(kb, key.bytes, key.length);
        c->nseen++;
        keymap_find(&c->seen, row_key_of, c, key.bytes, key.length, c->nseen);
    }
    gsql_key_free(&key);
    return first;
}

static int output_row(SqlCursor *c, GsqlBuf *out) {
    X *x = &c->x;
    out->length = 0;
    v3_w_array(out, c->plan.nout);
    for (size_t k = 0; k < c->plan.nout; k++) {
        if (eval_into(x, c->plan.out[k], out) != GSQL_OK) {
            return x->err->code;
        }
    }
    return GSQL_OK;
}

/* Folds every row the access path keeps into groups (docs/gsql_aggregate_design.md §5). */
static int aggregate(SqlCursor *c) {
    X *x = &c->x;
    SqlPlan *p = &c->plan;
    Groups *g = &c->groups;
    int one = p->scans[0].access == ACC_NONE;   /* no FROM: the one row, if kept */
    for (int first = 1;; first = 0) {
        int r;
        if (one) {
            r = first ? keep(x) : 0;
        } else {
            r = join_next(&c->j, x);
        }
        if (r < 0) {
            return x->err->code ? x->err->code : GSQL_EXPRESSION;
        }
        if (r == 0) {
            break;
        }
        GsqlKey key;
        gsql_key_init(&key);
        for (size_t k = 0; k < p->s->ngroup; k++) {
            GsqlHv *v = eval(x, p->s->group[k]);
            int rc = v ? key_of_value(x, v, p->s->group[k], &key) : (x->err->code ? x->err->code : GSQL_EXPRESSION);
            rel(x, v);
            if (rc != GSQL_OK) {
                gsql_key_free(&key);
                return rc;
            }
        }
        size_t at = keymap_find(&g->map, group_key_of, g, key.bytes, key.length, 0);
        Group *gr;
        if (at) {
            gr = &g->v[at - 1];
        } else {
            gr = group_new(x, g);
            v3_put(&gr->key, key.bytes, key.length);
            for (size_t k = 0; k < x->nsx; k++) {
                XScan *xs = &x->sx[k];
                if (xs->nulled) {
                    gr->state[k] = 2;
                } else if (xs->have_row) {
                    v3_put(&gr->reps[k], xs->row.bytes, xs->row.length);
                    gr->state[k] = 1;
                }
            }
            keymap_find(&g->map, group_key_of, g, key.bytes, key.length, g->n);
        }
        gsql_key_free(&key);
        for (size_t a = 0; a < p->naggs; a++) {
            if (fold(x, &gr->acc[a], p->aggs[a]) != GSQL_OK) {
                return x->err->code;
            }
        }
    }
    /* no GROUP BY: the input is one group, even when it is empty */
    if (p->s->ngroup == 0 && g->n == 0) {
        group_new(x, g);
    }
    for (size_t i = 0; i < g->n; i++) {
        Group *gr = &g->v[i];
        gr->fin = calloc(p->naggs ? p->naggs : 1, sizeof(GsqlHv *));
        if (!gr->fin) {
            abort();
        }
        for (size_t a = 0; a < p->naggs; a++) {
            if (!(gr->fin[a] = finish(x, &gr->acc[a], p->aggs[a]))) {
                return x->err->code ? x->err->code : GSQL_EXPRESSION;
            }
        }
    }
    g->order = malloc(sizeof(size_t) * (g->n ? g->n : 1));
    if (!g->order) {
        abort();
    }
    for (size_t i = 0; i < g->n; i++) {
        g->order[i] = i;
    }
    sort_ctx = g;
    qsort(g->order, g->n, sizeof(size_t), group_order_cmp);
    sort_ctx = NULL;
    row_clear(x);
    return GSQL_OK;
}

/* the next group HAVING keeps, made the current row: 1, 0 at the end, -1 failed */
static int next_group(SqlCursor *c) {
    X *x = &c->x;
    Groups *g = &c->groups;
    while (g->pos < g->n) {
        Group *gr = &g->v[g->order[g->pos++]];
        row_clear(x);
        for (size_t k = 0; k < x->nsx; k++) {
            XScan *xs = &x->sx[k];
            if (gr->state[k] == 1) {
                xs->row.length = 0;
                v3_put(&xs->row, gr->reps[k].bytes, gr->reps[k].length);
                xs->have_row = 1;
            } else if (gr->state[k] == 2) {
                xs->nulled = 1;
            }
        }
        x->aggv = gr->fin;
        if (!c->plan.s->having) {
            return 1;
        }
        GsqlHv *v = eval(x, c->plan.s->having);
        if (!v) {
            return -1;
        }
        int t = x->h->truth(x->h->ctx, v);
        int kind = kind_of(x, v);
        rel(x, v);
        if (t == -2) {
            xfail(x->err, GSQL_EXPRESSION, c->plan.s->having->pos, "HAVING needs a boolean, and this condition is %s",
                  tag_word(kind));
            return -1;
        }
        if (t == 1) {
            return 1;
        }
    }
    return 0;
}

static int source_next(SqlCursor *c) {
    return c->grouped ? next_group(c) : join_next(&c->j, &c->x);
}

static int sort_cmp(SqlCursor *c, const SortRow *a, const SortRow *b, int *failed) {
    X *x = &c->x;
    for (size_t k = 0; k < c->plan.s->norder; k++) {
        GsqlHv *va = a->keys[k], *vb = b->keys[k];
        int na = is_null(x, va), nb = is_null(x, vb);
        int cmp;
        if (na || nb) {
            cmp = na == nb ? 0 : na ? 1 : -1;   /* NULL after every value */
        } else {
            if (!comparable(x, va, vb, SQL_OP_LT, c->plan.order[k])) {
                *failed = 1;
                return 0;
            }
            if (x->h->order(x->h->ctx, va, vb, &cmp, x->err) != GSQL_OK) {
                if (x->err->position == 0) {
                    x->err->position = c->plan.order[k]->pos;
                }
                *failed = 1;
                return 0;
            }
        }
        if (cmp) {
            return c->plan.s->order[k].desc ? -cmp : cmp;
        }
    }
    return 0;
}

/* a stable merge sort that can stop on a failed comparison */
static int merge_sort(SqlCursor *c, SortRow *v, size_t n, SortRow *tmp) {
    if (n < 2) {
        return 0;
    }
    size_t m = n / 2;
    int failed = 0;
    if (merge_sort(c, v, m, tmp) || merge_sort(c, v + m, n - m, tmp)) {
        return 1;
    }
    size_t i = 0, j = m, k = 0;
    while (i < m && j < n) {
        int cmp = sort_cmp(c, &v[j], &v[i], &failed);
        if (failed) {
            return 1;
        }
        tmp[k++] = cmp < 0 ? v[j++] : v[i++];
    }
    while (i < m) {
        tmp[k++] = v[i++];
    }
    while (j < n) {
        tmp[k++] = v[j++];
    }
    memcpy(v, tmp, sizeof(SortRow) * n);
    return 0;
}

static void cursor_free(SqlCursor *c) {
    if (!c) {
        return;
    }
    if (c->backup) {
        c->x.e->backup_end(c->backup);   /* a copy not finished has no manifest */
    }
    for (size_t k = 0; k < c->nrows; k++) {
        gsql_buf_free(&c->rows[k].out);
        if (c->rows[k].keys) {
            for (size_t j = 0; j < c->plan.s->norder; j++) {
                rel(&c->x, c->rows[k].keys[j]);
            }
            free(c->rows[k].keys);
        }
    }
    free(c->rows);
    for (size_t k = 0; k < c->nseen; k++) {
        gsql_buf_free(&c->seen_keys[k]);
    }
    free(c->seen_keys);
    free(c->seen.slot);
    c->x.aggv = NULL;
    groups_free(&c->x, &c->groups);
    for (size_t k = 0; k < c->nlines; k++) {
        free(c->lines[k]);
    }
    free(c->lines);
    if (c->join_started) {
        join_close(&c->j);
    }
    x_free(&c->x);
    if (!c->borrowed) {
        sql_plan_free(&c->plan);
        sql_parsed_free(&c->parsed);
        free(c->text);
    }
    gsql_buf_free(&c->pending);
    for (size_t k = 0; k < c->nwseen; k++) {
        free(c->wseen[k]);
    }
    free(c->wseen);
    free(c);
}

/* the next output row: 1, 0 at the end, -1 failed */
static int cursor_next(SqlCursor *c, GsqlBuf *out) {
    if (c->have_pending) {
        c->have_pending = 0;
        out->length = 0;
        v3_put(out, c->pending.bytes, c->pending.length);
        return 1;
    }
    if (c->ended) {
        return 0;
    }
    if (c->explain) {
        if (c->pos >= c->nlines) {
            c->ended = 1;
            return 0;
        }
        out->length = 0;
        v3_w_array(out, 1);
        v3_w_text(out, c->lines[c->pos++]);
        return 1;
    }
    if (c->produced >= c->limit) {
        c->ended = 1;
        return 0;
    }
    if (c->sorted) {
        if (c->pos >= c->nrows) {
            c->ended = 1;
            return 0;
        }
        out->length = 0;
        v3_put(out, c->rows[c->pos].out.bytes, c->rows[c->pos].out.length);
        c->pos++;
        c->produced++;
        return 1;
    }
    if (c->grouped) {
        for (;;) {
            int r = next_group(c);
            if (r <= 0) {
                c->ended = 1;
                return r;
            }
            if (output_row(c, out) != GSQL_OK) {
                return -1;
            }
            int f = distinct_first(c, out);
            if (f < 0) {
                return -1;
            }
            if (!f) {
                continue;
            }
            if (c->offset) {
                c->offset--;
                continue;
            }
            c->produced++;
            return 1;
        }
    }
    if (c->plan.scans[0].access == ACC_NONE) {
        /* one row, with no table */
        if (c->pos++) {
            c->ended = 1;
            return 0;
        }
        int k = keep(&c->x);
        if (k <= 0 || c->offset > 0) {
            c->ended = 1;
            return k < 0 ? -1 : 0;
        }
        if (output_row(c, out) != GSQL_OK) {
            return -1;
        }
        c->produced++;
        return 1;
    }
    for (;;) {
        int r = join_next(&c->j, &c->x);
        if (r <= 0) {
            c->ended = 1;
            return r;
        }
        if (c->plan.s->distinct) {
            if (output_row(c, out) != GSQL_OK) {
                return -1;
            }
            int f = distinct_first(c, out);
            if (f < 0) {
                return -1;
            }
            if (!f) {
                continue;
            }
        }
        if (c->offset) {
            c->offset--;
            continue;
        }
        if (!c->plan.s->distinct && output_row(c, out) != GSQL_OK) {
            return -1;
        }
        c->produced++;
        return 1;
    }
}

static int collect_sorted(SqlCursor *c) {
    X *x = &c->x;
    size_t cap = 0;
    size_t nord = c->plan.s->norder;
    for (;;) {
        int r = source_next(c);
        if (r < 0) {
            return x->err->code;
        }
        if (r == 0) {
            break;
        }
        if (c->nrows == cap) {
            cap = cap ? cap * 2 : 64;
            c->rows = realloc(c->rows, sizeof(SortRow) * cap);
            if (!c->rows) {
                abort();
            }
        }
        SortRow *row = &c->rows[c->nrows];
        memset(row, 0, sizeof *row);
        if (output_row(c, &row->out) != GSQL_OK) {
            gsql_buf_free(&row->out);
            return x->err->code;
        }
        int f = distinct_first(c, &row->out);
        if (f <= 0) {
            gsql_buf_free(&row->out);
            if (f < 0) {
                return x->err->code;
            }
            continue;
        }
        row->keys = calloc(nord, sizeof(GsqlHv *));
        if (!row->keys) {
            abort();
        }
        c->nrows++;
        for (size_t k = 0; k < nord; k++) {
            if (c->plan.order_out[k] >= 0) {
                /* the output's own value: read it back from the row */
                GsBytes items[1024];
                long n = v3_array_items(row->out.bytes, row->out.length, items, 1024);
                long at = c->plan.order_out[k];
                if (n <= at) {
                    return xfail(x->err, GSQL_MISUSE, 0, "an output column was not there to sort by");
                }
                row->keys[k] = hv_bytes(x, items[at].p, items[at].n);
            } else {
                row->keys[k] = eval(x, c->plan.order[k]);
            }
            if (!row->keys[k]) {
                return x->err->code;
            }
        }
    }
    SortRow *tmp = malloc(sizeof(SortRow) * (c->nrows ? c->nrows : 1));
    if (!tmp) {
        abort();
    }
    int failed = merge_sort(c, c->rows, c->nrows, tmp);
    free(tmp);
    if (failed) {
        return x->err->code ? x->err->code : GSQL_EXPRESSION;
    }
    c->pos = c->offset < c->nrows ? (size_t)c->offset : c->nrows;
    return GSQL_OK;
}

static void explain_line(void *ctx, const char *text) {
    SqlCursor *c = ctx;
    c->lines = realloc(c->lines, sizeof(char *) * (c->nlines + 1));
    if (!c->lines) {
        abort();
    }
    c->lines[c->nlines] = malloc(strlen(text) + 1);
    if (!c->lines[c->nlines]) {
        abort();
    }
    strcpy(c->lines[c->nlines++], text);
}

/* the first batch, or the next: up to `count` rows into out->rows, and a
 * cursor left in out->cursor when more remain */
#define BACKUP_SLICE (8u << 20)

/* A backup's next slice, as one progress row: bytes copied, files the
 * snapshot names, files the copy already held, done. One row a fetch,
 * whatever the batch asked for, so a fetch is one bounded slice. */
static int backup_batch(SqlCursor *c, GsqlResult *out, GsqlError *err) {
    int done = 0;
    int st = c->x.e->backup_step(c->backup, BACKUP_SLICE, &done);
    if (st != GS_OK) {
        return xfail(err, GSQL_STORAGE, 0, "the backup stopped: the engine answered %s; the copy has no manifest, "
                     "so it does not open, and the next backup into it completes it", gs_status_name(st));
    }
    uint64_t copied, files, reused;
    c->x.e->backup_progress(c->backup, &copied, &files, &reused);
    out->has_rows = 1;
    out->rows.length = 0;
    v3_w_array(&out->rows, 1);
    v3_w_array(&out->rows, 4);
    v3_w_int(&out->rows, copied);
    v3_w_int(&out->rows, files);
    v3_w_int(&out->rows, reused);
    v3_w_bool(&out->rows, done);
    out->cursor = done ? NULL : c;
    return GSQL_OK;
}

static int cursor_batch(SqlCursor *c, uint32_t count, GsqlResult *out, GsqlError *err) {
    if (c->backup) {
        return backup_batch(c, out, err);
    }
    GsqlBuf rows = {0};
    GsqlBuf one = {0};
    size_t n = 0;
    int r = 1;
    while (n < count && (r = cursor_next(c, &one)) == 1) {
        v3_put(&rows, one.bytes, one.length);
        n++;
    }
    if (r == 1) {
        /* a full batch: is there another row? */
        r = cursor_next(c, &c->pending);
        if (r == 1) {
            c->have_pending = 1;
        }
    }
    gsql_buf_free(&one);
    if (r < 0) {
        gsql_buf_free(&rows);
        *err = c->err;
        return err->code ? err->code : GSQL_EXPRESSION;
    }
    out->has_rows = 1;
    out->rows.length = 0;
    v3_w_array(&out->rows, n);
    v3_put(&out->rows, rows.bytes, rows.length);
    gsql_buf_free(&rows);
    out->cursor = c->have_pending ? c : NULL;
    return GSQL_OK;
}

static void columns_of(SqlCursor *c, GsqlBuf *out) {
    out->length = 0;
    if (c->backup) {
        static const char *const names[4] = { "bytes", "files", "reused", "done" };
        static const char *const kinds[4] = { "integer", "integer", "integer", "boolean" };
        v3_w_array(out, 4);
        for (int k = 0; k < 4; k++) {
            v3_w_record(out, 2);
            v3_w_name(out, "name");
            v3_w_text(out, names[k]);
            v3_w_name(out, "kind");
            v3_w_text(out, kinds[k]);
        }
        return;
    }
    if (c->explain) {
        v3_w_array(out, 1);
        v3_w_record(out, 2);
        v3_w_name(out, "name");
        v3_w_text(out, "plan");
        v3_w_name(out, "kind");
        v3_w_text(out, "string");
        return;
    }
    v3_w_array(out, c->plan.nout);
    for (size_t k = 0; k < c->plan.nout; k++) {
        v3_w_record(out, 2);
        v3_w_name(out, "name");
        v3_w_text(out, c->plan.names[k]);
        v3_w_name(out, "kind");
        v3_w_text(out, c->plan.kinds[k]);
    }
}

/* ---- writes --------------------------------------------------------------- */

typedef struct {
    GsqlBuf key;
    GsqlBuf *vals;       /* UPDATE: one per target */
    int *deflt;          /* UPDATE: SET col = DEFAULT */
} Change;

static void changes_free(Change *v, size_t n, size_t ntargets) {
    for (size_t k = 0; k < n; k++) {
        gsql_buf_free(&v[k].key);
        for (size_t j = 0; v[k].vals && j < ntargets; j++) {
            gsql_buf_free(&v[k].vals[j]);
        }
        free(v[k].vals);
        free(v[k].deflt);
    }
    free(v);
}

/* ---- foreign keys (docs/gsql_fkey_design.md) -------------------------------
 *
 * A foreign key is stored by its name as { format, table, columns, references,
 * ref_columns, enforced, bound }: names, never positions, so it survives ADD
 * COLUMN and a rebuilt parent. It BINDS by name to its referenced table's
 * primary key; while it does not, nothing is checked, and a schema change says
 * so (2119). The checks run before a row is written, as UNIQUE's do. */

typedef struct {
    char *name, *table, *ref;
    char **cols, **ref_cols;
    size_t ncols, nref;
    int enforced, bound;
} FkRec;

static void fk_free(FkRec *k) {
    free(k->name);
    free(k->table);
    free(k->ref);
    for (size_t i = 0; i < k->ncols; i++) {
        free(k->cols[i]);
    }
    for (size_t i = 0; i < k->nref; i++) {
        free(k->ref_cols[i]);
    }
    free(k->cols);
    free(k->ref_cols);
    memset(k, 0, sizeof *k);
}

static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *d = malloc(n + 1);
    if (!d) {
        abort();
    }
    memcpy(d, s, n + 1);
    return d;
}

static char **text_list(const V3Field *f, size_t *n);

static int fk_read(const char *name, const GsqlBuf *b, FkRec *k) {
    memset(k, 0, sizeof *k);
    V3Field f[10];
    long n = v3_record_fields(b->bytes, b->length, f, 10);
    const V3Field *t = n > 0 ? v3_field_named(f, n, "table") : NULL;
    const V3Field *r = n > 0 ? v3_field_named(f, n, "references") : NULL;
    if (!t || !r) {
        return 0;
    }
    k->name = dup_cstr(name);
    k->table = v3_read_text(t->v, t->vlen);
    k->ref = v3_read_text(r->v, r->vlen);
    k->cols = text_list(v3_field_named(f, n, "columns"), &k->ncols);
    k->ref_cols = text_list(v3_field_named(f, n, "ref_columns"), &k->nref);
    const V3Field *e = v3_field_named(f, n, "enforced"), *bd = v3_field_named(f, n, "bound");
    if (e) {
        v3_read_bool(e->v, e->vlen, &k->enforced);
    }
    if (bd) {
        v3_read_bool(bd->v, bd->vlen, &k->bound);
    }
    return k->table && k->ref;
}

static void fk_write(GsqlBuf *out, const FkRec *k) {
    out->length = 0;
    v3_w_record(out, 8);
    v3_w_name(out, "format");
    v3_w_int(out, 1);
    v3_w_name(out, "kind");
    v3_w_text(out, "foreign key");
    v3_w_name(out, "table");
    v3_w_text(out, k->table);
    v3_w_name(out, "columns");
    v3_w_array(out, k->ncols);
    for (size_t i = 0; i < k->ncols; i++) {
        v3_w_text(out, k->cols[i]);
    }
    v3_w_name(out, "references");
    v3_w_text(out, k->ref);
    v3_w_name(out, "ref_columns");
    v3_w_array(out, k->nref);
    for (size_t i = 0; i < k->nref; i++) {
        v3_w_text(out, k->ref_cols[i]);
    }
    v3_w_name(out, "enforced");
    v3_w_bool(out, k->enforced);
    v3_w_name(out, "bound");
    v3_w_bool(out, k->bound);
}

static int fk_put(X *x, const FkRec *k, int replace) {
    GsqlBuf b = {0};
    fk_write(&b, k);
    int rc = gsql_fkey_put(x->e, x->t, k->name, b.bytes, b.length, replace, x->err);
    gsql_buf_free(&b);
    return rc;
}

typedef struct {
    FkRec *v;
    size_t n;
    X *x;
} FkList;

static int collect_fk(void *ctx, const char *name) {
    FkList *l = ctx;
    GsqlBuf b = {0};
    GsqlError e = { GSQL_OK, "", 0, 0 };
    if (gsql_fkey_get(l->x->e, l->x->t, name, &b, &e) == GSQL_OK) {
        FkRec *g = realloc(l->v, sizeof *g * (l->n + 1));
        if (!g) {
            abort();
        }
        l->v = g;
        if (fk_read(name, &b, &l->v[l->n])) {
            l->n++;
        }
    }
    gsql_buf_free(&b);
    return 0;
}

static void fk_list_free(FkList *l) {
    for (size_t i = 0; i < l->n; i++) {
        fk_free(&l->v[i]);
    }
    free(l->v);
    memset(l, 0, sizeof *l);
}

static int fk_all(X *x, FkList *l) {
    memset(l, 0, sizeof *l);
    l->x = x;
    GsqlError e = { GSQL_OK, "", 0, 0 };
    int rc = gsql_fkey_list(x->e, x->t, collect_fk, l, &e);
    if (rc != GSQL_OK) {
        *x->err = e;
    }
    return rc;
}

/* A foreign key bound to what is there now: its child's columns and its
 * parent's primary key, by position, with the same kinds. 1, or 0 with
 * `why` saying what stops it. The parent is opened into *parent. */
static int fk_bind(X *x, const FkRec *k, GsqlTable *child, GsqlTable **parent, unsigned *cpos, unsigned *ppos,
                   char *why, size_t cap) {
    *parent = NULL;
    GsqlError e = { GSQL_OK, "", 0, 0 };
    for (size_t i = 0; i < k->ncols; i++) {
        long c = gsql_table_find(child, k->cols[i]);
        if (c < 0) {
            snprintf(why, cap, "table `%s` has no column `%s`", k->table, k->cols[i]);
            return 0;
        }
        cpos[i] = (unsigned)c;
    }
    if (gsql_table_open(x->e, x->t, k->ref, parent, &e) != GSQL_OK) {
        *parent = NULL;
        snprintf(why, cap, "there is no table `%s`", k->ref);
        return 0;
    }
    size_t nkey = gsql_table_nkey(*parent);
    const unsigned *key = gsql_table_key_cols(*parent);
    int same = nkey == k->nref && nkey == k->ncols;
    for (size_t i = 0; same && i < nkey; i++) {
        same = strcmp(gsql_table_col(*parent, key[i])->name, k->ref_cols[i]) == 0;
    }
    if (!same) {
        size_t used = (size_t)snprintf(why, cap, "the primary key of `%s` is not (", k->ref);
        for (size_t i = 0; i < k->nref && used < cap; i++) {
            used += (size_t)snprintf(why + used, cap - used, "%s%s", i ? ", " : "", k->ref_cols[i]);
        }
        if (used < cap) {
            snprintf(why + used, cap - used, ")");
        }
        gsql_table_close(*parent);
        *parent = NULL;
        return 0;
    }
    for (size_t i = 0; i < nkey; i++) {
        const GsqlColumn *a = gsql_table_col(child, cpos[i]), *b = gsql_table_col(*parent, key[i]);
        ppos[i] = key[i];
        if (a->kind != b->kind || strcmp(a->currency, b->currency) != 0) {
            snprintf(why, cap, "`%s.%s` is %s and `%s.%s` is %s", k->table, a->name, gsql_kind_name(a->kind), k->ref,
                     b->name, gsql_kind_name(b->kind));
            gsql_table_close(*parent);
            *parent = NULL;
            return 0;
        }
    }
    return 1;
}

static int is_absent_bytes(const GsBytes *v) {
    return v->n == 0 || v->p[0] == SER_UNKNOWN || v->p[0] == SER_NULL;
}

/* Whether the parent has a row whose key is the child's values: 1, 0, or -1
 * failed. A value with an absence references nothing, and answers 1. */
static int fk_parent_has(X *x, GsqlTable *parent, const unsigned *ppos, const GsBytes *vals, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (is_absent_bytes(&vals[i])) {
            return 1;
        }
    }
    size_t pc = gsql_table_ncols(parent);
    GsqlIn *in = calloc(pc ? pc : 1, sizeof(GsqlIn));
    if (!in) {
        abort();
    }
    for (size_t i = 0; i < n; i++) {
        in[ppos[i]].present = 1;
        in[ppos[i]].p = vals[i].p;
        in[ppos[i]].n = vals[i].n;
    }
    GsqlBuf key = {0}, row = {0};
    GsqlError e = { GSQL_OK, "", 0, 0 };
    int r = -1;
    if (gsql_row_key(parent, in, &key, &e) == GSQL_OK) {
        int rc = gsql_row_get(x->e, x->t, parent, (GsBytes){ key.bytes, key.length }, &row, &e);
        r = rc == GSQL_OK ? 1 : rc == GSQL_NOT_FOUND ? 0 : -1;
    }
    if (r < 0) {
        *x->err = e;
    }
    gsql_buf_free(&key);
    gsql_buf_free(&row);
    free(in);
    return r;
}

/* the key encoding of n values, for comparing a child's with a parent's */
static int fk_key(const GsBytes *vals, size_t n, GsqlKey *k) {
    for (size_t i = 0; i < n; i++) {
        if (gsql_key_append(k, vals[i].p, vals[i].n, NULL, 0) != GSQL_KEY_OK) {
            return 0;
        }
    }
    return 1;
}

/* The child rows whose columns hold `want`'s key (or, with want NULL, that
 * reference no parent row): counted, by one walk of the child's rows. */
static int fk_walk(X *x, GsqlTable *child, const unsigned *cpos, GsqlTable *parent, const unsigned *ppos, size_t n,
                   const GsqlKey *want, uint64_t *count) {
    *count = 0;
    GsCursor *c = NULL;
    int st = x->e->cursor_open(x->t, gsql_table_rows_tree(child), &c);
    if (st) {
        return xfail(x->err, GSQL_STORAGE, 0, "a cursor could not be opened: the engine answered %s",
                     gs_status_name(st));
    }
    GsBytes *vals = calloc(n ? n : 1, sizeof(GsBytes));
    if (!vals) {
        abort();
    }
    int at = 0, rc = GSQL_OK;
    for (st = x->e->first(c, &at); !st && at && rc == GSQL_OK; st = x->e->next(c, &at)) {
        GsBytes row;
        if (x->e->value(c, &row)) {
            break;
        }
        int absent = 0;
        for (size_t i = 0; i < n && rc == GSQL_OK; i++) {
            rc = gsql_row_value(child, row.p, row.n, cpos[i], &vals[i], x->err);
            absent |= rc == GSQL_OK && is_absent_bytes(&vals[i]);
        }
        if (rc != GSQL_OK || absent) {
            continue;
        }
        if (want) {
            GsqlKey k;
            gsql_key_init(&k);
            if (fk_key(vals, n, &k) && k.length == want->length && memcmp(k.bytes, want->bytes, k.length) == 0) {
                (*count)++;
            }
            gsql_key_free(&k);
        } else {
            int has = fk_parent_has(x, parent, ppos, vals, n);
            if (has < 0) {
                rc = x->err->code ? x->err->code : GSQL_STORAGE;
            } else if (!has) {
                (*count)++;
            }
        }
    }
    free(vals);
    x->e->cursor_close(c);
    return rc;
}

/* A row about to be written to `child`: each enforced, bound foreign key on
 * it must find its parent. `vals` is the whole row, defaults resolved. */
static int fk_check_child(X *x, const FkList *fks, GsqlTable *child, const GsBytes *row) {
    for (size_t i = 0; i < fks->n; i++) {
        const FkRec *k = &fks->v[i];
        if (!k->enforced || !k->bound || strcmp(k->table, gsql_table_name(child)) != 0) {
            continue;
        }
        unsigned cpos[64], ppos[64];
        GsqlTable *parent;
        char why[300];
        if (k->ncols > 64 || !fk_bind(x, k, child, &parent, cpos, ppos, why, sizeof why)) {
            continue;   /* not bound now: the re-check will have said so */
        }
        GsBytes v[64];
        for (size_t c = 0; c < k->ncols; c++) {
            v[c] = row[cpos[c]];
        }
        int has = fk_parent_has(x, parent, ppos, v, k->ncols);
        gsql_table_close(parent);
        if (has < 0) {
            return x->err->code ? x->err->code : GSQL_STORAGE;
        }
        if (!has) {
            return xfail(x->err, GSQL_FOREIGN_KEY, 0, "foreign key `%s`: this row of `%s` references a row of `%s` "
                         "that is not there", k->name, k->table, k->ref);
        }
    }
    return GSQL_OK;
}

/* A parent row about to be deleted, or have its key changed: no child row of
 * an enforced, bound foreign key may still reference its old key. */
static int fk_check_parent(X *x, const FkList *fks, GsqlTable *parent, const GsBytes *row) {
    for (size_t i = 0; i < fks->n; i++) {
        const FkRec *k = &fks->v[i];
        if (!k->enforced || !k->bound || strcmp(k->ref, gsql_table_name(parent)) != 0) {
            continue;
        }
        GsqlTable *child = NULL, *bound = NULL;
        GsqlError e = { GSQL_OK, "", 0, 0 };
        if (gsql_table_open(x->e, x->t, k->table, &child, &e) != GSQL_OK) {
            continue;
        }
        unsigned cpos[64], ppos[64];
        char why[300];
        int ok = k->ncols <= 64 && fk_bind(x, k, child, &bound, cpos, ppos, why, sizeof why);
        int rc = GSQL_OK;
        if (ok) {
            GsBytes v[64];
            for (size_t c = 0; c < k->ncols; c++) {
                v[c] = row[ppos[c]];
            }
            GsqlKey want;
            gsql_key_init(&want);
            uint64_t count = 0;
            if (fk_key(v, k->ncols, &want)) {
                rc = fk_walk(x, child, cpos, NULL, NULL, k->ncols, &want, &count);
            }
            gsql_key_free(&want);
            if (rc == GSQL_OK && count) {
                rc = xfail(x->err, GSQL_FOREIGN_KEY, 0, "foreign key `%s`: %llu row%s of `%s` still reference%s this "
                           "row of `%s`", k->name, (unsigned long long)count, count == 1 ? "" : "s", k->table,
                           count == 1 ? "s" : "", k->ref);
            }
        }
        if (bound) {
            gsql_table_close(bound);
        }
        gsql_table_close(child);
        if (rc != GSQL_OK) {
            return rc;
        }
    }
    return GSQL_OK;
}

/* every value of a table's row, defaults and absences resolved */
static void row_values(GsqlTable *tb, const GsqlIn *in, GsBytes *out) {
    static const unsigned char unknown = SER_UNKNOWN;
    for (size_t c = 0; c < gsql_table_ncols(tb); c++) {
        const GsqlColumn *col = gsql_table_col(tb, c);
        if (in[c].present) {
            out[c].p = in[c].p;
            out[c].n = in[c].n;
        } else if (col->def) {
            out[c].p = col->def;
            out[c].n = col->def_len;
        } else {
            out[c].p = &unknown;
            out[c].n = 1;
        }
    }
}

/* ---- CHECK constraints (docs/gsql_check_design.md) ------------------------
 *
 * Stored beside the foreign keys as { format, kind: "check", table, text,
 * bound }. A check is planned as `SELECT (text) FROM table`, so its names bind
 * to the table's columns by the ordinary path, and evaluated against a row
 * about to be written, read by position (XScan.direct). A row passes unless
 * the check is FALSE. */

typedef struct {
    char *name, *table, *text;
    int bound;
} CkRec;

static void ck_free(CkRec *c) {
    free(c->name);
    free(c->table);
    free(c->text);
    memset(c, 0, sizeof *c);
}

/* the record's kind: "check", "foreign key", or NULL (a foreign key written
   before kinds were) */
static char *record_kind(const GsqlBuf *b) {
    V3Field f[10];
    long n = v3_record_fields(b->bytes, b->length, f, 10);
    const V3Field *k = n > 0 ? v3_field_named(f, n, "kind") : NULL;
    return k ? v3_read_text(k->v, k->vlen) : NULL;
}

static int ck_read(const char *name, const GsqlBuf *b, CkRec *c) {
    memset(c, 0, sizeof *c);
    char *kind = record_kind(b);
    int is = kind && strcmp(kind, "check") == 0;
    free(kind);
    if (!is) {
        return 0;
    }
    V3Field f[10];
    long n = v3_record_fields(b->bytes, b->length, f, 10);
    const V3Field *t = v3_field_named(f, n, "table"), *x = v3_field_named(f, n, "text"),
                  *bd = v3_field_named(f, n, "bound");
    if (!t || !x) {
        return 0;
    }
    c->name = dup_cstr(name);
    c->table = v3_read_text(t->v, t->vlen);
    c->text = v3_read_text(x->v, x->vlen);
    if (bd) {
        v3_read_bool(bd->v, bd->vlen, &c->bound);
    }
    return c->table && c->text;
}

static int ck_put(X *x, const CkRec *c, int replace) {
    GsqlBuf b = {0};
    v3_w_record(&b, 5);
    v3_w_name(&b, "format");
    v3_w_int(&b, 1);
    v3_w_name(&b, "kind");
    v3_w_text(&b, "check");
    v3_w_name(&b, "table");
    v3_w_text(&b, c->table);
    v3_w_name(&b, "text");
    v3_w_text(&b, c->text);
    v3_w_name(&b, "bound");
    v3_w_bool(&b, c->bound);
    int rc = gsql_fkey_put(x->e, x->t, c->name, b.bytes, b.length, replace, x->err);
    gsql_buf_free(&b);
    return rc;
}

typedef struct {
    CkRec *v;
    size_t n;
    X *x;
} CkList;

static int collect_ck(void *ctx, const char *name) {
    CkList *l = ctx;
    GsqlBuf b = {0};
    GsqlError e = { GSQL_OK, "", 0, 0 };
    if (gsql_fkey_get(l->x->e, l->x->t, name, &b, &e) == GSQL_OK) {
        CkRec *g = realloc(l->v, sizeof *g * (l->n + 1));
        if (!g) {
            abort();
        }
        l->v = g;
        if (ck_read(name, &b, &l->v[l->n])) {
            l->n++;
        }
    }
    gsql_buf_free(&b);
    return 0;
}

static void ck_list_free(CkList *l) {
    for (size_t i = 0; i < l->n; i++) {
        ck_free(&l->v[i]);
    }
    free(l->v);
    memset(l, 0, sizeof *l);
}

static int ck_all(X *x, CkList *l) {
    memset(l, 0, sizeof *l);
    l->x = x;
    GsqlError e = { GSQL_OK, "", 0, 0 };
    int rc = gsql_fkey_list(x->e, x->t, collect_ck, l, &e);
    if (rc != GSQL_OK) {
        *x->err = e;
    }
    return rc;
}

/* A check planned against its table as it is now. */
typedef struct {
    const CkRec *rec;
    char *query;
    SqlParsed parsed;
    SqlPlan plan;
    int planned;
} CkPlan;

static void ck_plan_free(CkPlan *cp) {
    if (cp->planned) {
        sql_plan_free(&cp->plan);
        sql_parsed_free(&cp->parsed);
    }
    free(cp->query);
    memset(cp, 0, sizeof *cp);
}

static int has_subquery(const SqlExpr *e) {
    if (!e) {
        return 0;
    }
    if (e->kind == SX_SUBQ) {
        return 1;
    }
    if (has_subquery(e->operand)) {
        return 1;
    }
    for (size_t k = 0; k < e->na; k++) {
        if (has_subquery(e->a[k])) {
            return 1;
        }
    }
    return 0;
}

/* `SELECT text FROM table`, its `nout` outputs reading only the row: 1
 * planned; 0 with `why` naming what stops it */
static int row_plan(X *x, const char *what, const char *table, const char *text, size_t nout, CkPlan *cp,
                    char *why, size_t cap) {
    memset(cp, 0, sizeof *cp);
    size_t qn = strlen(table) * 2 + strlen(text) + 32;
    cp->query = malloc(qn);
    if (!cp->query) {
        abort();
    }
    /* the table's name quoted, a quote inside it doubled */
    size_t used = (size_t)snprintf(cp->query, qn, "SELECT %s FROM \"", text);
    for (const char *c = table; *c && used + 3 < qn; c++) {
        cp->query[used++] = *c;
        if (*c == '"') {
            cp->query[used++] = '"';
        }
    }
    snprintf(cp->query + used, qn - used, "\"");
    GsqlError e = { GSQL_OK, "", 0, 0 };
    if (sql_parse(cp->query, strlen(cp->query), &cp->parsed, &e) != GSQL_OK) {
        snprintf(why, cap, "%s", e.message);
        return 0;
    }
    if (sql_plan(x->e, x->t, cp->parsed.stmt, &cp->parsed.arena, cp->query, 0, &cp->plan, &e) != GSQL_OK) {
        snprintf(why, cap, "%s", e.message);
        sql_parsed_free(&cp->parsed);
        return 0;
    }
    cp->planned = 1;
    int bad = cp->plan.nout != nout || cp->plan.grouped;
    for (size_t k = 0; !bad && k < cp->plan.nout; k++) {
        bad = has_subquery(cp->plan.out[k]);
    }
    if (bad) {
        snprintf(why, cap, "%s reads only its own row: no subquery and no aggregate", what);
        return 0;
    }
    return 1;
}

static int ck_plan(X *x, const char *table, const char *text, CkPlan *cp, char *why, size_t cap) {
    return row_plan(x, "a check", table, text, 1, cp, why, cap);
}

/* The check against one row: 1 passes, 0 refused (err filled), -1 failed. */
static int ck_row(X *x, CkPlan *cp, const GsBytes *row) {
    X cx;
    GsqlError e = { GSQL_OK, "", 0, 0 };
    x_init(&cx, x->q, x->e, x->t, &cp->plan, &e);
    cx.params = x->params;
    cx.paramslen = x->paramslen;
    cx.sx[0].direct = row;
    cx.sx[0].have_row = 1;
    GsqlHv *v = eval(&cx, cp->plan.out[0]);
    int r;
    if (!v) {
        *x->err = e;
        x->err->position = 0;   /* not a place in the check's own planned query */
        snprintf(x->err->message, sizeof x->err->message, "check `%.80s` on `%.80s`: %.300s", cp->rec->name,
                 cp->rec->table, e.message);
        r = -1;
    } else {
        int t = x->h->truth(x->h->ctx, v);
        if (t == -2) {
            xfail(x->err, GSQL_EXPRESSION, 0, "check `%s` on `%s` is not a condition: it answers %s", cp->rec->name,
                  cp->rec->table, tag_word(kind_of(&cx, v)));
            r = -1;
        } else if (t == 0) {
            xfail(x->err, GSQL_CHECK, 0, "check `%s` refuses this row of `%s`: %s is false", cp->rec->name,
                  cp->rec->table, cp->rec->text);
            r = 0;
        } else {
            r = 1;   /* true, or unknown, which the standard lets pass */
        }
        rel(&cx, v);
    }
    cx.sx[0].direct = NULL;
    x_free(&cx);
    return r;
}

/* the bound checks of one table, planned once for a statement */
typedef struct {
    CkList list;
    CkPlan *plans;
    size_t n;
} CkSet;

static int ck_load(X *x, const char *table, CkSet *set) {
    memset(set, 0, sizeof *set);
    int rc = ck_all(x, &set->list);
    set->plans = calloc(set->list.n ? set->list.n : 1, sizeof(CkPlan));
    if (!set->plans) {
        abort();
    }
    for (size_t i = 0; rc == GSQL_OK && i < set->list.n; i++) {
        CkRec *c = &set->list.v[i];
        char why[300];
        if (!c->bound || strcmp(c->table, table) != 0) {
            continue;
        }
        if (ck_plan(x, table, c->text, &set->plans[set->n], why, sizeof why)) {
            set->plans[set->n++].rec = c;
        } else {
            ck_plan_free(&set->plans[set->n]);
        }
    }
    return rc;
}

static void ck_set_free(CkSet *set) {
    for (size_t i = 0; i < set->n; i++) {
        ck_plan_free(&set->plans[i]);
    }
    free(set->plans);
    ck_list_free(&set->list);
    memset(set, 0, sizeof *set);
}

static int ck_check_row(X *x, CkSet *set, GsqlTable *tb, const GsBytes *row) {
    /* a value that does not fit its column is refused as that, first: a check
       over it would refuse it in a less direct sentence */
    for (size_t c = 0; set->n && c < gsql_table_ncols(tb); c++) {
        int rc = gsql_value_fits(gsql_table_col(tb, c), row[c].p, row[c].n, x->err);
        if (rc != GSQL_OK) {
            char m[sizeof x->err->message];
            /* the layer says `?.col`, knowing only the column */
            const char *msg = strncmp(x->err->message, "?.", 2) == 0 ? x->err->message + 1 : x->err->message;
            snprintf(m, sizeof m, "%s%s%.400s", gsql_table_name(tb), msg[0] == '.' ? "" : ": ", msg);
            snprintf(x->err->message, sizeof x->err->message, "%s", m);
            return rc;
        }
    }
    for (size_t i = 0; i < set->n; i++) {
        int r = ck_row(x, &set->plans[i], row);
        if (r <= 0) {
            return x->err->code ? x->err->code : GSQL_CHECK;
        }
    }
    return GSQL_OK;
}

/* ---- generated values: SERIAL and RANDOM (docs/gsql_generated_design.md) --
 *
 * A generator belongs to its column's schema. A serial's counters are the
 * table layer's (gsql_serial_get/set), one per scope, the scope being the key
 * encoding of `SELECT scope... FROM table` evaluated against the row about to
 * be written, as a CHECK is. A counter is written only once its row is, in
 * the same transaction, so numbers are gapless. */

typedef struct {
    size_t col;
    CkPlan plan;             /* SERIAL OF's scope; unplanned when unscoped */
    GsqlBuf scope;           /* this row's scope key */
    uint64_t next;           /* the counter to write once the row is: 0 none */
} GenSer;

typedef struct {
    GenSer *ser;
    size_t nser;
    size_t *rnd;             /* random columns */
    size_t nrnd;
    GsqlBuf *bufs;           /* one per column: a generated value */
} GenSet;

static void gen_set_free(GenSet *g, size_t ncols) {
    for (size_t i = 0; i < g->nser; i++) {
        ck_plan_free(&g->ser[i].plan);
        gsql_buf_free(&g->ser[i].scope);
    }
    for (size_t c = 0; g->bufs && c < ncols; c++) {
        gsql_buf_free(&g->bufs[c]);
    }
    free(g->ser);
    free(g->rnd);
    free(g->bufs);
    memset(g, 0, sizeof *g);
}

/* the scope's expressions as one select list */
static char *scope_list(const GsqlColumn *col) {
    size_t n = 1;
    for (size_t k = 0; k < col->nscope; k++) {
        n += strlen(col->scope[k]) + 2;
    }
    char *q = malloc(n);
    if (!q) {
        abort();
    }
    q[0] = 0;
    for (size_t k = 0; k < col->nscope; k++) {
        if (k) {
            strcat(q, ", ");
        }
        strcat(q, col->scope[k]);
    }
    return q;
}

static int reads_column(const SqlExpr *e, unsigned col) {
    if (!e) {
        return 0;
    }
    if (e->kind == SX_COL && e->tab == 0 && e->col == col) {
        return 1;
    }
    if (reads_column(e->operand, col)) {
        return 1;
    }
    for (size_t k = 0; k < e->na; k++) {
        if (reads_column(e->a[k], col)) {
            return 1;
        }
    }
    return 0;
}

/* A serial's scope planned against `table`: GSQL_OK, or a refusal at `pos`. */
static int scope_plan(X *x, const char *table, const GsqlColumn *col, long self, CkPlan *cp, uint32_t pos) {
    char *list = scope_list(col);
    char why[400];
    int planned = row_plan(x, "a scope", table, list, col->nscope, cp, why, sizeof why);
    free(list);
    if (!planned) {
        return xfail(x->err, GSQL_SCHEMA, pos, "serial `%s.%s`'s scope cannot be applied: %s", table, col->name, why);
    }
    for (size_t k = 0; self >= 0 && k < cp->plan.nout; k++) {
        if (reads_column(cp->plan.out[k], (unsigned)self)) {
            return xfail(x->err, GSQL_SCHEMA, pos, "serial `%s.%s`'s scope reads `%s` itself: the number would be "
                         "needed to find the counter it comes from", table, col->name, col->name);
        }
    }
    return GSQL_OK;
}

static int gen_load(X *x, GsqlTable *tb, GenSet *g) {
    memset(g, 0, sizeof *g);
    size_t ncols = gsql_table_ncols(tb);
    for (size_t c = 0; c < ncols; c++) {
        int gen = gsql_table_col(tb, c)->gen;
        g->nser += gen == GSQL_GEN_SERIAL;
        g->nrnd += gen == GSQL_GEN_RANDOM;
    }
    if (!g->nser && !g->nrnd) {
        return GSQL_OK;
    }
    g->ser = calloc(g->nser ? g->nser : 1, sizeof(GenSer));
    g->rnd = calloc(g->nrnd ? g->nrnd : 1, sizeof(size_t));
    g->bufs = calloc(ncols, sizeof(GsqlBuf));
    if (!g->ser || !g->rnd || !g->bufs) {
        abort();
    }
    size_t ns = 0, nr = 0;
    for (size_t c = 0; c < ncols; c++) {
        const GsqlColumn *col = gsql_table_col(tb, c);
        if (col->gen == GSQL_GEN_RANDOM) {
            g->rnd[nr++] = c;
        } else if (col->gen == GSQL_GEN_SERIAL) {
            GenSer *s = &g->ser[ns++];
            s->col = c;
            if (col->nscope) {
                int rc = scope_plan(x, gsql_table_name(tb), col, -1, &s->plan, 0);
                if (rc != GSQL_OK) {
                    return rc;
                }
            }
        }
    }
    return GSQL_OK;
}

/* n bytes of the system's cryptographic randomness */
static int draw(X *x, void *b, size_t n) {
    if (gb_secure_random(b, n) != 0) {
        return xfail(x->err, GSQL_STORAGE, 0, "the system's random source answered nothing");
    }
    return GSQL_OK;
}

/* uniform in [0, n): a draw of `width` bytes, those past the largest
 * multiple of n drawn again, so no value is likelier than another */
static int draw_below(X *x, uint64_t n, int width, uint64_t *out) {
    uint64_t span_max = width >= 8 ? UINT64_MAX : ((uint64_t)1 << (8 * width)) - 1;
    uint64_t limit = span_max - (span_max % n + 1) % n;   /* the last value kept */
    for (;;) {
        unsigned char b[8];
        int rc = draw(x, b, (size_t)width);
        if (rc != GSQL_OK) {
            return rc;
        }
        uint64_t r = 0;
        for (int i = 0; i < width; i++) {
            r = r << 8 | b[i];
        }
        if (r <= limit) {
            *out = r % n;
            return GSQL_OK;
        }
    }
}

static const char URL_SAFE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/* the characters of an alphabet: each one's offset, and a sentinel */
static size_t alphabet_chars(const char *a, size_t *at, size_t max) {
    size_t n = 0, len = strlen(a);
    for (size_t i = 0; i < len && n < max;) {
        unsigned char c = (unsigned char)a[i];
        size_t l = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        at[n++] = i;
        i += l;
    }
    at[n] = len;
    return n;
}

static void put_exact(GsqlBuf *b, uint64_t v) {
    b->length = 0;
    v3_w_int(b, v);
}

static int random_value(X *x, const GsqlColumn *col, GsqlBuf *out) {
    out->length = 0;
    if (col->kind == GSQL_COL_INTEGER) {
        uint64_t v;
        int rc;
        if (!col->length) {
            unsigned char b[8];
            rc = draw(x, b, 8);
            v = 0;
            for (int i = 0; i < 8; i++) {
                v = v << 8 | b[i];
            }
            v &= INT64_MAX;
        } else {
            uint64_t base = 1;
            for (uint32_t i = 1; i < col->length; i++) {
                base *= 10;
            }
            rc = draw_below(x, base * 9, 8, &v);   /* n digits: base .. 10*base - 1 */
            v += base;
        }
        if (rc == GSQL_OK) {
            put_exact(out, v);
        }
        return rc;
    }
    const char *a = col->alphabet ? col->alphabet : URL_SAFE;
    size_t *at = malloc((GSQL_RANDOM_MAX_ALPHABET + 1) * sizeof *at);
    if (!at) {
        abort();
    }
    size_t na = alphabet_chars(a, at, GSQL_RANDOM_MAX_ALPHABET);
    uint32_t len = col->length ? col->length : 22;
    GsqlBuf text = {0};
    int rc = GSQL_OK;
    for (uint32_t i = 0; i < len && rc == GSQL_OK; i++) {
        uint64_t k;
        rc = draw_below(x, na, na <= 256 ? 1 : 2, &k);
        if (rc == GSQL_OK) {
            v3_put(&text, a + at[k], at[k + 1] - at[k]);
        }
    }
    v3_put(&text, "", 1);
    if (rc == GSQL_OK) {
        v3_w_text(out, (const char *)text.bytes);
    }
    gsql_buf_free(&text);
    free(at);
    return rc;
}

/* how many values a random column can hold, for a refusal: 0 when more
 * than 10^18 */
static uint64_t random_space(const GsqlColumn *col) {
    if (col->kind == GSQL_COL_INTEGER) {
        if (!col->length) {
            return 0;
        }
        uint64_t base = 1;
        for (uint32_t i = 1; i < col->length; i++) {
            base *= 10;
        }
        return base * 9;
    }
    size_t at[GSQL_RANDOM_MAX_ALPHABET + 1];
    uint64_t na = alphabet_chars(col->alphabet ? col->alphabet : URL_SAFE, at, GSQL_RANDOM_MAX_ALPHABET);
    uint64_t v = 1;
    for (uint32_t i = 0; i < (col->length ? col->length : 22); i++) {
        if (v > 1000000000000000000ull / na) {
            return 0;
        }
        v *= na;
    }
    return v;
}

/* this row's scope key for one serial */
static int gen_scope(X *x, GsqlTable *tb, GenSer *s, const GsBytes *row) {
    s->scope.length = 0;
    if (!s->plan.planned) {
        return GSQL_OK;
    }
    X cx;
    GsqlError e = { GSQL_OK, "", 0, 0 };
    x_init(&cx, x->q, x->e, x->t, &s->plan.plan, &e);
    cx.params = x->params;
    cx.paramslen = x->paramslen;
    cx.sx[0].direct = row;
    cx.sx[0].have_row = 1;
    GsqlKey key;
    gsql_key_init(&key);
    int rc = GSQL_OK;
    for (size_t k = 0; rc == GSQL_OK && k < s->plan.plan.nout; k++) {
        GsqlHv *v = eval(&cx, s->plan.plan.out[k]);
        if (!v) {
            rc = e.code ? e.code : GSQL_EXPRESSION;
        } else {
            GsqlBuf b = {0};
            rc = cx.h->store(cx.h->ctx, v, &b, &e);
            if (rc == GSQL_OK && gsql_key_append(&key, b.bytes, b.length, NULL, 0) != GSQL_KEY_OK) {
                rc = xfail(&e, GSQL_EXPRESSION, 0, "%s has no order G-SQL can key, so it cannot be a scope",
                           tag_word(b.length ? b.bytes[0] : 0));
            }
            gsql_buf_free(&b);
            rel(&cx, v);
        }
    }
    if (rc == GSQL_OK) {
        v3_put(&s->scope, key.bytes, key.length);
    } else {
        *x->err = e;
        x->err->position = 0;   /* not a place in the scope's own planned query */
        snprintf(x->err->message, sizeof x->err->message, "serial `%.80s.%.80s`'s scope: %.300s",
                 gsql_table_name(tb), gsql_table_col(tb, s->col)->name, e.message);
    }
    gsql_key_free(&key);
    cx.sx[0].direct = NULL;
    x_free(&cx);
    return rc;
}

/* The generated values of one row about to be inserted: each random column
 * the statement omitted drawn, and each serial numbered or, given a value,
 * noted. `vals` gains the values; `whole` is the row as it will be written. */
static int gen_fill(X *x, GsqlTable *tb, GenSet *g, GsqlIn *vals, GsBytes *whole, const int *omitted) {
    for (size_t i = 0; i < g->nrnd; i++) {
        size_t c = g->rnd[i];
        if (!omitted[c]) {
            continue;
        }
        int rc = random_value(x, gsql_table_col(tb, c), &g->bufs[c]);
        if (rc != GSQL_OK) {
            return rc;
        }
        vals[c].present = 1;
        vals[c].p = g->bufs[c].bytes;
        vals[c].n = g->bufs[c].length;
    }
    row_values(tb, vals, whole);
    for (size_t i = 0; i < g->nser; i++) {
        GenSer *s = &g->ser[i];
        s->next = 0;
        if (!omitted[s->col]) {
            /* a value supplied: the counter moves up to it, never down */
            uint64_t v, hw;
            int rc = gen_scope(x, tb, s, whole);
            if (rc == GSQL_OK && v3_read_u64(vals[s->col].p, vals[s->col].n, &v)) {
                rc = gsql_serial_get(x->e, x->t, tb, s->col, (GsBytes){ s->scope.bytes, s->scope.length }, &hw,
                                     x->err);
                if (rc == GSQL_OK && v > hw) {
                    s->next = v;
                }
            }
            if (rc != GSQL_OK) {
                return rc;
            }
            continue;
        }
        int rc = gen_scope(x, tb, s, whole);
        uint64_t hw = 0;
        if (rc == GSQL_OK) {
            rc = gsql_serial_get(x->e, x->t, tb, s->col, (GsBytes){ s->scope.bytes, s->scope.length }, &hw, x->err);
        }
        if (rc != GSQL_OK) {
            return rc;
        }
        if (hw >= (uint64_t)INT64_MAX) {
            return xfail(x->err, GSQL_SERIAL, 0, "serial `%s.%s` has reached %lld in this scope: there is no next "
                         "number", gsql_table_name(tb), gsql_table_col(tb, s->col)->name, (long long)INT64_MAX);
        }
        s->next = hw + 1;
        put_exact(&g->bufs[s->col], s->next);
        vals[s->col].present = 1;
        vals[s->col].p = g->bufs[s->col].bytes;
        vals[s->col].n = g->bufs[s->col].length;
        whole[s->col].p = vals[s->col].p;
        whole[s->col].n = vals[s->col].n;
    }
    return GSQL_OK;
}

/* once the row is written: its counters */
static int gen_commit(X *x, GsqlTable *tb, GenSet *g) {
    for (size_t i = 0; i < g->nser; i++) {
        GenSer *s = &g->ser[i];
        if (s->next) {
            int rc = gsql_serial_set(x->e, x->t, tb, s->col, (GsBytes){ s->scope.bytes, s->scope.length }, s->next,
                                     x->err);
            if (rc != GSQL_OK) {
                return rc;
            }
        }
    }
    return GSQL_OK;
}

/* whether a DUPLICATE was in a key holding a random column the row drew, so
 * that drawing again can help */
static int gen_redraws(GsqlTable *tb, const GenSet *g, const int *omitted, int dup) {
    size_t n;
    const unsigned *cols;
    if (dup == -1) {
        n = gsql_table_nkey(tb);
        cols = gsql_table_key_cols(tb);
    } else if (dup > 0 && (size_t)dup <= gsql_table_nindexes(tb)) {
        const GsqlIndex *ix = gsql_table_index(tb, (size_t)dup - 1);
        n = ix->ncols;
        cols = ix->cols;
    } else {
        return 0;
    }
    for (size_t i = 0; i < g->nrnd; i++) {
        for (size_t k = 0; k < n; k++) {
            if (cols[k] == g->rnd[i] && omitted[g->rnd[i]]) {
                return 1;
            }
        }
    }
    return 0;
}

#define GEN_DRAWS 32

/* Every change an UPDATE or DELETE collected, checked against the foreign
 * keys before any is applied: a deleted row, or one whose key changes, must
 * not still be referenced; an updated child row must find its parent. */
static int fk_check_changes(X *x, GsqlTable *tb, const SqlStmt *s, const Change *ch, size_t n, const SqlPlan *p) {
    FkList fks;
    int rc = fk_all(x, &fks);
    CkSet cks;
    memset(&cks, 0, sizeof cks);
    if (rc == GSQL_OK && s->kind == SQ_UPDATE) {
        rc = ck_load(x, gsql_table_name(tb), &cks);
    }
    int any = cks.n > 0;
    for (size_t i = 0; i < fks.n; i++) {
        any |= fks.v[i].enforced && (strcmp(fks.v[i].table, gsql_table_name(tb)) == 0 ||
                                     strcmp(fks.v[i].ref, gsql_table_name(tb)) == 0);
    }
    size_t ncols = gsql_table_ncols(tb);
    GsBytes *oldv = calloc(ncols ? ncols : 1, sizeof(GsBytes)), *newv = calloc(ncols ? ncols : 1, sizeof(GsBytes));
    if (!oldv || !newv) {
        abort();
    }
    static const unsigned char unknown = SER_UNKNOWN;
    GsqlBuf row = {0};
    for (size_t k = 0; any && rc == GSQL_OK && k < n; k++) {
        rc = gsql_row_get(x->e, x->t, tb, (GsBytes){ ch[k].key.bytes, ch[k].key.length }, &row, x->err);
        for (size_t c = 0; rc == GSQL_OK && c < ncols; c++) {
            rc = gsql_row_value(tb, row.bytes, row.length, c, &oldv[c], x->err);
            newv[c] = oldv[c];
        }
        if (rc != GSQL_OK) {
            break;
        }
        if (s->kind == SQ_DELETE) {
            rc = fk_check_parent(x, &fks, tb, oldv);
            continue;
        }
        for (size_t j = 0; j < p->ntargets; j++) {
            const GsqlColumn *col = gsql_table_col(tb, p->targets[j]);
            if (ch[k].deflt[j]) {
                newv[p->targets[j]].p = col->def ? col->def : &unknown;
                newv[p->targets[j]].n = col->def ? col->def_len : 1;
            } else {
                newv[p->targets[j]].p = ch[k].vals[j].bytes;
                newv[p->targets[j]].n = ch[k].vals[j].length;
            }
        }
        rc = ck_check_row(x, &cks, tb, newv);
        if (rc == GSQL_OK) {
            rc = fk_check_child(x, &fks, tb, newv);
        }
        /* the key changed: what referenced the old one must not be left */
        size_t nkey = gsql_table_nkey(tb);
        const unsigned *key = gsql_table_key_cols(tb);
        int moved = 0;
        for (size_t i = 0; i < nkey; i++) {
            moved |= oldv[key[i]].n != newv[key[i]].n || memcmp(oldv[key[i]].p, newv[key[i]].p, oldv[key[i]].n) != 0;
        }
        if (rc == GSQL_OK && moved) {
            rc = fk_check_parent(x, &fks, tb, oldv);
        }
    }
    gsql_buf_free(&row);
    free(oldv);
    free(newv);
    fk_list_free(&fks);
    ck_set_free(&cks);
    return rc;
}

/* a serial counter an UPDATE moves up once its rows are written */
typedef struct {
    size_t col;
    GsqlBuf scope;
    uint64_t v;
} Bump;

static void bumps_free(Bump *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        gsql_buf_free(&b[i].scope);
    }
    free(b);
}

/* An UPDATE's serials (docs/gsql_generated_design.md §2): a row moved to
 * another scope is refused unless the same UPDATE sets the serial, and a
 * serial it sets moves its scope's counter up. */
static int gen_check_updates(X *x, GsqlTable *tb, const SqlPlan *p, const Change *ch, size_t n, Bump **bumps,
                             size_t *nbumps) {
    *bumps = NULL;
    *nbumps = 0;
    GenSet gs;
    int rc = gen_load(x, tb, &gs);
    size_t ncols = gsql_table_ncols(tb);
    if (rc != GSQL_OK || !gs.nser) {
        gen_set_free(&gs, ncols);
        return rc;
    }
    int *targeted = calloc(ncols, sizeof(int));
    GsBytes *oldv = calloc(ncols, sizeof(GsBytes)), *newv = calloc(ncols, sizeof(GsBytes));
    if (!targeted || !oldv || !newv) {
        abort();
    }
    for (size_t j = 0; j < p->ntargets; j++) {
        targeted[p->targets[j]] = 1;
    }
    static const unsigned char unknown = SER_UNKNOWN;
    GsqlBuf row = {0}, before = {0};
    size_t cap = 0;
    for (size_t k = 0; rc == GSQL_OK && k < n; k++) {
        rc = gsql_row_get(x->e, x->t, tb, (GsBytes){ ch[k].key.bytes, ch[k].key.length }, &row, x->err);
        for (size_t c = 0; rc == GSQL_OK && c < ncols; c++) {
            rc = gsql_row_value(tb, row.bytes, row.length, c, &oldv[c], x->err);
            newv[c] = oldv[c];
        }
        for (size_t j = 0; rc == GSQL_OK && j < p->ntargets; j++) {
            const GsqlColumn *col = gsql_table_col(tb, p->targets[j]);
            if (ch[k].deflt[j]) {
                newv[p->targets[j]].p = col->def ? col->def : &unknown;
                newv[p->targets[j]].n = col->def ? col->def_len : 1;
            } else {
                newv[p->targets[j]].p = ch[k].vals[j].bytes;
                newv[p->targets[j]].n = ch[k].vals[j].length;
            }
        }
        for (size_t i = 0; rc == GSQL_OK && i < gs.nser; i++) {
            GenSer *sr = &gs.ser[i];
            rc = gen_scope(x, tb, sr, oldv);
            if (rc != GSQL_OK) {
                break;
            }
            v3_set(&before, sr->scope.bytes, sr->scope.length);
            rc = gen_scope(x, tb, sr, newv);
            if (rc != GSQL_OK) {
                break;
            }
            int moved = before.length != sr->scope.length ||
                        (before.length && memcmp(before.bytes, sr->scope.bytes, before.length) != 0);
            const char *cname = gsql_table_col(tb, sr->col)->name;
            if (moved && !targeted[sr->col]) {
                rc = xfail(x->err, GSQL_SERIAL, 0, "this UPDATE moves a row of `%s` to another scope of serial `%s`, "
                           "whose number belongs to the old one: set `%s` in the same UPDATE to number it in the new "
                           "scope", gsql_table_name(tb), cname, cname);
                break;
            }
            uint64_t v;
            if (targeted[sr->col] && v3_read_u64(newv[sr->col].p, newv[sr->col].n, &v)) {
                if (*nbumps == cap) {
                    cap = cap ? cap * 2 : 16;
                    *bumps = realloc(*bumps, cap * sizeof(Bump));
                    if (!*bumps) {
                        abort();
                    }
                }
                Bump *b = &(*bumps)[(*nbumps)++];
                memset(b, 0, sizeof *b);
                b->col = sr->col;
                b->v = v;
                v3_set(&b->scope, sr->scope.bytes, sr->scope.length);
            }
        }
    }
    gsql_buf_free(&row);
    gsql_buf_free(&before);
    free(targeted);
    free(oldv);
    free(newv);
    gen_set_free(&gs, ncols);
    return rc;
}

static int bumps_apply(X *x, GsqlTable *tb, const Bump *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint64_t hw;
        GsBytes scope = { b[i].scope.bytes, b[i].scope.length };
        int rc = gsql_serial_get(x->e, x->t, tb, b[i].col, scope, &hw, x->err);
        if (rc == GSQL_OK && b[i].v > hw) {
            rc = gsql_serial_set(x->e, x->t, tb, b[i].col, scope, b[i].v, x->err);
        }
        if (rc != GSQL_OK) {
            return rc;
        }
    }
    return GSQL_OK;
}

static int run_update_delete(GsqlSql *q, X *x, GsqlResult *out) {
    SqlPlan *p = x->p;
    SqlStmt *s = p->s;
    GsqlError *err = x->err;
    Iter it;
    int rc = iter_open(&it, x, 0);
    if (rc != GSQL_OK) {
        iter_close(&it);
        return rc;
    }
    /* every row first, then every change: a change cannot move a row into
     * the walk's way (the Halloween problem) */
    Change *ch = NULL;
    size_t n = 0, cap = 0;
    int r;
    while ((r = next_kept(&it)) == 1) {
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            ch = realloc(ch, sizeof(Change) * cap);
            if (!ch) {
                abort();
            }
        }
        Change *c = &ch[n++];
        memset(c, 0, sizeof *c);
        v3_put(&c->key, x->sx[0].rowkey.bytes, x->sx[0].rowkey.length);
        if (s->kind == SQ_UPDATE) {
            c->vals = calloc(p->ntargets, sizeof(GsqlBuf));
            c->deflt = calloc(p->ntargets, sizeof(int));
            if (!c->vals || !c->deflt) {
                abort();
            }
            for (size_t k = 0; k < p->ntargets; k++) {
                if (!s->sets[k]) {
                    c->deflt[k] = 1;
                } else if (eval_into(x, s->sets[k], &c->vals[k]) != GSQL_OK) {
                    r = -1;
                    break;
                }
            }
            if (r < 0) {
                break;
            }
        }
    }
    iter_close(&it);
    row_clear(x);
    if (r < 0) {
        changes_free(ch, n, p->ntargets);
        return err->code ? err->code : GSQL_EXPRESSION;
    }
    size_t ncols = gsql_table_ncols(p->tb);
    rc = fk_check_changes(x, p->tb, s, ch, n, p);
    Bump *bumps = NULL;
    size_t nbumps = 0;
    if (rc == GSQL_OK && s->kind == SQ_UPDATE) {
        rc = gen_check_updates(x, p->tb, p, ch, n, &bumps, &nbumps);
    }
    if (rc != GSQL_OK) {
        bumps_free(bumps, nbumps);
        changes_free(ch, n, p->ntargets);
        return rc;
    }
    GsqlIn *vals = calloc(ncols ? ncols : 1, sizeof(GsqlIn));
    if (!vals) {
        abort();
    }
    unsigned char unknown = SER_UNKNOWN;
    uint64_t done = 0;
    rc = GSQL_OK;
    for (size_t k = 0; k < n && rc == GSQL_OK; k++) {
        GsBytes key = { ch[k].key.bytes, ch[k].key.length };
        if (s->kind == SQ_DELETE) {
            rc = gsql_row_delete(x->e, x->t, p->tb, key, err);
        } else {
            memset(vals, 0, sizeof(GsqlIn) * ncols);
            for (size_t j = 0; j < p->ntargets; j++) {
                const GsqlColumn *col = gsql_table_col(p->tb, p->targets[j]);
                GsqlIn *in = &vals[p->targets[j]];
                in->present = 1;
                if (ch[k].deflt[j]) {
                    in->p = col->def ? col->def : &unknown;
                    in->n = col->def ? col->def_len : 1;
                } else {
                    in->p = ch[k].vals[j].bytes;
                    in->n = ch[k].vals[j].length;
                }
            }
            rc = gsql_row_update(x->e, x->t, p->tb, key, vals, NULL, err);
        }
        if (rc == GSQL_OK) {
            done++;
        }
    }
    free(vals);
    changes_free(ch, n, p->ntargets);
    if (rc == GSQL_OK) {
        rc = bumps_apply(x, p->tb, bumps, nbumps);
    }
    bumps_free(bumps, nbumps);
    if (rc != GSQL_OK) {
        out->failed_part_way = done > 0;
        return rc;
    }
    (void)q;
    out->has_affected = 1;
    out->affected = done;
    return GSQL_OK;
}

static SqlCursor *sub_open(X *x, SqlPlan *p, X *outer);

/* INSERT ... SELECT (docs/gsql_insert_select_design.md): the query's rows,
 * every one read before any is written, so a table copied into itself never
 * meets its own new rows. */
static int insert_rows(X *x, GsqlBuf **out, size_t *n) {
    *out = NULL;
    *n = 0;
    SqlCursor *c = sub_open(x, x->p->left, NULL);
    if (!c) {
        return x->err->code ? x->err->code : GSQL_EXPRESSION;
    }
    size_t cap = 0;
    GsqlBuf row = {0};
    int rc = GSQL_OK;
    for (;;) {
        int r = cursor_next(c, &row);
        if (r < 0) {
            *x->err = c->err;
            rc = x->err->code ? x->err->code : GSQL_EXPRESSION;
            break;
        }
        if (r == 0) {
            break;
        }
        if (*n == cap) {
            cap = cap ? cap * 2 : 64;
            *out = realloc(*out, sizeof(GsqlBuf) * cap);
            if (!*out) {
                abort();
            }
        }
        memset(&(*out)[*n], 0, sizeof(GsqlBuf));
        v3_put(&(*out)[*n], row.bytes, row.length);
        (*n)++;
    }
    gsql_buf_free(&row);
    cursor_free(c);
    return rc;
}

static int run_insert(X *x, GsqlResult *out) {
    SqlPlan *p = x->p;
    SqlStmt *s = p->s;
    GsqlBuf *qrows = NULL;
    size_t nq = 0;
    if (s->ins_q) {
        int qrc = insert_rows(x, &qrows, &nq);
        if (qrc != GSQL_OK) {
            for (size_t k = 0; k < nq; k++) {
                gsql_buf_free(&qrows[k]);
            }
            free(qrows);
            return qrc;
        }
    }
    size_t nrows = s->ins_q ? nq : s->nrows;
    GsBytes *items = calloc(p->ntargets ? p->ntargets : 1, sizeof(GsBytes));
    if (!items) {
        abort();
    }
    size_t ncols = gsql_table_ncols(p->tb);
    GsqlIn *vals = calloc(ncols ? ncols : 1, sizeof(GsqlIn));
    GsqlBuf *bufs = calloc(p->ntargets ? p->ntargets : 1, sizeof(GsqlBuf));
    if (!vals || !bufs) {
        abort();
    }
    uint64_t done = 0;
    int rc = GSQL_OK;
    FkList fks;
    rc = fk_all(x, &fks);
    CkSet cks;
    if (rc == GSQL_OK) {
        rc = ck_load(x, s->table, &cks);
    } else {
        memset(&cks, 0, sizeof cks);
    }
    GsBytes *whole = calloc(ncols ? ncols : 1, sizeof(GsBytes));
    int *omitted = calloc(ncols ? ncols : 1, sizeof(int));
    if (!whole || !omitted) {
        abort();
    }
    GenSet gs;
    memset(&gs, 0, sizeof gs);
    if (rc == GSQL_OK) {
        rc = gen_load(x, p->tb, &gs);
    }
    for (size_t r = 0; r < nrows && rc == GSQL_OK; r++) {
        memset(vals, 0, sizeof(GsqlIn) * ncols);
        if (s->ins_q) {
            if (v3_array_items(qrows[r].bytes, qrows[r].length, items, (long)p->ntargets) != (long)p->ntargets) {
                rc = xfail(x->err, GSQL_MISUSE, 0, "a row of the query did not hold its columns");
            }
            for (size_t k = 0; k < p->ntargets && rc == GSQL_OK; k++) {
                vals[p->targets[k]].present = 1;
                vals[p->targets[k]].p = items[k].p;
                vals[p->targets[k]].n = items[k].n;
            }
        }
        for (size_t k = 0; !s->ins_q && k < p->ntargets && rc == GSQL_OK; k++) {
            bufs[k].length = 0;
            if (!s->rows[r][k]) {
                continue;   /* DEFAULT: omitted, so the column's default */
            }
            rc = eval_into(x, s->rows[r][k], &bufs[k]);
            vals[p->targets[k]].present = 1;
            vals[p->targets[k]].p = bufs[k].bytes;
            vals[p->targets[k]].n = bufs[k].length;
        }
        for (size_t c = 0; c < ncols; c++) {
            omitted[c] = !vals[c].present;
        }
        /* a random value a key already holds is drawn again (§3) */
        int at_insert = 0;
        for (int attempt = 1; rc == GSQL_OK; attempt++) {
            for (size_t c = 0; c < ncols; c++) {
                if (omitted[c]) {
                    vals[c].present = 0;
                }
            }
            rc = gen_fill(x, p->tb, &gs, vals, whole, omitted);
            if (rc == GSQL_OK && (fks.n || cks.n)) {
                rc = ck_check_row(x, &cks, p->tb, whole);
                if (rc == GSQL_OK) {
                    rc = fk_check_child(x, &fks, p->tb, whole);
                }
            }
            if (rc == GSQL_OK) {
                rc = gsql_row_insert(x->e, x->t, p->tb, vals, NULL, x->err);
                at_insert = rc != GSQL_OK;
            }
            if (rc != GSQL_DUPLICATE || !gen_redraws(p->tb, &gs, omitted, x->err->dup)) {
                break;
            }
            if (attempt == GEN_DRAWS) {
                /* the space is nearly full: say how large it is */
                const GsqlColumn *col = NULL;
                for (size_t i = 0; i < gs.nrnd && !col; i++) {
                    if (omitted[gs.rnd[i]]) {
                        col = gsql_table_col(p->tb, gs.rnd[i]);
                    }
                }
                char m[sizeof x->err->message];
                uint64_t space = random_space(col);
                char room[64];
                if (space) {
                    snprintf(room, sizeof room, "%llu", (unsigned long long)space);
                } else {
                    snprintf(room, sizeof room, "more than 10^18");
                }
                snprintf(m, sizeof m, "%.300s: random `%s` can hold %s values, and %d draws in a row were all taken",
                         x->err->message, col ? col->name : "?", room, GEN_DRAWS);
                snprintf(x->err->message, sizeof x->err->message, "%s", m);
                break;
            }
            rc = GSQL_OK;
        }
        if (rc == GSQL_OK) {
            rc = gen_commit(x, p->tb, &gs);
        }
        if (rc != GSQL_OK && at_insert) {
            if (nrows > 1) {
                char m[sizeof x->err->message];
                snprintf(m, sizeof m, "row %zu of %s: %.400s", r + 1, s->ins_q ? "the query" : "VALUES",
                         x->err->message);
                snprintf(x->err->message, sizeof x->err->message, "%s", m);
            }
        }
        if (rc == GSQL_OK) {
            done++;
        }
    }
    for (size_t k = 0; k < p->ntargets; k++) {
        gsql_buf_free(&bufs[k]);
    }
    free(bufs);
    free(vals);
    free(whole);
    free(omitted);
    gen_set_free(&gs, ncols);
    fk_list_free(&fks);
    ck_set_free(&cks);
    free(items);
    for (size_t k = 0; k < nq; k++) {
        gsql_buf_free(&qrows[k]);
    }
    free(qrows);
    if (rc != GSQL_OK) {
        out->failed_part_way = done > 0;
        return rc;
    }
    out->has_affected = 1;
    out->affected = done;
    return GSQL_OK;
}

/* ---- schema --------------------------------------------------------------- */

/* every table's name, to find an index by name */
typedef struct {
    char **names;
    size_t n;
} Names;

static int collect_name(void *ctx, const char *name) {
    Names *v = ctx;
    v->names = realloc(v->names, sizeof(char *) * (v->n + 1));
    if (!v->names) {
        abort();
    }
    v->names[v->n] = malloc(strlen(name) + 1);
    if (!v->names[v->n]) {
        abort();
    }
    strcpy(v->names[v->n++], name);
    return 0;
}

static void names_free(Names *v) {
    for (size_t k = 0; k < v->n; k++) {
        free(v->names[k]);
    }
    free(v->names);
}

/* The table holding index `name`, opened, and the index's position: 1 found,
 * 0 not, -1 failed. */
static int find_index(X *x, const char *name, GsqlTable **tb, size_t *at) {
    Names v = { NULL, 0 };
    if (gsql_table_list(x->e, x->t, collect_name, &v, x->err) != GSQL_OK) {
        names_free(&v);
        return -1;
    }
    int found = 0;
    for (size_t k = 0; k < v.n && !found; k++) {
        GsqlTable *t;
        if (gsql_table_open(x->e, x->t, v.names[k], &t, x->err) != GSQL_OK) {
            names_free(&v);
            return -1;
        }
        for (size_t i = 0; i < gsql_table_nindexes(t); i++) {
            if (strcmp(gsql_table_index(t, i)->name, name) == 0) {
                *tb = t;
                *at = i;
                found = 1;
                break;
            }
        }
        if (!found) {
            gsql_table_close(t);
        }
    }
    names_free(&v);
    return found;
}

static int default_bytes(X *x, const SqlColDef *d, GsqlBuf *out) {
    if (!d->def) {
        return GSQL_OK;
    }
    return eval_into(x, d->def, out);
}

static int create_indexes(X *x, const SqlStmt *s, int *made_any) {
    GsqlTable *tb;
    int rc = gsql_table_open(x->e, x->t, s->table, &tb, x->err);
    if (rc != GSQL_OK) {
        return rc;
    }
    /* column-level UNIQUE first, then table-level, each named as PostgreSQL
     * names it: <table>_<columns>_key */
    for (size_t pass = 0; pass < s->ncoldefs + s->nuniques && rc == GSQL_OK; pass++) {
        const char *one[1];
        const char *const *cols;
        size_t n;
        if (pass < s->ncoldefs) {
            if (!s->coldefs[pass].unique) {
                continue;
            }
            one[0] = s->coldefs[pass].name;
            cols = one;
            n = 1;
        } else {
            cols = (const char *const *)s->uniques[pass - s->ncoldefs].cols;
            n = s->uniques[pass - s->ncoldefs].n;
        }
        char name[300];
        int at = snprintf(name, sizeof name, "%s", s->table);
        unsigned pos[256];
        for (size_t k = 0; k < n; k++) {
            at += snprintf(name + at, sizeof name - (size_t)(at < (int)sizeof name ? at : (int)sizeof name), "_%s",
                           cols[k]);
            long c = gsql_table_find(tb, cols[k]);
            if (c < 0) {
                gsql_table_close(tb);
                return xfail(x->err, GSQL_NOT_FOUND, s->pos, "UNIQUE names `%s`, which is not a column of %s", cols[k],
                             s->table);
            }
            pos[k] = (unsigned)c;
        }
        snprintf(name + (at < (int)sizeof name - 4 ? at : (int)sizeof name - 5), 5, "_key");
        GsqlIndexDecl d = { name, n, pos, NULL, 1 };
        rc = gsql_index_create(x->e, x->t, tb, &d, x->err);
        if (rc == GSQL_OK) {
            *made_any = 1;
        }
    }
    gsql_table_close(tb);
    return rc;
}

/* ---- views (docs/gsql_view_design.md) -------------------------------------
 *
 * A view is stored as { format, text, declared, columns, ok }: its SELECT's
 * canonical text, the column list written in CREATE VIEW, and the columns and
 * whether it ran at its last check. After every schema change every view is
 * planned again, and a warning (2118) says what THIS change did: a view that
 * ran and now does not, or whose columns changed. */

typedef struct {
    char *text;
    char **declared;
    size_t ndeclared;
    char **columns;
    size_t ncolumns;
    int ok;
} ViewRec;

static void view_rec_free(ViewRec *v) {
    free(v->text);
    for (size_t k = 0; k < v->ndeclared; k++) {
        free(v->declared[k]);
    }
    free(v->declared);
    for (size_t k = 0; k < v->ncolumns; k++) {
        free(v->columns[k]);
    }
    free(v->columns);
    memset(v, 0, sizeof *v);
}

static char **text_list(const V3Field *f, size_t *n) {
    GsBytes items[256];
    long m = f ? v3_array_items(f->v, f->vlen, items, 256) : 0;
    *n = m > 0 ? (size_t)m : 0;
    char **out = calloc(*n ? *n : 1, sizeof(char *));
    if (!out) {
        abort();
    }
    for (size_t k = 0; k < *n; k++) {
        out[k] = v3_read_text(items[k].p, items[k].n);
    }
    return out;
}

static int view_rec_read(const GsqlBuf *b, ViewRec *v) {
    memset(v, 0, sizeof *v);
    V3Field f[8];
    long n = v3_record_fields(b->bytes, b->length, f, 8);
    const V3Field *t = n > 0 ? v3_field_named(f, n, "text") : NULL;
    const V3Field *ok = n > 0 ? v3_field_named(f, n, "ok") : NULL;
    if (!t) {
        return 0;
    }
    v->text = v3_read_text(t->v, t->vlen);
    v->declared = text_list(v3_field_named(f, n, "declared"), &v->ndeclared);
    v->columns = text_list(v3_field_named(f, n, "columns"), &v->ncolumns);
    if (ok) {
        v3_read_bool(ok->v, ok->vlen, &v->ok);
    }
    return v->text != NULL;
}

static void view_rec_write(GsqlBuf *out, const char *text, char *const *declared, size_t ndeclared,
                           const char *const *columns, size_t ncolumns, int ok) {
    out->length = 0;
    v3_w_record(out, 5);
    v3_w_name(out, "format");
    v3_w_int(out, 1);
    v3_w_name(out, "text");
    v3_w_text(out, text);
    v3_w_name(out, "declared");
    v3_w_array(out, ndeclared);
    for (size_t k = 0; k < ndeclared; k++) {
        v3_w_text(out, declared[k]);
    }
    v3_w_name(out, "columns");
    v3_w_array(out, ncolumns);
    for (size_t k = 0; k < ncolumns; k++) {
        v3_w_text(out, columns[k]);
    }
    v3_w_name(out, "ok");
    v3_w_bool(out, ok);
}

/* a list of columns said as (a, b, c) */
static void say_columns(char *out, size_t cap, const char *const *c, size_t n) {
    size_t used = 0;
    used += (size_t)snprintf(out, cap, "(");
    for (size_t k = 0; k < n && used < cap; k++) {
        used += (size_t)snprintf(out + used, cap - used, "%s%s", k ? ", " : "", c[k]);
    }
    if (used < cap) {
        snprintf(out + used, cap - used, ")");
    }
}

static int collect_view(void *ctx, const char *name) {
    struct { char **v; size_t n; } *l = ctx;
    char **g = realloc(l->v, sizeof *g * (l->n + 1));
    if (!g) {
        abort();
    }
    l->v = g;
    size_t len = strlen(name);
    l->v[l->n] = malloc(len + 1);
    if (!l->v[l->n]) {
        abort();
    }
    memcpy(l->v[l->n++], name, len + 1);
    return 0;
}

/* Every view, planned again after a schema change in the same transaction. */
static int recheck_views(X *x) {
    struct { char **v; size_t n; } names = { NULL, 0 };
    GsqlError lerr = { GSQL_OK, "", 0, 0 };
    int rc = gsql_view_list(x->e, x->t, collect_view, &names, &lerr);
    for (size_t k = 0; k < names.n && rc == GSQL_OK; k++) {
        GsqlBuf rec = {0};
        ViewRec old;
        if (gsql_view_get(x->e, x->t, names.v[k], &rec, &lerr) != GSQL_OK || !view_rec_read(&rec, &old)) {
            gsql_buf_free(&rec);
            continue;
        }
        SqlParsed vp;
        SqlPlan plan;
        memset(&plan, 0, sizeof plan);
        GsqlError verr = { GSQL_OK, "", 0, 0 };
        int nowok = sql_parse(old.text, strlen(old.text), &vp, &verr) == GSQL_OK;
        int planned = nowok && sql_plan(x->e, x->t, vp.stmt, &vp.arena, old.text, 0, &plan, &verr) == GSQL_OK;
        nowok = planned;
        const char *const *cols = NULL;
        size_t ncols = 0;
        if (nowok) {
            if (old.ndeclared && old.ndeclared != plan.nout) {
                nowok = 0;
                snprintf(verr.message, sizeof verr.message, "it names %zu column%s, and its query now has %zu",
                         old.ndeclared, old.ndeclared == 1 ? "" : "s", plan.nout);
            } else if (old.ndeclared) {
                cols = (const char *const *)old.declared;
                ncols = old.ndeclared;
            } else {
                cols = plan.names;
                ncols = plan.nout;
            }
        }
        int same = nowok && ncols == old.ncolumns;
        for (size_t c = 0; same && c < ncols; c++) {
            same = strcmp(cols[c], old.columns[c]) == 0;
        }
        char msg[700];
        if (old.ok && !nowok) {
            /* always naming the view checked: two views broken by one change,
               one through the other, must not say one sentence */
            snprintf(msg, sizeof msg, "view `%s` cannot run: %s", names.v[k], verr.message);
            sql_warn(x->q, 2118, msg);
        } else if (old.ok && nowok && !same) {
            char was[300], now[300];
            say_columns(was, sizeof was, (const char *const *)old.columns, old.ncolumns);
            say_columns(now, sizeof now, cols, ncols);
            snprintf(msg, sizeof msg, "view `%s`'s columns are now %s, where they were %s", names.v[k], now, was);
            sql_warn(x->q, 2118, msg);
        }
        if (nowok != old.ok || (nowok && !same)) {
            GsqlBuf upd = {0};
            view_rec_write(&upd, old.text, old.declared, old.ndeclared,
                           nowok ? cols : (const char *const *)old.columns, nowok ? ncols : old.ncolumns, nowok);
            rc = gsql_view_put(x->e, x->t, names.v[k], upd.bytes, upd.length, 1, x->err);
            gsql_buf_free(&upd);
        }
        if (planned) {
            sql_plan_free(&plan);
        }
        sql_parsed_free(&vp);
        view_rec_free(&old);
        gsql_buf_free(&rec);
    }
    for (size_t k = 0; k < names.n; k++) {
        free(names.v[k]);
    }
    free(names.v);
    return rc;
}

/* ---- foreign keys, the schema side (docs/gsql_fkey_design.md) ---------- */

/* A foreign key declared on `table`: named, its referenced columns resolved
 * to the parent's primary key where none were written, bound now, and, when
 * the table already has rows and the key is enforced, every row checked. */
static int fk_create(X *x, const char *table, const SqlFkey *d) {
    FkRec k;
    memset(&k, 0, sizeof k);
    char gen[600];
    if (d->name) {
        snprintf(gen, sizeof gen, "%s", d->name);
    } else {
        size_t used = (size_t)snprintf(gen, sizeof gen, "%s", table);
        for (size_t i = 0; i < d->cols.n && used < sizeof gen; i++) {
            used += (size_t)snprintf(gen + used, sizeof gen - used, "_%s", d->cols.cols[i]);
        }
        if (used < sizeof gen) {
            snprintf(gen + used, sizeof gen - used, "_fkey");
        }
    }
    GsqlBuf b = {0};
    GsqlError e = { GSQL_OK, "", 0, 0 };
    int got = gsql_fkey_get(x->e, x->t, gen, &b, &e);
    gsql_buf_free(&b);
    if (got == GSQL_OK) {
        return xfail(x->err, GSQL_EXISTS, d->pos, "there is already a constraint %s%s", gen,
                     d->name ? "" : ": name this one with CONSTRAINT name");
    }
    k.name = dup_cstr(gen);
    k.table = dup_cstr(table);
    k.ref = dup_cstr(d->ref);
    k.enforced = !d->not_enforced;
    k.ncols = d->cols.n;
    k.cols = calloc(k.ncols ? k.ncols : 1, sizeof(char *));
    if (!k.cols) {
        abort();
    }
    for (size_t i = 0; i < k.ncols; i++) {
        k.cols[i] = dup_cstr(d->cols.cols[i]);
    }
    int rc = GSQL_OK;
    GsqlTable *parent = NULL, *child = NULL;
    if (gsql_table_open(x->e, x->t, d->ref, &parent, &e) != GSQL_OK) {
        rc = xfail(x->err, GSQL_NOT_FOUND, d->pos, "foreign key `%s` references `%s`, and there is no table `%s`",
                   gen, d->ref, d->ref);
        parent = NULL;
    } else if (d->ref_cols.n) {
        k.nref = d->ref_cols.n;
        k.ref_cols = calloc(k.nref, sizeof(char *));
        if (!k.ref_cols) {
            abort();
        }
        for (size_t i = 0; i < k.nref; i++) {
            k.ref_cols[i] = dup_cstr(d->ref_cols.cols[i]);
        }
    } else {
        /* REFERENCES t alone: t's primary key */
        k.nref = gsql_table_nkey(parent);
        k.ref_cols = calloc(k.nref ? k.nref : 1, sizeof(char *));
        if (!k.ref_cols) {
            abort();
        }
        for (size_t i = 0; i < k.nref; i++) {
            k.ref_cols[i] = dup_cstr(gsql_table_col(parent, gsql_table_key_cols(parent)[i])->name);
        }
    }
    if (parent) {
        gsql_table_close(parent);
        parent = NULL;
    }
    if (rc == GSQL_OK && k.nref != k.ncols) {
        rc = xfail(x->err, GSQL_SCHEMA, d->pos, "foreign key `%s` names %zu column%s, and references %zu", gen, k.ncols,
                   k.ncols == 1 ? "" : "s", k.nref);
    }
    if (rc == GSQL_OK && gsql_table_open(x->e, x->t, table, &child, &e) != GSQL_OK) {
        child = NULL;
        rc = xfail(x->err, GSQL_NOT_FOUND, d->pos, "there is no table %s", table);
    }
    unsigned cpos[64], ppos[64];
    char why[300];
    if (rc == GSQL_OK && (k.ncols > 64 || !fk_bind(x, &k, child, &parent, cpos, ppos, why, sizeof why))) {
        rc = xfail(x->err, GSQL_SCHEMA, d->pos, "foreign key `%s` cannot reference `%s`: %s, and a foreign key "
                   "references a primary key with columns of the same kinds", gen, k.ref,
                   k.ncols > 64 ? "it has more than 64 columns" : why);
    }
    if (rc == GSQL_OK && k.enforced) {
        uint64_t orphans = 0;
        rc = fk_walk(x, child, cpos, parent, ppos, k.ncols, NULL, &orphans);
        if (rc == GSQL_OK && orphans) {
            rc = xfail(x->err, GSQL_FOREIGN_KEY, d->pos, "foreign key `%s`: %llu row%s of `%s` reference%s no row of "
                       "`%s`; fix them, or declare it NOT ENFORCED", gen, (unsigned long long)orphans,
                       orphans == 1 ? "" : "s", table, orphans == 1 ? "s" : "", k.ref);
        }
    }
    if (parent) {
        gsql_table_close(parent);
    }
    if (child) {
        gsql_table_close(child);
    }
    if (rc == GSQL_OK) {
        k.bound = 1;
        rc = fk_put(x, &k, 0);
    }
    fk_free(&k);
    return rc;
}

/* After a schema change: each foreign key bound, or not, to what is there
 * now, and a warning (2119) for what THIS change did to an enforced one. */
static int recheck_fkeys(X *x) {
    FkList fks;
    int rc = fk_all(x, &fks);
    for (size_t i = 0; i < fks.n && rc == GSQL_OK; i++) {
        FkRec *k = &fks.v[i];
        GsqlTable *child = NULL, *parent = NULL;
        GsqlError e = { GSQL_OK, "", 0, 0 };
        if (gsql_table_open(x->e, x->t, k->table, &child, &e) != GSQL_OK) {
            continue;   /* its own table went: dropping it dropped the key */
        }
        unsigned cpos[64], ppos[64];
        char why[300], msg[700];
        int now = k->ncols <= 64 && fk_bind(x, k, child, &parent, cpos, ppos, why, sizeof why);
        if (k->bound && !now && k->enforced) {
            snprintf(msg, sizeof msg, "foreign key `%s` on `%s` is not checked from here on: %s; it is checked again "
                     "when `%s` has that primary key", k->name, k->table, why, k->ref);
            sql_warn(x->q, 2119, msg);
        } else if (!k->bound && now && k->enforced) {
            uint64_t orphans = 0;
            rc = fk_walk(x, child, cpos, parent, ppos, k->ncols, NULL, &orphans);
            if (rc == GSQL_OK && orphans) {
                snprintf(msg, sizeof msg, "foreign key `%s` on `%s` is checked again, and %llu row%s of `%s` "
                         "reference%s no row of `%s`", k->name, k->table, (unsigned long long)orphans,
                         orphans == 1 ? "" : "s", k->table, orphans == 1 ? "s" : "", k->ref);
                sql_warn(x->q, 2119, msg);
            }
        }
        if (parent) {
            gsql_table_close(parent);
        }
        gsql_table_close(child);
        if (rc == GSQL_OK && now != k->bound) {
            k->bound = now;
            rc = fk_put(x, k, 1);
        }
    }
    fk_list_free(&fks);
    return rc;
}

/* A child table dropped takes its foreign keys with it; one renamed carries
 * them, a foreign key belonging to its table. */
static int fk_follow(X *x, const char *table, const char *renamed) {
    FkList fks;
    int rc = fk_all(x, &fks);
    for (size_t i = 0; i < fks.n && rc == GSQL_OK; i++) {
        FkRec *k = &fks.v[i];
        if (strcmp(k->table, table) != 0) {
            continue;
        }
        if (renamed) {
            free(k->table);
            k->table = dup_cstr(renamed);
            rc = fk_put(x, k, 1);
        } else {
            rc = gsql_fkey_drop(x->e, x->t, k->name, x->err);
        }
    }
    fk_list_free(&fks);
    return rc;
}

/* ---- CHECK, the schema side (docs/gsql_check_design.md) ---------------- */

static SqlCursor *sub_open(X *x, SqlPlan *p, X *outer);

/* How many of a table's rows a planned check refuses, by running it as the
 * query it is planned as; a check that raises on a row is that raise. */
static int ck_failing(X *x, CkPlan *cp, uint64_t *count) {
    *count = 0;
    SqlCursor *c = sub_open(x, &cp->plan, NULL);
    if (!c) {
        return x->err->code ? x->err->code : GSQL_EXPRESSION;
    }
    GsqlBuf row = {0};
    int rc = GSQL_OK;
    for (;;) {
        int r = cursor_next(c, &row);
        if (r < 0) {
            *x->err = c->err;
            rc = x->err->code ? x->err->code : GSQL_EXPRESSION;
            break;
        }
        if (r == 0) {
            break;
        }
        GsBytes item;
        if (v3_array_items(row.bytes, row.length, &item, 1) == 1) {
            GsqlHv *v = hv_bytes(x, item.p, item.n);
            if (v) {
                *count += x->h->truth(x->h->ctx, v) == 0;
                rel(x, v);
            }
        }
    }
    gsql_buf_free(&row);
    cursor_free(c);
    return rc;
}

static int ck_create(X *x, const char *table, const SqlCheck *d) {
    char gen[600];
    if (d->name) {
        snprintf(gen, sizeof gen, "%s", d->name);
    } else if (d->column) {
        snprintf(gen, sizeof gen, "%s_%s_check", table, d->column);
    } else {
        snprintf(gen, sizeof gen, "%s_check", table);
    }
    GsqlBuf b = {0};
    GsqlError e = { GSQL_OK, "", 0, 0 };
    int got = gsql_fkey_get(x->e, x->t, gen, &b, &e);
    gsql_buf_free(&b);
    if (got == GSQL_OK) {
        return xfail(x->err, GSQL_EXISTS, d->pos, "there is already a constraint %s%s", gen,
                     d->name ? "" : ": name this one with CONSTRAINT name");
    }
    if (d->e && d->e->kind == SX_LIT && d->e->litlen && d->e->lit[0] != SER_BOOL && d->e->lit[0] != SER_UNKNOWN &&
        d->e->lit[0] != SER_NULL) {
        return xfail(x->err, GSQL_SCHEMA, d->pos, "check `%s` is a constant that is not a condition", gen);
    }
    CkRec rec = { dup_cstr(gen), dup_cstr(table), dup_cstr(d->text), 1 };
    CkPlan cp;
    char why[400];
    int rc = GSQL_OK;
    if (!ck_plan(x, table, d->text, &cp, why, sizeof why)) {
        rc = xfail(x->err, GSQL_SCHEMA, d->pos, "check `%s` cannot be applied to `%s`: %s", gen, table, why);
    } else {
        uint64_t failing = 0;
        rc = ck_failing(x, &cp, &failing);
        if (rc == GSQL_OK && failing) {
            rc = xfail(x->err, GSQL_CHECK, d->pos, "check `%s`: %llu row%s of `%s` %s it; fix them first", gen,
                       (unsigned long long)failing, failing == 1 ? "" : "s", table, failing == 1 ? "fails" : "fail");
        }
    }
    ck_plan_free(&cp);
    if (rc == GSQL_OK) {
        rc = ck_put(x, &rec, 0);
    }
    ck_free(&rec);
    return rc;
}

/* A check follows its table: dropped with it, renamed with it. Since it reads
 * only its own table and no statement removes a column, nothing else can stop
 * it applying, so unlike a view or a foreign key it needs no re-check. */
static int ck_follow(X *x, const char *table, const char *renamed) {
    CkList l;
    int rc = ck_all(x, &l);
    for (size_t i = 0; i < l.n && rc == GSQL_OK; i++) {
        CkRec *c = &l.v[i];
        if (strcmp(c->table, table) != 0) {
            continue;
        }
        if (renamed) {
            free(c->table);
            c->table = dup_cstr(renamed);
            rc = ck_put(x, c, 1);
        } else {
            rc = gsql_fkey_drop(x->e, x->t, c->name, x->err);
        }
    }
    ck_list_free(&l);
    return rc;
}

static int run_schema_change(X *x, GsqlResult *out);

/* a column definition's generator, as the table layer takes it */
static void gen_column(GsqlColumn *c, const SqlColDef *d) {
    c->gen = d->gen;
    c->nscope = d->nscope;
    c->scope = (const char *const *)d->scope_text;
    c->length = d->length;
    c->alphabet = d->alphabet;
}

/* whether an unbound expression names a column by its bare name */
static int names_column(const SqlExpr *e, const char *name) {
    if (!e) {
        return 0;
    }
    if (e->kind == SX_NAME && e->nparts >= 1 && strcmp(e->parts[0], name) == 0) {
        return 1;
    }
    if (names_column(e->operand, name)) {
        return 1;
    }
    for (size_t k = 0; k < e->na; k++) {
        if (names_column(e->a[k], name)) {
            return 1;
        }
    }
    return 0;
}

/* A schema statement, then every view checked against what it left. */
static int run_schema(X *x, GsqlResult *out) {
    int rc = run_schema_change(x, out);
    if (rc == GSQL_OK && x->p->s->kind != SQ_CREATE_INDEX && x->p->s->kind != SQ_DROP_INDEX) {
        rc = recheck_views(x);
    }
    if (rc == GSQL_OK && x->p->s->kind != SQ_CREATE_INDEX && x->p->s->kind != SQ_DROP_INDEX) {
        rc = recheck_fkeys(x);
    }

    return rc;
}

static int run_schema_change(X *x, GsqlResult *out) {
    const SqlStmt *s = x->p->s;
    GsqlError *err = x->err;
    int rc = GSQL_OK;
    out->has_affected = 1;
    out->affected = 0;
    switch (s->kind) {
    case SQ_CREATE_VIEW: {
        GsqlBuf rec = {0};
        const SqlPlan *q = x->p->left;
        int named = s->view_cols.n > 0;
        view_rec_write(&rec, s->view_text, named ? s->view_cols.cols : NULL, s->view_cols.n,
                       named ? (const char *const *)s->view_cols.cols : q->names, named ? s->view_cols.n : q->nout, 1);
        rc = gsql_view_put(x->e, x->t, s->table, rec.bytes, rec.length, s->or_replace, err);
        gsql_buf_free(&rec);
        return rc;
    }
    case SQ_DROP_VIEW:
        rc = gsql_view_drop(x->e, x->t, s->table, err);
        if (rc == GSQL_NOT_FOUND && s->if_exists) {
            err->code = GSQL_OK;
            err->message[0] = 0;
            return GSQL_OK;
        }
        return rc;
    case SQ_CREATE_TABLE: {
        if (s->if_exists) {
            GsqlTable *t;
            if (gsql_table_open(x->e, x->t, s->table, &t, err) == GSQL_OK) {
                gsql_table_close(t);
                return GSQL_OK;
            }
            err->code = GSQL_OK;
        }
        size_t n = s->ncoldefs;
        GsqlColumn *cols = calloc(n ? n : 1, sizeof(GsqlColumn));
        GsqlBuf *defs = calloc(n ? n : 1, sizeof(GsqlBuf));
        unsigned key[256];
        size_t nkey = 0;
        if (!cols || !defs) {
            abort();
        }
        for (size_t k = 0; k < n && rc == GSQL_OK; k++) {
            const SqlColDef *d = &s->coldefs[k];
            cols[k].name = d->name;
            cols[k].kind = d->type.kind;
            memcpy(cols[k].currency, d->type.currency, sizeof cols[k].currency);
            cols[k].not_null = d->not_null;
            gen_column(&cols[k], d);
            rc = default_bytes(x, d, &defs[k]);
            if (d->def && rc == GSQL_OK) {
                cols[k].def = defs[k].bytes;
                cols[k].def_len = defs[k].length;
            }
            if (d->primary) {
                if (nkey || s->primary.n) {
                    rc = xfail(err, GSQL_SYNTAX, d->pos,
                               "PRIMARY KEY is declared twice: name every key column once, as PRIMARY KEY (a, b)");
                } else {
                    key[nkey++] = (unsigned)k;
                }
            }
        }
        for (size_t k = 0; k < s->primary.n && rc == GSQL_OK; k++) {
            long c = -1;
            for (size_t j = 0; j < n; j++) {
                if (strcmp(cols[j].name, s->primary.cols[k]) == 0) {
                    c = (long)j;
                }
            }
            if (c < 0) {
                rc = xfail(err, GSQL_NOT_FOUND, s->primary.pos, "PRIMARY KEY names `%s`, which is not a column",
                           s->primary.cols[k]);
            } else {
                key[nkey++] = (unsigned)c;
            }
        }
        if (rc == GSQL_OK) {
            GsqlTableDecl d = { s->table, n, cols, nkey, key };
            rc = gsql_table_create(x->e, x->t, &d, err);
        }
        /* a serial's scope binds to the table now made, or the table goes */
        for (size_t k = 0; rc == GSQL_OK && k < n; k++) {
            if (cols[k].nscope) {
                CkPlan cp;
                rc = scope_plan(x, s->table, &cols[k], (long)k, &cp, s->coldefs[k].gen_pos);
                ck_plan_free(&cp);
                if (rc != GSQL_OK) {
                    GsqlError undo = { GSQL_OK, "", 0, 0 };
                    GsqlError keep = *err;
                    if (gsql_table_drop(x->e, x->t, s->table, &undo) != GSQL_OK) {
                        out->failed_part_way = 1;
                    }
                    *err = keep;
                }
            }
        }
        for (size_t k = 0; k < n; k++) {
            gsql_buf_free(&defs[k]);
        }
        free(defs);
        free(cols);
        if (rc == GSQL_OK) {
            int made = 0;
            rc = create_indexes(x, s, &made);
            out->failed_part_way = rc != GSQL_OK;
            for (size_t k = 0; rc == GSQL_OK && k < s->nfkeys + s->nchecks; k++) {
                rc = k < s->nfkeys ? fk_create(x, s->table, &s->fkeys[k])
                                   : ck_create(x, s->table, &s->checks[k - s->nfkeys]);
                if (rc != GSQL_OK) {
                    /* a declaration refused: the table, its indexes and the
                       keys already made go again, so nothing stays written
                       and an explicit transaction carries on */
                    GsqlError undo = { GSQL_OK, "", 0, 0 };
                    GsqlError keep = *err;
                    if (fk_follow(x, s->table, NULL) != GSQL_OK || ck_follow(x, s->table, NULL) != GSQL_OK ||
                        gsql_table_drop(x->e, x->t, s->table, &undo) != GSQL_OK) {
                        out->failed_part_way = 1;
                    }
                    *err = keep;
                }
            }
        }
        return rc;
    }
    case SQ_ADD_FKEY:
        return fk_create(x, s->table, &s->fkeys[0]);
    case SQ_ADD_CHECK:
        return ck_create(x, s->table, &s->checks[0]);
    case SQ_DROP_CONSTRAINT: {
        GsqlBuf b = {0};
        FkRec k;
        CkRec ck;
        int found = gsql_fkey_get(x->e, x->t, s->constraint, &b, err) == GSQL_OK;
        int theirs = 0;
        if (found && ck_read(s->constraint, &b, &ck)) {
            theirs = strcmp(ck.table, s->table) == 0;
            ck_free(&ck);
        } else if (found && fk_read(s->constraint, &b, &k)) {
            theirs = strcmp(k.table, s->table) == 0;
            fk_free(&k);
        } else {
            found = 0;
        }
        gsql_buf_free(&b);
        if (!found) {
            return xfail(err, GSQL_NOT_FOUND, s->pos, "there is no constraint %s", s->constraint);
        }
        if (!theirs) {
            return xfail(err, GSQL_NOT_FOUND, s->pos, "table %s has no constraint %s", s->table, s->constraint);
        }
        return gsql_fkey_drop(x->e, x->t, s->constraint, err);
    }
    case SQ_DROP_TABLE:
        rc = fk_follow(x, s->table, NULL);
        if (rc == GSQL_OK) {
            rc = ck_follow(x, s->table, NULL);
        }
        if (rc != GSQL_OK) {
            return rc;
        }
        rc = gsql_table_drop(x->e, x->t, s->table, err);
        if (rc == GSQL_NOT_FOUND && s->if_exists) {
            err->code = GSQL_OK;
            return GSQL_OK;
        }
        return rc;
    case SQ_RENAME_TABLE:
        rc = gsql_table_rename(x->e, x->t, s->table, s->rename_to, err);
        if (rc == GSQL_OK) {
            rc = fk_follow(x, s->table, s->rename_to);
        }
        return rc == GSQL_OK ? ck_follow(x, s->table, s->rename_to) : rc;
    case SQ_ADD_COLUMN: {
        GsqlTable *tb;
        rc = gsql_table_open(x->e, x->t, s->table, &tb, err);
        if (rc != GSQL_OK) {
            return rc;
        }
        const SqlColDef *d = &s->coldefs[0];
        GsqlColumn c;
        memset(&c, 0, sizeof c);
        c.name = d->name;
        c.kind = d->type.kind;
        memcpy(c.currency, d->type.currency, sizeof c.currency);
        c.not_null = d->not_null;
        gen_column(&c, d);
        GsqlBuf def = {0};
        rc = default_bytes(x, d, &def);
        if (rc == GSQL_OK && c.nscope) {
            /* the scope binds to the table as it is: the new column itself,
               which it may not read, is not there yet */
            for (size_t k = 0; rc == GSQL_OK && k < d->nscope; k++) {
                if (names_column(d->scope[k], d->name)) {
                    rc = xfail(err, GSQL_SCHEMA, d->gen_pos, "serial `%s.%s`'s scope reads `%s` itself: the number "
                               "would be needed to find the counter it comes from", s->table, d->name, d->name);
                }
            }
            CkPlan cp;
            if (rc == GSQL_OK) {
                rc = scope_plan(x, s->table, &c, -1, &cp, d->gen_pos);
                ck_plan_free(&cp);
            }
        }
        if (rc == GSQL_OK) {
            if (d->def) {
                c.def = def.bytes;
                c.def_len = def.length;
            }
            rc = gsql_table_add_column(x->e, x->t, tb, &c, err);
        }
        gsql_buf_free(&def);
        gsql_table_close(tb);
        return rc;
    }
    case SQ_CREATE_INDEX: {
        GsqlTable *other;
        size_t at;
        int f = find_index(x, s->index, &other, &at);
        if (f < 0) {
            return err->code;
        }
        if (f) {
            gsql_table_close(other);
            if (s->if_exists) {
                return GSQL_OK;
            }
            return xfail(err, GSQL_EXISTS, s->pos, "there is already an index `%s`", s->index);
        }
        GsqlTable *tb;
        rc = gsql_table_open(x->e, x->t, s->table, &tb, err);
        if (rc != GSQL_OK) {
            if (rc == GSQL_NOT_FOUND) {
                err->code = GSQL_OK;
                return xfail(err, GSQL_NOT_FOUND, s->pos, "there is no table `%s`", s->table);
            }
            return rc;
        }
        unsigned pos[256];
        for (size_t k = 0; k < s->index_cols.n; k++) {
            long c = gsql_table_find(tb, s->index_cols.cols[k]);
            if (c < 0) {
                gsql_table_close(tb);
                return xfail(err, GSQL_NOT_FOUND, s->index_cols.pos, "table %s has no column `%s`", s->table,
                             s->index_cols.cols[k]);
            }
            pos[k] = (unsigned)c;
        }
        GsqlIndexDecl d = { s->index, s->index_cols.n, pos, s->index_cols.desc, s->unique };
        rc = gsql_index_create(x->e, x->t, tb, &d, err);
        gsql_table_close(tb);
        return rc;
    }
    case SQ_DROP_INDEX: {
        GsqlTable *tb;
        size_t at;
        int f = find_index(x, s->index, &tb, &at);
        if (f < 0) {
            return err->code;
        }
        if (!f) {
            if (s->if_exists) {
                return GSQL_OK;
            }
            return xfail(err, GSQL_NOT_FOUND, s->pos, "there is no index `%s`", s->index);
        }
        rc = gsql_index_drop(x->e, x->t, tb, s->index, err);
        gsql_table_close(tb);
        return rc;
    }
    default:
        return xfail(err, GSQL_MISUSE, s->pos, "not a schema statement");
    }
}

/* ---- the service ---------------------------------------------------------- */

/* the statement's text: a v3 string, or a prepared id */
static int statement_text(GsqlSql *q, const GsqlStatement *st, char **text, size_t *len, GsqlError *err) {
    const unsigned char *v = st->statement;
    size_t n = st->statement_length;
    if (n && v[0] == SER_STRING) {
        V3In in = { v, n, 1, 1 };
        uint64_t sn = v3_in_uleb(&in);
        if (!in.ok || in.pos + sn != n) {
            return xfail(err, GSQL_PROTOCOL, 0, "the statement's text is malformed");
        }
        *text = malloc(sn + 1);
        if (!*text) {
            abort();
        }
        memcpy(*text, v + in.pos, sn);
        (*text)[sn] = 0;
        *len = sn;
        return GSQL_OK;
    }
    uint64_t id;
    if (n && v3_read_u64(v, n, &id)) {
        if (id < 1 || id > q->nprepared) {
            return xfail(err, GSQL_NOT_FOUND, 0, "there is no prepared statement %llu", (unsigned long long)id);
        }
        *len = q->prepared_len[id - 1];
        *text = malloc(*len + 1);
        if (!*text) {
            abort();
        }
        memcpy(*text, q->prepared[id - 1], *len + 1);
        return GSQL_OK;
    }
    return xfail(err, GSQL_PROTOCOL, 0, "a statement is SQL text or a prepared statement's id");
}

static int svc_writes(void *ctx, const GsqlStatement *st, GsqlError *err) {
    GsqlSql *q = ctx;
    char *text;
    size_t len;
    if (statement_text(q, st, &text, &len, err) != GSQL_OK) {
        return -1;
    }
    SqlParsed p;
    int rc = sql_parse(text, len, &p, err);
    free(text);
    if (rc != GSQL_OK) {
        return -1;
    }
    /* a backup reads: it copies a snapshot, and other writers go on */
    int w = p.stmt->kind != SQ_SELECT && p.stmt->kind != SQ_BACKUP && !p.stmt->explain;
    sql_parsed_free(&p);
    return w;
}

/* A SELECT's rows begun: the joins started, the groups folded, and the rows
 * sorted where the access path does not give the order. */
static int select_start(SqlCursor *c) {
    SqlStmt *s = c->plan.s;
    if (c->plan.scans[0].access != ACC_NONE) {
        c->join_started = 1;
        if (join_start(&c->j, &c->x) != GSQL_OK) {
            return c->err.code ? c->err.code : GSQL_EXPRESSION;
        }
    }
    if (c->plan.grouped) {
        c->grouped = 1;
        if (aggregate(c) != GSQL_OK) {
            return c->err.code ? c->err.code : GSQL_EXPRESSION;
        }
    }
    if (c->plan.scans[0].access != ACC_NONE || c->grouped) {
        /* the access was to yield the order, and a bound that did not fit
         * turned it into a scan: then it must be sorted after all. Groups
         * come in key order, so a grouped query is always sorted. */
        int need_sort = s->norder && (c->grouped || c->plan.nscans > 1 || !c->plan.scans[0].ordered ||
                                      (c->join_started && c->j.its[0].fell_back));
        if (need_sort) {
            c->sorted = 1;
            if (collect_sorted(c) != GSQL_OK) {
                return c->err.code ? c->err.code : GSQL_EXPRESSION;
            }
            c->offset = 0;   /* applied by collect_sorted's start */
        }
    }
    return GSQL_OK;
}

/* ---- UNION, INTERSECT, EXCEPT (docs/gsql_setop_design.md) -------------
 *
 * Each side's rows are gathered, a SELECT through a cursor of its own over
 * the plan its parent owns, and combined by counting copies of each row's
 * key -- the key DISTINCT uses, so 3 and 3.0 are one value and NULLs one. */

typedef struct {
    GsqlBuf *v;
    size_t n, cap;
} Rows;

static void rows_add(Rows *r, const GsqlBuf *row) {
    if (r->n == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 64;
        r->v = realloc(r->v, sizeof(GsqlBuf) * r->cap);
        if (!r->v) {
            abort();
        }
    }
    memset(&r->v[r->n], 0, sizeof(GsqlBuf));
    v3_put(&r->v[r->n], row->bytes, row->length);
    r->n++;
}

static void rows_free(Rows *r) {
    for (size_t k = 0; k < r->n; k++) {
        gsql_buf_free(&r->v[k]);
    }
    free(r->v);
    memset(r, 0, sizeof *r);
}

/* keys with a count each */
typedef struct {
    KeyMap map;
    GsqlBuf *keys;
    long *count;
    size_t n, cap;
} Tally;

static const GsqlBuf *tally_key_of(void *ctx, size_t i) {
    return &((Tally *)ctx)->keys[i];
}

/* the slot (1-based) of `key`, added with a count of 0 when `add` and absent */
static size_t tally_slot(Tally *t, const GsqlKey *key, int add) {
    size_t at = keymap_find(&t->map, tally_key_of, t, key->bytes, key->length, 0);
    if (at || !add) {
        return at;
    }
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 64;
        t->keys = realloc(t->keys, sizeof(GsqlBuf) * t->cap);
        t->count = realloc(t->count, sizeof(long) * t->cap);
        if (!t->keys || !t->count) {
            abort();
        }
    }
    memset(&t->keys[t->n], 0, sizeof(GsqlBuf));
    v3_put(&t->keys[t->n], key->bytes, key->length);
    t->count[t->n] = 0;
    t->n++;
    keymap_find(&t->map, tally_key_of, t, key->bytes, key->length, t->n);
    return t->n;
}

static void tally_free(Tally *t) {
    for (size_t k = 0; k < t->n; k++) {
        gsql_buf_free(&t->keys[k]);
    }
    free(t->keys);
    free(t->count);
    free(t->map.slot);
    memset(t, 0, sizeof *t);
}

/* the key of output row `row` of plan `p` */
static int row_key(SqlCursor *c, const SqlPlan *p, const GsqlBuf *row, GsqlKey *key) {
    size_t n = p->nout;
    GsBytes *items = malloc(sizeof(GsBytes) * (n ? n : 1));
    if (!items) {
        abort();
    }
    if (v3_array_items(row->bytes, row->length, items, n) != (long)n) {
        free(items);
        return xfail(&c->err, GSQL_MISUSE, 0, "an output row did not hold its columns");
    }
    for (size_t k = 0; k < n; k++) {
        if (gsql_key_append(key, items[k].p, items[k].n, NULL, 0) != GSQL_KEY_OK) {
            int rc = xfail(&c->err, GSQL_EXPRESSION, p->s->pos,
                           "%s cannot be compared by a UNION, INTERSECT or EXCEPT: it has no single equality "
                           "G-SQL can key", tag_word(items[k].n ? items[k].p[0] : 0));
            free(items);
            return rc;
        }
    }
    free(items);
    return GSQL_OK;
}

static int side_rows(SqlCursor *c, SqlPlan *p, const GsEngine *e, GsTxn *t, Rows *out);

/* the rows of `l op r`, by the table in the design's §2 */
static int combine(SqlCursor *c, const SqlPlan *p, Rows *l, Rows *r, Rows *out) {
    SqlSetOp op = p->s->setop;
    int all = p->s->set_all;
    if (op == SQL_SET_UNION && all) {
        for (size_t k = 0; k < l->n; k++) {
            rows_add(out, &l->v[k]);
        }
        for (size_t k = 0; k < r->n; k++) {
            rows_add(out, &r->v[k]);
        }
        return GSQL_OK;
    }
    Tally right, emitted;
    memset(&right, 0, sizeof right);
    memset(&emitted, 0, sizeof emitted);
    int rc = GSQL_OK;
    if (op != SQL_SET_UNION) {
        for (size_t k = 0; k < r->n && rc == GSQL_OK; k++) {
            GsqlKey key;
            gsql_key_init(&key);
            rc = row_key(c, p, &r->v[k], &key);
            if (rc == GSQL_OK) {
                size_t at = tally_slot(&right, &key, 1);   /* may move right.count */
                right.count[at - 1]++;
            }
            gsql_key_free(&key);
        }
    }
    for (int side = 0; side < (op == SQL_SET_UNION ? 2 : 1) && rc == GSQL_OK; side++) {
        Rows *in = side ? r : l;
        for (size_t k = 0; k < in->n && rc == GSQL_OK; k++) {
            GsqlKey key;
            gsql_key_init(&key);
            rc = row_key(c, p, &in->v[k], &key);
            if (rc != GSQL_OK) {
                gsql_key_free(&key);
                break;
            }
            size_t at = op == SQL_SET_UNION ? 0 : tally_slot(&right, &key, 0);
            long *have = at ? &right.count[at - 1] : NULL;
            int keep;
            if (op == SQL_SET_UNION) {
                keep = 1;
            } else if (op == SQL_SET_INTERSECT) {
                keep = have && *have > 0;
                if (keep && all) {
                    (*have)--;
                }
            } else if (all) {
                keep = !(have && *have > 0);
                if (!keep) {
                    (*have)--;
                }
            } else {
                keep = !have;
            }
            if (keep && !all) {
                /* the plain forms keep one copy of each row */
                size_t e_at = tally_slot(&emitted, &key, 1);
                keep = emitted.count[e_at - 1]++ == 0;
            }
            if (keep) {
                rows_add(out, &in->v[k]);
            }
            gsql_key_free(&key);
        }
    }
    tally_free(&right);
    tally_free(&emitted);
    return rc;
}

static int side_rows(SqlCursor *c, SqlPlan *p, const GsEngine *e, GsTxn *t, Rows *out) {
    if (p->s->setop) {
        Rows l = {0}, r = {0};
        int rc = side_rows(c, p->left, e, t, &l);
        if (rc == GSQL_OK) {
            rc = side_rows(c, p->right, e, t, &r);
        }
        if (rc == GSQL_OK) {
            rc = combine(c, p, &l, &r, out);
        }
        rows_free(&l);
        rows_free(&r);
        return rc;
    }
    SqlCursor *k = calloc(1, sizeof *k);
    if (!k) {
        abort();
    }
    k->q = c->q;
    k->borrowed = 1;
    k->plan = *p;
    k->err.code = GSQL_OK;
    x_init(&k->x, c->q, e, t, &k->plan, &k->err);
    k->x.params = c->x.params;
    k->x.paramslen = c->x.paramslen;
    k->x.outer = c->x.outer;   /* a side is at its compound's level */
    k->limit = UINT64_MAX;
    int rc = select_start(k);
    GsqlBuf row = {0};
    while (rc == GSQL_OK) {
        int r = cursor_next(k, &row);
        if (r < 0) {
            rc = k->err.code ? k->err.code : GSQL_EXPRESSION;
        } else if (r == 0) {
            break;
        } else {
            rows_add(out, &row);
        }
    }
    if (rc != GSQL_OK) {
        c->err = k->err;
        if (c->err.code == GSQL_OK) {
            c->err.code = rc;
        }
    }
    gsql_buf_free(&row);
    cursor_free(k);
    return rc;
}

/* A compound SELECT's rows, gathered, combined and sorted; the cursor then
 * hands them out as it hands out any sorted rows. */
static int compound_start(SqlCursor *c, const GsEngine *e, GsTxn *t) {
    Rows all = {0};
    int rc = side_rows(c, &c->plan, e, t, &all);
    if (rc != GSQL_OK) {
        rows_free(&all);
        return rc;
    }
    size_t nord = c->plan.s->norder;
    c->rows = calloc(all.n ? all.n : 1, sizeof(SortRow));
    if (!c->rows) {
        abort();
    }
    for (size_t k = 0; k < all.n; k++) {
        SortRow *row = &c->rows[c->nrows++];
        row->out = all.v[k];
        row->keys = calloc(nord ? nord : 1, sizeof(GsqlHv *));
        if (!row->keys) {
            abort();
        }
        if (!nord) {
            continue;
        }
        GsBytes *items = malloc(sizeof(GsBytes) * (c->plan.nout ? c->plan.nout : 1));
        if (!items) {
            abort();
        }
        long n = v3_array_items(row->out.bytes, row->out.length, items, (long)c->plan.nout);
        for (size_t j = 0; j < nord; j++) {
            long at = c->plan.order_out[j];
            row->keys[j] = n > at ? hv_bytes(&c->x, items[at].p, items[at].n) : NULL;
            if (!row->keys[j]) {
                free(items);
                free(all.v);
                return xfail(&c->err, GSQL_MISUSE, 0, "an output column was not there to sort by");
            }
        }
        free(items);
    }
    free(all.v);   /* the rows' bytes moved into c->rows */
    if (nord) {
        SortRow *tmp = malloc(sizeof(SortRow) * (c->nrows ? c->nrows : 1));
        if (!tmp) {
            abort();
        }
        int failed = merge_sort(c, c->rows, c->nrows, tmp);
        free(tmp);
        if (failed) {
            return c->err.code ? c->err.code : GSQL_EXPRESSION;
        }
    }
    c->sorted = 1;
    c->pos = c->offset < c->nrows ? (size_t)c->offset : c->nrows;
    c->offset = 0;
    return GSQL_OK;
}

/* ---- subqueries (docs/gsql_subquery_design.md §4) ------------------------
 *
 * A subquery runs as a cursor of its own over the plan its node owns, its
 * execution's `outer` the one evaluating the node, so an outer reference reads
 * the row that execution has in hand. One that reads no enclosing row runs
 * once per execution, its answer kept on the node until the execution ends. */

static SqlCursor *sub_open(X *x, SqlPlan *p, X *outer) {
    SqlCursor *k = calloc(1, sizeof *k);
    if (!k) {
        abort();
    }
    k->q = x->q;
    k->borrowed = 1;
    k->plan = *p;
    k->err.code = GSQL_OK;
    x_init(&k->x, x->q, x->e, x->t, &k->plan, &k->err);
    k->x.params = x->params;
    k->x.paramslen = x->paramslen;
    k->x.outer = outer;
    k->limit = UINT64_MAX;
    SqlStmt *s = p->s;
    int rc = GSQL_OK;
    if (s->limit) {
        rc = count_value(&k->x, s->limit, "LIMIT", &k->limit);
    }
    if (rc == GSQL_OK && s->offset) {
        rc = count_value(&k->x, s->offset, "OFFSET", &k->offset);
        if (k->offset == UINT64_MAX) {
            k->offset = 0;
        }
    }
    if (rc == GSQL_OK) {
        rc = s->setop ? compound_start(k, x->e, x->t) : select_start(k);
    }
    if (rc != GSQL_OK) {
        *x->err = k->err;
        if (x->err->code == GSQL_OK) {
            x->err->code = rc;
        }
        cursor_free(k);
        return NULL;
    }
    return k;
}

/* an output row's one column, as a host value */
static GsqlHv *first_value(X *x, const GsqlBuf *row) {
    GsBytes item;
    if (v3_array_items(row->bytes, row->length, &item, 1) != 1) {
        xfail(x->err, GSQL_MISUSE, 0, "a subquery's row did not hold its column");
        return NULL;
    }
    return hv_bytes(x, item.p, item.n);
}

/* IN's Kleene fold of one more comparison: 1 decided, 0 go on, -1 failed */
static int in_step(X *x, SqlExpr *e, GsqlHv *a, GsqlHv *b, int *acc) {
    int t = compare(x, SQL_OP_EQ, a, b, e);
    if (t == -2) {
        return -1;
    }
    if (t == 1 || (t == -1 && *acc == 0)) {
        *acc = t;
    }
    return *acc == 1;
}

/* A derived scan's rows, through a cursor over its plan. A derived table
 * reads its query's enclosing query, as the scan's own query does; a WITH
 * query reads no enclosing query at all. */
static int derived_rows(X *x, size_t k) {
    const SqlScan *sc = &x->p->scans[k];
    XScan *xs = &x->sx[k];
    SqlCursor *c = sub_open(x, sc->derived, sc->cte ? NULL : x->outer);
    if (!c) {
        return x->err->code ? x->err->code : GSQL_EXPRESSION;
    }
    size_t cap = 0;
    GsqlBuf row = {0};
    int rc = GSQL_OK;
    for (;;) {
        int r = cursor_next(c, &row);
        if (r < 0) {
            *x->err = c->err;
            rc = x->err->code ? x->err->code : GSQL_EXPRESSION;
            break;
        }
        if (r == 0) {
            break;
        }
        if (xs->ndrows == cap) {
            cap = cap ? cap * 2 : 64;
            xs->drows = realloc(xs->drows, sizeof(GsqlBuf) * cap);
            if (!xs->drows) {
                abort();
            }
        }
        memset(&xs->drows[xs->ndrows], 0, sizeof(GsqlBuf));
        v3_put(&xs->drows[xs->ndrows], row.bytes, row.length);
        xs->ndrows++;
    }
    gsql_buf_free(&row);
    cursor_free(c);
    xs->dready = rc == GSQL_OK;
    return rc;
}

static GsqlHv *eval_subquery(X *x, SqlExpr *e) {
    if (e->op != SQL_SUB_IN && !e->correlated && e->hv) {
        return x->h->copy(x->h->ctx, e->hv);
    }
    GsqlHv *a = NULL;
    if (e->op == SQL_SUB_IN) {
        a = eval(x, e->a[0]);
        if (!a) {
            return NULL;
        }
    }
    SqlCursor *k = NULL;
    GsqlBuf row = {0};
    GsqlHv *result = NULL;
    int failed = 0;
    if (e->op == SQL_SUB_IN && !e->correlated) {
        /* the values, once per execution; then this row's comparisons */
        if (!e->gathered) {
            k = sub_open(x, e->subplan, x);
            failed = !k;
            size_t cap = 0;
            while (!failed) {
                int r = cursor_next(k, &row);
                if (r <= 0) {
                    failed = r < 0;
                    break;
                }
                GsqlHv *v = first_value(x, &row);
                if (!v) {
                    failed = 1;
                    break;
                }
                if (e->nset == cap) {
                    cap = cap ? cap * 2 : 16;
                    e->set = realloc(e->set, sizeof(void *) * cap);
                    if (!e->set) {
                        abort();
                    }
                }
                e->set[e->nset++] = v;
            }
            remember(x, e);
            e->gathered = !failed;
        }
        int acc = 0;
        for (size_t j = 0; !failed && j < e->nset; j++) {
            int d = in_step(x, e, a, e->set[j], &acc);
            failed = d < 0;
            if (d) {
                break;
            }
        }
        if (!failed) {
            if (e->neg && acc >= 0) {
                acc = !acc;
            }
            result = hv_tri(x, acc);
        }
    } else {
        k = sub_open(x, e->subplan, x);
        failed = !k;
        if (!failed && e->op == SQL_SUB_EXISTS) {
            int r = cursor_next(k, &row);
            failed = r < 0;
            result = failed ? NULL : hv_bool(x, r == 1);
        } else if (!failed && e->op == SQL_SUB_SCALAR) {
            int r = cursor_next(k, &row);
            if (r < 0) {
                failed = 1;
            } else if (r == 0) {
                result = hv_unknown(x);
            } else {
                GsqlHv *v = first_value(x, &row);
                int r2 = v ? cursor_next(k, &row) : -1;
                if (r2 == 1) {
                    xfail(x->err, GSQL_EXPRESSION, e->pos, "a subquery used as a value returned more than one row");
                }
                if (r2 != 0) {
                    rel(x, v);
                    failed = 1;
                } else {
                    result = v;
                }
            }
        } else if (!failed) {
            int acc = 0;
            for (;;) {
                int r = cursor_next(k, &row);
                if (r <= 0) {
                    failed = r < 0;
                    break;
                }
                GsqlHv *v = first_value(x, &row);
                int d = v ? in_step(x, e, a, v, &acc) : -1;
                rel(x, v);
                if (d) {
                    failed = d < 0;
                    break;
                }
            }
            if (!failed) {
                if (e->neg && acc >= 0) {
                    acc = !acc;
                }
                result = hv_tri(x, acc);
            }
        }
    }
    if (failed && k && x->err->code == GSQL_OK) {
        *x->err = k->err;
    }
    if (failed && x->err->code == GSQL_OK) {
        x->err->code = GSQL_EXPRESSION;
    }
    gsql_buf_free(&row);
    if (k) {
        cursor_free(k);
    }
    rel(x, a);
    if (!result) {
        return NULL;
    }
    if (e->op != SQL_SUB_IN && !e->correlated) {
        e->hv = result;
        remember(x, e);
        return x->h->copy(x->h->ctx, result);
    }
    return result;
}

static void take_warnings(GsqlSql *q, char ***seen, size_t *nseen, GsqlResult *out, int keep);

/* BACKUP TO dir: its path, then the engine's backup of this statement's own
 * snapshot, which the cursor keeps between fetches. */
static int backup_start(SqlCursor *c, const GsEngine *e, GsTxn *t) {
    GsqlBuf v = {0};
    int rc = eval_into(&c->x, c->parsed.stmt->backup_to, &v);
    if (rc != GSQL_OK) {
        gsql_buf_free(&v);
        return rc;
    }
    char *path = v.length && v.bytes[0] == SER_STRING ? v3_read_text(v.bytes, v.length) : NULL;
    gsql_buf_free(&v);
    uint32_t pos = c->parsed.stmt->backup_to->pos;
    if (!path) {
        return xfail(&c->err, GSQL_EXPRESSION, pos, "BACKUP TO takes the copy's directory as text");
    }
    if (path[0] != '/') {
        rc = xfail(&c->err, GSQL_EXPRESSION, pos, "BACKUP TO `%.200s`: the directory must be absolute, since the "
                   "owner's working directory is not the program's (gsql.backup resolves one for you)", path);
        free(path);
        return rc;
    }
    int st = e->backup_begin(t, path, &c->backup);
    if (st != GS_OK) {
        if (st == GS_BUSY) {
            rc = xfail(&c->err, GSQL_STATE, pos, "BACKUP TO `%.200s`: that database is open -- an owner holds it, which "
                       "a program using the copy does, and so does the live database itself", path);
        } else if (st == GS_FORMAT) {
            rc = xfail(&c->err, GSQL_EXISTS, pos, "BACKUP TO `%.200s`: the directory holds files that are not a "
                       "database's, so a copy there would delete them", path);
        } else {
            rc = xfail(&c->err, GSQL_STORAGE, pos, "BACKUP TO `%.200s`: the engine answered %s", path,
                       gs_status_name(st));
        }
        free(path);
        return rc;
    }
    free(path);
    return GSQL_OK;
}

static int svc_run(void *ctx, const GsEngine *e, GsTxn *t, const GsqlStatement *st, GsqlResult *out,
                   GsqlError *err) {
    GsqlSql *q = ctx;
    SqlCursor *c = calloc(1, sizeof *c);
    if (!c) {
        abort();
    }
    c->q = q;
    size_t len;
    int rc = statement_text(q, st, &c->text, &len, err);
    if (rc == GSQL_OK) {
        rc = sql_parse(c->text, len, &c->parsed, err);
    }
    if (rc == GSQL_OK) {
        rc = sql_plan(e, t, c->parsed.stmt, &c->parsed.arena, c->text, q->force_scan, &c->plan, err);
    }
    if (rc != GSQL_OK) {
        take_warnings(q, &c->wseen, &c->nwseen, out, 0);
        sql_parsed_free(&c->parsed);
        free(c->text);
        free(c);
        return rc;
    }
    c->err.code = GSQL_OK;
    x_init(&c->x, q, e, t, &c->plan, &c->err);
    c->x.params = st->parameters;
    c->x.paramslen = st->parameters_length;
    SqlStmt *s = c->parsed.stmt;
    if (s->kind == SQ_BACKUP) {
        if (backup_start(c, e, t) != GSQL_OK) {
            goto failed;
        }
    } else if (s->explain) {
        c->explain = 1;
        sql_explain(&c->plan, explain_line, c);
    } else if (s->kind == SQ_SELECT) {
        c->limit = UINT64_MAX;
        if (s->limit && count_value(&c->x, s->limit, "LIMIT", &c->limit) != GSQL_OK) {
            goto failed;
        }
        if (s->offset) {
            if (count_value(&c->x, s->offset, "OFFSET", &c->offset) != GSQL_OK) {
                goto failed;
            }
            if (c->offset == UINT64_MAX) {
                c->offset = 0;
            }
        }
        if ((s->setop ? compound_start(c, e, t) : select_start(c)) != GSQL_OK) {
            goto failed;
        }
    } else {
        if (s->kind == SQ_INSERT) {
            rc = run_insert(&c->x, out);
        } else if (s->kind == SQ_UPDATE || s->kind == SQ_DELETE) {
            rc = run_update_delete(q, &c->x, out);
        } else {
            rc = run_schema(&c->x, out);
        }
        if (rc != GSQL_OK) {
            *err = c->err;
            if (err->code == GSQL_OK) {
                err->code = rc;
            }
        }
        take_warnings(q, &c->wseen, &c->nwseen, out, rc == GSQL_OK);
        cursor_free(c);
        return rc;
    }
    columns_of(c, &out->columns);
    rc = cursor_batch(c, st->batch ? st->batch : 1, out, err);
    take_warnings(q, &c->wseen, &c->nwseen, out, rc == GSQL_OK);
    if (rc != GSQL_OK || !out->cursor) {
        out->cursor = NULL;
        cursor_free(c);
    }
    return rc;
failed:
    *err = c->err;
    rc = err->code ? err->code : GSQL_EXPRESSION;
    take_warnings(q, &c->wseen, &c->nwseen, out, 0);
    cursor_free(c);
    return rc;
}

/* Everything raised since the last reply -- the host's, from gBASIC's
 * operators, and the SQL layer's own -- into the result, each message once per
 * statement; or, for a statement that failed, dropped, since the refusal is
 * what the program must see. `seen` lives as long as the statement does. */
static void take_warnings(GsqlSql *q, char ***seen, size_t *nseen, GsqlResult *out, int keep) {
    char **msg = NULL;
    int *code = NULL;
    size_t n = 0;
    char buf[512];
    int c;
    for (;;) {
        char *m;
        if (q->nw) {
            c = q->wcode[0];
            m = q->wmsg[0];
            memmove(q->wmsg, q->wmsg + 1, sizeof *q->wmsg * (q->nw - 1));
            memmove(q->wcode, q->wcode + 1, sizeof *q->wcode * (q->nw - 1));
            q->nw--;
        } else if (q->h.warning && q->h.warning(q->h.ctx, &c, buf, sizeof buf)) {
            /* a gBASIC operator's, inside SQL, which has no source line */
            static const char where[] = ", in a G-SQL expression";
            size_t len = strlen(buf);
            m = malloc(len + sizeof where);
            if (!m) {
                abort();
            }
            memcpy(m, buf, len);
            memcpy(m + len, where, sizeof where);
        } else {
            break;
        }
        int dup = !keep;
        for (size_t k = 0; k < *nseen && !dup; k++) {
            dup = strcmp((*seen)[k], m) == 0;
        }
        if (dup) {
            free(m);
            continue;
        }
        char **g = realloc(*seen, sizeof **seen * (*nseen + 1));
        char **gm = realloc(msg, sizeof *gm * (n + 1));
        int *gc = realloc(code, sizeof *gc * (n + 1));
        if (!g || !gm || !gc) {
            abort();
        }
        *seen = g;
        msg = gm;
        code = gc;
        (*seen)[(*nseen)++] = m;
        msg[n] = m;
        code[n++] = c;
    }
    if (n) {
        out->warnings.length = 0;
        v3_w_array(&out->warnings, n);
        for (size_t k = 0; k < n; k++) {
            v3_w_record(&out->warnings, 2);
            v3_w_name(&out->warnings, "code");
            v3_w_int(&out->warnings, (uint64_t)code[k]);
            v3_w_name(&out->warnings, "message");
            v3_w_text(&out->warnings, msg[k]);
        }
    }
    free(msg);
    free(code);
}

static int svc_fetch(void *ctx, void *cursor, uint32_t count, GsqlResult *out, GsqlError *err) {
    GsqlSql *q = ctx;
    SqlCursor *c = cursor;
    int rc = cursor_batch(c, count, out, err);
    take_warnings(q, &c->wseen, &c->nwseen, out, rc == GSQL_OK);
    if (rc != GSQL_OK || !out->cursor) {
        out->cursor = NULL;
        cursor_free(c);
    }
    return rc;
}

static void svc_close(void *ctx, void *cursor) {
    (void)ctx;
    cursor_free(cursor);
}

static int svc_prepare(void *ctx, const GsqlStatement *st, GsqlBuf *description, GsqlError *err) {
    GsqlSql *q = ctx;
    char *text;
    size_t len;
    if (statement_text(q, st, &text, &len, err) != GSQL_OK) {
        return err->code;
    }
    SqlParsed p;
    int rc = sql_parse(text, len, &p, err);
    if (rc != GSQL_OK) {
        free(text);
        return rc;
    }
    size_t id = 0;
    for (size_t k = 0; k < q->nprepared && !id; k++) {
        if (q->prepared_len[k] == len && memcmp(q->prepared[k], text, len) == 0) {
            id = k + 1;
        }
    }
    if (!id) {
        q->prepared = realloc(q->prepared, sizeof(char *) * (q->nprepared + 1));
        q->prepared_len = realloc(q->prepared_len, sizeof(size_t) * (q->nprepared + 1));
        if (!q->prepared || !q->prepared_len) {
            abort();
        }
        q->prepared[q->nprepared] = text;
        q->prepared_len[q->nprepared] = len;
        id = ++q->nprepared;
    } else {
        free(text);
    }
    const SqlStmt *s = p.stmt;
    description->length = 0;
    v3_w_record(description, 3);
    v3_w_name(description, "id");
    v3_w_int(description, id);
    v3_w_name(description, "writes");
    v3_w_bool(description, s->kind != SQ_SELECT && !s->explain);
    v3_w_name(description, "parameters");
    if (s->nparam_names) {
        v3_w_array(description, s->nparam_names);
        for (size_t k = 0; k < s->nparam_names; k++) {
            v3_w_text(description, s->param_names[k]);
        }
    } else {
        v3_w_int(description, s->max_param_index);
    }
    sql_parsed_free(&p);
    return GSQL_OK;
}

GsqlSql *gsql_sql_new(const GsqlHost *host) {
    GsqlSql *q = calloc(1, sizeof *q);
    if (!q) {
        abort();
    }
    q->h = *host;
    return q;
}

void gsql_sql_free(GsqlSql *q) {
    if (!q) {
        return;
    }
    for (size_t k = 0; k < q->nprepared; k++) {
        free(q->prepared[k]);
    }
    free(q->prepared);
    free(q->prepared_len);
    for (size_t k = 0; k < q->nw; k++) {
        free(q->wmsg[k]);
    }
    free(q->wmsg);
    free(q->wcode);
    free(q);
}

GsqlService gsql_sql_service(GsqlSql *q) {
    GsqlService s = { q, svc_writes, svc_run, svc_fetch, svc_close, svc_prepare };
    return s;
}

void gsql_sql_force_scan(GsqlSql *q, int on) {
    q->force_scan = on;
}

void gsql_sql_counts(const GsqlSql *q, unsigned long *seeks, unsigned long *scans) {
    *seeks = q->seeks;
    *scans = q->scans;
}

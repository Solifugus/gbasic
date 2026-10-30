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
};

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

typedef struct {
    GsqlSql *q;
    const GsqlHost *h;
    const GsEngine *e;
    GsTxn *t;
    SqlPlan *p;
    GsqlError *err;
    /* parameters: one v3 record (by name) or array (by position), or none */
    const unsigned char *params;
    size_t paramslen;
    /* the current row */
    GsqlBuf row;
    GsqlBuf rowkey;
    int have_row;
    GsqlHv **cols;       /* loaded on first use, released per row */
    size_t ncols;
    /* nodes whose hv this execution set, released at its end */
    SqlExpr **cached;
    size_t ncached, capcached;
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

static void row_clear(X *x) {
    for (size_t k = 0; k < x->ncols; k++) {
        rel(x, x->cols[k]);
        x->cols[k] = NULL;
    }
    x->have_row = 0;
}

static void x_init(X *x, GsqlSql *q, const GsEngine *e, GsTxn *t, SqlPlan *p, GsqlError *err) {
    memset(x, 0, sizeof *x);
    x->q = q;
    x->h = &q->h;
    x->e = e;
    x->t = t;
    x->p = p;
    x->err = err;
    x->ncols = p->tb ? gsql_table_ncols(p->tb) : 0;
    x->cols = calloc(x->ncols ? x->ncols : 1, sizeof(GsqlHv *));
    if (!x->cols) {
        abort();
    }
}

static void x_free(X *x) {
    row_clear(x);
    free(x->cols);
    x->cols = NULL;
    for (size_t k = 0; k < x->ncached; k++) {
        rel(x, x->cached[k]->hv);
        x->cached[k]->hv = NULL;
    }
    free(x->cached);
    x->cached = NULL;
    x->ncached = 0;
    gsql_buf_free(&x->row);
    gsql_buf_free(&x->rowkey);
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
    if (!x->have_row || e->col >= x->ncols) {
        xfail(x->err, GSQL_MISUSE, e->pos, "a column was read with no row");
        return NULL;
    }
    if (!x->cols[e->col]) {
        GsBytes v;
        if (gsql_row_value(x->p->tb, x->row.bytes, x->row.length, e->col, &v, x->err) != GSQL_OK) {
            return NULL;
        }
        x->cols[e->col] = hv_bytes(x, v.p, v.n);
        if (!x->cols[e->col]) {
            return NULL;
        }
    }
    GsqlHv *v = x->h->copy(x->h->ctx, x->cols[e->col]);
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

static GsqlHv *eval(X *x, SqlExpr *e) {
    switch (e->kind) {
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

/* WHERE: 1 keep the row, 0 drop it, -1 failed */
static int keep(X *x) {
    for (size_t k = 0; k < x->p->nconj; k++) {
        GsqlHv *v = eval(x, x->p->conj[k]);
        if (!v) {
            return -1;
        }
        int t = x->h->truth(x->h->ctx, v);
        int kind = kind_of(x, v);
        rel(x, v);
        if (t == -2) {
            xfail(x->err, GSQL_EXPRESSION, x->p->conj[k]->pos, "WHERE needs a boolean, and this condition is %s",
                  tag_word(kind));
            return -1;
        }
        if (t != 1) {
            return 0;
        }
    }
    return 1;
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
    SqlPlan *p;
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
} Iter;

static unsigned flags_for(const SqlPlan *p, size_t j) {
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
    GsqlColumn c = *gsql_table_col(it->p->tb, it->p->keycols[j]);
    c.not_null = 0;
    GsqlError ignored = { GSQL_OK, "", 0 };
    int fits = b.bytes[0] != SER_NULL && gsql_value_fits(&c, b.bytes, b.length, &ignored) == GSQL_OK;
    if (fits) {
        fits = gsql_key_append(key, b.bytes, b.length, NULL, flags_for(it->p, j)) == GSQL_KEY_OK;
    }
    gsql_buf_free(&b);
    return fits;
}

static int iter_open(Iter *it, X *x) {
    memset(it, 0, sizeof *it);
    it->x = x;
    it->p = x->p;
    SqlPlan *p = x->p;
    gsql_key_init(&it->prefix);
    gsql_key_init(&it->klo);
    gsql_key_init(&it->khi);
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
            xfail(x->err, GSQL_CORRUPT, 0, "a key of table %s does not parse", gsql_table_name(it->p->tb));
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

/* The next row the access yields, into x->row and x->rowkey: 1, 0 at the
 * end, -1 failed. */
static int next_row(Iter *it) {
    X *x = it->x;
    row_clear(x);
    if (it->done) {
        return 0;
    }
    const GsEngine *e = x->e;
    if (it->point) {
        it->done = 1;
        x->rowkey.length = 0;
        v3_put(&x->rowkey, it->prefix.bytes, it->prefix.length);
        GsBytes rk = { x->rowkey.bytes, x->rowkey.length };
        x->row.length = 0;
        int rc = gsql_row_get(e, x->t, it->p->tb, rk, &x->row, x->err);
        if (rc == GSQL_NOT_FOUND) {
            x->err->code = GSQL_OK;
            x->err->message[0] = 0;
            return 0;
        }
        if (rc != GSQL_OK) {
            return -1;
        }
        x->have_row = 1;
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
        if (gsql_index_rowkey(it->p->tb, it->p->index, key, val, &rk, x->err) != GSQL_OK) {
            return -1;
        }
        x->rowkey.length = 0;
        v3_put(&x->rowkey, rk.p, rk.n);
        GsBytes rk2 = { x->rowkey.bytes, x->rowkey.length };
        x->row.length = 0;
        if (gsql_row_get(e, x->t, it->p->tb, rk2, &x->row, x->err) != GSQL_OK) {
            if (x->err->code == GSQL_NOT_FOUND) {
                x->err->code = GSQL_CORRUPT;
            }
            return -1;
        }
    } else {
        x->rowkey.length = 0;
        v3_put(&x->rowkey, key.p, key.n);
        x->row.length = 0;
        v3_put(&x->row, val.p, val.n);
    }
    x->have_row = 1;
    return 1;
}

/* The next row that passes WHERE. */
static int next_kept(Iter *it) {
    for (;;) {
        int r = next_row(it);
        if (r <= 0) {
            return r;
        }
        int k = keep(it->x);
        if (k != 0) {
            return k;
        }
    }
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
    Iter it;
    int iter_open;
    int sorted;
    SortRow *rows;
    size_t nrows, pos;
    uint64_t offset, limit, produced;
    int explain;
    char **lines;
    size_t nlines;
    GsqlBuf pending;    /* a row read ahead, to know whether more remain */
    int have_pending;
    int ended;
} SqlCursor;

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
    for (size_t k = 0; k < c->nlines; k++) {
        free(c->lines[k]);
    }
    free(c->lines);
    if (c->iter_open) {
        iter_close(&c->it);
    }
    x_free(&c->x);
    sql_plan_free(&c->plan);
    sql_parsed_free(&c->parsed);
    gsql_buf_free(&c->pending);
    free(c->text);
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
    if (c->plan.access == ACC_NONE) {
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
        int r = next_kept(&c->it);
        if (r <= 0) {
            c->ended = 1;
            return r;
        }
        if (c->offset) {
            c->offset--;
            continue;
        }
        if (output_row(c, out) != GSQL_OK) {
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
        int r = next_kept(&c->it);
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
        row->keys = calloc(nord, sizeof(GsqlHv *));
        if (!row->keys) {
            abort();
        }
        c->nrows++;
        if (output_row(c, &row->out) != GSQL_OK) {
            return x->err->code;
        }
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
static int cursor_batch(SqlCursor *c, uint32_t count, GsqlResult *out, GsqlError *err) {
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

static int run_update_delete(GsqlSql *q, X *x, GsqlResult *out) {
    SqlPlan *p = x->p;
    SqlStmt *s = p->s;
    GsqlError *err = x->err;
    Iter it;
    int rc = iter_open(&it, x);
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
        v3_put(&c->key, x->rowkey.bytes, x->rowkey.length);
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
    if (rc != GSQL_OK) {
        out->failed_part_way = done > 0;
        return rc;
    }
    (void)q;
    out->has_affected = 1;
    out->affected = done;
    return GSQL_OK;
}

static int run_insert(X *x, GsqlResult *out) {
    SqlPlan *p = x->p;
    SqlStmt *s = p->s;
    size_t ncols = gsql_table_ncols(p->tb);
    GsqlIn *vals = calloc(ncols ? ncols : 1, sizeof(GsqlIn));
    GsqlBuf *bufs = calloc(p->ntargets ? p->ntargets : 1, sizeof(GsqlBuf));
    if (!vals || !bufs) {
        abort();
    }
    uint64_t done = 0;
    int rc = GSQL_OK;
    for (size_t r = 0; r < s->nrows && rc == GSQL_OK; r++) {
        memset(vals, 0, sizeof(GsqlIn) * ncols);
        for (size_t k = 0; k < p->ntargets && rc == GSQL_OK; k++) {
            bufs[k].length = 0;
            if (!s->rows[r][k]) {
                continue;   /* DEFAULT: omitted, so the column's default */
            }
            rc = eval_into(x, s->rows[r][k], &bufs[k]);
            vals[p->targets[k]].present = 1;
            vals[p->targets[k]].p = bufs[k].bytes;
            vals[p->targets[k]].n = bufs[k].length;
        }
        if (rc == GSQL_OK) {
            rc = gsql_row_insert(x->e, x->t, p->tb, vals, NULL, x->err);
            if (rc != GSQL_OK && s->nrows > 1) {
                char m[sizeof x->err->message];
                snprintf(m, sizeof m, "row %zu of VALUES: %.400s", r + 1, x->err->message);
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

static int run_schema(X *x, GsqlResult *out) {
    const SqlStmt *s = x->p->s;
    GsqlError *err = x->err;
    int rc = GSQL_OK;
    out->has_affected = 1;
    out->affected = 0;
    switch (s->kind) {
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
        for (size_t k = 0; k < n; k++) {
            gsql_buf_free(&defs[k]);
        }
        free(defs);
        free(cols);
        if (rc == GSQL_OK) {
            int made = 0;
            rc = create_indexes(x, s, &made);
            out->failed_part_way = rc != GSQL_OK;
        }
        return rc;
    }
    case SQ_DROP_TABLE:
        rc = gsql_table_drop(x->e, x->t, s->table, err);
        if (rc == GSQL_NOT_FOUND && s->if_exists) {
            err->code = GSQL_OK;
            return GSQL_OK;
        }
        return rc;
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
        GsqlBuf def = {0};
        rc = default_bytes(x, d, &def);
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
    int w = p.stmt->kind != SQ_SELECT && !p.stmt->explain;
    sql_parsed_free(&p);
    return w;
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
    if (s->explain) {
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
        if (c->plan.access != ACC_NONE) {
            if (iter_open(&c->it, &c->x) != GSQL_OK) {
                c->iter_open = 1;
                goto failed;
            }
            c->iter_open = 1;
            /* the access was to yield the order, and a bound that did not fit
             * turned it into a scan: then it must be sorted after all */
            int need_sort = s->norder && (!c->plan.ordered || c->it.fell_back);
            if (need_sort) {
                c->sorted = 1;
                if (collect_sorted(c) != GSQL_OK) {
                    goto failed;
                }
                c->offset = 0;   /* applied by collect_sorted's start */
            }
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
        cursor_free(c);
        return rc;
    }
    columns_of(c, &out->columns);
    rc = cursor_batch(c, st->batch ? st->batch : 1, out, err);
    if (rc != GSQL_OK || !out->cursor) {
        out->cursor = NULL;
        cursor_free(c);
    }
    return rc;
failed:
    *err = c->err;
    rc = err->code ? err->code : GSQL_EXPRESSION;
    cursor_free(c);
    return rc;
}

static int svc_fetch(void *ctx, void *cursor, uint32_t count, GsqlResult *out, GsqlError *err) {
    (void)ctx;
    SqlCursor *c = cursor;
    int rc = cursor_batch(c, count, out, err);
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

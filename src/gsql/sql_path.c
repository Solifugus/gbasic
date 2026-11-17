/* A G-SQL PATH compiled to the SQL it means (docs/gsql_paths_design.md §2).
 *
 * `shop.customer[.city = "Rome"]` reaches the owner as a record of steps,
 * every program value already a parameter, and leaves here as SQL text the
 * SQL layer then parses and runs as it would any statement: one execution
 * path, so a path has no rules of its own. Names are resolved against the
 * catalog in the statement's own transaction, which is why this is done in
 * the owner and not the client. No value is ever written into the text --
 * only names the catalog holds, each quoted, and `$n` -- so nothing a program
 * reads can become SQL.
 *
 * The path record:
 *   { table, key: [n, ...], filter: tree, mode: "rows" | "count" | "sql" }
 * `key` holds the parameter numbers of the primary key's values, in the key's
 * column order. A filter tree node is one of
 *   { col: name }  { param: n }  { op, left, right }  { op: "not" | "neg" |
 *   "is_unknown", arg }
 * with `op` a comparison (= != < > <= >=), `and`, `or`, or + - * /. */

#include "sql.h"
#include "gsql_owner.h"
#include "v3.h"
#include "serialize_tags.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_MAX_FIELDS 16
#define PATH_MAX_KEY 32
#define PATH_MAX_DEPTH 64

static int pfail(GsqlError *err, int code, const char *fmt, ...) {
    memset(err, 0, sizeof *err);
    err->code = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof err->message, fmt, ap);
    va_end(ap);
    return code;
}

static void put(GsqlBuf *b, const char *s) {
    v3_put(b, s, strlen(s));
}

/* A catalog name, quoted, so a column called `order` is a column. */
static void put_name(GsqlBuf *b, const char *name) {
    v3_put(b, "\"", 1);
    for (const char *p = name; *p; p++) {
        v3_put(b, p, 1);
        if (*p == '"') {
            v3_put(b, "\"", 1);
        }
    }
    v3_put(b, "\"", 1);
}

static void put_param(GsqlBuf *b, uint64_t n) {
    char t[32];
    snprintf(t, sizeof t, "$%llu", (unsigned long long)n);
    put(b, t);
}

/* ---- names ---------------------------------------------------------- */

/* Edit distance, for the nearest name a refusal offers. */
static size_t distance(const char *a, const char *b) {
    size_t na = strlen(a), nb = strlen(b);
    if (na > 64 || nb > 64) {
        return (size_t)-1;
    }
    size_t row[65];
    for (size_t j = 0; j <= nb; j++) {
        row[j] = j;
    }
    for (size_t i = 1; i <= na; i++) {
        size_t diag = row[0];
        row[0] = i;
        for (size_t j = 1; j <= nb; j++) {
            size_t up = row[j];
            size_t cost = tolower((unsigned char)a[i - 1]) == tolower((unsigned char)b[j - 1]) ? 0 : 1;
            size_t best = diag + cost;
            if (row[j] + 1 < best) best = row[j] + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;
            row[j] = best;
            diag = up;
        }
    }
    return row[nb];
}

typedef struct {
    const char *want;
    char best[128];
    size_t score;
} Nearest;

static void nearest_offer(Nearest *n, const char *name) {
    size_t d = distance(n->want, name);
    size_t limit = strlen(n->want) / 3 + 1;
    if (d <= limit && d < n->score) {
        n->score = d;
        snprintf(n->best, sizeof n->best, "%s", name);
    }
}

static int nearest_table(void *ctx, const char *name) {
    nearest_offer(ctx, name);
    return 0;
}

static void nearest_init(Nearest *n, const char *want) {
    n->want = want;
    n->best[0] = 0;
    n->score = (size_t)-1;
}

static void hint(char *out, size_t cap, const Nearest *n) {
    if (n->best[0]) {
        snprintf(out, cap, " -- did you mean `%s`?", n->best);
    } else {
        out[0] = 0;
    }
}

/* A path's name matches the catalog as an unquoted SQL name does: as
 * written, or folded to lower case, which is how SQL stored it. */
static int open_table(const GsEngine *e, GsTxn *t, const char *name, GsqlTable **tb, GsqlError *err) {
    GsqlError quiet;
    if (gsql_table_open(e, t, name, tb, &quiet) == GSQL_OK) {
        return GSQL_OK;
    }
    char low[256];
    size_t n = strlen(name);
    if (n < sizeof low) {
        for (size_t i = 0; i <= n; i++) {
            low[i] = (char)tolower((unsigned char)name[i]);
        }
        if (strcmp(low, name) != 0 && gsql_table_open(e, t, low, tb, &quiet) == GSQL_OK) {
            return GSQL_OK;
        }
    }
    Nearest near;
    nearest_init(&near, name);
    gsql_table_list(e, t, nearest_table, &near, &quiet);
    char h[200];
    hint(h, sizeof h, &near);
    pfail(err, GSQL_NOT_FOUND, "the database has no table `%s`%s", name, h);
    snprintf(err->table, sizeof err->table, "%s", name);
    return GSQL_NOT_FOUND;
}

static const char *column_named(const GsqlTable *tb, const char *name, GsqlError *err) {
    long i = gsql_table_find(tb, name);
    if (i < 0) {
        char low[256];
        size_t n = strlen(name);
        if (n < sizeof low) {
            for (size_t k = 0; k <= n; k++) {
                low[k] = (char)tolower((unsigned char)name[k]);
            }
            i = gsql_table_find(tb, low);
        }
    }
    if (i >= 0) {
        return gsql_table_col(tb, (size_t)i)->name;
    }
    Nearest near;
    nearest_init(&near, name);
    for (size_t k = 0; k < gsql_table_ncols(tb); k++) {
        nearest_offer(&near, gsql_table_col(tb, k)->name);
    }
    char h[200];
    hint(h, sizeof h, &near);
    pfail(err, GSQL_NOT_FOUND, "`%s` has no column `%s`%s", gsql_table_name(tb), name, h);
    snprintf(err->table, sizeof err->table, "%s", gsql_table_name(tb));
    snprintf(err->column, sizeof err->column, "%s", name);
    return NULL;
}

/* ---- the filter ----------------------------------------------------- */

static char *field_text(const V3Field *f, long n, const char *name) {
    const V3Field *x = v3_field_named(f, n, name);
    return x ? v3_read_text(x->v, x->vlen) : NULL;
}

static int filter_sql(const GsqlTable *tb, const unsigned char *p, size_t n, int depth, GsqlBuf *out,
                      GsqlError *err) {
    if (depth > PATH_MAX_DEPTH) {
        return pfail(err, GSQL_PROTOCOL, "a path's filter is nested more than %d deep", PATH_MAX_DEPTH);
    }
    V3Field f[PATH_MAX_FIELDS];
    long nf = v3_record_fields(p, n, f, PATH_MAX_FIELDS);
    if (nf < 0) {
        return pfail(err, GSQL_PROTOCOL, "a path's filter node is not a record");
    }
    char *col = field_text(f, nf, "col");
    if (col) {
        const char *real = column_named(tb, col, err);
        free(col);
        if (!real) {
            return GSQL_NOT_FOUND;
        }
        put_name(out, real);
        return GSQL_OK;
    }
    const V3Field *pf = v3_field_named(f, nf, "param");
    if (pf) {
        uint64_t k;
        if (!v3_read_u64(pf->v, pf->vlen, &k) || k < 1) {
            return pfail(err, GSQL_PROTOCOL, "a path's parameter number is not a positive integer");
        }
        put_param(out, k);
        return GSQL_OK;
    }
    char *op = field_text(f, nf, "op");
    if (!op) {
        return pfail(err, GSQL_PROTOCOL, "a path's filter node has no `op`");
    }
    int rc = GSQL_OK;
    if (strcmp(op, "not") == 0 || strcmp(op, "neg") == 0 || strcmp(op, "is_unknown") == 0) {
        const V3Field *a = v3_field_named(f, nf, "arg");
        if (!a) {
            pfail(err, GSQL_PROTOCOL, "a path's `%s` has no `arg`", op);
            free(op);
            return GSQL_PROTOCOL;
        }
        put(out, strcmp(op, "not") == 0 ? "(NOT " : strcmp(op, "neg") == 0 ? "(-" : "(");
        rc = filter_sql(tb, a->v, a->vlen, depth + 1, out, err);
        put(out, strcmp(op, "is_unknown") == 0 ? " IS NULL)" : ")");
        free(op);
        return rc;
    }
    static const char *const ops[][2] = {
        { "=", " = " },   { "!=", " <> " }, { "<", " < " },     { ">", " > " },   { "<=", " <= " },
        { ">=", " >= " }, { "and", " AND " }, { "or", " OR " }, { "+", " + " },   { "-", " - " },
        { "*", " * " },   { "/", " / " },
    };
    const char *spelled = NULL;
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        if (strcmp(op, ops[i][0]) == 0) {
            spelled = ops[i][1];
        }
    }
    const V3Field *l = v3_field_named(f, nf, "left");
    const V3Field *r = v3_field_named(f, nf, "right");
    if (!spelled || !l || !r) {
        rc = pfail(err, GSQL_PROTOCOL, "a path's filter has an operator `%s` it cannot compile", op);
        free(op);
        return rc;
    }
    free(op);
    put(out, "(");
    rc = filter_sql(tb, l->v, l->vlen, depth + 1, out, err);
    if (rc == GSQL_OK) {
        put(out, spelled);
        rc = filter_sql(tb, r->v, r->vlen, depth + 1, out, err);
    }
    put(out, ")");
    return rc;
}

/* ---- the statement -------------------------------------------------- */

int gsql_path_compile(const GsEngine *e, GsTxn *t, const unsigned char *p, size_t n, GsqlBuf *sql,
                      int *uses_parameters, GsqlError *err) {
    *uses_parameters = 1;
    V3Field f[PATH_MAX_FIELDS];
    long nf = v3_record_fields(p, n, f, PATH_MAX_FIELDS);
    if (nf < 0) {
        return pfail(err, GSQL_PROTOCOL, "a path is a record");
    }
    char *table = field_text(f, nf, "table");
    char *mode = field_text(f, nf, "mode");
    if (!table || !mode) {
        free(table);
        free(mode);
        return pfail(err, GSQL_PROTOCOL, "a path needs a `table` and a `mode`");
    }
    int rows = strcmp(mode, "rows") == 0, count = strcmp(mode, "count") == 0, text = strcmp(mode, "sql") == 0;
    if (!rows && !count && !text) {
        pfail(err, GSQL_PROTOCOL, "a path's mode is rows, count or sql, not `%s`", mode);
        free(table);
        free(mode);
        return GSQL_PROTOCOL;
    }
    free(mode);
    GsqlTable *tb = NULL;
    int rc = open_table(e, t, table, &tb, err);
    free(table);
    if (rc != GSQL_OK) {
        return rc;
    }
    GsqlBuf b = {0};
    put(&b, count ? "SELECT count(*) AS \"count\" FROM " : "SELECT * FROM ");
    put_name(&b, gsql_table_name(tb));
    int where = 0;

    const V3Field *kf = v3_field_named(f, nf, "key");
    if (kf) {
        GsBytes items[PATH_MAX_KEY];
        long nk = v3_array_items(kf->v, kf->vlen, items, PATH_MAX_KEY);
        size_t want = gsql_table_nkey(tb);
        const unsigned *cols = gsql_table_key_cols(tb);
        if (nk < 0) {
            rc = pfail(err, GSQL_PROTOCOL, "a path's key is an array of parameter numbers");
            goto out;
        }
        if ((size_t)nk != want) {
            GsqlBuf names = {0};
            for (size_t i = 0; i < want; i++) {
                put(&names, i ? ", " : "");
                put(&names, gsql_table_col(tb, cols[i])->name);
            }
            v3_put(&names, "", 1);
            rc = pfail(err, GSQL_SYNTAX,
                       "`%s`'s primary key is %zu column%s (%s), and the path gave %ld value%s -- a key gives "
                       "one value for each, in that order",
                       gsql_table_name(tb), want, want == 1 ? "" : "s", (const char *)names.bytes, nk,
                       nk == 1 ? "" : "s");
            snprintf(err->table, sizeof err->table, "%s", gsql_table_name(tb));
            gsql_buf_free(&names);
            goto out;
        }
        for (long i = 0; i < nk; i++) {
            uint64_t k;
            if (!v3_read_u64(items[i].p, items[i].n, &k) || k < 1) {
                rc = pfail(err, GSQL_PROTOCOL, "a path's key names a parameter by its number");
                goto out;
            }
            put(&b, where ? " AND " : " WHERE ");
            where = 1;
            put_name(&b, gsql_table_col(tb, cols[i])->name);
            put(&b, " = ");
            put_param(&b, k);
        }
    }
    const V3Field *ff = v3_field_named(f, nf, "filter");
    if (ff) {
        put(&b, where ? " AND " : " WHERE ");
        where = 1;
        rc = filter_sql(tb, ff->v, ff->vlen, 0, &b, err);
        if (rc != GSQL_OK) {
            goto out;
        }
    }
    if (!count) {
        /* primary key order, always (docs/gsql_paths_design.md §3.3) */
        size_t nkey = gsql_table_nkey(tb);
        const unsigned *cols = gsql_table_key_cols(tb);
        for (size_t i = 0; i < nkey; i++) {
            put(&b, i ? ", " : " ORDER BY ");
            put_name(&b, gsql_table_col(tb, cols[i])->name);
        }
    }
    if (text) {
        /* what the path is, answered as one row and nothing run: the text
         * as a string literal, so it needs none of the parameters */
        GsqlBuf q = {0};
        put(&q, "SELECT '");
        for (size_t i = 0; i < b.length; i++) {
            v3_put(&q, &b.bytes[i], 1);
            if (b.bytes[i] == '\'') {
                v3_put(&q, "'", 1);
            }
        }
        put(&q, "' AS \"sql\"");
        gsql_buf_free(&b);
        b = q;
        *uses_parameters = 0;
    }
    v3_put(&b, "", 1);
    b.length--;   /* NUL-terminated, not counted */
    *sql = b;
    b.bytes = NULL;
    rc = GSQL_OK;
out:
    gsql_buf_free(&b);
    gsql_table_close(tb);
    return rc;
}

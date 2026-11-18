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
 *   { levels: [{ table, key: [n, ...], filter: tree, cols: [name, ...] }, ...],
 *     mode: "rows" | "count" | "sql" | "fkeys" | "update" | "keyinsert" |
 *           "insert" | "delete", expect: "one" | "many",
 *     set: [{ col, param }, ...],            -- update and keyinsert
 *     rows: [[{ col, param }, ...], ...] }   -- insert
 * A level is a table; each after the first is a STEP to it across the one
 * foreign key joining it to the level before. `key` holds the parameter
 * numbers of the primary key's values, in the key's column order. `expect` is
 * what the client decided the path is, from the foreign keys it holds; one
 * that is now many is refused STALE, so a client holding stale keys can cost
 * a round trip and never a wrong answer (§8.2). A filter tree node is one of
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
#define PATH_MAX_SET 256

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

/* a column of a level: `"t1"."city"`, or `"city"` in a path of one table */
static void put_col(GsqlBuf *b, const char *alias, const char *col) {
    if (alias) {
        put_name(b, alias);
        v3_put(b, ".", 1);
    }
    put_name(b, col);
}

static int filter_sql(const GsqlTable *tb, const char *alias, const unsigned char *p, size_t n, int depth,
                      GsqlBuf *out, GsqlError *err) {
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
        put_col(out, alias, real);
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
        rc = filter_sql(tb, alias, a->v, a->vlen, depth + 1, out, err);
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
    rc = filter_sql(tb, alias, l->v, l->vlen, depth + 1, out, err);
    if (rc == GSQL_OK) {
        put(out, spelled);
        rc = filter_sql(tb, alias, r->v, r->vlen, depth + 1, out, err);
    }
    put(out, ")");
    return rc;
}

/* ---- the foreign keys ------------------------------------------------ */

#define PATH_MAX_COLS 16

typedef struct {
    char *name;
    char *child;       /* the table holding the key */
    char *parent;      /* the table it references */
    size_t ncols;
    char *cols[PATH_MAX_COLS];      /* the child's columns */
    char *refcols[PATH_MAX_COLS];   /* the parent's, in the same order; none: its primary key */
    size_t nref;
    int bound;
} PathFk;

typedef struct {
    const GsEngine *e;
    GsTxn *t;
    PathFk *k;
    size_t n;
} FkList;

static void text_items(const V3Field *f, char **out, size_t *n) {
    *n = 0;
    if (!f) {
        return;
    }
    GsBytes items[PATH_MAX_COLS];
    long m = v3_array_items(f->v, f->vlen, items, PATH_MAX_COLS);
    for (long i = 0; i < m; i++) {
        out[(*n)++] = v3_read_text(items[i].p, items[i].n);
    }
}

static int fk_each(void *ctx, const char *name) {
    FkList *l = ctx;
    GsqlBuf b = {0};
    GsqlError quiet;
    if (gsql_fkey_get(l->e, l->t, name, &b, &quiet) != GSQL_OK) {
        gsql_buf_free(&b);
        return 0;
    }
    V3Field f[12];
    long n = v3_record_fields(b.bytes, b.length, f, 12);
    char *table = n > 0 ? field_text(f, n, "table") : NULL;
    char *ref = n > 0 ? field_text(f, n, "references") : NULL;
    if (table && ref) {
        l->k = realloc(l->k, (l->n + 1) * sizeof *l->k);
        if (!l->k) {
            abort();
        }
        PathFk *k = &l->k[l->n++];
        memset(k, 0, sizeof *k);
        k->name = malloc(strlen(name) + 1);
        if (!k->name) {
            abort();
        }
        strcpy(k->name, name);
        k->child = table;
        k->parent = ref;
        text_items(v3_field_named(f, n, "columns"), k->cols, &k->ncols);
        text_items(v3_field_named(f, n, "ref_columns"), k->refcols, &k->nref);
        const V3Field *bd = v3_field_named(f, n, "bound");
        if (bd) {
            v3_read_bool(bd->v, bd->vlen, &k->bound);
        }
        table = ref = NULL;
    }
    free(table);
    free(ref);
    gsql_buf_free(&b);
    return 0;
}

static void fk_free(FkList *l) {
    for (size_t i = 0; i < l->n; i++) {
        free(l->k[i].name);
        free(l->k[i].child);
        free(l->k[i].parent);
        for (size_t j = 0; j < l->k[i].ncols; j++) free(l->k[i].cols[j]);
        for (size_t j = 0; j < l->k[i].nref; j++) free(l->k[i].refcols[j]);
    }
    free(l->k);
}

static void fk_load(const GsEngine *e, GsTxn *t, FkList *l) {
    memset(l, 0, sizeof *l);
    l->e = e;
    l->t = t;
    GsqlError quiet;
    gsql_fkey_list(e, t, fk_each, l, &quiet);
}

/* The one foreign key a step from `from` to `to` crosses: its index, `up`
 * set when it runs from child to parent; or -1 with the refusal. */
static long fk_between(const FkList *l, const char *from, const char *to, int *up, GsqlError *err) {
    long found = -1, count = 0, unbound = -1;
    GsqlBuf names = {0};
    for (size_t i = 0; i < l->n; i++) {
        const PathFk *k = &l->k[i];
        int fwd = strcmp(k->child, from) == 0 && strcmp(k->parent, to) == 0;
        int back = strcmp(k->child, to) == 0 && strcmp(k->parent, from) == 0;
        if (!fwd && !back) {
            continue;
        }
        if (!k->bound) {
            unbound = (long)i;
            continue;
        }
        put(&names, count ? ", " : "");
        put(&names, k->name);
        found = (long)i;
        count++;
    }
    v3_put(&names, "", 1);
    const char *list = (const char *)names.bytes;
    int rc = GSQL_OK;
    if (count == 1 && strcmp(from, to) == 0) {
        const PathFk *k = &l->k[found];
        rc = pfail(err, GSQL_NOT_FOUND,
                   "`%s` references itself by %s, so a step cannot tell a parent from a child -- filter by its "
                   "column: `.%s = ...` for the rows that reference one (docs/gsql_paths_design.md §5)",
                   from, k->name, k->ncols ? k->cols[0] : "?");
    } else if (count == 1) {
        *up = strcmp(l->k[found].child, from) == 0;
        gsql_buf_free(&names);
        return found;
    } else if (count > 1) {
        rc = pfail(err, GSQL_NOT_FOUND,
                   "`%s` and `%s` are joined by %ld foreign keys, %s, so a step cannot tell which is meant -- "
                   "filter by the key's column instead (docs/gsql_paths_design.md §5)",
                   from, to, count, list);
    } else if (unbound >= 0) {
        rc = pfail(err, GSQL_NOT_FOUND, "the foreign key %s joining `%s` and `%s` is not bound to its table, so it "
                   "cannot be stepped across", l->k[unbound].name, from, to);
    } else {
        rc = pfail(err, GSQL_NOT_FOUND, "`%s` and `%s` are not joined by a foreign key, so a path cannot step "
                   "from one to the other", from, to);
    }
    snprintf(err->table, sizeof err->table, "%s", to);
    gsql_buf_free(&names);
    (void)rc;
    return -1;
}

/* mode fkeys: every foreign key, as the rows a client keeps to decide one or
 * many before it sends a path (§8.2) */
static void fkeys_sql(const FkList *l, GsqlBuf *b) {
    if (!l->n) {
        put(b, "SELECT '' AS \"name\", '' AS \"child\", '' AS \"parent\", false AS \"bound\" WHERE 1 = 0");
        return;
    }
    for (size_t i = 0; i < l->n; i++) {
        const char *parts[3] = { l->k[i].name, l->k[i].child, l->k[i].parent };
        put(b, i ? " UNION ALL SELECT " : "SELECT ");
        for (int j = 0; j < 3; j++) {
            put(b, "'");
            for (const char *c = parts[j]; *c; c++) {
                v3_put(b, c, 1);
                if (*c == '\'') {
                    v3_put(b, "'", 1);
                }
            }
            put(b, "'");
            if (i == 0) {
                put(b, j == 0 ? " AS \"name\"" : j == 1 ? " AS \"child\"" : " AS \"parent\"");
            }
            put(b, ", ");
        }
        put(b, l->k[i].bound ? "true" : "false");
        if (i == 0) {
            put(b, " AS \"bound\"");
        }
    }
}

/* ---- the statement -------------------------------------------------- */

#define PATH_MAX_LEVELS 16

typedef struct {
    GsqlTable *tb;
    char alias[16];
    const V3Field *key;
    const V3Field *filter;
    const V3Field *cols;   /* the columns its braces choose, or NULL */
    long fk;        /* the step INTO this level, from the one before: -1 for the first */
    int up;         /* that step runs from child to parent */
    int one;        /* this level is one row */
} PathLevel;

typedef struct {
    PathLevel lv[PATH_MAX_LEVELS];
    size_t n;
    int aliased;
    FkList fks;
} PathPlan;

static const char *alias_of(const PathPlan *pp, size_t i) {
    return pp->aliased ? pp->lv[i].alias : NULL;
}

static const char *parent_col(const PathFk *k, const GsqlTable *parent, size_t j) {
    if (j < k->nref) {
        return k->refcols[j];
    }
    const unsigned *cols = gsql_table_key_cols(parent);
    return j < gsql_table_nkey(parent) ? gsql_table_col(parent, cols[j])->name : "?";
}

static int level_cond(const PathPlan *pp, size_t i, GsqlBuf *b, int *any, GsqlError *err);

static void and_sep(GsqlBuf *b, int *any) {
    put(b, *any ? " AND " : "");
    *any = 1;
}

/* how level i is joined to level i-1, the subquery holding everything before */
static int level_link(const PathPlan *pp, size_t i, GsqlBuf *b, int *any, GsqlError *err) {
    const PathLevel *cur = &pp->lv[i], *prev = &pp->lv[i - 1];
    const PathFk *k = &pp->fks.k[cur->fk];
    /* cur->up: the previous level is the child, this one the parent */
    const GsqlTable *parent = cur->up ? cur->tb : prev->tb;
    and_sep(b, any);
    if (prev->one && k->ncols == 1) {
        /* one row before: a value this level is sought by, so the key or an
           index on the column is used */
        put_col(b, alias_of(pp, i), cur->up ? parent_col(k, parent, 0) : k->cols[0]);
        put(b, " = (SELECT ");
        put_col(b, alias_of(pp, i - 1), cur->up ? k->cols[0] : parent_col(k, parent, 0));
    } else {
        put(b, "EXISTS (SELECT 1");
    }
    put(b, " FROM ");
    put_name(b, gsql_table_name(prev->tb));
    put(b, " AS ");
    put_name(b, prev->alias);
    int inner = 0;
    GsqlBuf w = {0};
    if (!(prev->one && k->ncols == 1)) {
        for (size_t j = 0; j < k->ncols; j++) {
            and_sep(&w, &inner);
            put_col(&w, alias_of(pp, i), cur->up ? parent_col(k, parent, j) : k->cols[j]);
            put(&w, " = ");
            put_col(&w, alias_of(pp, i - 1), cur->up ? k->cols[j] : parent_col(k, parent, j));
        }
    }
    int rc = level_cond(pp, i - 1, &w, &inner, err);
    if (rc == GSQL_OK && inner) {
        put(b, " WHERE ");
        v3_put(b, w.bytes, w.length);
    }
    gsql_buf_free(&w);
    put(b, ")");
    return rc;
}

/* level i's own conditions -- its key, its filter, its link to what came
 * before -- each joined to what `b` holds by AND */
static int level_own(const PathPlan *pp, size_t i, GsqlBuf *b, int *any, GsqlError *err);

static int level_cond(const PathPlan *pp, size_t i, GsqlBuf *b, int *any, GsqlError *err) {
    int rc = level_own(pp, i, b, any, err);
    if (rc == GSQL_OK && i > 0) {
        rc = level_link(pp, i, b, any, err);
    }
    return rc;
}

/* level i's key and filter, without its link to the level before */
static int level_own(const PathPlan *pp, size_t i, GsqlBuf *b, int *any, GsqlError *err) {
    const PathLevel *lv = &pp->lv[i];
    if (lv->key) {
        GsBytes items[PATH_MAX_KEY];
        long nk = v3_array_items(lv->key->v, lv->key->vlen, items, PATH_MAX_KEY);
        size_t want = gsql_table_nkey(lv->tb);
        const unsigned *cols = gsql_table_key_cols(lv->tb);
        if (nk < 0) {
            return pfail(err, GSQL_PROTOCOL, "a path's key is an array of parameter numbers");
        }
        if ((size_t)nk != want) {
            GsqlBuf names = {0};
            for (size_t j = 0; j < want; j++) {
                put(&names, j ? ", " : "");
                put(&names, gsql_table_col(lv->tb, cols[j])->name);
            }
            v3_put(&names, "", 1);
            pfail(err, GSQL_SYNTAX,
                  "`%s`'s primary key is %zu column%s (%s), and the path gave %ld value%s -- a key gives one value "
                  "for each, in that order",
                  gsql_table_name(lv->tb), want, want == 1 ? "" : "s", (const char *)names.bytes, nk,
                  nk == 1 ? "" : "s");
            snprintf(err->table, sizeof err->table, "%s", gsql_table_name(lv->tb));
            gsql_buf_free(&names);
            return GSQL_SYNTAX;
        }
        for (long j = 0; j < nk; j++) {
            uint64_t k;
            if (!v3_read_u64(items[j].p, items[j].n, &k) || k < 1) {
                return pfail(err, GSQL_PROTOCOL, "a path's key names a parameter by its number");
            }
            and_sep(b, any);
            put_col(b, alias_of(pp, i), gsql_table_col(lv->tb, cols[j])->name);
            put(b, " = ");
            put_param(b, k);
        }
    }
    if (lv->filter) {
        and_sep(b, any);
        int rc = filter_sql(lv->tb, alias_of(pp, i), lv->filter->v, lv->filter->vlen, 0, b, err);
        if (rc != GSQL_OK) {
            return rc;
        }
    }
    return GSQL_OK;
}

/* ---- braces (docs/gsql_paths_design.md §3.5) ----------------------------- */

/* the select list of the braced levels from `from` on, each column named as
   the catalog names it; two levels giving one name are refused */
static int select_list(const PathPlan *pp, size_t from, GsqlBuf *b, GsqlError *err) {
    const char *given[PATH_MAX_SET];
    const char *owner[PATH_MAX_SET];
    size_t ngiven = 0;
    for (size_t i = from; i < pp->n; i++) {
        const PathLevel *lv = &pp->lv[i];
        if (!lv->cols) {
            continue;
        }
        GsBytes items[PATH_MAX_SET];
        long m = v3_array_items(lv->cols->v, lv->cols->vlen, items, PATH_MAX_SET);
        for (long j = 0; j < m; j++) {
            char *name = v3_read_text(items[j].p, items[j].n);
            const char *real = name ? column_named(lv->tb, name, err) : NULL;
            free(name);
            if (!real) {
                return err->code ? err->code : GSQL_PROTOCOL;
            }
            for (size_t k = 0; k < ngiven; k++) {
                if (strcmp(given[k], real) == 0) {
                    if (strcmp(owner[k], gsql_table_name(lv->tb)) == 0) {
                        return pfail(err, GSQL_SYNTAX, "`%s` is named twice", real);
                    }
                    return pfail(err, GSQL_SYNTAX, "`%s` and `%s` both give `%s` -- leave one out", owner[k],
                                 gsql_table_name(lv->tb), real);
                }
            }
            if (ngiven < PATH_MAX_SET) {
                given[ngiven] = real;
                owner[ngiven++] = gsql_table_name(lv->tb);
            }
            put(b, ngiven > 1 ? ", " : "");
            put_col(b, alias_of(pp, i), real);
            put(b, " AS ");
            put_name(b, real);
        }
    }
    return GSQL_OK;
}

/* the ON of a join from level i-1 to level i: its key's columns, equal */
static void join_on(const PathPlan *pp, size_t i, GsqlBuf *b) {
    const PathLevel *cur = &pp->lv[i], *prev = &pp->lv[i - 1];
    const PathFk *k = &pp->fks.k[cur->fk];
    const GsqlTable *parent = cur->up ? cur->tb : prev->tb;
    for (size_t j = 0; j < k->ncols; j++) {
        put(b, j ? " AND " : "");
        put_col(b, alias_of(pp, i), cur->up ? parent_col(k, parent, j) : k->cols[j]);
        put(b, " = ");
        put_col(b, alias_of(pp, i - 1), cur->up ? k->cols[j] : parent_col(k, parent, j));
    }
}

/* ---- writing ------------------------------------------------------- */


typedef struct {
    const char *col;   /* the catalog's name */
    uint64_t param;
} PathSet;

/* a list of { col, param } as the catalog's columns of `tb`: the count, or -1
   with the refusal. A key column is refused when `no_key`. */
static long set_list(const GsqlTable *tb, const unsigned char *p, size_t n, int no_key, PathSet *out,
                     GsqlError *err) {
    GsBytes items[PATH_MAX_SET];
    long m = v3_array_items(p, n, items, PATH_MAX_SET);
    if (m < 0) {
        pfail(err, GSQL_PROTOCOL, "a path's columns to write are an array of { col, param }");
        return -1;
    }
    for (long i = 0; i < m; i++) {
        V3Field f[4];
        long nf = v3_record_fields(items[i].p, items[i].n, f, 4);
        char *col = nf > 0 ? field_text(f, nf, "col") : NULL;
        const V3Field *pf = nf > 0 ? v3_field_named(f, nf, "param") : NULL;
        if (!col || !pf || !v3_read_u64(pf->v, pf->vlen, &out[i].param)) {
            free(col);
            pfail(err, GSQL_PROTOCOL, "a path's column to write is { col, param }");
            return -1;
        }
        out[i].col = column_named(tb, col, err);
        free(col);
        if (!out[i].col) {
            return -1;
        }
        for (size_t k = 0; no_key && k < gsql_table_nkey(tb); k++) {
            if (strcmp(gsql_table_col(tb, gsql_table_key_cols(tb)[k])->name, out[i].col) == 0) {
                pfail(err, GSQL_SYNTAX, "`%s` is `%s`'s key, which a path names and does not change -- a key is "
                      "changed by a sql block", out[i].col, gsql_table_name(tb));
                snprintf(err->table, sizeof err->table, "%s", gsql_table_name(tb));
                snprintf(err->column, sizeof err->column, "%s", out[i].col);
                return -1;
            }
        }
        for (long j = 0; j < i; j++) {
            if (strcmp(out[j].col, out[i].col) == 0) {
                pfail(err, GSQL_SYNTAX, "`%s` is written twice", out[i].col);
                return -1;
            }
        }
    }
    return m;
}

static int path_write_sql(const PathPlan *pp, const V3Field *f, long nf, char mode, GsqlBuf *b, GsqlError *err) {
    size_t n = pp->n;
    const PathLevel *last = &pp->lv[n - 1];
    const char *table = gsql_table_name(last->tb);
    PathSet *set = calloc(PATH_MAX_SET, sizeof *set);
    if (!set) {
        abort();
    }
    int rc = GSQL_OK;
    long nset = 0;
    if (mode == 'u' || mode == 'p') {
        const V3Field *sf = v3_field_named(f, nf, "set");
        nset = sf ? set_list(last->tb, sf->v, sf->vlen, 1, set, err) : 0;
        if (nset < 0) {
            rc = err->code;
            goto out;
        }
        if (nset == 0) {
            rc = pfail(err, GSQL_SYNTAX, "a record written to a path names a column of `%s`", table);
            goto out;
        }
    }
    if (mode == 'd' || mode == 'u') {
        put(b, mode == 'd' ? "DELETE FROM " : "UPDATE ");
        put_name(b, table);
        if (pp->aliased) {
            put(b, " AS ");
            put_name(b, last->alias);
        }
        for (long i = 0; mode == 'u' && i < nset; i++) {
            put(b, i ? ", " : " SET ");
            put_name(b, set[i].col);
            put(b, " = ");
            put_param(b, set[i].param);
        }
        GsqlBuf w = {0};
        int any = 0;
        rc = level_cond(pp, n - 1, &w, &any, err);
        if (rc == GSQL_OK && any) {
            put(b, " WHERE ");
            v3_put(b, w.bytes, w.length);
        }
        gsql_buf_free(&w);
        goto out;
    }
    if (mode == 'p') {
        /* a key path of one table whose row the UPDATE before this did not
           find: inserted with that key, by INSERT's rules (§10 A2). Not
           ON CONFLICT DO UPDATE, which checks the row it proposes -- NOT NULL
           among the rest -- before it finds the conflict, so an update of a
           row that is there would be refused for columns it never named. */
        if (n != 1 || !last->key) {
            rc = pfail(err, GSQL_PROTOCOL, "only a key path of one table is inserted with its key");
            goto out;
        }
        GsBytes items[PATH_MAX_KEY];
        long nk = v3_array_items(last->key->v, last->key->vlen, items, PATH_MAX_KEY);
        size_t want = gsql_table_nkey(last->tb);
        const unsigned *cols = gsql_table_key_cols(last->tb);
        GsqlBuf check = {0};
        int any = 0;
        rc = level_cond(pp, 0, &check, &any, err);   /* the key's count, refused as a read refuses it */
        gsql_buf_free(&check);
        if (rc != GSQL_OK) {
            goto out;
        }
        put(b, "INSERT INTO ");
        put_name(b, table);
        put(b, " (");
        for (size_t k = 0; k < want; k++) {
            put(b, k ? ", " : "");
            put_name(b, gsql_table_col(last->tb, cols[k])->name);
        }
        for (long i = 0; i < nset; i++) {
            put(b, ", ");
            put_name(b, set[i].col);
        }
        put(b, ") VALUES (");
        for (long k = 0; k < nk; k++) {
            uint64_t pn = 0;
            v3_read_u64(items[k].p, items[k].n, &pn);
            put(b, k ? ", " : "");
            put_param(b, pn);
        }
        for (long i = 0; i < nset; i++) {
            put(b, ", ");
            put_param(b, set[i].param);
        }
        put(b, ")");
        goto out;
    }
    /* insert */
    if (last->key || last->filter) {
        rc = pfail(err, GSQL_SYNTAX, "insert adds rows to a table, and this path names rows of `%s` by a %s: "
                   "insert through the table, `.%s.insert(...)`", table, last->key ? "key" : "filter", table);
        goto out;
    }
    const PathFk *k = NULL;
    const PathLevel *prev = n > 1 ? &pp->lv[n - 2] : NULL;
    if (prev) {
        k = &pp->fks.k[last->fk];
        if (last->up) {
            rc = pfail(err, GSQL_SYNTAX, "a step from `%s` up to `%s` inserts nothing: insert into `%s` itself",
                       gsql_table_name(prev->tb), table, table);
            goto out;
        }
        if (!prev->one) {
            rc = pfail(err, GSQL_SYNTAX, "a step from many rows of `%s` inserts nothing, since it would not say "
                       "which one the new rows of `%s` belong to: name one by its key first",
                       gsql_table_name(prev->tb), table);
            goto out;
        }
    }
    const V3Field *rf = v3_field_named(f, nf, "rows");
    GsBytes rows[PATH_MAX_SET];
    long nrows = rf ? v3_array_items(rf->v, rf->vlen, rows, PATH_MAX_SET) : -1;
    if (nrows < 1) {
        rc = pfail(err, GSQL_PROTOCOL, "an insert by path carries `rows`, at least one, at most %d", PATH_MAX_SET);
        goto out;
    }
    /* every column any row names, in the order first named */
    const char *cols[PATH_MAX_SET];
    size_t ncols = 0;
    PathSet *each = calloc((size_t)nrows * PATH_MAX_SET, sizeof *each);
    long *counts = calloc((size_t)nrows, sizeof *counts);
    if (!each || !counts) {
        abort();
    }
    for (long r = 0; r < nrows && rc == GSQL_OK; r++) {
        counts[r] = set_list(last->tb, rows[r].p, rows[r].n, 0, each + r * PATH_MAX_SET, err);
        if (counts[r] < 0) {
            rc = err->code;
            break;
        }
        for (long i = 0; i < counts[r]; i++) {
            const char *c = each[r * PATH_MAX_SET + i].col;
            for (size_t j = 0; k && j < k->ncols; j++) {
                if (strcmp(k->cols[j], c) == 0) {
                    rc = pfail(err, GSQL_SYNTAX, "`%s` comes from the path, which names the `%s` the new row "
                               "belongs to; leave it out of the record", c, gsql_table_name(prev->tb));
                    snprintf(err->column, sizeof err->column, "%s", c);
                }
            }
            size_t at = 0;
            while (at < ncols && strcmp(cols[at], c) != 0) {
                at++;
            }
            if (at == ncols && ncols < PATH_MAX_SET) {
                cols[ncols++] = c;
            }
        }
    }
    if (rc == GSQL_OK && ncols == 0 && !k) {
        rc = pfail(err, GSQL_SYNTAX, "a record inserted by path names a column of `%s`", table);
    }
    if (rc == GSQL_OK && k) {
        for (long r = 0; r < nrows && rc == GSQL_OK; r++) {
            if ((size_t)counts[r] != ncols) {
                rc = pfail(err, GSQL_SYNTAX, "rows inserted through a step name the same columns; row %ld names "
                           "%ld of the %zu", r + 1, counts[r], ncols);
            }
        }
    }
    if (rc == GSQL_OK) {
        put(b, "INSERT INTO ");
        put_name(b, table);
        put(b, " (");
        for (size_t j = 0; j < ncols; j++) {
            put(b, j ? ", " : "");
            put_name(b, cols[j]);
        }
        for (size_t j = 0; k && j < k->ncols; j++) {
            put(b, ncols || j ? ", " : "");
            put_name(b, k->cols[j]);
        }
        put(b, ")");
        for (long r = 0; r < nrows && rc == GSQL_OK; r++) {
            if (!k) {
                put(b, r ? ", (" : " VALUES (");
            } else {
                put(b, r ? " UNION ALL SELECT " : " SELECT ");
            }
            for (size_t j = 0; j < ncols; j++) {
                put(b, j ? ", " : "");
                long hit = -1;
                for (long i = 0; i < counts[r]; i++) {
                    if (strcmp(each[r * PATH_MAX_SET + i].col, cols[j]) == 0) {
                        hit = i;
                    }
                }
                if (hit >= 0) {
                    put_param(b, each[r * PATH_MAX_SET + hit].param);
                } else {
                    put(b, "DEFAULT");
                }
            }
            if (!k) {
                put(b, ")");
                continue;
            }
            /* the parent's key, read from the row the step came from */
            const GsqlTable *parent = prev->tb;
            for (size_t j = 0; j < k->ncols; j++) {
                put(b, ncols || j ? ", " : "");
                put_col(b, prev->alias, parent_col(k, parent, j));
            }
            put(b, " FROM ");
            put_name(b, gsql_table_name(prev->tb));
            put(b, " AS ");
            put_name(b, prev->alias);
            GsqlBuf w = {0};
            int any = 0;
            rc = level_cond(pp, n - 2, &w, &any, err);
            if (rc == GSQL_OK && any) {
                put(b, " WHERE ");
                v3_put(b, w.bytes, w.length);
            }
            gsql_buf_free(&w);
        }
        put(b, " RETURNING *");
    }
    free(each);
    free(counts);
out:
    free(set);
    return rc;
}

int gsql_path_compile(const GsEngine *e, GsTxn *t, const unsigned char *p, size_t n, GsqlBuf *sql,
                      int *uses_parameters, GsqlError *err) {
    *uses_parameters = 1;
    V3Field f[PATH_MAX_FIELDS];
    long nf = v3_record_fields(p, n, f, PATH_MAX_FIELDS);
    if (nf < 0) {
        return pfail(err, GSQL_PROTOCOL, "a path is a record");
    }
    char *mode = field_text(f, nf, "mode");
    if (!mode) {
        return pfail(err, GSQL_PROTOCOL, "a path needs a `mode`");
    }
    int rows = strcmp(mode, "rows") == 0, count = strcmp(mode, "count") == 0, text = strcmp(mode, "sql") == 0,
        fkeys = strcmp(mode, "fkeys") == 0, update = strcmp(mode, "update") == 0,
        upsert = strcmp(mode, "keyinsert") == 0, insert = strcmp(mode, "insert") == 0,
        del = strcmp(mode, "delete") == 0;
    free(mode);
    if (!rows && !count && !text && !fkeys && !update && !upsert && !insert && !del) {
        return pfail(err, GSQL_PROTOCOL, "a path's mode is rows, count, sql, fkeys, update, keyinsert, insert or "
                     "delete");
    }
    PathPlan *pp = calloc(1, sizeof *pp);
    if (!pp) {
        abort();
    }
    fk_load(e, t, &pp->fks);
    GsqlBuf b = {0};
    int rc = GSQL_OK;
    if (fkeys) {
        fkeys_sql(&pp->fks, &b);
        *uses_parameters = 0;
        goto done;
    }
    const V3Field *lf = v3_field_named(f, nf, "levels");
    GsBytes items[PATH_MAX_LEVELS];
    long nl = lf ? v3_array_items(lf->v, lf->vlen, items, PATH_MAX_LEVELS) : -1;
    if (nl < 1) {
        rc = pfail(err, GSQL_PROTOCOL, "a path's `levels` is an array of at least one table, at most %d",
                   PATH_MAX_LEVELS);
        goto out;
    }
    pp->n = (size_t)nl;
    pp->aliased = nl > 1;
    V3Field lfields[PATH_MAX_LEVELS][PATH_MAX_FIELDS];
    for (long i = 0; i < nl; i++) {
        PathLevel *lv = &pp->lv[i];
        snprintf(lv->alias, sizeof lv->alias, "t%ld", i);
        lv->fk = -1;
        long m = v3_record_fields(items[i].p, items[i].n, lfields[i], PATH_MAX_FIELDS);
        char *table = m > 0 ? field_text(lfields[i], m, "table") : NULL;
        if (!table) {
            rc = pfail(err, GSQL_PROTOCOL, "each level of a path names its `table`");
            goto out;
        }
        rc = open_table(e, t, table, &lv->tb, err);
        free(table);
        if (rc != GSQL_OK) {
            goto out;
        }
        lv->key = v3_field_named(lfields[i], m, "key");
        lv->filter = v3_field_named(lfields[i], m, "filter");
        lv->cols = v3_field_named(lfields[i], m, "cols");
        if (i > 0) {
            lv->fk = fk_between(&pp->fks, gsql_table_name(pp->lv[i - 1].tb), gsql_table_name(lv->tb), &lv->up, err);
            if (lv->fk < 0) {
                rc = err->code;
                goto out;
            }
        }
        lv->one = lv->key != NULL || (i > 0 && lv->up && pp->lv[i - 1].one);
    }
    /* braces before the last table join from the first of them: one row only
       when every table from there to the last is one (§3.5) */
    long first_braced = -1;
    int path_one = pp->lv[nl - 1].one;
    for (long i = 0; i < nl; i++) {
        if (pp->lv[i].cols && first_braced < 0) {
            first_braced = i;
        }
    }
    if (first_braced >= 0 && first_braced < nl - 1) {
        for (long i = first_braced; i < nl; i++) {
            path_one = path_one && pp->lv[i].one;
        }
    }
    char *expect = field_text(f, nf, "expect");
    int said_one = expect && strcmp(expect, "one") == 0;
    free(expect);
    if (said_one && !path_one) {
        rc = pfail(err, GSQL_STALE, "the path was read as one row, from foreign keys this client held, and the "
                   "database's own make it many");
        goto out;
    }
    if ((update || upsert || insert || del) && first_braced >= 0) {
        rc = pfail(err, GSQL_SYNTAX, "a path with braces chooses columns to read; write through the path without "
                   "them");
        goto out;
    }
    if (update || upsert || insert || del) {
        rc = path_write_sql(pp, f, nf, update ? 'u' : upsert ? 'p' : insert ? 'i' : 'd', &b, err);
        if (rc != GSQL_OK) {
            goto out;
        }
        goto done;
    }
    int join = first_braced >= 0 && first_braced < nl - 1;
    size_t from = join ? (size_t)first_braced : (size_t)nl - 1;
    if (count) {
        put(&b, "SELECT count(*) AS \"count\" FROM ");
    } else if (first_braced >= 0) {
        put(&b, "SELECT ");
        rc = select_list(pp, from, &b, err);
        if (rc != GSQL_OK) {
            goto out;
        }
        put(&b, " FROM ");
    } else {
        put(&b, "SELECT * FROM ");
    }
    put_name(&b, gsql_table_name(pp->lv[from].tb));
    if (pp->aliased) {
        put(&b, " AS ");
        put_name(&b, pp->lv[from].alias);
    }
    for (size_t i = from + 1; i < (size_t)nl; i++) {
        put(&b, " JOIN ");
        put_name(&b, gsql_table_name(pp->lv[i].tb));
        put(&b, " AS ");
        put_name(&b, pp->lv[i].alias);
        put(&b, " ON ");
        join_on(pp, i, &b);
    }
    GsqlBuf w = {0};
    int any = 0;
    /* the first table read takes its link to the levels before it; each
       joined one after only its own key and filter */
    rc = level_cond(pp, from, &w, &any, err);
    for (size_t i = from + 1; rc == GSQL_OK && i < (size_t)nl; i++) {
        rc = level_own(pp, i, &w, &any, err);
    }
    if (rc != GSQL_OK) {
        gsql_buf_free(&w);
        goto out;
    }
    if (any) {
        put(&b, " WHERE ");
        v3_put(&b, w.bytes, w.length);
    }
    gsql_buf_free(&w);
    if (!count) {
        /* primary key order, always (docs/gsql_paths_design.md §3.3): the
           last table's, then back through the tables joined */
        int first = 1;
        for (size_t lvl = (size_t)nl; lvl-- > from;) {
            const GsqlTable *tb = pp->lv[lvl].tb;
            size_t nkey = gsql_table_nkey(tb);
            const unsigned *cols = gsql_table_key_cols(tb);
            for (size_t i = 0; i < nkey; i++) {
                put(&b, first ? " ORDER BY " : ", ");
                first = 0;
                put_col(&b, alias_of(pp, lvl), gsql_table_col(tb, cols[i])->name);
            }
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
done:
    v3_put(&b, "", 1);
    b.length--;   /* NUL-terminated, not counted */
    *sql = b;
    b.bytes = NULL;
    rc = GSQL_OK;
out:
    gsql_buf_free(&b);
    for (size_t i = 0; i < PATH_MAX_LEVELS; i++) {
        if (pp->lv[i].tb) {
            gsql_table_close(pp->lv[i].tb);
        }
    }
    fk_free(&pp->fks);
    free(pp);
    return rc;
}

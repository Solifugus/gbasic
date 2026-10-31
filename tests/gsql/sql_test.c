/* G-SQL's SQL layer run in process against the interpreter's host
 * (docs/gsql_sql_design.md §7): the semantics, differential and explain
 * tiers. Linked against libgbasic.a, so every operator is gBASIC's own.
 *
 *   sql_test script FILE DIR    a script of statements and their answers
 *   sql_test diff DIR SEED N    random rows and predicates, each query run with
 *                               access paths and forced to a scan
 *
 * A script case is a `>` line of SQL, optional `@ name = expr` parameters
 * (each expr is evaluated by running SELECT expr, so a parameter can be money
 * or a date), and then `=` and the answer, `!` and a refusal (code, position,
 * a part of the message), or `%` and the result's columns. `begin write`,
 * `begin read`, `commit` and `rollback` lines hold a transaction across cases,
 * as a connection does; outside one, each statement runs in its own. */

#include "gsql_sql.h"
#include "gsql_store.h"
#include "gsql_wire.h"
#include "serialize_tags.h"
#include "../../src/gsql/v3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const GsEngine *E = &gs_engine_lsm;
static GsStore *db;
static GsqlSql *sql;
static GsqlService svc;
static GsTxn *held;     /* a transaction a script holds open */
static int held_write;
static int checks, mismatches;

/* ---- rendering a value ---------------------------------------------- */

static void render(GsqlBuf *o, const unsigned char *v, size_t n) {
    char b[64];
    switch (v[0]) {
    case SER_NULL:
    case SER_UNKNOWN:
        v3_put(o, "NULL", 4);
        return;
    case SER_BOOL:
        v3_put(o, v[1] ? "TRUE" : "FALSE", v[1] ? 4 : 5);
        return;
    case SER_NUMBER: {
        uint64_t bits = 0;
        for (int k = 0; k < 8; k++) {
            bits |= (uint64_t)v[2 + k] << (8 * k);
        }
        if (v[1]) {
            snprintf(b, sizeof b, "%lld", (long long)bits);
        } else {
            double d;
            memcpy(&d, &bits, sizeof d);
            snprintf(b, sizeof b, "%.17g", d);
            /* the shortest form that reads back, and a mark that it is inexact */
            for (int prec = 1; prec <= 17; prec++) {
                char t[64];
                snprintf(t, sizeof t, "%.*g", prec, d);
                if (strtod(t, NULL) == d) {
                    snprintf(b, sizeof b, "%s", t);
                    break;
                }
            }
            if (!strpbrk(b, ".eEni")) {
                strcat(b, ".0");
            }
        }
        v3_put(o, b, strlen(b));
        return;
    }
    case SER_STRING: {
        V3In in = { v, n, 1, 1 };
        uint64_t len = v3_in_uleb(&in);
        v3_put(o, "'", 1);
        v3_put(o, v + in.pos, len);
        v3_put(o, "'", 1);
        return;
    }
    default: {
        /* money, a date-time, a record: as gBASIC's string() writes it */
        const GsqlHost *h = gb_gsql_host();
        GsqlError err = { GSQL_OK, "", 0 };
        GsqlHv *x = h->load(h->ctx, v, n, &err);
        GsqlHv *args[1] = { x };
        GsqlHv *s = x ? h->call(h->ctx, "string", args, 1, &err) : NULL;
        GsqlBuf t = {0};
        if (s && h->store(h->ctx, s, &t, &err) == GSQL_OK && t.bytes[0] == SER_STRING) {
            V3In in = { t.bytes, t.length, 1, 1 };
            uint64_t len = v3_in_uleb(&in);
            const char *tag = v[0] == SER_MONEY ? "money " : v[0] == SER_DATETIME ? "datetime " : "";
            v3_put(o, tag, strlen(tag));
            v3_put(o, t.bytes + in.pos, len);
        } else {
            v3_put(o, "<?>", 3);
        }
        gsql_buf_free(&t);
        if (s) {
            h->release(h->ctx, s);
        }
        if (x) {
            h->release(h->ctx, x);
        }
        return;
    }
    }
}

/* A batch's rows, however many. A fixed cap here used to render a larger
 * batch as nothing at all, so a 6,000-row join read as "(none)" -- and two
 * large answers then agreed with each other whatever they held. */
static long rows_of(const GsqlBuf *rows, GsBytes **out) {
    for (long cap = 4096;; cap *= 4) {
        GsBytes *v = malloc(sizeof(GsBytes) * (size_t)cap);
        if (!v) {
            abort();
        }
        long n = v3_array_items(rows->bytes, rows->length, v, cap);
        if (n >= 0 || cap > (1L << 24)) {
            if (n < 0) {
                fprintf(stderr, "a batch of rows could not be read\n");
                exit(2);
            }
            *out = v;
            return n;
        }
        free(v);
    }
}

static void render_rows(GsqlBuf *o, const GsqlBuf *rows, size_t *nrows) {
    GsBytes *items;
    long n = rows_of(rows, &items);
    for (long r = 0; r < n; r++) {
        if (*nrows) {
            v3_put(o, " | ", 3);
        }
        (*nrows)++;
        GsBytes cols[256];
        long nc = v3_array_items(items[r].p, items[r].n, cols, 256);
        for (long c = 0; c < nc; c++) {
            if (c) {
                v3_put(o, ", ", 2);
            }
            render(o, cols[c].p, cols[c].n);
        }
    }
    free(items);
}

static void render_columns(GsqlBuf *o, const GsqlBuf *columns) {
    GsBytes items[256];
    long n = v3_array_items(columns->bytes, columns->length, items, 256);
    for (long k = 0; k < n; k++) {
        V3Field f[4];
        long nf = v3_record_fields(items[k].p, items[k].n, f, 4);
        const V3Field *name = v3_field_named(f, nf, "name");
        const V3Field *kind = v3_field_named(f, nf, "kind");
        char *a = name ? v3_read_text(name->v, name->vlen) : NULL;
        char *b = kind ? v3_read_text(kind->v, kind->vlen) : NULL;
        char line[300];
        snprintf(line, sizeof line, "%s%s:%s", k ? ", " : "", a ? a : "?", b ? b : "?");
        v3_put(o, line, strlen(line));
        free(a);
        free(b);
    }
}

/* ---- running one statement ------------------------------------------ */

typedef struct {
    int code;
    GsqlError err;
    int part_way;
    int has_rows;
    GsqlBuf text;       /* the rendered answer */
    GsqlBuf columns;
    GsqlBuf first_value; /* the first row's first value, for parameters */
    GsqlBuf raw;         /* the first batch's rows as v3, for the grouping oracle */
} Answer;

static void answer_free(Answer *a) {
    gsql_buf_free(&a->text);
    gsql_buf_free(&a->columns);
    gsql_buf_free(&a->first_value);
    gsql_buf_free(&a->raw);
}

static void run(const char *text, const GsqlBuf *params, uint32_t batch, Answer *a) {
    memset(a, 0, sizeof *a);
    GsqlBuf stmt = {0};
    v3_w_text(&stmt, text);
    GsqlStatement st;
    memset(&st, 0, sizeof st);
    st.statement = stmt.bytes;
    st.statement_length = stmt.length;
    if (params && params->length) {
        st.parameters = params->bytes;
        st.parameters_length = params->length;
    }
    st.batch = batch;
    a->err.code = GSQL_OK;
    int w = svc.writes(svc.ctx, &st, &a->err);
    if (w < 0) {
        a->code = a->err.code;
        gsql_buf_free(&stmt);
        return;
    }
    GsTxn *t = held;
    if (!t) {
        int rc = w ? E->begin_write(db, &t) : E->begin_read(db, &t);
        if (rc) {
            fprintf(stderr, "begin failed: %s\n", gs_status_name(rc));
            exit(2);
        }
    } else if (w && !held_write) {
        a->code = GSQL_READ_ONLY;
        snprintf(a->err.message, sizeof a->err.message, "the statement writes, and this transaction began READ");
        gsql_buf_free(&stmt);
        return;
    }
    GsqlResult res;
    memset(&res, 0, sizeof res);
    int rc = svc.run(svc.ctx, E, t, &st, &res, &a->err);
    a->code = rc;
    a->part_way = res.failed_part_way;
    if (rc == GSQL_OK && res.has_rows) {
        a->has_rows = 1;
        render_columns(&a->columns, &res.columns);
        size_t nrows = 0;
        render_rows(&a->text, &res.rows, &nrows);
        v3_put(&a->raw, res.rows.bytes, res.rows.length);
        GsBytes items[1];
        if (v3_array_items(res.rows.bytes, res.rows.length, items, 1) == 1) {
            GsBytes cols[1];
            if (v3_array_items(items[0].p, items[0].n, cols, 1) >= 1) {
                v3_put(&a->first_value, cols[0].p, cols[0].n);
            }
        }
        void *cur = res.cursor;
        while (cur && a->code == GSQL_OK) {
            GsqlResult more;
            memset(&more, 0, sizeof more);
            a->code = svc.fetch(svc.ctx, cur, batch, &more, &a->err);
            if (a->code == GSQL_OK) {
                render_rows(&a->text, &more.rows, &nrows);
            }
            cur = more.cursor;
            gsql_buf_free(&more.rows);
            gsql_buf_free(&more.columns);
        }
        if (cur) {
            svc.close(svc.ctx, cur);
        }
        if (nrows == 0) {
            v3_put(&a->text, "(none)", 6);
        }
    } else if (rc == GSQL_OK) {
        char b[64];
        snprintf(b, sizeof b, "done %llu", (unsigned long long)res.affected);
        v3_put(&a->text, b, strlen(b));
    }
    gsql_buf_free(&res.rows);
    gsql_buf_free(&res.columns);
    if (!held) {
        if (w && a->code == GSQL_OK) {
            uint64_t id;
            if (E->commit(t, 1, &id)) {
                fprintf(stderr, "commit failed\n");
                exit(2);
            }
        } else {
            E->abort(t);
        }
    }
    gsql_buf_free(&stmt);
    v3_put(&a->text, "", 0);
}

/* ---- the script tier ------------------------------------------------- */

static void mismatch(int line, const char *sql, const char *got, const char *want) {
    mismatches++;
    printf("MISMATCH line %d: %s\n  got:  %s\n  want: %s\n", line, sql, got, want);
}

static char *as_text(const GsqlBuf *b) {
    char *s = malloc(b->length + 1);
    if (b->length) {
        memcpy(s, b->bytes, b->length);
    }
    s[b->length] = 0;
    return s;
}

static int script(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return 2;
    }
    char line[8192], sql[8192] = "";
    int lineno = 0, sqlline = 0;
    /* parameters, gathered as one v3 record */
    char *pnames[32];
    GsqlBuf pvals[32];
    int np = 0;
    Answer last;
    int have_answer = 0;
    memset(&last, 0, sizeof last);
    while (fgets(line, sizeof line, f)) {
        lineno++;
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, "begin write") == 0 || strcmp(line, "begin read") == 0) {
            held_write = line[6] == 'w';
            if ((held_write ? E->begin_write(db, &held) : E->begin_read(db, &held)) != 0) {
                fprintf(stderr, "line %d: begin failed\n", lineno);
                return 2;
            }
            continue;
        }
        if (strcmp(line, "commit") == 0 || strcmp(line, "rollback") == 0) {
            if (held) {
                uint64_t id;
                if (line[0] == 'c' && held_write) {
                    E->commit(held, 1, &id);
                } else {
                    E->abort(held);
                }
                held = NULL;
            }
            continue;
        }
        if (line[0] == '>') {
            if (sqlline == lineno - 1 && sqlline) {
                size_t have = strlen(sql);
                snprintf(sql + have, sizeof sql - have, "\n%s", line + 2);
            } else {
                snprintf(sql, sizeof sql, "%s", line + 2);
                for (int k = 0; k < np; k++) {
                    free(pnames[k]);
                    gsql_buf_free(&pvals[k]);
                }
                np = 0;
                if (have_answer) {
                    answer_free(&last);
                    have_answer = 0;
                }
            }
            sqlline = lineno;
            continue;
        }
        if (line[0] == '@') {
            /* @ name = expression */
            char name[64];
            int used = 0;
            if (sscanf(line + 2, "%63s = %n", name, &used) < 1 || !used) {
                fprintf(stderr, "line %d: a malformed parameter\n", lineno);
                return 2;
            }
            const char *expr = line + 2 + used;
            memset(&pvals[np], 0, sizeof pvals[np]);
            /* {a: expr, b: expr} is a record, each field an expression; SQL
             * has no way to write one, and a record column needs it */
            char fields[8][256];
            char fnames[8][64];
            int nf = 0;
            if (expr[0] == '{') {
                char body[4096];
                snprintf(body, sizeof body, "%s", expr + 1);
                body[strcspn(body, "}")] = 0;
                for (char *tok = strtok(body, ","); tok && nf < 8; tok = strtok(NULL, ",")) {
                    while (*tok == ' ') {
                        tok++;
                    }
                    char *colon = strchr(tok, ':');
                    if (!colon) {
                        fprintf(stderr, "line %d: a record field wants name: expr\n", lineno);
                        return 2;
                    }
                    *colon = 0;
                    snprintf(fnames[nf], sizeof fnames[nf], "%s", tok);
                    snprintf(fields[nf], sizeof fields[nf], "%s", colon + 1);
                    nf++;
                }
                v3_w_record(&pvals[np], (size_t)nf);
            } else {
                snprintf(fields[0], sizeof fields[0], "%s", expr);
                nf = 1;
            }
            for (int k = 0; k < nf; k++) {
                char q[8300];
                snprintf(q, sizeof q, "SELECT %s", fields[k]);
                Answer pa;
                GsTxn *save = held;
                held = NULL;
                run(q, NULL, 10, &pa);
                held = save;
                if (pa.code != GSQL_OK || !pa.first_value.length) {
                    fprintf(stderr, "line %d: the parameter's expression failed: %s\n", lineno, pa.err.message);
                    return 2;
                }
                if (expr[0] == '{') {
                    v3_w_name(&pvals[np], fnames[k]);
                }
                v3_put(&pvals[np], pa.first_value.bytes, pa.first_value.length);
                answer_free(&pa);
            }
            pnames[np] = malloc(strlen(name) + 1);
            strcpy(pnames[np], name);
            np++;
            continue;
        }
        if (line[0] != '=' && line[0] != '!' && line[0] != '%') {
            continue;
        }
        if (!have_answer) {
            GsqlBuf params = {0};
            if (np) {
                /* names that are digits are positions: an array */
                if (pnames[0][0] >= '0' && pnames[0][0] <= '9') {
                    v3_w_array(&params, (size_t)np);
                    for (int k = 0; k < np; k++) {
                        v3_put(&params, pvals[k].bytes, pvals[k].length);
                    }
                } else {
                    v3_w_record(&params, (size_t)np);
                    for (int k = 0; k < np; k++) {
                        v3_w_name(&params, pnames[k]);
                        v3_put(&params, pvals[k].bytes, pvals[k].length);
                    }
                }
            }
            run(sql, &params, 2, &last);
            gsql_buf_free(&params);
            have_answer = 1;
        }
        checks++;
        char *got;
        if (line[0] == '%') {
            got = as_text(&last.columns);
            if (last.code != GSQL_OK || strcmp(got, line + 2) != 0) {
                mismatch(sqlline, sql, last.code != GSQL_OK ? last.err.message : got, line + 2);
            }
        } else if (line[0] == '=') {
            got = as_text(&last.text);
            if (last.code != GSQL_OK) {
                char m[700];
                snprintf(m, sizeof m, "refused %s at %u: %s", gsql_error_name(last.code), last.err.position,
                         last.err.message);
                mismatch(sqlline, sql, m, line + 2);
            } else if (strcmp(got, line + 2) != 0) {
                mismatch(sqlline, sql, got, line + 2);
            }
        } else {
            char code[32];
            unsigned pos;
            int used = 0;
            if (sscanf(line + 2, "%31s %u %n", code, &pos, &used) < 2) {
                fprintf(stderr, "line %d: a malformed refusal\n", lineno);
                return 2;
            }
            const char *part = line + 2 + used;
            int want_part_way = 0;
            char partbuf[512];
            snprintf(partbuf, sizeof partbuf, "%s", part);
            char *pw = strstr(partbuf, " [part way]");
            if (pw) {
                *pw = 0;
                want_part_way = 1;
            }
            got = as_text(&last.text);
            char m[700];
            snprintf(m, sizeof m, "%s at %u: %s%s", gsql_error_name(last.code), last.err.position, last.err.message,
                     last.part_way ? " [part way]" : "");
            char want[600];
            snprintf(want, sizeof want, "%s at %u: ...%s...%s", code, pos, partbuf, want_part_way ? " [part way]" : "");
            if (last.code == GSQL_OK) {
                mismatch(sqlline, sql, got, want);
            } else if (strcmp(gsql_error_name(last.code), code) != 0 || last.err.position != pos ||
                       !strstr(last.err.message, partbuf) || last.part_way != want_part_way) {
                mismatch(sqlline, sql, m, want);
            }
        }
        free(got);
    }
    fclose(f);
    if (have_answer) {
        answer_free(&last);
    }
    for (int k = 0; k < np; k++) {
        free(pnames[k]);
        gsql_buf_free(&pvals[k]);
    }
    return 0;
}

static int open_db(const char *dir) {
    GsOptions opt;
    memset(&opt, 0, sizeof opt);
    opt.create = 1;
    int rc = E->open(dir, &opt, &db);
    if (rc) {
        fprintf(stderr, "open %s: %s\n", dir, gs_status_name(rc));
        return 0;
    }
    sql = gsql_sql_new(gb_gsql_host());
    svc = gsql_sql_service(sql);
    return 1;
}


/* ---- the differential tier ------------------------------------------- */

static uint64_t rng_state;

static uint64_t rnd(void) {
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static unsigned pick(unsigned n) {
    return (unsigned)(rnd() % n);
}

/* a constant of column `col`'s kind, or now and then of another kind */
static int odd_kinds = 1;

static void constant(char *out, size_t cap, int col) {
    static const char *const ints[] = { "0", "1", "2", "3", "5", "8", "-1", "3.0", "2.5", "NULL" };
    static const char *const texts[] = { "''", "'a'", "'ab'", "'b'", "'ba'", "'z'", "'é'", "NULL" };
    static const char *const nums[] = { "0", "-0.0", "1", "1.5", "2", "1e3", "-7", "CAST('NaN' AS NUMBER)",
                                        "CAST('inf' AS NUMBER)", "NULL" };
    static const char *const dates[] = { "DATE '2026-01-01'", "DATE '2026-01-02'", "DATE '2025-12-31'",
                                         "DATE '2026-06-30'", "NULL" };
    static const char *const usd[] = { "CAST('0' AS MONEY(USD))", "CAST('1.50' AS MONEY(USD))",
                                       "CAST('2' AS MONEY(USD))", "CAST('-1' AS MONEY(USD))", "NULL" };
    static const char *const anym[] = { "CAST('1' AS MONEY(USD))", "CAST('1' AS MONEY(EUR))",
                                        "CAST('2' AS MONEY(JPY))", "NULL" };
    if (odd_kinds && pick(20) == 0) {
        /* the wrong kind: both paths must refuse it alike */
        static const char *const odd[] = { "'3'", "3", "TRUE", "DATE '2026-01-01'" };
        snprintf(out, cap, "%s", odd[pick(4)]);
        return;
    }
    switch (col) {
    case 0: case 1:
        snprintf(out, cap, "%s", ints[pick(10)]);
        return;
    case 2:
        snprintf(out, cap, "%s", texts[pick(8)]);
        return;
    case 3:
        snprintf(out, cap, "%s", nums[pick(10)]);
        return;
    case 4:
        snprintf(out, cap, "%s", dates[pick(5)]);
        return;
    case 5:
        snprintf(out, cap, "%s", usd[pick(5)]);
        return;
    case 6:
        snprintf(out, cap, "%s", anym[pick(4)]);
        return;
    default:
        snprintf(out, cap, "%s", pick(3) == 0 ? "NULL" : pick(2) ? "TRUE" : "FALSE");
    }
}

static const char *const dcols[] = { "id", "a", "b", "c", "d", "m", "x", "f" };

static void atom(char *out, size_t cap) {
    int col = (int)pick(8);
    char k1[64], k2[64];
    constant(k1, sizeof k1, col);
    static const char *const ops[] = { "=", "<", "<=", ">", ">=", "<>" };
    switch (pick(10)) {
    case 0:
        constant(k2, sizeof k2, col);
        snprintf(out, cap, "%s BETWEEN %s AND %s", dcols[col], k1, k2);
        return;
    case 1:
        constant(k2, sizeof k2, col);
        snprintf(out, cap, "%s IN (%s, %s)", dcols[col], k1, k2);
        return;
    case 2:
        snprintf(out, cap, "%s IS %sNULL", dcols[col], pick(2) ? "NOT " : "");
        return;
    case 3:
        /* the constant on the left */
        snprintf(out, cap, "%s %s %s", k1, ops[pick(6)], dcols[col]);
        return;
    default:
        snprintf(out, cap, "%s %s %s", dcols[col], ops[pick(5)], k1);
    }
}

static void predicate(char *out, size_t cap) {
    char a[256], b[256], c[256];
    atom(a, sizeof a);
    atom(b, sizeof b);
    atom(c, sizeof c);
    switch (pick(6)) {
    case 0:
        snprintf(out, cap, "%s", a);
        return;
    case 1:
        snprintf(out, cap, "%s OR %s", a, b);
        return;
    case 2:
        snprintf(out, cap, "%s AND (%s OR %s)", a, b, c);
        return;
    case 3:
        snprintf(out, cap, "NOT (%s) AND %s", a, b);
        return;
    default:
        snprintf(out, cap, "%s AND %s AND %s", a, b, c);
    }
}

/* A narrowed path reads fewer rows than the scan, so a condition that raises
 * on some row (a cross-currency comparison) may raise in the scan and never be
 * evaluated by the path: the scan refuses where the path answers. That is
 * allowed, and counted (gsql_sql_design.md §5). The other way round is a
 * defect: the path's rows are a subset of the scan's. */
static int narrowed_past;
static int forced_but_sought;

static int diverged(int path_code, int scan_code) {
    if (path_code == scan_code) {
        return 0;
    }
    if (path_code == GSQL_OK && scan_code == GSQL_EXPRESSION) {
        narrowed_past++;
        return 0;
    }
    return 1;
}

static int both_ways(const char *q, int *seek_runs) {
    Answer x, y;
    unsigned long s0, c0, s1, c1;
    gsql_sql_counts(sql, &s0, &c0);
    gsql_sql_force_scan(sql, 0);
    run(q, NULL, 3, &x);
    gsql_sql_counts(sql, &s1, &c1);
    if (s1 > s0) {
        (*seek_runs)++;
    }
    gsql_sql_force_scan(sql, 1);
    run(q, NULL, 7, &y);
    gsql_sql_force_scan(sql, 0);
    unsigned long s2, c2;
    gsql_sql_counts(sql, &s2, &c2);
    if (s2 != s1 || (y.code == GSQL_OK && c2 == c1)) {
        /* without this, a forced scan that quietly used the paths anyway
         * would agree with them on every query */
        forced_but_sought++;
    }
    checks++;
    char *tx = as_text(&x.text), *ty = as_text(&y.text);
    int same = !diverged(x.code, y.code);
    if (same && x.code == GSQL_OK && y.code == GSQL_OK) {
        same = strcmp(tx, ty) == 0;
    }
    if (!same) {
        mismatches++;
        printf("MISMATCH %s\n  with paths: %s %s\n  scanning:   %s %s\n", q,
               x.code ? gsql_error_name(x.code) : "", x.code ? x.err.message : tx,
               y.code ? gsql_error_name(y.code) : "", y.code ? y.err.message : ty);
    }
    int refused = x.code != GSQL_OK;
    free(tx);
    free(ty);
    answer_free(&x);
    answer_free(&y);
    return refused;
}

/* ---- the grouping oracle (docs/gsql_aggregate_design.md §6) ------------
 *
 * The same rows, fetched UNGROUPED and sorted by the group column, folded here:
 * consecutive rows whose group values the host's order calls equal (or that
 * are both NULL) are one group, a sum is the host's `+`, an extreme the host's
 * order. It shares none of the engine's grouping -- no key encoding, no hash
 * table, no accumulator -- so a grouping that merged two groups, split one, or
 * folded a value wrongly disagrees with it. */

static const GsqlHost *H;

static GsqlHv *hload(const unsigned char *v, size_t n) {
    GsqlError err = { GSQL_OK, "", 0 };
    return H->load(H->ctx, v, n, &err);
}

static int hnull(const GsqlHv *v) {
    int k = H->kind(H->ctx, v);
    return k == SER_NULL || k == SER_UNKNOWN;
}

static void hrender(GsqlBuf *o, const GsqlHv *v) {
    GsqlBuf b = {0};
    GsqlError err = { GSQL_OK, "", 0 };
    H->store(H->ctx, v, &b, &err);
    render(o, b.bytes, b.length);
    gsql_buf_free(&b);
}

static int hcmp(const GsqlHv *a, const GsqlHv *b) {
    int c = 0;
    GsqlError err = { GSQL_OK, "", 0 };
    H->order(H->ctx, a, b, &c, &err);
    return c;
}

typedef struct {
    GsqlHv *g;
    long count, nv;
    GsqlHv *sum, *lo, *hi;
} OGroup;

static void og_flush(OGroup *og, GsqlBuf *o, long having, size_t *nrows) {
    if (!og->g) {
        return;
    }
    if (og->count > having) {
        if ((*nrows)++) {
            v3_put(o, " | ", 3);
        }
        char b[64];
        hrender(o, og->g);
        snprintf(b, sizeof b, ", %ld, %ld, ", og->count, og->nv);
        v3_put(o, b, strlen(b));
        GsqlBuf nul = {0};
        v3_w_unknown(&nul);
        GsqlHv *none = hload(nul.bytes, nul.length);
        gsql_buf_free(&nul);
        hrender(o, og->sum ? og->sum : none);
        v3_put(o, ", ", 2);
        hrender(o, og->lo ? og->lo : none);
        v3_put(o, ", ", 2);
        hrender(o, og->hi ? og->hi : none);
        v3_put(o, ", ", 2);
        if (og->sum) {
            GsqlBuf nb = {0};
            v3_w_int(&nb, (uint64_t)og->nv);
            GsqlHv *n = hload(nb.bytes, nb.length);
            gsql_buf_free(&nb);
            GsqlError err = { GSQL_OK, "", 0 };
            GsqlHv *avg = H->op(H->ctx, GSQL_HOP_DIV, og->sum, n, &err);
            hrender(o, avg ? avg : none);
            if (avg) {
                H->release(H->ctx, avg);
            }
            H->release(H->ctx, n);
        } else {
            hrender(o, none);
        }
        H->release(H->ctx, none);
    }
    GsqlHv *vs[4] = { og->g, og->sum, og->lo, og->hi };
    for (int k = 0; k < 4; k++) {
        if (vs[k]) {
            H->release(H->ctx, vs[k]);
        }
    }
    memset(og, 0, sizeof *og);
}

/* rows of [g, v], sorted by g: the grouped answer, rendered as the engine's is */
static void oracle_fold(const GsqlBuf *raw, long having, GsqlBuf *o) {
    GsBytes *items;
    long n = rows_of(raw, &items);
    OGroup og;
    memset(&og, 0, sizeof og);
    size_t nrows = 0;
    for (long r = 0; r < n; r++) {
        GsBytes cols[2];
        v3_array_items(items[r].p, items[r].n, cols, 2);
        GsqlHv *g = hload(cols[0].p, cols[0].n);
        GsqlHv *v = hload(cols[1].p, cols[1].n);
        int same = og.g && (hnull(og.g) ? hnull(g) : !hnull(g) && hcmp(og.g, g) == 0);
        if (!same) {
            og_flush(&og, o, having, &nrows);
            og.g = g;
        } else {
            H->release(H->ctx, g);
        }
        og.count++;
        if (hnull(v)) {
            H->release(H->ctx, v);
            continue;
        }
        og.nv++;
        if (!og.sum) {
            og.sum = H->copy(H->ctx, v);
            og.lo = H->copy(H->ctx, v);
            og.hi = v;
            continue;
        }
        GsqlError err = { GSQL_OK, "", 0 };
        GsqlHv *s2 = H->op(H->ctx, GSQL_HOP_ADD, og.sum, v, &err);
        H->release(H->ctx, og.sum);
        og.sum = s2;
        if (hcmp(v, og.lo) < 0) {
            H->release(H->ctx, og.lo);
            og.lo = H->copy(H->ctx, v);
        }
        if (hcmp(v, og.hi) > 0) {
            H->release(H->ctx, og.hi);
            og.hi = H->copy(H->ctx, v);
        }
        H->release(H->ctx, v);
    }
    og_flush(&og, o, having, &nrows);
    free(items);
    if (nrows == 0) {
        v3_put(o, "(none)", 6);
    }
}

/* 0 and -0.0 are one value, and which of them a group's value, min or max
 * shows is whichever was seen first -- an order SQL leaves to the access path */
static void zero_spelling(GsqlBuf *b) {
    v3_put(b, "", 1);
    char *t = (char *)b->bytes;
    for (char *z; (z = strstr(t, "-0.0")) != NULL;) {
        memmove(z, z + 3, strlen(z + 3) + 1);
        z[0] = '0';
    }
    b->length = strlen(t);
}

static int grouping_oracle(int n, int *grouped_seen) {
    static const char *const gcols[] = { "a", "b", "c", "d", "m", "id" };
    static const char *const vcols[] = { "a", "c", "m" };
    H = gb_gsql_host();
    int refused = 0;
    for (int k = 0; k < n; k++) {
        char where[1024], q[2048], o[2048];
        predicate(where, sizeof where);
        const char *g = gcols[pick(6)], *v = vcols[pick(3)];
        long having = pick(3) ? -1 : (long)pick(3);
        const char *dir = pick(2) ? " DESC" : "";
        char hv[64] = "";
        if (having >= 0) {
            snprintf(hv, sizeof hv, " HAVING count(*) > %ld", having);
        }
        snprintf(q, sizeof q, "SELECT %s, count(*), count(%s), sum(%s), min(%s), max(%s), avg(%s) FROM r WHERE %s "
                 "GROUP BY %s%s ORDER BY %s%s", g, v, v, v, v, v, where, g, hv, g, dir);
        snprintf(o, sizeof o, "SELECT %s, %s FROM r WHERE %s ORDER BY %s%s, id", g, v, where, g, dir);
        /* both in ONE mode: a narrowed path may skip a row the scan refuses
           on (gsql_sql_design.md §5), and that is not a grouping question */
        Answer x, y;
        gsql_sql_force_scan(sql, pick(2));
        run(q, NULL, 100000, &x);
        run(o, NULL, 100000, &y);
        gsql_sql_force_scan(sql, 0);
        checks++;
        if (x.code != y.code) {
            mismatches++;
            printf("MISMATCH %s\n  grouped: %s %s\n  oracle's rows: %s %s\n", q, gsql_error_name(x.code),
                   x.err.message, gsql_error_name(y.code), y.err.message);
        } else if (x.code != GSQL_OK) {
            refused++;
        } else {
            GsqlBuf want = {0};
            oracle_fold(&y.raw, having, &want);
            GsqlBuf got = {0};
            v3_put(&got, x.text.bytes, x.text.length);
            zero_spelling(&got);
            zero_spelling(&want);
            if (strcmp((char *)got.bytes, (char *)want.bytes) != 0) {
                mismatches++;
                printf("MISMATCH %s\n  engine: %s\n  oracle: %s\n", q, (char *)got.bytes, (char *)want.bytes);
            } else if (strcmp((char *)got.bytes, "(none)") != 0) {
                (*grouped_seen)++;
            }
            gsql_buf_free(&want);
            gsql_buf_free(&got);
        }
        answer_free(&x);
        answer_free(&y);
    }
    return refused;
}

/* ---- the DISTINCT oracle (docs/gsql_distinct_design.md §6) -------------
 *
 * The same query without DISTINCT, its rows sorted by the same terms, with a
 * row dropped here when the host calls every column the same as the row kept
 * before it (both NULL, or ordered equal). Sorted, the copies of a row are
 * adjacent, so that is the whole of deduplication; it shares no key encoding
 * or hash table with the engine. LIMIT and OFFSET are then counted over what
 * is left. */

static int same_row(GsqlHv **a, GsqlHv **b, int ncol) {
    for (int k = 0; k < ncol; k++) {
        int na = hnull(a[k]), nb = hnull(b[k]);
        if (na != nb || (!na && hcmp(a[k], b[k]) != 0)) {
            return 0;
        }
    }
    return 1;
}

static void oracle_distinct(const GsqlBuf *raw, int ncol, long offset, long limit, GsqlBuf *o) {
    GsBytes *items;
    long n = rows_of(raw, &items);
    GsqlHv *kept[2] = { NULL, NULL };
    size_t nrows = 0;
    long seen = 0;
    for (long r = 0; r < n; r++) {
        GsBytes cols[2];
        v3_array_items(items[r].p, items[r].n, cols, ncol);
        GsqlHv *v[2] = { NULL, NULL };
        for (int k = 0; k < ncol; k++) {
            v[k] = hload(cols[k].p, cols[k].n);
        }
        if (kept[0] && same_row(kept, v, ncol)) {
            for (int k = 0; k < ncol; k++) {
                H->release(H->ctx, v[k]);
            }
            continue;
        }
        for (int k = 0; k < ncol; k++) {
            if (kept[k]) {
                H->release(H->ctx, kept[k]);
            }
            kept[k] = v[k];
        }
        if (seen++ < offset || (limit >= 0 && (long)nrows >= limit)) {
            continue;
        }
        if (nrows++) {
            v3_put(o, " | ", 3);
        }
        for (int k = 0; k < ncol; k++) {
            if (k) {
                v3_put(o, ", ", 2);
            }
            hrender(o, v[k]);
        }
    }
    for (int k = 0; k < ncol; k++) {
        if (kept[k]) {
            H->release(H->ctx, kept[k]);
        }
    }
    free(items);
    if (nrows == 0) {
        v3_put(o, "(none)", 6);
    }
}

static int stopped_short;

static int distinct_oracle(int n, int *distinct_seen, int *deduplicated) {
    /* not x: two currencies have no order, and the host would call them equal */
    static const char *const cols[] = { "a", "b", "c", "d", "m", "f" };
    H = gb_gsql_host();
    int refused = 0;
    for (int k = 0; k < n; k++) {
        char where[1024], sel[64], ord[128], lim[64] = "", q[2048], o[2048];
        predicate(where, sizeof where);
        int ncol = 1 + (int)pick(2);
        const char *c1 = cols[pick(6)], *c2 = cols[pick(6)];
        while (ncol == 2 && c2 == c1) {
            c2 = cols[pick(6)];
        }
        const char *dir = pick(2) ? " DESC" : "";
        if (ncol == 1) {
            snprintf(sel, sizeof sel, "%s", c1);
            snprintf(ord, sizeof ord, "%s%s", c1, dir);
        } else {
            snprintf(sel, sizeof sel, "%s, %s", c1, c2);
            snprintf(ord, sizeof ord, "%s%s, %s", c1, dir, c2);
        }
        long limit = -1, offset = 0;
        if (pick(3) == 0) {
            limit = 1 + (long)pick(4);
            offset = (long)pick(3);
            snprintf(lim, sizeof lim, " LIMIT %ld OFFSET %ld", limit, offset);
        }
        snprintf(q, sizeof q, "SELECT DISTINCT %s FROM r WHERE %s ORDER BY %s%s", sel, where, ord, lim);
        snprintf(o, sizeof o, "SELECT %s FROM r WHERE %s ORDER BY %s", sel, where, ord);
        /* both in one mode, for the reason the grouping oracle gives */
        Answer x, y;
        gsql_sql_force_scan(sql, pick(2));
        run(q, NULL, 100000, &x);
        run(o, NULL, 100000, &y);
        gsql_sql_force_scan(sql, 0);
        checks++;
        if (limit >= 0 && x.code == GSQL_OK && y.code == GSQL_EXPRESSION) {
            /* a LIMIT may stop before the row that refuses, as a narrowed
               path may (gsql_sql_design.md §5); the other way is a defect */
            stopped_short++;
        } else if (x.code != y.code) {
            mismatches++;
            printf("MISMATCH %s\n  distinct: %s %s\n  oracle's rows: %s %s\n", q, gsql_error_name(x.code),
                   x.err.message, gsql_error_name(y.code), y.err.message);
        } else if (x.code != GSQL_OK) {
            refused++;
        } else {
            GsqlBuf want = {0};
            oracle_distinct(&y.raw, ncol, offset, limit, &want);
            GsqlBuf got = {0};
            v3_put(&got, x.text.bytes, x.text.length);
            zero_spelling(&got);
            zero_spelling(&want);
            if (strcmp((char *)got.bytes, (char *)want.bytes) != 0) {
                mismatches++;
                printf("MISMATCH %s\n  engine: %s\n  oracle: %s\n", q, (char *)got.bytes, (char *)want.bytes);
            } else if (strcmp((char *)got.bytes, "(none)") != 0) {
                (*distinct_seen)++;
                /* the census: the oracle really dropped a copy here */
                GsqlBuf all = {0};
                oracle_distinct(&y.raw, ncol, 0, -1, &all);
                v3_put(&all, "", 1);
                size_t bars_all = 0, bars_raw = 0;
                for (const char *t = (char *)all.bytes; (t = strstr(t, " | ")); t += 3) {
                    bars_all++;
                }
                GsBytes *rows;
                bars_raw = (size_t)rows_of(&y.raw, &rows);
                free(rows);
                if (bars_all + 1 < bars_raw) {
                    (*deduplicated)++;
                }
                gsql_buf_free(&all);
            }
            gsql_buf_free(&want);
            gsql_buf_free(&got);
        }
        answer_free(&x);
        answer_free(&y);
    }
    return refused;
}

/* ---- the set-operation oracle (docs/gsql_setop_design.md §5) ----------
 *
 * Each side fetched alone, and the operation computed here by the design's
 * definition: a row matches another when the host calls every column the
 * same, ALL consumes one matching right row per left row, and the result is
 * then sorted here by the host's order. The engine keys rows and counts keys
 * in a hash table; this pairs rows one by one and shares none of that. */

typedef struct {
    GsqlHv *v[2];
    int used;
} ORow;

static long orows_load(const GsqlBuf *raw, int ncol, ORow **out) {
    GsBytes *items;
    long n = rows_of(raw, &items);
    ORow *v = calloc((size_t)(n ? n : 1), sizeof(ORow));
    if (!v) {
        abort();
    }
    for (long r = 0; r < n; r++) {
        GsBytes cols[2];
        v3_array_items(items[r].p, items[r].n, cols, ncol);
        for (int k = 0; k < ncol; k++) {
            v[r].v[k] = hload(cols[k].p, cols[k].n);
        }
    }
    free(items);
    *out = v;
    return n;
}

static void orows_free(ORow *v, long n, int ncol) {
    for (long r = 0; r < n; r++) {
        for (int k = 0; k < ncol; k++) {
            H->release(H->ctx, v[r].v[k]);
        }
    }
    free(v);
}

static int ocmp(const ORow *a, const ORow *b, int ncol, int desc) {
    for (int k = 0; k < ncol; k++) {
        int na = hnull(a->v[k]), nb = hnull(b->v[k]);
        int c = na || nb ? (na == nb ? 0 : na ? 1 : -1) : hcmp(a->v[k], b->v[k]);
        if (c) {
            return k == 0 && desc ? -c : c;
        }
    }
    return 0;
}

static int set_oracle(int n, int *set_seen, int *by_op) {
    static const char *const cols[] = { "a", "b", "c", "d", "m", "f" };
    static const char *const ops[] = { "UNION", "UNION ALL", "INTERSECT", "INTERSECT ALL", "EXCEPT", "EXCEPT ALL" };
    H = gb_gsql_host();
    int refused = 0;
    for (int k = 0; k < n; k++) {
        char w1[1024], w2[1024], sel[64], q[4096], lq[2048], rq[2048], lim[64] = "";
        predicate(w1, sizeof w1);
        predicate(w2, sizeof w2);
        int ncol = 1 + (int)pick(2);
        const char *c1 = cols[pick(6)], *c2 = cols[pick(6)];
        while (ncol == 2 && c2 == c1) {
            c2 = cols[pick(6)];
        }
        if (ncol == 1) {
            snprintf(sel, sizeof sel, "%s", c1);
        } else {
            snprintf(sel, sizeof sel, "%s, %s", c1, c2);
        }
        int op = (int)pick(6), desc = (int)pick(2);
        long limit = -1, offset = 0;
        if (pick(3) == 0) {
            limit = 1 + (long)pick(5);
            offset = (long)pick(3);
            snprintf(lim, sizeof lim, " LIMIT %ld OFFSET %ld", limit, offset);
        }
        snprintf(q, sizeof q, "SELECT %s FROM r WHERE %s %s SELECT %s FROM r WHERE %s ORDER BY 1%s%s%s", sel, w1,
                 ops[op], sel, w2, desc ? " DESC" : "", ncol == 2 ? ", 2" : "", lim);
        snprintf(lq, sizeof lq, "SELECT %s FROM r WHERE %s", sel, w1);
        snprintf(rq, sizeof rq, "SELECT %s FROM r WHERE %s", sel, w2);
        Answer x, l, r;
        gsql_sql_force_scan(sql, pick(2));
        run(q, NULL, 100000, &x);
        run(lq, NULL, 100000, &l);
        run(rq, NULL, 100000, &r);
        gsql_sql_force_scan(sql, 0);
        checks++;
        int side = l.code != GSQL_OK ? l.code : r.code;
        if (x.code != side) {
            mismatches++;
            printf("MISMATCH %s\n  compound: %s %s\n  sides: %s %s\n", q, gsql_error_name(x.code), x.err.message,
                   gsql_error_name(side), l.code ? l.err.message : r.err.message);
        } else if (x.code != GSQL_OK) {
            refused++;
        } else {
            ORow *L, *R;
            long nl = orows_load(&l.raw, ncol, &L), nr = orows_load(&r.raw, ncol, &R);
            ORow **res = malloc(sizeof(ORow *) * (size_t)(nl + nr + 1));
            if (!res) {
                abort();
            }
            long nres = 0;
            int base = op / 2, all = op % 2;   /* 0 union, 1 intersect, 2 except */
            for (int sd = 0; sd < (base == 0 ? 2 : 1); sd++) {
                ORow *in = sd ? R : L;
                long nin = sd ? nr : nl;
                for (long i = 0; i < nin; i++) {
                    int keep = 1;
                    if (base != 0) {
                        long hit = -1;
                        for (long j = 0; j < nr && hit < 0; j++) {
                            if ((!all || !R[j].used) && same_row(in[i].v, R[j].v, ncol)) {
                                hit = j;
                            }
                        }
                        if (all && hit >= 0) {
                            R[hit].used = 1;
                        }
                        keep = base == 1 ? hit >= 0 : hit < 0;
                    }
                    for (long j = 0; keep && !all && j < nres; j++) {
                        if (same_row(res[j]->v, in[i].v, ncol)) {
                            keep = 0;
                        }
                    }
                    if (keep) {
                        res[nres++] = &in[i];
                    }
                }
            }
            /* a stable insertion sort by the host's order */
            for (long i = 1; i < nres; i++) {
                ORow *t = res[i];
                long j = i;
                while (j > 0 && ocmp(res[j - 1], t, ncol, desc) > 0) {
                    res[j] = res[j - 1];
                    j--;
                }
                res[j] = t;
            }
            GsqlBuf want = {0};
            size_t nrows = 0;
            for (long i = offset; i < nres && (limit < 0 || (long)nrows < limit); i++) {
                if (nrows++) {
                    v3_put(&want, " | ", 3);
                }
                for (int c = 0; c < ncol; c++) {
                    if (c) {
                        v3_put(&want, ", ", 2);
                    }
                    hrender(&want, res[i]->v[c]);
                }
            }
            if (nrows == 0) {
                v3_put(&want, "(none)", 6);
            }
            GsqlBuf got = {0};
            v3_put(&got, x.text.bytes, x.text.length);
            zero_spelling(&got);
            zero_spelling(&want);
            if (strcmp((char *)got.bytes, (char *)want.bytes) != 0) {
                mismatches++;
                printf("MISMATCH %s\n  engine: %s\n  oracle: %s\n", q, (char *)got.bytes, (char *)want.bytes);
            } else if (nrows) {
                (*set_seen)++;
                by_op[base]++;
            }
            gsql_buf_free(&want);
            gsql_buf_free(&got);
            free(res);
            orows_free(L, nl, ncol);
            orows_free(R, nr, ncol);
        }
        answer_free(&x);
        answer_free(&l);
        answer_free(&r);
    }
    return refused;
}

/* ---- the join tiers (docs/gsql_join_design.md §5) -----------------------
 *
 * A second table q, whose column names differ from r's so a bare name in
 * the generated predicates belongs to r. Each join runs with access paths
 * (so q is sought for each row of r) and forced to scans, which must agree;
 * and the same join is computed here from r and q fetched alone, matched
 * pairwise with the host's `=` and null-extended by hand. */

static int join_tables(void) {
    static const char *const schema[] = {
        "CREATE TABLE q (qid INTEGER PRIMARY KEY, k INTEGER, t TEXT, v NUMBER)",
        "CREATE INDEX q_k ON q (k)",
        "CREATE INDEX q_t ON q (t)",
        NULL
    };
    for (int k = 0; schema[k]; k++) {
        Answer a;
        run(schema[k], NULL, 10, &a);
        if (a.code != GSQL_OK) {
            fprintf(stderr, "%s: %s\n", schema[k], a.err.message);
            return 0;
        }
        answer_free(&a);
    }
    static const char *const ks[] = { "0", "1", "2", "3", "5", "8", "-1", "NULL" };
    static const char *const ts[] = { "''", "'a'", "'ab'", "'b'", "'z'", "NULL" };
    static const char *const vs[] = { "0", "1.5", "2", "-7", "NULL" };
    int inserted = 0;
    for (int id = 1; id <= 120; id++) {
        char q[512];
        snprintf(q, sizeof q, "INSERT INTO q VALUES (%d, %s, %s, %s)", id, ks[pick(8)], ts[pick(6)], vs[pick(5)]);
        Answer a;
        run(q, NULL, 10, &a);
        inserted += a.code == GSQL_OK;
        answer_free(&a);
    }
    return inserted;
}

/* rows [id, key] of r and [qid, key, v] of q, both sorted by id: the join */
static void oracle_join(const GsqlBuf *rr, const GsqlBuf *qq, int left, GsqlBuf *o) {
    GsBytes *ri, *qi;
    long nr = rows_of(rr, &ri);
    long nq = rows_of(qq, &qi);
    size_t nrows = 0;
    for (long a = 0; a < nr; a++) {
        GsBytes rc[2];
        v3_array_items(ri[a].p, ri[a].n, rc, 2);
        GsqlHv *rk = hload(rc[1].p, rc[1].n);
        int matched = 0;
        for (long b = 0; b < nq; b++) {
            GsBytes qc[3];
            v3_array_items(qi[b].p, qi[b].n, qc, 3);
            GsqlHv *qk = hload(qc[1].p, qc[1].n);
            GsqlError err = { GSQL_OK, "", 0 };
            GsqlHv *eq = H->op(H->ctx, GSQL_HOP_EQ, qk, rk, &err);
            int t = eq ? H->truth(H->ctx, eq) : 0;
            if (eq) {
                H->release(H->ctx, eq);
            }
            H->release(H->ctx, qk);
            if (t != 1) {
                continue;
            }
            matched = 1;
            if (nrows++) {
                v3_put(o, " | ", 3);
            }
            render(o, rc[0].p, rc[0].n);
            v3_put(o, ", ", 2);
            render(o, qc[0].p, qc[0].n);
            v3_put(o, ", ", 2);
            render(o, qc[2].p, qc[2].n);
        }
        if (left && !matched) {
            if (nrows++) {
                v3_put(o, " | ", 3);
            }
            render(o, rc[0].p, rc[0].n);
            v3_put(o, ", NULL, NULL", 12);
        }
        H->release(H->ctx, rk);
    }
    free(ri);
    free(qi);
    if (nrows == 0) {
        v3_put(o, "(none)", 6);
    }
}

static int join_tiers(int n, int *seek_runs, int *joined_seen) {
    H = gb_gsql_host();
    int refused = 0;
    for (int k = 0; k < n; k++) {
        char where[1024], q[2048];
        predicate(where, sizeof where);
        int left = pick(2);
        int bytext = pick(2);
        const char *rkey = bytext ? "b" : "a", *qkey = bytext ? "t" : "k";
        /* the differential: a richer ON, and sometimes a WHERE on q, which a
           LEFT join must not use to narrow q */
        const char *extra_on = pick(3) == 0 ? " AND q.v > 0" : "";
        const char *extra_where = pick(3) == 0 ? " AND q.v IS NOT NULL" : "";
        snprintf(q, sizeof q, "SELECT r.id, q.qid, q.v FROM r %sJOIN q ON q.%s = r.%s%s WHERE %s%s ORDER BY r.id, q.qid",
                 left ? "LEFT " : "", qkey, rkey, extra_on, where, extra_where);
        refused += both_ways(q, seek_runs);
        /* the oracle: the plain equality join, with the predicate on r alone */
        char jq[2048], rq[2048], qq[256];
        snprintf(jq, sizeof jq, "SELECT r.id, q.qid, q.v FROM r %sJOIN q ON q.%s = r.%s WHERE %s ORDER BY r.id, q.qid",
                 left ? "LEFT " : "", qkey, rkey, where);
        snprintf(rq, sizeof rq, "SELECT id, %s FROM r WHERE %s ORDER BY id", rkey, where);
        snprintf(qq, sizeof qq, "SELECT qid, %s, v FROM q ORDER BY qid", qkey);
        Answer x, ra, qa;
        gsql_sql_force_scan(sql, pick(2));
        run(jq, NULL, 100000, &x);
        run(rq, NULL, 100000, &ra);
        gsql_sql_force_scan(sql, 0);
        run(qq, NULL, 100000, &qa);
        checks++;
        if (x.code != ra.code) {
            mismatches++;
            printf("MISMATCH %s\n  joined: %s %s\n  r alone: %s %s\n", jq, gsql_error_name(x.code), x.err.message,
                   gsql_error_name(ra.code), ra.err.message);
        } else if (x.code == GSQL_OK) {
            GsqlBuf want = {0};
            oracle_join(&ra.raw, &qa.raw, left, &want);
            v3_put(&want, "", 1);
            GsqlBuf got = {0};
            v3_put(&got, x.text.bytes, x.text.length);
            v3_put(&got, "", 1);
            if (strcmp((char *)got.bytes, (char *)want.bytes) != 0) {
                mismatches++;
                printf("MISMATCH %s\n  engine: %s\n  oracle: %s\n", jq, (char *)got.bytes, (char *)want.bytes);
            } else if (strcmp((char *)got.bytes, "(none)") != 0) {
                (*joined_seen)++;
            }
            gsql_buf_free(&want);
            gsql_buf_free(&got);
        }
        answer_free(&x);
        answer_free(&ra);
        answer_free(&qa);
    }
    return refused;
}

/* ---- the subquery tier (docs/gsql_subquery_design.md §6) ---------------
 *
 * IN over q's keys, uncorrelated; EXISTS and a count, correlated on q's
 * indexed key. Each runs with access paths and forced to scans, inside the
 * subquery as well; and each is computed here from r and q fetched alone,
 * matching values through the host's `=`. */

static const char *const qpreds[] = { "TRUE", "q.v > 0", "q.v IS NULL", "q.t = 'a'", "q.k < 3", "q.v = 1.5",
                                      "q.t IS NOT NULL" };

/* the truth of `=` between two stored values, through the host */
static int host_eq(const unsigned char *a, size_t an, const unsigned char *b, size_t bn) {
    GsqlHv *x = hload(a, an), *y = hload(b, bn);
    GsqlError err = { GSQL_OK, "", 0 };
    GsqlHv *eq = H->op(H->ctx, GSQL_HOP_EQ, x, y, &err);
    int t = eq ? H->truth(H->ctx, eq) : -2;
    if (eq) {
        H->release(H->ctx, eq);
    }
    H->release(H->ctx, x);
    H->release(H->ctx, y);
    return t;
}

/* rows [id, key] of r and [qid, key] of q, the latter already filtered */
static void oracle_subquery(int form, int neg, const GsqlBuf *rr, const GsqlBuf *qq, GsqlBuf *o) {
    GsBytes *ri, *qi;
    long nr = rows_of(rr, &ri);
    long nq = rows_of(qq, &qi);
    size_t nrows = 0;
    for (long a = 0; a < nr; a++) {
        GsBytes rc[2];
        v3_array_items(ri[a].p, ri[a].n, rc, 2);
        int acc = 0;      /* IN: false until equal or unknown */
        long count = 0;   /* EXISTS and the count: the q rows whose key is equal */
        for (long b = 0; b < nq; b++) {
            GsBytes qc[2];
            v3_array_items(qi[b].p, qi[b].n, qc, 2);
            int t = host_eq(rc[1].p, rc[1].n, qc[1].p, qc[1].n);
            if (t == 1) {
                acc = 1;
                count++;
            } else if (t == -1 && acc == 0) {
                acc = -1;
            }
        }
        int keep;
        if (form == 0) {
            if (neg && acc >= 0) {
                acc = !acc;
            }
            keep = acc == 1;
        } else if (form == 1) {
            keep = neg ? count == 0 : count > 0;
        } else {
            keep = 1;
        }
        if (!keep) {
            continue;
        }
        if (nrows++) {
            v3_put(o, " | ", 3);
        }
        render(o, rc[0].p, rc[0].n);
        if (form == 2) {
            char b[32];
            snprintf(b, sizeof b, ", %ld", count);
            v3_put(o, b, strlen(b));
        }
    }
    free(ri);
    free(qi);
    if (nrows == 0) {
        v3_put(o, "(none)", 6);
    }
}

static int subquery_tier(int n, int *seek_runs, int *by_form) {
    int refused = 0;
    for (int k = 0; k < n; k++) {
        char where[1024], q[2048];
        predicate(where, sizeof where);
        int form = (int)pick(3), neg = (int)pick(3) == 0;
        int bytext = pick(2);
        const char *rkey = bytext ? "b" : "a", *qkey = bytext ? "t" : "k";
        const char *qp = qpreds[pick(sizeof qpreds / sizeof qpreds[0])];
        if (form == 0) {
            snprintf(q, sizeof q, "SELECT id FROM r WHERE (%s) AND %s %sIN (SELECT %s FROM q WHERE %s) ORDER BY id",
                     where, rkey, neg ? "NOT " : "", qkey, qp);
        } else if (form == 1) {
            snprintf(q, sizeof q, "SELECT id FROM r WHERE (%s) AND %sEXISTS (SELECT 1 FROM q WHERE q.%s = r.%s AND %s) "
                     "ORDER BY id", where, neg ? "NOT " : "", qkey, rkey, qp);
        } else {
            snprintf(q, sizeof q, "SELECT id, (SELECT count(*) FROM q WHERE q.%s = r.%s AND %s) FROM r WHERE %s "
                     "ORDER BY id", qkey, rkey, qp, where);
        }
        refused += both_ways(q, seek_runs);
        char rq[2048], qq[512];
        snprintf(rq, sizeof rq, "SELECT id, %s FROM r WHERE %s ORDER BY id", rkey, where);
        snprintf(qq, sizeof qq, "SELECT qid, %s FROM q WHERE %s ORDER BY qid", qkey, qp);
        Answer x, ra, qa;
        gsql_sql_force_scan(sql, pick(2));
        run(q, NULL, 100000, &x);
        run(rq, NULL, 100000, &ra);
        gsql_sql_force_scan(sql, 0);
        run(qq, NULL, 100000, &qa);
        checks++;
        if (x.code != ra.code) {
            mismatches++;
            printf("MISMATCH %s\n  with the subquery: %s %s\n  r alone: %s %s\n", q, gsql_error_name(x.code),
                   x.err.message, gsql_error_name(ra.code), ra.err.message);
        } else if (x.code == GSQL_OK && qa.code != GSQL_OK) {
            mismatches++;
            printf("MISMATCH %s\n  q alone was refused: %s\n", qq, qa.err.message);
        } else if (x.code == GSQL_OK) {
            GsqlBuf want = {0};
            oracle_subquery(form, neg, &ra.raw, &qa.raw, &want);
            v3_put(&want, "", 1);
            GsqlBuf got = {0};
            v3_put(&got, x.text.bytes, x.text.length);
            v3_put(&got, "", 1);
            if (strcmp((char *)got.bytes, (char *)want.bytes) != 0) {
                mismatches++;
                printf("MISMATCH %s\n  engine: %s\n  oracle: %s\n", q, (char *)got.bytes, (char *)want.bytes);
            } else if (strcmp((char *)got.bytes, "(none)") != 0) {
                by_form[form]++;
            }
            gsql_buf_free(&want);
            gsql_buf_free(&got);
        }
        answer_free(&x);
        answer_free(&ra);
        answer_free(&qa);
    }
    return refused;
}

static int differential(const char *dir, uint64_t seed, int n) {
    if (!open_db(dir)) {
        return 2;
    }
    rng_state = seed;
    static const char *const schema[] = {
        "CREATE TABLE r (id INTEGER PRIMARY KEY, a INTEGER, b TEXT, c NUMBER, d DATE, m MONEY(USD), x MONEY, f BOOLEAN)",
        "CREATE INDEX r_a ON r (a)",
        "CREATE INDEX r_bc ON r (b, c DESC)",
        "CREATE INDEX r_d ON r (d DESC)",
        "CREATE INDEX r_m ON r (m)",
        "CREATE INDEX r_x ON r (x)",
        "CREATE INDEX r_ab ON r (a, b)",
        "CREATE UNIQUE INDEX r_did ON r (d, id)",
        NULL
    };
    for (int k = 0; schema[k]; k++) {
        Answer a;
        run(schema[k], NULL, 10, &a);
        if (a.code != GSQL_OK) {
            fprintf(stderr, "%s: %s\n", schema[k], a.err.message);
            return 2;
        }
        answer_free(&a);
    }
    /* the rows: values repeat, so equality finds several, and an exact and an
     * inexact spelling of one number both appear */
    int inserted = 0;
    odd_kinds = 0;
    for (int id = 1; id <= 300; id++) {
        char q[1024], va[64], vb[64], vc[64], vd[64], vm[64], vx[64], vf[64];
        do {
            constant(va, sizeof va, 0);
        } while (strchr(va, '\'') || strchr(va, '.') || strcmp(va, "TRUE") == 0 || strstr(va, "DATE"));
        constant(vb, sizeof vb, 2);
        constant(vc, sizeof vc, 3);
        constant(vd, sizeof vd, 4);
        constant(vm, sizeof vm, 5);
        constant(vx, sizeof vx, 6);
        constant(vf, sizeof vf, 7);
        snprintf(q, sizeof q, "INSERT INTO r VALUES (%d, %s, %s, %s, %s, %s, %s, %s)", id, va, vb, vc, vd, vm, vx, vf);
        Answer a;
        run(q, NULL, 10, &a);
        inserted += a.code == GSQL_OK;
        answer_free(&a);
    }
    odd_kinds = 1;
    int seek_runs = 0, refused = 0;
    for (int k = 0; k < n; k++) {
        char where[1024], q[2048];
        predicate(where, sizeof where);
        const char *col = dcols[pick(8)];
        if (strcmp(col, "x") == 0 || strcmp(col, "f") == 0) {
            col = "id";   /* a mixed-currency money or a boolean has no order */
        }
        switch (pick(4)) {
        case 0:
            snprintf(q, sizeof q, "SELECT id, a, b, c FROM r WHERE %s ORDER BY %s%s, id", where, col,
                     pick(2) ? " DESC" : "");
            break;
        case 1:
            snprintf(q, sizeof q, "SELECT id FROM r WHERE %s ORDER BY %s%s, id LIMIT %u", where, col,
                     pick(2) ? " DESC" : "", pick(5));
            break;
        case 2:
            snprintf(q, sizeof q, "SELECT * FROM r WHERE %s ORDER BY id%s", where, pick(2) ? " DESC" : "");
            break;
        default:
            snprintf(q, sizeof q, "DELETE FROM r WHERE %s", where);
            break;
        }
        if (strncmp(q, "DELETE", 6) == 0) {
            /* a write, held and rolled back, so both ways see the same rows */
            Answer x, y;
            E->begin_write(db, &held);
            held_write = 1;
            gsql_sql_force_scan(sql, 0);
            run(q, NULL, 3, &x);
            E->abort(held);
            E->begin_write(db, &held);
            gsql_sql_force_scan(sql, 1);
            run(q, NULL, 3, &y);
            E->abort(held);
            held = NULL;
            gsql_sql_force_scan(sql, 0);
            checks++;
            char *tx = as_text(&x.text), *ty = as_text(&y.text);
            if (diverged(x.code, y.code) || (x.code == GSQL_OK && y.code == GSQL_OK && strcmp(tx, ty) != 0)) {
                mismatches++;
                printf("MISMATCH %s\n  with paths: %s %s\n  scanning:   %s %s\n", q, gsql_error_name(x.code),
                       x.code ? x.err.message : tx, gsql_error_name(y.code), y.code ? y.err.message : ty);
            }
            refused += x.code != GSQL_OK;
            free(tx);
            free(ty);
            answer_free(&x);
            answer_free(&y);
            continue;
        }
        refused += both_ways(q, &seek_runs);
    }
    int grouped_seen = 0;
    int grouped_refused = grouping_oracle(n / 2, &grouped_seen);
    int distinct_seen = 0, deduplicated = 0;
    int distinct_refused = distinct_oracle(n / 2, &distinct_seen, &deduplicated);
    int set_seen = 0, by_op[3] = { 0, 0, 0 };
    int set_refused = set_oracle(n / 4, &set_seen, by_op);
    int q_rows = join_tables();
    int join_seeks = 0, joined_seen = 0;
    unsigned long js0, jc0, js1, jc1;
    gsql_sql_counts(sql, &js0, &jc0);
    int join_refused = join_tiers(n / 2, &join_seeks, &joined_seen);
    gsql_sql_counts(sql, &js1, &jc1);
    int sub_seeks = 0, by_form[3] = { 0, 0, 0 };
    unsigned long ss0, sc0, ss1, sc1;
    gsql_sql_counts(sql, &ss0, &sc0);
    int sub_refused = subquery_tier(n / 2, &sub_seeks, by_form);
    gsql_sql_counts(sql, &ss1, &sc1);
    unsigned long seeks, scans;
    gsql_sql_counts(sql, &seeks, &scans);
    printf("census: %d rows, %d queries, %d used a seek, %d refused, %d refused only by the scan\n", inserted, n,
           seek_runs, refused, narrowed_past);
    printf("census: %d grouped queries against the oracle, %d answered with groups, %d refused\n", n / 2,
           grouped_seen, grouped_refused);
    if (grouped_seen < n / 6) {
        mismatches++;
        printf("MISMATCH the grouping oracle compared too few answers with groups in them\n");
    }
    printf("census: %d DISTINCT queries against the oracle, %d answered with rows, %d of them dropping copies, "
           "%d refused, %d stopped by LIMIT before a refusal\n", n / 2, distinct_seen, deduplicated, distinct_refused,
           stopped_short);
    if (distinct_seen < n / 6 || deduplicated < n / 10) {
        mismatches++;
        printf("MISMATCH the DISTINCT oracle compared too few answers that dropped a copy\n");
    }
    printf("census: %d set operations against the oracle, %d answered with rows (%d union, %d intersect, %d except), "
           "%d refused\n", n / 4, set_seen, by_op[0], by_op[1], by_op[2], set_refused);
    /* each operation must have answered with rows, or a tier of unions alone
       would pass while INTERSECT and EXCEPT were never compared */
    if (set_seen < n / 20 || by_op[0] < 5 || by_op[1] < 3 || by_op[2] < 5) {
        mismatches++;
        printf("MISMATCH the set-operation oracle compared too few answers with rows in them\n");
    }
    printf("census: %d rows of q, %d joins both ways, %d of them sought, %lu seeks in all, %d answered by the "
           "oracle with rows, %d refused\n", q_rows, n / 2, join_seeks, js1 - js0, joined_seen, join_refused);
    /* the inner side must really have been sought, row by row: one seek per
       outer row, so far more seeks than queries */
    if (q_rows < 100 || join_seeks < n / 8 || js1 - js0 < (unsigned long)n || joined_seen < n / 6) {
        mismatches++;
        printf("MISMATCH the join census is below its floor: the tiers measured too little\n");
    }
    printf("census: %d subqueries both ways, %d of them sought, %lu seeks in all, answered by the oracle with rows: "
           "%d IN, %d EXISTS, %d counts; %d refused\n", n / 2, sub_seeks, ss1 - ss0, by_form[0], by_form[1],
           by_form[2], sub_refused);
    /* each form must have answered with rows, and a correlated subquery must
       really have sought q for each row of r: a binding that lost the outer
       reference's constancy answers correctly and only slowly */
    if (by_form[0] < n / 40 || by_form[1] < n / 40 || by_form[2] < n / 40 || ss1 - ss0 < (unsigned long)n) {
        mismatches++;
        printf("MISMATCH the subquery census is below its floor: the tier measured too little\n");
    }
    /* a floor on what the tier exercised: a planner that never seeks, or a
     * generator that is refused every time, agrees with the scan perfectly */
    if (forced_but_sought) {
        mismatches++;
        printf("MISMATCH %d queries forced to a scan did not scan: the comparison compared a path with itself\n",
               forced_but_sought);
    }
    if (inserted < 250 || seek_runs < n / 4 || refused > n / 2 || refused < 1) {
        mismatches++;
        printf("MISMATCH the census is below its floor: the tier measured too little\n");
    }
    E->close(db);
    gsql_sql_free(sql);
    printf("ok %d checks, %d mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "script") == 0) {
        if (!open_db(argv[3])) {
            return 2;
        }
        int rc = script(argv[2]);
        if (held) {
            E->abort(held);
        }
        E->close(db);
        gsql_sql_free(sql);
        if (rc) {
            return rc;
        }
        printf("ok %d checks, %d mismatches\n", checks, mismatches);
        return mismatches ? 1 : 0;
    }
    if (argc == 5 && strcmp(argv[1], "diff") == 0) {
        return differential(argv[2], strtoull(argv[3], NULL, 10), atoi(argv[4]));
    }
    fprintf(stderr, "usage: sql_test script FILE DIR | diff DIR SEED N\n");
    return 2;
}

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

static void render_rows(GsqlBuf *o, const GsqlBuf *rows, size_t *nrows) {
    GsBytes items[4096];
    long n = v3_array_items(rows->bytes, rows->length, items, 4096);
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
} Answer;

static void answer_free(Answer *a) {
    gsql_buf_free(&a->text);
    gsql_buf_free(&a->columns);
    gsql_buf_free(&a->first_value);
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
    fprintf(stderr, "usage: sql_test script FILE DIR\n");
    return 2;
}

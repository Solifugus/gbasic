/* G-SQL's SQL parser against a corpus (tests/gsql/sql_parse_cases.txt), with
 * no store and no interpreter (docs/gsql_sql_design.md §7, the parse tier).
 *
 * A case is a `>` line holding SQL, then either `=` and the canonical form the
 * statement must print back as, or `!` and the code, the position and a part
 * of the message a refusal must carry. A canonical form is also parsed AGAIN
 * and must print identically, so printing and parsing cannot drift apart. */

#include "sql.h"
#include "gsql_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, mismatches;

static void mismatch(int line, const char *fmt, const char *a, const char *b) {
    mismatches++;
    printf("MISMATCH line %d: ", line);
    printf(fmt, a, b);
    printf("\n");
}

static char *canon(const char *sql, GsqlError *err) {
    SqlParsed p;
    if (sql_parse(sql, strlen(sql), &p, err) != GSQL_OK) {
        return NULL;
    }
    GsqlBuf b = {0};
    sql_print_stmt(&b, p.stmt);
    char *s = malloc(b.length + 1);
    memcpy(s, b.bytes, b.length);
    s[b.length] = 0;
    gsql_buf_free(&b);
    sql_parsed_free(&p);
    return s;
}

int main(int argc, char **argv) {
    FILE *f = fopen(argc > 1 ? argv[1] : "tests/gsql/sql_parse_cases.txt", "r");
    if (!f) {
        perror("cases");
        return 2;
    }
    char line[4096], sql[4096] = "";
    int lineno = 0, sqlline = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '>') {
            /* consecutive `>` lines are one statement, joined by newlines */
            if (sqlline == lineno - 1 && sqlline) {
                size_t have = strlen(sql);
                snprintf(sql + have, sizeof sql - have, "\n%s", line[1] ? line + 2 : "");
            } else {
                snprintf(sql, sizeof sql, "%s", line[1] ? line + 2 : "");
            }
            sqlline = lineno;
            continue;
        }
        if (line[0] == '=') {
            checks++;
            GsqlError err = {0};
            char *got = canon(sql, &err);
            if (!got) {
                mismatch(sqlline, "refused (%s) where %s was wanted", err.message, line + 2);
                continue;
            }
            if (strcmp(got, line + 2) != 0) {
                mismatch(sqlline, "printed\n    %s\n  wanted\n    %s", got, line + 2);
            } else {
                char *again = canon(got, &err);
                if (!again || strcmp(again, got) != 0) {
                    mismatch(sqlline, "the canonical form does not print back as itself: %s -> %s", got,
                             again ? again : err.message);
                }
                free(again);
            }
            free(got);
            continue;
        }
        if (line[0] == '!') {
            checks++;
            char code[32];
            unsigned pos;
            int used = 0;
            if (sscanf(line + 2, "%31s %u %n", code, &pos, &used) < 2) {
                mismatch(lineno, "a malformed refusal line%s%s", "", "");
                continue;
            }
            const char *part = line + 2 + used;
            GsqlError err = {0};
            char *got = canon(sql, &err);
            if (got) {
                mismatch(sqlline, "accepted as %s where %s was wanted", got, code);
                free(got);
                continue;
            }
            char where[64];
            snprintf(where, sizeof where, "%s at %u", gsql_error_name(err.code), err.position);
            char want[64];
            snprintf(want, sizeof want, "%s at %u", code, pos);
            if (strcmp(where, want) != 0) {
                mismatch(sqlline, "refused as %s where %s was wanted", where, want);
            } else if (!strstr(err.message, part)) {
                mismatch(sqlline, "the message `%s` does not say `%s`", err.message, part);
            }
            continue;
        }
    }
    fclose(f);
    printf("ok %d checks, %d mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}

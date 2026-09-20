/* gb_parse_at — "this buffer is an EXCERPT; its first character is on line N."
 *
 * Sited here rather than in run_repl.sh because it is part of the EMBEDDING
 * API, and because a contract test in isolation is the only thing that can tell
 * a parser bug from a caller's bug — the same argument test_diagnostics_sink
 * makes one level down. The prompt is today's only caller; any host that parses
 * a fragment of a larger document (the LSP, a doc-example extractor) has the
 * same question and would use the same answer.
 *
 * THE LOAD-BEARING ASSERTION IS A DIFFERENCE, not a value: the same buffer
 * parsed at two origins must report positions differing by exactly the origin,
 * with the COLUMN UNMOVED. Asserting a single expected line passes on an
 * implementation that ignores the argument whenever the expectation happens to
 * be 1, and one that shifts columns too would still satisfy "the line moved".
 *
 * THE CONTROL is that gb_parse itself is unchanged — it is gb_parse_at with 1,
 * and eleven existing callers depend on that being true rather than nearly
 * true. */
#include "gbasic.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; } \
} while (0)

/* A parse error on the THIRD line of the buffer. */
static const char *SRC = "x = 1\ny = 2\n)\n";

static int first_line_of(gb_diagnostics *d) {
    if (gb_diagnostics_count(d) < 1) { return -1; }
    return gb_diagnostics_at(d, 0)->span.start_line;
}
static int first_col_of(gb_diagnostics *d) {
    if (gb_diagnostics_count(d) < 1) { return -1; }
    return gb_diagnostics_at(d, 0)->span.start_column;
}

int main(void) {
    gb_diagnostics at1, at0, at100, plain;
    AstStmtList p1 = {0}, p0 = {0}, p100 = {0}, pp = {0};

    gb_diagnostics_init(&at1);
    gb_diagnostics_init(&at0);
    gb_diagnostics_init(&at100);
    gb_diagnostics_init(&plain);

    (void)gb_parse_at(SRC, "buf.bas", 1,   &p1,   &at1);
    (void)gb_parse_at(SRC, "buf.bas", 0,   &p0,   &at0);
    (void)gb_parse_at(SRC, "buf.bas", 100, &p100, &at100);
    (void)gb_parse(SRC, "buf.bas", &pp, &plain);

    CHECK(gb_diagnostics_count(&at1) == 1, "the buffer produces one diagnostic");
    CHECK(first_line_of(&at1) == 3, "at origin 1 the error is on line 3");

    /* THE DIFFERENCE. Origin 0 and origin 100 must shift the SAME diagnostic by
     * exactly -1 and +99, and must not move the column at all. */
    CHECK(first_line_of(&at0) == 2, "origin 0 shifts it to line 2");
    CHECK(first_line_of(&at100) == 102, "origin 100 shifts it to line 102");
    /* THE COLUMN CLAIM NEEDS AN ERROR ON THE FIRST LINE and these cannot make
     * it: the lexer resets the column at every newline, so a diagnostic on
     * line 3 has forgotten whatever the column started at. MEASURED -- a
     * perturbation shifting the starting column by the origin left all of
     * these green. The first-line buffer below is the one that bites. */
    CHECK(first_col_of(&at0) == first_col_of(&at1), "origin 0 leaves the column alone");
    CHECK(first_col_of(&at100) == first_col_of(&at1), "origin 100 leaves the column alone");
    {
        gb_diagnostics c1, c9;
        AstStmtList q1 = {0}, q9 = {0};
        gb_diagnostics_init(&c1);
        gb_diagnostics_init(&c9);
        /* Error at column 5 of line 1, before any newline has been seen. */
        (void)gb_parse_at("x = )", "buf.bas", 1, &q1, &c1);
        (void)gb_parse_at("x = )", "buf.bas", 9, &q9, &c9);
        CHECK(first_col_of(&c1) == 5, "a first-line error is at column 5");
        CHECK(first_col_of(&c9) == first_col_of(&c1),
              "and the origin does not move it");
        CHECK(first_line_of(&c1) == 1 && first_line_of(&c9) == 9,
              "while the line does move");
        gb_diagnostics_free(&c1);
        gb_diagnostics_free(&c9);
    }

    /* THE CONTROL: gb_parse is gb_parse_at with 1, exactly. */
    CHECK(first_line_of(&plain) == first_line_of(&at1),
          "gb_parse agrees with gb_parse_at(.., 1, ..)");
    CHECK(first_col_of(&plain) == first_col_of(&at1),
          "gb_parse agrees on the column too");

    /* A CLEAN parse carries the origin onto the AST, which is what runtime
     * diagnostics read — a check on the sink alone would miss that entirely,
     * and the runtime half is the half the prompt actually needed. */
    {
        gb_diagnostics ok0, ok1;
        AstStmtList a = {0}, b = {0};
        gb_diagnostics_init(&ok0);
        gb_diagnostics_init(&ok1);
        int r1 = gb_parse_at("x = 1\ny = 2\n", "buf.bas", 1, &b, &ok1);
        int r0 = gb_parse_at("x = 1\ny = 2\n", "buf.bas", 7, &a, &ok0);
        CHECK(r1 == 0 && r0 == 0, "both clean parses succeed");
        if (r1 == 0 && r0 == 0 && b.count == 2 && a.count == 2) {
            CHECK(b.items[1]->line == 2, "at origin 1 the second statement is on line 2");
            CHECK(a.items[1]->line == 8, "at origin 7 it is on line 8");
            CHECK(a.items[1]->column == b.items[1]->column, "and the column is unmoved");
        } else {
            CHECK(0, "both parses yield two statements");
        }
        gb_diagnostics_free(&ok0);
        gb_diagnostics_free(&ok1);
    }

    gb_diagnostics_free(&at1);
    gb_diagnostics_free(&at0);
    gb_diagnostics_free(&at100);
    gb_diagnostics_free(&plain);

    if (failures == 0) {
        printf("    PASS test_parse_at\n");
        return 0;
    }
    printf("    %d failure(s)\n", failures);
    return 1;
}

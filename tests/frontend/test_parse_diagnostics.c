/* Phase 1 diagnostics test — TESTS-FIRST, EXPECTED TO FAIL until the lexer /
 * parser reporters are wired into the sink.
 *
 * Intent (from the plan): "parse a buffer with 3 known errors, assert 3
 * structured diagnostics with exact positions." The current front end has no
 * error recovery — a single parse aborts at the first error — so three errors
 * are surfaced by parsing three one-error buffers into ONE shared sink. Each
 * buffer's code / severity / exact span / message was verified against the
 * current binary (see the conversation log). Post-Phase-1, gb_parse must append
 * these three structured records instead of writing to stderr.
 *
 * Pre-Phase-1 the gb_parse shim leaves the sink empty, so this fails at the very
 * first "3 diagnostics" assertion. */
#include "gbasic.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; } \
} while (0)

typedef struct {
    const char  *source;      /* one-error buffer, no trailing newline */
    gb_severity  severity;
    gb_diag_code code;
    int          start_line, start_col, end_line, end_col;
    const char  *message;
} Expected;

int main(void) {
    /* buffer, severity, code, start(l,c), end(l,c), message */
    static const Expected expect[3] = {
        { ")",    GB_SEVERITY_ERROR, GB_DIAG_PARSE_ERROR, 1, 1, 1, 2,
          "syntax error, unexpected RPAREN, expecting end of file" },
        { "\"abc", GB_SEVERITY_ERROR, GB_DIAG_LEX_DETAIL, 1, 1, 1, 5,
          "unterminated string" },
        { "?",    GB_SEVERITY_ERROR, GB_DIAG_LEX_ERROR, 1, 1, 1, 2,
          "unexpected token" },
    };

    gb_diagnostics d;
    gb_diagnostics_init(&d);

    for (int i = 0; i < 3; i++) {
        AstStmtList program = {0};
        (void)gb_parse(expect[i].source, "buf.bas", &program, &d);
    }

    CHECK(gb_diagnostics_count(&d) == 3, "exactly 3 diagnostics collected");

    if (gb_diagnostics_count(&d) == 3) {
        for (int i = 0; i < 3; i++) {
            const gb_diag *g = gb_diagnostics_at(&d, i);
            const Expected *e = &expect[i];
            CHECK(g->severity == e->severity, "severity matches");
            CHECK(g->code == e->code, "code matches");
            CHECK(g->span.start_line == e->start_line && g->span.start_column == e->start_col,
                  "span start matches");
            CHECK(g->span.end_line == e->end_line && g->span.end_column == e->end_col,
                  "span end matches");
            CHECK(g->path && strcmp(g->path, "buf.bas") == 0, "path matches");
            CHECK(g->message && strcmp(g->message, e->message) == 0, "message matches");
        }
    }

    gb_diagnostics_free(&d);

    if (failures == 0) {
        printf("PASS test_parse_diagnostics\n");
        return 0;
    }
    printf("FAIL test_parse_diagnostics (%d checks failed)\n", failures);
    return 1;
}

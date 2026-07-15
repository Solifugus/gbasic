/* Unit test for the standalone diagnostic sink (include/diagnostics.h).
 * Exercises the API directly, with no lexer/parser/evaluator involvement, so it
 * validates the struct + sink contract in isolation. Expected: PASS from the
 * moment diagnostics.c exists. */
#include "diagnostics.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; } \
} while (0)

int main(void) {
    gb_diagnostics d;
    gb_diagnostics_init(&d);
    CHECK(gb_diagnostics_count(&d) == 0, "fresh sink is empty");

    gb_span s1 = {1, 5, 1, 9};
    gb_diagnostics_add(&d, GB_SEVERITY_ERROR, GB_DIAG_LEX_DETAIL, 0,
                       "buf.bas", s1, "unterminated string");
    gb_span s2 = {3, 1, 3, 2};
    gb_diagnostics_add(&d, GB_SEVERITY_WARNING, GB_DIAG_PARSE_ERROR, 42,
                       NULL, s2, "just a warning");

    CHECK(gb_diagnostics_count(&d) == 2, "two diagnostics appended");

    const gb_diag *a = gb_diagnostics_at(&d, 0);
    CHECK(a != NULL, "index 0 present");
    CHECK(a->severity == GB_SEVERITY_ERROR, "a severity");
    CHECK(a->code == GB_DIAG_LEX_DETAIL, "a code");
    CHECK(a->subcode == 0, "a subcode");
    CHECK(a->path && strcmp(a->path, "buf.bas") == 0, "a path copied");
    CHECK(a->span.start_line == 1 && a->span.start_column == 5, "a span start");
    CHECK(a->span.end_line == 1 && a->span.end_column == 9, "a span end");
    CHECK(a->message && strcmp(a->message, "unterminated string") == 0, "a message copied");

    const gb_diag *b = gb_diagnostics_at(&d, 1);
    CHECK(b != NULL, "index 1 present");
    CHECK(b->severity == GB_SEVERITY_WARNING, "b severity");
    CHECK(b->subcode == 42, "b subcode carried");
    CHECK(b->path == NULL, "b NULL path stays NULL");

    CHECK(gb_diagnostics_at(&d, 2) == NULL, "out-of-range returns NULL");

    CHECK(strcmp(gb_diag_code_str(GB_DIAG_PARSE_ERROR), "GB_DIAG_PARSE_ERROR") == 0,
          "code mnemonic");
    CHECK(strcmp(gb_severity_str(GB_SEVERITY_WARNING), "warning") == 0,
          "severity mnemonic");

    /* Force a growth past the initial capacity to exercise realloc. */
    for (int i = 0; i < 50; i++) {
        gb_span sp = {i, 1, i, 2};
        gb_diagnostics_add(&d, GB_SEVERITY_NOTE, GB_DIAG_NONE, 0, "x", sp, "n");
    }
    CHECK(gb_diagnostics_count(&d) == 52, "sink grows past initial capacity");

    gb_diagnostics_free(&d);
    CHECK(gb_diagnostics_count(&d) == 0, "freed sink is empty");

    if (failures == 0) {
        printf("PASS test_diagnostics_sink\n");
        return 0;
    }
    printf("FAIL test_diagnostics_sink (%d checks failed)\n", failures);
    return 1;
}

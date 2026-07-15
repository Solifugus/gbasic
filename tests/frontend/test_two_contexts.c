/* Phase 2 reentrancy smoke test — must PASS now that the parser is reentrant.
 *
 * Two threads each parse a valid buffer many times through gb_parse. Before
 * Phase 2 the global Bison parser (shared yylval/yylloc/yychar/parser stack plus
 * the file-scope active_lexer/parsed_program) made concurrent parses race and
 * corrupt or crash. Phase 2 threads a stack-allocated gb_parse_ctx through
 * yyparse/yylex/yyerror (api.pure full + %param), so every call must return 0
 * with the correct statement count and no crash. */
#include "gbasic.h"

#include <pthread.h>
#include <stdio.h>

#define ITERS 500

static const char *SRC = "a = 1\nb = 2\nc = a + b\n"; /* 3 statements */

static void *worker(void *arg) {
    long *bad = (long *)arg;
    for (int i = 0; i < ITERS; i++) {
        AstStmtList program = {0};
        gb_diagnostics d;
        gb_diagnostics_init(&d);
        int rc = gb_parse(SRC, "t.bas", &program, &d);
        if (rc != 0 || program.count != 3) {
            (*bad)++;
        }
        gb_diagnostics_free(&d);
    }
    return NULL;
}

int main(void) {
    pthread_t t1, t2;
    long bad1 = 0, bad2 = 0;

    pthread_create(&t1, NULL, worker, &bad1);
    pthread_create(&t2, NULL, worker, &bad2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    if (bad1 == 0 && bad2 == 0) {
        printf("PASS test_two_contexts\n");
        return 0;
    }
    printf("FAIL test_two_contexts (%ld+%ld bad parses of %d)\n", bad1, bad2, ITERS * 2);
    return 1;
}

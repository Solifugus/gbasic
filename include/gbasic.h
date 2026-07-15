#ifndef GBASIC_H
#define GBASIC_H

#include "ast.h"
#include "diagnostics.h"

/* Front-end entry point (PLAN.md Phases 1-2).
 *
 * Parse `source` (a NUL-terminated buffer) into an AST. `path` is used only for
 * diagnostic locations and may be NULL. Structured diagnostics are appended to
 * `diags`. Returns 0 on a clean parse, non-zero if any error diagnostics were
 * produced.
 *
 * SCAFFOLDING NOTE (pre-Phase-1): the current implementation delegates to the
 * legacy global parser and does NOT yet populate `diags` — routing the lexer /
 * parser reporters into the sink is the Phase 1 wiring, and making this
 * reentrant is Phase 2. This declaration exists now so the Phase 1 tests can be
 * written failing against the intended shape. */
int gb_parse(const char *source, const char *path,
             AstStmtList *out_program, gb_diagnostics *diags);

#endif

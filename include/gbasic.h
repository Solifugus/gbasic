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

/* As gb_parse, for a buffer that is an EXCERPT of something larger: `first_line`
 * is the line number its first character really has, and every position in the
 * resulting AST and in `diags` is numbered from there. gb_parse is this with 1.
 *
 * It exists because a host that WRAPS the author's text -- the prompt runs a
 * question as `print (\n<text>\n)` -- otherwise reports every diagnostic
 * against a line the author never typed, and no wrapper can avoid it: a shorter
 * opener on the same line fixes the line and breaks the column instead. The
 * offsets are stamped onto AST nodes as they are built and both parse-time and
 * run-time diagnostics read them from there, so this is the only place the
 * question can be answered once. */
int gb_parse_at(const char *source, const char *path, int first_line,
                AstStmtList *out_program, gb_diagnostics *diags);

#endif

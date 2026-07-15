#include "gbasic.h"

/* Legacy global parser entry points (src/parser.y). Declared here rather than
 * pulled from a header because parse_source is not yet part of a public
 * interface — Phase 2 replaces this shim with a reentrant, sink-aware parse. */
extern int parse_source(const char *source, AstStmtList *out_program);
extern void parse_set_source_path(const char *path);

int gb_parse(const char *source, const char *path,
             AstStmtList *out_program, gb_diagnostics *diags) {
    /* Pre-Phase-1: the sink is intentionally left empty. The legacy parser still
     * reports to stderr; wiring it into `diags` is the next step. */
    (void)diags;
    parse_set_source_path(path);
    return parse_source(source, out_program);
}

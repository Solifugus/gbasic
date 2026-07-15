#include "gbasic.h"

/* Legacy global parser entry points (src/parser.y). Declared here rather than
 * pulled from a header because parse_source is not yet part of a public
 * interface — Phase 2 replaces this shim with a reentrant, sink-aware parse. */
extern int parse_source(const char *source, AstStmtList *out_program);
extern void parse_set_source_path(const char *path);

int gb_parse(const char *source, const char *path,
             AstStmtList *out_program, gb_diagnostics *diags) {
    /* Install the sink for the duration of the parse: the parser's reporters push
     * structured diagnostics into it instead of stderr. Save/restore the previous
     * sink so nested/sequential parses compose cleanly. */
    gb_diagnostics *prev = gb_get_active_sink();
    gb_set_active_sink(diags);
    parse_set_source_path(path);
    int rc = parse_source(source, out_program);
    gb_set_active_sink(prev);
    return rc;
}

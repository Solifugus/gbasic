#include "gbasic.h"

/* Reentrant parse core (src/parser.y). Threads the sink and path straight into a
 * stack-allocated per-parse context, touching no file-scope parser state — so
 * concurrent gb_parse calls in one process share nothing. Declared here rather
 * than in a header because it is not yet part of a public interface. */
extern int parse_source_reentrant(const char *source, const char *path,
                                  gb_diagnostics *diags, AstStmtList *out_program);

int gb_parse(const char *source, const char *path,
             AstStmtList *out_program, gb_diagnostics *diags) {
    return parse_source_reentrant(source, path, diags, out_program);
}

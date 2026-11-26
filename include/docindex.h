#ifndef GBASIC_DOCINDEX_H
#define GBASIC_DOCINDEX_H

#include <stdio.h>

/* `gbasic --doc NAME`: the reference's entry for a builtin or module verb,
 * or a library function's signature and comment from its own file; 0 when
 * found, 1 (with the nearest names) when not. */
int gb_doc_main(const char *name, FILE *out);

/* `gbasic --explain CODE`: what a diagnostic, runtime error or warning code
 * means, from docs/ai/ERRORS.md; 0 when found, 1 when not. */
int gb_explain_main(const char *code, FILE *out);

#endif

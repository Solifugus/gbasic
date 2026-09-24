#ifndef GBASIC_PARSE_CTX_H
#define GBASIC_PARSE_CTX_H

#include "ast.h"
#include "lexer.h"
#include "diagnostics.h"

/* Per-parse state for the reentrant Bison parser (PLAN.md Phase 2).
 *
 * Everything the grammar and its lexer glue mutate lives here. It is stack-
 * allocated per parse (see parse_source_reentrant / parse_source in parser.y)
 * and threaded through yyparse/yylex/yyerror via Bison's %param, so concurrent
 * parses in one process share nothing. This replaces the four former file-scope
 * parser globals (active_lexer, lexer_error_reported, active_parse_path,
 * parsed_program) plus a per-parse diagnostics sink.
 *
 * This header is included from BOTH the parser's %code requires block (so the
 * generated header's yyparse(gb_parse_ctx*) prototype sees the type) and the
 * prologue (so the parser's helper functions see it). The include guard makes
 * the eventual re-include via parser.tab.h harmless. */
typedef struct {
    Lexer          *active_lexer;          /* current lexer (borrowed) */
    int             lexer_error_reported;  /* a lexer error already reported; suppress the
                                              redundant syntax error Bison would raise next */
    const char     *active_parse_path;     /* path for diagnostic locations (borrowed, may be NULL) */
    AstStmtList     parsed_program;        /* parse result, filled by the top rule */
    gb_diagnostics *diags;                 /* diagnostic sink; NULL => immediate stderr (legacy) */

    /* Location of the most recently lexed token, mirroring exactly what yylex
     * writes to *llocp. The former global yyerror read the offending token's
     * location from the global yylloc; recording it here lets action-level error
     * reporting reproduce that identical location without a global. */
    int la_line;
    int la_column;
    int la_end_line;
    int la_end_column;

    /* THE LAST TWO TOKENS, so a syntax error can say WHICH WORD was the
     * problem (DOGFOOD 43). `on = 0` reported `unexpected OP_EQ, expecting
     * IDENT or ERROR_VALUE` -- the position was right and the message was
     * about the `=`, the one token on that line that is NOT the problem, while
     * nothing said `on` is reserved. Bison knows the token; naming it is a
     * message, not an analysis.
     *
     * Two tokens rather than one, because the reserved word is the UNEXPECTED
     * token in a parameter list (`function f(a, each)`) and the one BEFORE it
     * in an assignment (`on = 0`). Spelled as the AUTHOR typed it, not as the
     * grammar names it -- a reader should not have to know that `EACH` is the
     * spelling of their own word. */
    TokenType       tok_type;              /* most recent token */
    TokenType       tok_prev_type;
    char            tok_word[40];          /* its source spelling, "" if not word-shaped */
    char            tok_prev_word[40];
    const char     *tok_after;             /* source just past the token (borrowed) */
} gb_parse_ctx;

#endif

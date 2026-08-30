%{
#include "ast.h"
#include "builtins.h"
#include "diagnostics.h"
#include "lexer.h"
#include "parse_ctx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A keyword used as a FIELD NAME arrives as its own token, carrying no text,
 * so the spelling is supplied here. Local rather than reusing eval.c's
 * copy_string: the parser is a separate translation unit and strdup is not in
 * strict C11. */
static char *kw_name(const char *s) {
    size_t n = strlen(s) + 1;
    char *out = malloc(n);
    if (!out) abort();
    memcpy(out, s, n);
    return out;
}

/* Report a diagnostic with an explicit span. Routes through gb_report_to, which
 * pushes to the per-parse sink (ctx->diags) or, when that is NULL, prints in the
 * legacy stderr format. */
static void report_diag(gb_parse_ctx *ctx, gb_diag_code code, int line, int column,
                        int end_line, int end_column, const char *message) {
    gb_span span = { line, column, end_line, end_column };
    gb_report_to(ctx->diags, code, 0, ctx->active_parse_path, span, message);
}


/* PLAT-WARN: `on warning ...` and `warning <expr>` carry NO reserved word --
 * the channel is an ordinary IDENT recognized by POSITION (the technique the
 * server block proved) and validated here. A word that is not "warning" is
 * named in the diagnostic rather than producing a bare syntax error, because
 * `on wanring stop` should say so. */
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);

/* PLAT-NEXT: `next NAME` must name the loop it closes.
 *
 * Classic BASIC let `next x` close an inner `y` loop by implicitly closing
 * both, so a one-letter typo silently restructured the program. Refused here:
 * either name this loop, or write `next` with no name at all. Consumes the
 * closer's name either way. */
static int for_end_matches(gb_parse_ctx *ctx, const char *loop_variable,
                           char *closer, int line, int column) {
    int ok;
    if (!closer) {
        return 1;
    }
    ok = loop_variable && strcmp(loop_variable, closer) == 0;
    if (!ok) {
        char message[256];
        snprintf(message, sizeof(message),
                 "next %s does not close this loop: it iterates %s "
                 "(write `next %s`, `next`, or `end for`)",
                 closer, loop_variable ? loop_variable : "another variable",
                 loop_variable ? loop_variable : "");
        report_syntax_error(ctx, line, column, line, column, message);
    }
    free(closer);
    return ok;
}

static int warn_channel_ok(gb_parse_ctx *ctx, const char *word,
                           int line, int column) {
    if (word && strcmp(word, "warning") == 0) {
        return 1;
    }
    char message[192];
    snprintf(message, sizeof(message),
             "unknown diagnostic channel '%s'; `on` takes `error` or `warning`",
             word ? word : "");
    report_diag(ctx, GB_DIAG_PARSE_ERROR, line, column, line, column, message);
    return 0;
}

static int warn_mode_word(gb_parse_ctx *ctx, const char *word,
                          int line, int column) {
    if (word && strcmp(word, "ignore") == 0) {
        return WARN_MODE_IGNORE;
    }
    char message[192];
    snprintf(message, sizeof(message),
             "unknown warning mode '%s'; expected print, ignore, stop, or goto next",
             word ? word : "");
    report_diag(ctx, GB_DIAG_PARSE_ERROR, line, column, line, column, message);
    return -1;
}

/* Same, computing the end position by walking `len` bytes of the lexeme exactly
 * as the lexer's advance() does (byte-based columns, '\n' resets to column 1). */
static void report_diag_lexeme(gb_parse_ctx *ctx, gb_diag_code code, int line, int column,
                               const char *text, int len, const char *message) {
    int end_line = line;
    int end_column = column;
    for (int i = 0; i < len; i++) {
        if (text[i] == '\n') {
            end_line++;
            end_column = 1;
        } else {
            end_column++;
        }
    }
    report_diag(ctx, code, line, column, end_line, end_column, message);
}

static char *copy_text(const char *start, int length) {
    char *text = malloc((size_t)length + 1);
    if (!text) {
        abort();
    }
    memcpy(text, start, (size_t)length);
    text[length] = '\0';
    return text;
}

static int hex_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* Encode a Unicode scalar value as UTF-8 into out (up to 4 bytes); returns the
 * byte count. Caller guarantees a valid scalar (0..0x10FFFF, no surrogate). */
static int utf8_encode_literal(unsigned cp, char out[4]) {
    if (cp <= 0x7fu) {
        out[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7ffu) {
        out[0] = (char)(0xc0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3fu));
        return 2;
    } else if (cp <= 0xffffu) {
        out[0] = (char)(0xe0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        out[2] = (char)(0x80u | (cp & 0x3fu));
        return 3;
    }
    out[0] = (char)(0xf0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3fu));
    out[3] = (char)(0x80u | (cp & 0x3fu));
    return 4;
}

static char *copy_string_literal(gb_parse_ctx *ctx, const char *start, int length, int line, int column, int *ok) {
    *ok = 1;
    if (length < 2) {
        return copy_text("", 0);
    }

    char *text = malloc((size_t)length - 1);
    if (!text) {
        abort();
    }
    int out = 0;
    for (int i = 1; i < length - 1; i++) {
        if (start[i] == '\\') {
            if (i + 1 >= length - 1) {
                report_diag_lexeme(ctx, GB_DIAG_STRING_LITERAL, line, column, start, length, "unterminated escape sequence");
                *ok = 0;
                free(text);
                return NULL;
            }
            i++;
            if (start[i] == 'n') {
                text[out++] = '\n';
            } else if (start[i] == 't') {
                text[out++] = '\t';
            } else if (start[i] == '"' || start[i] == '\\') {
                text[out++] = start[i];
            } else if (start[i] == 'u') {
                /* \u{HHHH}: decode a Unicode scalar to UTF-8. The lexer already
                 * guaranteed the { hexdigits } shape, so just read it. */
                i++; /* the '{' */
                unsigned cp = 0;
                int digits = 0;
                i++; /* first hex digit */
                while (i < length - 1 && start[i] != '}') {
                    cp = cp * 16u + (unsigned)hex_digit_value(start[i]);
                    digits++;
                    i++;
                }
                /* i now points at '}', which the for-loop's i++ will consume. */
                if (digits > 6 || cp > 0x10FFFFu) {
                    report_diag_lexeme(ctx, GB_DIAG_STRING_LITERAL, line, column, start, length,
                                       "invalid unicode escape: codepoint must be between 0 and 0x10FFFF");
                    *ok = 0;
                    free(text);
                    return NULL;
                }
                if (cp >= 0xD800u && cp <= 0xDFFFu) {
                    report_diag_lexeme(ctx, GB_DIAG_STRING_LITERAL, line, column, start, length,
                                       "invalid unicode escape: surrogate codepoints (0xD800..0xDFFF) are not valid");
                    *ok = 0;
                    free(text);
                    return NULL;
                }
                if (cp == 0) {
                    report_diag_lexeme(ctx, GB_DIAG_STRING_LITERAL, line, column, start, length,
                                       "invalid unicode escape: \\u{0} is not allowed in a literal; use chr(0)");
                    *ok = 0;
                    free(text);
                    return NULL;
                }
                char utf8[4];
                int n = utf8_encode_literal(cp, utf8);
                for (int b = 0; b < n; b++) {
                    text[out++] = utf8[b];
                }
            } else {
                char message[64];
                snprintf(message, sizeof(message), "invalid escape sequence: \\%c", start[i]);
                report_diag_lexeme(ctx, GB_DIAG_STRING_LITERAL, line, column, start, length, message);
                *ok = 0;
                free(text);
                return NULL;
            }
        } else {
            text[out++] = start[i];
        }
    }
    text[out] = '\0';
    return text;
}

static char *copy_const(const char *text) {
    return copy_text(text, (int)strlen(text));
}

static char *join_watch_path(char *left, char *right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    char *text = malloc(left_len + 1 + right_len + 1);
    if (!text) {
        abort();
    }
    memcpy(text, left, left_len);
    text[left_len] = '.';
    memcpy(text + left_len + 1, right, right_len + 1);
    free(left);
    free(right);
    return text;
}

static void split_qualified_ident(char *text, char **library, char **name) {
    char *dot = strchr(text, '.');
    if (!dot) {
        *library = text;
        *name = copy_const("");
        return;
    }
    *dot = '\0';
    *library = text;
    *name = copy_const(dot + 1);
}

static int is_modifier_target_expr(AstExpr *expr) {
    if (!expr) {
        return 0;
    }
    if (expr->kind == AST_EXPR_IDENT) {
        return 1;
    }
    if (expr->kind == AST_EXPR_FIELD) {
        return is_modifier_target_expr(expr->as.field.object);
    }
    if (expr->kind == AST_EXPR_INDEX) {
        return is_modifier_target_expr(expr->as.index.array);
    }
    return 0;
}

static AstExpr *expr_at(AstExpr *expr, int line, int column) {
    return ast_expr_position(expr, line, column);
}

static char *join_words(char *left, char *right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    char *joined = malloc(left_len + right_len + 2);
    if (!joined) {
        abort();
    }
    memcpy(joined, left, left_len);
    joined[left_len] = ' ';
    memcpy(joined + left_len + 1, right, right_len + 1);
    free(left);
    free(right);
    return joined;
}

static AstModifierUse parse_modifier_use(char *text) {
    AstModifierUse modifier = ast_modifier_use(text, ast_expr_list_empty());
    char *dot = strchr(modifier.name, '.');
    if (!dot) {
        return modifier;
    }

    char *start = modifier.name;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    char *library_end = dot;
    while (library_end > start && (library_end[-1] == ' ' || library_end[-1] == '\t')) {
        library_end--;
    }
    char *name_start = dot + 1;
    while (*name_start == ' ' || *name_start == '\t') {
        name_start++;
    }
    if (library_end == start || *name_start == '\0') {
        return modifier;
    }

    size_t library_len = (size_t)(library_end - start);
    char *library = malloc(library_len + 1);
    if (!library) {
        abort();
    }
    memcpy(library, start, library_len);
    library[library_len] = '\0';

    char *name = copy_const(name_start);
    free(modifier.name);
    modifier.library = library;
    modifier.name = name;
    return modifier;
}

static int unit_is(const char *text, const char *unit) {
    return strcmp(text, unit) == 0;
}

static AstDuration duration_add_unit(AstDuration duration, double amount, char *unit) {
    int value = (int)amount;
    if (unit_is(unit, "year") || unit_is(unit, "years")) {
        duration.years += value;
    } else if (unit_is(unit, "month") || unit_is(unit, "months")) {
        duration.months += value;
    } else if (unit_is(unit, "week") || unit_is(unit, "weeks")) {
        duration.weeks += value;
    } else if (unit_is(unit, "day") || unit_is(unit, "days")) {
        duration.days += value;
    } else if (unit_is(unit, "hour") || unit_is(unit, "hours")) {
        duration.hours += value;
    } else if (unit_is(unit, "minute") || unit_is(unit, "minutes")) {
        duration.minutes += value;
    } else if (unit_is(unit, "second") || unit_is(unit, "seconds")) {
        duration.seconds += value;
    } else {
        fprintf(stderr, "unknown duration unit: %s\n", unit);
    }
    free(unit);
    return duration;
}


%}

%code requires {
#include "ast.h"
#include "parse_ctx.h"

typedef enum {
    IDENT_SUFFIX_NONE,
    IDENT_SUFFIX_CALL,
    IDENT_SUFFIX_FIELD,
    IDENT_SUFFIX_QUALIFIED_CALL,
    IDENT_SUFFIX_METHOD          /* var.field.method(args): a chained method call */
} AstIdentSuffixKind;

typedef struct {
    AstIdentSuffixKind kind;
    char *name;
    AstExprList args;
} AstIdentSuffix;

/* Parsed PBI per-field policy clause: `( copy | link | exclude | reset <expr> )` */
typedef struct {
    AstFieldPolicy policy;
    AstExpr *reset_expr;
} FieldPolicySpec;
}

%union {
    double number;
    char *text;
    AstExpr *expr;
    AstStmt *stmt;
    AstStmtList stmt_list;
    AstExprList expr_list;
    AstRecordFieldList record_field_list;
    AstConsiderBranchList consider_branch_list;
    AstNameList name_list;
    AstModifierUse modifier;
    AstModifierSignature modifier_signature;
    AstDuration duration;
    AstIdentSuffix ident_suffix;
    FieldPolicySpec field_policy;
    char op_char;   /* compound-assignment operator: + - * / */
    AstServerItem *server_item;
    AstServerItemList server_item_list;
}

%token <number> NUMBER
%token <text> IDENT STRING LENS_CONTENT QUALIFIED_IDENT
/* `as` reaches the parser ONLY as a field name; everywhere else it is consumed
 * by the lexer's modifier/lens modes. Declared so it can join field_name --
 * before this it fell through to the token mapper's default arm and was
 * reported as an unexpected token rather than a keyword clash. */
%token AS
%token DIM
%token PLUS_EQ MINUS_EQ STAR_EQ SLASH_EQ
%token IF CONSIDER_IF THEN ELSE CONSIDER_ELSE END END_CONSIDER PRINT TRUE FALSE NOTHING UNKNOWN_VALUE AND OR NOT WITH NEW SPAWN FOR TO STEP DO UNTIL IN EACH WHILE CONSIDER BREAK CONTINUE FUNCTION RETURN GOTO GOSUB WATCH UNWATCH WITHOUT WATCHERS ON NEXT STOP ERROR_VALUE MODIFIER PROGRAM LIBRARY LOAD USE EXPORT
%token OP_EQ OP_NE OP_GT OP_LT OP_GE OP_LE OP_NGT OP_NLT OP_NGE OP_NLE
%token PLUS MINUS STAR SLASH LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE COMMA COLON NEWLINE
%precedence IF_WITHOUT_ELSE
%precedence ELSE
%precedence NO_DOT
%left DOT
%define parse.error verbose
%define api.pure full
%param {gb_parse_ctx *ctx}
%locations

/* Emitted after YYSTYPE/YYLTYPE are defined (so the pure-parser signatures are
 * legal here) and before yyparse — where the grammar actions that call
 * report_syntax_error live. */
%code {
static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx);
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message);
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message);
}

%type <stmt_list> program statement_list consider_statement_list consider_else_opt if_block_tail if_inline_tail
%type <text> for_end
%type <op_char> compound_op
%type <stmt> statement assignment print_statement call_statement with_lock_statement for_each_statement do_loop_statement while_statement consider_statement consider_body_statement function_statement modifier_statement program_statement library_statement use_statement return_statement label_statement goto_statement gosub_statement break_statement continue_statement watch_statement unwatch_statement without_watchers_statement on_error_statement error_statement if_statement inline_statement server_statement
%type <server_item> server_item
%type <server_item_list> server_item_list
%type <name_list> server_string_list
%type <expr> expression or_expression and_expression comparison_expression
%type <expr> additive_expression multiplicative_expression unary_expression postfix_expression primary lvalue record_literal
%type <expr_list> argument_list argument_list_opt array_argument_list
%type <text> field_name dot_field_name
%type <record_field_list> record_field_list
%type <field_policy> field_policy
%type <consider_branch_list> consider_branch_list
%type <expr> parameter_default
%type <name_list> parameter_list parameter_list_opt watch_target_list
%type <modifier> comparison_lens
%type <modifier_signature> modifier_signature
%type <duration> duration_terms
%type <ident_suffix> ident_suffix ident_dot_suffix
%type <text> modifier_name modifier_word modifier_context comparison_operator variable_name watch_target_path

/* Free semantic values discarded during error recovery. Without these, every
 * syntax error leaks the AST nodes / strings that were on the parser stack — which
 * matters now that gb_parse is called repeatedly on often-invalid source by
 * gbasic-lsp (per keystroke) and source_outline. Bison invokes a %destructor only
 * for symbols DISCARDED (popped on error / left on the stack at abort), never for
 * symbols consumed by a reduction, so these do not double-free values moved into a
 * parent node. Actions that both free a symbol and YYERROR null it first (below) so
 * the destructor's free is a no-op. POD types (number, duration, field_policy w/o
 * expr) need no destructor. */
%destructor { free($$); }                              <text>
%destructor { ast_free_expr($$); }                     <expr>
%destructor { ast_free_stmt($$); }                     <stmt>
%destructor { ast_free_program($$); }                  <stmt_list>
%destructor { ast_free_expr_list($$); }                <expr_list>
%destructor { ast_free_record_field_list($$); }        <record_field_list>
%destructor { ast_free_consider_branch_list($$); }     <consider_branch_list>
%destructor { ast_free_name_list($$); }                <name_list>
%destructor { ast_free_modifier_use($$); }             <modifier>
%destructor { ast_free_modifier_signature($$); }       <modifier_signature>
%destructor { free($$.name); ast_free_expr_list($$.args); } <ident_suffix>
%destructor { ast_free_expr($$.reset_expr); }          <field_policy>
%destructor { AstServerItemList one = ast_server_item_list_append(ast_server_item_list_empty(), $$); ast_free_server_item_list(one); } <server_item>
%destructor { ast_free_server_item_list($$); }         <server_item_list>

/* Bison also treats THE START SYMBOL AS DISCARDED WHEN THE PARSE SUCCEEDS: on
 * YYACCEPT the cleanup loop pops the whole stack, `program` included. Its value
 * has already been handed to ctx->parsed_program (and thence to the caller's
 * out_program), so letting the <stmt_list> destructor above run on it frees the
 * finished AST out from under the evaluator. This per-symbol destructor overrides
 * the per-type one for `program` alone and deliberately does nothing — ownership
 * moved to ctx. `program` is only ever on the stack after its own reduction, i.e.
 * one step from accepting, so exempting it leaks nothing on the error paths. */
%destructor { (void) $$; }                             program

%%

program
    : statement_list { ctx->parsed_program = $1; $$ = $1; }
    ;

statement_list
    : %empty { $$ = ast_stmt_list_empty(); }
    | statement_list NEWLINE { $$ = $1; }
    | statement_list statement { $$ = ast_stmt_list_append($1, $2); }
    ;

statement
    : assignment NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | print_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | call_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | with_lock_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | for_each_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | while_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | do_loop_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | consider_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | function_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | modifier_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | program_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | library_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | use_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | watch_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | server_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | unwatch_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | without_watchers_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | on_error_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | error_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | return_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | label_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | goto_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | gosub_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | break_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | continue_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | if_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    /* The QBasic refusal, now sited where it MEANS something: `dim` starting a
     * statement. Message byte-identical to the one yylex used to emit, so the
     * negative goldens do not move. Stated in both statement positions -- a
     * `dim` inside a `consider` body used to get this advice too, and losing
     * it there would be a regression dressed as a fix. */
    | DIM {
        $$ = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, @1.first_line, @1.first_column,
                            @1.last_line, @1.last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
    ;

assignment
    : lvalue OP_EQ expression { $$ = ast_assign($1, ast_modifier_none(), $3); }
    /* `x op= e` is exactly `x = x op e`, inheriting every type rule and every
     * refusal the operator already has -- `a += [1]` fails the way
     * `a = a + [1]` does. The operator rides the assign node rather than being
     * desugared here, so the target is built once and evaluated once. */
    | lvalue compound_op expression { $$ = ast_assign_op($1, ast_modifier_none(), $3, $2); }
    | lvalue comparison_lens compound_op expression {
        if (!is_modifier_target_expr($1)) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        $$ = ast_assign_op($1, $2, $4, $3);
      }
    /* PLAT-BRACE: the assignment clause in braces. A brace cannot open a call,
     * so unlike the paren spelling this needs no lookahead to recognize --
     * which is the whole reason the paren form was retired. */
    | lvalue comparison_lens OP_EQ expression {
        if (!is_modifier_target_expr($1)) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        $$ = ast_assign($1, $2, $4);
      }
    ;

compound_op
    : PLUS_EQ  { $$ = '+'; }
    | MINUS_EQ { $$ = '-'; }
    | STAR_EQ  { $$ = '*'; }
    | SLASH_EQ { $$ = '/'; }
    ;

lvalue
    : variable_name %prec NO_DOT { $$ = expr_at(ast_ident($1), @1.first_line, @1.first_column); }
    | lvalue LBRACKET expression RBRACKET %prec NO_DOT { $$ = expr_at(ast_index($1, $3), @2.first_line, @2.first_column); }
    | lvalue DOT dot_field_name %prec NO_DOT { $$ = expr_at(ast_field($1, $3), @2.first_line, @2.first_column); }
    ;

variable_name
    : IDENT %prec NO_DOT { $$ = $1; }
    | END %prec NO_DOT { $$ = copy_const("end"); }
    | NEXT %prec NO_DOT { $$ = copy_const("next"); }
    /* `end` and `next` never START a statement -- they only close one -- so
     * they stay usable as ordinary names. `until` was here too until
     * 2026-08-27, and could not stay: once `do ... until c` closes the loop
     * without `loop` in front, `until` IS statement-initial, and it collides
     * with `until[0] = 5` and `until{USD} = 9.95` on the `[`/`{` lookahead.
     * That is the price of dropping `loop`, and it was paid deliberately.
     * `loop` itself is no longer a keyword in any position -- see lexer.c. */
    ;

comparison_lens
    : LBRACE { lexer_begin_lens_content(ctx->active_lexer); } LENS_CONTENT RBRACE {
        $$ = parse_modifier_use($3);
      }
    ;

modifier_name
    : modifier_word { $$ = $1; }
    | modifier_name modifier_word { $$ = join_words($1, $2); }
    ;

modifier_word
    : IDENT { $$ = $1; }
    | TO { $$ = copy_const("to"); }
    | END { $$ = copy_const("end"); }
    | NEXT { $$ = copy_const("next"); }
    ;

print_statement
    : PRINT expression { $$ = ast_print($2); }
      /* PLAT-STDERR: the same statement with a named destination. `to` cannot
       * begin an expression, so the two alternatives are distinguishable at the
       * token right after PRINT and no conflict is introduced. Both existing
       * keywords are reused, so no identifier that was legal before stops being
       * legal now. */
    | PRINT TO ERROR_VALUE expression { $$ = ast_print_error($4); }
    ;

call_statement
    : IDENT LPAREN argument_list_opt RPAREN { $$ = ast_expr_stmt(ast_call($1, $3)); }
    | QUALIFIED_IDENT LPAREN argument_list_opt RPAREN {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident($1, &library, &name);
        $$ = ast_expr_stmt(ast_qualified_call(library, name, $3));
      }
    | lvalue DOT IDENT LPAREN argument_list_opt RPAREN {
        /* Bare chained-method-call statement with an lvalue receiver ending in a
         * plain IDENT method (e.g. a[0].show()). */
        $$ = ast_expr_stmt(expr_at(ast_method_call($1, $3, $5), @2.first_line, @2.first_column));
      }
    | lvalue DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN {
        /* Bare chained-method-call statement where the lexer folded the trailing
         * `field.method(` into one QUALIFIED_IDENT (e.g. holder.widget.present()). */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident($3, &field, &method);
        AstExpr *recv = expr_at(ast_field($1, field), @2.first_line, @2.first_column);
        $$ = ast_expr_stmt(expr_at(ast_method_call(recv, method, $5), @2.first_line, @2.first_column));
      }
    | ERROR_VALUE DOT IDENT LPAREN argument_list_opt RPAREN {
        size_t length = strlen("error.") + strlen($3);
        char *name = malloc(length + 1);
        if (!name) {
            abort();
        }
        snprintf(name, length + 1, "error.%s", $3);
        free($3);
        $$ = ast_expr_stmt(ast_call(name, $5));
      }
    ;

with_lock_statement
    : WITH IDENT LPAREN expression RPAREN NEWLINE statement_list END WITH NEWLINE {
        if (strcmp($2, "lock") != 0) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "expected lock in with lock block");
            free($2);
            $2 = NULL;
            YYERROR;
        }
        free($2);
        $$ = ast_with_lock($4, $7);
      }
    ;

/* PLAT-NEXT: BASIC's own terminator, beside the house `end <opener>` form.
 * Both spellings stay -- 410 `end for` sites do not move, and `next` costs
 * nothing to admit: it is not statement-initial, so it stays usable as an
 * ordinary variable name (see `variable_name`). Measured: 0 new conflicts.
 *
 * The optional name must MATCH the loop it closes. Classic BASIC let `next x`
 * close an inner `y` loop by implicitly closing both, which turns a one-letter
 * typo into a silent change of program structure; here it is refused. */
for_end
    : END FOR NEWLINE            { $$ = NULL; }
    | NEXT NEWLINE               { $$ = NULL; }
    | NEXT variable_name NEWLINE { $$ = $2; }
    ;

for_each_statement
    : FOR IDENT IN expression NEWLINE statement_list for_end {
        if (!for_end_matches(ctx, $2, $7, @7.first_line, @7.first_column)) { YYERROR; }
        $$ = ast_for_each($2, $4, $6);
      }
    | FOR EACH IDENT IN expression NEWLINE statement_list for_end {
        if (!for_end_matches(ctx, $3, $8, @8.first_line, @8.first_column)) { YYERROR; }
        $$ = ast_for_each($3, $5, $7);
      }
    /* Counted loop. `to` is INCLUSIVE, as every BASIC has had it, and `step`
     * defaults to 1. Distinguished from `for each`/`for ... in` by the token
     * after the name -- OP_EQ here, IN there -- so one lookahead settles it. */
    | FOR IDENT OP_EQ expression TO expression NEWLINE statement_list for_end {
        if (!for_end_matches(ctx, $2, $9, @9.first_line, @9.first_column)) { YYERROR; }
        $$ = ast_for_range($2, $4, $6, NULL, $8);
      }
    | FOR IDENT OP_EQ expression TO expression STEP expression NEWLINE statement_list for_end {
        if (!for_end_matches(ctx, $2, $11, @11.first_line, @11.first_column)) { YYERROR; }
        $$ = ast_for_range($2, $4, $6, $8, $10);
      }
    ;

do_loop_statement
    /* POST-test loop: the body always runs at least once. `while ... end while`
     * already covers the pre-test case, so only this one was missing.
     *
     * ONE FORM, and `until` is a STOP condition. A continue-condition spelling
     * was carried until 2026-08-27 and removed, for two reasons that turned out
     * to be the same reason. It is redundant -- `loop while c` is `until not c`,
     * and gBASIC has `!<`/`!>` for the single-comparison case. And it FORCED THE
     * `loop` KEYWORD: `do ... while c` is ambiguous with a body whose next
     * statement is a nested `while c ... end while`, because both readings are
     * complete programs and the `end while` that separates them can be
     * arbitrarily far ahead. Measured: 32 reduce/reduce conflicts, and dropping
     * the until form does not help -- the ambiguity is with the nested
     * statement, not with the other terminator. `until` never begins a
     * statement, so it needs no opening keyword and `loop` is gone. */
    : DO NEWLINE statement_list UNTIL expression NEWLINE {
        $$ = ast_do_loop($3, $5);
      }
    ;

while_statement
    : WHILE expression NEWLINE statement_list END WHILE NEWLINE {
        $$ = ast_while($2, $4);
      }
    ;

consider_statement
    : CONSIDER expression NEWLINE consider_branch_list consider_else_opt END_CONSIDER NEWLINE {
        $$ = ast_consider($2, $4, $5);
      }
    ;

consider_branch_list
    : CONSIDER_IF expression THEN NEWLINE consider_statement_list {
        $$ = ast_consider_branch_list_append(ast_consider_branch_list_empty(), $2, $5);
      }
    | consider_branch_list CONSIDER_IF expression THEN NEWLINE consider_statement_list {
        $$ = ast_consider_branch_list_append($1, $3, $6);
      }
    ;

consider_else_opt
    : %empty { $$ = ast_stmt_list_empty(); }
    | CONSIDER_ELSE NEWLINE consider_statement_list { $$ = $3; }
    ;

consider_statement_list
    : %empty { $$ = ast_stmt_list_empty(); }
    | consider_statement_list NEWLINE { $$ = $1; }
    | consider_statement_list consider_body_statement { $$ = ast_stmt_list_append($1, $2); }
    ;

consider_body_statement
    : assignment NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | print_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | call_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | with_lock_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | for_each_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | while_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | do_loop_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | consider_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | function_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | modifier_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | program_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | library_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | use_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | watch_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | unwatch_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | without_watchers_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | on_error_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | error_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | return_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | label_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | goto_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | gosub_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | break_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | continue_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | if_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    /* The QBasic refusal, now sited where it MEANS something: `dim` starting a
     * statement. Message byte-identical to the one yylex used to emit, so the
     * negative goldens do not move. Stated in both statement positions -- a
     * `dim` inside a `consider` body used to get this advice too, and losing
     * it there would be a regression dressed as a fix. */
    | DIM {
        $$ = NULL;      /* never read: YYERROR unwinds. Set so bison does not
                         * report an unset value and grow the warning list. */
        report_syntax_error(ctx, @1.first_line, @1.first_column,
                            @1.last_line, @1.last_column,
                            "`dim` is not a gBASIC statement; assign to create a variable (x = 0)");
        YYERROR;
      }
    ;

function_statement
    : FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE {
        $$ = ast_function($2, $4, $7);
      }
    | FUNCTION QUALIFIED_IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE {
        /* Dotted name: define-and-attach sugar. ast_function splits obj.method. */
        $$ = ast_function($2, $4, $7);
      }
    ;

modifier_statement
    : MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE {
        $$ = ast_modifier($2.name, $2.params, $4, 0, $6);
      }
    | EXPORT MODIFIER modifier_signature FOR modifier_context NEWLINE statement_list END MODIFIER NEWLINE {
        $$ = ast_modifier($3.name, $3.params, $5, 1, $7);
      }
    ;

program_statement
    : PROGRAM IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END PROGRAM NEWLINE {
        $$ = ast_program($2, $4, $7);
      }
    ;

library_statement
    : LIBRARY IDENT NEWLINE statement_list END LIBRARY NEWLINE {
        $$ = ast_library($2, $4);
      }
    ;

use_statement
    : USE IDENT { $$ = ast_use($2, NULL); }
    | LOAD IDENT { $$ = ast_use($2, NULL); }
    | USE STRING { $$ = ast_use($2, NULL); }
    | LOAD STRING { $$ = ast_use($2, NULL); }
    | USE IDENT IDENT STRING {
        if (strcmp($3, "from") != 0) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "expected from in use statement");
            free($2);
            free($3);
            free($4);
            $2 = NULL;
            $3 = NULL;
            $4 = NULL;
            YYERROR;
        }
        free($3);
        $$ = ast_use($2, $4);
      }
    | LOAD IDENT IDENT STRING {
        if (strcmp($3, "from") != 0) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "expected from in load statement");
            free($2);
            free($3);
            free($4);
            $2 = NULL;
            $3 = NULL;
            $4 = NULL;
            YYERROR;
        }
        free($3);
        $$ = ast_use($2, $4);
      }
    ;

modifier_signature
    : modifier_name { $$ = ast_modifier_signature($1, ast_name_list_empty()); }
    | modifier_name LPAREN parameter_list_opt RPAREN { $$ = ast_modifier_signature($1, $3); }
    ;

modifier_context
    : IDENT { $$ = $1; }
    ;

watch_statement
    : WATCH LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE {
        $$ = ast_watch(NULL, $3, $6);
      }
    | WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE {
        $$ = ast_watch(NULL, $2, $4);
      }
    /* The NAMED form: `watch doubler(a, b)` registers the watcher AND binds
     * `doubler` to a first-class watcher value. After `WATCH IDENT` the
     * lookahead decides: '(' can only continue this form (a target path never
     * contains one), so bison's default shift resolves the overlap with the
     * paren-free anonymous form correctly. */
    | WATCH IDENT LPAREN watch_target_list RPAREN NEWLINE statement_list END WATCH NEWLINE {
        $$ = ast_watch($2, $4, $7);
      }
    ;

unwatch_statement
    : UNWATCH expression { $$ = ast_unwatch($2); }
    ;

watch_target_list
    : watch_target_path { $$ = ast_name_list_append(ast_name_list_empty(), $1); }
    | watch_target_list COMMA watch_target_path { $$ = ast_name_list_append($1, $3); }
    ;

/* PLAT-WEB-5: the declarative server block (docs/plat-web-design-draft.md §2).
 *
 * ZERO reserved words -- one better than the draft's "exactly one". The head is
 * a generic IDENT IDENT production ("server" is checked by the load-time
 * validation pass, not the grammar), because by the time this grammar was
 * written the layer beneath the sugar had made `server` load-bearing
 * vocabulary: `server = webserver.listen(...)` appears in stdlib/web.bas and
 * thirty-odd fixtures, and a reserved word would have broken all of it. The
 * body words -- verbs, directives, `web`, hook names -- are ordinary IDENTs
 * validated semantically (draft §3), so adding PUT or a websocket kind never
 * touches this file. `on` rides the token the language already reserves.
 *
 * Head options are a record_field_list, and the validation pass restricts the
 * VALUES to literals: that restriction is what makes every §8 load-time check
 * (duplicate host, port collision, worker count) statically decidable. */
server_statement
    : IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE {
        $$ = ast_server($1, $2, $4, $7, $9);
      }
    | IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE {
        $$ = ast_server($1, $2, ast_record_field_list_empty(), $6, $8);
      }
    ;

server_item_list
    : %empty { $$ = ast_server_item_list_empty(); }
    | server_item_list NEWLINE { $$ = $1; }
    | server_item_list server_item { $$ = ast_server_item_list_append($1, $2); }
    ;

server_item
    : IDENT server_string_list NEWLINE {
        $$ = ast_server_directive($1, $2, @1.first_line, @1.first_column);
      }
    | IDENT STRING LPAREN parameter_list_opt RPAREN NEWLINE statement_list END IDENT NEWLINE {
        $$ = ast_server_handler($1, $2, $4, $7, $9, @1.first_line, @1.first_column);
      }
    | IDENT IDENT LPAREN record_field_list RPAREN NEWLINE server_item_list END IDENT NEWLINE {
        $$ = ast_server_site($1, $2, $4, $7, $9, @1.first_line, @1.first_column);
      }
    | IDENT IDENT LPAREN RPAREN NEWLINE server_item_list END IDENT NEWLINE {
        $$ = ast_server_site($1, $2, ast_record_field_list_empty(), $6, $8, @1.first_line, @1.first_column);
      }
    | ON IDENT NEWLINE statement_list END ON NEWLINE {
        $$ = ast_server_hook($2, $4, @1.first_line, @1.first_column);
      }
    ;

server_string_list
    : STRING { $$ = ast_name_list_append(ast_name_list_empty(), $1); }
    | server_string_list COMMA STRING { $$ = ast_name_list_append($1, $3); }
    ;

watch_target_path
    : variable_name { $$ = $1; }
    | watch_target_path DOT IDENT { $$ = join_watch_path($1, $3); }
    ;

without_watchers_statement
    : WITHOUT WATCHERS NEWLINE statement_list END WITHOUT NEWLINE {
        $$ = ast_without_watchers($4);
      }
    ;

on_error_statement
    : ON ERROR_VALUE GOTO IDENT { $$ = ast_on_error_goto($4); }
    | ON ERROR_VALUE GOTO NEXT { $$ = ast_on_error_goto_next(); }
    | ON ERROR_VALUE STOP { $$ = ast_on_error_stop(); }
    | ON IDENT GOTO NEXT {
        if (!warn_channel_ok(ctx, $2, @2.first_line, @2.first_column)) { YYERROR; }
        free($2);
        $$ = ast_on_warning(WARN_MODE_NEXT);
      }
    | ON IDENT GOTO IDENT {
        /* A warning fires from a statement that SUCCEEDED, so a label jump
         * would mean leaving successful code on an advisory signal. Refused
         * BY NAME rather than as a bare syntax error. */
        if (!warn_channel_ok(ctx, $2, @2.first_line, @2.first_column)) { YYERROR; }
        report_diag(ctx, GB_DIAG_PARSE_ERROR, @3.first_line, @3.first_column,
                    @3.first_line, @3.first_column,
                    "on warning has no goto-label form: a warning does not abandon "
                    "its statement, so there is nothing to jump away from "
                    "(use goto next, stop, ignore or print)");
        free($2); free($4);
        YYERROR;
      }
    | ON IDENT STOP {
        if (!warn_channel_ok(ctx, $2, @2.first_line, @2.first_column)) { YYERROR; }
        free($2);
        $$ = ast_on_warning(WARN_MODE_STOP);
      }
    | ON IDENT PRINT {
        if (!warn_channel_ok(ctx, $2, @2.first_line, @2.first_column)) { YYERROR; }
        free($2);
        $$ = ast_on_warning(WARN_MODE_PRINT);
      }
    | ON IDENT IDENT {
        if (!warn_channel_ok(ctx, $2, @2.first_line, @2.first_column)) { YYERROR; }
        int mode = warn_mode_word(ctx, $3, @3.first_line, @3.first_column);
        if (mode < 0) { free($2); free($3); YYERROR; }
        free($2); free($3);
        $$ = ast_on_warning(mode);
      }
    ;

error_statement
    : ERROR_VALUE expression { $$ = ast_error($2); }
    ;

return_statement
    : RETURN { $$ = ast_return(NULL); }
    | RETURN expression { $$ = ast_return($2); }
    ;

label_statement
    : variable_name COLON { $$ = ast_label($1); }
    ;

goto_statement
    /* `variable_name`, not IDENT, so a label may be any word a variable may be
     * -- `end`, `next`, `loop`, `until`. `label_statement` already used it, so
     * `loop:` parsed while `goto loop` did not, which stdlib/dates.bas found. */
    : GOTO variable_name { $$ = ast_goto($2); }
    ;

gosub_statement
    : GOSUB variable_name { $$ = ast_gosub($2); }
    ;

/* An optional loop name: `break x` leaves the loop whose variable is x,
 * `continue x` starts its next iteration. This is the capability classic
 * BASIC got from `next x` popping a runtime FOR stack -- spelled on the two
 * keywords that already exist, because `next x` would be ambiguous with the
 * terminator above and cannot express `break` at all. */
break_statement
    : BREAK { $$ = ast_break(NULL); }
    | BREAK IDENT { $$ = ast_break($2); }
    ;

continue_statement
    : CONTINUE { $$ = ast_continue(NULL); }
    | CONTINUE IDENT { $$ = ast_continue($2); }
    ;

if_statement
    : IF expression THEN NEWLINE statement_list if_block_tail {
        $$ = ast_if($2, $5);
        $$->as.if_stmt.else_body = $6;
      }
    | IF expression THEN inline_statement NEWLINE if_inline_tail {
        $$ = ast_if($2, ast_stmt_list_append(ast_stmt_list_empty(), $4));
        $$->as.if_stmt.else_body = $6;
      }
    ;

if_block_tail
    : END IF NEWLINE {
        $$ = ast_stmt_list_empty();
      }
    | ELSE inline_statement NEWLINE {
        $$ = ast_stmt_list_append(ast_stmt_list_empty(), $2);
      }
    | ELSE NEWLINE statement_list END IF NEWLINE {
        $$ = $3;
      }
    /* `else if` -- a chain closed by ONE `end if`, because the tail recurses
     * rather than nesting a whole if_statement (which would demand an `end if`
     * per rung). Desugars to exactly the nested form a reader would otherwise
     * write by hand, so the evaluator needs no new node and `--ast` shows the
     * nesting that is really there. `inline_statement` does not include `if`,
     * so `ELSE IF` cannot be confused with `ELSE inline_statement`. */
    | ELSE IF expression THEN NEWLINE statement_list if_block_tail {
        AstStmt *inner = ast_if($3, $6);
        inner->as.if_stmt.else_body = $7;
        $$ = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, @2.first_line, @2.first_column,
                                      @2.last_line, @2.last_column));
      }
    ;

if_inline_tail
    : %empty %prec IF_WITHOUT_ELSE {
        $$ = ast_stmt_list_empty();
      }
    | ELSE inline_statement NEWLINE {
        $$ = ast_stmt_list_append(ast_stmt_list_empty(), $2);
      }
    | ELSE NEWLINE statement_list END IF NEWLINE {
        $$ = $3;
      }
    /* `else if` after an INLINE consequent, so a chain that began inline can
     * continue that way. The rung itself may be inline or a block; the two
     * productions differ only in which tail closes them. */
    | ELSE IF expression THEN inline_statement NEWLINE if_inline_tail {
        AstStmt *inner = ast_if($3, ast_stmt_list_append(ast_stmt_list_empty(), $5));
        inner->as.if_stmt.else_body = $7;
        $$ = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, @2.first_line, @2.first_column,
                                      @2.last_line, @2.last_column));
      }
    | ELSE IF expression THEN NEWLINE statement_list if_block_tail {
        AstStmt *inner = ast_if($3, $6);
        inner->as.if_stmt.else_body = $7;
        $$ = ast_stmt_list_append(ast_stmt_list_empty(),
                 ast_stmt_span(inner, @2.first_line, @2.first_column,
                                      @2.last_line, @2.last_column));
      }
    ;

inline_statement
    : assignment { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | print_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | call_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | use_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | on_error_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | error_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | return_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | goto_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | gosub_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | break_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | continue_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    ;

expression
    : or_expression { $$ = $1; }
    ;

or_expression
    : and_expression { $$ = $1; }
    | or_expression OR and_expression { $$ = expr_at(ast_binary(copy_const("or"), ast_modifier_none(), $1, $3), @2.first_line, @2.first_column); }
    ;

and_expression
    : comparison_expression { $$ = $1; }
    | and_expression AND comparison_expression { $$ = expr_at(ast_binary(copy_const("and"), ast_modifier_none(), $1, $3), @2.first_line, @2.first_column); }
    ;

comparison_expression
    : additive_expression { $$ = $1; }
    | additive_expression comparison_operator additive_expression { $$ = expr_at(ast_binary($2, ast_modifier_none(), $1, $3), @2.first_line, @2.first_column); }
    | additive_expression comparison_lens comparison_operator additive_expression {
        $$ = expr_at(ast_binary($3, $2, $1, $4), @3.first_line, @3.first_column);
      }
    ;

additive_expression
    : multiplicative_expression { $$ = $1; }
    | additive_expression PLUS multiplicative_expression { $$ = expr_at(ast_binary(copy_const("+"), ast_modifier_none(), $1, $3), @2.first_line, @2.first_column); }
    | additive_expression MINUS multiplicative_expression { $$ = expr_at(ast_binary(copy_const("-"), ast_modifier_none(), $1, $3), @2.first_line, @2.first_column); }
    ;

multiplicative_expression
    : unary_expression { $$ = $1; }
    | multiplicative_expression STAR unary_expression { $$ = expr_at(ast_binary(copy_const("*"), ast_modifier_none(), $1, $3), @2.first_line, @2.first_column); }
    | multiplicative_expression SLASH unary_expression { $$ = expr_at(ast_binary(copy_const("/"), ast_modifier_none(), $1, $3), @2.first_line, @2.first_column); }
    ;

unary_expression
    : postfix_expression { $$ = $1; }
    | NOT unary_expression { $$ = expr_at(ast_unary(copy_const("not"), $2), @1.first_line, @1.first_column); }
    | MINUS unary_expression { $$ = expr_at(ast_unary(copy_const("-"), $2), @1.first_line, @1.first_column); }
    | NEW postfix_expression { $$ = expr_at(ast_new($2, NULL), @1.first_line, @1.first_column); }
    | NEW postfix_expression WITH record_literal { $$ = expr_at(ast_new($2, $4), @1.first_line, @1.first_column); }
    | SPAWN IDENT LPAREN argument_list_opt RPAREN { $$ = expr_at(ast_spawn($2, $4), @1.first_line, @1.first_column); }
    ;

postfix_expression
    : primary { $$ = $1; }
    | postfix_expression LBRACKET expression RBRACKET { $$ = expr_at(ast_index($1, $3), @2.first_line, @2.first_column); }
    | postfix_expression DOT dot_field_name { $$ = expr_at(ast_field($1, $3), @2.first_line, @2.first_column); }
    | postfix_expression DOT IDENT LPAREN argument_list_opt RPAREN {
        /* Method call on an expression receiver where the method name is a bare
         * IDENT (the receiver ends in ) or ], e.g. make().show(), a[0].show()). */
        $$ = expr_at(ast_method_call($1, $3, $5), @2.first_line, @2.first_column);
      }
    | postfix_expression DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN {
        /* Method call on an expression receiver where the lexer folded the final
         * `field.method(` into one QUALIFIED_IDENT (e.g. a.b.method(): the
         * `b.method` is a QUALIFIED_IDENT following `a DOT`). Split it: the field
         * extends the receiver, the tail is the method name. */
        char *field = NULL;
        char *method = NULL;
        split_qualified_ident($3, &field, &method);
        AstExpr *recv = expr_at(ast_field($1, field), @2.first_line, @2.first_column);
        $$ = expr_at(ast_method_call(recv, method, $5), @2.first_line, @2.first_column);
      }
    ;

comparison_operator
    : OP_EQ { $$ = copy_const("="); }
    | OP_NE { $$ = copy_const("!="); }
    | OP_GT { $$ = copy_const(">"); }
    | OP_LT { $$ = copy_const("<"); }
    | OP_GE { $$ = copy_const(">="); }
    | OP_LE { $$ = copy_const("<="); }
    | OP_NGT { $$ = copy_const("!>"); }
    | OP_NLT { $$ = copy_const("!<"); }
    | OP_NGE { $$ = copy_const("!>="); }
    | OP_NLE { $$ = copy_const("!<="); }
    ;

primary
    : NUMBER { $$ = expr_at(ast_number($1), @1.first_line, @1.first_column); }
    | WATCHERS LPAREN RPAREN { $$ = expr_at(ast_call(copy_const("watchers"), ast_expr_list_empty()), @1.first_line, @1.first_column); }
    | duration_terms { $$ = expr_at(ast_duration($1), @1.first_line, @1.first_column); }
    | STRING { $$ = expr_at(ast_string($1), @1.first_line, @1.first_column); }
    | variable_name ident_suffix {
        if ($2.kind == IDENT_SUFFIX_CALL) {
            $$ = expr_at(ast_call($1, $2.args), @1.first_line, @1.first_column);
        } else if ($2.kind == IDENT_SUFFIX_FIELD) {
            $$ = expr_at(ast_field(expr_at(ast_ident($1), @1.first_line, @1.first_column), $2.name), @2.first_line, @2.first_column);
        } else if ($2.kind == IDENT_SUFFIX_QUALIFIED_CALL) {
            $$ = expr_at(ast_qualified_call($1, $2.name, $2.args), @2.first_line, @2.first_column);
        } else if ($2.kind == IDENT_SUFFIX_METHOD) {
            char *field = NULL;
            char *method = NULL;
            split_qualified_ident($2.name, &field, &method);
            AstExpr *recv = expr_at(ast_field(expr_at(ast_ident($1), @1.first_line, @1.first_column), field), @2.first_line, @2.first_column);
            $$ = expr_at(ast_method_call(recv, method, $2.args), @2.first_line, @2.first_column);
        } else {
            $$ = expr_at(ast_ident($1), @1.first_line, @1.first_column);
        }
      }
    | QUALIFIED_IDENT LPAREN argument_list_opt RPAREN {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident($1, &library, &name);
        $$ = expr_at(ast_qualified_call(library, name, $3), @1.first_line, @1.first_column);
      }
    | ERROR_VALUE { $$ = expr_at(ast_ident(copy_const("error")), @1.first_line, @1.first_column); }
    | TRUE { $$ = expr_at(ast_bool(1), @1.first_line, @1.first_column); }
    | FALSE { $$ = expr_at(ast_bool(0), @1.first_line, @1.first_column); }
    | NOTHING { $$ = expr_at(ast_null(), @1.first_line, @1.first_column); }
    | UNKNOWN_VALUE { $$ = expr_at(ast_unknown(), @1.first_line, @1.first_column); }
    | LPAREN expression RPAREN { $$ = $2; }
    | LBRACKET optional_newlines RBRACKET { $$ = expr_at(ast_array(ast_expr_list_empty()), @1.first_line, @1.first_column); }
    | LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET { $$ = expr_at(ast_array($3), @1.first_line, @1.first_column); }
    | record_literal { $$ = $1; }
    ;

record_literal
    : LBRACE optional_newlines RBRACE { $$ = expr_at(ast_record(ast_record_field_list_empty()), @1.first_line, @1.first_column); }
    | LBRACE optional_newlines record_field_list optional_newlines RBRACE { $$ = expr_at(ast_record($3), @1.first_line, @1.first_column); }
    ;

ident_suffix
    : %empty %prec NO_DOT {
        $$.kind = IDENT_SUFFIX_NONE;
        $$.name = NULL;
        $$.args = ast_expr_list_empty();
      }
    | LPAREN argument_list_opt RPAREN {
        $$.kind = IDENT_SUFFIX_CALL;
        $$.name = NULL;
        $$.args = $2;
      }
    | DOT dot_field_name ident_dot_suffix {
        /* dot_field_name, not IDENT: a keyword is a legal FIELD name after a
         * dot, because nothing but a name can appear there. */
        $$ = $3;
        $$.name = $2;
      }
    | DOT QUALIFIED_IDENT LPAREN argument_list_opt RPAREN {
        /* var.field.method(args): the lexer folds the trailing `field.method(` into
         * one QUALIFIED_IDENT, so after `var DOT` we see it directly. This is the
         * first-dot case that the postfix `DOT QUALIFIED_IDENT` rule cannot reach
         * (the variable_name/ident_suffix path claims the first dot). */
        $$.kind = IDENT_SUFFIX_METHOD;
        $$.name = $2;
        $$.args = $4;
      }
    ;

ident_dot_suffix
    : %empty {
        $$.kind = IDENT_SUFFIX_FIELD;
        $$.name = NULL;
        $$.args = ast_expr_list_empty();
      }
    | LPAREN argument_list_opt RPAREN {
        $$.kind = IDENT_SUFFIX_QUALIFIED_CALL;
        $$.name = NULL;
        $$.args = $2;
      }
    ;

duration_terms
    : NUMBER IDENT {
        AstDuration duration = {0};
        $$ = duration_add_unit(duration, $1, $2);
      }
    | duration_terms NUMBER IDENT {
        $$ = duration_add_unit($1, $2, $3);
      }
    ;

argument_list_opt
    : %empty { $$ = ast_expr_list_empty(); }
    | argument_list { $$ = $1; }
    ;

argument_list
    : expression { $$ = ast_expr_list_append(ast_expr_list_empty(), $1); }
    | argument_list COMMA expression { $$ = ast_expr_list_append($1, $3); }
    ;

array_argument_list
    : expression { $$ = ast_expr_list_append(ast_expr_list_empty(), $1); }
    | array_argument_list COMMA optional_newlines expression { $$ = ast_expr_list_append($1, $4); }
    ;

parameter_list_opt
    : %empty { $$ = ast_name_list_empty(); }
    | parameter_list { $$ = $1; }
    ;

/* An OPTIONAL PARAMETER's default is a LITERAL and nothing else -- a number
 * (optionally signed), a string, `true`/`false`, `nothing` or `unknown`.
 *
 * Not an arbitrary expression, and the restriction is semantic rather than
 * grammatical convenience: an expression default has to be evaluated in SOME
 * scope, and gBASIC has no closures, so "can a default see an earlier
 * parameter, or a global" has no comfortable answer -- the read-then-shadow
 * rule run_core.sh guards would apply to it. A literal has nothing to see, so
 * the question does not arise. Going literal -> expression later is possible;
 * the reverse is not. */
parameter_default
    : NUMBER { $$ = expr_at(ast_number($1), @1.first_line, @1.first_column); }
    | MINUS NUMBER { $$ = expr_at(ast_number(-$2), @1.first_line, @1.first_column); }
    | PLUS NUMBER { $$ = expr_at(ast_number($2), @1.first_line, @1.first_column); }
    | STRING { $$ = expr_at(ast_string($1), @1.first_line, @1.first_column); }
    | TRUE { $$ = expr_at(ast_bool(1), @1.first_line, @1.first_column); }
    | FALSE { $$ = expr_at(ast_bool(0), @1.first_line, @1.first_column); }
    | NOTHING { $$ = expr_at(ast_null(), @1.first_line, @1.first_column); }
    | UNKNOWN_VALUE { $$ = expr_at(ast_unknown(), @1.first_line, @1.first_column); }
    ;

parameter_list
    : IDENT { $$ = ast_name_list_append(ast_name_list_empty(), $1); }
    | IDENT OP_EQ parameter_default {
        $$ = ast_name_list_append_default(ast_name_list_empty(), $1, $3);
      }
    | parameter_list COMMA IDENT { $$ = ast_name_list_append($1, $3); }
    | parameter_list COMMA IDENT OP_EQ parameter_default {
        $$ = ast_name_list_append_default($1, $3, $5);
      }
    ;


/* A FIELD NAME may be a keyword.
 *
 * Both places a field name appears are CLOSED CONTEXTS -- immediately before
 * ':' or '=' inside a record literal, and immediately after '.' -- so nothing
 * but a name is legal there and a keyword cannot be anything else. Without
 * this the keyword namespace reaches into the DATA namespace: a mapping spec
 * cannot say `from:` or `as:`, and ends up named by the grammar rather than by
 * meaning. See the 2026-08-12 DOGFOOD entry (c). */
field_name
    : dot_field_name { $$ = $1; }
    /* A QUOTED key admits names an identifier cannot spell -- "content-type",
     * "x y" -- closing a real asymmetry: decode() has always produced records
     * with such keys from JSON, but the literal syntax could not write them.
     * Reading them back is the existing dynamic form, r["content-type"].
     * Deliberately NOT part of dot_field_name: `r."content-type"` would be new
     * syntax nobody asked for, and the bracket form already reads it. */
    | STRING { $$ = $1; }
    ;

/* The keyword list itself, usable wherever a NAME is the only thing legal:
 * a record-literal key, and after a DOT. Both are closed contexts -- nothing
 * but a name can appear -- so admitting a keyword here costs no ambiguity and
 * keeps the KEYWORD namespace out of the DATA namespace. Before this was wired
 * into dot access, `{ end: 1 }` built a field that `r.end` could never read. */
dot_field_name
    : IDENT { $$ = $1; }
    | AS             { $$ = kw_name("as"); }
    | NEXT           { $$ = kw_name("next"); }
    | STOP           { $$ = kw_name("stop"); }
    | ERROR_VALUE    { $$ = kw_name("error"); }
    | END            { $$ = kw_name("end"); }
    | TO             { $$ = kw_name("to"); }
    | IN             { $$ = kw_name("in"); }
    | ON             { $$ = kw_name("on"); }
    | NEW            { $$ = kw_name("new"); }
    | EACH           { $$ = kw_name("each"); }
    | WITH           { $$ = kw_name("with"); }
    | WITHOUT        { $$ = kw_name("without"); }
    | THEN           { $$ = kw_name("then"); }
    | ELSE           { $$ = kw_name("else"); }
    | FOR            { $$ = kw_name("for"); }
    | IF             { $$ = kw_name("if"); }
    | WHILE          { $$ = kw_name("while"); }
    | DO             { $$ = kw_name("do"); }
    | UNTIL          { $$ = kw_name("until"); }
    | PRINT          { $$ = kw_name("print"); }
    | RETURN         { $$ = kw_name("return"); }
    | LOAD           { $$ = kw_name("load"); }
    | USE            { $$ = kw_name("use"); }
    | NOT            { $$ = kw_name("not"); }
    | AND            { $$ = kw_name("and"); }
    | OR             { $$ = kw_name("or"); }
    | TRUE           { $$ = kw_name("true"); }
    | FALSE          { $$ = kw_name("false"); }
    | NOTHING        { $$ = kw_name("nothing"); }
    | BREAK          { $$ = kw_name("break"); }
    | CONTINUE       { $$ = kw_name("continue"); }
    | GOTO           { $$ = kw_name("goto"); }
    | GOSUB          { $$ = kw_name("gosub"); }
    | SPAWN          { $$ = kw_name("spawn"); }
    | EXPORT         { $$ = kw_name("export"); }
    | LIBRARY        { $$ = kw_name("library"); }
    | FUNCTION       { $$ = kw_name("function"); }
    | MODIFIER       { $$ = kw_name("modifier"); }
    | PROGRAM        { $$ = kw_name("program"); }
    | WATCH          { $$ = kw_name("watch"); }
    | WATCHERS       { $$ = kw_name("watchers"); }
    | CONSIDER       { $$ = kw_name("consider"); }
    | STEP           { $$ = kw_name("step"); }
    | UNWATCH        { $$ = kw_name("unwatch"); }
    | UNKNOWN_VALUE  { $$ = kw_name("unknown"); }
    | DIM            { $$ = kw_name("dim"); }
    ;

record_field_list
    : field_name OP_EQ expression { $$ = ast_record_field_list_append(ast_record_field_list_empty(), $1, $3); }
    | field_name COLON expression { $$ = ast_record_field_list_append(ast_record_field_list_empty(), $1, $3); }
    | IDENT LPAREN field_policy RPAREN COLON expression { $$ = ast_record_field_list_append_policy(ast_record_field_list_empty(), $1, $6, $3.policy, $3.reset_expr); }
    | record_field_list COMMA optional_newlines field_name OP_EQ expression { $$ = ast_record_field_list_append($1, $4, $6); }
    | record_field_list COMMA optional_newlines field_name COLON expression { $$ = ast_record_field_list_append($1, $4, $6); }
    | record_field_list COMMA optional_newlines IDENT LPAREN field_policy RPAREN COLON expression { $$ = ast_record_field_list_append_policy($1, $4, $9, $6.policy, $6.reset_expr); }
    ;

/* PBI per-field policy clause. Keywords are contextual (matched as IDENT here),
 * so `copy`/`link`/`reset`/`exclude` remain ordinary identifiers and builtins
 * everywhere else. Only `reset` takes a value; the `)` lookahead is not in
 * FIRST(expression), so the two productions do not conflict. */
field_policy
    : IDENT {
        FieldPolicySpec spec;
        spec.reset_expr = NULL;
        if (strcmp($1, "copy") == 0) {
            spec.policy = AST_FIELD_POLICY_COPY;
        } else if (strcmp($1, "link") == 0) {
            spec.policy = AST_FIELD_POLICY_LINK;
        } else if (strcmp($1, "exclude") == 0) {
            spec.policy = AST_FIELD_POLICY_EXCLUDE;
        } else if (strcmp($1, "reset") == 0) {
            free($1);
            $1 = NULL;
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "reset policy requires a value, e.g. (reset 0)");
            YYERROR;
        } else {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "unknown field policy (expected copy, link, reset, or exclude)");
            free($1);
            $1 = NULL;
            YYERROR;
        }
        free($1);
        $$ = spec;
      }
    | IDENT expression {
        FieldPolicySpec spec;
        if (strcmp($1, "reset") == 0) {
            spec.policy = AST_FIELD_POLICY_RESET;
            spec.reset_expr = $2;
        } else {
            free($1);
            $1 = NULL;
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "only the reset policy takes a value");
            YYERROR;
        }
        free($1);
        $$ = spec;
      }
    ;

optional_newlines
    : %empty
    | optional_newlines NEWLINE
    ;

%%

/* Reentrant parse core: all mutable parser state lives in a stack-allocated
 * gb_parse_ctx, so concurrent parses in one process share nothing. `path` labels
 * diagnostic locations (may be NULL) and `diags` is the sink (NULL => immediate
 * stderr via gb_report_to). This is the entry point gb_parse (frontend.c) uses. */
int parse_source_reentrant(const char *source, const char *path,
                           gb_diagnostics *diags, AstStmtList *out_program) {
    gb_parse_ctx ctx;
    ctx.active_lexer = NULL;
    ctx.lexer_error_reported = 0;
    ctx.active_parse_path = path;
    ctx.parsed_program = ast_stmt_list_empty();
    ctx.diags = diags;
    ctx.la_line = 0;
    ctx.la_column = 0;
    ctx.la_end_line = 0;
    ctx.la_end_column = 0;

    Lexer lexer;
    lexer_init(&lexer, source);
    ctx.active_lexer = &lexer;

    int result = yyparse(&ctx);
    if (result != 0) {
        return result;
    }
    /* A diagnostic reported from yylex must fail the parse even when bison
     * ACCEPTED. yylex signals such a token by returning 0 -- end of file -- and
     * bison cannot tell that from a real one, so wherever the grammar allows a
     * program to end (top level, notably) it reduces the truncated prefix and
     * reports success. The file then ran up to the bad token and exited 0.
     * lexer_error_reported is the only evidence that the EOF was synthetic. */
    if (ctx.lexer_error_reported) {
        return 1;
    }

    *out_program = ctx.parsed_program;
    return 0;
}

/* Legacy global-backed shims for the single-threaded CLI paths that still use
 * parse_set_source_path + parse_source: --add-loads (main.c), actor mode
 * (main.c), and eval.c's import loader. The sink comes from the process-global
 * active sink (main.c sets it around eval, so import parse errors are collected
 * and drained); the path from parse_set_source_path. gb_parse bypasses both. */
static const char *legacy_parse_path = NULL;

int parse_source(const char *source, AstStmtList *out_program) {
    return parse_source_reentrant(source, legacy_parse_path,
                                  gb_get_active_sink(), out_program);
}

void parse_set_source_path(const char *path) {
    legacy_parse_path = path;
}

/* Mirror of the former global yyerror location logic, sourced from the per-parse
 * ctx. Both Bison's syntax-error yyerror and the grammar's action-level error
 * reports funnel through here so their output stays byte-identical to the
 * pre-Phase-2 global reporter. */
static void report_syntax_error(gb_parse_ctx *ctx, int line, int column,
                                int end_line, int end_column, const char *message) {
    if (ctx->lexer_error_reported) {
        return;
    }
    if (line <= 0 && ctx->active_lexer) {
        line = ctx->active_lexer->line;
        column = ctx->active_lexer->column;
    }
    if (line <= 0) {
        line = 1;
    }
    if (column <= 0) {
        column = 1;
    }
    /* End of the offending token; fall back to the start if it looks unset or
     * inverted. */
    if (end_line < line || (end_line == line && end_column < column)) {
        end_line = line;
        end_column = column;
    }
    report_diag(ctx, GB_DIAG_PARSE_ERROR, line, column, end_line, end_column, message);
}

static int yylex(YYSTYPE *lvalp, YYLTYPE *llocp, gb_parse_ctx *ctx) {
    Token token = lexer_next(ctx->active_lexer);
    llocp->first_line = token.line;
    llocp->first_column = token.column;
    llocp->last_line = token.line;
    llocp->last_column = token.column + token.length;
    /* Record the lookahead location so action-level error reporting reproduces
     * exactly what the former global yyerror read from the global yylloc. */
    ctx->la_line = token.line;
    ctx->la_column = token.column;
    ctx->la_end_line = token.line;
    ctx->la_end_column = token.column + token.length;

    switch (token.type) {
    case TOKEN_EOF: return 0;
    case TOKEN_IDENT:
        lvalp->text = copy_text(token.start, token.length);
        return IDENT;
    case TOKEN_QUALIFIED_IDENT:
        lvalp->text = copy_text(token.start, token.length);
        return QUALIFIED_IDENT;
    case TOKEN_AS: return AS;
    case TOKEN_NUMBER:
    {
        /* Convert exactly the token's bytes (handles decimal and 0x hex), so a
         * following character can never extend what strtod reads. */
        char numbuf[64];
        size_t nlen = token.length < sizeof(numbuf) - 1 ? token.length : sizeof(numbuf) - 1;
        memcpy(numbuf, token.start, nlen);
        numbuf[nlen] = '\0';
        lvalp->number = strtod(numbuf, NULL);
        return NUMBER;
    }
    case TOKEN_STRING:
    {
        int ok = 0;
        lvalp->text = copy_string_literal(ctx, token.start, token.length, token.line, token.column, &ok);
        if (!ok) {
            ctx->lexer_error_reported = 1;
            return 0;
        }
        return STRING;
    }
    case TOKEN_LENS_CONTENT:
        lvalp->text = copy_text(token.start, token.length);
        return LENS_CONTENT;
    case TOKEN_IF: return IF;
    case TOKEN_CONSIDER_IF: return CONSIDER_IF;
    case TOKEN_THEN: return THEN;
    case TOKEN_ELSE: return ELSE;
    case TOKEN_CONSIDER_ELSE: return CONSIDER_ELSE;
    case TOKEN_END: return END;
    case TOKEN_END_CONSIDER: return END_CONSIDER;
    case TOKEN_PRINT: return PRINT;
    case TOKEN_TRUE: return TRUE;
    case TOKEN_FALSE: return FALSE;
    case TOKEN_NOTHING: return NOTHING;
    case TOKEN_UNKNOWN: return UNKNOWN_VALUE;
    case TOKEN_AND: return AND;
    case TOKEN_OR: return OR;
    case TOKEN_NOT: return NOT;
    case TOKEN_WITH: return WITH;
    case TOKEN_NEW: return NEW;
    case TOKEN_SPAWN: return SPAWN;
    case TOKEN_FOR: return FOR;
    case TOKEN_TO: return TO;
    case TOKEN_STEP: return STEP;
    case TOKEN_DO: return DO;
    case TOKEN_UNTIL: return UNTIL;
    case TOKEN_IN: return IN;
    case TOKEN_EACH: return EACH;
    case TOKEN_WHILE: return WHILE;
    case TOKEN_CONSIDER: return CONSIDER;
    case TOKEN_BREAK: return BREAK;
    case TOKEN_CONTINUE: return CONTINUE;
    case TOKEN_FUNCTION: return FUNCTION;
    case TOKEN_RETURN: return RETURN;
    case TOKEN_GOTO: return GOTO;
    case TOKEN_GOSUB: return GOSUB;
    case TOKEN_WATCH: return WATCH;
    case TOKEN_UNWATCH: return UNWATCH;
    case TOKEN_WITHOUT: return WITHOUT;
    case TOKEN_WATCHERS: return WATCHERS;
    case TOKEN_ON: return ON;
    case TOKEN_PLUS_EQ: return PLUS_EQ;
    case TOKEN_MINUS_EQ: return MINUS_EQ;
    case TOKEN_STAR_EQ: return STAR_EQ;
    case TOKEN_SLASH_EQ: return SLASH_EQ;
    case TOKEN_NEXT: return NEXT;
    case TOKEN_STOP: return STOP;
    case TOKEN_ERROR_VALUE: return ERROR_VALUE;
    case TOKEN_MODIFIER: return MODIFIER;
    case TOKEN_PROGRAM: return PROGRAM;
    case TOKEN_LIBRARY: return LIBRARY;
    case TOKEN_LOAD: return LOAD;
    case TOKEN_USE: return USE;
    case TOKEN_EXPORT: return EXPORT;
    case TOKEN_OP_EQ: return OP_EQ;
    case TOKEN_OP_NE: return OP_NE;
    case TOKEN_OP_GT: return OP_GT;
    case TOKEN_OP_LT: return OP_LT;
    case TOKEN_OP_GE: return OP_GE;
    case TOKEN_OP_LE: return OP_LE;
    case TOKEN_OP_NGT: return OP_NGT;
    case TOKEN_OP_NLT: return OP_NLT;
    case TOKEN_OP_NGE: return OP_NGE;
    case TOKEN_OP_NLE: return OP_NLE;
    case TOKEN_PLUS: return PLUS;
    case TOKEN_MINUS: return MINUS;
    case TOKEN_STAR: return STAR;
    case TOKEN_SLASH: return SLASH;
    case TOKEN_LPAREN:
        /* PLAT-BRACE: `(` means a call or grouping and NOTHING else. The
         * ninety-line lookahead that used to decide between a call and a
         * modifier clause is gone with the paren clause spelling, and with it
         * the residual it could not close (docs/brace_modifier_design.md). */
        return LPAREN;
    case TOKEN_RPAREN: return RPAREN;
    case TOKEN_LBRACKET: return LBRACKET;
    case TOKEN_RBRACKET: return RBRACKET;
    case TOKEN_COMMA: return COMMA;
    case TOKEN_LBRACE: return LBRACE;
    case TOKEN_RBRACE: return RBRACE;
    case TOKEN_DOT: return DOT;
    case TOKEN_COLON: return COLON;
    case TOKEN_NEWLINE: return NEWLINE;
    case TOKEN_ERROR:
        if (ctx->active_lexer->error_message[0]) {
            report_diag_lexeme(ctx, GB_DIAG_LEX_DETAIL, token.line, token.column,
                               token.start, token.length, ctx->active_lexer->error_message);
        } else {
            report_diag_lexeme(ctx, GB_DIAG_LEX_ERROR, token.line, token.column,
                               token.start, token.length, "unexpected token");
        }
        ctx->lexer_error_reported = 1;
        return 0;
    case TOKEN_DIM:
        /* `dim` is lexed as a keyword for ONE reason: to be refused with advice
         * where someone arriving from QBasic would type it. There is no dim
         * statement -- assignment creates a variable -- and reserving the word
         * to say so is worth more than freeing it, because as an ordinary
         * identifier `dim x` would still fail, just less usefully.
         *
         * THE REFUSAL USED TO HAPPEN HERE, at token delivery, which fired it in
         * every position rather than the one it was written for: `{ dim: 7 }`
         * and `r.dim` were both rejected as "not a gBASIC statement" at a
         * position where no statement is possible. Every other keyword is a
         * legal field name (see dot_field_name) and `dim` was the sole
         * exception -- nothing chose that. The token is delivered now and the
         * grammar decides, which is the difference between asking WHAT the
         * word was and asking WHERE it appeared. */
        return DIM;
    default:
        /* Backstop for a token added to the lexer and not to the grammar. It
         * used to fprintf straight to stderr: unlocated, absent from the
         * diagnostics sink, and so under --json-diagnostics a bare line in the
         * middle of a JSON stream. Every diagnostic goes through the sink. */
        report_diag_lexeme(ctx, GB_DIAG_PARSE_ERROR, token.line, token.column,
                           token.start, token.length,
                           "token has no place in the grammar");
        ctx->lexer_error_reported = 1;
        return 0;
    }
}

/* Bison's syntax-error entry point. In the pure parser llocp points at the
 * offending lookahead token's location (what the former global yyerror read from
 * the global yylloc); report_syntax_error applies the shared fallback logic. */
static void yyerror(YYLTYPE *llocp, gb_parse_ctx *ctx, const char *message) {
    report_syntax_error(ctx, llocp->first_line, llocp->first_column,
                        llocp->last_line, llocp->last_column, message);
}

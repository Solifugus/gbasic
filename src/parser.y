%{
#include "ast.h"
#include "builtins.h"
#include "diagnostics.h"
#include "lexer.h"
#include "parse_ctx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Report a diagnostic with an explicit span. Routes through gb_report_to, which
 * pushes to the per-parse sink (ctx->diags) or, when that is NULL, prints in the
 * legacy stderr format. */
static void report_diag(gb_parse_ctx *ctx, gb_diag_code code, int line, int column,
                        int end_line, int end_column, const char *message) {
    gb_span span = { line, column, end_line, end_column };
    gb_report_to(ctx->diags, code, 0, ctx->active_parse_path, span, message);
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

static int ascii_lower(int ch) {
    return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch;
}

static int is_ident_start_char(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

static int is_ident_char(char ch) {
    return is_ident_start_char(ch) || (ch >= '0' && ch <= '9');
}

static int keyword_at(const char *p, const char *keyword) {
    const char *k = keyword;
    while (*k) {
        if (ascii_lower((unsigned char)*p) != *k) {
            return 0;
        }
        p++;
        k++;
    }
    return !is_ident_char(*p);
}

static int source_declares_function(gb_parse_ctx *ctx, const char *name) {
    const char *p = ctx->active_lexer->source;
    size_t name_len = strlen(name);
    int in_string = 0;
    int in_comment = 0;

    while (*p) {
        if (in_comment) {
            if (*p == '\n') {
                in_comment = 0;
            }
            p++;
            continue;
        }
        if (in_string) {
            if (*p == '\\' && p[1]) {
                p += 2;
                continue;
            }
            if (*p == '"') {
                in_string = 0;
            }
            p++;
            continue;
        }
        if (*p == '\'') {
            in_comment = 1;
            p++;
            continue;
        }
        if (*p == '"') {
            in_string = 1;
            p++;
            continue;
        }
        if ((p == ctx->active_lexer->source || !is_ident_char(p[-1])) &&
            keyword_at(p, "function")) {
            p += strlen("function");
            while (*p == ' ' || *p == '\t' || *p == '\r') {
                p++;
            }
            const char *start = p;
            if (!is_ident_start_char(*p)) {
                continue;
            }
            while (is_ident_char(*p)) {
                p++;
            }
            if ((size_t)(p - start) == name_len && strncmp(start, name, name_len) == 0) {
                return 1;
            }
            continue;
        }
        p++;
    }

    return 0;
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

static int modifier_lparen_ahead(gb_parse_ctx *ctx, const char *start) {
    const char *p = start + 1;
    int saw_term = 0;

    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
    }
    while (*p && *p != ')' && *p != '\n') {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_') {
            saw_term = 1;
            while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                   (*p >= '0' && *p <= '9') || *p == '_') {
                p++;
            }
        } else if ((*p >= '0' && *p <= '9') || *p == '"') {
            saw_term = 1;
            if (*p == '"') {
                /* Skip the string. A backslash escapes the next character, so
                 * that `"\""` is one string rather than two — without this, the
                 * scan stops at the ESCAPED quote, reads the real closing quote
                 * as the start of a second string, finds no terminator before
                 * the newline and gives up, and the whole clause silently
                 * degrades to an ordinary parenthesised expression. This is the
                 * first of three string scanners a modifier clause passes
                 * through; the others are modifier_content_token (src/lexer.c)
                 * and modifier_string_literal (src/eval.c). */
                p++;
                while (*p && *p != '"' && *p != '\n') {
                    if (*p == '\\' && p[1] && p[1] != '\n') {
                        p += 2;
                        continue;
                    }
                    p++;
                }
                if (*p != '"') {
                    return 0;
                }
                p++;
            } else {
                while (*p >= '0' && *p <= '9') {
                    p++;
                }
                if (*p == '.') {
                    p++;
                    while (*p >= '0' && *p <= '9') {
                        p++;
                    }
                }
            }
        } else if (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        } else if (*p == ',') {
            return 0;
        } else if (*p == '+' || *p == '-' || *p == '*' || *p == '/' ||
                   *p == '.' || *p == '[' || *p == ']') {
            p++;
        } else {
            return 0;
        }
    }
    if (*p != ')' || !saw_term) {
        return 0;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
    }
    int followed_by_comparison = *p == '=' || *p == '>' || *p == '<' ||
        (*p == '!' && (p[1] == '=' || p[1] == '>' || p[1] == '<'));
    if (!followed_by_comparison) {
        return 0;
    }

    const char *name_end = start;
    while (name_end > ctx->active_lexer->source &&
           (name_end[-1] == ' ' || name_end[-1] == '\t' || name_end[-1] == '\r')) {
        name_end--;
    }
    const char *name_start = name_end;
    while (name_start > ctx->active_lexer->source &&
           ((name_start[-1] >= 'A' && name_start[-1] <= 'Z') ||
            (name_start[-1] >= 'a' && name_start[-1] <= 'z') ||
            (name_start[-1] >= '0' && name_start[-1] <= '9') ||
            name_start[-1] == '_')) {
        name_start--;
    }
    if (name_start < name_end) {
        char *name = copy_text(name_start, (int)(name_end - name_start));
        int is_function = gbasic_builtin_function(name) || source_declares_function(ctx, name);
        free(name);
        if (is_function) {
            return 0;
        }
    }

    return 1;
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
}

%token <number> NUMBER
%token <text> IDENT STRING MOD_CONTENT LENS_CONTENT QUALIFIED_IDENT
%token IF CONSIDER_IF THEN ELSE CONSIDER_ELSE END END_CONSIDER PRINT TRUE FALSE NOTHING UNKNOWN_VALUE AND OR NOT WITH NEW SPAWN FOR TO IN EACH WHILE CONSIDER BREAK CONTINUE FUNCTION RETURN GOTO GOSUB WATCH WITHOUT WATCHERS ON RESUME NEXT STOP ERROR_VALUE MODIFIER PROGRAM LIBRARY LOAD USE EXPORT
%token OP_EQ OP_NE OP_GT OP_LT OP_GE OP_LE OP_NGT OP_NLT OP_NGE OP_NLE
%token PLUS MINUS STAR SLASH LPAREN MOD_LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE COMMA COLON NEWLINE
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
%type <stmt> statement assignment print_statement call_statement with_lock_statement for_each_statement while_statement consider_statement consider_body_statement function_statement modifier_statement program_statement library_statement use_statement return_statement label_statement goto_statement gosub_statement break_statement continue_statement watch_statement without_watchers_statement on_error_statement error_statement if_statement inline_statement
%type <expr> expression or_expression and_expression comparison_expression
%type <expr> additive_expression multiplicative_expression unary_expression postfix_expression primary lvalue record_literal
%type <expr_list> argument_list argument_list_opt array_argument_list
%type <record_field_list> record_field_list
%type <field_policy> field_policy
%type <consider_branch_list> consider_branch_list
%type <name_list> parameter_list parameter_list_opt watch_target_list
%type <modifier> modifier comparison_lens
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
    | consider_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | function_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | modifier_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | program_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | library_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | use_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | watch_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
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
    ;

assignment
    : lvalue OP_EQ expression { $$ = ast_assign($1, ast_modifier_none(), $3); }
    | lvalue modifier OP_EQ expression {
        if (!is_modifier_target_expr($1)) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
        $$ = ast_assign($1, $2, $4);
      }
    ;

lvalue
    : variable_name %prec NO_DOT { $$ = expr_at(ast_ident($1), @1.first_line, @1.first_column); }
    | lvalue LBRACKET expression RBRACKET %prec NO_DOT { $$ = expr_at(ast_index($1, $3), @2.first_line, @2.first_column); }
    | lvalue DOT IDENT %prec NO_DOT { $$ = expr_at(ast_field($1, $3), @2.first_line, @2.first_column); }
    ;

variable_name
    : IDENT %prec NO_DOT { $$ = $1; }
    | END %prec NO_DOT { $$ = copy_const("end"); }
    | NEXT %prec NO_DOT { $$ = copy_const("next"); }
    ;

modifier
    : MOD_LPAREN MOD_CONTENT { $$ = parse_modifier_use($2); }
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

for_each_statement
    : FOR IDENT IN expression NEWLINE statement_list END FOR NEWLINE {
        $$ = ast_for_each($2, $4, $6);
      }
    | FOR EACH IDENT IN expression NEWLINE statement_list END FOR NEWLINE {
        $$ = ast_for_each($3, $5, $7);
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
    | consider_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | function_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | modifier_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | program_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | library_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | use_statement NEWLINE { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
    | watch_statement { $$ = ast_stmt_span($1, @1.first_line, @1.first_column, @1.last_line, @1.last_column); }
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
        $$ = ast_watch($3, $6);
      }
    | WATCH watch_target_list NEWLINE statement_list END WATCH NEWLINE {
        $$ = ast_watch($2, $4);
      }
    ;

watch_target_list
    : watch_target_path { $$ = ast_name_list_append(ast_name_list_empty(), $1); }
    | watch_target_list COMMA watch_target_path { $$ = ast_name_list_append($1, $3); }
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
    | ON ERROR_VALUE RESUME NEXT { $$ = ast_on_error_resume_next(); }
    | ON ERROR_VALUE STOP { $$ = ast_on_error_stop(); }
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
    : GOTO IDENT { $$ = ast_goto($2); }
    ;

gosub_statement
    : GOSUB IDENT { $$ = ast_gosub($2); }
    ;

break_statement
    : BREAK { $$ = ast_break(); }
    ;

continue_statement
    : CONTINUE { $$ = ast_continue(); }
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
    | additive_expression modifier comparison_operator additive_expression {
        if (!is_modifier_target_expr($1)) {
            report_syntax_error(ctx, ctx->la_line, ctx->la_column,
                                ctx->la_end_line, ctx->la_end_column,
                                "modifier target must be a variable, field, or index");
            YYERROR;
        }
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
    | postfix_expression DOT IDENT { $$ = expr_at(ast_field($1, $3), @2.first_line, @2.first_column); }
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
    | DOT IDENT ident_dot_suffix {
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

parameter_list
    : IDENT { $$ = ast_name_list_append(ast_name_list_empty(), $1); }
    | parameter_list COMMA IDENT { $$ = ast_name_list_append($1, $3); }
    ;

record_field_list
    : IDENT OP_EQ expression { $$ = ast_record_field_list_append(ast_record_field_list_empty(), $1, $3); }
    | IDENT COLON expression { $$ = ast_record_field_list_append(ast_record_field_list_empty(), $1, $3); }
    | IDENT LPAREN field_policy RPAREN COLON expression { $$ = ast_record_field_list_append_policy(ast_record_field_list_empty(), $1, $6, $3.policy, $3.reset_expr); }
    | record_field_list COMMA optional_newlines IDENT OP_EQ expression { $$ = ast_record_field_list_append($1, $4, $6); }
    | record_field_list COMMA optional_newlines IDENT COLON expression { $$ = ast_record_field_list_append($1, $4, $6); }
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
    case TOKEN_MOD_CONTENT:
        lvalp->text = copy_text(token.start, token.length);
        return MOD_CONTENT;
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
    case TOKEN_WITHOUT: return WITHOUT;
    case TOKEN_WATCHERS: return WATCHERS;
    case TOKEN_ON: return ON;
    case TOKEN_RESUME: return RESUME;
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
        if (modifier_lparen_ahead(ctx, token.start)) {
            lexer_begin_modifier_content(ctx->active_lexer);
            return MOD_LPAREN;
        }
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
    default:
        fprintf(stderr, "unexpected token %s at %d:%d\n",
                token_type_name(token.type), token.line, token.column);
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

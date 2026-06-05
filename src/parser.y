%{
#include "ast.h"
#include "builtins.h"
#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Lexer *active_lexer;
static int lexer_error_reported;

AstStmtList parsed_program;

static char *copy_text(const char *start, int length) {
    char *text = malloc((size_t)length + 1);
    if (!text) {
        abort();
    }
    memcpy(text, start, (size_t)length);
    text[length] = '\0';
    return text;
}

static char *copy_string_literal(const char *start, int length, int *ok) {
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
                fprintf(stderr, "runtime error: unterminated escape sequence\n");
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
            } else {
                fprintf(stderr, "runtime error: invalid escape sequence: \\%c\n", start[i]);
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

static int source_declares_function(const char *name) {
    const char *p = active_lexer->source;
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
        if ((p == active_lexer->source || !is_ident_char(p[-1])) &&
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

static int modifier_lparen_ahead(const char *start) {
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
                p++;
                while (*p && *p != '"' && *p != '\n') {
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
    while (name_end > active_lexer->source &&
           (name_end[-1] == ' ' || name_end[-1] == '\t' || name_end[-1] == '\r')) {
        name_end--;
    }
    const char *name_start = name_end;
    while (name_start > active_lexer->source &&
           ((name_start[-1] >= 'A' && name_start[-1] <= 'Z') ||
            (name_start[-1] >= 'a' && name_start[-1] <= 'z') ||
            (name_start[-1] >= '0' && name_start[-1] <= '9') ||
            name_start[-1] == '_')) {
        name_start--;
    }
    if (name_start < name_end) {
        char *name = copy_text(name_start, (int)(name_end - name_start));
        int is_function = gbasic_builtin_function(name) || source_declares_function(name);
        free(name);
        if (is_function) {
            return 0;
        }
    }

    return 1;
}

static int yylex(void);
static void yyerror(const char *message);
%}

%code requires {
#include "ast.h"

typedef enum {
    IDENT_SUFFIX_NONE,
    IDENT_SUFFIX_CALL,
    IDENT_SUFFIX_FIELD,
    IDENT_SUFFIX_QUALIFIED_CALL
} AstIdentSuffixKind;

typedef struct {
    AstIdentSuffixKind kind;
    char *name;
    AstExprList args;
} AstIdentSuffix;
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
}

%token <number> NUMBER
%token <text> IDENT STRING MOD_CONTENT QUALIFIED_IDENT
%token IF CONSIDER_IF THEN ELSE CONSIDER_ELSE END END_CONSIDER PRINT TRUE FALSE NOTHING UNKNOWN_VALUE AND OR NOT WITH FOR TO IN WHILE CONSIDER BREAK CONTINUE FUNCTION RETURN GOTO GOSUB WATCH WITHOUT WATCHERS ON RESUME NEXT STOP ERROR_VALUE MODIFIER PROGRAM LIBRARY LOAD USE EXPORT
%token OP_EQ OP_NE OP_GT OP_LT OP_GE OP_LE OP_NGT OP_NLT OP_NGE OP_NLE
%token PLUS MINUS STAR SLASH LPAREN MOD_LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE COMMA COLON NEWLINE
%precedence NO_DOT
%left DOT
%define parse.error verbose
%locations

%type <stmt_list> program statement_list consider_statement_list consider_else_opt
%type <stmt> statement assignment print_statement call_statement with_lock_statement for_each_statement while_statement consider_statement consider_body_statement function_statement modifier_statement program_statement library_statement use_statement return_statement label_statement goto_statement gosub_statement break_statement continue_statement watch_statement without_watchers_statement on_error_statement error_statement if_statement inline_statement
%type <expr> expression or_expression and_expression comparison_expression
%type <expr> additive_expression multiplicative_expression unary_expression postfix_expression primary lvalue
%type <expr_list> argument_list argument_list_opt array_argument_list
%type <record_field_list> record_field_list
%type <consider_branch_list> consider_branch_list
%type <name_list> parameter_list parameter_list_opt watch_target_list
%type <modifier> modifier
%type <modifier_signature> modifier_signature
%type <duration> duration_terms
%type <ident_suffix> ident_suffix ident_dot_suffix
%type <text> modifier_name modifier_word modifier_context comparison_operator variable_name watch_target_path

%%

program
    : statement_list { parsed_program = $1; $$ = $1; }
    ;

statement_list
    : %empty { $$ = ast_stmt_list_empty(); }
    | statement_list NEWLINE { $$ = $1; }
    | statement_list statement { $$ = ast_stmt_list_append($1, $2); }
    ;

statement
    : assignment NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | print_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | call_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | with_lock_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | for_each_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | while_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | consider_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | function_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | modifier_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | program_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | library_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | use_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | watch_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | without_watchers_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | on_error_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | error_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | return_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | label_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | goto_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | gosub_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | break_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | continue_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | if_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    ;

assignment
    : lvalue OP_EQ expression { $$ = ast_assign($1, ast_modifier_none(), $3); }
    | lvalue modifier OP_EQ expression {
        if (!is_modifier_target_expr($1)) {
            yyerror("modifier target must be a variable, field, or index");
            YYERROR;
        }
        $$ = ast_assign($1, $2, $4);
      }
    ;

lvalue
    : variable_name %prec NO_DOT { $$ = ast_ident($1); }
    | lvalue LBRACKET expression RBRACKET %prec NO_DOT { $$ = ast_index($1, $3); }
    | lvalue DOT IDENT %prec NO_DOT { $$ = ast_field($1, $3); }
    ;

variable_name
    : IDENT %prec NO_DOT { $$ = $1; }
    | END %prec NO_DOT { $$ = copy_const("end"); }
    | NEXT %prec NO_DOT { $$ = copy_const("next"); }
    ;

modifier
    : MOD_LPAREN MOD_CONTENT { $$ = parse_modifier_use($2); }
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
    ;

call_statement
    : IDENT LPAREN argument_list_opt RPAREN { $$ = ast_expr_stmt(ast_call($1, $3)); }
    | QUALIFIED_IDENT LPAREN argument_list_opt RPAREN {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident($1, &library, &name);
        $$ = ast_expr_stmt(ast_qualified_call(library, name, $3));
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
            yyerror("expected lock in with lock block");
            free($2);
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
    : assignment NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | print_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | call_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | with_lock_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | for_each_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | while_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | consider_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | function_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | modifier_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | program_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | library_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | use_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | watch_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | without_watchers_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | on_error_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | error_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | return_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | label_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | goto_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | gosub_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | break_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | continue_statement NEWLINE { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    | if_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    ;

function_statement
    : FUNCTION IDENT LPAREN parameter_list_opt RPAREN NEWLINE statement_list END FUNCTION NEWLINE {
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
            yyerror("expected from in use statement");
            free($2);
            free($3);
            free($4);
            YYERROR;
        }
        free($3);
        $$ = ast_use($2, $4);
      }
    | LOAD IDENT IDENT STRING {
        if (strcmp($3, "from") != 0) {
            yyerror("expected from in load statement");
            free($2);
            free($3);
            free($4);
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
    : IF expression THEN NEWLINE statement_list END IF NEWLINE {
        $$ = ast_if($2, $5);
      }
    | IF expression THEN NEWLINE statement_list ELSE NEWLINE statement_list END IF NEWLINE {
        $$ = ast_if($2, $5);
        $$->as.if_stmt.else_body = $8;
      }
    | IF expression THEN inline_statement NEWLINE {
        $$ = ast_if($2, ast_stmt_list_append(ast_stmt_list_empty(), $4));
      }
    ;

inline_statement
    : goto_statement { $$ = $1; }
    | gosub_statement { $$ = $1; }
    ;

expression
    : or_expression { $$ = $1; }
    ;

or_expression
    : and_expression { $$ = $1; }
    | or_expression OR and_expression { $$ = ast_binary(copy_const("or"), ast_modifier_none(), $1, $3); }
    ;

and_expression
    : comparison_expression { $$ = $1; }
    | and_expression AND comparison_expression { $$ = ast_binary(copy_const("and"), ast_modifier_none(), $1, $3); }
    ;

comparison_expression
    : additive_expression { $$ = $1; }
    | additive_expression comparison_operator additive_expression { $$ = ast_binary($2, ast_modifier_none(), $1, $3); }
    | additive_expression modifier comparison_operator additive_expression {
        if (!is_modifier_target_expr($1)) {
            yyerror("modifier target must be a variable, field, or index");
            YYERROR;
        }
        $$ = ast_binary($3, $2, $1, $4);
      }
    ;

additive_expression
    : multiplicative_expression { $$ = $1; }
    | additive_expression PLUS multiplicative_expression { $$ = ast_binary(copy_const("+"), ast_modifier_none(), $1, $3); }
    | additive_expression MINUS multiplicative_expression { $$ = ast_binary(copy_const("-"), ast_modifier_none(), $1, $3); }
    ;

multiplicative_expression
    : unary_expression { $$ = $1; }
    | multiplicative_expression STAR unary_expression { $$ = ast_binary(copy_const("*"), ast_modifier_none(), $1, $3); }
    | multiplicative_expression SLASH unary_expression { $$ = ast_binary(copy_const("/"), ast_modifier_none(), $1, $3); }
    ;

unary_expression
    : postfix_expression { $$ = $1; }
    | NOT unary_expression { $$ = ast_unary(copy_const("not"), $2); }
    | MINUS unary_expression { $$ = ast_unary(copy_const("-"), $2); }
    ;

postfix_expression
    : primary { $$ = $1; }
    | postfix_expression LBRACKET expression RBRACKET { $$ = ast_index($1, $3); }
    | postfix_expression DOT IDENT { $$ = ast_field($1, $3); }
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
    : NUMBER { $$ = ast_number($1); }
    | duration_terms { $$ = ast_duration($1); }
    | STRING { $$ = ast_string($1); }
    | variable_name ident_suffix {
        if ($2.kind == IDENT_SUFFIX_CALL) {
            $$ = ast_call($1, $2.args);
        } else if ($2.kind == IDENT_SUFFIX_FIELD) {
            $$ = ast_field(ast_ident($1), $2.name);
        } else if ($2.kind == IDENT_SUFFIX_QUALIFIED_CALL) {
            $$ = ast_qualified_call($1, $2.name, $2.args);
        } else {
            $$ = ast_ident($1);
        }
      }
    | QUALIFIED_IDENT LPAREN argument_list_opt RPAREN {
        char *library = NULL;
        char *name = NULL;
        split_qualified_ident($1, &library, &name);
        $$ = ast_qualified_call(library, name, $3);
      }
    | ERROR_VALUE { $$ = ast_ident(copy_const("error")); }
    | TRUE { $$ = ast_bool(1); }
    | FALSE { $$ = ast_bool(0); }
    | NOTHING { $$ = ast_null(); }
    | UNKNOWN_VALUE { $$ = ast_unknown(); }
    | LPAREN expression RPAREN { $$ = $2; }
    | LBRACKET optional_newlines RBRACKET { $$ = ast_array(ast_expr_list_empty()); }
    | LBRACKET optional_newlines array_argument_list optional_newlines RBRACKET { $$ = ast_array($3); }
    | LBRACE optional_newlines RBRACE { $$ = ast_record(ast_record_field_list_empty()); }
    | LBRACE optional_newlines record_field_list optional_newlines RBRACE { $$ = ast_record($3); }
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
    | record_field_list COMMA optional_newlines IDENT OP_EQ expression { $$ = ast_record_field_list_append($1, $4, $6); }
    | record_field_list COMMA optional_newlines IDENT COLON expression { $$ = ast_record_field_list_append($1, $4, $6); }
    ;

optional_newlines
    : %empty
    | optional_newlines NEWLINE
    ;

%%

int parse_source(const char *source, AstStmtList *out_program) {
    Lexer lexer;
    lexer_init(&lexer, source);
    active_lexer = &lexer;
    lexer_error_reported = 0;
    parsed_program = ast_stmt_list_empty();

    int result = yyparse();
    active_lexer = NULL;
    if (result != 0) {
        return result;
    }

    *out_program = parsed_program;
    return 0;
}

static int yylex(void) {
    Token token = lexer_next(active_lexer);
    yylloc.first_line = token.line;
    yylloc.first_column = token.column;
    yylloc.last_line = token.line;
    yylloc.last_column = token.column + token.length;

    switch (token.type) {
    case TOKEN_EOF: return 0;
    case TOKEN_IDENT:
        yylval.text = copy_text(token.start, token.length);
        return IDENT;
    case TOKEN_QUALIFIED_IDENT:
        yylval.text = copy_text(token.start, token.length);
        return QUALIFIED_IDENT;
    case TOKEN_NUMBER:
        yylval.number = strtod(token.start, NULL);
        return NUMBER;
    case TOKEN_STRING:
    {
        int ok = 0;
        yylval.text = copy_string_literal(token.start, token.length, &ok);
        if (!ok) {
            lexer_error_reported = 1;
            return 0;
        }
        return STRING;
    }
    case TOKEN_MOD_CONTENT:
        yylval.text = copy_text(token.start, token.length);
        return MOD_CONTENT;
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
    case TOKEN_FOR: return FOR;
    case TOKEN_TO: return TO;
    case TOKEN_IN: return IN;
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
        if (modifier_lparen_ahead(token.start)) {
            lexer_begin_modifier_content(active_lexer);
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
        if (active_lexer->error_message[0]) {
            fprintf(stderr, "runtime error at %d:%d: %s\n",
                    token.line,
                    token.column,
                    active_lexer->error_message);
        } else {
            fprintf(stderr, "lexer error at %d:%d\n", token.line, token.column);
        }
        lexer_error_reported = 1;
        return 0;
    default:
        fprintf(stderr, "unexpected token %s at %d:%d\n",
                token_type_name(token.type), token.line, token.column);
        return 0;
    }
}

static void yyerror(const char *message) {
    if (lexer_error_reported) {
        return;
    }
    fprintf(stderr, "parse error: %s\n", message);
}

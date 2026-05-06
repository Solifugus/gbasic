%{
#include "ast.h"
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
        } else if (*p == '+' || *p == '-' || *p == '*' || *p == '/' ||
                   *p == '.' || *p == '[' || *p == ']' || *p == ',') {
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
    return *p == '=' || *p == '>' || *p == '<' ||
        (*p == '!' && (p[1] == '=' || p[1] == '>' || p[1] == '<'));
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
    AstNameList name_list;
    AstModifierUse modifier;
    AstModifierSignature modifier_signature;
    AstDuration duration;
    AstIdentSuffix ident_suffix;
}

%token <number> NUMBER
%token <text> IDENT STRING MOD_CONTENT
%token IF THEN END PRINT TRUE FALSE NOTHING UNKNOWN_VALUE AND OR NOT WITH FOR TO IN FUNCTION RETURN GOTO GOSUB WATCH WITHOUT WATCHERS ON RESUME NEXT STOP ERROR_VALUE MODIFIER PROGRAM LIBRARY LOAD USE EXPORT
%token OP_EQ OP_NE OP_GT OP_LT OP_GE OP_LE OP_NGT OP_NLT OP_NGE OP_NLE
%token PLUS MINUS STAR SLASH LPAREN MOD_LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE COMMA COLON NEWLINE
%precedence NO_DOT
%left DOT
%define parse.error verbose
%locations

%type <stmt_list> program statement_list
%type <stmt> statement assignment print_statement call_statement with_lock_statement for_each_statement function_statement modifier_statement program_statement library_statement use_statement return_statement label_statement goto_statement gosub_statement watch_statement without_watchers_statement on_error_statement error_statement if_statement inline_statement
%type <expr> expression or_expression and_expression comparison_expression
%type <expr> additive_expression multiplicative_expression unary_expression postfix_expression primary
%type <expr_list> argument_list argument_list_opt
%type <record_field_list> record_field_list
%type <name_list> parameter_list parameter_list_opt
%type <modifier> modifier
%type <modifier_signature> modifier_signature
%type <duration> duration_terms
%type <ident_suffix> ident_suffix ident_dot_suffix
%type <text> modifier_name modifier_word modifier_context comparison_operator variable_name

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
    | if_statement { $$ = ast_stmt_position($1, @1.first_line, @1.first_column); }
    ;

assignment
    : variable_name OP_EQ expression { $$ = ast_assign($1, ast_modifier_none(), $3); }
    | variable_name modifier OP_EQ expression { $$ = ast_assign($1, $2, $4); }
    | IDENT DOT IDENT OP_EQ expression { $$ = ast_field_assign($1, $3, $5); }
    ;

variable_name
    : IDENT { $$ = $1; }
    | END { $$ = copy_const("end"); }
    | NEXT { $$ = copy_const("next"); }
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
    | IDENT DOT IDENT LPAREN argument_list_opt RPAREN { $$ = ast_expr_stmt(ast_qualified_call($1, $3, $5)); }
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
    : WATCH LPAREN parameter_list RPAREN NEWLINE statement_list END WATCH NEWLINE {
        $$ = ast_watch($3, $6);
      }
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

if_statement
    : IF expression THEN NEWLINE statement_list END IF NEWLINE {
        $$ = ast_if($2, $5);
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
    | additive_expression modifier comparison_operator additive_expression { $$ = ast_binary($3, $2, $1, $4); }
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
    | ERROR_VALUE { $$ = ast_ident(copy_const("error")); }
    | TRUE { $$ = ast_bool(1); }
    | FALSE { $$ = ast_bool(0); }
    | NOTHING { $$ = ast_null(); }
    | UNKNOWN_VALUE { $$ = ast_unknown(); }
    | LPAREN expression RPAREN { $$ = $2; }
    | LBRACKET argument_list_opt RBRACKET { $$ = ast_array($2); }
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
    | record_field_list COMMA optional_newlines IDENT OP_EQ expression { $$ = ast_record_field_list_append($1, $4, $6); }
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
    case TOKEN_THEN: return THEN;
    case TOKEN_END: return END;
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

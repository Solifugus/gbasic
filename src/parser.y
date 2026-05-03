%{
#include "ast.h"
#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Lexer *active_lexer;

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

static char *copy_string_literal(const char *start, int length) {
    if (length >= 2) {
        return copy_text(start + 1, length - 2);
    }
    return copy_text("", 0);
}

static char *copy_const(const char *text) {
    return copy_text(text, (int)strlen(text));
}

static int modifier_lparen_ahead(const char *start) {
    const char *p = start + 1;
    int saw_term = 0;

    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
    }
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_')) {
        return 0;
    }
    saw_term = 1;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
           (*p >= '0' && *p <= '9') || *p == '_') {
        p++;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
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
}

%union {
    double number;
    char *text;
    AstExpr *expr;
    AstStmt *stmt;
    AstStmtList stmt_list;
    AstExprList expr_list;
}

%token <number> NUMBER
%token <text> IDENT STRING
%token IF THEN END PRINT TRUE FALSE AND OR NOT
%token OP_EQ OP_NE OP_GT OP_LT OP_GE OP_LE OP_NGT OP_NLT
%token PLUS MINUS STAR SLASH LPAREN MOD_LPAREN RPAREN LBRACKET RBRACKET COMMA NEWLINE

%type <stmt_list> program statement_list
%type <stmt> statement assignment print_statement if_statement
%type <expr> expression or_expression and_expression comparison_expression
%type <expr> additive_expression multiplicative_expression unary_expression postfix_expression primary
%type <expr_list> argument_list argument_list_opt
%type <text> modifier comparison_operator

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
    : assignment NEWLINE { $$ = $1; }
    | print_statement NEWLINE { $$ = $1; }
    | if_statement { $$ = $1; }
    ;

assignment
    : IDENT OP_EQ expression { $$ = ast_assign($1, NULL, $3); }
    | IDENT modifier OP_EQ expression { $$ = ast_assign($1, $2, $4); }
    ;

modifier
    : MOD_LPAREN IDENT RPAREN { $$ = $2; }
    ;

print_statement
    : PRINT expression { $$ = ast_print($2); }
    ;

if_statement
    : IF expression THEN NEWLINE statement_list END IF NEWLINE {
        $$ = ast_if($2, $5);
      }
    ;

expression
    : or_expression { $$ = $1; }
    ;

or_expression
    : and_expression { $$ = $1; }
    | or_expression OR and_expression { $$ = ast_binary(copy_const("or"), NULL, $1, $3); }
    ;

and_expression
    : comparison_expression { $$ = $1; }
    | and_expression AND comparison_expression { $$ = ast_binary(copy_const("and"), NULL, $1, $3); }
    ;

comparison_expression
    : additive_expression { $$ = $1; }
    | additive_expression comparison_operator additive_expression { $$ = ast_binary($2, NULL, $1, $3); }
    | additive_expression modifier comparison_operator additive_expression { $$ = ast_binary($3, $2, $1, $4); }
    ;

additive_expression
    : multiplicative_expression { $$ = $1; }
    | additive_expression PLUS multiplicative_expression { $$ = ast_binary(copy_const("+"), NULL, $1, $3); }
    | additive_expression MINUS multiplicative_expression { $$ = ast_binary(copy_const("-"), NULL, $1, $3); }
    ;

multiplicative_expression
    : unary_expression { $$ = $1; }
    | multiplicative_expression STAR unary_expression { $$ = ast_binary(copy_const("*"), NULL, $1, $3); }
    | multiplicative_expression SLASH unary_expression { $$ = ast_binary(copy_const("/"), NULL, $1, $3); }
    ;

unary_expression
    : postfix_expression { $$ = $1; }
    | NOT unary_expression { $$ = ast_unary(copy_const("not"), $2); }
    | MINUS unary_expression { $$ = ast_unary(copy_const("-"), $2); }
    ;

postfix_expression
    : primary { $$ = $1; }
    | postfix_expression LBRACKET expression RBRACKET { $$ = ast_index($1, $3); }
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
    ;

primary
    : NUMBER { $$ = ast_number($1); }
    | STRING { $$ = ast_string($1); }
    | IDENT { $$ = ast_ident($1); }
    | IDENT LPAREN argument_list_opt RPAREN { $$ = ast_call($1, $3); }
    | TRUE { $$ = ast_bool(1); }
    | FALSE { $$ = ast_bool(0); }
    | LPAREN expression RPAREN { $$ = $2; }
    | LBRACKET argument_list_opt RBRACKET { $$ = ast_array($2); }
    ;

argument_list_opt
    : %empty { $$ = ast_expr_list_empty(); }
    | argument_list { $$ = $1; }
    ;

argument_list
    : expression { $$ = ast_expr_list_append(ast_expr_list_empty(), $1); }
    | argument_list COMMA expression { $$ = ast_expr_list_append($1, $3); }
    ;

%%

int parse_source(const char *source, AstStmtList *out_program) {
    Lexer lexer;
    lexer_init(&lexer, source);
    active_lexer = &lexer;
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

    switch (token.type) {
    case TOKEN_EOF: return 0;
    case TOKEN_IDENT:
        yylval.text = copy_text(token.start, token.length);
        return IDENT;
    case TOKEN_NUMBER:
        yylval.number = strtod(token.start, NULL);
        return NUMBER;
    case TOKEN_STRING:
        yylval.text = copy_string_literal(token.start, token.length);
        return STRING;
    case TOKEN_IF: return IF;
    case TOKEN_THEN: return THEN;
    case TOKEN_END: return END;
    case TOKEN_PRINT: return PRINT;
    case TOKEN_TRUE: return TRUE;
    case TOKEN_FALSE: return FALSE;
    case TOKEN_AND: return AND;
    case TOKEN_OR: return OR;
    case TOKEN_NOT: return NOT;
    case TOKEN_OP_EQ: return OP_EQ;
    case TOKEN_OP_NE: return OP_NE;
    case TOKEN_OP_GT: return OP_GT;
    case TOKEN_OP_LT: return OP_LT;
    case TOKEN_OP_GE: return OP_GE;
    case TOKEN_OP_LE: return OP_LE;
    case TOKEN_OP_NGT: return OP_NGT;
    case TOKEN_OP_NLT: return OP_NLT;
    case TOKEN_PLUS: return PLUS;
    case TOKEN_MINUS: return MINUS;
    case TOKEN_STAR: return STAR;
    case TOKEN_SLASH: return SLASH;
    case TOKEN_LPAREN:
        if (modifier_lparen_ahead(token.start)) {
            return MOD_LPAREN;
        }
        return LPAREN;
    case TOKEN_RPAREN: return RPAREN;
    case TOKEN_LBRACKET: return LBRACKET;
    case TOKEN_RBRACKET: return RBRACKET;
    case TOKEN_COMMA: return COMMA;
    case TOKEN_NEWLINE: return NEWLINE;
    case TOKEN_ERROR:
        fprintf(stderr, "lexer error at %d:%d\n", token.line, token.column);
        return 0;
    default:
        fprintf(stderr, "unexpected token %s at %d:%d\n",
                token_type_name(token.type), token.line, token.column);
        return 0;
    }
}

static void yyerror(const char *message) {
    fprintf(stderr, "parse error: %s\n", message);
}

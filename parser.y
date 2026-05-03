%{
#include "ast.h"
#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

StmtList parsed_program;

static char *op_text(const char *text) {
    char *copy = malloc(strlen(text) + 1);
    if (!copy) {
        abort();
    }
    strcpy(copy, text);
    return copy;
}
%}

%code requires {
#include "ast.h"
}

%union {
    double number;
    char *string;
    Expr *expr;
    Stmt *stmt;
    ExprList expr_list;
    StmtList stmt_list;
    Modifier *modifier;
}

%token <string> IDENT STRING
%token <number> NUMBER
%token IF THEN ELSE END PRINT TRUE FALSE AND OR NOT
%token OP_EQ OP_NE OP_GT OP_LT OP_GE OP_LE OP_NGT OP_NLT
%token PLUS MINUS STAR SLASH LPAREN MOD_LPAREN RPAREN LBRACKET RBRACKET COMMA NEWLINE
%define parse.error verbose
%expect 38

%type <expr> expression primary function_call array_literal
%type <stmt> statement assignment print_statement if_statement
%type <stmt_list> statement_list else_part
%type <expr_list> argument_list argument_list_opt
%type <modifier> modifier modifier_terms
%type <string> comparison_operator modifier_term

%left OR
%left AND
%right NOT
%nonassoc OP_EQ OP_NE OP_GT OP_LT OP_GE OP_LE OP_NGT OP_NLT
%left PLUS MINUS
%left STAR SLASH
%precedence UMINUS

%%

program
    : statement_list { parsed_program = $1; }
    ;

statement_list
    : %empty { $$ = stmt_list_empty(); }
    | statement_list statement { $$ = stmt_list_append($1, $2); }
    | statement_list NEWLINE { $$ = $1; }
    ;

statement
    : assignment NEWLINE { $$ = $1; }
    | print_statement NEWLINE { $$ = $1; }
    | if_statement { $$ = $1; }
    ;

assignment
    : IDENT OP_EQ expression { $$ = stmt_assign($1, NULL, $3); }
    | IDENT modifier OP_EQ expression { $$ = stmt_assign($1, $2, $4); }
    ;

modifier
    : MOD_LPAREN modifier_terms RPAREN { $$ = $2; }
    ;

modifier_terms
    : modifier_term { $$ = modifier_new($1); }
    | modifier_terms modifier_term { $$ = modifier_append($1, $2); }
    ;

modifier_term
    : IDENT { $$ = $1; }
    | STRING { $$ = $1; }
    | NUMBER {
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", $1);
        $$ = op_text(buf);
      }
    ;

print_statement
    : PRINT expression { $$ = stmt_print($2); }
    ;

if_statement
    : IF expression THEN NEWLINE statement_list else_part END IF NEWLINE {
        $$ = stmt_if($2, $5, $6);
      }
    ;

else_part
    : %empty { $$ = stmt_list_empty(); }
    | ELSE NEWLINE statement_list { $$ = $3; }
    ;

expression
    : primary { $$ = $1; }
    | expression PLUS expression { $$ = expr_binary(op_text("+"), NULL, $1, $3); }
    | expression MINUS expression { $$ = expr_binary(op_text("-"), NULL, $1, $3); }
    | expression STAR expression { $$ = expr_binary(op_text("*"), NULL, $1, $3); }
    | expression SLASH expression { $$ = expr_binary(op_text("/"), NULL, $1, $3); }
    | expression comparison_operator expression { $$ = expr_binary($2, NULL, $1, $3); }
    | expression modifier comparison_operator expression { $$ = expr_binary($3, $2, $1, $4); }
    | expression AND expression { $$ = expr_binary(op_text("and"), NULL, $1, $3); }
    | expression OR expression { $$ = expr_binary(op_text("or"), NULL, $1, $3); }
    | NOT expression { $$ = expr_unary(op_text("not"), $2); }
    | MINUS expression %prec UMINUS { $$ = expr_unary(op_text("-"), $2); }
    ;

comparison_operator
    : OP_EQ { $$ = op_text("="); }
    | OP_NE { $$ = op_text("!="); }
    | OP_GT { $$ = op_text(">"); }
    | OP_LT { $$ = op_text("<"); }
    | OP_GE { $$ = op_text(">="); }
    | OP_LE { $$ = op_text("<="); }
    | OP_NGT { $$ = op_text("!>"); }
    | OP_NLT { $$ = op_text("!<"); }
    ;

primary
    : NUMBER { $$ = expr_number($1); }
    | STRING { $$ = expr_string($1); }
    | TRUE { $$ = expr_bool(1); }
    | FALSE { $$ = expr_bool(0); }
    | IDENT { $$ = expr_var($1); }
    | function_call { $$ = $1; }
    | array_literal { $$ = $1; }
    | LPAREN expression RPAREN { $$ = $2; }
    ;

function_call
    : IDENT LPAREN argument_list_opt RPAREN { $$ = expr_call($1, $3); }
    ;

argument_list_opt
    : %empty { $$ = expr_list_empty(); }
    | argument_list { $$ = $1; }
    ;

argument_list
    : expression { $$ = expr_list_append(expr_list_empty(), $1); }
    | argument_list COMMA expression { $$ = expr_list_append($1, $3); }
    ;

array_literal
    : LBRACKET argument_list_opt RBRACKET { $$ = expr_array($2); }
    ;

%%

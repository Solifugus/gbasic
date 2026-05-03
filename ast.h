#ifndef GBASIC_AST_H
#define GBASIC_AST_H

#include <stddef.h>

typedef enum {
    EXPR_NUMBER,
    EXPR_STRING,
    EXPR_BOOL,
    EXPR_VAR,
    EXPR_ARRAY,
    EXPR_CALL,
    EXPR_BINARY,
    EXPR_UNARY
} ExprKind;

typedef enum {
    STMT_ASSIGN,
    STMT_PRINT,
    STMT_IF
} StmtKind;

typedef struct Modifier {
    char **terms;
    size_t count;
} Modifier;

typedef struct Expr Expr;
typedef struct Stmt Stmt;

typedef struct ExprList {
    Expr **items;
    size_t count;
} ExprList;

typedef struct StmtList {
    Stmt **items;
    size_t count;
} StmtList;

struct Expr {
    ExprKind kind;
    union {
        double number;
        char *string;
        int boolean;
        char *var;
        ExprList array;
        struct {
            char *name;
            ExprList args;
        } call;
        struct {
            char *op;
            Modifier *modifier;
            Expr *left;
            Expr *right;
        } binary;
        struct {
            char *op;
            Expr *operand;
        } unary;
    } as;
};

struct Stmt {
    StmtKind kind;
    union {
        struct {
            char *name;
            Modifier *modifier;
            Expr *value;
        } assign;
        Expr *print;
        struct {
            Expr *condition;
            StmtList then_branch;
            StmtList else_branch;
        } if_stmt;
    } as;
};

ExprList expr_list_empty(void);
ExprList expr_list_append(ExprList list, Expr *expr);
StmtList stmt_list_empty(void);
StmtList stmt_list_append(StmtList list, Stmt *stmt);

Modifier *modifier_new(char *term);
Modifier *modifier_append(Modifier *modifier, char *term);

Expr *expr_number(double value);
Expr *expr_string(char *value);
Expr *expr_bool(int value);
Expr *expr_var(char *name);
Expr *expr_array(ExprList items);
Expr *expr_call(char *name, ExprList args);
Expr *expr_binary(char *op, Modifier *modifier, Expr *left, Expr *right);
Expr *expr_unary(char *op, Expr *operand);

Stmt *stmt_assign(char *name, Modifier *modifier, Expr *value);
Stmt *stmt_print(Expr *expr);
Stmt *stmt_if(Expr *condition, StmtList then_branch, StmtList else_branch);

void ast_free_statements(StmtList list);

#endif

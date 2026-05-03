#ifndef GBASIC_AST_H
#define GBASIC_AST_H

#include <stddef.h>

typedef enum {
    AST_STMT_ASSIGN,
    AST_STMT_FIELD_ASSIGN,
    AST_STMT_PRINT,
    AST_STMT_EXPR,
    AST_STMT_WITH_LOCK,
    AST_STMT_FOR_EACH,
    AST_STMT_FUNCTION,
    AST_STMT_RETURN,
    AST_STMT_LABEL,
    AST_STMT_GOTO,
    AST_STMT_IF
} AstStmtKind;

typedef enum {
    AST_EXPR_NUMBER,
    AST_EXPR_STRING,
    AST_EXPR_IDENT,
    AST_EXPR_BOOL,
    AST_EXPR_ARRAY,
    AST_EXPR_RECORD,
    AST_EXPR_DURATION,
    AST_EXPR_INDEX,
    AST_EXPR_FIELD,
    AST_EXPR_CALL,
    AST_EXPR_BINARY,
    AST_EXPR_UNARY
} AstExprKind;

typedef struct AstExpr AstExpr;
typedef struct AstStmt AstStmt;

typedef struct {
    char *name;
    AstExpr *value;
} AstRecordField;

typedef struct {
    AstStmt **items;
    size_t count;
} AstStmtList;

typedef struct {
    AstExpr **items;
    size_t count;
} AstExprList;

typedef struct {
    AstRecordField *items;
    size_t count;
} AstRecordFieldList;

typedef struct {
    char **items;
    size_t count;
} AstNameList;

typedef struct {
    int years;
    int months;
    int weeks;
    int days;
    int hours;
    int minutes;
    int seconds;
} AstDuration;

struct AstExpr {
    AstExprKind kind;
    union {
        double number;
        char *string;
        char *ident;
        int boolean;
        AstExprList array;
        AstRecordFieldList record;
        AstDuration duration;
        struct {
            AstExpr *array;
            AstExpr *index;
        } index;
        struct {
            AstExpr *object;
            char *field;
        } field;
        struct {
            char *name;
            AstExprList args;
        } call;
        struct {
            char *op;
            char *modifier;
            AstExpr *left;
            AstExpr *right;
        } binary;
        struct {
            char *op;
            AstExpr *expr;
        } unary;
    } as;
};

struct AstStmt {
    AstStmtKind kind;
    union {
        struct {
            char *name;
            char *modifier;
            AstExpr *value;
        } assign;
        struct {
            char *name;
            char *field;
            AstExpr *value;
        } field_assign;
        AstExpr *print;
        AstExpr *expr_stmt;
        struct {
            AstExpr *file;
            AstStmtList body;
        } with_lock;
        struct {
            char *name;
            AstExpr *iterable;
            AstStmtList body;
        } for_each;
        struct {
            char *name;
            AstNameList params;
            AstStmtList body;
        } function;
        AstExpr *return_expr;
        char *label;
        char *goto_label;
        struct {
            AstExpr *condition;
            AstStmtList body;
        } if_stmt;
    } as;
};

AstStmtList ast_stmt_list_empty(void);
AstStmtList ast_stmt_list_append(AstStmtList list, AstStmt *stmt);
AstExprList ast_expr_list_empty(void);
AstExprList ast_expr_list_append(AstExprList list, AstExpr *expr);
AstRecordFieldList ast_record_field_list_empty(void);
AstRecordFieldList ast_record_field_list_append(AstRecordFieldList list, char *name, AstExpr *value);
AstNameList ast_name_list_empty(void);
AstNameList ast_name_list_append(AstNameList list, char *name);

AstExpr *ast_number(double value);
AstExpr *ast_string(char *value);
AstExpr *ast_ident(char *name);
AstExpr *ast_bool(int value);
AstExpr *ast_array(AstExprList items);
AstExpr *ast_record(AstRecordFieldList fields);
AstExpr *ast_duration(AstDuration duration);
AstExpr *ast_index(AstExpr *array, AstExpr *index);
AstExpr *ast_field(AstExpr *object, char *field);
AstExpr *ast_call(char *name, AstExprList args);
AstExpr *ast_binary(char *op, char *modifier, AstExpr *left, AstExpr *right);
AstExpr *ast_unary(char *op, AstExpr *expr);

AstStmt *ast_assign(char *name, char *modifier, AstExpr *value);
AstStmt *ast_field_assign(char *name, char *field, AstExpr *value);
AstStmt *ast_print(AstExpr *expr);
AstStmt *ast_expr_stmt(AstExpr *expr);
AstStmt *ast_with_lock(AstExpr *file, AstStmtList body);
AstStmt *ast_for_each(char *name, AstExpr *iterable, AstStmtList body);
AstStmt *ast_function(char *name, AstNameList params, AstStmtList body);
AstStmt *ast_return(AstExpr *expr);
AstStmt *ast_label(char *name);
AstStmt *ast_goto(char *name);
AstStmt *ast_if(AstExpr *condition, AstStmtList body);

void ast_dump(AstStmtList program);
void ast_free_program(AstStmtList program);

#endif

#ifndef GBASIC_AST_H
#define GBASIC_AST_H

#include <stddef.h>

typedef enum {
    AST_STMT_ASSIGN,
    AST_STMT_PRINT,
    AST_STMT_EXPR,
    AST_STMT_WITH_LOCK,
    AST_STMT_FOR_EACH,
    AST_STMT_FUNCTION,
    AST_STMT_RETURN,
    AST_STMT_LABEL,
    AST_STMT_GOTO,
    AST_STMT_GOSUB,
    AST_STMT_WATCH,
    AST_STMT_WITHOUT_WATCHERS,
    AST_STMT_ON_ERROR_GOTO,
    AST_STMT_ON_ERROR_RESUME_NEXT,
    AST_STMT_ON_ERROR_STOP,
    AST_STMT_ERROR,
    AST_STMT_MODIFIER,
    AST_STMT_PROGRAM,
    AST_STMT_LIBRARY,
    AST_STMT_USE,
    AST_STMT_IF,
    AST_STMT_WHILE,
    AST_STMT_CONSIDER,
    AST_STMT_BREAK,
    AST_STMT_CONTINUE
} AstStmtKind;

typedef enum {
    AST_EXPR_NUMBER,
    AST_EXPR_STRING,
    AST_EXPR_IDENT,
    AST_EXPR_BOOL,
    AST_EXPR_NULL,
    AST_EXPR_UNKNOWN,
    AST_EXPR_ARRAY,
    AST_EXPR_RECORD,
    AST_EXPR_DURATION,
    AST_EXPR_INDEX,
    AST_EXPR_FIELD,
    AST_EXPR_CALL,
    AST_EXPR_BINARY,
    AST_EXPR_UNARY,
    AST_EXPR_NEW,
    AST_EXPR_SPAWN
} AstExprKind;

typedef struct AstExpr AstExpr;
typedef struct AstStmt AstStmt;

/* PBI per-property derivation policy (docs/pbi_design.md §2). COPY is the
 * default so an un-annotated field behaves exactly as before. */
typedef enum {
    AST_FIELD_POLICY_COPY = 0,
    AST_FIELD_POLICY_LINK,
    AST_FIELD_POLICY_RESET,
    AST_FIELD_POLICY_EXCLUDE
} AstFieldPolicy;

typedef struct {
    char *name;
    AstExpr *value;
    AstFieldPolicy policy;   /* declared derivation policy; default COPY */
    AstExpr *reset_expr;     /* evaluated per-instance; non-NULL only for RESET */
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
    AstExpr *match;
    AstStmtList body;
} AstConsiderBranch;

typedef struct {
    AstConsiderBranch *items;
    size_t count;
} AstConsiderBranchList;

typedef struct {
    char **items;
    size_t count;
} AstNameList;

typedef struct {
    char *library;
    char *name;
    AstExprList args;
} AstModifierUse;

typedef struct {
    char *name;
    AstNameList params;
} AstModifierSignature;

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
    int line;
    int column;
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
            char *library;
            char *name;
            AstExprList args;
        } call;
        struct {
            char *op;
            AstModifierUse modifier;
            AstExpr *left;
            AstExpr *right;
        } binary;
        struct {
            char *op;
            AstExpr *expr;
        } unary;
        struct {
            AstExpr *proto;   /* prototype to derive from */
            AstExpr *with;    /* optional override record literal, or NULL */
        } derive;
    } as;
};

struct AstStmt {
    AstStmtKind kind;
    int line;
    int column;
    union {
        struct {
            AstExpr *target;
            AstModifierUse modifier;
            AstExpr *value;
        } assign;
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
        char *gosub_label;
        struct {
            AstNameList names;
            AstStmtList body;
        } watch;
        AstStmtList without_watchers;
        char *on_error_label;
        AstExpr *error_message;
        struct {
            char *name;
            AstNameList params;
            char *context;
            int exported;
            AstStmtList body;
        } modifier;
        struct {
            char *name;
            AstNameList args;
            AstStmtList body;
        } program;
        struct {
            char *name;
            AstStmtList body;
        } library;
        struct {
            char *name;
            char *path;
        } use_stmt;
        struct {
            AstExpr *condition;
            AstStmtList body;
            AstStmtList else_body;
        } if_stmt;
        struct {
            AstExpr *condition;
            AstStmtList body;
        } while_stmt;
        struct {
            AstExpr *subject;
            AstConsiderBranchList branches;
            AstStmtList else_body;
        } consider;
    } as;
};

AstStmtList ast_stmt_list_empty(void);
AstStmtList ast_stmt_list_append(AstStmtList list, AstStmt *stmt);
AstExprList ast_expr_list_empty(void);
AstExprList ast_expr_list_append(AstExprList list, AstExpr *expr);
AstRecordFieldList ast_record_field_list_empty(void);
AstRecordFieldList ast_record_field_list_append(AstRecordFieldList list, char *name, AstExpr *value);
AstRecordFieldList ast_record_field_list_append_policy(AstRecordFieldList list, char *name, AstExpr *value, AstFieldPolicy policy, AstExpr *reset_expr);
AstConsiderBranchList ast_consider_branch_list_empty(void);
AstConsiderBranchList ast_consider_branch_list_append(AstConsiderBranchList list, AstExpr *match, AstStmtList body);
AstNameList ast_name_list_empty(void);
AstNameList ast_name_list_append(AstNameList list, char *name);

AstExpr *ast_number(double value);
AstExpr *ast_string(char *value);
AstExpr *ast_ident(char *name);
AstExpr *ast_bool(int value);
AstExpr *ast_null(void);
AstExpr *ast_unknown(void);
AstExpr *ast_array(AstExprList items);
AstExpr *ast_record(AstRecordFieldList fields);
AstExpr *ast_duration(AstDuration duration);
AstExpr *ast_index(AstExpr *array, AstExpr *index);
AstExpr *ast_field(AstExpr *object, char *field);
AstExpr *ast_call(char *name, AstExprList args);
AstExpr *ast_qualified_call(char *library, char *name, AstExprList args);
AstModifierUse ast_modifier_none(void);
AstModifierUse ast_modifier_use(char *name, AstExprList args);
AstModifierSignature ast_modifier_signature(char *name, AstNameList params);
AstExpr *ast_binary(char *op, AstModifierUse modifier, AstExpr *left, AstExpr *right);
AstExpr *ast_unary(char *op, AstExpr *expr);
AstExpr *ast_new(AstExpr *proto, AstExpr *with);
AstExpr *ast_spawn(char *name, AstExprList args);
AstExpr *ast_expr_position(AstExpr *expr, int line, int column);

AstStmt *ast_assign(AstExpr *target, AstModifierUse modifier, AstExpr *value);
AstStmt *ast_print(AstExpr *expr);
AstStmt *ast_expr_stmt(AstExpr *expr);
AstStmt *ast_with_lock(AstExpr *file, AstStmtList body);
AstStmt *ast_for_each(char *name, AstExpr *iterable, AstStmtList body);
AstStmt *ast_function(char *name, AstNameList params, AstStmtList body);
AstStmt *ast_return(AstExpr *expr);
AstStmt *ast_label(char *name);
AstStmt *ast_goto(char *name);
AstStmt *ast_gosub(char *name);
AstStmt *ast_watch(AstNameList names, AstStmtList body);
AstStmt *ast_without_watchers(AstStmtList body);
AstStmt *ast_on_error_goto(char *label);
AstStmt *ast_on_error_resume_next(void);
AstStmt *ast_on_error_stop(void);
AstStmt *ast_error(AstExpr *message);
AstStmt *ast_modifier(char *name, AstNameList params, char *context, int exported, AstStmtList body);
AstStmt *ast_program(char *name, AstNameList args, AstStmtList body);
AstStmt *ast_library(char *name, AstStmtList body);
AstStmt *ast_use(char *name, char *path);
AstStmt *ast_if(AstExpr *condition, AstStmtList body);
AstStmt *ast_while(AstExpr *condition, AstStmtList body);
AstStmt *ast_consider(AstExpr *subject, AstConsiderBranchList branches, AstStmtList else_body);
AstStmt *ast_break(void);
AstStmt *ast_continue(void);
AstStmt *ast_stmt_position(AstStmt *stmt, int line, int column);

void ast_dump(AstStmtList program);
void ast_free_program(AstStmtList program);

#endif

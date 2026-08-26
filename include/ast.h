#ifndef GBASIC_AST_H
#define GBASIC_AST_H

#include <stddef.h>

/* PLAT-WARN warning-channel modes (docs/warning_model_design.md). */
typedef enum {
    WARN_MODE_UNSET = 0,   /* no explicit setting: look further out */
    WARN_MODE_PRINT,
    WARN_MODE_IGNORE,
    WARN_MODE_NEXT,
    WARN_MODE_STOP
} WarnMode;

typedef enum {
    AST_STMT_ASSIGN,
    AST_STMT_PRINT,
    AST_STMT_EXPR,
    AST_STMT_WITH_LOCK,
    AST_STMT_FOR_EACH,
    AST_STMT_FOR_RANGE,
    AST_STMT_FUNCTION,
    AST_STMT_RETURN,
    AST_STMT_LABEL,
    AST_STMT_GOTO,
    AST_STMT_GOSUB,
    AST_STMT_WATCH,
    AST_STMT_UNWATCH,
    AST_STMT_WITHOUT_WATCHERS,
    AST_STMT_ON_ERROR_GOTO,
    AST_STMT_ON_ERROR_GOTO_NEXT,
    AST_STMT_ON_WARNING,        /* PLAT-WARN: mode in as.warn_mode */
    AST_STMT_WARNING,           /* PLAT-WARN: raise one; as.error_message */
    AST_STMT_ON_ERROR_STOP,
    AST_STMT_ERROR,
    AST_STMT_MODIFIER,
    AST_STMT_PROGRAM,
    AST_STMT_LIBRARY,
    AST_STMT_USE,
    AST_STMT_IF,
    AST_STMT_WHILE,
    AST_STMT_DO_LOOP,
    AST_STMT_CONSIDER,
    AST_STMT_BREAK,
    AST_STMT_CONTINUE,
    AST_STMT_SERVER
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

/* PLAT-WEB-5: one entry in a `server` declarative block. The grammar is
 * generic (IDENT-led productions, zero reserved words); which words are legal
 * -- verbs, directives, sub-block kinds, hook names -- is decided by the
 * load-time validation pass, exactly as the design draft's §3 asks. */
typedef enum {
    AST_SERVER_DIRECTIVE,   /* root "public" / trust_proxy "ip", ... */
    AST_SERVER_HANDLER,     /* get "/path"( req ) ... end get  (stream too) */
    AST_SERVER_SITE,        /* web name( host: ... ) ... end web */
    AST_SERVER_HOOK         /* on drain ... end on */
} AstServerItemKind;

typedef struct AstServerItem AstServerItem;

typedef struct {
    AstServerItem **items;
    size_t count;
} AstServerItemList;

struct AstServerItem {
    AstServerItemKind kind;
    char *word;              /* directive name / verb / site kind word / hook name */
    char *path;              /* handler: the path string literal */
    char *name;              /* site: the declared name */
    char *close_word;        /* handler/site: the word after `end`, matched in validation */
    AstNameList strings;     /* directive: string-literal arguments */
    AstRecordFieldList options; /* site: head options */
    AstNameList params;      /* handler: parameter names */
    AstStmtList body;        /* handler/hook body */
    AstServerItemList entries;  /* site: nested items */
    char *fn_name;           /* handler/hook: minted internal function name (NULL until registration) */
    AstStmt *fn_stmt;        /* handler/hook: the synthesized function node the registry
                              * holds. A SHELL over this item's params/body (shared, not
                              * copied), so the item frees those and nothing frees this. */
    int line;
    int column;
};

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
            char *library;    /* library/receiver-variable name, or NULL */
            char *name;       /* function / method name */
            AstExprList args;
            AstExpr *receiver; /* NULL for a plain/qualified call; set for a
                                * method call whose receiver is an arbitrary
                                * expression (a.b.method(), make().method()) */
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
    int line;         /* 1-based BYTE start line   (set by ast_stmt_span/position) */
    int column;       /* 1-based BYTE start column */
    int end_line;     /* 1-based BYTE end line     (exclusive end; 0 if unset)     */
    int end_column;   /* 1-based BYTE end column   (exclusive: one past last byte) */
    union {
        struct {
            AstExpr *target;
            AstModifierUse modifier;
            AstExpr *value;
            /* Compound assignment: '+', '-', '*', '/', or 0 for a plain `=`.
             *
             * Carried as an OPERATOR rather than desugared in the grammar
             * into `target = target op value`, which would need the target
             * subtree DUPLICATED -- two owners of the same nodes, and a double
             * free to go with them. The evaluator folds it instead, by handing
             * the real operator a synthetic binary node. */
            char op;
        } assign;
        /* PLAT-STDERR: `print` and `print to error` are one node kind with a
         * destination, not two kinds. They differ only in which stream the
         * rendered bytes go to, so sharing the node keeps every consumer that
         * switches on AstStmtKind -- the evaluator, the AST dump, --add-loads,
         * source_outline -- correct without any of them learning a new case. */
        struct {
            AstExpr *expr;
            int to_stderr;   /* 0 = standard output, 1 = standard error */
        } print;
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
            AstStmtList body;
            AstExpr *condition;
            int until;           /* 1 = `loop until c` (stop when true), 0 = `loop while c` */
        } do_loop;
        struct {
            char *name;          /* the counter variable */
            AstExpr *start;
            AstExpr *limit;      /* INCLUSIVE, as BASIC has always had it */
            AstExpr *step;       /* NULL means 1 */
            AstStmtList body;
        } for_range;
        struct {
            char *name;          /* bare: the declared name; dotted: internal name (NULL until eval) */
            AstNameList params;
            AstStmtList body;
            char *object;        /* dotted `function obj.method()`: receiver var name; else NULL */
            char *field;         /* dotted: the method field name; else NULL */
            /* The file this function was PARSED from, or NULL for the root
             * source. Owned here; stamped at registration (eval.c), not by the
             * parser, because the parser is handed text and does not know the
             * path it came from.
             *
             * It exists so a runtime error inside a loaded library names the
             * LIBRARY. Line numbers come from the library's own AST nodes, but
             * the path used to come from a global that had already been restored
             * by the time the function was CALLED -- so every stdlib diagnostic
             * reported a real line against the wrong file. */
            char *source_path;
        } function;
        AstExpr *return_expr;
        char *label;
        char *goto_label;
        char *gosub_label;
        char *loop_target;   /* `break x` / `continue x`: the named loop, or NULL */
        struct {
            char *name;          /* NULL for the anonymous form */
            AstNameList names;
            AstStmtList body;
        } watch;
        AstExpr *unwatch_expr;
        AstStmtList without_watchers;
        char *on_error_label;
        int warn_mode;   /* WarnMode; PLAT-WARN */
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
        struct {
            char *word;          /* the block kind word; "server" is the only one today */
            char *name;          /* the declared name bound in the enclosing scope */
            char *close_word;    /* the word after the closing `end` */
            AstRecordFieldList options;
            AstServerItemList items;
        } server;
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

AstServerItemList ast_server_item_list_empty(void);
AstServerItemList ast_server_item_list_append(AstServerItemList list, AstServerItem *item);
AstServerItem *ast_server_directive(char *word, AstNameList strings, int line, int column);
AstServerItem *ast_server_handler(char *word, char *path, AstNameList params,
                                  AstStmtList body, char *close_word, int line, int column);
AstServerItem *ast_server_site(char *word, char *name, AstRecordFieldList options,
                               AstServerItemList entries, char *close_word, int line, int column);
AstServerItem *ast_server_hook(char *word, AstStmtList body, int line, int column);
AstStmt *ast_server(char *word, char *name, AstRecordFieldList options,
                    AstServerItemList items, char *close_word);

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
AstExpr *ast_method_call(AstExpr *receiver, char *name, AstExprList args);
AstModifierUse ast_modifier_none(void);
AstModifierUse ast_modifier_use(char *name, AstExprList args);
AstModifierSignature ast_modifier_signature(char *name, AstNameList params);
AstExpr *ast_binary(char *op, AstModifierUse modifier, AstExpr *left, AstExpr *right);
AstExpr *ast_unary(char *op, AstExpr *expr);
AstExpr *ast_new(AstExpr *proto, AstExpr *with);
AstExpr *ast_spawn(char *name, AstExprList args);
AstExpr *ast_expr_position(AstExpr *expr, int line, int column);

AstStmt *ast_assign(AstExpr *target, AstModifierUse modifier, AstExpr *value);
AstStmt *ast_assign_op(AstExpr *target, AstModifierUse modifier, AstExpr *value, char op);
AstStmt *ast_print(AstExpr *expr);
AstStmt *ast_print_error(AstExpr *expr);
AstStmt *ast_expr_stmt(AstExpr *expr);
AstStmt *ast_with_lock(AstExpr *file, AstStmtList body);
AstStmt *ast_for_each(char *name, AstExpr *iterable, AstStmtList body);
AstStmt *ast_for_range(char *name, AstExpr *start, AstExpr *limit,
                       AstExpr *step, AstStmtList body);
AstStmt *ast_do_loop(AstStmtList body, AstExpr *condition, int until);
AstStmt *ast_function(char *name, AstNameList params, AstStmtList body);
AstStmt *ast_return(AstExpr *expr);
AstStmt *ast_label(char *name);
AstStmt *ast_goto(char *name);
AstStmt *ast_gosub(char *name);
AstStmt *ast_watch(char *name, AstNameList names, AstStmtList body);
AstStmt *ast_unwatch(AstExpr *expr);
AstStmt *ast_without_watchers(AstStmtList body);
AstStmt *ast_on_error_goto(char *label);
AstStmt *ast_on_error_goto_next(void);
AstStmt *ast_on_error_stop(void);
AstStmt *ast_on_warning(int mode);
AstStmt *ast_warning(AstExpr *message);
AstStmt *ast_error(AstExpr *message);
AstStmt *ast_modifier(char *name, AstNameList params, char *context, int exported, AstStmtList body);
AstStmt *ast_program(char *name, AstNameList args, AstStmtList body);
AstStmt *ast_library(char *name, AstStmtList body);
AstStmt *ast_use(char *name, char *path);
AstStmt *ast_if(AstExpr *condition, AstStmtList body);
AstStmt *ast_while(AstExpr *condition, AstStmtList body);
AstStmt *ast_consider(AstExpr *subject, AstConsiderBranchList branches, AstStmtList else_body);
AstStmt *ast_break(char *target);
AstStmt *ast_continue(char *target);
AstStmt *ast_stmt_position(AstStmt *stmt, int line, int column);
/* Set both the start (line,column) and the exclusive end (end_line,end_column)
 * BYTE positions on a statement. end_* use the parser's yylloc last_* convention
 * (one past the last byte of the last token in the construct). */
AstStmt *ast_stmt_span(AstStmt *stmt, int line, int column,
                       int end_line, int end_column);

void ast_dump(AstStmtList program);
void ast_free_program(AstStmtList program);

/* Single-node + aggregate frees for parser error-recovery destructors (all accept
 * the empty/NULL case). */
void ast_free_expr(AstExpr *expr);
void ast_free_stmt(AstStmt *stmt);
void ast_free_expr_list(AstExprList list);
void ast_free_name_list(AstNameList list);
void ast_free_record_field_list(AstRecordFieldList list);
void ast_free_server_item_list(AstServerItemList list);
void ast_free_consider_branch_list(AstConsiderBranchList list);
void ast_free_modifier_use(AstModifierUse modifier);
void ast_free_modifier_signature(AstModifierSignature sig);

#endif

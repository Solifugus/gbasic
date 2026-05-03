#include "ast.h"

#include <stdlib.h>

static void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        abort();
    }
    return ptr;
}

static void *xrealloc(void *ptr, size_t size) {
    void *next = realloc(ptr, size);
    if (!next) {
        abort();
    }
    return next;
}

ExprList expr_list_empty(void) {
    ExprList list = {0};
    return list;
}

ExprList expr_list_append(ExprList list, Expr *expr) {
    list.items = xrealloc(list.items, sizeof(Expr *) * (list.count + 1));
    list.items[list.count++] = expr;
    return list;
}

StmtList stmt_list_empty(void) {
    StmtList list = {0};
    return list;
}

StmtList stmt_list_append(StmtList list, Stmt *stmt) {
    list.items = xrealloc(list.items, sizeof(Stmt *) * (list.count + 1));
    list.items[list.count++] = stmt;
    return list;
}

Modifier *modifier_new(char *term) {
    Modifier *modifier = xmalloc(sizeof(*modifier));
    modifier->terms = NULL;
    modifier->count = 0;
    return modifier_append(modifier, term);
}

Modifier *modifier_append(Modifier *modifier, char *term) {
    modifier->terms = xrealloc(modifier->terms, sizeof(char *) * (modifier->count + 1));
    modifier->terms[modifier->count++] = term;
    return modifier;
}

Expr *expr_number(double value) {
    Expr *expr = xmalloc(sizeof(*expr));
    expr->kind = EXPR_NUMBER;
    expr->as.number = value;
    return expr;
}

Expr *expr_string(char *value) {
    Expr *expr = xmalloc(sizeof(*expr));
    expr->kind = EXPR_STRING;
    expr->as.string = value;
    return expr;
}

Expr *expr_bool(int value) {
    Expr *expr = xmalloc(sizeof(*expr));
    expr->kind = EXPR_BOOL;
    expr->as.boolean = value;
    return expr;
}

Expr *expr_var(char *name) {
    Expr *expr = xmalloc(sizeof(*expr));
    expr->kind = EXPR_VAR;
    expr->as.var = name;
    return expr;
}

Expr *expr_array(ExprList items) {
    Expr *expr = xmalloc(sizeof(*expr));
    expr->kind = EXPR_ARRAY;
    expr->as.array = items;
    return expr;
}

Expr *expr_call(char *name, ExprList args) {
    Expr *expr = xmalloc(sizeof(*expr));
    expr->kind = EXPR_CALL;
    expr->as.call.name = name;
    expr->as.call.args = args;
    return expr;
}

Expr *expr_binary(char *op, Modifier *modifier, Expr *left, Expr *right) {
    Expr *expr = xmalloc(sizeof(*expr));
    expr->kind = EXPR_BINARY;
    expr->as.binary.op = op;
    expr->as.binary.modifier = modifier;
    expr->as.binary.left = left;
    expr->as.binary.right = right;
    return expr;
}

Expr *expr_unary(char *op, Expr *operand) {
    Expr *expr = xmalloc(sizeof(*expr));
    expr->kind = EXPR_UNARY;
    expr->as.unary.op = op;
    expr->as.unary.operand = operand;
    return expr;
}

Stmt *stmt_assign(char *name, Modifier *modifier, Expr *value) {
    Stmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = STMT_ASSIGN;
    stmt->as.assign.name = name;
    stmt->as.assign.modifier = modifier;
    stmt->as.assign.value = value;
    return stmt;
}

Stmt *stmt_print(Expr *expr) {
    Stmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = STMT_PRINT;
    stmt->as.print = expr;
    return stmt;
}

Stmt *stmt_if(Expr *condition, StmtList then_branch, StmtList else_branch) {
    Stmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = STMT_IF;
    stmt->as.if_stmt.condition = condition;
    stmt->as.if_stmt.then_branch = then_branch;
    stmt->as.if_stmt.else_branch = else_branch;
    return stmt;
}

static void free_modifier(Modifier *modifier) {
    if (!modifier) {
        return;
    }
    for (size_t i = 0; i < modifier->count; i++) {
        free(modifier->terms[i]);
    }
    free(modifier->terms);
    free(modifier);
}

static void free_expr(Expr *expr) {
    if (!expr) {
        return;
    }
    switch (expr->kind) {
    case EXPR_STRING:
        free(expr->as.string);
        break;
    case EXPR_VAR:
        free(expr->as.var);
        break;
    case EXPR_ARRAY:
        for (size_t i = 0; i < expr->as.array.count; i++) {
            free_expr(expr->as.array.items[i]);
        }
        free(expr->as.array.items);
        break;
    case EXPR_CALL:
        free(expr->as.call.name);
        for (size_t i = 0; i < expr->as.call.args.count; i++) {
            free_expr(expr->as.call.args.items[i]);
        }
        free(expr->as.call.args.items);
        break;
    case EXPR_BINARY:
        free(expr->as.binary.op);
        free_modifier(expr->as.binary.modifier);
        free_expr(expr->as.binary.left);
        free_expr(expr->as.binary.right);
        break;
    case EXPR_UNARY:
        free(expr->as.unary.op);
        free_expr(expr->as.unary.operand);
        break;
    case EXPR_NUMBER:
    case EXPR_BOOL:
        break;
    }
    free(expr);
}

static void free_stmt(Stmt *stmt) {
    if (!stmt) {
        return;
    }
    switch (stmt->kind) {
    case STMT_ASSIGN:
        free(stmt->as.assign.name);
        free_modifier(stmt->as.assign.modifier);
        free_expr(stmt->as.assign.value);
        break;
    case STMT_PRINT:
        free_expr(stmt->as.print);
        break;
    case STMT_IF:
        free_expr(stmt->as.if_stmt.condition);
        ast_free_statements(stmt->as.if_stmt.then_branch);
        ast_free_statements(stmt->as.if_stmt.else_branch);
        break;
    }
    free(stmt);
}

void ast_free_statements(StmtList list) {
    for (size_t i = 0; i < list.count; i++) {
        free_stmt(list.items[i]);
    }
    free(list.items);
}

#include "ast.h"

#include <stdio.h>
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

AstStmtList ast_stmt_list_empty(void) {
    AstStmtList list = {0};
    return list;
}

AstStmtList ast_stmt_list_append(AstStmtList list, AstStmt *stmt) {
    list.items = xrealloc(list.items, sizeof(AstStmt *) * (list.count + 1));
    list.items[list.count++] = stmt;
    return list;
}

AstExprList ast_expr_list_empty(void) {
    AstExprList list = {0};
    return list;
}

AstExprList ast_expr_list_append(AstExprList list, AstExpr *expr) {
    list.items = xrealloc(list.items, sizeof(AstExpr *) * (list.count + 1));
    list.items[list.count++] = expr;
    return list;
}

AstExpr *ast_number(double value) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_NUMBER;
    expr->as.number = value;
    return expr;
}

AstExpr *ast_string(char *value) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_STRING;
    expr->as.string = value;
    return expr;
}

AstExpr *ast_ident(char *name) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_IDENT;
    expr->as.ident = name;
    return expr;
}

AstExpr *ast_bool(int value) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_BOOL;
    expr->as.boolean = value;
    return expr;
}

AstExpr *ast_array(AstExprList items) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_ARRAY;
    expr->as.array = items;
    return expr;
}

AstExpr *ast_index(AstExpr *array, AstExpr *index) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_INDEX;
    expr->as.index.array = array;
    expr->as.index.index = index;
    return expr;
}

AstExpr *ast_call(char *name, AstExprList args) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_CALL;
    expr->as.call.name = name;
    expr->as.call.args = args;
    return expr;
}

AstExpr *ast_binary(char *op, char *modifier, AstExpr *left, AstExpr *right) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_BINARY;
    expr->as.binary.op = op;
    expr->as.binary.modifier = modifier;
    expr->as.binary.left = left;
    expr->as.binary.right = right;
    return expr;
}

AstExpr *ast_unary(char *op, AstExpr *child) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_UNARY;
    expr->as.unary.op = op;
    expr->as.unary.expr = child;
    return expr;
}

AstStmt *ast_assign(char *name, char *modifier, AstExpr *value) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_ASSIGN;
    stmt->as.assign.name = name;
    stmt->as.assign.modifier = modifier;
    stmt->as.assign.value = value;
    return stmt;
}

AstStmt *ast_print(AstExpr *expr) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_PRINT;
    stmt->as.print = expr;
    return stmt;
}

AstStmt *ast_if(AstExpr *condition, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_IF;
    stmt->as.if_stmt.condition = condition;
    stmt->as.if_stmt.body = body;
    return stmt;
}

static void dump_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

static void dump_expr(AstExpr *expr, int indent) {
    if (!expr) {
        dump_indent(indent);
        printf("(null)\n");
        return;
    }

    dump_indent(indent);
    switch (expr->kind) {
    case AST_EXPR_NUMBER:
        printf("Number %g\n", expr->as.number);
        break;
    case AST_EXPR_STRING:
        printf("String \"%s\"\n", expr->as.string);
        break;
    case AST_EXPR_IDENT:
        printf("Identifier %s\n", expr->as.ident);
        break;
    case AST_EXPR_BOOL:
        printf("Boolean %s\n", expr->as.boolean ? "true" : "false");
        break;
    case AST_EXPR_ARRAY:
        printf("Array\n");
        for (size_t i = 0; i < expr->as.array.count; i++) {
            dump_expr(expr->as.array.items[i], indent + 1);
        }
        break;
    case AST_EXPR_INDEX:
        printf("Index\n");
        dump_indent(indent + 1);
        printf("Array\n");
        dump_expr(expr->as.index.array, indent + 2);
        dump_indent(indent + 1);
        printf("Subscript\n");
        dump_expr(expr->as.index.index, indent + 2);
        break;
    case AST_EXPR_CALL:
        printf("Call %s\n", expr->as.call.name);
        for (size_t i = 0; i < expr->as.call.args.count; i++) {
            dump_expr(expr->as.call.args.items[i], indent + 1);
        }
        break;
    case AST_EXPR_BINARY:
        if (expr->as.binary.modifier) {
            printf("Binary %s modifier(%s)\n", expr->as.binary.op, expr->as.binary.modifier);
        } else {
            printf("Binary %s\n", expr->as.binary.op);
        }
        dump_expr(expr->as.binary.left, indent + 1);
        dump_expr(expr->as.binary.right, indent + 1);
        break;
    case AST_EXPR_UNARY:
        printf("Unary %s\n", expr->as.unary.op);
        dump_expr(expr->as.unary.expr, indent + 1);
        break;
    }
}

static void dump_stmt(AstStmt *stmt, int indent) {
    dump_indent(indent);
    switch (stmt->kind) {
    case AST_STMT_ASSIGN:
        if (stmt->as.assign.modifier) {
            printf("Assign %s modifier(%s)\n", stmt->as.assign.name, stmt->as.assign.modifier);
        } else {
            printf("Assign %s\n", stmt->as.assign.name);
        }
        dump_expr(stmt->as.assign.value, indent + 1);
        break;
    case AST_STMT_PRINT:
        printf("Print\n");
        dump_expr(stmt->as.print, indent + 1);
        break;
    case AST_STMT_IF:
        printf("If\n");
        dump_indent(indent + 1);
        printf("Condition\n");
        dump_expr(stmt->as.if_stmt.condition, indent + 2);
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.if_stmt.body.count; i++) {
            dump_stmt(stmt->as.if_stmt.body.items[i], indent + 2);
        }
        break;
    }
}

void ast_dump(AstStmtList program) {
    printf("Program\n");
    for (size_t i = 0; i < program.count; i++) {
        dump_stmt(program.items[i], 1);
    }
}

static void free_expr(AstExpr *expr) {
    if (!expr) {
        return;
    }

    switch (expr->kind) {
    case AST_EXPR_STRING:
        free(expr->as.string);
        break;
    case AST_EXPR_IDENT:
        free(expr->as.ident);
        break;
    case AST_EXPR_BINARY:
        free(expr->as.binary.op);
        free(expr->as.binary.modifier);
        free_expr(expr->as.binary.left);
        free_expr(expr->as.binary.right);
        break;
    case AST_EXPR_ARRAY:
        for (size_t i = 0; i < expr->as.array.count; i++) {
            free_expr(expr->as.array.items[i]);
        }
        free(expr->as.array.items);
        break;
    case AST_EXPR_INDEX:
        free_expr(expr->as.index.array);
        free_expr(expr->as.index.index);
        break;
    case AST_EXPR_CALL:
        free(expr->as.call.name);
        for (size_t i = 0; i < expr->as.call.args.count; i++) {
            free_expr(expr->as.call.args.items[i]);
        }
        free(expr->as.call.args.items);
        break;
    case AST_EXPR_UNARY:
        free(expr->as.unary.op);
        free_expr(expr->as.unary.expr);
        break;
    case AST_EXPR_NUMBER:
    case AST_EXPR_BOOL:
        break;
    }

    free(expr);
}

static void free_stmt(AstStmt *stmt) {
    if (!stmt) {
        return;
    }

    switch (stmt->kind) {
    case AST_STMT_ASSIGN:
        free(stmt->as.assign.name);
        free(stmt->as.assign.modifier);
        free_expr(stmt->as.assign.value);
        break;
    case AST_STMT_PRINT:
        free_expr(stmt->as.print);
        break;
    case AST_STMT_IF:
        free_expr(stmt->as.if_stmt.condition);
        ast_free_program(stmt->as.if_stmt.body);
        break;
    }

    free(stmt);
}

void ast_free_program(AstStmtList program) {
    for (size_t i = 0; i < program.count; i++) {
        free_stmt(program.items[i]);
    }
    free(program.items);
}

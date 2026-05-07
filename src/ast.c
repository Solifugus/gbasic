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

AstRecordFieldList ast_record_field_list_empty(void) {
    AstRecordFieldList list = {0};
    return list;
}

AstRecordFieldList ast_record_field_list_append(AstRecordFieldList list, char *name, AstExpr *value) {
    list.items = xrealloc(list.items, sizeof(AstRecordField) * (list.count + 1));
    list.items[list.count].name = name;
    list.items[list.count].value = value;
    list.count++;
    return list;
}

AstNameList ast_name_list_empty(void) {
    AstNameList list = {0};
    return list;
}

AstNameList ast_name_list_append(AstNameList list, char *name) {
    list.items = xrealloc(list.items, sizeof(char *) * (list.count + 1));
    list.items[list.count++] = name;
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

AstExpr *ast_null(void) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_NULL;
    return expr;
}

AstExpr *ast_unknown(void) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_UNKNOWN;
    return expr;
}

AstExpr *ast_array(AstExprList items) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_ARRAY;
    expr->as.array = items;
    return expr;
}

AstExpr *ast_record(AstRecordFieldList fields) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_RECORD;
    expr->as.record = fields;
    return expr;
}

AstExpr *ast_duration(AstDuration duration) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_DURATION;
    expr->as.duration = duration;
    return expr;
}

AstExpr *ast_index(AstExpr *array, AstExpr *index) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_INDEX;
    expr->as.index.array = array;
    expr->as.index.index = index;
    return expr;
}

AstExpr *ast_field(AstExpr *object, char *field) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_FIELD;
    expr->as.field.object = object;
    expr->as.field.field = field;
    return expr;
}

AstExpr *ast_call(char *name, AstExprList args) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = AST_EXPR_CALL;
    expr->as.call.library = NULL;
    expr->as.call.name = name;
    expr->as.call.args = args;
    return expr;
}

AstExpr *ast_qualified_call(char *library, char *name, AstExprList args) {
    AstExpr *expr = ast_call(name, args);
    expr->as.call.library = library;
    return expr;
}

AstModifierUse ast_modifier_none(void) {
    AstModifierUse modifier = {0};
    return modifier;
}

AstModifierUse ast_modifier_use(char *name, AstExprList args) {
    AstModifierUse modifier;
    modifier.library = NULL;
    modifier.name = name;
    modifier.args = args;
    return modifier;
}

AstModifierSignature ast_modifier_signature(char *name, AstNameList params) {
    AstModifierSignature signature;
    signature.name = name;
    signature.params = params;
    return signature;
}

AstExpr *ast_binary(char *op, AstModifierUse modifier, AstExpr *left, AstExpr *right) {
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

AstStmt *ast_assign(char *name, AstModifierUse modifier, AstExpr *value) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_ASSIGN;
    stmt->as.assign.name = name;
    stmt->as.assign.modifier = modifier;
    stmt->as.assign.value = value;
    return stmt;
}

AstStmt *ast_field_assign(char *name, char *field, AstExpr *value) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_FIELD_ASSIGN;
    stmt->as.field_assign.name = name;
    stmt->as.field_assign.field = field;
    stmt->as.field_assign.value = value;
    return stmt;
}

AstStmt *ast_print(AstExpr *expr) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_PRINT;
    stmt->as.print = expr;
    return stmt;
}

AstStmt *ast_expr_stmt(AstExpr *expr) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_EXPR;
    stmt->as.expr_stmt = expr;
    return stmt;
}

AstStmt *ast_with_lock(AstExpr *file, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_WITH_LOCK;
    stmt->as.with_lock.file = file;
    stmt->as.with_lock.body = body;
    return stmt;
}

AstStmt *ast_for_each(char *name, AstExpr *iterable, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_FOR_EACH;
    stmt->as.for_each.name = name;
    stmt->as.for_each.iterable = iterable;
    stmt->as.for_each.body = body;
    return stmt;
}

AstStmt *ast_function(char *name, AstNameList params, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_FUNCTION;
    stmt->as.function.name = name;
    stmt->as.function.params = params;
    stmt->as.function.body = body;
    return stmt;
}

AstStmt *ast_return(AstExpr *expr) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_RETURN;
    stmt->as.return_expr = expr;
    return stmt;
}

AstStmt *ast_label(char *name) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_LABEL;
    stmt->as.label = name;
    return stmt;
}

AstStmt *ast_goto(char *name) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_GOTO;
    stmt->as.goto_label = name;
    return stmt;
}

AstStmt *ast_gosub(char *name) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_GOSUB;
    stmt->as.gosub_label = name;
    return stmt;
}

AstStmt *ast_watch(AstNameList names, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_WATCH;
    stmt->as.watch.names = names;
    stmt->as.watch.body = body;
    return stmt;
}

AstStmt *ast_without_watchers(AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_WITHOUT_WATCHERS;
    stmt->as.without_watchers = body;
    return stmt;
}

AstStmt *ast_on_error_goto(char *label) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_ON_ERROR_GOTO;
    stmt->as.on_error_label = label;
    return stmt;
}

AstStmt *ast_on_error_resume_next(void) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_ON_ERROR_RESUME_NEXT;
    return stmt;
}

AstStmt *ast_on_error_stop(void) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_ON_ERROR_STOP;
    return stmt;
}

AstStmt *ast_error(AstExpr *message) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_ERROR;
    stmt->as.error_message = message;
    return stmt;
}

AstStmt *ast_modifier(char *name, AstNameList params, char *context, int exported, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_MODIFIER;
    stmt->as.modifier.name = name;
    stmt->as.modifier.params = params;
    stmt->as.modifier.context = context;
    stmt->as.modifier.exported = exported;
    stmt->as.modifier.body = body;
    return stmt;
}

AstStmt *ast_program(char *name, AstNameList args, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_PROGRAM;
    stmt->as.program.name = name;
    stmt->as.program.args = args;
    stmt->as.program.body = body;
    return stmt;
}

AstStmt *ast_library(char *name, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_LIBRARY;
    stmt->as.library.name = name;
    stmt->as.library.body = body;
    return stmt;
}

AstStmt *ast_use(char *name, char *path) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_USE;
    stmt->as.use_stmt.name = name;
    stmt->as.use_stmt.path = path;
    return stmt;
}

AstStmt *ast_if(AstExpr *condition, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_IF;
    stmt->as.if_stmt.condition = condition;
    stmt->as.if_stmt.body = body;
    stmt->as.if_stmt.else_body = ast_stmt_list_empty();
    return stmt;
}

AstStmt *ast_while(AstExpr *condition, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_WHILE;
    stmt->as.while_stmt.condition = condition;
    stmt->as.while_stmt.body = body;
    return stmt;
}

AstStmt *ast_break(void) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_BREAK;
    return stmt;
}

AstStmt *ast_continue(void) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_CONTINUE;
    return stmt;
}

AstStmt *ast_stmt_position(AstStmt *stmt, int line, int column) {
    stmt->line = line;
    stmt->column = column;
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
    case AST_EXPR_NULL:
        printf("Nothing\n");
        break;
    case AST_EXPR_UNKNOWN:
        printf("Unknown\n");
        break;
    case AST_EXPR_ARRAY:
        printf("Array\n");
        for (size_t i = 0; i < expr->as.array.count; i++) {
            dump_expr(expr->as.array.items[i], indent + 1);
        }
        break;
    case AST_EXPR_RECORD:
        printf("Record\n");
        for (size_t i = 0; i < expr->as.record.count; i++) {
            dump_indent(indent + 1);
            printf("Field %s\n", expr->as.record.items[i].name);
            dump_expr(expr->as.record.items[i].value, indent + 2);
        }
        break;
    case AST_EXPR_DURATION:
        printf("Duration years=%d months=%d weeks=%d days=%d hours=%d minutes=%d seconds=%d\n",
               expr->as.duration.years,
               expr->as.duration.months,
               expr->as.duration.weeks,
               expr->as.duration.days,
               expr->as.duration.hours,
               expr->as.duration.minutes,
               expr->as.duration.seconds);
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
    case AST_EXPR_FIELD:
        printf("FieldAccess %s\n", expr->as.field.field);
        dump_expr(expr->as.field.object, indent + 1);
        break;
    case AST_EXPR_CALL:
        if (expr->as.call.library) {
            printf("Call %s.%s\n", expr->as.call.library, expr->as.call.name);
        } else {
            printf("Call %s\n", expr->as.call.name);
        }
        for (size_t i = 0; i < expr->as.call.args.count; i++) {
            dump_expr(expr->as.call.args.items[i], indent + 1);
        }
        break;
    case AST_EXPR_BINARY:
        if (expr->as.binary.modifier.name) {
            if (expr->as.binary.modifier.library) {
                printf("Binary %s modifier(%s.%s)\n",
                       expr->as.binary.op,
                       expr->as.binary.modifier.library,
                       expr->as.binary.modifier.name);
            } else {
                printf("Binary %s modifier(%s)\n", expr->as.binary.op, expr->as.binary.modifier.name);
            }
            for (size_t i = 0; i < expr->as.binary.modifier.args.count; i++) {
                dump_expr(expr->as.binary.modifier.args.items[i], indent + 1);
            }
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
        if (stmt->as.assign.modifier.name) {
            if (stmt->as.assign.modifier.library) {
                printf("Assign %s modifier(%s.%s)\n",
                       stmt->as.assign.name,
                       stmt->as.assign.modifier.library,
                       stmt->as.assign.modifier.name);
            } else {
                printf("Assign %s modifier(%s)\n", stmt->as.assign.name, stmt->as.assign.modifier.name);
            }
            for (size_t i = 0; i < stmt->as.assign.modifier.args.count; i++) {
                dump_expr(stmt->as.assign.modifier.args.items[i], indent + 1);
            }
        } else {
            printf("Assign %s\n", stmt->as.assign.name);
        }
        dump_expr(stmt->as.assign.value, indent + 1);
        break;
    case AST_STMT_FIELD_ASSIGN:
        printf("FieldAssign %s.%s\n", stmt->as.field_assign.name, stmt->as.field_assign.field);
        dump_expr(stmt->as.field_assign.value, indent + 1);
        break;
    case AST_STMT_PRINT:
        printf("Print\n");
        dump_expr(stmt->as.print, indent + 1);
        break;
    case AST_STMT_EXPR:
        printf("ExpressionStatement\n");
        dump_expr(stmt->as.expr_stmt, indent + 1);
        break;
    case AST_STMT_WITH_LOCK:
        printf("WithLock\n");
        dump_indent(indent + 1);
        printf("File\n");
        dump_expr(stmt->as.with_lock.file, indent + 2);
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.with_lock.body.count; i++) {
            dump_stmt(stmt->as.with_lock.body.items[i], indent + 2);
        }
        break;
    case AST_STMT_FOR_EACH:
        printf("ForEach %s\n", stmt->as.for_each.name);
        dump_indent(indent + 1);
        printf("Iterable\n");
        dump_expr(stmt->as.for_each.iterable, indent + 2);
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.for_each.body.count; i++) {
            dump_stmt(stmt->as.for_each.body.items[i], indent + 2);
        }
        break;
    case AST_STMT_FUNCTION:
        printf("Function %s\n", stmt->as.function.name);
        dump_indent(indent + 1);
        printf("Parameters");
        for (size_t i = 0; i < stmt->as.function.params.count; i++) {
            printf(" %s", stmt->as.function.params.items[i]);
        }
        printf("\n");
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.function.body.count; i++) {
            dump_stmt(stmt->as.function.body.items[i], indent + 2);
        }
        break;
    case AST_STMT_RETURN:
        printf("Return\n");
        if (stmt->as.return_expr) {
            dump_expr(stmt->as.return_expr, indent + 1);
        }
        break;
    case AST_STMT_LABEL:
        printf("Label %s\n", stmt->as.label);
        break;
    case AST_STMT_GOTO:
        printf("Goto %s\n", stmt->as.goto_label);
        break;
    case AST_STMT_GOSUB:
        printf("Gosub %s\n", stmt->as.gosub_label);
        break;
    case AST_STMT_WATCH:
        printf("Watch");
        for (size_t i = 0; i < stmt->as.watch.names.count; i++) {
            printf(" %s", stmt->as.watch.names.items[i]);
        }
        printf("\n");
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.watch.body.count; i++) {
            dump_stmt(stmt->as.watch.body.items[i], indent + 2);
        }
        break;
    case AST_STMT_WITHOUT_WATCHERS:
        printf("WithoutWatchers\n");
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.without_watchers.count; i++) {
            dump_stmt(stmt->as.without_watchers.items[i], indent + 2);
        }
        break;
    case AST_STMT_ON_ERROR_GOTO:
        printf("OnErrorGoto %s\n", stmt->as.on_error_label);
        break;
    case AST_STMT_ON_ERROR_RESUME_NEXT:
        printf("OnErrorResumeNext\n");
        break;
    case AST_STMT_ON_ERROR_STOP:
        printf("OnErrorStop\n");
        break;
    case AST_STMT_ERROR:
        printf("Error\n");
        dump_expr(stmt->as.error_message, indent + 1);
        break;
    case AST_STMT_MODIFIER:
        printf("%sModifier %s for %s\n",
               stmt->as.modifier.exported ? "Export " : "",
               stmt->as.modifier.name,
               stmt->as.modifier.context);
        dump_indent(indent + 1);
        printf("Parameters");
        for (size_t i = 0; i < stmt->as.modifier.params.count; i++) {
            printf(" %s", stmt->as.modifier.params.items[i]);
        }
        printf("\n");
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.modifier.body.count; i++) {
            dump_stmt(stmt->as.modifier.body.items[i], indent + 2);
        }
        break;
    case AST_STMT_PROGRAM:
        printf("ProgramBlock %s\n", stmt->as.program.name);
        dump_indent(indent + 1);
        printf("Args");
        for (size_t i = 0; i < stmt->as.program.args.count; i++) {
            printf(" %s", stmt->as.program.args.items[i]);
        }
        printf("\n");
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.program.body.count; i++) {
            dump_stmt(stmt->as.program.body.items[i], indent + 2);
        }
        break;
    case AST_STMT_LIBRARY:
        printf("Library %s\n", stmt->as.library.name);
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.library.body.count; i++) {
            dump_stmt(stmt->as.library.body.items[i], indent + 2);
        }
        break;
    case AST_STMT_USE:
        if (stmt->as.use_stmt.path) {
            printf("Use %s from \"%s\"\n", stmt->as.use_stmt.name, stmt->as.use_stmt.path);
        } else {
            printf("Use %s\n", stmt->as.use_stmt.name);
        }
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
        if (stmt->as.if_stmt.else_body.count > 0) {
            dump_indent(indent + 1);
            printf("Else\n");
            for (size_t i = 0; i < stmt->as.if_stmt.else_body.count; i++) {
                dump_stmt(stmt->as.if_stmt.else_body.items[i], indent + 2);
            }
        }
        break;
    case AST_STMT_WHILE:
        printf("While\n");
        dump_indent(indent + 1);
        printf("Condition\n");
        dump_expr(stmt->as.while_stmt.condition, indent + 2);
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.while_stmt.body.count; i++) {
            dump_stmt(stmt->as.while_stmt.body.items[i], indent + 2);
        }
        break;
    case AST_STMT_BREAK:
        printf("Break\n");
        break;
    case AST_STMT_CONTINUE:
        printf("Continue\n");
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
        free(expr->as.binary.modifier.library);
        free(expr->as.binary.modifier.name);
        for (size_t i = 0; i < expr->as.binary.modifier.args.count; i++) {
            free_expr(expr->as.binary.modifier.args.items[i]);
        }
        free(expr->as.binary.modifier.args.items);
        free_expr(expr->as.binary.left);
        free_expr(expr->as.binary.right);
        break;
    case AST_EXPR_ARRAY:
        for (size_t i = 0; i < expr->as.array.count; i++) {
            free_expr(expr->as.array.items[i]);
        }
        free(expr->as.array.items);
        break;
    case AST_EXPR_RECORD:
        for (size_t i = 0; i < expr->as.record.count; i++) {
            free(expr->as.record.items[i].name);
            free_expr(expr->as.record.items[i].value);
        }
        free(expr->as.record.items);
        break;
    case AST_EXPR_DURATION:
        break;
    case AST_EXPR_INDEX:
        free_expr(expr->as.index.array);
        free_expr(expr->as.index.index);
        break;
    case AST_EXPR_FIELD:
        free_expr(expr->as.field.object);
        free(expr->as.field.field);
        break;
    case AST_EXPR_CALL:
        free(expr->as.call.library);
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
    case AST_EXPR_NULL:
    case AST_EXPR_UNKNOWN:
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
        free(stmt->as.assign.modifier.library);
        free(stmt->as.assign.modifier.name);
        for (size_t i = 0; i < stmt->as.assign.modifier.args.count; i++) {
            free_expr(stmt->as.assign.modifier.args.items[i]);
        }
        free(stmt->as.assign.modifier.args.items);
        free_expr(stmt->as.assign.value);
        break;
    case AST_STMT_FIELD_ASSIGN:
        free(stmt->as.field_assign.name);
        free(stmt->as.field_assign.field);
        free_expr(stmt->as.field_assign.value);
        break;
    case AST_STMT_PRINT:
        free_expr(stmt->as.print);
        break;
    case AST_STMT_EXPR:
        free_expr(stmt->as.expr_stmt);
        break;
    case AST_STMT_WITH_LOCK:
        free_expr(stmt->as.with_lock.file);
        ast_free_program(stmt->as.with_lock.body);
        break;
    case AST_STMT_FOR_EACH:
        free(stmt->as.for_each.name);
        free_expr(stmt->as.for_each.iterable);
        ast_free_program(stmt->as.for_each.body);
        break;
    case AST_STMT_FUNCTION:
        free(stmt->as.function.name);
        for (size_t i = 0; i < stmt->as.function.params.count; i++) {
            free(stmt->as.function.params.items[i]);
        }
        free(stmt->as.function.params.items);
        ast_free_program(stmt->as.function.body);
        break;
    case AST_STMT_RETURN:
        free_expr(stmt->as.return_expr);
        break;
    case AST_STMT_LABEL:
        free(stmt->as.label);
        break;
    case AST_STMT_GOTO:
        free(stmt->as.goto_label);
        break;
    case AST_STMT_GOSUB:
        free(stmt->as.gosub_label);
        break;
    case AST_STMT_WATCH:
        for (size_t i = 0; i < stmt->as.watch.names.count; i++) {
            free(stmt->as.watch.names.items[i]);
        }
        free(stmt->as.watch.names.items);
        ast_free_program(stmt->as.watch.body);
        break;
    case AST_STMT_WITHOUT_WATCHERS:
        ast_free_program(stmt->as.without_watchers);
        break;
    case AST_STMT_ON_ERROR_GOTO:
        free(stmt->as.on_error_label);
        break;
    case AST_STMT_ON_ERROR_RESUME_NEXT:
    case AST_STMT_ON_ERROR_STOP:
        break;
    case AST_STMT_ERROR:
        free_expr(stmt->as.error_message);
        break;
    case AST_STMT_MODIFIER:
        free(stmt->as.modifier.name);
        for (size_t i = 0; i < stmt->as.modifier.params.count; i++) {
            free(stmt->as.modifier.params.items[i]);
        }
        free(stmt->as.modifier.params.items);
        free(stmt->as.modifier.context);
        ast_free_program(stmt->as.modifier.body);
        break;
    case AST_STMT_PROGRAM:
        free(stmt->as.program.name);
        for (size_t i = 0; i < stmt->as.program.args.count; i++) {
            free(stmt->as.program.args.items[i]);
        }
        free(stmt->as.program.args.items);
        ast_free_program(stmt->as.program.body);
        break;
    case AST_STMT_LIBRARY:
        free(stmt->as.library.name);
        ast_free_program(stmt->as.library.body);
        break;
    case AST_STMT_USE:
        free(stmt->as.use_stmt.name);
        free(stmt->as.use_stmt.path);
        break;
    case AST_STMT_IF:
        free_expr(stmt->as.if_stmt.condition);
        ast_free_program(stmt->as.if_stmt.body);
        ast_free_program(stmt->as.if_stmt.else_body);
        break;
    case AST_STMT_WHILE:
        free_expr(stmt->as.while_stmt.condition);
        ast_free_program(stmt->as.while_stmt.body);
        break;
    case AST_STMT_BREAK:
    case AST_STMT_CONTINUE:
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

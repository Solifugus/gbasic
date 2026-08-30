#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    return ast_record_field_list_append_policy(list, name, value, AST_FIELD_POLICY_COPY, NULL);
}

AstRecordFieldList ast_record_field_list_append_policy(AstRecordFieldList list, char *name, AstExpr *value, AstFieldPolicy policy, AstExpr *reset_expr) {
    list.items = xrealloc(list.items, sizeof(AstRecordField) * (list.count + 1));
    list.items[list.count].name = name;
    list.items[list.count].value = value;
    list.items[list.count].policy = policy;
    list.items[list.count].reset_expr = reset_expr;
    list.count++;
    return list;
}

AstConsiderBranchList ast_consider_branch_list_empty(void) {
    AstConsiderBranchList list = {0};
    return list;
}

AstConsiderBranchList ast_consider_branch_list_append(AstConsiderBranchList list, AstExpr *match, AstStmtList body) {
    list.items = xrealloc(list.items, sizeof(AstConsiderBranch) * (list.count + 1));
    list.items[list.count].match = match;
    list.items[list.count].body = body;
    list.count++;
    return list;
}

AstNameList ast_name_list_empty(void) {
    AstNameList list = {0};
    return list;
}

static AstNameList name_list_push(AstNameList list, char *name, AstExpr *dflt) {
    list.items = xrealloc(list.items, sizeof(char *) * (list.count + 1));
    list.defaults = xrealloc(list.defaults, sizeof(AstExpr *) * (list.count + 1));
    list.items[list.count] = name;
    list.defaults[list.count] = dflt;
    list.count++;
    if (!dflt) {
        /* `required` is the count of leading parameters with no default. A
         * default followed by a required parameter is a gap, and the caller
         * checks for it by comparing this against the position. */
        if (list.required == list.count - 1) {
            list.required = list.count;
        }
    }
    return list;
}

AstNameList ast_name_list_append(AstNameList list, char *name) {
    return name_list_push(list, name, NULL);
}

AstNameList ast_name_list_append_default(AstNameList list, char *name,
                                         AstExpr *default_value) {
    return name_list_push(list, name, default_value);
}

static AstExpr *ast_expr_new(AstExprKind kind) {
    AstExpr *expr = xmalloc(sizeof(*expr));
    expr->kind = kind;
    expr->line = 0;
    expr->column = 0;
    return expr;
}

AstExpr *ast_number(double value) {
    AstExpr *expr = ast_expr_new(AST_EXPR_NUMBER);
    expr->as.number = value;
    return expr;
}

AstExpr *ast_string(char *value) {
    AstExpr *expr = ast_expr_new(AST_EXPR_STRING);
    expr->as.string = value;
    return expr;
}

AstExpr *ast_ident(char *name) {
    AstExpr *expr = ast_expr_new(AST_EXPR_IDENT);
    expr->as.ident = name;
    return expr;
}

AstExpr *ast_bool(int value) {
    AstExpr *expr = ast_expr_new(AST_EXPR_BOOL);
    expr->as.boolean = value;
    return expr;
}

AstExpr *ast_null(void) {
    AstExpr *expr = ast_expr_new(AST_EXPR_NULL);
    return expr;
}

AstExpr *ast_unknown(void) {
    AstExpr *expr = ast_expr_new(AST_EXPR_UNKNOWN);
    return expr;
}

AstExpr *ast_array(AstExprList items) {
    AstExpr *expr = ast_expr_new(AST_EXPR_ARRAY);
    expr->as.array = items;
    return expr;
}

AstExpr *ast_record(AstRecordFieldList fields) {
    AstExpr *expr = ast_expr_new(AST_EXPR_RECORD);
    expr->as.record = fields;
    return expr;
}

AstExpr *ast_duration(AstDuration duration) {
    AstExpr *expr = ast_expr_new(AST_EXPR_DURATION);
    expr->as.duration = duration;
    return expr;
}

AstExpr *ast_index(AstExpr *array, AstExpr *index) {
    AstExpr *expr = ast_expr_new(AST_EXPR_INDEX);
    expr->as.index.array = array;
    expr->as.index.index = index;
    return expr;
}

AstExpr *ast_field(AstExpr *object, char *field) {
    AstExpr *expr = ast_expr_new(AST_EXPR_FIELD);
    expr->as.field.object = object;
    expr->as.field.field = field;
    return expr;
}

AstExpr *ast_call(char *name, AstExprList args) {
    AstExpr *expr = ast_expr_new(AST_EXPR_CALL);
    expr->as.call.library = NULL;
    expr->as.call.name = name;
    expr->as.call.args = args;
    expr->as.call.receiver = NULL;
    return expr;
}

AstExpr *ast_qualified_call(char *library, char *name, AstExprList args) {
    AstExpr *expr = ast_call(name, args);
    expr->as.call.library = library;
    return expr;
}

/* A method call whose receiver is an arbitrary expression (a.b.method(),
 * make().method()). library stays NULL; the evaluator evaluates `receiver` once
 * and dispatches by runtime kind. */
AstExpr *ast_method_call(AstExpr *receiver, char *name, AstExprList args) {
    AstExpr *expr = ast_call(name, args);
    expr->as.call.receiver = receiver;
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
    AstExpr *expr = ast_expr_new(AST_EXPR_BINARY);
    expr->as.binary.op = op;
    expr->as.binary.modifier = modifier;
    expr->as.binary.left = left;
    expr->as.binary.right = right;
    return expr;
}

AstExpr *ast_unary(char *op, AstExpr *child) {
    AstExpr *expr = ast_expr_new(AST_EXPR_UNARY);
    expr->as.unary.op = op;
    expr->as.unary.expr = child;
    return expr;
}

AstExpr *ast_new(AstExpr *proto, AstExpr *with) {
    AstExpr *expr = ast_expr_new(AST_EXPR_NEW);
    expr->as.derive.proto = proto;
    expr->as.derive.with = with;
    return expr;
}

AstExpr *ast_spawn(char *name, AstExprList args) {
    /* spawn reuses the call shape: a named entry function plus its arguments. */
    AstExpr *expr = ast_expr_new(AST_EXPR_SPAWN);
    expr->as.call.library = NULL;
    expr->as.call.name = name;
    expr->as.call.args = args;
    return expr;
}

AstExpr *ast_expr_position(AstExpr *expr, int line, int column) {
    if (!expr) {
        return NULL;
    }
    expr->line = line;
    expr->column = column;
    return expr;
}

AstStmt *ast_assign(AstExpr *target, AstModifierUse modifier, AstExpr *value) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_ASSIGN;
    stmt->as.assign.target = target;
    stmt->as.assign.modifier = modifier;
    stmt->as.assign.value = value;
    stmt->as.assign.op = 0;
    return stmt;
}

AstStmt *ast_assign_op(AstExpr *target, AstModifierUse modifier, AstExpr *value, char op) {
    AstStmt *stmt = ast_assign(target, modifier, value);
    stmt->as.assign.op = op;
    return stmt;
}

AstStmt *ast_print(AstExpr *expr) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_PRINT;
    stmt->as.print.expr = expr;
    stmt->as.print.to_stderr = 0;
    return stmt;
}

/* PLAT-STDERR: `print to error <expression>` -- the same statement with standard
 * error as its destination. */
AstStmt *ast_print_error(AstExpr *expr) {
    AstStmt *stmt = ast_print(expr);
    stmt->as.print.to_stderr = 1;
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

AstStmt *ast_do_loop(AstStmtList body, AstExpr *condition) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_DO_LOOP;
    stmt->as.do_loop.body = body;
    stmt->as.do_loop.condition = condition;
    return stmt;
}

AstStmt *ast_for_range(char *name, AstExpr *start, AstExpr *limit,
                       AstExpr *step, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_FOR_RANGE;
    stmt->as.for_range.name = name;
    stmt->as.for_range.start = start;
    stmt->as.for_range.limit = limit;
    stmt->as.for_range.step = step;   /* NULL means 1 */
    stmt->as.for_range.body = body;
    return stmt;
}

AstStmt *ast_function(char *name, AstNameList params, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_FUNCTION;
    stmt->as.function.params = params;
    stmt->as.function.body = body;
    stmt->as.function.object = NULL;
    stmt->as.function.field = NULL;
    stmt->as.function.source_path = NULL;   /* stamped at registration, see ast.h */
    /* A dotted name (`function obj.method()`) is the define-and-attach sugar: an
     * executable statement, not a hoisted declaration (first_class_functions_design
     * §6-7). Split it now; the internal registered name is generated lazily at
     * eval time from source position (it must be deterministic for §10). */
    char *dot = strchr(name, '.');
    if (dot) {
        size_t object_len = (size_t)(dot - name);
        char *object = xmalloc(object_len + 1);
        memcpy(object, name, object_len);
        object[object_len] = '\0';
        stmt->as.function.object = object;
        size_t field_len = strlen(dot + 1);
        char *field = xmalloc(field_len + 1);
        memcpy(field, dot + 1, field_len + 1);
        stmt->as.function.field = field;
        stmt->as.function.name = NULL;   /* filled in at registration time */
        free(name);
    } else {
        stmt->as.function.name = name;
    }
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

AstStmt *ast_watch(char *name, AstNameList names, AstStmtList body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_WATCH;
    stmt->as.watch.name = name;          /* NULL for the anonymous form */
    stmt->as.watch.names = names;
    stmt->as.watch.body = body;
    return stmt;
}

AstServerItemList ast_server_item_list_empty(void) {
    AstServerItemList list = {NULL, 0};
    return list;
}

AstServerItemList ast_server_item_list_append(AstServerItemList list, AstServerItem *item) {
    AstServerItem **items = realloc(list.items, sizeof(*items) * (list.count + 1));
    if (!items) {
        abort();
    }
    items[list.count] = item;
    list.items = items;
    list.count = list.count + 1;
    return list;
}

static AstServerItem *server_item_new(AstServerItemKind kind, int line, int column) {
    AstServerItem *item = xmalloc(sizeof(*item));
    memset(item, 0, sizeof(*item));
    item->kind = kind;
    item->line = line;
    item->column = column;
    return item;
}

AstServerItem *ast_server_directive(char *word, AstNameList strings, int line, int column) {
    AstServerItem *item = server_item_new(AST_SERVER_DIRECTIVE, line, column);
    item->word = word;
    item->strings = strings;
    return item;
}

AstServerItem *ast_server_handler(char *word, char *path, AstNameList params,
                                  AstStmtList body, char *close_word, int line, int column) {
    AstServerItem *item = server_item_new(AST_SERVER_HANDLER, line, column);
    item->word = word;
    item->path = path;
    item->params = params;
    item->body = body;
    item->close_word = close_word;
    return item;
}

AstServerItem *ast_server_site(char *word, char *name, AstRecordFieldList options,
                               AstServerItemList entries, char *close_word, int line, int column) {
    AstServerItem *item = server_item_new(AST_SERVER_SITE, line, column);
    item->word = word;
    item->name = name;
    item->options = options;
    item->entries = entries;
    item->close_word = close_word;
    return item;
}

AstServerItem *ast_server_hook(char *word, AstStmtList body, int line, int column) {
    AstServerItem *item = server_item_new(AST_SERVER_HOOK, line, column);
    item->word = word;
    item->body = body;
    return item;
}

AstStmt *ast_server(char *word, char *name, AstRecordFieldList options,
                    AstServerItemList items, char *close_word) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_SERVER;
    stmt->as.server.word = word;
    stmt->as.server.name = name;
    stmt->as.server.options = options;
    stmt->as.server.items = items;
    stmt->as.server.close_word = close_word;
    return stmt;
}

AstStmt *ast_unwatch(AstExpr *expr) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_UNWATCH;
    stmt->as.unwatch_expr = expr;
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

AstStmt *ast_on_error_goto_next(void) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_ON_ERROR_GOTO_NEXT;
    return stmt;
}

AstStmt *ast_on_error_stop(void) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_ON_ERROR_STOP;
    return stmt;
}

AstStmt *ast_on_warning(int mode) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_ON_WARNING;
    stmt->as.warn_mode = mode;
    return stmt;
}

AstStmt *ast_warning(AstExpr *message) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_WARNING;
    stmt->as.error_message = message;
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

AstStmt *ast_consider(AstExpr *subject, AstConsiderBranchList branches, AstStmtList else_body) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_CONSIDER;
    stmt->as.consider.subject = subject;
    stmt->as.consider.branches = branches;
    stmt->as.consider.else_body = else_body;
    return stmt;
}

AstStmt *ast_break(char *target) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_BREAK;
    stmt->as.loop_target = target;
    return stmt;
}

AstStmt *ast_continue(char *target) {
    AstStmt *stmt = xmalloc(sizeof(*stmt));
    stmt->kind = AST_STMT_CONTINUE;
    stmt->as.loop_target = target;
    return stmt;
}

AstStmt *ast_stmt_position(AstStmt *stmt, int line, int column) {
    stmt->line = line;
    stmt->column = column;
    stmt->end_line = 0;
    stmt->end_column = 0;
    return stmt;
}

AstStmt *ast_stmt_span(AstStmt *stmt, int line, int column,
                       int end_line, int end_column) {
    stmt->line = line;
    stmt->column = column;
    stmt->end_line = end_line;
    stmt->end_column = end_column;
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
            AstRecordField *field = &expr->as.record.items[i];
            dump_indent(indent + 1);
            /* COPY is the default and prints no suffix, so existing AST dumps
             * for un-annotated records stay byte-for-byte unchanged. */
            const char *policy_suffix =
                field->policy == AST_FIELD_POLICY_LINK ? " (link)" :
                field->policy == AST_FIELD_POLICY_RESET ? " (reset)" :
                field->policy == AST_FIELD_POLICY_EXCLUDE ? " (exclude)" : "";
            printf("Field %s%s\n", field->name, policy_suffix);
            if (field->policy == AST_FIELD_POLICY_RESET && field->reset_expr) {
                dump_indent(indent + 2);
                printf("ResetValue\n");
                dump_expr(field->reset_expr, indent + 3);
            }
            dump_expr(field->value, indent + 2);
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
        if (expr->as.call.receiver) {
            printf("MethodCall .%s\n", expr->as.call.name);
            dump_expr(expr->as.call.receiver, indent + 1);
        } else if (expr->as.call.library) {
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
    case AST_EXPR_NEW:
        printf("New\n");
        dump_indent(indent + 1);
        printf("Prototype\n");
        dump_expr(expr->as.derive.proto, indent + 2);
        if (expr->as.derive.with) {
            dump_indent(indent + 1);
            printf("With\n");
            dump_expr(expr->as.derive.with, indent + 2);
        }
        break;
    case AST_EXPR_SPAWN:
        printf("Spawn %s\n", expr->as.call.name);
        for (size_t i = 0; i < expr->as.call.args.count; i++) {
            dump_expr(expr->as.call.args.items[i], indent + 1);
        }
        break;
    }
}

static void dump_stmt(AstStmt *stmt, int indent);

static void dump_server_items(AstServerItemList items, int indent) {
    for (size_t i = 0; i < items.count; i++) {
        AstServerItem *item = items.items[i];
        dump_indent(indent);
        switch (item->kind) {
        case AST_SERVER_DIRECTIVE:
            printf("Directive %s", item->word);
            for (size_t j = 0; j < item->strings.count; j++) {
                printf(" \"%s\"", item->strings.items[j]);
            }
            printf("\n");
            break;
        case AST_SERVER_HANDLER:
            printf("Handler %s \"%s\"\n", item->word, item->path);
            for (size_t j = 0; j < item->body.count; j++) {
                dump_stmt(item->body.items[j], indent + 1);
            }
            break;
        case AST_SERVER_SITE:
            printf("Site %s \"%s\"\n", item->word, item->name);
            dump_server_items(item->entries, indent + 1);
            break;
        case AST_SERVER_HOOK:
            printf("Hook %s\n", item->word);
            for (size_t j = 0; j < item->body.count; j++) {
                dump_stmt(item->body.items[j], indent + 1);
            }
            break;
        }
    }
}

static void dump_stmt(AstStmt *stmt, int indent) {
    dump_indent(indent);
    switch (stmt->kind) {
    case AST_STMT_ASSIGN:
        if (stmt->as.assign.modifier.name) {
            if (stmt->as.assign.modifier.library) {
                printf("Assign modifier(%s.%s)\n",
                       stmt->as.assign.modifier.library,
                       stmt->as.assign.modifier.name);
            } else {
                printf("Assign modifier(%s)\n", stmt->as.assign.modifier.name);
            }
            dump_indent(indent + 1);
            printf("Target\n");
            dump_expr(stmt->as.assign.target, indent + 2);
            for (size_t i = 0; i < stmt->as.assign.modifier.args.count; i++) {
                dump_expr(stmt->as.assign.modifier.args.items[i], indent + 1);
            }
        } else {
            printf("Assign\n");
            dump_indent(indent + 1);
            printf("Target\n");
            dump_expr(stmt->as.assign.target, indent + 2);
        }
        dump_expr(stmt->as.assign.value, indent + 1);
        break;
    case AST_STMT_PRINT:
        printf("%s\n", stmt->as.print.to_stderr ? "PrintToError" : "Print");
        dump_expr(stmt->as.print.expr, indent + 1);
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
    case AST_STMT_DO_LOOP:
        printf("DoLoop until\n");
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.do_loop.body.count; i++) {
            dump_stmt(stmt->as.do_loop.body.items[i], indent + 2);
        }
        dump_indent(indent + 1);
        printf("Condition\n");
        dump_expr(stmt->as.do_loop.condition, indent + 2);
        break;
    case AST_STMT_FOR_RANGE:
        printf("ForRange %s\n", stmt->as.for_range.name);
        dump_indent(indent + 1);
        printf("Start\n");
        dump_expr(stmt->as.for_range.start, indent + 2);
        dump_indent(indent + 1);
        printf("Limit\n");
        dump_expr(stmt->as.for_range.limit, indent + 2);
        if (stmt->as.for_range.step) {
            dump_indent(indent + 1);
            printf("Step\n");
            dump_expr(stmt->as.for_range.step, indent + 2);
        }
        dump_indent(indent + 1);
        printf("Body\n");
        for (size_t i = 0; i < stmt->as.for_range.body.count; i++) {
            dump_stmt(stmt->as.for_range.body.items[i], indent + 2);
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
        if (stmt->as.function.object) {
            printf("Function %s.%s (attach)\n",
                   stmt->as.function.object, stmt->as.function.field);
        } else {
            printf("Function %s\n", stmt->as.function.name);
        }
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
        if (stmt->as.watch.name) {
            printf(" \"%s\"", stmt->as.watch.name);
        }
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
    case AST_STMT_SERVER:
        printf("Server %s \"%s\"\n", stmt->as.server.word, stmt->as.server.name);
        dump_server_items(stmt->as.server.items, indent + 1);
        break;
    case AST_STMT_ON_ERROR_GOTO:
        printf("OnErrorGoto %s\n", stmt->as.on_error_label);
        break;
    case AST_STMT_ON_ERROR_GOTO_NEXT:
        printf("OnErrorGotoNext\n");
        break;
    case AST_STMT_ON_WARNING:
        printf("OnWarning %d\n", stmt->as.warn_mode);
        break;
    case AST_STMT_WARNING:
        printf("Warning\n");
        dump_expr(stmt->as.error_message, indent + 1);
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
    case AST_STMT_CONSIDER:
        printf("Consider\n");
        dump_indent(indent + 1);
        printf("Subject\n");
        dump_expr(stmt->as.consider.subject, indent + 2);
        for (size_t i = 0; i < stmt->as.consider.branches.count; i++) {
            dump_indent(indent + 1);
            printf("Case\n");
            dump_expr(stmt->as.consider.branches.items[i].match, indent + 2);
            dump_indent(indent + 1);
            printf("Body\n");
            for (size_t j = 0; j < stmt->as.consider.branches.items[i].body.count; j++) {
                dump_stmt(stmt->as.consider.branches.items[i].body.items[j], indent + 2);
            }
        }
        if (stmt->as.consider.else_body.count > 0) {
            dump_indent(indent + 1);
            printf("Else\n");
            for (size_t i = 0; i < stmt->as.consider.else_body.count; i++) {
                dump_stmt(stmt->as.consider.else_body.items[i], indent + 2);
            }
        }
        break;
    case AST_STMT_BREAK:
        printf("Break%s%s\n",
               stmt->as.loop_target ? " " : "",
               stmt->as.loop_target ? stmt->as.loop_target : "");
        break;
    case AST_STMT_CONTINUE:
        printf("Continue%s%s\n",
               stmt->as.loop_target ? " " : "",
               stmt->as.loop_target ? stmt->as.loop_target : "");
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
            free_expr(expr->as.record.items[i].reset_expr);
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
        free_expr(expr->as.call.receiver);
        for (size_t i = 0; i < expr->as.call.args.count; i++) {
            free_expr(expr->as.call.args.items[i]);
        }
        free(expr->as.call.args.items);
        break;
    case AST_EXPR_UNARY:
        free(expr->as.unary.op);
        free_expr(expr->as.unary.expr);
        break;
    case AST_EXPR_NEW:
        free_expr(expr->as.derive.proto);
        free_expr(expr->as.derive.with);
        break;
    case AST_EXPR_SPAWN:
        free(expr->as.call.name);
        for (size_t i = 0; i < expr->as.call.args.count; i++) {
            free_expr(expr->as.call.args.items[i]);
        }
        free(expr->as.call.args.items);
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
        free_expr(stmt->as.assign.target);
        free(stmt->as.assign.modifier.library);
        free(stmt->as.assign.modifier.name);
        for (size_t i = 0; i < stmt->as.assign.modifier.args.count; i++) {
            free_expr(stmt->as.assign.modifier.args.items[i]);
        }
        free(stmt->as.assign.modifier.args.items);
        free_expr(stmt->as.assign.value);
        break;
    case AST_STMT_PRINT:
        free_expr(stmt->as.print.expr);
        break;
    case AST_STMT_EXPR:
        free_expr(stmt->as.expr_stmt);
        break;
    case AST_STMT_WITH_LOCK:
        free_expr(stmt->as.with_lock.file);
        ast_free_program(stmt->as.with_lock.body);
        break;
    case AST_STMT_DO_LOOP:
        ast_free_program(stmt->as.do_loop.body);
        free_expr(stmt->as.do_loop.condition);
        break;
    case AST_STMT_FOR_RANGE:
        free(stmt->as.for_range.name);
        free_expr(stmt->as.for_range.start);
        free_expr(stmt->as.for_range.limit);
        free_expr(stmt->as.for_range.step);
        ast_free_program(stmt->as.for_range.body);
        break;
    case AST_STMT_FOR_EACH:
        free(stmt->as.for_each.name);
        free_expr(stmt->as.for_each.iterable);
        ast_free_program(stmt->as.for_each.body);
        break;
    case AST_STMT_FUNCTION:
        free(stmt->as.function.name);
        free(stmt->as.function.object);
        free(stmt->as.function.field);
        free(stmt->as.function.source_path);
        ast_free_name_list(stmt->as.function.params);
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
        free(stmt->as.watch.name);
        for (size_t i = 0; i < stmt->as.watch.names.count; i++) {
            free(stmt->as.watch.names.items[i]);
        }
        free(stmt->as.watch.names.items);
        free(stmt->as.watch.names.defaults);
        ast_free_program(stmt->as.watch.body);
        break;
    case AST_STMT_UNWATCH:
        ast_free_expr(stmt->as.unwatch_expr);
        break;
    case AST_STMT_WITHOUT_WATCHERS:
        ast_free_program(stmt->as.without_watchers);
        break;
    case AST_STMT_ON_ERROR_GOTO:
        free(stmt->as.on_error_label);
        break;
    case AST_STMT_ON_ERROR_GOTO_NEXT:
    case AST_STMT_ON_ERROR_STOP:
    case AST_STMT_ON_WARNING:
        break;
    case AST_STMT_WARNING:
    case AST_STMT_ERROR:
        free_expr(stmt->as.error_message);
        break;
    case AST_STMT_MODIFIER:
        free(stmt->as.modifier.name);
        ast_free_name_list(stmt->as.modifier.params);
        free(stmt->as.modifier.context);
        ast_free_program(stmt->as.modifier.body);
        break;
    case AST_STMT_PROGRAM:
        free(stmt->as.program.name);
        for (size_t i = 0; i < stmt->as.program.args.count; i++) {
            free(stmt->as.program.args.items[i]);
        }
        free(stmt->as.program.args.items);
        free(stmt->as.program.args.defaults);
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
    case AST_STMT_CONSIDER:
        free_expr(stmt->as.consider.subject);
        for (size_t i = 0; i < stmt->as.consider.branches.count; i++) {
            free_expr(stmt->as.consider.branches.items[i].match);
            ast_free_program(stmt->as.consider.branches.items[i].body);
        }
        free(stmt->as.consider.branches.items);
        ast_free_program(stmt->as.consider.else_body);
        break;
    case AST_STMT_BREAK:
    case AST_STMT_CONTINUE:
        free(stmt->as.loop_target);
        break;
    case AST_STMT_SERVER:
        free(stmt->as.server.word);
        free(stmt->as.server.name);
        free(stmt->as.server.close_word);
        ast_free_record_field_list(stmt->as.server.options);
        ast_free_server_item_list(stmt->as.server.items);
        break;
    }

    free(stmt);
}

void ast_free_server_item_list(AstServerItemList list) {
    for (size_t i = 0; i < list.count; i++) {
        AstServerItem *item = list.items[i];
        free(item->word);
        free(item->path);
        free(item->name);
        free(item->close_word);
        free(item->fn_name);
        if (item->fn_stmt) {
            /* the registration shell: its name (and stamped source path) are
             * its own copies; params/body are THIS item's and are freed below.
             * The AST outlives every call, so the registry's pointer is dead
             * weight by the time this runs. */
            free(item->fn_stmt->as.function.name);
            free(item->fn_stmt->as.function.source_path);
            free(item->fn_stmt);
        }
        ast_free_name_list(item->strings);
        ast_free_record_field_list(item->options);
        ast_free_name_list(item->params);
        ast_free_program(item->body);
        ast_free_server_item_list(item->entries);
        free(item);
    }
    free(list.items);
}

void ast_free_program(AstStmtList program) {
    for (size_t i = 0; i < program.count; i++) {
        free_stmt(program.items[i]);
    }
    free(program.items);
}

/* Public single-node frees + aggregate frees, used by the parser's %destructor
 * rules to release semantic values discarded during error recovery (they mirror
 * the internal free_expr/free_stmt/ast_free_program above). All accept the empty/
 * NULL case. */
void ast_free_expr(AstExpr *expr) {
    free_expr(expr);
}

void ast_free_stmt(AstStmt *stmt) {
    free_stmt(stmt);
}

void ast_free_expr_list(AstExprList list) {
    for (size_t i = 0; i < list.count; i++) {
        free_expr(list.items[i]);
    }
    free(list.items);
}

void ast_free_name_list(AstNameList list) {
    for (size_t i = 0; i < list.count; i++) {
        free(list.items[i]);
        if (list.defaults && list.defaults[i]) {
            free_expr(list.defaults[i]);
        }
    }
    free(list.items);
    free(list.defaults);
}

void ast_free_record_field_list(AstRecordFieldList list) {
    for (size_t i = 0; i < list.count; i++) {
        free(list.items[i].name);
        free_expr(list.items[i].value);
        free_expr(list.items[i].reset_expr);
    }
    free(list.items);
}

void ast_free_consider_branch_list(AstConsiderBranchList list) {
    for (size_t i = 0; i < list.count; i++) {
        free_expr(list.items[i].match);
        ast_free_program(list.items[i].body);
    }
    free(list.items);
}

void ast_free_modifier_use(AstModifierUse modifier) {
    free(modifier.library);
    free(modifier.name);
    ast_free_expr_list(modifier.args);
}

void ast_free_modifier_signature(AstModifierSignature sig) {
    free(sig.name);
    ast_free_name_list(sig.params);
}

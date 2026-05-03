#include "eval.h"

#include "value.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    Value value;
} Binding;

typedef struct {
    Binding *items;
    size_t count;
} Env;

static Env env = {0};

static char *xstrdup(const char *src) {
    const char *text = src ? src : "";
    char *copy = malloc(strlen(text) + 1);
    if (!copy) {
        abort();
    }
    strcpy(copy, text);
    return copy;
}

static int modifier_is(Modifier *modifier, const char *name) {
    return modifier && modifier->count == 1 && strcmp(modifier->terms[0], name) == 0;
}

static Binding *env_find(const char *name) {
    for (size_t i = 0; i < env.count; i++) {
        if (strcmp(env.items[i].name, name) == 0) {
            return &env.items[i];
        }
    }
    return NULL;
}

static void env_set(const char *name, Value value) {
    Binding *binding = env_find(name);
    if (binding) {
        value_free(binding->value);
        binding->value = value;
        return;
    }
    Binding *items = realloc(env.items, sizeof(Binding) * (env.count + 1));
    if (!items) {
        abort();
    }
    env.items = items;
    env.items[env.count].name = xstrdup(name);
    env.items[env.count].value = value;
    env.count++;
}

static Value env_get(const char *name) {
    Binding *binding = env_find(name);
    if (!binding) {
        fprintf(stderr, "undefined variable '%s'\n", name);
        return value_null();
    }
    return value_copy(binding->value);
}

static int string_equal_caseless(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static double as_number(Value value) {
    switch (value.kind) {
    case VAL_NUMBER:
        return value.as.number;
    case VAL_MONEY:
        return value.as.money.amount;
    case VAL_BOOL:
        return value.as.boolean ? 1.0 : 0.0;
    default:
        return 0.0;
    }
}

static Value eval_expr(Expr *expr);

static Value eval_call(Expr *expr) {
    const char *name = expr->as.call.name;
    ExprList args = expr->as.call.args;

    if ((strcmp(name, "mean") == 0 || strcmp(name, "mode") == 0) && args.count == 1) {
        Value array = eval_expr(args.items[0]);
        if (array.kind != VAL_ARRAY || array.as.array.count == 0) {
            value_free(array);
            return value_null();
        }

        if (strcmp(name, "mean") == 0) {
            double sum = 0.0;
            for (size_t i = 0; i < array.as.array.count; i++) {
                sum += as_number(array.as.array.items[i]);
            }
            double result = sum / (double)array.as.array.count;
            value_free(array);
            return value_number(result);
        }

        double best = as_number(array.as.array.items[0]);
        size_t best_count = 0;
        for (size_t i = 0; i < array.as.array.count; i++) {
            double candidate = as_number(array.as.array.items[i]);
            size_t count = 0;
            for (size_t j = 0; j < array.as.array.count; j++) {
                if (as_number(array.as.array.items[j]) == candidate) {
                    count++;
                }
            }
            if (count > best_count) {
                best = candidate;
                best_count = count;
            }
        }
        value_free(array);
        return value_number(best);
    }

    fprintf(stderr, "unknown function '%s'\n", name);
    return value_null();
}

static Value compare_values(const char *op, Modifier *modifier, Value left, Value right) {
    int result = 0;

    if (modifier_is(modifier, "caseless") && left.kind == VAL_STRING && right.kind == VAL_STRING) {
        int equal = string_equal_caseless(left.as.string, right.as.string);
        if (strcmp(op, "=") == 0) {
            result = equal;
        } else if (strcmp(op, "!=") == 0) {
            result = !equal;
        }
    } else if (left.kind == VAL_STRING && right.kind == VAL_STRING) {
        int cmp = strcmp(left.as.string, right.as.string);
        if (strcmp(op, "=") == 0) result = cmp == 0;
        else if (strcmp(op, "!=") == 0) result = cmp != 0;
        else if (strcmp(op, ">") == 0) result = cmp > 0;
        else if (strcmp(op, "<") == 0) result = cmp < 0;
        else if (strcmp(op, ">=") == 0) result = cmp >= 0;
        else if (strcmp(op, "<=") == 0) result = cmp <= 0;
        else if (strcmp(op, "!>") == 0) result = cmp <= 0;
        else if (strcmp(op, "!<") == 0) result = cmp >= 0;
    } else {
        double a = as_number(left);
        double b = as_number(right);
        if (strcmp(op, "=") == 0) result = a == b;
        else if (strcmp(op, "!=") == 0) result = a != b;
        else if (strcmp(op, ">") == 0) result = a > b;
        else if (strcmp(op, "<") == 0) result = a < b;
        else if (strcmp(op, ">=") == 0) result = a >= b;
        else if (strcmp(op, "<=") == 0) result = a <= b;
        else if (strcmp(op, "!>") == 0) result = !(a > b);
        else if (strcmp(op, "!<") == 0) result = !(a < b);
    }

    value_free(left);
    value_free(right);
    return value_bool(result);
}

static Value eval_binary(Expr *expr) {
    const char *op = expr->as.binary.op;

    if (strcmp(op, "and") == 0) {
        Value left = eval_expr(expr->as.binary.left);
        int truth = value_truthy(left);
        value_free(left);
        if (!truth) {
            return value_bool(0);
        }
        Value right = eval_expr(expr->as.binary.right);
        truth = value_truthy(right);
        value_free(right);
        return value_bool(truth);
    }
    if (strcmp(op, "or") == 0) {
        Value left = eval_expr(expr->as.binary.left);
        int truth = value_truthy(left);
        value_free(left);
        if (truth) {
            return value_bool(1);
        }
        Value right = eval_expr(expr->as.binary.right);
        truth = value_truthy(right);
        value_free(right);
        return value_bool(truth);
    }

    Value left = eval_expr(expr->as.binary.left);
    Value right = eval_expr(expr->as.binary.right);

    if (strchr("=<>!", op[0])) {
        return compare_values(op, expr->as.binary.modifier, left, right);
    }

    double a = as_number(left);
    double b = as_number(right);
    value_free(left);
    value_free(right);

    if (strcmp(op, "+") == 0) return value_number(a + b);
    if (strcmp(op, "-") == 0) return value_number(a - b);
    if (strcmp(op, "*") == 0) return value_number(a * b);
    if (strcmp(op, "/") == 0) return value_number(a / b);

    return value_null();
}

static Value eval_expr(Expr *expr) {
    switch (expr->kind) {
    case EXPR_NUMBER:
        return value_number(expr->as.number);
    case EXPR_STRING:
        return value_string(expr->as.string);
    case EXPR_BOOL:
        return value_bool(expr->as.boolean);
    case EXPR_VAR:
        return env_get(expr->as.var);
    case EXPR_ARRAY: {
        Value *items = NULL;
        if (expr->as.array.count) {
            items = malloc(sizeof(Value) * expr->as.array.count);
            if (!items) {
                abort();
            }
            for (size_t i = 0; i < expr->as.array.count; i++) {
                items[i] = eval_expr(expr->as.array.items[i]);
            }
        }
        return value_array(items, expr->as.array.count);
    }
    case EXPR_CALL:
        return eval_call(expr);
    case EXPR_BINARY:
        return eval_binary(expr);
    case EXPR_UNARY: {
        Value value = eval_expr(expr->as.unary.operand);
        if (strcmp(expr->as.unary.op, "not") == 0) {
            int truth = !value_truthy(value);
            value_free(value);
            return value_bool(truth);
        }
        if (strcmp(expr->as.unary.op, "-") == 0) {
            double number = -as_number(value);
            value_free(value);
            return value_number(number);
        }
        value_free(value);
        return value_null();
    }
    }
    return value_null();
}

static Value apply_assignment_modifier(Modifier *modifier, Value value) {
    if (!modifier) {
        return value;
    }
    if (modifier_is(modifier, "usd")) {
        double amount = as_number(value);
        value_free(value);
        return value_money(amount, "USD");
    }
    if (modifier_is(modifier, "date")) {
        if (value.kind == VAL_STRING) {
            char *date = xstrdup(value.as.string);
            value_free(value);
            Value out = value_date(date);
            free(date);
            return out;
        }
    }
    if (modifier_is(modifier, "file")) {
        if (value.kind == VAL_STRING) {
            char *path = xstrdup(value.as.string);
            value_free(value);
            Value out = value_file(path);
            free(path);
            return out;
        }
    }
    fprintf(stderr, "unsupported assignment modifier\n");
    return value;
}

static void eval_statements(StmtList list);

static void eval_stmt(Stmt *stmt) {
    switch (stmt->kind) {
    case STMT_ASSIGN: {
        Value value = eval_expr(stmt->as.assign.value);
        value = apply_assignment_modifier(stmt->as.assign.modifier, value);
        env_set(stmt->as.assign.name, value);
        break;
    }
    case STMT_PRINT: {
        Value value = eval_expr(stmt->as.print);
        value_print(value);
        value_free(value);
        break;
    }
    case STMT_IF: {
        Value condition = eval_expr(stmt->as.if_stmt.condition);
        int truth = value_truthy(condition);
        value_free(condition);
        eval_statements(truth ? stmt->as.if_stmt.then_branch : stmt->as.if_stmt.else_branch);
        break;
    }
    }
}

static void eval_statements(StmtList list) {
    for (size_t i = 0; i < list.count; i++) {
        eval_stmt(list.items[i]);
    }
}

int eval_program(StmtList program) {
    eval_statements(program);
    for (size_t i = 0; i < env.count; i++) {
        free(env.items[i].name);
        value_free(env.items[i].value);
    }
    free(env.items);
    env.items = NULL;
    env.count = 0;
    return 0;
}

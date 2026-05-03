#include "eval.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    VALUE_NULL,
    VALUE_NUMBER,
    VALUE_STRING,
    VALUE_BOOL,
    VALUE_ARRAY
} ValueKind;

typedef struct {
    ValueKind kind;
    union {
        double number;
        char *string;
        int boolean;
        struct {
            double *items;
            size_t count;
        } array;
    } as;
} Value;

typedef struct {
    char *name;
    Value value;
} Symbol;

typedef struct {
    Symbol *items;
    size_t count;
} Env;

static Env env = {0};

static char *copy_string(const char *text) {
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) {
        abort();
    }
    memcpy(copy, text, length + 1);
    return copy;
}

static Value value_null(void) {
    Value value = {0};
    value.kind = VALUE_NULL;
    return value;
}

static Value value_number(double number) {
    Value value = {0};
    value.kind = VALUE_NUMBER;
    value.as.number = number;
    return value;
}

static Value value_string(const char *string) {
    Value value = {0};
    value.kind = VALUE_STRING;
    value.as.string = copy_string(string);
    return value;
}

static Value value_bool(int boolean) {
    Value value = {0};
    value.kind = VALUE_BOOL;
    value.as.boolean = boolean != 0;
    return value;
}

static Value value_array(double *items, size_t count) {
    Value value = {0};
    value.kind = VALUE_ARRAY;
    value.as.array.items = items;
    value.as.array.count = count;
    return value;
}

static Value value_copy(Value value) {
    if (value.kind == VALUE_STRING) {
        return value_string(value.as.string);
    }
    if (value.kind == VALUE_ARRAY) {
        double *items = NULL;
        if (value.as.array.count > 0) {
            items = malloc(sizeof(double) * value.as.array.count);
            if (!items) {
                abort();
            }
            memcpy(items, value.as.array.items, sizeof(double) * value.as.array.count);
        }
        return value_array(items, value.as.array.count);
    }
    return value;
}

static void value_free(Value value) {
    if (value.kind == VALUE_STRING) {
        free(value.as.string);
    } else if (value.kind == VALUE_ARRAY) {
        free(value.as.array.items);
    }
}

static int value_truthy(Value value) {
    switch (value.kind) {
    case VALUE_BOOL:
        return value.as.boolean;
    case VALUE_NUMBER:
        return value.as.number != 0.0;
    case VALUE_STRING:
        return value.as.string[0] != '\0';
    case VALUE_ARRAY:
        return value.as.array.count > 0;
    case VALUE_NULL:
        return 0;
    }
    return 0;
}

static double value_number_or_zero(Value value) {
    if (value.kind == VALUE_NUMBER) {
        return value.as.number;
    }
    if (value.kind == VALUE_BOOL) {
        return value.as.boolean ? 1.0 : 0.0;
    }
    return 0.0;
}

static void value_print(Value value) {
    switch (value.kind) {
    case VALUE_NULL:
        printf("nothing\n");
        break;
    case VALUE_NUMBER:
        printf("%g\n", value.as.number);
        break;
    case VALUE_STRING:
        printf("%s\n", value.as.string);
        break;
    case VALUE_BOOL:
        printf("%s\n", value.as.boolean ? "true" : "false");
        break;
    case VALUE_ARRAY:
        printf("[");
        for (size_t i = 0; i < value.as.array.count; i++) {
            if (i > 0) {
                printf(", ");
            }
            printf("%g", value.as.array.items[i]);
        }
        printf("]\n");
        break;
    }
}

static Symbol *env_find(const char *name) {
    for (size_t i = 0; i < env.count; i++) {
        if (strcmp(env.items[i].name, name) == 0) {
            return &env.items[i];
        }
    }
    return NULL;
}

static void env_set(const char *name, Value value) {
    Symbol *symbol = env_find(name);
    if (symbol) {
        value_free(symbol->value);
        symbol->value = value;
        return;
    }

    Symbol *items = realloc(env.items, sizeof(Symbol) * (env.count + 1));
    if (!items) {
        abort();
    }
    env.items = items;
    env.items[env.count].name = copy_string(name);
    env.items[env.count].value = value;
    env.count++;
}

static Value env_get(const char *name) {
    Symbol *symbol = env_find(name);
    if (!symbol) {
        fprintf(stderr, "undefined variable: %s\n", name);
        return value_null();
    }
    return value_copy(symbol->value);
}

static void env_clear(void) {
    for (size_t i = 0; i < env.count; i++) {
        free(env.items[i].name);
        value_free(env.items[i].value);
    }
    free(env.items);
    env.items = NULL;
    env.count = 0;
}

static int string_equal_caseless(const char *left, const char *right) {
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }
        left++;
        right++;
    }
    return *left == *right;
}

static int modifier_is(const char *modifier, const char *name) {
    return modifier && strcmp(modifier, name) == 0;
}

static Value eval_expr(AstExpr *expr);

static int number_compare(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static Value eval_call(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        fprintf(stderr, "%s expects one array argument\n", expr->as.call.name);
        return value_null();
    }

    Value arg = eval_expr(expr->as.call.args.items[0]);
    if (arg.kind != VALUE_ARRAY) {
        fprintf(stderr, "%s expects an array\n", expr->as.call.name);
        value_free(arg);
        return value_null();
    }

    const char *name = expr->as.call.name;
    size_t count = arg.as.array.count;
    double result = 0.0;

    if (strcmp(name, "len") == 0) {
        result = (double)count;
    } else if (count == 0) {
        fprintf(stderr, "%s expects a non-empty array\n", name);
        value_free(arg);
        return value_null();
    } else if (strcmp(name, "sum") == 0 || strcmp(name, "mean") == 0) {
        for (size_t i = 0; i < count; i++) {
            result += arg.as.array.items[i];
        }
        if (strcmp(name, "mean") == 0) {
            result /= (double)count;
        }
    } else if (strcmp(name, "min") == 0) {
        result = arg.as.array.items[0];
        for (size_t i = 1; i < count; i++) {
            if (arg.as.array.items[i] < result) {
                result = arg.as.array.items[i];
            }
        }
    } else if (strcmp(name, "max") == 0) {
        result = arg.as.array.items[0];
        for (size_t i = 1; i < count; i++) {
            if (arg.as.array.items[i] > result) {
                result = arg.as.array.items[i];
            }
        }
    } else if (strcmp(name, "median") == 0) {
        double *sorted = malloc(sizeof(double) * count);
        if (!sorted) {
            abort();
        }
        memcpy(sorted, arg.as.array.items, sizeof(double) * count);
        qsort(sorted, count, sizeof(double), number_compare);
        if (count % 2 == 1) {
            result = sorted[count / 2];
        } else {
            result = (sorted[count / 2 - 1] + sorted[count / 2]) / 2.0;
        }
        free(sorted);
    } else if (strcmp(name, "mode") == 0) {
        result = arg.as.array.items[0];
        size_t best_count = 0;
        for (size_t i = 0; i < count; i++) {
            size_t current_count = 0;
            for (size_t j = 0; j < count; j++) {
                if (arg.as.array.items[i] == arg.as.array.items[j]) {
                    current_count++;
                }
            }
            if (current_count > best_count) {
                best_count = current_count;
                result = arg.as.array.items[i];
            }
        }
    } else {
        fprintf(stderr, "unknown function: %s\n", name);
        value_free(arg);
        return value_null();
    }

    value_free(arg);
    return value_number(result);
}

static Value eval_comparison(AstExpr *expr, Value left, Value right) {
    const char *op = expr->as.binary.op;
    int result = 0;

    if (modifier_is(expr->as.binary.modifier, "caseless") &&
        left.kind == VALUE_STRING &&
        right.kind == VALUE_STRING) {
        int equal = string_equal_caseless(left.as.string, right.as.string);
        if (strcmp(op, "=") == 0) {
            result = equal;
        } else if (strcmp(op, "!=") == 0) {
            result = !equal;
        }
    } else if (left.kind == VALUE_STRING && right.kind == VALUE_STRING) {
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
        double a = value_number_or_zero(left);
        double b = value_number_or_zero(right);
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

static Value eval_binary(AstExpr *expr) {
    const char *op = expr->as.binary.op;

    if (strcmp(op, "and") == 0) {
        Value left = eval_expr(expr->as.binary.left);
        int left_truth = value_truthy(left);
        value_free(left);
        if (!left_truth) {
            return value_bool(0);
        }
        Value right = eval_expr(expr->as.binary.right);
        int right_truth = value_truthy(right);
        value_free(right);
        return value_bool(right_truth);
    }

    if (strcmp(op, "or") == 0) {
        Value left = eval_expr(expr->as.binary.left);
        int left_truth = value_truthy(left);
        value_free(left);
        if (left_truth) {
            return value_bool(1);
        }
        Value right = eval_expr(expr->as.binary.right);
        int right_truth = value_truthy(right);
        value_free(right);
        return value_bool(right_truth);
    }

    Value left = eval_expr(expr->as.binary.left);
    Value right = eval_expr(expr->as.binary.right);

    if (strcmp(op, "=") == 0 ||
        strcmp(op, "!=") == 0 ||
        strcmp(op, ">") == 0 ||
        strcmp(op, "<") == 0 ||
        strcmp(op, ">=") == 0 ||
        strcmp(op, "<=") == 0 ||
        strcmp(op, "!>") == 0 ||
        strcmp(op, "!<") == 0) {
        return eval_comparison(expr, left, right);
    }

    double a = value_number_or_zero(left);
    double b = value_number_or_zero(right);
    value_free(left);
    value_free(right);

    if (strcmp(op, "+") == 0) return value_number(a + b);
    if (strcmp(op, "-") == 0) return value_number(a - b);
    if (strcmp(op, "*") == 0) return value_number(a * b);
    if (strcmp(op, "/") == 0) return value_number(a / b);

    return value_null();
}

static Value eval_expr(AstExpr *expr) {
    switch (expr->kind) {
    case AST_EXPR_NUMBER:
        return value_number(expr->as.number);
    case AST_EXPR_STRING:
        return value_string(expr->as.string);
    case AST_EXPR_IDENT:
        return env_get(expr->as.ident);
    case AST_EXPR_BOOL:
        return value_bool(expr->as.boolean);
    case AST_EXPR_ARRAY: {
        double *items = NULL;
        if (expr->as.array.count > 0) {
            items = malloc(sizeof(double) * expr->as.array.count);
            if (!items) {
                abort();
            }
        }
        for (size_t i = 0; i < expr->as.array.count; i++) {
            Value item = eval_expr(expr->as.array.items[i]);
            if (item.kind != VALUE_NUMBER) {
                fprintf(stderr, "array literals currently support numbers only\n");
                items[i] = 0.0;
            } else {
                items[i] = item.as.number;
            }
            value_free(item);
        }
        return value_array(items, expr->as.array.count);
    }
    case AST_EXPR_INDEX: {
        Value array = eval_expr(expr->as.index.array);
        Value index = eval_expr(expr->as.index.index);
        if (array.kind != VALUE_ARRAY || index.kind != VALUE_NUMBER) {
            fprintf(stderr, "indexing expects array[number]\n");
            value_free(array);
            value_free(index);
            return value_null();
        }
        int position = (int)index.as.number;
        if (position < 0 || (size_t)position >= array.as.array.count) {
            fprintf(stderr, "array index out of range\n");
            value_free(array);
            value_free(index);
            return value_null();
        }
        double result = array.as.array.items[position];
        value_free(array);
        value_free(index);
        return value_number(result);
    }
    case AST_EXPR_CALL:
        return eval_call(expr);
    case AST_EXPR_BINARY:
        return eval_binary(expr);
    case AST_EXPR_UNARY: {
        Value value = eval_expr(expr->as.unary.expr);
        if (strcmp(expr->as.unary.op, "not") == 0) {
            int result = !value_truthy(value);
            value_free(value);
            return value_bool(result);
        }
        if (strcmp(expr->as.unary.op, "-") == 0) {
            double result = -value_number_or_zero(value);
            value_free(value);
            return value_number(result);
        }
        value_free(value);
        return value_null();
    }
    }

    return value_null();
}

static Value apply_assignment_modifier(const char *modifier, Value value) {
    if (!modifier) {
        return value;
    }

    if (strcmp(modifier, "USD") == 0) {
        /* TODO: add a money runtime value. For now USD stores the numeric amount. */
        double amount = value_number_or_zero(value);
        value_free(value);
        return value_number(amount);
    }

    fprintf(stderr, "unsupported assignment modifier: %s\n", modifier);
    return value;
}

static void eval_stmt_list(AstStmtList statements);

static void eval_stmt(AstStmt *stmt) {
    switch (stmt->kind) {
    case AST_STMT_ASSIGN: {
        Value value = eval_expr(stmt->as.assign.value);
        value = apply_assignment_modifier(stmt->as.assign.modifier, value);
        env_set(stmt->as.assign.name, value);
        break;
    }
    case AST_STMT_PRINT: {
        Value value = eval_expr(stmt->as.print);
        value_print(value);
        value_free(value);
        break;
    }
    case AST_STMT_IF: {
        Value condition = eval_expr(stmt->as.if_stmt.condition);
        int truth = value_truthy(condition);
        value_free(condition);
        if (truth) {
            eval_stmt_list(stmt->as.if_stmt.body);
        }
        break;
    }
    }
}

static void eval_stmt_list(AstStmtList statements) {
    for (size_t i = 0; i < statements.count; i++) {
        eval_stmt(statements.items[i]);
    }
}

int eval_program(AstStmtList program) {
    eval_stmt_list(program);
    env_clear();
    return 0;
}

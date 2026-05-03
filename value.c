#include "value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *xstrdup(const char *src) {
    const char *text = src ? src : "";
    char *copy = malloc(strlen(text) + 1);
    if (!copy) {
        abort();
    }
    strcpy(copy, text);
    return copy;
}

Value value_null(void) {
    Value value = {0};
    value.kind = VAL_NULL;
    return value;
}

Value value_number(double number) {
    Value value = {0};
    value.kind = VAL_NUMBER;
    value.as.number = number;
    return value;
}

Value value_string(const char *string) {
    Value value = {0};
    value.kind = VAL_STRING;
    value.as.string = xstrdup(string);
    return value;
}

Value value_bool(int boolean) {
    Value value = {0};
    value.kind = VAL_BOOL;
    value.as.boolean = boolean != 0;
    return value;
}

Value value_array(Value *items, size_t count) {
    Value value = {0};
    value.kind = VAL_ARRAY;
    value.as.array.items = items;
    value.as.array.count = count;
    return value;
}

Value value_money(double amount, const char *currency) {
    Value value = {0};
    value.kind = VAL_MONEY;
    value.as.money.amount = amount;
    value.as.money.currency = xstrdup(currency);
    return value;
}

Value value_date(const char *date) {
    Value value = {0};
    value.kind = VAL_DATE;
    value.as.date = xstrdup(date);
    return value;
}

Value value_file(const char *path) {
    Value value = {0};
    value.kind = VAL_FILE;
    value.as.file = xstrdup(path);
    return value;
}

Value value_copy(Value value) {
    switch (value.kind) {
    case VAL_STRING:
        return value_string(value.as.string);
    case VAL_ARRAY: {
        Value *items = NULL;
        if (value.as.array.count) {
            items = malloc(sizeof(Value) * value.as.array.count);
            if (!items) {
                abort();
            }
            for (size_t i = 0; i < value.as.array.count; i++) {
                items[i] = value_copy(value.as.array.items[i]);
            }
        }
        return value_array(items, value.as.array.count);
    }
    case VAL_MONEY:
        return value_money(value.as.money.amount, value.as.money.currency);
    case VAL_DATE:
        return value_date(value.as.date);
    case VAL_FILE:
        return value_file(value.as.file);
    case VAL_NUMBER:
    case VAL_BOOL:
    case VAL_NULL:
        return value;
    }
    return value_null();
}

void value_free(Value value) {
    switch (value.kind) {
    case VAL_STRING:
        free(value.as.string);
        break;
    case VAL_ARRAY:
        for (size_t i = 0; i < value.as.array.count; i++) {
            value_free(value.as.array.items[i]);
        }
        free(value.as.array.items);
        break;
    case VAL_MONEY:
        free(value.as.money.currency);
        break;
    case VAL_DATE:
        free(value.as.date);
        break;
    case VAL_FILE:
        free(value.as.file);
        break;
    case VAL_NUMBER:
    case VAL_BOOL:
    case VAL_NULL:
        break;
    }
}

int value_truthy(Value value) {
    switch (value.kind) {
    case VAL_BOOL:
        return value.as.boolean;
    case VAL_NUMBER:
        return value.as.number != 0.0;
    case VAL_STRING:
        return value.as.string[0] != '\0';
    case VAL_ARRAY:
        return value.as.array.count > 0;
    case VAL_MONEY:
        return value.as.money.amount != 0.0;
    case VAL_DATE:
    case VAL_FILE:
        return 1;
    case VAL_NULL:
        return 0;
    }
    return 0;
}

void value_print(Value value) {
    switch (value.kind) {
    case VAL_NULL:
        printf("null\n");
        break;
    case VAL_NUMBER:
        printf("%g\n", value.as.number);
        break;
    case VAL_STRING:
        printf("%s\n", value.as.string);
        break;
    case VAL_BOOL:
        printf("%s\n", value.as.boolean ? "true" : "false");
        break;
    case VAL_ARRAY:
        printf("[");
        for (size_t i = 0; i < value.as.array.count; i++) {
            if (i) {
                printf(", ");
            }
            if (value.as.array.items[i].kind == VAL_NUMBER) {
                printf("%g", value.as.array.items[i].as.number);
            } else {
                printf("?");
            }
        }
        printf("]\n");
        break;
    case VAL_MONEY:
        printf("%s %.2f\n", value.as.money.currency, value.as.money.amount);
        break;
    case VAL_DATE:
        printf("%s\n", value.as.date);
        break;
    case VAL_FILE:
        printf("%s\n", value.as.file);
        break;
    }
}

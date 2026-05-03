#ifndef GBASIC_VALUE_H
#define GBASIC_VALUE_H

#include <stddef.h>

typedef enum {
    VAL_NULL,
    VAL_NUMBER,
    VAL_STRING,
    VAL_BOOL,
    VAL_ARRAY,
    VAL_MONEY,
    VAL_DATE,
    VAL_FILE
} ValueKind;

typedef struct Value Value;

typedef struct {
    Value *items;
    size_t count;
} ValueArray;

struct Value {
    ValueKind kind;
    union {
        double number;
        char *string;
        int boolean;
        ValueArray array;
        struct {
            double amount;
            char *currency;
        } money;
        char *date;
        char *file;
    } as;
};

Value value_null(void);
Value value_number(double number);
Value value_string(const char *string);
Value value_bool(int boolean);
Value value_array(Value *items, size_t count);
Value value_money(double amount, const char *currency);
Value value_date(const char *date);
Value value_file(const char *path);
Value value_copy(Value value);
void value_free(Value value);
int value_truthy(Value value);
void value_print(Value value);

#endif

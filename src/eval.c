#include "eval.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum {
    VALUE_NULL,
    VALUE_NUMBER,
    VALUE_STRING,
    VALUE_BOOL,
    VALUE_ARRAY,
    VALUE_RECORD,
    VALUE_DATETIME,
    VALUE_DURATION,
    VALUE_FILE,
    VALUE_DIR
} ValueKind;

typedef enum {
    PREC_YEAR = 1,
    PREC_MONTH,
    PREC_DAY,
    PREC_HOUR,
    PREC_MINUTE,
    PREC_SECOND
} DateTimePrecision;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int time_only;
    DateTimePrecision precision;
} DateTime;

typedef struct {
    int years;
    int months;
    int weeks;
    int days;
    int hours;
    int minutes;
    int seconds;
} Duration;

typedef struct Value Value;

typedef struct {
    char *name;
    Value *value;
} RecordField;

struct Value {
    ValueKind kind;
    union {
        double number;
        char *string;
        int boolean;
        struct {
            Value *items;
            size_t count;
        } array;
        struct {
            RecordField *fields;
            size_t count;
        } record;
        DateTime datetime;
        Duration duration;
        char *file_path;
        char *dir_path;
    } as;
};

typedef struct {
    char *name;
    Value value;
} Symbol;

typedef struct {
    char *path;
    int fd;
    int depth;
} LockEntry;

typedef struct {
    int did_return;
    int did_goto;
    char *goto_label;
    Value value;
} EvalResult;

typedef struct Env {
    Symbol *items;
    size_t count;
    struct Env *parent;
} Env;

typedef struct {
    char *name;
    AstStmt *stmt;
} FunctionDef;

static Env global_env = {0};
static Env *current_env = &global_env;
static FunctionDef *functions = NULL;
static size_t function_count = 0;
static LockEntry *locks = NULL;
static size_t lock_count = 0;
static int function_depth = 0;

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

static Value value_array(Value *items, size_t count) {
    Value value = {0};
    value.kind = VALUE_ARRAY;
    value.as.array.items = items;
    value.as.array.count = count;
    return value;
}

static Value value_record(RecordField *fields, size_t count) {
    Value value = {0};
    value.kind = VALUE_RECORD;
    value.as.record.fields = fields;
    value.as.record.count = count;
    return value;
}

static Value value_datetime(DateTime datetime) {
    Value value = {0};
    value.kind = VALUE_DATETIME;
    value.as.datetime = datetime;
    return value;
}

static Value value_duration(Duration duration) {
    Value value = {0};
    value.kind = VALUE_DURATION;
    value.as.duration = duration;
    return value;
}

static Value value_file(const char *path) {
    Value value = {0};
    value.kind = VALUE_FILE;
    value.as.file_path = copy_string(path);
    return value;
}

static Value value_dir(const char *path) {
    Value value = {0};
    value.kind = VALUE_DIR;
    value.as.dir_path = copy_string(path);
    return value;
}

static EvalResult eval_no_result(void) {
    EvalResult result = {0};
    result.value = value_null();
    return result;
}

static EvalResult eval_return(Value value) {
    EvalResult result = {0};
    result.did_return = 1;
    result.value = value;
    return result;
}

static EvalResult eval_goto(const char *label) {
    EvalResult result = {0};
    result.did_goto = 1;
    result.goto_label = copy_string(label);
    result.value = value_null();
    return result;
}

static Value value_copy(Value value) {
    if (value.kind == VALUE_STRING) {
        return value_string(value.as.string);
    }
    if (value.kind == VALUE_FILE) {
        return value_file(value.as.file_path);
    }
    if (value.kind == VALUE_DIR) {
        return value_dir(value.as.dir_path);
    }
    if (value.kind == VALUE_ARRAY) {
        Value *items = NULL;
        if (value.as.array.count > 0) {
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
    if (value.kind == VALUE_RECORD) {
        RecordField *fields = NULL;
        if (value.as.record.count > 0) {
            fields = malloc(sizeof(RecordField) * value.as.record.count);
            if (!fields) {
                abort();
            }
            for (size_t i = 0; i < value.as.record.count; i++) {
                fields[i].name = copy_string(value.as.record.fields[i].name);
                fields[i].value = malloc(sizeof(Value));
                if (!fields[i].value) {
                    abort();
                }
                *fields[i].value = value_copy(*value.as.record.fields[i].value);
            }
        }
        return value_record(fields, value.as.record.count);
    }
    return value;
}

static void value_free(Value value) {
    if (value.kind == VALUE_STRING) {
        free(value.as.string);
    } else if (value.kind == VALUE_FILE) {
        free(value.as.file_path);
    } else if (value.kind == VALUE_DIR) {
        free(value.as.dir_path);
    } else if (value.kind == VALUE_ARRAY) {
        for (size_t i = 0; i < value.as.array.count; i++) {
            value_free(value.as.array.items[i]);
        }
        free(value.as.array.items);
    } else if (value.kind == VALUE_RECORD) {
        for (size_t i = 0; i < value.as.record.count; i++) {
            free(value.as.record.fields[i].name);
            value_free(*value.as.record.fields[i].value);
            free(value.as.record.fields[i].value);
        }
        free(value.as.record.fields);
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
    case VALUE_RECORD:
        return value.as.record.count > 0;
    case VALUE_DATETIME:
        return 1;
    case VALUE_DURATION:
        return value.as.duration.years || value.as.duration.months ||
            value.as.duration.weeks || value.as.duration.days ||
            value.as.duration.hours || value.as.duration.minutes ||
            value.as.duration.seconds;
    case VALUE_FILE:
        return value.as.file_path[0] != '\0';
    case VALUE_DIR:
        return value.as.dir_path[0] != '\0';
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
            if (value.as.array.items[i].kind == VALUE_NUMBER) {
                printf("%g", value.as.array.items[i].as.number);
            } else {
                printf("?");
            }
        }
        printf("]\n");
        break;
    case VALUE_RECORD:
        printf("{record}\n");
        break;
    case VALUE_DATETIME:
        if (value.as.datetime.time_only) {
            if (value.as.datetime.precision == PREC_HOUR) {
                printf("%02d\n", value.as.datetime.hour);
            } else if (value.as.datetime.precision == PREC_MINUTE) {
                printf("%02d:%02d\n", value.as.datetime.hour, value.as.datetime.minute);
            } else {
                printf("%02d:%02d:%02d\n",
                       value.as.datetime.hour,
                       value.as.datetime.minute,
                       value.as.datetime.second);
            }
        } else if (value.as.datetime.precision == PREC_YEAR) {
            printf("%04d\n", value.as.datetime.year);
        } else if (value.as.datetime.precision == PREC_MONTH) {
            printf("%04d-%02d\n", value.as.datetime.year, value.as.datetime.month);
        } else if (value.as.datetime.precision == PREC_DAY) {
            printf("%04d-%02d-%02d\n",
                   value.as.datetime.year,
                   value.as.datetime.month,
                   value.as.datetime.day);
        } else if (value.as.datetime.precision == PREC_HOUR) {
            printf("%04d-%02d-%02d %02d\n",
                   value.as.datetime.year,
                   value.as.datetime.month,
                   value.as.datetime.day,
                   value.as.datetime.hour);
        } else if (value.as.datetime.precision == PREC_MINUTE) {
            printf("%04d-%02d-%02d %02d:%02d\n",
                   value.as.datetime.year,
                   value.as.datetime.month,
                   value.as.datetime.day,
                   value.as.datetime.hour,
                   value.as.datetime.minute);
        } else {
            printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                   value.as.datetime.year,
                   value.as.datetime.month,
                   value.as.datetime.day,
                   value.as.datetime.hour,
                   value.as.datetime.minute,
                   value.as.datetime.second);
        }
        break;
    case VALUE_DURATION:
        printf("{duration}\n");
        break;
    case VALUE_FILE:
        printf("%s\n", value.as.file_path);
        break;
    case VALUE_DIR:
        printf("%s\n", value.as.dir_path);
        break;
    }
}

static Symbol *env_find_in_frame(Env *env, const char *name) {
    for (size_t i = 0; i < env->count; i++) {
        if (strcmp(env->items[i].name, name) == 0) {
            return &env->items[i];
        }
    }
    return NULL;
}

static Symbol *env_find(const char *name) {
    for (Env *env = current_env; env; env = env->parent) {
        Symbol *symbol = env_find_in_frame(env, name);
        if (symbol) {
            return symbol;
        }
    }
    return NULL;
}

static void env_set(const char *name, Value value) {
    Symbol *symbol = env_find_in_frame(current_env, name);
    if (symbol) {
        value_free(symbol->value);
        symbol->value = value;
        return;
    }

    Symbol *items = realloc(current_env->items, sizeof(Symbol) * (current_env->count + 1));
    if (!items) {
        abort();
    }
    current_env->items = items;
    current_env->items[current_env->count].name = copy_string(name);
    current_env->items[current_env->count].value = value;
    current_env->count++;
}

static Value env_get(const char *name) {
    Symbol *symbol = env_find(name);
    if (!symbol) {
        fprintf(stderr, "undefined variable: %s\n", name);
        return value_null();
    }
    return value_copy(symbol->value);
}

static void env_clear(Env *env) {
    for (size_t i = 0; i < env->count; i++) {
        free(env->items[i].name);
        value_free(env->items[i].value);
    }
    free(env->items);
    env->items = NULL;
    env->count = 0;
}

static LockEntry *lock_find(const char *path) {
    for (size_t i = 0; i < lock_count; i++) {
        if (strcmp(locks[i].path, path) == 0) {
            return &locks[i];
        }
    }
    return NULL;
}

static int lock_path(const char *path) {
    LockEntry *entry = lock_find(path);
    if (entry) {
        entry->depth++;
        return 1;
    }

    int fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror(path);
        return 0;
    }
    if (flock(fd, LOCK_EX) != 0) {
        perror(path);
        close(fd);
        return 0;
    }

    LockEntry *next = realloc(locks, sizeof(LockEntry) * (lock_count + 1));
    if (!next) {
        abort();
    }
    locks = next;
    locks[lock_count].path = copy_string(path);
    locks[lock_count].fd = fd;
    locks[lock_count].depth = 1;
    lock_count++;
    return 1;
}

static int unlock_path(const char *path) {
    for (size_t i = 0; i < lock_count; i++) {
        if (strcmp(locks[i].path, path) == 0) {
            locks[i].depth--;
            if (locks[i].depth > 0) {
                return 1;
            }
            int ok = flock(locks[i].fd, LOCK_UN) == 0;
            close(locks[i].fd);
            free(locks[i].path);
            locks[i] = locks[lock_count - 1];
            lock_count--;
            if (lock_count == 0) {
                free(locks);
                locks = NULL;
            }
            return ok;
        }
    }
    return 0;
}

static void lock_clear(void) {
    while (lock_count > 0) {
        flock(locks[lock_count - 1].fd, LOCK_UN);
        close(locks[lock_count - 1].fd);
        free(locks[lock_count - 1].path);
        lock_count--;
    }
    free(locks);
    locks = NULL;
}

static RecordField *record_find(Value *record, const char *name) {
    if (record->kind != VALUE_RECORD) {
        return NULL;
    }
    for (size_t i = 0; i < record->as.record.count; i++) {
        if (strcmp(record->as.record.fields[i].name, name) == 0) {
            return &record->as.record.fields[i];
        }
    }
    return NULL;
}

static void record_set(Value *record, const char *name, Value value) {
    RecordField *field = record_find(record, name);
    if (field) {
        value_free(*field->value);
        *field->value = value;
        return;
    }

    RecordField *fields = realloc(record->as.record.fields,
                                  sizeof(RecordField) * (record->as.record.count + 1));
    if (!fields) {
        abort();
    }
    record->as.record.fields = fields;
    field = &record->as.record.fields[record->as.record.count];
    field->name = copy_string(name);
    field->value = malloc(sizeof(Value));
    if (!field->value) {
        abort();
    }
    *field->value = value;
    record->as.record.count++;
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

static int all_digits(const char *text, int start, int count) {
    for (int i = 0; i < count; i++) {
        if (!isdigit((unsigned char)text[start + i])) {
            return 0;
        }
    }
    return 1;
}

static int parse_int_span(const char *text, int start, int count) {
    int value = 0;
    for (int i = 0; i < count; i++) {
        value = value * 10 + (text[start + i] - '0');
    }
    return value;
}

static int valid_date_parts(DateTime dt) {
    if (dt.time_only) {
        if (dt.hour < 0 || dt.hour > 23) {
            return 0;
        }
        if (dt.precision >= PREC_MINUTE && (dt.minute < 0 || dt.minute > 59)) {
            return 0;
        }
        if (dt.precision >= PREC_SECOND && (dt.second < 0 || dt.second > 59)) {
            return 0;
        }
        return 1;
    }
    if (dt.month < 1 || dt.month > 12) {
        return 0;
    }
    if (dt.precision >= PREC_DAY && (dt.day < 1 || dt.day > 31)) {
        return 0;
    }
    if (dt.precision >= PREC_HOUR && (dt.hour < 0 || dt.hour > 23)) {
        return 0;
    }
    if (dt.precision >= PREC_MINUTE && (dt.minute < 0 || dt.minute > 59)) {
        return 0;
    }
    if (dt.precision >= PREC_SECOND && (dt.second < 0 || dt.second > 59)) {
        return 0;
    }
    return 1;
}

static int parse_date_value(const char *text, DateTime *out) {
    size_t len = strlen(text);
    DateTime dt = {0};
    dt.month = 1;
    dt.day = 1;

    if (len < 4 || !all_digits(text, 0, 4)) {
        return 0;
    }
    dt.year = parse_int_span(text, 0, 4);
    dt.precision = PREC_YEAR;
    if (len == 4) {
        *out = dt;
        return 1;
    }

    if (len < 7 || text[4] != '-' || !all_digits(text, 5, 2)) {
        return 0;
    }
    dt.month = parse_int_span(text, 5, 2);
    dt.precision = PREC_MONTH;
    if (len == 7) {
        if (!valid_date_parts(dt)) {
            return 0;
        }
        *out = dt;
        return 1;
    }

    if (len < 10 || text[7] != '-' || !all_digits(text, 8, 2)) {
        return 0;
    }
    dt.day = parse_int_span(text, 8, 2);
    dt.precision = PREC_DAY;
    if (len == 10) {
        if (!valid_date_parts(dt)) {
            return 0;
        }
        *out = dt;
        return 1;
    }

    if (len < 13 || text[10] != ' ' || !all_digits(text, 11, 2)) {
        return 0;
    }
    dt.hour = parse_int_span(text, 11, 2);
    dt.precision = PREC_HOUR;
    if (len == 13) {
        if (!valid_date_parts(dt)) {
            return 0;
        }
        *out = dt;
        return 1;
    }

    if (len < 16 || text[13] != ':' || !all_digits(text, 14, 2)) {
        return 0;
    }
    dt.minute = parse_int_span(text, 14, 2);
    dt.precision = PREC_MINUTE;
    if (len == 16) {
        if (!valid_date_parts(dt)) {
            return 0;
        }
        *out = dt;
        return 1;
    }

    if (len != 19 || text[16] != ':' || !all_digits(text, 17, 2)) {
        return 0;
    }
    dt.second = parse_int_span(text, 17, 2);
    dt.precision = PREC_SECOND;
    if (!valid_date_parts(dt)) {
        return 0;
    }
    *out = dt;
    return 1;
}

static int parse_time_value(const char *text, DateTime *out) {
    size_t len = strlen(text);
    DateTime dt = {0};
    dt.time_only = 1;

    if (len < 2 || !all_digits(text, 0, 2)) {
        return 0;
    }
    dt.hour = parse_int_span(text, 0, 2);
    dt.precision = PREC_HOUR;
    if (len == 2) {
        if (!valid_date_parts(dt)) {
            return 0;
        }
        *out = dt;
        return 1;
    }

    if (len < 5 || text[2] != ':' || !all_digits(text, 3, 2)) {
        return 0;
    }
    dt.minute = parse_int_span(text, 3, 2);
    dt.precision = PREC_MINUTE;
    if (len == 5) {
        if (!valid_date_parts(dt)) {
            return 0;
        }
        *out = dt;
        return 1;
    }

    if (len != 8 || text[5] != ':' || !all_digits(text, 6, 2)) {
        return 0;
    }
    dt.second = parse_int_span(text, 6, 2);
    dt.precision = PREC_SECOND;
    if (!valid_date_parts(dt)) {
        return 0;
    }
    *out = dt;
    return 1;
}

static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

static void normalize_datetime(DateTime *dt) {
    while (dt->second >= 60) {
        dt->second -= 60;
        dt->minute++;
    }
    while (dt->second < 0) {
        dt->second += 60;
        dt->minute--;
    }
    while (dt->minute >= 60) {
        dt->minute -= 60;
        dt->hour++;
    }
    while (dt->minute < 0) {
        dt->minute += 60;
        dt->hour--;
    }
    while (dt->hour >= 24) {
        dt->hour -= 24;
        dt->day++;
    }
    while (dt->hour < 0) {
        dt->hour += 24;
        dt->day--;
    }
    while (dt->month > 12) {
        dt->month -= 12;
        dt->year++;
    }
    while (dt->month < 1) {
        dt->month += 12;
        dt->year--;
    }
    while (dt->day > days_in_month(dt->year, dt->month)) {
        dt->day -= days_in_month(dt->year, dt->month);
        dt->month++;
        if (dt->month > 12) {
            dt->month = 1;
            dt->year++;
        }
    }
    while (dt->day < 1) {
        dt->month--;
        if (dt->month < 1) {
            dt->month = 12;
            dt->year--;
        }
        dt->day += days_in_month(dt->year, dt->month);
    }
}

static DateTime add_duration_to_datetime(DateTime dt, Duration duration, int sign) {
    dt.year += sign * duration.years;
    dt.month += sign * duration.months;
    normalize_datetime(&dt);

    dt.day += sign * (duration.weeks * 7 + duration.days);
    dt.hour += sign * duration.hours;
    dt.minute += sign * duration.minutes;
    dt.second += sign * duration.seconds;
    normalize_datetime(&dt);

    if (dt.precision < PREC_SECOND &&
        (duration.hours || duration.minutes || duration.seconds)) {
        dt.precision = PREC_SECOND;
    }
    return dt;
}

static Value eval_expr(AstExpr *expr);
static EvalResult eval_stmt(AstStmt *stmt);
static EvalResult eval_stmt_list(AstStmtList statements);

static FunctionDef *function_find(const char *name) {
    for (size_t i = 0; i < function_count; i++) {
        if (strcmp(functions[i].name, name) == 0) {
            return &functions[i];
        }
    }
    return NULL;
}

static void function_register(AstStmt *stmt) {
    FunctionDef *function = function_find(stmt->as.function.name);
    if (function) {
        function->stmt = stmt;
        return;
    }

    FunctionDef *next = realloc(functions, sizeof(FunctionDef) * (function_count + 1));
    if (!next) {
        abort();
    }
    functions = next;
    functions[function_count].name = stmt->as.function.name;
    functions[function_count].stmt = stmt;
    function_count++;
}

static void function_clear(void) {
    free(functions);
    functions = NULL;
    function_count = 0;
}

static int find_function_label(AstStmtList body, const char *label, size_t *out_index) {
    for (size_t i = 0; i < body.count; i++) {
        AstStmt *stmt = body.items[i];
        if (stmt->kind == AST_STMT_LABEL && strcmp(stmt->as.label, label) == 0) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

static int number_compare(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static int array_is_numeric(Value array) {
    if (array.kind != VALUE_ARRAY) {
        return 0;
    }
    for (size_t i = 0; i < array.as.array.count; i++) {
        if (array.as.array.items[i].kind != VALUE_NUMBER) {
            return 0;
        }
    }
    return 1;
}

static char *read_whole_file(const char *path, long *out_size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);

    char *text = malloc((size_t)size + 1);
    if (!text) {
        abort();
    }
    size_t read_count = fread(text, 1, (size_t)size, file);
    if (ferror(file)) {
        free(text);
        fclose(file);
        return NULL;
    }
    text[read_count] = '\0';
    fclose(file);
    if (out_size) {
        *out_size = (long)read_count;
    }
    return text;
}

static Value eval_file_call(AstExpr *expr) {
    const char *name = expr->as.call.name;

    if (strcmp(name, "exists") == 0 ||
        strcmp(name, "read") == 0 ||
        strcmp(name, "bytes") == 0 ||
        strcmp(name, "lines") == 0 ||
        strcmp(name, "chars") == 0 ||
        strcmp(name, "lock") == 0 ||
        strcmp(name, "unlock") == 0) {
        if (expr->as.call.args.count != 1) {
            fprintf(stderr, "%s expects one file argument\n", name);
            return value_null();
        }
        Value file_value = eval_expr(expr->as.call.args.items[0]);
        if (file_value.kind != VALUE_FILE) {
            fprintf(stderr, "%s expects a file reference\n", name);
            value_free(file_value);
            return value_null();
        }

        if (strcmp(name, "exists") == 0) {
            FILE *file = fopen(file_value.as.file_path, "rb");
            int exists = file != NULL;
            if (file) {
                fclose(file);
            }
            value_free(file_value);
            return value_bool(exists);
        }
        if (strcmp(name, "lock") == 0) {
            int ok = lock_path(file_value.as.file_path);
            value_free(file_value);
            return value_bool(ok);
        }
        if (strcmp(name, "unlock") == 0) {
            int ok = unlock_path(file_value.as.file_path);
            value_free(file_value);
            return value_bool(ok);
        }

        long size = 0;
        char *text = read_whole_file(file_value.as.file_path, &size);
        if (!text) {
            value_free(file_value);
            return value_null();
        }

        if (strcmp(name, "read") == 0) {
            Value result = value_string(text);
            free(text);
            value_free(file_value);
            return result;
        }
        if (strcmp(name, "bytes") == 0 || strcmp(name, "chars") == 0) {
            /* TODO: chars currently counts bytes, not Unicode code points. */
            free(text);
            value_free(file_value);
            return value_number((double)size);
        }

        int lines = 0;
        for (long i = 0; i < size; i++) {
            if (text[i] == '\n') {
                lines++;
            }
        }
        if (size > 0 && text[size - 1] != '\n') {
            lines++;
        }
        free(text);
        value_free(file_value);
        return value_number((double)lines);
    }

    if (strcmp(name, "write") == 0 || strcmp(name, "append") == 0) {
        if (expr->as.call.args.count != 2) {
            fprintf(stderr, "%s expects file and text arguments\n", name);
            return value_null();
        }
        Value file_value = eval_expr(expr->as.call.args.items[0]);
        Value text_value = eval_expr(expr->as.call.args.items[1]);
        if (file_value.kind != VALUE_FILE || text_value.kind != VALUE_STRING) {
            fprintf(stderr, "%s expects a file reference and string\n", name);
            value_free(file_value);
            value_free(text_value);
            return value_null();
        }
        FILE *file = fopen(file_value.as.file_path, strcmp(name, "write") == 0 ? "wb" : "ab");
        if (!file) {
            value_free(file_value);
            value_free(text_value);
            return value_bool(0);
        }
        fputs(text_value.as.string, file);
        int ok = ferror(file) == 0;
        fclose(file);
        value_free(file_value);
        value_free(text_value);
        return value_bool(ok);
    }

    return value_null();
}

static Value make_dir_entry(const char *folder, const char *name, const char *type) {
    size_t folder_len = strlen(folder);
    size_t name_len = strlen(name);
    int needs_slash = folder_len > 0 && folder[folder_len - 1] != '/';
    char *path = malloc(folder_len + (size_t)needs_slash + name_len + 1);
    if (!path) {
        abort();
    }
    memcpy(path, folder, folder_len);
    if (needs_slash) {
        path[folder_len] = '/';
    }
    memcpy(path + folder_len + (size_t)needs_slash, name, name_len);
    path[folder_len + (size_t)needs_slash + name_len] = '\0';

    RecordField *fields = malloc(sizeof(RecordField) * 3);
    if (!fields) {
        abort();
    }

    fields[0].name = copy_string("name");
    fields[0].value = malloc(sizeof(Value));
    fields[1].name = copy_string("path");
    fields[1].value = malloc(sizeof(Value));
    fields[2].name = copy_string("type");
    fields[2].value = malloc(sizeof(Value));
    if (!fields[0].value || !fields[1].value || !fields[2].value) {
        abort();
    }

    *fields[0].value = value_string(name);
    *fields[1].value = value_string(path);
    *fields[2].value = value_string(type);
    free(path);
    return value_record(fields, 3);
}

static Value eval_dir_call(AstExpr *expr) {
    const char *name = expr->as.call.name;
    if (expr->as.call.args.count != 1) {
        fprintf(stderr, "%s expects one directory argument\n", name);
        return value_null();
    }

    Value dir_value = eval_expr(expr->as.call.args.items[0]);
    if (dir_value.kind != VALUE_DIR) {
        fprintf(stderr, "%s expects a directory reference\n", name);
        value_free(dir_value);
        return value_null();
    }

    DIR *dir = opendir(dir_value.as.dir_path);
    if (!dir) {
        value_free(dir_value);
        return value_array(NULL, 0);
    }

    Value *items = NULL;
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        size_t folder_len = strlen(dir_value.as.dir_path);
        size_t name_len = strlen(entry->d_name);
        int needs_slash = folder_len > 0 && dir_value.as.dir_path[folder_len - 1] != '/';
        char *path = malloc(folder_len + (size_t)needs_slash + name_len + 1);
        if (!path) {
            abort();
        }
        memcpy(path, dir_value.as.dir_path, folder_len);
        if (needs_slash) {
            path[folder_len] = '/';
        }
        memcpy(path + folder_len + (size_t)needs_slash, entry->d_name, name_len);
        path[folder_len + (size_t)needs_slash + name_len] = '\0';

        struct stat st;
        const char *type = "file";
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            type = "folder";
        }
        free(path);

        if (strcmp(name, "files") == 0 && strcmp(type, "file") != 0) {
            continue;
        }
        if (strcmp(name, "folders") == 0 && strcmp(type, "folder") != 0) {
            continue;
        }

        Value *next = realloc(items, sizeof(Value) * (count + 1));
        if (!next) {
            abort();
        }
        items = next;
        items[count++] = make_dir_entry(dir_value.as.dir_path, entry->d_name, type);
    }

    closedir(dir);
    value_free(dir_value);
    return value_array(items, count);
}

static Value eval_user_function(AstExpr *expr, FunctionDef *function) {
    AstStmt *stmt = function->stmt;
    if (expr->as.call.args.count != stmt->as.function.params.count) {
        fprintf(stderr, "%s expects %zu arguments\n",
                expr->as.call.name,
                stmt->as.function.params.count);
        return value_null();
    }

    Env local_env = {0};
    local_env.parent = &global_env;
    Env *previous_env = current_env;
    current_env = &local_env;

    for (size_t i = 0; i < expr->as.call.args.count; i++) {
        Value arg = eval_expr(expr->as.call.args.items[i]);
        env_set(stmt->as.function.params.items[i], arg);
    }

    function_depth++;
    size_t pc = 0;
    EvalResult result = eval_no_result();
    while (pc < stmt->as.function.body.count) {
        result = eval_stmt(stmt->as.function.body.items[pc]);
        if (result.did_return) {
            break;
        }
        if (result.did_goto) {
            size_t target = 0;
            if (!find_function_label(stmt->as.function.body, result.goto_label, &target)) {
                fprintf(stderr, "unknown label in function %s: %s\n",
                        stmt->as.function.name,
                        result.goto_label);
                free(result.goto_label);
                result = eval_no_result();
                break;
            }
            free(result.goto_label);
            result = eval_no_result();
            pc = target + 1;
            continue;
        }
        pc++;
    }
    function_depth--;
    current_env = previous_env;
    env_clear(&local_env);
    if (result.did_return) {
        return result.value;
    }
    return value_null();
}

static Value eval_call(AstExpr *expr) {
    if (strcmp(expr->as.call.name, "exists") == 0 ||
        strcmp(expr->as.call.name, "read") == 0 ||
        strcmp(expr->as.call.name, "write") == 0 ||
        strcmp(expr->as.call.name, "append") == 0 ||
        strcmp(expr->as.call.name, "lock") == 0 ||
        strcmp(expr->as.call.name, "unlock") == 0 ||
        strcmp(expr->as.call.name, "bytes") == 0 ||
        strcmp(expr->as.call.name, "lines") == 0 ||
        strcmp(expr->as.call.name, "chars") == 0) {
        return eval_file_call(expr);
    }
    if (strcmp(expr->as.call.name, "list") == 0 ||
        strcmp(expr->as.call.name, "files") == 0 ||
        strcmp(expr->as.call.name, "folders") == 0) {
        return eval_dir_call(expr);
    }

    FunctionDef *function = function_find(expr->as.call.name);
    if (function) {
        return eval_user_function(expr, function);
    }

    if (expr->as.call.args.count != 1) {
        fprintf(stderr, "%s expects one array argument\n", expr->as.call.name);
        return value_null();
    }

    Value arg = eval_expr(expr->as.call.args.items[0]);
    if (!array_is_numeric(arg)) {
        fprintf(stderr, "%s expects a numeric array\n", expr->as.call.name);
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
            result += arg.as.array.items[i].as.number;
        }
        if (strcmp(name, "mean") == 0) {
            result /= (double)count;
        }
    } else if (strcmp(name, "min") == 0) {
        result = arg.as.array.items[0].as.number;
        for (size_t i = 1; i < count; i++) {
            if (arg.as.array.items[i].as.number < result) {
                result = arg.as.array.items[i].as.number;
            }
        }
    } else if (strcmp(name, "max") == 0) {
        result = arg.as.array.items[0].as.number;
        for (size_t i = 1; i < count; i++) {
            if (arg.as.array.items[i].as.number > result) {
                result = arg.as.array.items[i].as.number;
            }
        }
    } else if (strcmp(name, "median") == 0) {
        double *sorted = malloc(sizeof(double) * count);
        if (!sorted) {
            abort();
        }
        for (size_t i = 0; i < count; i++) {
            sorted[i] = arg.as.array.items[i].as.number;
        }
        qsort(sorted, count, sizeof(double), number_compare);
        if (count % 2 == 1) {
            result = sorted[count / 2];
        } else {
            result = (sorted[count / 2 - 1] + sorted[count / 2]) / 2.0;
        }
        free(sorted);
    } else if (strcmp(name, "mode") == 0) {
        result = arg.as.array.items[0].as.number;
        size_t best_count = 0;
        for (size_t i = 0; i < count; i++) {
            size_t current_count = 0;
            for (size_t j = 0; j < count; j++) {
                if (arg.as.array.items[i].as.number == arg.as.array.items[j].as.number) {
                    current_count++;
                }
            }
            if (current_count > best_count) {
                best_count = current_count;
                result = arg.as.array.items[i].as.number;
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

    if (left.kind == VALUE_DATETIME && right.kind == VALUE_DATETIME) {
        int cmp = 0;
        DateTimePrecision precision =
            left.as.datetime.precision < right.as.datetime.precision
                ? left.as.datetime.precision
                : right.as.datetime.precision;

        if (left.as.datetime.time_only != right.as.datetime.time_only) {
            cmp = 1;
        } else if (!left.as.datetime.time_only && precision >= PREC_YEAR &&
                   left.as.datetime.year != right.as.datetime.year) {
            cmp = left.as.datetime.year < right.as.datetime.year ? -1 : 1;
        } else if (!left.as.datetime.time_only && precision >= PREC_MONTH &&
                   left.as.datetime.month != right.as.datetime.month) {
            cmp = left.as.datetime.month < right.as.datetime.month ? -1 : 1;
        } else if (!left.as.datetime.time_only && precision >= PREC_DAY &&
                   left.as.datetime.day != right.as.datetime.day) {
            cmp = left.as.datetime.day < right.as.datetime.day ? -1 : 1;
        } else if (precision >= PREC_HOUR &&
                   left.as.datetime.hour != right.as.datetime.hour) {
            cmp = left.as.datetime.hour < right.as.datetime.hour ? -1 : 1;
        } else if (precision >= PREC_MINUTE &&
                   left.as.datetime.minute != right.as.datetime.minute) {
            cmp = left.as.datetime.minute < right.as.datetime.minute ? -1 : 1;
        } else if (precision >= PREC_SECOND &&
                   left.as.datetime.second != right.as.datetime.second) {
            cmp = left.as.datetime.second < right.as.datetime.second ? -1 : 1;
        }

        if (strcmp(op, "=") == 0) result = cmp == 0;
        else if (strcmp(op, "!=") == 0) result = cmp != 0;
        else if (strcmp(op, ">") == 0) result = cmp > 0;
        else if (strcmp(op, "<") == 0) result = cmp < 0;
        else if (strcmp(op, ">=") == 0) result = cmp >= 0;
        else if (strcmp(op, "<=") == 0) result = cmp <= 0;
        else if (strcmp(op, "!>") == 0) result = !(cmp > 0);
        else if (strcmp(op, "!<") == 0) result = !(cmp < 0);
    } else if (modifier_is(expr->as.binary.modifier, "caseless") &&
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

    if ((strcmp(op, "+") == 0 || strcmp(op, "-") == 0) &&
        left.kind == VALUE_DATETIME &&
        right.kind == VALUE_DURATION) {
        DateTime result = add_duration_to_datetime(left.as.datetime,
                                                   right.as.duration,
                                                   strcmp(op, "+") == 0 ? 1 : -1);
        value_free(left);
        value_free(right);
        return value_datetime(result);
    }

    if (strcmp(op, "+") == 0 &&
        left.kind == VALUE_DURATION &&
        right.kind == VALUE_DATETIME) {
        DateTime result = add_duration_to_datetime(right.as.datetime,
                                                   left.as.duration,
                                                   1);
        value_free(left);
        value_free(right);
        return value_datetime(result);
    }

    if (strcmp(op, "+") == 0 &&
        left.kind == VALUE_STRING &&
        right.kind == VALUE_STRING) {
        size_t left_len = strlen(left.as.string);
        size_t right_len = strlen(right.as.string);
        char *combined = malloc(left_len + right_len + 1);
        if (!combined) {
            abort();
        }
        memcpy(combined, left.as.string, left_len);
        memcpy(combined + left_len, right.as.string, right_len + 1);
        Value result = value_string(combined);
        free(combined);
        value_free(left);
        value_free(right);
        return result;
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
    case AST_EXPR_DURATION: {
        Duration duration = {
            expr->as.duration.years,
            expr->as.duration.months,
            expr->as.duration.weeks,
            expr->as.duration.days,
            expr->as.duration.hours,
            expr->as.duration.minutes,
            expr->as.duration.seconds
        };
        return value_duration(duration);
    }
    case AST_EXPR_ARRAY: {
        Value *items = NULL;
        if (expr->as.array.count > 0) {
            items = malloc(sizeof(Value) * expr->as.array.count);
            if (!items) {
                abort();
            }
        }
        for (size_t i = 0; i < expr->as.array.count; i++) {
            items[i] = eval_expr(expr->as.array.items[i]);
        }
        return value_array(items, expr->as.array.count);
    }
    case AST_EXPR_RECORD: {
        RecordField *fields = NULL;
        if (expr->as.record.count > 0) {
            fields = malloc(sizeof(RecordField) * expr->as.record.count);
            if (!fields) {
                abort();
            }
        }
        for (size_t i = 0; i < expr->as.record.count; i++) {
            fields[i].name = copy_string(expr->as.record.items[i].name);
            fields[i].value = malloc(sizeof(Value));
            if (!fields[i].value) {
                abort();
            }
            *fields[i].value = eval_expr(expr->as.record.items[i].value);
        }
        return value_record(fields, expr->as.record.count);
    }
    case AST_EXPR_INDEX: {
        Value array = eval_expr(expr->as.index.array);
        Value index = eval_expr(expr->as.index.index);
        if (array.kind == VALUE_ARRAY && index.kind == VALUE_NUMBER) {
            int position = (int)index.as.number;
            if (position < 0 || (size_t)position >= array.as.array.count) {
                fprintf(stderr, "array index out of range\n");
                value_free(array);
                value_free(index);
                return value_null();
            }
            Value result = value_copy(array.as.array.items[position]);
            value_free(array);
            value_free(index);
            return result;
        }
        if (array.kind == VALUE_RECORD && index.kind == VALUE_STRING) {
            RecordField *field = record_find(&array, index.as.string);
            if (!field) {
                fprintf(stderr, "unknown record field: %s\n", index.as.string);
                value_free(array);
                value_free(index);
                return value_null();
            }
            Value result = value_copy(*field->value);
            value_free(array);
            value_free(index);
            return result;
        }
        fprintf(stderr, "indexing expects array[number] or record[string]\n");
        value_free(array);
        value_free(index);
        return value_null();
    }
    case AST_EXPR_FIELD: {
        Value object = eval_expr(expr->as.field.object);
        if (object.kind != VALUE_RECORD) {
            fprintf(stderr, "field access expects a record\n");
            value_free(object);
            return value_null();
        }
        RecordField *field = record_find(&object, expr->as.field.field);
        if (!field) {
            fprintf(stderr, "unknown record field: %s\n", expr->as.field.field);
            value_free(object);
            return value_null();
        }
        Value result = value_copy(*field->value);
        value_free(object);
        return result;
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
    if (strcmp(modifier, "date") == 0) {
        DateTime datetime;
        if (value.kind != VALUE_STRING || !parse_date_value(value.as.string, &datetime)) {
            fprintf(stderr, "date modifier expects an ISO-like date string\n");
            value_free(value);
            return value_null();
        }
        value_free(value);
        return value_datetime(datetime);
    }
    if (strcmp(modifier, "time") == 0) {
        DateTime datetime;
        if (value.kind != VALUE_STRING || !parse_time_value(value.as.string, &datetime)) {
            fprintf(stderr, "time modifier expects an ISO-like time string\n");
            value_free(value);
            return value_null();
        }
        value_free(value);
        return value_datetime(datetime);
    }
    if (strcmp(modifier, "file") == 0) {
        if (value.kind != VALUE_STRING) {
            fprintf(stderr, "file modifier expects a path string\n");
            value_free(value);
            return value_null();
        }
        Value file_value = value_file(value.as.string);
        value_free(value);
        return file_value;
    }
    if (strcmp(modifier, "dir") == 0) {
        if (value.kind != VALUE_STRING) {
            fprintf(stderr, "dir modifier expects a path string\n");
            value_free(value);
            return value_null();
        }
        Value dir_value = value_dir(value.as.string);
        value_free(value);
        return dir_value;
    }

    fprintf(stderr, "unsupported assignment modifier: %s\n", modifier);
    return value;
}

static EvalResult eval_stmt(AstStmt *stmt) {
    EvalResult no_result = eval_no_result();

    switch (stmt->kind) {
    case AST_STMT_ASSIGN: {
        Value value = eval_expr(stmt->as.assign.value);
        value = apply_assignment_modifier(stmt->as.assign.modifier, value);
        env_set(stmt->as.assign.name, value);
        break;
    }
    case AST_STMT_FIELD_ASSIGN: {
        Symbol *symbol = env_find(stmt->as.field_assign.name);
        if (!symbol || symbol->value.kind != VALUE_RECORD) {
            fprintf(stderr, "field assignment expects a record variable: %s\n",
                    stmt->as.field_assign.name);
            return no_result;
        }
        Value value = eval_expr(stmt->as.field_assign.value);
        record_set(&symbol->value, stmt->as.field_assign.field, value);
        break;
    }
    case AST_STMT_PRINT: {
        Value value = eval_expr(stmt->as.print);
        value_print(value);
        value_free(value);
        break;
    }
    case AST_STMT_EXPR: {
        Value value = eval_expr(stmt->as.expr_stmt);
        value_free(value);
        break;
    }
    case AST_STMT_WITH_LOCK: {
        Value file_value = eval_expr(stmt->as.with_lock.file);
        if (file_value.kind != VALUE_FILE) {
            fprintf(stderr, "with lock expects a file reference\n");
            value_free(file_value);
            break;
        }
        char *path = copy_string(file_value.as.file_path);
        value_free(file_value);
        if (lock_path(path)) {
            EvalResult result = eval_stmt_list(stmt->as.with_lock.body);
            unlock_path(path);
            if (result.did_return || result.did_goto) {
                free(path);
                return result;
            }
        }
        free(path);
        break;
    }
    case AST_STMT_FOR_EACH: {
        Value iterable = eval_expr(stmt->as.for_each.iterable);
        if (iterable.kind != VALUE_ARRAY) {
            fprintf(stderr, "for in expects an array\n");
            value_free(iterable);
            break;
        }
        for (size_t i = 0; i < iterable.as.array.count; i++) {
            env_set(stmt->as.for_each.name, value_copy(iterable.as.array.items[i]));
            EvalResult result = eval_stmt_list(stmt->as.for_each.body);
            if (result.did_return || result.did_goto) {
                value_free(iterable);
                return result;
            }
        }
        value_free(iterable);
        break;
    }
    case AST_STMT_FUNCTION:
        function_register(stmt);
        break;
    case AST_STMT_RETURN: {
        return eval_return(stmt->as.return_expr ? eval_expr(stmt->as.return_expr) : value_null());
    }
    case AST_STMT_LABEL:
        break;
    case AST_STMT_GOTO: {
        if (function_depth == 0) {
            fprintf(stderr, "goto is only supported inside functions for now\n");
            break;
        }
        return eval_goto(stmt->as.goto_label);
    }
    case AST_STMT_IF: {
        Value condition = eval_expr(stmt->as.if_stmt.condition);
        int truth = value_truthy(condition);
        value_free(condition);
        if (truth) {
            EvalResult result = eval_stmt_list(stmt->as.if_stmt.body);
            if (result.did_return || result.did_goto) {
                return result;
            }
        }
        break;
    }
    }

    return no_result;
}

static EvalResult eval_stmt_list(AstStmtList statements) {
    for (size_t i = 0; i < statements.count; i++) {
        EvalResult result = eval_stmt(statements.items[i]);
        if (result.did_return || result.did_goto) {
            return result;
        }
    }
    return eval_no_result();
}

int eval_program(AstStmtList program) {
    EvalResult result = eval_stmt_list(program);
    if (result.did_return) {
        value_free(result.value);
    }
    if (result.did_goto) {
        free(result.goto_label);
    }
    lock_clear();
    function_clear();
    env_clear(&global_env);
    return 0;
}

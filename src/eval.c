#include "eval.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

int parse_source(const char *source, AstStmtList *out_program);

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
    AstStmt *stmt;
} WatcherDef;

typedef enum {
    ERROR_MODE_STOP,
    ERROR_MODE_GOTO,
    ERROR_MODE_RESUME_NEXT
} ErrorMode;

typedef struct {
    int active;
    char *message;
    int line;
    int column;
    int code;
    char *source;
} RuntimeError;

typedef struct {
    int did_return;
    int return_has_value;
    int did_goto;
    char *goto_label;
    int did_gosub;
    char *gosub_label;
    int did_stop;
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
    int imported;
    int warned;
    char *library;
} FunctionDef;

typedef struct {
    char *name;
    char *context;
    AstStmt *stmt;
    int imported;
    int warned;
    char *library;
} ModifierDef;

typedef struct {
    char *path;
    AstStmtList program;
} LoadedFile;

typedef struct {
    char *path;
    char *library;
} UsePair;

static Env global_env = {0};
static Env *current_env = &global_env;
static FunctionDef *functions = NULL;
static size_t function_count = 0;
static ModifierDef *modifiers = NULL;
static size_t modifier_count = 0;
static LockEntry *locks = NULL;
static size_t lock_count = 0;
static int lock_cleanup_registered = 0;
static volatile sig_atomic_t lock_signal_cleanup_started = 0;
static WatcherDef *watchers = NULL;
static size_t watcher_count = 0;
static size_t *watcher_queue = NULL;
static size_t watcher_queue_count = 0;
static int function_depth = 0;
static int watcher_suppressed = 0;
static int watcher_draining = 0;
static RuntimeError current_error = {0};
static ErrorMode error_mode = ERROR_MODE_STOP;
static char *error_goto_label = NULL;
static int runtime_stopped = 0;
static int error_generation = 0;
static char *pending_error_goto_label = NULL;
static int current_line = 0;
static int current_column = 0;
static AstStmtList active_root = {0};
static char *root_source_path = NULL;
static char *current_import_path = NULL;
static LoadedFile *loaded_files = NULL;
static size_t loaded_file_count = 0;
static UsePair *used_pairs = NULL;
static size_t used_pair_count = 0;
static UsePair *use_stack = NULL;
static size_t use_stack_count = 0;

static char *copy_string(const char *text) {
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) {
        abort();
    }
    memcpy(copy, text, length + 1);
    return copy;
}

void eval_set_source_path(const char *path) {
    free(root_source_path);
    root_source_path = path ? copy_string(path) : NULL;
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

static EvalResult eval_return(Value value, int has_value) {
    EvalResult result = {0};
    result.did_return = 1;
    result.return_has_value = has_value;
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

static EvalResult eval_gosub(const char *label) {
    EvalResult result = {0};
    result.did_gosub = 1;
    result.gosub_label = copy_string(label);
    result.value = value_null();
    return result;
}

static EvalResult eval_stop(void) {
    EvalResult result = {0};
    result.did_stop = 1;
    result.value = value_null();
    return result;
}

static void error_clear_state(void) {
    free(current_error.message);
    free(current_error.source);
    memset(&current_error, 0, sizeof(current_error));
}

static void error_set_state(const char *message, int code, const char *source) {
    error_clear_state();
    current_error.active = 1;
    current_error.message = copy_string(message);
    current_error.line = current_line;
    current_error.column = current_column;
    current_error.code = code;
    current_error.source = copy_string(source ? source : "runtime");
}

static void runtime_error_raise(const char *message, int code, const char *source) {
    error_set_state(message, code, source);
    error_generation++;

    if (error_mode == ERROR_MODE_GOTO && error_goto_label) {
        free(pending_error_goto_label);
        pending_error_goto_label = error_goto_label;
        error_goto_label = NULL;
        error_mode = ERROR_MODE_STOP;
        return;
    }

    if (error_mode == ERROR_MODE_STOP) {
        fprintf(stderr, "runtime error at %d:%d: %s\n",
                current_error.line,
                current_error.column,
                current_error.message);
        runtime_stopped = 1;
    }
}

static EvalResult eval_error_result(void) {
    if (pending_error_goto_label) {
        char *label = pending_error_goto_label;
        pending_error_goto_label = NULL;
        EvalResult result = eval_goto(label);
        free(label);
        return result;
    }
    if (runtime_stopped) {
        return eval_stop();
    }
    return eval_no_result();
}

static int error_action_pending(void) {
    return pending_error_goto_label != NULL || runtime_stopped;
}

static Value value_error_object(void) {
    RecordField *fields = malloc(sizeof(RecordField) * 5);
    if (!fields) {
        abort();
    }
    const char *names[] = {"message", "line", "column", "code", "source"};
    for (size_t i = 0; i < 5; i++) {
        fields[i].name = copy_string(names[i]);
        fields[i].value = malloc(sizeof(Value));
        if (!fields[i].value) {
            abort();
        }
    }
    *fields[0].value = value_string(current_error.active ? current_error.message : "");
    *fields[1].value = value_number(current_error.active ? current_error.line : 0);
    *fields[2].value = value_number(current_error.active ? current_error.column : 0);
    *fields[3].value = value_number(current_error.active ? current_error.code : 0);
    *fields[4].value = value_string(current_error.active ? current_error.source : "");
    return value_record(fields, 5);
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
    if (strcmp(name, "error") == 0) {
        return value_bool(current_error.active);
    }
    Symbol *symbol = env_find(name);
    if (!symbol) {
        char message[256];
        snprintf(message, sizeof(message), "undefined variable: %s", name);
        runtime_error_raise(message, 1001, "undefined variable");
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

static void lock_cleanup_on_exit(void) {
    lock_clear();
}

static void lock_cleanup_on_signal(int signal_number) {
    if (lock_signal_cleanup_started) {
        _exit(128 + signal_number);
    }
    lock_signal_cleanup_started = 1;

    for (size_t i = 0; i < lock_count; i++) {
        if (locks[i].fd >= 0) {
            close(locks[i].fd);
            locks[i].fd = -1;
        }
    }
    _exit(128 + signal_number);
}

static void lock_install_cleanup(void) {
    if (lock_cleanup_registered) {
        return;
    }

    if (atexit(lock_cleanup_on_exit) != 0) {
        fprintf(stderr, "failed to register lock cleanup\n");
    }

    signal(SIGINT, lock_cleanup_on_signal);
    signal(SIGTERM, lock_cleanup_on_signal);
    signal(SIGHUP, lock_cleanup_on_signal);

    lock_cleanup_registered = 1;
}

static int lock_path(const char *path) {
    LockEntry *entry = lock_find(path);
    if (entry) {
        entry->depth++;
        lock_install_cleanup();
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
    lock_install_cleanup();
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
static Value eval_comparison(AstExpr *expr, Value left, Value right);
static EvalResult eval_stmt(AstStmt *stmt);
static EvalResult eval_stmt_list(AstStmtList statements);

static int watcher_matches(AstStmt *watcher, const char *name) {
    for (size_t i = 0; i < watcher->as.watch.names.count; i++) {
        if (strcmp(watcher->as.watch.names.items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void watcher_enqueue(size_t index) {
    size_t *next = realloc(watcher_queue, sizeof(size_t) * (watcher_queue_count + 1));
    if (!next) {
        abort();
    }
    watcher_queue = next;
    watcher_queue[watcher_queue_count++] = index;
}

static void watcher_drain(void) {
    if (watcher_draining) {
        return;
    }

    watcher_draining = 1;
    size_t cursor = 0;
    size_t steps = 0;
    while (cursor < watcher_queue_count) {
        if (++steps > 10000) {
            fprintf(stderr, "watcher queue limit reached\n");
            break;
        }
        size_t index = watcher_queue[cursor++];
        if (index < watcher_count) {
            EvalResult result = eval_stmt_list(watchers[index].stmt->as.watch.body);
            if (result.did_return) {
                value_free(result.value);
            }
            if (result.did_goto) {
                free(result.goto_label);
            }
            if (result.did_gosub) {
                free(result.gosub_label);
            }
        }
    }

    free(watcher_queue);
    watcher_queue = NULL;
    watcher_queue_count = 0;
    watcher_draining = 0;
}

static void watcher_trigger(const char *name) {
    if (watcher_suppressed || current_env != &global_env) {
        return;
    }
    for (size_t i = 0; i < watcher_count; i++) {
        if (watcher_matches(watchers[i].stmt, name)) {
            watcher_enqueue(i);
        }
    }
    watcher_drain();
}

static void watcher_register(AstStmt *stmt) {
    if (current_env != &global_env) {
        fprintf(stderr, "watch may only be registered at top level for now\n");
        return;
    }

    WatcherDef *next = realloc(watchers, sizeof(WatcherDef) * (watcher_count + 1));
    if (!next) {
        abort();
    }
    watchers = next;
    watchers[watcher_count].stmt = stmt;
    watcher_enqueue(watcher_count);
    watcher_count++;
    watcher_drain();
}

static void watcher_clear(void) {
    free(watchers);
    watchers = NULL;
    watcher_count = 0;
    free(watcher_queue);
    watcher_queue = NULL;
    watcher_queue_count = 0;
    watcher_suppressed = 0;
    watcher_draining = 0;
}

static FunctionDef *function_find_local(const char *name) {
    for (size_t i = 0; i < function_count; i++) {
        if (!functions[i].imported && strcmp(functions[i].name, name) == 0) {
            return &functions[i];
        }
    }
    return NULL;
}

static FunctionDef *function_resolve(const char *library, const char *name) {
    if (library) {
        for (size_t i = function_count; i > 0; i--) {
            FunctionDef *function = &functions[i - 1];
            if (function->imported &&
                function->library &&
                strcmp(function->library, library) == 0 &&
                strcmp(function->name, name) == 0) {
                return function;
            }
        }
        return NULL;
    }

    FunctionDef *local = function_find_local(name);
    if (local) {
        return local;
    }

    FunctionDef *best = NULL;
    for (size_t i = function_count; i > 0; i--) {
        FunctionDef *function = &functions[i - 1];
        if (function->imported && strcmp(function->name, name) == 0) {
            best = function;
            break;
        }
    }

    if (best && !best->warned) {
        for (size_t i = function_count; i > 0; i--) {
            FunctionDef *other = &functions[i - 1];
            if (other == best || !other->imported) {
                continue;
            }
            if (strcmp(other->name, best->name) == 0 &&
                other->library && best->library &&
                strcmp(other->library, best->library) != 0) {
                fprintf(stderr,
                        "warning: function '%s' from library '%s' overrides function from library '%s'\n",
                        best->name,
                        best->library,
                        other->library);
                best->warned = 1;
                break;
            }
        }
    }

    return best;
}

static void function_register_def(AstStmt *stmt, int imported, const char *library) {
    FunctionDef *function = function_find_local(stmt->as.function.name);
    if (function) {
        if (imported && !function->imported) {
            return;
        }
        function->stmt = stmt;
        function->imported = imported;
        free(function->library);
        function->library = library ? copy_string(library) : NULL;
        return;
    }

    FunctionDef *next = realloc(functions, sizeof(FunctionDef) * (function_count + 1));
    if (!next) {
        abort();
    }
    functions = next;
    functions[function_count].name = stmt->as.function.name;
    functions[function_count].stmt = stmt;
    functions[function_count].imported = imported;
    functions[function_count].warned = 0;
    functions[function_count].library = library ? copy_string(library) : NULL;
    function_count++;
}

static void function_register(AstStmt *stmt) {
    function_register_def(stmt, 0, NULL);
}

static void function_clear(void) {
    for (size_t i = 0; i < function_count; i++) {
        free(functions[i].library);
    }
    free(functions);
    functions = NULL;
    function_count = 0;
}

static ModifierDef *modifier_find(const char *name, const char *context) {
    for (size_t i = modifier_count; i > 0; i--) {
        ModifierDef *modifier = &modifiers[i - 1];
        if (strcmp(modifier->name, name) == 0 &&
            strcmp(modifier->context, context) == 0) {
            return modifier;
        }
    }
    return NULL;
}

static int modifier_phrase_matches(const char *phrase, const char *name, const char **args_start) {
    size_t name_len = strlen(name);
    while (*phrase == ' ' || *phrase == '\t') {
        phrase++;
    }
    if (strncmp(phrase, name, name_len) != 0) {
        return 0;
    }
    if (phrase[name_len] == '\0') {
        *args_start = phrase + name_len;
        return 1;
    }
    if (phrase[name_len] == ' ' || phrase[name_len] == '\t') {
        const char *p = phrase + name_len;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        *args_start = p;
        return 1;
    }
    return 0;
}

static void modifier_use_label(AstModifierUse use, char *buffer, size_t size) {
    if (use.library) {
        snprintf(buffer, size, "%s.%s", use.library, use.name);
    } else {
        snprintf(buffer, size, "%s", use.name);
    }
}

static ModifierDef *modifier_resolve(AstModifierUse use, const char *context, const char **args_start) {
    ModifierDef *best = NULL;
    const char *best_args = NULL;
    size_t best_len = 0;
    for (size_t i = modifier_count; i > 0; i--) {
        ModifierDef *modifier = &modifiers[i - 1];
        const char *candidate_args = NULL;
        if (strcmp(modifier->context, context) == 0 &&
            (!use.library ||
             (modifier->library && strcmp(modifier->library, use.library) == 0)) &&
            modifier_phrase_matches(use.name, modifier->name, &candidate_args)) {
            size_t len = strlen(modifier->name);
            if (!best || len > best_len) {
                best = modifier;
                best_args = candidate_args;
                best_len = len;
            }
        }
    }
    if (best) {
        *args_start = best_args;
        if (!use.library && best->imported && !best->warned) {
            for (size_t i = modifier_count; i > 0; i--) {
                ModifierDef *other = &modifiers[i - 1];
                if (other == best || !other->imported) {
                    continue;
                }
                if (strcmp(other->context, best->context) == 0 &&
                    strcmp(other->name, best->name) == 0 &&
                    other->library && best->library &&
                    strcmp(other->library, best->library) != 0) {
                    fprintf(stderr,
                            "warning: modifier '%s' from library '%s' overrides modifier from library '%s'\n",
                            best->name,
                            best->library,
                            other->library);
                    best->warned = 1;
                    break;
                }
            }
        }
    }
    return best;
}

static void modifier_register_def(AstStmt *stmt, int imported, const char *library) {
    if (strcmp(stmt->as.modifier.context, "assign") != 0 &&
        strcmp(stmt->as.modifier.context, "compare") != 0) {
        runtime_error_raise("modifier context must be assign or compare", 1003, "modifier");
        return;
    }

    ModifierDef *existing = modifier_find(stmt->as.modifier.name, stmt->as.modifier.context);
    if (existing) {
        if (imported && !existing->imported) {
            return;
        }
        if (imported && existing->imported) {
            existing = NULL;
        }
    }

    if (existing) {
        existing->stmt = stmt;
        existing->imported = imported;
        free(existing->library);
        existing->library = library ? copy_string(library) : NULL;
        return;
    }

    ModifierDef *next = realloc(modifiers, sizeof(ModifierDef) * (modifier_count + 1));
    if (!next) {
        abort();
    }
    modifiers = next;
    modifiers[modifier_count].name = stmt->as.modifier.name;
    modifiers[modifier_count].context = stmt->as.modifier.context;
    modifiers[modifier_count].stmt = stmt;
    modifiers[modifier_count].imported = imported;
    modifiers[modifier_count].warned = 0;
    modifiers[modifier_count].library = library ? copy_string(library) : NULL;
    modifier_count++;
}

static void modifier_register(AstStmt *stmt) {
    modifier_register_def(stmt, 0, NULL);
}

static void modifier_clear(void) {
    for (size_t i = 0; i < modifier_count; i++) {
        free(modifiers[i].library);
    }
    free(modifiers);
    modifiers = NULL;
    modifier_count = 0;
}

static AstStmt *library_find(const char *name) {
    for (size_t i = 0; i < active_root.count; i++) {
        AstStmt *stmt = active_root.items[i];
        if (stmt->kind == AST_STMT_LIBRARY && strcmp(stmt->as.library.name, name) == 0) {
            return stmt;
        }
    }
    return NULL;
}

static AstStmt *library_find_in(AstStmtList program, const char *name) {
    for (size_t i = 0; i < program.count; i++) {
        AstStmt *stmt = program.items[i];
        if (stmt->kind == AST_STMT_LIBRARY && strcmp(stmt->as.library.name, name) == 0) {
            return stmt;
        }
    }
    return NULL;
}

static char *dirname_copy(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return copy_string(".");
    }
    if (slash == path) {
        return copy_string("/");
    }
    size_t len = (size_t)(slash - path);
    char *dir = malloc(len + 1);
    if (!dir) {
        abort();
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

static char *resolve_use_path(const char *base_file, const char *use_path) {
    if (use_path[0] == '/') {
        return copy_string(use_path);
    }
    char *dir = dirname_copy(base_file ? base_file : ".");
    size_t dir_len = strlen(dir);
    size_t path_len = strlen(use_path);
    int needs_slash = dir_len > 0 && dir[dir_len - 1] != '/';
    char *resolved = malloc(dir_len + (size_t)needs_slash + path_len + 1);
    if (!resolved) {
        abort();
    }
    memcpy(resolved, dir, dir_len);
    if (needs_slash) {
        resolved[dir_len] = '/';
    }
    memcpy(resolved + dir_len + (size_t)needs_slash, use_path, path_len + 1);
    free(dir);
    return resolved;
}

static char *read_source_file(const char *path) {
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
    char *source = malloc((size_t)size + 1);
    if (!source) {
        abort();
    }
    size_t read_count = fread(source, 1, (size_t)size, file);
    if (ferror(file)) {
        free(source);
        fclose(file);
        return NULL;
    }
    source[read_count] = '\0';
    fclose(file);
    return source;
}

static LoadedFile *loaded_file_find(const char *path) {
    for (size_t i = 0; i < loaded_file_count; i++) {
        if (strcmp(loaded_files[i].path, path) == 0) {
            return &loaded_files[i];
        }
    }
    return NULL;
}

static LoadedFile *loaded_file_get(const char *path) {
    LoadedFile *loaded = loaded_file_find(path);
    if (loaded) {
        return loaded;
    }

    char *source = read_source_file(path);
    if (!source) {
        char message[512];
        snprintf(message, sizeof(message), "could not load library file: %s", path);
        runtime_error_raise(message, 1003, "use");
        return NULL;
    }

    AstStmtList program = ast_stmt_list_empty();
    if (parse_source(source, &program) != 0) {
        free(source);
        char message[512];
        snprintf(message, sizeof(message), "could not parse library file: %s", path);
        runtime_error_raise(message, 1003, "use");
        return NULL;
    }
    free(source);

    LoadedFile *next = realloc(loaded_files, sizeof(LoadedFile) * (loaded_file_count + 1));
    if (!next) {
        abort();
    }
    loaded_files = next;
    loaded_files[loaded_file_count].path = copy_string(path);
    loaded_files[loaded_file_count].program = program;
    loaded_file_count++;
    return &loaded_files[loaded_file_count - 1];
}

static int use_pair_contains(UsePair *pairs, size_t count, const char *path, const char *library) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(pairs[i].path, path) == 0 && strcmp(pairs[i].library, library) == 0) {
            return 1;
        }
    }
    return 0;
}

static void use_pair_add(UsePair **pairs, size_t *count, const char *path, const char *library) {
    UsePair *next = realloc(*pairs, sizeof(UsePair) * (*count + 1));
    if (!next) {
        abort();
    }
    *pairs = next;
    (*pairs)[*count].path = copy_string(path);
    (*pairs)[*count].library = copy_string(library);
    (*count)++;
}

static void use_pair_pop(UsePair *pairs, size_t *count) {
    if (*count == 0) {
        return;
    }
    (*count)--;
    free(pairs[*count].path);
    free(pairs[*count].library);
}

static void use_pairs_clear(UsePair **pairs, size_t *count) {
    for (size_t i = 0; i < *count; i++) {
        free((*pairs)[i].path);
        free((*pairs)[i].library);
    }
    free(*pairs);
    *pairs = NULL;
    *count = 0;
}

static void loaded_files_clear(void) {
    for (size_t i = 0; i < loaded_file_count; i++) {
        free(loaded_files[i].path);
        ast_free_program(loaded_files[i].program);
    }
    free(loaded_files);
    loaded_files = NULL;
    loaded_file_count = 0;
}

static void library_import_from_block(AstStmt *library);

static void library_import(const char *name, const char *path) {
    AstStmt *library = NULL;
    char *resolved = NULL;
    char *previous_import_path = NULL;

    if (path) {
        const char *base = current_import_path ? current_import_path : root_source_path;
        resolved = resolve_use_path(base, path);
        if (use_pair_contains(used_pairs, used_pair_count, resolved, name)) {
            free(resolved);
            return;
        }
        if (use_pair_contains(use_stack, use_stack_count, resolved, name)) {
            char message[512];
            snprintf(message, sizeof(message), "circular use detected for %s from %s", name, resolved);
            runtime_error_raise(message, 1003, "use");
            free(resolved);
            return;
        }
        use_pair_add(&use_stack, &use_stack_count, resolved, name);
        LoadedFile *loaded = loaded_file_get(resolved);
        if (!loaded) {
            use_pair_pop(use_stack, &use_stack_count);
            free(resolved);
            return;
        }
        library = library_find_in(loaded->program, name);
        if (!library) {
            char message[512];
            snprintf(message, sizeof(message), "library not found: %s in %s", name, resolved);
            runtime_error_raise(message, 1003, "use");
            use_pair_pop(use_stack, &use_stack_count);
            free(resolved);
            return;
        }
        previous_import_path = current_import_path;
        current_import_path = resolved;
        library_import_from_block(library);
        current_import_path = previous_import_path;
        use_pair_pop(use_stack, &use_stack_count);
        if (!error_action_pending()) {
            use_pair_add(&used_pairs, &used_pair_count, resolved, name);
        }
        free(resolved);
        return;
    }

    library = library_find(name);
    if (!library) {
        char message[256];
        snprintf(message, sizeof(message), "library not found: %s", name);
        runtime_error_raise(message, 1003, "use");
        return;
    }

    library_import_from_block(library);
}

static void library_import_from_block(AstStmt *library) {
    for (size_t i = 0; i < library->as.library.body.count; i++) {
        AstStmt *stmt = library->as.library.body.items[i];
        if (stmt->kind == AST_STMT_USE) {
            library_import(stmt->as.use_stmt.name, stmt->as.use_stmt.path);
            if (error_action_pending()) {
                return;
            }
        } else if (stmt->kind == AST_STMT_FUNCTION) {
            function_register_def(stmt, 1, library->as.library.name);
        } else if (stmt->kind == AST_STMT_MODIFIER && stmt->as.modifier.exported) {
            modifier_register_def(stmt, 1, library->as.library.name);
        }
    }
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
            char message[256];
            snprintf(message, sizeof(message), "%s expects one file argument", name);
            runtime_error_raise(message, 1004, "file operation");
            return value_null();
        }
        Value file_value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(file_value);
            return value_null();
        }
        if (file_value.kind != VALUE_FILE) {
            char message[256];
            snprintf(message, sizeof(message), "%s expects a file reference", name);
            runtime_error_raise(message, 1004, "file operation");
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
            char message[512];
            snprintf(message, sizeof(message), "could not read file: %s", file_value.as.file_path);
            runtime_error_raise(message, 1004, "file operation");
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
            char message[256];
            snprintf(message, sizeof(message), "%s expects file and text arguments", name);
            runtime_error_raise(message, 1004, "file operation");
            return value_null();
        }
        Value file_value = eval_expr(expr->as.call.args.items[0]);
        Value text_value = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(file_value);
            value_free(text_value);
            return value_null();
        }
        if (file_value.kind != VALUE_FILE || text_value.kind != VALUE_STRING) {
            char message[256];
            snprintf(message, sizeof(message), "%s expects a file reference and string", name);
            runtime_error_raise(message, 1004, "file operation");
            value_free(file_value);
            value_free(text_value);
            return value_null();
        }
        FILE *file = fopen(file_value.as.file_path, strcmp(name, "write") == 0 ? "wb" : "ab");
        if (!file) {
            char message[512];
            snprintf(message, sizeof(message), "could not write file: %s", file_value.as.file_path);
            runtime_error_raise(message, 1004, "file operation");
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
    size_t *gosub_stack = NULL;
    size_t gosub_count = 0;
    EvalResult result = eval_no_result();
    while (pc < stmt->as.function.body.count) {
        result = eval_stmt(stmt->as.function.body.items[pc]);
        if (result.did_return) {
            if (!result.return_has_value && gosub_count > 0) {
                pc = gosub_stack[--gosub_count];
                result = eval_no_result();
                continue;
            }
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
        if (result.did_gosub) {
            size_t target = 0;
            if (!find_function_label(stmt->as.function.body, result.gosub_label, &target)) {
                fprintf(stderr, "unknown label in function %s: %s\n",
                        stmt->as.function.name,
                        result.gosub_label);
                free(result.gosub_label);
                result = eval_no_result();
                break;
            }
            free(result.gosub_label);
            size_t *next = realloc(gosub_stack, sizeof(size_t) * (gosub_count + 1));
            if (!next) {
                abort();
            }
            gosub_stack = next;
            gosub_stack[gosub_count++] = pc + 1;
            result = eval_no_result();
            pc = target + 1;
            continue;
        }
        pc++;
    }
    free(gosub_stack);
    function_depth--;
    current_env = previous_env;
    env_clear(&local_env);
    if (result.did_return) {
        return result.value;
    }
    return value_null();
}

static void call_label(AstExpr *expr, char *buffer, size_t size) {
    if (expr->as.call.library) {
        snprintf(buffer, size, "%s.%s", expr->as.call.library, expr->as.call.name);
    } else {
        snprintf(buffer, size, "%s", expr->as.call.name);
    }
}

static Value eval_call(AstExpr *expr) {
    if (expr->as.call.library) {
        FunctionDef *function = function_resolve(expr->as.call.library, expr->as.call.name);
        if (function) {
            return eval_user_function(expr, function);
        }

        char message[256];
        char label[160];
        call_label(expr, label, sizeof(label));
        snprintf(message, sizeof(message), "invalid function call: %s", label);
        runtime_error_raise(message, 1003, "invalid function call");
        return value_null();
    }

    if (strcmp(expr->as.call.name, "error.clear") == 0) {
        if (expr->as.call.args.count != 0) {
            runtime_error_raise("error.clear expects no arguments", 1003, "invalid function call");
            return value_null();
        }
        error_clear_state();
        return value_null();
    }

    if (strcmp(expr->as.call.name, "lower") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("lower expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value value = eval_expr(expr->as.call.args.items[0]);
        if (value.kind != VALUE_STRING) {
            value_free(value);
            runtime_error_raise("lower expects a string", 1003, "invalid function call");
            return value_null();
        }
        char *text = copy_string(value.as.string);
        for (char *p = text; *p; p++) {
            *p = (char)tolower((unsigned char)*p);
        }
        Value result = value_string(text);
        free(text);
        value_free(value);
        return result;
    }

    if (strcmp(expr->as.call.name, "upper") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("upper expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value value = eval_expr(expr->as.call.args.items[0]);
        if (value.kind != VALUE_STRING) {
            value_free(value);
            runtime_error_raise("upper expects a string", 1003, "invalid function call");
            return value_null();
        }
        char *text = copy_string(value.as.string);
        for (char *p = text; *p; p++) {
            *p = (char)toupper((unsigned char)*p);
        }
        Value result = value_string(text);
        free(text);
        value_free(value);
        return result;
    }

    if (strcmp(expr->as.call.name, "round") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("round expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value value = eval_expr(expr->as.call.args.items[0]);
        Value places = eval_expr(expr->as.call.args.items[1]);
        double scale = 1.0;
        int n = (int)value_number_or_zero(places);
        for (int i = 0; i < n; i++) {
            scale *= 10.0;
        }
        double scaled = value_number_or_zero(value) * scale;
        long long rounded = scaled >= 0 ? (long long)(scaled + 0.5) : (long long)(scaled - 0.5);
        value_free(value);
        value_free(places);
        return value_number((double)rounded / scale);
    }

    if (strcmp(expr->as.call.name, "compare") == 0) {
        if (expr->as.call.args.count != 3) {
            runtime_error_raise("compare expects three arguments", 1003, "invalid function call");
            return value_null();
        }
        Value left = eval_expr(expr->as.call.args.items[0]);
        Value op = eval_expr(expr->as.call.args.items[1]);
        Value right = eval_expr(expr->as.call.args.items[2]);
        if (op.kind != VALUE_STRING) {
            value_free(left);
            value_free(op);
            value_free(right);
            runtime_error_raise("compare expects an operator string", 1003, "invalid function call");
            return value_null();
        }
        AstExpr fake = {0};
        fake.kind = AST_EXPR_BINARY;
        fake.as.binary.op = op.as.string;
        fake.as.binary.modifier = ast_modifier_none();
        Value result = eval_comparison(&fake, left, right);
        value_free(op);
        return result;
    }

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

    FunctionDef *function = function_resolve(NULL, expr->as.call.name);
    if (function) {
        return eval_user_function(expr, function);
    }

    if (expr->as.call.args.count != 1) {
        char message[256];
        snprintf(message, sizeof(message), "invalid function call: %s", expr->as.call.name);
        runtime_error_raise(message, 1003, "invalid function call");
        return value_null();
    }

    Value arg = eval_expr(expr->as.call.args.items[0]);
    if (!array_is_numeric(arg)) {
        char message[256];
        snprintf(message, sizeof(message), "%s expects a numeric array", expr->as.call.name);
        runtime_error_raise(message, 1003, "invalid function call");
        value_free(arg);
        return value_null();
    }

    const char *name = expr->as.call.name;
    size_t count = arg.as.array.count;
    double result = 0.0;

    if (strcmp(name, "len") == 0) {
        result = (double)count;
    } else if (count == 0) {
        char message[256];
        snprintf(message, sizeof(message), "%s expects a non-empty array", name);
        runtime_error_raise(message, 1003, "invalid function call");
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
        char message[256];
        snprintf(message, sizeof(message), "unknown function: %s", name);
        runtime_error_raise(message, 1003, "invalid function call");
        value_free(arg);
        return value_null();
    }

    value_free(arg);
    return value_number(result);
}

static char *copy_trimmed_span(const char *start, size_t length) {
    while (length > 0 && (*start == ' ' || *start == '\t')) {
        start++;
        length--;
    }
    while (length > 0 && (start[length - 1] == ' ' || start[length - 1] == '\t')) {
        length--;
    }
    char *text = malloc(length + 1);
    if (!text) {
        abort();
    }
    memcpy(text, start, length);
    text[length] = '\0';
    return text;
}

static Value eval_modifier_arg_text(const char *text) {
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) {
        len--;
    }
    if (len == 0) {
        return value_null();
    }
    if (text[0] == '"' && len >= 2 && text[len - 1] == '"') {
        char *inner = copy_trimmed_span(text + 1, len - 2);
        Value value = value_string(inner);
        free(inner);
        return value;
    }
    char *end = NULL;
    double number = strtod(text, &end);
    if (end && (size_t)(end - text) == len) {
        return value_number(number);
    }

    char *name = copy_trimmed_span(text, len);
    Value value = env_get(name);
    free(name);
    return value;
}

static void bind_modifier_args(AstStmt *stmt, const char *args_text) {
    size_t expected = stmt->as.modifier.params.count;
    while (*args_text == ' ' || *args_text == '\t') {
        args_text++;
    }
    if (expected == 0) {
        if (*args_text != '\0') {
            char message[256];
            snprintf(message, sizeof(message), "modifier %s expects no arguments",
                     stmt->as.modifier.name);
            runtime_error_raise(message, 1003, "modifier");
        }
        return;
    }
    if (*args_text == '\0') {
        char message[256];
        snprintf(message, sizeof(message), "modifier %s expects %zu arguments",
                 stmt->as.modifier.name,
                 expected);
        runtime_error_raise(message, 1003, "modifier");
        return;
    }

    if (expected == 1) {
        env_set(stmt->as.modifier.params.items[0], eval_modifier_arg_text(args_text));
        return;
    }

    const char *start = args_text;
    for (size_t i = 0; i < expected; i++) {
        const char *end = strchr(start, ',');
        if (!end && i + 1 < expected) {
            char message[256];
            snprintf(message, sizeof(message), "modifier %s expects %zu arguments",
                     stmt->as.modifier.name,
                     expected);
            runtime_error_raise(message, 1003, "modifier");
            return;
        }
        char *arg_text = end ? copy_trimmed_span(start, (size_t)(end - start)) : copy_string(start);
        env_set(stmt->as.modifier.params.items[i], eval_modifier_arg_text(arg_text));
        free(arg_text);
        if (!end) {
            break;
        }
        start = end + 1;
    }
}

static Value eval_assign_modifier(AstModifierUse use, Value value) {
    const char *args_text = "";
    ModifierDef *modifier = modifier_resolve(use, "assign", &args_text);
    if (!modifier) {
        char message[256];
        char label[160];
        modifier_use_label(use, label, sizeof(label));
        snprintf(message, sizeof(message), "assign modifier not found: %s", label);
        runtime_error_raise(message, 1003, "modifier");
        value_free(value);
        return value_null();
    }

    Env local_env = {0};
    local_env.parent = &global_env;
    Env *previous_env = current_env;
    current_env = &local_env;
    env_set("value", value);
    bind_modifier_args(modifier->stmt, args_text);

    EvalResult result = eval_stmt_list(modifier->stmt->as.modifier.body);
    current_env = previous_env;
    env_clear(&local_env);
    if (result.did_return) {
        return result.value;
    }
    return value_null();
}

static Value eval_compare_modifier(AstModifierUse use, const char *op, Value left, Value right) {
    const char *args_text = "";
    ModifierDef *modifier = modifier_resolve(use, "compare", &args_text);
    if (!modifier) {
        char message[256];
        char label[160];
        modifier_use_label(use, label, sizeof(label));
        snprintf(message, sizeof(message), "compare modifier not found: %s", label);
        runtime_error_raise(message, 1003, "modifier");
        value_free(left);
        value_free(right);
        return value_null();
    }

    Env local_env = {0};
    local_env.parent = &global_env;
    Env *previous_env = current_env;
    current_env = &local_env;
    env_set("left", left);
    env_set("right", right);
    env_set("operator", value_string(op));
    bind_modifier_args(modifier->stmt, args_text);

    EvalResult result = eval_stmt_list(modifier->stmt->as.modifier.body);
    current_env = previous_env;
    env_clear(&local_env);
    if (result.did_return) {
        return result.value;
    }
    return value_null();
}

static Value eval_comparison(AstExpr *expr, Value left, Value right) {
    const char *op = expr->as.binary.op;
    int result = 0;

    const char *modifier_args_text = "";
    if (expr->as.binary.modifier.name &&
        modifier_resolve(expr->as.binary.modifier, "compare", &modifier_args_text)) {
        return eval_compare_modifier(expr->as.binary.modifier, op, left, right);
    }

    if (expr->as.binary.modifier.name &&
        (expr->as.binary.modifier.library ||
         !modifier_is(expr->as.binary.modifier.name, "caseless"))) {
        char message[256];
        char label[160];
        modifier_use_label(expr->as.binary.modifier, label, sizeof(label));
        snprintf(message, sizeof(message), "compare modifier not found: %s", label);
        runtime_error_raise(message, 1003, "modifier");
        value_free(left);
        value_free(right);
        return value_null();
    }

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
    } else if (!expr->as.binary.modifier.library &&
        modifier_is(expr->as.binary.modifier.name, "caseless") &&
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
    if (error_action_pending()) {
        value_free(left);
        return value_null();
    }
    Value right = eval_expr(expr->as.binary.right);
    if (error_action_pending()) {
        value_free(left);
        value_free(right);
        return value_null();
    }

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
    if (strcmp(op, "/") == 0) {
        if (b == 0.0) {
            runtime_error_raise("division by zero", 1002, "division");
            return value_null();
        }
        return value_number(a / b);
    }

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
        if (error_action_pending()) {
            value_free(array);
            value_free(index);
            return value_null();
        }
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
        if (expr->as.field.object->kind == AST_EXPR_IDENT &&
            strcmp(expr->as.field.object->as.ident, "error") == 0) {
            Value object = value_error_object();
            RecordField *field = record_find(&object, expr->as.field.field);
            Value result = field ? value_copy(*field->value) : value_null();
            value_free(object);
            return result;
        }
        Value object = eval_expr(expr->as.field.object);
        if (error_action_pending()) {
            value_free(object);
            return value_null();
        }
        if (object.kind != VALUE_RECORD) {
            runtime_error_raise("field access expects a record", 1003, "field access");
            value_free(object);
            return value_null();
        }
        RecordField *field = record_find(&object, expr->as.field.field);
        if (!field) {
            char message[256];
            snprintf(message, sizeof(message), "unknown record field: %s", expr->as.field.field);
            runtime_error_raise(message, 1003, "field access");
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

static Value apply_assignment_modifier(AstModifierUse modifier, Value value) {
    if (!modifier.name) {
        return value;
    }

    const char *args_text = "";
    if (modifier_resolve(modifier, "assign", &args_text)) {
        return eval_assign_modifier(modifier, value);
    }

    if (!modifier.library && strcmp(modifier.name, "USD") == 0) {
        /* TODO: add a money runtime value. For now USD stores the numeric amount. */
        double amount = value_number_or_zero(value);
        value_free(value);
        return value_number(amount);
    }
    if (!modifier.library && strcmp(modifier.name, "date") == 0) {
        DateTime datetime;
        if (value.kind != VALUE_STRING || !parse_date_value(value.as.string, &datetime)) {
            fprintf(stderr, "date modifier expects an ISO-like date string\n");
            value_free(value);
            return value_null();
        }
        value_free(value);
        return value_datetime(datetime);
    }
    if (!modifier.library && strcmp(modifier.name, "time") == 0) {
        DateTime datetime;
        if (value.kind != VALUE_STRING || !parse_time_value(value.as.string, &datetime)) {
            fprintf(stderr, "time modifier expects an ISO-like time string\n");
            value_free(value);
            return value_null();
        }
        value_free(value);
        return value_datetime(datetime);
    }
    if (!modifier.library && strcmp(modifier.name, "file") == 0) {
        if (value.kind != VALUE_STRING) {
            fprintf(stderr, "file modifier expects a path string\n");
            value_free(value);
            return value_null();
        }
        Value file_value = value_file(value.as.string);
        value_free(value);
        return file_value;
    }
    if (!modifier.library && strcmp(modifier.name, "dir") == 0) {
        if (value.kind != VALUE_STRING) {
            fprintf(stderr, "dir modifier expects a path string\n");
            value_free(value);
            return value_null();
        }
        Value dir_value = value_dir(value.as.string);
        value_free(value);
        return dir_value;
    }

    char message[256];
    char label[160];
    modifier_use_label(modifier, label, sizeof(label));
    snprintf(message, sizeof(message), "assign modifier not found: %s", label);
    runtime_error_raise(message, 1003, "modifier");
    value_free(value);
    return value_null();
}

static EvalResult eval_stmt(AstStmt *stmt) {
    EvalResult no_result = eval_no_result();
    int previous_line = current_line;
    int previous_column = current_column;
    current_line = stmt->line;
    current_column = stmt->column;

    switch (stmt->kind) {
    case AST_STMT_ASSIGN: {
        int before_error = error_generation;
        Value value = eval_expr(stmt->as.assign.value);
        if (error_generation != before_error) {
            value_free(value);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        value = apply_assignment_modifier(stmt->as.assign.modifier, value);
        if (error_generation != before_error) {
            value_free(value);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        env_set(stmt->as.assign.name, value);
        watcher_trigger(stmt->as.assign.name);
        break;
    }
    case AST_STMT_FIELD_ASSIGN: {
        Symbol *symbol = env_find(stmt->as.field_assign.name);
        if (!symbol || symbol->value.kind != VALUE_RECORD) {
            fprintf(stderr, "field assignment expects a record variable: %s\n",
                    stmt->as.field_assign.name);
            current_line = previous_line;
            current_column = previous_column;
            return no_result;
        }
        int before_error = error_generation;
        Value value = eval_expr(stmt->as.field_assign.value);
        if (error_generation != before_error) {
            value_free(value);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        record_set(&symbol->value, stmt->as.field_assign.field, value);
        break;
    }
    case AST_STMT_PRINT: {
        int before_error = error_generation;
        Value value = eval_expr(stmt->as.print);
        if (error_generation != before_error) {
            value_free(value);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        value_print(value);
        value_free(value);
        break;
    }
    case AST_STMT_EXPR: {
        int before_error = error_generation;
        Value value = eval_expr(stmt->as.expr_stmt);
        if (error_generation != before_error) {
            value_free(value);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        value_free(value);
        break;
    }
    case AST_STMT_WITH_LOCK: {
        int before_error = error_generation;
        Value file_value = eval_expr(stmt->as.with_lock.file);
        if (error_generation != before_error) {
            value_free(file_value);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        if (file_value.kind != VALUE_FILE) {
            runtime_error_raise("with lock expects a file reference", 1004, "file operation");
            value_free(file_value);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        char *path = copy_string(file_value.as.file_path);
        value_free(file_value);
        if (lock_path(path)) {
            EvalResult result = eval_stmt_list(stmt->as.with_lock.body);
            unlock_path(path);
            if (result.did_return || result.did_goto || result.did_gosub || result.did_stop) {
                free(path);
                current_line = previous_line;
                current_column = previous_column;
                return result;
            }
        }
        free(path);
        break;
    }
    case AST_STMT_FOR_EACH: {
        int before_error = error_generation;
        Value iterable = eval_expr(stmt->as.for_each.iterable);
        if (error_generation != before_error) {
            value_free(iterable);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        if (iterable.kind != VALUE_ARRAY) {
            runtime_error_raise("for in expects an array", 1003, "invalid operation");
            value_free(iterable);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        for (size_t i = 0; i < iterable.as.array.count; i++) {
            env_set(stmt->as.for_each.name, value_copy(iterable.as.array.items[i]));
            EvalResult result = eval_stmt_list(stmt->as.for_each.body);
            if (result.did_return || result.did_goto || result.did_gosub || result.did_stop) {
                value_free(iterable);
                current_line = previous_line;
                current_column = previous_column;
                return result;
            }
        }
        value_free(iterable);
        break;
    }
    case AST_STMT_FUNCTION:
        function_register(stmt);
        break;
    case AST_STMT_MODIFIER:
        modifier_register(stmt);
        break;
    case AST_STMT_PROGRAM:
    case AST_STMT_LIBRARY:
        break;
    case AST_STMT_USE:
        library_import(stmt->as.use_stmt.name, stmt->as.use_stmt.path);
        if (error_action_pending()) {
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        break;
    case AST_STMT_WATCH:
        watcher_register(stmt);
        break;
    case AST_STMT_WITHOUT_WATCHERS: {
        watcher_suppressed++;
        EvalResult result = eval_stmt_list(stmt->as.without_watchers);
        watcher_suppressed--;
        if (result.did_return || result.did_goto || result.did_gosub || result.did_stop) {
            current_line = previous_line;
            current_column = previous_column;
            return result;
        }
        break;
    }
    case AST_STMT_ON_ERROR_GOTO:
        free(error_goto_label);
        error_goto_label = copy_string(stmt->as.on_error_label);
        error_mode = ERROR_MODE_GOTO;
        break;
    case AST_STMT_ON_ERROR_RESUME_NEXT:
        free(error_goto_label);
        error_goto_label = NULL;
        error_mode = ERROR_MODE_RESUME_NEXT;
        break;
    case AST_STMT_ON_ERROR_STOP:
        free(error_goto_label);
        error_goto_label = NULL;
        error_mode = ERROR_MODE_STOP;
        break;
    case AST_STMT_ERROR: {
        int before_error = error_generation;
        Value value = eval_expr(stmt->as.error_message);
        if (error_generation != before_error) {
            value_free(value);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        const char *message = value.kind == VALUE_STRING ? value.as.string : "explicit error";
        runtime_error_raise(message, 2000, "explicit error");
        value_free(value);
        if (error_action_pending()) {
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        break;
    }
    case AST_STMT_RETURN: {
        int before_error = error_generation;
        Value value = stmt->as.return_expr ? eval_expr(stmt->as.return_expr) : value_null();
        if (error_generation != before_error) {
            value_free(value);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        current_line = previous_line;
        current_column = previous_column;
        return eval_return(value, stmt->as.return_expr != NULL);
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
    case AST_STMT_GOSUB: {
        if (function_depth == 0) {
            fprintf(stderr, "gosub is only supported inside functions for now\n");
            break;
        }
        return eval_gosub(stmt->as.gosub_label);
    }
    case AST_STMT_IF: {
        int before_error = error_generation;
        Value condition = eval_expr(stmt->as.if_stmt.condition);
        if (error_generation != before_error) {
            value_free(condition);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        int truth = value_truthy(condition);
        value_free(condition);
        if (truth) {
            EvalResult result = eval_stmt_list(stmt->as.if_stmt.body);
            if (result.did_return || result.did_goto || result.did_gosub || result.did_stop) {
                current_line = previous_line;
                current_column = previous_column;
                return result;
            }
        }
        break;
    }
    }

    current_line = previous_line;
    current_column = previous_column;
    return no_result;
}

static EvalResult eval_stmt_list(AstStmtList statements) {
    size_t pc = 0;
    while (pc < statements.count) {
        EvalResult result = eval_stmt(statements.items[pc]);
        if (result.did_goto) {
            size_t target = 0;
            if (find_function_label(statements, result.goto_label, &target)) {
                free(result.goto_label);
                pc = target + 1;
                continue;
            }
            return result;
        }
        if (result.did_return || result.did_gosub || result.did_stop) {
            return result;
        }
        pc++;
    }
    return eval_no_result();
}

int eval_program(AstStmtList program) {
    active_root = program;
    AstStmt *program_block = NULL;
    for (size_t i = 0; i < program.count; i++) {
        if (program.items[i]->kind == AST_STMT_PROGRAM) {
            if (program_block) {
                runtime_error_raise("only one program block may execute", 1003, "program");
                break;
            }
            program_block = program.items[i];
        }
    }

    EvalResult result = runtime_stopped
        ? eval_stop()
        : eval_stmt_list(program_block ? program_block->as.program.body : program);
    int exit_status = result.did_stop ? 1 : 0;
    if (result.did_return) {
        value_free(result.value);
    }
    if (result.did_goto) {
        free(result.goto_label);
    }
    if (result.did_gosub) {
        free(result.gosub_label);
    }
    free(error_goto_label);
    error_goto_label = NULL;
    free(pending_error_goto_label);
    pending_error_goto_label = NULL;
    error_clear_state();
    error_mode = ERROR_MODE_STOP;
    runtime_stopped = 0;
    lock_clear();
    watcher_clear();
    modifier_clear();
    function_clear();
    loaded_files_clear();
    use_pairs_clear(&used_pairs, &used_pair_count);
    use_pairs_clear(&use_stack, &use_stack_count);
    free(current_import_path);
    current_import_path = NULL;
    free(root_source_path);
    root_source_path = NULL;
    env_clear(&global_env);
    active_root = ast_stmt_list_empty();
    return exit_status;
}

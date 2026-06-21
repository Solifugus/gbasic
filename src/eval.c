#include "eval.h"
#include "builtins.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

#if HAVE_GTK
#include <gtk/gtk.h>
#endif

#if HAVE_LIBPQ
#include <libpq-fe.h>
#endif

#if HAVE_SQLITE3
#include <sqlite3.h>
#endif

#if HAVE_LIBCURL
#include <curl/curl.h>
#endif

#define SECURE_TOKEN_MAX_LENGTH 4096

int parse_source(const char *source, AstStmtList *out_program);
void parse_set_source_path(const char *path);

typedef struct PgConnectionValue PgConnectionValue;
typedef struct SqliteConnectionValue SqliteConnectionValue;
typedef struct WebServer WebServer;
typedef struct WebServerClient WebServerClient;

typedef enum {
    VALUE_NULL,
    VALUE_UNKNOWN,
    VALUE_NUMBER,
    VALUE_STRING,
    VALUE_BOOL,
    VALUE_ARRAY,
    VALUE_RECORD,
    VALUE_DATETIME,
    VALUE_DURATION,
    VALUE_MONEY,
    VALUE_FILE,
    VALUE_DIR,
    VALUE_POSTGRES_CONNECTION,
    VALUE_SQLITE_CONNECTION
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
        long long cents;
        char *file_path;
        char *dir_path;
        PgConnectionValue *postgres_connection;
        SqliteConnectionValue *sqlite_connection;
    } as;
};

struct PgConnectionValue {
#if HAVE_LIBPQ
    PGconn *connection;
#endif
    size_t ref_count;
    int closed;
};

struct SqliteConnectionValue {
#if HAVE_SQLITE3
    sqlite3 *connection;
#endif
    size_t ref_count;
    int closed;
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
    int pending;
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
    int did_break;
    int did_continue;
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

typedef struct {
    char *path;
    AstStmt *library;
} LibraryMatch;

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
static int watcher_drain_origin_line = 0;
static int watcher_drain_origin_column = 0;
static int function_depth = 0;
static int loop_depth = 0;
static int consider_depth = 0;
static int watcher_suppressed = 0;
static int watcher_draining = 0;
enum {
    WATCHER_EXECUTION_LIMIT = 10000,
    WATCHER_CYCLE_ERROR_CODE = 1005
};
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
static int gui_library_loaded = 0;
static int pg_library_loaded = 0;
static int sqlite_library_loaded = 0;
static int webclient_library_loaded = 0;
static int webclient_curl_initialized = 0;
static int webserver_library_loaded = 0;
static WebServer *webservers = NULL;
static size_t webserver_count = 0;
static unsigned long webserver_next_id = 1;

static Value webserver_eval_call(AstExpr *expr);
static int webserver_run_event_loop(void);
static void webserver_clear(void);

#if HAVE_GTK
typedef struct GuiNativeWindow GuiNativeWindow;

typedef struct {
    char *id;
    GtkWidget *widget;
    Value *widget_record;
    GuiNativeWindow *window;
} GuiWidgetBinding;

typedef struct {
    char *window_handle_id;
    char *widget_id;
    char *field_name;
    Value new_value;
} GuiPendingMutation;

struct GuiNativeWindow {
    char *handle_id;
    char *watch_root_path;
    GtkWidget *window;
    GuiWidgetBinding **bindings;
    size_t binding_count;
    GuiPendingMutation *pending_mutations;
    size_t pending_mutation_count;
    int sync_depth;
    int closed;
};

static GuiNativeWindow *gui_windows = NULL;
static size_t gui_window_count = 0;
static int gui_next_window_id = 1;
static int gui_gtk_init_attempted = 0;
static int gui_gtk_available = 0;
#endif

static int path_equal(const char *left, const char *right) {
    return left && right && strcmp(left, right) == 0;
}

static const char *value_kind_name(ValueKind kind) {
    switch (kind) {
    case VALUE_NULL:
        return "nothing";
    case VALUE_UNKNOWN:
        return "unknown";
    case VALUE_NUMBER:
        return "number";
    case VALUE_STRING:
        return "string";
    case VALUE_BOOL:
        return "boolean";
    case VALUE_ARRAY:
        return "array";
    case VALUE_RECORD:
        return "record";
    case VALUE_DATETIME:
        return "datetime";
    case VALUE_DURATION:
        return "duration";
    case VALUE_MONEY:
        return "money";
    case VALUE_FILE:
        return "file";
    case VALUE_DIR:
        return "directory";
    case VALUE_POSTGRES_CONNECTION:
        return "postgres_connection";
    case VALUE_SQLITE_CONNECTION:
        return "sqlite_connection";
    }
    return "value";
}

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

static Value value_unknown(void) {
    Value value = {0};
    value.kind = VALUE_UNKNOWN;
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

static Value value_money(long long cents) {
    Value value = {0};
    value.kind = VALUE_MONEY;
    value.as.cents = cents;
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

static Value value_postgres_connection(PgConnectionValue *connection) {
    Value value = {0};
    value.kind = VALUE_POSTGRES_CONNECTION;
    value.as.postgres_connection = connection;
    return value;
}

static Value value_sqlite_connection(SqliteConnectionValue *connection) {
    Value value = {0};
    value.kind = VALUE_SQLITE_CONNECTION;
    value.as.sqlite_connection = connection;
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

static EvalResult eval_break(void) {
    EvalResult result = {0};
    result.did_break = 1;
    result.value = value_null();
    return result;
}

static EvalResult eval_continue(void) {
    EvalResult result = {0};
    result.did_continue = 1;
    result.value = value_null();
    return result;
}

static int eval_result_exits_block(EvalResult result) {
    return result.did_return || result.did_goto || result.did_gosub ||
           result.did_stop || result.did_break || result.did_continue;
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

static const char *runtime_error_path(void) {
    if (current_import_path && current_import_path[0]) {
        return current_import_path;
    }
    if (root_source_path && root_source_path[0]) {
        return root_source_path;
    }
    return NULL;
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
        const char *path = runtime_error_path();
        if (path) {
            fprintf(stderr, "runtime error at %s:%d:%d: %s\n",
                    path,
                    current_error.line,
                    current_error.column,
                    current_error.message);
        } else {
            fprintf(stderr, "runtime error at %d:%d: %s\n",
                    current_error.line,
                    current_error.column,
                    current_error.message);
        }
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
    if (value.kind == VALUE_POSTGRES_CONNECTION) {
        value.as.postgres_connection->ref_count++;
        return value_postgres_connection(value.as.postgres_connection);
    }
    if (value.kind == VALUE_SQLITE_CONNECTION) {
        value.as.sqlite_connection->ref_count++;
        return value_sqlite_connection(value.as.sqlite_connection);
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
    } else if (value.kind == VALUE_POSTGRES_CONNECTION) {
        PgConnectionValue *connection = value.as.postgres_connection;
        if (connection && --connection->ref_count == 0) {
#if HAVE_LIBPQ
            if (!connection->closed && connection->connection) {
                PQfinish(connection->connection);
            }
#endif
            free(connection);
        }
    } else if (value.kind == VALUE_SQLITE_CONNECTION) {
        SqliteConnectionValue *connection = value.as.sqlite_connection;
        if (connection && --connection->ref_count == 0) {
#if HAVE_SQLITE3
            if (!connection->closed && connection->connection) {
                sqlite3_close(connection->connection);
            }
#endif
            free(connection);
        }
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
    case VALUE_MONEY:
        return value.as.cents != 0;
    case VALUE_FILE:
        return value.as.file_path[0] != '\0';
    case VALUE_DIR:
        return value.as.dir_path[0] != '\0';
    case VALUE_POSTGRES_CONNECTION:
        runtime_error_raise("postgres connection cannot be used as a condition",
                            2001,
                            "postgres");
        return 0;
    case VALUE_SQLITE_CONNECTION:
        runtime_error_raise("sqlite connection cannot be used as a condition",
                            2002,
                            "sqlite");
        return 0;
    case VALUE_NULL:
        return 0;
    case VALUE_UNKNOWN:
        runtime_error_raise("unknown cannot be used as a condition", 1003, "unknown");
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

static int value_number_for_arithmetic(Value value, const char *op, double *out_number) {
    if (value.kind == VALUE_NUMBER) {
        *out_number = value.as.number;
        return 1;
    }

    char message[256];
    snprintf(message,
             sizeof(message),
             "arithmetic operator '%s' expected number but got %s",
             op,
             value_kind_name(value.kind));
    runtime_error_raise(message, 1003, "arithmetic");
    return 0;
}

static long long round_to_cents(double amount) {
    double scaled = amount * 100.0;
    return scaled >= 0 ? (long long)(scaled + 0.5) : (long long)(scaled - 0.5);
}

static void value_print(Value value) {
    switch (value.kind) {
    case VALUE_NULL:
        printf("nothing\n");
        break;
    case VALUE_UNKNOWN:
        printf("unknown\n");
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
    case VALUE_MONEY: {
        long long cents = value.as.cents;
        if (cents < 0) {
            printf("-");
            cents = -cents;
        }
        printf("%lld.%02lld\n", cents / 100, cents % 100);
        break;
    }
    case VALUE_FILE:
        printf("%s\n", value.as.file_path);
        break;
    case VALUE_DIR:
        printf("%s\n", value.as.dir_path);
        break;
    case VALUE_POSTGRES_CONNECTION:
        printf("<postgres_connection>\n");
        break;
    case VALUE_SQLITE_CONNECTION:
        printf("<sqlite_connection>\n");
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

static const RecordField *record_find_const(const Value *record, const char *name) {
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

static int value_storage_equal(const Value *left, const Value *right) {
    if (left->kind != right->kind) {
        return 0;
    }

    switch (left->kind) {
    case VALUE_NULL:
    case VALUE_UNKNOWN:
        return 1;
    case VALUE_NUMBER:
        /* If NaN enters the runtime through native code, treat it as changed. */
        return left->as.number == right->as.number;
    case VALUE_STRING:
        return strcmp(left->as.string, right->as.string) == 0;
    case VALUE_BOOL:
        return left->as.boolean == right->as.boolean;
    case VALUE_ARRAY:
        if (left->as.array.count != right->as.array.count) {
            return 0;
        }
        for (size_t i = 0; i < left->as.array.count; i++) {
            if (!value_storage_equal(&left->as.array.items[i], &right->as.array.items[i])) {
                return 0;
            }
        }
        return 1;
    case VALUE_RECORD:
        if (left->as.record.count != right->as.record.count) {
            return 0;
        }
        for (size_t i = 0; i < left->as.record.count; i++) {
            const RecordField *left_field = &left->as.record.fields[i];
            const RecordField *right_field = record_find_const(right, left_field->name);
            if (!right_field || !value_storage_equal(left_field->value, right_field->value)) {
                return 0;
            }
        }
        return 1;
    case VALUE_DATETIME:
        return left->as.datetime.year == right->as.datetime.year &&
            left->as.datetime.month == right->as.datetime.month &&
            left->as.datetime.day == right->as.datetime.day &&
            left->as.datetime.hour == right->as.datetime.hour &&
            left->as.datetime.minute == right->as.datetime.minute &&
            left->as.datetime.second == right->as.datetime.second &&
            left->as.datetime.time_only == right->as.datetime.time_only &&
            left->as.datetime.precision == right->as.datetime.precision;
    case VALUE_DURATION:
        return left->as.duration.years == right->as.duration.years &&
            left->as.duration.months == right->as.duration.months &&
            left->as.duration.weeks == right->as.duration.weeks &&
            left->as.duration.days == right->as.duration.days &&
            left->as.duration.hours == right->as.duration.hours &&
            left->as.duration.minutes == right->as.duration.minutes &&
            left->as.duration.seconds == right->as.duration.seconds;
    case VALUE_MONEY:
        return left->as.cents == right->as.cents;
    case VALUE_FILE:
        return strcmp(left->as.file_path, right->as.file_path) == 0;
    case VALUE_DIR:
        return strcmp(left->as.dir_path, right->as.dir_path) == 0;
    case VALUE_POSTGRES_CONNECTION:
        return left->as.postgres_connection == right->as.postgres_connection;
    case VALUE_SQLITE_CONNECTION:
        return left->as.sqlite_connection == right->as.sqlite_connection;
    }
    return 0;
}

static int string_equal_caseless(const char *left, const char *right);

static int value_is_integer_number(Value value) {
    if (value.kind != VALUE_NUMBER) {
        return 0;
    }
    double whole = (double)(int)value.as.number;
    return value.as.number == whole;
}

static int gui_component_is_container(const char *component) {
    return strcmp(component, "vert") == 0 || strcmp(component, "horiz") == 0;
}

static int gui_component_known(const char *component) {
    return strcmp(component, "vert") == 0 ||
           strcmp(component, "horiz") == 0 ||
           strcmp(component, "label") == 0 ||
           strcmp(component, "input") == 0 ||
           strcmp(component, "button") == 0 ||
           strcmp(component, "spacer") == 0;
}

static RecordField *gui_require_record_field(Value *record,
                                             const char *name,
                                             ValueKind kind,
                                             const char *context) {
    RecordField *field = record_find(record, name);
    if (!field) {
        char message[256];
        snprintf(message, sizeof(message), "%s missing required field: %s", context, name);
        runtime_error_raise(message, 1003, "gui");
        return NULL;
    }
    if (field->value->kind != kind) {
        char message[256];
        snprintf(message, sizeof(message), "%s field '%s' has wrong type", context, name);
        runtime_error_raise(message, 1003, "gui");
        return NULL;
    }
    return field;
}

static int gui_optional_bool_field(Value *record, const char *name, int fallback) {
    RecordField *field = record_find(record, name);
    if (!field) {
        return fallback;
    }
    if (field->value->kind != VALUE_BOOL) {
        char message[256];
        snprintf(message, sizeof(message), "gui field '%s' must be boolean", name);
        runtime_error_raise(message, 1003, "gui");
        return fallback;
    }
    return field->value->as.boolean;
}

static int gui_optional_int_field(Value *record, const char *name, int fallback) {
    RecordField *field = record_find(record, name);
    if (!field) {
        return fallback;
    }
    if (!value_is_integer_number(*field->value)) {
        char message[256];
        snprintf(message, sizeof(message), "gui field '%s' must be an integer number", name);
        runtime_error_raise(message, 1003, "gui");
        return fallback;
    }
    return (int)field->value->as.number;
}

typedef enum {
    GUI_SPACING_START,
    GUI_SPACING_END,
    GUI_SPACING_BETWEEN,
    GUI_SPACING_AROUND,
    GUI_SPACING_CENTER
} GuiSpacingMode;

static int gui_spacing_mode_from_text(const char *text, GuiSpacingMode *out_mode) {
    if (!text || !out_mode) {
        return 0;
    }
    if (strcmp(text, "start") == 0) {
        *out_mode = GUI_SPACING_START;
        return 1;
    }
    if (strcmp(text, "end") == 0) {
        *out_mode = GUI_SPACING_END;
        return 1;
    }
    if (strcmp(text, "between") == 0) {
        *out_mode = GUI_SPACING_BETWEEN;
        return 1;
    }
    if (strcmp(text, "around") == 0) {
        *out_mode = GUI_SPACING_AROUND;
        return 1;
    }
    if (strcmp(text, "center") == 0) {
        *out_mode = GUI_SPACING_CENTER;
        return 1;
    }
    return 0;
}

static int gui_spacing_mode_for_record(Value *record, GuiSpacingMode *out_mode) {
    RecordField *field = record_find(record, "spacing");
    if (!field) {
        *out_mode = GUI_SPACING_START;
        return 1;
    }
    if (field->value->kind != VALUE_STRING ||
        !gui_spacing_mode_from_text(field->value->as.string, out_mode)) {
        runtime_error_raise("gui field 'spacing' must be one of start, end, between, around, center",
                            1003,
                            "gui");
        return 0;
    }
    return 1;
}

typedef struct {
    char **ids;
    size_t count;
} GuiIdSet;

static void gui_id_set_clear(GuiIdSet *set) {
    for (size_t i = 0; i < set->count; i++) {
        free(set->ids[i]);
    }
    free(set->ids);
    set->ids = NULL;
    set->count = 0;
}

static int gui_id_set_contains(GuiIdSet *set, const char *id) {
    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->ids[i], id) == 0) {
            return 1;
        }
    }
    return 0;
}

static void gui_id_set_add(GuiIdSet *set, const char *id) {
    char **next = realloc(set->ids, sizeof(char *) * (set->count + 1));
    if (!next) {
        abort();
    }
    set->ids = next;
    set->ids[set->count++] = copy_string(id);
}

static int gui_window_field_reserved(const char *name) {
    return strcmp(name, "_window") == 0 ||
           strcmp(name, "_root") == 0 ||
           strcmp(name, "_ids") == 0;
}

static int gui_id_is_keyword(const char *name) {
    static const char *keywords[] = {
        "if", "then", "else", "end", "print", "for", "to", "step", "while",
        "consider", "break", "continue", "function", "return", "goto", "gosub",
        "watch", "without", "watchers", "modifier", "program", "library",
        "load", "use", "export", "on", "error", "resume", "next", "stop",
        "true", "false", "nothing", "unknown", "and", "or", "not", "with", "in"
    };

    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (string_equal_caseless(name, keywords[i])) {
            return 1;
        }
    }
    return 0;
}

static int gui_id_is_exposable_identifier(const char *id) {
    if (!id || id[0] == '\0') {
        return 0;
    }
    if (!(isalpha((unsigned char)id[0]) || id[0] == '_')) {
        return 0;
    }
    for (size_t i = 1; id[i] != '\0'; i++) {
        if (!(isalnum((unsigned char)id[i]) || id[i] == '_')) {
            return 0;
        }
    }
    return !gui_id_is_keyword(id);
}

static int gui_validate_widget_tree(Value *widget, GuiIdSet *ids) {
    if (widget->kind != VALUE_RECORD) {
        runtime_error_raise("gui widget must be a record", 1003, "gui");
        return 0;
    }

    RecordField *id_field = gui_require_record_field(widget, "id", VALUE_STRING, "gui widget");
    if (!id_field) {
        return 0;
    }
    if (id_field->value->as.string[0] == '\0') {
        runtime_error_raise("gui widget id must not be empty", 1003, "gui");
        return 0;
    }
    if (gui_id_set_contains(ids, id_field->value->as.string)) {
        char message[256];
        snprintf(message, sizeof(message), "duplicate gui widget id: %s", id_field->value->as.string);
        runtime_error_raise(message, 1003, "gui");
        return 0;
    }
    gui_id_set_add(ids, id_field->value->as.string);

    RecordField *component_field =
        gui_require_record_field(widget, "component", VALUE_STRING, "gui widget");
    if (!component_field) {
        return 0;
    }
    const char *component = component_field->value->as.string;
    if (!gui_component_known(component)) {
        char message[256];
        snprintf(message, sizeof(message), "unknown gui component: %s", component);
        runtime_error_raise(message, 1003, "gui");
        return 0;
    }

    if (record_find(widget, "width") && !value_is_integer_number(*record_find(widget, "width")->value)) {
        runtime_error_raise("gui field 'width' must be an integer number", 1003, "gui");
        return 0;
    }
    if (record_find(widget, "height") && !value_is_integer_number(*record_find(widget, "height")->value)) {
        runtime_error_raise("gui field 'height' must be an integer number", 1003, "gui");
        return 0;
    }
    if (record_find(widget, "enabled") && record_find(widget, "enabled")->value->kind != VALUE_BOOL) {
        runtime_error_raise("gui field 'enabled' must be boolean", 1003, "gui");
        return 0;
    }
    if (record_find(widget, "visible") && record_find(widget, "visible")->value->kind != VALUE_BOOL) {
        runtime_error_raise("gui field 'visible' must be boolean", 1003, "gui");
        return 0;
    }

    if (gui_component_is_container(component)) {
        GuiSpacingMode spacing_mode = GUI_SPACING_START;
        if (!gui_spacing_mode_for_record(widget, &spacing_mode)) {
            return 0;
        }
        RecordField *contains = record_find(widget, "contains");
        if (!contains) {
            Value *items = NULL;
            record_set(widget, "contains", value_array(items, 0));
            contains = record_find(widget, "contains");
        }
        if (contains->value->kind != VALUE_ARRAY) {
            runtime_error_raise("gui container field 'contains' must be an array", 1003, "gui");
            return 0;
        }
        for (size_t i = 0; i < contains->value->as.array.count; i++) {
            if (!gui_validate_widget_tree(&contains->value->as.array.items[i], ids)) {
                return 0;
            }
        }
        return 1;
    }

    if (strcmp(component, "label") == 0) {
        return gui_require_record_field(widget, "value", VALUE_STRING, "gui label") != NULL;
    }
    if (strcmp(component, "input") == 0) {
        return gui_require_record_field(widget, "value", VALUE_STRING, "gui input") != NULL;
    }
    if (strcmp(component, "button") == 0) {
        if (!gui_require_record_field(widget, "label", VALUE_STRING, "gui button")) {
            return 0;
        }
        RecordField *value = record_find(widget, "value");
        if (!value) {
            record_set(widget, "value", value_bool(0));
            return 1;
        }
        if (value->value->kind != VALUE_BOOL) {
            runtime_error_raise("gui button field 'value' must be boolean", 1003, "gui");
            return 0;
        }
        return 1;
    }

    return 1;
}

static Value eval_expr(AstExpr *expr);
static Value *resolve_lvalue_ref(AstExpr *target);
static char *lvalue_watch_path(AstExpr *target);
static int webserver_validate_response_append(AstExpr *target, Value item);
static char *lvalue_watch_path(AstExpr *target);

#if HAVE_GTK
static GuiNativeWindow *gui_window_registry_find(const char *handle_id);
static void gui_window_flush_mutations(GuiNativeWindow *window);
static void gui_window_refresh_widgets(GuiNativeWindow *window);
#endif
static char *gui_create_native_window(Value *ui,
                                      int width,
                                      int height,
                                      const char *title,
                                      const char *watch_root_path);
static int watcher_trigger_change(const char *path);

static int gui_build_widget_lookup(Value *lookup, Value *widget, const size_t *path, size_t depth) {
    RecordField *id_field = record_find(widget, "id");
    if (!id_field || id_field->value->kind != VALUE_STRING) {
        runtime_error_raise("gui widget missing required field: id", 1003, "gui");
        return 0;
    }

    const char *id = id_field->value->as.string;
    if (gui_window_field_reserved(id)) {
        char message[256];
        snprintf(message, sizeof(message), "gui widget id collides with window field: %s", id);
        runtime_error_raise(message, 1003, "gui");
        return 0;
    }
    if (!gui_id_is_exposable_identifier(id)) {
        char message[256];
        snprintf(message, sizeof(message), "gui widget id is not exposable as win.%s", id);
        runtime_error_raise(message, 1003, "gui");
        return 0;
    }

    Value *items = NULL;
    if (depth > 0) {
        items = malloc(sizeof(Value) * depth);
        if (!items) {
            abort();
        }
        for (size_t i = 0; i < depth; i++) {
            items[i] = value_number((double)path[i]);
        }
    }
    record_set(lookup, id, value_array(items, depth));

    RecordField *contains = record_find(widget, "contains");
    if (!contains || contains->value->kind != VALUE_ARRAY) {
        return 1;
    }
    for (size_t i = 0; i < contains->value->as.array.count; i++) {
        size_t child_path[256];
        if (depth >= sizeof(child_path) / sizeof(child_path[0])) {
            runtime_error_raise("gui widget tree nesting too deep", 1003, "gui");
            return 0;
        }
        for (size_t j = 0; j < depth; j++) {
            child_path[j] = path[j];
        }
        child_path[depth] = i;
        if (!gui_build_widget_lookup(lookup,
                                     &contains->value->as.array.items[i],
                                     child_path,
                                     depth + 1)) {
            return 0;
        }
    }
    return 1;
}

static int gui_window_fields(Value *window,
                             RecordField **window_meta,
                             RecordField **root_field,
                             RecordField **ids_field) {
    if (!window || window->kind != VALUE_RECORD) {
        return 0;
    }

    RecordField *meta = record_find(window, "_window");
    RecordField *root = record_find(window, "_root");
    RecordField *ids = record_find(window, "_ids");
    if (!meta || !root || !ids) {
        return 0;
    }
    if (meta->value->kind != VALUE_RECORD ||
        root->value->kind != VALUE_RECORD ||
        ids->value->kind != VALUE_RECORD) {
        return 0;
    }

    if (window_meta) {
        *window_meta = meta;
    }
    if (root_field) {
        *root_field = root;
    }
    if (ids_field) {
        *ids_field = ids;
    }
    return 1;
}

static Value *gui_window_lookup_widget_ref(Value *window, const char *id) {
    RecordField *root_field = NULL;
    RecordField *ids_field = NULL;
    if (!gui_window_fields(window, NULL, &root_field, &ids_field)) {
        return NULL;
    }

    RecordField *path_field = record_find(ids_field->value, id);
    if (!path_field) {
        return NULL;
    }
    if (path_field->value->kind != VALUE_ARRAY) {
        runtime_error_raise("gui window id lookup is malformed", 1003, "gui");
        return NULL;
    }

    Value *current = root_field->value;
    for (size_t i = 0; i < path_field->value->as.array.count; i++) {
        Value step = path_field->value->as.array.items[i];
        if (!value_is_integer_number(step)) {
            runtime_error_raise("gui window id lookup path is malformed", 1003, "gui");
            return NULL;
        }
        if (current->kind != VALUE_RECORD) {
            runtime_error_raise("gui window root lookup is malformed", 1003, "gui");
            return NULL;
        }
        RecordField *contains = record_find(current, "contains");
        if (!contains || contains->value->kind != VALUE_ARRAY) {
            runtime_error_raise("gui window root lookup is malformed", 1003, "gui");
            return NULL;
        }
        int index = (int)step.as.number;
        if (index < 0 || (size_t)index >= contains->value->as.array.count) {
            runtime_error_raise("gui window id lookup path is out of range", 1003, "gui");
            return NULL;
        }
        current = &contains->value->as.array.items[index];
    }
    return current;
}

static void gui_free_window_args(Value width_value,
                                 Value height_value,
                                 Value title_value,
                                 Value ui_value) {
    value_free(width_value);
    value_free(height_value);
    value_free(title_value);
    value_free(ui_value);
}

static Value gui_eval_window_call(AstExpr *expr) {
    if (expr->as.call.args.count != 4) {
        runtime_error_raise("gui.window expects four arguments", 1003, "gui");
        return value_null();
    }

    Value width_value = eval_expr(expr->as.call.args.items[0]);
    Value height_value = eval_expr(expr->as.call.args.items[1]);
    Value title_value = eval_expr(expr->as.call.args.items[2]);
    Value ui_value = eval_expr(expr->as.call.args.items[3]);
    if (error_action_pending()) {
        gui_free_window_args(width_value, height_value, title_value, ui_value);
        return value_null();
    }

    if (!value_is_integer_number(width_value) || !value_is_integer_number(height_value)) {
        gui_free_window_args(width_value, height_value, title_value, ui_value);
        runtime_error_raise("gui.window width and height must be integer numbers", 1003, "gui");
        return value_null();
    }
    if (title_value.kind != VALUE_STRING) {
        gui_free_window_args(width_value, height_value, title_value, ui_value);
        runtime_error_raise("gui.window title must be a string", 1003, "gui");
        return value_null();
    }
    if (ui_value.kind != VALUE_RECORD) {
        gui_free_window_args(width_value, height_value, title_value, ui_value);
        runtime_error_raise("gui.window ui must be a record", 1003, "gui");
        return value_null();
    }

    GuiIdSet ids = {0};
    int ok = gui_validate_widget_tree(&ui_value, &ids);
    gui_id_set_clear(&ids);
    if (!ok || error_action_pending()) {
        gui_free_window_args(width_value, height_value, title_value, ui_value);
        return value_null();
    }

    Value window_meta = value_record(NULL, 0);
    Value ids_value = value_record(NULL, 0);
    size_t root_path[1] = {0};
    if (!gui_build_widget_lookup(&ids_value, &ui_value, root_path, 0) || error_action_pending()) {
        value_free(ids_value);
        gui_free_window_args(width_value, height_value, title_value, ui_value);
        return value_null();
    }

    record_set(&window_meta, "width", value_number(width_value.as.number));
    record_set(&window_meta, "height", value_number(height_value.as.number));
    record_set(&window_meta, "title", value_string(title_value.as.string));

    Value window_value = value_record(NULL, 0);
    record_set(&window_value, "_window", window_meta);
    record_set(&window_value, "_root", ui_value);
    record_set(&window_value, "_ids", ids_value);

    value_free(width_value);
    value_free(height_value);
    value_free(title_value);
    return window_value;
}

static Value gui_eval_run_call(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        runtime_error_raise("gui.run expects one argument", 1003, "gui");
        return value_null();
    }

    AstExpr *window_expr = expr->as.call.args.items[0];
    Value *live_window = NULL;
    Value window_value = value_null();
    int owns_window_value = 0;

    if (window_expr->kind == AST_EXPR_IDENT ||
        window_expr->kind == AST_EXPR_FIELD ||
        window_expr->kind == AST_EXPR_INDEX) {
        live_window = resolve_lvalue_ref(window_expr);
        if (error_action_pending()) {
            return value_null();
        }
    }
    if (!live_window) {
        window_value = eval_expr(window_expr);
        if (error_action_pending()) {
            value_free(window_value);
            return value_null();
        }
        live_window = &window_value;
        owns_window_value = 1;
    }

    if (live_window->kind != VALUE_RECORD) {
        if (owns_window_value) {
            value_free(window_value);
        }
        runtime_error_raise("gui.run expects a window record", 1003, "gui");
        return value_null();
    }

    RecordField *window_meta = NULL;
    if (!gui_window_fields(live_window, &window_meta, NULL, NULL)) {
        if (owns_window_value) {
            value_free(window_value);
        }
        runtime_error_raise("gui.run expects a window created by gui.window", 1003, "gui");
        return value_null();
    }
    RecordField *width_field = record_find(window_meta->value, "width");
    RecordField *height_field = record_find(window_meta->value, "height");
    RecordField *title_field = record_find(window_meta->value, "title");
    RecordField *root_field = NULL;
    if (!width_field || !height_field || !title_field ||
        width_field->value->kind != VALUE_NUMBER ||
        height_field->value->kind != VALUE_NUMBER ||
        title_field->value->kind != VALUE_STRING ||
        !gui_window_fields(live_window, &window_meta, &root_field, NULL)) {
        if (owns_window_value) {
            value_free(window_value);
        }
        runtime_error_raise("gui.run expects a window created by gui.window", 1003, "gui");
        return value_null();
    }

#if HAVE_GTK
    char *watch_root_path = NULL;
    if (window_expr->kind == AST_EXPR_IDENT ||
        window_expr->kind == AST_EXPR_FIELD ||
        window_expr->kind == AST_EXPR_INDEX) {
        watch_root_path = lvalue_watch_path(window_expr);
    }
    char *handle_id = gui_create_native_window(root_field->value,
                                               (int)width_field->value->as.number,
                                               (int)height_field->value->as.number,
                                               title_field->value->as.string,
                                               watch_root_path);
    free(watch_root_path);
    if (error_action_pending() || !handle_id) {
        if (owns_window_value) {
            value_free(window_value);
        }
        free(handle_id);
        return value_null();
    }
    GuiNativeWindow *window = gui_window_registry_find(handle_id);
    free(handle_id);
    if (!window || !window->window) {
        if (owns_window_value) {
            value_free(window_value);
        }
        runtime_error_raise("gui window handle is invalid", 1003, "gui");
        return value_null();
    }
    gtk_widget_show_all(window->window);
    gui_window_refresh_widgets(window);
    while (!window->closed) {
        gtk_main_iteration_do(TRUE);
        gui_window_flush_mutations(window);
    }
    gui_window_flush_mutations(window);
#else
    gui_ensure_gtk_available();
    if (error_action_pending()) {
        if (owns_window_value) {
            value_free(window_value);
        }
        return value_null();
    }
#endif

    if (owns_window_value) {
        value_free(window_value);
    }
    return value_null();
}

#if HAVE_GTK
static GtkWidget *gui_build_gtk_widget(Value *node, GuiNativeWindow *window);

static GtkWidget *gui_build_flexible_spacer(GtkOrientation orientation) {
    GtkWidget *spacer = gtk_box_new(orientation, 0);
    gtk_widget_show(spacer);
    return spacer;
}

static void gui_box_pack_flexible_spacer(GtkWidget *box, GtkOrientation orientation) {
    GtkWidget *spacer = gui_build_flexible_spacer(orientation);
    gtk_box_pack_start(GTK_BOX(box), spacer, TRUE, TRUE, 0);
}

static void gui_box_pack_child(GtkWidget *box, GtkWidget *child) {
    gtk_box_pack_start(GTK_BOX(box), child, FALSE, FALSE, 0);
}

static void gui_box_pack_children(Value *contains_value,
                                  GtkWidget *box,
                                  GtkOrientation orientation,
                                  GuiNativeWindow *window,
                                  GuiSpacingMode spacing_mode);

static int gui_ensure_gtk_available(void) {
    if (gui_gtk_init_attempted) {
        return gui_gtk_available;
    }
    gui_gtk_init_attempted = 1;
    gui_gtk_available = gtk_init_check(NULL, NULL);
    if (!gui_gtk_available) {
        runtime_error_raise("GTK could not be initialized", 1003, "gui");
    }
    return gui_gtk_available;
}

static void gui_window_binding_add(GuiNativeWindow *window,
                                   const char *id,
                                   GtkWidget *widget,
                                   Value *widget_record) {
    GuiWidgetBinding **next = realloc(window->bindings,
                                      sizeof(GuiWidgetBinding *) * (window->binding_count + 1));
    if (!next) {
        abort();
    }
    window->bindings = next;
    GuiWidgetBinding *binding = malloc(sizeof(GuiWidgetBinding));
    if (!binding) {
        abort();
    }
    binding->id = copy_string(id);
    binding->widget = widget;
    binding->widget_record = widget_record;
    binding->window = window;
    window->bindings[window->binding_count] = binding;
    window->binding_count++;
}

static void gui_pending_mutation_clear(GuiPendingMutation *mutation) {
    if (!mutation) {
        return;
    }
    free(mutation->window_handle_id);
    free(mutation->widget_id);
    free(mutation->field_name);
    value_free(mutation->new_value);
    memset(mutation, 0, sizeof(*mutation));
}

static void gui_window_clear_pending_mutations(GuiNativeWindow *window) {
    if (!window) {
        return;
    }
    for (size_t i = 0; i < window->pending_mutation_count; i++) {
        gui_pending_mutation_clear(&window->pending_mutations[i]);
    }
    free(window->pending_mutations);
    window->pending_mutations = NULL;
    window->pending_mutation_count = 0;
}

static void gui_window_enqueue_mutation(GuiNativeWindow *window,
                                        const char *widget_id,
                                        const char *field_name,
                                        Value new_value) {
    if (!window || !widget_id || !field_name) {
        value_free(new_value);
        return;
    }
    if (window->sync_depth > 0) {
        value_free(new_value);
        return;
    }

    for (size_t i = 0; i < window->pending_mutation_count; i++) {
        GuiPendingMutation *mutation = &window->pending_mutations[i];
        if (strcmp(mutation->widget_id, widget_id) == 0 &&
            strcmp(mutation->field_name, field_name) == 0) {
            value_free(mutation->new_value);
            mutation->new_value = new_value;
            return;
        }
    }

    GuiPendingMutation *next = realloc(window->pending_mutations,
                                       sizeof(GuiPendingMutation) * (window->pending_mutation_count + 1));
    if (!next) {
        abort();
    }
    window->pending_mutations = next;
    GuiPendingMutation *mutation = &window->pending_mutations[window->pending_mutation_count];
    memset(mutation, 0, sizeof(*mutation));
    mutation->window_handle_id = copy_string(window->handle_id);
    mutation->widget_id = copy_string(widget_id);
    mutation->field_name = copy_string(field_name);
    mutation->new_value = new_value;
    window->pending_mutation_count++;
}

static void gui_window_flush_mutations(GuiNativeWindow *window) {
    if (!window || window->pending_mutation_count == 0) {
        return;
    }
    if (window->watch_root_path) {
        for (size_t i = 0; i < window->pending_mutation_count; i++) {
            GuiPendingMutation *mutation = &window->pending_mutations[i];
            size_t length = strlen(window->watch_root_path) +
                            1 + strlen(mutation->widget_id) +
                            1 + strlen(mutation->field_name);
            char *watch_path = malloc(length + 1);
            if (!watch_path) {
                abort();
            }
            snprintf(watch_path,
                     length + 1,
                     "%s.%s.%s",
                     window->watch_root_path,
                     mutation->widget_id,
                     mutation->field_name);
            int watcher_ok = watcher_trigger_change(watch_path);
            free(watch_path);
            if (!watcher_ok || error_action_pending()) {
                break;
            }
        }
    }
    if (!error_action_pending()) {
        gui_window_refresh_widgets(window);
    }
    gui_window_clear_pending_mutations(window);
}

static int gui_widget_set_string_field(Value *widget_record, const char *field_name, const char *text) {
    if (!widget_record || widget_record->kind != VALUE_RECORD) {
        return 0;
    }
    RecordField *field = record_find(widget_record, field_name);
    if (!field || field->value->kind != VALUE_STRING) {
        return 0;
    }
    const char *next_text = text ? text : "";
    if (strcmp(field->value->as.string, next_text) == 0) {
        return 0;
    }
    value_free(*field->value);
    *field->value = value_string(next_text);
    return 1;
}

static int gui_widget_set_bool_field(Value *widget_record, const char *field_name, int boolean) {
    if (!widget_record || widget_record->kind != VALUE_RECORD) {
        return 0;
    }
    RecordField *field = record_find(widget_record, field_name);
    if (!field || field->value->kind != VALUE_BOOL) {
        return 0;
    }
    int next_boolean = boolean != 0;
    if (field->value->as.boolean == next_boolean) {
        return 0;
    }
    value_free(*field->value);
    *field->value = value_bool(next_boolean);
    return 1;
}

static void gui_on_entry_commit(GtkWidget *widget, gpointer data) {
    GuiWidgetBinding *binding = data;
    if (!binding) {
        return;
    }
    const char *text = gtk_entry_get_text(GTK_ENTRY(widget));
    if (gui_widget_set_string_field(binding->widget_record, "value", text)) {
        gui_window_enqueue_mutation(binding->window,
                                    binding->id,
                                    "value",
                                    value_string(text));
    }
}

static gboolean gui_on_entry_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    (void)event;
    gui_on_entry_commit(widget, data);
    return FALSE;
}

static void gui_on_button_clicked(GtkWidget *widget, gpointer data) {
    GuiWidgetBinding *binding = data;
    if (!binding) {
        return;
    }
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), TRUE);
    }
    if (gui_widget_set_bool_field(binding->widget_record, "value", 1)) {
        gui_window_enqueue_mutation(binding->window,
                                    binding->id,
                                    "value",
                                    value_bool(1));
    }
}

static void gui_apply_widget_state(GtkWidget *widget, Value *node) {
    int width = gui_optional_int_field(node, "width", -1);
    int height = gui_optional_int_field(node, "height", -1);
    if (error_action_pending()) {
        return;
    }
    if (width >= 0 || height >= 0) {
        gtk_widget_set_size_request(widget, width, height);
    }
    int enabled = gui_optional_bool_field(node, "enabled", 1);
    if ((gtk_widget_get_sensitive(widget) != 0) != (enabled != 0)) {
        gtk_widget_set_sensitive(widget, enabled);
    }
    if (error_action_pending()) {
        return;
    }
    int visible = gui_optional_bool_field(node, "visible", 1);
    if (visible) {
        gtk_widget_show(widget);
    } else {
        gtk_widget_hide(widget);
    }
}

static void gui_refresh_widget_binding(GuiWidgetBinding *binding) {
    if (!binding || !binding->widget_record || binding->widget_record->kind != VALUE_RECORD) {
        return;
    }

    RecordField *component_field = record_find(binding->widget_record, "component");
    if (!component_field || component_field->value->kind != VALUE_STRING) {
        return;
    }

    const char *component = component_field->value->as.string;
    GtkWidget *widget = binding->widget;
    Value *record = binding->widget_record;

    if (strcmp(component, "label") == 0) {
        RecordField *value_field = record_find(record, "value");
        if (value_field && value_field->value->kind == VALUE_STRING) {
            const char *current = gtk_label_get_text(GTK_LABEL(widget));
            if (strcmp(current, value_field->value->as.string) != 0) {
                gtk_label_set_text(GTK_LABEL(widget), value_field->value->as.string);
            }
        }
    } else if (strcmp(component, "input") == 0) {
        RecordField *value_field = record_find(record, "value");
        if (value_field && value_field->value->kind == VALUE_STRING) {
            const char *current = gtk_entry_get_text(GTK_ENTRY(widget));
            if (strcmp(current, value_field->value->as.string) != 0) {
                gtk_entry_set_text(GTK_ENTRY(widget), value_field->value->as.string);
            }
        }
    } else if (strcmp(component, "button") == 0) {
        RecordField *label_field = record_find(record, "label");
        RecordField *value_field = record_find(record, "value");
        if (label_field && label_field->value->kind == VALUE_STRING) {
            const char *current = gtk_button_get_label(GTK_BUTTON(widget));
            if (!current || strcmp(current, label_field->value->as.string) != 0) {
                gtk_button_set_label(GTK_BUTTON(widget), label_field->value->as.string);
            }
        }
        if (value_field && value_field->value->kind == VALUE_BOOL) {
            int current = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) != 0;
            int next = value_field->value->as.boolean != 0;
            if (current != next) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), next);
            }
        }
    }

    gui_apply_widget_state(widget, record);
}

static void gui_window_refresh_widgets(GuiNativeWindow *window) {
    if (!window) {
        return;
    }
    window->sync_depth++;
    for (size_t i = 0; i < window->binding_count; i++) {
        gui_refresh_widget_binding(window->bindings[i]);
        if (error_action_pending()) {
            break;
        }
    }
    window->sync_depth--;
}

static void gui_box_pack_children(Value *contains_value,
                                  GtkWidget *box,
                                  GtkOrientation orientation,
                                  GuiNativeWindow *window,
                                  GuiSpacingMode spacing_mode) {
    size_t count = contains_value ? contains_value->as.array.count : 0;

    if (spacing_mode == GUI_SPACING_END ||
        spacing_mode == GUI_SPACING_CENTER ||
        spacing_mode == GUI_SPACING_AROUND) {
        gui_box_pack_flexible_spacer(box, orientation);
    }

    for (size_t i = 0; i < count; i++) {
        if ((spacing_mode == GUI_SPACING_BETWEEN ||
             spacing_mode == GUI_SPACING_AROUND) && i > 0) {
            gui_box_pack_flexible_spacer(box, orientation);
        }

        GtkWidget *child = gui_build_gtk_widget(&contains_value->as.array.items[i], window);
        if (error_action_pending()) {
            return;
        }
        gui_box_pack_child(box, child);
    }

    if (spacing_mode == GUI_SPACING_CENTER ||
        spacing_mode == GUI_SPACING_AROUND) {
        gui_box_pack_flexible_spacer(box, orientation);
    }
}

static GtkWidget *gui_build_gtk_widget(Value *node, GuiNativeWindow *window) {
    RecordField *component_field = record_find(node, "component");
    RecordField *id_field = record_find(node, "id");
    const char *component = component_field->value->as.string;
    GtkWidget *widget = NULL;
    GuiWidgetBinding *binding = NULL;

    if (strcmp(component, "vert") == 0 || strcmp(component, "horiz") == 0) {
        GtkOrientation orientation = strcmp(component, "vert") == 0
            ? GTK_ORIENTATION_VERTICAL
            : GTK_ORIENTATION_HORIZONTAL;
        GuiSpacingMode spacing_mode = GUI_SPACING_START;
        if (!gui_spacing_mode_for_record(node, &spacing_mode) || error_action_pending()) {
            return NULL;
        }
        widget = gtk_box_new(orientation, 0);
        RecordField *contains = record_find(node, "contains");
        if (contains && contains->value->kind == VALUE_ARRAY) {
            gui_box_pack_children(contains->value, widget, orientation, window, spacing_mode);
            if (error_action_pending()) {
                return NULL;
            }
        }
    } else if (strcmp(component, "label") == 0) {
        widget = gtk_label_new(record_find(node, "value")->value->as.string);
    } else if (strcmp(component, "input") == 0) {
        widget = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(widget), record_find(node, "value")->value->as.string);
    } else if (strcmp(component, "button") == 0) {
        widget = gtk_toggle_button_new_with_label(record_find(node, "label")->value->as.string);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),
                                     record_find(node, "value")->value->as.boolean);
    } else if (strcmp(component, "spacer") == 0) {
        widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    }

    gui_apply_widget_state(widget, node);
    if (error_action_pending()) {
        return NULL;
    }
    gui_window_binding_add(window, id_field->value->as.string, widget, node);
    binding = window->bindings[window->binding_count - 1];
    if (strcmp(component, "input") == 0) {
        g_signal_connect(widget, "activate", G_CALLBACK(gui_on_entry_commit), binding);
        g_signal_connect(widget, "focus-out-event", G_CALLBACK(gui_on_entry_focus_out), binding);
    } else if (strcmp(component, "button") == 0) {
        g_signal_connect(widget, "clicked", G_CALLBACK(gui_on_button_clicked), binding);
    }
    return widget;
}

static GuiNativeWindow *gui_window_registry_find(const char *handle_id) {
    for (size_t i = 0; i < gui_window_count; i++) {
        if (strcmp(gui_windows[i].handle_id, handle_id) == 0) {
            return &gui_windows[i];
        }
    }
    return NULL;
}

static void gui_on_window_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    GuiNativeWindow *window = data;
    if (window) {
        window->closed = 1;
        window->window = NULL;
    }
}

static char *gui_create_native_window(Value *ui,
                                      int width,
                                      int height,
                                      const char *title,
                                      const char *watch_root_path) {
    if (!gui_ensure_gtk_available()) {
        return NULL;
    }

    GuiNativeWindow *next = realloc(gui_windows, sizeof(GuiNativeWindow) * (gui_window_count + 1));
    if (!next) {
        abort();
    }
    gui_windows = next;
    GuiNativeWindow *window = &gui_windows[gui_window_count];
    memset(window, 0, sizeof(*window));
    gui_window_count++;

    char handle_id[64];
    snprintf(handle_id, sizeof(handle_id), "gui-window-%d", gui_next_window_id++);
    window->handle_id = copy_string(handle_id);
    window->watch_root_path = watch_root_path ? copy_string(watch_root_path) : NULL;
    window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window->window), title);
    gtk_window_set_default_size(GTK_WINDOW(window->window), width, height);

    GtkWidget *root_widget = gui_build_gtk_widget(ui, window);
    if (error_action_pending()) {
        return NULL;
    }
    gtk_container_add(GTK_CONTAINER(window->window), root_widget);
    g_signal_connect(window->window, "destroy", G_CALLBACK(gui_on_window_destroy), window);
    return copy_string(window->handle_id);
}

static void gui_clear_native_windows(void) {
    for (size_t i = 0; i < gui_window_count; i++) {
        for (size_t j = 0; j < gui_windows[i].binding_count; j++) {
            free(gui_windows[i].bindings[j]->id);
            free(gui_windows[i].bindings[j]);
        }
        gui_window_clear_pending_mutations(&gui_windows[i]);
        free(gui_windows[i].bindings);
        free(gui_windows[i].handle_id);
        free(gui_windows[i].watch_root_path);
        if (gui_windows[i].window) {
            gtk_widget_destroy(gui_windows[i].window);
        }
    }
    free(gui_windows);
    gui_windows = NULL;
    gui_window_count = 0;
    gui_next_window_id = 1;
    gui_gtk_init_attempted = 0;
    gui_gtk_available = 0;
}
#else
static int gui_ensure_gtk_available(void) {
    runtime_error_raise("gui support is unavailable; install GTK 3 development packages and rebuild",
                        1003,
                        "gui");
    return 0;
}

static char *gui_create_native_window(Value *ui,
                                      int width,
                                      int height,
                                      const char *title,
                                      const char *watch_root_path) {
    (void)ui;
    (void)width;
    (void)height;
    (void)title;
    (void)watch_root_path;
    gui_ensure_gtk_available();
    return NULL;
}

static void gui_clear_native_windows(void) {
}
#endif

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

static int datetime_lens_precision(const char *name, DateTimePrecision *out) {
    if (modifier_is(name, "year")) {
        *out = PREC_YEAR;
        return 1;
    }
    if (modifier_is(name, "month")) {
        *out = PREC_MONTH;
        return 1;
    }
    if (modifier_is(name, "day")) {
        *out = PREC_DAY;
        return 1;
    }
    if (modifier_is(name, "hour")) {
        *out = PREC_HOUR;
        return 1;
    }
    if (modifier_is(name, "minute")) {
        *out = PREC_MINUTE;
        return 1;
    }
    if (modifier_is(name, "second")) {
        *out = PREC_SECOND;
        return 1;
    }
    return 0;
}

static DateTime datetime_apply_lens(DateTime dt, DateTimePrecision lens) {
    dt.precision = lens;
    if (lens < PREC_SECOND) {
        dt.second = 0;
    }
    if (lens < PREC_MINUTE) {
        dt.minute = 0;
    }
    if (lens < PREC_HOUR) {
        dt.hour = 0;
    }
    if (!dt.time_only) {
        if (lens < PREC_DAY) {
            dt.day = 1;
        }
        if (lens < PREC_MONTH) {
            dt.month = 1;
        }
    }
    return dt;
}

static int parse_datetime_like_value(const char *text, DateTime *out) {
    return parse_date_value(text, out) || parse_time_value(text, out);
}

static Value apply_datetime_lens_to_value(Value value,
                                          DateTimePrecision lens,
                                          const char *modifier_name,
                                          int *ok) {
    DateTime dt;
    *ok = 1;
    if (value.kind == VALUE_DATETIME) {
        dt = value.as.datetime;
    } else if (value.kind == VALUE_STRING && parse_datetime_like_value(value.as.string, &dt)) {
        /* parsed below */
    } else {
        char message[256];
        snprintf(message, sizeof(message), "%s lens expects a date/time value", modifier_name);
        runtime_error_raise(message, 1003, "datetime");
        value_free(value);
        *ok = 0;
        return value_null();
    }

    value_free(value);
    if (dt.time_only && lens < PREC_HOUR) {
        char message[256];
        snprintf(message, sizeof(message), "%s lens cannot be applied to a time-only value", modifier_name);
        runtime_error_raise(message, 1003, "datetime");
        *ok = 0;
        return value_null();
    }
    if (dt.precision < lens) {
        char message[256];
        snprintf(message, sizeof(message), "%s lens requires sufficient date/time precision", modifier_name);
        runtime_error_raise(message, 1003, "datetime");
        *ok = 0;
        return value_null();
    }
    return value_datetime(datetime_apply_lens(dt, lens));
}

static int compare_ints(int left, int right) {
    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

static int datetime_compare_exact(DateTime left, DateTime right) {
    int cmp = compare_ints(left.time_only ? 1 : 0, right.time_only ? 1 : 0);
    if (cmp != 0) {
        return cmp;
    }

    if (!left.time_only) {
        cmp = compare_ints(left.year, right.year);
        if (cmp != 0) return cmp;
        cmp = compare_ints(left.month, right.month);
        if (cmp != 0) return cmp;
        cmp = compare_ints(left.day, right.day);
        if (cmp != 0) return cmp;
    }

    cmp = compare_ints(left.hour, right.hour);
    if (cmp != 0) return cmp;
    cmp = compare_ints(left.minute, right.minute);
    if (cmp != 0) return cmp;
    cmp = compare_ints(left.second, right.second);
    if (cmp != 0) return cmp;

    return compare_ints((int)left.precision, (int)right.precision);
}

static int comparison_result_from_cmp(const char *op, int cmp) {
    if (strcmp(op, "=") == 0) return cmp == 0;
    if (strcmp(op, "!=") == 0) return cmp != 0;
    if (strcmp(op, ">") == 0) return cmp > 0;
    if (strcmp(op, "<") == 0) return cmp < 0;
    if (strcmp(op, ">=") == 0) return cmp >= 0;
    if (strcmp(op, "<=") == 0) return cmp <= 0;
    if (strcmp(op, "!>") == 0) return !(cmp > 0);
    if (strcmp(op, "!<") == 0) return !(cmp < 0);
    if (strcmp(op, "!>=") == 0) return !(cmp >= 0);
    if (strcmp(op, "!<=") == 0) return !(cmp <= 0);
    return 0;
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

static int path_is_dot_prefix(const char *prefix, const char *path) {
    size_t prefix_length = strlen(prefix);
    if (strncmp(prefix, path, prefix_length) != 0) {
        return 0;
    }
    return path[prefix_length] == '\0' || path[prefix_length] == '.';
}

static int watcher_name_matches_change(const char *watch_name, const char *changed_path) {
    return path_is_dot_prefix(watch_name, changed_path) ||
        path_is_dot_prefix(changed_path, watch_name);
}

static int watcher_matches_change(AstStmt *watcher, const char *changed_path) {
    for (size_t i = 0; i < watcher->as.watch.names.count; i++) {
        if (watcher_name_matches_change(watcher->as.watch.names.items[i], changed_path)) {
            return 1;
        }
    }
    return 0;
}

static void watcher_enqueue(size_t index) {
    if (watchers[index].pending) {
        return;
    }
    size_t *next = realloc(watcher_queue, sizeof(size_t) * (watcher_queue_count + 1));
    if (!next) {
        abort();
    }
    watcher_queue = next;
    watchers[index].pending = 1;
    watcher_queue[watcher_queue_count++] = index;
}

static void watcher_clear_pending(void) {
    for (size_t i = 0; i < watcher_count; i++) {
        watchers[i].pending = 0;
    }
}

static int watcher_drain(void) {
    if (watcher_draining) {
        return 1;
    }

    watcher_draining = 1;
    if (watcher_drain_origin_line == 0) {
        watcher_drain_origin_line = current_line;
        watcher_drain_origin_column = current_column;
    }
    size_t cursor = 0;
    size_t steps = 0;
    int ok = 1;
    while (cursor < watcher_queue_count) {
        if (++steps > WATCHER_EXECUTION_LIMIT) {
            int previous_line = current_line;
            int previous_column = current_column;
            current_line = watcher_drain_origin_line;
            current_column = watcher_drain_origin_column;
            runtime_error_raise("watcher cycle exceeded 10000 executions in one drain cycle",
                                WATCHER_CYCLE_ERROR_CODE,
                                "watcher");
            current_line = previous_line;
            current_column = previous_column;
            ok = 0;
            break;
        }
        size_t index = watcher_queue[cursor++];
        if (index < watcher_count) {
            watchers[index].pending = 0;
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
            if (result.did_break || result.did_continue) {
                value_free(result.value);
            }
        }
    }

    free(watcher_queue);
    watcher_queue = NULL;
    watcher_queue_count = 0;
    watcher_clear_pending();
    watcher_draining = 0;
    watcher_drain_origin_line = 0;
    watcher_drain_origin_column = 0;
    return ok;
}

static int watcher_trigger_change(const char *path) {
    if (watcher_suppressed || current_env != &global_env) {
        return 1;
    }
    int start_drain = !watcher_draining && watcher_queue_count == 0;
    if (start_drain) {
        watcher_drain_origin_line = current_line;
        watcher_drain_origin_column = current_column;
    }
    for (size_t i = 0; i < watcher_count; i++) {
        if (watcher_matches_change(watchers[i].stmt, path)) {
            watcher_enqueue(i);
        }
    }
    return watcher_drain();
}

static int watcher_register(AstStmt *stmt) {
    if (current_env != &global_env) {
        fprintf(stderr, "watch may only be registered at top level for now\n");
        return 1;
    }

    WatcherDef *next = realloc(watchers, sizeof(WatcherDef) * (watcher_count + 1));
    if (!next) {
        abort();
    }
    watchers = next;
    watchers[watcher_count].stmt = stmt;
    watchers[watcher_count].pending = 0;
    watcher_enqueue(watcher_count);
    watcher_count++;
    return watcher_drain();
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
    watcher_drain_origin_line = 0;
    watcher_drain_origin_column = 0;
}

static int notify_lvalue_mutation(AstExpr *target) {
    char *watch_path = lvalue_watch_path(target);
    if (!watch_path) {
        return 1;
    }
    int ok = watcher_trigger_change(watch_path);
    free(watch_path);
    return ok;
}

static int expr_is_lvalue_path(AstExpr *expr) {
    return expr->kind == AST_EXPR_IDENT ||
        expr->kind == AST_EXPR_FIELD ||
        expr->kind == AST_EXPR_INDEX;
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
    if (gbasic_builtin_function(stmt->as.function.name)) {
        if (imported) {
            fprintf(stderr,
                    "warning: function '%s' from library '%s' has same name as a built-in; unqualified calls use the built-in\n",
                    stmt->as.function.name,
                    library ? library : "");
        } else {
            fprintf(stderr,
                    "warning: local function '%s' overrides built-in function\n",
                    stmt->as.function.name);
        }
    }

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
        } else if (!imported && existing->imported && existing->library) {
            fprintf(stderr,
                    "warning: local modifier '%s' overrides modifier from library '%s'\n",
                    stmt->as.modifier.name,
                    existing->library);
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

static char *join_path(const char *dir, const char *name) {
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    int needs_slash = dir_len > 0 && dir[dir_len - 1] != '/';
    char *path = malloc(dir_len + (size_t)needs_slash + name_len + 1);
    if (!path) {
        abort();
    }
    memcpy(path, dir, dir_len);
    if (needs_slash) {
        path[dir_len] = '/';
    }
    memcpy(path + dir_len + (size_t)needs_slash, name, name_len + 1);
    return path;
}

static int has_bas_extension(const char *path) {
    size_t len = strlen(path);
    return len >= 4 && strcmp(path + len - 4, ".bas") == 0;
}

static char *library_filename(const char *name) {
    size_t name_len = strlen(name);
    char *filename = malloc(name_len + strlen(".bas") + 1);
    if (!filename) {
        abort();
    }
    memcpy(filename, name, name_len);
    memcpy(filename + name_len, ".bas", strlen(".bas") + 1);
    return filename;
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
    parse_set_source_path(path);
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

static void library_match_add(LibraryMatch **matches, size_t *count, const char *path, AstStmt *library) {
    LibraryMatch *next = realloc(*matches, sizeof(LibraryMatch) * (*count + 1));
    if (!next) {
        abort();
    }
    *matches = next;
    (*matches)[*count].path = copy_string(path);
    (*matches)[*count].library = library;
    (*count)++;
}

static void library_matches_clear(LibraryMatch *matches, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(matches[i].path);
    }
    free(matches);
}

static int library_match_seen(LibraryMatch *matches, size_t count, const char *path) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(matches[i].path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

static void search_file_for_library(const char *path,
                                    const char *name,
                                    LibraryMatch **matches,
                                    size_t *match_count) {
    if (!has_bas_extension(path) ||
        path_equal(path, root_source_path) ||
        path_equal(path, current_import_path) ||
        library_match_seen(*matches, *match_count, path)) {
        return;
    }

    LoadedFile *loaded = loaded_file_get(path);
    if (!loaded) {
        return;
    }

    AstStmt *library = library_find_in(loaded->program, name);
    if (library) {
        library_match_add(matches, match_count, path, library);
    }
}

static void search_directory_for_library(const char *dir_path,
                                         const char *name,
                                         int recursive,
                                         int exact_filename,
                                         LibraryMatch **matches,
                                         size_t *match_count) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }

    char *expected_filename = exact_filename ? library_filename(name) : NULL;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char *path = join_path(dir_path, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISREG(st.st_mode)) {
                if (!exact_filename || strcmp(entry->d_name, expected_filename) == 0) {
                    search_file_for_library(path, name, matches, match_count);
                }
                if (error_action_pending()) {
                    free(path);
                    break;
                }
            } else if (recursive && S_ISDIR(st.st_mode)) {
                search_directory_for_library(path, name, 1, exact_filename, matches, match_count);
                if (error_action_pending()) {
                    free(path);
                    break;
                }
            }
        }
        free(path);
    }

    free(expected_filename);
    closedir(dir);
}

static void search_gbasic_path_for_library(const char *name,
                                           int exact_filename,
                                           LibraryMatch **matches,
                                           size_t *match_count) {
    const char *gbasic_path = getenv("GBASIC_PATH");
    if (!gbasic_path || gbasic_path[0] == '\0') {
        return;
    }

    const char *start = gbasic_path;
    while (*start) {
        const char *end = strchr(start, ':');
        size_t len = end ? (size_t)(end - start) : strlen(start);
        if (len > 0) {
            char *dir = malloc(len + 1);
            if (!dir) {
                abort();
            }
            memcpy(dir, start, len);
            dir[len] = '\0';
            search_directory_for_library(dir, name, 1, exact_filename, matches, match_count);
            free(dir);
            if (error_action_pending()) {
                return;
            }
        }
        if (!end) {
            break;
        }
        start = end + 1;
    }
}

static void library_import_from_block(AstStmt *library);

static void library_import(const char *name, const char *path) {
    AstStmt *library = NULL;
    char *resolved = NULL;
    char *previous_import_path = NULL;

    if (!path && strcmp(name, "pg") == 0) {
#if HAVE_LIBPQ
        pg_library_loaded = 1;
#else
        runtime_error_raise("PostgreSQL support is not available in this build",
                            2001,
                            "postgres");
#endif
        return;
    }

    if (!path && strcmp(name, "sqlite") == 0) {
#if HAVE_SQLITE3
        sqlite_library_loaded = 1;
#else
        runtime_error_raise("SQLite support is not available in this build",
                            2002,
                            "sqlite");
#endif
        return;
    }

    if (!path && strcmp(name, "webclient") == 0) {
#if HAVE_LIBCURL
        if (!webclient_curl_initialized) {
            if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
                runtime_error_raise("could not initialize libcurl",
                                    3001,
                                    "webclient");
                return;
            }
            webclient_curl_initialized = 1;
        }
        webclient_library_loaded = 1;
#else
        runtime_error_raise("WebClient support is not available in this build",
                            3001,
                            "webclient");
#endif
        return;
    }

    if (!path && strcmp(name, "webserver") == 0) {
        webserver_library_loaded = 1;
        return;
    }

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
    if (library) {
        const char *source_path = root_source_path ? root_source_path : "<current>";
        if (use_pair_contains(used_pairs, used_pair_count, source_path, name)) {
            return;
        }
        if (use_pair_contains(use_stack, use_stack_count, source_path, name)) {
            char message[512];
            snprintf(message, sizeof(message), "circular use detected for %s from %s", name, source_path);
            runtime_error_raise(message, 1003, "use");
            return;
        }

        use_pair_add(&use_stack, &use_stack_count, source_path, name);
        library_import_from_block(library);
        use_pair_pop(use_stack, &use_stack_count);
        if (!error_action_pending()) {
            use_pair_add(&used_pairs, &used_pair_count, source_path, name);
        }
        return;
    }

    LibraryMatch *matches = NULL;
    size_t match_count = 0;
    const char *base = current_import_path ? current_import_path : root_source_path;
    char *base_dir = dirname_copy(base ? base : ".");

    search_directory_for_library(base_dir, name, 0, 1, &matches, &match_count);
    if (!error_action_pending()) {
        search_directory_for_library(base_dir, name, 1, 1, &matches, &match_count);
    }
    if (!error_action_pending()) {
        search_gbasic_path_for_library(name, 1, &matches, &match_count);
    }
    if (!error_action_pending() && match_count == 0) {
        search_directory_for_library(base_dir, name, 0, 0, &matches, &match_count);
    }
    if (!error_action_pending() && match_count == 0) {
        search_directory_for_library(base_dir, name, 1, 0, &matches, &match_count);
    }
    if (!error_action_pending() && match_count == 0) {
        search_gbasic_path_for_library(name, 0, &matches, &match_count);
    }
    free(base_dir);

    if (error_action_pending()) {
        library_matches_clear(matches, match_count);
        return;
    }

    if (match_count == 0) {
        char message[256];
        snprintf(message, sizeof(message), "library not found: %s", name);
        runtime_error_raise(message, 1003, "use");
        library_matches_clear(matches, match_count);
        return;
    }

    if (use_pair_contains(used_pairs, used_pair_count, matches[0].path, name)) {
        library_matches_clear(matches, match_count);
        return;
    }
    if (use_pair_contains(use_stack, use_stack_count, matches[0].path, name)) {
        char message[512];
        snprintf(message, sizeof(message), "circular use detected for %s from %s", name, matches[0].path);
        runtime_error_raise(message, 1003, "use");
        library_matches_clear(matches, match_count);
        return;
    }

    for (size_t i = 1; i < match_count; i++) {
        fprintf(stderr,
                "warning: additional library '%s' match ignored: %s\n",
                name,
                matches[i].path);
    }

    use_pair_add(&use_stack, &use_stack_count, matches[0].path, name);
    previous_import_path = current_import_path;
    current_import_path = matches[0].path;
    library_import_from_block(matches[0].library);
    current_import_path = previous_import_path;
    use_pair_pop(use_stack, &use_stack_count);
    if (!error_action_pending()) {
        use_pair_add(&used_pairs, &used_pair_count, matches[0].path, name);
    }
    library_matches_clear(matches, match_count);
}

static void library_import_from_block(AstStmt *library) {
    if (strcmp(library->as.library.name, "gui") == 0) {
        gui_library_loaded = 1;
    }
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

static Value lines_from_text(const char *text, size_t size) {
    Value *items = NULL;
    size_t count = 0;
    size_t start = 0;

    for (size_t i = 0; i < size; i++) {
        if (text[i] != '\n') {
            continue;
        }
        size_t line_length = i - start;
        if (line_length > 0 && text[start + line_length - 1] == '\r') {
            line_length--;
        }
        char *line = malloc(line_length + 1);
        if (!line) {
            abort();
        }
        memcpy(line, text + start, line_length);
        line[line_length] = '\0';

        Value *next = realloc(items, sizeof(Value) * (count + 1));
        if (!next) {
            abort();
        }
        items = next;
        items[count++] = value_string(line);
        free(line);
        start = i + 1;
    }

    if (start < size) {
        size_t line_length = size - start;
        if (line_length > 0 && text[start + line_length - 1] == '\r') {
            line_length--;
        }
        char *line = malloc(line_length + 1);
        if (!line) {
            abort();
        }
        memcpy(line, text + start, line_length);
        line[line_length] = '\0';

        Value *next = realloc(items, sizeof(Value) * (count + 1));
        if (!next) {
            abort();
        }
        items = next;
        items[count++] = value_string(line);
        free(line);
    }

    return value_array(items, count);
}

static const char *file_target_path(Value value) {
    if (value.kind == VALUE_FILE) {
        return value.as.file_path;
    }
    if (value.kind == VALUE_STRING) {
        return value.as.string;
    }
    return NULL;
}

static const char *directory_path(Value value) {
    if (value.kind == VALUE_DIR) {
        return value.as.dir_path;
    }
    return file_target_path(value);
}

static char *join_path_utility(const char *left, const char *right) {
    size_t left_len = strlen(left);
    while (left_len > 1 && left[left_len - 1] == '/') {
        left_len--;
    }

    const char *right_start = right;
    if (left_len > 0) {
        while (*right_start == '/') {
            right_start++;
        }
    }
    size_t right_len = strlen(right_start);
    int needs_slash = left_len > 0 && right_len > 0 && left[left_len - 1] != '/';

    char *result = malloc(left_len + (size_t)needs_slash + right_len + 1);
    if (!result) {
        abort();
    }
    memcpy(result, left, left_len);
    if (needs_slash) {
        result[left_len] = '/';
    }
    memcpy(result + left_len + (size_t)needs_slash, right_start, right_len);
    result[left_len + (size_t)needs_slash + right_len] = '\0';
    return result;
}

static size_t path_length_without_trailing_separators(const char *path) {
    size_t length = strlen(path);
    while (length > 1 && path[length - 1] == '/') {
        length--;
    }
    return length;
}

static Value eval_path_call(AstExpr *expr) {
    const char *name = expr->as.call.name;
    size_t expected_count = strcmp(name, "join_path") == 0 ? 2 : 1;
    if (expr->as.call.args.count != expected_count) {
        char message[256];
        snprintf(message,
                 sizeof(message),
                 "%s expects %s",
                 name,
                 expected_count == 2 ? "two path arguments" : "one path argument");
        runtime_error_raise(message, 1004, "path operation");
        return value_null();
    }

    Value first = eval_expr(expr->as.call.args.items[0]);
    Value second = value_null();
    if (expected_count == 2) {
        second = eval_expr(expr->as.call.args.items[1]);
    }
    if (error_action_pending()) {
        value_free(first);
        value_free(second);
        return value_null();
    }

    const char *first_path = directory_path(first);
    const char *second_path = expected_count == 2 ? directory_path(second) : NULL;
    if (!first_path || (expected_count == 2 && !second_path)) {
        char message[256];
        snprintf(message,
                 sizeof(message),
                 "%s expects %s",
                 name,
                 expected_count == 2
                     ? "string, file reference, or directory reference arguments"
                     : "a string, file reference, or directory reference");
        runtime_error_raise(message, 1004, "path operation");
        value_free(first);
        value_free(second);
        return value_null();
    }

    Value result = value_null();
    if (strcmp(name, "join_path") == 0) {
        char *joined = join_path_utility(first_path, second_path);
        result = value_string(joined);
        free(joined);
    } else {
        size_t length = path_length_without_trailing_separators(first_path);
        const char *slash = NULL;
        for (size_t i = length; i > 0; i--) {
            if (first_path[i - 1] == '/') {
                slash = first_path + i - 1;
                break;
            }
        }

        if (strcmp(name, "file_name") == 0) {
            const char *base = slash ? slash + 1 : first_path;
            size_t base_length = length - (size_t)(base - first_path);
            char *text = malloc(base_length + 1);
            if (!text) {
                abort();
            }
            memcpy(text, base, base_length);
            text[base_length] = '\0';
            result = value_string(text);
            free(text);
        } else if (strcmp(name, "directory_name") == 0) {
            if (!slash) {
                result = value_string(".");
            } else if (slash == first_path) {
                result = value_string("/");
            } else {
                size_t dir_length = (size_t)(slash - first_path);
                char *text = malloc(dir_length + 1);
                if (!text) {
                    abort();
                }
                memcpy(text, first_path, dir_length);
                text[dir_length] = '\0';
                result = value_string(text);
                free(text);
            }
        } else {
            const char *base = slash ? slash + 1 : first_path;
            size_t base_length = length - (size_t)(base - first_path);
            const char *dot = NULL;
            for (size_t i = base_length; i > 1; i--) {
                if (base[i - 1] == '.') {
                    dot = base + i - 1;
                    break;
                }
            }
            if (!dot) {
                result = value_string("");
            } else {
                size_t extension_length = base_length - (size_t)(dot + 1 - base);
                char *text = malloc(extension_length + 1);
                if (!text) {
                    abort();
                }
                memcpy(text, dot + 1, extension_length);
                text[extension_length] = '\0';
                result = value_string(text);
                free(text);
            }
        }
    }

    value_free(first);
    value_free(second);
    return result;
}

static int copy_file_path(const char *source_path, const char *target_path) {
    if (strcmp(source_path, target_path) == 0) {
        return 0;
    }

    FILE *source = fopen(source_path, "rb");
    if (!source) {
        return 0;
    }

    struct stat source_stat;
    int has_source_stat = stat(source_path, &source_stat) == 0;
    FILE *target = fopen(target_path, "wb");
    if (!target) {
        fclose(source);
        return 0;
    }

    char buffer[8192];
    int ok = 1;
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        if (fwrite(buffer, 1, count, target) != count) {
            ok = 0;
            break;
        }
    }
    if (ferror(source)) {
        ok = 0;
    }
    if (fclose(target) != 0) {
        ok = 0;
    }
    fclose(source);

    if (ok && has_source_stat &&
        chmod(target_path, source_stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) != 0) {
        ok = 0;
    }
    if (!ok) {
        unlink(target_path);
    }
    return ok;
}

static int move_file_path(const char *source_path, const char *target_path) {
    if (rename(source_path, target_path) == 0) {
        return 1;
    }
    if (errno != EXDEV || !copy_file_path(source_path, target_path)) {
        return 0;
    }
    if (unlink(source_path) == 0) {
        return 1;
    }
    unlink(target_path);
    return 0;
}

static int file_value_compare(const void *left, const void *right) {
    const Value *a = left;
    const Value *b = right;
    return strcmp(a->as.file_path, b->as.file_path);
}

static Value eval_file_call(AstExpr *expr) {
    const char *name = expr->as.call.name;

    if (strcmp(name, "exists") == 0 ||
        strcmp(name, "read") == 0 ||
        strcmp(name, "read_lines") == 0 ||
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
        if (strcmp(name, "read_lines") == 0) {
            Value result = lines_from_text(text, (size_t)size);
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

    if (strcmp(name, "delete") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("delete expects one file argument", 1004, "file operation");
            return value_null();
        }
        Value file_value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(file_value);
            return value_null();
        }
        if (file_value.kind != VALUE_FILE) {
            runtime_error_raise("delete expects a file reference", 1004, "file operation");
            value_free(file_value);
            return value_null();
        }
        if (unlink(file_value.as.file_path) != 0) {
            char message[512];
            snprintf(message, sizeof(message), "could not delete file: %s", file_value.as.file_path);
            runtime_error_raise(message, 1004, "file operation");
            value_free(file_value);
            return value_null();
        }
        value_free(file_value);
        return value_bool(1);
    }

    if (strcmp(name, "copy") == 0 || strcmp(name, "move") == 0) {
        if (expr->as.call.args.count != 2) {
            char message[256];
            snprintf(message, sizeof(message), "%s expects source and target arguments", name);
            runtime_error_raise(message, 1004, "file operation");
            return value_null();
        }
        Value source = eval_expr(expr->as.call.args.items[0]);
        Value target = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(source);
            value_free(target);
            return value_null();
        }
        const char *target_path = file_target_path(target);
        if (source.kind != VALUE_FILE || !target_path) {
            char message[256];
            snprintf(message,
                     sizeof(message),
                     "%s expects a file reference and file reference or string target",
                     name);
            runtime_error_raise(message, 1004, "file operation");
            value_free(source);
            value_free(target);
            return value_null();
        }

        int ok = strcmp(name, "copy") == 0
                     ? copy_file_path(source.as.file_path, target_path)
                     : move_file_path(source.as.file_path, target_path);
        if (!ok) {
            char message[1024];
            snprintf(message,
                     sizeof(message),
                     "could not %s file: %s -> %s",
                     name,
                     source.as.file_path,
                     target_path);
            runtime_error_raise(message, 1004, "file operation");
            value_free(source);
            value_free(target);
            return value_null();
        }
        value_free(source);
        value_free(target);
        return value_bool(1);
    }

    if (strcmp(name, "list_files") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("list_files expects one path argument", 1004, "file operation");
            return value_null();
        }
        Value path_value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(path_value);
            return value_null();
        }
        const char *path = NULL;
        if (path_value.kind == VALUE_STRING) {
            path = path_value.as.string;
        } else if (path_value.kind == VALUE_DIR) {
            path = path_value.as.dir_path;
        }
        if (!path) {
            runtime_error_raise("list_files expects a string or directory reference",
                                1004,
                                "file operation");
            value_free(path_value);
            return value_null();
        }

        DIR *dir = opendir(path);
        if (!dir) {
            char message[512];
            snprintf(message, sizeof(message), "could not list files: %s", path);
            runtime_error_raise(message, 1004, "file operation");
            value_free(path_value);
            return value_null();
        }

        Value *items = NULL;
        size_t count = 0;
        int read_failed = 0;
        for (;;) {
            errno = 0;
            struct dirent *entry = readdir(dir);
            if (!entry) {
                read_failed = errno != 0;
                break;
            }
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char *entry_path = join_path(path, entry->d_name);
            struct stat st;
            if (stat(entry_path, &st) == 0 && S_ISREG(st.st_mode)) {
                Value *next = realloc(items, sizeof(Value) * (count + 1));
                if (!next) {
                    abort();
                }
                items = next;
                items[count++] = value_file(entry_path);
            }
            free(entry_path);
        }
        if (closedir(dir) != 0 || read_failed) {
            for (size_t i = 0; i < count; i++) {
                value_free(items[i]);
            }
            free(items);
            char message[512];
            snprintf(message, sizeof(message), "could not list files: %s", path);
            runtime_error_raise(message, 1004, "file operation");
            value_free(path_value);
            return value_null();
        }

        if (count > 1) {
            qsort(items, count, sizeof(Value), file_value_compare);
        }
        value_free(path_value);
        return value_array(items, count);
    }

    if (strcmp(name, "make_dir") == 0 || strcmp(name, "remove_dir") == 0) {
        if (expr->as.call.args.count != 1) {
            char message[256];
            snprintf(message, sizeof(message), "%s expects one path argument", name);
            runtime_error_raise(message, 1004, "file operation");
            return value_null();
        }
        Value path_value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(path_value);
            return value_null();
        }
        const char *path = directory_path(path_value);
        if (!path) {
            char message[256];
            snprintf(message,
                     sizeof(message),
                     "%s expects a string, file reference, or directory reference",
                     name);
            runtime_error_raise(message, 1004, "file operation");
            value_free(path_value);
            return value_null();
        }

        int ok = strcmp(name, "make_dir") == 0
                     ? mkdir(path, 0777) == 0
                     : rmdir(path) == 0;
        if (!ok) {
            char message[512];
            snprintf(message,
                     sizeof(message),
                     "could not %s directory: %s",
                     strcmp(name, "make_dir") == 0 ? "create" : "remove",
                     path);
            runtime_error_raise(message, 1004, "file operation");
            value_free(path_value);
            return value_null();
        }
        value_free(path_value);
        return value_bool(1);
    }

    if (strcmp(name, "overwrite") == 0) {
        if (expr->as.call.args.count != 3) {
            runtime_error_raise("overwrite expects file, text, and position arguments",
                                1004,
                                "file operation");
            return value_null();
        }
        Value file_value = eval_expr(expr->as.call.args.items[0]);
        Value text_value = eval_expr(expr->as.call.args.items[1]);
        Value position_value = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(file_value);
            value_free(text_value);
            value_free(position_value);
            return value_null();
        }
        if (file_value.kind != VALUE_FILE) {
            runtime_error_raise("overwrite expects a file reference", 1004, "file operation");
            value_free(file_value);
            value_free(text_value);
            value_free(position_value);
            return value_null();
        }
        if (text_value.kind != VALUE_STRING) {
            runtime_error_raise("overwrite expects text to be a string", 1004, "file operation");
            value_free(file_value);
            value_free(text_value);
            value_free(position_value);
            return value_null();
        }
        if (position_value.kind != VALUE_NUMBER ||
            !isfinite(position_value.as.number) ||
            position_value.as.number != floor(position_value.as.number)) {
            runtime_error_raise("overwrite position must be an integer", 1004, "file operation");
            value_free(file_value);
            value_free(text_value);
            value_free(position_value);
            return value_null();
        }
        if (position_value.as.number < 0) {
            runtime_error_raise("overwrite position must be non-negative", 1004, "file operation");
            value_free(file_value);
            value_free(text_value);
            value_free(position_value);
            return value_null();
        }
        if (position_value.as.number > LONG_MAX) {
            runtime_error_raise("overwrite position is beyond end of file", 1004, "file operation");
            value_free(file_value);
            value_free(text_value);
            value_free(position_value);
            return value_null();
        }

        FILE *file = fopen(file_value.as.file_path, "r+b");
        if (!file) {
            char message[512];
            snprintf(message,
                     sizeof(message),
                     "could not overwrite file: %s",
                     file_value.as.file_path);
            runtime_error_raise(message, 1004, "file operation");
            value_free(file_value);
            value_free(text_value);
            value_free(position_value);
            return value_null();
        }
        int ok = fseek(file, 0, SEEK_END) == 0;
        long size = ok ? ftell(file) : -1;
        long position = (long)position_value.as.number;
        if (!ok || size < 0) {
            fclose(file);
            char message[512];
            snprintf(message,
                     sizeof(message),
                     "could not overwrite file: %s",
                     file_value.as.file_path);
            runtime_error_raise(message, 1004, "file operation");
            value_free(file_value);
            value_free(text_value);
            value_free(position_value);
            return value_null();
        }
        if (position > size) {
            fclose(file);
            runtime_error_raise("overwrite position is beyond end of file",
                                1004,
                                "file operation");
            value_free(file_value);
            value_free(text_value);
            value_free(position_value);
            return value_null();
        }

        size_t text_size = strlen(text_value.as.string);
        ok = fseek(file, position, SEEK_SET) == 0;
        if (ok && text_size > 0) {
            ok = fwrite(text_value.as.string, 1, text_size, file) == text_size;
        }
        if (fclose(file) != 0) {
            ok = 0;
        }
        if (!ok) {
            char message[512];
            snprintf(message,
                     sizeof(message),
                     "could not overwrite file: %s",
                     file_value.as.file_path);
            runtime_error_raise(message, 1004, "file operation");
            value_free(file_value);
            value_free(text_value);
            value_free(position_value);
            return value_null();
        }

        value_free(file_value);
        value_free(text_value);
        value_free(position_value);
        return value_bool(1);
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

    Value *args = NULL;
    if (expr->as.call.args.count > 0) {
        args = malloc(sizeof(Value) * expr->as.call.args.count);
        if (!args) {
            abort();
        }
    }
    for (size_t i = 0; i < expr->as.call.args.count; i++) {
        args[i] = eval_expr(expr->as.call.args.items[i]);
    }

    Env local_env = {0};
    local_env.parent = &global_env;
    Env *previous_env = current_env;
    current_env = &local_env;

    for (size_t i = 0; i < expr->as.call.args.count; i++) {
        env_set(stmt->as.function.params.items[i], args[i]);
    }
    free(args);

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
        if (result.did_break || result.did_continue || result.did_stop) {
            break;
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
    if (result.did_break || result.did_continue) {
        runtime_error_raise(result.did_break ? "break outside loop" : "continue outside loop",
                            1003,
                            "invalid control flow");
        value_free(result.value);
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

static int values_equal(Value left, Value right) {
    AstExpr fake = {0};
    fake.kind = AST_EXPR_BINARY;
    fake.as.binary.op = "=";
    fake.as.binary.modifier = ast_modifier_none();
    Value result = eval_comparison(&fake, left, right);
    int equal = result.kind == VALUE_BOOL && result.as.boolean;
    value_free(result);
    return equal;
}

static Value builtin_len_value(Value value) {
    if (value.kind == VALUE_ARRAY) {
        double count = (double)value.as.array.count;
        value_free(value);
        return value_number(count);
    }
    if (value.kind == VALUE_STRING) {
        double count = (double)strlen(value.as.string);
        value_free(value);
        return value_number(count);
    }
    value_free(value);
    runtime_error_raise("len expects string or array", 1003, "invalid function call");
    return value_null();
}

static Value builtin_trim_value(Value text) {
    if (text.kind != VALUE_STRING) {
        value_free(text);
        runtime_error_raise("trim expects a string", 1003, "invalid function call");
        return value_null();
    }
    const char *start = text.as.string;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    const char *end = text.as.string + strlen(text.as.string);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    size_t length = (size_t)(end - start);
    char *trimmed = malloc(length + 1);
    if (!trimmed) {
        abort();
    }
    memcpy(trimmed, start, length);
    trimmed[length] = '\0';
    Value result = value_string(trimmed);
    free(trimmed);
    value_free(text);
    return result;
}

static Value builtin_lower_value(Value value) {
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

static Value builtin_upper_value(Value value) {
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

static Value builtin_split_value(Value text, Value separator, int has_separator) {
    if (text.kind != VALUE_STRING) {
        value_free(text);
        value_free(separator);
        runtime_error_raise("split expects a string", 1003, "invalid function call");
        return value_null();
    }

    if (has_separator) {
        if (separator.kind != VALUE_STRING) {
            value_free(text);
            value_free(separator);
            runtime_error_raise("split separator must be a string", 1003, "invalid function call");
            return value_null();
        }
        if (separator.as.string[0] == '\0') {
            value_free(text);
            value_free(separator);
            runtime_error_raise("split separator cannot be empty", 1003, "invalid function call");
            return value_null();
        }
    }

    Value *items = NULL;
    size_t count = 0;
    if (!has_separator) {
        const char *p = text.as.string;
        while (*p) {
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
            const char *start = p;
            while (*p && !isspace((unsigned char)*p)) {
                p++;
            }
            if (p > start) {
                size_t length = (size_t)(p - start);
                char *part = malloc(length + 1);
                if (!part) {
                    abort();
                }
                memcpy(part, start, length);
                part[length] = '\0';
                Value *next = realloc(items, sizeof(Value) * (count + 1));
                if (!next) {
                    abort();
                }
                items = next;
                items[count++] = value_string(part);
                free(part);
            }
        }
    } else {
        const char *sep = separator.as.string;
        size_t sep_len = strlen(sep);
        const char *start = text.as.string;
        for (;;) {
            const char *found = strstr(start, sep);
            size_t length = found ? (size_t)(found - start) : strlen(start);
            char *part = malloc(length + 1);
            if (!part) {
                abort();
            }
            memcpy(part, start, length);
            part[length] = '\0';
            Value *next = realloc(items, sizeof(Value) * (count + 1));
            if (!next) {
                abort();
            }
            items = next;
            items[count++] = value_string(part);
            free(part);
            if (!found) {
                break;
            }
            start = found + sep_len;
        }
    }

    value_free(text);
    value_free(separator);
    return value_array(items, count);
}

static Value builtin_join_value(Value array, Value separator) {
    if (array.kind != VALUE_ARRAY) {
        value_free(array);
        value_free(separator);
        runtime_error_raise("join expects an array", 1003, "invalid function call");
        return value_null();
    }
    if (separator.kind != VALUE_STRING) {
        value_free(array);
        value_free(separator);
        runtime_error_raise("join separator must be a string", 1003, "invalid function call");
        return value_null();
    }

    size_t sep_len = strlen(separator.as.string);
    size_t total = 0;
    for (size_t i = 0; i < array.as.array.count; i++) {
        if (array.as.array.items[i].kind != VALUE_STRING) {
            value_free(array);
            value_free(separator);
            runtime_error_raise("join array elements must be strings", 1003, "invalid function call");
            return value_null();
        }
        total += strlen(array.as.array.items[i].as.string);
        if (i > 0) {
            total += sep_len;
        }
    }

    char *joined = malloc(total + 1);
    if (!joined) {
        abort();
    }
    size_t offset = 0;
    for (size_t i = 0; i < array.as.array.count; i++) {
        if (i > 0) {
            memcpy(joined + offset, separator.as.string, sep_len);
            offset += sep_len;
        }
        size_t part_len = strlen(array.as.array.items[i].as.string);
        memcpy(joined + offset, array.as.array.items[i].as.string, part_len);
        offset += part_len;
    }
    joined[offset] = '\0';
    Value result = value_string(joined);
    free(joined);
    value_free(array);
    value_free(separator);
    return result;
}

static Value append_to_array_value(Value array, Value item, int prepend) {
    if (array.kind != VALUE_ARRAY) {
        value_free(array);
        value_free(item);
        runtime_error_raise(prepend ? "prepend expects an array" : "append expects an array",
                            1003,
                            "invalid function call");
        return value_null();
    }

    Value *items = malloc(sizeof(Value) * (array.as.array.count + 1));
    if (!items) {
        abort();
    }
    if (prepend) {
        items[0] = item;
        for (size_t i = 0; i < array.as.array.count; i++) {
            items[i + 1] = array.as.array.items[i];
        }
    } else {
        for (size_t i = 0; i < array.as.array.count; i++) {
            items[i] = array.as.array.items[i];
        }
        items[array.as.array.count] = item;
    }
    free(array.as.array.items);
    array.as.array.items = items;
    array.as.array.count++;
    return array;
}

static Value append_to_array_ref(Value *array, Value item, int prepend) {
    if (!array || array->kind != VALUE_ARRAY) {
        value_free(item);
        runtime_error_raise(prepend ? "prepend expects an array" : "append expects an array",
                            1003,
                            "invalid function call");
        return value_null();
    }

    Value *items = realloc(array->as.array.items,
                           sizeof(Value) * (array->as.array.count + 1));
    if (!items) {
        abort();
    }
    array->as.array.items = items;
    if (prepend) {
        memmove(array->as.array.items + 1,
                array->as.array.items,
                sizeof(Value) * array->as.array.count);
        array->as.array.items[0] = item;
    } else {
        array->as.array.items[array->as.array.count] = item;
    }
    array->as.array.count++;
    return value_copy(*array);
}

static int array_index_from_value(Value index_value, const char *name, int *out_index) {
    if (index_value.kind != VALUE_NUMBER) {
        value_free(index_value);
        char message[128];
        snprintf(message, sizeof(message), "%s index must be a number", name);
        runtime_error_raise(message, 1003, "invalid function call");
        return 0;
    }
    int index = (int)index_value.as.number;
    if ((double)index != index_value.as.number) {
        value_free(index_value);
        char message[128];
        snprintf(message, sizeof(message), "%s index must be an integer", name);
        runtime_error_raise(message, 1003, "invalid function call");
        return 0;
    }
    value_free(index_value);
    *out_index = index;
    return 1;
}

static Value insert_into_array_value(Value array, int index, Value item) {
    if (array.kind != VALUE_ARRAY) {
        value_free(array);
        value_free(item);
        runtime_error_raise("insert expects an array", 1003, "invalid function call");
        return value_null();
    }
    if (index < 0 || (size_t)index > array.as.array.count) {
        value_free(array);
        value_free(item);
        runtime_error_raise("insert index out of range", 1003, "invalid function call");
        return value_null();
    }

    Value *items = malloc(sizeof(Value) * (array.as.array.count + 1));
    if (!items) {
        abort();
    }
    for (size_t i = 0; i < (size_t)index; i++) {
        items[i] = array.as.array.items[i];
    }
    items[index] = item;
    for (size_t i = (size_t)index; i < array.as.array.count; i++) {
        items[i + 1] = array.as.array.items[i];
    }
    free(array.as.array.items);
    array.as.array.items = items;
    array.as.array.count++;
    return array;
}

static Value insert_into_array_ref(Value *array, int index, Value item) {
    if (!array || array->kind != VALUE_ARRAY) {
        value_free(item);
        runtime_error_raise("insert expects an array", 1003, "invalid function call");
        return value_null();
    }
    if (index < 0 || (size_t)index > array->as.array.count) {
        value_free(item);
        runtime_error_raise("insert index out of range", 1003, "invalid function call");
        return value_null();
    }

    Value *items = realloc(array->as.array.items,
                           sizeof(Value) * (array->as.array.count + 1));
    if (!items) {
        abort();
    }
    array->as.array.items = items;
    memmove(array->as.array.items + index + 1,
            array->as.array.items + index,
            sizeof(Value) * (array->as.array.count - (size_t)index));
    array->as.array.items[index] = item;
    array->as.array.count++;
    return value_copy(*array);
}

static Value remove_from_array_value(Value array, int index) {
    if (array.kind != VALUE_ARRAY) {
        value_free(array);
        runtime_error_raise("remove expects an array", 1003, "invalid function call");
        return value_null();
    }
    if (index < 0 || (size_t)index >= array.as.array.count) {
        value_free(array);
        runtime_error_raise("remove index out of range", 1003, "invalid function call");
        return value_null();
    }

    value_free(array.as.array.items[index]);
    for (size_t i = (size_t)index + 1; i < array.as.array.count; i++) {
        array.as.array.items[i - 1] = array.as.array.items[i];
    }
    array.as.array.count--;
    if (array.as.array.count == 0) {
        free(array.as.array.items);
        array.as.array.items = NULL;
        return array;
    }
    Value *items = realloc(array.as.array.items, sizeof(Value) * array.as.array.count);
    if (items) {
        array.as.array.items = items;
    }
    return array;
}

static Value remove_from_array_ref(Value *array, int index) {
    if (!array || array->kind != VALUE_ARRAY) {
        runtime_error_raise("remove expects an array", 1003, "invalid function call");
        return value_null();
    }
    if (index < 0 || (size_t)index >= array->as.array.count) {
        runtime_error_raise("remove index out of range", 1003, "invalid function call");
        return value_null();
    }

    value_free(array->as.array.items[index]);
    for (size_t i = (size_t)index + 1; i < array->as.array.count; i++) {
        array->as.array.items[i - 1] = array->as.array.items[i];
    }
    array->as.array.count--;
    if (array->as.array.count == 0) {
        free(array->as.array.items);
        array->as.array.items = NULL;
    } else {
        Value *items = realloc(array->as.array.items,
                               sizeof(Value) * array->as.array.count);
        if (items) {
            array->as.array.items = items;
        }
    }
    return value_copy(*array);
}

static int array_find_index(Value array, Value target, size_t *out_index) {
    if (array.kind != VALUE_ARRAY) {
        return 0;
    }
    for (size_t i = 0; i < array.as.array.count; i++) {
        if (values_equal(value_copy(array.as.array.items[i]), value_copy(target))) {
            *out_index = i;
            return 1;
        }
        if (error_action_pending()) {
            return 0;
        }
    }
    return 0;
}

static Value remove_value_from_array_value(Value array, Value target) {
    if (array.kind != VALUE_ARRAY) {
        value_free(array);
        value_free(target);
        runtime_error_raise("remove_value expects an array", 1003, "invalid function call");
        return value_null();
    }

    size_t index = 0;
    if (!array_find_index(array, target, &index)) {
        value_free(target);
        if (error_action_pending()) {
            value_free(array);
            return value_null();
        }
        return array;
    }
    value_free(target);
    return remove_from_array_value(array, (int)index);
}

static Value remove_value_from_array_ref(Value *array, Value target, int *changed) {
    *changed = 0;
    if (!array || array->kind != VALUE_ARRAY) {
        value_free(target);
        runtime_error_raise("remove_value expects an array", 1003, "invalid function call");
        return value_null();
    }

    size_t index = 0;
    if (!array_find_index(*array, target, &index)) {
        value_free(target);
        if (error_action_pending()) {
            return value_null();
        }
        return value_copy(*array);
    }
    value_free(target);
    *changed = 1;
    return remove_from_array_ref(array, (int)index);
}

static Value array_rest_value(Value array) {
    if (array.kind != VALUE_ARRAY) {
        value_free(array);
        runtime_error_raise("rest expects an array", 1003, "invalid function call");
        return value_null();
    }
    if (array.as.array.count <= 1) {
        value_free(array);
        return value_array(NULL, 0);
    }

    size_t count = array.as.array.count - 1;
    Value *items = malloc(sizeof(Value) * count);
    if (!items) {
        abort();
    }
    for (size_t i = 0; i < count; i++) {
        items[i] = value_copy(array.as.array.items[i + 1]);
    }
    value_free(array);
    return value_array(items, count);
}

static Value take_from_array_value(Value array, int take_last) {
    if (array.kind != VALUE_ARRAY) {
        value_free(array);
        runtime_error_raise(take_last ? "take_last expects an array" : "take_first expects an array",
                            1003,
                            "invalid function call");
        return value_null();
    }
    if (array.as.array.count == 0) {
        value_free(array);
        runtime_error_raise(take_last ? "take_last expects a non-empty array" : "take_first expects a non-empty array",
                            1003,
                            "invalid function call");
        return value_null();
    }

    size_t index = take_last ? array.as.array.count - 1 : 0;
    Value result = array.as.array.items[index];
    for (size_t i = index + 1; i < array.as.array.count; i++) {
        array.as.array.items[i - 1] = array.as.array.items[i];
    }
    array.as.array.count--;
    if (array.as.array.count == 0) {
        free(array.as.array.items);
    } else {
        Value *items = realloc(array.as.array.items, sizeof(Value) * array.as.array.count);
        if (items) {
            array.as.array.items = items;
        }
        value_free(array);
    }
    return result;
}

static Value take_from_array_ref(Value *array, int take_last) {
    if (!array || array->kind != VALUE_ARRAY) {
        runtime_error_raise(take_last ? "take_last expects an array" : "take_first expects an array",
                            1003,
                            "invalid function call");
        return value_null();
    }
    if (array->as.array.count == 0) {
        runtime_error_raise(take_last ? "take_last expects a non-empty array" : "take_first expects a non-empty array",
                            1003,
                            "invalid function call");
        return value_null();
    }

    size_t index = take_last ? array->as.array.count - 1 : 0;
    Value result = array->as.array.items[index];
    for (size_t i = index + 1; i < array->as.array.count; i++) {
        array->as.array.items[i - 1] = array->as.array.items[i];
    }
    array->as.array.count--;
    if (array->as.array.count == 0) {
        free(array->as.array.items);
        array->as.array.items = NULL;
    } else {
        Value *items = realloc(array->as.array.items,
                               sizeof(Value) * array->as.array.count);
        if (items) {
            array->as.array.items = items;
        }
    }
    return result;
}

static void reverse_array_items(Value *items, size_t count) {
    for (size_t i = 0; i < count / 2; i++) {
        Value tmp = items[i];
        items[i] = items[count - i - 1];
        items[count - i - 1] = tmp;
    }
}

static Value reverse_array_value(Value array) {
    if (array.kind != VALUE_ARRAY) {
        value_free(array);
        runtime_error_raise("reverse expects an array", 1003, "invalid function call");
        return value_null();
    }
    reverse_array_items(array.as.array.items, array.as.array.count);
    return array;
}

static Value reverse_array_ref(Value *array, int *changed) {
    *changed = 0;
    if (!array || array->kind != VALUE_ARRAY) {
        runtime_error_raise("reverse expects an array", 1003, "invalid function call");
        return value_null();
    }
    for (size_t i = 0; i < array->as.array.count / 2; i++) {
        if (!value_storage_equal(&array->as.array.items[i],
                                 &array->as.array.items[array->as.array.count - i - 1])) {
            *changed = 1;
            break;
        }
    }
    reverse_array_items(array->as.array.items, array->as.array.count);
    return value_copy(*array);
}

static int value_unique_comparable(Value value) {
    return value.kind == VALUE_NUMBER ||
        value.kind == VALUE_STRING ||
        value.kind == VALUE_BOOL ||
        value.kind == VALUE_DATETIME ||
        value.kind == VALUE_NULL ||
        value.kind == VALUE_UNKNOWN;
}

static int unique_values_equal(Value left, Value right) {
    if (left.kind != right.kind) {
        return 0;
    }
    switch (left.kind) {
    case VALUE_NUMBER:
        return left.as.number == right.as.number;
    case VALUE_STRING:
        return strcmp(left.as.string, right.as.string) == 0;
    case VALUE_BOOL:
        return left.as.boolean == right.as.boolean;
    case VALUE_DATETIME:
        return datetime_compare_exact(left.as.datetime, right.as.datetime) == 0;
    case VALUE_NULL:
    case VALUE_UNKNOWN:
        return 1;
    default:
        return 0;
    }
}

static int array_all_unique_comparable(Value array) {
    if (array.kind != VALUE_ARRAY) {
        runtime_error_raise("unique expects an array", 1003, "invalid function call");
        return 0;
    }
    for (size_t i = 0; i < array.as.array.count; i++) {
        if (!value_unique_comparable(array.as.array.items[i])) {
            runtime_error_raise("unique supports only scalar array values",
                                1003,
                                "invalid function call");
            return 0;
        }
    }
    return 1;
}

static Value unique_array_value(Value array) {
    if (!array_all_unique_comparable(array)) {
        value_free(array);
        return value_null();
    }

    size_t write = 0;
    for (size_t i = 0; i < array.as.array.count; i++) {
        int duplicate = 0;
        for (size_t j = 0; j < write; j++) {
            if (unique_values_equal(array.as.array.items[i], array.as.array.items[j])) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            value_free(array.as.array.items[i]);
        } else {
            if (write != i) {
                array.as.array.items[write] = array.as.array.items[i];
            }
            write++;
        }
    }
    array.as.array.count = write;
    if (write == 0) {
        free(array.as.array.items);
        array.as.array.items = NULL;
    } else {
        Value *items = realloc(array.as.array.items, sizeof(Value) * write);
        if (items) {
            array.as.array.items = items;
        }
    }
    return array;
}

static Value unique_array_ref(Value *array, int *changed) {
    *changed = 0;
    if (!array || array->kind != VALUE_ARRAY) {
        runtime_error_raise("unique expects an array", 1003, "invalid function call");
        return value_null();
    }
    if (!array_all_unique_comparable(*array)) {
        return value_null();
    }

    size_t write = 0;
    for (size_t i = 0; i < array->as.array.count; i++) {
        int duplicate = 0;
        for (size_t j = 0; j < write; j++) {
            if (unique_values_equal(array->as.array.items[i],
                                    array->as.array.items[j])) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            *changed = 1;
            value_free(array->as.array.items[i]);
        } else {
            if (write != i) {
                array->as.array.items[write] = array->as.array.items[i];
            }
            write++;
        }
    }
    array->as.array.count = write;
    if (write == 0) {
        free(array->as.array.items);
        array->as.array.items = NULL;
    } else {
        Value *items = realloc(array->as.array.items, sizeof(Value) * write);
        if (items) {
            array->as.array.items = items;
        }
    }
    return value_copy(*array);
}

static int value_sort_rank(Value value) {
    if (value.kind == VALUE_NULL) {
        return 0;
    }
    if (value.kind == VALUE_UNKNOWN) {
        return 1;
    }
    return 2;
}

static int value_sort_comparable(Value value) {
    return value.kind == VALUE_NUMBER ||
        value.kind == VALUE_STRING ||
        value.kind == VALUE_BOOL ||
        value.kind == VALUE_DATETIME ||
        value.kind == VALUE_NULL ||
        value.kind == VALUE_UNKNOWN;
}

static int array_all_sort_comparable(Value array) {
    if (array.kind != VALUE_ARRAY) {
        runtime_error_raise("sort expects an array", 1003, "invalid function call");
        return 0;
    }

    ValueKind ordinary_kind = VALUE_NULL;
    int have_ordinary_kind = 0;
    for (size_t i = 0; i < array.as.array.count; i++) {
        Value item = array.as.array.items[i];
        if (!value_sort_comparable(item)) {
            runtime_error_raise("sort supports only scalar array values",
                                1003,
                                "invalid function call");
            return 0;
        }
        if (item.kind == VALUE_NULL || item.kind == VALUE_UNKNOWN) {
            continue;
        }
        if (!have_ordinary_kind) {
            ordinary_kind = item.kind;
            have_ordinary_kind = 1;
        } else if (ordinary_kind != item.kind) {
            runtime_error_raise("sort requires ordinary values to have the same type",
                                1003,
                                "invalid function call");
            return 0;
        }
    }
    return 1;
}

static int sort_value_compare(const void *left_ptr, const void *right_ptr) {
    const Value *left = left_ptr;
    const Value *right = right_ptr;
    int left_rank = value_sort_rank(*left);
    int right_rank = value_sort_rank(*right);
    if (left_rank != right_rank) {
        return left_rank < right_rank ? -1 : 1;
    }
    if (left_rank < 2) {
        return 0;
    }

    if (left->kind == VALUE_NUMBER) {
        if (left->as.number < right->as.number) return -1;
        if (left->as.number > right->as.number) return 1;
        return 0;
    }
    if (left->kind == VALUE_STRING) {
        return strcmp(left->as.string, right->as.string);
    }
    if (left->kind == VALUE_BOOL) {
        return left->as.boolean - right->as.boolean;
    }
    if (left->kind == VALUE_DATETIME) {
        return datetime_compare_exact(left->as.datetime, right->as.datetime);
    }
    return 0;
}

static Value builtin_string_value(Value value);

static const char *builtin_type_name(Value value) {
    switch (value.kind) {
    case VALUE_NUMBER:
        return "number";
    case VALUE_STRING:
        return "string";
    case VALUE_BOOL:
        return "boolean";
    case VALUE_ARRAY:
        return "array";
    case VALUE_RECORD:
        return "record";
    case VALUE_NULL:
        return "nothing";
    case VALUE_UNKNOWN:
        return "unknown";
    case VALUE_DATETIME:
        return "datetime";
    case VALUE_DURATION:
        return "duration";
    case VALUE_MONEY:
        return "money";
    case VALUE_FILE:
        return "file";
    case VALUE_DIR:
        return "directory";
    case VALUE_POSTGRES_CONNECTION:
        return "postgres_connection";
    case VALUE_SQLITE_CONNECTION:
        return "sqlite_connection";
    }
    return "value";
}

static Value sort_array_value(Value array) {
    if (!array_all_sort_comparable(array)) {
        value_free(array);
        return value_null();
    }
    if (array.as.array.count > 1) {
        qsort(array.as.array.items, array.as.array.count, sizeof(Value), sort_value_compare);
    }
    return array;
}

static Value sort_array_ref(Value *array, int *changed) {
    *changed = 0;
    if (!array || array->kind != VALUE_ARRAY) {
        runtime_error_raise("sort expects an array", 1003, "invalid function call");
        return value_null();
    }
    if (!array_all_sort_comparable(*array)) {
        return value_null();
    }
    Value before = value_copy(*array);
    if (array->as.array.count > 1) {
        qsort(array->as.array.items,
              array->as.array.count,
              sizeof(Value),
              sort_value_compare);
    }
    *changed = !value_storage_equal(&before, array);
    value_free(before);
    return value_copy(*array);
}

static Value builtin_number_modifier_value(Value value) {
    if (value.kind == VALUE_NUMBER) {
        return value;
    }
    if (value.kind != VALUE_STRING) {
        value_free(value);
        runtime_error_raise("number modifier expects a number or numeric string", 1003, "modifier");
        return value_null();
    }

    const char *start = value.as.string;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    const char *end = value.as.string + strlen(value.as.string);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    if (start == end) {
        value_free(value);
        runtime_error_raise("number modifier expects a valid number string", 1003, "modifier");
        return value_null();
    }

    char *trimmed = malloc((size_t)(end - start) + 1);
    if (!trimmed) {
        abort();
    }
    memcpy(trimmed, start, (size_t)(end - start));
    trimmed[end - start] = '\0';

    errno = 0;
    char *parse_end = NULL;
    double number = strtod(trimmed, &parse_end);
    if (errno == ERANGE || parse_end == trimmed || *parse_end != '\0') {
        free(trimmed);
        value_free(value);
        runtime_error_raise("number modifier expects a valid number string", 1003, "modifier");
        return value_null();
    }

    free(trimmed);
    value_free(value);
    return value_number(number);
}

static Value builtin_string_modifier_value(Value value) {
    return builtin_string_value(value);
}

typedef struct {
    char *items;
    size_t length;
    size_t capacity;
} StringBuilder;

static void sb_init(StringBuilder *builder) {
    builder->capacity = 128;
    builder->length = 0;
    builder->items = malloc(builder->capacity);
    if (!builder->items) {
        abort();
    }
    builder->items[0] = '\0';
}

static void sb_append_char(StringBuilder *builder, char ch) {
    if (builder->length + 2 > builder->capacity) {
        builder->capacity *= 2;
        char *items = realloc(builder->items, builder->capacity);
        if (!items) {
            abort();
        }
        builder->items = items;
    }
    builder->items[builder->length++] = ch;
    builder->items[builder->length] = '\0';
}

static void sb_append_text(StringBuilder *builder, const char *text) {
    while (*text) {
        sb_append_char(builder, *text);
        text++;
    }
}

static char *sb_take(StringBuilder *builder) {
    char *items = builder->items;
    builder->items = NULL;
    builder->length = 0;
    builder->capacity = 0;
    return items;
}

static void encode_string_literal(StringBuilder *builder, const char *text) {
    sb_append_char(builder, '"');
    while (*text) {
        unsigned char ch = (unsigned char)*text;
        if (ch == '"') {
            sb_append_text(builder, "\\\"");
        } else if (ch == '\\') {
            sb_append_text(builder, "\\\\");
        } else if (ch == '\n') {
            sb_append_text(builder, "\\n");
        } else if (ch == '\t') {
            sb_append_text(builder, "\\t");
        } else if (ch == '\r') {
            sb_append_text(builder, "\\r");
        } else {
            sb_append_char(builder, (char)ch);
        }
        text++;
    }
    sb_append_char(builder, '"');
}

static int encode_value_to_builder(StringBuilder *builder, Value value);

static Value builtin_string_value(Value value) {
    char buffer[128];
    StringBuilder builder;
    int used_builder = 0;

    switch (value.kind) {
    case VALUE_STRING:
        return value;
    case VALUE_NUMBER:
        snprintf(buffer, sizeof(buffer), "%g", value.as.number);
        value_free(value);
        return value_string(buffer);
    case VALUE_BOOL:
        if (value.as.boolean) {
            value_free(value);
            return value_string("true");
        }
        value_free(value);
        return value_string("false");
    case VALUE_NULL:
        value_free(value);
        return value_string("nothing");
    case VALUE_UNKNOWN:
        value_free(value);
        return value_string("unknown");
    case VALUE_ARRAY:
    case VALUE_RECORD:
        sb_init(&builder);
        used_builder = 1;
        if (!encode_value_to_builder(&builder, value)) {
            free(builder.items);
            value_free(value);
            return value_null();
        }
        break;
    case VALUE_DATETIME:
        if (value.as.datetime.time_only) {
            if (value.as.datetime.precision == PREC_HOUR) {
                snprintf(buffer, sizeof(buffer), "%02d", value.as.datetime.hour);
            } else if (value.as.datetime.precision == PREC_MINUTE) {
                snprintf(buffer, sizeof(buffer), "%02d:%02d",
                         value.as.datetime.hour,
                         value.as.datetime.minute);
            } else {
                snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d",
                         value.as.datetime.hour,
                         value.as.datetime.minute,
                         value.as.datetime.second);
            }
        } else if (value.as.datetime.precision == PREC_YEAR) {
            snprintf(buffer, sizeof(buffer), "%04d", value.as.datetime.year);
        } else if (value.as.datetime.precision == PREC_MONTH) {
            snprintf(buffer, sizeof(buffer), "%04d-%02d",
                     value.as.datetime.year,
                     value.as.datetime.month);
        } else if (value.as.datetime.precision == PREC_DAY) {
            snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
                     value.as.datetime.year,
                     value.as.datetime.month,
                     value.as.datetime.day);
        } else if (value.as.datetime.precision == PREC_HOUR) {
            snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d",
                     value.as.datetime.year,
                     value.as.datetime.month,
                     value.as.datetime.day,
                     value.as.datetime.hour);
        } else if (value.as.datetime.precision == PREC_MINUTE) {
            snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d",
                     value.as.datetime.year,
                     value.as.datetime.month,
                     value.as.datetime.day,
                     value.as.datetime.hour,
                     value.as.datetime.minute);
        } else {
            snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                     value.as.datetime.year,
                     value.as.datetime.month,
                     value.as.datetime.day,
                     value.as.datetime.hour,
                     value.as.datetime.minute,
                     value.as.datetime.second);
        }
        value_free(value);
        return value_string(buffer);
    case VALUE_DURATION:
        value_free(value);
        return value_string("{duration}");
    case VALUE_MONEY: {
        long long cents = value.as.cents;
        long long abs_cents = cents < 0 ? -cents : cents;
        snprintf(buffer,
                 sizeof(buffer),
                 cents < 0 ? "-%lld.%02lld" : "%lld.%02lld",
                 abs_cents / 100,
                 abs_cents % 100);
        value_free(value);
        return value_string(buffer);
    }
    case VALUE_FILE:
    {
        Value result = value_string(value.as.file_path);
        value_free(value);
        return result;
    }
    case VALUE_DIR:
    {
        Value result = value_string(value.as.dir_path);
        value_free(value);
        return result;
    }
    case VALUE_POSTGRES_CONNECTION:
        value_free(value);
        return value_string("<postgres_connection>");
    case VALUE_SQLITE_CONNECTION:
        value_free(value);
        return value_string("<sqlite_connection>");
    }

    if (!used_builder) {
        value_free(value);
        runtime_error_raise("string conversion failed", 1003, "modifier");
        return value_null();
    }

    char *text = sb_take(&builder);
    Value result = value_string(text);
    free(text);
    value_free(value);
    return result;
}

static int encode_value_to_builder(StringBuilder *builder, Value value) {
    char number[64];
    switch (value.kind) {
    case VALUE_NULL:
        sb_append_text(builder, "nothing");
        return 1;
    case VALUE_UNKNOWN:
        sb_append_text(builder, "unknown");
        return 1;
    case VALUE_NUMBER:
        snprintf(number, sizeof(number), "%.17g", value.as.number);
        sb_append_text(builder, number);
        return 1;
    case VALUE_STRING:
        encode_string_literal(builder, value.as.string);
        return 1;
    case VALUE_BOOL:
        sb_append_text(builder, value.as.boolean ? "true" : "false");
        return 1;
    case VALUE_ARRAY:
        sb_append_char(builder, '[');
        for (size_t i = 0; i < value.as.array.count; i++) {
            if (i > 0) {
                sb_append_char(builder, ',');
            }
            if (!encode_value_to_builder(builder, value.as.array.items[i])) {
                return 0;
            }
        }
        sb_append_char(builder, ']');
        return 1;
    case VALUE_RECORD:
        sb_append_char(builder, '{');
        for (size_t i = 0; i < value.as.record.count; i++) {
            if (i > 0) {
                sb_append_char(builder, ',');
            }
            encode_string_literal(builder, value.as.record.fields[i].name);
            sb_append_char(builder, ':');
            if (!encode_value_to_builder(builder, *value.as.record.fields[i].value)) {
                return 0;
            }
        }
        sb_append_char(builder, '}');
        return 1;
    case VALUE_DATETIME:
    case VALUE_DURATION:
    case VALUE_MONEY:
    case VALUE_FILE:
    case VALUE_DIR:
    case VALUE_POSTGRES_CONNECTION:
    case VALUE_SQLITE_CONNECTION:
        runtime_error_raise("encode supports numbers, strings, booleans, nothing, unknown, arrays, and records",
                            1003,
                            "serialization");
        return 0;
    }
    return 0;
}

static Value builtin_encode_value(Value value) {
    StringBuilder builder;
    sb_init(&builder);
    if (!encode_value_to_builder(&builder, value)) {
        free(builder.items);
        value_free(value);
        return value_null();
    }
    char *text = sb_take(&builder);
    Value result = value_string(text);
    free(text);
    value_free(value);
    return result;
}

static Value builtin_quote_value(Value value) {
    char buffer[128];
    Value text;
    switch (value.kind) {
    case VALUE_STRING:
        text = value;
        break;
    case VALUE_NUMBER:
        snprintf(buffer, sizeof(buffer), "%g", value.as.number);
        value_free(value);
        text = value_string(buffer);
        break;
    case VALUE_BOOL:
        snprintf(buffer, sizeof(buffer), "%s", value.as.boolean ? "true" : "false");
        value_free(value);
        text = value_string(buffer);
        break;
    case VALUE_NULL:
        value_free(value);
        text = value_string("nothing");
        break;
    case VALUE_UNKNOWN:
        value_free(value);
        text = value_string("unknown");
        break;
    default:
        value_free(value);
        runtime_error_raise("quote expects a scalar value", 1003, "source generation");
        return value_null();
    }

    StringBuilder builder;
    sb_init(&builder);
    encode_string_literal(&builder, text.as.string);
    char *literal = sb_take(&builder);
    Value result = value_string(literal);
    free(literal);
    value_free(text);
    return result;
}

typedef struct {
    const char *text;
    size_t pos;
    int ok;
    int json_only;
    char message[160];
} DecodeParser;

static void decode_error(DecodeParser *parser, const char *message) {
    if (parser->ok) {
        snprintf(parser->message, sizeof(parser->message), "%s at byte %zu", message, parser->pos);
    }
    parser->ok = 0;
}

static void decode_skip_ws(DecodeParser *parser) {
    while (isspace((unsigned char)parser->text[parser->pos])) {
        parser->pos++;
    }
}

static int decode_match_text(DecodeParser *parser, const char *text) {
    size_t len = strlen(text);
    if (strncmp(parser->text + parser->pos, text, len) != 0) {
        return 0;
    }
    parser->pos += len;
    return 1;
}

static Value decode_parse_value(DecodeParser *parser);

static int decode_hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int decode_unicode_escape(DecodeParser *parser, unsigned *out) {
    unsigned value = 0;
    for (int i = 0; i < 4; i++) {
        int digit = decode_hex_digit(parser->text[parser->pos++]);
        if (digit < 0) {
            decode_error(parser, "invalid unicode escape");
            return 0;
        }
        value = value * 16u + (unsigned)digit;
    }
    *out = value;
    return 1;
}

static void decode_append_utf8(StringBuilder *builder, unsigned codepoint) {
    if (codepoint <= 0x7f) {
        sb_append_char(builder, (char)codepoint);
    } else if (codepoint <= 0x7ff) {
        sb_append_char(builder, (char)(0xc0u | (codepoint >> 6)));
        sb_append_char(builder, (char)(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint <= 0xffff) {
        sb_append_char(builder, (char)(0xe0u | (codepoint >> 12)));
        sb_append_char(builder, (char)(0x80u | ((codepoint >> 6) & 0x3fu)));
        sb_append_char(builder, (char)(0x80u | (codepoint & 0x3fu)));
    } else {
        sb_append_char(builder, (char)(0xf0u | (codepoint >> 18)));
        sb_append_char(builder, (char)(0x80u | ((codepoint >> 12) & 0x3fu)));
        sb_append_char(builder, (char)(0x80u | ((codepoint >> 6) & 0x3fu)));
        sb_append_char(builder, (char)(0x80u | (codepoint & 0x3fu)));
    }
}

static Value decode_parse_string(DecodeParser *parser) {
    if (parser->text[parser->pos] != '"') {
        decode_error(parser, "expected string");
        return value_null();
    }
    parser->pos++;

    StringBuilder builder;
    sb_init(&builder);
    while (parser->text[parser->pos]) {
        char ch = parser->text[parser->pos++];
        if (ch == '"') {
            char *text = sb_take(&builder);
            Value result = value_string(text);
            free(text);
            return result;
        }
        if (ch == '\\') {
            char esc = parser->text[parser->pos++];
            if (esc == '\0') {
                free(builder.items);
                decode_error(parser, "unterminated escape sequence");
                return value_null();
            }
            if (esc == 'n') {
                sb_append_char(&builder, '\n');
            } else if (esc == 't') {
                sb_append_char(&builder, '\t');
            } else if (esc == 'r') {
                sb_append_char(&builder, '\r');
            } else if (esc == 'b') {
                sb_append_char(&builder, '\b');
            } else if (esc == 'f') {
                sb_append_char(&builder, '\f');
            } else if (esc == '"' || esc == '\\' || esc == '/') {
                sb_append_char(&builder, esc);
            } else if (esc == 'u') {
                unsigned codepoint = 0;
                if (!decode_unicode_escape(parser, &codepoint)) {
                    free(builder.items);
                    return value_null();
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (parser->text[parser->pos] != '\\' ||
                        parser->text[parser->pos + 1] != 'u') {
                        free(builder.items);
                        decode_error(parser, "invalid unicode surrogate pair");
                        return value_null();
                    }
                    parser->pos += 2;
                    unsigned low = 0;
                    if (!decode_unicode_escape(parser, &low) ||
                        low < 0xdc00 || low > 0xdfff) {
                        free(builder.items);
                        if (parser->ok) {
                            decode_error(parser, "invalid unicode surrogate pair");
                        }
                        return value_null();
                    }
                    codepoint = 0x10000u +
                        ((codepoint - 0xd800u) << 10) +
                        (low - 0xdc00u);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    free(builder.items);
                    decode_error(parser, "invalid unicode surrogate pair");
                    return value_null();
                }
                decode_append_utf8(&builder, codepoint);
            } else {
                free(builder.items);
                decode_error(parser, "invalid escape sequence");
                return value_null();
            }
        } else {
            if (parser->json_only && (unsigned char)ch < 0x20) {
                free(builder.items);
                decode_error(parser, "unescaped control character");
                return value_null();
            }
            sb_append_char(&builder, ch);
        }
    }

    free(builder.items);
    decode_error(parser, "unterminated string");
    return value_null();
}

static Value decode_parse_array(DecodeParser *parser) {
    parser->pos++;
    Value *items = NULL;
    size_t count = 0;
    decode_skip_ws(parser);
    if (parser->text[parser->pos] == ']') {
        parser->pos++;
        return value_array(NULL, 0);
    }

    while (parser->ok) {
        Value item = decode_parse_value(parser);
        if (!parser->ok) {
            value_free(item);
            break;
        }
        Value *next = realloc(items, sizeof(Value) * (count + 1));
        if (!next) {
            abort();
        }
        items = next;
        items[count++] = item;

        decode_skip_ws(parser);
        if (parser->text[parser->pos] == ',') {
            parser->pos++;
            decode_skip_ws(parser);
            continue;
        }
        if (parser->text[parser->pos] == ']') {
            parser->pos++;
            return value_array(items, count);
        }
        decode_error(parser, "expected ',' or ']'");
        break;
    }

    for (size_t i = 0; i < count; i++) {
        value_free(items[i]);
    }
    free(items);
    return value_null();
}

static Value decode_parse_record(DecodeParser *parser) {
    parser->pos++;
    RecordField *fields = NULL;
    size_t count = 0;
    decode_skip_ws(parser);
    if (parser->text[parser->pos] == '}') {
        parser->pos++;
        return value_record(NULL, 0);
    }

    while (parser->ok) {
        Value key = decode_parse_string(parser);
        if (!parser->ok) {
            value_free(key);
            break;
        }
        decode_skip_ws(parser);
        if (parser->text[parser->pos] != ':') {
            value_free(key);
            decode_error(parser, "expected ':'");
            break;
        }
        parser->pos++;
        Value value = decode_parse_value(parser);
        if (!parser->ok) {
            value_free(key);
            value_free(value);
            break;
        }

        RecordField *next = realloc(fields, sizeof(RecordField) * (count + 1));
        if (!next) {
            abort();
        }
        fields = next;
        fields[count].name = copy_string(key.as.string);
        fields[count].value = malloc(sizeof(Value));
        if (!fields[count].value) {
            abort();
        }
        *fields[count].value = value;
        count++;
        value_free(key);

        decode_skip_ws(parser);
        if (parser->text[parser->pos] == ',') {
            parser->pos++;
            decode_skip_ws(parser);
            continue;
        }
        if (parser->text[parser->pos] == '}') {
            parser->pos++;
            return value_record(fields, count);
        }
        decode_error(parser, "expected ',' or '}'");
        break;
    }

    for (size_t i = 0; i < count; i++) {
        free(fields[i].name);
        value_free(*fields[i].value);
        free(fields[i].value);
    }
    free(fields);
    return value_null();
}

static Value decode_parse_number(DecodeParser *parser) {
    if (parser->json_only) {
        size_t start_pos = parser->pos;
        if (parser->text[parser->pos] == '-') {
            parser->pos++;
        }
        if (parser->text[parser->pos] == '0') {
            parser->pos++;
            if (isdigit((unsigned char)parser->text[parser->pos])) {
                decode_error(parser, "invalid JSON number");
                return value_null();
            }
        } else if (parser->text[parser->pos] >= '1' &&
                   parser->text[parser->pos] <= '9') {
            while (isdigit((unsigned char)parser->text[parser->pos])) {
                parser->pos++;
            }
        } else {
            decode_error(parser, "invalid JSON number");
            return value_null();
        }
        if (parser->text[parser->pos] == '.') {
            parser->pos++;
            if (!isdigit((unsigned char)parser->text[parser->pos])) {
                decode_error(parser, "invalid JSON number");
                return value_null();
            }
            while (isdigit((unsigned char)parser->text[parser->pos])) {
                parser->pos++;
            }
        }
        if (parser->text[parser->pos] == 'e' ||
            parser->text[parser->pos] == 'E') {
            parser->pos++;
            if (parser->text[parser->pos] == '+' ||
                parser->text[parser->pos] == '-') {
                parser->pos++;
            }
            if (!isdigit((unsigned char)parser->text[parser->pos])) {
                decode_error(parser, "invalid JSON number");
                return value_null();
            }
            while (isdigit((unsigned char)parser->text[parser->pos])) {
                parser->pos++;
            }
        }

        size_t length = parser->pos - start_pos;
        char *number_text = malloc(length + 1);
        if (!number_text) {
            abort();
        }
        memcpy(number_text, parser->text + start_pos, length);
        number_text[length] = '\0';
        errno = 0;
        char *end = NULL;
        double number = strtod(number_text, &end);
        int valid = errno != ERANGE && end && *end == '\0';
        free(number_text);
        if (!valid) {
            decode_error(parser, "invalid JSON number");
            return value_null();
        }
        return value_number(number);
    }

    const char *start = parser->text + parser->pos;
    char *end = NULL;
    errno = 0;
    double number = strtod(start, &end);
    if (end == start || errno == ERANGE) {
        decode_error(parser, "invalid number");
        return value_null();
    }
    parser->pos += (size_t)(end - start);
    return value_number(number);
}

static Value decode_parse_value(DecodeParser *parser) {
    decode_skip_ws(parser);
    char ch = parser->text[parser->pos];
    if (ch == '"') {
        return decode_parse_string(parser);
    }
    if (ch == '[') {
        return decode_parse_array(parser);
    }
    if (ch == '{') {
        return decode_parse_record(parser);
    }
    if (decode_match_text(parser, "true")) {
        return value_bool(1);
    }
    if (decode_match_text(parser, "false")) {
        return value_bool(0);
    }
    if (!parser->json_only && decode_match_text(parser, "nothing")) {
        return value_null();
    }
    if (decode_match_text(parser, "null")) {
        return value_null();
    }
    if (!parser->json_only && decode_match_text(parser, "unknown")) {
        return value_unknown();
    }
    if (ch == '-' ||
        (!parser->json_only && (ch == '+' || ch == '.')) ||
        isdigit((unsigned char)ch)) {
        return decode_parse_number(parser);
    }
    decode_error(parser, "expected value");
    return value_null();
}

static Value builtin_decode_text(Value text) {
    if (text.kind != VALUE_STRING) {
        value_free(text);
        runtime_error_raise("decode expects a string", 1003, "serialization");
        return value_null();
    }

    DecodeParser parser = {0};
    parser.text = text.as.string;
    parser.ok = 1;
    Value result = decode_parse_value(&parser);
    if (parser.ok) {
        decode_skip_ws(&parser);
        if (parser.text[parser.pos] != '\0') {
            value_free(result);
            decode_error(&parser, "unexpected trailing text");
            result = value_null();
        }
    }
    if (!parser.ok) {
        char message[220];
        snprintf(message, sizeof(message), "decode error: %s", parser.message);
        runtime_error_raise(message, 1003, "serialization");
        value_free(result);
        value_free(text);
        return value_null();
    }
    value_free(text);
    return result;
}

#if HAVE_LIBCURL
#define WEBCLIENT_ERROR_CODE 3001
#define WEBCLIENT_DEFAULT_TIMEOUT_SECONDS 30.0
#define WEBCLIENT_MAX_RESPONSE_SIZE (32u * 1024u * 1024u)

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    int too_large;
} WebclientBuffer;

typedef struct {
    Value headers;
    char *reason;
} WebclientResponseMetadata;

typedef struct {
    char *method;
    char *url;
    char *body;
    int has_body;
    double timeout;
    struct curl_slist *headers;
} WebclientRequest;

static void webclient_raise(const char *message) {
    runtime_error_raise(message, WEBCLIENT_ERROR_CODE, "webclient");
}

static void webclient_buffer_free(WebclientBuffer *buffer) {
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static size_t webclient_write_callback(char *data,
                                       size_t size,
                                       size_t count,
                                       void *userdata) {
    WebclientBuffer *buffer = userdata;
    if (size != 0 && count > SIZE_MAX / size) {
        buffer->too_large = 1;
        return 0;
    }
    size_t bytes = size * count;
    if (bytes > WEBCLIENT_MAX_RESPONSE_SIZE - buffer->length) {
        buffer->too_large = 1;
        return 0;
    }
    size_t needed = buffer->length + bytes + 1;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 4096;
        while (capacity < needed) {
            if (capacity > (WEBCLIENT_MAX_RESPONSE_SIZE + 1) / 2) {
                capacity = WEBCLIENT_MAX_RESPONSE_SIZE + 1;
                break;
            }
            capacity *= 2;
        }
        char *next = realloc(buffer->data, capacity);
        if (!next) {
            abort();
        }
        buffer->data = next;
        buffer->capacity = capacity;
    }
    if (bytes > 0) {
        memcpy(buffer->data + buffer->length, data, bytes);
        buffer->length += bytes;
    }
    buffer->data[buffer->length] = '\0';
    return bytes;
}

static char *webclient_trimmed_copy(const char *start, size_t length) {
    while (length > 0 && isspace((unsigned char)*start)) {
        start++;
        length--;
    }
    while (length > 0 && isspace((unsigned char)start[length - 1])) {
        length--;
    }
    char *copy = malloc(length + 1);
    if (!copy) {
        abort();
    }
    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

static void webclient_reset_response_headers(WebclientResponseMetadata *metadata) {
    value_free(metadata->headers);
    metadata->headers = value_record(NULL, 0);
    free(metadata->reason);
    metadata->reason = copy_string("");
}

static size_t webclient_header_callback(char *data,
                                        size_t size,
                                        size_t count,
                                        void *userdata) {
    WebclientResponseMetadata *metadata = userdata;
    if (size != 0 && count > SIZE_MAX / size) {
        return 0;
    }
    size_t bytes = size * count;
    if (bytes >= 5 && strncmp(data, "HTTP/", 5) == 0) {
        webclient_reset_response_headers(metadata);
        const char *line_end = data + bytes;
        while (line_end > data &&
               (line_end[-1] == '\r' || line_end[-1] == '\n')) {
            line_end--;
        }
        const char *first_space = memchr(data, ' ', (size_t)(line_end - data));
        if (first_space) {
            const char *second_space = memchr(first_space + 1,
                                              ' ',
                                              (size_t)(line_end - first_space - 1));
            if (second_space && second_space + 1 < line_end) {
                free(metadata->reason);
                metadata->reason = webclient_trimmed_copy(
                    second_space + 1,
                    (size_t)(line_end - second_space - 1));
            }
        }
        return bytes;
    }

    char *colon = memchr(data, ':', bytes);
    if (!colon) {
        return bytes;
    }
    size_t name_length = (size_t)(colon - data);
    while (name_length > 0 &&
           isspace((unsigned char)data[name_length - 1])) {
        name_length--;
    }
    if (name_length == 0) {
        return bytes;
    }

    char *name = malloc(name_length + 1);
    if (!name) {
        abort();
    }
    for (size_t i = 0; i < name_length; i++) {
        name[i] = (char)tolower((unsigned char)data[i]);
    }
    name[name_length] = '\0';

    const char *value_start = colon + 1;
    size_t value_length = bytes - (size_t)(value_start - data);
    char *value = webclient_trimmed_copy(value_start, value_length);
    record_set(&metadata->headers, name, value_string(value));
    free(value);
    free(name);
    return bytes;
}

static int webclient_token_char(unsigned char ch) {
    return isalnum(ch) ||
        ch == '!' || ch == '#' || ch == '$' || ch == '%' || ch == '&' ||
        ch == '\'' || ch == '*' || ch == '+' || ch == '-' || ch == '.' ||
        ch == '^' || ch == '_' || ch == '`' || ch == '|' || ch == '~';
}

static int webclient_valid_token(const char *text) {
    if (!text[0]) {
        return 0;
    }
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor;
         cursor++) {
        if (!webclient_token_char(*cursor)) {
            return 0;
        }
    }
    return 1;
}

static char *webclient_upper_method(const char *method) {
    if (!webclient_valid_token(method)) {
        webclient_raise("webclient request method is invalid");
        return NULL;
    }
    size_t length = strlen(method);
    char *upper = malloc(length + 1);
    if (!upper) {
        abort();
    }
    for (size_t i = 0; i < length; i++) {
        upper[i] = (char)toupper((unsigned char)method[i]);
    }
    upper[length] = '\0';
    return upper;
}

static int webclient_url_has_supported_scheme(const char *url) {
    return strncmp(url, "http://", 7) == 0 ||
        strncmp(url, "https://", 8) == 0;
}

static int webclient_append_request_headers(struct curl_slist **headers,
                                            Value record) {
    if (record.kind != VALUE_RECORD) {
        webclient_raise("webclient request headers must be a record");
        return 0;
    }
    for (size_t i = 0; i < record.as.record.count; i++) {
        RecordField *field = &record.as.record.fields[i];
        if (!webclient_valid_token(field->name)) {
            webclient_raise("webclient request header name is invalid");
            return 0;
        }
        if (field->value->kind != VALUE_STRING) {
            webclient_raise("webclient request header values must be strings");
            return 0;
        }
        if (strchr(field->value->as.string, '\r') ||
            strchr(field->value->as.string, '\n')) {
            webclient_raise("webclient request header value is invalid");
            return 0;
        }
        size_t name_length = strlen(field->name);
        size_t value_length = strlen(field->value->as.string);
        char *line = malloc(name_length + value_length + 3);
        if (!line) {
            abort();
        }
        snprintf(line,
                 name_length + value_length + 3,
                 "%s: %s",
                 field->name,
                 field->value->as.string);
        struct curl_slist *next = curl_slist_append(*headers, line);
        free(line);
        if (!next) {
            abort();
        }
        *headers = next;
    }
    return 1;
}

static void webclient_request_free(WebclientRequest *request) {
    free(request->method);
    free(request->url);
    free(request->body);
    curl_slist_free_all(request->headers);
    memset(request, 0, sizeof(*request));
}

static int webclient_validate_url(const char *url) {
    if (!url[0]) {
        webclient_raise("webclient URL must not be empty");
        return 0;
    }
    if (!webclient_url_has_supported_scheme(url)) {
        webclient_raise("webclient URL must use http:// or https://");
        return 0;
    }
    const char *authority = url + (strncmp(url, "https://", 8) == 0 ? 8 : 7);
    if (!authority[0] ||
        authority[0] == '/' ||
        strpbrk(url, " \t\r\n") != NULL) {
        webclient_raise("webclient URL is malformed");
        return 0;
    }
    return 1;
}

static int webclient_decode_json(const char *body, Value *out) {
    DecodeParser parser = {0};
    parser.text = body;
    parser.ok = 1;
    parser.json_only = 1;
    Value result = decode_parse_value(&parser);
    if (parser.ok) {
        decode_skip_ws(&parser);
        if (parser.text[parser.pos] != '\0') {
            value_free(result);
            return 0;
        }
        *out = result;
        return 1;
    }
    value_free(result);
    return 0;
}

static Value webclient_response_value(long status,
                                      WebclientResponseMetadata *metadata,
                                      WebclientBuffer *body) {
    if (memchr(body->data ? body->data : "", '\0', body->length)) {
        webclient_raise("webclient binary responses are not supported");
        return value_null();
    }

    Value response = value_record(NULL, 0);
    record_set(&response, "status", value_number((double)status));
    record_set(&response,
               "reason",
               value_string(metadata->reason ? metadata->reason : ""));
    record_set(&response, "headers", value_copy(metadata->headers));
    record_set(&response, "body", value_string(body->data ? body->data : ""));

    if (body->length > 0) {
        Value json;
        if (webclient_decode_json(body->data, &json)) {
            record_set(&response, "json", json);
        }
    }
    return response;
}

static Value webclient_perform(WebclientRequest *request) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        webclient_raise("could not create libcurl request");
        return value_null();
    }

    WebclientBuffer body = {0};
    WebclientResponseMetadata metadata = {0};
    metadata.headers = value_record(NULL, 0);
    metadata.reason = copy_string("");
    char error_buffer[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(curl, CURLOPT_URL, request->url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    long timeout_ms = (long)ceil(request->timeout * 1000.0);
    if (timeout_ms < 1) {
        timeout_ms = 1;
    }
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    long connect_timeout = timeout_ms;
    if (connect_timeout > 10000L) {
        connect_timeout = 10000L;
    }
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "gBASIC/0.1 webclient");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, webclient_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, webclient_header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &metadata);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request->headers);

    if (strcmp(request->method, "GET") == 0 && !request->has_body) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(request->method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl,
                         CURLOPT_POSTFIELDS,
                         request->has_body ? request->body : "");
        curl_easy_setopt(curl,
                         CURLOPT_POSTFIELDSIZE_LARGE,
                         (curl_off_t)(request->has_body
                             ? strlen(request->body)
                             : 0));
    } else if (strcmp(request->method, "HEAD") == 0) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "HEAD");
    } else {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request->method);
        if (request->has_body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
            curl_easy_setopt(curl,
                             CURLOPT_POSTFIELDSIZE_LARGE,
                             (curl_off_t)strlen(request->body));
        }
    }

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }

    Value response = value_null();
    if (code != CURLE_OK) {
        if (body.too_large) {
            webclient_raise("webclient response exceeds the 32 MiB limit");
        } else {
            char message[1024];
            const char *detail = error_buffer[0]
                ? error_buffer
                : curl_easy_strerror(code);
            snprintf(message, sizeof(message), "webclient request failed: %s", detail);
            webclient_raise(message);
        }
    } else {
        response = webclient_response_value(status, &metadata, &body);
    }

    curl_easy_cleanup(curl);
    webclient_buffer_free(&body);
    value_free(metadata.headers);
    free(metadata.reason);
    return response;
}

static int webclient_request_from_record(Value record, WebclientRequest *request) {
    static const char *allowed[] = {
        "method", "url", "headers", "body", "timeout"
    };
    for (size_t i = 0; i < record.as.record.count; i++) {
        int known = 0;
        for (size_t j = 0; j < sizeof(allowed) / sizeof(allowed[0]); j++) {
            if (strcmp(record.as.record.fields[i].name, allowed[j]) == 0) {
                known = 1;
                break;
            }
        }
        if (!known) {
            char message[256];
            snprintf(message,
                     sizeof(message),
                     "unknown webclient request field: %s",
                     record.as.record.fields[i].name);
            webclient_raise(message);
            return 0;
        }
    }

    RecordField *url = record_find(&record, "url");
    if (!url) {
        webclient_raise("webclient.request requires a url field");
        return 0;
    }
    if (url->value->kind != VALUE_STRING) {
        webclient_raise("webclient request url must be a string");
        return 0;
    }
    if (!webclient_validate_url(url->value->as.string)) {
        return 0;
    }
    request->url = copy_string(url->value->as.string);

    RecordField *method = record_find(&record, "method");
    if (method && method->value->kind != VALUE_STRING) {
        webclient_raise("webclient request method must be a string");
        return 0;
    }
    request->method = webclient_upper_method(
        method ? method->value->as.string : "GET");
    if (!request->method) {
        return 0;
    }

    RecordField *headers = record_find(&record, "headers");
    if (headers &&
        !webclient_append_request_headers(&request->headers, *headers->value)) {
        return 0;
    }

    RecordField *body = record_find(&record, "body");
    if (body) {
        if (body->value->kind != VALUE_STRING) {
            webclient_raise("webclient request body must be a string");
            return 0;
        }
        request->body = copy_string(body->value->as.string);
        request->has_body = 1;
    }

    request->timeout = WEBCLIENT_DEFAULT_TIMEOUT_SECONDS;
    RecordField *timeout = record_find(&record, "timeout");
    if (timeout) {
        if (timeout->value->kind != VALUE_NUMBER ||
            !isfinite(timeout->value->as.number) ||
            timeout->value->as.number <= 0 ||
            timeout->value->as.number > (double)LONG_MAX / 1000.0) {
            webclient_raise("webclient request timeout must be a positive number");
            return 0;
        }
        request->timeout = timeout->value->as.number;
    }
    return 1;
}

static Value webclient_eval_get(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        webclient_raise("webclient.get expects one argument");
        return value_null();
    }
    Value url = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(url);
        return value_null();
    }
    if (url.kind != VALUE_STRING) {
        value_free(url);
        webclient_raise("webclient.get url must be a string");
        return value_null();
    }
    if (!webclient_validate_url(url.as.string)) {
        value_free(url);
        return value_null();
    }

    WebclientRequest request = {0};
    request.method = copy_string("GET");
    request.url = copy_string(url.as.string);
    request.timeout = WEBCLIENT_DEFAULT_TIMEOUT_SECONDS;
    value_free(url);
    Value response = webclient_perform(&request);
    webclient_request_free(&request);
    return response;
}

static Value webclient_eval_post(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        webclient_raise("webclient.post expects two arguments");
        return value_null();
    }
    Value url = eval_expr(expr->as.call.args.items[0]);
    Value body = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(url);
        value_free(body);
        return value_null();
    }
    if (url.kind != VALUE_STRING) {
        value_free(url);
        value_free(body);
        webclient_raise("webclient.post url must be a string");
        return value_null();
    }
    if (body.kind != VALUE_STRING) {
        value_free(url);
        value_free(body);
        webclient_raise("webclient.post body must be a string");
        return value_null();
    }
    if (!webclient_validate_url(url.as.string)) {
        value_free(url);
        value_free(body);
        return value_null();
    }

    WebclientRequest request = {0};
    request.method = copy_string("POST");
    request.url = copy_string(url.as.string);
    request.body = copy_string(body.as.string);
    request.has_body = 1;
    request.timeout = WEBCLIENT_DEFAULT_TIMEOUT_SECONDS;
    value_free(url);
    value_free(body);
    Value response = webclient_perform(&request);
    webclient_request_free(&request);
    return response;
}

static Value webclient_eval_request(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        webclient_raise("webclient.request expects one argument");
        return value_null();
    }
    Value record = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(record);
        return value_null();
    }
    if (record.kind != VALUE_RECORD) {
        value_free(record);
        webclient_raise("webclient.request expects a record");
        return value_null();
    }

    WebclientRequest request = {0};
    if (!webclient_request_from_record(record, &request)) {
        value_free(record);
        webclient_request_free(&request);
        return value_null();
    }
    value_free(record);
    Value response = webclient_perform(&request);
    webclient_request_free(&request);
    return response;
}

static Value webclient_eval_call(AstExpr *expr) {
    if (strcmp(expr->as.call.name, "get") == 0) {
        return webclient_eval_get(expr);
    }
    if (strcmp(expr->as.call.name, "post") == 0) {
        return webclient_eval_post(expr);
    }
    if (strcmp(expr->as.call.name, "request") == 0) {
        return webclient_eval_request(expr);
    }
    char message[256];
    snprintf(message,
             sizeof(message),
             "invalid function call: webclient.%s",
             expr->as.call.name);
    webclient_raise(message);
    return value_null();
}
#endif

#define WEBSERVER_ERROR_CODE 4001
#define WEBSERVER_DEFAULT_TIMEOUT_SECONDS 30.0
#define WEBSERVER_MAX_REQUEST_SIZE (8u * 1024u * 1024u)
#define WEBSERVER_MAX_HEADER_SIZE (64u * 1024u)

struct WebServerClient {
    int fd;
    unsigned long id;
    char *buffer;
    size_t length;
    size_t capacity;
    int waiting_response;
    double deadline;
    char remote_ip[64];
    int remote_port;
};

struct WebServer {
    unsigned long id;
    int listen_fd;
    int port;
    int running;
    int shutdown_requested;
    unsigned long next_request_id;
    WebServerClient *clients;
    size_t client_count;
};

static void webserver_raise(const char *message) {
    runtime_error_raise(message, WEBSERVER_ERROR_CODE, "webserver");
}

static double webserver_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
    }
    return (double)time(NULL);
}

static double webserver_timeout_seconds(void) {
    const char *override = getenv("GBASIC_WEBSERVER_TIMEOUT");
    if (override && override[0]) {
        char *end = NULL;
        double value = strtod(override, &end);
        if (end && *end == '\0' && value > 0.0 && isfinite(value)) {
            return value;
        }
    }
    return WEBSERVER_DEFAULT_TIMEOUT_SECONDS;
}

static int webserver_set_blocking_mode(int fd, int blocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return 0;
    }
    int next = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return fcntl(fd, F_SETFL, next) == 0;
}

static WebServer *webserver_find(unsigned long id) {
    for (size_t i = 0; i < webserver_count; i++) {
        if (webservers[i].id == id) {
            return &webservers[i];
        }
    }
    return NULL;
}

static int webserver_record_id(Value *record, unsigned long *out_id) {
    if (!record || record->kind != VALUE_RECORD) {
        return 0;
    }
    RecordField *field = record_find(record, "_webserver_id");
    int id = 0;
    if (!field || !value_is_integer_number(*field->value)) {
        return 0;
    }
    id = (int)field->value->as.number;
    if (id <= 0) {
        return 0;
    }
    *out_id = (unsigned long)id;
    return 1;
}

static Value *webserver_find_record(unsigned long id, const char **out_name) {
    for (size_t i = 0; i < global_env.count; i++) {
        unsigned long record_id = 0;
        if (webserver_record_id(&global_env.items[i].value, &record_id) &&
            record_id == id) {
            if (out_name) {
                *out_name = global_env.items[i].name;
            }
            return &global_env.items[i].value;
        }
    }
    return NULL;
}

static Value *webserver_field(Value *record, const char *name) {
    RecordField *field = record_find(record, name);
    return field ? field->value : NULL;
}

static void webserver_client_close(WebServer *server, size_t index) {
    close(server->clients[index].fd);
    free(server->clients[index].buffer);
    server->clients[index] = server->clients[server->client_count - 1];
    server->client_count--;
    if (server->client_count == 0) {
        free(server->clients);
        server->clients = NULL;
    } else {
        WebServerClient *clients = realloc(server->clients,
                                           sizeof(WebServerClient) * server->client_count);
        if (clients) {
            server->clients = clients;
        }
    }
}

static void webserver_close_native(WebServer *server) {
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
    while (server->client_count > 0) {
        webserver_client_close(server, server->client_count - 1);
    }
    server->running = 0;
}

static void webserver_set_running(WebServer *server, int running) {
    server->running = running;
    Value *record = webserver_find_record(server->id, NULL);
    if (record) {
        record_set(record, "running", value_bool(running));
    }
}

static int webserver_any_active(void) {
    for (size_t i = 0; i < webserver_count; i++) {
        if (webservers[i].running || webservers[i].client_count > 0) {
            return 1;
        }
    }
    return 0;
}

static int webserver_token_char(unsigned char ch) {
    return isalnum(ch) ||
        ch == '!' || ch == '#' || ch == '$' || ch == '%' || ch == '&' ||
        ch == '\'' || ch == '*' || ch == '+' || ch == '-' || ch == '.' ||
        ch == '^' || ch == '_' || ch == '`' || ch == '|' || ch == '~';
}

static int webserver_valid_header_name(const char *text) {
    if (!text || !text[0]) {
        return 0;
    }
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor;
         cursor++) {
        if (!webserver_token_char(*cursor)) {
            return 0;
        }
    }
    return 1;
}

static int webserver_integer(Value value, int *out) {
    if (!value_is_integer_number(value)) {
        return 0;
    }
    *out = (int)value.as.number;
    return 1;
}

static int webserver_pending_request(WebServer *server, unsigned long id) {
    for (size_t i = 0; i < server->client_count; i++) {
        if (server->clients[i].waiting_response && server->clients[i].id == id) {
            return 1;
        }
    }
    return 0;
}

static int webserver_validate_response_value(WebServer *server,
                                             Value response,
                                             int require_pending) {
    if (response.kind != VALUE_RECORD) {
        webserver_raise("webserver response must be a record");
        return 0;
    }
    RecordField *id_field = record_find(&response, "id");
    int id = 0;
    if (!id_field) {
        webserver_raise("webserver response requires an id field");
        return 0;
    }
    if (!webserver_integer(*id_field->value, &id) || id <= 0) {
        webserver_raise("webserver response id must be a positive integer");
        return 0;
    }
    if (require_pending && !webserver_pending_request(server, (unsigned long)id)) {
        webserver_raise("webserver response id does not match a pending request");
        return 0;
    }
    RecordField *status = record_find(&response, "status");
    if (status) {
        int code = 0;
        if (!webserver_integer(*status->value, &code) || code < 100 || code > 599) {
            webserver_raise("webserver response status must be an integer HTTP status");
            return 0;
        }
    }
    RecordField *headers = record_find(&response, "headers");
    if (headers) {
        if (headers->value->kind != VALUE_RECORD) {
            webserver_raise("webserver response headers must be a record");
            return 0;
        }
        for (size_t i = 0; i < headers->value->as.record.count; i++) {
            RecordField *field = &headers->value->as.record.fields[i];
            if (!webserver_valid_header_name(field->name)) {
                webserver_raise("webserver response header name is invalid");
                return 0;
            }
            if (field->value->kind != VALUE_STRING) {
                webserver_raise("webserver response header values must be strings");
                return 0;
            }
            if (strchr(field->value->as.string, '\r') ||
                strchr(field->value->as.string, '\n')) {
                webserver_raise("webserver response header value is invalid");
                return 0;
            }
        }
    }
    RecordField *cookies = record_find(&response, "cookies");
    if (cookies) {
        if (cookies->value->kind != VALUE_ARRAY) {
            webserver_raise("webserver response cookies must be an array");
            return 0;
        }
        for (size_t i = 0; i < cookies->value->as.array.count; i++) {
            Value *cookie = &cookies->value->as.array.items[i];
            if (cookie->kind != VALUE_STRING) {
                webserver_raise("webserver response cookie values must be strings");
                return 0;
            }
            if (strchr(cookie->as.string, '\r') ||
                strchr(cookie->as.string, '\n')) {
                webserver_raise("webserver response cookie value is invalid");
                return 0;
            }
        }
    }
    RecordField *body = record_find(&response, "body");
    if (body && body->value->kind != VALUE_STRING) {
        webserver_raise("webserver response body must be a string");
        return 0;
    }
    return 1;
}

static int webserver_validate_response_append(AstExpr *target, Value item) {
    if (!target || target->kind != AST_EXPR_FIELD ||
        strcmp(target->as.field.field, "responses") != 0) {
        return 1;
    }
    Value *record = resolve_lvalue_ref(target->as.field.object);
    unsigned long id = 0;
    if (!webserver_record_id(record, &id)) {
        return 1;
    }
    WebServer *server = webserver_find(id);
    if (!server) {
        webserver_raise("webserver response target is closed");
        return 0;
    }
    return webserver_validate_response_value(server, item, server->client_count > 0);
}

static void webserver_array_remove(Value *array, size_t index) {
    value_free(array->as.array.items[index]);
    for (size_t i = index + 1; i < array->as.array.count; i++) {
        array->as.array.items[i - 1] = array->as.array.items[i];
    }
    array->as.array.count--;
    if (array->as.array.count == 0) {
        free(array->as.array.items);
        array->as.array.items = NULL;
    } else {
        Value *items = realloc(array->as.array.items,
                               sizeof(Value) * array->as.array.count);
        if (items) {
            array->as.array.items = items;
        }
    }
}

static char *webserver_percent_decode(const char *text, size_t length) {
    char *decoded = malloc(length + 1);
    if (!decoded) {
        abort();
    }
    size_t out = 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '%' && i + 2 < length &&
            isxdigit((unsigned char)text[i + 1]) &&
            isxdigit((unsigned char)text[i + 2])) {
            char hex[3] = {text[i + 1], text[i + 2], '\0'};
            decoded[out++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (text[i] == '+') {
            decoded[out++] = ' ';
        } else {
            decoded[out++] = text[i];
        }
    }
    decoded[out] = '\0';
    return decoded;
}

static Value webserver_query_record(const char *query) {
    Value record = value_record(NULL, 0);
    const char *cursor = query;
    while (*cursor) {
        const char *amp = strchr(cursor, '&');
        size_t length = amp ? (size_t)(amp - cursor) : strlen(cursor);
        const char *eq = memchr(cursor, '=', length);
        size_t name_length = eq ? (size_t)(eq - cursor) : length;
        size_t value_length = eq ? length - name_length - 1 : 0;
        if (name_length > 0) {
            char *name = webserver_percent_decode(cursor, name_length);
            char *value = webserver_percent_decode(eq ? eq + 1 : "", value_length);
            record_set(&record, name, value_string(value));
            free(name);
            free(value);
        }
        if (!amp) {
            break;
        }
        cursor = amp + 1;
    }
    return record;
}

static char *webserver_trim_copy(const char *start, size_t length) {
    while (length > 0 && isspace((unsigned char)*start)) {
        start++;
        length--;
    }
    while (length > 0 && isspace((unsigned char)start[length - 1])) {
        length--;
    }
    char *copy = malloc(length + 1);
    if (!copy) {
        abort();
    }
    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

static Value webserver_cookie_record(Value *headers) {
    Value record = value_record(NULL, 0);
    RecordField *cookie_header = headers && headers->kind == VALUE_RECORD ?
        record_find(headers, "cookie") : NULL;
    if (!cookie_header || cookie_header->value->kind != VALUE_STRING) {
        return record;
    }

    const char *cursor = cookie_header->value->as.string;
    while (*cursor) {
        const char *semicolon = strchr(cursor, ';');
        size_t length = semicolon ? (size_t)(semicolon - cursor) : strlen(cursor);
        const char *eq = memchr(cursor, '=', length);
        if (eq) {
            size_t name_length = (size_t)(eq - cursor);
            size_t value_length = length - name_length - 1;
            char *name = webserver_trim_copy(cursor, name_length);
            char *value = webserver_trim_copy(eq + 1, value_length);
            if (name[0]) {
                record_set(&record, name, value_string(value));
            }
            free(name);
            free(value);
        }
        if (!semicolon) {
            break;
        }
        cursor = semicolon + 1;
    }

    return record;
}

static int webserver_decode_json(const char *body, Value *out) {
    DecodeParser parser = {0};
    parser.text = body;
    parser.ok = 1;
    parser.json_only = 1;
    Value result = decode_parse_value(&parser);
    if (parser.ok) {
        decode_skip_ws(&parser);
        if (parser.text[parser.pos] == '\0') {
            *out = result;
            return 1;
        }
    }
    value_free(result);
    return 0;
}

static char *webserver_timestamp(void) {
    time_t now = time(NULL);
    struct tm value;
    char buffer[32] = "";
    if (gmtime_r(&now, &value)) {
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &value);
    }
    return copy_string(buffer);
}

static Value webserver_make_request(WebServerClient *client,
                                    const char *method,
                                    const char *path,
                                    const char *query,
                                    Value headers,
                                    const char *body) {
    Value request = value_record(NULL, 0);
    record_set(&request, "id", value_number((double)client->id));
    record_set(&request, "method", value_string(method));
    record_set(&request, "path", value_string(path));
    record_set(&request, "query", webserver_query_record(query));
    record_set(&request, "headers", headers);
    record_set(&request, "cookies", webserver_cookie_record(webserver_field(&request, "headers")));
    record_set(&request, "body", value_string(body));
    if (body[0]) {
        Value json;
        if (webserver_decode_json(body, &json)) {
            record_set(&request, "json", json);
        }
    }
    record_set(&request, "remote_ip", value_string(client->remote_ip));
    record_set(&request, "remote_port", value_number((double)client->remote_port));
    char *timestamp = webserver_timestamp();
    record_set(&request, "timestamp", value_string(timestamp));
    free(timestamp);
    return request;
}

static int webserver_parse_request(WebServer *server, WebServerClient *client) {
    char *end = strstr(client->buffer, "\r\n\r\n");
    size_t delimiter = 4;
    if (!end) {
        end = strstr(client->buffer, "\n\n");
        delimiter = 2;
    }
    if (!end) {
        return client->length > WEBSERVER_MAX_HEADER_SIZE ? -413 : 0;
    }
    size_t header_length = (size_t)(end - client->buffer);
    if (header_length > WEBSERVER_MAX_HEADER_SIZE) {
        return -413;
    }
    char *text = malloc(header_length + 1);
    if (!text) {
        abort();
    }
    memcpy(text, client->buffer, header_length);
    text[header_length] = '\0';
    char *first_end = strpbrk(text, "\r\n");
    if (!first_end) {
        free(text);
        return -400;
    }
    *first_end = '\0';
    char *method = strtok(text, " ");
    char *target = strtok(NULL, " ");
    char *version = strtok(NULL, " ");
    if (!method || !target || !version || strncmp(version, "HTTP/", 5) != 0) {
        free(text);
        return -400;
    }
    for (char *p = method; *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
    char *question = strchr(target, '?');
    char *query = "";
    if (question) {
        *question = '\0';
        query = question + 1;
    }

    Value headers = value_record(NULL, 0);
    size_t content_length = 0;
    char *cursor = first_end + 1;
    while (*cursor) {
        while (*cursor == '\r' || *cursor == '\n') {
            cursor++;
        }
        if (!*cursor) {
            break;
        }
        char *next = strpbrk(cursor, "\r\n");
        size_t line_length = next ? (size_t)(next - cursor) : strlen(cursor);
        char *colon = memchr(cursor, ':', line_length);
        if (colon) {
            size_t name_length = (size_t)(colon - cursor);
            char *name = webserver_trim_copy(cursor, name_length);
            for (char *p = name; *p; p++) {
                *p = (char)tolower((unsigned char)*p);
            }
            char *value = webserver_trim_copy(colon + 1,
                                              line_length - (size_t)(colon + 1 - cursor));
            record_set(&headers, name, value_string(value));
            if (strcmp(name, "content-length") == 0) {
                char *parse_end = NULL;
                unsigned long parsed = strtoul(value, &parse_end, 10);
                if (!parse_end || *parse_end != '\0') {
                    free(name);
                    free(value);
                    value_free(headers);
                    free(text);
                    return -400;
                }
                content_length = (size_t)parsed;
            }
            free(name);
            free(value);
        }
        if (!next) {
            break;
        }
        cursor = next + 1;
    }
    if (content_length > WEBSERVER_MAX_REQUEST_SIZE) {
        value_free(headers);
        free(text);
        return -413;
    }
    size_t body_offset = header_length + delimiter;
    if (client->length < body_offset + content_length) {
        value_free(headers);
        free(text);
        return 0;
    }
    const char *body_start = client->buffer + body_offset;
    if (memchr(body_start, '\0', content_length)) {
        value_free(headers);
        free(text);
        return -415;
    }
    char *body = malloc(content_length + 1);
    if (!body) {
        abort();
    }
    memcpy(body, body_start, content_length);
    body[content_length] = '\0';

    client->id = server->next_request_id++;
    client->waiting_response = 1;
    client->deadline = webserver_now() + webserver_timeout_seconds();
    Value request = webserver_make_request(client, method, target, query, headers, body);
    free(body);
    free(text);

    const char *server_name = NULL;
    Value *server_record = webserver_find_record(server->id, &server_name);
    Value *requests = server_record ? webserver_field(server_record, "requests") : NULL;
    if (!requests || requests->kind != VALUE_ARRAY || !server_name) {
        value_free(request);
        return -500;
    }
    Value ignored = append_to_array_ref(requests, request, 0);
    value_free(ignored);
    char watch_path[256];
    snprintf(watch_path, sizeof(watch_path), "%s.requests", server_name);
    if (!watcher_trigger_change(watch_path)) {
        return -500;
    }
    return 1;
}

static const char *webserver_reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default: return "OK";
    }
}

static void webserver_write_all(int fd, const char *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
#ifdef MSG_NOSIGNAL
        ssize_t written = send(fd, data + offset, length - offset, MSG_NOSIGNAL);
#else
        ssize_t written = send(fd, data + offset, length - offset, 0);
#endif
        if (written <= 0) {
            return;
        }
        offset += (size_t)written;
    }
}

static void webserver_send(WebServerClient *client,
                           int status,
                           Value *headers,
                           Value *cookies,
                           const char *body) {
    const char *payload = body ? body : "";
    webserver_set_blocking_mode(client->fd, 1);
    char prefix[256];
    int prefix_length = snprintf(prefix,
                                 sizeof(prefix),
                                 "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nConnection: close\r\n",
                                 status,
                                 webserver_reason(status),
                                 strlen(payload));
    if (prefix_length > 0) {
        webserver_write_all(client->fd, prefix, (size_t)prefix_length);
    }
    int content_type = 0;
    if (headers && headers->kind == VALUE_RECORD) {
        for (size_t i = 0; i < headers->as.record.count; i++) {
            RecordField *field = &headers->as.record.fields[i];
            if (string_equal_caseless(field->name, "content-length") ||
                string_equal_caseless(field->name, "connection")) {
                continue;
            }
            if (string_equal_caseless(field->name, "content-type")) {
                content_type = 1;
            }
            size_t line_length = strlen(field->name) +
                strlen(field->value->as.string) + 5;
            char *line = malloc(line_length);
            if (!line) {
                abort();
            }
            snprintf(line, line_length, "%s: %s\r\n",
                     field->name, field->value->as.string);
            webserver_write_all(client->fd, line, strlen(line));
            free(line);
        }
    }
    if (!content_type) {
        webserver_write_all(client->fd,
                            "Content-Type: text/plain\r\n",
                            strlen("Content-Type: text/plain\r\n"));
    }
    if (cookies && cookies->kind == VALUE_ARRAY) {
        for (size_t i = 0; i < cookies->as.array.count; i++) {
            Value *cookie = &cookies->as.array.items[i];
            size_t line_length = strlen(cookie->as.string) +
                strlen("Set-Cookie: \r\n") + 1;
            char *line = malloc(line_length);
            if (!line) {
                abort();
            }
            snprintf(line, line_length, "Set-Cookie: %s\r\n", cookie->as.string);
            webserver_write_all(client->fd, line, strlen(line));
            free(line);
        }
    }
    webserver_write_all(client->fd, "\r\n", 2);
    if (payload[0]) {
        webserver_write_all(client->fd, payload, strlen(payload));
    }
}

static WebServerClient *webserver_find_client(WebServer *server,
                                              unsigned long id,
                                              size_t *out_index) {
    for (size_t i = 0; i < server->client_count; i++) {
        if (server->clients[i].waiting_response && server->clients[i].id == id) {
            *out_index = i;
            return &server->clients[i];
        }
    }
    return NULL;
}

static void webserver_process_responses(WebServer *server) {
    Value *record = webserver_find_record(server->id, NULL);
    Value *responses = record ? webserver_field(record, "responses") : NULL;
    if (!responses || responses->kind != VALUE_ARRAY) {
        return;
    }
    while (responses->as.array.count > 0) {
        Value response = responses->as.array.items[0];
        if (!webserver_validate_response_value(server, response, 1)) {
            return;
        }
        int id = 0;
        webserver_integer(*record_find(&response, "id")->value, &id);
        size_t client_index = 0;
        WebServerClient *client = webserver_find_client(server,
                                                        (unsigned long)id,
                                                        &client_index);
        if (!client) {
            webserver_raise("webserver response id does not match a pending request");
            return;
        }
        RecordField *status = record_find(&response, "status");
        RecordField *headers = record_find(&response, "headers");
        RecordField *cookies = record_find(&response, "cookies");
        RecordField *body = record_find(&response, "body");
        webserver_send(client,
                       status ? (int)status->value->as.number : 200,
                       headers ? headers->value : NULL,
                       cookies ? cookies->value : NULL,
                       body ? body->value->as.string : "");
        webserver_client_close(server, client_index);
        webserver_array_remove(responses, 0);
    }
}

static void webserver_send_error(WebServer *server,
                                 size_t client_index,
                                 int status,
                                 const char *body) {
    webserver_send(&server->clients[client_index], status, NULL, NULL, body);
    webserver_client_close(server, client_index);
}

static void webserver_accept(WebServer *server) {
    for (;;) {
        struct sockaddr_in address;
        socklen_t length = sizeof(address);
        int fd = accept(server->listen_fd, (struct sockaddr *)&address, &length);
        if (fd < 0) {
            return;
        }
        webserver_set_blocking_mode(fd, 0);
        WebServerClient *clients = realloc(server->clients,
                                           sizeof(WebServerClient) * (server->client_count + 1));
        if (!clients) {
            abort();
        }
        server->clients = clients;
        WebServerClient *client = &server->clients[server->client_count++];
        memset(client, 0, sizeof(*client));
        client->fd = fd;
        const char *ip = inet_ntoa(address.sin_addr);
        snprintf(client->remote_ip, sizeof(client->remote_ip), "%s", ip ? ip : "");
        client->remote_port = ntohs(address.sin_port);
    }
}

static void webserver_read_client(WebServer *server, size_t index) {
    WebServerClient *client = &server->clients[index];
    char chunk[4096];
    ssize_t count = recv(client->fd, chunk, sizeof(chunk), 0);
    if (count <= 0) {
        if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            webserver_client_close(server, index);
        }
        return;
    }
    if (client->length + (size_t)count >
        WEBSERVER_MAX_REQUEST_SIZE + WEBSERVER_MAX_HEADER_SIZE) {
        webserver_send_error(server, index, 413, "Payload Too Large");
        return;
    }
    size_t needed = client->length + (size_t)count + 1;
    if (needed > client->capacity) {
        size_t capacity = client->capacity ? client->capacity : 4096;
        while (capacity < needed) {
            capacity *= 2;
        }
        char *buffer = realloc(client->buffer, capacity);
        if (!buffer) {
            abort();
        }
        client->buffer = buffer;
        client->capacity = capacity;
    }
    memcpy(client->buffer + client->length, chunk, (size_t)count);
    client->length += (size_t)count;
    client->buffer[client->length] = '\0';
    int parsed = webserver_parse_request(server, client);
    if (parsed < 0) {
        int status = -parsed;
        webserver_send_error(server, index, status, webserver_reason(status));
    }
}

static void webserver_sync_shutdown(WebServer *server) {
    Value *record = webserver_find_record(server->id, NULL);
    Value *running = record ? webserver_field(record, "running") : NULL;
    if (!record || (running && running->kind == VALUE_BOOL && !running->as.boolean)) {
        server->shutdown_requested = 1;
    }
}

static void webserver_finish_shutdown(WebServer *server) {
    if (!server->shutdown_requested) {
        return;
    }
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
    while (server->client_count > 0) {
        webserver_send_error(server, 0, 503, "Service Unavailable");
    }
    webserver_set_running(server, 0);
}

static void webserver_check_timeouts(WebServer *server) {
    double now = webserver_now();
    size_t i = 0;
    while (i < server->client_count) {
        if (server->clients[i].waiting_response && now >= server->clients[i].deadline) {
            webserver_send_error(server, i, 504, "Gateway Timeout");
        } else {
            i++;
        }
    }
}

static int webserver_run_event_loop(void) {
    while (webserver_any_active() && !runtime_stopped) {
        for (size_t i = 0; i < webserver_count; i++) {
            webserver_sync_shutdown(&webservers[i]);
            webserver_process_responses(&webservers[i]);
            if (error_action_pending()) {
                return 1;
            }
            webserver_finish_shutdown(&webservers[i]);
        }

        size_t descriptor_count = 0;
        for (size_t i = 0; i < webserver_count; i++) {
            if (webservers[i].running && !webservers[i].shutdown_requested) {
                descriptor_count++;
            }
            descriptor_count += webservers[i].client_count;
        }
        if (descriptor_count == 0) {
            break;
        }
        struct pollfd *pollfds = calloc(descriptor_count, sizeof(struct pollfd));
        if (!pollfds) {
            abort();
        }
        size_t cursor = 0;
        for (size_t i = 0; i < webserver_count; i++) {
            if (webservers[i].running && !webservers[i].shutdown_requested) {
                pollfds[cursor].fd = webservers[i].listen_fd;
                pollfds[cursor].events = POLLIN;
                cursor++;
            }
            for (size_t j = 0; j < webservers[i].client_count; j++) {
                pollfds[cursor].fd = webservers[i].clients[j].fd;
                pollfds[cursor].events =
                    webservers[i].clients[j].waiting_response ? 0 : POLLIN;
                cursor++;
            }
        }
        int ready = poll(pollfds, descriptor_count, 50);
        if (ready < 0 && errno != EINTR) {
            free(pollfds);
            webserver_raise("webserver poll failed");
            return 1;
        }
        cursor = 0;
        for (size_t i = 0; i < webserver_count; i++) {
            size_t polled_client_count = webservers[i].client_count;
            if (webservers[i].running && !webservers[i].shutdown_requested) {
                if (pollfds[cursor].revents & POLLIN) {
                    webserver_accept(&webservers[i]);
                }
                cursor++;
            }
            for (size_t j = 0; j < polled_client_count; j++) {
                int fd = pollfds[cursor].fd;
                short events = pollfds[cursor].revents;
                cursor++;
                size_t current = 0;
                while (current < webservers[i].client_count &&
                       webservers[i].clients[current].fd != fd) {
                    current++;
                }
                if (current >= webservers[i].client_count) {
                    continue;
                }
                if (events & (POLLERR | POLLHUP | POLLNVAL)) {
                    webserver_client_close(&webservers[i], current);
                } else if ((events & POLLIN) &&
                           !webservers[i].clients[current].waiting_response) {
                    webserver_read_client(&webservers[i], current);
                }
                if (error_action_pending()) {
                    free(pollfds);
                    return 1;
                }
            }
            webserver_process_responses(&webservers[i]);
            webserver_check_timeouts(&webservers[i]);
        }
        free(pollfds);
    }
    return error_action_pending() ? 1 : 0;
}

static Value webserver_eval_listen(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        webserver_raise("webserver.listen expects one argument");
        return value_null();
    }
    Value port_value = eval_expr(expr->as.call.args.items[0]);
    int port = 0;
    if (error_action_pending()) {
        value_free(port_value);
        return value_null();
    }
    if (!webserver_integer(port_value, &port) || port < 0 || port > 65535) {
        value_free(port_value);
        webserver_raise("webserver.listen port must be an integer from 0 to 65535");
        return value_null();
    }
    value_free(port_value);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        webserver_raise("webserver could not create socket");
        return value_null();
    }
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(fd, 16) != 0 ||
        !webserver_set_blocking_mode(fd, 0)) {
        close(fd);
        webserver_raise("webserver could not bind and listen on loopback");
        return value_null();
    }
    socklen_t length = sizeof(address);
    if (getsockname(fd, (struct sockaddr *)&address, &length) != 0) {
        close(fd);
        webserver_raise("webserver could not determine bound port");
        return value_null();
    }

    WebServer *servers = realloc(webservers, sizeof(WebServer) * (webserver_count + 1));
    if (!servers) {
        abort();
    }
    webservers = servers;
    WebServer *server = &webservers[webserver_count++];
    memset(server, 0, sizeof(*server));
    server->id = webserver_next_id++;
    server->listen_fd = fd;
    server->port = ntohs(address.sin_port);
    server->running = 1;
    server->next_request_id = 1;

    Value record = value_record(NULL, 0);
    record_set(&record, "_webserver_id", value_number((double)server->id));
    record_set(&record, "port", value_number((double)server->port));
    record_set(&record, "running", value_bool(1));
    record_set(&record, "requests", value_array(NULL, 0));
    record_set(&record, "responses", value_array(NULL, 0));
    return record;
}

static Value webserver_eval_close(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        webserver_raise("webserver.close expects one argument");
        return value_null();
    }
    Value value = eval_expr(expr->as.call.args.items[0]);
    unsigned long id = 0;
    if (error_action_pending()) {
        value_free(value);
        return value_null();
    }
    if (!webserver_record_id(&value, &id) || !webserver_find(id)) {
        value_free(value);
        webserver_raise("webserver.close expects a server record");
        return value_null();
    }
    value_free(value);
    WebServer *server = webserver_find(id);
    server->shutdown_requested = 1;
    webserver_set_running(server, 0);
    return value_bool(1);
}

static Value webserver_eval_call(AstExpr *expr) {
    if (strcmp(expr->as.call.name, "listen") == 0) {
        return webserver_eval_listen(expr);
    }
    if (strcmp(expr->as.call.name, "close") == 0) {
        return webserver_eval_close(expr);
    }
    char message[256];
    snprintf(message, sizeof(message), "invalid function call: webserver.%s",
             expr->as.call.name);
    webserver_raise(message);
    return value_null();
}

static void webserver_clear(void) {
    for (size_t i = 0; i < webserver_count; i++) {
        webserver_close_native(&webservers[i]);
    }
    free(webservers);
    webservers = NULL;
    webserver_count = 0;
    webserver_next_id = 1;
}

#if HAVE_SQLITE3
#define SQLITE_ERROR_CODE 2002

typedef struct {
    sqlite3_stmt *statement;
    char **values;
    int count;
} SqliteParameterList;

static void sqlite_raise_message(const char *message) {
    runtime_error_raise(message, SQLITE_ERROR_CODE, "sqlite");
}

static void sqlite_raise_connection_error(sqlite3 *connection, const char *prefix) {
    const char *detail = connection ? sqlite3_errmsg(connection) : "";
    char message[1024];
    if (detail && detail[0]) {
        snprintf(message, sizeof(message), "%s: %s", prefix, detail);
    } else {
        snprintf(message, sizeof(message), "%s", prefix);
    }
    sqlite_raise_message(message);
}

static SqliteConnectionValue *sqlite_connection_from_value(Value value) {
    if (value.kind != VALUE_SQLITE_CONNECTION) {
        sqlite_raise_message("sqlite operation expects a sqlite_connection");
        return NULL;
    }
    SqliteConnectionValue *connection = value.as.sqlite_connection;
    if (!connection || connection->closed || !connection->connection) {
        sqlite_raise_message("sqlite connection is closed");
        return NULL;
    }
    return connection;
}

static char *sqlite_datetime_text(DateTime datetime) {
    char buffer[64];
    if (datetime.time_only) {
        if (datetime.precision == PREC_HOUR) {
            snprintf(buffer, sizeof(buffer), "%02d:00:00", datetime.hour);
        } else if (datetime.precision == PREC_MINUTE) {
            snprintf(buffer,
                     sizeof(buffer),
                     "%02d:%02d:00",
                     datetime.hour,
                     datetime.minute);
        } else {
            snprintf(buffer,
                     sizeof(buffer),
                     "%02d:%02d:%02d",
                     datetime.hour,
                     datetime.minute,
                     datetime.second);
        }
    } else if (datetime.precision < PREC_DAY) {
        sqlite_raise_message("SQLite date parameters require day precision");
        return NULL;
    } else if (datetime.precision == PREC_DAY) {
        snprintf(buffer,
                 sizeof(buffer),
                 "%04d-%02d-%02d",
                 datetime.year,
                 datetime.month,
                 datetime.day);
    } else {
        snprintf(buffer,
                 sizeof(buffer),
                 "%04d-%02d-%02d %02d:%02d:%02d",
                 datetime.year,
                 datetime.month,
                 datetime.day,
                 datetime.hour,
                 datetime.minute,
                 datetime.second);
    }
    return copy_string(buffer);
}

static Value sqlite_eval_connect(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        sqlite_raise_message("sqlite.connect expects one argument");
        return value_null();
    }

    Value path = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(path);
        return value_null();
    }
    if (path.kind != VALUE_STRING) {
        value_free(path);
        sqlite_raise_message("sqlite.connect expects a database path string");
        return value_null();
    }

    sqlite3 *native = NULL;
    int rc = sqlite3_open_v2(path.as.string,
                             &native,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                             NULL);
    value_free(path);
    if (rc != SQLITE_OK) {
        sqlite_raise_connection_error(native, "SQLite connection failed");
        if (native) {
            sqlite3_close(native);
        }
        return value_null();
    }

    SqliteConnectionValue *connection = calloc(1, sizeof(SqliteConnectionValue));
    if (!connection) {
        sqlite3_close(native);
        abort();
    }
    connection->connection = native;
    connection->ref_count = 1;
    return value_sqlite_connection(connection);
}

static Value sqlite_eval_close(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        sqlite_raise_message("sqlite.close expects one argument");
        return value_null();
    }
    Value value = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(value);
        return value_null();
    }
    if (value.kind != VALUE_SQLITE_CONNECTION) {
        value_free(value);
        sqlite_raise_message("sqlite.close expects a sqlite_connection");
        return value_null();
    }
    SqliteConnectionValue *connection = value.as.sqlite_connection;
    if (!connection || connection->closed) {
        value_free(value);
        sqlite_raise_message("sqlite connection is already closed");
        return value_null();
    }
    int rc = sqlite3_close(connection->connection);
    if (rc != SQLITE_OK) {
        sqlite_raise_connection_error(connection->connection, "SQLite close failed");
        value_free(value);
        return value_null();
    }
    connection->connection = NULL;
    connection->closed = 1;
    value_free(value);
    return value_bool(1);
}

static char *sqlite_parameter_text(Value value) {
    switch (value.kind) {
    case VALUE_STRING:
        return copy_string(value.as.string);
    case VALUE_DATETIME:
        return sqlite_datetime_text(value.as.datetime);
    default:
        sqlite_raise_message("unsupported SQLite text parameter type");
        return NULL;
    }
}

static int sqlite_bind_value(sqlite3_stmt *statement, int index, Value value, char **owned_text) {
    int rc = SQLITE_OK;
    *owned_text = NULL;
    switch (value.kind) {
    case VALUE_NULL:
        rc = sqlite3_bind_null(statement, index);
        break;
    case VALUE_BOOL:
        rc = sqlite3_bind_int64(statement, index, value.as.boolean ? 1 : 0);
        break;
    case VALUE_NUMBER:
        if (!isfinite(value.as.number)) {
            sqlite_raise_message("SQLite number parameters must be finite");
            return 0;
        }
        rc = sqlite3_bind_double(statement, index, value.as.number);
        break;
    case VALUE_STRING:
    case VALUE_DATETIME:
        *owned_text = sqlite_parameter_text(value);
        if (!*owned_text) {
            return 0;
        }
        rc = sqlite3_bind_text(statement, index, *owned_text, -1, SQLITE_TRANSIENT);
        break;
    default:
        sqlite_raise_message("unsupported SQLite parameter type");
        return 0;
    }
    if (rc != SQLITE_OK) {
        sqlite_raise_message("could not bind SQLite parameter");
        return 0;
    }
    return 1;
}

static void sqlite_parameter_list_clear(SqliteParameterList *params) {
    if (params->statement) {
        sqlite3_finalize(params->statement);
    }
    for (int i = 0; i < params->count; i++) {
        free(params->values[i]);
    }
    free(params->values);
    memset(params, 0, sizeof(*params));
}

static int sqlite_sql_tail_is_empty(const char *tail) {
    while (*tail) {
        if (!isspace((unsigned char)*tail)) {
            return 0;
        }
        tail++;
    }
    return 1;
}

static int sqlite_prepare_statement(SqliteConnectionValue *connection,
                                    const char *sql,
                                    Value *params_value,
                                    SqliteParameterList *out) {
    const char *tail = NULL;
    int rc = sqlite3_prepare_v2(connection->connection, sql, -1, &out->statement, &tail);
    if (rc != SQLITE_OK) {
        sqlite_raise_connection_error(connection->connection, "SQLite prepare failed");
        return 0;
    }
    if (!out->statement) {
        sqlite_raise_message("SQLite SQL must contain a statement");
        return 0;
    }
    if (tail && !sqlite_sql_tail_is_empty(tail)) {
        sqlite_raise_message("SQLite query expects exactly one statement");
        return 0;
    }

    int expected = sqlite3_bind_parameter_count(out->statement);
    if (params_value) {
        if (params_value->kind != VALUE_ARRAY) {
            sqlite_raise_message("SQLite query parameters must be an array");
            return 0;
        }
        if (params_value->as.array.count > INT_MAX) {
            sqlite_raise_message("too many SQLite query parameters");
            return 0;
        }
        out->count = (int)params_value->as.array.count;
    } else {
        out->count = 0;
    }
    if (out->count != expected) {
        char message[160];
        snprintf(message,
                 sizeof(message),
                 "SQLite statement expects %d parameters but got %d",
                 expected,
                 out->count);
        sqlite_raise_message(message);
        return 0;
    }
    out->values = out->count > 0 ? calloc((size_t)out->count, sizeof(char *)) : NULL;
    if (out->count > 0 && !out->values) {
        abort();
    }
    for (int i = 0; i < out->count; i++) {
        if (!sqlite_bind_value(out->statement,
                               i + 1,
                               params_value->as.array.items[i],
                               &out->values[i])) {
            return 0;
        }
    }
    return 1;
}

static Value sqlite_column_value(sqlite3_stmt *statement, int column) {
    int type = sqlite3_column_type(statement, column);
    switch (type) {
    case SQLITE_NULL:
        return value_null();
    case SQLITE_INTEGER:
        return value_number((double)sqlite3_column_int64(statement, column));
    case SQLITE_FLOAT:
        return value_number(sqlite3_column_double(statement, column));
    case SQLITE_TEXT:
        return value_string((const char *)sqlite3_column_text(statement, column));
    case SQLITE_BLOB:
        sqlite_raise_message("SQLite blob results are not supported");
        return value_null();
    default:
        sqlite_raise_message("unsupported SQLite result type");
        return value_null();
    }
}

static int sqlite_result_columns_are_unique(sqlite3_stmt *statement) {
    int columns = sqlite3_column_count(statement);
    for (int i = 0; i < columns; i++) {
        const char *name = sqlite3_column_name(statement, i);
        for (int j = 0; j < i; j++) {
            if (strcmp(name, sqlite3_column_name(statement, j)) == 0) {
                char message[256];
                snprintf(message,
                         sizeof(message),
                         "duplicate SQLite result column: %s",
                         name);
                sqlite_raise_message(message);
                return 0;
            }
        }
    }
    return 1;
}

static int sqlite_rows_append(Value **items, size_t *count, size_t *capacity, Value row) {
    if (*count == *capacity) {
        size_t next = *capacity == 0 ? 8 : *capacity * 2;
        Value *resized = realloc(*items, sizeof(Value) * next);
        if (!resized) {
            abort();
        }
        *items = resized;
        *capacity = next;
    }
    (*items)[*count] = row;
    (*count)++;
    return 1;
}

static Value sqlite_rows_from_statement(sqlite3 *native, sqlite3_stmt *statement) {
    if (!sqlite_result_columns_are_unique(statement)) {
        return value_null();
    }

    int columns = sqlite3_column_count(statement);
    Value *items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    for (;;) {
        int rc = sqlite3_step(statement);
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            sqlite_raise_connection_error(native, "SQLite query failed");
            for (size_t i = 0; i < count; i++) {
                value_free(items[i]);
            }
            free(items);
            return value_null();
        }

        RecordField *fields = columns > 0
            ? calloc((size_t)columns, sizeof(RecordField))
            : NULL;
        if (columns > 0 && !fields) {
            abort();
        }
        int completed_fields = 0;
        for (int column = 0; column < columns; column++) {
            int before_error = error_generation;
            fields[column].name = copy_string(sqlite3_column_name(statement, column));
            fields[column].value = malloc(sizeof(Value));
            if (!fields[column].value) {
                abort();
            }
            *fields[column].value = sqlite_column_value(statement, column);
            completed_fields++;
            if (error_generation != before_error) {
                for (int i = 0; i < completed_fields; i++) {
                    free(fields[i].name);
                    value_free(*fields[i].value);
                    free(fields[i].value);
                }
                free(fields);
                for (size_t i = 0; i < count; i++) {
                    value_free(items[i]);
                }
                free(items);
                return value_null();
            }
        }
        sqlite_rows_append(&items, &count, &capacity, value_record(fields, (size_t)columns));
    }
    return value_array(items, count);
}

static char *sqlite_command_name(const char *sql) {
    while (*sql && isspace((unsigned char)*sql)) {
        sql++;
    }
    size_t length = 0;
    while (sql[length] && (isalpha((unsigned char)sql[length]) || sql[length] == '_')) {
        length++;
    }
    if (length == 0) {
        return copy_string("SQL");
    }
    char *name = malloc(length + 1);
    if (!name) {
        abort();
    }
    for (size_t i = 0; i < length; i++) {
        name[i] = (char)toupper((unsigned char)sql[i]);
    }
    name[length] = '\0';
    return name;
}

static Value sqlite_command_result(sqlite3 *native, const char *sql) {
    RecordField *fields = calloc(2, sizeof(RecordField));
    if (!fields) {
        abort();
    }
    fields[0].name = copy_string("command");
    fields[0].value = malloc(sizeof(Value));
    fields[1].name = copy_string("rows_affected");
    fields[1].value = malloc(sizeof(Value));
    if (!fields[0].value || !fields[1].value) {
        abort();
    }
    char *command = sqlite_command_name(sql);
    *fields[0].value = value_string(command);
    free(command);
    *fields[1].value = value_number((double)sqlite3_changes(native));
    return value_record(fields, 2);
}

static Value sqlite_eval_sql(AstExpr *expr, int query_mode) {
    if (expr->as.call.args.count != 2 && expr->as.call.args.count != 3) {
        sqlite_raise_message(query_mode
            ? "sqlite.query expects two or three arguments"
            : "sqlite.exec expects two or three arguments");
        return value_null();
    }

    Value connection_value = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(connection_value);
        return value_null();
    }
    SqliteConnectionValue *connection = sqlite_connection_from_value(connection_value);
    if (!connection) {
        value_free(connection_value);
        return value_null();
    }

    Value sql = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(sql);
        value_free(connection_value);
        return value_null();
    }
    if (sql.kind != VALUE_STRING) {
        value_free(sql);
        value_free(connection_value);
        sqlite_raise_message(query_mode
            ? "sqlite.query SQL must be a string"
            : "sqlite.exec SQL must be a string");
        return value_null();
    }

    Value params_value = value_null();
    Value *params_ptr = NULL;
    if (expr->as.call.args.count == 3) {
        params_value = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(params_value);
            value_free(sql);
            value_free(connection_value);
            return value_null();
        }
        params_ptr = &params_value;
    }

    SqliteParameterList params = {0};
    if (!sqlite_prepare_statement(connection, sql.as.string, params_ptr, &params)) {
        sqlite_parameter_list_clear(&params);
        value_free(params_value);
        value_free(sql);
        value_free(connection_value);
        return value_null();
    }

    int columns = sqlite3_column_count(params.statement);
    Value converted = value_null();
    if (query_mode) {
        converted = sqlite_rows_from_statement(connection->connection, params.statement);
    } else if (columns > 0) {
        sqlite_raise_message("sqlite.exec cannot discard row results; use sqlite.query");
    } else {
        int rc = sqlite3_step(params.statement);
        if (rc == SQLITE_DONE) {
            converted = sqlite_command_result(connection->connection, sql.as.string);
        } else {
            sqlite_raise_connection_error(connection->connection, "SQLite exec failed");
        }
    }

    sqlite_parameter_list_clear(&params);
    value_free(params_value);
    value_free(sql);
    value_free(connection_value);
    return converted;
}

static Value sqlite_eval_transaction(AstExpr *expr, const char *sql, const char *name) {
    if (expr->as.call.args.count != 1) {
        char message[128];
        snprintf(message, sizeof(message), "sqlite.%s expects one argument", name);
        sqlite_raise_message(message);
        return value_null();
    }
    Value connection_value = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(connection_value);
        return value_null();
    }
    SqliteConnectionValue *connection = sqlite_connection_from_value(connection_value);
    if (!connection) {
        value_free(connection_value);
        return value_null();
    }
    char *error = NULL;
    int rc = sqlite3_exec(connection->connection, sql, NULL, NULL, &error);
    if (rc != SQLITE_OK) {
        char message[1024];
        snprintf(message,
                 sizeof(message),
                 "SQLite transaction command failed: %s",
                 error ? error : sqlite3_errmsg(connection->connection));
        sqlite3_free(error);
        sqlite_raise_message(message);
        value_free(connection_value);
        return value_null();
    }
    sqlite3_free(error);
    value_free(connection_value);
    return value_bool(1);
}

static Value sqlite_eval_last_insert_rowid(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        sqlite_raise_message("sqlite.last_insert_rowid expects one argument");
        return value_null();
    }
    Value connection_value = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(connection_value);
        return value_null();
    }
    SqliteConnectionValue *connection = sqlite_connection_from_value(connection_value);
    if (!connection) {
        value_free(connection_value);
        return value_null();
    }
    sqlite3_int64 rowid = sqlite3_last_insert_rowid(connection->connection);
    value_free(connection_value);
    return value_number((double)rowid);
}

static Value sqlite_eval_call(AstExpr *expr) {
    const char *name = expr->as.call.name;
    if (strcmp(name, "connect") == 0) {
        return sqlite_eval_connect(expr);
    }
    if (strcmp(name, "close") == 0) {
        return sqlite_eval_close(expr);
    }
    if (strcmp(name, "query") == 0) {
        return sqlite_eval_sql(expr, 1);
    }
    if (strcmp(name, "exec") == 0) {
        return sqlite_eval_sql(expr, 0);
    }
    if (strcmp(name, "begin") == 0) {
        return sqlite_eval_transaction(expr, "BEGIN", "begin");
    }
    if (strcmp(name, "commit") == 0) {
        return sqlite_eval_transaction(expr, "COMMIT", "commit");
    }
    if (strcmp(name, "rollback") == 0) {
        return sqlite_eval_transaction(expr, "ROLLBACK", "rollback");
    }
    if (strcmp(name, "last_insert_rowid") == 0) {
        return sqlite_eval_last_insert_rowid(expr);
    }
    char message[256];
    snprintf(message, sizeof(message), "invalid function call: sqlite.%s", name);
    sqlite_raise_message(message);
    return value_null();
}
#endif

#if HAVE_LIBPQ
#define PG_ERROR_CODE 2001

typedef struct {
    char **values;
    const char **pointers;
    int count;
} PgParameterList;

static void pg_raise_message(const char *message) {
    runtime_error_raise(message, PG_ERROR_CODE, "postgres");
}

static void pg_raise_connection_error(PGconn *connection, const char *prefix) {
    const char *detail = connection ? PQerrorMessage(connection) : "";
    size_t detail_len = strlen(detail);
    while (detail_len > 0 &&
           (detail[detail_len - 1] == '\n' || detail[detail_len - 1] == '\r')) {
        detail_len--;
    }
    char message[1024];
    if (detail_len > 0) {
        snprintf(message, sizeof(message), "%s: %.*s", prefix, (int)detail_len, detail);
    } else {
        snprintf(message, sizeof(message), "%s", prefix);
    }
    pg_raise_message(message);
}

static void pg_raise_result_error(PGconn *connection, PGresult *result) {
    const char *detail = result ? PQresultErrorMessage(result) : NULL;
    if (!detail || detail[0] == '\0') {
        pg_raise_connection_error(connection, "PostgreSQL operation failed");
        return;
    }

    size_t detail_len = strlen(detail);
    while (detail_len > 0 &&
           (detail[detail_len - 1] == '\n' || detail[detail_len - 1] == '\r')) {
        detail_len--;
    }
    const char *sqlstate = PQresultErrorField(result, PG_DIAG_SQLSTATE);
    char message[1200];
    if (sqlstate && sqlstate[0]) {
        snprintf(message,
                 sizeof(message),
                 "%.*s [SQLSTATE %s]",
                 (int)detail_len,
                 detail,
                 sqlstate);
    } else {
        snprintf(message, sizeof(message), "%.*s", (int)detail_len, detail);
    }
    pg_raise_message(message);
}

static PgConnectionValue *pg_connection_from_value(Value value) {
    if (value.kind != VALUE_POSTGRES_CONNECTION) {
        pg_raise_message("pg operation expects a postgres_connection");
        return NULL;
    }
    PgConnectionValue *connection = value.as.postgres_connection;
    if (!connection || connection->closed || !connection->connection) {
        pg_raise_message("postgres connection is closed");
        return NULL;
    }
    return connection;
}

static int pg_config_field_allowed(const char *name) {
    static const char *allowed[] = {
        "host",
        "port",
        "database",
        "user",
        "password",
        "sslmode",
        "connect_timeout",
        "application_name"
    };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        if (strcmp(name, allowed[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *pg_config_keyword(const char *name) {
    return strcmp(name, "database") == 0 ? "dbname" : name;
}

static char *pg_integer_text(Value value, const char *field_name) {
    if (value.kind == VALUE_STRING) {
        return copy_string(value.as.string);
    }
    if (value.kind == VALUE_NUMBER &&
        isfinite(value.as.number) &&
        floor(value.as.number) == value.as.number &&
        value.as.number >= 0 &&
        value.as.number <= 2147483647.0) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.0f", value.as.number);
        return copy_string(buffer);
    }
    char message[256];
    snprintf(message,
             sizeof(message),
             "pg.connect config field '%s' expects a non-negative integer or string",
             field_name);
    pg_raise_message(message);
    return NULL;
}

static char *pg_config_value_text(const char *field_name, Value value) {
    if (strcmp(field_name, "port") == 0 ||
        strcmp(field_name, "connect_timeout") == 0) {
        return pg_integer_text(value, field_name);
    }
    if (value.kind != VALUE_STRING) {
        char message[256];
        snprintf(message,
                 sizeof(message),
                 "pg.connect config field '%s' expects a string",
                 field_name);
        pg_raise_message(message);
        return NULL;
    }
    return copy_string(value.as.string);
}

static Value pg_eval_connect(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        pg_raise_message("pg.connect expects one argument");
        return value_null();
    }

    Value config = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(config);
        return value_null();
    }
    if (config.kind != VALUE_RECORD) {
        value_free(config);
        pg_raise_message("pg.connect expects a config record");
        return value_null();
    }

    int before_error = error_generation;
    size_t count = config.as.record.count;
    const char **keywords = calloc(count + 1, sizeof(char *));
    const char **values = calloc(count + 1, sizeof(char *));
    char **owned_values = calloc(count, sizeof(char *));
    if (!keywords || !values || !owned_values) {
        abort();
    }

    size_t used = 0;
    for (size_t i = 0; i < count; i++) {
        RecordField *field = &config.as.record.fields[i];
        if (!pg_config_field_allowed(field->name)) {
            char message[256];
            snprintf(message,
                     sizeof(message),
                     "unknown pg.connect config field: %s",
                     field->name);
            pg_raise_message(message);
            break;
        }
        if (field->value->kind == VALUE_NULL) {
            continue;
        }
        char *text = pg_config_value_text(field->name, *field->value);
        if (!text) {
            break;
        }
        keywords[used] = pg_config_keyword(field->name);
        values[used] = text;
        owned_values[used] = text;
        used++;
    }

    if (error_generation != before_error) {
        for (size_t i = 0; i < used; i++) {
            free(owned_values[i]);
        }
        free(owned_values);
        free(values);
        free(keywords);
        value_free(config);
        return value_null();
    }

    PGconn *native = PQconnectdbParams(keywords, values, 0);
    for (size_t i = 0; i < used; i++) {
        free(owned_values[i]);
    }
    free(owned_values);
    free(values);
    free(keywords);
    value_free(config);

    if (!native) {
        pg_raise_message("could not allocate PostgreSQL connection");
        return value_null();
    }
    if (PQstatus(native) != CONNECTION_OK) {
        pg_raise_connection_error(native, "PostgreSQL connection failed");
        PQfinish(native);
        return value_null();
    }

    PgConnectionValue *connection = calloc(1, sizeof(PgConnectionValue));
    if (!connection) {
        PQfinish(native);
        abort();
    }
    connection->connection = native;
    connection->ref_count = 1;
    return value_postgres_connection(connection);
}

static Value pg_eval_close(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        pg_raise_message("pg.close expects one argument");
        return value_null();
    }
    Value value = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(value);
        return value_null();
    }
    if (value.kind != VALUE_POSTGRES_CONNECTION) {
        value_free(value);
        pg_raise_message("pg.close expects a postgres_connection");
        return value_null();
    }
    PgConnectionValue *connection = value.as.postgres_connection;
    if (!connection || connection->closed) {
        value_free(value);
        pg_raise_message("postgres connection is already closed");
        return value_null();
    }
    PQfinish(connection->connection);
    connection->connection = NULL;
    connection->closed = 1;
    value_free(value);
    return value_bool(1);
}

static void pg_json_append_string(StringBuilder *builder, const char *text) {
    static const char hex[] = "0123456789abcdef";
    sb_append_char(builder, '"');
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; cursor++) {
        unsigned char ch = *cursor;
        if (ch == '"' || ch == '\\') {
            sb_append_char(builder, '\\');
            sb_append_char(builder, (char)ch);
        } else if (ch == '\b') {
            sb_append_text(builder, "\\b");
        } else if (ch == '\f') {
            sb_append_text(builder, "\\f");
        } else if (ch == '\n') {
            sb_append_text(builder, "\\n");
        } else if (ch == '\r') {
            sb_append_text(builder, "\\r");
        } else if (ch == '\t') {
            sb_append_text(builder, "\\t");
        } else if (ch < 0x20) {
            sb_append_text(builder, "\\u00");
            sb_append_char(builder, hex[ch >> 4]);
            sb_append_char(builder, hex[ch & 0x0f]);
        } else {
            sb_append_char(builder, (char)ch);
        }
    }
    sb_append_char(builder, '"');
}

static int pg_json_append_value(StringBuilder *builder, Value value) {
    char number[64];
    switch (value.kind) {
    case VALUE_NULL:
        sb_append_text(builder, "null");
        return 1;
    case VALUE_BOOL:
        sb_append_text(builder, value.as.boolean ? "true" : "false");
        return 1;
    case VALUE_NUMBER:
        if (!isfinite(value.as.number)) {
            pg_raise_message("PostgreSQL JSON parameters require finite numbers");
            return 0;
        }
        snprintf(number, sizeof(number), "%.17g", value.as.number);
        sb_append_text(builder, number);
        return 1;
    case VALUE_STRING:
        pg_json_append_string(builder, value.as.string);
        return 1;
    case VALUE_ARRAY:
        sb_append_char(builder, '[');
        for (size_t i = 0; i < value.as.array.count; i++) {
            if (i > 0) {
                sb_append_char(builder, ',');
            }
            if (!pg_json_append_value(builder, value.as.array.items[i])) {
                return 0;
            }
        }
        sb_append_char(builder, ']');
        return 1;
    case VALUE_RECORD:
        sb_append_char(builder, '{');
        for (size_t i = 0; i < value.as.record.count; i++) {
            if (i > 0) {
                sb_append_char(builder, ',');
            }
            pg_json_append_string(builder, value.as.record.fields[i].name);
            sb_append_char(builder, ':');
            if (!pg_json_append_value(builder, *value.as.record.fields[i].value)) {
                return 0;
            }
        }
        sb_append_char(builder, '}');
        return 1;
    default:
        pg_raise_message("PostgreSQL JSON parameters support only records, arrays, strings, numbers, booleans, and nothing");
        return 0;
    }
}

static char *pg_datetime_text(DateTime datetime) {
    char buffer[64];
    if (datetime.time_only) {
        if (datetime.precision == PREC_HOUR) {
            snprintf(buffer, sizeof(buffer), "%02d:00:00", datetime.hour);
        } else if (datetime.precision == PREC_MINUTE) {
            snprintf(buffer,
                     sizeof(buffer),
                     "%02d:%02d:00",
                     datetime.hour,
                     datetime.minute);
        } else {
            snprintf(buffer,
                     sizeof(buffer),
                     "%02d:%02d:%02d",
                     datetime.hour,
                     datetime.minute,
                     datetime.second);
        }
    } else if (datetime.precision < PREC_DAY) {
        pg_raise_message("PostgreSQL date parameters require day precision");
        return NULL;
    } else if (datetime.precision == PREC_DAY) {
        snprintf(buffer,
                 sizeof(buffer),
                 "%04d-%02d-%02d",
                 datetime.year,
                 datetime.month,
                 datetime.day);
    } else {
        snprintf(buffer,
                 sizeof(buffer),
                 "%04d-%02d-%02d %02d:%02d:%02d",
                 datetime.year,
                 datetime.month,
                 datetime.day,
                 datetime.hour,
                 datetime.minute,
                 datetime.second);
    }
    return copy_string(buffer);
}

static char *pg_parameter_text(Value value) {
    char buffer[64];
    switch (value.kind) {
    case VALUE_STRING:
        return copy_string(value.as.string);
    case VALUE_BOOL:
        return copy_string(value.as.boolean ? "true" : "false");
    case VALUE_NUMBER:
        if (!isfinite(value.as.number)) {
            pg_raise_message("PostgreSQL number parameters must be finite");
            return NULL;
        }
        snprintf(buffer, sizeof(buffer), "%.17g", value.as.number);
        return copy_string(buffer);
    case VALUE_DATETIME:
        return pg_datetime_text(value.as.datetime);
    case VALUE_ARRAY:
    case VALUE_RECORD: {
        StringBuilder builder;
        sb_init(&builder);
        if (!pg_json_append_value(&builder, value)) {
            free(builder.items);
            return NULL;
        }
        return sb_take(&builder);
    }
    case VALUE_NULL:
        return NULL;
    default:
        pg_raise_message("unsupported PostgreSQL parameter type");
        return NULL;
    }
}

static void pg_parameter_list_clear(PgParameterList *params) {
    for (int i = 0; i < params->count; i++) {
        free(params->values[i]);
    }
    free(params->values);
    free(params->pointers);
    memset(params, 0, sizeof(*params));
}

static int pg_parameter_list_build(Value value, PgParameterList *out) {
    if (value.kind != VALUE_ARRAY) {
        pg_raise_message("PostgreSQL query parameters must be an array");
        return 0;
    }
    if (value.as.array.count > INT_MAX) {
        pg_raise_message("too many PostgreSQL query parameters");
        return 0;
    }

    out->count = (int)value.as.array.count;
    out->values = calloc(value.as.array.count, sizeof(char *));
    out->pointers = calloc(value.as.array.count, sizeof(char *));
    if (value.as.array.count > 0 && (!out->values || !out->pointers)) {
        abort();
    }
    for (size_t i = 0; i < value.as.array.count; i++) {
        Value item = value.as.array.items[i];
        if (item.kind == VALUE_NULL) {
            out->pointers[i] = NULL;
            continue;
        }
        out->values[i] = pg_parameter_text(item);
        if (!out->values[i]) {
            pg_parameter_list_clear(out);
            return 0;
        }
        out->pointers[i] = out->values[i];
    }
    return 1;
}

static PGresult *pg_execute_sql(PgConnectionValue *connection,
                                const char *sql,
                                PgParameterList *params) {
    if (params) {
        return PQexecParams(connection->connection,
                            sql,
                            params->count,
                            NULL,
                            params->pointers,
                            NULL,
                            NULL,
                            0);
    }
    return PQexec(connection->connection, sql);
}

static int pg_oid_is_array(Oid oid) {
    static const Oid array_oids[] = {
        1000, 1001, 1002, 1003, 1005, 1007, 1009, 1014, 1015, 1016,
        1021, 1022, 1028, 1115, 1182, 1183, 1185, 1187, 1231, 199,
        2951, 3807
    };
    for (size_t i = 0; i < sizeof(array_oids) / sizeof(array_oids[0]); i++) {
        if (oid == array_oids[i]) {
            return 1;
        }
    }
    return 0;
}

static Value pg_decode_json(const char *text) {
    DecodeParser parser = {0};
    parser.text = text;
    parser.ok = 1;
    Value result = decode_parse_value(&parser);
    if (parser.ok) {
        decode_skip_ws(&parser);
        if (parser.text[parser.pos] != '\0') {
            value_free(result);
            decode_error(&parser, "unexpected trailing text");
            result = value_null();
        }
    }
    if (!parser.ok) {
        char message[256];
        snprintf(message, sizeof(message), "invalid PostgreSQL JSON result: %s", parser.message);
        pg_raise_message(message);
        value_free(result);
        result = value_null();
    }
    return result;
}

static int pg_parse_datetime_result(const char *text, int time_only, Value *out) {
    char normalized[20];
    size_t required = time_only ? 8 : 19;
    size_t length = strlen(text);
    if (length < required) {
        return 0;
    }
    memcpy(normalized, text, required);
    normalized[required] = '\0';
    if (!time_only && normalized[10] == 'T') {
        normalized[10] = ' ';
    }
    DateTime datetime;
    int ok = time_only
        ? parse_time_value(normalized, &datetime)
        : parse_date_value(normalized, &datetime);
    if (!ok) {
        return 0;
    }
    *out = value_datetime(datetime);
    return 1;
}

static int pg_parse_number_result(const char *text, double *out) {
    errno = 0;
    char *end = NULL;
    double number = strtod(text, &end);
    if (errno == ERANGE || end == text || *end != '\0') {
        return 0;
    }
    *out = number;
    return 1;
}

static Value pg_result_value(PGresult *result, int row, int column) {
    if (PQgetisnull(result, row, column)) {
        return value_null();
    }

    Oid oid = PQftype(result, column);
    const char *text = PQgetvalue(result, row, column);
    if (pg_oid_is_array(oid)) {
        pg_raise_message("PostgreSQL array result types are not supported");
        return value_null();
    }
    if (oid == 16) {
        if (strcmp(text, "t") == 0 || strcmp(text, "true") == 0) {
            return value_bool(1);
        }
        if (strcmp(text, "f") == 0 || strcmp(text, "false") == 0) {
            return value_bool(0);
        }
        pg_raise_message("invalid PostgreSQL boolean result");
        return value_null();
    }
    if (oid == 21 || oid == 23 || oid == 700 || oid == 701) {
        double number = 0.0;
        if (!pg_parse_number_result(text, &number)) {
            pg_raise_message("invalid PostgreSQL numeric result");
            return value_null();
        }
        return value_number(number);
    }
    if (oid == 20 || oid == 1700 || oid == 1184 || oid == 1186) {
        return value_string(text);
    }
    if (oid == 114 || oid == 3802) {
        return pg_decode_json(text);
    }
    if (oid == 1082) {
        DateTime datetime;
        if (!parse_date_value(text, &datetime)) {
            pg_raise_message("invalid PostgreSQL date result");
            return value_null();
        }
        return value_datetime(datetime);
    }
    if (oid == 1083) {
        Value value;
        if (!pg_parse_datetime_result(text, 1, &value)) {
            pg_raise_message("invalid PostgreSQL time result");
            return value_null();
        }
        return value;
    }
    if (oid == 1114) {
        Value value;
        if (!pg_parse_datetime_result(text, 0, &value)) {
            pg_raise_message("invalid PostgreSQL timestamp result");
            return value_null();
        }
        return value;
    }
    if (oid == 17) {
        pg_raise_message("PostgreSQL bytea results are not supported");
        return value_null();
    }
    return value_string(text);
}

static Value pg_rows_from_result(PGresult *result) {
    int columns = PQnfields(result);
    int rows = PQntuples(result);
    for (int i = 0; i < columns; i++) {
        const char *name = PQfname(result, i);
        for (int j = 0; j < i; j++) {
            if (strcmp(name, PQfname(result, j)) == 0) {
                char message[256];
                snprintf(message,
                         sizeof(message),
                         "duplicate PostgreSQL result column: %s",
                         name);
                pg_raise_message(message);
                return value_null();
            }
        }
    }

    Value *items = rows > 0 ? calloc((size_t)rows, sizeof(Value)) : NULL;
    if (rows > 0 && !items) {
        abort();
    }
    int completed = 0;
    for (int row = 0; row < rows; row++) {
        RecordField *fields = columns > 0
            ? calloc((size_t)columns, sizeof(RecordField))
            : NULL;
        if (columns > 0 && !fields) {
            abort();
        }
        int completed_fields = 0;
        for (int column = 0; column < columns; column++) {
            int before_error = error_generation;
            fields[column].name = copy_string(PQfname(result, column));
            fields[column].value = malloc(sizeof(Value));
            if (!fields[column].value) {
                abort();
            }
            *fields[column].value = pg_result_value(result, row, column);
            completed_fields++;
            if (error_generation != before_error) {
                for (int i = 0; i < completed_fields; i++) {
                    free(fields[i].name);
                    value_free(*fields[i].value);
                    free(fields[i].value);
                }
                free(fields);
                for (int i = 0; i < completed; i++) {
                    value_free(items[i]);
                }
                free(items);
                return value_null();
            }
        }
        items[row] = value_record(fields, (size_t)columns);
        completed++;
    }
    return value_array(items, (size_t)rows);
}

static Value pg_command_result(PGresult *result) {
    const char *status = PQcmdStatus(result);
    size_t command_len = strcspn(status, " ");
    char *command = malloc(command_len + 1);
    if (!command) {
        abort();
    }
    memcpy(command, status, command_len);
    command[command_len] = '\0';

    RecordField *fields = calloc(2, sizeof(RecordField));
    if (!fields) {
        abort();
    }
    fields[0].name = copy_string("command");
    fields[0].value = malloc(sizeof(Value));
    fields[1].name = copy_string("rows_affected");
    fields[1].value = malloc(sizeof(Value));
    if (!fields[0].value || !fields[1].value) {
        abort();
    }
    *fields[0].value = value_string(command);
    free(command);

    const char *tuples = PQcmdTuples(result);
    if (tuples && tuples[0]) {
        double count = 0.0;
        if (!pg_parse_number_result(tuples, &count)) {
            for (size_t i = 0; i < 2; i++) {
                free(fields[i].name);
                if (fields[i].value) {
                    if (i == 0) {
                        value_free(*fields[i].value);
                    }
                    free(fields[i].value);
                }
            }
            free(fields);
            pg_raise_message("invalid PostgreSQL affected-row count");
            return value_null();
        }
        *fields[1].value = value_number(count);
    } else {
        *fields[1].value = value_null();
    }
    return value_record(fields, 2);
}

static Value pg_eval_sql(AstExpr *expr, int query_mode) {
    if (expr->as.call.args.count != 2 && expr->as.call.args.count != 3) {
        pg_raise_message(query_mode
            ? "pg.query expects two or three arguments"
            : "pg.exec expects two or three arguments");
        return value_null();
    }

    Value connection_value = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(connection_value);
        return value_null();
    }
    PgConnectionValue *connection = pg_connection_from_value(connection_value);
    if (!connection) {
        value_free(connection_value);
        return value_null();
    }

    Value sql = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(sql);
        value_free(connection_value);
        return value_null();
    }
    if (sql.kind != VALUE_STRING) {
        value_free(sql);
        value_free(connection_value);
        pg_raise_message(query_mode
            ? "pg.query SQL must be a string"
            : "pg.exec SQL must be a string");
        return value_null();
    }

    PgParameterList params = {0};
    Value params_value = value_null();
    PgParameterList *params_ptr = NULL;
    if (expr->as.call.args.count == 3) {
        params_value = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending() ||
            !pg_parameter_list_build(params_value, &params)) {
            value_free(params_value);
            value_free(sql);
            value_free(connection_value);
            return value_null();
        }
        params_ptr = &params;
    }

    PGresult *result = pg_execute_sql(connection, sql.as.string, params_ptr);
    pg_parameter_list_clear(&params);
    value_free(params_value);
    value_free(sql);
    if (!result) {
        pg_raise_connection_error(connection->connection, "PostgreSQL operation failed");
        value_free(connection_value);
        return value_null();
    }

    ExecStatusType status = PQresultStatus(result);
    Value converted = value_null();
    if (query_mode && status == PGRES_TUPLES_OK) {
        converted = pg_rows_from_result(result);
    } else if (query_mode && status == PGRES_COMMAND_OK) {
        converted = value_array(NULL, 0);
    } else if (!query_mode && status == PGRES_COMMAND_OK) {
        converted = pg_command_result(result);
    } else if (!query_mode && status == PGRES_TUPLES_OK) {
        pg_raise_message("pg.exec cannot discard row results; use pg.query");
    } else {
        pg_raise_result_error(connection->connection, result);
    }
    PQclear(result);
    value_free(connection_value);
    return converted;
}

static Value pg_eval_transaction(AstExpr *expr, const char *sql, const char *name) {
    if (expr->as.call.args.count != 1) {
        char message[128];
        snprintf(message, sizeof(message), "pg.%s expects one argument", name);
        pg_raise_message(message);
        return value_null();
    }
    Value connection_value = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(connection_value);
        return value_null();
    }
    PgConnectionValue *connection = pg_connection_from_value(connection_value);
    if (!connection) {
        value_free(connection_value);
        return value_null();
    }
    PGresult *result = PQexec(connection->connection, sql);
    if (!result) {
        pg_raise_connection_error(connection->connection, "PostgreSQL transaction command failed");
        value_free(connection_value);
        return value_null();
    }
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        pg_raise_result_error(connection->connection, result);
        PQclear(result);
        value_free(connection_value);
        return value_null();
    }
    PQclear(result);
    value_free(connection_value);
    return value_bool(1);
}

static Value pg_eval_call(AstExpr *expr) {
    const char *name = expr->as.call.name;
    if (strcmp(name, "connect") == 0) {
        return pg_eval_connect(expr);
    }
    if (strcmp(name, "close") == 0) {
        return pg_eval_close(expr);
    }
    if (strcmp(name, "query") == 0) {
        return pg_eval_sql(expr, 1);
    }
    if (strcmp(name, "exec") == 0) {
        return pg_eval_sql(expr, 0);
    }
    if (strcmp(name, "begin") == 0) {
        return pg_eval_transaction(expr, "BEGIN", "begin");
    }
    if (strcmp(name, "commit") == 0) {
        return pg_eval_transaction(expr, "COMMIT", "commit");
    }
    if (strcmp(name, "rollback") == 0) {
        return pg_eval_transaction(expr, "ROLLBACK", "rollback");
    }
    char message[256];
    snprintf(message, sizeof(message), "invalid function call: pg.%s", name);
    pg_raise_message(message);
    return value_null();
}
#endif

static Value eval_call(AstExpr *expr) {
    if (expr->as.call.library) {
        if (strcmp(expr->as.call.library, "webserver") == 0) {
            if (!webserver_library_loaded) {
                runtime_error_raise("library not loaded: webserver",
                                    4001,
                                    "webserver");
                return value_null();
            }
            return webserver_eval_call(expr);
        }

        if (strcmp(expr->as.call.library, "webclient") == 0) {
            if (!webclient_library_loaded) {
                runtime_error_raise("library not loaded: webclient",
                                    3001,
                                    "webclient");
                return value_null();
            }
#if HAVE_LIBCURL
            return webclient_eval_call(expr);
#else
            runtime_error_raise("WebClient support is not available in this build",
                                3001,
                                "webclient");
            return value_null();
#endif
        }

        if (strcmp(expr->as.call.library, "pg") == 0) {
            if (!pg_library_loaded) {
                runtime_error_raise("library not loaded: pg", 2001, "postgres");
                return value_null();
            }
#if HAVE_LIBPQ
            return pg_eval_call(expr);
#else
            runtime_error_raise("PostgreSQL support is not available in this build",
                                2001,
                                "postgres");
            return value_null();
#endif
        }

        if (strcmp(expr->as.call.library, "sqlite") == 0) {
            if (!sqlite_library_loaded) {
                runtime_error_raise("library not loaded: sqlite", 2002, "sqlite");
                return value_null();
            }
#if HAVE_SQLITE3
            return sqlite_eval_call(expr);
#else
            runtime_error_raise("SQLite support is not available in this build",
                                2002,
                                "sqlite");
            return value_null();
#endif
        }

        if (strcmp(expr->as.call.library, "gui") == 0) {
            if (!gui_library_loaded) {
                runtime_error_raise("library not loaded: gui", 1003, "gui");
                return value_null();
            }

            if (strcmp(expr->as.call.name, "window") == 0) {
                return gui_eval_window_call(expr);
            }

            if (strcmp(expr->as.call.name, "run") == 0) {
                return gui_eval_run_call(expr);
            }
        }

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

    FunctionDef *local_function = function_find_local(expr->as.call.name);
    if (local_function) {
        return eval_user_function(expr, local_function);
    }

    if (strcmp(expr->as.call.name, "env") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("env expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value name = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(name);
            return value_null();
        }
        if (name.kind != VALUE_STRING) {
            value_free(name);
            runtime_error_raise("env expects a string", 1003, "invalid function call");
            return value_null();
        }
        const char *value = getenv(name.as.string);
        value_free(name);
        if (!value) {
            return value_unknown();
        }
        return value_string(value);
    }

    if (strcmp(expr->as.call.name, "secure_token") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("secure_token expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value length_value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(length_value);
            return value_null();
        }
        if (length_value.kind != VALUE_NUMBER) {
            value_free(length_value);
            runtime_error_raise("secure_token expects a number", 1003, "invalid function call");
            return value_null();
        }
        double length_double = length_value.as.number;
        value_free(length_value);
        if (!isfinite(length_double) || length_double != floor(length_double)) {
            runtime_error_raise("secure_token length must be an integer", 1003, "invalid function call");
            return value_null();
        }
        if (length_double < 1 || length_double > SECURE_TOKEN_MAX_LENGTH) {
            runtime_error_raise("secure_token length must be between 1 and 4096", 1003, "invalid function call");
            return value_null();
        }

        size_t length = (size_t)length_double;
        unsigned char *random_bytes = malloc(length);
        char *token = malloc(length + 1);
        if (!random_bytes || !token) {
            free(random_bytes);
            free(token);
            abort();
        }

        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            free(random_bytes);
            free(token);
            runtime_error_raise("secure_token could not read secure random bytes", 1003, "random");
            return value_null();
        }

        size_t offset = 0;
        while (offset < length) {
            ssize_t count = read(fd, random_bytes + offset, length - offset);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                close(fd);
                free(random_bytes);
                free(token);
                runtime_error_raise("secure_token could not read secure random bytes", 1003, "random");
                return value_null();
            }
            offset += (size_t)count;
        }
        close(fd);

        static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        for (size_t i = 0; i < length; i++) {
            token[i] = alphabet[random_bytes[i] & 63];
        }
        token[length] = '\0';
        free(random_bytes);

        Value result = value_string(token);
        free(token);
        return result;
    }

    if (strcmp(expr->as.call.name, "lower") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("lower expects one argument", 1003, "invalid function call");
            return value_null();
        }
        return builtin_lower_value(eval_expr(expr->as.call.args.items[0]));
    }

    if (strcmp(expr->as.call.name, "upper") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("upper expects one argument", 1003, "invalid function call");
            return value_null();
        }
        return builtin_upper_value(eval_expr(expr->as.call.args.items[0]));
    }

    if (strcmp(expr->as.call.name, "string") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("string expects one argument", 1003, "invalid function call");
            return value_null();
        }
        return builtin_string_value(eval_expr(expr->as.call.args.items[0]));
    }

    if (strcmp(expr->as.call.name, "number") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("number expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(value);
            return value_null();
        }
        if (value.kind == VALUE_NUMBER) {
            return value;
        }
        if (value.kind == VALUE_STRING) {
            char *end;
            double result = strtod(value.as.string, &end);
            if (*end != '\0' || value.as.string[0] == '\0') {
                value_free(value);
                runtime_error_raise("number conversion failed: invalid numeric string", 1003, "invalid conversion");
                return value_null();
            }
            value_free(value);
            return value_number(result);
        }
        value_free(value);
        runtime_error_raise("number conversion failed: unsupported type", 1003, "invalid conversion");
        return value_null();
    }

    if (strcmp(expr->as.call.name, "boolean") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("boolean expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(value);
            return value_null();
        }
        if (value.kind == VALUE_BOOL) {
            return value;
        }
        if (value.kind == VALUE_STRING) {
            if (strcmp(value.as.string, "true") == 0) {
                value_free(value);
                return value_bool(1);
            }
            if (strcmp(value.as.string, "false") == 0) {
                value_free(value);
                return value_bool(0);
            }
            value_free(value);
            runtime_error_raise("boolean conversion failed: expected \"true\" or \"false\"", 1003, "invalid conversion");
            return value_null();
        }
        value_free(value);
        runtime_error_raise("boolean conversion failed: unsupported type", 1003, "invalid conversion");
        return value_null();
    }

    if (strcmp(expr->as.call.name, "array") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("array expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(value);
            return value_null();
        }
        if (value.kind == VALUE_ARRAY) {
            return value;
        }
        if (value.kind == VALUE_STRING) {
            Value decoded = builtin_decode_text(value);
            if (error_action_pending()) {
                return value_null();
            }
            if (decoded.kind != VALUE_ARRAY) {
                value_free(decoded);
                runtime_error_raise("array conversion failed: decoded value is not an array", 1003, "invalid conversion");
                return value_null();
            }
            return decoded;
        }
        value_free(value);
        runtime_error_raise("array conversion failed: unsupported type", 1003, "invalid conversion");
        return value_null();
    }

    if (strcmp(expr->as.call.name, "record") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("record expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(value);
            return value_null();
        }
        if (value.kind == VALUE_RECORD) {
            return value;
        }
        if (value.kind == VALUE_STRING) {
            Value decoded = builtin_decode_text(value);
            if (error_action_pending()) {
                return value_null();
            }
            if (decoded.kind != VALUE_RECORD) {
                value_free(decoded);
                runtime_error_raise("record conversion failed: decoded value is not a record", 1003, "invalid conversion");
                return value_null();
            }
            return decoded;
        }
        value_free(value);
        runtime_error_raise("record conversion failed: unsupported type", 1003, "invalid conversion");
        return value_null();
    }

    if (strcmp(expr->as.call.name, "replace") == 0) {
        if (expr->as.call.args.count != 3) {
            runtime_error_raise("replace expects three arguments", 1003, "invalid function call");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(text);
            return value_null();
        }
        Value from = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(text);
            value_free(from);
            return value_null();
        }
        Value to = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(text);
            value_free(from);
            value_free(to);
            return value_null();
        }

        if (text.kind != VALUE_STRING) {
            value_free(text);
            value_free(from);
            value_free(to);
            runtime_error_raise("replace: first argument must be a string", 1003, "invalid argument type");
            return value_null();
        }
        if (from.kind != VALUE_STRING) {
            value_free(text);
            value_free(from);
            value_free(to);
            runtime_error_raise("replace: second argument must be a string", 1003, "invalid argument type");
            return value_null();
        }
        if (to.kind != VALUE_STRING) {
            value_free(text);
            value_free(from);
            value_free(to);
            runtime_error_raise("replace: third argument must be a string", 1003, "invalid argument type");
            return value_null();
        }

        if (strlen(from.as.string) == 0) {
            value_free(text);
            value_free(from);
            value_free(to);
            runtime_error_raise("replace: search string cannot be empty", 1003, "invalid argument");
            return value_null();
        }

        const char *text_str = text.as.string;
        const char *from_str = from.as.string;
        const char *to_str = to.as.string;
        size_t from_len = strlen(from_str);
        size_t to_len = strlen(to_str);

        // Count occurrences to calculate result size
        size_t count = 0;
        const char *pos = text_str;
        while ((pos = strstr(pos, from_str)) != NULL) {
            count++;
            pos += from_len;
        }

        if (count == 0) {
            // No replacements needed
            Value result = value_string(text_str);
            value_free(text);
            value_free(from);
            value_free(to);
            return result;
        }

        // Calculate new string size
        size_t text_len = strlen(text_str);
        size_t new_len = text_len - (count * from_len) + (count * to_len);

        char *result_str = malloc(new_len + 1);
        if (!result_str) {
            value_free(text);
            value_free(from);
            value_free(to);
            runtime_error_raise("replace: memory allocation failed", 1003, "system error");
            return value_null();
        }

        char *result_pos = result_str;
        const char *current = text_str;

        while ((pos = strstr(current, from_str)) != NULL) {
            // Copy text before match
            size_t before_len = pos - current;
            memcpy(result_pos, current, before_len);
            result_pos += before_len;

            // Copy replacement text
            memcpy(result_pos, to_str, to_len);
            result_pos += to_len;

            // Move past the match
            current = pos + from_len;
        }

        // Copy remaining text
        strcpy(result_pos, current);

        Value result = value_string(result_str);
        free(result_str);
        value_free(text);
        value_free(from);
        value_free(to);
        return result;
    }

    if (strcmp(expr->as.call.name, "starts_with") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("starts_with expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(text);
            return value_null();
        }
        Value prefix = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(text);
            value_free(prefix);
            return value_null();
        }

        if (text.kind != VALUE_STRING) {
            value_free(text);
            value_free(prefix);
            runtime_error_raise("starts_with: first argument must be a string", 1003, "invalid argument type");
            return value_null();
        }
        if (prefix.kind != VALUE_STRING) {
            value_free(text);
            value_free(prefix);
            runtime_error_raise("starts_with: second argument must be a string", 1003, "invalid argument type");
            return value_null();
        }

        int result = strncmp(text.as.string, prefix.as.string, strlen(prefix.as.string)) == 0;
        value_free(text);
        value_free(prefix);
        return value_bool(result);
    }

    if (strcmp(expr->as.call.name, "ends_with") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("ends_with expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(text);
            return value_null();
        }
        Value suffix = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(text);
            value_free(suffix);
            return value_null();
        }

        if (text.kind != VALUE_STRING) {
            value_free(text);
            value_free(suffix);
            runtime_error_raise("ends_with: first argument must be a string", 1003, "invalid argument type");
            return value_null();
        }
        if (suffix.kind != VALUE_STRING) {
            value_free(text);
            value_free(suffix);
            runtime_error_raise("ends_with: second argument must be a string", 1003, "invalid argument type");
            return value_null();
        }

        size_t text_len = strlen(text.as.string);
        size_t suffix_len = strlen(suffix.as.string);

        int result = 0;
        if (suffix_len <= text_len) {
            result = strcmp(text.as.string + text_len - suffix_len, suffix.as.string) == 0;
        }

        value_free(text);
        value_free(suffix);
        return value_bool(result);
    }

    if (strcmp(expr->as.call.name, "repeat") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("repeat expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(text);
            return value_null();
        }
        Value count_val = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(text);
            value_free(count_val);
            return value_null();
        }

        if (text.kind != VALUE_STRING) {
            value_free(text);
            value_free(count_val);
            runtime_error_raise("repeat: first argument must be a string", 1003, "invalid argument type");
            return value_null();
        }
        if (count_val.kind != VALUE_NUMBER) {
            value_free(text);
            value_free(count_val);
            runtime_error_raise("repeat: second argument must be a number", 1003, "invalid argument type");
            return value_null();
        }

        double count_double = count_val.as.number;
        if (count_double != floor(count_double)) {
            value_free(text);
            value_free(count_val);
            runtime_error_raise("repeat: count must be an integer", 1003, "invalid argument");
            return value_null();
        }
        if (count_double < 0) {
            value_free(text);
            value_free(count_val);
            runtime_error_raise("repeat: count must be non-negative", 1003, "invalid argument");
            return value_null();
        }

        int count = (int)count_double;
        if (count == 0) {
            value_free(text);
            value_free(count_val);
            return value_string("");
        }

        size_t text_len = strlen(text.as.string);
        size_t result_len = text_len * count;

        char *result_str = malloc(result_len + 1);
        if (!result_str) {
            value_free(text);
            value_free(count_val);
            runtime_error_raise("repeat: memory allocation failed", 1003, "system error");
            return value_null();
        }

        char *pos = result_str;
        for (int i = 0; i < count; i++) {
            strcpy(pos, text.as.string);
            pos += text_len;
        }

        Value result = value_string(result_str);
        free(result_str);
        value_free(text);
        value_free(count_val);
        return result;
    }

    if (strcmp(expr->as.call.name, "keys") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("keys expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value record = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(record);
            return value_null();
        }

        if (record.kind != VALUE_RECORD) {
            value_free(record);
            runtime_error_raise("keys: argument must be a record", 1003, "invalid argument type");
            return value_null();
        }

        // Create array of keys
        size_t key_count = record.as.record.count;
        Value *keys = malloc(sizeof(Value) * key_count);
        if (!keys && key_count > 0) {
            value_free(record);
            runtime_error_raise("keys: memory allocation failed", 1003, "system error");
            return value_null();
        }

        for (size_t i = 0; i < key_count; i++) {
            keys[i] = value_string(record.as.record.fields[i].name);
        }

        Value result = value_array(keys, key_count);
        value_free(record);
        return result;
    }

    if (strcmp(expr->as.call.name, "values") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("values expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value record = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(record);
            return value_null();
        }

        if (record.kind != VALUE_RECORD) {
            value_free(record);
            runtime_error_raise("values: argument must be a record", 1003, "invalid argument type");
            return value_null();
        }

        // Create array of values
        size_t value_count = record.as.record.count;
        Value *values = malloc(sizeof(Value) * value_count);
        if (!values && value_count > 0) {
            value_free(record);
            runtime_error_raise("values: memory allocation failed", 1003, "system error");
            return value_null();
        }

        for (size_t i = 0; i < value_count; i++) {
            values[i] = value_copy(*record.as.record.fields[i].value);
        }

        Value result = value_array(values, value_count);
        value_free(record);
        return result;
    }

    if (strcmp(expr->as.call.name, "has") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("has expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value record = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(record);
            return value_null();
        }
        Value key = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(record);
            value_free(key);
            return value_null();
        }

        if (record.kind != VALUE_RECORD) {
            value_free(record);
            value_free(key);
            runtime_error_raise("has: first argument must be a record", 1003, "invalid argument type");
            return value_null();
        }
        if (key.kind != VALUE_STRING) {
            value_free(record);
            value_free(key);
            runtime_error_raise("has: second argument must be a string", 1003, "invalid argument type");
            return value_null();
        }

        // Check if key exists
        RecordField *field = record_find(&record, key.as.string);
        int result = field != NULL;

        value_free(record);
        value_free(key);
        return value_bool(result);
    }

    if (strcmp(expr->as.call.name, "remove_key") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("remove_key expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value record = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(record);
            return value_null();
        }
        Value key = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(record);
            value_free(key);
            return value_null();
        }

        if (record.kind != VALUE_RECORD) {
            value_free(record);
            value_free(key);
            runtime_error_raise("remove_key: first argument must be a record", 1003, "invalid argument type");
            return value_null();
        }
        if (key.kind != VALUE_STRING) {
            value_free(record);
            value_free(key);
            runtime_error_raise("remove_key: second argument must be a string", 1003, "invalid argument type");
            return value_null();
        }

        // Create new record without the specified key
        size_t original_count = record.as.record.count;
        RecordField *new_fields = NULL;
        size_t new_count = 0;

        // Count fields to keep and allocate
        for (size_t i = 0; i < original_count; i++) {
            if (strcmp(record.as.record.fields[i].name, key.as.string) != 0) {
                new_count++;
            }
        }

        if (new_count > 0) {
            new_fields = malloc(sizeof(RecordField) * new_count);
            if (!new_fields) {
                value_free(record);
                value_free(key);
                runtime_error_raise("remove_key: memory allocation failed", 1003, "system error");
                return value_null();
            }

            // Copy fields except the one to remove
            size_t new_index = 0;
            for (size_t i = 0; i < original_count; i++) {
                if (strcmp(record.as.record.fields[i].name, key.as.string) != 0) {
                    new_fields[new_index].name = malloc(strlen(record.as.record.fields[i].name) + 1);
                    strcpy(new_fields[new_index].name, record.as.record.fields[i].name);
                    new_fields[new_index].value = malloc(sizeof(Value));
                    *new_fields[new_index].value = value_copy(*record.as.record.fields[i].value);
                    new_index++;
                }
            }
        }

        Value result = value_record(new_fields, new_count);
        value_free(record);
        value_free(key);
        return result;
    }

    if (strcmp(expr->as.call.name, "count") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("count expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(value);
            return value_null();
        }

        int result;
        if (value.kind == VALUE_STRING) {
            result = (int)strlen(value.as.string);
        } else if (value.kind == VALUE_ARRAY) {
            result = (int)value.as.array.count;
        } else if (value.kind == VALUE_RECORD) {
            result = (int)value.as.record.count;
        } else {
            value_free(value);
            runtime_error_raise("count: argument must be a string, array, or record", 1003, "invalid argument type");
            return value_null();
        }

        value_free(value);
        return value_number((double)result);
    }

    if (strcmp(expr->as.call.name, "type") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("type expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(value);
            return value_null();
        }
        const char *name = builtin_type_name(value);
        value_free(value);
        return value_string(name);
    }

    if (strcmp(expr->as.call.name, "is_string") == 0 ||
        strcmp(expr->as.call.name, "is_number") == 0 ||
        strcmp(expr->as.call.name, "is_boolean") == 0 ||
        strcmp(expr->as.call.name, "is_array") == 0 ||
        strcmp(expr->as.call.name, "is_record") == 0 ||
        strcmp(expr->as.call.name, "is_nothing") == 0 ||
        strcmp(expr->as.call.name, "is_unknown") == 0) {
        if (expr->as.call.args.count != 1) {
            char message[128];
            snprintf(message, sizeof(message), "%s expects one argument", expr->as.call.name);
            runtime_error_raise(message, 1003, "invalid function call");
            return value_null();
        }
        Value value = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(value);
            return value_null();
        }
        int result = 0;
        if (strcmp(expr->as.call.name, "is_string") == 0) {
            result = value.kind == VALUE_STRING;
        } else if (strcmp(expr->as.call.name, "is_number") == 0) {
            result = value.kind == VALUE_NUMBER;
        } else if (strcmp(expr->as.call.name, "is_boolean") == 0) {
            result = value.kind == VALUE_BOOL;
        } else if (strcmp(expr->as.call.name, "is_array") == 0) {
            result = value.kind == VALUE_ARRAY;
        } else if (strcmp(expr->as.call.name, "is_record") == 0) {
            result = value.kind == VALUE_RECORD;
        } else if (strcmp(expr->as.call.name, "is_nothing") == 0) {
            result = value.kind == VALUE_NULL;
        } else if (strcmp(expr->as.call.name, "is_unknown") == 0) {
            result = value.kind == VALUE_UNKNOWN;
        }
        value_free(value);
        return value_bool(result);
    }

    if (strcmp(expr->as.call.name, "input") == 0) {
        if (expr->as.call.args.count > 1) {
            runtime_error_raise("input expects zero or one argument", 1003, "invalid function call");
            return value_null();
        }
        if (expr->as.call.args.count == 1) {
            Value prompt = eval_expr(expr->as.call.args.items[0]);
            if (prompt.kind != VALUE_STRING) {
                value_free(prompt);
                runtime_error_raise("input prompt must be a string", 1003, "invalid function call");
                return value_null();
            }
            printf("%s", prompt.as.string);
            fflush(stdout);
            value_free(prompt);
        }

        size_t capacity = 128;
        size_t length = 0;
        char *line = malloc(capacity);
        if (!line) {
            abort();
        }
        int ch = 0;
        while ((ch = fgetc(stdin)) != EOF && ch != '\n') {
            if (length + 1 >= capacity) {
                capacity *= 2;
                char *next = realloc(line, capacity);
                if (!next) {
                    abort();
                }
                line = next;
            }
            line[length++] = (char)ch;
        }
        if (length > 0 && line[length - 1] == '\r') {
            length--;
        }
        line[length] = '\0';
        Value result = value_string(line);
        free(line);
        return result;
    }

    if (strcmp(expr->as.call.name, "encode") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("encode expects one argument", 1003, "serialization");
            return value_null();
        }
        return builtin_encode_value(eval_expr(expr->as.call.args.items[0]));
    }

    if (strcmp(expr->as.call.name, "decode") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("decode expects one argument", 1003, "serialization");
            return value_null();
        }
        return builtin_decode_text(eval_expr(expr->as.call.args.items[0]));
    }

    if (strcmp(expr->as.call.name, "quote") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("quote expects one argument", 1003, "source generation");
            return value_null();
        }
        return builtin_quote_value(eval_expr(expr->as.call.args.items[0]));
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

    if (strcmp(expr->as.call.name, "find") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("find expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value value = eval_expr(expr->as.call.args.items[0]);
        Value target = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(value);
            value_free(target);
            return value_null();
        }

        if (value.kind == VALUE_STRING && target.kind == VALUE_STRING) {
            char *found = strstr(value.as.string, target.as.string);
            Value result = found
                ? value_number((double)(found - value.as.string))
                : value_null();
            value_free(value);
            value_free(target);
            return result;
        }

        if (value.kind == VALUE_ARRAY) {
            for (size_t i = 0; i < value.as.array.count; i++) {
                if (values_equal(value_copy(value.as.array.items[i]), value_copy(target))) {
                    value_free(value);
                    value_free(target);
                    return value_number((double)i);
                }
                if (error_action_pending()) {
                    value_free(value);
                    value_free(target);
                    return value_null();
                }
            }
            value_free(value);
            value_free(target);
            return value_null();
        }

        if (value.kind == VALUE_STRING || target.kind == VALUE_STRING) {
            value_free(value);
            value_free(target);
            runtime_error_raise("find string search expects string arguments", 1003, "invalid function call");
            return value_null();
        }

        value_free(value);
        value_free(target);
        runtime_error_raise("find expects string or array as first argument", 1003, "invalid function call");
        return value_null();
    }

    if (strcmp(expr->as.call.name, "contains") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("contains expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value array = eval_expr(expr->as.call.args.items[0]);
        Value target = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(array);
            value_free(target);
            return value_null();
        }
        if (array.kind != VALUE_ARRAY) {
            value_free(array);
            value_free(target);
            runtime_error_raise("contains expects an array", 1003, "invalid function call");
            return value_null();
        }

        size_t index = 0;
        int found = array_find_index(array, target, &index);
        value_free(array);
        value_free(target);
        if (error_action_pending()) {
            return value_null();
        }
        return value_bool(found);
    }

    if (strcmp(expr->as.call.name, "remove_value") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("remove_value expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        AstExpr *array_expr = expr->as.call.args.items[0];
        Value target = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(target);
            return value_null();
        }

        if (expr_is_lvalue_path(array_expr)) {
            Value *array = resolve_lvalue_ref(array_expr);
            if (!array) {
                value_free(target);
                return value_null();
            }
            int changed = 0;
            Value result = remove_value_from_array_ref(array, target, &changed);
            if (changed && !error_action_pending() && !notify_lvalue_mutation(array_expr)) {
                return result;
            }
            return result;
        }

        Value array = eval_expr(array_expr);
        if (error_action_pending()) {
            value_free(array);
            value_free(target);
            return value_null();
        }
        return remove_value_from_array_value(array, target);
    }

    if (strcmp(expr->as.call.name, "find_by") == 0) {
        if (expr->as.call.args.count != 3) {
            runtime_error_raise("find_by expects three arguments", 1003, "invalid function call");
            return value_null();
        }
        Value records = eval_expr(expr->as.call.args.items[0]);
        Value field_name = eval_expr(expr->as.call.args.items[1]);
        Value target = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(records);
            value_free(field_name);
            value_free(target);
            return value_null();
        }
        if (records.kind != VALUE_ARRAY || field_name.kind != VALUE_STRING) {
            value_free(records);
            value_free(field_name);
            value_free(target);
            runtime_error_raise("find_by expects array, string, value", 1003, "invalid function call");
            return value_null();
        }

        for (size_t i = 0; i < records.as.array.count; i++) {
            Value *item = &records.as.array.items[i];
            if (item->kind != VALUE_RECORD) {
                continue;
            }
            RecordField *field = record_find(item, field_name.as.string);
            if (!field) {
                continue;
            }
            if (values_equal(value_copy(*field->value), value_copy(target))) {
                value_free(records);
                value_free(field_name);
                value_free(target);
                return value_number((double)i);
            }
            if (error_action_pending()) {
                value_free(records);
                value_free(field_name);
                value_free(target);
                return value_null();
            }
        }
        value_free(records);
        value_free(field_name);
        value_free(target);
        return value_null();
    }

    if (strcmp(expr->as.call.name, "join_from") == 0) {
        if (expr->as.call.args.count != 3) {
            runtime_error_raise("join_from expects three arguments", 1003, "invalid function call");
            return value_null();
        }
        Value array = eval_expr(expr->as.call.args.items[0]);
        Value start_value = eval_expr(expr->as.call.args.items[1]);
        Value separator = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(array);
            value_free(start_value);
            value_free(separator);
            return value_null();
        }
        if (array.kind != VALUE_ARRAY || start_value.kind != VALUE_NUMBER) {
            value_free(array);
            value_free(start_value);
            value_free(separator);
            runtime_error_raise("join_from expects array, number, string", 1003, "invalid function call");
            return value_null();
        }
        int start_index = 0;
        if (!array_index_from_value(start_value, "join_from", &start_index)) {
            value_free(array);
            value_free(separator);
            return value_null();
        }
        if (start_index < 0 || (size_t)start_index >= array.as.array.count) {
            value_free(array);
            value_free(separator);
            return value_string("");
        }

        size_t count = array.as.array.count - (size_t)start_index;
        Value *items = malloc(sizeof(Value) * count);
        if (!items) {
            abort();
        }
        for (size_t i = 0; i < count; i++) {
            items[i] = value_copy(array.as.array.items[(size_t)start_index + i]);
        }
        value_free(array);
        return builtin_join_value(value_array(items, count), separator);
    }

    if (strcmp(expr->as.call.name, "first") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("first expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value array = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(array);
            return value_null();
        }
        if (array.kind != VALUE_ARRAY) {
            value_free(array);
            runtime_error_raise("first expects an array", 1003, "invalid function call");
            return value_null();
        }
        if (array.as.array.count == 0) {
            value_free(array);
            return value_null();
        }
        Value result = value_copy(array.as.array.items[0]);
        value_free(array);
        return result;
    }

    if (strcmp(expr->as.call.name, "rest") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("rest expects one argument", 1003, "invalid function call");
            return value_null();
        }
        return array_rest_value(eval_expr(expr->as.call.args.items[0]));
    }

    if (strcmp(expr->as.call.name, "left") == 0 ||
        strcmp(expr->as.call.name, "right") == 0) {
        const char *name = expr->as.call.name;
        if (expr->as.call.args.count != 2) {
            char message[80];
            snprintf(message, sizeof(message), "%s expects two arguments", name);
            runtime_error_raise(message, 1003, "invalid function call");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        Value count_value = eval_expr(expr->as.call.args.items[1]);
        if (text.kind != VALUE_STRING || count_value.kind != VALUE_NUMBER) {
            value_free(text);
            value_free(count_value);
            char message[96];
            snprintf(message, sizeof(message), "%s expects string and number", name);
            runtime_error_raise(message, 1003, "invalid function call");
            return value_null();
        }
        size_t text_len = strlen(text.as.string);
        int requested = (int)count_value.as.number;
        size_t count = requested < 0 ? 0 : (size_t)requested;
        if (count > text_len) {
            count = text_len;
        }
        size_t start = strcmp(name, "right") == 0 ? text_len - count : 0;
        char *result_text = malloc(count + 1);
        if (!result_text) {
            abort();
        }
        memcpy(result_text, text.as.string + start, count);
        result_text[count] = '\0';
        Value result = value_string(result_text);
        free(result_text);
        value_free(text);
        value_free(count_value);
        return result;
    }

    if (strcmp(expr->as.call.name, "mid") == 0) {
        if (expr->as.call.args.count != 3 && expr->as.call.args.count != 4) {
            runtime_error_raise("mid expects three or four arguments", 1003, "invalid function call");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        Value start_value = eval_expr(expr->as.call.args.items[1]);
        Value count_value = eval_expr(expr->as.call.args.items[2]);
        if (text.kind != VALUE_STRING ||
            start_value.kind != VALUE_NUMBER ||
            count_value.kind != VALUE_NUMBER) {
            value_free(text);
            value_free(start_value);
            value_free(count_value);
            runtime_error_raise("mid expects string, number, number", 1003, "invalid function call");
            return value_null();
        }

        size_t text_len = strlen(text.as.string);
        int raw_start = (int)start_value.as.number;
        int raw_count = (int)count_value.as.number;
        size_t start = raw_start < 0 ? 0 : (size_t)raw_start;
        size_t count = raw_count < 0 ? 0 : (size_t)raw_count;

        if (expr->as.call.args.count == 3) {
            if (start >= text_len) {
                value_free(text);
                value_free(start_value);
                value_free(count_value);
                return value_string("");
            }
            if (count > text_len - start) {
                count = text_len - start;
            }
            char *result_text = malloc(count + 1);
            if (!result_text) {
                abort();
            }
            memcpy(result_text, text.as.string + start, count);
            result_text[count] = '\0';
            Value result = value_string(result_text);
            free(result_text);
            value_free(text);
            value_free(start_value);
            value_free(count_value);
            return result;
        }

        Value replacement = eval_expr(expr->as.call.args.items[3]);
        if (replacement.kind != VALUE_STRING) {
            value_free(text);
            value_free(start_value);
            value_free(count_value);
            value_free(replacement);
            runtime_error_raise("mid replacement expects a string", 1003, "invalid function call");
            return value_null();
        }
        if (start > text_len) {
            start = text_len;
        }
        if (count > text_len - start) {
            count = text_len - start;
        }
        size_t replacement_len = strlen(replacement.as.string);
        size_t result_len = start + replacement_len + (text_len - start - count);
        char *result_text = malloc(result_len + 1);
        if (!result_text) {
            abort();
        }
        memcpy(result_text, text.as.string, start);
        memcpy(result_text + start, replacement.as.string, replacement_len);
        memcpy(result_text + start + replacement_len,
               text.as.string + start + count,
               text_len - start - count);
        result_text[result_len] = '\0';
        Value result = value_string(result_text);
        free(result_text);
        value_free(text);
        value_free(start_value);
        value_free(count_value);
        value_free(replacement);
        return result;
    }

    if (strcmp(expr->as.call.name, "trim") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("trim expects one argument", 1003, "invalid function call");
            return value_null();
        }
        return builtin_trim_value(eval_expr(expr->as.call.args.items[0]));
    }

    if (strcmp(expr->as.call.name, "split") == 0) {
        if (expr->as.call.args.count != 1 && expr->as.call.args.count != 2) {
            runtime_error_raise("split expects one or two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        Value separator = value_null();
        int has_separator = expr->as.call.args.count == 2;
        if (has_separator) {
            separator = eval_expr(expr->as.call.args.items[1]);
        }
        return builtin_split_value(text, separator, has_separator);
    }

    if (strcmp(expr->as.call.name, "join") == 0) {
        if (expr->as.call.args.count != 1 && expr->as.call.args.count != 2) {
            runtime_error_raise("join expects one or two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value array = eval_expr(expr->as.call.args.items[0]);
        Value separator = value_string(" ");
        if (expr->as.call.args.count == 2) {
            value_free(separator);
            separator = eval_expr(expr->as.call.args.items[1]);
        }
        return builtin_join_value(array, separator);
    }

    if (strcmp(expr->as.call.name, "append") == 0 ||
        strcmp(expr->as.call.name, "prepend") == 0) {
        int prepend = strcmp(expr->as.call.name, "prepend") == 0;
        if (expr->as.call.args.count != 2) {
            runtime_error_raise(prepend ? "prepend expects two arguments" : "append expects two arguments",
                                1003,
                                "invalid function call");
            return value_null();
        }
        AstExpr *array_expr = expr->as.call.args.items[0];
        if (!prepend && array_expr->kind == AST_EXPR_IDENT) {
            Symbol *symbol = env_find(array_expr->as.ident);
            if (symbol && symbol->value.kind == VALUE_FILE) {
                return eval_file_call(expr);
            }
        }
        Value item = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(item);
            return value_null();
        }

        if (expr_is_lvalue_path(array_expr)) {
            if (!prepend && !webserver_validate_response_append(array_expr, item)) {
                value_free(item);
                return value_null();
            }
            Value *array = resolve_lvalue_ref(array_expr);
            if (!array) {
                value_free(item);
                return value_null();
            }
            Value result = append_to_array_ref(array, item, prepend);
            if (!error_action_pending()) {
                if (!notify_lvalue_mutation(array_expr)) {
                    return result;
                }
            }
            return result;
        }

        Value array = eval_expr(array_expr);
        if (error_action_pending()) {
            value_free(array);
            value_free(item);
            return value_null();
        }
        return append_to_array_value(array, item, prepend);
    }

    if (strcmp(expr->as.call.name, "insert") == 0) {
        if (expr->as.call.args.count != 3) {
            runtime_error_raise("insert expects three arguments", 1003, "invalid function call");
            return value_null();
        }
        int index = 0;
        Value index_value = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending() || !array_index_from_value(index_value, "insert", &index)) {
            return value_null();
        }
        Value item = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(item);
            return value_null();
        }

        AstExpr *array_expr = expr->as.call.args.items[0];
        if (expr_is_lvalue_path(array_expr)) {
            Value *array = resolve_lvalue_ref(array_expr);
            if (!array) {
                value_free(item);
                return value_null();
            }
            Value result = insert_into_array_ref(array, index, item);
            if (!error_action_pending() && !notify_lvalue_mutation(array_expr)) {
                return result;
            }
            return result;
        }

        Value array = eval_expr(array_expr);
        if (error_action_pending()) {
            value_free(array);
            value_free(item);
            return value_null();
        }
        return insert_into_array_value(array, index, item);
    }

    if (strcmp(expr->as.call.name, "remove") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("remove expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        int index = 0;
        Value index_value = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending() || !array_index_from_value(index_value, "remove", &index)) {
            return value_null();
        }

        AstExpr *array_expr = expr->as.call.args.items[0];
        if (expr_is_lvalue_path(array_expr)) {
            Value *array = resolve_lvalue_ref(array_expr);
            if (!array) {
                return value_null();
            }
            Value result = remove_from_array_ref(array, index);
            if (!error_action_pending() && !notify_lvalue_mutation(array_expr)) {
                return result;
            }
            return result;
        }

        Value array = eval_expr(array_expr);
        if (error_action_pending()) {
            value_free(array);
            return value_null();
        }
        return remove_from_array_value(array, index);
    }

    if (strcmp(expr->as.call.name, "take_first") == 0 ||
        strcmp(expr->as.call.name, "take_last") == 0) {
        int take_last = strcmp(expr->as.call.name, "take_last") == 0;
        if (expr->as.call.args.count != 1) {
            runtime_error_raise(take_last ? "take_last expects one argument" : "take_first expects one argument",
                                1003,
                                "invalid function call");
            return value_null();
        }

        AstExpr *array_expr = expr->as.call.args.items[0];
        if (expr_is_lvalue_path(array_expr)) {
            Value *array = resolve_lvalue_ref(array_expr);
            if (!array) {
                return value_null();
            }
            Value result = take_from_array_ref(array, take_last);
            if (!error_action_pending()) {
                if (!notify_lvalue_mutation(array_expr)) {
                    return result;
                }
            }
            return result;
        }

        Value array = eval_expr(array_expr);
        if (error_action_pending()) {
            value_free(array);
            return value_null();
        }
        return take_from_array_value(array, take_last);
    }

    if (strcmp(expr->as.call.name, "reverse") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("reverse expects one argument", 1003, "invalid function call");
            return value_null();
        }

        AstExpr *array_expr = expr->as.call.args.items[0];
        if (expr_is_lvalue_path(array_expr)) {
            Value *array = resolve_lvalue_ref(array_expr);
            if (!array) {
                return value_null();
            }
            int changed = 0;
            Value result = reverse_array_ref(array, &changed);
            if (changed && !error_action_pending() && !notify_lvalue_mutation(array_expr)) {
                return result;
            }
            return result;
        }

        Value array = eval_expr(array_expr);
        if (error_action_pending()) {
            value_free(array);
            return value_null();
        }
        return reverse_array_value(array);
    }

    if (strcmp(expr->as.call.name, "unique") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("unique expects one argument", 1003, "invalid function call");
            return value_null();
        }

        AstExpr *array_expr = expr->as.call.args.items[0];
        if (expr_is_lvalue_path(array_expr)) {
            Value *array = resolve_lvalue_ref(array_expr);
            if (!array) {
                return value_null();
            }
            int changed = 0;
            Value result = unique_array_ref(array, &changed);
            if (changed && !error_action_pending() && !notify_lvalue_mutation(array_expr)) {
                return result;
            }
            return result;
        }

        Value array = eval_expr(array_expr);
        if (error_action_pending()) {
            value_free(array);
            return value_null();
        }
        return unique_array_value(array);
    }

    if (strcmp(expr->as.call.name, "sort") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("sort expects one argument", 1003, "invalid function call");
            return value_null();
        }

        AstExpr *array_expr = expr->as.call.args.items[0];
        if (expr_is_lvalue_path(array_expr)) {
            Value *array = resolve_lvalue_ref(array_expr);
            if (!array) {
                return value_null();
            }
            int changed = 0;
            Value result = sort_array_ref(array, &changed);
            if (changed && !error_action_pending() && !notify_lvalue_mutation(array_expr)) {
                return result;
            }
            return result;
        }

        Value array = eval_expr(array_expr);
        if (error_action_pending()) {
            value_free(array);
            return value_null();
        }
        return sort_array_value(array);
    }

    if (strcmp(expr->as.call.name, "len") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("len expects one argument", 1003, "invalid function call");
            return value_null();
        }
        return builtin_len_value(eval_expr(expr->as.call.args.items[0]));
    }

    if (strcmp(expr->as.call.name, "join_path") == 0 ||
        strcmp(expr->as.call.name, "file_name") == 0 ||
        strcmp(expr->as.call.name, "directory_name") == 0 ||
        strcmp(expr->as.call.name, "extension") == 0) {
        return eval_path_call(expr);
    }

    if (strcmp(expr->as.call.name, "exists") == 0 ||
        strcmp(expr->as.call.name, "read") == 0 ||
        strcmp(expr->as.call.name, "read_lines") == 0 ||
        strcmp(expr->as.call.name, "write") == 0 ||
        strcmp(expr->as.call.name, "append") == 0 ||
        strcmp(expr->as.call.name, "delete") == 0 ||
        strcmp(expr->as.call.name, "copy") == 0 ||
        strcmp(expr->as.call.name, "move") == 0 ||
        strcmp(expr->as.call.name, "list_files") == 0 ||
        strcmp(expr->as.call.name, "make_dir") == 0 ||
        strcmp(expr->as.call.name, "remove_dir") == 0 ||
        strcmp(expr->as.call.name, "overwrite") == 0 ||
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

    const char *name = expr->as.call.name;
    if (gbasic_builtin_function(name)) {
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

    FunctionDef *function = function_resolve(NULL, expr->as.call.name);
    if (function) {
        return eval_user_function(expr, function);
    }

    char message[256];
    snprintf(message, sizeof(message), "invalid function call: %s", expr->as.call.name);
    runtime_error_raise(message, 1003, "invalid function call");
    return value_null();
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
        char *inner = malloc(len - 1);
        if (!inner) {
            abort();
        }
        memcpy(inner, text + 1, len - 2);
        inner[len - 2] = '\0';
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

static const char *builtin_modifier_args_text(const char *phrase, const char *name) {
    size_t name_len = strlen(name);
    while (*phrase == ' ' || *phrase == '\t') {
        phrase++;
    }
    if (strncmp(phrase, name, name_len) != 0) {
        return NULL;
    }
    if (phrase[name_len] == '\0') {
        return phrase + name_len;
    }
    if (phrase[name_len] == ' ' || phrase[name_len] == '\t') {
        const char *args = phrase + name_len;
        while (*args == ' ' || *args == '\t') {
            args++;
        }
        return args;
    }
    return NULL;
}

static int modifier_args_empty(const char *args_text) {
    while (*args_text == ' ' || *args_text == '\t') {
        args_text++;
    }
    return *args_text == '\0';
}

static int modifier_args_have_comma(const char *args_text) {
    int in_string = 0;
    int escape = 0;
    for (const char *p = args_text; *p; p++) {
        if (escape) {
            escape = 0;
            continue;
        }
        if (*p == '\\' && in_string) {
            escape = 1;
            continue;
        }
        if (*p == '"') {
            in_string = !in_string;
            continue;
        }
        if (*p == ',' && !in_string) {
            return 1;
        }
    }
    return 0;
}

static Value eval_optional_modifier_arg(const char *modifier_name,
                                        const char *args_text,
                                        int *has_arg) {
    *has_arg = 0;
    if (modifier_args_empty(args_text)) {
        return value_null();
    }
    if (modifier_args_have_comma(args_text)) {
        char message[128];
        snprintf(message, sizeof(message), "%s modifier expects zero or one argument", modifier_name);
        runtime_error_raise(message, 1003, "modifier");
        return value_null();
    }
    *has_arg = 1;
    return eval_modifier_arg_text(args_text);
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
    if (result.did_break || result.did_continue) {
        runtime_error_raise(result.did_break ? "break outside loop" : "continue outside loop",
                            1003,
                            "invalid control flow");
        value_free(result.value);
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
    if (result.did_break || result.did_continue) {
        runtime_error_raise(result.did_break ? "break outside loop" : "continue outside loop",
                            1003,
                            "invalid control flow");
        value_free(result.value);
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

    DateTimePrecision lens = PREC_YEAR;
    if (expr->as.binary.modifier.name &&
        !expr->as.binary.modifier.library &&
        datetime_lens_precision(expr->as.binary.modifier.name, &lens)) {
        int ok = 0;
        Value lensed_left = apply_datetime_lens_to_value(left,
                                                         lens,
                                                         expr->as.binary.modifier.name,
                                                         &ok);
        if (!ok) {
            value_free(right);
            return value_null();
        }
        Value lensed_right = apply_datetime_lens_to_value(right,
                                                          lens,
                                                          expr->as.binary.modifier.name,
                                                          &ok);
        if (!ok) {
            value_free(lensed_left);
            return value_null();
        }
        AstExpr fake = {0};
        fake.kind = AST_EXPR_BINARY;
        fake.as.binary.op = expr->as.binary.op;
        fake.as.binary.modifier = ast_modifier_none();
        return eval_comparison(&fake, lensed_left, lensed_right);
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
        int cmp = datetime_compare_exact(left.as.datetime, right.as.datetime);
        result = comparison_result_from_cmp(op, cmp);
    } else if (left.kind == VALUE_DATETIME || right.kind == VALUE_DATETIME) {
        runtime_error_raise("date/time comparison requires date/time values", 1003, "datetime");
        value_free(left);
        value_free(right);
        return value_null();
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
        else if (strcmp(op, "!>=") == 0) result = cmp < 0;
        else if (strcmp(op, "!<=") == 0) result = cmp > 0;
    } else if (left.kind == VALUE_MONEY && right.kind == VALUE_MONEY) {
        long long a = left.as.cents;
        long long b = right.as.cents;
        if (strcmp(op, "=") == 0) result = a == b;
        else if (strcmp(op, "!=") == 0) result = a != b;
        else if (strcmp(op, ">") == 0) result = a > b;
        else if (strcmp(op, "<") == 0) result = a < b;
        else if (strcmp(op, ">=") == 0) result = a >= b;
        else if (strcmp(op, "<=") == 0) result = a <= b;
        else if (strcmp(op, "!>") == 0) result = !(a > b);
        else if (strcmp(op, "!<") == 0) result = !(a < b);
        else if (strcmp(op, "!>=") == 0) result = !(a >= b);
        else if (strcmp(op, "!<=") == 0) result = !(a <= b);
    } else if (left.kind == VALUE_MONEY || right.kind == VALUE_MONEY) {
        runtime_error_raise("money comparison requires money values", 1003, "money");
        value_free(left);
        value_free(right);
        return value_null();
    } else if (left.kind == VALUE_NULL || right.kind == VALUE_NULL ||
               left.kind == VALUE_UNKNOWN || right.kind == VALUE_UNKNOWN) {
        int equal = left.kind == right.kind &&
            (left.kind == VALUE_NULL || left.kind == VALUE_UNKNOWN);
        if (strcmp(op, "=") == 0) result = equal;
        else if (strcmp(op, "!=") == 0) result = !equal;
        else {
            runtime_error_raise("unknown and nothing support only = and !=", 1003, "comparison");
            value_free(left);
            value_free(right);
            return value_null();
        }
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
        else if (strcmp(op, "!>=") == 0) result = !(a >= b);
        else if (strcmp(op, "!<=") == 0) result = !(a <= b);
    }

    value_free(left);
    value_free(right);
    return value_bool(result);
}

static Value eval_binary(AstExpr *expr) {
    const char *op = expr->as.binary.op;
    int previous_line = current_line;
    int previous_column = current_column;
    if (expr->line > 0) {
        current_line = expr->line;
        current_column = expr->column > 0 ? expr->column : previous_column;
    }

    if (strcmp(op, "and") == 0) {
        Value left = eval_expr(expr->as.binary.left);
        int left_truth = value_truthy(left);
        value_free(left);
        if (!left_truth) {
            current_line = previous_line;
            current_column = previous_column;
            return value_bool(0);
        }
        Value right = eval_expr(expr->as.binary.right);
        int right_truth = value_truthy(right);
        value_free(right);
        current_line = previous_line;
        current_column = previous_column;
        return value_bool(right_truth);
    }

    if (strcmp(op, "or") == 0) {
        Value left = eval_expr(expr->as.binary.left);
        int left_truth = value_truthy(left);
        value_free(left);
        if (left_truth) {
            current_line = previous_line;
            current_column = previous_column;
            return value_bool(1);
        }
        Value right = eval_expr(expr->as.binary.right);
        int right_truth = value_truthy(right);
        value_free(right);
        current_line = previous_line;
        current_column = previous_column;
        return value_bool(right_truth);
    }

    Value left = eval_expr(expr->as.binary.left);
    if (error_action_pending()) {
        value_free(left);
        current_line = previous_line;
        current_column = previous_column;
        return value_null();
    }
    Value right = eval_expr(expr->as.binary.right);
    if (error_action_pending()) {
        value_free(left);
        value_free(right);
        current_line = previous_line;
        current_column = previous_column;
        return value_null();
    }

    if (strcmp(op, "=") == 0 ||
        strcmp(op, "!=") == 0 ||
        strcmp(op, ">") == 0 ||
        strcmp(op, "<") == 0 ||
        strcmp(op, ">=") == 0 ||
        strcmp(op, "<=") == 0 ||
        strcmp(op, "!>") == 0 ||
        strcmp(op, "!<") == 0 ||
        strcmp(op, "!>=") == 0 ||
        strcmp(op, "!<=") == 0) {
        Value result = eval_comparison(expr, left, right);
        current_line = previous_line;
        current_column = previous_column;
        return result;
    }

    if ((strcmp(op, "+") == 0 || strcmp(op, "-") == 0) &&
        left.kind == VALUE_DATETIME &&
        right.kind == VALUE_DURATION) {
        DateTime result = add_duration_to_datetime(left.as.datetime,
                                                   right.as.duration,
                                                   strcmp(op, "+") == 0 ? 1 : -1);
        value_free(left);
        value_free(right);
        current_line = previous_line;
        current_column = previous_column;
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
        current_line = previous_line;
        current_column = previous_column;
        return value_datetime(result);
    }

    if (strcmp(op, "+") == 0 &&
        (left.kind == VALUE_STRING || right.kind == VALUE_STRING)) {
        Value left_text = builtin_string_value(left);
        if (error_action_pending()) {
            value_free(right);
            current_line = previous_line;
            current_column = previous_column;
            return value_null();
        }
        Value right_text = builtin_string_value(right);
        if (error_action_pending()) {
            value_free(left_text);
            current_line = previous_line;
            current_column = previous_column;
            return value_null();
        }
        size_t left_len = strlen(left_text.as.string);
        size_t right_len = strlen(right_text.as.string);
        char *combined = malloc(left_len + right_len + 1);
        if (!combined) {
            abort();
        }
        memcpy(combined, left_text.as.string, left_len);
        memcpy(combined + left_len, right_text.as.string, right_len + 1);
        Value result = value_string(combined);
        free(combined);
        value_free(left_text);
        value_free(right_text);
        current_line = previous_line;
        current_column = previous_column;
        return result;
    }

    if (left.kind == VALUE_MONEY || right.kind == VALUE_MONEY) {
        if (left.kind == VALUE_MONEY && right.kind == VALUE_MONEY &&
            (strcmp(op, "+") == 0 || strcmp(op, "-") == 0)) {
            long long cents = strcmp(op, "+") == 0
                ? left.as.cents + right.as.cents
                : left.as.cents - right.as.cents;
            value_free(left);
            value_free(right);
            current_line = previous_line;
            current_column = previous_column;
            return value_money(cents);
        }
        if (left.kind == VALUE_MONEY && right.kind == VALUE_NUMBER &&
            (strcmp(op, "*") == 0 || strcmp(op, "/") == 0)) {
            double number = right.as.number;
            if (strcmp(op, "/") == 0 && number == 0.0) {
                value_free(left);
                value_free(right);
                runtime_error_raise("division by zero", 1002, "division");
                current_line = previous_line;
                current_column = previous_column;
                return value_null();
            }
            double amount = strcmp(op, "*") == 0
                ? (double)left.as.cents * number
                : (double)left.as.cents / number;
            value_free(left);
            value_free(right);
            current_line = previous_line;
            current_column = previous_column;
            return value_money(round_to_cents(amount / 100.0));
        }
        if (left.kind == VALUE_NUMBER && right.kind == VALUE_MONEY && strcmp(op, "*") == 0) {
            double amount = left.as.number * (double)right.as.cents;
            value_free(left);
            value_free(right);
            current_line = previous_line;
            current_column = previous_column;
            return value_money(round_to_cents(amount / 100.0));
        }
        value_free(left);
        value_free(right);
        runtime_error_raise("invalid money operation", 1003, "money");
        current_line = previous_line;
        current_column = previous_column;
        return value_null();
    }

    if (left.kind == VALUE_UNKNOWN || right.kind == VALUE_UNKNOWN) {
        value_free(left);
        value_free(right);
        runtime_error_raise("unknown cannot be used in arithmetic", 1003, "unknown");
        current_line = previous_line;
        current_column = previous_column;
        return value_null();
    }

    double a = 0.0;
    double b = 0.0;
    if (!value_number_for_arithmetic(left, op, &a)) {
        value_free(left);
        value_free(right);
        current_line = previous_line;
        current_column = previous_column;
        return value_null();
    }
    if (!value_number_for_arithmetic(right, op, &b)) {
        value_free(left);
        value_free(right);
        current_line = previous_line;
        current_column = previous_column;
        return value_null();
    }
    value_free(left);
    value_free(right);

    if (strcmp(op, "+") == 0) {
        current_line = previous_line;
        current_column = previous_column;
        return value_number(a + b);
    }
    if (strcmp(op, "-") == 0) {
        current_line = previous_line;
        current_column = previous_column;
        return value_number(a - b);
    }
    if (strcmp(op, "*") == 0) {
        current_line = previous_line;
        current_column = previous_column;
        return value_number(a * b);
    }
    if (strcmp(op, "/") == 0) {
        if (b == 0.0) {
            runtime_error_raise("division by zero", 1002, "division");
            current_line = previous_line;
            current_column = previous_column;
            return value_null();
        }
        current_line = previous_line;
        current_column = previous_column;
        return value_number(a / b);
    }

    current_line = previous_line;
    current_column = previous_column;
    return value_null();
}

static int values_equal_for_consider(Value subject, Value candidate) {
    AstExpr comparison = {0};
    comparison.kind = AST_EXPR_BINARY;
    comparison.as.binary.op = "=";
    comparison.as.binary.modifier = ast_modifier_none();

    Value result = eval_comparison(&comparison, value_copy(subject), value_copy(candidate));
    if (error_action_pending()) {
        value_free(result);
        return 0;
    }
    int equal = result.kind == VALUE_BOOL && result.as.boolean;
    value_free(result);
    return equal;
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
    case AST_EXPR_NULL:
        return value_null();
    case AST_EXPR_UNKNOWN:
        return value_unknown();
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
                value_free(array);
                value_free(index);
                return value_unknown();
            }
            Value result = value_copy(*field->value);
            value_free(array);
            value_free(index);
            return result;
        }
        value_free(array);
        value_free(index);
        runtime_error_raise("indexing expects array[number] or record[string]", 1003, "indexing");
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
            Value *widget = gui_window_lookup_widget_ref(&object, expr->as.field.field);
            if (widget) {
                Value result = value_copy(*widget);
                value_free(object);
                return result;
            }
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
            int previous_line = current_line;
            int previous_column = current_column;
            if (expr->line > 0) {
                current_line = expr->line;
                current_column = expr->column > 0 ? expr->column : previous_column;
            }
            double number = 0.0;
            if (!value_number_for_arithmetic(value, expr->as.unary.op, &number)) {
                value_free(value);
                current_line = previous_line;
                current_column = previous_column;
                return value_null();
            }
            value_free(value);
            current_line = previous_line;
            current_column = previous_column;
            return value_number(-number);
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
        if (value.kind != VALUE_NUMBER) {
            runtime_error_raise("USD modifier expects a number", 1003, "money");
            value_free(value);
            return value_null();
        }
        long long cents = round_to_cents(value.as.number);
        value_free(value);
        return value_money(cents);
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
    DateTimePrecision lens = PREC_YEAR;
    if (!modifier.library && datetime_lens_precision(modifier.name, &lens)) {
        int ok = 0;
        return apply_datetime_lens_to_value(value, lens, modifier.name, &ok);
    }
    const char *builtin_args = NULL;
    if (!modifier.library &&
        (builtin_args = builtin_modifier_args_text(modifier.name, "trimmed")) != NULL) {
        if (!modifier_args_empty(builtin_args)) {
            runtime_error_raise("trimmed modifier expects no arguments", 1003, "modifier");
            value_free(value);
            return value_null();
        }
        return builtin_trim_value(value);
    }
    if (!modifier.library &&
        (builtin_args = builtin_modifier_args_text(modifier.name, "lowered")) != NULL) {
        if (!modifier_args_empty(builtin_args)) {
            runtime_error_raise("lowered modifier expects no arguments", 1003, "modifier");
            value_free(value);
            return value_null();
        }
        return builtin_lower_value(value);
    }
    if (!modifier.library &&
        (builtin_args = builtin_modifier_args_text(modifier.name, "uppered")) != NULL) {
        if (!modifier_args_empty(builtin_args)) {
            runtime_error_raise("uppered modifier expects no arguments", 1003, "modifier");
            value_free(value);
            return value_null();
        }
        return builtin_upper_value(value);
    }
    if (!modifier.library &&
        (builtin_args = builtin_modifier_args_text(modifier.name, "split")) != NULL) {
        int has_arg = 0;
        Value separator = eval_optional_modifier_arg("split", builtin_args, &has_arg);
        if (error_action_pending()) {
            value_free(value);
            value_free(separator);
            return value_null();
        }
        return builtin_split_value(value, separator, has_arg);
    }
    if (!modifier.library &&
        (builtin_args = builtin_modifier_args_text(modifier.name, "join")) != NULL) {
        int has_arg = 0;
        Value separator = eval_optional_modifier_arg("join", builtin_args, &has_arg);
        if (error_action_pending()) {
            value_free(value);
            value_free(separator);
            return value_null();
        }
        if (!has_arg) {
            value_free(separator);
            separator = value_string(" ");
        }
        return builtin_join_value(value, separator);
    }
    if (!modifier.library &&
        (builtin_args = builtin_modifier_args_text(modifier.name, "length")) != NULL) {
        if (!modifier_args_empty(builtin_args)) {
            runtime_error_raise("length modifier expects no arguments", 1003, "modifier");
            value_free(value);
            return value_null();
        }
        return builtin_len_value(value);
    }
    if (!modifier.library &&
        (builtin_args = builtin_modifier_args_text(modifier.name, "number")) != NULL) {
        if (!modifier_args_empty(builtin_args)) {
            runtime_error_raise("number modifier expects no arguments", 1003, "modifier");
            value_free(value);
            return value_null();
        }
        return builtin_number_modifier_value(value);
    }
    if (!modifier.library &&
        (builtin_args = builtin_modifier_args_text(modifier.name, "string")) != NULL) {
        if (!modifier_args_empty(builtin_args)) {
            runtime_error_raise("string modifier expects no arguments", 1003, "modifier");
            value_free(value);
            return value_null();
        }
        return builtin_string_modifier_value(value);
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

static const char *lvalue_root_name(AstExpr *target) {
    if (!target) {
        return NULL;
    }
    switch (target->kind) {
    case AST_EXPR_IDENT:
        return target->as.ident;
    case AST_EXPR_FIELD:
        return lvalue_root_name(target->as.field.object);
    case AST_EXPR_INDEX:
        return lvalue_root_name(target->as.index.array);
    default:
        return NULL;
    }
}

static char *lvalue_watch_path(AstExpr *target) {
    if (!target) {
        return NULL;
    }
    switch (target->kind) {
    case AST_EXPR_IDENT:
        return copy_string(target->as.ident);
    case AST_EXPR_FIELD: {
        char *object_path = lvalue_watch_path(target->as.field.object);
        if (!object_path) {
            return NULL;
        }
        size_t object_length = strlen(object_path);
        size_t field_length = strlen(target->as.field.field);
        char *path = malloc(object_length + 1 + field_length + 1);
        if (!path) {
            abort();
        }
        snprintf(path,
                 object_length + 1 + field_length + 1,
                 "%s.%s",
                 object_path,
                 target->as.field.field);
        free(object_path);
        return path;
    }
    case AST_EXPR_INDEX:
        return lvalue_watch_path(target->as.index.array);
    default:
        return NULL;
    }
}

static Value *resolve_lvalue_ref(AstExpr *target) {
    switch (target->kind) {
    case AST_EXPR_IDENT: {
        Symbol *symbol = env_find(target->as.ident);
        if (!symbol) {
            char message[256];
            snprintf(message, sizeof(message), "undefined variable: %s", target->as.ident);
            runtime_error_raise(message, 1001, "undefined variable");
            return NULL;
        }
        return &symbol->value;
    }
    case AST_EXPR_FIELD: {
        Value *object = resolve_lvalue_ref(target->as.field.object);
        if (!object) {
            return NULL;
        }
        if (object->kind != VALUE_RECORD) {
            runtime_error_raise("field assignment target expects a record", 1003, "assignment");
            return NULL;
        }
        RecordField *field = record_find(object, target->as.field.field);
        if (!field) {
            Value *widget = gui_window_lookup_widget_ref(object, target->as.field.field);
            if (widget) {
                return widget;
            }
            char message[256];
            snprintf(message, sizeof(message), "unknown record field: %s", target->as.field.field);
            runtime_error_raise(message, 1003, "assignment");
            return NULL;
        }
        return field->value;
    }
    case AST_EXPR_INDEX: {
        Value *container = resolve_lvalue_ref(target->as.index.array);
        if (!container) {
            return NULL;
        }
        Value index = eval_expr(target->as.index.index);
        if (error_action_pending()) {
            value_free(index);
            return NULL;
        }
        if (container->kind == VALUE_ARRAY && index.kind == VALUE_NUMBER) {
            int position = (int)index.as.number;
            value_free(index);
            if (position < 0 || (size_t)position >= container->as.array.count) {
                runtime_error_raise("array index out of range", 1003, "assignment");
                return NULL;
            }
            return &container->as.array.items[position];
        }
        if (container->kind == VALUE_RECORD && index.kind == VALUE_STRING) {
            RecordField *field = record_find(container, index.as.string);
            if (!field) {
                char message[256];
                snprintf(message, sizeof(message), "unknown record field: %s", index.as.string);
                value_free(index);
                runtime_error_raise(message, 1003, "assignment");
                return NULL;
            }
            value_free(index);
            return field->value;
        }
        value_free(index);
        runtime_error_raise("assignment index expects array[number] or record[string]", 1003, "assignment");
        return NULL;
    }
    default:
        runtime_error_raise("invalid assignment target", 1003, "assignment");
        return NULL;
    }
}

typedef enum {
    LVALUE_ASSIGN_ERROR,
    LVALUE_ASSIGN_UNCHANGED,
    LVALUE_ASSIGN_CHANGED
} LValueAssignResult;

static LValueAssignResult assign_lvalue(AstExpr *target, Value value) {
    switch (target->kind) {
    case AST_EXPR_IDENT: {
        Symbol *symbol = env_find_in_frame(current_env, target->as.ident);
        if (symbol && value_storage_equal(&symbol->value, &value)) {
            value_free(value);
            return LVALUE_ASSIGN_UNCHANGED;
        }
        env_set(target->as.ident, value);
        return LVALUE_ASSIGN_CHANGED;
    }
    case AST_EXPR_FIELD: {
        Value *object = resolve_lvalue_ref(target->as.field.object);
        if (!object) {
            return LVALUE_ASSIGN_ERROR;
        }
        if (object->kind != VALUE_RECORD) {
            runtime_error_raise("field assignment target expects a record", 1003, "assignment");
            return LVALUE_ASSIGN_ERROR;
        }
        RecordField *field = record_find(object, target->as.field.field);
        if (field && value_storage_equal(field->value, &value)) {
            value_free(value);
            return LVALUE_ASSIGN_UNCHANGED;
        }
        record_set(object, target->as.field.field, value);
        return LVALUE_ASSIGN_CHANGED;
    }
    case AST_EXPR_INDEX: {
        Value *container = resolve_lvalue_ref(target->as.index.array);
        if (!container) {
            return LVALUE_ASSIGN_ERROR;
        }
        Value index = eval_expr(target->as.index.index);
        if (error_action_pending()) {
            value_free(index);
            return LVALUE_ASSIGN_ERROR;
        }
        if (container->kind == VALUE_ARRAY && index.kind == VALUE_NUMBER) {
            int position = (int)index.as.number;
            value_free(index);
            if (position < 0 || (size_t)position >= container->as.array.count) {
                runtime_error_raise("array index out of range", 1003, "assignment");
                return LVALUE_ASSIGN_ERROR;
            }
            if (value_storage_equal(&container->as.array.items[position], &value)) {
                value_free(value);
                return LVALUE_ASSIGN_UNCHANGED;
            }
            value_free(container->as.array.items[position]);
            container->as.array.items[position] = value;
            return LVALUE_ASSIGN_CHANGED;
        }
        if (container->kind == VALUE_RECORD && index.kind == VALUE_STRING) {
            RecordField *field = record_find(container, index.as.string);
            if (field && value_storage_equal(field->value, &value)) {
                value_free(value);
                value_free(index);
                return LVALUE_ASSIGN_UNCHANGED;
            }
            record_set(container, index.as.string, value);
            value_free(index);
            return LVALUE_ASSIGN_CHANGED;
        }
        value_free(index);
        runtime_error_raise("assignment index expects array[number] or record[string]", 1003, "assignment");
        return LVALUE_ASSIGN_ERROR;
    }
    default:
        runtime_error_raise("invalid assignment target", 1003, "assignment");
        return LVALUE_ASSIGN_ERROR;
    }
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
        LValueAssignResult assign_result = assign_lvalue(stmt->as.assign.target, value);
        if (assign_result == LVALUE_ASSIGN_ERROR) {
            value_free(value);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        if (assign_result == LVALUE_ASSIGN_UNCHANGED) {
            break;
        }
        char *watch_path = lvalue_watch_path(stmt->as.assign.target);
        if (watch_path) {
            int watcher_ok = watcher_trigger_change(watch_path);
            free(watch_path);
            if (!watcher_ok) {
                current_line = previous_line;
                current_column = previous_column;
                return eval_error_result();
            }
        } else {
            const char *root_name = lvalue_root_name(stmt->as.assign.target);
            if (root_name) {
                if (!watcher_trigger_change(root_name)) {
                    current_line = previous_line;
                    current_column = previous_column;
                    return eval_error_result();
                }
            }
        }
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
            if (eval_result_exits_block(result)) {
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
        loop_depth++;
        for (size_t i = 0; i < iterable.as.array.count; i++) {
            env_set(stmt->as.for_each.name, value_copy(iterable.as.array.items[i]));
            EvalResult result = eval_stmt_list(stmt->as.for_each.body);
            if (result.did_break) {
                value_free(result.value);
                break;
            }
            if (result.did_continue) {
                value_free(result.value);
                continue;
            }
            if (eval_result_exits_block(result)) {
                loop_depth--;
                value_free(iterable);
                current_line = previous_line;
                current_column = previous_column;
                return result;
            }
        }
        loop_depth--;
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
        if (!watcher_register(stmt)) {
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        break;
    case AST_STMT_WITHOUT_WATCHERS: {
        watcher_suppressed++;
        EvalResult result = eval_stmt_list(stmt->as.without_watchers);
        watcher_suppressed--;
        if (eval_result_exits_block(result)) {
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
    case AST_STMT_BREAK:
        if (loop_depth == 0 && consider_depth == 0) {
            runtime_error_raise("break outside loop or consider block", 1003, "invalid control flow");
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        current_line = previous_line;
        current_column = previous_column;
        return eval_break();
    case AST_STMT_CONTINUE:
        if (loop_depth == 0) {
            runtime_error_raise("continue outside loop", 1003, "invalid control flow");
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }
        current_line = previous_line;
        current_column = previous_column;
        return eval_continue();
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
            if (eval_result_exits_block(result)) {
                current_line = previous_line;
                current_column = previous_column;
                return result;
            }
        } else {
            EvalResult result = eval_stmt_list(stmt->as.if_stmt.else_body);
            if (eval_result_exits_block(result)) {
                current_line = previous_line;
                current_column = previous_column;
                return result;
            }
        }
        break;
    }
    case AST_STMT_WHILE: {
        loop_depth++;
        for (;;) {
            int before_error = error_generation;
            Value condition = eval_expr(stmt->as.while_stmt.condition);
            if (error_generation != before_error) {
                value_free(condition);
                loop_depth--;
                current_line = previous_line;
                current_column = previous_column;
                return eval_error_result();
            }
            int truth = value_truthy(condition);
            value_free(condition);
            if (!truth) {
                break;
            }
            EvalResult result = eval_stmt_list(stmt->as.while_stmt.body);
            if (result.did_break) {
                value_free(result.value);
                break;
            }
            if (result.did_continue) {
                value_free(result.value);
                continue;
            }
            if (eval_result_exits_block(result)) {
                loop_depth--;
                current_line = previous_line;
                current_column = previous_column;
                return result;
            }
        }
        loop_depth--;
        break;
    }
    case AST_STMT_CONSIDER: {
        int before_error = error_generation;
        Value subject = eval_expr(stmt->as.consider.subject);
        if (error_generation != before_error) {
            value_free(subject);
            current_line = previous_line;
            current_column = previous_column;
            return eval_error_result();
        }

        int matched = 0;
        EvalResult result = eval_no_result();
        for (size_t i = 0; i < stmt->as.consider.branches.count; i++) {
            Value candidate = eval_expr(stmt->as.consider.branches.items[i].match);
            if (error_generation != before_error) {
                value_free(candidate);
                value_free(subject);
                current_line = previous_line;
                current_column = previous_column;
                return eval_error_result();
            }
            int equal = values_equal_for_consider(subject, candidate);
            value_free(candidate);
            if (error_generation != before_error) {
                value_free(subject);
                current_line = previous_line;
                current_column = previous_column;
                return eval_error_result();
            }
            if (equal) {
                matched = 1;
                consider_depth++;
                result = eval_stmt_list(stmt->as.consider.branches.items[i].body);
                consider_depth--;
                break;
            }
        }

        if (!matched && stmt->as.consider.else_body.count > 0) {
            consider_depth++;
            result = eval_stmt_list(stmt->as.consider.else_body);
            consider_depth--;
        }

        value_free(subject);
        if (result.did_break) {
            value_free(result.value);
            break;
        }
        if (eval_result_exits_block(result)) {
            current_line = previous_line;
            current_column = previous_column;
            return result;
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
        if (result.did_return || result.did_gosub || result.did_stop ||
            result.did_break || result.did_continue) {
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
    if (result.did_break || result.did_continue) {
        value_free(result.value);
        exit_status = 1;
    }
    if (!exit_status && webserver_any_active()) {
        exit_status = webserver_run_event_loop();
    }
    free(error_goto_label);
    error_goto_label = NULL;
    free(pending_error_goto_label);
    pending_error_goto_label = NULL;
    error_clear_state();
    error_mode = ERROR_MODE_STOP;
    runtime_stopped = 0;
    loop_depth = 0;
    consider_depth = 0;
    lock_clear();
    watcher_clear();
    modifier_clear();
    function_clear();
    loaded_files_clear();
    use_pairs_clear(&used_pairs, &used_pair_count);
    use_pairs_clear(&use_stack, &use_stack_count);
    gui_clear_native_windows();
    gui_library_loaded = 0;
    pg_library_loaded = 0;
    webclient_library_loaded = 0;
    webserver_library_loaded = 0;
    webserver_clear();
    if (webclient_curl_initialized) {
#if HAVE_LIBCURL
        curl_global_cleanup();
#endif
        webclient_curl_initialized = 0;
    }
    free(current_import_path);
    current_import_path = NULL;
    free(root_source_path);
    root_source_path = NULL;
    env_clear(&global_env);
    active_root = ast_stmt_list_empty();
    return exit_status;
}

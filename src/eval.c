#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "eval.h"
#include "builtins.h"
#include "actor.h"
#include "diagnostics.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

#if HAVE_LIBXCRYPT
#include <crypt.h>
#endif

#if HAVE_LIBCRYPTO
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#endif

#if HAVE_LIBXML2
#include <libxml/xmlreader.h>
#endif

#if HAVE_GIR
/* GObject-Introspection bridge (gi.* module). girepository.h pulls in
 * glib-object.h (GObject, GValue, signals). Modern girepository-2.0 API only. */
#include <girepository/girepository.h>
#endif

#define SECURE_TOKEN_MAX_LENGTH 4096

/* Seedable general-purpose PRNG (statistics_design.md §8 shared infrastructure):
 * reproducibility for resampling, Monte Carlo, and golden tests. xoshiro256**
 * over SplitMix64 seeding — pure fixed-width integer math, so the byte stream is
 * identical across architectures (x86 / s390x / riscv64), which the exact-match
 * golden tests depend on. Distinct from secure_token, which is a CSPRNG and must
 * stay non-reproducible. */
static uint64_t gbasic_rng_state[4];
static int gbasic_rng_seeded = 0;

static uint64_t gbasic_rng_rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static void gbasic_rng_seed(uint64_t seed) {
    /* SplitMix64 expands the 64-bit seed into the 256-bit xoshiro state. */
    uint64_t z = seed;
    for (int i = 0; i < 4; i++) {
        z += 0x9E3779B97F4A7C15ULL;
        uint64_t x = z;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        x = x ^ (x >> 31);
        gbasic_rng_state[i] = x;
    }
    /* xoshiro requires a non-zero state. */
    if ((gbasic_rng_state[0] | gbasic_rng_state[1] |
         gbasic_rng_state[2] | gbasic_rng_state[3]) == 0) {
        gbasic_rng_state[0] = 0x9E3779B97F4A7C15ULL;
    }
    gbasic_rng_seeded = 1;
}

static void gbasic_rng_autoseed(void) {
    /* No explicit seed yet: draw a nondeterministic one so unseeded programs
     * still vary run to run. Tests call seed() for reproducibility. */
    uint64_t s = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    int got = 0;
    if (fd >= 0) {
        if (read(fd, &s, sizeof(s)) == (ssize_t)sizeof(s)) {
            got = 1;
        }
        close(fd);
    }
    if (!got) {
        s = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32);
    }
    gbasic_rng_seed(s);
}

static uint64_t gbasic_rng_next(void) {
    if (!gbasic_rng_seeded) {
        gbasic_rng_autoseed();
    }
    uint64_t *s = gbasic_rng_state;
    uint64_t result = gbasic_rng_rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = gbasic_rng_rotl(s[3], 45);
    return result;
}

/* Uniform double in [0, 1) from the top 53 bits (full mantissa precision). */
static double gbasic_rng_double(void) {
    return (double)(gbasic_rng_next() >> 11) * (1.0 / 9007199254740992.0);
}

/* Uniform integer in [0, bound) without modulo bias (rejection sampling). */
static uint64_t gbasic_rng_below(uint64_t bound) {
    if (bound == 0) {
        return 0;
    }
    uint64_t threshold = (uint64_t)(-bound) % bound; /* 2^64 mod bound */
    for (;;) {
        uint64_t r = gbasic_rng_next();
        if (r >= threshold) {
            return r % bound;
        }
    }
}

int parse_source(const char *source, AstStmtList *out_program);
void parse_set_source_path(const char *path);

typedef struct PgConnectionValue PgConnectionValue;
typedef struct SqliteConnectionValue SqliteConnectionValue;
typedef struct XmlReaderValue XmlReaderValue;
typedef struct GObjectValue GObjectValue;
typedef struct ActorHandle ActorHandle;
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
    VALUE_SQLITE_CONNECTION,
    VALUE_XML_READER,
    VALUE_GOBJECT,
    VALUE_ACTOR,
    VALUE_FUNCTION
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
    AstFieldPolicy policy;   /* PBI derivation policy; default COPY (0) */
    AstExpr *reset_expr;     /* shared AST pointer; non-NULL only for RESET */
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
        XmlReaderValue *xml_reader;
        GObjectValue *gobject;
        ActorHandle *actor;
        /* A first-class function value: a reference to a registered function by
         * name (NOT a capturing closure — see docs/first_class_functions_design.md
         * §2). `library` is the owning library for an imported function, or NULL
         * for a local/unqualified one. Both strings are owned copies. */
        struct {
            char *name;
            char *library;
        } function;
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

/* An opaque streaming XML reader handle (xml_design.md §4). Refcounted like the
 * connection values so copies share one xmlTextReader and the last release frees
 * it — the reader also closes on scope cleanup. `closed` marks an explicit
 * xml.close so use-after-close is a structured error, not a crash. */
struct XmlReaderValue {
#if HAVE_LIBXML2
    xmlTextReaderPtr reader;
#endif
    size_t ref_count;
    int closed;
};

/* An opaque GObject handle for the gi.* GObject-Introspection bridge. Exactly one
 * wrapper exists per underlying GObject (canonicalized via g_object_set_qdata), and
 * that wrapper owns exactly ONE strong reference to the object. `ref_count` is the
 * number of live gBASIC Values pointing at this wrapper (bumped by value_copy,
 * dropped by value_free); when it reaches 0 we drop the qdata link and g_object_unref
 * the object. `closed` guards against a double-unref after an explicit dispose. */
struct GObjectValue {
#if HAVE_GIR
    GObject *obj;
#endif
    size_t ref_count;
    int closed;
};

/* A handle to some actor's inbound mailbox (docs/multiprocessing_design.md §4).
 * `write_fd` is the capability — the write end of that mailbox; `id` is the
 * routable identity used for equality (and, later, cross-isolate handle passing).
 * Refcounted like the connection values so copies share one fd and the last
 * release closes it. */
struct ActorHandle {
    int write_fd;
    uint64_t id;
    size_t ref_count;
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
/* The receiver (`this`) of the currently executing function, or NULL when the
 * current function is a plain (non-method) call. Saved/restored around every
 * invoke_function exactly like current_env, so a plain call nested inside a
 * method does NOT inherit the method's receiver (first_class_functions_design
 * §4). Points at live record storage so `this.field = …` writes through. */
static Value *current_this = NULL;
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
static int xml_library_loaded = 0;
static int gi_library_loaded = 0;
#if HAVE_GIR
static int gi_gtk4_active = 0;   /* a Gtk-4.x namespace has been required via gi */
#endif
static WebServer *webservers = NULL;
static size_t webserver_count = 0;
static unsigned long webserver_next_id = 1;

static Value webserver_eval_call(AstExpr *expr);
static int webserver_run_event_loop(void);
static void webserver_clear(void);
static FunctionDef *function_resolve(const char *library, const char *name);
static void method_ensure_internal_name(AstStmt *stmt);
static void register_method_bodies_in(AstStmtList list);

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
    case VALUE_XML_READER:
        return "xml_reader";
    case VALUE_GOBJECT:
        return "gobject";
    case VALUE_ACTOR:
        return "actor";
    case VALUE_FUNCTION:
        return "function";
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

/* --- Runtime string buffers (Unicode design §3, docs/unicode_design.md) -----
 *
 * A runtime string VALUE (VALUE_STRING -> as.string) carries an explicit byte
 * length so it can be binary-safe (hold interior NUL bytes). The length lives in
 * a header placed immediately *before* the byte data; `as.string` keeps pointing
 * at the data, so the ~114 sites that read `value.as.string` as a `char *` are
 * untouched. This mirrors the PBI ValueCell trick (the bookkeeping rides in front
 * of the pointer the rest of the code already uses).
 *
 * Only `value_string`/`value_string_n` allocate these buffers and only
 * `value_free` releases them (via `string_free`). The buffer is always
 * NUL-terminated at [length] for C-string interop; the terminator is not counted
 * in `length`, and the data may contain interior NULs. NOTE: this is a different
 * allocation shape from `copy_string` (used for field names, paths, labels, …),
 * which stays a plain `malloc`/`free` buffer — never mix the two.
 *
 * Phase 0 establishes the representation; the length is stored but not yet
 * consumed (every string built today is valid NUL-terminated text, so
 * strlen == length and behaviour is unchanged). Phase 1 makes the runtime
 * length-authoritative. */
typedef struct {
    size_t length;
} StringHeader;

#define STRING_HEADER_SIZE (sizeof(StringHeader))

/* Allocate a runtime string buffer holding `length` bytes copied from `bytes`
 * (which may itself contain NULs). Returns a pointer to the byte data; recover
 * the header with string_length / free with string_free. */
static char *string_new(const char *bytes, size_t length) {
    char *block = malloc(STRING_HEADER_SIZE + length + 1);
    if (!block) {
        abort();
    }
    ((StringHeader *)block)->length = length;
    char *data = block + STRING_HEADER_SIZE;
    if (length > 0) {
        memcpy(data, bytes, length);
    }
    data[length] = '\0';
    return data;
}

/* Authoritative byte length of a runtime string value's data pointer. */
static size_t string_length(const char *data) {
    return ((const StringHeader *)(data - STRING_HEADER_SIZE))->length;
}

/* Release a runtime string buffer created by string_new. */
static void string_free(char *data) {
    if (data) {
        free(data - STRING_HEADER_SIZE);
    }
}

/* Binary-safe equality of two runtime string values (string_new buffers).
 * Compares the full authoritative byte length, so interior NULs are honored. */
static int string_value_equal(const char *a, const char *b) {
    size_t la = string_length(a);
    size_t lb = string_length(b);
    return la == lb && memcmp(a, b, la) == 0;
}

/* ASCII-only case folding (docs/unicode_design.md §6). Locale-independent by
 * design: only A-Z <-> a-z fold, every other byte — including all UTF-8
 * multibyte sequences — passes through untouched, so non-ASCII is never
 * mis-folded the way locale-sensitive tolower/toupper could. */
static char ascii_tolower(unsigned char c) {
    return (char)((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
}

static char ascii_toupper(unsigned char c) {
    return (char)((c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c);
}

/* Binary-safe ASCII-caseless equality of two runtime string values. Honors the
 * full byte length (interior NULs compare), folds ASCII letters, compares all
 * other bytes exactly. */
static int string_value_equal_caseless(const char *a, const char *b) {
    size_t la = string_length(a);
    size_t lb = string_length(b);
    if (la != lb) {
        return 0;
    }
    for (size_t i = 0; i < la; i++) {
        if (ascii_tolower((unsigned char)a[i]) != ascii_tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return 1;
}

/* Binary-safe ordering of two runtime string values (string_new buffers).
 * Lexicographic by unsigned byte; shorter string sorts first on a prefix tie. */
static int string_value_compare(const char *a, const char *b) {
    size_t la = string_length(a);
    size_t lb = string_length(b);
    size_t min = la < lb ? la : lb;
    int cmp = min ? memcmp(a, b, min) : 0;
    if (cmp != 0) {
        return cmp;
    }
    if (la < lb) return -1;
    if (la > lb) return 1;
    return 0;
}

/* Encode a Unicode scalar value as UTF-8 into `out` (up to 4 bytes); returns the
 * number of bytes written. Caller must pass a valid scalar (0..0x10FFFF, not a
 * surrogate); validation lives at the call sites that accept user input. */
static size_t utf8_encode_codepoint(unsigned cp, char out[4]) {
    if (cp <= 0x7f) {
        out[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7ff) {
        out[0] = (char)(0xc0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3fu));
        return 2;
    } else if (cp <= 0xffff) {
        out[0] = (char)(0xe0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        out[2] = (char)(0x80u | (cp & 0x3fu));
        return 3;
    } else {
        out[0] = (char)(0xf0u | (cp >> 18));
        out[1] = (char)(0x80u | ((cp >> 12) & 0x3fu));
        out[2] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        out[3] = (char)(0x80u | (cp & 0x3fu));
        return 4;
    }
}

/* Decode the first UTF-8 codepoint of `s` (which holds `len` bytes), storing it
 * in *cp and returning the number of bytes it occupies. A malformed or truncated
 * sequence degrades to one byte per the lenient policy (docs/unicode_design.md
 * §7), so this is total over arbitrary bytes and never reads past `len`. */
static size_t utf8_decode_first(const char *s, size_t len, unsigned *cp) {
    if (len == 0) {
        *cp = 0;
        return 0;
    }
    unsigned char b0 = (unsigned char)s[0];
    if (b0 < 0x80) {
        *cp = b0;
        return 1;
    }
    size_t need;
    unsigned value;
    if ((b0 & 0xe0u) == 0xc0u) { need = 1; value = b0 & 0x1fu; }
    else if ((b0 & 0xf0u) == 0xe0u) { need = 2; value = b0 & 0x0fu; }
    else if ((b0 & 0xf8u) == 0xf0u) { need = 3; value = b0 & 0x07u; }
    else { *cp = b0; return 1; }              /* stray continuation / 0xF8+ */
    if (need >= len) {                         /* truncated at end of string */
        *cp = b0;
        return 1;
    }
    for (size_t i = 1; i <= need; i++) {
        unsigned char bi = (unsigned char)s[i];
        if ((bi & 0xc0u) != 0x80u) {           /* not a continuation byte */
            *cp = b0;
            return 1;
        }
        value = (value << 6) | (bi & 0x3fu);
    }
    *cp = value;
    return need + 1;
}

/* Count Unicode codepoints in `len` bytes, with the lenient invalid-UTF-8 rule
 * (each malformed byte counts as one unit). Total over arbitrary bytes. */
static size_t string_codepoint_count(const char *s, size_t len) {
    size_t count = 0;
    size_t pos = 0;
    while (pos < len) {
        unsigned cp;
        pos += utf8_decode_first(s + pos, len - pos, &cp);
        count++;
    }
    return count;
}

/* Binary-safe substring search over raw bytes (memmem-style). Returns the byte
 * offset of the first match, or -1 if absent. An empty needle matches at 0. */
static long string_find_bytes(const char *hay, size_t hlen,
                              const char *needle, size_t nlen) {
    if (nlen == 0) {
        return 0;
    }
    if (nlen > hlen) {
        return -1;
    }
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) {
            return (long)i;
        }
    }
    return -1;
}

/* Byte offset at which the codepoint at index `cp_index` begins. If `cp_index`
 * is at or past the codepoint count, returns `len` (one-past-the-end). */
static size_t string_codepoint_offset(const char *s, size_t len, size_t cp_index) {
    size_t pos = 0;
    size_t seen = 0;
    while (pos < len && seen < cp_index) {
        unsigned cp;
        pos += utf8_decode_first(s + pos, len - pos, &cp);
        seen++;
    }
    return pos;
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

/* Binary-safe string constructor: copies `length` bytes (interior NULs allowed). */
static Value value_string_n(const char *bytes, size_t length) {
    Value value = {0};
    value.kind = VALUE_STRING;
    value.as.string = string_new(bytes, length);
    return value;
}

static Value value_string(const char *string) {
    return value_string_n(string, strlen(string));
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

/* Build a first-class function value referencing a registered function by name
 * (docs/first_class_functions_design.md §3). `library` may be NULL. */
static Value value_function(const char *name, const char *library) {
    Value value = {0};
    value.kind = VALUE_FUNCTION;
    value.as.function.name = copy_string(name);
    value.as.function.library = library ? copy_string(library) : NULL;
    return value;
}

/* Two function values are equal iff they reference the same registered function
 * (same name and same owning library). */
static int function_value_equal(const Value *left, const Value *right) {
    if (strcmp(left->as.function.name, right->as.function.name) != 0) {
        return 0;
    }
    const char *ll = left->as.function.library;
    const char *rl = right->as.function.library;
    if (ll == NULL && rl == NULL) {
        return 1;
    }
    return ll && rl && strcmp(ll, rl) == 0;
}

/*
 * Reference-counted storage cell for a record field's value (PBI Phase 0).
 *
 * `value` is the FIRST member, so a `Value *` held in a RecordField points at
 * `&cell->value`, which is the same address as the cell itself. Every existing
 * `field->value` dereference therefore keeps working unchanged; only the
 * allocation and release of a field's storage route through the helpers below.
 *
 * In Phase 0 every cell is uniquely owned with refcount 1, so behaviour is
 * identical to the previous one-Value-per-field allocate/free model.
 * Later PBI phases share cells (link = write-through, copy = copy-on-write).
 * See docs/pbi_design.md §4.
 */
typedef struct {
    Value value;
    size_t refcount;
} ValueCell;

/* Allocate a fresh field cell (refcount 1), value null-initialised so that
 * cell_release is always safe even before the real value is stored. Returns a
 * Value* usable exactly like a plain heap Value pointer. */
static Value *cell_alloc(void) {
    ValueCell *cell = malloc(sizeof(ValueCell));
    if (!cell) {
        abort();
    }
    cell->refcount = 1;
    cell->value = (Value){0};
    return &cell->value;
}

static Value value_datetime(DateTime datetime) {
    Value value = {0};
    value.kind = VALUE_DATETIME;
    value.as.datetime = datetime;
    return value;
}

/* User-facing number formatting. Integer-valued doubles that are exactly
 * representable (|v| < 2^53) print in full with no exponent — so epoch seconds,
 * 32-bit bitwise results, and large ids read correctly — while everything else
 * keeps the compact %g form. Distinct from the %.17g used for serialize/decode
 * round-trips, which must stay full-precision. */
static void format_number(char *buf, size_t bufsize, double v) {
    if (isfinite(v) && v == floor(v) && fabs(v) < 9007199254740992.0) {
        snprintf(buf, bufsize, "%.0f", v);
    } else {
        snprintf(buf, bufsize, "%g", v);
    }
}

/* Convert a datetime to Unix epoch seconds. Datetimes are local wall-clock
 * (now() uses localtime), so mktime() interprets the fields as local time and
 * yields the same instant epoch() reports. Missing calendar parts (year/month/
 * day-only precisions) default to the start of the period; a time-only value has
 * no date and cannot be placed on the timeline (*ok = 0). */
static double datetime_to_epoch(DateTime dt, int *ok) {
    if (dt.time_only) {
        *ok = 0;
        return 0;
    }
    struct tm tm = {0};
    tm.tm_year = dt.year - 1900;
    tm.tm_mon = (dt.month >= 1 ? dt.month : 1) - 1;
    tm.tm_mday = dt.day >= 1 ? dt.day : 1;
    tm.tm_hour = dt.hour;
    tm.tm_min = dt.minute;
    tm.tm_sec = dt.second;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    if (t == (time_t)-1) {
        *ok = 0;
        return 0;
    }
    *ok = 1;
    return (double)t;
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

static Value value_xml_reader(XmlReaderValue *reader) {
    Value value = {0};
    value.kind = VALUE_XML_READER;
    value.as.xml_reader = reader;
    return value;
}

/* Refcount release: last owner frees the underlying xmlTextReader. Defined here
 * (not in modules/xml.c) so value_free can call it even in a HAVE_LIBXML2=0
 * build, where no reader is ever created and this is dead code. */
static void xml_reader_release(XmlReaderValue *reader) {
    if (!reader) {
        return;
    }
    if (--reader->ref_count == 0) {
#if HAVE_LIBXML2
        if (!reader->closed && reader->reader) {
            xmlFreeTextReader(reader->reader);
        }
#endif
        free(reader);
    }
}

static Value value_gobject(GObjectValue *handle) {
    Value value = {0};
    value.kind = VALUE_GOBJECT;
    value.as.gobject = handle;
    return value;
}

#if HAVE_GIR
/* Quark under which each GObject stores a back-pointer to its one canonical
 * wrapper (qdata canonicalization). Looked up lazily so no work happens unless the
 * bridge is actually used. */
static GQuark gi_wrapper_quark(void) {
    static GQuark quark = 0;
    if (quark == 0) {
        quark = g_quark_from_static_string("gbasic-gobject-wrapper");
    }
    return quark;
}

/* Weak-ref callback: fires when the underlying GObject is finalized out from under
 * us — which GTK does to widgets when their toplevel is destroyed, even though we
 * hold a strong ref through it. Once this runs, `obj` is a dangling pointer, so we
 * null it and mark the wrapper closed; gobject_release then skips the (now illegal)
 * qdata/unref calls instead of crashing on freed memory. */
static void gi_wrapper_weak_notify(gpointer data, GObject *where_the_object_was) {
    (void)where_the_object_was;
    GObjectValue *handle = data;
    handle->obj = NULL;
    handle->closed = 1;
}
#endif

/* Refcount release: last live Value drops the qdata link and the single strong
 * object reference the wrapper owns. Defined unconditionally so value_free links in
 * a HAVE_GIR=0 build, where no wrapper is ever created and this is dead code. */
static void gobject_release(GObjectValue *handle) {
    if (!handle) {
        return;
    }
    if (--handle->ref_count == 0) {
#if HAVE_GIR
        if (!handle->closed && handle->obj) {
            /* Remove our weak-ref before the strong unref so the notify above never
             * fires on a handle we are about to free, then sever the canonical link
             * so a later wrapping of the same address can't resurrect this wrapper. */
            g_object_weak_unref(handle->obj, gi_wrapper_weak_notify, handle);
            g_object_set_qdata(handle->obj, gi_wrapper_quark(), NULL);
            g_object_unref(handle->obj);
        }
#endif
        free(handle);
    }
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
        /* Route through the diagnostic sink: pushed when the CLI has installed a
         * sink (drained after eval), or emitted immediately in the legacy format
         * when none is set (e.g. actor mode). A STOP-mode error is terminal, so
         * either way it remains the last line on stderr — byte-exact. `code`
         * rides along as the language-level subcode. */
        gb_span span = { current_error.line, current_error.column,
                         current_error.line, current_error.column };
        gb_report(GB_DIAG_RUNTIME_ERROR, code, runtime_error_path(), span,
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
    RecordField *fields = calloc(5, sizeof(RecordField));
    if (!fields) {
        abort();
    }
    const char *names[] = {"message", "line", "column", "code", "source"};
    for (size_t i = 0; i < 5; i++) {
        fields[i].name = copy_string(names[i]);
        fields[i].value = cell_alloc();
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
        /* Length-aware so binary-safe content (interior NULs) survives a copy. */
        return value_string_n(value.as.string, string_length(value.as.string));
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
    if (value.kind == VALUE_XML_READER) {
        value.as.xml_reader->ref_count++;
        return value_xml_reader(value.as.xml_reader);
    }
    if (value.kind == VALUE_GOBJECT) {
        value.as.gobject->ref_count++;
        return value_gobject(value.as.gobject);
    }
    if (value.kind == VALUE_ACTOR) {
        value.as.actor->ref_count++;
        return value;
    }
    if (value.kind == VALUE_FUNCTION) {
        return value_function(value.as.function.name, value.as.function.library);
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
            fields = calloc(value.as.record.count, sizeof(RecordField));
            if (!fields) {
                abort();
            }
            for (size_t i = 0; i < value.as.record.count; i++) {
                RecordField *src = &value.as.record.fields[i];
                fields[i].name = copy_string(src->name);
                /* Preserve PBI policy so it travels with copies/assignments. */
                fields[i].policy = src->policy;
                fields[i].reset_expr = src->reset_expr;
                /* Share the cell (refcount++). For `link` this is permanent
                 * write-through identity; for every other policy it is
                 * copy-on-write — the field's write barrier (cell_fork_for_write)
                 * forks the cell on first mutation so the copy stays independent.
                 * This makes value_copy O(fields) instead of a full deep copy. */
                ValueCell *cell = (ValueCell *)src->value;
                cell->refcount++;
                fields[i].value = src->value;
            }
        }
        return value_record(fields, value.as.record.count);
    }
    return value;
}

static void cell_release(Value *cell_value);

static void value_free(Value value) {
    if (value.kind == VALUE_STRING) {
        string_free(value.as.string);
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
            cell_release(value.as.record.fields[i].value);
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
    } else if (value.kind == VALUE_XML_READER) {
        xml_reader_release(value.as.xml_reader);
    } else if (value.kind == VALUE_GOBJECT) {
        gobject_release(value.as.gobject);
    } else if (value.kind == VALUE_ACTOR) {
        ActorHandle *handle = value.as.actor;
        if (handle && --handle->ref_count == 0) {
            if (handle->write_fd >= 0) {
                close(handle->write_fd);
            }
            free(handle);
        }
    } else if (value.kind == VALUE_FUNCTION) {
        free(value.as.function.name);
        free(value.as.function.library);
    }
}

/* Drop one reference to a record-field cell, freeing the contained value and
 * the cell block when the last reference goes away. Mirrors the previous
 * `value_free(*p); free(p);` pair. The cell is recovered by casting back to its
 * first member (`value` at offset 0). */
static void cell_release(Value *cell_value) {
    ValueCell *cell = (ValueCell *)cell_value;
    if (--cell->refcount == 0) {
        value_free(cell->value);
        free(cell);
    }
}

/* Copy-on-write barrier. Before a record-field cell's content is mutated in
 * place, make sure this field owns the cell privately. A `link` field is shared
 * identity by design and must never fork — writes are meant to be seen through
 * every alias. Any other field whose cell is still shared (refcount > 1) forks
 * into a private copy so the pending write stays local; this is what keeps
 * `copy` observably independent while `new`/assignment only bump refcounts. The
 * fork uses value_copy, which is itself copy-on-write, so a deep structure
 * diverges one level at a time as each level is first written. */
static void cell_fork_for_write(RecordField *field) {
    if (field->policy == AST_FIELD_POLICY_LINK) {
        return;
    }
    ValueCell *cell = (ValueCell *)field->value;
    if (cell->refcount > 1) {
        Value *fresh = cell_alloc();
        *fresh = value_copy(cell->value);
        cell->refcount--;
        field->value = fresh;
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
    case VALUE_XML_READER:
        runtime_error_raise("xml reader cannot be used as a condition",
                            5001,
                            "xml");
        return 0;
    case VALUE_SQLITE_CONNECTION:
        runtime_error_raise("sqlite connection cannot be used as a condition",
                            2002,
                            "sqlite");
        return 0;
    case VALUE_GOBJECT:
        return 1;
    case VALUE_ACTOR:
        return 1;
    case VALUE_FUNCTION:
        return 1;
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
    case VALUE_NUMBER: {
        char nb[32];
        format_number(nb, sizeof(nb), value.as.number);
        printf("%s\n", nb);
        break;
    }
    case VALUE_STRING:
        fwrite(value.as.string, 1, string_length(value.as.string), stdout);
        putchar('\n');
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
                char nb[32];
                format_number(nb, sizeof(nb), value.as.array.items[i].as.number);
                printf("%s", nb);
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
    case VALUE_XML_READER:
        printf("<xml_reader>\n");
        break;
    case VALUE_GOBJECT:
        printf("<gobject>\n");
        break;
    case VALUE_ACTOR:
        printf("<actor>\n");
        break;
    case VALUE_FUNCTION:
        printf("<function %s>\n", value.as.function.name);
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
    if (strcmp(name, "this") == 0) {
        /* The receiver, bound at the call site (first_class_functions_design §4).
         * Outside a method call there is no receiver. */
        if (!current_this) {
            runtime_error_raise("this is only bound inside a method call",
                                1003, "this");
            return value_null();
        }
        return value_copy(*current_this);
    }
    Symbol *symbol = env_find(name);
    if (!symbol) {
        /* A bare function name (no parentheses) evaluates to a function value —
         * a reference to the registered function (first_class_functions_design
         * §3). Variables shadow this: a variable of the same name is found above. */
        FunctionDef *function = function_resolve(NULL, name);
        if (function) {
            return value_function(function->name, function->library);
        }
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

#if HAVE_LIBXCRYPT
static int constant_time_string_equal(const char *left, const char *right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    size_t max_len = left_len > right_len ? left_len : right_len;
    unsigned char diff = (unsigned char)(left_len ^ right_len);

    for (size_t i = 0; i < max_len; i++) {
        unsigned char left_char = i < left_len ? (unsigned char)left[i] : 0;
        unsigned char right_char = i < right_len ? (unsigned char)right[i] : 0;
        diff |= (unsigned char)(left_char ^ right_char);
    }

    return diff == 0;
}
#endif

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
        if (field->policy != AST_FIELD_POLICY_LINK &&
            ((ValueCell *)field->value)->refcount > 1) {
            /* The cell is shared (copy-on-write) and is about to be replaced
             * wholesale: detach into a private cell so the aliases keep the old
             * value. No need to copy the old content — it is being overwritten.
             * `link` falls through and writes in place (write-through). */
            cell_release(field->value);
            field->value = cell_alloc();
        } else {
            value_free(*field->value);
        }
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
    field->value = cell_alloc();
    if (!field->value) {
        abort();
    }
    *field->value = value;
    /* realloc does not zero the new slot; a runtime-added field defaults to
     * the copy policy. (An existing field keeps its policy; see the early
     * return above, which only reassigns the value.) */
    field->policy = AST_FIELD_POLICY_COPY;
    field->reset_expr = NULL;
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
        return string_value_equal(left->as.string, right->as.string);
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
    case VALUE_XML_READER:
        return left->as.xml_reader == right->as.xml_reader;
    case VALUE_GOBJECT:
        return left->as.gobject == right->as.gobject;
    case VALUE_ACTOR:
        return left->as.actor->id == right->as.actor->id;
    case VALUE_FUNCTION:
        return function_value_equal(left, right);
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
static int gui_ensure_gtk_available(void);
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
    cell_fork_for_write(field);
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
    cell_fork_for_write(field);
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
        if (ascii_tolower((unsigned char)*left) != ascii_tolower((unsigned char)*right)) {
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
    const char *start = gbasic_path;
    size_t before_path = *match_count;
    while (start && *start) {
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

#ifdef GBASIC_DEFAULT_STDLIB
    /* Fallback so an installed gbasic finds its stdlib without GBASIC_PATH set.
     * Only consulted when GBASIC_PATH did not already resolve the library, so a
     * GBASIC_PATH copy is never shadowed by (or warns against) the installed one. */
    if (*match_count == before_path && GBASIC_DEFAULT_STDLIB[0] != '\0') {
        search_directory_for_library(GBASIC_DEFAULT_STDLIB, name, 1, exact_filename, matches, match_count);
    }
#endif
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

    if (!path && strcmp(name, "xml") == 0) {
#if HAVE_LIBXML2
        xml_library_loaded = 1;
#else
        runtime_error_raise("XML support is not available in this build",
                            5001,
                            "xml");
#endif
        return;
    }

    if (!path && strcmp(name, "gi") == 0) {
#if HAVE_GIR
        gi_library_loaded = 1;
#else
        runtime_error_raise("gobject-introspection support is unavailable; "
                            "install libgirepository-2.0-dev (GLib >= 2.80) and rebuild",
                            6001,
                            "gi");
#endif
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
#if HAVE_GIR
        /* A single process cannot host both GTK 3 (this gui module) and GTK 4
         * (loaded via gi.require). Refuse the second toolkit rather than crash. */
        if (gi_gtk4_active) {
            runtime_error_raise("GTK 3 (gui module) and GTK 4 (gi) cannot be used in the same process",
                                6001, "gi");
            return;
        }
#endif
        gui_library_loaded = 1;
    }
    for (size_t i = 0; i < library->as.library.body.count; i++) {
        AstStmt *stmt = library->as.library.body.items[i];
        if (stmt->kind == AST_STMT_USE) {
            library_import(stmt->as.use_stmt.name, stmt->as.use_stmt.path);
            if (error_action_pending()) {
                return;
            }
        } else if (stmt->kind == AST_STMT_FUNCTION && !stmt->as.function.object) {
            /* Dotted defs are executable attach statements, not hoistable
             * declarations — they are never library exports. */
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

/* Linear-interpolation quantile (type 7, the R/NumPy default) on an
 * ascending-sorted array. q in [0,1] (statistics_design.md §8). */
static double quantile_sorted(const double *sorted, size_t count, double q) {
    if (count == 1) {
        return sorted[0];
    }
    double h = (double)(count - 1) * q;
    double lo = floor(h);
    size_t i = (size_t)lo;
    if (i + 1 >= count) {
        return sorted[count - 1];
    }
    double frac = h - lo;
    return sorted[i] + frac * (sorted[i + 1] - sorted[i]);
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

        size_t text_size = string_length(text_value.as.string);
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
        fwrite(text_value.as.string, 1, string_length(text_value.as.string), file);
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

    RecordField *fields = calloc(3, sizeof(RecordField));
    if (!fields) {
        abort();
    }

    fields[0].name = copy_string("name");
    fields[0].value = cell_alloc();
    fields[1].name = copy_string("path");
    fields[1].value = cell_alloc();
    fields[2].name = copy_string("type");
    fields[2].value = cell_alloc();
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

/* Run a user function body with its parameters bound to the already-evaluated
 * `args` (count must equal the parameter count). Takes ownership of the `args`
 * array and the values in it, freeing the array. Shared by ordinary calls
 * (eval_user_function) and the spawned-actor entry path (eval_run_actor). */
static Value invoke_function(AstStmt *stmt, Value *args, size_t argc, Value *receiver) {
    Env local_env = {0};
    local_env.parent = &global_env;
    Env *previous_env = current_env;
    current_env = &local_env;

    /* Bind `this` for this frame only. A method call passes a live receiver; a
     * plain call passes NULL, which also hides any outer method's `this`. */
    Value *previous_this = current_this;
    current_this = receiver;

    for (size_t i = 0; i < argc; i++) {
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
    current_this = previous_this;
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

/* Evaluate a user-function call. `receiver` is NULL for a plain call and a live
 * record pointer for a method call (binds `this` inside the body). */
static Value eval_user_function_with_receiver(AstExpr *expr,
                                              FunctionDef *function,
                                              Value *receiver) {
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

    return invoke_function(stmt, args, expr->as.call.args.count, receiver);
}

static Value eval_user_function(AstExpr *expr, FunctionDef *function) {
    return eval_user_function_with_receiver(expr, function, NULL);
}

/* If `instance` carries a `constructor` function field, invoke it with `this` =
 * the instance (no argument list — inputs arrive via `new … with {…}`, read from
 * this; first_class_functions_design.md §8). Returns 1 on success or when there
 * is no constructor; returns 0 if the constructor raised, in which case the
 * caller discards the half-built instance (§12.5: propagate, no instance). */
static int invoke_constructor(Value *instance) {
    RecordField *field = record_find(instance, "constructor");
    if (!field || field->value->kind != VALUE_FUNCTION) {
        return 1;
    }
    FunctionDef *ctor = function_resolve(field->value->as.function.library,
                                         field->value->as.function.name);
    if (!ctor) {
        runtime_error_raise("constructor references unknown function", 1003,
                            "constructor");
        return 0;
    }
    if (ctor->stmt->as.function.params.count != 0) {
        runtime_error_raise("constructor must take no parameters "
                            "(inputs come through `with`)", 1003, "constructor");
        return 0;
    }
    Value result = invoke_function(ctor->stmt, NULL, 0, instance);
    value_free(result);
    return !error_action_pending();
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
        double count = (double)string_codepoint_count(value.as.string,
                                                      string_length(value.as.string));
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
    size_t len = string_length(value.as.string);
    char *text = malloc(len + 1);
    if (!text) {
        abort();
    }
    for (size_t i = 0; i < len; i++) {
        text[i] = ascii_tolower((unsigned char)value.as.string[i]);
    }
    text[len] = '\0';
    Value result = value_string_n(text, len);
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
    size_t len = string_length(value.as.string);
    char *text = malloc(len + 1);
    if (!text) {
        abort();
    }
    for (size_t i = 0; i < len; i++) {
        text[i] = ascii_toupper((unsigned char)value.as.string[i]);
    }
    text[len] = '\0';
    Value result = value_string_n(text, len);
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

/* Reverse a string by codepoint: each codepoint's bytes stay in order, but the
 * codepoints are mirrored, so multibyte characters survive intact (lenient
 * invalid-UTF-8 bytes reverse as single units). Strings are immutable values, so
 * this returns a fresh string and never mutates in place. */
static Value reverse_string_value(const char *s, size_t len) {
    char *out = malloc(len + 1);
    if (!out) {
        abort();
    }
    size_t pos = 0;
    size_t out_end = len;
    while (pos < len) {
        unsigned cp;
        size_t n = utf8_decode_first(s + pos, len - pos, &cp);
        out_end -= n;
        memcpy(out + out_end, s + pos, n);
        pos += n;
    }
    out[len] = '\0';
    Value result = value_string_n(out, len);
    free(out);
    return result;
}

static Value reverse_array_value(Value array) {
    if (array.kind != VALUE_ARRAY) {
        value_free(array);
        runtime_error_raise("reverse expects an array or string", 1003, "invalid function call");
        return value_null();
    }
    reverse_array_items(array.as.array.items, array.as.array.count);
    return array;
}

static Value reverse_array_ref(Value *array, int *changed) {
    *changed = 0;
    if (!array || array->kind != VALUE_ARRAY) {
        runtime_error_raise("reverse expects an array or string", 1003, "invalid function call");
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
        return string_value_equal(left.as.string, right.as.string);
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
        return string_value_compare(left->as.string, right->as.string);
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
    case VALUE_XML_READER:
        return "xml_reader";
    case VALUE_GOBJECT:
        return "gobject";
    case VALUE_ACTOR:
        return "actor";
    case VALUE_FUNCTION:
        return "function";
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

/* Emit a JSON string literal for `length` bytes (binary-safe: interior NULs and
 * other control bytes are escaped as \u00XX, so the encoded text never contains a
 * raw NUL and round-trips through decode). */
static void encode_string_literal(StringBuilder *builder, const char *text, size_t length) {
    sb_append_char(builder, '"');
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)text[i];
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
        } else if (ch < 0x20) {
            char escape[8];
            snprintf(escape, sizeof(escape), "\\u%04x", ch);
            sb_append_text(builder, escape);
        } else {
            sb_append_char(builder, (char)ch);
        }
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
        format_number(buffer, sizeof(buffer), value.as.number);
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
    case VALUE_XML_READER:
        value_free(value);
        return value_string("<xml_reader>");
    case VALUE_GOBJECT:
        value_free(value);
        return value_string("<gobject>");
    case VALUE_ACTOR:
        value_free(value);
        return value_string("<actor>");
    case VALUE_FUNCTION:
        snprintf(buffer, sizeof(buffer), "<function %s>", value.as.function.name);
        value_free(value);
        return value_string(buffer);
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
        encode_string_literal(builder, value.as.string, string_length(value.as.string));
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
            encode_string_literal(builder, value.as.record.fields[i].name,
                                  strlen(value.as.record.fields[i].name));
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
    case VALUE_XML_READER:
    case VALUE_GOBJECT:
    case VALUE_ACTOR:
    case VALUE_FUNCTION:
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

/* --- Multiprocessing Phase 0: value serialization ----------------------------
 * (docs/multiprocessing_design.md §5). `serialize(value)` produces a binary-safe
 * string of bytes; `deserialize(string)` reconstructs the value. The wire format
 * is a small self-describing, length-prefixed binary encoding. It is the shared
 * foundation the future actor transport (fork+exec of the same `gbasic`) sends
 * over a mailbox; because round-trips are always same-binary, native fixed-width
 * encodings are written directly. Non-sendable kinds (live DB connections) and
 * over-deep/cyclic structures raise a structured `actor` error. Records lose PBI
 * policy on the round-trip (a snapshot is plain `copy`), matching §6. */

#define SER_MAGIC0 'g'
#define SER_MAGIC1 'B'
#define SER_MAGIC2 'S'
#define SER_VERSION 1
#define SER_MAX_DEPTH 256

typedef enum {
    SER_NULL = 1,
    SER_UNKNOWN,
    SER_BOOL,
    SER_NUMBER,
    SER_STRING,
    SER_ARRAY,
    SER_RECORD,
    SER_DATETIME,
    SER_DURATION,
    SER_MONEY,
    SER_FILE,
    SER_DIR,
    SER_ACTOR,  /* spawn-args only: an actor handle, realized by an inherited fd */
    SER_FUNCTION /* a function value: registered name (+ optional library), §10 */
} SerTag;

/* --- fd transfer for spawn arguments (docs/multiprocessing_design.md §4.1) ----
 * Actor handles are a live capability, not plain data, so serialize()/send()
 * reject them. The one exception is `spawn`'s argument frame: a handle there is
 * realized by inheriting its mailbox write-end fd across fork+exec. While
 * serializing a spawn frame the parent installs a SpawnFdXfer; serialize_value
 * then emits SER_ACTOR carrying the (dup'd, FD_CLOEXEC-cleared) fd number the
 * child will inherit, and records that fd so the parent can close it after the
 * fork. The child installs a non-NULL xfer too, so deserialize_value accepts
 * SER_ACTOR and wraps the inherited fd. Everywhere else the xfer is NULL and
 * SER_ACTOR is rejected. The interpreter is single-threaded within one process,
 * so a file-scope context pointer is safe (no nested serialization runs). */
typedef struct {
    int *fds;       /* fds the parent dup'd for inheritance (to close post-fork) */
    size_t count;
    size_t capacity;
    int failed;     /* a dup/fcntl failed mid-walk */
} SpawnFdXfer;

static SpawnFdXfer *active_spawn_xfer = NULL;

/* Non-zero only while a child actor is deserializing its startup-argument frame,
 * which is the one context where SER_ACTOR (an inherited handle fd) is accepted
 * on the read side. */
static int active_spawn_recv = 0;

/* Wrap an already-open, inherited mailbox write fd as an actor handle the child
 * owns (defined with the rest of the actor machinery below). */
static Value actor_handle_adopt_fd(int fd);

static void spawn_xfer_record(SpawnFdXfer *x, int fd) {
    if (x->count == x->capacity) {
        size_t cap = x->capacity ? x->capacity * 2 : 4;
        int *next = realloc(x->fds, sizeof(int) * cap);
        if (!next) {
            abort();
        }
        x->fds = next;
        x->capacity = cap;
    }
    x->fds[x->count++] = fd;
}

/* --- runtime handle passing (docs/multiprocessing_design.md §4.1, Phase 2) -----
 * A message sent to a running actor may itself contain actor handles (giving the
 * receiver a channel to a third actor). Unlike spawn, no fork occurs, so the
 * handle's write fd travels as SCM_RIGHTS ancillary data on the same frame
 * (channel_send_fds / channel_recv_fds). While send() serializes the message an
 * ActorMsgSend collects each handle's write fd and emits SER_ACTOR carrying its
 * INDEX into that fd array; receive() installs an ActorMsgRecv holding the fds it
 * received (in the same order) so SER_ACTOR resolves index -> a freshly adopted
 * fd. serialize()/spawn use their own contexts; with none installed SER_ACTOR is
 * rejected. */
typedef struct {
    int fds[ACTOR_MAX_MESSAGE_FDS]; /* borrowed handle write fds, in walk order */
    size_t count;
    int overflow;                   /* more than ACTOR_MAX_MESSAGE_FDS handles */
} ActorMsgSend;

typedef struct {
    const int *fds;   /* descriptors received with the frame */
    size_t count;
    int *used;        /* which were bound to a handle (others get closed) */
} ActorMsgRecv;

static ActorMsgSend *active_msg_send = NULL;
static ActorMsgRecv *active_msg_recv = NULL;

/* Non-zero while serializing a strict send (§6 / Phase 3c): a record field that
 * still carries a live PBI `link` policy is diagnosed rather than silently
 * degraded to a copy. Off by default, so plain send() stays total. */
static int active_serialize_strict = 0;

typedef struct {
    char *bytes;
    size_t length;
    size_t capacity;
} SerBuf;

static void serbuf_init(SerBuf *b) {
    b->capacity = 64;
    b->length = 0;
    b->bytes = malloc(b->capacity);
    if (!b->bytes) {
        abort();
    }
}

static void serbuf_append(SerBuf *b, const void *data, size_t n) {
    if (b->length + n > b->capacity) {
        while (b->length + n > b->capacity) {
            b->capacity *= 2;
        }
        char *grown = realloc(b->bytes, b->capacity);
        if (!grown) {
            abort();
        }
        b->bytes = grown;
    }
    memcpy(b->bytes + b->length, data, n);
    b->length += n;
}

static void serbuf_u8(SerBuf *b, unsigned char v) {
    serbuf_append(b, &v, 1);
}

static void serbuf_u64(SerBuf *b, uint64_t v) {
    serbuf_append(b, &v, sizeof v);
}

/* Append `len` length-prefixed bytes (used for strings, paths, field names). */
static void serbuf_blob(SerBuf *b, const char *data, size_t len) {
    serbuf_u64(b, (uint64_t)len);
    serbuf_append(b, data, len);
}

/* Returns 1 on success; on a non-sendable kind or excessive depth it raises a
 * structured error and returns 0. */
static int serialize_value(SerBuf *b, Value v, int depth) {
    if (depth > SER_MAX_DEPTH) {
        runtime_error_raise("serialize: value nested too deeply (possible cycle)",
                            1003, "actor");
        return 0;
    }
    switch (v.kind) {
    case VALUE_NULL:
        serbuf_u8(b, SER_NULL);
        return 1;
    case VALUE_UNKNOWN:
        serbuf_u8(b, SER_UNKNOWN);
        return 1;
    case VALUE_BOOL:
        serbuf_u8(b, SER_BOOL);
        serbuf_u8(b, v.as.boolean ? 1 : 0);
        return 1;
    case VALUE_NUMBER:
        serbuf_u8(b, SER_NUMBER);
        serbuf_append(b, &v.as.number, sizeof v.as.number);
        return 1;
    case VALUE_STRING:
        serbuf_u8(b, SER_STRING);
        serbuf_blob(b, v.as.string, string_length(v.as.string));
        return 1;
    case VALUE_ARRAY:
        serbuf_u8(b, SER_ARRAY);
        serbuf_u64(b, (uint64_t)v.as.array.count);
        for (size_t i = 0; i < v.as.array.count; i++) {
            if (!serialize_value(b, v.as.array.items[i], depth + 1)) {
                return 0;
            }
        }
        return 1;
    case VALUE_RECORD:
        serbuf_u8(b, SER_RECORD);
        serbuf_u64(b, (uint64_t)v.as.record.count);
        for (size_t i = 0; i < v.as.record.count; i++) {
            RecordField *f = &v.as.record.fields[i];
            if (active_serialize_strict && f->policy == AST_FIELD_POLICY_LINK) {
                /* §6: crossing the boundary copies everything, so a `link` loses
                 * its write-through identity. Strict mode reports that rather than
                 * degrading it silently. */
                char message[256];
                snprintf(message, sizeof message,
                         "send: strict: field '%s' is a live link and loses its "
                         "shared identity across the actor boundary",
                         f->name);
                runtime_error_raise(message, 1003, "actor");
                return 0;
            }
            serbuf_blob(b, f->name, strlen(f->name));
            if (!serialize_value(b, *f->value, depth + 1)) {
                return 0;
            }
        }
        return 1;
    case VALUE_DATETIME:
        serbuf_u8(b, SER_DATETIME);
        serbuf_append(b, &v.as.datetime, sizeof v.as.datetime);
        return 1;
    case VALUE_DURATION:
        serbuf_u8(b, SER_DURATION);
        serbuf_append(b, &v.as.duration, sizeof v.as.duration);
        return 1;
    case VALUE_MONEY:
        serbuf_u8(b, SER_MONEY);
        serbuf_append(b, &v.as.cents, sizeof v.as.cents);
        return 1;
    case VALUE_FILE:
        serbuf_u8(b, SER_FILE);
        serbuf_blob(b, v.as.file_path, strlen(v.as.file_path));
        return 1;
    case VALUE_DIR:
        serbuf_u8(b, SER_DIR);
        serbuf_blob(b, v.as.dir_path, strlen(v.as.dir_path));
        return 1;
    case VALUE_POSTGRES_CONNECTION:
    case VALUE_SQLITE_CONNECTION:
    case VALUE_XML_READER:
        runtime_error_raise("serialize: database connections cannot be serialized",
                            1003, "actor");
        return 0;
    case VALUE_GOBJECT:
        runtime_error_raise("serialize: gobjects cannot be serialized",
                            1003, "actor");
        return 0;
    case VALUE_ACTOR:
        /* Handles are a live capability, not data. The user-facing serialize()
         * and any message that embeds a handle (send()) are rejected — those run
         * with no xfer installed. Only a spawn-argument frame (active_spawn_xfer
         * set) may carry a handle, realized by inheriting its mailbox write fd
         * across fork+exec (§4.1). */
        if (active_spawn_xfer) {
            SpawnFdXfer *x = active_spawn_xfer;
            int dup_fd = fcntl(v.as.actor->write_fd, F_DUPFD_CLOEXEC, 3);
            int cleared = -1;
            if (dup_fd >= 0) {
                /* The child must inherit this fd across exec, so clear CLOEXEC. */
                int flags = fcntl(dup_fd, F_GETFD, 0);
                cleared = (flags < 0) ? -1 : fcntl(dup_fd, F_SETFD, flags & ~FD_CLOEXEC);
            }
            if (dup_fd < 0 || cleared < 0) {
                if (dup_fd >= 0) {
                    close(dup_fd);
                }
                x->failed = 1;
                runtime_error_raise("spawn: could not pass actor handle to child",
                                    1004, "actor");
                return 0;
            }
            spawn_xfer_record(x, dup_fd);
            serbuf_u8(b, SER_ACTOR);
            serbuf_u64(b, (uint64_t)dup_fd);
            return 1;
        }
        if (active_msg_send) {
            /* Runtime handle passing: the fd ships as SCM_RIGHTS; the frame only
             * records its position in the attached-fd array. */
            ActorMsgSend *m = active_msg_send;
            if (m->count >= ACTOR_MAX_MESSAGE_FDS) {
                m->overflow = 1;
                runtime_error_raise("send: too many actor handles in one message",
                                    1004, "actor");
                return 0;
            }
            uint64_t index = (uint64_t)m->count;
            m->fds[m->count++] = v.as.actor->write_fd;   /* borrowed, not closed */
            serbuf_u8(b, SER_ACTOR);
            serbuf_u64(b, index);
            return 1;
        }
        runtime_error_raise("serialize: actor handles cannot be serialized",
                            1003, "actor");
        return 0;
    case VALUE_FUNCTION:
        /* A function value travels as its registered name (+ owning library), not
         * as code or captured state (§10). The receiver resolves it through its
         * own registry — within one program (the actor case execs the same
         * program) it always resolves, like a spawn entry name. */
        serbuf_u8(b, SER_FUNCTION);
        serbuf_blob(b, v.as.function.name, strlen(v.as.function.name));
        if (v.as.function.library) {
            serbuf_u8(b, 1);
            serbuf_blob(b, v.as.function.library, strlen(v.as.function.library));
        } else {
            serbuf_u8(b, 0);
        }
        return 1;
    }
    runtime_error_raise("serialize: unsupported value", 1003, "actor");
    return 0;
}

/* Serialize a value into a freshly malloc'd byte frame (magic + version +
 * value). Returns 1 with the output pointer and length set (caller frees the
 * buffer), or 0 after raising. Does not consume `value`. Shared by the
 * serialize() builtin and the actor send() path. */
static int serialize_to_buffer(Value value, char **out, size_t *out_len) {
    SerBuf b;
    serbuf_init(&b);
    serbuf_u8(&b, SER_MAGIC0);
    serbuf_u8(&b, SER_MAGIC1);
    serbuf_u8(&b, SER_MAGIC2);
    serbuf_u8(&b, SER_VERSION);
    if (!serialize_value(&b, value, 0)) {
        free(b.bytes);
        return 0;
    }
    *out = b.bytes;
    *out_len = b.length;
    return 1;
}

static Value builtin_serialize_value(Value value) {
    char *bytes = NULL;
    size_t len = 0;
    if (!serialize_to_buffer(value, &bytes, &len)) {
        value_free(value);
        return value_null();
    }
    Value result = value_string_n(bytes, len);
    free(bytes);
    value_free(value);
    return result;
}

typedef struct {
    const char *data;
    size_t len;
    size_t pos;
    int ok;
} SerReader;

static int serread_bytes(SerReader *r, void *out, size_t n) {
    if (!r->ok || r->pos + n > r->len) {
        r->ok = 0;
        return 0;
    }
    memcpy(out, r->data + r->pos, n);
    r->pos += n;
    return 1;
}

static unsigned char serread_u8(SerReader *r) {
    unsigned char v = 0;
    serread_bytes(r, &v, 1);
    return v;
}

static uint64_t serread_u64(SerReader *r) {
    uint64_t v = 0;
    serread_bytes(r, &v, sizeof v);
    return v;
}

/* Reconstruct one value. On malformed/truncated input sets `r->ok = 0` and
 * returns a null placeholder; callers check `r->ok`. */
static Value deserialize_value(SerReader *r, int depth) {
    if (!r->ok || depth > SER_MAX_DEPTH) {
        r->ok = 0;
        return value_null();
    }
    unsigned char tag = serread_u8(r);
    if (!r->ok) {
        return value_null();
    }
    switch (tag) {
    case SER_NULL:
        return value_null();
    case SER_UNKNOWN:
        return value_unknown();
    case SER_BOOL:
        return value_bool(serread_u8(r) ? 1 : 0);
    case SER_NUMBER: {
        double d = 0;
        serread_bytes(r, &d, sizeof d);
        return value_number(d);
    }
    case SER_STRING:
    case SER_FILE:
    case SER_DIR: {
        uint64_t len = serread_u64(r);
        if (!r->ok || len > r->len - r->pos) {
            r->ok = 0;
            return value_null();
        }
        const char *bytes = r->data + r->pos;
        r->pos += (size_t)len;
        if (tag == SER_STRING) {
            return value_string_n(bytes, (size_t)len);
        }
        char *path = malloc((size_t)len + 1);
        if (!path) {
            abort();
        }
        memcpy(path, bytes, (size_t)len);
        path[len] = '\0';
        Value v = {0};
        v.kind = (tag == SER_FILE) ? VALUE_FILE : VALUE_DIR;
        if (tag == SER_FILE) {
            v.as.file_path = path;
        } else {
            v.as.dir_path = path;
        }
        return v;
    }
    case SER_ARRAY: {
        uint64_t count = serread_u64(r);
        /* Each element is at least one tag byte, so a count larger than the
         * bytes left is corrupt — guard before allocating. */
        if (!r->ok || count > r->len - r->pos) {
            r->ok = 0;
            return value_null();
        }
        Value *items = count ? calloc((size_t)count, sizeof(Value)) : NULL;
        if (count && !items) {
            abort();
        }
        for (uint64_t i = 0; i < count; i++) {
            items[i] = deserialize_value(r, depth + 1);
            if (!r->ok) {
                for (uint64_t j = 0; j < i; j++) {
                    value_free(items[j]);
                }
                free(items);
                return value_null();
            }
        }
        return value_array(items, (size_t)count);
    }
    case SER_RECORD: {
        uint64_t count = serread_u64(r);
        if (!r->ok || count > r->len - r->pos) {
            r->ok = 0;
            return value_null();
        }
        RecordField *fields = count ? calloc((size_t)count, sizeof(RecordField)) : NULL;
        if (count && !fields) {
            abort();
        }
        for (uint64_t i = 0; i < count; i++) {
            uint64_t nlen = serread_u64(r);
            if (!r->ok || nlen > r->len - r->pos) {
                r->ok = 0;
                Value partial = value_record(fields, (size_t)i);
                value_free(partial);
                return value_null();
            }
            char *name = malloc((size_t)nlen + 1);
            if (!name) {
                abort();
            }
            memcpy(name, r->data + r->pos, (size_t)nlen);
            name[nlen] = '\0';
            r->pos += (size_t)nlen;
            fields[i].name = name;
            fields[i].value = cell_alloc();
            fields[i].policy = AST_FIELD_POLICY_COPY;
            fields[i].reset_expr = NULL;
            *fields[i].value = deserialize_value(r, depth + 1);
            if (!r->ok) {
                Value partial = value_record(fields, (size_t)i + 1);
                value_free(partial);
                return value_null();
            }
        }
        return value_record(fields, (size_t)count);
    }
    case SER_DATETIME: {
        Value v = {0};
        v.kind = VALUE_DATETIME;
        serread_bytes(r, &v.as.datetime, sizeof v.as.datetime);
        return v;
    }
    case SER_DURATION: {
        Value v = {0};
        v.kind = VALUE_DURATION;
        serread_bytes(r, &v.as.duration, sizeof v.as.duration);
        return v;
    }
    case SER_MONEY: {
        Value v = {0};
        v.kind = VALUE_MONEY;
        serread_bytes(r, &v.as.cents, sizeof v.as.cents);
        return v;
    }
    case SER_ACTOR: {
        uint64_t ref = serread_u64(r);
        if (!r->ok) {
            return value_null();
        }
        /* In a spawn frame the payload is the inherited fd number itself. */
        if (active_spawn_recv) {
            return actor_handle_adopt_fd((int)ref);
        }
        /* In a runtime message the payload indexes the SCM_RIGHTS fds received
         * with the frame. */
        if (active_msg_recv) {
            if (ref >= active_msg_recv->count) {
                r->ok = 0;
                return value_null();
            }
            active_msg_recv->used[ref] = 1;
            return actor_handle_adopt_fd(active_msg_recv->fds[ref]);
        }
        /* No fd-transfer context: a forged or corrupt handle tag. */
        r->ok = 0;
        return value_null();
    }
    case SER_FUNCTION: {
        uint64_t nlen = serread_u64(r);
        if (!r->ok || nlen > r->len - r->pos) {
            r->ok = 0;
            return value_null();
        }
        char *name = malloc((size_t)nlen + 1);
        if (!name) {
            abort();
        }
        memcpy(name, r->data + r->pos, (size_t)nlen);
        name[nlen] = '\0';
        r->pos += (size_t)nlen;

        char *library = NULL;
        unsigned char has_library = serread_u8(r);
        if (r->ok && has_library) {
            uint64_t llen = serread_u64(r);
            if (!r->ok || llen > r->len - r->pos) {
                free(name);
                r->ok = 0;
                return value_null();
            }
            library = malloc((size_t)llen + 1);
            if (!library) {
                abort();
            }
            memcpy(library, r->data + r->pos, (size_t)llen);
            library[llen] = '\0';
            r->pos += (size_t)llen;
        }
        if (!r->ok) {
            free(name);
            free(library);
            return value_null();
        }
        /* §10: the name must resolve in the receiving program (same shape as an
         * unknown spawn entry). Within one program it always does. */
        FunctionDef *fn = function_resolve(library, name);
        if (!fn) {
            free(name);
            free(library);
            r->ok = 0;
            return value_null();
        }
        Value v = value_function(name, library);
        free(name);
        free(library);
        return v;
    }
    default:
        r->ok = 0;
        return value_null();
    }
}

static Value builtin_deserialize_value(Value value) {
    if (value.kind != VALUE_STRING) {
        value_free(value);
        runtime_error_raise("deserialize: argument must be a string", 1003, "actor");
        return value_null();
    }
    SerReader r = { value.as.string, string_length(value.as.string), 0, 1 };
    if (r.len < 4 ||
        serread_u8(&r) != SER_MAGIC0 || serread_u8(&r) != SER_MAGIC1 ||
        serread_u8(&r) != SER_MAGIC2 || serread_u8(&r) != SER_VERSION) {
        value_free(value);
        runtime_error_raise("deserialize: not a valid serialized value", 1003, "actor");
        return value_null();
    }
    Value result = deserialize_value(&r, 0);
    /* A clean frame consumes exactly its bytes; trailing data is corruption. */
    if (!r.ok || r.pos != r.len) {
        value_free(result);
        value_free(value);
        runtime_error_raise("deserialize: corrupt or truncated serialized data",
                            1003, "actor");
        return value_null();
    }
    value_free(value);
    return result;
}

/* Reconstruct a value from raw frame bytes. Sets *ok and returns the value (null
 * on failure). Raises nothing — the caller chooses the message. Shared by the
 * actor receive() path with the deserialize() builtin's format. */
static Value deserialize_from_buffer(const char *data, size_t len, int *ok) {
    SerReader r = { data, len, 0, 1 };
    if (r.len < 4 ||
        serread_u8(&r) != SER_MAGIC0 || serread_u8(&r) != SER_MAGIC1 ||
        serread_u8(&r) != SER_MAGIC2 || serread_u8(&r) != SER_VERSION) {
        *ok = 0;
        return value_null();
    }
    Value result = deserialize_value(&r, 0);
    if (!r.ok || r.pos != r.len) {
        value_free(result);
        *ok = 0;
        return value_null();
    }
    *ok = 1;
    return result;
}

/* --- Multiprocessing Phase 1: actor mailboxes -------------------------------
 * (docs/multiprocessing_design.md §3-§4). Every interpreter is an actor with one
 * inbound mailbox. self() returns a handle to it; send() serializes a value and
 * delivers it as one frame to a handle's mailbox; receive() blocks for the next
 * frame and deserializes it.
 *
 * Phase 1c adds spawn: `spawn worker(args)` fork+execs a fresh interpreter
 * (`gbasic --actor worker <program>`) with its own inbound mailbox, returning a
 * handle the parent sends to. Handles in the spawn arguments — notably the
 * parent's own self() — are wired by inheriting their mailbox write fds across
 * exec (§3, §4.1), so a child can message its parent. The root interpreter
 * creates its mailbox lazily with a fresh socketpair; a spawned child instead
 * adopts the inbox/self fds the parent set up for it (actor_child_init). */

static Mailbox root_mailbox = { -1, -1 };
static int root_mailbox_ready = 0;
static uint64_t next_actor_id = 1;
static ActorHandle *root_actor_handle = NULL;

static int ensure_root_mailbox(void) {
    if (root_mailbox_ready) {
        return 1;
    }
    if (mailbox_open(&root_mailbox) != 0) {
        runtime_error_raise("actor: could not create mailbox", 1004, "actor");
        return 0;
    }
    root_actor_handle = malloc(sizeof(ActorHandle));
    if (!root_actor_handle) {
        abort();
    }
    /* The root permanently owns its mailbox write end; this registry reference
     * keeps refcount >= 1 so value_free never closes the live mailbox. */
    root_actor_handle->write_fd = root_mailbox.write_fd;
    root_actor_handle->id = next_actor_id++;
    root_actor_handle->ref_count = 1;
    root_mailbox_ready = 1;
    return 1;
}

static Value value_actor(ActorHandle *handle) {
    Value value = {0};
    value.kind = VALUE_ACTOR;
    value.as.actor = handle;
    return value;
}

/* Wrap an already-open mailbox write fd (inherited across exec, or otherwise
 * owned) as a fresh actor handle. The handle owns the fd: closing the last
 * reference closes it. */
static Value actor_handle_adopt_fd(int fd) {
    ActorHandle *handle = malloc(sizeof(ActorHandle));
    if (!handle) {
        abort();
    }
    handle->write_fd = fd;
    handle->id = next_actor_id++;
    handle->ref_count = 1;
    return value_actor(handle);
}

/* Initialize a spawned child's mailbox from the fds the parent set up: inbox_fd
 * is the read end of this actor's inbound mailbox; self_fd is a write end to that
 * same mailbox (a dup the parent passed so self() works in the child). Replaces
 * the lazy fresh-socketpair path for children. */
static void actor_child_init(int inbox_fd, int self_fd) {
    root_mailbox.read_fd = inbox_fd;
    root_mailbox.write_fd = self_fd;
    root_actor_handle = malloc(sizeof(ActorHandle));
    if (!root_actor_handle) {
        abort();
    }
    root_actor_handle->write_fd = self_fd;
    root_actor_handle->id = next_actor_id++;
    root_actor_handle->ref_count = 1;
    root_mailbox_ready = 1;
}

/* --- spawned-child bookkeeping (orphan cleanup §7; death reasons §7.1) -------
 * Every actor this interpreter spawns joins one dedicated process group it owns,
 * and its pid is tracked so the group can be torn down and reaped on exit. The
 * group is never the inherited one (which may hold a parent shell pipeline).
 *
 * Phase 3 extends each record with the actor handle id the parent holds for that
 * child and, once reaped, the raw wait status -- so a monitor on a child this
 * interpreter spawned reports an ACCURATE death reason from the exit code rather
 * than the coarse mailbox-hangup fallback (§7.1, detection path 1). */
typedef struct {
    pid_t pid;
    uint64_t handle_id;   /* actor handle id for this child (0 until bound) */
    int reaped;           /* waitpid has collected this child */
    int status;           /* raw wait status, valid once reaped */
} ActorChild;

static pid_t actor_group_pgid = 0;
static ActorChild *actor_children = NULL;
static size_t actor_child_count = 0;

/* Track a freshly forked child; returns its record index so the caller can bind
 * the handle id once the parent's handle to the child has been created. */
static size_t actor_track_child(pid_t pid) {
    ActorChild *next = realloc(actor_children,
                               sizeof(ActorChild) * (actor_child_count + 1));
    if (!next) {
        abort();
    }
    actor_children = next;
    actor_children[actor_child_count].pid = pid;
    actor_children[actor_child_count].handle_id = 0;
    actor_children[actor_child_count].reaped = 0;
    actor_children[actor_child_count].status = 0;
    return actor_child_count++;
}

/* Reap any exited children without blocking, capturing each one's wait status so
 * a later monitor death-notification can report the precise reason. */
static void actor_reap_children(void) {
    for (size_t i = 0; i < actor_child_count; i++) {
        if (actor_children[i].reaped) {
            continue;
        }
        int status = 0;
        pid_t r;
        while ((r = waitpid(actor_children[i].pid, &status, WNOHANG)) < 0 &&
               errno == EINTR) {
            /* retry */
        }
        if (r == actor_children[i].pid) {
            actor_children[i].reaped = 1;
            actor_children[i].status = status;
        }
    }
}

/* If `handle_id` names a child this interpreter spawned, ensure it is reaped and
 * store its raw wait status in *status, returning 1. Callers reach here only once
 * the child's mailbox has hung up (POLLHUP), which guarantees the child is exiting
 * -- so a blocking waitpid closes the race where POLLHUP is observed a moment
 * before the zombie becomes reapable, without risking an indefinite stall. A
 * target that is not one of our children returns 0 (it gets the coarse reason). */
static int actor_child_wait_status(uint64_t handle_id, int *status) {
    if (handle_id == 0) {
        return 0;
    }
    for (size_t i = 0; i < actor_child_count; i++) {
        if (actor_children[i].handle_id != handle_id) {
            continue;
        }
        if (!actor_children[i].reaped) {
            int st = 0;
            pid_t r;
            while ((r = waitpid(actor_children[i].pid, &st, 0)) < 0 &&
                   errno == EINTR) {
                /* retry */
            }
            if (r == actor_children[i].pid) {
                actor_children[i].reaped = 1;
                actor_children[i].status = st;
            }
        }
        if (actor_children[i].reaped) {
            *status = actor_children[i].status;
            return 1;
        }
        return 0;
    }
    return 0;
}

static void actor_cleanup_children(void) {
    if (actor_group_pgid > 0) {
        /* Signal the whole actor group; harmless if members already exited. */
        kill(-actor_group_pgid, SIGTERM);
    }
    for (size_t i = 0; i < actor_child_count; i++) {
        if (actor_children[i].reaped) {
            continue;   /* already collected by actor_reap_children */
        }
        int status = 0;
        while (waitpid(actor_children[i].pid, &status, 0) < 0 && errno == EINTR) {
            /* retry */
        }
    }
    free(actor_children);
    actor_children = NULL;
    actor_child_count = 0;
    actor_group_pgid = 0;
}

static Value builtin_actor_self(void) {
    if (!ensure_root_mailbox()) {
        return value_null();
    }
    root_actor_handle->ref_count++;
    return value_actor(root_actor_handle);
}

static Value builtin_actor_send(Value handle, Value message, int strict) {
    if (handle.kind != VALUE_ACTOR) {
        value_free(handle);
        value_free(message);
        runtime_error_raise("send: first argument must be an actor handle",
                            1003, "actor");
        return value_null();
    }
    /* Serialize the message, collecting the write fds of any actor handles it
     * contains so they ride along as SCM_RIGHTS (runtime handle passing). In
     * strict mode a live PBI `link` field is diagnosed instead of degrading. */
    ActorMsgSend xfer = {{0}, 0, 0};
    active_msg_send = &xfer;
    active_serialize_strict = strict;
    char *bytes = NULL;
    size_t len = 0;
    int serialized = serialize_to_buffer(message, &bytes, &len);
    active_serialize_strict = 0;
    active_msg_send = NULL;
    if (!serialized || xfer.overflow) {
        /* serialize_value already raised (non-sendable content / too many fds). */
        free(bytes);
        value_free(handle);
        value_free(message);
        return value_null();
    }
    int rc = channel_send_fds(handle.as.actor->write_fd, bytes, len,
                              xfer.count ? xfer.fds : NULL, xfer.count);
    free(bytes);
    value_free(handle);
    value_free(message);   /* closes the handles' fds; the receiver has its own */
    switch (rc) {
    case ACTOR_CHANNEL_OK:
        return value_null();
    case ACTOR_CHANNEL_FULL:
        runtime_error_raise("send: target mailbox is full", 1004, "actor");
        return value_null();
    case ACTOR_CHANNEL_TOOBIG:
        runtime_error_raise("send: message is too large for one frame",
                            1004, "actor");
        return value_null();
    default:
        runtime_error_raise("send: target actor is no longer reachable",
                            1004, "actor");
        return value_null();
    }
}

/* Read and deserialize exactly one frame from this actor's mailbox, binding any
 * SCM_RIGHTS descriptors it carries to the handles in the value. Returns an
 * ACTOR_RECV_* code; on ACTOR_RECV_OK the value is in *out. Does not raise. */
static int actor_recv_one(Value *out) {
    void *bytes = NULL;
    size_t len = 0;
    int *fds = NULL;
    size_t nfds = 0;
    int rc = channel_recv_fds(root_mailbox.read_fd, &bytes, &len, &fds, &nfds);
    if (rc != ACTOR_RECV_OK) {
        return rc;
    }

    int *used = nfds ? calloc(nfds, sizeof(int)) : NULL;
    ActorMsgRecv recv_xfer = { fds, nfds, used };
    active_msg_recv = &recv_xfer;
    int ok = 0;
    Value result = deserialize_from_buffer(bytes, len, &ok);
    active_msg_recv = NULL;
    free(bytes);

    /* Any descriptor the frame did not claim would otherwise leak. */
    for (size_t i = 0; i < nfds; i++) {
        if (!used || !used[i]) {
            close(fds[i]);
        }
    }
    free(used);
    free(fds);

    if (!ok) {
        value_free(result);
        return ACTOR_RECV_ERROR;
    }
    *out = result;
    return ACTOR_RECV_OK;
}

/* Userspace retain queue for selective receive (§9.2): messages pulled from the
 * mailbox that did not match a `receive(tag)` are held here, in arrival order,
 * and offered to later receives. A plain `receive()` always takes the oldest
 * retained message first, so strict FIFO is preserved across the two forms. */
typedef struct RetainedMsg {
    Value value;
    struct RetainedMsg *next;
} RetainedMsg;

static RetainedMsg *retain_head = NULL;
static RetainedMsg *retain_tail = NULL;

static void retain_append(Value value) {
    RetainedMsg *node = malloc(sizeof(RetainedMsg));
    if (!node) {
        abort();
    }
    node->value = value;
    node->next = NULL;
    if (retain_tail) {
        retain_tail->next = node;
    } else {
        retain_head = node;
    }
    retain_tail = node;
}

static void retain_clear(void) {
    RetainedMsg *node = retain_head;
    while (node) {
        RetainedMsg *next = node->next;
        value_free(node->value);
        free(node);
        node = next;
    }
    retain_head = NULL;
    retain_tail = NULL;
}

/* A message's selector tag: the message itself if it is a string, or its first
 * element if it is a non-empty array (the tagged-tuple convention). Other shapes
 * have no tag and are never selected by `receive(tag)`. Returns 1 and sets *tag
 * (a borrowed reference into `msg`) when a tag exists. */
static int message_tag(Value msg, Value *tag) {
    if (msg.kind == VALUE_STRING) {
        *tag = msg;
        return 1;
    }
    if (msg.kind == VALUE_ARRAY && msg.as.array.count > 0) {
        *tag = msg.as.array.items[0];
        return 1;
    }
    return 0;
}

static int message_matches(Value msg, Value tag) {
    Value mtag;
    if (!message_tag(msg, &mtag)) {
        return 0;
    }
    return values_equal(value_copy(mtag), value_copy(tag));
}

/* Detach and return a retained message: the oldest (tag == NULL) or the first
 * whose tag matches *tag. Returns 1 and sets *out when one is found. */
static int retain_take(const Value *tag, Value *out) {
    RetainedMsg *prev = NULL;
    for (RetainedMsg *node = retain_head; node; prev = node, node = node->next) {
        if (tag && !message_matches(node->value, *tag)) {
            continue;
        }
        if (prev) {
            prev->next = node->next;
        } else {
            retain_head = node->next;
        }
        if (node == retain_tail) {
            retain_tail = prev;
        }
        *out = node->value;
        free(node);
        return 1;
    }
    return 0;
}

static long long monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Total milliseconds in a duration used as a timeout. Calendar units use nominal
 * lengths (year = 365 days, month = 30 days); timeouts are normally seconds to
 * hours, so the approximation is immaterial. */
static long long duration_to_ms(Duration d) {
    long long secs = 0;
    secs += (long long)d.years * 365 * 24 * 3600;
    secs += (long long)d.months * 30 * 24 * 3600;
    secs += (long long)d.weeks * 7 * 24 * 3600;
    secs += (long long)d.days * 24 * 3600;
    secs += (long long)d.hours * 3600;
    secs += (long long)d.minutes * 60;
    secs += d.seconds;
    return secs * 1000;
}

/* --- death notification / monitors (§7.1) ----------------------------------
 * monitor(handle) registers the caller's interest in another actor's death. The
 * monitor keeps a copy of the target handle -- which holds the target's mailbox
 * write end, our detection fd -- and a unique reference the caller can later pass
 * to demonitor(). When the target process dies its inbox read end closes, so our
 * held write end reports POLLHUP; we synthesize the ordinary tagged message
 * ["down", handle, reason] into the retain buffer, where it is delivered through
 * normal receive() (and matched by receive("down")). A child this interpreter
 * spawned yields an accurate reason from its captured exit status (path 1); any
 * other target yields the coarse reason "down" (path 2). One down per reference. */
typedef struct Monitor {
    uint64_t ref;       /* reference returned to gBASIC; demonitor() cancels by it */
    Value target;       /* retained copy of the monitored handle (VALUE_ACTOR) */
    struct Monitor *next;
} Monitor;

static Monitor *monitor_head = NULL;
static uint64_t next_monitor_ref = 1;

static void monitor_clear(void) {
    Monitor *m = monitor_head;
    while (m) {
        Monitor *next = m->next;
        value_free(m->target);
        free(m);
        m = next;
    }
    monitor_head = NULL;
}

/* Map a reaped child's raw wait status to a death reason (§7.1 table). */
static const char *reason_from_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status) == 0 ? "normal" : "error";
    }
    if (WIFSIGNALED(status)) {
        return "killed";
    }
    return "down";
}

/* Accurate reason if we spawned (and have reaped) the target; else `fallback`. */
static const char *monitor_reason(uint64_t target_id, const char *fallback) {
    int status;
    if (actor_child_wait_status(target_id, &status)) {
        return reason_from_status(status);
    }
    return fallback;
}

/* Build the death message ["down", handle, reason]; takes ownership of `handle`. */
static Value make_down_message(Value handle, const char *reason) {
    Value *items = malloc(sizeof(Value) * 3);
    if (!items) {
        abort();
    }
    items[0] = value_string("down");
    items[1] = handle;
    items[2] = value_string(reason);
    return value_array(items, 3);
}

/* Has the target behind this mailbox write fd gone (its read end closed)? */
static int monitor_fd_hung_up(int write_fd) {
    struct pollfd p = { write_fd, 0, 0 };
    int r = poll(&p, 1, 0);
    return r > 0 && (p.revents & (POLLHUP | POLLERR | POLLNVAL));
}

/* Reap children, then synthesize a "down" for every monitor whose target has
 * died, enqueueing it into the retain buffer and retiring that monitor (one down
 * per reference). Never blocks. */
static void actor_collect_downs(void) {
    actor_reap_children();
    Monitor **pp = &monitor_head;
    while (*pp) {
        Monitor *m = *pp;
        if (monitor_fd_hung_up(m->target.as.actor->write_fd)) {
            const char *reason = monitor_reason(m->target.as.actor->id, "down");
            retain_append(make_down_message(value_copy(m->target), reason));
            *pp = m->next;
            value_free(m->target);
            free(m);
        } else {
            pp = &m->next;
        }
    }
}

/* Wait until the inbox is readable, a monitored actor dies, or timeout_ms passes
 * (negative blocks forever). Returns 1 (inbox), 2 (a monitor signalled), 0
 * (timeout), or -1 (error). The blocking receive path polls {inbox} U {monitored
 * fds} so a death wakes a receiver exactly as a message does (§7.1). */
static int actor_wait(int timeout_ms) {
    for (;;) {
        size_t nmon = 0;
        for (Monitor *m = monitor_head; m; m = m->next) {
            nmon++;
        }
        size_t n = 1 + nmon;
        struct pollfd *p = malloc(sizeof(struct pollfd) * n);
        if (!p) {
            abort();
        }
        p[0].fd = root_mailbox.read_fd;
        p[0].events = POLLIN;
        p[0].revents = 0;
        size_t i = 1;
        for (Monitor *m = monitor_head; m; m = m->next, i++) {
            p[i].fd = m->target.as.actor->write_fd;
            p[i].events = 0;   /* POLLHUP/POLLERR are reported regardless */
            p[i].revents = 0;
        }
        int r = poll(p, n, timeout_ms);
        if (r < 0) {
            int eintr = (errno == EINTR);
            free(p);
            if (eintr) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            free(p);
            return 0;
        }
        int inbox = p[0].revents != 0;
        int mon = 0;
        for (size_t k = 1; k < n; k++) {
            if (p[k].revents != 0) {
                mon = 1;
                break;
            }
        }
        free(p);
        if (inbox) {
            return 1;
        }
        if (mon) {
            return 2;
        }
        /* spurious wakeup: poll again */
    }
}

static Value builtin_actor_monitor(Value handle) {
    if (handle.kind != VALUE_ACTOR) {
        value_free(handle);
        runtime_error_raise("monitor: argument must be an actor handle",
                            1003, "actor");
        return value_null();
    }
    if (!ensure_root_mailbox()) {
        value_free(handle);
        return value_null();
    }
    Monitor *m = malloc(sizeof(Monitor));
    if (!m) {
        abort();
    }
    m->ref = next_monitor_ref++;
    m->target = handle;          /* take ownership of the caller's handle copy */
    m->next = monitor_head;
    monitor_head = m;
    uint64_t ref = m->ref;

    /* If the target is already gone, deliver the death now rather than lose it.
     * A child we spawned still yields its accurate reason; otherwise "noproc". */
    actor_reap_children();
    if (monitor_fd_hung_up(m->target.as.actor->write_fd)) {
        const char *reason = monitor_reason(m->target.as.actor->id, "noproc");
        retain_append(make_down_message(value_copy(m->target), reason));
        monitor_head = m->next;
        value_free(m->target);
        free(m);
    }
    return value_number((double)ref);
}

static Value builtin_actor_demonitor(Value ref) {
    if (ref.kind != VALUE_NUMBER) {
        value_free(ref);
        runtime_error_raise("demonitor: argument must be a monitor reference",
                            1003, "actor");
        return value_null();
    }
    uint64_t id = (uint64_t)ref.as.number;
    value_free(ref);
    for (Monitor **pp = &monitor_head; *pp; pp = &(*pp)->next) {
        if ((*pp)->ref == id) {
            Monitor *dead = *pp;
            *pp = dead->next;
            value_free(dead->target);
            free(dead);
            break;
        }
    }
    return value_null();
}

/* The single implementation behind every receive form. With `has_tag`, only a
 * message whose tag matches `tag` is returned and non-matches are retained
 * (selective receive); otherwise the oldest message is returned (FIFO). With
 * `has_timeout`, the call returns `nothing` if no qualifying message arrives
 * within `timeout_ms`. A monitored actor's death is folded in as a synthesized
 * ["down", ...] message each pass, so it is delivered like any other (§7.1).
 * Consumes `tag`. */
static Value actor_receive_impl(int has_tag, Value tag,
                                int has_timeout, long long timeout_ms) {
    if (!ensure_root_mailbox()) {
        if (has_tag) {
            value_free(tag);
        }
        return value_null();
    }

    long long deadline = has_timeout ? monotonic_ms() + timeout_ms : 0;
    for (;;) {
        /* Fold in any deaths first, so a synthesized ["down", ...] is offered to
         * this receive like a retained message (and matches receive("down")). */
        actor_collect_downs();
        Value taken;
        if (retain_take(has_tag ? &tag : NULL, &taken)) {
            if (has_tag) {
                value_free(tag);
            }
            return taken;
        }

        int wait_ms = -1;
        if (has_timeout) {
            long long remaining = deadline - monotonic_ms();
            if (remaining < 0) {
                remaining = 0;
            }
            wait_ms = remaining > INT_MAX ? INT_MAX : (int)remaining;
        }

        int w = actor_wait(wait_ms);
        if (w < 0) {
            if (has_tag) {
                value_free(tag);
            }
            runtime_error_raise("receive: could not wait on mailbox",
                                1004, "actor");
            return value_null();
        }
        if (w == 0) {
            /* Only reachable with a deadline: timed out -> nothing. */
            if (has_tag) {
                value_free(tag);
            }
            return value_null();
        }
        if (w == 2) {
            /* A monitored actor died; loop so actor_collect_downs synthesizes it. */
            continue;
        }

        /* w == 1: the inbox is readable. */
        Value msg = value_null();
        int rc = actor_recv_one(&msg);
        if (rc == ACTOR_RECV_CLOSED) {
            if (has_tag) {
                value_free(tag);
            }
            runtime_error_raise("receive: mailbox closed (no senders remain)",
                                1004, "actor");
            return value_null();
        }
        if (rc != ACTOR_RECV_OK) {
            if (has_tag) {
                value_free(tag);
            }
            runtime_error_raise("receive: received a corrupt message frame",
                                1004, "actor");
            return value_null();
        }
        if (!has_tag) {
            return msg;
        }
        if (message_matches(msg, tag)) {
            value_free(tag);
            return msg;
        }
        retain_append(msg);
    }
}

/* Find a top-level function definition by name in the loaded program. Both the
 * parent (validating a spawn entry, §3) and the child (locating the entry to
 * run) resolve the entry this way. */
static AstStmt *find_top_level_function(const char *name) {
    for (size_t i = 0; i < active_root.count; i++) {
        AstStmt *stmt = active_root.items[i];
        if (stmt->kind == AST_STMT_FUNCTION &&
            !stmt->as.function.object &&
            strcmp(stmt->as.function.name, name) == 0) {
            return stmt;
        }
    }
    return NULL;
}

/* Absolute path to this interpreter, so a spawned child re-execs the same
 * binary at the same program (§3). Caller frees; NULL on failure. */
static char *actor_self_exe_path(void) {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) {
        return NULL;
    }
    buf[(size_t)n] = '\0';
    return copy_string(buf);
}

/* Clear FD_CLOEXEC on fd so it survives exec into the child. Returns 0 on
 * success, -1 on failure. */
static int fd_clear_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC);
}

/* `spawn worker(args...)` — start a fresh interpreter as a new actor running
 * `worker`, returning a handle to its inbound mailbox (docs/multiprocessing_design.md
 * §3-§4). The parent validates the entry, serializes the arguments (inheriting
 * any handle fds they carry), enqueues them as the child's reserved first frame,
 * fork+execs `gbasic --actor`, and blocks on a control pipe until the child
 * reports ready. */
static Value eval_spawn(AstExpr *expr) {
    char *entry = expr->as.call.name;
    size_t argc = expr->as.call.args.count;

    if (!root_source_path || !root_source_path[0]) {
        runtime_error_raise("spawn requires a program loaded from a file",
                            1003, "actor");
        return value_null();
    }

    AstStmt *fn = find_top_level_function(entry);
    if (!fn) {
        char message[256];
        snprintf(message, sizeof message, "spawn: no function named %s", entry);
        runtime_error_raise(message, 1003, "actor");
        return value_null();
    }
    if (argc != fn->as.function.params.count) {
        char message[256];
        snprintf(message, sizeof message, "spawn: %s expects %zu arguments",
                 entry, fn->as.function.params.count);
        runtime_error_raise(message, 1003, "actor");
        return value_null();
    }

    char *exe = actor_self_exe_path();
    if (!exe) {
        runtime_error_raise("spawn: could not locate the interpreter executable",
                            1004, "actor");
        return value_null();
    }

    /* Evaluate the arguments; abort the spawn if any raised. */
    Value *items = argc ? malloc(sizeof(Value) * argc) : NULL;
    if (argc && !items) {
        abort();
    }
    size_t evaluated = 0;
    int arg_error = 0;
    for (size_t i = 0; i < argc; i++) {
        items[i] = eval_expr(expr->as.call.args.items[i]);
        evaluated++;
        if (error_action_pending() || runtime_stopped) {
            arg_error = 1;
            break;
        }
    }
    if (arg_error) {
        for (size_t i = 0; i < evaluated; i++) {
            value_free(items[i]);
        }
        free(items);
        free(exe);
        return value_null();
    }
    Value args_array = value_array(items, argc);

    /* The child's inbound mailbox: the parent keeps the write end as the handle
     * it sends to; the child inherits the read end as its inbox. */
    Mailbox child_box;
    if (mailbox_open(&child_box) != 0) {
        value_free(args_array);
        free(exe);
        runtime_error_raise("spawn: could not create child mailbox", 1004, "actor");
        return value_null();
    }

    /* A second write end (CLOEXEC cleared) the child inherits so its own self()
     * resolves to its inbox. */
    int child_self_fd = fcntl(child_box.write_fd, F_DUPFD_CLOEXEC, 3);
    if (child_self_fd < 0 || fd_clear_cloexec(child_self_fd) < 0) {
        if (child_self_fd >= 0) {
            close(child_self_fd);
        }
        mailbox_close(&child_box);
        value_free(args_array);
        free(exe);
        runtime_error_raise("spawn: could not set up child self handle",
                            1004, "actor");
        return value_null();
    }

    /* Control pipe: the child writes one status byte; the parent blocks on it. */
    int ctrl[2];
    if (pipe(ctrl) != 0) {
        close(child_self_fd);
        mailbox_close(&child_box);
        value_free(args_array);
        free(exe);
        runtime_error_raise("spawn: could not create control pipe", 1004, "actor");
        return value_null();
    }
    if (fd_clear_cloexec(ctrl[1]) < 0) {
        close(ctrl[0]);
        close(ctrl[1]);
        close(child_self_fd);
        mailbox_close(&child_box);
        value_free(args_array);
        free(exe);
        runtime_error_raise("spawn: could not prepare control pipe", 1004, "actor");
        return value_null();
    }

    /* Serialize the startup arguments, inheriting the write fd of any handle
     * they contain (active_spawn_xfer). */
    SpawnFdXfer xfer = {0};
    active_spawn_xfer = &xfer;
    char *frame = NULL;
    size_t frame_len = 0;
    int serialized = serialize_to_buffer(args_array, &frame, &frame_len);
    active_spawn_xfer = NULL;
    value_free(args_array);

    int fatal = 0;
    if (!serialized || xfer.failed) {
        fatal = 1;   /* serialize_value already raised */
    } else if (frame_len > channel_max_message(child_box.write_fd)) {
        runtime_error_raise("spawn: arguments are too large for one frame",
                            1004, "actor");
        fatal = 1;
    } else {
        /* Reserved startup frame: enqueued before the handle exists, so it is
         * always the child's first message (§3). */
        int sr = channel_send(child_box.write_fd, frame, frame_len);
        if (sr != ACTOR_CHANNEL_OK) {
            runtime_error_raise("spawn: could not deliver startup arguments",
                                1004, "actor");
            fatal = 1;
        }
    }
    free(frame);
    if (fatal) {
        for (size_t i = 0; i < xfer.count; i++) {
            close(xfer.fds[i]);
        }
        free(xfer.fds);
        close(ctrl[0]);
        close(ctrl[1]);
        close(child_self_fd);
        mailbox_close(&child_box);
        free(exe);
        return value_null();
    }

    pid_t pid = fork();
    if (pid < 0) {
        for (size_t i = 0; i < xfer.count; i++) {
            close(xfer.fds[i]);
        }
        free(xfer.fds);
        close(ctrl[0]);
        close(ctrl[1]);
        close(child_self_fd);
        mailbox_close(&child_box);
        free(exe);
        runtime_error_raise("spawn: could not fork", 1004, "actor");
        return value_null();
    }

    if (pid == 0) {
        /* ---- child, between fork and exec ---- */
        /* Join the dedicated actor process group (0 => become its leader). */
        setpgid(0, actor_group_pgid);

        /* Keep only the fds the fresh interpreter needs; close everything else so
         * no libpq/sqlite/gtk/server descriptor leaks across exec (§3). */
        int keep[3];
        keep[0] = child_box.read_fd;
        keep[1] = child_self_fd;
        keep[2] = ctrl[1];
        long maxfd = sysconf(_SC_OPEN_MAX);
        if (maxfd < 0) {
            maxfd = 1024;
        }
        for (int fd = 3; fd < (int)maxfd; fd++) {
            int keepit = 0;
            for (size_t k = 0; k < 3; k++) {
                if (keep[k] == fd) {
                    keepit = 1;
                    break;
                }
            }
            for (size_t k = 0; !keepit && k < xfer.count; k++) {
                if (xfer.fds[k] == fd) {
                    keepit = 1;
                }
            }
            if (!keepit) {
                close(fd);
            }
        }

        char inbox_s[16];
        char self_s[16];
        char ctrl_s[16];
        snprintf(inbox_s, sizeof inbox_s, "%d", child_box.read_fd);
        snprintf(self_s, sizeof self_s, "%d", child_self_fd);
        snprintf(ctrl_s, sizeof ctrl_s, "%d", ctrl[1]);
        char *child_argv[] = {
            exe, "--actor", entry, root_source_path,
            "--actor-inbox", inbox_s,
            "--actor-self", self_s,
            "--actor-control", ctrl_s,
            NULL
        };
        execv(exe, child_argv);
        /* exec failed: report failure to the parent and die. */
        char b = 'X';
        ssize_t w = write(ctrl[1], &b, 1);
        (void)w;
        _exit(127);
    }

    /* ---- parent ---- */
    if (actor_group_pgid == 0) {
        actor_group_pgid = pid;
    }
    setpgid(pid, actor_group_pgid);   /* harmless race with the child's setpgid */
    size_t child_index = actor_track_child(pid);

    /* Every child-only fd now belongs to the child. */
    close(child_box.read_fd);
    close(child_self_fd);
    close(ctrl[1]);
    for (size_t i = 0; i < xfer.count; i++) {
        close(xfer.fds[i]);
    }
    free(xfer.fds);
    free(exe);

    /* Block until the child reports ready ('R') or the pipe closes on failure. */
    char status = 0;
    ssize_t n;
    do {
        n = read(ctrl[0], &status, 1);
    } while (n < 0 && errno == EINTR);
    close(ctrl[0]);

    if (n == 1 && status == 'R') {
        Value handle = actor_handle_adopt_fd(child_box.write_fd);
        /* Bind this child's record to the handle so a monitor on it can recover
         * the accurate death reason from the child's exit status (§7.1). */
        actor_children[child_index].handle_id = handle.as.actor->id;
        return handle;
    }

    close(child_box.write_fd);   /* the child is reaped by actor_cleanup_children */
    runtime_error_raise("spawn: child actor failed to start", 1004, "actor");
    return value_null();
}

/* Entry point for a spawned actor process (`gbasic --actor entry program ...`).
 * Hoists the program's top-level definitions, adopts the inherited mailbox fds,
 * signals readiness on the control pipe, reads its reserved startup-argument
 * frame, and runs the entry function. Returns a process exit status. */
int eval_run_actor(AstStmtList program, const char *entry,
                   int inbox_fd, int self_fd, int control_fd) {
    active_root = program;

    /* Tie this actor's lifetime to its parent's: if the parent dies for any
     * reason -- normal exit, crash, or kill -- the kernel sends this process
     * SIGTERM (docs/multiprocessing_design.md §7). This cascades down a multi-level
     * actor tree and, unlike the parent's own cleanup pass, survives the parent
     * being killed by an uncatchable signal. The window between fork and this call
     * is covered by re-checking the parent below. */
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() == 1) {
        /* The spawning parent already exited before PDEATHSIG was armed; the
         * signal would never come, so leave now rather than orphan-loop. */
        return 0;
    }

    /* Register top-level functions, modifiers, and imports so the entry and its
     * helpers resolve (the normal top-level walk does not run for an actor). */
    for (size_t i = 0; i < program.count; i++) {
        AstStmt *stmt = program.items[i];
        if (stmt->kind == AST_STMT_FUNCTION && !stmt->as.function.object) {
            function_register(stmt);
        } else if (stmt->kind == AST_STMT_MODIFIER) {
            modifier_register(stmt);
        } else if (stmt->kind == AST_STMT_USE) {
            library_import(stmt->as.use_stmt.name, stmt->as.use_stmt.path);
        }
    }
    /* Methods attach via dotted-def statements the child never executes; register
     * their bodies (recursively, including inside the program block) so a received
     * record-with-method resolves (§10). */
    register_method_bodies_in(program);

    AstStmt *fn = find_top_level_function(entry);
    if (!fn) {
        char b = 'E';
        ssize_t w = write(control_fd, &b, 1);
        (void)w;
        close(control_fd);
        fprintf(stderr, "actor: no function named %s\n", entry);
        return 1;
    }

    actor_child_init(inbox_fd, self_fd);

    /* Ready: the parent's spawn() unblocks and returns the handle. */
    char ready = 'R';
    ssize_t w = write(control_fd, &ready, 1);
    (void)w;
    close(control_fd);

    /* The reserved first frame carries the serialized startup arguments. */
    void *bytes = NULL;
    size_t len = 0;
    if (channel_recv(inbox_fd, &bytes, &len) != ACTOR_RECV_OK) {
        fprintf(stderr, "actor: %s could not read startup arguments\n", entry);
        return 1;
    }
    active_spawn_recv = 1;
    int ok = 0;
    Value arg_value = deserialize_from_buffer(bytes, len, &ok);
    active_spawn_recv = 0;
    free(bytes);
    if (!ok || arg_value.kind != VALUE_ARRAY) {
        value_free(arg_value);
        fprintf(stderr, "actor: %s received corrupt startup arguments\n", entry);
        return 1;
    }
    size_t argc = arg_value.as.array.count;
    if (argc != fn->as.function.params.count) {
        value_free(arg_value);
        fprintf(stderr, "actor: %s startup argument count mismatch\n", entry);
        return 1;
    }

    /* Transfer ownership of the elements to invoke_function; free only the array
     * container so the values are not double-freed. */
    Value *args = argc ? malloc(sizeof(Value) * argc) : NULL;
    if (argc && !args) {
        abort();
    }
    for (size_t i = 0; i < argc; i++) {
        args[i] = arg_value.as.array.items[i];
    }
    free(arg_value.as.array.items);

    Value result = invoke_function(fn, args, argc, NULL);
    int exit_status = runtime_stopped ? 1 : 0;
    value_free(result);

    actor_cleanup_children();
    retain_clear();
    monitor_clear();
    function_clear();
    modifier_clear();
    env_clear(&global_env);
    return exit_status;
}

static Value builtin_quote_value(Value value) {
    char buffer[128];
    Value text;
    switch (value.kind) {
    case VALUE_STRING:
        text = value;
        break;
    case VALUE_NUMBER:
        format_number(buffer, sizeof(buffer), value.as.number);
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
    encode_string_literal(&builder, text.as.string, string_length(text.as.string));
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
            size_t len = builder.length;
            char *text = sb_take(&builder);
            Value result = value_string_n(text, len);
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
        fields[count].value = cell_alloc();
        if (!fields[count].value) {
            abort();
        }
        *fields[count].value = value;
        /* realloc does not zero the new slot; decoded fields default to copy. */
        fields[count].policy = AST_FIELD_POLICY_COPY;
        fields[count].reset_expr = NULL;
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
        cell_release(fields[i].value);
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

static int webserver_redirect_status(int status) {
    return status == 301 ||
        status == 302 ||
        status == 303 ||
        status == 307 ||
        status == 308;
}

static Value webserver_eval_redirect(AstExpr *expr) {
    if (expr->as.call.args.count != 2 && expr->as.call.args.count != 3) {
        webserver_raise("webserver.redirect expects two or three arguments");
        return value_null();
    }

    Value request = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(request);
        return value_null();
    }
    if (request.kind != VALUE_RECORD) {
        value_free(request);
        webserver_raise("webserver.redirect expects a request record");
        return value_null();
    }
    RecordField *id_field = record_find(&request, "id");
    int id = 0;
    if (!id_field || !webserver_integer(*id_field->value, &id) || id <= 0) {
        value_free(request);
        webserver_raise("webserver.redirect request id must be a positive integer");
        return value_null();
    }

    Value location = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(request);
        value_free(location);
        return value_null();
    }
    if (location.kind != VALUE_STRING) {
        value_free(request);
        value_free(location);
        webserver_raise("webserver.redirect location must be a string");
        return value_null();
    }
    if (!location.as.string[0] ||
        strchr(location.as.string, '\r') ||
        strchr(location.as.string, '\n')) {
        value_free(request);
        value_free(location);
        webserver_raise("webserver.redirect location is invalid");
        return value_null();
    }

    int status = 303;
    if (expr->as.call.args.count == 3) {
        Value status_value = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(request);
            value_free(location);
            value_free(status_value);
            return value_null();
        }
        if (!webserver_integer(status_value, &status) ||
            !webserver_redirect_status(status)) {
            value_free(request);
            value_free(location);
            value_free(status_value);
            webserver_raise("webserver.redirect status must be 301, 302, 303, 307, or 308");
            return value_null();
        }
        value_free(status_value);
    }

    Value headers = value_record(NULL, 0);
    record_set(&headers, "location", value_string(location.as.string));

    Value response = value_record(NULL, 0);
    record_set(&response, "id", value_number((double)id));
    record_set(&response, "status", value_number((double)status));
    record_set(&response, "headers", headers);
    record_set(&response, "body", value_string(""));

    value_free(request);
    value_free(location);
    return response;
}

static Value webserver_eval_call(AstExpr *expr) {
    if (strcmp(expr->as.call.name, "listen") == 0) {
        return webserver_eval_listen(expr);
    }
    if (strcmp(expr->as.call.name, "close") == 0) {
        return webserver_eval_close(expr);
    }
    if (strcmp(expr->as.call.name, "redirect") == 0) {
        return webserver_eval_redirect(expr);
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
            fields[column].value = cell_alloc();
            if (!fields[column].value) {
                abort();
            }
            *fields[column].value = sqlite_column_value(statement, column);
            completed_fields++;
            if (error_generation != before_error) {
                for (int i = 0; i < completed_fields; i++) {
                    free(fields[i].name);
                    cell_release(fields[i].value);
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
    fields[0].value = cell_alloc();
    fields[1].name = copy_string("rows_affected");
    fields[1].value = cell_alloc();
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
            fields[column].value = cell_alloc();
            if (!fields[column].value) {
                abort();
            }
            *fields[column].value = pg_result_value(result, row, column);
            completed_fields++;
            if (error_generation != before_error) {
                for (int i = 0; i < completed_fields; i++) {
                    free(fields[i].name);
                    cell_release(fields[i].value);
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
    fields[0].value = cell_alloc();
    fields[1].name = copy_string("rows_affected");
    fields[1].value = cell_alloc();
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
                    cell_release(fields[i].value);
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

/* XML module (xml_design.md, WP-XML-1). Translation-unit include so it can use
 * the static Value API above; guarded internally by HAVE_LIBXML2. */
#include "modules/xml.c"

/* ===== Cryptography (docs/crypto_design.md) ===============================
 * Phase 1 (base64/hex encoding, random_bytes, constant-time bytes_equal) is
 * plain C and always available. Phases 2/4 (hashing, HMAC, AES-GCM, Ed25519)
 * bind OpenSSL libcrypto behind HAVE_LIBCRYPTO and raise a clean runtime error
 * when it is absent. All inputs/outputs are binary-safe gBASIC strings: read an
 * argument as `arg.as.string` with `string_length(arg.as.string)` bytes; build
 * a result with `value_string_n(buf, len)`. Decoders return NULL on malformed
 * input and the dispatch turns that into `unknown`. */

static const char CRYPTO_B64_STD[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char CRYPTO_B64_URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static Value crypto_base64_encode(const unsigned char *in, size_t n, int url, int pad) {
    const char *alpha = url ? CRYPTO_B64_URL : CRYPTO_B64_STD;
    size_t out_cap = ((n + 2) / 3) * 4 + 1;
    char *out = malloc(out_cap);
    if (!out) {
        abort();
    }
    size_t o = 0;
    size_t i = 0;
    while (i + 3 <= n) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8) | in[i + 2];
        out[o++] = alpha[(v >> 18) & 63];
        out[o++] = alpha[(v >> 12) & 63];
        out[o++] = alpha[(v >> 6) & 63];
        out[o++] = alpha[v & 63];
        i += 3;
    }
    size_t rem = n - i;
    if (rem == 1) {
        unsigned v = (unsigned)in[i] << 16;
        out[o++] = alpha[(v >> 18) & 63];
        out[o++] = alpha[(v >> 12) & 63];
        if (pad) {
            out[o++] = '=';
            out[o++] = '=';
        }
    } else if (rem == 2) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8);
        out[o++] = alpha[(v >> 18) & 63];
        out[o++] = alpha[(v >> 12) & 63];
        out[o++] = alpha[(v >> 6) & 63];
        if (pad) {
            out[o++] = '=';
        }
    }
    Value result = value_string_n(out, o);
    free(out);
    return result;
}

static int crypto_b64_val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return (int)(c - 'A');
    if (c >= 'a' && c <= 'z') return (int)(c - 'a') + 26;
    if (c >= '0' && c <= '9') return (int)(c - '0') + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

/* Decodes std or url base64 (padding optional). Returns malloc'd bytes and sets
 * *out_len; returns NULL on any invalid character or truncated quad. */
static unsigned char *crypto_base64_decode(const unsigned char *in, size_t n, size_t *out_len) {
    unsigned char *out = malloc(n / 4 * 3 + 4);
    if (!out) {
        abort();
    }
    size_t o = 0;
    int quad[4];
    int qn = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = in[i];
        if (c == '=') {
            break;
        }
        int v = crypto_b64_val(c);
        if (v < 0) {
            free(out);
            return NULL;
        }
        quad[qn++] = v;
        if (qn == 4) {
            out[o++] = (unsigned char)((quad[0] << 2) | (quad[1] >> 4));
            out[o++] = (unsigned char)(((quad[1] & 15) << 4) | (quad[2] >> 2));
            out[o++] = (unsigned char)(((quad[2] & 3) << 6) | quad[3]);
            qn = 0;
        }
    }
    if (qn == 1) {
        free(out);
        return NULL;
    }
    if (qn == 2) {
        out[o++] = (unsigned char)((quad[0] << 2) | (quad[1] >> 4));
    } else if (qn == 3) {
        out[o++] = (unsigned char)((quad[0] << 2) | (quad[1] >> 4));
        out[o++] = (unsigned char)(((quad[1] & 15) << 4) | (quad[2] >> 2));
    }
    *out_len = o;
    return out;
}

static Value crypto_hex_encode(const unsigned char *in, size_t n) {
    static const char hexd[] = "0123456789abcdef";
    char *out = malloc(n * 2 + 1);
    if (!out) {
        abort();
    }
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = hexd[in[i] >> 4];
        out[i * 2 + 1] = hexd[in[i] & 15];
    }
    Value result = value_string_n(out, n * 2);
    free(out);
    return result;
}

static int crypto_hex_val(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

/* Returns malloc'd bytes and sets *out_len; NULL on odd length or non-hex. */
static unsigned char *crypto_hex_decode(const unsigned char *in, size_t n, size_t *out_len) {
    if (n % 2 != 0) {
        return NULL;
    }
    unsigned char *out = malloc(n / 2 + 1);
    if (!out) {
        abort();
    }
    for (size_t i = 0; i < n; i += 2) {
        int hi = crypto_hex_val(in[i]);
        int lo = crypto_hex_val(in[i + 1]);
        if (hi < 0 || lo < 0) {
            free(out);
            return NULL;
        }
        out[i / 2] = (unsigned char)((hi << 4) | lo);
    }
    *out_len = n / 2;
    return out;
}

/* Constant-time byte comparison: always scans max(la,lb) so timing does not leak
 * the match position; unequal lengths still compare false. */
static int crypto_bytes_equal(const unsigned char *a, size_t la,
                              const unsigned char *b, size_t lb) {
    size_t m = la > lb ? la : lb;
    unsigned char diff = (unsigned char)(la ^ lb);
    for (size_t i = 0; i < m; i++) {
        unsigned char av = i < la ? a[i] : 0;
        unsigned char bv = i < lb ? b[i] : 0;
        diff |= (unsigned char)(av ^ bv);
    }
    return diff == 0;
}

/* Evaluate a call's single string argument into *out (caller value_free's it).
 * Raises and returns 0 on wrong arity or non-string. */
static int crypto_one_string(AstExpr *expr, const char *fname, Value *out) {
    char m[128];
    if (expr->as.call.args.count != 1) {
        snprintf(m, sizeof(m), "%s expects one argument", fname);
        runtime_error_raise(m, 1003, "invalid function call");
        return 0;
    }
    Value s = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(s);
        return 0;
    }
    if (s.kind != VALUE_STRING) {
        value_free(s);
        snprintf(m, sizeof(m), "%s expects a string", fname);
        runtime_error_raise(m, 1003, "invalid argument type");
        return 0;
    }
    *out = s;
    return 1;
}

/* Evaluate a call's two string arguments into *a and *b (caller frees both). */
static int crypto_two_strings(AstExpr *expr, const char *fname, Value *a, Value *b) {
    char m[128];
    if (expr->as.call.args.count != 2) {
        snprintf(m, sizeof(m), "%s expects two arguments", fname);
        runtime_error_raise(m, 1003, "invalid function call");
        return 0;
    }
    Value av = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(av);
        return 0;
    }
    Value bv = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(av);
        value_free(bv);
        return 0;
    }
    if (av.kind != VALUE_STRING || bv.kind != VALUE_STRING) {
        value_free(av);
        value_free(bv);
        snprintf(m, sizeof(m), "%s expects strings", fname);
        runtime_error_raise(m, 1003, "invalid argument type");
        return 0;
    }
    *a = av;
    *b = bv;
    return 1;
}

/* Evaluate exactly n string arguments into out[0..n-1] (caller frees each). */
static int crypto_n_strings(AstExpr *expr, const char *fname, int n, Value *out) {
    char m[128];
    if ((int)expr->as.call.args.count != n) {
        snprintf(m, sizeof(m), "%s expects %d arguments", fname, n);
        runtime_error_raise(m, 1003, "invalid function call");
        return 0;
    }
    for (int i = 0; i < n; i++) {
        Value v = eval_expr(expr->as.call.args.items[i]);
        if (error_action_pending()) {
            value_free(v);
            for (int j = 0; j < i; j++) value_free(out[j]);
            return 0;
        }
        if (v.kind != VALUE_STRING) {
            value_free(v);
            for (int j = 0; j < i; j++) value_free(out[j]);
            snprintf(m, sizeof(m), "%s expects strings", fname);
            runtime_error_raise(m, 1003, "invalid argument type");
            return 0;
        }
        out[i] = v;
    }
    return 1;
}

/* Bitwise ops (docs/bitwise_design.md): 32-bit unsigned model — the only integer
 * width exact in a double. Operands must be integers in [0, 2^32); shift/rotate
 * counts integers in [0, 31]; anything else raises (no silent truncation). */
#define BITWISE_MODULUS 4294967296.0 /* 2^32 */

static int bitwise_operand(double d, uint32_t *out) {
    if (!isfinite(d) || d != floor(d) || d < 0 || d >= BITWISE_MODULUS) {
        return 0;
    }
    *out = (uint32_t)d;
    return 1;
}

static int bitwise_count(double d, unsigned *out) {
    if (!isfinite(d) || d != floor(d) || d < 0 || d > 31) {
        return 0;
    }
    *out = (unsigned)d;
    return 1;
}

/* Evaluate exactly n numeric arguments into out[0..n-1]. */
static int bitwise_eval_args(AstExpr *expr, const char *fname, int n, double *out) {
    char m[128];
    if ((int)expr->as.call.args.count != n) {
        snprintf(m, sizeof(m), "%s expects %d argument%s", fname, n, n == 1 ? "" : "s");
        runtime_error_raise(m, 1003, "invalid function call");
        return 0;
    }
    for (int i = 0; i < n; i++) {
        Value v = eval_expr(expr->as.call.args.items[i]);
        if (error_action_pending()) {
            value_free(v);
            return 0;
        }
        if (v.kind != VALUE_NUMBER) {
            value_free(v);
            snprintf(m, sizeof(m), "%s expects numbers", fname);
            runtime_error_raise(m, 1003, "invalid argument type");
            return 0;
        }
        out[i] = v.as.number;
        value_free(v);
    }
    return 1;
}

#if HAVE_LIBCRYPTO
static Value crypto_digest(const EVP_MD *md, const unsigned char *in, size_t n) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int outlen = 0;
    if (EVP_Digest(in, n, out, &outlen, md, NULL) != 1) {
        return value_unknown();
    }
    return value_string_n((char *)out, outlen);
}

static Value crypto_hmac(const EVP_MD *md, const unsigned char *key, size_t kn,
                         const unsigned char *msg, size_t mn) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int outlen = 0;
    if (HMAC(md, key, (int)kn, msg, mn, out, &outlen) == NULL) {
        return value_unknown();
    }
    return value_string_n((char *)out, outlen);
}

/* AES-GCM. key 16/24/32 bytes selects AES-128/192/256; nonce 12 bytes; 16-byte
 * tag. encrypt returns malloc'd ciphertext||tag; decrypt verifies the tag and
 * returns malloc'd plaintext, or NULL (bad params / auth failure). */
static const EVP_CIPHER *crypto_aes_gcm_cipher(size_t keylen) {
    if (keylen == 16) return EVP_aes_128_gcm();
    if (keylen == 24) return EVP_aes_192_gcm();
    if (keylen == 32) return EVP_aes_256_gcm();
    return NULL;
}

static unsigned char *crypto_aes_gcm_encrypt(const unsigned char *key, size_t keylen,
                                             const unsigned char *nonce, size_t noncelen,
                                             const unsigned char *pt, size_t ptlen,
                                             const unsigned char *aad, size_t aadlen,
                                             size_t *out_len) {
    const EVP_CIPHER *cipher = crypto_aes_gcm_cipher(keylen);
    if (!cipher || noncelen != 12) return NULL;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return NULL;
    unsigned char *out = malloc(ptlen + 16);
    if (!out) {
        EVP_CIPHER_CTX_free(ctx);
        abort();
    }
    int ok = 1, len = 0;
    size_t o = 0;
    if (EVP_EncryptInit_ex(ctx, cipher, NULL, key, nonce) != 1) ok = 0;
    if (ok && aadlen > 0 && EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)aadlen) != 1) ok = 0;
    if (ok && EVP_EncryptUpdate(ctx, out, &len, pt, (int)ptlen) != 1) ok = 0;
    if (ok) o = (size_t)len;
    if (ok && EVP_EncryptFinal_ex(ctx, out + o, &len) != 1) ok = 0;
    if (ok) o += (size_t)len;
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, out + o) != 1) ok = 0;
    if (ok) o += 16;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        free(out);
        return NULL;
    }
    *out_len = o;
    return out;
}

static unsigned char *crypto_aes_gcm_decrypt(const unsigned char *key, size_t keylen,
                                             const unsigned char *nonce, size_t noncelen,
                                             const unsigned char *blob, size_t bloblen,
                                             const unsigned char *aad, size_t aadlen,
                                             size_t *out_len) {
    const EVP_CIPHER *cipher = crypto_aes_gcm_cipher(keylen);
    if (!cipher || noncelen != 12 || bloblen < 16) return NULL;
    size_t ctlen = bloblen - 16;
    unsigned char tag[16];
    memcpy(tag, blob + ctlen, 16);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return NULL;
    unsigned char *out = malloc(ctlen + 1);
    if (!out) {
        EVP_CIPHER_CTX_free(ctx);
        abort();
    }
    int ok = 1, len = 0;
    size_t o = 0;
    if (EVP_DecryptInit_ex(ctx, cipher, NULL, key, nonce) != 1) ok = 0;
    if (ok && aadlen > 0 && EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)aadlen) != 1) ok = 0;
    if (ok && EVP_DecryptUpdate(ctx, out, &len, blob, (int)ctlen) != 1) ok = 0;
    if (ok) o = (size_t)len;
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag) != 1) ok = 0;
    int fin = ok ? EVP_DecryptFinal_ex(ctx, out + o, &len) : 0;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok || fin != 1) {
        free(out);
        return NULL;
    }
    o += (size_t)len;
    *out_len = o;
    return out;
}

/* Ed25519: private key is the 32-byte raw seed, public 32 bytes, signature 64. */
static int crypto_ed25519_keypair(unsigned char *pub32, unsigned char *priv32) {
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (!pctx) return 0;
    int ok = 1;
    if (EVP_PKEY_keygen_init(pctx) != 1) ok = 0;
    if (ok && EVP_PKEY_keygen(pctx, &pkey) != 1) ok = 0;
    size_t publ = 32, privl = 32;
    if (ok && EVP_PKEY_get_raw_public_key(pkey, pub32, &publ) != 1) ok = 0;
    if (ok && EVP_PKEY_get_raw_private_key(pkey, priv32, &privl) != 1) ok = 0;
    if (ok && (publ != 32 || privl != 32)) ok = 0;
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(pctx);
    return ok;
}

static unsigned char *crypto_ed25519_sign(const unsigned char *priv, size_t privlen,
                                          const unsigned char *msg, size_t msglen,
                                          size_t *out_len) {
    if (privlen != 32) return NULL;
    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, priv, 32);
    if (!pkey) return NULL;
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    unsigned char *sig = malloc(64);
    if (!mctx || !sig) {
        free(sig);
        EVP_MD_CTX_free(mctx);
        EVP_PKEY_free(pkey);
        abort();
    }
    int ok = 1;
    size_t siglen = 64;
    if (EVP_DigestSignInit(mctx, NULL, NULL, NULL, pkey) != 1) ok = 0;
    if (ok && EVP_DigestSign(mctx, sig, &siglen, msg, msglen) != 1) ok = 0;
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_free(pkey);
    if (!ok || siglen != 64) {
        free(sig);
        return NULL;
    }
    *out_len = siglen;
    return sig;
}

static int crypto_ed25519_verify(const unsigned char *pub, size_t publen,
                                 const unsigned char *msg, size_t msglen,
                                 const unsigned char *sig, size_t siglen) {
    if (publen != 32 || siglen != 64) return 0;
    EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, pub, 32);
    if (!pkey) return 0;
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) {
        EVP_PKEY_free(pkey);
        return 0;
    }
    int rc = 0;
    if (EVP_DigestVerifyInit(mctx, NULL, NULL, NULL, pkey) == 1) {
        rc = EVP_DigestVerify(mctx, sig, siglen, msg, msglen) == 1 ? 1 : 0;
    }
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_free(pkey);
    return rc;
}
#endif

#if HAVE_GIR
/* ---------------------------------------------------------------------------
 * gi.* — GObject-Introspection bridge (docs: PLAN.md "Phase GI"). Raw generic
 * FFI over libgirepository-2.0: create objects by type name, get/set properties,
 * call methods, and connect gBASIC functions to signals. Idiomatic property/
 * method syntax is deferred; everything here rides the eval_call dispatch path.
 * ------------------------------------------------------------------------- */

static GMainLoop *gi_main_loop = NULL;

static GIRepository *gi_repo(void) {
    static GIRepository *repo = NULL;
    if (!repo) {
        repo = gi_repository_dup_default();
    }
    return repo;
}

static Value gi_raise(const char *message) {
    runtime_error_raise(message, 6001, "gi");
    return value_null();
}

static Value gi_raisef(const char *fmt, const char *a) {
    char message[512];
    snprintf(message, sizeof(message), fmt, a);
    runtime_error_raise(message, 6001, "gi");
    return value_null();
}

/* Ensure `obj` is owned (floating refs sunk) and return a new gBASIC Value that
 * references its ONE canonical wrapper. `have_ref` means the caller holds a
 * reference that should be consumed here (transfer-full / freshly constructed);
 * otherwise we take our own (transfer-none). Canonicalized via qdata so the same
 * GObject always maps to the same wrapper — identity and refcounts stay correct. */
static Value gi_canonical_wrap(GObject *obj, gboolean have_ref) {
    if (!obj) {
        return value_null();
    }
    GObjectValue *existing = g_object_get_qdata(obj, gi_wrapper_quark());
    if (existing) {
        /* The canonical wrapper already owns a hard ref; release any incoming
         * ownership so we do not leak (a wrapped object is never floating). */
        if (have_ref) {
            g_object_unref(obj);
        }
        existing->ref_count++;
        return value_gobject(existing);
    }
    if (!have_ref) {
        g_object_ref(obj);
    }
    if (g_object_is_floating(obj)) {
        g_object_ref_sink(obj);   /* floating(1) -> owned(1), no count change */
    }
    GObjectValue *handle = calloc(1, sizeof(*handle));
    if (!handle) {
        abort();
    }
    handle->obj = obj;
    handle->ref_count = 1;
    handle->closed = 0;
    g_object_set_qdata(obj, gi_wrapper_quark(), handle);
    /* Learn if the toolkit finalizes this object out-of-band (e.g. a widget when
     * its window is destroyed) so a later release can't touch a dangling pointer. */
    g_object_weak_ref(obj, gi_wrapper_weak_notify, handle);
    return value_gobject(handle);
}

/* GValue -> gBASIC Value. Returns 1 on success; 0 (no raise) if the GType is not
 * one v1 supports — the caller raises with context. */
static int gi_value_from_gvalue(const GValue *gv, Value *out) {
    GType fund = G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(gv));
    switch (fund) {
    case G_TYPE_BOOLEAN: *out = value_bool(g_value_get_boolean(gv)); return 1;
    case G_TYPE_CHAR:    *out = value_number(g_value_get_schar(gv)); return 1;
    case G_TYPE_UCHAR:   *out = value_number(g_value_get_uchar(gv)); return 1;
    case G_TYPE_INT:     *out = value_number(g_value_get_int(gv)); return 1;
    case G_TYPE_UINT:    *out = value_number(g_value_get_uint(gv)); return 1;
    case G_TYPE_LONG:    *out = value_number((double)g_value_get_long(gv)); return 1;
    case G_TYPE_ULONG:   *out = value_number((double)g_value_get_ulong(gv)); return 1;
    case G_TYPE_INT64:   *out = value_number((double)g_value_get_int64(gv)); return 1;
    case G_TYPE_UINT64:  *out = value_number((double)g_value_get_uint64(gv)); return 1;
    case G_TYPE_FLOAT:   *out = value_number(g_value_get_float(gv)); return 1;
    case G_TYPE_DOUBLE:  *out = value_number(g_value_get_double(gv)); return 1;
    case G_TYPE_ENUM:    *out = value_number(g_value_get_enum(gv)); return 1;
    case G_TYPE_FLAGS:   *out = value_number(g_value_get_flags(gv)); return 1;
    case G_TYPE_STRING: {
        const char *s = g_value_get_string(gv);
        *out = s ? value_string(s) : value_null();
        return 1;
    }
    case G_TYPE_OBJECT: {
        GObject *o = g_value_get_object(gv);   /* borrowed: transfer-none */
        *out = gi_canonical_wrap(o, FALSE);
        return 1;
    }
    case G_TYPE_NONE: *out = value_null(); return 1;
    default: return 0;
    }
}

/* gBASIC Value -> GValue initialised to `target`. Returns 1 on success; on failure
 * the GValue is left uninitialised and the caller raises. */
static int gi_value_to_gvalue(Value v, GType target, GValue *out) {
    GType fund = G_TYPE_FUNDAMENTAL(target);
    memset(out, 0, sizeof(*out));
    g_value_init(out, target);
    switch (fund) {
    case G_TYPE_BOOLEAN:
        g_value_set_boolean(out, v.kind == VALUE_BOOL ? v.as.boolean
                                                      : value_number_or_zero(v) != 0.0);
        return 1;
    case G_TYPE_CHAR:   g_value_set_schar(out, (gint8)value_number_or_zero(v)); return 1;
    case G_TYPE_UCHAR:  g_value_set_uchar(out, (guchar)value_number_or_zero(v)); return 1;
    case G_TYPE_INT:    g_value_set_int(out, (gint)value_number_or_zero(v)); return 1;
    case G_TYPE_UINT:   g_value_set_uint(out, (guint)value_number_or_zero(v)); return 1;
    case G_TYPE_LONG:   g_value_set_long(out, (glong)value_number_or_zero(v)); return 1;
    case G_TYPE_ULONG:  g_value_set_ulong(out, (gulong)value_number_or_zero(v)); return 1;
    case G_TYPE_INT64:  g_value_set_int64(out, (gint64)value_number_or_zero(v)); return 1;
    case G_TYPE_UINT64: g_value_set_uint64(out, (guint64)value_number_or_zero(v)); return 1;
    case G_TYPE_FLOAT:  g_value_set_float(out, (gfloat)value_number_or_zero(v)); return 1;
    case G_TYPE_DOUBLE: g_value_set_double(out, value_number_or_zero(v)); return 1;
    case G_TYPE_ENUM:   g_value_set_enum(out, (gint)value_number_or_zero(v)); return 1;
    case G_TYPE_FLAGS:  g_value_set_flags(out, (guint)value_number_or_zero(v)); return 1;
    case G_TYPE_STRING:
        if (v.kind == VALUE_STRING) { g_value_set_string(out, v.as.string); return 1; }
        if (v.kind == VALUE_NULL)   { g_value_set_string(out, NULL); return 1; }
        g_value_unset(out); return 0;
    case G_TYPE_OBJECT:
        if (v.kind == VALUE_GOBJECT) { g_value_set_object(out, v.as.gobject->obj); return 1; }
        if (v.kind == VALUE_NULL)    { g_value_set_object(out, NULL); return 1; }
        g_value_unset(out); return 0;
    default:
        g_value_unset(out); return 0;
    }
}

/* Split "Ns.Rest" at the first dot. Returns 0 if there is no dot. */
static int gi_split_first(const char *qualified, char *ns, size_t ns_size,
                          const char **rest) {
    const char *dot = strchr(qualified, '.');
    if (!dot || dot == qualified) {
        return 0;
    }
    size_t n = (size_t)(dot - qualified);
    if (n >= ns_size) {
        n = ns_size - 1;
    }
    memcpy(ns, qualified, n);
    ns[n] = '\0';
    *rest = dot + 1;
    return 1;
}

/* Resolve "Namespace.TypeName" to a GType, or G_TYPE_INVALID if unknown. */
static GType gi_lookup_gtype(const char *qualified) {
    char ns[128];
    const char *name = NULL;
    if (!gi_split_first(qualified, ns, sizeof(ns), &name)) {
        return G_TYPE_INVALID;
    }
    GIBaseInfo *info = gi_repository_find_by_name(gi_repo(), ns, name);
    if (!info) {
        return G_TYPE_INVALID;
    }
    GType t = G_TYPE_INVALID;
    if (GI_IS_REGISTERED_TYPE_INFO(info)) {
        t = gi_registered_type_info_get_g_type((GIRegisteredTypeInfo *)info);
    }
    gi_base_info_unref(info);
    return t;
}

/* Marshal one native return/argument GIArgument (by type tag) into a gBASIC Value.
 * `transfer` governs ownership of strings/objects handed back to us. */
static int gi_value_from_giarg(GITypeInfo *ti, GITransfer transfer,
                               GIArgument *arg, Value *out) {
    switch (gi_type_info_get_tag(ti)) {
    case GI_TYPE_TAG_VOID:    *out = value_null(); return 1;
    case GI_TYPE_TAG_BOOLEAN: *out = value_bool(arg->v_boolean); return 1;
    case GI_TYPE_TAG_INT8:    *out = value_number(arg->v_int8); return 1;
    case GI_TYPE_TAG_UINT8:   *out = value_number(arg->v_uint8); return 1;
    case GI_TYPE_TAG_INT16:   *out = value_number(arg->v_int16); return 1;
    case GI_TYPE_TAG_UINT16:  *out = value_number(arg->v_uint16); return 1;
    case GI_TYPE_TAG_INT32:   *out = value_number(arg->v_int32); return 1;
    case GI_TYPE_TAG_UINT32:  *out = value_number(arg->v_uint32); return 1;
    case GI_TYPE_TAG_INT64:   *out = value_number((double)arg->v_int64); return 1;
    case GI_TYPE_TAG_UINT64:  *out = value_number((double)arg->v_uint64); return 1;
    case GI_TYPE_TAG_FLOAT:   *out = value_number(arg->v_float); return 1;
    case GI_TYPE_TAG_DOUBLE:  *out = value_number(arg->v_double); return 1;
    case GI_TYPE_TAG_GTYPE:   *out = value_number((double)arg->v_size); return 1;
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
        *out = arg->v_string ? value_string(arg->v_string) : value_null();
        if (transfer == GI_TRANSFER_EVERYTHING && arg->v_string) {
            g_free(arg->v_string);
        }
        return 1;
    case GI_TYPE_TAG_INTERFACE: {
        GIBaseInfo *iface = gi_type_info_get_interface(ti);
        int ok = 0;
        if (iface) {
            if (GI_IS_OBJECT_INFO(iface) || GI_IS_INTERFACE_INFO(iface)) {
                *out = gi_canonical_wrap((GObject *)arg->v_pointer,
                                         transfer == GI_TRANSFER_EVERYTHING);
                ok = 1;
            } else if (GI_IS_ENUM_INFO(iface)) {
                *out = value_number(arg->v_int32);
                ok = 1;
            }
            gi_base_info_unref(iface);
        }
        return ok;
    }
    default:
        return 0;
    }
}

/* gBASIC Value -> one native IN GIArgument (by type tag). Strings are borrowed for
 * the duration of the call (no copy), so the source Value must outlive the invoke. */
static int gi_giarg_from_value(GITypeInfo *ti, Value v, GIArgument *arg) {
    memset(arg, 0, sizeof(*arg));
    switch (gi_type_info_get_tag(ti)) {
    case GI_TYPE_TAG_BOOLEAN:
        arg->v_boolean = (v.kind == VALUE_BOOL ? v.as.boolean : value_number_or_zero(v) != 0.0);
        return 1;
    case GI_TYPE_TAG_INT8:   arg->v_int8 = (gint8)value_number_or_zero(v); return 1;
    case GI_TYPE_TAG_UINT8:  arg->v_uint8 = (guint8)value_number_or_zero(v); return 1;
    case GI_TYPE_TAG_INT16:  arg->v_int16 = (gint16)value_number_or_zero(v); return 1;
    case GI_TYPE_TAG_UINT16: arg->v_uint16 = (guint16)value_number_or_zero(v); return 1;
    case GI_TYPE_TAG_INT32:  arg->v_int32 = (gint32)value_number_or_zero(v); return 1;
    case GI_TYPE_TAG_UINT32: arg->v_uint32 = (guint32)value_number_or_zero(v); return 1;
    case GI_TYPE_TAG_INT64:  arg->v_int64 = (gint64)value_number_or_zero(v); return 1;
    case GI_TYPE_TAG_UINT64: arg->v_uint64 = (guint64)value_number_or_zero(v); return 1;
    case GI_TYPE_TAG_FLOAT:  arg->v_float = (gfloat)value_number_or_zero(v); return 1;
    case GI_TYPE_TAG_DOUBLE: arg->v_double = value_number_or_zero(v); return 1;
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
        if (v.kind == VALUE_STRING) { arg->v_string = v.as.string; return 1; }
        if (v.kind == VALUE_NULL)   { arg->v_string = NULL; return 1; }
        return 0;
    case GI_TYPE_TAG_INTERFACE: {
        GIBaseInfo *iface = gi_type_info_get_interface(ti);
        int ok = 0;
        if (iface) {
            if (GI_IS_OBJECT_INFO(iface) || GI_IS_INTERFACE_INFO(iface)) {
                if (v.kind == VALUE_GOBJECT) { arg->v_pointer = v.as.gobject->obj; ok = 1; }
                else if (v.kind == VALUE_NULL) { arg->v_pointer = NULL; ok = 1; }
            } else if (GI_IS_ENUM_INFO(iface)) {
                arg->v_int32 = (gint32)value_number_or_zero(v);
                ok = 1;
            }
            gi_base_info_unref(iface);
        }
        return ok;
    }
    default:
        return 0;
    }
}

/* Pull the live GObject out of a gBASIC value, raising on a non-object or a
 * disposed handle. */
static int gi_object_arg(Value v, const char *ctx, GObject **out) {
    if (v.kind != VALUE_GOBJECT) {
        gi_raisef("%s expects a gobject", ctx);
        return 0;
    }
    if (v.as.gobject->closed || !v.as.gobject->obj) {
        gi_raisef("%s: gobject has been disposed", ctx);
        return 0;
    }
    *out = v.as.gobject->obj;
    return 1;
}

/* --- signal dispatch ---------------------------------------------------- */

typedef struct {
    char *name;      /* gBASIC function name */
    char *library;   /* owning library, or NULL */
} GiClosureData;

static void gi_closure_finalize(gpointer data, GClosure *closure) {
    (void)closure;
    GiClosureData *d = data;
    if (!d) {
        return;
    }
    free(d->name);
    free(d->library);
    free(d);
}

/* The one generic marshaller for every connected signal. Converts the GValue
 * parameter array into gBASIC values, re-enters the interpreter to run the user's
 * function, and — crucially — snapshots/restores the global error/line/stop state
 * so a raising handler cannot corrupt the outer program. An unhandled handler
 * error is surfaced (it stays in the diagnostics sink) and quits the main loop. */
static void gi_signal_marshal(GClosure *closure, GValue *return_gvalue,
                              guint n_param_values, const GValue *param_values,
                              gpointer invocation_hint, gpointer marshal_data) {
    (void)return_gvalue;
    (void)invocation_hint;
    (void)marshal_data;
    GiClosureData *d = closure->data;
    FunctionDef *def = function_resolve(d->library, d->name);
    if (!def || !def->stmt) {
        return;   /* handler function disappeared; nothing to call */
    }

    size_t want = def->stmt->as.function.params.count;
    Value *args = want ? malloc(sizeof(Value) * want) : NULL;
    if (want && !args) {
        abort();
    }
    for (size_t i = 0; i < want; i++) {
        if (i < n_param_values) {
            if (!gi_value_from_gvalue(&param_values[i], &args[i])) {
                args[i] = value_null();
            }
        } else {
            args[i] = value_null();
        }
    }

    int saved_stopped = runtime_stopped;
    ErrorMode saved_mode = error_mode;
    int saved_line = current_line;
    int saved_column = current_column;
    int before_gen = error_generation;

    Value result = invoke_function(def->stmt, args, want, NULL);
    value_free(result);

    if (runtime_stopped || error_generation != before_gen) {
        /* The handler raised. In STOP mode the raise already pushed a diagnostic to
         * the sink (surfaced at program end); here we contain it: restore the outer
         * error/stop state so the program that pumped the loop continues cleanly,
         * and end any running main loop. */
        runtime_stopped = saved_stopped;
        error_mode = saved_mode;
        error_clear_state();
        if (gi_main_loop) {
            g_main_loop_quit(gi_main_loop);
        }
    }
    current_line = saved_line;
    current_column = saved_column;
}

/* --- individual builtins ------------------------------------------------ */

static Value gi_do_require(AstExpr *expr) {
    size_t argc = expr->as.call.args.count;
    if (argc < 1 || argc > 2) {
        return gi_raise("gi.require expects a namespace and optional version");
    }
    Value ns = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) { value_free(ns); return value_null(); }
    Value ver = argc == 2 ? eval_expr(expr->as.call.args.items[1]) : value_null();
    if (error_action_pending()) { value_free(ns); value_free(ver); return value_null(); }
    if (ns.kind != VALUE_STRING || (argc == 2 && ver.kind != VALUE_STRING)) {
        value_free(ns); value_free(ver);
        return gi_raise("gi.require expects string arguments");
    }

    /* GTK3 (gui module) and GTK4 (loaded here) must not share a process. */
    const char *version = argc == 2 ? ver.as.string : NULL;
    if (strcmp(ns.as.string, "Gtk") == 0 && version && version[0] == '4' && gui_library_loaded) {
        value_free(ns); value_free(ver);
        return gi_raise("GTK 3 (gui module) and GTK 4 (gi) cannot be used in the same process");
    }

    GError *error = NULL;
    GITypelib *typelib = gi_repository_require(gi_repo(), ns.as.string, version,
                                               0, &error);
    if (!typelib) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s-%s",
                 ns.as.string, version ? version : "(any)");
        if (error) {
            g_error_free(error);
        }
        value_free(ns); value_free(ver);
        return gi_raisef("gi.require: could not load namespace %s", detail);
    }
    if (strcmp(ns.as.string, "Gtk") == 0 && version && version[0] == '4') {
        gi_gtk4_active = 1;
    }
    value_free(ns); value_free(ver);
    return value_null();
}

static Value gi_do_new(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        return gi_raise("gi.new expects one argument");
    }
    Value type_name = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) { value_free(type_name); return value_null(); }
    if (type_name.kind != VALUE_STRING) {
        value_free(type_name);
        return gi_raise("gi.new expects a type name string");
    }
    GType gtype = gi_lookup_gtype(type_name.as.string);
    if (gtype == G_TYPE_INVALID) {
        Value r = gi_raisef("gi.new: unknown type: %s", type_name.as.string);
        value_free(type_name);
        return r;
    }
    if (!G_TYPE_IS_OBJECT(gtype)) {
        Value r = gi_raisef("gi.new: not an instantiable object type: %s", type_name.as.string);
        value_free(type_name);
        return r;
    }
    value_free(type_name);
    GObject *obj = g_object_new(gtype, NULL);
    if (!obj) {
        return gi_raise("gi.new: object construction failed");
    }
    return gi_canonical_wrap(obj, TRUE);   /* adopt the fresh (possibly floating) ref */
}

static Value gi_do_get(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        return gi_raise("gi.get expects an object and a property name");
    }
    Value ov = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) { value_free(ov); return value_null(); }
    Value pv = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) { value_free(ov); value_free(pv); return value_null(); }
    GObject *obj = NULL;
    if (!gi_object_arg(ov, "gi.get", &obj)) { value_free(ov); value_free(pv); return value_null(); }
    if (pv.kind != VALUE_STRING) {
        value_free(ov); value_free(pv);
        return gi_raise("gi.get expects a property name string");
    }
    GParamSpec *ps = g_object_class_find_property(G_OBJECT_GET_CLASS(obj), pv.as.string);
    if (!ps) {
        Value r = gi_raisef("gi.get: unknown property: %s", pv.as.string);
        value_free(ov); value_free(pv);
        return r;
    }
    GValue gv = G_VALUE_INIT;
    g_value_init(&gv, G_PARAM_SPEC_VALUE_TYPE(ps));
    g_object_get_property(obj, pv.as.string, &gv);
    Value out;
    int ok = gi_value_from_gvalue(&gv, &out);
    g_value_unset(&gv);
    if (!ok) {
        Value r = gi_raisef("gi.get: unsupported property type for: %s", pv.as.string);
        value_free(ov); value_free(pv);
        return r;
    }
    value_free(ov); value_free(pv);
    return out;
}

static Value gi_do_set(AstExpr *expr) {
    if (expr->as.call.args.count != 3) {
        return gi_raise("gi.set expects an object, a property name, and a value");
    }
    Value ov = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) { value_free(ov); return value_null(); }
    Value pv = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) { value_free(ov); value_free(pv); return value_null(); }
    Value val = eval_expr(expr->as.call.args.items[2]);
    if (error_action_pending()) { value_free(ov); value_free(pv); value_free(val); return value_null(); }
    GObject *obj = NULL;
    if (!gi_object_arg(ov, "gi.set", &obj)) { value_free(ov); value_free(pv); value_free(val); return value_null(); }
    if (pv.kind != VALUE_STRING) {
        value_free(ov); value_free(pv); value_free(val);
        return gi_raise("gi.set expects a property name string");
    }
    GParamSpec *ps = g_object_class_find_property(G_OBJECT_GET_CLASS(obj), pv.as.string);
    if (!ps) {
        Value r = gi_raisef("gi.set: unknown property: %s", pv.as.string);
        value_free(ov); value_free(pv); value_free(val);
        return r;
    }
    GValue gv;
    if (!gi_value_to_gvalue(val, G_PARAM_SPEC_VALUE_TYPE(ps), &gv)) {
        Value r = gi_raisef("gi.set: value not convertible for property: %s", pv.as.string);
        value_free(ov); value_free(pv); value_free(val);
        return r;
    }
    g_object_set_property(obj, pv.as.string, &gv);
    g_value_unset(&gv);
    value_free(ov); value_free(pv); value_free(val);
    return value_null();
}

static Value gi_do_call(AstExpr *expr) {
    size_t argc = expr->as.call.args.count;
    if (argc < 2) {
        return gi_raise("gi.call expects an object and a method name");
    }
    Value ov = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) { value_free(ov); return value_null(); }
    Value mv = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) { value_free(ov); value_free(mv); return value_null(); }
    GObject *obj = NULL;
    if (!gi_object_arg(ov, "gi.call", &obj)) { value_free(ov); value_free(mv); return value_null(); }
    if (mv.kind != VALUE_STRING) {
        value_free(ov); value_free(mv);
        return gi_raise("gi.call expects a method name string");
    }

    GIBaseInfo *base = gi_repository_find_by_gtype(gi_repo(), G_OBJECT_TYPE(obj));
    if (!base || !GI_IS_OBJECT_INFO(base)) {
        if (base) gi_base_info_unref(base);
        Value r = gi_raisef("gi.call: type is not introspectable: %s", G_OBJECT_TYPE_NAME(obj));
        value_free(ov); value_free(mv);
        return r;
    }
    /* Walk the class hierarchy: find_method_using_interfaces searches a class's own
     * methods and the interfaces it implements, but NOT its ancestors, so an
     * inherited method (e.g. Gio.Application.register on a Gtk.Application) is only
     * found by climbing parents. Each level still searches that level's interfaces. */
    GIFunctionInfo *finfo = NULL;
    GIObjectInfo *cur = (GIObjectInfo *)base;
    int cur_owned = 0;   /* `base` is released separately below; parents are owned */
    while (cur) {
        finfo = gi_object_info_find_method_using_interfaces(cur, mv.as.string, NULL);
        GIObjectInfo *parent = gi_object_info_get_parent(cur);
        if (cur_owned) {
            gi_base_info_unref(cur);
        }
        if (finfo) {
            if (parent) gi_base_info_unref(parent);
            break;
        }
        cur = parent;
        cur_owned = 1;
    }
    gi_base_info_unref(base);
    if (!finfo) {
        Value r = gi_raisef("gi.call: unknown method: %s", mv.as.string);
        value_free(ov); value_free(mv);
        return r;
    }

    GICallableInfo *cinfo = (GICallableInfo *)finfo;
    gboolean is_method = (gi_function_info_get_flags(finfo) & GI_FUNCTION_IS_METHOD) != 0;
    unsigned int ndecl = gi_callable_info_get_n_args(cinfo);
    size_t provided = argc - 2;
    if (provided != ndecl) {
        char detail[160];
        snprintf(detail, sizeof(detail), "%s expects %u argument(s)", mv.as.string, ndecl);
        gi_base_info_unref(finfo);
        Value r = gi_raisef("gi.call: %s", detail);
        value_free(ov); value_free(mv);
        return r;
    }

    /* Evaluate the method arguments (kept live until after invoke; strings are
     * borrowed by GIArgument). */
    Value *argvals = provided ? calloc(provided, sizeof(Value)) : NULL;
    GIArgument *in_args = calloc(ndecl + 1, sizeof(GIArgument));
    if ((provided && !argvals) || !in_args) {
        abort();
    }
    size_t n_in = 0;
    int failed = 0;
    if (is_method) {
        in_args[n_in++].v_pointer = obj;
    }
    for (unsigned int i = 0; i < ndecl && !failed; i++) {
        argvals[i] = eval_expr(expr->as.call.args.items[2 + i]);
        if (error_action_pending()) { failed = 1; break; }
        GIArgInfo *ai = gi_callable_info_get_arg(cinfo, i);
        if (gi_arg_info_get_direction(ai) != GI_DIRECTION_IN) {
            gi_base_info_unref(ai);
            gi_raisef("gi.call: %s has an unsupported out/inout argument", mv.as.string);
            failed = 1;
            break;
        }
        GITypeInfo *ti = gi_arg_info_get_type_info(ai);
        int ok = gi_giarg_from_value(ti, argvals[i], &in_args[n_in]);
        gi_base_info_unref(ti);
        gi_base_info_unref(ai);
        if (!ok) {
            gi_raisef("gi.call: unsupported argument type for method: %s", mv.as.string);
            failed = 1;
            break;
        }
        n_in++;
    }

    Value out = value_null();
    if (!failed) {
        GIArgument retval;
        memset(&retval, 0, sizeof(retval));
        GError *ierr = NULL;
        if (!gi_function_info_invoke(finfo, in_args, n_in, NULL, 0, &retval, &ierr)) {
            char detail[256];
            snprintf(detail, sizeof(detail), "%s: %s", mv.as.string,
                     ierr && ierr->message ? ierr->message : "call failed");
            if (ierr) g_error_free(ierr);
            gi_raisef("gi.call: %s", detail);
        } else {
            GITypeInfo *rti = gi_callable_info_get_return_type(cinfo);
            GITransfer rtransfer = gi_callable_info_get_caller_owns(cinfo);
            if (!gi_value_from_giarg(rti, rtransfer, &retval, &out)) {
                out = value_null();   /* unsupported return type -> nothing */
            }
            gi_base_info_unref(rti);
        }
    }

    for (size_t i = 0; i < provided; i++) {
        value_free(argvals[i]);
    }
    free(argvals);
    free(in_args);
    gi_base_info_unref(finfo);
    value_free(ov); value_free(mv);
    return out;
}

static Value gi_do_connect(AstExpr *expr) {
    if (expr->as.call.args.count != 3) {
        return gi_raise("gi.connect expects an object, a signal name, and a function");
    }
    Value ov = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) { value_free(ov); return value_null(); }
    Value sv = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) { value_free(ov); value_free(sv); return value_null(); }
    Value fv = eval_expr(expr->as.call.args.items[2]);
    if (error_action_pending()) { value_free(ov); value_free(sv); value_free(fv); return value_null(); }
    GObject *obj = NULL;
    if (!gi_object_arg(ov, "gi.connect", &obj)) { value_free(ov); value_free(sv); value_free(fv); return value_null(); }
    if (sv.kind != VALUE_STRING) {
        value_free(ov); value_free(sv); value_free(fv);
        return gi_raise("gi.connect expects a signal name string");
    }
    if (fv.kind != VALUE_FUNCTION) {
        value_free(ov); value_free(sv); value_free(fv);
        return gi_raise("gi.connect expects a function as the handler");
    }

    GiClosureData *data = calloc(1, sizeof(*data));
    if (!data) {
        abort();
    }
    data->name = copy_string(fv.as.function.name);
    data->library = fv.as.function.library ? copy_string(fv.as.function.library) : NULL;

    GClosure *closure = g_closure_new_simple(sizeof(GClosure), data);
    g_closure_set_marshal(closure, gi_signal_marshal);
    g_closure_add_finalize_notifier(closure, data, gi_closure_finalize);

    gulong id = g_signal_connect_closure(obj, sv.as.string, closure, FALSE);
    value_free(ov); value_free(sv); value_free(fv);
    if (id == 0) {
        return gi_raise("gi.connect: no such signal on this object");
    }
    return value_number((double)id);
}

static Value gi_do_disconnect(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        return gi_raise("gi.disconnect expects an object and a handler id");
    }
    Value ov = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) { value_free(ov); return value_null(); }
    Value idv = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) { value_free(ov); value_free(idv); return value_null(); }
    GObject *obj = NULL;
    if (!gi_object_arg(ov, "gi.disconnect", &obj)) { value_free(ov); value_free(idv); return value_null(); }
    if (idv.kind != VALUE_NUMBER) {
        value_free(ov); value_free(idv);
        return gi_raise("gi.disconnect expects a numeric handler id");
    }
    gulong id = (gulong)idv.as.number;
    if (id != 0 && g_signal_handler_is_connected(obj, id)) {
        g_signal_handler_disconnect(obj, id);
    }
    value_free(ov); value_free(idv);
    return value_null();
}

static Value gi_do_enum(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        return gi_raise("gi.enum expects one argument");
    }
    Value qv = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) { value_free(qv); return value_null(); }
    if (qv.kind != VALUE_STRING) {
        value_free(qv);
        return gi_raise("gi.enum expects a qualified enum member string");
    }
    /* "Ns.EnumName.MEMBER" -> ns / enum name / member */
    const char *q = qv.as.string;
    const char *first = strchr(q, '.');
    const char *last = strrchr(q, '.');
    if (!first || first == last) {
        Value r = gi_raisef("gi.enum: expected Namespace.Enum.MEMBER, got: %s", q);
        value_free(qv);
        return r;
    }
    char ns[128];
    char ename[128];
    size_t nsn = (size_t)(first - q);
    size_t enn = (size_t)(last - (first + 1));
    if (nsn >= sizeof(ns)) nsn = sizeof(ns) - 1;
    if (enn >= sizeof(ename)) enn = sizeof(ename) - 1;
    memcpy(ns, q, nsn); ns[nsn] = '\0';
    memcpy(ename, first + 1, enn); ename[enn] = '\0';
    const char *member = last + 1;

    GIBaseInfo *info = gi_repository_find_by_name(gi_repo(), ns, ename);
    if (!info || !GI_IS_ENUM_INFO(info)) {
        if (info) gi_base_info_unref(info);
        Value r = gi_raisef("gi.enum: unknown enum: %s", q);
        value_free(qv);
        return r;
    }
    char *want = g_ascii_strup(member, -1);
    int found = 0;
    int64_t result = 0;
    unsigned int n = gi_enum_info_get_n_values((GIEnumInfo *)info);
    for (unsigned int i = 0; i < n && !found; i++) {
        GIValueInfo *vi = gi_enum_info_get_value((GIEnumInfo *)info, i);
        const char *vname = gi_base_info_get_name((GIBaseInfo *)vi);
        char *up = g_ascii_strup(vname, -1);
        if (strcmp(up, want) == 0) {
            result = gi_value_info_get_value(vi);
            found = 1;
        }
        g_free(up);
        gi_base_info_unref(vi);
    }
    g_free(want);
    gi_base_info_unref(info);
    value_free(qv);
    if (!found) {
        return gi_raisef("gi.enum: unknown member: %s", q);
    }
    return value_number((double)result);
}

static Value gi_do_is_a(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        return gi_raise("gi.is_a expects an object and a type name");
    }
    Value ov = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) { value_free(ov); return value_null(); }
    Value tv = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) { value_free(ov); value_free(tv); return value_null(); }
    GObject *obj = NULL;
    if (!gi_object_arg(ov, "gi.is_a", &obj)) { value_free(ov); value_free(tv); return value_null(); }
    if (tv.kind != VALUE_STRING) {
        value_free(ov); value_free(tv);
        return gi_raise("gi.is_a expects a type name string");
    }
    GType target = gi_lookup_gtype(tv.as.string);
    if (target == G_TYPE_INVALID) {
        Value r = gi_raisef("gi.is_a: unknown type: %s", tv.as.string);
        value_free(ov); value_free(tv);
        return r;
    }
    int yes = g_type_is_a(G_OBJECT_TYPE(obj), target);
    value_free(ov); value_free(tv);
    return value_bool(yes);
}

static Value gi_do_type_name(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        return gi_raise("gi.type_name expects one argument");
    }
    Value ov = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) { value_free(ov); return value_null(); }
    GObject *obj = NULL;
    if (!gi_object_arg(ov, "gi.type_name", &obj)) { value_free(ov); return value_null(); }
    Value r = value_string(G_OBJECT_TYPE_NAME(obj));
    value_free(ov);
    return r;
}

static Value gi_do_main(AstExpr *expr) {
    if (expr->as.call.args.count != 0) {
        return gi_raise("gi.main expects no arguments");
    }
    if (!gi_main_loop) {
        gi_main_loop = g_main_loop_new(NULL, FALSE);
    }
    g_main_loop_run(gi_main_loop);
    return value_null();
}

static Value gi_do_quit(AstExpr *expr) {
    if (expr->as.call.args.count != 0) {
        return gi_raise("gi.quit expects no arguments");
    }
    if (gi_main_loop) {
        g_main_loop_quit(gi_main_loop);
    }
    return value_null();
}

static Value gi_eval_call(AstExpr *expr) {
    const char *name = expr->as.call.name;
    if (strcmp(name, "require") == 0)     return gi_do_require(expr);
    if (strcmp(name, "new") == 0)         return gi_do_new(expr);
    if (strcmp(name, "get") == 0)         return gi_do_get(expr);
    if (strcmp(name, "set") == 0)         return gi_do_set(expr);
    if (strcmp(name, "call") == 0)        return gi_do_call(expr);
    if (strcmp(name, "connect") == 0)     return gi_do_connect(expr);
    if (strcmp(name, "disconnect") == 0)  return gi_do_disconnect(expr);
    if (strcmp(name, "enum") == 0)        return gi_do_enum(expr);
    if (strcmp(name, "is_a") == 0)        return gi_do_is_a(expr);
    if (strcmp(name, "type_name") == 0)   return gi_do_type_name(expr);
    if (strcmp(name, "main") == 0)        return gi_do_main(expr);
    if (strcmp(name, "quit") == 0)        return gi_do_quit(expr);
    return gi_raisef("invalid function call: gi.%s", name);
}
#endif /* HAVE_GIR */

static Value eval_call(AstExpr *expr) {
    if (expr->as.call.library) {
        /* §5 disambiguation: X.y(args) where X is a variable bound to a record
         * whose field y holds a function value is a METHOD call (this = that
         * record), not a qualified library call. Checked first, so a record
         * variable wins over a library of the same name. */
        Symbol *receiver_symbol = env_find(expr->as.call.library);
        if (receiver_symbol && receiver_symbol->value.kind == VALUE_RECORD) {
            RecordField *method_field =
                record_find(&receiver_symbol->value, expr->as.call.name);
            if (method_field && method_field->value->kind == VALUE_FUNCTION) {
                FunctionDef *method =
                    function_resolve(method_field->value->as.function.library,
                                     method_field->value->as.function.name);
                if (!method) {
                    char message[256];
                    snprintf(message, sizeof(message),
                             "method value references unknown function: %s",
                             method_field->value->as.function.name);
                    runtime_error_raise(message, 1003, "invalid function call");
                    return value_null();
                }
                return eval_user_function_with_receiver(expr, method,
                                                        &receiver_symbol->value);
            }
        }

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

        if (strcmp(expr->as.call.library, "xml") == 0) {
            if (!xml_library_loaded) {
                runtime_error_raise("library not loaded: xml", 5001, "xml");
                return value_null();
            }
#if HAVE_LIBXML2
            return xml_eval_call(expr);
#else
            runtime_error_raise("XML support is not available in this build",
                                5001,
                                "xml");
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

        if (strcmp(expr->as.call.library, "gi") == 0) {
            if (!gi_library_loaded) {
                runtime_error_raise("library not loaded: gi", 6001, "gi");
                return value_null();
            }
#if HAVE_GIR
            return gi_eval_call(expr);
#else
            runtime_error_raise("gobject-introspection support is unavailable; "
                                "install libgirepository-2.0-dev (GLib >= 2.80) and rebuild",
                                6001,
                                "gi");
            return value_null();
#endif
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

    if (strcmp(expr->as.call.name, "sleep") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("sleep expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value seconds = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(seconds);
            return value_null();
        }
        if (seconds.kind != VALUE_NUMBER) {
            value_free(seconds);
            runtime_error_raise("sleep expects a number", 1003, "invalid function call");
            return value_null();
        }
        double secs = seconds.as.number;
        value_free(seconds);
        if (isnan(secs) || secs < 0.0) {
            runtime_error_raise("sleep expects a non-negative number", 1003, "invalid function call");
            return value_null();
        }
        /* Fractional seconds via nanosleep. Resume across signal interruption so
         * the full requested interval always elapses — the monitor loop
         * (edgar_design.md §7) depends on the elapsed >= requested guarantee. */
        struct timespec req;
        req.tv_sec = (time_t)secs;
        req.tv_nsec = (long)((secs - (double)req.tv_sec) * 1e9);
        if (req.tv_nsec < 0) {
            req.tv_nsec = 0;
        } else if (req.tv_nsec > 999999999L) {
            req.tv_nsec = 999999999L;
        }
        struct timespec rem;
        while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
            req = rem;
        }
        return value_number(secs);
    }

    if (strcmp(expr->as.call.name, "password_hash") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("password_hash expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value password = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(password);
            return value_null();
        }
        if (password.kind != VALUE_STRING) {
            value_free(password);
            runtime_error_raise("password_hash expects a string", 1003, "invalid function call");
            return value_null();
        }
#if HAVE_LIBXCRYPT
        char *salt = crypt_gensalt_ra(NULL, 0, NULL, 0);
        if (!salt) {
            value_free(password);
            runtime_error_raise("password_hash could not generate a salt", 1003, "password_hash");
            return value_null();
        }

        void *data = NULL;
        int data_size = 0;
        char *hash = crypt_ra(password.as.string, salt, &data, &data_size);
        free(salt);
        value_free(password);
        if (!hash) {
            free(data);
            runtime_error_raise("password_hash could not hash the password", 1003, "password_hash");
            return value_null();
        }

        Value result = value_string(hash);
        free(data);
        return result;
#else
        value_free(password);
        runtime_error_raise("Password hashing support is not available in this build",
                            1003,
                            "password_hash");
        return value_null();
#endif
    }

    if (strcmp(expr->as.call.name, "password_verify") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("password_verify expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value password = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(password);
            return value_null();
        }
        Value hash = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(password);
            value_free(hash);
            return value_null();
        }
        if (password.kind != VALUE_STRING) {
            value_free(password);
            value_free(hash);
            runtime_error_raise("password_verify expects a string password", 1003, "invalid function call");
            return value_null();
        }
        if (hash.kind != VALUE_STRING) {
            value_free(password);
            value_free(hash);
            runtime_error_raise("password_verify expects a string hash", 1003, "invalid function call");
            return value_null();
        }
#if HAVE_LIBXCRYPT
        void *data = NULL;
        int data_size = 0;
        char *computed = crypt_ra(password.as.string, hash.as.string, &data, &data_size);
        int verified = computed && constant_time_string_equal(computed, hash.as.string);
        free(data);
        value_free(password);
        value_free(hash);
        return value_bool(verified);
#else
        value_free(password);
        value_free(hash);
        runtime_error_raise("Password hashing support is not available in this build",
                            1003,
                            "password_verify");
        return value_null();
#endif
    }

    if (strcmp(expr->as.call.name, "now") == 0) {
        if (expr->as.call.args.count != 0) {
            runtime_error_raise("now expects no arguments", 1003, "invalid function call");
            return value_null();
        }
        time_t raw = time(NULL);
        if (raw == (time_t)-1) {
            runtime_error_raise("could not read the current time", 1003, "clock");
            return value_null();
        }
        struct tm local;
        if (!localtime_r(&raw, &local)) {
            runtime_error_raise("could not convert the current time", 1003, "clock");
            return value_null();
        }
        DateTime dt = {0};
        dt.year = local.tm_year + 1900;
        dt.month = local.tm_mon + 1;
        dt.day = local.tm_mday;
        dt.hour = local.tm_hour;
        dt.minute = local.tm_min;
        dt.second = local.tm_sec;
        dt.time_only = 0;
        dt.precision = PREC_SECOND;
        return value_datetime(dt);
    }

    if (strcmp(expr->as.call.name, "epoch") == 0) {
        if (expr->as.call.args.count != 0) {
            runtime_error_raise("epoch expects no arguments", 1003, "invalid function call");
            return value_null();
        }
        time_t raw = time(NULL);
        if (raw == (time_t)-1) {
            runtime_error_raise("could not read the current time", 1003, "clock");
            return value_null();
        }
        return value_number((double)raw);
    }

    if (strcmp(expr->as.call.name, "from_epoch") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("from_epoch expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value sv = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(sv);
            return value_null();
        }
        if (sv.kind != VALUE_NUMBER) {
            value_free(sv);
            runtime_error_raise("from_epoch expects a number", 1003, "invalid argument type");
            return value_null();
        }
        double sd = sv.as.number;
        value_free(sv);
        if (!isfinite(sd) || sd != floor(sd)) {
            runtime_error_raise("from_epoch expects an integer number of seconds", 1003, "invalid argument");
            return value_null();
        }
        time_t raw = (time_t)sd;
        struct tm local;
        if (!localtime_r(&raw, &local)) {
            runtime_error_raise("could not convert the given epoch time", 1003, "clock");
            return value_null();
        }
        DateTime dt = {0};
        dt.year = local.tm_year + 1900;
        dt.month = local.tm_mon + 1;
        dt.day = local.tm_mday;
        dt.hour = local.tm_hour;
        dt.minute = local.tm_min;
        dt.second = local.tm_sec;
        dt.time_only = 0;
        dt.precision = PREC_SECOND;
        return value_datetime(dt);
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

    /* ----- Bitwise (32-bit unsigned) ----- */
    if (strcmp(expr->as.call.name, "band") == 0 ||
        strcmp(expr->as.call.name, "bor") == 0 ||
        strcmp(expr->as.call.name, "bxor") == 0) {
        const char *name = expr->as.call.name;
        double a[2];
        if (!bitwise_eval_args(expr, name, 2, a)) return value_null();
        uint32_t x, y;
        if (!bitwise_operand(a[0], &x) || !bitwise_operand(a[1], &y)) {
            char m[128];
            snprintf(m, sizeof(m), "%s: operands must be integers in [0, 2^32)", name);
            runtime_error_raise(m, 1003, "invalid argument");
            return value_null();
        }
        uint32_t r = name[1] == 'a' ? (x & y) : (name[1] == 'o' ? (x | y) : (x ^ y));
        return value_number((double)r);
    }
    if (strcmp(expr->as.call.name, "bnot") == 0) {
        double a[1];
        if (!bitwise_eval_args(expr, "bnot", 1, a)) return value_null();
        uint32_t x;
        if (!bitwise_operand(a[0], &x)) {
            runtime_error_raise("bnot: operand must be an integer in [0, 2^32)", 1003, "invalid argument");
            return value_null();
        }
        return value_number((double)(uint32_t)(~x));
    }
    if (strcmp(expr->as.call.name, "shl") == 0 ||
        strcmp(expr->as.call.name, "shr") == 0 ||
        strcmp(expr->as.call.name, "rotl") == 0 ||
        strcmp(expr->as.call.name, "rotr") == 0) {
        const char *name = expr->as.call.name;
        double a[2];
        if (!bitwise_eval_args(expr, name, 2, a)) return value_null();
        uint32_t x;
        unsigned n;
        if (!bitwise_operand(a[0], &x)) {
            char m[128];
            snprintf(m, sizeof(m), "%s: value must be an integer in [0, 2^32)", name);
            runtime_error_raise(m, 1003, "invalid argument");
            return value_null();
        }
        if (!bitwise_count(a[1], &n)) {
            char m[128];
            snprintf(m, sizeof(m), "%s: shift/rotate count must be an integer in [0, 31]", name);
            runtime_error_raise(m, 1003, "invalid argument");
            return value_null();
        }
        uint32_t r;
        if (strcmp(name, "shl") == 0) {
            r = (uint32_t)(x << n);
        } else if (strcmp(name, "shr") == 0) {
            r = x >> n;
        } else if (strcmp(name, "rotl") == 0) {
            r = n == 0 ? x : (uint32_t)((x << n) | (x >> (32 - n)));
        } else {
            r = n == 0 ? x : (uint32_t)((x >> n) | (x << (32 - n)));
        }
        return value_number((double)r);
    }

    /* ----- Cryptography: encoding (always available) ----- */
    if (strcmp(expr->as.call.name, "base64_encode") == 0) {
        Value s;
        if (!crypto_one_string(expr, "base64_encode", &s)) return value_null();
        Value r = crypto_base64_encode((const unsigned char *)s.as.string, string_length(s.as.string), 0, 1);
        value_free(s);
        return r;
    }
    if (strcmp(expr->as.call.name, "base64url_encode") == 0) {
        Value s;
        if (!crypto_one_string(expr, "base64url_encode", &s)) return value_null();
        Value r = crypto_base64_encode((const unsigned char *)s.as.string, string_length(s.as.string), 1, 0);
        value_free(s);
        return r;
    }
    if (strcmp(expr->as.call.name, "base64_decode") == 0 ||
        strcmp(expr->as.call.name, "base64url_decode") == 0) {
        Value s;
        if (!crypto_one_string(expr, expr->as.call.name, &s)) return value_null();
        size_t out_len = 0;
        unsigned char *dec = crypto_base64_decode((const unsigned char *)s.as.string,
                                                  string_length(s.as.string), &out_len);
        value_free(s);
        if (!dec) {
            return value_unknown();
        }
        Value r = value_string_n((char *)dec, out_len);
        free(dec);
        return r;
    }
    if (strcmp(expr->as.call.name, "hex_encode") == 0) {
        Value s;
        if (!crypto_one_string(expr, "hex_encode", &s)) return value_null();
        Value r = crypto_hex_encode((const unsigned char *)s.as.string, string_length(s.as.string));
        value_free(s);
        return r;
    }
    if (strcmp(expr->as.call.name, "hex_decode") == 0) {
        Value s;
        if (!crypto_one_string(expr, "hex_decode", &s)) return value_null();
        size_t out_len = 0;
        unsigned char *dec = crypto_hex_decode((const unsigned char *)s.as.string,
                                               string_length(s.as.string), &out_len);
        value_free(s);
        if (!dec) {
            return value_unknown();
        }
        Value r = value_string_n((char *)dec, out_len);
        free(dec);
        return r;
    }
    if (strcmp(expr->as.call.name, "bytes_equal") == 0) {
        Value a, b;
        if (!crypto_two_strings(expr, "bytes_equal", &a, &b)) return value_null();
        int eq = crypto_bytes_equal((const unsigned char *)a.as.string, string_length(a.as.string),
                                    (const unsigned char *)b.as.string, string_length(b.as.string));
        value_free(a);
        value_free(b);
        return value_bool(eq);
    }
    if (strcmp(expr->as.call.name, "random_bytes") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("random_bytes expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value nv = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(nv);
            return value_null();
        }
        if (nv.kind != VALUE_NUMBER) {
            value_free(nv);
            runtime_error_raise("random_bytes expects a number", 1003, "invalid argument type");
            return value_null();
        }
        double nd = nv.as.number;
        value_free(nv);
        if (!isfinite(nd) || nd != floor(nd)) {
            runtime_error_raise("random_bytes length must be an integer", 1003, "invalid argument");
            return value_null();
        }
        if (nd < 1 || nd > SECURE_TOKEN_MAX_LENGTH) {
            runtime_error_raise("random_bytes length must be between 1 and 4096", 1003, "invalid argument");
            return value_null();
        }
        size_t n = (size_t)nd;
        unsigned char *buf = malloc(n);
        if (!buf) {
            abort();
        }
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            free(buf);
            runtime_error_raise("random_bytes could not read secure random bytes", 1003, "random");
            return value_null();
        }
        size_t off = 0;
        while (off < n) {
            ssize_t c = read(fd, buf + off, n - off);
            if (c < 0 && errno == EINTR) {
                continue;
            }
            if (c <= 0) {
                close(fd);
                free(buf);
                runtime_error_raise("random_bytes could not read secure random bytes", 1003, "random");
                return value_null();
            }
            off += (size_t)c;
        }
        close(fd);
        Value r = value_string_n((char *)buf, n);
        free(buf);
        return r;
    }

    /* ----- Cryptography: hashing + HMAC (libcrypto) ----- */
    if (strcmp(expr->as.call.name, "sha256") == 0 ||
        strcmp(expr->as.call.name, "sha512") == 0 ||
        strcmp(expr->as.call.name, "sha1") == 0 ||
        strcmp(expr->as.call.name, "md5") == 0) {
        Value s;
        if (!crypto_one_string(expr, expr->as.call.name, &s)) return value_null();
#if HAVE_LIBCRYPTO
        const EVP_MD *md = EVP_sha256();
        if (strcmp(expr->as.call.name, "sha512") == 0) md = EVP_sha512();
        else if (strcmp(expr->as.call.name, "sha1") == 0) md = EVP_sha1();
        else if (strcmp(expr->as.call.name, "md5") == 0) md = EVP_md5();
        Value r = crypto_digest(md, (const unsigned char *)s.as.string, string_length(s.as.string));
        value_free(s);
        return r;
#else
        value_free(s);
        runtime_error_raise("hashing requires OpenSSL (libcrypto); rebuild with it installed",
                            1003, "unsupported");
        return value_null();
#endif
    }
    if (strcmp(expr->as.call.name, "hmac_sha256") == 0 ||
        strcmp(expr->as.call.name, "hmac_sha512") == 0) {
        Value key, msg;
        if (!crypto_two_strings(expr, expr->as.call.name, &key, &msg)) return value_null();
#if HAVE_LIBCRYPTO
        const EVP_MD *md = strcmp(expr->as.call.name, "hmac_sha512") == 0 ? EVP_sha512() : EVP_sha256();
        Value r = crypto_hmac(md, (const unsigned char *)key.as.string, string_length(key.as.string),
                              (const unsigned char *)msg.as.string, string_length(msg.as.string));
        value_free(key);
        value_free(msg);
        return r;
#else
        value_free(key);
        value_free(msg);
        runtime_error_raise("HMAC requires OpenSSL (libcrypto); rebuild with it installed",
                            1003, "unsupported");
        return value_null();
#endif
    }

    /* ----- Cryptography: AES-GCM + Ed25519 (libcrypto) ----- */
    if (strcmp(expr->as.call.name, "aes_gcm_encrypt") == 0 ||
        strcmp(expr->as.call.name, "aes_gcm_decrypt") == 0) {
        int encrypt = strcmp(expr->as.call.name, "aes_gcm_encrypt") == 0;
        Value a[4];
        if (!crypto_n_strings(expr, expr->as.call.name, 4, a)) return value_null();
#if HAVE_LIBCRYPTO
        const unsigned char *key = (const unsigned char *)a[0].as.string;
        const unsigned char *nonce = (const unsigned char *)a[1].as.string;
        const unsigned char *data = (const unsigned char *)a[2].as.string;
        const unsigned char *aad = (const unsigned char *)a[3].as.string;
        size_t out_len = 0;
        unsigned char *out;
        if (encrypt) {
            out = crypto_aes_gcm_encrypt(key, string_length(a[0].as.string),
                                         nonce, string_length(a[1].as.string),
                                         data, string_length(a[2].as.string),
                                         aad, string_length(a[3].as.string), &out_len);
        } else {
            out = crypto_aes_gcm_decrypt(key, string_length(a[0].as.string),
                                         nonce, string_length(a[1].as.string),
                                         data, string_length(a[2].as.string),
                                         aad, string_length(a[3].as.string), &out_len);
        }
        for (int i = 0; i < 4; i++) value_free(a[i]);
        if (!out) {
            return value_unknown();
        }
        Value r = value_string_n((char *)out, out_len);
        free(out);
        return r;
#else
        for (int i = 0; i < 4; i++) value_free(a[i]);
        runtime_error_raise("AES-GCM requires OpenSSL (libcrypto); rebuild with it installed",
                            1003, "unsupported");
        return value_null();
#endif
    }
    if (strcmp(expr->as.call.name, "ed25519_keypair") == 0) {
        if (expr->as.call.args.count != 0) {
            runtime_error_raise("ed25519_keypair expects no arguments", 1003, "invalid function call");
            return value_null();
        }
#if HAVE_LIBCRYPTO
        unsigned char pub[32], priv[32];
        if (!crypto_ed25519_keypair(pub, priv)) {
            return value_unknown();
        }
        RecordField *fields = calloc(2, sizeof(RecordField));
        if (!fields) {
            abort();
        }
        const char *names[] = {"public", "private"};
        for (size_t i = 0; i < 2; i++) {
            fields[i].name = copy_string(names[i]);
            fields[i].value = cell_alloc();
            if (!fields[i].value) {
                abort();
            }
        }
        *fields[0].value = value_string_n((char *)pub, 32);
        *fields[1].value = value_string_n((char *)priv, 32);
        return value_record(fields, 2);
#else
        runtime_error_raise("Ed25519 requires OpenSSL (libcrypto); rebuild with it installed",
                            1003, "unsupported");
        return value_null();
#endif
    }
    if (strcmp(expr->as.call.name, "ed25519_sign") == 0) {
        Value a[2];
        if (!crypto_n_strings(expr, "ed25519_sign", 2, a)) return value_null();
#if HAVE_LIBCRYPTO
        size_t out_len = 0;
        unsigned char *sig = crypto_ed25519_sign((const unsigned char *)a[0].as.string,
                                                 string_length(a[0].as.string),
                                                 (const unsigned char *)a[1].as.string,
                                                 string_length(a[1].as.string), &out_len);
        value_free(a[0]);
        value_free(a[1]);
        if (!sig) {
            return value_unknown();
        }
        Value r = value_string_n((char *)sig, out_len);
        free(sig);
        return r;
#else
        value_free(a[0]);
        value_free(a[1]);
        runtime_error_raise("Ed25519 requires OpenSSL (libcrypto); rebuild with it installed",
                            1003, "unsupported");
        return value_null();
#endif
    }
    if (strcmp(expr->as.call.name, "ed25519_verify") == 0) {
        Value a[3];
        if (!crypto_n_strings(expr, "ed25519_verify", 3, a)) return value_null();
#if HAVE_LIBCRYPTO
        int ok = crypto_ed25519_verify((const unsigned char *)a[0].as.string,
                                       string_length(a[0].as.string),
                                       (const unsigned char *)a[1].as.string,
                                       string_length(a[1].as.string),
                                       (const unsigned char *)a[2].as.string,
                                       string_length(a[2].as.string));
        for (int i = 0; i < 3; i++) value_free(a[i]);
        return value_bool(ok);
#else
        for (int i = 0; i < 3; i++) value_free(a[i]);
        runtime_error_raise("Ed25519 requires OpenSSL (libcrypto); rebuild with it installed",
                            1003, "unsupported");
        return value_null();
#endif
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
        if (value.kind == VALUE_DATETIME) {
            int ok = 0;
            double e = datetime_to_epoch(value.as.datetime, &ok);
            value_free(value);
            if (!ok) {
                runtime_error_raise("number conversion failed: a time-only value has no epoch",
                                    1003, "invalid conversion");
                return value_null();
            }
            return value_number(e);
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

    if (strcmp(expr->as.call.name, "chr") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("chr expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value code_val = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(code_val);
            return value_null();
        }
        if (code_val.kind != VALUE_NUMBER) {
            value_free(code_val);
            runtime_error_raise("chr: argument must be a number", 1003, "invalid argument type");
            return value_null();
        }
        double code_double = code_val.as.number;
        value_free(code_val);
        if (code_double != floor(code_double)) {
            runtime_error_raise("chr: code must be an integer", 1003, "invalid argument");
            return value_null();
        }
        if (code_double < 0 || code_double > 0x10FFFF) {
            runtime_error_raise("chr: codepoint must be between 0 and 0x10FFFF", 1003, "invalid argument");
            return value_null();
        }
        unsigned cp = (unsigned)code_double;
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            runtime_error_raise("chr: surrogate codepoints (0xD800..0xDFFF) are not valid", 1003, "invalid argument");
            return value_null();
        }
        char utf8[4];
        size_t n = utf8_encode_codepoint(cp, utf8);
        return value_string_n(utf8, n);
    }

    if (strcmp(expr->as.call.name, "code") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("code expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(text);
            return value_null();
        }
        if (text.kind != VALUE_STRING) {
            value_free(text);
            runtime_error_raise("code: argument must be a string", 1003, "invalid argument type");
            return value_null();
        }
        if (string_length(text.as.string) == 0) {
            value_free(text);
            runtime_error_raise("code: string must not be empty", 1003, "invalid argument");
            return value_null();
        }
        unsigned cp = 0;
        utf8_decode_first(text.as.string, string_length(text.as.string), &cp);
        value_free(text);
        return value_number((double)cp);
    }

    if (strcmp(expr->as.call.name, "byte_count") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("byte_count expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(text);
            return value_null();
        }
        if (text.kind != VALUE_STRING) {
            value_free(text);
            runtime_error_raise("byte_count: argument must be a string", 1003, "invalid argument type");
            return value_null();
        }
        double count = (double)string_length(text.as.string);
        value_free(text);
        return value_number(count);
    }

    if (strcmp(expr->as.call.name, "byte_at") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("byte_at expects a string and an index", 1003, "invalid function call");
            return value_null();
        }
        Value text = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(text);
            return value_null();
        }
        Value index_val = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(text);
            value_free(index_val);
            return value_null();
        }
        if (text.kind != VALUE_STRING || index_val.kind != VALUE_NUMBER) {
            value_free(text);
            value_free(index_val);
            runtime_error_raise("byte_at: argument must be a string and a number", 1003, "invalid argument type");
            return value_null();
        }
        double index_double = index_val.as.number;
        value_free(index_val);
        if (index_double != floor(index_double)) {
            value_free(text);
            runtime_error_raise("byte_at: index must be an integer", 1003, "invalid argument");
            return value_null();
        }
        size_t length = string_length(text.as.string);
        /* 0-based to match the language's existing mid/index convention. */
        if (index_double < 0 || (size_t)index_double >= length) {
            value_free(text);
            runtime_error_raise("byte_at: index out of range", 1003, "invalid argument");
            return value_null();
        }
        unsigned char byte = (unsigned char)text.as.string[(size_t)index_double];
        value_free(text);
        return value_number((double)byte);
    }

    if (strcmp(expr->as.call.name, "from_bytes") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("from_bytes expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value array = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(array);
            return value_null();
        }
        if (array.kind != VALUE_ARRAY) {
            value_free(array);
            runtime_error_raise("from_bytes: argument must be an array of numbers", 1003, "invalid argument type");
            return value_null();
        }
        size_t count = array.as.array.count;
        char *bytes = malloc(count + 1);
        if (!bytes) {
            abort();
        }
        for (size_t i = 0; i < count; i++) {
            Value item = array.as.array.items[i];
            if (item.kind != VALUE_NUMBER ||
                item.as.number != floor(item.as.number) ||
                item.as.number < 0 || item.as.number > 255) {
                free(bytes);
                value_free(array);
                runtime_error_raise("from_bytes: every element must be a byte value 0..255", 1003, "invalid argument");
                return value_null();
            }
            bytes[i] = (char)(unsigned char)item.as.number;
        }
        bytes[count] = '\0';
        Value result = value_string_n(bytes, count);
        free(bytes);
        value_free(array);
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
            new_fields = calloc(new_count, sizeof(RecordField));
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
                    new_fields[new_index].value = cell_alloc();
                    *new_fields[new_index].value = value_copy(*record.as.record.fields[i].value);
                    /* Preserve the kept field's PBI policy. */
                    new_fields[new_index].policy = record.as.record.fields[i].policy;
                    new_fields[new_index].reset_expr = record.as.record.fields[i].reset_expr;
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

    if (strcmp(expr->as.call.name, "serialize") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("serialize expects one argument", 1003, "actor");
            return value_null();
        }
        Value arg = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(arg);
            return value_null();
        }
        return builtin_serialize_value(arg);
    }

    if (strcmp(expr->as.call.name, "deserialize") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("deserialize expects one argument", 1003, "actor");
            return value_null();
        }
        Value arg = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(arg);
            return value_null();
        }
        return builtin_deserialize_value(arg);
    }

    if (strcmp(expr->as.call.name, "self") == 0) {
        if (expr->as.call.args.count != 0) {
            runtime_error_raise("self expects no arguments", 1003, "actor");
            return value_null();
        }
        return builtin_actor_self();
    }

    if (strcmp(expr->as.call.name, "send") == 0) {
        size_t sc = expr->as.call.args.count;
        if (sc != 2 && sc != 3) {
            runtime_error_raise("send expects an actor handle, a message, and an "
                                "optional strict flag", 1003, "actor");
            return value_null();
        }
        Value handle = eval_expr(expr->as.call.args.items[0]);
        Value message = eval_expr(expr->as.call.args.items[1]);
        /* Optional third argument: a boolean enabling strict-link diagnosis (§6). */
        int strict = 0;
        if (sc == 3) {
            Value flag = eval_expr(expr->as.call.args.items[2]);
            if (error_action_pending()) {
                value_free(handle);
                value_free(message);
                value_free(flag);
                return value_null();
            }
            if (flag.kind != VALUE_BOOL) {
                value_free(handle);
                value_free(message);
                value_free(flag);
                runtime_error_raise("send: the strict flag must be a boolean",
                                    1003, "actor");
                return value_null();
            }
            strict = flag.as.boolean;
            value_free(flag);
        }
        if (error_action_pending()) {
            value_free(handle);
            value_free(message);
            return value_null();
        }
        return builtin_actor_send(handle, message, strict);
    }

    if (strcmp(expr->as.call.name, "receive") == 0) {
        size_t rc = expr->as.call.args.count;
        if (rc == 0) {
            return actor_receive_impl(0, value_null(), 0, 0);
        }
        if (rc == 1) {
            /* One argument is a timeout if it is a duration, else a selector tag. */
            Value a = eval_expr(expr->as.call.args.items[0]);
            if (error_action_pending()) {
                value_free(a);
                return value_null();
            }
            if (a.kind == VALUE_DURATION) {
                long long ms = duration_to_ms(a.as.duration);
                value_free(a);
                return actor_receive_impl(0, value_null(), 1, ms < 0 ? 0 : ms);
            }
            return actor_receive_impl(1, a, 0, 0);
        }
        if (rc == 2) {
            /* Selective receive with a timeout: receive(tag, <duration>). */
            Value tag = eval_expr(expr->as.call.args.items[0]);
            Value timeout = eval_expr(expr->as.call.args.items[1]);
            if (error_action_pending()) {
                value_free(tag);
                value_free(timeout);
                return value_null();
            }
            if (timeout.kind != VALUE_DURATION) {
                value_free(tag);
                value_free(timeout);
                runtime_error_raise("receive: the second argument must be a duration timeout",
                                    1003, "actor");
                return value_null();
            }
            long long ms = duration_to_ms(timeout.as.duration);
            value_free(timeout);
            return actor_receive_impl(1, tag, 1, ms < 0 ? 0 : ms);
        }
        runtime_error_raise("receive expects at most a tag and a timeout",
                            1003, "actor");
        return value_null();
    }

    if (strcmp(expr->as.call.name, "monitor") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("monitor expects an actor handle", 1003, "actor");
            return value_null();
        }
        Value handle = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(handle);
            return value_null();
        }
        return builtin_actor_monitor(handle);
    }

    if (strcmp(expr->as.call.name, "demonitor") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("demonitor expects a monitor reference",
                                1003, "actor");
            return value_null();
        }
        Value ref = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(ref);
            return value_null();
        }
        return builtin_actor_demonitor(ref);
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

    /* Elementary scalar math (statistics_design.md §8 — the numeric foundation
     * the statistics library builds on). One-argument functions over a number. */
    if (strcmp(expr->as.call.name, "sqrt") == 0 ||
        strcmp(expr->as.call.name, "abs") == 0 ||
        strcmp(expr->as.call.name, "exp") == 0 ||
        strcmp(expr->as.call.name, "log") == 0 ||
        strcmp(expr->as.call.name, "log10") == 0 ||
        strcmp(expr->as.call.name, "floor") == 0 ||
        strcmp(expr->as.call.name, "ceil") == 0 ||
        strcmp(expr->as.call.name, "erf") == 0 ||
        strcmp(expr->as.call.name, "erfc") == 0 ||
        strcmp(expr->as.call.name, "lgamma") == 0 ||
        strcmp(expr->as.call.name, "sign") == 0) {
        const char *fn = expr->as.call.name;
        if (expr->as.call.args.count != 1) {
            char message[256];
            snprintf(message, sizeof(message), "%s expects one argument", fn);
            runtime_error_raise(message, 1003, "invalid function call");
            return value_null();
        }
        Value v = eval_expr(expr->as.call.args.items[0]);
        if (v.kind != VALUE_NUMBER) {
            char message[256];
            snprintf(message, sizeof(message), "%s expects a number", fn);
            runtime_error_raise(message, 1003, "invalid function call");
            value_free(v);
            return value_null();
        }
        double x = v.as.number;
        value_free(v);
        double r;
        if (strcmp(fn, "sqrt") == 0) {
            if (x < 0.0) {
                runtime_error_raise("sqrt of a negative number", 1003, "invalid function call");
                return value_null();
            }
            r = sqrt(x);
        } else if (strcmp(fn, "abs") == 0) {
            r = fabs(x);
        } else if (strcmp(fn, "exp") == 0) {
            r = exp(x);
        } else if (strcmp(fn, "log") == 0) {
            if (x <= 0.0) {
                runtime_error_raise("log of a non-positive number", 1003, "invalid function call");
                return value_null();
            }
            r = log(x);
        } else if (strcmp(fn, "log10") == 0) {
            if (x <= 0.0) {
                runtime_error_raise("log10 of a non-positive number", 1003, "invalid function call");
                return value_null();
            }
            r = log10(x);
        } else if (strcmp(fn, "floor") == 0) {
            r = floor(x);
        } else if (strcmp(fn, "ceil") == 0) {
            r = ceil(x);
        } else if (strcmp(fn, "erf") == 0) {
            r = erf(x);
        } else if (strcmp(fn, "erfc") == 0) {
            r = erfc(x);
        } else if (strcmp(fn, "lgamma") == 0) {
            /* log |Gamma(x)|; poles at non-positive integers. */
            if (x <= 0.0 && x == floor(x)) {
                runtime_error_raise("lgamma of a non-positive integer", 1003, "invalid function call");
                return value_null();
            }
            r = lgamma(x);
        } else {
            r = (double)((x > 0.0) - (x < 0.0));
        }
        return value_number(r);
    }

    /* pow(base, exponent) — two-argument scalar math. */
    if (strcmp(expr->as.call.name, "pow") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("pow expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value base = eval_expr(expr->as.call.args.items[0]);
        Value ex = eval_expr(expr->as.call.args.items[1]);
        if (base.kind != VALUE_NUMBER || ex.kind != VALUE_NUMBER) {
            runtime_error_raise("pow expects two numbers", 1003, "invalid function call");
            value_free(base);
            value_free(ex);
            return value_null();
        }
        double r = pow(base.as.number, ex.as.number);
        value_free(base);
        value_free(ex);
        return value_number(r);
    }

    /* seed(n) — set the PRNG seed for reproducible draws; returns the seed. */
    if (strcmp(expr->as.call.name, "seed") == 0) {
        if (expr->as.call.args.count != 1) {
            runtime_error_raise("seed expects one argument", 1003, "invalid function call");
            return value_null();
        }
        Value v = eval_expr(expr->as.call.args.items[0]);
        if (v.kind != VALUE_NUMBER) {
            runtime_error_raise("seed expects a number", 1003, "invalid function call");
            value_free(v);
            return value_null();
        }
        double n = v.as.number;
        value_free(v);
        /* Reinterpret the double's bits so any seed (incl. fractional) maps to a
         * stable 64-bit value; the same number always yields the same stream. */
        uint64_t bits;
        memcpy(&bits, &n, sizeof(bits));
        gbasic_rng_seed(bits);
        return value_number(n);
    }

    /* random() — uniform double in [0, 1). */
    if (strcmp(expr->as.call.name, "random") == 0) {
        if (expr->as.call.args.count != 0) {
            runtime_error_raise("random expects no arguments", 1003, "invalid function call");
            return value_null();
        }
        return value_number(gbasic_rng_double());
    }

    /* random_int(lo, hi) — uniform integer in [lo, hi], both ends inclusive. */
    if (strcmp(expr->as.call.name, "random_int") == 0) {
        if (expr->as.call.args.count != 2) {
            runtime_error_raise("random_int expects two arguments", 1003, "invalid function call");
            return value_null();
        }
        Value lo = eval_expr(expr->as.call.args.items[0]);
        Value hi = eval_expr(expr->as.call.args.items[1]);
        if (lo.kind != VALUE_NUMBER || hi.kind != VALUE_NUMBER) {
            runtime_error_raise("random_int expects two numbers", 1003, "invalid function call");
            value_free(lo);
            value_free(hi);
            return value_null();
        }
        double lod = lo.as.number;
        double hid = hi.as.number;
        value_free(lo);
        value_free(hi);
        if (lod != floor(lod) || hid != floor(hid)) {
            runtime_error_raise("random_int bounds must be whole numbers", 1003, "invalid function call");
            return value_null();
        }
        if (hid < lod) {
            runtime_error_raise("random_int upper bound is below the lower bound", 1003, "invalid function call");
            return value_null();
        }
        uint64_t span = (uint64_t)(hid - lod) + 1;
        double r = lod + (double)gbasic_rng_below(span);
        return value_number(r);
    }

    /* quantile(xs, q) / percentile(xs, p) — a numeric array plus a cut point,
     * linear interpolation (statistics_design.md §8). quantile takes q in
     * [0,1]; percentile takes p in [0,100]. */
    if (strcmp(expr->as.call.name, "quantile") == 0 ||
        strcmp(expr->as.call.name, "percentile") == 0) {
        const char *fn = expr->as.call.name;
        if (expr->as.call.args.count != 2) {
            char message[256];
            snprintf(message, sizeof(message), "%s expects two arguments", fn);
            runtime_error_raise(message, 1003, "invalid function call");
            return value_null();
        }
        Value data = eval_expr(expr->as.call.args.items[0]);
        Value cut = eval_expr(expr->as.call.args.items[1]);
        if (!array_is_numeric(data) || data.as.array.count == 0 ||
            cut.kind != VALUE_NUMBER) {
            char message[256];
            snprintf(message, sizeof(message),
                     "%s expects a non-empty numeric array and a number", fn);
            runtime_error_raise(message, 1003, "invalid function call");
            value_free(data);
            value_free(cut);
            return value_null();
        }
        double q = cut.as.number;
        if (strcmp(fn, "percentile") == 0) {
            q /= 100.0;
        }
        if (q < 0.0 || q > 1.0) {
            char message[256];
            snprintf(message, sizeof(message), "%s cut point is out of range", fn);
            runtime_error_raise(message, 1003, "invalid function call");
            value_free(data);
            value_free(cut);
            return value_null();
        }
        size_t n = data.as.array.count;
        double *sorted = malloc(sizeof(double) * n);
        if (!sorted) {
            abort();
        }
        for (size_t i = 0; i < n; i++) {
            sorted[i] = data.as.array.items[i].as.number;
        }
        qsort(sorted, n, sizeof(double), number_compare);
        double r = quantile_sorted(sorted, n, q);
        free(sorted);
        value_free(data);
        value_free(cut);
        return value_number(r);
    }

    /* correlation(xs, ys) / covariance(xs, ys) — paired numeric arrays of equal
     * length. covariance is the sample form (divide by n-1); correlation is
     * Pearson's r (statistics_design.md §8). */
    if (strcmp(expr->as.call.name, "correlation") == 0 ||
        strcmp(expr->as.call.name, "covariance") == 0) {
        const char *fn = expr->as.call.name;
        if (expr->as.call.args.count != 2) {
            char message[256];
            snprintf(message, sizeof(message), "%s expects two arguments", fn);
            runtime_error_raise(message, 1003, "invalid function call");
            return value_null();
        }
        Value xs = eval_expr(expr->as.call.args.items[0]);
        Value ys = eval_expr(expr->as.call.args.items[1]);
        if (!array_is_numeric(xs) || !array_is_numeric(ys) ||
            xs.as.array.count != ys.as.array.count || xs.as.array.count < 2) {
            char message[256];
            snprintf(message, sizeof(message),
                     "%s expects two numeric arrays of equal length (>= 2)", fn);
            runtime_error_raise(message, 1003, "invalid function call");
            value_free(xs);
            value_free(ys);
            return value_null();
        }
        size_t n = xs.as.array.count;
        double mx = 0.0, my = 0.0;
        for (size_t i = 0; i < n; i++) {
            mx += xs.as.array.items[i].as.number;
            my += ys.as.array.items[i].as.number;
        }
        mx /= (double)n;
        my /= (double)n;
        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (size_t i = 0; i < n; i++) {
            double dx = xs.as.array.items[i].as.number - mx;
            double dy = ys.as.array.items[i].as.number - my;
            sxy += dx * dy;
            sxx += dx * dx;
            syy += dy * dy;
        }
        double r;
        if (strcmp(fn, "covariance") == 0) {
            r = sxy / (double)(n - 1);
        } else {
            if (sxx == 0.0 || syy == 0.0) {
                runtime_error_raise("correlation is undefined for zero-variance data",
                                    1003, "invalid function call");
                value_free(xs);
                value_free(ys);
                return value_null();
            }
            r = sxy / (sqrt(sxx) * sqrt(syy));
        }
        value_free(xs);
        value_free(ys);
        return value_number(r);
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
            size_t hlen = string_length(value.as.string);
            size_t nlen = string_length(target.as.string);
            long off = string_find_bytes(value.as.string, hlen, target.as.string, nlen);
            /* Report the match as a codepoint index, not a byte offset. */
            Value result = off >= 0
                ? value_number((double)string_codepoint_count(value.as.string, (size_t)off))
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
        size_t byte_len = string_length(text.as.string);
        size_t cp_len = string_codepoint_count(text.as.string, byte_len);
        int requested = (int)count_value.as.number;
        size_t count = requested < 0 ? 0 : (size_t)requested;
        if (count > cp_len) {
            count = cp_len;
        }
        /* Translate codepoint slice bounds to byte offsets. */
        size_t byte_start, byte_end;
        if (strcmp(name, "right") == 0) {
            byte_start = string_codepoint_offset(text.as.string, byte_len, cp_len - count);
            byte_end = byte_len;
        } else {
            byte_start = 0;
            byte_end = string_codepoint_offset(text.as.string, byte_len, count);
        }
        size_t byte_count = byte_end - byte_start;
        char *result_text = malloc(byte_count + 1);
        if (!result_text) {
            abort();
        }
        memcpy(result_text, text.as.string + byte_start, byte_count);
        result_text[byte_count] = '\0';
        Value result = value_string_n(result_text, byte_count);
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

        size_t byte_len = string_length(text.as.string);
        size_t cp_len = string_codepoint_count(text.as.string, byte_len);
        int raw_start = (int)start_value.as.number;
        int raw_count = (int)count_value.as.number;
        size_t start = raw_start < 0 ? 0 : (size_t)raw_start;
        size_t count = raw_count < 0 ? 0 : (size_t)raw_count;
        /* Codepoint slice bounds [start, start+count) clamped to the string. */
        if (start > cp_len) {
            start = cp_len;
        }
        if (count > cp_len - start) {
            count = cp_len - start;
        }
        size_t byte_start = string_codepoint_offset(text.as.string, byte_len, start);
        size_t byte_end = string_codepoint_offset(text.as.string, byte_len, start + count);

        if (expr->as.call.args.count == 3) {
            size_t slice = byte_end - byte_start;
            char *result_text = malloc(slice + 1);
            if (!result_text) {
                abort();
            }
            memcpy(result_text, text.as.string + byte_start, slice);
            result_text[slice] = '\0';
            Value result = value_string_n(result_text, slice);
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
        size_t replacement_len = string_length(replacement.as.string);
        size_t tail_len = byte_len - byte_end;
        size_t result_len = byte_start + replacement_len + tail_len;
        char *result_text = malloc(result_len + 1);
        if (!result_text) {
            abort();
        }
        memcpy(result_text, text.as.string, byte_start);
        memcpy(result_text + byte_start, replacement.as.string, replacement_len);
        memcpy(result_text + byte_start + replacement_len,
               text.as.string + byte_end,
               tail_len);
        result_text[result_len] = '\0';
        Value result = value_string_n(result_text, result_len);
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
            /* Strings are immutable values: reverse a copy, never the binding. */
            if (array->kind == VALUE_STRING) {
                return reverse_string_value(array->as.string,
                                            string_length(array->as.string));
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
        if (array.kind == VALUE_STRING) {
            Value result = reverse_string_value(array.as.string,
                                                string_length(array.as.string));
            value_free(array);
            return result;
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
    } else if (strcmp(name, "variance") == 0 || strcmp(name, "stdev") == 0 ||
               strcmp(name, "pvariance") == 0 || strcmp(name, "pstdev") == 0 ||
               strcmp(name, "skewness") == 0 || strcmp(name, "kurtosis") == 0) {
        /* Central moments about the mean (statistics_design.md §8). variance/
         * stdev are sample estimators (divide by n-1); pvariance/pstdev are the
         * population forms (divide by n). skewness/kurtosis are moment-based
         * (population) with excess kurtosis, matching scipy defaults. */
        int sample = (strcmp(name, "variance") == 0 || strcmp(name, "stdev") == 0);
        if (sample && count < 2) {
            char message[256];
            snprintf(message, sizeof(message), "%s expects at least two values", name);
            runtime_error_raise(message, 1003, "invalid function call");
            value_free(arg);
            return value_null();
        }
        double mean_v = 0.0;
        for (size_t i = 0; i < count; i++) {
            mean_v += arg.as.array.items[i].as.number;
        }
        mean_v /= (double)count;
        double m2 = 0.0, m3 = 0.0, m4 = 0.0;
        for (size_t i = 0; i < count; i++) {
            double d = arg.as.array.items[i].as.number - mean_v;
            double d2 = d * d;
            m2 += d2;
            m3 += d2 * d;
            m4 += d2 * d2;
        }
        if (strcmp(name, "variance") == 0) {
            result = m2 / (double)(count - 1);
        } else if (strcmp(name, "stdev") == 0) {
            result = sqrt(m2 / (double)(count - 1));
        } else if (strcmp(name, "pvariance") == 0) {
            result = m2 / (double)count;
        } else if (strcmp(name, "pstdev") == 0) {
            result = sqrt(m2 / (double)count);
        } else {
            double var_p = m2 / (double)count;
            if (var_p == 0.0) {
                char message[256];
                snprintf(message, sizeof(message),
                         "%s is undefined for zero-variance data", name);
                runtime_error_raise(message, 1003, "invalid function call");
                value_free(arg);
                return value_null();
            }
            if (strcmp(name, "skewness") == 0) {
                result = (m3 / (double)count) / pow(var_p, 1.5);
            } else {
                result = (m4 / (double)count) / (var_p * var_p) - 3.0;
            }
        }
    } else if (strcmp(name, "range") == 0) {
        double lo = arg.as.array.items[0].as.number;
        double hi = lo;
        for (size_t i = 1; i < count; i++) {
            double v = arg.as.array.items[i].as.number;
            if (v < lo) {
                lo = v;
            }
            if (v > hi) {
                hi = v;
            }
        }
        result = hi - lo;
    } else if (strcmp(name, "iqr") == 0) {
        double *sorted = malloc(sizeof(double) * count);
        if (!sorted) {
            abort();
        }
        for (size_t i = 0; i < count; i++) {
            sorted[i] = arg.as.array.items[i].as.number;
        }
        qsort(sorted, count, sizeof(double), number_compare);
        result = quantile_sorted(sorted, count, 0.75) -
                 quantile_sorted(sorted, count, 0.25);
        free(sorted);
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

    /* A variable bound to a function value, called like `f(args)`. Functions and
     * builtins above take precedence; this is the fallback once nothing else
     * matched the name (first_class_functions_design.md §3). */
    Symbol *fn_symbol = env_find(expr->as.call.name);
    if (fn_symbol && fn_symbol->value.kind == VALUE_FUNCTION) {
        FunctionDef *target = function_resolve(fn_symbol->value.as.function.library,
                                               fn_symbol->value.as.function.name);
        if (!target) {
            char message[256];
            snprintf(message, sizeof(message),
                     "function value references unknown function: %s",
                     fn_symbol->value.as.function.name);
            runtime_error_raise(message, 1003, "invalid function call");
            return value_null();
        }
        return eval_user_function(expr, target);
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
        int equal = string_value_equal_caseless(left.as.string, right.as.string);
        if (strcmp(op, "=") == 0) {
            result = equal;
        } else if (strcmp(op, "!=") == 0) {
            result = !equal;
        }
    } else if (left.kind == VALUE_STRING && right.kind == VALUE_STRING) {
        int cmp = string_value_compare(left.as.string, right.as.string);
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
    } else if (left.kind == VALUE_FUNCTION && right.kind == VALUE_FUNCTION) {
        int equal = function_value_equal(&left, &right);
        if (strcmp(op, "=") == 0) {
            result = equal;
        } else if (strcmp(op, "!=") == 0) {
            result = !equal;
        } else {
            runtime_error_raise("functions support only = and !=", 1003, "comparison");
            value_free(left);
            value_free(right);
            return value_null();
        }
    } else if (left.kind == VALUE_FUNCTION || right.kind == VALUE_FUNCTION) {
        /* A function compared against a non-function: equal only under !=. */
        if (strcmp(op, "=") == 0) {
            result = 0;
        } else if (strcmp(op, "!=") == 0) {
            result = 1;
        } else {
            runtime_error_raise("functions support only = and !=", 1003, "comparison");
            value_free(left);
            value_free(right);
            return value_null();
        }
    } else if (left.kind == VALUE_GOBJECT && right.kind == VALUE_GOBJECT) {
        /* Identity by canonical wrapper: qdata canonicalization guarantees one
         * wrapper per underlying GObject, so wrapper-pointer equality is object
         * identity. (Wrapper-pointer form also compiles when HAVE_GIR=0, where no
         * such value can ever exist.) */
        int equal = left.as.gobject == right.as.gobject;
        if (strcmp(op, "=") == 0) {
            result = equal;
        } else if (strcmp(op, "!=") == 0) {
            result = !equal;
        } else {
            runtime_error_raise("gobjects support only = and !=", 1003, "comparison");
            value_free(left);
            value_free(right);
            return value_null();
        }
    } else if (left.kind == VALUE_GOBJECT || right.kind == VALUE_GOBJECT) {
        /* A gobject compared against a non-gobject: unequal, matching the function
         * value rule. */
        if (strcmp(op, "=") == 0) {
            result = 0;
        } else if (strcmp(op, "!=") == 0) {
            result = 1;
        } else {
            runtime_error_raise("gobjects support only = and !=", 1003, "comparison");
            value_free(left);
            value_free(right);
            return value_null();
        }
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
        size_t left_len = string_length(left_text.as.string);
        size_t right_len = string_length(right_text.as.string);
        char *combined = malloc(left_len + right_len + 1);
        if (!combined) {
            abort();
        }
        memcpy(combined, left_text.as.string, left_len);
        memcpy(combined + left_len, right_text.as.string, right_len);
        combined[left_len + right_len] = '\0';
        Value result = value_string_n(combined, left_len + right_len);
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

/* --- PBI derivation (docs/pbi_design.md §4.2) ---------------------------- */

static Value derive_record(Value proto);

/* Build a new instance from a prototype record by applying each field's policy:
 *   exclude -> dropped; link -> share the cell (write-through identity);
 *   reset   -> fresh value from the reset expression evaluated in global scope;
 *   copy    -> independent, recursive copy.
 * Policies persist on the instance so it can itself serve as a prototype (so a
 * reset re-fires, and a link stays linked, when the instance is re-derived). */
static Value derive_record(Value proto) {
    size_t n = proto.as.record.count;
    RecordField *fields = n ? calloc(n, sizeof(RecordField)) : NULL;
    if (n && !fields) {
        abort();
    }
    size_t out = 0;
    for (size_t i = 0; i < n; i++) {
        RecordField *src = &proto.as.record.fields[i];
        if (src->policy == AST_FIELD_POLICY_EXCLUDE) {
            continue;
        }
        fields[out].name = copy_string(src->name);
        fields[out].policy = src->policy;
        fields[out].reset_expr = src->reset_expr;
        if (src->policy == AST_FIELD_POLICY_LINK) {
            ValueCell *cell = (ValueCell *)src->value;
            cell->refcount++;
            fields[out].value = src->value;
        } else if (src->policy == AST_FIELD_POLICY_RESET) {
            fields[out].value = cell_alloc();
            Env *saved = current_env;
            current_env = &global_env;
            *fields[out].value =
                src->reset_expr ? eval_expr(src->reset_expr) : value_null();
            current_env = saved;
        } else if (src->value->kind == VALUE_RECORD) {
            /* COPY of a nested instance: re-derive recursively so its own
             * policies (notably `reset`) re-fire at this `new` — the §6
             * recursive-derivation semantics. The recursion's own leaves
             * copy-on-write share, so it is mostly refcount bumps. */
            fields[out].value = cell_alloc();
            *fields[out].value = derive_record(*src->value);
        } else {
            /* COPY of a leaf value: copy-on-write share the cell now; the deep
             * copy is deferred to the first write (cell_fork_for_write). */
            ValueCell *cell = (ValueCell *)src->value;
            cell->refcount++;
            fields[out].value = src->value;
        }
        out++;
    }
    return value_record(fields, out);
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
            fields = calloc(expr->as.record.count, sizeof(RecordField));
            if (!fields) {
                abort();
            }
        }
        for (size_t i = 0; i < expr->as.record.count; i++) {
            fields[i].name = copy_string(expr->as.record.items[i].name);
            fields[i].value = cell_alloc();
            if (!fields[i].value) {
                abort();
            }
            *fields[i].value = eval_expr(expr->as.record.items[i].value);
            /* Carry the declared PBI policy from the literal into the runtime
             * record so `new` (Phase 2) can derive from it. reset_expr is a
             * shared AST pointer (the AST outlives all values). */
            fields[i].policy = expr->as.record.items[i].policy;
            fields[i].reset_expr = expr->as.record.items[i].reset_expr;
        }
        return value_record(fields, expr->as.record.count);
    }
    case AST_EXPR_NEW: {
        Value proto = eval_expr(expr->as.derive.proto);
        if (error_action_pending()) {
            value_free(proto);
            return value_null();
        }
        if (proto.kind != VALUE_RECORD) {
            value_free(proto);
            runtime_error_raise("new requires a record prototype", 1003,
                                "type error");
            return value_null();
        }
        Value instance = derive_record(proto);
        value_free(proto);
        if (error_action_pending()) {
            value_free(instance);
            return value_null();
        }
        if (expr->as.derive.with) {
            Value overrides = eval_expr(expr->as.derive.with);
            if (error_action_pending()) {
                value_free(overrides);
                value_free(instance);
                return value_null();
            }
            /* Apply the `with { … }` overrides. Each entry binds a fresh cell
             * (never writes through an inherited link to the prototype) and
             * carries its own declared policy; an unknown name is added. */
            for (size_t i = 0; i < overrides.as.record.count; i++) {
                RecordField *wf = &overrides.as.record.fields[i];
                RecordField *existing = record_find(&instance, wf->name);
                if (existing) {
                    cell_release(existing->value);
                    existing->value = cell_alloc();
                    *existing->value = value_copy(*wf->value);
                    existing->policy = wf->policy;
                    existing->reset_expr = wf->reset_expr;
                } else {
                    record_set(&instance, wf->name, value_copy(*wf->value));
                    RecordField *added = record_find(&instance, wf->name);
                    if (added) {
                        added->policy = wf->policy;
                        added->reset_expr = wf->reset_expr;
                    }
                }
            }
            value_free(overrides);
        }
        /* §8: after derivation + `with`, a `constructor` field runs with this =
         * the new instance. A raising constructor propagates with no instance. */
        if (!invoke_constructor(&instance)) {
            value_free(instance);
            return value_null();
        }
        return instance;
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
    case AST_EXPR_SPAWN:
        return eval_spawn(expr);
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
    if (!modifier.library && strcmp(modifier.name, "datetime") == 0) {
        /* A datetime is always a full timestamp: parse the date/time parts the
         * same way `date` does, then force second precision so a date-only
         * string fills 00:00:00 (distinguishing it from precision-inferring
         * `date`). Matches the value `now()` produces. */
        DateTime datetime;
        if (value.kind != VALUE_STRING || !parse_date_value(value.as.string, &datetime)) {
            fprintf(stderr, "datetime modifier expects an ISO-like date-time string\n");
            value_free(value);
            return value_null();
        }
        datetime.precision = PREC_SECOND;
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
        if (strcmp(target->as.ident, "this") == 0) {
            /* `this.field = …` resolves through the live receiver, so the write
             * reaches the real object and PBI field policies apply. */
            if (!current_this) {
                runtime_error_raise("this is only bound inside a method call",
                                    1003, "this");
                return NULL;
            }
            return current_this;
        }
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
        /* The caller takes a mutable pointer into this cell, so fork it now if
         * it is a shared copy-on-write cell (no-op for private or `link`). */
        cell_fork_for_write(field);
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
            /* See the AST_EXPR_FIELD branch: fork a shared copy-on-write cell
             * before handing out a mutable pointer. */
            cell_fork_for_write(field);
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
        if (strcmp(target->as.ident, "this") == 0) {
            /* `this` is read-only — you mutate through it, never rebind it. */
            runtime_error_raise("this is read-only", 1003, "this");
            value_free(value);
            return LVALUE_ASSIGN_ERROR;
        }
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

/* Give a dotted-def function (`function obj.method()`) its internal registered
 * name. Derived from source position so it is deterministic and stable across the
 * same program (required for cross-actor resolution, §10), and never typed by the
 * user. Idempotent: set once, reused on re-execution. */
static void method_ensure_internal_name(AstStmt *stmt) {
    if (stmt->as.function.name) {
        return;
    }
    char buffer[320];
    snprintf(buffer, sizeof(buffer), "%s.%s@%d:%d",
             stmt->as.function.object,
             stmt->as.function.field,
             stmt->line,
             stmt->column);
    stmt->as.function.name = copy_string(buffer);
}

/* Recursively register every dotted-def method body (anywhere in the program,
 * including inside a `program` block, functions, and control flow) under its
 * deterministic internal name, without performing the field store. A child actor
 * runs only its entry function, so it never reaches these attach statements; this
 * pre-pass makes the bodies resolvable so a record-with-method received over a
 * mailbox can call them (§10). Idempotent and safe to run alongside the bare
 * top-level function pre-pass. */
static void register_method_bodies_in(AstStmtList list) {
    for (size_t i = 0; i < list.count; i++) {
        AstStmt *stmt = list.items[i];
        switch (stmt->kind) {
        case AST_STMT_FUNCTION:
            if (stmt->as.function.object) {
                method_ensure_internal_name(stmt);
                function_register(stmt);
            }
            register_method_bodies_in(stmt->as.function.body);
            break;
        case AST_STMT_PROGRAM:
            register_method_bodies_in(stmt->as.program.body);
            break;
        case AST_STMT_LIBRARY:
            register_method_bodies_in(stmt->as.library.body);
            break;
        case AST_STMT_MODIFIER:
            register_method_bodies_in(stmt->as.modifier.body);
            break;
        case AST_STMT_WITH_LOCK:
            register_method_bodies_in(stmt->as.with_lock.body);
            break;
        case AST_STMT_FOR_EACH:
            register_method_bodies_in(stmt->as.for_each.body);
            break;
        case AST_STMT_WATCH:
            register_method_bodies_in(stmt->as.watch.body);
            break;
        case AST_STMT_WITHOUT_WATCHERS:
            register_method_bodies_in(stmt->as.without_watchers);
            break;
        case AST_STMT_IF:
            register_method_bodies_in(stmt->as.if_stmt.body);
            register_method_bodies_in(stmt->as.if_stmt.else_body);
            break;
        case AST_STMT_WHILE:
            register_method_bodies_in(stmt->as.while_stmt.body);
            break;
        case AST_STMT_CONSIDER:
            for (size_t j = 0; j < stmt->as.consider.branches.count; j++) {
                register_method_bodies_in(stmt->as.consider.branches.items[j].body);
            }
            register_method_bodies_in(stmt->as.consider.else_body);
            break;
        default:
            break;
        }
    }
}

/* Execute a define-and-attach statement: register the body under its internal
 * name, then store a function value referencing it in obj.field (ordinary field
 * assignment, so obj must be a record and PBI policies apply). Returns 1 on
 * success, 0 after raising. */
static int eval_method_attach(AstStmt *stmt) {
    method_ensure_internal_name(stmt);
    function_register(stmt);

    AstExpr object_expr = {0};
    object_expr.kind = AST_EXPR_IDENT;
    object_expr.as.ident = stmt->as.function.object;
    AstExpr field_expr = {0};
    field_expr.kind = AST_EXPR_FIELD;
    field_expr.as.field.object = &object_expr;
    field_expr.as.field.field = stmt->as.function.field;

    Value fn = value_function(stmt->as.function.name, NULL);
    LValueAssignResult result = assign_lvalue(&field_expr, fn);
    if (result == LVALUE_ASSIGN_ERROR) {
        /* assign_lvalue leaves the value to the caller on error (see the
         * AST_STMT_ASSIGN path). */
        value_free(fn);
        return 0;
    }
    if (result == LVALUE_ASSIGN_CHANGED) {
        char *watch_path = lvalue_watch_path(&field_expr);
        if (watch_path) {
            int ok = watcher_trigger_change(watch_path);
            free(watch_path);
            if (!ok) {
                return 0;
            }
        }
    }
    return 1;
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
        if (stmt->as.function.object) {
            /* Dotted def: an executable attach statement, not a hoisted decl. */
            if (!eval_method_attach(stmt)) {
                current_line = previous_line;
                current_column = previous_column;
                return eval_error_result();
            }
        } else {
            function_register(stmt);
        }
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

    /* When a program block runs, the top-level statements outside it are not
     * walked, so their function/modifier declarations would never register. Do
     * that up front (like the actor child entry does) so the block body can name
     * top-level functions and resolve function values — including methods, whose
     * dotted-def bodies register under their deterministic internal name. Without
     * a program block the normal top-level walk still registers on-reach. */
    if (program_block) {
        for (size_t i = 0; i < program.count; i++) {
            AstStmt *stmt = program.items[i];
            if (stmt->kind == AST_STMT_FUNCTION && !stmt->as.function.object) {
                function_register(stmt);
            } else if (stmt->kind == AST_STMT_MODIFIER) {
                modifier_register(stmt);
            }
        }
        /* Method bodies (dotted defs) may sit inside the program block; register
         * them so the parent resolves its own function values and a child can too
         * (the internal name is deterministic across the program). */
        register_method_bodies_in(program);
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
    actor_cleanup_children();
    retain_clear();
    monitor_clear();
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
    gi_library_loaded = 0;
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

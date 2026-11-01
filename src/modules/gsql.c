/* ===================== G-SQL: the interpreter as the SQL layer's host =====
 * (docs/gsql_sql_design.md §2). Included into src/eval.c, like the other
 * modules, because it is made of the evaluator's own static functions.
 *
 * THE POINT OF THIS FILE IS THAT IT DEFINES NO OPERATOR. Each one the SQL
 * layer asks for is a two-operand gBASIC expression -- `sql_a + sql_b`,
 * `sql_a < sql_b`, `not sql_a`, `lower(sql_a)`, `{date}sql_a` -- parsed once
 * by gBASIC's own parser and evaluated by eval_expr with the operands bound in
 * a private frame. So SQL means what gBASIC means by construction: three-valued
 * logic, exact integers, money with its guard digits and currency refusals,
 * date arithmetic. A second copy of those rules in the SQL layer would be a
 * second thing that can disagree.
 *
 * A raise inside a template is not reported: it is captured, cleared, and
 * handed back as the SQL error EXPRESSION carrying gBASIC's own sentence. */

#include "gsql_sql.h"
#include "../gsql/sql.h"

struct GsqlHv {
    Value v;
};

static GsqlHv *gsql_hv(Value v) {
    GsqlHv *h = malloc(sizeof *h);
    if (!h) {
        abort();
    }
    h->v = v;
    return h;
}

static int gsql_host_fail(GsqlError *err, int code, const char *fmt, ...) {
    if (err->code != GSQL_OK) {
        return err->code;
    }
    err->code = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof err->message, fmt, ap);
    va_end(ap);
    return code;
}

/* A raise that happened while the host worked: take it, clear it, and answer
 * it as an EXPRESSION error. 1 if there was one. */
static int gsql_host_caught(GsqlError *err) {
    if (!raise_in_flight && !runtime_stopped) {
        return 0;
    }
    gsql_host_fail(err, GSQL_EXPRESSION, "%s",
                   current_error.message ? current_error.message : "the expression raised an error");
    error_clear_state();
    raise_in_flight = 0;
    runtime_stopped = 0;
    return 1;
}

/* A warning from inside a SQL expression, KEPT until the SQL layer takes it
 * (docs/gsql_warning_design.md): the SQL layer says it once per statement and
 * the reply carries it to the program, which raises it as a gBASIC warning.
 * There is no source line inside SQL to put it at, which is why it is not
 * printed here. A message already waiting is not kept twice. */
static char **gsql_warned;
static int *gsql_warned_code;
static size_t gsql_nwarned;

static void gsql_host_warned(int code, const char *message) {
    if (!message) {
        return;
    }
    for (size_t k = 0; k < gsql_nwarned; k++) {
        if (strcmp(gsql_warned[k], message) == 0) {
            return;
        }
    }
    char **grown = realloc(gsql_warned, sizeof *grown * (gsql_nwarned + 1));
    int *codes = realloc(gsql_warned_code, sizeof *codes * (gsql_nwarned + 1));
    if (!grown || !codes) {
        abort();
    }
    gsql_warned = grown;
    gsql_warned_code = codes;
    gsql_warned_code[gsql_nwarned] = code;
    gsql_warned[gsql_nwarned++] = copy_string(message);
}

static int gsql_h_warning(void *ctx, int *code, char *message, size_t cap) {
    (void)ctx;
    if (!gsql_nwarned) {
        return 0;
    }
    *code = gsql_warned_code[0];
    snprintf(message, cap, "%s", gsql_warned[0]);
    free(gsql_warned[0]);
    memmove(gsql_warned, gsql_warned + 1, sizeof *gsql_warned * (gsql_nwarned - 1));
    memmove(gsql_warned_code, gsql_warned_code + 1, sizeof *gsql_warned_code * (gsql_nwarned - 1));
    gsql_nwarned--;
    return 1;
}

/* ---- templates ---------------------------------------------------------- */

typedef struct {
    char *source;
    AstStmtList program;   /* one assignment: sql_r = <the template> */
    AstExpr *expr;
} GsqlTemplate;

static GsqlTemplate *gsql_templates;
static size_t gsql_ntemplates;

static AstExpr *gsql_template(const char *source, GsqlError *err) {
    for (size_t k = 0; k < gsql_ntemplates; k++) {
        if (strcmp(gsql_templates[k].source, source) == 0) {
            return gsql_templates[k].expr;
        }
    }
    char text[256];
    snprintf(text, sizeof text, "sql_r = %s\n", source);
    AstStmtList program = { NULL, 0 };
    gb_diagnostics diags;
    gb_diagnostics_init(&diags);
    int rc = gb_parse(text, "<gsql>", &program, &diags);
    gb_diagnostics_free(&diags);
    if (rc != 0 || program.count != 1 || program.items[0]->kind != AST_STMT_ASSIGN) {
        ast_free_program(program);
        gsql_host_fail(err, GSQL_MISUSE, "the host could not parse its own template `%s`", source);
        return NULL;
    }
    GsqlTemplate *grown = realloc(gsql_templates, sizeof *grown * (gsql_ntemplates + 1));
    if (!grown) {
        abort();
    }
    gsql_templates = grown;
    gsql_templates[gsql_ntemplates].source = copy_string(source);
    gsql_templates[gsql_ntemplates].program = program;
    gsql_templates[gsql_ntemplates].expr = program.items[0]->as.assign.value;
    return gsql_templates[gsql_ntemplates++].expr;
}

/* Evaluates `source` with sql_a and sql_b bound to the operands (borrowed). */
static GsqlHv *gsql_apply(const char *source, const Value *const *args, size_t nargs, GsqlError *err) {
    AstExpr *expr = gsql_template(source, err);
    if (!expr) {
        return NULL;
    }
    static const char *const names[] = { "sql_a", "sql_b" };
    Env frame;
    memset(&frame, 0, sizeof frame);
    frame.parent = &global_env;
    Env *saved = current_env;
    current_env = &frame;
    for (size_t k = 0; k < nargs && k < 2; k++) {
        env_set(names[k], value_copy(*args[k]));
    }
    int line = current_line, column = current_column;
    /* A warning (2112, an overflowing sum) is CAPTURED rather than printed:
     * printed, it would name a position in this template, and since warnings
     * are deduplicated by site, every overflow in every SQL statement would
     * share that one site and only the first would ever be seen. */
    ErrorFrame frame_err;
    memset(&frame_err, 0, sizeof frame_err);
    frame_err.parent = current_error_frame;
    frame_err.warn_mode = WARN_MODE_NEXT;
    frame_err.entry_generation = error_generation;
    ErrorFrame *saved_err = current_error_frame;
    current_error_frame = &frame_err;
    Value r = eval_expr(expr);
    current_error_frame = saved_err;
    if (frame_err.warn_pending) {
        gsql_host_warned(current_warning.code, current_warning.message);
        warning_clear_state();
    }
    free(frame_err.label);
    current_line = line;
    current_column = column;
    current_env = saved;
    for (size_t k = 0; k < frame.count; k++) {
        free(frame.items[k].name);
        value_free(frame.items[k].value);
    }
    free(frame.items);
    for (size_t k = 0; k < frame.outer_reads_count; k++) {
        free(frame.outer_reads[k]);
    }
    free(frame.outer_reads);
    if (gsql_host_caught(err)) {
        value_free(r);
        return NULL;
    }
    return gsql_hv(r);
}

/* ---- the host's operations ---------------------------------------------- */

static GsqlHv *gsql_h_load(void *ctx, const unsigned char *v, size_t n, GsqlError *err) {
    (void)ctx;
    SerReader r = { (const char *)v, n, 0, 1, SER_VERSION };
    Value out = deserialize_value(&r, 0);
    if (gsql_host_caught(err)) {
        value_free(out);
        return NULL;
    }
    if (!r.ok || r.pos != n) {
        value_free(out);
        gsql_host_fail(err, GSQL_CORRUPT, "a stored value does not parse");
        return NULL;
    }
    /* SQL's NULL is unknown, whether it was bound as nothing or not (§3.1) */
    if (out.kind == VALUE_NULL) {
        out = value_unknown();
    }
    return gsql_hv(out);
}

static int gsql_h_store(void *ctx, const GsqlHv *v, GsqlBuf *out, GsqlError *err) {
    (void)ctx;
    SerBuf b;
    serbuf_init(&b);
    int ok = serialize_value(&b, v->v, 0);
    if (!ok || gsql_host_caught(err)) {
        free(b.bytes);
        return gsql_host_fail(err, GSQL_TYPE, "a value could not be encoded");
    }
    if (out->length + b.length > out->capacity) {
        size_t cap = out->capacity ? out->capacity : 64;
        while (cap < out->length + b.length) {
            cap *= 2;
        }
        unsigned char *grown = realloc(out->bytes, cap);
        if (!grown) {
            abort();
        }
        out->bytes = grown;
        out->capacity = cap;
    }
    memcpy(out->bytes + out->length, b.bytes, b.length);
    out->length += b.length;
    free(b.bytes);
    return GSQL_OK;
}

static int gsql_h_kind(void *ctx, const GsqlHv *v) {
    (void)ctx;
    switch (v->v.kind) {
    case VALUE_NULL: return SER_NULL;
    case VALUE_UNKNOWN: return SER_UNKNOWN;
    case VALUE_BOOL: return SER_BOOL;
    case VALUE_NUMBER: return SER_NUMBER;
    case VALUE_STRING: return SER_STRING;
    case VALUE_ARRAY: return SER_ARRAY;
    case VALUE_RECORD: return SER_RECORD;
    case VALUE_DATETIME: return SER_DATETIME;
    case VALUE_DURATION: return SER_DURATION;
    case VALUE_MONEY: return SER_MONEY;
    case VALUE_FILE: return SER_FILE;
    case VALUE_DIR: return SER_DIR;
    default: return 0;
    }
}

static int gsql_h_truth(void *ctx, const GsqlHv *v) {
    (void)ctx;
    if (v->v.kind == VALUE_BOOL) {
        return v->v.as.boolean ? 1 : 0;
    }
    if (v->v.kind == VALUE_UNKNOWN || v->v.kind == VALUE_NULL) {
        return -1;
    }
    return -2;
}

static GsqlHv *gsql_h_op(void *ctx, int op, const GsqlHv *a, const GsqlHv *b, GsqlError *err) {
    (void)ctx;
    static const char *const templates[] = {
        [GSQL_HOP_OR] = "sql_a or sql_b", [GSQL_HOP_AND] = "sql_a and sql_b",
        [GSQL_HOP_EQ] = "sql_a = sql_b", [GSQL_HOP_NE] = "sql_a != sql_b",
        [GSQL_HOP_LT] = "sql_a < sql_b", [GSQL_HOP_LE] = "sql_a <= sql_b",
        [GSQL_HOP_GT] = "sql_a > sql_b", [GSQL_HOP_GE] = "sql_a >= sql_b",
        [GSQL_HOP_ADD] = "sql_a + sql_b", [GSQL_HOP_SUB] = "sql_a - sql_b",
        [GSQL_HOP_MUL] = "sql_a * sql_b", [GSQL_HOP_DIV] = "sql_a / sql_b",
        [GSQL_HOP_MOD] = "mod(sql_a, sql_b)",
        [GSQL_HOP_NOT] = "not sql_a", [GSQL_HOP_NEG] = "-sql_a",
    };
    if (op < 1 || op > GSQL_HOP_NEG || !templates[op]) {
        gsql_host_fail(err, GSQL_MISUSE, "the host has no operator %d", op);
        return NULL;
    }
    const Value *args[2] = { &a->v, b ? &b->v : NULL };
    return gsql_apply(templates[op], args, b ? 2 : 1, err);
}

static GsqlHv *gsql_h_field(void *ctx, const GsqlHv *rec, const char *name, GsqlError *err) {
    (void)ctx;
    if (rec->v.kind == VALUE_UNKNOWN || rec->v.kind == VALUE_NULL) {
        return gsql_hv(value_unknown());
    }
    if (rec->v.kind != VALUE_RECORD) {
        gsql_host_fail(err, GSQL_EXPRESSION, "`.%s` reads a field of a record, and this value is %s", name,
                       builtin_type_name(rec->v));
        return NULL;
    }
    Value copy = rec->v;
    RecordField *f = record_find(&copy, name);
    return gsql_hv(f ? value_copy(*f->value) : value_unknown());
}

static GsqlHv *gsql_h_call(void *ctx, const char *name, GsqlHv *const *args, size_t n, GsqlError *err) {
    (void)ctx;
    char source[64];
    if (n == 1) {
        snprintf(source, sizeof source, "%s(sql_a)", name);
    } else if (n == 2) {
        snprintf(source, sizeof source, "%s(sql_a, sql_b)", name);
    } else {
        gsql_host_fail(err, GSQL_MISUSE, "the host calls with one or two arguments");
        return NULL;
    }
    const Value *vs[2] = { &args[0]->v, n > 1 ? &args[1]->v : NULL };
    return gsql_apply(source, vs, n, err);
}

static GsqlHv *gsql_h_cast(void *ctx, const GsqlHv *v, int kind, const char *currency, GsqlError *err) {
    (void)ctx;
    const Value *args[1] = { &v->v };
    int is_text = v->v.kind == VALUE_STRING;
    switch (kind) {
    case GSQL_COL_NUMBER:
        if (v->v.kind == VALUE_NUMBER) {
            return gsql_hv(value_copy(v->v));
        }
        return gsql_apply("{number}sql_a", args, 1, err);
    case GSQL_COL_INTEGER: {
        GsqlHv *r = v->v.kind == VALUE_NUMBER ? gsql_hv(value_copy(v->v)) : gsql_apply("{number}sql_a", args, 1, err);
        if (r && !(r->v.kind == VALUE_NUMBER && r->v.as.num.is_exact)) {
            value_free(r->v);
            free(r);
            gsql_host_fail(err, GSQL_EXPRESSION,
                           "CAST to INTEGER does not round: the value is not an exact integer");
            return NULL;
        }
        return r;
    }
    case GSQL_COL_STRING:
        return gsql_apply("string(sql_a)", args, 1, err);
    case GSQL_COL_BOOLEAN:
        if (v->v.kind == VALUE_BOOL) {
            return gsql_hv(value_copy(v->v));
        }
        if (is_text && (strcmp(v->v.as.string, "true") == 0 || strcmp(v->v.as.string, "false") == 0)) {
            return gsql_hv(value_bool(v->v.as.string[0] == 't'));
        }
        gsql_host_fail(err, GSQL_EXPRESSION, "CAST to BOOLEAN takes the text 'true' or 'false'");
        return NULL;
    case GSQL_COL_MONEY: {
        if (!currency || !currency[0]) {
            gsql_host_fail(err, GSQL_EXPRESSION, "CAST to MONEY needs its currency: MONEY(USD)");
            return NULL;
        }
        char source[32];
        snprintf(source, sizeof source, "{%s}sql_a", currency);
        return gsql_apply(source, args, 1, err);
    }
    case GSQL_COL_DATE:
        /* the inner {date} reads text at its written precision, the outer one
           takes the value's date (docs/datetime_conversion_design.md §3) */
        return gsql_apply("{date}({date}sql_a)", args, 1, err);
    case GSQL_COL_TIME:
        return gsql_apply("{time}sql_a", args, 1, err);
    case GSQL_COL_DATETIME:
        return gsql_apply("{datetime}sql_a", args, 1, err);
    case GSQL_COL_RECORD: {
        /* a record's text is gBASIC's `encode` form, read by its own `decode`
           (docs/gsql_dump_design.md §4) */
        if (v->v.kind == VALUE_RECORD) {
            return gsql_hv(value_copy(v->v));
        }
        if (!is_text) {
            gsql_host_fail(err, GSQL_EXPRESSION, "CAST to RECORD takes a record's text, and this value is %s",
                           builtin_type_name(v->v));
            return NULL;
        }
        GsqlHv *r = gsql_apply("decode(sql_a)", args, 1, err);
        if (r && r->v.kind != VALUE_RECORD) {
            gsql_host_fail(err, GSQL_EXPRESSION, "CAST to RECORD: the text is %s, not a record",
                           builtin_type_name(r->v));
            value_free(r->v);
            free(r);
            return NULL;
        }
        return r;
    }
    default:
        gsql_host_fail(err, GSQL_MISUSE, "CAST to an unknown kind");
        return NULL;
    }
}

static int gsql_h_order(void *ctx, const GsqlHv *a, const GsqlHv *b, int *cmp, GsqlError *err) {
    (void)ctx;
    if (a->v.kind != b->v.kind) {
        return gsql_host_fail(err, GSQL_EXPRESSION, "cannot order %s against %s", builtin_type_name(a->v),
                              builtin_type_name(b->v));
    }
    if (a->v.kind == VALUE_MONEY && a->v.as.money.currency != b->v.as.money.currency) {
        return gsql_host_fail(err, GSQL_EXPRESSION, "cannot order money in two currencies");
    }
    if (a->v.kind == VALUE_RECORD || a->v.kind == VALUE_ARRAY) {
        return gsql_host_fail(err, GSQL_EXPRESSION, "a %s has no order", builtin_type_name(a->v));
    }
    Value pair[1] = { a->v };
    if (!values_all_sort_comparable(pair, 1, "ORDER BY")) {
        gsql_host_caught(err);
        return err->code;
    }
    *cmp = sort_value_compare(&a->v, &b->v);
    return GSQL_OK;
}

static GsqlHv *gsql_h_copy(void *ctx, const GsqlHv *v) {
    (void)ctx;
    return gsql_hv(value_copy(v->v));
}

static void gsql_h_release(void *ctx, GsqlHv *v) {
    (void)ctx;
    value_free(v->v);
    free(v);
}

static const GsqlHost gsql_interpreter_host = {
    NULL, gsql_h_load, gsql_h_store, gsql_h_kind, gsql_h_truth, gsql_h_op, gsql_h_field,
    gsql_h_call, gsql_h_cast, gsql_h_order, gsql_h_copy, gsql_h_release, gsql_h_warning
};

const GsqlHost *gb_gsql_host(void) {
    return &gsql_interpreter_host;
}

void gb_gsql_host_teardown(void) {
    for (size_t k = 0; k < gsql_ntemplates; k++) {
        free(gsql_templates[k].source);
        ast_free_program(gsql_templates[k].program);
    }
    free(gsql_templates);
    gsql_templates = NULL;
    gsql_ntemplates = 0;
    for (size_t k = 0; k < gsql_nwarned; k++) {
        free(gsql_warned[k]);
    }
    free(gsql_warned);
    free(gsql_warned_code);
    gsql_warned = NULL;
    gsql_warned_code = NULL;
    gsql_nwarned = 0;
}

/* ===================== the `gsql` module: a program as a client ==========
 * (docs/gsql_module_design.md). `load gsql`, then gsql.connect, query, exec,
 * begin, commit, rollback and close. Every call is one request to the
 * database's owner over the protocol (docs/gsql_owner_design.md), which this
 * process starts as its child when no owner is running.
 *
 * REQUESTS AND REPLIES ARE gBASIC VALUES ON BOTH SIDES OF THE WIRE: a body is
 * a record the interpreter builds and `serialize`s, and a reply is
 * `deserialize`d back into one, so a parameter and a column value cross by the
 * same codec rows are stored in (v3) and an exact integer, a money amount and
 * a date arrive as what they were. */

#include "gsql_client.h"

/* rows asked for per batch: enough to amortise a round trip, small enough
 * that a batch is never most of a frame */
#define GSQL_MODULE_BATCH 500

/* A refusal from G-SQL. `error.details.code` names it, so a program can tell a
 * duplicate from a lost owner without reading English, and `position` says
 * where in the statement when there is a place. */
static void gsql_module_raise(const char *verb, const char *code, const char *message, long position,
                              const char *sql) {
    if (raise_in_flight) {
        return;
    }
    char text[1024];
    /* a verb is `query`, said as gsql.query; an sql block names itself, `sql cheap` */
    const char *dot = strncmp(verb, "sql ", 4) == 0 ? "" : "gsql.";
    if (position > 0) {
        snprintf(text, sizeof text, "%s%s: %s [%s at character %ld]", dot, verb, message, code, position);
    } else {
        snprintf(text, sizeof text, "%s%s: %s [%s]", dot, verb, message, code);
    }
    sql_append_statement(text, sizeof text, sql);
    Value details = value_record(NULL, 0);
    record_set(&details, "code", value_string(code));
    if (position > 0) {
        record_set(&details, "position", value_number_exact(position));
    }
    error_set_state_full(text, GSQL_ERROR_CODE, "gsql", details, value_null(), 0, 0);
    error_generation++;
    raise_in_flight = 1;
}

/* A misuse the module sees before anything is sent. */
static void gsql_module_refuse(const char *message) {
    runtime_error_raise(message, GSQL_ERROR_CODE, "gsql");
}

static void gsql_connection_shut(GsqlConnectionValue *c) {
    if (c->client) {
        gsql_client_close(c->client);
        c->client = NULL;
    }
    c->closed = 1;
}

static int gsql_connection_open(GsqlConnectionValue *c, const char *verb);

/* The connection a value is, borrowed (whatever holds the value still holds
 * it), connected if it was declared and not yet used; or NULL with a raise.
 * `v` is consumed. */
static GsqlConnectionValue *gsql_connection_of(Value v, const char *verb) {
    char msg[160];
    if (v.kind != VALUE_GSQL_CONNECTION) {
        snprintf(msg, sizeof msg, "gsql.%s expects a gsql connection first, not %s", verb, value_kind_name(v.kind));
        value_free(v);
        gsql_module_refuse(msg);
        return NULL;
    }
    GsqlConnectionValue *c = v.as.gsql_connection;
    value_free(v);
    if (c->closed) {
        snprintf(msg, sizeof msg, "gsql.%s: this connection is closed", verb);
        gsql_module_refuse(msg);
        return NULL;
    }
    if (!c->client && !gsql_connection_open(c, verb)) {
        return NULL;
    }
    return c;
}

/* The connection in the first argument, borrowed, or NULL with a raise. */
static GsqlConnectionValue *gsql_connection_arg(AstExpr *expr, const char *verb) {
    Value v = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(v);
        return NULL;
    }
    return gsql_connection_of(v, verb);
}

/* A string argument, owned, or NULL with a raise. */
static char *gsql_text_arg(AstExpr *expr, size_t index, const char *verb, const char *what) {
    Value v = eval_expr(expr->as.call.args.items[index]);
    if (error_action_pending()) {
        value_free(v);
        return NULL;
    }
    if (v.kind != VALUE_STRING) {
        char msg[200];
        snprintf(msg, sizeof msg, "gsql.%s expects %s as text, not %s", verb, what, value_kind_name(v.kind));
        value_free(v);
        gsql_module_refuse(msg);
        return NULL;
    }
    if (memchr(v.as.string, 0, string_length(v.as.string))) {
        char msg[200];
        snprintf(msg, sizeof msg, "gsql.%s: %s holds a NUL byte", verb, what);
        value_free(v);
        gsql_module_refuse(msg);
        return NULL;
    }
    char *s = copy_string(v.as.string);
    value_free(v);
    return s;
}

static Value *gsql_field(Value *record, const char *name) {
    RecordField *f = record_find(record, name);
    return (f && f->value) ? f->value : NULL;
}

/* GBASIC_GSQL_TRACE=1 writes one line per request to stderr, naming its
 * message: how a suite sees that a loop which stopped early sent no FETCH and
 * did send a CLOSE, which nothing in a program can observe. */
static void gsql_trace(unsigned type) {
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("GBASIC_GSQL_TRACE");
        on = e && strcmp(e, "1") == 0;
    }
    if (on) {
        fprintf(stderr, "gsql-trace: %s\n", gsql_msg_name((int)type));
    }
}

/* One request and its reply. 1 with *reply (a record) and *type; 0 with a
 * raise, the owner's own ERROR included. */
static void gsql_cursor_close_quiet(GsqlConnectionValue *c, Value cursor);

static int gsql_request(GsqlConnectionValue *c, unsigned type, Value body, const char *verb, const char *sql,
                        Value *reply, unsigned *reply_type) {
    *reply = value_null();
    SerBuf sb;
    serbuf_init(&sb);
    int ok = serialize_value(&sb, body, 0);
    value_free(body);
    if (!ok || error_action_pending()) {
        free(sb.bytes);
        return 0;
    }
    GsqlBuf b = { (unsigned char *)sb.bytes, sb.length, sb.capacity };
    GsqlFrame f;
    GsqlError err;
    memset(&err, 0, sizeof err);
    gsql_trace(type);
    int rc = gsql_client_call(c->client, type, &b, &f, &err);
    free(sb.bytes);
    if (rc) {
        gsql_module_raise(verb, gsql_error_name(err.code), err.message, 0, sql);
        return 0;
    }
    SerReader r = { (const char *)f.body, f.length, 0, 1, SER_VERSION };
    Value v = deserialize_value(&r, 0);
    if (error_action_pending()) {
        value_free(v);
        return 0;
    }
    if (!r.ok || r.pos != f.length || v.kind != VALUE_RECORD) {
        value_free(v);
        gsql_module_raise(verb, "PROTOCOL", "the owner answered with a body this client cannot read", 0, sql);
        return 0;
    }
    if (f.type == GSQL_M_ERROR) {
        Value *code = gsql_field(&v, "code");
        Value *message = gsql_field(&v, "message");
        Value *position = gsql_field(&v, "position");
        gsql_module_raise(verb, code && code->kind == VALUE_STRING ? code->as.string : "?",
                          message && message->kind == VALUE_STRING ? message->as.string : "(no message)",
                          position && position->kind == VALUE_NUMBER ? (long)position->as.num.value : 0, sql);
        value_free(v);
        return 0;
    }
    /* the statement's warnings, as gBASIC warnings at this call
       (docs/gsql_warning_design.md); under `on warning stop` the first one is
       an error, and an open cursor is closed rather than left behind */
    Value *w = gsql_field(&v, "warnings");
    for (size_t k = 0; w && w->kind == VALUE_ARRAY && k < w->as.array.store->count && !error_action_pending(); k++) {
        Value item = w->as.array.store->items[k];
        Value *code = item.kind == VALUE_RECORD ? gsql_field(&item, "code") : NULL;
        Value *message = item.kind == VALUE_RECORD ? gsql_field(&item, "message") : NULL;
        const char *text = message && message->kind == VALUE_STRING ? message->as.string : "(no message)";
        int n = code && code->kind == VALUE_NUMBER ? (int)code->as.num.value : 0;
        if (n == 2112) {
            runtime_warn_distinct(text, 2112, "exactness", value_null());
        } else if (n == 2118) {
            runtime_warn_distinct(text, 2118, "view", value_null());
        } else if (n == 2119) {
            runtime_warn_distinct(text, 2119, "foreign key", value_null());
        } else {
            runtime_warn_distinct(text, n, "gsql", value_null());
        }
    }
    if (error_action_pending()) {
        /* the quiet close, since the ordinary one declines while a raise is
           pending -- which is exactly now */
        Value *cur = f.type == GSQL_M_ROWS ? gsql_field(&v, "cursor") : NULL;
        if (cur) {
            gsql_cursor_close_quiet(c, value_copy(*cur));
        }
        value_free(v);
        return 0;
    }
    *reply = v;
    *reply_type = f.type;
    return 1;
}

/* --- connect --------------------------------------------------------------- */

static Value gsql_eval_connect(AstExpr *expr) {
    size_t argc = expr->as.call.args.count;
    if (argc < 1 || argc > 2) {
        gsql_module_refuse("gsql.connect expects a database path, and optionally { create: true }");
        return value_null();
    }
    Value p = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(p);
        return value_null();
    }
    const char *text = p.kind == VALUE_STRING ? p.as.string : p.kind == VALUE_FILE ? p.as.file_path
                     : p.kind == VALUE_DIR ? p.as.dir_path : NULL;
    if (!text || !*text) {
        char msg[200];
        snprintf(msg, sizeof msg, "gsql.connect expects a database path as text, not %s",
                 text ? "empty text" : value_kind_name(p.kind));
        value_free(p);
        gsql_module_refuse(msg);
        return value_null();
    }
    if (p.kind == VALUE_STRING && memchr(p.as.string, 0, string_length(p.as.string))) {
        value_free(p);
        gsql_module_refuse("gsql.connect: the path holds a NUL byte");
        return value_null();
    }
    char *path = copy_string(text);
    value_free(p);
    int create = 0;
    if (argc == 2) {
        Value o = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(o);
            free(path);
            return value_null();
        }
        if (o.kind != VALUE_RECORD) {
            char msg[200];
            snprintf(msg, sizeof msg, "gsql.connect expects its options as a record, not %s", value_kind_name(o.kind));
            value_free(o);
            free(path);
            gsql_module_refuse(msg);
            return value_null();
        }
        /* the option set is closed: a misspelt `create` would otherwise be a
           connect that silently refuses to make the database it was asked to */
        RecordField *fields = o.as.record.fields;
        for (size_t i = 0; i < o.as.record.count; i++) {
            if (strcmp(fields[i].name, "create") != 0) {
                char msg[300];
                snprintf(msg, sizeof msg, "gsql.connect has no option `%s`; the one option is `create`",
                         fields[i].name);
                value_free(o);
                free(path);
                gsql_module_refuse(msg);
                return value_null();
            }
            if (fields[i].value->kind != VALUE_BOOL) {
                value_free(o);
                free(path);
                gsql_module_refuse("gsql.connect: `create` must be true or false");
                return value_null();
            }
            create = fields[i].value->as.boolean;
        }
        value_free(o);
    }
    GsqlConnectionValue *c = calloc(1, sizeof *c);
    if (!c) {
        abort();
    }
    c->ref_count = 1;
    c->path = path;
    c->create = create;
    if (!gsql_connection_open(c, "connect")) {
        c->closed = 1;
        Value dead = value_gsql_connection(c);
        value_free(dead);
        return value_null();
    }
    return value_gsql_connection(c);
}

/* Reach the database's owner, or start one: 1, or 0 with a raise and the
 * connection still unconnected. gsql.connect calls it at once; a `database`
 * block's connection is opened by its first use. */
static int gsql_connection_open(GsqlConnectionValue *c, const char *verb) {
    if (!c->exe) {
        char exe[4096];
        if (!gb_exe_path(exe, sizeof exe)) {
            gsql_module_raise(verb, "NO_OWNER", "this interpreter cannot find its own executable, which it runs as "
                              "the database's owner", 0, NULL);
            return 0;
        }
        c->exe = copy_string(exe);
        c->owner_argv[0] = c->exe;
        c->owner_argv[1] = "--gsql-owner";
        c->owner_argv[2] = NULL;
    }
    GsqlClientOptions opt;
    memset(&opt, 0, sizeof opt);
    opt.owner_argv = c->owner_argv;
    opt.create = c->create;
    opt.name = "gbasic";
    GsqlError err;
    memset(&err, 0, sizeof err);
    if (gsql_client_open(c->path, &opt, &c->client, &err)) {
        c->client = NULL;
        char msg[700];
        if (err.code == GSQL_NOT_FOUND && !c->create) {
            snprintf(msg, sizeof msg, c->decl ? "there is no database at %s; give the block create: true to make one"
                                              : "there is no database at %s; give { create: true } to make one",
                     c->path);
        } else {
            snprintf(msg, sizeof msg, "%s", err.message);
        }
        gsql_module_raise(verb, gsql_error_name(err.code), msg, 0, NULL);
        return 0;
    }
    return 1;
}

/* --- statements ------------------------------------------------------------- */

/* The request for one statement; parameters, when given, are the third
 * argument: a record names them (:name), an array numbers them ($1). */
static int gsql_statement_body(AstExpr *expr, const char *verb, char **sql, Value *body) {
    size_t argc = expr->as.call.args.count;
    if (argc < 2 || argc > 3) {
        char msg[200];
        snprintf(msg, sizeof msg, "gsql.%s expects a connection, a statement, and optionally its parameters", verb);
        gsql_module_refuse(msg);
        return 0;
    }
    *sql = gsql_text_arg(expr, 1, verb, "the statement");
    if (!*sql) {
        return 0;
    }
    *body = value_record(NULL, 0);
    record_set(body, "statement", value_string(*sql));
    if (argc == 3) {
        Value params = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(params);
            value_free(*body);
            free(*sql);
            return 0;
        }
        if (params.kind != VALUE_RECORD && params.kind != VALUE_ARRAY) {
            char msg[300];
            snprintf(msg, sizeof msg,
                     "gsql.%s expects its parameters as a record (for :name) or an array (for $1, $2, ...), not %s",
                     verb, value_kind_name(params.kind));
            value_free(params);
            value_free(*body);
            free(*sql);
            gsql_module_refuse(msg);
            return 0;
        }
        record_set(body, "parameters", params);
    }
    record_set(body, "batch", value_number_exact(GSQL_MODULE_BATCH));
    return 1;
}

static void gsql_cursor_close(GsqlConnectionValue *c, Value *rows_reply) {
    Value *cur = gsql_field(rows_reply, "cursor");
    if (!cur) {
        return;
    }
    Value body = value_record(NULL, 0);
    record_set(&body, "cursor", value_copy(*cur));
    Value reply;
    unsigned type;
    if (gsql_request(c, GSQL_M_CLOSE, body, "close", NULL, &reply, &type)) {
        value_free(reply);
    }
}

/* --- a result, row by row -----------------------------------------------------
 *
 * A running SELECT and the batch of rows in hand. `gsql.query` drains one into
 * an array; `for each row in block` pulls one row per pass, so a block's
 * result is never held whole -- one batch of GSQL_MODULE_BATCH rows at a time.
 * Ending it early closes the owner's cursor (gsql_rows_end, called on every
 * exit from the loop). */
struct GsqlRows {
    GsqlConnectionValue *c;  /* a reference of its own, released at the end */
    char *who;
    char *sql;
    char **names;            /* the result's column names, borrowed from `columns` */
    size_t ncols;
    Value columns;
    Value batch;
    size_t next;             /* the next row of `batch` */
    int open;                /* whether the owner holds a cursor for it */
};

static void gsql_rows_end(struct GsqlRows *r);

/* Closes a cursor whatever is pending: a raise escaping the loop is exactly
 * when the cursor must still go, and gsql_request would decline to send. */
static void gsql_cursor_close_quiet(GsqlConnectionValue *c, Value cursor) {
    Value body = value_record(NULL, 0);
    record_set(&body, "cursor", cursor);
    SerBuf sb;
    serbuf_init(&sb);
    int ok = serialize_value(&sb, body, 0);
    value_free(body);
    if (ok && c->client) {
        GsqlBuf b = { (unsigned char *)sb.bytes, sb.length, sb.capacity };
        GsqlFrame f;
        GsqlError err;
        memset(&err, 0, sizeof err);
        gsql_trace(GSQL_M_CLOSE);
        if (gsql_client_call(c->client, GSQL_M_CLOSE, &b, &f, &err) == 0) {
            (void)f;
        }
    }
    free(sb.bytes);
}

/* Runs the statement in `body` and answers its rows, or NULL with a raise.
 * `sql` is taken; `remedy` says what to use when it returns no rows. */
static struct GsqlRows *gsql_rows_begin(GsqlConnectionValue *c, const char *who, char *sql, Value body,
                                        const char *remedy) {
    Value reply;
    unsigned type;
    if (!gsql_request(c, GSQL_M_QUERY, body, who, sql, &reply, &type)) {
        free(sql);
        return NULL;
    }
    if (type != GSQL_M_ROWS) {
        /* it RAN: say so, rather than let a refusal read as "nothing happened" */
        Value *n = gsql_field(&reply, "rows");
        char msg[300];
        if (n && n->kind == VALUE_NUMBER) {
            snprintf(msg, sizeof msg, "the statement returns no rows (it ran, and affected %lld); use %s for it",
                     (long long)n->as.num.value, remedy);
        } else {
            snprintf(msg, sizeof msg, "the statement returns no rows (it ran); use %s for it", remedy);
        }
        value_free(reply);
        gsql_module_raise(who, "STATE", msg, 0, sql);
        free(sql);
        return NULL;
    }
    struct GsqlRows *r = calloc(1, sizeof *r);
    if (!r) {
        abort();
    }
    c->ref_count++;
    r->c = c;
    r->who = copy_string(who);
    r->sql = sql;
    r->batch = reply;
    r->open = gsql_field(&reply, "cursor") != NULL;
    Value *cols = gsql_field(&reply, "columns");
    r->columns = cols ? value_copy(*cols) : value_array(NULL, 0);
    r->ncols = r->columns.kind == VALUE_ARRAY ? r->columns.as.array.store->count : 0;
    r->names = calloc(r->ncols ? r->ncols : 1, sizeof *r->names);
    if (!r->names) {
        abort();
    }
    /* the column names, once: two the same would make a record that keeps one
       and silently drops the other */
    for (size_t i = 0; i < r->ncols; i++) {
        Value col = r->columns.as.array.store->items[i];
        Value *nm = col.kind == VALUE_RECORD ? gsql_field(&col, "name") : NULL;
        r->names[i] = nm && nm->kind == VALUE_STRING ? nm->as.string : "?";
        for (size_t j = 0; j < i; j++) {
            if (strcmp(r->names[i], r->names[j]) == 0) {
                char msg[300];
                snprintf(msg, sizeof msg, "two result columns are named `%s`, and a row is a record; give one of them "
                         "another name with AS", r->names[i]);
                gsql_module_raise(who, "DUPLICATE", msg, 0, sql);
                gsql_rows_end(r);
                return NULL;
            }
        }
    }
    return r;
}

/* The next row as a record: 1, or 0 at the end or with a raise. */
static int gsql_rows_next(struct GsqlRows *r, Value *row) {
    for (;;) {
        Value *rows = gsql_field(&r->batch, "rows");
        if (rows && rows->kind == VALUE_ARRAY && r->next < rows->as.array.store->count) {
            Value raw = rows->as.array.store->items[r->next++];
            Value rec = value_record(NULL, 0);
            size_t n = raw.kind == VALUE_ARRAY ? raw.as.array.store->count : 0;
            for (size_t k = 0; k < r->ncols && k < n; k++) {
                record_set(&rec, r->names[k], value_copy(raw.as.array.store->items[k]));
            }
            *row = rec;
            return 1;
        }
        Value *cur = gsql_field(&r->batch, "cursor");
        if (!cur) {
            r->open = 0;
            return 0;
        }
        Value fetch = value_record(NULL, 0);
        record_set(&fetch, "cursor", value_copy(*cur));
        record_set(&fetch, "count", value_number_exact(GSQL_MODULE_BATCH));
        Value next;
        unsigned type;
        if (!gsql_request(r->c, GSQL_M_FETCH, fetch, r->who, r->sql, &next, &type)) {
            /* whatever the owner holds of it is gone with the failure */
            r->open = 0;
            return 0;
        }
        value_free(r->batch);
        r->batch = next;
        r->next = 0;
    }
}

static void gsql_rows_end(struct GsqlRows *r) {
    if (!r) {
        return;
    }
    if (r->open) {
        Value *cur = gsql_field(&r->batch, "cursor");
        if (cur) {
            gsql_cursor_close_quiet(r->c, value_copy(*cur));
        }
    }
    value_free(r->batch);
    value_free(r->columns);
    free(r->names);
    free(r->who);
    free(r->sql);
    Value held = value_gsql_connection(r->c);
    value_free(held);
    free(r);
}

/* --- sql blocks (docs/gsql_module_design.md §7) -------------------------------- */

/* A block's :name parameters, in order of first appearance; the load-time
 * check has already parsed the statement, so a failure here leaves none and
 * the statement's own refusal comes from the owner. */
static char **gsql_statement_params(const char *sql, size_t *count) {
    *count = 0;
    SqlParsed parsed;
    GsqlError err;
    memset(&err, 0, sizeof err);
    if (sql_parse(sql, strlen(sql), &parsed, &err) != 0) {
        return NULL;
    }
    size_t n = parsed.stmt->nparam_names;
    char **names = n ? calloc(n, sizeof *names) : NULL;
    if (n && !names) {
        abort();
    }
    for (size_t i = 0; i < n; i++) {
        names[i] = copy_string(parsed.stmt->param_names[i]);
    }
    sql_parsed_free(&parsed);
    *count = n;
    return names;
}

/* The connection a block is on: its database, by name, in the global frame,
 * where the `database` block bound it. Borrowed, or NULL with a raise. */
static GsqlConnectionValue *gsql_block_connection(const GsqlStatementValue *st, const char *who) {
    Symbol *sym = env_find_in_frame(&global_env, st->database);
    if (!sym || sym->value.kind != VALUE_GSQL_CONNECTION) {
        char msg[300];
        snprintf(msg, sizeof msg, "it is on `%s`, which %s", st->database,
                 sym ? "no longer holds a database -- something assigned over the `database` block's name"
                     : "is not declared");
        gsql_module_raise(who, "STATE", msg, 0, st->sql);
        return NULL;
    }
    return gsql_connection_of(value_copy(sym->value), who);
}

/* The request for a block: its statement, and each :name parameter read from
 * the variable of that name where the block runs. 1, or 0 with a raise. */
static int gsql_block_body(const GsqlStatementValue *st, const char *who, Value *body) {
    *body = value_record(NULL, 0);
    record_set(body, "statement", value_string(st->sql));
    if (st->nparams) {
        Value params = value_record(NULL, 0);
        for (size_t i = 0; i < st->nparams; i++) {
            Symbol *sym = env_find(st->params[i]);
            if (!sym) {
                char msg[300];
                snprintf(msg, sizeof msg, "the statement reads :%s, and there is no variable `%s` here",
                         st->params[i], st->params[i]);
                value_free(params);
                value_free(*body);
                gsql_module_raise(who, "PARAMETER", msg, 0, st->sql);
                return 0;
            }
            record_set(&params, st->params[i], value_copy(sym->value));
        }
        record_set(body, "parameters", params);
    }
    record_set(body, "batch", value_number_exact(GSQL_MODULE_BATCH));
    return 1;
}

/* `for each row in block`: the block's rows, or NULL with a raise. */
static struct GsqlRows *gsql_block_rows(const GsqlStatementValue *st) {
    char who[160];
    snprintf(who, sizeof who, "sql %s", st->name);
    GsqlConnectionValue *c = gsql_block_connection(st, who);
    if (!c) {
        return NULL;
    }
    Value body;
    if (!gsql_block_body(st, who, &body)) {
        return NULL;
    }
    char remedy[200];
    snprintf(remedy, sizeof remedy, "gsql.exec(%s)", st->name);
    return gsql_rows_begin(c, who, copy_string(st->sql), body, remedy);
}

/* The first argument of query and exec: a connection, then the statement and
 * its parameters; or an sql block alone. 1 with the connection, the statement,
 * the request and the name to refuse under; 0 with a raise. */
static int gsql_call_target(AstExpr *expr, const char *verb, GsqlConnectionValue **c, char **sql, Value *body,
                            char *who, size_t who_cap) {
    size_t argc = expr->as.call.args.count;
    if (argc < 1) {
        char msg[200];
        snprintf(msg, sizeof msg, "gsql.%s expects a connection, a statement, and optionally its parameters", verb);
        gsql_module_refuse(msg);
        return 0;
    }
    Value v = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(v);
        return 0;
    }
    if (v.kind == VALUE_GSQL_STATEMENT) {
        GsqlStatementValue *st = v.as.gsql_statement;
        if (argc != 1) {
            char msg[300];
            snprintf(msg, sizeof msg, "gsql.%s(%s) takes the block alone; its parameters are the variables its "
                     ":names name", verb, st->name);
            value_free(v);
            gsql_module_refuse(msg);
            return 0;
        }
        snprintf(who, who_cap, "sql %s", st->name);
        *c = gsql_block_connection(st, who);
        if (!*c || !gsql_block_body(st, who, body)) {
            value_free(v);
            return 0;
        }
        *sql = copy_string(st->sql);
        value_free(v);
        return 1;
    }
    snprintf(who, who_cap, "%s", verb);
    *c = gsql_connection_of(v, verb);
    return *c && gsql_statement_body(expr, verb, sql, body);
}

static Value gsql_eval_query(AstExpr *expr) {
    GsqlConnectionValue *c;
    char *sql;
    Value body;
    char who[160];
    if (!gsql_call_target(expr, "query", &c, &sql, &body, who, sizeof who)) {
        return value_null();
    }
    char remedy[200];
    if (strncmp(who, "sql ", 4) == 0) {
        snprintf(remedy, sizeof remedy, "gsql.exec(%s)", who + 4);
    } else {
        snprintf(remedy, sizeof remedy, "gsql.exec");
    }
    struct GsqlRows *r = gsql_rows_begin(c, who, sql, body, remedy);
    if (!r) {
        return value_null();
    }
    Value out = value_array(NULL, 0);
    Value row;
    while (gsql_rows_next(r, &row)) {
        Value grown = append_to_array_ref(&out, row, 0);
        value_free(grown);
    }
    gsql_rows_end(r);
    if (error_action_pending()) {
        value_free(out);
        return value_null();
    }
    return out;
}

static Value gsql_eval_exec(AstExpr *expr) {
    GsqlConnectionValue *c;
    char *sql;
    Value body;
    char who[160];
    if (!gsql_call_target(expr, "exec", &c, &sql, &body, who, sizeof who)) {
        return value_null();
    }
    Value reply;
    unsigned type;
    if (!gsql_request(c, GSQL_M_QUERY, body, who, sql, &reply, &type)) {
        free(sql);
        return value_null();
    }
    if (type == GSQL_M_ROWS) {
        gsql_cursor_close(c, &reply);
        value_free(reply);
        gsql_module_raise(who, "STATE", strcmp(who, "exec") == 0
                          ? "the statement returns rows; use gsql.query for it"
                          : "the statement returns rows; use gsql.query, or for each row in the block", 0, sql);
        free(sql);
        return value_null();
    }
    Value *n = gsql_field(&reply, "rows");
    char *command = sql_command_name(sql);
    Value result = value_record(NULL, 0);
    record_set(&result, "command", value_string(command));
    /* a count only where one means something, the rule sqlite, pg and odbc
       share: a CREATE has no rows to have affected */
    record_set(&result, "rows_affected",
               sql_command_counts_rows(command) && n ? value_copy(*n) : value_null());
    free(command);
    value_free(reply);
    free(sql);
    return result;
}

/* --- transactions and close --------------------------------------------------- */

static Value gsql_eval_simple(AstExpr *expr, const char *verb, unsigned type) {
    size_t argc = expr->as.call.args.count;
    size_t most = type == GSQL_M_BEGIN ? 2 : 1;
    if (argc < 1 || argc > most) {
        char msg[200];
        snprintf(msg, sizeof msg, type == GSQL_M_BEGIN ? "gsql.begin expects a connection, and optionally \"read\" or \"write\""
                                                       : "gsql.%s expects one connection", verb);
        gsql_module_refuse(msg);
        return value_null();
    }
    GsqlConnectionValue *c = gsql_connection_arg(expr, verb);
    if (!c) {
        return value_null();
    }
    Value body = value_record(NULL, 0);
    if (type == GSQL_M_BEGIN) {
        char *mode = argc == 2 ? gsql_text_arg(expr, 1, verb, "the mode") : copy_string("write");
        if (!mode) {
            value_free(body);
            return value_null();
        }
        if (strcmp(mode, "read") != 0 && strcmp(mode, "write") != 0) {
            char msg[200];
            snprintf(msg, sizeof msg, "gsql.begin: the mode is \"read\" or \"write\", not \"%.60s\"", mode);
            free(mode);
            value_free(body);
            gsql_module_refuse(msg);
            return value_null();
        }
        record_set(&body, "mode", value_string(mode));
        free(mode);
    }
    Value reply;
    unsigned rtype;
    if (!gsql_request(c, type, body, verb, NULL, &reply, &rtype)) {
        return value_null();
    }
    value_free(reply);
    return value_bool(1);
}

static Value gsql_eval_close(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        gsql_module_refuse("gsql.close expects one connection");
        return value_null();
    }
    Value v = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(v);
        return value_null();
    }
    /* a declared database that was never used has no owner to reach, and
       closing it must not start one */
    if (v.kind == VALUE_GSQL_CONNECTION && !v.as.gsql_connection->closed && !v.as.gsql_connection->client) {
        v.as.gsql_connection->closed = 1;
        value_free(v);
        return value_bool(1);
    }
    GsqlConnectionValue *c = gsql_connection_of(v, "close");
    if (!c) {
        return value_null();
    }
    gsql_connection_shut(c);
    return value_bool(1);
}

/* gsql.backup(connection, dir): a consistent copy as of now, made while the
 * database stays in use, refreshing a copy already there
 * (docs/gsql_backup_design.md). It is BACKUP TO, whose rows are its slices:
 * read to the end, and the last answered. A relative directory is the
 * PROGRAM's, resolved here, since the owner's working directory is its own. */
static Value gsql_eval_backup(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        gsql_module_refuse("gsql.backup expects a connection and the copy's directory");
        return value_null();
    }
    GsqlConnectionValue *c = gsql_connection_arg(expr, "backup");
    if (!c) {
        return value_null();
    }
    char *dir = gsql_text_arg(expr, 1, "backup", "the copy's directory");
    if (!dir) {
        return value_null();
    }
    if (!dir[0]) {
        free(dir);
        gsql_module_refuse("gsql.backup: the copy's directory is empty");
        return value_null();
    }
    char *path = dir;
    if (dir[0] != '/') {
        char cwd[4096];
        if (!getcwd(cwd, sizeof cwd)) {
            free(dir);
            gsql_module_refuse("gsql.backup: the working directory could not be read, to resolve a relative path");
            return value_null();
        }
        path = malloc(strlen(cwd) + strlen(dir) + 2);
        sprintf(path, "%s/%s", cwd, dir);
        free(dir);
    }
    Value params = value_array(NULL, 0);
    Value grown = append_to_array_ref(&params, value_string(path), 0);
    value_free(grown);
    free(path);
    Value body = value_record(NULL, 0);
    record_set(&body, "statement", value_string("BACKUP TO $1"));
    record_set(&body, "parameters", params);
    record_set(&body, "batch", value_number_exact(1));
    struct GsqlRows *r = gsql_rows_begin(c, "backup", copy_string("BACKUP TO $1"), body, "gsql.backup");
    if (!r) {
        return value_null();
    }
    Value last = value_null(), row;
    while (gsql_rows_next(r, &row)) {
        value_free(last);
        last = row;
    }
    gsql_rows_end(r);
    if (error_action_pending() || last.kind != VALUE_RECORD) {
        value_free(last);
        return value_null();
    }
    Value out = value_record(NULL, 0);
    const char *keep[3] = { "bytes", "files", "reused" };
    for (int k = 0; k < 3; k++) {
        Value *f = gsql_field(&last, keep[k]);
        record_set(&out, keep[k], f ? value_copy(*f) : value_null());
    }
    value_free(last);
    return out;
}

/* --- dump and restore (docs/gsql_dump_design.md) ----------------------------- */

/* a path made absolute against the PROGRAM's working directory, which the
   owner's is not; takes `p` */
static char *gsql_absolute(char *p, const char *verb) {
    if (p[0] == '/') {
        return p;
    }
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) {
        free(p);
        char msg[200];
        snprintf(msg, sizeof msg, "gsql.%s: the working directory could not be read, to resolve a relative path",
                 verb);
        gsql_module_refuse(msg);
        return NULL;
    }
    char *path = malloc(strlen(cwd) + strlen(p) + 2);
    if (!path) {
        abort();
    }
    sprintf(path, "%s/%s", cwd, p);
    free(p);
    return path;
}

/* gsql.dump(connection, file [, { schema_only: true }]): DUMP TO, read to
   its last progress row */
static Value gsql_eval_dump(AstExpr *expr) {
    size_t argc = expr->as.call.args.count;
    if (argc < 2 || argc > 3) {
        gsql_module_refuse("gsql.dump expects a connection, the file, and optionally { schema_only: true }");
        return value_null();
    }
    GsqlConnectionValue *c = gsql_connection_arg(expr, "dump");
    if (!c) {
        return value_null();
    }
    int schema_only = 0;
    if (argc == 3) {
        Value o = eval_expr(expr->as.call.args.items[2]);
        if (error_action_pending()) {
            value_free(o);
            return value_null();
        }
        if (o.kind != VALUE_RECORD) {
            char msg[200];
            snprintf(msg, sizeof msg, "gsql.dump expects its options as a record, not %s", value_kind_name(o.kind));
            value_free(o);
            gsql_module_refuse(msg);
            return value_null();
        }
        RecordField *fields = o.as.record.fields;
        for (size_t i = 0; i < o.as.record.count; i++) {
            if (strcmp(fields[i].name, "schema_only") != 0) {
                char msg[300];
                snprintf(msg, sizeof msg, "gsql.dump has no option `%.100s`; the one option is `schema_only`",
                         fields[i].name);
                value_free(o);
                gsql_module_refuse(msg);
                return value_null();
            }
            if (fields[i].value->kind != VALUE_BOOL) {
                value_free(o);
                gsql_module_refuse("gsql.dump: `schema_only` must be true or false");
                return value_null();
            }
            schema_only = fields[i].value->as.boolean;
        }
        value_free(o);
    }
    char *file = gsql_text_arg(expr, 1, "dump", "the file");
    if (!file) {
        return value_null();
    }
    if (!file[0]) {
        free(file);
        gsql_module_refuse("gsql.dump: the file's name is empty");
        return value_null();
    }
    char *path = gsql_absolute(file, "dump");
    if (!path) {
        return value_null();
    }
    const char *sql = schema_only ? "DUMP TO $1 SCHEMA ONLY" : "DUMP TO $1";
    Value params = value_array(NULL, 0);
    Value grown = append_to_array_ref(&params, value_string(path), 0);
    value_free(grown);
    free(path);
    Value body = value_record(NULL, 0);
    record_set(&body, "statement", value_string(sql));
    record_set(&body, "parameters", params);
    record_set(&body, "batch", value_number_exact(1));
    struct GsqlRows *r = gsql_rows_begin(c, "dump", copy_string(sql), body, "gsql.dump");
    if (!r) {
        return value_null();
    }
    Value last = value_null(), row;
    while (gsql_rows_next(r, &row)) {
        value_free(last);
        last = row;
    }
    gsql_rows_end(r);
    if (error_action_pending() || last.kind != VALUE_RECORD) {
        value_free(last);
        return value_null();
    }
    Value out = value_record(NULL, 0);
    const char *keep[4] = { "tables", "rows", "skipped", "bytes" };
    for (int k = 0; k < 4; k++) {
        Value *f = gsql_field(&last, keep[k]);
        record_set(&out, keep[k], f ? value_copy(*f) : value_null());
    }
    value_free(last);
    return out;
}

/* one request sent whatever is pending: the rollback after a failure */
static void gsql_simple_quiet(GsqlConnectionValue *c, unsigned type) {
    Value body = value_record(NULL, 0);
    SerBuf sb;
    serbuf_init(&sb);
    int ok = serialize_value(&sb, body, 0);
    value_free(body);
    if (ok && c->client) {
        GsqlBuf b = { (unsigned char *)sb.bytes, sb.length, sb.capacity };
        GsqlFrame f;
        GsqlError err;
        memset(&err, 0, sizeof err);
        gsql_trace(type);
        (void)gsql_client_call(c->client, type, &b, &f, &err);
    }
    free(sb.bytes);
}

typedef struct {
    char **v;
    size_t n;
} GsqlNames;

static int gsql_names_has(const GsqlNames *l, const char *name) {
    for (size_t k = 0; k < l->n; k++) {
        if (strcmp(l->v[k], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void gsql_names_free(GsqlNames *l) {
    for (size_t k = 0; k < l->n; k++) {
        free(l->v[k]);
    }
    free(l->v);
}

/* What a restore may run: the statements a dump writes, onto tables this
   restore made, so a file -- edited or hostile -- cannot change data that was
   there before it. NULL, or why not. */
static const char *gsql_restore_allowed(const SqlStmt *s, GsqlNames *made, char *why, size_t cap) {
    if (s->explain) {
        return "EXPLAIN is not a statement a dump writes";
    }
    switch (s->kind) {
    case SQ_CREATE_TABLE:
        if (s->if_exists) {
            return "CREATE TABLE IF NOT EXISTS could leave a table that was already there to be written to";
        }
        return NULL;
    case SQ_CREATE_VIEW:
        return s->or_replace ? "CREATE OR REPLACE VIEW could replace a view that was already there" : NULL;
    case SQ_INSERT:
        if (s->ins_q) {
            return "INSERT ... SELECT is not a statement a dump writes";
        }
        /* FALLTHROUGH */
    case SQ_CREATE_INDEX:
    case SQ_RESTART:
    case SQ_ADD_CHECK:
    case SQ_ADD_FKEY:
        if (!gsql_names_has(made, s->table)) {
            snprintf(why, cap, "it changes `%.100s`, a table this restore did not create", s->table);
            return why;
        }
        return NULL;
    default:
        return "only CREATE TABLE, CREATE INDEX, INSERT, ALTER TABLE ... ADD CONSTRAINT, ALTER COLUMN ... RESTART "
               "and CREATE VIEW, which a dump writes, may be in a file a restore runs";
    }
}

/* one statement of the file sent; 1, or 0 with a raise naming its line */
static int gsql_restore_send(GsqlConnectionValue *c, const char *sql, size_t len, long line, const char *file,
                             double *affected) {
    Value body = value_record(NULL, 0);
    record_set(&body, "statement", value_string_n(sql, len));
    SerBuf sb;
    serbuf_init(&sb);
    int ok = serialize_value(&sb, body, 0);
    value_free(body);
    if (!ok) {
        free(sb.bytes);
        return 0;
    }
    GsqlBuf b = { (unsigned char *)sb.bytes, sb.length, sb.capacity };
    GsqlFrame f;
    GsqlError err;
    memset(&err, 0, sizeof err);
    gsql_trace(GSQL_M_QUERY);
    int rc = gsql_client_call(c->client, GSQL_M_QUERY, &b, &f, &err);
    free(sb.bytes);
    char where[1300];
    if (rc) {
        snprintf(where, sizeof where, "line %ld of %.200s: %s", line, file, err.message);
        gsql_module_raise("restore", gsql_error_name(err.code), where, 0, NULL);
        return 0;
    }
    SerReader r = { (const char *)f.body, f.length, 0, 1, SER_VERSION };
    Value v = deserialize_value(&r, 0);
    if (!r.ok || v.kind != VALUE_RECORD) {
        value_free(v);
        gsql_module_raise("restore", "PROTOCOL", "the owner answered with a body this client cannot read", 0, NULL);
        return 0;
    }
    if (f.type == GSQL_M_ERROR) {
        Value *code = gsql_field(&v, "code");
        Value *message = gsql_field(&v, "message");
        snprintf(where, sizeof where, "line %ld of %.200s: %.900s", line, file,
                 message && message->kind == VALUE_STRING ? message->as.string : "(no message)");
        gsql_module_raise("restore", code && code->kind == VALUE_STRING ? code->as.string : "?", where, 0, NULL);
        value_free(v);
        return 0;
    }
    Value *n = gsql_field(&v, "rows");
    if (n && n->kind == VALUE_NUMBER) {
        *affected += n->as.num.value;
    }
    value_free(v);
    return 1;
}

#define GSQL_RESTORE_CHUNK (1u << 20)
#define GSQL_RESTORE_BATCH 10

/* gsql.restore(connection, file): runs a dump's statements, the rows ten
   statements a transaction; streamed, so the file is never held whole */
static Value gsql_eval_restore(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        gsql_module_refuse("gsql.restore expects a connection and the dump's file");
        return value_null();
    }
    GsqlConnectionValue *c = gsql_connection_arg(expr, "restore");
    if (!c) {
        return value_null();
    }
    char *file = gsql_text_arg(expr, 1, "restore", "the dump's file");
    if (!file) {
        return value_null();
    }
    FILE *in = fopen(file, "rb");
    if (!in) {
        char msg[400];
        snprintf(msg, sizeof msg, "gsql.restore: `%.200s` could not be read: %s", file, strerror(errno));
        free(file);
        gsql_module_refuse(msg);
        return value_null();
    }
    char *buf = NULL;
    size_t cap = 0, have = 0, at = 0;
    int eof = 0, in_txn = 0, failed = 0;
    long line = 1, batch = 0;
    double rows = 0, statements = 0;
    GsqlNames made = { NULL, 0 };
    while (!failed) {
        size_t end = 0;
        int empty = 1;
        int r = sql_statement_end(buf ? buf + at : "", have - at, &end, &empty);
        if (r != 1 && !eof) {
            /* more text: keep what is left, read a chunk more */
            memmove(buf, buf + at, have - at);
            have -= at;
            at = 0;
            if (have + GSQL_RESTORE_CHUNK + 1 > cap) {
                cap = have + GSQL_RESTORE_CHUNK + 1;
                char *g = realloc(buf, cap);
                if (!g) {
                    abort();
                }
                buf = g;
            }
            size_t got = fread(buf + have, 1, GSQL_RESTORE_CHUNK, in);
            have += got;
            eof = got == 0;
            continue;
        }
        if (r == 0 && empty) {
            break;                       /* the end, comments and space after the last statement */
        }
        const char *stmt = buf + at;
        size_t slen = r == 1 ? end : have - at;
        long first = line;
        for (size_t k = 0; k < slen; k++) {
            line += stmt[k] == '\n';
        }
        /* the statement's own first line, past the space and comments before it */
        size_t lead = 0;
        while (lead < slen && (stmt[lead] == ' ' || stmt[lead] == '\n' || stmt[lead] == '\t' || stmt[lead] == '\r' ||
                               (stmt[lead] == '-' && lead + 1 < slen && stmt[lead + 1] == '-'))) {
            if (stmt[lead] == '-') {
                while (lead < slen && stmt[lead] != '\n') {
                    lead++;
                }
            } else {
                first += stmt[lead] == '\n';
                lead++;
            }
        }
        at += slen;
        char where[600];
        if (r < 0) {
            snprintf(where, sizeof where, "line %ld of %.200s: the file ends inside a string or a comment", first,
                     file);
            gsql_module_raise("restore", "SYNTAX", where, 0, NULL);
            failed = 1;
            break;
        }
        SqlParsed parsed;
        GsqlError perr;
        memset(&perr, 0, sizeof perr);
        if (sql_parse(stmt, slen, &parsed, &perr) != GSQL_OK) {
            snprintf(where, sizeof where, "line %ld of %.200s: %.300s", first, file, perr.message);
            gsql_module_raise("restore", gsql_error_name(perr.code), where, 0, NULL);
            failed = 1;
            break;
        }
        char why[300];
        const char *no = gsql_restore_allowed(parsed.stmt, &made, why, sizeof why);
        int insert = parsed.stmt->kind == SQ_INSERT;
        char *created = parsed.stmt->kind == SQ_CREATE_TABLE ? copy_string(parsed.stmt->table) : NULL;
        sql_parsed_free(&parsed);
        if (no) {
            free(created);
            snprintf(where, sizeof where, "line %ld of %.200s: refused, and nothing after it run: %s", first, file,
                     no);
            gsql_module_raise("restore", "STATE", where, 0, NULL);
            failed = 1;
            break;
        }
        if (insert && !in_txn) {
            Value body = value_record(NULL, 0);
            record_set(&body, "mode", value_string("write"));
            Value reply;
            unsigned type;
            if (!gsql_request(c, GSQL_M_BEGIN, body, "restore", NULL, &reply, &type)) {
                failed = 1;
                free(created);
                break;
            }
            value_free(reply);
            in_txn = 1;
            batch = 0;
        } else if (!insert && in_txn) {
            Value reply;
            unsigned type;
            in_txn = 0;
            if (!gsql_request(c, GSQL_M_COMMIT, value_record(NULL, 0), "restore", NULL, &reply, &type)) {
                failed = 1;
                free(created);
                break;
            }
            value_free(reply);
        }
        double affected = 0;
        if (!gsql_restore_send(c, stmt, slen, first, file, &affected)) {
            failed = 1;
            free(created);
            break;
        }
        statements++;
        if (insert) {
            rows += affected;
        }
        if (created) {
            char **g = realloc(made.v, sizeof *g * (made.n + 1));
            if (!g) {
                abort();
            }
            made.v = g;
            made.v[made.n++] = created;
        }
        if (insert && ++batch == GSQL_RESTORE_BATCH) {
            Value reply;
            unsigned type;
            in_txn = 0;
            if (!gsql_request(c, GSQL_M_COMMIT, value_record(NULL, 0), "restore", NULL, &reply, &type)) {
                failed = 1;
                break;
            }
            value_free(reply);
        }
    }
    if (in_txn) {
        if (failed) {
            gsql_simple_quiet(c, GSQL_M_ROLLBACK);
        } else {
            Value reply;
            unsigned type;
            if (gsql_request(c, GSQL_M_COMMIT, value_record(NULL, 0), "restore", NULL, &reply, &type)) {
                value_free(reply);
            } else {
                failed = 1;
            }
        }
    }
    fclose(in);
    free(buf);
    free(file);
    gsql_names_free(&made);
    if (failed) {
        return value_null();
    }
    Value out = value_record(NULL, 0);
    record_set(&out, "statements", value_number_exact((long long)statements));
    record_set(&out, "rows", value_number_exact((long long)rows));
    return out;
}

static Value gsql_eval_call(AstExpr *expr) {
    const char *fn = expr->as.call.name;
    if (strcmp(fn, "connect") == 0) return gsql_eval_connect(expr);
    if (strcmp(fn, "query") == 0) return gsql_eval_query(expr);
    if (strcmp(fn, "exec") == 0) return gsql_eval_exec(expr);
    if (strcmp(fn, "begin") == 0) return gsql_eval_simple(expr, "begin", GSQL_M_BEGIN);
    if (strcmp(fn, "commit") == 0) return gsql_eval_simple(expr, "commit", GSQL_M_COMMIT);
    if (strcmp(fn, "rollback") == 0) return gsql_eval_simple(expr, "rollback", GSQL_M_ROLLBACK);
    if (strcmp(fn, "close") == 0) return gsql_eval_close(expr);
    if (strcmp(fn, "backup") == 0) return gsql_eval_backup(expr);
    if (strcmp(fn, "dump") == 0) return gsql_eval_dump(expr);
    if (strcmp(fn, "restore") == 0) return gsql_eval_restore(expr);
    char message[256];
    module_unknown_verb(message, sizeof(message), "gsql", fn);
    gsql_module_refuse(message);
    return value_null();
}

#ifndef _WIN32
/* `gbasic --gsql-owner DB FD MODE`: the owner process a client starts
 * (docs/gsql_owner_design.md §6), serving the SQL layer over this host. */
int gb_gsql_owner_main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "gbasic --gsql-owner: wants DB FD create|open\n");
        return 2;
    }
    GsqlOwnerOptions opt;
    memset(&opt, 0, sizeof opt);
    opt.ready_fd = atoi(argv[1]);
    opt.create = strcmp(argv[2], "create") == 0;
    GsqlSql *q = gsql_sql_new(&gsql_interpreter_host);
    GsqlService svc = gsql_sql_service(q);
    int rc = gsql_owner_run(argv[0], &svc, &opt);
    gsql_sql_free(q);
    gb_gsql_host_teardown();
    return rc;
}
#else
int gb_gsql_owner_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr, "gbasic --gsql-owner: G-SQL's owner is not yet available on Windows\n");
    return 2;
}
#endif

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

/* A warning from inside a SQL expression, said once per distinct message for
 * the life of the owner: there is no source line to put it at, and a loop over
 * a million rows must not print a million lines. It reaches the owner's stderr,
 * which is its client's; carrying it to the client as data is later work
 * (docs/gsql_sql_design.md §8). */
static char **gsql_warned;
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
    if (!grown) {
        abort();
    }
    gsql_warned = grown;
    gsql_warned[gsql_nwarned++] = copy_string(message);
    fprintf(stderr, "warning: %s, in a G-SQL expression [%d]\n", message, code);
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
        return gsql_apply("{date}sql_a", args, 1, err);
    case GSQL_COL_TIME:
        return gsql_apply("{time}sql_a", args, 1, err);
    case GSQL_COL_DATETIME:
        return gsql_apply("{datetime}sql_a", args, 1, err);
    default:
        gsql_host_fail(err, GSQL_EXPRESSION, "CAST to RECORD is not supported");
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
    gsql_h_call, gsql_h_cast, gsql_h_order, gsql_h_copy, gsql_h_release
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
    gsql_warned = NULL;
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
    if (position > 0) {
        snprintf(text, sizeof text, "gsql.%s: %s [%s at character %ld]", verb, message, code, position);
    } else {
        snprintf(text, sizeof text, "gsql.%s: %s [%s]", verb, message, code);
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

/* The connection in argument `index`, borrowed (the variable still holds it),
 * or NULL with a raise. */
static GsqlConnectionValue *gsql_connection_arg(AstExpr *expr, const char *verb) {
    Value v = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(v);
        return NULL;
    }
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
    return c;
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

/* One request and its reply. 1 with *reply (a record) and *type; 0 with a
 * raise, the owner's own ERROR included. */
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
    char exe[4096];
    if (!gb_exe_path(exe, sizeof exe)) {
        free(path);
        gsql_module_raise("connect", "NO_OWNER", "this interpreter cannot find its own executable, which it runs as "
                          "the database's owner", 0, NULL);
        return value_null();
    }
    GsqlConnectionValue *c = calloc(1, sizeof *c);
    if (!c) {
        abort();
    }
    c->ref_count = 1;
    c->path = path;
    c->exe = copy_string(exe);
    c->owner_argv[0] = c->exe;
    c->owner_argv[1] = "--gsql-owner";
    c->owner_argv[2] = NULL;
    GsqlClientOptions opt;
    memset(&opt, 0, sizeof opt);
    opt.owner_argv = c->owner_argv;
    opt.create = create;
    opt.name = "gbasic";
    GsqlError err;
    memset(&err, 0, sizeof err);
    if (gsql_client_open(c->path, &opt, &c->client, &err)) {
        char msg[700];
        if (err.code == GSQL_NOT_FOUND && !create) {
            snprintf(msg, sizeof msg, "there is no database at %s; give { create: true } to make one", c->path);
        } else {
            snprintf(msg, sizeof msg, "%s", err.message);
        }
        gsql_module_raise("connect", gsql_error_name(err.code), msg, 0, NULL);
        c->closed = 1;
        Value dead = value_gsql_connection(c);
        value_free(dead);
        return value_null();
    }
    return value_gsql_connection(c);
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

static Value gsql_eval_query(AstExpr *expr) {
    GsqlConnectionValue *c = expr->as.call.args.count >= 1 ? gsql_connection_arg(expr, "query") : NULL;
    if (!c) {
        if (expr->as.call.args.count < 1) {
            gsql_module_refuse("gsql.query expects a connection, a statement, and optionally its parameters");
        }
        return value_null();
    }
    char *sql;
    Value body;
    if (!gsql_statement_body(expr, "query", &sql, &body)) {
        return value_null();
    }
    Value reply;
    unsigned type;
    if (!gsql_request(c, GSQL_M_QUERY, body, "query", sql, &reply, &type)) {
        free(sql);
        return value_null();
    }
    if (type != GSQL_M_ROWS) {
        /* it RAN: say so, rather than let a refusal read as "nothing happened" */
        Value *n = gsql_field(&reply, "rows");
        char msg[200];
        if (n && n->kind == VALUE_NUMBER) {
            snprintf(msg, sizeof msg, "the statement returns no rows (it ran, and affected %lld); use gsql.exec for it",
                     (long long)n->as.num.value);
        } else {
            snprintf(msg, sizeof msg, "the statement returns no rows (it ran); use gsql.exec for it");
        }
        value_free(reply);
        gsql_module_raise("query", "STATE", msg, 0, sql);
        free(sql);
        return value_null();
    }
    /* the column names, once: two the same would make a record that keeps one
       and silently drops the other */
    Value *cols = gsql_field(&reply, "columns");
    size_t ncols = cols && cols->kind == VALUE_ARRAY ? cols->as.array.store->count : 0;
    char **names = calloc(ncols ? ncols : 1, sizeof *names);
    if (!names) {
        abort();
    }
    for (size_t i = 0; i < ncols; i++) {
        Value col = cols->as.array.store->items[i];
        Value *nm = col.kind == VALUE_RECORD ? gsql_field(&col, "name") : NULL;
        names[i] = nm && nm->kind == VALUE_STRING ? nm->as.string : "?";
        for (size_t j = 0; j < i; j++) {
            if (strcmp(names[i], names[j]) == 0) {
                char msg[300];
                snprintf(msg, sizeof msg, "two result columns are named `%s`, and a row is a record; give one of them "
                         "another name with AS", names[i]);
                gsql_cursor_close(c, &reply);
                value_free(reply);
                free(names);
                gsql_module_raise("query", "DUPLICATE", msg, 0, sql);
                free(sql);
                return value_null();
            }
        }
    }
    Value out = value_array(NULL, 0);
    Value batch = reply;      /* the first batch is the reply itself */
    int owned = 0;            /* whether `batch` is a later one, to free */
    for (;;) {
        Value *rows = gsql_field(&batch, "rows");
        if (rows && rows->kind == VALUE_ARRAY) {
            for (size_t r = 0; r < rows->as.array.store->count; r++) {
                Value row = rows->as.array.store->items[r];
                Value rec = value_record(NULL, 0);
                size_t n = row.kind == VALUE_ARRAY ? row.as.array.store->count : 0;
                for (size_t k = 0; k < ncols && k < n; k++) {
                    record_set(&rec, names[k], value_copy(row.as.array.store->items[k]));
                }
                Value grown = append_to_array_ref(&out, rec, 0);
                value_free(grown);
            }
        }
        Value *cur = gsql_field(&batch, "cursor");
        if (!cur) {
            break;
        }
        Value fetch = value_record(NULL, 0);
        record_set(&fetch, "cursor", value_copy(*cur));
        record_set(&fetch, "count", value_number_exact(GSQL_MODULE_BATCH));
        if (owned) {
            value_free(batch);
        }
        owned = 0;
        Value next;
        if (!gsql_request(c, GSQL_M_FETCH, fetch, "query", sql, &next, &type)) {
            break;
        }
        batch = next;
        owned = 1;
    }
    if (owned) {
        value_free(batch);
    }
    free(names);
    value_free(reply);
    free(sql);
    if (error_action_pending()) {
        value_free(out);
        return value_null();
    }
    return out;
}

static Value gsql_eval_exec(AstExpr *expr) {
    GsqlConnectionValue *c = expr->as.call.args.count >= 1 ? gsql_connection_arg(expr, "exec") : NULL;
    if (!c) {
        if (expr->as.call.args.count < 1) {
            gsql_module_refuse("gsql.exec expects a connection, a statement, and optionally its parameters");
        }
        return value_null();
    }
    char *sql;
    Value body;
    if (!gsql_statement_body(expr, "exec", &sql, &body)) {
        return value_null();
    }
    Value reply;
    unsigned type;
    if (!gsql_request(c, GSQL_M_QUERY, body, "exec", sql, &reply, &type)) {
        free(sql);
        return value_null();
    }
    if (type == GSQL_M_ROWS) {
        gsql_cursor_close(c, &reply);
        value_free(reply);
        gsql_module_raise("exec", "STATE", "the statement returns rows; use gsql.query for it", 0, sql);
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
    GsqlConnectionValue *c = gsql_connection_arg(expr, "close");
    if (!c) {
        return value_null();
    }
    gsql_connection_shut(c);
    return value_bool(1);
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

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

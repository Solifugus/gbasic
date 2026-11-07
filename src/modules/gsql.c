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
    static const char *const names[] = { "sql_a", "sql_b", "sql_c" };
    Env frame;
    memset(&frame, 0, sizeof frame);
    frame.parent = &global_env;
    Env *saved = current_env;
    current_env = &frame;
    for (size_t k = 0; k < nargs && k < 3; k++) {
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

/* substring(text, start [, length]) as gBASIC's mid, which counts from 0
 * and clamps at the end: SQL counts characters from 1. A start before 1 or a
 * negative or fractional number is refused (gdash's A5). */
static GsqlHv *gsql_h_substring(GsqlHv *const *args, size_t n, GsqlError *err) {
    for (size_t k = 1; k < n; k++) {
        const Value *v = &args[k]->v;
        double d = v->kind == VALUE_NUMBER ? v->as.num.value : -1;
        if (v->kind != VALUE_NUMBER || d != floor(d) || d < (k == 1 ? 1 : 0)) {
            gsql_host_fail(err, GSQL_EXPRESSION, k == 1
                           ? "substring's start is a whole number from 1, counting the first character as 1"
                           : "substring's length is a whole number from 0");
            return NULL;
        }
    }
    const Value *vs[3] = { &args[0]->v, &args[1]->v, n > 2 ? &args[2]->v : NULL };
    return gsql_apply(n > 2 ? "mid(sql_a, sql_b - 1, sql_c)" : "mid(sql_a, sql_b - 1, len(sql_a))", vs, n, err);
}

/* One of a datetime's fields as a number, or -1 when it has none (finer than
 * the value's precision, gBASIC's `unknown`). */
static long gsql_dt_field(const Value *v, const char *field, GsqlError *err) {
    char source[48];
    snprintf(source, sizeof source, "sql_a.%s", field);
    const Value *vs[1] = { v };
    GsqlHv *r = gsql_apply(source, vs, 1, err);
    if (!r) {
        return -2;
    }
    long out = r->v.kind == VALUE_NUMBER ? (long)r->v.as.num.value : -1;
    value_free(r->v);
    free(r);
    return out;
}

/* ISO 8601's weeks in a year: 53 when it begins on a Thursday, or is a leap
 * year beginning on a Wednesday. */
static int gsql_iso_weeks(long y) {
    long p = (y + y / 4 - y / 100 + y / 400) % 7;
    long q = ((y - 1) + (y - 1) / 4 - (y - 1) / 100 + (y - 1) / 400) % 7;
    return p == 4 || q == 3 ? 53 : 52;
}

/* extract(part FROM value), gdash's A5: gBASIC's own field for year, month,
 * day, hour, minute and second; quarter from the month; week as ISO 8601
 * counts it (week 1 holds the year's first Thursday). A part the value does
 * not hold is NULL, as gBASIC's field is unknown. */
static GsqlHv *gsql_h_extract(GsqlHv *const *args, size_t n, GsqlError *err) {
    if (n != 2 || args[0]->v.kind != VALUE_STRING) {
        gsql_host_fail(err, GSQL_MISUSE, "extract reached the host without its part");
        return NULL;
    }
    const char *part = args[0]->v.as.string;
    const Value *v = &args[1]->v;
    if (v->kind != VALUE_DATETIME) {
        char msg[200];
        snprintf(msg, sizeof msg, "extract reads a date, a time or a datetime, and was given %s",
                 value_kind_name(v->kind));
        gsql_host_fail(err, GSQL_EXPRESSION, "%s", msg);
        return NULL;
    }
    long r;
    if (strcmp(part, "quarter") == 0) {
        long m = gsql_dt_field(v, "month", err);
        r = m < 0 ? m : (m - 1) / 3 + 1;
    } else if (strcmp(part, "week") == 0) {
        long y = gsql_dt_field(v, "year", err);
        long doy = y < 0 ? y : gsql_dt_field(v, "day_of_year", err);
        long wd = doy < 0 ? doy : gsql_dt_field(v, "weekday", err);
        if (wd < 0) {
            r = wd;
        } else {
            r = (doy - wd + 10) / 7;
            if (r < 1) {
                r = gsql_iso_weeks(y - 1);
            } else if (r > gsql_iso_weeks(y)) {
                r = 1;
            }
        }
    } else {
        r = gsql_dt_field(v, part, err);
    }
    if (r == -2) {
        return NULL;
    }
    return gsql_hv(r < 0 ? value_unknown() : value_number_exact(r));
}

static GsqlHv *gsql_h_call(void *ctx, const char *name, GsqlHv *const *args, size_t n, GsqlError *err) {
    (void)ctx;
    if (strcmp(name, "substring") == 0) {
        return gsql_h_substring(args, n, err);
    }
    if (strcmp(name, "extract") == 0) {
        return gsql_h_extract(args, n, err);
    }
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
        if (v->v.kind == VALUE_MONEY) {
            /* a cast moves data, so money keeps every digit it holds, where
               string() shows the currency's precision (gdash S1) */
            char buf[64];
            money_render_exact(v->v.as.money, buf, sizeof buf);
            return gsql_hv(value_string(buf));
        }
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
static void gsql_module_raise_about(const char *verb, const char *code, const char *message, long position,
                                    const char *sql, Value *reply);

static void gsql_module_raise(const char *verb, const char *code, const char *message, long position,
                              const char *sql) {
    gsql_module_raise_about(verb, code, message, position, sql, NULL);
}

static Value *gsql_field(Value *record, const char *name);

/* ...with the owner's reply, whose `table`, `column` and `constraint` go into
 * `details` beside `code` (docs/gsql_error_details_design.md). */
static void gsql_module_raise_about(const char *verb, const char *code, const char *message, long position,
                                    const char *sql, Value *reply) {
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
    static const char *about[] = { "table", "column", "constraint" };
    for (size_t k = 0; reply && k < 3; k++) {
        Value *f = gsql_field(reply, about[k]);
        if (f && f->kind == VALUE_STRING) {
            record_set(&details, about[k], value_copy(*f));
        }
    }
    /* gsql.insert's: which of the caller's rows, as its own index */
    Value *index = reply ? gsql_field(reply, "index") : NULL;
    if (index && index->kind == VALUE_NUMBER) {
        record_set(&details, "index", value_copy(*index));
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

/* ---- a connection's attributes (docs/gsql_recursive_design.md §3) --------
 *
 * `bank.max_recursion = 2000`: WITH RECURSIVE's round limit for every
 * statement run on the connection, sent with each request. The only
 * attribute a connection has; a name it lacks is refused naming it. */

static void gsql_attr_refuse(const char *name, int set) {
    char msg[300];
    snprintf(msg, sizeof msg, "a database has no attribute `%s` to %s: its one attribute is max_recursion, the rounds a "
             "WITH RECURSIVE query may run (and its verbs are called, as db.close())", name, set ? "set" : "read");
    runtime_error_raise(msg, 1003, set ? "assignment" : "field access");
}

static int gsql_connection_attr_get(const GsqlConnectionValue *c, const char *name, Value *out) {
    if (strcmp(name, "max_recursion") != 0) {
        gsql_attr_refuse(name, 0);
        return 0;
    }
    *out = value_number_exact((long long)(c->max_recursion ? c->max_recursion : GSQL_MAX_RECURSION_DEFAULT));
    return 1;
}

static int gsql_connection_attr_set(GsqlConnectionValue *c, const char *name, Value v) {
    /* refused, `v` stays the caller's, which frees it (assign_lvalue's
       contract: only a change takes the value) */
    if (strcmp(name, "max_recursion") != 0) {
        gsql_attr_refuse(name, 1);
        return 0;
    }
    if (v.kind == VALUE_UNKNOWN) {
        c->max_recursion = 0;   /* back to the default */
        value_free(v);
        return 1;
    }
    int whole = v.kind == VALUE_NUMBER &&
                (v.as.num.is_exact ? v.as.num.exact >= 1
                                   : v.as.num.value >= 1 && v.as.num.value <= 9007199254740992.0 &&
                                         v.as.num.value == (double)(long long)v.as.num.value);
    if (!whole) {
        char phrase[48], msg[200];
        snprintf(msg, sizeof msg, "max_recursion takes a whole number of rounds, one or more (or unknown, for the "
                 "default of %d), not %s", GSQL_MAX_RECURSION_DEFAULT, kind_phrase(v.kind, phrase, sizeof phrase));
        runtime_error_raise(msg, 1003, "assignment");
        return 0;
    }
    c->max_recursion = v.as.num.is_exact ? (uint64_t)v.as.num.exact : (uint64_t)v.as.num.value;
    value_free(v);
    return 1;
}

/* One request and its reply. 1 with *reply (a record) and *type; 0 with a
 * raise, the owner's own ERROR included. */
static void gsql_cursor_close_quiet(GsqlConnectionValue *c, Value cursor);

/* As gsql_request, but an ERROR from the owner is handed back in *refused,
 * not raised, so a caller can say it in its own terms (gsql.insert names the
 * caller's row). Everything else still raises. */
static int gsql_request_ex(GsqlConnectionValue *c, unsigned type, Value body, const char *verb, const char *sql,
                           Value *reply, unsigned *reply_type, Value *refused);

static int gsql_request(GsqlConnectionValue *c, unsigned type, Value body, const char *verb, const char *sql,
                        Value *reply, unsigned *reply_type) {
    return gsql_request_ex(c, type, body, verb, sql, reply, reply_type, NULL);
}

static int gsql_request_ex(GsqlConnectionValue *c, unsigned type, Value body, const char *verb, const char *sql,
                           Value *reply, unsigned *reply_type, Value *refused) {
    *reply = value_null();
    if (type == GSQL_M_QUERY && c->max_recursion) {
        /* the connection's max_recursion, on each statement it runs
           (docs/gsql_recursive_design.md §3) */
        record_set(&body, "max_recursion", value_number_exact((int64_t)c->max_recursion));
    }
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
    if (f.type == GSQL_M_ERROR && refused) {
        *refused = v;
        return 0;
    }
    if (f.type == GSQL_M_ERROR) {
        Value *code = gsql_field(&v, "code");
        Value *message = gsql_field(&v, "message");
        Value *position = gsql_field(&v, "position");
        gsql_module_raise_about(verb, code && code->kind == VALUE_STRING ? code->as.string : "?",
                                message && message->kind == VALUE_STRING ? message->as.string : "(no message)",
                                position && position->kind == VALUE_NUMBER ? (long)position->as.num.value : 0, sql,
                                &v);
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
    long linger_ms = -1;
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
        const char *bad = NULL;
        for (size_t i = 0; i < o.as.record.count && !bad; i++) {
            Value *v = fields[i].value;
            if (strcmp(fields[i].name, "create") == 0) {
                if (v->kind != VALUE_BOOL) {
                    bad = "gsql.connect: `create` must be true or false";
                } else {
                    create = v->as.boolean;
                }
            } else if (strcmp(fields[i].name, "linger") == 0) {
                double sec = v->kind == VALUE_NUMBER ? v->as.num.value : -1;
                if (v->kind != VALUE_NUMBER || !(sec >= 0 && sec <= 86400)) {
                    bad = "gsql.connect: `linger` is the seconds an owner it starts waits after its last "
                          "connection closes, from 0 to 86400";
                } else {
                    linger_ms = (long)(sec * 1000 + 0.5);
                }
            } else {
                char msg[300];
                snprintf(msg, sizeof msg, "gsql.connect has no option `%s`; its options are `create` and `linger`",
                         fields[i].name);
                value_free(o);
                free(path);
                gsql_module_refuse(msg);
                return value_null();
            }
        }
        value_free(o);
        if (bad) {
            free(path);
            gsql_module_refuse(bad);
            return value_null();
        }
    }
    GsqlConnectionValue *c = calloc(1, sizeof *c);
    if (!c) {
        abort();
    }
    c->ref_count = 1;
    c->path = path;
    c->create = create;
    c->linger_ms = linger_ms;
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
    if (c->role) {
        char msg[400];
        snprintf(msg, sizeof msg, "gsql.%s: `%s` is a database chosen at run time (`database %s()`, with no file), so "
                 "there is nothing to connect to; give its blocks a connection -- gsql.exec(connection, block), or "
                 "gsql.on(connection, block) for a loop", verb, c->role, c->role);
        gsql_module_refuse(msg);
        return 0;
    }
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
        if (c->linger_ms >= 0) {
            snprintf(c->linger_arg, sizeof c->linger_arg, "--linger=%ld", c->linger_ms);
            c->owner_argv[2] = c->linger_arg;
            c->owner_argv[3] = NULL;
        }
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
/* `text`, the second argument already evaluated, is consumed. */
static int gsql_statement_body_of(AstExpr *expr, const char *verb, Value text, char **sql, Value *body) {
    size_t argc = expr->as.call.args.count;
    if (text.kind != VALUE_STRING) {
        char msg[200];
        snprintf(msg, sizeof msg, "gsql.%s expects the statement as text or an sql block, not %s", verb,
                 value_kind_name(text.kind));
        value_free(text);
        gsql_module_refuse(msg);
        return 0;
    }
    if (memchr(text.as.string, 0, string_length(text.as.string))) {
        char msg[200];
        snprintf(msg, sizeof msg, "gsql.%s: the statement holds a NUL byte", verb);
        value_free(text);
        gsql_module_refuse(msg);
        return 0;
    }
    *sql = copy_string(text.as.string);
    value_free(text);
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

/* Whether a statement answers rows: a select, an explain, a backup or dump's
 * progress. Unparsable text answers no; its refusal comes from the owner. */
static int gsql_statement_returns_rows(const char *sql) {
    SqlParsed parsed;
    GsqlError err;
    memset(&err, 0, sizeof err);
    if (sql_parse(sql, strlen(sql), &parsed, &err) != 0) {
        return 0;
    }
    SqlStmt *x = parsed.stmt;
    int rows = x->kind == SQ_SELECT || x->explain || x->kind == SQ_BACKUP || x->kind == SQ_DUMP;
    sql_parsed_free(&parsed);
    return rows;
}

/* A block's statements, split at `;` by G-SQL's own lexer, so a `;` in a
 * string or a comment ends nothing; its :name parameters, every statement's,
 * in order of first appearance (docs/gsql_multi_statement_design.md). */
static void gsql_block_split(GsqlStatementValue *st) {
    const char *t = st->sql;
    size_t n = strlen(t), at = 0;
    st->stmts = NULL;
    st->nstmts = 0;
    st->params = NULL;
    st->nparams = 0;
    while (at < n) {
        size_t end = 0;
        int empty = 1;
        int r = sql_statement_end(t + at, n - at, &end, &empty);
        if (r < 0) {
            end = n - at;   /* a lexing failure: the rest, whole */
            empty = 0;
        }
        size_t len = r == 1 ? end - 1 : end;   /* without its `;` */
        if (!empty || r < 0) {
            char *piece = malloc(len + 1);
            if (!piece) {
                abort();
            }
            memcpy(piece, t + at, len);
            piece[len] = 0;
            st->stmts = realloc(st->stmts, (st->nstmts + 1) * sizeof *st->stmts);
            st->stmts[st->nstmts++] = piece;
            size_t np = 0;
            char **ps = gsql_statement_params(piece, &np);
            for (size_t k = 0; k < np; k++) {
                int seen = 0;
                for (size_t m = 0; m < st->nparams && !seen; m++) {
                    seen = strcmp(st->params[m], ps[k]) == 0;
                }
                if (seen) {
                    free(ps[k]);
                } else {
                    st->params = realloc(st->params, (st->nparams + 1) * sizeof *st->params);
                    st->params[st->nparams++] = ps[k];
                }
            }
            free(ps);
        }
        if (r != 1 || end == 0) {
            break;
        }
        at += end;
    }
    if (!st->nstmts) {
        st->stmts = malloc(sizeof *st->stmts);
        st->stmts[0] = copy_string(st->sql);
        st->nstmts = 1;
    }
    st->last_rows = gsql_statement_returns_rows(st->stmts[st->nstmts - 1]);
}

/* The connection a block is on: its database, by name, in the global frame,
 * where the `database` block bound it. Borrowed, or NULL with a raise. */
static GsqlConnectionValue *gsql_block_connection(const GsqlStatementValue *st, const char *who) {
    if (st->bound.kind == VALUE_GSQL_CONNECTION) {
        return gsql_connection_of(value_copy(st->bound), who);
    }
    Symbol *sym = NULL;
    const char *dot = strchr(st->database, '.');
    if (dot) {
        /* `sql mine on store.shop`: a program's block on a LIBRARY's
         * database (gap G6 from the gBASIC books), found in that library's
         * frame, where its `database` block is bound */
        char lib[200];
        snprintf(lib, sizeof lib, "%.*s", (int)(dot - st->database), st->database);
        Env *frame = library_frame_find(lib);
        if (!frame) {
            char msg[400];
            snprintf(msg, sizeof msg, "it is on `%s`, and no library `%s` is loaded", st->database, lib);
            gsql_module_raise(who, "STATE", msg, 0, st->sql);
            return NULL;
        }
        sym = env_find_in_frame(frame, dot + 1);
    } else {
        sym = env_find_in_frame(st->home ? st->home : &global_env, st->database);
    }
    if (!sym || sym->value.kind != VALUE_GSQL_CONNECTION) {
        char msg[300];
        snprintf(msg, sizeof msg, "it is on `%s`, which %s", st->database,
                 sym ? "no longer holds a database -- something assigned over the `database` block's name"
                     : "is not declared");
        gsql_module_raise(who, "STATE", msg, 0, st->sql);
        return NULL;
    }
    if (sym->value.as.gsql_connection->role) {
        char msg[400];
        snprintf(msg, sizeof msg, "it is on `%s`, a database chosen at run time, so it needs a connection: "
                 "gsql.exec(connection, %s), or gsql.on(connection, %s) for a loop", st->database, st->name, st->name);
        gsql_module_raise(who, "STATE", msg, 0, st->sql);
        return NULL;
    }
    return gsql_connection_of(value_copy(sym->value), who);
}

/* The request for one of a block's statements: its text, and each :name
 * parameter it reads, from the variable of that name where the block runs.
 * 1, or 0 with a raise. */
/* The record a call gave a block, for the length of that call: its fields
 * supply :name parameters first, and a name it lacks is read from the
 * variable in scope (Matthew's ruling, docs/gsql_module_design.md §7.2).
 * NULL when the call gave none. */
static Value *g_block_with;

static int gsql_piece_body(const GsqlStatementValue *st, size_t piece, const char *who, Value *body) {
    const char *text = st->stmts[piece];
    size_t np = 0;
    char **names = st->nstmts == 1 ? st->params : gsql_statement_params(text, &np);
    if (st->nstmts == 1) {
        np = st->nparams;
    }
    *body = value_record(NULL, 0);
    record_set(body, "statement", value_string(text));
    int ok = 1;
    if (np) {
        Value params = value_record(NULL, 0);
        for (size_t i = 0; i < np && ok; i++) {
            RecordField *given = g_block_with ? record_find(g_block_with, names[i]) : NULL;
            if (given) {
                record_set(&params, names[i], value_copy(*given->value));
                continue;
            }
            Symbol *sym = env_find(names[i]);
            if (!sym) {
                char msg[300];
                snprintf(msg, sizeof msg, "the statement reads :%s, and there is no variable `%s` here%s", names[i],
                         names[i], g_block_with ? ", nor a field of that name in the record it was given" : "");
                value_free(params);
                value_free(*body);
                gsql_module_raise(who, "PARAMETER", msg, 0, text);
                ok = 0;
            } else {
                record_set(&params, names[i], value_copy(sym->value));
            }
        }
        if (ok) {
            record_set(body, "parameters", params);
        }
    }
    if (st->nstmts != 1) {
        for (size_t i = 0; i < np; i++) {
            free(names[i]);
        }
        free(names);
    }
    if (ok) {
        record_set(body, "batch", value_number_exact(GSQL_MODULE_BATCH));
    }
    return ok;
}

/* The request for a block's last statement, the one that answers. */
static int gsql_block_body(const GsqlStatementValue *st, const char *who, Value *body) {
    return gsql_piece_body(st, st->nstmts - 1, who, body);
}

/* ---- a block of several statements (docs/gsql_multi_statement_design.md) -- */

typedef struct {
    int multi;          /* more than one statement */
    int own;            /* the block began its transaction, and ends it */
    int rows;           /* the last statement answers rows */
    Value statements;   /* one { command, rows_affected } each, so far */
} GsqlBlockRun;

/* One request whose refusal is an answer rather than a raise: the reply's
 * type, and on an ERROR its code. 0 when the call itself failed (raised). */
static int gsql_raw(GsqlConnectionValue *c, unsigned type, Value body, const char *who, unsigned *reply_type,
                    char *code, size_t code_n) {
    code[0] = 0;
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
    gsql_trace(type);
    int rc = gsql_client_call(c->client, type, &b, &f, &err);
    free(sb.bytes);
    if (rc) {
        if (!error_action_pending()) {
            gsql_module_raise(who, gsql_error_name(err.code), err.message, 0, NULL);
        }
        return 0;
    }
    *reply_type = f.type;
    if (f.type == GSQL_M_ERROR) {
        SerReader r = { (const char *)f.body, f.length, 0, 1, SER_VERSION };
        Value v = deserialize_value(&r, 0);
        Value *cv = v.kind == VALUE_RECORD ? gsql_field(&v, "code") : NULL;
        snprintf(code, code_n, "%s", cv && cv->kind == VALUE_STRING ? cv->as.string : "?");
        value_free(v);
    }
    return 1;
}

/* A block's own transaction ends: committed, or rolled back -- the second
 * also while a raise is pending, which is when it is needed. */
static void gsql_block_rollback(GsqlConnectionValue *c, GsqlBlockRun *run, const char *who) {
    if (run->own) {
        unsigned t;
        char code[64];
        gsql_raw(c, GSQL_M_ROLLBACK, value_record(NULL, 0), who, &t, code, sizeof code);
        run->own = 0;
    }
}
static int gsql_block_commit(GsqlConnectionValue *c, GsqlBlockRun *run, const char *who) {
    if (!run->own) {
        return 1;
    }
    run->own = 0;
    Value reply;
    unsigned type;
    if (!gsql_request(c, GSQL_M_COMMIT, value_record(NULL, 0), who, NULL, &reply, &type)) {
        return 0;
    }
    value_free(reply);
    return 1;
}

static Value gsql_count_of(const char *sql, Value *reply) {
    Value *n = gsql_field(reply, "rows");
    char *command = sql_command_name(sql);
    Value result = value_record(NULL, 0);
    record_set(&result, "command", value_string(command));
    /* a count only where one means something, the rule sqlite, pg and odbc
       share: a CREATE has no rows to have affected */
    record_set(&result, "rows_affected", sql_command_counts_rows(command) && n ? value_copy(*n) : value_null());
    free(command);
    return result;
}

/* Every statement but the last, in a transaction the block owns or joins.
 * `answers_rows`: the call wants the last statement's rows (query, a loop),
 * else its count (exec); refused before anything runs if the block's last
 * statement is the other kind. 1, or 0 with a raise and nothing kept. */
static int gsql_block_prelude(GsqlConnectionValue *c, const GsqlStatementValue *st, const char *who,
                              int answers_rows, GsqlBlockRun *run) {
    run->multi = st->nstmts > 1;
    run->own = 0;
    run->statements = value_array(NULL, 0);
    if (!run->multi) {
        return 1;
    }
    if (answers_rows && !st->last_rows) {
        char msg[300];
        snprintf(msg, sizeof msg, "its last statement returns no rows, so there is nothing to iterate; nothing ran -- "
                 "gsql.exec(%s)", st->name);
        gsql_module_raise(who, "STATE", msg, 0, st->stmts[st->nstmts - 1]);
        return 0;
    }
    /* a transaction already open is the program's: the block joins it */
    Value begin = value_record(NULL, 0);
    record_set(&begin, "mode", value_string("write"));
    unsigned type;
    char code[64];
    if (!gsql_raw(c, GSQL_M_BEGIN, begin, who, &type, code, sizeof code)) {
        return 0;
    }
    if (type == GSQL_M_ERROR && strcmp(code, "STATE") != 0) {
        char msg[200];
        snprintf(msg, sizeof msg, "the block's transaction could not begin (%s)", code);
        gsql_module_raise(who, code, msg, 0, NULL);
        return 0;
    }
    run->own = type != GSQL_M_ERROR;
    for (size_t i = 0; i + 1 < st->nstmts; i++) {
        Value body, reply;
        if (!gsql_piece_body(st, i, who, &body) ||
            !gsql_request(c, GSQL_M_QUERY, body, who, st->stmts[i], &reply, &type)) {
            gsql_block_rollback(c, run, who);
            return 0;
        }
        Value one = gsql_count_of(st->stmts[i], &reply);
        value_free(reply);
        Value grown = append_to_array_ref(&run->statements, one, 0);
        value_free(grown);
    }
    return 1;
}

/* A block the call was given: the connection it runs on, and what runs before
 * its last statement. The last statement's request is in `body`. */
static int gsql_block_start(const GsqlStatementValue *st, GsqlConnectionValue *on, const char *who, int answers_rows,
                            GsqlConnectionValue **c, char **sql, Value *body, GsqlBlockRun *run) {
    run->statements = value_null();
    run->own = run->multi = 0;
    /* exec takes whatever the block ends in (docs/gsql_store_conversion_design.md §2) */
    if (answers_rows < 0) {
        answers_rows = st->last_rows;
    }
    run->rows = answers_rows;
    *c = on ? on : gsql_block_connection(st, who);
    if (!*c) {
        return 0;
    }
    if (!gsql_block_prelude(*c, st, who, answers_rows, run)) {
        value_free(run->statements);
        run->statements = value_null();
        return 0;
    }
    if (!gsql_block_body(st, who, body)) {
        gsql_block_rollback(*c, run, who);
        value_free(run->statements);
        run->statements = value_null();
        return 0;
    }
    *sql = copy_string(st->stmts[st->nstmts - 1]);
    /* a query's changes are kept before its rows are read: the select
       changes nothing, and its rows stream after (§2) */
    if (answers_rows && !gsql_block_commit(*c, run, who)) {
        value_free(*body);
        free(*sql);
        value_free(run->statements);
        run->statements = value_null();
        return 0;
    }
    return 1;
}

/* `for each row in block`: the block's rows, or NULL with a raise. */
static struct GsqlRows *gsql_block_rows(const GsqlStatementValue *st) {
    char who[160];
    snprintf(who, sizeof who, "sql %s", st->name);
    GsqlConnectionValue *c;
    char *sql;
    Value body;
    GsqlBlockRun run;
    if (!gsql_block_start(st, NULL, who, 1, &c, &sql, &body, &run)) {
        return NULL;
    }
    value_free(run.statements);
    char remedy[200];
    snprintf(remedy, sizeof remedy, "gsql.exec(%s)", st->name);
    return gsql_rows_begin(c, who, sql, body, remedy);
}

/* A block's record of :name values, the argument at `index`: 1 with it, or 0
 * with a refusal naming what it was. */
static int gsql_with_arg(AstExpr *expr, size_t index, const char *verb, const char *block, Value *with) {
    *with = eval_expr(expr->as.call.args.items[index]);
    if (error_action_pending()) {
        value_free(*with);
        *with = value_null();
        return 0;
    }
    if (with->kind != VALUE_RECORD) {
        char msg[300];
        snprintf(msg, sizeof msg, "gsql.%s(%s, ...): a block's values come as a record, a field for each :name it "
                 "reads, not %s", verb, block, value_kind_name(with->kind));
        value_free(*with);
        *with = value_null();
        gsql_module_refuse(msg);
        return 0;
    }
    return 1;
}

/* The first argument of query and exec: a connection, then the statement and
 * its parameters; or an sql block alone. 1 with the connection, the statement,
 * the request and the name to refuse under; 0 with a raise. */
static int gsql_call_target(AstExpr *expr, const char *verb, GsqlConnectionValue **c, char **sql, Value *body,
                            char *who, size_t who_cap, GsqlBlockRun *run) {
    size_t argc = expr->as.call.args.count;
    int answers_rows = strcmp(verb, "query") == 0 ? 1 : -1;
    run->statements = value_null();
    run->own = run->multi = run->rows = 0;
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
        Value with = value_null();
        if (argc > 2 || (argc == 2 && !gsql_with_arg(expr, 1, verb, st->name, &with))) {
            if (argc > 2) {
                char msg[300];
                snprintf(msg, sizeof msg, "gsql.%s(%s, record) takes the block and, optionally, a record of its "
                         ":name values", verb, st->name);
                gsql_module_refuse(msg);
            }
            value_free(v);
            return 0;
        }
        snprintf(who, who_cap, "sql %s", st->name);
        g_block_with = argc == 2 ? &with : NULL;
        int ok = gsql_block_start(st, NULL, who, answers_rows, c, sql, body, run);
        g_block_with = NULL;
        value_free(with);
        value_free(v);
        return ok;
    }
    snprintf(who, who_cap, "%s", verb);
    *c = gsql_connection_of(v, verb);
    if (!*c) {
        return 0;
    }
    if (argc < 2 || argc > 3) {
        char msg[200];
        snprintf(msg, sizeof msg, "gsql.%s expects a connection, a statement, and optionally its parameters", verb);
        gsql_module_refuse(msg);
        return 0;
    }
    Value second = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(second);
        return 0;
    }
    /* a block in second place runs on this connection
       (docs/gsql_block_connection_design.md) */
    if (second.kind == VALUE_GSQL_STATEMENT) {
        GsqlStatementValue *st = second.as.gsql_statement;
        snprintf(who, who_cap, "sql %s", st->name);
        Value with = value_null();
        if (argc == 3 && !gsql_with_arg(expr, 2, verb, st->name, &with)) {
            value_free(second);
            return 0;
        }
        g_block_with = argc == 3 ? &with : NULL;
        int ok = gsql_block_start(st, *c, who, answers_rows, c, sql, body, run);
        g_block_with = NULL;
        value_free(with);
        value_free(second);
        return ok;
    }
    int ok = gsql_statement_body_of(expr, verb, second, sql, body);
    run->rows = ok && gsql_statement_returns_rows(*sql);
    return ok;
}

/* gsql.on(connection, block): the block, bound to run on that connection --
 * what `for each` takes, having no argument list to put a connection in */
static Value gsql_eval_on(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        gsql_module_refuse("gsql.on expects a connection and an sql block");
        return value_null();
    }
    Value cv = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(cv);
        return value_null();
    }
    if (cv.kind != VALUE_GSQL_CONNECTION) {
        char msg[200];
        snprintf(msg, sizeof msg, "gsql.on expects a gsql connection first, not %s", value_kind_name(cv.kind));
        value_free(cv);
        gsql_module_refuse(msg);
        return value_null();
    }
    Value bv = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(cv);
        value_free(bv);
        return value_null();
    }
    if (bv.kind != VALUE_GSQL_STATEMENT) {
        char msg[240];
        snprintf(msg, sizeof msg, "gsql.on expects an sql block second, not %s; a SQL string runs on a connection "
                 "directly, gsql.query(connection, text)", value_kind_name(bv.kind));
        value_free(cv);
        value_free(bv);
        gsql_module_refuse(msg);
        return value_null();
    }
    GsqlStatementValue *from = bv.as.gsql_statement;
    GsqlStatementValue *st = calloc(1, sizeof *st);
    if (!st) {
        abort();
    }
    st->ref_count = 1;
    st->name = copy_string(from->name);
    st->home = from->home;
    st->database = copy_string(from->database);
    st->sql = copy_string(from->sql);
    gsql_block_split(st);
    st->bound = cv;   /* the copy holds the connection */
    value_free(bv);
    return value_gsql_statement(st);
}

static Value gsql_eval_query(AstExpr *expr) {
    GsqlConnectionValue *c;
    char *sql;
    Value body;
    char who[160];
    GsqlBlockRun run;
    if (!gsql_call_target(expr, "query", &c, &sql, &body, who, sizeof who, &run)) {
        return value_null();
    }
    value_free(run.statements);   /* a query's changes are already kept */
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
    GsqlBlockRun run;
    if (!gsql_call_target(expr, "exec", &c, &sql, &body, who, sizeof who, &run)) {
        return value_null();
    }
    if (run.rows) {
        /* the last statement returns rows: they are its answer, beside its
           command, as the last statement's value is the block's */
        char *command = sql_command_name(sql);
        char remedy[200];
        snprintf(remedy, sizeof remedy, "gsql.exec");
        struct GsqlRows *r = gsql_rows_begin(c, who, sql, body, remedy);
        if (!r) {
            free(command);
            value_free(run.statements);
            return value_null();
        }
        Value rows = value_array(NULL, 0);
        Value row;
        while (gsql_rows_next(r, &row)) {
            Value grown = append_to_array_ref(&rows, row, 0);
            value_free(grown);
        }
        gsql_rows_end(r);
        if (error_action_pending()) {
            value_free(rows);
            free(command);
            value_free(run.statements);
            return value_null();
        }
        Value result = value_record(NULL, 0);
        record_set(&result, "command", value_string(command));
        record_set(&result, "rows_affected", value_null());
        record_set(&result, "rows", rows);
        if (run.multi) {
            Value one = value_record(NULL, 0);
            record_set(&one, "command", value_string(command));
            record_set(&one, "rows_affected", value_null());
            Value grown = append_to_array_ref(&run.statements, one, 0);
            value_free(grown);
            record_set(&result, "statements", run.statements);
        } else {
            value_free(run.statements);
        }
        free(command);
        return result;
    }
    Value reply;
    unsigned type;
    if (!gsql_request(c, GSQL_M_QUERY, body, who, sql, &reply, &type)) {
        gsql_block_rollback(c, &run, who);
        value_free(run.statements);
        free(sql);
        return value_null();
    }
    if (type == GSQL_M_ROWS) {
        gsql_cursor_close(c, &reply);
        value_free(reply);
        gsql_module_raise(who, "STATE", strcmp(who, "exec") == 0
                          ? "the statement returns rows; use gsql.query for it"
                          : "the statement returns rows; use gsql.query, or for each row in the block", 0, sql);
        gsql_block_rollback(c, &run, who);
        value_free(run.statements);
        free(sql);
        return value_null();
    }
    Value result = gsql_count_of(sql, &reply);
    value_free(reply);
    free(sql);
    if (run.multi) {
        /* every statement's count, the last's too, beside the last's own */
        Value grown = append_to_array_ref(&run.statements, value_copy(result), 0);
        value_free(grown);
        record_set(&result, "statements", run.statements);
        if (!gsql_block_commit(c, &run, who)) {
            value_free(result);
            return value_null();
        }
    } else {
        value_free(run.statements);
    }
    return result;
}

/* --- wait_for_owner: has anyone's owner stopped writing a database? -----------
 *
 * gsql.wait_for_owner(path, seconds) waits up to `seconds` for no owner to
 * hold the database at `path`, and answers whether none does. It is
 * close({ wait })'s question asked by any program, for a database whose
 * owner was started by processes that have since exited (gdash's test
 * cleanup: server workers, refresh children, CLI calls).
 *
 * THE LOCK, NOT THE SOCKET. An owner removes its socket first and releases
 * the store's lock last, after its final write (gsql_owner_design.md §3), so
 * the socket going says only that it stopped taking connections. The probe
 * is a shared, non-blocking flock on the lock file, released at once: it
 * fails while an owner holds its exclusive one. An owner starting in that
 * moment finds the lock busy and retries, which it does anyway for a racing
 * owner. ON THIS MACHINE ONLY: flock is local, so an owner on another
 * machine sharing the storage is not seen (the lease is its answer). A path
 * with no lock file has no owner, and answers true. */
static int gsql_owner_holds(const char *db) {
#ifndef _WIN32
    size_t n = strlen(db);
    char *lock = malloc(n + sizeof "/db.lock");
    if (!lock) {
        abort();
    }
    memcpy(lock, db, n);
    memcpy(lock + n, "/db.lock", sizeof "/db.lock");
    int fd = open(lock, O_RDONLY | O_CLOEXEC);
    free(lock);
    if (fd < 0) {
        return 0;
    }
    int held = flock(fd, LOCK_SH | LOCK_NB) != 0 && errno == EWOULDBLOCK;
    if (!held) {
        flock(fd, LOCK_UN);
    }
    close(fd);
    return held;
#else
    (void)db;
    return 0;
#endif
}

static Value gsql_eval_wait_for_owner(AstExpr *expr) {
    if (expr->as.call.args.count != 2) {
        gsql_module_refuse("gsql.wait_for_owner expects a database path and the seconds to wait");
        return value_null();
    }
    char *path = gsql_text_arg(expr, 0, "wait_for_owner", "the database path");
    if (!path) {
        return value_null();
    }
    Value w = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(w);
        free(path);
        return value_null();
    }
    double wait_s = -1;
    if (w.kind == VALUE_NUMBER) {
        wait_s = w.as.num.value;
    } else if (w.kind == VALUE_DURATION) {
        long long months = 0, seconds = 0;
        duration_totals(w.as.duration, &months, &seconds);
        wait_s = months ? -1 : (double)seconds;
    }
    value_free(w);
    if (!(wait_s >= 0 && wait_s <= 3600)) {
        free(path);
        gsql_module_refuse("gsql.wait_for_owner: the wait is from 0 to 3600 seconds");
        return value_null();
    }
    double waited = 0;
    while (gsql_owner_holds(path) && waited < wait_s) {
        struct timespec ts = { 0, 20 * 1000000L };
        nanosleep(&ts, NULL);
        waited += 0.02;
    }
    int stopped = !gsql_owner_holds(path);
    free(path);
    return value_bool(stopped);
}

/* --- columns: a query described, not run (gdash's A2) --------------------------
 *
 * gsql.columns(db, sql [, parameters]) answers [{ name, kind }], the result
 * columns a query would give, in order, from the plan: no row is read and
 * nothing is changed, so it is safe on any statement and cheap on a large
 * table. `kind` is the SQL layer's (the same word a column was declared
 * with; "any" where an expression could be several). The columns of a table
 * are `db.columns("select * from t")`. A statement that returns no rows has
 * no columns and is refused (STATE). */
static Value gsql_eval_columns(AstExpr *expr) {
    size_t argc = expr->as.call.args.count;
    if (argc < 2 || argc > 3) {
        gsql_module_refuse("gsql.columns expects a connection, a query, and optionally its parameters");
        return value_null();
    }
    GsqlConnectionValue *c = gsql_connection_arg(expr, "columns");
    if (!c) {
        return value_null();
    }
    Value text = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(text);
        return value_null();
    }
    char *sql;
    Value body;
    if (!gsql_statement_body_of(expr, "columns", text, &sql, &body)) {
        return value_null();
    }
    record_set(&body, "describe", value_bool(1));
    Value reply;
    unsigned type;
    if (!gsql_request(c, GSQL_M_QUERY, body, "columns", sql, &reply, &type)) {
        free(sql);
        return value_null();
    }
    free(sql);
    Value *cols = gsql_field(&reply, "columns");
    Value out = cols && cols->kind == VALUE_ARRAY ? value_copy(*cols) : value_array(NULL, 0);
    value_free(reply);
    return out;
}

/* --- insert: many rows in one call (gdash's A3) ---------------------------------
 *
 * gsql.insert(db, table, rows): rows are records keyed by column name, as a
 * frame's rows are. One statement per chunk, `insert into "t" ("a", "b")
 * values ($1, $2), ($3, DEFAULT), ...`, so each value is converted through
 * its column's kind and checked exactly as INSERT does it, and a column a row
 * leaves out takes its default. ALL OR NOTHING: the chunks run in a
 * transaction, or in a savepoint inside the caller's (a nested begin), and a
 * refused row undoes the whole call, naming the caller's own row.
 *
 * Measured before it was written: one row per statement costs about 36 us a
 * row, and 1000 per statement about 14 -- once a statement's parameters were
 * read once, not once per reference (they had been n^2, and past 1024 none). */

static void gsql_simple_quiet(GsqlConnectionValue *c, unsigned type);

#define GSQL_INSERT_PARAMS 4096          /* parameters in one statement */
#define GSQL_INSERT_BYTES (4u << 20)     /* about this much data in one */

static void gsql_insert_put(char **text, size_t *len, size_t *cap, const char *piece) {
    size_t n = strlen(piece);
    if (*len + n + 1 >= *cap) {
        *cap = (*len + n + 1) * 2;
        *text = realloc(*text, *cap);
        if (!*text) {
            abort();
        }
    }
    memcpy(*text + *len, piece, n + 1);
    *len += n;
}

/* a name as a quoted identifier, any `"` in it doubled */
static void gsql_insert_quote(char **text, size_t *len, size_t *cap, const char *name) {
    gsql_insert_put(text, len, cap, "\"");
    for (const char *p = name; *p; p++) {
        char one[3] = { *p, *p == '"' ? '"' : 0, 0 };
        gsql_insert_put(text, len, cap, one);
    }
    gsql_insert_put(text, len, cap, "\"");
}

/* about how many bytes a value takes on the wire, to keep a chunk well under
   the 16 MiB frame */
static size_t gsql_insert_size(const Value *v) {
    if (v->kind == VALUE_STRING) {
        return string_length(v->as.string) + 16;
    }
    if (v->kind == VALUE_ARRAY || v->kind == VALUE_RECORD) {
        return 4096;   /* a column of a compound kind: counted generously */
    }
    return 16;
}

/* The owner refused a chunk whose first row is the caller's rows[first]:
   raised naming the caller's row, when the refusal was of one. */
static void gsql_insert_refused(Value *refused, size_t first) {
    Value *code = gsql_field(refused, "code");
    Value *message = gsql_field(refused, "message");
    Value *row = gsql_field(refused, "row");
    const char *text = message && message->kind == VALUE_STRING ? message->as.string : "(no message)";
    char said[700];
    snprintf(said, sizeof said, "%s", text);
    if (row && row->kind == VALUE_NUMBER && row->as.num.value >= 1) {
        size_t index = first + (size_t)row->as.num.value - 1;
        /* the statement's own "row N of VALUES: " is about a statement the
           caller never wrote */
        const char *of = strncmp(text, "row ", 4) == 0 ? strstr(text, " of VALUES: ") : NULL;
        snprintf(said, sizeof said, "rows[%zu]: %s", index, of ? of + strlen(" of VALUES: ") : text);
        record_set(refused, "index", value_number_exact((int64_t)index));
    }
    gsql_module_raise_about("insert", code && code->kind == VALUE_STRING ? code->as.string : "?", said, 0, NULL,
                            refused);
}

static void gsql_insert_free_args(char **cols, Value *rows, char *table) {
    free(cols);
    value_free(*rows);
    free(table);
}

static Value gsql_eval_insert(AstExpr *expr) {
    if (expr->as.call.args.count != 3) {
        gsql_module_refuse("gsql.insert expects a connection, a table's name and the rows, an array of records");
        return value_null();
    }
    GsqlConnectionValue *c = gsql_connection_arg(expr, "insert");
    if (!c) {
        return value_null();
    }
    char *table = gsql_text_arg(expr, 1, "insert", "the table's name");
    if (!table) {
        return value_null();
    }
    Value rows = eval_expr(expr->as.call.args.items[2]);
    if (error_action_pending()) {
        value_free(rows);
        free(table);
        return value_null();
    }
    char msg[400];
    msg[0] = 0;
    if (rows.kind != VALUE_ARRAY) {
        snprintf(msg, sizeof msg, "gsql.insert expects the rows as an array of records, not %s",
                 value_kind_name(rows.kind));
        gsql_insert_free_args(NULL, &rows, table);
        gsql_module_refuse(msg);
        return value_null();
    }
    size_t nrows = rows.as.array.store->count;
    Value *items = rows.as.array.store->items;
    /* the columns: every name any row gives, in the order first given */
    char **cols = NULL;
    size_t ncols = 0;
    for (size_t r = 0; r < nrows && !msg[0]; r++) {
        if (items[r].kind != VALUE_RECORD) {
            snprintf(msg, sizeof msg, "gsql.insert: rows[%zu] is %s, not a record of column values", r,
                     value_kind_name(items[r].kind));
            break;
        }
        if (items[r].as.record.count == 0) {
            snprintf(msg, sizeof msg, "gsql.insert: rows[%zu] is an empty record, so it names no column", r);
            break;
        }
        RecordField *fields = items[r].as.record.fields;
        for (size_t i = 0; i < items[r].as.record.count; i++) {
            size_t k = 0;
            while (k < ncols && strcmp(cols[k], fields[i].name) != 0) {
                k++;
            }
            if (k == ncols) {
                if (ncols == GSQL_INSERT_PARAMS) {
                    snprintf(msg, sizeof msg, "gsql.insert: the rows name more than %d columns", GSQL_INSERT_PARAMS);
                    break;
                }
                cols = realloc(cols, sizeof *cols * (ncols + 1));
                if (!cols) {
                    abort();
                }
                cols[ncols++] = fields[i].name;
            }
        }
    }
    if (msg[0]) {
        gsql_insert_free_args(cols, &rows, table);
        gsql_module_refuse(msg);
        return value_null();
    }
    Value result = value_record(NULL, 0);
    record_set(&result, "command", value_string("INSERT"));
    if (nrows == 0) {
        record_set(&result, "rows_affected", value_number_exact(0));
        gsql_insert_free_args(cols, &rows, table);
        return result;
    }
    /* the head every chunk shares */
    size_t hcap = 256, hlen = 0;
    char *head = malloc(hcap);
    if (!head) {
        abort();
    }
    head[0] = 0;
    gsql_insert_put(&head, &hlen, &hcap, "insert into ");
    gsql_insert_quote(&head, &hlen, &hcap, table);
    gsql_insert_put(&head, &hlen, &hcap, " (");
    for (size_t k = 0; k < ncols; k++) {
        if (k) {
            gsql_insert_put(&head, &hlen, &hcap, ", ");
        }
        gsql_insert_quote(&head, &hlen, &hcap, cols[k]);
    }
    gsql_insert_put(&head, &hlen, &hcap, ") values ");

    /* a transaction of its own, or a savepoint in the caller's */
    Value reply;
    unsigned type;
    Value begin = value_record(NULL, 0);
    record_set(&begin, "mode", value_string("write"));
    int ok = gsql_request(c, GSQL_M_BEGIN, begin, "insert", NULL, &reply, &type);
    value_free(reply);
    int64_t affected = 0;
    size_t r = 0;
    while (ok && r < nrows) {
        size_t first = r, params = 0, bytes = 0;
        size_t cap = hlen + 64, len = 0;
        char *text = malloc(cap);
        if (!text) {
            abort();
        }
        text[0] = 0;
        gsql_insert_put(&text, &len, &cap, head);
        Value values = value_array(NULL, 0);
        /* at least one row, then rows while both limits hold */
        while (r < nrows && (r == first || (params + ncols <= GSQL_INSERT_PARAMS && bytes < GSQL_INSERT_BYTES))) {
            gsql_insert_put(&text, &len, &cap, r == first ? "(" : ", (");
            for (size_t k = 0; k < ncols; k++) {
                if (k) {
                    gsql_insert_put(&text, &len, &cap, ", ");
                }
                RecordField *f = record_find(&items[r], cols[k]);
                if (!f || !f->value) {
                    gsql_insert_put(&text, &len, &cap, "DEFAULT");
                    continue;
                }
                char mark[24];
                snprintf(mark, sizeof mark, "$%zu", ++params);
                gsql_insert_put(&text, &len, &cap, mark);
                bytes += gsql_insert_size(f->value);
                Value grown = append_to_array_ref(&values, value_copy(*f->value), 0);
                value_free(grown);
            }
            gsql_insert_put(&text, &len, &cap, ")");
            r++;
        }
        Value body = value_record(NULL, 0);
        record_set(&body, "statement", value_string(text));
        record_set(&body, "parameters", values);
        record_set(&body, "batch", value_number_exact(GSQL_MODULE_BATCH));
        free(text);
        Value refused = value_null();
        ok = gsql_request_ex(c, GSQL_M_QUERY, body, "insert", NULL, &reply, &type, &refused);
        if (!ok) {
            /* the savepoint or the transaction undone, then the refusal said */
            gsql_simple_quiet(c, GSQL_M_ROLLBACK);
            if (refused.kind == VALUE_RECORD) {
                gsql_insert_refused(&refused, first);
            }
            value_free(refused);
            break;
        }
        Value *n = gsql_field(&reply, "rows");
        if (n && n->kind == VALUE_NUMBER) {
            affected += (int64_t)n->as.num.value;
        }
        value_free(reply);
    }
    if (ok) {
        ok = gsql_request(c, GSQL_M_COMMIT, value_record(NULL, 0), "insert", NULL, &reply, &type);
        value_free(reply);
    }
    free(head);
    gsql_insert_free_args(cols, &rows, table);
    if (!ok) {
        value_free(result);
        return value_null();
    }
    record_set(&result, "rows_affected", value_number_exact(affected));
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

/* Whether process `pid` is gone. The owner is nobody's child (it is started
 * through an intermediate that exits, gsql_owner_lifetime_design.md §2), so
 * there is no zombie to reap and kill(pid, 0) answers for it. */
static int gsql_owner_gone(pid_t pid) {
#ifndef _WIN32
    return pid <= 0 || (kill(pid, 0) != 0 && errno == ESRCH);
#else
    (void)pid;
    return 1;
#endif
}

/* gsql.close(db [, { wait }]). With `wait` (seconds, or a duration), the
 * close then waits up to that long for the connection's owner process to
 * exit, and answers WHETHER IT DID (gdash's A4): a program deleting a
 * database, or a test checking its scratch directory, must know the owner has
 * stopped writing. The owner stops only when nobody else is using it and its
 * linger has passed, so `false` is an answer, not an error: someone else
 * holds it, or it is lingering, or it is still finishing its last write. */
static Value gsql_eval_close(AstExpr *expr) {
    size_t argc = expr->as.call.args.count;
    if (argc < 1 || argc > 2) {
        gsql_module_refuse("gsql.close expects one connection, and optionally { wait: seconds }");
        return value_null();
    }
    double wait_s = -1;
    if (argc == 2) {
        Value o = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(o);
            return value_null();
        }
        const char *bad = NULL;
        if (o.kind != VALUE_RECORD) {
            bad = "gsql.close: the options are a record, { wait: seconds }";
        }
        for (size_t i = 0; !bad && i < o.as.record.count; i++) {
            RecordField *f = &o.as.record.fields[i];
            if (strcmp(f->name, "wait") != 0) {
                bad = "gsql.close has one option, `wait`";
                continue;
            }
            Value *w = f->value;
            if (w && w->kind == VALUE_NUMBER) {
                wait_s = w->as.num.value;
            } else if (w && w->kind == VALUE_DURATION) {
                long long months = 0, seconds = 0;
                duration_totals(w->as.duration, &months, &seconds);
                wait_s = months ? -1 : (double)seconds;
            }
            if (!(wait_s >= 0 && wait_s <= 3600)) {
                bad = "gsql.close: `wait` is how long to wait for the owner to stop, from 0 to 3600 seconds";
            }
        }
        value_free(o);
        if (bad) {
            gsql_module_refuse(bad);
            return value_null();
        }
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
    pid_t owner = c->client ? gsql_client_owner_pid(c->client) : 0;
    gsql_connection_shut(c);
    if (wait_s < 0) {
        return value_bool(1);
    }
    double waited = 0;
    while (!gsql_owner_gone(owner) && waited < wait_s) {
        struct timespec ts = { 0, 20 * 1000000L };
        nanosleep(&ts, NULL);
        waited += 0.02;
    }
    return value_bool(gsql_owner_gone(owner));
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
    if (c->max_recursion) {
        record_set(&body, "max_recursion", value_number_exact((int64_t)c->max_recursion));
    }
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
    if (strcmp(fn, "insert") == 0) return gsql_eval_insert(expr);
    if (strcmp(fn, "columns") == 0) return gsql_eval_columns(expr);
    if (strcmp(fn, "wait_for_owner") == 0) return gsql_eval_wait_for_owner(expr);
    if (strcmp(fn, "on") == 0) return gsql_eval_on(expr);
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
    GsqlOwnerOptions opt;
    memset(&opt, 0, sizeof opt);
    if (argc == 4 && strncmp(argv[0], "--linger=", 9) == 0) {
        long ms = atol(argv[0] + 9);
        opt.linger_ms = ms <= 0 ? GSQL_LINGER_NONE : (uint32_t)ms;
        argc--;
        argv++;
    }
    if (argc != 3) {
        fprintf(stderr, "gbasic --gsql-owner: wants [--linger=MS] DB FD create|open\n");
        return 2;
    }
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

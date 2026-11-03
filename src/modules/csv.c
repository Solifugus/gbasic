/* The `csv` module (docs/csv_design.md): reading and writing delimited files.
 *
 * Included into eval.c like the other modules. The reading itself is
 * src/csv_reader.c, which knows nothing of values; this file turns its spans
 * into records and owns the options, the problems and the loop.
 *
 * A SOURCE IS A DESCRIPTION, NOT AN OPEN FILE. `csv.records(f)` holds the path
 * and the options, and `for each` opens it, so the loop owns the file as it
 * owns one in `for each line in f`, and every exit closes it. A source kept in
 * a variable and looped over twice reads the file twice. */

#include "gb_csv.h"

#define CSV_ERROR_CODE 5101

struct CsvSourceValue {
    int ref_count;
    char *path;          /* a file, or NULL for text */
    Value text;          /* the text, for csv.parse */
    char delimiter;
    int quote;           /* a byte, or -1 for no quoting */
    int header;
    int deliver;         /* problems: "deliver" */
    Value names;         /* given names: an array of text, or nothing */
};

static void csv_source_retain(CsvSourceValue *source) {
    source->ref_count++;
}

static void csv_source_release(CsvSourceValue *source) {
    if (!source || --source->ref_count > 0) {
        return;
    }
    free(source->path);
    value_free(source->text);
    value_free(source->names);
    free(source);
}

static Value csv_raise(const char *message) {
    runtime_error_raise(message, CSV_ERROR_CODE, "csv");
    return value_null();
}

/* A problem on one record: raised with where it is, so a handler can report
 * the line without reading English. */
static void csv_raise_at(const char *message, uint64_t line, uint64_t offset, uint64_t record) {
    if (raise_in_flight) {
        return;
    }
    Value details = value_record(NULL, 0);
    record_set(&details, "line", value_number_exact((long long)line));
    record_set(&details, "offset", value_number_exact((long long)offset));
    if (record > 0) {
        record_set(&details, "record", value_number_exact((long long)record));
    }
    error_set_state_full(message, CSV_ERROR_CODE, "csv", details, value_null(), 0, 0);
    error_generation++;
    raise_in_flight = 1;
}

/* One byte from an option's text, or a refusal naming it. */
static int csv_option_byte(const char *verb, const char *option, Value v, int allow_empty, int *out) {
    char message[256];
    if (v.kind != VALUE_STRING) {
        snprintf(message, sizeof message, "%s: %s must be text of one character", verb, option);
        csv_raise(message);
        return 0;
    }
    size_t n = string_length(v.as.string);
    if (n == 0 && allow_empty) {
        *out = -1;
        return 1;
    }
    if (n != 1) {
        snprintf(message, sizeof message, "%s: %s must be one character%s", verb, option,
                 allow_empty ? ", or \"\" for none" : "");
        csv_raise(message);
        return 0;
    }
    unsigned char c = (unsigned char)v.as.string[0];
    if (c == '\r' || c == '\n') {
        snprintf(message, sizeof message, "%s: %s cannot be a line break", verb, option);
        csv_raise(message);
        return 0;
    }
    *out = c;
    return 1;
}

typedef struct {
    int delimiter;
    int quote;
    int header;
    int deliver;
    Value names;          /* borrowed from the options record */
    const char *line_end; /* borrowed; writing only */
    size_t line_end_len;
} CsvOptions;

/* The closed option set, read from a record. `writing` admits the writer's
 * options and refuses the reader's, and the other way round. */
static int csv_read_options(const char *verb, Value *options, int writing, CsvOptions *o) {
    o->delimiter = ',';
    o->quote = '"';
    o->header = 1;
    o->deliver = 0;
    o->names = value_null();
    o->line_end = "\n";
    o->line_end_len = 1;
    if (!options) {
        return 1;
    }
    char message[320];
    if (options->kind != VALUE_RECORD) {
        snprintf(message, sizeof message, "%s: options must be a record, such as { delimiter: \"\\t\" }", verb);
        csv_raise(message);
        return 0;
    }
    int header_given = 0;
    for (size_t i = 0; i < options->as.record.count; i++) {
        RecordField *f = &options->as.record.fields[i];
        Value v = *f->value;
        if (strcmp(f->name, "delimiter") == 0) {
            if (!csv_option_byte(verb, "delimiter", v, 0, &o->delimiter)) {
                return 0;
            }
        } else if (strcmp(f->name, "quote") == 0) {
            if (!csv_option_byte(verb, "quote", v, 1, &o->quote)) {
                return 0;
            }
        } else if (strcmp(f->name, "names") == 0) {
            if (v.kind != VALUE_ARRAY || v.as.array.store->count == 0) {
                snprintf(message, sizeof message, "%s: names must be an array of field names", verb);
                csv_raise(message);
                return 0;
            }
            for (size_t k = 0; k < v.as.array.store->count; k++) {
                Value n = v.as.array.store->items[k];
                if (n.kind != VALUE_STRING || string_length(n.as.string) == 0) {
                    snprintf(message, sizeof message, "%s: name %zu is not a field name; a name is non-empty text",
                             verb, k + 1);
                    csv_raise(message);
                    return 0;
                }
                for (size_t j = 0; j < k; j++) {
                    if (string_value_equal(n.as.string, v.as.array.store->items[j].as.string)) {
                        snprintf(message, sizeof message, "%s: names %zu and %zu are both '%.100s'",
                                 verb, j + 1, k + 1, n.as.string);
                        csv_raise(message);
                        return 0;
                    }
                }
            }
            o->names = v;
        } else if (!writing && strcmp(f->name, "header") == 0) {
            if (v.kind != VALUE_BOOL) {
                snprintf(message, sizeof message, "%s: header must be true or false", verb);
                csv_raise(message);
                return 0;
            }
            o->header = v.as.boolean;
            header_given = 1;
        } else if (!writing && strcmp(f->name, "problems") == 0) {
            if (v.kind == VALUE_STRING && strcmp(v.as.string, "raise") == 0) {
                o->deliver = 0;
            } else if (v.kind == VALUE_STRING && strcmp(v.as.string, "deliver") == 0) {
                o->deliver = 1;
            } else {
                snprintf(message, sizeof message, "%s: problems must be \"raise\" or \"deliver\"", verb);
                csv_raise(message);
                return 0;
            }
        } else if (writing && strcmp(f->name, "line_end") == 0) {
            if (v.kind != VALUE_STRING ||
                !(strcmp(v.as.string, "\n") == 0 || strcmp(v.as.string, "\r\n") == 0)) {
                snprintf(message, sizeof message, "%s: line_end must be \"\\n\" or \"\\r\\n\"", verb);
                csv_raise(message);
                return 0;
            }
            o->line_end = v.as.string;
            o->line_end_len = string_length(v.as.string);
        } else {
            snprintf(message, sizeof message, "%s: '%.60s' is not an option; the options are %s", verb,
                     f->name,
                     writing ? "delimiter, quote, names and line_end"
                             : "delimiter, quote, header, names and problems");
            csv_raise(message);
            return 0;
        }
    }
    if (o->quote >= 0 && o->quote == o->delimiter) {
        snprintf(message, sizeof message, "%s: the delimiter and the quote are the same character", verb);
        csv_raise(message);
        return 0;
    }
    if (!writing && o->names.kind != VALUE_NULL) {
        if (header_given && o->header) {
            snprintf(message, sizeof message,
                     "%s: names are for a file with no header line, so they cannot go with header: true",
                     verb);
            csv_raise(message);
            return 0;
        }
        o->header = 0;
    }
    return 1;
}

/* csv.records(f, options) and csv.parse(text, options): a source. */
static Value csv_make_source(AstExpr *expr, int from_text) {
    const char *verb = from_text ? "csv.parse" : "csv.records";
    char message[256];
    size_t argc = expr->as.call.args.count;
    if (argc < 1 || argc > 2) {
        snprintf(message, sizeof message, "%s expects %s and an optional options record", verb,
                 from_text ? "the text" : "a file");
        return csv_raise(message);
    }
    Value src = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(src);
        return value_null();
    }
    Value opts = value_null();
    if (argc == 2) {
        opts = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(src);
            value_free(opts);
            return value_null();
        }
    }
    CsvOptions o;
    if (!csv_read_options(verb, argc == 2 ? &opts : NULL, 0, &o)) {
        value_free(src);
        value_free(opts);
        return value_null();
    }
    CsvSourceValue *s = calloc(1, sizeof *s);
    if (!s) {
        abort();
    }
    s->ref_count = 1;
    s->text = value_null();
    s->names = o.names.kind == VALUE_NULL ? value_null() : value_copy(o.names);
    s->delimiter = (char)o.delimiter;
    s->quote = o.quote;
    s->header = o.header;
    s->deliver = o.deliver;
    value_free(opts);
    if (from_text) {
        if (src.kind != VALUE_STRING) {
            value_free(src);
            csv_source_release(s);
            return csv_raise("csv.parse expects text; for a file, use csv.records(f)");
        }
        s->text = src;
    } else {
        if (src.kind != VALUE_FILE && src.kind != VALUE_STRING) {
            value_free(src);
            csv_source_release(s);
            return csv_raise("csv.records expects a file, such as {file}\"catalog.csv\"; for text "
                             "already in memory, use csv.parse(text)");
        }
        s->path = copy_string(file_target_path(src));
        value_free(src);
    }
    Value v = {0};
    v.kind = VALUE_CSV_SOURCE;
    v.as.csv_source = s;
    return v;
}

/* --- Writing ---------------------------------------------------------------- */

static int csv_put(char **buf, size_t *len, size_t *cap, const char *bytes, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t next = *cap ? *cap : 256;
        while (next < *len + n + 1) {
            next *= 2;
        }
        char *grown = realloc(*buf, next);
        if (!grown) {
            abort();
        }
        *buf = grown;
        *cap = next;
    }
    memcpy(*buf + *len, bytes, n);
    *len += n;
    return 1;
}

/* One field as the writer writes it: text as it is, `unknown` and nothing as
 * an empty field, money with every digit it holds, anything else as string()
 * writes it, quoted only when it
 * needs to be. Answers 0 having raised for a value that is not one field. */
static int csv_put_field(const char *verb, Value v, const CsvOptions *o,
                         char **buf, size_t *len, size_t *cap, const char *where) {
    if (v.kind == VALUE_NULL || v.kind == VALUE_UNKNOWN) {
        return 1;
    }
    if (v.kind == VALUE_ARRAY || v.kind == VALUE_RECORD) {
        char message[256];
        snprintf(message, sizeof message, "%s: %s is %s, which is not one field", verb, where,
                 v.kind == VALUE_ARRAY ? "an array" : "a record");
        csv_raise(message);
        return 0;
    }
    Value text;
    if (v.kind == VALUE_MONEY) {
        /* every digit it holds: a file is data moving, not money being shown,
           and string() would round 1250.7512 to 1250.75 (gdash S1) */
        char buf[64];
        money_render_exact(v.as.money, buf, sizeof buf);
        text = value_string(buf);
    } else {
        text = v.kind == VALUE_STRING ? value_copy(v) : builtin_string_value(value_copy(v));
    }
    if (error_action_pending() || text.kind != VALUE_STRING) {
        value_free(text);
        return 0;
    }
    const char *t = text.as.string;
    size_t n = string_length(t);
    if (gb_csv_needs_quote(t, n, (char)o->delimiter, o->quote)) {
        char q = (char)o->quote;
        if (o->quote < 0) {
            char message[256];
            snprintf(message, sizeof message,
                     "%s: %s holds the delimiter or a line break, and with quote: \"\" it cannot be written",
                     verb, where);
            value_free(text);
            csv_raise(message);
            return 0;
        }
        csv_put(buf, len, cap, &q, 1);
        size_t from = 0;
        for (size_t i = 0; i < n; i++) {
            if (t[i] == q) {
                csv_put(buf, len, cap, t + from, i + 1 - from);
                csv_put(buf, len, cap, &q, 1);
                from = i + 1;
            }
        }
        csv_put(buf, len, cap, t + from, n - from);
        csv_put(buf, len, cap, &q, 1);
    } else {
        csv_put(buf, len, cap, t, n);
    }
    value_free(text);
    return 1;
}

/* One record's text into buf. `names` orders a record's fields when given;
 * otherwise a record is written in its own order. */
static int csv_put_record_fields(const char *verb, Value row, Value names, const CsvOptions *o,
                                 char **buf, size_t *len, size_t *cap, size_t rowno);

/* A record of ONE EMPTY FIELD is written as `""`: unquoted it is an empty
 * line, which every reader skips, so the record would vanish on the way back. */
static int csv_put_record(const char *verb, Value row, Value names, const CsvOptions *o,
                          char **buf, size_t *len, size_t *cap, size_t rowno) {
    size_t start = *len;
    if (!csv_put_record_fields(verb, row, names, o, buf, len, cap, rowno)) {
        return 0;
    }
    if (*len == start) {
        size_t n = row.kind == VALUE_ARRAY ? row.as.array.store->count
                 : row.kind == VALUE_RECORD ? row.as.record.count : 0;
        if (n == 1) {
            if (o->quote < 0) {
                char message[200];
                snprintf(message, sizeof message,
                         "%s: a record of one empty field cannot be written with quote: \"\"; "
                         "it would be an empty line, which a reader skips", verb);
                csv_raise(message);
                return 0;
            }
            char q[2] = { (char)o->quote, (char)o->quote };
            csv_put(buf, len, cap, q, 2);
        }
    }
    return 1;
}

static int csv_put_record_fields(const char *verb, Value row, Value names, const CsvOptions *o,
                                 char **buf, size_t *len, size_t *cap, size_t rowno) {
    char where[96];
    char d = (char)o->delimiter;
    if (row.kind == VALUE_ARRAY) {
        ArrayStorage *a = row.as.array.store;
        for (size_t i = 0; i < a->count; i++) {
            if (i > 0) {
                csv_put(buf, len, cap, &d, 1);
            }
            if (rowno) {
                snprintf(where, sizeof where, "row %zu field %zu", rowno, i + 1);
            } else {
                snprintf(where, sizeof where, "field %zu", i + 1);
            }
            if (!csv_put_field(verb, a->items[i], o, buf, len, cap, where)) {
                return 0;
            }
        }
        return 1;
    }
    if (row.kind != VALUE_RECORD) {
        char message[200];
        if (rowno) {
            snprintf(message, sizeof message, "%s: row %zu is not an array or a record", verb, rowno);
        } else {
            snprintf(message, sizeof message, "%s expects an array or a record of fields", verb);
        }
        csv_raise(message);
        return 0;
    }
    if (names.kind == VALUE_ARRAY) {
        ArrayStorage *a = names.as.array.store;
        if (row.as.record.count != a->count) {
            char message[300], which[40];
            if (rowno) {
                snprintf(which, sizeof which, "row %zu", rowno);
            } else {
                snprintf(which, sizeof which, "the record");
            }
            snprintf(message, sizeof message,
                     "%s: %s has %zu field%s where the %s %zu; a column that shifts is "
                     "the defect this refuses", verb, which, row.as.record.count,
                     row.as.record.count == 1 ? "" : "s", rowno ? "header has" : "names give", a->count);
            csv_raise(message);
            return 0;
        }
        for (size_t i = 0; i < a->count; i++) {
            if (i > 0) {
                csv_put(buf, len, cap, &d, 1);
            }
            const char *name = a->items[i].as.string;
            RecordField *f = record_find(&row, name);
            if (!f) {
                char message[300];
                if (rowno) {
                    snprintf(message, sizeof message, "%s: row %zu has no field '%.80s', which the header names",
                             verb, rowno, name);
                } else {
                    snprintf(message, sizeof message, "%s: the record has no field '%.80s', which the names give",
                             verb, name);
                }
                csv_raise(message);
                return 0;
            }
            if (rowno) {
                snprintf(where, sizeof where, "row %zu field '%.40s'", rowno, name);
            } else {
                snprintf(where, sizeof where, "field '%.40s'", name);
            }
            if (!csv_put_field(verb, *f->value, o, buf, len, cap, where)) {
                return 0;
            }
        }
        return 1;
    }
    for (size_t i = 0; i < row.as.record.count; i++) {
        if (i > 0) {
            csv_put(buf, len, cap, &d, 1);
        }
        snprintf(where, sizeof where, "field '%.40s'", row.as.record.fields[i].name);
        if (!csv_put_field(verb, *row.as.record.fields[i].value, o, buf, len, cap, where)) {
            return 0;
        }
    }
    return 1;
}

/* csv.line(values, options): one record as text, with its line ending. */
static Value csv_line(AstExpr *expr) {
    size_t argc = expr->as.call.args.count;
    if (argc < 1 || argc > 2) {
        return csv_raise("csv.line expects the fields, as an array or a record, and an optional options record");
    }
    Value row = eval_expr(expr->as.call.args.items[0]);
    Value opts = value_null();
    if (!error_action_pending() && argc == 2) {
        opts = eval_expr(expr->as.call.args.items[1]);
    }
    if (error_action_pending()) {
        value_free(row);
        value_free(opts);
        return value_null();
    }
    CsvOptions o;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    Value out = value_null();
    if (csv_read_options("csv.line", argc == 2 ? &opts : NULL, 1, &o) &&
        csv_put_record("csv.line", row, o.names, &o, &buf, &len, &cap, 0)) {
        csv_put(&buf, &len, &cap, o.line_end, o.line_end_len);
        out = value_string_n(buf, len);
    }
    free(buf);
    value_free(row);
    value_free(opts);
    return out;
}

/* csv.write(f, rows, options): a header and the rows, replacing the file. */
static Value csv_write(AstExpr *expr) {
    size_t argc = expr->as.call.args.count;
    if (argc < 2 || argc > 3) {
        return csv_raise("csv.write expects a file, the rows and an optional options record");
    }
    Value target = eval_expr(expr->as.call.args.items[0]);
    Value rows = value_null();
    Value opts = value_null();
    if (!error_action_pending()) {
        rows = eval_expr(expr->as.call.args.items[1]);
    }
    if (!error_action_pending() && argc == 3) {
        opts = eval_expr(expr->as.call.args.items[2]);
    }
    if (error_action_pending()) {
        value_free(target);
        value_free(rows);
        value_free(opts);
        return value_null();
    }
    CsvOptions o;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    Value names = value_null();
    int ok = csv_read_options("csv.write", argc == 3 ? &opts : NULL, 1, &o);
    const char *path = ok ? file_target_path(target) : NULL;
    if (ok && (target.kind != VALUE_FILE && target.kind != VALUE_STRING)) {
        csv_raise("csv.write expects a file, such as {file}\"out.csv\"");
        ok = 0;
    }
    if (ok && rows.kind != VALUE_ARRAY) {
        csv_raise("csv.write expects the rows as an array of records or arrays");
        ok = 0;
    }
    if (ok) {
        ArrayStorage *a = rows.as.array.store;
        if (o.names.kind == VALUE_ARRAY) {
            names = value_copy(o.names);
        } else if (a->count > 0 && a->items[0].kind == VALUE_RECORD) {
            Value first = a->items[0];
            Value *items = malloc(sizeof(Value) * (first.as.record.count ? first.as.record.count : 1));
            if (!items) {
                abort();
            }
            for (size_t i = 0; i < first.as.record.count; i++) {
                items[i] = value_string(first.as.record.fields[i].name);
            }
            names = value_array(items, first.as.record.count);
        }
        if (names.kind == VALUE_ARRAY) {
            ok = csv_put_record("csv.write", names, value_null(), &o, &buf, &len, &cap, 0);
            if (ok) {
                csv_put(&buf, &len, &cap, o.line_end, o.line_end_len);
            }
        }
        for (size_t i = 0; ok && i < a->count; i++) {
            Value row = a->items[i];
            if (row.kind == VALUE_RECORD && names.kind != VALUE_ARRAY) {
                csv_raise("csv.write: a record row needs a header; the first row is an array, so give names:");
                ok = 0;
                break;
            }
            ok = csv_put_record("csv.write", row, row.kind == VALUE_RECORD ? names : value_null(),
                                &o, &buf, &len, &cap, i + 1);
            if (ok) {
                csv_put(&buf, &len, &cap, o.line_end, o.line_end_len);
            }
        }
    }
    if (ok) {
        FILE *fp = fopen(path, "wb");
        if (!fp || (len && fwrite(buf, 1, len, fp) != len) || fclose(fp) != 0) {
            char message[512];
            snprintf(message, sizeof message, "could not write file: %s (%s)", path, strerror(errno));
            runtime_error_raise(message, 1004, "file operation");
            ok = 0;
        }
    }
    free(buf);
    value_free(names);
    value_free(target);
    value_free(rows);
    value_free(opts);
    return value_null();
}

static Value csv_eval_call(AstExpr *expr) {
    const char *verb = expr->as.call.name;
    if (strcmp(verb, "records") == 0) {
        return csv_make_source(expr, 0);
    }
    if (strcmp(verb, "parse") == 0) {
        return csv_make_source(expr, 1);
    }
    if (strcmp(verb, "line") == 0) {
        return csv_line(expr);
    }
    if (strcmp(verb, "write") == 0) {
        return csv_write(expr);
    }
    char message[200];
    module_unknown_verb(message, sizeof message, "csv", verb);
    return csv_raise(message);
}

/* --- Reading: the loop ---------------------------------------------------------
 *
 * A cursor over a source: the reader, the header's names, and the count of
 * data records so far. Opened by `for each`, closed by every exit from it. */
typedef struct {
    GbCsvReader *reader;
    CsvSourceValue *source;
    char **names;
    size_t *name_lens;
    size_t nnames;
    uint64_t record;
} CsvCursor;

static void csv_cursor_close(CsvCursor *c) {
    gb_csv_close(c->reader);
    for (size_t i = 0; i < c->nnames; i++) {
        free(c->names[i]);
    }
    free(c->names);
    free(c->name_lens);
    memset(c, 0, sizeof *c);
}

static void csv_cursor_add_name(CsvCursor *c, const char *name, size_t n) {
    c->names = realloc(c->names, sizeof(char *) * (c->nnames + 1));
    c->name_lens = realloc(c->name_lens, sizeof(size_t) * (c->nnames + 1));
    if (!c->names || !c->name_lens) {
        abort();
    }
    c->names[c->nnames] = malloc(n + 1);
    if (!c->names[c->nnames]) {
        abort();
    }
    memcpy(c->names[c->nnames], name, n);
    c->names[c->nnames][n] = '\0';
    c->name_lens[c->nnames] = n;
    c->nnames++;
}

static void csv_raise_reader(CsvCursor *c, const char *label) {
    char message[400];
    snprintf(message, sizeof message, "%s: %s", label, gb_csv_error(c->reader));
    runtime_error_raise(message, CSV_ERROR_CODE, "csv");
}

/* Open the source and read its header. Answers 0 having raised. */
static int csv_cursor_open(CsvCursor *c, CsvSourceValue *s) {
    memset(c, 0, sizeof *c);
    c->source = s;
    const char *label = s->path ? "csv.records" : "csv.parse";
    if (s->path) {
        c->reader = gb_csv_open_file(s->path, s->delimiter, s->quote);
        if (!c->reader) {
            char message[512];
            snprintf(message, sizeof message, "could not read file: %s (%s)", s->path, strerror(errno));
            runtime_error_raise(message, 1004, "file operation");
            return 0;
        }
    } else {
        c->reader = gb_csv_open_text(s->text.as.string, string_length(s->text.as.string),
                                     s->delimiter, s->quote);
    }
    if (s->names.kind == VALUE_ARRAY) {
        ArrayStorage *a = s->names.as.array.store;
        for (size_t i = 0; i < a->count; i++) {
            csv_cursor_add_name(c, a->items[i].as.string, string_length(a->items[i].as.string));
        }
        return 1;
    }
    if (!s->header) {
        return 1;
    }
    int got = gb_csv_next(c->reader);
    if (got < 0) {
        csv_raise_reader(c, label);
        csv_cursor_close(c);
        return 0;
    }
    if (got == 0) {
        return 1;              /* an empty file: no header and no records */
    }
    char message[400];
    if (gb_csv_problem(c->reader) != GB_CSV_OK) {
        snprintf(message, sizeof message, "%s: the header on line %llu has text after a closing quote",
                 label, (unsigned long long)gb_csv_line(c->reader));
        csv_raise_at(message, gb_csv_line(c->reader), gb_csv_offset(c->reader), 0);
        csv_cursor_close(c);
        return 0;
    }
    size_t n = gb_csv_field_count(c->reader);
    for (size_t i = 0; i < n; i++) {
        size_t len;
        const char *name = gb_csv_field(c->reader, i, &len);
        if (len == 0) {
            snprintf(message, sizeof message,
                     "%s: column %zu of the header on line %llu has no name; give names: and header: false "
                     "to name the columns yourself", label, i + 1, (unsigned long long)gb_csv_line(c->reader));
            csv_raise_at(message, gb_csv_line(c->reader), gb_csv_offset(c->reader), 0);
            csv_cursor_close(c);
            return 0;
        }
        for (size_t j = 0; j < c->nnames; j++) {
            if (c->name_lens[j] == len && memcmp(c->names[j], name, len) == 0) {
                snprintf(message, sizeof message,
                         "%s: columns %zu and %zu of the header on line %llu are both named '%.80s'",
                         label, j + 1, i + 1, (unsigned long long)gb_csv_line(c->reader), c->names[j]);
                csv_raise_at(message, gb_csv_line(c->reader), gb_csv_offset(c->reader), 0);
                csv_cursor_close(c);
                return 0;
            }
        }
        csv_cursor_add_name(c, name, len);
    }
    return 1;
}

/* The next record as (element, where). Answers 1 with both set, 0 at the end,
 * -1 having raised. */
static int csv_cursor_next(CsvCursor *c, Value *element, Value *where) {
    const char *label = c->source->path ? "csv.records" : "csv.parse";
    int got = gb_csv_next(c->reader);
    if (got < 0) {
        csv_raise_reader(c, label);
        return -1;
    }
    if (got == 0) {
        return 0;
    }
    c->record++;
    GbCsvReader *r = c->reader;
    size_t n = gb_csv_field_count(r);
    char problem[300];
    problem[0] = '\0';
    if (gb_csv_problem(r) == GB_CSV_TEXT_AFTER_QUOTE) {
        snprintf(problem, sizeof problem, "line %llu (record %llu) has text after a closing quote",
                 (unsigned long long)gb_csv_line(r), (unsigned long long)c->record);
    } else if (c->nnames > 0 && n != c->nnames) {
        snprintf(problem, sizeof problem, "line %llu (record %llu) has %zu field%s where the %s %zu",
                 (unsigned long long)gb_csv_line(r), (unsigned long long)c->record, n, n == 1 ? "" : "s",
                 c->source->names.kind == VALUE_ARRAY ? "names give" : "header has", c->nnames);
    }
    if (problem[0] && !c->source->deliver) {
        char message[400];
        snprintf(message, sizeof message, "%s: %s", label, problem);
        csv_raise_at(message, gb_csv_line(r), gb_csv_offset(r), c->record);
        return -1;
    }
    if (c->nnames > 0) {
        *element = value_record(NULL, 0);
        size_t k = n < c->nnames ? n : c->nnames;
        for (size_t i = 0; i < k; i++) {
            size_t len;
            const char *bytes = gb_csv_field(r, i, &len);
            record_set_n(element, c->names[i], c->name_lens[i], value_string_n(bytes, len));
        }
    } else {
        Value *items = malloc(sizeof(Value) * (n ? n : 1));
        if (!items) {
            abort();
        }
        for (size_t i = 0; i < n; i++) {
            size_t len;
            const char *bytes = gb_csv_field(r, i, &len);
            items[i] = value_string_n(bytes, len);
        }
        *element = value_array(items, n);
    }
    *where = value_record(NULL, 0);
    record_set(where, "line", value_number_exact((long long)gb_csv_line(r)));
    record_set(where, "offset", value_number_exact((long long)gb_csv_offset(r)));
    record_set(where, "record", value_number_exact((long long)c->record));
    record_set(where, "ok", value_bool(problem[0] == '\0'));
    record_set(where, "problem", value_string(problem));
    if (problem[0]) {
        Value *items = malloc(sizeof(Value) * (n ? n : 1));
        if (!items) {
            abort();
        }
        for (size_t i = 0; i < n; i++) {
            size_t len;
            const char *bytes = gb_csv_field(r, i, &len);
            items[i] = value_string_n(bytes, len);
        }
        record_set(where, "fields", value_array(items, n));
    }
    return 1;
}

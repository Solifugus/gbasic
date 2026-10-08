#include "diagnostics.h"

#include <stdlib.h>
#include <string.h>

/* Standalone diagnostic sink. No dependency on the lexer, parser, or evaluator,
 * so it is trivially reentrant and unit-testable in isolation. */

static char *diag_strdup(const char *text) {
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *copy = malloc(len + 1);
    if (!copy) {
        abort();
    }
    memcpy(copy, text, len + 1);
    return copy;
}

void gb_diagnostics_init(gb_diagnostics *diags) {
    diags->items = NULL;
    diags->count = 0;
    diags->capacity = 0;
}

void gb_diagnostics_free(gb_diagnostics *diags) {
    for (size_t i = 0; i < diags->count; i++) {
        free(diags->items[i].message);
        free(diags->items[i].path);
    }
    free(diags->items);
    diags->items = NULL;
    diags->count = 0;
    diags->capacity = 0;
}

void gb_diagnostics_add(gb_diagnostics *diags,
                        gb_severity severity,
                        gb_diag_code code,
                        int subcode,
                        const char *path,
                        gb_span span,
                        const char *message) {
    if (diags->count == diags->capacity) {
        size_t next = diags->capacity ? diags->capacity * 2 : 8;
        gb_diag *grown = realloc(diags->items, next * sizeof(gb_diag));
        if (!grown) {
            abort();
        }
        diags->items = grown;
        diags->capacity = next;
    }

    gb_diag *slot = &diags->items[diags->count++];
    slot->severity = severity;
    slot->code = code;
    slot->subcode = subcode;
    slot->path = diag_strdup(path);
    slot->span = span;
    slot->message = diag_strdup(message ? message : "");
}

size_t gb_diagnostics_count(const gb_diagnostics *diags) {
    return diags->count;
}

const gb_diag *gb_diagnostics_at(const gb_diagnostics *diags, size_t index) {
    if (index >= diags->count) {
        return NULL;
    }
    return &diags->items[index];
}

const char *gb_diag_code_str(gb_diag_code code) {
    switch (code) {
    case GB_DIAG_NONE:           return "GB_DIAG_NONE";
    case GB_DIAG_LEX_ERROR:      return "GB_DIAG_LEX_ERROR";
    case GB_DIAG_LEX_DETAIL:     return "GB_DIAG_LEX_DETAIL";
    case GB_DIAG_PARSE_ERROR:    return "GB_DIAG_PARSE_ERROR";
    case GB_DIAG_STRING_LITERAL: return "GB_DIAG_STRING_LITERAL";
    case GB_DIAG_SERVER_BLOCK:   return "GB_DIAG_SERVER_BLOCK";
    case GB_DIAG_RUNTIME_ERROR:  return "GB_DIAG_RUNTIME_ERROR";
    }
    return "GB_DIAG_UNKNOWN";
}

const char *gb_severity_str(gb_severity severity) {
    switch (severity) {
    case GB_SEVERITY_ERROR:   return "error";
    case GB_SEVERITY_WARNING: return "warning";
    case GB_SEVERITY_NOTE:    return "note";
    }
    return "error";
}

/* ---- Reporting --------------------------------------------------------------
 * A single process-global active sink. NULL means "emit immediately to stderr".
 * Not reentrant by design yet — Phase 2/3 relocate it into a context. */
static gb_diagnostics *g_active_sink = NULL;

void gb_set_active_sink(gb_diagnostics *sink) {
    g_active_sink = sink;
}

gb_diagnostics *gb_get_active_sink(void) {
    return g_active_sink;
}

/* Human "kind" word the CLI prints. Kept here (not baked into the code enum) so
 * the machine code stays stable while presentation can evolve. Preserves the
 * legacy quirk that a message-bearing lexer error prints as "runtime error". */
const char *gb_diag_kind_str(gb_diag_code code) {
    switch (code) {
    case GB_DIAG_LEX_ERROR:      return "lexer error";
    case GB_DIAG_PARSE_ERROR:
    case GB_DIAG_SERVER_BLOCK:   return "parse error";
    case GB_DIAG_LEX_DETAIL:
    case GB_DIAG_STRING_LITERAL:
    case GB_DIAG_RUNTIME_ERROR:  return "runtime error";
    case GB_DIAG_NONE:           break;
    }
    return "error";
}

void gb_diag_format(FILE *out, const gb_diag *diag) {
    /* SEVERITY WINS OVER THE CODE'S KIND WORD. The kind word answers "which
     * stage produced this", which is the right question for an error and the
     * wrong one for a warning: the first warning-severity diagnostic ever
     * emitted (an unknown string escape, 2026-09-30) printed as "runtime error"
     * because GB_DIAG_STRING_LITERAL maps there, which is worse than saying
     * nothing.
     *
     * NO EXISTING OUTPUT MOVES: measured, nothing in this tree had ever emitted
     * a non-ERROR severity, so every diagnostic that exists today takes the
     * `kind` branch exactly as before. */
    const char *kind = diag->severity == GB_SEVERITY_ERROR
        ? gb_diag_kind_str(diag->code)
        : gb_severity_str(diag->severity);
    if (diag->path && diag->path[0]) {
        fprintf(out, "%s at %s:%d:%d: %s\n", kind, diag->path,
                diag->span.start_line, diag->span.start_column, diag->message);
    } else {
        fprintf(out, "%s at %d:%d: %s\n", kind,
                diag->span.start_line, diag->span.start_column, diag->message);
    }
}

/* Write `s` as a JSON string literal (with quotes). Control characters below
 * 0x20 become \uXXXX or their short escapes; valid multi-byte UTF-8 passes
 * through unchanged (raw bytes are legal inside a JSON string). */
static void json_escape(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"':  fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\n': fputs("\\n", out);  break;
        case '\r': fputs("\\r", out);  break;
        case '\t': fputs("\\t", out);  break;
        case '\b': fputs("\\b", out);  break;
        case '\f': fputs("\\f", out);  break;
        default:
            if (c < 0x20) {
                fprintf(out, "\\u%04x", c);
            } else {
                fputc(c, out);
            }
        }
    }
    fputc('"', out);
}

void gb_diag_write_json(FILE *out, const gb_diag *diag) {
    fputs("{\"severity\":", out);
    json_escape(out, gb_severity_str(diag->severity));
    fputs(",\"code\":", out);
    json_escape(out, gb_diag_code_str(diag->code));
    fprintf(out, ",\"subcode\":%d", diag->subcode);
    fputs(",\"path\":", out);
    if (diag->path) {
        json_escape(out, diag->path);
    } else {
        fputs("null", out);
    }
    fprintf(out, ",\"start\":{\"line\":%d,\"column\":%d}",
            diag->span.start_line, diag->span.start_column);
    fprintf(out, ",\"end\":{\"line\":%d,\"column\":%d}",
            diag->span.end_line, diag->span.end_column);
    fputs(",\"message\":", out);
    json_escape(out, diag->message ? diag->message : "");
    fputs("}\n", out);
}

void gb_report_to(gb_diagnostics *sink, gb_diag_code code, int subcode,
                  const char *path, gb_span span, const char *message) {
    if (sink) {
        gb_diagnostics_add(sink, GB_SEVERITY_ERROR, code, subcode,
                           path, span, message);
        return;
    }
    /* No sink: format immediately, exactly as the legacy reporters did. The temp
     * record borrows path/message — gb_diag_format only reads them. */
    gb_diag d;
    d.severity = GB_SEVERITY_ERROR;
    d.code = code;
    d.subcode = subcode;
    d.path = (char *)path;
    d.span = span;
    d.message = (char *)message;
    gb_diag_format(stderr, &d);
}

void gb_report(gb_diag_code code, int subcode, const char *path,
               gb_span span, const char *message) {
    gb_report_to(g_active_sink, code, subcode, path, span, message);
}

/* A WARNING, TO THE SINK AND NOWHERE ELSE.
 *
 * Found by tests/run_absence.sh and PRE-EXISTING for every one of the warning
 * codes: `--json-diagnostics` emitted a warning as its plain stderr line, so any
 * program that warned put a NON-JSON line into a JSON stream -- the defect
 * run_parse_exit.sh exists for, one channel along, and `--json-diagnostics` is
 * gBASIC Studio's own consumer. The `gb_diag` struct has carried
 * GB_SEVERITY_WARNING since it was written and nothing routed a runtime warning
 * through it.
 *
 * REPORTS ONLY WHEN A SINK IS SET, and with no stderr fallback, because the
 * caller (`runtime_warn_at`) owns the text form and its format was chosen by
 * measurement -- `warning: <msg> at <path>:<l>:<c> [<code>]`, with the code at
 * the end so the `warning: ` prefix two shell checks grep for survived. Falling
 * back to `gb_diag_format` here would silently rewrite that line into
 * `warning at <path>: <msg>` for every ordinary run.
 *
 * Answers whether it took the diagnostic, so the caller knows not to print. */
int gb_report_warning(int subcode, const char *path, gb_span span,
                      const char *message) {
    if (!g_active_sink) {
        return 0;
    }
    gb_diagnostics_add(g_active_sink, GB_SEVERITY_WARNING, GB_DIAG_RUNTIME_ERROR,
                       subcode, path, span, message);
    return 1;
}

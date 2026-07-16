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
    case GB_DIAG_PARSE_ERROR:    return "parse error";
    case GB_DIAG_LEX_DETAIL:
    case GB_DIAG_STRING_LITERAL:
    case GB_DIAG_RUNTIME_ERROR:  return "runtime error";
    case GB_DIAG_NONE:           break;
    }
    return "error";
}

void gb_diag_format(FILE *out, const gb_diag *diag) {
    const char *kind = gb_diag_kind_str(diag->code);
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

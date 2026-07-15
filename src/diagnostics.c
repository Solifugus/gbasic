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

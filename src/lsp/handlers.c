#include "handlers.h"
#include "rpc.h"
#include "gbasic.h"      /* gb_parse, gb_diagnostics_*, gb_diag_code_str, ast_free_program */

#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (!c) {
        abort();
    }
    memcpy(c, s, n);
    return c;
}

void lsp_server_init(LspServer *s) {
    s->initialized = 0;
    s->shutdown_requested = 0;
    s->encoding = LSP_ENCODING_UTF16; /* LSP default until negotiated */
    s->docs = NULL;
    s->doc_count = 0;
    s->doc_cap = 0;
}

void lsp_server_free(LspServer *s) {
    for (size_t i = 0; i < s->doc_count; i++) {
        free(s->docs[i].uri);
        free(s->docs[i].text);
    }
    free(s->docs);
    s->docs = NULL;
    s->doc_count = 0;
    s->doc_cap = 0;
}

/* ---- document store ---------------------------------------------------- */

static LspDocument *find_doc(LspServer *s, const char *uri) {
    for (size_t i = 0; i < s->doc_count; i++) {
        if (strcmp(s->docs[i].uri, uri) == 0) {
            return &s->docs[i];
        }
    }
    return NULL;
}

static void set_doc(LspServer *s, const char *uri, const char *text) {
    LspDocument *d = find_doc(s, uri);
    if (d) {
        free(d->text);
        d->text = dupstr(text);
        return;
    }
    if (s->doc_count == s->doc_cap) {
        size_t next = s->doc_cap ? s->doc_cap * 2 : 4;
        LspDocument *grown = realloc(s->docs, next * sizeof(LspDocument));
        if (!grown) {
            abort();
        }
        s->docs = grown;
        s->doc_cap = next;
    }
    s->docs[s->doc_count].uri = dupstr(uri);
    s->docs[s->doc_count].text = dupstr(text);
    s->doc_count++;
}

static void remove_doc(LspServer *s, const char *uri) {
    for (size_t i = 0; i < s->doc_count; i++) {
        if (strcmp(s->docs[i].uri, uri) == 0) {
            free(s->docs[i].uri);
            free(s->docs[i].text);
            s->docs[i] = s->docs[--s->doc_count]; /* order does not matter */
            return;
        }
    }
}

/* ---- JSON helpers ------------------------------------------------------ */

static cJSON *make_response(const cJSON *id) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    if (id && !cJSON_IsNull(id)) {
        cJSON_AddItemToObject(r, "id", cJSON_Duplicate(id, 1));
    } else {
        cJSON_AddNullToObject(r, "id");
    }
    return r;
}

static cJSON *position_json(const char *text, int line, int column, lsp_encoding enc) {
    lsp_position p = lsp_position_from_byte(text, line, column, enc);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "line", p.line);
    cJSON_AddNumberToObject(o, "character", p.character);
    return o;
}

static int severity_to_lsp(gb_severity sev) {
    switch (sev) {
    case GB_SEVERITY_ERROR:   return 1;
    case GB_SEVERITY_WARNING: return 2;
    case GB_SEVERITY_NOTE:    return 3; /* LSP Information */
    }
    return 1;
}

/* Send a textDocument/publishDiagnostics notification carrying `arr` (which the
 * notification takes ownership of). */
static void send_publish(const char *uri, cJSON *arr) {
    cJSON *note = cJSON_CreateObject();
    cJSON_AddStringToObject(note, "jsonrpc", "2.0");
    cJSON_AddStringToObject(note, "method", "textDocument/publishDiagnostics");
    cJSON *params = cJSON_AddObjectToObject(note, "params");
    cJSON_AddStringToObject(params, "uri", uri);
    cJSON_AddItemToObject(params, "diagnostics", arr);
    rpc_write_message(note);
    cJSON_Delete(note);
}

/* Parse `text` and publish its diagnostics for `uri`. Because the parser aborts
 * at the first error today (error recovery is a deferred phase), this publishes
 * 0 or 1 diagnostic; the shape is already the final one. */
static void publish_for(LspServer *s, const char *uri, const char *text) {
    gb_diagnostics sink;
    gb_diagnostics_init(&sink);

    AstStmtList program = ast_stmt_list_empty();
    gb_parse(text, uri, &program, &sink);
    ast_free_program(program);

    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < gb_diagnostics_count(&sink); i++) {
        const gb_diag *d = gb_diagnostics_at(&sink, i);
        cJSON *item = cJSON_CreateObject();
        cJSON *range = cJSON_AddObjectToObject(item, "range");
        cJSON_AddItemToObject(range, "start",
            position_json(text, d->span.start_line, d->span.start_column, s->encoding));
        cJSON_AddItemToObject(range, "end",
            position_json(text, d->span.end_line, d->span.end_column, s->encoding));
        cJSON_AddNumberToObject(item, "severity", severity_to_lsp(d->severity));
        cJSON_AddStringToObject(item, "code", gb_diag_code_str(d->code));
        cJSON_AddStringToObject(item, "source", "gbasic");
        cJSON_AddStringToObject(item, "message", d->message ? d->message : "");
        cJSON_AddItemToArray(arr, item);
    }
    gb_diagnostics_free(&sink);

    send_publish(uri, arr);
}

/* ---- request/notification handlers ------------------------------------- */

static void handle_initialize(LspServer *s, const cJSON *id, const cJSON *params) {
    /* Negotiate position encoding: prefer utf-8 (our columns are byte offsets =
     * UTF-8 code units) when the client advertises it; otherwise LSP-default
     * utf-16. */
    s->encoding = LSP_ENCODING_UTF16;
    const cJSON *caps = params ? cJSON_GetObjectItemCaseSensitive(params, "capabilities") : NULL;
    const cJSON *general = caps ? cJSON_GetObjectItemCaseSensitive(caps, "general") : NULL;
    const cJSON *encs = general ? cJSON_GetObjectItemCaseSensitive(general, "positionEncodings") : NULL;
    if (cJSON_IsArray(encs)) {
        const cJSON *e = NULL;
        cJSON_ArrayForEach(e, encs) {
            if (cJSON_IsString(e) && strcmp(e->valuestring, "utf-8") == 0) {
                s->encoding = LSP_ENCODING_UTF8;
                break;
            }
        }
    }
    s->initialized = 1;

    cJSON *resp = make_response(id);
    cJSON *result = cJSON_AddObjectToObject(resp, "result");
    cJSON *capabilities = cJSON_AddObjectToObject(result, "capabilities");
    cJSON_AddNumberToObject(capabilities, "textDocumentSync", 1); /* Full */
    cJSON_AddStringToObject(capabilities, "positionEncoding",
        s->encoding == LSP_ENCODING_UTF8 ? "utf-8" : "utf-16");
    rpc_write_message(resp);
    cJSON_Delete(resp);
}

static void handle_shutdown(LspServer *s, const cJSON *id) {
    s->shutdown_requested = 1;
    cJSON *resp = make_response(id);
    cJSON_AddNullToObject(resp, "result");
    rpc_write_message(resp);
    cJSON_Delete(resp);
}

static const cJSON *td_uri(const cJSON *params) {
    const cJSON *td = params ? cJSON_GetObjectItemCaseSensitive(params, "textDocument") : NULL;
    return td ? cJSON_GetObjectItemCaseSensitive(td, "uri") : NULL;
}

static void handle_did_open(LspServer *s, const cJSON *params) {
    const cJSON *td = params ? cJSON_GetObjectItemCaseSensitive(params, "textDocument") : NULL;
    const cJSON *uri = td ? cJSON_GetObjectItemCaseSensitive(td, "uri") : NULL;
    const cJSON *text = td ? cJSON_GetObjectItemCaseSensitive(td, "text") : NULL;
    if (cJSON_IsString(uri) && cJSON_IsString(text)) {
        set_doc(s, uri->valuestring, text->valuestring);
        publish_for(s, uri->valuestring, text->valuestring);
    }
}

static void handle_did_change(LspServer *s, const cJSON *params) {
    const cJSON *uri = td_uri(params);
    const cJSON *changes = params ? cJSON_GetObjectItemCaseSensitive(params, "contentChanges") : NULL;
    if (!cJSON_IsString(uri) || !cJSON_IsArray(changes)) {
        return;
    }
    /* Full sync: the last change carries the whole new document text. */
    const cJSON *last = NULL, *c = NULL;
    cJSON_ArrayForEach(c, changes) {
        last = c;
    }
    const cJSON *text = last ? cJSON_GetObjectItemCaseSensitive(last, "text") : NULL;
    if (cJSON_IsString(text)) {
        set_doc(s, uri->valuestring, text->valuestring);
        publish_for(s, uri->valuestring, text->valuestring);
    }
}

static void handle_did_close(LspServer *s, const cJSON *params) {
    const cJSON *uri = td_uri(params);
    if (cJSON_IsString(uri)) {
        remove_doc(s, uri->valuestring);
        send_publish(uri->valuestring, cJSON_CreateArray()); /* clear */
    }
}

static void send_method_not_found(const cJSON *id) {
    cJSON *resp = make_response(id);
    cJSON *err = cJSON_AddObjectToObject(resp, "error");
    cJSON_AddNumberToObject(err, "code", -32601);
    cJSON_AddStringToObject(err, "message", "Method not found");
    rpc_write_message(resp);
    cJSON_Delete(resp);
}

int lsp_handle_message(LspServer *s, const cJSON *msg, int *exit_code) {
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(msg, "method");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(msg, "id");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(msg, "params");

    if (!cJSON_IsString(method)) {
        return 1; /* a response or malformed message: nothing to do */
    }
    const char *m = method->valuestring;
    int is_request = (id != NULL) && !cJSON_IsNull(id);

    if (strcmp(m, "initialize") == 0) {
        handle_initialize(s, id, params);
    } else if (strcmp(m, "initialized") == 0) {
        /* notification, no-op */
    } else if (strcmp(m, "shutdown") == 0) {
        handle_shutdown(s, id);
    } else if (strcmp(m, "exit") == 0) {
        *exit_code = s->shutdown_requested ? 0 : 1;
        return 0;
    } else if (strcmp(m, "textDocument/didOpen") == 0) {
        handle_did_open(s, params);
    } else if (strcmp(m, "textDocument/didChange") == 0) {
        handle_did_change(s, params);
    } else if (strcmp(m, "textDocument/didClose") == 0) {
        handle_did_close(s, params);
    } else if (is_request) {
        send_method_not_found(id); /* unknown request */
    }
    /* unknown notification: silently ignored */

    return 1;
}

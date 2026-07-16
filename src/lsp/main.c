/* gbasic-lsp — a diagnostics-only Language Server for gBASIC (PLAN.md Phase L).
 *
 * The first external consumer of libgbasic's reentrant front end. Speaks
 * JSON-RPC 2.0 over stdio (Content-Length framing) and pushes
 * textDocument/publishDiagnostics on every open/change. Single-threaded v1;
 * scope is deliberately small (see PLAN.md). */
#include "rpc.h"
#include "handlers.h"
#include "cJSON.h"

#include <stdlib.h>

/* Reply to an unparseable message body with a JSON-RPC Parse Error (id null). */
static void send_parse_error(void) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    cJSON_AddNullToObject(resp, "id");
    cJSON *err = cJSON_AddObjectToObject(resp, "error");
    cJSON_AddNumberToObject(err, "code", -32700);
    cJSON_AddStringToObject(err, "message", "Parse error");
    rpc_write_message(resp);
    cJSON_Delete(resp);
}

int main(void) {
    LspServer server;
    lsp_server_init(&server);

    int exit_code = 0;
    char *body;
    while ((body = rpc_read_message()) != NULL) {
        cJSON *msg = cJSON_Parse(body);
        free(body);
        if (!msg) {
            send_parse_error();
            continue;
        }
        int keep = lsp_handle_message(&server, msg, &exit_code);
        cJSON_Delete(msg);
        if (!keep) {
            break;
        }
    }

    lsp_server_free(&server);
    return exit_code;
}

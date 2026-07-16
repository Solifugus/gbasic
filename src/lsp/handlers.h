#ifndef GBASIC_LSP_HANDLERS_H
#define GBASIC_LSP_HANDLERS_H

#include "cJSON.h"
#include "lsp_position.h"

#include <stddef.h>

/* An open text document: full text kept in memory (Full-sync only). */
typedef struct {
    char *uri;
    char *text;
} LspDocument;

/* Server state for a single stdio session. Single-threaded v1. */
typedef struct {
    int          initialized;        /* received `initialize` */
    int          shutdown_requested; /* received `shutdown` (governs exit code) */
    lsp_encoding encoding;           /* negotiated at initialize (default UTF-16) */
    LspDocument *docs;
    size_t       doc_count;
    size_t       doc_cap;
} LspServer;

void lsp_server_init(LspServer *s);
void lsp_server_free(LspServer *s);

/* Handle one decoded JSON-RPC message, writing any response/notification to
 * stdout via rpc_write_message. Returns 1 to keep the loop running, or 0 to
 * exit — in which case *exit_code is set (0 after a clean shutdown, else 1). */
int lsp_handle_message(LspServer *s, const cJSON *msg, int *exit_code);

#endif

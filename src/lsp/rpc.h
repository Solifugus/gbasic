#ifndef GBASIC_LSP_RPC_H
#define GBASIC_LSP_RPC_H

#include "cJSON.h"

/* JSON-RPC 2.0 transport over stdio with LSP Content-Length framing.
 *
 * The base protocol frames each message as:
 *     Content-Length: <N>\r\n
 *     \r\n
 *     <N bytes of UTF-8 JSON>
 * (other headers are tolerated and ignored). Reads are binary-safe. */

/* Read one framed message body from stdin. Returns a malloc'd, NUL-terminated
 * string the caller must free, or NULL on clean EOF / an unrecoverable framing
 * error (the caller should then terminate its loop). */
char *rpc_read_message(void);

/* Serialize `msg` compactly, frame it, write it to stdout, and flush. */
void  rpc_write_message(const cJSON *msg);

#endif

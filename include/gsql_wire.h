#ifndef GBASIC_GSQL_WIRE_H
#define GBASIC_GSQL_WIRE_H

/* G-SQL's client protocol on the wire (docs/gsql_owner_design.md §2,
 * docs/gsql_design.md §6.5): frames, their reassembly from a stream, the
 * message types and the error codes. The owner (gsql_owner.h) and the client
 * (gsql_client.h) both speak it through this file, so they cannot disagree
 * about what a frame is.
 *
 * A frame: le32 length of the rest, u8 type, le32 request id, then ONE
 * `serialize` v3 value, a record. */

#include <stddef.h>
#include <stdint.h>

#include "gsql_table.h"   /* GsqlBuf, GsqlError, the table layer's codes */

#define GSQL_PROTO_MAJOR 1
#define GSQL_PROTO_MINOR 0
#define GSQL_FRAME_HEADER 9
#define GSQL_FRAME_MAX (16u << 20)   /* the rest of the frame: type, id, body */

typedef enum {
    GSQL_M_HELLO = 1,
    GSQL_M_WELCOME,
    GSQL_M_REFUSED,
    GSQL_M_QUERY,
    GSQL_M_ROWS,
    GSQL_M_FETCH,
    GSQL_M_CLOSE,
    GSQL_M_PREPARE,
    GSQL_M_PREPARED,
    GSQL_M_BEGIN,
    GSQL_M_COMMIT,
    GSQL_M_ROLLBACK,
    GSQL_M_CANCEL,
    GSQL_M_DONE,
    GSQL_M_PING,
    GSQL_M_PONG,
    GSQL_M_BYE,
    GSQL_M_ERROR,
    GSQL_M_LAST = GSQL_M_ERROR
} GsqlMsg;

const char *gsql_msg_name(int type);

/* The protocol's error codes, continuing the table layer's GsqlCode so that
 * one GsqlError carries either. On the wire every code is its NAME. */
typedef enum {
    GSQL_PROTOCOL = 10, /* a malformed frame or body */
    GSQL_VERSION,       /* a protocol major this side does not speak */
    GSQL_TOO_LARGE,     /* a frame past GSQL_FRAME_MAX */
    GSQL_CANCELLED,     /* a parked request, cancelled */
    GSQL_UNSUPPORTED,   /* no service, or a message the service does not implement */
    GSQL_STATE,         /* COMMIT with nothing begun, BEGIN inside a transaction */
    GSQL_READ_ONLY,     /* a writing statement in BEGIN READ */
    GSQL_FAILED,        /* the transaction failed earlier: ROLLBACK */
    GSQL_TIMEOUT,       /* an idle cursor or transaction, ended */
    GSQL_LOST,          /* client side: the owner exited */
    GSQL_NO_OWNER,      /* client side: no owner could be reached or started */
    GSQL_CODE_LAST = GSQL_NO_OWNER
} GsqlWireCode;

/* Any code's name, the table layer's included; "?" for none. And back:
 * -1 for a name this side does not know. */
const char *gsql_error_name(int code);
int gsql_error_code(const char *name);

/* One frame. `body` is borrowed: valid until the next call on whatever
 * produced it. */
typedef struct {
    unsigned type;
    uint32_t id;
    const unsigned char *body;
    size_t length;
} GsqlFrame;

/* Appends one frame to `out`. */
void gsql_frame_put(GsqlBuf *out, unsigned type, uint32_t id, const void *body, size_t length);

/* Reassembles frames from a stream delivered in pieces of any size. */
typedef struct {
    GsqlBuf in;
    size_t pos;         /* the first byte not yet consumed */
} GsqlReader;

typedef enum {
    GSQL_READ_FRAME,     /* *f is the next frame */
    GSQL_READ_MORE,      /* no whole frame yet */
    GSQL_READ_SHORT,     /* a length under 5: the stream cannot be resynchronised */
    GSQL_READ_TOO_LARGE  /* a length past the limit: likewise */
} GsqlReadStatus;

void gsql_reader_feed(GsqlReader *r, const void *bytes, size_t length);
int gsql_reader_next(GsqlReader *r, GsqlFrame *f);
size_t gsql_reader_pending(const GsqlReader *r);   /* bytes held, not yet a frame */
void gsql_reader_free(GsqlReader *r);

#endif

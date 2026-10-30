/* G-SQL's frames and their reassembly (include/gsql_wire.h). */

#include "gsql_wire.h"
#include "v3.h"

#include <stdlib.h>
#include <string.h>

static const char *const msg_names[] = {
    "?", "HELLO", "WELCOME", "REFUSED", "QUERY", "ROWS", "FETCH", "CLOSE", "PREPARE", "PREPARED",
    "BEGIN", "COMMIT", "ROLLBACK", "CANCEL", "DONE", "PING", "PONG", "BYE", "ERROR"
};

const char *gsql_msg_name(int type) {
    if (type < 1 || type > GSQL_M_LAST) {
        return "?";
    }
    return msg_names[type];
}

static const char *const wire_names[] = {
    "PROTOCOL", "VERSION", "TOO_LARGE", "CANCELLED", "UNSUPPORTED", "STATE", "READ_ONLY",
    "FAILED", "TIMEOUT", "LOST", "NO_OWNER"
};

const char *gsql_error_name(int code) {
    if (code >= GSQL_PROTOCOL && code <= GSQL_CODE_LAST) {
        return wire_names[code - GSQL_PROTOCOL];
    }
    return gsql_code_name(code);
}

int gsql_error_code(const char *name) {
    for (int c = GSQL_OK; c <= GSQL_MISUSE; c++) {
        if (strcmp(gsql_code_name(c), name) == 0) {
            return c;
        }
    }
    for (int c = GSQL_PROTOCOL; c <= GSQL_CODE_LAST; c++) {
        if (strcmp(wire_names[c - GSQL_PROTOCOL], name) == 0) {
            return c;
        }
    }
    return -1;
}

void gsql_frame_put(GsqlBuf *out, unsigned type, uint32_t id, const void *body, size_t length) {
    v3_le(out, (uint64_t)length + 5, 4);
    v3_u8(out, type);
    v3_le(out, id, 4);
    v3_put(out, body, length);
}

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Drops what earlier frames consumed. Only between calls, never while a
 * frame handed out is still in use: a frame is valid until the next call. */
static void compact(GsqlReader *r) {
    if (r->pos == 0) {
        return;
    }
    size_t rest = r->in.length - r->pos;
    if (rest) {
        memmove(r->in.bytes, r->in.bytes + r->pos, rest);
    }
    r->in.length = rest;
    r->pos = 0;
}

void gsql_reader_feed(GsqlReader *r, const void *bytes, size_t length) {
    compact(r);
    v3_put(&r->in, bytes, length);
}

int gsql_reader_next(GsqlReader *r, GsqlFrame *f) {
    compact(r);
    size_t have = r->in.length;
    if (have < 4) {
        return GSQL_READ_MORE;
    }
    uint32_t len = le32(r->in.bytes);
    /* refused on the length alone, before the rest arrives: a client that
     * names a frame of four gigabytes is not waited for */
    if (len < 5) {
        return GSQL_READ_SHORT;
    }
    if (len > GSQL_FRAME_MAX) {
        return GSQL_READ_TOO_LARGE;
    }
    if (have - 4 < len) {
        return GSQL_READ_MORE;
    }
    f->type = r->in.bytes[4];
    f->id = le32(r->in.bytes + 5);
    f->body = r->in.bytes + GSQL_FRAME_HEADER;
    f->length = len - 5;
    r->pos = 4 + (size_t)len;
    return GSQL_READ_FRAME;
}

size_t gsql_reader_pending(const GsqlReader *r) {
    return r->in.length - r->pos;
}

void gsql_reader_free(GsqlReader *r) {
    gsql_buf_free(&r->in);
    r->pos = 0;
}

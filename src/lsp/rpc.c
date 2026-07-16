#include "rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* strncasecmp */

/* Read one header line from stdin into buf (CR stripped, LF-terminated line).
 * Returns 1 if a line was read (possibly empty), 0 on EOF before any byte. */
static int read_header_line(char *buf, size_t cap) {
    size_t n = 0;
    int c;
    int any = 0;
    while ((c = getchar()) != EOF) {
        any = 1;
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue; /* the following '\n' terminates the line */
        }
        if (n + 1 < cap) {
            buf[n++] = (char)c;
        }
    }
    buf[n] = '\0';
    return any;
}

char *rpc_read_message(void) {
    long content_length = -1;
    char line[8192];

    /* Headers, until a blank line. */
    for (;;) {
        if (!read_header_line(line, sizeof line)) {
            return NULL; /* EOF */
        }
        if (line[0] == '\0') {
            break; /* blank line: end of headers */
        }
        if (strncasecmp(line, "content-length:", 15) == 0) {
            content_length = strtol(line + 15, NULL, 10);
        }
        /* Any other header (e.g. Content-Type) is tolerated and ignored. */
    }

    if (content_length < 0) {
        return NULL; /* no Content-Length: unframed / EOF-ish, give up */
    }

    char *body = malloc((size_t)content_length + 1);
    if (!body) {
        abort();
    }
    size_t got = fread(body, 1, (size_t)content_length, stdin);
    if (got != (size_t)content_length) {
        free(body);
        return NULL; /* truncated stream */
    }
    body[content_length] = '\0';
    return body;
}

void rpc_write_message(const cJSON *msg) {
    char *out = cJSON_PrintUnformatted(msg);
    if (!out) {
        return;
    }
    size_t len = strlen(out);
    printf("Content-Length: %zu\r\n\r\n", len);
    fwrite(out, 1, len, stdout);
    fflush(stdout);
    cJSON_free(out);
}

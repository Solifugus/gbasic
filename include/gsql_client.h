#ifndef GBASIC_GSQL_CLIENT_H
#define GBASIC_GSQL_CLIENT_H

/* G-SQL's client side (docs/gsql_owner_design.md §6, §7): reach a database's
 * owner, or start one and hear whether it is ready, then exchange frames.
 *
 * Blocking, and one connection per handle. When the owner disappears, the
 * request in flight fails with GSQL_LOST and NOTHING IS RETRIED: the next
 * send reconnects, starting an owner if there is none, and the program
 * decides whether to repeat what failed (§6.5). */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "gsql_wire.h"

typedef struct {
    /* How to start an owner: argv[0] is the program, NULL-terminated. The
     * client appends three arguments: the database path, the number of the
     * descriptor the readiness line goes to, and "create" or "open". */
    const char *const *owner_argv;
    int create;                 /* the owner may create the database */
    uint32_t wait_ms;           /* how long one attempt waits for an owner; 0: 10 000 */
    const char *name;           /* HELLO's `client`; NULL: "gsql" */
    uint32_t major, minor;      /* HELLO's version; 0 and 0: this side's */
} GsqlClientOptions;

typedef struct GsqlClient GsqlClient;

int gsql_client_open(const char *db, const GsqlClientOptions *opt, GsqlClient **out, GsqlError *err);
void gsql_client_close(GsqlClient *c);   /* sends BYE */

/* One request; *id gets its request id. A lost connection reconnects first. */
int gsql_client_send(GsqlClient *c, unsigned type, const GsqlBuf *body, uint32_t *id, GsqlError *err);
/* The next reply, borrowed until the next call on `c`. */
int gsql_client_recv(GsqlClient *c, GsqlFrame *reply, GsqlError *err);
/* send then recv, checking the reply answers the request. */
int gsql_client_call(GsqlClient *c, unsigned type, const GsqlBuf *body, GsqlFrame *reply, GsqlError *err);

pid_t gsql_client_owner_pid(const GsqlClient *c);   /* from WELCOME */
pid_t gsql_client_started(const GsqlClient *c);     /* the owner this client started, or 0 */
int gsql_client_fd(const GsqlClient *c);            /* -1 while lost */

#endif

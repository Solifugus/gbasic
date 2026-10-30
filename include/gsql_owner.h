#ifndef GBASIC_GSQL_OWNER_H
#define GBASIC_GSQL_OWNER_H

/* G-SQL's owner (docs/gsql_owner_design.md): the one process that opens a
 * database, serving every client over a local socket (docs/gsql_design.md
 * §6.1, §6.5). Single-threaded: one poll over the listening socket and every
 * connection, each request answered in its connection's order.
 *
 * The owner runs transactions, the one writer, cursors, batches, timeouts and
 * dropped connections itself; a STATEMENT it hands to the service it was
 * given. The product's service is the SQL layer (increment 3); with none,
 * every statement answers UNSUPPORTED. */

#include <stddef.h>
#include <stdint.h>

#include "gsql_store.h"
#include "gsql_table.h"
#include "gsql_wire.h"

/* A statement, borrowed from the QUERY or PREPARE that carried it. Each part
 * is one v3 value (NULL with length 0 where the request had none). */
typedef struct {
    const unsigned char *statement;   /* text, or a prepared id */
    size_t statement_length;
    const unsigned char *parameters;  /* an array */
    size_t parameters_length;
    const unsigned char *principal;   /* the client's principal(), any value */
    size_t principal_length;
    uint32_t batch;                   /* rows per batch, at least 1 */
} GsqlStatement;

/* What a statement or a fetch produced. The owner clears it before each call.
 * A statement that returns rows sets `has_rows`, `rows` (ONE v3 array of
 * rows), `columns` (one v3 value, on the first batch) and, when more remain,
 * `cursor`. One that does not sets `affected`. */
typedef struct {
    int has_rows;
    GsqlBuf columns;
    GsqlBuf rows;
    void *cursor;          /* the service's; NULL when nothing remains */
    int has_affected;
    uint64_t affected;
} GsqlResult;

typedef struct {
    void *ctx;
    /* Whether a statement writes: only the SQL layer can say. 1 writes, 0
     * reads, negative refuses the statement with `err` filled. */
    int (*writes)(void *ctx, const GsqlStatement *s, GsqlError *err);
    /* Runs a statement in `t`. A refused statement answers its code and leaves
     * `t` usable; GSQL_STORAGE means the engine failed, and the owner then
     * treats the transaction as failed. */
    int (*run)(void *ctx, const GsEngine *e, GsTxn *t, const GsqlStatement *s, GsqlResult *out,
               GsqlError *err);
    /* The next `count` rows of a cursor; clears out->cursor when it ends. */
    int (*fetch)(void *ctx, void *cursor, uint32_t count, GsqlResult *out, GsqlError *err);
    void (*close)(void *ctx, void *cursor);
    /* May be NULL: PREPARE then answers UNSUPPORTED. */
    int (*prepare)(void *ctx, const GsqlStatement *s, GsqlBuf *description, GsqlError *err);
} GsqlService;

typedef struct {
    int create;                  /* create the database if it is absent */
    int ready_fd;                /* -1: none. One line: ready, busy, or error <why> */
    uint32_t snapshot_idle_ms;   /* an idle cursor or BEGIN READ; 0: 300 000 */
    uint32_t writer_idle_ms;     /* an idle BEGIN WRITE; 0: 300 000 */
    size_t output_limit;         /* a connection's unsent replies before its
                                    requests stop being read; 0: 4 MiB */
    const GsOptions *store;      /* the engine's options; NULL: the defaults */
} GsqlOwnerOptions;

/* Serves `db` until SIGTERM. Answers the process's exit status: 0 for a clean
 * stop, and for a lost race (another owner holds the lock), 1 for a failure
 * already reported on ready_fd and stderr. `service` may be NULL. */
int gsql_owner_run(const char *db, const GsqlService *service, const GsqlOwnerOptions *opt);

/* The socket for `db`, an existing database, in the runtime directory: named
 * from the database's device and inode, so two paths to one database find
 * one owner. The directory is created at mode 0700 and refused unless it is
 * a directory, not a symlink, owned by this user and closed to everyone
 * else (§3). The owner and the client both ask here. */
int gsql_socket_path(const char *db, char *out, size_t cap, uint64_t *device, uint64_t *inode,
                     GsqlError *err);

#endif

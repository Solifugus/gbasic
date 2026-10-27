#ifndef GBASIC_GSQL_IO_INTERNAL_H
#define GBASIC_GSQL_IO_INTERNAL_H

/* The vtable both I/O implementations fill in. Each implementation's GsqlIo and
 * GsqlFile structs begin with these headers, so a pointer to one is a pointer
 * to the other. */

#include "gsql_io.h"

typedef struct GsqlIoOps {
    int (*open)(GsqlIo *, const char *, int, GsqlFile **);
    int (*read_at)(GsqlFile *, uint64_t, void *, size_t, size_t *);
    int (*write_at)(GsqlFile *, uint64_t, const void *, size_t);
    int (*sync)(GsqlFile *);
    int (*size)(GsqlFile *, uint64_t *);
    int (*truncate)(GsqlFile *, uint64_t);
    int (*close)(GsqlFile *);
    int (*rename)(GsqlIo *, const char *, const char *);
    int (*remove)(GsqlIo *, const char *);
    int (*sync_dir)(GsqlIo *, const char *);
    int (*lock)(GsqlFile *, int);
    void (*free)(GsqlIo *);
    void (*random)(GsqlIo *, void *, size_t);
    int (*mkdir)(GsqlIo *, const char *);
    int (*list)(GsqlIo *, const char *, int (*)(void *, const char *), void *);
} GsqlIoOps;

/* io.c: sort the names (malloc'd, freed here), then hand each to `each`. */
int gsql_io_walk_names(char **names, size_t n, int (*each)(void *, const char *), void *ctx);

struct GsqlIo {
    const GsqlIoOps *ops;
};

struct GsqlFile {
    GsqlIo *io;
};

#endif

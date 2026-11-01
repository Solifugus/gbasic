#ifndef GBASIC_GSQL_LEASE_H
#define GBASIC_GSQL_LEASE_H

/* The lease: one owner per database on storage whose flock is local to each
 * machine (docs/gsql_lease_design.md). A store takes it after its flock and
 * before it reads anything, keeps it from the owner's poll loop, and writes
 * only through gs_lease_io, which checks it before every write another owner
 * could see. */

#include <stddef.h>
#include <stdint.h>

#include "gsql_io.h"
#include "gsql_store.h"

typedef struct GsLease GsLease;

/* The lock file's name for a database at `dbpath` (malloc'd): DB.lock, or
 * DB.lock.<machine> under a test's local lock. */
char *gs_lease_lock_name(const char *dbpath, const GsLeaseOptions *opt);

/* GS_OK and the lease, or GS_ELSEWHERE with `why` naming the machine that has
 * it, or an I/O status. */
int gs_lease_take(GsqlIo *io, const char *dbpath, const GsLeaseOptions *opt, GsLease **out, char *why, size_t why_n);
/* Renews when due (always, with `force`); GS_OK, or GS_FENCED once another
 * machine's record is found, after which it is FENCED for good. */
int gs_lease_keep(GsLease *l, int force, uint64_t *due_ms);
int gs_lease_fenced(const GsLease *l);
/* Who took it: "host H (pid P)". Empty until fenced. */
const char *gs_lease_holder(const GsLease *l);
/* The layer the store writes through: every write, truncation, rename,
 * removal and create first asks the lease (renewing when its last confirmation is older than
 * trust_ms), and
 * answers GSQL_IO_FENCED once it is lost. Freed by gs_lease_close. */
GsqlIo *gs_lease_io(GsLease *l);
void gs_lease_stats(const GsLease *l, uint64_t *renewals, uint64_t *renew_us_max);
/* A clean close removes the lease file, after checking it still holds ours;
 * otherwise (a failed open, a fenced store) it only frees. */
void gs_lease_close(GsLease *l, int clean);
/* What the lease of the database in directory `dir` (what an engine's open
 * is given) says about who holds it, for a refusal: "host H (pid P)", or a
 * sentence saying it cannot be read. NULL io = the real layer. */
void gs_lease_describe(GsqlIo *io, const char *dir, char *out, size_t n);

#endif

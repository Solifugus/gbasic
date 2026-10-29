#ifndef GSQL_RESEARCH_STORE_H
#define GSQL_RESEARCH_STORE_H

/* The research candidates beside the chosen engine. The interface itself is
 * include/gsql_store.h, and 2.C moved to src/gsql/lsm.c in step 5; 2.A and
 * 2.B stay here, 2.B as the fallback (docs/gsql_storage_design.md §6). Spike
 * code: nothing here is linked into gbasic or run by the gate. */

#include "gsql_store.h"

extern const GsEngine gs_engine_cow;  /* §2.A, retired */
extern const GsEngine gs_engine_wal;  /* §2.B, the fallback */

#endif

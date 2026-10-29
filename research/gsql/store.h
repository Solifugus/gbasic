#ifndef GSQL_RESEARCH_STORE_H
#define GSQL_RESEARCH_STORE_H

/* The research candidates beside the chosen engine. The interface itself is
 * include/gsql_store.h, and 2.C moved to src/gsql/lsm.c in step 5; 2.B stays here as
 * the fallback; 2.A was retired and deleted in step 6, its numbers kept in
 * README.md and the design (docs/gsql_storage_design.md §6). Spike
 * code: nothing here is linked into gbasic or run by the gate. */

#include "gsql_store.h"

extern const GsEngine gs_engine_wal;  /* §2.B, the fallback */

#endif

/* G-SQL's crash harness (docs/gsql_storage_design.md §5.3). See
 * include/gsql_crash.h for the contract.
 *
 * THE ORACLE IS A MODEL, NOT THE ENGINE: the workload generator knows what each
 * transaction wrote, so the state after every prefix of commits is computed
 * here, independently, and recovery is judged against those. A harness that
 * asked the engine what it had committed would accept any engine that agreed
 * with itself. tests/run_gsql_crash.sh therefore points this at a correct toy
 * engine AND at four deliberately broken copies of it, and the broken ones
 * must be caught -- a harness that reports zero violations for everything is
 * indistinguishable from one that checks nothing. */

#include "gsql_crash.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- states ------------------------------------------------------------- */

typedef struct {
    unsigned char *key, *value;
    size_t key_len, value_len;
} Pair;

struct GsqlCrashState {
    Pair *items;
    size_t count, cap;
};

static void state_free(GsqlCrashState *st) {
    for (size_t i = 0; i < st->count; i++) {
        free(st->items[i].key);
        free(st->items[i].value);
    }
    free(st->items);
    st->items = NULL;
    st->count = st->cap = 0;
}

static unsigned char *dup_bytes(const unsigned char *b, size_t n) {
    unsigned char *d = malloc(n ? n : 1);
    if (d && n) memcpy(d, b, n);
    return d;
}

static int key_cmp(const unsigned char *a, size_t la, const unsigned char *b, size_t lb) {
    size_t n = la < lb ? la : lb;
    int c = n ? memcmp(a, b, n) : 0;
    if (c) return c;
    return la < lb ? -1 : la > lb;
}

static long state_find(const GsqlCrashState *st, const unsigned char *k, size_t kl) {
    for (size_t i = 0; i < st->count; i++)
        if (key_cmp(st->items[i].key, st->items[i].key_len, k, kl) == 0) return (long)i;
    return -1;
}

int gsql_crash_state_put(GsqlCrashState *st, const unsigned char *key, size_t key_len,
                         const unsigned char *value, size_t value_len) {
    /* No de-duplication: an engine handing back one key twice has a structural
     * defect, and keeping both makes a state no model can match. */
    if (st->count == st->cap) {
        size_t cap = st->cap ? st->cap * 2 : 16;
        Pair *p = realloc(st->items, cap * sizeof *p);
        if (!p) return 1;
        st->items = p;
        st->cap = cap;
    }
    Pair *p = &st->items[st->count];
    p->key = dup_bytes(key, key_len);
    p->value = dup_bytes(value, value_len);
    p->key_len = key_len;
    p->value_len = value_len;
    if (!p->key || !p->value) return 1;
    st->count++;
    return 0;
}

static void state_apply(GsqlCrashState *st, const GsqlCrashTxn *txn) {
    for (size_t i = 0; i < txn->count; i++) {
        const GsqlCrashPut *p = &txn->puts[i];
        long at = state_find(st, p->key, p->key_len);
        if (!p->value) {
            if (at >= 0) {
                free(st->items[at].key);
                free(st->items[at].value);
                st->items[at] = st->items[--st->count];
            }
        } else if (at >= 0) {
            free(st->items[at].value);
            st->items[at].value = dup_bytes(p->value, p->value_len);
            st->items[at].value_len = p->value_len;
        } else {
            gsql_crash_state_put(st, p->key, p->key_len, p->value, p->value_len);
        }
    }
}

static int pair_cmp(const void *a, const void *b) {
    const Pair *x = a, *y = b;
    return key_cmp(x->key, x->key_len, y->key, y->key_len);
}

static void state_sort(GsqlCrashState *st) {
    if (st->count > 1) qsort(st->items, st->count, sizeof *st->items, pair_cmp);
}

static int state_equal(const GsqlCrashState *a, const GsqlCrashState *b) {
    if (a->count != b->count) return 0;
    for (size_t i = 0; i < a->count; i++) {
        const Pair *x = &a->items[i], *y = &b->items[i];
        if (key_cmp(x->key, x->key_len, y->key, y->key_len) != 0) return 0;
        if (x->value_len != y->value_len) return 0;
        if (x->value_len && memcmp(x->value, y->value, x->value_len) != 0) return 0;
    }
    return 1;
}

static void state_copy(GsqlCrashState *dst, const GsqlCrashState *src) {
    memset(dst, 0, sizeof *dst);
    for (size_t i = 0; i < src->count; i++)
        gsql_crash_state_put(dst, src->items[i].key, src->items[i].key_len,
                             src->items[i].value, src->items[i].value_len);
}

/* ---- the workload ------------------------------------------------------- */

static uint64_t splitmix(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

typedef struct {
    int is_sync;          /* a db-level sync, not a commit */
    int sync;             /* the commit's own flag */
    GsqlCrashTxn txn;
} Step;

typedef struct {
    Step *steps;
    size_t step_count;
    GsqlCrashTxn *txns;   /* the commits, in order: txns[i] is commit i+1 */
    size_t txn_count;
    GsqlCrashState *model; /* model[k]: the state after the first k commits */
    unsigned char *arena;  /* every key and value byte */
    GsqlCrashPut *puts;
} Workload;


static int workload_make(Workload *w, const GsqlCrashConfig *cfg) {
    memset(w, 0, sizeof *w);
    uint64_t rng = cfg->workload_seed * 0x2545F4914F6CDD1DULL + 7;
    size_t T = cfg->transactions;
    size_t max_puts = cfg->max_puts ? cfg->max_puts : 4;
    size_t keyspace = cfg->keyspace ? cfg->keyspace : 12;
    size_t arena_cap = T * max_puts * (cfg->max_value + 16) + 16;
    w->arena = malloc(arena_cap);
    w->puts = malloc((T * max_puts + 1) * sizeof *w->puts);
    w->txns = malloc((T + 1) * sizeof *w->txns);
    w->steps = malloc((2 * T + 1) * sizeof *w->steps);
    w->model = calloc(T + 1, sizeof *w->model);
    if (!w->arena || !w->puts || !w->txns || !w->steps || !w->model) return 0;
    size_t used = 0, np = 0;
    for (size_t t = 0; t < T; t++) {
        size_t n = 1 + (size_t)(splitmix(&rng) % max_puts);
        GsqlCrashPut *first = &w->puts[np];
        for (size_t i = 0; i < n; i++) {
            GsqlCrashPut *p = &w->puts[np++];
            unsigned char *k = w->arena + used;
            unsigned id = (unsigned)(splitmix(&rng) % keyspace);
            size_t kl = (size_t)(keyspace <= 100 ? snprintf((char *)k, 8, "k%02u", id) : snprintf((char *)k, 8, "k%06u", id));
            used += kl;
            p->key = k;
            p->key_len = kl;
            if (splitmix(&rng) % 8 == 0) {
                p->value = NULL;
                p->value_len = 0;
            } else {
                size_t len = cfg->max_value ? (size_t)(splitmix(&rng) % (cfg->max_value + 1)) : 0;
                unsigned char *v = w->arena + used;
                /* Distinct per transaction and position, so a value read from
                 * the wrong commit cannot happen to match. */
                for (size_t b = 0; b < len; b++)
                    v[b] = (unsigned char)(1 + ((t * 131 + i * 17 + b * 7) % 251));
                used += len;
                p->value = v;
                p->value_len = len;
            }
        }
        w->txns[t].puts = first;
        w->txns[t].count = n;
        Step *s = &w->steps[w->step_count++];
        s->is_sync = 0;
        unsigned pct = cfg->sync_percent ? cfg->sync_percent : 67;
        s->sync = (splitmix(&rng) % 100) < pct;
        s->txn = w->txns[t];
        if (!s->sync && splitmix(&rng) % 4 == 0) {
            Step *y = &w->steps[w->step_count++];
            memset(y, 0, sizeof *y);
            y->is_sync = 1;
        }
    }
    w->txn_count = T;
    for (size_t k = 1; k <= T; k++) {
        state_copy(&w->model[k], &w->model[k - 1]);
        state_apply(&w->model[k], &w->txns[k - 1]);
    }
    for (size_t k = 0; k <= T; k++) state_sort(&w->model[k]);
    return 1;
}

static void workload_free(Workload *w) {
    if (w->model)
        for (size_t k = 0; k <= w->txn_count; k++) state_free(&w->model[k]);
    free(w->model);
    free(w->steps);
    free(w->txns);
    free(w->puts);
    free(w->arena);
}

/* ---- one run ------------------------------------------------------------ */

#define DIR "gsql"

typedef struct {
    size_t reached;  /* commits whose commit call was entered */
    size_t committed;/* commits whose commit call returned success */
    size_t durable;  /* commits that had RETURNED durable */
    int stopped_rc;  /* the error that stopped the run, if any */
    int opened;
} RunOutcome;

/* Run the workload against `io` until the first error. `after_sync`, when
 * given, collects the number of the first operation after each sync step
 * that succeeded (room for every step is the caller's). */
static RunOutcome run_workload_at(const GsqlCrashEngine *e, GsqlIo *io, const Workload *w,
                                  uint64_t *after_sync, size_t *nafter) {
    RunOutcome o = {0, 0, 0, 0, 0};
    int err = 0;
    void *db = e->open(io, DIR, &err);
    if (!db) {
        o.stopped_rc = err ? err : -1;
        return o;
    }
    o.opened = 1;
    size_t ci = 0;
    for (size_t s = 0; s < w->step_count; s++) {
        const Step *st = &w->steps[s];
        int rc;
        if (st->is_sync) {
            rc = e->sync(db);
            if (rc == 0) o.durable = o.committed;
            if (rc == 0 && after_sync) after_sync[(*nafter)++] = gsql_io_sim_ops(io) + 1;
        } else {
            o.reached = ++ci;
            rc = e->commit(db, &st->txn, st->sync);
            if (rc == 0) {
                o.committed = ci;
                if (st->sync) o.durable = ci;
            }
        }
        if (rc) {
            o.stopped_rc = rc;
            break;
        }
    }
    e->close(db);
    return o;
}
static RunOutcome run_workload(const GsqlCrashEngine *e, GsqlIo *io, const Workload *w) {
    return run_workload_at(e, io, w, NULL, NULL);
}

typedef struct {
    const GsqlCrashEngine *e;
    const GsqlCrashConfig *cfg;
    GsqlCrashCase mode;
    GsqlCrashResult *res;
} Ctx;

static void report(Ctx *c, uint64_t seed, uint64_t op, const char *stage, const char *msg) {
    c->res->violations++;
    if (c->res->violations > c->cfg->max_reports) return;
    printf("VIOLATION candidate=%s workload=%" PRIu64 " seed=%" PRIu64 " op=%" PRIu64
           " mode=%s%s%s: %s\n",
           c->e->name, c->cfg->workload_seed, seed, op, gsql_crash_mode_name(c->mode),
           stage[0] ? " stage=" : "", stage, msg);
}

static int read_state(const GsqlCrashEngine *e, void *db, GsqlCrashState *st) {
    memset(st, 0, sizeof *st);
    int rc = e->read_all(db, st);
    state_sort(st);
    return rc;
}

/* Which prefix, if any, the recovered state is. -1: none in [lo, hi]. */
static long match_prefix(const Workload *w, const GsqlCrashState *st, size_t lo, size_t hi) {
    for (size_t p = lo; p <= hi && p <= w->txn_count; p++)
        if (state_equal(st, &w->model[p])) return (long)p;
    return -1;
}

static GsqlIo *clone_disk(GsqlIo *io) { return gsql_io_sim_crash_image(io, GSQL_CRASH_PROCESS, 0); }

static uint64_t mix(uint64_t a, uint64_t b, uint64_t c) {
    uint64_t s = a * 0x9E3779B97F4A7C15ULL ^ b * 0xC2B2AE3D27D4EB4FULL ^ c;
    return splitmix(&s);
}

static GsqlCrashMode disk_mode(GsqlCrashCase m) {
    switch (m) {
    case GSQL_CRASH_MODE_PROCESS: return GSQL_CRASH_PROCESS;
    case GSQL_CRASH_MODE_TORN: return GSQL_CRASH_TORN;
    default: return GSQL_CRASH_POWER;
    }
}

/* §5.3: "Work continues after recovery: a further workload runs, crashes and
 * recovers correctly." A continuation of mixed synced and unsynced commits is
 * run on the recovered disk, crashed at a seeded point, and judged against the
 * recovered state plus a prefix of the continuation. A single durable commit
 * after recovery could not see an engine that accepts STALE log frames -- the
 * ones a lost commit left behind, partly overwritten by later ones -- because
 * nothing unsynced was ever written over them (found by perturbing §2.B's
 * checksum chain away: 44,000 crashes and no violation). */
static void continuation(Ctx *c, const Workload *w, GsqlIo *disk, const GsqlCrashState *base,
                         uint64_t seed, uint64_t op) {
    size_t K = w->txn_count < 8 ? w->txn_count : 8;
    if (!K) return;
    uint64_t rng = seed ^ 0x51ed270b27f1a3cdULL;
    int syncs[8];
    for (size_t k = 0; k < K; k++) syncs[k] = splitmix(&rng) % 2 == 0;
    GsqlCrashState *states = calloc(K + 1, sizeof *states);
    state_copy(&states[0], base);
    for (size_t k = 1; k <= K; k++) {
        state_copy(&states[k], &states[k - 1]);
        state_apply(&states[k], &w->txns[(k - 1) % w->txn_count]);
        state_sort(&states[k]);
    }
    state_sort(&states[0]);
    /* a dry run, to know how many operations there are to crash among */
    GsqlIo *x = clone_disk(disk);
    int err = 0;
    void *db = c->e->open(x, DIR, &err);
    uint64_t start = gsql_io_sim_ops(x), total = 0;
    if (db) {
        for (size_t k = 0; k < K; k++)
            if (c->e->commit(db, &w->txns[k % w->txn_count], syncs[k])) break;
        total = gsql_io_sim_ops(x) - start;
        c->e->close(db);
    }
    gsql_io_free(x);
    if (!db || !total) goto out;
    x = clone_disk(disk);
    gsql_io_sim_crash_at(x, start + 1 + splitmix(&rng) % total);
    size_t reached = 0, durable = 0;
    db = c->e->open(x, DIR, &err);
    if (db) {
        for (size_t k = 0; k < K; k++) {
            reached = k + 1;
            int rc = c->e->commit(db, &w->txns[k % w->txn_count], syncs[k]);
            if (rc) break;
            if (syncs[k]) durable = k + 1;
        }
        c->e->close(db);
    }
    {
        uint64_t s2 = mix(seed, op, 0xc0de);
        GsqlIo *img = gsql_io_sim_crash_image(x, disk_mode(c->mode), s2);
        c->res->crashes++;
        GsqlIo *y = clone_disk(img);
        void *d2 = c->e->open(y, DIR, &err);
        char msg[200];
        if (!d2) {
            snprintf(msg, sizeof msg, "recovery after a crashed continuation failed (error %d)", err);
            report(c, s2, op, "continuation", msg);
        } else {
            GsqlCrashState got;
            memset(&got, 0, sizeof got);
            int rrc = c->e->read_all(d2, &got);
            state_sort(&got);
            long p = -1;
            for (size_t k = durable; !rrc && k <= reached; k++)
                if (state_equal(&got, &states[k])) { p = (long)k; break; }
            if (p < 0) {
                long any = -1;
                for (size_t k = 0; !rrc && k <= K; k++)
                    if (state_equal(&got, &states[k])) { any = (long)k; break; }
                snprintf(msg, sizeof msg,
                         "after recovery, a continuation crashed and came back as %s%ld of its commits (durable %zu, reached %zu)",
                         any >= 0 ? "" : "NO prefix; nearest ", any, durable, reached);
                report(c, s2, op, "continuation", msg);
            }
            state_free(&got);
            c->e->close(d2);
        }
        gsql_io_free(y);
        gsql_io_free(img);
    }
    gsql_io_free(x);
out:
    for (size_t k = 0; k <= K; k++) state_free(&states[k]);
    free(states);
}

/* Recover from `disk` and judge it. With `full`, also the idempotence and
 * work-continues checks, on clones so `disk` stays pristine. */
static void judge(Ctx *c, const Workload *w, GsqlIo *disk, size_t lo, size_t hi,
                  uint64_t seed, uint64_t op, const char *stage, int full) {
    char msg[256];
    GsqlIo *a = clone_disk(disk);
    int err = 0;
    void *db = c->e->open(a, DIR, &err);
    if (!db) {
        snprintf(msg, sizeof msg, "recovery failed (error %d)", err);
        report(c, seed, op, stage, msg);
        gsql_io_free(a);
        return;
    }
    GsqlCrashState st;
    int rrc = read_state(c->e, db, &st);
    long p = rrc ? -1 : match_prefix(w, &st, lo, hi);
    if (p < 0) {
        long any = rrc ? -1 : match_prefix(w, &st, 0, w->txn_count);
        if (rrc)
            snprintf(msg, sizeof msg, "reading the recovered state failed (error %d)", rrc);
        else if (any >= 0)
            snprintf(msg, sizeof msg,
                     "recovered the state after %ld commits; durable %zu, reached %zu", any, lo, hi);
        else
            snprintf(msg, sizeof msg,
                     "recovered %zu keys matching NO prefix of the commits (durable %zu, reached %zu)",
                     st.count, lo, hi);
        report(c, seed, op, stage, msg);
        state_free(&st);
        c->e->close(db);
        gsql_io_free(a);
        return;
    }
    if ((size_t)p > lo) c->res->not_prefix_ok++;
    if ((size_t)p < hi) c->res->prefix_dropped++;
    if (lo > 0) c->res->durable_checked++;
    c->e->close(db);

    if (full) {
        /* Idempotent: reopening the recovered disk gives the same state. */
        db = c->e->open(a, DIR, &err);
        GsqlCrashState st2;
        if (!db) {
            report(c, seed, op, stage, "a second recovery failed");
        } else {
            read_state(c->e, db, &st2);
            if (!state_equal(&st, &st2)) report(c, seed, op, stage, "recovery is not idempotent");
            state_free(&st2);

            /* Work continues: one more durable commit, then a fresh open must
             * see the recovered state plus it. */
            static const unsigned char key[] = "after", val[] = "continued";
            GsqlCrashPut put = {key, 5, val, 9};
            GsqlCrashTxn txn = {&put, 1};
            int rc = c->e->commit(db, &txn, 1);
            c->e->close(db);
            if (rc) {
                snprintf(msg, sizeof msg, "a commit after recovery failed (error %d)", rc);
                report(c, seed, op, stage, msg);
            } else {
                GsqlCrashState want;
                state_copy(&want, &st);
                state_apply(&want, &txn);
                state_sort(&want);
                /* ... and survive a power loss, since it returned durable. */
                GsqlIo *b = gsql_io_sim_crash_image(a, GSQL_CRASH_POWER, seed ^ 0x5bd1e995);
                db = c->e->open(b, DIR, &err);
                if (!db) {
                    report(c, seed, op, stage, "recovery after further work failed");
                } else {
                    read_state(c->e, db, &st2);
                    if (!state_equal(&st2, &want))
                        report(c, seed, op, stage,
                               "a durable commit made after recovery was lost by the next crash");
                    state_free(&st2);
                    c->e->close(db);
                }
                gsql_io_free(b);
                state_free(&want);
            }
        }
        continuation(c, w, disk, &st, seed, op);
    }
    state_free(&st);
    gsql_io_free(a);
}


/* Crash the RECOVERY of `disk` at each of its operations, then recover again. */
static void crash_recovery(Ctx *c, const Workload *w, GsqlIo *disk, size_t lo, size_t hi,
                           uint64_t seed, uint64_t op) {
    GsqlIo *probe = clone_disk(disk);
    int err = 0;
    void *db = c->e->open(probe, DIR, &err);
    uint64_t r = gsql_io_sim_ops(probe);
    if (db) c->e->close(db);
    gsql_io_free(probe);
    char stage[48];
    for (uint64_t m = 1; m <= r; m++) {
        GsqlIo *x = clone_disk(disk);
        gsql_io_sim_crash_at(x, m);
        db = c->e->open(x, DIR, &err);
        if (db) c->e->close(db);
        uint64_t s2 = mix(seed, m, 0x7f4a7c15);
        GsqlIo *img = gsql_io_sim_crash_image(x, disk_mode(c->mode), s2);
        c->res->crashes++;
        snprintf(stage, sizeof stage, "recovery-op-%" PRIu64, m);
        judge(c, w, img, lo, hi, s2, op, stage, 0);
        gsql_io_free(img);
        gsql_io_free(x);
    }
}

static void run_crash_points(Ctx *c, const Workload *w) {
    GsqlIo *base = gsql_io_sim();
    /* the operation just after each sync step: a sync that answers success
     * and flushes nothing is visible ONLY from there to the next flush, which
     * for 2.C is one or two operations. Sampled uniformly the gate missed it
     * at 50,625 crashes with zero violations (found 2026-10-29 by making
     * 2.C's `flush` a no-op), and sampling just before flushes cannot see it,
     * since a no-op sync issues none. */
    uint64_t *after = malloc((w->step_count + 1) * sizeof *after);
    size_t nafter = 0;
    RunOutcome full = run_workload_at(c->e, base, w, after, after ? &nafter : NULL);
    uint64_t total = gsql_io_sim_ops(base);
    size_t nflush = gsql_io_sim_flush_points(base, NULL, 0);
    uint64_t *flushes = malloc((nflush + 1) * sizeof *flushes);
    if (flushes) gsql_io_sim_flush_points(base, flushes, nflush);
    gsql_io_free(base);
    if (full.stopped_rc) {
        report(c, 0, 0, "", "the uncrashed workload did not complete");
        free(flushes);
        free(after);
        return;
    }
    uint64_t limit = c->cfg->crash_limit && c->cfg->crash_limit < total ? c->cfg->crash_limit : total;
    uint64_t points = c->cfg->sample_points && c->cfg->sample_points < limit ? c->cfg->sample_points : limit;
    /* GSQL_CRASH_ONLY=N: just that crash point, to reproduce one report */
    const char *only = getenv("GSQL_CRASH_ONLY");
    for (uint64_t j = 1; j <= points; j++) {
        if (only) { if (j > 1) break; }
        uint64_t n;
        if (points == limit) n = j;
        else if (j % 4 == 0 && nafter && after[0] <= limit) {
            /* a quarter just after a sync step returned */
            n = after[mix(c->cfg->workload_seed, j, 0x5a1u) % nafter];
            if (n > limit) n = after[0];
        }
        else if (j % 2 == 0 && flushes && nflush && flushes[0] <= limit)
            /* half the samples land on a flush, crashing just before it */
        {
            n = flushes[mix(c->cfg->workload_seed, j, 0xf1u) % nflush];
            if (n > limit) n = flushes[0];
        }
        else n = 1 + mix(c->cfg->workload_seed, j, 0xc0ffee) % limit;
        if (only) n = strtoull(only, NULL, 10);
        GsqlIo *io = gsql_io_sim();
        gsql_io_sim_crash_at(io, n);
        RunOutcome o = run_workload(c->e, io, w);
        unsigned images = c->cfg->images_per_point ? c->cfg->images_per_point : 1;
        if (c->mode == GSQL_CRASH_MODE_PROCESS) images = 1; /* one disk, however seeded */
        for (unsigned j = 0; j < images; j++) {
            uint64_t seed = mix(c->cfg->workload_seed, n, (uint64_t)c->mode * 1000 + j);
            GsqlIo *disk = gsql_io_sim_crash_image(io, disk_mode(c->mode), seed);
            c->res->crashes++;
            judge(c, w, disk, o.durable, o.reached, seed, n, "", 1);
            if (c->cfg->crash_recovery) crash_recovery(c, w, disk, o.durable, o.reached, seed, n);
            gsql_io_free(disk);
        }
        gsql_io_free(io);
        if (c->res->violations > c->cfg->max_reports) break;
    }
    free(flushes);
    free(after);
}

/* Failed sync: the k-th flush fails. The engine must report it, refuse further
 * work, and recover from what is really on disk (rule 5: what a failed flush
 * left there is unknown, so the disk is a power-loss image). */
static void run_failed_sync(Ctx *c, const Workload *w) {
    for (unsigned k = 1; k < 100000; k++) {
        GsqlIo *io = gsql_io_sim();
        uint64_t seed = mix(c->cfg->workload_seed, k, 0xf5);
        gsql_io_sim_seed(io, seed);
        gsql_io_sim_fail(io, GSQL_IO_FAIL_SYNC, k);
        int err = 0;
        RunOutcome o = {0, 0, 0, 0, 0};
        void *db = c->e->open(io, DIR, &err);
        int refused_ok = 1, reported = 0, fired_in_work = 1;
        if (!db) {
            reported = 1;
        } else {
            size_t ci = 0;
            for (size_t s = 0; s < w->step_count; s++) {
                const Step *st = &w->steps[s];
                int rc;
                if (st->is_sync) {
                    rc = c->e->sync(db);
                    if (rc == 0) o.durable = o.committed;
                } else {
                    o.reached = ++ci;
                    rc = c->e->commit(db, &st->txn, st->sync);
                    if (rc == 0) {
                        o.committed = ci;
                        if (st->sync) o.durable = ci;
                    }
                }
                if (rc) {
                    reported = 1;
                    /* §1.3: every later operation answers FAILED, never a retry. */
                    static const unsigned char key[] = "x";
                    GsqlCrashPut put = {key, 1, key, 1};
                    GsqlCrashTxn txn = {&put, 1};
                    int again = c->e->commit(db, &txn, 1);
                    int again_sync = c->e->sync(db);
                    refused_ok = again == GSQL_E_FAILED && again_sync == GSQL_E_FAILED;
                    break;
                }
            }
            /* Asked BEFORE close: a close may flush, and the interface gives it
             * no way to report a failure, so a fault that fires there is not
             * one the engine swallowed. The disk it leaves is still judged. */
            fired_in_work = !gsql_io_sim_fault_armed(io, GSQL_IO_FAIL_SYNC);
            c->e->close(db);
        }
        int fired = !gsql_io_sim_fault_armed(io, GSQL_IO_FAIL_SYNC);
        if (!fired) {
            gsql_io_free(io);
            return; /* past the last flush the workload makes */
        }
        c->res->crashes++;
        if (!reported && fired_in_work) report(c, seed, k, "", "a failed flush was reported as success");
        else if (!refused_ok)
            report(c, seed, k, "", "after a failed flush the engine did not refuse further work with FAILED");
        GsqlIo *disk = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, seed);
        judge(c, w, disk, o.durable, o.reached, seed, k, "", 1);
        gsql_io_free(disk);
        gsql_io_free(io);
        if (c->res->violations > c->cfg->max_reports) return;
    }
}

/* Full disk: the k-th write answers ENOSPC. The transaction it belonged to is
 * aborted with FULL; the database stays usable; nothing committed is lost. */
static void run_full_disk(Ctx *c, const Workload *w) {
    char msg[200];
    for (unsigned k = 1; k < 100000; k++) {
        GsqlIo *io = gsql_io_sim();
        gsql_io_sim_fail(io, GSQL_IO_FAIL_WRITE, k);
        int err = 0;
        void *db = c->e->open(io, DIR, &err);
        GsqlCrashState want;
        memset(&want, 0, sizeof want);
        int bad = 0;
        if (db) {
            for (size_t s = 0; s < w->step_count && !bad; s++) {
                const Step *st = &w->steps[s];
                int rc = st->is_sync ? c->e->sync(db) : c->e->commit(db, &st->txn, st->sync);
                if (rc == 0) {
                    if (!st->is_sync) state_apply(&want, &st->txn);
                } else if (rc != GSQL_E_FULL) {
                    /* FULL from a sync is allowed: a sync is not a transaction,
                     * and the commits it could not make durable stay committed */
                    snprintf(msg, sizeof msg, "a full disk gave error %d at step %zu, not FULL", rc, s);
                    report(c, k, k, "", msg);
                    bad = 1;
                }
            }
            if (!bad) {
                /* Durable now, so a power loss must keep all of it. */
                int src = c->e->sync(db);
                /* the fault may land on this very sync; it fires once, so a
                 * usable database takes the retry */
                if (src == GSQL_E_FULL) src = c->e->sync(db);
                if (src != 0) {
                    snprintf(msg, sizeof msg, "the database was not usable after a full disk (sync gave %d)", src);
                    report(c, k, k, "", msg);
                    bad = 1;
                }
            }
            c->e->close(db);
        }
        int fired = !gsql_io_sim_fault_armed(io, GSQL_IO_FAIL_WRITE);
        if (!fired) {
            state_free(&want);
            gsql_io_free(io);
            return;
        }
        c->res->crashes++;
        if (db && !bad) {
            state_sort(&want);
            GsqlIo *disk = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, mix(k, 3, 9));
            void *d2 = c->e->open(disk, DIR, &err);
            if (!d2) {
                report(c, k, k, "", "recovery after a full disk failed");
            } else {
                GsqlCrashState got;
                read_state(c->e, d2, &got);
                if (!state_equal(&got, &want))
                    report(c, k, k, "", "a commit made before or after the full disk was lost");
                state_free(&got);
                c->e->close(d2);
            }
            gsql_io_free(disk);
        }
        state_free(&want);
        gsql_io_free(io);
        if (c->res->violations > c->cfg->max_reports) return;
    }
}

GsqlCrashResult gsql_crash_run(const GsqlCrashEngine *engine, GsqlCrashCase mode,
                               const GsqlCrashConfig *cfg) {
    GsqlCrashResult res;
    memset(&res, 0, sizeof res);
    Workload w;
    if (!workload_make(&w, cfg)) {
        workload_free(&w);
        res.violations = 1;
        printf("VIOLATION candidate=%s: the harness could not build its workload\n", engine->name);
        return res;
    }
    Ctx c = {engine, cfg, mode, &res};
    if (mode == GSQL_CRASH_MODE_FAILED_SYNC) run_failed_sync(&c, &w);
    else if (mode == GSQL_CRASH_MODE_FULL_DISK) run_full_disk(&c, &w);
    else run_crash_points(&c, &w);
    workload_free(&w);
    return res;
}

const char *gsql_crash_mode_name(GsqlCrashCase mode) {
    switch (mode) {
    case GSQL_CRASH_MODE_PROCESS: return "process";
    case GSQL_CRASH_MODE_POWER: return "power";
    case GSQL_CRASH_MODE_TORN: return "torn";
    case GSQL_CRASH_MODE_FAILED_SYNC: return "failed-sync";
    case GSQL_CRASH_MODE_FULL_DISK: return "full-disk";
    }
    return "?";
}

/* The crash harness (src/gsql/crash.c) pointed at a TOY ENGINE and at four
 * deliberately broken copies of it. The toy is not a candidate: it is the
 * smallest engine that can be correct under the I/O contract -- an append-only
 * log of checksummed commit records -- and each broken copy omits exactly one
 * of the things that correctness rests on:
 *
 *   nosync     commit(sync) does not flush        (rule 1)
 *   nodirsync  creating the log is never made durable (rule 2)
 *   nocrc      recovery trusts a record without checking it (rule 3)
 *   notrunc    recovery leaves a damaged tail and appends after it
 *   swallow    a failed flush is reported as success          (rule 5)
 *   carryon    a failed flush is reported, and work then carries on (§1.3)
 *   fullskip   a write the full disk refused still moves the end of the log
 *   rewrite    recovery empties the log, syncs, then writes it back -- correct
 *              unless recovery ITSELF is interrupted, so only the harness's
 *              crash-the-recovery stage can see it
 *
 * The harness must report zero violations for the correct toy and at least one
 * for each broken copy IN THE MODE THAT CAN SEE IT. A harness that cannot tell
 * them apart has measured nothing, however many crashes it injected.
 *
 * Usage: crash_check VARIANT MODE SEED[-SEED] TRANSACTIONS MAX_VALUE RECOVERY [SYNC_PERCENT [IMAGES]]
 * Prints one summary line: "crashes=N violations=N durable_checked=N
 * kept_unsynced=N dropped_unsynced=N". */

#include "gsql_crash.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { V_OK, V_NOSYNC, V_NODIRSYNC, V_NOCRC, V_NOTRUNC, V_SWALLOW, V_CARRYON, V_FULLSKIP, V_REWRITE };
static int variant = V_OK;

/* ---- the toy engine ------------------------------------------------------ */

typedef struct {
    unsigned char *k, *v;
    size_t kl, vl;
} Ent;

typedef struct {
    GsqlIo *io;
    GsqlFile *f;
    uint64_t end;
    int failed;
    Ent *ents;
    size_t n, cap;
} Toy;

static uint32_t crc32(const unsigned char *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void toy_set(Toy *t, const unsigned char *k, size_t kl, const unsigned char *v, size_t vl, int del) {
    for (size_t i = 0; i < t->n; i++) {
        if (t->ents[i].kl == kl && memcmp(t->ents[i].k, k, kl) == 0) {
            free(t->ents[i].v);
            if (del) {
                free(t->ents[i].k);
                t->ents[i] = t->ents[--t->n];
            } else {
                t->ents[i].v = malloc(vl ? vl : 1);
                if (vl) memcpy(t->ents[i].v, v, vl);
                t->ents[i].vl = vl;
            }
            return;
        }
    }
    if (del) return;
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 16;
        t->ents = realloc(t->ents, t->cap * sizeof *t->ents);
    }
    Ent *e = &t->ents[t->n++];
    e->k = malloc(kl ? kl : 1);
    memcpy(e->k, k, kl);
    e->kl = kl;
    e->v = malloc(vl ? vl : 1);
    if (vl) memcpy(e->v, v, vl);
    e->vl = vl;
}

/* Parse one payload into the state; 0 if it is malformed. */
static int apply_payload(Toy *t, const unsigned char *p, size_t n, int dry) {
    if (n < 4) return 0;
    uint32_t count = get32(p);
    size_t at = 4;
    for (uint32_t i = 0; i < count; i++) {
        if (at + 1 > n) return 0;
        size_t kl = p[at++];
        if (at + kl + 5 > n) return 0;
        const unsigned char *k = p + at;
        at += kl;
        int del = p[at++];
        size_t vl = get32(p + at);
        at += 4;
        if (at + vl > n) return 0;
        if (!dry) toy_set(t, k, kl, p + at, vl, del);
        at += vl;
    }
    return at == n;
}

static void toy_free(Toy *t) {
    for (size_t i = 0; i < t->n; i++) {
        free(t->ents[i].k);
        free(t->ents[i].v);
    }
    free(t->ents);
    free(t);
}

static void *toy_open(GsqlIo *io, const char *dir, int *err) {
    char path[256];
    snprintf(path, sizeof path, "%s/log", dir);
    Toy *t = calloc(1, sizeof *t);
    t->io = io;
    int rc = gsql_io_open(io, path, 0, &t->f);
    if (rc == ENOENT) {
        rc = gsql_io_open(io, path, GSQL_IO_CREATE | GSQL_IO_EXCLUSIVE, &t->f);
        if (rc == 0 && variant != V_NODIRSYNC) rc = gsql_io_sync_dir(io, dir);
    }
    if (rc == 0) rc = gsql_io_lock(t->f, 1);
    uint64_t size = 0;
    if (rc == 0) rc = gsql_io_size(t->f, &size);
    unsigned char *all = NULL;
    if (rc == 0 && size) {
        all = malloc((size_t)size);
        size_t got = 0;
        rc = gsql_io_read_at(t->f, 0, all, (size_t)size, &got);
        if (rc == 0 && got != size) rc = EIO;
    }
    /* Recovery: every record up to the first one that is short, empty or does
     * not check out. Everything after it is a commit that never finished. */
    uint64_t good = 0;
    while (rc == 0 && good + 8 <= size) {
        uint32_t len = get32(all + good);
        uint32_t crc = get32(all + good + 4);
        if (len == 0 || good + 8 + len > size) break;
        const unsigned char *payload = all + good + 8;
        if (variant != V_NOCRC && crc32(payload, len) != crc) break;
        if (!apply_payload(t, payload, len, 1)) break;
        apply_payload(t, payload, len, 0);
        good += 8 + len;
    }
    if (rc == 0 && variant == V_REWRITE && good > 0) {
        rc = gsql_io_truncate(t->f, 0);
        if (rc == 0) rc = gsql_io_sync(t->f);
        if (rc == 0) rc = gsql_io_write_at(t->f, 0, all, (size_t)good);
        if (rc == 0) rc = gsql_io_sync(t->f);
    }
    free(all);
    t->end = good;
    if (rc == 0 && good < size) {
        if (variant == V_NOTRUNC) {
            t->end = size; /* the defect: the damaged tail stays, and new work goes after it */
        } else {
            rc = gsql_io_truncate(t->f, good);
            if (rc == 0) rc = gsql_io_sync(t->f);
        }
    }
    if (rc) {
        if (t->f) gsql_io_close(t->f);
        toy_free(t);
        *err = rc;
        return NULL;
    }
    return t;
}

static int toy_flush(Toy *t) {
    if (variant == V_NOSYNC) return 0;
    int rc = gsql_io_sync(t->f);
    if (rc == 0) return 0;
    if (variant == V_SWALLOW) return 0;
    if (variant == V_CARRYON) return rc; /* the defect: not marked failed, so it carries on */
    t->failed = 1;
    return rc;
}

static int toy_commit(void *db, const GsqlCrashTxn *txn, int sync) {
    Toy *t = db;
    if (t->failed) return GSQL_E_FAILED;
    size_t n = 4;
    for (size_t i = 0; i < txn->count; i++)
        n += 1 + txn->puts[i].key_len + 1 + 4 + (txn->puts[i].value ? txn->puts[i].value_len : 0);
    unsigned char *rec = malloc(8 + n);
    unsigned char *p = rec + 8;
    put32(p, (uint32_t)txn->count);
    size_t at = 4;
    for (size_t i = 0; i < txn->count; i++) {
        const GsqlCrashPut *u = &txn->puts[i];
        p[at++] = (unsigned char)u->key_len;
        memcpy(p + at, u->key, u->key_len);
        at += u->key_len;
        p[at++] = u->value == NULL;
        size_t vl = u->value ? u->value_len : 0;
        put32(p + at, (uint32_t)vl);
        at += 4;
        if (vl) memcpy(p + at, u->value, vl);
        at += vl;
    }
    put32(rec, (uint32_t)n);
    put32(rec + 4, crc32(p, n));
    int rc = gsql_io_write_at(t->f, t->end, rec, 8 + n);
    if (rc == 0) {
        t->end += 8 + n;
        apply_payload(t, p, n, 0);
        if (sync) rc = toy_flush(t);
    } else if (rc == ENOSPC) {
        if (variant == V_FULLSKIP) t->end += 8 + n; /* the defect: a hole in the log */
        rc = GSQL_E_FULL; /* nothing was written; the database is unchanged */
    }
    free(rec);
    return rc;
}

static int toy_sync(void *db) {
    Toy *t = db;
    if (t->failed) return GSQL_E_FAILED;
    return toy_flush(t);
}

static int toy_read_all(void *db, GsqlCrashState *out) {
    Toy *t = db;
    for (size_t i = 0; i < t->n; i++)
        if (gsql_crash_state_put(out, t->ents[i].k, t->ents[i].kl, t->ents[i].v, t->ents[i].vl))
            return ENOMEM;
    return 0;
}

static void toy_close(void *db) {
    Toy *t = db;
    gsql_io_close(t->f);
    toy_free(t);
}

int main(int argc, char **argv) {
    if (argc < 7 || argc > 9) {
        fprintf(stderr, "usage: crash_check VARIANT MODE SEED TXNS MAX_VALUE RECOVERY [SYNC_PERCENT [IMAGES]]\n");
        return 2;
    }
    static const char *variants[] = {"ok", "nosync", "nodirsync", "nocrc", "notrunc",
                                     "swallow", "carryon", "fullskip", "rewrite"};
    static const char *modes[] = {"", "process", "power", "torn", "failed-sync", "full-disk"};
    int mode = 0;
    variant = -1;
    for (int i = 0; i < 9; i++) if (strcmp(argv[1], variants[i]) == 0) variant = i;
    for (int i = 1; i <= 5; i++) if (strcmp(argv[2], modes[i]) == 0) mode = i;
    if (variant < 0 || !mode) {
        fprintf(stderr, "unknown variant or mode\n");
        return 2;
    }
    char name[32];
    snprintf(name, sizeof name, "toy-%s", variants[variant]);
    GsqlCrashEngine e = {name, toy_open, toy_commit, toy_sync, toy_read_all, toy_close};
    /* SEED may be a range, "1-200": one process, many workloads, summed. Odd
     * seeds sync rarely and even ones often unless SYNC_PERCENT is given, so a
     * range covers both shapes. */
    unsigned long long lo = strtoull(argv[3], NULL, 10), hi = lo;
    const char *dash = strchr(argv[3], '-');
    if (dash) hi = strtoull(dash + 1, NULL, 10);
    GsqlCrashResult sum;
    memset(&sum, 0, sizeof sum);
    const char *reports = getenv("GSQL_CRASH_REPORTS");
    for (unsigned long long seed = lo; seed <= hi; seed++) {
        GsqlCrashConfig cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.workload_seed = seed;
        cfg.transactions = (size_t)strtoul(argv[4], NULL, 10);
        cfg.max_value = (size_t)strtoul(argv[5], NULL, 10);
        cfg.crash_recovery = atoi(argv[6]);
        cfg.sync_percent = argc >= 8 && argv[7][0] ? (unsigned)atoi(argv[7]) : (seed % 2 ? 20 : 80);
        if (argc >= 9) cfg.images_per_point = (unsigned)atoi(argv[8]);
        /* Every violation, so a runner can ask WHICH check fired, not only
         * whether one did: a broken copy caught only by a neighbouring check
         * says the check written for it is blind. */
        cfg.max_reports = reports ? (unsigned)atoi(reports) : 3;
        GsqlCrashResult r = gsql_crash_run(&e, (GsqlCrashCase)mode, &cfg);
        sum.crashes += r.crashes;
        sum.violations += r.violations;
        sum.durable_checked += r.durable_checked;
        sum.not_prefix_ok += r.not_prefix_ok;
        sum.prefix_dropped += r.prefix_dropped;
    }
    printf("crashes=%llu violations=%llu durable_checked=%llu kept_unsynced=%llu dropped_unsynced=%llu\n",
           (unsigned long long)sum.crashes, (unsigned long long)sum.violations,
           (unsigned long long)sum.durable_checked, (unsigned long long)sum.not_prefix_ok,
           (unsigned long long)sum.prefix_dropped);
    return 0;
}

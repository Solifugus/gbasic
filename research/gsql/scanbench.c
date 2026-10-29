/* The scan path (docs/gsql_storage_design.md §6: "the scan path is about
 * 10x slower than LMDB's in all three candidates", and where G-Dash's page
 * time goes, §5.1.1 G2). A full scan of one tree, key and value read for
 * every row, over the chosen engine.
 *
 *   scanbench DIR ROWS [REPEATS]
 *
 * Loads ROWS rows of 100 bytes in key order, flushes so they sit in sorted
 * files, reopens, then times REPEATS full scans (first scan included: the
 * blocks come from the page cache either way, the store's own block cache
 * is warm only after it). Every scan is checked: ROWS rows, in order, every
 * value the one its key was loaded with, summed into a checksum that must
 * match the load's. Prints one JSON line. */

#define _POSIX_C_SOURCE 200809L
#include "store_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const GsEngine *E = &gs_engine_lsm;
#define VLEN 100

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static size_t key_of(char *k, uint64_t i) { return (size_t)snprintf(k, 32, "k%015llu", (unsigned long long)i); }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: scanbench DIR ROWS [REPEATS]\n"); return 2; }
    uint64_t rows = strtoull(argv[2], NULL, 10);
    int reps = argc > 3 ? atoi(argv[3]) : 5;
    GsOptions o = {1, 0, 0, NULL, 0, 0, 0, 0};
    GsStore *db;
    if (E->open(argv[1], &o, &db)) return 1;
    GsTxn *w;
    E->begin_write(db, &w);
    E->tree_create(w, gss("t"));
    uint64_t sum = 0;
    for (uint64_t i = 0; i < rows; i++) {
        char k[32];
        unsigned char v[VLEN];
        size_t kn = key_of(k, i);
        for (int j = 0; j < VLEN; j++) v[j] = (unsigned char)(i * 31 + (uint64_t)j);
        sum += v[0] + v[VLEN - 1];
        if (E->put(w, gss("t"), gsb(k, kn), gsb(v, VLEN), GS_APPEND)) return 1;
    }
    if (E->commit(w, 1, NULL) || E->flush(db) || E->close(db)) return 1;
    o.create = 0;
    if (E->open(argv[1], &o, &db)) return 1;
    double best = 1e30, first = 0;
    int bad = 0;
    for (int r = 0; r < reps; r++) {
        GsTxn *t;
        GsCursor *c;
        int pos;
        uint64_t n = 0, s = 0;
        double t0 = now_ms();
        E->begin_read(db, &t);
        E->cursor_open(t, gss("t"), &c);
        char prev[40] = "";
        size_t pn = 0;
        for (int rc = E->first(c, &pos); rc == GS_OK && pos; rc = E->next(c, &pos)) {
            GsBytes k, v;
            E->key(c, &k);
            E->value(c, &v);
            if (v.n != VLEN || (pn && (k.n != pn || memcmp(k.p, prev, pn) <= 0))) bad++;
            memcpy(prev, k.p, k.n < sizeof prev ? k.n : sizeof prev);
            pn = k.n;
            s += v.p[0] + v.p[VLEN - 1];
            n++;
        }
        E->cursor_close(c);
        E->abort(t);
        double ms = now_ms() - t0;
        if (n != rows || s != sum) bad++;
        if (r == 0) first = ms;
        if (ms < best) best = ms;
    }
    E->close(db);
    printf("{\"test\":\"scan\",\"engine\":\"%s\",\"rows\":%llu,\"first_ms\":%.1f,\"best_ms\":%.1f,\"rows_per_s\":%.0f,\"bad\":%d}\n",
           E->name, (unsigned long long)rows, first, best, rows / (best / 1e3), bad);
    return bad != 0;
}

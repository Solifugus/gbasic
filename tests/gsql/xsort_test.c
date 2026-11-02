/* The external sorter against an in-memory stable sort (tests/run_gsql_table.sh,
 * TIER sort; docs/gsql_spill_design.md §5). xsort_test DIR: runs go in DIR,
 * which must be left empty. */
#define _XOPEN_SOURCE 700
#include "xsort.h"
#include <dirent.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static long checks, bad;
#define EXPECT(c, ...) do { checks++; if (!(c)) { bad++; if (bad < 20) { printf("MISMATCH "); printf(__VA_ARGS__); printf("\n"); } } } while (0)
typedef struct { unsigned char k[24]; size_t kn; unsigned id; } E;
static int cmpE(const void *a, const void *b) {
    const E *x = a, *y = b; size_t n = x->kn < y->kn ? x->kn : y->kn;
    int c = n ? memcmp(x->k, y->k, n) : 0; if (c) return c;
    if (x->kn != y->kn) return x->kn < y->kn ? -1 : 1;
    return x->id < y->id ? -1 : x->id > y->id;
}
static int entries_in(const char *d) { DIR *dd = opendir(d); int n = 0; struct dirent *e; if (!dd) return -1; while ((e = readdir(dd))) if (e->d_name[0] != '.') n++; closedir(dd); return n; }
static void one(const char *tmp, size_t n, size_t budget, unsigned keyspace, unsigned seed, int expect_runs) {
    srand(seed);
    E *es = malloc((n ? n : 1) * sizeof *es);
    GsXsort *s; EXPECT(gs_xsort_open(tmp, budget, &s) == 0, "open");
    for (size_t i = 0; i < n; i++) {
        E *e = &es[i]; e->id = (unsigned)i; e->kn = (size_t)(rand() % 6);
        for (size_t j = 0; j < e->kn; j++) e->k[j] = (unsigned char)(rand() % keyspace);
        EXPECT(gs_xsort_add(s, e->k, e->kn, &e->id, sizeof e->id) == 0, "add %zu", i);
    }
    EXPECT(gs_xsort_finish(s) == 0, "finish");
    EXPECT(expect_runs ? gs_xsort_runs(s) > 50 : gs_xsort_runs(s) == 0, "n=%zu budget=%zu runs=%u", n, budget, gs_xsort_runs(s));
    /* 2: more runs than one merge opens, so at least one pass ran first */
    EXPECT(expect_runs != 2 || (gs_xsort_runs(s) > 2000 && gs_xsort_passes(s) >= 1), "n=%zu runs=%u passes=%u", n,
           gs_xsort_runs(s), gs_xsort_passes(s));
    qsort(es, n, sizeof *es, cmpE);
    size_t got = 0; int have = 1;
    while (have) {
        GsBytes k, v; EXPECT(gs_xsort_next(s, &k, &v, &have) == 0, "next"); if (!have) break;
        unsigned id; memcpy(&id, v.p, sizeof id);
        if (got < n) EXPECT(k.n == es[got].kn && (!k.n || !memcmp(k.p, es[got].k, k.n)) && id == es[got].id, "entry %zu: id %u want %u", got, id, es[got].id);
        got++;
    }
    EXPECT(got == n, "got %zu of %zu", got, n);
    gs_xsort_close(s);
    EXPECT(entries_in(tmp) == 0, "the sort left %d files in %s", entries_in(tmp), tmp);
    free(es);
}
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    const char *tmp = argv[1];
    one(tmp, 0, 0, 256, 1, 0);
    one(tmp, 1, 0, 256, 2, 0);
    one(tmp, 50000, 0, 4, 3, 0);            /* fits: no runs, many ties */
    one(tmp, 200000, 64 * 1024, 4, 4, 1);   /* hundreds of runs, many ties */
    one(tmp, 200000, 64 * 1024, 256, 5, 1);
    /* thousands of runs, merged in passes: with at most 256 files open, a
       merge of every run at once cannot open them (measured: it failed) */
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    struct rlimit low = rl;
    low.rlim_cur = 256;
    setrlimit(RLIMIT_NOFILE, &low);
    one(tmp, 300000, 4096, 4, 6, 2);
    setrlimit(RLIMIT_NOFILE, &rl);
    /* the sweep: a gone process's directories removed, files and all; this
       process's, and a name not the sorter's, kept */
    pid_t child = fork();
    if (child == 0) {
        _exit(0);
    }
    waitpid(child, NULL, 0);
    char p1[600], p2[600], p3[600], p4[600], f1[700];
    snprintf(p1, sizeof p1, "%s/gsql-sort-%ld-abcdef", tmp, (long)child);
    snprintf(p2, sizeof p2, "%s/gsql-spool-%ld-abcdef", tmp, (long)child);
    snprintf(p3, sizeof p3, "%s/gsql-sort-%ld-abcdef", tmp, (long)getpid());
    snprintf(p4, sizeof p4, "%s/gsql-sorting-%ld", tmp, (long)child);
    mkdir(p1, 0700); mkdir(p2, 0700); mkdir(p3, 0700); mkdir(p4, 0700);
    snprintf(f1, sizeof f1, "%s/run-0", p1);
    FILE *f = fopen(f1, "w"); if (f) { fputs("x", f); fclose(f); }
    EXPECT(gs_xsort_sweep(tmp) == 2, "the sweep removed the gone process's two directories");
    struct stat st;
    EXPECT(stat(p1, &st) != 0 && stat(p2, &st) != 0, "a gone process's sort and spool are gone");
    EXPECT(stat(p3, &st) == 0, "this process's own sort is kept");
    EXPECT(stat(p4, &st) == 0, "a name not the sorter's is kept");
    rmdir(p3); rmdir(p4);
    EXPECT(entries_in(tmp) == 0, "the sweep test left %d files in %s", entries_in(tmp), tmp);
    printf("ok %ld checks, %ld mismatches\n", checks, bad);
    return bad != 0;
}

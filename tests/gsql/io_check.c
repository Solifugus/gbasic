/* The I/O layer held to its own contract (include/gsql_io.h).
 *
 *   io_check rules            the simulated layer against the five rules
 *   io_check parity DIR SEED N  one seeded operation sequence through the real
 *                             layer (in DIR) and the simulated one, every answer
 *                             and every byte required to agree
 *
 * EVERY RULE IS ASSERTED AS A DIFFERENCE. A simulated layer that always kept a
 * pending write, or always dropped one, satisfies any one-sided check -- and
 * then the crash harness built on it tests a disk that does not exist, which
 * is the risk §3.4 names. So "an unsynced write may be lost" requires it LOST
 * in some image AND KEPT in some other, and so on for each rule.
 *
 * Prints "ok N checks" on success, one MISMATCH line per failure. */

#include "gsql_io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, mismatches;

static void check(int cond, const char *what) {
    checks++;
    if (!cond) {
        mismatches++;
        printf("MISMATCH %s\n", what);
    }
}

static GsqlFile *must_open(GsqlIo *io, const char *path, int flags) {
    GsqlFile *f = NULL;
    int rc = gsql_io_open(io, path, flags, &f);
    if (rc) {
        printf("MISMATCH open %s failed with %d\n", path, rc);
        mismatches++;
        exit(1);
    }
    return f;
}

static void fill(GsqlFile *f, uint64_t off, char c, size_t n) {
    char *b = malloc(n);
    memset(b, c, n);
    int rc = gsql_io_write_at(f, off, b, n);
    free(b);
    if (rc) {
        printf("MISMATCH write failed with %d\n", rc);
        mismatches++;
    }
}

/* The byte at `off` of `path` on an image, or -1 if absent/short. */
static int byte_at(GsqlIo *img, const char *path, size_t off) {
    size_t len;
    unsigned char *c = gsql_io_sim_contents(img, path, &len);
    int r = (c && off < len) ? c[off] : -1;
    free(c);
    return r;
}

static int exists(GsqlIo *img, const char *path) {
    size_t len;
    unsigned char *c = gsql_io_sim_contents(img, path, &len);
    int r = c != NULL;
    free(c);
    return r;
}

#define IMAGES 256

/* ---- rule 1: durable only after sync ------------------------------------ */
static void rule1(void) {
    GsqlIo *io = gsql_io_sim();
    GsqlFile *f = must_open(io, "d/f", GSQL_IO_CREATE);
    gsql_io_sync_dir(io, "d");
    fill(f, 0, 'A', 10);
    gsql_io_sync(f);
    fill(f, 100, 'B', 10);
    int a_always = 1, b_kept = 0, b_lost = 0;
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, s);
        if (byte_at(img, "d/f", 0) != 'A') a_always = 0;
        if (byte_at(img, "d/f", 100) == 'B') b_kept = 1; else b_lost = 1;
        gsql_io_free(img);
    }
    check(a_always, "rule 1: a synced write survives every power loss");
    check(b_kept, "rule 1: an unsynced write is KEPT by some power loss");
    check(b_lost, "rule 1: an unsynced write is LOST by some power loss");
    GsqlIo *p = gsql_io_sim_crash_image(io, GSQL_CRASH_PROCESS, 0);
    check(byte_at(p, "d/f", 100) == 'B', "a process crash keeps the unsynced write (the page cache survived)");
    gsql_io_free(p);
    gsql_io_close(f);
    gsql_io_free(io);
}

/* ---- rule 2: names durable only after sync_dir -------------------------- */
static void rule2(void) {
    /* creation */
    GsqlIo *io = gsql_io_sim();
    GsqlFile *f = must_open(io, "d/new", GSQL_IO_CREATE);
    fill(f, 0, 'x', 4);
    gsql_io_sync(f); /* the DATA is durable; the NAME is not */
    int seen = 0, missing = 0;
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, s);
        if (exists(img, "d/new")) seen = 1; else missing = 1;
        gsql_io_free(img);
    }
    check(seen && missing, "rule 2: an unsynced creation is kept by some power loss and lost by another");
    gsql_io_sync_dir(io, "d/"); /* a trailing slash names the same directory */
    int always = 1;
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, s);
        if (byte_at(img, "d/new", 0) != 'x') always = 0;
        gsql_io_free(img);
    }
    check(always, "rule 2: after sync_dir the creation survives every power loss");
    /* sync_dir of ANOTHER directory does nothing for this one */
    GsqlFile *g = must_open(io, "e/other", GSQL_IO_CREATE);
    gsql_io_sync_dir(io, "d");
    missing = 0;
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, s);
        if (!exists(img, "e/other")) missing = 1;
        gsql_io_free(img);
    }
    check(missing, "rule 2: sync_dir of one directory does not make another's creation durable");
    gsql_io_close(g);

    /* rename over an existing file: either name's contents, never neither */
    gsql_io_sync_dir(io, "e");
    GsqlFile *t = must_open(io, "d/tmp", GSQL_IO_CREATE);
    fill(t, 0, 'N', 4);
    gsql_io_sync(t);
    gsql_io_sync_dir(io, "d");
    check(gsql_io_rename(io, "d/tmp", "d/new") == 0, "rename answers 0");
    int old_seen = 0, new_seen = 0, neither = 0;
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, s);
        int b = byte_at(img, "d/new", 0);
        if (b == 'x') old_seen = 1;
        else if (b == 'N') new_seen = 1;
        else neither = 1;
        gsql_io_free(img);
    }
    check(old_seen && new_seen, "rule 2: an unsynced rename leaves the old contents in some images and the new in others");
    check(!neither, "rule 2: a rename over a durable file never leaves the name empty");
    gsql_io_sync_dir(io, "d");
    int new_always = 1, tmp_gone = 1;
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, s);
        if (byte_at(img, "d/new", 0) != 'N') new_always = 0;
        if (exists(img, "d/tmp")) tmp_gone = 0;
        gsql_io_free(img);
    }
    check(new_always && tmp_gone, "rule 2: after sync_dir the rename survives every power loss");

    /* removal */
    check(gsql_io_remove(io, "d/new") == 0, "remove answers 0");
    seen = missing = 0;
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, s);
        if (exists(img, "d/new")) seen = 1; else missing = 1;
        gsql_io_free(img);
    }
    check(seen && missing, "rule 2: an unsynced removal is undone by some power loss and kept by another");
    gsql_io_close(f);
    gsql_io_close(t);
    gsql_io_free(io);
}

/* ---- rule 3: atomic per 512-byte sector, at absolute offsets ------------ */
static void rule3(void) {
    GsqlIo *io = gsql_io_sim();
    GsqlFile *f = must_open(io, "d/t", GSQL_IO_CREATE);
    gsql_io_sync_dir(io, "d");
    fill(f, 0, 'O', 2048);
    gsql_io_sync(f);
    /* 1536 bytes at offset 256 span FOUR sectors: [256,512) [512,1024)
     * [1024,1536) [1536,1792). Counted from the write's own start it would be
     * three, ending at 768, 1280 -- which this check would see. */
    fill(f, 256, 'N', 1536);
    int seen[2049];
    memset(seen, 0, sizeof seen);
    int bad = 0;
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_TORN, s);
        size_t len;
        unsigned char *c = gsql_io_sim_contents(img, "d/t", &len);
        if (!c || len < 2048) {
            /* The file is durable, so it must be there whole; and a checker
             * that dereferenced the absence would crash and hide every
             * mismatch it had already found. */
            bad = 1;
            free(c);
            gsql_io_free(img);
            continue;
        }
        /* The new bytes must be a PREFIX of the write: [256, b) new, the rest old. */
        size_t b = 256;
        while (b < 1792 && c[b] == 'N') b++;
        for (size_t i = 0; i < 2048; i++) {
            char want = (i >= 256 && i < b) ? 'N' : 'O';
            if (c[i] != want) bad = 1;
        }
        seen[b] = 1;
        free(c);
        gsql_io_free(img);
    }
    check(!bad, "rule 3: a torn write leaves a PREFIX of its sectors new and the rest old");
    check(seen[256] && seen[512] && seen[1024] && seen[1536],
          "rule 3: every prefix of the four sectors is produced (0, 1, 2 and 3 kept)");
    check(!seen[768] && !seen[1280] && !seen[1792],
          "rule 3: tears fall on ABSOLUTE 512-byte boundaries, and torn mode never keeps the whole write");
    int whole = 0;
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, s);
        if (byte_at(img, "d/t", 1791) == 'N' && byte_at(img, "d/t", 256) == 'N') whole = 1;
        gsql_io_free(img);
    }
    check(whole, "rule 3's control: a power loss without a tear applies the whole write");

    /* A single-sector write cannot tear. */
    GsqlIo *io2 = gsql_io_sim();
    GsqlFile *g = must_open(io2, "d/s", GSQL_IO_CREATE);
    gsql_io_sync_dir(io2, "d");
    fill(g, 0, 'O', 512);
    gsql_io_sync(g);
    fill(g, 0, 'N', 512);
    int partial = 0;
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io2, GSQL_CRASH_TORN, s);
        int a = byte_at(img, "d/s", 0), z = byte_at(img, "d/s", 511);
        if (a != z) partial = 1;
        gsql_io_free(img);
    }
    check(!partial, "rule 3: a write within one sector is atomic");
    gsql_io_close(g);
    gsql_io_free(io2);
    gsql_io_close(f);
    gsql_io_free(io);
}

/* ---- rule 4: any subset, any order -------------------------------------- */
static void rule4(void) {
    GsqlIo *io = gsql_io_sim();
    GsqlFile *f = must_open(io, "d/r", GSQL_IO_CREATE);
    gsql_io_sync_dir(io, "d");
    fill(f, 0, '.', 1024);
    gsql_io_sync(f);
    fill(f, 0, 'A', 10);   /* first */
    fill(f, 500, 'B', 10); /* second, elsewhere */
    int combo[4] = {0, 0, 0, 0};
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, s);
        int a = byte_at(img, "d/r", 0) == 'A', b = byte_at(img, "d/r", 500) == 'B';
        combo[a * 2 + b] = 1;
        gsql_io_free(img);
    }
    check(combo[0] && combo[1] && combo[2] && combo[3],
          "rule 4: every subset of two pending writes survives some power loss (incl. the LATER one alone)");

    /* Order: X [0,100) then Y [50,150). Applied in order, [50,100) is Y's; only
     * the REVERSED order leaves it X's while [100,150) is Y's. */
    GsqlIo *io2 = gsql_io_sim();
    GsqlFile *g = must_open(io2, "d/o", GSQL_IO_CREATE);
    gsql_io_sync_dir(io2, "d");
    fill(g, 0, '.', 200);
    gsql_io_sync(g);
    fill(g, 0, 'X', 100);
    fill(g, 50, 'Y', 100);
    int in_order = 0, reversed = 0;
    for (uint64_t s = 0; s < IMAGES; s++) {
        GsqlIo *img = gsql_io_sim_crash_image(io2, GSQL_CRASH_POWER, s);
        int mid = byte_at(img, "d/o", 75), tail = byte_at(img, "d/o", 125), head = byte_at(img, "d/o", 25);
        if (head == 'X' && mid == 'Y' && tail == 'Y') in_order = 1;
        if (head == 'X' && mid == 'X' && tail == 'Y') reversed = 1;
        gsql_io_free(img);
    }
    check(in_order && reversed, "rule 4: two overlapping pending writes reach the disk in either order");
    GsqlIo *p = gsql_io_sim_crash_image(io2, GSQL_CRASH_PROCESS, 0);
    check(byte_at(p, "d/o", 75) == 'Y', "a process crash applies pending writes IN ORDER");
    gsql_io_free(p);
    gsql_io_close(g);
    gsql_io_free(io2);
    gsql_io_close(f);
    gsql_io_free(io);
}

/* ---- rule 5: after a failed sync, unknown ------------------------------- */
static void rule5(void) {
    int kept = 0, lost = 0, view_new = 1, reported = 1, later_ok = 1;
    for (uint64_t seed = 0; seed < 64; seed++) {
        GsqlIo *io = gsql_io_sim();
        gsql_io_sim_seed(io, seed);
        GsqlFile *f = must_open(io, "d/w", GSQL_IO_CREATE);
        gsql_io_sync_dir(io, "d");
        fill(f, 0, 'O', 16);
        gsql_io_sync(f);
        fill(f, 0, 'W', 16);
        gsql_io_sim_fail(io, GSQL_IO_FAIL_SYNC, 1);
        if (gsql_io_sync(f) != EIO) reported = 0;
        /* The trap fsyncgate named: a retry "succeeds" and proves nothing. */
        if (gsql_io_sync(f) != 0) later_ok = 0;
        char b = 0;
        size_t got;
        gsql_io_read_at(f, 0, &b, 1, &got);
        if (b != 'W') view_new = 0;
        GsqlIo *img = gsql_io_sim_crash_image(io, GSQL_CRASH_POWER, seed);
        if (byte_at(img, "d/w", 0) == 'W') kept = 1; else lost = 1;
        gsql_io_free(img);
        gsql_io_close(f);
        gsql_io_free(io);
    }
    check(reported, "rule 5: an injected sync failure answers EIO");
    check(later_ok && view_new, "rule 5: the retry succeeds and the process still reads the new data");
    check(kept && lost,
          "rule 5: after a failed sync AND a successful retry, the write is on disk in some runs and LOST in others");
}

/* ---- the rest of the contract ------------------------------------------- */
static void contract(void) {
    GsqlIo *io = gsql_io_sim();
    GsqlFile *f = must_open(io, "d/c", GSQL_IO_CREATE);
    fill(f, 0, 'z', 10);
    char buf[32];
    size_t got = 99;
    check(gsql_io_read_at(f, 5, buf, 32, &got) == 0 && got == 5, "a read past the end answers SHORT, not an error");
    check(gsql_io_read_at(f, 50, buf, 4, &got) == 0 && got == 0, "a read wholly past the end answers 0 bytes");

    gsql_io_sim_fail(io, GSQL_IO_FAIL_WRITE, 1);
    check(gsql_io_write_at(f, 0, "QQ", 2) == ENOSPC, "an injected full disk answers ENOSPC");
    check(gsql_io_read_at(f, 0, buf, 1, &got) == 0 && buf[0] == 'z', "... and changes nothing");
    check(gsql_io_write_at(f, 0, "QQ", 2) == 0, "the fault fires once");
    gsql_io_sim_fail(io, GSQL_IO_FAIL_READ, 2);
    check(gsql_io_read_at(f, 0, buf, 1, &got) == 0, "a read fault armed for the 2nd read spares the 1st");
    check(gsql_io_read_at(f, 0, buf, 1, &got) == EIO, "... and fails the 2nd with EIO");
    check(!gsql_io_sim_fault_armed(io, GSQL_IO_FAIL_READ), "a fired fault is no longer armed");

    GsqlFile *dup = NULL;
    check(gsql_io_open(io, "d/c", GSQL_IO_CREATE | GSQL_IO_EXCLUSIVE, &dup) == EEXIST,
          "an exclusive create of an existing file answers EEXIST");
    check(gsql_io_open(io, "d/none", 0, &dup) == ENOENT, "opening a missing file without CREATE answers ENOENT");
    GsqlFile *ro = must_open(io, "d/c", GSQL_IO_READ_ONLY);
    check(gsql_io_write_at(ro, 0, "x", 1) == EBADF, "writing a read-only handle answers EBADF");
    GsqlFile *f2 = must_open(io, "d/c", 0);
    check(gsql_io_lock(f, 1) == 0, "the first lock is taken");
    check(gsql_io_lock(f2, 1) == EWOULDBLOCK, "a second holder is refused");
    gsql_io_close(f);
    check(gsql_io_lock(f2, 1) == 0, "closing the holder releases the lock");
    check(gsql_io_rename(io, "d/c", "e/c") == EXDEV, "the simulated layer refuses a cross-directory rename");
    check(gsql_io_rename(io, "d/none", "d/x") == ENOENT, "renaming a missing file answers ENOENT");
    check(gsql_io_remove(io, "d/none") == ENOENT, "removing a missing file answers ENOENT");
    uint64_t sz = 0;
    gsql_io_truncate(f2, 3);
    gsql_io_truncate(f2, 8);
    gsql_io_size(f2, &sz);
    check(sz == 8, "truncate shrinks then grows");
    check(gsql_io_read_at(f2, 3, buf, 5, &got) == 0 && got == 5 && memcmp(buf, "\0\0\0\0\0", 5) == 0,
          "growing by truncate reads back as zeros, not the bytes cut off before");
    gsql_io_close(ro);
    gsql_io_close(f2);

    /* The process image IS the view. */
    GsqlIo *p = gsql_io_sim_crash_image(io, GSQL_CRASH_PROCESS, 0);
    size_t la, lb;
    unsigned char *a = gsql_io_sim_contents(io, "d/c", &la), *b = gsql_io_sim_contents(p, "d/c", &lb);
    check(a && b && la == lb && memcmp(a, b, la) == 0, "a process-crash image equals what the process saw");
    check(gsql_io_sim_file_count(p) == gsql_io_sim_file_count(io), "... file for file");
    free(a);
    free(b);
    gsql_io_free(p);
    gsql_io_free(io);

    /* Crash at an operation: it and everything after have no effect. */
    GsqlIo *c = gsql_io_sim();
    GsqlFile *h = must_open(c, "d/k", GSQL_IO_CREATE); /* op 1 */
    fill(h, 0, 'a', 4);                                  /* op 2 */
    gsql_io_sim_crash_at(c, 4);
    check(gsql_io_write_at(h, 0, "b", 1) == 0, "the operation before the crash point happens"); /* op 3 */
    check(gsql_io_write_at(h, 1, "c", 1) == GSQL_IO_CRASHED, "the crash-point operation answers CRASHED");
    check(gsql_io_sync(h) == GSQL_IO_CRASHED && gsql_io_sim_crashed(c), "... and so does everything after");
    unsigned char *k = gsql_io_sim_contents(c, "d/k", &la);
    check(k && la == 4 && memcmp(k, "baaa", 4) == 0, "nothing at or after the crash point reached even the view");
    free(k);
    gsql_io_close(h);
    gsql_io_free(c);

    /* Determinism: one seed, one disk; different seeds, different disks. */
    GsqlIo *d = gsql_io_sim();
    GsqlFile *m = must_open(d, "d/m", GSQL_IO_CREATE);
    for (int i = 0; i < 8; i++) fill(m, (uint64_t)i * 700, (char)('a' + i), 900);
    int same = 1, differ = 0;
    unsigned char *first = NULL;
    size_t flen = 0;
    for (uint64_t s = 0; s < 32; s++) {
        GsqlIo *i1 = gsql_io_sim_crash_image(d, GSQL_CRASH_TORN, s);
        GsqlIo *i2 = gsql_io_sim_crash_image(d, GSQL_CRASH_TORN, s);
        size_t l1, l2;
        unsigned char *x1 = gsql_io_sim_contents(i1, "d/m", &l1), *x2 = gsql_io_sim_contents(i2, "d/m", &l2);
        if (l1 != l2 || (l1 && memcmp(x1, x2, l1) != 0)) same = 0;
        if (!first) {
            first = x1 ? x1 : malloc(1);
            flen = l1;
            x1 = NULL;
        } else if (l1 != flen || (l1 && memcmp(first, x1, l1) != 0)) {
            differ = 1;
        }
        free(x1);
        free(x2);
        gsql_io_free(i1);
        gsql_io_free(i2);
    }
    free(first);
    check(same, "the same mode and seed build the same disk");
    check(differ, "different seeds build different disks");
    gsql_io_close(m);
    gsql_io_free(d);
}

/* ---- parity: the real layer and the simulated one agree ----------------- */

static uint64_t splitmix(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

#define NAMES 4
#define SLOTS 6

static int parity(const char *dir, uint64_t seed, unsigned ops) {
    GsqlIo *io[2] = {gsql_io_real(), gsql_io_sim()};
    GsqlFile *h[2][SLOTS];
    int ro[SLOTS];
    memset(h, 0, sizeof h);
    memset(ro, 0, sizeof ro);
    char paths[NAMES][512];
    for (int i = 0; i < NAMES; i++) snprintf(paths[i], sizeof paths[i], "%s/f%d", dir, i);
    uint64_t rng = seed;
    unsigned char buf[2][5000], data[5000];
    unsigned agreed = 0, errors = 0;
    unsigned long long bytes_read = 0;
    for (unsigned op = 1; op <= ops; op++) {
        unsigned kind = (unsigned)(splitmix(&rng) % 12);
        int slot = (int)(splitmix(&rng) % SLOTS);
        int name = (int)(splitmix(&rng) % NAMES), name2 = (int)(splitmix(&rng) % NAMES);
        uint64_t off = splitmix(&rng) % 6000;
        size_t n = (size_t)(splitmix(&rng) % 4000);
        int rc[2] = {0, 0};
        size_t got[2] = {0, 0};
        uint64_t sz[2] = {0, 0};
        char what[96];
        if (kind == 0 || (h[0][slot] == NULL && kind < 8)) {
            if (h[0][slot]) { gsql_io_close(h[0][slot]); gsql_io_close(h[1][slot]); h[0][slot] = h[1][slot] = NULL; }
            int choice = (int)(splitmix(&rng) % 4);
            int flags = choice == 0 ? 0 : choice == 1 ? GSQL_IO_CREATE
                      : choice == 2 ? GSQL_IO_CREATE | GSQL_IO_EXCLUSIVE : GSQL_IO_READ_ONLY;
            for (int k = 0; k < 2; k++) {
                rc[k] = gsql_io_open(io[k], paths[name], flags, &h[k][slot]);
                if (rc[k]) h[k][slot] = NULL;
            }
            ro[slot] = flags == GSQL_IO_READ_ONLY;
            if ((h[0][slot] == NULL) != (h[1][slot] == NULL)) {
                /* one opened and the other did not: close it so the slots agree */
                for (int k = 0; k < 2; k++) if (h[k][slot]) { gsql_io_close(h[k][slot]); h[k][slot] = NULL; }
            }
            snprintf(what, sizeof what, "open f%d flags %d", name, flags);
        } else if (kind == 1 || kind == 2) {
            if (ro[slot]) continue;
            for (size_t i = 0; i < n; i++) data[i] = (unsigned char)splitmix(&rng);
            for (int k = 0; k < 2; k++) rc[k] = gsql_io_write_at(h[k][slot], off, data, n);
            snprintf(what, sizeof what, "write_at %llu+%zu", (unsigned long long)off, n);
        } else if (kind == 3 || kind == 4) {
            for (int k = 0; k < 2; k++) rc[k] = gsql_io_read_at(h[k][slot], off, buf[k], n, &got[k]);
            snprintf(what, sizeof what, "read_at %llu+%zu", (unsigned long long)off, n);
            checks++;
            if (got[0] != got[1] || memcmp(buf[0], buf[1], got[0]) != 0) {
                printf("MISMATCH parity seed=%llu op=%u %s: real read %zu bytes, simulated %zu (or the bytes differ)\n",
                       (unsigned long long)seed, op, what, got[0], got[1]);
                mismatches++;
            }
        } else if (kind == 5) {
            for (int k = 0; k < 2; k++) rc[k] = gsql_io_size(h[k][slot], &sz[k]);
            snprintf(what, sizeof what, "size");
            checks++;
            if (sz[0] != sz[1]) {
                printf("MISMATCH parity seed=%llu op=%u size: real %llu, simulated %llu\n",
                       (unsigned long long)seed, op, (unsigned long long)sz[0], (unsigned long long)sz[1]);
                mismatches++;
            }
        } else if (kind == 6) {
            if (ro[slot]) continue;
            for (int k = 0; k < 2; k++) rc[k] = gsql_io_truncate(h[k][slot], off);
            snprintf(what, sizeof what, "truncate %llu", (unsigned long long)off);
        } else if (kind == 7) {
            for (int k = 0; k < 2; k++) rc[k] = gsql_io_sync(h[k][slot]);
            snprintf(what, sizeof what, "sync");
        } else if (kind == 8) {
            for (int k = 0; k < 2; k++) rc[k] = gsql_io_rename(io[k], paths[name], paths[name2]);
            snprintf(what, sizeof what, "rename f%d f%d", name, name2);
        } else if (kind == 9) {
            for (int k = 0; k < 2; k++) rc[k] = gsql_io_remove(io[k], paths[name]);
            snprintf(what, sizeof what, "remove f%d", name);
        } else if (kind == 10) {
            for (int k = 0; k < 2; k++) rc[k] = gsql_io_sync_dir(io[k], dir);
            snprintf(what, sizeof what, "sync_dir");
        } else {
            if (!h[0][slot]) continue;
            for (int k = 0; k < 2; k++) rc[k] = gsql_io_lock(h[k][slot], 1);
            snprintf(what, sizeof what, "lock");
        }
        checks++;
        agreed++;
        if (rc[0] && rc[0] == rc[1]) errors++;
        bytes_read += got[0] < got[1] ? got[0] : got[1];
        if (rc[0] != rc[1]) {
            printf("MISMATCH parity seed=%llu op=%u %s: real answered %d, simulated %d\n",
                   (unsigned long long)seed, op, what, rc[0], rc[1]);
            mismatches++;
        }
    }
    for (int s = 0; s < SLOTS; s++)
        for (int k = 0; k < 2; k++) if (h[k][s]) gsql_io_close(h[k][s]);
    /* Every file, byte for byte, through the real layer and the simulated one. */
    for (int i = 0; i < NAMES; i++) {
        GsqlFile *r = NULL;
        int rr = gsql_io_open(io[0], paths[i], GSQL_IO_READ_ONLY, &r);
        size_t sl;
        unsigned char *sc = gsql_io_sim_contents(io[1], paths[i], &sl);
        checks++;
        if (rr != 0) {
            if (sc) {
                printf("MISMATCH parity seed=%llu: f%d exists only in the simulated layer\n", (unsigned long long)seed, i);
                mismatches++;
            }
        } else {
            uint64_t rs = 0;
            gsql_io_size(r, &rs);
            unsigned char *rc2 = malloc(rs ? rs : 1);
            size_t g = 0;
            gsql_io_read_at(r, 0, rc2, (size_t)rs, &g);
            if (!sc || sl != g || memcmp(rc2, sc, g) != 0) {
                printf("MISMATCH parity seed=%llu: f%d differs (real %zu bytes, simulated %zu)\n",
                       (unsigned long long)seed, i, g, sl);
                mismatches++;
            }
            free(rc2);
            gsql_io_close(r);
        }
        free(sc);
    }
    gsql_io_free(io[0]);
    gsql_io_free(io[1]);
    /* A census, so a runner can floor it: a sequence that never met an error
     * or never read a byte back would agree about nothing worth agreeing on. */
    printf("census errors=%u bytes_read=%llu\n", errors, bytes_read);
    return (int)agreed;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "rules") == 0) {
        rule1();
        rule2();
        rule3();
        rule4();
        rule5();
        contract();
    } else if (argc == 5 && strcmp(argv[1], "parity") == 0) {
        int done = parity(argv[2], strtoull(argv[3], NULL, 10), (unsigned)strtoul(argv[4], NULL, 10));
        printf("ops %d\n", done);
    } else {
        fprintf(stderr, "usage: io_check rules | io_check parity DIR SEED OPS\n");
        return 2;
    }
    if (mismatches) {
        printf("%u of %u checks failed\n", mismatches, checks);
        return 1;
    }
    printf("ok %u checks\n", checks);
    return 0;
}

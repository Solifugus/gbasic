/* 2.C's on-disk format, held to what docs/gsql_storage_design.md §3.1.5
 * fixes (2026-10-29). Three modes:
 *
 *   format_test write DIR     write the fixture database (tests/gsql/format1/
 *                             is one, committed: tools/make_gsql_format_fixture.sh)
 *   format_test read DIR      open a COPY of it and check every value: a build
 *                             that cannot is a build that broke the format
 *   format_test refuse DIR    in scratch databases under DIR, the refusals the
 *                             format exists to make, each beside its control
 *   format_test busy DIR      a store through flushes, compactions, blob
 *                             relocation and reopens, for format_check.py
 *
 * Self-checking: prints one MISMATCH per failure and `ok N checks` last. */

#define _POSIX_C_SOURCE 200809L
#include "store_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const GsEngine *E = &gs_engine_lsm;
static unsigned checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("MISMATCH "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the fixture's contents, which `read` checks and `write` makes ------- */

#define NKEYS 200
static void big_value(unsigned char *v, size_t n, unsigned salt) {
    for (size_t i = 0; i < n; i++) v[i] = (unsigned char)(i * 7 + salt);
}

static int put(GsTxn *w, const char *tree, const char *k, const void *v, size_t vn) {
    return E->put(w, gss(tree), gss(k), gsb(v, vn), 0);
}

/* Options the fixture is written AND read with: a memtable small enough
 * that the zero-filled log stays small, and values of 64 bytes or more in
 * blob files, so every file kind is in it. */
static GsOptions fixture_options(int create) {
    GsOptions o = {create, 512, 0, NULL, 4096, 0, 64, 0};
    return o;
}

static int write_fixture(const char *dir) {
    GsOptions o = fixture_options(1);
    GsStore *db;
    int rc = E->open(dir, &o, &db);
    if (rc) { printf("MISMATCH creating %s answered %s\n", dir, gs_status_name(rc)); return 1; }
    GsTxn *w;
    E->begin_write(db, &w);
    E->tree_create(w, gss("t"));
    E->tree_create(w, gss("u"));
    char k[16], v[16];
    for (unsigned i = 0; i < NKEYS && rc == GS_OK; i++) {
        snprintf(k, sizeof k, "k%03u", i);
        snprintf(v, sizeof v, "v%03u", i);
        rc = put(w, "t", k, v, strlen(v));
    }
    unsigned char big[300];
    big_value(big, sizeof big, 1);
    if (rc == GS_OK) rc = put(w, "u", "big", big, sizeof big);
    if (rc == GS_OK) rc = put(w, "u", "small", "s", 1);
    if (rc == GS_OK) rc = E->commit(w, 1, NULL);
    /* into sorted and blob files */
    if (rc == GS_OK) rc = E->flush(db);
    /* and a commit left in the log, which the next open replays */
    if (rc == GS_OK) {
        E->begin_write(db, &w);
        rc = E->del(w, gss("t"), gss("k050"));
        if (rc == GS_OK) rc = put(w, "t", "k010", "changed", 7);
        unsigned char late[200];
        big_value(late, sizeof late, 2);
        if (rc == GS_OK) rc = put(w, "u", "late", late, sizeof late);
        if (rc == GS_OK) rc = E->commit(w, 1, NULL);
    }
    int crc = E->close(db);
    if (rc || crc) { printf("MISMATCH writing the fixture answered %s\n", gs_status_name(rc ? rc : crc)); return 1; }
    printf("ok fixture written\n");
    return 0;
}

static int get(GsTxn *t, const char *tree, const char *k, GsBuf *out, int *found) {
    GsBytes b;
    int rc = E->get(t, gss(tree), gss(k), &b);
    *found = rc == GS_OK;
    if (rc == GS_OK) gsbuf_set(out, b.p, b.n);
    return rc == GS_NOT_FOUND ? GS_OK : rc;
}

static void say(void *ctx, const char *line) { (void)ctx; printf("check: %s\n", line); }

static void read_fixture(const char *dir) {
    GsOptions o = fixture_options(0);
    GsStore *db;
    int rc = E->open(dir, &o, &db);
    CHECK(rc == GS_OK, "opening the committed format-1 database answered %s", gs_status_name(rc));
    if (rc) return;
    GsStats st;
    E->stats(db, &st);
    CHECK(st.replayed >= 1, "the fixture's last commit is in its log (replayed %llu records)", (unsigned long long)st.replayed);
    GsTxn *t;
    E->begin_read(db, &t);
    GsBuf v = {0};
    int found;
    char k[16], want[16];
    unsigned present = 0;
    for (unsigned i = 0; i < NKEYS; i++) {
        snprintf(k, sizeof k, "k%03u", i);
        rc = get(t, "t", k, &v, &found);
        if (i == 50) { CHECK(rc == GS_OK && !found, "k050 was deleted in the log"); continue; }
        if (i == 10) snprintf(want, sizeof want, "changed");
        else snprintf(want, sizeof want, "v%03u", i);
        int ok = rc == GS_OK && found && v.n == strlen(want) && !memcmp(v.p, want, v.n);
        CHECK(ok, "t %s reads back as %s", k, want);
        present += ok;
    }
    CHECK(present == NKEYS - 1, "%u of %u rows of t", present, NKEYS - 1);
    unsigned char big[300], late[200];
    big_value(big, sizeof big, 1);
    big_value(late, sizeof late, 2);
    rc = get(t, "u", "big", &v, &found);
    CHECK(rc == GS_OK && found && v.n == sizeof big && !memcmp(v.p, big, v.n), "u big: the value from the blob file");
    rc = get(t, "u", "small", &v, &found);
    CHECK(rc == GS_OK && found && v.n == 1 && v.p[0] == 's', "u small: the inline value");
    rc = get(t, "u", "late", &v, &found);
    CHECK(rc == GS_OK && found && v.n == sizeof late && !memcmp(v.p, late, v.n), "u late: the value from the log");
    gsbuf_free(&v);
    E->abort(t);
    CHECK(E->check(db, say, NULL) == GS_OK, "the engine's own check passes on the fixture");
    E->close(db);
}

/* ---- raw file editing, for the refusals ---------------------------------- */

static unsigned char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *p = malloc(sz > 0 ? (size_t)sz : 1);
    *n = fread(p, 1, (size_t)sz, f);
    fclose(f);
    return p;
}
static int spit(const char *path, const unsigned char *p, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    int bad = fwrite(p, 1, n, f) != n;
    return fclose(f) || bad;
}

typedef struct { char names[16][64]; int n; const char *suffix; } Found;
static int find_each(void *ctx, const char *name) {
    Found *f = ctx;
    size_t a = strlen(name), b = strlen(f->suffix);
    if (a > b && !strcmp(name + a - b, f->suffix) && f->n < 16) snprintf(f->names[f->n++], 64, "%s", name);
    return 0;
}
/* The first file in `dir` ending in `suffix`, as a path; "" if none. */
static void first_file(const char *dir, const char *suffix, char *out, size_t cap) {
    Found f = {.n = 0, .suffix = suffix};
    GsqlIo *io = gsql_io_real();
    gsql_io_list(io, dir, find_each, &f);
    gsql_io_free(io);
    if (f.n) snprintf(out, cap, "%s/%s", dir, f.names[0]);
    else out[0] = 0;
}

/* One tree "t" holding k -> `value`, flushed, so it is in a sorted file. */
static int one_row_db(const char *dir, const char *value) {
    GsOptions o = {1, 512, 0, NULL, 0, 0, 0, 0};
    GsStore *db;
    int rc = E->open(dir, &o, &db);
    if (rc) return rc;
    GsTxn *w;
    E->begin_write(db, &w);
    E->tree_create(w, gss("t"));
    rc = put(w, "t", "k", value, strlen(value));
    if (rc == GS_OK) rc = E->commit(w, 1, NULL);
    if (rc == GS_OK) rc = E->flush(db);
    int crc = E->close(db);
    return rc ? rc : crc;
}

/* What reading k answers: the status, and the value into `out`. */
static int read_k(const char *dir, char *out, size_t cap) {
    GsOptions o = {0, 0, 0, NULL, 0, 0, 0, 0};
    GsStore *db;
    int rc = E->open(dir, &o, &db);
    if (rc) return rc;
    GsTxn *t;
    E->begin_read(db, &t);
    GsBytes v;
    rc = E->get(t, gss("t"), gss("k"), &v);
    if (rc == GS_OK) snprintf(out, cap, "%.*s", (int)v.n, (const char *)v.p);
    E->abort(t);
    E->close(db);
    return rc;
}

/* Rewrite a sorted file's header: `edit` changes it, the checksum is redone. */
static void edit_sst_header(const char *path, int major, int flags) {
    size_t n;
    unsigned char *p = slurp(path, &n);
    if (!p || n < 64) { printf("MISMATCH %s is not there to edit\n", path); free(p); return; }
    if (major >= 0) put16(p + 8, (uint16_t)major);
    if (flags >= 0) p[13] = (unsigned char)flags;
    put32(p + 60, crc32c(p, 60));
    spit(path, p, n);
    free(p);
}

/* The manifest: its header checksum at 76 covers [0,76), the whole file's
 * at the end covers the rest. */
static void edit_manifest(const char *path, int major, int minor, int flags, size_t extra) {
    size_t n;
    unsigned char *p = slurp(path, &n);
    if (!p || n < 120) { printf("MISMATCH %s is not there to edit\n", path); free(p); return; }
    p = realloc(p, n + extra);
    if (extra) { memmove(p + n - 4 + extra, p + n - 4, 4); memset(p + n - 4, 0xAB, extra); n += extra; }
    if (major >= 0) put16(p + 8, (uint16_t)major);
    if (minor >= 0) put16(p + 10, (uint16_t)minor);
    if (flags >= 0) put32(p + 40, (uint32_t)flags);
    put32(p + 76, crc32c(p, 76));
    put32(p + n - 4, crc32c(p, n - 4));
    spit(path, p, n);
    free(p);
}

static void refusals(const char *base) {
    char a[600], b[600], c[600], sa[700], sb[700], got[64];
    snprintf(a, sizeof a, "%s/a", base);
    snprintf(b, sizeof b, "%s/b", base);
    snprintf(c, sizeof c, "%s/c", base);
    int rc = one_row_db(a, "from-a");
    CHECK(rc == GS_OK, "building database a answered %s", gs_status_name(rc));
    rc = one_row_db(b, "from-b");
    CHECK(rc == GS_OK, "building database b answered %s", gs_status_name(rc));
    if (failures) return;

    /* (1) Another database's file under this one's name. Both were built
     * the same way, so a's sorted file has the number b's manifest names --
     * and without the identity in the header, b reads a's value as its own,
     * an ordinary-looking answer. The control: b's own file reads b's. */
    first_file(a, ".sst", sa, sizeof sa);
    first_file(b, ".sst", sb, sizeof sb);
    CHECK(sa[0] && sb[0] && !strcmp(strrchr(sa, '/'), strrchr(sb, '/')), "a and b have a sorted file of the same number (%s, %s)", sa, sb);
    size_t na, nb;
    unsigned char *pa = slurp(sa, &na), *pb = slurp(sb, &nb);
    rc = read_k(b, got, sizeof got);
    CHECK(rc == GS_OK && !strcmp(got, "from-b"), "control: b reads its own row (%s, %s)", gs_status_name(rc), rc ? "" : got);
    spit(sb, pa, na);
    rc = read_k(b, got, sizeof got);
    CHECK(rc == GS_CORRUPT, "b holding a's sorted file is refused as CORRUPT, not read (answered %s%s%s)",
          gs_status_name(rc), rc ? "" : " with ", rc ? "" : got);
    spit(sb, pb, nb);
    rc = read_k(b, got, sizeof got);
    CHECK(rc == GS_OK && !strcmp(got, "from-b"), "b's own file put back reads again (%s)", gs_status_name(rc));
    free(pa);
    free(pb);

    /* (2) This database's file under another number: the header names the
     * number it was written as. */
    {
        size_t n;
        unsigned char *p = slurp(sb, &n);
        if (p && n >= 64) { put64(p + 32, get64(p + 32) + 1000); put32(p + 60, crc32c(p, 60)); spit(sb, p, n); }
        free(p);
        rc = read_k(b, got, sizeof got);
        CHECK(rc == GS_CORRUPT, "a sorted file whose header names another number is CORRUPT (answered %s)", gs_status_name(rc));
    }

    /* (3) A sealed sorted file, and a newer major: FORMAT, which says "not
     * this build's" where CORRUPT says "damaged". The checksum is redone, so
     * only the field can be what is refused. */
    rc = one_row_db(c, "from-c");
    first_file(c, ".sst", sb, sizeof sb);
    size_t n0;
    unsigned char *orig = slurp(sb, &n0);
    edit_sst_header(sb, -1, 1);
    rc = read_k(c, got, sizeof got);
    CHECK(rc == GS_FORMAT, "a sorted file flagged encrypted is FORMAT (answered %s)", gs_status_name(rc));
    spit(sb, orig, n0);
    edit_sst_header(sb, GS_FORMAT_MAJOR + 1, -1);
    rc = read_k(c, got, sizeof got);
    CHECK(rc == GS_FORMAT, "a sorted file of a newer major is FORMAT (answered %s)", gs_status_name(rc));
    spit(sb, orig, n0);
    rc = read_k(c, got, sizeof got);
    CHECK(rc == GS_OK && !strcmp(got, "from-c"), "control: the file restored reads (%s)", gs_status_name(rc));
    free(orig);

    /* (4) The manifest. A newer major and a sealed one are FORMAT; a newer
     * MINOR that added to the body's end opens, which is the promise the
     * minor makes -- and the same bytes added under this minor are damage. */
    char man[700];
    snprintf(man, sizeof man, "%s/db", c);
    orig = slurp(man, &n0);
    edit_manifest(man, GS_FORMAT_MAJOR + 1, -1, -1, 0);
    rc = read_k(c, got, sizeof got);
    CHECK(rc == GS_FORMAT, "a manifest of a newer major is FORMAT (answered %s)", gs_status_name(rc));
    spit(man, orig, n0);
    edit_manifest(man, -1, -1, 1, 0);
    rc = read_k(c, got, sizeof got);
    CHECK(rc == GS_FORMAT, "a manifest flagged encrypted is FORMAT (answered %s)", gs_status_name(rc));
    spit(man, orig, n0);
    edit_manifest(man, -1, GS_FORMAT_MINOR + 1, -1, 8);
    rc = read_k(c, got, sizeof got);
    CHECK(rc == GS_OK && !strcmp(got, "from-c"), "a newer minor's added fields are passed over (answered %s)", gs_status_name(rc));
    spit(man, orig, n0);
    edit_manifest(man, -1, -1, -1, 8);
    rc = read_k(c, got, sizeof got);
    CHECK(rc == GS_CORRUPT, "the same bytes under this minor are CORRUPT (answered %s)", gs_status_name(rc));
    spit(man, orig, n0);
    free(orig);

    /* (5) A log record flagged sealed: replay must not stop at it as if it
     * were the end, which would drop a durable commit in silence. */
    {
        char d[600], lp[700];
        snprintf(d, sizeof d, "%s/d", base);
        GsOptions o = {1, 512, 0, NULL, 0, 0, 0, 0};
        GsStore *db;
        rc = E->open(d, &o, &db);
        if (rc == GS_OK) {
            GsTxn *w;
            E->begin_write(db, &w);
            E->tree_create(w, gss("t"));
            rc = put(w, "t", "k", "logged", 6);
            if (rc == GS_OK) rc = E->commit(w, 1, NULL);
            E->close(db);
        }
        CHECK(rc == GS_OK, "building the log database answered %s", gs_status_name(rc));
        rc = read_k(d, got, sizeof got);
        CHECK(rc == GS_OK && !strcmp(got, "logged"), "control: the logged commit replays (%s)", gs_status_name(rc));
        first_file(d, ".log.1", lp, sizeof lp);
        size_t n;
        unsigned char *p = slurp(lp, &n);
        uint32_t len = p && n >= 8 ? get32(p) : 0;
        CHECK(len && len + 8 <= n, "the log's first record is %u bytes", len);
        if (len && len + 8 <= n) {
            p[8] = 1;
            put32(p + 4, crc32c(p + 8, len));
            spit(lp, p, n);
            rc = read_k(d, got, sizeof got);
            CHECK(rc == GS_FORMAT, "a log record flagged sealed is FORMAT, not the end of the log (answered %s)", gs_status_name(rc));
        }
        free(p);
    }
}

/* A store that has been through everything that writes a file: flushes,
 * compactions to several levels, blob relocation, logs recycled, reopens. For
 * format_check.py to decode; it checks nothing itself but that it ran. */
static int busy(const char *dir) {
    GsOptions o = {1, 512, 0, NULL, 4096, 2, 64, 0};
    GsStore *db;
    int rc = E->open(dir, &o, &db);
    uint64_t r = 0x9E3779B97F4A7C15ULL;
    unsigned char v[400];
    char k[24];
    for (int round = 0; rc == GS_OK && round < 6; round++) {
        GsTxn *w;
        if (round == 0) {
            E->begin_write(db, &w);
            E->tree_create(w, gss("a"));
            E->tree_create(w, gss("b"));
            rc = E->commit(w, 1, NULL);
        }
        for (int c = 0; rc == GS_OK && c < 300; c++) {
            E->begin_write(db, &w);
            for (int j = 0; rc == GS_OK && j < 4; j++) {
                r ^= r << 13; r ^= r >> 7; r ^= r << 17;
                snprintf(k, sizeof k, "k%04u", (unsigned)(r % 2000));
                const char *tree = r & 1 ? "a" : "b";
                if (r % 7 == 0) { rc = E->del(w, gss(tree), gss(k)); if (rc == GS_NOT_FOUND) rc = GS_OK; continue; }
                size_t n = 1 + (size_t)(r >> 20) % sizeof v;
                big_value(v, n, (unsigned)r);
                rc = put(w, tree, k, v, n);
            }
            if (rc == GS_OK) rc = E->commit(w, c % 5 == 0, NULL);
        }
        if (rc == GS_OK) rc = E->close(db);
        if (rc == GS_OK) { o.create = 0; rc = E->open(dir, &o, &db); }
    }
    if (rc == GS_OK) rc = E->close(db);
    if (rc) { printf("MISMATCH the busy store answered %s\n", gs_status_name(rc)); return 1; }
    printf("ok busy store written\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: format_test write|read|refuse DIR\n"); return 2; }
    if (!strcmp(argv[1], "write")) return write_fixture(argv[2]);
    if (!strcmp(argv[1], "busy")) return busy(argv[2]);
    if (!strcmp(argv[1], "read")) read_fixture(argv[2]);
    else if (!strcmp(argv[1], "refuse")) refusals(argv[2]);
    else { fprintf(stderr, "unknown mode %s\n", argv[1]); return 2; }
    if (!failures) printf("ok %u checks\n", checks);
    return failures != 0;
}

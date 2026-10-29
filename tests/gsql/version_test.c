/* The version store (src/gsql/version.c, §4) against a model, over the chosen
 * engine with its own commit ids.
 *
 *   version_test SEED [real DIR]
 *
 * The model holds every value every row ever had: per row, the list of
 * (start id, value or REMOVED) in id order. A random run of write
 * transactions inserts, updates and deletes rows -- some rows several times
 * in one transaction, some transactions aborted, some committed unsynced and
 * covered by a later sync, several to a second and now and then with the
 * clock stepped BACK -- as a relational layer would: the row holds its
 * current value behind the stamp that made it current, and every change
 * hands the old value to the version store. Then, in read snapshots, every
 * row is asked AS OF every id that matters (every start, every commit id,
 * one either side, 0 and the largest), and every second is mapped to an id,
 * all against the model. Halfway and at the end the store is closed and
 * reopened, so the answers are also those of history read back from sorted
 * files after compaction. The engine's txn_id is checked against the ids its
 * commits answer: past every earlier commit, at most its own.
 *
 * Prints "ok N checks" or MISMATCH lines. */

#include "gsql_version.h"
#include "store_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures <= 25) { printf("MISMATCH "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint64_t rng;
static uint64_t rnd(void) {
    uint64_t z = (rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static const GsEngine *E = &gs_engine_lsm;
#define NROWS 40
#define TABLE gss("orders")
#define COL 3

/* the row as the "relational layer" stores it: start(8) second(8) author(4) value */
#define ROW_HDR 20

typedef struct { uint64_t start; int removed; char value[40]; uint64_t second; uint32_t author; } Event;
typedef struct { Event *ev; size_t n, cap; } History;
static History hist[NROWS];

/* ids and seconds the model knows about */
static uint64_t ids[20000];
static size_t nids;
typedef struct { uint64_t second, id; } SecMark;
static SecMark secs[20000];
static size_t nsecs;

static void ev_set(History *h, Event e) {
    if (h->n && h->ev[h->n - 1].start == e.start) { h->ev[h->n - 1] = e; return; } /* again in one txn */
    if (h->n == h->cap) { h->cap = h->cap ? h->cap * 2 : 8; h->ev = realloc(h->ev, h->cap * sizeof *h->ev); }
    h->ev[h->n++] = e;
}

/* the model's answer: the last event with start <= asof */
static const Event *model_at(int row, uint64_t asof) {
    const Event *best = NULL;
    for (size_t i = 0; i < hist[row].n; i++)
        if (hist[row].ev[i].start <= asof) best = &hist[row].ev[i];
    return best && !best->removed ? best : NULL;
}

static GsBytes row_key(int r, char *buf) { return gsb(buf, (size_t)snprintf(buf, 16, "row%03d", r)); }

static int read_row(GsTxn *t, int r, GsqlStamp *st, GsBytes *val) {
    char kb[16];
    GsBytes v;
    int rc = E->get(t, gss("orders"), row_key(r, kb), &v);
    if (rc) return rc;
    if (v.n < ROW_HDR) return GS_CORRUPT;
    st->start = get64(v.p);
    st->second = get64(v.p + 8);
    st->author = get32(v.p + 16);
    *val = gsb(v.p + ROW_HDR, v.n - ROW_HDR);
    return GS_OK;
}

static void check_all(GsStore *db, const char *when) {
    GsTxn *t;
    int rc = E->begin_read(db, &t);
    CHECK(rc == GS_OK, "%s: begin_read answered %s", when, gs_status_name(rc));
    if (rc) return;
    GsqlVersionValue out = {0};
    for (int r = 0; r < NROWS; r++) {
        GsqlStamp cur, got;
        GsBytes cv = {0}, cvc;
        int have = read_row(t, r, &cur, &cv) == GS_OK;
        /* the current value must be the model's at the last id */
        const Event *now = model_at(r, UINT64_MAX);
        CHECK(have == (now != NULL), "%s: row %d current %s, model %s", when, r, have ? "present" : "absent", now ? "present" : "absent");
        /* the row's value is borrowed from the snapshot: copy it, since the
         * version store reads through the same snapshot */
        char cbuf[64];
        size_t cn = have ? cv.n : 0;
        if (cn > sizeof cbuf) cn = sizeof cbuf;
        if (cn) memcpy(cbuf, cv.p, cn);
        cvc = gsb(cbuf, cn);
        uint64_t probes[64];
        size_t np = 0;
        probes[np++] = 0;
        probes[np++] = UINT64_MAX;
        for (size_t i = 0; i < hist[r].n && np + 3 < 64; i++) {
            uint64_t s = hist[r].ev[i].start;
            probes[np++] = s - 1, probes[np++] = s, probes[np++] = s + 1;
        }
        for (size_t i = 0; i < 6; i++) probes[np++ % 64] = nids ? ids[rnd() % nids] : 0;
        if (np > 64) np = 64;
        char kb[16];
        for (size_t i = 0; i < np; i++) {
            uint64_t a = probes[i];
            const Event *m = model_at(r, a);
            rc = gsql_version_asof(E, t, TABLE, row_key(r, kb), COL, a, have ? &cur : NULL, cvc, &got, &out);
            if (!m) {
                CHECK(rc == GS_NOT_FOUND, "%s: row %d AS OF %llu: model has no value, store answered %s", when, r,
                      (unsigned long long)a, gs_status_name(rc));
            } else {
                CHECK(rc == GS_OK && out.length == strlen(m->value) && !memcmp(out.bytes, m->value, out.length) &&
                          got.start == m->start && got.second == m->second && got.author == m->author,
                      "%s: row %d AS OF %llu: model '%s' from %llu, store %s '%.*s' from %llu", when, r,
                      (unsigned long long)a, m->value, (unsigned long long)m->start, gs_status_name(rc),
                      rc ? 0 : (int)out.length, rc ? "" : (const char *)out.bytes, (unsigned long long)got.start);
            }
        }
    }
    /* every second, and one either side of the run */
    uint64_t lo = nsecs ? secs[0].second : 0, hi = nsecs ? secs[nsecs - 1].second : 0;
    for (uint64_t s = lo ? lo - 2 : 0; s <= hi + 2; s++) {
        uint64_t want = 0, got;
        for (size_t i = 0; i < nsecs; i++) if (secs[i].second <= s) want = secs[i].id;
        rc = gsql_version_at(E, t, s, &got);
        CHECK(rc == GS_OK && got == want, "%s: second %llu maps to %llu, model %llu", when, (unsigned long long)s,
              (unsigned long long)got, (unsigned long long)want);
    }
    gsql_version_value_free(&out);
    E->abort(t);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: version_test SEED [real DIR]\n"); return 2; }
    rng = strtoull(argv[1], NULL, 10);
    int real = argc >= 4 && !strcmp(argv[2], "real");
    char path[512];
    GsqlIo *io = real ? gsql_io_real() : gsql_io_sim();
    snprintf(path, sizeof path, "%s/db", real ? argv[3] : "d");
    /* a small memtable and few files per level, so history is read back from
     * sorted files that compaction has merged, not only from memory */
    GsOptions opt = {1, 1024, 256 * 1024, io, 2048, 3, 64, 0};
    GsStore *db;
    int rc = E->open(path, &opt, &db);
    CHECK(rc == GS_OK, "open answered %s", gs_status_name(rc));
    if (rc) return 1;

    /* a read snapshot cannot version, and without the trees nothing is found */
    {
        GsTxn *t;
        GsqlStamp st;
        uint64_t id = 7;
        E->begin_read(db, &t);
        CHECK(gsql_version_begin(E, t, 1, 1, &st) == GS_MISUSE, "a read snapshot cannot begin versioning");
        CHECK(gsql_version_at(E, t, 100, &id) == GS_OK && id == 0, "an empty store maps every second to 0");
        GsqlVersionValue v = {0};
        CHECK(gsql_version_asof(E, t, TABLE, gss("row000"), COL, 5, NULL, gsb("", 0), &st, &v) == GS_NOT_FOUND,
              "an empty store has no history");
        E->abort(t);
        GsEngine noid = *E;
        noid.txn_id = NULL;
        E->begin_write(db, &t);
        CHECK(gsql_version_begin(&noid, t, 1, 1, &st) == GS_MISUSE, "an engine without txn_id cannot version");
        E->abort(t);
    }

    uint64_t last_commit = 0, last_second = 0, clock = 1000000;
    int pending = 0; /* unsynced commits since the last sync */
    for (int txn = 0; txn < 1200; txn++) {
        if (txn == 600) {
            if (pending) { E->sync(db, NULL); pending = 0; }
            check_all(db, "before reopen");
            CHECK(E->close(db) == GS_OK, "close");
            rc = E->open(path, &opt, &db);
            CHECK(rc == GS_OK, "reopen answered %s", gs_status_name(rc));
            if (rc) return 1;
            check_all(db, "after reopen");
        }
        /* several commits a second; now and then the clock steps back */
        if (rnd() % 3 == 0) clock++;
        uint64_t second = clock;
        if (rnd() % 40 == 0) second = clock - 1 - rnd() % 3;
        GsTxn *w;
        rc = E->begin_write(db, &w);
        CHECK(rc == GS_OK, "begin_write answered %s", gs_status_name(rc));
        if (rc) break;
        uint32_t author = 1 + (uint32_t)(rnd() % 5);
        GsqlStamp now;
        rc = gsql_version_begin(E, w, second, author, &now);
        CHECK(rc == GS_OK, "gsql_version_begin answered %s", gs_status_name(rc));
        CHECK(now.start > last_commit, "txn %d: txn_id %llu is not past the last commit %llu", txn,
              (unsigned long long)now.start, (unsigned long long)last_commit);
        uint64_t eff = second > last_second ? second : last_second;
        CHECK(now.second == eff, "txn %d: the stamp's second %llu, want %llu (clamped)", txn,
              (unsigned long long)now.second, (unsigned long long)eff);
        int abort_it = rnd() % 12 == 0;
        Event staged[NROWS];
        int touched[NROWS] = {0};
        int ops = 1 + (int)(rnd() % 6);
        for (int o = 0; o < ops && rc == GS_OK; o++) {
            int r = (int)(rnd() % NROWS);
            if (rnd() % 4 == 0 && o) r = (int)(rnd() % 3); /* hot rows, often twice in one txn */
            char kb[16];
            GsBytes key = row_key(r, kb);
            GsqlStamp was;
            GsBytes old = gsb("", 0);
            int have = read_row(w, r, &was, &old) == GS_OK;
            char oldc[64];
            size_t on = have ? old.n : 0;
            if (on > sizeof oldc) on = sizeof oldc;
            if (on) memcpy(oldc, old.p, on);
            if (have) {
                rc = gsql_version_keep(E, w, TABLE, key, COL, &was, gsb(oldc, on), &now);
                CHECK(rc == GS_OK, "keep answered %s", gs_status_name(rc));
            }
            if (have && rnd() % 5 == 0) {
                rc = gsql_version_remove(E, w, TABLE, key, COL, &now);
                CHECK(rc == GS_OK, "remove answered %s", gs_status_name(rc));
                rc = E->del(w, gss("orders"), key);
                CHECK(rc == GS_OK, "del answered %s", gs_status_name(rc));
                staged[r] = (Event){now.start, 1, "", now.second, author};
            } else {
                Event e = {now.start, 0, "", now.second, author};
                snprintf(e.value, sizeof e.value, "v%d-%llu", txn, (unsigned long long)(rnd() % 1000));
                unsigned char row[ROW_HDR + 40];
                put64(row, now.start);
                put64(row + 8, now.second);
                put32(row + 16, author);
                size_t vn = strlen(e.value);
                memcpy(row + ROW_HDR, e.value, vn);
                rc = E->put(w, gss("orders"), key, gsb(row, ROW_HDR + vn), 0);
                if (rc == GS_NOT_FOUND) { E->tree_create(w, gss("orders")); rc = E->put(w, gss("orders"), key, gsb(row, ROW_HDR + vn), 0); }
                CHECK(rc == GS_OK, "put answered %s", gs_status_name(rc));
                staged[r] = e;
            }
            touched[r] = 1;
        }
        if (abort_it) { E->abort(w); continue; }
        int sync = rnd() % 4 != 0;
        uint64_t cid = 0;
        rc = E->commit(w, sync, &cid);
        CHECK(rc == GS_OK, "commit answered %s", gs_status_name(rc));
        if (rc) break;
        CHECK(cid >= now.start, "txn %d: commit id %llu is below its txn_id %llu", txn, (unsigned long long)cid,
              (unsigned long long)now.start);
        last_commit = cid;
        last_second = eff;
        for (int r = 0; r < NROWS; r++) if (touched[r]) ev_set(&hist[r], staged[r]);
        if (nids + 2 < sizeof ids / sizeof *ids) { ids[nids++] = now.start; ids[nids++] = cid; }
        if (nsecs && secs[nsecs - 1].second == eff) secs[nsecs - 1].id = now.start;
        else if (nsecs < sizeof secs / sizeof *secs) secs[nsecs++] = (SecMark){eff, now.start};
        pending = !sync || pending;
        if (pending && rnd() % 5 == 0) { E->sync(db, NULL); pending = 0; }
        if (!pending && txn % 150 == 149) check_all(db, "during the run");
    }
    if (pending) E->sync(db, NULL);
    check_all(db, "at the end");
    CHECK(E->close(db) == GS_OK, "close");
    rc = E->open(path, &opt, &db);
    CHECK(rc == GS_OK, "final reopen answered %s", gs_status_name(rc));
    if (rc == GS_OK) {
        check_all(db, "after the final reopen");
        CHECK(E->check(db, NULL, NULL) == GS_OK, "check after the run");
        E->close(db);
    }
    gsql_io_free(io);
    for (int r = 0; r < NROWS; r++) free(hist[r].ev);
    if (failures) { printf("FAILED %u of %u checks\n", failures, checks); return 1; }
    printf("ok %u checks\n", checks);
    return 0;
}

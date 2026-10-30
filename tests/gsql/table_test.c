/* G-SQL's table layer under test (docs/gsql_table_design.md §9;
 * tests/run_gsql_table.sh).
 *
 *   table_test kinds FILE          the type rules over values gBASIC wrote,
 *                                  verdicts by hand (tests/gsql/table_values.bas)
 *   table_test refuse              every refusal beside the case that passes
 *   table_test model SEED [DIR]    a random workload against a model; the
 *                                  simulated disk, or the real one under DIR
 *   table_test layout DIR          a fixed database, dumped as `tree key value`
 *                                  hex lines for tests/gsql/table_layout.py
 *
 * THE MODEL DOES NOT ASK THE LAYER WHAT IS ALLOWED. Each pool value carries a
 * CLASS written here, and a table written here says which column kinds take
 * which class; uniqueness is key equality, from the key encoder, which its own
 * suite holds to gBASIC (run_gsql_key.sh). Prints `ok N checks` or MISMATCH
 * lines. */

#include "gsql_key.h"
#include "gsql_store.h"
#include "gsql_table.h"
#include "serialize_tags.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const GsEngine *E = &gs_engine_lsm;
static long checks, mismatches;

static void mismatch(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    mismatches++;
    if (mismatches <= 40) {
        printf("MISMATCH ");
        vprintf(fmt, ap);
        printf("\n");
    }
    va_end(ap);
}

#define CHECK(cond, ...)                \
    do {                                \
        checks++;                       \
        if (!(cond)) mismatch(__VA_ARGS__); \
    } while (0)

static uint64_t rng = 1;
static uint64_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

/* ---- a v3 writer of the test's own, written from §3.3's table ---------- */

typedef struct {
    unsigned char b[256];
    size_t n;
} Val;

static void vb(Val *v, unsigned x) { v->b[v->n++] = (unsigned char)x; }
static void vle(Val *v, uint64_t x, int w) {
    for (int i = 0; i < w; i++) vb(v, (unsigned)(x >> (8 * i)) & 0xff);
}
static void vleb(Val *v, uint64_t x) {
    do {
        unsigned c = x & 0x7f;
        x >>= 7;
        vb(v, c | (x ? 0x80 : 0));
    } while (x);
}
static Val v_int(int64_t i) { Val v = {{0}, 0}; vb(&v, SER_NUMBER); vb(&v, 1); vle(&v, (uint64_t)i, 8); return v; }
static Val v_dbl(double d) {
    Val v = {{0}, 0};
    uint64_t bits;
    memcpy(&bits, &d, 8);
    vb(&v, SER_NUMBER); vb(&v, 0); vle(&v, bits, 8);
    return v;
}
static Val v_str(const char *s, size_t n) { Val v = {{0}, 0}; vb(&v, SER_STRING); vleb(&v, n); memcpy(v.b + v.n, s, n); v.n += n; return v; }
static Val v_bool(int b) { Val v = {{0}, 0}; vb(&v, SER_BOOL); vb(&v, (unsigned)b); return v; }
static Val v_money(int64_t units, const char *code) {
    Val v = {{0}, 0};
    /* the storage exponent is the minor unit's; the units carry four guard digits */
    vb(&v, SER_MONEY); vle(&v, (uint64_t)units, 8); vb(&v, 2); vle(&v, strcmp(code, "EUR") ? 840 : 978, 2);
    vb(&v, (unsigned)strlen(code)); memcpy(v.b + v.n, code, strlen(code)); v.n += strlen(code);
    return v;
}
static Val v_dt(int y, int mo, int d, int h, int mi, int s, int time_only, int prec) {
    Val v = {{0}, 0};
    int f[6] = {y, mo, d, h, mi, s};
    vb(&v, SER_DATETIME);
    for (int i = 0; i < 6; i++) vle(&v, (uint64_t)(uint32_t)f[i], 4);
    vb(&v, (unsigned)time_only); vb(&v, (unsigned)prec);
    return v;
}
static Val v_tag(unsigned tag) { Val v = {{0}, 0}; vb(&v, tag); return v; }
static Val v_rec1(const char *field, Val inner) {
    Val v = {{0}, 0};
    vb(&v, SER_RECORD); vleb(&v, 1); vleb(&v, strlen(field));
    memcpy(v.b + v.n, field, strlen(field)); v.n += strlen(field);
    memcpy(v.b + v.n, inner.b, inner.n); v.n += inner.n;
    return v;
}
static Val v_arr2(Val a, Val b) {
    Val v = {{0}, 0};
    vb(&v, SER_ARRAY); vleb(&v, 2);
    memcpy(v.b + v.n, a.b, a.n); v.n += a.n;
    memcpy(v.b + v.n, b.b, b.n); v.n += b.n;
    return v;
}
static Val v_dur(void) { Val v = {{0}, 0}; vb(&v, SER_DURATION); for (int i = 0; i < 7; i++) vle(&v, i == 3 ? 2 : 0, 4); return v; }

/* ---- the pool and what each kind takes ---------------------------------- */

enum { C_INT, C_FLOAT, C_STR, C_BOOL, C_USD, C_EUR, C_DATE, C_TIME, C_DT, C_REC, C_ARR, C_DUR, C_UNKNOWN, C_NOTHING, NCLASS };

/* accept[kind][class], from docs/gsql_table_design.md §3.1. An absence is
 * accepted by every kind here; NOT NULL is applied on top. */
static const char *const ACCEPT[] = {
    /*                 I F S B U E d t D r a u ? 0 */
    [GSQL_COL_NUMBER]   = "YYnnnnnnnnnnYY",
    [GSQL_COL_INTEGER]  = "YnnnnnnnnnnnYY",
    [GSQL_COL_STRING]   = "nnYnnnnnnnnnYY",
    [GSQL_COL_BOOLEAN]  = "nnnYnnnnnnnnYY",
    [GSQL_COL_MONEY]    = "nnnnYYnnnnnnYY",
    [GSQL_COL_DATE]     = "nnnnnnYnnnnnYY",
    [GSQL_COL_TIME]     = "nnnnnnnYnnnnYY",
    [GSQL_COL_DATETIME] = "nnnnnnnnYnnnYY",
    [GSQL_COL_RECORD]   = "nnnnnnnnnYnnYY",
};

typedef struct {
    Val v;
    int cls;
} PoolValue;

#define NPOOL 48
/* Pool positions the schemas name, checked by pool_build. */
enum { P_INT3 = 3, P_STR_A = 11, P_TRUE = 18, P_USD1 = 19, P_UNKNOWN = 34, P_NOTHING = 36 };
static PoolValue pool[NPOOL];
static int npool;

static void pool_add(Val v, int cls) {
    pool[npool].v = v;
    pool[npool].cls = cls;
    npool++;
}

static void pool_build(void) {
    for (int i = 0; i < 6; i++) pool_add(v_int(i), C_INT);
    pool_add(v_int(-7), C_INT);
    pool_add(v_int(9007199254740993LL), C_INT);
    pool_add(v_dbl(2.0), C_FLOAT);   /* the same key as exact 2 */
    pool_add(v_dbl(3.0), C_FLOAT);
    pool_add(v_dbl(2.5), C_FLOAT);
    const char *ss[] = {"a", "b", "c", "d"};
    for (int i = 0; i < 4; i++) pool_add(v_str(ss[i], 1), C_STR);
    pool_add(v_str("a\0b", 3), C_STR);
    pool_add(v_str("", 0), C_STR);
    pool_add(v_bool(0), C_BOOL);
    pool_add(v_bool(1), C_BOOL);
    pool_add(v_money(1000000, "USD"), C_USD);
    pool_add(v_money(2000000, "USD"), C_USD);
    pool_add(v_money(1000000, "EUR"), C_EUR);
    pool_add(v_dt(2026, 10, 29, 0, 0, 0, 0, 3), C_DATE);
    pool_add(v_dt(2026, 10, 30, 0, 0, 0, 0, 3), C_DATE);
    pool_add(v_dt(1999, 1, 1, 0, 0, 0, 0, 3), C_DATE);
    pool_add(v_dt(0, 0, 0, 13, 45, 0, 1, 5), C_TIME);
    pool_add(v_dt(0, 0, 0, 9, 0, 0, 1, 5), C_TIME);
    pool_add(v_dt(2026, 10, 29, 13, 45, 0, 0, 6), C_DT);
    pool_add(v_dt(2026, 10, 29, 14, 0, 0, 0, 5), C_DT);
    pool_add(v_rec1("x", v_int(1)), C_REC);
    pool_add(v_rec1("y", v_arr2(v_str("p", 1), v_money(5, "USD"))), C_REC);
    pool_add(v_arr2(v_int(1), v_int(2)), C_ARR);
    pool_add(v_rec1("d", v_dur()), C_DUR);   /* a record holding a duration is refused too */
    pool_add(v_dur(), C_DUR);
    pool_add(v_tag(SER_UNKNOWN), C_UNKNOWN);
    pool_add(v_tag(SER_UNKNOWN), C_UNKNOWN);
    pool_add(v_tag(SER_NULL), C_NOTHING);
    if (pool[P_INT3].cls != C_INT || pool[P_STR_A].cls != C_STR || pool[P_TRUE].cls != C_BOOL ||
        pool[P_USD1].cls != C_USD || pool[P_UNKNOWN].cls != C_UNKNOWN || pool[P_NOTHING].cls != C_NOTHING) {
        printf("MISMATCH the pool's named positions moved\n");
        exit(1);
    }
}

/* ---- the schemas the workload uses -------------------------------------- */

typedef struct {
    const char *name;
    int kind;
    const char *currency;
    int not_null;
    int def;        /* pool index of the default, -1 none */
} ColSpec;

typedef struct {
    const char *name;
    int ncols;
    ColSpec cols[8];
    int nkey;
    unsigned key[3];
} TableSpec;

static TableSpec SPECS[] = {
    {"accounts", 5,
     {{"id", GSQL_COL_INTEGER, "", 1, -1},
      {"owner", GSQL_COL_STRING, "", 1, -1},
      {"balance", GSQL_COL_MONEY, "USD", 0, P_USD1},
      {"opened", GSQL_COL_DATE, "", 0, -1},
      {"note", GSQL_COL_RECORD, "", 0, -1}},
     1, {0}},
    {"events", 4,
     {{"at", GSQL_COL_DATETIME, "", 0, -1},
      {"kind", GSQL_COL_STRING, "", 0, P_STR_A},
      {"amount", GSQL_COL_NUMBER, "", 0, -1},
      {"flag", GSQL_COL_BOOLEAN, "", 1, P_TRUE}},
     0, {0}},
    {"pairs", 4,
     {{"a", GSQL_COL_STRING, "", 0, -1},
      {"b", GSQL_COL_NUMBER, "", 0, -1},
      {"c", GSQL_COL_MONEY, "", 0, -1},
      {"t", GSQL_COL_TIME, "", 0, -1}},
     2, {0, 1}},
};
#define NSPECS 3


/* ---- the model ----------------------------------------------------------- */

typedef struct {
    char name[16];
    int ncols;
    unsigned cols[3];
    int desc[3];
    int unique;
} MIdx;

typedef struct {
    Val key;            /* the row key the layer answered */
    Val v[12];          /* one value per column, as stored */
} MRow;

typedef struct {
    int live;
    int ncols;
    ColSpec cols[12];
    int nkey;
    unsigned key[3];
    int nidx;
    MIdx idx[4];
    MRow *rows;
    int nrows, cap;
} MTable;

typedef struct {
    MTable t[NSPECS];
} Model;

static void model_copy(Model *dst, const Model *src) {
    for (int i = 0; i < NSPECS; i++) {
        free(dst->t[i].rows);
        dst->t[i] = src->t[i];
        dst->t[i].rows = malloc((size_t)(src->t[i].cap ? src->t[i].cap : 1) * sizeof(MRow));
        if (src->t[i].nrows) memcpy(dst->t[i].rows, src->t[i].rows, (size_t)src->t[i].nrows * sizeof(MRow));
    }
}

static void model_free(Model *m) {
    for (int i = 0; i < NSPECS; i++) free(m->t[i].rows), m->t[i].rows = NULL;
}

static int is_absent_v(const Val *v) { return v->n == 1 && (v->b[0] == SER_UNKNOWN || v->b[0] == SER_NULL); }

static int class_of(const Val *v) {
    for (int i = 0; i < npool; i++)
        if (pool[i].v.n == v->n && memcmp(pool[i].v.b, v->b, v->n) == 0) return pool[i].cls;
    return -1;
}

/* The model's own verdict for a value in a column. */
static int model_fits(const ColSpec *c, const Val *v) {
    int cls = class_of(v);
    if (cls < 0) return GSQL_TYPE;
    if (ACCEPT[c->kind][cls] != 'Y') return GSQL_TYPE;
    if (c->currency[0] && cls == C_EUR) return GSQL_TYPE;   /* only USD is ever declared */
    if ((cls == C_UNKNOWN || cls == C_NOTHING) && c->not_null) return GSQL_NOT_NULL;
    return GSQL_OK;
}

static void key_of(const MTable *t, const Val *vals, int ncols, const unsigned *cols, const int *desc, GsqlKey *k) {
    for (int j = 0; j < ncols; j++) {
        const ColSpec *c = &t->cols[cols[j]];
        unsigned flags = (desc && desc[j]) ? GSQL_KEY_DESC : 0;
        if (c->kind == GSQL_COL_MONEY && c->currency[0]) flags |= GSQL_KEY_MONEY_TYPED;
        gsql_key_append(k, vals[cols[j]].b, vals[cols[j]].n, NULL, flags);
    }
}

static int same_key(const MTable *t, const Val *a, const Val *b, int ncols, const unsigned *cols) {
    GsqlKey ka, kb;
    gsql_key_init(&ka);
    gsql_key_init(&kb);
    key_of(t, a, ncols, cols, NULL, &ka);
    key_of(t, b, ncols, cols, NULL, &kb);
    int same = ka.length == kb.length && memcmp(ka.bytes, kb.bytes, ka.length) == 0;
    gsql_key_free(&ka);
    gsql_key_free(&kb);
    return same;
}

static int any_absent(const MTable *t, const Val *vals, int ncols, const unsigned *cols) {
    (void)t;
    for (int j = 0; j < ncols; j++)
        if (is_absent_v(&vals[cols[j]])) return 1;
    return 0;
}

/* Would a row with `vals` collide with a row other than `self`? */
static int model_collides(const MTable *t, const Val *vals, int self) {
    for (int r = 0; r < t->nrows; r++) {
        if (r == self) continue;
        if (t->nkey && same_key(t, vals, t->rows[r].v, t->nkey, t->key)) return 1;
        for (int i = 0; i < t->nidx; i++) {
            if (!t->idx[i].unique || any_absent(t, vals, t->idx[i].ncols, t->idx[i].cols)) continue;
            if (same_key(t, vals, t->rows[r].v, t->idx[i].ncols, t->idx[i].cols)) return 1;
        }
    }
    return 0;
}

/* A value for a column: usually one its kind takes, so rows are accepted
 * often enough for duplicates and moves to happen; sometimes anything. */
static Val pick_for(const ColSpec *c) {
    /* an absence, explicitly: unknown, or a bound `nothing`, which must be
     * stored as unknown -- without this the workload stored almost none and
     * a layer keeping `nothing` passed every model check */
    if (rnd() % 10 == 0) return pool[rnd() % 3 == 0 ? P_NOTHING : P_UNKNOWN].v;
    if (rnd() % 8 != 0) {
        for (int tries = 0; tries < 64; tries++) {
            const PoolValue *pv = &pool[rnd() % (uint64_t)npool];
            if (ACCEPT[c->kind][pv->cls] == 'Y' && pv->cls != C_UNKNOWN && pv->cls != C_NOTHING &&
                !(c->currency[0] && pv->cls == C_EUR))
                return pv->v;
        }
    }
    return pool[rnd() % (uint64_t)npool].v;
}

static void normalize(Val *v) {
    if (v->n == 1 && v->b[0] == SER_NULL) v->b[0] = SER_UNKNOWN;
}

static void push_row(MTable *t, const MRow *r) {
    if (t->nrows == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 16;
        t->rows = realloc(t->rows, (size_t)t->cap * sizeof(MRow));
    }
    t->rows[t->nrows++] = *r;
}

static int find_row(const MTable *t, const unsigned char *key, size_t n) {
    for (int r = 0; r < t->nrows; r++)
        if (t->rows[r].key.n == n && memcmp(t->rows[r].key.b, key, n) == 0) return r;
    return -1;
}

/* ---- comparing the store with the model --------------------------------- */

static void compare(GsTxn *t, GsqlTable **tbs, const Model *m, const char *when) {
    for (int s = 0; s < NSPECS; s++) {
        const MTable *mt = &m->t[s];
        GsqlTable *tb = tbs[s];
        if (!mt->live) {
            CHECK(tb == NULL, "%s: %s is live in the store and not in the model", when, SPECS[s].name);
            continue;
        }
        CHECK(gsql_table_ncols(tb) == (size_t)mt->ncols, "%s: %s has %zu columns, model %d", when, SPECS[s].name,
              gsql_table_ncols(tb), mt->ncols);
        CHECK(gsql_table_nindexes(tb) == (size_t)mt->nidx, "%s: %s has %zu indexes, model %d", when, SPECS[s].name,
              gsql_table_nindexes(tb), mt->nidx);
        GsCursor *c;
        if (E->cursor_open(t, gsql_table_rows_tree(tb), &c) != GS_OK) {
            mismatch("%s: %s's rows tree will not open", when, SPECS[s].name);
            continue;
        }
        int pos = 0, seen = 0;
        E->first(c, &pos);
        while (pos) {
            GsBytes k, v;
            E->key(c, &k);
            E->value(c, &v);
            int r = find_row(mt, k.p, k.n);
            CHECK(r >= 0, "%s: %s holds a row the model does not", when, SPECS[s].name);
            if (r >= 0) {
                for (int col = 0; col < mt->ncols; col++) {
                    GsBytes got;
                    GsqlError er;
                    int rc = gsql_row_value(tb, v.p, v.n, (size_t)col, &got, &er);
                    const Val *want = &mt->rows[r].v[col];
                    CHECK(rc == GSQL_OK && got.n == want->n && memcmp(got.p, want->b, want->n) == 0,
                          "%s: %s row %d column %s differs from the model", when, SPECS[s].name, r, mt->cols[col].name);
                }
            }
            seen++;
            E->next(c, &pos);
        }
        E->cursor_close(c);
        CHECK(seen == mt->nrows, "%s: %s holds %d rows, model %d", when, SPECS[s].name, seen, mt->nrows);
        /* Each index, scanned in order, gives every row exactly once, in the
         * order of its entries. */
        for (int i = 0; i < mt->nidx; i++) {
            if (E->cursor_open(t, gsql_index_tree(tb, (size_t)i), &c) != GS_OK) {
                mismatch("%s: an index tree will not open", when);
                continue;
            }
            int n = 0;
            unsigned char *prev = NULL;
            size_t prevn = 0;
            E->first(c, &pos);
            int *hit = calloc((size_t)mt->nrows + 1, sizeof *hit);
            while (pos) {
                GsBytes ek, ev, rk;
                GsqlError er;
                E->key(c, &ek);
                E->value(c, &ev);
                int rc = gsql_index_rowkey(tb, (size_t)i, ek, ev, &rk, &er);
                int r = rc == GSQL_OK ? find_row(mt, rk.p, rk.n) : -1;
                CHECK(r >= 0, "%s: index %s gives a row key the model does not hold (%s)", when, mt->idx[i].name,
                      rc ? er.message : "unmatched");
                if (r >= 0) {
                    hit[r]++;
                    /* The entry's leading components are the row's own columns. */
                    GsqlKey want;
                    gsql_key_init(&want);
                    key_of(mt, mt->rows[r].v, mt->idx[i].ncols, mt->idx[i].cols, mt->idx[i].desc, &want);
                    CHECK(ek.n >= want.length && memcmp(ek.p, want.bytes, want.length) == 0,
                          "%s: index %s's entry for a row does not start with its columns", when, mt->idx[i].name);
                    gsql_key_free(&want);
                }
                if (prev) {
                    size_t mn = prevn < ek.n ? prevn : ek.n;
                    int cmp = memcmp(prev, ek.p, mn);
                    CHECK(cmp < 0 || (cmp == 0 && prevn < ek.n), "%s: index %s is out of order", when, mt->idx[i].name);
                }
                free(prev);
                prev = malloc(ek.n ? ek.n : 1);
                memcpy(prev, ek.p, ek.n);
                prevn = ek.n;
                n++;
                E->next(c, &pos);
            }
            free(prev);
            E->cursor_close(c);
            CHECK(n == mt->nrows, "%s: index %s has %d entries for %d rows", when, mt->idx[i].name, n, mt->nrows);
            for (int r = 0; r < mt->nrows; r++)
                CHECK(hit[r] == 1, "%s: index %s gives row %d %d times", when, mt->idx[i].name, r, hit[r]);
            free(hit);
        }
    }
}

static void report_line(void *ctx, const char *line) {
    (void)ctx;
    mismatch("check: %s", line);
}

static void decl_of(const MTable *mt, const char *name, GsqlTableDecl *d, GsqlColumn *cols) {
    for (int i = 0; i < mt->ncols; i++) {
        memset(&cols[i], 0, sizeof cols[i]);
        cols[i].name = mt->cols[i].name;
        cols[i].kind = (GsqlColKind)mt->cols[i].kind;
        strcpy(cols[i].currency, mt->cols[i].currency);
        cols[i].not_null = mt->cols[i].not_null;
        if (mt->cols[i].def >= 0) {
            cols[i].def = pool[mt->cols[i].def].v.b;
            cols[i].def_len = pool[mt->cols[i].def].v.n;
        }
    }
    d->name = name;
    d->ncols = (size_t)mt->ncols;
    d->cols = cols;
    d->nkey = (size_t)mt->nkey;
    d->key = mt->key;
}

static void open_all(GsTxn *t, GsqlTable **tbs, const Model *m) {
    for (int s = 0; s < NSPECS; s++) {
        gsql_table_close(tbs[s]);
        tbs[s] = NULL;
        if (m->t[s].live) {
            GsqlError er;
            int rc = gsql_table_open(E, t, SPECS[s].name, &tbs[s], &er);
            CHECK(rc == GSQL_OK, "opening %s answered %s: %s", SPECS[s].name, gsql_code_name(rc), er.message);
        }
    }
}

static int model_run(const char *dir) {
    pool_build();
    GsqlIo *io = dir ? gsql_io_real() : gsql_io_sim();
    char path[512];
    snprintf(path, sizeof path, "%s/db", dir ? dir : "d");
    GsOptions opt = {1, 1024, 256 * 1024, io, 2000, 3, 64, 0};
    GsStore *db;
    int rc = E->open(path, &opt, &db);
    if (rc) {
        printf("MISMATCH open answered %s\n", gs_status_name(rc));
        return 1;
    }
    Model m, saved;
    memset(&m, 0, sizeof m);
    memset(&saved, 0, sizeof saved);
    GsqlTable *tbs[NSPECS] = {0};
    long refused[GSQL_MISUSE + 1] = {0};
    long accepted = 0, reopens = 0, aborts = 0, moved = 0, nothings = 0;
    for (int round = 0; round < 400; round++) {
        GsTxn *t;
        if ((rc = E->begin_write(db, &t))) {
            mismatch("begin_write answered %s", gs_status_name(rc));
            break;
        }
        model_copy(&saved, &m);
        open_all(t, tbs, &m);
        int ops = 1 + (int)(rnd() % 12);
        for (int op = 0; op < ops; op++) {
            int s = (int)(rnd() % NSPECS);
            MTable *mt = &m.t[s];
            GsqlError er;
            unsigned what = (unsigned)(rnd() % 100);
            if (!mt->live) {
                /* create it */
                const TableSpec *sp = &SPECS[s];
                mt->live = 1;
                mt->ncols = sp->ncols;
                memcpy(mt->cols, sp->cols, sizeof sp->cols);
                mt->nkey = sp->nkey;
                memcpy(mt->key, sp->key, sizeof sp->key);
                for (int i = 0; i < mt->nkey; i++) mt->cols[mt->key[i]].not_null = 1;
                mt->nidx = 0;
                mt->nrows = 0;
                GsqlColumn cols[12];
                GsqlTableDecl d;
                decl_of(mt, sp->name, &d, cols);
                rc = gsql_table_create(E, t, &d, &er);
                CHECK(rc == GSQL_OK, "creating %s answered %s: %s", sp->name, gsql_code_name(rc), er.message);
                rc = gsql_table_open(E, t, sp->name, &tbs[s], &er);
                CHECK(rc == GSQL_OK, "opening the new %s answered %s", sp->name, gsql_code_name(rc));
                continue;
            }
            GsqlTable *tb = tbs[s];
            if (what < 45) {
                /* insert */
                GsqlIn in[12];
                Val sent[12], vals[12];   /* what the layer is handed; what the model expects stored */
                int want = GSQL_OK;
                for (int c = 0; c < mt->ncols; c++) {
                    int omit = rnd() % 6 == 0;
                    in[c].present = !omit;
                    if (omit) {
                        sent[c] = mt->cols[c].def >= 0 ? pool[mt->cols[c].def].v : v_tag(SER_UNKNOWN);
                    } else {
                        sent[c] = pick_for(&mt->cols[c]);
                    }
                    in[c].p = sent[c].b;
                    in[c].n = sent[c].n;
                    int f = model_fits(&mt->cols[c], &sent[c]);
                    if (want == GSQL_OK) want = f;
                    vals[c] = sent[c];
                    normalize(&vals[c]);
                }
                if (want == GSQL_OK && model_collides(mt, vals, -1)) want = GSQL_DUPLICATE;
                GsqlBuf key = {0};
                rc = gsql_row_insert(E, t, tb, in, &key, &er);
                /* A row refused for several reasons reports the first column's;
                 * the model agrees on which, since both check in column order. */
                CHECK(rc == want, "insert into %s answered %s, the model %s (%s)", SPECS[s].name, gsql_code_name(rc),
                      gsql_code_name(want), er.message);
                if (rc == GSQL_OK && want == GSQL_OK) {
                    for (int c = 0; c < mt->ncols; c++)
                        if (in[c].present && in[c].n == 1 && in[c].p[0] == SER_NULL) nothings++;
                    MRow r;
                    memset(&r, 0, sizeof r);
                    memcpy(r.key.b, key.bytes, key.length);
                    r.key.n = key.length;
                    for (int c = 0; c < mt->ncols; c++) r.v[c] = vals[c];
                    push_row(mt, &r);
                    accepted++;
                } else if (rc != GSQL_OK) {
                    refused[rc]++;
                }
                gsql_buf_free(&key);
            } else if (what < 65 && mt->nrows) {
                /* update a random subset of a row's columns */
                int r = (int)(rnd() % (uint64_t)mt->nrows);
                GsqlIn in[12];
                Val sent[12], vals[12];
                int want = GSQL_OK;
                for (int c = 0; c < mt->ncols; c++) {
                    int change = rnd() % 3 == 0;
                    in[c].present = change;
                    sent[c] = change ? pick_for(&mt->cols[c]) : mt->rows[r].v[c];
                    in[c].p = sent[c].b;
                    in[c].n = sent[c].n;
                    int f = model_fits(&mt->cols[c], &sent[c]);
                    if (want == GSQL_OK) want = f;
                    vals[c] = sent[c];
                    normalize(&vals[c]);
                }
                if (want == GSQL_OK && model_collides(mt, vals, r)) want = GSQL_DUPLICATE;
                GsqlBuf key = {0};
                Val oldkey = mt->rows[r].key;
                rc = gsql_row_update(E, t, tb, (GsBytes){oldkey.b, oldkey.n}, in, &key, &er);
                CHECK(rc == want, "update of %s answered %s, the model %s (%s)", SPECS[s].name, gsql_code_name(rc),
                      gsql_code_name(want), er.message);
                if (rc == GSQL_OK && want == GSQL_OK) {
                    if (key.length != oldkey.n || memcmp(key.bytes, oldkey.b, oldkey.n)) moved++;
                    memcpy(mt->rows[r].key.b, key.bytes, key.length);
                    mt->rows[r].key.n = key.length;
                    for (int c = 0; c < mt->ncols; c++) mt->rows[r].v[c] = vals[c];
                    accepted++;
                } else if (rc != GSQL_OK) {
                    refused[rc]++;
                }
                gsql_buf_free(&key);
            } else if (what < 78 && mt->nrows) {
                int r = (int)(rnd() % (uint64_t)mt->nrows);
                rc = gsql_row_delete(E, t, tb, (GsBytes){mt->rows[r].key.b, mt->rows[r].key.n}, &er);
                CHECK(rc == GSQL_OK, "delete from %s answered %s: %s", SPECS[s].name, gsql_code_name(rc), er.message);
                Val gone = mt->rows[r].key;
                mt->rows[r] = mt->rows[--mt->nrows];
                /* ...and deleting it again answers NOT_FOUND */
                rc = gsql_row_delete(E, t, tb, (GsBytes){gone.b, gone.n}, &er);
                CHECK(rc == GSQL_NOT_FOUND, "deleting a deleted row answered %s", gsql_code_name(rc));
            } else if (what < 85 && mt->nidx < 4) {
                /* an index on one to three non-record columns */
                int n = 1 + (int)(rnd() % 3);
                unsigned cols[3];
                int desc[3], got = 0;
                for (int tries = 0; tries < 20 && got < n; tries++) {
                    unsigned c = (unsigned)(rnd() % (uint64_t)mt->ncols);
                    int dup = 0;
                    for (int j = 0; j < got; j++) dup |= cols[j] == c;
                    if (dup || mt->cols[c].kind == GSQL_COL_RECORD) continue;
                    desc[got] = (int)(rnd() % 2);
                    cols[got++] = c;
                }
                if (!got) continue;
                int unique = rnd() % 2 == 0;
                char name[16];
                snprintf(name, sizeof name, "ix%d", (int)(rnd() % 1000));
                int want = GSQL_OK;
                for (int i = 0; i < mt->nidx; i++)
                    if (!strcmp(mt->idx[i].name, name)) want = GSQL_EXISTS;
                if (want == GSQL_OK && unique) {
                    for (int a = 0; a < mt->nrows && want == GSQL_OK; a++)
                        for (int b = a + 1; b < mt->nrows && want == GSQL_OK; b++)
                            if (!any_absent(mt, mt->rows[a].v, got, cols) &&
                                same_key(mt, mt->rows[a].v, mt->rows[b].v, got, cols))
                                want = GSQL_DUPLICATE;
                }
                GsqlIndexDecl d = {name, (size_t)got, cols, desc, unique};
                rc = gsql_index_create(E, t, tb, &d, &er);
                CHECK(rc == want, "index %s on %s answered %s, the model %s (%s)", name, SPECS[s].name,
                      gsql_code_name(rc), gsql_code_name(want), er.message);
                if (rc == GSQL_OK && want == GSQL_OK) {
                    int i = mt->nidx++;
                    strcpy(mt->idx[i].name, name);
                    mt->idx[i].ncols = got;
                    memcpy(mt->idx[i].cols, cols, sizeof cols);
                    memcpy(mt->idx[i].desc, desc, sizeof desc);
                    mt->idx[i].unique = unique;
                } else if (rc != GSQL_OK) {
                    refused[rc]++;
                }
            } else if (what < 88 && mt->nidx) {
                int i = (int)(rnd() % (uint64_t)mt->nidx);
                rc = gsql_index_drop(E, t, tb, mt->idx[i].name, &er);
                CHECK(rc == GSQL_OK, "dropping index %s answered %s", mt->idx[i].name, gsql_code_name(rc));
                /* the layer keeps the others in order */
                memmove(&mt->idx[i], &mt->idx[i + 1], (size_t)(mt->nidx - i - 1) * sizeof mt->idx[0]);
                mt->nidx--;
            } else if (what < 91 && mt->ncols < 12) {
                /* ADD COLUMN: nullable, with a default about half the time */
                static const char *const ADDED[] = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
                                                    "x8", "x9", "x10", "x11", "x12"};
                const char *nm = ADDED[mt->ncols];
                ColSpec cs = {nm, GSQL_COL_NUMBER, "", 0, rnd() % 2 ? P_INT3 : -1};
                if (rnd() % 3 == 0) cs.kind = GSQL_COL_STRING, cs.def = cs.def >= 0 ? P_STR_A : -1;
                GsqlColumn c;
                memset(&c, 0, sizeof c);
                c.name = cs.name;
                c.kind = (GsqlColKind)cs.kind;
                if (cs.def >= 0) c.def = pool[cs.def].v.b, c.def_len = pool[cs.def].v.n;
                rc = gsql_table_add_column(E, t, tb, &c, &er);
                CHECK(rc == GSQL_OK, "adding column %s answered %s: %s", nm, gsql_code_name(rc), er.message);
                if (rc == GSQL_OK) {
                    int at = mt->ncols++;
                    mt->cols[at] = cs;
                    for (int r = 0; r < mt->nrows; r++)
                        mt->rows[r].v[at] = cs.def >= 0 ? pool[cs.def].v : v_tag(SER_UNKNOWN);
                }
            } else if (what < 92) {
                rc = gsql_table_drop(E, t, SPECS[s].name, &er);
                CHECK(rc == GSQL_OK, "dropping %s answered %s", SPECS[s].name, gsql_code_name(rc));
                gsql_table_close(tbs[s]);
                tbs[s] = NULL;
                mt->live = 0;
                mt->nrows = 0;
                mt->nidx = 0;
            } else if (mt->nrows) {
                /* a point read by the key the layer answered */
                int r = (int)(rnd() % (uint64_t)mt->nrows);
                GsqlBuf row = {0};
                rc = gsql_row_get(E, t, tb, (GsBytes){mt->rows[r].key.b, mt->rows[r].key.n}, &row, &er);
                CHECK(rc == GSQL_OK, "reading a row of %s answered %s", SPECS[s].name, gsql_code_name(rc));
                if (mt->nkey && rc == GSQL_OK) {
                    /* ...and by its key VALUES, which must make the same key */
                    GsqlIn kin[3];
                    for (int i = 0; i < mt->nkey; i++) {
                        kin[i].present = 1;
                        kin[i].p = mt->rows[r].v[mt->key[i]].b;
                        kin[i].n = mt->rows[r].v[mt->key[i]].n;
                    }
                    GsqlBuf k = {0};
                    rc = gsql_row_key(tb, kin, &k, &er);
                    CHECK(rc == GSQL_OK && k.length == mt->rows[r].key.n &&
                              memcmp(k.bytes, mt->rows[r].key.b, k.length) == 0,
                          "a row's key values do not make its key");
                    gsql_buf_free(&k);
                }
                gsql_buf_free(&row);
            }
        }
        compare(t, tbs, &m, "inside the transaction");
        if (rnd() % 7 == 0) {
            E->abort(t);
            model_copy(&m, &saved);
            aborts++;
        } else {
            uint64_t id;
            rc = E->commit(t, rnd() % 4 == 0, &id);
            CHECK(rc == GS_OK, "commit answered %s", gs_status_name(rc));
        }
        for (int s = 0; s < NSPECS; s++) gsql_table_close(tbs[s]), tbs[s] = NULL;
        if (round % 25 == 24) {
            E->close(db);
            rc = E->open(path, &opt, &db);
            if (rc) {
                mismatch("reopen answered %s", gs_status_name(rc));
                break;
            }
            reopens++;
        }
        if (round % 25 == 24 || round == 399) {
            GsTxn *r;
            E->begin_read(db, &r);
            open_all(r, tbs, &m);
            compare(r, tbs, &m, "after a reopen");
            GsqlError er;
            rc = gsql_table_check(E, r, report_line, NULL, &er);
            CHECK(rc == GSQL_OK, "the invariant check answered %s: %s", gsql_code_name(rc), er.message);
            for (int s = 0; s < NSPECS; s++) gsql_table_close(tbs[s]), tbs[s] = NULL;
            E->abort(r);
        }
    }
    E->close(db);
    gsql_io_free(io);
    model_free(&m);
    model_free(&saved);
    printf("accepted %ld (%ld bound nothings), refused TYPE %ld NOT_NULL %ld DUPLICATE %ld, moved %ld, aborts %ld, "
           "reopens %ld\n", accepted, nothings, refused[GSQL_TYPE], refused[GSQL_NOT_NULL], refused[GSQL_DUPLICATE],
           moved, aborts, reopens);
    /* The workload must have reached every outcome, or agreement says little. */
    CHECK(accepted > 300 && refused[GSQL_TYPE] > 50 && refused[GSQL_NOT_NULL] > 5 && refused[GSQL_DUPLICATE] > 10 &&
              moved > 3 && aborts > 10 && reopens >= 10 && nothings > 10,
          "the workload did not reach every outcome");
    printf("ok %ld checks, %ld mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}

/* ---- kinds: the battery gBASIC wrote ------------------------------------ */

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int kinds_run(const char *file) {
    static const int ORDER[10] = {GSQL_COL_NUMBER, GSQL_COL_INTEGER, GSQL_COL_STRING, GSQL_COL_BOOLEAN,
                                  GSQL_COL_MONEY, GSQL_COL_MONEY, GSQL_COL_DATE, GSQL_COL_TIME,
                                  GSQL_COL_DATETIME, GSQL_COL_RECORD};
    FILE *f = fopen(file, "r");
    if (!f) {
        printf("MISMATCH cannot read %s\n", file);
        return 1;
    }
    char line[8192];
    int values = 0, absent = 0;
    while (fgets(line, sizeof line, f)) {
        char verdicts[32], label[128], hex[8000];
        if (sscanf(line, "V %31s %127s %7999s", verdicts, label, hex) != 3) continue;
        size_t hn = strlen(hex);
        if (strlen(verdicts) != 10 || hn < 10 || hn % 2) {
            mismatch("a malformed line: %s", line);
            continue;
        }
        unsigned char *b = malloc(hn / 2);
        for (size_t i = 0; i < hn / 2; i++) b[i] = (unsigned char)(hexval(hex[2 * i]) * 16 + hexval(hex[2 * i + 1]));
        const unsigned char *v = b + 4;   /* serialize's header */
        size_t n = hn / 2 - 4;
        values++;
        int is_absent = n == 1 && (v[0] == SER_UNKNOWN || v[0] == SER_NULL);
        absent += is_absent;
        for (int k = 0; k < 10; k++) {
            GsqlColumn c;
            memset(&c, 0, sizeof c);
            c.name = "c";
            c.kind = (GsqlColKind)ORDER[k];
            if (k == 5) strcpy(c.currency, "USD");
            GsqlError er;
            int rc = gsql_value_fits(&c, v, n, &er);
            int want = verdicts[k] == 'y' ? GSQL_OK : GSQL_TYPE;
            CHECK(rc == want, "%s in a %s%s column answered %s, want %s (%s)", label, gsql_kind_name(c.kind),
                  k == 5 ? "(USD)" : "", gsql_code_name(rc), gsql_code_name(want), er.message);
            /* NOT NULL changes the answer for an absence and nothing else. */
            c.not_null = 1;
            rc = gsql_value_fits(&c, v, n, &er);
            int want_nn = is_absent ? GSQL_NOT_NULL : want;
            CHECK(rc == want_nn, "%s in a NOT NULL %s column answered %s, want %s", label, gsql_kind_name(c.kind),
                  gsql_code_name(rc), gsql_code_name(want_nn));
            /* Every truncation of the value is refused, never accepted as a
             * shorter one. */
            if (k == 0) {
                for (size_t cut = 0; cut < n; cut++) {
                    c.not_null = 0;
                    c.kind = GSQL_COL_RECORD;
                    int tr = gsql_value_fits(&c, v, cut, &er);
                    CHECK(tr == GSQL_TYPE && strstr(er.message, "well-formed"),
                          "%s cut to %zu of %zu bytes answered %s", label, cut, n, gsql_code_name(tr));
                }
            }
        }
        free(b);
    }
    fclose(f);
    CHECK(values >= 30 && absent == 2, "the battery held %d values and %d absences", values, absent);
    printf("values %d\n", values);
    printf("ok %ld checks, %ld mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}

/* ---- refusals, each beside the case that passes ------------------------- */

static GsStore *fresh(GsqlIo *io, const char *path) {
    GsOptions opt = {1, 1024, 256 * 1024, io, 0, 0, 0, 0};
    GsStore *db = NULL;
    E->open(path, &opt, &db);
    return db;
}

static int expect(int rc, int want, const GsqlError *er, const char *needle, const char *what) {
    checks++;
    if (rc != want || (needle && !strstr(er->message, needle))) {
        mismatch("%s answered %s (%s), want %s%s%s", what, gsql_code_name(rc), er->message, gsql_code_name(want),
                 needle ? " naming " : "", needle ? needle : "");
        return 0;
    }
    return 1;
}

static int refuse_run(void) {
    pool_build();
    GsqlIo *io = gsql_io_sim();
    GsStore *db = fresh(io, "d/db");
    GsTxn *t;
    E->begin_write(db, &t);
    GsqlError er;
    GsqlColumn cols[3];
    memset(cols, 0, sizeof cols);
    cols[0].name = "id";
    cols[0].kind = GSQL_COL_INTEGER;
    cols[1].name = "name";
    cols[1].kind = GSQL_COL_STRING;
    cols[2].name = "info";
    cols[2].kind = GSQL_COL_RECORD;
    unsigned key0[1] = {0}, key2[1] = {2}, key_bad[1] = {3}, key_twice[2] = {0, 0};
    GsqlTableDecl d = {"t", 3, cols, 1, key0};

    /* declarations */
    d.name = "";
    expect(gsql_table_create(E, t, &d, &er), GSQL_SCHEMA, &er, "needs a name", "a table with no name");
    char longname[300];
    memset(longname, 'n', 299);
    longname[299] = 0;
    d.name = longname;
    expect(gsql_table_create(E, t, &d, &er), GSQL_SCHEMA, &er, "past the limit", "a 299-byte table name");
    d.name = "t";
    d.ncols = 0;
    expect(gsql_table_create(E, t, &d, &er), GSQL_SCHEMA, &er, "no columns", "a table with no columns");
    d.ncols = 3;
    cols[1].name = "id";
    expect(gsql_table_create(E, t, &d, &er), GSQL_SCHEMA, &er, "two columns named id", "two columns of one name");
    cols[1].name = "name";
    d.key = key_bad;
    expect(gsql_table_create(E, t, &d, &er), GSQL_SCHEMA, &er, "names column 3", "a key on a column not there");
    d.key = key_twice;
    d.nkey = 2;
    expect(gsql_table_create(E, t, &d, &er), GSQL_SCHEMA, &er, "twice", "a key naming a column twice");
    d.nkey = 1;
    d.key = key2;
    expect(gsql_table_create(E, t, &d, &er), GSQL_SCHEMA, &er, "is a record", "a key on a record column");
    d.key = key0;
    Val three = v_int(3), inexact = v_dbl(3.0), text = v_str("x", 1), unk = v_tag(SER_UNKNOWN);
    cols[0].def = inexact.b;
    cols[0].def_len = inexact.n;
    expect(gsql_table_create(E, t, &d, &er), GSQL_SCHEMA, &er, "default refused", "an integer column defaulting to 3.0");
    cols[0].def = unk.b;
    cols[0].def_len = unk.n;
    expect(gsql_table_create(E, t, &d, &er), GSQL_SCHEMA, &er, "no default", "a default of unknown");
    cols[0].def = three.b;
    cols[0].def_len = three.n;
    strcpy(cols[1].currency, "USD");
    expect(gsql_table_create(E, t, &d, &er), GSQL_SCHEMA, &er, "only a money column", "a string naming a currency");
    cols[1].currency[0] = 0;
    /* the control: the same declaration, corrected, is accepted, and twice is EXISTS */
    expect(gsql_table_create(E, t, &d, &er), GSQL_OK, &er, NULL, "the corrected declaration");
    expect(gsql_table_create(E, t, &d, &er), GSQL_EXISTS, &er, "already a table", "creating it twice");

    GsqlTable *tb;
    expect(gsql_table_open(E, t, "nope", &tb, &er), GSQL_NOT_FOUND, &er, "no table nope", "opening a table not there");
    expect(gsql_table_open(E, t, "t", &tb, &er), GSQL_OK, &er, NULL, "opening it");
    checks++;
    if (!gsql_table_col(tb, 0)->not_null) mismatch("a key column was not made NOT NULL");

    /* writes */
    GsqlIn in[3] = {{1, three.b, three.n}, {1, text.b, text.n}, {1, unk.b, unk.n}};
    in[0].p = inexact.b;
    in[0].n = inexact.n;
    expect(gsql_row_insert(E, t, tb, in, NULL, &er), GSQL_TYPE, &er, "not an exact integer", "3.0 into an integer key");
    in[0].p = three.b;
    in[0].n = three.n;
    in[1].p = three.b;
    in[1].n = three.n;
    expect(gsql_row_insert(E, t, tb, in, NULL, &er), GSQL_TYPE, &er, "t.name is string", "a number into a string column");
    in[1].p = text.b;
    in[1].n = text.n;
    in[0].present = 0;   /* omitted: the default, 3 */
    expect(gsql_row_insert(E, t, tb, in, NULL, &er), GSQL_OK, &er, NULL, "a row taking the default key");
    in[0].present = 1;
    expect(gsql_row_insert(E, t, tb, in, NULL, &er), GSQL_DUPLICATE, &er, "primary key", "the same key again");
    in[0].p = unk.b;
    in[0].n = unk.n;
    expect(gsql_row_insert(E, t, tb, in, NULL, &er), GSQL_NOT_NULL, &er, "t.id is NOT NULL", "an unknown key");
    Val four = v_int(4);
    in[0].p = four.b;
    in[0].n = four.n;
    expect(gsql_row_insert(E, t, tb, in, NULL, &er), GSQL_OK, &er, NULL, "the next key");

    /* a unique index the rows break, beside one they do not */
    unsigned on_name[1] = {1}, on_id[1] = {0}, on_info[1] = {2};
    GsqlIndexDecl ix = {"by_name", 1, on_name, NULL, 1};
    expect(gsql_index_create(E, t, tb, &ix, &er), GSQL_DUPLICATE, &er, "cannot be unique", "a unique index on two equal names");
    checks++;
    if (gsql_table_nindexes(tb) != 0) mismatch("a refused index stayed on the handle");
    ix.unique = 0;
    expect(gsql_index_create(E, t, tb, &ix, &er), GSQL_OK, &er, NULL, "the same index, not unique");
    expect(gsql_index_create(E, t, tb, &ix, &er), GSQL_EXISTS, &er, "already has an index", "the same index twice");
    GsqlIndexDecl ix2 = {"by_id", 1, on_id, NULL, 1};
    expect(gsql_index_create(E, t, tb, &ix2, &er), GSQL_OK, &er, NULL, "a unique index the rows keep");
    GsqlIndexDecl ix3 = {"by_info", 1, on_info, NULL, 0};
    expect(gsql_index_create(E, t, tb, &ix3, &er), GSQL_SCHEMA, &er, "is a record", "an index on a record");

    /* UNIQUE lets several rows hold an absence (NULLS DISTINCT) */
    GsqlColumn ucol[2];
    memset(ucol, 0, sizeof ucol);
    ucol[0].name = "k";
    ucol[0].kind = GSQL_COL_INTEGER;
    ucol[1].name = "u";
    ucol[1].kind = GSQL_COL_STRING;
    GsqlTableDecl ud = {"u", 2, ucol, 1, key0};
    expect(gsql_table_create(E, t, &ud, &er), GSQL_OK, &er, NULL, "a second table");
    GsqlTable *ut;
    gsql_table_open(E, t, "u", &ut, &er);
    GsqlIndexDecl uix = {"uu", 1, on_name, NULL, 1};
    expect(gsql_index_create(E, t, ut, &uix, &er), GSQL_OK, &er, NULL, "a unique index on an empty table");
    Val one = v_int(1), two = v_int(2), five = v_int(5);
    GsqlIn r1[2] = {{1, one.b, one.n}, {1, unk.b, unk.n}};
    GsqlIn r2[2] = {{1, two.b, two.n}, {1, unk.b, unk.n}};
    GsqlIn r3[2] = {{1, five.b, five.n}, {1, text.b, text.n}};
    GsqlIn r4[2] = {{1, three.b, three.n}, {1, text.b, text.n}};
    expect(gsql_row_insert(E, t, ut, r1, NULL, &er), GSQL_OK, &er, NULL, "a first unknown under UNIQUE");
    expect(gsql_row_insert(E, t, ut, r2, NULL, &er), GSQL_OK, &er, NULL, "a second unknown under UNIQUE");
    expect(gsql_row_insert(E, t, ut, r3, NULL, &er), GSQL_OK, &er, NULL, "a first x under UNIQUE");
    expect(gsql_row_insert(E, t, ut, r4, NULL, &er), GSQL_DUPLICATE, &er, "unique index uu", "a second x under UNIQUE");

    /* a refused update changes nothing */
    GsqlBuf k5 = {0}, row = {0};
    GsqlIn kin[1] = {{1, five.b, five.n}};
    gsql_row_key(ut, kin, &k5, &er);
    GsqlIn upd[2] = {{1, two.b, two.n}, {0, NULL, 0}};
    expect(gsql_row_update(E, t, ut, (GsBytes){k5.bytes, k5.length}, upd, NULL, &er), GSQL_DUPLICATE, &er,
           "primary key", "moving a row onto another's key");
    expect(gsql_row_get(E, t, ut, (GsBytes){k5.bytes, k5.length}, &row, &er), GSQL_OK, &er, NULL,
           "the refused update's row is still there");
    GsqlError ce;
    expect(gsql_table_check(E, t, report_line, NULL, &ce), GSQL_OK, &ce, NULL, "the check after the refusals");

    /* ADD COLUMN: NOT NULL needs a default */
    GsqlColumn add;
    memset(&add, 0, sizeof add);
    add.name = "extra";
    add.kind = GSQL_COL_NUMBER;
    add.not_null = 1;
    expect(gsql_table_add_column(E, t, ut, &add, &er), GSQL_SCHEMA, &er, "no default", "a NOT NULL column with no default");
    add.def = three.b;
    add.def_len = three.n;
    expect(gsql_table_add_column(E, t, ut, &add, &er), GSQL_OK, &er, NULL, "the same with a default");
    GsBytes got;
    gsql_buf_free(&row);
    gsql_row_get(E, t, ut, (GsBytes){k5.bytes, k5.length}, &row, &er);
    int rc = gsql_row_value(ut, row.bytes, row.length, 2, &got, &er);
    checks++;
    if (rc != GSQL_OK || got.n != three.n || memcmp(got.p, three.b, three.n))
        mismatch("a row written before ADD COLUMN does not read the column as its default");
    expect(gsql_table_check(E, t, report_line, NULL, &ce), GSQL_OK, &ce, NULL, "the check after ADD COLUMN");

    /* DROP leaves no tree behind, and the check would say so */
    expect(gsql_table_drop(E, t, "t", &er), GSQL_OK, &er, NULL, "dropping a table");
    expect(gsql_table_drop(E, t, "t", &er), GSQL_NOT_FOUND, &er, NULL, "dropping it twice");
    expect(gsql_table_check(E, t, report_line, NULL, &ce), GSQL_OK, &ce, NULL, "the check after DROP");
    /* the check's own control: a stray rows tree is reported */
    unsigned char stray[10] = {0, 'r', 0, 0, 0, 0, 0, 0, 0, 99};
    E->tree_create(t, (GsBytes){stray, 10});
    long before = mismatches;
    rc = gsql_table_check(E, t, NULL, NULL, &ce);
    checks++;
    if (rc != GSQL_CORRUPT) mismatch("a rows tree no table names was not reported");
    mismatches = before + (rc != GSQL_CORRUPT);
    E->tree_drop(t, (GsBytes){stray, 10});

    gsql_table_close(tb);
    gsql_table_close(ut);
    gsql_buf_free(&k5);
    gsql_buf_free(&row);
    E->abort(t);
    E->close(db);
    gsql_io_free(io);
    printf("ok %ld checks, %ld mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}

/* ---- layout: a fixed database, dumped ----------------------------------- */

static void hexout(const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
}

static int dump_tree(void *ctx, GsBytes name) {
    GsTxn *t = ctx;
    GsCursor *c;
    if (E->cursor_open(t, name, &c) != GS_OK) return 0;
    int pos = 0;
    E->first(c, &pos);
    while (pos) {
        GsBytes k, v;
        E->key(c, &k);
        E->value(c, &v);
        printf("E ");
        hexout(name.p, name.n);
        printf(" ");
        hexout(k.p, k.n);
        printf(" ");
        hexout(v.p, v.n);
        printf("\n");
        E->next(c, &pos);
    }
    E->cursor_close(c);
    return 0;
}

static int layout_run(const char *dir) {
    char path[512];
    snprintf(path, sizeof path, "%s/db", dir);
    GsqlIo *io = gsql_io_real();
    GsStore *db = fresh(io, path);
    if (!db) {
        printf("MISMATCH cannot open %s\n", path);
        return 1;
    }
    GsTxn *t;
    E->begin_write(db, &t);
    GsqlError er;
    GsqlColumn cols[3];
    memset(cols, 0, sizeof cols);
    Val zero_usd = v_money(0, "USD");
    cols[0].name = "id";
    cols[0].kind = GSQL_COL_INTEGER;
    cols[1].name = "owner";
    cols[1].kind = GSQL_COL_STRING;
    cols[1].not_null = 1;
    cols[2].name = "balance";
    cols[2].kind = GSQL_COL_MONEY;
    strcpy(cols[2].currency, "USD");
    cols[2].def = zero_usd.b;
    cols[2].def_len = zero_usd.n;
    unsigned key0[1] = {0};
    GsqlTableDecl d = {"accounts", 3, cols, 1, key0};
    int rc = gsql_table_create(E, t, &d, &er);
    GsqlTable *tb;
    rc |= gsql_table_open(E, t, "accounts", &tb, &er);
    unsigned on_owner[1] = {1};
    int desc[1] = {1};
    GsqlIndexDecl ix = {"by_owner", 1, on_owner, desc, 0};
    rc |= gsql_index_create(E, t, tb, &ix, &er);
    Val id7 = v_int(7), ada = v_str("Ada", 3), b = v_money(1250000, "USD"), nothing = v_tag(SER_NULL);
    GsqlIn r1[3] = {{1, id7.b, id7.n}, {1, ada.b, ada.n}, {1, b.b, b.n}};
    rc |= gsql_row_insert(E, t, tb, r1, NULL, &er);
    Val id9 = v_int(9), bob = v_str("Bob", 3);
    GsqlIn r2[3] = {{1, id9.b, id9.n}, {1, bob.b, bob.n}, {0, NULL, 0}};
    rc |= gsql_row_insert(E, t, tb, r2, NULL, &er);
    GsqlColumn add;
    memset(&add, 0, sizeof add);
    add.name = "closed";
    add.kind = GSQL_COL_DATE;
    rc |= gsql_table_add_column(E, t, tb, &add, &er);
    Val id11 = v_int(11);
    GsqlIn r3[4] = {{1, id11.b, id11.n}, {1, ada.b, ada.n}, {1, nothing.b, nothing.n}, {0, NULL, 0}};
    rc |= gsql_row_insert(E, t, tb, r3, NULL, &er);
    if (rc) {
        printf("MISMATCH building the layout database answered %s: %s\n", gsql_code_name(er.code), er.message);
        return 1;
    }
    E->tree_list(t, dump_tree, t);
    gsql_table_close(tb);
    uint64_t id;
    E->commit(t, 1, &id);
    E->close(db);
    gsql_io_free(io);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "kinds")) return kinds_run(argv[2]);
    if (argc >= 2 && !strcmp(argv[1], "refuse")) return refuse_run();
    if (argc >= 3 && !strcmp(argv[1], "model")) {
        rng = strtoull(argv[2], NULL, 10) * 2654435761u + 1;
        return model_run(argc >= 4 ? argv[3] : NULL);
    }
    if (argc >= 3 && !strcmp(argv[1], "layout")) return layout_run(argv[2]);
    fprintf(stderr, "usage: table_test kinds FILE | refuse | model SEED [DIR] | layout DIR\n");
    return 2;
}

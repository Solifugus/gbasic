#ifndef GBASIC_GB_VECTOR_H
#define GBASIC_GB_VECTOR_H

/* A VECTOR'S ARITHMETIC (docs/vectors_design.md §4), shared by the value kind
 * in src/eval.c and, from increment 2, G-SQL's distance operator, so a
 * distance computed in a query and one computed in a program are the same
 * number.
 *
 * THE SUMS ARE IN DOUBLE over the float32 elements, in four running sums
 * taken in turn (element i to sum i mod 4) and added (s0 + s1) + (s2 + s3).
 * The order is part of the contract: it is fixed so an answer is the same on
 * every machine, and the test's oracle sums in this order. No compiler
 * reorders it, since nothing is built with -ffast-math. */

#include <math.h>
#include <stddef.h>

typedef struct GbVector {
    size_t ref_count;
    size_t dim;
    float data[];
} GbVector;

/* sum term(i) over i < n into four running sums, element i to sum i & 3 */
#define GB_VEC_SUM4(n, term, out)                                    \
    do {                                                             \
        double s_[4] = { 0, 0, 0, 0 };                               \
        for (size_t i = 0; i < (n); i++) {                           \
            s_[i & 3] += (term);                                     \
        }                                                            \
        (out) = (s_[0] + s_[1]) + (s_[2] + s_[3]);                   \
    } while (0)

static inline double gb_vec_dot(const float *a, const float *b, size_t n) {
    double sum;
    GB_VEC_SUM4(n, (double)a[i] * (double)b[i], sum);
    return sum;
}

static inline double gb_vec_norm(const float *a, size_t n) {
    double sum;
    GB_VEC_SUM4(n, (double)a[i] * (double)a[i], sum);
    return sqrt(sum);
}

static inline double gb_vec_l2(const float *a, const float *b, size_t n) {
    double sum;
    GB_VEC_SUM4(n, ((double)a[i] - (double)b[i]) * ((double)a[i] - (double)b[i]), sum);
    return sqrt(sum);
}

/* a.b, a.a and b.b in one pass, each summed in the order above, so each is
   the same double gb_vec_dot and gb_vec_norm give */
static inline void gb_vec_dot3(const float *a, const float *b, size_t n, double *ab,
                               double *aa, double *bb) {
    double d[4] = { 0, 0, 0, 0 }, x[4] = { 0, 0, 0, 0 }, y[4] = { 0, 0, 0, 0 };
    for (size_t i = 0; i < n; i++) {
        double p = a[i], q = b[i];
        d[i & 3] += p * q;
        x[i & 3] += p * p;
        y[i & 3] += q * q;
    }
    *ab = (d[0] + d[1]) + (d[2] + d[3]);
    *aa = (x[0] + x[1]) + (x[2] + x[3]);
    *bb = (y[0] + y[1]) + (y[2] + y[3]);
}

/* The cosine similarity, held to [-1, 1] as pgvector holds it: rounding can
 * put two equal directions at 1.0000000000000002. NaN when either norm is
 * zero; the caller refuses that before asking. */
/* the cosine from gb_vec_dot3's three sums, clamped to [-1, 1] */
static inline double gb_vec_cosine_of(double ab, double aa, double bb) {
    double c = ab / (sqrt(aa) * sqrt(bb));
    return c > 1 ? 1 : c < -1 ? -1 : c;
}

static inline double gb_vec_cosine(const float *a, const float *b, size_t n) {
    double ab, aa, bb;
    gb_vec_dot3(a, b, n, &ab, &aa, &bb);
    return gb_vec_cosine_of(ab, aa, bb);
}

/* The same cosine, for ranking many b against one a whose a.a is known:
 * one pass over b for a.b and b.b. *bb_out is b.b, so a caller can refuse a
 * zero b; the answer is then NaN. */
static inline double gb_vec_cosine_given(const float *a, double aa, const float *b, size_t n,
                                         double *bb_out) {
    double d[4] = { 0, 0, 0, 0 }, y[4] = { 0, 0, 0, 0 };
    for (size_t i = 0; i < n; i++) {
        double p = a[i], q = b[i];
        d[i & 3] += p * q;
        y[i & 3] += q * q;
    }
    double ab = (d[0] + d[1]) + (d[2] + d[3]);
    double bb = (y[0] + y[1]) + (y[2] + y[3]);
    *bb_out = bb;
    double c = ab / (sqrt(aa) * sqrt(bb));
    return c > 1 ? 1 : c < -1 ? -1 : c;
}

typedef enum { GB_VEC_L2, GB_VEC_COSINE, GB_VEC_IP } GbVecMetric;

/* smaller is nearer in all three: l2, 1 - cosine, and the negated inner
   product (pgvector's <->, <=> and <#>) */
static inline double gb_vec_distance(GbVecMetric m, const float *a, const float *b, size_t n) {
    switch (m) {
    case GB_VEC_COSINE: return 1 - gb_vec_cosine(a, b, n);
    case GB_VEC_IP: return -gb_vec_dot(a, b, n);
    default: return gb_vec_l2(a, b, n);
    }
}

#endif

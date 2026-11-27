/* --- Vectors: making one, its arithmetic (docs/vectors_design.md) -----------
 *
 * #included by src/eval.c, as the other modules are, for its static helpers.
 * The sums themselves are include/gb_vector.h's, shared with G-SQL. Every
 * refusal is code 1003 from source "vector" and names what it got. */

/* Build a vector from what vector() accepts: a vector (shared), an array of
 * numbers, or the text of one (`[1, 2, 3]`, pgvector's text form and JSON's).
 * Takes ownership of `value`. NULL, raised, when it is not one; `verb` begins
 * the message. */
static GbVector *vector_from_value(Value value, const char *verb) {
    char phrase[48];
    if (value.kind == VALUE_VECTOR) {
        GbVector *shared = value.as.vector;   /* the reference moves to the caller */
        return shared;
    }
    if (value.kind == VALUE_STRING) {
        Value decoded = builtin_decode_text(value);
        if (error_action_pending()) {
            return NULL;
        }
        if (decoded.kind != VALUE_ARRAY) {
            raise_fmt(1003, "vector", "%s: this text is %s, not a list of numbers such as "
                      "[0.1, 0.2]", verb, kind_phrase(decoded.kind, phrase, sizeof phrase));
            value_free(decoded);
            return NULL;
        }
        value = decoded;
    }
    if (value.kind != VALUE_ARRAY) {
        raise_fmt(1003, "vector", "%s expects an array of numbers, the text of one, or a "
                  "vector, and this is %s", verb, kind_phrase(value.kind, phrase, sizeof phrase));
        value_free(value);
        return NULL;
    }
    const ArrayStorage *st = value.as.array.store;
    if (st->count == 0) {
        value_free(value);
        raise_fmt(1003, "vector", "%s: an empty array makes no vector; a vector has at least "
                  "one number", verb);
        return NULL;
    }
    GbVector *v = vector_new(st->count);
    for (size_t i = 0; i < st->count; i++) {
        const Value *e = &st->items[i];
        if (e->kind != VALUE_NUMBER) {
            raise_fmt(1003, "vector", "%s: the element at index %zu is %s, not a number", verb, i,
                      kind_phrase(e->kind, phrase, sizeof phrase));
            vector_release(v);
            value_free(value);
            return NULL;
        }
        double x = e->as.num.value;
        if (!isfinite(x) || fabs(x) > FLT_MAX) {
            char shown[64];
            format_number(shown, sizeof shown, x);
            raise_fmt(1003, "vector", "%s: the element at index %zu is %s, and a vector holds "
                      "finite numbers a float32 can (up to about 3.4e38)", verb, i, shown);
            vector_release(v);
            value_free(value);
            return NULL;
        }
        v->data[i] = (float)x;
    }
    value_free(value);
    return v;
}

static Value vector_to_array(const GbVector *v) {
    Value *items = malloc(v->dim * sizeof *items);
    if (!items) {
        abort();
    }
    for (size_t i = 0; i < v->dim; i++) {
        items[i] = value_number((double)v->data[i]);
    }
    return value_array(items, v->dim);
}

/* Read argument `at` as a vector; NULL, raised, when it is not one. */
static GbVector *vector_arg(AstExpr *expr, size_t at) {
    Value v = eval_expr(expr->as.call.args.items[at]);
    if (error_action_pending()) {
        value_free(v);
        return NULL;
    }
    if (v.kind != VALUE_VECTOR) {
        char phrase[48];
        raise_fmt(1003, "vector", "%s expects a vector as its %s argument, and this is %s%s",
                  expr->as.call.name, at == 0 ? "first" : at == 1 ? "second" : "next",
                  kind_phrase(v.kind, phrase, sizeof phrase),
                  v.kind == VALUE_ARRAY ? "; make one with vector(...)" : "");
        value_free(v);
        return NULL;
    }
    return v.as.vector;
}

static int vector_same_dim(const char *verb, const GbVector *a, const GbVector *b) {
    if (a->dim == b->dim) {
        return 1;
    }
    raise_fmt(1003, "vector", "%s: these vectors have %zu and %zu dimensions, and only "
              "vectors of one dimension compare", verb, a->dim, b->dim);
    return 0;
}

/* a direction needs a length: a cosine of a zero vector is refused (§4) */
static int vector_has_direction(const char *verb, const GbVector *v, const char *which) {
    if (gb_vec_norm(v->data, v->dim) > 0) {
        return 1;
    }
    raise_fmt(1003, "vector", "%s: %s is all zeros, which has no direction, so no cosine",
              verb, which);
    return 0;
}

static int vector_metric_of(const char *verb, Value m, GbVecMetric *out) {
    if (m.kind == VALUE_STRING) {
        if (strcmp(m.as.string, "l2") == 0) { *out = GB_VEC_L2; return 1; }
        if (strcmp(m.as.string, "cosine") == 0) { *out = GB_VEC_COSINE; return 1; }
        if (strcmp(m.as.string, "ip") == 0) { *out = GB_VEC_IP; return 1; }
    }
    char shown[80];
    if (m.kind == VALUE_STRING) {
        snprintf(shown, sizeof shown, "\"%.60s\"", m.as.string);
    } else {
        kind_phrase(m.kind, shown, sizeof shown);
    }
    raise_fmt(1003, "vector", "%s: the metric is \"l2\", \"cosine\" or \"ip\", not %s", verb,
              shown);
    return 0;
}

typedef struct {
    double distance;
    size_t index;
} VectorHit;

static int vector_hit_order(const void *pa, const void *pb) {
    const VectorHit *a = pa, *b = pb;
    if (a->distance != b->distance) {
        return a->distance < b->distance ? -1 : 1;
    }
    return (a->index > b->index) - (a->index < b->index);
}

/* nearest(q, vs, k[, metric]): every distance, then the k smallest, a tie
   by the lower index, so the answer does not depend on the sort */
static Value vector_nearest(AstExpr *expr) {
    size_t argc = expr->as.call.args.count;
    GbVector *q = vector_arg(expr, 0);
    if (!q) {
        return value_null();
    }
    Value rows = eval_expr(expr->as.call.args.items[1]);
    Value kv = error_action_pending() ? value_null() : eval_expr(expr->as.call.args.items[2]);
    Value mv = argc == 4 && !error_action_pending() ? eval_expr(expr->as.call.args.items[3])
                                                    : value_string("l2");
    Value result = value_null();
    char phrase[48];
    GbVecMetric metric = GB_VEC_L2;
    if (error_action_pending()) {
        goto done;
    }
    if (rows.kind != VALUE_ARRAY) {
        raise_fmt(1003, "vector", "nearest expects an array of vectors as its second argument, "
                  "and this is %s", kind_phrase(rows.kind, phrase, sizeof phrase));
        goto done;
    }
    if (kv.kind != VALUE_NUMBER || kv.as.num.value < 0 ||
        kv.as.num.value != floor(kv.as.num.value)) {
        raise_fmt(1003, "vector", "nearest: k is how many to answer, a whole number from 0, "
                  "and this is %s", kind_phrase(kv.kind, phrase, sizeof phrase));
        goto done;
    }
    if (!vector_metric_of("nearest", mv, &metric)) {
        goto done;
    }
    if (metric == GB_VEC_COSINE && !vector_has_direction("nearest", q, "the query")) {
        goto done;
    }
    double qq;   /* q.q, summed as gb_vec_dot3 sums it, once for every row */
    GB_VEC_SUM4(q->dim, (double)q->data[i] * (double)q->data[i], qq);
    const ArrayStorage *st = rows.as.array.store;
    VectorHit *hits = st->count ? malloc(st->count * sizeof *hits) : NULL;
    if (st->count && !hits) {
        abort();
    }
    for (size_t i = 0; i < st->count; i++) {
        const Value *e = &st->items[i];
        if (e->kind != VALUE_VECTOR) {
            raise_fmt(1003, "vector", "nearest: the element at index %zu is %s, not a vector", i,
                      kind_phrase(e->kind, phrase, sizeof phrase));
            free(hits);
            goto done;
        }
        const GbVector *r = e->as.vector;
        if (r->dim != q->dim) {
            raise_fmt(1003, "vector", "nearest: the element at index %zu has %zu dimensions and "
                      "the query %zu", i, r->dim, q->dim);
            free(hits);
            goto done;
        }
        if (metric == GB_VEC_COSINE) {
            double rr;
            double c = gb_vec_cosine_given(q->data, qq, r->data, q->dim, &rr);
            if (!(rr > 0)) {
                raise_fmt(1003, "vector", "nearest: the element at index %zu is all zeros, which "
                          "has no direction, so no cosine", i);
                free(hits);
                goto done;
            }
            hits[i].distance = 1 - c;
        } else {
            hits[i].distance = gb_vec_distance(metric, q->data, r->data, q->dim);
        }
        hits[i].index = i;
    }
    if (st->count) {
        qsort(hits, st->count, sizeof *hits, vector_hit_order);
    }
    size_t k = kv.as.num.value < (double)st->count ? (size_t)kv.as.num.value : st->count;
    Value *items = k ? malloc(k * sizeof *items) : NULL;
    if (k && !items) {
        abort();
    }
    for (size_t i = 0; i < k; i++) {
        Value hit = value_record(NULL, 0);
        record_set(&hit, "index", value_number_exact((long long)hits[i].index));
        record_set(&hit, "distance", value_number(hits[i].distance));
        items[i] = hit;
    }
    free(hits);
    result = value_array(items, k);
done:
    vector_release(q);
    value_free(rows);
    value_free(kv);
    value_free(mv);
    return result;
}

static Value eval_vector_family(AstExpr *expr) {
    const char *verb = expr->as.call.name;
    size_t argc = expr->as.call.args.count;
    int two = CALL_IS(expr, dot) || CALL_IS(expr, cosine);
    int is_distance = CALL_IS(expr, distance);
    int is_nearest = CALL_IS(expr, nearest);
    size_t lo = two ? 2 : is_distance ? 2 : is_nearest ? 3 : 1;
    size_t hi = two ? 2 : is_distance ? 3 : is_nearest ? 4 : 1;
    if (argc < lo || argc > hi) {
        static const char *const wants[] = {
            "vector", "an array of numbers, the text of one, or a vector",
            "vector_bytes", "a vector",
            "vector_from_bytes", "the bytes of float32s, four an element, little-endian",
            "dot", "two vectors", "cosine", "two vectors",
            "distance", "two vectors and an optional metric (\"l2\", \"cosine\" or \"ip\")",
            "norm", "a vector", "normalize", "a vector",
            "nearest", "a query vector, an array of vectors, how many (k), and an optional metric",
            NULL
        };
        const char *want = "its arguments";
        for (int i = 0; wants[i]; i += 2) {
            if (strcmp(wants[i], verb) == 0) {
                want = wants[i + 1];
            }
        }
        raise_fmt(1003, "invalid function call", "%s expects %s", verb, want);
        return value_null();
    }
    if (is_nearest) {
        return vector_nearest(expr);
    }
    if (CALL_IS(expr, vector)) {
        Value v = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(v);
            return value_null();
        }
        GbVector *made = vector_from_value(v, "vector");
        return made ? value_vector(made) : value_null();
    }
    if (CALL_IS(expr, vector_from_bytes)) {
        Value b = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(b);
            return value_null();
        }
        char phrase[48];
        size_t n = b.kind == VALUE_STRING ? string_length(b.as.string) : 0;
        if (b.kind != VALUE_STRING || n == 0 || n % 4 != 0) {
            if (b.kind != VALUE_STRING) {
                raise_fmt(1003, "vector", "vector_from_bytes expects bytes, and this is %s",
                          kind_phrase(b.kind, phrase, sizeof phrase));
            } else {
                raise_fmt(1003, "vector", "vector_from_bytes: %zu bytes is not a whole number "
                          "of float32s, which are four bytes each%s", n,
                          n == 0 ? " (and a vector has at least one)" : "");
            }
            value_free(b);
            return value_null();
        }
        GbVector *v = vector_new(n / 4);
        const unsigned char *p = (const unsigned char *)b.as.string;
        for (size_t i = 0; i < v->dim; i++) {
            uint32_t bits = (uint32_t)p[4 * i] | (uint32_t)p[4 * i + 1] << 8 |
                            (uint32_t)p[4 * i + 2] << 16 | (uint32_t)p[4 * i + 3] << 24;
            memcpy(&v->data[i], &bits, 4);
            if (!isfinite(v->data[i])) {
                raise_fmt(1003, "vector", "vector_from_bytes: the float32 at index %zu (bytes "
                          "%zu to %zu) is %s, and a vector holds finite numbers", i, 4 * i,
                          4 * i + 3, isnan(v->data[i]) ? "not a number" : "infinite");
                vector_release(v);
                value_free(b);
                return value_null();
            }
        }
        value_free(b);
        return value_vector(v);
    }
    GbVector *a = vector_arg(expr, 0);
    if (!a) {
        return value_null();
    }
    if (CALL_IS(expr, vector_bytes)) {
        char *bytes = malloc(a->dim * 4 + 1);
        if (!bytes) {
            abort();
        }
        for (size_t i = 0; i < a->dim; i++) {
            uint32_t bits;
            memcpy(&bits, &a->data[i], 4);
            for (int j = 0; j < 4; j++) {
                bytes[4 * i + j] = (char)(bits >> (8 * j));
            }
        }
        Value out = value_string_n(bytes, a->dim * 4);
        free(bytes);
        vector_release(a);
        return out;
    }
    if (CALL_IS(expr, norm)) {
        double n = gb_vec_norm(a->data, a->dim);
        vector_release(a);
        return value_number(n);
    }
    if (CALL_IS(expr, normalize)) {
        double n = gb_vec_norm(a->data, a->dim);
        if (!(n > 0)) {
            vector_release(a);
            raise_fmt(1003, "vector", "normalize: this vector is all zeros, which has no "
                      "direction to keep");
            return value_null();
        }
        GbVector *out = vector_new(a->dim);
        for (size_t i = 0; i < a->dim; i++) {
            out->data[i] = (float)((double)a->data[i] / n);
        }
        vector_release(a);
        return value_vector(out);
    }
    GbVector *b = vector_arg(expr, 1);
    if (!b) {
        vector_release(a);
        return value_null();
    }
    Value result = value_null();
    GbVecMetric metric = GB_VEC_L2;
    if (is_distance && argc == 3) {
        Value m = eval_expr(expr->as.call.args.items[2]);
        int ok = !error_action_pending() && vector_metric_of(verb, m, &metric);
        value_free(m);
        if (!ok) {
            goto done;
        }
    }
    if (!vector_same_dim(verb, a, b)) {
        goto done;
    }
    if (CALL_IS(expr, dot)) {
        result = value_number(gb_vec_dot(a->data, b->data, a->dim));
    } else if (CALL_IS(expr, cosine) || metric == GB_VEC_COSINE) {
        if (vector_has_direction(verb, a, "the first vector") &&
            vector_has_direction(verb, b, "the second vector")) {
            result = value_number(is_distance
                                      ? gb_vec_distance(GB_VEC_COSINE, a->data, b->data, a->dim)
                                      : gb_vec_cosine(a->data, b->data, a->dim));
        }
    } else {
        result = value_number(gb_vec_distance(metric, a->data, b->data, a->dim));
    }
done:
    vector_release(a);
    vector_release(b);
    return result;
}

/* `a + b`, `a - b`, `v * n`, `n * v` and `v / n`, elementwise (§4). Called
 * when either side is a vector; takes both, answers or refuses. Each result
 * element is rounded to float32 once, from the double the operation gave. */
static Value vector_binary(int opc, Value left, Value right) {
    Value result = value_null();
    char lp[48], rp[48];
    int lv = left.kind == VALUE_VECTOR, rv = right.kind == VALUE_VECTOR;
    if ((opc == OP_ADD || opc == OP_SUB) && lv && rv) {
        const GbVector *a = left.as.vector, *b = right.as.vector;
        if (vector_same_dim(opc == OP_ADD ? "+" : "-", a, b)) {
            GbVector *out = vector_new(a->dim);
            int ok = 1;
            for (size_t i = 0; i < a->dim; i++) {
                double x = opc == OP_ADD ? (double)a->data[i] + b->data[i]
                                         : (double)a->data[i] - b->data[i];
                if (fabs(x) > FLT_MAX) {
                    raise_fmt(1003, "vector", "the element at index %zu would be beyond a "
                              "float32's range (about 3.4e38)", i);
                    ok = 0;
                    break;
                }
                out->data[i] = (float)x;
            }
            if (ok) {
                result = value_vector(out);
            } else {
                vector_release(out);
            }
        }
    } else if ((opc == OP_MUL && (lv != rv) &&
                (left.kind == VALUE_NUMBER || right.kind == VALUE_NUMBER)) ||
               (opc == OP_DIV && lv && right.kind == VALUE_NUMBER)) {
        const GbVector *a = lv ? left.as.vector : right.as.vector;
        double n = lv ? right.as.num.value : left.as.num.value;
        if (opc == OP_DIV && n == 0) {
            raise_fmt(1003, "vector", "division by zero");
        } else if (!isfinite(n)) {
            raise_fmt(1003, "vector", "a vector scales by a finite number");
        } else {
            GbVector *out = vector_new(a->dim);
            int ok = 1;
            for (size_t i = 0; i < a->dim; i++) {
                double x = opc == OP_DIV ? (double)a->data[i] / n : (double)a->data[i] * n;
                if (fabs(x) > FLT_MAX) {
                    raise_fmt(1003, "vector", "the element at index %zu would be beyond a "
                              "float32's range (about 3.4e38)", i);
                    ok = 0;
                    break;
                }
                out->data[i] = (float)x;
            }
            if (ok) {
                result = value_vector(out);
            } else {
                vector_release(out);
            }
        }
    } else {
        raise_fmt(1003, "vector", "vectors add and subtract with a vector of the same "
                  "dimension and multiply or divide by a number; this is %s %s %s",
                  kind_phrase(left.kind, lp, sizeof lp), opc == OP_ADD ? "+" : opc == OP_SUB ? "-"
                  : opc == OP_MUL ? "*" : opc == OP_DIV ? "/" : "with",
                  kind_phrase(right.kind, rp, sizeof rp));
    }
    value_free(left);
    value_free(right);
    return result;
}

#!/usr/bin/env python3
"""The oracle for search by distance in G-SQL (docs/gsql_vector_design.md §7).

  oracle.py gen SEED CASES       write a table and its queries for oracle.bas
  oracle.py check CASES ANSWERS  judge what G-SQL answered, from Python's own
                                 arithmetic; never from anything gBASIC printed

The sums are in the order docs/vectors_design.md fixes (element i to running
sum i & 3, added (s0 + s1) + (s2 + s3), in double over float32 elements), so a
distance is one double and is compared EXACTLY. The k answers are the k smallest
distances among the rows the WHERE keeps, ties in id order (the order a primary
key scan reads them, which a sort keeps). Copies make ties; the query itself is
among the rows.

Two controls: a left-to-right sum must disagree with some answer (else exact
comparison proved nothing), and each metric's answers must differ from another
metric's on some query (else a metric mapped to the wrong one would pass). Below
five dimensions the specified order is left to right, and the first is not made.
"""
import math
import random
import struct
import sys


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def hexof(xs):
    return struct.pack("<%df" % len(xs), *xs).hex()


def unhex(h):
    data = bytes.fromhex(h)
    return list(struct.unpack("<%df" % (len(data) // 4), data))


def lanes(terms):
    s = [0.0, 0.0, 0.0, 0.0]
    for i, t in enumerate(terms):
        s[i & 3] += t
    return (s[0] + s[1]) + (s[2] + s[3])


def dot(a, b):
    return lanes(x * y for x, y in zip(a, b))


def distance(metric, a, b):
    if metric == "cosine":
        c = dot(a, b) / (math.sqrt(lanes(x * x for x in a)) * math.sqrt(lanes(y * y for y in b)))
        return 1 - max(-1.0, min(1.0, c))
    if metric == "ip":
        return -dot(a, b)
    return math.sqrt(lanes((x - y) * (x - y) for x, y in zip(a, b)))


def plain(metric, a, b):
    """the same distance summed left to right: the control's"""
    if metric == "cosine":
        c = sum(x * y for x, y in zip(a, b)) / (math.sqrt(sum(x * x for x in a)) * math.sqrt(sum(y * y for y in b)))
        return 1 - max(-1.0, min(1.0, c))
    if metric == "ip":
        return -sum(x * y for x, y in zip(a, b))
    return math.sqrt(sum((x - y) * (x - y) for x, y in zip(a, b)))


ACLS = ["staff", "board", "legal"]
SPELLINGS = {"l2": ["<->", "l2_distance", "distance"], "cosine": ["<=>", "cosine_distance", "distance"],
             "ip": ["<#>", "negative_inner_product", "distance"]}


def gen(seed, path):
    rng = random.Random(seed)
    d = rng.choice([3, 17, 64, 1536])
    n = 400 if d < 1536 else 150
    rows = [[f32(rng.gauss(0, 1)) for _ in range(d)] for _ in range(n)]
    for i in range(0, n, 9):           # copies: their distances tie
        rows[(i * 5 + 3) % n] = list(rows[i])
    out = ["dim %d" % d]
    for i, r in enumerate(rows):
        out.append("row %d %s %s" % (i + 1, ACLS[rng.randrange(3)], hexof(r)))
    for qn in range(24):
        q = list(rows[rng.randrange(n)]) if qn % 4 == 0 else [f32(rng.gauss(0, 1)) for _ in range(d)]
        metric = ["l2", "cosine", "ip"][qn % 3]
        spelling = SPELLINGS[metric][(qn // 3) % 3]
        acl = rng.choice(ACLS + ["*"])
        k = rng.choice([1, 5, 10, 40, n + 5])
        out.append("query q%d %s %s %s %d %s" % (qn, metric, spelling, acl, k, hexof(q)))
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def same(text, want):
    got = float(text)
    return got == want and math.copysign(1, got) == math.copysign(1, want)


def check(cases_path, answers_path):
    cases = [l.split() for l in open(cases_path) if l.strip()]
    answers = {}
    for l in open(answers_path):
        f = l.split()
        if f and f[0] == "a":
            answers.setdefault(f[1], []).append((int(f[2]), f[3]))
    rows = {int(c[1]): (c[2], unhex(c[3])) for c in cases if c[0] == "row"}
    checks = bad = plain_differs = metric_differs = 0

    def judge(ok, what):
        nonlocal checks, bad
        checks += 1
        if not ok:
            bad += 1
            if bad <= 10:
                print("MISMATCH " + what)

    for c in cases:
        if c[0] != "query":
            continue
        name, metric, spelling, acl, k, q = c[1], c[2], c[3], c[4], int(c[5]), unhex(c[6])
        kept = [(i, v) for i, (a, v) in sorted(rows.items()) if acl == "*" or a == acl]
        ranked = sorted((distance(metric, q, v), i) for i, v in kept)[:k]
        got = answers.get(name, [])
        judge(len(got) == len(ranked), "%s (%s %s): %d answers, want %d" % (name, metric, spelling, len(got),
                                                                          len(ranked)))
        for (gi, gd), (d, i) in zip(got, ranked):
            judge(gi == i and same(gd, d), "%s (%s %s): got %d:%s want %d:%r" % (name, metric, spelling, gi, gd,
                                                                                   i, d))
            if plain(metric, q, rows[i][1]) != d:
                plain_differs += 1
        for other in ["l2", "cosine", "ip"]:
            if other != metric:
                alt = [i for _, i in sorted((distance(other, q, v), i) for i, v in kept)[:k]]
                if alt != [i for _, i in ranked]:
                    metric_differs += 1
                    break
    nq = sum(1 for c in cases if c[0] == "query")
    print("ok %d checks, %d mismatches; a left-to-right sum differs on %d answers; %d of %d queries rank "
          "differently under another metric" % (checks, bad, plain_differs, metric_differs, nq))
    dim = int(next(c[1] for c in cases if c[0] == "dim"))
    if dim < 5:
        print("note: at %d dimensions the specified order IS left to right, so that control does not apply" % dim)
    elif plain_differs == 0:
        print("MISMATCH control: no answer tells the specified sum order from a plain one")
        bad += 1
    if metric_differs < nq // 2:
        print("MISMATCH control: too few queries tell one metric from another")
        bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1] == "gen":
        gen(int(sys.argv[2]), sys.argv[3])
    else:
        sys.exit(check(sys.argv[2], sys.argv[3]))

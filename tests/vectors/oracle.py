#!/usr/bin/env python3
"""The oracle for gBASIC's vector arithmetic (docs/vectors_design.md §4).

  oracle.py gen SEED CASES       write random cases for tests/vectors/oracle.bas
  oracle.py check CASES ANSWERS  judge what gBASIC answered, from Python's own
                                 arithmetic; never from anything gBASIC printed

The design fixes the order of every sum (element i to running sum i & 3, the
four added (s0 + s1) + (s2 + s3), in double over the float32 elements), so an
answer is a single double and is compared EXACTLY: gBASIC prints the shortest
text that reads back as its double, and float() reads it back. A float32 is
made by struct, Python's own conversion. The control at the end counts the
answers a plain left-to-right sum would have got wrong: if none, the cases
could not tell the specified order from another and the tier proves nothing.
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


def norm(a):
    return math.sqrt(lanes(x * x for x in a))


def l2(a, b):
    return math.sqrt(lanes((x - y) * (x - y) for x, y in zip(a, b)))


def cosine(a, b):
    c = dot(a, b) / (math.sqrt(lanes(x * x for x in a)) * math.sqrt(lanes(y * y for y in b)))
    return max(-1.0, min(1.0, c))


def distance(metric, a, b):
    if metric == "cosine":
        return 1 - cosine(a, b)
    if metric == "ip":
        return -dot(a, b)
    return l2(a, b)


def element(rng, kind):
    if kind == "gauss":
        return f32(rng.gauss(0, 1))
    if kind == "tiny":
        return f32(rng.gauss(0, 1) * 1e-30)
    if kind == "huge":
        return f32(rng.gauss(0, 1) * 1e18)
    if kind == "ints":
        return float(rng.randint(-9, 9))
    # mixed: zeros, negative zeros, wide magnitudes
    return rng.choice([0.0, -0.0, f32(rng.uniform(-1, 1)), f32(rng.gauss(0, 1) * 1e6),
                       f32(rng.gauss(0, 1) * 1e-6)])


def gen(seed, path):
    rng = random.Random(seed)
    out = []
    dims = [1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 37, 64, 1536]
    scales = ["2", "0.5", "3", "-1.25", "0.001", "7", "1e-3", "-0.1"]
    n = 0
    for d in dims:
        for kind in ["gauss", "tiny", "huge", "ints", "mixed", "mixed", "gauss"]:
            a = [element(rng, kind) for _ in range(d)]
            b = [element(rng, kind) for _ in range(d)]
            if n % 11 == 0:
                b = [0.0] * d          # a zero vector: no cosine, no normalize
            elif n % 5 == 0:
                b = list(a)            # itself: a cosine rounding can put past 1
            out.append("pair p%d %s %s %s" % (n, hexof(a), hexof(b), rng.choice(scales)))
            n += 1
    for r, (d, rows) in enumerate([(3, 60), (37, 200), (384, 120), (1, 30), (5, 80)]):
        pool = [[element(rng, "gauss") for _ in range(d)] for _ in range(rows)]
        for i in range(0, rows, 7):    # copies, so ties are broken by index
            pool[(i * 5 + 3) % rows] = list(pool[i])
        q = [element(rng, "gauss") for _ in range(d)]
        for i in range(2, rows, 13):   # the query itself: distance 0, or past it unheld
            pool[i] = list(q)
        k = rng.choice([1, 5, 10, rows, rows + 50])
        out.append("rank r%d %d %s %s" % (r, k, hexof(q), " ".join(hexof(v) for v in pool)))
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def same(text, want):
    got = float(text)
    return got == want and math.copysign(1, got) == math.copysign(1, want)


def shortest_ok(text, x):
    """the text reads back as x's float32, and one fewer digit would not"""
    if f32(float(text)) != x:
        return False
    digits = len(text.lstrip("-").split("e")[0].replace(".", "").lstrip("0")) or 1
    if "e" not in text and "." not in text:
        return True                     # a whole number, printed in full
    if digits == 1:
        return True
    return f32(float("%.*g" % (digits - 1, x))) != x


def check(cases_path, answers_path):
    cases = [l.split() for l in open(cases_path) if l.strip()]
    answers = [l.split() for l in open(answers_path) if l.strip()]
    checks = bad = plain_differs = 0
    by_name = {}
    for a in answers:
        by_name.setdefault(a[0], []).append(a)

    def judge(ok, what):
        nonlocal checks, bad
        checks += 1
        if not ok:
            bad += 1
            if bad <= 10:
                print("MISMATCH " + what)

    for c in cases:
        name = c[1]
        got = by_name.get(name)
        if not got:
            judge(False, "%s: no answer" % name)
            continue
        if c[0] == "pair":
            a, b, s = unhex(c[2]), unhex(c[3]), float(c[4])
            g = got[0]
            want = [dot(a, b), norm(a), norm(b), l2(a, b), -dot(a, b)]
            labels = ["dot", "norm a", "norm b", "l2", "ip"]
            for i, (w, label) in enumerate(zip(want, labels)):
                judge(same(g[1 + i], w), "%s %s: got %s want %r" % (name, label, g[1 + i], w))
            if norm(a) > 0 and norm(b) > 0:
                judge(same(g[6], cosine(a, b)), "%s cosine: got %s want %r" % (name, g[6], cosine(a, b)))
                judge(same(g[7], 1 - cosine(a, b)), "%s cosine distance: got %s" % (name, g[7]))
                judge(g[8] == hexof([f32(x / norm(a)) for x in a]), "%s normalize" % name)
            else:
                judge(g[6] == "-" and g[7] == "-", "%s: a zero vector was given a cosine" % name)
            judge(g[9] == hexof([f32(x + y) for x, y in zip(a, b)]), "%s a + b" % name)
            judge(g[10] == hexof([f32(x - y) for x, y in zip(a, b)]), "%s a - b" % name)
            judge(g[11] == hexof([f32(x * s) for x in a]), "%s a * s" % name)
            judge(g[12] == hexof([f32(x / s) for x in a]), "%s a / s" % name)
            printed = g[13]
            elems = printed[len("vector["):-1].split(",")
            judge(printed.startswith("vector[") and len(elems) == len(a) and
                  all(shortest_ok(t, x) for t, x in zip(elems, a)),
                  "%s printed %s" % (name, printed[:80]))
            if len(a) >= 16 and sum(x * y for x, y in zip(a, b)) != dot(a, b):
                plain_differs += 1
        else:
            k, q = int(c[2]), unhex(c[3])
            rows = [unhex(h) for h in c[4:]]
            for metric, g in zip(["l2", "cosine", "ip"], got):
                ranked = sorted((distance(metric, q, r), i) for i, r in enumerate(rows))[:k]
                hits = g[2:]
                judge(g[1] == metric and len(hits) == len(ranked),
                      "%s %s: %d hits, want %d" % (name, metric, len(hits), len(ranked)))
                for hit, (d, i) in zip(hits, ranked):
                    idx, _, dtext = hit.partition(":")
                    judge(int(idx) == i and same(dtext, d),
                          "%s %s: got %s want %d:%r" % (name, metric, hit, i, d))
    print("ok %d checks, %d mismatches; a left-to-right sum differs on %d long pairs"
          % (checks, bad, plain_differs))
    if plain_differs == 0:
        print("MISMATCH control: no case tells the specified sum order from a plain one")
        bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1] == "gen":
        gen(int(sys.argv[2]), sys.argv[3])
    else:
        sys.exit(check(sys.argv[2], sys.argv[3]))

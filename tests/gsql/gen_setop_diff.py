#!/usr/bin/env python3
# DISTINCT and set operations differential (docs/gsql_operator_spill_design.md
# §9): random rows with repeats and NULLs, random SELECT DISTINCT and
# UNION / INTERSECT / EXCEPT [ALL] queries, nested, written two ways:
#
#   ORDERED    ordered by every column, each answer computed outside G-SQL: SQLite
#              answers each simple SELECT, and the compound is the definition
#              written out over multisets here (SQLite has no INTERSECT ALL or
#              EXCEPT ALL), so the rows are checked by code that is not G-SQL's
#   UNORDERED  the same queries with no ORDER BY and `= ?` for an answer: the
#              suite runs them in memory and spilled and requires the same rows
#              IN THE SAME ORDER, since arrival order is what a spill keeps by
#              sorting twice, and no SQLite order is G-SQL's to compare with
#
#   gen_setop_diff.py SEED QUERIES ORDERED_OUT UNORDERED_OUT
import collections
import random
import sqlite3
import sys

seed, nq, out_o, out_u = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4]
rnd = random.Random(seed)

def maybe_null(v, p=0.15):
    return None if rnd.random() < p else v

def table(name):
    n = rnd.randint(0, 25)
    return [(i, maybe_null(rnd.randint(1, 4)), maybe_null(rnd.randint(1, 3))) for i in range(1, n + 1)]

data = {"t": table("t"), "u": table("u")}
db = sqlite3.connect(":memory:")
head = []
def lit(v):
    return "NULL" if v is None else str(v)
for name, rows in data.items():
    db.execute("CREATE TABLE %s (id INTEGER PRIMARY KEY, a INTEGER, b INTEGER)" % name)
    db.executemany("INSERT INTO %s VALUES (?, ?, ?)" % name, rows)
    head.append("> CREATE TABLE %s (id INTEGER PRIMARY KEY, a INTEGER, b INTEGER)" % name)
    head.append("= done 0")
    if rows:
        head.append("> INSERT INTO %s VALUES " % name + ", ".join("(%s)" % ", ".join(lit(x) for x in r) for r in rows))
        head.append("= done %d" % len(rows))

census = {}
def count(k):
    census[k] = census.get(k, 0) + 1

def simple():
    t = rnd.choice(["t", "u"])
    cols = rnd.choice(["a", "b", "a, b"])
    where = " WHERE a > %d" % rnd.randint(0, 3) if rnd.random() < 0.3 else ""
    d = "DISTINCT " if rnd.random() < 0.3 else ""
    if d:
        count("DISTINCT")
    sql = "SELECT %s%s FROM %s%s" % (d, cols, t, where)
    return sql, cols.count(",") + 1, db.execute(sql).fetchall()

def distinct(rows):
    seen, out = set(), []
    for r in rows:
        if r not in seen:
            seen.add(r)
            out.append(r)
    return out

def combine(op, al, l, r):
    """what each set operation means, over multisets (order is not checked here)"""
    if op == "UNION":
        return l + r if al else distinct(l + r)
    cl, cr = collections.Counter(l), collections.Counter(r)
    if op == "INTERSECT":
        if al:
            return [k for k in cl for _ in range(min(cl[k], cr[k]))]
        return [k for k in cl if cr[k]]
    if al:
        return [k for k in cl for _ in range(max(cl[k] - cr[k], 0))]
    return [k for k in cl if not cr[k]]

def query(depth=0):
    if depth > 1 or rnd.random() < 0.3:
        return simple() + ([],)
    left, nl, lrows, lops = query(depth + 1)
    right, nr, rrows = simple()
    while nr != nl:
        right, nr, rrows = simple()
    op = rnd.choice(["UNION", "INTERSECT", "EXCEPT"])
    al = " ALL" if rnd.random() < 0.5 else ""
    count(op + al)
    # a chain is read left to right only while its operators are of one level:
    # INTERSECT binds tighter, so a chain mixing it with the others is not made
    return "%s %s%s %s" % (left, op, al, right), nl, combine(op, al, lrows, rrows), lops + [op]

lo, lu = [], []
lo.extend(head)
lu.extend(head)
def null_last(r):
    return tuple((v is None, v if v is not None else 0) for v in r)

for q in range(nq):
    sql, n, rows, ops = query()
    if "INTERSECT" in ops and ("UNION" in ops or "EXCEPT" in ops):
        continue
    gorder = " ORDER BY " + ", ".join(str(i + 1) for i in range(n))
    got = sorted(rows, key=null_last)
    lo.append("> " + sql + gorder)
    lo.append("= " + (" | ".join(", ".join(lit(v) for v in r) for r in got) if got else "(none)"))
    lu.append("> " + sql)
    lu.append("= ?")

open(out_o, "w", newline="\n", encoding="utf-8").write("\n".join(lo) + "\n")
open(out_u, "w", newline="\n", encoding="utf-8").write("\n".join(lu) + "\n")
floor = {"DISTINCT": 10, "UNION": 5, "UNION ALL": 5, "INTERSECT": 5, "INTERSECT ALL": 5, "EXCEPT": 5,
         "EXCEPT ALL": 5}
short = [k for k, v in floor.items() if census.get(k, 0) < v]
print("census: " + ", ".join("%s %d" % (k, census.get(k, 0)) for k in sorted(floor)))
if short:
    print("census below its floor: " + ", ".join(short))
    sys.exit(2)

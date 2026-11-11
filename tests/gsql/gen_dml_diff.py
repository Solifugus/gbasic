#!/usr/bin/env python3
# ON DELETE CASCADE, INSERT ... ON CONFLICT and RETURNING differential
# (docs/gsql_fkey_design.md §6, docs/gsql_upsert_design.md,
# docs/gsql_returning_design.md): a random chain of
# tables, each foreign key cascading or NO ACTION by the seed, a table
# referencing itself, and random DELETEs and upserts. SQLite, with foreign keys
# on, runs every statement first; its count of rows affected, or its refusal,
# is the answer, and after each statement every table is read back from SQLite
# whole, so a cascade that went too far, or not far enough, is a mismatch in
# the rows rather than in a count.
#
# Where SQL leaves SQLite and PostgreSQL apart, the generator stays out:
#   - a bare column in DO UPDATE is SQLite's existing row and PostgreSQL's
#     ambiguity (G-SQL's); every column there is qualified
#   - one upsert proposing two rows with one key is changed twice by SQLite
#     and refused by PostgreSQL (and G-SQL); the keys an upsert proposes differ
#   - an upsert SQLite refuses (a unique value elsewhere) is not emitted, since
#     how far G-SQL had got is its own (counted in the census)
#
#   gen_dml_diff.py SEED STATEMENTS OUT
import random
import sqlite3
import sys

seed, nstmt, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
rnd = random.Random(seed)
db = sqlite3.connect(":memory:", isolation_level=None)
db.execute("PRAGMA foreign_keys = ON")
lines = []
census = {}


def count(k):
    census[k] = census.get(k, 0) + 1


def lit(v):
    return "NULL" if v is None else str(v) if isinstance(v, int) else "'%s'" % v


def case(sql, answer):
    lines.append("> " + sql)
    lines.append(answer)


def action():
    return rnd.choice(["ON DELETE CASCADE", ""])


# p <- c <- g, and t referencing itself; a unique column in p for upserts
# p's children always cascade, so every seed has deletes that reach other
# tables; g and t are the seed's, and at least one of them refuses
acts = {"c": "ON DELETE CASCADE", "g": action(), "t": action()}
if acts["g"] and acts["t"]:
    acts[rnd.choice(["g", "t"])] = ""
ddl = [
    "CREATE TABLE p (id INTEGER PRIMARY KEY, u INTEGER UNIQUE, v INTEGER)",
    "CREATE TABLE c (id INTEGER PRIMARY KEY, p INTEGER REFERENCES p %s, v INTEGER)" % acts["c"],
    "CREATE TABLE g (id INTEGER PRIMARY KEY, c INTEGER REFERENCES c %s, v INTEGER)" % acts["g"],
    "CREATE TABLE t (id INTEGER PRIMARY KEY, up INTEGER REFERENCES t %s, v INTEGER)" % acts["t"],
]
for d in ddl:
    db.execute(d)
    case(d.replace("  ", " ").replace(" ,", ","), "= done 0")
for k, a in acts.items():
    count("%s %s" % (k, "cascade" if a else "no action"))


def ids(table):
    return [r[0] for r in db.execute("SELECT id FROM %s" % table)]


nextid = {"p": 1, "c": 1, "g": 1, "t": 1}


def fresh(table, n):
    """n ids no row of the table has held: a refill after deletes adds rows"""
    first = max([nextid[table]] + [i + 1 for i in ids(table)])
    nextid[table] = first + n
    return range(first, first + n)


def fill():
    n = rnd.randint(4, 12)
    rows = [(i, rnd.choice([None, i * 10]), rnd.randint(0, 9)) for i in fresh("p", n)]
    db.executemany("INSERT INTO p VALUES (?, ?, ?)", rows)
    case("INSERT INTO p VALUES " + ", ".join("(%s)" % ", ".join(lit(x) for x in r) for r in rows), "= done %d" % n)
    for child, parent in (("c", "p"), ("g", "c")):
        pid = ids(parent)
        rows = [(i, rnd.choice(pid + [None]), rnd.randint(0, 9)) for i in fresh(child, rnd.randint(4, 20))]
        db.executemany("INSERT INTO %s VALUES (?, ?, ?)" % child, rows)
        case("INSERT INTO %s VALUES " % child + ", ".join("(%s)" % ", ".join(lit(x) for x in r) for r in rows),
             "= done %d" % len(rows))
    # a forest, each row under one written before it, then a few cycles
    rows = []
    for i in fresh("t", rnd.randint(5, 25)):
        rows.append((i, rnd.choice([None] + [r[0] for r in rows]), rnd.randint(0, 9)))
    db.executemany("INSERT INTO t VALUES (?, ?, ?)", rows)
    case("INSERT INTO t VALUES " + ", ".join("(%s)" % ", ".join(lit(x) for x in r) for r in rows),
         "= done %d" % len(rows))
    for _ in range(rnd.randint(0, 2)):
        a, b = rnd.choice(rows)[0], rnd.choice(rows)[0]
        cur = db.execute("UPDATE t SET up = ? WHERE id = ?", (b, a))
        case("UPDATE t SET up = %d WHERE id = %d" % (b, a), "= done %d" % cur.rowcount)


def snapshot():
    for table in ("p", "c", "g", "t"):
        rows = db.execute("SELECT * FROM %s ORDER BY id" % table).fetchall()
        case("SELECT * FROM %s ORDER BY id" % table,
             "= " + (" | ".join(", ".join(lit(v) for v in r) for r in rows) if rows else "(none)"))


def where(table):
    col = rnd.choice(["id", "v"])
    op = rnd.choice(["=", "<", ">", "<="])
    return "%s %s %d" % (col, op, rnd.randint(0, 12) if col == "id" else rnd.randint(0, 9))


def delete():
    table = rnd.choice(["p", "p", "c", "t", "t", "g"])
    sql = "DELETE FROM %s WHERE %s" % (table, where(table))
    before = {k: len(ids(k)) for k in ("p", "c", "g", "t")}
    # the rows the WHERE names, counted before: SQLite counts only those it
    # reached itself, so a row its own cascade removed first is not counted;
    # PostgreSQL, whose cascades run at the end of the statement, counts every
    # row named, and so does G-SQL
    named = db.execute("SELECT count(*) FROM %s WHERE %s" % (table, sql.split(" WHERE ", 1)[1])).fetchone()[0]
    try:
        cur = db.execute(sql)
    except sqlite3.IntegrityError:
        count("delete refused")
        case(sql, "! FOREIGN_KEY 0 still reference")
        return
    after = {k: len(ids(k)) for k in ("p", "c", "g", "t")}
    if any(before[k] - after[k] for k in before if k != table) or before[table] - after[table] > named:
        count("delete cascaded")
    if cur.rowcount != named:
        count("delete counted by SQLite as fewer than it named")
    count("delete")
    case(sql, "= done %d" % named)


def upsert():
    table = rnd.choice(["p", "p", "c", "t"])
    on_u = table == "p" and rnd.random() < 0.4
    keys = rnd.sample(range(1, nextid[table] + 6), rnd.randint(1, 4))
    rows = []
    for k in keys:
        if table == "p":
            rows.append((k, k * 10 if on_u or rnd.random() < 0.5 else None, rnd.randint(0, 9)))
        else:
            parent = "p" if table == "c" else "t"
            rows.append((k, rnd.choice(ids(parent) + [None]), rnd.randint(0, 9)))
    if on_u and len({r[1] for r in rows}) != len(rows):
        return
    target = "(u)" if on_u else "(id)"
    sets = rnd.choice([
        "v = excluded.v",
        "v = %s.v + excluded.v" % table,
        "v = %s.v + 1" % table,
        "v = excluded.v, %s = excluded.%s" % (("u", "u") if table == "p" else (("p", "p") if table == "c" else ("up", "up"))),
    ])
    if on_u and "u =" in sets:
        sets = "v = excluded.v"
    tail = rnd.choice(["DO NOTHING", "DO UPDATE SET " + sets, "DO UPDATE SET " + sets,
                       "DO UPDATE SET %s WHERE %s.v < %d" % (sets, table, rnd.randint(0, 9))])
    conflict = "ON CONFLICT %s %s" % ("" if tail == "DO NOTHING" and rnd.random() < 0.3 else target, tail)
    sql = "INSERT INTO %s VALUES %s %s" % (table, ", ".join("(%s)" % ", ".join(lit(x) for x in r) for r in rows),
                                            conflict)
    returning = rnd.random() < 0.5
    if returning:
        sql += rnd.choice([" RETURNING id, v", " RETURNING *", " RETURNING v * 10 + id AS w"])
    sql = sql.replace("  ", " ")
    db.execute("SAVEPOINT s")
    try:
        cur = db.execute(sql)
        got = cur.fetchall() if returning else None
    except sqlite3.IntegrityError:
        db.execute("ROLLBACK TO s")
        db.execute("RELEASE s")
        count("upsert refused by SQLite, not emitted")
        return
    db.execute("RELEASE s")
    count("upsert " + ("nothing" if tail == "DO NOTHING" else "update"))
    if returning:
        # the rows written, as they were stored: inserted, or updated as they
        # ended; a row left by DO NOTHING or a false WHERE returns nothing
        count("upsert returning rows" if got else "upsert returning none")
        case(sql, "= " + (" | ".join(", ".join(lit(v) for v in r) for r in got) if got else "(none)"))
    else:
        case(sql, "= done %d" % cur.rowcount)


fill()
snapshot()
for _ in range(nstmt):
    if not ids("p") and rnd.random() < 0.5:
        fill()
    (delete if rnd.random() < 0.5 else upsert)()
    snapshot()

open(out, "w", newline="\n", encoding="utf-8").write("\n".join(lines) + "\n")
floor = ["delete", "delete cascaded", "delete refused", "upsert nothing", "upsert update", "upsert returning rows",
         "upsert returning none"]
print("census: " + ", ".join("%s %d" % (k, census.get(k, 0)) for k in sorted(census)))
# a RETURNING that returns nothing needs DO NOTHING, or a false WHERE, on
# every row of one statement: rare by construction, so its floor is one
short = [k for k in floor if census.get(k, 0) < (1 if k == "upsert returning none" else 3)]
if short:
    print("census below its floor: " + ", ".join(short))
    sys.exit(1)

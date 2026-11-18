#!/usr/bin/env python3
"""G-SQL paths, the differential tier (docs/gsql_paths_design.md §7).

Writes a gBASIC program that makes random tables and rows, then reads each
random path two ways: by the path, and by the SQL this generator writes for
it FROM ITS OWN MODEL of §3's rules -- never from the compiler's .sql(), since
an oracle reading the output agrees with it by construction. Rows must be
equal, as print shows them (a record holding unknown cannot be compared with
`=`). Usage: gen_paths_diff.py SEED DB_DIR > program.bas
"""
import random
import sys

TEXTS = ["a", "b", "ab", "Rome", "rome", "it's", 'say "hi"', "é", "", "zz", "-- x", "; y"]


def glit(v):
    """A value as gBASIC source."""
    if v is None:
        return "unknown"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'


def sqllit(v):
    """A value as SQL literal text, for the rows this program inserts."""
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    return "'" + v.replace("'", "''") + "'"


class Table:
    def __init__(self, rng, name):
        self.name = name
        self.key = ["k1"] if rng.random() < 0.6 else ["k1", "k2"]
        self.cols = []   # (name, kind)
        for i in range(rng.randint(2, 5)):
            self.cols.append(("c%d" % i, rng.choice(["integer", "text", "boolean"])))
        self.rows = []
        seen = set()
        for _ in range(rng.randint(0, 25)):
            k = tuple(rng.randint(-3, 9) for _ in self.key)
            if k in seen:
                continue
            seen.add(k)
            row = dict(zip(self.key, k))
            for c, kind in self.cols:
                row[c] = None if rng.random() < 0.2 else value(rng, kind)
            self.rows.append(row)

    def kind(self, c):
        return dict(self.cols).get(c, "integer")

    def all_cols(self):
        return [(k, "integer") for k in self.key] + self.cols


def value(rng, kind):
    if kind == "integer":
        return rng.randint(-5, 12)
    if kind == "boolean":
        return rng.random() < 0.5
    return rng.choice(TEXTS)


class Case:
    """One filter, written twice: as a path's bracket and as SQL."""

    def __init__(self):
        self.params = []

    def p(self, v):
        self.params.append(v)
        return "$%d" % len(self.params)


def comparison(rng, t, case):
    c, kind = rng.choice(t.all_cols())
    ops = ["=", "!="] if kind == "boolean" else ["=", "!=", "<", ">", "<=", ">="]
    op = rng.choice(ops)
    sql_op = "<>" if op == "!=" else op
    shape = rng.random()
    same = [n for n, k in t.all_cols() if k == kind and n != c]
    if shape < 0.15:
        return "is_unknown(.%s)" % c, "%s is null" % c
    if shape < 0.3 and same:
        d = rng.choice(same)
        return ".%s %s .%s" % (c, op, d), "%s %s %s" % (c, sql_op, d)
    if shape < 0.45 and kind == "integer":
        n = rng.randint(-3, 3)
        v = value(rng, kind)
        return "(.%s + %s) %s %s" % (c, glit(n), op, glit(v)), "(%s + %s) %s %s" % (c, case.p(n), sql_op, case.p(v))
    v = None if rng.random() < 0.08 else value(rng, kind)
    return ".%s %s %s" % (c, op, glit(v)), "%s %s %s" % (c, sql_op, case.p(v))


def condition(rng, t, case, depth=0):
    r = rng.random()
    if depth < 2 and r < 0.2:
        a, sa = condition(rng, t, case, depth + 1)
        b, sb = condition(rng, t, case, depth + 1)
        word = rng.choice(["and", "or"])
        return "(%s %s %s)" % (a, word, b), "(%s %s %s)" % (sa, word, sb)
    if depth < 2 and r < 0.3:
        a, sa = condition(rng, t, case, depth + 1)
        return "not (%s)" % a, "not (%s)" % sa
    return comparison(rng, t, case)


def steps_main(seed, db):
    """Tables in a chain, t1.r -> t0, t2.r -> t1, ...; random paths that step
    up and down it. The expected SQL nests IN subqueries, a different form
    from the compiler's scalar and EXISTS ones (§8.2), written from this
    model: a key makes a level one, a step up from one is one, all else many."""
    rng = random.Random(seed)
    n_tables = rng.randint(2, 4)
    tables = []
    for i in range(n_tables):
        t = Table.__new__(Table)
        t.name = "t%d" % i
        t.key = ["k1"]
        t.cols = [("c%d" % j, rng.choice(["integer", "text", "boolean"])) for j in range(rng.randint(1, 3))]
        t.rows = []
        keys = rng.sample(range(-3, 15), rng.randint(0, 12))
        for k in keys:
            row = {"k1": k}
            for c, kind in t.cols:
                row[c] = None if rng.random() < 0.2 else value(rng, kind)
            if i > 0:
                parents = [r["k1"] for r in tables[i - 1].rows]
                row["r"] = rng.choice(parents) if parents and rng.random() < 0.85 else None
            t.rows.append(row)
        tables.append(t)
    out = []
    w = out.append
    w("' generated by tests/gsql/gen_paths_diff.py %d steps -- do not edit" % seed)
    w("load gsql")
    w("checks = { n: 0, bad: 0, rows: 0 }")
    w("function same(label, got, want)")
    w("    checks.n = checks.n + 1")
    w("    if string(want) != \"[]\" and string(want) != \"unknown\" and string(want) != \"0\" then")
    w("        checks.rows = checks.rows + 1")
    w("    end if")
    w("    if string(got) != string(want) then")
    w("        checks.bad = checks.bad + 1")
    w('        print "MISMATCH " + label + ": path " + string(got) + ", sql " + string(want)')
    w("    end if")
    w("    return nothing")
    w("end function")
    w("function first_row(rows)")
    w("    if len(rows) = 0 then")
    w("        return unknown")
    w("    end if")
    w("    return rows[0]")
    w("end function")
    w('db = gsql.connect(%s, { create: true, linger: 0, wait: 600 })' % glit(db))
    for i, t in enumerate(tables):
        cols = ["k1 integer primary key"] + ["%s %s" % c for c in t.cols]
        if i > 0:
            cols.append("r integer references t%d" % (i - 1))
        w('db.exec("create table %s (%s)")' % (t.name, ", ".join(cols)))
        for row in t.rows:
            names = ["k1"] + [c for c, _ in t.cols] + (["r"] if i > 0 else [])
            vals = ", ".join(sqllit(row[c]) for c in names)
            w("db.exec(%s)" % glit("insert into %s (%s) values (%s)" % (t.name, ", ".join(names), vals)))
    for n in range(100):
        # a walk along the chain: each level a table, maybe a key or filters
        at = rng.randrange(n_tables)
        levels = []
        for step in range(rng.randint(1, 4)):
            if step > 0:
                moves = [d for d in (-1, 1) if 0 <= at + d < n_tables]
                at += rng.choice(moves)
            levels.append({"t": tables[at], "i": at})
        case = Case()
        path = "db"
        one = False
        conds = []
        for li, lv in enumerate(levels):
            t = lv["t"]
            path += ("." if True else "") + t.name
            lv_conds = []
            keyed = False
            r = rng.random()
            if r < 0.25:
                k = rng.choice(t.rows)["k1"] if t.rows and rng.random() < 0.7 else rng.randint(-3, 15)
                path += "[%s]" % glit(k)
                lv["key"] = k
                keyed = True
            elif r < 0.55:
                a, sa = condition(rng, t, case)
                path += "[%s]" % a
                lv["filter"] = sa
            if li == 0:
                one = keyed
            else:
                up = lv["i"] < levels[li - 1]["i"]
                one = keyed or (up and one)
        # the expected SQL, inside out, with its own parameter numbering
        sqlcase = Case()

        def sub(li, col):
            lv = levels[li]
            t = lv["t"]
            where = []
            if "key" in lv:
                where.append("k1 = %s" % sqlcase.p(lv["key"]))
            if "filter" in lv:
                # the filter's parameters, renumbered into this statement
                f = lv["filter"]
                for idx in range(len(case.params), 0, -1):
                    f = f.replace("$%d" % idx, "\0%d\0" % idx)
                for idx in range(1, len(case.params) + 1):
                    if "\0%d\0" % idx in f:
                        f = f.replace("\0%d\0" % idx, sqlcase.p(case.params[idx - 1]))
                where.append("(%s)" % f)
            if li > 0:
                prev = levels[li - 1]
                if lv["i"] < prev["i"]:
                    where.append("k1 in (%s)" % sub(li - 1, "r"))      # up: parents of the rows before
                else:
                    where.append("r in (%s)" % sub(li - 1, "k1"))      # down: children of them
            return "select %s from %s%s" % (col, t.name, " where " + " and ".join(where) if where else "")
        sql = sub(len(levels) - 1, "*") + " order by k1"
        params = "[" + ", ".join(glit(v) for v in sqlcase.params) + "]"
        label = glit("case %d %s" % (n, path))
        if one:
            w("same(%s, %s, first_row(db.query(%s, %s)))" % (label, path, glit(sql), params))
        elif rng.random() < 0.3:
            w("same(%s, %s.count(), len(db.query(%s, %s)))" % (label, path, glit(sql), params))
        else:
            w("same(%s, %s.query(), db.query(%s, %s))" % (label, path, glit(sql), params))
    w("db.close()")
    w('print "answered " + string(checks.rows) + " of " + string(checks.n) + " with rows"')
    w('print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"')
    print("\n".join(out))


def chain(rng):
    """Two to three tables in a chain, t1.r -> t0, t2.r -> t1."""
    n_tables = rng.randint(2, 3)
    tables = []
    for i in range(n_tables):
        t = Table.__new__(Table)
        t.name = "t%d" % i
        t.key = ["k1"]
        t.cols = [("c%d" % j, rng.choice(["integer", "text", "boolean"])) for j in range(rng.randint(1, 3))]
        t.rows = []
        for k in rng.sample(range(0, 12), rng.randint(2, 8)):
            row = {"k1": k}
            for c, kind in t.cols:
                row[c] = None if rng.random() < 0.2 else value(rng, kind)
            if i > 0:
                parents = [r["k1"] for r in tables[i - 1].rows]
                row["r"] = rng.choice(parents) if rng.random() < 0.85 else None
            t.rows.append(row)
        tables.append(t)
    return tables


def walk(rng, tables, case):
    """A random path along the chain: its gBASIC text, whether it is one, and
    its levels, each with the SQL condition of its own key or filter."""
    at = rng.randrange(len(tables))
    levels = []
    for step in range(rng.randint(1, 3)):
        if step > 0:
            moves = [d for d in (-1, 1) if 0 <= at + d < len(tables)]
            at += rng.choice(moves)
        levels.append({"t": tables[at], "i": at})
    path = "db"
    one = False
    for li, lv in enumerate(levels):
        t = lv["t"]
        path += "." + t.name
        keyed = False
        r = rng.random()
        if r < 0.3:
            k = rng.choice(t.rows)["k1"] if t.rows and rng.random() < 0.8 else rng.randint(0, 14)
            path += "[%s]" % glit(k)
            lv["key"] = k
            keyed = True
        elif r < 0.6:
            a, sa = condition(rng, t, case)
            path += "[%s]" % a
            lv["filter"] = sa
        one = keyed if li == 0 else keyed or (lv["i"] < levels[li - 1]["i"] and one)
    return path, one, levels


def where_of(levels, case, sqlcase, li):
    """level li's rows, as the generator's own SQL: nested IN subqueries."""
    lv = levels[li]
    where = []
    if "key" in lv:
        where.append("k1 = %s" % sqlcase.p(lv["key"]))
    if "filter" in lv:
        f = lv["filter"]
        for idx in range(len(case.params), 0, -1):
            f = f.replace("$%d" % idx, "\0%d\0" % idx)
        for idx in range(1, len(case.params) + 1):
            if "\0%d\0" % idx in f:
                f = f.replace("\0%d\0" % idx, sqlcase.p(case.params[idx - 1]))
        where.append("(%s)" % f)
    if li > 0:
        prev = levels[li - 1]
        if lv["i"] < prev["i"]:
            where.append("k1 in (select r from %s%s)" % (prev["t"].name, wsql(where_of(levels, case, sqlcase, li - 1))))
        else:
            where.append("r in (select k1 from %s%s)" % (prev["t"].name, wsql(where_of(levels, case, sqlcase, li - 1))))
    return where


def wsql(where):
    return " where " + " and ".join(where) if where else ""


def writes_main(seed, db):
    """Two databases made alike; random writes, by path to one and by the SQL
    this generator writes for them to the other; after each, every table of
    the two must be equal row for row, and both refuse or neither does. A
    path's own NOT_FOUND (a row of one that is not there) is the SQL side
    changing nothing."""
    rng = random.Random(seed)
    tables = chain(rng)
    out = []
    w = out.append
    w("' generated by tests/gsql/gen_paths_diff.py %d writes -- do not edit" % seed)
    w("load gsql")
    w("checks = { n: 0, bad: 0, changed: 0 }")
    w("last = { a: \"\" }")
    w("function code_of(e)")
    w("    if not e then")
    w("        return \"ok\"")
    w("    end if")
    w("    return e.details.code")
    w("end function")
    w("function dump(d)")
    w("    out = \"\"")
    for t in tables:
        w("    out = out + string(d.query(\"select * from %s order by k1\"))" % t.name)
    w("    return out")
    w("end function")
    w("function same(label, a_code, b_code, a, b)")
    w("    checks.n = checks.n + 1")
    w("    if a_code = \"NOT_FOUND\" and b_code = \"ok\" then")
    w("        a_code = \"ok\"")
    w("    end if")
    w("    if a_code != b_code or dump(a) != dump(b) then")
    w("        checks.bad = checks.bad + 1")
    w('        print "MISMATCH " + label + ": path " + a_code + ", sql " + b_code')
    w("    end if")
    w("    now = dump(a)")
    w("    if now != last.a then")
    w("        checks.changed = checks.changed + 1")
    w("    end if")
    w("    last.a = now")
    w("    return nothing")
    w("end function")
    w('a = gsql.connect(%s, { create: true, linger: 0, wait: 600 })' % glit(db + "a"))
    w('b = gsql.connect(%s, { create: true, linger: 0, wait: 600 })' % glit(db + "b"))
    for d in ("a", "b"):
        for i, t in enumerate(tables):
            cols = ["k1 integer primary key"] + ["%s %s" % c for c in t.cols]
            if i > 0:
                cols.append("r integer references t%d" % (i - 1))
            w('%s.exec("create table %s (%s)")' % (d, t.name, ", ".join(cols)))
            for row in t.rows:
                names = ["k1"] + [c for c, _ in t.cols] + (["r"] if i > 0 else [])
                vals = ", ".join(sqllit(row[c]) for c in names)
                w("%s.exec(%s)" % (d, glit("insert into %s (%s) values (%s)" % (t.name, ", ".join(names), vals))))
    w("last.a = dump(a)")
    for n in range(60):
        case = Case()
        sqlcase = Case()
        op = rng.choice(["delete", "update", "update", "upsert", "insert", "stepinsert"])
        if op in ("delete", "update"):
            path, one, levels = walk(rng, tables, case)
            if op == "update" and len(levels) == 1 and "key" in levels[0]:
                op = "upsert-by-walk"
            last = levels[-1]["t"]
            if op == "delete":
                write_a = "x = %s.delete()" % path
                where = wsql(where_of(levels, case, sqlcase, len(levels) - 1))
                sql = "delete from %s%s" % (last.name, where)
            else:
                cols = rng.sample(last.cols, rng.randint(1, len(last.cols)))
                vals = [(c, None if rng.random() < 0.1 else value(rng, kind)) for c, kind in cols]
                rec = "{ " + ", ".join("%s: %s" % (c, glit(v)) for c, v in vals) + " }"
                write_a = "%s = %s" % (path, rec)
                if op == "upsert-by-walk":
                    k = levels[0]["key"]
                    names = ["k1"] + [c for c, _ in vals]
                    ph = [sqlcase.p(k)] + [sqlcase.p(v) for _, v in vals]
                    sql = "insert into %s (%s) values (%s) on conflict (k1) do update set %s" % (
                        last.name, ", ".join(names), ", ".join(ph),
                        ", ".join("%s = excluded.%s" % (c, c) for c, _ in vals))
                else:
                    sets = ", ".join("%s = %s" % (c, sqlcase.p(v)) for c, v in vals)
                    where = wsql(where_of(levels, case, sqlcase, len(levels) - 1))
                    sql = "update %s set %s%s" % (last.name, sets, where)
        elif op == "upsert":
            t = rng.choice(tables)
            k = rng.choice(t.rows)["k1"] if rng.random() < 0.5 else rng.randint(0, 14)
            cols = rng.sample(t.cols, rng.randint(1, len(t.cols)))
            vals = [(c, value(rng, kind)) for c, kind in cols]
            path = "db.%s[%s]" % (t.name, glit(k))
            write_a = "%s = { %s }" % (path, ", ".join("%s: %s" % (c, glit(v)) for c, v in vals))
            names = ["k1"] + [c for c, _ in vals]
            ph = [sqlcase.p(k)] + [sqlcase.p(v) for _, v in vals]
            sql = "insert into %s (%s) values (%s) on conflict (k1) do update set %s" % (
                t.name, ", ".join(names), ", ".join(ph), ", ".join("%s = excluded.%s" % (c, c) for c, _ in vals))
        elif op == "insert":
            t = rng.choice(tables)
            k = rng.randint(0, 14)
            vals = [(c, value(rng, kind)) for c, kind in t.cols if rng.random() < 0.7]
            path = "db.%s" % t.name
            rec = "{ " + ", ".join(["k1: %s" % glit(k)] + ["%s: %s" % (c, glit(v)) for c, v in vals]) + " }"
            write_a = "x = %s.insert(%s)" % (path, rec)
            names = ["k1"] + [c for c, _ in vals]
            ph = [sqlcase.p(k)] + [sqlcase.p(v) for _, v in vals]
            sql = "insert into %s (%s) values (%s)" % (t.name, ", ".join(names), ", ".join(ph))
        else:
            i = rng.randrange(len(tables) - 1)
            parent, child = tables[i], tables[i + 1]
            pk = rng.choice(parent.rows)["k1"] if rng.random() < 0.8 else rng.randint(0, 14)
            k = rng.randint(0, 14)
            vals = [(c, value(rng, kind)) for c, kind in child.cols]
            path = "db.%s[%s].%s" % (parent.name, glit(pk), child.name)
            rec = "{ " + ", ".join(["k1: %s" % glit(k)] + ["%s: %s" % (c, glit(v)) for c, v in vals]) + " }"
            write_a = "x = %s.insert(%s)" % (path, rec)
            names = ["k1"] + [c for c, _ in vals] + ["r"]
            ph = [sqlcase.p(k)] + [sqlcase.p(v) for _, v in vals]
            sql = "insert into %s (%s) select %s, k1 from %s where k1 = %s" % (
                child.name, ", ".join(names), ", ".join(ph), parent.name, sqlcase.p(pk))
            # the path refuses a parent that is not there; SQL inserts nothing
        params = "[" + ", ".join(glit(v) for v in sqlcase.params) + "]"
        w("function op%d_a(db)" % n)
        w("    on error goto next")
        w("    %s" % write_a)
        w("    return code_of(error)")
        w("end function")
        w("function op%d_b(db)" % n)
        w("    on error goto next")
        w("    x = db.exec(%s, %s)" % (glit(sql), params))
        w("    return code_of(error)")
        w("end function")
        w("same(%s, op%d_a(a), op%d_b(b), a, b)" % (glit("op %d %s: %s" % (n, op, write_a)), n, n))
    w("a.close()")
    w("b.close()")
    w('print "changed the data " + string(checks.changed) + " of " + string(checks.n) + " times"')
    w('print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"')
    print("\n".join(out))


def main():
    seed = int(sys.argv[1])
    db = sys.argv[2]
    if len(sys.argv) > 3 and sys.argv[3] == "writes":
        writes_main(seed, db)
        return
    if len(sys.argv) > 3 and sys.argv[3] == "steps":
        steps_main(seed, db)
        return
    rng = random.Random(seed)
    tables = [Table(rng, "t%d" % i) for i in range(rng.randint(1, 3))]
    out = []
    w = out.append
    w("' generated by tests/gsql/gen_paths_diff.py %d -- do not edit" % seed)
    w("load gsql")
    w("checks = { n: 0, bad: 0, rows: 0 }")
    w("function same(label, got, want)")
    w("    checks.n = checks.n + 1")
    w("    if string(want) != \"[]\" and string(want) != \"unknown\" and string(want) != \"0\" then")
    w("        checks.rows = checks.rows + 1")
    w("    end if")
    w("    if string(got) != string(want) then")
    w("        checks.bad = checks.bad + 1")
    w('        print "MISMATCH " + label + ": path " + string(got) + ", sql " + string(want)')
    w("    end if")
    w("    return nothing")
    w("end function")
    w("function first_row(rows)")
    w("    if len(rows) = 0 then")
    w("        return unknown")
    w("    end if")
    w("    return rows[0]")
    w("end function")
    w('db = gsql.connect(%s, { create: true, linger: 0, wait: 600 })' % glit(db))
    for t in tables:
        cols = ", ".join(["%s integer" % k for k in t.key] + ["%s %s" % c for c in t.cols])
        w('db.exec("create table %s (%s, primary key (%s))")' % (t.name, cols, ", ".join(t.key)))
        for row in t.rows:
            names = [c for c, _ in t.all_cols()]
            vals = ", ".join(sqllit(row[c]) for c in names)
            w("db.exec(%s)" % glit("insert into %s (%s) values (%s)" % (t.name, ", ".join(names), vals)))
    order = lambda t: ", ".join(t.key)
    n = 0
    for _ in range(120):
        t = rng.choice(tables)
        case = Case()
        kind = rng.random()
        if kind < 0.2:
            # a key: one row or unknown
            if t.rows and rng.random() < 0.7:
                k = [rng.choice(t.rows)[c] for c in t.key]
            else:
                k = [rng.randint(-3, 9) for _ in t.key]
            path = "db.%s[%s]" % (t.name, ", ".join(glit(v) for v in k))
            where = " and ".join("%s = %s" % (c, case.p(v)) for c, v in zip(t.key, k))
            sql = "select * from %s where %s" % (t.name, where)
            w("same(%s, %s, first_row(db.query(%s, %s)))" % (glit("case %d %s" % (n, path)), path, glit(sql),
                                                          "[" + ", ".join(glit(v) for v in case.params) + "]"))
        else:
            parts = []
            sqls = []
            for _ in range(rng.randint(0, 3)):
                a, sa = condition(rng, t, case)
                parts.append(a)
                sqls.append("(%s)" % sa)
            path = "db.%s" % t.name
            if parts:
                # one bracket of several, or several brackets: both are and
                if rng.random() < 0.5:
                    path += "[" + ", ".join(parts) + "]"
                else:
                    path += "".join("[%s]" % a for a in parts)
            sql = "select * from %s%s order by %s" % (t.name, " where " + " and ".join(sqls) if sqls else "",
                                                     order(t))
            params = "[" + ", ".join(glit(v) for v in case.params) + "]"
            read = rng.choice(["query", "count"])
            if read == "count":
                w("same(%s, %s.count(), len(db.query(%s, %s)))" % (glit("case %d count %s" % (n, path)), path,
                                                                   glit(sql), params))
            else:
                w("same(%s, %s.query(), db.query(%s, %s))" % (glit("case %d %s" % (n, path)), path, glit(sql),
                                                              params))
        n += 1
    w("db.close()")
    # how many cases had something to get wrong: a tier whose every answer is
    # empty passes against a compiler that drops every filter
    w('print "answered " + string(checks.rows) + " of " + string(checks.n) + " with rows"')
    w('print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"')
    print("\n".join(out))


main()

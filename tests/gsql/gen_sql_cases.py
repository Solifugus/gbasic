#!/usr/bin/env python3
# Writes tests/gsql/sql_cases.txt: python3 tests/gsql/gen_sql_cases.py tests/gsql/sql_cases.txt
# Writes tests/gsql/sql_cases.txt. A refusal names the token at fault as
# (text, occurrence); its 1-based character position is computed from the SQL
# here, so the corpus says WHICH token, and the number follows from it.
import sys
out = []
def c(line): out.append(line)
def stmt(sql, *answers, params=()):
    for i, l in enumerate(sql.split("\n")):
        c("> " + l)
    for p in params:
        c("@ " + p)
    for a in answers:
        if isinstance(a, tuple):
            code, tok, msg = a[0], a[1], a[2]
            occ = a[3] if len(a) > 3 else 1
            if tok is None:
                pos = 0   # a refusal with no single place: the table layer's
            else:
                at = -1
                for _ in range(occ):
                    at = sql.index(tok, at + 1)
                pos = len(sql[:at]) + 1
            c(f"! {code} {pos} {msg}")
        else:
            c(a)
def sec(t):
    c("")
    c("# ---- " + t + " " + "-" * max(1, 64 - len(t)))
c("# G-SQL's SQL semantics, in process against the interpreter's host")
c("# (tests/gsql/sql_test.c script). Generated from tests/gsql/gen_sql_cases.py;")
c("# every answer is written by hand there, never captured from a run.")

sec("constants: gBASIC's numbers, text and logic")
stmt("SELECT 1 + 2, 'a' || 'b', 5 / 2, 6 / 2, 7 % 3, 2 * 3.5", "= 3, 'ab', 2.5, 3.0, 1, 7.0")
stmt("SELECT 9007199254740993 + 1, 9007199254740992.0 + 1", "= 9007199254740994, 9007199254740992.0")
# gBASIC degrades an overflowing sum to an inexact number with warning 2112;
# an expression keeps that rule, and an INTEGER column refuses the result
stmt("SELECT 9223372036854775807 + 1", "= 9.223372036854776e+18")
stmt("SELECT NULL = NULL, NULL <> 1, 1 < NULL, NOT NULL", "= NULL, NULL, NULL, NULL")
stmt("SELECT NULL AND FALSE, FALSE AND NULL, NULL OR TRUE, TRUE OR NULL, NULL AND TRUE, NULL OR FALSE",
     "= FALSE, FALSE, TRUE, TRUE, NULL, NULL")
stmt("SELECT NULL IS NULL, 1 IS NULL, NULL IS NOT NULL, 1 IS NOT NULL, UNKNOWN IS NULL",
     "= TRUE, FALSE, FALSE, TRUE, TRUE")
stmt("SELECT 1 IN (1, 2), 3 IN (1, 2), 3 IN (1, NULL), 1 IN (NULL, 1), 3 NOT IN (1, 2), 3 NOT IN (1, NULL)",
     "= TRUE, FALSE, NULL, TRUE, TRUE, NULL")
stmt("SELECT 2 BETWEEN 1 AND 3, 4 BETWEEN 1 AND 3, 2 NOT BETWEEN 1 AND 3, NULL BETWEEN 1 AND 3, 5 BETWEEN NULL AND 3",
     "= TRUE, FALSE, FALSE, NULL, FALSE")
stmt("SELECT 'abc' < 'abd', 'b' > 'abc', 'A' = 'a', 'x' <> 'y'", "= TRUE, TRUE, FALSE, TRUE")
stmt("SELECT 'apple' LIKE 'a%', 'apple' LIKE '_pple', 'apple' LIKE 'b%', 'héllo' LIKE 'h_llo', 'x' NOT LIKE '%', NULL LIKE 'a'",
     "= TRUE, TRUE, FALSE, TRUE, FALSE, NULL")
stmt("SELECT 'a' || NULL, NULL || 'b', 'a' || 'b' || 'c'", "= NULL, NULL, 'abc'")
stmt("SELECT CASE WHEN 1 > 2 THEN 'x' WHEN NULL THEN 'y' ELSE 'z' END, CASE 2 WHEN 1 THEN 'one' WHEN 2 THEN 'two' END, CASE 3 WHEN 1 THEN 'one' END",
     "= 'z', 'two', NULL")
stmt("SELECT coalesce(NULL, NULL, 3, 4), coalesce(NULL), nullif(1, 1), nullif(1, 2)", "= 3, NULL, NULL, 1")
stmt("SELECT lower('AbC'), upper('abc'), length('héllo'), char_length(''), trim('  a b  '), abs(-3), abs(-2.5), mod(7, 3)",
     "= 'abc', 'ABC', 5, 0, 'a b', 3, 2.5, 1")
stmt("SELECT lower(NULL), length(NULL), abs(NULL), round(NULL)", "= NULL, NULL, NULL, NULL")
stmt("SELECT -5, - -5, -(2 - 7), -NULL", "= -5, 5, 5, NULL")
stmt("SELECT 1 WHERE 1 = 2", "= (none)")
stmt("SELECT 1 WHERE NULL", "= (none)")
stmt("SELECT 1 WHERE 1 = 1", "= 1")

sec("no implicit casts (§2): each refusal beside the spelling that works")
stmt("SELECT '1' = 1", ("EXPRESSION", "=", "= compares text with a number"))
stmt("SELECT CAST('1' AS INTEGER) = 1", "= TRUE")
stmt("SELECT 'a' + 1", ("EXPRESSION", "+", "join text with ||"))
stmt("SELECT 'a' || CAST(1 AS TEXT)", "= 'a1'")
stmt("SELECT TRUE + 1", ("EXPRESSION", "+", "one side is a boolean"))
stmt("SELECT 1 || 'a'", ("EXPRESSION", "||", "|| joins text, and one side is a number"))
stmt("SELECT 1 AND TRUE", ("EXPRESSION", "AND", "AND works on booleans, and was given a number"))
stmt("SELECT NOT 'x'", ("EXPRESSION", "NOT", "NOT works on booleans, and was given text"))
stmt("SELECT 1 WHERE 1", ("EXPRESSION", "1", "WHERE needs a boolean, and this condition is a number", 2))
stmt("SELECT lower(1)", ("EXPRESSION", "lower", "lower works on text, and was given a number"))
stmt("SELECT 'a' LIKE 1", ("EXPRESSION", "LIKE", "LIKE matches text, and was given a number"))
# kinds are checked as values meet, so a list that answers before reaching
# the odd item out answers; one that reaches it is refused
stmt("SELECT 1 IN (1, 'a')", "= TRUE")
stmt("SELECT 3 IN (1, 'a')", ("EXPRESSION", "IN", "= compares a number with text"))
stmt("SELECT 1 / 0", ("EXPRESSION", "/", "division by zero"))
stmt("SELECT CAST('x' AS INTEGER)", ("EXPRESSION", "CAST", "expects a valid number string"))
stmt("SELECT CAST(2.5 AS INTEGER)", ("EXPRESSION", "CAST", "CAST to INTEGER does not round"))
stmt("SELECT CAST('2.5' AS NUMBER), CAST(3 AS INTEGER), CAST('true' AS BOOLEAN)", "= 2.5, 3, TRUE")

sec("money and dates are gBASIC's")
stmt("SELECT CAST('1.25' AS MONEY(USD)) + CAST('2.50' AS MONEY(USD)), CAST('1.25' AS MONEY(USD)) * 2",
     "= money 3.75, money 2.50")
stmt("SELECT CAST('1.00' AS MONEY(USD)) + CAST('1.00' AS MONEY(EUR))", ("EXPRESSION", "+", "cannot add money in different currencies (USD and EUR)"))
stmt("SELECT CAST('1.00' AS MONEY(USD)) = CAST('1.00' AS MONEY(EUR))", "= FALSE")
stmt("SELECT CAST('1.00' AS MONEY(USD)) < CAST('1.00' AS MONEY(EUR))", ("EXPRESSION", "<", "cannot order money in different currencies"))
stmt("SELECT CAST('1.00' AS MONEY)", ("EXPRESSION", "CAST", "CAST to MONEY needs its currency"))
stmt("SELECT DATE '2026-10-30' < DATE '2026-11-01', DATE '2026-10-30' = DATE '2026-10-30'", "= TRUE, TRUE")
stmt("SELECT DATE 'not a date'", ("EXPRESSION", "DATE", "expects an ISO-like date string"))

sec("a table: types, keys, NULL and defaults")
stmt("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT NOT NULL, price MONEY(USD), qty INTEGER DEFAULT 1, made DATE)",
     "= done 0")
stmt("INSERT INTO t (id, name, price, made) VALUES (1, 'apple', CAST('1.50' AS MONEY(USD)), DATE '2026-01-05'), (2, 'pear', NULL, NULL), (3, 'fig', CAST('0.75' AS MONEY(USD)), DATE '2026-03-01')",
     "= done 3")
stmt("SELECT * FROM t", "% id:integer, name:string, price:money(USD), qty:integer, made:date",
     "= 1, 'apple', money 1.50, 1, datetime 2026-01-05 | 2, 'pear', NULL, 1, NULL | 3, 'fig', money 0.75, 1, datetime 2026-03-01")
stmt("SELECT name, qty * 2 AS twice, price FROM t WHERE id = 3", "% name:string, twice:any, price:money(USD)", "= 'fig', 2, money 0.75")
stmt("INSERT INTO t VALUES (4, 'kiwi', NULL, NULL, NULL)", "= done 1")
stmt("SELECT qty FROM t WHERE id = 4", "= NULL")
stmt("INSERT INTO t VALUES (5, 'lime', NULL, DEFAULT, NULL)", "= done 1")
stmt("SELECT qty FROM t WHERE id = 5", "= 1")
stmt("INSERT INTO t (id, name) VALUES (1, 'again')", ("DUPLICATE", None, "the primary key already holds that value"))
stmt("INSERT INTO t (id) VALUES (6)", ("NOT_NULL", None, "t.name is NOT NULL"))
stmt("INSERT INTO t (id, name, qty) VALUES (6, 'x', 6 / 2)", ("TYPE", None, "t.qty is integer, and the number is not an exact integer"))
stmt("INSERT INTO t (id, name, qty) VALUES (6, 'x', 3)", "= done 1")
stmt("INSERT INTO t (id, name, made) VALUES (7, 'x', TIMESTAMP '2026-01-05 10:00')", ("TYPE", None, "t.made is date, and the value is a date and time"))
stmt("INSERT INTO t (id, name, price) VALUES (7, 'x', CAST('1' AS MONEY(EUR)))", ("TYPE", None, "t.price is money(USD), and the money is in EUR"))
stmt("INSERT INTO t (id, name) VALUES (7)", ("SYNTAX", "INSERT", "INSERT has 1 value for 2 columns"))
stmt("INSERT INTO t (id, nope) VALUES (7, 1)", ("NOT_FOUND", "(", "table t has no column `nope`"))
stmt("INSERT INTO t (id, id) VALUES (7, 1)", ("SYNTAX", "(", "the column list names id twice"))
stmt("INSERT INTO t (id, name) VALUES (7, name)", ("SYNTAX", "name", "VALUES cannot read a column", 2))
stmt("INSERT INTO nope VALUES (1)", ("NOT_FOUND", "INSERT", "there is no table `nope`"))
stmt("SELECT nope FROM t", ("NOT_FOUND", "nope", "table t has no column `nope`"))
stmt("SELECT t.name, x.name FROM t x", ("NOT_FOUND", "t.name", "the table is called `x` in this statement"))
stmt("SELECT x.name FROM t x WHERE x.id = 1", "= 'apple'")
stmt("SELECT name.first FROM t", ("TYPE", "name", "column name is string, so it has no field `first`"))
stmt("SELECT nope(1)", ("NOT_FOUND", "nope", "`nope` is not a G-SQL function"))
stmt("SELECT max(id) FROM t", "= 6")
stmt("SELECT abs(1, 2)", ("SYNTAX", "abs", "abs takes 1 argument, and was given 2"))

sec("ORDER BY, LIMIT, OFFSET: NULL last ascending, first descending")
stmt("SELECT id, price FROM t ORDER BY price", "= 3, money 0.75 | 1, money 1.50 | 2, NULL | 4, NULL | 5, NULL | 6, NULL")
stmt("SELECT id, price FROM t ORDER BY price DESC, id", "= 2, NULL | 4, NULL | 5, NULL | 6, NULL | 1, money 1.50 | 3, money 0.75")
stmt("SELECT id FROM t ORDER BY id DESC LIMIT 2", "= 6 | 5")
stmt("SELECT id FROM t ORDER BY id LIMIT 2 OFFSET 1", "= 2 | 3")
stmt("SELECT id FROM t ORDER BY id OFFSET 4", "= 5 | 6")
stmt("SELECT id FROM t ORDER BY id LIMIT 0", "= (none)")
stmt("SELECT id, name AS n FROM t ORDER BY n LIMIT 3", "= 1, 'apple' | 3, 'fig' | 4, 'kiwi'")
stmt("SELECT id, name FROM t ORDER BY 2 DESC LIMIT 1", "= 6, 'x'")
stmt("SELECT id FROM t ORDER BY 3", ("SYNTAX", "3", "ORDER BY 3 names no column: the select list has 1"))
stmt("SELECT id FROM t LIMIT -1", ("EXPRESSION", "-", "LIMIT needs a whole number"))
stmt("SELECT id FROM t LIMIT 1.5", ("EXPRESSION", "1.5", "LIMIT needs a whole number"))
stmt("SELECT id FROM t ORDER BY id LIMIT :n", "= 1 | 2 | 3", params=("n = 3",))

sec("a bound of the wrong kind is refused, never seeks to nothing (§5)")
# the differential tier allows the scan to refuse where the path answers, so
# these two are asserted here: a path that skipped the refusal would answer
# (none), and pass the comparison
stmt("SELECT name FROM t WHERE id = '1'", ("EXPRESSION", "=", "= compares a number with text"))
stmt("CREATE TABLE w (id INTEGER PRIMARY KEY, x MONEY)", "= done 0")
stmt("CREATE INDEX w_x ON w (x)", "= done 0")
stmt("INSERT INTO w VALUES (1, CAST('1' AS MONEY(USD))), (2, CAST('2' AS MONEY(EUR))), (3, CAST('3' AS MONEY(USD)))", "= done 3")
stmt("SELECT id FROM w WHERE x > CAST('1' AS MONEY(USD))", ("EXPRESSION", ">", "cannot order money in different currencies"))
stmt("EXPLAIN SELECT id FROM w WHERE x > CAST('1' AS MONEY(USD))", "= 'Scan on w' | '  Filter: (x > CAST('1' AS MONEY(USD)))'")
stmt("SELECT id FROM w WHERE x = CAST('3' AS MONEY(USD))", "= 3")
stmt("EXPLAIN SELECT id FROM w WHERE x = CAST('3' AS MONEY(USD))", "= 'Index Seek on w using w_x (x = CAST('3' AS MONEY(USD)))'")

sec("parameters")
stmt("SELECT name FROM t WHERE id = :id", "= 'pear'", params=("id = 2",))
stmt("SELECT name FROM t WHERE id = $1 OR id = $2 ORDER BY id", "= 'apple' | 'fig'", params=("1 = 1", "2 = 3"))
stmt("SELECT name FROM t WHERE id = :id", ("NOT_FOUND", ":id", "no value was given for the parameter :id"))
stmt("SELECT name FROM t WHERE id = $1", ("NOT_FOUND", "$1", "its parameters must be an array"), params=("x = 1",))
stmt("SELECT :p", "= NULL", params=("p = NULL",))
stmt("SELECT name FROM t WHERE price = :p", "= 'apple'", params=("p = CAST('1.50' AS MONEY(USD))",))

sec("UPDATE and DELETE")
stmt("UPDATE t SET qty = qty + 10 WHERE id <= 3", "= done 3")
stmt("SELECT id, qty FROM t WHERE id <= 3", "= 1, 11 | 2, 11 | 3, 11")
stmt("UPDATE t SET qty = DEFAULT, price = NULL WHERE id = 1", "= done 1")
stmt("SELECT qty, price FROM t WHERE id = 1", "= 1, NULL")
stmt("UPDATE t SET id = id + 100 WHERE id = 6", "= done 1")
stmt("SELECT id, name FROM t WHERE id > 5", "= 106, 'x'")
stmt("UPDATE t SET name = NULL WHERE id = 106", ("NOT_NULL", None, "t.name is NOT NULL"))
stmt("UPDATE t SET nope = 1", ("NOT_FOUND", "nope", "table t has no column `nope` to SET"))
stmt("UPDATE t SET qty = 1, qty = 2", ("SYNTAX", "qty", "SET assigns column qty twice"))
stmt("UPDATE t SET qty = 0 WHERE name = 'nobody'", "= done 0")
stmt("DELETE FROM t WHERE id = 106", "= done 1")
stmt("DELETE FROM t WHERE price IS NULL AND id > 3", "= done 2")
stmt("SELECT id FROM t", "= 1 | 2 | 3")

sec("a statement that fails part way fails its transaction (§4)")
stmt("CREATE TABLE u (k INTEGER PRIMARY KEY, v TEXT UNIQUE)", "= done 0")
stmt("INSERT INTO u VALUES (1, 'a'), (2, 'b'), (3, 'c')", "= done 3")
c("begin write")
stmt("UPDATE u SET v = 'z'", ("DUPLICATE", None, "u_v_key already holds that value [part way]"))
c("rollback")
c("begin write")
stmt("INSERT INTO u VALUES (4, 'd'), (5, 'a')", ("DUPLICATE", None, "row 2 of VALUES [part way]"))
c("rollback")
c("begin write")
stmt("INSERT INTO u VALUES (4, 'a'), (5, 'e')", ("DUPLICATE", None, "row 1 of VALUES"))
stmt("INSERT INTO u VALUES (4, 'd')", "= done 1")
c("commit")
stmt("SELECT k, v FROM u", "= 1, 'a' | 2, 'b' | 3, 'c' | 4, 'd'")

sec("schema")
stmt("CREATE TABLE t (a INTEGER)", ("EXISTS", None, "there is already a table t"))
stmt("CREATE TABLE IF NOT EXISTS t (a INTEGER)", "= done 0")
stmt("DROP TABLE nope", ("NOT_FOUND", None, "there is no table nope"))
stmt("DROP TABLE IF EXISTS nope", "= done 0")
stmt("ALTER TABLE t ADD COLUMN note TEXT DEFAULT 'none'", "= done 0")
stmt("SELECT id, note FROM t", "= 1, 'none' | 2, 'none' | 3, 'none'")
stmt("CREATE INDEX by_name ON t (name)", "= done 0")
stmt("CREATE INDEX by_name ON u (v)", ("EXISTS", "CREATE", "there is already an index `by_name`"))
stmt("CREATE INDEX IF NOT EXISTS by_name ON u (v)", "= done 0")
stmt("CREATE UNIQUE INDEX one_qty ON t (qty)", ("DUPLICATE", None, "so index one_qty cannot be unique"))
stmt("CREATE INDEX bad ON t (nope)", ("NOT_FOUND", "(", "table t has no column `nope`"))
stmt("DROP INDEX by_name", "= done 0")
stmt("DROP INDEX by_name", ("NOT_FOUND", "DROP", "there is no index `by_name`"))
stmt("DROP INDEX IF EXISTS by_name", "= done 0")
stmt("CREATE TABLE k2 (a INTEGER PRIMARY KEY, b INTEGER PRIMARY KEY)", ("SYNTAX", "b", "PRIMARY KEY is declared twice"))
stmt("CREATE TABLE d (a INTEGER DEFAULT 'x')", ("SCHEMA", None, "the default refused by its own column"))
stmt("CREATE TABLE d (a INTEGER DEFAULT a)", ("SYNTAX", "a", "DEFAULT cannot read a column", 2))

sec("records (§3.5)")
stmt("CREATE TABLE assets (id INTEGER PRIMARY KEY, p RECORD)", "= done 0")
stmt("INSERT INTO assets VALUES (1, :p)", "= done 1", params=("p = {vendor: 'acme', size: 3}",))
stmt("INSERT INTO assets VALUES (2, :p)", "= done 1", params=("p = {vendor: 'zeta'}",))
stmt("SELECT id, p.vendor, p.size FROM assets ORDER BY id", "= 1, 'acme', 3 | 2, 'zeta', NULL")
stmt("SELECT id FROM assets WHERE p.vendor = 'zeta'", "= 2")
stmt("SELECT id FROM assets WHERE p.size > 1", "= 1")
stmt("SELECT p.vendor.x FROM assets", ("EXPRESSION", "p.vendor.x", "reads a field of a record"))

sec("aggregates, GROUP BY and HAVING (docs/gsql_aggregate_design.md)")
# g repeats and is NULL twice; n holds an exact integer past 2^53; x holds 3
# and 3.0, which are one value; m holds money in two currencies
stmt("CREATE TABLE s (id INTEGER PRIMARY KEY, g TEXT, n INTEGER, x NUMBER, m MONEY)", "= done 0")
stmt("INSERT INTO s VALUES (1, 'a', 1, 1.5, CAST('1.00' AS MONEY(USD))), (2, 'a', 2, NULL, CAST('2.50' AS MONEY(USD))), "
     "(3, 'b', NULL, 3, CAST('1' AS MONEY(EUR))), (4, NULL, 4, 4, NULL), (5, NULL, 9007199254740993, NULL, NULL), "
     "(6, 'b', 5, 3.0, CAST('2' AS MONEY(EUR)))", "= done 6")
# NULL is skipped by every aggregate but count(*)
stmt("SELECT count(*), count(n), count(x), count(g), count(DISTINCT g), count(DISTINCT x) FROM s",
     "% count:integer, count:integer, count:integer, count:integer, count:integer, count:integer",
     "= 6, 5, 4, 4, 2, 3")
# groups in key order, NULL's group last; the sum stays exact past 2^53
stmt("SELECT g, count(*), sum(n), min(n), max(n) FROM s GROUP BY g",
     "= 'a', 2, 3, 1, 2 | 'b', 2, 5, 5, 5 | NULL, 2, 9007199254740997, 4, 9007199254740993")
# avg is the sum divided by the count, with gBASIC's /, so it is never integer division
stmt("SELECT g, avg(n) FROM s WHERE g IS NOT NULL GROUP BY g", "= 'a', 1.5 | 'b', 5.0")
stmt("SELECT g, sum(m) FROM s GROUP BY g", "= 'a', money 3.50 | 'b', money 3.00 | NULL, NULL")
stmt("SELECT sum(m) FROM s", ("EXPRESSION", "sum", "cannot add money in different currencies (USD and EUR)"))
stmt("SELECT sum(m) FROM s WHERE id < 3", "= money 3.50")
# 3 and 3.0 are one value, so one group, as a unique index would say
stmt("SELECT x, count(*) FROM s GROUP BY x", "= 1.5, 1 | 3, 2 | 4, 1 | NULL, 2")
stmt("SELECT lower(g), count(*) FROM s GROUP BY lower(g)", "= 'a', 2 | 'b', 2 | NULL, 2")
stmt("SELECT upper(g) FROM s GROUP BY g", "= 'A' | 'B' | NULL")
stmt("SELECT g FROM s GROUP BY g HAVING count(n) > 1", "= 'a' | NULL")
stmt("SELECT g FROM s GROUP BY g ORDER BY sum(n) DESC", "= NULL | 'b' | 'a'")
stmt("SELECT g, count(*) AS c FROM s GROUP BY g ORDER BY c, g DESC LIMIT 2", "= NULL, 2 | 'b', 2")
# the whole input is one group, even when it is empty; GROUP BY of nothing is no groups
stmt("SELECT count(*), sum(n), min(n), avg(x) FROM s WHERE id > 99", "= 0, NULL, NULL, NULL")
stmt("SELECT g, count(*) FROM s WHERE id > 99 GROUP BY g", "= (none)")
stmt("SELECT count(*) FROM s HAVING count(*) > 99", "= (none)")
stmt("SELECT count(*)", "= 1")
stmt("SELECT g, n FROM s GROUP BY g", ("SYNTAX", "n", "the select list reads column `n`, which is neither grouped by nor inside an aggregate"))
stmt("SELECT g FROM s GROUP BY g ORDER BY n", ("SYNTAX", "n", "ORDER BY reads column `n`"))
stmt("SELECT g FROM s GROUP BY g HAVING n > 1", ("SYNTAX", "n", "HAVING reads column `n`"))
stmt("SELECT id FROM s WHERE count(*) > 1", ("SYNTAX", "count", "WHERE cannot use an aggregate"))
stmt("SELECT count(*) FROM s GROUP BY count(*)", ("SYNTAX", "count", "GROUP BY cannot use an aggregate", 2))
stmt("SELECT sum(sum(n)) FROM s", ("SYNTAX", "sum", "an aggregate cannot hold another aggregate", 2))
stmt("SELECT sum(g) FROM s", ("EXPRESSION", "sum", "+ adds numbers and money, and one side is text"))
stmt("SELECT min(g), max(g) FROM s", "= 'a', 'b'")
stmt("SELECT g FROM s GROUP BY g HAVING count(*)", ("EXPRESSION", "count", "HAVING needs a boolean, and this condition is a number"))
stmt("SELECT sum(n, x) FROM s", ("SYNTAX", "sum", "sum takes 1 argument, and was given 2"))
stmt("SELECT lower(DISTINCT g) FROM s", ("SYNTAX", "lower", "only an aggregate takes DISTINCT"))
stmt("SELECT stddev(n) FROM s", ("UNSUPPORTED", "stddev", "stddev is not in this release of G-SQL"))
stmt("EXPLAIN SELECT g, count(*) FROM s WHERE id > 1 GROUP BY g HAVING count(*) > 1 ORDER BY 2",
     "= 'Seek on s using the primary key (1 <= id)' | '  Aggregate: group by g' | '  Having: (count(*) > 1)' | '  Order: count(*) (sorted)'")
stmt("EXPLAIN SELECT count(*) FROM s", "= 'Scan on s' | '  Aggregate: one group'")

sec("joins (docs/gsql_join_design.md)")
# dee has no region; order 13 has no customer and order 14 a customer that
# does not exist; only order 11 has a refund
stmt("CREATE TABLE cu (id INTEGER PRIMARY KEY, name TEXT, region TEXT)", "= done 0")
stmt("CREATE TABLE od (id INTEGER PRIMARY KEY, cust INTEGER, total MONEY(USD), note TEXT)", "= done 0")
stmt("CREATE INDEX od_cust ON od (cust)", "= done 0")
stmt("CREATE TABLE rf (od INTEGER PRIMARY KEY, amt MONEY(USD))", "= done 0")
stmt("INSERT INTO cu VALUES (1, 'ann', 'east'), (2, 'bob', 'west'), (3, 'cy', 'east'), (4, 'dee', NULL)", "= done 4")
stmt("INSERT INTO od VALUES (10, 1, CAST('5' AS MONEY(USD)), 'a'), (11, 1, CAST('7' AS MONEY(USD)), NULL), "
     "(12, 2, CAST('3' AS MONEY(USD)), 'b'), (13, NULL, CAST('9' AS MONEY(USD)), 'c'), (14, 9, CAST('1' AS MONEY(USD)), 'd')",
     "= done 5")
stmt("INSERT INTO rf VALUES (11, CAST('7' AS MONEY(USD)))", "= done 1")
stmt("SELECT cu.name, od.id FROM cu JOIN od ON od.cust = cu.id ORDER BY od.id", "= 'ann', 10 | 'ann', 11 | 'bob', 12")
stmt("SELECT cu.name, od.id FROM cu INNER JOIN od ON od.cust = cu.id WHERE cu.id = 2", "= 'bob', 12")
# a left row with no match appears once, its right side NULL
stmt("SELECT cu.name, od.id FROM cu LEFT JOIN od ON od.cust = cu.id ORDER BY cu.id, od.id",
     "= 'ann', 10 | 'ann', 11 | 'bob', 12 | 'cy', NULL | 'dee', NULL")
# the same condition in ON and in WHERE: ON decides the match, WHERE filters after
stmt("SELECT cu.name, od.id FROM cu LEFT JOIN od ON od.cust = cu.id AND od.note IS NOT NULL ORDER BY cu.id, od.id",
     "= 'ann', 10 | 'bob', 12 | 'cy', NULL | 'dee', NULL")
stmt("SELECT cu.name, od.id FROM cu LEFT JOIN od ON od.cust = cu.id WHERE od.note IS NOT NULL ORDER BY cu.id, od.id",
     "= 'ann', 10 | 'bob', 12")
stmt("SELECT cu.name FROM cu LEFT JOIN od ON od.cust = cu.id WHERE od.id IS NULL ORDER BY cu.id", "= 'cy' | 'dee'")
# a NULL key matches nothing, NULL = NULL being unknown
stmt("SELECT count(*) FROM od JOIN cu ON cu.id = od.cust", "= 3")
stmt("SELECT a.name, b.name FROM cu a JOIN cu b ON a.region = b.region AND a.id < b.id ORDER BY a.id", "= 'ann', 'cy'")
stmt("SELECT cu.name, od.id, rf.amt FROM cu JOIN od ON od.cust = cu.id LEFT JOIN rf ON rf.od = od.id ORDER BY od.id",
     "= 'ann', 10, NULL | 'ann', 11, money 7.00 | 'bob', 12, NULL")
stmt("SELECT count(*) FROM cu, rf", "= 4")
stmt("SELECT count(*) FROM cu CROSS JOIN od", "= 20")
stmt("SELECT cu.name, count(od.id), sum(od.total) FROM cu LEFT JOIN od ON od.cust = cu.id GROUP BY cu.name ORDER BY cu.name",
     "= 'ann', 2, money 12.00 | 'bob', 1, money 3.00 | 'cy', 0, NULL | 'dee', 0, NULL")
stmt("SELECT * FROM rf JOIN cu ON cu.id = 1", "% od:integer, amt:money(USD), id:integer, name:string, region:string",
     "= 11, money 7.00, 1, 'ann', 'east'")
stmt("SELECT cu.* FROM cu JOIN rf ON rf.od = 11 WHERE cu.id = 2", "= 2, 'bob', 'west'")
stmt("SELECT name, amt FROM cu JOIN rf ON true WHERE id = 3", "= 'cy', money 7.00")
stmt("SELECT id FROM cu JOIN od ON od.cust = cu.id", ("SYNTAX", "id", "column `id` is in both cu and od"))
stmt("SELECT nope FROM cu JOIN od ON true", ("NOT_FOUND", "nope", "no table of this statement has a column `nope`"))
stmt("SELECT 1 FROM cu JOIN od ON rf.od = od.id JOIN rf ON true", ("SYNTAX", "rf.od", "ON cannot read `rf`: that table is joined after this one"))
stmt("SELECT 1 FROM cu JOIN cu ON true", ("SYNTAX", "cu", "`cu` names two tables of this statement", 2))
stmt("SELECT 1 FROM cu JOIN od ON od.cust = cu.name", ("EXPRESSION", "=", "= compares a number with text"))
stmt("SELECT 1 FROM cu JOIN od ON od.cust", ("EXPRESSION", "od.cust", "ON needs a boolean, and this condition is a number"))
stmt("SELECT 1 FROM cu JOIN od ON count(*) > 1", ("SYNTAX", "count", "ON cannot use an aggregate"))
stmt("EXPLAIN SELECT cu.name FROM cu JOIN od ON od.cust = cu.id",
     "= 'Nested Loop' | '  Scan on cu' | '  Index Seek on od using od_cust (cust = cu.id)'")
stmt("EXPLAIN SELECT cu.name FROM cu LEFT JOIN od ON od.cust = cu.id WHERE od.note = 'a'",
     "= 'Nested Loop' | '  Scan on cu' | '  Left: Index Seek on od using od_cust (cust = cu.id)' | '    Filter: (od.note = 'a')'")
stmt("EXPLAIN SELECT a.name FROM cu a JOIN cu b ON b.id = a.id + 1",
     "= 'Nested Loop' | '  Scan on cu AS a' | '  Point Read on cu AS b using the primary key (id = (a.id + 1))'")

sec("EXPLAIN says which path, and lists only what it did not consume")
stmt("CREATE TABLE o (id INTEGER PRIMARY KEY, cust TEXT, total NUMBER, placed DATE)", "= done 0")
stmt("CREATE INDEX o_cust ON o (cust, placed DESC)", "= done 0")
stmt("EXPLAIN SELECT * FROM o WHERE id = :id", "= 'Point Read on o using the primary key (id = :id)'")
stmt("EXPLAIN SELECT * FROM o WHERE id > 5 AND total > 1",
     "= 'Seek on o using the primary key (5 <= id)' | '  Filter: (total > 1)'")
stmt("EXPLAIN SELECT * FROM o WHERE cust = 'a' AND lower(cust) = 'a'",
     "= 'Index Seek on o using o_cust (cust = 'a')' | '  Filter: (lower(cust) = 'a')'")
stmt("EXPLAIN SELECT * FROM o WHERE cust = 'a' ORDER BY placed DESC LIMIT 5",
     "= 'Index Seek on o using o_cust (cust = 'a')' | '  Order: placed DESC (by the index)' | '  Limit: 5'")
stmt("EXPLAIN SELECT * FROM o WHERE cust = 'a' ORDER BY placed",
     "= 'Index Seek on o using o_cust (cust = 'a') backward' | '  Order: placed (by the index)'")
stmt("EXPLAIN SELECT * FROM o ORDER BY total",
     "= 'Scan on o' | '  Order: total (sorted)'")
stmt("EXPLAIN SELECT * FROM o ORDER BY id DESC",
     "= 'Scan on o backward' | '  Order: id DESC (by the primary key)'")
stmt("EXPLAIN SELECT * FROM o WHERE total = 5",
     "= 'Scan on o' | '  Filter: (total = 5)'")
stmt("EXPLAIN SELECT * FROM o WHERE id BETWEEN 2 AND 4",
     "= 'Seek on o using the primary key (2 <= id <= 4)'")
stmt("EXPLAIN UPDATE o SET total = 0 WHERE cust = :c",
     "= 'Update on o' | '  Index Seek on o using o_cust (cust = :c)'")
stmt("EXPLAIN DELETE FROM o WHERE total < 0",
     "= 'Delete on o' | '  Scan on o' | '    Filter: (total < 0)'")
stmt("EXPLAIN SELECT 1", "= 'Result'")

open(sys.argv[1], "w").write("\n".join(out) + "\n")

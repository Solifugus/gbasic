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
# text meeting a number is read as one (docs/gsql_compare_conversion_design.md,
# Matthew's ruling 2026-11-02), with storing's reader: trimmed, written plainly
stmt("SELECT '1' = 1", "= TRUE")
stmt("SELECT ' 1.0 ' = 1", "= TRUE")
stmt("SELECT '1.5' = 1", "= FALSE")
stmt("SELECT 'one' = 1", ("EXPRESSION", "=", "= compares a number with text that is not a number written in digits"))
stmt("SELECT '1' + 1", ("EXPRESSION", "+", "+ adds numbers and money, and one side is text"))
stmt("SELECT TRUE = 'true'", ("EXPRESSION", "=", "= compares a boolean with text"))
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
# these are asserted here: a path that skipped the refusal would answer
# (none), and pass the comparison. Text that reads as the column's kind seeks
# it (docs/gsql_compare_conversion_design.md §3); text that does not, refuses
stmt("SELECT name FROM t WHERE id = '1'", "= 'apple'")
stmt("SELECT name FROM t WHERE id = ' 1 '", "= 'apple'")
stmt("SELECT name FROM t WHERE id = '1.5'", "= (none)")
stmt("SELECT name FROM t WHERE id IN ('1', '3') ORDER BY id", "= 'apple' | 'fig'")
stmt("SELECT name FROM t WHERE id = 'one'", ("EXPRESSION", "=", "= compares a number with text that is not a number written in digits"))
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
# text into an integer column is read by the store conversion, so a default
# that is no whole number is refused there, as a date column's is
# (docs/gsql_store_conversion_design.md §1a); a default that is not text is
# still the table layer's refusal
stmt("CREATE TABLE d (a INTEGER DEFAULT 'x')", ("TYPE", None, "d.a is integer, and the text given is not a number written in digits"))
stmt("CREATE TABLE d (a INTEGER DEFAULT 2.5)", ("SCHEMA", None, "the default refused by its own column"))
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
     "= 'Seek on s using the primary key (1 <= id)' | '  Filter: (id > 1)' | '  Aggregate: group by g' | '  Having: (count(*) > 1)' | "
     "'  Order: count(*) (sorted)'")
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
# a LEFT join's WHERE may narrow it: a plain comparison is unknown on the
# null-extended row, so that row is dropped whether or not the index was used
stmt("SELECT cu.name, od.id FROM cu LEFT JOIN od ON true WHERE od.cust = 2 ORDER BY cu.id",
     "= 'ann', 12 | 'bob', 12 | 'cy', 12 | 'dee', 12")
stmt("EXPLAIN SELECT cu.name FROM cu LEFT JOIN od ON true WHERE od.cust = 2",
     "= 'Nested Loop' | '  Scan on cu' | '  Left: Index Seek on od using od_cust (cust = 2)' | '    Filter: TRUE'")
stmt("EXPLAIN SELECT a.name FROM cu a JOIN cu b ON b.id = a.id + 1",
     "= 'Nested Loop' | '  Scan on cu AS a' | '  Point Read on cu AS b using the primary key (id = (a.id + 1))'")

sec("SELECT DISTINCT (docs/gsql_distinct_design.md)")
# s's g is a, a, b, NULL, NULL, b and its x 1.5, NULL, 3, 4, NULL, 3.0; with
# no ORDER BY a row comes out where its first copy did, here primary-key order
stmt("SELECT DISTINCT g FROM s", "= 'a' | 'b' | NULL")
stmt("SELECT ALL g FROM s WHERE id < 3", "= 'a' | 'a'")
# NULLs are one value and 3 and 3.0 are one value, as GROUP BY says
stmt("SELECT DISTINCT x FROM s", "= 1.5 | NULL | 3 | 4")
stmt("SELECT DISTINCT g, x FROM s", "= 'a', 1.5 | 'a', NULL | 'b', 3 | NULL, 4 | NULL, NULL")
stmt("SELECT DISTINCT g FROM s ORDER BY g", "= 'a' | 'b' | NULL")
stmt("SELECT DISTINCT g FROM s ORDER BY 1 DESC", "= NULL | 'b' | 'a'")
stmt("SELECT DISTINCT lower(g) AS l FROM s ORDER BY l", "= 'a' | 'b' | NULL")
stmt("SELECT DISTINCT upper(g) FROM s ORDER BY upper(g) DESC", "= NULL | 'B' | 'A'")
# LIMIT and OFFSET count distinct rows, not the rows before them
stmt("SELECT DISTINCT g FROM s LIMIT 2", "= 'a' | 'b'")
stmt("SELECT DISTINCT g FROM s LIMIT 5 OFFSET 1", "= 'b' | NULL")
stmt("SELECT DISTINCT g FROM s ORDER BY g DESC LIMIT 1 OFFSET 1", "= 'b'")
stmt("SELECT DISTINCT region FROM cu", "= 'east' | 'west' | NULL")
stmt("SELECT DISTINCT * FROM cu WHERE id < 3", "= 1, 'ann', 'east' | 2, 'bob', 'west'")
stmt("SELECT DISTINCT cu.name FROM cu JOIN od ON od.cust = cu.id", "= 'ann' | 'bob'")
# over a grouped query it is the outputs that are made distinct: the group
# sizes here, which no GROUP BY of the rows could say
stmt("SELECT DISTINCT count(*) FROM s GROUP BY g", "= 2")
stmt("SELECT DISTINCT count(n) FROM s GROUP BY g", "= 2 | 1")
stmt("SELECT DISTINCT 1", "= 1")
stmt("SELECT DISTINCT g FROM s ORDER BY n",
     ("SYNTAX", "n", "with SELECT DISTINCT, ORDER BY may name only an output column, and `n` is not one"))
stmt("SELECT DISTINCT p FROM assets", ("EXPRESSION", "p", "a record cannot be grouped or made DISTINCT"))
stmt("EXPLAIN SELECT DISTINCT g FROM s ORDER BY g", "= 'Scan on s' | '  Distinct: g' | '  Order: g (sorted)'")
stmt("EXPLAIN SELECT DISTINCT id FROM s ORDER BY id", "= 'Scan on s' | '  Distinct: id' | '  Order: id (by the primary key)'")

sec("UNION, INTERSECT and EXCEPT (docs/gsql_setop_design.md)")
# s's g is a, a, b, NULL, NULL, b; cu's region is east, west, east, NULL
stmt("SELECT g FROM s WHERE id < 3 UNION ALL SELECT g FROM s WHERE id > 4", "= 'a' | 'a' | NULL | 'b'")
stmt("SELECT g FROM s UNION SELECT region FROM cu", "= 'a' | 'b' | NULL | 'east' | 'west'")
stmt("SELECT g FROM s UNION SELECT region FROM cu ORDER BY 1", "= 'a' | 'b' | 'east' | 'west' | NULL")
stmt("SELECT 1 UNION DISTINCT SELECT 1", "= 1")
# 3 and 3.0 are one value and NULLs one value, across the two sides as within one
stmt("SELECT x FROM s INTERSECT SELECT 3", "= 3")
stmt("SELECT x FROM s INTERSECT SELECT NULL", "= NULL")
# the right side has one a, one b and one NULL
stmt("SELECT g FROM s INTERSECT ALL SELECT g FROM s WHERE id IN (1, 3, 4)", "= 'a' | 'b' | NULL")
stmt("SELECT g FROM s EXCEPT SELECT 'a'", "= 'b' | NULL")
stmt("SELECT g FROM s EXCEPT ALL SELECT g FROM s WHERE id IN (1, 3, 4)", "= 'a' | NULL | 'b'")
# INTERSECT binds tighter; UNION and EXCEPT group left to right
stmt("SELECT 1 UNION SELECT 2 INTERSECT SELECT 3", "= 1")
stmt("SELECT 1 UNION SELECT 2 EXCEPT SELECT 1", "= 2")
# values keep their kinds: a number and a text are two rows
stmt("SELECT 3 UNION SELECT '3'", "= 3 | '3'")
# a row is the same only when every column is
stmt("SELECT 1, 'a' UNION SELECT 1, 'b' UNION SELECT 1, 'a'", "= 1, 'a' | 1, 'b'")
stmt("SELECT id, name FROM cu WHERE id < 3 UNION ALL SELECT id, name FROM cu WHERE id > 2 ORDER BY name DESC LIMIT 2 OFFSET 1",
     "% id:integer, name:string", "= 3, 'cy' | 2, 'bob'")
stmt("SELECT id FROM cu WHERE id = 1 UNION SELECT 'x'", "% id:any", "= 1 | 'x'")
stmt("SELECT :a UNION SELECT :b UNION SELECT :a", "= 1 | 2", params=("a = 1", "b = 2"))
stmt("SELECT DISTINCT g FROM s UNION ALL SELECT 'a'", "= 'a' | 'b' | NULL | 'a'")
stmt("SELECT 1, 2 UNION SELECT 1", ("SYNTAX", "UNION", "each side of a UNION must have as many columns as the other: the left has 2 and the right 1"))
stmt("SELECT 1 ORDER BY 1 UNION SELECT 2", ("SYNTAX", "UNION", "ORDER BY, LIMIT and OFFSET go after the last SELECT"))
stmt("SELECT g FROM s UNION SELECT region FROM cu ORDER BY lower(g)",
     ("SYNTAX", "lower", "ORDER BY over a UNION may name only a column of the result, by name or by position, and `lower(g)` is not one"))
stmt("SELECT g FROM s EXCEPT SELECT region FROM cu ORDER BY 2", ("SYNTAX", "2", "ORDER BY 2 names no column: the result has 1"))
# parentheses group as they say: (1 UNION 2) INTERSECT 2 against 1 UNION (2 INTERSECT 2)
stmt("SELECT 1 UNION (SELECT 2 INTERSECT SELECT 2)", "= 1 | 2")
stmt("SELECT 1 INTERSECT (SELECT 1 UNION SELECT 2)", "= 1")
stmt("SELECT 2 UNION (SELECT 1 UNION SELECT 3) ORDER BY 1 DESC LIMIT 2", "= 3 | 2")
stmt("SELECT p FROM assets UNION SELECT p FROM assets", ("EXPRESSION", "UNION", "a record cannot be compared by a UNION, INTERSECT or EXCEPT"))
stmt("SELECT p FROM assets UNION ALL SELECT p FROM assets", "= {\"vendor\":\"acme\",\"size\":3} | {\"vendor\":\"zeta\"} | {\"vendor\":\"acme\",\"size\":3} | {\"vendor\":\"zeta\"}")
stmt("EXPLAIN SELECT g FROM s UNION ALL SELECT region FROM cu WHERE id = 2 ORDER BY 1 LIMIT 3",
     "= 'Union All' | '  Scan on s' | '  Point Read on cu using the primary key (id = 2)' | '  Order: g (sorted)' | '  Limit: 3'")

sec("subqueries (docs/gsql_subquery_design.md)")
# cu: 1 ann east, 2 bob west, 3 cy east, 4 dee NULL; od (id, cust): 10 1,
# 11 1, 12 2, 13 NULL, 14 9; rf holds od 11 only
stmt("SELECT (SELECT max(id) FROM od), (SELECT name FROM cu WHERE id = 2)", "% max:any, name:string", "= 14, 'bob'")
stmt("SELECT (SELECT 1 + 1)", "= 2")
stmt("SELECT name, (SELECT count(*) FROM od WHERE od.cust = cu.id) FROM cu ORDER BY id",
     "= 'ann', 2 | 'bob', 1 | 'cy', 0 | 'dee', 0")
# no rows is NULL; two rows are refused when they happen
stmt("SELECT (SELECT id FROM od WHERE cust = 7)", "= NULL")
stmt("SELECT coalesce((SELECT id FROM od WHERE cust = 7), -1)", "= -1")
stmt("SELECT (SELECT id FROM od WHERE cust = 1)", ("EXPRESSION", "(SELECT", "a subquery used as a value returned more than one row"))
stmt("SELECT (SELECT id FROM od WHERE cust = cu.id) FROM cu ORDER BY id",
     ("EXPRESSION", "(SELECT", "a subquery used as a value returned more than one row"))
stmt("SELECT (SELECT id FROM od WHERE cust = cu.id ORDER BY id DESC LIMIT 1) FROM cu ORDER BY id", "= 11 | 12 | NULL | NULL")
stmt("SELECT (SELECT id, cust FROM od)", ("SYNTAX", "(SELECT", "a subquery used as a value must have one column, and this one has 2"))
stmt("SELECT name FROM cu WHERE id IN (SELECT cust FROM od) ORDER BY id", "= 'ann' | 'bob'")
# NOT IN meets the subquery's NULL: no row is equal, so every answer is unknown or false
stmt("SELECT name FROM cu WHERE id NOT IN (SELECT cust FROM od)", "= (none)")
stmt("SELECT name FROM cu WHERE id NOT IN (SELECT cust FROM od WHERE cust IS NOT NULL) ORDER BY id", "= 'cy' | 'dee'")
# no rows is false, even for NULL on the left; NULL against rows is unknown
stmt("SELECT NULL IN (SELECT id FROM od WHERE id > 99), 13 NOT IN (SELECT id FROM od WHERE id > 99), NULL IN (SELECT id FROM od)",
     "= FALSE, TRUE, NULL")
# seen where unknown is not dropped: the select list
stmt("SELECT 3 NOT IN (SELECT cust FROM od), 1 NOT IN (SELECT cust FROM od), 3 IN (SELECT cust FROM od)", "= NULL, FALSE, NULL")
stmt("SELECT id, id NOT IN (SELECT cust FROM od WHERE od.id >= cu.id + 9) FROM cu ORDER BY id",
     "= 1, FALSE | 2, FALSE | 3, NULL | 4, NULL")
stmt("SELECT 1 FROM cu WHERE name IN (SELECT cust FROM od)", ("EXPRESSION", "IN", "= compares a number with text that is not a number written in digits"))
stmt("SELECT id FROM od WHERE total IN (SELECT amt FROM rf WHERE rf.od = od.id)", "= 11")
stmt("SELECT name FROM cu WHERE EXISTS (SELECT 1 FROM od WHERE od.cust = cu.id) ORDER BY id", "= 'ann' | 'bob'")
stmt("SELECT name FROM cu WHERE NOT EXISTS (SELECT * FROM od WHERE od.cust = cu.id) ORDER BY id", "= 'cy' | 'dee'")
stmt("SELECT EXISTS (SELECT 1 FROM rf), EXISTS (SELECT 1 FROM rf WHERE od = 1)", "% exists:boolean, exists:boolean", "= TRUE, FALSE")
# a bare name is the subquery's own first: od.id, which no cu.id equals
stmt("SELECT name FROM cu WHERE id IN (SELECT id FROM od)", "= (none)")
# two levels out: cu.region read from inside the innermost
stmt("SELECT name FROM cu WHERE EXISTS (SELECT 1 FROM od WHERE od.cust = cu.id AND "
     "EXISTS (SELECT 1 FROM rf WHERE rf.od = od.id AND cu.region = 'east'))", "= 'ann'")
stmt("SELECT name FROM cu WHERE EXISTS (SELECT 1 FROM od WHERE od.cust = cu.id UNION SELECT 1 FROM rf WHERE cu.id = 3) ORDER BY id",
     "= 'ann' | 'bob' | 'cy'")
stmt("SELECT name FROM cu WHERE id IN (SELECT cust FROM od WHERE id = 10 UNION SELECT 3) ORDER BY id", "= 'ann' | 'cy'")
stmt("SELECT name FROM cu WHERE id = (SELECT cust FROM od WHERE id = :o)", "= 'bob'", params=("o = 12",))
stmt("SELECT cu.name, od.id FROM cu JOIN od ON od.cust = cu.id AND od.id = (SELECT max(o2.id) FROM od o2 WHERE o2.cust = cu.id) ORDER BY cu.id",
     "= 'ann', 11 | 'bob', 12")
# an outer row null-extended by a LEFT join reads NULL
stmt("SELECT cu.name, (SELECT count(*) FROM rf WHERE rf.od = od.id) FROM cu LEFT JOIN od ON od.cust = cu.id ORDER BY cu.id, od.id",
     "= 'ann', 0 | 'ann', 1 | 'bob', 0 | 'cy', 0 | 'dee', 0")
# a WHERE conjunct whose subquery reads the second table is checked at that table
stmt("SELECT cu.name, od.id FROM cu JOIN od ON od.cust = cu.id WHERE EXISTS (SELECT 1 FROM rf WHERE rf.od = od.id)", "= 'ann', 11")
# a subquery reading the table's own row is no bound for that table's path
stmt("SELECT id FROM od WHERE id = (SELECT max(o2.id) FROM od o2 WHERE o2.cust = od.cust)", "= 11 | 12 | 14")
stmt("SELECT name FROM cu ORDER BY (SELECT count(*) FROM od WHERE od.cust = cu.id) DESC, id", "= 'ann' | 'bob' | 'cy' | 'dee'")
stmt("SELECT cust, count(*) FROM od GROUP BY cust HAVING count(*) > (SELECT count(*) FROM rf) ORDER BY cust", "= 1, 2")
# a grouped query reaches a grouped column through its subqueries, even two deep
stmt("SELECT region, (SELECT count(*) FROM od WHERE od.cust IN (SELECT c2.id FROM cu c2 WHERE c2.region = cu.region)) "
     "FROM cu GROUP BY region ORDER BY region", "= 'east', 2 | 'west', 1 | NULL, 0")
stmt("SELECT region, (SELECT count(*) FROM od WHERE od.cust = cu.id) FROM cu GROUP BY region",
     ("SYNTAX", "cu.id", "the select list reads column `id`, which is neither grouped by nor inside an aggregate"))
stmt("SELECT (SELECT sum(cu.id) FROM od) FROM cu", ("UNSUPPORTED", "cu.id", "an aggregate in a subquery may read only the subquery's own tables"))
stmt("SELECT count(*) FROM cu GROUP BY (SELECT 1)", ("UNSUPPORTED", "(SELECT", "GROUP BY cannot hold a subquery"))
stmt("SELECT id FROM od LIMIT (SELECT 1)", ("UNSUPPORTED", "(SELECT", "LIMIT cannot hold a subquery"))
stmt("INSERT INTO rf VALUES ((SELECT max(id) FROM od), NULL)", ("UNSUPPORTED", "(SELECT", "INSERT ... VALUES cannot hold a subquery"))
stmt("SELECT name FROM cu WHERE id IN (SELECT nope FROM od)", ("NOT_FOUND", "nope", "table od has no column `nope`"))
stmt("SELECT " + "(SELECT " * 32 + "1" + ")" * 32, "= 1")
stmt("SELECT " + "(SELECT " * 33 + "1" + ")" * 33, ("UNSUPPORTED", "SELECT", "subqueries may nest at most 32 deep", 34))
# UPDATE and DELETE read every row before changing one: a subquery sees the
# table as the statement found it
stmt("CREATE TABLE sq (id INTEGER PRIMARY KEY, v INTEGER)", "= done 0")
stmt("INSERT INTO sq VALUES (1, 1), (2, 5), (3, 9)", "= done 3")
stmt("UPDATE sq SET v = (SELECT max(v) FROM sq) WHERE v < (SELECT avg(v) FROM sq)", "= done 1")
stmt("DELETE FROM sq WHERE v = (SELECT min(v) FROM sq)", "= done 1")
stmt("UPDATE sq SET v = (SELECT count(*) FROM sq s2 WHERE s2.v >= sq.v)", "= done 2")
stmt("SELECT id, v FROM sq", "= 1, 2 | 3, 2")
stmt("EXPLAIN SELECT name FROM cu WHERE EXISTS (SELECT 1 FROM od WHERE od.cust = cu.id)",
     "= 'Scan on cu' | '  Filter: EXISTS (SubPlan 1)' | 'SubPlan 1, per row of the outer query' | "
     "'  Index Seek on od using od_cust (cust = cu.id)'")
stmt("EXPLAIN SELECT id FROM od WHERE id = (SELECT max(od) FROM rf)",
     "= 'Point Read on od using the primary key (id = (SubPlan 1))' | 'SubPlan 1, once' | '  Scan on rf' | "
     "'    Aggregate: one group'")

sec("derived tables and WITH (docs/gsql_derived_design.md)")
# cu: 1 ann east, 2 bob west, 3 cy east, 4 dee NULL; od (id, cust, total):
# 10 1 5, 11 1 7, 12 2 3, 13 NULL 9, 14 9 1
stmt("SELECT cu.name, t.n FROM cu JOIN (SELECT cust, count(*) AS n FROM od GROUP BY cust) t ON t.cust = cu.id ORDER BY cu.id",
     "= 'ann', 2 | 'bob', 1")
stmt("SELECT d.c, d.k FROM (SELECT cust, count(*) FROM od WHERE cust IS NOT NULL GROUP BY cust) AS d (c, k) ORDER BY d.c",
     "= 1, 2 | 2, 1 | 9, 1")
stmt("SELECT d.n FROM (SELECT id AS n FROM od) d WHERE d.n > 12 ORDER BY d.n DESC", "= 14 | 13")
stmt("SELECT * FROM (SELECT name FROM cu) d ORDER BY name DESC", "= 'dee' | 'cy' | 'bob' | 'ann'")
stmt("SELECT d.*, cu.name FROM (SELECT id, total FROM od WHERE id = 12) d JOIN cu ON cu.id = 2",
     "% id:integer, total:money(USD), name:string", "= 12, money 3.00, 'bob'")
# two outputs of one name: readable through *, and by name once renamed
stmt("SELECT * FROM (SELECT 1 AS x, 2 AS x) d", "= 1, 2")
stmt("SELECT x FROM (SELECT 1 AS x, 2 AS x) d", ("SYNTAX", "x", "`d` has two columns called `x`"))
stmt("SELECT a, b FROM (SELECT 1 AS x, 2 AS x) d (a, b)", "= 1, 2")
stmt("SELECT * FROM (SELECT 1, 2) d (a)", ("SYNTAX", "(a)", "the subquery `d` is given 1 column name, and its query has 2 columns"))
stmt("SELECT * FROM cu c (a, b, c)", ("UNSUPPORTED", "(a, b", "a column list renames a derived table's, WITH query's or view's columns"))
stmt("SELECT d.nope FROM (SELECT 1 AS x) d", ("NOT_FOUND", "d.nope", "derived table d has no column `nope`"))
# a record column keeps its fields through a derived table
stmt("SELECT d.p.vendor FROM (SELECT p FROM assets) d", "= 'acme' | 'zeta'")
stmt("SELECT g, count(*) FROM (SELECT CASE WHEN total > CAST('4' AS MONEY(USD)) THEN 'big' ELSE 'small' END AS g FROM od) d "
     "GROUP BY g ORDER BY g", "= 'big', 3 | 'small', 2")
stmt("SELECT count(*) FROM (SELECT cust FROM od UNION SELECT id FROM cu) u", "= 6")
# a derived table inside a correlated subquery reads its outer row
stmt("SELECT name, (SELECT count(*) FROM (SELECT id FROM od WHERE od.cust = cu.id) d) FROM cu ORDER BY id",
     "= 'ann', 2 | 'bob', 1 | 'cy', 0 | 'dee', 0")
# a WITH name used twice, one used by a later one, one shadowing a table
stmt("WITH c AS (SELECT cust FROM od WHERE cust IS NOT NULL) SELECT count(*) FROM c a JOIN c b ON a.cust = b.cust", "= 6")
stmt("WITH a AS (SELECT id, cust FROM od), b AS (SELECT cust FROM a WHERE id > 11) SELECT cust FROM b ORDER BY cust",
     "= 2 | 9 | NULL")
stmt("WITH cu AS (SELECT 7 AS id) SELECT id FROM cu", "= 7")
stmt("WITH w (a, b) AS (SELECT 1) SELECT * FROM w", ("SYNTAX", "(a, b)", "WITH query `w` is given 2 column names, and its query has 1 column"))
stmt("WITH w (a) AS (SELECT id FROM od WHERE id < 11) SELECT a FROM w", "= 10")
stmt("WITH big AS (SELECT cust FROM od WHERE id < 12) SELECT name FROM cu WHERE id IN (SELECT cust FROM big) ORDER BY id",
     "= 'ann'")
stmt("SELECT name FROM cu WHERE id IN (WITH m AS (SELECT cust FROM od WHERE total > CAST('4' AS MONEY(USD))) SELECT cust FROM m) "
     "ORDER BY id", "= 'ann'")
stmt("WITH w AS (SELECT 1 AS one) SELECT one FROM w UNION SELECT one + 1 FROM w ORDER BY 1", "= 1 | 2")
# a WITH query reads only tables and earlier WITH queries, never its statement's rows
stmt("SELECT (WITH w AS (SELECT id FROM od WHERE od.cust = cu.id) SELECT count(*) FROM w) FROM cu",
     ("NOT_FOUND", "cu.id", "a WITH query reads only tables and earlier WITH queries"))
stmt("WITH a AS (SELECT * FROM b), b AS (SELECT 1) SELECT * FROM a", ("NOT_FOUND", "SELECT", "there is no table `b`"))
stmt("EXPLAIN WITH c AS (SELECT cust FROM od) SELECT cu.name FROM cu JOIN c ON c.cust = cu.id",
     "= 'Nested Loop' | '  Scan on cu' | '  Hash CTE Scan on c using (cust = cu.id)' | 'CTE c' | '  Scan on od'")
stmt("EXPLAIN SELECT n FROM (SELECT id AS n FROM od WHERE id = 12) d",
     "= 'Derived Scan on d' | 'Derived d' | '  Point Read on od using the primary key (id = 12)'")

sec("hash joins (docs/gsql_hashjoin_design.md)")
# hj has no index on k, so a join on it is hashed; duplicates on both sides,
# NULLs, 3 against 3.0, and money in two currencies
stmt("CREATE TABLE hj (id INTEGER PRIMARY KEY, k NUMBER, m MONEY, t TEXT)", "= done 0")
stmt("INSERT INTO hj VALUES (1, 1, CAST('1' AS MONEY(USD)), 'a'), (2, 1, CAST('1' AS MONEY(EUR)), 'b'), (3, 3.0, NULL, NULL), "
     "(4, NULL, CAST('2' AS MONEY(USD)), 'a'), (5, 2, CAST('1' AS MONEY(USD)), NULL)", "= done 5")
stmt("SELECT a.id, b.id FROM hj a JOIN hj b ON b.k = a.k ORDER BY a.id, b.id", "= 1, 1 | 1, 2 | 2, 1 | 2, 2 | 3, 3 | 5, 5")
stmt("SELECT s.id, hj.id FROM s JOIN hj ON hj.k = s.x ORDER BY s.id, hj.id", "= 3, 3 | 6, 3")
stmt("SELECT a.id, b.id FROM hj a JOIN hj b ON b.m = a.m ORDER BY a.id, b.id", "= 1, 1 | 1, 5 | 2, 2 | 4, 4 | 5, 1 | 5, 5")
stmt("SELECT a.id, b.id FROM hj a LEFT JOIN hj b ON b.t = a.t AND b.id <> a.id ORDER BY a.id, b.id",
     "= 1, 4 | 2, NULL | 3, NULL | 4, 1 | 5, NULL")
# a key of another kind is never reached, so the refusal `=` would make is
# not made: the allowance every access path has (gsql_sql_design.md §5)
stmt("SELECT count(*) FROM cu JOIN hj ON hj.k = cu.name", "= 0")
stmt("SELECT cu.name, d.n FROM cu JOIN (SELECT cust, count(*) AS n FROM od GROUP BY cust) d ON d.cust = cu.id ORDER BY cu.id",
     "= 'ann', 2 | 'bob', 1")
# a record has no key: it is in no group, and a probe with none reads every row
stmt("SELECT a.id, b.id FROM assets a JOIN (SELECT id, p FROM assets) b ON b.p = a.p ORDER BY a.id", "= 1, 1 | 2, 2")
# a condition reading the inner table on both sides is a filter, never a probe
stmt("SELECT count(*) FROM hj a JOIN hj b ON b.k = b.id", "= 10")
stmt("EXPLAIN SELECT a.id FROM hj a JOIN hj b ON b.k = a.k",
     "= 'Nested Loop' | '  Scan on hj AS a' | '  Hash Scan on hj AS b using (k = a.k)'")
stmt("EXPLAIN SELECT cu.name FROM cu JOIN (SELECT cust FROM od) d ON d.cust = cu.id AND d.cust > 0",
     "= 'Nested Loop' | '  Scan on cu' | '  Hash Derived Scan on d using (cust = cu.id)' | '    Filter: (d.cust > 0)' | "
     "'Derived d' | '  Scan on od'")
# an index is still the better path where there is one
stmt("EXPLAIN SELECT cu.name FROM cu JOIN od ON od.cust = cu.id",
     "= 'Nested Loop' | '  Scan on cu' | '  Index Seek on od using od_cust (cust = cu.id)'")

sec("ALTER TABLE ... RENAME TO (docs/gsql_rename_design.md)")
# the drop-and-rebuild swap: build the new one, drop the old, rename the new
stmt("CREATE TABLE rn (id INTEGER PRIMARY KEY, k INTEGER)", "= done 0")
stmt("INSERT INTO rn VALUES (1, 10), (2, 20)", "= done 2")
stmt("CREATE TABLE rn_new (id INTEGER PRIMARY KEY, k INTEGER, note TEXT)", "= done 0")
stmt("CREATE INDEX rn_new_k ON rn_new (k)", "= done 0")
stmt("INSERT INTO rn_new VALUES (1, 10, 'a'), (2, 20, 'b'), (3, 30, 'c')", "= done 3")
stmt("DROP TABLE rn", "= done 0")
stmt("ALTER TABLE rn_new RENAME TO rn", "= done 0")
stmt("SELECT id, k, note FROM rn ORDER BY id", "= 1, 10, 'a' | 2, 20, 'b' | 3, 30, 'c'")
stmt("SELECT * FROM rn_new", ("NOT_FOUND", "SELECT", "there is no table `rn_new`"))
# its index came along, under its own name
stmt("EXPLAIN SELECT id FROM rn WHERE k = 20", "= 'Index Seek on rn using rn_new_k (k = 20)'")
stmt("ALTER TABLE nope RENAME TO x", ("NOT_FOUND", None, "there is no table nope"))
stmt("ALTER TABLE rn RENAME TO cu", ("EXISTS", None, "there is already a table cu"))
stmt("ALTER TABLE rn RENAME TO rn", ("EXISTS", None, "there is already a table rn"))
# a rename is undone with its transaction
c("begin write")
stmt("ALTER TABLE rn RENAME TO rn2", "= done 0")
stmt("SELECT count(*) FROM rn2", "= 3")
c("rollback")
stmt("SELECT count(*) FROM rn", "= 3")
stmt("SELECT * FROM rn2", ("NOT_FOUND", "SELECT", "there is no table `rn2`"))

sec("views (docs/gsql_view_design.md)")
stmt("CREATE TABLE vo (id INTEGER PRIMARY KEY, cust INTEGER, total MONEY(USD))", "= done 0")
stmt("INSERT INTO vo VALUES (1, 1, CAST('5' AS MONEY(USD))), (2, 1, CAST('7' AS MONEY(USD))), (3, 2, CAST('3' AS MONEY(USD)))",
     "= done 3")
stmt("CREATE VIEW spend (cust, spent) AS SELECT cust, sum(total) FROM vo GROUP BY cust", "= done 0")
stmt("SELECT * FROM spend ORDER BY cust", "% cust:integer, spent:any", "= 1, money 12.00 | 2, money 3.00")
stmt("SELECT cu.name, s.spent FROM cu JOIN spend s ON s.cust = cu.id ORDER BY cu.id", "= 'ann', money 12.00 | 'bob', money 3.00")
stmt("SELECT a FROM spend s (a, b) WHERE b > CAST('5' AS MONEY(USD))", "= 1")
# a view of a view
stmt("CREATE VIEW big AS SELECT cust FROM spend WHERE spent > CAST('5' AS MONEY(USD))", "= done 0")
stmt("SELECT * FROM big", "= 1")
stmt("EXPLAIN SELECT * FROM big",
     "= 'View Scan on big' | 'View big' | '  View Scan on spend' | '    Filter: (spent > CAST('5' AS MONEY(USD)))' | "
     "'  View spend' | '    Scan on vo' | '      Aggregate: group by cust'")
# one namespace with tables
stmt("CREATE VIEW spend AS SELECT 1", ("EXISTS", None, "there is already a view spend"))
stmt("CREATE VIEW cu AS SELECT 1", ("EXISTS", None, "there is already a table cu"))
stmt("CREATE TABLE spend (x INTEGER)", ("EXISTS", None, "there is already a view spend"))
# planned when it is created, so a view that cannot run is refused then
stmt("CREATE VIEW bad AS SELECT nope FROM vo", ("NOT_FOUND", "nope", "table vo has no column `nope`"))
stmt("CREATE VIEW pv AS SELECT :x", ("SYNTAX", "SELECT", "a view cannot hold parameters"))
stmt("CREATE VIEW w2 (a, b) AS SELECT 1", ("SYNTAX", "(a, b)", "view `w2` is given 2 column names, and its query has 1 column"))
# read-only
stmt("INSERT INTO spend VALUES (1, 2)", ("UNSUPPORTED", "INSERT", "`spend` is a view, and a view is read-only"))
stmt("UPDATE spend SET cust = 1", ("UNSUPPORTED", "UPDATE", "`spend` is a view, and a view is read-only"))
# a view may not come to read itself, however many views it goes through
stmt("CREATE VIEW c1 AS SELECT 1 AS x", "= done 0")
stmt("CREATE VIEW c2 AS SELECT x FROM c1", "= done 0")
stmt("CREATE OR REPLACE VIEW c1 AS SELECT x FROM c2", ("SYNTAX", "SELECT", "view `c1` reads itself: `c1` reads `c2` reads `c1`"))
stmt("CREATE OR REPLACE VIEW c1 AS SELECT 2 AS x", "= done 0")
stmt("SELECT x FROM c2", "= 2")
# a change under a view is never blocked: the view fails clearly when used,
# and runs again once the drop-and-rebuild puts back what it reads
stmt("DROP TABLE vo", "= done 0")
stmt("SELECT * FROM spend", ("NOT_FOUND", "SELECT", "view `spend` cannot run: there is no table `vo`"))
stmt("SELECT * FROM big", ("NOT_FOUND", "SELECT", "view `spend` cannot run: there is no table `vo`"))
stmt("CREATE TABLE vo_new (id INTEGER PRIMARY KEY, cust INTEGER, total MONEY(USD))", "= done 0")
stmt("INSERT INTO vo_new VALUES (1, 3, CAST('9' AS MONEY(USD)))", "= done 1")
stmt("ALTER TABLE vo_new RENAME TO vo", "= done 0")
stmt("SELECT * FROM big", "= 3")
# SELECT * reflects the table as it is now
stmt("CREATE VIEW allv AS SELECT * FROM vo", "= done 0")
stmt("ALTER TABLE vo ADD COLUMN note TEXT", "= done 0")
stmt("SELECT * FROM allv", "% id:integer, cust:integer, total:money(USD), note:string", "= 1, 3, money 9.00, NULL")
stmt("DROP VIEW allv", "= done 0")
stmt("DROP VIEW allv", ("NOT_FOUND", None, "there is no view allv"))
stmt("DROP VIEW IF EXISTS allv", "= done 0")
stmt("CREATE TABLE allv (id INTEGER PRIMARY KEY)", "= done 0")
stmt("DROP TABLE allv", "= done 0")

sec("foreign keys (docs/gsql_fkey_design.md)")
stmt("CREATE TABLE mem (member_id INTEGER PRIMARY KEY, name TEXT)", "= done 0")
stmt("INSERT INTO mem VALUES (1, 'ann'), (2, 'bob')", "= done 2")
# two foreign keys between the same two tables, one of them only declared
stmt("CREATE TABLE tx (id INTEGER PRIMARY KEY, orig INTEGER REFERENCES mem (member_id), dest INTEGER, "
     "CONSTRAINT destination FOREIGN KEY (dest) REFERENCES mem NOT ENFORCED)", "= done 0")
stmt("INSERT INTO tx VALUES (10, 1, 2)", "= done 1")
stmt("INSERT INTO tx VALUES (11, 3, 1)",
     ("FOREIGN_KEY", None, "foreign key `tx_orig_fkey`: this row of `tx` references a row of `mem` that is not there"))
# NULL references nothing, and NOT ENFORCED is never checked
stmt("INSERT INTO tx VALUES (12, NULL, 99)", "= done 1")
# a refusal before the write leaves an explicit transaction usable
c("begin write")
stmt("INSERT INTO tx VALUES (13, 9, 1)", ("FOREIGN_KEY", None, "foreign key `tx_orig_fkey`"))
stmt("INSERT INTO tx VALUES (13, 2, 1)", "= done 1")
c("rollback")
stmt("SELECT count(*) FROM tx", "= 2")
stmt("UPDATE tx SET orig = 5 WHERE id = 10", ("FOREIGN_KEY", None, "references a row of `mem` that is not there"))
stmt("UPDATE tx SET orig = 2 WHERE id = 10", "= done 1")
# a parent still referenced is neither deleted nor re-keyed; another change is fine
stmt("DELETE FROM mem WHERE member_id = 2",
     ("FOREIGN_KEY", None, "foreign key `tx_orig_fkey`: 1 row of `tx` still references this row of `mem`"))
stmt("UPDATE mem SET member_id = 7 WHERE member_id = 2", ("FOREIGN_KEY", None, "still references this row of `mem`"))
stmt("UPDATE mem SET name = 'b2' WHERE member_id = 2", "= done 1")
stmt("DELETE FROM mem WHERE member_id = 1", "= done 1")
# a composite key, and MATCH SIMPLE
stmt("CREATE TABLE pc (a INTEGER, b TEXT, PRIMARY KEY (a, b))", "= done 0")
stmt("INSERT INTO pc VALUES (1, 'x')", "= done 1")
stmt("CREATE TABLE cc (id INTEGER PRIMARY KEY, a INTEGER, b TEXT, FOREIGN KEY (a, b) REFERENCES pc)", "= done 0")
stmt("INSERT INTO cc VALUES (1, 1, 'x')", "= done 1")
stmt("INSERT INTO cc VALUES (2, 1, 'y')", ("FOREIGN_KEY", None, "foreign key `cc_a_b_fkey`"))
stmt("INSERT INTO cc VALUES (3, 1, NULL)", "= done 1")
# what a foreign key may reference
stmt("CREATE TABLE bad1 (x TEXT REFERENCES mem)",
     ("SCHEMA", "REFERENCES", "cannot reference `mem`: `bad1.x` is string and `mem.member_id` is integer"))
stmt("CREATE TABLE bad2 (x INTEGER REFERENCES mem (name))", ("SCHEMA", "REFERENCES", "the primary key of `mem` is not (name)"))
stmt("CREATE TABLE bad3 (x INTEGER REFERENCES nope)", ("NOT_FOUND", "REFERENCES", "there is no table `nope`"))
stmt("CREATE TABLE bad4 (x INTEGER, y INTEGER, FOREIGN KEY (x, y) REFERENCES mem)",
     ("SCHEMA", "FOREIGN", "names 2 columns, and references 1"))
stmt("CREATE TABLE bad5 (x INTEGER, CONSTRAINT destination FOREIGN KEY (x) REFERENCES mem)",
     ("EXISTS", "CONSTRAINT", "there is already a constraint destination"))
stmt("SELECT * FROM bad1", ("NOT_FOUND", "SELECT", "there is no table `bad1`"))
# refused inside a transaction, the declaration leaves it usable
c("begin write")
stmt("CREATE TABLE bad6 (x INTEGER REFERENCES nope)", ("NOT_FOUND", "REFERENCES", "there is no table `nope`"))
stmt("SELECT * FROM bad6", ("NOT_FOUND", "SELECT", "there is no table `bad6`"))
stmt("CREATE TABLE ok6 (x INTEGER REFERENCES mem)", "= done 0")
c("rollback")
# added over existing rows: refused while one references nothing
stmt("CREATE TABLE lone (id INTEGER PRIMARY KEY, m INTEGER)", "= done 0")
stmt("INSERT INTO lone VALUES (1, 2), (2, 42)", "= done 2")
stmt("ALTER TABLE lone ADD CONSTRAINT lone_m FOREIGN KEY (m) REFERENCES mem",
     ("FOREIGN_KEY", "CONSTRAINT", "1 row of `lone` references no row of `mem`; fix them, or declare it NOT ENFORCED"))
stmt("DELETE FROM lone WHERE id = 2", "= done 1")
stmt("ALTER TABLE lone ADD CONSTRAINT lone_m FOREIGN KEY (m) REFERENCES mem", "= done 0")
stmt("ALTER TABLE lone DROP CONSTRAINT lone_m", "= done 0")
stmt("ALTER TABLE lone DROP CONSTRAINT lone_m", ("NOT_FOUND", "ALTER", "there is no constraint lone_m"))
stmt("ALTER TABLE lone DROP CONSTRAINT tx_orig_fkey", ("NOT_FOUND", "ALTER", "table lone has no constraint tx_orig_fkey"))
# a table referencing itself; a row may not reference one its own statement writes
stmt("CREATE TABLE emp (id INTEGER PRIMARY KEY, boss INTEGER REFERENCES emp)", "= done 0")
stmt("INSERT INTO emp VALUES (1, NULL)", "= done 1")
stmt("INSERT INTO emp VALUES (2, 1)", "= done 1")
stmt("INSERT INTO emp VALUES (3, 3)", ("FOREIGN_KEY", None, "foreign key `emp_boss_fkey`"))
stmt("DELETE FROM emp WHERE id = 1", ("FOREIGN_KEY", None, "1 row of `emp` still references this row of `emp`"))
# drop and rebuild the parent under its foreign keys: never blocked, and
# checked again once a table of that name returns
stmt("DROP TABLE mem", "= done 0")
stmt("INSERT INTO tx VALUES (20, 99, NULL)", "= done 1")
stmt("CREATE TABLE mem_new (member_id INTEGER PRIMARY KEY, name TEXT)", "= done 0")
stmt("INSERT INTO mem_new VALUES (2, 'bob')", "= done 1")
stmt("ALTER TABLE mem_new RENAME TO mem", "= done 0")
stmt("INSERT INTO tx VALUES (21, 98, NULL)", ("FOREIGN_KEY", None, "foreign key `tx_orig_fkey`"))
# a child renamed carries its keys; a child dropped takes them with it
stmt("ALTER TABLE tx RENAME TO tx2", "= done 0")
stmt("INSERT INTO tx2 VALUES (22, 97, NULL)", ("FOREIGN_KEY", None, "this row of `tx2` references a row of `mem`"))
stmt("DROP TABLE tx2", "= done 0")
stmt("CREATE TABLE tx (id INTEGER PRIMARY KEY)", "= done 0")
stmt("ALTER TABLE tx DROP CONSTRAINT tx_orig_fkey", ("NOT_FOUND", "ALTER", "there is no constraint tx_orig_fkey"))

sec("INSERT ... SELECT (docs/gsql_insert_select_design.md)")
stmt("CREATE TABLE src (id INTEGER PRIMARY KEY, k INTEGER, note TEXT DEFAULT 'none')", "= done 0")
stmt("INSERT INTO src VALUES (1, 10, 'a'), (2, 20, 'b'), (3, NULL, 'c')", "= done 3")
stmt("CREATE TABLE dst (id INTEGER PRIMARY KEY, k INTEGER, note TEXT DEFAULT 'none')", "= done 0")
# a column list, and a column it leaves out takes its default
stmt("INSERT INTO dst (id, k) SELECT id, k FROM src WHERE k IS NOT NULL", "= done 2")
stmt("SELECT id, k, note FROM dst ORDER BY id", "= 1, 10, 'none' | 2, 20, 'none'")
# into itself: the query is read whole before a row is written, so once
stmt("INSERT INTO dst SELECT id + 10, k, note FROM dst", "= done 2")
stmt("SELECT count(*) FROM dst", "= 4")
stmt("INSERT INTO dst WITH big AS (SELECT * FROM src WHERE id = 3) SELECT id + 100, k, note FROM big", "= done 1")
stmt("INSERT INTO dst (id, note) SELECT s.id + 200, d.note FROM src s JOIN dst d ON d.id = s.id ORDER BY s.id", "= done 2")
stmt("SELECT id, note FROM dst WHERE id > 100 ORDER BY id", "= 103, 'c' | 201, 'none' | 202, 'none'")
stmt("INSERT INTO dst (id, k) SELECT id FROM src", ("SYNTAX", "SELECT", "the query gives 1 column for 2 columns of dst"))
# a later row refused is part way, and names its row of the query
stmt("INSERT INTO dst (id, k) SELECT id + 300, CASE WHEN id = 2 THEN 'x' ELSE k END FROM src ORDER BY id",
     ("TYPE", None, "row 2 of the query: dst.k is integer, and the text given is not a number written in digits [part way]"))
# foreign keys check each row
stmt("CREATE TABLE fkd (id INTEGER PRIMARY KEY, s INTEGER REFERENCES src)", "= done 0")
stmt("INSERT INTO fkd SELECT id, id FROM src", "= done 3")
stmt("INSERT INTO fkd SELECT id + 10, id + 5 FROM src", ("FOREIGN_KEY", None, "foreign key `fkd_s_fkey`"))
# the drop-and-rebuild workflow, end to end: build the new table, copy the
# rows across, drop the old one and rename the new one into its place; the
# view over it runs again and the foreign key onto it is checked again
stmt("CREATE VIEW src_notes AS SELECT note FROM src", "= done 0")
stmt("CREATE TABLE src_new (id INTEGER PRIMARY KEY, k INTEGER, note TEXT, added DATE)", "= done 0")
stmt("INSERT INTO src_new (id, k, note) SELECT id, k, note FROM src", "= done 3")
stmt("DROP TABLE src", "= done 0")
stmt("ALTER TABLE src_new RENAME TO src", "= done 0")
stmt("SELECT note FROM src_notes ORDER BY note", "= 'a' | 'b' | 'c'")
stmt("INSERT INTO fkd VALUES (50, 9)", ("FOREIGN_KEY", None, "foreign key `fkd_s_fkey`"))
stmt("INSERT INTO fkd VALUES (50, 3)", "= done 1")

sec("CHECK constraints (docs/gsql_check_design.md)")
stmt("CREATE TABLE acct (id INTEGER PRIMARY KEY, bal MONEY(USD) CHECK (bal >= CAST('0' AS MONEY(USD))), "
     "opened DATE, closed DATE, CONSTRAINT closes_after CHECK (closed IS NULL OR closed >= opened))", "= done 0")
stmt("INSERT INTO acct VALUES (1, CAST('5' AS MONEY(USD)), DATE '2026-01-01', NULL)", "= done 1")
stmt("INSERT INTO acct VALUES (2, CAST('-1' AS MONEY(USD)), NULL, NULL)",
     ("CHECK", None, "check `acct_bal_check` refuses this row of `acct`: (bal >= CAST('0' AS MONEY(USD))) is false"))
# unknown passes, as the standard says: NOT NULL is how to refuse it
stmt("INSERT INTO acct VALUES (3, NULL, NULL, NULL)", "= done 1")
# a table's check reads two columns
stmt("INSERT INTO acct VALUES (4, CAST('1' AS MONEY(USD)), DATE '2026-02-01', DATE '2026-01-01')",
     ("CHECK", None, "check `closes_after` refuses this row of `acct`"))
# a check that raises refuses with gBASIC's own sentence
stmt("INSERT INTO acct VALUES (5, CAST('1' AS MONEY(EUR)), NULL, NULL)", ("TYPE", None, "acct.bal is money(USD)"))
stmt("UPDATE acct SET bal = CAST('-2' AS MONEY(USD)) WHERE id = 1", ("CHECK", None, "check `acct_bal_check`"))
stmt("UPDATE acct SET bal = CAST('2' AS MONEY(USD)) WHERE id = 1", "= done 1")
# a refusal before the write leaves a transaction usable
c("begin write")
stmt("INSERT INTO acct VALUES (6, CAST('-3' AS MONEY(USD)), NULL, NULL)", ("CHECK", None, "acct_bal_check"))
stmt("INSERT INTO acct VALUES (6, CAST('3' AS MONEY(USD)), NULL, NULL)", "= done 1")
c("rollback")
stmt("SELECT count(*) FROM acct", "= 2")
# INSERT ... SELECT checks each row
stmt("INSERT INTO acct (id, bal) SELECT id + 10, CAST('0' AS MONEY(USD)) - bal FROM acct WHERE bal IS NOT NULL",
     ("CHECK", None, "acct_bal_check"))
# added over existing rows: refused while one fails, accepted once fixed
stmt("ALTER TABLE acct ADD CONSTRAINT small_id CHECK (id < 3)", ("CHECK", "CONSTRAINT", "check `small_id`: 1 row of `acct` fails it"))
stmt("DELETE FROM acct WHERE id = 3", "= done 1")
stmt("ALTER TABLE acct ADD CONSTRAINT small_id CHECK (id < 3)", "= done 0")
stmt("INSERT INTO acct VALUES (7, NULL, NULL, NULL)", ("CHECK", None, "check `small_id`"))
stmt("ALTER TABLE acct DROP CONSTRAINT small_id", "= done 0")
stmt("INSERT INTO acct VALUES (7, NULL, NULL, NULL)", "= done 1")
stmt("ALTER TABLE acct ADD CHECK (5)", ("SCHEMA", "CHECK", "is a constant that is not a condition"))
stmt("ALTER TABLE acct ADD CHECK (nope > 1)", ("SCHEMA", "CHECK", "check `acct_check` cannot be applied to `acct`: table acct has no column `nope`"))
stmt("ALTER TABLE acct ADD CHECK (id IN (SELECT id FROM acct))", ("SCHEMA", "CHECK", "a check reads only its own row"))
stmt("CREATE TABLE ckbad (a INTEGER CHECK (b > 0))", ("SCHEMA", "CHECK", "table ckbad has no column `b`"))
stmt("SELECT * FROM ckbad", ("NOT_FOUND", "SELECT", "there is no table `ckbad`"))
# a check belongs to its table: dropping the table drops it, so the table
# renamed into its place has only its own checks (here none)
stmt("CREATE TABLE acct_new (id INTEGER PRIMARY KEY, bal MONEY(USD), opened DATE)", "= done 0")
stmt("DROP TABLE acct", "= done 0")
stmt("ALTER TABLE acct_new RENAME TO acct", "= done 0")
stmt("INSERT INTO acct VALUES (1, CAST('-9' AS MONEY(USD)), NULL)", "= done 1")
stmt("ALTER TABLE acct ADD COLUMN closed DATE", "= done 0")

sec("SERIAL: counting, per scope, gapless (docs/gsql_generated_design.md)")
stmt("CREATE TABLE gcu (id INTEGER SERIAL PRIMARY KEY, name TEXT)", "= done 0")
stmt("INSERT INTO gcu (name) VALUES ('a'), ('b')", "= done 2")
stmt("INSERT INTO gcu (name) VALUES ('c')", "= done 1")
stmt("SELECT id, name FROM gcu ORDER BY id", "= 1, 'a' | 2, 'b' | 3, 'c'")
# a rollback undoes its numbers with its rows: no gap
c("begin write")
stmt("INSERT INTO gcu (name) VALUES ('rolled back')", "= done 1")
c("rollback")
stmt("INSERT INTO gcu (name) VALUES ('d')", "= done 1")
stmt("SELECT id FROM gcu WHERE name = 'd'", "= 4")
# a refused row takes no number
stmt("INSERT INTO gcu (name) VALUES (1)", ("TYPE", None, "gcu.name is string"))
stmt("INSERT INTO gcu (name) VALUES ('e')", "= done 1")
stmt("SELECT id FROM gcu WHERE name = 'e'", "= 5")
# and inside a transaction, where a refusal before any write leaves it usable,
# the refused row's number is not spent either
c("begin write")
stmt("INSERT INTO gcu (name) VALUES (2)", ("TYPE", None, "gcu.name is string"))
stmt("INSERT INTO gcu (name) VALUES ('e2')", "= done 1")
c("commit")
stmt("SELECT id FROM gcu WHERE name = 'e2'", "= 6")
stmt("DELETE FROM gcu WHERE name = 'e2'", "= done 1")
# the newest deleted is never reissued: the counter is the highest issued
stmt("DELETE FROM gcu WHERE id = 5", "= done 1")
stmt("INSERT INTO gcu (name) VALUES ('f')", "= done 1")
stmt("SELECT id FROM gcu WHERE name = 'f'", "= 7")
# a supplied value moves the counter up, never down; a key refuses a duplicate
stmt("INSERT INTO gcu VALUES (10, 'g')", "= done 1")
stmt("INSERT INTO gcu (name) VALUES ('h')", "= done 1")
stmt("SELECT id FROM gcu WHERE name = 'h'", "= 11")
stmt("INSERT INTO gcu VALUES (2, 'below')", ("DUPLICATE", None, "the primary key already holds that value"))
stmt("INSERT INTO gcu VALUES (-4, 'negative')", "= done 1")
stmt("INSERT INTO gcu (name) VALUES ('i')", "= done 1")
stmt("SELECT id FROM gcu WHERE name = 'i'", "= 12")
# a generated column is NOT NULL, so an explicit NULL is refused
stmt("INSERT INTO gcu VALUES (NULL, 'null')", ("NOT_NULL", None, "gcu.id"))
# DEFAULT asks for the generated value
stmt("INSERT INTO gcu VALUES (DEFAULT, 'j')", "= done 1")
stmt("SELECT id FROM gcu WHERE name = 'j'", "= 13")
# INSERT ... SELECT numbers each row
stmt("INSERT INTO gcu (name) SELECT name || '2' FROM gcu WHERE id BETWEEN 1 AND 2", "= done 2")
stmt("SELECT id, name FROM gcu WHERE id > 13 ORDER BY id", "= 14, 'a2' | 15, 'b2'")
# a serial with no key: the counter still never repeats itself
stmt("CREATE TABLE gnk (n INTEGER SERIAL, x INTEGER)", "= done 0")
stmt("INSERT INTO gnk (x) VALUES (1), (2)", "= done 2")
stmt("INSERT INTO gnk VALUES (1, 3)", "= done 1")
stmt("INSERT INTO gnk (x) VALUES (4)", "= done 1")
stmt("SELECT n, x FROM gnk ORDER BY x", "= 1, 1 | 2, 2 | 1, 3 | 3, 4")
# a generated column is NOT NULL even when it is not a key
stmt("INSERT INTO gnk VALUES (NULL, 9)", ("NOT_NULL", None, "gnk.n"))
# overflow: the largest integer supplied, and then nothing comes next
stmt("INSERT INTO gnk VALUES (9223372036854775807, 5)", "= done 1")
stmt("INSERT INTO gnk (x) VALUES (6)", ("SERIAL", None, "serial `gnk.n` has reached 9223372036854775807 in this scope: there is no next number"))

sec("SERIAL OF: one counter per scope")
stmt("CREATE TABLE gtl (task INTEGER, n INTEGER SERIAL OF (task), note TEXT, PRIMARY KEY (task, n))", "= done 0")
stmt("INSERT INTO gtl (task, note) VALUES (1, 'a'), (1, 'b'), (2, 'c'), (1, 'd')", "= done 4")
stmt("SELECT task, n, note FROM gtl ORDER BY task, n", "= 1, 1, 'a' | 1, 2, 'b' | 1, 3, 'd' | 2, 1, 'c'")
# a supplied value moves only its own scope's counter
stmt("INSERT INTO gtl VALUES (2, 10, 'jump')", "= done 1")
stmt("INSERT INTO gtl (task, note) VALUES (2, 'e'), (1, 'f')", "= done 2")
stmt("SELECT task, n FROM gtl WHERE note IN ('e', 'f') ORDER BY task", "= 1, 4 | 2, 11")
# a glate insert into an old scope takes that scope's next number
stmt("INSERT INTO gtl (task, note) VALUES (1, 'glate')", "= done 1")
stmt("SELECT n FROM gtl WHERE note = 'glate'", "= 5")
# a NULL scope is one scope of its own
stmt("INSERT INTO gtl (task, note) VALUES (NULL, 'x'), (NULL, 'y')", ("NOT_NULL", None, "gtl.task"))
stmt("CREATE TABLE gns (k NUMBER, n INTEGER SERIAL OF (k))", "= done 0")
stmt("INSERT INTO gns (k) VALUES (NULL), (NULL), (3), (3.0), (3.5)", "= done 5")
stmt("SELECT k, n FROM gns", "= NULL, 1 | NULL, 2 | 3, 1 | 3.0, 2 | 3.5, 1")
# a composite scope, and one written as an expression of the row
stmt("CREATE TABLE gtx (day DATE, branch TEXT, no INTEGER SERIAL OF (day, lower(branch)))", "= done 0")
stmt("INSERT INTO gtx (day, branch) VALUES (DATE '2026-10-01', 'North'), (DATE '2026-10-01', 'north'), "
     "(DATE '2026-10-01', 'South'), (DATE '2026-10-02', 'North')", "= done 4")
stmt("SELECT branch, no FROM gtx ORDER BY day, branch, no", "= 'North', 1 | 'South', 1 | 'north', 2 | 'North', 1")
# an UPDATE that moves a row to another scope is refused, unless it sets the
# serial too; a move within one scope is no move
stmt("UPDATE gtl SET task = 3 WHERE note = 'd'",
     ("SERIAL", None, "this UPDATE moves a row of `gtl` to another scope of serial `n`, whose number belongs to the old one: set `n` in the same UPDATE to number it in the new scope"))
stmt("UPDATE gtl SET task = 3, n = 7 WHERE note = 'd'", "= done 1")
stmt("INSERT INTO gtl (task, note) VALUES (3, 'after')", "= done 1")
stmt("SELECT n FROM gtl WHERE note = 'after'", "= 8")
stmt("UPDATE gtx SET branch = 'NORTH' WHERE branch = 'North' AND day = DATE '2026-10-01'", "= done 1")
# setting the serial alone moves its scope's counter up
stmt("UPDATE gtl SET n = 20 WHERE note = 'glate'", "= done 1")
stmt("INSERT INTO gtl (task, note) VALUES (1, 'after20')", "= done 1")
stmt("SELECT n FROM gtl WHERE note = 'after20'", "= 21")
# the counters belong to the table: renamed with it, gone with it
stmt("ALTER TABLE gtl RENAME TO gtl2", "= done 0")
stmt("INSERT INTO gtl2 (task, note) VALUES (1, 'renamed')", "= done 1")
stmt("SELECT n FROM gtl2 WHERE note = 'renamed'", "= 22")
stmt("DROP TABLE gtl2", "= done 0")
stmt("CREATE TABLE gtl2 (task INTEGER, n INTEGER SERIAL OF (task), note TEXT)", "= done 0")
stmt("INSERT INTO gtl2 (task, note) VALUES (1, 'again')", "= done 1")
stmt("SELECT n FROM gtl2", "= 1")
# a scope with no order is refused with its row
stmt("CREATE TABLE grs (r RECORD, n INTEGER SERIAL OF (r))", "= done 0")
stmt("INSERT INTO grs (r) VALUES (NULL)", "= done 1")
stmt("INSERT INTO grs (r) VALUES (:p)", ("EXPRESSION", None, "serial `grs.n`'s scope: a record has no order G-SQL can key, so it cannot be a scope"),
     params=("p = {vendor: 'acme'}",))
stmt("SELECT count(*) FROM grs", "= 1")

sec("CAST between dates, times and datetimes (docs/datetime_conversion_design.md)")
stmt("SELECT CAST(CAST('2026-01-05 10:30:45' AS DATETIME) AS DATE), CAST(CAST('2026-01-05 10:30:45' AS DATETIME) AS TIME)",
     "= datetime 2026-01-05, datetime 10:30:45")
stmt("SELECT CAST(DATE '2026-01-05' AS DATETIME)", "= datetime 2026-01-05 00:00:00")
# text cast to DATE takes its date, as PostgreSQL's does and a DATE column holds
stmt("SELECT CAST('2026-01-05 10:30' AS DATE) = DATE '2026-01-05'", "= TRUE")
stmt("SELECT CAST(CAST('2026-01-05 23:59:59' AS DATETIME) AS DATE) = DATE '2026-01-05'", "= TRUE")
stmt("SELECT CAST(CAST('10:30' AS TIME) AS DATE)", ("EXPRESSION", "CAST", "a time of day has no date to take", 1))
stmt("SELECT CAST(DATE '2026-01-05' AS TIME)", ("EXPRESSION", "CAST", "a date has no time of day to take"))
stmt("SELECT CAST(CAST('10:30' AS TIME) AS DATETIME)", ("EXPRESSION", "CAST", "a time of day has no date to take", 1))
# each cast fits its column
stmt("CREATE TABLE when3 (d DATE, t TIME, dt DATETIME)", "= done 0")
stmt("INSERT INTO when3 VALUES (CAST(CAST('2026-01-05 10:30:45' AS DATETIME) AS DATE), "
     "CAST(CAST('2026-01-05 10:30:45' AS DATETIME) AS TIME), CAST(DATE '2026-01-05' AS DATETIME))", "= done 1")
stmt("INSERT INTO when3 (d) VALUES (CAST('2026-01-05 10:30' AS DATE))", "= done 1")
# a cast time is the same key as the time written: no date left behind in it
stmt("CREATE TABLE tkey (t TIME UNIQUE)", "= done 0")
stmt("INSERT INTO tkey VALUES (CAST(CAST('2026-01-05 10:30:45' AS DATETIME) AS TIME))", "= done 1")
stmt("INSERT INTO tkey VALUES (CAST('10:30:45' AS TIME))", ("DUPLICATE", None, "unique index tkey_t_key already holds that value"))
# gsql_design.md §10E's own example, written: a serial per day of a datetime
stmt("CREATE TABLE req (at DATETIME, n INTEGER SERIAL OF (CAST(at AS DATE)))", "= done 0")
stmt("INSERT INTO req (at) VALUES (CAST('2026-01-05 09:00' AS DATETIME)), (CAST('2026-01-05 17:30' AS DATETIME)), "
     "(CAST('2026-01-06 08:00' AS DATETIME))", "= done 3")
stmt("SELECT n FROM req ORDER BY at", "= 1 | 2 | 1")
# a later time on the same day is no move; the next day is
stmt("UPDATE req SET at = CAST('2026-01-05 23:00' AS DATETIME) WHERE at = CAST('2026-01-05 09:00' AS DATETIME)", "= done 1")
stmt("UPDATE req SET at = CAST('2026-01-07 09:00' AS DATETIME) WHERE at = CAST('2026-01-06 08:00' AS DATETIME)",
     ("SERIAL", None, "moves a row of `req` to another scope of serial `n`"))

sec("SERIAL and RANDOM: what a declaration may say")
stmt("CREATE TABLE gbad (x TEXT SERIAL)", ("SCHEMA", None, "gbad.x: SERIAL counts, so it belongs on an INTEGER column"))
stmt("CREATE TABLE gbad (x INTEGER SERIAL OF (x))", ("SCHEMA", "SERIAL", "serial `gbad.x`'s scope reads `x` itself"))
stmt("CREATE TABLE gbad (x INTEGER SERIAL OF (y))", ("SCHEMA", "SERIAL", "serial `gbad.x`'s scope cannot be applied: table gbad has no column `y`"))
stmt("CREATE TABLE gbad (a INTEGER, x INTEGER SERIAL OF ((SELECT 1)))", ("SCHEMA", "SERIAL", "a scope reads only its own row"))
stmt("CREATE TABLE gbad (a INTEGER, x INTEGER SERIAL OF (count(a)))", ("SCHEMA", "SERIAL", "a scope reads only its own row"))
stmt("CREATE TABLE gbad (a INTEGER, x INTEGER SERIAL OF (gcu.id))", ("SCHEMA", "SERIAL", "cannot be applied"))
stmt("SELECT * FROM gbad", ("NOT_FOUND", "SELECT", "there is no table `gbad`"))
stmt("CREATE TABLE gbad (x INTEGER SERIAL DEFAULT 3)", ("SCHEMA", None, "a generated column takes no DEFAULT"))
stmt("CREATE TABLE gbad (x INTEGER SERIAL RANDOM)", ("SYNTAX", "RANDOM", "a column has at most one generator"))
stmt("CREATE TABLE gbad (x NUMBER RANDOM)", ("SCHEMA", None, "RANDOM draws text or an integer, not number"))
stmt("CREATE TABLE gbad (x INTEGER RANDOM(3, 'ab'))", ("SCHEMA", None, "an integer is drawn in digits"))
stmt("CREATE TABLE gbad (x INTEGER RANDOM(19))", ("SCHEMA", None, "at most 18 digits"))
stmt("CREATE TABLE gbad (x TEXT RANDOM(0))", ("SYNTAX", "0", "RANDOM draws 1 to 1024 characters"))
stmt("CREATE TABLE gbad (x TEXT RANDOM(1025))", ("SYNTAX", "1025", "RANDOM draws 1 to 1024 characters"))
stmt("CREATE TABLE gbad (x TEXT RANDOM(2.5))", ("SYNTAX", "2.5", "a whole number of characters or digits"))
stmt("CREATE TABLE gbad (x TEXT RANDOM(4, 'aba'))", ("SCHEMA", None, "the alphabet holds `a` twice"))
stmt("CREATE TABLE gbad (x TEXT RANDOM(4, 'a'))", ("SCHEMA", None, "an alphabet of 1 character draws nothing random"))
stmt("CREATE TABLE gbad (x TEXT RANDOM(4, ''))", ("SCHEMA", None, "an alphabet of 0 characters"))
stmt("CREATE TABLE gbad (x TEXT RANDOM(4, 7))", ("SYNTAX", "7", "the alphabet, as a string"))
# a generated column added: refused over rows, accepted on an empty table
stmt("ALTER TABLE gcu ADD COLUMN seq INTEGER SERIAL", ("SCHEMA", None, "gcu already has rows, so a generated column seq cannot be added"))
stmt("CREATE TABLE glate (a INTEGER)", "= done 0")
stmt("ALTER TABLE glate ADD COLUMN s INTEGER SERIAL OF (a)", "= done 0")
stmt("ALTER TABLE glate ADD COLUMN s2 INTEGER SERIAL OF (s2)", ("SCHEMA", "SERIAL", "serial `glate.s2`'s scope reads `s2` itself"))
stmt("ALTER TABLE glate ADD COLUMN k TEXT RANDOM(5)", "= done 0")
stmt("INSERT INTO glate (a) VALUES (7), (7)", "= done 2")
stmt("SELECT a, s, length(k) FROM glate", "= 7, 1, 5 | 7, 2, 5")

sec("RANDOM: drawn once, stored, and drawn again only for a key")
stmt("CREATE TABLE gus (id TEXT RANDOM PRIMARY KEY, pin TEXT RANDOM(6, '0123456789'), sh INTEGER RANDOM, "
     "code INTEGER RANDOM(8), one INTEGER RANDOM(1), name TEXT)", "= done 0")
stmt("INSERT INTO gus (name) VALUES ('a'), ('b'), ('c')", "= done 3")
stmt("SELECT count(*) FROM gus WHERE length(id) = 22 AND length(pin) = 6 AND sh >= 0 "
     "AND code >= 10000000 AND code <= 99999999 AND one >= 1 AND one <= 9", "= 3")
stmt("SELECT count(DISTINCT id) FROM gus", "= 3")
# a value supplied is kept, which is how imported keys stay
stmt("INSERT INTO gus (id, pin, name) VALUES ('mine', '0000', 'd')", "= done 1")
stmt("SELECT pin FROM gus WHERE id = 'mine'", "= '0000'")
# an UPDATE never draws
stmt("UPDATE gus SET name = 'dd' WHERE id = 'mine'", "= done 1")
stmt("SELECT pin FROM gus WHERE id = 'mine'", "= '0000'")
# a small space on a key: drawn again, and refused after its draws, naming it
stmt("CREATE TABLE gtiny (x TEXT RANDOM(1, 'ab') PRIMARY KEY)", "= done 0")
stmt("INSERT INTO gtiny VALUES (DEFAULT)", "= done 1")
stmt("INSERT INTO gtiny VALUES (DEFAULT)", "= done 1")
stmt("SELECT count(*) FROM gtiny WHERE x IN ('a', 'b')", "= 2")
stmt("INSERT INTO gtiny VALUES (DEFAULT)",
     ("DUPLICATE", None, "gtiny: the primary key already holds that value: random `x` can hold 2 values, and 32 draws in a row were all taken"))
# a duplicate in a key with no random column is refused at once, as before
stmt("CREATE TABLE gem (id TEXT RANDOM PRIMARY KEY, email TEXT UNIQUE)", "= done 0")
stmt("INSERT INTO gem (email) VALUES ('a@x')", "= done 1")
stmt("INSERT INTO gem (email) VALUES ('a@x')", ("DUPLICATE", None, "gem: unique index gem_email_key already holds that value"))
# the alphabet may be any text; a non-ASCII character is one character
stmt("CREATE TABLE gk (g TEXT RANDOM(3, 'αβ'))", "= done 0")
stmt("INSERT INTO gk VALUES (DEFAULT)", "= done 1")
stmt("SELECT length(g) FROM gk", "= 3")

sec("BACKUP TO: what it refuses before it copies anything (docs/gsql_backup_design.md)")
stmt("BACKUP TO 'relative/copy'", ("EXPRESSION", "'relative/copy'", "the directory must be absolute"))
stmt("BACKUP TO 5", ("EXPRESSION", "5", "BACKUP TO takes the copy's directory as text"))
stmt("BACKUP TO name", ("SYNTAX", "name", "BACKUP TO cannot read a column"))

sec("window functions (docs/gsql_window_design.md)")
# acct a's rows sorted by d are 1, 2, 3, 6, with 2 and 3 peers (d = 2), read in id order
stmt("CREATE TABLE wn (id INTEGER PRIMARY KEY, acct TEXT, d INTEGER, amt INTEGER, m MONEY(USD))", "= done 0")
stmt("INSERT INTO wn VALUES (1, 'a', 1, 10, 1.00), (2, 'a', 2, 20, 2.50), (3, 'a', 2, 5, 0.25), (4, 'b', 1, 7, NULL), "
     "(5, 'b', 3, NULL, 3.00), (6, 'a', 4, 1, 1.00), (7, NULL, 5, 3, 0.50)", "= done 7")
stmt("SELECT id, row_number() OVER (PARTITION BY acct ORDER BY d), rank() OVER (PARTITION BY acct ORDER BY d), "
     "dense_rank() OVER (PARTITION BY acct ORDER BY d) FROM wn ORDER BY id",
     "= 1, 1, 1, 1 | 2, 2, 2, 2 | 3, 3, 2, 2 | 4, 1, 1, 1 | 5, 2, 2, 2 | 6, 4, 4, 3 | 7, 1, 1, 1")
# the default frame ends at the current row's last peer, so peers share a running total; NULLs are skipped
stmt("SELECT id, sum(amt) OVER (PARTITION BY acct ORDER BY d), count(amt) OVER (PARTITION BY acct ORDER BY d) "
     "FROM wn ORDER BY id", "= 1, 10, 1 | 2, 35, 3 | 3, 35, 3 | 4, 7, 1 | 5, 7, 1 | 6, 36, 4 | 7, 3, 1")
# ROWS counts rows, so peers do not share
stmt("SELECT id, sum(amt) OVER (PARTITION BY acct ORDER BY d ROWS UNBOUNDED PRECEDING) FROM wn WHERE acct = 'a' "
     "ORDER BY id", "= 1, 10 | 2, 30 | 3, 35 | 6, 36")
stmt("SELECT id, sum(amt) OVER (ORDER BY id ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING) FROM wn ORDER BY id",
     "= 1, 30 | 2, 35 | 3, 32 | 4, 12 | 5, 8 | 6, 4 | 7, 4")
# a frame that ends at the partition's end is folded from the end
stmt("SELECT id, sum(amt) OVER (ORDER BY id ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING) FROM wn ORDER BY id",
     "= 1, 46 | 2, 36 | 3, 16 | 4, 11 | 5, 4 | 6, 4 | 7, 3")
stmt("SELECT id, sum(amt) OVER (PARTITION BY acct ORDER BY d RANGE BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING) "
     "FROM wn ORDER BY id", "= 1, 36 | 2, 26 | 3, 26 | 4, 7 | 5, NULL | 6, 1 | 7, 3")
# a frame wholly before the row: empty for the first, where sum is NULL and count 0
stmt("SELECT id, sum(amt) OVER (ORDER BY id ROWS BETWEEN 2 PRECEDING AND 1 PRECEDING), "
     "count(*) OVER (ORDER BY id ROWS BETWEEN 2 PRECEDING AND 1 PRECEDING) FROM wn ORDER BY id",
     "= 1, NULL, 0 | 2, 10, 1 | 3, 30, 2 | 4, 25, 2 | 5, 12, 2 | 6, 7, 2 | 7, 1, 2")
stmt("SELECT id, sum(amt) OVER (ORDER BY id ROWS BETWEEN :n PRECEDING AND CURRENT ROW) FROM wn ORDER BY id",
     "= 1, 10 | 2, 30 | 3, 25 | 4, 12 | 5, 7 | 6, 1 | 7, 4", params=("n = 1",))
# with no ORDER BY the frame is the whole partition; money stays money; NULL is a partition of its own
stmt("SELECT id, count(*) OVER (PARTITION BY acct), sum(m) OVER (PARTITION BY acct), max(d) OVER () FROM wn ORDER BY id",
     "= 1, 4, money 4.75, 5 | 2, 4, money 4.75, 5 | 3, 4, money 4.75, 5 | 4, 2, money 3.00, 5 | "
     "5, 2, money 3.00, 5 | 6, 4, money 4.75, 5 | 7, 1, money 0.50, 5")
stmt("SELECT id, lag(amt) OVER (ORDER BY id), lead(amt, 2) OVER (ORDER BY id), lag(id, 0) OVER (ORDER BY id), "
     "lead(id, 1, -1) OVER (ORDER BY id) FROM wn ORDER BY id",
     "= 1, NULL, 5, 1, 2 | 2, 10, 7, 2, 3 | 3, 20, NULL, 3, 4 | 4, 5, 1, 4, 5 | 5, 7, 3, 5, 6 | "
     "6, NULL, NULL, 6, 7 | 7, 1, NULL, 7, -1")
# last_value over the default frame is the current row's last peer, not the partition's last row
stmt("SELECT id, first_value(id) OVER (PARTITION BY acct ORDER BY d), last_value(id) OVER (PARTITION BY acct ORDER BY d), "
     "last_value(id) OVER (PARTITION BY acct ORDER BY d ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING), "
     "nth_value(id, 2) OVER (PARTITION BY acct ORDER BY d) FROM wn ORDER BY id",
     "= 1, 1, 1, 6, NULL | 2, 1, 3, 6, 2 | 3, 1, 3, 6, 2 | 4, 4, 4, 5, NULL | 5, 4, 5, 5, 5 | 6, 1, 6, 6, 2 | "
     "7, 7, 7, 7, NULL")
# rows 1..5 by d are 1, 4 | 2, 3 | 5: ranks 1, 1, 3, 3, 5 of 5
stmt("SELECT id, ntile(2) OVER (ORDER BY d), percent_rank() OVER (ORDER BY d), cume_dist() OVER (ORDER BY d) "
     "FROM wn WHERE id <= 5 ORDER BY id",
     "= 1, 1, 0.0, 0.4 | 2, 1, 0.5, 0.8 | 3, 2, 0.5, 0.8 | 4, 1, 0.0, 0.4 | 5, 2, 1.0, 1.0")
stmt("SELECT id, ntile(3) OVER (ORDER BY id) FROM wn ORDER BY id", "= 1, 1 | 2, 1 | 3, 1 | 4, 2 | 5, 2 | 6, 3 | 7, 3")
# a grouped query's windows see its groups
stmt("SELECT acct, sum(amt), sum(sum(amt)) OVER (ORDER BY acct), rank() OVER (ORDER BY count(*) DESC) FROM wn "
     "GROUP BY acct ORDER BY acct", "= 'a', 36, 36, 1 | 'b', 7, 43, 2 | NULL, 3, 46, 3")
# the latest row per key, filtered outside the window
stmt("SELECT id, acct FROM (SELECT id, acct, row_number() OVER (PARTITION BY acct ORDER BY d DESC) AS rn FROM wn) AS t "
     "WHERE rn = 1 ORDER BY id", "= 5, 'b' | 6, 'a' | 7, NULL")
# with no ORDER BY of its own the query answers in the order it reads
stmt("SELECT id, row_number() OVER (ORDER BY id DESC) FROM wn WHERE id < 4", "= 1, 3 | 2, 2 | 3, 1")
# the statement's ORDER BY does not reorder a window's partition (measured: reading backward to save
# a sort made last_value answer each partition's first row)
stmt("SELECT id, last_value(id) OVER (PARTITION BY acct) FROM wn ORDER BY id DESC",
     "= 7, 7 | 6, 6 | 5, 5 | 4, 5 | 3, 6 | 2, 6 | 1, 6")
stmt("SELECT id FROM wn WHERE acct = 'a' ORDER BY row_number() OVER (ORDER BY amt DESC)", "= 2 | 1 | 3 | 6")
stmt("SELECT DISTINCT acct, count(*) OVER (PARTITION BY acct) FROM wn ORDER BY 1", "= 'a', 4 | 'b', 2 | NULL, 1")
stmt("SELECT row_number() OVER (), count(*) OVER ()", "= 1, 1")
stmt("SELECT row_number() OVER () FROM wn WHERE id > 99", "= (none)")
stmt("SELECT id, row_number() OVER (ORDER BY id) AS rn FROM wn ORDER BY rn DESC LIMIT 2 OFFSET 1", "= 6, 6 | 5, 5")
# a view's text is the canonical print, so a window survives it
stmt("CREATE VIEW wv AS SELECT id, rank() OVER (ORDER BY d) AS r FROM wn", "= done 0")
stmt("SELECT * FROM wv ORDER BY id", "= 1, 1 | 2, 3 | 3, 3 | 4, 1 | 5, 5 | 6, 6 | 7, 7")
stmt("SELECT row_number() OVER (), rank() OVER (ORDER BY d) FROM wn", "% row_number:integer, rank:integer")
stmt("SELECT id FROM wn WHERE row_number() OVER () = 1",
     ("SYNTAX", "row_number", "WHERE cannot use a window function: windows are computed after WHERE"))
stmt("SELECT acct FROM wn GROUP BY acct HAVING rank() OVER (ORDER BY acct) = 1",
     ("SYNTAX", "rank", "HAVING cannot use a window function"))
stmt("SELECT sum(row_number() OVER ()) FROM wn", ("SYNTAX", "row_number", "an aggregate cannot use a window function"))
stmt("SELECT sum(row_number() OVER ()) OVER () FROM wn",
     ("SYNTAX", "row_number", "a window function cannot use a window function"))
stmt("SELECT rank() OVER (ORDER BY d ROWS UNBOUNDED PRECEDING) FROM wn",
     ("SYNTAX", "ROWS", "rank does not read a frame: a frame reaches only the aggregates and first_value"))
stmt("SELECT sum(amt) OVER (ORDER BY d RANGE 1 PRECEDING) FROM wn",
     ("UNSUPPORTED", "1", "RANGE takes only UNBOUNDED and CURRENT ROW bounds"))
stmt("SELECT sum(amt) OVER (ORDER BY d ROWS BETWEEN CURRENT ROW AND 1 PRECEDING) FROM wn",
     ("SYNTAX", "ROWS", "a frame cannot start after it ends"))
stmt("SELECT sum(amt) OVER (ORDER BY d ROWS BETWEEN UNBOUNDED FOLLOWING AND UNBOUNDED FOLLOWING) FROM wn",
     ("SYNTAX", "ROWS", "a frame cannot start at UNBOUNDED FOLLOWING"))
stmt("SELECT id, rank() OVER (ORDER BY 2) FROM wn", ("SYNTAX", "2", "a number here sorts by that constant"))
stmt("SELECT lower(acct) OVER () FROM wn", ("NOT_FOUND", "lower", "`lower` is not a window function"))
stmt("SELECT count(DISTINCT acct) OVER () FROM wn", ("UNSUPPORTED", "count", "DISTINCT in a window aggregate"))
stmt("SELECT ntile() OVER () FROM wn", ("SYNTAX", "ntile", "ntile takes 1 argument, and was given 0"))
stmt("SELECT acct, sum(amt) OVER (ORDER BY acct) FROM wn GROUP BY acct",
     ("SYNTAX", "amt", "the select list reads column `amt`, which is neither grouped by"))
stmt("SELECT lag(amt, -1) OVER (ORDER BY id) FROM wn", ("EXPRESSION", "-", "lag needs a whole number that is not negative"))
stmt("SELECT ntile(0) OVER (ORDER BY id) FROM wn", ("EXPRESSION", "0", "ntile needs a whole number of one or more"))
stmt("SELECT sum(amt) OVER (ORDER BY id ROWS BETWEEN NULL PRECEDING AND CURRENT ROW) FROM wn",
     ("EXPRESSION", "NULL", "and this one is NULL"))
stmt("SELECT rank() OVER (ORDER BY CASE WHEN id < 3 THEN id ELSE 'x' END) FROM wn",
     ("EXPRESSION", "CASE", "< compares text with a number"))

sec("WITH RECURSIVE (docs/gsql_recursive_design.md)")
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM c WHERE i < 5) SELECT i FROM c", "= 1 | 2 | 3 | 4 | 5")
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM c WHERE i < 100) SELECT sum(i), count(*) FROM c",
     "= 5050, 100")
# an ordinary query in a RECURSIVE list is an ordinary WITH query
stmt("WITH RECURSIVE two (x) AS (SELECT 2), c (i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM c, two WHERE i < two.x) "
     "SELECT i FROM c", "= 1 | 2")
stmt("CREATE TABLE staff (id INTEGER PRIMARY KEY, boss INTEGER, name TEXT)", "= done 0")
stmt("INSERT INTO staff VALUES (1, NULL, 'ada'), (2, 1, 'bo'), (3, 1, 'cy'), (4, 2, 'di'), (5, 4, 'ed'), (6, 3, 'fy')",
     "= done 6")
stmt("WITH RECURSIVE t (id, name, depth) AS (SELECT id, name, 0 FROM staff WHERE boss IS NULL UNION ALL "
     "SELECT e.id, e.name, t.depth + 1 FROM staff e JOIN t ON e.boss = t.id) SELECT name, depth FROM t ORDER BY depth, name",
     "= 'ada', 0 | 'bo', 1 | 'cy', 1 | 'di', 2 | 'fy', 2 | 'ed', 3")
# the query read second in FROM, and read by name with an alias
stmt("WITH RECURSIVE under (id) AS (SELECT 2 UNION SELECT e.id FROM under AS u JOIN staff e ON e.boss = u.id) "
     "SELECT id FROM under ORDER BY id", "= 2 | 4 | 5")
# a bill of materials: quantities multiplied down, then totalled by the query that reads it
stmt("CREATE TABLE bom (id INTEGER PRIMARY KEY, assembly TEXT, component TEXT, qty INTEGER)", "= done 0")
stmt("INSERT INTO bom VALUES (1, 'arm', 'joint', 2), (2, 'arm', 'motor', 1), (3, 'joint', 'bolt', 4), "
     "(4, 'joint', 'motor', 1), (5, 'motor', 'bolt', 2)", "= done 5")
stmt("WITH RECURSIVE parts (part, qty) AS (SELECT component, qty FROM bom WHERE assembly = 'arm' UNION ALL "
     "SELECT b.component, p.qty * b.qty FROM parts p JOIN bom b ON b.assembly = p.part) "
     "SELECT part, sum(qty) FROM parts GROUP BY part ORDER BY part", "= 'bolt', 14 | 'joint', 2 | 'motor', 3")
# 1 -> 2 -> 3 -> 1 is a cycle, and 3 -> 4 leaves it
stmt("CREATE TABLE edge (id INTEGER PRIMARY KEY, a INTEGER, b INTEGER)", "= done 0")
stmt("INSERT INTO edge VALUES (1, 1, 2), (2, 2, 3), (3, 3, 1), (4, 3, 4)", "= done 4")
# UNION drops a row it has kept, so a cycle over rows that repeat ends
stmt("WITH RECURSIVE r (n) AS (SELECT 1 UNION SELECT edge.b FROM r JOIN edge ON edge.a = r.n) SELECT n FROM r ORDER BY n",
     "= 1 | 2 | 3 | 4")
# the anchor's own repeats are dropped too, and the rows come round by round
stmt("WITH RECURSIVE r (n) AS (SELECT 4 UNION SELECT 4 UNION SELECT edge.b FROM r JOIN edge ON edge.a = r.n) SELECT n FROM r",
     "= 4")
# CYCLE: the looping row is answered, marked, and goes no further
stmt("WITH RECURSIVE r (n, d) AS (SELECT 1, 0 UNION ALL SELECT edge.b, r.d + 1 FROM r JOIN edge ON edge.a = r.n) "
     "CYCLE n SET looped USING route SELECT n, d, looped, route FROM r ORDER BY d, n",
     "= 1, 0, FALSE, [1] | 2, 1, FALSE, [1,2] | 3, 2, FALSE, [1,2,3] | 1, 3, TRUE, [1,2,3,1] | "
     "4, 3, FALSE, [1,2,3,4]")
stmt("WITH RECURSIVE r (n, k) AS (SELECT 1, 'x' UNION ALL SELECT edge.b, r.k FROM r JOIN edge ON edge.a = r.n) "
     "CYCLE n, k SET c USING p SELECT n, c, p FROM r WHERE c",
     "= 1, TRUE, [[1,\"x\"],[2,\"x\"],[3,\"x\"],[1,\"x\"]]")
stmt("WITH RECURSIVE r (n) AS (SELECT 1 UNION ALL SELECT edge.b FROM r JOIN edge ON edge.a = r.n) CYCLE n SET looped "
     "USING route SELECT * FROM r", "% n:any, looped:boolean, route:any")
# the round limit, lowered here: rounds past it may run, but not add rows
c("rounds 5")
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM c WHERE i < 6) SELECT count(*) FROM c", "= 6")
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM c WHERE i < 7) SELECT count(*) FROM c",
     ("EXPRESSION", "c (i)", "WITH RECURSIVE `c` was still adding rows after 5 rounds: if the data has a cycle"))
stmt("WITH RECURSIVE r (n, d) AS (SELECT 1, 0 UNION ALL SELECT edge.b, r.d + 1 FROM r JOIN edge ON edge.a = r.n) "
     "SELECT count(*) FROM r", ("EXPRESSION", "r (n, d)", "mark it with CYCLE"))
c("rounds 100000")
# rows that double each round: refused by the bytes they hold, lowered here to 1 MB
stmt("CREATE TABLE g2 (id INTEGER PRIMARY KEY, a INTEGER, b INTEGER)", "= done 0")
stmt("INSERT INTO g2 VALUES (1, 1, 2), (2, 2, 1), (3, 1, 1)", "= done 3")
c("rowbytes 1048576")
stmt("WITH RECURSIVE r (n) AS (SELECT 1 UNION ALL SELECT g2.b FROM r JOIN g2 ON g2.a = r.n) SELECT count(*) FROM r",
     ("EXPRESSION", "r (n)", "WITH RECURSIVE `r` grew past 1 MB of rows in"))
c("rowbytes 1073741824")
stmt("WITH RECURSIVE c (i) AS (SELECT 1 INTERSECT SELECT i FROM c) SELECT i FROM c",
     ("SYNTAX", "c (i)", "must be an anchor, then UNION or UNION ALL, then a step"))
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM c ORDER BY 1 LIMIT 3) SELECT i FROM c",
     ("SYNTAX", "c (i)", "ORDER BY, LIMIT and OFFSET cannot stand inside WITH RECURSIVE `c`"))
stmt("WITH RECURSIVE c (i) AS (SELECT i FROM c UNION ALL SELECT i + 1 FROM c) SELECT i FROM c",
     ("SYNTAX", "SELECT", "the anchor of WITH RECURSIVE `c`, everything before its last UNION, cannot read `c`"))
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT a.i + b.i FROM c a, c b) SELECT i FROM c",
     ("SYNTAX", "SELECT", "must be one SELECT reading `c` once", 2))
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM staff WHERE id IN (SELECT i FROM c)) SELECT i FROM c",
     ("SYNTAX", "SELECT", "not inside a subquery or derived table", 2))
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT staff.id FROM staff LEFT JOIN c ON c.i = staff.boss) SELECT i FROM c",
     ("SYNTAX", "c ON", "cannot read it on the null-filled side of a LEFT JOIN"))
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT count(*) FROM c) SELECT i FROM c",
     ("SYNTAX", "SELECT", "cannot aggregate or use a window function", 2))
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT i, i FROM c) SELECT i FROM c",
     ("SYNTAX", "SELECT", "the anchor of `c` has 1 and its step 2", 2))
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM c) CYCLE j SET m USING r SELECT i FROM c",
     ("NOT_FOUND", "CYCLE", "CYCLE watches `j`, which is not a column of `c`"))
stmt("WITH RECURSIVE c (i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM c) CYCLE i SET i USING r SELECT i FROM c",
     ("SYNTAX", "CYCLE", "`i` is already a column of `c`"))
stmt("EXPLAIN WITH RECURSIVE r (n) AS (SELECT 1 UNION SELECT edge.b FROM r JOIN edge ON edge.a = r.n) SELECT n FROM r",
     "= 'CTE Scan on r' | 'CTE r' | '  Recursive Union' | '    Result' | '    Nested Loop' | '      CTE Scan on r' | "
     "'      Hash Scan on edge using (a = r.n)'")
stmt("WITH RECURSIVE c (i) AS (SELECT 1) CYCLE i SET m USING r SELECT i FROM c",
     ("SYNTAX", "CYCLE", "`c` does not read itself"))

sec("ROLLUP, CUBE, GROUPING SETS and GROUPING (docs/gsql_rollup_design.md)")
stmt("CREATE TABLE sale (id INTEGER PRIMARY KEY, region TEXT, product TEXT, amount MONEY(USD))", "= done 0")
stmt("INSERT INTO sale VALUES (1, 'east', 'apple', 1.00), (2, 'east', 'apple', 2.00), (3, 'east', 'pear', 3.00), "
     "(4, 'west', 'apple', 4.00), (5, 'west', NULL, 5.00), (6, NULL, 'pear', 6.00)", "= done 6")
# with no ORDER BY the groups come as a report reads them: each region's rows, its subtotal, the grand total last;
# a NULL in the data (west's NULL product) comes before the subtotal, and GROUPING tells them apart
stmt("SELECT region, product, sum(amount), GROUPING(region, product) FROM sale GROUP BY ROLLUP(region, product)",
     "= 'east', 'apple', money 3.00, 0 | 'east', 'pear', money 3.00, 0 | 'east', NULL, money 6.00, 1 | "
     "'west', 'apple', money 4.00, 0 | 'west', NULL, money 5.00, 0 | 'west', NULL, money 9.00, 1 | "
     "NULL, 'pear', money 6.00, 0 | NULL, NULL, money 6.00, 1 | NULL, NULL, money 21.00, 3")
stmt("SELECT region, product, count(*), GROUPING(region, product) AS g FROM sale GROUP BY CUBE(region, product) "
     "ORDER BY g, region, product",
     "= 'east', 'apple', 2, 0 | 'east', 'pear', 1, 0 | 'west', 'apple', 1, 0 | 'west', NULL, 1, 0 | NULL, 'pear', 1, 0 | "
     "'east', NULL, 3, 1 | 'west', NULL, 2, 1 | NULL, NULL, 1, 1 | NULL, 'apple', 3, 2 | NULL, 'pear', 2, 2 | "
     "NULL, NULL, 1, 2 | NULL, NULL, 6, 3")
stmt("SELECT region, product, count(*) FROM sale GROUP BY GROUPING SETS ((region), (product), ())",
     "= 'east', NULL, 3 | 'west', NULL, 2 | NULL, NULL, 1 | NULL, 'apple', 3 | NULL, 'pear', 2 | NULL, NULL, 1 | "
     "NULL, NULL, 6")
# a plain expression beside ROLLUP is in every set
stmt("SELECT region, product, count(*) FROM sale WHERE region IS NOT NULL GROUP BY region, ROLLUP(product)",
     "= 'east', 'apple', 2 | 'east', 'pear', 1 | 'east', NULL, 3 | 'west', 'apple', 1 | 'west', NULL, 1 | "
     "'west', NULL, 2")
stmt("SELECT region, sum(amount) FROM sale GROUP BY ROLLUP(region) HAVING GROUPING(region) = 1", "= NULL, money 21.00")
stmt("SELECT region, sum(amount) FROM sale GROUP BY ROLLUP(region) HAVING region IS NULL",
     "= NULL, money 6.00 | NULL, money 21.00")
# an expression over a left-out one reads it as NULL; an aggregate's argument reads the rows
stmt("SELECT upper(region), count(*), max(region) FROM sale GROUP BY ROLLUP(region)",
     "= 'EAST', 3, 'east' | 'WEST', 2, 'west' | NULL, 1, NULL | NULL, 6, 'west'")
stmt("SELECT upper(region), count(*) FROM sale GROUP BY ROLLUP(upper(region))",
     "= 'EAST', 3 | 'WEST', 2 | NULL, 1 | NULL, 6")
# a set named twice answers twice, as SQL says
stmt("SELECT region, count(*) FROM sale GROUP BY region, ROLLUP(region)",
     "= 'east', 3 | 'east', 3 | 'west', 2 | 'west', 2 | NULL, 1 | NULL, 1")
# the empty set answers even over no rows
stmt("SELECT count(*), sum(amount) FROM sale WHERE id > 99 GROUP BY ROLLUP(region)", "= 0, NULL")
stmt("SELECT region, GROUPING(region) FROM sale GROUP BY region ORDER BY 1", "= 'east', 0 | 'west', 0 | NULL, 0")
stmt("SELECT region, sum(amount), rank() OVER (ORDER BY sum(amount) DESC) FROM sale GROUP BY ROLLUP(region) ORDER BY 3",
     "= NULL, money 21.00, 1 | 'west', money 9.00, 2 | 'east', money 6.00, 3 | NULL, money 6.00, 3")
stmt("CREATE VIEW sale_totals AS SELECT region, product, sum(amount) AS total FROM sale GROUP BY ROLLUP(region, product)",
     "= done 0")
stmt("SELECT count(*), sum(total) FROM sale_totals", "= 9, money 63.00")
stmt("SELECT region, GROUPING(region) FROM sale GROUP BY region", "% region:string, grouping:integer")
stmt("EXPLAIN SELECT region, count(*) FROM sale GROUP BY region, ROLLUP(region, product)",
     "= 'Scan on sale' | '  Aggregate: group by region, product (3 grouping sets)'")
stmt("SELECT GROUPING(region) FROM sale", ("SYNTAX", "GROUPING", "this query has no GROUP BY"))
stmt("SELECT region FROM sale WHERE GROUPING(region) = 0 GROUP BY region",
     ("SYNTAX", "GROUPING", "WHERE cannot use GROUPING"))
stmt("SELECT sum(GROUPING(region)) FROM sale GROUP BY region", ("SYNTAX", "GROUPING", "an aggregate cannot use GROUPING"))
stmt("SELECT GROUPING(amount) FROM sale GROUP BY region",
     ("SYNTAX", "amount", "GROUPING's argument `amount` is not one of the GROUP BY expressions"))
stmt("SELECT product FROM sale GROUP BY ROLLUP(region)",
     ("SYNTAX", "product", "the select list reads column `product`, which is neither grouped by"))
stmt("SELECT count(*) FROM sale GROUP BY CUBE(id, region, product, amount, id, region, product, amount, id, region, "
     "product)", ("UNSUPPORTED", "CUBE", "CUBE of 11 expressions makes 2048 sets"))

sec("ORDER BY past its budget refuses as in memory (docs/gsql_operator_spill_design.md)")
# every value of an ORDER BY term must be comparable with its first, spilled or not; the spilled
# tier runs this whole corpus with every sort on disk
stmt("SELECT id FROM wn ORDER BY CASE WHEN id < 3 THEN id ELSE 'x' END",
     ("EXPRESSION", "CASE", "< compares text with a number"))
stmt("SELECT id FROM wn ORDER BY CASE WHEN id < 3 THEN NULL ELSE 'x' END, id DESC", "= 7 | 6 | 5 | 4 | 3 | 2 | 1")

sec("EXPLAIN says which path, and lists only what it did not consume")
stmt("CREATE TABLE o (id INTEGER PRIMARY KEY, cust TEXT, total NUMBER, placed DATE)", "= done 0")
stmt("CREATE INDEX o_cust ON o (cust, placed DESC)", "= done 0")
stmt("EXPLAIN SELECT * FROM o WHERE id = :id", "= 'Point Read on o using the primary key (id = :id)'")
stmt("EXPLAIN SELECT * FROM o WHERE id > 5 AND total > 1",
     "= 'Seek on o using the primary key (5 <= id)' | '  Filter: (id > 5) AND (total > 1)'")
# a strict bound stays in the Filter, which drops the bound itself; an inclusive one does not
stmt("EXPLAIN SELECT * FROM o WHERE id > 2 AND id <= 4", "= 'Seek on o using the primary key (2 <= id <= 4)' | '  Filter: (id > 2)'")
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
stmt("EXPLAIN SELECT id, rank() OVER (ORDER BY total), sum(total) OVER (PARTITION BY cust), count(*) OVER (ORDER BY total) "
     "FROM o ORDER BY id",
     "= 'Scan on o' | '  Window: rank() OVER (ORDER BY total), sum(total) OVER (PARTITION BY cust), "
     "count(*) OVER (ORDER BY total) (2 orderings, sorted)' | '  Order: id (sorted)'")

open(sys.argv[1], "w").write("\n".join(out) + "\n")

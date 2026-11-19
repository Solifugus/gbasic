' Forked readers against slicing (docs/gsql_parallel_readers_design.md §6
' increment 2): the same statements, each long enough to be working after
' its first slice, so with readers on each runs in a reader of its own and
' with GBASIC_GSQL_READERS=0 each is sliced in the owner. run_gsql_readers.sh
' runs this both ways and requires the two outputs to be identical, and the
' reader log to show readers in one run and none in the other. Every row of
' every answer is printed, so the comparison is byte for byte.
load gsql

path = join_path(env("GSQL_DIR"), env("GSQL_NAME"))
db = gsql.connect(path, { create: true })
x = gsql.exec(db, "create table t (a integer primary key, b integer, c integer, s text, m number)")
x = gsql.exec(db, "create table u (k integer primary key, ta integer, w integer)")
x = gsql.exec(db, "create index t_b on t (b)")
rows = []
for i = 1 to 30000
    rows = append(rows, { a: i, b: mod(i * 7919, 101), c: mod(i * 104729, 9973), s: "s" + string(mod(i * 31, 997)), m: i / 4 })
end for
x = gsql.insert(db, "t", rows)
rows = []
for i = 1 to 10000
    rows = append(rows, { k: i, ta: mod(i * 7, 30000) + 1, w: mod(i, 13) })
end for
x = gsql.insert(db, "u", rows)

statements = [
    "select b, count(*) as n, sum(c) as sc, min(s) as lo, max(m) as hi from t group by b order by b",
    "select a, b, c, s from t order by c desc, a",
    "select distinct s from t order by s",
    "select count(*) as n, sum(a) as sa, avg(c) as ac from t",
    "select t.a, t.c, u.w from t join u on u.ta = t.a order by u.w, t.a",
    "select b, sum(w) as sw from t join u on u.ta = t.a group by b order by sw desc, b",
    "select a, c, row_number() over (partition by b order by c, a) as rn from t order by a",
    "select a, s from t where c < 500 order by s, a",
    "select s, b from t where b < 50 union select s, b from t where c < 3000 order by s, b",
    "select b, c from t where a in (select ta from u where w = 3) order by b, c",
    "select a from t order by a limit 50 offset 20000",
    "with big as (select b, count(*) as n from t group by b) select b, n from big where n > 290 order by n, b",
    "select a, sum(c) over (order by a rows between 2 preceding and current row) as run from t order by a",
    "select b, count(distinct s) as ds from t group by b order by b"]

for each st, n in statements
    r = gsql.query(db, st)
    print string(n) + ": " + string(len(r)) + " rows"
    for each row in r
        print row
    end for
end for

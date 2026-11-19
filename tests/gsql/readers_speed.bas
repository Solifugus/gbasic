' Forked readers measured (docs/gsql_parallel_readers_design.md §5, §6
' increment 2): run_gsql_readers.sh starts this in roles against one owner.
'   GSQL_ROLE=load    the table, 100 000 rows
'   GSQL_ROLE=group   a GROUP BY over every row, GSQL_TIMES times
'   GSQL_ROLE=point   point reads until the file `done` exists, then the
'                     slowest and the 99th percentile in milliseconds
' The runner times N group clients at once against an owner with readers and
' against one with GBASIC_GSQL_READERS=0, the same rows in each.
load gsql

path = join_path(env("GSQL_DIR"), "speed.gsql")
role = env("GSQL_ROLE")
if role = "load" then
    db = gsql.connect(path, { create: true })
    x = gsql.exec(db, "create table t (a integer primary key, b integer, c integer, d integer)")
    for chunk = 0 to 9
        rows = []
        for i = 1 to 10000
            k = chunk * 10000 + i
            rows = append(rows, { a: k, b: mod(k * 7919, 1009), c: mod(k * 104729, 9973), d: mod(k, 97) })
        end for
        x = gsql.insert(db, "t", rows)
    end for
    print "loaded " + string(gsql.query(db, "select count(*) as n from t")[0].n)
else if role = "group" then
    db = gsql.connect(path)
    n = 0
    for i = 1 to number(env("GSQL_TIMES"))
        r = gsql.query(db, "select b, count(*) as n, sum(c) as s, min(d) as lo from t group by b order by b")
        n = n + len(r)
    end for
    print "groups " + string(n)
else
    db = gsql.connect(path)
    times = []
    i = 0
    while not exists(join_path(env("GSQL_DIR"), "done"))
        i = i + 1
        t0 = monotonic()
        r = gsql.query(db, "select c from t where a = ?", [mod(i * 7919, 100000) + 1])
        times = append(times, (monotonic() - t0) * 1000)
    end while
    times = sort(times)
    if len(times) = 0 then
        times = [0]
    end if
    print "point reads " + string(len(times)) + ", p99 " + string(round(times[floor(len(times) * 0.99)], 1)) + " ms, slowest " + string(round(times[len(times) - 1], 1)) + " ms"
end if

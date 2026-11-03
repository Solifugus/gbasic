' Run by run_gsql_module.sh's compare-text tier: `setup` builds 20 000 rows;
' `text` looks 100 keys up as TEXT, which must SEEK; `scan` looks the same keys
' up through `k + 0`, which cannot. The tier times the two from outside, since a
' seek and a scan give the same rows and only the time tells them apart
' (docs/gsql_compare_conversion_design.md §3).
load gsql
program main(args)
    db = gsql.connect("seek.gsql", { create: true })
    if args[0] = "setup" then
        db.exec("create table t (k integer primary key, n integer)")
        db.exec("create index by_n on t (n)")
        db.exec("insert into t select i, i * 3 from (with recursive c (i) as (select 1 union all select i + 1 from c where i < 20000) select i from c) as g")
        print(db.query("select count(*) as c from t")[0].c)
    else
        found = 0
        for j = 1 to 100
            if args[0] = "text" then
                r = db.query("select k from t where n = $1", [string(j * 197 * 3)])
            else
                r = db.query("select k from t where n + 0 = $1", [j * 197 * 3])
            end if
            found = found + count(r)
        next j
        print(found)
    end if
    db.close()
end program

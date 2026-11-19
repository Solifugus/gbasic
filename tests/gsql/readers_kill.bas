' A reader or its owner killed part way through a read
' (docs/gsql_parallel_readers_design.md §4), driven by run_gsql_readers.sh.
' The program streams a read a reader answers; after its first row it writes
' `streaming` and waits for `go`, while the runner kills the reader
' (GSQL_MODE=reader) or the owner (GSQL_MODE=owner). The rest of the read must
' then fail as LOST, never end early as if it had finished; after a killed
' reader the database answers on, after a killed owner a new one does.
load gsql

database k( file: "kill.gsql" )

sql everything on k
select a, c from t order by c, a
end sql

' run with the work directory current, so "kill.gsql" is there
path = join_path(env("GSQL_DIR"), "kill.gsql")
setup = gsql.connect(path, { create: true })
x = gsql.exec(setup, "create table t (a integer primary key, c integer)")
rows = []
for i = 1 to 30000
    rows = append(rows, { a: i, c: mod(i * 104729, 9973) })
end for
x = gsql.insert(setup, "t", rows)
x = gsql.close(setup)

' the read, as far as it got, and how it ended
function stream()
    n = 0
    on error goto next
    for each row in everything
        if n = 0 then
            x = write(join_path(env("GSQL_DIR"), "streaming"), "")
            waited = 0
            while not exists(join_path(env("GSQL_DIR"), "go")) and waited < 600
                sleep(0.1)
                waited = waited + 1
            end while
        end if
        n = n + 1
    end for
    e = error
    if e then
        return { n: n, code: e.code, message: e.message }
    end if
    return { n: n, code: "none", message: "" }
end function

r = stream()
if r.n < 30000 then
    print "rows before it ended: fewer than all"
else
    print "rows before it ended: " + string(r.n)
end if
print "ended: " + r.code
print "  " + r.message
again = gsql.connect(path)
print "a new read: " + string(gsql.query(again, "select count(*) as n from t")[0].n)

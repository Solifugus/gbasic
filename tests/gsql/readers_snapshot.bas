' A forked reader's snapshot (docs/gsql_parallel_readers_design.md §4): a
' read handed to a reader answers the database as it was when it started,
' while another connection deletes, updates and inserts, and compaction
' replaces the files the reader is reading. The owner holds the reader's
' version until the reader is reaped, so no file it may read is deleted: run
' by run_gsql_readers.sh with a 64 KB memtable, compaction at 2 level-0
' files and 2 read handles kept open, so the reader reopens files by name
' and a file deleted early fails the read. GSQL_DIR is the work directory.
load gsql

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if string(got) = string(want) then
        return nothing
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    return nothing
end function

' the sorted files of the database, by name
function sorted_files(dir)
    out = []
    for each f in files(dir)
        if ends_with(f.name, ".sst") then
            out = append(out, f.name)
        end if
    end for
    return out
end function

database snap( file: "snap.gsql", create: true )

sql everything on snap
select a, v, pad from t order by v, a
end sql

dir = join_path(env("GSQL_DIR"), "snap.gsql")
w = gsql.connect(dir, { create: true })
x = gsql.exec(w, "create table t (a integer primary key, v integer, pad text)")
rows = []
for i = 1 to 20000
    rows = append(rows, { a: i, v: mod(i * 7919, 20011), pad: "p" + repeat("x", 100) })
end for
x = gsql.insert(w, "t", rows)
start = gsql.query(w, "select count(*) as n, sum(a) as sa, sum(v) as sv from t")[0]

n = 0
sa = 0
sv = 0
last = -1
ordered = true
pads = true
held = []
for each row in everything
    if n = 0 then
        ' the reader is running and holds the rest of the rows: change
        ' every one of them, enough to flush and compact many times over
        held = sorted_files(dir)
        x = gsql.exec(w, "delete from t where a <= 10000")
        x = gsql.exec(w, "update t set v = v + 1, pad = 'q' || pad")
        more = []
        for i = 20001 to 30000
            more = append(more, { a: i, v: 0, pad: "new" })
        end for
        x = gsql.insert(w, "t", more)
        still = sorted_files(dir) intersecting held
        check("every file the reader's snapshot names is still there while it reads", len(still), len(held))
    end if
    n = n + 1
    sa = sa + row.a
    sv = sv + row.v
    if row.v < last then
        ordered = false
    end if
    last = row.v
    if not starts_with(row.pad, "px") then
        pads = false
    end if
end for
check("the reader answered every row there was when it started", n, start.n)
check("  the same rows", sa, start.sa)
check("  with the values they had", sv, start.sv)
check("  none of the later updates", pads, true)
check("  in order", ordered, true)
check("there were sorted files to hold", len(held) > 0, true)
after = gsql.query(w, "select count(*) as n, sum(v) as sv from t")[0]
check("the other connection's changes are there for a new read", after.n, 20000)
' once the reader is reaped, compaction's leftovers go: so the files were
' held for the reader, and the check above could have failed
gone = 0
tries = 0
while gone = 0 and tries < 50
    gone = len(held) - len(sorted_files(dir) intersecting held)
    tries = tries + 1
    if gone = 0 then
        sleep(0.1)
    end if
end while
check("after the reader ended, files it held were deleted", gone > 0, true)

print "checks: " + string(checks.n)
print "mismatches: " + string(checks.bad)

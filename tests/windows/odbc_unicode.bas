' NON-ASCII TEXT THROUGH ODBC, ASSERTED BY THE SERVER -- not by reading it back.
'
' Needs a SQL Server connection string in GBASIC_ODBC_CONNECTION, e.g.
'   Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=tempdb;
'   Trusted_Connection=yes;TrustServerCertificate=yes
' and SKIPS, saying so, without one. It creates one table and drops it.
'
' WHY THE SERVER IS THE ORACLE: gBASIC's ODBC module uses the ANSI entry points,
' which on Windows pass text through the process code page. gbasic.exe sets
' that to UTF-8 (src/gbasic.manifest). MEASURED WITHOUT THE MANIFEST: every
' round trip below still came back EQUAL -- the reader undid the writer's
' mangling -- while SQL Server held mojibake ("日本語" stored as 9 characters).
' A check that only reads its own write back passes on that binary. LEN() and
' UNICODE() ask the server what it actually stored.

tally = { checks: 0, bad: 0 }

function ok(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print("ok   " + label)
    else
        print("MISMATCH " + label + ": got [" + string(got) + "] want [" + string(want) + "]")
        tally.bad = tally.bad + 1
    end if
end function

conn = env("GBASIC_ODBC_CONNECTION")
if is_unknown(conn) or not contains(lower(conn), "sql server") then
    print("SKIP odbc_unicode (set GBASIC_ODBC_CONNECTION to a SQL Server connection string)")
    exit(0)
end if

load odbc
db = odbc.connect(conn)
odbc.exec(db, "if object_id('tempdb..gb_odbc_unicode') is not null drop table gb_odbc_unicode", [])
odbc.exec(db, "create table gb_odbc_unicode (id int, v nvarchar(100))", [])

samples = ["caf" + chr(233), chr(26085) + chr(26412) + chr(35486), chr(9731),
           "pi " + chr(960) + " " + chr(8364)]
names = ["Latin-1 (cafe)", "CJK (three characters)", "a symbol (snowman, three UTF-8 bytes)",
         "Greek and the euro sign"]
i = 0
for each s in samples
    odbc.exec(db, "insert into gb_odbc_unicode values (?, ?)", [i, s])
    i += 1
end for

rows = odbc.query(db, "select id, v, len(v) as n, unicode(v) as first from gb_odbc_unicode order by id", [])
for each r in rows
    s = samples[r.id]
    ok(names[r.id] + ": the server counts the right characters", r.n, len(s))
    ok(names[r.id] + ": the server stored the right first code point", r.first, code(s))
    ok(names[r.id] + ": and it reads back unchanged", r.v = s, true)
end for

' Text in the SQL itself goes through SQLPrepare rather than a bound parameter.
lit = odbc.query(db, "select len(N'" + chr(26085) + chr(26412) + "') as n, unicode(N'" + chr(960) + "') as c", [])
ok("a literal in the SQL text: length", lit[0].n, 2)
ok("a literal in the SQL text: code point", lit[0].c, 960)

odbc.exec(db, "drop table gb_odbc_unicode", [])
odbc.close(db)

print("")
if tally.checks < 14 then
    print("BROKEN: only " + string(tally.checks) + " checks were counted")
    print("MISMATCHES: 1")
else
    print("checks: " + string(tally.checks))
    if tally.bad = 0 then
        print("mismatches: 0")
    else
        print("MISMATCHES: " + string(tally.bad))
    end if
end if

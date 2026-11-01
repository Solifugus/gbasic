' gsql.backup (docs/gsql_backup_design.md): a consistent copy made while the
' database stays in use, refreshing a copy already there. Run by
' tests/run_gsql_module.sh FROM AN EMPTY DIRECTORY, which is what a relative
' path is resolved against. Every answer is written here by hand.
load gsql

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if type(got) = type(want) then
        if got = want then
            return nothing
        end if
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + type(got) + " " + string(got) + ", want " + type(want) + " " + string(want)
    return nothing
end function

db = gsql.connect("live.gsql", { create: true })
pad = repeat("a value long enough to fill the memtable sooner ", 3)
gsql.exec(db, "create table t (id integer primary key, v text)")
' enough rows that the engine has written sorted files, so a refresh has files
' to keep: about 40 MB, past the 32 MB memtable
for b = 0 to 29
    gsql.begin(db)
    for i = 1 to 10000
        n = b * 10000 + i
        gsql.exec(db, "insert into t values ($1, $2)", [n, pad + string(n)])
    next
    gsql.commit(db)
next

' a relative directory is the program's, not the owner's
first = gsql.backup(db, "copy.gsql")
check("a first backup copies something", first.bytes > 0, true)
check("a first backup names files", first.files > 0, true)
check("a first backup keeps nothing", first.reused, 0)
check("it was made beside the program, as a relative path says", exists({dir}"copy.gsql"), true)

' a backup READS: inside a read transaction it copies that snapshot, where a
' statement counted as a write would be refused (and would hold the writer)
gsql.begin(db, "read")
in_read = gsql.backup(db, "copy-read.gsql")
gsql.commit(db)
check("a backup inside a read transaction", in_read.bytes > 0, true)

gsql.exec(db, "insert into t values (300001, 'after the first backup')")
again = gsql.backup(db, "copy.gsql")
check("a refresh keeps files the copy already held", again.reused > 0, true)
' Less, not "far less": the unflushed changes, up to the 32 MB memtable, are
' written whole at every refresh however little changed -- measured here at
' 32 MB of a 60 MB database, a floor that is negligible at many gigabytes
check("a refresh copies less than the first backup", again.bytes < first.bytes, true)

' The copy is read by a program of its own, as a report would be: an owner
' lives as long as the program that started it, so a copy THIS program opened
' would stay held, and refusing a refresh, until this program ended.
function ask_copy(sql)
    write({file}"ask.bas", "load gsql\ncp = gsql.connect(\"copy.gsql\")\nprint gsql.query(cp, \"" + sql + "\")[0].x\n")
    r = process.run({ command: env("GBASIC_BIN"), args: ["ask.bas"] })
    return trim(r.stdout)
end function
check("the refreshed copy holds every row", ask_copy("select count(*) as x from t"), "300001")
check("including the one written after the first backup", ask_copy("select max(id) as x from t"), "300001")
check("and reads the same values", ask_copy("select v as x from t where id = 12345"), pad + "12345")

function refused(target)
    on error goto next
    gsql.backup(db, target)
    if error then
        return error.details.code
    end if
    return "(no error)"
end function
' a copy this program holds open is refused, and so is the live database
cp = gsql.connect("copy.gsql")
check("a copy that is open is refused", refused("copy.gsql"), "STATE")
check("and so is the live database itself", refused("live.gsql"), "STATE")
gsql.close(cp)
' (and this program's own owner for the copy holds it until the program ends,
' so the copy is refreshed below under a second name)
' a directory holding a file no database writes
make_dir({dir}"not-a-database")
write({file}"not-a-database/notes.txt", "keep me")
check("a directory of other files is refused", refused("not-a-database"), "EXISTS")
check("and its file is left alone", read({file}"not-a-database/notes.txt"), "keep me")

function not_text()
    on error goto next
    gsql.backup(db, 5)
    if error then
        return "refused"
    end if
    return "(no error)"
end function
check("a directory that is not text is refused", not_text(), "refused")

' a refresh after a deletion holds the deletion: a second copy, refreshed
gsql.exec(db, "delete from t where id > 299990")
gsql.backup(db, "copy2.gsql")
gsql.exec(db, "delete from t where id > 299980")
last = gsql.backup(db, "copy2.gsql")
check("a refresh of the second copy keeps its files", last.reused > 0, true)
write({file}"ask2.bas", "load gsql\ncp = gsql.connect(\"copy2.gsql\")\nprint gsql.query(cp, \"select count(*) as x from t\")[0].x\n")
check("and holds the deletion", trim(process.run({ command: env("GBASIC_BIN"), args: ["ask2.bas"] }).stdout), "299980")

gsql.close(db)
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"

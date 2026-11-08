load sqlite

db = sqlite.connect(":memory:")
print(type(db))

created = sqlite.exec(db, "create table gbasic_sqlite_test (id integer primary key, active integer, name text, score real, optional text)")
print(created.command)

inserted = sqlite.exec(db, "insert into gbasic_sqlite_test (id, active, name, score, optional) values (?, ?, ?, ?, ?)", [1, true, "Ada", 12.5, nothing])
print(inserted.command)
print(inserted.rows_affected)
print(sqlite.last_insert_rowid(db))

rows = sqlite.query(db, "select id, active, name, score, optional from gbasic_sqlite_test where id = ?", [1])
print(count(rows))
print(rows[0].id)
print(rows[0].active)
print(rows[0].name)
print(rows[0].score)
print(rows[0].optional = nothing)

sqlite.begin(db)
sqlite.exec(db, "insert into gbasic_sqlite_test (id, active, name) values (2, 0, 'Rollback')")
sqlite.rollback(db)
rolled_back = sqlite.query(db, "select count(*) as count from gbasic_sqlite_test where id = 2")
print(rolled_back[0].count)

sqlite.begin(db)
sqlite.exec(db, "insert into gbasic_sqlite_test (id, active, name) values (3, 1, 'Commit')")
sqlite.commit(db)
committed = sqlite.query(db, "select count(*) as count from gbasic_sqlite_test where id = 3")
print(committed[0].count)

on error goto next
duplicate = sqlite.query(db, "select 1 as id, 2 as id")
print(error.source)
error.clear()

discarded = sqlite.exec(db, "select 1")
print(error.source)
error.clear()

bad_param = sqlite.query(db, "select ?", [unknown])
print(error.source)
error.clear()

' an INTEGER past 2^53 comes back EXACT (book gap G46): it came back as the
' nearest double, 9007199254740993 as ...992, with nothing said
sqlite.exec(db, "create table big (n integer)")
sqlite.exec(db, "insert into big values (9007199254740993), (-9223372036854775808)")
big = sqlite.query(db, "select n from big")
print(big[0].n + 1)
print(big[1].n)

alias = db
print(sqlite.close(db))
closed = sqlite.query(alias, "select 1")
print(error.source)
error.clear()

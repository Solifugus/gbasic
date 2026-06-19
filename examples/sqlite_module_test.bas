load sqlite

db = sqlite.connect(":memory:")

sqlite.exec(db, "create table users (id integer primary key, name text, active integer)")
sqlite.exec(db, "insert into users (name, active) values (?, ?)", ["Ada", true])

print(sqlite.last_insert_rowid(db))

rows = sqlite.query(db, "select id, name, active from users where active = ?", [true])
print(count(rows))
print(rows[0].id)
print(rows[0].name)
print(rows[0].active)

print(sqlite.close(db))

load sqlite
db = sqlite.connect(":memory:")
print(sqlite.query(db, "select zeroblob(1) as payload"))

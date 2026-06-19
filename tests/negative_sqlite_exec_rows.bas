load sqlite
db = sqlite.connect(":memory:")
print(sqlite.exec(db, "select 1"))

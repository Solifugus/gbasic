load pg

schema(file)= "examples/gbasic_site/sql/schema.sql"
reset(file)= "examples/gbasic_site/sql/reset.sql"
seed(file)= "examples/gbasic_site/sql/seed.sql"

db = pg.connect({})

pg.exec(db, read(schema))
pg.exec(db, read(reset))
pg.exec(db, read(seed))

print("gbasic_site database ready")
pg.close(db)

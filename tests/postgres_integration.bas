load pg

db = pg.connect({})
print(type(db))

pg.exec(db, "create temporary table gbasic_pg_test (id integer primary key, active boolean, name text, payload jsonb, happened_at timestamp)")

inserted = pg.exec(db, "insert into gbasic_pg_test (id, active, name, payload, happened_at) values ($1, $2, $3, $4::jsonb, $5::timestamp)", [1, true, "Ada", {role:"admin", enabled:true, optional:nothing}, "2026-06-06 12:34:56"])
print(inserted.command)
print(inserted.rows_affected)

rows = pg.query(db, "select id, active, name, payload, happened_at, 9223372036854775807::bigint as large_id, 12.3400::numeric as exact_value from gbasic_pg_test where id = $1", [1])
print(len(rows))
print(rows[0].id)
print(rows[0].active)
print(rows[0].name)
print(rows[0].payload.role)
print(is_nothing(rows[0].payload.optional))
print(rows[0].happened_at)
print(rows[0].large_id)
print(rows[0].exact_value)

null_row = pg.query(db, "select null::text as absent")
print(is_nothing(null_row[0].absent))

pg.begin(db)
pg.exec(db, "insert into gbasic_pg_test (id, active, name) values (2, false, 'Rollback')")
pg.rollback(db)
rolled_back = pg.query(db, "select count(*) as count from gbasic_pg_test where id = 2")
print(rolled_back[0].count)

pg.begin(db)
pg.exec(db, "insert into gbasic_pg_test (id, active, name) values (3, true, 'Commit')")
pg.commit(db)
committed = pg.query(db, "select count(*) as count from gbasic_pg_test where id = 3")
print(committed[0].count)

on error goto next
duplicate = pg.query(db, "select 1 as id, 2 as id")
print(error.source)
error.clear()

discarded = pg.exec(db, "select 1")
print(error.source)
error.clear()

' a kind Postgres cannot take (until 2026-11-20 this was `unknown`, which now
' binds as NULL, below)
unsupported = pg.query(db, "select $1", [5 minutes])
print(error.source)
error.clear()

bad_sql = pg.query(db, "select from")
print(error.source)
error.clear()

' `unknown` binds as SQL NULL, as G-SQL and pg.insert take it (DOGFOOD
' 2026-11-18): Postgres's own `is null` counts it, in an array element too
u = pg.exec(db, "create temp table gb_unknown (id integer, v text)")
u = pg.exec(db, "insert into gb_unknown values ($1, $2)", [1, unknown])
u = pg.exec(db, "insert into gb_unknown values ($1, $2)", [2, "x"])
print(pg.query(db, "select count(*) as n from gb_unknown where v is null")[0].n)
print(pg.query(db, "select $1::int[] as a", [[1, unknown, 3]])[0].a)
alias = db
print(pg.close(db))
closed = pg.query(alias, "select 1")
print(error.source)
error.clear()

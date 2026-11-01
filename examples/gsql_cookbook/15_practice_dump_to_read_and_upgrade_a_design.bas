' Recipe 15 (a practice): a SQL dump, to read a database's design and to
' upgrade it. A dump is text a person can read and edit, and gsql.restore
' runs it into another database: the schema-only dump is the design on one
' page, and editing a full dump before restoring it is how a design changes
' in a way no ALTER TABLE offers.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "shop.gsql"), { create: true })
gsql.exec(db, "create table customer (id integer serial primary key, name text not null, phone text)")
gsql.exec(db, "insert into customer (name, phone) values ('Ada', '555-0100'), ('Grace', null)")

' the design, on one page
schema = join_path(work, "shop-schema.sql")
gsql.dump(db, schema, { schema_only: true })
print read({file}schema)

' everything, then the design changed in the text: phone becomes NOT NULL
' with a default, which existing rows must satisfy, so the dump's NULL is
' filled in the same pass
full = join_path(work, "shop.sql")
r = gsql.dump(db, full)
print "dumped " + string(r.tables) + " table, " + string(r.rows) + " rows"
text = read({file}full)
text = replace(text, "phone TEXT", "phone TEXT NOT NULL DEFAULT 'unlisted'")
text = replace(text, "'Grace', NULL", "'Grace', 'unlisted'")
upgraded = join_path(work, "shop-v2.sql")
write({file}upgraded, text)

v2 = gsql.connect(join_path(work, "shop-v2.gsql"), { create: true })
r = gsql.restore(v2, upgraded)
print "restored " + string(r.statements) + " statements, " + string(r.rows) + " rows"
for each c in gsql.query(v2, "select id, name, phone from customer order by id")
    print string(c.id) + " " + c.name + " " + c.phone
next

' the serial carries on from where the old database left off
gsql.exec(v2, "insert into customer (name) values ('Linus')")
print "next id: " + string(gsql.query(v2, "select max(id) as m from customer")[0].m)

' a restore only writes to tables it made, so a file cannot touch data
' already there
on error goto next
write({file}upgraded, "delete from customer;")
gsql.restore(v2, upgraded)
if error then
    print "a DELETE in the file is refused: " + error.details.code
end if

gsql.close(v2)
gsql.close(db)
remove_tree(work)

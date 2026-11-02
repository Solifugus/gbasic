' Recipe 15 (a practice): a SQL dump, to read a database's design and to
' upgrade it. A dump is text a person can read and edit, and gsql.restore
' runs it into another database: the schema-only dump is the design on one
' page, and editing a full dump before restoring it is how a design changes
' in a way no ALTER TABLE offers.
load gsql

for each f in ["cookbook_dump.gsql", "cookbook_dump_v2.gsql"]
    if exists({dir}f) then
        remove_tree({dir}f)
    end if
next

database shop( file: "cookbook_dump.gsql", create: true )
database v2( file: "cookbook_dump_v2.gsql", create: true )

sql make on shop
    create table customer (id integer serial primary key, name text not null, phone text);
    insert into customer (name, phone) values ('Ada', '555-0100'), ('Grace', null)
end sql

sql customers on v2
    select id, name, phone from customer order by id
end sql

sql add_linus on v2
    insert into customer (name) values ('Linus')
end sql

sql highest on v2
    select max(id) as m from customer
end sql

make.exec()

' the design, on one page
work = temp_dir()
schema = join_path(work, "shop-schema.sql")
shop.dump(schema, { schema_only: true })
print read({file}schema)

' everything, then the design changed in the text: phone becomes NOT NULL
' with a default, which existing rows must satisfy, so the dump's NULL is
' filled in the same pass. The dump is text, edited as text: this is where a
' string, not a block, is the right tool.
full = join_path(work, "shop.sql")
r = shop.dump(full)
print "dumped " + string(r.tables) + " table, " + string(r.rows) + " rows"
text = read({file}full)
text = replace(text, "phone TEXT", "phone TEXT NOT NULL DEFAULT 'unlisted'")
text = replace(text, "'Grace', NULL", "'Grace', 'unlisted'")
upgraded = join_path(work, "shop-v2.sql")
write({file}upgraded, text)

r = v2.restore(upgraded)
print "restored " + string(r.statements) + " statements, " + string(r.rows) + " rows"
for each c in customers
    print string(c.id) + " " + c.name + " " + c.phone
next

' the serial carries on from where the old database left off
add_linus.exec()
print "next id: " + string(highest.query()[0].m)

' a restore only writes to tables it made, so a file cannot touch data
' already there
on error goto next
write({file}upgraded, "delete from customer;")
v2.restore(upgraded)
if error then
    print "a DELETE in the file is refused: " + error.details.code
end if

v2.close()
shop.close()
remove_tree(work)
remove_tree({dir}"cookbook_dump.gsql")
remove_tree({dir}"cookbook_dump_v2.gsql")

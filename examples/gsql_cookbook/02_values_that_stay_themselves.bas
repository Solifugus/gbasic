' Recipe 2: values cross into the database and back as what they were.
load gsql

if exists({dir}"cookbook_values.gsql") then
    remove_tree({dir}"cookbook_values.gsql")
end if

database vals( file: "cookbook_values.gsql", create: true )

sql make on vals
    create table v (id integer primary key, big integer, cost money(EUR), day date, note text)
end sql

sql add_full on vals
    insert into v values (1, :big, :cost, :day, :note)
end sql

sql add_bare on vals
    insert into v (id) values (2)
end sql

sql everything on vals
    select * from v order by id
end sql

sql first on vals
    select big, cost, day, note from v where id = 1
end sql

sql second_note on vals
    select note from v where id = 2
end sql

make.exec()

big {number}= "9007199254740993"            ' 2^53 + 1: past what a double holds exactly
cost = {EUR}"19.99"
day = {date}"2026-11-01"
note = ""
add_full.exec()
add_bare.exec()                          ' every other column NULL

for each row in everything
    print row
next

back = first.query()[0]
print "big + 1 = " + string(back.big + 1)          ' exact: ...994
print "cost is " + money.currency(back.cost)
print "day is a " + type(back.day) + ", note is " + type(back.note) + " of length " + string(len(back.note))

' SQL NULL reads back as unknown, which is not the empty string
gap = second_note.query()[0].note
print "a missing note is unknown: " + string(is_unknown(gap))

vals.close()
remove_tree({dir}"cookbook_values.gsql")

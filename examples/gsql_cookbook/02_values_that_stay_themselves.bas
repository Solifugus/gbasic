' Recipe 2: values cross into the database and back as what they were.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "values.gsql"), { create: true })
gsql.exec(db, "create table v (id integer primary key, big integer, cost money(EUR), day date, note text)")

big {number}= "9007199254740993"            ' 2^53 + 1: past what a double holds exactly
gsql.exec(db, "insert into v values (1, :big, :cost, :day, :note)",
          { big: big, cost: {EUR}"19.99", day: {date}"2026-11-01", note: "" })
gsql.exec(db, "insert into v (id) values (2)")   ' every other column NULL

for each row in gsql.query(db, "select * from v order by id")
    print row
next

back = gsql.query(db, "select big, cost, day, note from v where id = 1")[0]
print "big + 1 = " + string(back.big + 1)          ' exact: ...994
print "cost is " + money.currency(back.cost)
print "day is a " + type(back.day) + ", note is " + type(back.note) + " of length " + string(len(back.note))

' SQL NULL reads back as unknown, which is not the empty string
gap = gsql.query(db, "select note from v where id = 2")[0].note
print "a missing note is unknown: " + string(is_unknown(gap))

gsql.close(db)
remove_tree(work)

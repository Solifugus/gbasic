' Recipe 3: transactions -- many writes, one commit; and changing your mind.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "bank.gsql"), { create: true })
gsql.exec(db, "create table acct (id integer primary key, owner text, bal money(USD))")

' Every exec outside begin/commit is its own transaction, and each commit is
' one sync to disk. Batching rows into one transaction is the single biggest
' thing a program can do for write speed (docs/gsql_practices.md).
gsql.begin(db)
for i = 1 to 100
    gsql.exec(db, "insert into acct values ($1, $2, $3)", [i, "owner " + string(i), {USD}"100.00"])
next
gsql.commit(db)
print "after the batch: " + string(gsql.query(db, "select count(*) as n from acct")[0].n) + " accounts"

' a transfer: both halves or neither
gsql.begin(db)
gsql.exec(db, "update acct set bal = bal - cast('30' as money(USD)) where id = 1")
gsql.exec(db, "update acct set bal = bal + cast('30' as money(USD)) where id = 2")
gsql.rollback(db)                      ' changed our mind: neither happened
r = gsql.query(db, "select id, bal from acct where id <= 2 order by id")
print "after a rollback: " + string(r[0].bal) + " and " + string(r[1].bal)

' A refused statement that changed nothing leaves the transaction usable.
function insert_twice()
    on error goto next
    gsql.begin(db)
    gsql.exec(db, "insert into acct values (1, 'again', cast('1' as money(USD)))")   ' id 1 is taken
    if error then
        print "refused: " + error.details.code + ", and the transaction carries on"
    end if
    gsql.exec(db, "insert into acct values (101, 'new', cast('1' as money(USD)))")
    gsql.commit(db)
    return nothing
end function
insert_twice()
print "now " + string(gsql.query(db, "select count(*) as n from acct")[0].n) + " accounts"

gsql.close(db)
remove_tree(work)

' Recipe 3: transactions -- many writes, one commit; and changing your mind.
load gsql

if exists({dir}"cookbook_bank.gsql") then
    remove_tree({dir}"cookbook_bank.gsql")
end if

database bank( file: "cookbook_bank.gsql", create: true )

sql make on bank
    create table acct (id integer primary key, owner text, bal money(USD))
end sql

sql open_acct on bank
    insert into acct values (:id, :owner, :bal)
end sql

sql how_many on bank
    select count(*) as n from acct
end sql

sql debit on bank
    update acct set bal = bal - :amount where id = :from_id
end sql

sql credit on bank
    update acct set bal = bal + :amount where id = :to_id
end sql

sql first_two on bank
    select id, bal from acct where id <= 2 order by id
end sql

make.exec()

' Every exec outside begin/commit is its own transaction, and each commit is
' one sync to disk. Batching rows into one transaction is the single biggest
' thing a program can do for write speed (docs/gsql_practices.md).
bank.begin()
bal = {USD}"100.00"
for i = 1 to 100
    id = i
    owner = "owner " + string(i)
    open_acct.exec()
next
bank.commit()
print "after the batch: " + string(how_many.query()[0].n) + " accounts"

' a transfer: both halves or neither
amount = {USD}"30"
from_id = 1
to_id = 2
bank.begin()
debit.exec()
credit.exec()
bank.rollback()                    ' changed our mind: neither happened
r = first_two.query()
print "after a rollback: " + string(r[0].bal) + " and " + string(r[1].bal)

' A refused statement that changed nothing leaves the transaction usable.
function insert_twice()
    on error goto next
    bank.begin()
    id = 1                             ' id 1 is taken
    owner = "again"
    bal = {USD}"1"
    open_acct.exec()
    if error then
        print "refused: " + error.details.code + ", and the transaction carries on"
    end if
    id = 101
    owner = "new"
    open_acct.exec()
    bank.commit()
    return nothing
end function
insert_twice()
print "now " + string(how_many.query()[0].n) + " accounts"

bank.close()
remove_tree({dir}"cookbook_bank.gsql")

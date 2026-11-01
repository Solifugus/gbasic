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

gsql.exec(make)

' Every exec outside begin/commit is its own transaction, and each commit is
' one sync to disk. Batching rows into one transaction is the single biggest
' thing a program can do for write speed (docs/gsql_practices.md).
gsql.begin(bank)
bal = {USD}"100.00"
for i = 1 to 100
    id = i
    owner = "owner " + string(i)
    gsql.exec(open_acct)
next
gsql.commit(bank)
print "after the batch: " + string(gsql.query(how_many)[0].n) + " accounts"

' a transfer: both halves or neither
amount = {USD}"30"
from_id = 1
to_id = 2
gsql.begin(bank)
gsql.exec(debit)
gsql.exec(credit)
gsql.rollback(bank)                    ' changed our mind: neither happened
r = gsql.query(first_two)
print "after a rollback: " + string(r[0].bal) + " and " + string(r[1].bal)

' A refused statement that changed nothing leaves the transaction usable.
function insert_twice()
    on error goto next
    gsql.begin(bank)
    id = 1                             ' id 1 is taken
    owner = "again"
    bal = {USD}"1"
    gsql.exec(open_acct)
    if error then
        print "refused: " + error.details.code + ", and the transaction carries on"
    end if
    id = 101
    owner = "new"
    gsql.exec(open_acct)
    gsql.commit(bank)
    return nothing
end function
insert_twice()
print "now " + string(gsql.query(how_many)[0].n) + " accounts"

gsql.close(bank)
remove_tree({dir}"cookbook_bank.gsql")

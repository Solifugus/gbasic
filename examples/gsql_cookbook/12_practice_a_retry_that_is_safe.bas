' Recipe 12 (a practice): make a write safe to retry. A program that does not
' know whether its last write landed -- a timeout, a lost owner, a crash --
' sends it again. With a request id held in a UNIQUE column, the second copy
' is refused as a duplicate instead of being applied twice.
load gsql

if exists({dir}"cookbook_pay.gsql") then
    remove_tree({dir}"cookbook_pay.gsql")
end if

database pay_db( file: "cookbook_pay.gsql", create: true )

sql make on pay_db
    create table payment (id integer serial primary key, request text unique not null, amount money(USD))
end sql

sql record_payment on pay_db
    insert into payment (request, amount) values (:request, :amount)
end sql

sql paid_in_all on pay_db
    select count(*) as n, sum(amount) as s from payment
end sql

gsql.exec(make)

' :request and :amount are this function's own parameters
function pay(request, amount)
    on error goto next
    gsql.exec(record_payment)
    if error then
        if error.details.code = "DUPLICATE" then
            return "already done"
        end if
        error error                ' anything else is a real failure: pass it on
    end if
    return "paid"
end function

print pay("req-1001", {USD}"25.00")
print pay("req-1001", {USD}"25.00")       ' the retry
print pay("req-1002", {USD}"40.00")
r = gsql.query(paid_in_all)[0]
print string(r.n) + " payments, " + string(r.s) + " in all"

gsql.close(pay_db)
remove_tree({dir}"cookbook_pay.gsql")

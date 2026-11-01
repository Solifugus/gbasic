' Recipe 12 (a practice): make a write safe to retry. A program that does not
' know whether its last write landed -- a timeout, a lost owner, a crash --
' sends it again. With a request id held in a UNIQUE column, the second copy
' is refused as a duplicate instead of being applied twice.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "pay.gsql"), { create: true })
gsql.exec(db, "create table payment (id integer serial primary key, request text unique not null, amount money(USD))")

function pay(request, amount)
    on error goto next
    gsql.exec(db, "insert into payment (request, amount) values ($1, $2)", [request, amount])
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
r = gsql.query(db, "select count(*) as n, sum(amount) as s from payment")[0]
print string(r.n) + " payments, " + string(r.s) + " in all"

gsql.close(db)
remove_tree(work)

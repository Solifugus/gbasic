' Recipe 11 (a practice): one database per tenant, and combining their
' answers correctly. Databases are cheap -- about 1 MB empty, each with its own
' owner process started on demand -- so a tenant is backed up, moved or deleted
' as a whole, and one tenant's long report never stalls another's work.
load gsql

work = temp_dir()
tenants = ["acme", "globex"]
dbs = {}
for each t in tenants
    dbs[t] = gsql.connect(join_path(work, t + ".gsql"), { create: true })
    gsql.exec(dbs[t], "create table invoice (id integer serial primary key, amount money(USD))")
next
' acme: three small invoices; globex: one large one
gsql.exec(dbs["acme"], "insert into invoice (amount) values (cast('10' as money(USD))), (cast('20' as money(USD))), (cast('30' as money(USD)))")
gsql.exec(dbs["globex"], "insert into invoice (amount) values (cast('300' as money(USD)))")

' Combine PARTS, not answers. count, sum, min and max merge directly; an
' average is the sums over the counts, never the averages averaged.
total = {USD}"0"
count = 0
averages = []
for each t in tenants
    r = gsql.query(dbs[t], "select count(*) as n, sum(amount) as s, avg(amount) as a from invoice")[0]
    count = count + r.n
    total = total + r.s
    append(averages, r.a)
    print t + ": " + string(r.n) + " invoices, average " + string(r.a)
next
print "the right average across tenants:   " + string(total / count)
print "the average of the averages (wrong): " + string((averages[0] + averages[1]) / 2)

for each t in tenants
    gsql.close(dbs[t])
next
remove_tree(work)

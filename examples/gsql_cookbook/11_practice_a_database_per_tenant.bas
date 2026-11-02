' Recipe 11 (a practice): one database per tenant, and combining their
' answers correctly. Databases are cheap -- about 1 MB empty, each with its own
' owner process started on demand -- so a tenant is backed up, moved or deleted
' as a whole, and one tenant's long report never stalls another's work.
load gsql

' The SQL is the same for every tenant, so it is written once, on a database
' chosen at run time: `database tenant()` has no file, and each use of one of
' its statements says which tenant's connection it runs on.
database tenant()

sql make on tenant
    create table invoice (id integer serial primary key, amount money(USD))
end sql

sql bill on tenant
    insert into invoice (amount) values (:amount)
end sql

sql totals on tenant
    select count(*) as n, sum(amount) as s, avg(amount) as a from invoice
end sql

work = temp_dir()
tenants = ["acme", "globex"]
dbs = {}
for each t in tenants
    dbs[t] = gsql.connect(join_path(work, t + ".gsql"), { create: true })
    dbs[t].exec(make)
next
' acme: three small invoices; globex: one large one
for each amount in [{USD}"10", {USD}"20", {USD}"30"]
    dbs["acme"].exec(bill)
next
amount = {USD}"300"
dbs["globex"].exec(bill)

' Combine PARTS, not answers. count, sum, min and max merge directly; an
' average is the sums over the counts, never the averages averaged.
total = {USD}"0"
count = 0
averages = []
for each t in tenants
    r = dbs[t].query(totals)[0]
    count = count + r.n
    total = total + r.s
    append(averages, r.a)
    print t + ": " + string(r.n) + " invoices, average " + string(r.a)
next
print "the right average across tenants:   " + string(total / count)
print "the average of the averages (wrong): " + string((averages[0] + averages[1]) / 2)

for each t in tenants
    dbs[t].close()
next
remove_tree(work)

' Recipe 16: window functions -- a running balance, the latest row per key,
' the change since the row before, and a rank. An aggregate folds its rows
' into one; a window function answers for every row, from the rows around it.
load gsql

if exists({dir}"cookbook_windows.gsql") then
    remove_tree({dir}"cookbook_windows.gsql")
end if

database books( file: "cookbook_windows.gsql", create: true )

sql make on books
    create table entry (id integer primary key, account text, posted date, amount money(USD));
    insert into entry values
        (1, 'cash', '2026-10-01', 500.00), (2, 'card', '2026-10-02', -45.50),
        (3, 'cash', '2026-10-03', -120.00), (4, 'cash', '2026-10-03', -30.00),
        (5, 'savings', '2026-10-04', 1000.00), (6, 'card', '2026-10-05', -12.25),
        (7, 'cash', '2026-10-07', 200.00), (8, 'card', '2026-10-09', 57.75)
end sql

' a running balance: the sum over the account's rows up to this one
sql balances on books
    select account, posted, amount,
           sum(amount) over (partition by account order by posted, id) as balance
    from entry
    order by account, posted, id
end sql

' the latest row per account: number each account's rows newest first, then
' keep the first. A window cannot stand in WHERE, so it is numbered inside a
' derived table and filtered outside it.
sql latest on books
    select account, posted, amount from (
        select account, posted, amount,
               row_number() over (partition by account order by posted desc, id desc) as newest
        from entry
    ) as t
    where newest = 1
    order by account
end sql

' the change from the account's row before; the first row has none
sql changes on books
    select account, posted, amount,
           amount - lag(amount) over (partition by account order by posted, id) as change
    from entry
    where account = 'card'
    order by posted
end sql

' a window over groups: each account's total, and its place among them
sql ranked on books
    select account, sum(amount) as total, rank() over (order by sum(amount) desc) as place
    from entry
    group by account
    order by place
end sql

make.exec()

print "Running balances:"
for each r in balances
    print "  " + r.account + "  " + string(r.posted) + "  " + string(r.amount) + "  balance " + string(r.balance)
next

print "Latest per account:"
for each r in latest
    print "  " + r.account + "  " + string(r.posted) + "  " + string(r.amount)
next

print "Card, change since the row before:"
for each r in changes
    print "  " + string(r.posted) + "  " + string(r.amount) + "  change " + string(r.change)
next

print "Accounts by total:"
for each r in ranked
    print "  " + string(r.place) + ". " + r.account + "  " + string(r.total)
next

books.close()
remove_tree({dir}"cookbook_windows.gsql")

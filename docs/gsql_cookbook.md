# G-SQL cookbook

Worked recipes for G-SQL, gBASIC's own database, from a first table to the practices a production program needs.

**This page cannot lie.** Every code block below is a real file under `examples/gsql_cookbook/`, and every output block is that file's actual stdout, compared byte for byte by `tests/run_gsql_cookbook.sh`.

Run any recipe yourself:

```
GBASIC_PATH=stdlib ./gbasic examples/gsql_cookbook/01_first_database.bas
```

**SQL is written in blocks**, G-SQL's normal form: a `database` block names the file and an `sql` block holds one statement, checked when the program loads, with values reaching it only as `:name` parameters, so nothing a user types can become part of the SQL. A string appears only where a recipe needs SQL that is not fixed text: recipe 10 sends broken SQL on purpose, and recipe 15 edits a dump. Part 3 is SQL for reports: window functions, recursive queries and subtotals. Each recipe's database is a file in the current directory, removed before and after, so it runs the same way every time.

Where to read more:

- the module's verbs and the blocks: `docs/reference.md`, *G-SQL Module*;
- what SQL is understood: `docs/gsql_sql_design.md` and the design pages beside it;
- the practices in §2, with the measurements behind them: `docs/gsql_practices.md`.

---

# Part 1: using it

## 1. A first database

A database is a directory, created on first use with `create: true`. A `database` block names it, and opens it the first time one of its statements runs. Its **owner**, the one process that serves every program using it, is started by the first program that connects. It serves every program connected to it, and stops by itself once nobody has been connected for 3 seconds (`linger:` changes that).

A connection and a block are **handles**, and a handle's verbs are its methods: `add.exec()`, `shop.close()`. `gsql.exec(add)` and `gsql.close(shop)` are the same calls written the other way; this page uses the methods throughout.

<!--CODE:01_first_database-->

```basic
' Recipe 1: a first database -- create it, make a table, put rows in, ask.
load gsql

' A `database` block names the file, found from the current directory; it
' opens itself the first time a statement uses it. This recipe cleans up
' after itself, before and after.
if exists({dir}"cookbook_first.gsql") then
    remove_tree({dir}"cookbook_first.gsql")
end if

database shop( file: "cookbook_first.gsql", create: true )

sql make on shop
    create table item (id integer primary key, name text not null, price money(USD))
end sql

' :name parameters are read from the variables of those names where the
' statement runs; nothing is ever pasted into the SQL
sql add on shop
    insert into item (id, name, price) values (:id, :name, :price)
end sql

sql add_two on shop
    insert into item values (3, 'fig', cast('1.20' as money(USD))), (4, 'kiwi', cast('0.40' as money(USD)))
end sql

sql under on shop
    select id, name, price from item where price < :limit order by price
end sql

make.exec()

id = 1
name = "apple"
price = {USD}"0.50"
add.exec()
id = 2
name = "pear"
price = {USD}"0.75"
add.exec()
r = add_two.exec()
print r.command + ": " + string(r.rows_affected) + " rows"

' a select block is iterated a row at a time, fields in select-list order
limit = {USD}"1.00"
for each row in under
    print string(row.id) + "  " + row.name + "  " + string(row.price)
next

shop.close()
remove_tree({dir}"cookbook_first.gsql")
```

<!--OUT:01_first_database-->

```
INSERT: 2 rows
4  kiwi  0.40
1  apple  0.50
2  pear  0.75
```

## 2. Values stay themselves

What goes in comes back as what it was:

- an integer past 2^53, exact;
- money with its currency;
- a date as a date;
- the empty string as the empty string.

SQL's `NULL` reads back as gBASIC's `unknown`, which is a different thing from `""`.

<!--CODE:02_values_that_stay_themselves-->

```basic
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
```

<!--OUT:02_values_that_stay_themselves-->

```
{"id":1,"big":9007199254740993,"cost":19.99,"day":2026-11-01,"note":""}
{"id":2,"big":unknown,"cost":unknown,"day":unknown,"note":unknown}
big + 1 = 9007199254740994
cost is EUR
day is a datetime, note is string of length 0
a missing note is unknown: true
```

## 3. Transactions

Outside `gsql.begin` … `gsql.commit`, every statement is its own transaction. Inside, they all land or none do. A statement refused before it changed anything leaves the transaction usable.

<!--CODE:03_transactions-->

```basic
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

sql take_out on bank
    update acct set bal = bal - :amount where id = :from_id
end sql

sql pay_in on bank
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
take_out.exec()
pay_in.exec()
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
```

<!--OUT:03_transactions-->

```
after the batch: 100 accounts
after a rollback: 100.00 and 100.00
refused: DUPLICATE, and the transaction carries on
now 101 accounts
```

## 4. The `database` and `sql` blocks

Every recipe here is written with blocks; this one shows their details. A `:name` parameter is read from whatever variable of that name is in scope when the statement runs, including a function's own parameters. A block ends at `end sql`, so SQL's own `CASE … END` reads naturally, and `for each row in block` streams a result a batch at a time, closing its cursor if the loop is left early.

A string is for SQL assembled as the program runs, such as a table name it chose: `db.query(text, parameters)`, with every value bound as a parameter, never pasted in.

<!--CODE:04_database_and_sql_blocks-->

```basic
' Recipe 4: declare the database and its statements instead of passing strings.
load gsql

' A `database` block's file is a literal, found from the current directory.
' This recipe cleans up after itself, before and after.
if exists({dir}"cookbook_blocks.gsql") then
    remove_tree({dir}"cookbook_blocks.gsql")
end if

database shop( file: "cookbook_blocks.gsql", create: true )

sql make on shop
    create table item (id integer primary key, name text not null, price money(USD))
end sql

sql add on shop
    insert into item (id, name, price) values (:id, :name, :price)
end sql

' a block ends at `end sql`, so SQL's own CASE ... END reads naturally
sql priced on shop
    select name, price,
           case when price < :limit then 'cheap'
                else 'dear'
           end as band
    from item
    order by id
end sql

make.exec()

' :name parameters are read from the variables in scope when the block runs
function stock(id, name, price)
    add.exec()
    return nothing
end function
stock(1, "apple", {USD}"0.50")
stock(2, "melon", {USD}"3.00")
stock(3, "plum", {USD}"0.80")

limit = {USD}"1.00"
for each row in priced          ' a statement block is iterated a batch at a time
    print row.name + "  " + string(row.price) + "  " + row.band
next

shop.close()
remove_tree({dir}"cookbook_blocks.gsql")
```

<!--OUT:04_database_and_sql_blocks-->

```
apple  0.50  cheap
melon  3.00  dear
plum  0.80  cheap
```

## 5. Keys G-SQL makes

Three kinds:

- **`serial`** counts 1, 2, 3 with no gaps (a rollback or a refused row spends no number), and never reuses a number;
- **`serial of (...)`** counts separately for each value of its scope;
- **`random`** draws a key nobody can guess, of a declared length and alphabet.

<!--CODE:05_keys_serial_and_random-->

```basic
' Recipe 5: keys G-SQL makes for you -- counted, counted per scope, and random.
load gsql

if exists({dir}"cookbook_keys.gsql") then
    remove_tree({dir}"cookbook_keys.gsql")
end if

database keys( file: "cookbook_keys.gsql", create: true )

' a plain counter: 1, 2, 3, no gaps, and a number is never reused
' a block may hold several statements: it runs all of them or none
sql customers_in on keys
    create table customer (id integer serial primary key, name text);
    insert into customer (name) values ('Ada'), ('Grace'), ('Edsger');
    delete from customer where name = 'Edsger';
    insert into customer (name) values ('Barbara')
end sql

sql customers on keys
    select id, name from customer order by id
end sql

customers_in.exec()
for each row in customers
    print string(row.id) + " " + row.name             ' 3 is gone for good: Barbara is 4
next

' a counter per scope: each task's log lines are 1, 2, 3 ...
sql log_lines on keys
    create table task_log (task integer, n integer serial of (task), note text, primary key (task, n));
    insert into task_log (task, note) values (7, 'opened'), (8, 'opened'), (7, 'assigned'), (7, 'closed')
end sql

sql logged on keys
    select task, n, note from task_log order by task, n
end sql

log_lines.exec()
for each row in logged
    print "task " + string(row.task) + " #" + string(row.n) + " " + row.note
next

' per day of a datetime: the scope is any expression over the row
sql requests_in on keys
    create table request (at datetime, no integer serial of (cast(at as date)));
    insert into request (at) values (cast('2026-11-01 09:00' as datetime)),
                                    (cast('2026-11-01 17:30' as datetime)),
                                    (cast('2026-11-02 08:15' as datetime))
end sql

sql requests on keys
    select at, no from request order by at
end sql

requests_in.exec()
for each row in requests
    print string(row.at) + "  #" + string(row.no)
next

' random: a key nobody can guess, of a declared length and alphabet
sql members_in on keys
    create table member (id text random primary key, pin text random(6, '0123456789'), name text);
    insert into member (name) values ('Ada'), ('Grace')
end sql

sql members on keys
    select id, pin, name from member order by name
end sql

members_in.exec()
for each row in members
    ' the values differ every run, so this prints their shape, not them
    print row.name + ": id of " + string(len(row.id)) + " characters, pin of " + string(len(row.pin)) + " digits"
next

keys.close()
remove_tree({dir}"cookbook_keys.gsql")
```

<!--OUT:05_keys_serial_and_random-->

```
1 Ada
2 Grace
4 Barbara
task 7 #1 opened
task 7 #2 assigned
task 7 #3 closed
task 8 #1 opened
2026-11-01 09:00:00  #1
2026-11-01 17:30:00  #2
2026-11-02 08:15:00  #1
Ada: id of 22 characters, pin of 6 digits
Grace: id of 22 characters, pin of 6 digits
```

## 6. Letting the database refuse bad rows

Each rule refuses with its own code in `error.details.code`, so a program can tell a duplicate from a broken reference without reading English.

<!--CODE:06_constraints-->

```basic
' Recipe 6: let the database refuse bad rows -- NOT NULL, UNIQUE, CHECK and
' foreign keys -- and tell which rule said no by its code.
load gsql

if exists({dir}"cookbook_orders.gsql") then
    remove_tree({dir}"cookbook_orders.gsql")
end if

database shop( file: "cookbook_orders.gsql", create: true )

sql schema on shop
    create table customer (id integer primary key, email text not null unique);
    create table orders (id integer serial primary key,
                         customer integer references customer (id),
                         total money(USD) check (total > cast('0' as money(USD))));
    insert into customer values (1, 'ada@example.com')
end sql

sql good_order on shop
    insert into orders (customer, total) values (1, cast('25.00' as money(USD)))
end sql

sql no_email on shop
    insert into customer values (2, null)
end sql

sql same_email on shop
    insert into customer values (3, 'ada@example.com')
end sql

sql zero_total on shop
    insert into orders (customer, total) values (1, cast('0' as money(USD)))
end sql

sql no_such_customer on shop
    insert into orders (customer, total) values (9, cast('5' as money(USD)))
end sql

sql drop_ada on shop
    delete from customer where id = 1
end sql

' a number into money is converted (the column says which currency), but
' text into an integer is refused: a stored value takes its column's kind
' only where nothing is guessed
sql text_customer on shop
    insert into orders (customer, total) values ('1', 5)
end sql

sql order_count on shop
    select count(*) as n from orders
end sql

schema.exec()

function try(statement)
    on error goto next
    statement.exec()
    if error then
        print "refused  " + error.details.code
        return nothing
    end if
    print "accepted"
    return nothing
end function

try(good_order)
try(no_email)               ' NOT_NULL
try(same_email)             ' DUPLICATE
try(zero_total)             ' CHECK
try(no_such_customer)       ' FOREIGN_KEY: no customer 9
try(drop_ada)               ' FOREIGN_KEY: an order points at it
try(text_customer)          ' TYPE: text is not an integer
print string(order_count.query()[0].n) + " order kept"

shop.close()
remove_tree({dir}"cookbook_orders.gsql")
```

<!--OUT:06_constraints-->

```
accepted
refused  NOT_NULL
refused  DUPLICATE
refused  CHECK
refused  FOREIGN_KEY
refused  FOREIGN_KEY
refused  TYPE
1 order kept
```

## 7. Asking questions

Joins, grouping with `HAVING`, `DISTINCT`, correlated subqueries and `WITH`.

Every operator is gBASIC's own, so `sum` of money stays money and keeps its currency. An outer join's missing rows sum to `NULL`, which `coalesce` turns into zero.

<!--CODE:07_asking_questions-->

```basic
' Recipe 7: the queries a report is made of -- joins, grouping, DISTINCT,
' subqueries and WITH.
load gsql

if exists({dir}"cookbook_sales.gsql") then
    remove_tree({dir}"cookbook_sales.gsql")
end if

database sales( file: "cookbook_sales.gsql", create: true )

sql setup on sales
    create table region (id integer primary key, name text);
    create table sale (id integer primary key, region integer, rep text, amount money(USD));
    insert into region values (1, 'North'), (2, 'South'), (3, 'West');
    insert into sale values
        (1, 1, 'kim', cast('120' as money(USD))), (2, 1, 'lee', cast('80' as money(USD))),
        (3, 2, 'kim', cast('200' as money(USD))), (4, 2, 'max', cast('50' as money(USD))),
        (5, 1, 'kim', cast('30' as money(USD)))
end sql

sql by_region on sales
    select r.name, count(s.id) as sales, coalesce(sum(s.amount), cast('0' as money(USD))) as total
    from region r left join sale s on s.region = r.id
    group by r.name
    order by r.name
end sql

sql roaming_reps on sales
    select rep from sale group by rep having count(distinct region) > 1
end sql

sql above_average on sales
    select id, rep, amount from sale s
    where amount > (select avg(amount) from sale t where t.region = s.region)
    order by id
end sql

sql above_average_in_steps on sales
    with avgs as (select region, avg(amount) as mean from sale group by region)
    select s.id from sale s join avgs a on a.region = s.region
    where s.amount > a.mean
    order by s.id
end sql

setup.exec()

print "-- totals by region, joined to its name, regions with nothing sold included"
for each row in by_region
    print row.name + "  " + string(row.sales) + "  " + string(row.total)
next

print "-- reps who sold in more than one region"
for each row in roaming_reps
    print row.rep
next

print "-- each sale above its region's average, by a correlated subquery"
for each row in above_average
    print string(row.id) + " " + row.rep + " " + string(row.amount)
next

print "-- the same question asked in steps, with WITH"
for each row in above_average_in_steps
    print row.id
next

sales.close()
remove_tree({dir}"cookbook_sales.gsql")
```

<!--OUT:07_asking_questions-->

```
-- totals by region, joined to its name, regions with nothing sold included
North  3  230.00
South  2  250.00
West  0  0.00
-- reps who sold in more than one region
kim
-- each sale above its region's average, by a correlated subquery
1 kim 120.00
2 lee 80.00
3 kim 200.00
-- the same question asked in steps, with WITH
1
2
3
```

## 8. Views, and changing the schema under them

A schema change is never blocked. What it did to a view is warning 2118, which `on warning goto next` reads. Under `on warning stop` inside a transaction, it becomes an error before anything is committed, so a careful migration rolls back.

<!--CODE:08_views_and_changing_the_schema-->

```basic
' Recipe 8: views, and changing a table under them. A change is never
' blocked; what it did to a view is a warning, which a careful program can
' turn into an error and roll back.
load gsql

if exists({dir}"cookbook_views.gsql") then
    remove_tree({dir}"cookbook_views.gsql")
end if

database shop( file: "cookbook_views.gsql", create: true )

sql setup on shop
    create table item (id integer primary key, name text, price money(USD));
    insert into item values (1, 'apple', cast('0.50' as money(USD))), (2, 'melon', cast('3.00' as money(USD)));
    create view cheap as select name from item where price < cast('1' as money(USD))
end sql

sql read_cheap on shop
    select * from cheap
end sql

' rebuild the table differently: build the new one, copy, drop, rename
sql build_new on shop
    create table item_new (id integer primary key, name text, price money(USD), stocked boolean default true);
    insert into item_new (id, name, price) select id, name, price from item
end sql

sql drop_item on shop
    drop table item
end sql

sql rename_new on shop
    alter table item_new rename to item
end sql

setup.exec()
print read_cheap.query()

build_new.exec()

function drop_old()
    on warning goto next
    drop_item.exec()
    if warning then
        print "warned " + string(warning.code) + ": " + warning.message
    end if
    return nothing
end function
drop_old()                                        ' the view now reads nothing
rename_new.exec()
print read_cheap.query()                      ' and reads again, once item is back

' the careful version: under `on warning stop`, inside a transaction, the
' warning is an error before anything commits
function careful()
    on error goto next
    on warning stop
    shop.begin()
    drop_item.exec()
    if error then
        shop.rollback()
        return "rolled back: " + string(error.code)
    end if
    shop.commit()
    return "committed"
end function
print careful()
print read_cheap.query()

shop.close()
remove_tree({dir}"cookbook_views.gsql")
```

<!--OUT:08_views_and_changing_the_schema-->

```
[{"name":"apple"}]
warned 2118: view `cheap` cannot run: there is no table `item`
[{"name":"apple"}]
rolled back: 2118
[{"name":"apple"}]
```

## 9. Indexes and `EXPLAIN`

`EXPLAIN` names the path a query takes: a point read, an index seek, or a scan. An index only narrows: every condition is still checked on every row it yields, so a wrong index is slow, never wrong.

<!--CODE:09_indexes_and_explain-->

```basic
' Recipe 9: an index makes a lookup cost the size of the answer, not of the
' table. EXPLAIN says which path a query takes.
load gsql

if exists({dir}"cookbook_log.gsql") then
    remove_tree({dir}"cookbook_log.gsql")
end if

database log( file: "cookbook_log.gsql", create: true )

sql make_event on log
    create table event (id integer primary key, user_id integer, kind text, at date)
end sql

sql add_event on log
    insert into event values (:id, :user_id, :kind, :at)
end sql

sql index_user on log
    create index event_user on event (user_id)
end sql

sql by_id on log
    explain select * from event where id = 42
end sql

sql by_user on log
    explain select * from event where user_id = 7
end sql

sql by_user_and_kind on log
    explain select * from event where user_id = 7 and kind = 'click'
end sql

sql user_count on log
    select count(*) as n from event where user_id = 7
end sql

make_event.exec()
log.begin()
kind = "click"
at = {date}"2026-11-01"
for i = 1 to 2000
    id = i
    user_id = mod(i, 50)
    add_event.exec()
next
log.commit()

function plan(explained)
    for each row in explained
        print "    " + row.plan
    next
    return nothing
end function

print "by the primary key:"
plan(by_id)
print "by a column with no index -- every row is read:"
plan(by_user)
index_user.exec()
print "the same, with an index on it:"
plan(by_user)
print "an index narrows; every condition is still checked:"
plan(by_user_and_kind)
print string(user_count.query()[0].n) + " events for user 7"

log.close()
remove_tree({dir}"cookbook_log.gsql")
```

<!--OUT:09_indexes_and_explain-->

```
by the primary key:
    Point Read on event using the primary key (id = 42)
by a column with no index -- every row is read:
    Scan on event
      Filter: (user_id = 7)
the same, with an index on it:
    Index Seek on event using event_user (user_id = 7)
an index narrows; every condition is still checked:
    Index Seek on event using event_user (user_id = 7)
      Filter: (kind = 'click')
40 events for user 7
```

## 10. When SQL says no

A refusal is an ordinary gBASIC error, with G-SQL's code and, where there is one, a position in the statement. This recipe uses strings, because it sends broken SQL on purpose: in a block, a syntax error is refused when the program loads, before anything runs.

One thing to know: a comparison is checked when a row reaches it. A query that compares text with a number answers nothing over an empty table, and is refused once a row is there.

<!--CODE:10_when_sql_says_no-->

```basic
' Recipe 10: a refusal is an ordinary gBASIC error, with G-SQL's code by name
' and, where the statement has a place to point at, its position.
'
' This recipe sends deliberately broken SQL, so it uses strings: in an `sql`
' block a syntax error is refused when the program LOADS, at its line and
' column, before anything runs -- which is why blocks are the normal form, and
' why they cannot show a syntax error being caught at run time.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "e.gsql"), { create: true })
db.exec("create table t (id integer primary key, name text)")

function explain_error(sql)
    on error goto next
    db.query(sql)
    if error then
        where = ""
        if has(error.details, "position") then
            where = " at character " + string(error.details.position)
        end if
        print error.details.code + where + ": " + sql
    end if
    return nothing
end function

explain_error("select nme from t")             ' NOT_FOUND: no column nme
explain_error("selct * from t")                ' SYNTAX

' A comparison is checked when a row reaches it. Over an empty table nothing
' reaches it, so a query that will be refused later answers nothing now:
explain_error("select * from t where name = 1")
print "  (said nothing: the table is empty)"
db.exec("insert into t values (1, 'one')")
explain_error("select * from t where name = 1")   ' EXPRESSION: no implicit casts
explain_error("select * from t where name = '1'") ' fine: says nothing

' and a query of a statement that returns nothing: gsql.query is for rows,
' so it is refused -- saying that the statement ran, since it did.
' (gsql.exec runs anything, and answers rows too when there are any.)
function wrong_verb()
    on error goto next
    db.query("update t set name = 'uno' where id = 1")
    if error then
        print error.details.code + ": " + error.message
    end if
    return nothing
end function
wrong_verb()

db.close()
remove_tree(work)
```

<!--OUT:10_when_sql_says_no-->

```
NOT_FOUND at character 8: select nme from t
SYNTAX at character 1: selct * from t
  (said nothing: the table is empty)
EXPRESSION at character 28: select * from t where name = 1
STATE: gsql.query: the statement returns no rows (it ran, and affected 1); use gsql.exec for it [STATE] -- in "update t set name = 'uno' where id = 1"
```

---

# Part 2: practices

These follow from how G-SQL is built, and the figures behind them are in `docs/gsql_practices.md`. That page also lists the **current limits**, dated and each naming the work that removes it. Read it before designing for large tables.

## 11. One database per tenant, and combining their answers

When tenants are independent, give each a database. The SQL is the same for all of them, so it is written once, on a database chosen at run time: `database tenant()`, with no file, and each statement given the tenant's connection, `db.exec(bill)` or `for each row in db.on(totals)`. Each database stays small and fast. It is backed up, moved or deleted as a whole. And one tenant's long report never stalls another's work, since each database has its own owner.

**Across databases, combine parts, not answers.** `count`, `sum`, `min` and `max` merge directly. An average is the sums over the counts: averaging the averages is wrong whenever the databases hold different numbers of rows.

<!--CODE:11_practice_a_database_per_tenant-->

```basic
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
```

<!--OUT:11_practice_a_database_per_tenant-->

```
acme: 3 invoices, average 20.00
globex: 1 invoices, average 300.00
the right average across tenants:   90.00
the average of the averages (wrong): 160.00
```

## 12. A write that is safe to retry

**No transaction spans two databases**, and a program does not always know whether its last write landed. Give the write a request id in a `UNIQUE` column, and a retry is refused as a duplicate rather than applied twice.

<!--CODE:12_practice_a_retry_that_is_safe-->

```basic
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

make.exec()

' :request and :amount are this function's own parameters
function pay(request, amount)
    on error goto next
    record_payment.exec()
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
r = paid_in_all.query()[0]
print string(r.n) + " payments, " + string(r.s) + " in all"

pay_db.close()
remove_tree({dir}"cookbook_pay.gsql")
```

<!--OUT:12_practice_a_retry_that_is_safe-->

```
paid
already done
paid
2 payments, 65.00 in all
```

## 13. Load in batches

**A commit costs one sync to disk,** and on slower storage the sync is most of the cost. Group rows into transactions of a few thousand.

Create indexes before or after loading, whichever suits: building one over rows already there sorts its entries first and grows linearly with the table (`docs/gsql_spill_design.md`).

<!--CODE:13_practice_load_in_batches-->

```basic
' Recipe 13 (a practice): load in batches. Each commit costs one sync to disk,
' and on slower storage the sync is most of the cost: measured at 10 ms a sync,
' one row per commit wrote 93 rows a second, and 1 000 rows per commit 67 739
' (docs/gsql_scale_measurements.md). So group rows into transactions of a few
' thousand -- big enough to share the sync, small enough to stay light.
load gsql

if exists({dir}"cookbook_load.gsql") then
    remove_tree({dir}"cookbook_load.gsql")
end if

database readings( file: "cookbook_load.gsql", create: true )

sql make on readings
    create table reading (id integer primary key, sensor integer, value number);
    create index reading_sensor on reading (sensor)
end sql

sql add on readings
    insert into reading values (:id, :sensor, :value)
end sql

sql how_many on readings
    select count(*) as n from reading
end sql

sql from_sensor_3 on readings
    select count(*) as n from reading where sensor = 3
end sql

make.exec()

batch = 1000
commits = 0
readings.begin()
for i = 1 to 2500
    id = i
    sensor = mod(i, 7)
    value = i / 4
    add.exec()
    if mod(i, batch) = 0 then
        readings.commit()
        commits = commits + 1
        readings.begin()
    end if
next
readings.commit()           ' the last, partial batch
commits = commits + 1

r = how_many.query()[0]
print string(r.n) + " rows in " + string(commits) + " commits"
print string(from_sensor_3.query()[0].n) + " readings from sensor 3"

readings.close()
remove_tree({dir}"cookbook_load.gsql")
```

<!--OUT:13_practice_load_in_batches-->

```
2500 rows in 3 commits
357 readings from sensor 3
```

## 14. Back up while in use, and report on the copy

`db.backup(dir)` makes a consistent copy while other programs go on working. It copies a slice at a time, and the owner serves everyone else between slices. Backing up into the same directory again **refreshes** it: files the copy already holds are kept, so the cost is roughly what changed.

**The copy is an ordinary database,** which makes it the place for long reports: each database has its own owner, so a report there stalls nobody on the live one.

**A copy that is open is not refreshed.** Open a copy you mean to refresh with `{ linger: 0 }`, so its owner stops when you close it, and the refresh can follow at once.

<!--CODE:14_practice_backup_and_a_reporting_copy-->

```basic
' Recipe 14 (a practice): back up while the database is in use, and run long
' reports on the copy. A copy is a consistent snapshot, made a slice at a time
' while other programs go on reading and writing; backing up into the same
' place again refreshes it, keeping the files it already holds.
load gsql

if exists({dir}"cookbook_live.gsql") then
    remove_tree({dir}"cookbook_live.gsql")
end if

database live( file: "cookbook_live.gsql", create: true )
' the copy is wherever the backup put it, so its statements run on a
' connection made at run time
database reporting()

sql make on live
    create table sale (id integer serial primary key, amount money(USD))
end sql

sql sell on live
    insert into sale (amount) values (:amount)
end sql

sql sales_total on reporting
    select count(*) as n, sum(amount) as total from sale
end sql

make.exec()
live.begin()
amount = {USD}"10.00"
for i = 1 to 1000
    sell.exec()
next
live.commit()

work = temp_dir()
copy = join_path(work, "reporting.gsql")
first = live.backup(copy)
print "first backup: " + string(first.files) + " file, nothing kept: " + string(first.reused = 0)

amount = {USD}"99.00"
sell.exec()
live.backup(copy)                  ' a refresh: as of now

' The report runs on the copy, so a long one stalls nobody on the live
' database. (This program now holds the copy open, and a copy that is open is
' not refreshed: a report runs as a program of its own, and its hold ends with
' it.)
report = gsql.connect(copy)
r = report.query(sales_total)[0]
print "the copy: " + string(r.n) + " sales, " + string(r.total)

on error goto next
live.backup(copy)
if error then
    print "refreshing it while it is open is refused: " + error.details.code
end if

report.close()
live.close()
remove_tree(work)
remove_tree({dir}"cookbook_live.gsql")
```

<!--OUT:14_practice_backup_and_a_reporting_copy-->

```
first backup: 1 file, nothing kept: true
the copy: 1001 sales, 10099.00
refreshing it while it is open is refused: STATE
```

## 15. A SQL dump, to read a design and to upgrade it

`db.dump(file)` writes the database as SQL text: its tables, indexes, rows, serial counters, constraints and views, from one snapshot, a slice at a time while other programs go on working. With `{ schema_only: true }` it writes the design alone, which is the database's design on one page. `db.restore(file)` runs a dump into another database.

**Edit the text, then restore it, to change a design** in a way `ALTER TABLE` does not offer: here a column becomes `NOT NULL` with a default, and the one row that broke the new rule is fixed in the same pass.

**Every value reads back as itself:** money keeps its sub-cent digits and its currency, an exact integer stays exact, a time of day keeps its precision. **A serial carries on** from where the old database left off, even past rows that were deleted.

**A restore only writes to tables it made,** so a file, edited or not, cannot touch data that was already there.

<!--CODE:15_practice_dump_to_read_and_upgrade_a_design-->

```basic
' Recipe 15 (a practice): a SQL dump, to read a database's design and to
' upgrade it. A dump is text a person can read and edit, and gsql.restore
' runs it into another database: the schema-only dump is the design on one
' page, and editing a full dump before restoring it is how a design changes
' in a way no ALTER TABLE offers.
load gsql

for each f in ["cookbook_dump.gsql", "cookbook_dump_v2.gsql"]
    if exists({dir}f) then
        remove_tree({dir}f)
    end if
next

database shop( file: "cookbook_dump.gsql", create: true )
database v2( file: "cookbook_dump_v2.gsql", create: true )

sql make on shop
    create table customer (id integer serial primary key, name text not null, phone text);
    insert into customer (name, phone) values ('Ada', '555-0100'), ('Grace', null)
end sql

sql customers on v2
    select id, name, phone from customer order by id
end sql

sql add_linus on v2
    insert into customer (name) values ('Linus')
end sql

sql highest on v2
    select max(id) as m from customer
end sql

make.exec()

' the design, on one page
work = temp_dir()
schema = join_path(work, "shop-schema.sql")
shop.dump(schema, { schema_only: true })
print read({file}schema)

' everything, then the design changed in the text: phone becomes NOT NULL
' with a default, which existing rows must satisfy, so the dump's NULL is
' filled in the same pass. The dump is text, edited as text: this is where a
' string, not a block, is the right tool.
full = join_path(work, "shop.sql")
r = shop.dump(full)
print "dumped " + string(r.tables) + " table, " + string(r.rows) + " rows"
text = read({file}full)
text = replace(text, "phone TEXT", "phone TEXT NOT NULL DEFAULT 'unlisted'")
text = replace(text, "'Grace', NULL", "'Grace', 'unlisted'")
upgraded = join_path(work, "shop-v2.sql")
write({file}upgraded, text)

r = v2.restore(upgraded)
print "restored " + string(r.statements) + " statements, " + string(r.rows) + " rows"
for each c in customers
    print string(c.id) + " " + c.name + " " + c.phone
next

' the serial carries on from where the old database left off
add_linus.exec()
print "next id: " + string(highest.query()[0].m)

' a restore only writes to tables it made, so a file cannot touch data
' already there
on error goto next
write({file}upgraded, "delete from customer;")
v2.restore(upgraded)
if error then
    print "a DELETE in the file is refused: " + error.details.code
end if

v2.close()
shop.close()
remove_tree(work)
remove_tree({dir}"cookbook_dump.gsql")
remove_tree({dir}"cookbook_dump_v2.gsql")
```

<!--OUT:15_practice_dump_to_read_and_upgrade_a_design-->

```
-- G-SQL dump, format 1, schema only (docs/gsql_dump_design.md)

CREATE TABLE customer (
    id INTEGER SERIAL,
    name TEXT NOT NULL,
    phone TEXT,
    PRIMARY KEY (id)
);


dumped 1 table, 2 rows
restored 3 statements, 2 rows
1 Ada 555-0100
2 Grace unlisted
next id: 3
a DELETE in the file is refused: STATE
```

---

# Part 3: SQL for reports

## 16. Window functions: a running balance, the latest per key, a change, a rank

An aggregate folds its rows into one answer; a **window function** answers for every row, from the rows around it. `OVER (PARTITION BY account ORDER BY posted)` names those rows: the row's own account, in date order, up to and including the row. So `sum(amount) OVER (...)` is a running balance, and `lag(amount) OVER (...)` is the amount on the row before.

**The latest row per key** is the most common use: number each key's rows newest first with `row_number()`, and keep row 1. A window is worked out after `WHERE`, so it cannot be filtered there; number the rows inside a derived table and filter outside it.

Windows over a grouped query see the groups, so `rank() OVER (ORDER BY sum(amount) DESC)` places each account by its total. `docs/gsql_window_design.md` lists every function and frame.

<!--CODE:16_window_functions-->

```basic
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
```

<!--OUT:16_window_functions-->

```
Running balances:
  card  2026-10-02  -45.50  balance -45.50
  card  2026-10-05  -12.25  balance -57.75
  card  2026-10-09  57.75  balance 0.00
  cash  2026-10-01  500.00  balance 500.00
  cash  2026-10-03  -120.00  balance 380.00
  cash  2026-10-03  -30.00  balance 350.00
  cash  2026-10-07  200.00  balance 550.00
  savings  2026-10-04  1000.00  balance 1000.00
Latest per account:
  card  2026-10-09  57.75
  cash  2026-10-07  200.00
  savings  2026-10-04  1000.00
Card, change since the row before:
  2026-10-02  -45.50  change unknown
  2026-10-05  -12.25  change 33.25
  2026-10-09  57.75  change 70.00
Accounts by total:
  1. savings  1000.00
  2. cash  550.00
  3. card  0.00
```

## 17. Recursive queries: a bill of materials, and a cycle caught

A hierarchy of unknown depth, such as parts within parts, managers over managers or accounts rolling up, is one `WITH RECURSIVE` query. Its **anchor** is where the walk starts, and its **step**, after `UNION ALL`, goes one level further each round, reading the rows the round before produced. Here the step multiplies the quantities down, so the query that reads it totals every part in a robot arm.

**A cycle in the data,** a part listed inside itself, would make the walk go round forever. `CYCLE part SET looped USING route` watches the part: a row whose part is already on its route is answered, marked `looped`, and goes no further, so the walk ends and the loop can be reported with the route that found it. Without `CYCLE`, G-SQL refuses a recursive query still adding rows after a million rounds, or holding more than 1 GiB, rather than run until memory gives out; a program whose recursion is meant to run longer sets the database's limit, as `plant.max_recursion = 5000000`. `docs/gsql_recursive_design.md` has the rules.

<!--CODE:17_recursive_queries-->

```basic
' Recipe 17: WITH RECURSIVE -- walking a hierarchy of unknown depth: a bill
' of materials exploded into total parts, and a cycle in the data caught and
' reported rather than run forever.
load gsql

if exists({dir}"cookbook_recursive.gsql") then
    remove_tree({dir}"cookbook_recursive.gsql")
end if

database plant( file: "cookbook_recursive.gsql", create: true )

sql make on plant
    create table bom (id integer primary key, assembly text, component text, qty integer);
    insert into bom values
        (1, 'robot-arm', 'joint', 3), (2, 'robot-arm', 'gripper', 1),
        (3, 'joint', 'motor', 1), (4, 'joint', 'bolt', 6),
        (5, 'gripper', 'motor', 2), (6, 'gripper', 'finger', 3),
        (7, 'finger', 'bolt', 2)
end sql

' every part of a robot arm and how many of each: the anchor is the arm's own
' components, and each round goes one level down, multiplying the quantities
sql explode on plant
    with recursive parts (part, qty, depth) as (
        select component, qty, 1 from bom where assembly = 'robot-arm'
        union all
        select b.component, p.qty * b.qty, p.depth + 1
        from parts p join bom b on b.assembly = p.part
    )
    select part, sum(qty) as total, max(depth) as deepest
    from parts
    group by part
    order by part
end sql

' a mistake in the data: a finger listed as made of a gripper
sql mistake on plant
    insert into bom values (8, 'finger', 'gripper', 1)
end sql

' CYCLE watches the part: a row whose part is already on its route is marked
' and goes no further, so the walk ends and the loop can be reported
sql loops on plant
    with recursive parts (part, depth) as (
        select component, 1 from bom where assembly = 'robot-arm'
        union all
        select b.component, p.depth + 1
        from parts p join bom b on b.assembly = p.part
    ) cycle part set looped using route
    select part, route from parts where looped
end sql

make.exec()

print "Parts in one robot arm:"
for each r in explode
    print "  " + r.part + "  x" + string(r.total) + "  (down to level " + string(r.deepest) + ")"
next

mistake.exec()
print "Loops in the bill of materials:"
for each r in loops
    print "  " + r.part + " is inside itself: " + join(r.route, " > ")
next

plant.close()
remove_tree({dir}"cookbook_recursive.gsql")
```

<!--OUT:17_recursive_queries-->

```
Parts in one robot arm:
  bolt  x24  (down to level 3)
  finger  x3  (down to level 2)
  gripper  x1  (down to level 1)
  joint  x3  (down to level 1)
  motor  x5  (down to level 2)
Loops in the bill of materials:
  gripper is inside itself: gripper > finger > gripper
```

## 18. Subtotals with ROLLUP

`GROUP BY ROLLUP(region, product)` groups three ways in one query: by region and product, by region alone, and not at all. So it answers the detail rows, each region's subtotal and the grand total, and with no `ORDER BY` they come in the order a report reads them, each region's rows followed by its subtotal and the grand total last.

In a total, a column it leaves out reads NULL. `GROUPING(region, product)` says which rows are totals, one bit per column, so a NULL in the data is never mistaken for a subtotal: 0 on a detail row, 1 on a region's subtotal and 3 on the grand total. `CUBE(...)` groups by every combination, and `GROUPING SETS (...)` by the ones listed (`docs/gsql_rollup_design.md`).

<!--CODE:18_subtotals_with_rollup-->

```basic
' Recipe 18: subtotals with ROLLUP -- one query answers each region's sales by
' product, each region's subtotal and the grand total, in the order a report
' reads them; GROUPING says which rows are totals.
load gsql

if exists({dir}"cookbook_rollup.gsql") then
    remove_tree({dir}"cookbook_rollup.gsql")
end if

database sales( file: "cookbook_rollup.gsql", create: true )

sql make on sales
    create table sale (id integer primary key, region text, product text, amount money(USD));
    insert into sale values
        (1, 'east', 'gripper', 1200.00), (2, 'east', 'joint', 450.00),
        (3, 'east', 'gripper', 800.00), (4, 'west', 'joint', 300.00),
        (5, 'west', 'motor', 975.50), (6, 'west', 'joint', 150.00)
end sql

' GROUP BY ROLLUP(region, product) groups by both, then by region alone, then
' not at all; a column a total leaves out reads NULL, and GROUPING(region,
' product) is 0 on a detail row, 1 on a region's subtotal and 3 on the total
sql report on sales
    select region, product, sum(amount) as total, count(*) as sales,
           grouping(region, product) as level
    from sale
    group by rollup(region, product)
end sql

make.exec()

for each r in report
    if r.level = 3 then
        print "Grand total            " + string(r.total) + "  (" + string(r.sales) + " sales)"
    else if r.level = 1 then
        print "  " + r.region + " subtotal        " + string(r.total)
    else
        print "  " + r.region + "  " + r.product + "  " + string(r.total)
    end if
next

sales.close()
remove_tree({dir}"cookbook_rollup.gsql")
```

<!--OUT:18_subtotals_with_rollup-->

```
  east  gripper  2000.00
  east  joint  450.00
  east subtotal        2450.00
  west  joint  450.00
  west  motor  975.50
  west subtotal        1425.50
Grand total            3875.50  (6 sales)
```


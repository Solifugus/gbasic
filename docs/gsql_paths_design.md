# Paths over G-SQL: reading and writing tables by path

**Status:** design note, 2026-11-16, revised the same day with Matthew's rulings on its first draft (§10). **Built, increments 1 to 5** (one table read by path; steps between tables across foreign keys; writing by path; columns in braces; the cookbook's Part 4, recipes 19 and 20; §8.1 to §8.4 record where the build departed from this text).

## 1. What it is, and what it is not

Matthew, 2026-11-14:

> Not to replace SQL but to supplement it. SQL makes most sense for data analytics, ETL, integrations, and migrations. The paths approach is, however, more practical for applications. The two could work in harmony ... They would talk to the same data -- the G-SQL data store. We are only talking about reading/writing data via paths. G-SQL would still be used to create tables and relationships.

**Plain data has the braces too** since 2026-11-20 (`docs/reference.md`,
*Braces -- several fields at once*): one spelling for a path's columns and a
record's fields, flat across steps as §3.5's join is. **And the brackets**
(*Brackets -- the rows a condition holds for*): `rows[.paid]` filters a plain
array as `[.paid]` filters a table, with any gBASIC expression allowed, since
nothing has to become SQL.

The spelling is borrowed from AmorphDB's MBL (`~/development/amorphdb/docs/mbl_reference.md`): dots walk, brackets filter, braces choose. Only the spelling comes over. **Not** AmorphDB's tree of attributes, **not** its history (`[@date]`), stamps or permissions, **not** schema by path. A path reads and writes ordinary G-SQL tables, and the steps between tables are the foreign keys SQL declared.

**The dots name tables, always and only** (Matthew, 2026-11-16: *"AmorphDB is not tabular data"*). Columns are named in braces at the end, `{ name, email }`, or inside brackets with a leading dot, `[.city = "Rome"]`. A path with no braces reads every column.

```basic
load gsql
database shop( file: "shop.gsql" )

c = shop.customer[3]                                ' one row, a record, or unknown
print c.email
for each r in shop.customer[.city = "Rome"]{ name, email }
    print r.name, r.email
next
for each inv in shop.customer[3].invoice            ' the invoices that reference customer 3
    print inv.issued, inv.total
next
print (shop.invoice[17].customer{ name }).name       ' across the foreign key to its parent

shop.customer[3] = { email: "ana@example.com" }      ' updates customer 3's email
row = shop.customer.insert({ name: "Bo", city: "Oslo" })   ' the row as stored, its SERIAL key included
shop.customer[3].invoice.insert({ issued: today() })        ' customer filled in from the path
shop.invoice[17].delete()
print shop.customer[3].invoice.sql()                ' the SQL that path is
```

The schema the examples use, made by SQL as always:

```sql
create table customer (id integer serial primary key, name text not null, email text, city text);
create table invoice  (id integer serial primary key,
                       customer integer references customer,
                       issued date, total money(USD), paid money(USD));
create table line     (invoice integer references invoice on delete cascade,
                       n integer serial of (invoice), item text, amount money(USD),
                       primary key (invoice, n));
```

## 2. The rule everything else follows from: a path is one SQL statement

**A path compiles to one G-SQL statement and runs as one request**, by the protocol every other call uses (`gsql_design.md` §6: one execution path, never a second). So it has **no semantics of its own**:

- storing converts as SQL stores (`gsql_store_conversion_design.md`), and `NOT NULL`, defaults, `CHECK`, `UNIQUE` and foreign keys apply as they do to SQL's `INSERT` and `UPDATE`;
- `unknown` is SQL's `NULL`; a refusal is a G-SQL code in `error.details.code` (`NOT_FOUND`, `FOREIGN_KEY`, `DUPLICATE`, ...);
- a write inside `shop.begin()` ... `commit()` is part of that transaction, and in autocommit is its own;
- a path walking three tables is **one** statement over one snapshot, never a read per step. `shop.customer[.city = "Rome"].invoice` is a semi-join, not a loop over customers, so the N+1 queries an ORM is known for cannot happen by construction;
- `.sql()` at the end of any path answers the statement it is, without running it (§3.4), so a programmer can always see what runs and move to a `sql` block when a path is not enough.

That is what "in harmony" comes to: a path is a SQL statement written shorter, over the same tables, and a program can mix the two freely, even within one transaction.

**Where it compiles.** In the owner, against the catalog, since only the catalog knows which foreign key joins two tables. The client sends the path as a request (a serialized record of steps, with each program expression already evaluated and sent as a parameter), and the owner resolves the names, builds its SQL tree and runs it as it would have run the text. The SQL is never assembled as text, so nothing in a value can become SQL.

## 3. Reading

### 3.1 The steps

| step | written | means |
|---|---|---|
| root | `shop` | a connection: a `database` block, or `gsql.connect(...)`'s answer (§10 A9) |
| table | `shop.customer` | every row of the table |
| key | `shop.customer[3]`, `shop.customer[n + 1]` | the row whose primary key is that value |
| filter | `shop.customer[.city = "Rome", is_unknown(.email)]` | the rows matching; `,` is `and`, `or` is written; `(...)` groups |
| step | `shop.invoice[17].customer`, `shop.customer[3].invoice` | to another table across the one foreign key joining the two, in whichever direction it points |
| columns | `...{ name, email }` | only these columns of the table just before it, as fields in this order; at the end of the path, or after any table along it (§3.5) |

**Inside a bracket, a column is written with a leading dot and everything else is an ordinary gBASIC expression** (§10 A3), evaluated where the path is written and sent as a parameter: `[.city = wanted]`, `[.total > limit * 2]`, `[.paid < .total]`. (As built, a column may be compared, combined, used in arithmetic and tested with `is_unknown()`, but not handed to a function: §8.1.) The dot also shows the column belongs to the table to the left of the bracket.

**A bracket with no column in it is a key**: `[3]`, `[n]`, and for a primary key of several columns their values in the key's order, `shop.line[17, 2]`. Part of a composite key is a filter, `shop.line[.invoice = 17]`, and so is a filter naming every key column, `shop.line[.invoice = 17, .n = 2]`: many, holding at most one row. (Built so; the first text made that filter a key, §8.1.)

**A step goes across a foreign key, by the other table's name.** The direction is the key's own: from `invoice` to `customer` is child to parent (each invoice's one customer), from `customer` to `invoice` is parent to children (each customer's invoices). Two tables joined by no foreign key, or by more than one, cannot be stepped between; §5 says what is written instead.

### 3.2 One or many

A path is **one** or **many**:

- a key is one; a step from child to parent, taken from one, is one;
- a table, a filter, and a step from parent to children are many; anything stepped from many is many.

| path ends at | one | many |
|---|---|---|
| a table, no braces | a record of every column (the fields a `query` row has, in table order), or **`unknown` when there is no such row** | a **result**, §3.3 |
| `{ columns }` | a record of those fields, or `unknown` | a result whose rows have those fields |

**Every column, when no braces are written anywhere in the path; only the braced columns when any are** (§3.5).

So one value is read through its record: `c = shop.customer[3]` and then `c.email`, or `(shop.customer[3]{ email }).email`. Without the parentheses, `.email` after the braces would be a step to a table called `email` (§3.5).

**A step from child to parent, taken from many, gives each parent once** (`shop.invoice[.issued > d].customer` is the customers with such an invoice, each once, as `where id in (select customer from invoice where ...)` would). A step from parent to children gives every child once.

### 3.3 A path of many is a result, as a `sql` block's select is (§10 A5, A7)

A path of many is **not read where it is written**. It answers a **result**: a handle like a `sql` block holding a `select`, which runs when it is used, and each time it is used.

```basic
rome = shop.customer[.city = "Rome"]       ' nothing has been read yet
for each c in rome                          ' runs now, a batch at a time
    print c.name
next
all = rome.query()                          ' runs again: an array of records
n = rome.count()                            ' select count(*) ... : a number
print rome[is_unknown(.email)].sql()       ' a result goes on being a path
ana = rome[1]                               ' ...including to one row by its key: read now
```

- **`for each` streams it** a batch at a time, so `for each c in shop.customer` never holds the table.
- **`.query()`** reads it whole into an array, as a `sql` block's `query()` does. **`.count()`** answers how many rows it would give.
- **It goes on being a path.** `t = shop.customer` then `t[3]` is customer 3, not the table's fourth row, and `t[.city = "Rome"].invoice` steps on from it. A result is a path not yet run, so nothing about it differs from writing the whole path out.
- **It runs when used, against the data as it is then.** `rome` above is not a snapshot; a row added to Rome between the two uses is in the second. That is a `sql` block's rule too, and a program wanting one snapshot reads `.query()` once, or uses a read transaction.
- **A path of one is read where it is written**, since one record is cheap and is what a program wants in hand.

**Order:** primary key order, always, so the same path over the same data gives the same rows (§10 A5). "The ten newest invoices" is a `sql` block.

### 3.4 Methods at the end of a path

A name followed by `(` is a method, never a table, so a table may be called `count` or `sql`.

| method | on | answers |
|---|---|---|
| `.query()` | many | an array of records |
| `.count()` | many | the number of rows |
| `.sql()` | any path, one or many | the statement as text, with its parameters shown as `$1`, ... and their values listed after it; nothing is run |
| `.insert(rec)`, `.insert([rec, ...])` | a table, or a step from parent to children | the rows as stored (§4) |
| `.delete()` | any path but a step from parent to children taken from many | the number of rows deleted (§4) |

`.sql()` replaces the first draft's `shop.sql_of(path)` (§10 A8). A function given a path would have had the path read before it saw it; a method at the end of the path is part of the path, so nothing is read.

```basic
print shop.customer[.city = "Rome"].invoice[.paid < .total].sql()
' select invoice.* from invoice
'  where invoice.customer in (select customer.id from customer where customer.city = $1)
'    and invoice.paid < invoice.total
'  order by invoice.id
' $1 = "Rome"
```

### 3.5 Columns from several tables: a join (§10 A10)

Braces may follow any table along a path, and each chooses that table's columns. The result is the join: one row per row of the last table, carrying the braced columns of every table before it.

```basic
for each r in shop.customer[3]{ id, name, phone }.purchases{ item, price }
    print r.name, r.item, r.price     ' { id, name, phone, item, price }, one per purchase
next
```

is

```sql
select customer.id, customer.name, customer.phone, purchases.item, purchases.price
  from customer join purchases on purchases.customer = customer.id
 where customer.id = $1
 order by purchases.id
```

- **An inner join.** A customer with no purchases gives no rows: the path asks for purchases.
- **A table with no braces gives no columns** once braces are written anywhere: it is passed through. `shop.customer[3].purchases{ item }` gives `item` alone. With no braces anywhere, the last table gives every column, as in §3.2.
- **The fields are in path order, then brace order.**
- **Two tables giving one field name are refused,** naming both: ``customer and purchases both give `id` -- leave one out``. A renaming spelling (`{ id as customer_id }`) is left until a program asks for it.
- **One or many is the path's**, by §3.2: here many, since the step is from parent to children.
- **A step from child to parent keeps one row per child**, carrying its parent's columns: `shop.invoice[.paid < .total]{ id, total }.customer{ name }` is each unpaid invoice with its customer's name. This is the one place where "each parent once" (§3.2) gives way, since the braces before the step ask for the invoices' rows.

Writes through such a path are refused; a join has no one table to write to.

### 3.6 A miss is not an error, on reading

`shop.customer[99]` is `unknown`, and so is `shop.customer[99]{ email }`. That is G-SQL's `NULL` and gBASIC's three-valued rule: `if (shop.customer[99]{ email }).email = "x"` refuses unknown as `if` always does, which is what makes a miss loud where it matters. An unknown **name** is refused at once (§5).

## 4. Writing

A write assigns a **record** to a path: the record's fields are the columns written. There is no writing to one column alone, since a column is not a step (§10 A1); `{ email: v }` is the record of one field.

| write | means | when nothing matches |
|---|---|---|
| `shop.customer[3] = { email: v }` | the row is there: `update customer set email = $1 where id = 3` | **SQL's `INSERT` rules apply** (§10 A2): `insert into customer (id, email) values (3, $1)`, refused as that insert would be, here `NOT_NULL` for `name` |
| `shop.customer[.city = "Rome"] = { active: false }` | `update customer set active = false where city = $1` | nothing changes, as SQL's `UPDATE` (§10 A4) |
| `shop.customer.insert(rec)` | `insert ... returning *`; answers the row as stored, its `SERIAL`, `RANDOM` and default columns filled in | — |
| `shop.customer.insert([rec, rec])` | one `insert` of several rows, all or none; answers the rows | — |
| `shop.customer[3].invoice.insert(rec)` | `insert into invoice`, with `customer = 3` filled in by the step | refused, `FOREIGN_KEY`, if customer 3 is not there; refused if `rec` gives `customer` another value |
| `shop.invoice[17].delete()` | `delete from invoice where id = 17`, cascading as declared | answers `0` (§10 A4) |
| `shop.invoice[.paid >= .total].delete()` | `delete ... where paid >= total` | answers the count |
| `shop.invoice[17].customer = { email: v }` | `update customer set email = $1 where id = (select customer from invoice where id = 17)` | refused, `NOT_FOUND`: there is no row to insert into a parent named only by its child |

- **A key's record may not change the key.** `shop.customer[3] = { id: 4 }` is refused; a key is changed by a `sql` block, where the reader sees it.
- **Columns in braces are not written through**: `shop.customer[3]{ name } = ...` is refused, since it says nothing the record form does not.
- **A step from parent to children, taken from many, inserts nothing**: `shop.customer[.city = "Rome"].invoice.insert(...)` is refused, since it would not say which customer.

*§10 A2, confirmed:* "follow the normal INSERT rules" means: a key path naming a row that is not there inserts it, by every rule an `INSERT` has, and one naming a row that is there updates the fields given. The other reading, that `=` is always an insert and refuses `DUPLICATE` when the row is there, would leave no path form for an update.

## 5. Names

**After the root, a dotted name is a table, always and only.** From the root it is any table; after that, a table joined to the one before it by exactly one foreign key. Columns appear only inside brackets (`.city`) and braces. **A method is anything followed by `(`.** The root keeps its attributes, `max_recursion` and `ef_search`; a table of either name is reached by `shop["max_recursion"]` (§8.5).

**When a step cannot be taken, the refusal says what to write.** Two tables joined twice, and a table joined to itself, are where a step by table name cannot tell which key is meant (§10 A6). The program filters by the key's column instead, which is what the step would have done:

```sql
create table invoice (..., bill_to integer references customer, ship_to integer references customer);
create table employee (id integer serial primary key, name text, boss integer references employee);
```

```basic
' "the customer invoice 17 is billed to"
inv = shop.invoice[17]
who = shop.customer[inv.bill_to]
' "the invoices billed to customer 3"
billed = shop.invoice[.bill_to = 3]

' "employee 7's manager"
e = shop.employee[7]
boss = shop.employee[e.boss]
' "the people who report to employee 7"
reports = shop.employee[.boss = 7]
```

And the refusals that lead there:

> ``shop.invoice[17].customer: invoice reaches customer by two foreign keys, bill_to and ship_to -- filter by the one meant: shop.customer[.id = ...], or shop.invoice[.bill_to = ...]``
>
> ``shop.employee[7].employee: employee references itself by boss, so a step cannot tell a manager from a report -- filter: shop.employee[.boss = 7] for the reports``

The parent's lookup takes two requests where a step takes one. A program that needs it in one, over many rows, writes the join in a `sql` block. A spelling naming the key in the step is left out until a program shows the filter is not enough; adding one later breaks nothing.

**Every refusal names what was there**, with a nearest match, the shape the `ui` and `load` refusals use: ``customer has no column `emial` -- did you mean `email`?``, ``shop has no table `custmer` -- did you mean `customer`?``, ``customer and line are not joined by a foreign key``. A step across a foreign key that is `NOT ENFORCED` works, since the relationship is declared even if it is not checked; one that is **unbound** (its table dropped, `gsql_fkey_design.md` §3) is refused, saying so.

**A table's name chosen at run time:** `shop["name"]` starts a path at the table a text names, as `shop.name` would, for a program that does not know its tables when it is written (§10 A8; built 2026-11-27 as brackets, which replaced the `shop.table(name)` first proposed here, §8.5):

```basic
chosen = input("table? ")
for each r in shop[chosen]
    print r
next
```

Everything after `shop[name]` is as above, so `shop[chosen][.active = true]{ id }` works. `shop.tables()`, the catalog's list of names, is not built.

## 6. The grammar, as it stands today (measured 2026-11-16)

Each change below is held to **zero new conflicts** and to every existing test of its neighbours, as every grammar addition has been, and its cost is measured before it is accepted.

- **`shop.customer` parses today** and is refused at run time: ``a database has no attribute `customer` to read: its one attribute is max_recursion ...``. The space is free, and the refusal becomes the path.
- **`shop.customer[x = 1]` parses today**: an index holding a comparison. A path's bracket is read by the evaluator, as a key or a filter, when what is indexed is a path.
- **A leading-dot column, `[.city = "Rome"]`, does not parse today** (`unexpected DOT`); neither does `.5`, so nothing starts an expression with a dot now. It becomes a primary expression meaning "this column", refused outside a path's bracket at load time where the parser can tell, and at run time otherwise.
- **A comma inside an index does not parse today** (`unexpected COMMA, expecting RBRACKET`). A filter's `,` needs the bracket to admit a list.
- **`x{ name, email }` does not parse today**: the lexer reads a `{` straight after a name or `]` as a modifier's value (`unexpected MODIFIER_VALUE`), the inline-modifier rule (`{file}p`). The braces need the lexer to tell the two apart, by the token before the brace, with every `{file}` form lexed as it is now (`run_inline_modifier.sh`). If that cannot be had cleanly, they are spelled `.{ name, email }`. *(Built so, §8.4.)*

## 7. How it will be tested

- **The differential tier, the main one.** Every path is compiled and run, and the hand-written SQL it means is run beside it on the same database; the rows must be equal. A generator makes random schemas (keys single and composite, one and two foreign keys between a pair, a table referencing itself, `NOT ENFORCED`, cascades) and random paths over them, and writes the expected SQL **from its own model of the rules in §3–§5, never from the compiler's `.sql()`**: an oracle that read the compiler's output would agree with it by construction.
- **By hand**, each row of the §3 and §4 tables, with its answer worked out on paper; each refusal in §5 beside the same path that is allowed.
- **One statement, one snapshot**: a three-table path read while another client writes between the steps' tables must see one or the other, never a mix; counted by the owner's request log, a path is one request.
- **A result runs when used**: a row added between two uses of one result is in the second and not the first; `t[3]` on a result is the key, not the fourth row.
- **Nothing in a value becomes SQL**: values holding `'`, `;`, `--` and `:name` are stored and read back as themselves.
- **The grammar**: zero conflicts, the `--ast` of each form, every inline-modifier test unchanged.
- **Valgrind**, bounded.
- **Perturbations** before it is called done: a step taken in the wrong direction, primary key order dropped, the `INSERT` rules skipped on a missing key, a result read at assignment, a bare name in a bracket read as a column. Each must turn a tier red.

## 8. Increments

1. **Read, one table:** root, table, key, filter with dotted columns, results (`for each`, `query`, `count`, `sql`). Compiles in the owner; differential tier over one-table schemas. Grammar: the leading dot and the comma.
2. **Steps** in both directions, and the refusals of §5.
3. **Writes:** a record to a key or a filter, `insert`, `delete`, inserting through a step.
4. **Columns in braces**, at the end and along the path (§3.5), after the lexer's cost is measured (§6).
5. **The cookbook and reference**: `docs/gsql_cookbook.md` gains a paths chapter, every recipe run by the gate, each beside the `sql` block it equals.

### 8.1 As built: increment 1 (2026-11-16)

`src/gsql/sql_path.c` (the compiler, in the owner), the `path` field of a QUERY (`src/gsql/owner.c`), the module's half in `src/modules/gsql.c`, and `tests/run_gsql_paths.sh`. Where the build departed from the text above, and why:

- **A composite key is its values in order, `shop.line[17, 2]`.** The first text made a filter naming every key column a key, because a comma in brackets did not parse. It parses at zero new conflicts (measured), and then whether a bracket is one or many is decided by how it is written, which the client can see without asking the catalog. That matters because the client must know before anything is sent whether to read the row now or hand back a result (§3.3). A filter naming every key column is a filter: many, holding at most one row.
- **A column is compared, combined (`and`, `or`, `not`), used in `+ - * /` and tested with `is_unknown()`; nothing else takes one.** `[len(.name) > 20]` is refused, saying a `sql` block does more. Program values are free expressions (`[.name = left(x, 2)]`). Passing a column to a function needs a decision about which gBASIC functions have a G-SQL function of the same meaning; that is for an increment that wants it.
- **`.email = unknown` matches no row**, as SQL's `= NULL` does and gBASIC's three-valued `=` says. `is_unknown(.email)` is the test, compiled to `IS NULL`. The first text's example was wrong and is corrected above.
- **Parentheses end a path**: `(shop.customer[3]).name` reads the record's field. The parser marks a parenthesized expression, since parentheses leave no node.
- **A name after a connection is a table**, except `max_recursion`. `db.timeout` was refused as an unknown attribute; it is now a path, refused when used. `tests/gsql/max_recursion_test.bas` asserted the old refusal and was rebaselined to the new one, and CHANGELOG lists it.
- **How it reaches the owner:** a QUERY carrying `path` (table, the key's parameter numbers, the filter tree, a mode: rows, count or sql) in place of `statement`. The owner compiles it in the connection's transaction, or in a snapshot of its own taken and dropped at once, then runs the text exactly as a QUERY's. Nothing else runs between the two, so the statement sees the catalog the compiler saw. A path of many is an `sql` block value (`GsqlStatementValue`, `is_path`), so `for each`, `query` and the batches are a block's own code.
- **`.sql()` shows the parameters** as print shows them inside an array, `$1 = "Rome"`, `$2 = 10`.
- **Measured:** the semantics fixture (93 checks by hand), five differential seeds of 120 paths each (43 to 89 answering rows), valgrind clean. Five perturbations, each red: `!=` compiled as `=`, the order reversed, a composite key's `AND` made `OR`, a key's row never read, `IS NULL` made `IS NOT NULL`.

**Settled for increment 2** (Matthew, 2026-11-17): whether a step is one or many depends on which way its foreign key points, which only the catalog knows, while the client must know before it sends anything whether to read now. He chose the client holding the foreign keys, so a step reads as a record does, *"gBASIC should work for the programmer over having the programmer work for it"*, on the condition that a stale copy could never give a wrong answer: the owner checks every request (§8.2).

### 8.2 As built: increment 2, steps (2026-11-17)

- **One or many, decided by the client and checked by the owner.** A connection holds the database's foreign keys (`{ name, child, parent, bound }`), fetched by a path request in mode `fkeys` the first time a step needs them. Each request says what the client decided, `expect: "one"` or `"many"`. The owner resolves every step again from its own catalog, in the statement's transaction, and refuses a path the client called one that is now many with **`STALE`**, a new wire code. The client then fetches the keys again and decides again: still one, it reads once more; now many, the path is answered as a result. A path the client called many that is one is not refused, since a result holding one row is still right. So keys gone stale cost a round trip, never a wrong answer. Measured by a perturbation: with the owner's check switched off, the fixture's stale case handed the program one customer's record where the database held many.
- **A step the held keys cannot take** (no key, two, a table joined to itself) fetches them again first, in case they are stale; if they still cannot, the client asks the owner to compile the path without running it, and raises the owner's refusal, which names the tables and, for a misspelling, the nearest. If the owner takes it after all (a change between the two reads), the path is answered as a result.
- **What a step compiles to.** Each level is a table aliased `t0`, `t1`, ...; the last is the one read. A step from one row across a one-column key is a scalar subquery the step's table is sought by, `"t1"."id" = (SELECT "t0"."customer" FROM "invoice" AS "t0" WHERE "t0"."id" = $1)`, so the primary key or an index on the column is used. Anything else, a step from many or across a key of several columns, is a correlated `EXISTS`. Each parent is then given once, and the path is still one statement over one snapshot. A path of one table is written as before, with no aliases, so its `.sql()` text did not change.
- **The refusals** are the owner's, code `NOT_FOUND`: *`shipment` and `customer` are joined by 2 foreign keys, shipment_bill_to_fkey, shipment_ship_to_fkey, so a step cannot tell which is meant -- filter by the key's column instead*; *`employee` references itself by employee_boss_fkey, so a step cannot tell a parent from a child -- filter by its column: `.boss = ...`*; *not joined by a foreign key*; an unbound key; and *has no table `invoce` -- did you mean `invoice`?*.
- **Tests:** `tests/gsql/paths_steps_test.bas` (38 checks by hand: down and up, keys and filters on a step, a two-column key both ways, the refusals beside the filter each points to, and the stale case driven from a second connection that turns a key around), and `stepdiff`, random chains of two to four tables walked up and down, against SQL the generator nests as `IN` subqueries, a different form from the compiler's (five seeds of 100 paths, 21 to 68 answering rows). Six perturbations, each red: the owner's check off, a step's direction reversed, one carried down a step, an `EXISTS` join comparing a column to itself, the client's one carried down, and the stale retry dropped.

### 8.3 As built: increment 3, writes (2026-11-17)

- **What each write compiles to,** one statement each:

  | write | SQL |
  |---|---|
  | `shop.customer[3] = rec` (a key of one table) | `UPDATE customer SET c = $n, ... WHERE id = $k` and, when it changed no row, `INSERT INTO customer (id, ...) VALUES ($k, ...)`, both in one write transaction (the program's, if one is open) |
  | any other path `= rec` | `UPDATE t [AS tN] SET c = $n, ... WHERE <the path's conditions>` |
  | `path.insert(rec or [recs])` | `INSERT INTO t (cols) VALUES (...), (...) RETURNING *`, a column a row leaves out as `DEFAULT` |
  | `parent[k].child.insert(...)` | `INSERT INTO child (cols, fk) SELECT $n, ..., t0.id FROM parent AS t0 WHERE ... [UNION ALL ...] RETURNING *` |
  | `path.delete()` | `DELETE FROM t [AS tN] WHERE <the path's conditions>` |

- **Corrected 2026-11-17, by the cookbook's recipe:** the key's write was first one `INSERT ... ON CONFLICT DO UPDATE`. That checks the row it proposes, `NOT NULL` among the rest, *before* it finds the conflict (PostgreSQL's order too), so `shop.customer[1] = { city: "Paris" }` on a row that is there was refused because the record left out `name`, a column the write never meant to touch. The writes fixture's `name` had a default and the differential's tables had no `NOT NULL` column, so neither could see it. It is now an `UPDATE`, then the `INSERT` only when no row was found, in one write transaction so no other writer comes between them. The fixture gained a `NOT NULL` column with no default, and the differential's tables one each, with its own SQL written as the same two statements; run against the old build they fail (16, 1 and 40 mismatches over three seeds; the fixture stops at the new check).
- **A missing row.** A key of one table is inserted by `INSERT`'s rules (§10 A2): defaults, `NOT NULL`, `CHECK`, foreign keys, a `SERIAL` key supplied moving its counter up. Any other path of one row, which is to say one reached by a step, is refused **`NOT_FOUND`** when its row is not there, since what would be inserted is not said: `shop.customer[2].invoice[1] = ...` when invoice 1 is another customer's. The client counts the rows the `UPDATE` changed, since `UPDATE ... RETURNING` is not in this release. A path of many writes the rows it has, none included.
- **Departures from §4:**
  - a record naming a key column is refused even when the value is the key's own, which is simpler to say than "may not change it";
  - `delete()` works on every path, a step from many included (`shop.customer[.city = "Rome"].invoice.delete()`), since what it removes is as well defined as what that path reads;
  - rows inserted through a step all name the same columns, since the form is `INSERT ... SELECT` and a `SELECT` has no `DEFAULT`; through a table alone they need not;
  - an insert through a step from a row that is not there inserts nothing and is refused **`NOT_FOUND`**, not `FOREIGN_KEY`: the `SELECT` finds no parent, so no row reaches the key's check.
- **Assignment.** `x = ...` whose target is a path (a dot or bracket after a connection, by the variable it starts from) is a write, not a variable's; `shop.max_recursion = n` is still the attribute. A compound assignment or a modifier clause on a path is refused. Grammar: a list in an assignment's brackets, `shop.line[17, 2] = ...`, at zero new conflicts.
- **Tests:** `tests/gsql/paths_writes_test.bas` (59 checks by hand, every write read back by SQL, never by a path, so the two cannot share a mistake), and `writediff`: two databases made alike, 60 random writes (delete, update, upsert, insert, insert through a step) by path to one and by the generator's own SQL to the other, every table compared row for row after each, and the same refusal on both sides (five seeds, 20 to 30 writes each changing data). Five perturbations, each red: `ON CONFLICT DO NOTHING`, an `UPDATE` losing its `WHERE`, a step's insert writing `NULL` for the key, the client's `NOT_FOUND` dropped, a key column allowed in a record.

### 8.4 As built: increment 4, braces (2026-11-17)

- **The lexer decides,** as §6 proposed, and gives a path's `{` a token of its own, `PATH_LBRACE`, so the grammar never has to choose: with `LBRACE` the rule cost two conflicts against a comparison's lens (`a {caseless} < b`), with its own token none. A `{` is a path's when the token before it is a name, a qualified name or `]` (a keyword is not a name, so `return {USD}"1"` and `print {date}s` are untouched), and its group is not followed by `=`, a compound assignment (`+= -= *= /=`) or a comparison, which is what an assignment clause and a lens always have. The first rule missed the compound one: `name{upper} += "cd"` in `examples/loop_syntax_test.bas` stopped parsing, and run_examples caught it. My first search of the tree for the shape had been cut at 20 lines of output, which is how the case was missed; the search was run again whole, and every remaining hit was a keyword. So `x{ name } = ...` is a modifier clause by its shape, and a path refuses it as one.
- **One table:** a select list, `SELECT "name" AS "name" FROM ...`, otherwise as before.
- **Braces before a step join** from the first braced table to the last, `FROM a AS t0 JOIN b AS t1 ON ...`, each joined table's key and filter in the `WHERE`, and the tables before the first braced one a semi-join on it, as an unbraced path is. So a table passed through gives no columns, and `shop.customer[.city = "Rome"].purchases{ item }` is each purchase once. The rows are one per row reached along the join: a step down gives a row per child, a step up a row per child carrying its parent (§3.5 said "one row per row of the last table", which is right only for steps down; the build follows the join). **Order:** the last table's primary key, then back through the tables joined.
- **One or many** with braces before the last table: one only when every table from the first braced one to the last is one, since a join from a key can still reach many (`shop.purchases[11]{ item }.customer{ name }` is one; `shop.customer[3]{ name }.purchases{ item }` is many). The client and the owner work it out alike, and the STALE check covers it.
- **Refusals:** two tables giving one name (*`customer` and `purchases` both give `id` -- leave one out*), a name twice, a column not there (with the nearest), braces twice on one table, braces on something not a path, and any write, insert or delete through braces.
- **Tests:** `tests/gsql/paths_braces_test.bas` (32 checks by hand, the forms `{` already had among them), and the stepdiff tier's paths now carry braces about four times in ten, against SQL the generator writes as a comma join with every column qualified, a different form from the compiler's `JOIN ... ON` (13 to 27 joins and 19 to 27 last-table braces a seed). Five perturbations, each red: the `ON` columns swapped, no join, the order by the last table only, the client's one-or-many ignoring braces, and the lexer's compound-assignment test dropped.

### 8.5 As built: a table named by a value (2026-11-27)

Matthew: "This also allows code to specify tables/views dynamically."

- **`db["customer"]` is `db.customer`**, the same path and the same SQL, for a text: literal, computed, or held in a variable or a parameter. A name a connection's attribute takes (`db["ef_search"]` is the table, `db.ef_search` the attribute), a reserved word (`db["select"]`) and one that is no identifier (`db["order lines"]`) are reached too. The name goes to the owner as a value and is looked up in the catalog, folded to lower case as an unquoted SQL name is, so a computed name cannot become SQL.
- **Refused, at the bracket:** a number (tables have no order; AmorphDB's numeric index has nothing to count here), anything but a text, an empty text, a text holding a NUL (never cut at it), two names, and a filter, `db[.name = "x"]`, which is kept free for a filter over the catalog. On a table, brackets are a key or filters, as before.
- **A defect found on the way:** a brace chain whose first step was a filter on the connection, `db[.cond]{ a }`, was handed to the PATH's bracket, which read the connection as a path statement: a segfault. Both the index expression and the brace chain now give a connection to one function.
- **Views are not reached**, by this or by a dot: a path opens tables only.
- **Tests:** `tests/gsql/paths_name_test.bas` (41 checks by hand, writes read back by SQL; run_gsql_paths' names tier, and its valgrind tier). Four perturbations, each red: the brace chain back as it was (a segfault), the NUL check dropped, the index expression's branch dropped, two names accepted.

## 9. Not in this note

- **Watching a path** (`watch(shop.customer[3])`), which is MBL's centre. It needs the owner to tell a client what changed, and G-SQL has no channel for that yet. A design of its own, if wanted after paths are used.
- **History** (`[@2026-01-01]`). G-SQL keeps none.
- **Aggregates by path** beyond `count()`. SQL's `GROUP BY` is where those belong, and §2 makes moving to it cheap.
- **Order and limits by path.** Ruled out of the first increments (§10 A5).
- **Schema by path.** Ruled out by Matthew.

## 10. Rulings (Matthew, 2026-11-16)

The first draft asked nine questions. The answers, and what each changed:

- **A1. Dots are tables, always and only;** columns are chosen in braces at the end, all of them when there are none. *"AmorphDB is not tabular data."* This removed column steps, and with them the first draft's collision between a foreign-key column and its table (`invoice.customer`), its rule that a column outranks a table, and steps by constraint name (§3.1, §5).
- **A2. A key path naming a missing row follows SQL's `INSERT` rules.** An insert of that key and the record's fields, and a row that is there is updated (§4).
- **A3. Columns in a bracket are always dotted,** so the rest is free expression, and the dot shows the column belongs to the table to the left (§3.1).
- **A4. A write through a filter that matches nothing, and a `delete` that removes nothing, are allowed;** `delete()` answers its count. Only a missing key is handled differently, by A2.
- **A5. Primary key order only;** and a path of many answers what a `sql` block's `select` does, a handle to iterate (§3.3).
- **A6. Asked for examples** of a table joined to itself. §5 gives them: such steps are refused and the filter is written instead. Matthew: the aim is always to save the programmer work, which is hard here, so the filter stands for now.
- **A7. Agreed** that a path of many may be stored. With A5 it is stored as a result, not read, which removes the first draft's trap (`t[3]` is customer 3).
- **A8. Asked for examples.** §3.4 and §5 give them: `.sql()` as a method replaces `sql_of(path)`, and `shop.table(name)` with `shop.tables()`.
- **A10. Braces along a path make a join** (Matthew asked whether `shop.customer[3]{id,name,phone}.purchases{item,price}` would; it does, §3.5).
- **A2, A6, A8 confirmed** as written, 2026-11-16.
- **A9. The root is a connection** (`database` block or `gsql.connect`). A `database` role with no file has no tables until given a connection, so `tenant.customer` is refused, saying to step from the connection instead.

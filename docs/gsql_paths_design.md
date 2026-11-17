# Paths over G-SQL: reading and writing tables by path

**Status:** design note, 2026-11-16, revised the same day with Matthew's rulings on its first draft (§10). Nothing here is built.

## 1. What it is, and what it is not

Matthew, 2026-11-14:

> Not to replace SQL but to supplement it. SQL makes most sense for data analytics, ETL, integrations, and migrations. The paths approach is, however, more practical for applications. The two could work in harmony ... They would talk to the same data -- the G-SQL data store. We are only talking about reading/writing data via paths. G-SQL would still be used to create tables and relationships.

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
| filter | `shop.customer[.city = "Rome", .email = unknown]` | the rows matching; `,` is `and`, `or` is written; `(...)` groups |
| step | `shop.invoice[17].customer`, `shop.customer[3].invoice` | to another table across the one foreign key joining the two, in whichever direction it points |
| columns | `...{ name, email }` | only these columns of the table just before it, as fields in this order; at the end of the path, or after any table along it (§3.5) |

**Inside a bracket, a column is written with a leading dot and everything else is an ordinary gBASIC expression** (§10 A3), evaluated where the path is written and sent as a parameter: `[.city = wanted]`, `[.total > limit * 2]`, `[.paid < .total]`, `[len(.name) > 20]`. The dot also shows the column belongs to the table to the left of the bracket.

**A bracket with no column in it is a key**, for a table whose primary key is one column: `[3]`, `[n]`. **A filter that fixes every primary key column with `=`, and nothing else, is also a key**, so a composite key is written by naming its columns, `shop.line[.invoice = 17, .n = 2]`. Part of a composite key is a filter, `shop.line[.invoice = 17]`.

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
print rome[.email = unknown].sql()          ' a result goes on being a path
first = rome[.name = "Ana"]                 ' ...including to one row: read now
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

**After the root, a dotted name is a table, always and only.** From the root it is any table; after that, a table joined to the one before it by exactly one foreign key. Columns appear only inside brackets (`.city`) and braces. **A method is anything followed by `(`.** The root keeps its one attribute, `max_recursion`; a table of that name is reached by `shop.table("max_recursion")`.

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

**A table's name chosen at run time:** `shop.table(name)` starts a path at the table whose name `name` holds, for a program that does not know its tables when it is written (§10 A8):

```basic
' an admin screen: list any table the user picks
for each name in shop.tables()
    print name
next
chosen = input("table? ")
for each r in shop.table(chosen)
    print r
next
```

`shop.tables()` is the catalog's list of table names. Everything after `shop.table(name)` is as above, so `shop.table(chosen)[.active = true]{ id }` works.

## 6. The grammar, as it stands today (measured 2026-11-16)

Each change below is held to **zero new conflicts** and to every existing test of its neighbours, as every grammar addition has been, and its cost is measured before it is accepted.

- **`shop.customer` parses today** and is refused at run time: ``a database has no attribute `customer` to read: its one attribute is max_recursion ...``. The space is free, and the refusal becomes the path.
- **`shop.customer[x = 1]` parses today**: an index holding a comparison. A path's bracket is read by the evaluator, as a key or a filter, when what is indexed is a path.
- **A leading-dot column, `[.city = "Rome"]`, does not parse today** (`unexpected DOT`); neither does `.5`, so nothing starts an expression with a dot now. It becomes a primary expression meaning "this column", refused outside a path's bracket at load time where the parser can tell, and at run time otherwise.
- **A comma inside an index does not parse today** (`unexpected COMMA, expecting RBRACKET`). A filter's `,` needs the bracket to admit a list.
- **`x{ name, email }` does not parse today**: the lexer reads a `{` straight after a name or `]` as a modifier's value (`unexpected MODIFIER_VALUE`), the inline-modifier rule (`{file}p`). The braces need the lexer to tell the two apart, by the token before the brace, with every `{file}` form lexed as it is now (`run_inline_modifier.sh`). If that cannot be had cleanly, they are spelled `.{ name, email }`.

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

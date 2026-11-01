# G-SQL: a block run on a database chosen at run time

**Status:** Shipped 2026-11-01 (`gsql_eval_on` and `gsql_call_target` in `src/modules/gsql.c`; `tests/gsql/blocks_test.bas`). Designed the same day, from Matthew's ruling the same day that the block form is G-SQL's normal form and strings are for dynamic SQL (option 3 of three, his choice): the connection is chosen at the call.

## 1. The gap

`sql add on shop ... end sql` runs only on `shop`, and `shop` must be a `database` block, whose `file:` is a literal (`gsql_module_design.md` §7). Declarations are bound before the program runs, so they cannot read a variable. So a program whose SQL is fixed but whose database is chosen at run time has no block form. That includes the arrangement `gsql_practices.md` §1 recommends, one database per tenant, and every program that keeps its database in a directory it is given. It is forced back to strings for SQL that is not dynamic at all.

## 2. The rule

**A block can be given the connection to run on when it is used.** Its SQL stays fixed text, parsed and checked when the program loads, with values arriving only as `:name` parameters. Only the database is chosen at run time.

```basic
load gsql

database tenant()                ' a role: its blocks are given a connection

sql add on tenant
    insert into item (id, name) values (:id, :name)
end sql

sql items on tenant
    select id, name from item order by id
end sql

db = gsql.connect(join_path(tenants, name + ".gsql"), { create: true })
id = 1
name = "apple"
gsql.exec(db, add)                       ' connection first, as with a string
for each row in gsql.on(db, items)       ' a block bound to db, for a loop
    print row.name
next
```

- **`gsql.exec(connection, block)` and `gsql.query(connection, block)`** run the block on that connection. This is the shape the verbs already have for a string, connection first. A third argument is refused, since a block's parameters are the variables its `:names` name.
- **`gsql.on(connection, block)`** answers the same block bound to that connection. It is what `for each` takes, since a loop has no argument list. Anything that takes a block takes it: `gsql.exec(gsql.on(db, add))` works too.
- **`database NAME()`, with no `file:`, declares a role** rather than a database: its blocks must be given a connection. Using one without a connection is refused at run time, naming the remedy (`gsql.on(connection, items)`).
- **A block on a `database` block that does have a file** can be given another connection the same way. Its own file stays the default.

## 3. What stays checked

- **The SQL is parsed at load**, so a syntax error is still located at its line and column in the program.
- **`on` must still name a `database` block in the file.** A role is a `database` block, so the check that `on` names one survives unchanged, and a misspelt `on` is still a load-time error.
- **A role takes no `create:` or `linger:`.** Those are a connection's options and belong to the `gsql.connect` that makes it; refused at load time, naming that.
- **Nothing is spliced into the SQL**, whatever connection is chosen, so injection stays impossible by construction.

## 4. Decisions

- **The connection comes first** in `exec` and `query`, matching the string form, rather than a named option. gBASIC passes options as records, and an options record holding a connection reads worse than the shape the verbs already have.
- **`gsql.on` makes a new block value** and leaves the declared one alone: binding is not assignment, so two tenants' bound blocks can be live at once.
- **A bound block holds its connection,** as any value holding a connection does; closing the connection makes the bound block refuse, naming it closed.

## 5. Tests

In `tests/run_gsql_module.sh`, beside the block tiers:

- **Two tenants at once:** one role, two databases connected at run time, the same blocks run on each; each holds only its own rows, and the loop over `gsql.on` streams each one's rows.
- **The default stays:** a block on a `database` block with a file still runs there without a connection, and runs elsewhere when given one.
- **Refusals, each beside its legal neighbour:** a role's block used alone; a third argument to `exec` with a block; `gsql.on` given something that is not a connection, or not a block; a role with `create:`; a closed connection under a bound block.
- **Parameters still come from where the block runs:** inside a function, its own variables.

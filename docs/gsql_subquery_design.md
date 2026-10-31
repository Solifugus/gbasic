# G-SQL subqueries (Phase 2, increment 5)

**Status:** designed 2026-10-31. The fifth increment of `gsql_design.md` §11 Phase 2, after aggregates, joins, `DISTINCT` and set operations. It covers subqueries **in expressions**. Subqueries in `FROM` and non-recursive `WITH` are the next increment (§8), because they need a scan over rows that are not a table's.

## 1. What it is

```sql
select name, (select count(*) from orders o where o.customer = c.id) as orders
from customer c
where exists (select 1 from orders o where o.customer = c.id and o.total > 100)
  and c.region in (select region from target)
```

Three forms, each a whole `SELECT` (with joins, grouping, `DISTINCT`, set operations, `ORDER BY` and `LIMIT`) inside parentheses:

- **A scalar subquery,** `(select ...)`, used as a value. It must have exactly one column. No rows gives `NULL`. More than one row is refused when it happens, as PostgreSQL refuses it.
- **`x [NOT] IN (select ...)`.** The subquery must have exactly one column.
- **`[NOT] EXISTS (select ...)`.** Any columns; only whether a row exists matters.

**Correlation.** A name the subquery's own tables do not have is looked up in the enclosing query, then the one enclosing that, outward. Such an **outer reference** reads the outer row current when the subquery runs, so a correlated subquery is evaluated once per outer row. A subquery with no outer reference is evaluated **once per execution**, on first use, and its answer is reused.

**Where.** Anywhere an expression is allowed in `SELECT`, `UPDATE` and `DELETE`: the select list, `WHERE`, `ON`, `HAVING`, `ORDER BY`, `SET`, and inside function calls and aggregate arguments.

**It is not:**

- `ANY`, `SOME` and `ALL`. `x = ANY (select ...)` is `x IN (select ...)`; the others are refused as not in this release.
- Row comparisons, `(a, b) IN (select x, y ...)`.
- A subquery in `GROUP BY`, in a column `DEFAULT`, in `LIMIT`/`OFFSET`, or in `INSERT ... VALUES`. Each is refused by name. `INSERT ... VALUES` is refused because its rows are written one at a time, so a subquery reading the table would see the rows written before it. That is wrong by SQL's rule that a statement sees the database as it was when it began.
- An aggregate in a subquery whose argument reads an outer column, `(select sum(o.x) from s)` with `o` outer. SQL makes that the **outer** query's aggregate, which is a rule nobody expects. It is refused rather than given the other meaning.

## 2. Semantics, which are SQL's

- **`IN`** is the `IN` list's Kleene rule over the subquery's rows, with gBASIC's `=` (`gsql_sql_design.md` §9): true if any row is equal, otherwise unknown if any comparison was unknown, otherwise false. A subquery with no rows gives false, even for a `NULL` on the left. `NOT IN` is its negation, so `x NOT IN (select ...)` is unknown, and drops the row, whenever the subquery yields a `NULL` and nothing equal. That is SQL's notorious rule, and the tests assert it.
- **No implicit casts:** `'1' in (select id ...)` against an integer column is refused, as `=` refuses it.
- **`EXISTS`** is true or false, never unknown.
- **A scalar subquery's** value is its one row's one column.

## 3. Binding

The planner binds each subquery as a plan of its own, with a pointer to the enclosing binder. A name is bound in the subquery's own tables first, then in each enclosing query's, outward. One found outward becomes an **outer reference**: the number of levels up, the scan, and the column. The usual rules apply at the level where the name is found:

- An `ON` condition's subquery sees only the tables joined so far.
- A grouped outer query's select list, `HAVING` and `ORDER BY` may reach only a `GROUP BY` column through a subquery, as directly. The outer row a subquery then reads is the group's first row, which is how a grouped query already reads its group values (`gsql_aggregate_design.md`).
- A bare name in both the subquery's tables and an outer one is the subquery's, as in SQL. That is not ambiguous, so it is not refused.

Each subquery node records whether it is correlated, and which columns of the **immediately enclosing** query it reads, directly or through a subquery nested inside it. That list is what the access-path rule below uses.

## 4. Running it

A subquery runs as a child execution of its own plan, sharing the statement's parameters and transaction. An outer reference reads the row its level has in hand.

- **Uncorrelated:** run once, on first use. A scalar keeps its value. `EXISTS` keeps its answer. `IN` keeps the column's values and compares the left side against each.
- **Correlated:** run afresh for each evaluation. `EXISTS` stops at the first row, `IN` at the first equal row, and a scalar at the second row, where it refuses.
- **The cost is stated:** `IN` compares one value at a time, so an uncorrelated `IN` over *n* rows costs *n* comparisons per outer row. Hashing it is the Phase 2 hash-operators item. It is not done here because a hash decides equality by the key encoding, while `=` also refuses mismatched kinds, which a hash would silently answer.

**Inside the subquery, an outer reference is a constant.** So `o.customer = c.id` narrows the subquery's scan of `orders` exactly as it would if `c.id` were a literal (`gsql_join_design.md` §3). With an index on `orders(customer)`, a correlated `EXISTS` is one seek per customer.

**Outside, a subquery may be an access path's bound** if every column it reads from this query belongs to a table read earlier, the rule a join already applies to a bound's columns. So `where id = (select max(id) from t)` is a primary-key seek.

**Writes.** `UPDATE` and `DELETE` collect every change before applying any (`sql_exec.c`'s change list), so a subquery reading the table being changed sees it as the statement found it.

**`EXPLAIN`** prints each subquery's plan after the statement's, numbered in the order they appear:

```
Scan on customer AS c
  Filter: EXISTS (SubPlan 1)
SubPlan 1, per row of the outer query
  Index Seek on orders AS o using orders_by_customer (customer = c.id)
```

## 5. Parsing

`(` followed by `SELECT` begins a subquery wherever a value may appear. `IN (` followed by `SELECT` is the `IN` form, and `IN ((select ...), 2)` is still a list whose first item is a scalar subquery. `EXISTS (` must be followed by a `SELECT`. Inside the parentheses a subquery is a whole compound, with its own `ORDER BY`, `LIMIT` and `OFFSET`. The canonical printer writes each form back, so that it parses again.

## 6. How it is tested

`tests/run_gsql_sql.sh`:

- **parse:** each form printed back canonically; the refusals of §1 with their positions.
- **semantics:** hand-written answers. They cover the three forms correlated and not; no rows for each (scalar `NULL`, `IN` false, `EXISTS` false); a scalar with two rows refused; `NOT IN` with a `NULL` in the subquery; `IN` with a mismatched kind refused; a bare name shadowing an outer one; two levels of nesting reaching the outermost table; a subquery in `ON`, `HAVING` and the select list of a grouped query; an outer reference to a non-grouped column refused; `UPDATE ... where x in (select ... from` the same table); and `EXPLAIN` showing a correlated subquery's seek.
- **differential:** random `IN`, `EXISTS` and correlated scalar subqueries over the second table, each run with access paths and forced to scans, inside the subquery as well. A census floor requires seeks inside correlated subqueries, since a binding that lost the outer reference's constancy answers correctly and only slowly.
- **oracle:** the same predicates computed by the test from each table fetched alone, matching rows through the host's `=`, sharing no subquery code with the engine. Each form has its own floor of answers with rows, as set operations have per operation.

The one-directional allowance carries over. A path may stop before a row that refuses, and so may `EXISTS` and `IN`, which stop at the first row that decides them.

## 7. Building it

The executor's state for one execution gains a pointer to its parent, and a subquery runs as a child execution. That child is the cursor set operations already run each side as. Nothing about a single-table, join or compound statement changes, and the existing tiers must stay green with their censuses unmoved.

## 8. Next: `FROM` subqueries and `WITH`

A subquery in `FROM`, and a `WITH` name, is a **derived table**: a scan whose rows come from a plan rather than from storage. Its columns are the plan's output names and kinds. It is materialized once per execution, or per outer row for `LATERAL`, which will not be in that increment. Its rows are read by position rather than through a table's row layout. Only the scan's row source differs from a table's. That increment gets its own design.

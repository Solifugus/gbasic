# G-SQL `UNION`, `INTERSECT` and `EXCEPT` (Phase 2, increment 4)

**Status:** designed 2026-10-31. The fourth increment of `gsql_design.md` §11 Phase 2, after aggregates, joins and `SELECT DISTINCT`.

## 1. What it is

```sql
select id, name from customer where region = 'east'
union
select id, name from customer where vip
order by name
limit 20
```

- **`UNION`**, **`INTERSECT`** and **`EXCEPT`**, each also with `ALL`, between any number of `SELECT`s. `UNION DISTINCT` and the others with `DISTINCT` are accepted and mean the plain form.
- **`INTERSECT` binds tighter than `UNION` and `EXCEPT`**, which group left to right. That is the standard's rule and PostgreSQL's: `a union b intersect c` is `a union (b intersect c)`.
- **`ORDER BY`, `LIMIT` and `OFFSET` go at the end and apply to the whole result.** One of them on an earlier branch is refused, naming the rule.

- **A branch may be parenthesized** to group: `a intersect (b union c)`. That is also how the canonical form prints a group that binds tighter than the words alone would, so the printed form parses back as itself.

**It is not** `ORDER BY`, `LIMIT` or `OFFSET` inside parentheses (`(select … limit 1) union …`), a statement that begins with a parenthesis, set operations inside a subquery or a view (neither exists yet), or `CORRESPONDING`.

## 2. Semantics, which are SQL's

- **The branches must have the same number of columns.** The refusal names both counts.
- **The result's column names are the first branch's.**
- **No implicit casts, and none needed.** A row's values keep their own kinds, so a branch giving `3` and another giving `'3'` produce two rows, a number and a text. A column's declared kind is the branches' kind where they agree and `any` where they do not.
- **"The same row" is the index key's equality**, the one `DISTINCT`, `GROUP BY` and `UNIQUE` use: `NULL`s are one value, `3` and `3.0` are one value. Five places in G-SQL now ask whether two values are one, and all five answer it alike.
- With the key equality, for a row that appears `l` times on the left and `r` times on the right:

| operation | copies in the result |
|---|---|
| `UNION ALL` | `l + r` |
| `UNION` | 1 if `l + r > 0` |
| `INTERSECT ALL` | `min(l, r)` |
| `INTERSECT` | 1 if both are > 0 |
| `EXCEPT ALL` | `max(l - r, 0)` |
| `EXCEPT` | 1 if `l > 0` and `r = 0` |

## 3. `ORDER BY` names an output

Over a compound result there are no tables to read a column from, only the result's columns. So an `ORDER BY` term is a position or the name of a column of the result (the first branch's names). An expression is refused, naming the rule. This is PostgreSQL's rule.

**Without `ORDER BY`, rows come out in the order the branches produced them**, left first, a copy dropped where it repeats one already out. SQL leaves that order unspecified; it is still reproducible.

## 4. How it runs

Each branch is planned as the ordinary `SELECT` it is, with its own access path, so an index still narrows each branch and `EXPLAIN` shows how. The set operation runs over the branches' output rows, keyed as `DISTINCT` keys them. The result is held in memory, like an unindexed `ORDER BY`; spilling is later Phase 2 work.

```
Union
  Index Seek on customer using customer_region (region = 'east')
  Scan on customer
    Filter: vip
  Order: name (sorted)
  Limit: 20
```

## 5. How it is tested

- **parse:** each form printed back canonically, the precedence shown by the printer's parentheses, and the refusals at their positions.
- **semantics:** hand-written answers for each of the six forms with duplicates on both sides, `NULL`s and `3`/`3.0` matching across branches, three branches mixing the precedence, `ORDER BY` by position and by name, `LIMIT`/`OFFSET`, a column-count mismatch, an `ORDER BY` on an earlier branch, and an expression in the final `ORDER BY`.
- **differential (oracle):** random pairs of `SELECT`s over the random table, combined by a random operation; the test fetches each branch alone and computes the result itself by counting copies through the host's equality, sharing no key or hash code with the engine.

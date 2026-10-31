# G-SQL hash joins (Phase 2, increment 7)

**Status:** designed and built 2026-11-01 (`src/gsql/sql_plan.c` `choose_hash`, `src/gsql/sql_exec.c` `hash_build`/`hash_probe`; `tests/run_gsql_sql.sh`). One of `gsql_design.md` §11 Phase 2's "efficient hash operators": the one with a measured need.

## 1. Why

A join reads its inner table once per outer row (`gsql_join_design.md` §3). That is cheap when the inner table has an index on the join key, because each read is one seek. Without an index each read is a whole scan, and the join is quadratic. Measured through the `gsql` module, `select count(*) from a join b on b.k = a.k` with no index on `k`:

| rows per side | join time |
|---|---|
| 1,000 | about 0.56 s |
| 2,000 | about 2.2 s |
| 4,000 | about 9 s |

That is about 560 ns per pair. **A derived table or `WITH` query can never have an index** (`gsql_derived_design.md`), so every join on one pays this.

## 2. What it is

When an inner table (scan *k* > 0) would be read whole, and one of its conditions is `col = expr`, the planner gives the scan a **hash**. Here `col` is a plain column of scan *k* and `expr` reads only earlier tables, the same rule an index seek's bound follows. The condition may be in `ON`, or a `WHERE` conjunct placed there.

1. **Building.** The first time the scan is opened in an execution, every row is read once and grouped by `col`'s **index key**: the encoding `DISTINCT`, `GROUP BY` and set operations already use to decide "the same value".
2. **Probing.** Each later opening evaluates `expr` against the outer row, keys it, and reads only that key's rows.

**The hash only narrows, the filter decides.** Every condition, the equality included, is still checked on every row the hash yields, by gBASIC's `=` as before. The narrowing is sound because every row on which `col = expr` is true has `col`'s key equal to `expr`'s:

- `=` is true for 3 and 3.0, and both have one key.
- `=` is never true for a `NULL`, so a `NULL` probe yields no rows.
- `=` answers false for two currencies, and they have two keys.

Where `=` would **refuse**, as for a number against text, the hash reads no such row. So that refusal is not reached, which is the one-directional allowance every access path already has (`gsql_sql_design.md` §5).

**A value with no key,** such as a record, is left out of the groups. A probe with no key reads every row. That is enough, because `=` is true of a value with no key only against another value with no key, and every such probe reads every row anyway. Either way the answer is the nested loop's.

**Forced to a scan** (`gsql_sql_force_scan`), no hash is used. That is what lets the differential tier compare the hash join with the nested loop it replaces.

**The cost is memory:** the inner table's rows are held for the execution. Spill-to-disk is a later item, and this is the second place that will need it.

**`EXPLAIN`:** `Hash Scan on q using (k = r.a)`, and for a derived table `Hash Derived Scan on d using (...)`.

## 3. How it is tested

- **semantics:** a hashed join with duplicates on both sides; `NULL` keys; 3 against 3.0; two currencies; a `LEFT` join with unmatched rows; a hash over a derived table and a `WITH` query; a record column falling back; and `EXPLAIN` showing the hash, and the seek it does not replace where an index exists.
- **differential:** the join tier's tables gain an unindexed column, so random joins on it run with the hash and forced to a nested loop, which must agree. A census floor requires that probes happened.
- **the cost, as a ratio:** the 4,000-row join must be at least 20x faster than the nested loop. The shape is asserted, not an absolute time.

## 4. What building it found

- **The ratio tier:** 2,000 rows against 1,000, hashed in 0.0027 s against 1.2 s for the nested loop, a ratio of about 440 against a gate of 20. The random tier probed in 221 to 234 of 300 joins a seed, every one agreeing with the nested loop.
- **A fallback was designed that nothing needed.** The first version turned the hash off when a value could not be keyed. A perturbation that skipped such a row instead stayed green, and working it through shows why: `=` is true of a value with no key, such as a record, only against another value with no key, and every such probe already reads every row. The fallback was removed.
- **Seven perturbations:**
  - five red: a probe reading the wrong group, a chain keeping one row per key, a hash under a forced scan, a probe keyed with other flags, and a probe allowed to read its own table;
  - the last one was green at first, and a case was added for `ON b.k = b.id`;
  - one green and harmless, which is the finding just above;
  - one green and harmless as well: grouping `NULL` rows, which a `NULL` probe never reads.

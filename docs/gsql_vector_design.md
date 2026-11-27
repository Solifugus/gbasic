# G-SQL: a `VECTOR` column, pgvector's distances, and search by distance

**Status:** increment 2 of the AI-engineering work (docs/ai_engineering_gaps.md
§2.2, §4), 2026-11-27, built: part A (§1-§8), the column, the three operators,
the functions and exact search; part B (§9), the HNSW index
(`tests/run_gsql_vector.sh`, and run_gsql_table's hnsw tier). Matthew's ruling on
the gap survey's question 2 (2026-11-27, "Those were good choices"): **both**
pgvector's operators and functions that name them.

The value is gBASIC's own vector (docs/vectors_design.md): packed float32, one
dimension, immutable. Its kernels are include/gb_vector.h's, and SQL reaches them
through the host as every SQL operator does (gsql_sql_design.md §2), so a
distance in SQL is the same double `distance()` answers in a program.

## 1. The column

```sql
create table chunk (id integer primary key, body text, acl text, e vector(1536))
```

`VECTOR(N)` holds vectors of exactly N dimensions, 1 to 16000 (pgvector's limit
for a stored vector). `VECTOR` alone holds a vector of any dimension, as
pgvector's unsized `vector` does. A vector may also be stored inside a record
column.

## 2. Into the column

| given | stored |
|---|---|
| a vector | itself, its dimension checked against N |
| text | read as `vector()` reads it: `'[0.1, 0.2]'`, pgvector's text form and JSON's |
| an array of numbers | converted as `vector()` converts it, so `gsql.insert` of a record holding `[0.1, 0.2]` works |
| anything else | refused: *"doc.e is vector(3), and the value is a number"* |

A vector of the wrong dimension: *"doc.e is vector(3), and the vector has 2
dimensions"*. Empty text, an element that is not a number, NaN, or one past
float32's range is refused with `vector()`'s own sentence.

## 3. Never keyed

A vector has no order, so `PRIMARY KEY`, `UNIQUE`, `CREATE INDEX` over one and a
foreign key are refused when declared (*"doc.e is a vector, which index ie cannot
hold: a vector has no order; it is searched by distance, as in ORDER BY e <=> ?
LIMIT 10"*). `GROUP BY`, `DISTINCT` and `count(DISTINCT …)` refuse one when it is
met, as they refuse a duration (*"G-SQL has no key for it"*). `ORDER BY e` is
refused with the distance form. `=` and `<>` compare element by element, as in
gBASIC; `<` raises gBASIC's sentence. The approximate index of §9 is a different
kind of index, declared with `USING`.

## 4. The operators

| SQL | meaning | gBASIC |
|---|---|---|
| `a <-> b` | Euclidean distance | `distance(a, b, "l2")` |
| `a <=> b` | cosine distance, `1 - cos` | `distance(a, b, "cosine")` |
| `a <#> b` | the NEGATED inner product | `distance(a, b, "ip")` |

Smaller is nearer for all three, which is why pgvector negates the inner product,
and why `ORDER BY e <#> ? LIMIT 10` finds the largest products.

- **Precedence** is PostgreSQL's for an operator it has no other place for, the
  level of `||`: looser than `+` and `-`, tighter than a comparison, so
  `e <=> ? < 0.2` compares the distance.
- **Operands** are vectors, or text or an array read as one (§2's reading), so
  `e <-> '[1, 0, 0]'` works as it does in pgvector. Anything else is refused:
  *"<-> measures between two vectors, and one side is a number"*. NULL in, NULL
  out.
- **Two dimensions** are refused with gBASIC's sentence, as pgvector refuses them.
- **A zero vector under `<=>` is refused** (*"the first vector is all zeros,
  which has no direction, so no cosine"*). pgvector answers NaN there; a NaN that
  sorts silently is the answer this language does not give.
- **Text compared with a vector** is read as one: `e = '[1,0,0]'` (the one
  implicit conversion of gsql_compare_conversion_design.md, extended to this kind).
- **Lexing.** `<->`, `<=>` and `<#>` are read as one token before `<=` is tried.
  None of the three could begin anything valid before, so nothing that parsed
  changes.

## 5. The functions

pgvector's names, for SQL carried over from Postgres, and gBASIC's `distance`, so
SQL and a program spell it alike:

| function | answers |
|---|---|
| `l2_distance(a, b)` | `a <-> b` |
| `cosine_distance(a, b)` | `a <=> b` |
| `negative_inner_product(a, b)` | `a <#> b` |
| `inner_product(a, b)` | the product itself, `dot(a, b)`, as pgvector's is |
| `distance(a, b [, metric])` | gBASIC's, metric `'l2'` (default), `'cosine'` or `'ip'` |
| `vector_dims(v)` | its dimension |
| `vector_norm(v)` | its Euclidean length |

Operands are read as for the operators. `+` and `-` between vectors, and `*` and `/`
by a number, are gBASIC's own operators and work in SQL as they do in a program.

## 6. CAST, text and the dump

`CAST(text AS VECTOR)` reads as §2 does; `CAST(x AS VECTOR(N))` also checks the
dimension. `CAST(v AS TEXT)` is pgvector's text, `[0.6,0.8,0]`, each element the
shortest decimal that reads back as the same float32 (docs/vectors_design.md §3),
so the text round-trips exactly. `DUMP TO` writes a vector as
`CAST('[...]' AS VECTOR)` and a column as `VECTOR(N)`, so a restore puts back the
same bytes.

## 7. Search by distance

```sql
select id, body from chunk where acl = ? order by e <=> ? limit 10
```

The permission filter and the ranking are one statement, the property
stdlib/retrieval.bas holds on pgvector. Without an index (§9) it is EXACT: each
row passing the WHERE is measured and the k nearest kept. Ties come in the order
the rows were read, which is a sort's rule for ties everywhere in G-SQL.

`gsql.columns` describes a vector column as `vector(1536)` (or `vector`), and
`gsql.expect` holds a shape's `e:vector(1536)` to such a column; a column of any
dimension holds a lock of one, which still checks each row read.

**Measured** (1536 dimensions, this laptop, 2026-11-27; the same 10 answers and
distances as `nearest()` every time):

| rows | `ORDER BY e <=> ? LIMIT 10` in SQL | `nearest()` in memory | ratio |
|---|---|---|---|
| 20 000 | 0.17 s | 0.027 s | ~6x |
| 100 000 | 0.83 s | 0.13 s | ~6.4x |

**The first measurement was 0.51 s for 20 000**, and a profile of the owner
(callgrind) put the cosine itself at 8% of it. The rest was three readings of each
row done one float at a time, each a rule kept, none an answer changed:

- G-SQL's walker (src/gsql/v3.c) checked every float32 through a bounds-checked
  call (41%); it now checks the bounds once and reads the exponent bytes in place.
- The interpreter's reader made each element with a bounds-checked read (about
  30%); it now reads in place.
- `distance(a, b, "cosine")` took both norms to refuse a zero vector, then the
  cosine: three passes. a.a and b.b come from the same pass as a.b
  (`gb_vec_dot3`, the same doubles the norms square), so it is one pass now.

What is left is reading 6 KB a row out of the store and through the host. The cost
tier holds the ratio under 15x (`tests/run_gsql_vector.sh`). At a million rows exact
search is seconds, which is what part B is for.

## 8. Compatibility

The catalog names a column's kind as a word and writes a `dimension` field only for
`VECTOR(N)`, so a database with no vector column stores and reads as before. An
older build refuses a table with a vector column as *"schema does not parse
[CORRUPT]"*, as it refuses a duration column (gsql_duration_design.md §6); the
table is not corrupt.

serialize v3's tag 16 is the vector's own (docs/vectors_design.md §6). G-SQL's
walker (src/gsql/v3.c) holds a stored one to the interpreter's rules: a dimension
from 1, the bytes present for it, every element finite.

## 9. The approximate index (part B)

pgvector's spelling, so its DDL carries over:

```sql
create index chunk_e on chunk using hnsw (e vector_cosine_ops) with (m = 16, ef_construction = 64)
```

`vector_l2_ops` (the default), `vector_cosine_ops` and `vector_ip_ops` say which
distance the graph is built for; the column must be `VECTOR(N)`; `m` is 2 to 100
and `ef_construction` 4 to 1000 and at least twice `m`, pgvector's ranges. `USING
btree` is the index there always was; `ivfflat` and any other method are refused by
name. HNSW (Malkov and Yashunin) is pgvector's default: no training step, and it
takes rows as they come. The code is src/gsql/hnsw.c, kept by the table layer
(src/gsql/table.c) and searched by the SQL layer's ACC_ANN access path.

- **Stored in the engine, as rows are.** The index's tree holds, per row, `'v'` +
  row key, the vector's float32s, and `'n'` + row key, its level and its
  neighbours' row keys at each level (src/gsql/hnsw.h has the bytes); `"e"` is
  the entry point and `"c"` the count. Two keys a node, so an insert rewiring 16 to
  32 neighbours rewrites their short lists, not their 6 KB vectors. Every write is
  the row's own transaction: crash safety, rollback and a reader's snapshot are
  the store's, not the index's, and a reader searches the graph as of its snapshot.
- **Deterministic.** A node's level is drawn from a hash of its row key, not a
  random source, so the same rows written in the same order make the same graph on
  every machine (run_gsql_table's hnsw tier writes one workload into two
  databases and holds every answer equal).
- **Writes.** An insert links the node by the paper's neighbour heuristic
  (Algorithm 4, the pruned kept to fill) and shrinks a neighbour's list the same
  way past its cap (`2m` at level 0, `m` above). A delete removes the node and
  repairs its neighbourhood as pgvector's vacuum does: a search around the
  deleted vector, at each of its levels with the build's beam, finds the nodes
  near it, and each one pointing at it drops that edge and is refilled from what
  the search found, by the same heuristic. Its own neighbours alone are not
  enough, since an edge need not run both ways (a shrink drops back-links).
  Measured with three quarters of the rows deleted (run_gsql_table's hnsw tier,
  seeds 1-8, `m` 6 and 8): recall@10 0.90 to 1.00 this way; 0.62 to 0.96
  repairing only the deleted node's own neighbours, which was built first; 0.18
  with no repair. An edge left pointing at a deleted row is skipped when met. An update that changes `e`, or
  moves the row's key, is a delete and an insert; one that leaves `e` alone does
  nothing to the graph. A NULL vector is not in the graph. A zero vector is
  refused by a cosine graph (*"the vector is all zeros, which has no direction, so
  cosine index idx_cos cannot hold it"*), as `<=>` refuses one; pgvector stores it
  and answers NaN.
- **The planner** takes the index for `ORDER BY <its distance>(e, q) LIMIT k` over
  one table, ascending, `q` a literal, a parameter or a CAST of one, and EXPLAIN
  says `HNSW Search on chunk using chunk_e (nearest $1)`. Anything else (another
  distance, `DESC`, no `LIMIT`, another term ordered first, a join, grouping,
  `DISTINCT`, a window) is §7's exact search.
- **A query the graph cannot answer as the exact search would** (a NULL `q`, text
  that is no vector, another dimension, a zero `q` under cosine, `LIMIT NULL`) is
  the exact search, so its answer and its refusal are the exact search's own.
- **Filters, and never short.** The beam (`ef_search`, at least LIMIT + OFFSET) is
  searched and the WHERE applied as rows come; if fewer pass than asked, the beam
  doubles and the search goes on, and once it holds every node the graph reaches,
  every node is measured, then the rows with no vector follow (as NULL sorts
  last). So a narrow filter returns what the exact search returns, never fewer
  rows. pgvector before 0.8 answered short there; 0.8's iterative scan is this.
- **Ties.** Rows tying with the last one kept are taken too, so a second ORDER BY
  term (`, id DESC`) chooses among them as it would in the exact search (found by
  the differential: it chose the lower id).
- **The answer** is ranked by each candidate's exact distance, the same double §4
  gives; only WHICH rows are candidates is approximate.
- **`ef_search`** is the connection's attribute, as `max_recursion` is (G-SQL has no
  `SET`): `db.ef_search = 100`, 1 to 1000 as pgvector's `hnsw.ef_search`, unknown
  for the default of 40. A table named `ef_search` is therefore not reachable as a
  path, as one named `max_recursion` never was.
- **A dump** declares the index (`USING hnsw (e vector_cosine_ops) WITH (m = 16,
  ef_construction = 64)`) before the rows, so a restore builds the graph as they go
  in.

### 9.1 Measured (2026-11-27, this laptop, 1536 dimensions, 20 000 rows)

Two data sets, 30 queries each, recall@10 against `nearest()` over the same
vectors: **embedding-like** (clusters in a 32-dimensional latent space, projected
to 1536 by a fixed random matrix, as real embeddings lie near a low-dimensional
manifold) and **near-orthogonal** (sums of 500 random 1536-dimensional bases,
nearly orthogonal to each other, so a greedy descent has little slope to follow).
pgvector 0.7.2 on PostgreSQL 17 held the same rows with the same `m = 16,
ef_construction = 64`:

| | beam 40 | beam 100 | beam 200 |
|---|---|---|---|
| embedding-like, G-SQL | 1.0, 1.5 ms | 1.0, 3.0 ms | 1.0, 6.6 ms |
| embedding-like, pgvector | 1.0, 2.3 ms | 1.0, 3.1 ms | 1.0, 4.9 ms |
| near-orthogonal, G-SQL | 0.77, 1.2 ms | 0.87, 3.1 ms | 0.93, 5.8 ms |
| near-orthogonal, pgvector | 0.62, 2.4 ms | 0.82, 3.0 ms | 0.95, 3.9 ms |

- **Against the exact search**: §7's 0.17 s for 20 000 rows, so about 110x faster at
  the default beam on embedding-like data with every answer right.
- **The near-orthogonal set is hard for HNSW itself**: pgvector misses as G-SQL does,
  within run-to-run variation (the data is drawn afresh; G-SQL's beam-40 recall was
  0.47 in another draw). A missed answer is a real miss there, not a tie swapped:
  the 10th returned was up to 6x the 10th true distance. Raising `ef_search` is the
  remedy, as in pgvector.
- **Building is slower**: 64 s for 20 000 rows (3.2 ms a row, inserted with the
  index in place), pgvector 38 s (its graph outgrew `maintenance_work_mem` part
  way). Not optimised yet: every node met costs an engine read.

The runner's recall tier holds a smaller, seeded case (4000 x 256): recall@10 at
least 0.95 on embedding-like data at the default beam (measured 1.0), `ef_search 1`
below `ef_search 200` on near-orthogonal data (0.7 and 1.0), and the index at least
5x faster than the exact search (measured ~18x).

## 10. Tests

`tests/run_gsql_vector.sh`:

- **semantics:** `tests/gsql_vector/sql.bas`, 85 checks, each answer worked by hand
  from vectors chosen so every distance is exact, each refusal beside an accepted
  neighbour, precedence, CAST and text, `gsql.columns`, `gsql.expect`, and a dump
  and restore to the same serialize bytes.
- **oracle:** `tests/gsql_vector/oracle.py` writes a table and 24 queries for each
  of four seeds (3, 17, 64 and 1536 dimensions; copies for ties; every spelling; a
  filter or none), and judges each top-k exactly from its own arithmetic. Its two
  controls: a left-to-right sum must disagree with some answers, so exact comparison
  proves the specified order (not at 3 dimensions, where that order IS left to
  right); and most queries must rank differently under another metric, so a metric
  mapped to the wrong one would be seen.
- **parse:** `n<-1` is still `n < -1`, and the three operators lex without spaces.
- **cost:** exact search within 15x of `nearest()` over the same 5000 vectors in one
  process, and the same 10 answers.
- **hnsw:** `tests/gsql_vector/hnsw.bas`, 94 checks: the index declared and each
  refusal beside its accepted neighbour; when EXPLAIN shows the planner taking it;
  and a differential, the same rows in a table with the index and in one without,
  every answer identical (ids and distances) through both metrics, limits from 1
  past the rows, filters, OFFSET, NULL vectors, ties, a second ORDER BY key, the
  writes that keep the graph (updates, a moved key, deletes, inserts), a narrow
  filter on 300 rows that must widen a beam of 5, `ef_search`, and a dump and
  restore.
- **recall:** `tests/gsql_vector/recall.bas`, seeded, 4000 x 256, judged by the
  exact search and `nearest()` (which must agree on all 30 top-10s, or the oracle is
  in doubt), with the bounds of §9.1.
- **valgrind:** the semantics and hnsw fixtures.

`tests/run_gsql_table.sh`'s **hnsw** tier runs the graph alone against a model,
under ASan and UBSan (tests/gsql/table_test.c, `hnsw SEED`): inserts (a tenth with
no vector), vector updates, primary-key moves and deletes over an l2 graph and a
cosine one built over rows already there, the table layer's invariant check after
every committed batch (each row's vector in each graph byte for byte, nothing for a
row that is not there, the count right); then three quarters deleted and checked
again; `every` against the model's vectors, nearest first, ties by key, each
distance the kernel's; recall@10 at ef 64 at least 0.9; and the same workload into
a second database answering the same.

Sixteen perturbations of part B were each red, two of them only after a tier was
added for them: a deleted node's vector left behind, the count not lowered, no
back-links, a search stopping at its first node, `every` stopping short, levels from
a counter (not deterministic), an update skipping the graph, the delete repair off
(green until the three-quarters deletion; then recall 0.12, and 0.18 against the
search-based repair that replaced the first), the beam never widened
(green until the 300-row filter: 30 rows fit one beam), no tail of NULL rows, no
tie continuation, the planner ignoring the metric, `ef_search` ignored, the set of
rows yielded ignored, a zero vector admitted to a cosine graph, and `DESC` taken.

Eleven perturbations of part A were each red: `<=>` answering l2, `<#>` not negated, the
dimension check removed, the catalog dropping the dimension, the operators at `+`'s
level, `<=>` lexed after `<=`, an element's bits read wrongly, the zero-vector
sentence naming the wrong side, text no longer compared as a vector, the dump's
vector branch removed, and an array no longer converted into the column.

**Not tested:** G-SQL's walker refusing a stored NaN or infinity (§8). Nothing a
program can send makes one, since `vector()` and the interpreter's reader refuse
both first; the walker's check is a second wall, and the test that reaches it is a
C test of the walker, not yet written.

**Found on the way** (DOGFOOD 2026-11-27): a frame's unchecked error was lost to any
callee that caught one of its own, so a check, a re-raise and the end-of-program
report named the callee's handled error (fixed: `ErrorFrame.held`;
tests/error_model/held_error.bas); and Python 3.12's `sum()` over floats is
compensated, so the "left to right" control here and in increment 1's oracle was
not one (both now add in a loop).

# G-SQL: a `VECTOR` column, pgvector's distances, and search by distance

**Status:** increment 2 of the AI-engineering work (docs/ai_engineering_gaps.md
§2.2, §4), 2026-11-27. Part A, this page's §1-§8, is built: the column, the three
operators, the functions, and exact search (`tests/run_gsql_vector.sh`). Part B,
the approximate index (§9), is designed here and built next. Matthew's ruling on
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
stdlib/retrieval.bas holds on pgvector. Without the index of §9 it is EXACT: each
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

## 9. The approximate index (part B, designed here, not built)

pgvector's spelling, so its DDL carries over:

```sql
create index chunk_e on chunk using hnsw (e vector_cosine_ops) with (m = 16, ef_construction = 64)
```

`vector_l2_ops`, `vector_cosine_ops` and `vector_ip_ops` say which distance the
graph is built for; the column must be `VECTOR(N)`. HNSW (Malkov and Yashunin) is
pgvector's default and the usual choice: no training step, and inserts as rows come.

- **Stored in the engine, as rows are.** A tree `\0h` + `be64(index id)` holds a
  node per row, keyed by the row key: its level, its neighbours' row keys at each
  level, and a copy of the vector, so a search reads no row it does not answer.
  The entry point is one key of its own. Every write is the row's own
  transaction, so crash safety, rollback and a reader's snapshot are the store's,
  not the index's: a reader searches the graph as of its snapshot.
- **Deterministic.** A node's level is drawn from a hash of its row key, not a
  random source, so the same rows inserted in the same order make the same graph
  on every machine, and a recall measured once is the recall.
- **Deletes** remove the node; an edge to a missing node is skipped when met and
  dropped when that list is next rewritten. An update of `e` is a delete and an
  insert.
- **The planner** uses the index for `ORDER BY <the index's distance>(e, q) LIMIT
  k` with `q` a constant or parameter, ascending, and EXPLAIN says so. Anything
  else (another distance, `DESC`, no `LIMIT`) is the exact search of §7.
- **Filters.** The beam (`ef_search`, default 40, at least k) is searched, then
  the WHERE applied; if fewer than k rows pass, the beam doubles and the search
  continues, up to the whole graph, so a narrow filter returns k rows (or every
  row that passes), never fewer silently. This is pgvector 0.8's iterative scan,
  and the place pgvector before it answered short.
- **The answer** is ranked by each candidate's exact distance, the same double
  §4 gives, ties by row key; only WHICH rows are candidates is approximate.
- **`ef_search`** is a connection option (`gsql.connect(path, { ef_search: 100 })`),
  since G-SQL has no `SET`.

**Tested against the exact search as its oracle**: the same query with the index
and with `gsql_sql_force_scan`, recall@10 by query over clustered and uniform data,
a floor asserted and the distribution printed; the graph after a crash mid-insert
(the store's crash harness); deletes and updates against a model; and the speed
at 100 000 x 1536 against §7's 0.83 s.

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
- **valgrind:** the semantics fixture.

Eleven perturbations were each red: `<=>` answering l2, `<#>` not negated, the
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

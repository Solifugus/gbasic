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

**Measured** (20 000 rows of 1536 dimensions, this laptop, 2026-11-27): MEASUREMENTS
PENDING.

## 8. Compatibility

The catalog names a column's kind as a word and writes a `dimension` field only for
`VECTOR(N)`, so a database with no vector column stores and reads as before. An
older build refuses a table with a vector column as *"schema does not parse
[CORRUPT]"*, as it refuses a duration column (gsql_duration_design.md §6); the
table is not corrupt.

serialize v3's tag 16 is the vector's own (docs/vectors_design.md §6). G-SQL's
walker (src/gsql/v3.c) holds a stored one to the interpreter's rules: a dimension
from 1, the bytes present for it, every element finite.

## 9. The approximate index (part B, next)

PENDING: designed after part A's measurement.

## 10. Tests

`tests/run_gsql_vector.sh`. PENDING.

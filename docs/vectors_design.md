# Vectors: a packed value and its arithmetic

Status: **increment 1 (2026-11-27): the value, its arithmetic, its lock.**
G-SQL's vector column, distance operator and ANN index are increment 2
(docs/ai_engineering_gaps.md §2.2). This note is the contract for both.

## 1. Why a value kind

Measured 2026-11-27, before anything was designed: a cosine over 1536
dimensions written as a gBASIC loop over two arrays takes **0.68 ms**,
interpreted or compiled (`GBASIC_COMPILE=all`, 0.67 ms), so ranking 100,000
stored embeddings takes **68 seconds**. An array of 1536 numbers is 1536
`Value`s of 40 bytes, 61 KB; the same numbers packed as float32 are 6 KB.
Embeddings come as float32 from every provider, and pgvector stores them so.

So a vector is a value kind (docs/ai_engineering_gaps.md question 1, taken as
proposed): `VALUE_VECTOR`, a dimension and that many float32s, contiguous.
Like a regex it is **immutable and shared**: copying one is a reference
count, which is what lets an array of 100,000 of them be passed and stored
without copying 600 MB.

## 2. Making one, and back

- `vector(x)`: from an array of numbers, from the text `[1, 2, 3]`
  (pgvector's text form, and a JSON array), or from a vector (itself).
  Each element is refused by its position when it is not a number, or is
  not finite, or does not fit a float32. An empty vector is refused:
  nothing measures a distance to it.
- `array(v)`: its numbers, as an array (the strict conversion `array`
  already is).
- `vector_bytes(v)` and `vector_from_bytes(b)`: little-endian float32, four
  bytes an element. This is OpenAI's `encoding_format: "base64"` once
  base64-decoded, and pgvector's binary form after its header.
- A value read back is the float32, widened: `vector([0.1])[0]` is
  0.10000000149011612. That is what the vector holds; printing shows the
  shortest decimal that gives back the same float32, `0.1`.

## 3. Reading one

`len(v)` is its dimension. `v[i]` reads an element (from 0, refused out of
range as an array is). `for each x in v` binds each element. `v[i] = x` is
refused: a vector is never changed in place, so a shared one cannot change
under another holder; build a new one.

`print v` shows `vector[0.1, 0.2, 0.3]`, every element. `type(v)` is
`"vector"`. `=` and `!=` compare dimension and every element; `<` is refused
(vectors have no order). `json_encode(v)` writes a JSON array of numbers,
because that is how every service that takes an embedding takes it; it
decodes as an array, not a vector.

## 4. Arithmetic

| Builtin | Answer |
| --- | --- |
| `dot(a, b)` | the inner product |
| `norm(v)` | the Euclidean length |
| `cosine(a, b)` | the cosine SIMILARITY, from -1 to 1 |
| `distance(a, b[, metric])` | `"l2"` (default) Euclidean; `"cosine"`, 1 - cosine; `"ip"`, the NEGATED inner product, so smaller is nearer in all three (pgvector's `<->`, `<=>`, `<#>`) |
| `normalize(v)` | `v` divided by its norm |
| `nearest(q, vs, k[, metric])` | the `k` nearest of an array of vectors, as records `{ index, distance }`, nearest first, a tie by the lower index |

`a + b` and `a - b` (one dimension), `v * n`, `n * v` and `v / n` are
elementwise and give a vector. Adding vectors of two dimensions, a vector
and a number, or anything else is refused by what it got.

**The sums are in double**, over the float32 elements, four running sums
taken in turn (element i to sum i mod 4) and added `(s0 + s1) + (s2 + s3)`.
Four sums let the processor overlap the additions; double keeps 1536 terms
from losing the digits a float32 sum would. The order is fixed so an
answer is the same on every machine, and the test's oracle sums in the same
order. A cosine or a cosine distance involving a zero vector is refused: it
has no direction, and pgvector's NaN there sorts nowhere sensible.

Two vectors of different dimensions are refused everywhere, naming both
dimensions; in `nearest`, naming the element.

## 5. The lock: `e:vector(1536) = ...`

A lock `x:vector` converts what is assigned as `vector(x)` would, and
`x:vector(N)` also refuses a dimension other than N. So a shape's field
`embedding:vector(1536)` checks an embedding at the boundary, and
`json_schema` gives it as an array of exactly N numbers.

**This was a grammar change, found by building it.** A lock's argument was
a name only (`money(USD)`); `vector(1536)` was a syntax error. The rule
`type_lock: IDENT LPAREN NUMBER RPAREN` is added, with zero conflicts, now,
because the core grammar freezes at 1.0. Only `vector` takes a number;
`money(2)` and `number(3)` are refused by name.

## 6. On disk and between processes

`serialize` gains tag 16, `SER_VECTOR`: the dimension (LEB128) and the
float32s, little-endian. Appended, so every payload ever written still
reads; an older binary refuses a payload holding one by its tag, as it
refuses any tag it does not know. An actor receives a vector as a vector.
A G-SQL index key refuses one (a vector has no order to index by), until
increment 2 gives the column.

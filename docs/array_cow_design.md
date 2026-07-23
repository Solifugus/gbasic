# Copy-on-Write Arrays — design & implementation

Status: **implemented** (2026-07-23). Core-runtime work package, landed as its
own focused commit ahead of resuming NAP-12.

## Problem

gBASIC arrays presented uniform deep **value semantics** but stored their
elements in a bare `{Value *items; size_t count}` with no sharing. Three copies
followed from that:

1. **Every rvalue read deep-copied the whole array.** `env_get` ends in
   `value_copy(symbol->value)`, and `value_copy` on an array recursively copied
   every element. So `b = a`, `a[i]`, and passing an array to a function were all
   O(n); a `while` loop indexing an array was O(n²).
2. **`append` mutated in place but then returned a full deep copy** (to give the
   expression a value), so a build loop `a = append(a, x)` was O(n²). The bare
   statement form `append(a, x)` — 669 of ~690 call sites in the tree — paid that
   copy for a result nobody read.
3. Writes and `for each` were already O(1)/O(n): the lvalue path borrowed a
   mutable pointer and `for each` snapshotted the container once.

Measured before: reading 200 elements of a 128k array ≈ 1.1s; building a 25k
record array ≈ 88s. These were language-wide pathologies (analytics, ETL,
finance, function calls, Studio), not a DataGrid symptom.

## Observable semantics (unchanged)

Arrays remain value types. A copy is independent; mutation of one copy never
affects another, at any depth:

```basic
a = [1,2,3]      b = a       b[0]=99      ' a == [1,2,3], b == [99,2,3]
n2 = n1          n2[i][j]=x               ' n1 untouched
r2 = r1          r2.rows[i]=x             ' r1 untouched
```

Equality stays structural (`[1,2,3] = [1,2,3]` is true). Serialization,
reflection, actor messaging, and `for each` are byte-for-byte unchanged. The
whole golden suite passes unmodified — this is an optimization, not a semantic
change.

## Representation

```c
typedef struct ArrayStorage {
    size_t ref_count;   /* live VALUE_ARRAY handles pointing here */
    size_t count;       /* logical length */
    size_t capacity;    /* allocated slots (>= count); enables amortized append */
    Value *items;
} ArrayStorage;
```

`Value.as.array` became `{ ArrayStorage *store; }` — a handle, not an owner. The
model mirrors the record `ValueCell` refcount/fork mechanism already in the
runtime, but at **whole-array granularity** (one refcount for the buffer), not
per-element: an array needs one shared buffer, whereas a record shares each of
its ~N fields' cells. `capacity` is never user-visible and never serialized.

## Invariants

1. Every live `VALUE_ARRAY` owns one reference to a valid `ArrayStorage`.
2. `value_copy(array)` is `store->ref_count++` — O(1).
3. `value_free(array)` is `array_storage_release` — decrement, and at zero free
   every element then the buffer then the store.
4. Reads never detach.
5. `capacity == ` the number of slots actually allocated in `items` (so append
   can trust it). Every reallocation keeps this true.

## The write barrier

One helper, `array_ensure_unique(Value *array)`, is the single detach point:

```c
if (store->ref_count == 1) return;          /* uniquely owned: mutate in place */
/* else: deep-copy each element via value_copy into a fresh unique store,
   drop the old reference, repoint the Value. O(n), paid once. */
```

Every mutating operation routes through it before touching the store. There are
no ad-hoc refcount checks scattered elsewhere. Call sites:

- **Indexed write** `a[i] = x` (the lvalue-assign INDEX handler): detach the
  container before the element write (skipped when the value is unchanged).
- **Nested lvalue resolution** `resolve_lvalue_ref` INDEX case: detach each array
  container it descends through before handing out a mutable element pointer.
  This is what isolates `b[i][j] = x` and `b[i].field = x` — the recursion
  detaches every shared level along the path, and it composes with the record
  `cell_fork_for_write` barrier (record fork copies the array handle = share;
  the array barrier then privatizes it).
- **The mutating builtins** — `append`/`prepend` (value+ref), `insert`,
  `remove`, `remove_value`, `take_first`/`take_last`, `reverse`, `sort`,
  `unique`, and the webserver's internal `webserver_array_remove` — each call
  `array_ensure_unique` at entry.

Because `value_copy` is now O(1), `append`'s historical "mutate in place, then
return a copy of the result" becomes free and correct: the returned value simply
shares the mutated store (refcount bump), and any later mutation of either the
variable or the returned value detaches. The mixed semantics that used to be a
pathology are now the cheap path.

## Append & capacity

`append`/`prepend` on an assignable path (the hot build-loop path) detach, then
`array_storage_reserve(store, count+1)` grows capacity by doubling (min 4) —
amortized O(1). Other mutators that change length keep `capacity == allocation`:
`insert` reserves like append; `remove`/`take` shift down and simply decrement
`count` (keeping the buffer, since `capacity >= count` stays valid); `unique`
compacts and sets `count`. `reverse`/`sort` reorder in place.

## Ownership & foreign values

Detach copies elements with `value_copy`, so reference-semantic elements
(GObjects, boxed/GVariant, actors, files, DB handles, functions) are refcounted,
never byte-duplicated or double-freed — identical to how they behaved under the
old deep copy. `array_storage_release` frees each element exactly once at the
last reference. Verified leak-free under valgrind with GObject elements across
300 detach rounds.

## Concurrency

Sharing is strictly intra-process. Actors are `fork`+`exec` isolated and exchange
values via `serialize`/`deserialize`, which rebuilds arrays from bytes into fresh
uniquely-owned stores — a store is never shared across a process boundary. The
interpreter is single-threaded, so `ref_count` needs no atomics.

## Complexity: before → after

| Operation                     | Before   | After              |
| ----------------------------- | -------- | ------------------ |
| assignment / copy (`b = a`)   | O(n)     | **O(1)**           |
| argument passing (read-only)  | O(n)     | **O(1)**           |
| indexed read (`a[i]`)         | O(n)     | **O(1)**           |
| indexed write, unique         | O(1)     | O(1)               |
| first write to a shared array | O(1)*    | O(n) detach (once) |
| append, unique w/ capacity    | O(n)     | **amortized O(1)** |
| build loop of n appends       | O(n²)    | **O(n)**           |
| iteration (`for each`)        | O(n)     | O(n)               |

\* previously O(1) only because copies were already fully independent (eager),
which is exactly what made assignment O(n).

Measured after (was in parentheses): 200 reads of a 128k array 0.01s (1.1s);
build 25k record array 0.02s (88s); build 100k 0.06s. Assignment and read are
flat across n (16k/64k/128k all ≤0.01s). An identity fast path in
`value_storage_equal` (two arrays sharing a store are equal) keeps an unchanged
reassignment O(1).

## Files changed

- `src/eval.c` — `ArrayStorage` type; `Value.as.array` → handle;
  `array_storage_new/reserve/release`, `array_ensure_unique`; `value_copy` /
  `value_free` array cases; barriers in the lvalue-assign and
  `resolve_lvalue_ref` INDEX cases and in all array mutators; `value_storage_equal`
  identity fast path; mechanical `.as.array.items/count` → `.store->...` across
  all Value-context sites.
- `src/modules/xml.c` — same mechanical field-access update (it is `#include`d
  into eval.c and builds node-record arrays).
- `examples/array_cow_test.bas` (+`.out`), wired into `tests/run_examples.sh`.

## Tests & evidence

- Semantic golden `array_cow_test` covers assignment isolation, three-alias
  detach, nested arrays, arrays of records, records-of-arrays, function args
  (read-only + callee mutation), return values, append (bare/assigned/aliased/
  growth), insert/remove/sort/reverse, iteration, equality, serialization.
- Full regression boundary green with zero rebaselines, including the
  `watcher_mutator_notification_test` golden that pins the mutate-in-place +
  change-detection behavior of the whole mutator family.
- Valgrind: 0 leaks / 0 errors over a 300-round mixed stress (unique, shared
  detach, nested, append growth, function passing, serialization, GObject
  elements, out-of-range error path), the golden, and a 50k append-build +
  detach.

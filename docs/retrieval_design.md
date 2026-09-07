# `retrieval`: the permission filter and the ranking are one query

**Status:** Shipped (2026-09-07). `stdlib/retrieval.bas`,
`tests/run_retrieval.sh`. Step 7 of
[the AI reference proposal](gbasic_ai_reference_and_primitives.md) (§1.7).

## 1. The defect this exists to prevent

The obvious implementation of permission-aware search ranks first and then
drops what the caller may not see:

```sql
select * from (
    select id, text, vec <-> $1 as distance from chunks
    order by distance limit 10
) ranked
where ranked.acl && $2          -- WRONG
```

It fails in a way that looks like an ordinary empty result. A user with narrow
permissions asks a question, the ten globally nearest chunks all belong to
someone else, every one is dropped, and they are told nothing matched. Nothing
errors. Their own best matches were never considered.

Putting the predicate in the `WHERE` makes the database narrow first and rank
what remains:

```sql
select id, source, text, vec <-> $1::vector as distance
from chunks
where acl && $2
order by distance
limit $3
```

That is the whole library. `tests/run_retrieval.sh` asserts it as a
**difference**: a corpus where the three nearest chunks are ones the asking
user may not see, and they still get their own top-2 — with the control that a
user who *may* see them gets those three instead. Without the control, "the
narrow user got two rows" is equally satisfied by an ACL that does nothing.

## 2. The surface

| Call | What it does |
| --- | --- |
| `retrieval.schema(table, dimensions)` | the DDL, as statements to run |
| `retrieval.create(db, table, dimensions)` | run them |
| `retrieval.store(db, table, chunks)` | upsert, keyed by content hash → `{written, skipped}` |
| `retrieval.search(db, table, qvec, groups, limit)` | the query |
| `retrieval.query_text(table)` | what `search` runs, for a caller that wants to read it |

A chunk is `{ id, source, text, hash, acl, vec }`. The ACL is a `text[]` and the
predicate is `acl && $2`, the array-overlap operator — which needs `pg` to send
a native array, built earlier in this same sequence of work.

`schema` **returns** statements rather than running them, so a caller can see
what is about to happen to their database and a migration tool can own the
running. The ACL column is GIN-indexed, because it is what every query filters
on.

`query_text` is public because **a permission predicate nobody can read is a
permission predicate nobody audits** — and `search` calls it rather than
building the same SQL again. That is not tidiness: written as two separate
string builds, a perturbation that changed only `search` left the structural
tier passing. Two representations of one query drift; one source cannot.

## 3. Content-hash keying

`store` upserts with `where table.hash is distinct from excluded.hash`, so a
chunk whose content has not changed is left alone and reported as `skipped`.
That is what makes an indexer resumable: a crash mid-run costs the batch in
flight and nothing else.

## 4. Identifiers are refused, not escaped

A table name cannot be a bound parameter, so it is validated against a narrow
rule — lowercase letters, digits, underscore, not starting with a digit — and
anything else is **refused**. Escaping an identifier is a decision about a
quoting dialect; refusing one is a fact. This is the rule `dbframe` already
follows, for the same reason.

## 5. `llm.embed`, which this needs

Shipped alongside (`docs/llm_design.md`). **Batch is the primitive** and one
text is the special case: an embedding API charges and rate-limits per request,
so a chunked document embedded one chunk at a time is the same work at many
times the cost.

Two things it refuses rather than guesses. **An embedding model is a different
model from a chat model**, so `embed` requires `llm.with_embed_model` rather
than reaching for `m.model` and letting the provider answer with an error about
a model that does not do this. And **Anthropic has no embeddings endpoint** —
refused by name, pointing at `llm.openai` or `llm.local`.

**The order is what goes silently wrong.** The API returns a `data` array whose
entries carry an `index`, and nothing in the protocol promises they arrive
sorted. Read positionally, every chunk is paired with another chunk's vector —
and the result still looks like a list of vectors, the store still fills, and
retrieval returns the wrong documents forever. Vectors are placed **by index**,
and the suite feeds a deliberately shuffled response.

## 6. Not built

- **No chunking.** Chunk boundaries are a policy, as §1.6 says — they depend on
  the document, the model and what a citation should point at.
- **No connectors.** `changed_since`, `acl_of` and the rest are an interface the
  application implements against its own sources.
- **No `rank` primitive.** pgvector does filter-then-rank in SQL, so the
  candidate-mask primitive Part 2 item 5 argued for has no consumer.
- **No re-ranking or hybrid search.** Both are real and both need a second
  measurement to justify a shape.

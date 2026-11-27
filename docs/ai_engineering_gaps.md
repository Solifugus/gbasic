# The AI engineer in gBASIC: what exists, what is missing

Status: **survey, 2026-11-26.** Increment 1, vector values and math (§2.1),
built 2026-11-27 (docs/vectors_design.md, run_vectors), question 1 taken as
proposed (a value kind). Increment 2 part A, G-SQL's `VECTOR(N)` column,
pgvector's operators and functions and exact search, built the same day
(docs/gsql_vector_design.md, run_gsql_vector), question 2 ruled "both"; the HNSW
index (part B) is designed there, not built. The rest is not built. Written for the 1.0
order agreed with Matthew on 2026-11-26: the AI-engineering capabilities are
built FULLY before 1.0 ("It will be important and i have not given it enough
weight"), because building them is how the needs that touch the frozen core
grammar get found. That is how the performance work found typed `for each`
and shapes. "Fully" means a reference application, Steward grown, built all in
gBASIC: ingestion, permissioned search, an agent with tools, evaluated, and
measured against the same application on pgvector.

The 1.0 freeze covers the core language's syntax and grammar only. Builtins,
native modules, the runtime, the compiler and G-SQL's own syntax may keep
growing after 1.0 (Matthew, 2026-11-26). So each gap below is marked by what
it would touch: **grammar** (must land before 1.0), **native** (C, could
land later but is wanted now), or **library** (gBASIC code).

## 1. What exists (measured 2026-11-26)

| Piece | Where | What it does |
| --- | --- | --- |
| Model calls | stdlib/llm.bas | Anthropic Messages, OpenAI chat, `local()` for Ollama/vLLM; one canonical part-based transcript; keyed replay for tests; retry on 429/5xx with backoff and Retry-After; token `usage` per call |
| Structured answers | llm.ask_json (llm.bas:1333) | strips fences, one corrective retry; no provider JSON mode, no schema |
| Embeddings | llm.embed (llm.bas:719) | batch, OpenAI-compatible only |
| Tools | stdlib/tools.bas | one declaration generates the JSON Schema a model sees (`param_schema`, :251) and checks each call against it; errors become results; an actor pool |
| Agent loop | stdlib/agent.bas | a conversation is a value; `apply(run, event)` is a pure step returning actions; approval gate for mutating tools; usage summed |
| MCP | stdlib/mcp.bas | publish (stdio, Streamable HTTP, signed sessions) and consume (`connect`, `call`) |
| Retrieval | stdlib/retrieval.bas | permission-filtered nearest-neighbour search on **pgvector**: the ACL filter and the ranking in one WHERE (`acl && $2 ... order by vec <-> $1`); no ANN index created |
| Ingestion | pdf.bas (+ ocr.bas), xlsx, csv, xml and `xml.parse_html`, mail.parse | documents into text and tables |
| Statistics | stats.bas, scoring.bas | bootstrap, t-tests, effect sizes, kappa, AUC |
| Test data | fake.bas | seeded, deterministic generators |
| Non-blocking HTTP | the `http` module | bytes delivered as they arrive, through `http.events`; frames nothing (reference.md:4038) |
| Reference app | examples/steward | ask, approve, read a run; model answers from replay fixtures; non-streaming only |

That is a sound base, and most of it is library code already.

## 2. What is missing

### 2.1 Vectors (native)

Vectors are gBASIC arrays of numbers today, sent to Postgres as the text
`"[a,b,...]"`. There is no vector math in C: `matrix.vec_dot` is a gBASIC
loop over a list, and no norm or cosine exists. Retrieval cannot leave
pgvector without this.

- **A packed vector value**: float32, contiguous, a fixed dimension. It needs
  no grammar. It is a value kind made by a builtin (`vector([..])`, from an
  embedding call, from bytes), the way regex and money are value kinds with
  no syntax of their own. Arithmetic is through builtins: `dot`,
  `cosine`, `norm`, `distance(a, b, "l2"|"cosine"|"ip")`, `normalize`.
- **Measure first**: a 1536-dimension cosine over 100,000 vectors in a gBASIC
  loop against the C builtin. The gap justifies the work, so it is measured
  before the design is fixed.

### 2.2 Vectors in G-SQL (G-SQL syntax, native)

- A `vector(N)` column kind; serialize v3 grows a tag for it (an additive
  format change, so old databases still read).
- A distance operator in SQL (pgvector's `<->`, `<=>`, `<#>` are the spellings
  people know), so **the permission filter and the ranking stay one
  statement**, the property retrieval.bas exists to hold.
- An approximate nearest-neighbour index (HNSW is the default choice) in the
  storage engine, with exact search as its oracle: recall against the brute
  force on the same rows, by query, is the test.
- G-SQL paths get it free: `kb.chunk[.acl has p] order by distance(...)`.

### 2.3 Full-text and hybrid search (G-SQL, native)

Embeddings miss exact names, codes and numbers, which business documents are
full of. A full-text index with BM25 ranking, and a documented way to fuse it
with the vector ranking (reciprocal rank fusion is the usual one), in one
query. The oracle is a BM25 written separately (Python's rank_bm25 or a
hand-worked corpus).

### 2.4 Streaming model output (library, over native http)

`http` already delivers bytes as they arrive; what is missing is above it:
an SSE parser (events from partial lines, as `http.read` says the caller
must reassemble), and `llm.stream(m, transcript)` delivering text deltas and
tool-call parts as events, for both wire formats. A web handler forwarding
them to a browser is what Steward's chat needs. **Grammar check**: a stream
consumed by `watch` or by `for each` over a lazy source (`csv.records`
already is one) needs no new syntax; to be confirmed by building it.

### 2.5 Token counting (native or library: measure)

nlq.bas estimates 2.5 characters a token. A context budget needs the
model's count. Two routes: the providers' count endpoints (exact, a network
call), and a local BPE tokenizer for OpenAI-compatible models (fast, its
vocabulary a data file). Whether the local one must be C is a measurement:
tokenizing a 100-page document in gBASIC against the C version.

### 2.6 Structured output (grammar: shapes)

`ask_json` hopes. A shape (docs/shapes_design.md) generates the JSON Schema
sent as the provider's response format, and the reply is converted into the
shape, each field refused by name. **This is the one item here that touches
the core grammar**, and it is why shapes come before these increments.

### 2.7 Ingestion and chunking (library)

The readers exist; what is missing is the step after: chunking by structure
(headings, pages, paragraphs, tables kept whole), by size with overlap, with
each chunk's provenance (file, page, byte range) carried, the way finio
carries a value's source. Deduplication by content hash exists in
retrieval.store.

### 2.8 Evaluation (library)

No harness. Wanted: a dataset of cases, a run of each through a pipeline,
scored by exact match, by a function, or by a model as judge (with the
judge's agreement with people measured by kappa, which stats.bas has),
results compared between two versions with a paired test. Replay makes a run
reproducible.

### 2.9 Tracing and cost (library)

Nothing records a model call's latency, tokens or cost, or the spans of an
agent run. Wanted: a trace per run (each model call, tool call and retrieval
with its time, tokens and cost), written as data, and a price table per model
that the caller supplies (prices change; the library must not bake them in).

### 2.10 Smaller gaps found on the way

- Embeddings for Anthropic-format providers raise (llm.bas:731): Anthropic
  has no embedding endpoint, so this is a fact to document, not a gap.
- Provider prompt caching (`cache_control`) is not sent: an option on `chat`.
- tools' schemas are a subset (no nested objects or enums): shapes fix this
  too, since a tool's parameters can be a shape.
- mcp's client takes an event-stream reply's first event whole; streaming
  covers it.

## 3. The reference application

Steward grows, in order, each step usable:

1. **Ingest** a folder of a business's documents (PDF with OCR, xlsx, mail),
   chunked with provenance, embedded, stored in G-SQL with each chunk's ACL.
2. **Search** with the permission filter and the ranking in one statement,
   hybrid (vector + BM25).
3. **Answer** through the agent with tools, streamed to a chat page, citing
   chunks, with approval for mutating tools (already built).
4. **Evaluate**: a question set with expected answers and sources; recall of
   the right chunk, answer quality judged and checked against people.
5. **Measure against pgvector**: the same corpus, questions and model, with
   retrieval on Postgres+pgvector (retrieval.bas as it is): recall, latency at
   p50/p99, memory, and the lines of code in each. The claim to earn is that
   gBASIC alone is as good, or exactly how far it is not.

## 4. Proposed order of increments

1. Vector values and math (2.1), measured first.
2. G-SQL vector column, distance operator, then the ANN index (2.2).
3. Streaming (2.4), and Steward's chat over it.
4. Shapes (already ordered before these) give structured output (2.6).
5. Full-text and hybrid (2.3).
6. Token counting (2.5), after measuring.
7. Chunking (2.7), evaluation (2.8), tracing and cost (2.9).
8. The reference application and the pgvector comparison (§3), finished last
   because it is the measurement of all of the above.

**Added 2026-11-27 (Matthew):** after increment 2's HNSW index, before
increment 3, an **analytics benchmark and design note**: numeric speed on large
arrays is the gap against Python that matters most to him ("pandas has too big
of a learning curve"). 1 to 10 million rows (CSV load, filter, a computed money
column, group-by, join, sort, a window) as a gBASIC loop, through frame.bas,
in G-SQL, and in pandas and polars for reference; then a note on where the time
goes and a packed COLUMN value kind (the vector kind generalized: float64, exact
int64, money, date, boolean, text; ordinary gBASIC expressions applying to whole
columns). Whether it is built before 1.0 turns on one question the note must
answer: whether it needs core grammar (an inline predicate or expression form).

## 5. Questions for Matthew

1. **The vector value**: a new value kind made by builtins, with no syntax
   (proposed), or plain arrays with C-speed builtins over them (simpler,
   slower: every call converts)?
2. **pgvector's operators** (`<->`, `<=>`, `<#>`) in G-SQL, so SQL people
   already know reads the same, or functions (`distance(a, b)`)? (proposed:
   both: the operators, and functions that name them)
3. **Prices** for cost tracking: supplied by the caller (proposed), or a
   table shipped and dated?
4. **Local tokenizer**: worth a vocabulary file in the tree, or provider count
   endpoints only to start?

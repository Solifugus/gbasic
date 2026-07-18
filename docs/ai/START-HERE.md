# START HERE — writing gBASIC as an AI agent

You are about to write or modify **gBASIC** source (`.bas` / `.gb`). gBASIC looks
like BASIC but diverges from QBasic/VB intuition in ways that will silently bite
you. Read this layer first; it is short by design.

## Reading order

1. **`UNLEARN.md`** — the surprises. "gBASIC is not QBasic/VB": every behavior that
   contradicts common BASIC intuition, bluntly stated with a minimal snippet, plus
   negative knowledge (things other BASICs have that gBASIC does not, and the
   gBASIC alternative). **Read this before writing any gBASIC.**
2. **`COOKBOOK.md`** — the blessed idioms. One correct, runnable way to do each
   common thing (iterate an array, build a record, handle bad input, call a
   module). Every snippet points at a real file under `examples/`/`tests/`.
3. **`ERRORS.md`** — the diagnostic/runtime-error catalog: what each error domain
   and code means, its typical cause, and the fix.

For the full **human** language reference (every construct and builtin library at
reference depth), see `docs/reference.md` and `README.md`. This `docs/ai/` layer
does not duplicate that content — it is the agent-facing distillation: what will
surprise you, the one right idiom, and how to read an error.

## The two standing rules (also in CLAUDE.md / AGENTS.md)

- **Before writing gBASIC**, follow this layer (UNLEARN first).
- **When you work around a gBASIC limitation or surprise**, append an entry to
  `/DOGFOOD.md` (root) using its template *before continuing*. The friction log is
  how these files stay honest and how the language earns its next fixes.

## Status

`UNLEARN.md`, `ERRORS.md`, and `COOKBOOK.md` are **placeholders** in Phase D1 and
are filled with content in Phase D3 (see `PLAN.md`). Until then, the authoritative
surprise list is `/DOGFOOD.md`'s D0 seed and the human reference above.

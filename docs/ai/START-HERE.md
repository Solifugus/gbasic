# START HERE — writing gBASIC as an AI agent

You are about to write or modify **gBASIC** source (`.bas` / `.gb`). gBASIC looks
like BASIC but diverges from QBasic/VB intuition in ways that will silently bite
you. Read this layer first; it is short by design.

## Reading order

0. **`RULES.md`** (or `gbasic --rules`) — the card: fifteen traps a model meets
   first, one line and one example each, every example run by a test. Two
   minutes; read it before anything else.
1. **`UNLEARN.md`** — the surprises. "gBASIC is not QBasic/VB": every behavior that
   contradicts common BASIC intuition, bluntly stated with a minimal snippet, plus
   negative knowledge (things other BASICs have that gBASIC does not, and the
   gBASIC alternative). **Read this before writing any gBASIC.**
2. **`COOKBOOK.md`** — the blessed idioms. One correct, runnable way to do each
   common thing (iterate an array, build a record, handle bad input, call a
   module). Every snippet points at a real file under `examples/`/`tests/`.
3. **`ERRORS.md`** — the diagnostic/runtime-error catalog: what each error domain
   and code means, its typical cause, and the fix.
4. **`GSQL.md`** — before writing anything that uses G-SQL, gBASIC's own
   database (`load gsql`): the surprises (SQL in blocks, `:name` parameters, no
   conversion in comparisons, typed columns), catching refusals by code, the SQL
   there is and what is refused.

For the full **human** language reference (every construct and builtin library at
reference depth), see `docs/reference.md` and `README.md`. This `docs/ai/` layer
does not duplicate that content — it is the agent-facing distillation: what will
surprise you, the one right idiom, and how to read an error.

## Ask the binary, then check, before you run

The interpreter answers for the build you are using, which a page may not:

- `gbasic --doc NAME` -- a builtin's, module verb's (`process.start`) or
  library function's (`dates.days_between`) signature, meaning and example.
  Ask it instead of guessing a signature from another language.
- `gbasic --check FILE` -- parse the file and everything it loads, make every
  check that comes before a statement runs, and run nothing. One JSON line per
  diagnostic on stdout; none means clean. A diagnostic whose rewrite is
  certain carries `"fix"` (the bytes to replace and the text to put there):
  apply it. Write, `--check`, fix, repeat; then run.
- `gbasic --rules` -- the rules card, as this build has it.
- `gbasic --explain CODE` -- what a code in a diagnostic, an `error.code` or a
  warning means, and for a warning how to silence or escalate it.

## The two standing rules (also in CLAUDE.md / AGENTS.md)

- **Before writing gBASIC**, follow this layer (UNLEARN first).
- **When you work around a gBASIC limitation or surprise**, append an entry to
  `/DOGFOOD.md` (root) using its template *before continuing*. The friction log is
  how these files stay honest and how the language earns its next fixes.

## Keeping it honest

`/DOGFOOD.md` (repo root) is the append-only friction log — the raw stream these
files distill. When you hit a new surprise or workaround, add a DOGFOOD entry.
`COOKBOOK.md`'s file references are enforced by `tests/run_docs_gate.sh` (also run
by `make dev`): every idiom must point at a real, suite-wired file.

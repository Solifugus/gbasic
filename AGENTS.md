# AGENTS.md

Entry point for Codex (and other coding agents). Thin by design — build, test,
house rules, pointers. The deep architecture guide is `CLAUDE.md`; do not
duplicate language content here.

## What this is

gBASIC is an experimental BASIC-family language: a tree-walking interpreter in
C11. The single binary `gbasic` lexes, parses, and evaluates `.bas`/`.gb` files.
See `README.md` for the feature surface, `docs/` for design/reference, and
`CLAUDE.md` for the architecture map (where the weight sits: `src/eval.c` is the
whole runtime).

## Build

```sh
make            # build ./gbasic
make dev        # build every binary: gbasic, libgbasic.a, gbasic-lsp (dev/CI entry)
```

## Test

Golden-file based (a `.bas` plus a sibling `.out` of expected stdout; negatives
pair `.bas` with `.err`). Output is compared byte-for-byte. Run the suites listed
in `CLAUDE.md` (`## Tests`); `make dev` first so `gbasic-lsp` can't rot.

## House rules

These are identical to `CLAUDE.md`'s — follow them exactly:

- **Before writing gBASIC code**, read `docs/ai/START-HERE.md` and follow it
  (`UNLEARN.md` first). gBASIC diverges from QBasic/VB intuition in ways that fail
  silently.
- **When you work around a gBASIC limitation or surprise**, append an entry to
  `/DOGFOOD.md` using its template *before continuing*.
- **Evidence standards:** tests-first where feasible; keep goldens byte-exact (a
  behavioral change that moves a golden is a deliberate, listed rebaseline);
  measure, don't assume; report what you could not verify. Never mark anything
  "verified".

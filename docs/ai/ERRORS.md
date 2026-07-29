# ERRORS — gBASIC diagnostic & runtime-error catalog

gBASIC has **two** code systems. Do not confuse them:

1. **Diagnostic kind** (`gb_diag_code`, `include/diagnostics.h`) — the *frozen*,
   machine-readable classification of a diagnostic (lexer / parse / runtime). This
   is what `--json-diagnostics` and the `gbasic-lsp` language server emit as
   `"code"`. Values are append-only, never renumbered.
2. **Runtime `error.code` / `error.source`** — what a running gBASIC program reads
   from the `error` object after a raise. `error.code` is coarse (most runtime
   errors share `1003`); **`error.source` is the real discriminator.**

The two are linked: a runtime error is reported as diagnostic kind
`GB_DIAG_RUNTIME_ERROR` with the language-level `error.code` carried in the
diagnostic's `subcode` field.

## 1. Diagnostic kind (`gb_diag_code`)

Emitted as the `"code"` string by `--json-diagnostics` and the LSP
(`gb_diag_code_str`). Frozen numeric identities:

| Numeric | Mnemonic | CLI "kind" printed |
|---|---|---|
| 1000 | `GB_DIAG_LEX_ERROR` | `lexer error` (message-less) |
| 1001 | `GB_DIAG_LEX_DETAIL` | `runtime error` (message-bearing lexer error — legacy quirk, see diagnostics.h) |
| 2000 | `GB_DIAG_PARSE_ERROR` | `parse error` |
| 2001 | `GB_DIAG_STRING_LITERAL` | `runtime error` (parser string-literal / `\u{}` decode) |
| 3000 | `GB_DIAG_RUNTIME_ERROR` | `runtime error` (evaluator) |

### JSON / LSP diagnostic shape

`gbasic --json-diagnostics FILE` writes one JSON object per diagnostic to stderr
(`gb_diag_write_json`):

```json
{"severity":"error","code":"GB_DIAG_RUNTIME_ERROR","subcode":1003,
 "path":"prog.bas","start":{"line":1,"column":1},"end":{"line":1,"column":1},
 "message":"..."}
```

Positions are **1-based byte** offsets, inclusive-start / exclusive-end (see
`include/diagnostics.h` and the reference's Errors section). `subcode` is the
runtime `error.code` below (0 for non-runtime diagnostics).

## 2. Runtime `error.code` / `error.source`

After a raise, a program reads `error` (truthy), `error.message`, `error.line`,
`error.column`, `error.code`, and `error.source`. The codes:

| `error.code` | Meaning | Typical `error.source` values |
|---|---|---|
| 1001 | Undefined variable | `undefined variable` |
| 1002 | Division by zero | `division` |
| 1003 | **Generic invalid operation / bad argument** (the catch-all) | see the domain list below |
| 1004 | I/O-style operation failure | `file operation`, `path operation`, `actor` |
| 1005 | Watcher cascade cap exceeded | `watcher` |
| 2000 | Explicit `error "…"` statement | `explicit error` |
| 2001 | PostgreSQL module error | `postgres` |
| 2002 | SQLite module error | `sqlite` |
| 5001 | XML module not loaded / XML error | `xml` |
| 6001 | GObject-Introspection (`gi`) error | `gi` |

Because `1003` covers most runtime errors, **branch on `error.source`, not
`error.code`.** The `error.source` domains seen with `1003` today:

`invalid function call`, `invalid argument type`, `invalid argument`,
`invalid conversion`, `assignment`, `comparison`, `arithmetic`, `indexing`,
`field access`, `invalid control flow`, `invalid operation`, `modifier`,
`serialization`, `source generation`, `datetime`, `clock`, `money`, `random`,
`password_hash`, `system error`, `use`, `program`, `this`, `unknown`, `gui`.

## 3. `on error` modes and the proven `resume next` model

Three modes; the mode is a **process-global** setting once executed:

- `on error stop` (default) — an unhandled raise stops the program with a nonzero
  exit and the diagnostic on stderr.
- `on error goto label` — single-use jump to `label` on the next raise; the
  handler clears itself after firing.
- `on error resume next` — described precisely below.

### `on error resume next` — verified semantics

Proven by `examples/on_error_resume_next_test.bas` (wired golden). A raise under
this mode:

1. **Does not stop the program.** It sets error state (`error`, `error.code`,
   `error.source`, `error.line`/`column`) and bumps an internal generation
   counter.
2. **Statement-list resume.** Execution continues at the *next statement in the
   same statement list* as the statement that raised — at every frame level (a
   raise inside a called function resumes at that function's next statement).
3. **Expression abandonment that propagates through calls.** The statement that
   was executing does not complete its value: an assignment does not write its
   target (it keeps its prior value, or stays unbound). This abandonment
   propagates up through every enclosing expression **and through call
   boundaries** — `x = f()` is abandoned if anything inside `f()` raised, even
   though `f()` locally resumed and returned a value.
4. **Therefore a function cannot catch-and-return a fallback.** `answer =
   safe(bad)` is abandoned by the caller's generation check regardless of what
   `safe` returns, and **`error.clear()` clears error *state* but not the
   generation counter**, so it does not rescue the caller.

The consequence for library code: **pre-validate**. Ship a non-raising checker and
call the raising builtin only on input that will succeed. Do not rely on `on error`
inside a function to tolerate bad input.

**For JSON, do not write that checker — use `try_decode(text)`.** It returns
`{ok, value, message, offset, line, column}` and never raises, so the pre-validate
pass disappears entirely. This matters beyond convenience: a hand-written scanner
in gBASIC is **quadratic**, because `mid(s, i, 1)` is O(i) on codepoint-indexed
strings. Measured on the validator this replaced — 64 KB: 16 s, 128 KB: 69 s,
256 KB: 291 s, against well under a second for the C parser. If you find yourself
scanning a string character by character in gBASIC, that curve is what you are
signing up for.

## 4. How this file was derived / regenerating it

- Diagnostic kinds: the `gb_diag_code` enum and `gb_diag_code_str` in
  `include/diagnostics.h` / `src/diagnostics.c`.
- Runtime codes and domains, from source:

  ```sh
  # distinct error.code values
  grep -oE ', [0-9]{4}, "[^"]*"' src/eval.c | sed -E 's/, ([0-9]+), .*/\1/' | sort -u
  # code -> source-domain pairs
  grep -oE ', [0-9]{4}, "[^"]*"' src/eval.c | sort -u
  ```

- Resume-next semantics: read the `error_generation` counter and
  `runtime_error_raise` in `src/eval.c`, and confirmed by running
  `examples/on_error_resume_next_test.bas`.

Numeric `error.code` values are stable in practice but, unlike `gb_diag_code`,
are not annotated as frozen in a header; re-run the greps after evaluator changes.

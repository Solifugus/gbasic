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

Two invariants a tool can rely on, both true only since 0.1.0-rc7:

- **Every diagnostic goes through the sink.** Under `--json-diagnostics` the
  stream is JSON objects and nothing else. Until rc7 an unmappable token —
  `dim x`, or a byte the lexer could not read — printed a bare line straight to
  stderr, in the middle of that stream.
- **A reported diagnostic means a nonzero exit.** A parse whose lexer reported an
  error fails even where the grammar would have accepted the truncated prefix.
  Until rc7 a top-level file could report an error, run the statements before it,
  drop everything after it, and exit 0.

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

## 3. `on error` is frame-scoped (PLAT-ERR, 2026-08-23)

Full design and rationale: `docs/error_model_design.md`. Proven by
`tests/run_error_model.sh` (17 cases) and `examples/on_error_goto_next_test.bas`.

`on error` governs **the frame that executed it** — one function invocation, or
the top level — and nothing else. A function starts in the default state
whatever its caller set, and the setting dies with the frame.

- `on error stop` (default) — propagate. The raise unwinds to the nearest
  ancestor frame with a handler; reaching the top unhandled is fatal, nonzero
  exit, one line on stderr.
- `on error goto next` — absorb: abandon the statement, mark the error
  **pending**, continue at the next statement of the same list.
- `on error goto <label>` — absorb: **disarm** the frame and jump. The jump is
  the acknowledgment, so bare `error` is false inside the handler (while
  `error.message` still reads), and rules 1–2 below do not apply.

### The consequence that matters

**A function can now catch a raise and return a clean fallback.** This is the
exact case the old process-global `resume next` could not do — the caller's
statement was abandoned by a generation check no matter what the callee
returned, and `error.clear()` did not rescue it. Absorption now restores the
generation to the absorbing frame's entry value, which is what makes the raise
invisible to outer frames.

So the old **pre-validate doctrine is retired**: stdlib functions may raise
freely, because callers can afford it. `try_decode` remains as a convenience
(and because a scanner reports *where* the JSON is malformed), not as the only
way to survive bad input.

### Two anti-silence rules

1. **One pending error at a time.** A raise while an unacknowledged error is
   pending is not absorbed — it escapes the frame as if unhandled.
2. **Pending errors do not survive the frame.** Returning, or ending the
   program, with an unacknowledged error re-raises at the call site.

Together: no raise can vanish. Forgetting a check produces noise, never
silence — the inversion of the VB6 failure mode.

### Reading and raising

Bare `error` **acknowledges** and yields the error object (truthy) once per
raise, `false` after; `error.field` reads without acknowledging, which is why
`error.message` works inside the block. Only bare `error` clears the flag —
reading `error.source` alone leaves it pending, and the next raise then escapes
under rule 1.

`error <record>` raises structurally (`message` required; extra fields become
`error.details`), and since a snapshot carries `message`, `error e` re-raises
one — preserving the original `trace` and location. `error.trace` is an array of
`{name, path, line, column}`, innermost first.

## 3b. The warning channel (PLAT-WARN, 2026-08-23)

Design: `docs/warning_model_design.md`; proof: `tests/run_warning_model.sh`.

A second, independent channel for advice. Same shape as errors —
`on warning print|ignore|goto next|stop`, read with bare `warning` (claims) and
`warning.field` (does not) — with two deliberate differences:

- **Rules 1 and 2 do not apply.** An unacknowledged warning dies with its
  frame. Warnings are advisory; advice that must be acknowledged is not advice.
  The pending flag is SEPARATE from the error's for exactly this reason.
- **Mode lookup is dynamic**, outward to the nearest explicit setting, unlike
  frame-local error mode. A failure is the callee's business; the noise budget
  is the caller's.

`on warning stop` escalates at the warning's own site; from then on it is an
error, and `error.severity` reads `"warning"`.

`warning` is a **soft name** — resolved only when no variable of that name is in
scope — so it is not a reserved word and `r.warning` still parses.

### Warning codes

| code | meaning | `source` |
|---|---|---|
| 2100 | explicit `warning(...)` from a program or library | `explicit warning` |
| 2101 | a discarded return value | `unused-result` |
| 2102 | a function or modifier shadows another, or a built-in | `override` |
| 2103 | more than one library matched a name; the extra was ignored | `library-match` |
| 2104 | an assignment created a local shadowing an outer name that was read | `shadow` |

The 2102–2104 diagnostics predate the channel and printed straight to stderr
until 2026-08-23. Routing them through it means they can now be suppressed
(`on warning ignore`) and made fatal (`on warning stop`) like any other.

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

- Frame semantics: `ErrorFrame` / `error_frame_absorb` / `raise_report_fatal`
  in `src/eval.c`, and confirmed by running `tests/run_error_model.sh` and
  `examples/on_error_goto_next_test.bas`.

Numeric `error.code` values are stable in practice but, unlike `gb_diag_code`,
are not annotated as frozen in a header; re-run the greps after evaluator changes.

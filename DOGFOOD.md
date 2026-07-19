# DOGFOOD.md — gBASIC friction log

Append-only. When writing gBASIC (a human, Claude Code, or Codex) hits a surprise,
a limitation, a bug, or a missing feature and has to work around it, add an entry
here **before continuing**. Newest at the bottom. Do not edit or delete past
entries; when something is later fixed, add a *new* entry (or a `Status:` update
line) rather than rewriting history.

This is a chronological **log**, not the reference. The systematic "gBASIC is not
QBasic/VB" catalog lives in `docs/ai/UNLEARN.md`; the error catalog in
`docs/ai/ERRORS.md`; the blessed idioms in `docs/ai/COOKBOOK.md`. Log entries may
name a quirk, but the durable explanation belongs in those files.

## Entry template

```
## <YYYY-MM-DD> — [human|CC|Codex] — while: <what you were doing>
- **Type:** language-surprise | bug | missing-feature | perf | doc-gap
- **Severity:** low | medium | high
- **What:** <what actually happened, with a minimal snippet if useful>
- **Workaround:** <what you did instead — or "resolved in <commit>">
```

---

## Seed entries — D0 audit surprise harvest

The first entries below come from the Phase D0 audit (2026-07-17/18). Status
reflects **current truth**, not the audit snapshot: two were resolved in Phase
D0.6, the rest remain open or are by-design.

## 2026-07-18 — CC — while: D0 audit (S2)
- **Type:** missing-feature
- **Severity:** medium
- **What:** `program main(args)` declared a parameter but never bound it —
  referencing `args` raised `undefined variable: args`, and the CLI rejected any
  trailing arguments (`rc=2`).
- **Workaround:** RESOLVED in `06eca7b` (D0.6 B4). A `program NAME(param)` block's
  first parameter now binds to the command-line args after the script path as a
  0-based string array (empty when none). `gbasic FILE [args...]` is accepted.

## 2026-07-18 — CC — while: D0 audit (S3)
- **Type:** language-surprise
- **Severity:** medium
- **What:** `goto`/`gosub` at the top level silently printed
  "…only supported inside functions for now" to stderr and *continued* — a no-op
  that looked like it ran.
- **Workaround:** RESOLVED in `fbe62b5` (D0.6 B5). Top-level `goto`/`gosub` now
  raise a structured runtime error (code 1003). They remain supported **inside
  functions** — that limitation is intentional and now fails loudly.

## 2026-07-18 — CC — while: D0 audit (S6)
- **Type:** missing-feature
- **Severity:** medium
- **What:** No modulo. `%` and `\` are lexer errors; there is no `mod()` /
  `remainder()` builtin; `7 mod 2` collides with duration syntax
  (`unknown duration unit: mod`).
- **Workaround:** OPEN. Compute `a - floor(a / b) * b` by hand. A `mod()`/
  `remainder()` builtin (or a `%` operator) is unbuilt — candidate for a future
  phase.

## 2026-07-18 — CC — while: D0 audit (S13)
- **Type:** language-surprise
- **Severity:** high
- **What:** `on error resume next` cannot be used inside a library function to
  catch a raise (e.g. from `decode`) and still return a clean fallback — the whole
  failing caller statement is abandoned and the assigned variable is left unbound.
  Black-box tests also showed inconsistent resume points (local-resume in one
  case, whole-statement abandonment in another), so the exact semantics are not
  yet pinned.
- **Workaround:** OPEN. PRE-VALIDATE with a non-raising checker and only call the
  raising builtin when it will succeed (see `stdlib/llm.bas` `_json_valid`). The
  true resume model must be read from the `eval.c` error path — tracked as the
  Phase D3 `ERRORS.md` task.

## 2026-07-18 — CC — while: D0 audit (BASIC-intuition cluster)
- **Type:** language-surprise
- **Severity:** low
- **What:** Several behaviors contradict QBasic/VB intuition (all **by design**,
  verified against the current binary): no numeric `for i = 1 to n` (only
  `for each x in coll … end for`); `<>` is a parse error (use `!=`); `mid` is
  **0-based** and strings are not indexable (`s[0]` raises — use `mid(s,i,1)`);
  comments are `'` only (`rem`/`//` error) and string concat is `+` only
  (`&` errors); `print` takes a single expression; keywords are case-insensitive
  but identifiers are case-sensitive; record-literal keys must be identifiers
  (`{ "a": 1 }` is a parse error — use `{ a: 1 }` or bracket-assign); no
  `dim`/`redim` (variables spring into being on assignment); `0` and `""` are
  falsy.
- **Workaround:** By design — use the gBASIC idiom in each case. The full,
  blunt treatment with snippets goes in `docs/ai/UNLEARN.md` (Phase D3); this
  entry just records that the audit surfaced them.

## 2026-07-18 — CC — while: D0 audit (perf traps)
- **Type:** perf
- **Severity:** high
- **What:** `arr[i]` inside a `while i < count(arr)` loop **deep-copies the whole
  array every iteration** → O(n²) (a 12k-item loop took 13s). `append(arr, x)`
  also returns a full copy each call, so accumulating a large list via `append` is
  O(n²) too.
- **Workaround:** Iterate large arrays with `for each x in arr` (0.15s for the
  same 12k loop); stream/count in place instead of accumulating with `append`
  when you don't need to keep the list.

## 2026-07-18 — CC — while: D3 UNLEARN verification
- **Type:** bug
- **Severity:** low
- **What:** `dim x` (dim is unsupported) prints `unexpected token DIM at 1:1` to
  **stderr**, produces empty stdout, and **exits 0** — an error message with a
  success exit code. Most parse errors exit nonzero; this path is inconsistent.
- **Workaround:** Don't use `dim` (there is no such statement — assign to create
  variables; see `docs/ai/UNLEARN.md`). Non-blocker for gbasic-studio, but the
  exit-code inconsistency is worth fixing; noted in PLAN.md deferred.

## 2026-07-18 — CC — while: D3 ERRORS.md (S13 follow-up)
- **Type:** language-surprise
- **Severity:** high
- **What:** Status update to the S13 entry above. The `on error resume next`
  semantics are now **pinned from source** (`error_generation` counter in
  `src/eval.c`) and proven by fixture. Precise model: a raise resumes at the next
  statement in the *same* statement list at every frame, but the raising
  statement's value is abandoned and that abandonment propagates through call
  boundaries — so a callee cannot catch-and-return a fallback, and `error.clear()`
  does not rescue the caller (it clears state, not the generation counter).
- **Workaround:** Unchanged — PRE-VALIDATE. Full model + proof in
  `docs/ai/ERRORS.md` and `examples/on_error_resume_next_test.bas`.

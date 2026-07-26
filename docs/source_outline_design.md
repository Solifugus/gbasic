# gBASIC source-outline facility — design & R1 investigation

Status: **investigation / design recommendation.** No implementation. This is the
resolution study for **Risk R1** in `docs/gbasic_studio_plan.md`: *"there is no
gBASIC-reachable parse/AST/outline API today,"* which the plan names as the gate
before **STU-3** (the execution-section engine / structural anchoring).

The question this document answers is deliberately **broader than Studio**: what is
the smallest correct, reusable, gBASIC-accessible facility that lets *any* tool
(Studio, editors, linters, outline views, navigators, source analyzers) obtain
stable structural information from gBASIC source? Studio is the first consumer, not
the design center.

All claims below were verified against the current source and the built binary on
2026-07-26; source references and probe results are inline. Nothing here is marked
"verified" in the acceptance sense — it is empirical evidence gathered during
investigation.

---

## 1. Existing architecture (how parsing and structure work today)

The pipeline is the classic lex → parse → AST → walk (`CLAUDE.md`). The parts that
matter for structure:

### 1.1 The front end is already reentrant

- `src/frontend.c` exposes `gb_parse(source, path, out_program, diags)` — a thin
  wrapper over `parse_source_reentrant` (`src/parser.y:1195`). The comment states
  it "is not yet part of a public interface."
- Per-parse state lives in a **stack-allocated** `gb_parse_ctx`
  (`include/parse_ctx.h`) threaded through Bison via `%param`; there are no
  file-scope parser globals, so **concurrent parses in one process share nothing**.
  This is the correct substrate to build a reusable outline core on.
- `gbasic-lsp` already links the parser as a library (`libgbasic.a`) and calls
  `gb_parse` (`src/lsp/handlers.c:132`). So a **general parse core with two
  consumers already exists** — it simply produces *diagnostics*, not structure.

### 1.2 The AST carries start positions on every node — but no end, no offset

- `include/ast.h`: every `AstStmt` and `AstExpr` has `int line; int column;`.
- Every statement rule in `src/parser.y` (lines 554–576) wraps its result in
  `ast_stmt_position($1, @1.first_line, @1.first_column)`, and expressions in
  `expr_at(...)` (line 289). **Confirmed: every node has an accurate start
  position.**
- **There is no end position** on any node, and **no absolute byte offset** — only
  1-based start line/column. Block terminator token positions (`end program`,
  `end if`, `end for`) are **not** captured anywhere in the AST.
- The AST *shape* already encodes nesting and the structural units STU-3 needs:
  `AST_STMT_PROGRAM`, `_LIBRARY`, `_FUNCTION`, `_MODIFIER`, `_IF`, `_WHILE`,
  `_FOR_EACH`, `_CONSIDER`, `_WATCH`, `_WITH_LOCK`; bodies are nested `AstStmtList`s
  (`ast.h:168`–242), so parent/child is intrinsic to the tree.

### 1.3 Positions are 1-based BYTE line/column (no Unicode awareness)

`include/diagnostics.h:48`–64 documents the lexer's `advance()` precisely and I
re-verified it against a probe:

- Line and column are counted in **bytes**, 1-based. A multi-byte UTF-8 character
  advances the column by its **byte count**, not by 1. Columns are **not**
  codepoints and **not** UTF-16 units.
- A tab advances column by exactly 1 (no tab-stop expansion).
- Only `\n` starts a new line; a lone `\r`/CR of CRLF is ordinary column-advancing
  whitespace.
- `end_column = start_column + token byte-length` (the yylloc convention used by
  diagnostics).

The LSP already transcodes these byte columns to the client's UTF-16/UTF-8 unit by
re-measuring the line's bytes (`src/lsp/lsp_position.c`). Any outline that crosses
into an LSP-shaped consumer must do the same; an in-process gBASIC consumer that
works in the same byte model needs no transcoding.

### 1.4 Comments are stripped by the lexer

Probe (`--tokens` on `x = 1  ' trailing`\n`' full line`\n`y = 2`):

```
   1:1  IDENT x   1:3 OP_EQ   1:5 NUMBER 1   1:26 NEWLINE
   2:20 NEWLINE
   3:1  IDENT y   ...
```

There is **no COMMENT token** — comment text is consumed, only the `NEWLINE`
survives. Consequence: **comments cannot participate in structural anchoring via the
parser.** Any comment/marker-based anchoring would require a separate raw-text scan
(explicitly out of scope for a parser/outline facility).

### 1.5 The parser has NO error recovery — it aborts at the first error

This is the single most consequential finding.

- The grammar has `%define parse.error verbose` and **no Bison `error`
  recovery productions** (grep of `src/parser.y` for `error` tokens: only
  `report_syntax_error`/`yyerror`, never an `error:` rule).
- `parse_source_reentrant` (`src/parser.y:1215`): `if (result != 0) return
  result;` — **without filling `*out_program`**. On any parse error the caller
  receives an **empty AST**, never a partial tree.
- `src/lsp/handlers.c:126` says it outright: *"the parser aborts at the first error
  today (error recovery is a deferred phase),"* so it publishes 0 or 1 diagnostic.

Probes (`--ast`), confirming behavior:

| Source | Result |
|---|---|
| empty file | `Program` (empty), exit 0 |
| `x = 1` | full AST, exit 0 |
| valid `program`/`if`/`for each`/`function` (nested) | full nested AST, exit 0 |
| `function add(a,b)` then `return a +` (incomplete) | `parse error … 2:13`, **no AST**, exit 1 |
| `if x > 0 then …` with no `end if` | parse error, **no AST**, exit 1 |

So a structural facility built on the current parser is **all-or-nothing per
parse**: a fully valid file yields a complete structure; a file with a single syntax
error (the normal state while typing) yields **nothing plus one diagnostic**. There
is no partial outline available from the parser as it stands.

### 1.6 The `--ast` CLI is a human dump with no positions

`src/main.c:869` calls `ast_dump(program)` (`src/ast.c:604` `dump_stmt`). The output
is indented human text (`Program` / `Function add` / `If` / `Body` …) and prints
**no line/column**. It is **not** machine-consumable as an outline and would need a
new emitter regardless of which option is chosen.

### 1.7 No parse/outline/AST facility is reachable from gBASIC today

`src/builtins.c` (the name registry) contains no `parse`/`ast`/`outline`/`tokens`/
`reflect` builtin. The only `parse_source` call inside `eval.c` is the **`load`
import loader** (`eval.c:4203`), which parses library files internally and is not
exposed. There is no `eval`/dynamic-exec builtin. **R1's premise is confirmed: no
gBASIC-reachable structural facility exists.**

### 1.8 Performance (measured)

Generated 5,000 functions = 40,000 lines.

- One `gbasic --ast big.bas >/dev/null`: **~50 ms** wall, **~20 MB** RSS.
- 20 full invocations (process spawn + parse + dump): **1.11 s** ⇒ ~55 ms each
  *including* fork/exec.
- Pure process startup (`--version` ×30): **~5.4 ms** each.

⇒ An **in-process** parse of a realistic file (hundreds–few-thousand lines) is
sub-millisecond to low-single-digit ms. **Full reparse on every edit is entirely
acceptable; incremental parsing is unjustified.** A CLI-subprocess adapter adds
~5 ms startup per call — cheap, but per-keystroke reparse over a subprocess is the
only case where it would matter, and even STU-3 reparses at checkpoints, not
keystrokes.

---

## 2. What STU-3 actually needs (from the accepted plan §STU-3)

Grounded in `docs/gbasic_studio_plan.md:433`–481, the section engine needs to:

1. **Enumerate the legal-location set** — the safe structural positions a boundary
   may occupy: *between top-level statements* (and never inside an expression, loop
   body, or function body in a way that breaks scope).
2. **Anchor persistently** — "after the Nth top-level statement" + a **statement
   fingerprint** + a **byte-offset hint**, stored in the STU-0 per-file dotfile.
3. **Re-resolve drift on open** — re-parse, reattach anchors that still resolve,
   **flag stale** (never silently drop) anchors whose statement was deleted or
   rewritten.
4. Do all of this **without executing** the source and **headless**.

Mapped to structural primitives, STU-3 needs, per source file:

- the **ordered list of top-level structural units** (statements and block units at
  program scope), each with **kind**, **name** (where applicable), and a **source
  range**;
- enough per-unit identity to compute a **fingerprint** (kind + name + normalized
  header, or a content hash — Studio's choice, see §8);
- a **byte offset** (or line/column it can convert) for the offset hint;
- a signal for **partial/invalid** source so drift re-resolution can decline to
  reattach against a broken parse rather than mis-attach.

Notably STU-3 does **not** need full expression detail, deep nesting semantics, or a
public AST. It needs a **compact, stable outline**. Full nesting is useful (to know a
unit's extent and to reject illegal in-body locations) but the frozen public surface
should be an outline, not the compiler's AST.

---

## 3. Options evaluated

### Option A — General in-process gBASIC outline builtin  ★ recommended (as core)

Shape (illustrative): a builtin `source_outline(text)` returning ordinary gBASIC
records/arrays:

```
result = source_outline(text)
' result.ok            : bool  (did it fully parse?)
' result.nodes         : array of node records (see §7)
' result.diagnostics   : array of {severity, code, line, column, end_line, end_column, message}
```

- **Reuses:** `gb_parse` (reentrant, stack ctx, memory-safe) + the diagnostics sink
  (`gb_diagnostics`) verbatim. Node start positions already exist on every AST node.
- **New native code:** an **AST → gBASIC `Value` outline emitter** in `eval.c`
  (walk `AstStmtList`, emit node records with children), a `source_outline` builtin
  entry in `eval.c` + a name in `src/builtins.c`; **optionally** end-position capture
  in `parser.y` (§5). No new dependency; always-builds.
- **Error model:** `ok=false` + `diagnostics` (currently 0/1 entry) + `nodes=[]`
  on a parse error — bounded by §1.5 until recovery lands.
- **Performance:** in-process, sub-ms–low-ms (§1.8). No serialization, no spawn.
- **Memory/lifetime:** outline is a normal gBASIC value (COW, GC'd like any record);
  the transient AST is freed inside the builtin (`ast_free_program`) exactly as the
  LSP does. No caller-managed handles.
- **Schema stability:** the returned records are a **frozen public contract** — the
  compiler's internal AST is *not* exposed; the emitter is the insulation layer.
- **Permanent?** Yes. This is the general facility any editor/linter/formatter wants.

### Option B — LSP `documentSymbol` endpoint

Studio (or any editor) speaks LSP to `gbasic-lsp`; add a `textDocument/documentSymbol`
handler that runs the same outline core and returns `DocumentSymbol[]`.

- **Reuses:** `gb_parse`, the LSP framing/position-transcode already present.
- **New native code:** a `documentSymbol` handler + the same AST→symbol emitter, in
  LSP-shaped JSON with UTF-16 transcoding.
- **Cost against Studio:** process lifecycle, JSON-RPC, incremental-document
  bookkeeping, failure isolation, and a **standing subprocess dependency** for an
  app that is otherwise fully in-process gBASIC. Latency is process-bound.
- **Verdict:** Wrong as Studio's primary path — it couples an in-process app to an
  external service for data it can compute in-process in <1 ms. **But** LSP editors
  genuinely want `documentSymbol`, so this should be built **later, on the same
  outline core** (§Option E). Not the R1 answer for Studio.

### Option C — CLI `--outline` adapter emitting strict JSON

`gbasic --outline FILE` prints a strict-JSON outline (via `json_encode`, never native
`encode()`); Studio shells it with `process.run` (NAP-6) and `decode`s the result.

- **Reuses:** `gb_parse`, `main.c` flag plumbing, the strict JSON serializer already
  shipped (f156839).
- **New native code:** an AST→JSON emitter in `main.c` — i.e. **the same emitter as
  Option A**, targeting JSON instead of `Value`. `--ast` today has no positions/JSON,
  so this is new code either way.
- **Cost:** ~5 ms process startup per call (§1.8) + temp-file/stdin plumbing +
  serialize/parse round-trip. Deterministic and portable; invalid source ⇒ exit 1 +
  a diagnostics JSON (or empty outline), matching §1.5.
- **Verdict:** Viable **stopgap** to unblock STU-3 without freezing an in-process
  API — but since the emitter is ~all the work, it buys little over Option A while
  adding a subprocess. Recommend only if the team wants **zero new `eval.c`
  surface** during STU-3 and to defer the builtin schema freeze.

### Option D — Pure-gBASIC structural scanner

A line-based gBASIC scanner recognizing `program`/`function`/`if`/`for each`/… and
their `end …` terminators, with no C changes.

- **Skeptical assessment (as instructed):** it would **re-implement the grammar** and
  **drift as the language evolves** — modifiers (`(...)=` content mode), `consider`
  column tracking, PBI field policies, dotted `function obj.method()`, multi-line
  constructs, string/`\u{}` edge cases. The lexer already needs stateful modes
  (`modifier_content_mode`, `consider_depth`) that a naive scanner cannot reproduce
  correctly. **It cannot remain correct.** Rejected as a structural facility.
- The *only* legitimate line-based scan is the **comment/marker** case (§1.4), which
  the parser cannot provide — a separate, shallow, clearly-labeled concern, not a
  substitute for parsing.

### Option E — One reusable outline core, several thin front ends  ★ the meta-recommendation

The front end is **already** reentrant and **already** serves two consumers
(interpreter + LSP). The correct architecture is to add **one** reusable C outline
core built on `gb_parse` — `gb_outline(text) → structure` — and let the consumers be
thin:

- **A** (in-process builtin) — Studio and any gBASIC tool.
- **B** (`documentSymbol`) — LSP editors, later.
- **C** (`--outline`) — CLI/portable, stopgap or scripting.

This satisfies "avoid parallel representations": the AST→outline mapping lives once;
JSON vs `Value` vs LSP-symbol is a thin per-consumer serializer. It is the honest
reading of the platform's existing shape.

---

## 4. Recommended solution

**Build Option A over an Option-E core**, and defer B/C until a real consumer needs
them:

> A single reusable **outline core** in C, fed by the existing reentrant `gb_parse`,
> that lowers the AST to a compact, frozen **outline** (kind/name/range/children +
> diagnostics + a partial flag). Expose it first as an **in-process gBASIC builtin**
> `source_outline(text)`. Keep the core free of any Studio concept. Let a future LSP
> `documentSymbol` and a `gbasic --outline` CLI reuse the identical core.

Rationale: it reuses everything already proven (reentrant parser, diagnostics sink,
strict JSON); it is in-process (sub-ms, no subprocess dependency for a desktop app);
it insulates callers from the AST behind a frozen schema; and it is *general* — it is
the facility an editor, linter, or formatter would independently ask for, with **no
Studio-specific C** (§Platform boundary).

**Stopgap:** if the team prefers not to freeze an in-process schema during STU-3,
`gbasic --outline` (Option C) unblocks STU-3 immediately over `process.run` +
`decode`, using the same emitter, and can be promoted to the builtin later.

---

## 5. Proposed API (for review — this is where real choices remain)

### 5.1 In-process builtin

```
outline = source_outline(text)      ' text: whole-file source string
```

Returns a record:

```
{
  ok:          bool,        ' true iff the file parsed with no errors
  nodes:       [node],      ' top-level structural units, in source order ([] if not ok)
  diagnostics: [diag]       ' 0..1 today (§1.5); grows if/when recovery lands
}
```

`node` (see §7 for the field-set decision):

```
{
  kind:         "program" | "library" | "function" | "modifier" | "if" | "while"
              | "for_each" | "consider" | "watch" | "with_lock" | "statement",
  name:         string,     ' declared name, or "" for anonymous/plain statements
  start_line:   int, start_column: int,     ' 1-based BYTE (§1.3)
  end_line:     int, end_column:   int,     ' see §5.3 (end-position decision)
  start_offset: int, end_offset:   int,     ' absolute BYTE offsets; see §7
  children:     [node]      ' nested units; [] for leaves
}
```

`diag`: `{ severity, code, subcode, start_line, start_column, end_line, end_column,
message }` — the fields already in `gb_diag` (`include/diagnostics.h`), in the
native byte model.

### 5.2 Optional consumers (same core, deferred)

- **CLI:** `gbasic --outline FILE` → the same object as strict JSON via
  `json_encode` (never native `encode()`), one object, `\n`-terminated; exit 1 on
  parse error with `ok:false`.
- **LSP:** `textDocument/documentSymbol` → `DocumentSymbol[]` with UTF-16 ranges via
  the existing `lsp_position` transcoder.

### 5.3 Decisions that need Matthew's sign-off before implementation

These are the "significant API choices" that make this a **design recommendation,
not an implementation**:

1. **End positions.** The AST stores start only (§1.2). Two ways to get `end_*`:
   - **(a) Derive** in the emitter: a unit's end ≈ the start of its next sibling
     (minus trailing blank/`end` line), or the last descendant's start. Cheap, no
     grammar change, but **approximate** — it excludes the `end program`/`end if`
     terminator line and is fuzzy around trailing comments.
   - **(b) Capture** the terminator token's location in `parser.y` for each block
     rule and store it on the node (new `end_line/end_column` AST fields). **Exact**,
     but a real grammar/AST change (new native code touching every block rule) —
     which I should **not** make unilaterally inside an investigation.
   Recommendation: **(b)** for correct outlines, done as its own tightly-scoped step
   of the platform phase. If STU-3 can tolerate approximate ends, **(a)** avoids the
   grammar change entirely — needs a product call.
2. **Offsets vs line/column.** STU-3 wants a byte-offset hint. Offsets can be
   **derived** from line/column + the source text (walk to line N, add column−1) in
   the emitter in one pass — no parser change. Decision: expose **both** (offsets for
   fingerprint/anchor math, line/column for display/transcode), or line/column only
   and let Studio derive offsets? Recommendation: expose **both**, computed once.
3. **Nesting representation.** `children` arrays (recommended — mirrors the AST and
   reads naturally in gBASIC) vs a flat list with `parent` indices vs parent IDs.
   Recommendation: **child arrays**.
4. **Depth.** Top-level only, or full nesting? STU-3's legal-location rule is about
   top-level positions but needs to *know* a unit's extent to reject in-body splits.
   Recommendation: **full nesting**, so the one facility also serves outline views
   and navigation; Studio reads only the top level for boundaries.
5. **Anonymous/plain statements.** Represent every top-level statement as a
   `"statement"` node (so "after the Nth top-level statement" is directly
   enumerable), or only named block units? Recommendation: **every top-level
   statement is a node** (STU-3 counts them); deeper leaves can be omitted to keep
   the outline compact — another sign-off point.
6. **Schema freeze.** Once shipped, the record field-set is a frozen contract (like
   the diagnostic codes in `diagnostics.h`). Confirm the field-set in §5.1 before
   the first release.

Because #1 implies a grammar change and #2–#6 freeze a public schema, the honest
outcome is to **stop here with this recommendation** rather than implement.

---

## 6. Partial-source behavior (invalid / incomplete files)

The hard constraint (§1.5): the parser today **aborts at the first error and returns
no AST**. So for a file that is mid-edit-invalid — the *common* editor state — the
outline facility can only return `ok:false`, `nodes:[]`, and one diagnostic. It
cannot return a partial outline **from the parser as it stands**.

What callers receive, and how STU-3 must cope:

- **Valid file:** full outline, `ok:true`.
- **Invalid file:** `ok:false`, `nodes:[]`, `diagnostics:[…]`. STU-3's drift
  re-resolution must therefore **decline to reattach against a failed parse** and
  **retain the last-known-good outline/anchors** (from the STU-0 dotfile) until the
  file parses cleanly again — exactly the safe behavior (never mis-attach on broken
  source). This is a Studio responsibility, not a platform one.

Two future platform paths (out of scope for R1, noted for honesty):

- **Real parser error recovery** (Bison `error` productions / statement-level
  resync) would let the outline return the valid prefix/regions plus diagnostics.
  This is a substantial, separate front-end phase; do **not** invent recovery
  behavior silently. Its absence is not a blocker for STU-3 given the last-known-good
  fallback.
- A **shallow tolerant top-level scan** (recognize `function`/`program`/… headers by
  line even when bodies don't parse) could feed outline views while typing — but it
  reintroduces Option D's correctness risk and should be weighed separately, not
  bundled into the structural facility.

**Recommendation:** ship the exact-parse outline first; drive STU-3 with
last-known-good on failure; treat error recovery as its own later platform phase if
live-while-typing outlines become a requirement.

---

## 7. Outline data model (the minimum stable representation)

Proposed minimum (frozen once shipped):

- `kind` — enumerated string (§5.1).
- `name` — string; `""` when none.
- `start_line`, `start_column`, `end_line`, `end_column` — 1-based **bytes** (§1.3).
- `start_offset`, `end_offset` — absolute **byte** offsets.
- `children` — array of nodes.

Resolved recommendations (subject to §5.3 sign-off):

- **Both** offsets and line/column: line/column for display and LSP transcode;
  offsets for O(1) fingerprint/anchor arithmetic. Deriving one from the other costs a
  text walk, so compute both once in the emitter.
- **Child arrays**, not a flat parent-ID list — mirrors the AST, reads naturally.
- **Anonymous blocks** (`if`/`while`/`for each`/`consider`/`with_lock`/`watch`) carry
  `name:""` and their own range/children.
- **Incomplete nodes:** not representable today (all-or-nothing parse, §1.5); when
  recovery lands, an `ok:false` node flag would be added — deferred, not invented now.
- **Stable IDs / fingerprints:** the platform exposes the *ingredients* (kind, name,
  ranges, offsets); it does **not** mint anchor IDs — that is Studio's job (§8).
- **Comments/markers:** **not** in the structural result (parser strips them, §1.4).
  If ever wanted, a separate raw-text marker scan — clearly not part of this facility.
- **Full AST detail stays private.** The outline is the public contract; the AST is
  an internal representation the emitter insulates callers from.

---

## 8. Anchoring boundary — platform vs Studio

A clean separation, which the platform API must respect:

**The platform exposes source structure (and nothing Studio-specific):**
- the ordered structural units, their kinds/names, ranges, offsets, nesting;
- diagnostics and an `ok`/partial signal.

**Studio owns execution-section identity and replay (none of it in the parser API):**
- computing an **anchor fingerprint** from the exposed fields (e.g. kind + name +
  normalized header text + sibling ordinal + optional content hash of the range);
- **anchor identity/IDs**, the "after the Nth top-level statement" addressing, and
  the byte-offset hint stored in the STU-0 dotfile;
- **drift re-resolution**: match persisted fingerprints against the fresh outline,
  reattach survivors, **flag stale** the rest (never mis-attach; never silently drop);
- everything downstream — sections, replay, result panes, branches, agent context.

The parser API must **never** contain `section`, `replay`, `branch`, `result`, or
`agent` concepts (Platform boundary, below). Anchor ingredients the future engine
will draw from the platform: `kind`, enclosing `name`, `start/end` range, `offset`,
sibling ordinal (derivable from node order), and (for a content fingerprint) the byte
range to hash. Normalized-header and fingerprint computation are **Studio's**.

---

## 9. Performance

Measured (§1.8): in-process parse is **~50 ms for 40k lines**, so **<1 ms for a
typical file**, ~20 MB peak for the pathological case. Repeated full reparse on edit
is fine; **no incremental parsing is justified** by any evidence here. A CLI adapter
adds ~5 ms startup per call — negligible for STU-3's checkpoint-driven reparse, and
only relevant if some future consumer reparsed per keystroke over a subprocess
(which the in-process builtin avoids entirely). Serialization cost applies only to
Options B/C; Option A returns native values with none.

---

## 10. Security & correctness

- **No execution to obtain structure.** `source_outline`/`--outline`/`documentSymbol`
  all call `gb_parse` only — lex + parse, never eval. No `eval`, no dynamic
  execution, no model-generated parsing. (There is no dynamic-exec builtin in the
  runtime to misuse; §1.7.)
- **Deterministic.** Parsing is pure; the same bytes yield the same outline.
- **Strict JSON across any process boundary.** Option C uses `json_encode`
  (RFC-8259, shipped f156839), never native `encode()`.
- **Memory-safe/reentrant.** `gb_parse` uses a stack `gb_parse_ctx` and no globals;
  the emitter frees the transient AST (`ast_free_program`) as the LSP already does.

---

## 11. Platform boundary

Any permanent parser/outline capability is **general platform infrastructure**:

- **No Studio-specific C.** The outline core and builtin know nothing of Studio.
- **No Studio concepts** (`replay`, `execution branch`, `result pane`, `agent
  context`, `section`) anywhere in the parser/outline API.
- The facility is justified purely as general tooling infrastructure (editors,
  linters, outline views, navigators) — Studio is merely its first caller.

---

## 12. Risks

- **Schema coupling / freeze.** The outline record becomes a public contract; adding
  fields is safe, changing/removing is not. Mitigation: keep the schema compact
  (§7), freeze deliberately (like `diagnostics.h` codes).
- **Parser coupling.** The emitter reads AST internals; AST changes could ripple.
  Mitigation: the emitter is the *only* new reader, co-located with the parser, and
  the AST already changes rarely and additively.
- **End-position accuracy.** If we derive ends (§5.3 #1a) rather than capture them,
  outlines are approximate around terminators — acceptable for boundary placement
  between top-level units, wrong for exact block folding. Decide per consumer needs.
- **Partial-source gap.** No recovery ⇒ no outline while typing-invalid. Mitigated
  for STU-3 by last-known-good; a real limitation for a live outline view (needs the
  separate recovery phase).
- **Maintenance.** One core, three potential consumers — low duplication risk if
  Option E discipline holds; higher if B/C grow their own emitters (avoid).

---

## 13. Implementation plan (a separately reviewable platform phase, if approved)

Proposed as its own phase (**PLAT-OUTLINE**), *before* STU-3, in bounded steps —
each its own commit, none touching Studio:

1. **(optional, if exact ends chosen)** Capture block-terminator locations in
   `parser.y`; add `end_line/end_column` to `AstStmt`/`AstExpr`; set them in every
   block rule. Independent parser tests; existing goldens must stay byte-exact.
2. **Outline core** — `gb_outline(text) → outline` in C over `gb_parse`: AST walk →
   an internal outline struct (kind/name/ranges/offsets/children) + diagnostics +
   `ok`. Offsets computed from line/column + text in one pass.
3. **Builtin** — `source_outline(text)` in `eval.c` lowering the core to gBASIC
   `Value` records; register the name in `src/builtins.c`. Free the transient AST.
4. **Independent platform tests** — a golden suite (`tests/run_outline.sh`) over the
   §Tests corpus: exact ranges/offsets asserted; byte-vs-char confirmed; malformed ⇒
   `ok:false` + diagnostic; large-file timing; repeated calls; valgrind-clean.
5. **(deferred)** `gbasic --outline` CLI and/or LSP `documentSymbol` on the same
   core, only when a consumer needs them.

STU-3 then consumes `source_outline` (or `--outline` as the stopgap) with no
Studio-private parsing.

---

## 14. Stopgap

`gbasic --outline` over `process.run` + `decode` (strict JSON via `json_encode`) is
an **acceptable temporary bridge** to unblock STU-3 without freezing an in-process
schema — using the same emitter, promotable to the builtin later. It is *not*
preferred as the permanent path (subprocess dependency for in-process-computable
data), and it carries the same partial-source limitation (§6). Recommended only if
the team wants zero new `eval.c` surface during STU-3.

---

## 15. Decision

**READY TO IMPLEMENT GENERAL PLATFORM FACILITY** — the design is clear and
overwhelmingly favors **Option A over an Option-E reusable core** (in-process
`source_outline` on the existing reentrant `gb_parse`, insulated by a frozen outline
schema, general to all tooling, no Studio-specific C).

Two qualifications, deliberately leaving the trigger to Matthew rather than
implementing during an investigation:

- The **end-position choice (§5.3 #1)** is a genuine fork: exact ends need a bounded
  **grammar change** I should not make unilaterally here; approximate ends need none.
- The **outline record schema (§5.1/§7)** becomes a **frozen public contract** and
  wants sign-off before first release.

Accordingly, per the prototype policy ("prefer stopping with a design recommendation
when significant API choices remain"), **no platform code was written** in this
investigation. If those two decisions are made, PLAT-OUTLINE (§13) is a tightly
bounded, independently testable platform phase that resolves R1 and unblocks STU-3.

Until then, STU-3 also has a viable **temporary adapter** (`gbasic --outline`, §14)
if implementation is deferred but STU-3 must proceed.

---

## Appendix — probe corpus & commands (reproduce)

Binary built with `make dev` at investigation time (2026-07-26). Probes:

- `./gbasic --tokens` on `x = 1 ' c` — comment stripped, no COMMENT token (§1.4).
- `./gbasic --ast` on empty / `x=1` / valid nested `program`+`function` — full nested
  AST, no positions printed (§1.6); on `return a +` and unmatched `if` — parse error,
  **no AST** (§1.5).
- `./gbasic --json-diagnostics` on the incomplete function — one
  `GB_DIAG_PARSE_ERROR` object with byte line/column (§1.3).
- 40k-line generated file: ~50 ms/parse, ~20 MB RSS; 20 invocations 1.11 s; startup
  ~5.4 ms (§1.8).
- Source reads: `frontend.c`, `parse_ctx.h`, `diagnostics.h`, `ast.h`,
  `parser.y` (position wrapping 554–576; reentrant core 1195–1222; no `error`
  productions), `ast.c` (`dump_stmt` 604+), `main.c` (869), `builtins.c` (no parse
  builtin), `eval.c:4203` (import loader), `src/lsp/handlers.c` (diagnostics-only,
  126).

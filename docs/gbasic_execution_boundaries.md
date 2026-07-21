# gBASIC Studio — Execution Boundaries Specification

Status: **focused design/specification, not implemented.** Resolves the early blocking
question from `docs/gbasic_studio_design.md` §5 / §22-Q1:

> **Exactly where may Studio place persistent execution boundaries in ordinary gBASIC
> source, and what are the semantics of executing, replaying, moving, deleting, and
> restoring those boundaries?**

Grounded in the current parser/AST/evaluator. Companion to
`docs/gbasic_studio_design.md` (product model), `docs/gbasic_studio_research.md`
(feasibility), and the platform docs (`…coverage.md`, `…plan.md`). This document does
**not** add syntax to `.bas` and does **not** treat boundaries as notebook cells;
boundaries live entirely in Studio metadata (design §2.1/§2.2).

Tags: **VERIFIED** (grounded in cited code), **PROPOSED** (a design choice),
**REQUIRED** (must hold), **DEFERRED** (out of near-term scope). All `file:line` cites
are `src/eval.c` unless noted.

---

## 1. Executive summary

The current evaluator makes the boundary problem **much simpler than a notebook**,
because at the top level gBASIC execution is **strictly sequential and
non-jumping** (VERIFIED, §2). Three facts decide the whole design:

1. **Top level cannot jump.** `goto`/`gosub` raise an error at top level; labels are
   inert; `break`/`continue` are legal only inside a loop/`consider`
   (18359-18400). The top-level statement list is walked linearly by `eval_stmt_list`
   (18528-18548). So a "seam between two adjacent top-level statements" is a
   fully-defined, reproducible point in execution.
2. **Compound statements are atomic to the walker.** `if`/`while`/`for each`/
   `consider`/`with lock`/`without watchers` each evaluate their entire body inside a
   single `eval_stmt` call (18197-18520). The evaluator has **no mechanism to stop and
   resume in the middle of a loop or block** — re-entering means re-running the whole
   construct.
3. **The only persistent state is the global environment.** Functions run in a fresh
   `local_env` parented to `global_env`, cleared on return (5392-5467). Top-level
   variables in `global_env` (397-398) are the sole durable, inspectable state between
   statements.

Therefore (PROPOSED v1, REQUIRED to be conservative): **a persistent execution
boundary may sit only at a top-level statement seam** — between two adjacent statements
of the executed statement list (the top-level list, or a `program` block body). A
boundary means *"the global state after all statements above it have run to
completion."* Sections are the runs of statements between successive boundaries.
Restoration is **replay of the prefix** in a fresh actor process; the design's
replay→snapshot ladder (design §8.2) rides on top without changing this user model.
Finer-grained stops **inside** loops/functions are the province of **debugger
breakpoints** (a separate, transient mechanism — §4/§5), never persistent resumable
roots in v1.

---

## 2. Verified current control-flow / execution architecture

### 2.1 AST shape (VERIFIED, `include/ast.h`)

- The program is a flat `AstStmtList` (`ast.h:72-75`). Statement kinds are enumerated
  `ast.h:6-32`; expression kinds `ast.h:34-51`.
- Every `AstStmt`/`AstExpr` carries `line` and `column` (`ast.h:123-126, 165-168`,
  set by `ast_stmt_position`/`ast_expr_position`, `ast.h:273,300`). **There is no
  stable per-node identity** beyond position — anchoring must be structural (§12).
- Compound bodies are nested `AstStmtList`s: `if_stmt.body`/`else_body`
  (`ast.h:224-228`), `while_stmt.body` (229-232), `for_each.body` (181-185),
  `consider.branches[i].body`/`else_body` (233-237), `function.body` (186-192),
  `program.body` (211-215), `library.body` (216-219), `with_lock.body` (177-180),
  `without_watchers` (201).

### 2.2 Top-level walk (VERIFIED)

- `eval_program(program)` sets `active_root = program` (18551), optionally locates a
  single `program` block (18552-18561), then runs either the program-block body or the
  whole top-level list via `eval_stmt_list` (18603-18605).
- `eval_stmt_list` (18528-18548) walks `items[pc]` in order, honoring control-flow
  results (`did_goto`/`did_return`/`did_gosub`/`did_stop`/`did_break`/`did_continue`).
  At the **top level** none of goto/gosub/break/continue can legally originate (see
  below), so the walk is a straight `for pc in 0..count`.
- Each `eval_stmt` (18117-18526) executes exactly one statement **to completion**,
  including the entire body of any compound statement.

### 2.3 Jumps are function-local only (VERIFIED)

- `goto` at top level raises *"goto is not supported at top level; supported inside
  functions"* (`function_depth == 0`, 18361-18369). Same for `gosub` (18371-18379).
- Labels (`AST_STMT_LABEL`) are **no-ops** wherever walked (18359-18360); they are jump
  *targets* resolved only inside a function body (`find_function_label` over
  `function.body`, 5422-5457; 18534).
- `break`/`continue` raise unless inside a loop/`consider` (`loop_depth`/
  `consider_depth` guards, 18381-18400); they unwind only to the enclosing loop
  (18248-18254, 18446-18454). A `did_break`/`did_continue` that reaches `eval_program`
  is treated as an error exit (18616-18619).

> **VERIFIED consequence:** at the top level, control flow is a linear sequence of
> complete statements. There are no cross-statement jumps, no early loops, no labels to
> resume from. This is the property that makes seam-boundaries sound.

### 2.4 Compound statements (VERIFIED atomic)

`if` (18401-18428), `while` (18429-18464), `for each` (18228-18267), `consider`
(18465-18520), `with lock` (18197-18227), `without watchers` (18301-18311) each call
`eval_stmt_list(body)` **inline** and return only after the body finishes (or a
control-flow result exits the block). The walker never observes a partially-executed
compound. Loop bodies re-run per iteration within the single `eval_stmt` call
(18245-18263, 18431-18461).

### 2.5 Functions (VERIFIED)

- `invoke_function` (5391) creates `local_env` with `.parent = &global_env`
  (5392-5395) — **not** the caller's frame; globals are visible, caller locals are not.
- Locals are freed on return (`env_clear(&local_env)`, 5467); **function locals never
  persist**.
- `function_depth++/--` brackets the call (5407, 5464); `goto`/`gosub` inside a
  function resolve against `function.body` with a per-invocation gosub stack
  (5408-5457).
- Function declarations register **on-reach** during the top-level walk
  (`function_register`, 18276-18278) — or are **hoisted up front** when a `program`
  block exists (18569-18581). `AST_STMT_MODIFIER` likewise (18280-18282). Dotted method
  defs are *executable attach statements*, not hoisted declarations (18268-18275).

### 2.6 Programs & libraries (VERIFIED)

- Only one `program` block may execute (18554-18560). Its body is the executed unit;
  top-level declarations are hoisted before it (18569-18581); its declared parameter
  binds argv (18588-18600). `program`/`library` statements are **no-ops** when walked
  by `eval_stmt` (18283-18285).
- `library` blocks execute nothing on import: `library_import_from_block` (4436-4463)
  registers only exported functions/modifiers and recurses into nested `use`s. A
  library body **cannot run arbitrary top-level code**.

### 2.7 `load`/`use` (VERIFIED idempotent, declaration-only)

- `library_import` (4230-4434) is **idempotent**: a `(resolved-path, name)` pair
  already in `used_pairs` returns immediately (4308-4311, 4350-4351, 4405-4408);
  circular imports are detected (4312-4318). Files are parsed once and cached in
  `loaded_files` (4004-4044, `loaded_file_get`).
- Native modules (`pg`/`sqlite`/`webclient`/`webserver`/`xml`/`gi`/`gui`) set a
  `*_library_loaded` flag (4235-4303, 4437-4448) — also idempotent.
- **Therefore `load` has no re-run side effects on repeated evaluation** (beyond
  setting an already-set flag); it is safe to replay.

### 2.8 Watchers (VERIFIED synchronous)

- `watch` registers only at top level/global env (`watcher_register`, 3578-3593); a
  non-global registration warns and is ignored (3579-3582).
- `watcher_trigger_change` fires **only on global-env mutations** (`current_env !=
  &global_env` → skip, 3561-3564) and **drains synchronously**: it enqueues matching
  watchers and calls `watcher_drain`, running every pending watcher body to completion
  before returning (3565-3575), guarded by a 10 000-execution cycle limit
  (3517-3531). Assignment statements trigger this inline (18151-18169).

> **VERIFIED:** all synchronous watcher cascades caused by a statement complete before
> that statement returns — hence before the next statement, hence before any following
> boundary. (Confirms design §15.)

### 2.9 Actors / concurrency (VERIFIED)

- `spawn` fork+execs `gbasic --actor …` (8354-8357); shared-nothing OS processes;
  mailbox over `AF_UNIX/SOCK_SEQPACKET` (`root_mailbox` 7447). `receive` **blocks**
  (`actor_receive_impl` 8030). Children are reaped by `actor_cleanup_children`, called
  at program end (18632) and available mid-run (8401).
- Actor handles are live capabilities: **not serializable** except in-transit via
  spawn-arg/send fd passing (7046-7093).

### 2.10 Serialization (VERIFIED per-value; live handles rejected)

- `serialize`/`deserialize` operate **per Value** (`serialize_value` 6959,
  `builtin_serialize_value` 7133, `builtin_deserialize_value` 7385). Magic+version
  framed (`gBS` v1). **There is no whole-`Env` serializer.**
- Serializable kinds: null/unknown/bool/number/string/array/record/datetime/duration/
  money (6966-7027), `file`/`dir` **as a path string only** (7028-7035), and
  `function` **by name (+library)** (7094-7107).
- **Rejected** (raise): postgres/sqlite/xml handles (7036-7041), gobjects (7042-7045),
  actor handles outside transfer contexts (7091-7093).

### 2.11 Front end is reentrant; evaluator is not (VERIFIED)

- `gb_parse(source, path, …)` fills a `gb_parse_ctx` and "concurrent gb_parse calls in
  one process share nothing" (`src/frontend.c:5-10`, `include/parse_ctx.h:18-37`) —
  Studio can lex→parse→AST a buffer **without touching the evaluator**, to compute
  legal boundary locations and structural anchors.
- The evaluator is global-state (`static Env global_env` 397, `static current_env` 398,
  ~60 file-scope statics) — **no in-process snapshot; concurrency is process actors**
  (consistent with design §8). This is *why* v1 is replay-in-a-fresh-process.

---

## 3. Boundary definition

### 3.1 Canonical convention (PROPOSED, REQUIRED once chosen)

> **A boundary is a *seam* between two adjacent statements of an executed statement
> list. Boundary *N* denotes the global-environment state and accumulated effects
> **after** every statement above the seam has run to completion, and **before** the
> statement below it runs.**

"State **after** the preceding unit" (not before) is chosen because:

- it matches the sequential top-level model exactly — a seam is an index into the
  statement list, and the post-state of statement *k−1* is definitionally the pre-state
  of statement *k* (VERIFIED: `eval_stmt_list` advances `pc` only after a statement
  completes, 18545);
- replay to boundary *N* = *"run the prefix `items[0..N-1]`"* — a closed, reproducible
  operation (§8);
- it makes a boundary a branch root cleanly (design §9): everything above is shared
  ancestry, everything below may diverge (§13).

### 3.2 What a boundary is made of (PROPOSED)

| Field | Meaning |
|---|---|
| **identity** | a Studio-generated stable id (persisted in metadata; design §2.2), independent of source text |
| **scope** | which executed list the seam indexes: the file's top-level list, or a named `program` block body (§6) |
| **anchor** | a *structural* anchor into the AST seam (§12) — statement-index path + fingerprints of the neighbours — **plus** a `line/column` hint (VERIFIED available `ast.h:123-126,165-168`); never line-only |
| **position** | "before statement *k*" ≡ "after statement *k−1*" at the same nesting level (top level only in v1) |
| **classification** | the determinism/effect class of the section *above* it (§9), for replay-safety |
| **state ref** | optional handle to captured post-state / checkpoint for this boundary (§8, §14) |
| **branch data** | optional: this seam is a branch root (§13) |

### 3.3 Sections (PROPOSED)

Given ordered boundaries `B0 < B1 < … < Bm` over an executed list, a **section** `S_i`
is the run of statements strictly between `B_{i-1}` and `B_i`. Two implicit boundaries
always exist: **start** (before the first statement) and **end** (after the last). A
file with no user boundaries is a single section. "State at `B_i`" = post-state of
`S_i` = the global env after `S_i` completes.

---

## 4. Legal boundary matrix

Placement is judged against the current walker (§2). A boundary is **placeable** only
where the evaluator can actually *stop and later resume by replay* — i.e., at a
top-level statement seam. "Inside" a compound means "between child statements of its
body"; VERIFIED these are not walker-resumable in v1.

Legend: **Before / After** = at the seam immediately before/after the whole construct
(a top-level seam); **Inside** = between statements of the construct's body;
**BP-only** = a location reserved for debugger breakpoints (§5), not a persistent
boundary in v1.

| Construct | Before | After | Inside | Notes |
|---|:--:|:--:|:--:|---|
| simple assignment | ✅ | ✅ | — | atomic; watcher cascade completes within it (§2.8, 18151-18169) |
| expression / function-call statement | ✅ | ✅ | — | atomic (18185-18196); effects (I/O, spawn, db) classified per §9 |
| `print` / `input` | ✅ | ✅ | — | `print` atomic (18172-18184); `input` is an external read (§9) |
| `load`/`use` | ✅ | ✅ | — | idempotent, declaration-only (§2.7); a boundary right after it is trivially replayable |
| `if` / `else` | ✅ | ✅ | ❌→BP-only | whole `if` is atomic (18401-18428); a seam inside a branch body is BP-only |
| `while` | ✅ | ✅ | ❌→BP-only | atomic loop (18429-18464); cannot persistently stop mid-loop (§5) |
| `for` / `for each` | ✅ | ✅ | ❌→BP-only | atomic loop (18228-18267); no mid-iteration resume |
| `consider` | ✅ | ✅ | ❌→BP-only | atomic (18465-18520); branch bodies BP-only |
| `with lock` | ✅ | ✅ | ❌ | lock is held only for the body (18215-18225); a persistent stop inside would hold a lock across interaction — **forbidden** |
| `without watchers` | ✅ | ✅ | ❌ | suppression is scoped to the body (18301-18311); no persistent stop inside |
| function declaration | ✅ | ✅ | ❌→BP-only | the *declaration* seam is a normal top-level seam; a seam **in the body** is BP-only (§5) and never a replay root |
| dotted method attach (`function obj.method()`) | ✅ | ✅ | ❌→BP-only | executable attach statement (18268-18275); treated as an ordinary top-level statement |
| `program` block | ✅ | ✅ | ✅ (its body *is* the executed list) | boundaries live **inside** a program block because its body is what runs (§6) |
| `library` block | ✅ | ✅ | ❌ | body executes nothing on import (§2.6); no meaningful inside-boundary |
| modifier declaration | ✅ | ✅ | ❌ | registration only (18280-18282) |
| label | ✅ | ✅ | n/a | inert at top level (18359-18360); a boundary "at" a label is just the seam there |
| `goto` | ✅ | ✅ | n/a | **top-level `goto` is a runtime error** (18361-18369); only appears inside functions ⇒ BP-only context (§7) |
| `gosub` | ✅ | ✅ | n/a | same — top-level error (18371-18379); function-only ⇒ BP-only |
| `return` | ✅ | ✅ | n/a | at top level ends execution (18346-18357, 18607-18609); inside a function it is control flow ⇒ BP-only |
| `break` | — | — | n/a | only valid inside a loop/`consider` (18381-18390); never a top-level seam target |
| `continue` | — | — | n/a | only valid inside a loop (18391-18400) |
| `watch` block | ✅ | ✅ | ❌ | registration at top level (18294-18300); body runs during drains (§15), not as a section |
| `with lock` / lock ops | ✅ | ✅ | ❌ | see `with lock` above |
| `spawn` (expression) | ✅ | ✅ | — | atomic statement; but the section is **effectful/actor** (§9, §16) — replay-guarded |
| `send` / `receive` | ✅ | ✅ | — | atomic; `receive` blocks (§2.9) ⇒ the section is external/actor (§16) |
| error-handling (`on error …`, `error`) | ✅ | ✅ | — | `on error goto/resume/stop` set evaluator mode (18312-18326); a boundary after them is fine but note the mode is **global evaluator state** replay reconstructs (§17) |

**Rule extracted (REQUIRED v1):** *placeable ⇔ a seam at the top level of the executed
list* (the file's top-level list, or a `program` block body). Everything marked
BP-only is a **debugger** concern (§5), not a persistent boundary.

---

## 5. Compound / block semantics

This is the crux. Answers to the design's explicit questions (all VERIFIED against §2):

- **Can Studio place a persistent boundary between statements inside a block?**
  **No (v1).** The walker executes a compound statement's body inside one `eval_stmt`
  call (§2.4); there is no way to *stop after the 3rd statement of a loop body and later
  resume there* without re-running the enclosing construct. A persistent boundary there
  would be a lie — it could not be a replay root.
- **If yes, can replay safely stop there?** It cannot, for the same reason. Replay
  granularity is a whole top-level statement.
- **Does the enclosing control-flow context need reconstruction?** For a top-level seam,
  **no** — there is no enclosing loop/branch state at the top level (§2.3). That is
  precisely why top-level seams are safe and inside-block seams are not.
- **Can a section begin inside a loop / end mid-iteration?** **No (v1).** A section is
  bounded by top-level seams; a loop is executed atomically within one section.
- **Can a user execute "just this loop" as one section?** **Yes** — put a boundary
  before and after the whole loop; the loop is then its own section, executed in full.
  This is the natural unit and matches the evaluator.
- **What happens to `break`/`continue` if a section is executed independently?** They
  remain **inside** their loop (VERIFIED unwinds only to the enclosing loop,
  18248-18254, 18446-18454); a section is always ≥ one whole loop, so `break`/`continue`
  never cross a section boundary. A stray top-level `break`/`continue` is already a
  runtime error (18381-18400) and simply surfaces as a section error (§17).
- **What if a condition takes a different branch during replay?** Because replay re-runs
  the real code from a deterministic prefix (§8), the branch taken is whatever the code
  computes — Studio does not record/replay branch decisions, it **re-executes** them.
  Determinism is a §9 concern (captured seed/time), not a boundary concern.

**Distinction (REQUIRED, mirrors design §5.2):**

| Structural execution boundary | Debugger stepping / breakpoint |
|---|---|
| persistent; a seam between top-level statements | transient; any line, incl. inside loops/functions |
| a replay/branch/state root (§8, §13) | a place execution *pauses during an active run* |
| coarse (whole top-level statements) | fine (single line/expression) |
| survives edits via structural anchor (§12) | recreated per debug session |

> **REQUIRED:** persistent sections are **not** required to support every location a
> debugger can stop. BP-only locations (inside loops/functions/branches) are served by
> the debugger, which pauses a live run and never needs to *resume by replay from that
> inner point*.

---

## 6. Functions, programs, libraries

### 6.1 Functions (VERIFIED + PROPOSED)

- **Declarations vs. bodies.** A top-level `function` *declaration* seam is an ordinary
  top-level boundary (the declaration registers a name, 18276-18278). A seam **inside**
  a function body is **BP-only** — the body runs only when invoked, in a `local_env`
  that is discarded on return (§2.5), so it can never be a persistent replay root.
- **"Run Section" on a section that contains a function declaration** means: execute
  that declaration (register the function) as part of the prefix — not "call the
  function". Calling happens only where the code calls it.
- **Can Studio show boundaries inside functions?** For **debugging** yes (breakpoints,
  step); as **persistent replay roots**, no (v1). This is the recommended split.
- **Locals / call stack.** Function locals are frame-local and vanish on return (5467);
  they are visible **only during an active debugger stop**, never as persistent
  boundary state (§14). Persistent state is the global env.

### 6.2 Programs (VERIFIED)

- A file with a `program` block executes **that block's body** (18603-18605), with
  top-level declarations hoisted first (18569-18581). **Therefore Studio's sections for
  such a file are the seams of the program-block body**, and the boundary `scope`
  (§3.2) names the block. Declarations outside the block are loaded before section 1
  (hoisted) — replay reproduces this.
- Only one program block may run (18554-18560); a second is a runtime error Studio
  should surface at anchor-time, not at run-time, if it can (PROPOSED static check via
  the reentrant parse, §2.11).

### 6.3 Libraries & `load` (VERIFIED)

- `library` blocks initialize **once per runner** and only register declarations
  (§2.6/§2.7). Across replay, because Studio starts a **fresh** runner each time (§8),
  each `load`/library initializes exactly once per replay — no accumulation.
- **Repeated replay does not re-run library init side effects**, because (a) a runner is
  fresh (nothing to accumulate) and (b) within a runner `load` is idempotent (§2.7).
  The only "side effect" of `load` is registering names / setting a module-loaded flag.
- **PROPOSED hoist for incremental runs:** when executing a *single* section in
  isolation is ever offered, Studio should first replay the prefix so declarations and
  `load`s that section depends on are present. In v1 this is automatic because Studio
  **always replays the prefix** (§8, §10) rather than running a section against an empty
  environment.

---

## 7. Goto, gosub, labels — control-flow hazards

VERIFIED, these are **not hazards at the top level**, because the language already
forbids them there:

- **Boundaries around labels:** a top-level label is inert (18359-18360); a boundary at
  that seam is just an ordinary seam. Fine.
- **May a `goto` jump across a boundary?** At the top level there is **no `goto`** — it
  raises (18361-18369). Inside a function, `goto`/`gosub` are confined to the function
  body (5422-5457) and cannot escape it, so they cannot jump across a top-level section
  boundary. **VERIFIED: jumps never cross section boundaries.**
- **Can a section execute independently if control enters it from an earlier label?**
  There is no top-level label-based entry (labels are inert; no top-level goto). Control
  only ever enters a section by falling through from the previous section. So "replay
  from a known root" is the *only* entry mechanism, which is exactly the v1 model.
- **Can `gosub` state span boundaries?** No — the gosub return stack is per-invocation
  inside `invoke_function` (5409-5463) and does not exist at the top level. It cannot
  span sections.
- **Must replay always begin from a known earlier root?** **Yes (REQUIRED v1).** Because
  the only top-level entry is fall-through, a section is meaningful only as the
  continuation of its prefix. Studio never executes an arbitrary section against an
  unknown environment (§8, §10).

Because the language already bans the constructs that would break naive sectioning at
the top level, **no section needs a "not independently executable" flag for goto/gosub
reasons** in v1. (A different reason — external effects — does gate *automatic replay*;
that is §9, not a structural ban.)

---

## 8. Replay model

### 8.1 The v1 mechanism (PROPOSED, REQUIRED to be prefix-replay)

To restore boundary `B_k` (or run up to it):

1. **Fresh runner.** Spawn a new evaluator process (a Studio *runner*, using the actor
   fork+exec plumbing, 8354-8357; VERIFIED shared-nothing). This sidesteps the
   non-reentrant global state (§2.11).
2. **Parse & load.** The runner parses the current source (`gb_parse`, §2.11) and, on
   first reach, performs `load`s/declarations exactly as normal evaluation does
   (idempotent, §2.7).
3. **Execute the prefix.** Run `items[0..k-1]` of the executed list (top-level list or
   program-block body) to completion — this **is** ordinary evaluation stopped at a seam
   (the runner is told to halt when `pc` reaches the seam index; VERIFIED `eval_stmt_list`
   advances by whole statements, 18528-18548, so halting at a seam needs no evaluator
   surgery — a cooperative "stop after statement N" check).
4. **Capture state.** Serialize the global env's top-level variables
   (`serialize_value` per variable, §2.10) plus captured stdout/stderr/diagnostics, and
   ship them to Studio over the mailbox (design §7 async model).
5. **Present.** Studio reconstructs the inspector/console for `B_k` (design §6).

### 8.2 Replay root, section sequence (PROPOSED)

- **Replay root** = the **start boundary** (before statement 0) in v1 — a fresh runner
  always begins from the top. (Later ladder rungs let the root be the nearest cached
  checkpoint, §20 — UX unchanged.)
- **Section sequence** to reach `B_k` = `S_1 … S_k` run in order (fall-through). No
  section is skipped or reordered.

### 8.3 Caching opportunities (DEFERRED beyond v1)

- **Checkpoint after a section:** serialize the post-state of `S_i` (only the
  serializable global vars, §2.10) so a later restore of `B_j (j>i)` can start the
  runner from the checkpoint rather than the top. Requires a whole-env capture built
  from per-value `serialize` (there is no env serializer yet — §2.10; this is design
  §8.2 rung 2/3 and platform NAP-9).
- **Memoize pure sections:** a section classified Pure (§9) with unchanged upstream may
  reuse its cached post-state without re-running.

### 8.4 Failure & stale behavior (PROPOSED)

- **Section error:** replay stops at the failing statement; Studio reports the last
  good boundary, partial output, and the error (§17).
- **Stale source:** if an anchor no longer resolves (§12) or upstream changed, dependent
  boundaries/state are invalidated and marked stale — never shown as current (design
  §9.3; §11 here).
- **Auto-replay safety:** a prefix is auto-replayable iff **every** section in it is
  Pure or Capturable (§9). If any section is External-read, Studio may reuse captured
  provenance (DEFERRED, needs a provenance store) or ask. If any is External-effect,
  replay **requires explicit confirmation** (REQUIRED — never silently re-run writes,
  design §8.3).

### 8.5 Reused results vs. re-execution (PROPOSED)

For External-read/effect sections, Studio prefers **captured provenance** (the recorded
result/summary from the last real run) over re-execution when merely *restoring a view*
(design §8.3). Re-execution of such a section is an explicit, confirmed user action.

---

## 9. Determinism / effect classification

gBASIC has **no static type or effect system** (VERIFIED: dynamic values, no
declarations). So classification is a **conservative syntactic heuristic + user
override**, not an inferred effect system (REQUIRED: do not build one).

### 9.1 Classes (PROPOSED)

| Class | Meaning | Examples | Auto-replay |
|---|---|---|---|
| **Pure / deterministic** | same prefix ⇒ same result | arithmetic, string/record/array ops, pure function calls | ✅ freely |
| **Capturable nondeterminism** | nondeterministic unless a seed/input is captured | `random`/`random_int` (with captured `seed`), `now()`/time read (captured as an input value) | ✅ if the seed/time is captured & re-injected; else ⚠ |
| **External read** | reads outside state; repeatable-ish | file read, `input`, db query, http/LLM response, `receive` | ⚠ prefer captured provenance; confirm to re-read |
| **External effect** | mutates outside state | file write, db mutation, network send, email, `send`/`spawn` with effect, external process with side effects | ⛔ never silent; **confirm** each replay |

### 9.2 Inference strategy (PROPOSED v1: conservative denylist)

- Studio scans a section's AST (via the reentrant parse, §2.11) for **calls to known
  effectful/external builtins** (a maintained denylist: file writes, db `exec`,
  `send`/`spawn`/`receive`, `http.*`, `webclient.*`, `webserver.*`, `input`, LLM calls,
  process exec, etc.) and for values that carry **live non-serializable handles**
  (db/gobject/actor — VERIFIED unserializable, §2.10).
- **Default:** a section with no denylisted call and no live-handle dependency is
  **Pure**; anything touching the denylist is **External-read** or **External-effect**
  by the builtin's known direction; RNG/time calls are **Capturable**.
- **User override** (REQUIRED): the user (or project AI rules, design §16) may reclassify
  a section (e.g. mark a "read-only" db query as safely re-runnable, or force a section
  effectful). Overrides are metadata, audited in the action log (design §14).
- **Bias conservative:** when unsure, classify **External-effect** (confirm-required).
  A false "effectful" costs a confirmation; a false "pure" could silently repeat a
  write — unacceptable (design principle 6).

---

## 10. Section execution commands

Semantics of user actions (design §5, §6). **REQUIRED framing:** every "run" is
*replay of a prefix and/or continuation from a known valid state* — Studio never
executes arbitrary section text against an undefined environment.

| Command | Meaning | v1? |
|---|---|---|
| **Run to Boundary `B_k`** | fresh runner; execute prefix `S_1…S_k`; capture state at `B_k` | ✅ |
| **Run Section `S_i`** | ensure the runner is at `B_{i-1}` (replaying the prefix if needed), then execute `S_i` through to `B_i` | ✅ (defined as "from `B_{i-1}` through the next boundary", **not** isolated text) |
| **Continue** | from the current live runner state, execute the next section `S_{i+1}` | ✅ (if a live runner is retained) / else replay then continue |
| **Rerun Section `S_i`** | restore `B_{i-1}` (replay prefix), re-execute `S_i`; effectful ⇒ confirm (§9) | ✅ |
| **Run From Here** | run from the current section to end (or next branch point) | ✅ |
| **Restore Here `B_k`** | reconstruct state/inspector at `B_k` (replay or, later, checkpoint) without advancing | ✅ (replay-backed) |
| **Branch From Here** | make `B_k` a branch root and create an alternate continuation (§13) | state-only: ✅ (design Studio 2); code-overlay: DEFERRED (Studio 3) |

**Clarified (REQUIRED):** *"Run Section" ≡ execute from the last known valid boundary
state (`B_{i-1}`) through the next boundary (`B_i`)* — because top-level entry is only
by fall-through (§7), a section has no meaning in isolation. In v1, "the last known
valid boundary state" is obtained by **replaying the prefix** (a live retained runner is
an optimization, §20).

---

## 11. Boundary editing lifecycle

### 11.1 Add boundary (PROPOSED)

- **Anchor** to the chosen top-level seam (§12): compute the structural anchor from the
  current AST and store it with a `line/column` hint.
- **Snap:** if the drop point is not a legal seam (e.g. inside a loop body), snap to the
  nearest enclosing legal seam (before/after the whole construct) and tell the user
  (design §5).
- **State:** adding a boundary **subdivides** an existing section. The new `B` inherits
  its post-state from a replay to that seam on demand (it does not invent state). No
  existing results are invalidated by *adding* a boundary — the code and its effects are
  unchanged; only the *granularity of inspection* increased.

### 11.2 Move boundary (PROPOSED)

- Re-anchor to the new seam. Moving a boundary changes which statements fall in the
  adjacent sections, so **the two adjacent sections' captured post-states become stale**
  and are invalidated (marked stale, recomputed on demand). Downstream sections whose
  *prefix content is unchanged* need not be invalidated (their prefix is the same set of
  statements); Studio invalidates conservatively by prefix-content hash (§12).

### 11.3 Delete boundary (PROPOSED) — merges adjacent sections

- Removing `B_i` **merges** `S_i` and `S_{i+1}` into one section (design §5). The merged
  section's classification is the *union* (most-effectful) of the two (§9). The two old
  post-states collapse to one (the post-state of the merged section) — the intermediate
  state is discarded/invalidated.
- **If `B_i` was a branch root** (§13): deletion is **disallowed until its branches are
  resolved** (promoted/discarded) — REQUIRED, to avoid orphaning divergent
  continuations. Studio prompts to handle branches first.
- **Action history / provenance:** the delete is recorded (`boundary_removed`, design
  §14); provenance of the merged section supersedes the two prior sections' provenance
  (kept in history for "why did this change?", not as live state).

---

## 12. Structural anchoring & source-edit survival

**REQUIRED:** never anchor solely by line number; never silently attach a boundary to
the wrong statement (design §5.3).

### 12.1 Anchor model (PROPOSED)

A boundary anchor is a tuple:

- **scope path** — top level, or `program "name"` / (future) function for BP-only marks;
- **statement index path** — the seam's index within its list (e.g. "after top-level
  statement 7");
- **neighbour fingerprints** — a normalized-source fingerprint of the statement
  immediately *above* and *below* the seam (whitespace/comment-insensitive hash of the
  AST subtree), so the seam can be re-found if indices shift;
- **kind context** — the `AstStmtKind` of the neighbours (`ast.h:6-32`);
- **position hint** — `line/column` (VERIFIED available), used only as a tiebreaker/UX
  hint, never as the primary key.

(Studio generates a **persistent boundary id** separately, §3.2 — the anchor is how the
id re-finds its seam after edits.)

### 12.2 Re-resolution states (PROPOSED)

On each open/parse, re-resolve every anchor against the fresh AST:

| State | Condition | Studio action |
|---|---|---|
| **attached** | index + both neighbour fingerprints match | reattach silently |
| **reattached** | fingerprints match at a shifted index (lines inserted above, statement moved) | reattach at the new seam; log it |
| **ambiguous** | fingerprints match in >1 place | mark ambiguous; ask the user; do **not** guess |
| **detached** | neighbours found but fingerprints changed materially (statement rewritten) | mark detached; keep as a floating marker near the hint; invalidate dependent state |
| **stale** | the anchored statement was deleted / block restructured | mark stale; invalidate downstream state (§11); never auto-attach |

### 12.3 Edit scenarios (PROPOSED mapping)

| Edit | Result |
|---|---|
| lines inserted above | **reattached** (index shifts, fingerprints hold) |
| statement reformatted (whitespace/comments) | **attached** (fingerprint is normalized) |
| statement moved | **reattached** if it moved as a unit; **ambiguous/stale** if it crossed other boundaries |
| statement substantially changed | **detached** |
| block restructured (loop split, `if` refactored) | affected inner BP marks **stale**; top-level seams around it re-resolve by neighbours |
| external edit (edited outside Studio) | same re-resolution runs on next open — VERIFIED source is never Studio-owned (design §2.1), so this is the normal path, not a special case |

**Effect on state:** any non-`attached` outcome invalidates that boundary's captured
state and everything downstream of it (§11, design §9.3). Stale results are never shown
as current.

---

## 13. Interaction with exploratory branches

(Design §9. Keep Studio branches distinct from Git — REQUIRED.)

- **Branch root = a boundary.** Making `B_k` a branch root captures its post-state as
  **shared ancestry** (the prefix `S_1…S_k`). Everything **strictly below** `B_k`
  belongs to the selected branch (design §9.1 mental model).
- **State-only branch** (design §9.2, Studio 2): same source, different runtime below
  `B_k` (different inputs/params). Served entirely by replay: each branch is a distinct
  continuation run from the shared `B_k` state. No source change. ✅ v1-adjacent.
- **Code-overlay branch** (design §9.2, Studio 3, DEFERRED): the overlay applies
  **strictly after `B_k`** (never above it — that would change shared ancestry). Stored
  in metadata, materialized only into the temp source the runner executes; the canonical
  `.bas` is untouched (design §2.1).
- **If the root boundary moves:** the shared-ancestry prefix changes ⇒ all branches at
  that root are invalidated/recomputed (§11.2, design §9.3). Studio warns before moving
  a branch root.
- **If upstream code changes:** descendants (all branches) go **stale** (§12, design
  §9.3); overlays are re-applied/rebased where they still apply, else surfaced as a
  conflict — never silently misapplied.

---

## 14. State / results semantics

At each completed boundary Studio shows changed variables, console output,
errors/warnings, and viewers (design §6).

- **Conceptual boundary state (PROPOSED):** the **global environment's top-level
  variables** at `B_k` (VERIFIED: the only persistent state, §2.5), captured by
  serializing each (§2.10), plus the section's captured stdout/stderr/diagnostics and
  provenance (§9).
- **Changed vs. all (PROPOSED):** "changed variables" = a diff of the global env between
  `B_{k-1}` and `B_k`. **v1 scope: top-level (global) variables only.** Local variables
  are visible **only during an active debugger stop** (§6.1), never in persistent
  boundary state.
- **Change detection (PROPOSED):** compare serialized forms (§2.10) or a per-variable
  version stamp. For deep structures, a content hash of the serialized value is the v1
  mechanism (value identity is unreliable across a fresh replay runner — each replay
  rebuilds values). Deep structural diffing of records/arrays is DEFERRED (needs the
  reflection facility, platform NAP-9; VERIFIED `keys`/`values`/`type`/`is_*` already
  enumerate values for a shallow-to-deep diff).
- **Non-serializable values (VERIFIED constraint):** db/gobject/actor handles cannot be
  captured (§2.10). A section that leaves such a value in a top-level variable has that
  variable shown as *"live handle (not captured; recreated on replay)"* — never
  serialized, never restored (design §8.3). Replay re-creates it by re-running the code.

---

## 15. Watchers

VERIFIED (§2.8): watcher cascades triggered by a statement **drain synchronously to
completion before that statement returns** (`watcher_trigger_change` → `watcher_drain`,
3561-3576), fire only on **global-env** mutations (3562), and are bounded by a
10 000-execution cycle guard (3517-3531).

**Design consequences (PROPOSED):**

- **Watcher effects belong to the section that caused them.** A watcher body that runs
  because `S_i` mutated a watched top-level variable completes *within* `S_i`; its
  effects are part of `S_i`'s post-state at `B_i`. There is no "between-boundary" limbo.
  (Confirms design §15's proposed rule against VERIFIED behavior.)
- **Replay:** because draining is deterministic given the same mutations, replaying `S_i`
  reproduces the same watcher cascade — no special handling. **Unless** a watcher body
  itself performs an external effect, in which case the section inherits that effect
  class (§9) and is replay-guarded accordingly.
- **Registration:** `watch` blocks register at top level (18294-18300); a boundary after
  a `watch` registration is trivially replayable (registration is a declaration-like
  effect on the runner, reproduced from the top).

---

## 16. Actors / concurrency

VERIFIED (§2.9): actors are shared-nothing processes; `receive` blocks; handles are live
and unserializable; children are reaped at program end.

**Conservative v1 rule (PROPOSED, REQUIRED conservative):**

- A section that **spawns an actor, sends/receives, or leaves actors running** is
  classified **External-effect / actor** (§9) and is **not automatically replayable** —
  replay requires confirmation, because re-running spawns new processes and re-sends
  messages (side effects).
- A section is considered **safely resumable only when actors have reached a quiescent
  state** — no live child handles remain in top-level variables and no pending receive.
  In v1, Studio treats any section with outstanding actor state as **effectful and
  cold-on-restore**: restoring its boundary shows captured provenance (last output), and
  re-execution is an explicit action (design §8.3).
- **Actor output arriving after the section appears complete:** because `receive` is
  explicit and blocking (§2.9), a section only observes actor output where the code calls
  `receive`. Output not yet received is **not** part of the boundary state; it is
  captured only if/when a later section receives it. Studio does not attempt to
  asynchronously fold late actor output into an already-closed boundary in v1 (DEFERRED —
  would need the design §7 loop integration to attribute late frames).
- **Live handles are never serialized** (VERIFIED §2.10) — an actor handle in a top-level
  variable is shown as a live capability, recreated only by re-running (§14).

---

## 17. Error behavior

When a section errors before reaching its ending boundary (VERIFIED error propagation:
`eval_error_result` unwinds; `error_mode` governs GOTO/RESUME-NEXT/STOP, 18312-18343;
`on error resume next` unwinds to the top frame — see memory `gbasic_error_handling_gotcha`):

- **Failed-section state (PROPOSED):** Studio preserves the **last valid boundary**
  (`B_{i-1}`, fully replayable) and captures **partial console output** produced before
  the error, plus the structured diagnostic (VERIFIED `--json-diagnostics` exists,
  design §6.1).
- **Variable inspection:** the global env *as of the failure point* may be shown
  **best-effort** from the runner before it exits, clearly marked *"partial — section
  did not complete"*. The authoritative restorable state remains `B_{i-1}`.
- **Rerun:** "Rerun Section" restores `B_{i-1}` and re-executes; effectful sections
  confirm (§9).
- **Downstream boundaries:** become **unavailable/stale** — a section cannot be reached
  through a failing prefix. Studio marks `B_i` and everything after it stale until the
  error is resolved (design §9.3).
- **`on error` modes are evaluator-global state** (VERIFIED 18312-18326): replay
  reconstructs them by re-running the prefix (the mode set by an earlier section is
  reproduced), so error-handling context is consistent across replay without special
  capture.

---

## 18. UI semantics

(Design §18; concise, no final styling.)

```text
──── (start) ─────────────────
load "stats"                     ← S1  [Pure]        ✓ ran 14:02
customers = read_csv("m.csv")    ← S2  [Ext-read]    ✓ (provenance cached)
──── Boundary A ───────────────  ← restored here ●
summary = analyze(customers)     ← S3  [Pure]        ✓
──── Boundary B ───────────────  ← last executed ✓
write_report(summary, "out.pdf") ← S4  [Ext-effect]  ⚠ confirm to run
──── [Baseline] [Robust] [+] ──  ← Boundary C = branch root
…selected branch below…
```

Selecting a **section** focuses its code and shows *its* post-state/console in the
panes (design §6). Selecting a **boundary** = "state after everything above". The UI
must legibly distinguish (PROPOSED states, no colors specified):

- **current / restored** boundary (where the inspector is showing);
- **last successfully executed** boundary;
- **stale** sections (upstream changed / anchor not `attached`, §12);
- **failed** sections (§17);
- **effectful / non-replayable** sections (§9, confirm-to-run);
- **branch roots** (§13).

---

## 19. Recommended v1 rule set (deliberately conservative)

**REQUIRED for v1.** A small model that expands cleanly (§20):

1. **Placement:** a persistent execution boundary may be placed **only at a top-level
   statement seam** of the executed list — the file's top-level statement list, or a
   single `program` block's body (§4, §6.2). Nowhere else.
2. **No inside-block persistent boundaries:** seams inside loops/`if`/`consider`/
   function bodies/`with lock`/`without watchers` are **debugger-breakpoint locations
   only** (§5), never replay roots.
3. **Meaning:** boundary `N` = global state *after* statements `0..N-1` complete (§3.1).
4. **Execution = prefix replay:** every run/restore starts from a known valid root and
   executes forward; **no section runs in isolation** (§7, §10).
5. **Fresh-runner replay** in a spawned process (§8.1); state captured by per-value
   serialization of top-level (global) variables (§2.10, §14).
6. **Auto-replay only for Pure/Capturable prefixes;** External-read prefers captured
   provenance; **External-effect always requires confirmation** (§9, §8.4) — never
   silently re-run a write/send/spawn.
7. **Actors/effects conservative:** sections with outstanding actor state or external
   effects are effectful, cold-on-restore, confirm-to-rerun (§16).
8. **Structural anchoring, never line-only;** non-`attached` re-resolution invalidates
   downstream state and is surfaced, never silently reattached to the wrong statement
   (§12).
9. **Changed-variable view = global vars only;** locals only at active debug stops
   (§14, §6.1).
10. **Branch roots block boundary deletion** until branches are resolved (§11.3).

This rule set is *implementable against the current evaluator with a single cooperative
"stop after top-level statement N" hook* and per-value serialization — **no interpreter
re-entrancy, no snapshot machinery, no new control-flow surgery** required for v1.

---

## 20. Future evolution (user model stays stable)

The user-facing model (§3.1, §10) is fixed; mechanisms improve underneath (design §8.2
ladder):

- **Finer-grained resumability** — persistent boundaries inside blocks/functions become
  possible only after the **interpreter-context refactor** (PLAN Phase 3) makes the
  evaluator re-entrant/snapshottable; until then they stay debugger-only. UX unchanged.
- **Cached checkpoints** (§8.3) — replay starts from the nearest serialized post-state
  instead of the top. Needs a whole-env capture built over per-value serialize + the
  reflection facility (platform NAP-9). UX unchanged (faster restore).
- **Environment serialization** — a real `Env` serializer generalizes §14 capture and
  enables cross-session state restore.
- **True interpreter snapshots** — fork-based or context-struct snapshots replace prefix
  replay for O(1) restore (design §8.2 rung 4).
- **Copy-on-write state** — the value model is already refcounted COW cells (VERIFIED,
  design §8.2 rung 5), a natural substrate for cheap branch divergence.
- **Deeper function-level interactive execution** — step/inspect inside functions as
  first-class sections, once re-entrancy lands.
- **Richer branch comparison** — deep structural diff of branch post-states, once the
  reflection/diff facility exists (§14, NAP-9).

At every rung, "boundary = state after the code above it; restore/branch/replay from
here" remains the whole user model.

---

## 21. Open questions

Tagged BLOCKS-EARLY (needed before Studio 1 implements this) or DEFERRABLE.

| # | Question | Status |
|---|---|---|
| B1 | The **cooperative "stop after statement N" hook** in `eval_stmt_list` — is a runner-mode counter acceptable, or does halting need a first-class mechanism? (VERIFIED: the walk advances by whole statements, 18545, so a counter suffices — but confirm no re-entrancy corner cases with nested `load`.) | **BLOCKS-EARLY** |
| B2 | **Whole-env capture** for §14/§8.3 — v1 can iterate the global env and per-value `serialize` each top-level var; is that sufficient, or is NAP-9 reflection required first? (VERIFIED: no env serializer today; `keys`/`values`/`type`/`is_*` exist for values.) | **BLOCKS-EARLY** (picks the capture path) |
| B3 | **Effect denylist** — the concrete list of builtins classified External-read/effect (§9.2) and how it is maintained as the stdlib grows. | **BLOCKS-EARLY** (needed for trustworthy replay) |
| B4 | **Capturable time/RNG** — is `seed`/`random` state reachable for capture+re-injection (§9), or is time/RNG treated as External-read in v1? | **BLOCKS-EARLY** (determinism trust) |
| B5 | **program-block scoping** — should Studio present a program-block file's sections as the block body (recommended §6.2) or refuse boundaries in such files in v1? | **BLOCKS-EARLY** (affects §6.2) |
| B6 | **Neighbour-fingerprint normalization** — exact rule for AST-subtree fingerprints (§12.1): which nodes/positions are included, how comments/whitespace are excluded. | **DEFERRABLE** (v1 can start with a coarse fingerprint) |
| B7 | **Live retained runner** vs. always-fresh replay (§10) — keeping a warm runner for "Continue" is an optimization; does it risk divergence from clean replay? | **DEFERRABLE** |
| B8 | **Late actor output** attribution (§16) — folding asynchronously-received frames into a closed boundary. | **DEFERRABLE** (v1: not folded) |
| B9 | **Deep change detection / diff** of records/arrays for the changed-variables view (§14). | **DEFERRABLE** (v1: content-hash of serialized form) |
| B10 | **Debugger-breakpoint mechanism** (§5) — the finer-grained stepping model is out of scope here; it needs its own spec and likely the re-entrancy refactor. | **DEFERRABLE** (separate document) |

---

## Final answers

**1. What exact AST locations should persistent execution boundaries support in v1?**
Only **top-level statement seams** — the point *between two adjacent statements* of the
executed statement list: the file's top-level `AstStmtList`, or, when the file has a
`program` block, that block's body list. Concretely: before/after any top-level
statement (assignment, expr/call, `print`, `load`/`use`, a *whole* `if`/`while`/`for
each`/`consider`/`with lock`/`without watchers`, a function/modifier/`watch`/`library`
declaration, a method-attach statement). **Never** between statements *inside* a
compound body — the evaluator executes compound bodies atomically and cannot resume
mid-body (VERIFIED §2.4, §5).

**2. Which locations should be reserved for debugger breakpoints only?**
Everything **inside** a block or function body: statements within a loop/`if`/`consider`
body, inside `with lock`/`without watchers`, and any line inside a function (which runs
only when invoked, in a discarded `local_env`). Also the function-internal `goto`/
`gosub`/`return`/`break`/`continue` control points. These are transient stops during an
active run, not persistent replay roots (§5, §6.1, §7).

**3. Can a section be executed independently, or must Studio replay from a prior valid
root?** It must **replay from a prior valid root**. At the top level the only entry into
a statement is fall-through from the previous statement (no top-level `goto`/labels —
VERIFIED §2.3, §7), so a section is meaningful only as the continuation of its prefix.
"Run Section `S_i`" means *"ensure state `B_{i-1}` (by replaying the prefix), then run
`S_i` through `B_i`"* — never arbitrary text in isolation (§10). A warm retained runner
is an allowed optimization, not the semantic model.

**4. What makes a section automatically replayable vs. confirmation-required?**
Its **effect class** (§9). **Pure** and **Capturable** (with captured seed/time)
sections replay automatically. **External-read** sections prefer captured provenance and
ask before re-reading. **External-effect** sections (file/db/network writes, `send`/
`spawn`, external processes) **always require explicit confirmation** — Studio never
silently re-runs an effect. Classification is a conservative syntactic denylist scan
plus user override, biased toward "effectful" when unsure (§9.2). Sections holding live
non-serializable handles (db/gobject/actor — VERIFIED §2.10) are effectful and
cold-on-restore.

**5. What is the safest rule for loops/functions/goto/gosub in v1?**
Treat every loop, `if`/`consider`, and function body as an **atomic unit** with respect
to persistent boundaries: a boundary may sit only *before or after the whole construct*,
so a loop is executed in full as one section and `break`/`continue` never cross a
section boundary (VERIFIED §2.4, §5). `goto`/`gosub` need no special handling because
they are **function-local and top-level-forbidden** (VERIFIED §2.3, §7) — they cannot
jump across a section boundary. Finer stops inside these constructs are debugger-only.

**6. What minimum runtime/introspection support is required before Studio 1 can
implement this model?** Three things, all small and none requiring the interpreter
refactor:
   - a **cooperative "halt the runner after top-level statement N" hook** in the
     top-level walk (VERIFIED feasible: `eval_stmt_list` advances by whole statements,
     18545 — a runner-mode counter suffices; open question B1);
   - **whole-env state capture** = iterate the global env's top-level variables and
     `serialize` each (VERIFIED per-value serialize exists, §2.10; no env serializer yet
     — v1 builds one trivially by iteration; a richer version is platform NAP-9);
   - the **reentrant front end** to compute legal seams + structural anchors without
     running code (VERIFIED `gb_parse`/`gb_parse_ctx`, §2.11).
   Everything else Studio needs (fresh-runner replay via the actor fork+exec plumbing,
   the async result path) already exists (VERIFIED §2.9, design §7). The changed-variable
   *diff view* and *checkpointing* can start coarse (content-hash) and improve with
   NAP-9 reflection — they do **not** block Studio 1.

---

## Contradiction check against `docs/gbasic_studio_design.md`

**No contradictions found.** This specification refines design §5/§22-Q1 without
conflicting with any of it:

- Design §5's "boundaries snap to structurally valid AST locations" is made precise here
  as *top-level statement seams* (§4) — a **narrowing**, not a conflict. Design §5
  already anticipated "not arbitrarily inside active loops/functions" (design §5, §19)
  and reserved finer stops for debug breakpoints (design §5.2) — this document confirms
  that split against the VERIFIED evaluator.
- Design §8 (replay-first, non-in-process-snapshot, actor runners) is exactly the §8
  model here.
- Design §8.3 non-rewindable-resource honesty maps 1:1 to §9's effect classes and the
  VERIFIED serialize rejections (§2.10).
- Design §9 branch model and §12 anchoring are consistent with §13/§12 here.

One item to **flag for deliberate confirmation** (not a contradiction): design §5 lists
"execute through a section / continue execution / restore an earlier execution point"
as user capabilities without fixing whether a section can run *in isolation*. This
document resolves that to **"always replay from a prior valid root, never isolated"**
(Final answer 3). This is a *tightening* consistent with design §8's replay-first stance
— but if a future design intends true isolated-section execution, design §5 and this §10
should be reconciled then. No change is made to `gbasic_studio_design.md` in this task.

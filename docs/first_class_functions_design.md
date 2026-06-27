# First-Class Functions & Methods — Design

Status: **in progress (2026-06-26). Phase 0 (function values) shipped; Phases 1–4
pending** (see §11). This is consciously a **fourth pre-freeze language thread**, after
PBI, Unicode, and Multiprocessing (all complete) — it adds surface, so it moves the
freeze deliberately rather than by accident. The goal is to let *behavior* travel with
data, i.e. real methods on objects, built on the smallest enabling primitive: function
values.

This document pins the decisions reached in design discussion so the phased plan in
§11 can be executed without re-litigating semantics, in the same discipline as
`pbi_design.md` / `unicode_design.md` / `multiprocessing_design.md`.

## 1. Goal and reframe

gBASIC already has prototype objects: PBI gives records *data* with inheritance
policies (`copy` / `link` / `exclude` / `reset`), derived via `new`. What is missing
is **behavior on those objects** — methods that read and write the object they
belong to and dispatch off it. The enabling primitive is **first-class functions**:
functions that can be stored in a field, passed, and returned. Methods, `constructor`,
and prototype "classes" are then ordinary uses of that primitive plus PBI — no
bespoke OO machinery in the runtime.

## 2. The core decision: references, not closures

Function values are **references to registered functions**. They do **not** capture
the lexical environment in which they were defined or stored. This is "option (a)"
from the design discussion; full lexical closures ("option (b)") are **deferred**
(§13).

**Invariant (load-bearing): a function value is always a reference to a registered
function — never an anonymous, capturing closure.** Everything below depends on it,
because it buys two properties a refcounted, manual-memory, process-isolated runtime
needs:

- **No reference cycles.** A method that closed over `self`, stored in a field of
  `self`, would be an instant cycle that refcounting cannot collect. With references
  only, the field holds a name, not a back-pointer, so no cycle exists.
- **Actor-sendable by name.** A function value serializes as its registered name
  (§10). Because every actor execs the *same* program, the name resolves on the
  other side — consistent with how `spawn` already names entry functions.

## 3. Function values (the primitive)

- A **bare function name** (no parentheses) evaluates to a function value:
  `f = deposit`. `deposit(x)` is still a call; `deposit` alone is the value.
- A function value can be stored in a variable, an array element, or a record field,
  and passed to / returned from functions.
- `type(f)` is `"function"`. Two function values are **equal** iff they reference the
  same registered function. `print`/`quote` of a function shows a debug form
  (`<function deposit>`); `quote` does not treat it as a scalar (consistent with how
  `quote` already rejects records).
- Calling a function value held in a variable uses the normal call form once the
  variable resolves to it: `f(100)`. (A bare `f(100)` has no receiver — see §4.)

## 4. Methods and `this`

A **method** is a function value reached **through a record field** and called. The
object it was called on is the **receiver**, bound to the keyword **`this`** at the
**call site** — not captured at definition time. That call-site binding is exactly
why methods work under §2 with no capture.

Rules:

- **`obj.method(args)`** binds `this` to `obj` inside the body for that call.
- **A bare call `method(args)`** has no receiver; **referencing `this` there is a
  runtime error** ("`this` is only bound inside a method call").
- **`this` is read-only**: you cannot rebind it. You mutate *through* it —
  `this.balance = …` — which is ordinary field assignment, so **PBI field policies
  apply automatically** (a `copy` field stays private to the instance; a `link`
  field writes through). Methods and PBI compose with **no new rules**.
- **Dynamic dispatch falls out for free**: two records carrying different function
  values in the same field, called as `a.m()` and `b.m()`, each get their own `this`
  and their own behavior.

**`this`, not `self`** — because `self()` is already the actor builtin returning the
current actor's handle. Reusing `self` for the receiver would collide; `this` is free
(verified: not a token today) and unambiguous.

## 5. The `X.y(args)` disambiguation (key reconciliation)

`X.y(args)` at a call site **already** parses as a *qualified library call* —
"function `y` from library `X`" (e.g. `math.add(2,3)` after `load math`,
`ast_qualified_call`). Method calls reuse that surface, so the evaluator gains one
disambiguation rule, decided at eval time:

> When evaluating `X.y(args)`: if `X` is a **variable bound to a record** whose field
> `y` holds a **function value**, it is a **method call** (`this` = that record).
> Otherwise it is a **qualified library call**, exactly as today.

Libraries and variables live in different namespaces, so this is decidable. If a
variable and a library ever share the name `X`, **the record variable wins** (local
data shadows a module name); document loudly. No grammar change — only the evaluator's
qualified-call path gains this branch.

## 6. Three ways to attach behavior (and the one excluded)

1. **Reference a named function in a record literal:** `{ deposit: deposit }` — the
   right-hand `deposit` is a function value (a reference).
2. **Assign after creation:** `account.deposit = deposit`.
3. **Define-and-attach in one statement (sugar):**
   `function account.deposit(amount) … end function`.

**Excluded — deliberately:** an inline function *literal* inside the braces
(`{ deposit: function(amount) … end function }`). A function literal in expression
position is what visually invites capture (§2/option b) and is the natural home for
closures; excluding it keeps the §2 invariant intact. You lose no expressiveness —
forms 1–3 cover every case — you trade one convenience for the invariant that makes
the whole model hang together.

## 7. Declaration vs statement (the hoisting split)

A **bare name is a declaration; a dotted name is an assignment statement.**

- **`function foo()`** — a top-level **declaration**: registered before any line runs
  (hoisted), callable from anywhere regardless of order, with a real top-level name.
- **`function obj.method()`** — an **executable statement**, equivalent to
  `obj.method = <function value>`. It runs when control reaches it, requires `obj` to
  already exist and be a record (else a runtime error), and creates **no** top-level
  name. It desugars to an **internal registered function** plus a field store — still
  a named reference under the hood (§2), the name just isn't typed by the user.

Consequences:

- **Only the attachment is sequential.** A method *body* may freely call hoisted
  top-level functions defined later; non-hoisting applies to when the method becomes
  available on the object, not to what the body can see.
- **Practical rule:** wire up an object's methods before calling them.
- **Grammar:** the dotted form must be accepted in **statement position** (inside
  `program` / functions / top-level execution), not only as a top-level declaration —
  that is what makes the non-hoisting expressible. This maps onto a distinction the
  language already has (named declarations hoist; value assignments run in place).

## 8. `constructor`

A method named **`constructor`** is auto-invoked by `new`. No new keyword (it is an
ordinary field-name convention) and **no new grammar**: `new` already supports
`new proto` and `new proto with { …overrides… }`.

- After `new` derives the instance (applying PBI policies and any `with` overrides),
  if the instance has a `constructor` function field, it is called with **`this` =
  the new instance** and no separate argument list — inputs arrive via `with { … }`,
  which the constructor reads from `this`. Its return value is ignored.
- It composes with PBI: the `constructor` field is itself a function value, so it
  copies into instances like any field; a derived prototype can override it.

(A positional `new proto(args)` that forwards to `constructor` is a possible future
convenience but is **not** in v1 — `with` already carries inputs and needs no new
syntax.)

## 9. No `destructor`

Dropped deliberately. Deterministic finalization is unsafe in this runtime:

- It would run user gBASIC code from inside `value_free`, which fires mid-operation
  and mid-error-cleanup all over `eval.c` — a re-entrancy hazard.
- Resource-holding objects are the most likely to form **cycles**, which never reach
  refcount 0, so the destructor would silently never run — failing worst where it is
  needed most.
- It is **incoherent with the actor model**: process death (`PDEATHSIG` / `kill`)
  runs no destructors, so cross-boundary cleanup cannot rely on them anyway.

The honest replacement is **explicit disposal** — a `close`/`dispose` method
convention (and possibly a future `with`-style scoped cleanup) — which is
deterministic, re-entrancy-safe, and degrades sanely when a process is killed. This
is already how gBASIC manages file/dir/connection resources internally.

## 10. Actor consistency and serialization

A function value serializes as its **registered name** (a new `SER_FUNCTION` tag
carrying the name); deserialization resolves it through the function registry. Within
one program (the actor case — every actor execs the same program) it always resolves,
exactly like a `spawn` entry name. A record field holding a function therefore sends
across a mailbox as a reference, with no capability leak (unlike actor handles, which
remain non-serializable).

- **Internal (dotted-def) functions need a deterministic, stable name** across the
  same program so parent and child resolve the same reference — derive it from source
  position (file:line:col) or a deterministic per-program counter, not a runtime
  address.
- Resolving a function name absent from the receiving program is an error (same shape
  as an unknown `spawn` entry). User-facing `serialize`/`deserialize` within one
  program round-trips; across *different* programs it may fail — document.

## 11. Phased plan (each phase green before the next)

- **Phase 0 — function values (invisible-ish foundation). DONE (2026-06-26).** Added the
  `VALUE_FUNCTION` kind (an owned `{name, library}` registered-name reference, never a
  closure), bare-name-evaluates-to-a-value (`env_get` falls back to `function_resolve`
  when no variable shadows the name; variables win), calling a function value held in a
  variable (`f(args)`, resolved in `eval_call`'s tail after user functions and builtins),
  `type()` → `"function"`, same-reference equality (`=`/`!=` only; ordering and other ops
  raise; non-function compares unequal), `print`/`string()` show `<function NAME>`, and
  `quote`/`encode`/`serialize` reject (functions are not yet scalar-encodable or
  actor-sendable — Phase 4). Round-trips through variables, arrays, and record fields as
  plain data. Tests: `examples/first_class_function_test.bas`,
  `tests/negative_function_compare_order.bas`. Valgrind-clean. This settles open questions
  §12.3 (type `"function"`, same-reference equality; `is_function` not added) and §12.4
  (`<function NAME>` representation; `quote` rejects).
- **Phase 1 — methods + `this`.** The §5 disambiguation in the qualified-call path,
  `this` bound at the call site through a record field, the bare-call-has-no-`this`
  error, `this.field` assignment honoring PBI policy. Dispatch demonstrated by two
  records sharing a field name with different function values.
- **Phase 2 — attach sugar.** The dotted `function obj.method()` statement (desugar to
  internal registered function + field store), accepted in statement position;
  confirm forms 1–2 (literal reference, post-hoc assignment) work from Phase 0.
- **Phase 3 — `constructor`.** `new` invokes a `constructor` field after derivation,
  `this` = instance, inputs via `with`.
- **Phase 4 — actor-sendable functions (optional, ties to multiprocessing).**
  `SER_FUNCTION` by name + registry resolution; deterministic naming for internal
  functions; record-with-method sends across a mailbox.
- **Deferred (documented, not built):** §13.

**Sequencing note.** Phases 0–2 are the high-confidence core and the part justified
by present evidence (the actor supervisor that couldn't be parameterized because
functions weren't values). `constructor` and actor-sendability can follow. The whole
thread should be **validated by building** — the site and libraries — before the
freeze locks it; usage should decide whether closures/bound methods ever earn their
complexity, not speculation.

## 12. Open questions (must-decide, non-blocking for Phase 0)

1. **Name-collision precedence** when a variable and a library share `X` in `X.y()` —
   proposed: record variable wins (§5); confirm.
2. **Internal function naming** scheme for dotted defs (§7, §10) — source-position vs
   deterministic counter; must be stable across the same program for serialization
   and equality.
3. **Function-value equality and `type`** surface (§3) — confirm `"function"` and
   same-reference equality; decide whether to add `is_function`.
4. **`print`/`quote` representation** of a function value (§3).
5. **`constructor` error propagation** — a constructor that raises: does `new` return
   the half-built instance, `nothing`, or propagate? (Lean: propagate, no instance.)

## 13. Deferred to future (documented, not built)

- **Lexical closures (option b).** Capture of the defining environment. Powerful, but
  reopens reference-cycle collection in a refcounted runtime; add only if real code
  demands capture, and only with a cycle story.
- **Bound methods.** `f = obj.method` *remembering* `obj`. This reintroduces the
  record → field → bound-value → record cycle (§2); v1 binds `this` only by the direct
  `obj.method()` call form, so detaching loses the receiver.
- **Inline function literals / lambdas** in expression position (§6).
- **`destructor` / implicit finalization** (§9) — replaced by explicit disposal.
- **Positional `new proto(args)`** forwarding to `constructor` (§8) — `with` suffices.

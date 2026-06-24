# Policy-Based Inheritance (PBI) — Design

Status: **proposal / not yet implemented.** This document works through the
language model, syntax, runtime/memory model, a staged implementation plan
grounded in the current code, and the open questions. It is the design counterpart
to the Unicode and multiprocessing threads; where those threads converge with PBI
is called out explicitly (§9).

PBI is gBASIC's object model. Rather than classes and instances, it is
**prototypal**: any record can serve as a prototype, and a new instance is
*derived* from it. What makes it PBI rather than plain prototyping is that each
property carries a **derivation policy** that decides how that property crosses
the boundary from prototype to derived instance.

---

## 1. Why PBI (and why this shape)

Real-world modelling needs three things that gBASIC's current deep-copy records
cannot express:

1. **Shared mutable state** — many instances that genuinely refer to one thing
   (a shared configuration, a parent aggregate, a connection pool handle), where
   a write through any instance is seen by all.
2. **Independent per-instance state** — fields that look shared but logically are
   not, where a write through one instance must not disturb the others.
3. **Fresh-per-instance state** — fields that must be reinitialised for every new
   instance (an id, a creation timestamp, an empty working collection) rather than
   inherited from the prototype at all.

Today gBASIC offers exactly one behaviour: assignment deep-copies a record
(`value_copy`, `src/eval.c:628`), so everything is case 2 and only case 2. There
is no way to share, and no way to reset. PBI makes the choice **per property** and
makes the default the safe one (independent copy), so existing record code keeps
its current meaning.

The design is deliberately prototypal because gBASIC already has structural
records and no class system (`src/eval.c` has `VALUE_RECORD` but no class/`new`
machinery). A prototype *is* a record; an instance *is* a record. There is no
second concept to learn.

---

## 2. The four policies

A property is annotated with one of four policies. The policy governs **what
derivation does** with that property; it does not change how the property behaves
once the instance exists.

| Policy | At derivation, the derived instance gets… | After derivation |
|--------|-------------------------------------------|------------------|
| `copy` *(default)* | a logically independent copy of the prototype's value | writes are private to each instance |
| `link` | the **same** storage as the prototype | writes through either side are visible to both |
| `reset <expr>` | a fresh value from evaluating `<expr>` at derivation time | a normal private field thereafter |
| `exclude` | nothing — the property is not inherited | the instance simply lacks the property |

`copy` is the default so that an un-annotated record literal derives exactly like
today's deep copy. Choosing `copy` explicitly only documents intent.

### copy is copy-on-write

`copy` does **not** eagerly duplicate. At derivation the prototype and instance
share one storage cell; the duplication happens lazily on the first write to
either side ("fork on write"). This keeps `new` cheap regardless of record
size and is invisible to the program — semantically `copy` is an independent
copy; mechanically it is shared until someone writes.

The fork is **symmetric**: whoever writes first forks. If the prototype is
mutated after instances were derived, the prototype forks and the instances keep
the old shared value; if an instance is mutated, the instance forks and the
prototype keeps it. Neither side is privileged.

### link is identity, not copy

`link` is the opposite: the cell is shared and **never** forks. A write through
the prototype or any linked instance is seen by all of them. `link` is how PBI
expresses identity — "these instances refer to the same thing." It is also the
mechanism behind shared behaviour (see §7).

### reset is per-instance reinitialisation

`reset <expr>` ignores the prototype's value entirely and evaluates `<expr>`
afresh **every time** an instance is derived. This is the natural home for ids,
timestamps, counters, and empty per-instance collections:

```basic
session = {
    id      (reset new_id()): 0,
    started (reset now()):     0,
    log     (reset []):        []
}
```

`<expr>` is a full expression, not a literal — that is the whole point.

**Evaluation scope (decided).** A `reset` expression is evaluated at `new`-time
against the **stable global/program scope**. It may call builtins and
top-level/loaded functions (`now()`, `new_id()`, `next_id()`) and read globals.
It may **not** see local variables of wherever the prototype literal appeared,
nor the instance's sibling fields, nor the `with` overrides. The reason is
concrete: gBASIC is a manual-memory tree-walker with no closures, so capturing
the literal's defining scope would mean storing a possibly-dangling environment
pointer. The global/program scope outlives every instance and sidesteps that
hazard while still serving every realistic reset (constants, generators,
timestamps). The cost is that a sibling-referencing reset such as
`total (reset price * qty)` does **not** work in v1 — compute that after
derivation, or pass it via `with`. Sibling/closure-capturing resets are a future
lift that depends on first-class closures (see §10).

### exclude drops the property

`exclude` means the property exists on the prototype but is not carried into
derived instances. Use it for scratch/working fields that belong to the prototype
itself, or for marker fields that should not propagate.

---

## 3. Syntax

### 3.1 Policy annotations in record literals

A record field may carry a parenthesised policy between the field name and the
field separator:

```basic
account = {
    owner   (copy):        "unnamed",
    bank    (link):        "First Bank",
    balance (reset 0):     0,
    scratch (exclude):     "temp"
}
```

Grammatically this extends the existing record-field rule. A plain field is
`IDENT ( ':' | '=' ) expression`; PBI adds an optional policy clause **on the
`:` form only** (implemented):

```
record_field
    : IDENT ':' expression
    | IDENT '=' expression
    | IDENT '(' field_policy ')' ':' expression   /* policy-annotated, ':' only */
    ;
field_policy                /* keywords are contextual IDENTs, not tokens */
    : IDENT                 /* copy | link | exclude */
    | IDENT expression      /* reset <expr> */
    ;
```

**Policy annotations use the `:` separator, not `=` — a deliberate, enforced
constraint.** With `=`, the source `prop (copy)= …` matches the existing
*assignment-modifier* shape (`lvalue (mod)= value`): the lexer's
`modifier_lparen_ahead` heuristic fires whenever a `)` is followed by an
assignment/comparison operator, so `(copy)` would tokenize as `MOD_LPAREN`/
`MOD_CONTENT` instead of ordinary parens. A `:` after `)` does **not** trigger
that heuristic, so on the `:` form `(policy)` lexes as plain parens and the
contextual keywords parse normally. Writing `prop (copy)= 1` therefore produces a
clean syntax error rather than silently mis-parsing. This needs no lexer change.

**`copy`/`link`/`reset`/`exclude` are contextual keywords, not reserved
words.** `copy` is already a file builtin, and reserving these globally would
repeat the §5 builtin-shadowing problem. They are matched as ordinary `IDENT`s
inside the policy parentheses and validated in the grammar action; everywhere
else they remain ordinary identifiers and builtins. (Verified: `copy = 1` and
`{ copy: 10 }` still work, and the `copy(...)` file builtin is unaffected.)

This deliberately does **not** reuse the assignment-modifier lexer mode
(`modifier_content_mode`, `MOD_LPAREN`/`MOD_CONTENT`). That mode captures
parenthesis content as *raw text* and resolves it through the modifier namespace,
which is wrong here for two reasons: `reset <expr>` needs a normally-parsed
sub-expression (not raw text), and the policy set is closed and context-bound.
Inside a record literal, after `IDENT`, a `(` was previously a syntax error, so
the new rule is unambiguous and needs no source-level lookahead heuristic.

Status: **parsing implemented (Phase 1).** Policies are stored in the AST
(`AstRecordField.policy` / `.reset_expr`) and shown by `--ast`; they are inert at
runtime until derivation (Phase 2) consumes them.

**`copy`/`link`/`reset`/`exclude` must be contextual keywords, not reserved
words.** `copy` is already a file builtin (`docs/historical_development_archive.md`),
and reserving these globally would repeat the §5 builtin-shadowing problem from
the language design. They are recognised as policy keywords **only** inside the
policy parentheses of a record field; everywhere else they remain ordinary
identifiers and builtins.

This deliberately does **not** reuse the assignment-modifier lexer mode
(`modifier_content_mode`, `MOD_LPAREN`/`MOD_CONTENT`). That mode captures
parenthesis content as *raw text* and resolves it through the modifier namespace,
which is wrong here for two reasons: `reset <expr>` needs a normally-parsed
sub-expression (not raw text), and the policy set is closed and context-bound.
Inside a record literal, after `IDENT`, a `(` is currently a syntax error, so the
new rule is unambiguous and needs no source-level lookahead heuristic.

### 3.2 Deriving an instance

A new instance is produced with a `new` prefix expression, optionally with a
`with { … }` block that overrides and/or extends:

```basic
a = new account
b = new account with { owner: "Ada" }
c = new account with { owner: "Grace", tier (link): premium_tier }
```

`new proto` *derives* an instance: it applies every field policy of `proto` to
build it. The surface keyword is `new`; the operation it names is the derivation
defined in §4.2 (keyword vs concept, exactly like Java's `new` keyword vs the
concept "instantiation"). There is no class/instance split — `new X` derives from
the record `X` itself. `with { … }` is itself a record literal — it may set values
**and** annotate policies, so a derivation can both fill in instance data and
re-state a property's policy for the instance's own future descendants.

`new` is a **contextual** prefix keyword, consistent with the policy words. It is
free today — not a lexer token, not a builtin, and not used as an identifier in
any current program — so introducing `new <expr>` is unambiguous (in value
position a bare second identifier is currently a syntax error). `with` is likewise
already a contextual keyword (`with lock` on file references), so reusing it adds
no new reserved word. `new` was chosen over `derive` for intuitiveness; its mild
"class instantiation" connotation is harmless here because a prototype simply *is*
a record.

#### What `with { … }` may do (decided)

The `with` block is a record literal applied **on top of** policy-based
derivation. It may:

- **Override** an inherited field's value — `new account with { owner: "Ada" }`.
- **Re-annotate** a field's policy — `new account with { tier (link): premium }`.
  Like any record-literal annotation this is **forward-looking**: it sets the
  policy for *this instance's own future descendants*, not for the current
  derivation step.
- **Add** a field the prototype does not have — gBASIC records are open (a field
  can be set on any record at any time), so `with` adding a new field is
  consistent with the rest of the language. An added field defaults to `copy`.

It may **not** remove an inherited field from the instance; v1 has no
removal-at-derivation mechanism. To keep a field off instances, annotate it
`(exclude)` on the prototype; to drop one after the fact, use `remove_key`. This
preserves `exclude`'s single meaning ("do not pass to descendants") and never
overloads it into "delete from this instance."

Two rules fall out of "derivation never mutates the prototype":

- **A `with` entry binds a fresh cell.** `new account with { bank: "Other" }`
  gives the new instance its *own* `bank`; it does not write through an inherited
  `link` to change the prototype. (The inherited policy still applies to the
  instance's own descendants unless re-annotated.)
- **`with` overrides win over `reset`.** `new session with { id: 5 }` yields
  `id = 5`; the field's `reset` does not fire, because `with` is applied after the
  policy step (§4.2).

### 3.3 Worked example — leaf policies

```basic
account = {
    owner   (copy):    "unnamed",
    bank    (link):    "First Bank",
    balance (reset 0): 0,
    scratch (exclude): "temp"
}

a = new account
b = new account

a.owner = "Grace"        ' copy → forks; a.owner private now
a.bank  = "Second Bank"  ' link → writes through to the shared cell

print b.owner            ' "unnamed"      (a's fork did not touch b)
print b.bank             ' "Second Bank"  (linked: a's write is visible)
print a.balance          ' 0              (reset fired at derivation)
print has(a, "scratch")  ' false          (excluded from instances)
print account.scratch    ' "temp"         (still on the prototype itself)
```

---

## 4. Runtime and memory model

PBI cannot run on the current "every record field is a uniquely-owned `Value*`"
model. `copy` (COW) and `link` (aliasing) both require **shared, reference-counted
storage cells** with a fork-on-write barrier. This is the central implementation
fact: *PBI forces a refcounted value-cell model on records.*

There is already a precedent to generalise. Database connections are
reference-counted today (`value_copy`/`value_free` increment/decrement a
`ref_count` for `VALUE_POSTGRES_CONNECTION`/`VALUE_SQLITE_CONNECTION`,
`src/eval.c:638`). PBI extends that idea from connections to record-field storage.

### 4.1 The cell and the field

Current field (`src/eval.c:107`):

```c
typedef struct {
    char *name;
    Value *value;     /* uniquely owned */
} RecordField;
```

Proposed:

```c
typedef struct {
    Value value;
    size_t refcount;
} ValueCell;

typedef enum { POLICY_COPY, POLICY_LINK } SlotShare;   /* runtime sharing mode */

typedef struct {
    char  *name;
    ValueCell *cell;   /* shared, refcounted */
    SlotShare  share;  /* COPY = fork-on-write, LINK = write-through */
} RecordField;
```

**Implementation note (as built).** The runtime `RecordField` carries the full
declared policy (`AstFieldPolicy policy` + a shared `AstExpr *reset_expr`), not
just a two-state share mode. **Policies persist on derived instances** — this is
the resolution of the former §10.3 question. A derived `reset` field keeps
`policy = RESET` (and its `reset_expr`), so when the instance is itself used as a
prototype the reset re-fires; a derived `link` field keeps `policy = LINK`, so it
stays linked when re-derived. Only `exclude` does not persist (the field is
simply absent, and absence cannot be re-excluded). This persistence is exactly
what makes the recursive §6 semantics work across multiple `new` levels.

`reset_expr` is a pointer into the program AST, which outlives every value, so it
is shared (never copied or freed by the value layer).

### 4.2 Derivation algorithm

`new proto` (derivation):

```
for each field F in proto:
    switch F.policy:
      exclude:        skip
      reset(expr):    child gets a NEW cell, value = eval(expr), share = COPY, refcount 1
      link:           child.cell = F.cell;  F.cell->refcount++;  share = LINK
      copy:           child.cell = F.cell;  F.cell->refcount++;  share = COPY   (COW)

then apply `with { … }` on top:
    for each entry W in with-block:
      W binds a FRESH cell (value = eval(W), refcount 1) — it never writes
      through an inherited link; the field's share/policy is W's annotation if
      given, else the inherited policy. A with entry for a reset field overrides
      it (the reset does not fire). A with entry naming an unknown field adds it
      (default copy).
```

`link` and `copy` both start by *sharing* the prototype's cell and bumping its
refcount; they differ only in what a later write does. A `with` entry always
detaches into a fresh cell, so derivation never mutates `proto` (§3.2).

### 4.3 The write barrier (fork on write)

Every mutation of a record field flows through `record_set` (`src/eval.c:1113`)
and the lvalue path `resolve_lvalue_ref`/`assign_lvalue`
(`src/eval.c:12474`/`12555`). The barrier lives here:

```
to write value V into field F:
    if F.share == LINK:
        *F.cell.value = V                  # write-through, all aliases see it
    else if F.share == COPY and F.cell.refcount > 1:
        new = ValueCell{ value: V, refcount: 1 }   # FORK
        F.cell.refcount--                  # detach from the shared cell
        F.cell = new
    else:                                  # COPY, sole owner
        *F.cell.value = V
```

Because the fork triggers for *whichever* side writes and only when the cell is
still shared (`refcount > 1`), copy semantics are correctly symmetric and lazy.

### 4.4 Reference cycles

`link` can create cycles (instance A links a field to B, B links back to A),
which pure reference counting leaks. v1 should **document** this rather than ship
a cycle collector; a later pass can add cycle detection if real programs hit it.
This is consistent with the rest of the interpreter being deliberately simple.

---

## 5. Backward compatibility

- A record literal with **no** policy annotations derives every field as `copy`.
  `new plain_record` therefore behaves exactly like today's deep copy, and an
  ordinary `x = some_record` assignment is unchanged in meaning.
- The COW model is an internal optimisation; observable copy semantics are
  identical to the current eager deep copy. Existing golden-file tests must
  continue to pass byte-for-byte.
- `new`, and the four policy keywords *in policy position*, are the only new
  surface. No existing identifier meaning changes (the policy keywords stay
  contextual — see §3.1).

---

## 6. Worked example — nested instances (the subtle case)

When a property holds **another instance**, "copy" has two possible meanings, and
the difference is the single most important semantic decision in PBI.

```basic
engine = {
    serial (reset new_serial()): 0,
    rpm    (copy):               0
}

car = {
    make  (copy):          "Forda",
    motor (copy): new engine        ' the property value is itself an instance
}

c1 = new car
c2 = new car
```

- **Recursive derivation (recommended):** deriving `car` re-derives `motor`, so
  `c1.motor` and `c2.motor` are distinct engines with distinct serials, and each
  engine's own `copy`/`reset` policies apply. This matches how anyone modelling
  real objects expects "copy" to behave.
- **Flat deep-copy:** both cars share one engine value (same serial) until a
  write forks it. This is simpler to implement but almost never what the program
  means, and it silently discards the inner `reset`.

**Decided: recursive derivation (Reading A).** `copy` of a property whose value
is an instance performs a **recursive derivation**, not a flat duplication, and
`link` of such a property shares the inner instance's identity. This is the only
reading under which a nested `reset` (or any inner policy) keeps working: under
flat duplication the inner engine's `serial (reset …)` would be frozen at the
moment the `car` literal was written and copied verbatim forever, silently
turning `reset` into a no-op as soon as its object is nested. Recursion is cheap
because `copy` is copy-on-write — re-deriving a nested instance is mostly
refcount bumps until something is actually written.

Two mechanics still to pin down during implementation (not blocking the decision):
the rule for **how deep** recursion goes (every nested instance, all the way
down — there is no fixed bound; it follows the data), and **leaf vs instance
detection** (when is a field value "an instance to re-derive" vs "a plain value to
COW-share"? — i.e. any `VALUE_RECORD` that carries field policies, versus a bare
record/scalar/array). See §10.1.

---

## 7. Behaviour / methods (follow-on)

PBI subsumes "methods" without a separate concept: a method is a
**function-valued property marked `link`**, so every instance shares one function
cell rather than copying code per instance.

```basic
counter = {
    n    (reset 0): 0,
    bump (link):    function(self) self.n = self.n + 1 end function
}
```

This is a **follow-on, not part of PBI v1**, because it has a hard prerequisite:
gBASIC functions are not currently first-class values (there is no
`VALUE_FUNCTION` in the `ValueKind` enum at `src/eval.c:58`; functions live in
their own declaration/registry world). Two things must land first:

1. First-class function values (so a function can be stored in a field cell).
2. A receiver-binding convention — how `instance.bump()` passes the instance as
   `self`. This is an open question (§10.4).

PBI v1 should ship **data** inheritance only. The `link` policy is designed so
that methods slot in later with no change to the policy model: shared behaviour is
just a linked function-valued property.

---

## 8. Staged implementation plan

Each phase is independently testable. Phase 0 is the large, invasive one; the
rest are comparatively surgical.

**Phase 0 — refcounted value cells (foundation). DONE.**
Generalised the connection refcount pattern into `ValueCell` (the `Value` is the
cell's first member, so all existing `field->value` derefs are untouched; only
`cell_alloc`/`cell_release` at the allocation/free sites changed). Observable
behaviour unchanged (every cell refcount-1, eager-equivalent). Verified green
against the full suite and Valgrind-clean on record-heavy examples.

**Phase 1 — policy annotations (parse). DONE (AST only).**
Added `AstFieldPolicy` + `policy`/`reset_expr` to `AstRecordField`
(`include/ast.h`), a `field_policy` grammar rule and the `:`-only policy-clause
production with contextual keywords (`src/parser.y`), AST freeing of
`reset_expr`, and `--ast` rendering (no suffix for `copy`, so existing AST dumps
are unchanged). Positive example + three negative tests added; full suite green;
Valgrind-clean.

Runtime storage was **deliberately deferred to Phase 2** rather than done here:
storing the policy in the runtime `RecordField` only matters once `new` reads it,
and bundling that plumbing with the code that exercises it is safer than shipping
an unread runtime field. Phase 1 keeps the policy in the AST only; the runtime
record is unchanged and policy-annotated records behave exactly like plain
records today.

**Phase 2 — `new`, `with`, and runtime policy storage. DONE.**
- *2a (runtime policy storage):* added `policy`/`reset_expr` to the runtime
  `RecordField`, read from the AST in the record-literal eval, preserved by
  `value_copy`/`remove_key`; module records default to `copy`. Behaviorally
  invisible.
- *2b (`new` + derivation):* `NEW` is a reserved keyword token (every gBASIC
  keyword is a token; `new` is unused as an identifier). Added
  `AST_EXPR_NEW { proto, with }`, the `new <postfix> [with <record-literal>]`
  grammar (reusing an extracted `record_literal` non-terminal), and the §4.2
  derivation in `eval.c`: `exclude` drops, `reset` evaluates in global scope,
  `copy` makes an independent recursive copy (so nested instances re-derive per
  §6 — verified with distinct nested serials), `link` shares the cell.

  **`link` works through gBASIC's value-copy model** because `value_copy` was
  made link-aware: a `link` field shares its cell (refcount++) instead of being
  deep-copied, so the cell keeps one identity across the copies that variable
  reads/assignments perform, and `record_set`'s in-place write is seen by every
  alias. A write through any of prototype / instance / sibling is visible to all.

**Phase 3 DONE — copy-on-write.**
`copy` is now lazy. `value_copy` and `new`-time leaf derivation share the cell
(refcount++) exactly as `link` does; the difference is a fork-on-write barrier,
`cell_fork_for_write`, which forks any non-`link` cell still shared (`refcount >
1`) into a private deep copy before the pending mutation. The barrier sits at the
two — and only two — choke points through which record-field content is mutated
in place:

- `record_set` (the field-assignment sink, incl. index-form `r["f"] = …`):
  detaches a shared cell before overwriting it. `link` falls through and writes
  in place (write-through preserved).
- `resolve_lvalue_ref` (the FIELD and record-INDEX branches): forks before
  handing a mutable pointer to a caller that mutates content in place — this is
  what makes `append(r.items, …)` and `r.inner.n = …` fork correctly.

The fork's deep copy is itself copy-on-write, so a deep structure diverges one
level at a time as each level is first written. **Nested-instance `copy` stays
eager-recursive** at `new` (re-firing inner `reset`s per §6); only its leaves
COW-share — which is the "mostly refcount bumps" the §6 note anticipated.
Observable behaviour is identical to Phase 2; this only removes the eager
duplication, making `new`/assignment O(fields) instead of O(deep size).
(commit pending, 2026-06-23.) Test: `examples/pbi_cow_test` exercises all six
fork paths (scalar, array, nested, index-form, symmetric prototype fork, link
exemption); suite green at 102/162, Valgrind-clean.

**Phase 4 — methods (deferred).**
Only after first-class functions exist (§7). Out of scope for PBI v1.

### Test matrix (golden-file, per the project's test conventions)

- copy: write to instance forks; prototype and siblings unaffected.
- copy: write to prototype forks; existing instances keep old value.
- link: write through instance visible on prototype and siblings.
- reset: distinct value per `new`; expression re-evaluated each time.
- exclude: property absent on instance, present on prototype (`has`).
- `with`: overrides value; re-annotates policy.
- nested instance: per §6 chosen semantics (distinct serials under recursion).
- back-compat: `new` of an un-annotated record equals today's deep copy.
- negative: policy keyword outside policy position is still an ordinary
  identifier/builtin; malformed `( … )` policy clause diagnostics.

---

## 9. Convergence with the other two threads

PBI is not isolated; it shares machinery with the Unicode and multiprocessing
work, and doing it first de-risks both.

- **Memory-safety / value-model.** The refcounted-cell + COW model (§4) is the
  same machinery a general memory-safety upgrade wants, and the same shape a
  future immutable/shared **text** type (the Unicode bytes-vs-text split) would
  use. Phase 0 is therefore a shared foundation, not PBI-only cost.

- **Multiprocessing (actors).** `link` is **intra-isolate only**. Actors do not
  share memory, so a linked cell cannot span isolates. When an instance crosses
  an actor/process boundary it is serialised; `link` fields cannot retain shared
  identity across the boundary and must **degrade to an independent copy
  (snapshot) at send time** (or be a diagnosed error — §10.7). This is the same
  principle as "watcher boundaries = concurrency boundaries": shared, reactive
  state stops where the isolate stops.

- **Watchers.** A `link`-shared cell written through one instance must fire
  watchers registered via *any* alias of that cell. Integrating the write barrier
  (§4.3) with watcher notification (`docs/gbasic-design.md` §9) is required, not
  optional.

---

## 10. Open design questions

1. **Nested-instance copy semantics — DONE: recursive (§6).** Implemented:
   `derive_value` re-derives any `VALUE_RECORD` under a `copy` field (every nested
   record is treated as re-derivable), and leaves are `value_copy`'d. Verified
   with distinct nested serials across `new`.
2. **Derivation surface — DONE.** `new` is a reserved prefix keyword performing
   derivation; `with { … }` overrides values (fresh cell, wins over `reset`),
   carries its own policy, and adds unknown fields but cannot remove inherited
   ones. See §3.2 / §8.
3. **Policy persistence across levels — DONE.** The instance retains each field's
   `policy`/`reset_expr` in its runtime `RecordField` (preserved by `value_copy`),
   so it re-applies on re-derivation; `with` sets the policy of the fields it
   touches. See §4.1.
4. **Methods and `self`.** Receiver-binding convention for function-valued
   `link` properties; depends on first-class functions (§7).
5. **Identity vs structural equality.** Does `link` introduce an identity notion?
   Should there be an `is` (same-cell) comparison distinct from `=` (structural)?
6. **`reset` evaluation scope — DECIDED.** `<expr>` evaluates at `new`-time
   against the stable global/program scope (builtins, top-level/loaded functions,
   globals); it may not see defining-scope locals, sibling fields, or `with`
   overrides. Sibling-referencing resets are a future lift gated on closures. See
   §2 (reset). Still minor/open: whether `: value` may be omitted when
   `(reset expr)` is present.
7. **Cross-actor `link` degradation.** Snapshot-copy at send vs diagnosed error
   (§9).
8. **Introspection.** Can a program read or change a property's policy at runtime
   (e.g. `policy_of(rec, "field")`)? Useful for serialisation and debugging.
9. **Reference cycles.** Document-and-leak in v1 vs cycle detection (§4.4).
10. **Arrays.** This document scopes PBI to record fields. Whether array elements
    can carry policies, or whether arrays are always `copy`, is unspecified.

---

End of PBI design.

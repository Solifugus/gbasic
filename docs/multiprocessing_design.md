# Multiprocessing (Actors) — Design

Status: **proposal / for discussion** (2026-06-24). The **last** of the three
pre-freeze language threads; PBI and Unicode (its two prerequisites) are now
complete. This document consolidates the principles that were settled while
designing those two threads and works out the surface that was deliberately left
open. Nothing here is implemented yet. It is meant to be read and argued with,
not treated as final — §3 and §6 in particular present real choices with a
recommendation, not a decree.

## 1. Why this is last, and what is already decided

Multiprocessing was sequenced after PBI and Unicode on purpose: both of those
threads exist partly to de-risk this one. Three things are already settled (from
`docs/pbi_design.md §9` and `docs/unicode_design.md §9`):

1. **Shared-nothing actors.** Concurrency is isolates that do not share memory.
   Values move between them by being **copied across a boundary**, never by
   sharing a pointer. This is the only model that is safe for a manual-memory
   tree-walking interpreter without rewriting its memory discipline.

2. **"Watcher boundaries = concurrency boundaries."** Shared, reactive state
   stops where the isolate stops. Watchers (`docs/gbasic-design.md §9`) are
   synchronous *within* an isolate and never fire across one. A value handed to
   another actor is a snapshot; mutating it there cannot notify the sender's
   watchers.

3. **PBI `link` degrades at the boundary.** A `link` field (shared cell,
   write-through identity) is **intra-isolate only**. When a record crosses into
   another actor it is serialized, and `link` identity cannot survive — the field
   becomes an independent copy. This falls out automatically from "copy across
   the boundary" (§6) and needs no special case beyond a possible diagnostic.

And one **blocker is now cleared**: because a gBASIC string is *length + bytes*
(Unicode v1), serializing one is a length-prefixed byte copy — binary-safe and
total, no encoding questions. The old `char*`+`strlen` model could not serialize
an interior NUL at all. Serialization (§5) is therefore now tractable.

What was **never** designed, and is the real work of this thread: the isolate
mechanism (§3), the spawn/send/receive surface (§4), the per-type serialization
rules (§5), and the fault/lifecycle model (§7).

## 2. The shape of the feature

A first, deliberately small target — enough to be real, not enough to be Erlang:

```basic
' worker is an ordinary named function; it becomes the actor's body.
function worker(name)
    consider receive()                 ' block for the next message
        if "ping" then
            send(parent, "pong from " + name)
        if "stop" then
            return                     ' actor ends when its body returns
    end consider
end function

program main(args)
    parent = self()                    ' this isolate's handle
    a = spawn worker("a")              ' returns an actor handle
    send(a, "ping")
    print(receive())                   ' "pong from a"
    send(a, "stop")
end program
```

Five primitives carry the whole model: `spawn`, `send`, `receive`, `self`, and
the actor handle value. Everything else (timeouts, selective receive,
supervision) is layered on top and can wait.

Note how `consider` (gBASIC's existing structural match) pairs naturally with
`receive()` — message dispatch is just matching on the received value. That is a
genuine convergence with an existing feature, not a new construct.

## 3. The isolate mechanism — processes vs threads

This is the first real decision and it colors everything.

The interpreter is a manual-memory tree-walker with substantial **file-scope
mutable global state** in `src/eval.c` (~13k lines): the environment/scope
chain, the watcher registry and its drain queue, `current_error`, the module
connection tables, the GTK loop. None of it is thread-safe, and making it so
would mean threading a per-interpreter context through essentially every
function — a very large, very risky refactor.

- **Option A — OS processes (recommended).** Each actor is a child process with
  its own heap and its own copy of the interpreter globals. Messages travel over
  a pipe/socketpair as serialized bytes (§5). True shared-nothing by
  construction; **zero changes to the existing globals**; a crash in one actor
  cannot corrupt another. Costs: spawn is heavier (fork + interpreter setup), and
  every message is a copy through the kernel. For a language whose whole model is
  "copy across the boundary," those costs are inherent, not incidental. The name
  the user has always used — *multiprocessing* — matches this.

- **Option B — OS threads with isolated interpreter state.** Cheaper spawn and
  cheaper messaging, but only after all of `eval.c`'s globals become per-isolate
  state behind a context pointer. High blast radius across a frozen-soon
  codebase; easy to introduce subtle data races. Not worth it for v1.

**Recommendation: processes (Option A).** It is the lowest-risk path to a correct
shared-nothing model and requires no disturbance of the soon-to-freeze runtime.
Threads remain a possible *later* performance optimization once (if) the globals
are ever encapsulated — explicitly out of scope here.

A consequence worth stating plainly: with processes, `spawn` is not cheap, so the
intended granularity is **coarse** — a handful of long-lived workers, a pipeline,
a request fan-out — not millions of tiny actors. That is the right granularity
for BASIC anyway.

## 4. The surface (placeholder names — see §9.1)

- **`spawn <function-call>`** — start a new isolate running the named function
  with the given (serialized) arguments. Returns an **actor handle**. The callee
  must be a declared function (or `program`/`library` entry); gBASIC has no
  first-class functions (`pbi_design.md §7`), so the body is named, not a lambda.
  `spawn` is a prefix keyword like `new`, chosen contextually so it does not
  reserve a common identifier (validate against the lexer/builtin registry the
  way `new` was).

- **`send(handle, value)`** — copy `value` (§5) into the target's mailbox.
  Non-blocking from the sender's side (fire-and-forget). Ordering is **per-sender
  FIFO**: messages from one sender arrive in send order; no global ordering across
  different senders (standard actor semantics).

- **`receive()`** — block until a message is in this isolate's mailbox; remove and
  return it. v1 is a simple blocking dequeue. Selective/pattern receive and a
  timeout form (`receive(within: 5 seconds)`, leaning on the duration type) are
  §9 extensions.

- **`self()`** — the current isolate's handle, so it can be sent to others.

- **Actor handle** — a new leaf value kind (`VALUE_ACTOR`) wrapping a routable
  id, not a raw pointer. Handles are **themselves sendable**, which is what lets
  actors learn about each other and form topologies beyond parent/child.

An actor's life ends when its body function returns (or errors — §7). Messages to
a dead actor are dropped by default (with an optional diagnostic later).

## 5. Serialization — the core of the work

Sending a value means serializing it to bytes, copying those bytes to the target
isolate, and deserializing. This is the one genuinely new piece of runtime
machinery, and it should be built and tested **on its own, before any
concurrency** — round-tripping a value to bytes and back inside a single process
(this is the §8 Phase 0, mirroring how PBI and Unicode each began with an
invisible foundation phase).

Per value kind:

| Kind | Rule |
| --- | --- |
| Number, Boolean, Null, Unknown | trivial fixed encoding |
| String | length-prefixed bytes (already binary-safe — Unicode v1) |
| Date/time, Duration, Money | copy the value struct field-wise |
| Array | length-prefix, then serialize each element recursively |
| Record | field count, then each `name` + serialized value, recursively |
| File / Directory reference | serialize as its path string (these are typed *paths*, not open handles — safe to cross; the receiver may open it itself) |
| Postgres / SQLite connection | **not sendable** — a live handle bound to the originating process; raise a diagnosed runtime error at `send` time |
| GUI widgets/records | **not sendable** — bound to one isolate's GTK loop; diagnosed error |

Record **policies** need no special serialization: since every field's *value* is
copied, a `link` field automatically arrives as an independent copy — its
shared-cell identity simply cannot be represented in the target isolate. This is
exactly the §1.3 degradation, achieved for free. The only question is whether to
*announce* it (§6).

**Cycles.** Records/arrays can in principle form reference cycles (PBI §10.9
leaves cycle handling as document-and-leak in v1). Serialization must not infinite
-loop: either detect back-references and encode them, or (simpler for v1) cap
depth and raise a structured error on cycles. Recommend the cap+error for v1,
consistent with the watcher-drain cap precedent.

## 6. The `link`-across-boundary decision (PBI §10.7)

The one explicitly-open PBI question that this thread must answer: when a record
with a `link` field is sent, should the silent degradation-to-copy be **silent**
or **diagnosed**?

- **Silent snapshot (recommended).** Crossing the boundary copies everything
  anyway; `link` losing identity is a natural consequence of "shared reactive
  state stops at the isolate" (§1.2), which the programmer already accepts by
  using actors. Keeps `send` total and simple. This is the default.

- **Diagnosed.** Refuse, or warn, when sending a record that carries a live
  `link`, on the grounds that silent identity-loss can surprise. More protective
  but makes `send` partial on a property the sender may not control.

**Recommendation: silent snapshot in v1**, with the door open to an opt-in strict
mode later (a `send(... strict)` modifier or a program-level flag) for code that
wants the diagnostic. Document the behavior loudly either way.

## 7. Faults and lifecycle

The minimum honest story for v1, with the richer parts flagged as future:

- **Normal end:** body returns → actor terminates, mailbox discarded.
- **Crash:** an unhandled runtime error in an actor terminates *that* actor only
  (process isolation guarantees it cannot corrupt others). v1: the crash is
  logged to stderr and the actor dies; senders are not notified.
- **Supervision / linking** (Erlang-style "notify my supervisor when I die",
  restart strategies) is **future work**, not v1. It depends on handles being
  sendable (§4) which v1 provides, so it can be added without rework.
- **Orphans:** if `main` exits while children live, v1 should terminate children
  (kill the process group) rather than leak them. Define this explicitly.

## 8. Phased plan (PBI/Unicode discipline)

Each phase merged green before the next; the first is an invisible foundation.

- **Phase 0 — serialization core.** A `serialize(value) → bytes` /
  `deserialize(bytes) → value` pair (§5), total over sendable types, a structured
  error on the non-sendable ones and on cycles. Round-trips in a single process;
  no concurrency yet. Independently useful and independently testable — could even
  be surfaced as builtins. This is the shared foundation, analogous to PBI Phase 0
  and Unicode Phase 0.
- **Phase 1 — spawn/send/receive over processes.** Fork an isolate running a
  named function; one mailbox; blocking `receive`; `self`; the `VALUE_ACTOR`
  handle; pipe transport carrying Phase 0 bytes. Parent/child topology only.
  Tests: a worker that echoes; a fan-out that sums replies.
- **Phase 2 — topologies & ergonomics.** Sendable handles (actor-to-actor),
  `consider`-style selective receive, a duration-typed `receive` timeout, orphan
  cleanup. Tests: a ring; a request/reply with timeout.
- **Phase 3 — fault model.** Defined crash behavior, the §6 `link` diagnostic
  (if adopted), and the decision record for what supervision would look like when
  first-class functions eventually land.

## 9. Open questions

1. **Primitive names.** `spawn`/`send`/`receive`/`self` are placeholders. Confirm
   each against the builtin registry and the contextual-keyword rules (`new` is
   the precedent); `send`/`receive` in particular must not collide with a future
   module verb.
2. **Selective receive.** Does `receive` pull strictly FIFO, or can a `consider`
   pattern skip non-matching messages and leave them queued (Erlang selective
   receive)? The latter is more expressive but needs a scan-and-retain mailbox.
3. **Backpressure.** Unbounded mailboxes can grow without limit. Bounded queues
   with a blocking or erroring `send` when full? v1 likely unbounded + documented.
4. **Naming/registry.** A way to find an actor by name rather than by passing a
   handle (a process-registry builtin)? Useful but not essential for v1.
5. **Threads later.** Is encapsulating `eval.c`'s globals behind a context ever
   worth it for a same-process threaded transport, or do processes stay the model
   permanently?
6. **`receive` and the GUI/event loop.** An actor that also drives a GTK window
   has two event sources (mailbox + GTK). How (or whether) those compose is its
   own question — likely "an actor is either a worker or a UI, not both" in v1.

## 10. Convergence recap

- **Unicode → serialization.** Length+bytes strings make §5 total and binary-safe.
  Done.
- **PBI → `link` semantics.** The shared-cell model already defined what must
  happen at the boundary (§1.3, §6). The refcounted-cell/COW machinery is
  untouched by this thread — actors copy values; they don't share cells across
  isolates.
- **Watchers → the governing principle.** "Watcher boundaries = concurrency
  boundaries" (§1.2) is the one-line mental model for the whole feature.

With this thread designed and built, all three pre-freeze language additions are
complete and gBASIC reaches the feature surface intended for the GNU-project goal.

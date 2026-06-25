# Multiprocessing (Actors) — Design

Status: **accepted — ready for implementation** (revised 2026-06-24). The **last**
of the three pre-freeze language threads; PBI and Unicode (its two prerequisites)
are complete. This revision closes the open architectural questions from the
proposal draft so the phased plan in §8 can be executed without re-litigating the
runtime model.

**Decisions folded into this revision (previously open or under-specified):**

1. The isolate mechanism is **fork + exec of a fresh interpreter** (§3, Option A′),
   not a bare `fork()` of the running process. Bare fork inherits live libpq /
   SQLite / GTK file descriptors and is not safe for those libraries; exec gives
   each actor a clean address space. This is non-negotiable for the
   production-readiness goal.
2. Mailboxes are **bounded**; `send` is non-blocking and raises a structured
   error when the target mailbox is full (§4, §6). The old "unbounded +
   documented" lean is rejected as a memory-exhaustion / DoS hazard for a
   web-serving language.
3. The mailbox **topology is settled** (§4.1): one inbound mailbox per actor,
   atomically framed, which fixes the ordering and fairness guarantees and is the
   substrate selective-receive and backpressure build on.
4. The §2 canonical example is corrected to actually loop until `"stop"`.
5. Orphan cleanup uses a **dedicated process group** the root interpreter owns,
   never the inherited group (§7).
6. The **actor handle** now has a defined wire form (§5) and a defined Phase-1
   capability (§4.1): handles passed in `spawn` args — including the parent's own
   `self()` — are wired by plain fd inheritance across exec, so the §2 example
   (child sends to parent) runs in Phase 1. Runtime handle passing to a third
   actor is the only thing that needs `SCM_RIGHTS`, and that stays Phase 2.
7. There is a **maximum serialized message size** (§4.1): the boundary-preserving
   datagram transport caps one frame, so `send` checks the serialized size up front
   and raises the same structured `actor` error as a full mailbox when a value is
   too large. Chunking / out-of-band hand-off for huge payloads is an explicit
   later refinement, not v1.
8. The **control pipe** is defined (§3): a startup-status channel the child uses to
   report ready-or-failure, so `spawn` blocks until the actor is live and returns a
   clean error instead of a half-born actor.

Nothing here is implemented yet.

## 1. What is already decided (carried in from PBI / Unicode)

Three things were settled while designing the other two threads
(`docs/pbi_design.md §9`, `docs/unicode_design.md §9`) and are treated as fixed:

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
   the boundary" (§5–§6) and needs no special case beyond an optional diagnostic.

And one **blocker is cleared**: because a gBASIC string is *length + bytes*
(Unicode v1), serializing one is a length-prefixed byte copy — binary-safe and
total, with no encoding questions. The old `char*` + `strlen` model could not
serialize an interior NUL at all. Serialization (§5) is therefore tractable.

The real work of this thread is the isolate mechanism (§3), the
spawn/send/receive surface (§4), the per-type serialization rules (§5), and the
fault/lifecycle model (§7).

## 2. The shape of the feature

A first, deliberately small target — enough to be real, not enough to be Erlang:

```basic
' worker is an ordinary named function; it becomes the actor's body.
function worker(name, parent)
    while true                              ' an actor runs until it returns
        consider receive()                  ' block for the next message
            if "ping" then
                send(parent, "pong from " + name)
            if "stop" then
                return                       ' actor ends when its body returns
        end consider
    end while
end function

program main(args)
    me = self()                             ' this isolate's handle
    a = spawn worker("a", me)               ' returns an actor handle
    send(a, "ping")
    print(receive())                        ' "pong from a"
    send(a, "stop")
end program
```

Five primitives carry the whole model: `spawn`, `send`, `receive`, `self`, and
the actor-handle value. Everything else (timeouts, selective receive,
supervision) is layered on top and can wait.

Note the loop: a single `consider receive()` handles exactly one message, so a
long-lived worker wraps it in `while true` and leaves only via `return` (here, on
`"stop"`). Without the loop the actor would handle one message and terminate.

`consider` (gBASIC's existing structural match) pairing with `receive()` is a
genuine convergence with an existing feature, not a new construct: message
dispatch is just matching on the received value.

## 3. The isolate mechanism — fork + exec a fresh interpreter

The interpreter is a manual-memory tree-walker with substantial **file-scope
mutable global state** in `src/eval.c` (~13k lines): the environment/scope chain,
the watcher registry and its drain queue, `current_error`, the module connection
tables, and the GTK loop. None of it is thread-safe.

The model is **OS processes**, and specifically **fork + exec (or `posix_spawn`)
of a fresh `gbasic` process**, not a bare `fork()` of the already-running one.

Why exec and not bare fork:

- A bare `fork()` inherits every open file descriptor at the instant of the call.
  That includes any live **libpq** socket, **SQLite** handle, and the **GTK**
  connection. libpq explicitly documents that forking with open connections is
  unsafe (parent and child then share one socket and its protocol state) and
  recommends exec from the child; SQLite handles and GTK are likewise not
  fork-safe. A bare-fork actor model passes every in-process test and then
  corrupts a connection under real load — exactly the failure class this thread
  exists to avoid.
- exec gives the child a **clean address space**: no inherited connections, no
  GTK loop, no half-built watcher registry, no copied global state to reason
  about. True shared-nothing by construction, and zero changes to the existing
  globals.

Mechanism:

1. `spawn worker(args…)` serializes `args…` (§5) in the parent. Before doing any
   work the parent confirms `worker` is a declared function in the *currently
   loaded program* and fails fast with a clear error if not — because the child
   re-execs that same program file, the parent can validate the entry name
   without paying for a fork+exec round-trip that would otherwise die obscurely
   post-exec.
2. The parent creates the child's inbound mailbox channel (§4.1) and a **control
   pipe**: a separate close-on-exec-cleared pipe the child uses to report startup
   status back to the parent — a one-byte "ready" once it has loaded the program
   and bound the entry, or a structured failure (entry not found post-exec, load
   error) before it begins running. The parent's `spawn` blocks on this pipe until
   ready-or-failure, so `spawn` returns either a live handle or a clean error, never
   a half-born actor. Once running, the child closes its end; the pipe is not used
   for messages (those go over the mailbox channel).
3. **Handle wiring.** For every actor handle present in `args…` — including the
   common case of the parent's own `self()` — the parent arranges the
   corresponding mailbox **write-end fd** to be inherited by the child (dup the
   write end, clear `FD_CLOEXEC` on just that fd). This is plain fd inheritance,
   not `SCM_RIGHTS`; it is the same mechanism that hands the child its own
   mailbox. The serialized handle (§5) records which inherited fd realizes it.
4. The parent `fork`s and the child `exec`s `gbasic` with the running program's
   path and the entry-function name on `argv` (e.g.
   `gbasic --actor worker <program-path>`). The child's own mailbox endpoint and
   any handle write-ends from step 3 are the only fds with `FD_CLOEXEC` cleared;
   every other inherited fd keeps it, so nothing else leaks across the exec.
5. The fresh interpreter loads the program file, locates `worker`, binds the
   inherited handle fds, reads its serialized startup arguments as the **first
   frame** on its mailbox, and runs.

**Reserved startup frame.** The startup-argument frame (step 5) is enqueued into
the child's mailbox *as part of `spawn`, before the handle is returned to the
parent*. Nothing else holds the new child's handle yet, so by construction the
arg frame occupies the first slot and no early `send` can displace it — even if
the parent's very next statement is `send(child, …)`.

Cost: each actor re-parses/re-loads the program. Because the intended granularity
is **coarse** — a handful of long-lived workers, a pipeline, a request fan-out,
not millions of tiny actors — that startup cost amortizes to nothing. This is the
right granularity for BASIC anyway.

Constraint (document this): because the child re-execs the program **from its
file path**, `spawn` requires a program loaded from a file. A program run from
stdin or a future REPL cannot be re-exec'd and must raise a clear error at
`spawn`.

**The root is an actor too.** `main` calls `self()` and `receive()` in the §2
example even though nothing spawned it, so the root interpreter **bootstraps its
own inbound mailbox during startup**. Its `self()` returns a handle to that
mailbox; it simply has no parent handle above it. `main`'s arguments arrive from
the program invocation (`args`), not from a startup arg frame — the reserved-frame
rule above applies only to spawned actors.

Threads with per-isolate interpreter state remain a possible *later* performance
optimization, only if `eval.c`'s globals are ever encapsulated behind a context
pointer. Explicitly out of scope here; not required, and high blast radius
against a soon-to-freeze codebase.

## 4. The surface

- **`spawn <function-call>`** — start a new isolate (§3) running the named
  function with the given (serialized) arguments. Returns an **actor handle**. The
  callee must be a declared `function` (or `program`/`library` entry), and because
  the child re-execs the same program the parent **validates the entry name at
  `spawn` time and fails fast** if it is undefined (§3). gBASIC has no first-class
  functions (`pbi_design.md §7`), so the body is named, not a lambda. `spawn` is a
  contextual prefix keyword like `new`, chosen so it does not reserve a common
  identifier (validate against the lexer/builtin registry the way `new` was — see
  §9.1).

- **`send(handle, value)`** — copy `value` (§5) into the target actor's mailbox as
  one atomic frame. **Non-blocking**: it does not wait for the receiver. If the
  target mailbox is **full** (§4.1), `send` raises a structured runtime error
  (source `actor`) rather than blocking — the caller decides whether to retry,
  drop, or back off. Ordering is **per-sender FIFO**: messages from one sender
  arrive in send order; there is no global ordering across different senders
  (standard actor semantics, and a direct consequence of §4.1).

- **`receive()`** — block until a frame is in this actor's mailbox; remove and
  return the deserialized value. v1 is a strict blocking FIFO dequeue.
  Selective/pattern receive and a duration-typed timeout
  (`receive(within: 5 seconds)`) are §9 / Phase 2 extensions.

- **`self()`** — the current actor's handle, so it can be sent to others.

- **Actor handle** — a new leaf value kind (`VALUE_ACTOR`) wrapping a routable id
  (not a raw pointer). Handles are **themselves sendable** (§4.1), which is what
  lets actors learn about each other and form topologies beyond parent/child.

An actor's life ends when its body returns (or errors — §7). Messages to a dead
actor are dropped by default (an optional diagnostic can come later).

### 4.1 Mailbox topology, ordering, and bounds (settled)

Each actor owns **exactly one inbound mailbox**: a single channel endpoint it
reads, fed by any number of senders. This one-mailbox-per-receiver shape is what
defines the ordering and fairness guarantees, so it is fixed here rather than left
to implementation.

- **Atomic framing.** Every message is one self-contained frame carrying the
  Phase-0 serialized bytes (§5), length-prefixed. Frames are atomic: two
  concurrent senders never byte-interleave into a corrupt frame. This is what
  makes a single shared inbound channel safe for multiple writers.
- **Ordering.** A single sender's frames arrive in send order (per-sender FIFO).
  Across distinct senders, frames interleave in arrival order with no global
  guarantee — exactly the §4 contract.
- **Bounds / backpressure.** The mailbox has a **bounded** capacity (the channel's
  kernel send/receive buffer, optionally tightened). A `send` into a full mailbox
  fails fast with a structured error (above). Unbounded mailboxes are rejected:
  they are a memory-exhaustion DoS for a web-serving language, and they are
  incompatible with a non-blocking `send` over a real OS transport anyway (the
  kernel buffer is finite; pretending otherwise just moves the unboundedness into
  userspace). v1 chooses **bounded + erroring `send`** over bounded + blocking
  `send` specifically so the failure is a *catchable runtime error* rather than a
  silent hang or a two-actors-blocked-on-each-other deadlock.
- **Maximum message size.** A boundary-preserving datagram transport
  (`SOCK_SEQPACKET`/`SOCK_DGRAM`) caps a single frame at the socket's datagram
  limit, so there is a **hard maximum serialized size per `send`** — one `send`
  moves one whole value as one atomic frame, and a value that serializes past the
  limit cannot be split across frames without breaking atomicity. v1 makes that cap
  explicit: `send` computes the serialized size (§5) up front and, if it exceeds
  the frame limit, raises the **same structured `actor` error** as a full mailbox
  rather than truncating or silently failing — so an oversized message and a
  backed-up mailbox surface through one catchable failure path. The limit is a
  property of the `channel` (queried from the socket's `SO_SNDBUF` / max-datagram,
  with a documented floor so programs have a portable lower bound to rely on), not
  a language constant. Raising it for genuinely large payloads — chunked framing
  over the `SOCK_STREAM` fallback, or an out-of-band shared-memory/temp-file
  hand-off referenced by a small in-frame token — is a deliberate later refinement,
  not a v1 obligation; the explicit error keeps the v1 boundary honest in the
  meantime. (The §3 startup-argument frame rides the same channel and is bound by
  the same cap: `spawn` with very large arguments fails the same way, which is why
  large initial state is better sent as a follow-up message the child pulls in
  pieces.)

**Transport (implementation note, abstracted).** The mailbox is an internal
`channel` abstraction so the transport can vary by platform without touching the
language surface or the framing. Primary implementation: an `AF_UNIX`
`SOCK_SEQPACKET` socket pair, which preserves message boundaries natively and
supports fd passing (below). Where `AF_UNIX`/`SOCK_SEQPACKET` is unavailable
(notably some macOS versions), the fallback is `AF_UNIX` `SOCK_DGRAM` (also
boundary-preserving, reliable, and ordered for local sockets) or, failing that, a
`SOCK_STREAM` pair with explicit length-prefix framing dedicated **one pair per
sender** (so no stream interleaving). All three carry identical Phase-0 frames;
the choice is contained entirely within `channel`. Pick the primary at build time
via the existing `HAVE_*` feature-detection; do not branch in the evaluator.

**Sendable handles — two cases.** A handle is the write endpoint of some actor's
mailbox, so giving an actor a handle means giving it that write capability. There
are two distinct ways it happens, and they land in different phases:

- **At spawn (Phase 1) — fd inheritance.** Any handle in a `spawn` call's
  arguments, including the parent's own `self()`, is realized by inheriting that
  mailbox's write-end fd into the child across fork+exec (§3, step 3). This is
  plain fd inheritance — the same mechanism that hands the child its own mailbox —
  and it is what makes the §2 example work: the child receives `parent` as an
  argument and can `send(parent, …)` immediately. So Phase 1 supports *child talks
  to its parent and to any actor whose handle it was given at spawn.*
- **At runtime (Phase 2) — `SCM_RIGHTS`.** Passing a *third* actor's handle to an
  already-running actor (mid-flight, where no fork is occurring) requires
  transferring the write-end fd via `SCM_RIGHTS` over the existing actor-to-actor
  socket. The recipient gains a direct channel to the target with no central
  broker and no bottleneck. This is the only handle case that needs ancillary-data
  fd passing, and it is the one thing deferred to Phase 2.

## 5. Serialization — the core of the work

Sending a value means serializing it to bytes, copying those bytes to the target
isolate, and deserializing. This is the one genuinely new piece of runtime
machinery, and it is built and tested **on its own, before any concurrency** —
round-tripping a value to bytes and back inside a single process (Phase 0,
mirroring how PBI and Unicode each began with an invisible foundation phase).

Per value kind:

| Kind | Rule |
| --- | --- |
| Number, Boolean, Null, Unknown | trivial fixed encoding |
| String | length-prefixed bytes (already binary-safe — Unicode v1) |
| Date/time, Duration, Money | copy the value struct field-wise |
| Array | length-prefix, then serialize each element recursively |
| Record | field count, then each `name` + serialized value, recursively |
| File / Directory reference | serialize as its path string (these are typed *paths*, not open handles — safe to cross; the receiver may open it itself) |
| Actor handle (`VALUE_ACTOR`) | serialize as its routable id; the usable channel travels separately as an fd (see note) |
| Postgres / SQLite connection | **not sendable** — a live handle bound to the originating process; raise a diagnosed runtime error at `send` time |
| GUI widgets/records | **not sendable** — bound to one isolate's GTK loop; diagnosed error |

Record **policies** need no special serialization: since every field's *value* is
copied, a `link` field automatically arrives as an independent copy — its
shared-cell identity cannot be represented in the target isolate. This is exactly
the §1.3 degradation, achieved for free. The only question is whether to
*announce* it (§6).

**Actor handles are the one kind whose serialization is not pure data.** A
handle's *value* is just its routable id, but that id is only usable if the
matching mailbox write-end fd reaches the receiver. The two are decoupled: the id
serializes into the frame like any scalar, while the fd travels by the channel
mechanism appropriate to the case — fd inheritance at spawn (Phase 1) or
`SCM_RIGHTS` at runtime (Phase 2), per §4.1. On deserialization the id is bound to
whichever inherited/received fd realizes it. Within a single process (Phase 0's
in-process round-trip, used for deep-clone) the id resolves directly against the
local actor table and no fd transfer is involved — which is why Phase 0 can be
built and tested before any of the cross-isolate fd machinery exists.

**Cycles.** Records/arrays can in principle form reference cycles (PBI §10.9
leaves cycle handling as document-and-leak in v1). Serialization must not
infinite-loop: v1 **caps serialization depth and raises a structured error** on
exceeding it, consistent with the watcher-drain cap precedent. Back-reference
encoding (true graph serialization) is a later refinement.

## 6. The `link`-across-boundary behavior

When a record with a `link` field is sent, the field silently degrades to an
independent copy (§1.3, §5). v1 makes this **silent**: crossing the boundary
copies everything anyway, and `link` losing identity is the natural consequence of
"shared reactive state stops at the isolate" — a property the programmer already
accepts by using actors. This keeps `send` total and simple.

A strict opt-in (a `send(… strict)` modifier or a program-level flag) that
*diagnoses* sending a live `link` is **Phase 3** work, for code that wants the
warning. Either way the behavior is documented loudly.

## 7. Faults and lifecycle

The minimum honest story for v1, richer parts flagged as future:

- **Normal end:** body returns → actor terminates, mailbox discarded.
- **Crash:** an unhandled runtime error in an actor terminates *that actor only*
  (process isolation guarantees it cannot corrupt others). v1: the crash is logged
  to stderr and the actor dies; senders are not notified.
- **Supervision / linking** (Erlang-style death notification, restart strategies)
  is **future work**. It depends only on handles being sendable (§4.1), which v1
  provides, so it can be added without rework.
- **Orphans:** the **root interpreter places every actor it spawns into a
  dedicated process group** that it owns (via `setpgid` on each child), and on
  exit terminates *that* group. It must **not** signal its own inherited process
  group, which may contain a parent shell pipeline or supervisor it did not spawn.
  Child PIDs are also tracked explicitly as a backstop. Define and test this so a
  dying `main` never leaks live children and never kills unrelated siblings.

## 8. Phased plan (PBI / Unicode discipline)

Each phase merges green before the next; the first is an invisible foundation.

- **Phase 0 — serialization core. DONE (2026-06-25).** `serialize(value)` /
  `deserialize(string)` builtins (`src/eval.c`, near `builtin_encode_value`): a
  self-describing, length-prefixed binary format (magic `gBS` + version) carried in
  a binary-safe string. Total over every sendable kind — number, string (interior
  NUL preserved), boolean, nothing, unknown, array, record (nested), date/time,
  duration, money, file/dir reference — with a structured `actor` error on live DB
  connections and on over-deep structures (`SER_MAX_DEPTH` 256, the cycle guard).
  `deserialize` validates magic/version, bounds every read, rejects trailing bytes,
  and frees partially-built structures on malformed input. Records round-trip as
  plain `copy` (PBI policy dropped — the §6 snapshot degradation, for free).
  Same-binary round-trip, so native fixed-width encodings are written directly.
  In-process only; no concurrency. Already useful as deep-clone/persistence. Tests:
  `examples/serialize_roundtrip_test` + 3 negatives. Suite 106/168/sqlite/webserver/
  site green, Valgrind-clean incl. the partial-free error path.
- **Phase 1 — spawn/send/receive over fork+exec processes.** The `channel`
  abstraction (§4.1) with the primary transport; fork + exec a fresh interpreter
  at a named entry (§3); one bounded mailbox; blocking `receive`; `self`; the
  `VALUE_ACTOR` handle; frames carrying Phase-0 bytes. **Handles passed at spawn
  time are wired by fd inheritance**, so an actor can talk to its parent and to
  any actor whose handle it received as an argument — this is what makes the §2
  example run (no `SCM_RIGHTS` yet). Tests: an echo worker; a fan-out that sums
  replies.
- **Phase 2 — topologies & ergonomics.** Runtime handle passing via `SCM_RIGHTS`
  (giving a *third* actor's handle to an already-running actor), `consider`-style
  selective receive, a duration-typed `receive` timeout, orphan/process-group
  cleanup hardening. Tests: a ring; a request/reply with timeout.
- **Phase 3 — fault model.** Defined crash behavior, the §6 `link` strict
  diagnostic (if adopted), and the decision record for supervision once
  first-class functions eventually land.

**Test determinism.** Phase 1's chosen tests are order-independent by
construction — a sum is commutative, so scheduling nondeterminism cannot flake
them. Preserve that discipline: Phase 2's selective-receive and timeout tests do
**not** get that for free and must be made deterministic deliberately (injected
clocks, fixed interleavings, bounded retries) rather than relying on timing.

## 9. Open questions (non-blocking; do not gate Phase 0–1)

1. **Primitive names.** `spawn`/`send`/`receive`/`self` are placeholders. Confirm
   each against the builtin registry and the contextual-keyword rules (`new` is
   the precedent); `send`/`receive` in particular must not collide with a future
   module verb.
2. **Selective receive (Phase 2).** Does `receive` pull strictly FIFO, or can a
   `consider` pattern skip non-matching messages and leave them queued (Erlang
   selective receive)? The latter is more expressive but needs a scan-and-retain
   mailbox layered over the §4.1 channel.
3. **Naming/registry.** A way to find an actor by name rather than by passing a
   handle (a process-registry builtin)? Useful but not essential for v1.
4. **Threads later.** Is encapsulating `eval.c`'s globals behind a context ever
   worth it for a same-process threaded transport, or do processes stay the model
   permanently?
5. **`receive` and the GUI/event loop.** An actor that also drives a GTK window
   has two event sources (mailbox + GTK loop). v1 rule: **an actor is either a
   worker or a UI, not both.** This is not a dead end — because the mailbox is a
   real fd, a future GUI actor can `select()`/`poll()` over the mailbox fd and the
   GTK fd together and unify the two event sources cleanly. Deferred, not blocked.

## 10. Convergence recap

- **Unicode → serialization.** Length+bytes strings make §5 total and
  binary-safe. Done.
- **PBI → `link` semantics.** The shared-cell model already defined what must
  happen at the boundary (§1.3, §6). The refcounted-cell/COW machinery is
  untouched by this thread — actors copy values; they never share cells across
  isolates.
- **Watchers → the governing principle.** "Watcher boundaries = concurrency
  boundaries" (§1.2) is the one-line mental model for the whole feature.

With this thread built, all three pre-freeze language additions are complete and
gBASIC reaches the feature surface intended for the GNU-project goal.

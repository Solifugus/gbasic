# Multiprocessing (Actors) — Design

Status: **Phases 0-2 implemented (2026-06-25); Phase 3 (fault model /
supervision) designed below (§7.1, §8), not yet implemented** (design revised
2026-06-24, Phase 3 added 2026-06-25). The **last**
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

Phases 0-2 are implemented and tested (§8): the serialization core;
`spawn`/`send`/`receive`/`self` over fork+exec processes with spawn-time fd
inheritance; runtime handle passing via `SCM_RIGHTS`; selective receive; a
duration-typed receive timeout; and `PR_SET_PDEATHSIG` orphan-cleanup hardening.
Phase 3 (the fault model / supervision) is **designed** in §7.1 and §8 but not yet
implemented; it is the only remaining thread item, and the design below shows it
needs exactly one new primitive (`monitor`) — supervisors fall out as ordinary
gBASIC programs on top of it.

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
  is **Phase 3**, designed in §7.1. It depends only on handles being sendable
  (§4.1), which v1 provides, so it adds one primitive (`monitor`) and no rework.
- **Orphans (implemented):** two mechanisms, together robust to a tree of any
  depth and to abnormal death.
  - *Parent-death signal (primary).* Each spawned actor arms
    `PR_SET_PDEATHSIG(SIGTERM)` at startup, so the kernel terminates it when its
    parent dies for **any** reason — normal exit, crash, or `kill -9`. This
    cascades down a multi-level tree without the parent needing to run any code,
    which is what a process-group sweep alone cannot guarantee (a signal-killed
    root never runs its cleanup, and a grandchild lives in a different group). A
    `getppid()` re-check right after arming closes the fork→exec→arm race.
  - *Process group + reaping (backstop).* The spawning interpreter still places
    its direct children in a **dedicated** group it owns (never its inherited
    group, which may hold a parent shell pipeline) and, on normal exit,
    `SIGTERM`s that group and `waitpid`s the children so they are reaped rather
    than left as zombies.

### 7.1 Phase 3 — death notification and supervision (design)

`PR_SET_PDEATHSIG` (§7) gives the **downward** edge of the fault graph: a parent's
death cascades to its children. Phase 3 adds the one missing edge, the **upward**
one — an actor learning that *another* actor it cares about has died, **with a
reason**, so it can react (restart it, escalate, release resources, log). That
single capability — death notification — is everything supervision needs.
Supervision itself is then an ordinary gBASIC **program**, not a runtime feature.

**Why notification, not a `supervisor` construct.** gBASIC has no first-class
functions (`pbi_design.md §7`), so a supervisor cannot be parameterized by a
"restart function" passed as a lambda. But a supervisor that *spawns* its children
already holds everything restart needs: the worker's entry-function **name** and
its **argument values** — both are plain data it constructed to call `spawn` in the
first place. So a supervisor is just an actor that loops on `receive()` and
re-`spawn`s on a death message. Nothing else is required of the language. This is
the same minimalism as §2 ("five primitives carry the whole model"): Phase 3 adds
the death message and the rest is library code.

**The primitive: `monitor`.**

- **`monitor(handle)`** — the calling actor asks to be told when the actor behind
  `handle` dies. **Unidirectional**: the monitor learns; the monitored actor is
  unaffected and unaware. Returns a small **monitor reference** value so multiple
  monitors are distinguishable and a later `demonitor(ref)` can cancel one. The
  caller must already hold `handle` — which it does, because a handle *is* the
  capability (§4.1).
- **`demonitor(ref)`** — cancel a monitor so no `down` message will be delivered
  for it (best-effort: a `down` already enqueued still arrives).
- **Naming.** `link` is taken (PBI per-field policy, §1.3 / `parser.y`); `watch`
  is taken (reactive watchers, §1.2 / `TOKEN_WATCH`). `monitor`/`demonitor` collide
  with neither and are the exact Erlang term for the unidirectional form. Validate
  against the builtin registry and the contextual-keyword rules like the other
  primitives (§9.1).

**Delivery: `down` is an ordinary message.** When the monitored actor dies, the
monitor receives a normal mailbox frame — the tagged tuple
`["down", handle, reason]` (tag first, so it composes with selective receive,
`receive("down")`, and with `consider`). No new receive surface: death
notification rides the §4.1 mailbox like every other message, which is precisely
why handles and tagged tuples were built the way they were. `reason` is a small
string:

| reason | meaning |
| --- | --- |
| `"normal"` | the body returned / stopped cleanly |
| `"error"` | unhandled runtime error (the error text may ride as a 4th tuple element) |
| `"killed"` | died to a signal, including the `PDEATHSIG` cascade |
| `"noproc"` | already dead when `monitor` was called (delivered immediately) |

**Mechanism — two detection paths, one delivery point.** A handle is the
write-end fd of the target's mailbox, and every monitor already holds it. Death
detection therefore comes for free, two ways, by OS parentage:

1. **Parent monitoring its own child — the supervision case.** The spawning
   interpreter is the child's OS parent, so `SIGCHLD` / `waitpid` reports the death
   *and its exit status*, yielding an **accurate** reason (exit 0 → `normal`,
   nonzero → `error`, signalled → `killed`). The spawner already tracks child pids
   (Phase 1c's `actor_child_pids`); Phase 3 extends that table to map
   pid → monitored handle id + captured exit status.
2. **Non-parent monitoring** (A monitors B; neither spawned the other). A cannot
   `waitpid(B)`, but it holds B's write-end fd, and when B's process dies B's
   mailbox read-end closes, so A's held fd reports `POLLHUP`/`POLLERR`. A's receive
   loop already `poll()`s its inbox fd (Phase 2c's timeout path); Phase 3 adds each
   monitored fd to that poll set. A hangup means "B is gone" — **liveness, but not
   the exit code** (only B's OS parent has that), so the reason defaults to the
   coarse `"down"`.

Both paths converge at one point: synthesize `["down", handle, reason]` and enqueue
it into the local **retained buffer** (Phase 2b), so it is delivered through
ordinary `receive()` in arrival order, de-duped (one `down` per monitor ref), after
which the fd is dropped from the poll set. **Implementation note:** a *blocking*
`receive()` must now `poll()` over `{inbox} ∪ {monitored fds}` (waking on either a
message or a death), not the inbox alone — the no-timeout path gains the same poll
the timeout path already uses.

**Honest limitation (document loudly).** A monitor that is **not** the dead actor's
OS parent gets liveness but a *coarse* reason (`"down"`, never `error`/`killed`).
Because supervisors **spawn** their workers, a supervisor *is* the OS parent and
gets accurate reasons — the 95% case is covered. Full cross-tree reason fidelity
would need a death-reason broadcast (a dying actor telling its monitors *why*),
which requires the runtime to know an actor's monitor set; that is a later
refinement, not v1.

**Restart = re-spawn, written in the language.** With `monitor` + `down`, a
supervisor is an ordinary actor:

```basic
' ---------------------------------------------------------------------
'  The worker. An ordinary named function -- gBASIC has no first-class
'  functions, so an actor body is always a named function, never a
'  lambda. `boss` is the supervisor's handle, handed in at spawn time
'  and wired across fork+exec by plain fd inheritance (Phase 1).
' ---------------------------------------------------------------------
function worker(id, boss)
    while true                          ' an actor runs until its body returns
        consider receive()              ' block for the next message
            if "ping" then              ' branches align to `consider`
                send(boss, "pong from " + id)
            if "boom" then
                ' An unhandled runtime error ends THIS actor only -- process
                ' isolation guarantees it cannot corrupt the supervisor or any
                ' sibling. The interpreter exits non-zero; that non-zero status
                ' is what becomes reason "error" upstream.
                error("worker " + id + " exploded")
            if "stop" then
                return                  ' clean exit -- becomes reason "normal"
        end consider
    end while
end function

' ---------------------------------------------------------------------
'  The supervisor. NOT a language construct -- just an actor that loops.
'  Because it SPAWNS the worker, it holds everything restart needs: the
'  entry name `worker` and the argument values. Re-running spawn IS the
'  restart. And because it is the worker's OS parent, its death reasons
'  are the accurate ones (waitpid path), not the coarse fallback.
' ---------------------------------------------------------------------
function supervisor(args)
    me = self()                         ' this actor's own handle

    ' Start the worker and begin watching it. monitor() registers the
    ' worker's mailbox write-fd (which the handle already wraps) into our
    ' local "monitored set". From now on the worker's death arrives in OUR
    ' mailbox as a normal message. monitor is UNIDIRECTIONAL: the worker is
    ' never told it is being watched.
    w   = spawn worker("w1", me)
    ref = monitor(w)                    ' returns a monitor reference token
    restarts = 0

    while true
        ' receive("down") is a SELECTIVE receive (Phase 2): it pulls the next
        ' message tagged "down" and leaves any other messages queued -- so a
        ' flood of "pong" replies cannot hide a death from us. The "down" frame
        ' was synthesized by the runtime, not sent by anyone; it looks like any
        ' other message, which is the whole point: no new receive surface.
        '
        '   Under the hood this blocking call is now a poll() over
        '   {our inbox fd} U {the worker's monitored fd}. It wakes on EITHER a
        '   real message OR the worker's mailbox closing (POLLHUP) when its
        '   process dies. Since we are the worker's parent, the runtime also
        '   reaped it via waitpid and knows the exit status, so `reason` below
        '   is accurate rather than the coarse "down".
        msg    = receive("down")        ' msg is ["down", handle, reason]
        reason = msg[2]                 ' arrays are 0-indexed: [0] is "down"

        if reason = "normal" then
            print("worker retired cleanly; supervisor done")
            return
        else
            ' It crashed. Restart up to a bounded number of times -- this bound
            ' is the "max restart intensity" of real supervisors, expressed as
            ' plain gBASIC, not a runtime knob.
            if restarts < 3 then
                restarts = restarts + 1
                print("worker died (" + reason + ") -- restart " + restarts)
                ' Restart == re-spawn the SAME entry with the SAME args. We get
                ' a brand-new actor (new process, new mailbox), so we must
                ' monitor the new one -- the old monitor died with the old
                ' worker. demonitor(ref) is hygiene, not strictly required: a
                ' dead actor fires "down" exactly once.
                demonitor(ref)
                w   = spawn worker("w1", me)
                ref = monitor(w)
            else
                print("too many crashes -- giving up")
                return
            end if
        end if
    end while
end function
```

Restart **strategies** (one-for-one, all-for-one, rest-for-one),
**max-restart-intensity** (give up after N restarts in T), and **escalation** (a
supervisor that is itself monitored by a higher one) are all ordinary control flow
over this one primitive. Recommendation: ship **one canonical supervisor pattern**
in `stdlib` as a documented example and leave richer strategies to the program,
rather than baking strategy enums into the runtime.

**Links (bidirectional, propagating) — deferred, deliberately.** Erlang also has
`link`: bidirectional, where an *abnormal* death propagates an exit signal that
kills the linked peer unless it "traps exits." Phase 3 v1 **recommends shipping
`monitor` only** and deferring propagating links, because:

- The **downward** half of link propagation already exists (`PR_SET_PDEATHSIG`:
  parent death kills the tree, §7).
- The **upward** half (child death notifying parent) is exactly what `monitor`
  provides — without the foot-gun of auto-killing the monitor.
- **"Trap exits"** — the flag that turns a propagated kill back into a message — is
  the genuinely subtle part of the Erlang model and adds a mode bit to every actor;
  it earns its complexity only once real programs demand auto-propagation. Monitors
  cover supervision (the actual goal) without it.
- The name is contended anyway (`link` = PBI), so propagating links would need a
  fresh verb (`couple`? `bind`? — TBD) — another reason not to rush it.

If added later, links need **no new transport** — a link is two monitors plus a
propagation policy — so deferring costs nothing.

**The §6 `link`-strict diagnostic (independent).** Separately, §6 left
"diagnose sending a live PBI `link` field across a boundary" as Phase 3.
Recommendation: keep silent degradation the default (§6) and add an **opt-in**
`send(target, value, strict)` (or a program-level flag) that raises a diagnosed
error when the value contains a live `link` field, for code that wants the boundary
made loud. It is small, isolated, and untangled from the death-notification work —
it can land within Phase 3 or slip to post-freeze without affecting anything else.

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
- **Phase 1 — spawn/send/receive over fork+exec processes. DONE (2026-06-25).**
  Built in three green sub-steps:
  - **1a (`01241df`)** — the `channel` mailbox transport (`src/actor.c` /
    `include/actor.h`): an `AF_UNIX` `SOCK_SEQPACKET` socket pair with a
    non-blocking, bounded write end (`ACTOR_CHANNEL_FULL`), a per-frame maximum
    (`ACTOR_CHANNEL_TOOBIG`, from `SO_SNDBUF` with a 64 KiB floor), and clean EOF.
  - **1b (`4afe0ad`)** — `self`/`send`/`receive` over a real mailbox in-process:
    the `VALUE_ACTOR` handle backed by a refcounted `ActorHandle {write_fd,id}`,
    `send` serializing (Phase 0) and delivering one frame, `receive` blocking and
    deserializing. A message that embeds a handle is rejected (handles are a live
    capability, not data).
  - **1c** — `spawn worker(args)`: a `spawn` prefix keyword (lexer/parser/AST),
    a `gbasic --actor ENTRY PROGRAM --actor-inbox/-self/-control FD` child mode,
    and the fork+exec machinery in `eval_spawn`. The parent validates the entry,
    serializes the arguments, enqueues them as the child's **reserved first
    frame**, fork+execs a fresh interpreter, and blocks on a **control pipe**
    until the child reports ready (so `spawn` never returns a half-born actor).
    **Handles in the spawn arguments are wired by fd inheritance** — the parent's
    own `self()` and any other actor's handle are realized by inheriting their
    mailbox write fds across exec (a `SER_ACTOR` frame tag carries the fd; only a
    spawn frame may contain one). Between fork and exec the child joins a
    dedicated process group and closes every fd except its mailbox/control/handle
    fds, so no libpq/SQLite/GTK descriptor leaks into the fresh interpreter (§3).
    The root reaps and signals that group on exit (§7). Tests:
    `spawn_echo_test` (request/reply), `spawn_fanout_test` (commutative sum over
    three children), `spawn_loop_test` (the §2 consider/receive worker), plus
    negatives for an unknown entry and an arity mismatch. A spawned actor holds a
    write end to its own inbox so `self()` resolves, and therefore ends by
    returning (or on a "stop" message), not by mailbox EOF.
- **Phase 2 — topologies & ergonomics.** *In progress.*
  - **Runtime handle passing via `SCM_RIGHTS` — DONE (2026-06-25).** A message
    sent to a running actor may itself contain actor handles, giving the receiver
    a channel to a *third* actor with no fork involved. `send` serializes each
    embedded handle as a `SER_ACTOR` tag carrying its *index*, and ships the
    handles' mailbox write fds as `SCM_RIGHTS` ancillary data on the same frame
    (`channel_send_fds`); `receive` collects those descriptors
    (`channel_recv_fds`, `MSG_CMSG_CLOEXEC`) and binds index → a freshly adopted
    fd, closing any the frame did not claim. Capped at `ACTOR_MAX_MESSAGE_FDS`
    (32) handles per message. The spawn path keeps its own fd-inheritance encoding
    of `SER_ACTOR`; `serialize()` still rejects handles outright. Test:
    `spawn_handle_passing_test` (a handle forwarded twice through a single-sender
    pipeline, so it is deterministic).
  - **Selective receive — DONE (2026-06-25).** `receive(tag)` returns the next
    message whose **tag** matches `tag` — the message itself if it is a string, or
    its first element if it is a non-empty array (the tagged-tuple convention) —
    leaving every non-matching message queued. Implemented as a userspace
    scan-and-retain buffer layered over the §4.1 channel: messages pulled while
    waiting for a match are held in arrival order, scanned first on the next
    `receive(tag)`, and drained oldest-first by a plain `receive()`, so strict
    FIFO is preserved across the two forms. Equality is the same comparison
    `consider` uses. Test: `spawn_selective_receive_test` (single sender, so
    arrival order is fixed and the out-of-order selection is deterministic).
  - **Receive timeout — DONE (2026-06-25).** A `receive` argument that is a
    **duration** is a deadline: `receive(5 seconds)` returns `nothing` if no
    message arrives in time, and `receive(tag, 5 seconds)` is a selective receive
    with the same deadline. (gBASIC calls are positional, so the timeout is a
    duration-typed argument rather than the design sketch's `within:` keyword; a
    duration therefore cannot also serve as a selector tag.) Implemented by
    `poll`-ing the mailbox fd with the remaining time before each blocking read,
    tracked against a `CLOCK_MONOTONIC` deadline across retained non-matches. A
    queued message returns immediately regardless of the deadline. Tests:
    `spawn_receive_timeout_test` (queued → immediate, empty → times out: both
    outcomes are deterministic, independent of timing margins).
  - **Orphan-cleanup hardening — DONE (2026-06-25).** Phase 1c reaped direct
    children via a dedicated process group on normal exit, but that missed two
    cases: a *grandchild* (a spawned actor's own child) sits in a different group,
    and a root killed by an uncatchable signal never runs its cleanup pass at all.
    Both are now closed with `PR_SET_PDEATHSIG`: each spawned actor asks the kernel
    to send it `SIGTERM` when its parent dies, for *any* reason. This cascades down
    a multi-level tree (root dies → children get SIGTERM → grandchildren get
    SIGTERM …) and does not depend on the parent getting to run code, so a
    `kill -9` of the root tears the whole tree down. The fork→exec→arm window is
    covered by re-checking `getppid()` right after arming. The process group +
    `waitpid` remain as the direct-child reaping backstop. Verified manually
    (normal exit and `SIGKILL` both leave no survivors across a three-level tree;
    process-lifecycle behavior does not fit a stdout golden test, but every
    `spawn_*` example exercises normal-exit cleanup on each run).
  - **Phase 2 is complete.**
- **Phase 3 — fault model / supervision (designed, §7.1; 3a built).** One new
  primitive; supervisors are then programs, not runtime features. Sub-steps, each
  green before the next per the discipline above:
  - **3a — death notification — DONE (2026-06-26).** `monitor(handle)` /
    `demonitor(ref)` builtins; the `["down", handle, reason]` tagged-tuple message
    delivered through the existing mailbox + retained buffer (§7.1). Two detection
    paths into one delivery point: the spawner's child table (`actor_children`)
    gained the per-child handle id + captured `waitpid` status (accurate reason for
    a parent monitoring its child), and blocking `receive()` became a `poll()` over
    `{inbox} ∪ {monitored fds}` (`actor_wait`) so a monitored actor's
    mailbox-`POLLHUP` synthesizes a coarse-reason `down` for a non-parent monitor.
    One `down` per monitor ref (the monitor is retired on fire, dropping its fd).
    A `monitor` ref is a plain number; a `down` for a target already dead at
    `monitor` time is delivered immediately (reason `noproc`, or accurate if it was
    our reaped child). The POLLHUP-before-reapable race is closed by a bounded
    blocking `waitpid` on a child once its mailbox has hung up. Test:
    `spawn_monitor_test` (clean exit → `normal`, crash → `error`, `demonitor` then
    timed `receive("down", 1 seconds)` → `nothing`), Valgrind-clean across the
    parent and its children.
  - **3b — the supervisor pattern + a deterministic test.** A canonical
    crash-and-restart supervisor in `stdlib` (the §7.1 example), plus a golden test
    made deterministic by construction: a worker that errors on a specific message,
    a supervisor that restarts a *fixed* number of times then gives up, printing a
    fixed transcript — no timing margins (mirrors the Phase 2 determinism rule).
  - **3c — the §6 `link`-strict diagnostic (optional, independent).** Opt-in
    `send(…, strict)` / program flag that diagnoses a live PBI `link` field crossing
    the boundary. Lands within Phase 3 or slips to post-freeze; touches nothing else.
  - **Deferred to future (documented, not built):** propagating bidirectional links
    and trap-exit. They are two monitors plus a propagation policy and a fresh verb
    (`link` is taken by PBI), added without rework if real programs ever demand
    auto-propagation (§7.1).

**Test determinism.** Phase 1's chosen tests are order-independent by
construction — a sum is commutative, so scheduling nondeterminism cannot flake
them. Preserve that discipline: Phase 2's selective-receive and timeout tests do
**not** get that for free and must be made deterministic deliberately (injected
clocks, fixed interleavings, bounded retries) rather than relying on timing.

## 9. Open questions (non-blocking; do not gate Phase 0–1)

1. **Primitive names.** `spawn`/`send`/`receive`/`self` are placeholders. Confirm
   each against the builtin registry and the contextual-keyword rules (`new` is
   the precedent); `send`/`receive` in particular must not collide with a future
   module verb. Phase 3 adds `monitor`/`demonitor` — checked free of the `link`
   (PBI policy) and `watch` (reactive watcher) collisions (§7.1); confirm likewise.
2. **Selective receive (Phase 2). RESOLVED — implemented (§8).** Both forms
   coexist: `receive()` is strict FIFO, `receive(tag)` selectively pulls the next
   message whose tag matches and leaves the rest queued (Erlang-style), via a
   scan-and-retain buffer over the §4.1 channel. The selector is a tag (string
   self / array first element), not a full `consider` pattern — a deliberate v1
   simplification; richer structural patterns can layer on later.
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

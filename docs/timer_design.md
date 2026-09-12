# `timer` — periodic work on the event loop

**Status:** Design (this document) → shipped in the same change.
**Raised by:** gdash, GDASH-11 (`gdash11_platform_ask_stream.md`), and the
answer to it (`gdash11_platform_answer_stream.md`).

------------------------------------------------------------------------

## 1. The gap

gBASIC has no way to run periodic work on the event loop **at all**. Every
source the loop polls is request-, reply- or transfer-driven:

| source | fires when |
|---|---|
| `server.requests` | a client sent a request |
| `inbox.messages`  | an actor sent a reply |
| `http.events`     | a transfer made progress |

Nothing fires because time passed. A program that wants to do something every
N seconds has exactly one option today — `sleep` in a loop — and on the event
loop that is fatal: the handler never returns, so its worker never comes back.
That is the whole of what gdash measured and misattributed to streams.

The shape this unblocks is the one the ask was really about: a **parked SSE
stream poked on a schedule**. Parking already works (`docs/reference.md`, the
`stream` section); what was missing is anything to do the poking.

It is broader than streams. A cache that expires, a queue depth sampled for a
dashboard, a health check, a file whose mtime must be noticed — none of them
had a home.

## 2. Shape

A native module needing no `load` (the `process` precedent — there is no
optional dependency here, only clock arithmetic), delivering through a watched
global (the `http.events` / `inbox.messages` precedent).

```basic
t = timer.every(0.5)

watch(timer.ticks)
    while count(timer.ticks) > 0
        ev = take_first(timer.ticks)
        poke_streams()
    end while
end watch
```

- `timer.every(seconds)` → a repeating timer. Returns `{ id, interval, repeating: true }`.
- `timer.after(seconds)` → a one-shot. Returns `{ id, interval, repeating: false }`.
- `timer.cancel(t)` → `true` if it was live, `false` if it had already gone.
- `timer.ticks` → the delivery queue, watched like any other source.

A tick is `{ id, kind: "tick", count, skipped, timer }`.

## 3. The four decisions that decide whether this is safe

### 3.1 Coalescing, not catch-up — and `skipped` reports it

A handler slower than the interval is not an edge case, it is the normal
failure. Two schools:

- **fixed rate** — schedule from the last *due* time, so a slow stretch is
  followed by a burst that catches up;
- **fixed delay** — schedule from *now*, so ticks are at least `interval`
  apart and a slow stretch simply loses ticks.

This takes fixed delay, and not as a preference. A burst on the event loop is
an unbounded queue, which is the defect PLAT-HTTP already shipped once and
whose symptom was **a hang, not a failure** (`run_http`'s `IGNORED` tier, and
the standing lesson that a hang is not a failure). At most one tick per timer
per loop iteration, and the next due time is computed from the delivery.

But silently losing ticks is its own wrong answer: a program counting ticks to
measure elapsed time would be quietly short. So every tick carries **`skipped`**
— the number of scheduled intervals that were *not* delivered since the
previous one, `0` while the loop keeps up. Intervals elapsed is
`1 + skipped` per tick. Reported rather than hidden; the same rule R15 follows
in `insight`, where an incapable search has to say so.

### 3.2 The monotonic clock

`CLOCK_MONOTONIC`, never the wall clock. An NTP step backwards on a wall-clock
timer stalls it; a step forwards fires every interval it crossed. Both are
silent, and both happen on real machines. **This cannot be asserted
behaviourally** — a test cannot move the system clock — so the tier reads the
source, the same device `run_agent` uses to assert purity.

### 3.3 What keeps the loop alive, and what lets it end

The loop runs while a watcher on `timer.ticks` exists **and at least one timer
is live**. Two consequences, both deliberate:

- a program whose only timer is `timer.after(...)` **exits by itself** once it
  fires, because a one-shot removes itself;
- a program with a repeating timer runs forever until `timer.cancel` (or
  `unwatch`). That is the same bargain `watch(inbox.messages)` makes, and for
  the same reason: a mailbox and a clock have no completion of their own, so
  the program says when it is done.

### 3.4 A timer nobody watches

`timer.every(0.5)` with no `watch(timer.ticks)` anywhere delivers nothing, and
would do so in silence — the trap class this tree hunts. Warned (2108) when
the program would otherwise exit with live timers and no watcher. A warning and
not a refusal, because the `watch` may legitimately be registered later; PLAT-WARN
is what makes it affordable to say so at all.

## 4. What it does not do

- **It does not preempt.** A tick is delivered between loop iterations, so a
  handler that blocks delays every timer. That is not a limitation to be fixed
  — it is the single-threaded model, and `skipped` is how a program sees it.
- **It does not cross workers.** `workers: N` is N processes and each runs the
  program, so each registers its own timers. A timer poking parked streams
  reaches the streams *in its own worker* (`docs/reference.md`, the parked
  stream section).
- **It is not a scheduler.** No cron expressions, no calendar. `dates` and
  `schedule` own calendar arithmetic; a program that wants "every weekday at
  09:00" uses a short timer and asks them.
- **It has no sub-millisecond accuracy.** The loop's own tick governs; the
  poll timeout is shortened to the next due timer, so the floor is one poll
  round trip.

### The clock asymmetry is deliberate

A program may **ask** for a 0.5-second interval and may not **timestamp** one:
`timer.every(0.5)` and `sleep(0.25)` take fractions, while `now()` and
`epoch(now())` are second-resolution, so a program cannot measure how long its
own handler took.

That reads like an inconsistency and is a decision. Sub-second intervals are
about **responsiveness** — how promptly the machine comes back, which is the
machine's own business. A timestamp is about **data**, and in business data
processing the second is the smallest unit that means anything; a datetime
carrying milliseconds invites a programmer to reason about a precision the
business does not have. gBASIC's programmer experience is meant to stay
business-oriented rather than technically distracted, so the type stops where
the business does.

The question this leaves — *is my handler keeping up?* — is answered without a
clock. A tick's `skipped` says exactly how many intervals were lost, which is
more directly useful than an elapsed time the program would have to compare
against the interval itself. Where a real measurement is wanted, it belongs
**outside**: `tests/run_timer.sh` times this module against the wall clock from
the shell, which is the standard `run_core.sh` already holds `sleep` to and a
stronger oracle than anything the program could say about itself.

## 5. Refusals

| | |
|---|---|
| `timer.every(0)` / a negative interval | refused — a zero interval is a busy loop, and the message says so |
| a non-number interval | refused |
| `timer.cancel` of a non-record | refused |
| `timer.cancel` of an unknown or already-cancelled id | **`false`, not a raise** — a timer going away twice is an ordinary outcome |

The last row is the one worth stating: the refusals above it are mistakes in
the program, and that one is not.

## 6. Testing

`tests/run_timer.sh`. Self-checking rather than golden, and forced: every
defect here is a **plausible number of ticks**. A timer that fires twice as
often, or half as often, or that silently caught up after a stall, all produce
output that reads exactly like a working timer, and a golden would record
whichever count came out and defend it.

The tiers, and what each catches that no other does:

- **CADENCE** — a 0.1s timer over ~1s delivers a count inside a band, asserted
  as a band because it is a clock and not a value.
- **COALESCE** — the load-bearing tier, and a **difference**: a handler slower
  than the interval must deliver *fewer* ticks than the elapsed time implies
  **and** report the shortfall in `skipped`, with the control that a fast
  handler reports `skipped: 0`. Asserting only "ticks arrived" passes on a
  catch-up implementation, which is the defect.
- **ONE-SHOT** — `after` fires exactly once and the program **exits by
  itself**, with the control that a program holding a repeating timer does not.
- **CANCEL** — cancelling the last timer ends the loop; cancelling twice
  answers `false` rather than raising.
- **MONOTONIC** — a source tripwire, because §3.2 cannot be tested from
  outside.
- **UNWATCHED** — the 2108 warning fires, with the control that a watched timer
  is silent.
- **STREAM** — the shape the ask asked for, end to end: a parked SSE stream
  poked by a timer, with a client receiving events it never requested.
- **VALGRIND**.

# A handler in its frame: a review, measured

**Status:** Review for Matthew, 2026-11-20. Nothing is built. This follows
`docs/closures_review.md` §4 option 4: "give a handler what a watch already
has: it runs in the frame that declared it". The question here is how much
real code that would help, measured before it is designed.

## 1. What happens today

- **A window's `on` block is a hidden function.** It is called beneath
  `ui.run()` with a fresh frame whose parent is the global frame, as every
  function call is (`src/eval.c`, `local_env.parent = ... &global_env`).
- **`program main`'s variables live in the global frame.** So a handler can
  read `main`'s `n`. If it writes `n = n + 1`, it makes a handler-local, and
  warning 2104 says so. Measured on this build:
  `f reads n = 1`, `main after: 1`.
- **A watch already runs in its frame.** Its body is run with `current_env`
  set to the frame that declared it (`src/eval.c:7970`,
  `docs/watch_scope_design.md` §3). The mechanism exists; handlers do not use
  it.

## 2. What real code does

Every `on <part>.<event>` block in this repository was classified by script,
along with gBASIC Studio, gdash and tedderland.

| Corpus | Handlers | Writing a field of an outer record | Rebinding a name they read |
|---|---|---|---|
| this repository, `on` blocks | 166 | **55** (54 of them a record named `g`) | **0** |
| gBASIC Studio | 65, all named functions | all 65 read the global record `G` | 0 |
| gdash, tedderland | 0 `on` blocks | — | — |

- **The zero is the documentation working, not an absence of need.** The
  tutorial and the UI cookbook both teach "one record, handlers mutate its
  fields". A third of all `on` blocks follow that idiom.
- **Studio would not benefit at all.** Its handlers are named functions
  connected with `qt_on(win, part, "pressed", fn)`. A named function has no
  declaring frame to run in, so this change cannot reach it.
- **The reported failures were in functions, not `on` blocks.** DOGFOOD has
  two entries:
  - 2026-07-20 (NAP-3, medium): an event-loop callback's `ticks = ticks + 1`.
  - 2026-08-22 (Studio STU-10, high): `G_app = r.app` in a tool adapter made a
    local. The tool reported success and nothing had changed.

  Both are named functions. Neither would be fixed by running an `on` block in
  its frame.
- **Server handlers are a different case.** With `workers:` above 1, each
  worker is its own process. A handler rebinding `main`'s variable would
  change only that worker's copy, so a shared frame would promise more than it
  can keep. The `server.draining` entry (DOGFOOD, 2026-08) was a field on the
  server record, and is already answered.

## 3. What it would cost

- **Scratch names would collide.** 44 of the 166 blocks assign a plain name.
  5 of those names are also assigned in the file's `program`: four `x` and one
  `again`, all discard results such as `x = ui.set(...)`. Run in `main`'s
  frame, those would overwrite `main`'s variable of the same name. All five
  are harmless today, but it is the trap a watch body already has, made
  common.
- **Which frame is not obvious.** A `window` is declared at the top level, so
  its declaring frame is always the global one. The useful frame is the one
  that **opened** the window, as `form to edit cust` already takes `cust` from
  there. That would let a window opened from a function reach that function's
  locals, which nothing can do today.
- **The work itself is small.** Run the hidden function with its parent set
  to the opening frame, plus one of two rules:
  - **A**: the watch rule. The body runs in that frame and every assignment
    lands there, scratch names included.
  - **B**: rebind what exists. A name that already exists in the frame is
    rebound, and a new name stays the handler's own. This leaks no new names,
    but it still has the collision above.

## 4. Recommendation

**Do not build it now.** It would remove a well-taught idiom from a third of
`on` blocks. It would fix no defect anyone has reported. It would not reach
Studio, the largest GUI program. It adds a collision trap in exchange.

The failure that actually cost time was the **silent** one in a named
function (STU-10, high): a callback rebinding a global and getting a local,
with a correct-looking result. If that is worth more work, the cheaper lever
is there, not in `on` blocks. Two options:

- Make warning 2104 an error inside a function that is connected as a
  handler or callback.
- Measure how often 2104 fires on correct code before proposing it be an
  error anywhere.

This is Matthew's call. The measurements above are the evidence for it.

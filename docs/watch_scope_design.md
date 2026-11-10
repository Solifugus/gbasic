# A watch belongs to the scope that declares it

**Status:** Increment 1 (§3) SHIPPED 2026-11-07, increment 2 (§4, `wait
until`) SHIPPED 2026-11-08, both held by `tests/run_watch_scope.sh`. Matthew
ruled on §5 on 2026-11-07, `for <duration>` confirmed the next day. Two
departures from the text below, each in §6: `wait` is a keyword only before
`until`, and a wait past the depth cap raises rather than warning.

**Why:** Transward wanted a library to set up its own watcher, and found that a
`watch` in a function does nothing. Matthew's model, in his words: *"a watch
works within the scope in which it is declared, not only in the global scope.
If function returns quickly and kills the scope then the watch dies with it."*
That is not how it works today (§1). This document proposes making it so.

## 1. How it works today (measured 2026-11-07)

- **One registry for the whole program.** `watcher_register` (`src/eval.c`)
  appends to a global array, and nothing removes an entry when a scope ends.
- **Only the global frame may register.** In a function, the anonymous form
  prints `watch may only be registered at top level for now` on stderr, with no
  position, no code, not catchable, registers nothing, and the program carries
  on (exit 0). The named form (`watch w(...)`) raises `1003`, because a handle
  in a vanishing scope would be a watcher that fires for ever with its
  off-switch out of reach.
- **A watch fires only while the global frame is current.**
  `watcher_trigger_change` returns at once otherwise, and the body runs in
  whatever frame is current when the queue drains, which is the global one.
- **Two kinds of firing.** A watch on a *variable* fires on registration and on
  each assignment, synchronously: `watch(x)` then `x = 2`, `x = 3` printed
  `x is 1`, `x is 2`, `x is 3`. A watch on an *event source* (`timer.ticks`,
  `inbox.messages`, `server.requests`, `http`/`net`/`process` events) fires only
  from the event loop (`webserver_run_event_loop`), which runs **after** the
  `program` block ends: a 0.2 s timer, then `sleep(1)`, printed `registered`,
  `after sleep, end of program block`, then `FIRED`.
- **A library's top-level statements do not run at all.** A `print` and an
  assignment at the top of a `library` block did nothing, silently, so "a watch
  at a library's top level" is not available either. That is a separate
  surprise, logged in DOGFOOD the same day.
- **Nothing depends on the current behaviour.** Of every `watch` statement in
  gbasic (140, `.bas` and Markdown), gdash (1), Transward (6), gbasic-studio
  (1) and tedderland (0), none is inside a function, a `sub` or a handler. The
  scan's 18 apparent hits in gbasic's Markdown were all prose lines beginning
  with "function". So scoping changes no existing program.

## 2. The rule

**A watch belongs to the frame that declares it**, the global frame or one call
of a function, and lives until that frame ends or it is unwatched.

- **Its body runs in that frame**, so it sees the function's parameters and
  locals, and an assignment in it is an assignment to them.
- **Each call has its own watch.** Two calls, or a recursive call, register
  two watches, each in its own frame, each ending with its own return.
- **It ends with the frame, by every exit:** `return`, the end of the body, and
  a raise unwinding through it. A watch never outlives the frame it can see.
- **Blocks are not frames.** gBASIC has function scope, so a watch in an `if`
  or a loop inside a function lives until the function returns, as a variable
  assigned there does.
- **The global frame is unchanged.** A top-level watch lives for the program,
  as now.

## 3. Increment 1: watches on a function's own variables

**What it gives.** A function can watch its own locals:

```basic
function tally(items)
    total = 0
    watch(total)
        if total > 100 then
            print "over budget at " + string(total)
        end if
    end watch
    for each it in items
        total = total + it.cost
    end for
    return total
end function
```

**How.**

- **The registry entry records its frame.** `invoke_function` ends the
  watches its frame owns on every exit path. It marks them inactive, as an
  unwatched or redeclared watcher is now, so a queued firing is skipped.
  `Env` frames are on the C stack and their addresses are reused, so the
  ending must happen at the return, never by comparing addresses later.
- **A change fires the watches of the frame it is made in.**
  `watcher_trigger_change` compares the entry's frame with the current one,
  where it now compares the current one with the global frame. A callee's
  local of the same name is the callee's, so a caller watching `x` is not
  fired when the function it calls assigns its own `x`.
- **It fires on registration, as a top-level watch does.**
- **The named form is allowed in a function.** `watch w(x)` gives a handle
  that `unwatch(w)` honours until the frame ends. A handle that outlives its
  frame (returned, stored) names a watch that has ended. `unwatch` of it does
  nothing, the same as unwatching a watcher twice.

**Refused, located, catchable** (`1003`, the named form's code), each because
the watch could never do anything:

- **A watch in a function on a name that is not that function's local** (or
  parameter). A function cannot assign a global (an assignment creates a
  local), and a global changes only at the top level, while no function is
  running. Such a watch could fire only on registration.
- **A watch in a function on an event source**, until increment 2. The loop
  runs only after `main`, so the watch would end before any event arrived.
  The message says the function would have to wait for events, which is §4.

**What changes for existing programs:** nothing (§1's census). The bare
stderr line goes away: the anonymous form registers or is refused like the
named one.

**Tests** (a new `tests/run_watch_scope.sh`, self-checking, every answer by
hand):

- fires on each assignment with locals visible, and assignments in the body
  reach the locals;
- two calls register two watches;
- recursion, each level its own;
- a second call does not fire the first call's watch (it ended);
- a raise unwinding through the function ends its watch;
- a callee's local of the same name does not fire the caller's watch;
- named in a function, and `unwatch` of it inside and after;
- each refusal beside an accepted neighbour;
- the top level unchanged, by the existing suites (`run_inbox`, `run_timer`,
  named watchers).

Perturbations, each of which must go red: an entry not ended on return
(two firings), an entry ended only on a normal return (the raise case), and
firing ignoring the frame (the same-name case).

## 4. Increment 2: event sources in a function, which needs a wait

**The problem.** Events are delivered only by the loop, and the loop runs only
after `main` returns. A function that watches `server.requests` and returns
ends its watch before any request can arrive. To be useful, a function must stay
alive and let the loop run while it waits.

**The proposal: `wait until <condition>`**, a statement:

```basic
function serve(port)
    server = webserver.listen(port)
    served = 0
    watch(server.requests)
        while count(server.requests) > 0
            req = take_first(server.requests)
            append(server.responses, { id: req.id, body: "hi" })
            served = served + 1
        end while
    end watch
    wait until served >= 10
    webserver.close(server)
    return served
end function
```

- **It runs the event loop from inside the current frame**, delivering to the
  watches of **every live frame**: the global frame's and those of every call
  on the stack, since all of them are still alive.
- **The condition is re-checked after each delivery**, and the statement ends
  once it is true.
- **This is the same change that ends the open `qt.run()`-from-a-function
  defect** (DOGFOOD live list row 1). That defect is the loop running inside a
  function while delivery is refused because the global frame is not current.
  With delivery to every live frame, `qt.run()` inside a function services its
  sources too.
- **Transward's case:** a library function, such as `transport.run(config)`,
  watches its own sources and waits until it is stopped. The library owns the
  watching; the program calls one function.

## 5. Decisions for increment 2

**Matthew's ruling, 2026-11-07:** *"the spelling is good and I agree with
everything else."* So: `wait until <condition>`; a wait that no live source
could end raises, naming the condition; a time limit as `for <duration>`,
raising when it expires (the first of the two options in 3, taken as the one
agreed); and nesting allowed with a depth cap and a warning. The questions as
they were put:

1. **Spelling.** `wait until <condition>` is a new reserved word, `wait`;
   `until` is already reserved by `do ... until`. The alternative is a builtin,
   but a condition re-checked after each delivery is an expression the
   statement re-evaluates, which a function argument cannot be without
   closures.
2. **When the sources run out first.** If no source is left that could make the
   condition true (no listener, no timer, no transfer), the wait would block
   for ever. The proposal is to raise, naming the condition, rather than hang.
3. **A time limit.** `wait until done for 30s` raising when it expires, or
   leaving that to a timer watch that sets a flag.
4. **Nesting.** A watch body that calls a function that waits gives a wait
   inside a wait. The proposal is to allow it with a depth cap and a warning, as
   Qt handler re-entry has (`2117`).

## 6. What building increment 1 found

- **A handle holds a serial, not an index.** Entries come and go at the
  registry's end as frames do, so an index is reused; a handle that outlived
  its frame would have reached a newer watcher through it. The registry is
  sorted by serial, so a handle is found by binary search.
- **An ended watch must leave the registry,** not stay as a tombstone: kept,
  every change scans them all, and the shape tier measured a function called
  20,000 times at 16.5x the time of 5,000 calls. It is removed at once unless a
  drain is under way, when a queued index must not be reused.
- **The ending is in `env_clear`,** which every frame passes through on every
  exit (function calls and modifier frames), so no exit path can miss it. A
  perturbation ending watches only on a normal return went red on the raise
  case.
- **Two of my own checks needed fixing before they meant anything.** The shape
  tier first let a 120 s `timeout` cap the slow run, which made a quadratic
  registry read as linear (a timeout is a failure now). And a perturbation that
  ignored the frame changed only one of the two places the frame is tested, so
  it stayed green; done fully, it is red on the same-name case.

### What building increment 2 found

- **`wait` is a keyword only when `until` follows** (§5.1 said a reserved
  word). Measured first: gdash's throttle has a variable `wait`, and
  `gsql.close` takes `{ wait: 10 }`. `until` is already reserved, so the pair
  means nothing else, and it costs no grammar conflicts; it is the lexer's own
  idiom for `window`, `database` and `sql`.
- **Past the cap a nested wait RAISES** (§5.4 said a warning, as Qt's `2117`).
  A Qt handler past its cap is simply not called; a wait cannot be skipped, as
  the code after it relies on its condition being true.
- **A delivery and an assignment differ.** A delivery by the loop fires every
  live frame's watches; an assignment fires its own frame's. The flag is
  cleared before the bodies run, so their own assignments are assignments.
- **A server in a local** is found through the frames of live watches, and its
  requests go to that frame alone; they used to be found only in a global.
- **A wait in a watch body deadlocked first.** The running drain could not take
  the wait's deliveries (it was running the body that waits), and the outer
  queue's `pending` marks then dropped them, a watch registered in that very
  body among them. The wait now sets the running queue aside, clearing those
  marks, and restores both after.
- **It ended live row 1** (`qt.run()` from a function starved the loop), as §4
  predicted: that was a delivery refused because a function frame was current.
- **One of my own tiers could hang.** Valgrind ran the fixture unbounded, and
  a perturbation removing the no-source check hung it for 15 minutes. It is
  bounded at 240 s and a timeout is a failure.
- **Eleven perturbations, all red:** increment 1's four, then delivery to the
  current frame only (the top-level-beside check and qtrun), servers found in
  globals only (serve), no set-aside and the pending marks kept (the nested
  wait), no no-source check and the limit ignored (hangs, failed by timeout),
  and no depth cap.

## 7. Not proposed

- **Closures.** A watch sees its own frame because its body runs in it, not
  because it captures it. A watch never outlives its frame, so nothing needs
  capturing.
- **A watch that outlives its frame.** That is what a top-level watch is for.
- **Running a library's top-level statements.** That is a question of its own
  (DOGFOOD, 2026-11-07).

# Closures: a review, and a recommendation

**Status:** Review, 2026-11-18, for Matthew to decide (§5). Matthew,
2026-11-18: "I am not sure we need or want it. We should do a review and
decide."

## 1. Where things stand

- **A function value is a reference to a named function** and captures
  nothing (`docs/first_class_functions_design.md` §2). That document deferred
  closures with a condition: "add only if real code demands capture, and only
  with a cycle story" (§13).
- **`bound(fn, context)`** carries one value, **copied** when it is bound, and
  passes it as the last argument (`docs/reference.md`). It is not a closure:
  changing the original afterwards is not seen. Being a copy, it cannot make a
  reference cycle, and it keeps `encode`, `serialize` and actor sending
  intact.
- **A watch sees the frame that declares it** (`docs/watch_scope_design.md`).
  It does that by running in that frame, not by capturing it, and it never
  outlives the frame. That design rejected closures in so many words: "A
  watch never outlives its frame, so nothing needs capturing" (§7).
- **There are no `map`, `filter` or `reduce` builtins.** gBASIC code loops.
  So the place a closure would pay off in most languages, a short function
  handed to a collection operation, barely exists here.

## 2. What real code ran into

Every DOGFOOD entry that mentions closures was read for this review. They
fall into three kinds.

**(a) A callback needs data.** There were two:

- a heartbeat callback that needed a database handle;
- `tools.dispatch`, where one dispatcher could not serve 33 entries.

`bound` answers the first. The second was answered by calling the entry's
function as a method, `e.fn(prepared)`, so `this` is the entry. Neither needs
capture.

**(b) A handler must change shared state.** This is the common one:

- a GUI handler cannot rebind a variable of `main`. Warning 2104 says so, and
  the tutorial teaches "one record, handlers mutate its fields";
- a `server` handler could not reach the server record to set `draining`
  (2026-08 entry).

This is the friction people actually meet. **A closure in the usual sense
would answer it by sharing a variable between two scopes.** That is a
reference to a mutable binding, which gBASIC has nowhere else.

**(c) Passing `lib.fn` as a value** was refused. It was fixed on its own and
was never about capture.

**Use, measured 2026-11-18:**

- `bound(` appears **11** times in `stdlib` (two libraries, `dates` and
  `rules`) and **0** times in `examples` and in gBASIC Studio.
- Studio passes function values about 170 times (a rough count) and never
  binds one.

The demand for carrying data into a callback is real, but small, and already
met.

## 3. What closures would cost

- **Value semantics.** A record or array is a value: assigning copies it, and
  copy-on-write makes that cheap (`run_arridx.sh`, `run_recidx.sh`). Capture
  by reference makes a variable shared between two frames, a kind of value
  gBASIC deliberately does not have. Every reader of a function would then
  have to ask what else can change its variables.
- **Cycles in a reference-counted runtime.** A closure that captures a record
  holding the closure is a cycle, and reference counting never frees it. The
  first-class-functions design named this as the condition for adding
  closures at all. There is no cycle collector, and adding one is a project of
  its own.
- **Sending and saving.** A function value crosses to an actor or into
  `serialize` as its **name** (§2 there). A captured environment has no name.
  It would be refused, as a bound value is, or would need a wire form of its
  own.
- **"No hidden machinery."** The reference's rule is that a callee receives
  a value only as an argument or through `this`. `with principal` is the one
  exception, and it was argued over at length (DOGFOOD, 2026-09). Closures
  would make implicit capture the ordinary case.
- **The LLVM backend.** Closures make compiling harder (environments that
  outlive their frames are allocated on the heap). Not deciding this now
  leaves that cost out of code generation.

## 4. The options

1. **No closures, and close the question.** Keep `bound` for data, `this` for
   objects, and records for shared state.
2. **Capture by copy**: an inline function that copies the values it
   names when it is made. It is `bound` without writing the context. It makes
   no cycles and changes no value semantics, but it **does not solve (b)**:
   a copy cannot change `main`'s variable either. It saves typing for (a),
   which is already rare.
3. **Closures by reference**: they solve (b), at every cost in §3.
4. **Solve (b) directly, without closures.** Give a handler what a watch
   already has: it runs **in the frame that declared it**, so
   `on bump.pressed` written inside `program main`, or a function, reads and
   **writes** that frame's variables while the frame lives. Nothing is
   captured, so nothing can outlive its frame or make a cycle. This is the
   watch-scope design applied to handlers. It would retire warning 2104's
   most common case and the tutorial's "one record" workaround. It is a
   language change of its own, and needs its own design note before it can be
   promised.

## 5. Recommendation

**Option 1 for closures, and option 4 as an item in the "less work for the
developer" sweep.**

- Close closures as **not wanted**. Real code's demand for carrying data
  (a) is small and met by `bound` and `this`. The demand that is real and
  common (b) is about **shared state**, and closures answer it by giving up
  value semantics and adding cycles.
- Answer (b) as watches were answered: by the frame a handler is declared in,
  not by capture. Write a design note during the sweep, and price it there.
  It touches `window` handlers and `server` handlers alike.
- Update `docs/first_class_functions_design.md` §13 to say closures were
  reviewed on 2026-11-18 and declined, with this page as the reason. Then
  the deferral stops looking like an open promise.

# `tools`: one declaration of what a model may call

**Status:** Shipped (2026-09-06). `stdlib/tools.bas`, `tests/run_tools.sh`,
`tests/run_inbox.sh`. Step 3 of
[the AI reference proposal](gbasic_ai_reference_and_primitives.md).

## 1. Why this exists

`llm.bas` already lets a program hand a model a set of callable tools. Three
things about that shape do not survive contact with an agent, and each is a
defect rather than a preference.

**The schema and the validator are two hand-written things.** `llm.tool(name,
description, schema, fn)` takes a JSON-Schema record *and* a function, and
nothing checks that they agree. The schema can tell the model `id` is required
while the function reads `customer_id`; the model does as it was told and the
tool fails for a reason nobody can see from either side. Here `params` is the
declaration, `tools.schema` derives what the model is told, and
`tools.dispatch` validates against the same thing. They cannot drift, and
`tests/run_tools.sh` drives dispatch *from* the published schema to say so.

**A raising tool kills the run.** `llm.bas` documents that a tool "must not
raise", which was true when it was written — a raise could not be caught at
all. PLAT-ERR changed that and the rule outlived it. Measured on the existing
path, a tool that raises still ends the whole program. `tools.dispatch` runs
the body under `on error` and returns *any* failure as a result the model can
read, which is the difference between a bad tool call and a dead agent.

**A tool body on the event loop freezes every client.** A request handler runs
on the loop's thread. A tool that queries a database or calls a service blocks
there for its whole duration, and moving the body to a worker helps only if the
*reply* also arrives without blocking. Both halves are covered: `tools.serve`
runs the body in a pre-spawned worker, and `watch(inbox.messages)` delivers the
reply through the loop's own `poll`.

## 2. The declaration

```basic
ts = tools.define("crm", [
    { name: "lookup",
      describe: "Find a customer by id",
      params: [
        { name: "id",    type: "string", describe: "the customer id", required: true },
        { name: "limit", type: "number", describe: "how many rows",   required: false, default: 10 }
      ],
      reads: ["customers"],
      mutates: [],
      fn: lookup }
])
```

A tool function takes **one argument**, the arguments record, matching what
`llm.bas` already does. Parameter types are `string`, `number`, `boolean`,
`array`, `record` — deliberately the JSON types that gBASIC also has a
predicate for, so what the model is told and what dispatch checks are the same
set with no mapping between them.

`reads` and `mutates` are **required**, not defaulted to empty: "this tool
changes nothing" is a claim somebody should have to make. The library does not
gate on them. Whether a mutation needs approval is a decision, and decisions
belong to whoever is accountable for them; this only makes the question
answerable without opening the body.

Every field is checked at `define` time and an unrecognised one is refused **by
name**, the rule `webserver.listen` follows for its options: a misspelled
`describe` is otherwise indistinguishable from a deliberate omission, and the
tool ships with no description and nothing said.

## 3. The surface

| Call | What it does |
| --- | --- |
| `tools.define(name, entries)` | validates and returns a toolset |
| `tools.names(ts)` | the tool names, in declared order |
| `tools.entry(ts, name)` | one entry, or `unknown` |
| `tools.reads(ts, name)` / `tools.mutates(ts, name)` | the declared effects |
| `tools.param_schema(entry)` | the JSON-Schema subset for one tool |
| `tools.schema(ts)` | the whole toolset as records — what MCP publishes |
| `tools.as_llm_tools(ts)` | the same declaration as `llm.tool` records |
| `tools.dispatch(ts, name, args)` | validate, run, and return a tool-result part |
| `tools.serve(back, ts)` | the worker loop |
| `tools.pool(handles)` | wrap spawned workers |
| `tools.request(who, run_id, call_id, name, args)` | one call to send |
| `tools.send_call(pool, req)` | send to the next worker; returns the advanced pool |
| `tools.is_result(m)` | is this inbox message a worker reply? |
| `tools.shutdown(pool)` | tell the workers to stop |

`tools.dispatch` **never raises**. A model sends whatever it likes and a tool
body is ordinary code that can fail; both must come back as something the model
can read and retry.

`as_llm_tools` is the evidence this is an extraction rather than a second
vocabulary: the same declaration drives the existing chat loop.

## 4. The pool

```basic
function tool_worker(back, ts)      ' in your program, not in the library
    tools.serve(back, ts)
    return nothing
end function

me = self()
handles = []
for i = 1 to 4
    append(handles, spawn tool_worker(me, ts))
end for
p = tools.pool(handles)

watch(inbox.messages)
    while count(inbox.messages) > 0
        m = take_first(inbox.messages)
        if tools.is_result(m) then
            ' m.call_id says which call; m.result is the tool-result part
        end if
    end while
end watch
```

**The caller spawns, not the library**, and that is the language rather than a
preference: `spawn` resolves a bare function *name*, so it takes neither a
library function nor a function value. The worker loop still lives in
`tools.serve`, so the protocol has one definition.

**Why a pool and not an actor per call.** Measured: a `spawn` round trip is
50–78 ms of fork and exec before any work happens, and a `pg` connection cannot
cross `spawn`, so each call would also reconnect. Once a worker exists a message
round trip is 0.015 ms and it keeps its own connections. So the pool is built
once at startup.

**`send_call` returns the advanced pool.** A gBASIC record is a value, so a pool
mutated inside a function is mutated in a copy — the same reason
`accounting.post` returns the new ledger.

**The principal travels as ordinary data.** A principal does not cross `spawn`;
the message carries it and the worker's first act is to re-enter the scope. An
identity that arrived without anyone writing it down is one nobody can audit,
so the handoff is explicit by design rather than by omission.

## 5. `watch(inbox.messages)`

`receive()` blocks, which is right for a sequential program and fatal in a
handler. The interpreter's **one** inbox therefore joins the event loop's
`poll` set, and a message arrives as an event:

```basic
watch reader(inbox.messages)
    while count(inbox.messages) > 0
        m = take_first(inbox.messages)
        ' ...
    end while
    if done then unwatch reader
end watch
```

`load` has no part in this — `send`, `receive` and `spawn` are builtins — so the
global record is bound when the **watcher is registered**. The watch is what
declares that the program wants delivery, and the binding follows from it.

**A mailbox has no completion**, unlike an http transfer, so the program says
when it is finished: `unwatch` ends the delivery and the loop exits. Without
that a program waits forever for a message that may never come, which is what
the first version did — and a hang is not a failure, which is why the tier that
covers it is bounded.

`receive()` inside a watcher **warns**, for the same reason and in the same
words as `http.wait`: the watcher *is* the event loop, and a pool whose replies
are collected with `receive` has moved the tool body off the loop and then made
the loop wait for it.

## 6. What building it found

- **`spawn` takes a bare function name**, not a library function and not a
  function value. Recorded in `DOGFOOD.md`; it is why the pool's spawn is the
  caller's.
- **A function value crosses `spawn` and is callable in the child.** This is
  what lets a whole toolset — records containing function values — be handed to
  a worker wholesale.
- **The root mailbox was never closed.** `ensure_root_mailbox` opens a
  socketpair the root actor owns for life and nothing released it, so every
  program that called `self()` or `spawn` ended with two descriptors open. Not
  a new defect: an untouched `examples/spawn_handle_passing_test.bas` reports
  the same two. It went unseen because **no suite had ever run valgrind over an
  actor program** until `run_inbox.sh`. Fixed at teardown.
- **A record passed to a function and mutated is mutated in a copy.** The first
  `_prepare` filled an out-parameter, so every tool ran with an empty argument
  record. It failed loudly only because the tool read a field that was missing.

## 7. Not built

- **No approval gate.** `mutates` is published; who may act on it is a decision.
- **No retry or backoff.** A failed tool returns a result; what to do about it
  belongs to the agent loop (step 5).
- **No worker health checks.** A worker that dies takes its in-flight call with
  it and the caller waits. Respawning needs a liveness signal the actor layer
  does not offer yet, and inventing one here would be the wrong place.
- **No `via:` binding.** The proposal allowed `fn` *or* `via` (a named module
  function); only `fn` is implemented, because a function value already names
  anything reachable and a second spelling would be a second thing to validate.

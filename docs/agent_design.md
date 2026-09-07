# `agent`: a conversation is a value, and the loop is a pure step

**Status:** Shipped (2026-09-07). `stdlib/agent.bas`, `tests/run_agent.sh`.
Step 5 of [the AI reference proposal](gbasic_ai_reference_and_primitives.md),
whose §1.8 is the argument.

## 1. Why it is not a loop

The naive design is a loop: send the transcript, take the reply, dispatch any
tool call, re-send, until done. It cannot be written that way, and the reason
is the **approval gate**. When the model asks to do something that changes the
world, a person has to say yes. They may take a minute. The wait spans HTTP
requests. The server is single-threaded and watcher-driven. Nothing may block.

So a conversation in progress is a **record**, and the loop is a pure function:

```basic
result = agent.apply(run, event)
' result.run     -- the new run
' result.actions -- what the caller must now do
```

`apply` performs no I/O. It returns the new run plus an array of *actions*; the
caller performs them and feeds the results back as events. Because the run is
plain data it survives `encode`/`decode`, can be stored between HTTP requests,
and can be resumed after a restart — which is the only thing that makes an
approval arriving ten minutes later possible at all.

## 2. What the run may contain is decided by `encode`

Two structural facts follow, and neither was in the design:

**A toolset cannot be in the run.** It holds function values and `encode`
refuses them, so the run keeps `tools.schema(ts)` — ordinary records, which
carry `mutates`, which is exactly what the approval decision needs. The caller
keeps the toolset for dispatch. This is the same rule §1.8 already states for
the live model handle, reached from the same place: *a run that cannot be
stored is not a run*.

**`expires_at` is a number.** `encode` refuses a datetime too, so the run
carries epoch seconds and `agent.is_expired(run, now_seconds)` compares them —
which also keeps the clock read out of the library and in the caller, where a
test can name the moment.

```
run = {
    id:, ctx:, system:, tools: [...],
    transcript: [...], step:, stage:,
    pending: [...], results: [...],
    usage: { input:, output: }, expires_at:
}
```

`stage` is one of `idle`, `calling_model`, `awaiting_tool`,
`awaiting_approval`, `done`.

## 3. The surface

| Call | What it does |
| --- | --- |
| `agent.begin(ctx, system, toolset)` | a fresh run; publishes the toolset as data |
| `agent.apply(run, event)` | `{run, actions}` — the pure step |
| `agent.is_expired(run, now_seconds)` | a comparison, not a clock read |
| `agent.idempotency_key(run, call_id)` | what a mutating tool uses to recognise its own earlier effect |
| `agent.user_event(text)` | a person said something |
| `agent.model_event(msg, usage)` | the model replied (a canonical `llm.message`) |
| `agent.tool_result_event(call_id, result)` | a `tools.dispatch` result came back |
| `agent.approval_event(call_id, granted)` | a person answered |
| `agent.cancel_event(why)` | stop, from any stage |

Actions are `start_model`, `dispatch_tool`, `ask_approval`, `emit_text`,
`finish`.

Events are **constructed** rather than written as literals, so a malformed one
is refused where it is written rather than three stages later.

**`apply`, not `step`.** `step` is a keyword — `for i = 1 to 10 step 2` — so it
cannot be a function name, the same wall `new` and `stop` put up elsewhere in
this stdlib. `apply` is also what this library already calls folding an event
into a state: `lending.apply` and `credit.apply` are the same shape over a loan
and a portfolio.

## 4. The approval gate

A tool that declares `mutates` needs a person to say yes. The default is **on**,
because the safe default is the one you have to switch off deliberately; a
context carrying `approve_mutations: false` turns it off for that run.

The flow is then trivial, which was the point:

1. the model asks for `freeze_card`; `apply` returns `ask_approval`
2. the caller stores the run and renders the request
3. the approval endpoint loads the run and applies an `approval` event
4. `apply` returns `dispatch_tool`

**A refusal is an answer, not an error.** A declined call appends a tool-result
part marked `is_error`, naming who declined — so the model is *told*, in the
transcript, and can say so or propose something else. A refusal that raised
would end the conversation, which is not what a person declining one action
means.

The idempotency key is `run.id + ":" + call_id`. **Concatenated rather than
hashed**, against the design's wording: both halves are already opaque ids, so
a hash adds no uniqueness and costs the reader the ability to see which run and
which call an effect belongs to.

## 5. Purity, and what the language gives for free

`apply` reads no clock, opens no socket, generates no id. `tests/run_agent.sh`
asserts this **structurally** — by reading the function's body — because a
behavioural test cannot notice a clock read that happens to return the same
value twice, and a run that read the time would resume differently after a
restart. That tier is the only one that catches it; the perturbation proving so
is a single added `epoch(now())`.

One thing came free. A gBASIC record is a **value**, so `apply` cannot mutate
the run its caller holds even by accident — the copy-on-write that makes
`accounting.post` return a new ledger makes purity here structural rather than
disciplined.

## 6. Not built

- **No `llm` call.** `apply` returns `start_model`; the caller performs it. That
  is why `llm.start`/`poll`/`read`/`wait` are not needed yet.
- **No persistence.** The run encodes; where it is stored is the application's.
- **No expiry sweep.** `is_expired` is the predicate; the periodic delete is a
  job, not a library function.
- **No cost accounting.** `usage` accumulates tokens; converting to money needs
  per-model prices, which change, and belong to configuration.

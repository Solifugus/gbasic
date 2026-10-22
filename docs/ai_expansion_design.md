# AI capability expansion: extending `llm`, `tools` and `agent`

**Status:** Proposal, v0.3 (2026-10-21). **Nothing in it is implemented** except where a line says *exists*.
**Supersedes:** `gbasic-llm-enhancement-proposal.md` (the v0.1 discussion draft of the same day), renamed and revised here rather than kept beside it, so the index has one row and nobody has to reconcile two documents.
**Related:** `docs/llm_design.md`, `docs/tools_design.md`, `docs/agent_design.md`, `docs/gbasic_ai_reference_and_primitives.md`, `examples/steward/steward.bas`.

## 0. What checking v0.1 against the tree changed

v0.1 was written from reading the libraries. v0.2 checked each premise against the source, and these are the differences:

| v0.1 said | the tree says | consequence |
|---|---|---|
| `tools.simulated(...)` and `tools.decoy(...)` **factories** returning a handler that holds its fixture | gBASIC has **no closures**: a returned function value cannot hold anything, and the tool pool's `spawn` takes a bare function **name** | No factories. A simulated or decoy tool is an ordinary **named** function in an ordinary `tools.define` entry (§4.2) |
| Streaming: "measure `webclient` support" | `webclient` **blocks** by design. `http` is the non-blocking module: `http.start`/`read`/`stop`, readiness delivered through `watch(http.events)` | Streaming is built on `http`, and what is missing is a **parser**, not plumbing (§5) |
| Q2: "what warning API should a tool refusal use?" | `warning(...)` exists, with `on warning` suppression and escalation (PLAT-WARN) | Answered. A refusal is a warning on that channel; no audit hook is invented (§4.1) |
| Q3: "what worker, watcher, streaming primitives exist?" | Event-loop sources: `server.requests`, `inbox.messages`, `http.events`, `timer`. Workers: `tools.pool`/`tools.serve` over `spawn` | Answered |
| Q4: "does `tools.define` need a principal argument?" | `with principal(p)` / `principal()` exist and are dynamically scoped; the pool carries the principal **as data** into the worker, since a principal does not cross `spawn` | Answered: no argument. Dispatch already runs under the caller's principal |
| An unknown tool should return `not_authorized` | `tools.dispatch` already returns an error **result** (`unknown tool 'x'`) rather than raising | The delta is a stable `code` field and a warning (§4.1), not a new behaviour |
| A job runner with priority, aging, leases and fencing (§4 of v0.1) | Steward, the reference application, found **one pool serves both kinds of work**: a model call is just another thing that blocks | The scheduler is **deferred** until an application needs more than that pool (§7) |
| "~1,782 lines" in `llm.bas` | 1,781 lines today | Immaterial; recorded so the number is not repeated as a fact |

**Design rule (kept from v0.1):** add flexibility only when it does not add complexity to the common case. Prefer extending the three existing libraries and the reference application to adding a framework.

**The harness is the developer's (agreed, v0.3).** The libraries supply model values, tools, a pure agent step and transports; how a program combines them is its own business. A common shape is **local first, cloud when more power is needed**, and it needs nothing from the library beyond what already exists, because a model is a value and every model value answers the same calls:

```basic
load llm
fast = llm.local("http://localhost:11434", "qwen2.5-coder:14b")
deep = llm.anthropic("claude-opus-5-5", env("ANTHROPIC_API_KEY"))

function answer(question)
    draft = llm.ask(fast, "Answer briefly. Say UNSURE if you are not certain.", question)
    if contains(draft, "UNSURE") then return llm.ask(deep, "Answer carefully.", question)
    return draft
end function
```

The escalation rule (a self-reported doubt, a failed JSON check, a length, a cost budget) is the developer's choice, so it is shown in a cookbook recipe rather than built in as a routing policy. What the libraries owe that choice is uniformity: every backend, including an embedded one (§7.1), must accept the same `ask`, `chat`, `ask_json`, tool and streaming calls, or switching backends stops being a one-line change.

## 1. What exists

Every entry here *exists*, with a suite behind it.

- **`llm`** (`run_llm_transcript.sh`): `openai`, `anthropic`, `local` (Ollama/vLLM through the OpenAI wire), `ask`, `chat`, `ask_json`, `embed`, a canonical part-based transcript (`message`, `text`, `tool_call`, `tool_result`), tool calling (`with_tools`, `run_tools`), retries, and three ways to answer without a network: `offline`, `with_transport` and keyed `replay`.
- **`tools`** (`run_tools.sh`): one declaration (`define`) producing both the schema the model sees and the validator `dispatch` enforces, with `reads`/`mutates` declared per tool. A raising tool body becomes an error result, and the next call still works. `pool`/`serve`/`send_call` run bodies in pre-spawned workers so a tool never freezes the event loop.
- **`agent`** (`run_agent.sh`): `begin` and a pure `apply(run, event)` that returns the new run and the actions its caller must perform. The run survives `encode`/`decode` and resumes with the same actions. A mutating tool asks for approval, a declined call is an answer rather than an error, and `cancel_event`, `expires_at` and `idempotency_key` exist.
- **`mcp`** (`run_mcp.sh`): publishing and consuming tools over the Model Context Protocol.
- **`http`** (`run_http.sh`): non-blocking requests, with `http.stop` as cancellation.
- **`with principal`** (`run_principal.sh`): the identity a body acts for. It does not leak into an actor or a request handler.
- **Steward** (`run_steward.sh`): all of the above joined, including two runs suspended while a third completes.

## 2. The common case stays as it is

```basic
load llm
m = llm.local("http://localhost:11434", "qwen2.5-coder:14b")
print llm.ask(m, "You explain SQL clearly.", "Explain SELECT ... GROUP BY")
```

Acceptance case 1 below is that this program, and every existing `llm` example, behaves exactly as it does today. Nothing proposed here changes a return shape or adds a required argument.

## 3. Increments, in order

| # | increment | new surface | depends on |
|---|---|---|---|
| 1 | Tool refusals that are visible (§4.1) | a `code` field on error results; warning on refusal | nothing |
| 2 | Simulated and decoy tools (§4.2) | none, or one optional entry field | nothing |
| 3 | Streaming a model reply (§5) | a pure stream fold in `llm` | `http` |
| 4 | Cancellation, end to end (§6) | wiring only | 3 |
| 5 | Embedded local inference: a spike, then a model constructor (§7.1) | `llm.embedded(...)` | a measured spike |
| — | Scheduler, model management, coding-agent adapters | deferred (§7) | a real consumer |

Each increment ships with its own suite, self-checking rather than golden. Every defect in this area produces an ordinary-looking transcript, and a golden would record it as expected.

## 4. Tool authority

**Decision (kept from v0.1):** the toolset passed to a run is the authorisation boundary. A tool the model was not offered cannot be dispatched, and a name the model invents is **never resolved dynamically**: no lookup by name outside the toolset, and no evaluation of model-supplied source. The toolset is a ceiling and not a substitute for object-level checks, which a tool body makes against `principal()` at execution time.

### 4.1 Making a refusal visible

`tools.dispatch` already refuses an unknown tool, a wrong argument type and a missing required argument as **results** marked `is_error`. That is correct, because the model can read the result and choose again. What is missing is anything telling the *operator* a refusal happened. Two changes:

- **A stable `code` on every error result**: `unknown_tool`, `invalid_arguments`, `tool_failed`, `declined`. Today a program can only tell them apart by matching message text, and the message is for the model. The codes are a closed list, asserted against the source so a new refusal path cannot omit one.
- **`unknown_tool` raises a warning** through `warning(...)`, carrying the tool name and the run id. It does not raise an error, since the run should continue. The warning channel already gives the operator every choice needed: `on warning ignore` silences it, `on warning stop` escalates it, and a handler can log it. No audit-hook parameter is added, because the channel is the hook.

The other three codes do not warn. A wrong argument type is an ordinary model mistake, and warning on it would bury the one signal that matters: a model asking for something it was never offered.

### 4.2 Simulated and decoy tools need no new mechanism

A tool's body is any named function, so:

- **A simulated tool** is a named function returning fixture data. Because gBASIC has no closures, the fixture is read inside the function, for example from a file named after the tool, rather than captured at definition time. Swapping it for the real body changes no schema, since the schema comes from the entry and not from the function. That is the property v0.1 wanted, and it already holds.
- **A decoy** is a toolset entry whose body is inert and synthetic. It never reads a secret, never writes production state and never returns a realistic credential, and it warns when called. It is **advertised**, because the model can only call what it was offered. **A request for an unadvertised name is never routed to a decoy**: it is refused (§4.1). Routing it would make the toolset a lie to the operator rather than a boundary.
- **The one optional addition** is a `classification` field on an entry (`real`, `simulated`, `decoy`), for observability and for test rules such as "a production build contains no simulated tool". `tools.define` refuses unknown fields by name, so this is a deliberate one-word extension to its field list, and `tools.schema` must **not** carry it to the model. Ship it only if a test rule needs it.

The model side of simulation already exists in `llm.offline`, `with_transport` and `replay`. What this increment ships is a cookbook recipe showing one agent run against simulated tools and recorded replies, then the same run against real ones.

## 5. Streaming a reply

`llm.ask` and `llm.chat` return complete responses, and they will continue to. `docs/llm_design.md` deferred streaming because "analysis workloads read complete responses". Steward's chat UI is the interactive case that reopens it.

**Built on `http`, as a pure fold.** `http.read` hands back the bytes that have arrived and frames nothing: a partial SSE event is ordinary. So the work is a parser, kept pure in the shape `agent.apply` already has:

```basic
' Proposed, not existing:
h = http.start(llm.stream_request(m, system, messages))   ' the wire request, with stream on
s = llm.stream_begin(m)
watch(http.events)
    ' ... for an event about h:
    step = llm.stream_feed(s, http.read(h).body)   ' -> { state, events }
    s = step.state                                  ' events: text_delta, tool_call, usage, ...
end watch
' when the transfer is done:
response = llm.stream_end(s)                        ' the canonical response
```

- **`stream_feed` does no I/O.** It folds bytes into state and returns events, so it is testable with no network and with recorded byte streams, the way `agent.apply` is.
- **The load-bearing assertion is equality**: `stream_end` over a recorded stream must equal `from_response` over the same reply delivered whole. A streamed and a non-streamed answer must not differ in content, tool calls or usage.
- **Split everywhere.** The fixture feeds the same recording split at every byte boundary, since a parser that only works on whole events passes every test fed whole events.
- Both wire shapes, OpenAI and Anthropic, each with its own SSE event vocabulary, translated into one event list. Provider-specific detail stays reachable through `raw`, as it does on every transcript part.
- **Replay** records the byte stream keyed exactly as a whole response is, so a streamed run replays deterministically.

## 6. Cancellation

The parts exist: `http.stop` cancels a transfer and leaves what arrived readable, `agent.cancel_event` ends a run, and `expires_at` bounds one. The increment wires them together and states the one thing that cannot be done: **a tool body already running in a worker cannot be interrupted**. A worker is a separate process running gBASIC code with no preemption point. Cancellation therefore stops the model transfer and the run, discards that tool's eventual result, and leaves the worker to finish. The acceptance case asserts that discarded result never reaches a later run.

## 7. Deferred, with the condition that reopens each

- **A scheduler** (priority, aging, worker leases and fencing, per-model concurrency, RAM/VRAM and token-rate budgets). Reopen when an application measurably needs more than one `tools.pool`. That means interactive requests starving behind background ones in a real workload, not a hypothetical one. v0.1's analysis of what such a scheduler must get right (starvation, stale results from a delayed worker, cancellation that a backend may not honour) is the right checklist when it does.
- **Model management** (inspect, download with pinned revision and integrity check, hardware-fit estimate, benchmark). Reopen when an application helps users choose models. When built, an estimate must say it is an estimate, and a benchmark must record the hardware and options it ran on.
- **Coding-agent adapters** (Codex, Claude Code). These are a separate tool-backed agent shape, not a completion API. They are only worth building over a documented automation interface. A PTY in Studio is a user interface, not a library protocol.

### 7.1 Embedded local inference (promoted from deferred, v0.3)

**Why it is worth the work:** a model server such as Ollama is one more process, one more install and one more HTTP layer between the program and the model. Running the model in-process removes those layers, which is a design value here and not only a convenience.

**Shape: a model constructor, nothing more.** `llm.embedded("models/qwen2.5-7b-q4.gguf", { context: 8192, gpu_layers: 99, threads: 8 })` returns a model value that `ask`, `chat`, `ask_json`, `embed`, tool calling and streaming accept exactly as they accept `llm.local`. The harness in §0 switches backends by changing one line. Capabilities the loaded model cannot honour (tool calls, embeddings, JSON grammar) are **refused by name**, never approximated.

- **Configuration is a value; the loaded model is a handle.** The constructor's record is serializable and can sit in an `agent` run; the loaded weights are a live, non-serializable handle that is freed deterministically, like every other handle kind. A run that holds a model value resumes after a restart by loading again.
- **Built on llama.cpp** (MIT), GGUF models. Not a new inference engine.
- **An optional module** behind its own `HAVE_*` switch. A build without it refuses `llm.embedded` with a sentence naming the missing support, and `has_builtin` and `--capabilities` answer for it. Whether the release downloads carry it is a tier decision measured the way `run_library_tiers.sh` measures every other one.
- **The spike comes first, and decides whether to continue:** load a small GGUF, complete, stream, cancel mid-reply, unload, load again; measure peak memory, time to first token and tokens per second against the same model under Ollama on the same machine; measure binary size and build time with CPU and GPU backends; build on Windows. The result goes into this document either way.
- **Open:** a long generation runs in C and the interpreter is single-threaded, so the spike must establish how a reply reaches the event loop without blocking it: a worker process (the tool pool's arrangement, which costs a model load per worker) or a native thread delivering readiness through the loop as `http` does.

## 8. Acceptance cases

1. Every existing `llm`, `tools`, `agent` and Steward suite passes unchanged.
2. An invented tool name returns `code: "unknown_tool"`, warns once with the name, and does not stop the run. Under `on warning ignore` it is silent. A wrong argument type returns `invalid_arguments` and does **not** warn (the control).
3. The same agent run with simulated tools and with real tools sends the model **identical** schemas.
4. A decoy is callable when advertised. An unadvertised name matching the decoy's is **refused**, not routed.
5. Streamed and whole responses produce equal canonical responses for both providers, with the stream split at every byte boundary.
6. A cancelled run issues no further model calls, its stopped transfer leaves partial text readable, and a tool result arriving after cancellation reaches no run.
7. The same harness runs unchanged against `llm.local` and `llm.embedded` for the same model file, and a capability the embedded model lacks is refused by name.
8. An approval applies only to the call stored in the run it answers. This needs a test, because it holds by construction today and nothing asserts it.

## 9. Open questions

1. Should `stream_request` live in `llm`, or should `llm` take an `http` transport the way it takes `with_transport`? The second keeps one request builder; the first is simpler to read.
2. Does a refused `unknown_tool` count against the run's tool-call limit? It should if the limit is about cost, and should not if it is about progress.
3. Should `classification` exist at all, or is a naming convention plus a test enough? Decide when §4.2's recipe is written.

## 10. Revision history

- **v0.3 (2026-10-21):** the harness is the developer's, with local-then-cloud shown as ordinary code; embedded local inference promoted from deferred to a spike and a model constructor (§7.1).
- **v0.2 (2026-10-21):** checked against the tree (§0). Factories dropped (no closures); streaming moved from `webclient` to `http` as a pure fold; four open questions answered from the source; scheduler, embedded inference, model management and coding-agent adapters deferred with reopening conditions; refusal codes and the refusal warning added; acceptance cases made checkable.
- **v0.1 (2026-10-21):** discussion draft, as `gbasic-llm-enhancement-proposal.md`.

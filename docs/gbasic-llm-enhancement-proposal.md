# gBASIC AI/LLM Capability Expansion: Draft Design Proposal

**Status:** Discussion draft, 2026-10-21. **Target:** `Solifugus/gbasic`, branch `master`. This is an extension plan, **not** a claim that the proposed APIs already exist.

## 1. Objective

Enable gBASIC developers to build AI-assisted applications, autonomous agents, and local inference tools with clarity and minimal infrastructure. A trivial prompt should remain trivial; serious applications should gain predictable scheduling, tool authority, model selection, resumability, and observability without building their own orchestration platform.

**Design rule:** add flexibility only when it does not impose incidental complexity on the common case. Prefer composition of current libraries and executable reference applications over a new umbrella framework.

## 2. Repository reality check (important)

The current repository contains more infrastructure than our initial discussion assumed:

| Existing component | Observed design/capability | Consequence |
|---|---|---|
| `stdlib/llm.bas` | gBASIC implementation using `webclient`; OpenAI and Anthropic wire adapters; Ollama/vLLM through OpenAI-compatible endpoints; `ask`, `chat`, `ask_json`, embeddings, canonical message records, tool calling, retries, injected/offline/replay transports | **Extend**, don't rewrite or create duplicate provider abstractions |
| `stdlib/tools.bas` | `tools.define`, generated schema, `tools.dispatch`, declared `reads` and `mutates`, worker-pool integration; dispatch turns invalid calls/errors into tool results | This should be the **sole authority and execution gate** for tools |
| `stdlib/agent.bas` | `agent.begin`, pure `agent.apply(run, event)` state transition, serializable run value, actions for calling model/dispatch/approval; explicit cancellation, idempotency | Keep orchestration execution **outside** the pure state machine; do not invent another agent loop |
| Existing design docs | `docs/llm_design.md`, `docs/tools_design.md`, `docs/agent_design.md`, `docs/gbasic_ai_reference_and_primitives.md` | Amend these in focused follow-up design notes; reconcile dated assumptions with current code |
| Reference application | `examples/steward/steward.bas` and AI test suites | Add real acceptance cases instead of specifying hypothetical infrastructure only |

Several original designs consciously avoided hidden state, blocking the event loop, and gratuitous platform changes. This proposal preserves those constraints. The `llm_design.md` implementation-status heading is historically dated (it names 1,182 lines whereas the inspected `stdlib/llm.bas` currently has ~1,782); treat executable source and tests as the authority.

### Architectural decision

Keep **three distinct responsibilities**, not three stacked mandatory layers:

- **`llm`**: request/response semantics, provider adapters, embeddings, serialization, inference backends.
- **`tools`**: the declared, validated, authorized universe of callable functions, regardless of whether real, simulated, or decoy.
- **`agent`**: pure resumable decisions and approval lifecycle, producing actions but performing no I/O.

An optional **job-runner/scheduling component** performs blocking and asynchronous I/O. Ordinary synchronous `llm.ask` remains available and needs none of it.

## 3. Proposed public experience

These are **illustrative sketches**, using existing gBASIC syntax and functions where possible; proposed names are not implemented.

### 3.1 Simple synchronous use: no new ceremony

```basic
load llm
m = llm.local("http://localhost:11434", "qwen2.5-coder:14b")
answer = llm.ask(m, "Explain SQL clearly", "Explain SELECT ...")
print answer
```

Retain current response/failure semantics rather than silently changing return types. A new one-call convenience constructor can be considered only if multiple examples demonstrate that it improves clarity.

### 3.2 Conversation state stays data

```basic
messages = [llm.message("user", [llm.text("Explain this ETL failure")])]
r = llm.chat(m, "You diagnose ETL jobs", messages)
' Store canonical messages and append subsequent turns explicitly.
```

Do not store live connections, executable closures, or secret-bearing handles inside a persisted conversation. The current `agent` design already enforces this separation.

### 3.3 Optional job submission

```basic
' Proposed optional runner API, NOT existing syntax:
job = inference.submit({model: m, system: "Explain SQL", messages: messages,
                        priority: 70, stream: true, deadline_at: unknown})
result = inference.wait(job.id)    ' for batch clients
' GUI/web clients consume events and never block the event loop.
```

Provide a synchronous wait and event/inbox path over the **same job record**. An `await` shorthand should only be added if gBASIC's actual concurrency semantics justify it. Do not create three separate execution engines.

## 4. Scheduling and resource accounting

A single logical queue can serve both interactive and background work, but **priority alone is insufficient**: avoid starvation with aging or bounded weighted fair scheduling. The job's explicit, serializable record should include:

`id`, `owner/tenant`, `created_at`, `priority`, `deadline_at` (optional), `state`, `request`, `model_requirement`, `resource_requirement`, `stream`, `attempts`, `result_ref`, `trace_id`, `timestamps`.

States: `queued`, `reserved`, `running`, `waiting_approval`, `completed`, `failed`, `cancelled`. A worker lease and fencing/attempt identifier prevents a delayed worker from committing a stale result. Cancellation is cooperative and may not interrupt all backends. Deadline should be interpreted as a job policy, not a promise of hard real-time response.

For first delivery use an **in-process worker pool and a memory queue**. Persistence is an opt-in adapter (file/Postgres initially, potentially GSQL later); a saved job does not continue running after the process exits unless an actual worker service exists. A separate scheduler may be preferable if WorkSplicer eventually supplies these facilities, but do not couple `llm` to WorkSplicer or GSQL.

**Capacity modeling:** manage per-model concurrency, local RAM/VRAM budgets, provider rate limits, and tokens per minute as separate constraints. Preemption of an in-flight inference is not assumed; reserve capacity for interactive requests or allow controlled admission. Expose clear queue and execution metrics.

## 5. Streaming, events, and errors

Add streaming only as the smallest new primitive supported by `webclient`/backend adapters. Event forms might include `started`, `text_delta`, `tool_call`, `usage`, `warning`, `completed`, `failed`, `cancelled`. Preserve provider-specific details in an escape-hatch field. Keep canonical completed responses identical whether streamed or not.

A **tool refusal** is a structured result (not a process-killing error). The gBASIC warning mechanism should separately notify observers, with an audit event correlated by run/job/tool-call ID. Transport or provider failures should retain the current `llm` failure contract unless an explicitly new job wrapper converts them into job state. Avoid leaks of prompts, credentials, or private tool outputs in logs by default.

## 6. Security: explicit tool authority

**Decision:** the per-call/per-run tool collection is the authorization boundary. Use `tools.define` and `tools.dispatch`, extending these rather than adding an independent MCP allowlist.

- Only advertised and authorized tools are dispatched. An unregistered or disallowed name returns `{is_error: true, code: "not_authorized", ...}` and emits a gBASIC warning/audit event.
- An attacker can ask for an unknown tool name, including by manipulating returned text. **Never dynamically resolve such names** or evaluate model-supplied gBASIC source.
- Carry a verified **principal/context** into dispatch; the tool collection is a ceiling on capabilities, not a substitute for object-level authorization. Check tenant, user, resource, and parameters at execution time.
- Keep `reads` / `mutates` effects and the existing `agent` approval gate. Approval is bound to exact call ID and arguments; use the existing idempotency key for mutating calls.
- Enforce limits on turns, token/cost budget, tool invocations, output size, and wall time. Tool implementation timeouts and sandboxing remain the host application's responsibility unless suitable primitives already exist.
- Treat retrieved documents, MCP descriptions, model output, and model files as untrusted input. Provenance can inform policy but does not create trust by itself.

**Caution:** if a model is not offered a tool it cannot legitimately call it. A honeypot that appears in the advertised toolset *is* intentionally callable, but must itself be inert and fully synthetic. Attempting an undisclosed tool should always be blocked rather than secretly routed to a decoy.

## 7. Simulated and honeypot tools share one contract

Rather than creating a honeypot mode, allow entries in `tools.define` whose `fn` is a safe simulator or decoy. Add optional helpers, not a new dispatcher:

- `tools.simulated(...)`: an illustrative factory for deterministic fixture-backed responses, delays, failures and capture/replay.
- `tools.decoy(...)`: an illustrative factory for synthetic data and warning/audit hooks; **never reads secrets, writes production state, or exposes realistic sensitive credentials**.
- Both must preserve existing `reads`, `mutates`, and parameter schema contracts. Consider an optional `classification` metadata field (`real`, `simulated`, `decoy`) solely for observability and test rules, not a trust bypass.

A decoy can be advertised as a deliberately tempting but harmless tool. Record the exact invocation and subsequent model behavior, but bound follow-up turns and costs. Security teams must approve deployment-specific decoy descriptions; avoid deceptive responses that could be mistaken by downstream systems for production facts.

Testing benefit: a developer can create the toolset before implementation, prototype an agent, then swap simulated handlers for real functions **without changing the model-facing schema**. Use the already-present offline transport/replay mechanisms for deterministic AI responses.

## 8. Embedded local inference

**Recommendation:** native inference by binding an existing inference library, starting with **llama.cpp** and GGUF models; do not write a new inference engine. This is a distinct execution backend, not a third network wire format.

Two interfaces can coexist:

1. The same inference-level model contract as remote models (`ask`, `chat`, `embed`, stream if supported).
2. An optional native-control API to load/unload a model, choose GPU offload, context length, memory limits and threads.

Explicitly distinguish **model configuration (serializable)** from **live native model/session objects (nonserializable)**. The latter need deterministic cleanup and thread-safety contracts. Prototype: load a small GGUF, complete, stream, cancel, unload, then profile concurrency and GPU memory. Build binding/licensing/package tests on Linux and Windows. Infer only capabilities actually supported by the selected backend (not all models reliably support tool calls, images, embeddings, JSON grammar, or function calling).

Keep Ollama and vLLM service endpoints as first-class choices. vLLM is best treated as a serving endpoint, not an embedded C dependency.

## 9. Model discovery, download, assessment, and benchmarking

Expose capabilities programmatically so gBASIC apps can help users choose models:

- Query locally installed and available models; enumerate runtime-supported formats/features.
- Retrieve a specified model artifact (e.g. pinned Hugging Face GGUF revision/file), verify content integrity, store metadata/license/revision/provenance, cache atomically, support resume where feasible.
- Estimate fit from CPU/RAM/GPU/VRAM and model architecture, weights/quantization, KV cache/context and concurrent sessions. Return an **estimate and uncertainty**, not a guarantee.
- Benchmark throughput, time-to-first-token, peak memory, and representative quality/eval tasks. Label results by exact hardware, runtime options, model revision and prompt shape.
- Offer task-oriented ranking only if it explains its criteria (quality, license, context, latency, privacy, cost, hardware). No opaque universal "best" model.

Start with explicit `inspect`, `ensure`, and `benchmark` operations. Search/recommendation catalog can follow real demand. Model license obligations should integrate with existing `LICENSING.md` and the application packager; automatically including metadata is helpful but does not grant redistribution rights.

## 10. Codex and Claude Code integrations

Treat a coding-agent CLI/SDK as a **separate tool-backed agent adapter**, not as interchangeable raw completion APIs. Only use documented supported authentication, session and automation interfaces, and respect provider subscription/billing terms. A PTY terminal in Studio is a good user interface for these programs, but a PTY is **not** the general-purpose library protocol. Keep terminal UX and structured agent control separate.

## 11. Observability, testing, and acceptance

Reuse `llm.offline`, `llm.replay`, `tools.dispatch`, and `agent.apply`; add integration tests, not just isolated adapters.

Acceptance cases:

1. Existing synchronous examples behave unchanged; no migrations needed.
2. Five concurrent interactive requests and background requests honor capacity limits; no starvation; cancellation and retry are observable.
3. A serialized agent awaiting approval survives restart and resumes exactly once; approval cannot be applied to changed tool arguments.
4. An undeclared tool is blocked and produces both normalized error result and gBASIC warning/audit event; no secret is exposed.
5. The same schema works with simulated, decoy, and real tool implementations.
6. A local GGUF model loads and streams; clean shutdown releases resources; errors do not corrupt the next job.
7. Downloaded models are pinned, verified, cache-reused, and carry provenance/licensing metadata.
8. An application can compare candidate models using measured performance, with hardware/configuration recorded.

Representative vertical slices: (a) SQL explainer, (b) ETL failure investigator using database discovery tools, (c) Studio interactive agent with a background queue, (d) model chooser/benchmark GUI. Ensure examples compile and run before freezing API names.

## 12. Implementation phases and decision gates

**Phase A: API reconciliation and reference tests.** Confirm current code, enumerate real gaps and existing warnings/event hooks, write two compiling examples, preserve compatibility. No speculative parser/runtime changes.

**Phase B: tool policy and simulation.** Enforce per-run toolset authority with explicit principals and warning/audit result; simulate/replay and inert decoy factories. Keep `agent.apply` pure.

**Phase C: asynchronous execution.** Small queue and worker adapter; streaming; cancellation; explicit priority/resource scheduling, in-process first. Measure `webclient` support and current worker plumbing before altering core.

**Phase D: embedded inference spike.** C binding to llama.cpp; memory/performance/license feasibility on Linux and Windows. Continue only if packaging and operational complexity is justified versus local service endpoints.

**Phase E: model management.** Download/cache/inspect, capability reporting, hardware estimation and benchmark. Add recommendations after evaluation criteria exist.

**Phase F: optional persistence/service deployment.** GSQL integration when ready, process restart semantics, durable queue, distributed worker leases only when demanded by an actual app.

**Non-goals:** training/fine-tuning frameworks, building an inference engine from scratch, replacing WorkSplicer scheduling, opaque automated tool privileges, mandatory GSQL, or automatic conversion of CLI subscriptions into API entitlement.

## 13. Questions to settle before implementation

1. Should `inference` be a sibling library, or should queue helpers live under `llm`? Prefer sibling if asynchronous execution creates dependencies or complexity for ordinary `llm` users.
2. What exact gBASIC warning emission API and structured error style should tool rejection use? Verify in source.
3. What are worker, watcher, and streaming primitives available on Linux and Windows today? Benchmark before choosing new native plumbing.
4. Does `tools.define` need a first-class `principal`/policy argument, or can it use the existing principal scope consistently?
5. Should a decoy's classification be published to the model? Usually **no**; the dispatcher/audit knows it, the model-facing schema need not.
6. How do a low-level tool-result refusal and an `agent.apply` approval refusal remain distinct in logs and model-facing transcripts?

## 14. Repository references (reviewed)

- [`stdlib/llm.bas`](https://github.com/Solifugus/gbasic/blob/master/stdlib/llm.bas)
- [`stdlib/tools.bas`](https://github.com/Solifugus/gbasic/blob/master/stdlib/tools.bas)
- [`stdlib/agent.bas`](https://github.com/Solifugus/gbasic/blob/master/stdlib/agent.bas)
- [`docs/llm_design.md`](https://github.com/Solifugus/gbasic/blob/master/docs/llm_design.md)
- [`docs/tools_design.md`](https://github.com/Solifugus/gbasic/blob/master/docs/tools_design.md)
- [`docs/agent_design.md`](https://github.com/Solifugus/gbasic/blob/master/docs/agent_design.md)
- [`docs/gbasic_ai_reference_and_primitives.md`](https://github.com/Solifugus/gbasic/blob/master/docs/gbasic_ai_reference_and_primitives.md)
- [`examples/steward/steward.bas`](https://github.com/Solifugus/gbasic/blob/master/examples/steward/steward.bas)

*Draft intentionally avoids modifying the repository and avoids asserting speculative methods as implemented.*

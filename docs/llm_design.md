# LLM Client Library (`llm.bas`) — Design

Status: **design proposal; nothing built.** A general chat-completion client
for gBASIC. First customer is `mdna.bas` (the analyst panel in
`edgar_design.md`), but the module is general: an LLM call is JSON over
HTTPS, nothing more, so this is **pure gBASIC over `webclient`** — the
compositions rule holds with no core changes expected.

---

## 1. Two wire formats cover the provider space

The stated targets — OpenAI, Anthropic, Ollama, vLLM — reduce to **two
adapters**, because vLLM serves the OpenAI chat-completions format natively
and Ollama exposes an OpenAI-compatible endpoint alongside its own API:

| Format | Reaches |
|---|---|
| `anthropic` (Messages API) | Anthropic |
| `openai` (chat completions) | OpenAI, vLLM, Ollama, and most other hosts (Groq, Together, …) |

Local inference is therefore not a third adapter — it is the `openai` adapter
with a `base_url` pointed at your own machine. An adapter is a small internal
record of functions (build request record, extract response record); adding a
genuinely new wire format later is additive.

---

## 2. Model handles

A model handle is a plain record — inspectable, copyable, tweakable by
ordinary field assignment:

```basic
load llm

m1 = llm.anthropic("claude-sonnet-4-6", key)
m2 = llm.openai("gpt-4o", key)
m3 = llm.local("http://localhost:11434/v1", "llama3.3:70b")   ' Ollama
m4 = llm.local("http://gpubox:8000/v1", "qwen2.5-72b")        ' vLLM

m1.temperature = 0        ' fields: format, base_url, model, key,
m1.max_tokens  = 2000     '         temperature, max_tokens, timeout, retries
```

Key sourcing: the constructor's `key` argument, else the conventional
environment variable (`ANTHROPIC_API_KEY` / `OPENAI_API_KEY`), else error at
first call. Keys are never written to disk by this module. **Possible core
need:** an `env(name)` builtin if gBASIC lacks environment access — a
one-liner, and useful far beyond this library; verified before Phase 1.

---

## 3. Calls

```basic
text = llm.ask(m, system, prompt)          ' the 90% case -> string

r = llm.chat(m, system, messages)          ' full form
' messages: [ {role:"user", content:"..."}, {role:"assistant", ...}, ... ]
' r: { text, usage: {input, output}, stop_reason, model, raw }
```

- Conversation state is the caller's message list — the module is stateless,
  matching how the API actually works and how the project already thinks
  about context (`anthropic_api` patterns): resend history each call.
- `usage` is always populated when the provider reports it, so cost tracking
  is a fold over response records — no hidden ledger in the module.
- `raw` carries the decoded provider response for anything the common shape
  omits; the common shape is the contract, `raw` is the escape hatch.

### Structured output

```basic
rec = llm.ask_json(m, system, prompt)      ' -> record/list, or unknown
```

Owns the sloppiness handling (this settles the open question recorded in
`edgar_design.md`): strip markdown fences, attempt `decode`; on failure, issue
**one** corrective retry ("your previous reply was not valid JSON; reply with
only the JSON"), then return `unknown`. Local models are the sloppy ones and
also the cheap ones, so one retry is proportionate. Callers wanting schema
*validation* check fields themselves — `unknown`-on-missing-field is already
idiomatic via dynamic record reads.

---

## 4. Failure policy

Two failure classes, handled differently on principle:

- **Transport / provider failures raise** (structured error, source `llm`):
  connection refused, timeout, auth rejection, and 429/5xx *after* the retry
  budget. Retries: automatic on 429 and 5xx with exponential backoff
  (1s, 2s, 4s…, capped, honoring `Retry-After` when present), budget from
  `m.retries` (default 3). These are environment problems; `on error` is the
  right tool.
- **Model-output problems return `unknown`** (bad JSON after the corrective
  retry, empty completion). These are data problems; NA-policy is the right
  tool, consistent with the statistics library.

---

## 5. Economics note (why `local` is a first-class constructor)

Panel-style workloads (N analysts × many filings) are volume work. The
intended shape — and the reason `mdna.bas` takes a model handle *per
panelist* — is free local models (Ollama/vLLM on the 3090) for the volume
passes and a paid frontier model as the referee. The module makes that a
configuration choice, not an architecture choice.

---

## 6. Non-goals (v1)

- **Token streaming** — analysis workloads read complete responses; also
  depends on `webclient` streaming support. Revisit if an interactive
  customer appears.
- ~~**Tool use / function calling**~~ — **SHIPPED (NAP-13)**, see §9.
- **Images / documents in requests** — deferred.
- **Embeddings** — deliberately *near*-scope: `llm.embed(m, text) -> list`
  is a natural v2 and immediately useful for filing-similarity work
  (risk-factor drift as cosine distance rather than string diff). Flagged
  for the finance library's later phases.
- Prompt caching, batch APIs, provider-side JSON mode flags — later
  optimizations behind the same call surface.

---

## 7. Open questions

1. Whether `ask_json`'s corrective retry should count against `m.retries`
   or be its own single-shot budget (leaning: separate, always exactly one).
2. Timeout default — panels over large MD&A sections on local hardware can
   legitimately run minutes; proposal: 120s default, per-handle field.
3. Whether provider-side structured-output modes (Anthropic/OpenAI JSON
   modes) should be used when available under the same `ask_json` surface
   (leaning: yes, transparently — the contract is the returned record, not
   the mechanism).

---

## 8. Roadmap

Testing note: unit goldens run against **recorded fixtures** (request records
in, canned responses out — the adapter layer is pure functions over records,
so it tests without a network). One live smoke script per provider format is
kept in `examples/` and run manually, never in CI.

### Phase 1 — Core
Both adapters; `anthropic`/`openai`/`local` constructors; `ask`/`chat`;
usage extraction; failure policy + backoff. Verified against fixture goldens
plus a manual live smoke on Ollama (free) and Anthropic.

### Phase 2 — Structured output
`ask_json` with fence-stripping and the corrective retry; `mdna.bas` verdict
decoding is the acceptance case.

### Phase 3 — (as earned)
Embeddings; provider JSON modes; streaming if a customer appears; then
graduation of anything the finance panel proves generally useful (e.g. a
generic N-models-one-referee helper, *if* a second panel-shaped customer
ever exists — until then it stays in `mdna.bas`).

---

## 9. Tool / function calling (NAP-13, shipped)

A tool is a gBASIC function the model may ask you to run. Both wire formats are
normalized to one internal shape, so programs never see provider field names.

### Public API

| Call | Purpose |
| --- | --- |
| `llm.tool(name, description, schema, fn)` | Build a tool definition (validates). |
| `llm.tool_error(message)` | Build the failure record a tool returns. |
| `llm.with_tools(m, tools)` | Attach the registry; raises on duplicate/invalid. |
| `llm.with_max_tool_rounds(m, n)` | Cap tool-executing rounds (default 8). |
| `llm.run_tools(m, system, messages)` | Automatic loop → final response. |
| `llm.tool_calls(response)` | Normalized calls on a response (possibly `[]`). |
| `llm.execute_tools(m, calls)` | Run calls → normalized results. |
| `llm.append_tool_results(m, messages, response, results)` | Next message list. |

`chat`/`ask`/`ask_json` are unchanged; `chat` simply gains a `tool_calls` field.
Use `run_tools` for the automatic loop, or `chat` + the last three calls to drive
it manually.

### Normalized representation

```
tool definition : { name, description, schema, fn }
tool call       : { id, name, arguments, error }
tool result     : { id, name, content, is_error }
```

`run_tools` returns the final response plus `rounds` and `messages` (the full
transcript, including tool turns).

### Callable signature

A tool takes exactly ONE argument — a record of decoded arguments:

```basic
function get_customer(args)
    return { id: args.id, name: lookup(args.id) }
end function
```

### Tools must be TOTAL

A tool must RETURN, never `error`. gBASIC's `on error resume next` unwinds to the
top program frame, so a library cannot catch a raise from a function it calls — a
raising tool aborts the program and the loop cannot contain it. Report failure by
returning `llm.tool_error("...")` (a constructor, because `error` is a reserved
word and `{ error: ... }` will not parse). Everything the library *can* check —
arguments, required fields, result serializability — is pre-validated before the
call, matching the `_json_valid`-before-`decode` rule used by `ask_json`.

### Schema

A JSON-Schema-subset record passed through to the provider:

```basic
{ type: "object", properties: { id: { type: "integer" } }, required: ["id"] }
```

Locally checked: `required` presence, and property types `string`, `number`,
`integer`, `boolean`, `array`, `object`. Anything else is accepted unchecked —
this is a practical guard, not a complete JSON Schema validator (the provider
validates too).

### Security

The registry is the sole authority over what is callable. A call naming an
unregistered tool yields a controlled error result; arguments are parsed as JSON
into records and passed as VALUES — never evaluated as gBASIC source, and no
dynamic name lookup occurs. Prose that merely names a tool is not dispatched.

### Provider mapping

| Normalized | anthropic | openai |
| --- | --- | --- |
| definition | `tools[].input_schema` | `tools[].function.parameters` |
| call | `content[].type="tool_use"` (`id`,`name`,`input`) | `message.tool_calls[]` (`id`, `function.arguments` — a JSON *string*) |
| result | user msg, `tool_result` block w/ `tool_use_id`, `is_error` | `{role:"tool", tool_call_id, content}` |

Ids and order are preserved exactly; multiple calls in one turn are executed
sequentially.

### Errors

| Class | Behavior |
| --- | --- |
| network/provider, retries exhausted | raises (existing §4 policy) |
| unknown tool | controlled `is_error` result |
| malformed tool arguments | controlled `is_error` result |
| invalid arguments (missing/typed) | controlled `is_error` result, tool NOT called |
| tool failure (`llm.tool_error`) | controlled `is_error` result |
| unserializable tool result | controlled `is_error` result, pre-checked |
| max tool rounds exceeded | raises |
| tool that RAISES | aborts the program — not containable (see above) |

### JSON on the wire — RESOLVED

The whole wire path now uses **`json_encode`** (strict RFC 8259), never `encode`
(gBASIC's dialect, which spells the empty values `nothing`/`unknown` and is not
valid JSON — see "Three serializers, three jobs" in `docs/reference.md`).

`nothing` therefore serializes as `null` on its own, so the openai assistant
tool-call turn carries `"content": null` exactly as the provider sent it, rather
than having the field dropped to dodge the dialect. The earlier `_json_safe`
workaround is gone. What remains is `_drop_unknown`, whose narrower job is to
remove `unknown` — gBASIC's NA, the one value strict JSON genuinely cannot
express, and which reaches the message path only from `_field` on a key the
provider omitted, so dropping the field is the faithful reading. Tool results are
gated by `json_encodable` and fall back to `null`.

This is PREFLIGHT, not catch: `json_encode` raises, and `on error resume next`
unwinds past a library frame, so a raise here could not be contained.

Every request body the tool path emits is parsed by an independent JSON parser in
`tests/run_json_strict.sh` — gBASIC's own `decode` accepts the dialect and so
cannot prove standards compliance.

### Async / cancellation

Calls BLOCK, exactly as before NAP-13 — the tool loop adds rounds, not
concurrency. `llm.bas` creates no threads and no actors. A native GTK application
that must stay responsive should run the conversation the way it already runs any
blocking work (the established actor / main-loop architecture); NAP-13 does not
change that and does not add UI orchestration. There is no cancellation token:
the existing surface has none, and the bounded round cap plus the injectable
transport are the control points. A transport that raises stops the loop
immediately.

---

End of LLM client library design.

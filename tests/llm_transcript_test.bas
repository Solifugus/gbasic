' `llm`: the canonical part-based transcript and keyed replay.
' Self-checking; run by tests/run_llm_transcript.sh.
'
' Step 4 of docs/gbasic_ai_reference_and_primitives.md, items 3 and 4 of its
' Part 2.
'
' A MESSAGE IS NOT { role, content: string }. After a tool call the assistant
' turn carries text AND tool-call parts, and the reply carries tool-result
' parts. Providers shape those differently -- Anthropic nests blocks inside a
' user turn, OpenAI gives tool results their own `tool` role -- so a transcript
' re-sent in the wrong shape is REJECTED. The canonical shape exists so an
' agent can build a transcript without knowing which provider it will meet.
'
' THE LOAD-BEARING TIER IS THAT ONE TRANSCRIPT PRODUCES TWO DIFFERENT WIRE
' SHAPES. Asserting that each translation is "valid" is satisfied by a
' translator that emits the same thing twice, which is precisely the bug -- one
' of the two providers would reject it. So the fixture asserts the shapes DIFFER
' as well as what each one is.
'
' AND KEYED REPLAY IS A DIFFERENT THING FROM `llm.offline`, not a better one.
' `offline` returns one fixed file for EVERY request, so it can test a single
' turn and cannot replay a conversation: every turn gets the same answer. That
' is asserted here as the control -- two turns through `offline` get the same
' answer, two turns through `replay` get their own.
'
' SELF-CHECKING, and forced: every defect here is a PLAUSIBLE TRANSCRIPT or a
' plausible answer, and a golden would record the wrong provider shape as
' expected and defend it.

load llm

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function nosleep(s)
    return nothing
end function

anth = llm.anthropic("claude-test", "k")
oai = llm.openai("gpt-test", "k")

' ---- the canonical shape ---------------------------------------------------
turns = [
    llm.message("user", [ llm.text("what is the balance for 7?") ]),
    llm.message("assistant", [ llm.text("Looking that up."),
                               llm.tool_call("call_1", "get_balance", { id: 7 }) ]),
    llm.message("user", [ llm.tool_result("call_1", "42.00", false) ])
]

check("a text part knows its kind", llm.text("x").kind, "text")
check("a tool call carries its id", llm.tool_call("c", "n", {}).id, "c")
check("a tool result carries its content", llm.tool_result("c", "v", false).content, "v")
check("text_of joins the text parts, ignoring the rest",
      llm.text_of(turns[1]), "Looking that up.")
check("parts_of reads a canonical message", count(llm.parts_of(turns[1])), 2)
check("and is tolerant of a legacy one",
      count(llm.parts_of({ role: "user", content: "plain" })), 1)
check("reading it as a text part",
      llm.text_of({ role: "user", content: "plain" }), "plain")

' ---- one transcript, two providers ----------------------------------------
wa = llm.to_wire(anth, turns)
wo = llm.to_wire(oai, turns)

check("anthropic keeps one wire message per canonical turn", count(wa), 3)
check("anthropic nests blocks", count(wa[1].content), 2)
check("naming the tool call a tool_use", wa[1].content[1].type, "tool_use")
check("and the result a tool_result inside a USER turn", wa[2].content[0].type, "tool_result")
check("keyed by tool_use_id", wa[2].content[0]["tool_use_id"], "call_1")

check("openai puts tool calls on the assistant message",
      count(wo[1]["tool_calls"]), 1)
check("with the arguments as a JSON STRING, which anthropic does not",
      is_string(wo[1]["tool_calls"][0]["function"]["arguments"]), true)
check("and gives the result its own `tool` role message", wo[2].role, "tool")
check("keyed by tool_call_id", wo[2]["tool_call_id"], "call_1")

' THE DIFFERENCE. A translator that emitted one shape for both would satisfy
' every check above that names only one provider.
check("THE TWO WIRE SHAPES DIFFER", encode(wa) != encode(wo), true)

' A provider-shaped message passes through untouched, so an existing program
' keeps working and a transcript can be migrated one message at a time.
legacy = llm.to_wire(anth, [ { role: "user", content: "plain" } ])
check("a legacy message passes through", legacy[0].content, "plain")

' ---- raw is kept, so a turn can be echoed back verbatim --------------------
' An assistant turn must be re-sent exactly or tool-call ids stop lining up.
block = { type: "text", text: "from the provider", provider_only: "keep me" }
withraw = { role: "assistant", parts: [ { kind: "text", text: "from the provider", raw: block } ] }
out = llm.to_wire(anth, [ withraw ])
check("a part with `raw` is re-sent as the provider's own block",
      out[0].content[0]["provider_only"], "keep me")

' ---- fingerprint -----------------------------------------------------------
' The key is a CANONICAL rendering with sorted keys, so it cannot depend on the
' order a caller happened to build a record in.
req_a = { model: "x", system: "s", messages: [ { role: "user", content: "hi" } ] }
req_b = { messages: [ { content: "hi", role: "user" } ], system: "s", model: "x" }
check("the same request written two ways has ONE key",
      llm.fingerprint(anth, req_a), llm.fingerprint(anth, req_b))
req_c = { model: "x", system: "s", messages: [ { role: "user", content: "different" } ] }
check("a different request has a different key",
      llm.fingerprint(anth, req_a) != llm.fingerprint(anth, req_c), true)

' The first failure the design names: a system prompt carrying today's date
' never matches a recorded key.
vol = llm.with_volatile(anth, ["system"])
d1 = { model: "x", system: "today is 2026-09-07", messages: [ { role: "user", content: "hi" } ] }
d2 = { model: "x", system: "today is 2199-01-01", messages: [ { role: "user", content: "hi" } ] }
check("a declared volatile field does not change the key",
      llm.fingerprint(vol, d1), llm.fingerprint(vol, d2))
check("and WITHOUT the declaration the same two differ -- so it is the "
      + "declaration doing the work, not the hash ignoring things",
      llm.fingerprint(anth, d1) != llm.fingerprint(anth, d2), true)
check("a volatile path descends into arrays too",
      llm.fingerprint(llm.with_volatile(anth, ["messages.0.content"]), req_a),
      llm.fingerprint(llm.with_volatile(anth, ["messages.0.content"]), req_c))

' The canonical algorithm is PINNED, because the committed replay fixtures are
' named by it. Without this, changing the rendering makes every fixture fail
' with "no recorded response" -- true, and pointing at the wrong thing.
known = { model: "claude-test", system: "anything",
          messages: [ llm.message("user", [ llm.text("hello") ]) ], tools: unknown }
check("the canonical rendering is pinned", llm.fingerprint(vol, known), "da72956e")

' ---- refusals, each beside its nearest legal neighbour ---------------------
on error goto next

x = llm.message("user", "not an array")
if error then
    check("a message needs an array of parts",
          contains(error.message, "needs an array of parts"), true)
    error.clear()
end if

x = llm.message("user", [ { kind: "shout", text: "hi" } ])
if error then
    check("an unknown part kind is refused by name",
          contains(error.message, "unknown part kind 'shout'"), true)
    error.clear()
end if

x = llm.message("user", [ { text: "no kind" } ])
if error then
    check("a part with no kind is refused",
          contains(error.message, "has no kind"), true)
    error.clear()
end if

x = llm.with_volatile(anth, "system")
if error then
    check("volatile paths must be an array",
          contains(error.message, "expects an array of dotted paths"), true)
    error.clear()
end if

ok = llm.message("user", [ llm.text("fine") ])
check("the control: a well-formed message is accepted", ok.role, "user")
ok2 = llm.with_volatile(anth, ["system"])
check("and a well-formed volatile list is", count(ok2.volatile), 1)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

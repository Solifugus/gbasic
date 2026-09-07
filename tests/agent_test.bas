' `agent` -- a conversation in progress is a VALUE, and the loop is a pure step.
' Self-checking; run by tests/run_agent.sh.
'
' docs/gbasic_ai_reference_and_primitives.md §1.8, step 5. The naive design is
' a loop: send the transcript, take the reply, dispatch a tool call, re-send.
' It cannot be written that way because of the APPROVAL GATE -- a person has to
' say yes, they may take a minute, the wait spans HTTP requests, and the server
' is single-threaded. Nothing may block.
'
' THE LOAD-BEARING TIER IS STORAGE. A run must survive `encode`/`decode` and
' behave identically afterwards, because that is the only thing that makes an
' approval arriving ten minutes later on a different request possible. It is
' asserted as a DIFFERENCE between two runs -- the original and the one that
' went through text -- because "the run encodes" alone is satisfied by a run
' that encodes and then steps differently.
'
' SELF-CHECKING, and forced: every defect here is a PLAUSIBLE CONVERSATION. A
' mutating tool dispatched without asking, an approval that resumes the wrong
' call, a transcript one turn short -- each produces a run that looks entirely
' ordinary, and a golden would record it as expected.

load agent
load tools
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

function freeze_card(args)
    return "frozen " + args.id
end function
function get_balance(args)
    return "42.00"
end function

ts = tools.define("bank", [
    { name: "get_balance", describe: "read a balance",
      params: [ { name: "id", type: "string", describe: "account", required: true } ],
      reads: ["accounts"], mutates: [], fn: get_balance },
    { name: "freeze_card", describe: "freeze a card",
      params: [ { name: "id", type: "string", describe: "card", required: true } ],
      reads: [], mutates: ["cards"], fn: freeze_card }
])

function kinds(actions)
    out = []
    for each a in actions
        append(out, a.kind)
    end for
    return join(out, ",")
end function

' ---- the run is data, and that is checked before anything else -------------
' A toolset holds FUNCTION VALUES and `encode` refuses them, so the run keeps
' `tools.schema(ts)` instead. Without that the run could not be stored, which
' is the whole design.
r0 = agent.begin({ user: "alice" }, "You are a bank assistant.", ts)
check("a fresh run is idle", r0.stage, "idle")
check("it publishes the toolset as DATA, not the toolset", count(r0.tools), 2)
check("carrying what each tool changes, which is what approval needs",
      count(r0.tools[1].mutates), 1)
check("expires_at is a NUMBER, because encode refuses a datetime",
      is_number(r0.expires_at), true)

on error goto next
enc = encode(r0)
if error then
    check("A RUN MUST ENCODE -- it did not: " + error.message, false, true)
    error.clear()
else
    check("a fresh run encodes", len(enc) > 0, true)
end if

x = encode({ ts: ts })
if error then
    check("...and a toolset does NOT, which is why the run holds the schema",
          contains(error.message, "encode supports"), true)
    error.clear()
end if

' ---- a read-only call is dispatched; a mutating one is not -----------------
' The difference is the whole approval gate. Asserting only the second is
' satisfied by a run that asks permission for everything, which no one would use.
s1 = agent.apply(r0, agent.user_event("what is my balance?"))
check("a user turn asks the model", kinds(s1.actions), "start_model")
check("and the run is waiting for it", s1.run.stage, "calling_model")

read_reply = llm.message("assistant", [ llm.text("Checking."),
                                        llm.tool_call("c1", "get_balance", { id: "a1" }) ])
s2 = agent.apply(s1.run, agent.model_event(read_reply, { input: 10, output: 4 }))
check("a READ-ONLY tool is dispatched without asking",
      kinds(s2.actions), "emit_text,dispatch_tool")
check("and the run waits for the tool, not for a person", s2.run.stage, "awaiting_tool")
check("the idempotency key names the run and the call",
      s2.actions[1].idempotency, agent.idempotency_key(s2.run, "c1"))

s3 = agent.apply(s2.run, agent.tool_result_event("c1", { content: "42.00", is_error: false }))
check("the result resumes the model", kinds(s3.actions), "start_model")
check("having appended the assistant turn and the results turn",
      count(s3.run.transcript), 3)

mutate_reply = llm.message("assistant", [ llm.tool_call("c2", "freeze_card", { id: "card9" }) ])
s4 = agent.apply(s3.run, agent.model_event(mutate_reply, { input: 20, output: 6 }))
check("A MUTATING TOOL ASKS FIRST", kinds(s4.actions), "ask_approval")
check("and nothing is dispatched", contains(kinds(s4.actions), "dispatch_tool"), false)
check("the run is waiting for a person", s4.run.stage, "awaiting_approval")

' ---- THE STORAGE TIER ------------------------------------------------------
' The approval arrives later, on another request, in another process. So the
' run goes through text and must behave identically. Asserted as a DIFFERENCE
' between the two, because "it encodes" is satisfied by a run that encodes and
' then steps differently.
stored = encode(s4.run)
revived = decode(stored)
check("a run awaiting approval encodes", len(stored) > 0, true)
direct = agent.apply(s4.run, agent.approval_event("c2", true))
resumed = agent.apply(revived, agent.approval_event("c2", true))
check("STEPPING THE REVIVED RUN GIVES THE SAME ACTIONS",
      kinds(resumed.actions), kinds(direct.actions))
check("the same stage", resumed.run.stage, direct.run.stage)
check("and the same idempotency key, so a replayed effect is recognised",
      resumed.actions[0].idempotency, direct.actions[0].idempotency)
check("which is dispatch, now that a person said yes",
      kinds(direct.actions), "dispatch_tool")

' ---- purity ----------------------------------------------------------------
' No I/O, no clock, no generated id. The same (run, event) twice must give the
' same answer, or the run could not be resumed at all.
again = agent.apply(s4.run, agent.approval_event("c2", true))
check("the same step twice gives the same actions",
      kinds(again.actions), kinds(direct.actions))
check("and does not mutate the run it was given", s4.run.stage, "awaiting_approval")

' ---- a refusal is an ANSWER, not an error ----------------------------------
denied = agent.apply(s4.run, agent.approval_event("c2", false))
check("a declined call resumes the model rather than failing",
      kinds(denied.actions), "start_model")
' The declination reaches the model as a TOOL RESULT part, not as text --
' which is the shape a model reads as "that call did not happen", rather than
' as the assistant having said something.
last_turn = denied.run.transcript[count(denied.run.transcript) - 1]
declined_text = ""
for each p in llm.parts_of(last_turn)
    if p.kind = "tool_result" then
        declined_text = declined_text + string(p.content)
    end if
end for
check("the model is told, as a tool result, that a person declined",
      contains(declined_text, "declined"), true)
check("naming who", contains(declined_text, "alice"), true)
check("and it is marked an error, so the model does not read it as success",
      llm.parts_of(last_turn)[0].is_error, true)

' ---- a context may turn the gate off ---------------------------------------
' The default is to require approval, because the safe default is the one you
' have to switch off deliberately. This is the control that says the gate is a
' decision and not a hard rule.
open_run = agent.begin({ user: "bob", approve_mutations: false }, "sys", ts)
o1 = agent.apply(open_run, agent.user_event("freeze it"))
o2 = agent.apply(o1.run, agent.model_event(mutate_reply, unknown))
check("with the gate off, a mutating tool dispatches", kinds(o2.actions), "dispatch_tool")
check("and the same tool ASKS when the gate is on -- so it is the context deciding",
      kinds(s4.actions), "ask_approval")

' ---- finishing, cancelling, expiring ---------------------------------------
done_reply = llm.message("assistant", [ llm.text("All set.") ])
f1 = agent.apply(s3.run, agent.model_event(done_reply, unknown))
check("a reply with no tool call finishes", kinds(f1.actions), "emit_text,finish")
check("and the run is done", f1.run.stage, "done")

c1 = agent.apply(s4.run, agent.cancel_event("user left"))
check("a cancel finishes from any stage", kinds(c1.actions), "finish")
check("clearing what was outstanding", count(c1.run.pending), 0)

check("a run is not expired before its time", agent.is_expired(r0, r0.expires_at - 1), false)
check("and is after it", agent.is_expired(r0, r0.expires_at + 1), true)

' ---- usage accumulates -----------------------------------------------------
check("usage adds up across turns", s3.run.usage.input, 10)
check("both directions", s3.run.usage.output, 4)

' ---- refusals --------------------------------------------------------------
y = agent.apply(f1.run, agent.user_event("more?"))
if error then
    check("a finished run refuses another step",
          contains(error.message, "this run is finished"), true)
    error.clear()
end if

y = agent.apply(s4.run, agent.tool_result_event("nosuch", { content: "x" }))
if error then
    check("a result for a call nobody is waiting on is refused",
          contains(error.message, "no call 'nosuch' is outstanding"), true)
    error.clear()
end if

y = agent.apply(s2.run, agent.approval_event("nosuch", true))
if error then
    check("so is an approval for one", contains(error.message, "no call 'nosuch'"), true)
    error.clear()
end if

y = agent.apply(s1.run, agent.tool_result_event("c1", { content: "x" }))
if error then
    check("a tool result while calling the model is refused",
          contains(error.message, "no call 'c1' is outstanding"), true)
    error.clear()
end if

y = agent.apply(r0, { kind: "shout" })
if error then
    check("an unknown event kind is refused by name",
          contains(error.message, "unknown event kind 'shout'"), true)
    error.clear()
end if

y = agent.model_event("not a message", unknown)
if error then
    check("a model event needs a canonical message",
          contains(error.message, "canonical assistant message"), true)
    error.clear()
end if

y = agent.begin("not a record", "sys", ts)
if error then
    check("begin needs a context record", contains(error.message, "context record"), true)
    error.clear()
end if

ok = agent.begin({ user: "c" }, "sys", unknown)
check("the control: a run with no toolset is legal and publishes none",
      count(ok.tools), 0)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

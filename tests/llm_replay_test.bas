' Keyed replay end to end: the two failures the design names, each as a
' DIFFERENCE, plus the control that says this is a different thing from
' `llm.offline` rather than a better one.
'
' Self-checking; run by tests/run_llm_transcript.sh.

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

base = llm.with_volatile(llm.anthropic("claude-test", "k"), ["system"])
m = llm.replay(base, "tests/llm/replay")

t1 = [ llm.message("user", [ llm.text("hello") ]) ]
t2 = [ llm.message("user", [ llm.text("hello") ]),
       llm.message("assistant", [ llm.text("Hi there.") ]),
       llm.message("user", [ llm.text("and again") ]) ]

' ---- a conversation replays, turn by turn ---------------------------------
r1 = llm.chat(m, "you are helpful, today is 2026-09-07", t1)
r2 = llm.chat(m, "you are helpful, today is 2026-09-07", t2)
check("turn one replays its own recording", r1.text, "Hi there.")
check("turn two replays a DIFFERENT one", r2.text, "Second answer.")
check("which is the whole point: two turns, two answers", r1.text != r2.text, true)

' THE CONTROL. `llm.offline` returns one fixed file for every request, so the
' same two turns get the same answer -- which is why it cannot replay a
' conversation, and why keyed replay is a different thing and not a refinement.
off = llm.offline(base, "tests/llm/offline")
o1 = llm.chat(off, "you are helpful", t1)
o2 = llm.chat(off, "you are helpful", t2)
check("offline gives turn one the one fixed answer", o1.text, "the one fixed answer")
check("and gives turn two THE SAME ANSWER", o1.text = o2.text, true)

' ---- failure 1: a volatile system prompt still matches --------------------
later = llm.chat(m, "you are helpful, today is 2199-01-01", t1)
check("a year later the same turn still finds its recording", later.text, "Hi there.")

' The control for it: a change OUTSIDE the volatile path must NOT match, or
' "volatile" would just mean "the key ignores things".
on error goto next
miss = llm.chat(m, "you are helpful, today is 2026-09-07",
                [ llm.message("user", [ llm.text("a question nobody recorded") ]) ])
if error then
    check("but an unrecorded request is refused, naming the key it wanted",
          contains(error.message, "no recorded response"), true)
    error.clear()
end if

' ---- failure 2: a retry replays the SECOND recording ----------------------
' Attempt 0 is a 429 and attempt 1 succeeds. Without occurrence indexing the
' retry would replay the 429 forever and retry logic would be untestable.
rt = llm.with_sleep(llm.replay(base, "tests/llm/retry"), nosleep)
r3 = llm.chat(rt, "anything", t1)
check("after a 429 the retry replays the NEXT recording", r3.text, "the retry answer")
check("which is not what attempt 0 held", r3.text != "Hi there.", true)

' ---- the collision guard ---------------------------------------------------
' A 32-bit key can collide, and a collision would silently serve another
' request's answer. The fixture records the canonical text it was made from
' and replay refuses when it does not match.
bad = llm.replay(base, "tests/llm/collide")
x = llm.chat(bad, "anything", t1)
if error then
    check("a fixture recorded for a different request is refused",
          contains(error.message, "recorded for a different request"), true)
    error.clear()
end if

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

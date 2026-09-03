' automation.execute / would / rehearsal / observe
' (docs/automation_recipe_06_should_we_act.md, design §5, R5, R6, R7).
'
' The layer that can change external state, so this is the fixture where a
' defect does not produce a wrong number -- it produces something HAPPENING
' that should not have. That is why two tiers here assert an ABSENCE, and why
' both of them prove the absence with a side effect on disk rather than by
' trusting a return value.
'
' THE LOAD-BEARING TIER IS THE SHARED GATE. `would` and `execute` must agree on
' every case, because R5's whole argument is that a rehearsal tells you what
' the live run will do. If the dry run and the live path could disagree, the
' rehearsal would be a statement about a different program.

load automation
load reasoning

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

' The executor leaves a mark on disk, so "nothing happened" is checkable
' rather than assumed.
marker = "tests/.automation_marker"

function mark_file()
    f {file}= "tests/.automation_marker"
    return f
end function

function acted()
    return exists(mark_file())
end function

function clear_mark()
    if exists(mark_file()) then
        delete(mark_file())
    end if
    return nothing
end function

function executor(dec)
    write(mark_file(), "executed: " + dec.recommendation)
    return { ok: true, note: "did " + dec.recommendation }
end function

' --- the values under test -------------------------------------------------

function decide(needs_authority)
    return reasoning.decision({
        objective: { measure: "revenue", direction: "maximize" },
        alternatives: [], recommendation: "restock",
        expected_value: 7497, authority_required: needs_authority,
        authority_reason: "cost 15000 exceeds the spend limit of 5000",
        sized_off: { quantity: "leading_cell", established: true, value: 0 - 16835 },
        provenance: { method: "m", rows: 1, parameters: { }, assumptions: [] } })
end function

cheap = decide(false)
dear = decide(true)

ctx = { authority: { spend_limit: 5000, min_rehearsal_periods: 12 } }
approved = { authority: { spend_limit: 5000, min_rehearsal_periods: 12 },
             approval: { by: "regional director", at: "2026-09-02" } }

full = automation.rehearsal({ periods: 12, fired: 1, false_alarms: 0, missed: 0 })
short = automation.rehearsal({ periods: 3, fired: 1, false_alarms: 0 })

' --- TIER: R5, the simulation gate ----------------------------------------
clear_mark()
on error goto next
x = automation.execute(cheap, ctx, unknown, executor)
check("an unrehearsed process may not act",
      contains(error.message, "never been replayed against history"), true)
error.clear()
check("  and nothing happened", acted(), false)

x = automation.execute(cheap, ctx, short, executor)
check("a process rehearsed over too little history may not act",
      contains(error.message, "short of the 12 this authority requires"), true)
error.clear()
check("  and nothing happened", acted(), false)

' R5 IS CHECKED BEFORE AUTHORITY, deliberately: an unrehearsed process must not
' be executable even WITH a human's approval, because the human has nothing to
' approve on -- nobody knows how often it fires or how often it is wrong.
x = automation.execute(dear, approved, unknown, executor)
check("approval does not substitute for a rehearsal",
      contains(error.message, "never been replayed"), true)
error.clear()
check("  and nothing happened", acted(), false)
on error stop

' THE CONTROL. Without it every check above is satisfied by an `execute` that
' refuses everything.
act1 = automation.execute(cheap, ctx, full, executor)
check("a rehearsed, within-authority decision DOES act", acted(), true)
check("  and the action records its result", act1.result.ok, true)
check("  and what authorised it", act1.authority.needed, false)
check("  and the rehearsal it rested on", act1.rehearsal.periods, 12)

' --- TIER: R6, the ENFORCEMENT half ---------------------------------------
' `decision` states what a recommendation needs; this is where it is spent.
' Until this layer existed only the stating half had ever been tested.
clear_mark()
on error goto next
x = automation.execute(dear, ctx, full, executor)
check("a decision beyond delegated authority may not act unattended",
      contains(error.message, "no approval is on file"), true)
error.clear()
check("  and nothing happened", acted(), false)
on error stop

act2 = automation.execute(dear, approved, full, executor)
check("the same decision acts once approved", acted(), true)
check("  and the action names who approved it",
      act2.authority.granted_by, "regional director")
check("  and that it needed approval at all", act2.authority.needed, true)

' --- TIER: THE SHARED GATE ------------------------------------------------
' R5's argument is that a rehearsal tells you what the live run will do. If the
' dry run and the live path could disagree, the rehearsal would be about a
' different program. So they must agree on EVERY case -- checked by running
' both over the same matrix and comparing.
cases = [{ name: "cheap/unrehearsed", d: cheap, c: ctx, r: unknown },
         { name: "cheap/short", d: cheap, c: ctx, r: short },
         { name: "cheap/full", d: cheap, c: ctx, r: full },
         { name: "dear/full/unapproved", d: dear, c: ctx, r: full },
         { name: "dear/full/approved", d: dear, c: approved, r: full },
         { name: "dear/short/approved", d: dear, c: approved, r: short }]
agreed = 0
allowed_n = 0
for each cs in cases
    w = automation.would(cs.d, cs.c, cs.r)
    clear_mark()
    on error goto next
    x = automation.execute(cs.d, cs.c, cs.r, executor)
    live = not error
    if error then
        error.clear()
    end if
    on error stop
    if w.would_act = live then
        agreed = agreed + 1
    end if
    if live then
        allowed_n = allowed_n + 1
    end if
next
check("the dry run and the live path agree on every case", agreed, count(cases))
' And the matrix is not trivially all-one-way, or agreement proves nothing.
check("  with the matrix landing both ways", allowed_n > 0 and allowed_n < count(cases), true)

' `would` cannot act BY CONSTRUCTION -- it takes no executor at all, so the
' safety property is structural rather than a flag somebody must remember.
clear_mark()
w = automation.would(cheap, ctx, full)
check("a dry run that WOULD act still does not act", acted(), false)
check("  though it says it would", w.would_act, true)

' --- TIER: R7, an unmeasured outcome is not evidence ----------------------
clear_mark()
act3 = automation.execute(cheap, ctx, full, executor)
on error goto next
x = reasoning.as_evidence(act3)
check("an action with no measured outcome is not evidence",
      contains(error.message, "it is a memory"), true)
error.clear()
on error stop

' R10, AND IT IS THE EXPENSIVE ONE. A measured outcome with NO COMPARISON is
' evidence that something happened next -- not that the action worked. An
' action is taken precisely when a measure is EXTREME, and extremes revert on
' their own: recipe 7 measured a 52% apparent "recovery" from an intervention
' whose true effect was exactly zero.
uncontrolled = automation.observe(act3, { value: 9000, at: "2026-10-01" })
on error goto next
x = reasoning.as_evidence(uncontrolled)
check("a measured but UNCONTROLLED outcome is still not evidence of an effect",
      contains(error.message, "only that something happened next"), true)
error.clear()
on error stop
' It may still be read -- honestly labelled, in the value rather than in a
' comment somebody may not read.
obs = reasoning.as_observation(uncontrolled)
check("  it may be read as an observation", obs.kind, "prior_observation")
check("  which says so about itself", obs.uncontrolled, true)
check("  and carries the caveat", contains(obs.caveat, "not the effect of acting"), true)

' THE CONTROL: with a holdout, it IS evidence, and the effect is the difference.
observed = automation.observe(act3, { value: 9000, holdout: 6000, at: "2026-10-01" })
ev = reasoning.as_evidence(observed)
check("with a holdout, it is evidence", ev.kind, "prior_action")
check("  and carries what was expected", ev.expected, 7497)
check("  and what was observed", ev.observed, 9000)
check("  and what the untreated did", ev.holdout, 6000)
check("  so the EFFECT is the difference, not the observation", ev.effect, 3000)
check("  and whether it was met", ev.met, true)
worse = automation.observe(act3, { value: 100, holdout: 50, at: "2026-10-01" })
check("an action that underperformed says so", worse.outcome.met, false)
check("  and an effect can be small even when the observation is large",
      automation.observe(act3, { value: 9000, holdout: 8990, at: "x" }).outcome.effect, 10)

' --- TIER: holding out, the concept the architecture lacked ---------------
' A system that always acts when it should can never learn whether acting
' helps. Assignment is DETERMINISTIC in the key, not random, because a replay
' must make the same assignments as the live run (R5) and an operator asking
' "why was this one held back" must get an answer.
first = automation.assign("North-2/Outdoor", 0.3)
again = automation.assign("North-2/Outdoor", 0.3)
check("assignment is deterministic in the key", first.arm, again.arm)
check("and says why", contains(first.why, "held back deliberately")
      or contains(first.why, "assigned to act"), true)
' SHORT SEQUENTIAL KEYS ON PURPOSE. The first hash left consecutive short keys
' consecutive, so a whole run landed on one side of the threshold -- 0 holdouts
' out of 400 for exactly these keys -- while longer varied keys looked fine. A
' distribution tier that uses comfortable keys does not test the hash.
held = 0
for i = 1 to 400
    if automation.assign("c" + string(i), 0.25).arm = "holdout" then
        held = held + 1
    end if
next
check("short sequential keys land near the declared rate",
      held > 80 and held < 120, true)
held2 = 0
for i = 1 to 400
    if automation.assign("North-" + string(i) + "/Outdoor", 0.5).arm = "holdout" then
        held2 = held2 + 1
    end if
next
check("and so do path-shaped keys at another rate",
      held2 > 160 and held2 < 240, true)
' A zero holdout is allowed and SAYS what it costs, rather than being refused:
' plenty of processes have nothing worth learning about.
none = automation.assign("x", 0)
check("a zero holdout treats everything", none.arm, "treat")
check("  and says what that costs",
      contains(none.why, "nothing can be learned"), true)
on error goto next
x = automation.assign("x", 1)
check("holding everything back is refused", contains(error.message, "never acting"), true)
error.clear()
on error stop

' --- TIER: rehearsal validation -------------------------------------------
on error goto next
x = automation.rehearsal({ periods: 3, fired: 5 })
check("a process cannot fire more often than it ran",
      contains(error.message, "cannot"), true)
error.clear()
x = automation.rehearsal({ periods: 12, fired: 2, false_alarms: 5 })
check("nor have more false alarms than firings",
      contains(error.message, "false alarms out of"), true)
error.clear()
x = automation.execute(cheap, { authority: { spend_limit: 1 } }, full, executor)
check("an authority that does not declare a rehearsal requirement is refused",
      contains(error.message, "min_rehearsal_periods"), true)
error.clear()
on error stop

r = automation.rehearsal({ periods: 12, fired: 1, false_alarms: 0 })
check("a well-formed rehearsal is accepted", r.periods, 12)
check("and reports a false-alarm rate", r.false_alarm_rate, 0)

clear_mark()
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

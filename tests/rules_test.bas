' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' rules_test.bas -- stdlib/rules.bas, increment 1.
'
' SELF-CHECKING RATHER THAN GOLDEN, AND HERE THAT IS FORCED. Every defect this
' library can have produces A PLAUSIBLE DECISION: a payment capped at the wrong
' figure, a condition that quietly became false, an "unresolved" where the rule
' was actually wrong. A golden would record the wrong payment as the expected
' output and then defend it, which is exactly how a decision engine comes to be
' trusted while being wrong.
'
' So every check states its own expected answer and prints `ok` or a MISMATCH
' naming both sides, and the runner asserts a check COUNT -- a file that stops
' running its checks otherwise passes by saying nothing.
load rules

checks = 0
fails = 0

function ck(label, got, want)
    if got = want then
        print "ok   " + label
        return 1
    end if
    print "MISMATCH " + label + ": got [" + got + "] want [" + want + "]"
    return 0
end function

' --------------------------------------------------------------------------
' THE REAL RULE. This is the proposal's own worked contract example, and it is
' the first thing built because a record schema chosen before a real rule has
' been encoded through it is a guess.
'
'   Pay 80% of the eligible amount, subject to a USD 500 maximum. For services
'   performed on or after January 1, 2027, the maximum increases to USD 600.
'
' FOUR NAMED TARGETS RATHER THAN ONE EXPRESSION, because the amendment replaces
' ONE COMPONENT -- the cap -- and an expression alone cannot say which earlier
' component an author meant.
' --------------------------------------------------------------------------
function contract()
    initial = {}
    initial["rate"] = rules.val(0.80)
    initial["cap"] = rules.val({USD}"500.00")
    initial["gross"] = rules.op_of("*", [rules.target_of("rate"),
                                         rules.bind("eligible", "money", "the eligible amount")])
    initial["payment"] = rules.call_of("min", [rules.target_of("gross"), rules.target_of("cap")])
    rs = []
    append(rs, rules.rule_of("amend-2027", "cap",
        rules.op_of(">=", [rules.bind("service_date", "datetime", "the service date"),
                           rules.val({datetime}"2027-01-01 00:00:00")]),
        rules.val({USD}"600.00"),
        { agreement: "MSA-4471", version: 2, clause: "3.1(b)",
          wording: "For services performed on or after January 1, 2027, the maximum increases to USD 600." }))
    return rules.stack_of(initial, rs)
end function

st = contract()
before = { eligible: {USD}"800.00", service_date: {datetime}"2026-06-01 00:00:00" }
after = { eligible: {USD}"800.00", service_date: {datetime}"2027-03-01 00:00:00" }

print "--- the real contract ---"
v = rules.validate(st)
checks = checks + ck("the contract validates", string(v.ok), "true")
checks = checks + ck("with no problems", string(count(v.problems)), "0")
checks = checks + ck("targets order by dependency", join(rules.target_order(st).order, ","), "rate,cap,gross,payment")

r = rules.evaluate(st, before, true)
checks = checks + ck("gross is the negotiated 80%", string(r.targets["gross"].value), "640.00")
checks = checks + ck("the cap before the amendment", string(r.targets["cap"].value), "500.00")
checks = checks + ck("payment is capped", string(r.targets["payment"].value), "500.00")
checks = checks + ck("nothing errored", string(r.ok), "true")
checks = checks + ck("nothing unresolved", string(count(r.unresolved)), "0")

r2 = rules.evaluate(st, after, true)
checks = checks + ck("the amendment raises the cap", string(r2.targets["cap"].value), "600.00")
checks = checks + ck("and the payment with it", string(r2.targets["payment"].value), "600.00")

' THE SAME FIGURE BY BOTH PATHS. This is the agreement property stated on the
' one rule that matters; tests/rules_agreement.bas states it as a property over
' generated stacks, which is where a subtly wrong simplification shows up.
c = rules.condense(st)
cr = rules.evaluate_compiled(c, before)
cr2 = rules.evaluate_compiled(c, after)
checks = checks + ck("condensed agrees before", string(cr.targets["payment"].value), "500.00")
checks = checks + ck("condensed agrees after", string(cr2.targets["payment"].value), "600.00")
checks = checks + ck("the condensed cap is one conditional",
    rules.render(c.expressions["cap"]),
    "IF (the service date >= 2027-01-01 00:00:00) THEN 600.00 ELSE 500.00")

' THE EXPLANATION MUST NAME THE RULES THE EVALUATOR ACTUALLY APPLIED. Judged
' against the trace, never by whether it reads well: a suite asserting only
' "the text contains the amount" would pass on a confident fabrication.
ex = rules.explain_decision(r, "cap")
checks = checks + ck("before the amendment, no rule is claimed", join(ex.applied, ","), "")
ex2 = rules.explain_decision(r2, "cap")
checks = checks + ck("after it, the amendment is named", join(ex2.applied, ","), "amend-2027")
checks = checks + ck("and the explanation says so", string(contains(ex2.lines, "Rule amend-2027 applies.")), "true")
' AND IT REFUSES TO BE RECONSTRUCTED AFTER THE FACT. An explanation assembled
' from a result that carries no trace is a second walk by another name.
untraced = rules.evaluate(st, before)
checks = checks + ck("an untraced result cannot be explained", string(rules.explain_decision(untraced, "cap").ok), "false")

' --------------------------------------------------------------------------
' THE BOUNDARY, BOTH WAYS. A missing fact is an ordinary business condition and
' must answer "unresolved, waiting on X"; a currency mismatch is a DEFECT IN
' THE RULE and must answer error. Either half asserted alone passes on a
' library that always says one thing, so the pair is the check.
' --------------------------------------------------------------------------
print ""
print "--- absence propagates; invalidity raises ---"

no_date = { eligible: {USD}"800.00" }
ra = rules.evaluate(st, no_date, true)
checks = checks + ck("a missing fact leaves the cap unresolved", ra.targets["cap"].state, "absent")
checks = checks + ck("and it propagates to the payment", ra.targets["payment"].state, "absent")
checks = checks + ck("naming what it waits for", join(ra.waiting, ","), "service_date")
checks = checks + ck("an unresolved result is not an error", string(count(ra.errors)), "0")
checks = checks + ck("res.ok stays true for a missing fact", string(ra.ok), "true")

' The same stack with a EUR cap: nothing is missing, the rule cannot work.
bad = {}
bad["cap"] = rules.val({EUR}"500.00")
bad["gross"] = rules.val({USD}"640.00")
bad["payment"] = rules.op_of("<", [rules.target_of("gross"), rules.target_of("cap")])
rb = rules.evaluate(rules.stack_of(bad, []), {}, false)
checks = checks + ck("a currency mismatch is an error, not unresolved", rb.targets["payment"].state, "error")
checks = checks + ck("res.ok is false for a broken rule", string(rb.ok), "false")
checks = checks + ck("and the host's own sentence survives",
    string(contains(rb.targets["payment"].message, "cannot order money in different currencies")), "true")
checks = checks + ck("a broken rule is NOT reported as waiting", string(count(rb.waiting)), "0")

' THE SAME MISMATCH BETWEEN TWO LITERALS IS CAUGHT BEFORE ANY FACT ARRIVES,
' which is the other end of the same boundary.
lit = {}
lit["t"] = rules.op_of("<", [rules.val({USD}"1.00"), rules.val({EUR}"1.00")])
vb = rules.validate(rules.stack_of(lit, []))
checks = checks + ck("two literal currencies are a validation problem", string(vb.ok), "false")
checks = checks + ck("named as a currency mismatch", vb.problems[0].problem, "currency-mismatch")
checks = checks + ck("with a remedy that names money.convert",
    string(contains(vb.problems[0].remedy, "money.convert")), "true")

' --------------------------------------------------------------------------
' THREE-VALUED LOGIC. Each row is a fact about what a business author should be
' told, and the table is the measured DIVERGENCE from gBASIC: the host raises
' on five of these and answers `false` on a sixth.
' --------------------------------------------------------------------------
print ""
print "--- three-valued logic ---"

function one_of(stack, ctx, target)
    r = rules.evaluate(stack, ctx, false)
    cell = r.targets[target]
    if cell.state = "ok" then return string(cell.value)
    if cell.state = "absent" then return "absent:" + cell.absence
    return "error"
end function

function one(e, ctx)
    r = rules.evaluate(rules.stack_of({ t: e }, []), ctx, false)
    cell = r.targets["t"]
    if cell.state = "ok" then return string(cell.value)
    if cell.state = "absent" then return "absent:" + cell.absence
    return "error"
end function

u = rules.val(unknown)
no = rules.val(nothing)
tt = rules.val(true)
ff = rules.val(false)

checks = checks + ck("not unknown is unknown (gBASIC raises)", one(rules.op_of("not", [u]), {}), "absent:unknown")
checks = checks + ck("false and unknown is false", one(rules.op_of("and", [ff, u]), {}), "false")
checks = checks + ck("unknown and false is false too -- Kleene, not short-circuit", one(rules.op_of("and", [u, ff]), {}), "false")
checks = checks + ck("true and unknown is unknown (gBASIC raises)", one(rules.op_of("and", [tt, u]), {}), "absent:unknown")
checks = checks + ck("true or unknown is true", one(rules.op_of("or", [tt, u]), {}), "true")
checks = checks + ck("unknown or true is true", one(rules.op_of("or", [u, tt]), {}), "true")
checks = checks + ck("false or unknown is unknown (gBASIC raises)", one(rules.op_of("or", [ff, u]), {}), "absent:unknown")
checks = checks + ck("unknown + 1 is unknown (gBASIC raises)", one(rules.op_of("+", [u, rules.val(1)]), {}), "absent:unknown")
checks = checks + ck("unknown > 1 is unknown (gBASIC raises)", one(rules.op_of(">", [u, rules.val(1)]), {}), "absent:unknown")
' THE OTHER DIRECTION OF DIVERGENCE, AND IT IS THE DELIBERATE ONE: gBASIC
' answers `false` here. In a contract, "is the claim type X" when the claim
' type is not yet known must not answer "no".
checks = checks + ck("unknown = 1 is unknown, NOT false as in gBASIC", one(rules.op_of("=", [u, rules.val(1)]), {}), "absent:unknown")
checks = checks + ck("and != likewise", one(rules.op_of("!=", [u, rules.val(1)]), {}), "absent:unknown")

' NOTHING IS A DIFFERENT ABSENCE and the two must not render as one sentence:
' "we agreed there is no cap" is not "we do not yet know the cap".
checks = checks + ck("nothing propagates like unknown", one(rules.op_of("+", [no, rules.val(1)]), {}), "absent:nothing")
checks = checks + ck("is nothing tells them apart", one(rules.op_of("is nothing", [no]), {}), "true")
checks = checks + ck("is nothing is false for unknown", one(rules.op_of("is nothing", [u]), {}), "false")
checks = checks + ck("is unknown is true for unknown", one(rules.op_of("is unknown", [u]), {}), "true")
checks = checks + ck("is unknown is false for nothing", one(rules.op_of("is unknown", [no]), {}), "false")
checks = checks + ck("is unknown is false for a present value", one(rules.op_of("is unknown", [rules.val(7)]), {}), "false")
' WHEN TWO ABSENCES MEET, UNKNOWN DOMINATES: "we do not know" plus "we agreed
' there is none" is still not known, and reporting `nothing` there would claim
' an agreement nobody made.
checks = checks + ck("unknown + nothing is unknown", one(rules.op_of("+", [u, no]), {}), "absent:unknown")
checks = checks + ck("nothing + unknown is unknown too", one(rules.op_of("+", [no, u]), {}), "absent:unknown")
checks = checks + ck("nothing + nothing stays nothing", one(rules.op_of("+", [no, no]), {}), "absent:nothing")
checks = checks + ck("and both absent joins the same way", one(rules.op_of("and", [u, no]), {}), "absent:unknown")

' SHORT-CIRCUIT WHERE THE LEFT OPERAND DECIDES. The right operand is a genuine
' error, so the only way these can pass is by never evaluating it.
boom = rules.op_of("+", [rules.val({USD}"1.00"), rules.val({EUR}"1.00")])
checks = checks + ck("false AND <error> never evaluates the error", one(rules.op_of("and", [ff, boom]), {}), "false")
checks = checks + ck("true OR <error> never evaluates the error", one(rules.op_of("or", [tt, boom]), {}), "true")
checks = checks + ck("but true AND <error> does", one(rules.op_of("and", [tt, boom]), {}), "error")
checks = checks + ck("an unselected IF branch is never evaluated",
    one(rules.if_of(tt, rules.val(1), boom), {}), "1")

' MEMBERSHIP: unknown while an unresolved element could change the answer, and
' settled the moment a match is found however many absences sit beside it.
checks = checks + ck("in, found", one(rules.op_of("in", [rules.val("b"), rules.val(["a","b"])]), {}), "true")
checks = checks + ck("in, absent, no match possible", one(rules.op_of("in", [rules.val("z"), rules.val(["a",unknown])]), {}), "absent:unknown")
checks = checks + ck("in, a found match settles it despite an absence", one(rules.op_of("in", [rules.val("a"), rules.val(["a",unknown])]), {}), "true")
checks = checks + ck("in, not found and nothing absent", one(rules.op_of("in", [rules.val("z"), rules.val(["a","b"])]), {}), "false")
checks = checks + ck("not in negates it", one(rules.op_of("not in", [rules.val("z"), rules.val(["a","b"])]), {}), "true")
checks = checks + ck("not in stays absent where in would be", one(rules.op_of("not in", [rules.val("z"), rules.val(["a",unknown])]), {}), "absent:unknown")
checks = checks + ck("nothing is in nothing: an empty collection settles an absent subject",
    one(rules.op_of("in", [u, rules.val([])]), {}), "false")

' --------------------------------------------------------------------------
' NEGATED COMPARISONS, AND THE ONE CASE THAT FORBIDS REWRITING THEM.
' --------------------------------------------------------------------------
print ""
print "--- negated comparisons ---"

checks = checks + ck("4 !> 5", one(rules.op_of("!>", [rules.val(4), rules.val(5)]), {}), "true")
checks = checks + ck("6 !> 5", one(rules.op_of("!>", [rules.val(6), rules.val(5)]), {}), "false")
checks = checks + ck("5 !< 4", one(rules.op_of("!<", [rules.val(5), rules.val(4)]), {}), "true")
checks = checks + ck("5 !>= 6", one(rules.op_of("!>=", [rules.val(5), rules.val(6)]), {}), "true")
checks = checks + ck("5 !<= 4", one(rules.op_of("!<=", [rules.val(5), rules.val(4)]), {}), "true")

' THE CASE. `!>` IS NOT `<=`, and a normalizer that rewrote it would be wrong
' for any operand that can be NaN -- measured in gBASIC: not (nan > 1) is TRUE
' while nan <= 1 is FALSE. The two sides are asserted to DIFFER, because
' asserting either value alone passes on a library that rewrote the operator.
nn = number("nan")
checks = checks + ck("nan !> 1 is true", one(rules.op_of("!>", [rules.val(nn), rules.val(1)]), {}), "true")
checks = checks + ck("nan <= 1 is false", one(rules.op_of("<=", [rules.val(nn), rules.val(1)]), {}), "false")
checks = checks + ck("so !> and <= DIFFER, which is why the rewrite is refused",
    string(one(rules.op_of("!>", [rules.val(nn), rules.val(1)]), {}) != one(rules.op_of("<=", [rules.val(nn), rules.val(1)]), {})), "true")

' AND THE AUTHOR'S SPELLING SURVIVES INTO WHAT THEY READ BACK.
checks = checks + ck("the spelling is preserved in render",
    rules.render(rules.op_of("!>=", [rules.bind("a","number"), rules.bind("b","number")])), "(a !>= b)")

' --------------------------------------------------------------------------
' RULE-STACK SEMANTICS. A later applicable rule overrides an earlier result for
' the same target; rules for different targets do not implicitly replace each
' other.
' --------------------------------------------------------------------------
print ""
print "--- stack ordering ---"

function allow_stack(rule_list)
    initial = {}
    initial["allow"] = rules.op_of("and", [rules.bind("active", "boolean"),
                                           rules.op_of(">=", [rules.bind("balance", "number"),
                                                              rules.bind("minimum", "number")])])
    return rules.stack_of(initial, rule_list)
end function

emergency = rules.rule_of("emergency", "allow", rules.bind("emergency", "boolean"), rules.val(true), {})
frozen = rules.rule_of("frozen", "allow", rules.bind("frozen", "boolean"), rules.val(false), {})

' The proposal's own example: initial, then emergency, then frozen -- so frozen
' is OUTERMOST in the condensed form and wins.
ordered = allow_stack([emergency, frozen])
base = { active: true, balance: 10, minimum: 5, emergency: true, frozen: true }
checks = checks + ck("the LAST applicable rule wins", one_of(ordered, base, "allow"), "false")
checks = checks + ck("and the condensed form is the proposal's exact form",
    rules.render(rules.condense(ordered).expressions["allow"]),
    "IF frozen THEN false ELSE IF emergency THEN true ELSE (active AND (balance >= minimum))")

' SWAPPED. The same two rules in the other order must give the OTHER answer, or
' "the last rule wins" is satisfied by a library with no ordering at all.
swapped = allow_stack([frozen, emergency])
checks = checks + ck("swapping the two rules swaps the answer", one_of(swapped, base, "allow"), "true")

checks = checks + ck("an inapplicable rule retains the preceding result",
    one_of(ordered, { active: true, balance: 10, minimum: 5, emergency: false, frozen: false }, "allow"), "true")
checks = checks + ck("the initial expression still decides when no rule fires",
    one_of(ordered, { active: true, balance: 1, minimum: 5, emergency: false, frozen: false }, "allow"), "false")

' AN UNRESOLVED CONDITION POISONS THE RESULT, and a later UNCONDITIONAL rule
' makes that irrelevant. Both halves, because the first alone is satisfied by a
' library that can never recover and the second by one that never poisons.
poisoned = allow_stack([rules.rule_of("maybe", "allow", rules.bind("unseen", "boolean"), rules.val(true), {})])
checks = checks + ck("an undecidable condition leaves the target unresolved",
    one_of(poisoned, { active: true, balance: 10, minimum: 5 }, "allow"), "absent:unknown")
rescued = allow_stack([rules.rule_of("maybe", "allow", rules.bind("unseen", "boolean"), rules.val(true), {}),
                       rules.rule_of("override", "allow", rules.always(), rules.val(false), {})])
checks = checks + ck("a later unconditional rule makes it irrelevant",
    one_of(rescued, { active: true, balance: 10, minimum: 5 }, "allow"), "false")
checks = checks + ck("and condensing collapses what it made unreachable",
    rules.render(rules.condense(rescued).expressions["allow"]), "false")

' DIFFERENT TARGETS DO NOT INTERFERE.
two = {}
two["a"] = rules.val(1)
two["b"] = rules.val(2)
tw = rules.stack_of(two, [rules.rule_of("ra", "a", rules.always(), rules.val(9), {})])
rt = rules.evaluate(tw, {}, false)
checks = checks + ck("a rule for a names a", string(rt.targets["a"].value), "9")
checks = checks + ck("and leaves b alone", string(rt.targets["b"].value), "2")

' --------------------------------------------------------------------------
' VALIDATION IS A VALUE, AND IT REPORTS EVERY PROBLEM. An author fixing six
' mistakes one reload at a time stops using the tool.
' --------------------------------------------------------------------------
print ""
print "--- validation ---"

sixbad = {}
sixbad["t"] = rules.op_of("<>", [rules.val(1), rules.val(2)])
sixbad["u"] = rules.op_of("+", [rules.val(1)])
sixbad["w"] = rules.call_of("minimum", [rules.val(1)])
sixbad["x"] = rules.target_of("no_such_target")
sixbad["y"] = rules.op_of("+", [rules.val(1), rules.val("two")])
sixbad["z"] = { kind: "nonsense" }
vs = rules.validate(rules.stack_of(sixbad, []))
checks = checks + ck("validation answers as a value, never a raise", type(vs), "record")
checks = checks + ck("six independent mistakes, six problems", string(count(vs.problems)), "6")

function problem_named(v, nm)
    for each p in v.problems
        if p.problem = nm then return p
    next
    return nothing
end function

p1 = problem_named(vs, "unknown-operator")
checks = checks + ck("the angle-bracket not-equal is refused by name", string(not is_nothing(p1)), "true")
' THE REMEDY IS gBASIC'S OWN SENTENCE, because the readers who write `<>` in a
' rule are the readers who will write it in gBASIC, and a rule language whose
' primary spelling is the one the host rejects teaches a habit that fails.
checks = checks + ck("and reproduces the host's remedy verbatim",
    p1.remedy, "'<>' is not an operator; not-equal is != -- a != b")
checks = checks + ck("the author's own spelling is in the problem", p1.spelling, "<>")

checks = checks + ck("wrong operand count is located", problem_named(vs, "operator-arity").where, "initial")
checks = checks + ck("an unregistered function is refused", string(not is_nothing(problem_named(vs, "unregistered-function"))), "true")
checks = checks + ck("with a near-miss hint", string(contains(problem_named(vs, "unregistered-function").remedy, "did you mean 'min'")), "true")
checks = checks + ck("an unknown target is refused", problem_named(vs, "unknown-target").found, "no_such_target")
checks = checks + ck("a number against a string is refused", problem_named(vs, "type-mismatch").found, "number and string")
checks = checks + ck("and so is an invented node kind", problem_named(vs, "unknown-kind").found, "nonsense")

' EVERY PROBLEM LOCATES ITSELF BY THE RULE'S OWN ID, not an array index: the
' author reordered the stack and the index moved.
idstack = rules.stack_of({ q: rules.val(1) },
    [rules.rule_of("clause-7b", "q", rules.val(true), rules.op_of("<>", [rules.val(1), rules.val(2)]), {})])
vi = rules.validate(idstack)
checks = checks + ck("a problem names the rule's id", vi.problems[0].rule, "clause-7b")
checks = checks + ck("and where in the rule it is", vi.problems[0].where, "replacement")
checks = checks + ck("and the target it writes", vi.problems[0].target, "q")

' A CONDITION THAT CANNOT POSSIBLY BE A BOOLEAN is caught before any fact
' arrives, and the commoner mistake is not a literal -- it is writing the
' quantity where the comparison was meant. DELIBERATELY NARROW: only what is
' certain from the node alone, since a binding or a registered call could be
' anything and a diagnostic that is sometimes a lie is worse than a terse one.
function cond_problem(c)
    v = rules.validate(rules.stack_of({ t: rules.val(1) }, [rules.rule_of("r", "t", c, rules.val(2), {})]))
    if v.ok then return "accepted"
    return v.problems[0].problem
end function
checks = checks + ck("a number as a condition is refused", cond_problem(rules.val(5)), "condition-not-boolean")
checks = checks + ck("so is arithmetic as a condition", cond_problem(rules.op_of("+", [rules.bind("a","number"), rules.bind("b","number")])), "condition-not-boolean")
' FOUR CONTROLS, because this rule fires on a SHAPE and over-firing would flag
' correct rules -- the failure that got the blind-shadow warning reverted.
checks = checks + ck("but a comparison is accepted", cond_problem(rules.op_of(">=", [rules.bind("a","number"), rules.bind("b","number")])), "accepted")
checks = checks + ck("and a boolean binding", cond_problem(rules.bind("flag", "boolean")), "accepted")
checks = checks + ck("and an absence test", cond_problem(rules.op_of("is unknown", [rules.bind("x", "any")])), "accepted")
checks = checks + ck("and an unconditional rule", cond_problem(rules.always()), "accepted")
checks = checks + ck("and a registered call, which could be anything", cond_problem(rules.call_of("min", [rules.bind("a","number")])), "accepted")

' THE CONTROL. A refusal suite with no accepted neighbour is satisfied by a
' validator that refuses everything.
checks = checks + ck("a well-formed stack has no problems", string(rules.validate(st).ok), "true")

' A duplicate id cannot be told apart in an explanation.
dup = rules.stack_of({ q: rules.val(1) },
    [rules.rule_of("same", "q", rules.always(), rules.val(2), {}),
     rules.rule_of("same", "q", rules.always(), rules.val(3), {})])
checks = checks + ck("two rules with one id are refused", problem_named(rules.validate(dup), "duplicate-id").found, "same")

' --------------------------------------------------------------------------
' A HOSTILE STACK. The interesting rule arrived from outside -- a counterparty,
' a web form, a store written years ago -- so the evaluator must be safe against
' a stack that is not merely wrong but hostile. Four doors, each asserted.
' --------------------------------------------------------------------------
print ""
print "--- the security boundary ---"

' DOOR 1: calls reach only the registered whitelist. There is no path from a
' rule to function_resolve, so these names are not refused by a blacklist --
' they are simply not reachable, and the same answer comes back for a stdlib
' function, a builtin and a module verb.
for each forbidden in ["read", "write", "spawn", "sqlite.query", "webclient.get", "bound", "process.run", "eval"]
    hostile = rules.stack_of({ t: rules.call_of(forbidden, [rules.val("/etc/passwd")]) }, [])
    got = one_of(hostile, {}, "t")
    vh = rules.validate(hostile)
    checks = checks + ck("a rule cannot call " + forbidden, got + "/" + string(vh.ok), "error/false")
next

' DOOR 2: a function VALUE is admitted as data so a stack can round-trip, and
' is never invoked. Without this, a deserialized operand plus bound() is
' arbitrary execution with extra steps.
function harmless()
    return 1
end function
fv = rules.stack_of({ t: rules.val(harmless) }, [])
checks = checks + ck("a function value operand is inert", one_of(fv, {}, "t"), "error")
checks = checks + ck("and refused at validation too", string(rules.validate(fv).ok), "false")
checks = checks + ck("named as what it is", problem_named(rules.validate(fv), "function-value").found, "a function value")

' DOOR 3: a binding resolves only against the supplied context, so a rule
' cannot name a variable the host did not offer. THE CONTROL IS THE SECOND
' HALF: a binding the context DOES offer must resolve, or "bindings cannot
' reach the environment" is satisfied by bindings that never work.
secret_in_scope = "do not read me"
peek = rules.stack_of({ t: rules.bind("secret_in_scope", "string") }, [])
checks = checks + ck("a binding cannot reach the enclosing environment", one_of(peek, {}, "t"), "absent:unknown")
checks = checks + ck("and the control: an offered binding does resolve",
    one_of(peek, { secret_in_scope: "offered" }, "t"), "offered")

' BOUNDED: a rule stack is attacker-controlled recursion, and gBASIC's own
' recursion segfaults with NO DIAGNOSTIC -- measured between 380 and 400 levels
' for this evaluator. So the cap is a named constant well under that, and it
' refuses rather than crashing. run_rules.sh asserts the margin.
function nest(n)
    e = rules.val(1)
    i = 0
    while i < n
        e = rules.op_of("+", [e, rules.val(1)])
        i = i + 1
    end while
    return e
end function
checks = checks + ck("just under the cap still evaluates", one_of(rules.stack_of({ t: nest(95) }, []), {}, "t"), "96")
checks = checks + ck("past the cap is refused, not crashed", one_of(rules.stack_of({ t: nest(400) }, []), {}, "t"), "error")
checks = checks + ck("far past the cap is still only refused", one_of(rules.stack_of({ t: nest(3000) }, []), {}, "t"), "error")
deep_v = rules.validate(rules.stack_of({ t: nest(3000) }, []))
checks = checks + ck("and the validator says so", string(not is_nothing(problem_named(deep_v, "too-deep"))), "true")

' A CYCLE IS REFUSED, NOT RESOLVED. A target defined in terms of itself has no
' value to report, and guessing a starting point would produce one.
cyc = {}
cyc["a"] = rules.target_of("b")
cyc["b"] = rules.target_of("a")
checks = checks + ck("a two-target cycle is refused", string(rules.validate(rules.stack_of(cyc, [])).ok), "false")
checks = checks + ck("named as a cycle", problem_named(rules.validate(rules.stack_of(cyc, [])), "cycle").found, "a -> b")
checks = checks + ck("and evaluating one does not hang", one_of(rules.stack_of(cyc, []), {}, "a"), "error")
selfref = {}
selfref["a"] = rules.op_of("+", [rules.target_of("a"), rules.val(1)])
checks = checks + ck("a target referring to itself is refused", string(rules.validate(rules.stack_of(selfref, [])).ok), "false")
' THE CONTROL: a CHAIN is not a cycle, or cycle detection would refuse every
' derived target -- which is the whole point of named components.
checks = checks + ck("but a chain of three is fine", join(rules.target_order(st).order, ","), "rate,cap,gross,payment")

' A MALFORMED STACK IS A VALUE TOO, and this was a HOLE rather than a design
' choice -- found by trying it rather than by reading. Measured before the fix:
' a stack record with no `rules` array RAISED `unknown record field: rules`
' straight out of `evaluate`, so a caller told that failure is a value got an
' exception; and a stack that was not a record at all came back `ok: true`,
' which is worse, being a successful-looking decision about a string.
'
' `validate` refused both all along. That is necessary and not sufficient:
' nothing obliges a caller to validate before evaluating, and the caller who
' forgets is exactly the one holding attacker-supplied data.
print ""
print "--- a malformed stack ---"

function refused(stack)
    r = rules.evaluate(stack, {}, false)
    if r.ok then return "accepted"
    if count(r.errors) = 0 then return "refused with no reason"
    return "refused"
end function

checks = checks + ck("a stack with no rules array is refused, not raised", refused({ initial: { t: rules.val(1) } }), "refused")
checks = checks + ck("a stack with no initial record", refused({ rules: [] }), "refused")
checks = checks + ck("a string is not a stack", refused("not a stack at all"), "refused")
checks = checks + ck("nor is an array", refused([1, 2, 3]), "refused")
checks = checks + ck("a rule that is not a record", refused(rules.stack_of({ t: rules.val(1) }, ["oops"])), "refused")
checks = checks + ck("a rule with no target", refused(rules.stack_of({ t: rules.val(1) }, [{ id: "x", replacement: rules.val(2) }])), "refused")
checks = checks + ck("a rule with no id", refused(rules.stack_of({ t: rules.val(1) }, [{ target: "t", replacement: rules.val(2) }])), "refused")
' THE CONTROL: the guard must not refuse a well-formed stack, or "malformed is
' refused" is satisfied by refusing everything.
checks = checks + ck("and the control: the real contract is still accepted", refused(st), "accepted")

' CONDENSE AND THE COMPILED PATH REFUSE THE SAME THINGS, since a caller may
' reach either first and a guard on one entrance is not a guard.
checks = checks + ck("condense refuses a malformed stack as a value", string(rules.condense({ initial: {} }).ok), "false")
checks = checks + ck("and does not raise doing it", string(has(rules.condense("rubbish"), "message")), "true")
checks = checks + ck("a refused compiled result is refused by the compiled walk",
    string(rules.evaluate_compiled(rules.condense("rubbish"), {}).ok), "false")
checks = checks + ck("and so is something that was never compiled at all",
    string(rules.evaluate_compiled({ expressions: {} }, {}).ok), "false")
checks = checks + ck("the control: a real compiled result still evaluates",
    string(rules.evaluate_compiled(rules.condense(st), before).targets["payment"].value), "500.00")

' THE TWO SHAPE RULES MUST AGREE IN ONE DIRECTION, and it is the direction that
' matters: the evaluator may not refuse a stack the validator called fine. The
' reverse is legitimate and not asserted -- validate passes a well-formed stack
' whose evaluation then errors on a currency mismatch, which is the whole point
' of the boundary. They are written in two places because they report different
' things (a message against a located problem record), and this is what stops
' the two from drifting into disagreement about what is walkable at all.
malformed = []
append(malformed, { initial: { t: rules.val(1) } })
append(malformed, { rules: [] })
append(malformed, "not a stack at all")
append(malformed, [1, 2, 3])
append(malformed, rules.stack_of({ t: rules.val(1) }, ["oops"]))
append(malformed, rules.stack_of({ t: rules.val(1) }, [{ id: "x", replacement: rules.val(2) }]))
append(malformed, rules.stack_of({ t: rules.val(1) }, [{ target: "t", replacement: rules.val(2) }]))
agreed = 0
for each m in malformed
    if not rules.validate(m).ok then agreed = agreed + 1
next
checks = checks + ck("everything the evaluator refuses, the validator refuses too", string(agreed), string(count(malformed)))

' --------------------------------------------------------------------------
' EXPANSION. Different stacks condense to the same expression, so exact
' expansion is only possible from retained source -- and an equivalent
' expansion must not claim otherwise.
' --------------------------------------------------------------------------
print ""
print "--- expansion ---"

c2 = rules.condense(ordered)
src = rules.expand(c2, "source")
checks = checks + ck("source mode recovers the authored stack", string(src.stack = ordered), "true")
checks = checks + ck("and says it claims the source", string(src.claims_source), "true")

eq = rules.expand(c2, "equivalent")
checks = checks + ck("equivalent mode does NOT claim the source", string(eq.claims_source), "false")
checks = checks + ck("and says so in as many words", string(contains(eq.message, "not original")), "true")
' THE LOAD-BEARING CHECK: an equivalent expansion must decide the same way. A
' regenerated stack that read differently and ANSWERED differently would be
' worse than no expansion at all.
checks = checks + ck("but it answers identically", one_of(eq.stack, base, "allow"), one_of(ordered, base, "allow"))
checks = checks + ck("and identically on the other context",
    one_of(eq.stack, { active: true, balance: 1, minimum: 5, emergency: false, frozen: false }, "allow"),
    one_of(ordered, { active: true, balance: 1, minimum: 5, emergency: false, frozen: false }, "allow"))
' AND THE IDS ARE GENERATED AND SAY SO, since the original wording is gone.
checks = checks + ck("the regenerated rules are marked generated", string(eq.stack.rules[0].meta.generated), "true")

' --------------------------------------------------------------------------
print ""
print "checks: " + string(checks)
if checks = 0 then
    print "FAIL: no checks ran"
end if

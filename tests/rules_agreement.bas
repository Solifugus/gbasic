' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' rules_agreement.bas -- THE PROPERTY THE WHOLE DESIGN RESTS ON.
'
' Evaluating the authored stack and evaluating its condensed form must agree.
' Condensation is only safe if it changes nothing, and this is the only check
' that fails when a fold is SUBTLY wrong rather than obviously so.
'
' ASSERTED AS A PROPERTY OVER GENERATED STACKS, NOT A HANDFUL OF EXAMPLES. A
' hand-written case exercises the shapes the author already had in mind, and the
' folds that go wrong are the ones nobody pictured -- an absence meeting a
' short-circuit, a negated comparison over NaN, a later unconditional override
' above an unresolved condition. tests/rules_test.bas states the property on the
' one real contract; this states it over a population.
'
' THE TWO PATHS ARE GENUINELY DIFFERENT CODE, which is what makes agreement
' evidence rather than a tautology. `evaluate` walks the rules FORWARD and
' evaluates every condition whether or not a later rule will overwrite the
' result; `evaluate_compiled` walks nested conditionals LATEST-FIRST and
' short-circuits. They visit conditions in opposite orders and the compiled one
' visits fewer.
'
' SO AGREEMENT IS ON THE TARGET RESULTS AND DELIBERATELY NOT ON THE COUNTERS OR
' THE TRACE: the paths are MEANT to do different amounts of work, and asserting
' equal node counts would be asserting the fold does not work.
'
' THE CENSUS IS WHAT STOPS THIS BEING VACUOUS. A generator that only ever
' produced `ok` results would agree perfectly while exercising none of the cases
' the property exists for, so the run must be shown to have reached all three
' states, both absence kinds, and rules that fired and rules that did not.
load rules
load fake

function pool_value(seed, i)
    k = fake.between(seed, i * 7 + 1, 0, 9)
    if k = 0 then return 3
    if k = 1 then return 7
    if k = 2 then return true
    if k = 3 then return false
    if k = 4 then return "alpha"
    if k = 5 then return {USD}"10.00"
    if k = 6 then return {EUR}"10.00"
    if k = 7 then return unknown
    if k = 8 then return nothing
    return [3, 7, unknown]
end function

function pool_binding(seed, i)
    names = ["present_number", "present_boolean", "present_money", "absent_one", "absent_two", "explicit_unknown", "explicit_nothing"]
    nm = fake.pick(seed, i * 11 + 3, names)
    return rules.bind(nm, "any", nm)
end function

function context_of()
    return { present_number: 5, present_boolean: true, present_money: {USD}"10.00",
             explicit_unknown: unknown, explicit_nothing: nothing }
end function

' A random expression. The cursor is threaded by hand because `fake` is a pure
' function of (seed, index) -- which is also what makes a failing seed
' reproducible on its own, with no shared generator state to replay.
function gen_expr(seed, cursor, depth, targets)
    c = cursor
    if depth <= 0 then
        k = fake.between(seed, c, 0, 2)
        c = c + 1
        if k = 0 then return { expr: rules.val(pool_value(seed, c)), cursor: c + 1 }
        if k = 1 then return { expr: pool_binding(seed, c), cursor: c + 1 }
        if count(targets) = 0 then return { expr: rules.val(pool_value(seed, c)), cursor: c + 1 }
        return { expr: rules.target_of(fake.pick(seed, c, targets)), cursor: c + 1 }
    end if

    shape = fake.between(seed, c, 0, 11)
    c = c + 1
    if shape = 0 then
        return gen_expr(seed, c, 0, targets)
    end if
    if shape = 1 then
        a = gen_expr(seed, c, depth - 1, targets)
        return { expr: rules.op_of("not", [a.expr]), cursor: a.cursor }
    end if
    if shape = 2 then
        a = gen_expr(seed, c, depth - 1, targets)
        return { expr: rules.op_of("neg", [a.expr]), cursor: a.cursor }
    end if
    if shape = 3 then
        a = gen_expr(seed, c, depth - 1, targets)
        return { expr: rules.op_of(fake.pick(seed, c + 1, ["is unknown", "is nothing"]), [a.expr]), cursor: a.cursor }
    end if
    if shape = 4 then
        a = gen_expr(seed, c, depth - 1, targets)
        b = gen_expr(seed, a.cursor, depth - 1, targets)
        d = gen_expr(seed, b.cursor, depth - 1, targets)
        return { expr: rules.if_of(a.expr, b.expr, d.expr), cursor: d.cursor }
    end if
    if shape = 5 then
        a = gen_expr(seed, c, depth - 1, targets)
        b = gen_expr(seed, a.cursor, depth - 1, targets)
        return { expr: rules.call_of(fake.pick(seed, c + 2, ["min", "max"]), [a.expr, b.expr]), cursor: b.cursor }
    end if
    ops = ["+", "-", "*", "=", "!=", "<", ">=", "!>", "!<=", "and", "or", "in", "not in"]
    a = gen_expr(seed, c, depth - 1, targets)
    b = gen_expr(seed, a.cursor, depth - 1, targets)
    return { expr: rules.op_of(fake.pick(seed, c + 3, ops), [a.expr, b.expr]), cursor: b.cursor }
end function

' A CONDITION IS GENERATED AS A BOOLEAN, deliberately, and this was a
' CORRECTION rather than a design choice. The first version drew conditions from
' the free-form generator above: 200 stacks produced 9 conditions that fired
' against 184 that did not, and 69% of all target results were errors -- so the
' property was being checked almost entirely on stacks where no override ever
' applied, which is the one case that needs no ordering to get right.
'
' A population can agree perfectly and exercise nothing. The census below is
' what made that visible, and it is why the census is a tier rather than a note.
function gen_condition(seed, cursor, depth, targets)
    c = cursor
    shape = fake.between(seed, c, 0, 9)
    c = c + 1
    if depth > 0 then
        if shape = 0 then
            a = gen_condition(seed, c, depth - 1, targets)
            return { expr: rules.op_of("not", [a.expr]), cursor: a.cursor }
        end if
        if shape = 1 or shape = 2 then
            a = gen_condition(seed, c, depth - 1, targets)
            b = gen_condition(seed, a.cursor, depth - 1, targets)
            return { expr: rules.op_of(fake.pick(seed, c + 1, ["and", "or"]), [a.expr, b.expr]), cursor: b.cursor }
        end if
    end if
    if shape = 3 then
        return { expr: rules.bind(fake.pick(seed, c, ["present_boolean", "absent_one", "explicit_unknown", "explicit_nothing"]), "boolean"), cursor: c + 1 }
    end if
    if shape = 4 then
        return { expr: rules.val(fake.pick(seed, c, [true, false])), cursor: c + 1 }
    end if
    if shape = 5 then
        a = gen_expr(seed, c, 0, targets)
        return { expr: rules.op_of(fake.pick(seed, c + 1, ["is unknown", "is nothing"]), [a.expr]), cursor: a.cursor }
    end if
    ' A comparison of two LIKE-TYPED operands, so it usually answers rather than
    ' erroring -- with `absent_one` in the pool so absence still reaches the
    ' condition position, which is where an override gets poisoned.
    nums = ["present_number", "absent_one"]
    left = rules.bind(fake.pick(seed, c, nums), "number")
    right = rules.val(fake.pick(seed, c + 1, [3, 5, 7]))
    opn = fake.pick(seed, c + 2, ["=", "!=", "<", "<=", ">", ">=", "!>", "!<", "!>=", "!<="])
    return { expr: rules.op_of(opn, [left, right]), cursor: c + 3 }
end function

' A random stack over `n` targets, each target referring only to EARLIER ones so
' the population is acyclic by construction -- cycles are refused, and a
' refusal has nothing to agree about.
function gen_stack(seed)
    n = fake.between(seed, 1, 1, 3)
    names = []
    initial = {}
    c = 100
    i = 0
    while i < n
        nm = "t" + string(i)
        g = gen_expr(seed, c, fake.between(seed, c + 1, 1, 3), names)
        c = g.cursor + 1
        initial[nm] = g.expr
        append(names, nm)
        i = i + 1
    end while

    rs = []
    m = fake.between(seed, 2, 0, 4)
    j = 0
    while j < m
        tgt = fake.pick(seed, c, names)
        earlier = []
        for each nm in names
            if nm != tgt then append(earlier, nm)
        next
        c = c + 1
        unconditional = fake.between(seed, c, 0, 4) = 0
        c = c + 1
        if unconditional then
            rep = gen_expr(seed, c, fake.between(seed, c + 1, 1, 2), earlier)
            c = rep.cursor + 1
            append(rs, rules.rule_of("r" + string(j), tgt, rules.always(), rep.expr, {}))
        else
            cond = gen_condition(seed, c, fake.between(seed, c + 1, 1, 2), earlier)
            c = cond.cursor + 1
            rep = gen_expr(seed, c, fake.between(seed, c + 1, 1, 2), earlier)
            c = rep.cursor + 1
            append(rs, rules.rule_of("r" + string(j), tgt, cond.expr, rep.expr, {}))
        end if
        j = j + 1
    end while
    return rules.stack_of(initial, rs)
end function

function describe(cell)
    if cell.state = "ok" then return "ok=" + string(cell.value)
    if cell.state = "absent" then return "absent:" + cell.absence
    return "error"
end function

program main(args)
    total = 200
    if count(args) > 0 then total = number(args[0])

    ctx = context_of()
    disagreements = 0
    compared = 0
    n_ok = 0
    n_absent_unknown = 0
    n_absent_nothing = 0
    n_error = 0
    n_fired = 0
    n_not_fired = 0
    n_unconditional = 0
    stacks_with_rules = 0

    seed = 1
    while seed <= total
        st = gen_stack(seed)
        a = rules.evaluate(st, ctx, true)
        b = rules.evaluate_compiled(rules.condense(st), ctx)

        for each t in keys(a.targets)
            compared = compared + 1
            left = describe(a.targets[t])
            right = "MISSING"
            if has(b.targets, t) then right = describe(b.targets[t])
            if left != right then
                disagreements = disagreements + 1
                print "DISAGREE seed=" + string(seed) + " target=" + t + " stack=[" + left + "] condensed=[" + right + "]"
                if disagreements = 1 then
                    print "  " + rules.render_stack(st)
                end if
            end if
            if a.targets[t].state = "ok" then n_ok = n_ok + 1
            if a.targets[t].state = "error" then n_error = n_error + 1
            if a.targets[t].state = "absent" then
                if a.targets[t].absence = "unknown" then n_absent_unknown = n_absent_unknown + 1
                if a.targets[t].absence = "nothing" then n_absent_nothing = n_absent_nothing + 1
            end if
        next

        if count(st.rules) > 0 then stacks_with_rules = stacks_with_rules + 1
        for each tr in a.trace
            if tr.where = "condition" then
                if tr.applied then
                    n_fired = n_fired + 1
                else
                    n_not_fired = n_not_fired + 1
                end if
            end if
            if tr.where = "unconditional" then n_unconditional = n_unconditional + 1
        next

        seed = seed + 1
    end while

    print "stacks: " + string(total)
    print "target results compared: " + string(compared)
    print "disagreements: " + string(disagreements)
    print "census ok: " + string(n_ok)
    print "census absent unknown: " + string(n_absent_unknown)
    print "census absent nothing: " + string(n_absent_nothing)
    print "census error: " + string(n_error)
    print "stacks carrying rules: " + string(stacks_with_rules)
    print "conditions that fired: " + string(n_fired)
    print "conditions that did not: " + string(n_not_fired)
    print "unconditional overrides: " + string(n_unconditional)
end program

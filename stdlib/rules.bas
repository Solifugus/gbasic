' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' rules.bas -- typed business rules as DATA: build, validate, evaluate,
' condense, expand (docs/gbasic-rules-library-proposal.md).
'
' THIS IS THE FIRST INCREMENT AND IT IS A VERTICAL SLICE, not a layer. The
' proposal's implementation sequence puts "specify record schemas" first and the
' agreement property fifth; both are done HERE, because a schema chosen before a
' real rule has been encoded through it is a guess, and a condensation whose
' agreement with its source is checked two increments later is a condensation
' nobody can still change.
'
' TWO PRIORITIES GOVERN EVERY CHOICE, and they are the proposal's:
'
'   1. CLARITY FOR A BUSINESS AUTHOR. Where a spelling or a semantic could suit
'      the implementation or suit the person who negotiated the rule, it suits
'      the reader. That is why `!>` is stored as `!>` and never rewritten, why a
'      missing fact is an answer rather than a crash, and why every validation
'      problem names a remedy.
'   2. NO EXPOSURE OF gBASIC. A rule is data, and the interesting rule arrived
'      from outside -- a counterparty, a web form, a store written years ago.
'      There is NO path from a rule to `function_resolve`: a call reaches a
'      hand-written dispatch over a registered key and nothing else, so a rule
'      cannot name a stdlib function, a module verb, a socket or a file. See
'      `_apply_call` and `registry`.
'
' THE RULE LANGUAGE'S ABSENCE IS NOT gBASIC'S, AND THAT IS DELIBERATE. Measured
' at 0.7.0: `not unknown`, `true and unknown`, `false or unknown`, `unknown + 1`
' and `unknown > 1` all RAISE, and `unknown = 1` answers `false`. That is the
' kindest answer when a missing value is a programmer's mistake. In a negotiated
' agreement a missing fact is an ORDINARY BUSINESS CONDITION -- the invoice has
' not arrived, the inspection is not booked -- so this library PROPAGATES and
' reports what it is waiting for.
'
' THE SAFETY RAIL IS THAT INVALIDITY DOES NOT BECOME ABSENCE. Propagation covers
' absence and nothing else. Adding USD to EUR, ordering a month-bearing
' duration, comparing incompatible kinds -- those are the HOST's refusals, they
' are let through to the host on purpose, and they come back as `error`. An
' unresolved target means A FACT IS MISSING; an error means THE RULE IS WRONG,
' and an engine that laundered the second into the first would send an author
' hunting for an invoice that was never the problem.
'
' SO THERE ARE THREE RESULT STATES AND NOT TWO: ok, absent, error. A cell is
' never gBASIC's `unknown`, because gBASIC's `unknown` raises the moment it
' meets arithmetic and the whole point here is to survive it.
'
' FAILURE IS A VALUE, BOTH TIMES. `validate` answers { ok, problems } and
' `evaluate` answers a result carrying `errors`, following the rule the rest of
' this tree settled -- `try_decode` reports where JSON is malformed, `ldap.bind`
' returns a reason, `market` answers a dead network as data. A web form checking
' a rule as it is typed cannot have the validator end the request, and neither
' can one evaluating a hostile stack. The proposal said invalid operations
' "remain errors ... exactly as they are in the host"; the DISTINCTION is kept
' exactly, the DELIVERY is a value, because the caller is a request handler.
'
' DEPTH IS CAPPED FROM A MEASUREMENT, NOT FROM A PRECEDENT. The proposal cited
' `try_decode`'s 10,000-level cap. That is a cap on a C walk. This walk is
' gBASIC, and gBASIC's own recursion segfaults with NO DIAGNOSTIC between 700 and
' 740 frames on an 8 MB stack (measured; DOGFOOD records 936/937 for a bare
' tail call, and a frame holding one extra value dies earlier). A rules
' evaluator spends two frames per expression level, so 10,000 would be a cap
' that never fires in front of a crash that always does. See `max_depth`.
library rules

    ' ---------------------------------------------------------------- limits

    ' MEASURED, not chosen. tests/run_rules.sh asserts the margin between this
    ' and where the interpreter actually dies, and goes red if it closes --
    ' a cap above the crash is not a cap.
    function max_depth()
        return 100
    end function

    ' A stack that evaluates for an hour is a denial of service even when every
    ' step is legal, so size is bounded as well as depth.
    function max_nodes()
        return 20000
    end function

    function max_calls()
        return 5000
    end function

    ' ------------------------------------------------------------- operators

    ' The operators of the first increment. The author's SPELLING is what is
    ' stored: `!>` is a node whose op is `!>`, evaluated as the negation of `>`
    ' and rendered back as `!>`.
    '
    ' `!>` IS NOT REWRITTEN TO `<=` ANYWHERE, and that is a correctness rule
    ' rather than a courtesy to the reader. Measured in gBASIC:
    '
    '     n = nan :  not (n > 1) -> true ,  n <= 1 -> false
    '
    ' Every ordering comparison against NaN is false, so negating one is true
    ' while its "equivalent" is false.
    function operators()
        return ["+", "-", "*", "/", "neg",
                "=", "!=", "<", "<=", ">", ">=",
                "!>", "!<", "!>=", "!<=",
                "not", "and", "or",
                "in", "not in",
                "is unknown", "is nothing"]
    end function

    function _arity(op)
        if op = "neg" then return 1
        if op = "not" then return 1
        if op = "is unknown" then return 1
        if op = "is nothing" then return 1
        return 2
    end function

    ' What the negated comparisons negate. The table is the only place the
    ' relationship is written down, so the evaluator and the renderer cannot
    ' disagree about what `!>=` means.
    function _negates(op)
        if op = "!=" then return "="
        if op = "!>" then return ">"
        if op = "!<" then return "<"
        if op = "!>=" then return ">="
        if op = "!<=" then return "<="
        return ""
    end function

    ' ------------------------------------------------------- the whitelist

    ' DOOR 1 OF THE SECURITY BOUNDARY. A rule names a function by a key THIS
    ' LIBRARY registered. `_apply_call` is a hand-written dispatch over these
    ' keys; there is no name lookup, no `bound()`, and no route to
    ' `function_resolve`. A rule therefore cannot reach a stdlib function, a
    ' user function or a module verb -- which is also why it can never open a
    ' socket, read a file or spawn a process: those are reachable only through
    ' names it cannot say.
    function registry()
        r = []
        append(r, { key: "min",   least: 1, most: 32, kinds: "orderable",
                    template: "the smaller of {args}" })
        append(r, { key: "max",   least: 1, most: 32, kinds: "orderable",
                    template: "the greater of {args}" })
        append(r, { key: "abs",   least: 1, most: 1,  kinds: "number",
                    template: "the size of {args}" })
        append(r, { key: "round", least: 1, most: 2,  kinds: "number",
                    template: "{args} rounded" })
        return r
    end function

    function _registered(key)
        reg = registry()
        for each entry in reg
            if entry.key = key then return entry
        next
        return nothing
    end function

    ' ---------------------------------------------------------- the builders

    ' A VALUE OPERAND is a snapshot: fixed until the rule is edited.
    function val(v)
        return { kind: "value", value: v }
    end function

    ' A BINDING OPERAND resolves a current typed value at evaluation and
    ' exposes its identity to dependency discovery. `declared` is the native
    ' type the author expects; `label` is what an explanation calls it.
    function bind(name, declared, label = "")
        shown = label
        if shown = "" then shown = name
        return { kind: "binding", name: name, declared: declared, label: shown }
    end function

    ' A TARGET REFERENCE reads another target's result. This is what lets a
    ' decision be assembled from named components -- a rate, a cap, a gross
    ' amount -- so that an amendment can replace ONE of them. An expression
    ' alone cannot say which earlier component the author meant.
    function target_of(name)
        return { kind: "target", name: name }
    end function

    function op_of(op, args)
        return { kind: "op", op: op, args: args }
    end function

    function if_of(cond, a, b)
        return { kind: "if", cond: cond, then: a, otherwise: b }
    end function

    function call_of(fn, args)
        return { kind: "call", fn: fn, args: args }
    end function

    ' A RULE: when this condition holds, replace this target with this
    ' expression; otherwise retain its preceding result.
    '
    ' `meta` carries the provenance of a NEGOTIATED OBLIGATION -- agreement and
    ' version, source clause, effective date, original wording. It deliberately
    ' does not reuse `reasoning.bas`'s provenance: that record describes a
    ' STATISTICAL CLAIM and REQUIRES `search.width` by its own R1, and a rule
    ' has no search width because nothing was searched. What is inherited is the
    ' discipline, not the record.
    function rule_of(id, target, condition, replacement, meta)
        return { id: id, target: target, condition: condition,
                 replacement: replacement, meta: meta }
    end function

    ' An UNCONDITIONAL rule. `nothing` as a condition reads better in a stack
    ' than a value operand holding `true`, and the two evaluate identically.
    function always()
        return nothing
    end function

    function stack_of(initial, rule_list)
        return { initial: initial, rules: rule_list }
    end function

    ' --------------------------------------------------------------- cells

    ' The internal result of evaluating one expression. Three states, because
    ' two would collapse absence into invalidity at the first opportunity.
    function _ok(v)
        return { state: "ok", value: v, absence: "", missing: [], message: "", where: "" }
    end function

    function _absent(kind_of_absence, missing)
        return { state: "absent", value: unknown, absence: kind_of_absence,
                 missing: missing, message: "", where: "" }
    end function

    function _err(message, where)
        return { state: "error", value: unknown, absence: "", missing: [],
                 message: message, where: where }
    end function

    ' `unique(concat(a, b))` RATHER THAN A LOOP, and that is a ruling rather
    ' than a style: run_absence.sh counts hand-rolled set loops by SHAPE and
    ' holds the tree to a measured number, so that a union written out longhand
    ' cannot quietly accumulate. It caught five of them in this library's first
    ' version, and its message is the right one -- re-measure, do not just move
    ' the bound. `unique` keeps first-occurrence order, which is what makes the
    ' missing list read in the order a reader met the facts.
    function _merge_missing(a, b)
        return unique(concat(a, b))
    end function

    ' Absence has a KIND and two absences can meet. `unknown` dominates:
    ' "we do not know" plus "we agreed there is none" is still not known.
    function _join_absence(a, b)
        if a = "unknown" then return "unknown"
        if b = "unknown" then return "unknown"
        return "nothing"
    end function

    ' ----------------------------------------------------- the host boundary

    ' WHERE INVALIDITY COMES FROM. The operands are known to be present, so
    ' whatever the host refuses here is a fact about the VALUES -- a currency
    ' mismatch, an unorderable duration, two incompatible kinds -- and it is
    ' reported as an error carrying the host's own sentence, which already names
    ' both currencies or both kinds.
    '
    ' This is the one place the library hands values to gBASIC's own operators,
    ' and it is deliberate: reimplementing the arithmetic would mean
    ' reimplementing the refusals, and a second opinion about whether USD and
    ' EUR may be added is exactly the drift this library must not introduce.
    function _host_binary(op, a, b, where)
        on error goto next
        if op = "+" then
            r = a + b
        else if op = "-" then
            r = a - b
        else if op = "*" then
            r = a * b
        else if op = "/" then
            r = a / b
        else if op = "=" then
            r = a = b
        else if op = "!=" then
            r = a != b
        else if op = "<" then
            r = a < b
        else if op = "<=" then
            r = a <= b
        else if op = ">" then
            r = a > b
        else if op = ">=" then
            r = a >= b
        else
            return _err("operator is not evaluable: " + op, where)
        end if
        if error then
            m = error.message
            error.clear()
            return _err(m, where)
        end if
        return _ok(r)
    end function

    function _host_unary(op, a, where)
        on error goto next
        if op = "neg" then
            r = -a
        else
            return _err("operator is not evaluable: " + op, where)
        end if
        if error then
            m = error.message
            error.clear()
            return _err(m, where)
        end if
        return _ok(r)
    end function

    ' --------------------------------------------------- registered functions

    ' DOOR 1, implemented. A dispatch over keys, nothing more. Adding a function
    ' means writing a branch here AND an entry in `registry()`, and
    ' tests/run_rules.sh requires the two to agree -- two statements of one
    ' whitelist drift, and the direction that drifts silently is a key the
    ' dispatch honours and the registry does not list.
    function _apply_call(key, vals, where)
        on error goto next
        if key = "min" then
            r = min(vals)
        else if key = "max" then
            r = max(vals)
        else if key = "abs" then
            r = abs(vals[0])
        else if key = "round" then
            if count(vals) = 1 then
                r = round(vals[0])
            else
                r = round(vals[0], vals[1])
            end if
        else
            return _err("function is not registered: " + key, where)
        end if
        if error then
            m = error.message
            error.clear()
            return _err(m, where)
        end if
        return _ok(r)
    end function

    ' ------------------------------------------------------- the evaluator

    ' ONE WALK. `trace` is a flag on it rather than a second traversal that
    ' reconstructs what it thinks happened -- `ari.trace` is built this way in
    ' this tree for the stated reason that a trace describing a different
    ' program is worse than no trace, and the stakes here are higher: an
    ' explanation that plausibly misstates why a payment was capped is a
    ' document somebody may rely on in a dispute.
    '
    ' `st` is the walk's own mutable state, carried as a RECORD HELD IN AN
    ' ARRAY -- gBASIC records are values, so a plain record parameter cannot
    ' count anything back to the caller. The array is the one reference the
    ' language gives, and `st[0]` is read and written in place.
    function _eval(e, ctx, results, st, depth)
        if depth > max_depth() then
            return _err("expression is nested deeper than " + string(max_depth()) + " levels", "depth")
        end if

        s = st[0]
        s.nodes = s.nodes + 1
        st[0] = s
        if s.nodes > max_nodes() then
            return _err("expression has more than " + string(max_nodes()) + " nodes", "size")
        end if

        if type(e) != "record" then
            return _err("expression is not a record", "shape")
        end if
        if not has(e, "kind") then
            return _err("expression has no kind", "shape")
        end if

        k = e.kind

        if k = "value" then
            v = e["value"]
            ' An author may write absence literally. `unknown` is "not known";
            ' `nothing` is "we agreed there is none". Both propagate, and
            ' `is nothing` is what tells them apart.
            if is_unknown(v) then return _absent("unknown", [])
            if is_nothing(v) then return _absent("nothing", [])
            ' DOOR 2: a function VALUE is admitted as data so a stack can
            ' round-trip, and is never invoked. Without this refusal a
            ' deserialized operand plus `bound()` is arbitrary execution with
            ' extra steps.
            if type(v) = "function" then
                return _err("a function value cannot be evaluated", "value")
            end if
            return _ok(v)
        end if

        if k = "binding" then
            ' DOOR 3: bindings resolve ONLY against the supplied context,
            ' never against the enclosing environment, so a rule cannot name a
            ' variable the host did not offer. There is no read primitive here.
            nm = e.name
            if type(ctx) != "record" then return _absent("unknown", [nm])
            if not has(ctx, nm) then return _absent("unknown", [nm])
            v = ctx[nm]
            if is_unknown(v) then return _absent("unknown", [nm])
            if is_nothing(v) then return _absent("nothing", [nm])
            if type(v) = "function" then
                return _err("a function value cannot be evaluated", "binding " + nm)
            end if
            return _ok(v)
        end if

        if k = "target" then
            nm = e.name
            if not has(results, nm) then
                return _err("no such target: " + nm, "target " + nm)
            end if
            cell = results[nm]
            return cell
        end if

        if k = "if" then
            c = _eval(e.cond, ctx, results, st, depth + 1)
            if c.state = "error" then return c
            if c.state = "absent" then return c
            if type(c.value) != "boolean" then
                return _err("a condition must be a boolean, not " + type(c.value), "if")
            end if
            ' SHORT-CIRCUIT: the unselected branch is never evaluated, so an
            ' error or an absence inside it never happens.
            if c.value then return _eval(e.then, ctx, results, st, depth + 1)
            return _eval(e.otherwise, ctx, results, st, depth + 1)
        end if

        if k = "call" then
            s = st[0]
            s.calls = s.calls + 1
            st[0] = s
            if s.calls > max_calls() then
                return _err("more than " + string(max_calls()) + " registered calls", "size")
            end if
            entry = _registered(e.fn)
            if is_nothing(entry) then
                return _err("function is not registered: " + string(e.fn), "call")
            end if
            vals = []
            miss = []
            absence = "nothing"
            any_absent = false
            for each a in e.args
                cell = _eval(a, ctx, results, st, depth + 1)
                if cell.state = "error" then return cell
                if cell.state = "absent" then
                    any_absent = true
                    absence = _join_absence(absence, cell.absence)
                    miss = _merge_missing(miss, cell.missing)
                else
                    append(vals, cell.value)
                end if
            next
            if any_absent then return _absent(absence, miss)
            return _apply_call(e.fn, vals, "call " + e.fn)
        end if

        if k = "op" then
            return _eval_op(e, ctx, results, st, depth)
        end if

        return _err("unknown expression kind: " + string(k), "shape")
    end function

    ' THREE-VALUED LOGIC, AND IT IS KLEENE'S RATHER THAN A SHORT-CIRCUIT.
    '
    ' The distinction matters and it is the business author's: `unknown AND
    ' false` is FALSE, because false and anything is false, and a decision
    ' engine that answered "we do not know" there would be refusing to answer a
    ' question the facts already settle -- "we have not got the inspection date,
    ' but the account is closed, so no". Getting that requires evaluating the
    ' RIGHT operand when the left is absent, which a strictly left-to-right
    ' short-circuit does not do.
    '
    ' So short-circuit applies exactly where the left operand DETERMINES the
    ' answer -- ok(false) for AND, ok(true) for OR -- and there the right is
    ' never evaluated, so an error inside it never happens. Everywhere else both
    ' operands are evaluated. Dependency discovery is a separate static walk for
    ' precisely this reason: a branch that did not run this time still carries a
    ' dependency that may matter next time.
    function _eval_op(e, ctx, results, st, depth)
        op = e.op
        args = e.args
        if type(args) != "array" then
            return _err("operator " + string(op) + " has no argument array", "op")
        end if
        n = count(args)
        want = _arity(op)
        if not contains(operators(), op) then
            return _err("unknown operator: " + string(op), "op " + string(op))
        end if
        if n != want then
            return _err("operator " + op + " takes " + string(want) + " operand(s), given " + string(n), "op " + op)
        end if

        where = "op " + op

        ' --- unary absence tests. THE ONLY WAY OUT OF ABSENCE, which is what
        ' makes them necessary rather than convenient: without them a stack
        ' that meets a missing fact can never again produce a boolean.
        if op = "is unknown" then
            a = _eval(args[0], ctx, results, st, depth + 1)
            if a.state = "error" then return a
            if a.state = "absent" then return _ok(a.absence = "unknown")
            return _ok(false)
        end if
        if op = "is nothing" then
            a = _eval(args[0], ctx, results, st, depth + 1)
            if a.state = "error" then return a
            if a.state = "absent" then return _ok(a.absence = "nothing")
            return _ok(false)
        end if

        if op = "not" then
            a = _eval(args[0], ctx, results, st, depth + 1)
            if a.state = "error" then return a
            if a.state = "absent" then return a
            if type(a.value) != "boolean" then
                return _err("not requires a boolean, got " + type(a.value), where)
            end if
            return _ok(not a.value)
        end if

        if op = "neg" then
            a = _eval(args[0], ctx, results, st, depth + 1)
            if a.state = "error" then return a
            if a.state = "absent" then return a
            return _host_unary("neg", a.value, where)
        end if

        if op = "and" then
            a = _eval(args[0], ctx, results, st, depth + 1)
            if a.state = "error" then return a
            if a.state = "ok" then
                if type(a.value) != "boolean" then
                    return _err("and requires booleans, got " + type(a.value), where)
                end if
                if not a.value then return _ok(false)
            end if
            b = _eval(args[1], ctx, results, st, depth + 1)
            if b.state = "error" then return b
            if b.state = "ok" then
                if type(b.value) != "boolean" then
                    return _err("and requires booleans, got " + type(b.value), where)
                end if
                if not b.value then return _ok(false)
            end if
            if a.state = "ok" and b.state = "ok" then return _ok(true)
            if a.state = "ok" then return b
            if b.state = "ok" then return a
            return _absent(_join_absence(a.absence, b.absence), _merge_missing(a.missing, b.missing))
        end if

        if op = "or" then
            a = _eval(args[0], ctx, results, st, depth + 1)
            if a.state = "error" then return a
            if a.state = "ok" then
                if type(a.value) != "boolean" then
                    return _err("or requires booleans, got " + type(a.value), where)
                end if
                if a.value then return _ok(true)
            end if
            b = _eval(args[1], ctx, results, st, depth + 1)
            if b.state = "error" then return b
            if b.state = "ok" then
                if type(b.value) != "boolean" then
                    return _err("or requires booleans, got " + type(b.value), where)
                end if
                if b.value then return _ok(true)
            end if
            if a.state = "ok" and b.state = "ok" then return _ok(false)
            if a.state = "ok" then return b
            if b.state = "ok" then return a
            return _absent(_join_absence(a.absence, b.absence), _merge_missing(a.missing, b.missing))
        end if

        if op = "in" then return _eval_member(e, ctx, results, st, depth, false)
        if op = "not in" then return _eval_member(e, ctx, results, st, depth, true)

        ' --- the negated comparisons. Each is evaluated as the NEGATION of its
        ' positive, via the one table in `_negates`, so the evaluator and the
        ' renderer cannot come to different views of what `!>=` means. The
        ' author's spelling is untouched in the node.
        neg_of = _negates(op)
        if neg_of != "" then
            a = _eval(args[0], ctx, results, st, depth + 1)
            if a.state = "error" then return a
            b = _eval(args[1], ctx, results, st, depth + 1)
            if b.state = "error" then return b
            if a.state = "absent" or b.state = "absent" then
                return _absent(_join_absence(a.absence, b.absence), _merge_missing(a.missing, b.missing))
            end if
            r = _host_binary(neg_of, a.value, b.value, where)
            if r.state != "ok" then return r
            if type(r.value) != "boolean" then
                return _err(op + " did not produce a boolean", where)
            end if
            return _ok(not r.value)
        end if

        ' --- ordinary arithmetic and comparison. Absence propagates; whatever
        ' the host refuses about the VALUES comes back as an error.
        a = _eval(args[0], ctx, results, st, depth + 1)
        if a.state = "error" then return a
        b = _eval(args[1], ctx, results, st, depth + 1)
        if b.state = "error" then return b
        if a.state = "absent" or b.state = "absent" then
            return _absent(_join_absence(a.absence, b.absence), _merge_missing(a.missing, b.missing))
        end if
        return _host_binary(op, a.value, b.value, where)
    end function

    ' MEMBERSHIP IS UNKNOWN WHILE AN UNRESOLVED ELEMENT COULD CHANGE THE
    ' ANSWER -- but a FOUND match settles it however many absences sit beside
    ' it, which is why the walk looks for a hit before it gives up.
    function _eval_member(e, ctx, results, st, depth, negated)
        where = "op " + e.op
        a = _eval(e.args[0], ctx, results, st, depth + 1)
        if a.state = "error" then return a
        b = _eval(e.args[1], ctx, results, st, depth + 1)
        if b.state = "error" then return b
        if b.state = "absent" then return b
        if type(b.value) != "array" then
            return _err(e.op + " requires an array on the right, got " + type(b.value), where)
        end if
        if a.state = "absent" then
            ' An absent subject cannot be looked for, EXCEPT that an empty
            ' collection settles it: nothing is in nothing.
            if count(b.value) = 0 then
                if negated then return _ok(true)
                return _ok(false)
            end if
            return a
        end if
        unresolved_element = false
        absence = "nothing"
        for each el in b.value
            if is_unknown(el) then
                unresolved_element = true
                absence = "unknown"
            else if is_nothing(el) then
                unresolved_element = true
            else
                hit = _host_binary("=", a.value, el, where)
                if hit.state = "error" then return hit
                if hit.value then
                    if negated then return _ok(false)
                    return _ok(true)
                end if
            end if
        next
        if unresolved_element then return _absent(absence, a.missing)
        if negated then return _ok(true)
        return _ok(false)
    end function

    ' ------------------------------------------------- the static walk

    ' Every expression of a stack, in a flat array, each with where it came
    ' from. `validate` and `dependencies` both walk this rather than the stack
    ' directly, so neither can forget a place an expression hides.
    function _expressions(stack)
        out = []
        if type(stack) = "record" and has(stack, "initial") and type(stack.initial) = "record" then
            for each t in keys(stack.initial)
                append(out, { expr: stack.initial[t], rule: "", target: t, where: "initial" })
            next
        end if
        if type(stack) = "record" and has(stack, "rules") and type(stack.rules) = "array" then
            i = 0
            for each r in stack.rules
                rid = "rule " + string(i)
                tgt = ""
                if type(r) = "record" then
                    if has(r, "id") then rid = string(r.id)
                    if has(r, "target") then tgt = string(r.target)
                    if has(r, "condition") then
                        if not is_nothing(r.condition) then
                            append(out, { expr: r.condition, rule: rid, target: tgt, where: "condition" })
                        end if
                    end if
                    if has(r, "replacement") then
                        append(out, { expr: r.replacement, rule: rid, target: tgt, where: "replacement" })
                    end if
                end if
                i = i + 1
            next
        end if
        return out
    end function

    ' Conservative: EVERY branch, including ones a particular evaluation
    ' short-circuits past. The watcher section of the proposal requires this --
    ' branch short-circuiting must not permanently omit a dependency that
    ' becomes relevant later -- and it is also what makes the walk a safe basis
    ' for cycle detection.
    function _walk_refs(e, acc, depth)
        if depth > max_depth() then return acc
        if type(e) != "record" then return acc
        if not has(e, "kind") then return acc
        k = e.kind
        if k = "binding" then
            if not contains(acc.bindings, e.name) then append(acc.bindings, e.name)
            return acc
        end if
        if k = "target" then
            if not contains(acc.targets, e.name) then append(acc.targets, e.name)
            return acc
        end if
        if k = "call" then
            if not contains(acc.calls, e.fn) then append(acc.calls, e.fn)
            if has(e, "args") and type(e.args) = "array" then
                for each a in e.args
                    acc = _walk_refs(a, acc, depth + 1)
                next
            end if
            return acc
        end if
        if k = "op" then
            if has(e, "args") and type(e.args) = "array" then
                for each a in e.args
                    acc = _walk_refs(a, acc, depth + 1)
                next
            end if
            return acc
        end if
        if k = "if" then
            acc = _walk_refs(e.cond, acc, depth + 1)
            acc = _walk_refs(e.then, acc, depth + 1)
            acc = _walk_refs(e.otherwise, acc, depth + 1)
            return acc
        end if
        return acc
    end function

    function _refs_of(e)
        return _walk_refs(e, { bindings: [], targets: [], calls: [] }, 0)
    end function

    ' Everything a TARGET depends on: its initial expression plus every rule
    ' that writes it, conditions included. A condition is a dependency of the
    ' target it guards, which is easy to leave out and would make the ordering
    ' wrong rather than merely incomplete.
    function _target_refs(stack, t)
        acc = { bindings: [], targets: [], calls: [] }
        for each item in _expressions(stack)
            if item.target = t then
                acc = _walk_refs(item.expr, acc, 0)
            end if
        next
        return acc
    end function

    ' DEPENDENCY ORDER, derived rather than declared -- a declared order is a
    ' second statement of a fact the expressions already carry, and two
    ' statements of one fact drift.
    '
    ' A CYCLE IS REFUSED, not resolved. The proposal says the initial
    ' implementation should reject them, and it is the right refusal: a target
    ' defined in terms of itself has no value to report and guessing a starting
    ' point would produce one.
    function target_order(stack)
        pending = []
        if type(stack) = "record" and has(stack, "initial") and type(stack.initial) = "record" then
            for each t in keys(stack.initial)
                append(pending, t)
            next
        end if
        ordered = []
        guard = count(pending) + 1
        rounds = 0
        while count(pending) > 0 and rounds < guard
            moved = []
            for each t in pending
                deps = _target_refs(stack, t).targets
                ready = true
                for each d in deps
                    if d = t then
                        ready = false
                    else
                        if contains(pending, d) and not contains(ordered, d) then
                            ready = false
                        end if
                    end if
                next
                if ready then append(moved, t)
            next
            if count(moved) = 0 then
                return { ok: false, order: ordered, cycle: pending }
            end if
            for each t in moved
                append(ordered, t)
                remove_value(pending, t)
            next
            rounds = rounds + 1
        end while
        if count(pending) > 0 then
            return { ok: false, order: ordered, cycle: pending }
        end if
        return { ok: true, order: ordered, cycle: [] }
    end function

    ' ------------------------------------------------- evaluating a stack

    function _is_always(c)
        if is_nothing(c) then return true
        return false
    end function

    function _new_state()
        return [{ nodes: 0, calls: 0 }]
    end function

    function _result_shell()
        return { ok: true, targets: {}, reads: {}, unresolved: [], errors: [], waiting: [], trace: [], nodes: 0, calls: 0 }
    end function

    function _record_cell(res, t, cell, src)
        res.targets[t] = cell
        if cell.state = "absent" then
            if not contains(res.unresolved, t) then append(res.unresolved, t)
            res.waiting = unique(concat(res.waiting, cell.missing))
        end if
        if cell.state = "error" then
            res.ok = false
            append(res.errors, { target: t, rule: src, where: cell.where, message: cell.message })
        end if
        return res
    end function

    ' THE SHAPE GUARD, AND IT CLOSES A HOLE IN THE "FAILURE IS A VALUE"
    ' PROMISE THAT ONLY APPEARED WHEN SOMETHING MALFORMED WAS TRIED. Measured
    ' before it existed: a stack record with no `rules` array RAISED `unknown
    ' record field: rules` straight out of `evaluate` -- so a caller that had
    ' been told failure is a value got an exception instead -- and a stack that
    ' was not a record at all came back `ok: true`, WHICH IS WORSE: a
    ' successful-looking result for a string.
    '
    ' Both reach here from the same place, and it is door 4: a deserializer is
    ' what writes a stack the builders never built. `validate` already refused
    ' both, which is necessary and not sufficient, because nothing obliges a
    ' caller to validate before evaluating and the one that forgets is exactly
    ' the one holding attacker-supplied data.
    function _shape_problem(stack)
        if type(stack) != "record" then
            return "a rule stack must be a record, not " + type(stack)
        end if
        if not has(stack, "initial") or type(stack["initial"]) != "record" then
            return "a rule stack needs an `initial` record of target name to expression"
        end if
        if not has(stack, "rules") or type(stack["rules"]) != "array" then
            return "a rule stack needs a `rules` array; pass [] for a stack with no overrides"
        end if
        for each r in stack["rules"]
            if type(r) != "record" then
                return "every entry of `rules` must be a record, not " + type(r)
            end if
            if not has(r, "target") or not has(r, "replacement") then
                return "every rule needs a target and a replacement"
            end if
            if not has(r, "id") then
                return "every rule needs an id"
            end if
        next
        return ""
    end function

    function _refused(message)
        res = _result_shell()
        res.ok = false
        append(res.errors, { target: "", rule: "", where: "stack", message: message })
        return res
    end function

    ' THE REFERENCE IMPLEMENTATION, AND IT IS DELIBERATELY NAIVE: rules are
    ' walked in stack order, every condition is evaluated whether or not a
    ' later rule will overwrite the result, and the last applicable rule wins.
    ' That is the semantics written out longhand, which is what makes it a
    ' usable oracle for `evaluate_compiled`. Making it clever would make the
    ' agreement property a comparison of two optimisations.
    function evaluate(stack, ctx, want_trace = false)
        bad = _shape_problem(stack)
        if bad != "" then return _refused(bad)
        res = _result_shell()
        ord = target_order(stack)
        if not ord.ok then
            res.ok = false
            for each t in ord.cycle
                cell = _err("target depends on itself through " + join(ord.cycle, ", "), "cycle")
                res = _record_cell(res, t, cell, "")
            next
        end if
        st = _new_state()
        for each t in ord.order
            res.reads[t] = _target_refs(stack, t).targets
            cell = _eval(stack.initial[t], ctx, res.targets, st, 0)
            if want_trace then
                append(res.trace, { rule: "", target: t, where: "initial", applied: true, state: cell.state, value: cell.value })
            end if
            for each r in stack.rules
                if r.target = t then
                    if _is_always(r.condition) then
                        cell = _eval(r.replacement, ctx, res.targets, st, 0)
                        if want_trace then
                            append(res.trace, { rule: string(r.id), target: t, where: "unconditional", applied: true, state: cell.state, value: cell.value })
                        end if
                    else
                        c = _eval(r.condition, ctx, res.targets, st, 0)
                        applied = false
                        if c.state = "error" then
                            cell = c
                        else if c.state = "absent" then
                            cell = _absent(c.absence, c.missing)
                        else if type(c.value) != "boolean" then
                            cell = _err("a rule condition must be a boolean, not " + type(c.value), "condition")
                        else if c.value then
                            cell = _eval(r.replacement, ctx, res.targets, st, 0)
                            applied = true
                        end if
                        if want_trace then
                            append(res.trace, { rule: string(r.id), target: t, where: "condition", applied: applied, state: c.state, value: cell.value })
                        end if
                    end if
                end if
            next
            res = _record_cell(res, t, cell, "")
        next
        res.nodes = st[0].nodes
        res.calls = st[0].calls
        return res
    end function

    ' ------------------------------------------------------------- condense

    ' Fold a target's ordered overrides into ONE expression. The last rule ends
    ' up outermost, which is what makes a later rule beat an earlier one:
    '
    '     IF c_late THEN v_late ELSE (IF c_early THEN v_early ELSE initial)
    '
    ' AN UNCONDITIONAL RULE COLLAPSES everything beneath it, because everything
    ' beneath it is unreachable. That is the one simplification this increment
    ' performs, it is exactly the case the proposal names (a later
    ' unconditional override makes an earlier unresolved result irrelevant),
    ' and the agreement property is what proves it changed nothing.
    '
    ' Constant folding of a literal `true`/`false` condition is available and
    ' deliberately NOT done here: fewer moving parts while the agreement
    ' property is being established, and a simplification nobody needed yet is
    ' a simplification nobody can test against a real rule.
    function condense(stack)
        bad = _shape_problem(stack)
        if bad != "" then
            return { expressions: {}, order: [], cyclic: false, cycle: [], dependencies: {}, source: stack, version: 1, ok: false, message: bad }
        end if
        exprs = {}
        ord = target_order(stack)
        for each t in keys(stack.initial)
            e = stack.initial[t]
            for each r in stack.rules
                if r.target = t then
                    if _is_always(r.condition) then
                        e = r.replacement
                    else
                        e = if_of(r.condition, r.replacement, e)
                    end if
                end if
            next
            exprs[t] = e
        next
        deps = {}
        for each t in keys(exprs)
            deps[t] = _refs_of(exprs[t])
        next
        return { expressions: exprs, order: ord.order, cyclic: not ord.ok, cycle: ord.cycle, dependencies: deps, source: stack, version: 1, ok: true, message: "" }
    end function

    ' The other path. Same expression evaluator, genuinely different override
    ' logic -- nested conditionals walked latest-first with short-circuiting,
    ' against a forward loop that evaluates everything. If these two ever
    ' disagree, condensation is unsafe and the whole design rests on it.
    function evaluate_compiled(compiled, ctx, want_trace = false)
        if type(compiled) != "record" then
            return _refused("a compiled result must be a record, not " + type(compiled))
        end if
        for each field in ["expressions", "order", "cyclic", "cycle"]
            if not has(compiled, field) then
                return _refused("a compiled result needs `" + field + "`; build it with rules.condense(stack)")
            end if
        next
        if has(compiled, "ok") then
            if not compiled["ok"] then return _refused(string(compiled["message"]))
        end if
        res = _result_shell()
        if compiled.cyclic then
            res.ok = false
            for each t in compiled.cycle
                cell = _err("target depends on itself through " + join(compiled.cycle, ", "), "cycle")
                res = _record_cell(res, t, cell, "")
            next
        end if
        st = _new_state()
        for each t in compiled.order
            res.reads[t] = compiled.dependencies[t].targets
            cell = _eval(compiled.expressions[t], ctx, res.targets, st, 0)
            if want_trace then
                append(res.trace, { rule: "", target: t, where: "compiled", applied: true, state: cell.state, value: cell.value })
            end if
            res = _record_cell(res, t, cell, "")
        next
        res.nodes = st[0].nodes
        res.calls = st[0].calls
        return res
    end function

    ' --------------------------------------------------------------- expand

    ' TWO MODES, AND THE DISTINCTION IS THE POINT. Different stacks condense to
    ' the same expression, so exact expansion is only possible from retained
    ' source. An equivalent expansion must not claim to recover the author's
    ' wording or clause order, and `claims_source` is how a caller is told
    ' which one it got.
    function expand(compiled, mode)
        if mode = "source" then
            if has(compiled, "source") and type(compiled.source) = "record" then
                return { ok: true, claims_source: true, stack: compiled.source }
            end if
            return { ok: false, claims_source: false, stack: nothing, message: "no retained source; ask for an equivalent expansion instead" }
        end if
        if mode = "equivalent" then
            initial = {}
            rule_list = []
            for each t in keys(compiled.expressions) 
                peeled = _peel(compiled.expressions[t], t, rule_list, 0)
                initial[t] = peeled.base
                rule_list = peeled.rules
            next
            return { ok: true, claims_source: false, stack: stack_of(initial, rule_list), message: "equivalent, not original: clause order and wording are not recovered" }
        end if
        return { ok: false, claims_source: false, stack: nothing, message: "mode must be source or equivalent" }
    end function

    ' Unwrap nested conditionals back into rules, innermost first, so the
    ' regenerated stack applies them in an order that reproduces the same
    ' nesting. The ids are generated and say so.
    function _peel(e, t, rule_list, depth)
        if depth > max_depth() then return { base: e, rules: rule_list }
        if type(e) = "record" and has(e, "kind") then
            if e.kind = "if" then
                inner = _peel(e.otherwise, t, rule_list, depth + 1)
                out = inner.rules
                append(out, rule_of("generated-" + t + "-" + string(count(out)), t, e.cond, e.then, { generated: true }))
                return { base: inner.base, rules: out }
            end if
        end if
        return { base: e, rules: rule_list }
    end function

    ' ---------------------------------------------------------- dependencies

    ' What external code connects to watchers. Stable descriptors, nothing
    ' process-local. This library registers no watcher and polls nothing.
    function dependencies(compiled)
        bindings = []
        declared = {}
        calls = []
        targets = {}
        for each t in keys(compiled.expressions)
            acc = _refs_of(compiled.expressions[t])
            bindings = unique(concat(bindings, acc.bindings))
            calls = unique(concat(calls, acc.calls))
            targets[t] = acc.targets
        next
        for each item in _expressions(compiled.source)
            dec = _declared_of(item.expr, declared, 0)
            declared = dec
        next
        return { bindings: bindings, declared: declared, calls: calls, targets: targets }
    end function

    function _declared_of(e, acc, depth)
        if depth > max_depth() then return acc
        if type(e) != "record" then return acc
        if not has(e, "kind") then return acc
        if e.kind = "binding" then
            acc[e.name] = e.declared
            return acc
        end if
        if e.kind = "if" then
            acc = _declared_of(e.cond, acc, depth + 1)
            acc = _declared_of(e.then, acc, depth + 1)
            acc = _declared_of(e.otherwise, acc, depth + 1)
            return acc
        end if
        if has(e, "args") and type(e.args) = "array" then
            for each a in e.args
                acc = _declared_of(a, acc, depth + 1)
            next
        end if
        return acc
    end function

    ' ------------------------------------------------------------- validate

    ' VALIDATION IS WHERE A BUSINESS AUTHOR MEETS THIS LIBRARY, so its failure
    ' path is a primary interface and not an error case. Four requirements, all
    ' from the proposal and all asserted by tests/run_rules.sh:
    '
    '   1. A validation result is a VALUE. A web form checking a rule as it is
    '      typed cannot have the validator end the request.
    '   2. EVERY problem, not the first. gBASIC's parser stops at the first
    '      syntax error and is right to -- recovery invents structure the author
    '      did not write -- but a validator over a tree that ALREADY EXISTS has
    '      no such excuse, and an author fixing six mistakes one reload at a
    '      time stops using the tool.
    '   3. Each problem locates itself in the author's vocabulary: the rule's
    '      own ID rather than an array index (the author reordered the stack and
    '      the index moved), the target, condition-or-replacement, what was
    '      found against what the operator admits, and THE AUTHOR'S OWN
    '      SPELLING, so a message about `!>` says `!>`.
    '   4. The message names the REMEDY. This project's diagnostics say
    '      "not-equal is !=" rather than "unexpected token", and that house
    '      style is inherited here rather than restated.
    function validate(stack)
        problems = []

        if type(stack) != "record" then
            append(problems, _problem("not-a-stack", "", "", "stack", type(stack), "record", "", "build the stack with rules.stack_of(initial, rules)"))
            return { ok: false, problems: problems }
        end if
        if not has(stack, "initial") or type(stack.initial) != "record" then
            append(problems, _problem("no-targets", "", "", "stack", "", "a record of target name to expression", "", "give each target an initial expression"))
            return { ok: false, problems: problems }
        end if
        if count(keys(stack.initial)) = 0 then
            append(problems, _problem("no-targets", "", "", "stack", "0 targets", "at least one", "", "a stack with no targets can decide nothing"))
        end if
        if not has(stack, "rules") or type(stack.rules) != "array" then
            append(problems, _problem("no-rule-array", "", "", "stack", "", "an array of rules", "", "pass [] for a stack with no overrides"))
            return { ok: false, problems: problems }
        end if

        known = keys(stack.initial)

        ' --- the stack's own shape
        seen = []
        i = 0
        for each r in stack.rules
            label = "rule " + string(i)
            if type(r) != "record" then
                append(problems, _problem("not-a-rule", label, "", "rule", type(r), "record", "", "build it with rules.rule_of(...)"))
            else
                if not has(r, "id") then
                    append(problems, _problem("missing-field", label, "", "rule", "no id", "id", "", "every rule needs a stable id, because an index moves when the stack is reordered"))
                else
                    label = string(r.id)
                    if contains(seen, label) then
                        append(problems, _problem("duplicate-id", label, "", "rule", label, "a unique id", "", "two rules with one id cannot be told apart in an explanation"))
                    end if
                    append(seen, label)
                end if
                if not has(r, "target") then
                    append(problems, _problem("missing-field", label, "", "rule", "no target", "target", "", "say which target this rule replaces"))
                else if not contains(known, string(r.target)) then
                    append(problems, _problem("unknown-target", label, string(r.target), "rule", string(r.target), join(known, ", "), "", "give " + string(r.target) + " an initial expression, or write the rule against an existing target"))
                end if
                if not has(r, "replacement") then
                    append(problems, _problem("missing-field", label, "", "replacement", "no replacement", "an expression", "", "a rule with no replacement changes nothing"))
                end if
            end if
            i = i + 1
        next

        ' --- the expressions
        for each item in _expressions(stack)
            problems = _check_expr(item.expr, item, known, problems, 0)
            if item.where = "condition" then
                problems = _check_condition(item.expr, item, problems)
            end if
        next

        ' --- cross-target cycles
        ord = target_order(stack)
        if not ord.ok then
            append(problems, _problem("cycle", "", join(ord.cycle, ", "), "stack", join(ord.cycle, " -> "), "an acyclic set of targets", "", "break the loop: a target defined in terms of itself has no value to report"))
        end if

        return { ok: count(problems) = 0, problems: problems }
    end function

    ' A CONDITION THAT CANNOT POSSIBLY BE A BOOLEAN is an authoring mistake
    ' worth catching before any fact arrives, and the common one is not a
    ' literal -- it is writing the quantity where the comparison was meant
    ' (`amount + cap` for `amount >= cap`). Both shapes are decidable from the
    ' node alone.
    '
    ' DELIBERATELY NARROW. Only the cases that are certain are reported: a
    ' literal of the wrong type, and an operator whose result can never be a
    ' boolean. A binding or a registered call could be anything, and guessing
    ' at those would make this a diagnostic that is sometimes a lie, which is
    ' worse than a terse one -- the rule run_negative.sh's reserved-word tier
    ' states for the same reason.
    function _check_condition(e, item, problems)
        if type(e) != "record" then return problems
        if not has(e, "kind") then return problems
        if e.kind = "value" then
            v = e["value"]
            if is_unknown(v) or is_nothing(v) then return problems
            if type(v) != "boolean" then
                append(problems, _problem("condition-not-boolean", item.rule, item.target, "condition", type(v), "boolean", "", "a condition must answer true or false; compare it to something, or use `is unknown` / `is nothing`"))
            end if
            return problems
        end if
        if e.kind = "op" then
            op = string(e["op"])
            if contains(["+", "-", "*", "/", "neg"], op) then
                append(problems, _problem("condition-not-boolean", item.rule, item.target, "condition", "the result of " + op, "boolean", op, "arithmetic cannot be a condition; did you mean a comparison such as >= ?"))
            end if
        end if
        return problems
    end function

    function _problem(kind_of_problem, rule, target, where, found, wanted, spelling, remedy)
        msg = kind_of_problem
        if found != "" then msg = msg + ": found " + found
        if wanted != "" then msg = msg + ", wanted " + wanted
        if remedy != "" then msg = msg + " -- " + remedy
        return { problem: kind_of_problem, rule: rule, target: target, where: where, found: found, wanted: wanted, spelling: spelling, remedy: remedy, message: msg }
    end function

    function _check_expr(e, item, known, problems, depth)
        if depth > max_depth() then
            append(problems, _problem("too-deep", item.rule, item.target, item.where, "deeper than " + string(max_depth()), "at most " + string(max_depth()) + " levels", "", "a rule nested that deeply cannot be read, and the evaluator will not walk it"))
            return problems
        end if
        if type(e) != "record" then
            append(problems, _problem("not-an-expression", item.rule, item.target, item.where, type(e), "an expression record", "", "wrap a plain value with rules.val(...)"))
            return problems
        end if
        if not has(e, "kind") then
            append(problems, _problem("no-kind", item.rule, item.target, item.where, "a record with no kind", "value, binding, target, op, if or call", "", "build expressions with the rules builders rather than by hand"))
            return problems
        end if
        k = e.kind

        if k = "value" then
            v = e["value"]
            ' DOOR 2, checked statically as well as at evaluation, because the
            ' deserializer is where an attacker writes and a loader that trusts
            ' its own format is the standard shape of this mistake.
            if type(v) = "function" then
                append(problems, _problem("function-value", item.rule, item.target, item.where, "a function value", "data", "", "a rule may hold a function value as data and may never call one; remove it or register a pure function instead"))
            end if
            return problems
        end if

        if k = "binding" then
            if not has(e, "name") or string(e.name) = "" then
                append(problems, _problem("missing-field", item.rule, item.target, item.where, "a binding with no name", "a name", "", "a binding needs a stable identity so a watcher can find it"))
            end if
            if not has(e, "declared") or string(e.declared) = "" then
                append(problems, _problem("missing-field", item.rule, item.target, item.where, "binding " + string(e["name"]) + " with no declared type", "a declared native type", "", "declare the type you expect, so a mismatch is caught before the decision is made"))
            end if
            return problems
        end if

        if k = "target" then
            if not contains(known, string(e.name)) then
                append(problems, _problem("unknown-target", item.rule, item.target, item.where, string(e.name), join(known, ", "), "", "give " + string(e.name) + " an initial expression"))
            end if
            return problems
        end if

        if k = "if" then
            for each part in ["cond", "then", "otherwise"]
                if not has(e, part) then
                    append(problems, _problem("missing-field", item.rule, item.target, item.where, "a conditional with no " + part, part, "", "build it with rules.if_of(cond, then, otherwise)"))
                else
                    problems = _check_expr(e[part], item, known, problems, depth + 1)
                end if
            next
            return problems
        end if

        if k = "call" then
            key = string(e["fn"])
            entry = _registered(key)
            if is_nothing(entry) then
                append(problems, _problem("unregistered-function", item.rule, item.target, item.where, key, join(_registry_keys(), ", "), key, _call_remedy(key)))
            else
                if not has(e, "args") or type(e.args) != "array" then
                    append(problems, _problem("missing-field", item.rule, item.target, item.where, "a call with no argument array", "an array", key, "pass [] for a call with no arguments"))
                else
                    n = count(e.args)
                    if n < entry.least or n > entry.most then
                        append(problems, _problem("call-arity", item.rule, item.target, item.where, string(n) + " argument(s)", string(entry.least) + " to " + string(entry.most), key, key + " takes between " + string(entry.least) + " and " + string(entry.most) + " arguments"))
                    end if
                    for each a in e.args
                        problems = _check_expr(a, item, known, problems, depth + 1)
                    next
                end if
            end if
            return problems
        end if

        if k = "op" then
            op = string(e["op"])
            if not contains(operators(), op) then
                append(problems, _problem("unknown-operator", item.rule, item.target, item.where, op, join(operators(), " "), op, _operator_remedy(op)))
                return problems
            end if
            if not has(e, "args") or type(e.args) != "array" then
                append(problems, _problem("missing-field", item.rule, item.target, item.where, "operator " + op + " with no argument array", "an array", op, "build it with rules.op_of(op, args)"))
                return problems
            end if
            n = count(e.args)
            want = _arity(op)
            if n != want then
                append(problems, _problem("operator-arity", item.rule, item.target, item.where, string(n) + " operand(s)", string(want), op, op + " takes " + string(want) + " operand(s)"))
            end if
            for each a in e.args
                problems = _check_expr(a, item, known, problems, depth + 1)
            next
            if n = 2 then
                problems = _check_literal_pair(e, op, item, problems)
            end if
            return problems
        end if

        append(problems, _problem("unknown-kind", item.rule, item.target, item.where, k, "value, binding, target, op, if or call", "", "build expressions with the rules builders"))
        return problems
    end function

    function _registry_keys()
        out = []
        for each entry in registry()
            append(out, entry.key)
        next
        return out
    end function

    ' THE REMEDY IS THE WHOLE POINT OF THIS FUNCTION. `<>` is refused BY NAME
    ' and reproduces gBASIC's own sentence, because the readers arriving from
    ' QBasic who will write `<>` in a rule are the same readers who will write
    ' it in gBASIC, and a rule language whose primary spelling is the one the
    ' host rejects teaches a habit that fails the moment they do.
    function _operator_remedy(op)
        if op = "<>" then return "'<>' is not an operator; not-equal is != -- a != b"
        if op = "!in" then return "membership is spelled 'not in', not '!in'"
        if op = "notin" then return "membership is spelled 'not in', with a space"
        if op = "==" then return "equality is a single = in gBASIC -- a = b"
        if op = "&&" then return "conjunction is spelled 'and'"
        if op = "||" then return "disjunction is spelled 'or'"
        if op = "!" then return "negation is spelled 'not'"
        if op = "%" then return "there is no modulo operator in this increment"
        if op = "unary-" then return "unary negation is spelled 'neg'"
        return "use one of the listed operators"
    end function

    function _call_remedy(key)
        for each entry in registry()
            if starts_with(entry.key, left(key, 2)) then
                return "not registered; did you mean '" + entry.key + "'? Only registered pure functions can be called from a rule"
            end if
        next
        return "only registered pure functions can be called from a rule, and " + key + " is not one"
    end function

    ' WHAT CAN BE CAUGHT BEFORE ANY FACT ARRIVES. Two literal operands are
    ' fully known at validation, so a currency mismatch or a number against a
    ' string is a defect in the RULE and belongs here -- which is the other
    ' half of the absence/invalidity boundary, seen from the static end. The
    ' mismatches that involve a binding cannot be caught here (a binding
    ' declared `money` does not say which currency) and surface as evaluation
    ' errors instead; both are errors, never absence.
    function _check_literal_pair(e, op, item, problems)
        a = e.args[0]
        b = e.args[1]
        if type(a) != "record" or type(b) != "record" then return problems
        if not has(a, "kind") or not has(b, "kind") then return problems
        if a.kind != "value" or b.kind != "value" then return problems
        av = a["value"]
        bv = b["value"]
        if is_unknown(av) or is_nothing(av) or is_unknown(bv) or is_nothing(bv) then return problems
        if op = "=" or op = "!=" then return problems
        if op = "in" or op = "not in" then return problems
        if type(av) = "money" and type(bv) = "money" then
            ca = money.currency(av)
            cb = money.currency(bv)
            if ca != cb then
                append(problems, _problem("currency-mismatch", item.rule, item.target, item.where, ca + " and " + cb, "one currency", op, "convert one side first -- money.convert(amount, \"" + ca + "\", on_date)"))
            end if
            return problems
        end if
        if type(av) != type(bv) then
            append(problems, _problem("type-mismatch", item.rule, item.target, item.where, type(av) + " and " + type(bv), "two operands of one type", op, "this library does not convert text into numbers, dates or money; make both sides the same type"))
        end if
        return problems
    end function

    ' --------------------------------------------------------------- render

    ' THE AUTHOR'S SPELLING SURVIVES. `!>` renders as `!>` even though the
    ' evaluator computed it as the negation of `>`, because the person reading
    ' this is the person who wrote it.
    function render(e)
        return _render(e, 0)
    end function

    function _render(e, depth)
        if depth > max_depth() then return "..."
        if type(e) != "record" then return "?"
        if not has(e, "kind") then return "?"
        k = e.kind
        if k = "value" then
            v = e["value"]
            if is_unknown(v) then return "unknown"
            if is_nothing(v) then return "nothing"
            if type(v) = "string" then return "\"" + v + "\""
            if type(v) = "array" then
                parts = []
                for each el in v
                    if is_unknown(el) then
                        append(parts, "unknown")
                    else if is_nothing(el) then
                        append(parts, "nothing")
                    else if type(el) = "string" then
                        append(parts, "\"" + el + "\"")
                    else
                        append(parts, string(el))
                    end if
                next
                return "[" + join(parts, ", ") + "]"
            end if
            return string(v)
        end if
        if k = "binding" then return e.label
        if k = "target" then return e.name
        if k = "call" then
            parts = []
            for each a in e.args
                append(parts, _render(a, depth + 1))
            next
            return e.fn + "(" + join(parts, ", ") + ")"
        end if
        if k = "if" then
            return "IF " + _render(e.cond, depth + 1) + " THEN " + _render(e.then, depth + 1) + " ELSE " + _render(e.otherwise, depth + 1)
        end if
        if k = "op" then
            op = e.op
            if op = "neg" then return "-" + _render(e.args[0], depth + 1)
            if op = "not" then return "NOT " + _render(e.args[0], depth + 1)
            if op = "is unknown" then return _render(e.args[0], depth + 1) + " IS UNKNOWN"
            if op = "is nothing" then return _render(e.args[0], depth + 1) + " IS NOTHING"
            shown = op
            if op = "and" then shown = "AND"
            if op = "or" then shown = "OR"
            if op = "in" then shown = "IN"
            if op = "not in" then shown = "NOT IN"
            return "(" + _render(e.args[0], depth + 1) + " " + shown + " " + _render(e.args[1], depth + 1) + ")"
        end if
        return "?"
    end function

    function render_stack(stack)
        lines = []
        for each t in target_order(stack).order
            append(lines, t + " = " + render(stack.initial[t]))
        next
        for each r in stack.rules
            if _is_always(r.condition) then
                append(lines, "ALWAYS SET " + r.target + " = " + render(r.replacement) + "   [" + string(r.id) + "]")
            else
                append(lines, "WHEN " + render(r.condition) + " SET " + r.target + " = " + render(r.replacement) + "   [" + string(r.id) + "]")
            end if
        next
        return join(lines, "\n")
    end function

    ' -------------------------------------------------------------- explain

    ' A DECISION EXPLANATION COMES FROM THE EVALUATION, NOT ALONGSIDE IT. It
    ' reads the trace the evaluator produced on the one walk it made, so it can
    ' only name rules that were actually applied. A second traversal that
    ' reconstructed what it thought happened would agree with the first until
    ' the day a short-circuit, an absence or a fold made it disagree, and on
    ' that day it would still read fluently -- which is worse, because an
    ' explanation that plausibly misstates why a payment was capped is a
    ' document somebody may rely on in a dispute.
    function explain_decision(res, target)
        if not has(res.targets, target) then
            return { ok: false, lines: [], applied: [], state: "", message: "no such target in this result: " + target }
        end if
        if count(res.trace) = 0 then
            return { ok: false, lines: [], applied: [], state: "", message: "this result carries no trace; evaluate with want_trace set, because an explanation may not be reconstructed after the fact" }
        end if
        acc = { lines: [], applied: [], told: [] }
        acc = _explain_target(res, target, acc, 0)
        cell = res.targets[target]
        return { ok: true, lines: acc.lines, applied: acc.applied, state: cell.state, message: join(acc.lines, " ") }
    end function

    ' A DERIVED TARGET IS EXPLAINED THROUGH ITS COMPONENTS, in the order they
    ' were evaluated. Without this a payment explanation reads "started from the
    ' agreed payment" and never mentions the cap that decided it -- true in the
    ' narrow sense and useless to a reader, which is the failure mode this
    ' document warns about for the explanation layer specifically.
    '
    ' `told` stops a component shared by two targets being explained twice and
    ' bounds the walk, since the graph is acyclic by refusal rather than by
    ' construction.
    function _explain_target(res, target, acc, depth)
        if depth > max_depth() then return acc
        if contains(acc.told, target) then return acc
        append(acc.told, target)

        if has(res.reads, target) then
            for each dep in res.reads[target]
                if dep != target then
                    acc = _explain_target(res, dep, acc, depth + 1)
                end if
            next
        end if

        ' A target nobody overrode has one thing to say, and saying it twice
        ' -- once as the agreed starting point and once as the answer -- is how
        ' a template-driven explanation starts reading like a machine.
        touched = 0
        for each tr in res.trace
            if tr.target = target and tr.where != "initial" and tr.where != "compiled" then
                touched = touched + 1
            end if
        next

        for each tr in res.trace
            if tr.target = target then
                if tr.where = "initial" or tr.where = "compiled" then
                    if touched > 0 then
                        append(acc.lines, "The agreed " + target + " is " + _say_value(tr.state, tr.value) + ".")
                    end if
                else if tr.where = "unconditional" then
                    append(acc.lines, "Rule " + tr.rule + " applies unconditionally.")
                    append(acc.applied, tr.rule)
                else if tr.applied then
                    append(acc.lines, "Rule " + tr.rule + " applies.")
                    append(acc.applied, tr.rule)
                else if tr.state = "absent" then
                    append(acc.lines, "Rule " + tr.rule + " cannot be decided: a fact it needs is missing.")
                else if tr.state = "error" then
                    append(acc.lines, "Rule " + tr.rule + " cannot be evaluated.")
                else
                    append(acc.lines, "Rule " + tr.rule + " does not apply.")
                end if
            end if
        next

        cell = res.targets[target]
        feeds = []
        if has(res.reads, target) then feeds = res.reads[target]
        if count(feeds) > 0 then
            parts = []
            for each dep in feeds
                append(parts, dep + " is " + _say(res.targets[dep]))
            next
            append(acc.lines, "With " + join(parts, " and ") + ", " + target + " is " + _say(cell) + ".")
        else if touched > 0 then
            append(acc.lines, "So " + target + " is " + _say(cell) + ".")
        else
            append(acc.lines, "The agreed " + target + " is " + _say(cell) + ".")
        end if
        return acc
    end function

    function _say_value(state, v)
        if state = "ok" then return string(v)
        if state = "absent" then return "not yet known"
        return "undecidable"
    end function

    function _say(cell)
        if cell.state = "ok" then return string(cell.value)
        if cell.state = "absent" then
            if count(cell.missing) = 0 then return "not known"
            return "not yet known (waiting on " + join(cell.missing, ", ") + ")"
        end if
        return "undecidable: " + cell.message
    end function

end library

' `tools` -- one declaration of what a model may call, and the safe way to call
' it. Self-checking; run by tests/run_tools.sh.
'
' SELF-CHECKING AND FORCED. Every defect in this layer produces a PLAUSIBLE
' TOOL RESULT: a validation that passes what it should refuse, a default that
' silently does not apply, an error reported as success. A golden records
' whichever result came out and then defends it.
'
' THE LOAD-BEARING TIER IS THE ORACLE, near the bottom: `tools.schema` is what
' the model is TOLD and `tools.dispatch` is what is ENFORCED, and the whole
' reason this layer exists is that those were two hand-written things in
' `llm.tool` with nothing checking they agree. So the fixture reads the schema
' back and drives dispatch FROM it -- every parameter the schema calls
' required must be one dispatch refuses to run without, and every type it
' names must be one dispatch rejects a wrong value for. A `params` declaration
' that generated a schema nobody enforced would pass every other check here.

load tools

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

' ---- the tools under test --------------------------------------------------

function lookup(args)
    return "customer " + args.id + " limit " + string(args.limit)
end function

function boom(args)
    error "the tool raised"
    return "unreached"
end function

function miss(args)
    r = {}
    r["error"] = "no such customer"
    return r
end function

function shapes(args)
    ' One line: gBASIC continues a statement only inside an unclosed bracket
    ' (PLAT-CONT), so a trailing `+` does not continue it.
    return "s=" + args.s + " n=" + string(args.n) + " b=" + string(args.b) + " a=" + string(count(args.a)) + " r=" + string(count(keys(args.r)))
end function

function sees(args)
    p = principal()
    if p = nothing then return "nobody"
    return p.user
end function

ts = tools.define("crm", [
    { name: "lookup", describe: "Find a customer",
      params: [
        { name: "id", type: "string", describe: "the customer id", required: true },
        { name: "limit", type: "number", describe: "how many rows", required: false, default: 10 }
      ],
      reads: ["customers"], mutates: [], fn: lookup },
    { name: "boom", describe: "always raises",
      params: [], reads: [], mutates: [], fn: boom },
    { name: "miss", describe: "reports a miss",
      params: [], reads: ["customers"], mutates: [], fn: miss },
    { name: "shapes", describe: "every parameter type",
      params: [
        { name: "s", type: "string", describe: "a string", required: true },
        { name: "n", type: "number", describe: "a number", required: true },
        { name: "b", type: "boolean", describe: "a boolean", required: true },
        { name: "a", type: "array", describe: "an array", required: true },
        { name: "r", type: "record", describe: "a record", required: true }
      ],
      reads: [], mutates: ["nothing really"], fn: shapes },
    { name: "sees", describe: "reports the principal",
      params: [], reads: [], mutates: [], fn: sees }
])

' ---- the declaration -------------------------------------------------------
check("the toolset lists its tools in declared order",
      join(tools.names(ts), ","), "lookup,boom,miss,shapes,sees")
check("effects are readable without opening the body",
      join(tools.reads(ts, "lookup"), ","), "customers")
check("and so is the claim that a tool changes nothing",
      count(tools.mutates(ts, "lookup")), 0)
check("a tool that declares a mutation says so",
      count(tools.mutates(ts, "shapes")), 1)

' ---- dispatch --------------------------------------------------------------
r = tools.dispatch(ts, "lookup", { id: "c1", limit: 5 })
check("a good call succeeds", r.is_error, false)
check("and returns what the tool returned", r.content, "customer c1 limit 5")
check("the result is a tool_result part", r.type, "tool_result")
check("naming the tool", r.name, "lookup")

d = tools.dispatch(ts, "lookup", { id: "c2" })
check("a declared default is applied", d.content, "customer c2 limit 10")

check("a missing required parameter is refused",
      tools.dispatch(ts, "lookup", {}).content, "missing required parameter 'id'")
check("and refusal is reported as an error, not a value",
      tools.dispatch(ts, "lookup", {}).is_error, true)
check("a wrongly typed parameter is refused",
      tools.dispatch(ts, "lookup", { id: 7 }).content, "parameter 'id' should be a string")
check("a parameter nobody declared is refused",
      tools.dispatch(ts, "lookup", { id: "c", nope: 1 }).content, "unknown parameter 'nope'")
check("an unknown tool is refused",
      tools.dispatch(ts, "nosuch", {}).content, "unknown tool 'nosuch'")
check("arguments that are not a record are refused",
      tools.dispatch(ts, "lookup", "id=c1").content, "arguments must be a record")

' ---- A RAISING TOOL IS SURVIVABLE ------------------------------------------
' `llm.bas` documents that a tool "must not raise", which was true when it was
' written: a raise could not be caught at all. PLAT-ERR changed that and the
' rule outlived it. Measured on the older path, a raising tool still ends the
' whole run -- so this is the difference between a bad tool call and a dead
' agent, and the SECOND check is the one that says so.
b = tools.dispatch(ts, "boom", {})
check("a tool that raises comes back as a result", b.is_error, true)
check("carrying what went wrong", b.content, "the tool failed: the tool raised")
after = tools.dispatch(ts, "lookup", { id: "c9" })
check("AND THE NEXT CALL STILL WORKS -- the run is alive", after.is_error, false)
check("with the right answer", after.content, "customer c9 limit 10")

' A tool may also report a miss by returning an error, which is not a defect.
m = tools.dispatch(ts, "miss", {})
check("a tool reporting a miss is an error result", m.is_error, true)
check("with the tool's own words", m.content, "no such customer")

' ---- the principal reaches the body ----------------------------------------
check("a tool called outside any scope sees no principal",
      tools.dispatch(ts, "sees", {}).content, "nobody")
with principal({ user: "jo" })
    check("and inside one it sees who is acting",
          tools.dispatch(ts, "sees", {}).content, "jo")
end with

' ---- THE ORACLE: what the model is told is what is enforced -----------------
' Driven from `tools.schema`, not from the declaration -- a second reading of
' the same thing. `sample` and `wrong` are the only mapping this needs, and
' both are deliberately written here rather than borrowed from the library.

function sample(t)
    if t = "string" then return "x"
    if t = "number" then return 1
    if t = "boolean" then return true
    if t = "array" then return [1]
    return { k: 1 }
end function

function wrong(t)
    ' A value of some OTHER declared type. Number is the odd one out because
    ' `0 = false` is the language's one coercion; a string is safe against
    ' every type but string.
    if t = "string" then return 1
    return "not-a-" + t
end function

published = tools.schema(ts)
check("the schema publishes every tool", count(published), 5)

oracle_checks = 0
for each pub in published
    props = pub.parameters.properties
    req = pub.parameters.required

    ' Build a call satisfying exactly what the schema declares required.
    good = {}
    for each pname in req
        good[pname] = sample(props[pname].type)
    end for
    ok_result = tools.dispatch(ts, pub.name, good)
    ' `boom` and `miss` fail in the BODY, not in validation -- what is asserted
    ' is that validation let them through, which their own message shows.
    passed_validation = true
    if ok_result.is_error then
        if contains(ok_result.content, "missing required") then passed_validation = false
        if contains(ok_result.content, "should be a") then passed_validation = false
        if contains(ok_result.content, "unknown parameter") then passed_validation = false
    end if
    oracle_checks = oracle_checks + 1
    check("schema-satisfying arguments pass validation for " + pub.name,
          passed_validation, true)

    ' Every parameter the schema calls REQUIRED must be one dispatch refuses
    ' without.
    for each pname in req
        short = {}
        for each other in req
            if other != pname then
                short[other] = sample(props[other].type)
            end if
        end for
        got = tools.dispatch(ts, pub.name, short)
        oracle_checks = oracle_checks + 1
        check("dropping required '" + pname + "' from " + pub.name + " is refused",
              got.content, "missing required parameter '" + pname + "'")
    end for

    ' Every type the schema NAMES must be one dispatch rejects a wrong value
    ' for.
    for each pname in keys(props)
        bad = {}
        for each other in req
            bad[other] = sample(props[other].type)
        end for
        bad[pname] = wrong(props[pname].type)
        got2 = tools.dispatch(ts, pub.name, bad)
        oracle_checks = oracle_checks + 1
        check("a wrong " + props[pname].type + " for '" + pname + "' in " + pub.name + " is refused",
              got2.content, "parameter '" + pname + "' should be a " + props[pname].type)
    end for
end for

' A floor, so the oracle cannot pass by having generated nothing.
check("the oracle actually generated its checks", oracle_checks >= 15, true)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

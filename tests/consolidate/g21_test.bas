' consolidate.merge says what it could not read and which column it used (gap
' G21 from the gBASIC books): per-cell refusals, the mapping per source,
' `exact: true` on a rule, and a source in which two columns match one rule
' rejected rather than resolved by a pick. Every answer is written here by hand.
' Run by tests/run_consolidate.sh with GBASIC_PATH=stdlib.
load consolidate
load frame

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if type(got) = type(want) then
        if got = want then
            return nothing
        end if
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + type(got) + " " + string(got) + ", want " + type(want) + " " + string(want)
    return nothing
end function

a = frame.from_rows([
    { "Loan": "L1", "Bal $": "1,500.00", "Rate": "5.25", "Term": "36" },
    { "Loan": "L2", "Bal $": "TBD", "Rate": "n/a", "Term": "sixty" },
    { "Loan": "L3", "Bal $": "", "Rate": "4.75", "Term": "48" },
    { "Loan": "L4", "Bal $": unknown, "Rate": "  ", "Term": unknown }
])
spec = { columns: {
    loan: { from: ["Loan"], kind: "text", required: true },
    balance: { from: ["Bal"], kind: "money", required: true },
    rate: { kind: "percent", scale: "whole" },
    term: { kind: "number" }
} }
r = consolidate.merge([{ name: "A", frame: a }], spec)
check("accepted", r.ok, true)
check("the mapping names the column each canonical one came from", r.mapping.A,
      { loan: "Loan", balance: "Bal $", rate: "Rate", term: "Term" })
got = []
for each x in r.refused
    append(got, x.source + " " + string(x.row) + " " + x.column + "->" + x.canon + " " + string(x.value) + ": " + x.why)
end for
check("every unreadable cell, and only those: not the empty, blank or absent ones", got,
      ["A 1 Bal $->balance TBD: not money", "A 1 Rate->rate n/a: not a percent", "A 1 Term->term sixty: not a number"])
rows = frame.to_rows(r.frame)
check("  the refused cell's merged value is unknown", is_unknown(rows[1].balance), true)
check("  as is the empty one's, which is not refused", is_unknown(rows[2].balance), true)
check("  and a readable one is read", rows[0].balance, 1500)
check("the notes count them", contains(r.notes, "A: 3 value(s) could not be read and are unknown: see refused"), true)

' a fuzzy match is visible in the mapping
p = frame.from_rows([{ "Parts": 12, "Tech": "Ana" }])
r = consolidate.merge([{ name: "P", frame: p }], { columns: { parts: { from: ["Parts $"], kind: "number" } } })
check("from: [\"Parts $\"] takes a column named Parts, and says so", r.mapping.P.parts, "Parts")

' exact: the declared alias is the whole mapping
r = consolidate.merge([{ name: "P", frame: p }],
                      { columns: { parts: { from: ["Parts $"], kind: "number", exact: true, required: true } } })
check("exact: \"Parts $\" does not match Parts", r.rejected, ["P"])
check("  and the source is rejected as missing it", contains(r.notes, "P: REJECTED, missing required column(s) parts"), true)
t = frame.from_rows([{ "Technician": "Ana", "Hours": 2 }])
r = consolidate.merge([{ name: "T", frame: t }], { columns: { technician: { from: ["Engineer"], kind: "text" } } })
check("without exact the canonical name is tried after the aliases", r.mapping.T.technician, "Technician")
r = consolidate.merge([{ name: "T", frame: t }],
                      { columns: { technician: { from: ["Engineer"], kind: "text", exact: true } } })
check("with exact it is not", has(r.mapping.T, "technician"), false)
r = consolidate.merge([{ name: "T", frame: t }],
                      { columns: { Technician: { from: ["Engineer"], kind: "text", exact: true } } })
check("  not even written exactly as the column is", has(r.mapping.T, "Technician"), false)
r = consolidate.merge([{ name: "T", frame: t }], { columns: { hours: { kind: "number", exact: true } } })
check("exact with no aliases matches the canonical name as written", has(r.mapping.T, "hours"), false)
r = consolidate.merge([{ name: "T", frame: t }], { columns: { Hours: { kind: "number", exact: true } } })
check("  and does match it written the same", r.mapping.T.Hours, "Hours")

' two columns matching one rule are refused, not picked between
two = frame.from_rows([{ "Parts": 12, "Parts $": 15.5, "Tech": "Ana" }])
r = consolidate.merge([{ name: "D", frame: two }, { name: "P", frame: p }],
                      { columns: { parts: { from: ["Parts $"], kind: "number" } } })
check("two columns match one rule: the source is rejected", r.rejected, ["D"])
check("  naming both", contains(r.notes,
      "D: REJECTED, two columns match one rule: parts (Parts, Parts $) -- name one in `from` with `exact: true`"), true)
check("  and the other source still merges", r.accepted, ["P"])
r = consolidate.merge([{ name: "D", frame: two }],
                      { columns: { parts: { from: ["Parts $"], kind: "number", exact: true } } })
check("exact settles it", r.mapping.D.parts, "Parts $")
check("  and takes that column's values", frame.to_rows(r.frame)[0].parts, 15.5)

print "checks: " + string(checks.n)
print "mismatches: " + string(checks.bad)

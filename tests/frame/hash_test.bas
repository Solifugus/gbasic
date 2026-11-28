' frame.summarize and frame.join by hash (docs/columnar_analytics_design.md
' §5 A): what they answer is what the scans answered. Every answer here is
' worked by hand from the frames below.
load frame

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if string(got) != string(want) then
        checks.bad = checks.bad + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function total(sub)
    return sum(sub.v)
end function

function rows(sub)
    return len(sub.v)
end function

' --- summarize: groups in first-seen order; unknown keys one group; an exact 3
' and a computed 3.0 one group (=, which _sig follows)
df = { k: ["b", "a", "b", unknown, "a", unknown, 3, 6 / 2], v: [1, 2, 3, 4, 5, 6, 7, 8] }
g = frame.summarize(df, "k", { total: total, n: rows })
check("the keys, first seen first", g.k, ["b", "a", unknown, 3])
check("each group's sum", g.total, [4, 7, 10, 15])
check("each group's rows", g.n, [2, 2, 2, 2])
check("the columns: the key, then the aggregates", keys(g), ["k", "total", "n"])
check("an empty frame has no groups", frame.summarize({ k: [], v: [] }, "k", { total: total }).k, [])
one = frame.summarize({ k: ["x", "x", "x"], v: [1, 2, 3] }, "k", { total: total })
check("one group", one.total, [6])

' --- join: left order, each left row's matches in right order; duplicates on
' both sides give every pair; a key with no match gives no row; unknown keys
' match each other, as the pairwise comparison did
left = { id: [1, 2, 2, 3, unknown], x: ["p", "q", "r", "s", "t"] }
right = { id: [2, 1, 2, 4, unknown], y: ["A", "B", "C", "D", "E"] }
j = frame.join(left, right, "id")
check("join keys", j.id, [1, 2, 2, 2, 2, unknown])
check("join left column", j.x, ["p", "q", "q", "r", "r", "t"])
check("join right column", j.y, ["B", "A", "C", "A", "C", "E"])
check("join columns", keys(j), ["id", "x", "y"])
both = frame.join({ id: [1], v: [10] }, { id: [1], v: [20] }, "id")
check("a right column named like a left one", keys(both), ["id", "v", "v_right"])
check("...its value", both.v_right, [20])
check("nothing matches", frame.join({ id: [1], x: ["a"] }, { id: [2], y: ["b"] }, "id").x, [])
check("exact 3 meets computed 3.0", frame.join({ id: [3], x: ["a"] }, { id: [6 / 2], y: ["b"] }, "id").y, ["b"])

print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"

' fake.plant -- a clean population with a KNOWN defect in a KNOWN place
' (docs/fake_data_design.md §6).
'
' SELF-CHECKING, and here it is forced twice over. A planting defect produces a
' population that STILL LOOKS LIKE A POPULATION: an anomaly that landed on the
' wrong row, one that landed on four rows instead of three, or a "clean" row
' quietly altered on the way past are all ordinary business data, and a golden
' would record every one of them as expected.
'
' THE THREE THINGS §6 SAYS THIS HAS TO PROVE, and every tier below is one of
' them: exactly n rows carry the anomaly, they are the rows the report NAMES,
' and every other row is byte-for-byte what it was. The third is the one a
' careless implementation fails -- it is easy to plant correctly and detach a
' shared array on the way, and nothing about the result looks wrong.
'
' PLUS THE PROPERTY THE WHOLE DESIGN RESTS ON: the planted rows carry NO
' MARKER. If a detector can find them by shape rather than by detecting, the
' data cannot test the detector, so `no marker` gets its own tier.

load fake

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

start {date}= "2026-01-01"
finish {date}= "2026-06-30"
customers = fake.customers(11, 40)
clean = fake.invoices(11, customers, 200, start, finish, "USD")

' Indices where two populations of the same length differ, by deep record
' comparison -- PLAT-EQ makes `=` on records answer about the VALUES.
function differing(before, after)
    out = []
    for i = 0 to count(before) - 1
        if not (before[i] = after[i]) then
            append(out, i)
        end if
    end for
    return out
end function

function ids_of(report)
    out = []
    for each p in report
        append(out, p.id)
    next
    return out
end function

function indexes_of(report)
    out = []
    for each p in report
        append(out, p.index)
    next
    return out
end function

' The row a report entry points back at, found by id rather than by index --
' `sequence_gap` shifts every index after a removal, so an index would be a
' fact about the result and not about the source.
function source_row(rows, want)
    for each r in rows
        if r.id = want then
            return r
        end if
    next
    return unknown
end function

function id_set(rows)
    seen = { }
    for each r in rows
        seen[r.id] = true
    next
    return seen
end function

' ------------------------------------------------------- round dollar
r = fake.plant(clean, { anomaly: "round_dollar", count: 6, at: 5 })

check("the population keeps its length", count(r.rows), count(clean))
check("six anomalies are reported", count(r.planted), 6)
check("exactly six rows changed", count(differing(clean, r.rows)), 6)
check("and they are the rows the report names",
      string(differing(clean, r.rows)), string(indexes_of(r.planted)))

round_ok = 0
was_round_already = 0
for each p in r.planted
    v = number(money.text(p.now))
    grain = 10
    if v >= 1000 then
        grain = 1000
    else if v >= 100 then
        grain = 100
    end if
    if v = round(v / grain, 0) * grain then
        round_ok = round_ok + 1
    end if
    if number(money.text(p.was)) = v then
        was_round_already = was_round_already + 1
    end if
next
check("every planted amount is a round figure", round_ok, 6)
check("and none of them was round before", was_round_already, 0)

' The report's `was` must be what the ORIGINAL row held, not a reconstruction.
was_matches = 0
for each p in r.planted
    if p.was = clean[p.index].total then
        was_matches = was_matches + 1
    end if
next
check("the report's `was` is the value that was there", was_matches, 6)

' A changed row must change ONLY its amount.
rest_intact = 0
for each p in r.planted
    b = clean[p.index]
    a = r.rows[p.index]
    if a.id = b.id and a.customer = b.customer and a.on_date = b.on_date and a.lines = b.lines then
        rest_intact = rest_intact + 1
    end if
next
check("only the amount moved on a planted row", rest_intact, 6)

' ------------------------------------------------------------ no marker
' The rows carry nothing that says they were planted. If they did, a detector
' could pass by reading the label.
same_shape = 0
for each p in r.planted
    if string(sort(keys(r.rows[p.index]))) = string(sort(keys(clean[0]))) then
        same_shape = same_shape + 1
    end if
next
check("a planted row has exactly its neighbours' fields", same_shape, 6)

' ------------------------------------------------------------ just under
limit = money.of("USD", "5000")
u = fake.plant(clean, { anomaly: "just_under", count: 5, at: 21, threshold: limit })
check("five just-under anomalies", count(u.planted), 5)
check("exactly five rows changed", count(differing(clean, u.rows)), 5)

under = 0
close_by = 0
for each p in u.planted
    v = number(money.text(p.now))
    if v < 5000 then
        under = under + 1
    end if
    if v >= 4750 then
        close_by = close_by + 1
    end if
next
check("every planted amount is strictly under the limit", under, 5)
check("and within five percent of it, which is what makes it suspicious", close_by, 5)

' --------------------------------------------------------------- weekend
w = fake.plant(clean, { anomaly: "weekend", count: 7, at: 33 })
check("seven weekend anomalies", count(w.planted), 7)
check("exactly seven rows changed", count(differing(clean, w.rows)), 7)

on_weekend = 0
was_weekday = 0
for each p in w.planted
    if p.now.weekday >= 6 then
        on_weekend = on_weekend + 1
    end if
    if p.was.weekday <= 5 then
        was_weekday = was_weekday + 1
    end if
next
check("every planted date falls on a weekend", on_weekend, 7)
check("and every one of them was a business day before", was_weekday, 7)

' The control that makes the tier mean something: the UNPLANTED rows must
' still be business days, or a generator that weekended everything would pass.
planted_here = { }
for each p in w.planted
    planted_here[string(p.index)] = true
next
stray = 0
for i = 0 to count(w.rows) - 1
    if is_unknown(planted_here[string(i)]) and w.rows[i].on_date.weekday >= 6 then
        stray = stray + 1
    end if
end for
check("no unplanted row drifted onto a weekend", stray, 0)

' ------------------------------------------------------------- duplicate
d = fake.plant(clean, { anomaly: "duplicate", count: 4, at: 7 })
check("the population grows by exactly four", count(d.rows), count(clean) + 4)
check("four duplicates are reported", count(d.planted), 4)

' Every original row survives, in place and unchanged.
head_intact = 0
for i = 0 to count(clean) - 1
    if clean[i] = d.rows[i] then
        head_intact = head_intact + 1
    end if
end for
check("every original row is untouched", head_intact, count(clean))

matched = 0
new_id = 0
for each p in d.planted
    copy = d.rows[p.index]
    src = source_row(clean, p.source)
    if copy.customer = src.customer and copy.total = src.total and copy.lines = src.lines then
        matched = matched + 1
    end if
    if is_unknown(id_set(clean)[p.id]) then
        new_id = new_id + 1
    end if
next
check("a duplicate matches its source on party, amount and lines", matched, 4)
check("and carries an id the population did not already have", new_id, 4)

' The id must CONTINUE the sequence rather than announce itself: same prefix,
' same width. An id shaped differently is a marker by another name.
shaped = 0
for each p in d.planted
    if len(p.id) = len(clean[0].id) and left(p.id, 3) = left(clean[0].id, 3) then
        shaped = shaped + 1
    end if
next
check("a duplicate's id is shaped like every other id", shaped, 4)

' --------------------------------------------------------- sequence gap
g = fake.plant(clean, { anomaly: "sequence_gap", count: 3, at: 12 })
check("the population shrinks by exactly three", count(g.rows), count(clean) - 3)
check("three removals are reported", count(g.planted), 3)

left_behind = id_set(g.rows)
gone = 0
for each p in g.planted
    if is_unknown(left_behind[p.id]) then
        gone = gone + 1
    end if
next
check("every named row is actually absent", gone, 3)

survivors = 0
removed = { }
for each p in g.planted
    removed[p.id] = true
next
for each row in clean
    if is_unknown(removed[row.id]) and not is_unknown(left_behind[row.id]) then
        survivors = survivors + 1
    end if
next
check("and every row NOT named is still there", survivors, count(clean) - 3)

' The removed row comes back whole in the report, which is what lets a caller
' put it back or assert on what was lost.
whole = 0
for each p in g.planted
    if p.was = source_row(clean, p.id) then
        whole = whole + 1
    end if
next
check("the report carries the whole removed row", whole, 3)

' --------------------------------------------- the caller's array is untouched
check("planting five times left the original population alone", count(clean), 200)
check("and its first row unchanged", clean[0].id, "INV0000")

' ------------------------------------------------------------ determinism
again = fake.plant(clean, { anomaly: "round_dollar", count: 6, at: 5 })
check("the same seed plants the same rows", string(ids_of(again.planted)), string(ids_of(r.planted)))

other = fake.plant(clean, { anomaly: "round_dollar", count: 6, at: 6 })
check("a different seed does not", string(ids_of(other.planted)) != string(ids_of(r.planted)), true)

' ---------------------------------------------------------------- avoid
' Composing two plants: the second must not land on a row the first took, or
' the second anomaly overwrites the first and the report lies about both.
first = fake.plant(clean, { anomaly: "round_dollar", count: 5, at: 2 })
second = fake.plant(first.rows, { anomaly: "weekend", count: 5, at: 2,
                                  avoid: indexes_of(first.planted) })
overlap = 0
taken = { }
for each p in first.planted
    taken[string(p.index)] = true
next
for each p in second.planted
    if not is_unknown(taken[string(p.index)]) then
        overlap = overlap + 1
    end if
next
check("`avoid` keeps a second plant off the first's rows", overlap, 0)
check("and both anomalies survive in the result",
      count(differing(clean, second.rows)), 10)

' ------------------------------------------------------------- refusals
' Each beside its nearest legal neighbour, so the tier cannot be satisfied by
' a `plant` that refuses everything.
on error goto next

bad = fake.plant(clean, { anomaly: "benford", count: 2, at: 1 })
check("an unknown anomaly is refused, and the known ones named",
      error.message,
      "fake.plant: unknown anomaly benford (known: round_dollar, duplicate, just_under, sequence_gap, weekend)")
error.clear()

bad = fake.plant(clean, { count: 2, at: 1 })
check("a spec with no anomaly is refused",
      error.message,
      "fake.plant: unknown anomaly unknown (known: round_dollar, duplicate, just_under, sequence_gap, weekend)")
error.clear()

bad = fake.plant(clean, { anomaly: "round_dollar", at: 1 })
check("a spec with no count is refused", error.message, "fake.plant: count must be a positive number")
error.clear()

bad = fake.plant(clean, { anomaly: "round_dollar", count: 0, at: 1 })
check("a count of zero is refused", error.message, "fake.plant: count must be a positive number")
error.clear()

bad = fake.plant(clean, { anomaly: "round_dollar", count: 500, at: 1 })
check("more anomalies than rows is refused",
      error.message, "fake.plant: asked for 500 anomalies in 200 rows")
error.clear()

bad = fake.plant(clean, { anomaly: "round_dollar", count: 2 })
check("a missing seed is refused, since a plant nobody can reproduce is not a fixture",
      error.message,
      "fake.plant needs `at:`, the seed that decides which rows are planted")
error.clear()

bad = fake.plant(clean, { anomaly: "just_under", count: 2, at: 1 })
check("`just_under` with no threshold is refused",
      error.message,
      "fake.plant: `just_under` needs `threshold:`, the limit the amounts must fall under")
error.clear()

bad = fake.plant([], { anomaly: "round_dollar", count: 1, at: 1 })
check("an empty population is refused", error.message, "fake.plant expects a non-empty array of rows")
error.clear()

wordy = [{ id: "alpha", total: money.of("USD", "10") },
         { id: "beta", total: money.of("USD", "20") }]
bad = fake.plant(wordy, { anomaly: "duplicate", count: 1, at: 1 })
check("ids that cannot be continued are refused rather than given a marker",
      error.message,
      "fake.plant: `duplicate` needs ids ending in digits to continue the sequence (got beta)")
error.clear()

' The controls: the same shapes, one step legal.
fine = fake.plant(clean, { anomaly: "round_dollar", count: 200, at: 1 })
check("planting EVERY row is legal", count(fine.planted), 200)
error.clear()

wordy2 = [{ id: "alpha1", total: money.of("USD", "10") },
          { id: "beta2", total: money.of("USD", "20") }]
fine2 = fake.plant(wordy2, { anomaly: "duplicate", count: 1, at: 1 })
check("and ids that DO end in digits are duplicated", count(fine2.rows), 3)
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)

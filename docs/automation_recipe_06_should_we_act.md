# Design laboratory — Recipe 6: Should we actually act?

Status: **Design laboratory**, and the increment it produced is built
(`stdlib/automation.bas`, `tests/run_automation.sh`). With it, all three
layers have a first increment and the architecture is closed.

The first recipe that **acts**. Recipe 5 crossed into `decision`; nothing had
crossed into `automation`, which left two of the design's refusals never tested
at all:

- **R5** — a process that has never been replayed against history may not act.
- **R6** — authority is *enforced* at the action. Only the **stating** half had
  ever been exercised.

Executable: `examples/automation_lab/04_should_we_act.bas`.

---

## Business problem

We have a decision. Should the software carry it out on its own?

## Automation policy

Spend up to 5,000 unattended; anything beyond needs a named human. And nothing
runs at all until the process has been replayed over at least a year.

---

## The interesting half is not the gate

The gate is four lines of policy. What matters is what the **replay tells you**,
and that is not knowable from the source.

The same process was replayed over twelve months at two granularities. A real
collapse is planted in month 7 and nowhere else.

| configuration | cells | fired | caught the real one | false alarms |
|---|---|---|---|---|
| by region × category | 12 | 1 | **no** | 1 |
| plus store | 60 | 1 | **yes** | 0 |

The coarse configuration **misses the collapse it exists to find, and the one
time it fires it is wrong** — every alarm it raises in a year is a false one.
The fine one catches the real event and raises nothing else.

Same code, same data, same year. Nothing in the source says which of the two you
have. **Only the replay does** — which is exactly why R5 makes simulation a
precondition rather than a feature to add later.

## The gates

```text
within authority, never rehearsed         REFUSED  never been replayed against history
within authority, rehearsed               acts     within delegated authority
beyond authority, rehearsed, unapproved   REFUSED  exceeds delegated authority, no approval on file
beyond authority, rehearsed, approved     acts     approved by regional director
```

Note the first line. **An unrehearsed process is refused before authority is
even considered, and approval does not substitute.** A human asked to approve
one has nothing to approve *on*: nobody knows how often it fires or how often it
is wrong.

---

## Design lessons

**L12 — the dry run and the live path must share one gate.** `would` and
`execute` both call one `_gate` and neither holds a copy of the rules. If they
could disagree, a rehearsal would be a statement about a *different program*
than the one that runs, and R5's entire argument evaporates. Asserted over a
six-case matrix, with a control that the matrix lands both ways — unanimous
agreement over a matrix of all-refusals would prove nothing.

**L13 — `would` cannot act by construction.** It takes no executor at all,
rather than taking one and a `dry_run` flag. The safety property is structural
instead of something a caller must remember, which is the difference between a
guarantee and a convention.

**L14 — this is the layer where a defect is not a wrong number.** It is
something *happening* that should not have. So the refusal tiers assert an
**absence**, and prove it with a side effect on disk: the executor writes a
marker file, and "nothing happened" means the file is not there. A return value
would prove nothing about whether the executor ran.

**L15 — a defect in `insight` was found by the recipe, not by `insight`'s own
suite.** Replaying a year at 12 cells exposed that a cell was being standardised
against a spread *including itself*, which puts a hard ceiling on how extreme
anything can look: `max|z| = (n-1)/sqrt(n)`. At 8 cells that ceiling is 2.47,
**below the threshold** — so the test could never fire however completely a cell
had collapsed, and would report *within ordinary variation* for a cell that had
gone to zero. Fixed by leave-one-out standardisation with a *t* threshold
(`n−2` degrees of freedom, since a leave-one-out residual is not normal), and a
refusal below four cells where there is no dispersion left to estimate.

That is the lab working as intended: the increment's own fixture was green, and
only running the thing over a year of history at a different granularity showed
it up.

---

## What is still untested

`observe` closes the loop and R7 refuses to cite an unmeasured action as
evidence — but **nothing yet learns from an outcome**. §11's learning cycle
ends at recording. A later recipe should feed a measured outcome back in as
evidence and show it change a subsequent decision, which is the only thing that
makes the loop more than bookkeeping.

Also untouched: retries, timeouts, queues, idempotency and failure recovery,
all of which §6 of the charter listed and none of which a single-action recipe
can motivate.

# ARI: known limitations

A register of what `stdlib/ari.bas` does not do, or does wrongly, **recorded
rather than reactively fixed**.

## Why this file exists

`ari_discover` is being built against a corpus this project generated.

**The hazard is not that the corpus is generated.** An earlier draft of this
file said it was, and that was wrong in a way worth correcting rather than
quietly editing out, because it points at the wrong remedy: it suggests the fix
is to obtain real reports and stop generating, and that would throw away most of
what the corpus is good for.

**The hazard is the feedback loop.** A corpus is a fine *measurement
instrument* and a poor *requirements source*. When the same corpus both tells
you whether the engine works AND tells you what the engine should support, the
engine converges on the corpus instead of on the world — and because we wrote
the corpus, that convergence looks like progress. **Teaching to the test.** What
has to be broken is the second arrow, not the first.

### The two corpora answer different questions

Neither substitutes for the other, and this project has measured both sides.

**A generated corpus can do things a real one structurally cannot:**

- **Carry its own answer key.** One declaration produces the report *and* the
  truth, so precision and recall are computable rather than reported. A real
  report arrives with no truth attached, and a hand-written answer key drifts —
  `stdlib/estate.bas` exists for exactly this reason.
- **Span the space rather than a sample.** A real corpus is one draw from an
  unknown distribution: it contains what one organisation's systems happened to
  emit. `examples/fixtures/ari_discover/` varies nine axes *chosen by index*, so
  none is left with one sample — which a real corpus cannot promise and a
  randomly drawn one does not deliver.
- **Vary one thing at a time.** Attribution needs ablation. A failure on a
  generated source names the axis that caused it; a failure on a real report is
  merely noticed.
- **Supply cases that are rare or absent in any real sample.** The non-ASCII
  adversarial source was hand-built precisely because the generated corpus is
  all ASCII — and without it the byte-versus-codepoint defect ships, because the
  two index spaces coincide exactly until a multi-byte character appears.
- **Supply a null.** There is no such thing as "a real report with no structure
  in it". The null corpus is only obtainable by construction, and it is the
  load-bearing tier: it caught 18 false positives that nothing else could.

**A real corpus does the one thing generation structurally cannot:**

- **It contains what we did not think of.** A generator shares its author's
  blind spots, so a corpus we wrote can only ever confirm them. `finio`
  measured the cost: ten ACH files from another project found defects in
  **three of four adapters** while every suite was green — including a strict
  94-byte rule that refused four of the ten outright, and a record carrying 94
  codepoints in 95 bytes.
- **It calibrates what is common.** The "note" column below says which forms
  matter; no population stands behind that, and none can until real reports
  exist (**L0**).

So the discipline below is not "prefer real data". It is: use the generated
corpus to *measure*, and require something outside it to *decide*.

The rules:

1. **Record the deficiency here** when it is found, with what it actually does.
2. **Do not fix it because discovery tripped on it.** A change to `ari` needs
   evidence from outside the discovery corpus — the format literature, a real
   report, or the existing hand-authored fixtures.
3. **Sweep at the end**, generally, against the whole register — not one entry
   at a time in the order discovery happened to hit them.
4. **Every entry has an executable probe** in `tests/run_ari.sh`, which asserts
   the limitation *still holds* and goes **red when it is fixed**, naming the
   entry to strike. A register nobody runs rots; this one cannot.

That last rule is the one `run_limitations.sh` already enforces for the
language, and for the reason measured there: five of fourteen entries in
`DOGFOOD.md` were **false** — fixed by shipped work, still cited as design
justification, and not catchable by reading.

## The one change made so far, and why it is not an exception

`DD-MMM-YYYY` and `MMM-DD-YYYY` were added to `_date_in` on 2026-09-16 while
building the discovery corpus. Stated plainly, so a reader can judge it:

- **It was prompted by the corpus.** The corpus emits that format, and `ari`
  answered `no-date-found` for every one.
- **It is sanctioned by design intent independent of the corpus.**
  `docs/text_design.md` §5.1 says the type keywords are "a permissive
  recognizer over **the union of common forms**", and an alphabetic-month date
  is a common form in print-image reports — not a property of anything invented
  here.
- **But the selection was corpus-led**, and that is the part to be careful
  about. The design says *union of common forms*; it does not say which forms,
  and the measurement below shows several other common ones still missing. One
  format was added because discovery tripped on it, which is exactly the order
  of operations this file exists to stop.
- **It is now tested where `ari` is tested**, not only where discovery is.

## What was measured

Every form below was run through `ari.parse` with `as money` / `as date` on
2026-09-16. This is behaviour, not reading.

**No population measurement stands behind the "how common" notes.** This
project holds no corpus of real print-image reports — the Enron corpus is
spreadsheets, and `examples/fixtures/ari/` is three hand-authored files. That
absence is limitation **L0**.

**It blocks less than an earlier draft of this file claimed.** Two different
questions get conflated under "evidence", and only one of them needs a
population:

- *Is this a legitimate form?* — answerable now, from the format literature and
  from `ari`'s own stated intent (§5.1's "union of common forms"). This is the
  question that decides **correctness**, and it is the one the sweep turns on.
- *How often does it occur?* — needs L0, and decides only **priority**.

So the sweep is not blocked on L0. What L0 costs is the ordering, and the
ability to say a form is rare enough to leave out.

---

### Class A — silent wrong answers

**These matter more than everything below them put together.** Each returns a
plausible number that is wrong, with no diagnostic. A caller cannot tell them
from a correct read, and `ari`'s own contract (§8: a bad cell becomes `unknown`,
never a silent value) is what they violate.

**Class A is empty as of 2026-09-17.** All four entries were discharged by the
sweep; each is recorded below with what it did, and each has a **control** in
`tests/ari_limitations_test.bas` asserting it is provably gone rather than
merely deleted.

#### A1 and A2 — struck 2026-09-17, and they were one defect

They read as two entries and they were one: **every money pattern ends in
`\.[0-9]{2}` and none of them required the match to be the whole number**, so a
pattern could match an *infix* of a longer numeric token and return it as the
value.

Measuring it before fixing it found two forms worse than either recorded entry:

| input | was | now |
|---|---|---|
| `1,234.567` | **1234.56** | `unknown` + `malformed-money` |
| `1.234,56` | **1.23** — a **thousandfold** error | `unknown` + `malformed-money` |
| `12.345.678,90` | **12.34** — a **millionfold** one | `unknown` + `malformed-money` |
| `1.234,56-` | **1.23**, sign lost as well | `unknown` + `malformed-money` |

The register had A1 at one separator; the error grows with every further group,
and nothing said so.

**The fix is a statement about the token, not a tenth pattern**: a match is
rejected when a digit touches it, or when a `.` or `,` touches it *and a digit
follows that*. The second half is what keeps ordinary punctuation working —
`Ending Cash 1,234.56.` ends a sentence, and refusing it would trade one wrong
answer for a different one. gBASIC regex is POSIX ERE with no lookaround, so it
is a check on the text either side, which is also the honest place for it.

**What this discharges and what it does not.** Both entries said *"or
`unknown`"*, and `unknown` is what they now answer — the silent wrong answer is
gone. **Reading** the continental and three-decimal forms is a different and
smaller question, recorded below as B10 and B11.

> **And the fix broke something, which the discovery corpus caught and
> `run_ari`'s own goldens did not.** The boundary test was written about the
> *match* when it is about the *number*: every pattern carries decoration —
> `\$?[ ]*`, a leading `<`, `(` or `-` — so a match can **begin on a space**,
> and any earlier digit on the line then read as *this number continues*.
> **18 of 230 planted amounts lost their sign**, each coming back as an ordinary
> positive figure. `run_ari` passed throughout: its trailing-minus amounts do
> not happen to share a line with an earlier digit.
>
> This is the register's own rule doing its work — *after the sweep, re-run the
> discovery corpus; a change that fixes a register entry and moves a discovery
> score has done something nobody intended*. It moved the score, and the reason
> was a defect nothing else in the tree could see. The two lines now in the
> probe differ only in whether what precedes the amount contains a digit.

---

### Class B — honest misses

Each returns `unknown` with a diagnostic. **This is correct behaviour** under
§8 — a form the recognizer does not know is not a form it should guess at — so
these are *gaps to decide about*, not defects.

| id | input | diagnostic | note |
|---|---|---|---|
| **B1** | `1,234` | `malformed-money` | Whole amounts with no cents. Common in summary and count columns. |
| **B2** | `1,234.5` | `malformed-money` | One decimal place. |
| **B3** | `1 234,56` | `malformed-money` | Space grouping (French/SI). |
| **B4** | `10/16/26` | `no-date-found` | **Two-digit year.** Ubiquitous in legacy print-image output, and the one on this list most likely to appear in a real report. Needs a century rule, which is a *decision* (a sliding window? a declared pivot?) and not merely a pattern. **Measured**: adding the pattern alone and nothing else does not produce a wrong date — it produces `26-10-16` and then a **raise**, `date modifier expects an ISO-like date string`, which ends the parse. The naive fix is not merely incomplete, it is worse than the miss. |
| **B5** | `16-OCT-26` | `no-date-found` | Two-digit year, alphabetic month. Same decision as B4, and the same measured consequence. |
| **B6** | `20261016` | `no-date-found` | Compact `YYYYMMDD`. **Deliberately contentious**: it is also a plausible identifier and a plausible integer, so recognising it as a date by default would be exactly the silent guess §8 forbids. Probably belongs in a declared `type` block rather than the built-in union. |
| **B7** | `OCTOBER 16, 2026` | `no-date-found` | Full month name. |
| **B8** | `2026/10/16` | `no-date-found` | ISO order with slashes. |
| **B10** | `1.234,56`, `12.345.678,90` | `malformed-money` | **European grouping**, the remainder of A1 once the silent read was removed. It cannot be settled by a tenth pattern: `1.234` is one-thousand-two-hundred-thirty-four continental and one-point-two-three-four decimal, and nothing in the token says which. Where BOTH separators appear the **last one is the decimal mark** — true of either convention and settled by the format literature — so those forms are decidable; the single-separator ones need §5.1's declared convention, which is exactly the shape `using date: dmy` has. |
| **B11** | `1,234.567` | `malformed-money` | **More than two decimals**, the remainder of A2. Representable: `money` carries four guard digits below the minor unit, which is why `money.text(amount, places)` exists — sub-cent prices are ordinary (fuel at $3.459 a gallon). Same family as B2 (one decimal place), and the same decision: how many places `as money` should admit is a question about the type, not a pattern to paste. |
| **B9** | a custom `date` type whose rule **captures** its components — `/([0-9]{2})\/([0-9]{2})\/([0-9]{4})/ -> dmy` | `no-date-found` | With no `/re/repl/` and at least one capture, `_convert` takes `groups[0]` as the value. That is right for money — a capture is how the digits are pulled out of the symbols and the sign — and never right for a date, where the captures are the components and the first is a two-digit day. **The control is the same rule without parentheses, which works**, so the difference is the capture and nothing else. Found writing `tests/ari_using_test.bas` (2026-09-17) and recorded rather than fixed, because the sweep rule below is *Class A first, and on its own*. It is Class B by this file's own definition — an honest `unknown` with a diagnostic — but note the diagnostic **misattributes**: `no-date-found` points at the data when the spec is what is wrong. |

---

#### A3 — struck 2026-09-17, and this register had it backwards

This file recorded `1,234.56 DR` as **should be `-1234.56`**. That is one
convention asserted as the truth, and measuring it showed the opposite of what
the entry claimed.

`CR` is read as negative, so `DR` being positive is *internally consistent* — it
is the **trial-balance** reading, where debits are the positive column. The
recorded "should be" would have inverted a correct answer.

**The real defect is larger than the entry and points the other way.** On a
**customer statement** a credit *increases* the balance: `1,234.56 CR` is money
in, and `ari` read it as **negative**. Both conventions are standard, neither is
rarer, and nothing in the token says which a report uses — so a statement read
through `ari` had **every signed amount inverted, silently**. That is Class A,
and it is the sign it got right by accident that hid it.

§5.1's own rule applies: *prefer the more common reading and record the
ambiguity, rather than pick silently*. `ledger` stays the default, so no
existing spec moves, and `using money: statement` declares the other — the same
mechanism `using date: dmy` uses, which is why stage 1 had to come first.

**The control that keeps this from being "flip everything":** the *notational*
negatives — a trailing minus, parentheses, angle brackets — are untouched by the
convention. They are notation, not a DR/CR sense, and a version that flipped
them too would pass every check that only looked at `CR`.

#### A4 — struck 2026-09-17

`_valid_ymd` range-checked `1..12` and `1..31` and did not know month lengths,
so `31-FEB-2026` came back as the date `2026-02-31` — a value that does not
exist, returned as if it did. It is checked against the length of its month now,
with the **full** Gregorian leap rule: 1900 is not a leap year and 2000 is, and
the shortcut that gets 2000 wrong is the commonest date bug there is, so both
are asserted rather than assumed.

The entry called this "a deliberate looseness to revisit" because the two paths
**agreed** — `31/02/2026` was accepted the same way — and that consistency was
what made it a policy rather than a bug. Both paths go through `_valid_ymd`, so
both tightened together and the consistency is kept.

### Struck by the sweep — 2026-09-17

**Three defects in `using`**, all silent, all found in the first hour of the
sweep, and all in *the remedy for another entry*. They were never in this
register because nothing had run the mechanism: the only exercised form is
`using date: <custom type>`, which works and is covered by
`examples/ari_delinquency_test.bas`.

| what | did | now |
|---|---|---|
| `using date: dmy` in a section | returned **the whole raw line as text** | binds the dialect |
| `using date: dmy` at file scope | **silently ignored** | refused, naming where a binding belongs |
| `using colour: x`, `using date: nonsense` | **silently ignored / raw text** | refused, naming the built-in types and the dialects |

**The first is the one that matters, and it is Class A in the worst place.**
`ari.inspect` prints, verbatim, *"declare `using date: dmy` (or mdy) on the
enclosing section"* for an `ambiguous-date` finding — and writing exactly that
produced a date column of ordinary-looking strings, with nothing raised and no
diagnostic. The mechanism only ever resolved a binding to a **declared custom
type**; a dialect word matched no branch and fell through to the final
`return trim(span)`. So the remedy for one silent wrong answer was itself a
silent wrong answer.

`tests/ari_using_test.bas` is the control that keeps all three struck. Its
load-bearing tier **builds its spec out of the hint `ari` prints**, lifting the
backticked fragment from `inspect`'s own output — because the defect was neither
the hint nor the converter but the **drift between them**, and asserting either
half alone would have caught neither.

**One claim that fixture made and then corrected.** Its first control asserted
that an unambiguous date reads the same under either dialect — the natural shape
for a tie-break, and false. `27/12/2026` under `using date: mdy` is
`unknown` + `invalid-date`, because month 27 does not exist and the author has
stated the column is month-first. Silently re-reading it day-first would be
guessing against an explicit declaration, which is the one thing this library
refuses to do anywhere else. **A declaration is authoritative, not a hint**, and
the three readings of that one token are what say so.

---

### Class C — capability gaps

| id | gap | note |
|---|---|---|
| **C1** | No span-level diagnostic surface | `ari.inspect` reports diagnostics by reason with field paths, and `ari.clean_grid` exposes the furniture pass — but nothing reports **which source span each rule claimed** or **what stayed unclaimed**. `docs/ari_discover_design.md` §20 names this as discovery's most consequential prerequisite, and it improves hand-written spec debugging as much as it does inference. |
| **L0** | **No corpus of real print-image reports** | Everything `ari` has ever been run against was written here. The three fixtures in `examples/fixtures/ari/` are hand-authored to be awkward, which is valuable and is not the same as being real. What this costs is **the unknown unknowns and the frequencies** — not correctness, which the format literature settles. `finio` measured the first cost precisely: ten ACH files from another project found defects in three of four adapters while every suite was green. Same gap `finio` records for `camt.053`, same remedy: files somebody else produced. **Striking L0 does not retire the generated corpus** — the two answer different questions, and the null corpus in particular is obtainable no other way. |

---

## The sweep, when `ari_discover` is done

Rules for it, so it generalises rather than accumulating one-off fixes:

- **Class A first, and on its own.** A silent wrong answer is a different kind
  of thing from a missing form, and mixing them lets the easy additions crowd
  out the corrections.
- **A1 and A3 are not new patterns — they are a decision about ambiguity.**
  `1.234,56` and `1,234.56` cannot both be read by one pattern set without a
  declared convention, which is precisely what §5.1's *overridable per section
  and per field* mechanism is for. The right fix is probably a declared
  grouping convention, not a cleverer regex.
- **Class B needs a decision per entry, not a pattern per entry**, and B4 is
  the proof. Adding its pattern and nothing else makes `ari` *raise* rather
  than mis-read — measured, not predicted — so an entry here is a question to
  answer, never a regex to paste. B6 needs a judgement about
  identifier-versus-date that the built-in union may be the wrong place for.
- **Separate the two questions before arguing about evidence.** Whether a form
  is legitimate is settled by the literature and is answerable today; how common
  it is needs L0 and decides only what gets done first. Conflating them makes
  the sweep look blocked when it is not.
- **Every addition gets a fixture in `examples/fixtures/ari/`**, not only a
  probe here, so the capability is exercised by the report parser's own suite
  against its own corpus.
- **The discovery corpus stays a measurement instrument throughout.** After the
  sweep, re-run it — a change that fixes a register entry and moves a discovery
  score has done something nobody intended, and that is what the corpus is good
  at telling you.
- **Nothing is added because `ari_discover` tripped on it.** If discovery meets
  a form `ari` cannot read, the entry goes in this register and discovery
  reports it as unrecognised — which is the honest profile, and is information
  a person can act on.

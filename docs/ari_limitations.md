# ARI: known limitations

A register of what `stdlib/ari.bas` does not do, or does wrongly, **recorded
rather than reactively fixed**.

## Why this file exists

`ari_discover` is being built against a corpus this project generated. That
creates a specific hazard: every time discovery meets something `ari` cannot
parse, the cheapest response is to change `ari` — and after enough of those,
`ari` is shaped to one invented corpus rather than to the population of real
reports. **Teaching to the test.**

So the discipline is:

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

**No population measurement stands behind the "how common" column.** This
project holds no corpus of real print-image reports — the Enron corpus is
spreadsheets, and `examples/fixtures/ari/` is three hand-authored files. That
absence is itself limitation **L0**, and it is the one that most limits what
the end-of-project sweep can claim.

---

### Class A — silent wrong answers

**These matter more than everything below them put together.** Each returns a
plausible number that is wrong, with no diagnostic. A caller cannot tell them
from a correct read, and `ari`'s own contract (§8: a bad cell becomes `unknown`,
never a silent value) is what they violate.

| id | input | `ari` answers | should be | note |
|---|---|---|---|---|
| **A1** | `1.234,56` | **1.23** | `1234.56`, or `unknown` | European grouping. A continental report read by this parser yields a number **a thousand times too small**, silently. The generic pattern matches `1.23` inside it and stops. |
| **A2** | `1,234.567` | **1234.56** | `1234.567`, or `unknown` | Three decimals silently truncated to two. Rates, FX and unit prices carry more than two. |
| **A3** | `1,234.56 DR` | **1234.56** | `-1234.56` | `CR` is recognised as negative; `DR` is not recognised at all. A report using the DR/CR pair gets **half its signs right**, which is worse than getting none right, because the total looks nearly plausible. |
| **A4** | `31-FEB-2026` | `2026-02-31` | `unknown` + `invalid-date` | `_valid_ymd` range-checks only (`1..12`, `1..31`) and does not know month lengths. **Pre-existing and consistent** — the numeric path accepts `31/02/2026` the same way — so this is a deliberate looseness to revisit, not a regression. |

A1 is the sharpest: it is the failure mode this whole library exists to
prevent, in the recognizer itself.

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

---

### Class C — capability gaps

| id | gap | note |
|---|---|---|
| **C1** | No span-level diagnostic surface | `ari.inspect` reports diagnostics by reason with field paths, and `ari.clean_grid` exposes the furniture pass — but nothing reports **which source span each rule claimed** or **what stayed unclaimed**. `docs/ari_discover_design.md` §20 names this as discovery's most consequential prerequisite, and it improves hand-written spec debugging as much as it does inference. |
| **L0** | **No corpus of real print-image reports** | Everything `ari` has ever been run against was written here. The three fixtures in `examples/fixtures/ari/` are hand-authored to be awkward, which is valuable and is not the same as being real. Until a real population exists, "how common is this form" cannot be answered, and the sweep below can only be justified by the format literature. This is the same gap `finio` records for `camt.053`, and it has the same remedy: files somebody else produced. |

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
- **Every addition gets a fixture in `examples/fixtures/ari/`**, not only a
  probe here, so the capability is exercised by the report parser's own suite
  against its own corpus.
- **Nothing is added because `ari_discover` tripped on it.** If discovery meets
  a form `ari` cannot read, the entry goes in this register and discovery
  reports it as unrecognised — which is the honest profile, and is information
  a person can act on.

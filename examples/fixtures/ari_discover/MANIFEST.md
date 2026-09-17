# ARI Discover fixture manifest

**Every file in this directory is SYNTHETIC.** Nothing here was captured from
anywhere. These are generated page images that reproduce the *layout* of a class
of report. They contain **no real account, member, employee or institution
data**, and no proprietary content. Names, numbers, dates and identifiers are
invented.

The distinction matters both ways: a reader who finds a file here that resembles
a production report must be able to tell at once that it is invented, and the
fixtures must never become a route by which real data enters the repository.

| what | files | generator | reproduce with |
|---|---|---|---|
| **structured corpus** | `NN_branch_activity.rpt` × 24, `truth.json` | `tools/gen_discover_corpus.bas` | `gbasic tools/gen_discover_corpus.bas examples/fixtures/ari_discover 24 20260916` |
| **null corpus** | `null/null_NN_noise.rpt` × 12, `null/truth.json` | `tools/gen_discover_null.bas` | `gbasic tools/gen_discover_null.bas examples/fixtures/ari_discover/null 12 90210` |

Both generators are pure functions of their arguments — seeded RNG, no clock, a
fixed run stamp — so the same arguments produce byte-identical files and these
can back a golden.

## Why this is not `examples/fixtures/ari/`

That directory holds three files and serves a different job. `ari` is a parser,
and the answer key for a parser test is **the specification somebody wrote by
hand**. Discovery has no such key, because inferring one *is* the thing under
test.

Two consequences follow, and they are why a new corpus was needed rather than a
larger old one.

### One declaration produces both the report and the truth

`truth.json` is written by the same program that writes the reports, from the
values it planted — never read back out of the text it printed. That rule is R1
of `stdlib/estate.bas` and it is here for the same reason: a hand-written answer
key drifts the first time either side changes, and **a fixture whose answer key
is wrong teaches the tool to be wrong and then certifies it.**

What the key carries, and what each part exists to score (design §8):

| field | scores |
|---|---|
| `furniture_lines` | page-furniture detection as precision **and** recall, by line number — not "it found some headers" |
| `families` | row-family recall, including the minority-family absorption §7 Phase 4 warns about |
| `branches[].accounts[]` | **the planted values.** The strong oracle §15.4 asks for: an accepted specification, run through ordinary `ari.parse`, must recover these. A coverage percentage cannot substitute — a specification can claim every line and extract the wrong number |
| `variant` | the nine drift axes this source was generated under, so a failure is attributable to an axis rather than merely noticed |

Verified when generated: 808 detail rows, every planted account/name/amount
appearing exactly once in its own file, every branch total equal to the sum of
its accounts, every furniture line really furniture, 0 mismatches.

### Three reports cannot distinguish a constant from an accident

Design §5.1 says variation across files is what separates a true constant from
an accidental one. `examples/fixtures/ari/` holds three files, two of which are
the same report — and at that size a `minimum_support` of `0.80` means "3 of 3",
because 2/3 is 0.67. Every support question is binary, so 0.80 and 0.95 are the
same threshold and neither can be calibrated. §8.2's holdout would leave two.

Twenty-four sources across nine declared axes, **20 of them multi-page**. That
balance is deliberate and was corrected once: with 2–4 branches per source only
6 of 24 ran to a second page, so the furniture tier — the one thing Phase 0 can
be scored on exactly — had six samples and eighteen sources on which the right
answer is a refusal. Both cases are wanted; a corpus three-quarters weighted to
the refusal is not.

The axes:

| axis | values |
|---|---|
| money notation | plain · trailing minus · parenthesised |
| account-column heading | `ACCT` · `ACCOUNT` · `ACCT NO` |
| total label | `BRANCH TOTAL` · `TOTAL FOR BRANCH` · `BRANCH TOTALS` |
| date dialect | `MM/DD/YYYY` · `DD-MMM-YYYY` |
| table indent | 0 · 2 · 4 columns |
| name-column width | 22 · 28 |
| page length | 50 · 60 · 66 lines |
| pagination | form feeds · header line only |
| optional `REMARKS:` section | present · absent |

Chosen by index rather than at random, so each axis is covered evenly. A
randomly drawn corpus can leave an axis with one sample or none, and a discovery
failure on that axis then looks like luck.

**Pagination is a separate pass**, blind to content and driven purely by a line
count, so page breaks land mid-table and mid-branch. That is
`gen_teller_report.bas`'s rule and it is load-bearing for the same reason:
breaking at tidy boundaries would produce a corpus that is larger and strictly
*easier* than a real report.

## The null corpus, and why it is the load-bearing one

`null/` holds twelve reports with **no recoverable structure in them at all**.
The expected result is a refusal.

Inference is a search, and a search always returns a winner. This project
measured that once already, at cost: `examples/automation_lab` recipe 1 ran the
same decomposition over a population with a real 45% collapse planted in a known
cell and over one holding nothing but noise, and **the output could not tell
them apart** — both produced a confident three-level causal chain, both declined
1.8%, top-region share 82.6% against 80.3%.

ARI Discover is the same shape of machine. A scorecard full of high numbers on a
corpus that *has* structure is not evidence that discovery found the structure;
it is equally consistent with a tool that always finds something. The only thing
separating those two is running it where the right answer is nothing.

**What makes it a fair null** — which is the whole difficulty. Random letters
would measure nothing, because discovery would reject them for reasons (no
money, no dates, no report-like density) unrelated to structure. So the null is
deliberately indistinguishable from the real corpus at the token level: the same
token kinds from the same vocabularies, the same money notations and date
dialects, comparable line lengths, plausible indentation. It differs in exactly
one respect — token order, token count and indentation are drawn per line, no
literal recurs at a stable position, and there is no furniture, heading, total
or repeating family.

Measured over the two corpora, on the features an inference engine keys on:

| | structured | null |
|---|---|---|
| non-blank lines | 1,564 | 679 |
| distinct structural signatures | **9** | **389** |
| share covered by the top 3 signatures | **73.8 %** | **6.5 %** |
| most common (indent, first token) pair | `BRANCH` at column 0, 169× (**10.8 %**) | 4× (**0.6 %**) |

Same tokens, structure absent by two orders of magnitude on exactly the measures
that matter.

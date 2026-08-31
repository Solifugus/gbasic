# Accounting design

**Status:** Shipped — `stdlib/accounting.bas`, `tests/run_accounting.sh`.
Sections marked deferred remain proposals.
**Scope:** `stdlib/accounting.bas` — chart of accounts, journal entries,
posting, the ledger, trial balance, the two primary statements, and period
closing. Phase 2 of the
[finance and business platform proposal](gbasic_finance_business_platform_proposal.md),
which was reordered ahead of lending on 2026-08-30.

Receivables, payables, inventory costing and consolidation are named in the
workstream and **deliberately out of scope here**; they are applications of
what this document settles, and each wants its own review.

---

## 1. Why this is the right thing to build first

Double-entry is the **substrate** the rest of the workstream posts into. A loan
schedule that cannot emit journal entries is half a product; a sales pipeline
that cannot recognise revenue is a spreadsheet. Lending, deposits, payments and
sales all end in an entry.

It is also the phase with the least convention risk. Lending has several
genuine ambiguities to rule on — accrual basis, payment waterfalls, what counts
as a fee. Double-entry has essentially one rule, it has held since 1494, and
nobody disputes it.

**And gBASIC is unusually well suited to it**, in a way that is measurable
rather than rhetorical. The single most common class of accounting bug —
mixing currencies — is already impossible:

```basic
u {USD}= "100.00"
e {EUR}= "100.00"
t = u + e        ' raises: cannot add money in different currencies (USD and EUR)
```

Exact integer money means a ledger that balances stays balanced: no
accumulated float error to sweep into a rounding account. `(u - u) = z` is
exactly true. Those two properties are most of what an accounting kernel spends
its defensive code on.

---

## 2. The one invariant

**Every entry balances: total debits equal total credits, in every currency.**

Everything else in this document is a consequence. The balance-sheet identity
(`assets = liabilities + equity`) is not a second rule to enforce — it *follows*
from this one plus correct account typing, which is why it makes such a good
test: if it ever fails, the defect is in the typing or the posting, and the
test says so without being told what to look for.

**An unbalanced entry is refused at post time, not discovered at trial balance.**
Storing one and reporting later would mean the ledger passes through a state no
ledger may be in, and the report that eventually notices cannot say which entry
did it.

---

## 3. Shape

### An account

```
{ code: "1000", name: "Cash", kind: "asset" }
```

`kind` is one of `asset`, `liability`, `equity`, `revenue`, `expense`. It fixes
the **normal balance side** — debit for assets and expenses, credit for the
other three — which decides both statement placement and the sign a reader
expects.

`accounting.chart(accounts)` validates and returns the chart: codes unique,
kinds known, names non-empty. **An entry naming an account not in the chart is
refused**, because the alternative is that a typo silently creates a phantom
account that appears in no statement anyone reads.

### An entry

```
{ date: when, memo: "Invoice 1042", lines: [
    { account: "1100", debit:  amount },
    { account: "4000", credit: amount }
]}
```

**A line carries `debit` or `credit`, never a signed amount.** Signed amounts
are more compact and are the wrong choice here: "is a negative debit a credit"
is a question every reader has to re-answer, and getting it wrong produces a
statement that balances and is backwards. Naming the side makes the journal
read like a journal, and exactly one of the two fields must be present.

Internally a line normalises to a signed amount with debit positive, so the
balance check is `sum = 0` per currency.

---

## 4. What it does

| Call | Meaning |
|---|---|
| `accounting.chart(accounts)` | validate and build a chart |
| `accounting.entry(chart, date, memo, lines)` | build **and validate** one entry |
| `accounting.post(ledger, entry)` | append; refuses an invalid entry |
| `accounting.balances(ledger)` | balance per account, signed by normal side |
| `accounting.trial_balance(ledger)` | every account with its debit/credit total |
| `accounting.balance_sheet(chart, ledger, as_of)` | assets, liabilities, equity |
| `accounting.income_statement(chart, ledger, from, to)` | revenue, expenses, net |
| `accounting.close(chart, ledger, through, equity_account)` | the closing entry |

`entry` validates and `post` refuses — two steps, because a caller assembling
entries from imported data wants to *inspect* the failure before deciding
whether to skip the row or stop the run.

### Closing

Closing zeroes revenue and expense into an equity account. **It must be
idempotent-safe rather than idempotent**: running it twice would double the
transfer, so a second close over a period already closed is **refused**, not
silently repeated. This is the operation people actually get wrong, and it is
wrong in a way that leaves the ledger balanced.

---

## 5. Failure modes worth designing against

Every one of these leaves a *balanced* ledger, which is why "the trial balance
balances" is a weak test:

| Mistake | Consequence | Guard |
|---|---|---|
| Right amounts, wrong side | statements backwards, trial balance fine | assert the accounting equation *and* a known net income |
| Account not in the chart | phantom account in no statement | refuse at entry |
| Closing run twice | equity doubled | refuse a second close of a closed period |
| Mixed currencies in one entry | nonsense totals | `money` raises already; balance is checked **per currency** |
| Allocation off by a cent | a rounding line nobody notices | `money.allocate`, which sums back exactly |

---

## 6. Validation

- **The accounting equation is asserted arithmetically**, not as a golden:
  after any sequence of posts, `assets = liabilities + equity + (revenue -
  expenses)`. A golden records whatever the library produced; this states what
  must be true of it.
- **A worked business**: a small company from opening balances through a month
  of transactions to statements, with figures **computed outside gBASIC** and
  checked against them.
- **Sign coverage**: at least one entry per account kind, so a normal-side
  error cannot hide in an untested quadrant.
- **Refusals with controls**: each refusal beside its nearest legal neighbour,
  since a refusal suite alone is satisfied by refusing everything.
- **Round trip through the database**: a ledger loaded via `dbframe` and
  re-summed must give identical statements — which is what makes the pipeline
  claim real rather than asserted.

---

## 7. Deferred, with reasons

- **Receivables, payables, inventory costing** — applications of this kernel;
  each has its own conventions (aging buckets, FIFO vs weighted average) worth
  a separate ruling.
- **Multi-entity consolidation and eliminations** — needs the single-entity
  case settled first.
- **Tax** — jurisdiction policy with effective dates, per principle 6 of the
  platform proposal; sales tax and VAT are allocation problems and belong with
  `money.allocate`, but the *rates and rules* do not belong in a timeless
  library.
- **Accrual scheduling and reversing entries** — real, but additions to a
  settled kernel rather than decisions about it.

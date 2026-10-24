# `unknown` truth tables and the NaN order — design

**Status: PARTIAL. Increment A shipped 2026-10-23 (`tests/run_same_value.sh`);
increment B is not built.** The direction was agreed in `docs/gsql_design.md`
§3.1.1; this note measures what it changes in the tree and splits it into two
increments. **The three decisions in §6 were taken 2026-10-23 as recommended:**
NaN keeps IEEE under the operators, string `+` keeps rendering an `unknown`, and
`nothing` stays two-valued; so does compound `=` keep the "same value" answer.

**Increment A, as built, found one more defect:** the deep comparison behind
compound `=`, `contains` on nested values and the watchers compared numbers by
their DOUBLE, so `[9007199254740993] = [9007199254740992]` answered `true`. Both
rules live in one place now, `number_same_value` and `number_order` in
`src/eval.c`.

## 1. What is already decided

From `gsql_design.md` §3.1.1 (v0.4):

- `unknown` was modelled on SQL `NULL`, and its truth tables should be
  **identical** to SQL's: Kleene `not`/`and`/`or`, and `unknown` propagating
  through comparison and arithmetic.
- **`if`, `while`, `until` and an inline `if` keep raising on `unknown`.** SQL's
  `WHERE` silently drops a NULL row; gBASIC refuses, which is its anti-silence
  rule. Inside a `sql` block SQL is followed exactly. The same value, a
  different consumer.
- **Two equalities.** The operator follows three-valued logic; the "is this the
  same value" routes (`contains`, `find`, `remove_value`, `unique`, `sort`,
  record and array comparison, the watcher change test) use SQL's "not
  distinct", or `contains([unknown], unknown)` stops being true.
- **NaN shares the second equality**: after `+inf`, equal to itself. That is
  already its place in G-SQL's index keys (`gsql_storage_design.md` §3.2, built).

## 2. Today, measured

Each line run on its own (`x = <expr>`, then printed):

| expression | today | SQL |
|---|---|---|
| `not unknown` | raises *unknown cannot be used as a condition* | NULL |
| `true and unknown`, `unknown and false`, `unknown or true`, `false or unknown` | all raise | NULL, FALSE, TRUE, NULL |
| `unknown = 1` | `false` | NULL |
| `unknown = unknown` | `true` | NULL |
| `unknown != unknown` | `false` | NULL |
| `unknown > 1` | raises | NULL |
| `unknown + 1`, `-unknown` | raise | NULL |
| `"a" + unknown` | **`"aunknown"`** | NULL (for `\|\|`) |
| `nothing = nothing` | `true` | — |
| `not nothing` | `true` | — |
| `unknown = nothing` | `false` | — |
| `contains([unknown], unknown)` | `true` | — |
| `unique([unknown, unknown])` | `[unknown]` | — |
| `decode("nan") = decode("nan")` | `false` | see §6, D1 |
| `contains([nan], nan)` | `false` | — |
| `unique([nan, nan])` | `[nan, nan]` | — |
| `sort([nan, 1, nan, 0])` | **`[nan, 1, nan, 0]`** | — |
| `min([nan, 1])` | `nan` | — |

Three findings §3.1.1 did not record:

1. **A NaN stops the OTHER elements sorting**, not only itself: `1` and `0`
   above stay in input order. A comparator answering "tie" for every pair
   involving NaN is not a consistent order, so the sort's result depends on
   where the NaNs sit.
2. **`"a" + unknown` is the text `"aunknown"`.** String `+` renders its
   operands; it is not arithmetic.
3. **`nothing` is a separate question.** It is two-valued today and nothing in
   §3.1.1 asks it to change (§6, D3).

## 3. What the change touches in the tree, measured

**Method.** A scratch build logged one line per event that would answer
differently: `=`/`!=` with an `unknown` operand (split by whether it arrived
through the operator or through a "same value" route), `and`/`or`/`not` and
arithmetic and ordering meeting `unknown`, string `+` with an absence, and every
comparison meeting NaN. The **whole gate** ran on it (195 suites passed, so the
counts are a normal run).

| event | events | sites | what changes |
|---|---|---|---|
| operator `=`/`!=`, one side `unknown` | 3,660 | 26 | answers `unknown` instead of a boolean |
| operator `=`/`!=`, both `unknown` | 26 | 3 | the same |
| route equality with `unknown` | 17 | 12 | **nothing** (stays "not distinct") |
| `and`/`or`/`not` meeting `unknown` | **0** | 0 | — |
| arithmetic meeting `unknown` | **0** | 0 | — |
| ordering meeting `unknown` | 3 | 2 | both are tests asserting today's refusal |
| string `+` with `unknown` | 10 | 5 | display lines only (§6, D2) |
| any comparison meeting NaN | 14 | 4 | §6, D1 |
| `sort` meeting NaN | **0** | 0 | — |

**So nothing in the tree depends on the raises the change removes**, and the
whole cost is in the operator `=`/`!=` against an absence. Those 26 sites fall
into three groups:

- **An `if` that relies on the comparison answering `false`** (the visible
  breakage, and it breaks *loudly*, because the `if` then raises):
  `examples/automation_lab/13_the_whole_loop.bas:297` (a missing record key,
  2,625 of the events), `stdlib/edgar.bas:268` (a variable that starts
  `unknown`), `stdlib/mail.bas:106` and `:383` (explicit `= unknown` /
  `!= unknown` tests), `stdlib/finio.bas:596`, `tests/mcp/server.bas:36` and
  `tests/windows/actor_transport.bas:113` (`env("X") = "..."` with the variable
  unset), `examples/path_builtins_test.bas:26` (`file_type` of a missing path),
  and two examples that test exactly this behaviour
  (`unknown_test.bas`, `unknown_nothing_distinction_integration_test.bas`).
  The rewrite is `is_unknown(x)`, or a default before the comparison.
- **A printed answer**: three cookbook recipes print `string(x = 0)` with `x`
  absent to teach that an absence is not zero
  (`lending_cookbook/06`, `finio_cookbook/03`, `credit_cookbook/04`). They
  would print `unknown` instead of `false`, which teaches the same point more
  truthfully; the pages move with their goldens.
- **Test helpers** (`check(...)`) comparing `got = want` where both may be
  `unknown`. They need the "same value" equality, which is what a test means.

**The idiom to watch is `env("X") = "..."`**: `env` answers `unknown` for an
unset variable, so the commonest way to read a flag becomes a raise. That is the
anti-silence rule doing its job (an unset variable was silently `false`), and the
reference must say so where `env` is documented.

## 4. The proposal

**Three-valued logic.** `not`, `and`, `or` follow Kleene's tables and are
commutative: `unknown and false` is `false` like `false and unknown`. The right
operand is skipped only when the left **determines** the answer, which is
`stdlib/rules.bas`'s rule and is asserted there. `and`/`or` still accept any
truthy operand as they do today; only `unknown` is three-valued.

**Propagation.** `=`, `!=`, the ordering operators, unary minus and arithmetic
answer `unknown` when an operand is `unknown`. `unknown = unknown` is `unknown`.

**The second equality.** The routes listed in §1 use "not distinct": `unknown`
is the same value as `unknown`, NaN as NaN. PLAT-EQ already sends them all
through `values_equal`, and the measurement confirms the operator and the routes
can be told apart at exactly that function, so the split is made there and
nowhere else.

**The NaN order.** In `sort`, `sort_by`, `unique`, `min`, `max`, `median` and
the percentile family, NaN is one value placed after `+inf` and before any
absence. So `sort([nan, 1, nan, 0])` is `[0, 1, nan, nan]`, `min([nan, 1])` is
`1` and `max([nan, 1])` is `nan`, which is PostgreSQL's answer for both. This
one order is the comparator `sort` already shares with the selectors, so it is
one change.

**Control flow is unchanged**: an `if` on `unknown` raises with today's message.

## 5. Two increments, in this order

**Increment A — the second equality and the NaN order.** Measured cost: **zero**
sites change behaviour, and it fixes finding 1. Ships alone, and G-SQL's index
already assumes it.

**Increment B — three-valued logic and propagation.** The visible change, with
the 26 sites above migrated **in the same commit**, since splitting the
migration from the change would leave a commit where `stdlib/edgar.bas` raises.
The two `negative_unknown_order`-style tests that assert today's refusal become
positive checks.

Each gets its own self-checking suite with SQL's tables written out by hand as
the oracle (a table copied from the implementation would be a transcript), plus
the controls that matter: `contains([unknown], unknown)` stays true, `if
unknown` still raises, and `is_unknown` is unchanged.

## 6. Decisions still open

**D1. Does NaN equal itself under the `=` operator?** The storage design said
NaN keeps IEEE behaviour in expressions (`nan = nan` false) and takes
"PostgreSQL's rule" only for placement. **That pairing is not PostgreSQL's:**
PostgreSQL's float comparisons treat NaN as equal to itself and greater than
every other value in expressions too, as far as I know without having run it
here. So there are two coherent choices:

- **(a) IEEE under the operators, the fixed order only in the routes and
  sorting.** Recommended. The measurement shows code that relies on IEEE:
  `stdlib/rules.bas` asserts that `not (nan > 1)` and `nan <= 1` differ, which is
  only true under IEEE, and numeric code like `finance.rate`'s bisection is
  written against IEEE. SQLite has no NaN at all (it stores NULL), so the
  PostgreSQL/SQLite common subset never promised either answer; G-SQL records
  the difference from PostgreSQL as a dialect deviation.
- **(b) PostgreSQL's rule everywhere**: NaN equal to itself and greatest under
  `=` and `<` as well. One rule instead of two, at the cost of `rules.bas`'s
  stated semantics and of IEEE for any numeric code that tests a NaN.

**D2. Does string `+` propagate `unknown`?** SQL's `'a' || NULL` is NULL.
Recommended: **no.** gBASIC's `+` on a string renders its operands, which is
how a program builds a message, and all five measured sites are display lines
that would print only `unknown`. G-SQL's `||` follows SQL inside a `sql` block,
the same seam as `if`.

**D3. Does `nothing` stay two-valued?** Recommended: **yes.** `nothing` is "no
value here" (a function's empty return), not a missing fact; §3.1.1 only asks
about `unknown`, and G-SQL already normalizes a bound `nothing` to an absence at
the boundary. Under the proposal `nothing = nothing` stays `true`, `not nothing`
stays `true`, and `unknown = nothing` becomes `unknown`, since one side is
`unknown`.

**Also open, and not blocking:** whether `[unknown] = [unknown]` and
`{a: unknown} = {a: unknown}` (today `true`) follow the operator's three values
or the "same value" equality. Recommended: the "same value" equality, since
compound `=` is already answered by the PLAT-EQ deep comparison that watchers
use. SQL's row comparison would answer NULL, so this is a stated deviation.

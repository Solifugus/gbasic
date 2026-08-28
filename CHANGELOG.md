# Changelog

All notable changes to gBASIC are recorded here.

This project uses [semantic versioning](https://semver.org/). Until 1.0.0 the
language surface may still change between releases.

---

## Unreleased

- **Every public stdlib function is documented, and a gate keeps it that way.**
  64 were not, across 14 libraries, and nothing could tell. The design documents
  explain *why* each library exists and the cookbooks show recipes; neither is a
  per-function reference, so a function could ship, be tested, be correct, and
  be undiscoverable. `matrix` was the extreme — eight public functions, the
  primitives every regression in `stats` is built on, and **zero** mentions in
  any document. `stats` had 32 undocumented distribution functions.

  New: `docs/statistics_design.md` §8b, an API reference for the distribution
  families (pdf/pmf, cdf, quantile for ten continuous and three discrete) and
  for `matrix`. Every signature in it was checked against the source rather than
  written from memory. It also states two things that would otherwise surprise:
  gamma and exponential take a **rate** while Weibull takes a **scale**, and the
  discrete quantiles spell one of their two `p` arguments differently because
  the probability being inverted and the distribution's own success probability
  collide.

  `tests/run_stdlib_docs.sh` now fails when a public function is undocumented.
  "Public" is the only thing gBASIC enforces — a name without a leading
  underscore — so a helper that should not be called is not an exception, it is
  a function that wants renaming, and doing that is the other way to pass.

  **The second tier caught three documentation bugs that would fail if copied:**
  `chart.new(...)` in a `chart_design.md` example, in the same document whose
  next paragraph explains a library *cannot define* `new` (it is `chart.spec`);
  `dates.from_zone` / `to_zone` / `zone_offset` in `datetime_design.md`, which
  are **core builtins**, not `dates` calls; and `stats.mean(...)` in
  `text_design.md`, where `mean` is a builtin. All four verified failing before
  the fix. Three remaining named-but-absent mentions are allowlisted with a
  stated reason each, because an allowlist without one is where a real defect
  goes to be forgotten.

- **`dates.dayname` is O(1) instead of O(days).** It walked one day at a time
  from a hardcoded Monday — 45 ms for a date twenty thousand days out, behind a
  name that reads as constant time. It predated `d.dayname`, a core field on
  every date value, and now delegates to it. Same answers, and the two
  day-name-stepping helpers went with the loop. Found while documenting it:
  writing down what a function does is a good way to notice it should not exist.

  Rebaseline: six `negative_dates_*.err` goldens, all by the same 53 lines and
  none by a single character of message — they pin `stdlib/dates.bas` LINE
  NUMBERS, the hazard `CLAUDE.md` already records for `chart`. Removing two
  helper functions moved every diagnostic below them.

- **BREAKING: the post-test loop is `do … until c`.** `loop` is gone from the
  syntax, and the continue-condition form `do … loop while c` is removed.

  ```basic
  do                          do
      tries += 1     →            tries += 1
  loop until tries >= 3       until tries >= 3
  ```

  Two objections to `loop` turned out to be one answer. It *reads* redundant —
  and it was, on the `until` side. But it was **load-bearing on the `while`
  side**, and not removably so: `do … while c` cannot be distinguished from a
  body whose next statement is a nested `while c … end while`, because both
  readings are complete programs and the `end while` that separates them can be
  arbitrarily far ahead. Measured at **32 reduce/reduce conflicts**, and —
  the part worth recording — *dropping the `until` form does not help*, because
  the ambiguity is with the nested statement, not with the other terminator.

  So `loop` could only be deleted by keeping `until` and dropping `while`.
  Which is fine, because the `while` form was always redundant: it means
  `until not c`, and `!<`/`!>` cover the single-comparison case without a `not`
  (`loop while j < 3` is now `until j !< 3`). For a compound condition, negate
  the whole thing rather than applying De Morgan by hand. Evidence it was not
  wanted: outside its own test, `loop while` had **zero** uses in the tree, and
  `loop until` had one.

  **The keyword ledger, verified against the binary rather than counted by
  hand:** 47 keywords → **46**. `loop` stops being a keyword in *any* position
  and is an ordinary identifier again (it is a label in `stdlib/dates.bas`).
  `until` goes the other way: `do … until c` makes it statement-initial, so it
  can no longer be a variable — it collided with `until[0] = 5` and
  `until{USD} = 9.95` on the `[`/`{` lookahead, which is exactly where the two
  shift/reduce conflicts landed when this was measured. Words usable as
  ordinary names go from four to two: `end` and `next`.

  Zero grammar conflicts. The `until` flag is removed from the AST node rather
  than left always-true, because dead machinery that still parses is how a
  retired construct comes back.

  Rebaselined: `examples/do_loop_test`, `loop_syntax_test`,
  `keyword_stability_test`. New negatives pin both retirements —
  `negative_until_as_name`, `negative_do_loop_until`.

- **Two loose ends closed, and the second was found by measuring rather than
  reading.**

  **`dim` is refused as a statement, and only there.** It was refused in
  `yylex` at token delivery, so the refusal fired in every position rather than
  the one it was written for: `{ dim: 7 }` and `r.dim` were both rejected with
  "`dim` is not a gBASIC statement" at a column where no statement is possible.
  Every other keyword is a legal field name — `dim` was the sole exception and
  nothing chose that. The token is delivered now and the grammar decides, which
  is the difference between asking *what* the word was and asking *where* it
  appeared. Zero new grammar conflicts, measured. The message is byte-identical
  and stated in **both** statement positions, because a `dim` inside a
  `consider` body used to get the advice too and losing it there would be a
  regression dressed as a fix.

  **The directory family was still failing silently.** `DOGFOOD.md`'s `round`
  entry named an undone follow-up — sweep the remaining builtins for the same
  "coerces where its siblings refuse" shape. Done by *probing all 176 builtins*
  with wrong-typed arguments in the first and second positions rather than by
  reading for the pattern. The coercion class came back **clean**: `round` was
  genuinely the sole outlier. But the probe surfaced a different and worse
  one — `list`, `files` and `folders` each carried two bare `fprintf` refusals
  (wrong arity, non-directory argument) that returned `nothing` with **exit
  code 0** and nothing catchable by `on error`. That is exactly the signature
  `tests/run_silent_traps.sh` exists for; the 2026-08-23 sweep promoted
  `goto`/`gosub` and out-of-range reads and missed this family. All three now
  raise: located, fatal, catchable. They were the only three left, which the
  sweep makes a measurement rather than a hope.

  **Two hardcoded test counts made real.** `run_parse_exit.sh` printed a
  literal `7` and `run_silent_traps.sh` a literal `12` — the latter was
  *already wrong*, running 13 cases while claiming 12. A gate that reports a
  number it does not measure can shrink without saying so.

- **Documentation sweep (2026-08-26)** — the statistics field expansion and
  `market` existed only in `reference.md` and this file. Now in the two
  task-first stats cookbooks (event studies, causal inference and price history
  in econometrics & finance; survival/Cox, meta-analysis and factor analysis in
  social & behavioral), in `README.md`'s feature surface, in `docs/ai/COOKBOOK.md`,
  and in `docs/project_state.md`. Every new recipe is executed by
  `examples/cookbook_econ_test.bas` / `examples/cookbook_social_test.bas`.

  **Reserved words are documented for the first time.** There was no list in any
  document — they were named one at a time, scattered across pages. Measured
  against the binary rather than read: 47 keywords, of which 43 cannot be a
  variable name and exactly four (`end`, `loop`, `next`, `until`) can; `server`,
  `warning`, `default`, `resume` and `from` are not reserved. `reference.md`'s
  old list was explicitly partial and omitted eleven.

  **Three stale claims retired**, each contradicting shipped behaviour: "a raise
  cannot be caught" in `reference.md` (frame-scoped `on error` shipped in
  PLAT-ERR, and the sentence cited `docs/ai/ERRORS.md`, which already said so);
  "records are an association list with linear field lookup" in
  `project_state.md` (hash-indexed since PLAT-RECIDX); and the same phrasing in
  `xlsx_design.md`, kept as history but dated so it cannot be read as current.

  Audited by execution, not by reading — the lesson of the three reading sweeps
  that each missed 163 lines of `#` comments. Every function named in both stats
  cookbooks exists, and every documented `record.field` exists on the record its
  function actually returns, checked across both cookbooks and `reference.md`.
  That audit found four wrong field names, all in the new text: `agg.p_value`
  (it is `agg.p`) and `km.std_errors` / `km.ci_low` / `km.ci_high` (they are
  `se` / `lower` / `upper`).

  Also fixed: `tests/run_doc_examples.sh` was briefly pointed at both stats
  cookbooks, which caught a reserved-word bug in the new `market` recipe (`to`
  as a variable) before backing out — those pages are API catalogues whose
  field-listing lines are not valid gBASIC, and gating them would take 61
  exemption markers or a rewrite that damages them. Recorded rather than
  papered over.

- **Causal inference in `stats`** — `did` and `pre_trends`
  (difference-in-differences) and `iv_2sls` (instrumental variables), with
  cluster-robust (CR1) and HC0–HC3 covariance available to both.

  **Both estimators can be right in the coefficient and wrong in the standard
  error**, which is the reason the suite is shaped the way it is: nothing about
  the output looks off, and a golden would record the wrong standard error as
  the expected value and defend it forever.

  2SLS run as two ordinary regressions — fit *x* on *z*, then *y* on *x̂* —
  produces the identical point estimate and measures its residuals against
  *x̂*. The model's residuals are `y - X*beta`, against the original *x*. The
  fixture performs the naive version alongside and pins that the coefficients
  agree to ten digits while the errors do not: on two datasets differing only
  in the sign of the confounding, the naive error is 1.78× too large and 2.7×
  too small. It is not conservative.

  A DiD on serially correlated panel data understates its own uncertainty
  (Bertrand, Duflo & Mullainathan 2004). In the test panel — thirty units,
  twenty periods, a persistent post-period shift per unit — the conventional
  error is 3.2× too small and reports *p* < 0.001 where clustering reports
  *p* > 0.10, on an estimate identical to twelve digits.

  So almost every numeric claim is derived a **second way inside the fixture**
  rather than recorded: the DiD estimate against the four cell means; CR1
  against `ols_robust`'s HC1, which it must equal exactly when every cluster
  holds one observation; the 2SLS estimate against the Wald ratio (four means,
  no matrix algebra); its standard error against σ²/Σ(x̂−x̄̂)²; the first-stage
  F against t² from an ordinary `ols`; Sargan's J against *n*·R²; Wu-Hausman's
  and the pre-trend F against two residual sums of squares.

  Eleven red proofs; two came back green and drove real coverage. Both were
  the same blind spot: the main IV fixture has no exogenous controls, so
  restricting the wrong block in `_f_drop` — for the first-stage F and again
  for Wu-Hausman — was a no-op there. Only a fixture *with* a control can tell
  a test of the excluded instruments from a joint test over everything.

  `pre_trends` reports what it is. Parallel trends cannot be tested — it is a
  claim about what the treated group *would* have done — so a large *p*-value
  is the absence of evidence against it over however many pre-periods exist,
  and the returned `note` says that in words. One fixture check deliberately
  asserts the *opposite* of the easy lesson: dropping an exogenous control
  orthogonal to the instrument does not bias 2SLS, it only widens the interval.

  Also fixed, in seven runners: a `diff … | head -N` under `set -euo pipefail`
  aborts the script, so a failing suite reported only its first failure and
  skipped every remaining tier.

- **Exploratory factor analysis in `stats`** — `factor_analysis`, principal-axis
  factoring with iterated communalities and a varimax rotation implemented
  without trigonometry (gBASIC has none; the quarter-angle comes from two
  half-angle identities and a square root).

  **It is not PCA.** PCA explains total variance, factor analysis explains
  common variance, and the whole difference is 1s versus communalities on the
  diagonal. On half-noise data that is 0.60 against 0.40 — using PCA where a
  latent construct is meant overstates what the factors explain by half.
  Rotation cannot improve fit, and Heywood cases are reported rather than
  clamped.

- **Survival analysis in `stats`** — `kaplan_meier` (with Greenwood standard
  errors and bands), `survival_at`, `logrank`. Verified against the *published*
  results of the Freireich 1963 leukaemia trial rather than against itself:
  median remission 23 weeks versus 8 on placebo, S(10) = 0.7529, S(23) =
  0.4482, log-rank χ² = 16.79.

  **Censoring is the subject.** Both ways of avoiding it are wrong and neither
  announces itself — on that same trial, dropping censored subjects gives a
  median of 10 and counting them as events gives 16, where the answer is 23.
  The event indicator is therefore required, not inferred. A median that the
  curve never reaches is `unknown`, not the largest observed time.

  **`cox_ph`** completes it: the proportional-hazards model, fitted through the
  partial likelihood so the baseline hazard cancels, reproducing the published
  fit of that same trial to four decimals (β = 1.5092, HR = 4.523, SE = 0.4096,
  p = 0.00023). `hr_per` reports the ratio over a stated interval, because a
  hazard ratio is per unit and a covariate in dollars otherwise reads as no
  effect.

- **Meta-analysis in `stats`** — `meta_analysis`, `smd_variance`,
  `eggers_test`. Fixed-effect and random-effects (DerSimonian–Laird) pooling,
  always reported beside Cochran's Q, I² and τ², because a pooled estimate over
  wildly heterogeneous studies is a precise summary of nothing.

  **Ratio measures pool on the log scale.** An odds, risk or hazard ratio is
  multiplicative: 0.5 and 2.0 are the same effect in opposite directions, so
  the true pooled effect is *none*, yet averaged as plain numbers they give
  1.25 — a 25% apparent harm. Nothing can detect the mistake from the values,
  so `scale: "ratio"` is explicit and back-transforms the estimate and its
  interval.

- **Event studies in `stats`** — `event_window`, `abnormal_returns` and
  `event_study`. The method that turns an EDGAR filing date into a testable
  claim: estimate a normal-return model (market, market-adjusted or mean)
  before an event, take the residuals across the event window as abnormal
  returns, cumulate to CAR, and aggregate across events to CAAR with a t-test.

  Four traps are refused rather than left to the caller, each of which yields a
  plausible *number* rather than an error: windows count **trading days**, not
  calendar days; an event on a day the market was shut moves to the next
  trading day and reports that it moved; an estimation window overlapping its
  own event window is refused (look-ahead); and a CAAR over unequal windows is
  refused. A fifth — **contaminated estimation windows**, where clustered
  events sit inside each other's baselines — is *reported* rather than refused,
  since clustering is sometimes unavoidable; on a constructed pair whose true
  CAAR is exactly 0.025 it produces 0.02455, close enough to read as noise.

- **`market` — daily price history** (`stdlib/market.bas`). The finance stack
  was complete except for its input: `stats.simple_returns`, `sharpe_ratio`,
  `max_drawdown`, `value_at_risk`, `capm` and
  `forensics.altman_classic(facts, prices)` all take prices as an *argument*,
  and nothing produced them — EDGAR serves filings, not quotes. `market.daily`
  returns `{ok, frame, adjusted, message}`, and the frame is the shape both
  consumers already want (`forensics` indexes by column; `frame["close"]` is
  the flat array `stats` takes). Providers are pluggable — Stooq needs no key,
  Tiingo is adjusted — behind the `offline`/`with_transport` seams `llm` and
  `edgar` use, so tests never reach the network.

  Two guarantees, because both failures produce a plausible *number* rather
  than an error: rows are always sorted **ascending by date** (a reversed
  series yields negated returns, which looks like ordinary market data), and
  **`adjusted` reports what the provider supplies** rather than being assumed
  (returns across a split from unadjusted prices read as a −50% day).

  **An adjusted provider's adjustment applies to every price column**, not just
  the close. Tiingo serves `adjOpen`/`adjHigh`/`adjLow`/`adjVolume` beside
  `adjClose`; taking the adjusted close while leaving the rest raw puts the
  columns on different scales either side of a split or dividend. Measured
  against six months of real AAPL data, that produced a close **below its own
  low on 89 of 124 rows** — and no single number looks wrong, which is why it
  survived until the live wire format was read.

  **Provider reality, checked live rather than assumed:** Tiingo is **verified
  working** (2026-08-26, real free-tier key). Keyless daily equity data has
  largely gone. Stooq answers any HTTP client with a JavaScript
  anti-bot challenge (HTTP 200, an HTML body, no data, regardless of
  user-agent) and Yahoo's chart endpoint returned 429. A keyed provider is the
  reliable path. `daily` names a challenge page and a rate limit for what they
  are, instead of reporting "no rows" and sending you to look for a bad symbol.

- **Fixed: `round` coerced where every other numeric builtin refuses.** It ran
  its arguments through a zero-defaulting conversion, so `round(record, 2)`,
  `round(array, 2)` and `round("3.14", 2)` all answered **0** — silently. Every
  neighbour (`sqrt`, `abs`, `floor`, `ceil`, `exp`, `log`, `log10`, `erf`,
  `erfc`, `lgamma`, `sign`, `pow`) raises `<fn> expects a number` and refuses
  numeric strings and booleans too; measured across all twelve, `round` was the
  sole outlier, which makes it a bug rather than a policy. Found by dogfooding:
  `round(stats.max_drawdown(prices), 6)` printed `0`, reading as "this series
  never fell" — `max_drawdown` returns a record. Now raises
  `round expects a number` / `round places must be a number`; four pinned
  negatives; no test in the tree depended on the coercion.


- **Compound assignment — `+=`, `-=`, `*=`, `/=`.** `x op= e` means exactly
  `x = x op e`, so it inherits every type rule and every refusal the operator
  already has: it works on numbers, strings, `date + duration`, money and
  durations, through record fields and array indexes, and `list += [1]` raises
  precisely as `list = list + [1]` does. With a modifier the modifier applies to
  the folded result (`name{upper} += "cd"` on `"ab"` gives `"ABCD"`).
  Statement-level only — there is no `y = (x += 1)`.

- **`for` loops may close with `next`.** `next`, `next <name>` and `end for` are
  the same statement; all existing `end for` code is untouched. A named
  terminator must name the loop it closes — classic BASIC let `next x` close an
  inner `y` loop by implicitly closing both, so a one-letter typo silently
  restructured the program, and that is refused here at load time. Costs no
  reserved word: `next` remains usable as an ordinary variable, as do `loop`
  and `until`.

- **`break` and `continue` may name a loop.** `continue x` abandons the inner
  loop and takes the next iteration of the loop over `x`; `break x` leaves it
  entirely. The name is a loop variable, so a named flow passes straight
  through any `while` or `do` in between. Naming a loop that does not enclose
  the statement is a located runtime error (`break: no enclosing loop named
  'zzz'`) — previously a break reaching the top level set a nonzero exit and
  printed **nothing**, a silent path that was unreachable before named flows
  existed and is now closed.

- **`default(value, fallback)`** — the value, unless there isn't one. Returns
  `fallback` when `value` is `unknown` *or* `nothing`, because the two commonest
  producers of an absent result split across them (`env` yields `unknown` when
  unset, `find` yields `nothing` on a miss). Tests presence, not truthiness:
  `false`, `0` and `""` are values and come back unchanged.

## 0.1.0-rc8 — 2026-08-25

**The DOGFOOD ledger's "worth fixing" list is now empty.** The last four items,
closed together.

One **breaking** change: `crypto.json_encode` is removed (see below). Everything
else is additive.

### Password-based key derivation

`pbkdf2_sha256(password, salt, iterations, length)`, `pbkdf2_sha512(...)` and
`scrypt(password, salt, n, r, p, length)` — RFC 8018 and RFC 7914, returning raw
key bytes ready for `aes_gcm_encrypt`. `crypto` had hashing, HMAC and AEAD but no
KDF, so a **passphrase** could not safely become a key, and gBASIC Studio
declined to offer passphrase-protected secrets rather than ship a single-round
hash that looks like one.

Verified against INDEPENDENT implementations — python3 `hashlib.pbkdf2_hmac`, and
RFC 7914 §12 — never against gBASIC itself. A KDF that agrees only with itself
proves nothing: a shared bug still round-trips, and the derived key is simply
weak.

Two decisions worth knowing. **An empty salt is refused**, though RFC 8018
permits one: it turns a KDF into a plain iterated hash and nothing about the
result looks different. **The cost parameters are not floored**, because RFC 6070
and RFC 7914 publish vectors with deliberately tiny costs and a floor would make
the implementation untestable against the vectors that prove it right — so the
recommended values are in `docs/reference.md` where a reader sees them.

### `xlsx.try_open`

`xlsx.try_open(path)` → `{ok, workbook, message}`, the `try_decode` shape. One
malformed workbook used to end an entire corpus scan, which is why the
15,871-workbook Enron scan ran one process per file.

`open` and `try_open` share **one** code path. A `try_` twin that accepts a file
its raising sibling rejects — or reports a different reason for the same file —
invites you to trust a verdict the real function does not share. A non-path
*argument* still raises from both: that is a bug in the caller, not a bad
workbook.

### Removed: `crypto.json_encode`

Use the core `json_encode` — which is what an unqualified call already reached.
Once `json_encode` became a builtin, the library's flat copy was unreachable
except when spelled `crypto.json_encode`, and the runtime warned on every
`load crypto` that it was being shadowed. `jwt_encode` now preflights with
`json_encodable` and calls the builtin, so a claim JSON cannot represent is
refused rather than quietly signed as `null`.

`crypto.json_decode` **stays**, and not for symmetry: it reads attacker-supplied
token payloads and accepts RFC 8259 only, where `try_decode` deliberately speaks
the permissive gBASIC dialect.

### Fixed: `crypto.json_decode` raised on malformed input

Its contract is `unknown` for anything out of domain, and on attacker-supplied
input a raise is a denial of service rather than a rejection. Value dispatch fell
through to a number for every character that is not `"`, `t`, `f` or `n`, so
`{"a":inf}` reached `number("")` and ended the program. It now scans RFC 8259's
number grammar, which also refuses `+1`, `1.2.3`, `1e`, `01` and a magnitude no
double can hold. 32 hostile payloads are pinned in
`examples/crypto_json_hostile_test.bas`.

### Documentation

The three ledger doc-gaps — typed-value construction, the
library-dependency-inside-the-block rule, and `gtk.application`'s single-instance
default — are written into `docs/reference.md`, along with `list` / `list_files`
and the recursive-walk idiom, none of which were documented at all.

The first turned out to be the small part of a bigger problem: the **Modifiers**
section was stale from rc6 in three ways. It described the paren spelling as
merely deprecated, said parenthesized *assignment* modifiers were not deprecated
(they were removed), and said modifiers do not apply to call results (they do).
Every claim there is now checked against a running program.

## 0.1.0-rc7 — 2026-08-24

Additive: no compatibility break. Four fixes, three of them from the DOGFOOD
ledger, and each closes a failure that was **silent** — a wrong answer, a
truncated program, or a file that could not be read back, with nothing on stderr
and nothing in the exit code to say so.

### Fixed: a child no longer outlives a killed interpreter

`docs/reference.md` promises in bold that nothing the interpreter starts
outlives it, and that promise was kept by a teardown pass at the end of the
program — which does not run when the interpreter is `SIGKILL`ed. The DOGFOOD
ledger recorded the consequence: four gBASIC children found sleeping two days
after the runs that started them, three with their working directory already
deleted.

Every child now arms a **parent-death signal** in the kernel between fork and
exec, so it receives `SIGTERM` the moment the interpreter dies, however it dies.
Spawned actors have always done this; `process.start` and `process.run` children
never did.

**Behaviour change:** `process.start` was never a documented way to launch a
process that outlives the program, and it is now definitively not one. A child
that should survive belongs to a service manager. (A child that ignores
`SIGTERM` still survives, as with `process.stop`; a set-user-ID executable loses
the armed signal at `exec`.)

Also fixed by the same change: a spawned actor exited immediately at startup
when the interpreter itself was pid 1 — a container entry point — because the
race check it used, `getppid() == 1`, cannot tell "my parent is gone" from "my
parent is init". Arming before the exec compares against the spawner's recorded
pid instead. `tests/run_process_lifetime.sh`.

### Fixed: `encode`/`decode` round-trips non-finite numbers

The `encode`/`decode` dialect has one promise — an exact gBASIC-to-gBASIC round
trip — and it did not hold for the values IEEE arithmetic produces. `encode`
wrote bare `inf` / `nan`; its own `decode` refused them. A program could write a
file it could not read back, with no diagnostic on either side, and ordinary
overflow reaches that state quietly: `number("1e308") * 10` is infinity.

`decode` now accepts the four spellings `encode` emits (`inf`, `-inf`, `nan`,
`-nan`), which are the same text `print` and `string` show and the same values
`serialize`/`deserialize` already round-tripped. `encode`'s output is byte-for-
byte unchanged.

**The wire parser is untouched and stays strict.** A JSON request or response
body still cannot carry `inf`, `nan`, `nothing` or `unknown` — RFC 8259 has no
syntax for them, and `json_encode` / `json_encodable` still refuse non-finite
values. `tests/webserver_client.py` posts each of them to a live server to prove
it.

(`-inf` decoded correctly the whole time, by accident: `strtod` parses it and a
leading `-` entered the number branch. One spelling of four working is what made
this a bug rather than a policy.)

### Fixed: a reported parse error no longer exits 0

A token the grammar has no place for — `dim x`, or any byte the lexer cannot
read — was signalled to the parser as **end of file**, and the parser cannot
tell a synthetic EOF from a real one. Inside a `program` block that produced a
syntax error, so the defect was invisible there. At **top level** it did not:
the grammar allows a program to end, so the file was accepted as whatever
preceded the bad token. The statements before it ran, everything after it
silently disappeared, and the process **exited 0**.

Both halves are fixed. Every token diagnostic now goes through the diagnostics
sink — located, and carried by `--json-diagnostics`, which previously received
a bare non-JSON line in the middle of a JSON stream — and a parse that reported
a diagnostic fails even when the parser accepted.

`dim` keeps its reserved status for exactly one purpose: it is now a located
parse error that says to assign instead, which is what a reader arriving from
QBasic needs to be told. `tests/run_parse_exit.sh`.

### `mod`, `concat` and `merge`

Three of the oldest DOGFOOD ledger items, closed together.

- **`mod(a, b)`** — the remainder, **floored**: the result takes the sign of
  the divisor, so `mod(-7, 3)` is `2`. **This differs from QBasic's `MOD`**,
  which truncates, and the divergence is deliberate. gBASIC has had no modulo,
  so the documented workaround was `a - floor(a/b)*b` — which is floored — and
  the libraries written against that advice depend on it;
  `stdlib/forensics.bas`'s civil-date algorithm is correct for negative years
  only under floored semantics. Shipping truncated would have silently
  disagreed with every workaround the builtin replaces. `mod(a, 0)` raises.

- **`concat(a, b, …)`** — one new array with the elements of each, in order.
  Variadic; sources untouched.

- **`merge(a, b, …)`** — one new record with the fields of each, **later
  winning** on a duplicate key, so `merge(defaults, overrides)` reads the way
  it looks. Shallow; sources untouched. This is the answer to composing onto a
  library's return value, which previously required binding and field-assigning
  in three lines.

All three are **builtins**. The infix `%`, array `+` and record `+` remain
separate decisions: `%` is lexer work, and whether `+` on a container
concatenates or adds element-wise should not be settled as a side effect of
adding a convenience.

## 0.1.0-rc6 — 2026-08-24

Two syntax changes, both aimed at the same thing: the language was spending
ambiguity and vocabulary it did not need to spend.

### Breaking: modifier clauses are written in braces

- **`x{USD} = 19.95`**, not `x(USD)= 19.95`. Every modifier position moves:
  assignment, comparison, library-qualified (`name{text.caseless}`), and with
  arguments (`s{join ", "}`). The brace form already existed for comparisons;
  this finishes it and retires the paren spelling.

- **Why.** `name(caseless) = "joe"` and `kind(x) = "record"` were the same
  tokens in the same order, so the parser had to GUESS which was a clause and
  which was a call — ninety lines of lookahead whose own comment admitted the
  identifier-argument case could not be closed at token delivery
  (`docs/gbasic_clause_recognition.md` §9). It did not fail cleanly: it parsed,
  ran, and died with `compare modifier not found: x`, naming the caller's own
  argument as a missing modifier. A brace cannot open a call, so there is
  nothing to guess. The guesser and the `MOD_LPAREN`/`MOD_CONTENT` tokens are
  deleted, and `(` means a call or grouping and nothing else.

- **Also fixed by the move:** a modifier on a call result
  (`getname(){caseless} = "joe"`) was refused by the paren form and is
  meaningful in the brace form, matching the lens-on-any-operand rule that
  always applied to literals. `tests/negative_function_result_modifier.*`
  retired.

- **Migration:** 699 clauses across both repositories, driven by the closed set
  of modifier NAMES rather than by punctuation — an ordinary call comparison
  must not be touched. `tests/run_brace_modifiers.sh`.

### Keywords may be field names after a dot

- `r = { end: 1, on: 2 }` and now `r.end`, `r.on`. A field name is a closed
  context, so a keyword there is unambiguous. Until now the language could
  build a field the dot form could not read, which had forced four renames in
  shipped designs.

## 0.1.0-rc5 — 2026-08-24

Everything since **rc2**. The rc3 and rc4 tags were cut without CHANGELOG
sections, so their content is folded in here rather than reconstructed from
memory; `git log v0.1.0-rc2..v0.1.0-rc4` is the authority on which of the
entries below shipped when.

The theme of rc5 itself is **failures that were not saying so**: the error model
was rebuilt so a function can catch one, a warning channel was added so advice
can be suppressed or made fatal, and four failures that reported without a
diagnostic became raises.

### Silent failures promoted to raises

- **An out-of-range array read** now raises (`error.source` `"indexing"`),
  matching the assignment path, which always did. It used to print an
  **unlocated** line, yield `nothing`, and leave the exit code at 0 — and since
  `nothing` is a legitimate value, callers could not tell the failure from a
  real one and CI saw success.

- **`goto` / `gosub` to a label that does not exist** now raises
  (`"invalid control flow"`). It used to print and then abandon the rest of the
  function, so a typo'd label silently truncated it.

- **The `date`, `datetime`, `time`, `file` and `dir` modifiers** now raise when
  they cannot construct a value (`error.source` `"datetime"` or `"modifier"`),
  matching `USD`, which raised four lines away in the same dispatch function.
  They used to print and assign `nothing` — so `d(date) = user_input` silently
  produced a `nothing` that flowed onward. `docs/text_design.md` and
  `stdlib/ari.bas` had both *claimed* these raised for months; the claim was
  measured, found false, and made true rather than weakened.

- **A raise inside a watcher body** now stops the drain instead of being
  dropped. Previously the watcher never fired, draining continued, and the
  program produced results built on a watcher that had not run — with the
  diagnostic surfacing only at exit.

All are now located, fatal by default, and catchable with `on error goto next`
— which a printed line never was. `tests/run_silent_traps.sh`.

### The warning channel

- **`on warning print | ignore | goto next | stop`** — a second diagnostic
  channel for advice, read with `if warning then` and `warning.message` exactly
  as errors are. `on warning stop` is the `-Werror` of a language with no build
  step: put it in `main` and every warning becomes a raise. `on warning ignore`
  is the opt-out that makes aggressive diagnostics possible at all.

- **Two deliberate differences from errors.** The anti-silence rules do NOT
  apply — an unacknowledged warning dies with its frame, because advice that
  must be acknowledged is not advice. And mode lookup is **dynamic**, outward to
  the nearest explicit setting, rather than frame-local: *a failure is the
  callee's business; the noise budget is the caller's.*

- **`warning` is not a reserved word.** It is a soft name, resolved only when no
  variable of that name is in scope, so `warning = 1`, `r.warning` and
  `{ warning: … }` keep working. Raise one with `warning("msg")` or
  `warning({ message: "…", extra: x })`. Note a typo'd variable called
  `warning` therefore reads `false` rather than raising.

- **New diagnostic: `unused-result`.** Discarding a non-`nothing` return from a
  gBASIC-defined function now warns. A function cannot change its caller, so
  every update API returns the new value and calling one for effect does
  nothing — the mechanism that let a worker pool supervise nobody through a
  tagged release. Builtins are exempt (`append` mutates in place by design) and
  `return nothing`, the void convention, is exempt by value. Turning it on
  found three real sites in the standard library.

### Breaking: `on error` is frame-scoped, and `on error resume next` is gone

- **`on error` now governs only the frame that executed it** — one function
  invocation, or the top level. A function you call starts in the default state
  whatever you armed, and your arming dies with your frame. The consequence is
  the point: **a function can catch a raise and return a clean fallback**, which
  the old process-global mode provably could not do (the caller's statement was
  abandoned by a generation check regardless of what the callee returned, and
  `error.clear()` did not rescue it).

  ```basic
  function safe_div(a, b)
      on error goto next
      q = a / b
      if error then
          return -1          ' the caller never knows
      end if
      return q
  end function
  ```

- **`on error resume next` is removed**; `resume` is an ordinary identifier
  again. Migrate to `on error goto next` — the checks you already wrote keep
  working, under semantics that no longer poison the caller. `on error goto
  <label>` and `on error stop` are unchanged in spelling, frame-scoped in
  meaning. Net keyword count: −1.

- **Two rules make deferred checking safe.** A second raise arriving while one
  is still unacknowledged *escapes* the frame rather than shadowing the first;
  and returning — or ending the program — with an unacknowledged error re-raises
  at the call site. Together, no raise can vanish: forgetting a check produces
  noise, never silence.

- **Bare `error` acknowledges; `error.field` does not.** `if error then` is true
  exactly once per raise (so no stale-state trap), and `e = error` acknowledges
  and snapshots in one move, while `error.message` and friends read without
  claiming — which is what lets the block body describe what it caught.

- **Structured raises and traces.** `error { message: "...", balance: b }` raises
  with the extra fields on `error.details`, so a library can ship error *data*
  instead of a string to match on. `error e` re-raises a snapshot, preserving its
  original location and `error.trace` — an array of `{name, path, line, column}`,
  innermost first.

- The fatal stderr line is **byte-identical** to before, which is why all 333
  negative-suite cases pass this change unmodified. Design:
  `docs/error_model_design.md`; proof: `tests/run_error_model.sh` (17 cases) and
  `examples/on_error_goto_next_test.bas`.

- **`real_path(p)` and `file_type(p)`** — two filesystem questions gBASIC could
  not ask. `real_path` returns the canonical absolute path with `.`, `..` and
  every symlink resolved by the kernel, or `unknown` when the path does not
  exist; a path containing an interior NUL is refused rather than truncated.
  `file_type` returns `"file"`, `"folder"` or `"other"` (or `unknown`), and is
  the only way to ask whether a path is a directory **without raising** —
  `file_size` on a directory raises, and a raise cannot be caught, so code
  holding an untrusted path had no safe way to ask. Together they are what a
  containment check needs: a "starts with the root" test on the path a client
  sent can be walked out of with `..` and cannot see a symlink at all; the same
  test on the resolved path cannot.

- **`web.static(relative, root)`** — serve one file from under a root, with
  canonicalize-then-check: the path is resolved first and containment tested on
  the answer, on a separator boundary so a root of `pub` does not match
  `pub-secret`. A path resolving outside the root is 403 even when the file is
  really there; a directory is 404 rather than a listing; unknown extensions
  are served as `application/octet-stream` rather than guessed at. The body is
  read whole, so this is for pages and assets, not large downloads.

- **`web` — a route table as data** (`stdlib/web.bas`, `docs/web_routing.md`).
  Routes are `{ method, path, handler }` records validated when the table is
  built, so an unknown verb, a malformed pattern, an uncallable handler or two
  routes that can never be told apart raise at startup rather than becoming a
  404 at 3am. `{id}` captures one segment and `{rest...}` the remainder, both
  reaching the handler as `req.params`. Matching is decided by specificity —
  static beats `{id}` beats `{rest...}` — so `/products/new` wins over
  `/products/{id}` however the table is ordered. `web.dispatch` returns a
  response record the WebServer takes verbatim, answering 404 for an unknown
  path and **405 with an `allow` header** for a known path and the wrong verb.
  `web.resolve` is the same matching with no handler called, and `web.paths`
  reports the table as sorted `"METHOD /path"` lines.

- **`webserver.listen` can bind an address** — `webserver.listen(8080,
  { address: "0.0.0.0" })`. Omitting the option still binds `127.0.0.1`, so a
  server stays private until its author publishes it deliberately. `address`
  takes a numeric IPv4 or IPv6 address; a hostname is refused rather than
  resolved (no name lookup at bind time), and an unknown option field is
  refused by name rather than ignored. The returned record gains `address`,
  reported by the socket itself the way `port` already is. A dual-stack
  listener (`"::"`) reports IPv4 peers in `request.remote_ip` as ordinary
  dotted quads rather than `::ffff:`-mapped, so address comparisons behave the
  same on either kind of listener.

- **Named, first-class watchers** — `watch recalc(a, b) … end watch` registers
  the watcher and binds `recalc` to a watcher value: `unwatch recalc` turns it
  off (a quiet no-op on an already-off handle), `watchers()` returns the live
  handles, `.name`/`.targets` identify one, and re-declaring a bound name
  **replaces** the old registration so setup code is safe to re-run. Handles
  compare by identity (`=`/`!=` only) and are refused by `encode` and actor
  `send`; named declarations are top-level only. The anonymous `watch(...)`
  form is unchanged. `unwatch` is a new reserved word.

---

## 0.1.0-rc2 — 2026-08-20

Five days after rc1: the datetime/duration redesign in full, two loop
constructs, and an xlsx correctness campaign measured against 15,871 real
Enron workbooks. Still a release **candidate** — the CLA question is open and
0.1.0 final waits on it.

### Language

- **Counted `for`** — `for i = a to b [step c] … end for`. The counter keeps
  its last value after the loop (this differs from QBasic).
- **Post-test loops** — `do … loop until <expr>` and `do … loop while <expr>`.
  `loop` and `until` remain usable as variable names and as `goto` labels.
- Modifier verbs accept the base spelling alongside the participle:
  `(upper)=`, `(lower)=`, `(trim)=` now work like `(uppered)=` and kin.
- `p = $19.99` now fails with a teaching error — money is a modifier
  (`p(USD)= 19.99`), not a literal, and the message says so.
- A runtime error inside a `load`ed library now names that library and the
  line inside it, instead of pointing at the caller's `load` line.

### Datetime and duration (the redesign — breaking changes)

The whole layer was redesigned; `docs/datetime_design.md` is the contract and
`docs/datetime_cookbook.md` (12 executable, suite-enforced recipes) the tour.

- **Month arithmetic uses the accountant's rule**: `jan31 + 1 month` is
  Feb 28, not Mar 3 — years/months clamp the day, then exact parts apply.
  Round-trips deliberately do not hold at month-end.
- **Durations are a (months, seconds) pair, never blurred**: `1 month =
  30 days` is now *false* (it was true — and simultaneously true for
  31 days). Ordering month-bearing durations against exact ones refuses.
  Signed durations; `datetime − datetime` yields a signed exact duration;
  `×`/`÷` with canonical results.
- **Dot extraction** — `d.year`, `d.month`, `d.day`, `d.weekday` (ISO:
  Monday=1…Sunday=7), `d.time` (exact duration since midnight), and kin.
  Reading a field finer than the value's declared precision yields `unknown`.
- **Business calendars as data** (`stdlib/dates.bas`) — `dates.calendar(spec)`
  with `weekend:`, `holidays:` (user-supplied data by design; gBASIC ships no
  national packs), `hours:`, `observe: "nearest"|"forward"` for observed
  holidays; `is_business_day`, `next/previous_business_day`,
  `add_business_days`, `business_days_between`, `dates.merge` (mutual
  working days obey the conjunction law), `dates.between`.
- **Recurrence as data** — `dates.matches(d, rule)`, `dates.select(rule,
  range)`, `dates.series(rule, bounds)`: `nth:`/`weekday:`/`day:`/`month:`/
  `when:`/`except:`/`roll:` vocabulary ("every third Thursday", "first
  Tuesday after the 15th", RRULE BYDAY/BYSETPOS/BYMONTH shapes). A miss is
  `unknown`; a malformed rule raises.
- **Business-hours arithmetic** — `add_business_hours` (an SLA clock that
  pauses overnight, over weekends and holidays), `business_hours_between`,
  `is_business_time`, with the round-trip law tested.
- **Timezones at the edges** — `to_zone`/`from_zone`/`zone_offset`/
  `zone_resolve` over IANA names; UTC timeline, civil calendar. DST policy
  matches Temporal's "compatible"; unknown zones are refused rather than
  silently UTC; all-day values are refused (no instant).
- **Scheduling** (`stdlib/schedule.bas`) — `slots` (a physician-style
  appointment grid) and `layout` (sessions packed into business days,
  bumping over breaks; the oversized are reported by name in `unplaced:`).

### xlsx

Measured against the full 15,871-workbook Enron corpus, cells with formulas
judged against Excel's own cached results: **disagreeing cells fell from
461,578 to 64,227 and fully-agreeing workbooks rose from 91.1% to 95.7%**
(`docs/xlsx_design.md` §13.Z–§13.AE record every step and its measurement).

- **Defined names** — `<definedNames>` resolve by lexer-level splice,
  including sheet-qualified scope (`Sheet!name`) and local-over-global
  shadowing; names for ranges flatten correctly in argument positions.
- **Implicit intersection** — a range in a scalar slot takes its element on
  the formula's own row/column, per Excel's pre-dynamic-array rule.
- **Coercion fixed to Excel's rules** — the empty *string* does not coerce
  to a number (`""+1` is `#VALUE!`) while the empty *cell* is 0; text dates
  in `DD-MMM-YYYY` coerce (English month names, deliberately locale-narrow).
- **Lookup/criteria empty-cell rules** — an empty-cell lookup key and an
  empty-cell criteria are 0 (`VLOOKUP`/`HLOOKUP`/`MATCH`, the IF-criteria
  family); the empty *string* stays text in both places.
- **Deleted-reference literals** — `Sheet!#REF!` in formula text evaluates
  to `#REF!` instead of failing to tokenize.
- **SUMIF-family error handling** — errors on non-matching rows are skipped;
  a matched cell's error still propagates. Empty arguments (`SUM(1,,2)`,
  trailing commas) contribute empty, not `#VALUE!`.
- **Honest refusals, priced by name** — CSE array formulas (`t="array"`),
  external-workbook references *and* external-workbook defined names
  (`[1]!Name`) are reported unavailable rather than answered plausibly wrong;
  recalc never overwrites a cached value it cannot recompute.
- Corpus instruments committed: `tools/xlsx_corpus_*.sh` (check / report /
  blockers / disagree), frozen-binary + per-worker-file methodology.

### Documentation

- `docs/datetime_cookbook.md` — 12 recipes, executable and suite-enforced
  like the xlsx cookbook.
- A first-twenty-minutes on-ramp for newcomers (tutorial + UNLEARN "names
  that nearly work").
- `docs/xlsx_design.md` §13.Z–AE — the corpus campaign, each fix measured.

---

## 0.1.0-rc1 — 2026-08-15

The first tagged release. gBASIC has been developed since 2026-05-02 (366
commits) without a prior tag, so this entry describes the shipped surface by
subsystem rather than diffing against a previous version.

A release **candidate** rather than 0.1.0: three defects that prevented the
project from building at all on current Ubuntu were found and fixed on the day
this was cut (see *Portability* below), and none of them were caught by the test
suite. That is a statement about how little exposure the build has had outside
one developer machine, and an rc gives the packaging configurations a chance to
be exercised by someone else first.

### Language and runtime

- Tree-walking interpreter for a BASIC-family language, in C11 with no required
  third-party dependencies. `gbasic` lexes, parses and evaluates `.bas`/`.gb`.
- Values: numbers, strings (binary-safe, UTF-8 aware), booleans, arrays,
  records, dates/times, durations, money, files, functions, regexes, plus
  `unknown` and `nothing` as distinct absences.
- Records and arrays are shared, refcounted and copy-on-write, preserving value
  semantics without copying on every read.
- Modifiers (`(...)=` clauses), watchers, `consider` blocks, locks, structured
  errors, `on error resume next`.
- **Policy-Based Inheritance (PBI)** — `copy`/`link`/`reset`/`exclude` field
  policies with `new` derivation.
- **First-class functions** — function values, methods via `this`, dotted-def
  attachment, `constructor`. (Closures are *not* implemented.)
- **Multiprocessing** — shared-nothing actors over fork+exec, `spawn`/`send`/
  `receive`/`self`, selective receive with timeout, handle passing over
  `SCM_RIGHTS`, and `monitor`/`demonitor` death notification.
- **Unicode** — codepoint operations, byte builtins, `\u{}` escapes.
- **Regex as a value kind**, overloading `contains`/`replace`/`split`, with
  `match`/`match_all` for the cases a literal API cannot express.
- Bitwise builtins (`band`/`bor`/`bxor`/`bnot`/`shl`/`shr`/`rotl`/`rotr`).

### Language additions since 0.1.0-rc1

- **Counted `for`** — `for i = a to b [step c] … end for`. gBASIC previously had
  only `while` and `for each`, so every counted loop was a hand-rolled counter;
  that idiom appeared 22 times in shipped code, including the standard library.
  `to` is inclusive, `step` defaults to 1 and may be negative or fractional,
  bounds are evaluated once at entry, and `step 0` raises rather than hanging.
- **Post-test loop** — `do … loop until c` and `do … loop while c`, for the
  "run at least once, then decide" shape `while` cannot express. There is no
  pre-test `do while … loop`, because `while` already is one, and no
  `repeat … until`, because `repeat` is a string builtin. `loop` and `until`
  never begin a statement and so remain usable as variable names and as labels;
  `do` does, and is reserved like `while` and `for`.

### Datetime and duration arithmetic (docs/datetime_design.md §4)

The floor of the datetime redesign, and three genuine bug fixes:

- **`Jan 31 + 1 month` is now `Feb 28`**, not `Mar 3` — the accountant's rule:
  years and months are added first, the day is clamped to the resulting month,
  then exact parts (weeks/days/hours/minutes/seconds) are added as elapsed
  time. The old behaviour added "the number of days in the starting month",
  which is right everywhere except month-end — where invoices live.
- **Duration comparison worked in no direction and now works in every one.**
  Durations fell through to numeric coercion (the PLAT-EQ defect, fixed for
  arrays and records, missed for durations), so every equality was true and
  every ordering false — `(1 month) = (30 days)` *and* `= (31 days)` were both
  true. Now: equality compares (months, seconds) pairs (`1 year = 12 months`,
  `1 week = 7 days`, `1 month = 30 days` is **false**); ordering is a total
  order on exact durations, and ordering a month-bearing duration is refused —
  a month has no fixed length.
- **The missing arithmetic exists**: `datetime − datetime` → signed exact
  duration; `duration ± duration`; `duration × n` and `/ n` (months scale only
  by integers; seconds round to the whole second). Results are canonical:
  `(45 minutes) * 4` is `3 hours`.

### Datetime component extraction (docs/datetime_design.md §3)

`d.year`, `d.month`, `d.day`, `d.hour`, `d.minute`, `d.second`, `d.weekday`
(ISO Monday=1…Sunday=7), `d.dayname`, `d.day_of_year`, `d.precision`, and
`d.time` (an exact duration since midnight). A field finer than the value's
precision reads as `unknown`; an unknown field *name* raises. Durations answer
their stored components and `total_seconds`, which is refused for
month-bearing durations. Previously there was no way to get 2026 out of a
datetime as a number short of slicing its string.

### Business calendars (docs/datetime_design.md §5, `stdlib/dates.bas`)

Calendars are data — `dates.calendar({ weekend:, holidays:, hours: })`, passed
explicitly to `is_business_day`, `next`/`previous_business_day`,
`add_business_days`, and `business_days_between` (counted over `(a, b]`,
signed, convention stated because half-open intervals are where calendar bugs
live). Holidays are normalised to day precision at construction, so a holiday
supplied as a full timestamp still blocks the day. `dates.merge(cals)` unions
constraints — weekend ∪, holidays ∪, hours intersected — with the tested law
`is_business_day(d, merge([a,b])) = is_business_day(d,a) and
is_business_day(d,b)`, which is why finding mutual meeting days needs no new
search machinery. `dates.between(a, b, "days"|"months"|"years")` answers the
calendar difference, consistent with clamping by construction (Jan 31 → Feb 28
is 1 month, exactly as Jan 31 + 1 month is Feb 28). An empty calendar makes
lookups fail fast rather than hang.

### Date selectors (docs/datetime_design.md §7, `stdlib/dates.bas`)

One spec-record vocabulary, three verbs: `dates.matches(d, spec, cal)`,
`dates.select(spec, anchor, cal)` (the one day, or `unknown` on a miss),
`dates.series(spec, bounds, cal)`. "Third Thursday of the month", "first
Tuesday after the 15th", "first business day before a deadline", "every third
Thursday at 14:00 all year", "payroll every 2 weeks rolled off holidays" are
all one-liners, and every series element satisfies `matches` with the same
rule — the two verbs verify each other in the tests. Strictness lives in the
anchor names (`after` vs `on_or_after`); roll conventions include the finance
`modified` rule; monthly stepping is multiplicative from the start, so
Jan 31 → Feb 28 → **Mar 31**, not Feb-28-forever. The series sub-rule is
`when:` and bounds are `{from:, through:|count:}` — `on` and `to` are keywords
that cannot follow a dot.

### Scheduling (`stdlib/schedule.bas`)

`schedule.slots(day, spec, cal)` cuts a working day into appointment slots
(the physician grid); `schedule.layout(plan, days, cal)` packs ordered
sessions into business days around immovable breaks — sessions keep their
order, one that misses the day end moves **whole** to the next day, and one
that fits nowhere is reported in `unplaced:` rather than dropped. With this,
every planned v1 layer of the datetime redesign is built.

### Two ergonomic debts cleared

- The string modifiers accept both spellings: `(upper)=` beside `(uppered)=`,
  likewise `lower`/`lowered` and `trim`/`trimmed`. The near-miss
  (`assign modifier not found: upper`) was the most-hit trap in UNLEARN; the
  modifier namespace is separate from builtins, so `(upper)=` and the function
  `upper()` never collide.
- `p = $19.99` now fails with a teaching error — `'$' is not a money literal;
  write p(USD)= 19.99` — instead of `unexpected token`. Sigils privilege one
  currency and change over time; money stays a modifier from a plain number.

### Observed holidays and month constraints (`stdlib/dates.bas`)

`dates.calendar({ …, observe: "nearest" | "forward" })` moves a weekend
holiday's day off to a working day — nearest free weekday with ties forward
(the US federal rule; July 4 2026 is a Saturday, observed Friday July 3), or
always forward (the UK substitute-day style). Chained weekend holidays take
consecutive weekdays. Computed once at construction, so every downstream verb
inherits it. Specs also gain `month:` (a number or list — RRULE's BYMONTH),
so "the 15th of January and July" is one rule.

### Business-hours arithmetic (`stdlib/dates.bas`)

`add_business_hours` (signed), `business_hours_between` (signed), and
`is_business_time` — working time that pauses overnight, across weekends and
holidays. The clock starts at the next open; a deadline exhausting exactly at
close is due at close (rolling would silently extend an SLA); the window is
half-open; only exact durations are accepted. The round-trip law
`between(a, add(a, n)) = n` is tested over mixed durations.

### Timezones (docs/datetime_design.md §9)

`to_zone` / `from_zone` / `zone_offset` / `zone_resolve`, core builtins in the
epoch family over the system IANA database. UTC for the timeline, civil time
for the calendar, zone names at the edges — no zone field on datetimes, no new
kind. DST edges are named, never guessed: the compatible default (ambiguous →
earlier, gap → shifted forward) with `zone_resolve` exposing both instants.
Unknown zones and all-day values are refused — glibc's silent UTC fallback on
a bad `TZ` is exactly the plausible-wrong-answer class this design refuses.
Safe with actors (processes, not threads); `TZ` is saved and restored around
every call.

### Recurrence extension

`when:` without `nth:` in a series emits **every** matching day in the period
— `{ every: "week", when: { weekday: ["monday","wednesday","friday"] } }` is
the Mon/Wed/Fri standup as one rule. This closes the main expressiveness gap
against iCalendar RRULE's `BYDAY` lists; gBASIC's `nth`-over-candidates
already covered `BYSETPOS`. The timezone *position* is now recorded in the
design doc's §9: UTC for the timeline, civil time for the calendar, zone
names at the edges — intentions stored as rule + zone, never as future UTC
instants.

### Documentation (datetime)

`docs/datetime_cookbook.md` — 10 recipes covering the whole datetime surface
(precision, extraction, duration algebra, deadlines and ages, business
calendars, date expressions, recurring schedules, mutual calendars, convention
layout, appointment slots), enforced by `tests/run_datetime_cookbook.sh` with
the same cannot-drift harness as the xlsx cookbook (one shared sync tool). The
tutorial and reference gained the arithmetic rules and the calendar/selector
surface, and the keyword-after-dot trap is recorded in UNLEARN.

### Platform

- `--tokens`, `--ast`, `--add-loads`, `--json-diagnostics`, `--line-buffered`.
- `print to error` — the program's route to standard error.
- `try_decode(text)` — JSON decode that reports failure as a value rather than
  raising, sharing the parser with `decode` so both accept the same dialect.
- `source_outline(text)` — in-process structural outline over a reentrant parser.
- `process.*` — run a child, or start one and poll/read/wait/stop it.
- Filesystem metadata and `atomic_replace`.
- `gbasic-lsp`, a language server (built by `make dev`, not by `make`).

### Performance

Three per-element access patterns that were quadratic are now linear, each by
adding an index behind an unchanged API:

- **Strings** — reading a string variable no longer deep-copies; `len`/`mid`/
  `left`/`right` memoize the codepoint count and keep a sparse index, so
  backward scans are no longer quadratic.
- **Arrays** — shared refcounted storage with copy-on-write.
- **Records** — a hash index from name to slot for records above a size
  threshold, plus the same copy-on-write sharing.

Repeated string concatenation with `+` remains quadratic and is deliberately
used as the negative control in the complexity test tiers.

### Correctness fixes worth calling out

- `print` and `string()` now share **one** renderer. `print` previously emitted
  `[?, ?]` for a string array and the literal `{record}` for a record — a record
  could not be displayed at all. Display is now total and never raises.
- Numbers render as the **shortest decimal that reads back identically**, bare
  or nested. `265550.75` used to print as `265551`.
- `=` on arrays and records is deep and structural. Both sides previously fell
  through to a numeric coercion where any two compound values compared equal,
  which silently affected `contains`, `find`, `remove_value` and `consider`.

### Spreadsheet pipeline (xlsx)

Requires zlib and libxml2.

- Reads and writes `.xlsx` through a hand-written ZIP container and a part tree
  that **discards nothing**, so an existing workbook can be edited and saved
  with every unmodelled part preserved byte-for-byte. Saves are deterministic.
- A formula evaluator validated against Excel's own cached values via
  `xlsx.check`, dependency-ordered recalculation across sheets, shared formulas,
  cross-sheet and external references, and the text/math/lookup/clock families.
- Measured against a 15,871-workbook corpus of real Excel files: **97.38%
  cell-level agreement**, zero read errors, 91.1% of workbooks with no
  disagreement at all.
- Layered libraries above it: `grid` (a messy sheet into clean frames),
  `consolidate` (many differently-shaped sources into one frame), `dbframe`
  (a frame into a SQLite table), and `xlsx.to_sql` / `xlsx.apply` (a column
  formula compiled to SQL or applied vectorised over a frame).

### Statistics

`stdlib/stats.bas` and friends, in pure gBASIC: distributions, matrix toolkit,
OLS, seedable RNG and resampling, data frames, inferential tests, GLMs,
clustering and PCA, time series through ARIMA/GARCH on a shared MLE optimizer,
power analysis, robust standard errors, mediation/moderation, and econometric
diagnostics. Field cookbooks for the social/behavioral and econometrics/finance
clusters.

### EDGAR suite

`stdlib/edgar.bas` plus `fundamentals`, `forensics`, `insiders`, `ownership`,
`mdna`, `screener` and `llm`. Built against real SEC data captured under an
authorized identity (see `examples/fixtures/edgar/MANIFEST.md`). All 33 work
packages in `docs/edgar_suite_development_plan.md` are complete.

Deliberately **not** included: the network forms of `report_13f` and 13D/G
full-text search, grants/exercises, and full-market acceptance against bulk
data. No test performs network access.

### GUI

- `gi` — a generic GObject-Introspection bridge (GTK 4 path), plus `gtk.bas`,
  `sourceeditor`, `gtkui` (a declarative reconciler) and `datagrid`.
- `gui` — the older GTK 3 record-driven module, still an experimental proof of
  concept. Prefer `gi` for new work; the two cannot share a process.

### Other modules

`sqlite`, `pg` (PostgreSQL), `webclient`, `webserver`, `xml` (tree and
streaming), and libcrypto-backed crypto builtins with `stdlib/crypto.bas`.

### License

gBASIC is **dual-licensed**. `LICENSING.md` is the map, and every file declares
its own license with an SPDX identifier.

- **Apache-2.0** (`LICENSE`, verbatim, md5 `3b83ef96387f14655fc854ddc3c6bd57`) —
  the language, the interpreter, every C module compiled into it *including the
  whole xlsx engine*, and 14 of the 24 standard libraries.
- **AGPL-3.0-or-later** (`LICENSE.AGPL-3.0`, verbatim from gnu.org, md5
  `eb1e647870add0502f8f010b19de32af`) — the spreadsheet-to-database pipeline
  (`grid`, `consolidate`, `dbframe`) and the EDGAR securities-analysis suite
  (`edgar`, `fundamentals`, `forensics`, `insiders`, `ownership`, `mdna`,
  `screener`). A commercial license for these is available.

Writing gBASIC programs, or embedding the interpreter, is Apache-2.0 and
unrestricted. The xlsx *engine* is Apache because it compiles into the binary
and could not carry a different license without making the whole interpreter
AGPL; what is AGPL is the layer built on top of it.

No Apache-licensed file depends on an AGPL one — the dependency graph was
checked, and the AGPL libraries are leaves. The docs gate enforces that every
stdlib library declares a license and that it matches `LICENSING.md`.

The repository previously carried no license at all, which meant default
copyright applied and nobody had permission to use it. `make install` places
both license texts, `NOTICE` and `LICENSING.md` under `$PREFIX/share/doc/gbasic`.

### Packaging

- **`make install PREFIX=...` installed a binary that looked somewhere else.**
  The stdlib path is compiled in (`GBASIC_DEFAULT_STDLIB`), but make cannot see a
  changed `-D`, so `make && make install PREFIX=$HOME/.local` — the sequence the
  Makefile itself recommends — installed an already-built binary still pointing
  at `/usr/local`. Nothing errored; `load` simply failed later, or silently
  resolved against a different gBASIC's stdlib. A stamp now invalidates the two
  objects that carry the path, and only those.
- `make install-lsp` installs `gbasic-lsp`, which previously had no supported
  route to a `PATH`. Kept separate from `make install` so a plain install stays
  lean; `make uninstall` removes both, plus the doc directory.

### Portability

- **riscv64** is a supported target; the suite runs on Ubuntu 24.04 riscv64.
- Fixed: `gi_repository_dup_default` does not exist in girepository-2.0 before
  ~2.88 (absent in 2.80.0 and 2.84.1). The build enabled `HAVE_GIR` on
  `pkg-config --exists` with no version floor and then failed to **link**,
  taking the whole binary with it, on Ubuntu 24.04 LTS and 25.04. Now uses
  `gi_repository_new()`.
- Fixed: libxml2's structured-error handler gained a `const` in 2.12.0. Against
  2.9.14 that is a warning under GCC 13 and an **error** under GCC 14, so
  Ubuntu 25.04 could not compile. The signature is now selected on
  `LIBXML_VERSION`.
- Fixed: the GTK 3 `gui` module had not compiled since 2026-07-23, still using
  the array layout that copy-on-write replaced.
- Fixed: `tools/check-deps.sh` named two packages that do not exist on
  Debian/Ubuntu (`libxcrypt-dev`, and `libgirepository1.0-dev` for a
  `girepository-2.0` module). Because `--install` issues a single `apt-get`,
  one bad name meant nothing was installed.
- The example and negative suites now **skip** cases whose module is compiled
  out instead of failing them. A build with no optional dependencies previously
  failed 34 of 182 examples for behaving exactly as documented.

### Documentation

`docs/README.md` indexes every document and marks each **Shipped**, **Proposal**,
**Partial** or **Record**, so a design for unbuilt work cannot be mistaken for a
description of working behaviour. `tests/run_docs_gate.sh` fails if a document is
missing from the index or if the index links to something that does not exist.

Six stale status lines were corrected — `xml`, `pbi`, `ari`, `statistics`,
`edgar` and `llm` all claimed unbuilt what ships with passing goldens. The xml
one had caused a working module to be filed as a release blocker.

`docs/xlsx_cookbook.md` is a 12-recipe tutorial for the spreadsheet library,
covering all fifteen `xlsx.*` calls and the `grid`/`consolidate`/`dbframe`
layers above them. Every code block and every output block on the page is
checked byte-for-byte against a real file in `examples/xlsx_cookbook/` and its
recorded output, so the page cannot drift from the product:
`tools/sync_xlsx_cookbook.sh` copies both in, and `tests/run_xlsx_cookbook.sh`
fails while any of them disagree — including the case a run-only suite would
wave through, where a comment-only edit leaves the output identical.

`CONTRIBUTING.md` covers building, running the suites, and the house rules —
and states plainly that code contributions are not being merged yet, pending a
Contributor License Agreement.

### Testing

CI (`.github/workflows/ci.yml`) builds three configurations on every push: all
optional modules enabled, none enabled, and install-to-a-prefix-then-run-from-it.
Each is a configuration that was genuinely broken and invisible from a developer
machine — which is the failure class tests cannot reach, since a `#if` guard and
a `pkg-config --exists` check are both blind to the configuration they did not
select.

216 example goldens, 303 negative cases and 45 suite runners. Goldens are
compared byte-for-byte. Optional-dependency suites skip cleanly when their
library is absent.

### Known limits

- Not stable. The language surface may change before 1.0.0.
- No closures, no exponent literals (`1e20` lexes as a duration — use
  `number("1e20")`), and repeated string `+` is quadratic.
- `valgrind` has no riscv64 port, so the memory tiers can only skip there;
  ASan/UBSan work but report use-after-free with degraded diagnostics.
- GUI suites need a display and skip without one.
- `use`/`--add-uses` is legacy; prefer `load`/`--add-loads`.
- Many documents in `docs/` are design proposals, not descriptions of shipped
  behavior. Check the status line at the top of each.

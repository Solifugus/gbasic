# Proposal: gBASIC Rules Library

Draft for design review — October 11, 2026

## Purpose

Provide a reusable library for constructing typed conditions, combining ordered rule stacks, evaluating decisions, and explaining their meaning and results. Initial applications include G-Run scheduling and contract management; the representation may also inform R9 and Conatus.

Use existing gBASIC arrays and records rather than introducing a core language type. Keep watchers separate, with an explicit dependency interface for integration. This is a declarative business-logic evaluator, not an evaluator of arbitrary gBASIC programs.

Two priorities govern every choice below, and they are stated here because they
decide the cases where other reasonable answers conflict:

1. **Clarity for a business author, with minimal translation.** A rule is read
   and edited by the person who negotiated it. Where a spelling or a semantic
   could be chosen to suit the implementation or to suit that reader, it is
   chosen to suit the reader.
2. **No exposure of gBASIC.** A rule is data, often authored or persisted
   outside the trust boundary of the program evaluating it. The rule language
   must not be a route to executing gBASIC, reading values the context did not
   offer, or exhausting the host. See *Security boundary*, which is a
   requirement rather than a precaution.

These two pull against each other exactly once -- a business author would like
to call a function, and arbitrary calls are the whole attack surface -- and the
registered pure-function interface is where that tension is resolved.

## Design foundation

- Preserve native values and explicit live bindings; do not interpolate values into expression strings.
- Keep rule stacks as the authoritative, readable, editable source.
- Condense stacks into reusable executable expressions, without substituting current live values.
- Preserve ordering, typed semantics, dependencies, and provenance through transformations.
- Expand condensed results back into rule stacks and render terms or decisions in clear English.
- Compile after rule changes; evaluate after relevant value changes. No polling is required by the library.

## Representation

The public representation consists of documented arrays and records, constructed and checked by library functions. Exact record layouts and gBASIC call syntax remain to be specified.

| Object | Contents |
| --- | --- |
| Value operand | Native gBASIC value, embedded as a snapshot |
| Binding operand | Stable binding identity, declared native type, and optional display label |
| Expression | Operators and operands with explicit structure |
| Condition | Expression producing boolean or unknown |
| Rule | ID, target, condition, replacement expression, and metadata |
| Rule stack | Ordered array of rules plus explicit initial results for targets |
| Compiled result | Executable expressions, dependencies, source stack, provenance, and version |
| Evaluation result | Typed target results, unresolved inputs, and optional explanation trace |

An array of typed tokens is an acceptable authoring representation. Parsing produces a typed expression tree; a shared graph may later optimize repeated subexpressions. Literal text containing operators can be an optional convenience input, but strings are not the mandatory or canonical representation.

Native operands retain number exactness, money currencies, datetime forms, duration restrictions, boolean values, nothing, and unknown. Arrays and records support membership and access. Function values may exist as data, but invocation must go through an explicitly registered pure-function interface.

### Values versus bindings

A value operand is fixed until the source expression changes. A binding operand resolves a current typed value during evaluation and exposes its identity to dependency discovery. Copying an expression preserves bindings; snapshotting replaces them with captured native values.

A binding cannot merely be a raw pointer or a copied value. Its lifetime, resolution, type checking, and connection to the existing watcher mechanism must be defined. The library can begin with explicit context bindings; direct variable bindings may require runtime support. No new user-visible core type is required by that possibility.

## Rule-stack semantics

Rules are processed in stack order. A later applicable rule overrides an earlier result for the same target. Rules for different targets do not implicitly replace each other.

Each rule says:

> When this condition holds, replace this target with this expression; otherwise retain its preceding result.

For a target with accumulated expression E, a later rule with condition C and replacement V produces:

```text
E_next = IF C THEN V ELSE E
```

Named targets support replacement of individual components, such as a payment cap. Whole-decision overrides use the decision target. Expressions alone cannot identify which earlier component the author intends to replace.

Example, shown as conceptual notation rather than implemented gBASIC syntax:

```text
initial allow = account.active AND balance >= minimum
later: WHEN emergency       SET allow = true
later: WHEN account.frozen  SET allow = false
```

The exact condensed form is:

```text
IF account.frozen THEN false
ELSE IF emergency THEN true
ELSE account.active AND balance >= minimum
```

Boolean simplification is allowed only when it preserves unknown behavior, errors, and evaluation semantics. Ordering of cross-target dependencies needs a defined policy; the initial implementation should reject dependency cycles.

## Operations

Names below are proposed API roles, not finalized exported signatures.

| Operation | Responsibility |
| --- | --- |
| Build | Construct values, bindings, expressions, conditions, rules, and stacks |
| Edit | Insert, remove, replace, reorder, and copy rules or expression elements |
| Inspect | Inspect operators, operand kinds, native types, bindings, and metadata |
| Validate | Check structure, operator signatures, bindings, targets, and dependencies; report every failure as data (see *Validation*) |
| Condense | Fold ordered overrides into executable expressions and simplify safely |
| Expand | Restore the retained source stack or generate an equivalent stack |
| Evaluate | Evaluate compiled expressions against a consistent typed context |
| Dependencies | Enumerate bindings and collection dependencies for external watchers |
| Snapshot | Capture current bound values without losing their native types |
| Render | Produce readable expression or rule-stack strings |
| Explain | Describe terms or trace a particular decision in controlled English |
| Serialize / deserialize | Persist rules, types, metadata, and stable binding descriptors |

Conceptual usage:

```text
compiled = rules.condense(stack)
dependencies = rules.dependencies(compiled)
result = rules.evaluate(compiled, context)
original = rules.expand(compiled, source_mode)
description = rules.explain(compiled, terms_mode)
explanation = rules.explain(result, decision_mode)
```

Condense and expand are preferred terms. Compress can be confused with byte compression; tetrize and detetrize describe the motivating idea but need not be API names.

### Expansion and reversibility

Different stacks can yield the same simplified expression. Therefore exact expansion requires retained source and provenance. Provide a clear distinction between restoring the original stack and generating an equivalent stack. An equivalent expansion must not claim to recover the original wording or clause ordering.

## Initial operator set

| Category | Operators | Meaning |
| --- | --- | --- |
| Grouping | `( )` | Explicit precedence in textual/token forms |
| Arithmetic | `+`, `-`, `*`, `/`, unary `-` | Native typed arithmetic |
| Comparison | `=`, `!=`, `<`, `<=`, `>`, `>=` | Native typed comparisons |
| Negated comparison | `!=`, `!>`, `!<`, `!>=`, `!<=` | Negation of the corresponding comparison |
| Boolean | `NOT`, `AND`, `OR` | Three-valued boolean logic |
| Membership | `IN`, `NOT IN` | Membership and its negation |
| Conditional | `IF condition THEN a ELSE b` | Select a result without evaluating the unselected branch |
| Absence inspection | `IS UNKNOWN`, `IS NOTHING` | Explicit absence-kind tests |
| Access | Field access and array indexing | Typed record/array lookup; exact surface syntax pending |

**Not-equal is `!=`, and `<>` is refused by name.** An earlier draft had this
the other way around. gBASIC itself rejects `<>` with a diagnostic that names
the remedy --

```text
'<>' is not an operator; not-equal is != -- a != b
```

-- shipped in 0.5.1 for readers arriving from QBasic, who are the same readers
who will author rules. A rule language whose primary spelling is the one the
host rejects would teach a habit that fails the moment the author writes
gBASIC, so the validator refuses `<>` and reproduces that remedy. The `!`
prefix is then consistent across `!=`, `!>`, `!<`, `!>=`, `!<=`, which is worth
more to a reader than matching SQL. Use `NOT IN`, not `!IN`.

Negated comparisons support the natural flow of an author's reasoning:

```text
a !> b  means  NOT (a > b)
a !< b  means  NOT (a < b)
```

**Do not rewrite `!>` as `<=`.** This was stated as a caution in an earlier
draft without the case that justifies it, which is how a restriction gets
simplified away later. The case is NaN, measured in gBASIC:

```text
n = nan
not (n > 1)   ->  true
n <= 1        ->  false
```

Every ordering comparison against NaN is false, so negating one is true while
the "equivalent" comparison is false. The rewrite is unsound for any operand
that can be NaN, and the normalizer must not perform it. Preserve the author's
operator spelling in source and English explanations even where the executable
representation is normalized.

### Proposed precedence

For optional textual/token authoring, strongest to weakest:

1. Grouping, access, and registered function calls.
2. Unary arithmetic negation.
3. Multiplication and division.
4. Addition and subtraction.
5. Comparisons, membership, and absence tests.
6. Boolean NOT.
7. AND.
8. OR.
9. Conditional selection.

Treat NOT IN as one membership operator. Reject chained comparisons unless explicitly combined with AND.

**And therefore the textual form is not in the first implementation.** This
section ends by observing that *structured expression construction avoids
precedence ambiguity entirely* -- which, taken seriously, says the nine levels
above are the cost of a convenience rather than of the feature. A parser,
nine precedence levels and five negated-comparison spellings are the largest
and least certain part of this document, and everything the library is *for*
-- ordered overrides, typed operands, dependency discovery, validation,
explanation -- works on the structured form alone.

So the precedence table stays as the specification the textual layer will be
built to, and the layer itself waits until something has been authored through
the structured interface and found wanting. The risk of deferring is small and
recoverable: adding a parser later changes no stored rule, because text was
never the canonical representation. The risk of building it first is a grammar
fixed before a single real contract has been encoded.

### Pure functions

Use named functions for minimum/maximum, rounding, string matching, date calculations, and collection aggregates. Register signatures, purity, dependency behavior, and explanation templates. The initial whitelist remains to be enumerated; do not expose arbitrary gBASIC function execution.

## Security boundary

A rule is **data**, and the interesting case is data that arrived from outside:
authored by a counterparty, edited through a web form, or deserialized from a
store written years ago. The evaluator must therefore be safe against a rule
stack that is not merely wrong but *hostile*. This is a requirement with tests,
not a precaution.

**Nothing in a rule may name gBASIC.** Four doors, each closed explicitly:

1. **Calls reach only the registered whitelist.** A rule names a function by a
   key the host registered; an unregistered name is a validation failure, never
   a lookup. There is no path from a rule to `function_resolve`, so a rule
   cannot reach a stdlib function, a user function, or a module verb -- which
   also means a rule can never open a socket, read a file or spawn a process,
   because those are reachable only through names it cannot say.
2. **A function VALUE in an operand is inert.** The representation admits one
   as data so a stack can round-trip, and the evaluator must refuse to invoke
   it. Otherwise `bound()` plus a deserialized operand is arbitrary execution
   with extra steps.
3. **A binding resolves only against the supplied context.** Bindings are
   looked up in the record the caller passed to `evaluate`, never in the
   enclosing environment. A rule cannot name a variable the host did not offer,
   so there is no read primitive and no information disclosure. "Direct
   variable bindings may require runtime support" in the section above is
   therefore not only a cost question: if it is ever built, it opens this door
   and needs its own answer.
4. **Deserialization is the real attack surface, and is validated like input.**
   A persisted stack must go through the same validator a built one does --
   same whitelist check, same type matrix, same depth cap -- because the
   deserializer is where an attacker writes. A loader that trusts its own
   format because the builder was careful is the standard shape of this
   mistake.

**Bounded, because a rule stack is attacker-controlled recursion.** The
evaluator, the condenser, the renderer and the deserializer all walk a tree
whose depth the author chooses. gBASIC's own `try_decode` caps nesting at
10,000 for exactly this reason: unbounded parser recursion segfaulted the
interpreter at around 45,000 levels on an 8 MB stack. So each walk carries an
explicit depth limit and refuses beyond it, with the limit a named constant
rather than whatever the C stack happened to allow. Expression *size* and
registered-call *count* want ceilings for the same reason -- a stack that
evaluates for an hour is a denial of service even when every step is legal.

Expressions remain read-only and side-effect-free, and time is supplied as a
binding rather than read from the clock. Those two are stated elsewhere in this
document as semantics; they are also security properties, and removing either
later would need to be argued on both grounds.

## Type, absence, and evaluation semantics

Validation must use a documented operator/type compatibility matrix. Do not implicitly convert strings into numbers, dates, or money. Preserve native arithmetic restrictions, including currency rules and refusal to order month-bearing durations. Invalid operations remain errors; negating an invalid comparison does not make it valid.

### Absence propagates; invalidity raises

This was marked "subject to compatibility review with existing gBASIC
behavior". It has now been measured, and **it is not compatible** -- so the
divergence is a decision rather than a discrepancy, and this section states it
deliberately.

What gBASIC does, measured at 0.7.0:

| expression | gBASIC |
| --- | --- |
| `false and unknown` | `false` |
| `true or unknown` | `true` |
| `not unknown` | **raises** -- *unknown cannot be used as a condition* |
| `true and unknown` | **raises** |
| `false or unknown` | **raises** |
| `unknown + 1` | **raises** -- *unknown cannot be used in arithmetic* |
| `unknown > 1` | **raises** -- *unknown and nothing support only = and !=* |
| `unknown = 1` | `false` |

gBASIC **short-circuits, and otherwise refuses**. The two cases it answers are
the two where short-circuiting makes the operand irrelevant. Note also that
`unknown = 1` answers `false`, not unknown: PLAT-EQ's rule is that equality
answers and ordering refuses.

**The rule language propagates instead, and reports an unresolved result.** The
reason is the first governing priority. In gBASIC a missing value is a
programmer's mistake and raising is the kindest answer. In a negotiated
agreement a missing fact is an *ordinary business condition* -- the invoice has
not arrived, the inspection is not booked -- and a decision engine that aborts
on it cannot do its job. "I cannot tell you yet, and here is what I am waiting
for" is a useful answer; a runtime error is not.

So, for the rule language only:

- Arithmetic and ordering comparisons involving unknown produce unknown.
- `NOT unknown` is unknown.
- `false AND unknown` is false; `true AND unknown` is unknown.
- `true OR unknown` is true; `false OR unknown` is unknown.
- Equality against unknown is unknown, **not false** -- which diverges from
  gBASIC in the other direction, and deliberately: in a contract, *"is the
  claim type equal to X"* when the claim type is not yet known must not answer
  "no". That is the silent-false failure this document already refuses one
  paragraph down for conditions, applied consistently to equality.
- Membership is unknown while an unresolved element could change the answer.
- `IS UNKNOWN` and `IS NOTHING` always answer a boolean. They are the only way
  out of unknown, which is what makes them necessary rather than convenient.

**THE LIMIT, AND IT IS THE SAFETY RAIL ON ALL OF THE ABOVE: invalidity still
raises.** Propagation covers *absence* and nothing else. gBASIC's refusals that
are about the *values themselves* -- adding USD to EUR, ordering a month-bearing
duration, comparing two incompatible kinds -- remain errors in the rule
language, exactly as they are in the host. They are not absence and must not be
laundered into unknown.

The distinction a reader should carry away: an unknown result means *a fact is
missing*; an error means *the rule is wrong*. If propagation ever swallowed the
second, the engine would answer "unresolved" for a rule that can never be
evaluated, and the author would go looking for a missing invoice instead of
fixing a currency mismatch. A test must pin that both ways -- a missing fact
yields unresolved, a mixed-currency comparison yields an error -- or the two
collapse into each other at the first convenient moment.

Nothing remains deliberate absence. It is not zero, an empty string, or
unknown -- measured: `nothing = 0`, `nothing = ""` and `unknown = nothing` all
answer `false` in gBASIC. In the rule language `nothing` propagates like
unknown through arithmetic and ordering, and `IS NOTHING` distinguishes the two
kinds of absence, because "we agreed there is no cap" and "we do not yet know
the cap" are different facts about a contract and must not render as one
sentence.

An unknown rule condition must not silently become false. The conservative default is an unresolved target when that condition could change its result. A later unconditional override can make an earlier unresolved result irrelevant. Proving equal alternatives is a possible later optimization.

AND, OR, and conditionals short-circuit. Expressions are read-only and have no side effects. Time must be supplied explicitly as a binding when relevant; avoid hidden reads of the clock.

Evaluate against a consistent input snapshot where needed, so a single decision does not mix values from different moments.

## Validation

Validation is where a business author meets this library, so its *failure* path
is a primary interface rather than an error case. Four requirements.

**A validation result is a VALUE, not a raise.** `rules.validate(stack)`
answers `{ ok, problems }`. This follows the rule the rest of this tree already
settled -- `try_decode` reports where JSON is malformed, `ldap.bind` returns a
reason, `market` answers a dead network as data -- *failure is a value where
failure is expected*, and an invalid rule stack is entirely expected. It also
matters for the surface that will consume this: a web form validating a rule as
it is typed cannot have the validator end the request.

**Every problem, not the first.** gBASIC's own parser stops at the first syntax
error, and that is right for a parser -- recovery invents structure the author
did not write. A validator over a *tree that already exists* has no such
excuse, and an author fixing six mistakes one reload at a time will stop using
the tool. Structural problems are independent; report them all.

**Each problem locates itself and speaks the author's vocabulary.** Enough to
act on, in the words the author used:

| field | why |
| --- | --- |
| `rule` | the rule's own ID, not an array index -- the author reordered the stack and the index moved |
| `target` | which target the rule writes |
| `where` | `condition` or `replacement`, and a path within it |
| `found` / `wanted` | the type or operator met, and what the operator admits |
| `spelling` | the author's own operator text, so a message about `!>` says `!>` and not the normalized form |
| `remedy` | the fix, where there is one |

**The message names the remedy, because that is this project's house style and
it is already load-bearing.** gBASIC's `<>` diagnostic says *not-equal is `!=`*
rather than *unexpected token*; its reserved-word errors name the word in the
spelling the author typed. The rules validator inherits that: *"`amount` is
money in USD and `cap` is money in EUR; a comparison needs one currency --
convert with `money.convert(cap, "USD", on_date)`"* is actionable, where
*"type error at node 7"* is not.

A deliberate consequence: a validation problem must distinguish **a rule that
cannot work** from **a fact that is missing**. The second is not a validation
problem at all -- it is an unresolved evaluation result, and surfacing it as a
rule defect would send the author to edit a rule that is correct. This is the
same boundary *Absence propagates; invalidity raises* draws, visible at the
other end of the system, and it should be tested as one property rather than
two.

## Watcher integration

The rules library does not own watcher registration, scheduling, or polling. It exposes stable dependency descriptors. External code connects those descriptors to existing watchers and invokes evaluation on relevant changes.

- Variable assignment can invalidate dependent expressions.
- Array or record mutations must invalidate relevant collection/access dependencies.
- Replacing a collection must also invalidate dependencies on its contents.
- Rule edits require rebuilding the compiled representation and dependency registrations.
- Time conditions can use externally scheduled timer notifications that update a time binding.

Start with complete conservative dependency discovery. Selective reevaluation and shared subexpression caching can follow once correctness is established. Branch short-circuiting must not permanently omit dependencies that become relevant later.

## Contract management and explanations

Rules may encode negotiated payment schedules, exceptions, caps, effective dates, and amendments. Store rule IDs, agreement/version IDs, source clause references, effective dates, labels, and optional original wording.

Two explanation modes serve different needs:

1. Terms explanation: render the stack's obligations and exceptions in clear English.
2. Decision explanation: identify applied rules, input facts, intermediate amounts, and the final result for a particular case.

Hypothetical terms:

> Pay 80% of the eligible amount, subject to a USD 500 maximum. For services performed on or after January 1, 2027, the maximum increases to USD 600.

Hypothetical decision:

> The eligible amount was USD 800. The negotiated rate produces USD 640. Because the service occurred before January 1, 2027, the USD 500 maximum applies. Payment due: USD 500.

Generate English through controlled templates associated with operators and registered functions. Preserve dates, currencies, rounding, precedence, negation, and unresolved facts. Unsupported explanation forms should be identified rather than paraphrased speculatively.

**A decision explanation must come from the evaluation, not alongside it.** One
walk produces both the result and the trace, with explanation a flag on the
evaluator rather than a second traversal that reconstructs what it thinks
happened. `ari.trace` is built this way in this tree for the stated reason that
*a trace describing a different program is worse than no trace*, and the stakes
here are higher: an explanation that plausibly misstates why a payment was
capped is a document somebody may rely on in a dispute. A second walk agrees
with the first until the day a short-circuit, an unresolved operand or a
normalization makes it disagree, and on that day it will still read fluently.

That is also the strongest available test of the explanation layer: the
explanation must name the rules the evaluator *actually applied* -- comparable
against the trace -- rather than being judged by whether it reads well. An
explanation suite whose assertions are all "the text contains the amount" would
pass on a confident fabrication, which is the failure the `insight` and
`decision` work in this tree spent a dozen recipes learning to prevent.

Retain the rule version, input snapshot, evaluator version, and calculation trace for reproducibility. An explanation shows what the encoded rules require; source links allow review of whether the encoding matches the signed agreement.

## Persistence

Serialize tagged native values losslessly, including exact numbers, money precision/currency, datetime forms, duration components, nothing, and unknown. Bindings serialize stable descriptors, not process-local addresses, and must be explicitly rebound when loaded. Unknown or incompatible bindings produce diagnostics rather than silent coercion.

## Implementation sequence

1. Specify record schemas, binding contracts, override semantics, and the
   operator/type matrix.
2. Implement builders, validation (reporting every problem as data) and a
   straightforward typed evaluator.
3. Implement stack condensation and exact source expansion with provenance --
   **with the agreement property built in the same increment, not later.**
4. Add dependency discovery and exercise integration with existing watchers.
5. Add rendering, controlled English explanations, and lossless persistence.
6. Optimize using shared graphs and incremental reevaluation after correctness
   is established.

### The property to build first

**Evaluating the stack and evaluating the condensed form must agree.** It is
the one claim the whole design rests on -- condensation is only safe if it
changes nothing -- and it is the only check that fails when a simplification is
*subtly* wrong rather than obviously so. It was listed last here, among
validation to do afterwards; it belongs in increment 3, because by increment 5
there is a body of rules whose condensed forms are already trusted.

Assert it as a **property over generated stacks**, not over a handful of
examples. A hand-written case exercises the shapes the author already had in
mind, and the simplifications that go wrong are the ones nobody pictured --
unknown meeting a short-circuit, a negated comparison over NaN, a later
unconditional override above an unresolved condition. `fake.bas` already
generates populations in this tree and is the obvious engine for it.

Three representations exist by design here -- the authored stack, the compiled
expression, and the provenance linking them -- and three statements of one fact
drifting apart is the defect every tripwire in this tree exists to catch. The
agreement property is what keeps the first two honest; provenance round-tripping
is what keeps the third.

The rest of the suite should cover exact source round trips, typed edge cases,
negated comparisons, the absence/invalidity boundary **in both directions**,
mutation invalidation, deserialization of a hostile stack, depth refusal, and
reproducible contract calculations.

## Remaining decisions

- Exact public record layouts and exported gBASIC signatures.
- Live binding implementation and lifetime behavior.
- Complete operator/type matrix, especially nothing, membership, and ordering failures.
- Initial pure-function whitelist and explanation templates.
- Defaults for targets, cross-target reference semantics, and cycle handling.
- Error/result representation and consistent input snapshot mechanism.
- Persistence schema and rule/evaluator versioning.
### Decided: provenance is its own, and does not reuse `reasoning.bas`

Raised and settled rather than left open, so it is not re-opened later on the
general principle that one fact should have one representation.

That principle is right and does not apply here, because these are not one
fact. `reasoning.bas`'s provenance describes **a statistical claim** -- subject,
measure, comparison, null model, search width, correction, what was
established. A contract rule's describes **a negotiated obligation** --
agreement and version ID, source clause reference, effective date, the original
wording. The two overlap in almost no field.

The decisive detail is a refusal rather than a field list. `reasoning.finding`
**requires** `search.width`, by design R1, because a finding's threshold is
derived from how wide the search was -- a search that does not know its own
width cannot set its cut. A contract rule has no search width: nothing was
searched, the rule was agreed. So sharing the shape would mean either relaxing
R1 -- weakening a load-bearing refusal in a shipped library to accommodate a
caller it was never about -- or carrying a superset record in which half the
fields are permanently absent. Both are worse than two honest records, and the
second is itself the kind of thing that rots: a field nobody fills is a field
nobody notices going wrong.

**What IS inherited is the discipline, not the record**: provenance travels
with the claim rather than beside it, it is derived rather than restated, and a
layer may not assert what it was not given. Those are why `reasoning.bas` works
and they cost nothing to adopt independently.

The established direction is a library over existing types, typed operands and bindings, ordered overrides, reversible source retention, separate watcher integration, and explainable execution. These remaining decisions refine that direction rather than require a new core type.

# Proposal: gBASIC Rules Library

Draft for design review — October 11, 2026

## Purpose

Provide a reusable library for constructing typed conditions, combining ordered rule stacks, evaluating decisions, and explaining their meaning and results. Initial applications include G-Run scheduling and contract management; the representation may also inform R9 and Conatus.

Use existing gBASIC arrays and records rather than introducing a core language type. Keep watchers separate, with an explicit dependency interface for integration. This is a declarative business-logic evaluator, not an evaluator of arbitrary gBASIC programs.

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
| Validate | Check structure, operator signatures, bindings, targets, and dependencies |
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
| Comparison | `=`, `<>`, `<`, `<=`, `>`, `>=` | Native typed comparisons |
| Negated comparison | `!=`, `!>`, `!<`, `!>=`, `!<=` | Negation of the corresponding comparison |
| Boolean | `NOT`, `AND`, `OR` | Three-valued boolean logic |
| Membership | `IN`, `NOT IN` | Membership and its negation |
| Conditional | `IF condition THEN a ELSE b` | Select a result without evaluating the unselected branch |
| Absence inspection | `IS UNKNOWN`, `IS NOTHING` | Explicit absence-kind tests |
| Access | Field access and array indexing | Typed record/array lookup; exact surface syntax pending |

`!=` aliases `<>`. Use `NOT IN`, not `!IN`. Negated comparisons support the natural flow of an author's reasoning:

```text
a !> b  means  NOT (a > b)
a !< b  means  NOT (a < b)
```

Do not rewrite `!>` as `<=` unless the operand type semantics guarantee equivalence. Preserve the author's operator spelling in source and English explanations even when the executable representation is normalized.

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

Treat NOT IN as one membership operator. Reject chained comparisons unless explicitly combined with AND. Structured expression construction avoids precedence ambiguity entirely.

### Pure functions

Use named functions for minimum/maximum, rounding, string matching, date calculations, and collection aggregates. Register signatures, purity, dependency behavior, and explanation templates. The initial whitelist remains to be enumerated; do not expose arbitrary gBASIC function execution.

## Type, absence, and evaluation semantics

Validation must use a documented operator/type compatibility matrix. Do not implicitly convert strings into numbers, dates, or money. Preserve native arithmetic restrictions, including currency rules and refusal to order month-bearing durations. Invalid operations remain errors; negating an invalid comparison does not make it valid.

Recommended initial unknown policy, subject to compatibility review with existing gBASIC behavior:

- Arithmetic and ordinary comparisons involving unknown produce unknown.
- NOT unknown is unknown.
- false AND unknown is false; true AND unknown is unknown.
- true OR unknown is true; false OR unknown is unknown.
- Unknown membership remains unknown when unresolved elements could change the answer.
- IS UNKNOWN and IS NOTHING always return a boolean.

Nothing remains deliberate absence. It is not zero, an empty string, or unknown. Its allowed comparisons and membership behavior must be specified against gBASIC's existing semantics before implementation.

An unknown rule condition must not silently become false. The conservative default is an unresolved target when that condition could change its result. A later unconditional override can make an earlier unresolved result irrelevant. Proving equal alternatives is a possible later optimization.

AND, OR, and conditionals short-circuit. Expressions are read-only and have no side effects. Time must be supplied explicitly as a binding when relevant; avoid hidden reads of the clock.

Evaluate against a consistent input snapshot where needed, so a single decision does not mix values from different moments.

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

Retain the rule version, input snapshot, evaluator version, and calculation trace for reproducibility. An explanation shows what the encoded rules require; source links allow review of whether the encoding matches the signed agreement.

## Persistence

Serialize tagged native values losslessly, including exact numbers, money precision/currency, datetime forms, duration components, nothing, and unknown. Bindings serialize stable descriptors, not process-local addresses, and must be explicitly rebound when loaded. Unknown or incompatible bindings produce diagnostics rather than silent coercion.

## Implementation sequence

1. Specify record schemas, binding contracts, override semantics, and the operator/type matrix.
2. Implement builders, validation, and a straightforward typed evaluator.
3. Implement stack condensation and exact source expansion with provenance.
4. Add dependency discovery and exercise integration with existing watchers.
5. Add rendering, controlled English explanations, and lossless persistence.
6. Optimize using shared graphs and incremental reevaluation after correctness is established.

Meaningful validation should cover equivalence of stack and condensed evaluation, exact source round trips, typed edge cases, negated comparisons, unknown propagation, mutation invalidation, and reproducible contract calculations.

## Remaining decisions

- Exact public record layouts and exported gBASIC signatures.
- Live binding implementation and lifetime behavior.
- Complete operator/type matrix, especially nothing, membership, and ordering failures.
- Initial pure-function whitelist and explanation templates.
- Defaults for targets, cross-target reference semantics, and cycle handling.
- Error/result representation and consistent input snapshot mechanism.
- Persistence schema and rule/evaluator versioning.

The established direction is a library over existing types, typed operands and bindings, ordered overrides, reversible source retention, separate watcher integration, and explainable execution. These remaining decisions refine that direction rather than require a new core type.

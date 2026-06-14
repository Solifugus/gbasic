# gBASIC Implementation Validation

Validated: 2026-06-13

## Executive Summary

This review validates implementation, not intended design.

1. Modifier/function-call disambiguation is implemented by parser-side lexical
   lookahead, not by Bison grammar alone and not by parsing one form and
   reinterpreting its AST later. `yylex()` scans the original source around
   `(`, returns either `LPAREN` or `MOD_LPAREN`, and puts the lexer into a raw
   modifier-content mode for the latter. Function calls and modifiers both work
   in conditions. Function and modifier namespaces can overlap, but the
   lookahead has name-discovery edge cases.
2. Precision-aware date/time equality is implemented now. Date/time is a
   first-class runtime value with stored precision. Bare date/time comparisons
   use the lower precision of the two operands. This is lenient and can make
   equality non-transitive.
3. Ordinary language watchers execute synchronously after each mutation.
   Watchers run once immediately when registered. Matching watchers are queued
   in registration/source order; mutations inside a watcher append more work
   to the same queue. There is no batching or deduplication. GUI-originated
   mutations are deferred and coalesced until after a GTK iteration. WebServer
   request insertion happens at event-loop points and immediately drains the
   normal watcher queue.

`docs/gui_state.md` and `docs/webserver_progress.md` are deleted in the current
worktree. The former is empty in `HEAD`; the latter was read from `HEAD` as
historical evidence. Current GUI status was validated against
`docs/gui_design.md`, `examples/gui/README.md`, and source. The deletions were
not reverted.

## 1. Modifier vs Function Call Disambiguation

### Current Implemented Behavior

The grammar has distinct productions after token classification:

- assignment modifier: `lvalue modifier OP_EQ expression`
- comparison modifier:
  `additive_expression modifier comparison_operator additive_expression`
- function call: `IDENT LPAREN argument_list_opt RPAREN` or the equivalent
  primary-expression suffix

The decisive step occurs before Bison sees the parenthesis:

- `src/parser.y:299` implements `modifier_lparen_ahead()`.
- It scans to a same-line closing `)`, rejects comma-separated content, and
  requires the next non-space character to begin an assignment/comparison
  operator.
- It examines the identifier immediately before `(`. If that identifier is a
  known built-in function or is found by scanning the current source for a
  `function` declaration, it chooses a normal function-call `LPAREN`.
- `src/parser.y:1084` calls this lookahead from `yylex()` and returns
  `MOD_LPAREN` or `LPAREN`.
- `src/lexer.c:100` and `src/lexer.c:291` show the lexer mode that returns the
  entire modifier body as one `MOD_CONTENT` token.

The parser therefore does not first create a call AST and reinterpret it later.
The form has already been selected at tokenization time.

Examples:

- `number("55")` is a normal call because no comparison/assignment operator
  follows its closing parenthesis.
- `number(age_text) >= 18` is a normal call because `number` is a known
  built-in function.
- `age(number)= input("Age? ")` is a modifier assignment because the identifier
  before `(` is `age`, not `number`, and the closing parenthesis is followed by
  `=`.
- `name(lowered)= "bob"` is parsed as a comparison modifier in an expression.
  It succeeds at runtime only if `lowered` resolves in the compare-modifier
  context. The built-in `lowered` modifier is assignment-only, so the
  exploratory test defines a custom compare modifier with that name.

Modifiers are allowed in conditions because `if` consumes a normal expression
and comparison expressions include the modifier production. Ordinary function
calls are also normal primary expressions and may appear as a whole condition
or as comparison operands.

Function definitions and modifier definitions are stored and resolved
separately (`src/eval.c:209-212`). The exploratory test confirms that a
function and comparison modifier can both be named `overlap` and resolve by
syntax/context.

### Bison Conflicts

Running:

```sh
bison -Wall -Wcounterexamples -v -o /tmp/gbasic-parser.c src/parser.y
```

reported no shift/reduce or reduce/reduce conflicts. It reported only:

```text
warning: useless associativity for DOT, use %precedence
```

The absence of a grammar conflict is partly achieved by the parser-side
lookahead returning two different parenthesis token types.

### Existing Tests

- `examples/function_call_comparison_test.bas` covers built-in calls, a
  source-declared function call, a comparison modifier, and a parameterized
  comparison modifier.
- `examples/parser_hardening_test.bas` covers calls and modifiers in the same
  program.
- `tests/negative_function_assignment.bas`,
  `tests/negative_foo_assignment.bas`, and
  `tests/negative_function_result_modifier.bas` reject calls as lvalues or
  modifier targets.
- Qualified function and modifier behavior has separate examples, but there is
  no focused test for an externally loaded, qualified, single-argument
  function immediately followed by a comparison operator.

### New Exploratory Test

`tests/exploratory_parser_condition_disambiguation.bas` is intentionally not
registered in `run_examples.sh`. It passed and covers:

```basic
if len(name) > 0 then
if starts_with(name, "B") then
if number(age_text) >= 18 then
if name(lowered)= "bob" then
```

It also confirms overlapping function/modifier names.

### Known Risks

- Classification uses an ad hoc raw-source scan in `src/parser.y`, separate
  from the normal lexer and grammar.
- Function detection knows built-ins and declarations in the current source.
  It does not resolve functions imported later from another file.
- The source declaration scan is global to the file rather than scoped to the
  active program/library.
- A variable/lvalue whose name is also recognized as a function is biased
  toward call syntax in the ambiguous form, which can make its modifier form
  unparseable.
- Qualified or externally loaded calls are under-tested in the ambiguous
  one-argument-plus-comparison shape.
- Modifier content is captured as raw text and parsed later by modifier
  runtime helpers, so its accepted expression surface is not the normal
  expression grammar.

### Recommended Next Design Decisions

- Decide whether disambiguation should remain source lookahead or become an
  explicit grammar/semantic rule.
- Define the required behavior when an lvalue name is also a function name.
- Define how externally loaded and qualified functions participate in
  disambiguation.
- Add permanent tests only after those ambiguity rules are intentional.

## 2. Precision-Aware Equality

### Current Implemented Behavior

Precision-aware equality is implemented, not design-only.

- `VALUE_DATETIME` is a first-class runtime kind (`src/eval.c:45-55`).
- `DateTime` stores a `DateTimePrecision` from year through second
  (`src/eval.c:58-75`).
- Date/time modifiers construct typed values; date/time lenses are implemented
  in `src/eval.c:2350-2438`.
- Bare date/time comparison selects the lower numeric precision of the two
  operands and compares only fields at or above that precision
  (`src/eval.c:10423-10461`).

Therefore bare `=` is already lenient:

```basic
d(date)= "2026-05-15"
t(date)= "2026-05-15 12:05:03"
if d = t then
```

evaluates true because day is the lower precision.

There is no separate built-in `same_day` comparison modifier. The implemented
equivalent explicit mechanism is the `day` lens:

```basic
if value(day)= other then
```

The `year`, `month`, `day`, `hour`, `minute`, and `second` lenses can accept
typed date/time values or parse date/time strings, subject to sufficient source
precision.

### Non-Transitivity

Current equality can be non-transitive. The exploratory test demonstrates:

```text
year(2026) = 2026-05-15
year(2026) = 2026-06-20
2026-05-15 != 2026-06-20
```

This follows directly from pairwise selection of the lower precision. It also
means date/time equality is unsuitable as a conventional equivalence relation
for hashing, uniqueness, set membership, or ordering assumptions unless those
operations define separate rules.

### Existing Tests

- `examples/datetime_test.gb` verifies bare same-day and same-month equality
  plus ordering.
- `examples/datetime_lens_test.bas` verifies all explicit date/time lenses.
- Both are registered in `tests/run_examples.sh`.
- PostgreSQL design and implementation map date/time database values into the
  same first-class runtime type.

No existing permanent test explicitly documents non-transitivity.

### New Exploratory Test

`tests/exploratory_precision_equality.bas` is not registered in the standard
suite. It passed and makes the non-transitive result visible.

### Known Risks

- The reference lists date/time values and lens comparisons but does not
  clearly state that bare equality is precision-lenient.
- Pairwise lower-precision equality violates transitivity.
- All relational operators use the same truncated comparison, so values that
  differ at hidden precision can be both `<=` and `>=`.
- Future collection operations could accidentally assume equality is an
  equivalence relation.

### Recommended Next Design Decisions

- Decide whether bare date/time `=` should remain precision-aware or require
  exact value/precision equality.
- Decide whether same-period behavior belongs behind explicit lenses or named
  comparison modifiers.
- Define collection equality, uniqueness, and ordering rules before date/time
  values are used as keys or set-like members.

No date/time behavior was changed by this validation.

## 3. Watcher Semantics and Ordering

### Ordinary Runtime Watchers

Watcher registration is active execution, not passive subscription:

- `watcher_register()` appends the watcher, enqueues it immediately, and drains
  the queue (`src/eval.c:2602-2616`).
- Every watcher therefore runs once at declaration/registration time.

After mutation:

- Assignment mutates the target, derives a watch path, and immediately calls
  `watcher_trigger_change()` before the next statement
  (`src/eval.c:11319-11334`).
- `append`/`prepend` and `take_first`/`take_last` on assignable paths similarly
  trigger immediately (`src/eval.c:9750-9792`, `src/eval.c:9873-9899`).
- Matching watchers are enqueued by ascending registration index
  (`src/eval.c:2590-2599`), which normally equals source execution order.
- The queue drains synchronously. There is no ordinary event-pump safe point,
  transaction boundary, or end-of-statement batch after the mutation call.

If watcher A mutates a value watched by B while the queue is already draining,
`watcher_trigger_change()` appends B but the nested drain returns because
`watcher_draining` is set. The outer drain later executes B. This continues
until the queue is empty or the step limit is reached.

Consequences:

- Multiple ordinary mutations are not batched.
- A watcher sees the state at its execution point, including intermediate
  state from earlier mutations.
- Multiple matching watchers run in registration order.
- Cascaded watchers run after work already present in the queue.
- The queue does not deduplicate watcher indexes. Repeated mutations can enqueue
  repeated executions.
- This is queue-to-exhaustion behavior, similar to a fixpoint pass, but there is
  no value-change check and no guarantee of convergence.
- `without watchers` suppresses trigger creation entirely. It does not enqueue
  one settled-state pass when the block exits.

Runaway behavior is limited by 10,000 dequeued watcher executions
(`src/eval.c:2561-2564`). Reaching the limit prints
`watcher queue limit reached`, discards the remaining queue during cleanup, and
does not raise the normal structured runtime error or necessarily produce a
nonzero exit.

### Path Matching and Nested Mutation

Watcher matching is one-way prefix matching:

```text
watch path == changed path
or changed path begins with watch path + "."
```

Thus `watch(state)` observes `state.value` and deeper static field changes.
However, replacing `state` does not trigger `watch(state.value)`.

Watch targets support static dotted paths. Array/dynamic indexes are omitted
from generated watch paths (`src/eval.c:11134-11161`). This makes parent array
watchers useful but is not truly index-aware or key-aware.

Nested array mutation is reliable for the explicitly lvalue-aware
`append`/`prepend` and `take_first`/`take_last` paths. Existing coverage proves
`append(state.items, ...)` and `take_first(state.items)` trigger
`watch(state.items)`.

Other array mutators such as `insert`, `remove`, `reverse`, `sort`, and
`unique` only use direct-symbol mutation paths or operate on evaluated values;
they do not use the same nested-lvalue watcher-trigger path. Path-aware
mutation behavior is therefore not uniform across all mutators.

### GUI Interaction

GTK callbacks first mutate the bound live widget record, then enqueue a GUI
pending mutation (`src/eval.c:1856-1889`). The GUI queue:

- coalesces repeated changes to the same widget id and field, keeping the last
  value (`src/eval.c:1764-1770`);
- flushes after `gtk_main_iteration_do()` returns;
- triggers ordinary watchers once per queued unique widget field;
- refreshes existing GTK widgets after watcher execution
  (`src/eval.c:1789-1819`).

GUI watchers therefore run at an explicit GUI safe point, not inside GTK
callbacks. Because live records are mutated before flushing, a watcher for the
first queued field can observe other GUI fields that have already reached
their settled queued state. Trigger dispatch itself remains sequential and
synchronous. Dynamic widget-tree mutation is not implemented.

GUI watcher verification remains manual through
`examples/gui/watch_demo.bas` and `examples/gui/README.md`; it is not part of
the automated suites.

### WebServer Interaction

WebServer is single-threaded:

- socket work occurs in `webserver_run_event_loop()`;
- after a complete request is parsed, native code appends the request to the
  live `server.requests` array and immediately calls
  `watcher_trigger_change("<server>.requests")`
  (`src/eval.c:7168-7195`);
- the watcher drains before request parsing returns to the rest of the
  WebServer event-loop iteration;
- application appends to `server.responses` trigger ordinary watchers
  immediately through the normal nested-array path;
- queued responses are transmitted and removed by WebServer processing after
  watcher/application code returns (`src/eval.c:7289-7319`).

The live arrays are ordinary mutable values plus native association. They are
not concurrently mutated. The watcher commonly consumes all currently queued
requests with a `while count(server.requests) > 0` loop. Since each parsed
request triggers immediately, normal operation usually exposes one newly
parsed request at a time rather than a native batch.

### Existing Tests

- `examples/watch_test.gb`: initial execution, mutation triggering, and
  `without watchers`.
- `examples/watch_path_test.bas`: root and static nested-path matching.
- `examples/nested_array_mutation_test.bas`: nested `append` and `take_first`
  triggering.
- `tests/webserver_integration.bas` plus `tests/run_webserver.sh`: watcher-driven
  request consumption and response append.
- GUI watcher behavior has a manual demo but no automated GTK event test.

### New Exploratory Test

`tests/exploratory_watcher_ordering.bas` is not registered in the standard
suite. It passed and confirms:

- immediate execution at registration;
- source/registration order for two watchers on the same value;
- intermediate-state visibility;
- cascaded watcher execution after already queued watchers;
- no replay after `without watchers`.

### Known Risks

- Immediate registration execution is significant behavior but is not clearly
  documented in `docs/reference.md`.
- There is no deduplication or changed-value check; assigning an equal value
  still triggers.
- The 10,000-step cutoff is an unstructured stderr warning rather than a
  catchable runtime error.
- Prefix matching does not notify child watchers when a parent is replaced.
- Index/key details are discarded from watch paths.
- Nested mutation support differs by array mutator.
- GUI batching/coalescing semantics differ from ordinary language mutation
  semantics.
- GUI behavior lacks automated event-level coverage.
- Wholesale replacement of WebServer queue fields remains unsafe and is not
  protected by the native server layer.

### Recommended Next Design Decisions

- Specify whether initial watcher execution is intentional.
- Specify immediate, statement-end, or explicit safe-point semantics for
  ordinary mutations.
- Define deduplication and repeated-trigger behavior.
- Define whether watcher processing must converge and how cycles become
  structured runtime errors.
- Define parent/child replacement notifications and index/key-aware paths.
- Decide whether all mutating collection built-ins must share one lvalue and
  watcher-notification contract.
- Explicitly document the intentional GUI batching difference if retained.

## Verification

Exploratory fixtures were run directly and passed. They remain unregistered
because they expose unresolved semantic decisions rather than stable language
contracts.

Final results on 2026-06-13:

- `make clean` followed by `make`: passed.
- `./tests/run_examples.sh`: passed.
- `./tests/run_negative.sh`: passed.
- `./tests/run_webserver.sh`: passed.
- `./tests/run_webclient.sh`: passed.
- `tests/exploratory_parser_condition_disambiguation.bas`: passed.
- `tests/exploratory_precision_equality.bas`: passed.
- `tests/exploratory_watcher_ordering.bas`: passed.

The Bison diagnostic run found no parser conflicts and one unrelated
precedence-style warning for `DOT`.

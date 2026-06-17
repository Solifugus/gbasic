# Comparison Lens Syntax Development Plan

## Executive Summary

Replace comparison-modifier syntax:

```basic
name(caseless)= "joe"
amount(rounded 2)= expected
a(day)< b
```

with explicit comparison-lens syntax:

```basic
name {caseless}= "joe"
amount {rounded 2}= expected
amount {rounded to 2}= expected
a {day}< b
a {day}>= b
```

Assignment modifiers stay unchanged:

```basic
balance(USD)= 19.95
age(number)= input("Age? ")
```

The key parser invariant is:

```text
expr{...}
```

is never an operand form. Curly braces after an expression are reserved for
comparison-lens syntax only, and only when followed by a comparison operator
and a right-hand operand.

This removes comparison dependence on parser-side parenthesis lookahead while
preserving the existing immediate syntax for assignment modifiers.

## Current Implementation Evidence

### Lexer and parser behavior

Current comparison modifiers are selected before Bison sees the token stream:

* `src/parser.y` defines `MOD_LPAREN` and `MOD_CONTENT`.
* `src/parser.y` implements `modifier_lparen_ahead()`.
* `yylex()` calls `modifier_lparen_ahead()` when the normal lexer returns
  `TOKEN_LPAREN`.
* If that lookahead accepts the form, `yylex()` calls
  `lexer_begin_modifier_content()` and returns `MOD_LPAREN`.
* `src/lexer.c` then runs `modifier_content_token()` and returns the whole
  parenthesized modifier body as one `MOD_CONTENT` token.

The grammar then has distinct productions:

```bison
assignment
    : lvalue OP_EQ expression
    | lvalue modifier OP_EQ expression
    ;

comparison_expression
    : additive_expression
    | additive_expression comparison_operator additive_expression
    | additive_expression modifier comparison_operator additive_expression
    ;

modifier
    : MOD_LPAREN MOD_CONTENT
    ;
```

The lookahead exists to decide whether a parenthesis after an expression is a
function call, grouping, or a modifier marker. It scans ahead to the closing
`)`, checks for a following comparison/assignment operator, rejects comma
content, and biases known function names back to ordinary `LPAREN`.

### Lexer modes

The only current modifier-specific lexer mode is `modifier_content_mode` in
`src/lexer.c`. It captures raw text until `)` and disables normal tokenization
inside the modifier body.

This mode is shared by assignment and comparison modifier syntax today.
Assignment modifiers can keep using it in this plan. Comparison lenses should
move to a separate brace-based path so their recognition no longer depends on
`modifier_lparen_ahead()`.

### Runtime behavior

The AST already stores comparison modifiers as `AstModifierUse` inside
`AST_EXPR_BINARY`. `eval_comparison()` resolves that use in compare context
and already supports:

* user-defined comparison modifiers
* built-in `caseless`
* date/time lenses: `year`, `month`, `day`, `hour`, `minute`, `second`
* all comparison operators: `=`, `!=`, `>`, `<`, `>=`, `<=`, `!>`, `!<`,
  `!>=`, `!<=`

The syntax migration should therefore be mostly front-end work. The evaluator
can continue receiving an `AstModifierUse` on binary comparison nodes.

## Bison Proof Work

Command run against the current grammar:

```sh
bison -Wall -Wcounterexamples -v -o /tmp/gbasic-parser-proof.c src/parser.y
```

Result:

* shift/reduce conflicts: 0
* reduce/reduce conflicts: 0
* warning: existing useless associativity for `DOT`

A temporary proof grammar was also created under `/tmp` with this shape:

```bison
comparison_expression
    : additive_expression
    | additive_expression comparison_operator additive_expression
    | additive_expression comparison_lens comparison_operator additive_expression
    ;

comparison_lens
    : LBRACE LENS_CONTENT RBRACE
    ;
```

Command run:

```sh
bison -Wall -Wcounterexamples -v -o /tmp/gbasic-parser-lens-proof.c /tmp/parser-lens-proof.y
```

Result:

* shift/reduce conflicts: 0
* reduce/reduce conflicts: 0
* warning: existing useless associativity for `DOT`

Expected conflict count after implementation: zero shift/reduce and zero
reduce/reduce conflicts. The remaining `DOT` warning is pre-existing and not
caused by comparison lenses.

## Parsing Invariant

Encode and document this invariant:

```text
expr{...}
```

is never parsed as a value expression, postfix expression, call form, index
form, record update form, or operand.

Curly braces after an expression are reserved from value syntax. They are
recognized only by the comparison grammar:

```text
LHS comparison_lens comparison_operator RHS
```

This preserves LALR(1) parsing because, after an `additive_expression`, the
parser can decide from one token:

* comparison operator means ordinary comparison
* `{` means comparison-lens comparison
* anything else reduces to a non-comparison expression

Record literals remain primary expressions that start with `{`. A record
literal appears where an operand starts, not immediately after a completed
left-hand additive expression. This keeps:

```basic
x = {name: "joe"}
```

separate from:

```basic
x {caseless}= "joe"
```

The parser may temporarily shift a `{` after a left-hand expression while it is
trying to complete a comparison. If the lens is not followed by a comparison
operator and a right-hand operand, parsing fails. That is intentional; it does
not make `expr{...}` a valid operand form.

## Lens Grammar Recommendation

Use a real grammar production for lens placement, with raw lens-content
capture inside braces:

```bison
comparison_expression
    : additive_expression
    | additive_expression comparison_operator additive_expression
    | additive_expression comparison_lens comparison_operator additive_expression
    ;

comparison_lens
    : LBRACE LENS_CONTENT RBRACE
    ;
```

The `comparison_lens` production is real grammar: it exists only in the
comparison-expression position and is not part of `primary`,
`postfix_expression`, `unary_expression`, `multiplicative_expression`, or
`additive_expression`.

Capture the lens body as raw text for now, analogous to existing
`MOD_CONTENT`, but with `}` as the terminator. Then reuse the existing
`parse_modifier_use()` and comparison modifier resolution path.

### Why raw lens content is recommended

Raw lens content preserves existing comparison modifier semantics:

* `caseless`
* `rounded 2`
* `rounded to 2`
* `rounded places`
* `text.caseless`
* `stricttext.caseless`
* user-defined multi-word comparison modifiers such as `pass through`

The current runtime resolves modifier phrases by choosing the longest declared
modifier name prefix and treating the remaining text as arguments. A fully
tokenized grammar would have to duplicate that runtime phrase-resolution rule
or introduce a breaking syntax for user-defined comparison modifiers.

Raw lens content also avoids reintroducing the old function-call ambiguity.
There is no need to inspect whether the preceding expression is a function
name, because `{` after an expression is not used for calls.

### Why not fully parse lens names now

A fully parsed grammar is attractive for the fixed built-ins:

```bison
comparison_lens
    : LBRACE CASELESS RBRACE
    | LBRACE ROUNDED expression RBRACE
    | LBRACE ROUNDED TO expression RBRACE
    | LBRACE YEAR RBRACE
    | LBRACE MONTH RBRACE
    | LBRACE DAY RBRACE
    | LBRACE HOUR RBRACE
    | LBRACE MINUTE RBRACE
    | LBRACE SECOND RBRACE
    ;
```

However, gBASIC currently allows user-defined comparison modifiers with
multi-word names and raw textual arguments. Examples already in the suite
include `rounded places`, qualified modifiers, and `pass through`. A fully
parsed lens grammar should wait until modifier declarations and modifier
arguments are redesigned together.

### Required supported forms

The raw-content lens production supports all required forms:

```basic
name {caseless}= "joe"
amount {rounded 2}= expected
amount {rounded to 2}= expected
a {year}= b
a {month}= b
a {day}= b
a {hour}= b
a {minute}= b
a {second}= b
a {day}< b
a {day}>= b
```

## Phase 1: Parser Shape and Lens Capture

### Goal

Add comparison-lens syntax while preserving old comparison modifier syntax
temporarily for migration.

### Files touched

Production:

* `src/parser.y`
* `src/lexer.c`
* `include/lexer.h`
* generated `src/parser.tab.c`
* generated `src/parser.tab.h`

Documentation:

* `docs/reference.md`
* `docs/implementation_validation.md`
* this plan, if implementation discoveries change it

### Parser changes

Add a new token:

```bison
%token <text> LENS_CONTENT
```

Add:

```bison
%type <modifier> comparison_lens
```

Add the new comparison production:

```bison
comparison_expression
    : additive_expression
    | additive_expression comparison_operator additive_expression
    | additive_expression modifier comparison_operator additive_expression
    | additive_expression comparison_lens comparison_operator additive_expression
    ;
```

Add:

```bison
comparison_lens
    : LBRACE LENS_CONTENT RBRACE { $$ = parse_modifier_use($2); }
    ;
```

Keep the old comparison modifier production in this phase only so existing
examples continue to pass while new lens coverage is added.

### Lexer changes

Add a new lens-content mode similar to `modifier_content_mode`:

* `lexer_begin_lens_content(Lexer *lexer)`
* `lens_content_token()`
* terminator: `}`
* token returned: `TOKEN_LENS_CONTENT`

The parser needs a midrule action or equivalent mechanism after shifting
`LBRACE` in `comparison_lens` so the next lexer token is raw content up to
`}`. Do not call `modifier_lparen_ahead()` for this path.

The raw lens token should:

* allow spaces and tabs
* allow strings with escaped quotes
* stop at `}` outside strings
* reject newline before `}`
* leave normal `{ ... }` record literal lexing unchanged

### Tests affected

Add lens syntax coverage without migrating old tests yet:

* new focused example for `caseless`
* new focused example for `rounded 2`
* new focused example for `rounded to 2`
* new focused example for all date/time lenses
* comparisons with every comparison operator or at least a representative
  matrix including `=`, `<`, `>=`, `!>=`, and `!<=`

### Migration strategy

This phase is additive. Existing code using comparison modifiers with
parentheses remains valid.

### Stop point

Stop when:

* `bison -Wall -Wcounterexamples -v` reports zero conflicts
* `make clean && make` passes
* existing suites pass
* new lens examples pass
* no assignment modifier behavior changes

## Phase 2: Standard Test Migration

### Goal

Migrate standard comparison-modifier tests and examples from parenthesized
comparison syntax to brace comparison-lens syntax.

### Files touched

Examples and outputs:

* `examples/function_call_comparison_test.bas`
* `examples/function_call_comparison_test.out`, only if output changes
* `examples/parser_hardening_test.bas`
* `examples/parser_hardening_test.out`, only if output changes
* `examples/modifier_test.gb`
* `examples/modifier_test.out`, only if output changes
* `examples/parse_test.gb`
* `examples/datetime_lens_test.bas`
* `examples/datetime_lens_test.out`, only if output changes
* `examples/qualified_modifier_test.bas`
* `examples/modifier_library_regression_test.bas`
* `examples/negated_comparison_test.bas`

Exploratory tests:

* `tests/exploratory_parser_condition_disambiguation.bas`
* `tests/exploratory_precision_equality.bas`, if it uses date/time comparison
  lenses later

Documentation:

* `docs/reference.md`
* `docs/tutorial.md`, if tutorial migration is included in this phase

### Parser changes

None expected beyond Phase 1.

### Lexer changes

None expected beyond Phase 1.

### Tests affected

Migrate examples:

```basic
if name(caseless) = "joe" then
```

to:

```basic
if name {caseless} = "joe" then
```

Migrate parameterized forms:

```basic
if amount(rounded 2) = expected then
if amount(rounded to 2) = expected then
if amount(rounded places) = expected then
```

to:

```basic
if amount {rounded 2} = expected then
if amount {rounded to 2} = expected then
if amount {rounded places} = expected then
```

Migrate qualified forms:

```basic
if name(text.caseless)= "joe" then
```

to:

```basic
if name {text.caseless}= "joe" then
```

Migrate date/time lens comparisons:

```basic
if d(day)= "2026-05-15" then
```

to:

```basic
if d {day}= "2026-05-15" then
```

Do not change assignment modifiers such as:

```basic
y(year)= d
d(date)= "2026-05-15"
x(end of month)= today
```

### Migration strategy

Migrate permanent examples first. Leave old syntax accepted in the parser so
any unmodified non-test programs still run.

### Stop point

Stop when all standard examples use lens syntax for comparisons, assignment
modifier examples still use parentheses, and all standard suites pass.

## Phase 3: Deprecation Diagnostics for Old Comparison Syntax

### Goal

Warn on old parenthesized comparison modifier syntax without rejecting it yet.

### Files touched

Production:

* `src/parser.y`
* possibly `src/ast.c` and `include/ast.h` if the AST needs to remember whether
  a comparison modifier came from old syntax
* possibly `src/eval.c` if warning is emitted during evaluation instead of
  parsing

Tests:

* new warning-focused tests, if the project has a stable warning harness
* otherwise a small unregistered exploratory test

Documentation:

* `docs/reference.md`
* `docs/tutorial.md`

### Parser changes

Preferred parser approach:

* create a separate nonterminal for old comparison modifier use
* set a syntax-origin flag on the binary expression or emit a parse-time
  deprecation warning

Do not warn for assignment modifiers. The assignment production must remain
unchanged and quiet.

### Lexer changes

No functional lexer changes expected. `modifier_lparen_ahead()` still exists
for old comparison syntax and assignment syntax in this phase.

### Tests affected

If warnings become part of captured output, migrate remaining standard tests
before enabling warnings in the standard suite. Otherwise warning coverage can
remain manual or exploratory until a warning harness exists.

### Migration strategy

Keep old comparison syntax operational but visibly deprecated. Documentation
should present brace syntax as canonical and parenthesized comparison syntax as
legacy.

### Stop point

Stop when old comparison syntax warns, assignment syntax does not warn, and
standard suites remain stable.

## Phase 4: Remove Old Comparison Modifier Syntax

### Goal

Reject parenthesized comparison modifiers and remove comparison dependence on
`modifier_lparen_ahead()`.

### Files touched

Production:

* `src/parser.y`
* `src/lexer.c`
* `include/lexer.h`
* generated `src/parser.tab.c`
* generated `src/parser.tab.h`

Tests:

* negative test for old comparison modifier syntax
* updated expected errors if parser wording changes

Documentation:

* `docs/reference.md`
* `docs/tutorial.md`
* possibly historical design docs if they are actively maintained

### Parser changes

Remove this comparison production:

```bison
| additive_expression modifier comparison_operator additive_expression
```

Keep assignment:

```bison
assignment
    : lvalue OP_EQ expression
    | lvalue modifier OP_EQ expression
    ;
```

Keep:

```bison
modifier
    : MOD_LPAREN MOD_CONTENT
    ;
```

for assignment modifiers only.

### Lexer changes

Remove comparison-specific behavior from `modifier_lparen_ahead()`.

The remaining parenthesis decision should be assignment-only:

* after `)`
* optional horizontal whitespace
* then `=`
* not `!=`, `>=`, `<=`, `!>`, `!<`, `!>=`, or `!<=`

The function should be renamed to make the narrower contract explicit, for
example:

```c
assignment_modifier_lparen_ahead()
```

After this phase, comparison parsing must not depend on:

* discovering whether the preceding identifier names a built-in function
* scanning same-file function declarations
* rejecting comma content to distinguish calls
* checking for comparison operators after `)`

The function may still scan for assignment modifier content because assignment
modifiers intentionally keep parenthesized syntax.

### Tests affected

Add negative tests:

```basic
if name(caseless)= "joe" then
```

should fail with a parser error that points users toward:

```basic
if name {caseless}= "joe" then
```

Also cover:

```basic
if amount(rounded 2)= expected then
if d(day)= "2026-05-15" then
if name(text.caseless)= "joe" then
```

Ensure assignment still passes:

```basic
age(number)= input("Age? ")
balance(USD)= 19.95
```

### Migration strategy

Remove old comparison syntax only after all standard examples and docs use
brace lenses. If external user compatibility matters, split this phase into a
warning release and a later removal release.

### Stop point

Stop when:

* old comparison syntax is rejected
* assignment modifiers still parse and evaluate
* function calls in comparisons do not require special lexer lookahead
* `bison -Wall -Wcounterexamples -v` reports zero conflicts
* all standard suites pass

## Phase 5: Documentation and Cleanup

### Goal

Clean up references and remove dead compatibility code.

### Files touched

Documentation:

* `docs/reference.md`
* `docs/tutorial.md`
* `docs/project_state.md`, if the implementation summary needs wording
* `docs/implementation_validation.md`, if it is maintained as current evidence

Production:

* `src/parser.y`
* `src/lexer.c`
* `include/lexer.h`
* `src/main.c`, if `--add-loads` or unresolved modifier analysis mentions old
  comparison syntax

Tests:

* remove or archive exploratory tests that only exist for old disambiguation
* keep permanent regression coverage for function-call comparisons and lens
  comparisons

### Parser changes

No new syntax. Confirm the grammar contains only:

* assignment parenthesized modifiers
* comparison brace lenses

### Lexer changes

Delete any function-discovery logic that exists only to decide whether
`foo(...)` before a comparison is a call or comparison modifier.

Depending on the Phase 4 implementation, this may remove or simplify:

* `source_declares_function()`
* `modifier_lparen_ahead()`, or its comparison branches
* lexer branches that return `MOD_LPAREN` for comparison contexts

Keep only what assignment modifiers still require.

### Tests affected

Final test set should include:

* `if len(name) > 0 then`
* `if starts_with(name, "A") then`
* `if number(age_text) >= 18 then`
* `if name {caseless}= "bob" then`
* `if amount {rounded 2}= expected then`
* `if amount {rounded to 2}= expected then`
* `if d {day}= "2026-05-15" then`
* `if d {day}< later then`
* `if d {day}>= earlier then`
* assignment modifier examples with unchanged parenthesized syntax

### Migration strategy

After cleanup, document old comparison syntax only in a compatibility or
historical section, not in the main language reference.

### Stop point

Stop when source, docs, examples, and negative tests all agree on one
comparison-lens syntax and one assignment-modifier syntax.

## Affected Tests and Examples

Known standard files with comparison modifier syntax:

* `examples/function_call_comparison_test.bas`
* `examples/parser_hardening_test.bas`
* `examples/modifier_test.gb`
* `examples/parse_test.gb`
* `examples/datetime_lens_test.bas`
* `examples/qualified_modifier_test.bas`
* `examples/modifier_library_regression_test.bas`
* `examples/negated_comparison_test.bas`

Known exploratory file:

* `tests/exploratory_parser_condition_disambiguation.bas`

Known negative file:

* `tests/negative_function_result_modifier.bas`

Files that contain parenthesized assignment modifiers and should not be
migrated:

* `examples/lower_upper_modifier_test.bas`
* `examples/dates_lib_test.bas`
* `examples/keyword_stability_test.bas`
* `examples/duration_test.gb`
* `examples/number_string_modifier_test.bas`
* `examples/input_trimmed_integration_test.bas`
* `examples/modifier_string_helpers_test.bas`
* `examples/nested_lvalue_test.bas`
* adventure and BAG examples using `(trimmed)`, `(lowered)`, `(split)`,
  `(number)`, `(file)`, or date assignment modifiers

## Verification

For every implementation phase, run:

```sh
bison -Wall -Wcounterexamples -v -o /tmp/gbasic-parser-proof.c src/parser.y
make clean && make
./tests/run_examples.sh
./tests/run_negative.sh
./tests/run_webserver.sh
./tests/run_webclient.sh
```

Run WebServer and WebClient scripts only when present. The syntax work should
not affect runtime networking behavior, but the project standard verification
should stay broad.

Also run the affected examples directly while migrating them so parse failures
are localized.

## Open Questions

* Should brace lenses remain raw text until the whole modifier system is
  redesigned, or should user-defined comparison modifiers receive a new,
  fully parsed argument syntax at the same time?
* Should Phase 1 allow comparison lenses on any left-hand expression, or keep
  the current restriction to variables, fields, and indexes until a separate
  design decision relaxes modifier targets?
* What exact parser error should old comparison syntax produce after removal?
* Should `expr {lens}` without a comparison operator produce a custom
  diagnostic explaining that lenses only apply to comparisons?
* Should the deprecated parenthesized comparison syntax warn for one release
  before removal, or can it be removed immediately because the language is
  still pre-stable?
* Should documentation call all brace forms "comparison lenses", or reserve
  "lens" for date/time and call user-defined forms "comparison modifiers in
  lens syntax"?

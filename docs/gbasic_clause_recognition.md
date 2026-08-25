# R3 — Clause recognition

Status: **investigation, since RULED ON and implemented.** Options A and F were
adopted and shipped as PLAT-CLAUSE; option D was explicitly deferred as a
language-design question. See §8 for what actually landed, what remains broken,
and the two answers PLAT-CLAUSE's Step 0 added to this document.

Written 2026-07-29, against `5078042` (post PLAT-DEBT). §8 appended after
implementation.

PLAT-DEBT found that `if (m1 - m0) > 0` does not parse, because `if (` is lexed
as `MOD_LPAREN`. It was correctly left alone at the time: it is a distinct
problem from the escape bug, it fails loudly, and changing when a clause is
recognised is a grammar-adjacent decision. This is the investigation that was
deferred.

The headline is that the reported symptom is the smaller half of the problem.

---

## 1. The current rule, exactly

### Where the decision is made and committed

`modifier_lparen_ahead()` — **`src/parser.y:373`**. It is called from the
token-translation shim at **`src/parser.y:1436`**:

```c
case TOKEN_LPAREN:
    if (modifier_lparen_ahead(ctx, token.start)) {
        lexer_begin_modifier_content(ctx->active_lexer);
        return MOD_LPAREN;
    }
    return LPAREN;
```

Two things follow from this shape and both constrain every option below.

**The decision is made on raw source text, not on tokens.** Its inputs are the
parse context and a `const char *` pointing at the `(` in the source buffer. It
scans the raw characters forward from the `(` and backward from it. There is no
token stream, no previous-token record in the context (there is no `prev_token`
field anywhere in `parser.y`), and no parser state beyond the source pointer.

**The decision is irrevocable once taken.** `lexer_begin_modifier_content()`
mutates lexer state, so the *next* token is a raw `MOD_CONTENT` span rather than
ordinary tokens. There is no backtracking: by the time the grammar could object,
the text has already been consumed as raw clause content. This is why a misfire
surfaces as `syntax error, unexpected MOD_LPAREN` rather than as a quiet
mis-parse — and it is the one genuinely good property of the current design.

### What makes it fire

Reading `src/parser.y:373-470`, a `(` becomes `MOD_LPAREN` when **all** of:

1. **The content is "simple".** Scanning forward, only identifiers, numbers,
   strings, whitespace and the characters `+ - * / . [ ]` are accepted. A comma
   returns 0 immediately (`:424`). Any other character — notably `(`, `>`, `<`,
   `=` — returns 0 (`:429`).
2. **At least one term was seen** (`saw_term`), and the scan ended on `)`
   (`:433`).
3. **The `)` is immediately followed by a comparison or assignment operator** —
   `=`, `>`, `<`, or `!` followed by one of those (`:440`). Anything else returns
   0.
4. **The identifier immediately before the `(` is not a known function**
   (`:461`). "Known" means `gbasic_builtin_function(name)` — the static builtin
   registry — or `source_declares_function(ctx, name)`, which re-scans **the
   current source file** for a `function <name>` declaration.

Condition 3 is not arbitrary. It exists because both grammar positions that
consume a clause require an operator immediately after it:

- **`src/parser.y:626`** — `lvalue modifier OP_EQ expression`, the assign form:
  `f(file) = path`.
- **`src/parser.y:1005`** — `additive_expression modifier comparison_operator
  additive_expression`, the compare form: `name(caseless) = "joe"`.
- **`src/parser.y:1002`** — the same compare shape for the `{...}` lens form.

### The three-way ambiguity, and how it is resolved today

A modifier clause, a function call and a parenthesised expression can all present
as *something* followed by `(`.

| | distinguished by | at what stage |
|---|---|---|
| function call | condition 4 — backward scan for a known function name | parse time, **current file only** |
| parenthesised expression | conditions 1–3 — content shape and the trailing operator | parse time, text only |
| modifier clause | whatever survives both | parse time |

The critical observation is that **the compare form and a parenthesised
expression are structurally identical**. `X (m) > Y` is the compare form; `(a - b)
> 0` is a parenthesised expression compared against something. The lookahead
distinguishes them only by hoping the content of a real clause looks like a
modifier phrase and the content of an expression does not — and `a - b` looks
exactly like a modifier phrase, because `-` is in the accepted set.

### When are modifiers "known"?

PLAT-DEBT noted the raw capture is deliberate so a clause can be split into name
and argument once registered modifiers are known. Establishing *when* that is
bounds option C below.

**The parser knows no modifier names at all.** Grepping `src/parser.y` for any
modifier name, name table, or registry returns nothing; the only namespace it
consults is `gbasic_builtin_function` (functions) and `source_declares_function`
(functions). There is no `source_declares_modifier`.

**Modifiers are registered at eval time.** `modifier_register_def()` —
`src/eval.c:4351` — is reached from statement evaluation and from
`eval_program`'s pre-registration pass; resolution happens in
`modifier_resolve()`, `src/eval.c:4306`. Library modifiers arrive later still,
when `load` executes.

So the ordering is: **lex/parse decides → eval registers → eval resolves.** The
decision is committed two stages before the information that would inform it
exists. This is not an implementation accident that could be reordered cheaply:
`load` is a runtime statement, so the set of modifiers is not statically knowable
from one file at all.

---

## 2. The measured misfire surface

Measured by running programs, not by reading. Every case below was executed.

### Class A — a parenthesised expression followed by a comparison

`if` is not special. **Every context tested fails**, because nothing about the
preceding context is consulted unless it is an identifier:

| context | example | result |
|---|---|---|
| after `if` | `if (a - b) > 0 then` | `unexpected MOD_LPAREN` |
| after `while` | `while (a - b) > 99` | `unexpected MOD_LPAREN` |
| after `return` | `return (a - b) > 0` | `unexpected MOD_LPAREN` |
| after `print` | `print (a - b) > 0` | `unexpected MOD_LPAREN` |
| after `=` | `c = (a - b) > 0` | `unexpected MOD_LPAREN` |
| after an operator | `c = 1 + ((a - b) > 0)` | `unexpected MOD_LPAREN` |
| after a comma | `g(1, (a - b) > 0)` | `unexpected MOD_LPAREN` |
| after `(` | `c = ((a - b) > 0)` | `unexpected MOD_LPAREN` |
| statement start | `(a - b) > 0` | `unexpected MOD_LPAREN` |

`until` and `elseif` are not gBASIC keywords and were not applicable; `then` is
covered by the `if` row.

**What escapes class A** — the boundary, also measured:

| form | why it is safe |
|---|---|
| `if (a > b) then` | `>` inside the parens is not in the accepted content set |
| `if ((a) - b) > 0` | a nested `(` is not in the accepted content set |
| `if (abs(a - b)) > 0` | same — the inner `(` rejects it |
| `if (a - b) then` | trailing token is not a comparison operator |
| `if (a - b) and c` | same |
| `print (a - b) + 1` | same |
| `if abs(a - b) > 0` | preceded by a known builtin (condition 4) |

So class A requires: simple content, a trailing comparison operator, and no
preceding known-function name. That is an ordinary shape, which is why it is hit
in normal use.

### Class B — a call the lookahead cannot see

This class was **not** in the brief's list and is the more damaging half. It was
found in the corpus (§3) and then reproduced.

| case | example | result |
|---|---|---|
| function declared in the **same file** | `if kind(1) = "record"` | **parses** |
| function from a **loaded library** | `if helper.kind(1) = "record"` | `unexpected MOD_LPAREN` |
| **method** held in a record field | `if r.m(1) = "record"` | `unexpected MOD_LPAREN` |

The cause is condition 4's incompleteness, in two independent ways:

1. `source_declares_function` scans only `ctx->active_lexer->source` — the file
   being parsed. A function in a `load`ed library is invisible, because at parse
   time that file has not been read.
2. The backward scan stops at any non-alphanumeric character, so for
   `helper.kind(1)` it extracts `kind`, not `helper.kind`; and for `r.m(1)` it
   extracts `m`, which is a record field holding a function value and is not a
   declared function name in any file.

Class B cannot be fixed by refining the lookahead, because the information it
needs — the set of callable names — is not available at parse time for exactly
the same reason modifier names are not (§1). **This is the finding that most
constrains the options.**

### The converse: what must keep working

Measured against the prototype in §4. All of these are recognised today and must
remain so:

- `f(file) = "/tmp/x"` — the dominant corpus form, a type/reference lens
- `t(trimmed) = "  hi  "` — a no-argument assign modifier
- `p(split ",") = "a,b,c"` — an assign modifier with a string argument
- `name(caseless) = "joe barnes"` — the compare form
- `"x"{rounded 2} = y` — the `{...}` lens form

Any option that fixes `if` by breaking these is not a candidate.

---

## 3. How much existing code this touches

### Constructs relying on clause recognition

Across `stdlib/`, `examples/` and `tests/`:

- **580 clause uses** in **168 files**, `(...)` form.
- **39** uses of the `{...}` lens form.
- **37** of the 580 are in `stdlib/studio_*.bas`.

By modifier name, the top of the distribution is overwhelmingly the
reference/lens family rather than user-defined modifiers:

| name | uses | | name | uses |
|---|---|---|---|---|
| `file` | 153 | | `args` | 22 |
| `date` | 52 | | `dir` | 14 |
| `string` | 44 | | `lowered` | 12 |
| `trimmed` | 27 | | `day` | 12 |
| `number` | 25 | | `words` | 10 |

This matters for option D: the syntax is not a niche feature used in a corner,
it is how file and directory references are constructed everywhere.

### Constructs that route around the defect

Seven distinct places, found by grepping for the workaround rather than the
symptom. This is the proxy for how long this has been absorbed without being
reported as a defect:

1. **`docs/pbi_design.md:151-158`** — the strongest signal. PBI's policy
   annotations use `:` instead of `=` **specifically because** `prop (copy)= …`
   would tokenize as `MOD_LPAREN`. A language design decision was made to route
   around this rule.
2. **`docs/pbi_design.md:167,186`** — PBI twice records deliberately *not*
   reusing the clause lexer mode.
3. **`stdlib/studio_store.bas:21-22`** — `_last` exists as a bound-out helper
   "to avoid the `call(...) = x` modifier-lexer collision on inline
   comparisons".
4. **`docs/PROGRESS.md:1673-1675`** — `if monitor_alerts.severity(...) =
   "critical"` failed to parse; worked around by binding to a temp. This is
   class B.
5. **`DOGFOOD.md:294-305`** (2026-07-23, NAP-12) — `string(datagrid.row_count(g)
   = 100)` and `if reflect.kind(x) = "record"`. Also class B, logged as severity
   low.
6. **`docs/gbasic_studio_plan.md:914`** — records the collision as a known
   constraint of the language.
7. **`examples/monotonic_test.bas:46-47`** (2026-07-29, PLAT-DEBT 3) — the
   instance that prompted this investigation.

Two observations. The workarounds are **not** concentrated in one subsystem —
they span the PBI language design, the Studio store, the EDGAR monitor, the
NAP-12 DataGrid tests and the platform tests. And items 4 and 5 are class B, not
class A: the more common lived complaint is `call(...) =`, not `if (`.

---

## 4. Options, with costs

### A — restrict by the preceding token

**Rule change.** A clause may only follow something that can *end* an `lvalue` or
an `additive_expression`. Concretely: if the identifier before `(` is a reserved
keyword, it is not a clause; if there is no preceding identifier, only `]` or `)`
may precede it. `END` and `NEXT` must be exempted, because
`variable_name: IDENT | END | NEXT` (`src/parser.y:643-647`) admits them as
variable names.

**Prototyped.** Built in a scratch worktree at `5078042`, ~30 lines: a
`lexer_identifier_type()` accessor exported from `src/lexer.c`, and the gate
added to `modifier_lparen_ahead`. Measured:

- **Fixes all of class A.** Every row of the class A table now parses and runs.
- **Fixes none of class B.** `helper.kind(1) = …` and `r.m(1) = …` still misfire,
  because the preceding token *is* an identifier and the gate passes.
- **Breaks nothing measured.** Full suite green: examples 200, negative 281, and
  `run_studio` (94), `run_outline`, `run_gui_parse`, `run_stridx`, `run_arridx`,
  `run_process`, `run_stderr`, `run_stream`, `run_try_decode`,
  `run_json_diagnostics`, `run_docs_gate`, `run_nap_fs`, `run_sqlite`, `run_gi`,
  `run_native_platform` all pass unchanged.
- All five must-keep-working forms in §2 still work.

**Does any currently-legal program change meaning?** No silent change was found.
The gate only ever turns `MOD_LPAREN` into `LPAREN`, i.e. turns a parse error
into a successful parse. A program that parses today either still parses
identically, or was relying on a clause directly preceded by a keyword — which
is not reachable, since no grammar position admits `if (mod) = x`.

**Lookahead refinement or grammar change?** Refinement. No rule, token or
precedence changes; the grammar is untouched.

**Cost.** Small. The prototype is ~30 lines. The real cost is the keyword
exemption list and the tests to pin it: every keyword in `identifier_type`
(`src/lexer.c:265`) needs a decision, and `END`/`NEXT` are known exceptions —
there may be others.

**`source_outline` blast radius.** Schema and ranges unchanged. Measured on an
ordinary file, before and after: identical (`ok=true, nodes=18, diags=0`). The
only change is for a file that previously *failed to parse*: `ok=false, nodes=0,
diags=1` becomes `ok=true, nodes=5, diags=0`. That is strictly the correct
direction — a valid program stops being reported invalid — and it moves no
golden (`run_outline` and `run_studio` both pass). STU-3's "last-known-good on
invalid source" path is simply exercised less often.

### B — restrict by what follows the `(`

**Rule change.** Refine the forward scan or the trailing-operator test rather
than the trigger.

**Analytically dead for class A, no prototype needed.** The compare form is
`additive_expression modifier comparison_operator additive_expression`. Its text
after the `(` is a modifier phrase, then `)`, then a comparison operator. A
parenthesised expression in `if (a - b) > 0` presents *exactly* that text. There
is no refinement of the forward scan that separates `(caseless) =` from
`(a - b) >`, because `a - b` is a legal modifier phrase shape — `rounded 2` and
`split ","` are the same shape. Tightening the content set (e.g. forbidding `-`)
would break real clauses and still not separate `(trimmed) =` from `(x) =`.

**Cost.** Not viable alone. Could narrow class A slightly at the price of
rejecting legal modifier phrases.

### C — require the clause name to be a registered modifier

**Blocked by timing, established in §1.** The parser knows zero modifier names,
and modifiers are registered at eval time, after `load` runs. Making this work
would require either:

- a parse-time modifier declaration scan analogous to
  `source_declares_function` — which would cover only same-file modifiers and so
  reproduce class B's incompleteness exactly, breaking every clause whose
  modifier comes from a library (the `file`/`dir`/`date` family, i.e. most of
  the 580); or
- deferring the lex decision until eval, which means not committing to
  `MOD_CONTENT` at token time — a re-architecture of the clause mechanism, not a
  refinement.

**Cost.** Large, and the cheap version is actively wrong.

### D — a distinguishing syntactic marker on clauses

**Rule change.** Give clauses an unambiguous marker, e.g. `f(:file) = path` or
`f[file] = path`, so no lookahead is needed.

**Breaks existing programs: yes, comprehensively.** 580 uses across 168 files,
plus 39 lens uses if the lens form changes too. Every `f(file) = path` in the
tree would need rewriting, including 37 in Studio.

**Grammar change,** and it also removes the need for `modifier_lparen_ahead`
entirely, which would delete a whole class of ambiguity — including class B —
permanently. It is the only option that fixes both classes.

**Cost.** Very large in migration; moderate in implementation. Would need a
deprecation path, or a mechanical rewrite of the corpus, or both syntaxes
supported for a period. `source_outline` blast radius is nil for the schema, but
every existing document's ranges shift if the source text is rewritten.

### E — leave it, document the constraint

**Cost.** Zero to build. The ongoing cost is measured in §3: seven recorded
workarounds, one of which redirected a language feature's syntax, and a defect
class (B) that produces a confusing error for ordinary method and library calls.
The constraint is currently documented in scattered places
(`docs/pbi_design.md`, `DOGFOOD.md`, `docs/gbasic_studio_plan.md`,
`docs/PROGRESS.md`) but is **not** in `docs/ai/UNLEARN.md`, which is where a
contributor would look.

### Not evaluated, but suggested by the code

**F — extend condition 4 to qualified names.** The backward scan could recognise
`helper.kind` rather than `kind`, and treat *any* dotted name as a call rather
than a clause. Clauses are never dotted at the call site (the library
qualification lives *inside* the parens — `parse_modifier_use` splits on `.`
within the clause text, `src/parser.y:308`). This would fix the `r.m(1) =` and
`helper.kind(1) =` rows of class B without needing to know any names. It would
not fix an *unqualified* call to a library function. This is a lookahead
refinement and composes with A; it was not prototyped.

---

## 5. Scanner census

PLAT-DEBT found three scanners by tracing one clause end to end and said it had
not proved a fourth did not exist. Searching instead for the pattern (`in_string`
tracking and quote handling across `src/*.c`, `src/*.y`, `src/modules/*.c`)
finds **ten** paths that scan gBASIC clause or literal text, plus four that scan
a deliberately different dialect.

### gBASIC literals and clause bodies

| # | function | file:line | handles escapes consistently? |
|---|---|---|---|
| 1 | `string_token` | `src/lexer.c:78` | **reference** — validates, rejects invalid escapes |
| 2 | `modifier_content_token` | `src/lexer.c:136` | yes (fixed in PLAT-DEBT 4) |
| 3 | `lens_content_token` | `src/lexer.c:178` | yes (already did) |
| 4 | `copy_string_literal` | `src/parser.y:78` | **reference decoder** |
| 5 | `source_declares_function` | `src/parser.y:214` | yes — tracks strings *and* comments |
| 6 | `modifier_lparen_ahead` | `src/parser.y:373` | yes (fixed in PLAT-DEBT 4) |
| 7 | `modifier_string_literal` | `src/eval.c:20556` | yes — mirrors #4 (added PLAT-DEBT 4) |
| 8 | `eval_modifier_arg_text` | `src/eval.c:20648` | delegates to #7 |
| 9 | `modifier_args_have_comma` | `src/eval.c:20702` | yes |
| 10 | `bind_modifier_args` | `src/eval.c:20742` | **NO** — see below |

**#10 is the one genuine inconsistency, and it is demonstrably broken.** The
multi-argument path splits on commas with a bare `strchr(start, ',')`
(`src/eval.c:20772`), with no string awareness. Reproduced:

```basic
modifier wrap(a, b) for compare ... end modifier
if "x"{wrap "L,R", "T"} = "xL,RT" then
```
→ `undefined variable: "L` and `undefined variable: R"`

It splits inside the string literal and then looks up the fragments as variable
names. It fails loudly rather than silently, but the error names a fragment of
the user's own string, which is baffling rather than diagnostic.

Reachable only through the `{...}` lens form: the `(...)` form rejects a comma in
the lookahead (`src/parser.y:424`), so a multi-argument clause cannot be written
that way at all. **Finding only — not fixed, per the non-goals.**

### Different dialects (correctly different, listed for completeness)

| function | file:line | dialect |
|---|---|---|
| `decode_parse_string` | `src/eval.c:9357` | JSON — `\/`, `\uXXXX` |
| `encode_string_literal` | `src/eval.c:7182` | `encode` output |
| `pg_json_append_string` | `src/eval.c:12372` | Postgres JSON output |
| attribute quoting | `src/modules/xml.c:523` | XML |

Two more live in gBASIC rather than C and scan JSON, not gBASIC literals:
`stdlib/studio_json.bas` (one surviving caller) and `stdlib/crypto.bas`
(flat-JSON).

---

## 6. Recommendation

**A + F together**, as a lookahead refinement, and not as a syntax change.

A is prototyped, fixes the entire measured class A, breaks nothing across ~700
test cases including all of Studio, leaves `source_outline`'s schema and ranges
untouched, and is roughly thirty lines. F is the natural companion: it addresses
the two class B rows that were actually complained about in the corpus
(`helper.kind(1) =` and `r.m(1) =`) using information the lookahead already has
in hand, without needing a name registry.

D is the only option that removes the ambiguity at its root, and if the language
were younger it would be the right answer. Against 580 call sites in 168 files it
is a migration project, not a fix, and the recommendation is to not spend that
now.

E is defensible only if paired with putting the constraint in
`docs/ai/UNLEARN.md`, where it currently is not. Seven recorded workarounds
across five subsystems is evidence that leaving it costs something real and
recurring.

### What I am uncertain about

- **A + F still leaves class B partly open.** An unqualified call to a function
  from a `load`ed library — `kind(1) = "record"` where `kind` comes from a
  library — would still misfire. I did not measure how common that is versus the
  qualified form; the corpus evidence (items 4 and 5 in §3) is qualified in both
  cases, but two instances is not a distribution.
- **The keyword exemption list is not settled.** I verified `END` and `NEXT` must
  be exempt because the grammar admits them as variable names. I did not audit
  every keyword in `identifier_type` (`src/lexer.c:265`) for the same property.
- **F is unprototyped.** Its claim — that a clause is never dotted at the call
  site — is read from `parse_modifier_use` (`src/parser.y:308`), which splits the
  library qualifier out of the clause *interior*. I did not test whether any
  corpus construct puts a dot immediately before a clause's `(`.
- **"Breaks nothing" means nothing in this corpus.** The suite is large and
  includes Studio, but it is not a proof about programs nobody here has written.

**The decision is not mine.** This document is written to make a ruling possible,
not to pre-empt one. Whether the recurring cost in §3 justifies touching clause
recognition at all — and whether a partial fix that leaves class B open is worth
having — is a judgement about the language's direction, which belongs to its
author.

---

## 7. What I could not settle

- **Whether class B has a fix short of option D.** A + F narrows it; nothing
  short of knowing all callable names closes it, and that information does not
  exist at parse time because `load` is a runtime statement. I could not find a
  formulation that avoids this.
- **Whether the trailing-operator condition could be dropped** if A were in
  place. It exists to separate a clause from an ordinary call, but with a
  preceding-token gate some of that work may be redundant. I did not test
  removing it, and doing so would widen rather than narrow recognition, so it
  carries risk I could not bound in this investigation.
- **The cost of D accurately.** I counted the call sites (580 across 168 files)
  but did not attempt a mechanical rewrite, so I cannot say whether migration is
  a scripted afternoon or a long tail of hand-edits.
- **Whether any *third-party* gBASIC exists** outside this repository that would
  be affected by A. If it does, "breaks nothing measured" understates the risk of
  any change here.

---

## 8. Outcome — PLAT-CLAUSE (2026-07-29)

Options **A and F were adopted and implemented**; option D was deferred as a
language-design question, not a defect fix. Both landed as a lookahead
refinement inside `modifier_lparen_ahead`: the parsing tables
(`yytable`/`yycheck`/`yypact`/`yydefact`/`yydefgoto`/`yypgoto`/`yyr1`/`yyr2`/
`yystos`/`yytranslate`, 601 lines) are byte-identical before and after, so the
grammar is untouched.

### What A permits, derived from the grammar rather than from the prototype

A clause may only follow a token that can **end an expression**. Only two
grammar positions consume a clause and both put a target to its left
(`lvalue modifier OP_EQ expression`; `additive_expression modifier
comparison_operator additive_expression`).

**Permitted before the `(`:** `IDENT`, `END`, `NEXT` (both admitted as variable
names by `variable_name`), `NUMBER`, `STRING`, `)`, `]`.

**Forbidden:** every other keyword, every operator, `=`, `,`, `(`, `[`, `.`,
and the start of a statement.

The permitted set is deliberately **wider than the set of legal targets**.
`is_modifier_target_expr` accepts only an identifier, a field or an index, so a
number, a string or a call result can never be a legal target — but they *can*
end an expression, so they are allowed through the lookahead and rejected by
that check instead, which says "modifier target must be a variable, field, or
index". Rejecting them earlier would replace a precise diagnostic with a generic
syntax error and would move `tests/negative_function_result_modifier`. Deciding
*could this be a clause* is the lookahead's job; deciding *is this a legal
target* is the grammar's.

### What "dotted" means in F

F rejects **only the exact shape the lexer turns into a `QUALIFIED_IDENT`**:
`IDENT . IDENT (` — one plain identifier, one dot, one identifier, then the
paren (`identifier_token`, `src/lexer.c`). For that shape the grammar has a call
production and no clause production, so reading it as a clause could only ever
be wrong.

Every other dotted form keeps its clause. This matters: a **field target reaches
the clause path legitimately when the chain is broken by an index**.
`player.inventory[slot].name(trimmed) = v` is a working clause in
`examples/nested_lvalue_test.bas` — `]` before the dot means the lexer does not
build a `QUALIFIED_IDENT`. An early, broader form of F that rejected *any*
preceding dot broke exactly that file. The corpus-wide `source_outline` sweep is
what caught it **first**, but only because it was run first — `run_examples.sh`
catches it too, and PLAT-CLAUSE's claim that the suite would have missed it was
wrong. See §9.

The backward scan still stops at the dot when extracting the name for the
function check; F reads the boundary character rather than ignoring it.

### The residual — reachable, and it fails at run time

**A + F do not close class B.** An **unqualified** call to a function from a
**loaded library**, with a single argument, followed by a comparison, is still
read as a clause: the preceding token is an ordinary identifier, so A permits it;
there is no dot, so F does not apply; and the function check cannot see across
the file boundary.

```basic
load clause_probe from "libs/clause_probe.bas"
if kind(1) = "record" then      ' `kind` lives in the library
```

It is worse than the misfires that were fixed, because it does **not** fail at
parse time. `1` is a legal clause body, so the program parses and fails at run
time with:

```
runtime error: compare modifier not found: 1
```

— a message naming a fragment of the caller's own argument list, which points
nowhere near the cause. It requires the argument list to contain no comma, since
a comma still returns 0 in the lookahead, so only single-argument calls reach it.

Pinned by `tests/negative_clause_residual.bas`, which asserts the current
behaviour rather than the desired one, so that closing it later forces the test
to be retired deliberately.

### Why option B was rejected, and the narrow form that would close the residual

B was rejected as a **complete** fix for class A, and correctly: a no-argument
clause and a single-identifier parenthesised expression are textually identical.
`(caseless)` and `(a)` are the same shape, and `modifier_name : modifier_word+`
with `modifier_word : IDENT | TO | END | NEXT` means a bare identifier **is** a
complete legal clause. No content test can separate them; only the preceding
token can, which is what A does. `if (a) > 0` is the counterexample, and it is
covered by `examples/clause_recognition_test.bas`.

But a **narrow form of B composes with A + F** and closes part of the residual.
Because `modifier_word` is only ever identifier-like, a clause body can never
begin with a digit or a quote. Requiring the first non-space character after the
`(` to start an identifier rejects `(1)` and `("x")` while accepting every legal
clause in the corpus.

It was adopted and implemented as PLAT-CLAUSE-B. **It does not close the residual
"exactly", as this section originally claimed — that overstated it.** See §9 for
what it actually closes and what cannot be closed at all.

### Effect on the recorded workarounds

Measured, and **neither was changed** (out of scope):

- **`stdlib/studio_store.bas`'s `_last` helper could now be unbent.**
  `if studio_store._last(acc) = "/" then` parses — that is a `QUALIFIED_IDENT`
  call, which F now settles.
- **PBI's `:` annotations still stand on their own.** `prop (copy)= 1` remains
  `unexpected MOD_LPAREN`, because the `(` follows an ordinary identifier, which
  A permits. Option A does not unbend PBI; nothing about that design decision
  changes.

### Part 3 — the comma splitter

R3's census found ten paths over clause and literal text, nine consistent. The
tenth, `bind_modifier_args`, split multi-argument clauses with a bare `strchr`
for a comma. It now **shares** the existing string-aware scanner rather than
gaining a tenth behaviour: `modifier_args_have_comma` was generalised into
`modifier_args_next_comma`, and both callers use it. `{wrap "L,R", "T"}` no
longer splits inside the string.

**Reachability is unchanged by A and F.** Both only ever *add* rejections — they
turn a `MOD_LPAREN` into an `LPAREN`, never the reverse — so the `(...)` form
still cannot carry a comma (the lookahead returns 0 on one), and the splitter
remains reachable only through the `{...}` lens form.

### Verification

- Full suite from `make clean`: examples 201, negative 283, and 365 further
  cases across 23 runners including `run_studio` (94). Zero rebaselines — no
  existing golden moved.
- `source_outline` compared over **693 corpus files**, old binary against new:
  **692 byte-identical**. The one difference is
  `examples/clause_recognition_test.bas`, added by this phase, which previously
  failed to parse. Schema and ranges are unmoved, so STU-3's anchors, STU-4B's
  position map and STU-5A's fingerprints are unaffected.
- valgrind clean on the clause, nested-lvalue and modifier paths, and on both
  new negative tests.

---

## 9. PLAT-CLAUSE-B (2026-07-30) — the narrow content test, and a correction

Option **B-narrow was adopted and implemented**, composed with A and F. Option D
remains deferred.

### The rule

A clause body always opens with the modifier's **name**, and a modifier name is
a sequence of `modifier_word`, which the grammar defines as
`IDENT | TO | END | NEXT` — always identifier-shaped. So the first non-space
character after the `(` must start an identifier, or the `(` is an ordinary
parenthesis.

**Identifier-start is `A-Z`, `a-z`, `_`.** Taken from the lexer's own test
(`isalpha(ch) || ch == '_'`, `src/lexer.c:428`), not assumed. Nothing in the tree
calls `setlocale`, so that runs in the C locale and is ASCII-only; a non-ASCII
byte does not start an identifier there either, and the lexer would reject it as
an unexpected character.

### What it closes, measured — and what it does not

The residual was wider than the single pinned case. Against an unqualified call
to a `load`ed library's function:

| form | before B-narrow | after |
|---|---|---|
| `kind(1) = "record"` | runtime `compare modifier not found: 1` | **fixed** |
| `kind("q") = "record"` | runtime `compare modifier not found: "q"` | **fixed** |
| `kind(x) = "record"` | runtime `compare modifier not found: x` | **still broken** |
| `kind(one(1)) = "record"` | already worked | unchanged |
| `kind((1)) = "record"` | already worked | unchanged |

§8 said B-narrow "would close the residual exactly". That was wrong, and the
error was in the direction that matters: it understated what remains.

### The identifier-argument case is unresolvable at token delivery

Not an oversight, and no refinement of this lookahead can fix it. These two lines
are the same tokens in the same order — `IDENT ( IDENT ) = STRING`:

```basic
if name(caseless) = "joe" then      ' MUST be a modifier clause
if kind(x)        = "record" then   ' MUST be a call
```

Both were run in one program to confirm it: the first works, the second fails.
Separating them requires knowing whether `caseless` is a registered modifier, or
whether `kind` is callable. Neither fact exists at token delivery — modifiers are
registered at eval time and `load` is a runtime statement (§1) — and the decision
is committed irrevocably when the token is handed over, because
`lexer_begin_modifier_content` has already changed how the next token is read.

**This is the argument for option D.** A syntactic marker on clauses removes the
ambiguity rather than narrowing it, and it is the only option that can. Recorded
here, not acted on: D remains deferred.

Pinned by `tests/negative_clause_residual.bas`, updated in this phase to assert
the *reduced* residual (`kind(x)`, the identifier form) rather than the numeric
form it used to assert, which now works.

### Monotonicity

B-narrow only ever adds a `return 0`, so like A and F it can only turn a
`MOD_LPAREN` into an `LPAREN`, never the reverse. Two consequences:

- No construct that parsed before stops parsing. Everything it changes either
  failed to parse or failed at run time.
- The `{...}` comma-splitter's reachability is unchanged **again**. The `(...)`
  form still cannot carry a comma (the lookahead returns 0 on one), and the lens
  form does not consult `modifier_lparen_ahead` at all — it is `LBRACE`,
  `lens_content_token`, `RBRACE`, a separate path.

### The coverage question, and a correction

PLAT-CLAUSE reported that a first cut of F broke
`examples/nested_lvalue_test.bas` and that "the corpus-wide `source_outline`
sweep is what caught it, not the suite". **The second half of that was wrong.**

`nested_lvalue_test.bas` is in `run_examples.sh`'s case list (line 170) and has a
`.out`. The runner treats a nonzero exit as a failure, prints `FAIL` and aborts.
Reintroducing the broad F and running the suite confirms it directly:

```
FAIL examples/nested_lvalue_test.bas
parse error at examples/nested_lvalue_test.bas:30:37: syntax error, unexpected OP_EQ, expecting NEWLINE
```

The sweep caught it first only because it was run first. There was no coverage
gap for that file, and the earlier claim should not be relied on.

**A smaller, real gap did turn up.** The runner passes a case with no `.out` on
exit status alone (`else printf 'PASS'`). Of 190 case-list entries, 5 had no
golden. Four were deterministic and path-free and are now goldened — listed as
intentional additions, since they add assertions rather than move any:

- `examples/error_test.out` (13 lines) — the most valuable of them; it pins the
  error model's messages, codes and propagation, which was entirely unasserted.
- `examples/lock_cleanup_test.out`, `examples/program_test.out` (1 line each).
- `examples/library_test.out` (empty — pins that it produces no output).

The fifth, `dir_test.gb`, is deliberately left ungoldened: it lists the live
`examples/` directory, so its output changes whenever a file is added there —
including by this phase. That is why it has no golden, and it should not get one.


---

## 10. Closed 2026-08-24 — by retiring the spelling, not by out-thinking it

Everything above analyses an ambiguity in the PAREN clause form:
`name(caseless) = "joe"` and `kind(x) = "record"` are the same tokens in the
same order, and §9 concludes the identifier-argument case cannot be separated
at token delivery. That conclusion stands. It was never wrong.

PLAT-BRACE (`brace_modifier_design.md`) removed the ambiguity by removing the
construct: a modifier clause is written `name{caseless} = "joe"`, and a brace
cannot open a call, so there is nothing to decide. `modifier_lparen_ahead` --
the ninety-line lookahead this document exists to justify -- is deleted, along
with the `MOD_LPAREN` / `MOD_CONTENT` token pair.

`tests/negative_clause_residual.*` retired with it: the behaviour it pinned no
longer exists, and `tests/brace_modifiers/residual.bas` now asserts the
opposite -- that an unqualified call to a loaded library's function, with an
identifier argument, in a comparison, parses as **a call**.

The examples in the sections above are left in the paren spelling on purpose.
Rewriting them would make the analysis incoherent: the whole argument is about
two constructs that LOOK alike, and in the new spelling they do not.

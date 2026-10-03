# Modifier clauses move to braces (PLAT-BRACE)

Status: **approved 2026-08-24.** A deliberate compatibility break; migration in
§6. Supersedes the paren clause form and closes
`docs/gbasic_clause_recognition.md` §9 by removing the construct that created
it.

## 1. What the parentheses cost

gBASIC has two constructs that are **the same tokens in the same order**:

```basic
name(caseless) = "joe"        ' a modifier clause
kind(x)        = "record"     ' a function call, compared
```

Both are `IDENT ( IDENT ) = STRING`. Telling them apart requires knowing
whether `caseless` is a registered modifier or `kind` is callable, and neither
fact exists at parse time. `docs/gbasic_clause_recognition.md` §1 and §8 work
through why no refinement at token delivery can separate them.

The consequence is not a parse error, which would at least be honest. It
PARSES, runs, and fails with:

```
runtime error at prog.bas:24:14: compare modifier not found: x
```

— naming *the caller's own argument* as though it were a misspelled modifier.
A defect that misdirects is worse than one that stops.

The machinery that guesses is `modifier_lparen_ahead` in `src/parser.y`: about
ninety lines of hand-written lookahead, complete with its own string scanner
(one of three a clause passes through), whose comment admits it cannot close
the remaining case. PLAT-CLAUSE-B narrowed the residual to *identifier*
arguments and stopped there, because that is as far as the technique goes.

## 2. Why braces end it

**A brace can never open a call.** There is nothing to guess, so the guess —
and the residual, and `modifier_lparen_ahead` itself — all go away.

The form is not new. `comparison_lens` already exists and already works:

```basic
if n{caseless} = "joe" then            ' works today
if "x"{wrap "L", "T"} = "xLT" then     ' works today, with arguments
```

What is missing is the **assignment** position, which is still paren-only:

```basic
p{USD} = 19.95                         ' parse error today
```

So this is less "design a new syntax" than "finish the one that is half
built, then retire the one that costs".

## 3. The change

```basic
price{USD}    = 19.95
due{date}     = "2026-03-01"
f{file}       = "notes.txt"
if name{caseless} = "joe" then
if a{month} = b then
```

- `lvalue comparison_lens OP_EQ expression` joins the assignment production.
  Verified **zero LALR conflicts** before this document was written.
- The paren form is **removed**, along with `modifier_lparen_ahead` and the
  `MOD_LPAREN` / `MOD_CONTENT` token pair. `(` returns to meaning exactly one
  thing.
- `docs/gbasic_clause_recognition.md` gains a closing section: the residual it
  documents is gone, and the reason is that the ambiguous spelling was retired
  rather than out-thought.

## 4. Why braces read better, not merely parse better

`price{USD} = 19.95` marks the modifier visually as *metadata about the
assignment* rather than an argument to a call — which is what it means. The
paren form looks like a call precisely because it is spelled like one, and
that resemblance is the whole problem in one glyph.

Braces are already the language's "this is a shape, not a computation" glyph:
record literals and lens clauses both use them.

## 5. What does NOT change

- Modifier semantics, names, arity, resolution order, library qualification.
- `{ a: 1 }` record literals — the brace positions are distinguishable
  (a record literal is an expression; a clause follows an lvalue or an
  operand).
- The `modifier` statement that DECLARES one.
- Field policies, `x(copy)`-style — those are a record-literal construct with
  its own production and are untouched.

## 6. Migration

Measured across both repositories before starting: roughly **400 assignment
sites** (`(date)=` 190+, `(file)=` 65+, `(string)=` 44, `(number)=` 25,
`(USD)=` 11, plus `trimmed` / `lowered` / `uppered` / `length` / `datetime` /
`time` / `dir`), and a smaller number of comparison clauses.

The rewrite is mechanical — `name(mod)=` becomes `name{mod}=` — but it is NOT
a blind regex over `(`: an ordinary call comparison must not be touched. The
migration therefore drives off the MODIFIER NAME LIST, which is closed and
known (the builtin set plus every `modifier` declaration in the tree), and
every changed file is re-parsed afterwards.

Studio is migrated in the same pass, since it is the other repository that
compiles against this grammar.

## 7. Version

Compatibility break: **0.1.0-rc6**. rc5 shipped one break already
(`on error resume next`); this is the second, and the reference gains a
migration note beside it.

## 7a. Follow-up (2026-08-24): the machinery, not only the caller

The rc6 change deleted `modifier_lparen_ahead` and the grammar's paren clause,
which made two things unreachable without making them go away: the lexer's raw
`(...)` span mode (`TOKEN_MOD_CONTENT`, `modifier_content_token`,
`lexer_begin_modifier_content`) and the parser's source-wide `function NAME`
pre-scan (`source_declares_function` with its four helpers). Both still compiled;
one of them was §1's "one of three string scanners a clause passes through", so
the cost argument this document makes was still half true in the source.

Found by auditing the token map for a different defect — an unmapped token
reached a raw `fprintf` — which is how `TOKEN_MOD_CONTENT` turned up as a token
nothing could emit. `tests/run_brace_modifiers.sh` now names all of it, because
dead code that still parses is how a retired construct comes back.

**The migration in §6 was also incomplete, and it stayed that way for a
release.** It drove off the modifier NAME list across `.bas` files, which was
the right call for the risk it was avoiding — but three test scripts embed
gBASIC in a **shell heredoc** (`tests/run_ari.sh`, `tests/run_nap_fs.sh`,
`tests/run_render.sh`), and one cookbook carried the old spelling in prose
directly above code already migrated. The three scripts failed from rc6 to rc7
and nothing went red, because every gate anyone ran was a hand-maintained list
that did not name them. `tests/run_all.sh` now discovers suites by glob, which
is the actual repair: the migration was findable, the failure was not.

Generalisation worth keeping: **a source-file sweep is a sweep of files that
look like source.** Generated programs, heredocs, and documentation prose are
the same language and are reached by none of it.

## 8. Test obligations

`tests/run_brace_modifiers.sh`: the assignment form for every builtin modifier;
the comparison form; a modifier with arguments; a library-qualified modifier;
a modifier on a field and on an index target; and the case that motivates all
of it —

```basic
load probe
k = probe.kind(x)      ' bound first, worked before
if kind(x) = "record"  ' UNQUALIFIED, identifier argument: the residual
```

must now parse and run as an ordinary call. `tests/negative_clause_residual.*`
is retired with a note pointing here, because the behaviour it pinned no
longer exists.

Negative: the paren form is a parse error naming the replacement.

## 9. Open question: several modifiers in one clause (measured 2026-10-03)

Asked by Matthew after `{trimmed,upper}=` was tried. **Not decided** — this
section records what the measurements say so the next person does not re-derive
them.

### The grammar is free; the comma is not

**No grammar change is needed for any in-brace separator.** The lexer captures
`{...}` as ONE raw token (`LENS_CONTENT`) and the parser never looks inside, so
bison's count is unchanged at **0 conflicts**. Demonstrated: `{trimmed|upper}`,
`{trimmed;upper}`, `{trimmed>upper}` and `{trimmed+upper}` all reach the same
runtime message (`assign modifier not found: trimmed|upper`) — the parser has
no objection to any of them. Compare §3's measurement that admitting the
general lens form in *expression* position costs **19**.

**But the comma is already the argument separator**, which is the fact that
decides this. Measured against a two-parameter modifier:

```basic
modifier between(lo, hi) for assign … end modifier
x {between "a", "b"}= v     ' works
x {between "a" "b"}= v      ' refused: expects 2 arguments
```

So the collision is real and concrete: `{split ",", trimmed}` reports
`split modifier expects zero or one argument` today, having read `",", trimmed`
as two arguments. **Arity cannot disambiguate it**, because `split`'s argument
is *optional* — `{split}` and `{split ","}` are both legal, so "one more
comma-separated item" is a second argument or a second modifier with nothing to
choose between them.

### And the comma's current meaning is already whitespace-sensitive

Measured, four spellings, three different outcomes:

| written | today |
|---|---|
| `{trimmed, upper}` | `assign modifier not found: trimmed, upper` |
| `{split, trimmed}` | `assign modifier not found: split, trimmed` |
| `{trimmed , upper}` | **`trimmed modifier expects no arguments`** |
| `{split ",", trimmed}` | `split modifier expects zero or one argument` |

`modifier_phrase_matches` requires the registered name be followed by
end-of-string or whitespace, so a **space before the comma** is what turns
`, upper` into arguments. Two consequences: claiming `{trimmed, upper}` for
composition breaks no working program (it has no meaning today), and the
*spaced* spelling would diverge from the unspaced one, which is a trap.

### DECIDED 2026-10-03 (Matthew): option B, with a SEMICOLON — and the
### comparison half turned out to be the valuable one

`;` was chosen over `|` on preference, and the measurements say that costs
nothing: **neither is a token in gBASIC** (`print "a"; "b"` and `print 6 | 3`
are both *lexer* errors, and bitwise operations are builtins — `band`, `bor`,
`bnot`, `bxor` — so `|` is not reserved for a future bitwise-or either). There
was no technical edge to either; the earlier draft of this section implied there
was and that was wrong.

**MY "KEEP THE COMPARISON HALF SEPARATE" ADVICE WAS WRONG, and measuring the
compare path is what showed it.** A comparison lens is not a comparison *mode*
to be composed — `eval_compare_modifier` hands a declared lens
`left`/`right`/`operator` and takes its *verdict*, so two of those cannot chain.
But `caseless` is implemented as `string_value_equal_caseless`, a **normalised
comparison**, and the datetime precision lenses directly above it already lens
**both operands** and re-enter `eval_comparison` with the modifier cleared. So
the composable thing is the **normalisation**, the pattern was already in the
same function, and one mechanism serves both halves.

Measured before building, which is why the compare half is the larger win:

| | before |
|---|---|
| `a {caseless}= b`, `a = "  Joe  "`, `b = "joe"` | **false** — the spaces defeat it |
| `a {trimmed}= b` | `compare modifier not found: trimmed` |
| what it took | `trim(lower(a)) = trim(lower(b))` — both sides, both ways |

So `caseless` was the **only** comparison lens, and an assignment modifier could
not be used to compare at all, composed or alone. Both gaps close with the one
change: every stage normalises both sides, a terminal stage (a verdict) must be
last, and `{trimmed; caseless}=` answers **true**.

### What it cost

Zero grammar conflicts, as predicted. `AstModifierUse` gained a `next` pointer —
additive, so every existing reader sees stage one. The split is in
`parse_modifier_use` (each stage may carry its own qualifier) and each stage is
**trimmed**, which the first run forced: `{trimmed; caseless}` reported
`compare modifier not found:  caseless`, with the space visible in the message,
naming a modifier that exists.

**AND IT LEAKED, which is the part worth remembering.** Three places store a
modifier use and only ONE called `ast_free_modifier_use`; the binary (comparison
lens) and assign (clause) sites open-coded `free(library); free(name);`. So the
shape change was applied once and missed twice — 8 blocks, 320 direct bytes,
caught by valgrind within minutes of the feature working. PLAT-OPTPARAM's lesson
exactly. Both sites go through the helper now and
`tests/run_brace_modifiers.sh` has a valgrind tier so the next shape change
cannot repeat it.

### Options as they stood before the decision, with effort

- **(A) Nothing.** Composition already exists for one-word modifiers by
  nesting the inline form (`{upper}{trimmed}s`, documented in the reference),
  and `x {upper}= {trimmed}s` reaches the clause case. **Cost 0.** The gap is
  that **multi-word and argument-taking modifiers have no inline form**, so
  `{end of month}` and `{split ","}` cannot compose by any route — that is the
  only thing actually unreachable today.
- **(B) A non-comma separator, e.g. `{trimmed|upper}`.** Zero grammar cost;
  reuses `modifier_args_next_comma`, which is already the single quote-aware
  top-level scanner; no ambiguity with arguments, so argument-taking stages
  compose — which is the gap in (A). Cost: a second spelling for an idea the
  inline form expresses by juxtaposition, against the `{USD}x` / `x {USD}=`
  parity §3 was careful to keep. **Estimate ~1 day.**
- **(C) Comma, disambiguated by arity.** Not recommended: unresolvable for
  optional-arity modifiers, and the failure is a *wrong argument count* rather
  than a refusal, so it guesses where this project refuses.
- **(D) Comma, refused when any stage takes arguments.** `{trimmed,upper}` is
  unambiguous and would work; `{split ",", trimmed}` is refused by name with a
  remedy. Keeps the comma, keeps zero ambiguity. **Estimate ~1 day.** Cost: it
  refuses exactly the case (A) cannot reach, so it closes no gap — and whether
  a comma means "stage" or "argument" would depend on whether the *first* stage
  took one, which is the context-sensitivity this design removed from the paren
  form in the first place.

### What any of B/C/D also has to answer

- **Which stage failed.** A clause is ONE source position, so three stages share
  it; the diagnostic must name the stage and the value it received, or
  `{a|b|c}=` failing is worse than three lines. (The shared-position hazard is
  the same one that made warning 2110 silent — see DOGFOOD.)
- **Comparison lenses share the production.** `left {caseless|trimmed}= right`
  would compose too, and a comparison *mode* is not a transformation; it needs
  its own ruling rather than inheriting this one.
- **Shape change.** `AstModifierUse` is one name plus args. A stage list touches
  ast.c (23 references), eval.c (58), ast.h (12), parser.y (7), main.c (8),
  repl.c (1) — mostly reads of `.name` a compatibility accessor could keep.

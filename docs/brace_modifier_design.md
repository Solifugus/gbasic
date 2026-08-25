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

# Typed variables: `x:number = ...`

Status: **proposal, not built.** Proposed by Matthew 2026-11-03. The rulings in
§2 were made the same day; §6 holds the questions still open.

## 1. What it is

A name followed by a colon and a data type locks the variable to that type:

```basic
x:number = form.qty       ' "42" becomes 42, and x is locked to number
x = form.other_qty        ' "5" becomes 5
x = "abc"                 ' refused, naming the declaration that locked x
x:text = "abc"            ' relocking, written out
```

A plain `=` into a locked variable converts the value to the type or refuses.
A variable that was never locked behaves exactly as it does today, so no
existing program changes meaning.

This form is **for data types only**: `number`, `text`, `boolean`, `date`,
`time`, `datetime`, `duration`, `money(USD)`, `file`, `dir`. Exactly one type,
never a chain. Transformations (`{trimmed}`, `{caseless}`, `{upper}`) and
pipelines (`{number}{trimmed}raw`, `{round(2); trimmed}`) stay lenses and
modifiers. Those apply once and do not lock anything.

### Why lock, rather than convert once

`x {number}= "42"` converts once. A later `x = "hello"` makes `x` text, and
nothing says so (measured 2026-11-03). The colon spelling is what TypeScript,
Kotlin, Swift, Go, Pascal and Python annotations use, and in every one of them
the reader takes `x: T` as a promise about `x` from then on. Python runs
`x: int = 5; x = "hello"`, but every Python type checker flags it. A person or
a model that reads `x:number` at the top of a function will assume `x` is still
a number twenty lines later. If a plain assignment could quietly change the
type, the familiar spelling would carry an unfamiliar meaning, and the failure
would be silent. Locking the type makes the familiar reading the true one.

## 2. Rulings (Matthew, 2026-11-03)

1. **The colon locks the type.** A plain `=` converts into the type or
   refuses.
2. **Relocking is allowed** by redeclaring (`x:text = ...`). The change is
   always visible in the source.
3. **A locked variable may hold `nothing` or `unknown`**, the way a typed
   G-SQL column holds a null. Otherwise every optional form field would need a
   workaround.
4. **The lock lives with the variable in its own scope.** A function's local
   `x` is a different variable. Passing `x` to a function passes the value,
   not the lock. A parameter can carry its own lock: `function f(a:number)`.
5. **A comparison reads text as the declared kind.** With `x` locked to
   number, `x = "1"` reads `"1"` as a number, as G-SQL does for a typed column,
   and warning 2122 does not fire. This answers the 2026-11-03 `1 = "1"`
   question: the conversion happens where a declared kind exists
   (docs/number_text_equality_design.md §7).
6. **Record fields and loop variables are out of the first version**
   (`r.amount:money(USD) = ...`, `for each x:number in ...`). They will be
   decided once plain variables are in use.

## 3. Grammar, measured

Measured 2026-11-03 on a scratch copy of `src/parser.y`, not in the tree.

- **Assignment.** `variable_name COLON type_spec OP_EQ expression`, where
  `type_spec` is `IDENT | IDENT LPAREN argument_list RPAREN`, gives **0
  conflicts**. A label is `variable_name COLON` and must be followed by a
  newline (`label_statement NEWLINE`), so the token after the colon decides:
  a newline means a label, a name means a type.
- **Parameters.** `IDENT COLON type_spec`, with or without `= default`, in
  `parameter_list` gives **0 conflicts**, measured together with the
  assignment rule.
- **Lexer.** No change is needed. `x:number`, `y: money(USD)`, `z:date` and
  `w:text` already lex as `IDENT COLON IDENT ...` (`--tokens`, measured). The
  rule that turns a keyword before `:` into a field name applies only inside
  braces. `x: number`, with a space, lexes the same way, and it must stay
  accepted because Python style puts the space there.

## 4. Traps found while designing

- **A compound assignment must convert first.** `42 + "1"` is `421` in
  gBASIC (measured). If `x:number = 42` followed by `x += "1"` were evaluated
  as written and then converted, it would give 421, silently. For a locked
  variable, `x op= e` converts `e` to the locked type and then applies `op`,
  giving 43. This differs from an unlocked variable on purpose. The
  difference is the lock.
- **A foreign type name needs a near-miss hint.** Models will write
  `x: int`, `x: str`, `x: float` and `x: bool`. The refusal names the
  gBASIC type, for example: "`int` is not a gBASIC type; write `x:number`".
  This is the near-miss mechanism the `window` vocabulary already has.
- **A refusal names its lock.** "x is locked to number by `x:number` at
  line 12", so whoever reads the error can find the declaration.

## 5. Code generation (the 1.0 aim)

Matthew wants actual code generation by 1.0. A lock helps that in specific
ways, and it is worth being clear about which:

- **A locked variable has one representation along its live range.** A
  compiler can keep `x:number` unboxed and skip type dispatch on arithmetic,
  converting only where values enter, which is the assignment.
- **Relocking does not defeat this**, because it is lexically visible. Each
  `x:T` starts a new live range, which SSA treats as a new variable. A silent
  change by plain assignment would have defeated it, so rule 1 matters as much
  to the compiler as to the reader.
- **Parameters are where it pays most.** Inference inside a function body can
  find most types without annotations, but it stops at a call boundary.
  `function f(a:number)` gives the callee its type without whole-program
  analysis.
- **What it does not buy:**
  - **`number` is not one machine type.** It holds exact integers and also
    floats that degrade on overflow (docs/exact_number_design.md), so a
    locked number still needs that distinction. Because a number already
    records whether it is an exact integer, a compiler can still take an int64
    fast path with an overflow check (§6 Q1).
  - **Rule 3 keeps a tag.** Absence needs one even on a locked variable,
    like a nullable column. It costs a bit, not a dispatch.

The tree-walker's behaviour stays the specification, and the golden corpus is
the differential oracle for any backend.

## 6. Open questions

1. **Is `integer` a type? Not in the first version.** This was settled
   before G-SQL: `number` is exact for integers, so gBASIC needs no integer
   type. Locks bring one new fact, a place to say *whole*, but the case is
   modest. It would add input validation that mirrors a G-SQL integer column,
   and one more name to remember. The code-generation argument is weaker than
   §5 first put it: a number already records whether it is an exact integer,
   so a compiler can take an int64 fast path with an overflow check without
   being told. An `integer` lock is purely additive, so it can be added later
   if real programs keep checking for whole numbers by hand. (2026-11-03)
2. **Should a parameter lock refuse or convert?** Assignment converts, so
   `f("42")` into `a:number` would convert too. The alternative is to refuse
   at the call, which is stricter and closer to the languages the spelling
   comes from. I recommend converting, for consistency with rule 1.
3. **An optional return type**, `function f(a:number):number`. This gives
   **0 conflicts**, measured on the scratch grammar for the plain `function
   NAME(...)` form; the `export` and dotted forms are not measured yet. The
   same rule as a lock applies: `return` converts into the type or refuses,
   naming the declaration. With no return type, a function behaves as it does
   today. I recommend it, for three reasons: it tells the caller (and a model
   reading the signature) what comes back, Studio can show it on hover, and
   it carries a type across a call for code generation. Awaiting Matthew's
   ruling.
4. **What does `--ast`, `source_outline` and the LSP show for a lock?**
   Studio's hoisting rule and hover are consumers.
5. **Actors.** A value sent to an actor carries no lock, by rule 4. Confirm.

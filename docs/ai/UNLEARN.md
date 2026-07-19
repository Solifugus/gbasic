# UNLEARN — gBASIC is not QBasic/VB

Read this before writing gBASIC. Every item below is verified against the current
binary; each shows the **actual** behavior. If your BASIC instinct disagrees, your
instinct is wrong here. The full language reference is `docs/reference.md`; this
file is only the surprises.

Two resolved surprises are noted first (they used to bite and no longer do), then
the standing ones.

## Recently fixed — now works as you'd hope

- **`program NAME(args)` binds command-line arguments.** The first parameter is a
  0-based array of the strings after the script path (empty when none). Run with
  `gbasic FILE arg1 arg2`. (Previously the parameter was unbound.)
- **Top-level `goto`/`gosub` fails loudly.** They are supported only *inside
  functions*; at the top level they now raise a runtime error instead of silently
  doing nothing.

## Control flow

- **There is no numeric `for`.** `for i = 1 to n` is a parse error
  (`expecting IN`). The only loop-with-iterator is `for each`:

  ```basic
  for each x in [1, 2, 3]
      print(x)
  end for
  ```

  To count, use `while` with your own counter — but note the O(n²) array trap
  below.

- **`goto`/`gosub` work only inside functions.** At the top level they raise.

- **`0` and `""` are falsy**; any other number or non-empty string is truthy.

  ```basic
  if 0 then
      print("truthy")
  else
      print("falsy")        ' <- this runs
  end if
  ```

## Operators and lexical

- **Not-equal is `!=`, not `<>`.** `<>` is a parse error. The family
  `!>`, `!<`, `!>=`, `!<=` also exists.
- **No modulo.** `%` and `\` are lexer errors; `mod` collides with duration syntax
  (`7 mod 2` → `unknown duration unit: mod`). Compute it: `a - floor(a / b) * b`.
- **String concatenation is `+` only.** `&` is a lexer error. `+` concatenates
  when *either* operand is a string; `-`, `*`, `/` are strict numeric.
- **Comments are `'` only.** `rem` and `//` are parse errors.
- **Keywords are case-insensitive; identifiers are case-sensitive.** `IF`/`THEN`
  are fine, but `Name` and `name` are different variables (reading the wrong case
  raises `undefined variable`).

## Strings

- **`mid` is 0-based**, and strings are **not indexable**. `s[0]` raises
  (`indexing expects array[number] or record[string]`); use `mid(s, 0, 1)`.

  ```basic
  print(mid("hello", 0, 2))   ' he
  ```

- Strings are binary-safe and length-counts-bytes; case folding (`upper`/`lower`)
  is ASCII-only. See the reference for codepoint/byte builtins.

## Variables, records, arrays

- **No `dim` / `redim`.** Variables are created by assignment; reading an
  unassigned name raises `undefined variable`. (`dim x` is not a statement — it
  prints `unexpected token DIM` to stderr.)
- **Record-literal keys must be identifiers.** `{ "a": 1 }` is a parse error. Use
  `{ a: 1 }`, or bracket-assign for non-identifier keys:

  ```basic
  r = { }
  r["Retry-After"] = 5        ' hyphenated key via bracket assignment
  ```

- **`rec.field` on a missing field raises; `rec["field"]` returns `unknown`.**
  Guard optional nested reads with the bracket form.

## `print`

- **`print` takes a single expression.** `print(a, b)` and `print a; b` are parse
  errors. Concatenate: `print(string(a) + " " + string(b))`.

## Error handling — the big one

`on error resume next` does **not** work like an exception catch. See
`docs/ai/ERRORS.md` for the proven, precise model, but the rule you must
internalize: **a function cannot use `on error resume next` to catch a raise and
return a clean fallback to its caller** — the caller's statement is abandoned
regardless of what the function returns, and `error.clear()` does not rescue it.
**Pre-validate** with a non-raising check and only call the raising builtin when
it will succeed. Proof: `examples/on_error_resume_next_test.bas`.

## Performance traps (correctness-adjacent)

- **`arr[i]` inside `while i < count(arr)` is O(n²)** — each index deep-copies the
  whole array. Iterate with `for each` instead (seconds → milliseconds on large
  arrays).
- **`append(arr, x)` returns a full copy each call**, so accumulating a large list
  with `append` is O(n²). Stream/count in place when you don't need to keep the
  list.

---

Negative knowledge in one line each — feature you expect → gBASIC instead:
numeric `for` → `for each`; `<>` → `!=`; `mod`/`%` → `a - floor(a/b)*b`;
`&` → `+`; `rem`/`//` → `'`; `dim x` → just assign; `s[i]` → `mid(s,i,1)`;
`print a, b` → `print(string(a)+" "+string(b))`; exception catch → pre-validate.

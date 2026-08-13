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

  To count, use `while` with your own counter. (Older notes warn that indexing
  in a `while` loop is O(n²); that has not been true since 2026-07-23 — see
  Performance traps.)

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

- **`x(mod) = v` is a modifier clause, and `(` is otherwise just a paren.**
  `if (a - b) > 0` parses normally, as does a qualified or method call in a
  comparison (`lib.f(1) = "x"`, `rec.m(1) = "x"`). **One case still misfires:** an
  **unqualified** call to a function from a **`load`ed library** whose argument is
  a bare *identifier* — `if kind(x) = "record"` where `kind` came from a library.
  It *parses*, then fails at run time with `compare modifier not found: x`, naming
  your own argument. A number or string argument (`kind(1)`, `kind("q")`) is fine:
  a modifier name is always identifier-shaped, so those cannot be clauses. The
  identifier case is not fixable here — `name(caseless) = "joe"` is the same
  tokens in the same order and must mean a clause. Call it qualified
  (`lib.kind(x)`) or bind the result to a variable first. Pinned in
  `tests/negative_clause_residual.bas`; analysis in
  `docs/gbasic_clause_recognition.md` §9.

## Strings

- **`mid` is 0-based**, and strings are **not indexable**. `s[0]` raises
  (`indexing expects array[number] or record[string]`); use `mid(s, 0, 1)`.

  ```basic
  print(mid("hello", 0, 2))   ' he
  ```

- Strings are binary-safe and length-counts-bytes; case folding (`upper`/`lower`)
  is ASCII-only. See the reference for codepoint/byte builtins.

- **Scanning a string per character is fine.** `while i < len(s)` with
  `mid(s, i, 1)` is O(n), forward or backward, ASCII or multibyte. This was
  quadratic until PLAT-STRIDX and is the one place where old advice in older
  notes is now wrong — a 256 000-character scan went from 249 s to 0.30 s. What
  remains proportional to the string is *building* one: `s = s + x` in a loop
  copies both sides each time, so collect pieces in an array and `join` at the
  end.

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
- **Printed output does not leave the process when you pipe it.** Into a terminal,
  output appears line by line; into a **pipe** (`| less`, a log collector, a parent
  program reading you) stdio switches to block buffering and holds it until ~4 KB
  have accumulated — so a slow-printing program looks hung, and one that is killed
  loses whatever was still buffered. This is standard Unix stdio, not a gBASIC
  quirk, and `print` is not at fault. Run the program with `--line-buffered` (an
  interpreter flag, so it goes *before* the filename) when something is reading the
  output as it is produced. An `input` prompt is the exception: the interpreter
  already flushes those explicitly, so prompts are never withheld.
- **`print` always means standard output; standard error is a separate statement.**
  Write to it with **`print to error <expression>`**. There is no `print #handle`,
  no redirect form, and no builtin under another name — `error` is the only
  destination keyword, and reaching stderr by opening `/dev/stderr` as a file is a
  trap, not a shortcut: `write` on it *truncates* the file your caller was
  appending diagnostics to. `print to error` renders exactly what `print` renders
  (same values, same newline) and is prompt regardless of `--line-buffered`, which
  governs stdout alone. Put anything that is not the program's data there, or a
  caller cannot pipe your program anywhere.

## Regex

- **`match` SCANS; it does not anchor.** It is Python's `re.search`, not
  Python's `re.match`. Reading it as "matches the whole string" gives wrong
  answers with no error:

  ```basic
  print(match("xx 42", "[0-9]+").text)    ' 42  -- found mid-string
  print(is_unknown(match("xx 42", "^[0-9]+")))   ' true -- anchoring is opt-in
  ```

- **There is no `re_find`/`re_replace` family, and no `/pattern/` literal.**
  A pattern is either a plain string or a compiled `regex(p)` value, and the
  verbs you already know are overloaded on it: `contains(s, regex(p))`,
  `replace(s, regex(p), r)`, `split(s, regex(p))`. Only `match`/`match_all` are
  new names, because their return shape (a record, not an index) has no literal
  counterpart. **`find` is NOT overloaded** — it stays literal and returns a
  number.

- **A string pattern means a LITERAL, always.** `contains(s, "b*")` asks whether
  the two characters `b*` occur; `contains(s, regex("b*"))` is the pattern. The
  same split applies to `replace` and `split`. Forgetting `regex(...)` does not
  raise — it silently searches for the pattern text itself.

- **A match record carries `length`, not `end`** (`end` is a reserved word and
  cannot be a record field), and `start`/`length` are **codepoint** measures, so
  `mid(s, m.start, m.length)` composes. A miss is `unknown`, not an error:
  test with `is_unknown`.

- **`\b` does not exist**, nor lookaround, backreferences, or non-greedy
  quantifiers — the engine is POSIX ERE. `\d \D \w \W \s \S` do work (they are
  translated), but `\D \W \S` are rejected *inside* `[...]` because POSIX has no
  negated class there.

- **`contains` now takes a string as well as an array.** `contains("hello",
  "ell")` used to raise `contains expects an array`; it returns `true` now.

## Error handling — the big one

`on error resume next` does **not** work like an exception catch. See
`docs/ai/ERRORS.md` for the proven, precise model, but the rule you must
internalize: **a function cannot use `on error resume next` to catch a raise and
return a clean fallback to its caller** — the caller's statement is abandoned
regardless of what the function returns, and `error.clear()` does not rescue it.
**Pre-validate** with a non-raising check and only call the raising builtin when
it will succeed. Proof: `examples/on_error_resume_next_test.bas`.

## Performance traps (correctness-adjacent)

- **Building a string by repeated `+` is O(n²)** — each concatenation allocates
  and copies both sides. Collect the pieces in an array and `join` once. This is
  the one that is still real; measured 0.33 s to build 200 000 characters that
  way, 8.11 s for 800 000. Pinned by the negative control in
  `tests/run_arridx.sh`, which requires this to stay quadratic — so if it is ever
  fixed, that test fails and this bullet has to be rewritten rather than left
  standing as a lie.
- **Arrays are not a trap** (they were until 2026-07-23). `arr[i]` inside
  `while i < count(arr)`, and accumulating with `append`, are both **linear** —
  arrays are a shared refcounted store with copy-on-write, so reading a variable,
  indexing and passing an array to a function are O(1), and `append` grows by
  doubling. Measured at 1 000 000 elements: indexed read 1.14 s, append 0.70 s,
  `for each` 0.84 s. `for each` is still the more readable loop and is slightly
  faster, but the O(n²) reason for preferring it is gone. Guarded by
  `tests/run_arridx.sh`, which fails if any of them goes superlinear.
- **Reading a string variable is not a trap** (it used to be). It shares the
  buffer rather than copying it, so passing a large string to a function, or
  touching it inside a loop, costs nothing proportional to its size. Character
  access is O(1) and a full scan is O(n) in either direction — measured in
  `tests/run_stridx.sh`, which fails if that stops being true.
- **Records are not a trap either** (they were until 2026-08-13). Keying data by
  string — `r["k" + string(i)] = v` in a loop, then reading it back — is
  **linear**, so a record is a usable map. Two things changed: field lookup uses
  a hash index once a record is big enough to need one, and the field array is
  shared with copy-on-write instead of being duplicated on every read of the
  variable. Both were needed; either alone leaves the loop quadratic. Measured:
  building 32 000 fields 3.42 s → 0.04 s, and 128 000 fields (previously not
  finishing in useful time) 0.13 s. Reading is flat in the record's size — 300
  000 lookups of one key cost the same against a 1 000-field record as an 8 000
  -field one, where before they cost 0.65 s and 8.41 s. Guarded by
  `tests/run_recidx.sh`, which fails if a lookup ever starts scaling with the
  record again. Still proportional to the field count, by design: assigning a
  record to a name that already holds an equal one compares them field by field
  to decide whether a watcher fires.

---

Negative knowledge in one line each — feature you expect → gBASIC instead:
numeric `for` → `for each`; `<>` → `!=`; `mod`/`%` → `a - floor(a/b)*b`;
`&` → `+`; `rem`/`//` → `'`; `dim x` → just assign; `s[i]` → `mid(s,i,1)`;
`print a, b` → `print(string(a)+" "+string(b))`; exception catch → pre-validate;
`print #f` / stderr redirect → `print to error x`.

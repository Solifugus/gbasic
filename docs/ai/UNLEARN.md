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
- **`{upper}=`, `{lower}=` and `{trim}=` work** (2026-08-18). The string
  modifiers accept both the builtin's name and the participle
  (`uppered`/`lowered`/`trimmed`), so the near-miss that used to raise
  `assign modifier not found: upper` — this file's most-hit trap — is gone.
  Both spellings are the same modifier; existing code is untouched.
- **`p = $19.99` now explains itself** (2026-08-18): the lexer error says
  `'$' is not a money literal; write p{USD}= 19.99` instead of
  `unexpected token`.

## Assignment

- **Compound assignment exists: `+=`, `-=`, `*=`, `/=`.** `x op= e` is defined
  as `x = x op e` and inherits every rule the operator has, so it covers
  numbers, strings, `date + duration`, money, durations, record fields and
  array indexes — and refuses what the operator refuses (`list += [1]` raises).
  It is STATEMENT-LEVEL: there is no `y = (x += 1)`, and no `++`/`--` at all.

- **`default(value, fallback)` covers a missing value**, and covers BOTH
  shapes: `unknown` (what `env` gives for an unset variable) and `nothing`
  (what `find` gives on a miss). It tests presence, not truthiness — `false`,
  `0` and `""` are values and pass through. Prefer it to the
  `if is_unknown(x) then x = ...` dance.

## Control flow

- **The counted `for` exists as of 2026-08-16, with BASIC semantics, not C's.**
  `for i = 1 to 5 … end for`: `to` is INCLUSIVE, `step` may be negative or
  fractional, bounds are read ONCE at entry, and afterwards the counter holds
  the last value the body saw (3 after `1 to 3` — QBasic would say 4).
  `for i = 5 to 1` with no step does not run; `step 0` raises. There is also a
  post-test loop, `do … until c` (a STOP condition) — but **no
  `repeat … until`**, because `repeat` is a string builtin. `for each` iterates
  arrays as before. (Older notes saying gBASIC has no numeric `for` predate
  2026-08-16; older notes about O(n²) indexing predate 2026-07-23.)

- **A `for` closes with `end for`, `next`, or `next <name>`** — one statement,
  three spellings, all current. A NAMED terminator must name that loop's own
  variable: unlike classic BASIC, `next x` does NOT close an enclosing `y` loop
  by implicitly closing both; the mismatch is a load-time error. `next` is not
  a reserved word — `next = 5` is still an ordinary assignment.

- **`break` and `continue` take an optional loop name.** `continue x` abandons
  the inner loop and resumes the loop over `x`; `break x` leaves it. The name
  is a loop VARIABLE, so it only ever selects a `for` — a named flow travels
  straight through any `while` or `do` in between, which have no name to match.
  Naming a loop that does not enclose the statement is a located runtime error,
  not a silent exit.

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
- **`mod(a, b)` exists** (0.1.0-rc7), and it is **FLOORED** — the result takes
  the sign of the DIVISOR, so `mod(-7, 3)` is `2`, not `-1`. **That differs
  from QBasic's `MOD`**, which truncates. Floored was chosen because it is what
  the workaround this replaces computed (`a - floor(a/b)*b`) and what at least
  four libraries already relied on — `stdlib/forensics.bas`'s civil-date
  algorithm is only correct for negative years under floored semantics.
  The infix `%` is still a lexer error, and `7 mod 2` is still duration syntax
  (`unknown duration unit: mod`) — `mod` is a CALL.
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
  identifier case is not fixable here — `name{caseless} = "joe"` is the same
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
  unassigned name raises `undefined variable`. `dim` is reserved for one purpose:
  `dim x` is a located parse error that says to assign instead.
- **Record-literal keys must be identifiers.** `{ "a": 1 }` is a parse error. Use
  `{ a: 1 }`, or bracket-assign for non-identifier keys:

  ```basic
  r = { }
  r["Retry-After"] = 5        ' hyphenated key via bracket assignment
  ```

- **`rec.field` on a missing field raises; `rec["field"]` returns `unknown`.**
  Guard optional nested reads with the bracket form.

- **`=` on two records or arrays is a deep, structural comparison** — and field
  order does not matter, because a record is a mapping:

  ```basic
  print({ a: 1, b: 2 } = { b: 2, a: 1 })    ' true
  print([{ x: [1, 2] }] = [{ x: [1, 2] }])  ' true  -- all the way down
  print([1, 2] = [1, 2, 3])                 ' false
  ```

  This was wrong until 2026-08-14: every pair of compound values compared
  **equal**, because they fell through to a numeric coercion where both sides
  became `0`. If you read older code that avoids comparing records, that is why.
  `contains`, `find`, `remove_value` and `consider` all use this comparison, so
  they were wrong in the same way — a search through an array of records
  matched the first element and reported success.

- **Ordering operators refuse compound values.** `{a:1} < {a:2}` raises
  `records support only = and !=`, and likewise for arrays. There is no
  defensible `<` between two records, and the old coercion silently answered one
  anyway. Pinned by `tests/run_equality.sh`.

- **A child dies with the interpreter, even under `kill -9`** (0.1.0-rc7).
  `process.start` and `process.run` children arm a kernel parent-death signal, so
  there is no way to launch a process that outlives the program. That was always
  the documented intent; until rc7 it was enforced by an exit-time pass that a
  SIGKILL skipped. A process that must survive belongs to a service manager.

- **A reported parse error FAILS the parse** (0.1.0-rc7). At top level — a file
  with no `program` block — a token the grammar has no place for used to
  truncate the file silently: the statements before it ran, everything after it
  vanished, and the process exited 0. `dim x` and any byte the lexer cannot read
  both did it. If you have a script that seemed to stop halfway with no
  complaint, this was why.

- **A raise inside a watcher body STOPS the program** (0.1.0-rc5). It used to
  be dropped — the watcher never fired, execution continued, and the error only
  surfaced at exit — so results were built on a watcher that had not run.

- **A typed-value modifier RAISES when it cannot construct** (0.1.0-rc5):
  `d{date} = "not-a-date"`, and likewise `time`, `datetime`, `file`, `dir`.
  They used to print an unlocated line and assign `nothing` with exit 0, so a
  bad date became a `nothing` that flowed onward. Validate the string first if
  a bad value is expected rather than exceptional (this is what
  `stdlib/ari.bas` does for imported reports), or arm `on error goto next`.

- **An out-of-range index RAISES, on read as well as write** (0.1.0-rc5). It
  used to print an unlocated line and hand back `nothing` with exit code 0, so
  the failure looked exactly like a legitimate `nothing`. Test with `count(a)`
  before indexing, or arm `on error goto next`.

- **A `goto`/`gosub` to a label that does not exist RAISES** (0.1.0-rc5). It
  used to print and then ABANDON THE REST OF THE FUNCTION, returning `nothing`
  and exiting 0 — a typo'd label silently truncated the function.

- **`find` answers `nothing` for a miss, not `-1`.** Test with `is_nothing`,
  not by comparing to an index — position `0` is a real hit.

## `print`

- **`print` takes a single expression.** `print(a, b)` and `print a; b` are parse
  errors. Concatenate: `print(string(a) + " " + string(b))`.
- **`print` and `string()` render identically** — one renderer, as of
  2026-08-14. Records and arrays show their contents:

  ```basic
  print({ a: 1, b: "two" })     ' {"a":1,"b":"two"}
  print(["a", "b"])             ' ["a","b"]
  print(2 days 3 hours)         ' 2 days 3 hours
  ```

  Before that, `print` had its own renderer that understood only numbers inside
  an array: `["a","b"]` printed as `[?, ?]` and a record printed as the literal
  word `{record}`. If you find older code calling `print(string(x))` to see a
  record, that is why — the wrapper is now redundant. Pinned by
  `tests/run_render.sh`, which requires the two to stay byte-identical.
- **A number renders the same bare or nested**, in display and in JSON alike:
  `print(0.1)` and `print([0.1])` give `0.1` and `[0.1]`, and
  `encode({a: 0.1})` gives `{"a":0.1}`. All of it is shortest-round-trip, so
  `decode(encode(x))` returns exactly `x`. (`encode` emitted 17 digits until
  2026-08-14 — lossless but noisy, and it disagreed with `print`.)
- **Displaying a value never raises, but encoding one can.** `string()` and
  `print` have a text form for every value, including a date or a function
  nested inside a record. `encode` and `json_encode` deliberately **refuse**
  those — a lossy token would produce text `decode` cannot read back. So
  `print({ when: aDate })` is fine and `encode({ when: aDate })` raises.
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

## Numbers on screen

- **`print` shows the number the program actually holds, in full.** Until
  2026-08-14 it rendered about six significant digits, so `265550.75` came out
  as `265551` and a program could compute a total it had no way to display.
  It now prints the **shortest decimal that reads back as the same value** —
  which is exact, and is why `number(string(x)) = x` for every `x`.

- **That means floating-point error is now visible, and that is deliberate.**

  ```basic
  print(0.1 + 0.2)      ' 0.30000000000000004, not 0.3
  print(1 / 3)          ' 0.3333333333333333
  ```

  Binary floating point genuinely cannot hold `0.3`, and the old format hid the
  difference rather than removing it. If you want a tidy display, round for
  display — `round(0.1 + 0.2, 2)` prints `0.30` — but do not expect the
  formatter to do it for you. For money, use a **money value**, declared with a
  currency modifier, which displays at fixed currency precision:

  ```basic
  a{USD}= 0.1
  b{USD}= 0.2
  print a + b           ' 0.30
  t{USD}= 265550.75
  print t               ' 265550.75
  ```

- **Integers are unaffected** — an integer-valued number below 2^53 still prints
  in full with no exponent and no decimal point, so ids, epoch seconds and
  bitwise results read as you would expect. At or above 2^53 the exponent form
  appears (`1e+20`), because past that point the digits a full form would print
  are not all real.

- **There is no exponent literal.** `1e20` is not a number — `e20` lexes as a
  **duration unit** and raises `unknown duration unit: e20`. Build such values
  from text: `number("1e20")`.

Pinned by `tests/run_numfmt.sh`.

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

## Names and forms that nearly work

Every item here is a case where the obvious guess is *close enough to look
right* and fails on a detail. Four of the five were hit in one sitting writing
five short sample programs, by someone who had been reading this codebase all
day — so assume you will hit them too.

- **Money and dates are modifiers, not literals.** There is no `$19.99` syntax —
  the lexer error tells you the form (`'$' is not a money literal; write
  p{USD}= 19.99`). A modifier gives the value its kind at the point of
  assignment, and the result is a real `money`/`datetime`, not a number or a
  string.

  ```basic
  price{USD}= 19.95
  print type(price)              ' money
  print price * 3                ' 59.85
  due {date}= "2026-03-01 09:00:00"
  print due + 45 days            ' 2026-04-15 09:00:00
  ```

- **There is no `today()`.** `now()` exists and returns a `datetime`, so
  `today()` is the natural guess; it fails with `invalid function call: today`.

- **`spawn` needs the call form, even with no arguments of your own.**
  `spawn worker` is a *parse* error (`expecting LPAREN`), not a runtime one.

  ```basic
  w = spawn worker(self())   ' right
  w = spawn worker           ' syntax error, unexpected NEWLINE, expecting LPAREN
  ```

- **`watch(...)` requires its variables to already exist**, and getting this
  wrong fails *late and quietly*. Written first — the natural order, since it
  reads like a declaration — it raises `undefined variable` from inside the
  watcher body, then **carries on**, and the value you expected the watcher to
  compute simply reads as `nothing` later.

  ```basic
  a = 5                  ' declare FIRST
  watch(a)
      b = a * 2
  end watch
  print b                ' 10
  a = 10
  print b                ' 20
  ```

- **`unwatch` is a reserved word (2026-08-20).** It cannot be a variable or
  label name. `watch`, `watchers`, and `without` already were; the named
  watcher form `watch name(a, b)` binds `name` as an ordinary variable, and a
  watcher body runs ONCE at registration — a `print` inside one prints
  immediately, before any change.

- **A keyword may now be a field name, both ways** (0.1.0-rc6). `{ on: 1 }`
  constructs and `r.on` reads it. Until rc6 the literal worked and the dot did
  not, which forced four renames in shipped designs (`kind:` in consolidate,
  `open`/`close` for calendar hours, `when:` and `through:` in date specs).
  Those renames are still in the code; nothing needs undoing, but new APIs no
  longer have to dodge the keyword list.

- **43 of the 47 keywords cannot be a variable name; the full list is in
  `docs/reference.md` under Reserved words (2026-08-26).** Before that there was
  no list anywhere — the docs named reserved words one at a time, scattered
  across pages, so the only way to find out was to hit one. The four that ARE
  usable as ordinary names are `end`, `loop`, `next` and `until`. The ones that
  bite hardest are short and innocuous: **`to`, `in`, `on`, `as`, `step`,
  `stop`, `error`, `new`, `each`, `do`** — `to = 5` and `from`/`to` date ranges
  are the common trap (use `start`/`finish`, or `from_d`/`to_d` as the tests
  do; `from` itself is fine).

- **`dim` is refused as a STATEMENT, and only there** (fixed 2026-08-27). It is
  an ordinary record field like every other keyword — `{ dim: 1 }` and `r.dim`
  both work. Until that date the refusal happened at token delivery, so it
  fired in every position: a record literal was rejected as "not a gBASIC
  statement" at a column where no statement is possible. Writing `dim x` still
  gets the advice it was always meant to give.

- **A function value in a variable named after a builtin is unreachable
  (warned since 2026-08-28).** Precedence is builtin → user function →
  function-valued variable, so `first = my_fn` then `first(xs)` runs the
  BUILTIN and returns an element of `xs`: a plausible value from the wrong
  function. It warns at the call site now. Holding such a variable is fine
  (`list = [1,2]`); only calling it is the mistake. A record field is immune.

- **The library-override warning is CALL-TRIGGERED, not load-triggered
  (measured 2026-08-30).** Two loaded libraries that define the same public
  name produce **no warning at all** while every call is qualified. The warning
  lives in the *unqualified* lookup, so it fires the first time something calls
  the bare name and resolves ambiguously:

  ```basic
  load alpha from "alpha.bas"     ' both define ensure_dir
  load beta from "beta.bas"
  print alpha.ensure_dir("x")     ' silent -- qualified
  print beta.ensure_dir("x")      ' silent
  print ensure_dir("x")           ' warns: 'ensure_dir' from 'beta' overrides 'alpha'
  ```

  That is correct for what it is — an override only *matters* where an
  unqualified call has to choose — but it means **the warning cannot be used
  as a namespace audit**. A collision between libraries whose functions are
  always called qualified is invisible, and stays invisible until someone adds
  the first unqualified call. Reported by the gdash session, which had exactly
  this pair: `persist` and its own `gdash_paths` both define `ensure_dir` and
  nothing warned, while a `resolve` colliding with `web.resolve` did — same
  code path, different call sites.

  For reference, `stdlib` itself currently has **seven** such shared public
  names: `at` (dates/grid), `create` (datagrid/sourceeditor), `merge`
  (consolidate/dates), `offline` (edgar/llm/market), `select` (dates/frame),
  `series` (dates/fundamentals), `with_transport` (llm/market). All are
  harmless while callers qualify them, which is why a load-time warning was not
  added: it would fire on every program that loads `dates` and `frame` and
  never writes a bare `select`.

- **`lib.fn` is a function value (2026-08-28).** It used to work only in CALL
  position: `lib.fn(x)` ran, `f = lib.fn` died with "undefined variable: lib",
  so passing a library function as a callback had no direct form. The
  workaround people reached for -- a record carrying state and function
  together, invoked as a method -- drags otherwise-private wiring into the
  caller purely for reachability. Not needed now.

- **`to_zone(now(), "UTC")` is a NO-OP, not a conversion (fixed by addition
  2026-08-28).** A gBASIC datetime carries no zone; it is civil wall-clock
  text. `to_zone` reads its input as ALREADY UTC and renders it in the target,
  so handing it a local value returns that value unchanged — silently wrong by
  your offset, with a UTC label on it. Use **`now("UTC")`**.

- **`number(dt)` and `epoch(dt)` read a datetime as LOCAL.** That is right for
  `now()` and wrong for anything you converted: `number(from_zone(now(), zone))`
  — the documented route to UTC — is off by the offset, because a UTC civil
  value is being read as local. Use **`epoch(dt, zone)`** and say which zone the
  value is in. An audit trail built the other way stores timestamps hours out
  and nothing reports it.

- **A program can set its own exit status: `exit(code)`** (2026-08-28). It
  unwinds out of any function, loop or block — `stop` that names a status.
  0–255; anything wider is refused rather than truncated, because the kernel
  keeps only the low byte and `exit(256)` would report success. Before this
  there was no way at all, and the workaround was printing a sentinel line for
  a wrapper script to exit with — which put the program's most
  externally-visible contract in the shell.

## Error handling — rebuilt in 0.1.0-rc5, so unlearn the old advice too

**`on error resume next` is gone** (`resume` is an ordinary identifier again).
The replacement is `on error goto next`, and it is **frame-scoped**: it governs
the function that executed it and nothing else.

The rule that used to matter most — *a function cannot catch a raise and return
a fallback* — **is no longer true**, and neither is the pre-validate doctrine
built on it:

```basic
function safe_div(a, b)
    on error goto next
    q = a / b
    if error then
        return -1        ' the caller never knows anything happened
    end if
    return q
end function
```

Three things that surprise on the way in:

- **Only bare `error` acknowledges.** `if error then` claims the error and is
  true exactly once; `error.message` reads without claiming. Reading only
  `error.message` leaves it pending — and the next raise then ESCAPES the frame
  rather than being absorbed (one pending error at a time).
- **A pending error survives nothing.** Return with one unacknowledged and it
  re-raises at the call site; end the program with one and it is still reported.
  Forgetting a check makes noise, never silence.
- **A callee is not covered by your handler.** It arms its own frame or its
  raise propagates through it.

See `docs/error_model_design.md`; proof in `examples/on_error_goto_next_test.bas`
and `tests/run_error_model.sh`.

**Warnings are a second channel with the same shape and two differences.**
`on warning print|ignore|goto next|stop`, read with `if warning then` and
`warning.message`. The differences matter:

- **The anti-silence rules do NOT apply.** An unacknowledged warning dies with
  its frame. It is advice.
- **Mode lookup is dynamic, not frame-local.** Your `on warning ignore`
  silences warnings raised inside what you call — the noise budget is the
  caller's, unlike error handling where a callee owns its failures.

`warning` is a SOFT name: a variable of that name shadows it, and `r.warning`
still parses. A typo'd variable called `warning` therefore reads `false` rather
than raising. Raise one with `warning("msg")` — a builtin call, not a statement.

**New in 0.1.0-rc5: `unused-result`.** Discarding a non-`nothing` return from a
gBASIC function now warns, because a function cannot change its caller — so
`pool_tick(p)` for effect does nothing at all. Builtins (`append`) and
`return nothing` are exempt.

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
numeric `for` → `for each`; `<>` → `!=`; `a % b` → `mod(a, b)` (floored);
`&` → `+`; `rem`/`//` → `'`; `dim x` → just assign; `s[i]` → `mid(s,i,1)`;
`print a, b` → `print(string(a)+" "+string(b))`; exception catch → `on error goto next` + `if error then`;
`print #f` / stderr redirect → `print to error x`.

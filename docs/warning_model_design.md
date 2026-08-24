# The warning channel (PLAT-WARN)

Status: **approved 2026-08-23**, built on PLAT-ERR (`error_model_design.md`).
Additive: no existing program changes meaning, and no reserved word is added.

## 1. Why

gBASIC's runtime has a "fail loudly" culture, but an unusual share of its worst
traps are silent: a discarded return value (which, because functions cannot
mutate their caller, is how EVERY update API is misused — and is what let
`web._serve_pool` supervise nobody through a tagged release), `contains(s, "b*")`
searching for a literal asterisk, a blind shadow of a global inside a function.

Each of those is a warning waiting to be written. None of them can be written
today, because **every warning gBASIC ships must be near-zero-false-positive**:
there is no way for a program to say "I meant that here." A measured example —
across both repos there are 2,215 bare call statements; 173 discard a
non-`nothing` return from a gBASIC-defined function, and roughly 68 of those
(excluding one test helper used 105 times) are deliberate. Without suppression
a warning firing 173 times is unusable; with it, the 68 say so once each and the
rest are real findings.

So the missing feature is not any particular warning. It is the **channel**:
suppression makes aggressive warnings affordable, and escalation makes them
enforceable. `on warning stop` is `-Werror` for a language with no build step to
put a flag on.

## 2. The model

Four modes, frame-statement-scoped, mirroring `on error`:

```basic
on warning print       ' stderr, then continue (the default)
on warning ignore      ' suppress here
on warning goto next   ' record it; check with `if warning then`
on warning stop        ' escalate: the warning becomes a raise at its own site
```

```basic
on warning goto next
r = pool_tick(p)              ' completes normally; the warning is recorded
if warning then
    log_it(warning.message, warning.line)
end if
```

### 2.1 `warning` is NOT a keyword

It appears as a bare word in exactly two places, and neither needs a token.

**Statement position** is recognized by POSITION, the technique the `server`
block proved: `ON IDENT GOTO NEXT | ON IDENT STOP | ON IDENT PRINT |
ON IDENT IDENT`, with `$2` validated *semantically* (a word that is not
`warning` is a located diagnostic, like `unknown declarative block 'observer'`).
Verified zero LALR conflicts before this doc was written.

**Expression position** uses a SOFT NAME. `error` is a hard token checked in
`env_get` *before* the environment walk — which is why `r.error` cannot parse
and why no variable may be called `error`. `warning` is resolved **after** the
walk, in the not-found path, beside the existing rule that resolves a bare
function name there:

```c
if (!symbol) {
    FunctionDef *function = function_resolve(NULL, name);   /* existing */
    if (function) { return value_function(...); }
    if (strcmp(name, "warning") == 0) {                     /* new */
        return value_pending_warning();
    }
    /* ...undefined variable */
}
```

That placement is the whole design:

- `board.warning` — field access never reaches `env_get`. Untouched
  (`examples/edgar/monitor_harness_test.bas` uses exactly this, 5 times).
- `{ warning: [] }` — a record-literal key. Untouched.
- `warning = 1` — an ordinary local, which then **shadows** the diagnostic. This
  is not a new kind of subtlety: it is the rule bare function names already
  follow ("Variables shadow this", `src/eval.c`).

Net reserved words added: **zero**.

### 2.2 A SEPARATE pending state

Warnings get their own slot on the frame (`warn_pending`, `current_warning`),
independent of the error slots. This is the load-bearing decision. If a warning
set the error's pending flag, **rule 2 would catch it** — an unacknowledged
pending diagnostic re-raises at frame exit — and every unchecked warning would
become an error, which is the one thing a warning must never do.

So, explicitly: **PLAT-ERR rules 1 and 2 do not apply to warnings.** A warning
may be replaced by a later warning, and an unacknowledged warning dies quietly
with its frame. That is what keeps them advisory. The anti-silence rules are for
failures; advice that must be acknowledged is not advice.

### 2.3 Reading

Mirrors `error` exactly:

- **Bare `warning`** asks "is there an unacknowledged warning?" and CLAIMS it:
  yields the warning object (a record — truthy) once, `false` thereafter.
  `w = warning` acknowledges and snapshots.
- **`warning.field`** reads without acknowledging, so the block body can
  describe what it caught after the condition consumed the flag. Fields:
  `message`, `code`, `source`, `line`, `column`, `path`, `details`.
- Only `on warning goto next` records. Under `print` / `ignore` / `stop`, bare
  `warning` is `false` — there is nothing pending by construction.

### 2.4 Mode lookup is DYNAMIC — deliberately unlike errors

Error mode is frame-local: a callee starts in the default state whatever the
caller armed, because letting a caller silence a callee's FAILURES is dangerous.

Warning mode is looked up **outward through the frame stack** to the nearest
frame with an explicit setting (default `print`). Because suppressing ADVICE
from above is what suppression means: when a library warns "you passed a literal
pattern where `regex()` was likely meant", the frame entitled to decide whether
that is noise is the caller's.

One sentence for the reference: *a failure is the callee's business; the noise
budget is the caller's.*

`ignore` and `stop` both obey nearest-setting-wins, including an inner frame
overriding an outer one. A project that wants warnings fatal puts `on warning
stop` in `main`; a single site that means it puts `on warning ignore` in its own
function.

### 2.5 Escalation

`on warning stop` converts the warning into a raise **at the warning's own
site**, so `error.line` and `error.trace` point at the offending statement
rather than wherever it was noticed. From that moment it IS an error and the
PLAT-ERR model governs it completely — including rules 1 and 2, which is correct
because the program asked for that conversion.

`error.severity` (`"error"` | `"warning"`) is the one new field on the error
object, so a caught escalation can say what it started as.

### 2.6 No `on warning goto <label>`

Deliberately absent. An error ABANDONS its statement, so jumping is coherent —
the statement failed, go elsewhere. A warning fires from a statement that
SUCCEEDED, so jumping means leaving successful code on an advisory signal: your
loop body warns once and control leaves the loop. Same reasoning that deleted
`resume`: a mode that cannot do its job cleanly is worse than no mode.

This is also why `if warning then` exists rather than being replaced by
`on warning stop` + `on error goto next`. That composition *works*, but a raise
abandons its statement — so escalating a warning fired during a SUCCESSFUL
assignment would discard an assignment that completed. `if warning then`
abandons nothing.

### 2.7 Raising one

`warning("message")` and `warning({ message: "...", extra: x })` — a BUILTIN
CALL, not a statement form, with the same shape rules as `error` (message
required; extras become `details`). Libraries can participate rather than
reaching for `print to error`.

**Why a call and not `warning <expr>` mirroring `error <expr>`:** measured, not
assumed. `IDENT expression` as a statement production yields **4 shift/reduce
conflicts** against assignment and bare-call statements. `error` gets away with
the statement form only because it is a hard token. Since the entire point of
this design is adding no reserved word, the raise takes the one shape that needs
no grammar change at all. It also reads unambiguously beside the value:
`warning` is the pending diagnostic, `warning(...)` raises one.

`warning(...)` returns `nothing`, so it is exempt from the unused-result
diagnostic by the same rule everything else is.

## 3. The wrinkle, stated plainly

Because `warning` RESOLVES instead of raising, a **typo'd variable** named
`warning` reads `false` rather than `undefined variable`. That is a small new
silent trap inside a change whose purpose is removing silent traps, and it
belongs in the reference rather than in a footnote. It is the same exposure
`error` already carries, and the shadowing rule bounds it: the moment you assign
to `warning`, it is yours.

## 4. Test obligations (tests/run_warning_model.sh)

Modes: `print` is the default and reaches stderr; `ignore` silences; `goto next`
records and `print`/`stop`/`ignore` do not; `stop` raises at the warning's own
line and carries `error.severity = "warning"`.

Reading: bare `warning` acknowledges (a second check is false); `warning.field`
does not; `w = warning` snapshots.

Non-interference: an unacknowledged warning does NOT re-raise at frame exit
(rule 2 must not leak); a warning does not disturb a pending ERROR, and an error
and a warning can be pending simultaneously and read independently.

Scoping: a caller's `ignore` suppresses a warning raised inside a callee
(dynamic lookup); an inner setting overrides an outer one.

Soft name: `warning = 1` shadows the diagnostic and reads back as 1;
`r.warning` and `{ warning: … }` still work; a program that never triggers one
reads `false` rather than raising.

Negative: `on warning goto some_label` is refused; `on wanring stop` is refused
BY NAME; `warning { no_message: 1 }` is refused.

## 5. Implementation map

- Grammar: four `ON IDENT …` productions + `warning <expr>` statement, all
  validated semantically. No lexer change.
- `ErrorFrame` gains `warn_mode` (unset/print/ignore/stop — unset is what makes
  the dynamic lookup work) and `warn_pending`; `current_warning` beside
  `current_error`.
- `runtime_warn(message, code, source, details)`: walk frames outward for the
  first explicit `warn_mode`; `ignore` returns; `print` does today's dedup'd
  `fprintf`; `goto next` records; `stop` sets severity `"warning"` and takes
  `runtime_error_raise`'s path.
- `env_get` not-found path resolves the soft name; `AST_EXPR_FIELD` special-cases
  an ident object named `warning` (only when no variable of that name resolves).
- `value_error_object` gains `severity`.
- The four existing `fprintf(stderr, "warning: …")` sites re-point at
  `runtime_warn`, gaining suppression and escalation retroactively.

## 6. First new diagnostic this makes possible

`unused-result`: a bare call statement discarding a non-`nothing` return from a
gBASIC-defined function. Builtins and natives are exempt (which is what keeps
`append`'s 1,101 sites quiet), and `return nothing` — the established void
convention — is exempt by value. Measured: 173 sites today. Shipped in the same
change, because a channel with nothing to carry proves nothing.


## 7. Candidates the channel now makes possible

Written 2026-08-23, from a survey of `src/eval.c` for paths that report without
a diagnostic. Each was confirmed by running it. **None of these were shipped
with the channel** — this is the backlog the channel exists to serve, in
descending order of how badly the current behaviour misleads.

Every Tier 1 entry has the same signature: an **unlocated** bare line on stderr,
a result of `nothing`, and **exit code 0**. A caller cannot tell the failure
from a legitimate `nothing`, and CI sees success.

| | what | today | belongs as |
|---|---|---|---|
| 1 | ~~`goto <unknown label>`~~ **SHIPPED 2026-08-23** | was: prints, then abandons the rest of the function; `nothing`, exit 0 | raises `invalid control flow`; `tests/run_silent_traps.sh` |
| 2 | ~~the `date` / `datetime` / `time` / `file` / `dir` modifiers (5 sites)~~ **SHIPPED 2026-08-23** | was: prints, assigns `nothing`, exit 0 | raises `datetime` / `modifier`, matching `USD` four lines away in the same dispatch function; `tests/run_silent_traps.sh` |
| 3 | ~~`a[99]` (read)~~ **SHIPPED 2026-08-23** | was: prints, yields `nothing`, exit 0 while assignment raised | raises `indexing`; `tests/run_silent_traps.sh` |
| 4 | ~~a raise inside a watcher body~~ **SHIPPED 2026-08-23** | was: the drain loop dropped `did_raise`, so draining carried on and the program ran with a watcher that had not fired; the diagnostic surfaced only at exit | propagates like any other raise; `tests/run_silent_traps.sh` |

Tier 2 — behavioural silence, no output at all:

| | what | today | belongs as |
|---|---|---|---|
| 5 | `contains(s, "b*")` | searches for the literal two characters | **DECLINED 2026-08-23, measured.** Across ~100k lines there are **zero** literal patterns containing unambiguous regex syntax (`\d`, `[…]`), so a precise rule would fire never. The 8 patterns carrying any other metacharacter are `"\n"`, `"\t"`, `"\""` and `"+"` — escape sequences and punctuation — so a rule loose enough to catch `"b*"` flags all of those instead. No evidence the trap occurs; the UNLEARN entry is the right treatment |
| 6 | blind shadow — assigning an outer name inside a function without reading it first | silent; the read-then-shadow warning fires only if the name was READ | **BUILT, MEASURED, REVERTED 2026-08-23.** It fired **287 times across 103 files**, overwhelmingly on single-letter locals (`r`, `i`, `s`, `a`) that merely share a name with something further out. The original code comment predicted exactly this. An opt-out does not rescue a diagnostic that is wrong far more often than right — the channel lowers the bar for shipping a warning, it does not remove it |
| 7 | `r["typo"]` compared to a value | `unknown`, which compares not-equal silently | **BUILT, MEASURED, REVERTED 2026-08-23.** Narrowed twice and still wrong. Firing on any computed `unknown` hit `file_type(p) = "folder"` and `env(V) = "1"`; narrowed to INDEX reads it was silent across gBASIC but fired twice in Studio, **correctly both times** — `shell.marked[doc] != rev` and `stated(cfg)[tier] = v`, where absent-therefore-not-equal is the intended reading. Nothing separates that from a typo without value provenance or a statement of intent |

The through-line: gBASIC's runtime has a fail-loudly culture, and these are the
places it quietly does not. Rows 1 and 3 were never warnings at all — they were
raises that had not been written, and they shipped as raises on 2026-08-23,
independent of this channel — as did row 2. **None cost a single test**: 333
negative cases and 232 examples passed unchanged each time, which says nothing
in either tree was relying on the old silence. Rows 4, 5, 6 and 7 remain open,
and they are the ones that genuinely need the channel rather than a raise:
each has a legitimate use that must not be nagged.


## 8. What the backlog taught

Four rows were worked in one pass and **one shipped**. That ratio is the useful
part, and it is not a failure of the exercise — three candidate warnings were
killed by evidence that would otherwise have reached users as noise.

**A channel lowers the bar for shipping a warning; it does not remove it.** Row
6 was implemented exactly as proposed, measured at 287 warnings across 103
files, and reverted — because a diagnostic that is wrong far more often than it
is right teaches people to reach for `on warning ignore` reflexively, which
destroys the value of every *other* warning. The suppression mechanism is not a
licence to guess.

**Measure before shipping, and measure the FALSE POSITIVES specifically.** Row 5
looked worth doing from the UNLEARN entry alone; measuring found zero instances
of the pattern it targets in ~100k lines. Row 7 was narrowed twice and still
flagged only correct code — and note where the evidence came from: it was
silent across all of gBASIC and only revealed itself against Studio, a real
application. A language's own test suite is not a representative sample of how
the language gets used.

**A "silent trap" and "a warning" are different claims.** Rows 1–4 were failures
with no defensible reading, and all four became raises. Rows 5–7 were shapes
that *look* like mistakes and usually are not: `absent key therefore not equal`
is how optional keys are handled, and a local sharing a global's name is
ordinary. Cataloguing something as a trap does not establish that the compiler
can tell.

**A NULL pointer found the same day.** Row 7's implementation dereferenced the
comparison's operand expressions, which are NULL on the datetime-lens path
where `eval_comparison` re-enters itself with a synthesized `AstExpr`. That
segfaulted 16 suites. The gate caught it in one run; a narrower check would
not have. The guard is documented in place so the next person adding a check
there knows the operands are not always real.

**A static estimate is not a measurement.** Row 6's static estimate said 3 sites;
the runtime said 287, because the estimate only saw same-file top-level globals.
The unused-result estimate said 173 and the runtime said 54, for the opposite
reason: name collisions across files inflated it. Both numbers were wrong in
different directions, and only running the code settled it.

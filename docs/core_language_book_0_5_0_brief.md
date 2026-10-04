# What 0.5.0 (and 0.5.1) changes for a core-language book

**Status: Record.** A working note, not an authority. Written 2026-10-03 for the session finishing *gBASIC: The
Core Language*, which was held for this release. Every example below was RUN
against the 0.5.0 binary and its output pasted, not written from memory — twice,
because the first attempt at §3 was vacuous and said so (see the note there).

This is a **working brief, not an authority**: `CHANGELOG.md`'s 0.5.0 section is
the curated list a reader sees, `docs/reference.md` is the language authority,
and this page exists only to say *where a draft is now wrong* and *what is new
enough to need writing*.

---

## 1. Re-capture first — a finished draft may already be wrong here

**Twelve diagnostics were reworded**, plus the whole impossible-date family. Any
passage quoting a message verbatim has to be re-captured against the 0.5.0
binary. `./tools/since-release.sh v0.4.0` reports the mechanical half; it is a
**floor, never a ceiling**, and this release is the clearest demonstration of
why — one `.err` moved while the same commit reworded the sentence **nine**
module dispatchers emit, and none of those nine is pinned by a golden.

- `invalid function call: NAME` → **`undefined function: NAME`**, for a name
  nothing defines. It says *no function, library function or builtin of that
  name is in scope at this call* — "in scope at this call" rather than "nothing
  defines that name", because in script mode a function declared *below* its
  call is not yet registered, and the blunter sentence would be false in exactly
  the place a beginner is most confused.
- **The same for nine modules**: `undefined function: odbc.vacuum -- the 'odbc'
  module does not define 'vacuum'`.
- **The actor frame limit names both numbers** (`send` and `spawn`).
- **Every refusal of an impossible date** — see §4.

**One result changed.** `chars(f)` counted BYTES and now counts codepoints; a
12-character file holding one accented letter reported 13 and now reports 12. A
passage that tabulated `chars` against `bytes` for non-ASCII input is now wrong.

**And four reference corrections, one of which actively taught bad practice.**
The page said `mid(s, i, 1)` is O(i) and therefore a per-character scan is
O(n²). PLAT-STRIDX removed both costs and the page was never updated, so a
reader who believed it **rewrites a linear loop into something worse**. If the
draft repeats that claim, it is the single most harmful sentence to carry
forward. Measured on 0.5.0: `mid` is flat in the index (ratio 1.09 across a 4×
string, where O(i) would be ~4) and a scan is linear. The quoted timings were
never in dispute; the *reason* was wrong, and accumulating with `+` inside the
scan is the real quadratic.

---

## 2. New core-language surface

All of it is core — no module, no optional dependency — so it falls inside this
volume rather than a later one.

### Date and time layouts, for writing AND reading

Until 0.5.0 there was **no output formatting at all**: `string(dt)` gave ISO and
everything else was assembled by hand from the dot fields.

```
YYYY-MM-DD hh:mm:ss  -> 2026-03-07 14:05:09
MM / mm              -> 03 / 05
D/M/YY               -> 7/3/26
DDD D MMM YYYY       -> Sat 7 Mar 2026
DDDD D MMMM YYYY     -> Saturday 7 March 2026
h:mm pm              -> 2:05 pm
hh:mm                -> 14:05
```

Three rules carry the whole notation, and the first is the one every other
scheme fumbles:

1. **Date parts are UPPERCASE, time parts lowercase.** `MM` is the month, `mm`
   is the minutes. That is the collision resolved.
2. **One or two letters is a number, three a short name, four a long name** —
   `MMM` is Mar, `MMMM` March, `DDD` Sat, `DDDD` Saturday.
3. **The 12-hour clock is implicit**: `hh` is 24-hour unless the layout carries
   `am` or `pm`, and the case you write is the case you get.

Reading uses the same notation, and takes a **list** when one shape is not
enough:

```
{date "DD/MM/YYYY"}       of "07/03/2026" -> 2026-03-07
{date "MM/DD/YYYY"}       of "07/03/2026" -> 2026-07-03
{date "DD/MM/YYYY", "MM/DD/YYYY"} of "03/15/2026" -> 2026-03-15
{date "DD/MM/YY"}         of "07/03/68"   -> 2068-03-07
{date "DD/MM/YY"}         of "07/03/69"   -> 1969-03-07
```

**The order is the declaration**, which is what makes first-match-wins honest
rather than a race: `03/07/2026` is 7 March or 3 July depending on where the
report came from, and writing `DD/MM/YYYY` first is the author saying which. **A
layout matches only if it also yields a real date**, which is why the list above
disambiguates itself — `03/15/2026` skips the `DD/MM` candidate rather than
inventing month 15. The two-digit year takes the POSIX pivot (`00`–`68` this
century), which the last two lines demonstrate.

### Chaining, inline position, and comparison lenses

```
{trimmed; upper}"  joe  "                  -> JOE
"  Joe " {trimmed; caseless}= "joe"        -> true
```

- **A clause chains with `;`**, applied left to right. The comma could not be
  used: it is already the argument separator (`{between "a", "b"}`) and arity
  cannot disambiguate it. A `;` inside an argument is content.
- **Every modifier shape now works inline** — an argument (`{split ","}line`), a
  multi-word name (`{dates.end of month}d`) and a chain (`{trimmed; upper}raw`).
  In 0.4.0 the first two were parse errors.
- **Any modifier is now a comparison lens**, alone or chained, which replaces
  `trim(lower(a)) = trim(lower(b))`. A lens that *answers* the comparison
  (`caseless`, or one declared `for compare`) must be the **last** stage.

### ISO 8601

`{datetime}"2026-03-07T14:05:09Z"` is accepted — the `T` separator and `Z`,
`+HH:MM`, `+HHMM`, `+HH` and their `-` forms. **An offset is honoured by
converting to UTC**, because a gBASIC datetime is *civil* and carries no zone
while the text denotes an *instant*. So `+02:00` moves the digits back two
hours. The alternative — keep the wall clock, drop the offset — would make
`14:05:09+02:00` and `14:05:09Z` the same value while they are two hours apart.

---

## 3. The one distinction the book must get right

Clause and inline are **not** two spellings of one thing, and the difference is
invisible if you demonstrate it with `string()`:

```
bb {number}= "10" + "5"        clause: value=105 type=number    bb + 1 = 106
hh = {number}"10" + "5"        inline: value=105 type=string    hh + 1 = 1051
```

**The clause takes the whole right-hand side; inline binds like unary minus.**
So the clause converts the *sum of two strings* and the inline form converts
`"10"` and then concatenates. A compound assignment (`n {number}+= "5"`) has no
inline spelling at all.

> **A note on method, because it bit me writing this page.** My first version of
> this demonstration printed `string(bb)` and `string(hh)` and reported **105
> for both** — a result that proves nothing, since `string(105)` and
> `string("105")` are the same characters. It is the same shape as the `finio`
> OFX defect, where amounts were text for the life of an adapter because every
> assertion went through `string()`. **Only `type()` or arithmetic separates
> them.** If the chapter shows this difference, show it with `+ 1`.

The division of labour to teach: **clause when the value is being stored, inline
when it is being used.**

---

## 4. Traps to name

### `mode` returns a wrong answer and is not fixed

```
mode([1, 2, 3])      -> 1     there is NO mode; this is element 1
mode([2, 2, 1, 1])   -> 2     a tie, resolved by SOURCE ORDER
median([4, 1, 2, 3]) -> 2.5   correct: the mean of the two middle values
```

**On continuous data — money, any real price list — every value is unique, so
`mode` always returns the first element and always looks like an answer.**
Filed as open ledger item **49**. It was raised specifically because this volume
pins 0.5.0, which would make the defect documented-and-wrong on paper, and
**Matthew ruled on 2026-10-03: leave the builtin alone and OMIT `mode` from this
volume entirely — no mention, no caveat.**

That is a deliberate silence, so the reasoning is worth having: a caveat in print
dates the book to a release we intend to supersede, and a page teaching a known
wrong answer is worse than a page one builtin short. **`median` is unaffected
and safe to teach as it stands** — measured, it already takes the statistical
convention on an even count.

If a later edition picks `mode` up, the design argument is in
`docs/bulk_data_design.md` §2b, including why resolving a tie to the value
*between* the modes is unsound: on `[10,10,10,100,100,100]` it names 55, which
occurs **zero** times, so the "most typical" sale would be reported as the
single rarest value present.

### An impossible date is now refused

`{date}"2026-02-30"` used to be **accepted**, and `+ 1 day` then answered
`2026-03-03`, because the epoch conversion normalises 30 February to 2 March. A
date that does not exist silently became a different real date two days later.
It now refuses and **names the calendar rule**:

```
`2026-02-30` is not a real date -- February 2026 has 28 days
`1900-02-29` is not a real date -- 1900 was not a leap year, so February 1900 has 28 days
`2026-13-01` is not a real date -- a month is 1 to 12, not 13
```

A string that is genuinely not a date still gets the old generic sentence, which
is the distinction to teach: the shape and the calendar are two different
failures.

### Prose does not go inside a layout

```
{string "Business hours: hh:mm"}ts
  -> `Business` is not a date layout token -- the tokens are YYYY YY,
     M MM MMM MMMM, D DD DDD DDDD, h hh, m mm, s ss and am/pm, and
     anything else goes outside the layout
```

That refusal is what makes the notation safe rather than merely short. If prose
passed through, `"Business hours: hh:mm"` would render the `ss` in *Business* as
seconds — measured, **4,536 of 104,334 English words contain `ss`, about one in
twenty-three**.

### A lens stage is not free, and must be pure

A comparison applies each stage to **both** operands, so a three-stage chain is
**six** invocations of your code where the assignment form is three. And **a
stage must be a pure function of its input**: one that answers differently each
call is handed the two operands separately, so **equal values compare unequal**
and nothing can detect it. That is the one rule a modifier author has to keep.

---

## 5. What is absent on purpose, and is not coming

Worth stating plainly in a core-language volume, because readers arriving from
other languages look for these and the reasons are structural rather than
backlog:

- **No closures.** A reference cycle has no owner in a refcounted runtime; and
  `spawn` is fork+exec, so a closure could not cross an actor boundary. It also
  protects `encode` totality, which is what lets a run be stored between HTTP
  requests.
- **No references.** A loop body therefore cannot write through the element
  variable — `for each item, i in list` plus `list[i] = item` is the idiom, and
  writing to `item` alone is warned about (`2107`).

---

## 6. The release pin, which is a promise and not a version number

`examples/gbasic_site/site.bas` holds **two** version strings and they mean
different things: `version:` is the current release, and **`book_version()` is
the release the paperback was written against**. The site's prose promises that
archive stays downloadable, and its checksum is printed on paper where it cannot
be corrected afterwards. `book_version()` currently names **0.4.0**.

So if this volume pins **0.5.0**, two things follow and neither is automatic:
`book_version()` moves, and **0.4.0 must be added to the `pinned:` list** in the
tedderland page so its archive is not swept. `RELEASING.md` now carries this,
including the trap: a blanket `sed s/0.4.0/0.5.0/g` over the tree passes all
three version gates and breaks a printed promise, because the docs gate reads
`version: "…"` and cannot see `book_version()`.

Run the book's examples against the **tarball's** binary rather than whatever is
on `PATH`, or the pin is decorative.

---

## Addendum: 0.5.1, and it came from this book

0.5.1 exists because of the ch02 revision. Rewriting the complaint that the
`^`/`%` refusals "name neither the character you typed nor the thing to type
instead" — true of 0.3.0, fixed in 0.5.0 — surfaced that **`<>` had no sentence
at all**. Sweeping the rest of what a reader arriving from QBasic types found
two more. `MOD`, `&` and `dim` already named a remedy and these three did not,
which is what made it an inconsistency rather than a policy.

**Pin 0.5.1, not 0.5.0**, and teach these:

```
print 1 <> 2    '<>' is not an operator; not-equal is != -- a != b
let x = 1       `let` is not a gBASIC statement; assign directly (x = 1)
rem a comment   `rem` is not a gBASIC comment; a comment starts with ' and runs to the end of the line
```

**The `let`/`rem` pair is the sharper story for a chapter, because the old
messages did not merely fail to help — they misdirected.** Both parse as the
beginning of a *call*, so the parser asked for a parenthesis:

```
let x = 1       syntax error, unexpected OP_EQ, expecting LPAREN      (0.5.0)
rem a comment   syntax error, unexpected IDENT, expecting LPAREN      (0.5.0)
```

A beginner typing the word every BASIC book opens with was told to add a
parenthesis, which is the one change that cannot help. If the chapter wants an
example of the difference between a terse diagnostic and a *wrong* one, this is
a better one than `^`.

**Three facts the chapter can rely on, each measured:**

- **Neither word is reserved**, and that is deliberate — `let` remains a legal
  name for your own function (`function let(a)` … `print let(2)` answers `3`).
  The rule is the one `sub` already set: the fix is a message, not a keyword,
  because reserving a word breaks every program using it as a name.
- **A name that merely begins with one is untouched**: `letter` and `remainder`
  are ordinary names.
- **`(` is the discriminator**, so a syntax error *inside* a call you really
  wrote keeps the parser's own sentence rather than being answered with advice
  about a statement you did not write.

**`UNLEARN.md` had all three right all along** — `<>` → `!=`, `rem` → `'`,
`dim x` → just assign — so the page was correct and only the binary was
unhelpful, which is the same pattern `run_qbasic_diagnostics.sh` records for
`mod`. If the draft cites UNLEARN on these, the citation was never wrong; what
changed is that the binary now agrees with it. The `<>` and `let`/`rem` entries
there are updated to quote the new sentences.

**And a note on the ledger, since this is the second time it mattered.** The
`on-repin.md` entry about `on warning stop` predicted the fix correctly and its
consequence wrongly — a local function still defeats `main`'s setting, and only
the *library* boundary changed. Re-measuring rather than applying the list is
what caught it, and that is the right default for every remaining entry: the
ledger says where to look, not what is true.

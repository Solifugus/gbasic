# Files and folders for ETL — design

**Status: Shipped.** §2 (the streaming read) and §3 (the recursive glob) both
**shipped 2026-10-15**, §4's `intake` on 2026-10-15/16, and §8's scratch space on
2026-10-16 — see the note at the end of each. **Two of the five
items this document called absent were already shipped under other names**:
`modified` is `file_mtime`, and `basename`/`dirname`/`extension` are
`file_name`/`directory_name`/`extension` — see the two corrections in §1. Of
path splitting only `stem` was missing, and it **shipped 2026-10-15** too.
`read_at` (the bounded binary reader), the `intake` library, **temp files** and
**`chmod`** all shipped after this document was written — see §8, §9 and
`docs/intake_design.md`. **Nothing in §1's list of five is outstanding**, and
measuring around the last of them found three defects rather than one absence
(§9).

**Provenance.** Where this says *measured* it means a command was run on
2026-10-14/15 and the number is quoted. The requirements come from Matthew's own
production ETL work, including a Python class whose `Central.archive(file_path,
archive_path, days_retention=180)` is the signature §4 is designed against.

**Why now.** The next book is about **ETL, data integration, migration and
processing**. The *libraries* for that are unusually strong already — `finio`
(financial adapters with provenance), `ari` (anchor-relative report parsing), the
xlsx `grid`→`consolidate`→`dbframe`→`to_sql` pipeline, `discovery` (estates and
column lineage), `frame`, `odbc`/`pg`/`sqlite`, `fake`/`fake.plant`, `notation`,
`persist`. The gap is not the ETL layer. It is the **file substrate underneath
it**, and the thinnest plank there is a wall rather than a nuisance.

---

## 1. The measurement that sets the priority

**All file reading is whole-file, and it costs about 4.5× the file size.**

| | |
|---|---|
| baseline interpreter | 13.8 MB resident |
| 200,000-line, 7.6 MB CSV through `read_lines` | 48.7 MB resident |
| per line (38-byte rows) | **179 bytes** |
| so a 1 GB extract needs | **~4.5 GB** |

`read(f)` is whole-file, `read_lines(f)` returns an array of *every* line, and
`lines(f)` is only the count. So a 10 GB extract is not slow, it is impossible —
and for a book about ETL that is the first-order problem. **Everything else in
this document is a convenience by comparison.**

Measured for contrast, the surface that *does* exist is broader than it feels:
`exists read write append bytes lines chars lock unlock list files folders
make_dir remove_dir delete copy move atomic_replace real_path file_type
join_path extension file_size` — 23 operations. `copy`, `move`, `remove_dir`,
`atomic_replace` and `lock` are all there, which covers more of the ETL cycle
than one would guess.

What is absent, in priority order for this book: **streaming**, a **recursive
glob**, ~~path splitting (`basename`/`dirname`/`stem`)~~ — **two thirds of which
already existed; see the second correction below** — temp files, and `chmod`.
`chmod` is cited three times in `DOGFOOD.md` — including that *"a credential
store cannot be created 0600 from gBASIC"*, which bent Studio's design — so it
is real, but it belongs to the credentials story rather than to this one.

### A correction: `modified` was never missing

The first draft of this list named `modified` (mtime) second, and said of it
that `gb_file_mtime` was already in the platform layer so *"the platform work is
done; a Windows body is needed"*. **Both halves were wrong.** `file_mtime(f)` is
a registered builtin, has been for months, returns a `datetime` in local time at
second precision, works on a directory as well as a file, reads through
`gb_file_mtime` precisely so a Windows TZ shift cannot move it, has a Windows
body already, and is documented in `docs/reference.md`. It was found by going to
look at the platform layer before writing the easy half of an increment.

The reason it reads as absent is the **name**: nothing in this document, and
nothing in the brief it came from, would lead anyone to `file_mtime` — it was
read as a sibling of `file_size`, which it is, rather than as the answer to
"when did this change", which it also is. That is a real cost and it is the one
§3 pays off rather than this correction: a *field on the listing record* is where
an incremental load actually wants it, because deciding whether to process a file
is a filter over thousands of them and a builtin call per candidate is a call per
candidate.

The lesson is the one this tree keeps relearning from the other direction. A
list of what is missing is a claim about the tree, and a claim about the tree
that nothing runs rots exactly like the performance claims `run_docs_gate.sh`
was built for.

### A second correction: two thirds of path splitting existed too

**SHIPPED 2026-10-15.** `basename` is `file_name(p)`, `dirname` is
`directory_name(p)`, and `extension(p)` reports the suffix — all three builtins,
all three documented in `docs/reference.md`, all three taking a plain string or
a file or directory reference. The list above was written from the *names* this
brief uses, which are POSIX's; the tree spells them in words. Only **`stem`**
was genuinely missing, and it is a builtin now
(`tests/run_path_split.sh`, 34 checks).

That is **two of five** items in one list that turned out already shipped, which
makes the lesson above a measurement rather than a remark: of the things this
document called absent, 40% were present under a different name. The remedy is
not better reading — both were found by *running* a probe — it is that a gap
list must be executable, the shape `run_limitations.sh` gives `DOGFOOD.md`'s
ledger, where every live entry is a negative control that goes red when the
limitation is fixed.

Three things came out of building `stem` that reading the design would not
have produced:

1. **`stem` and `extension` are two halves of one cut**, so what the suite
   asserts is an invariant rather than a list of values — with an extension,
   `stem(p) + "." + extension(p)` is `file_name(p)`; without one, `stem(p)` *is*
   `file_name(p)`.
2. **That invariant found a real boundary question.** `extension` answers `""`
   for two *different* reasons — no dot at all, and a dot with nothing after it
   — and only the first means "nothing to strip". Sharing one scan between the
   two was necessary and not sufficient: before the trailing-dot rule,
   `stem("a.")` answered `"a"` while `extension("a.")` answered `""`, and
   `stem("..")` answered `"."`. Both are perfectly ordinary filenames, which is
   why the tier is an invariant and the fixture is self-checking rather than a
   golden. `extension` is unchanged by the rule, having already answered `""` in
   both cases, so no existing golden moved.
3. **The interior-NUL refusal had been written for two of six names in the same
   function.** `real_path` and `file_type` refused a path containing a NUL, with
   the argument stated three lines above the branches that did not: a path
   crosses into the system as a NUL-terminated C string, so one with a NUL
   inside would be *checked* as one path and *opened* as another. That argument
   is about what a path is, not about which call is holding one. Measured before
   widening it, with `p = "/a/b" + chr(0) + "x/c.txt"`: `file_name(p)` answered
   `"b"` — the basename of `/a/b` — `directory_name(p)` answered `"/a"`, and
   `join_path("/a" + chr(0) + "b", "c")` answered `"/a/c"`. Three plausible
   paths, none of them about the path the program held. All seven refuse now,
   both arguments of `join_path` included, and PLAT-NUL's rule picked *refuse*
   over *repair* by asking the far side whether it takes a byte count, which
   `open` does not.

`stdlib/edgar.bas` hand-rolled `directory_name` as a 17-line segment walk and is
the citation under §7's path-splitting entry; it is one call now. Measured over
nine cache-path shapes the walk and the builtin agree on eight, differing only
on a trailing slash, and all 252 example goldens stayed byte-identical.

## 2. Streaming: `for each line in f`

```basic
for each line in f
    load(line)
end for
```

**Why this spelling rather than an open/read/close handle.** The manual form
needs six lines, a visible handle and a sentinel:

```basic
h = open(f)                       ' NOT proposed
while true
    line = read_line(h)
    if line = nothing then break
    ...
end while
close(h)
```

Three things are wrong with it, and all three are failure modes rather than
ugliness:

1. **The handle leaks** on an early `break`, a `return`, or a raise. The loop
   form gives the runtime the handle and it closes on every exit — the unwinding
   guarantee `with lock` already provides, where *"the pop happens before the
   result propagates so a return, a goto and a raise all leave it"*.
2. **The sentinel is a known trap in this tree.** `input()` already *"returns
   `""` for both a blank line and EOF with no way to tell them apart"*. A loop
   with no sentinel cannot have that bug.
3. It is six lines for the commonest thing an ETL job does.

**And it cannot change the meaning of any existing program** — measured: `for
each` is array-only today, and a file, a string and a record all raise the same
`for in expects an array`. One site, one diagnostic, so no working program
contains the shape. That is the same proof the array-projection work rested on,
and it is stronger than surveying the tree.

**Lines as the default is a choice, not an inference.** It follows the grain of
the existing file API, where `read`, `write`, `chars` and `lines` are all text
and `bytes` is the one that has to say so. Binary says so too:
`for each chunk in chunks(f, 65536)`.

**`break` naming its loop makes the ETL shape read correctly**, and it is
already in the language:

```basic
for each f in files({dir}"/data", "**/vendor_*.xlsx")
    for each line in {file}f.path
        if malformed(line) then
            report(f.path)
            break f                    ' abandon this input, take the next
        end if
        take(line)
    end for
end for
```

**The `{file}` is load-bearing and this example did not have it until the two
halves were run together.** §3's listing yields a *record*, so the inner loop
needs a file reference built from its `path` — written `for each line in f`, the
inner loop raises `for in expects an array or a file`, correctly, about a
record. Found by running the shape rather than by reading it.

**An explicit handle is still wanted** for seeking, for interleaving two files
and for fixed-width records. `with` is the right shape, since it already
guarantees unwinding — but `with open(f) as h` needs an `as` binding that
`with lock(...)` and `with principal(...)` do not have, so that is new grammar
and must be priced before it is promised.

### SHIPPED 2026-10-15

`for each line in f` streams. **Measured on the same 161 MB, 4-million-line CSV:
13.3 MB resident streaming against 750 MB for `read_lines`** — 56x less, and
flat across a 21x range of input where the whole-file route is proportional.

**It needed no grammar change at all**, which this section had not predicted:
`for each line in f` already PARSED and raised at ONE runtime site, so the whole
feature is a runtime change and §6's bison-conflict measurement was answered
before it was taken. `getline` was not used — measured, MinGW reports an implicit
declaration and `src/eval.c` is compiled for Windows too — so the reader is a
portable buffered one, which the NUL handling wanted anyway.

**TWO DEFECTS CAME OUT OF READING THE CODE BEFORE WRITING IN IT**, both
pre-existing and neither found by a test:

- **`break` out of a named `for each` leaked the iterable** — 112 bytes,
  definitely lost, once per occurrence. Named break *is* covered, but every case
  used a COUNTED loop, which holds no iterable and cannot exhibit it. Fixed
  first, because the streaming loop would have leaked an open FILE handle on that
  same path, and a leaked descriptor is a limit a long ETL run reaches.
- **`read_lines` truncated a line at an interior NUL** — measured, `a\0b` came
  back as one byte while `read(f)` on the same file returned all nine. Each line
  was built as a C string. Fixed, and the fix is smaller than what it replaced.
  This is PLAT-NUL's standing lesson holding again: a truncation found in one
  place is evidence about every place that reads a string.

`tests/run_file_stream.sh` has four tiers and none is a golden. The load-bearing
one is **AGREEMENT**: streaming and `read_lines` are two independent
implementations of one line rule, so their agreeing over twelve shapes — an
interior NUL, a CRLF file, a line longer than the read buffer, a file not ending
in a newline — is evidence where a golden of either alone is a transcript. The
**MEMORY** tier is a ratio across a 10x step, never an absolute, **with
`read_lines` as the control**: it must EXCEED the gate, or "streaming is flat" is
being asserted by a harness that cannot tell flat from proportional. Four
perturbations proven red, each caught by its own tier.

**Still open from this section**: an explicit handle for seeking and interleaving
(`with open(f) as h` needs an `as` binding `with` does not have), and
`for each chunk in chunks(f, n)` for binary input.

## 3. Finding files: extend `files`, do not add a name

**`find` is already a builtin** (searching an array or a string), measured, so
the glob cannot have that name. It does not need one: `list`, `files` and
`folders` already return `{name, path, type}` **records**, measured —

```
{"name":"finio_iso20022.bas","path":"stdlib/finio_iso20022.bas","type":"file"}
```

— so the glob is an **optional second argument** to the function that already
answers this question, and it returns the same record:

```basic
files(d)                                   ' unchanged: one level, every file
files(d, "*.xlsx")                         ' one level, matching
files(d, "**/*.xlsx")                      ' any depth
files(d, "2026/*/vendor_*.xlsx")           ' a year, every quarter under it
files(d, "**/vendor_*.xlsx")               ' and when the folder shape changes
```

`**` is what makes this answer the real requirement, which is *"folder
structures that can and will change"*: a pattern anchored at the top and at the
filename, indifferent to what is in between.

**The record gains `size` and `modified`.** Adding fields cannot break a reader,
so this is an extension rather than a new API — and it matters for more than
tidiness: deciding whether to process a file is a filter over thousands of them,
and a `stat` per candidate from gBASIC would be a call per file.
`gb_file_mtime` already exists in `include/platform.h`, `src/platform_posix.c`
**and `src/platform_win32.c`**, so there is no platform work at all — the
"a Windows body is needed" in the first draft of this paragraph was wrong, and
`run_platform.sh`'s derived tripwire (2026-10-14) would have failed if it were
true.

Glob is implemented over `regex`, which already exists. The pattern *language*
is glob because that is what people know and what reads well in a config file.

### SHIPPED 2026-10-15

`list`, `files` and `folders` take an optional glob pattern, the record carries
`size` and `modified`, and `tests/run_glob.sh` has ten tiers.

**`regex` WAS THE WRONG SUBSTRATE AND THAT WAS DECIDED BY TRYING TO USE IT.**
The sentence above is struck: the matcher is ~120 lines written here instead,
for three reasons none of which is preference. `**` has no regular-expression
spelling that survives translation, because it is a rule about **path
segments** and not about characters — the naive `.*` crosses separators where
`*` must not. A glob-to-ERE translator has to escape every metacharacter of the
*target* language, so a filename containing `.` or `+` becomes a wildcard in
silence, which is the "plausible wrong answer" class this whole tree is
organised against. And libc's POSIX ERE is one more thing whose behaviour
differs across the platforms `src/eval.c` is compiled for. `fnmatch` was
rejected for the same portability reason that ruled out `getline` in §2 — absent
on MinGW.

**THE LOAD-BEARING TIER IS bash.** The claim "the pattern language is glob
because that is what people already know" is only worth making if it is true, so
twelve patterns are expanded over one tree by gBASIC and by `bash` with
`shopt -s globstar`, and the two must agree exactly. bash is an independent
implementation written by somebody else and it is the implementation the claim
is *about*, where a golden of our own output would be a transcript.

**Three decisions the design did not contain, each with a consequence:**

- **A leading `.` must be matched explicitly**, as in every shell. This is not
  tidiness here: §4's ledger lives at `/data/.intake.json`, so without the rule
  `files(d, "**/*.json")` would feed the ledger to itself, and `**` would walk
  into `.git`.
- **An unreadable subdirectory WARNS (`2113`) and the walk continues**, where an
  unreadable *top-level* directory still raises. If the directory you named
  cannot be read you got none of what you asked for; but one locked folder three
  levels down must not throw away a scan of ten thousand files, which in ETL is
  a job that fails nightly for a reason nobody can see. This is the first row of
  §5's table reached, and the warning channel is what makes it affordable —
  `on warning ignore` for a walk that expects it, `on warning stop` for a job
  that needs completeness.
- **The walk does not follow symlinks to directories**, because `ln -s .. loop`
  makes a recursive walk run forever and a **hang reports nothing at all**. The
  cost is stated rather than discovered and is asserted as a tier: a
  `/data/current -> /data/2026-Q2` arrangement is reached by naming the link as
  the directory, or through `real_path`, and not by globbing through it.

**ONE THING THIS TOOK AWAY, and it is stated rather than discovered.** An entry
used to be three strings and went through `encode` like any record; `modified`
is a `datetime`, and `encode` deliberately refuses typed values because a lossy
token yields text `decode` cannot read back. It is kept a datetime anyway, for
the same reason `file_mtime` is one — two routes to a question that disagree
about the TYPE are exactly the defect the shared-helper refactor above removed,
and `if f.modified > last_run` is the whole of an incremental-load predicate.
The two routes out are asserted beside the refusal: `notation` keeps every type
and round-trips a whole entry, and `epoch(f.modified)` is the number for a
caller that wants `encode`. §4's ledger stores its own records rather than raw
entries, so this does not reach it.

**The result is sorted by path.** readdir order is the directory hash's, so it
is neither alphabetical nor stable across machines; every consumer had to sort
(`stdlib/filetree.bas` does it by hand) and a depth-first walk interleaves in an
order nobody can predict, while ETL routinely wants its inputs in order.

**TWO DEFECTS OF MINE, both found by running rather than by reading, and both
the same class as the ones §2 records.** Perturbing the sort away left **every
check green**: the order tier asked "is the result sorted" over a nine-entry
directory whose readdir order happened to be alphabetical. It now establishes
its own premise twice — structurally, with a directory `b` beside a file
`b-x.txt` whose sorted order and depth-first order differ in *both* readdir
orders, and over sixty files whose readdir order is asserted to be scrambled
before the check is believed. And the arity check hit `pipefail` for the fifth
recorded time in this tree: `./gbasic ... | grep -q` takes gbasic's status, and
the fixture is *meant* to exit nonzero, so it reported "a third argument was
accepted" against a binary that had just refused it — in a branch whose helper
two lines above already captured into a variable.

**Still open from this section**: a relative path on the entry record (filed in
`DOGFOOD.md`), which §4's archive step will want and which can be decided with
that evidence rather than guessed at here.

## 4. `intake`: what is new, what was taken, what to keep

Items 3 and 4 of the brief are **not file operations** — *"determine if i
already imported them and if not, import them"* and *"after processing a file, I
need to archive it or what I derived from it with purge criteria"* both need
**state**. So they are a library over the primitives, not more primitives.

```basic
load intake

ledger = intake.open({file}"/data/.intake.json")

for each f in intake.arrived(ledger, {dir}"/data", "**/*.xlsx", identity: "content")
    rows = load_workbook(f)
    intake.taken(ledger, f, { rows: count(rows) })
    intake.archive(f, to: {dir}"/data/archive", keep: 180 days)
end for

intake.purge(ledger, {dir}"/data/archive")
```

**`keep: 180 days` is native syntax, measured** — a duration literal works as a
record option value, `type()` answers `duration`, and `now() - keep` yields a
datetime. So the retention is unit-safe in a way `days_retention=180` is not:
there is no unit to misread and no arithmetic to get wrong.

**`identity:` IS REQUIRED, with no default**, and that is the most important
decision here. It declares what makes two files the same file:

| | |
|---|---|
| `"path"` | the path alone — for a source that never reuses a name |
| `"stamp"` | path + size + mtime — cheap, and right for a 10 GB file you cannot afford to hash |
| `"content"` | sha256 — for a vendor who re-drops the same filename with new contents |

Each is correct for a different source and **wrong for the others**: guessing
re-imports silently or skips silently, and in ETL both look exactly like success.
So it follows the rule `ldap`'s `security` and `lending`'s accrual basis already
follow — the argument with consequences is the one that may not be defaulted.
`crypto` already has `sha256`, and `persist` is already crash-safe, so the ledger
is cheap to build and hard to corrupt.

## 5. Robustness is the test plan, not a section

Matthew's word was **"and robust"**. In ETL that has specific, testable
meanings, and this tree already has a precedent for nearly every one — so these
are the tiers, written before the code:

| failure | required behaviour | precedent |
|---|---|---|
| a file is still being **written** | an incomplete upload is not imported: require stability (size+mtime unchanged across an interval) or a declared sentinel file | — |
| the run **crashes** mid-import | the ledger survives and says what completed | `persist`: atomic temp+rename, reporting missing/corrupt/loaded as a **value** |
| the same file is **re-dropped** | idempotency under the declared identity | §4 |
| a file **vanishes mid-scan** | the walk continues and reports it; it does not abort | `run_library_depth`: *"a file that does not parse does not define the library being looked for"* |
| a folder is **unreadable** | that is not an empty folder | fixed 2026-10-13 in `run_dir_builtins` |
| a stream is **truncated** mid-read | raise; a short read must never look like end-of-file | the same distinction as the unreadable folder |
| **archive** fails halfway | the original is never lost before the copy is durable | `atomic_replace` |
| **purge** runs | nothing is deleted that the ledger does not record as archived | the site's `pinned:` rule — *"a published release a book is based on is never removed"* |
| two loads start **at once** | one waits or refuses | `lock`/`unlock` already exist |

The first row is the only one with no precedent, and it is the one a real feed
hits weekly. It needs a decision rather than a default: **stability window**, or
**sentinel file**, or **both, declared**.

## 6. What must be measured before any code

1. **The cost of `for each` over a stream**, against the 4.5× figure in §1. The
   claim is constant memory; the tier is `run_xml_bigfile`'s shape — sample
   `VmHWM` from outside while walking a file far larger than RAM, and fail if the
   peak scales with the file.
2. **Whether `for each` can take a non-array without a bison conflict.** Zero is
   the standard, and this project rejected `IDENT expression` as a statement form
   over 4 measured conflicts.
3. **Glob over a real tree**, including the awkward cases: a symlink loop, a
   directory that disappears during the walk, and a pattern matching nothing
   (which must be an empty list, never an error — an empty input run is normal in
   ETL).
4. **The identity rules against a re-drop**, which is the only way to catch an
   idempotency bug: the same filename, new contents, and `"path"` must skip while
   `"content"` must take it. Asserted as a **difference**, since either alone
   passes on a ledger that always answers one way.

## 7. Open questions

- ~~**Partial-upload detection**: stability window, sentinel file, or both.~~
  **MEASURED AND SETTLED 2026-10-15** — see *The partial-upload row, measured*
  below. A stability window is not a check but a race, and it loses at ordinary
  speeds; the readiness rule will be **declared**, with no default that guesses.
- **`with open(f) as h`** needs an `as` binding `with` does not have. Price it.
- **Library name.** `intake` describes arrival and not archival or purge. `files`
  is taken; `ingest`, `pipeline` and `etl` are candidates.
- **Does `purge` delete, or report what it would delete?** Deleting data is the
  one operation here that cannot be undone, and the `--dry-run` habit of
  `deploy.sh` suggests the answer is both, with the reporting form the default.
- ~~**Path splitting** (`basename`, `dirname`, `stem`)~~ — **SETTLED
  2026-10-15.** `file_name`, `directory_name` and `extension` already existed;
  `stem` is a builtin now. See §1's second correction. The three hand-rolled
  `split(p, "/")` sites this entry cited were re-read, and only *one* was a
  path split: `edgar._parent_dir` was `directory_name` written out, and is one
  call now. The other two are not — `web._segments` and
  `edgar._url_to_fixture` need the segment *array* (`contains(parts,
  "submissions")` asks whether a word appears anywhere in a URL), which
  basename and dirname cannot express and which `split` already does. So the
  measurement that motivated the entry was two thirds wrong in the same
  direction as the entry itself.

### The partial-upload row, measured

This was the one robustness row with no precedent in the tree, and it was left
as a choice between a *stability window* (the file has not changed for N
seconds, so it must be finished) and a *sentinel file* (the writer says when it
is done). Three measurements were taken against a writer appending 207 bytes
every 250 ms for eight rows, with a reader sampling `file_size` and `file_mtime`
every 200 ms.

**1. A stability window is not a check, it is a race — and it loses at
ordinary speeds.** The size was *unchanged between consecutive samples three
times while the file was still being written*:

```
3 size=621   4 size=828   5 size=828   6 size=1035   ...
8 size=1449  9 size=1449  10 size=1656 11 size=1656  (final)
```

Samples 4→5, 8→9 and 10→11 are indistinguishable from the end of the upload. A
reader applying "unchanged since the last look ⇒ take it" would have taken a
**40%-complete file** and reported a successful load — the ordinary-looking
wrong answer this document's §5 exists to prevent, and in ETL it is a short load
that nothing raises about.

**2. `mtime` cannot break the tie, because the clock stops at seconds.**
Samples 4, 5, 6 and 7 all carry mtime `1792111513`. gBASIC's datetime is
second-resolution by design — a business decision recorded elsewhere and not one
to reopen here — so an mtime-based window cannot be shorter than about two
seconds *and still mean anything*, which is slower than the writer it is trying
to outlast.

**3. The one sound mechanism exists and cannot be used as a probe.** An advisory
lock really is visible across processes: with a second process provably holding
`lock(f)` (gate-file synchronised, not slept at), the reader's `lock(f)`
**blocked for 2.91 seconds** and acquired it only when the holder released. That
is correct `flock` behaviour and it is exactly what a readiness check wants to
ask — *is someone writing this?* — except that **there is no way to ask without
waiting**: `lock` has no non-blocking form, so a scan over a thousand files
stalls on the first one still being written. Filed in `DOGFOOD.md`.

*A correction worth keeping, because it was nearly a false defect report:* the
first run of that experiment concluded a writer's lock was **not** visible, on
the strength of the reader printing `lock acquired`. The probe's own message was
a lie it had no way to detect, having never looked at the clock. The
discriminator was elapsed time, and the second run — same code, one `date` on
each side — reversed the finding. Same shape as the `ldap` TLS episode: a
symptom reproduced is not a cause found.

**So the ruling.** `intake` will take a **declared** readiness rule, with no
default that guesses:

- `ready: "sentinel"` — the writer publishes `<name>.done`; the ledger takes a
  file only when its sentinel is there, and the sentinel is removed with it.
  Sound, and the writer's half is one `write`.
- `ready: "renamed"` — the writer builds the file elsewhere and renames it into
  the intake directory. Sound, and `atomic_replace` is already the writer's half
  of it; nothing is needed from this library but to document that appearing *is*
  the signal.
- `ready: { stable_for: 30 seconds }` — the fallback for a writer that will not
  cooperate, which is a real situation (an SFTP drop from a counterparty), and
  it is offered **with the race named in the reference** rather than as a
  default. A window has to be longer than the writer's longest pause, which the
  reader cannot know, so what the number buys is a smaller probability and never
  a guarantee.

A fourth is deliberately **not** offered: *detecting an open writer without any
cooperation.* On Linux that means reading `/proc/*/fd`, which is not portable,
needs the writer to be the same user, and would be the only thing in this
library that cannot work on Windows at all. The honest position is that the
sound answers are cooperative and the uncooperative one is a probability.

---

## 8. Scratch space: temp files and removing a tree — SHIPPED 2026-10-16

§1's list named **temp files** as the fourth absent item. Building them made the
list one item short, because a temp *directory* is only useful if something can
remove it, and `remove_dir` removes an empty one — so nothing in the language
could remove a tree it had just built. `remove_tree(d)`, `temp_file([options])`
and `temp_dir([options])` shipped together for that reason.

**The measurement that shaped the API.** The brief for a temp file is the
safe-write pattern — write a temp, swap it in — and written the obvious way
*that pattern does not work*: `atomic_replace` is same-filesystem only and says
so by name, and the system temporary directory is routinely on another
filesystem. Measured here, `/tmp` is tmpfs while the repository is on ext4, so

```
atomic_replace requires source and destination on the same filesystem:
/tmp/gbasic-jWApZv -> .../crossdev.txt (Invalid cross-device link)
```

A temp file in the system directory therefore cannot be swapped onto a
destination beside the program's own data. Hence `in:`, which is the option the
pattern is actually built on, and hence an **options record** rather than a
positional prefix: `temp_file({ in: here, prefix: "stage-" })` names both, and
an unknown field is refused by name.

**It also subsumes what `persist.bas` does by hand.** `persist` writes
`path + ".tmp"` beside its destination — correct about the filesystem, and a
collision as soon as two writers touch one file, since they share one temp
name. `temp_file({ in: directory_name(path) })` is the same place with a unique
name. `persist` is **not** migrated here: its goldens would not move, so the
change would be unverifiable from the outside, and it belongs with whatever
next touches that library.

**What the decisions were.**

- **It is created, not named.** A function that answers an unused name hands
  back a race. POSIX gets that from `mkstemp`, which also makes the file 0600 —
  so a program can hold a secret in one *before* `chmod` exists in this
  language, which is the `DOGFOOD` entry §1 cites about a credential store.
  Windows uses `GetTempPath` plus `gb_secure_random` and `CREATE_NEW`, so the
  creation is still the test; **the divergence is the mode, not the race**, and
  it is documented beside `chmod`'s rather than papered over.
- **Nothing is removed automatically.** In the safe-write pattern the temp file
  is *meant* to outlive the call that made it — it becomes the destination — so
  an interpreter that swept temp files at exit would break the main use.
- **A symlink inside a tree is unlinked, never descended**, which is the
  load-bearing property of `remove_tree` and the one its own fixture cannot
  assert, gBASIC being unable to create a symlink. A link given as the *top*
  path is refused instead of silently removing just the link, because a function
  called `remove_tree` answering "done" having removed no tree is worse than a
  refusal.
- **Depth is bounded at 256**, the glob's bound, and the refusal names the
  **top** of the tree: the first version named the path 256 levels down, which
  is 512 bytes of separators that did not fit in the message, so it reported a
  truncated path with its reason cut off the end.

**What is still absent from §1's list:** `chmod`, which remains the credentials
story rather than this one, and which the Windows divergence above now has a
shape for.
# Increment 3 — metadata: what a file says about itself

## The remaining gap, measured

`chmod` is the one item of §1's five still outstanding, and measuring around it
found four more things in the same place, three of them defects rather than
absences.

### 1. `file_type` FOLLOWS a symlink, and the LISTING disagrees with it

Measured 2026-10-16:

| path | `file_type(p)` | `list(d)`'s `e.type` |
|---|---|---|
| link to a file | `file` | `file` |
| link to a directory | `folder` | `folder` |
| **dangling link** | **`unknown`** | **`file`** |

`file_type` asks `stat` and answers `unknown` when it fails; the listing asks
`stat` and falls back to `"file"` for anything that is not a directory, so a
dangling link, a FIFO, a socket and a device all arrive labelled `file`. Two
routes to one question disagreeing is the shape this tree keeps finding
(`contains`/`ends_with` on a NUL, `sort`/`=` on an exact integer), and here the
listing's answer is the wrong one: there is no file there. A program writing
`if e.type = "file" then read(e.path)` raises on a dangling link and **blocks
forever** on a FIFO.

The glob shares that code, so `files(d, "**/*")` reports them as files too.

**Fix:** the listing answers what `file_type` answers — `file`, `folder`,
`other`, and `unknown` when `stat` fails. The entry is still *reported* (the
`intake` suite's vanished-file tier depends on a dangling link appearing in the
listing while `exists` denies it); only its label becomes honest. Measured
in-tree: `filetree.bas` is the only consumer and compares against `"folder"`.

### 2. There is no way to ask whether something IS a link

`gb_is_symlink` has been in the platform layer since the glob needed a cycle
guard, and nothing exposes it. `remove_tree` has just made the distinction
user-visible — *"is a symlink, not a tree"* — so a program that wants to check
before calling has no way to ask.

### 3. Nothing can create one

Three suites build symlinks with `ln -s` in bash, and `tests/run_scratch.sh`
says in as many words that its load-bearing tier lives in the runner because
*"gBASIC cannot create a symlink, so the one behaviour separating a correct
recursive delete from a destructive one cannot be written in the language it is
a property of"*.

### 4. `file_mtime` reads and nothing writes

An archive step that preserves the source timestamp is ordinary — the Python
class §1 is designed against copies metadata — and `copy` does not preserve it.

## What ships

- `permissions(p)` -> `{ mode, readable, writable, executable }`
- `chmod(f, "0640")`
- `set_mtime(f, when)`
- `is_symlink(p)`
- `symlink(target, path)`
- the listing's type, corrected

### `permissions` reports TWO different things, named so they cannot be confused

`mode` is **what the file says**: the permission bits as TEXT, `"0640"`.
`readable` / `writable` / `executable` are **what this process can do**, from
`access(2)`.

They are not the same question and conflating them is the classic bug: root can
read a file whose mode is `0000`, and a file on a read-only mount is `0644` and
not writable. A program asking "can I read this?" wants the second; one
auditing a credential store wants the first.

**The mode is TEXT and that is forced**: gBASIC has no octal literal. Measured —
`0o600` is a parse error and `0600` is the number six hundred — so a numeric
mode could not be written correctly even if it were offered.

### `chmod` refuses a number BY NAME

`chmod(f, 0600)` is the mistake every author will make once, and six hundred is
a perfectly good `mode_t`: it would set bits nobody asked for and succeed. So a
number is refused with the arithmetic in the message rather than silently
applied.

### Windows: the divergence is named, and no ACL is attempted

Under the ruling that differences be minimal and named:

| call | Windows |
|---|---|
| `is_symlink` | identical (`FILE_ATTRIBUTE_REPARSE_POINT`) |
| `set_mtime` | identical (`SetFileTime`) |
| `symlink` | `CreateSymbolicLinkW`; without the privilege it refuses **naming it** (Developer Mode or an elevated process), because "permission denied" sends an author to the filesystem |
| `permissions` | `mode` from `_stat`, which synthesises `0666`/`0444` from the read-only attribute; `executable` is **`unknown`**, Windows deciding by extension rather than by a bit |
| `chmod` | honours the owner **write** bit via `FILE_ATTRIBUTE_READONLY` and **warns** about every bit it cannot express |

A warning rather than a refusal, because the ruling is to minimise divergence
rather than to refuse the operation — and a program hardening a file on Windows
should still get the one bit Windows has.

**No ACL.** `src/platform_win32.c` is syntax-checked against real `windows.h`
and executed by nothing in this gate, so an ACL implementation would be a claim
with no evidence behind it — and ACL code that is subtly wrong on permissions is
worse than an honest warning.

## How it is tested

`tests/run_metadata.sh`, and the shape follows from what can be observed:

- **the fixture** states the values: mode text round-tripping through
  `chmod`/`permissions`, `set_mtime` read back by `file_mtime`, `is_symlink`
  both ways, a symlink that `read` follows, refusals (a numeric mode, a bad mode
  string, a missing file) each beside the call that must still work.
- **the runner** holds the two halves a fixture cannot: `stat -c %a` as an
  ORACLE for the mode (so the claim is not `permissions` agreeing with
  `chmod` — two calls into one implementation), and the `access(2)`-versus-mode
  difference, which needs a file whose mode says one thing and whose
  accessibility says another. As root both collapse, so that tier **skips with
  its reason**.
- **the correction** is a difference: `file_type` and the listing must agree
  about a dangling link, with the CONTROL that an ordinary file is still `file`
  and a directory still `folder` — or "they agree" is satisfied by a listing
  that labels everything `unknown`.

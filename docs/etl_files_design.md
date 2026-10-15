# Files and folders for ETL — design

**Status: Proposal. None of this exists.** No streaming read, no glob, no
`modified`, no `intake` library. Do not code against it.

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

What is absent, in priority order for this book: **streaming**, `modified`
(mtime), a **recursive glob**, path splitting (`basename`/`dirname`/`stem`),
temp files, and `chmod`. `chmod` is cited three times in `DOGFOOD.md` — including
that *"a credential store cannot be created 0600 from gBASIC"*, which bent
Studio's design — so it is real, but it belongs to the credentials story rather
than to this one.

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
for each file in files({dir}"/data", "**/vendor_*.xlsx")
    for each line in file
        if malformed(line) then
            report(file.path)
            break file                 ' abandon this input, take the next
        end if
        load(line)
    end for
end for
```

**An explicit handle is still wanted** for seeking, for interleaving two files
and for fixed-width records. `with` is the right shape, since it already
guarantees unwinding — but `with open(f) as h` needs an `as` binding that
`with lock(...)` and `with principal(...)` do not have, so that is new grammar
and must be priced before it is promised.

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
`gb_file_mtime` already exists in `include/platform.h` and
`src/platform_posix.c`, so the platform work is done; a Windows body is needed,
and `run_platform.sh`'s derived tripwire (2026-10-14) now fails if one is
missing.

Glob is implemented over `regex`, which already exists. The pattern *language*
is glob because that is what people know and what reads well in a config file.

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

- **Partial-upload detection**: stability window, sentinel file, or both. The
  one robustness row with no precedent here.
- **`with open(f) as h`** needs an `as` binding `with` does not have. Price it.
- **Library name.** `intake` describes arrival and not archival or purge. `files`
  is taken; `ingest`, `pipeline` and `etl` are candidates.
- **Does `purge` delete, or report what it would delete?** Deleting data is the
  one operation here that cannot be undone, and the `--dry-run` habit of
  `deploy.sh` suggests the answer is both, with the reporting form the default.
- **Path splitting** (`basename`, `dirname`, `stem`) is listed in §1 and not
  designed here. Measured: the stdlib already hand-rolls it with
  `split(p, "/")` in `web.bas`, `edgar.bas` and `insiders.bas`.

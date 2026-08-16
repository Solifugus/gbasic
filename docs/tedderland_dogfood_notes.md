# tedderland.com dogfood notes

Friction, gaps, and language/library improvement ideas discovered while building
**tedderland.com** (a separate project at `~/development/tedderland`) in gBASIC.

This file is **append-only**; add dated entries. The tedderland build sessions
record here; the gBASIC sessions triage. Each entry: what we were doing, what was
awkward, and a concrete suggestion. See the sibling
`docs/gbasic_dogfood_notes.md` for friction found building `examples/gbasic_site`.

---

## 2026-06-27 — Initial static-site generator (`build.bas`)

Built a self-contained static-site generator: parses `key: value` front-matter +
HTML-body content files, fills `{{TOKEN}}` HTML templates, walks a content
directory, and writes per-subdomain `dist/<sub>/index.html`. Everything below was
hit while getting that ~230-line program to run.

### High-impact

1. **`list_files` returns `file` VALUES, not strings — and only files, not
   subdirectories.** This was the single biggest stumble. A path that comes back
   from `list_files` is a `file` value (confirmed via `type(x)` → `"file"`), so
   feeding it straight back into the `(file)=` modifier fails with *"file modifier
   expects a path string"*. Helpers that read a path therefore have to branch on
   `type(path) = "file"`. Two asks:
   - Make `(file)=` (and `read`/`write`/`copy`) accept **either** a string path
     **or** an existing `file` value, uniformly. The round-trip
     `list_files` → `read` should "just work".
   - Provide a way to list **subdirectories** (or a `list_dirs`, or have
     `list_files` optionally include them). Needed for directory-existence checks
     (see #2).

2. **No file/dir existence test, and `make_dir` errors if the dir already
   exists.** With no `path_exists()`/`is_dir()` builtin and `list_files` not
   returning subdirectories, there's no clean way to check before creating. Had
   to make `make_dir` idempotent by wrapping it:
   ```
   function ensure_dir(path)
       on error resume next
       make_dir(path)
       on error stop
   end function
   ```
   Asks: a `path_exists(p)` builtin, **or** a `make_dir` that no-ops on an
   existing directory (or a `make_dir(p, true)` "parents/idempotent" flag, like
   `mkdir -p`). `make_dir` also appears to be single-level (no parent creation),
   which compounds this.

3. **`find(s, sub)` returns `nothing` when the substring is absent** (not `-1`,
   not `false`). So every substring test is `is_number(find(...))` /
   `is_nothing(find(...))`. Easy to get subtly wrong (a naive `find(...) >= 0`
   would compare `nothing >= 0`). Consider either returning `-1`, or — better —
   add a dedicated boolean (see #4).

4. **`contains` is array-only; there is no string-substring predicate.**
   `contains(str, sub)` errors with *"contains expects an array"*. Building HTML
   means lots of "does this string contain X" checks, all of which currently go
   through the `find` + `is_number` dance. Ask: let `contains` accept a string
   haystack (substring test), or add `includes(str, sub)` returning a boolean.

### Medium

5. **No string interpolation — `+` concatenation only.** Generating HTML is a
   wall of `"<a class=\"card\" href=\"https://" + sub + "." + domain + "/\">..."`.
   With escaped quotes inside HTML attributes this gets hard to read and
   error-prone. This is already on the dogfood list from `gbasic_site`; building a
   whole templating layer here reinforces it strongly. An f-string / `"${x}"`
   interpolation (or a `format()` builtin) would massively cut the noise.

6. **`copy` has asymmetric argument types.** The **source** must be a `file`
   value/reference (a string source errors: *"copy expects a file reference and
   file reference or string target"*), but the **target** may be a string. Since
   `list_files` yields file values this happens to work for copying listed files,
   but it's surprising. Ask: accept a string source too (symmetry with the
   target), or document the asymmetry prominently.

7. **`library` / `export` semantics aren't obvious enough to adopt confidently.**
   Wanted to split helpers into `lib/template.bas` + `lib/content.bas` and
   `load ... from "..."`. The stdlib uses `library NAME` + `export modifier ...`,
   but it's unclear how plain `function`s are exported/namespaced (flat? prefixed?
   underscore = private?). Kept everything inlined in one `build.bas` rather than
   risk it. Ask: a short "writing a library" doc with a plain-function export
   example, and clarity on the call convention (flat vs `lib.fn`).

### Minor / papercuts

8. **Missing `end program` gives a far-away parse error.** Omitting `end program`
   reports *"syntax error, unexpected end of file"* at the last line, not at the
   `program` that was left open. A "started here, never closed" hint would save a
   beat. (Same likely applies to unclosed `function`/`for`/`while`.)

9. **Confirmed-good, for the record (so we don't re-test):** `replace` replaces
   **all** occurrences; `mid/left/right/split` are 0-indexed and `mid` clamps an
   over-long length; `find` is a 0-based index on hit; record dynamic-key add via
   `rec[k] = v` on a `{}` literal works; `not (a = b)` for inequality;
   `on error resume next` / `on error stop` work as classic BASIC; `number("7")`
   converts; `file_name`/`extension`/`directory_name` accept `file` values and
   `extension` strips the dot.

## 2026-06-27 — no recursive directory removal / no file-type test
While building the project-sync tool for tedderland I wanted `build.bas` to wipe
`dist/` before regenerating, so a removed project's stale page disappears on
rebuild. This isn't expressible in pure gBASIC today:
- `remove_dir(path)` maps to C `rmdir()`, which only removes an *empty*
  directory — there's no recursive delete to clear an output tree.
- There's no `is_dir`/`is_file`/`file_exists` builtin (CLAUDE.md already notes
  the missing file-exists), so I can't even walk `list_files()` and recurse to
  delete a tree manually — I can't tell a file from a subdirectory.
- No shell-exec builtin to fall back on `rm -rf` either.
Result: I had to do the `dist/` clean in a shell wrapper (`sync.sh`) instead of
in `build.bas`, so a standalone `gbasic build.bas` run still leaves orphaned
output for deleted projects.
Suggestion: add a recursive `remove_tree(path)` (or make `remove_dir` recursive
with an opt-in flag), and/or an `is_dir(path)`/`file_exists(path)` predicate so
output-tree cleanup is writable in gBASIC.

## 2026-08-16 — writing gBASIC *samples* is where the surprises land
While rewriting the gBASIC project page I wrote five short samples and ran every
one before publishing it. Four of the five failed on the first attempt, and none
of the failures was a language defect — all four were me writing the obvious
thing rather than the real thing. That is worth recording precisely because the
page is aimed at newcomers, who will guess the same way:

- **`watch(a, b)` requires the watched variables to already exist.** Writing the
  watcher first (the natural order, since it reads like a declaration) gives
  `undefined variable: subtotal` and then prints `nothing` twice — so it fails
  *late and quietly* rather than at the watch statement.
- **Money is a modifier, not a literal.** `price = $19.99` is a lexer error;
  it is `price(USD)= 19.95`. Nothing in the error text points at the modifier
  form.
- **The string modifiers are `uppered`/`lowered`/`trimmed`, not
  `upper`/`lower`/`trim`.** The builtins are `upper`/`lower`; the modifiers take
  the past participle. `name (upper)= "x"` gives `assign modifier not found:
  upper`, which is a good message but the near-miss is easy to hit.
- **There is no `today()`.** `now()` exists, so `today()` is the natural guess
  for a date-only value; it fails as `invalid function call: today`.
- **`spawn f` needs the call form** — `spawn worker` is a parse error
  (`expecting LPAREN`); it must be `spawn worker(args...)` even when the child
  takes only the parent handle.

None of these needs a language change and most are documented somewhere. The
observation is about *shape*: every one is a case where the obvious guess is
close enough to look right and fails on a detail. That is exactly the population
`docs/ai/UNLEARN.md` exists for, and four of these five are not in it.
Suggestion: a short "first twenty minutes" section in the tutorial covering the
modifier-vs-builtin naming split (`upper` vs `uppered`), the money/date modifier
forms, and the ordering requirement for `watch`.

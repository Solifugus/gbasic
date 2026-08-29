# COOKBOOK — one blessed gBASIC idiom per task

Each entry names the idiom, states the one thing to get right, and points at a
**wired, suite-verified** file you can read and run. The cookbook references code;
it never contains it (an executable-docs gate, `tests/run_docs_gate.sh`, asserts
every referenced file exists and is wired into a suite). Read the referenced file
for the exact, working form. For what will surprise you, read `UNLEARN.md` first;
for error handling, `ERRORS.md`.

## Language constructs

- **Branch** — `if/else` blocks; one inline statement may follow `then`/`else`.
  → `examples/if_else_test.bas`, `examples/inline_if_test.bas`
- **Chain conditions** — `else if`, closed by a SINGLE `end if` however many
  rungs. Rungs may be inline or blocks; a trailing `else` is optional. Prefer
  `consider` when every branch tests the same subject — it names the subject
  once. → `examples/else_if_test.bas`
- **Dispatch on a value** — `consider` evaluates the subject once and runs the
  first matching `if <value> then` branch; `break` exits the consider, not an
  enclosing loop. → `examples/consider_test.bas`
- **Loop** — counted `for i = a to b [step c]` (inclusive `to`, bounds read
  once, counter keeps its last value), `while`, and post-test
  `do … until c` (a STOP condition; there is no continue-condition form,
  because it is `until not c`) — all with `break`/`continue`. No `repeat … until`
  (`repeat` is a builtin). → `examples/for_range_test.bas`,
  `examples/do_loop_test.bas`, `examples/while_break_continue_test.bas`
- **Iterate a collection** — `for each x in coll … end for` (or `next x`).
  Preferred for
  readability; indexing in a `while` loop is linear too, and has been since
  arrays became copy-on-write (cost pinned by `tests/run_arridx.sh`).
  → `examples/for_each_test.bas`
- **Array value semantics** — assignment, argument passing and mutation are
  independent at any depth, over a shared refcounted store; indexing and
  `append` are linear, which `tests/run_arridx.sh` fails if it stops being true.
  → `tests/arridx_test.bas`
- **Display any value** — `print v` and `string(v)` use one renderer and always
  agree, so a record shows its fields (`{"a":1}`), an array its elements
  (`["a","b"]`) and a duration its units (`2 days 3 hours`). Display is total —
  it never raises, even for a date or function nested in a record — whereas
  `encode`/`json_encode` refuse those, because a lossy token would not survive
  `decode`. → `tests/render_parity_test.bas`
- **Compare or search compound values** — `=` and `!=` on records and arrays are
  deep and structural, and record field order is irrelevant. `contains`, `find`,
  `remove_value` and `consider` all use the same comparison, so dispatching on a
  record's shape works; `find` answers `nothing` on a miss. Ordering operators
  (`<`, `>`) refuse compound operands rather than guessing.
  → `tests/equality_test.bas`, `tests/equality_dispatch_test.bas`
- **Show a number** — `print` renders the shortest decimal that reads back as
  the same value, so nothing is truncated and `number(string(x)) = x` always
  holds. That makes floating-point error visible (`0.1 + 0.2` shows
  `0.30000000000000004`): round for display with `round(x, 2)`, or use a money
  value (`t{USD}= 265550.75`) for figures that must read exactly. Integers below
  2^53 print plainly; there is no exponent *literal*, so build extremes with
  `number("1e20")`. → `tests/numfmt_test.bas`
- **Functions** — definition, params, `return`. → `examples/function_test.gb`
- **Function values** — a bare function name is a value you can store, pass, and
  call; equality is same-reference. → `examples/first_class_function_test.bas`
- **Methods** — a function in a record field, called through the record, with the
  receiver bound to `this` at the call site. → `examples/method_test.bas`
- **Construction** — a `constructor` field is auto-invoked by `new … with { … }`;
  read inputs from `this`. → `examples/constructor_test.bas`
- **Prototypal objects (PBI)** — `new <prototype>` derives an instance; policies
  govern inheritance/copy-on-write. → `examples/pbi_derive_test.bas`
- **Records** — literals (identifier keys only), nested access, bracket keys for
  non-identifier fields. → `examples/record_test.gb`
- **Arrays** — build and mutate: `append`/`prepend` extend in place,
  `insert`/`remove` shift. Cost story is the value-semantics entry above.
  → `examples/array_append_prepend_test.bas`
- **Modifiers** — assignment/comparison modifier clauses. A string literal in a
  clause means what it means anywhere else: `p{split "\n"} = s` splits on a
  newline. → `examples/modifier_test.gb`, `examples/modifier_escape_test.bas`
- **Watchers** — `watch(...)` runs once at registration, then synchronously on
  storage-changing mutations. → `examples/watch_test.gb`
- **Named watchers** — `watch name(a, b)` binds `name` to a first-class handle:
  `unwatch name` turns it off, re-declaring the same name REPLACES (setup is
  safe to re-run), `watchers()` lists live handles, `.name`/`.targets`
  identify one. Top-level only. → `examples/watch_named_test.bas`
- **Persisting application state** — `load persist`; `write_atomic` swaps a
  temp file in with a single rename so a crash never truncates it, and
  `read_status` returns `missing`/`corrupt`/`loaded` as a value instead of
  raising. → `examples/persist_test.bas`
- **Walking a directory as a tree** — `load filetree`; `scan(path, expanded)`
  builds a value tree (caller controls which directories expand), `flatten`
  gives display rows with depth. → `examples/filetree_test.bas`
- **File locks** — `with lock(f) … end with` unlocks on exit and on error.
  → `examples/lock_test.gb`
- **Program arguments** — declare `program main(args)`; `args` is the 0-based
  string array after the script path. → `examples/args_test.bas`
- **Libraries** — `library`/`load` (and `load NAME from "file"`).
  → `examples/library_test.bas`
- **Unicode-aware strings** — codepoint iteration and byte access.
  → `examples/unicode_chars_test.bas`
- **Date/time, duration, money** — the specialized value types and their
  comparisons. → `examples/datetime_test.gb`, `examples/duration_test.gb`,
  `examples/money_test.bas`
- **A library that depends on another library** — declare it INSIDE the
  `library` block (`load frame from "frame.bas"`), never at file top level. The
  top-level form works only while the CALLER also loads the dependency, so the
  library passes its own tests and breaks for the first program that loads it
  alone. → `stdlib/stats.bas` over `matrix.bas`
- **Serialize/deserialize** — `encode`/`decode` round-trips values through text,
  including `nothing`, `unknown` and the non-finite numbers, which is what makes
  it a gBASIC DIALECT and not JSON.
  → `examples/serialization_test.bas`, `examples/encode_roundtrip_test.bas`
- **Opening a workbook that might be bad** — `xlsx.try_open(path)` returns
  `{ok, workbook, message}` and never raises on a bad FILE, so one malformed
  workbook does not end a batch. `xlsx.open` still raises; both give the same
  verdict for the same file. → `examples/xlsx_try_open_test.bas`
- **Walking a directory tree** — `list_files` reports FILES ONLY and does not
  recurse, so it cannot drive a walk: a subdirectory is invisible to it. Use
  `list(d)` on a `{dir}` reference, which answers `{name, type}` for every entry
  including folders, and keep your own worklist. → `stdlib/filetree.bas`
- **JSON for anything leaving gBASIC** — `json_encode(value)`, strict RFC 8259;
  `json_encodable(value)` asks the same question without raising. Reach for this
  and NOT `encode` for an HTTP body, a file another program reads, or a queue
  message: `encode` writes `nothing`/`unknown`/`inf`/`nan` as bare words that no
  other parser accepts, and the receiving end is where you find out.
  → `examples/json_strict_test.bas`
- **Reading JSON you did not write** — `try_decode(text)` returns
  `{ok, value, message, offset, line, column}` and never raises, so there is no
  pre-validate pass. Use it rather than hand-writing a JSON scanner: the C parser
  is far faster than an interpreted per-character loop, and it reports *where* the
  input went wrong. (The scanner it replaced took 92 s on a 116 KB store, most of
  it a quadratic per-character walk that PLAT-STRIDX has since fixed — the walk is
  linear now (`tests/run_stridx.sh` guards that), but a gBASIC-level scanner is
  still much slower than the builtin.) → `tests/try_decode_test.bas`
- **Walking text character by character** — `while i < len(s)` with
  `mid(s, i, 1)` is linear, in either direction, for ASCII and multibyte alike —
  asserted by `tests/run_stridx.sh`. Build strings with an array plus `join`, not
  repeated `+`. → `tests/stridx_test.bas`
- **Timing an operation** — `monotonic()` returns fractional seconds from an
  arbitrary origin; subtract two readings. Never `epoch()`, which is whole
  seconds and can step backwards when the wall clock is corrected.
  → `examples/monotonic_test.bas`
- **Bitwise** — `band`/`bor`/`bxor`/`bnot`/`shl`/`shr`/`rotl`/`rotr` on 32-bit
  unsigned integers. → `examples/bitwise_test.bas`
- **Match a pattern** — `match(s, p)` for the first match, `match_all(s, p)` for
  every one; a miss is `unknown`, so test with `is_unknown` — or take a
  fallback with `default(v, fallback)`, which covers `unknown` and `nothing`
  alike. The record is
  `{text, start, length, groups}` with `start`/`length` in codepoints, so
  `mid(s, m.start, m.length)` composes. `match` scans rather than anchoring —
  it is Python's `re.search`, not `re.match`. → `tests/regex_test.bas`
- **Search, replace or split by pattern** — pass `regex(p)` where the verb takes
  a literal: `contains(s, regex(p))`, `replace(s, regex(p), "$1")`,
  `split(s, regex(p))`. A plain string argument stays literal, so omitting
  `regex(...)` silently searches for the pattern text itself. `find` is not
  overloaded. → `tests/regex_test.bas`
- **Reuse a pattern** — `regex(p)` compiles once and is an immutable value; hoist
  it out of a loop rather than passing the string each time. Flags belong to
  `regex(p, "ims")`: `i` ignore case, `m` `^`/`$` per line, `s` dot matches
  newline. → `tests/regex_test.bas`
- **Tolerate bad input** — arm the frame with `on error goto next` and check
  with `if error then`; a function may catch and return a fallback, so callers
  never see it. Only bare `error` acknowledges (`error.message` reads without
  claiming), and an unacknowledged error escapes the frame rather than being
  shadowed by the next one. See `ERRORS.md`. →
  `examples/on_error_goto_next_test.bas`
- **Modulo, array join, record compose** — `mod(a, b)` (FLOORED: the sign
  follows the divisor, unlike QBasic), `concat(a, b, …)` for arrays and
  `merge(a, b, …)` for records, later winning on a duplicate key. All three are
  builtins; `%`, array `+` and record `+` are separate decisions. →
  `examples/mod_concat_merge_test.bas`
- **Make advice enforceable** — `on warning stop` in `main` turns every warning
  into a raise (the `-Werror` of a language with no build step); `on warning
  ignore` in one function says "I meant that" without silencing anyone else.
  Mode lookup runs OUTWARD, so a caller's setting governs its callees. →
  `tests/warning_model/dynamic_scope.bas`
- **Never call an update API for effect** — a gBASIC function cannot change its
  caller, so `pool_tick(p)` discards the new pool and does nothing. Assign it.
  Since 0.1.0-rc5 this warns (`unused-result`). →
  `tests/warning_model/unused_result.bas`

## Dates, durations, calendars

- **UTC** — `now("UTC")`. NOT `to_zone(now(), "UTC")`, which is a no-op that
  returns local time unchanged: `to_zone` reads its input as already-UTC.
  → `examples/datetime_zone_test.bas`
- **An instant from a datetime** — `epoch(dt, zone)`. Bare `epoch(dt)` and
  `number(dt)` read the value as LOCAL, so they are wrong for anything
  converted; `epoch()` with no argument is the current instant.
  → `examples/datetime_zone_test.bas`
- **Exit status** — `exit(code)`, 0-255, unwinds out of everything. Wider
  values are refused, not truncated. → `tests/run_core.sh`

- **Month arithmetic clamps** — `jan31 + 1 month` is Feb 28 (accountant's
  rule); the round trip does not hold at month-end. → `examples/datetime_arithmetic_test.bas`
- **Extract with dot fields, truncate with lenses** — `d.year` is a number,
  `{month}= d` is a coarser datetime; a field finer than the value's precision
  is `unknown`, a misspelled field raises. ISO weekday, Monday=1. →
  `examples/datetime_fields_test.bas`
- **Durations: exact vs calendar, never blurred** — ordering month-bearing
  durations raises; `1 month = 30 days` is false; algebra is signed and
  canonical. → `examples/datetime_arithmetic_test.bas`
- **Business calendars are data** — `dates.calendar(spec)`, pass `cal`
  explicitly; `dates.merge` unions constraints for mutual availability. →
  `examples/dates_calendar_test.bas`
- **Date expressions** — one spec record, three verbs
  (`matches`/`select`/`series`); miss → `unknown`, malformed → raise. NOTE the
  keyword-after-dot renames: the sub-rule is `when:` not `on:`, bounds are
  `{from:, through:}` not `to:`. → `examples/dates_select_test.bas`
- **Scheduling** — `schedule.slots` / `schedule.layout`; unplaceable sessions
  reported by name, never dropped. → `examples/schedule_test.bas`
- Full tutorial: `docs/datetime_cookbook.md`, enforced over
  `examples/datetime_cookbook/01_dates_and_durations.bas` and its siblings by
  `tests/run_datetime_cookbook.sh`.

## Concurrency

- **Actors** — `spawn`/`send`/`receive`/`self`; shared-nothing, message-copying.
  Pair `receive()` with `consider` for dispatch. → `examples/actor_loopback_test.bas`

## Shipping

- **Turning a program into an installable package** — `packaging/build-deb.sh
  <app-dir>` builds a .deb carrying its OWN interpreter and stdlib under
  `/usr/lib/<app>/`, built lean (optional modules are compile-time gated: a
  server build drops GTK, GObject-introspection, PostgreSQL, libxml2 and
  libcurl — 48 shared libraries down to 10). System libssl/libcrypto/sqlite3
  are used, NOT vendored. Full guide: `docs/shipping_applications.md`.
  → `packaging/example-app/app/notesd.bas`
- **A shipped app must load stdlib by ABSOLUTE PATH** — bare `load NAME`
  searches the source file's own directory tree RECURSIVELY and FIRST, ahead of
  GBASIC_PATH and the compiled-in stdlib, so any `NAME.bas` beneath the app
  silently replaces the shipped library. Write `load stats from
  "@RUNTIME@/stdlib/stats.bas"`; the packager substitutes the token and fails
  the build if one survives. Native modules (`sqlite`, `pg`, `odbc`, `webclient`,
  `webserver`, `xml`, `gui`, `gi`) are compiled in and exempt.
  → `docs/shipping_applications.md`
- **Bind the result of `serve`** — `h = serve(app)`. A bare call discards a
  non-`nothing` return, so unused-result warns on every service start; and
  `h.port` is how you learn the port after binding `port: 0`.
  → `packaging/example-app/app/notesd.bas`

## Modules

- **Process (run)** — `process.run`; unconditional builtin, no `load`. Synchronous,
  argv passed literally (no shell). → `tests/native_platform/nap6_streams.bas`
- **Process (supervise)** — `process.start` returns a handle;
  `poll`/`read`/`wait`/`stop`/`release` control a LIVE child. `read` never blocks
  and never frames lines — concatenate the reads. `stop(h)` is SIGTERM only;
  escalation to SIGKILL requires `stop(h, {force_after: N})`. Dropping the last
  handle copy reaps the child, so `release` is optional.
  → `tests/native_platform/plat_proc_basic.bas`
- **A child cannot outlive the interpreter** — every child arms a kernel
  parent-death signal, so it gets SIGTERM the moment the parent dies, including
  a `kill -9` no cleanup code survives. There is no opt-out: `process.start` is
  not a way to launch a daemon, and a process that must survive belongs to a
  service manager. → `tests/run_process_lifetime.sh`
- **Supervising a child from a GUI loop** — compose the two: `process.start`,
  then a `gi.timeout` callback that polls and reads each tick. No actor and no
  mailbox are needed, because `start`/`poll`/`read` never block, so a GUI loop
  can drive a live child without a worker. → `tests/native_platform/plat_proc_basic.bas`
  (the non-blocking primitives), `tests/native_platform/loop_timeout.bas` (the
  GTK timeout that drives them)
- **Seeing a child's output while it runs** — a gBASIC child's stdout is BLOCK-buffered
  on a pipe, so short output only appears when it exits, and output still buffered when
  the child is killed is lost outright. Start the child with `--line-buffered` as its
  FIRST argument and every completed `print` arrives immediately. Opt-in only; nothing
  implies it. → `tests/native_platform/plat_stream_stream.bas`
- **Writing to standard error** — `print to error <expression>`. Everything that is
  not the program's data (progress, warnings, usage) goes there, so a caller can
  pipe the program somewhere and receive the data alone. Renders exactly what
  `print` renders, and is prompt with or without `--line-buffered` (which governs
  stdout only). `error` is the only destination keyword — there is no file-handle
  or redirect form. → `tests/native_platform/plat_stderr_streams.bas`,
  `tests/native_platform/plat_stderr_parity.bas`
- **SQLite** — `load sqlite`; parameterized query/exec. → `examples/sqlite_module_test.bas`
- **PostgreSQL** — `load pg`; opt-in suite. → `tests/postgres_integration.bas`
- **ODBC** — `load odbc`; one connection string reaches SQL Server, MySQL,
  Oracle, DB2 and the rest. `BIGINT`/`DECIMAL`/`NUMERIC` come back as STRINGS
  (a double loses the cents), and money binds as exact decimal text.
  → `tests/odbc_test.bas`, `docs/odbc_cookbook.md`
- **WebClient** — `load webclient`; synchronous HTTP against a loopback fixture.
  → `tests/webclient_integration.bas`
- **WebServer** — `load webserver`; single request/connection.
  → `tests/webserver_integration.bas`
- **XML** — `load xml`; parse and navigate by path; streaming reader for large
  files. Note `load` is an EXECUTABLE statement, so with a `program` block it
  goes INSIDE the block — a top-level `load` never runs and the symptom is
  `library not loaded: xml`. → `examples/xml_parse_test.bas`
- **Spreadsheets (`xlsx`)** — no `load`; needs zlib + libxml2. Read, EDIT and
  save an existing workbook (unmodelled parts survive byte-for-byte), evaluate
  formulas, and check our engine against Excel's own cached values with
  `xlsx.check` before trusting anything computed. Rows are 1-based, columns
  0-based; cells are SPARSE, so loop the cells you were given rather than
  rows × columns. Full tutorial in `docs/xlsx_cookbook.md`, whose code and
  output blocks are checked against these files by `tests/run_xlsx_cookbook.sh`.
  → `examples/xlsx_cookbook/01_open_and_look.bas`,
  `examples/xlsx_cookbook/06_check_the_oracle.bas`
- **Spreadsheet → table** — `grid.extract` a sheet into a frame,
  `consolidate.merge` several differently-shaped sheets onto one schema,
  `dbframe.to_table` into SQLite. Column types are inferred from EVERY value,
  not the first; a source missing a required column is rejected by name rather
  than filled with blanks. → `examples/xlsx_cookbook/09_messy_sheet.bas`,
  `examples/xlsx_cookbook/11_frame_to_sqlite.bas`
- **Statistics (`load stats`)** — descriptive and inferential statistics,
  regression and the GLM suite, time series, finance metrics, survival
  (Kaplan-Meier / log-rank / Cox), meta-analysis, factor analysis, event
  studies and causal inference. Every function returns a RECORD, or `ok: false`
  with a `message`, or `unknown` on degenerate input — check before reading a
  field. Task-first guides: `docs/cookbook_social_behavioral.md` and
  `docs/cookbook_econometrics_finance.md`.
  → `examples/cookbook_social_test.bas`, `examples/cookbook_econ_test.bas`
- **Causal inference is where the trap is** — `did` and `iv_2sls` can be RIGHT
  IN THE COEFFICIENT and wrong in the standard error, and the wrong one is the
  flattering one. Never hand-roll 2SLS as two `ols` calls: same estimate,
  residuals measured against the fitted x instead of the real one, error off by
  up to 2.7x in the direction that manufactures significance. On panel data
  pass `cluster:` — conventional DiD errors are understated. `pre_trends` does
  NOT test parallel trends (nothing can); it tests whether the groups moved
  together beforehand, and its `note` says so. → `tests/causal_test.bas`
- **Market data (`load market`)** — daily price history as a frame:
  `market.daily(m, symbol, from, to)` → `{ok, frame, adjusted, message}`.
  Failure is a VALUE, not a raise. Rows are always ascending by date, because a
  reversed series yields NEGATED returns that look like ordinary data; and
  `adjusted` reports what the provider supplies rather than being assumed,
  because unadjusted prices across a split read as a -50% day. Use
  `market.offline(m, dir)` in tests — never the network. → `tests/market_test.bas`
- **Cryptography (builtins)** — password hashing and random tokens.
  → `examples/password_hash_test.bas`, `examples/secure_token_test.bas`
- **Cryptography (`load crypto`)** — signed cookies, CSRF, JWT/HS256, flat JSON.
  → `examples/crypto_compose_test.bas`
- **Desktop UI, where to start** — `gtk` constructors + `gi.connect`, which is
  what gBASIC Studio is built from (86 `gtk.` calls, 72 `gi.`, zero `gtkui`).
  `gtkui` is for a CHANGING KEYED LIST, not for the frame around it; `gi.new`
  is the escape hatch for the ~90% of GTK `gtk.bas` does not wrap. Tutorial:
  `docs/gui_tutorial.md`; recipes: `docs/gui_cookbook.md`.
  → `examples/gui_cookbook/01_first_window.bas`
- **STATE IN A GUI CALLBACK MUST LIVE IN A RECORD** — gBASIC has no closures,
  so a handler assigning to an outer scalar silently makes a function-local and
  the outer value never changes: a counter stuck at 1, a loop that never quits.
  Mutate a field of a shared record instead. This is the single most common way
  gBASIC GUI code fails, and it fails silently.
  → `examples/gui_cookbook/02_state_in_callbacks.bas`
- **Testing GUI code** — `gtk.init()` needs a display but SHOWING does not, so
  a suite builds real widgets and interrogates them with nothing on screen.
  Run under `G_DEBUG=fatal-criticals`. No signal can be emitted from gBASIC, so
  drive behaviour with `gi.timeout` or verify by hand.
  → `examples/gui_cookbook/08_testing_gui_code.bas`
- **GObject-Introspection (`gi`, GTK 4)** — the canonical `GtkApplication` idiom:
  construct-time properties via `gi.new`, the app drives its own `run` loop, the
  window is built in the `activate` handler. Manual (needs a display); guarded by
  the parse-only smoke. → `examples/gi/gtk4_hello.bas`

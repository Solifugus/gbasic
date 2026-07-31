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
- **Dispatch on a value** — `consider` evaluates the subject once and runs the
  first matching `if <value> then` branch; `break` exits the consider, not an
  enclosing loop. → `examples/consider_test.bas`
- **Loop** — `while` with `break`/`continue`. There is no numeric `for`.
  → `examples/while_break_continue_test.bas`
- **Iterate a collection** — `for each x in coll … end for`. Preferred for
  readability; indexing in a `while` loop is linear too, and has been since
  arrays became copy-on-write (cost pinned by `tests/run_arridx.sh`).
  → `examples/for_each_test.bas`
- **Array value semantics** — assignment, argument passing and mutation are
  independent at any depth, over a shared refcounted store; indexing and
  `append` are linear, which `tests/run_arridx.sh` fails if it stops being true.
  → `tests/arridx_test.bas`
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
  clause means what it means anywhere else: `p(split "\n") = s` splits on a
  newline. → `examples/modifier_test.gb`, `examples/modifier_escape_test.bas`
- **Watchers** — `watch(...)` runs once at registration, then synchronously on
  storage-changing mutations. → `examples/watch_test.gb`
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
- **Serialize/deserialize** — `encode`/`decode` round-trips values through text.
  → `examples/serialization_test.bas`
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
- **Tolerate bad input** — do **not** catch with `on error resume next`;
  pre-validate and call the raising builtin only when it will succeed. See
  `ERRORS.md`. → `examples/on_error_resume_next_test.bas`

## Concurrency

- **Actors** — `spawn`/`send`/`receive`/`self`; shared-nothing, message-copying.
  Pair `receive()` with `consider` for dispatch. → `examples/actor_loopback_test.bas`

## Modules

- **Process (run)** — `process.run`; unconditional builtin, no `load`. Synchronous,
  argv passed literally (no shell). → `tests/native_platform/nap6_streams.bas`
- **Process (supervise)** — `process.start` returns a handle;
  `poll`/`read`/`wait`/`stop`/`release` control a LIVE child. `read` never blocks
  and never frames lines — concatenate the reads. `stop(h)` is SIGTERM only;
  escalation to SIGKILL requires `stop(h, {force_after: N})`. Dropping the last
  handle copy reaps the child, so `release` is optional.
  → `tests/native_platform/plat_proc_basic.bas`
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
- **WebClient** — `load webclient`; synchronous HTTP against a loopback fixture.
  → `tests/webclient_integration.bas`
- **WebServer** — `load webserver`; single request/connection.
  → `tests/webserver_integration.bas`
- **XML** — `load xml`; parse and navigate by path; streaming reader for large
  files. → `examples/xml_parse_test.bas`
- **Cryptography (builtins)** — password hashing and random tokens.
  → `examples/password_hash_test.bas`, `examples/secure_token_test.bas`
- **Cryptography (`load crypto`)** — signed cookies, CSRF, JWT/HS256, flat JSON.
  → `examples/crypto_compose_test.bas`
- **GObject-Introspection (`gi`, GTK 4)** — the canonical `GtkApplication` idiom:
  construct-time properties via `gi.new`, the app drives its own `run` loop, the
  window is built in the `activate` handler. Manual (needs a display); guarded by
  the parse-only smoke. → `examples/gi/gtk4_hello.bas`

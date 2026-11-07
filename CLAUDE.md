# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

gBASIC is a modern BASIC-family language implemented as a tree-walking
interpreter in C11. The single binary `gbasic` lexes, parses, and evaluates
`.bas`/`.gb` source files (both extensions are used interchangeably). Version is
`0.10.0`. See `README.md` for the language feature surface and `docs/` for
design/reference documents.

## Build & run

```sh
make                       # build ./gbasic (runs bison, then cc)
make clean && make         # full rebuild
./gbasic program.bas       # run a program
./gbasic --tokens program.bas   # dump lexer tokens
./gbasic --ast program.bas      # dump parsed AST
./gbasic --add-loads program.bas # print source with suggested `load` lines
./gbasic --json-diagnostics program.bas # emit parse/runtime diagnostics as JSON on stderr
./gbasic --line-buffered program.bas # flush stdout per completed line (pipes block-buffer by default)
./gbasic --version
sudo make install          # install to /usr/local (binary + stdlib); PREFIX overridable
```

Build requires a C11 compiler, `make`, `bison`, and PCRE2 (`libpcre2-dev`; the
regex engine, docs/regex_pcre2_design.md). Optional native modules are
detected at build time via `pkg-config` and gated behind `HAVE_*` macros in the
`Makefile` (`HAVE_LIBPQ`, `HAVE_SQLITE3`, `HAVE_ODBC`, `HAVE_LDAP`, `HAVE_LIBCURL`,
`HAVE_LIBXCRYPT`, `HAVE_LIBCRYPTO`, `HAVE_LIBXML2`, `HAVE_QT`). The interpreter always builds; a missing dependency turns the
affected feature into a clean runtime error rather than a build failure. When
adding code that touches an optional library, guard it with the matching
`#if HAVE_*` block (the existing module code in `src/eval.c` is the pattern).

`GBASIC_PATH=stdlib` tells the interpreter where to resolve `load`ed standard
libraries (`stdlib/*.bas`); set it when running from the source tree, e.g.
`GBASIC_PATH=stdlib ./gbasic examples/gui/demo.bas`. After `make install` this is
unnecessary: the install path (`$(PREFIX)/share/gbasic/stdlib`, default
`/usr/local/share/gbasic/stdlib`) is compiled in as `GBASIC_DEFAULT_STDLIB` and
searched as a fallback when `GBASIC_PATH` is unset or doesn't resolve a library.
`GBASIC_PATH` still wins when set, so it remains the dev override.

## Architecture

The pipeline is the classic lex → parse → AST → walk, but note where the weight
sits:

- `src/lexer.c` (`include/lexer.h`) — hand-written lexer. Has stateful modes
  (`modifier_content_mode`, `lens_content_mode`, `consider_depth`) because
  modifier `(...)=` clauses and `consider` blocks need context-sensitive
  tokenization. Token list lives in `lexer.h`.
- `src/parser.y` — Bison grammar (`-d` generates `parser.tab.c`/`.h`). Builds the
  AST. Regenerated automatically by `make` when `parser.y` changes.
- `src/ast.c` (`include/ast.h`) — AST node definitions and constructors.
  `AstStmtKind`/`AstExprKind` enumerate every node type.
- `src/eval.c` — **the entire runtime, ~18k lines.** This is the file you will
  most often edit. It contains the `Value` model, environment/scope handling,
  watchers, locks, error handling, the date/time/money/file value types, AND the
  full implementation of every optional module — `sqlite`, `pg` (PostgreSQL),
  `odbc` (any ODBC-reachable database), `ldap` (bind and search against a directory),
  `webclient`, `http` (the same requests non-blocking, over libcurl's multi
  interface, with readiness delivered by the webserver's event loop),
  `webserver`, `xml` (libxml2), the Qt `window` backend (`src/modules/qt.c`
  over `qt_backend.cpp`), and the libcrypto crypto builtins — each
  inside `#if HAVE_*` guards. Module dispatch (`sqlite.query`, `pg.exec`,
  `qt.open`, etc.) is resolved here, not in separate files.
- `src/builtins.c` (`include/builtins.h`) — **only a name registry.** A flat list
  of builtin function names so the parser/evaluator can recognize them. The
  actual implementations live in `src/eval.c`. To add a builtin: register the
  name here, implement the behavior in `eval.c`.
- `src/main.c` — CLI entry point and flag handling.

## Tests

Tests are golden-file based: a source file plus a sibling `.out` holding the
expected stdout. Output is compared verbatim (string equality), so update the
`.out` when intended output changes. Negative tests pair a `.bas` with a `.err`.

Each suite below carries a short note. Its full history, the defects it found
and how its tiers were proven, is in `docs/test_suite_history.md`.

```sh
./tests/run_all.sh           # Every suite, discovered by glob: this is the gate, and its full run gates a push. It builds once, then runs each suite in the lane its `# GATE-LANE:` line declares (serial first, parallel across nproc/2 workers capped at 12, exclusive last), checksumming ./gbasic around each parallel suite. Takes a substring filter; `--changed` narrows the run for iteration only (with `GATE_BASE=HEAD` to exercise it); `GATE_JOBS=1` runs serially; `SUITE_TIMEOUT` and `RUN_MANUAL` override. Never build or edit the tree while it runs, and never run two at once.
./tests/run_examples.sh      # Baseline positive suite over a hardcoded list of examples. Runs make clean && make first; new example tests must be added to its list.
./tests/run_library_depth.sh # Checks where a library may be loaded from: beside the loading file, never below it. A local file beside the loader still overrides, and a file that does not parse is skipped unless it is named after the library.
./tests/run_negative.sh      # Error and diagnostic suite: negative tests pairing .bas with .err, including syntax errors that name the reserved word as the author typed it.
./tests/run_repl.sh          # The interactive prompt (gbasic with no arguments, or --repl): sessions, what is recorded versus answered, continuation lines, Ctrl-C, line editing and history via a pty, and the session cache. The pty and interrupt tiers need python3; it exports its own GBASIC_SESSION_DIR.
./tests/run_core.sh          # Core checks a golden cannot express: sleep against the wall clock, env against a variable the script sets, and the read-then-shadow warning (no closures, so assigning a name just read creates a local). Headless, never skips.
./tests/run_sqlite.sh        # The sqlite module, including that database errors name the failing statement (one shared formatter for sqlite, pg and odbc). Skips cleanly if sqlite3 dev files are absent.
./tests/run_insight.sh       # insight.explain_change and reasoning.bas: a drill-down over a population checked against a null model, with a planted-versus-null pair as the main test. Self-checking, headless, takes about 90 seconds.
./tests/run_hypothesis.sh    # insight.weigh and reasoning.hypothesis: ranks candidate explanations against a finding by set agreement, ties identical predictions, and never claims a cause or a probability. Self-checking, headless.
./tests/run_decision.sh      # decision.evaluate and related reasoning libraries: recommendations sized from the right quantity, best versus best-affordable option, assurance, context-field checks, calibration and pooling refusals. Self-checking, headless.
./tests/run_automation.sh    # automation.execute/would/rehearsal/observe: the acting layer, with refusals proven by a side effect on disk (a marker file), a shared gate for would and execute, and holdout assignment. Headless, about 10 seconds.
./tests/run_automation_lab.sh # The automation-reasoning design laboratory: runs the same decomposition on a planted collapse and on pure noise to show a drill-down needs a null model. Experiments, not user documentation. Headless.
./tests/run_accounting_cookbook.sh # docs/accounting_cookbook.md on the shared cookbook harness (RUN/CODE/OUTPUT/COVER tiers over examples/accounting_cookbook). Never skips. Run tools/sync_accounting_cookbook.sh after changing a recipe.
./tests/run_fake.sh          # stdlib/fake.bas fabricated realistic data: generators are pure functions of (seed, index), output is identical across machines, distributions are realistic, and generated invoices post cleanly to accounting. Headless, never skips.
./tests/run_fake_plant.sh    # fake.plant: plants a known defect (round_dollar, just_under, weekend, duplicate, sequence_gap) in known rows with no marker in the data; the report is a separate value. Headless, self-checking.
./tests/run_valgrind_policy.sh # The shared valgrind policy (tests/valgrind_tier.sh) used by many suites, checked against small C programs with a known leak and a known invalid read. Skips without valgrind or a C compiler.
./tests/run_credit.sh        # stdlib/credit.bas portfolio analytics: vintage curves, roll rates, migration and charge-off over a status table, with attrition counted as a state and every loan accounted for. Fixture takes about 12 seconds. Headless.
./tests/run_scoring.sh       # stdlib/scoring.bas credit scorecards: WOE/IV binning, AUC, point scaling and PSI, with expected figures computed externally and a pipeline direction check. Headless, never skips.
./tests/run_deposits.sh      # stdlib/deposits.bas deposit interest, crediting and certificates: balance methods asserted as differences, compounding versus crediting, and early-withdrawal penalties. Headless, never skips.
./tests/run_lending.sh       # stdlib/lending.bas loans, servicing and payoff: declared accrual basis, waterfall and day count, apply as a fold, and a ledger tier posting a loan's life to accounting. Self-checking, headless.
./tests/run_accounting.sh    # stdlib/accounting.bas double-entry: the accounting equation after every posting and close, a sign check per account kind, refusals each beside a legal neighbour, and refused double closing. post returns the new ledger. Headless.
./tests/run_exact.sh         # Exact integers as a property of number: arithmetic, comparison, sort and counting builtins stay exact above 2^53, with overflow degrading and warning. Checked against python3 big ints. Headless, no network.
./tests/run_serialize.sh     # serialize version 3, the portable little-endian value encoding shared with G-SQL: round trips, pinned bytes, reading old v2 payloads, corruption refusals. The independent decoder oracle needs python3 and skips without it.
./tests/run_gsql_key.sh      # G-SQL index key encoding: memcmp order of keys must match gBASIC's own value order, ascending, descending and composite, with pinned keys. Skips without a C compiler.
./tests/run_gsql_io.sh       # G-SQL simulated I/O layer and crash harness: crash, power-loss and torn-write images, held to POSIX rules, with a toy engine and broken copies the harness must catch. Takes about 23 seconds. Skips without a C compiler.
./tests/run_gsql_store.sh    # G-SQL storage engine (LSM) and version store: build under -Wpedantic -Werror, store and version tests against models, the crash gate, and the on-disk format with a committed fixture. Takes about 2.5 minutes; GSQL_GATE_SCALE=full runs research ranges. Skips without a C compiler.
./tests/run_gsql_table.sh    # G-SQL table layer: catalog, typed rows, keys, indexes, defaults, ADD COLUMN, and the external sorter, working on serialize v3 bytes against a random model and an independent layout reader. Takes about 6 seconds. Skips without a C compiler.
./tests/run_gsql_owner.sh    # G-SQL owner process and wire protocol: framing, queueing, cancel, timeouts, lease, group commit, owner lifetime and linger. Uses a short runtime directory under /tmp. Takes about 14 seconds. Skips without a C compiler.
./tests/run_gsql_sql.sh      # G-SQL SQL layer: parse, semantics corpus, differential tests against forced scans, joins and join ordering, aggregates, subqueries, views, foreign keys and checks, window functions, WITH RECURSIVE and ROLLUP/CUBE/GROUPING SETS (each checked against SQLite by a differential tier, which skips without python3's sqlite3), with SQL operators evaluated by gBASIC itself. About 20 seconds. Skips without a C compiler.
./tests/run_gsql_module.sh   # The gsql module from a program: connect, query, exec, insert (many rows as records, all or nothing), columns (a query described from its plan, nothing run), transactions (a nested begin is a savepoint; a statement refused part way is undone alone), sql and database blocks (including a library's, from outside), dump and restore, backup, warnings, a connection's `max_recursion`, lost-owner handling, and processes with a private /tmp each (systemd's PrivateTmp, via bwrap) reaching one owner, since the socket is in the database directory. Takes about 2 seconds. Skips when the build has no G-SQL.
./tests/run_same_value.sh    # The second equality (same value, used by contains, find, unique, deep comparison, watchers) versus the = operator, and the NaN sort order: NaN is the same value as NaN in routes but not under = or <. Headless.
./tests/run_three_valued.sh  # Three-valued logic: unknown follows Kleene tables for and/or/not and propagates through comparison and arithmetic; if/while still refuse unknown. Oracle is SQL's tables written by hand. Headless.
./tests/run_money.sh         # The money type: exact construction from text or shortest decimal, 178 currencies with minor units, guard digits, allocation, dated FX, and the finance library. Self-checking, headless, never skips.
./tests/run_estate.sh        # The estate fixture (stdlib/estate.bas) with written-down truth, and discovery traced across it, including lineage. The offline tier needs no database; the live tier needs GBASIC_ESTATE_CONNECTION and skips without it.
./tests/run_discovery.sh     # stdlib/discovery.bas: declared facts about a database estate (catalog, identity, predicates, explain, column lineage, SQL reader). Pure-SQL tiers run without a database; the catalog tier needs an ODBC driver, and the whole suite skips without ODBC in the build.
./tests/run_odbc.sh          # The odbc module: connect, bound parameters, exact numbers, catalog calls, refusals. Runs hermetically against the SQLite3 ODBC driver, or against another database via GBASIC_ODBC_CONNECTION. Skips cleanly without ODBC in the build or a driver installed. Checks are self-checking, not golden.
./tests/run_webclient.sh     # The webclient module against a loopback fixture, including binary (NUL-bearing) bodies in both directions. Skips if the environment is unavailable.
./tests/run_steward.sh       # Steward, the AI reference application's first slice: exercises HTTP request, model-call worker, tool call and approval joined end to end. Answers come from recorded fixtures, no network. Skips without python3. Note a top-level watch beside a program block registers nothing.
./tests/run_retrieval.sh     # The retrieval library: permission-filtered nearest-neighbour search over pgvector (the ACL filter must narrow before ranking). An offline structural tier always runs; live tiers need GBASIC_POSTGRES_TEST=1 and a PGDATABASE with pgvector.
./tests/run_mcp.sh           # The mcp library: publishing a toolset over the Model Context Protocol (one dispatcher, stdio and HTTP transports) and consuming one (mcp.connect). Protocol tiers need no transport (resources, templates, prompts, a per-request principal, and Streamable HTTP's rules on request records); transport tiers skip without python3/curl, and the streamable tier runs a server block with two workers to show one signed session served by both. Stdio sessions need --line-buffered.
./tests/run_agent.sh         # The agent library: a conversation is a value and agent.apply(run, event) is a pure step returning the new run plus actions, with an approval gate for mutating tools. Headless, no network, never skips (bar valgrind).
./tests/run_llm_transcript.sh # The llm canonical part-based transcript (one transcript, different Anthropic/OpenAI wire shapes), keyed replay, and llm.embed batch embeddings. Headless, no network, never skips (bar valgrind). The negative_llm_*.err goldens use a .nolineno sidecar so library line numbers are not pinned.
./tests/run_web_configure.sh # web.configure(sv, settings): deployment settings (e.g. workers, port) layered onto a declared server block, validated by name. Includes a tripwire that the admitted-options lists in src/frontend.c and stdlib/web.bas stay identical, plus a live tier.
./tests/run_tools.sh         # The tools library: one declaration of what a model may call, generating the schema and validating/dispatching calls (errors become results, bodies run in a worker pool). Callers spawn the workers, since spawn takes a bare function name.
./tests/run_inbox.sh         # watch(inbox.messages): an actor reply delivered by the event loop instead of a blocking receive(). Use unwatch to end delivery so the loop can exit; tiers with possible hangs are bounded.
./tests/run_timer.sh         # The timer source: periodic work on the event loop, with coalescing (at most one tick per timer per iteration) and a reported skipped count. Time is measured from outside the interpreter because its clock has second resolution. The timer watch must be inside a program block.
./tests/run_principal.sh     # with principal(p) / principal(): the identity a body acts on behalf of, dynamically scoped, not inherited by actors or event-loop handlers. Headless except the handler tier, which needs python3 and curl.
./tests/run_net.sh          # The net module (TCP and Unix sockets, TLS, UDP datagrams) and pack/unpack: waited and watched connections, take/read framing, backpressure, idle timeouts, peer credentials, named error codes. Python peers check split frames, 1000 connections, a 10 MiB write to a slow reader and a flat memory bound; a modal Qt dialog does not stop service. TLS tiers run against Python's ssl module with certificates made per run (verification, SNI, mutual TLS, start_tls and the STARTTLS injection, bytes held inside libssl). Datagram tiers check that one receive or event is one datagram (never split or joined), truncation past max_datagram is reported, and 1000 random datagrams echo through Python's socket module. Valgrind and --track-fds. Keepalive (`keepalive:` read back from the socket, the kernel's timers read by `ss`) and a waited reader behind a faster peer, whose refill must cost what it consumed (a ratio of two buffer sizes; it was 60x). Peer tiers skip without python3; TLS skips without openssl or libssl.
./tests/run_text_encoding.sh # transcode, valid_utf8 and first_invalid_utf8: 4120 conversions across utf-8, latin-1, windows-1252 and the three utf-16s, a third corrupted, each decided by Python's codecs (output bytes or the refusal's offset), plus utf-16 with no BOM (big-endian, where Python differs) and the refusals' wording. Valgrind. Skips without python3.
./tests/run_zip.sh           # The zip module (zip.members, zip.read, zip.extract) over zlib: 274 members of 43 archives made by Python's zipfile agree with its answers (names in UTF-8 and cp437, data descriptors, stored and deflated, NUL bytes); refusals for duplicate names, ZIP64, encryption, other methods, a wrong CRC and a cut-short file; an extract with one unsafe name (.., absolute, link) writes nothing. Valgrind. Skips without python3 or zlib.
./tests/run_websocket.sh     # The websocket library (RFC 6455 over net) and the webserver's upgrade hand-off: valid_utf8/bytes_xor against Python, both ends in one program, the server and client against Python's websockets package and raw-socket peers (every length form, fragments, ping, both closing handshakes, eleven violations each closed with its code, a frame a byte at a time and one in the handshake's write), wss with the TLS session moved to net, valgrind. Skips without libcrypto; peer tiers skip without python3's websockets.
./tests/run_http.sh          # The http module: non-blocking requests over libcurl's multi interface, with readiness delivered by the event loop. Python3 loopback fixture on an OS-assigned port; skips without python3 or libcurl in the build. Tiers that can hang are bounded by timeout.
./tests/run_webserver.sh     # The webserver module, including bodies as bytes both ways: a NUL in a response body keeps its Content-Length, and a request body holding a NUL is accepted rather than refused with 415. Driven through tests/webserver_client.py.
./tests/run_scope.sh         # Scope rules: defining the same function twice in one scope is refused, and a function from another library must be qualified (the error names the fix). Headless, self-contained.
./tests/run_alias.sh         # load NAME as ALIAS: an alias is the importing file's name for a library, so two libraries declaring the same name can coexist. Covers aliasing controls and seven refusals, and that one file reached by two path spellings (`util.bas`, `web/../util.bas`, a symlink) is one library. Headless, never skips (bar valgrind).
./tests/run_namespace.sh     # The library_collisions() and password_hash_cost() builtins: the first reports names defined by more than one loaded library (not an error; stdlib has benign collisions), the second times one real hash. Headless; the password tier needs libxcrypt.
./tests/run_web_form.sh      # req.form: the request body decoded as application/x-www-form-urlencoded (only for a form content type, empty otherwise), sharing the query-string parser, including %00 handling. Uses curl on loopback; skips without curl.
./tests/run_web_bind.sh      # webserver.listen address binding: the default must stay loopback-only (127.0.0.1) and explicit wildcard, specific, IPv6 and v4-mapped binds behave as asked. Client is bash /dev/tcp on loopback. Never skips bar IPv6/valgrind.
./tests/run_gbasic_site.sh   # The gbasic.org site prototype (examples/gbasic_site/site.bas) driven end to end.
./tests/run_frontend.sh      # The embedding API: links libgbasic.a from C to test the diagnostics sink, structured parse diagnostics, and two concurrent parse contexts. Structural, not stderr text. Skips cleanly with no C compiler.
./tests/run_json_diagnostics.sh # The --json-diagnostics output against a golden, and that default output stays byte-exact.
./tests/lsp/run_lsp.sh       # gbasic-lsp: a position-transcode unit test and a framed JSON-RPC handshake golden. Builds gbasic-lsp itself.
./tests/run_process.sh       # The process API: process.run and live child control (start, poll, read, wait, stop, release, write/close_stdin). Deterministic via gate files, not timing. GI-independent, never skips; the zombie tier skips if ps is absent.
./tests/run_json_strict.sh   # Strict JSON (json_encode/json_encodable): golden type mapping plus a standards proof validating output with python3's json module, never gBASIC's own decode. Skips without python3.
./tests/run_stream.sh        # The --line-buffered flag: output streams mid-run with it and not without, composes with --json-diagnostics, and survives a killed child. Headless, never skips (bar valgrind); gate-file determinism.
./tests/run_try_decode.sh    # try_decode(text): decode reporting failure as a value ({ok,value,message,offset,line,column}) instead of raising, sharing decode's parser; a key written twice in one object is refused at the second key. Headless, no python3, valgrind tier over malformed inputs.
./tests/run_stderr.sh        # print to error: output reaches fd 2 and not fd 1 from every statement position, renders identically to print, and interleaves correctly with and without --line-buffered. Headless, never skips (bar valgrind).
./tests/run_pre_registration.sh # Tripwire on the set of declarations eval_program pre-registers before a program block (read from the PRE-REGISTRATION SET markers in src/eval.c), plus behavioural checks. gBASIC Studio's declaration-hoisting rule is defined as that set. Also pins warning 2106 for top-level statements beside a program block.
./tests/run_web_routes.sh    # The route table as data (stdlib/web.bas): matching and specificity, order-independence, web.static containment, build-time refusals, and a live loopback tier. Self-checking fixtures; refusals are compared by message with position stripped.
./tests/run_web_hardening.sh # Web hardening: request and idle-read timeouts (504/408), smuggling-resistant framing (Transfer-Encoding 501, conflicting Content-Length 400), and web.trust_proxy. Client is curl and nc; skips without curl.
./tests/run_web_stream.sh    # Streaming responses: SSE from a handler, parked streams poked from another connection, timeout exemption, disconnect reaping, and large file serving with Content-Length. Skips without curl.
./tests/run_web_pool.sh      # The web process worker pool: supervisor holding the listener, workers inheriting it, drain on SIGTERM, and rolling reload that never retires a worker on faith. Ports are OS-assigned; driven with curl, so it skips without curl.
./tests/run_web_tls.sh       # TLS termination: default certificate, SNI selection (checked with openssl s_client), bad cert/key refused at listen time, and pooled shape. Certificates are generated per run. Skips without curl or openssl, or if the build has no libssl.
./tests/run_web_server_block.sh # The declarative server block: offline web.routes/web.dispatch, declaration hoisting, load-time refusals, --json-diagnostics, and live tiers over the runtime. Live tiers skip without curl; block_tls skips without openssl or libssl. 'server' is the only reserved word added.
./tests/run_nap_fs.sh        # Checks filesystem metadata builtins and atomic_replace. Needs no GI; the cross-device case is gated on a distinct /dev/shm, and NAP_FS_STRESS=1 opts into a concurrency stress tier.
./tests/run_chart.sh         # Checks chart.bas, which renders line, scatter, area, bar, histogram, pie, heatmap and sparkline charts as deterministic SVG text, with golden, oracle, determinism, structure and valgrind tiers. The structure tier skips without libxml2. Chart .err goldens pin stdlib/chart.bas line numbers, so editing the library means regenerating them.
./tests/run_rules.sh         # Checks stdlib/rules.bas, typed business rules as data: evaluating a stack of rules against a condensed compiled form, absence (unknown) handling, validation, and depth caps. Self-checking, headless, no network; includes a valgrind tier. Adding hand-rolled set-union loops can trip the run_absence.sh census.
./tests/run_datetime_convert.sh # Checks that {date}, {time} and {datetime} accept a datetime value and convert it, refusing values that lack what the target kind needs and leaving the text forms unchanged. Headless, never skips apart from valgrind.
./tests/run_set_ops.sh       # Checks the infix word operators `excluding` and `intersecting`: membership semantics, preserved duplicates, and the documented product-of-lengths cost. Headless; includes a valgrind tier.
./tests/run_notation.sh      # Checks stdlib/notation.bas, a readable text format that keeps every gBASIC type (dates, money, durations, files), including round trips, hand-edit tolerance and refusals. Headless, no network; includes a valgrind tier.
./tests/run_persist.sh       # Checks stdlib/persist.bas: crash-safe atomic versioned writes and a read that reports missing, corrupt or loaded as a value, plus round trips and a large-index timing bound.
./tests/run_outline.sh       # Checks the source_outline(text) builtin: 12 path-free structural fixtures with byte ranges and diagnostics, a 25000-node large file, repeated-call stability, and a valgrind tier that skips if valgrind is absent.
./tests/run_stridx.sh        # Checks that string access (byte_at, len, mid, left, right) is not quadratic in string size, via correctness goldens, 4x-step shape ratio tests, a ceiling, and valgrind. Headless, never skips apart from valgrind.
./tests/run_arridx.sh        # Checks array aliasing under copy-on-write and that arr[i] and append are linear, using aliasing, shape-ratio, negative-control and valgrind tiers. Headless; never skips apart from valgrind.
./tests/run_recidx.sh        # Checks that record field access is a hashed map rather than a linear walk, using semantics, indexed-vs-linear parity, linear and flat shape ratios, a negative control and valgrind. Trap: detach (copy-on-write) only where ownership is structural, or refcounts double-free.
./tests/run_render.sh        # Checks that print and string() share one renderer so arrays, records, typed values and durations display correctly, while encode/json_encode still refuse typed and live values. Tiers: parity, golden, per-shape, totality, boundary, valgrind.
./tests/run_equality.sh      # Checks that `=` on compound values (arrays, records) and on scalar/other kinds compares values rather than coercing to zero, and that contains, find, remove_value, consider and unique agree with it. Self-checking fixtures with a deep-equality oracle written in gBASIC; includes valgrind.
./tests/run_typed_locks.sh  # Typed variables (`x:number = e`): the colon locks a variable to one data type, a plain = converts into it or refuses naming the lock, and `x:text = ...` relocks. Also `function f(a:number):money(USD)`: arguments, defaults and every `return` convert. Covers compound assignment converting text first, comparisons reading text as the locked type, loops refusing a locked variable, parse-time refusals with the gBASIC spelling (`x:int`), the prompt, --ast and valgrind. Headless.
./tests/run_numfmt.sh        # Checks that print, string() and quote() show the shortest decimal that round-trips, including numbers nested in containers. Tiers: nesting invariant, golden, round-trip, an awk oracle, buffer width, valgrind. Headless, no python3.
./tests/run_string_nul.sh    # Checks that gBASIC strings are counted bytes where NUL is content, across len, mid, replace, trim, join, split, repeat, starts_with, ends_with and the modifier forms; empty-needle refusal is still enforced. The fixture runs under timeout because a regression hangs; includes valgrind.
./tests/run_regex.sh         # Checks the regex value kind (POSIX ERE via libc) and the overloads of contains, replace and split plus match and match_all: golden, flag matrix, negative messages, actor round trip, valgrind. Headless, never skips apart from valgrind.
./tests/run_xlsx.sh          # Checks the xlsx module over zlib and libxml2: ZIP container, part tree, sparse cells, read/write round trip, formula evaluation and recalc, cross-sheet refs, shared formulas, shape timing, valgrind. Number formats type cells (date, datetime, time, duration, percent; both epochs) against values Python computed; creating a workbook (new, add_sheet, cells in order, typed values, set_formula) is read back here and by LibreOffice as shown. An independent-reader tier needs LibreOffice and skips without it; the suite skips if built without zlib or libxml2.
./tests/run_xlsx_cookbook.sh # Checks docs/xlsx_cookbook.md on the shared cookbook harness: RUN, CODE, OUTPUT and COVER tiers over examples/xlsx_cookbook/*.bas, whose files own the code and output while tools/sync_xlsx_cookbook.sh copies them into the page. Skips without zlib/libxml2; recipes 11-12 skip without sqlite.
./tests/run_nlq.sh           # Checks nlq schema retrieval and grounding with no model and no SQL, against a committed estate catalog and questions: recall floor, selection ceiling, synonym improvement, ranking order. A value tier that scores recorded SQL needs NLQ_ODBC_CONNECTION; otherwise it skips and says nothing was scored. Headless, no network.
./tests/run_finio.sh         # Checks the finio Phase 0 value model: location tied to the source file, unknown/invalid/present field answers, the registry rules, and an amount-kind tripwire requiring every adapter to declare its amount type. The provenance cost measurement runs only with RUN_FINIO_COST=1. No network.
./tests/run_finance_cookbook.sh # Checks docs/finance_cookbook.md on the shared cookbook harness (RUN/CODE/OUTPUT/COVER over examples/finance_cookbook/*.bas). Pure gBASIC; never skips.
./tests/run_handle_methods.sh # Checks that h.verb(args) on a handle equals module.verb(h, args) for G-SQL, Qt, sqlite, pg, odbc, ldap, xml, process, http and workbook handles. Each module tier skips, naming what was not measured, if the build lacks the module; the Qt tier needs Qt and a display.
./tests/run_gsql_cookbook.sh # Checks docs/gsql_cookbook.md on the shared cookbook harness (RUN/CODE/OUTPUT/COVER over examples/gsql_cookbook/*.bas). Skips, saying so, when the build has no G-SQL. Trap: a line continues only inside brackets, so a SQL string built with a trailing + does not parse.
./tests/run_money_cookbook.sh # Checks docs/money_cookbook.md on the shared cookbook harness (RUN/CODE/OUTPUT/COVER over examples/money_cookbook/*.bas). Never skips; money is core and finance is pure gBASIC.
./tests/run_odbc_cookbook.sh # Checks docs/odbc_cookbook.md on the shared cookbook harness (RUN/CODE/OUTPUT/COVER over examples/odbc_cookbook/*.bas). Skips if ODBC is compiled out or no SQLite3 ODBC driver is installed.
./tests/run_chart_cookbook.sh # Checks docs/chart_cookbook.md on the shared cookbook harness (RUN/CODE/OUTPUT/COVER over examples/chart_cookbook/*.bas). Pure gBASIC; never skips.
./tests/run_datetime_cookbook.sh # Checks docs/datetime_cookbook.md on the shared cookbook harness (RUN/CODE/OUTPUT/COVER over examples/datetime_cookbook/*.bas). Never skips.
./tests/run_lending_cookbook.sh # Checks docs/lending_cookbook.md on the shared cookbook harness (RUN/CODE/OUTPUT/COVER over examples/lending_cookbook/*.bas). Pure gBASIC over money; never skips.
./tests/run_deposits_cookbook.sh # Checks docs/deposits_cookbook.md on the shared cookbook harness (RUN/CODE/OUTPUT/COVER over examples/deposits_cookbook/*.bas). Never skips.
./tests/run_credit_cookbook.sh # Checks docs/credit_cookbook.md on the shared cookbook harness (RUN/CODE/OUTPUT/COVER over examples/credit_cookbook/*.bas). Never skips.
./tests/run_ari.sh           # Checks stdlib/ari.bas, the anchor-relative report parser: goldens over hand-made and generated fixtures, generator drift, furniture independence across pagination, valgrind, the limitations register, ari.trace claimed/unclaimed spans, and the book's gaps (an amount printed as asterisks, `wraps`, `ends` without `starts`, `using century`). Headless, no network. Money is asserted as integer cents or as its printed text with cents, because print hides lost cents.
./tests/run_grid.sh          # Checks stdlib/grid.bas, which turns a messy worksheet into clean frames: automatic table guess with confidence versus explicit spec extraction, with arithmetic checks against the sheet's own total row. A second fixture has merged header parents, a heading written twice and "<name> subtotal" rows, with every dropped row reported. Needs GBASIC_PATH, so it has its own runner.
./tests/run_consolidate.sh   # Checks stdlib/consolidate.bas, which merges many differently-shaped tapes into one frame with fuzzy aliasing, money and percent normalisation, and rejection of sources missing required columns, or with two columns matching one rule. Cells a kind cannot read are listed in `refused`, never silently blank. Assertions are arithmetic, not goldens. Needs GBASIC_PATH.
./tests/run_dbframe.sh       # Checks stdlib/dbframe.bas, which loads a frame into a database table, running the whole xlsx, grid, consolidate and sqlite pipeline, and into an existing G-SQL table (`to_gsql`), each refused row reported and undone alone. Values are always bound and identifiers validated. Needs GBASIC_PATH; skips if sqlite is not in the build.
./tests/run_compile.sh       # Checks the xlsx formula compiler (xlsx.to_sql) and xlsx.apply: lowering column formulas to SQL, row-by-row oracle against the evaluator, three-way agreement with the frame pass, refusals, and a speed ratio. Needs GBASIC_PATH and sqlite.
./tests/run_market.sh        # stdlib/market.bas daily price history: checks series come back oldest-first regardless of provider order and that split-adjusted prices are used as the provider supplies them. No network; replays committed fixtures. Failure is a returned value, not a raise.
./tests/run_crypto_ssh.sh    # SSH's primitives: X25519, RSA as records of big-endian parts ({n,e,d,p,q,iqmp}), AES-CTR returning the next counter, bcrypt_pbkdf (vendored from OpenSSH, src/vendor/), and (crypto_ssh_needed_test.bas) raw ChaCha20/Poly1305, ECDSA/ECDH on P-256/384/521, classic DH, HKDF, sha384 and incremental hashing whose state is a value. RFC 7748, SP 800-38A, RFC 8439 and RFC 5869 vectors, ChaCha20/Poly1305/HKDF rewritten in Python from the RFCs, DH against Python's pow over groups computed from RFC 3526, Python cryptography both ways (PKCS#1 v1.5 signatures equal byte for byte), ssh-keygen and openssl dgst, Python bcrypt, and a gBASIC program opening keys ssh-keygen -a encrypted, valgrind. Skips without libcrypto or python3 cryptography.
./tests/run_otp.sh           # stdlib/otp.bas one-time passwords (HOTP/TOTP) checked against the published RFC 4226 and RFC 6238 vectors, plus replay refusal, skew limit, base32 handling and refusals. Self-checking, no network; SHA256/SHA512 vectors use longer seeds than SHA1.
./tests/run_crypto.sh        # The libcrypto builtins and stdlib/crypto.bas: base64/hex, digests, HMAC, AES-256-GCM, Ed25519, signed cookies, CSRF and JWT. Binary outputs are hex-encoded; random-input cases assert round trips and tamper rejection. Skips without libcrypto.
./tests/run_gui_parse.sh     # Parse-only smoke test of every program in examples/qt, so syntax rot is caught without Qt or a display.
./tests/run_doc_examples.sh  # Runs every ```basic block in the docs pages to check it parses (and runs, per page mode). Exemptions are markers in the page: <!--fragment: why--> and <!--needs-context-->; a marker that is not needed fails.
./tests/run_docs_gate.sh     # Executable-docs gate: every file referenced by docs/ai/COOKBOOK.md exists and is wired into a suite; every performance claim must cite a tests/run_*.sh that asserts it; the project_state roster matches stdlib/*.bas; docs may not deny shipped capabilities.
./tests/run_library_tiers.sh # Derives which stdlib libraries the adjacent binary can actually load (from each library's load lines, transitively, plus builtins whose availability moves with the build) and holds docs/reference.md's two tables to that derivation. Headless, no network.
./tests/run_ksyntax.sh       # Checks the repo's Kate definition (editors/kate/gbasic.xml) works with KDE KSyntaxHighlighting, looked up by name because *.bas collides with FreeBASIC. Skips saying nothing was checked if the library is absent; KSYNTAX_INCLUDE/KSYNTAX_LIB override the location.
./tests/run_window.sh        # The `window` declaration parsed, validated and introspected with no display and no Qt: closed vocabulary with near-miss hints, handler targets, naming rules, grammar conflict count (must stay zero). Headless; never skips.
./tests/run_qt_backend.sh    # The Qt backend rendering a `window`: no-display refusal (Qt aborts uncatchably, so it asks Qt in a forked child), event-loop service, signals via qt.invoke, form binding, option refusals. Self-checking. Skips without Qt, saying nothing about rendering was measured.
bash tests/run_bag_smoke.sh  # BAG (Basic Adventure Generator) smoke test, including the Qt GUI player which plays itself in check mode through the real handlers. The GUI tier skips with its reason where Qt or a display is absent. A known warning 2101 from bag.bas appears in its output.
./tests/run_qt_tree.sh       # Qt `tree` widget (rows with a depth) and qt.depth: declaration rules, depth column, index and selection semantics. The depth column is declared but not drawn. Grammar and declaration tiers run without Qt; the rest skips without Qt.
./tests/run_qt_reentry.sh    # Qt handlers that call back into the backend (select then refresh inside a handler), including deferred deletion, a handler-depth cap (warning 2117) and survival over ten runs. Run under valgrind. Skips without Qt except the source-reading one-door tier.
./tests/run_qt_frame.sh      # Qt main-window furniture: `docks`, `dock`, `toolbar` and `status`, qt.dock and saved layout (base64). A window is a QMainWindow only when it declares one of these. Skips without Qt; grammar, declaration and root tiers run anyway.
./tests/run_qt_clipboard.sh  # qt.copy and qt.clipboard: clipboard round trip after the copying frame returns, NUL carried, unknown when empty, display required. A C++ probe linking the backend checks the write and read halves separately (skips without a C++ compiler).
./tests/run_qt_which.sh      # qt.which_tab and qt.right_click aimed at tree rows and tabs: which thing an event was about, including tabs placed at the bottom. A source scan checks the signal slot is chosen by type name. Skips without Qt; the scan runs anyway.
./tests/run_qt_keys.sh       # Several shortcuts per menu or toolbar item (`shortcut: [...]`), qt.shortcuts and qt.key, which sends a real key event through Qt's shortcut map. A synthesized key is not a keyboard layout. Skips without Qt.
./tests/run_qt_align.sh      # The `align:` option on table and tree columns and qt.alignment; column option sets are closed and checked at load time. Declaration tier needs no Qt; the rest skips without Qt.
./tests/run_qt_find.sh       # Studio's find-and-replace verbs: qt.label (a part's caption, read from Qt), `whitespace` on a source or memo, qt.select_text/qt.selection, qt.replace_text as one undo step, qt.highlight_text/qt.highlights, and qt.click (left button, aimed as qt.right_click aims). Columns count codepoints. Skips without Qt.
./tests/run_qt_gutter.sh     # Source-editor margin: qt.mark categories (section, break, error, warning, note; line 1 is the first line), qt.marks, qt.gutter and `on <part>.gutter` clicks. Paint probe needs a C++ compiler; skips without Qt, and the tripwire tiers read source and still run.
./tests/run_qt_children.sh   # qt.refresh on containers whose children change, plus qt.children and qt.tabs; kept children retain state and removed ones are deleted. Skips without Qt except grammar, declaration and source-tripwire tiers.
./tests/run_qt_cookbook.sh   # docs/qt_cookbook.md on the shared cookbook harness: RUN/CODE/OUTPUT/COVER over examples/qt_cookbook, rendered offscreen. Only the event-loop recipe calls qt.run(). Suite exports GBASIC_SYNTAX_PATH=editors/kate; recipe 09 skips without KSyntaxHighlighting.
./tests/run_qt_capture.sh    # qt.capture: a window or one part written as a PNG at a scale, offscreen. Sizes are read from the PNG header, pixels by PIL (colours, text extent doubling at scale 2), and twelve captures of a focused editor in a running loop must be one picture (the caret blinks otherwise). Skips without Qt; the pixel tier skips without PIL.
./tests/run_qt_lifecycle.sh  # Window lifecycle: qt.close, qt.is_open, `on <win>.closed` and addressing a window by its own name. A close removes the window from the registry, so the closed handler is the last place to read the form. Skips without Qt; the declaration tier runs anyway.
./tests/run_qt_chooser.sh    # qt.choose_file, qt.choose_save, qt.choose_folder and qt.dialog(): Qt's own widget dialog is forced (the native one cannot be driven), a cancel answers unknown, and options are a closed set. Skips without Qt except the native-forcing source tripwire.
./tests/run_qt_split.sh      # Qt `split` and `scroll`: declaration, qt.pane oracle, and a source tripwire that every door asking a parent for a layout admits a splitter. Skips without Qt except the grammar, guard and declaration tiers.
./tests/run_qt_modal.sh      # qt.open_modal, qt.accept and qt.reject: the same window shown modally through one shared open path. The modal-extent tier needs curl; valgrind tier is time-bounded. Skips without Qt except the one-open-path source tripwire.
./tests/run_qt_icon.sh       # Icons: `icon:` on leaves and menu items, qt.set icon and qt.has_icon. Three sources (SP_ standard pixmaps, existing file, theme name); a missing theme name warns (2116) rather than raising. The runner generates its own PNG. Skips without Qt except the one-door tripwire.
./tests/run_qt_ask.sh        # Model-backed tables: `table x to ask fn`, qt.rows, and cost proportional to the viewport rather than row count. Asserts cell-callback counts across table sizes and has a hang guard. Skips without Qt except grammar and declaration tiers.
./tests/run_qt_panes.sh      # qt.pane sizes and qt.resize_panes (restoring split layout): sizes are proportions, not exact values; a wrong pane count is refused. Skips without Qt except grammar and one-walk source tiers.
./tests/run_qt_wheel.sh      # The wheel: `on <part>.wheel`, qt.wheel, qt.swallow, qt.scroll, qt.scrolled and qt.scroll_to. Ctrl+wheel zoom needs no verb (read and write the font property). Skips without Qt except grammar, no-zoom-verb and declaration tiers.
./tests/run_qt_context.sh    # Context menus: `popup`, qt.popup (including popping under a part), qt.right_click and qt.menus. Declaring a context handler replaces Qt's default menu for that part. A popped menu stays up until dismissed. Skips without Qt except source and declaration tiers.
./tests/run_qt_dialog.sh     # Dialogs: qt.inform, qt.confirm (answers a boolean, No is the default) and qt.answer. A dialog raised inside a watcher body stalls loop-delivered sources; the stall tier needs curl. Skips without Qt, saying dialogs were not measured.
./tests/run_qt_examples.sh   # Runs every program in examples/qt in check mode offscreen (exit 0, output, clean stderr), also with no display (must degrade with a sentence), and checks the README lists every example. Each example accepts `check` as an argument.
./tests/run_qt_source.sh     # The `source` code-editor widget: highlighter definition lookup by name via GBASIC_SYNTAX_PATH or the baked-in path, qt.highlighting, qt.caret and qt.caret_to (1-based), warning 2115 when no definition is found. Valgrind tier turns the regex JIT off. Skips without Qt or KSyntaxHighlighting headers.
./tests/run_qt_builder.sh    # The builder: a window a program builds as a record, plus qt.check, which applies the declaration's rules (one checker shared with the AST adapter). qt.check needs no Qt; the built-window tier skips without Qt. Nesting depth is capped at 64.
./tests/run_qt_loop.sh       # Qt event-loop probe: measures that Qt owning the loop with gBASIC sources as socket notifiers keeps serving during a modal dialog, and that the other arrangement starves. Skips without Qt6Widgets dev files, moc or a C++ compiler, saying the loop question was not measured.
./tests/run_portable.sh      # Harness helpers: tests/portable.sh supplies GNU-style tools where the system has BSD ones, and tests/premise.sh probes whether the machine can build a tier's premise (chmod effect, mkfifo, symlinks, RSS meter). A suite that calls a probe must source premise.sh, or its guarded tiers skip silently.
./tests/run_capabilities.sh  # `gbasic --capabilities`: the native-module surface a scanner derives, checked against the interpreter's own lists in both directions, plus `has_builtin` answering truthfully for the current build (true exactly when calling the name does not refuse for unavailability). Every conditionally registered builtin needs a probe.
./tests/run_limitations.sh   # Runs the DOGFOOD.md accepted-limitations ledger: each live limitation is probed and the suite goes red when it is fixed, naming the entry to strike. Struck entries get the opposite check. A new live bullet must be probed or listed in the small not-mechanical list.
./tests/run_silent_traps.sh  # Former silent failures now raise: goto/gosub to a missing label, out-of-range array reads, and failing typed-value modifiers. Each must exit nonzero with a located runtime error, stop execution at the failure, and be catchable with `on error goto next`.
./tests/run_warning_model.sh # The warning channel (PLAT-WARN): modes, claiming, dynamic mode lookup, escalation attributed to the warning's own site, independence from pending errors, and unused-result warnings with their exemptions. Warnings must not trigger the error model's rule 2 re-raise.
./tests/run_error_model.sh   # Frame-scoped `on error` (PLAT-ERR): catch-and-return, the two anti-silence rules, propagation, disarm and re-arm, `e = error` snapshots and re-raise, traces, first-raise-wins, and raise positions naming the failing expression. The fatal stderr line format must stay byte-identical.
./tests/run_library_scope.sh # A library's unqualified call resolves to its own function first, independent of load order, while a root program and unrelated names still resolve normally. Also checks the note emitted when a library function shadows a built-in: correct text and the declaration's own position.
./tests/run_keyword_fields.sh # A keyword used as a record field name is a plain name and keeps its case, both in literals (`{ OR: 1 }`) and after a dot. Controls confirm keywords are still rejected as variable names, e.g. `to = 5` stays a parse error.
./tests/run_brace_modifiers.sh # Brace-form modifier clauses (PLAT-BRACE): every modifier position in the brace spelling, the old paren spelling refused, and source tripwires that the old lookahead guesser and its lexer/parser machinery are gone. Headless.
./tests/run_for_each_index.sh # `for each item, i in list` gives element and index; the body still cannot write through the element, so write-back is `list[i] = item`. Covers snapshot semantics when appending in a loop, zero grammar conflicts, and warning 2107 for a discarded element write (deliberately conservative, one hop only).
./tests/run_exponent_literal.sh # Scientific-notation numeric literals (`1e20`) equal `number("...")` on the same text, with hex, durations and names like `e2` left alone. A malformed duration unit is a located, nonzero-exit parse error that reaches the JSON diagnostics sink. Valgrind runs twice, including the refusal path.
./tests/run_lock_signal.sh   # A program holding a `lock` must die by the real signal that killed it, not exit with 128+signo. Uses a python3 probe reading waitpid status, since a shell cannot tell the two apart. Skips without python3. No valgrind tier.
./tests/run_libcurl_floor.sh # Guards that gBASIC compiles against libcurl 7.81 (the ubuntu:22.04 floor) by comparing the libcurl identifiers the sources use against a generated list of symbols at that floor. A real container build is opt-in via LIBCURL_FLOOR_BUILD=1. The symbol list is generated by tools/make_libcurl_floor_symbols.sh, never hand-edited.
./tests/run_dir_builtins.sh  # `exists` on a directory (answered by stat) and `make_dir(path, { parents: true })`. `make_dir` without parents is deliberately not idempotent because an existing directory must stay an error for it to work as an atomic cross-process mutex. Unreadable-file tier skips as root or when chmod has no effect.
./tests/run_path_split.sh    # `stem(p)` and the path-splitting family (`file_name`, `directory_name`, `extension`), including the invariant that stem plus extension reassembles the file name, a python3 posixpath oracle (skipped without python3), and refusal of interior NULs in all path builtins including both join_path arguments.
./tests/run_intake.sh        # `intake` library: a ledger of which files arrived and were taken, with `identity:` (path, stamp, content) and `ready:` both required, archive/purge, and crash durability via SIGKILL mid-import. The same-size-and-mtime case needs `touch -r` in the runner. `read` on a directory now raises instead of aborting.
./tests/run_csv.sh           # The csv module: csv.records streams a delimited file a record at a time with where each started (line from 1, byte offset), csv.parse reads text, csv.line/csv.write quote only what needs it. Checked against the generator's own record, Python's csv module both ways, flat memory, and every loop exit under --track-fds. Only the big files split a CRLF across the read buffer.
./tests/run_file_path_string.sh # Whole-file verbs (`read`, `write`, `exists`, `delete`, `copy`, `with lock` and the rest) take a path string as `{file}` would, checked against the `{file}` route. `append` alone refuses a string, since it is also the array verb, and the runner reads the disk to show the refusal wrote nothing. Valgrind.
./tests/run_read_at.sh       # `read_at(f, position, count)`: round-trips with `overwrite` at every offset, bounded-memory chunked reading of a large file, and clamped counts, plus AES-GCM chunks appended, `fsync`'d and read back by offset, with strace watching fsync reach the file and its directory. Positions are bytes while `len` counts codepoints, so a chunk loop must advance by bytes. The directory tier needs a directory on the repository's filesystem.
./tests/run_metadata.sh      # File metadata builtins: `permissions`, `chmod`, `set_mtime`, `is_symlink`, `symlink`, plus the directory listing labelling dangling links and FIFOs as not-a-file. `chmod` takes text modes only and refuses numbers. Modes are verified from outside with `stat`; needs mkfifo, and access checks differ as root.
./tests/run_scratch.sh       # `temp_file`, `temp_dir` and `remove_tree`: unique creation with 0600/0700 modes, `in:` for same-filesystem swaps, and a symlink tier built in bash because gBASIC cannot create links. `remove_tree` must never follow a link and refuses a link as its top path. Nothing is removed automatically; the fixture cleans up after itself.
./tests/run_inline_modifier.sh # Inline modifier syntax `{file}path` usable wherever an expression goes, recognized in the lexer to keep grammar conflicts at zero. Checks parity with the assignment clause by type, equality and rendering, precedence like unary minus, token shape, and subject errors reporting the subject's cause. Headless.
./tests/run_optparams.sh     # Literal default parameter values `function f(a, b = 10)`: defaults apply, non-literal defaults and defaults on modifiers, program blocks and server handlers are refused, actor calls fill the tail in the child, and the grammar keeps zero conflicts. New per-parameter-list allocations must be freed in every site.
./tests/run_continuation.sh  # Line continuation inside unclosed `(`, `[` or `{`: token streams equal the one-line form, newlines outside brackets still terminate, brackets in strings or comments do not count, an unclosed bracket at EOF names its opener, and nesting past the 64-entry opener stack is safe under valgrind.
./tests/run_ldap.sh          # The `ldap` module (bind and search only) against a BER-speaking python3 mock, checking invalid_credentials, unreachable and tls_failed are distinct answers, empty passwords are refused, and the password never appears in output. Skips without libldap, python3 or openssl.
./tests/run_smtp.sh          # Mail composition (`stdlib/mail.bas`, pure gBASIC) and the native `smtp` module: self-checking composition, byte-exact golden, a python3 `email` oracle, refusals, and wire checks (CRLF, dot-stuffing, Bcc handling) through a loopback sink. Skips without python3 or libcurl; TLS tiers need openssl.
./tests/run_parse_exit.sh    # A reported parse error must fail the parse: located diagnostics, nonzero exit, nothing run, and a single cause with no follow-on end-of-file message. Includes a source tripwire that the raw fprintf bypassing the diagnostics sink is gone. Headless.
./tests/run_process_lifetime.sh # Nothing the interpreter starts outlives it: kills the parent with SIGKILL and checks that started, abandoned and synchronous children are gone, with a clean-exit control and a count of fork sites that arm PR_SET_PDEATHSIG. Each child carries a per-run marker in argv.
./tests/run_web_handler_errors.sh # A malformed handler response in a `server` block must give a 500, a reason on stderr, and a listener that still answers the next request, across 17 response cases. Also a header-echo CRLF case and a crashing-handler case under `workers: 2`. Needs curl.
./tests/run_insight_calibration.sh # Manual tier, not in the routine gate: measures whether the insight search threshold's stated alpha is delivered, using about 250 searches over generated null data (roughly five minutes), plus a repeated-search family-wise tier. INSIGHT_CALIBRATION_FULL=1 raises the counts.
./tests/run_xml_bigfile.sh   # Manual tier: generates a document of at least 100 MB and checks that xml streaming (skip_to/subtree) keeps peak RSS under a ceiling (64 MiB by default; BOUND_KB and TARGET_BYTES override). Fails if VmHWM cannot be sampled. Invoke directly; it takes tens of seconds.
GBASIC_POSTGRES_TEST=1 PGDATABASE=gbasic_test ./tests/run_postgres.sh # Opt-in PostgreSQL suite (GBASIC_POSTGRES_TEST=1, PGDATABASE=gbasic_test); provision once with `sudo tools/setup_postgres_dev.sh`. Includes native array parameters and results checked with Postgres as the oracle: arrays render by the type Postgres infers, while jsonb parameters and untyped `$1` stay JSON.
GBASIC_SITE_POSTGRES_TEST=1 PGDATABASE=... PGUSER=... ./tests/run_gbasic_site_postgres.sh # Opt-in: runs examples/gbasic_site/site_postgres.bas against a real PostgreSQL, covering the site's data layer, seeded rows, rate limit, CSRF and session expiry via a python3 client. Needs GBASIC_SITE_POSTGRES_TEST=1, PGDATABASE and PGUSER. It backs up and restores examples/gbasic_site/server_port.txt.
```

`gbasic-lsp` is deliberately kept out of the default `all` target, so a routine
test pass must build it explicitly or it will silently rot. Use `make dev`
(builds every binary: `gbasic`, `libgbasic.a`, and `gbasic-lsp`) as the developer/
CI entry point; `tests/lsp/run_lsp.sh` also runs `make gbasic-lsp` itself.

`run_examples.sh` runs a hardcoded list of cases (it does not auto-discover), so
when adding an example test, add its filename to that list as well as creating
the `.out`. To run one case by hand: `./gbasic examples/foo_test.bas` and diff
against `examples/foo_test.out`. Module runners skip rather than fail when their
native dependency is missing. GUI testing is manual (needs a display).

## Conventions

- Optional-dependency code must stay behind its `#if HAVE_*` guard and degrade to
  a runtime error when the feature is compiled out. **And the inverse, which is
  easier to get wrong because the ordinary build hides it**: code that is NOT
  optional must stay OUT of those guards. `with principal` was written next to
  `http_multi`, which is inside `#if HAVE_LIBCURL`, so the whole feature vanished
  from a build without libcurl — caught only by `run_packaging.sh`, which builds
  lean. `make LIBCURL_AVAILABLE=0` (or the matching `HAVE_*` variable) is the
  cheap check before trusting a green gate — **and it no longer needs
  `make clean` in front of it**, which that advice used to carry. No object
  depended on the switch values, so changing one rebuilt NOTHING; a
  `.buildswitch-stamp` is a prerequisite of every object compiled with CFLAGS
  now. That mattered more from 2026-10-11, when `src/builtins.c`'s registry
  became conditional so `has_builtin` would answer for THIS build: a stale
  `builtins.o` made it LIE ABOUT THE BINARY IT WAS IN, which is the defect that
  change was made to remove. Measured — after `make LIBCRYPTO_AVAILABLE=0
  ZLIB_AVAILABLE=0` with no clean, `has_builtin("sha256")` still answered true.
- Design docs in `docs/` may describe unimplemented future work; check the
  implementation-status documents (and `eval.c`) before treating a proposal as a
  shipped feature. `docs/historical_development_archive.md` records completed
  phases.
- The `use`/`--add-uses` syntax is legacy and retained only for compatibility;
  prefer `load`/`--add-loads`.

## House rules

Identical to `AGENTS.md` — the two entry files carry the same rules:

- **Before writing gBASIC code**, read `docs/ai/START-HERE.md` and follow it
  (`UNLEARN.md` first). gBASIC diverges from QBasic/VB intuition in ways that fail
  silently.
- **When you work around a gBASIC limitation or surprise**, append an entry to
  `/DOGFOOD.md` using its template *before continuing*.
- **Evidence standards:** tests-first where feasible; keep goldens byte-exact (a
  behavioral change that moves a golden is a deliberate, listed rebaseline);
  measure, don't assume; report what you could not verify. Never mark anything
  "verified".

## gBASIC Studio

Studio lives in its own project: `~/development/gbasic-studio`. It is not built
here and its libraries are not in this `stdlib`. What DID stay is everything that
was general rather than Studio-specific — `persist` (crash-safe versioned
persistence) and `filetree` (a directory as a value tree), both extracted from
Studio and documented in the cookbook. Studio consumes this repo's platform
surface (`source_outline`, `try_decode`, `process.*`, `--line-buffered`,
`print to error`, `atomic_replace`, the `qt` module and its `window` declarations) and
finds it via `GBASIC_PATH`; nothing here depends on Studio.

## EDGAR suite work
Governed by docs/edgar_suite_development_plan.md. One WP per session,
assigned by Matthew. Follow its §1 session protocol exactly: read only
the WP's Read list, stop at the WP boundary, record verbatim evidence in
docs/PROGRESS.md. Never mark anything `verified`. No network in tests.

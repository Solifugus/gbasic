# Windows port: what is measured, what is built, what is next

**Status: Record.** A handover for a session working on the Windows port, on the
`windows-port` branch. gBASIC is **Linux-only** today — `README.md` says so, and
CI builds Ubuntu 22.04/24.04/current on x86-64 and nothing else.

Everything below was run. The commands are named so the next session can
re-measure rather than trust this page.

---

## 1. The number, and how to get it

```sh
./tools/cross-build-windows.sh
```

**123 errors on the baseline toolchain, MSYS2 UCRT64 gcc 16** (re-baselined
2026-10-02, see §9; it was 82 on Ubuntu's mingw gcc 13, and BOTH numbers are
correct for their compiler). It is a **ratchet**: the count lives in
`tools/cross-build-windows.baseline` with the toolchain it was measured on, and
the script fails if the count rises — or if it is run with a different
toolchain, since counts from two compilers say nothing about each other. It is
NOT wired into `run_all.sh` and is opt-in. Run it from an "MSYS2 UCRT64" shell.

It generates shim headers into a temp directory and throws them away. Some are
honest (`poll.h` → `WSAPoll`), some are deliberate lies (`termios.h`, `regex.h`)
whose only job is to let the compiler get far enough to count real errors.

## 2. What is already built

**`include/platform.h`** (139 lines) with `src/platform_posix.c` (127) and
`src/platform_win32.c` (136). Three Linux-specific mechanisms are behind it,
plus a socket seam:

| | POSIX | Windows |
|---|---|---|
| `gb_exe_path` | `/proc/self/exe` | `GetModuleFileNameW` (wide, then UTF-8) |
| `gb_arm_parent_death` | `PR_SET_PDEATHSIG` | Job Object, `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` |
| `gb_channel_socketpair` | `socketpair(SOCK_SEQPACKET)` | **returns 0 — refuses** |
| `gb_net_init`, `gb_sock_close`, `gb_sock_set_blocking` | no-ops / `close` / `fcntl` | `WSAStartup` / `closesocket` / `ioctlsocket` |

`platform_win32.c` is syntax-checked against a real `windows.h` and has **never
run**. Treat every line of it as unverified.

`tests/portable.sh` makes the harness run where the tools are BSD rather than
GNU; on Linux it defines nothing, and `GB_PORTABLE_FORCE` exercises the
fallbacks.

## 3. What the 82 errors are

- **16 constants in `eval.c`** — the bulk of it, and mechanical.
- `nfds_t`, `struct sigaction`, `mkdir` arity, `WNOHANG`.
- **`%z` in printf at 15 sites (18 warnings)**, which MSVCRT does not take. A
  wrong `Content-Length` is one of these, so it is a correctness bug rather than
  a warning to silence.
- A bundled ERE engine is needed: `regex.h` is one of the deliberate lies.
- A console line editor: `src/lineedit.c` is POSIX termios.

## 4. THE CORRECTION THAT MATTERS MOST

An earlier version of this scoping said the event loop mixes pipes with sockets
and therefore needs IOCP or `WSAEventSelect` plus threads — weeks of work, and
the stated Tier 1 blocker.

**Measured, that is false.** The main loop's pollfd set is the listening socket,
accepted clients, libcurl's transfer fds, and the mailbox only when actors are
in use. **Zero process pipes** — the grep over the region that builds it returns
0. So **`WSAPoll` serves the loop and it needs no redesign.**

The pipes are elsewhere: `process.run` and `process.poll` each poll a child's
stdout/stderr in their own call. Those need a Windows mechanism — overlapped I/O
or `PeekNamedPipe` — but that is **two functions, not an architecture.**

That correction is in `include/platform.h` and in the script's own output, and it
is the reason Tier 1 is a list rather than a research project.

## 5. What the ratchet CANNOT see, and says so

**A socket is not a file descriptor on Windows.** `read`/`write`/`close` on one
compile cleanly and fail at **runtime**, so no error count will ever notice. The
webserver already uses `recv`/`send` and the seam for the rest is `gb_sock_*`,
but every new socket call site is a chance to forget. This is the defect class
to watch, and it cannot be caught by compiling.

## 6. Tier 1, and what is deliberately out

**In:** the 16 constants, the socket seam, `WSAPoll` for the loop, a bundled
ERE, the console line editor, the 15 `%z` sites, pipe polling in two functions.

**Out of Tier 1, deliberately:** actors. `gb_channel_socketpair` **refuses**
rather than faking a channel, because `AF_UNIX`/`SOCK_SEQPACKET` has no Windows
equivalent that preserves message boundaries and fd passing, and a silently
different channel is worse than an absent one. `spawn` should raise a clear
refusal on Windows until Tier 2. **(Done 2026-10-03, §22: a length-prefixed
AF_UNIX stream, handles as paths. `gb_channel_socketpair` still refuses; the
Windows transport does not use a pair.)**

## 7. Two things to decide before writing much

1. **WSL or native?** Native is substantially more work; WSL makes the Linux
   build the Windows story and costs the user a setup step. Matthew has a Windows
   VM and was installing WSL on it. This page assumes native, because that is
   what the ratchet measures.
2. **Which libraries ship?** The release tiers are built for glibc and asserted
   against sonames (`tools/build-release-tarball.sh`). A Windows tarball needs
   its own tier definition and its own floor assertion, and `xlsx`/`xml`/
   `password_hash` are excluded from BOTH Linux tiers already because libxml2 and
   libxcrypt sonames differ across distributions.

## 8. House rules that apply here especially

- **Measure, don't assume.** §4 exists because the first scoping of this port was
  wrong about the single most expensive item, and reading more carefully would
  not have fixed it — only counting did.
- **A check that cannot fail is indistinguishable from one that passes.** §5 is
  the honest statement of what this ratchet does not cover.
- The ratchet count going DOWN is progress and the script says so; update
  `tools/cross-build-windows.baseline` in the same commit that earns the gain.

## 9. First session on a Windows machine (2026-10-02)

Windows 11 Pro 10.0.26200, x64. Toolchains: MSYS2 UCRT64 (gcc 16.2.0, make
4.4.1, bison 3.8.2) natively, and Ubuntu's mingw gcc 13-win32 under WSL.

**The toolchain decision: MSYS2 UCRT64.** The goal is a Windows download that
works as broadly as possible with nothing else installed. UCRT is the C runtime
built into Windows 10 and 11 (and delivered by Windows Update before that), so
a UCRT binary with libgcc linked statically needs no runtime shipped beside it.
MSVCRT is the legacy runtime Ubuntu's cross-compiler targets. The Makefile, the
`tools/*.sh` scripts and pkg-config all work unchanged under MSYS2, and it
packages zlib, libxml2, sqlite3 and libcurl for M3–M5.

**Why the count went 82 → 123, measured on the same tree:**

| | Ubuntu mingw gcc 13 (MSVCRT) | MSYS2 gcc 16 (UCRT) |
|---|---|---|
| errors | 82 | 123 |
| errors with `-fpermissive` | — | 80 |
| `%z` warnings (15 sites) | 18 | **0** |

gcc 14 made implicit declarations, incompatible pointer types and int-conversion
ERRORS by default; that is 43 of the 123 (39 + 3 + 1). They are real port work
(each is a POSIX function or type Windows lacks), merely reported at a different
severity. UCRT's printf accepts `%z`, so §3's fifteen `%z` sites are a defect
only for an MSVCRT build — still worth fixing if MSVCRT is ever targeted, but
not a Tier 1 blocker on this toolchain.

**Three defects in the ratchet itself, all fixed in the script:**
- WSL had mingw but not `make`; make failed with "command not found", the log
  held no `error:` line, and the script reported **"improved 82 -> 0" and
  PASSED**. A run that compiles nothing now FAILS. Proven both ways.
- `make clean` without `PLATFORM_OBJ` left `platform_win32.o` behind, so a
  later broken run could count a stale object as compiled. The script now
  removes objects itself.
- Every run deleted the COMMITTED `src/parser.tab.c`/`.h` via `make clean`; they
  are now kept aside and restored.
- (And under MSYS2's locale gcc quotes with `'` rather than `‘’`, so the
  "distinct causes" list printed EMPTY. Both are matched now.)

**ODBC, §4 of the plan — measured with a standalone C probe, not through gBASIC:**
- All **24** ODBC functions `src/eval.c` calls (inside `#if HAVE_ODBC`; the
  module is in eval.c, not `src/modules/`) **link against Windows' own
  `odbc32`** from the standard `<sql.h>`/`<sqlext.h>`. No unixODBC-specific
  call or header is used.
- The probe RAN: an ODBC 3 environment allocated as eval.c does, and
  `SQLDrivers` listed **3 drivers** (SQL Server, ODBC Driver 17 and 18 for
  SQL Server). `SQLLEN`/`SQLULEN` are 8 bytes, matching 64-bit unixODBC.
- **The 64-bit driver manager has NO Access driver on this machine.** Every
  Access/Excel/dBase/Text driver present is the 32-bit Jet driver. So §4's
  "SQL Server, Access and Excel-as-a-data-source" holds for SQL Server out of
  the box; Access/Excel need Microsoft's Access Database Engine (ACE)
  redistributable installed, or a 32-bit build.
- NOT verified: a real connection, a query, or a round trip. And eval.c uses
  the ANSI (`A`) entry points, which on Windows pass text through the system
  code page rather than UTF-8 — non-ASCII text could be mangled both ways.
  Candidate remedies are a UTF-8 `activeCodePage` application manifest or the
  `W` entry points; neither has been tried.

**Found by reading the code while porting it (none yet measured on Windows):**
- **Time zones.** `zone_push` sets `TZ` to an IANA name and checks
  `/usr/share/zoneinfo/<zone>`. Windows has no zoneinfo directory and its CRT's
  `TZ` takes a different syntax (`EST5EDT`), so every named-zone operation will
  fail or silently use the wrong offset. Candidates: Windows' ICU (`icu.dll`,
  Windows 10 1903+, carries IANA zones) or a bundled tz database. Unresolved;
  the `gb_setenv` seam compiles it, it does not make it correct.
- **`--line-buffered`.** Windows' `setvbuf` treats `_IOLBF` as FULL buffering,
  so the flag would silently do nothing — the MCP stdio deadlock. Needs an
  explicit flush per completed line. Recorded in `include/platform.h`.
- **stdin.** stdout/stderr go to binary mode at startup (`gb_stdio_binary`);
  stdin is left in text mode until something reading it on Windows is measured.

## 10. gbasic.exe builds natively and RUNS (2026-10-02)

From an "MSYS2 UCRT64" shell, `make` builds `gbasic.exe` (lean: every optional
module off). It is statically linked against libgcc/winpthread, so it imports
only Windows' own DLLs — the UCRT (`api-ms-win-crt-*`), KERNEL32, ADVAPI32 and
WS2_32 — and needs nothing installed beside it. The ratchet is at **0**, which
now means "the Windows build compiles clean" and must stay there.

**Measured on Windows 11, natively (not under MSYS2):**
- `tests/windows/smoke.bas`: **20 checks, 0 mismatches** — values, money,
  dates, durations, files, CRLF reading, the interior-NUL path refusal, and
  `process.run`.
- `tests/windows/process_run.bas` (new, cross-platform): **33 checks, 0
  mismatches** — argument quoting (13 hostile arguments round-trip exactly,
  including `C:\Program Files\`), stdout/stderr separately, exit codes, 2 MB on
  both streams at once without deadlock, `env` merge, `cwd`, a timeout that
  ends a 30 s child in ~1 s, and launch failure raised and as a record.
- stdout is LF-only: 0 CR bytes in the smoke run's output (binary mode).

**Proven red, not only green:** with the quoter's trailing-backslash doubling
removed, the ARGV tier first STAYED GREEN — `trailing\` needs no quoting, so it
never exercised the rule. Adding `has space\` and `C:\Program Files\` made the
perturbation fail, and the way it failed is the reason the tier exists: the
broken quoter made one argument SWALLOW THE NEXT, with nothing raised.

**How each POSIX mechanism maps (see include/platform.h and
include/posix_compat.h):**

| | Windows body | status |
|---|---|---|
| `process.run` | `CreateProcessW`, pipes drained on two threads, a Job Object (KILL_ON_JOB_CLOSE) for the timeout and for "nothing outlives the interpreter", an explicit inherited-handle list | **works**, 33 checks |
| `lock` | `LockFileEx` over the whole file | compiles; NOT yet exercised on Windows |
| `real_path`, library "beside the program" | `GetFinalPathNameByHandleW` (resolves links, requires existence) | compiles; NOT yet exercised; answers use `\` and a drive letter |
| close-on-exec | `SetHandleInformation` | compiles |
| prompt Ctrl-C | `signal()` with a re-arming trampoline | compiles; console behaviour NOT measured |
| prompt line editing | none: plain line input, history file kept | works as a plain prompt |
| `process.start/poll/read/wait/stop`, `process.which` | **refused by name** | next |
| `spawn` (actors), `webserver.listen` | **refused by name** | out of Tier 1 |
| regex | TRE through libsystre (BSD-2) | links; run_regex's flag matrix NOT yet run against it |

**What a Windows process.run cannot match, and says so:** there are no signals
(`signal` is always 0; a timed-out child reports `exit_code` -1), and
CreateProcess searches the program's own directory and the current directory
before PATH and appends only `.exe` — a `.bat`/`.cmd` needs `cmd /c`.

**Linux unchanged, checked as a difference** in WSL Ubuntu 26.04 (gcc 15.2):
HEAD and HEAD plus these changes built side by side with identical warnings,
eighteen suites run on both, logs identical bar line numbers, temp names,
timings, one non-deterministic child count and the order of one stderr line.
**Both `.bas` suites above also pass on Linux** (20 and 33 checks), so one file
asserts one behaviour on both platforms. Pre-existing failures on BOTH trees in
that environment, not investigated: run_repl's Ctrl-C tiers (5 to 10 checks,
varying run to run — timing-sensitive on a 2-core machine), run_library_depth
(the chart-library check), run_examples (gui_fields_test), run_negative (a real
server block), run_xlsx (fixtures it cannot read there).

## 11. The Linux suites, run against gbasic.exe (2026-10-02)

MSYS2 ships bash, so on a DEVELOPMENT machine the tree's own bash suites can
drive the native binary unchanged (`./gbasic` resolves to `gbasic.exe`). That is
the same test definitions Linux runs, which is far stronger than new Windows-only
tests. (An end user's machine has no bash; that is what tests/windows/ is for.)
A plain `make` in an "MSYS2 UCRT64" shell now builds the lean static binary:
optional modules are no longer auto-detected on Windows (see the Makefile).

**run_examples' whole list, keep-going:** 181 of 235 pass on Windows. Every
remaining failure is accounted for: 21 SKIP (xlsx/xml/password modules off by
design), 12 actors (Tier 2; each now says "not available on Windows"), 2 named
time zones, and 3 that fail on Linux in WSL too (crypto_kdf, env_builtin,
gui_fields). On Linux the same list gives IDENTICAL status before and after
every change in this section.

**Thirty-four suites run on Windows.** Twenty-three pass outright, among them
the error and warning models, money (146 checks), regex, numfmt, render,
continuation, keyword fields, for-each-index, stridx/arridx/recidx (their
COMPLEXITY tiers included), exponent literal, brace modifiers, parse-exit,
fake, lending, deposits, credit, scoring, chart, notation, insight, decision,
automation, try_decode, outline, persist, library_scope, dir_builtins. The
rest fail only on: actors or process.start (alias, optparams, scope,
equality_kinds, stderr); a module off by design that the suite thinks is
present because MSYS2 has the library installed (string_nul's sqlite door,
accounting's SQLite tier, finio's XML tier); POSIX-only fixtures (/bin/sh,
/etc/hostname, /dev/null, a Python signal probe, `/tmp/...` written INTO a
program, which a native binary cannot open); named time zones; and the two
library_depth failures Linux-in-WSL has too.

**WHAT RUNNING THEM FOUND, each fixed and proven:**
- **`with lock(f)` lost the write inside it, silently.** Windows locks are
  MANDATORY: locking the whole file blocked the program's own write. gb_flock
  now locks one byte at 2^62, past any content, which is flock's advisory
  meaning. examples/lock_test.gb passes.
- **`write`/`append` reported success for bytes that never arrived -- ON LINUX
  TOO.** fclose's result was ignored, and the bytes reach the file in fclose.
  Measured on Linux: writing to /dev/full returned TRUE with exit 0. It now
  raises "could not write file". New tier in tests/run_silent_traps.sh, PROVEN
  RED on the old binary; it asserts the REASON ("No space left on device"), so
  where the binary cannot reach a real /dev/full it says SKIP instead of passing
  on an open failure -- which is what it first did under MSYS2.
- **Non-ASCII file names and arguments were mangled.** "greek_pi_π.txt" was
  created as "greek_pi_Ï€.txt". Fixed for every narrow-string call at once by
  the activeCodePage=UTF-8 manifest (src/gbasic.manifest, Windows 10 1903+).
  process_run.bas's code-page tier PROVEN RED without it (café -> caf�).
  An XML comment with two hyphens in it made the manifest invalid and the
  binary refused to START; the manifest says so now.
- **atomic_replace failed on every real call**: Windows' rename refuses an
  existing target. gb_rename_replace uses MoveFileExW(REPLACE_EXISTING),
  keeping EXDEV for a cross-volume move. nap_fs_test passes; persist too.
- **secure_token / random_bytes failed**: no /dev/urandom. gb_secure_random
  uses BCryptGenRandom.
- **real_path answered `C:\...`**, so file_name(real_path(p)) returned the whole
  path. It answers `C:/...` now; Windows accepts `/` everywhere.
- **make_dir with parents failed on every absolute path**: the walk began with
  mkdir("C:"). It now starts after the root (drive or UNC) and accepts `\`.
- **NaN printed differently**: glibc "-nan", UCRT "nan". The formatter spells
  non-finite numbers itself, in glibc's words, so no Linux output moved.
- **A deep JSON document CRASHED the interpreter** (STATUS_STACK_OVERFLOW):
  Windows' 1 MB main stack is exhausted before the parser's 10,000-level cap
  can refuse it. The binary now reserves 8 MB, the Linux default.
- **TRE has no REG_STARTEND**, so Windows runs regex's FALLBACK path -- the
  one a musl build once got wrong. run_regex's fallback tier passes, and its
  flag matrix passes against TRE.

**Not mine to fix here, flagged instead:** tests/run_core.sh prints
"FAIL sleep(0) returns 0" ON LINUX and still exits 0 -- a stale assertion
(sleep now answers `nothing` by design) behind a gate that does not fail.

## 12. process.start and the live-child verbs (2026-10-02)

`process.start`, `poll`, `read`, `wait`, `stop`, `write`, `close_stdin` and
`release` work on Windows. **tests/windows/process_start.bas: 37 checks, 0
mismatches** on Windows; it is cross-platform like process_run.bas, with
gBASIC itself as the child.

**How:** process.run and process.start share ONE launch (`win_launch` in
src/platform_win32.c: quoting, environment, handle list, job), so they cannot
disagree. The child's pipes are handed back as C-runtime fds, so eval.c's
read/write/close on them are unchanged; four things got Windows bodies:

| POSIX | Windows |
|---|---|
| non-blocking read | `PeekNamedPipe`, then read only what is there |
| `waitpid(WNOHANG)` | `WaitForSingleObject(process, 0)` |
| `poll` over the pipes | a 20 ms pump tick (anonymous pipes cannot be polled) |
| SIGTERM / SIGKILL to the group | `CTRL_BREAK` to the child's own process group / `TerminateJobObject` |

A child this interpreter ENDS reports `exit_code` -1 and `success` false, the
Windows form of "killed by a signal" (`signal` is always 0: there are none).
`listen_fds` is refused by name on Windows.

**PROVEN RED, and the first attempt was not:** with the forced stop disabled,
every stop tier STAYED GREEN -- a gBASIC child dies on the polite CTRL_BREAK,
so `force_after` was never needed. No gBASIC program can refuse the polite
stop, so a Windows-only tier uses `ping`, which answers Ctrl+Break by printing
its statistics and carrying on: it asserts the polite stop does NOT kill it
and `force_after` then does, promptly. With force disabled that tier fails
("and promptly": the stop waited out all 30 pings). On POSIX the same property
is run_process.sh's SIGTERM-ignoring child.

**A released child dies with the interpreter**: after the suite (which
releases a running `sleep 30` child), no gbasic.exe remains -- the job is kept
and KILL_ON_JOB_CLOSE ends it at exit. Measured by process count, not yet a
committed tier.

**`--line-buffered` now works on Windows**, where setvbuf's `_IOLBF` is FULL
buffering: process_start.bas measured a child's first line staying in its
buffer until exit. On Windows the flag makes stdout UNBUFFERED (at least as
prompt; more writes, paid only when asked for). run_stream's mid-run,
partial-line, 20 000-line volume, JSON and opt-in tiers pass on Windows.
run_equality now passes completely (it compares two child handles).

## 13. process.which (2026-10-02)

`process.which(name)` works on Windows, and the whole `process.*` family is now
available there (bar `listen_fds`). Its contract is POSIX's -- "the path
execvp would run" -- whose point is that asking first never disagrees with
running. So the Windows answer is the path **CreateProcess** would run, by
CreateProcess's own documented search (the program's directory, the current
directory, System32, the 16-bit System directory, the Windows directory, then
each `;`-separated PATH entry, quotes stripped), appending ".exe" only when the
name has no extension.

**Deliberately NOT PATHEXT.** It is the tempting choice and the wrong one:
CreateProcess ignores PATHEXT, so a `which` that found `tool.bat` through it
would answer "installed" for a command process.run then cannot launch. A `.bat`
or `.cmd` is reached by naming it, or through `cmd /c`.

**The oracle, in tests/windows/process_run.bas and true on either platform
whatever is installed:** for each probe (cmd, ping, sh, ls, hostname, whoami,
and a name that exists nowhere), `which` finds it EXACTLY WHEN process.run can
launch it -- with a control that at least one probe was found, since an
always-unknown `which` satisfies the agreement on a machine where every probe
fails. PROVEN RED: with the ".exe" rule removed, four probes disagree and the
control fires. 46/46 on Windows.

## 14. Named time zones (2026-10-02)

`now(zone)`, `to_zone`, `from_zone`, `zone_offset` and `zone_resolve` work on
Windows with IANA names, through the ICU that ships with Windows 10 1903 and
later (System32\icu.dll: the full IANA database, kept current by Windows
Update; the same 1903 floor the UTF-8 manifest sets). It is LOADED, not linked,
so a Windows without it still runs gBASIC and only refuses zones -- with a
sentence saying the zone DATABASE is missing, not that the name is misspelled.

**One question, asked of ICU: a zone's offset at an instant.** Instant-to-civil
is the instant plus that offset. Civil-to-instant takes the offsets a day
before and a day after and keeps each candidate whose own offset matches: two
survivors are the repeated fall-back hour, none is the spring-forward gap. The
answers are POSIX's by construction -- ambiguous takes the earlier instant; in
the gap the instant read with the pre-transition offset (02:30 EST, shown as
03:30 EDT). ICU's ucal_open does not fail on an unknown name -- it opens
"Etc/Unknown", which is GMT, glibc's silent fallback again -- so known-ness is
asked separately (ucal_getCanonicalTimeZoneID's is-system flag).

**Measured on Windows:** examples/zone_test.bas (21 checks: EDT/EST, Berlin,
Kolkata's half hour, round trips, the gap, the ambiguous hour) and
datetime_zone_test.bas pass; the datetime cookbook passes 36/36; run_core's
now("Asia/Tokyo") tier passes. PROVEN RED: with the gap choosing the earlier
candidate, "gap shifts FORWARD" fails (01:30 where 03:30 is right).
run_examples' whole list is now **183 of 235** on Windows; what remains is 11
actor examples (Tier 2, each says so) and 3 that fail on Linux-in-WSL too.
(§11 said 12 actor examples; the count is 11.)

Linux keeps its TZ/zoneinfo path; only the two TZ-dance call sites moved into
a helper (`zone_localtime`), unchanged in behaviour.

## 15. Milestone 2: ODBC on Windows (2026-10-03)

**ODBC is built in on Windows by default.** Its driver manager, odbc32.dll,
ships WITH Windows, so linking it costs the download nothing and needs nothing
installed; the drivers are the operator's, as on Linux. gbasic.exe still
imports only Windows' own DLLs (ODBC32 added).

**Run against a real SQL Server** (the local default instance, Windows
authentication, in tempdb) through the native binary, with the tree's own
suites and `GBASIC_ODBC_CONNECTION` / `GBASIC_ESTATE_CONNECTION` /
`GBASIC_ODBC_DRIVER` pointing at it:
- **run_odbc.sh: 39 checks, 0 failed** -- odbc_test's 57 fixture checks
  (BIGINT past 2^53 exact, DECIMAL to the last digit, money through a bound
  parameter, the injection tier, NULL as `nothing`, commit/rollback), the
  13-check refusal fixture, the pinned negatives, the 23-check catalog.
- **run_discovery.sh**: its live tier, 35 checks against the connection.
- **run_estate.sh**: the live tier materialised the estate in tempdb and traced
  it, "13 checks against a real sqlserver".

**NON-ASCII TEXT, ASSERTED BY THE SERVER** (new tests/windows/odbc_unicode.bas,
14 checks; SKIPs without a SQL Server connection). The module uses ODBC's ANSI
entry points, so on Windows text crosses the process code page -- UTF-8 only
because of the manifest. **MEASURED WITHOUT IT: every round trip still read
back EQUAL** while SQL Server stored mojibake ("日本語" as 9 characters, ☃ as
3): the reader undid the writer's mangling, the same silent class FreeTDS
produced on Linux. So the suite asks the server (LEN, UNICODE), not itself.
PROVEN RED: 8 server-side mismatches without the manifest, while the three
read-back checks stayed green -- the reason they cannot be the oracle. 14/14
with it, bound parameters and SQL-text literals alike. The plan's open
question about the ANSI entry points is answered: the manifest settles them.

**One message improved:** Windows' driver manager answers an unknown `Driver=`
with "Data source name not found and no default driver specified" (IM002),
naming nothing, where unixODBC names the driver. odbc.connect now says "no
ODBC driver named 'X' is installed (odbc.drivers() lists those that are)" ahead
of the driver manager's own text -- run_odbc's "refused by name" tier.

**Access, as measured in §9:** a 64-bit gbasic.exe reaches Access/Excel only
with Microsoft's Access Database Engine installed (Windows' own Access driver
is 32-bit). Not tested here.

**Linux, with ODBC compiled in** (WSL Ubuntu with unixODBC and the SQLite ODBC
driver, before = c0e6a1b, after = a4f4f6d): run_odbc.sh 44/44, the ODBC
cookbook 24/24, discovery, estate and the negatives -- all five logs IDENTICAL
before and after. As expected, unixODBC reports an absent driver as 01000 and
already names it, so the new IM002 message never fires there.

## 16. Milestone 3: xml and xlsx on Windows (2026-10-03)

**zlib and libxml2 2.15.4 are built in, STATICALLY**: gbasic.exe (4.7 MB) still
imports only Windows' own DLLs. `LIBXML_STATIC` is required, or libxml2's
headers declare every function dllimport. This is the plan's §4 argument made
real: the Linux tarballs omit xlsx because libxml2's soname varies across
distributions, a problem a static Windows build does not have.

**Measured on Windows:** all 10 xlsx and 8 xml examples pass (run_examples'
list: 203 of 235); run_xlsx passes EVERY tier but one -- the reader discards
nothing (all 11 container entries retained, the unmodelled part byte-identical),
saves are byte-deterministic and validate under an independent `unzip -t`, the
evaluator agrees with every cached value including the LibreOffice-computed
post-2001, text/math and cross-sheet sets, dependency-ordered recalc within and
across sheets, the six in-test fixtures (defined names, intersection, coercion,
residue, arrays, external names), the 40k-row ceiling, every refusal. Also
passing: the xlsx cookbook (30), grid, consolidate, compress (30), finio with
its amount_kind tier now running, the finio cookbook and tutorial, every finio
adapter's main fixture (camt 92, pain.001 37, BAI2 38, NACHA 141, OFX 55), chart's
XML structure tier, ari.

**Two Windows defects running it found, both fixed:**
- **Every date before 1970 failed on Windows.** gmtime_s, localtime_s,
  _mkgmtime and mktime all refuse a negative instant there, so epoch() of a 1950
  datetime RAISED and a pinned 1900 clock read as zero. gb_gmtime/gb_timegm are
  now calendar arithmetic (Hinnant's days-from-civil, exact for every year), and
  gb_localtime/new gb_mktime add the local zone's offset from Windows' ICU --
  which also carries HISTORICAL rules: New York's 1950 daylight time comes out as
  on Linux. A pre-1970 probe (epoch local and UTC, to_zone, from_zone,
  zone_offset, 1969 + 1 second, a 1900 date) now prints the same as Linux line
  for line.
- **TZ=Area/City was silently misread.** Windows' C runtime takes only POSIX
  TZ syntax; an IANA name gave the system's own time with no complaint. Local
  time now honours a TZ that names a zone ICU knows, as Linux does (measured:
  TZ=Australia/Sydney gives UTC+10, TZ=Asia/Tokyo UTC+9). An unknown TZ is
  ignored in favour of the system zone. Ambiguous and skipped local times resolve
  as zone_resolve does.

**Harness facts, not gBASIC defects, all measured:**
- MSYS2 REMOVES `TZ` when it launches a native program, so run_xlsx's Sydney
  tier and run_nap_fs's mtime tier (both set TZ) cannot pass under MSYS2 bash;
  natively the same TZ is honoured. **CORRECTED in §17, both halves:** MSYS2
  removes only a TZ the C runtime cannot parse (`Australia/Sydney` is removed,
  `UTC` arrives), and run_nap_fs was a REAL defect -- file times shifted by TZ.
- A suite that writes a `/tmp/...` path INTO a program fails because a native
  binary cannot resolve it. Running the suites with `TMPDIR` in `C:/...` form
  (which bash and the binary both understand) cures it.
- The finio generators and reference scripts are Python writing in text mode,
  which on Windows emits CRLF: "fixtures drifted" and BAI2's "disagreement" over
  identical text are that.
- `chmod 000` has no effect on Windows (NACHA's unreadable tier, run_core's
  permission tier), and Windows reports a path through a file as "No such file",
  not "Not a directory". (The second is now gBASIC's to answer, and it does --
  §17.)

**Still open, in order:** the suites a partial sweep found failing on a build
refusal (agent, ari_advisor, doc_examples, event_study) and a full sweep for the
rest; M4 (sqlite); M5 (libcurl).

## 17. The full suite sweep (2026-10-03)

**Every `tests/run_*.sh` against gbasic.exe under MSYS2.** The first
sweep: 99 of 158 exited 0. After this round: 157 of 159 (the 159th is the new
run_windows_suite.sh), then 159 of 159 once the two below were dealt with. What remains red is listed at the end,
each with its reason.

**Four Windows defects the sweep found, all fixed:**
- **A program whose only event source is a timer died on its first iteration**
  with "webserver poll failed": WSAPoll refuses an EMPTY set (poll() waits out
  the timeout) and reports through WSAGetLastError, never errno. poll() is a
  wrapper now (include/posix_compat.h). run_timer: 8 failures -> 0.
- **An extracted install resolved NO library.** gb_exe_path answered with
  backslashes, so the stdlib-beside-the-binary rule found no separator to walk
  back from -- the one thing an installer must not get wrong. Forward slashes
  now, as gb_realpath. run_relocatable passes every tier but the actor one.
- **file_mtime was shifted by TZ.** The C runtime COMPUTES st_mtime, through the
  system zone and back through TZ, so TZ=UTC on a UTC-5 machine read a file
  stamped 2020-01-01T00:00Z as 1577818800 -- five hours early -- while gBASIC's
  own local time honoured TZ. New gb_file_mtime reads the UTC FILETIME from a
  handle. Measured: epoch 1577836800 under TZ unset, UTC and Asia/Tokyo.
- **The session cache, and the history file, were written with CRLF** (open()
  defaults to text mode on Windows). GB_O_BINARY at both opens; run_repl's
  cache tier went red on it and green after.

**And one message made right:** a write or read through a path whose parent is
a plain FILE said "No such file or directory" -- Windows reports both as path
not found -- where DOGFOOD 35's whole point is that the two need different
fixes. An ENOENT whose nearest existing ancestor is not a directory is now
reported as "Not a directory" (path_open_errno; unchanged on POSIX).

**One build defect:** eval.o, main.o, repl.o, actor.o and lineedit.o did not
depend on include/platform.h (nor eval.o on posix_compat.h), so editing either
header REBUILT NOTHING -- found when the poll fix produced an identical binary.
It affects Linux equally.

**Gates that could not tell "absent" from "broken" -- the bulk of the sweep.**
tests/build_has.sh now answers, by asking the binary, for: gi; the libcrypto
builtins; and three PLATFORM capabilities -- `actors`, `listen` (a probe that
never binds) and `signals`/`lineedit` (absent on Windows). About forty suites
gate on it, whole-suite where the subject is the capability and per tier where
other tiers still run; every skip names what is missing and why. Two of the
pre-existing inline probes (run_http, run_smtp) could NEVER skip, for reasons
that have nothing to do with Windows: `./gbasic probe | grep -q` under pipefail
takes gbasic's exit 1, and a probe written to a fixed /tmp path is unreadable
by a native binary.

**MEASURED ON LINUX, AND THIS IS THE STRONGER RESULT:** the WSL tree is a LEAN
build (no libcurl, libcrypto, sqlite, pg, GI), and before this round 19 suites
FAILED there for exactly the reasons Windows did -- run_examples stopped at
gui_fields, run_negative, run_library_depth, run_native_workbench, every
llm-loading suite. After it, all 19 pass or skip by name, and no suite moved
the other way (57 suites compared base against new; pass counts identical
wherever base was green, run_repl's 137 included).

**Harness defects fixed on both platforms** (each was green only on a machine
with gBASIC INSTALLED, or only by accident):
- run_library_depth built GBASIC_PATH as `$PWD/../../../stdlib` with PWD already
  inside mktemp -- `/stdlib`, which exists nowhere; run_examples' gui_fields and
  run_negative's server-block control loaded stdlib libraries with no
  GBASIC_PATH at all.
- run_web_routes' coverage floor now falls by the MEASURED size of a tier
  skipped by name (2 and 11), and by nothing else.
- run_process_lifetime passed for the WRONG REASON under MSYS2: its `kill -9`
  ends a native process's descendants itself, so the tiers stayed green against
  a build with the Job Object's kill-on-close REMOVED. It kills with a bare
  TerminateProcess now; that perturbation goes red, the real build green.
- tests/windows/*.bas were the Windows gate and NOTHING RAN THEM.
  tests/run_windows_suite.sh discovers them by glob, on every platform; with the
  local SQL Server it passes odbc_unicode (14), process_run (46),
  process_start (37) and smoke (20).
- run_process's fixtures are POSIX by design (#! helpers, /tmp, `which sh`);
  where the binary cannot exec a #! script they are skipped BY NAME and
  run_windows_suite covers process.* instead. Its env tier measured MSYS2's sh
  (which re-creates HOME), not gBASIC: a native child proves the unset works,
  and the fixture now unsets a variable the runner exports, with a control.
- run_repl drove live sessions through a FIFO, and a native program reads
  NOTHING from an MSYS2 FIFO; it uses a coprocess (a pipe) now. The cache tiers
  -- a killed session leaves its program, `recover` restores it as entries --
  pass on Windows as a result, and on Linux with the same 137 checks.
- Python's text mode (CRLF) in four fixture generators, run_doc_examples'
  manifest and the BAI2 oracle; `grep $'\r'` under MSYS2 (which strips CR).

**Corrected harness facts (§16 was wrong on both):** MSYS2 removes a TZ only
when the C runtime cannot parse it -- TZ=UTC reaches the binary,
TZ=Australia/Sydney does not (tests/tz_seen.bas lets a suite ask). And /proc
under MSYS2 is Cygwin's emulation, which cannot see a native process, so
gb_have_proc answers no there.

**UNVERIFIED on Windows, and said so rather than claimed:**
- Ctrl-C at a Windows prompt interrupting a runaway loop: the tier sends POSIX
  signals down a FIFO, neither of which reaches a native console program.
- web.static containment against Windows symlinks and junctions: native
  symlinks need Developer Mode, and MSYS2's `ln -s` makes a copy, so the tier
  has no premise here (moot until the listener exists).
- The line editor does not exist on Windows (raw_on declines; the prompt reads
  plain lines) -- a known gap, its tiers skipped by name.

**Still red under MSYS2:** nothing. The re-sweep left two, and neither is
a Windows defect: run_docs_gate counted 159 suites against the 158 README.md
and site.bas stated (this round added one; updated), and run_stridx's ASCII
forward-scan shape case measured 15x across a 4x step ONCE -- re-run three
times it measured 1.97x, 2.26x and 2.11x against its 8x gate, so that was a
timing outlier on a busy machine, recorded here rather than called green.

**Still open, in order:** M4 (sqlite); M5 (libcurl, which brings webclient,
http, smtp and every llm-loading suite back); actors (a length-prefixed stream
channel -- §6); the listener; the Windows line editor.
## 18. Milestone 4: sqlite on Windows (2026-10-03)

**sqlite 3.53.4 is built in, STATICALLY** (`-lsqlite3 -lz`; its header declares
no dllimport, so no _STATIC define the way libxml2 needs one). gbasic.exe is
6.4 MB and still imports only Windows' own DLLs (objdump: the UCRT api-ms-win-
crt set, KERNEL32, ADVAPI32, bcrypt, ODBC32, WS2_32). The library was already in
MSYS2 as a Python dependency; nothing was installed.

**Measured on Windows, all exit 0:** run_sqlite (every tier, the integration
fixture and its ten pinned refusals, the statement-note tier), run_dbframe (the
whole xlsx -> grid -> consolidate -> sqlite pipeline, the injection tier),
run_compile (the formula compiler's interpreter/SQL/frame three-way oracle),
run_string_nul (its sqlite door now runs: an interior NUL survives both ways,
asked of the database), run_accounting, the xlsx cookbook (36, recipes 11-12 no
longer skipped), run_doc_examples (its sqlite block now runs), run_negative,
run_capabilities, run_platform. run_examples: 219 passed, up from 216, with no
sqlite skip left -- and the WebClient skips rose 7 -> 13, because the edgar and
screener examples now get past sqlite to their next dependency, which is M5.

The skips run_discovery and run_odbc_cookbook still report are the SQLite ODBC
DRIVER (a separate installable those M2 tiers use), not this module.

Linux is untouched by construction: the change is inside the Makefile's
Windows-only block. Ratchet still 0.

**Next:** M5 (libcurl -- webclient, http, smtp and everything that loads
`llm`), then actors, the listener, and the Windows line editor.
## 19. No LGPL code in gbasic.exe (2026-10-03)

**Found while scoping M5, and true since M3:** the static gbasic.exe contained
two LGPL-2.1+ libraries -- libiconv (pulled in by libxml2's encoding.o and by
libintl) and gettext's libintl (92 symbols, pulled in by TRE's regerror.o for
translated regex messages). LGPL permits static linking only if every release
lets users relink against a modified library, an obligation an installer would
carry forever. **Removed (Matthew's ruling).** Everything else linked is
permissive: OpenSSL is not linked (it is since §21, Apache-2.0), libxml2 MIT, sqlite public domain, zlib,
TRE BSD-2, libsystre BSD-2.

`nm -u` says exactly what was used: libxml2 takes libiconv_open, libiconv and
libiconv_close; TRE takes libintl_gettext. src/platform_win32.c now supplies
those four -- gettext returns its argument (what TRE got with no catalogue
anyway), and iconv converts one character at a time over the Windows code
pages, honouring the contract: a partial character is EINVAL, no room E2BIG,
invalid or unmappable EILSEQ, and an encoding that is stateful or has
four-byte characters is refused at open. MSYS2 no longer packages win-iconv,
so this is gBASIC's own code, not a substitute library.

**tests/windows/xml_encodings.bas** (run by run_windows_suite on every
platform) checks Shift_JIS, windows-1252, KOI8-R, GBK, Big5 and EUC-KR against
codepoints taken from the encoding STANDARDS, not from either implementation:
14 of 14 on Linux (glibc iconv), on the old Windows binary (libiconv) and on the
new one. Two perturbations go red -- iconv refusing everything, and a partial
character swallowed instead of handed back -- and the second needed the tier
rebuilt first: MEASURED, xml.parse converts an in-memory document in ONE call,
so a long document through it never splits a character and the swallowing
converter passed. The tier streams a ~200 KB file through xml.reader instead.

After: the link line has no -liconv or -lintl, nm finds none of either
library's internals, and run_regex, run_xlsx, the camt and pain.001 adapters,
the xlsx cookbook, grid, chart, run_examples and run_negative all exit 0.
## 20. Milestone 5: libcurl on Windows, over Schannel (2026-10-03)

**libcurl 8.22.0, built by tools/build-curl-windows.sh**: pinned version and
SHA-256 (cross-checked against MSYS2's own recipe for the same release, which
records the identical hash), configure-only (the release tarball ships its
configure, so no autotools), static, TLS = **Schannel**. Protocols HTTP(S) and
SMTP(S) -- plus WS/WSS, which curl enables by default at no dependency cost;
`file:` is off. Dependencies: zlib and Windows' own libraries -- no OpenSSL, no
LGPL. The Makefile switches libcurl on exactly when
~/gbasic-deps/curl-schannel/lib/libcurl.a exists, so a machine that has not run
the script still builds, with webclient/http/smtp refusing cleanly.

gbasic.exe: 6.0 MB, importing only Windows DLLs (adds CRYPT32, IPHLPAPI,
Secur32). Smaller than at M4 because §19 removed the LGPL libraries.

**TLS TRUST IS THE WINDOWS STORE, measured:** a real https:// fetch returns 200
with the chain verified; a certificate for the WRONG NAME is refused
(SEC_E_WRONG_PRINCIPAL) and a SELF-SIGNED one is refused (SEC_E_UNTRUSTED_ROOT).
Nothing in gBASIC sets a CA path (only ldap has the option, and ldap is off),
so Schannel's default trust is what applies.

**Measured on Windows, all exit 0:** run_webclient; run_http (SEMANTICS 31,
CONCURRENCY 1824ms sequential vs 639ms concurrent, DELIVERY, NO_WATCH, IGNORED,
RAISE/http, WARN); run_smtp (11: wire framing and dot-stuffing, auth, reject,
TLS and STARTTLS with verification ON by default, and a STARTTLS downgrade
refused); run_mcp (17 over the stdio transport -- what a desktop MCP client
uses); run_agent (45), run_llm_transcript, run_market (44 fixture assertions),
run_event_study, run_json_strict, run_nlq (112), run_tools, run_ari_advisor,
run_libcurl_floor, run_doc_examples, run_examples, run_negative.
Still skipped by name: the tiers that need a LISTENER (http's RAISE/server and
DEFERRED, mcp's HTTP transport, steward) -- that is the listener milestone.

**Harness defects fixed, several not Windows-specific:**
- A native Windows python ends its output `\r\n`: run_http's fixture-server
  port became "12345\r" and every URL built from it was malformed.
- tests/mcp/client.py used select() on a pipe (Windows accepts only sockets:
  WinError 10093) and text mode (a blank line arrived as "\r"); it uses a reader
  thread and bytes now, unchanged in meaning.
- run_mcp passed MCP_URL="" when no server was up, and the fixture's
  is_string() is true for an empty string -- so on ANY machine without a server
  it connected to an empty URL. Set only when there is one.
- run_http's and run_ari_advisor's valgrind tiers never asked vg_available, so
  they FAILED on any machine without valgrind; hidden until now because both
  suites skipped whole on builds without libcurl.
- run_smtp's certificate step: MSYS2 rewrote `-subj /CN=...` into a path for the
  native openssl (MSYS2_ARG_CONV_EXCL on that one call).

**NOT VERIFIED ON LINUX:** the WSL tree is a lean build without libcurl, so the
suites changed here skip there whole -- in particular tests/mcp/client.py's
rewrite has run on Windows only.

## 21. Crypto builtins and password hashing (2026-10-03)

**The choice, measured first (Matthew's ruling: static libcrypto).** The two
options were OpenSSL's libcrypto, linked statically, and Windows' own CNG
(bcrypt.dll):

- **libcrypto** was measured with a trial build, using MSYS2's
  `openssl 3.6.5` archive (nothing downloaded). It linked first time, and every
  run_crypto case plus run_otp passed. The cost is SIZE: stripped, gbasic.exe
  goes from 4.5 MB to 9.3 MB, because OpenSSL 3 pulls in its provider layer for
  the thirteen builtins we use.
- **CNG** was not built. Per Microsoft's documented algorithm list it has
  SHA-1/256/512, MD5, HMAC, AES-GCM and PBKDF2, but no Ed25519 and no scrypt.
  That would mean a second implementation of every builtin plus two vendored
  libraries, all kept in agreement with Linux.

A slimmer, pinned OpenSSL build (like curl's) may recover much of the 5 MB.
That has not been measured. libssl stays off: it serves only the webserver's
TLS, which needs the listener.

**password_hash / password_verify: vendored yescrypt (Matthew's ruling).**
Linux uses libxcrypt, which is LGPL and therefore excluded by §19. OpenSSL has
no yescrypt. Openwall's yescrypt 1.1.0 is vendored unmodified in
third_party/yescrypt (BSD-2; provenance and SHA-256 in its README). Every
vendored file is byte-identical between openwall.com and the GitHub tag. It is
built only where libxcrypt is absent (`HAVE_YESCRYPT`), so Linux is unchanged.
Both backends sit behind one pair of helpers in src/eval.c, so each call site
has a single path. Its `SHA256_*` symbols are renamed `libcperciva_*` and do not
collide with OpenSSL's in the same static link.

A hash is DATA, so the two backends must agree. The vendored path writes exactly
libxcrypt's default: `$y$j9T$` (N=4096, r=32, p=1) over 16 salt bytes.
**tests/windows/password_interop.bas** (17 checks) carries one hash made by
each backend, and was checked from OUTSIDE gBASIC with perl's crypt() over the
system libxcrypt. Results:

- Windows: 17/17.
- Linux on libxcrypt: 17/17. This is libxcrypt verifying the vendored output.
- Linux built with `make YESCRYPT_VENDORED=1 LIBXCRYPT_AVAILABLE=0`: 17/17, with
  no libcrypt linked.

Three perturbations go red: a different cost, the `$6$` refusal removed, and the
NUL refusal removed.

Two decisions:

- The vendored backend cannot read `$6$`/`$2b$` hashes. For a WELL-FORMED hash
  of another scheme it RAISES, naming the scheme. Answering false would tell the
  caller the password was wrong and lock a real user out with nothing said.
  Anything that is not a crypt hash at all (an empty column, garbage) still
  answers false, as libxcrypt does.
- **Found and fixed on BOTH platforms:** crypt(3) takes a C string, so a
  password containing NUL was hashed only up to the NUL. "a\0xyz" hashed as "a"
  and then verified for "a\0anything". Both functions now refuse such a password
  (the PLAT-NUL rule for a door that cannot hold the byte).

gbasic.exe is now 11.9 MB unstripped and still imports only Windows DLLs.

**The full sweep (tests/run_all.sh, Windows), 159 suites:**

- First pass: 118 passed, 11 failed, 28 skipped whole (each by name), 2 manual.
- 9 of the 11 failures were the RUN, not the code. The sweep was started without
  `TMPDIR` set to a Windows path (§17's sweep set it), so gbasic.exe was handed
  `/tmp/...` paths it cannot resolve. Rerun with it, all 9 exit 0.
- The other two were real, both harness, and both caused by this change:
  - run_docs_gate requires every `HAVE_*` the Makefile probes to appear in the
    README's dependency table. HAVE_YESCRYPT is vendored, so the libxcrypt row
    now names it.
  - run_regex's fallback tier rebuilds a copy of the tree, and the copy left out
    third_party/, which the Windows build now compiles.
- Final: 129 pass, 28 skip by name, 2 manual. run_docs_gate, run_regex and
  run_windows_suite also exit 0 on Linux with this change applied.

**NOT VERIFIED ON LINUX:** the libcrypto change itself. The WSL tree has no
OpenSSL development files, so run_crypto and run_otp skip there, as before.
The change touches only the Makefile's Windows block.

## 22. Actors on Windows (2026-10-03)

`spawn`, `send`, `receive`, `self()`, monitors, handle passing and
`watch(inbox.messages)` now work on Windows. §6 refused them because the
transport's three properties come from one Linux socket type. Each is rebuilt
in src/actor.c rather than approximated:

| Property | Linux | Windows |
|---|---|---|
| one send is one whole message | SOCK_SEQPACKET | `[u32 length][u32 handle count]` prefix, reassembled per connection |
| senders never interleave | the kernel queues datagrams | the inbox is an AF_UNIX LISTENER and each sending PROCESS has its own connection, so no connection has two writers |
| a handle can travel in a message | the descriptor, via SCM_RIGHTS | the inbox's PATH, attached to the frame; the receiver connects itself |

There is no DuplicateHandle, because a path needs nothing duplicated. All the
handles one process holds to one inbox share one connection, which keeps what
one process sends to one actor in sending order, as on Linux. An inbox lives in
the user's own temporary directory (`gbasic-actor-<pid>-<n>-<random>.sock`),
which other users cannot open, and is deleted when it closes.

**Measured before writing it** (a C probe against Windows 11 26200):

- a client may connect before the listener accepts;
- a non-blocking send was accepted WHOLE or refused with nothing written, up to
  8 MiB. A partial send is still handled by finishing the frame, because
  nothing documents that property;
- an actor that EXITS or IS KILLED shows POLLHUP on every connection to it,
  which is the monitor signal;
- a send to a closed peer REPORTS SUCCESS once, so a send checks for the
  hang-up first;
- the socket FILE outlives its process, so the parent deletes a killed child's.

**What differs, and is reported rather than hidden:**

- The largest message is a fixed 4 MiB (Linux: about 104 KiB, derived from
  SO_SNDBUF). Both exceed the 64 KiB floor the reference promises.
- A monitor's death reason cannot tell `killed` from `error`. A Windows process
  has an exit code and nothing records whether it was terminated, so both are
  `error`. `normal` is exact.
- A child is a process handle in a Job Object with KILL_ON_JOB_CLOSE, in its own
  process group (CREATE_NEW_PROCESS_GROUP, as Linux uses setpgid). Teardown
  ends each child's job, the form of Linux's SIGTERM to the actor group.

**tests/windows/actor_transport.bas** (12 checks on Windows, 11 on Linux) attacks what
was rebuilt rather than the features the examples already cover:

- four senders at once, with self-describing payloads of 2–24 KB, so a split,
  spliced or reordered frame cannot pass;
- a handle passed at RUNTIME to an actor that never saw its target;
- 1 MiB carried whole (Windows) or refused by size (Linux);
- 16 MiB refused on both;
- a send to an actor that has gone must raise.

Three perturbations of src/actor.c were run, and two went red. "Frames
delivered newest first" passed, and that is the transport rather than a blind
test: the inbox reads a new frame only when its queue is empty, so it never
holds two to reorder. The SENDING-ORDER check has NOT been proven red by a
clean perturbation; the one attempted also broke spawn's startup. Order holds
by construction, because each sender writes one stream.

- No hang-up check before a send: red ("a send to it raises").
- A received handle bound to the wrong inbox: red (6 checks).

**Found by the actor tiers, which had always skipped on Windows:**

- **A real bug:** `load lib from "C:/…"` was treated as RELATIVE and joined
  onto the loading file's directory (`C:/x/C:/Users/…`). Absolute means a
  drive path or a rooted path on Windows now (`path_is_absolute`, src/eval.c).
- **Three harness gaps:**
  - run_alias wrote MSYS2's `/c/…` `$PWD` into a program (now `cygpath -m`).
  - run_repl's "no cache can be written" used `/proc/…`, which MSYS2 rewrites
    to a directory Windows will create (now a directory under a regular file).
  - run_inbox's warn tier needs a LISTENER (now skipped by name without one).

**Results.**

- Windows, tests/run_all.sh: **130 passed, 0 failed, 27 skipped by name, 2
  manual.** §21 was 129/28. Every actor tier that had skipped now runs. What
  is left skips for the listener (next), for modules not built here
  (postgres, ldap, gi), or for no valgrind.
- run_examples runs all 11 actor examples (245 pass).
- Linux (WSL), 14 suites on HEAD and on HEAD+patch: all exit 0, and compiler
  warnings are identical (20 and 20). run_inbox's valgrind tier is clean on
  the patched tree.

On Linux, run_repl failed on UNPATCHED HEAD in one run, because files it had
just made in WSL's `/tmp` disappeared mid-run. With a private TMPDIR both trees
pass 147/147. That is recorded as the machine, not the code.

**Not verified:**

- A Windows actor that is KILLED by something outside gBASIC. Its exit code is
  `error`, by design, and that path was not exercised.
- A temporary directory long enough to overflow AF_UNIX's 108 bytes. The
  refusal names ENAMETOOLONG, but no such directory was tried.
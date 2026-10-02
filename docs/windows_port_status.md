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
refusal on Windows until Tier 2.

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

**Still open, in order:** `process.start` and friends; `process.which` with `;`
and PATHEXT; `--line-buffered`; named time zones; making suites tell a module
that is installed from one that is BUILT IN; the optional modules (M2 ODBC
first).

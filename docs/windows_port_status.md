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

**Still open:** nothing here has RUN gBASIC on Windows yet — §5 stands.

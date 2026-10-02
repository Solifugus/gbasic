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

**82 errors**, unchanged by 0.4.0's work. It is a **ratchet**: the count lives in
`tools/cross-build-windows.baseline` and the script fails if the count rises,
so the port can be done in increments without a regression going unnoticed. It
is NOT wired into `run_all.sh` — it needs `x86_64-w64-mingw32-gcc` (13-win32
here) and is opt-in.

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

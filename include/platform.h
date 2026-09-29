/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * platform.h -- the Linux-specific surface of gBASIC, in one place.
 *
 * WHY THIS FILE EXISTS. Before it, there were ZERO platform conditionals in the
 * tree: the only `_MSC_VER` in the repository is inside bison's generated
 * parser. Linux was not a supported target, it was an assumption, spread through
 * a 39,000-line eval.c. Measured against the source, the Linux-specific surface
 * is THREE MECHANISMS -- which is small enough that keeping them in one file is
 * worth more than the file costs, and small enough that a port is a day's work
 * on the mechanisms rather than a month of `#ifdef` archaeology.
 *
 * It is extracted BEFORE either port and with only the Linux bodies written, so
 * the gate proves the extraction changed nothing. That is the whole point of
 * doing it first: a refactor whose correctness rests on a machine nobody here
 * owns is not a refactor, it is a guess.
 *
 * WHAT IS DELIBERATELY NOT HERE. Everything already portable POSIX stays where
 * it is. The event loop uses poll(), not epoll -- usually the thing that blocks
 * a port and already fine. fork+exec, waitpid, fcntl, flock, termios, dirent and
 * the sockets are POSIX and work on macOS unchanged; on Windows almost none of
 * them do, and that is a Tier-2 problem this file is the right shape for rather
 * than the thing it solves today.
 *
 * WHAT A PORT MUST ANSWER. Each function below states the Linux mechanism, the
 * property RELIED ON, and what is unresolved elsewhere. The property matters
 * more than the call: a substitute that provides the call and not the property
 * is how a platform port produces an ordinary-looking wrong answer.
 */
#ifndef GBASIC_PLATFORM_H
#define GBASIC_PLATFORM_H

#include <stddef.h>
#include <sys/types.h>

/* The path of the running executable.
 *
 * LINUX: readlink("/proc/self/exe").
 * RELIED ON: the answer cannot change during a run, and it is the REAL binary
 *   even when invoked through a symlink or a relative path -- which is what
 *   makes the released tarball relocatable, asserted by the build
 *   ("it finds its own stdlib after being moved") and by run_relocatable.sh.
 * macOS: _NSGetExecutablePath(), which may return a path needing realpath().
 * Windows: GetModuleFileNameW().
 *
 * Writes a NUL-terminated path into buf. Returns 1 on success, 0 on failure --
 * and a caller must handle 0, because a failure here is not fatal: the stdlib
 * also resolves via GBASIC_PATH and the compiled-in install prefix.
 */
int gb_exe_path(char *buf, size_t size);

/* A socket pair for actor mailboxes.
 *
 * LINUX: socketpair(AF_UNIX, SOCK_SEQPACKET).
 * RELIED ON, AND THIS IS THE LOAD-BEARING ONE: message boundaries are preserved,
 *   so ONE READABLE EVENT IS EXACTLY ONE WHOLE FRAME and sendmsg is
 *   all-or-nothing. The GI bridge's mailbox source and the `watch(inbox.messages)`
 *   delivery path both rest on it; without it a reader can see half a message and
 *   nothing says so.
 * macOS: has no SOCK_SEQPACKET on AF_UNIX. SOCK_DGRAM is the candidate because it
 *   also preserves boundaries -- BUT THAT MUST BE MEASURED, not assumed, and the
 *   all-or-nothing send property measured separately from the framing one.
 * Windows: AF_UNIX is stream-only, so the property cannot be bought from the
 *   socket at all and the channel layer needs an explicit length prefix, which
 *   src/actor.c does not have today.
 *
 * Returns 1 on success, 0 on failure.
 */
int gb_channel_socketpair(int sv[2]);

/* Arm "this process dies when its parent does", at the kernel level.
 *
 * LINUX: prctl(PR_SET_PDEATHSIG, SIGTERM).
 * THE PROMISE THIS KEEPS is docs/reference.md's, in bold: "nothing this
 *   interpreter started outlives it". A userspace sweep at teardown cannot keep
 *   it, because a SIGKILL never reaches the sweep -- which is how four gBASIC
 *   children were once found still sleeping two days after the runs that started
 *   them. run_process_lifetime.sh tests it from the observable side.
 * macOS: HAS NO EQUIVALENT. kqueue EVFILT_PROC/NOTE_EXIT is the mechanism and it
 *   needs a watcher, so this is A DESIGN DECISION rather than a substitution. On
 *   macOS this is a no-op, so run_process_lifetime.sh SHOULD GO RED there -- the
 *   promise genuinely is not kept yet, and a gate that said otherwise would be
 *   the lie this codebase spends most of its effort avoiding.
 * Windows: a Job Object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
 *
 * DELIBERATELY NARROW: the caller between fork and exec ALSO re-checks
 * getppid() against the pid it recorded, for the race where the parent died
 * before the arming landed. That check is pure POSIX and stays with its caller
 * -- a platform file holding portable logic is a platform file people stop
 * trusting to be only about platforms.
 *
 * Returns nothing: a forked child three lines from exec can do nothing useful
 * with a failure.
 */
void gb_arm_parent_death(void);

/* ---------------------------------------------------------------------------
 * THE SOCKET SEAM.
 *
 * On POSIX a socket IS a file descriptor and every one of these is a no-op or a
 * one-liner. They exist because on Windows a socket is a SOCKET, not an fd:
 * `close()` on one leaks it, and `fcntl()` does not apply. Neither mistake is
 * visible to a compiler -- both fail at RUNTIME while building cleanly -- so the
 * seam is introduced on POSIX, where the gate can prove it changed nothing, the
 * same way the three mechanisms above were.
 *
 * THE PART THAT IS NOT A WRAPPER, AND IS THE REAL WINDOWS BLOCKER:
 *
 *   `WSAPoll` TAKES SOCKETS ONLY. gBASIC's event loop polls a MIXTURE -- the
 *   listening socket, accepted clients, libcurl's transfer fds, the actor
 *   mailbox, and `process.start`'s stdout/stderr PIPES. A pipe cannot go in a
 *   WSAPoll set at all, so on Windows this loop does not merely need a renamed
 *   call, it needs a different SHAPE: IOCP, or WSAEventSelect plus
 *   WaitForMultipleObjects with the non-socket sources fed by threads.
 *
 *   This is the Tier 1 design decision, it is invisible to
 *   tools/cross-build-windows.sh (the error count will never mention it), and it
 *   is what decides whether a Windows port is weeks or months. It is written
 *   here rather than discovered later.
 *
 * Each returns 1 on success and 0 on failure.
 */
int gb_net_init(void);                        /* WSAStartup; nothing on POSIX */
int gb_sock_close(int fd);                    /* close vs closesocket */
int gb_sock_set_blocking(int fd, int blocking); /* fcntl vs ioctlsocket FIONBIO */

/* Which platform the bodies above came from, for diagnostics and for the tests
 * that need to say why a tier does not apply. */
const char *gb_platform_name(void);

#endif /* GBASIC_PLATFORM_H */

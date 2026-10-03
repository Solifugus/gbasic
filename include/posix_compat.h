/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * posix_compat.h -- the system headers that exist on POSIX and not on Windows,
 * in one place.
 *
 * ON POSIX THIS IS EXACTLY THE INCLUDE LIST src/eval.c HAD BEFORE IT, and
 * nothing more, so the Linux build is unchanged by construction.
 *
 * ON WINDOWS it supplies what is genuinely equivalent -- Winsock for the
 * socket headers, WSAPoll for poll() -- and NOTHING that only pretends.
 * tools/cross-build-windows.sh writes deliberate lies (an empty termios.h, a
 * regex.h that does nothing) so a probe can count errors; those must never be
 * what a real binary is built from, because the result COMPILES AND DOES NOT
 * WORK.
 *
 * THE ONE KIND OF STAND-IN ALLOWED: one that FAILS TRUTHFULLY. Below, fork()
 * fails with ENOSYS and waitpid() answers "no such child" -- both TRUE on a
 * Windows build that cannot start a child this way -- so the existing error
 * paths report them. None of them ever reports a success it did not achieve.
 * Their callers are also refused EXPLICITLY at the entry points (process.run,
 * process.start, spawn, process.which, webserver.listen), so a user meets a
 * sentence naming Windows, not an errno; these stand-ins exist so the code
 * behind those refusals compiles, and they are what any missed path hits.
 * Anything an ordinary program reaches -- lock, real_path, close-on-exec,
 * fsync, signal handlers -- has a REAL Windows body in include/platform.h.
 *
 * WSAPoll TAKES SOCKETS ONLY. That is enough for the event loop, which polls
 * only sockets (measured, platform.h), and is NOT enough for a pipe -- so a
 * poll() over a child's pipes must not be reached on Windows.
 */
#ifndef GBASIC_POSIX_COMPAT_H
#define GBASIC_POSIX_COMPAT_H

#ifdef _WIN32

#include <winsock2.h>   /* must precede windows.h, which anything may include */
#include <ws2tcpip.h>
#include <io.h>
#include <process.h>

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

typedef unsigned long nfds_t;

/* WSAPoll differs from poll() in two ways the event loop meets. It REFUSES
 * an empty set (WSAEINVAL) where poll() simply waits out the timeout -- and a
 * program whose only event source is a timer polls exactly that, so every
 * timer program failed on its first iteration with "webserver poll failed".
 * And it reports through WSAGetLastError, never errno, so the caller's
 * `errno != EINTR` test read whatever errno last held. */
static inline int gb_compat_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (nfds == 0) {
        Sleep(timeout < 0 ? INFINITE : (DWORD)timeout);
        return 0;
    }
    int r = WSAPoll(fds, (ULONG)nfds, timeout);
    if (r == SOCKET_ERROR) {
        errno = WSAGetLastError() == WSAEINTR ? EINTR : EIO;
        return -1;
    }
    return r;
}
#define poll gb_compat_poll

/* Client sockets are made non-blocking at accept, so a peek without the flag
 * is the same peek there. (The webserver is refused on Windows anyway.) */
#define MSG_DONTWAIT 0

/* Child processes: see above. The status macros give the WINDOWS meaning of a
 * status -- an exit code, never a signal -- which is what a CreateProcess
 * implementation will hand them. */
#define WNOHANG 1
#ifndef SIGKILL
#define SIGKILL 9
#endif
#define WIFEXITED(s)   1
#define WEXITSTATUS(s) (s)
#define WIFSIGNALED(s) 0
#define WTERMSIG(s)    0

static inline pid_t fork(void) { errno = ENOSYS; return -1; }
static inline int pipe(int fds[2]) { (void)fds; errno = ENOSYS; return -1; }
static inline pid_t waitpid(pid_t pid, int *status, int options) {
    (void)pid; (void)status; (void)options;
    errno = ECHILD;                /* true: this build started no such child */
    return -1;
}
static inline int kill(pid_t pid, int sig) {
    (void)pid; (void)sig;
    errno = ESRCH;                 /* true for the same reason */
    return -1;
}
static inline pid_t getppid(void) { errno = ENOSYS; return -1; }
static inline int setpgid(pid_t a, pid_t b) { (void)a; (void)b; errno = ENOSYS; return -1; }

#define _SC_OPEN_MAX 4
static inline long sysconf(int name) { (void)name; errno = EINVAL; return -1; }
#define _CS_PATH 0
static inline size_t confstr(int name, char *buf, size_t len) {
    (void)name; (void)buf; (void)len;
    return 0;                      /* "no value", which callers already handle */
}

/* fcntl: every use left on a Windows-reachable path goes through
 * gb_set_cloexec instead; the rest are behind the refused entry points. */
#define F_DUPFD          0
#define F_GETFD          1
#define F_SETFD          2
#define F_GETFL          3
#define F_SETFL          4
#define F_DUPFD_CLOEXEC  1030
#define FD_CLOEXEC       1
#ifndef O_NONBLOCK
#define O_NONBLOCK       0x4000
#endif
static inline int fcntl(int fd, int cmd, ...) { (void)fd; (void)cmd; errno = ENOSYS; return -1; }

#else

#include <poll.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#endif

#endif /* GBASIC_POSIX_COMPAT_H */

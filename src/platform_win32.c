/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * platform_win32.c -- the three mechanisms include/platform.h names, for
 * Windows. NOT YET BUILT INTO ANYTHING: the Makefile still compiles
 * platform_posix.c, and a Windows build needs the socket seam, an ERE engine and
 * a console line editor before it links. What this file IS: the two mechanisms
 * whose Windows answer is unambiguous, written and SYNTAX-CHECKED against real
 * Windows headers by tools/cross-build-windows.sh, plus the one that is not,
 * refusing rather than pretending.
 *
 * It is here rather than waiting because the alternative is discovering these
 * three answers on a machine we are borrowing.
 */
#include "platform.h"

#include <windows.h>
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *gb_platform_name(void) { return "windows"; }

/* GetModuleFileNameW with a NULL module is the running .exe. WIDE, then UTF-8:
 * the ANSI form goes through the process code page, so a path containing any
 * character outside it comes back MANGLED rather than failing -- a wrong path
 * that looks like a path, which is how the stdlib would be "not found" in a
 * directory that plainly contains it.
 *
 * Unlike /proc/self/exe this is already canonical, so there is no realpath step
 * (which macOS does need -- see platform_posix.c). */
int gb_exe_path(char *buf, size_t size) {
    if (!buf || size == 0) {
        return 0;
    }
    WCHAR wide[32768];   /* the extended-length maximum, not MAX_PATH: a path
                          * longer than 260 is legal and truncating one silently
                          * is the same defect as the ANSI form above. */
    DWORD n = GetModuleFileNameW(NULL, wide, (DWORD)(sizeof wide / sizeof wide[0]));
    if (n == 0 || n >= (sizeof wide / sizeof wide[0])) {
        return 0;
    }
    int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (need <= 0 || (size_t)need > size) {
        return 0;
    }
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, buf, need, NULL, NULL) > 0;
}

/* NOT IMPLEMENTED, AND THAT IS THE ANSWER FOR NOW.
 *
 * The property relied on is not "a pair of connected sockets" -- it is that ONE
 * READ IS EXACTLY ONE WHOLE FRAME, which SOCK_SEQPACKET provides and Windows
 * cannot: its AF_UNIX is STREAM ONLY. A loopback TCP pair would connect and
 * would silently coalesce and split messages, so a reader would see half a frame
 * with nothing to say so -- an ordinary-looking wrong answer, which is worse
 * than not working.
 *
 * The honest sequence is: give src/actor.c an explicit length prefix (it has
 * none today, because SEQPACKET made one unnecessary), and THEN a stream
 * transport is correct here. That is Tier 2 -- it also needs DuplicateHandle in
 * place of SCM_RIGHTS -- and nothing in the 64 of 65 standard libraries that do
 * not use actors is waiting on it.
 *
 * Refusing means `spawn` raises on Windows. That is a documented gap, which is
 * the same treatment every compiled-out module gets, and it is recoverable. A
 * transport that loses frame boundaries is not. */
int gb_channel_socketpair(int sv[2]) {
    (void)sv;
    return 0;
}

/* A JOB OBJECT WITH KILL_ON_JOB_CLOSE is Windows' PDEATHSIG, and it is stronger
 * in the way that matters: the kernel kills the children when the last handle to
 * the job closes, INCLUDING when the parent is killed uncatchably -- which is
 * the case a userspace sweep can never cover and the reason PDEATHSIG is used on
 * Linux at all.
 *
 * IT IS THE OPPOSITE WAY ROUND, and that is the part to read carefully. On Linux
 * the CHILD arms this on itself between fork and exec. On Windows the PARENT
 * creates the job and assigns the child to it, so this function -- which has
 * only the child's point of view -- can do the useful half: put the CURRENT
 * process into a job that dies with its handle. The caller still re-checks
 * getppid() equivalently.
 *
 * The job is deliberately LEAKED: it must outlive this call and be released only
 * when the process does, which is exactly what not closing the handle means. */
void gb_arm_parent_death(void) {
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (!job) {
        return;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
    memset(&li, 0, sizeof li);
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li)) {
        CloseHandle(job);
        return;
    }
    if (!AssignProcessToJobObject(job, GetCurrentProcess())) {
        CloseHandle(job);   /* already in a job that forbids nesting: no worse
                             * than Linux's setuid case, which the header names */
    }
}

/* _putenv_s with an empty value REMOVES the variable; see the header. */
int gb_setenv(const char *name, const char *value) {
    return _putenv_s(name, value) == 0;
}

int gb_unsetenv(const char *name) {
    return _putenv_s(name, "") == 0;
}

/* The _s forms take (out, in) -- reversed from POSIX -- and return an errno. */
struct tm *gb_localtime(const time_t *t, struct tm *out) {
    return localtime_s(out, t) == 0 ? out : NULL;
}

struct tm *gb_gmtime(const time_t *t, struct tm *out) {
    return gmtime_s(out, t) == 0 ? out : NULL;
}

time_t gb_timegm(struct tm *tm) {
    return _mkgmtime(tm);
}

/* No mode: see the header. _mkdir sets errno = EEXIST for an existing entry,
 * which is the distinction the callers depend on. */
int gb_mkdir(const char *path, int mode) {
    (void)mode;
    return _mkdir(path);
}

/* See the header: LF out, on every platform. */
void gb_stdio_binary(void) {
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
}

/* WSAStartup is not optional and not idempotent-by-accident: every socket call
 * before it fails with WSANOTINITIALISED. Called once, from wherever a socket is
 * first wanted; Winsock refcounts, so a second call is harmless. */
int gb_net_init(void) {
    static int started = 0;
    if (started) {
        return 1;
    }
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 0;
    }
    started = 1;
    return 1;
}

/* `close()` on a SOCKET does not close it -- it fails, and the socket leaks
 * until the process ends. A server that leaked one per request would run for a
 * while and then stop accepting, which is the ordinary-looking failure this
 * wrapper exists to prevent. */
int gb_sock_close(int fd) {
    return closesocket((SOCKET)fd) == 0;
}

/* FIONBIO rather than fcntl: there is no F_GETFL on a socket, and Windows
 * offers no way to READ the current mode back -- so unlike the POSIX body this
 * cannot preserve other flags. It does not need to; O_NONBLOCK is the only flag
 * this tree ever changes on a socket. */
int gb_sock_set_blocking(int fd, int blocking) {
    u_long nonblocking = blocking ? 0 : 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &nonblocking) == 0;
}

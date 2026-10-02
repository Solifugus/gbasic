/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
 *
 * platform_posix.c -- the three mechanisms include/platform.h names, for the
 * POSIX family. Linux is implemented and gated; macOS is written where the
 * substitution is unambiguous and MARKED UNVERIFIED, because no Mac has ever
 * built this tree. Nothing here is claimed to work on a platform it has not run
 * on -- see include/platform.h for what each port still has to answer.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* readlink; the convention src/actor.c and src/eval.c use */
#endif
#ifdef __APPLE__
#  define _DARWIN_C_SOURCE
#endif

#include "platform.h"

#include <limits.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#  include <sys/prctl.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>   /* _NSGetExecutablePath */
#endif

const char *gb_platform_name(void) {
#if defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#else
    return "posix";
#endif
}

int gb_exe_path(char *buf, size_t size) {
    if (!buf || size == 0) {
        return 0;
    }
#if defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", buf, size - 1);
    if (n <= 0 || (size_t)n >= size) {
        return 0;
    }
    buf[n] = '\0';
    return 1;
#elif defined(__APPLE__)
    /* UNVERIFIED -- never run on a Mac. _NSGetExecutablePath wants the size as a
     * uint32_t IN/OUT and may hand back a path that is not canonical (it can
     * carry `..` or follow from a relative argv[0]), which readlink on
     * /proc/self/exe never does. The tarball's relocatability rests on the
     * CANONICAL answer, so realpath() is part of the substitution rather than a
     * refinement of it. */
    uint32_t want = (uint32_t)size;
    char raw[PATH_MAX];
    uint32_t rawsz = (uint32_t)sizeof raw;
    if (_NSGetExecutablePath(raw, &rawsz) != 0) {
        return 0;
    }
    char resolved[PATH_MAX];
    if (!realpath(raw, resolved)) {
        return 0;
    }
    if (strlen(resolved) + 1 > (size_t)want) {
        return 0;
    }
    memcpy(buf, resolved, strlen(resolved) + 1);
    return 1;
#else
    (void)size;
    return 0;
#endif
}

int gb_channel_socketpair(int sv[2]) {
#if defined(__linux__)
    /* SOCK_SEQPACKET: message boundaries AND an all-or-nothing sendmsg. Both are
     * relied on -- see the header. */
    return socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0;
#elif defined(__APPLE__)
    /* UNVERIFIED. AF_UNIX has no SOCK_SEQPACKET here. SOCK_DGRAM preserves
     * boundaries, which is the framing property; whether its send is
     * all-or-nothing for the sizes this tree uses is a SEPARATE question and is
     * unmeasured. A partial send that reported success would hand a reader half a
     * frame, and nothing downstream could tell. */
    return socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) == 0;
#else
    (void)sv;
    return 0;
#endif
}

void gb_arm_parent_death(void) {
#if defined(__linux__)
    prctl(PR_SET_PDEATHSIG, SIGTERM);
#else
    /* macOS has no equivalent -- see include/platform.h. Deliberately a no-op
     * rather than an approximation: the caller's getppid() re-check narrows the
     * window and does not close it, and pretending otherwise would make
     * run_process_lifetime.sh pass while the promise it tests was unkept. */
#endif
}

int gb_setenv(const char *name, const char *value) {
    return setenv(name, value, 1) == 0;
}

int gb_unsetenv(const char *name) {
    return unsetenv(name) == 0;
}

struct tm *gb_localtime(const time_t *t, struct tm *out) {
    return localtime_r(t, out);
}

struct tm *gb_gmtime(const time_t *t, struct tm *out) {
    return gmtime_r(t, out);
}

time_t gb_timegm(struct tm *tm) {
    return timegm(tm);
}

int gb_mkdir(const char *path, int mode) {
    return mkdir(path, (mode_t)mode);
}

int gb_flock(int fd, int op) {
    return flock(fd, op == GB_LOCK_EXCLUSIVE ? LOCK_EX : LOCK_UN);
}

char *gb_realpath(const char *path) {
    return realpath(path, NULL);
}

int gb_set_cloexec(int fd, int on) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFD, on ? (flags | FD_CLOEXEC) : (flags & ~FD_CLOEXEC));
}

int gb_fsync(int fd) {
    return fsync(fd);
}

int gb_process_alive(long pid) {
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
}

int gb_on_signal(int sig, void (*handler)(int), int restart) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags = restart ? SA_RESTART : 0;
    sigemptyset(&sa.sa_mask);
    return sigaction(sig, &sa, NULL);
}

int gb_secure_random(void *buf, size_t n) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    size_t off = 0;
    while (off < n) {
        ssize_t c = read(fd, (char *)buf + off, n - off);
        if (c < 0 && errno == EINTR) {
            continue;
        }
        if (c <= 0) {
            close(fd);
            return -1;
        }
        off += (size_t)c;
    }
    close(fd);
    return 0;
}

int gb_rename_replace(const char *from, const char *to) {
    return rename(from, to);
}

void gb_stdio_binary(void) {
    /* POSIX has no text mode: bytes are bytes. */
}

int gb_net_init(void) {
    return 1;   /* nothing to start: sockets are file descriptors here */
}

int gb_sock_close(int fd) {
    return close(fd) == 0;
}

int gb_sock_set_blocking(int fd, int blocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return 0;
    }
    int next = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return fcntl(fd, F_SETFL, next) == 0;
}

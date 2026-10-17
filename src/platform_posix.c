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
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>   /* utimes, for gb_set_mtime */
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

/* `long` is already 64 bits on every 64-bit POSIX ABI, so these are the calls
 * the callers made before the platform layer had them. */
int gb_fseek(FILE *file, gb_off_t offset, int whence) {
    return fseek(file, (long)offset, whence);
}

gb_off_t gb_ftell(FILE *file) { return (gb_off_t)ftell(file); }

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
    /* NOT SUFFICIENT, AND NOW MEASURED RATHER THAN "UNVERIFIED". AF_UNIX has no
     * SOCK_SEQPACKET here. SOCK_DGRAM preserves boundaries, which is the framing
     * property -- but it HAS NO END-OF-FILE, and src/actor.c reads a
     * zero-length frame as "the peer closed". Measured on Linux, where both
     * types exist (tests/channel_peek_probe.c): closing the peer gives recv 0 on
     * SEQPACKET and BLOCKS FOREVER on DGRAM. So an actor whose peer exits would
     * hang the reader here instead of reporting a shutdown.
     *
     * Left in place deliberately: it is the closest thing the platform offers,
     * it makes the rest of the port buildable and testable, and the real remedy
     * is the one Windows needed -- a STREAM socket with an explicit length
     * prefix in the channel layer. See include/platform.h. */
    return socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) == 0;
#else
    (void)sv;
    return 0;
#endif
}

ssize_t gb_channel_peek_len(int fd) {
/* GB_FORCE_PORTABLE_PEEK SELECTS THE NON-LINUX BRANCH ON LINUX, which is the
 * only way this code can be tested at all from here: the branch below exists
 * for macOS and the BSDs, no such machine is reachable, and an untested branch
 * in the actor transport is how a platform ships with actors that silently
 * deliver nothing. tests/channel_peek_probe.c compiles with it and asserts the
 * sizing against a real socketpair. */
#if defined(__linux__) && !defined(GB_FORCE_PORTABLE_PEEK)
    /* One zero-byte read answers it: MSG_TRUNC as an input flag is Linux's own
     * extension and reports the datagram's true length. */
    return recv(fd, NULL, 0, MSG_PEEK | MSG_TRUNC);
#else
    /* THE PORTABLE FORM, and it exists because the Linux one COMPILES
     * ELSEWHERE AND LIES. POSIX defines MSG_TRUNC only as an output flag, so
     * macOS answers 0 to the call above -- which src/actor.c reads as "the peer
     * closed", since a real frame always carries a 4-byte header. Actors would
     * have delivered nothing, silently.
     *
     * So the question is asked with only what POSIX guarantees: peek into a
     * buffer and grow while the kernel reports the data was truncated. MSG_PEEK
     * does not consume, so retrying is free of side effects.
     *
     * The control byte buffer is NOT supplied deliberately -- this call is only
     * sizing the payload, and asking for the descriptors here would receive
     * them twice. */
    size_t cap = 4096;
    for (;;) {
        char *probe = malloc(cap);
        if (!probe) {
            errno = ENOMEM;
            return -1;
        }
        struct iovec iov;
        iov.iov_base = probe;
        iov.iov_len = cap;
        struct msghdr msg;
        memset(&msg, 0, sizeof msg);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        ssize_t n = recvmsg(fd, &msg, MSG_PEEK);
        if (n < 0) {
            int saved = errno;
            free(probe);
            errno = saved;
            return -1;
        }
        int truncated = (msg.msg_flags & MSG_TRUNC) != 0;
        free(probe);
        if (!truncated) {
            return n;
        }
        /* It did not fit. A frame is bounded by the socket's own buffer, so this
         * cannot grow without end, but the ceiling is explicit rather than
         * trusted -- an unbounded doubling driven by a peer is a denial of
         * service, not a loop. */
        if (cap >= (size_t)16 * 1024 * 1024) {
            errno = EMSGSIZE;
            return -1;
        }
        cap *= 2;
    }
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

time_t gb_mktime(struct tm *tm) {
    return mktime(tm);
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

int gb_file_mtime(const char *path, time_t *out) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    *out = st.st_mtime;
    return 0;
}

int gb_is_symlink(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return 0;
    }
    return S_ISLNK(st.st_mode) ? 1 : 0;
}

int gb_stat(const char *path, struct stat *out) { return stat(path, out); }

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

long gb_stack_limit_kb(void) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) != 0) {
        return 0;
    }
    if (rl.rlim_cur == RLIM_INFINITY) {
        return 0;          /* unbounded: the caller keeps its own ceiling */
    }
    return (long)(rl.rlim_cur / 1024);
}

void gb_stdout_line_buffered(void) {
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
}

void gb_stdio_binary(void) {
    /* POSIX has no text mode: bytes are bytes. */
}

void gb_stdin_binary(void) {
    /* No text mode here either. */
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

/* The system temp directory, honouring TMPDIR. */
static const char *posix_temp_root(void) {
    const char *dir = getenv("TMPDIR");
    if (dir && dir[0] != '\0') {
        return dir;
    }
    return "/tmp";
}

static int posix_temp_template(const char *dir, const char *prefix,
                               char *out, size_t out_size) {
    const char *root = (dir && dir[0] != '\0') ? dir : posix_temp_root();
    size_t len = strlen(root);
    const char *sep = (len > 0 && root[len - 1] == '/') ? "" : "/";
    const char *use = (prefix && prefix[0] != '\0') ? prefix : "gbasic-";
    int w = snprintf(out, out_size, "%s%s%sXXXXXX", root, sep, use);
    if (w < 0 || (size_t)w >= out_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int gb_make_temp_file(const char *dir, const char *prefix, char *out, size_t out_size) {
    if (posix_temp_template(dir, prefix, out, out_size) != 0) {
        return -1;
    }
    /* `mkstemp` creates it with O_EXCL and mode 0600 -- the atomicity is the
     * point, and the mode is why a program can hold a secret in a temp file
     * before any chmod exists. The descriptor is closed: this answers a PATH,
     * and the race was already won by creating the name. */
    int fd = mkstemp(out);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return 0;
}

int gb_make_temp_dir(const char *dir, const char *prefix, char *out, size_t out_size) {
    if (posix_temp_template(dir, prefix, out, out_size) != 0) {
        return -1;
    }
    /* mkdtemp creates it 0700. */
    return mkdtemp(out) ? 0 : -1;
}

int gb_permissions(const char *path, GbPermissions *out) {
    struct stat info;
    if (stat(path, &info) != 0) {
        return -1;
    }
    out->mode = (unsigned)(info.st_mode & 07777);
    /* access(2), NOT the bits: this is the "can I" question, and the two
     * genuinely differ -- as root every mode is readable, and on a read-only
     * mount a 0644 file is not writable. */
    out->readable = access(path, R_OK) == 0;
    out->writable = access(path, W_OK) == 0;
    out->executable = access(path, X_OK) == 0;
    return 0;
}

int gb_chmod(const char *path, unsigned mode, int *dropped) {
    if (dropped) {
        *dropped = 0;   /* POSIX honours every bit; nothing to report. */
    }
    return chmod(path, (mode_t)(mode & 07777));
}

int gb_set_mtime(const char *path, time_t when) {
    /* The ACCESS time is preserved rather than stamped: an archive step cares
     * about when the data was written, and rewriting atime as a side effect
     * would destroy information the caller did not offer. */
    struct stat info;
    struct timeval times[2];
    if (stat(path, &info) == 0) {
        times[0].tv_sec = info.st_atime;
    } else {
        times[0].tv_sec = when;
    }
    times[0].tv_usec = 0;
    times[1].tv_sec = when;
    times[1].tv_usec = 0;
    return utimes(path, times);
}

int gb_symlink(const char *target, const char *path, char *why, size_t why_size) {
    if (symlink(target, path) == 0) {
        return 0;
    }
    snprintf(why, why_size, "could not create symlink %s -> %s (%s)",
             path, target, strerror(errno));
    return -1;
}

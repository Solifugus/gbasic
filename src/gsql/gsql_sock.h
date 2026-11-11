#ifndef GBASIC_GSQL_SOCK_H
#define GBASIC_GSQL_SOCK_H

/* The owner's and the client's sockets, on both platforms
 * (docs/gsql_owner_design.md §2). Self-contained rather than through
 * include/platform.h, because the owner and client are also linked into test
 * programs that carry no platform layer.
 *
 * ON POSIX EVERY HELPER IS EXACTLY THE CALL IT REPLACES, so the Linux owner is
 * unchanged by construction.
 *
 * ON WINDOWS a socket is a Winsock SOCKET, not a descriptor: read, write,
 * close and fcntl on one fail or leak, and failures arrive through
 * WSAGetLastError. AF_UNIX is a stream socket there (afunix.h, Windows 10 1803
 * and later), which is the kind the owner uses everywhere. The owner's wake --
 * a pipe its SIGTERM handler writes on POSIX -- is a loopback socket pair,
 * since WSAPoll takes sockets only. */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#include <windows.h>
#include <aclapi.h>
#include <stdlib.h>
#include <sys/types.h>

#ifndef EOPNOTSUPP
#define EOPNOTSUPP 130
#endif

typedef unsigned long gs_nfds_t;
#define nfds_t gs_nfds_t

static inline int gs_sk_start(void) {
    static int started = 0;
    WSADATA w;
    if (!started && WSAStartup(MAKEWORD(2, 2), &w) == 0) {
        started = 1;
    }
    return started;
}

static inline int gs_sk_map(int w) {
    switch (w) {
    case WSAEWOULDBLOCK: return EAGAIN;
    case WSAEINTR: return EINTR;
    case WSAECONNREFUSED: return ECONNREFUSED;
    case WSAECONNRESET: case WSAECONNABORTED: case WSAESHUTDOWN: return ECONNRESET;
    case WSAEADDRINUSE: return EADDRINUSE;
    case WSAEACCES: return EACCES;
    case WSAEOPNOTSUPP: case WSAEAFNOSUPPORT: return EOPNOTSUPP;
    case WSAENAMETOOLONG: return ENAMETOOLONG;
    case WSAEINVAL: return EINVAL;
    case WSAENOTSOCK: return EBADF;
    case WSAENOBUFS: return ENOBUFS;
    default: return EIO;
    }
}
static inline int gs_sk_fail(void) {
    errno = gs_sk_map(WSAGetLastError());
    return -1;
}

static inline int gs_sk_unix(void) {
    if (!gs_sk_start()) {
        errno = EIO;
        return -1;
    }
    SOCKET s = WSASocketW(AF_UNIX, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    return s == INVALID_SOCKET ? gs_sk_fail() : (int)s;
}
static inline int gs_sk_bind(int fd, const struct sockaddr *a, int n) {
    return bind((SOCKET)fd, a, n) == 0 ? 0 : gs_sk_fail();
}
static inline int gs_sk_connect(int fd, const struct sockaddr *a, int n) {
    return connect((SOCKET)fd, a, n) == 0 ? 0 : gs_sk_fail();
}
static inline int gs_sk_listen(int fd, int backlog) {
    return listen((SOCKET)fd, backlog) == 0 ? 0 : gs_sk_fail();
}
static inline int gs_sk_accept(int fd) {
    SOCKET c = accept((SOCKET)fd, NULL, NULL);
    if (c == INVALID_SOCKET) {
        return gs_sk_fail();
    }
    SetHandleInformation((HANDLE)c, HANDLE_FLAG_INHERIT, 0);
    return (int)c;
}
static inline ssize_t gs_sk_read(int fd, void *b, size_t n) {
    int r = recv((SOCKET)fd, (char *)b, n > 0x40000000u ? 0x40000000 : (int)n, 0);
    return r == SOCKET_ERROR ? gs_sk_fail() : r;
}
static inline ssize_t gs_sk_write(int fd, const void *b, size_t n) {
    int r = send((SOCKET)fd, (const char *)b, n > 0x40000000u ? 0x40000000 : (int)n, 0);
    return r == SOCKET_ERROR ? gs_sk_fail() : r;
}
static inline int gs_sk_close(int fd) {
    return closesocket((SOCKET)fd) == 0 ? 0 : gs_sk_fail();
}
static inline int gs_sk_nonblock(int fd) {
    u_long on = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &on) == 0 ? 0 : gs_sk_fail();
}
/* made non-inheritable at creation (and at accept) */
static inline int gs_sk_cloexec(int fd) {
    (void)fd;
    return 0;
}
static inline int gs_sk_poll(struct pollfd *f, gs_nfds_t n, int timeout) {
    if (n == 0) {
        Sleep(timeout < 0 ? INFINITE : (DWORD)timeout);
        return 0;
    }
    int r = WSAPoll(f, (ULONG)n, timeout);
    return r == SOCKET_ERROR ? gs_sk_fail() : r;
}
/* A connected pair over loopback TCP: the owner's wake. The accepted end is
 * checked to be the socket this function connected, so nothing else on the
 * machine can become the wake in the instant the listener exists. */
static inline int gs_sk_pair(int fds[2]) {
    if (!gs_sk_start()) {
        errno = EIO;
        return -1;
    }
    SOCKET l = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    SOCKET c = INVALID_SOCKET, a = INVALID_SOCKET;
    struct sockaddr_in in, peer, mine;
    int n = sizeof in;
    memset(&in, 0, sizeof in);
    in.sin_family = AF_INET;
    in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (l == INVALID_SOCKET || bind(l, (struct sockaddr *)&in, sizeof in) != 0 || listen(l, 1) != 0 ||
        getsockname(l, (struct sockaddr *)&in, &n) != 0) {
        goto fail;
    }
    c = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (c == INVALID_SOCKET || connect(c, (struct sockaddr *)&in, sizeof in) != 0) {
        goto fail;
    }
    n = sizeof peer;
    a = accept(l, (struct sockaddr *)&peer, &n);
    int mn = sizeof mine;
    if (a == INVALID_SOCKET || getsockname(c, (struct sockaddr *)&mine, &mn) != 0 ||
        peer.sin_port != mine.sin_port || peer.sin_addr.s_addr != mine.sin_addr.s_addr) {
        goto fail;
    }
    SetHandleInformation((HANDLE)a, HANDLE_FLAG_INHERIT, 0);
    closesocket(l);
    fds[0] = (int)a;
    fds[1] = (int)c;
    return 0;
fail:
    gs_sk_fail();
    if (a != INVALID_SOCKET) closesocket(a);
    if (c != INVALID_SOCKET) closesocket(c);
    if (l != INVALID_SOCKET) closesocket(l);
    return -1;
}

/* rename over an existing name, as POSIX rename does: the CRT's refuses one */
static inline int gs_rename_over(const char *from, const char *to) {
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) {
        return 0;
    }
    errno = GetLastError() == ERROR_ACCESS_DENIED ? EACCES : EIO;
    return -1;
}

/* A directory's identity: the volume and the file index, Windows' device and
 * inode. Two paths to one directory -- a junction, a symlink, a mapped drive
 * on the same volume -- give the same pair. */
static inline int gs_dir_identity(const char *path, unsigned long long *dev, unsigned long long *ino) {
    HANDLE h = CreateFileA(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    BY_HANDLE_FILE_INFORMATION fi;
    int ok = GetFileInformationByHandle(h, &fi);
    CloseHandle(h);
    if (!ok) {
        return -1;
    }
    *dev = fi.dwVolumeSerialNumber;
    *ino = ((unsigned long long)fi.nFileIndexHigh << 32) | fi.nFileIndexLow;
    return 0;
}

/* Windows' form of the trust check gsql_socket_path makes with a uid and a
 * mode (docs/gsql_owner_design.md §3): the socket in a directory is trusted
 * only if nobody but this user could have put it there. Answers 0 when the
 * directory is this user's and nobody else may write in it; 1 when it belongs
 * to somebody else; 2 when another account may write in it (with `strict`,
 * as for the runtime directory, may have any access at all). SYSTEM and the
 * Administrators group are not "somebody else" -- they can take any file, as
 * root can -- and an inherit-only entry does not apply to the directory. */
static inline int gs_dir_trust(const char *path, int strict) {
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return 1;
    }
    DWORD need = 0;
    GetTokenInformation(token, TokenUser, NULL, 0, &need);
    TOKEN_USER *user = need ? malloc(need) : NULL;
    int verdict = 1;
    PSID owner = NULL;
    PACL dacl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    BYTE sys_buf[SECURITY_MAX_SID_SIZE], adm_buf[SECURITY_MAX_SID_SIZE];
    DWORD sl = sizeof sys_buf, al = sizeof adm_buf;
    PSID sys = (PSID)sys_buf, adm = (PSID)adm_buf;
    if (!user || !GetTokenInformation(token, TokenUser, user, need, &need) ||
        !CreateWellKnownSid(WinLocalSystemSid, NULL, sys, &sl) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, adm, &al) ||
        GetNamedSecurityInfoA(path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                              NULL, &dacl, NULL, &sd) != ERROR_SUCCESS) {
        goto done;
    }
    BOOL admin_member = FALSE;
    if (!EqualSid(owner, user->User.Sid) &&
        !(EqualSid(owner, adm) && CheckTokenMembership(NULL, adm, &admin_member) && admin_member)) {
        verdict = 1;
        goto done;
    }
    verdict = 0;
    if (!dacl) {
        verdict = 2;   /* a NULL DACL grants everybody everything */
        goto done;
    }
    const DWORD writes = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_WRITE |
                         GENERIC_ALL | WRITE_DAC | WRITE_OWNER | DELETE | FILE_DELETE_CHILD;
    for (DWORD i = 0; i < dacl->AceCount; i++) {
        ACE_HEADER *ace = NULL;
        if (!GetAce(dacl, i, (LPVOID *)&ace) || ace->AceType != ACCESS_ALLOWED_ACE_TYPE ||
            (ace->AceFlags & INHERIT_ONLY_ACE)) {
            continue;
        }
        ACCESS_ALLOWED_ACE *a = (ACCESS_ALLOWED_ACE *)ace;
        PSID sid = (PSID)&a->SidStart;
        if (EqualSid(sid, user->User.Sid) || EqualSid(sid, sys) || EqualSid(sid, adm)) {
            continue;
        }
        if (strict || (a->Mask & writes)) {
            verdict = 2;
            break;
        }
    }
done:
    if (sd) {
        LocalFree(sd);
    }
    free(user);
    CloseHandle(token);
    return verdict;
}

#else /* POSIX */

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static inline int gs_sk_start(void) { return 1; }
static inline int gs_sk_unix(void) { return socket(AF_UNIX, SOCK_STREAM, 0); }
static inline int gs_sk_bind(int fd, const struct sockaddr *a, socklen_t n) { return bind(fd, a, n); }
static inline int gs_sk_connect(int fd, const struct sockaddr *a, socklen_t n) { return connect(fd, a, n); }
static inline int gs_sk_listen(int fd, int backlog) { return listen(fd, backlog); }
static inline int gs_sk_accept(int fd) { return accept(fd, NULL, NULL); }
static inline ssize_t gs_sk_read(int fd, void *b, size_t n) { return read(fd, b, n); }
static inline ssize_t gs_sk_write(int fd, const void *b, size_t n) { return write(fd, b, n); }
static inline int gs_sk_close(int fd) { return close(fd); }
static inline int gs_sk_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL);
    return fl < 0 ? -1 : fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}
static inline int gs_sk_cloexec(int fd) { return fcntl(fd, F_SETFD, FD_CLOEXEC); }
static inline int gs_sk_poll(struct pollfd *f, nfds_t n, int timeout) { return poll(f, n, timeout); }
static inline int gs_sk_pair(int fds[2]) { return pipe(fds); }
static inline int gs_rename_over(const char *from, const char *to) { return rename(from, to); }

#endif

#endif

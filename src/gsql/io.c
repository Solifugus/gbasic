/* G-SQL's I/O layer: the dispatch, and the REAL implementation
 * (docs/gsql_storage_design.md §3.4). The simulated one is io_sim.c. See
 * include/gsql_io.h for the contract both obey. */

#define _GNU_SOURCE
#define _CRT_RAND_S   /* rand_s, the Windows layer's random source */
#include "io_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

int gsql_io_open(GsqlIo *io, const char *path, int flags, GsqlFile **out) {
    *out = NULL;
    return io->ops->open(io, path, flags, out);
}
int gsql_io_read_at(GsqlFile *f, uint64_t offset, void *buf, size_t n, size_t *got) {
    *got = 0;
    return f->io->ops->read_at(f, offset, buf, n, got);
}
int gsql_io_write_at(GsqlFile *f, uint64_t offset, const void *buf, size_t n) {
    return f->io->ops->write_at(f, offset, buf, n);
}
int gsql_io_sync(GsqlFile *f) { return f->io->ops->sync(f); }
int gsql_io_size(GsqlFile *f, uint64_t *out) {
    *out = 0;
    return f->io->ops->size(f, out);
}
int gsql_io_truncate(GsqlFile *f, uint64_t n) { return f->io->ops->truncate(f, n); }
int gsql_io_close(GsqlFile *f) { return f->io->ops->close(f); }
int gsql_io_rename(GsqlIo *io, const char *from, const char *to) {
    return io->ops->rename(io, from, to);
}
int gsql_io_remove(GsqlIo *io, const char *path) { return io->ops->remove(io, path); }
int gsql_io_sync_dir(GsqlIo *io, const char *dir) { return io->ops->sync_dir(io, dir); }
int gsql_io_lock(GsqlFile *f, int nonblocking) { return f->io->ops->lock(f, nonblocking); }
void gsql_io_free(GsqlIo *io) {
    if (io) io->ops->free(io);
}
void gsql_io_random(GsqlIo *io, void *out, size_t n) { io->ops->random(io, out, n); }
uint64_t gsql_io_now_ms(GsqlIo *io) { return io->ops->now_ms(io); }
void gsql_io_sleep_ms(GsqlIo *io, uint64_t ms) { io->ops->sleep_ms(io, ms); }

uint64_t gsql_io_mono_ms(GsqlIo *io) {
    (void)io;
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}
void gsql_io_mono_sleep(GsqlIo *io, uint64_t ms) {
    (void)io;
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
#endif
}
int gsql_io_mkdir(GsqlIo *io, const char *path) { return io->ops->mkdir(io, path); }
int gsql_io_list(GsqlIo *io, const char *dir, int (*each)(void *, const char *), void *ctx) {
    return io->ops->list(io, dir, each, ctx);
}

/* Shared by both layers: the names in byte order, then the walk, so the two
 * cannot disagree about order and a parity test compares like with like. */
static int name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
int gsql_io_walk_names(char **names, size_t n, int (*each)(void *, const char *), void *ctx) {
    if (n) qsort(names, n, sizeof *names, name_cmp);
    int rc = 0;
    for (size_t i = 0; i < n && !rc; i++) rc = each(ctx, names[i]);
    for (size_t i = 0; i < n; i++) free(names[i]);
    free(names);
    return rc;
}

#if !defined(_WIN32)
static void urandom(GsqlIo *io, void *out, size_t n) {
    (void)io;
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got = f ? fread(out, 1, n, f) : 0;
    if (f) fclose(f);
    /* no urandom: the clock, which is weak and still not a constant */
    for (size_t i = got; i < n; i++) ((unsigned char *)out)[i] = (unsigned char)(clock() * 2654435761u >> (8 * (i % 4)));
}
#endif

#if defined(_WIN32)

/* THE WINDOWS LAYER (docs/gsql_storage_design.md §3.4). The contract is the
 * POSIX one in include/gsql_io.h, and each rule has its Windows route:
 *
 *   sync        FlushFileBuffers, which on NTFS reaches the disk (it issues
 *               the flush that fdatasync does);
 *   rename      SetFileInformationByHandle(FileRenameInfoEx) with POSIX
 *               semantics: atomic, and replacing a target another handle has
 *               open -- which MoveFileEx refuses, and an LSM's manifest swap is
 *               exactly that. MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH) is
 *               the fallback for a filesystem without POSIX semantics (FAT).
 *   remove      FileDispositionInfoEx with POSIX semantics: the name goes now
 *               and the data when the last handle closes, as unlink does.
 *   sync_dir    FlushFileBuffers on the directory handle. NTFS journals its
 *               metadata and this forces the journal out, which is rule 2.
 *   lock        LockFileEx on a range FAR PAST the end of the file. Windows
 *               byte-range locks are MANDATORY, unlike flock: a lock over the
 *               data would refuse reads and writes through any other handle.
 *               A range no I/O ever touches is a pure mutual exclusion, which
 *               is what flock gives (SQLite does the same). Per handle, so a
 *               second handle in the same process is refused too, as with
 *               flock.
 *   mkdir       a directory whose DACL is this user's alone, protected from
 *               inheritance: what 0700 means.
 *
 * Every file is opened with FILE_SHARE_READ | WRITE | DELETE, so a reader's
 * handle never stops a rename or a removal, which POSIX never stops either.
 * Paths are UTF-8; '/' is accepted by every call used here. */

#include <sddl.h>
#include <io.h>

typedef struct {
    GsqlFile base;
    HANDLE h;
    int locked;     /* flock re-taken through the same handle is a no-op; LockFileEx refuses it */
} WinFile;

static int win_errno(DWORD e) {
    switch (e) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_NAME:
        return ENOENT;
    case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS:
        return EEXIST;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION:
        return EACCES;
    case ERROR_LOCK_VIOLATION: case ERROR_IO_PENDING:
        return EWOULDBLOCK;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL:
        return ENOSPC;
    case ERROR_DIR_NOT_EMPTY:
        return ENOTEMPTY;
    case ERROR_NOT_ENOUGH_MEMORY: case ERROR_OUTOFMEMORY:
        return ENOMEM;
    case ERROR_DIRECTORY:
        return ENOTDIR;
    case ERROR_FILENAME_EXCED_RANGE:
        return ENAMETOOLONG;
    default:
        return EIO;
    }
}

/* UTF-8 to UTF-16, malloc'd; NULL on a malformed name or no memory. */
static wchar_t *win_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0) {
        return NULL;
    }
    wchar_t *w = malloc((size_t)n * sizeof *w);
    if (w && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) != n) {
        free(w);
        return NULL;
    }
    return w;
}

static int win_open(GsqlIo *io, const char *path, int flags, GsqlFile **out) {
    wchar_t *w = win_wide(path);
    if (!w) {
        return ENOENT;
    }
    DWORD access = (flags & GSQL_IO_READ_ONLY) ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE;
    DWORD disp = OPEN_EXISTING;
    if (flags & GSQL_IO_CREATE) {
        disp = (flags & GSQL_IO_EXCLUSIVE) ? CREATE_NEW : OPEN_ALWAYS;
    }
    HANDLE h = CreateFileW(w, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disp,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD e = GetLastError();
    free(w);
    if (h == INVALID_HANDLE_VALUE) {
        return win_errno(e);
    }
    WinFile *f = calloc(1, sizeof *f);
    if (!f) {
        CloseHandle(h);
        return ENOMEM;
    }
    f->base.io = io;
    f->h = h;
    *out = &f->base;
    return 0;
}

static int win_read_at(GsqlFile *base, uint64_t offset, void *buf, size_t n, size_t *got) {
    WinFile *f = (WinFile *)base;
    size_t done = 0;
    while (done < n) {
        OVERLAPPED ov;
        memset(&ov, 0, sizeof ov);
        uint64_t at = offset + done;
        ov.Offset = (DWORD)at;
        ov.OffsetHigh = (DWORD)(at >> 32);
        size_t want = n - done;
        DWORD chunk = want > 0x40000000u ? 0x40000000u : (DWORD)want;
        DWORD r = 0;
        if (!ReadFile(f->h, (char *)buf + done, chunk, &r, &ov)) {
            DWORD e = GetLastError();
            if (e == ERROR_HANDLE_EOF) {
                break;   /* end of file: a short read, not an error */
            }
            *got = done;
            return win_errno(e);
        }
        if (r == 0) {
            break;
        }
        done += r;
    }
    *got = done;
    return 0;
}

static int win_write_at(GsqlFile *base, uint64_t offset, const void *buf, size_t n) {
    WinFile *f = (WinFile *)base;
    size_t done = 0;
    while (done < n) {
        OVERLAPPED ov;
        memset(&ov, 0, sizeof ov);
        uint64_t at = offset + done;
        ov.Offset = (DWORD)at;
        ov.OffsetHigh = (DWORD)(at >> 32);
        size_t want = n - done;
        DWORD chunk = want > 0x40000000u ? 0x40000000u : (DWORD)want;
        DWORD w = 0;
        if (!WriteFile(f->h, (const char *)buf + done, chunk, &w, &ov)) {
            return win_errno(GetLastError());
        }
        done += w;
    }
    return 0;
}

static int win_sync(GsqlFile *base) {
    HANDLE h = ((WinFile *)base)->h;
    if (FlushFileBuffers(h)) {
        return 0;
    }
    DWORD e = GetLastError();
    if (e != ERROR_ACCESS_DENIED) {
        return win_errno(e);
    }
    /* A read-only handle: FlushFileBuffers wants write access, where fsync on
     * a read-only descriptor flushes the file. The same file, reopened for
     * writing, is flushed instead. */
    HANDLE w = ReOpenFile(h, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    if (w == INVALID_HANDLE_VALUE) {
        return win_errno(GetLastError());
    }
    int rc = FlushFileBuffers(w) ? 0 : win_errno(GetLastError());
    CloseHandle(w);
    return rc;
}

static int win_size(GsqlFile *base, uint64_t *out) {
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(((WinFile *)base)->h, &sz)) {
        return win_errno(GetLastError());
    }
    *out = (uint64_t)sz.QuadPart;
    return 0;
}

static int win_truncate(GsqlFile *base, uint64_t n) {
    FILE_END_OF_FILE_INFO eof;
    eof.EndOfFile.QuadPart = (LONGLONG)n;
    return SetFileInformationByHandle(((WinFile *)base)->h, FileEndOfFileInfo, &eof, sizeof eof)
               ? 0 : win_errno(GetLastError());
}

static int win_close(GsqlFile *base) {
    WinFile *f = (WinFile *)base;
    int rc = CloseHandle(f->h) ? 0 : win_errno(GetLastError());
    free(f);
    return rc;
}

/* Not in every MinGW header yet (Windows 10 1709 and later). */
#ifndef FILE_RENAME_FLAG_REPLACE_IF_EXISTS
#define FILE_RENAME_FLAG_REPLACE_IF_EXISTS 0x00000001
#define FILE_RENAME_FLAG_POSIX_SEMANTICS 0x00000002
#endif
#ifndef FILE_DISPOSITION_FLAG_DELETE
#define FILE_DISPOSITION_FLAG_DELETE 0x00000001
#define FILE_DISPOSITION_FLAG_POSIX_SEMANTICS 0x00000002
#endif
#define GSQL_FileRenameInfoEx ((FILE_INFO_BY_HANDLE_CLASS)22)
#define GSQL_FileDispositionInfoEx ((FILE_INFO_BY_HANDLE_CLASS)21)

static HANDLE win_open_for_delete(const wchar_t *w) {
    return CreateFileW(w, DELETE | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
}

static int win_rename(GsqlIo *io, const char *from, const char *to) {
    (void)io;
    wchar_t *wf = win_wide(from), *wt = win_wide(to);
    if (!wf || !wt) {
        free(wf);
        free(wt);
        return ENOENT;
    }
    int rc = 0;
    HANDLE h = win_open_for_delete(wf);
    if (h == INVALID_HANDLE_VALUE) {
        rc = win_errno(GetLastError());
    } else {
        /* FILE_RENAME_INFO with a variable-length name; Flags shares its
         * offset with the BOOLEAN ReplaceIfExists of the older class */
        size_t tl = wcslen(wt);
        size_t sz = sizeof(FILE_RENAME_INFO) + (tl + 1) * sizeof(wchar_t);
        FILE_RENAME_INFO *ri = calloc(1, sz);
        if (!ri) {
            CloseHandle(h);
            free(wf);
            free(wt);
            return ENOMEM;
        }
        *(DWORD *)ri = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
        ri->RootDirectory = NULL;
        ri->FileNameLength = (DWORD)(tl * sizeof(wchar_t));
        memcpy(ri->FileName, wt, (tl + 1) * sizeof(wchar_t));
        /* the target name must be in the NT form: '/' is not converted here */
        for (size_t i = 0; i < tl; i++) {
            if (ri->FileName[i] == L'/') {
                ri->FileName[i] = L'\\';
            }
        }
        BOOL ok = SetFileInformationByHandle(h, GSQL_FileRenameInfoEx, ri, (DWORD)sz);
        DWORD e = GetLastError();
        free(ri);
        CloseHandle(h);
        if (!ok) {
            /* a filesystem without POSIX semantics (FAT, some network shares) */
            if (MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                rc = 0;
            } else {
                rc = win_errno(e == ERROR_INVALID_PARAMETER || e == ERROR_NOT_SUPPORTED ? GetLastError() : e);
            }
        }
    }
    free(wf);
    free(wt);
    return rc;
}

static int win_remove(GsqlIo *io, const char *path) {
    (void)io;
    wchar_t *w = win_wide(path);
    if (!w) {
        return ENOENT;
    }
    int rc = 0;
    HANDLE h = win_open_for_delete(w);
    if (h == INVALID_HANDLE_VALUE) {
        rc = win_errno(GetLastError());
    } else {
        DWORD flags = FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS;
        BOOL ok = SetFileInformationByHandle(h, GSQL_FileDispositionInfoEx, &flags, sizeof flags);
        if (!ok) {
            FILE_DISPOSITION_INFO d = { TRUE };
            ok = SetFileInformationByHandle(h, FileDispositionInfo, &d, sizeof d);
        }
        if (!ok) {
            rc = win_errno(GetLastError());
        }
        CloseHandle(h);
    }
    free(w);
    return rc;
}

static int win_sync_dir(GsqlIo *io, const char *dir) {
    (void)io;
    wchar_t *w = win_wide(dir[0] ? dir : ".");
    if (!w) {
        return ENOENT;
    }
    HANDLE h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    DWORD e = GetLastError();
    free(w);
    if (h == INVALID_HANDLE_VALUE) {
        return win_errno(e);
    }
    int rc = FlushFileBuffers(h) ? 0 : win_errno(GetLastError());
    CloseHandle(h);
    return rc;
}

static int win_lock(GsqlFile *base, int nonblocking) {
    WinFile *f = (WinFile *)base;
    if (f->locked) {
        return 0;
    }
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.Offset = 0;
    ov.OffsetHigh = GSQL_IO_WIN_LOCK_HIGH;   /* far past any file this engine writes */
    DWORD flags = LOCKFILE_EXCLUSIVE_LOCK | (nonblocking ? LOCKFILE_FAIL_IMMEDIATELY : 0);
    if (LockFileEx(f->h, flags, 0, 1, 0, &ov)) {
        f->locked = 1;
        return 0;
    }
    DWORD e = GetLastError();
    return e == ERROR_LOCK_VIOLATION || e == ERROR_IO_PENDING ? EWOULDBLOCK : win_errno(e);
}

static void win_free(GsqlIo *io) { free(io); }

static void win_random(GsqlIo *io, void *out, size_t n) {
    (void)io;
    unsigned char *p = out;
    for (size_t i = 0; i < n; i += 4) {
        unsigned int v = 0;
        if (rand_s(&v) != 0) {
            v = (unsigned int)(clock() * 2654435761u);   /* as urandom's fallback */
        }
        for (size_t k = 0; k < 4 && i + k < n; k++) {
            p[i + k] = (unsigned char)(v >> (8 * k));
        }
    }
}

/* This user's alone, protected from inheritance: what mode 0700 means. */
static int win_mkdir(GsqlIo *io, const char *path) {
    (void)io;
    wchar_t *w = win_wide(path);
    if (!w) {
        return ENOENT;
    }
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, FALSE };
    HANDLE token = NULL;
    LPWSTR sid = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD need = 0;
        GetTokenInformation(token, TokenUser, NULL, 0, &need);
        TOKEN_USER *user = need ? malloc(need) : NULL;
        if (user && GetTokenInformation(token, TokenUser, user, need, &need)) {
            ConvertSidToStringSidW(user->User.Sid, &sid);
        }
        free(user);
        CloseHandle(token);
    }
    if (sid) {
        WCHAR sddl[256];
        _snwprintf(sddl, sizeof sddl / sizeof sddl[0], L"D:P(A;OICI;FA;;;%ls)", sid);
        sddl[sizeof sddl / sizeof sddl[0] - 1] = L'\0';
        LocalFree(sid);
        ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL);
    }
    if (!sa.lpSecurityDescriptor) {
        free(w);
        return EACCES;   /* a directory that is silently not private breaks the promise */
    }
    BOOL ok = CreateDirectoryW(w, &sa);
    DWORD e = GetLastError();
    LocalFree(sa.lpSecurityDescriptor);
    free(w);
    return ok ? 0 : win_errno(e);
}

static int win_list(GsqlIo *io, const char *dir, int (*each)(void *, const char *), void *ctx) {
    (void)io;
    size_t dl = strlen(dir[0] ? dir : ".");
    char *pat = malloc(dl + 3);
    if (!pat) {
        return ENOMEM;
    }
    memcpy(pat, dir[0] ? dir : ".", dl);
    memcpy(pat + dl, "/*", 3);
    wchar_t *w = win_wide(pat);
    free(pat);
    if (!w) {
        return ENOENT;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    DWORD e = GetLastError();
    free(w);
    if (h == INVALID_HANDLE_VALUE) {
        return e == ERROR_FILE_NOT_FOUND ? 0 : win_errno(e);   /* an empty directory has no entries */
    }
    char **names = NULL;
    size_t n = 0, cap = 0;
    int rc = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
            continue;
        }
        int len = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, NULL, 0, NULL, NULL);
        char *name = len > 0 ? malloc((size_t)len) : NULL;
        if (!name) {
            rc = ENOMEM;
            break;
        }
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, len, NULL, NULL);
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            char **p = realloc(names, cap * sizeof *p);
            if (!p) {
                free(name);
                rc = ENOMEM;
                break;
            }
            names = p;
        }
        names[n++] = name;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (rc) {
        for (size_t i = 0; i < n; i++) {
            free(names[i]);
        }
        free(names);
        return rc;
    }
    return gsql_io_walk_names(names, n, each, ctx);
}

static const GsqlIoOps real_ops = {
    win_open, win_read_at, win_write_at, win_sync, win_size, win_truncate,
    win_close, win_rename, win_remove, win_sync_dir, win_lock, win_free, win_random,
    win_mkdir, win_list, gsql_io_mono_ms, gsql_io_mono_sleep
};

#else

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    GsqlFile base;
    int fd;
} RealFile;

static int real_open(GsqlIo *io, const char *path, int flags, GsqlFile **out) {
    int oflags = (flags & GSQL_IO_READ_ONLY) ? O_RDONLY : O_RDWR;
    if (flags & GSQL_IO_CREATE) oflags |= O_CREAT;
    if (flags & GSQL_IO_EXCLUSIVE) oflags |= O_EXCL;
#ifdef O_CLOEXEC
    oflags |= O_CLOEXEC;
#endif
    int fd = open(path, oflags, 0644);
    if (fd < 0) return errno;
    RealFile *f = calloc(1, sizeof *f);
    if (!f) {
        close(fd);
        return ENOMEM;
    }
    f->base.io = io;
    f->fd = fd;
    *out = &f->base;
    return 0;
}

static int real_read_at(GsqlFile *base, uint64_t offset, void *buf, size_t n, size_t *got) {
    RealFile *f = (RealFile *)base;
    size_t done = 0;
    while (done < n) {
        ssize_t r = pread(f->fd, (char *)buf + done, n - done, (off_t)(offset + done));
        if (r < 0) {
            if (errno == EINTR) continue;
            *got = done;
            return errno;
        }
        if (r == 0) break; /* end of file: a short read, not an error */
        done += (size_t)r;
    }
    *got = done;
    return 0;
}

static int real_write_at(GsqlFile *base, uint64_t offset, const void *buf, size_t n) {
    RealFile *f = (RealFile *)base;
    size_t done = 0;
    while (done < n) {
        ssize_t r = pwrite(f->fd, (const char *)buf + done, n - done, (off_t)(offset + done));
        if (r < 0) {
            if (errno == EINTR) continue;
            return errno;
        }
        done += (size_t)r;
    }
    return 0;
}

static int flush_fd(int fd) {
#if defined(__APPLE__)
    if (fcntl(fd, F_FULLFSYNC) == 0) return 0;
    /* Some filesystems refuse F_FULLFSYNC; fsync is then the best there is. */
    return fsync(fd) == 0 ? 0 : errno;
#elif defined(__linux__)
    return fdatasync(fd) == 0 ? 0 : errno;
#else
    return fsync(fd) == 0 ? 0 : errno;
#endif
}

static int real_sync(GsqlFile *base) { return flush_fd(((RealFile *)base)->fd); }

static int real_size(GsqlFile *base, uint64_t *out) {
    struct stat st;
    if (fstat(((RealFile *)base)->fd, &st) != 0) return errno;
    *out = (uint64_t)st.st_size;
    return 0;
}

static int real_truncate(GsqlFile *base, uint64_t n) {
    return ftruncate(((RealFile *)base)->fd, (off_t)n) == 0 ? 0 : errno;
}

static int real_close(GsqlFile *base) {
    RealFile *f = (RealFile *)base;
    int rc = close(f->fd) == 0 ? 0 : errno;
    free(f);
    return rc;
}

static int real_rename(GsqlIo *io, const char *from, const char *to) {
    (void)io;
    return rename(from, to) == 0 ? 0 : errno;
}

static int real_remove(GsqlIo *io, const char *path) {
    (void)io;
    return unlink(path) == 0 ? 0 : errno;
}

static int real_sync_dir(GsqlIo *io, const char *dir) {
    (void)io;
    int fd = open(dir[0] ? dir : ".", O_RDONLY
#ifdef O_DIRECTORY
                  | O_DIRECTORY
#endif
    );
    if (fd < 0) return errno;
    int rc = fsync(fd) == 0 ? 0 : errno;
    close(fd);
    return rc;
}

static int real_lock(GsqlFile *base, int nonblocking) {
    RealFile *f = (RealFile *)base;
    if (flock(f->fd, LOCK_EX | (nonblocking ? LOCK_NB : 0)) == 0) return 0;
    return errno == EAGAIN ? EWOULDBLOCK : errno;
}

static void real_free(GsqlIo *io) { free(io); }

static int real_mkdir(GsqlIo *io, const char *path) {
    (void)io;
    return mkdir(path, 0700) == 0 ? 0 : errno;
}

static int real_list(GsqlIo *io, const char *dir, int (*each)(void *, const char *), void *ctx) {
    (void)io;
    DIR *d = opendir(dir[0] ? dir : ".");
    if (!d) return errno;
    char **names = NULL;
    size_t n = 0, cap = 0;
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            char **p = realloc(names, cap * sizeof *p);
            if (!p) { rc = ENOMEM; break; }
            names = p;
        }
        if (!(names[n] = strdup(de->d_name))) { rc = ENOMEM; break; }
        n++;
    }
    closedir(d);
    if (rc) {
        for (size_t i = 0; i < n; i++) free(names[i]);
        free(names);
        return rc;
    }
    return gsql_io_walk_names(names, n, each, ctx);
}

static const GsqlIoOps real_ops = {
    real_open, real_read_at, real_write_at, real_sync, real_size, real_truncate,
    real_close, real_rename, real_remove, real_sync_dir, real_lock, real_free, urandom,
    real_mkdir, real_list, gsql_io_mono_ms, gsql_io_mono_sleep
};

#endif

GsqlIo *gsql_io_real(void) {
    GsqlIo *io = calloc(1, sizeof *io);
    if (io) io->ops = &real_ops;
    return io;
}

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
#include <bcrypt.h>
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <signal.h>
#include <stdint.h>
#include <wchar.h>
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

/* ONE BYTE AT OFFSET 2^62, NOT THE FILE'S CONTENTS -- and the first version got
 * this wrong. Windows byte-range locks are MANDATORY: locking the whole file
 * made every other handle's read or write of it FAIL, including the program's
 * own. `with lock(f)` locks the very file it then writes (examples/lock_test.gb),
 * so the write inside the block was lost -- measured, with nothing raised.
 *
 * Windows allows locking past end-of-file, and no real file reaches 2^62
 * bytes, so this byte never overlaps content: the lock excludes only another
 * locker, which is exactly flock's ADVISORY meaning. Every gBASIC process locks
 * the same byte, so mutual exclusion between them is unchanged. (SQLite locks
 * the same way, at a fixed offset outside the data.) LockFileEx needs the
 * OVERLAPPED even on a synchronous handle: it carries the range's offset. */
int gb_flock(int fd, int op) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.OffsetHigh = 0x40000000;     /* offset 2^62 */
    BOOL ok = (op == GB_LOCK_EXCLUSIVE)
                  ? LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov)
                  : UnlockFileEx(h, 0, 1, 0, &ov);
    return ok ? 0 : -1;
}

/* UTF-8 <-> UTF-16 for the W APIs. The caller frees. NULL on failure. */
static WCHAR *utf8_to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0) {
        return NULL;
    }
    WCHAR *w = malloc((size_t)n * sizeof(WCHAR));
    if (w && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}

static char *wide_to_utf8(const WCHAR *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) {
        return NULL;
    }
    char *s = malloc((size_t)n);
    if (s && WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL) <= 0) {
        free(s);
        return NULL;
    }
    return s;
}

/* Open, then ask the handle for its final name: that resolves symlinks and
 * junctions and fails for a path that does not exist, both as realpath does.
 * BACKUP_SEMANTICS is what lets CreateFileW open a DIRECTORY. The result
 * carries the \\?\ prefix, which is stripped (\\?\UNC\server\share becomes
 * \\server\share) so the answer reads as an ordinary path. */
char *gb_realpath(const char *path) {
    WCHAR *wpath = utf8_to_wide(path);
    if (!wpath) {
        return NULL;
    }
    HANDLE h = CreateFileW(wpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wpath);
    if (h == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    DWORD need = GetFinalPathNameByHandleW(h, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    WCHAR *final = need ? malloc((size_t)(need + 1) * sizeof(WCHAR)) : NULL;
    DWORD got = final ? GetFinalPathNameByHandleW(h, final, need + 1,
                                                  FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)
                      : 0;
    CloseHandle(h);
    if (got == 0 || got > need) {
        free(final);
        return NULL;
    }
    WCHAR *start = final;
    if (wcsncmp(start, L"\\\\?\\UNC\\", 8) == 0) {
        start += 6;        /* keep "\\" + "server\share..." */
        start[0] = L'\\';
    } else if (wcsncmp(start, L"\\\\?\\", 4) == 0) {
        start += 4;
    }
    char *out = wide_to_utf8(start);
    free(final);
    /* FORWARD SLASHES. gBASIC paths are written with `/` throughout -- file_name,
     * dir_name, "beside the loader", web.static's containment, every stdlib
     * library -- and every Windows API accepts `/`. Answering C:\Users\... made
     * file_name(real_path(p)) return the WHOLE PATH (measured,
     * examples/path_builtins_test.bas), so a portable program would break on
     * the one platform. C:/Users/... keeps it portable; a UNC share becomes
     * //server/share, which Windows also accepts. A backslash cannot be part of
     * a Windows file name, so nothing is lost by converting. */
    for (char *p = out; p && *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    return out;
}

int gb_set_cloexec(int fd, int on) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    return SetHandleInformation(h, HANDLE_FLAG_INHERIT, on ? 0 : HANDLE_FLAG_INHERIT) ? 0 : -1;
}

int gb_fsync(int fd) {
    return _commit(fd);
}

/* ACCESS_DENIED means the process exists and is not ours: alive, as EPERM is. */
int gb_process_alive(long pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) {
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    DWORD code = 0;
    BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return ok && code == STILL_ACTIVE;
}

/* signal() on Windows resets the disposition to SIG_DFL once a handler runs, so
 * the trampoline re-arms FIRST and then calls the real handler -- otherwise the
 * second Ctrl-C at the prompt would end the session. `restart` has no Windows
 * equivalent and is ignored; see the header. */
static void (*gb_signal_handlers[NSIG])(int);

static void gb_signal_trampoline(int sig) {
    signal(sig, gb_signal_trampoline);
    if (sig >= 0 && sig < NSIG && gb_signal_handlers[sig]) {
        gb_signal_handlers[sig](sig);
    }
}

int gb_on_signal(int sig, void (*handler)(int), int restart) {
    (void)restart;
    if (sig < 0 || sig >= NSIG) {
        return -1;
    }
    gb_signal_handlers[sig] = handler;
    return signal(sig, gb_signal_trampoline) == SIG_ERR ? -1 : 0;
}

/* ---- process.run: CreateProcessW, pipes, threads, a job ------------------- */

typedef struct {
    WCHAR *buf;
    size_t len;
    size_t cap;
} WBuf;

static int wbuf_put(WBuf *b, const WCHAR *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + n + 1) {
            cap *= 2;
        }
        WCHAR *next = realloc(b->buf, cap * sizeof(WCHAR));
        if (!next) {
            return 0;
        }
        b->buf = next;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, s, n * sizeof(WCHAR));
    b->len += n;
    b->buf[b->len] = 0;
    return 1;
}

static int wbuf_putc(WBuf *b, WCHAR c) {
    return wbuf_put(b, &c, 1);
}

/* ONE ARGUMENT, QUOTED SO THE CHILD'S C RUNTIME SPLITS IT BACK EXACTLY. Windows
 * has no argv: CreateProcess takes ONE string and the child re-parses it, so
 * an argument containing a space, a quote or a trailing backslash would arrive
 * as several arguments or a different one -- an ordinary-looking wrong answer.
 * The rules are the MS C runtime's (and CommandLineToArgvW's): backslashes are
 * literal UNLESS they precede a quote, where 2n backslashes + quote means n
 * backslashes and a closing quote, and 2n+1 means n backslashes and a literal
 * quote. Note cmd.exe parses its own command line differently again; this is
 * correct for ordinary programs, which is what process.run promises. */
static int append_quoted_arg(WBuf *b, const WCHAR *arg) {
    size_t n = wcslen(arg);
    if (n > 0 && !wcspbrk(arg, L" \t\n\v\"")) {
        return wbuf_put(b, arg, n);
    }
    if (!wbuf_putc(b, L'"')) {
        return 0;
    }
    size_t i = 0;
    for (;;) {
        size_t backslashes = 0;
        while (i < n && arg[i] == L'\\') {
            backslashes++;
            i++;
        }
        if (i == n) {
            for (size_t k = 0; k < backslashes * 2; k++) {
                if (!wbuf_putc(b, L'\\')) return 0;
            }
            break;
        }
        if (arg[i] == L'"') {
            for (size_t k = 0; k < backslashes * 2 + 1; k++) {
                if (!wbuf_putc(b, L'\\')) return 0;
            }
        } else {
            for (size_t k = 0; k < backslashes; k++) {
                if (!wbuf_putc(b, L'\\')) return 0;
            }
        }
        if (!wbuf_putc(b, arg[i])) return 0;
        i++;
    }
    return wbuf_putc(b, L'"');
}

/* The parent's environment with the edits applied, as a CREATE_UNICODE_ENVIRONMENT
 * block: NAME=VALUE entries, each NUL-terminated, then one more NUL. Windows
 * compares names case-insensitively, so an edit to "path" replaces "Path".
 * Entries beginning with '=' (the per-drive current directories cmd keeps) are
 * carried through untouched; an edit name can never match one, since process.run
 * refuses a name containing '='. NULL with *ok = 1 means "no edits: inherit". */
static WCHAR *build_env_block(const char *const *names, const char *const *values,
                              size_t count, int *ok) {
    *ok = 1;
    if (count == 0) {
        return NULL;
    }
    *ok = 0;
    WCHAR **wnames = calloc(count, sizeof(WCHAR *));
    WCHAR **wvalues = calloc(count, sizeof(WCHAR *));
    WCHAR *current = GetEnvironmentStringsW();
    WBuf out = {0};
    int good = wnames && wvalues && current;
    for (size_t i = 0; good && i < count; i++) {
        wnames[i] = utf8_to_wide(names[i]);
        wvalues[i] = values[i] ? utf8_to_wide(values[i]) : NULL;
        good = wnames[i] && (!values[i] || wvalues[i]);
    }
    for (const WCHAR *e = current; good && e && *e; e += wcslen(e) + 1) {
        int replaced = 0;
        for (size_t i = 0; i < count && !replaced; i++) {
            size_t nl = wcslen(wnames[i]);
            replaced = e[0] != L'=' && _wcsnicmp(e, wnames[i], nl) == 0 && e[nl] == L'=';
        }
        if (!replaced) {
            good = wbuf_put(&out, e, wcslen(e) + 1);
        }
    }
    for (size_t i = 0; good && i < count; i++) {
        if (wvalues[i]) {
            good = wbuf_put(&out, wnames[i], wcslen(wnames[i])) && wbuf_putc(&out, L'=') &&
                   wbuf_put(&out, wvalues[i], wcslen(wvalues[i])) && wbuf_putc(&out, 0);
        }
    }
    good = good && wbuf_putc(&out, 0);
    if (current) {
        FreeEnvironmentStringsW(current);
    }
    for (size_t i = 0; i < count; i++) {
        if (wnames) free(wnames[i]);
        if (wvalues) free(wvalues[i]);
    }
    free(wnames);
    free(wvalues);
    if (!good) {
        free(out.buf);
        return NULL;
    }
    *ok = 1;
    return out.buf;
}

/* One pipe, drained on its own thread. Anonymous pipes cannot be polled or
 * overlapped, so concurrency comes from threads: each blocks in ReadFile on
 * its own pipe, and neither can stall the other. */
typedef struct {
    HANDLE pipe;
    char  *data;
    size_t len;
    size_t cap;
    int    failed;
} PipeReader;

static DWORD WINAPI pipe_reader_main(LPVOID arg) {
    PipeReader *r = arg;
    char chunk[65536];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(r->pipe, chunk, sizeof chunk, &got, NULL)) {
            if (GetLastError() != ERROR_BROKEN_PIPE) {   /* broken pipe IS end of file */
                r->failed = 1;
            }
            break;
        }
        if (got == 0) {
            break;
        }
        if (r->len + got > r->cap) {
            size_t cap = r->cap ? r->cap * 2 : 65536;
            while (cap < r->len + got) {
                cap *= 2;
            }
            char *next = realloc(r->data, cap);
            if (!next) {
                r->failed = 1;
                break;
            }
            r->data = next;
            r->cap = cap;
        }
        memcpy(r->data + r->len, chunk, got);
        r->len += got;
    }
    return 0;
}

static void set_why_from_error(char *why, size_t why_size, DWORD code) {
    WCHAR *msg = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, code, 0, (LPWSTR)&msg, 0, NULL);
    char *utf8 = msg ? wide_to_utf8(msg) : NULL;
    if (msg) {
        LocalFree(msg);
    }
    if (utf8) {
        size_t n = strlen(utf8);
        while (n > 0 && (utf8[n - 1] == '\n' || utf8[n - 1] == '\r' || utf8[n - 1] == ' ')) {
            utf8[--n] = '\0';
        }
        snprintf(why, why_size, "%s", utf8);
        free(utf8);
    } else {
        snprintf(why, why_size, "Windows error %lu", (unsigned long)code);
    }
}

/* Jobs whose tree was still running when process.run returned -- something the
 * command started in the background. They are kept open, deliberately, so that
 * KILL_ON_JOB_CLOSE ends those processes when the interpreter exits (by any
 * means, including being killed outright): "nothing this interpreter started
 * outlives it". A job whose tree has fully exited is closed at once, so a loop
 * of process.run calls does not accumulate handles. */
static HANDLE *lingering_jobs = NULL;
static size_t lingering_count = 0;

static void retire_job(HANDLE job) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION acct;
    if (QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &acct,
                                  sizeof acct, NULL) &&
        acct.ActiveProcesses == 0) {
        CloseHandle(job);
        return;
    }
    HANDLE *next = realloc(lingering_jobs, (lingering_count + 1) * sizeof(HANDLE));
    if (next) {
        lingering_jobs = next;
        lingering_jobs[lingering_count++] = job;
    }
    /* realloc failing leaks the handle, which the OS closes at exit: the same
     * outcome, without the bookkeeping. */
}

/* ONE LAUNCH, shared by process.run and process.start so the two cannot
 * disagree about quoting, the environment, which handles a child inherits, or
 * the job it lives in.
 *
 * `in`, `out`, `err` are the child's three standard handles, already
 * inheritable; exactly these and nothing else reach it (POSIX's close-on-exec
 * discipline, by an explicit handle list). The child is created SUSPENDED,
 * put in a fresh Job Object with KILL_ON_JOB_CLOSE, then resumed -- so it is in
 * the job before it can start anything of its own. `extra_flags` adds creation
 * flags (process.start passes CREATE_NEW_PROCESS_GROUP; see gb_child_stop).
 *
 * Returns 0 with *pi (thread handle already closed) and *job filled -- *job may
 * be NULL where the system refused a job, in which case ending the child ends
 * that process alone -- or 1 with `why` saying why it could not be launched. */
static int win_launch(char *const argv[], const char *cwd,
                      const char *const *env_names, const char *const *env_values,
                      size_t env_count, HANDLE in, HANDLE out, HANDLE err,
                      DWORD extra_flags, PROCESS_INFORMATION *pi, HANDLE *job_out,
                      char *why, size_t why_size) {
    *job_out = NULL;
    memset(pi, 0, sizeof *pi);

    WBuf cmdline = {0};
    for (size_t i = 0; argv[i]; i++) {
        WCHAR *w = utf8_to_wide(argv[i]);
        int good = w && (i == 0 || wbuf_putc(&cmdline, L' ')) && append_quoted_arg(&cmdline, w);
        free(w);
        if (!good) {
            free(cmdline.buf);
            snprintf(why, why_size, "an argument is not valid UTF-8");
            return 1;
        }
    }
    WCHAR *wcwd = NULL;
    if (cwd && !(wcwd = utf8_to_wide(cwd))) {
        free(cmdline.buf);
        snprintf(why, why_size, "the working directory is not valid UTF-8");
        return 1;
    }
    int env_ok = 0;
    WCHAR *envblock = build_env_block(env_names, env_values, env_count, &env_ok);
    if (!env_ok) {
        free(cmdline.buf);
        free(wcwd);
        snprintf(why, why_size, "could not build the environment");
        return 1;
    }

    int ready = 1;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    HANDLE inherit[3] = { in, out, err };
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    attrs = malloc(attr_size);
    if (!attrs || !InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size)) {
        free(attrs);
        attrs = NULL;
        ready = 0;
    } else if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                          inherit, sizeof inherit, NULL, NULL)) {
        ready = 0;
    }

    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        memset(&li, 0, sizeof li);
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li)) {
            CloseHandle(job);
            job = NULL;
        }
    }

    BOOL launched = FALSE;
    DWORD launch_error = ERROR_NOT_ENOUGH_MEMORY;
    if (ready) {
        STARTUPINFOEXW si;
        memset(&si, 0, sizeof si);
        si.StartupInfo.cb = sizeof si;
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = in;
        si.StartupInfo.hStdOutput = out;
        si.StartupInfo.hStdError = err;
        si.lpAttributeList = attrs;
        launched = CreateProcessW(NULL, cmdline.buf, NULL, NULL, TRUE,
                                  CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                                      EXTENDED_STARTUPINFO_PRESENT | extra_flags,
                                  envblock, wcwd, &si.StartupInfo, pi);
        if (!launched) {
            launch_error = GetLastError();
        }
    }
    if (attrs) {
        DeleteProcThreadAttributeList(attrs);
        free(attrs);
    }
    free(cmdline.buf);
    free(wcwd);
    free(envblock);

    if (!launched) {
        if (job) CloseHandle(job);
        set_why_from_error(why, why_size, launch_error);
        return 1;
    }
    if (job && !AssignProcessToJobObject(job, pi->hProcess)) {
        CloseHandle(job);
        job = NULL;
    }
    ResumeThread(pi->hThread);
    CloseHandle(pi->hThread);
    pi->hThread = NULL;
    *job_out = job;
    return 0;
}

/* The parent's stdin, duplicated inheritable for a child -- inherited, as on
 * POSIX -- or NUL when there is none (no console). NULL only on failure. */
static HANDLE inheritable_stdin(void) {
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE std_in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE dup = NULL;
    if (std_in && std_in != INVALID_HANDLE_VALUE &&
        DuplicateHandle(GetCurrentProcess(), std_in, GetCurrentProcess(), &dup,
                        0, TRUE, DUPLICATE_SAME_ACCESS)) {
        return dup;
    }
    dup = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                      OPEN_EXISTING, 0, NULL);
    return dup == INVALID_HANDLE_VALUE ? NULL : dup;
}

/* A pipe whose CHILD end is inheritable and whose PARENT end is not. `child_reads`
 * says which end the child gets. Returns 1 on success. */
static int child_pipe(HANDLE *parent_end, HANDLE *child_end, int child_reads) {
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE r = NULL, w = NULL;
    if (!CreatePipe(&r, &w, &sa, 0)) {
        return 0;
    }
    *parent_end = child_reads ? w : r;
    *child_end = child_reads ? r : w;
    if (!SetHandleInformation(*parent_end, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(r);
        CloseHandle(w);
        return 0;
    }
    return 1;
}

int gb_run_capture(char *const argv[], const char *cwd,
                   const char *const *env_names, const char *const *env_values,
                   size_t env_count, long timeout_ms, GbRunResult *res) {
    memset(res, 0, sizeof *res);

    HANDLE out_r = NULL, out_w = NULL, err_r = NULL, err_w = NULL;
    HANDLE in_dup = NULL;
    int pipes = child_pipe(&out_r, &out_w, 0);
    pipes = pipes && child_pipe(&err_r, &err_w, 0);
    pipes = pipes && (in_dup = inheritable_stdin()) != NULL;
    if (!pipes) {
        if (out_r) CloseHandle(out_r);
        if (out_w) CloseHandle(out_w);
        if (err_r) CloseHandle(err_r);
        if (err_w) CloseHandle(err_w);
        snprintf(res->why, sizeof res->why, "could not create pipes");
        return 1;
    }

    PROCESS_INFORMATION pi;
    HANDLE job = NULL;
    int rc = win_launch(argv, cwd, env_names, env_values, env_count,
                        in_dup, out_w, err_w, 0, &pi, &job, res->why, sizeof res->why);
    /* Our copies of the child's ends: closed, or the readers never see EOF. */
    CloseHandle(out_w);
    CloseHandle(err_w);
    CloseHandle(in_dup);
    if (rc != 0) {
        CloseHandle(out_r);
        CloseHandle(err_r);
        return 1;
    }

    PipeReader out = { out_r, NULL, 0, 0, 0 };
    PipeReader err = { err_r, NULL, 0, 0, 0 };
    HANDLE threads[2];
    threads[0] = CreateThread(NULL, 0, pipe_reader_main, &out, 0, NULL);
    threads[1] = CreateThread(NULL, 0, pipe_reader_main, &err, 0, NULL);
    if (!threads[0] || !threads[1]) {
        /* Cannot drain: end the child rather than leave it blocked on a pipe. */
        if (job) TerminateJobObject(job, 1); else TerminateProcess(pi.hProcess, 1);
        if (threads[0]) { WaitForSingleObject(threads[0], INFINITE); CloseHandle(threads[0]); }
        if (threads[1]) { WaitForSingleObject(threads[1], INFINITE); CloseHandle(threads[1]); }
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(out_r);
        CloseHandle(err_r);
        if (job) CloseHandle(job);
        free(out.data);
        free(err.data);
        return -1;
    }

    /* The timeout covers the child AND the draining, as on POSIX: output still
     * arriving from something the child left behind counts against it too. */
    ULONGLONG deadline = timeout_ms >= 0 ? GetTickCount64() + (ULONGLONG)timeout_ms : 0;
    DWORD wait = WaitForSingleObject(pi.hProcess,
                                     timeout_ms >= 0 ? (DWORD)timeout_ms : INFINITE);
    if (wait == WAIT_OBJECT_0) {
        DWORD left = INFINITE;
        if (timeout_ms >= 0) {
            ULONGLONG now = GetTickCount64();
            left = now >= deadline ? 0 : (DWORD)(deadline - now);
        }
        wait = WaitForMultipleObjects(2, threads, TRUE, left);
    }
    if (wait == WAIT_TIMEOUT) {
        res->timed_out = 1;
        if (job) TerminateJobObject(job, 1); else TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    WaitForMultipleObjects(2, threads, TRUE, INFINITE);
    CloseHandle(threads[0]);
    CloseHandle(threads[1]);
    CloseHandle(out_r);
    CloseHandle(err_r);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (job) {
        retire_job(job);
    }

    if (out.failed || err.failed) {
        free(out.data);
        free(err.data);
        return -1;
    }
    res->exit_code = res->timed_out ? -1 : (int)code;
    res->out = out.data;
    res->out_len = out.len;
    res->err = err.data;
    res->err_len = err.len;
    return 0;
}

/* ---- named time zones: Windows' own ICU ------------------------------------ */

/* The few ICU C-API calls used, bound at first use from System32\icu.dll (the
 * combined, unversioned ICU Windows ships since 1903). Loaded rather than
 * linked so a Windows without it still RUNS gBASIC and only refuses zones. */
typedef void   *(*IcuCalOpen)(const WCHAR *zone, int32_t len, const char *locale,
                              int type, int *status);
typedef void    (*IcuCalClose)(void *cal);
typedef void    (*IcuCalSetMillis)(void *cal, double ms, int *status);
typedef int32_t (*IcuCalGet)(const void *cal, int field, int *status);
typedef int32_t (*IcuCalCanonical)(const WCHAR *id, int32_t len, WCHAR *result,
                                   int32_t cap, signed char *is_system, int *status);

static struct {
    int tried;
    int ok;
    IcuCalOpen open;
    IcuCalClose close;
    IcuCalSetMillis set_millis;
    IcuCalGet get;
    IcuCalCanonical canonical;
} icu;

enum { ICU_GREGORIAN = 1, ICU_ZONE_OFFSET = 15, ICU_DST_OFFSET = 16 };
#define ICU_FAILED(status) ((status) > 0)

static int icu_load(void) {
    if (icu.tried) {
        return icu.ok;
    }
    icu.tried = 1;
    HMODULE m = LoadLibraryExW(L"icu.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) {
        return 0;
    }
    /* Through a generic FUNCTION pointer, not void *: ISO C allows converting
     * one function pointer type to another, and not via an object pointer. */
    typedef void (*AnyFn)(void);
    icu.open = (IcuCalOpen)(AnyFn)GetProcAddress(m, "ucal_open");
    icu.close = (IcuCalClose)(AnyFn)GetProcAddress(m, "ucal_close");
    icu.set_millis = (IcuCalSetMillis)(AnyFn)GetProcAddress(m, "ucal_setMillis");
    icu.get = (IcuCalGet)(AnyFn)GetProcAddress(m, "ucal_get");
    icu.canonical = (IcuCalCanonical)(AnyFn)GetProcAddress(m, "ucal_getCanonicalTimeZoneID");
    icu.ok = icu.open && icu.close && icu.set_millis && icu.get && icu.canonical;
    return icu.ok;
}

/* KNOWN means ICU's own system data has it. Asked explicitly, because
 * ucal_open on an unknown name does NOT fail: it quietly opens "Etc/Unknown",
 * which is GMT -- the same silent fallback glibc's tzset makes, and the
 * reason the POSIX side checks /usr/share/zoneinfo before trusting TZ. */
int gb_zone_known(const char *zone) {
    if (!icu_load()) {
        return -1;
    }
    WCHAR *w = utf8_to_wide(zone);
    if (!w) {
        return 0;
    }
    WCHAR canon[128];
    signed char is_system = 0;
    int status = 0;
    icu.canonical(w, -1, canon, (int32_t)(sizeof canon / sizeof canon[0]), &is_system, &status);
    free(w);
    return !ICU_FAILED(status) && is_system;
}

int gb_zone_offset(const char *zone, long long utc_epoch, int *offset_seconds) {
    if (!icu_load()) {
        return -1;
    }
    WCHAR *w = utf8_to_wide(zone);
    if (!w) {
        return -1;
    }
    int status = 0;
    void *cal = icu.open(w, -1, "", ICU_GREGORIAN, &status);
    free(w);
    if (!cal || ICU_FAILED(status)) {
        if (cal) icu.close(cal);
        return -1;
    }
    icu.set_millis(cal, (double)utc_epoch * 1000.0, &status);
    int32_t zone_ms = icu.get(cal, ICU_ZONE_OFFSET, &status);
    int32_t dst_ms = icu.get(cal, ICU_DST_OFFSET, &status);
    icu.close(cal);
    if (ICU_FAILED(status)) {
        return -1;
    }
    *offset_seconds = (int)((zone_ms + dst_ms) / 1000);
    return 0;
}

/* ---- process.which: CreateProcess's search, answered without launching --- */

/* Is `w` an existing REGULAR file (not a directory)? */
static int which_is_file(const WCHAR *w) {
    DWORD attrs = GetFileAttributesW(w);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

/* Try dir + name (+ ".exe" when the name has no extension). On a hit, the
 * full path as UTF-8 with `/` separators; NULL otherwise. `dir` may be NULL
 * for "the name as given". */
static char *which_try(const WCHAR *dir, size_t dir_len, const WCHAR *name, int add_exe) {
    WBuf b = {0};
    int good = 1;
    if (dir && dir_len > 0) {
        good = wbuf_put(&b, dir, dir_len);
        if (good && dir[dir_len - 1] != L'\\' && dir[dir_len - 1] != L'/') {
            good = wbuf_putc(&b, L'\\');
        }
    }
    good = good && wbuf_put(&b, name, wcslen(name));
    if (good && add_exe) {
        good = wbuf_put(&b, L".exe", 4);
    }
    char *out = NULL;
    if (good && which_is_file(b.buf)) {
        WCHAR full[32768];
        DWORD n = GetFullPathNameW(b.buf, (DWORD)(sizeof full / sizeof full[0]), full, NULL);
        out = (n > 0 && n < sizeof full / sizeof full[0]) ? wide_to_utf8(full) : NULL;
        for (char *p = out; p && *p; p++) {
            if (*p == '\\') {
                *p = '/';
            }
        }
    }
    free(b.buf);
    return out;
}

char *gb_which(const char *name) {
    WCHAR *wname = utf8_to_wide(name);
    if (!wname || !wname[0]) {
        free(wname);
        return NULL;
    }
    /* CreateProcess appends ".exe" only when the LAST COMPONENT has no
     * extension at all; "tool.bat" and "tool.exe" are taken as given. */
    const WCHAR *last = wname;
    for (const WCHAR *p = wname; *p; p++) {
        if (*p == L'\\' || *p == L'/' || *p == L':') {
            last = p + 1;
        }
    }
    int add_exe = wcschr(last, L'.') == NULL;
    int is_path = last != wname;     /* a separator or a drive: not searched */

    char *hit = NULL;
    if (is_path) {
        hit = which_try(NULL, 0, wname, add_exe);
        free(wname);
        return hit;
    }

    /* The documented order: application dir, current dir, System32, System,
     * Windows, then PATH. */
    WCHAR dir[32768];
    DWORD n = GetModuleFileNameW(NULL, dir, (DWORD)(sizeof dir / sizeof dir[0]));
    if (n > 0 && n < sizeof dir / sizeof dir[0]) {
        WCHAR *slash = wcsrchr(dir, L'\\');
        if (slash) {
            hit = which_try(dir, (size_t)(slash - dir), wname, add_exe);
        }
    }
    if (!hit && (n = GetCurrentDirectoryW((DWORD)(sizeof dir / sizeof dir[0]), dir)) > 0 &&
        n < sizeof dir / sizeof dir[0]) {
        hit = which_try(dir, n, wname, add_exe);
    }
    if (!hit && (n = GetSystemDirectoryW(dir, (DWORD)(sizeof dir / sizeof dir[0]))) > 0 &&
        n < sizeof dir / sizeof dir[0]) {
        hit = which_try(dir, n, wname, add_exe);
    }
    WCHAR windir[MAX_PATH + 1];
    DWORD wn = GetWindowsDirectoryW(windir, MAX_PATH + 1);
    if (!hit && wn > 0 && wn <= MAX_PATH) {
        WCHAR sys16[MAX_PATH + 16];
        swprintf(sys16, sizeof sys16 / sizeof sys16[0], L"%ls\\System", windir);
        hit = which_try(sys16, wcslen(sys16), wname, add_exe);
        if (!hit) {
            hit = which_try(windir, wn, wname, add_exe);
        }
    }
    if (!hit) {
        DWORD need = GetEnvironmentVariableW(L"PATH", NULL, 0);
        WCHAR *path = need ? malloc((size_t)need * sizeof(WCHAR)) : NULL;
        if (path && GetEnvironmentVariableW(L"PATH", path, need) > 0) {
            WCHAR *cursor = path;
            while (!hit && cursor) {
                WCHAR *semi = wcschr(cursor, L';');
                size_t len = semi ? (size_t)(semi - cursor) : wcslen(cursor);
                /* A PATH entry may be quoted ("C:\Program Files\x"). An empty
                 * entry is skipped: unlike POSIX it does not mean the current
                 * directory, which CreateProcess already searched. */
                WCHAR *entry = cursor;
                if (len >= 2 && entry[0] == L'"' && entry[len - 1] == L'"') {
                    entry++;
                    len -= 2;
                }
                if (len > 0) {
                    hit = which_try(entry, len, wname, add_exe);
                }
                cursor = semi ? semi + 1 : NULL;
            }
        }
        free(path);
    }
    free(wname);
    return hit;
}

/* ---- process.start: the same launch, handed back running ----------------- */

int gb_child_start(char *const argv[], const char *cwd,
                   const char *const *env_names, const char *const *env_values,
                   size_t env_count, int want_stdin, GbChild *child,
                   char *why, size_t why_size) {
    memset(child, 0, sizeof *child);
    child->out_fd = child->err_fd = child->in_fd = -1;

    HANDLE out_r = NULL, out_w = NULL, err_r = NULL, err_w = NULL;
    HANDLE in_parent = NULL, in_child = NULL;
    int ok = child_pipe(&out_r, &out_w, 0) && child_pipe(&err_r, &err_w, 0);
    if (ok) {
        ok = want_stdin ? child_pipe(&in_parent, &in_child, 1)
                        : (in_child = inheritable_stdin()) != NULL;
    }
    if (!ok) {
        HANDLE all[] = { out_r, out_w, err_r, err_w, in_parent, in_child };
        for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
            if (all[i]) CloseHandle(all[i]);
        }
        snprintf(why, why_size, "could not create pipes");
        return 1;
    }

    PROCESS_INFORMATION pi;
    HANDLE job = NULL;
    int rc = win_launch(argv, cwd, env_names, env_values, env_count,
                        in_child, out_w, err_w, CREATE_NEW_PROCESS_GROUP,
                        &pi, &job, why, why_size);
    CloseHandle(out_w);
    CloseHandle(err_w);
    CloseHandle(in_child);
    if (rc != 0) {
        CloseHandle(out_r);
        CloseHandle(err_r);
        if (in_parent) CloseHandle(in_parent);
        return 1;
    }

    /* Ours become C-runtime fds: eval.c reads, writes and closes them with the
     * same calls it uses on POSIX, and closing the fd closes the handle. */
    child->out_fd = _open_osfhandle((intptr_t)out_r, _O_RDONLY | _O_BINARY);
    child->err_fd = _open_osfhandle((intptr_t)err_r, _O_RDONLY | _O_BINARY);
    child->in_fd = in_parent ? _open_osfhandle((intptr_t)in_parent, _O_WRONLY | _O_BINARY) : -1;
    child->pid = (long)pi.dwProcessId;
    child->process = pi.hProcess;
    child->job = job;
    return 0;
}

/* Anonymous pipes cannot be made non-blocking or polled, so ask how much is
 * there and read exactly that. A broken pipe with nothing left is end of
 * file: the child's end was closed (it exited, or closed its stdout). */
int gb_child_read(int fd, char *buf, size_t cap) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        return -2;
    }
    DWORD avail = 0;
    if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -2;
    }
    if (avail == 0) {
        return -1;
    }
    DWORD want = avail < cap ? avail : (DWORD)cap;
    DWORD got = 0;
    if (!ReadFile(h, buf, want, &got, NULL)) {
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -2;
    }
    return (int)got;
}

int gb_child_exited(GbChild *child, int *exit_code) {
    if (!child->process) {
        return 1;
    }
    if (WaitForSingleObject((HANDLE)child->process, 0) != WAIT_OBJECT_0) {
        return 0;
    }
    DWORD code = 0;
    GetExitCodeProcess((HANDLE)child->process, &code);
    *exit_code = (int)code;
    return 1;
}

void gb_child_stop(GbChild *child, int force) {
    if (!child->process) {
        return;
    }
    if (!force) {
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, (DWORD)child->pid);
        return;
    }
    if (child->job) {
        TerminateJobObject((HANDLE)child->job, 1);
    } else {
        TerminateProcess((HANDLE)child->process, 1);
    }
}

void gb_child_release(GbChild *child) {
    if (child->process) {
        CloseHandle((HANDLE)child->process);
        child->process = NULL;
    }
    if (child->job) {
        retire_job((HANDLE)child->job);
        child->job = NULL;
    }
}

/* BCryptGenRandom takes a ULONG count, so a large request is filled in
 * pieces; the system-preferred RNG needs no algorithm handle. */
int gb_secure_random(void *buf, size_t n) {
    unsigned char *p = buf;
    while (n > 0) {
        ULONG chunk = n > 0x40000000u ? 0x40000000u : (ULONG)n;
        if (BCryptGenRandom(NULL, p, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            return -1;
        }
        p += chunk;
        n -= chunk;
    }
    return 0;
}

/* The replacing rename. Wide on both sides so a UTF-8 path is not mangled by
 * the ANSI code page; GetLastError mapped onto the errno values the callers
 * already test, EXDEV above all. */
int gb_rename_replace(const char *from, const char *to) {
    WCHAR *wfrom = utf8_to_wide(from);
    WCHAR *wto = wfrom ? utf8_to_wide(to) : NULL;
    if (!wfrom || !wto) {
        free(wfrom);
        free(wto);
        errno = EINVAL;
        return -1;
    }
    BOOL ok = MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING);
    DWORD e = ok ? 0 : GetLastError();
    free(wfrom);
    free(wto);
    if (ok) {
        return 0;
    }
    switch (e) {
        case ERROR_NOT_SAME_DEVICE:   errno = EXDEV;  break;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:    errno = ENOENT; break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION: errno = EACCES; break;
        case ERROR_ALREADY_EXISTS:    errno = EEXIST; break;
        default:                      errno = EIO;    break;
    }
    return -1;
}

/* _IONBF, not _IOLBF: see the header -- Windows' _IOLBF is full buffering. */
void gb_stdout_line_buffered(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
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

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
#include <signal.h>
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

/* The whole range, blocking. LockFileEx needs the OVERLAPPED even for a
 * synchronous handle: its Offset fields are where the range starts. */
int gb_flock(int fd, int op) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    BOOL ok = (op == GB_LOCK_EXCLUSIVE)
                  ? LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &ov)
                  : UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, &ov);
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

static void set_why_from_error(GbRunResult *res, DWORD code) {
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
        snprintf(res->why, sizeof res->why, "%s", utf8);
        free(utf8);
    } else {
        snprintf(res->why, sizeof res->why, "Windows error %lu", (unsigned long)code);
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

int gb_run_capture(char *const argv[], const char *cwd,
                   const char *const *env_names, const char *const *env_values,
                   size_t env_count, long timeout_ms, GbRunResult *res) {
    memset(res, 0, sizeof *res);

    /* The command line. */
    WBuf cmdline = {0};
    for (size_t i = 0; argv[i]; i++) {
        WCHAR *w = utf8_to_wide(argv[i]);
        int good = w && (i == 0 || wbuf_putc(&cmdline, L' ')) && append_quoted_arg(&cmdline, w);
        free(w);
        if (!good) {
            free(cmdline.buf);
            snprintf(res->why, sizeof res->why, "an argument is not valid UTF-8");
            return 1;
        }
    }
    WCHAR *wcwd = NULL;
    if (cwd && !(wcwd = utf8_to_wide(cwd))) {
        free(cmdline.buf);
        snprintf(res->why, sizeof res->why, "the working directory is not valid UTF-8");
        return 1;
    }
    int env_ok = 0;
    WCHAR *envblock = build_env_block(env_names, env_values, env_count, &env_ok);
    if (!env_ok) {
        free(cmdline.buf);
        free(wcwd);
        snprintf(res->why, sizeof res->why, "could not build the environment");
        return 1;
    }

    /* Pipes: the child's ends inheritable, ours not. stdin is inherited, as on
     * POSIX; with no console stdin the child reads NUL. */
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE out_r = NULL, out_w = NULL, err_r = NULL, err_w = NULL, in_dup = NULL;
    int pipes = CreatePipe(&out_r, &out_w, &sa, 0) && CreatePipe(&err_r, &err_w, &sa, 0) &&
                SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0) &&
                SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
    HANDLE std_in = GetStdHandle(STD_INPUT_HANDLE);
    if (pipes && !(std_in && std_in != INVALID_HANDLE_VALUE &&
                   DuplicateHandle(GetCurrentProcess(), std_in, GetCurrentProcess(), &in_dup,
                                   0, TRUE, DUPLICATE_SAME_ACCESS))) {
        in_dup = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, NULL);
        if (in_dup == INVALID_HANDLE_VALUE) {
            in_dup = NULL;
            pipes = 0;
        }
    }

    /* Exactly these three handles reach the child, and nothing else that
     * happens to be inheritable -- POSIX's close-on-exec discipline. */
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    HANDLE inherit[3] = { in_dup, out_w, err_w };
    if (pipes) {
        SIZE_T attr_size = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
        attrs = malloc(attr_size);
        if (!attrs || !InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size)) {
            free(attrs);
            attrs = NULL;
            pipes = 0;
        } else if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                              inherit, sizeof inherit, NULL, NULL)) {
            pipes = 0;
        }
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

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    BOOL launched = FALSE;
    DWORD launch_error = ERROR_NOT_ENOUGH_MEMORY;
    if (pipes) {
        STARTUPINFOEXW si;
        memset(&si, 0, sizeof si);
        si.StartupInfo.cb = sizeof si;
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = in_dup;
        si.StartupInfo.hStdOutput = out_w;
        si.StartupInfo.hStdError = err_w;
        si.lpAttributeList = attrs;
        /* SUSPENDED so it is in the job before it can start anything itself. */
        launched = CreateProcessW(NULL, cmdline.buf, NULL, NULL, TRUE,
                                  CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                                      EXTENDED_STARTUPINFO_PRESENT,
                                  envblock, wcwd, &si.StartupInfo, &pi);
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
    /* Our copies of the child's ends: closed, or the readers never see EOF. */
    if (out_w) CloseHandle(out_w);
    if (err_w) CloseHandle(err_w);
    if (in_dup) CloseHandle(in_dup);

    if (!launched) {
        if (out_r) CloseHandle(out_r);
        if (err_r) CloseHandle(err_r);
        if (job) CloseHandle(job);
        set_why_from_error(res, launch_error);
        return 1;
    }
    if (job && !AssignProcessToJobObject(job, pi.hProcess)) {
        CloseHandle(job);    /* still runs; a timeout then ends only this process */
        job = NULL;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

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

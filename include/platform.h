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
#include <time.h>

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
 * `WSAPoll` TAKES SOCKETS ONLY, AND THAT TURNS OUT NOT TO BLOCK TIER 1 --
 * a CORRECTION to what this comment said when it was first written, on
 * 2026-09-29, after counting what the loop actually polls instead of assuming.
 *
 *   THE MAIN EVENT LOOP IS ALL SOCKETS: the listening socket, accepted clients,
 *   libcurl's transfer fds, and -- only when actors are in use -- the mailbox.
 *   Measured: the pollfd set it builds contains ZERO process pipes. So WSAPoll
 *   serves it, and the loop needs no redesign for Tier 1.
 *
 *   THE PIPES ARE SOMEWHERE ELSE. `process.run` and `process.poll` each poll a
 *   child's stdout/stderr in their OWN poll call, not in the event loop. Those
 *   two need a Windows mechanism of their own (overlapped I/O, or
 *   PeekNamedPipe), which is work confined to two functions rather than an
 *   architecture.
 *
 *   The first version of this note claimed the loop mixed pipes with sockets and
 *   therefore needed IOCP or WSAEventSelect-plus-threads. That was reasoned from
 *   "the tree polls pipes somewhere" and was wrong. It is recorded rather than
 *   quietly edited, because a scary estimate that nobody re-measures is how a
 *   port gets abandoned before it starts.
 *
 * Each returns 1 on success and 0 on failure.
 */
int gb_net_init(void);                        /* WSAStartup; nothing on POSIX */
int gb_sock_close(int fd);                    /* close vs closesocket */
int gb_sock_set_blocking(int fd, int blocking); /* fcntl vs ioctlsocket FIONBIO */

/* ---------------------------------------------------------------------------
 * PORTABLE C LIBRARY GAPS.
 *
 * Unlike the mechanisms above these are not design questions: each is a POSIX
 * function the Windows C runtime (UCRT) spells differently, with the same
 * meaning. They are here rather than as `#ifdef`s at their call sites because
 * eval.c calls most of them from several places, and one seam is one thing to
 * get right.
 */

/* setenv(name, value, 1) / unsetenv(name). Windows: _putenv_s, where an EMPTY
 * value removes the variable -- so Windows cannot hold an environment variable
 * set to the empty string, and gb_setenv(name, "") there is an unset. Returns 1
 * on success, 0 on failure. */
int gb_setenv(const char *name, const char *value);
int gb_unsetenv(const char *name);

/* localtime_r / gmtime_r: the thread-safe forms, writing into `out`. Return
 * `out`, or NULL on failure, as POSIX does. Windows: localtime_s / gmtime_s,
 * whose argument order is REVERSED and which return an errno rather than the
 * pointer -- the kind of difference a bare macro would get wrong silently. */
struct tm;
struct tm *gb_localtime(const time_t *t, struct tm *out);
struct tm *gb_gmtime(const time_t *t, struct tm *out);

/* timegm: civil fields interpreted as UTC, to epoch seconds. Not POSIX at all
 * (a BSD/glibc extension), which is why it needs a seam even on POSIX-like
 * systems that lack it. Windows: _mkgmtime. */
time_t gb_timegm(struct tm *tm);

/* mkdir(path, mode). Windows: _mkdir(path), which has no mode -- permissions
 * there are ACLs, and the 0777 every caller here passes means "the default",
 * which is what _mkdir gives. Returns 0 on success, -1 with errno set, exactly
 * like mkdir, because the callers test errno == EEXIST and that distinction is
 * what makes `make_dir` usable as a cross-process lock (see run_dir_builtins).
 *
 * UNVERIFIED ON WINDOWS: a path is UTF-8 in gBASIC and _mkdir takes the ANSI
 * code page unless the process runs under a UTF-8 code page. The intended
 * answer is the UTF-8 activeCodePage manifest (Windows 10 1903+), which fixes
 * every narrow-string path call in the tree at once rather than converting at
 * each one -- not yet built or measured. */
int gb_mkdir(const char *path, int mode);

/* flock(fd, LOCK_EX) / flock(fd, LOCK_UN): a whole-file advisory lock that
 * BLOCKS until granted and is released by the OS when the process dies --
 * which is what `with lock(...)` relies on (run_lock_signal.sh measured that
 * the kernel release is the whole of the cleanup). Windows: LockFileEx on ONE
 * BYTE far past any real end-of-file, because Windows locks are MANDATORY for
 * the range they cover and `with lock(f)` writes the very file it locks --
 * covering the contents made that write fail (measured; see platform_win32.c).
 * A byte no data reaches makes the lock advisory, as flock is. Returns 0 / -1. */
enum { GB_LOCK_RELEASE = 0, GB_LOCK_EXCLUSIVE = 1 };
int gb_flock(int fd, int op);

/* realpath(path, NULL): the canonical absolute path of an EXISTING file, with
 * every symlink resolved, malloc'd; NULL if it cannot be resolved. web.static's
 * containment check rests on "canonicalize, then compare" (run_web_routes.sh),
 * so a substitute that did not resolve links would serve files outside the
 * root. Windows: GetFinalPathNameByHandleW on an opened handle -- which, like
 * realpath, requires existence and follows links -- then UTF-8, with every
 * separator written as `/` (C:/Users/...) so that gBASIC's `/`-based path
 * handling works unchanged on Windows. */
char *gb_realpath(const char *path);

/* Set (on=1) or clear (on=0) close-on-exec / non-inheritance on an fd.
 * Windows: SetHandleInformation(HANDLE_FLAG_INHERIT) on the fd's OS handle.
 * Returns 0 / -1. */
int gb_set_cloexec(int fd, int on);

/* fsync. Windows: _commit. Returns 0 / -1. */
int gb_fsync(int fd);

/* Is a process with this id still running? kill(pid, 0) on POSIX, counting
 * EPERM as alive (it exists, it is just not ours). Windows: OpenProcess and
 * GetExitCodeProcess. Used by the prompt to tell a live session's cache file
 * from an orphan's. */
int gb_process_alive(long pid);

/* Install `handler` for `sig`. `restart`: a blocking read interrupted by the
 * signal resumes rather than failing with EINTR (SA_RESTART).
 * WINDOWS DIFFERS IN A WAY THAT MATTERS: signal() there resets the handler to
 * the default once it fires, so a second Ctrl-C would kill the prompt. The
 * Windows body re-arms before calling the handler. Windows has no SA_RESTART;
 * what a console read does on Ctrl-C there is UNMEASURED. Returns 0 / -1. */
int gb_on_signal(int sig, void (*handler)(int), int restart);

#ifdef _WIN32
/* process.run's LAUNCH-AND-DRAIN half, for Windows. POSIX keeps its fork/exec
 * path in src/eval.c unchanged; option parsing is shared, and the two will
 * converge on one seam when process.start is ported, once there are two real
 * implementations to shape it from rather than one and a guess.
 *
 * Runs argv[0] with argv[1..] (NULL-terminated), stdout and stderr captured,
 * stdin inherited. `cwd` may be NULL. The environment is the parent's MERGED
 * with env_names[i] = env_values[i] (a NULL value unsets) -- process.run's
 * documented `env` semantics, with names compared case-insensitively as Windows
 * does. timeout_ms < 0 means none.
 *
 * THE PROPERTIES KEPT, each the Windows form of a POSIX one:
 *   - the child runs in a JOB OBJECT with KILL_ON_JOB_CLOSE, so a timeout ends
 *     the whole tree (POSIX: kill the process group) and an interpreter killed
 *     outright takes its children with it (POSIX: PR_SET_PDEATHSIG);
 *   - exactly three handles are inherited, by an explicit handle list (POSIX:
 *     close-on-exec on everything else);
 *   - both pipes are drained concurrently, so a child filling one cannot
 *     deadlock against a parent reading the other (POSIX: poll over both).
 *
 * WHAT DIFFERS, and is reported rather than papered over:
 *   - there are no signals: `signal` is always 0, and a timed-out child reports
 *     exit_code -1 with timed_out true;
 *   - CreateProcess searches the application's directory and the CURRENT
 *     directory before PATH, and appends only ".exe" -- so a ".bat"/".cmd"
 *     script needs `cmd /c`, unlike execvp running a script with a shebang.
 *
 * Returns 0 when the child ran (res filled; res->out/err malloc'd, caller
 * frees), 1 when it could not be LAUNCHED (res->why says why; nothing to
 * free), -1 on an internal failure reading its output. */
typedef struct {
    int    exit_code;
    int    timed_out;
    char  *out;
    size_t out_len;
    char  *err;
    size_t err_len;
    char   why[512];
} GbRunResult;

int gb_run_capture(char *const argv[], const char *cwd,
                   const char *const *env_names, const char *const *env_values,
                   size_t env_count, long timeout_ms, GbRunResult *res);

/* process.start's LIVE CHILD, for Windows: the same launch as gb_run_capture
 * (one function, so the two cannot disagree), but handed back running.
 *
 * THE PIPES ARE C-RUNTIME FILE DESCRIPTORS (_open_osfhandle), so src/eval.c's
 * read/write/close on them work unchanged; what does not is non-blocking
 * reading, which gb_child_read provides with PeekNamedPipe. `in_fd` is -1
 * unless want_stdin (process.start's `stdin: "pipe"`); otherwise the child
 * inherits ours, as on POSIX.
 *
 * The child gets its own PROCESS GROUP (CREATE_NEW_PROCESS_GROUP), the Windows
 * form of POSIX's setpgid: it is what lets gb_child_stop address it, and, as on
 * POSIX, a Ctrl-C typed at the console is not delivered to it. */
typedef struct {
    long  pid;
    void *process;      /* HANDLE; NULL once released */
    void *job;          /* HANDLE or NULL */
    int   out_fd;
    int   err_fd;
    int   in_fd;
} GbChild;

/* NAMED TIME ZONES on Windows, through the ICU that ships with Windows 10 1903
 * and later (System32\icu.dll): the full IANA database, kept current by
 * Windows Update, and the same 1903 floor the UTF-8 manifest already sets.
 * POSIX uses /usr/share/zoneinfo and the TZ variable instead, neither of which
 * exists on Windows in a form an IANA name can use.
 *
 * gb_zone_known: 1 for a zone ICU knows, 0 for one it does not, -1 when ICU
 * itself is unavailable (Windows older than 1903) -- distinguished so the
 * caller does not report an absent DATABASE as a misspelled NAME.
 *
 * gb_zone_offset: the zone's total UTC offset (standard + daylight), in
 * seconds, at the UTC instant `utc_epoch`. 0 on success, -1 on failure. Both
 * civil-to-instant and instant-to-civil are built from this one question in
 * src/eval.c, so ICU is asked only what it is unambiguous about. */
int gb_zone_known(const char *zone);
int gb_zone_offset(const char *zone, long long utc_epoch, int *offset_seconds);

/* process.which: the path CreateProcess WOULD RUN for `name`, malloc'd, or NULL.
 *
 * The contract is POSIX's "the path execvp would run", and its point is that
 * asking first never disagrees with running -- so this follows CreateProcess's
 * own search and NOT PATHEXT, which CreateProcess ignores: a `which` that found
 * "tool.bat" through PATHEXT would answer yes for a command process.run then
 * cannot launch. Order, as CreateProcess documents it: the program's own
 * directory, the current directory, System32, the 16-bit System directory, the
 * Windows directory, then each PATH entry (`;`-separated, quotes stripped).
 * ".exe" is appended when the name's last component has no extension. A name
 * containing a separator or a drive is a path, checked as given (plus ".exe"
 * on the same rule). Only a REGULAR FILE answers -- never a directory. The
 * answer is written with `/`, like gb_realpath. */
char *gb_which(const char *name);

/* 0 started (*child filled), 1 could not be launched (why says why). */
int gb_child_start(char *const argv[], const char *cwd,
                   const char *const *env_names, const char *const *env_values,
                   size_t env_count, int want_stdin, GbChild *child,
                   char *why, size_t why_size);

/* NEVER BLOCKS. >0 bytes read into buf; 0 end of file (the child closed its end
 * and nothing is left); -1 nothing available right now; -2 an error. */
int gb_child_read(int fd, char *buf, size_t cap);

/* NEVER BLOCKS. 1 and *exit_code once the process has exited, else 0. */
int gb_child_exited(GbChild *child, int *exit_code);

/* force = 0: the POLITE stop -- CTRL_BREAK to the child's process group, the
 * nearest Windows has to SIGTERM; a child may handle it and keep running, which
 * is the same bargain SIGTERM strikes. Needs a shared console; without one it
 * does nothing, and the caller reports the child as still running.
 * force = 1: TerminateJobObject (the whole tree), the form of SIGKILL. */
void gb_child_stop(GbChild *child, int force);

/* Done with the handle: closes the process handle and RETIRES the job -- closed
 * at once if the tree has exited, otherwise kept so KILL_ON_JOB_CLOSE ends it
 * when the interpreter exits (the orphan bargain POSIX strikes in eval.c).
 * Does not touch the fds. Idempotent. */
void gb_child_release(GbChild *child);
#endif

/* Fill buf with n bytes from the operating system's CRYPTOGRAPHIC random
 * source -- secure_token, random_bytes and the unseeded RNG's seed all rest
 * on it. POSIX: /dev/urandom. Windows: BCryptGenRandom with the system
 * preferred RNG (there is no /dev/urandom, and secure_token failed outright
 * without this -- measured). Returns 0 when all n bytes were filled, -1
 * otherwise; never a partial fill reported as success. */
int gb_secure_random(void *buf, size_t n);

/* rename(from, to), REPLACING `to` if it exists, atomically on one volume --
 * which is what atomic_replace promises a reader ("the whole old file or the
 * whole new one"). POSIX rename does exactly that. Windows' C-runtime rename
 * REFUSES an existing target, so atomic_replace failed on every call that
 * mattered (measured, examples/nap_fs_test.gb); MoveFileExW with
 * MOVEFILE_REPLACE_EXISTING is the replacing rename. A cross-volume move is
 * refused with errno EXDEV on both, so callers keep their existing "same
 * filesystem" handling. Returns 0 / -1 with errno set. */
int gb_rename_replace(const char *from, const char *to);

/* Make stdout and stderr write bytes exactly as given. Called first thing in
 * main, before any output.
 *
 * WINDOWS: the C runtime opens both in TEXT mode, translating every "\n" into
 * "\r\n" underneath the program. gBASIC would be emitting LF and the file on
 * disk would hold CRLF, and every one of the ~680 byte-exact goldens in this
 * tree would fail for a reason invisible in the source. Binary mode makes
 * gBASIC write LF on every platform (windows_port_plan.md §7.1). POSIX: no-op.
 *
 * STDIN IS DELIBERATELY LEFT ALONE for now: in binary mode `input()` would see
 * the "\r" a Windows console line ends with. Unmeasured; decide it when
 * something reads stdin on Windows. */
void gb_stdio_binary(void);

/* What `--line-buffered` (and the prompt) ask of stdout: every completed line
 * leaves the process as it is printed. POSIX: setvbuf _IOLBF.
 *
 * WINDOWS CANNOT LINE-BUFFER: its setvbuf treats _IOLBF as FULL buffering, so
 * the flag silently did nothing -- MEASURED by tests/windows/process_start.bas,
 * where a child's first line stayed in its buffer until it exited. There it
 * is UNBUFFERED instead: at least as prompt (a partial line also leaves at
 * once), the same bytes, at the cost of more writes -- paid only by a program
 * that asked for the flag. */
void gb_stdout_line_buffered(void);

/* Which platform the bodies above came from, for diagnostics and for the tests
 * that need to say why a tier does not apply. */
const char *gb_platform_name(void);

#endif /* GBASIC_PLATFORM_H */

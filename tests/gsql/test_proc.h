/* What the SQL layer's harnesses (sql_owner_test.c, sql_stall_test.c) need
 * from a process, on POSIX and on Windows.
 *
 * ENDING AN OWNER THE PLANNED WAY. The owner a client starts is NOT that
 * client's child on either platform, so its exit status cannot be read:
 *
 *   POSIX: the client forks an intermediate that calls setsid and forks the
 *   owner, so the owner is a GRANDCHILD (src/gsql/client.c). The planned stop
 *   is SIGTERM; owner_end sends it and waits, within a bound, for the process
 *   to be gone. These harnesses used to waitpid() it as a child, which failed
 *   with ECHILD and left the status they then tested uninitialised -- the
 *   check passed by accident and measured nothing (2026-11-12).
 *
 *   Windows: the owner is started DETACHED (no console, its own process
 *   group), so there is no signal to send; its planned end is its LINGER --
 *   once no client is connected it leaves by itself after the linger (3 s by
 *   default). owner_end waits for that and, since a process handle can be
 *   opened on any process, reads the exit code too.
 *
 * 1 for "ended within the bound" (and, on Windows, with exit code 0), 0 for
 * anything else. An owner still running at the bound is ended outright, so a
 * failure leaves no process behind. */
#ifndef GSQL_TEST_PROC_H
#define GSQL_TEST_PROC_H

#include <sys/types.h>

#ifdef _WIN32
#include <windows.h>

#define OWNER_END_HOW "after its linger, with exit code 0"

static int owner_end(pid_t pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                           FALSE, (DWORD)pid);
    if (!h) {
        return 0;
    }
    DWORD code = 1;
    int clean = 0;
    if (WaitForSingleObject(h, 20000) == WAIT_OBJECT_0) {
        clean = GetExitCodeProcess(h, &code) && code == 0;
    } else {
        TerminateProcess(h, 1);
        WaitForSingleObject(h, 5000);
    }
    CloseHandle(h);
    return clean;
}
#else
#include <errno.h>
#include <signal.h>
#include <time.h>

#define OWNER_END_HOW "on SIGTERM"

static int owner_end(pid_t pid) {
    if (kill(pid, SIGTERM) != 0) {
        return 0;
    }
    struct timespec tick = { 0, 20 * 1000 * 1000 };
    for (int i = 0; i < 1000; i++) {          /* 20 s */
        if (kill(pid, 0) != 0 && errno == ESRCH) {
            return 1;
        }
        nanosleep(&tick, NULL);
    }
    kill(pid, SIGKILL);
    return 0;
}
#endif

#endif

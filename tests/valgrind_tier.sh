# One valgrind policy for every suite that has a valgrind tier. Sourced.
#
# WHY. 36 suites ran a valgrind tier through 12 distinct invocations: two
# different --error-exitcode values, --track-fds on seven of them and not the
# other twenty-nine, -q on four, and -- the one that actually changes what
# counts as a failure -- --errors-for-leak-kinds=definite on most but not all.
# Without that flag valgrind's default is `definite,possible`, so a suite that
# omitted it was running STRICTER than its neighbours, and which strictness a
# suite got was historical accident rather than a decision.
#
# The policy below is the one the suites all CLAIM in their own words: "no
# definite leak or invalid access". `possible` is not asserted, because an
# interior pointer into a live allocation is ordinary in a tree-walking
# interpreter and reporting it as a failure would be noise.
#
#   vg_run PROG ARGS...              the ordinary tier
#   vg_run_access_only PROG ARGS...  no leak claim at all -- see below
#   vg_available                     is valgrind installed
#   $VG_EXIT                         the exit code a valgrind failure produces
#   $VG_EXTRA                        extra flags, e.g. a suppression file
#
# The caller keeps its own env prefix and redirections, so the call sites read
# exactly as they did:
#
#   if GBASIC_PATH=stdlib vg_run ./gbasic tests/x.bas >/dev/null 2>"$f"; then
#
# ACCESS-ONLY IS A REAL DISTINCTION, NOT A LOOPHOLE, and it has three current
# users. run_odbc's driver manager dlopens libraries that leak by design, so
# its claim is no INVALID ACCESS and the leaks are suppressed by name in
# tests/odbc.supp. run_continuation's depth tier asks whether going past the
# fixed 64-entry opener stack writes out of bounds. run_smtp runs through
# libcurl. In each the tier greps for "Invalid" rather than trusting a leak
# count, which is why -q is kept here: it leaves only what that grep is for.

VG_EXIT=99
VG_FLAGS="--error-exitcode=$VG_EXIT --leak-check=full --errors-for-leak-kinds=definite --track-fds=yes"
VG_ACCESS_FLAGS="-q --error-exitcode=$VG_EXIT --leak-check=no --errors-for-leak-kinds=none"

# QT'S OWN s390x REPORTS, suppressed ON s390x ONLY: tests/qt.supp names five
# Qt functions in which valgrind reports a conditional jump on uninitialised
# data there with no gBASIC in the stack (tests/qt_valgrind_probe.cpp). Only
# where it was measured, because the entries would ALSO hide uninitialised
# text that we passed into Qt -- shaping it branches in the same functions --
# and on x86, where Qt reports nothing, every Qt tier keeps that check.
if [ "$(uname -m 2>/dev/null)" = s390x ]; then
    VG_FLAGS="$VG_FLAGS --suppressions=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/qt.supp"
    VG_ACCESS_FLAGS="$VG_ACCESS_FLAGS --suppressions=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/qt.supp"
fi
# AND ONE ON EVERY ARCHITECTURE: tests/qt_hash.supp names a 16-byte read in
# Qt's style sheet parser, under its seeded string hash, reproduced with no
# gBASIC by tests/qt_valgrind_probe.cpp. It names Qt's own frames only.
VG_FLAGS="$VG_FLAGS --suppressions=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/qt_hash.supp"
VG_ACCESS_FLAGS="$VG_ACCESS_FLAGS --suppressions=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/qt_hash.supp"
vg_available() { command -v valgrind >/dev/null 2>&1; }

# shellcheck disable=SC2086
vg_run() { valgrind $VG_FLAGS ${VG_EXTRA:-} "$@"; }

# shellcheck disable=SC2086
vg_run_access_only() { valgrind $VG_ACCESS_FLAGS ${VG_EXTRA:-} "$@"; }

# INSTRUCTIONS, NOT MEMORY: callgrind's count of the instructions a program ran,
# printed as one number (empty if valgrind gave none). It is here, not typed
# into a suite, so that every valgrind invocation stays in this file. A count
# repeats exactly where wall time does not (tests/run_profile.sh's `off` tier:
# one binary timed against its own copy ranged 0.81-1.01 under load). Its
# stdin is /dev/null and the program's own output is thrown away.
vg_count_instructions() {
    valgrind --tool=callgrind --callgrind-out-file=/dev/null "$@" 2>&1 >/dev/null </dev/null |
        awk '/Collected :/ { print $4 }'
}

# THREADS, NOT MEMORY: helgrind's race detector, for a suite whose program runs
# a thread of its own (tests/run_profile.sh: the sampler). Suppressions are the
# caller's, named in its arguments.
vg_run_helgrind() { valgrind --tool=helgrind --error-exitcode=0 ${VG_EXTRA:-} "$@"; }

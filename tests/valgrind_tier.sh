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
vg_available() { command -v valgrind >/dev/null 2>&1; }

# shellcheck disable=SC2086
vg_run() { valgrind $VG_FLAGS ${VG_EXTRA:-} "$@"; }

# shellcheck disable=SC2086

# shellcheck disable=SC2086
vg_run_access_only() { valgrind $VG_ACCESS_FLAGS ${VG_EXTRA:-} "$@"; }

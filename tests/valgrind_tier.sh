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
#   vg_run_leaks_only PROG ARGS...   the leak claim WITHOUT the fd claim -- see below
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
# LEAKS-ONLY DROPS EXACTLY ONE AXIS, --track-fds, AND KEEPS THE LEAK CLAIM, which
# is the opposite trade from access-only. Its user is run_gi: a program that pumps a
# GLib main loop ends with GLib's OWN event-loop descriptors open -- an eventfd from
# g_main_context_new_with_flags, and whatever g_bus_get_sync holds -- which valgrind
# reports at exit and which no gBASIC code opened or can close. MEASURED on a clean
# build: `definitely lost: 0 bytes`, and 4 errors, all of them open descriptors
# inside libglib and libgio.
#
# THIS IS NOT A SUPPRESSION CASE. A `.supp` file matches ERRORS by stack, which is
# how tests/odbc.supp hides driver-internal invalid reads; an fd left open at exit
# is a different report and is not suppressible that way. Dropping the flag for the
# one suite whose subject is a GLib main loop is narrower than the alternative of
# giving that suite no leak tier at all, which is where it was -- run_gi and
# run_datagrid had NO valgrind tier, which is how an owned Value inside a gi closure
# could have leaked with nothing in the gate to notice.
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
VG_LEAKS_FLAGS="--error-exitcode=$VG_EXIT --leak-check=full --errors-for-leak-kinds=definite"
VG_ACCESS_FLAGS="-q --error-exitcode=$VG_EXIT --leak-check=no --errors-for-leak-kinds=none"

vg_available() { command -v valgrind >/dev/null 2>&1; }

# shellcheck disable=SC2086
vg_run() { valgrind $VG_FLAGS ${VG_EXTRA:-} "$@"; }

# shellcheck disable=SC2086
vg_run_leaks_only() { valgrind $VG_LEAKS_FLAGS ${VG_EXTRA:-} "$@"; }

# shellcheck disable=SC2086
vg_run_access_only() { valgrind $VG_ACCESS_FLAGS ${VG_EXTRA:-} "$@"; }

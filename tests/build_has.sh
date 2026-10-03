# shellcheck shell=bash
# build_has MODULE -- is MODULE BUILT INTO the ./gbasic under test?
#
# Source this file, then:
#     if ! build_has sqlite; then printf 'SKIP ... (no sqlite in this build)\n'; exit 0; fi
#
# WHY THE BINARY IS ASKED AND NOT THE MACHINE. Suites used to ask pkg-config
# whether a library is INSTALLED, which is a different question from whether
# THIS gbasic was built with it -- and the two part company exactly where it
# matters. Measured 2026-10-03: MSYS2 carries sqlite3, libcrypto and libxml2 as
# other packages' dependencies while the Windows build deliberately leaves them
# out (they arrive milestone by milestone), so pkg-config said yes, the suites
# ran their tiers, and those tiers FAILED with "not available in this build" --
# a red gate reporting a correct build. On Linux the same split appears on any
# machine with a library's runtime but a build made without it (`make
# SQLITE3_AVAILABLE=0`). tests/run_finio_camt.sh already asked the binary; this
# is that answer, shared.
#
# THE RULE: run a probe that touches MODULE; it is ABSENT only when the binary
# answers with a BUILD REFUSAL -- "... not available in this build", or
# "... requires OpenSSL" for the libcrypto builtins, or the platform's own
# "... not available on Windows" for actors and listeners. Any other outcome means it
# is present, INCLUDING an ordinary error (the xlsx probe opens a file that does
# not exist, which a build WITH xlsx refuses for a different reason). Returns 0
# present, 1 absent, 2 when the probe could not run at all -- a missing binary
# must not read as "absent" and quietly skip a suite.
build_has() {
    local probe
    case "$1" in
        sqlite|pg|odbc|http|webclient|smtp|ldap|xml|gi) probe="load $1" ;;
        crypto)   probe='x = sha256("build_has")' ;;
        password) probe='x = password_hash("build_has")' ;;
        xlsx)     probe='x = xlsx.open("build_has_no_such.xlsx")' ;;
        # PLATFORM capabilities, not modules: always compiled in, refused at run
        # time where the platform cannot provide them (Windows, today). `self()`
        # opens the root mailbox, which is where that refusal lives. The listen
        # probe asks for a port that cannot exist, so a platform WITH listeners
        # refuses it for the ordinary reason and NOTHING IS EVER BOUND -- a probe
        # that really listened would leave the event loop serving after main.
        actors)   probe='x = self()' ;;
        listen)   probe=$'load webserver\ns = webserver.listen(70000)' ;;
        # POSIX SIGNALS -- a child that DIES BY a signal, with no cleanup run.
        # Windows has none: process.stop ends a child with a console control
        # event it handles and exits through. Not a refusal, so asked of the
        # environment the binary runs in, which Windows names in OS=Windows_NT
        # for every process; answered as a refusal so the rule below holds.
        signals)  probe=$'if env("OS") = "Windows_NT" then\n    error "signals are not available on Windows"\nend if' ;;
        # THE PROMPT'S LINE EDITOR (arrows, history, Ctrl-A/K): src/lineedit.c's
        # raw mode is termios, and on Windows `raw_on` declines so the prompt
        # reads plain lines. A known gap, asked the same way as signals.
        lineedit) probe=$'if env("OS") = "Windows_NT" then\n    error "line editing is not available on Windows yet"\nend if' ;;
        *) printf 'build_has: unknown module %s\n' "$1" >&2; return 2 ;;
    esac
    if [ ! -e ./gbasic ] && [ ! -e ./gbasic.exe ]; then
        printf 'build_has: no ./gbasic to ask about %s\n' "$1" >&2
        return 2
    fi
    local f out
    f="$(mktemp "${TMPDIR:-/tmp}/build_has.XXXXXX.bas")"
    printf '%s\n' "$probe" > "$f"
    out="$(./gbasic "$f" 2>&1)"
    rm -f "$f"
    # (`gi` words its refusal "gobject-introspection support is unavailable".)
    if printf '%s' "$out" | grep -qE 'not available in this build|requires OpenSSL|not available on Windows|support is unavailable'; then
        return 1
    fi
    return 0
}

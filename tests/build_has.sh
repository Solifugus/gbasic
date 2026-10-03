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
# "... requires OpenSSL" for the libcrypto builtins. Any other outcome means it
# is present, INCLUDING an ordinary error (the xlsx probe opens a file that does
# not exist, which a build WITH xlsx refuses for a different reason). Returns 0
# present, 1 absent, 2 when the probe could not run at all -- a missing binary
# must not read as "absent" and quietly skip a suite.
build_has() {
    local probe
    case "$1" in
        sqlite|pg|odbc|http|webclient|smtp|ldap|xml) probe="load $1" ;;
        crypto)   probe='x = sha256("build_has")' ;;
        password) probe='x = password_hash("build_has")' ;;
        xlsx)     probe='x = xlsx.open("build_has_no_such.xlsx")' ;;
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
    if printf '%s' "$out" | grep -qE 'not available in this build|requires OpenSSL'; then
        return 1
    fi
    return 0
}

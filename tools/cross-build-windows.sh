#!/usr/bin/env bash
# WHAT WOULD A WINDOWS BUILD COST? -- measured, not estimated.
#
#   ./tools/cross-build-windows.sh           report the surface
#   ./tools/cross-build-windows.sh --ratchet fail if it got WORSE
#
# Needs mingw-w64: `sudo apt install gcc-mingw-w64-x86-64`. Skips with a reason
# when it is absent, and says what is NOT being measured rather than printing
# nothing -- a silent skip reads exactly like a pass.
#
# THIS IS A PROBE, NOT A BUILD. It cannot produce a usable gbasic.exe and must
# not look as though it could:
#
#   * The shim headers below are WRITTEN INTO A TEMP DIRECTORY BY THIS SCRIPT
#     and thrown away. Some are honest (poll.h really is WSAPoll; sys/socket.h
#     really is winsock2), and some ARE LIES kept deliberately -- termios.h is
#     empty and regex.h is an API-shaped stub that does nothing. Committing
#     those to include/ would let somebody build a binary that COMPILES AND DOES
#     NOT WORK, which is the single worst artifact this port could produce.
#   * IT COMPILES. IT DOES NOT LINK, AND IT SAYS NOTHING ABOUT RUNNING.
#
# THE BIGGEST COST IS INVISIBLE TO THIS SCRIPT AND IS STATED IN ITS OUTPUT: on
# Windows A SOCKET IS NOT A FILE DESCRIPTOR. `read`/`write`/`close` on a socket
# fail at RUNTIME while compiling perfectly happily, and this tree does that in
# ~37 places. A falling error count here does not mean the port is closer if
# that seam has not moved.
#
# WHY A RATCHET rather than a pass/fail gate: the port is not done, so "does it
# compile" can only answer no for a long time. A number that may not RISE turns
# an unfinished port into something a gate can hold -- the same shape as a
# negative control.
set -uo pipefail
cd "$(dirname "$0")/.."

BASELINE_FILE="tools/cross-build-windows.baseline"
CC_WIN="${CC_WIN:-x86_64-w64-mingw32-gcc}"

if ! command -v "$CC_WIN" >/dev/null 2>&1; then
    echo "SKIP cross-build-windows: $CC_WIN not installed"
    echo "     (sudo apt install gcc-mingw-w64-x86-64) -- the Windows surface is NOT measured"
    exit 0
fi

shim="$(mktemp -d)"
trap 'rm -rf "$shim"' EXIT
mkdir -p "$shim/sys" "$shim/arpa" "$shim/netinet"

# Honest shims: these are what a real port would use.
printf '#pragma once\n#include <winsock2.h>\n#define poll WSAPoll\n'          > "$shim/poll.h"
printf '#pragma once\n#include <winsock2.h>\n#include <ws2tcpip.h>\n'         > "$shim/sys/socket.h"
printf '#pragma once\n#include <winsock2.h>\n#include <ws2tcpip.h>\n'         > "$shim/netinet/in.h"
printf '#pragma once\n#include <ws2tcpip.h>\n'                                > "$shim/arpa/inet.h"
# Deliberate lies, so the compiler gets past them and shows what is BEHIND them.
for lie in termios.h netdb.h sys/wait.h sys/select.h sys/ioctl.h sys/file.h sys/un.h; do
    printf '#pragma once\n/* PROBE STUB -- empty on purpose. A real port replaces this. */\n' > "$shim/$lie"
done
cat > "$shim/regex.h" <<'EOF'
#pragma once
/* PROBE STUB: API-shaped so eval.c compiles. It does NOT implement anything.
   A real port bundles an ERE engine -- Windows has no POSIX regex. */
#include <stddef.h>
typedef struct { size_t re_nsub; void *opaque; } regex_t;
typedef long regoff_t;
typedef struct { regoff_t rm_so, rm_eo; } regmatch_t;
#define REG_EXTENDED 1
#define REG_ICASE    2
#define REG_NEWLINE  4
#define REG_NOSUB    8
#define REG_NOTBOL  16
#define REG_NOTEOL  32
#define REG_NOMATCH  1
int regcomp(regex_t *, const char *, int);
int regexec(const regex_t *, const char *, size_t, regmatch_t *, int);
size_t regerror(int, const regex_t *, char *, size_t);
void regfree(regex_t *);
EOF

log="$(mktemp)"; trap 'rm -rf "$shim" "$log"' EXIT
echo "== cross-compiling for Windows with $($CC_WIN -dumpversion) =="
make clean >/dev/null 2>&1
make -k CC="$CC_WIN" PLATFORM_OBJ=src/platform_win32.o \
     CFLAGS="-std=c11 -Wall -Iinclude -I$shim -g -D_WIN32_WINNT=0x0601" \
     GTK_AVAILABLE=0 GIR_AVAILABLE=0 GIO_AVAILABLE=0 LIBPQ_AVAILABLE=0 ODBC_AVAILABLE=0 \
     LDAP_AVAILABLE=0 LIBCURL_AVAILABLE=0 LIBXCRYPT_AVAILABLE=0 LIBXML2_AVAILABLE=0 \
     LIBCRYPTO_AVAILABLE=0 LIBSSL_AVAILABLE=0 SQLITE3_AVAILABLE=0 ZLIB_AVAILABLE=0 \
     > "$log" 2>&1
errors=$(grep -c 'error:' "$log")
objs=$(ls src/*.o 2>/dev/null | wc -l)

echo
echo "-- objects that cross-compile UNMODIFIED: $objs --"
ls src/*.o 2>/dev/null | xargs -n1 basename 2>/dev/null | tr '\n' ' '; echo
echo
echo "-- errors by file --"
grep -oE "^[a-z/._]*\.c:[0-9]+:[0-9]+: error" "$log" | cut -d: -f1 | sort | uniq -c | sort -rn
echo
echo "-- distinct causes --"
grep -E "error" "$log" \
  | grep -oE "‘[A-Za-z_0-9]+’ undeclared|unknown type name ‘[A-Za-z_0-9]+’|too many arguments to function ‘[a-z_]+’|invalid use of undefined type ‘[a-z_ ]+’" \
  | sed "s/‘\([^’]*\)’/\1/" | sort -u | sed 's/^/   /'
echo
echo "-- %z, which MSVCRT's printf does not take (a wrong Content-Length is one of these) --"
echo "   $(grep -c 'unknown conversion type character ‘z’' "$log") warnings from $(grep -c '%zu' src/*.c | awk -F: '{t+=$2} END {print t}') occurrences in src/"
echo
echo "-- NOT MEASURED HERE --"
echo "   a socket is not a file descriptor on Windows: read/write/close on one"
echo "   fail at RUNTIME and compile cleanly. This tree does that in ~37 places,"
echo "   and no error count above will ever notice."
echo
echo "TOTAL ERRORS: $errors"

make clean >/dev/null 2>&1
if [ "${1:-}" = "--ratchet" ]; then
    [ -f "$BASELINE_FILE" ] || { echo "$errors" > "$BASELINE_FILE"; echo "baseline recorded: $errors"; exit 0; }
    base=$(cat "$BASELINE_FILE")
    if [ "$errors" -gt "$base" ]; then
        echo "FAIL the Windows surface GREW: $base -> $errors"
        echo "     (if that is intended, update $BASELINE_FILE in the same commit)"
        exit 1
    fi
    [ "$errors" -lt "$base" ] && echo "note: improved $base -> $errors; update $BASELINE_FILE to hold the gain"
    echo "PASS not worse than the recorded $base"
fi
exit 0

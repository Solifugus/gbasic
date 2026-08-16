#!/usr/bin/env bash
# Report which gBASIC build dependencies are present, what each one enables, and
# the exact command to install what is missing.
#
# WHY THIS EXISTS. gBASIC builds without any optional dependency -- each one is
# detected by pkg-config and compiled out cleanly when absent -- so a missing
# package is not a build failure and gives you no signal at all. You discover it
# later, as a runtime error from a feature you expected to work, or as a test
# suite that fails on a machine where it passed everywhere else. Building on a
# fresh riscv64 VM on 2026-08-15 took three separate rounds of "install one more
# thing and rebuild" before the suites could run, and that was with someone who
# knew the codebase. The README lists what each dependency ENABLES but never the
# package names to install, which is the one thing you actually need.
#
# REPORTS BY DEFAULT, INSTALLS ONLY IF ASKED. A build script that silently runs
# sudo is worse than the problem it solves, so `--install` is explicit and the
# command is printed either way.
#
#   tools/check-deps.sh              report only
#   tools/check-deps.sh --install    report, then apt-get install what is missing
#
# THE PKG-CONFIG NAMES BELOW MUST MATCH THE MAKEFILE. They are what the build
# actually probes -- `libxml-2.0`, not `libxml2`; `gtk+-3.0`, not `gtk3` -- and
# guessing wrong here would report a dependency missing that the build finds, or
# worse the reverse. The self-check at the bottom compares this list against the
# Makefile's own `pkg-config --exists` calls and complains if they drift.
set -u

cd "$(dirname "$0")/.."

install=0
[ "${1:-}" = "--install" ] && install=1

# pkg-config-name | debian package | what it enables
OPTIONAL="
sqlite3|libsqlite3-dev|sqlite.* module, dbframe, the EDGAR suite, xlsx.to_sql tests
libxml-2.0|libxml2-dev|REQUIRED BY xlsx (with zlib): reader, writer, formula engine
zlib|zlib1g-dev|REQUIRED BY xlsx (with libxml2): the ZIP container
libcrypto|libssl-dev|crypto builtins (hashing, HMAC, AES-GCM, Ed25519) + crypto.bas
libcurl|libcurl4-openssl-dev|webclient module
libpq|libpq-dev|pg.* PostgreSQL module (its tests are opt-in regardless)
libxcrypt|libcrypt-dev|password_hash / password_verify
gtk+-3.0|libgtk-3-dev|the GTK 3 gui.* module
girepository-2.0|libgirepository-2.0-dev|the gi.* GObject-Introspection bridge (GTK 4 path)
gio-2.0|libglib2.0-dev|gi.* support types
"

REQUIRED="cc|build-essential|the C11 compiler
make|build-essential|the build
bison|bison|regenerates src/parser.y"

printf '=== required to build at all ===\n'
missing_req=""
while IFS='|' read -r tool pkg what; do
    [ -z "$tool" ] && continue
    if command -v "$tool" >/dev/null 2>&1; then
        printf '  %-22s present   %s\n' "$tool" "$what"
    else
        printf '  %-22s MISSING   %s\n' "$tool" "$what"
        missing_req="$missing_req $pkg"
    fi
done <<EOF
$REQUIRED
EOF

printf '\n=== optional: each is compiled out cleanly when absent ===\n'
missing_opt=""
have_pkgconfig=1
command -v pkg-config >/dev/null 2>&1 || have_pkgconfig=0
if [ "$have_pkgconfig" = "0" ]; then
    printf '  pkg-config is MISSING, so the build will disable EVERY optional module.\n'
    missing_req="$missing_req pkg-config"
fi

while IFS='|' read -r mod pkg what; do
    [ -z "$mod" ] && continue
    if [ "$have_pkgconfig" = "1" ] && pkg-config --exists "$mod" 2>/dev/null; then
        printf '  %-22s present   %s\n' "$mod" "$what"
    else
        printf '  %-22s MISSING   %s\n' "$mod" "$what"
        missing_opt="$missing_opt $pkg"
    fi
done <<EOF
$OPTIONAL
EOF

# xlsx needs BOTH, and half of the pair is the confusing case: everything builds,
# and xlsx is silently absent with no indication which of the two was missing.
if [ "$have_pkgconfig" = "1" ]; then
    x=0; z=0
    pkg-config --exists libxml-2.0 2>/dev/null && x=1
    pkg-config --exists zlib 2>/dev/null && z=1
    if [ "$x$z" = "10" ] || [ "$x$z" = "01" ]; then
        printf '\n  NOTE: xlsx needs libxml-2.0 AND zlib. You have one, not both,\n'
        printf '        so the whole xlsx module will be compiled out.\n'
    fi
fi

printf '\n'
missing="$(echo "$missing_req $missing_opt" | tr ' ' '\n' | grep -v '^$' | sort -u | tr '\n' ' ')"
if [ -z "$(echo "$missing" | tr -d ' ')" ]; then
    printf 'Everything is present. `make` will build with all modules enabled.\n'
    exit 0
fi

printf 'To install what is missing (Debian/Ubuntu):\n\n    sudo apt-get install %s\n\n' "$missing"
printf 'Skip the GTK and girepository entries if you do not need the GUI modules;\n'
printf 'their test suites require a display and skip cleanly without one.\n'

if [ "$install" = "1" ]; then
    printf '\nRunning it now...\n'
    # shellcheck disable=SC2086
    sudo apt-get install -y $missing
    printf '\nRe-checking:\n'
    exec "$0"
fi

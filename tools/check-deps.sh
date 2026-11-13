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
drift=0

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
libssl|libssl-dev|TLS in net and the web server (with libcrypto)
libpq|libpq-dev|pg.* PostgreSQL module (its tests are opt-in regardless)
libxcrypt|libcrypt-dev|password_hash / password_verify
odbc|unixodbc-dev|odbc.* module (any ODBC-reachable database)
ldap|libldap-dev|ldap.* module (bind and search)
Qt6Widgets|qt6-base-dev qt6-base-dev-tools|the qt module and window declarations (needs moc too)
Qt6Svg|qt6-svg-dev|qt.render_svg (with Qt6Widgets)
"

REQUIRED="cc|build-essential|the C11 compiler
make|build-essential|the build
bison|bison|regenerates src/parser.y"

# libraries the build cannot do without, probed as the Makefile probes them
REQUIRED_LIBS="libpcre2-8|libpcre2-dev|regex (PCRE2), which is core"

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
while IFS='|' read -r mod pkg what; do
    [ -z "$mod" ] && continue
    if pkg-config --exists "$mod" 2>/dev/null; then
        printf '  %-22s present   %s\n' "$mod" "$what"
    else
        printf '  %-22s MISSING   %s\n' "$mod" "$what"
        missing_req="$missing_req $pkg"
    fi
done <<EOF
$REQUIRED_LIBS
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

# KDE Frameworks ship no .pc, so the Makefile looks for the header directory,
# and so does this (it rides Qt's switch: no Qt, no highlighter)
if [ -d /usr/include/KF6/KSyntaxHighlighting ] || [ -d /usr/local/include/KF6/KSyntaxHighlighting ]; then
    printf '  %-22s present   %s\n' "KF6SyntaxHighlighting" "the source widget's highlighting (with Qt)"
else
    printf '  %-22s MISSING   %s\n' "KF6SyntaxHighlighting" "the source widget's highlighting (with Qt)"
    missing_opt="$missing_opt libkf6syntaxhighlighting-dev"
fi

# THE SELF-CHECK the header promises: every library the Makefile probes (outside
# its comments) is in a list above. It had been promised and never written, and
# the lists had lost PCRE2, which the build requires, and six optional modules.
listed="$(printf '%s\n%s\n' "$OPTIONAL" "$REQUIRED_LIBS" | cut -d'|' -f1)"
for mod in $(grep -v '^[[:space:]]*#' Makefile | grep -oE 'pkg-config --(exists|libs) [A-Za-z0-9_.+-]+' | awk '{print $3}' | sort -u); do
    if ! printf '%s\n' "$listed" | grep -qx "$mod"; then
        printf '\n  DRIFT: the Makefile probes %s, which this script does not list\n' "$mod"
        drift=1
    fi
done

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
    exit $drift
fi

printf 'To install what is missing (Debian/Ubuntu):\n\n    sudo apt-get install %s\n\n' "$missing"

if [ "$install" = "1" ]; then
    printf '\nRunning it now...\n'
    # shellcheck disable=SC2086
    sudo apt-get install -y $missing
    printf '\nRe-checking:\n'
    exec "$0"
fi

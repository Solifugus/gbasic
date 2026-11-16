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
# command is printed either way. ./install.sh is the whole road (packages,
# build, install); this is the report.
#
#   tools/check-deps.sh              report only
#   tools/check-deps.sh --install    report, then install what is missing
#
# THE LIST IS tools/deps.list, shared with ./install.sh, naming each package on
# Debian/Ubuntu, Fedora and Arch (2026-11-15; it was Debian's names only).
#
# THE PKG-CONFIG NAMES IN IT MUST MATCH THE MAKEFILE. They are what the build
# actually probes -- `libxml-2.0`, not `libxml2`; `gtk+-3.0`, not `gtk3` -- and
# guessing wrong here would report a dependency missing that the build finds, or
# worse the reverse. The self-check at the bottom compares the list against the
# Makefile's own `pkg-config --exists` calls and complains if they drift.
set -u
drift=0

cd "$(dirname "$0")/.."

install=0
[ "${1:-}" = "--install" ] && install=1

. tools/deps.sh
distro=$(deps_distro)

# report GROUP...: one line per row; a missing row's packages join $missing,
# and $absent counts the missing rows whether or not packages are known
missing=""
absent=0
report() {
    local row kind probe what
    while IFS= read -r row; do
        [ -z "$row" ] && continue
        kind=$(printf '%s' "$row" | cut -d'|' -f1)
        probe=$(printf '%s' "$row" | cut -d'|' -f2)
        what=$(printf '%s' "$row" | cut -d'|' -f7)
        if deps_present "$kind" "$probe"; then
            printf '  %-22s present   %s\n' "${probe##*/}" "$what"
        else
            printf '  %-22s MISSING   %s\n' "${probe##*/}" "$what"
            absent=$((absent + 1))
            [ "$distro" != unknown ] && missing="$missing $(deps_packages "$distro" "$row")"
        fi
    done < <(deps_rows "$@")
}

printf '=== required to build at all ===\n'
report required
printf '\n=== optional: each is compiled out cleanly when absent ===\n'
command -v pkg-config >/dev/null 2>&1 ||
    printf '  pkg-config is MISSING, so the build will disable EVERY optional module.\n'
report optional qt

# THE SELF-CHECK the header promises: every library the Makefile probes (outside
# its comments) is in the list. It had been promised and never written, and
# the lists had lost PCRE2, which the build requires, and six optional modules.
listed="$(deps_rows required optional qt | awk -F'|' '$1 == "pc" { print $2 }')"
for mod in $(grep -v '^[[:space:]]*#' Makefile | grep -oE 'pkg-config --(exists|libs) [A-Za-z0-9_.+-]+' | awk '{print $3}' | sort -u); do
    if ! printf '%s\n' "$listed" | grep -qx "$mod"; then
        printf '\n  DRIFT: the Makefile probes %s, which tools/deps.list does not list\n' "$mod"
        drift=1
    fi
done

# xlsx needs BOTH, and half of the pair is the confusing case: everything builds,
# and xlsx is silently absent with no indication which of the two was missing.
if command -v pkg-config >/dev/null 2>&1; then
    x=0; z=0
    pkg-config --exists libxml-2.0 2>/dev/null && x=1
    pkg-config --exists zlib 2>/dev/null && z=1
    if [ "$x$z" = "10" ] || [ "$x$z" = "01" ]; then
        printf '\n  NOTE: xlsx needs libxml-2.0 AND zlib. You have one, not both,\n'
        printf '        so the whole xlsx module will be compiled out.\n'
    fi
fi

printf '\n'
if [ "$absent" = 0 ]; then
    printf 'Everything is present. `make` will build with all modules enabled.\n'
    exit $drift
fi
if [ "$distro" = unknown ]; then
    printf 'This is not Debian/Ubuntu, Fedora or Arch, so no package names are known here;\n'
    printf 'tools/deps.list names each dependency and its package on those three.\n'
    exit 1
fi

missing="$(echo "$missing" | tr ' ' '\n' | grep -v '^$' | sort -u | tr '\n' ' ')"
printf 'To install what is missing (%s):\n\n    sudo %s %s\n\n' "$distro" "$(deps_install_cmd "$distro")" "$missing"
printf 'Or ./install.sh, which installs them, then builds and installs gBASIC.\n'

if [ "$install" = "1" ]; then
    printf '\nRunning it now...\n'
    [ "$distro" = debian ] && sudo apt-get update
    # shellcheck disable=SC2086
    sudo $(deps_install_cmd "$distro") $missing
    printf '\nRe-checking:\n'
    exec "$0"
fi
exit $drift

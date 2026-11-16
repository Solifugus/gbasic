#!/usr/bin/env bash
# Build and install gBASIC from a clone: the packages it needs, then make, then
# make install, then a check that the installed gbasic runs.
#
#   ./install.sh                 every optional module; asks whether to add Qt
#   ./install.sh --minimal       only what the build cannot do without
#   ./install.sh --qt | --no-qt  answer the Qt question in advance
#   ./install.sh --system        install to /usr/local (with sudo), not ~/.local
#   ./install.sh --prefix DIR    install under DIR
#   ./install.sh --no-install    build ./gbasic and stop
#   ./install.sh --yes           run the package command without asking
#   ./install.sh --dry-run       print what would run, run nothing
#
# Debian/Ubuntu, Fedora and Arch; the packages are tools/deps.list, which
# tools/check-deps.sh reports on. Nothing runs under sudo without being shown
# first, and without a yes unless --yes was given.
set -u
cd "$(dirname "$0")"

minimal=0 qt=ask system=0 prefix="" no_install=0 yes=0 dry=0
while [ $# -gt 0 ]; do
    case "$1" in
        --minimal)    minimal=1 ;;
        --qt)         qt=yes ;;
        --no-qt)      qt=no ;;
        --system)     system=1 ;;
        --prefix)     [ $# -ge 2 ] || { echo "install.sh: --prefix needs a directory" >&2; exit 2; }
                      prefix="$2"; shift ;;
        --prefix=*)   prefix="${1#--prefix=}" ;;
        --no-install) no_install=1 ;;
        --yes|-y)     yes=1 ;;
        --dry-run)    dry=1 ;;
        -h|--help)    sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)            echo "install.sh: unknown option $1 (--help lists them)" >&2; exit 2 ;;
    esac
    shift
done
if [ -z "$prefix" ]; then
    if [ "$system" = 1 ]; then prefix=/usr/local; else prefix="$HOME/.local"; fi
fi

. tools/deps.sh
distro=$(deps_distro)

# AS ROOT, as in a container, there is no sudo to ask and nothing to ask for
sudo=""
[ "$(id -u)" = 0 ] || sudo=sudo

say() { printf '%s\n' "$*"; }
run() {   # show it; run it unless this is a dry run
    printf '  $ %s\n' "$*"
    [ "$dry" = 1 ] || "$@"
}
ask() {   # ask QUESTION DEFAULT(y|n) -> 0 for yes; no terminal takes the default
    local a d="$2" hint="[y/N]"
    [ "$d" = y ] && hint="[Y/n]"
    if [ ! -t 0 ]; then
        [ "$d" = y ]; return
    fi
    printf '%s %s ' "$1" "$hint"
    read -r a || a=""
    case "${a:-$d}" in y|Y|yes|Yes) return 0 ;; *) return 1 ;; esac
}

# --- 1. which packages ---------------------------------------------------------
groups="required"
[ "$minimal" = 1 ] || groups="required optional"

qt_missing=0
while IFS= read -r row; do
    [ -z "$row" ] && continue
    deps_present "$(printf '%s' "$row" | cut -d'|' -f1)" "$(printf '%s' "$row" | cut -d'|' -f2)" || qt_missing=1
done < <(deps_rows qt)

if [ "$minimal" = 0 ] && [ "$qt_missing" = 1 ]; then
    if [ "$qt" = ask ]; then
        say "Qt 6 adds the qt module: windows, dialogs and a code editor widget."
        say "It is a large install (a few hundred MB). Without it, everything else still works."
        if [ ! -t 0 ]; then
            say "No terminal to ask on, so no Qt (--qt adds it)."
            qt=no
        elif ask "Install Qt too?" n; then
            qt=yes
        else
            qt=no
        fi
    fi
    [ "$qt" = yes ] && groups="$groups qt"
elif [ "$minimal" = 0 ] && [ "$qt_missing" = 0 ]; then
    say "Qt is already installed, so the qt module will be built."
fi

# the rows asked for that are not on this machine
absent="" absent_rows=""
while IFS= read -r row; do
    [ -z "$row" ] && continue
    deps_present "$(printf '%s' "$row" | cut -d'|' -f1)" "$(printf '%s' "$row" | cut -d'|' -f2)" && continue
    absent="$absent $(printf '%s' "$row" | cut -d'|' -f2 | sed 's|.*/||')"
    absent_rows="$absent_rows$row
"
done < <(deps_rows $groups)

# --- 2. install them -----------------------------------------------------------
if [ -n "${absent# }" ]; then
    say ""
    say "Missing:${absent}"
    if [ "$distro" = unknown ]; then
        say "This is not Debian/Ubuntu, Fedora or Arch, so this script does not know the"
        say "package names here. tools/deps.list names every dependency, what it enables"
        say "and its package on those three; install the equivalents and run this again."
        # only the required ones stop the build
        req_absent=0
        while IFS= read -r row; do
            [ -z "$row" ] && continue
            deps_present "$(printf '%s' "$row" | cut -d'|' -f1)" "$(printf '%s' "$row" | cut -d'|' -f2)" || req_absent=1
        done < <(deps_rows required)
        [ "$req_absent" = 0 ] || exit 1
        say "(The required ones are present, so building without the rest.)"
    elif [ "$yes" = 1 ] || [ "$dry" = 1 ] || ask "Install them with $(deps_install_cmd "$distro" | cut -d' ' -f1)${sudo:+ (using sudo)}?" y; then
        # A FRESH DEBIAN/UBUNTU HAS NO PACKAGE LISTS until an update, and both
        # choosing between alternatives and knowing what this release packages
        # need them, so the update comes first.
        [ "$distro" = debian ] && { run $sudo apt-get update || { say "apt-get update failed."; exit 1; }; }
        # A FRESH ARCH has no sync database, and installing against none fails
        if [ "$distro" = arch ] && [ "$dry" = 0 ] && ! pacman -Si pcre2 >/dev/null 2>&1; then
            say "pacman has no package database yet; run 'sudo pacman -Syu' first, then this again."
            exit 1
        fi
        missing="" unpackaged=""
        while IFS= read -r row; do
            [ -z "$row" ] && continue
            for p in $(deps_packages "$distro" "$row"); do
                # A PACKAGE THIS RELEASE DOES NOT HAVE would make the whole install
                # refuse (Ubuntu 22.04 and Debian 12 package no KF6), so it is left
                # out and named, unless the build cannot do without it
                if [ "$dry" = 1 ] || _deps_has_candidate "$distro" "$p"; then
                    missing="$missing $p"
                elif [ "$(printf '%s' "$row" | cut -d'|' -f6)" = required ]; then
                    say "This release has no package $p, which the build needs."
                    exit 1
                else
                    unpackaged="$unpackaged $p ($(printf '%s' "$row" | cut -d'|' -f2 | sed 's|.*/||'))"
                fi
            done
        done <<< "$absent_rows"
        missing="$(printf '%s\n' $missing | sort -u | tr '\n' ' ')"
        missing="${missing% }"
        [ -n "$unpackaged" ] && say "Not packaged on this release, so left out:$unpackaged"
        if [ -n "$missing" ]; then
            # shellcheck disable=SC2086
            run $sudo $(deps_install_cmd "$distro") $missing ||
                { say "The package install failed; nothing was built."; exit 1; }
        fi
    else
        say "Not installed. Building with what is present: missing optional modules are left out."
        say "(tools/check-deps.sh prints the command to install them.)"
    fi
else
    say "Every dependency asked for is already installed."
fi

# --- 3. build ------------------------------------------------------------------
jobs=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
say ""
say "Building:"
run make -j"$jobs" || { say "The build failed; the first error above is the one to read."; exit 1; }
[ "$dry" = 1 ] || [ -x ./gbasic ] || { say "make finished but left no ./gbasic"; exit 1; }

if [ "$no_install" = 1 ]; then
    [ "$dry" = 1 ] || say "Built ./gbasic ($(./gbasic --version)). Not installed (--no-install)."
    exit 0
fi

# --- 4. install ----------------------------------------------------------------
say ""
say "Installing under $prefix:"
# sudo only where this user cannot write: the prefix, or the nearest part of it
# that exists (a fresh ~/.local may not)
inst_sudo=""
d="$prefix"
while [ ! -d "$d" ] && [ "$d" != / ]; do d=$(dirname "$d"); done
[ -w "$d" ] || inst_sudo="$sudo"
run $inst_sudo make install PREFIX="$prefix" || { say "make install failed."; exit 1; }
[ "$dry" = 1 ] && exit 0

# --- 5. does it run ------------------------------------------------------------
bin="$prefix/bin/gbasic"
if ! "$bin" --version >/dev/null 2>&1; then
    say "Installed, but $bin --version did not run."
    exit 1
fi
# A PROGRAM THAT LOADS A STANDARD LIBRARY, run from outside the tree with no
# GBASIC_PATH: a binary that starts but cannot find its stdlib is the install
# failure worth catching, and `--version` alone cannot see it.
# IN A DIRECTORY OF ITS OWN: a library is also looked for beside the loading
# file, and a probe in a shared /tmp met another tree's stdlib there (warning
# 2103 on the first run of this script).
probe=$(mktemp -d "${TMPDIR:-/tmp}/gbasic-install.XXXXXX") || exit 1
printf 'load notation\nprint notation.from_text(notation.to_text({ hello: "from gBASIC" })).hello\n' > "$probe/probe.bas"
out=$(cd "$probe" && env -u GBASIC_PATH "$bin" probe.bas 2>&1)
rm -rf "$probe"
say ""
say "$("$bin" --version) is installed at $bin"
if [ "$out" != "from gBASIC" ]; then
    say "but a program loading a standard library did not run from outside the source tree:"
    say "    $out"
    exit 1
fi
# the modules whose presence depends on a library (the rest are always built)
caps=$("$bin" --capabilities | cut -f1)
built="" left=""
for m in sqlite pg odbc ldap webclient http smtp xml xlsx zip qt; do
    if printf '%s\n' "$caps" | grep -qx "$m"; then built="$built $m"; else left="$left $m"; fi
done
say "Optional modules built:${built:- none}"
[ -n "$left" ] && say "Left out (their libraries are not installed):$left"
case ":$PATH:" in
    *":$prefix/bin:"*) say "Run it as: gbasic program.bas" ;;
    *) say ""
       say "$prefix/bin is not on your PATH. Add it, for example:"
       say "    echo 'export PATH=\"$prefix/bin:\$PATH\"' >> ~/.bashrc"
       say "and open a new terminal; until then run $bin." ;;
esac

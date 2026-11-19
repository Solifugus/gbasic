#!/usr/bin/env bash
# ./install.sh and tools/check-deps.sh over tools/deps.list: from a clone to an
# installed gbasic on Debian/Ubuntu, Fedora and Arch.
#
# Tiers:
#   1. LIST -- every row of tools/deps.list well formed, and its pkg-config
#      probes the Makefile's exactly (check-deps.sh's self-check, both ways).
#   2. DISTRO -- os-release files of twelve distributions read as the family
#      whose package names apply, or as unknown.
#   3. COMMANDS -- with every library hidden from pkg-config, a dry run's
#      install command for each family, and what --minimal, --qt, --no-qt and
#      a run with no terminal (no Qt, saying so) put in it.
#   4. UNKNOWN -- a distribution it has no names for refuses when a required
#      library is missing, and says where the names are.
#   5. INSTALL -- a real run here into a scratch prefix: gbasic installed,
#      a program loading a standard library run from outside the tree, the
#      optional modules named; --no-install installs nothing.
#   6. CONTAINERS (opt-in, INSTALL_CONTAINER_TEST=1: it downloads images and
#      packages) -- ./install.sh --qt --yes in fresh Ubuntu 22.04 and 24.04,
#      Debian 12, Fedora and Arch containers, from a copy of this tree, each
#      held to the modules its build reports. The proof that deps.list's names
#      are real is this tier; without it they are only read.
# GATE-LANE: exclusive -- installs into a scratch prefix with make install PREFIX=..., and the install path is compiled in, so it recompiles and relinks ./gbasic in this tree (its first gate, 2026-11-15, broke eight suites beside it)
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -u
cd "$(dirname "$0")/.."
root=$(pwd)
work=$(mktemp -d)
# AND PUTS THE TREE BACK: a ./gbasic built for the scratch prefix would look
# for its installed stdlib in a directory this trap has just removed
trap 'rm -rf "$work"; make >/dev/null 2>&1' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }
has()  { case "$2" in *"$1"*) return 0 ;; esac; return 1; }

# os-release files, by the fields deps_distro reads
osr() { printf 'ID=%s\nID_LIKE="%s"\n' "$2" "$3" > "$work/$1.os"; }
osr ubuntu ubuntu debian;   osr debian debian "";        osr pop pop "ubuntu debian"
osr mint linuxmint "ubuntu debian"; osr fedora fedora ""; osr rocky rocky "rhel centos fedora"
osr alma almalinux "rhel centos fedora"; osr arch arch ""; osr manjaro manjaro arch
osr endeavour endeavouros arch; osr suse opensuse-tumbleweed "opensuse suse"
osr rhel rhel "fedora"; printf 'VERSION_ID="10.3"\n' >> "$work/rhel.os"

printf 'TIER list: tools/deps.list is well formed and is the Makefile'"'"'s\n'
bad=$(grep -v '^[[:space:]]*#' tools/deps.list | grep -v '^[[:space:]]*$' | awk -F'|' '
    NF != 7 { print "fields " NR ": " $0; next }
    $1 !~ /^(tool|pc|dir)$/ { print "kind " $1 }
    $6 !~ /^(required|optional|qt)$/ { print "group " $6 }
    $3 == "" || $4 == "" || $5 == "" { print "a distribution with no package: " $2 }')
[ -z "$bad" ] && pass "every row has a kind, three package columns, a group and a purpose" || fail "malformed: $bad"
out=$(bash tools/check-deps.sh 2>&1); rc=$?
if ! has DRIFT "$out" && [ "$rc" = 0 ]; then
    pass "every library the Makefile probes is listed (check-deps.sh's self-check)"
else
    fail "check-deps.sh: rc $rc, $(printf '%s' "$out" | grep DRIFT)"
fi
# THE OTHER DIRECTION, which the self-check does not ask: a listed pc probe the
# Makefile never makes is a package installed for nothing
probes=$(grep -v '^[[:space:]]*#' Makefile | grep -oE 'pkg-config --(exists|libs|variable=[a-z]+) [A-Za-z0-9_.+-]+' | awk '{print $3}' | sort -u)
extra=""
for p in $(grep -v '^[[:space:]]*#' tools/deps.list | awk -F'|' '$1 == "pc" { print $2 }'); do
    printf '%s\n' "$probes" | grep -qx "$p" || extra="$extra $p"
done
[ -z "$extra" ] && pass "and every listed library is one the Makefile probes" || fail "listed but never probed:$extra"
# CONTROL: the self-check sees a probe taken out of the list
sed '/^pc|zlib|/d' tools/deps.list > "$work/deps.list"
out=$(GB_DEPS_LIST="$work/deps.list" bash tools/check-deps.sh 2>&1)
has "DRIFT: the Makefile probes zlib" "$out" && pass "CONTROL: with zlib's row removed, the self-check names it" ||
    fail "CONTROL: the self-check did not see zlib's row removed"

printf 'TIER distro: os-release read as the family whose names apply\n'
want="ubuntu:debian debian:debian pop:debian mint:debian fedora:fedora rocky:fedora alma:fedora arch:arch manjaro:arch endeavour:arch rhel:fedora suse:unknown"
wrong=""
for pair in $want; do
    got=$(GB_OS_RELEASE="$work/${pair%%:*}.os" bash -c '. tools/deps.sh; deps_distro')
    [ "$got" = "${pair#*:}" ] || wrong="$wrong ${pair%%:*}=$got"
done
[ -z "$wrong" ] && pass "12 distributions, each its family (openSUSE unknown)" || fail "read wrongly:$wrong"
[ "$(GB_OS_RELEASE="$work/none.os" bash -c '. tools/deps.sh; deps_distro')" = unknown ] &&
    pass "and no os-release at all is unknown" || fail "a missing os-release was not unknown"
# RHEL and its rebuilds keep some -devel packages in a repository shipped
# disabled (unixODBC-devel on RHEL 10.3, gbasic books 2026-11-18); install.sh
# names the command that enables it, and nothing on other families
hint() { GB_OS_RELEASE="$work/$1.os" bash -c '. tools/deps.sh; deps_extra_repo_hint'; }
[ "$(hint rhel)" = "sudo subscription-manager repos --enable codeready-builder-for-rhel-10-$(uname -m)-rpms" ] &&
[ "$(hint rocky)" = "sudo dnf config-manager --set-enabled crb" ] &&
[ "$(hint alma)" = "sudo dnf config-manager --set-enabled crb" ] &&
    pass "RHEL is told CodeReady Builder's command (its major version, this arch), Rocky and Alma crb" ||
    fail "repository hint: rhel [$(hint rhel)] rocky [$(hint rocky)] alma [$(hint alma)]"
quiet=""
for d in fedora ubuntu debian arch suse; do hint "$d" >/dev/null && quiet="$quiet $d"; done
[ -z "$quiet" ] && pass "and no other family is told of a repository (Fedora's packages need none)" ||
    fail "a repository hint on:$quiet"

printf 'TIER commands: what a dry run would install, every library hidden\n'
# pkg-config finds nothing, so every library row is missing; the tools are here
dry() { local d="$1"; shift; PKG_CONFIG_LIBDIR="$work/nopc" PKG_CONFIG_PATH="" GB_OS_RELEASE="$work/$d.os" \
        ./install.sh --dry-run --no-install "$@" < /dev/null 2>&1; }
o=$(dry ubuntu --no-qt)
if has "apt-get update" "$o" && has "apt-get install -y" "$o" && has libpcre2-dev "$o" && has libsqlite3-dev "$o" &&
   has libldap-dev "$o" && ! has qt6-base-dev "$o"; then
    pass "Ubuntu: apt-get update first, then the libraries, Qt left out by --no-qt"
else fail "Ubuntu --no-qt: $o"; fi
o=$(dry ubuntu --qt)
has "qt6-base-dev qt6-base-dev-tools" "$o" && has qt6-svg-dev "$o" && pass "  --qt adds Qt's packages" || fail "Ubuntu --qt: $o"
o=$(dry ubuntu)
if has "No terminal to ask on, so no Qt" "$o" && ! has qt6-base-dev "$o"; then
    pass "  with no terminal to ask on, no Qt, and it says so"
else fail "Ubuntu, no terminal: $o"; fi
o=$(dry ubuntu --minimal)
inst=$(printf '%s\n' "$o" | grep 'apt-get install')
[ "$(echo $inst)" = '$ sudo apt-get install -y libpcre2-dev' ] || [ "$(echo $inst)" = '$ apt-get install -y libpcre2-dev' ] &&
    pass "  --minimal: only PCRE2 (the tools are present)" || fail "Ubuntu --minimal: $inst"
o=$(dry fedora --qt)
if has "dnf install -y" "$o" && has pcre2-devel "$o" && has openldap-devel "$o" && has qt6-qtbase-devel "$o" && ! has apt-get "$o"; then
    pass "Fedora: dnf with its own names, and no apt-get"
else fail "Fedora: $o"; fi
o=$(dry rocky --no-qt)
has "dnf install -y" "$o" && has libcurl-devel "$o" && pass "  Rocky reads as Fedora's family" || fail "Rocky: $o"
o=$(dry arch --qt)
if has "pacman -S --needed --noconfirm" "$o" && has postgresql-libs "$o" && has qt6-base "$o" && ! has apt-get "$o"; then
    pass "Arch: pacman with its own names"
else fail "Arch: $o"; fi
o=$(dry manjaro --no-qt)
has "pacman -S" "$o" && pass "  Manjaro reads as Arch's family" || fail "Manjaro: $o"

printf 'TIER unknown: a distribution with no names\n'
o=$(dry suse --no-qt); rc=$?
if [ "$rc" != 0 ] && has "tools/deps.list names every dependency" "$o" && ! has "Building:" "$o"; then
    pass "with PCRE2 missing it stops before building and says where the names are"
else fail "unknown, rc $rc: $o"; fi
o=$(GB_OS_RELEASE="$work/suse.os" ./install.sh --dry-run --no-install --no-qt < /dev/null 2>&1); rc=$?
if [ "$rc" = 0 ] && has "Building:" "$o"; then
    pass "and with everything required present it builds"
else fail "unknown with the libraries present, rc $rc: $o"; fi

printf 'TIER install: a real run here, into a scratch prefix\n'
o=$(./install.sh --no-qt --prefix "$work/p/q" < /dev/null 2>&1); rc=$?
if [ "$rc" = 0 ] && [ -x "$work/p/q/bin/gbasic" ] && [ -f "$work/p/q/share/gbasic/stdlib/notation.bas" ]; then
    pass "gbasic and its stdlib installed under a prefix that did not exist"
else fail "install rc $rc: $(printf '%s' "$o" | tail -5)"; fi
has "is installed at $work/p/q/bin/gbasic" "$o" && ! has "did not run" "$o" &&
    pass "  and a program loading a standard library ran from outside the tree" || fail "the probe: $(printf '%s' "$o" | tail -4)"
# ITS OWN WORD IS NOT THE CHECK: run the installed binary from elsewhere
mkdir -p "$work/elsewhere"
printf 'load notation\nprint notation.from_text(notation.to_text({ v: 41 + 1 })).v\n' > "$work/elsewhere/t.bas"
got=$(cd "$work/elsewhere" && env -u GBASIC_PATH "$work/p/q/bin/gbasic" t.bas 2>&1)
[ "$got" = 42 ] && pass "  checked from here: the installed binary finds its stdlib" || fail "the installed binary said: $got"
# THE REPORT IS THE INSTALLED BINARY'S --modules, which tests/modules_check.sh
# holds to what the binary does. This check compared it with --capabilities
# until 2026-11-18, and so held the report to the very list that lists every
# module whatever the build has: it passed while install.sh told a RHEL
# machine with no unixODBC that ODBC was built (gbasic books).
mods=$(printf '%s\n' "$o" | sed -n 's/^Optional modules built: //p')
wantm=$("$work/p/q/bin/gbasic" --modules | awk -F'\t' '$2 == "built" { print $1 }' | tr '\n' ' ')
[ "$(printf '%s\n' $mods | sort | tr '\n' ' ')" = "$(printf '%s\n' $wantm | sort | tr '\n' ' ')" ] && pass "  the modules it names built are the installed binary's --modules ($(echo $mods | wc -w))" ||
    fail "modules named [$mods], --modules [$wantm]"
# and what it lists left out is exactly the rest, each with what it needs
# (none, on a machine with every library: then there is no such line at all)
gotl=$(printf '%s\n' "$o" | sed -n 's/^    \([a-z_]*\) (needs .*)$/\1/p' | sort | tr '\n' ' ')
wantl=$("$work/p/q/bin/gbasic" --modules | awk -F'\t' '$2 == "left out" { print $1 }' | sort | tr '\n' ' ')
if [ "$gotl" = "$wantl" ] && { [ -n "$wantl" ] || ! has "Left out" "$o"; }; then
    pass "  and the ones it lists left out are --modules' left-out rows [${wantl:-none}]"
else
    fail "left out listed [$gotl], --modules [$wantl]"
fi
o=$(./install.sh --no-install --prefix "$work/never" < /dev/null 2>&1); rc=$?
[ "$rc" = 0 ] && [ ! -e "$work/never" ] && has "Not installed (--no-install)" "$o" &&
    pass "--no-install builds and installs nothing" || fail "--no-install rc $rc: $(printf '%s' "$o" | tail -3)"
o=$(./install.sh --frobnicate 2>&1); rc=$?
[ "$rc" = 2 ] && has "unknown option --frobnicate" "$o" && pass "an unknown option is refused by name" || fail "unknown option: rc $rc $o"

printf 'TIER containers: a fresh machine of each distribution\n'
if [ "${INSTALL_CONTAINER_TEST:-}" != 1 ]; then
    printf '  SKIP (INSTALL_CONTAINER_TEST=1 runs it; it downloads images and packages) -- the package NAMES were NOT installed here\n'
else
    engine=$(command -v podman || command -v docker || true)
    if [ -z "$engine" ]; then
        fail "INSTALL_CONTAINER_TEST=1 but no podman or docker"
    else
        # THIS TREE AS IT STANDS, committed or not, without build products
        git ls-files -co --exclude-standard | tar -cf "$work/tree.tar" -T - 2>/dev/null
        # EACH IMAGE, what a real machine of that kind would have run first, and
        # the packages it is EXPECTED not to carry: only KF6, which arrived
        # after these releases. Anything else unpackaged is a wrong name in
        # deps.list (Fedora's zlib-devel and 22.04's qt6-svg-dev were, and the
        # first version of this tier passed them).
        for img in ubuntu:22.04 ubuntu:24.04 debian:12 fedora:41 archlinux:latest; do
            case "$img" in
                archlinux:*) pre="pacman -Syu --noconfirm" ;;
                *)           pre="true" ;;
            esac
            case "$img" in
                ubuntu:22.04|ubuntu:24.04|debian:12) expect_unpk="libkf6syntaxhighlighting-dev" ;;
                *) expect_unpk="" ;;
            esac
            log="$work/$(printf '%s' "$img" | tr ':/' '__').log"
            timeout 3600 "$engine" run --rm -i -v "$work/tree.tar:/tree.tar:ro" "$img" bash -c "
                set -e; $pre >/dev/null
                mkdir /src && cd /src && tar -xf /tree.tar
                ./install.sh --qt --yes </dev/null
                PATH=\$HOME/.local/bin:\$PATH gbasic --capabilities | cut -f1 | tr '\n' ' ' > /tmp/caps
                echo; echo \"CAPS \$(cat /tmp/caps)\"" > "$log" 2>&1
            rc=$?
            caps=$(sed -n 's/^CAPS //p' "$log")
            built=$(sed -n 's/^Optional modules built: //p' "$log")
            left=$(sed -n 's/^    \([a-z_]*\) (needs .*/\1/p' "$log" | tr '\n' ' ')
            unpk=$(sed -n 's/^Not packaged on this release, so left out://p' "$log" | grep -oE '[a-z0-9.+-]+ \(' | tr -d ' (' | sort | tr '\n' ' ')
            if [ "$rc" = 0 ] && [ -n "$caps" ]; then
                pass "$img: installed; built:$built${left:+; left out:$left}"
                [ -z "$left" ] && pass "  $img: every optional module built, qt included" ||
                    fail "  $img left out:$left"
                [ "$(echo $unpk)" = "$(echo $expect_unpk)" ] &&
                    pass "  $img: unpackaged here only what was expected${expect_unpk:+ ($expect_unpk)}" ||
                    fail "  $img: unpackaged [$(echo $unpk)], expected [$expect_unpk] -- a wrong name in tools/deps.list"
            else
                fail "$img: rc $rc"
                tail -25 "$log" | sed 's/^/       /'
            fi
        done
    fi
fi

[ "$status" = 0 ] && printf 'run_install_script: all checks passed\n' || printf 'run_install_script: FAILED\n'
exit $status

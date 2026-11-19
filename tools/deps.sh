# Sourced by tools/check-deps.sh and ./install.sh: reads tools/deps.list, says
# which distribution this is, and which of the list is present.
#
#   deps_distro               debian | fedora | arch | unknown (GB_OS_RELEASE
#                             names another os-release file, for the tests)
#   deps_present KIND PROBE   0 when that dependency is on this machine
#   deps_rows GROUP...        the list's rows in those groups, comments dropped
#   deps_packages DISTRO ROW  the row's package names for DISTRO, alternatives
#                             resolved against the package manager when it is
#                             this machine's (else the first is taken)
#   deps_install_cmd DISTRO   the command that installs packages, without them

GB_DEPS_LIST="${GB_DEPS_LIST:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/deps.list}"

deps_distro() {
    local f="${GB_OS_RELEASE:-/etc/os-release}" id="" like=""
    [ -r "$f" ] || { echo unknown; return; }
    id=$(. "$f" 2>/dev/null; printf '%s' "${ID:-}")
    like=$(. "$f" 2>/dev/null; printf '%s' "${ID_LIKE:-}")
    for w in $id $like; do
        case "$w" in
            debian|ubuntu) echo debian; return ;;
            fedora|rhel|centos) echo fedora; return ;;
            arch|archlinux) echo arch; return ;;
        esac
    done
    echo unknown
}

# The command that enables the repository RHEL and its rebuilds keep some
# -devel packages in (CodeReady Builder; "crb" on Alma, Rocky and CentOS
# Stream), or nothing and status 1 elsewhere. Fedora itself needs none.
deps_extra_repo_hint() {
    local f="${GB_OS_RELEASE:-/etc/os-release}" id="" ver=""
    [ -r "$f" ] || return 1
    id=$(. "$f" 2>/dev/null; printf '%s' "${ID:-}")
    ver=$(. "$f" 2>/dev/null; printf '%s' "${VERSION_ID:-}")
    case "$id" in
        rhel) echo "sudo subscription-manager repos --enable codeready-builder-for-rhel-${ver%%.*}-$(uname -m)-rpms" ;;
        almalinux|rocky|centos) echo "sudo dnf config-manager --set-enabled crb" ;;
        *) return 1 ;;
    esac
}

deps_present() {
    case "$1" in
        tool) command -v "$2" >/dev/null 2>&1 ;;
        pc)   command -v pkg-config >/dev/null 2>&1 && pkg-config --exists "$2" 2>/dev/null ;;
        dir)  [ -d "/usr/include/$2" ] || [ -d "/usr/local/include/$2" ] ;;
        *)    return 1 ;;
    esac
}

deps_rows() {
    local g pat=""
    for g in "$@"; do pat="$pat|$g"; done
    grep -v '^[[:space:]]*#' "$GB_DEPS_LIST" | grep -v '^[[:space:]]*$' |
        awk -F'|' -v want="${pat#|}" 'BEGIN { n = split(want, w, "|"); for (i = 1; i <= n; i++) ok[w[i]] = 1 } ok[$6]'
}

# Whether this machine's package manager has a candidate for one package.
_deps_has_candidate() {
    case "$1" in
        debian) apt-cache show --no-all-versions "$2" >/dev/null 2>&1 ;;
        fedora) dnf -q info "$2" >/dev/null 2>&1 || dnf -q info --available "$2" >/dev/null 2>&1 ;;
        arch)   pacman -Si "$2" >/dev/null 2>&1 || pacman -Qi "$2" >/dev/null 2>&1 ;;
        *)      return 1 ;;
    esac
}

deps_packages() {
    local distro="$1" row="$2" col field p alt out=""
    case "$distro" in debian) col=3 ;; fedora) col=4 ;; arch) col=5 ;; *) return 1 ;; esac
    field=$(printf '%s\n' "$row" | cut -d'|' -f"$col")
    for p in $field; do
        if [ "${p#*/}" != "$p" ]; then
            # a/b: the first with a candidate here, or the first when this is
            # not that distribution's machine (a dry run of another's command)
            local chosen=""
            if [ "$(deps_distro)" = "$distro" ]; then
                for alt in $(printf '%s' "$p" | tr '/' ' '); do
                    _deps_has_candidate "$distro" "$alt" && { chosen="$alt"; break; }
                done
            fi
            p="${chosen:-${p%%/*}}"
        fi
        out="$out $p"
    done
    printf '%s\n' "${out# }"
}

deps_install_cmd() {
    case "$1" in
        debian) echo "apt-get install -y" ;;
        fedora) echo "dnf install -y" ;;
        arch)   echo "pacman -S --needed --noconfirm" ;;
        *)      return 1 ;;
    esac
}

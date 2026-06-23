#!/usr/bin/env bash
set -euo pipefail

LIBVIRT_URI="${LIBVIRT_URI:-qemu:///system}"

have() {
    command -v "$1" >/dev/null 2>&1
}

check_tool() {
    local tool="$1"
    if have "$tool"; then
        printf 'OK   %s: %s\n' "$tool" "$(command -v "$tool")"
    else
        printf 'MISS %s\n' "$tool"
        missing=1
    fi
}

missing=0

printf 'Checking s390x VM prerequisites\n'
printf 'Libvirt URI: %s\n\n' "$LIBVIRT_URI"

check_tool qemu-system-s390x
check_tool qemu-img
check_tool virt-install
check_tool virsh
check_tool curl

printf '\nChecking libvirt access\n'
if virsh -c "$LIBVIRT_URI" list --all >/dev/null 2>&1; then
    printf 'OK   virsh can access %s without sudo\n' "$LIBVIRT_URI"
elif have sudo && sudo -n virsh -c "$LIBVIRT_URI" list --all >/dev/null 2>&1; then
    printf 'OK   virsh can access %s with sudo\n' "$LIBVIRT_URI"
else
    printf 'WARN virsh could not access %s without an interactive sudo prompt\n' "$LIBVIRT_URI"
fi

printf '\nChecking default network\n'
if virsh -c "$LIBVIRT_URI" net-info default >/dev/null 2>&1; then
    printf 'OK   libvirt network "default" exists\n'
elif have sudo && sudo -n virsh -c "$LIBVIRT_URI" net-info default >/dev/null 2>&1; then
    printf 'OK   libvirt network "default" exists with sudo\n'
else
    printf 'WARN libvirt network "default" was not visible. Create/start it or set VM_NETWORK.\n'
fi

printf '\nChecking useful groups\n'
groups_text="$(id -nG)"
for group in libvirt kvm; do
    case " $groups_text " in
        *" $group "*) printf 'OK   current user is in group %s\n' "$group" ;;
        *) printf 'INFO current user is not in group %s\n' "$group" ;;
    esac
done

printf '\nSuggested packages\n'
printf 'Debian/Ubuntu: sudo apt install qemu-system-s390x virtinst libvirt-daemon-system libvirt-clients virt-manager qemu-utils curl\n'
printf 'Fedora/RHEL:   sudo dnf install qemu-system-s390x virt-install libvirt virt-manager qemu-img curl\n'
printf 'SLES/openSUSE: sudo zypper install qemu-s390x virt-install libvirt virt-manager qemu-tools curl\n'

if [[ "$missing" -ne 0 ]]; then
    exit 1
fi

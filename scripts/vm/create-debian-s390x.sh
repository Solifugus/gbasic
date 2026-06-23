#!/usr/bin/env bash
set -euo pipefail

VM_NAME="${VM_NAME:-gbasic-debian-s390x}"
VM_RAM="${VM_RAM:-2048}"
VM_VCPUS="${VM_VCPUS:-2}"
VM_DISK_SIZE="${VM_DISK_SIZE:-20G}"
VM_STORAGE_DIR="${VM_STORAGE_DIR:-/var/lib/libvirt/images}"
VM_ASSET_DIR="${VM_ASSET_DIR:-$HOME/.cache/gbasic-vm/s390x/debian}"
VM_NETWORK="${VM_NETWORK:-network=default}"
LIBVIRT_URI="${LIBVIRT_URI:-qemu:///system}"
DEBIAN_SUITE="${DEBIAN_SUITE:-trixie}"
DEBIAN_OSINFO="${DEBIAN_OSINFO:-debian13}"
DEBIAN_MIRROR="${DEBIAN_MIRROR:-https://deb.debian.org/debian}"
DEBIAN_INSTALLER_BASE="${DEBIAN_INSTALLER_BASE:-$DEBIAN_MIRROR/dists/$DEBIAN_SUITE/main/installer-s390x/current/images/generic}"
EXTRA_KERNEL_ARGS="${EXTRA_KERNEL_ARGS:-}"
VM_DRY_RUN="${VM_DRY_RUN:-0}"

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Missing required command: %s\n' "$1" >&2
        exit 1
    fi
}

storage_needs_sudo() {
    if [[ -d "$VM_STORAGE_DIR" ]]; then
        [[ ! -w "$VM_STORAGE_DIR" ]]
        return
    fi
    local parent
    parent="$(dirname "$VM_STORAGE_DIR")"
    [[ ! -w "$parent" ]]
}

run_storage() {
    if ! storage_needs_sudo; then
        "$@"
    else
        if ! command -v sudo >/dev/null 2>&1; then
            printf 'Need sudo to write %s, but sudo is not installed.\n' "$VM_STORAGE_DIR" >&2
            exit 1
        fi
        sudo "$@"
    fi
}

virsh_cmd() {
    if virsh -c "$LIBVIRT_URI" list --all >/dev/null 2>&1; then
        virsh -c "$LIBVIRT_URI" "$@"
    else
        if ! command -v sudo >/dev/null 2>&1; then
            printf 'Need sudo to access %s, but sudo is not installed.\n' "$LIBVIRT_URI" >&2
            exit 1
        fi
        sudo virsh -c "$LIBVIRT_URI" "$@"
    fi
}

virt_install_cmd() {
    if virsh -c "$LIBVIRT_URI" list --all >/dev/null 2>&1; then
        virt-install --connect "$LIBVIRT_URI" "$@"
    else
        if ! command -v sudo >/dev/null 2>&1; then
            printf 'Need sudo to access %s, but sudo is not installed.\n' "$LIBVIRT_URI" >&2
            exit 1
        fi
        sudo virt-install --connect "$LIBVIRT_URI" "$@"
    fi
}

download() {
    local url="$1"
    local dest="$2"
    if [[ -s "$dest" ]]; then
        printf 'Using cached %s\n' "$dest"
        return
    fi
    if [[ "$VM_DRY_RUN" == "1" ]]; then
        printf 'Would download %s -> %s\n' "$url" "$dest"
        return
    fi
    printf 'Downloading %s\n' "$url"
    curl -fL --retry 3 --output "$dest.tmp" "$url"
    mv "$dest.tmp" "$dest"
}

need curl
need qemu-img
need virt-install
need virsh

mkdir -p "$VM_ASSET_DIR"
if [[ "$VM_DRY_RUN" == "1" ]]; then
    printf 'Would ensure storage directory %s\n' "$VM_STORAGE_DIR"
else
    run_storage install -d -m 0755 "$VM_STORAGE_DIR"
fi

kernel="$VM_ASSET_DIR/kernel.debian"
initrd="$VM_ASSET_DIR/initrd.debian"
parmfile="$VM_ASSET_DIR/parmfile.debian"
disk="$VM_STORAGE_DIR/$VM_NAME.qcow2"

download "$DEBIAN_INSTALLER_BASE/kernel.debian" "$kernel"
download "$DEBIAN_INSTALLER_BASE/initrd.debian" "$initrd"
download "$DEBIAN_INSTALLER_BASE/parmfile.debian" "$parmfile"

if [[ "$VM_DRY_RUN" != "1" ]] && virsh_cmd dominfo "$VM_NAME" >/dev/null 2>&1; then
    printf 'VM already exists: %s\n' "$VM_NAME" >&2
    printf 'Use virt-manager/virsh to remove it first, or set VM_NAME to a new name.\n' >&2
    exit 1
fi

if [[ "$VM_DRY_RUN" == "1" ]]; then
    printf 'Would create qcow2 disk %s size %s\n' "$disk" "$VM_DISK_SIZE"
elif [[ -e "$disk" ]]; then
    if qemu-img info "$disk" >/dev/null 2>&1; then
        printf 'Using existing disk %s\n' "$disk"
    else
        printf 'Disk already exists and is not a readable qemu image: %s\n' "$disk" >&2
        printf 'Move it aside or set VM_NAME/VM_STORAGE_DIR to a new path.\n' >&2
        exit 1
    fi
else
    run_storage qemu-img create -f qcow2 "$disk" "$VM_DISK_SIZE"
fi

if [[ -s "$parmfile" ]]; then
    parm_args="$(tr '\n' ' ' < "$parmfile" | tr -s '[:space:]' ' ')"
else
    parm_args="ro locale=C"
fi
kernel_args="$parm_args console=ttysclp0 netcfg/choose_interface=auto mirror/country=manual mirror/http/hostname=deb.debian.org mirror/http/directory=/debian $EXTRA_KERNEL_ARGS"

printf '\nCreating VM %s\n' "$VM_NAME"
printf 'Disk: %s\n' "$disk"
printf 'Installer: %s\n\n' "$DEBIAN_INSTALLER_BASE"

virt_args=(
    --name "$VM_NAME"
    --arch s390x
    --machine s390-ccw-virtio
    --virt-type qemu
    --memory "$VM_RAM"
    --vcpus "$VM_VCPUS"
    --osinfo "$DEBIAN_OSINFO"
    --disk "path=$disk,format=qcow2,bus=virtio"
    --network "$VM_NETWORK,model=virtio"
    --graphics none
    --console pty,target_type=sclp
    --boot "kernel=$kernel,initrd=$initrd,kernel_args=$kernel_args"
    --noautoconsole
)

if [[ "$VM_DRY_RUN" == "1" ]]; then
    printf 'Would run virt-install with:\n'
    printf '  virt-install --connect %q' "$LIBVIRT_URI"
    printf ' %q' "${virt_args[@]}"
    printf '\n'
    exit 0
fi

virt_install_cmd "${virt_args[@]}"

printf '\nVM defined and started.\n'
printf 'Open virt-manager and connect to %s, or attach with:\n' "$LIBVIRT_URI"
printf '  virsh -c %s console %s\n' "$LIBVIRT_URI" "$VM_NAME"

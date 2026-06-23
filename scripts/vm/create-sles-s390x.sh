#!/usr/bin/env bash
set -euo pipefail

VM_NAME="${VM_NAME:-gbasic-sles-s390x}"
VM_RAM="${VM_RAM:-2048}"
VM_VCPUS="${VM_VCPUS:-2}"
VM_DISK_SIZE="${VM_DISK_SIZE:-20G}"
VM_STORAGE_DIR="${VM_STORAGE_DIR:-/var/lib/libvirt/images}"
VM_NETWORK="${VM_NETWORK:-network=default}"
LIBVIRT_URI="${LIBVIRT_URI:-qemu:///system}"
SLES_OSINFO="${SLES_OSINFO:-sles12}"
SLES_ISO="${SLES_ISO:-}"
VM_DRY_RUN="${VM_DRY_RUN:-0}"

if [[ -z "$SLES_ISO" ]]; then
    printf 'Set SLES_ISO to a local SLES s390x install ISO path.\n' >&2
    printf 'Example: SLES_ISO=$HOME/iso/sles-s390x-dvd.iso %s\n' "$0" >&2
    exit 2
fi

if [[ "$VM_DRY_RUN" != "1" && ! -f "$SLES_ISO" ]]; then
    printf 'SLES_ISO does not exist: %s\n' "$SLES_ISO" >&2
    exit 2
fi

if [[ -d "$VM_STORAGE_DIR" && -w "$VM_STORAGE_DIR" ]] || [[ ! -d "$VM_STORAGE_DIR" && -w "$(dirname "$VM_STORAGE_DIR")" ]]; then
    SUDO=()
else
    if ! command -v sudo >/dev/null 2>&1; then
        printf 'Need sudo to write %s, but sudo is not installed.\n' "$VM_STORAGE_DIR" >&2
        exit 1
    fi
    SUDO=(sudo)
fi

if [[ "$VM_DRY_RUN" == "1" ]]; then
    printf 'Would ensure storage directory %s\n' "$VM_STORAGE_DIR"
else
    "${SUDO[@]}" install -d -m 0755 "$VM_STORAGE_DIR"
fi
disk="$VM_STORAGE_DIR/$VM_NAME.qcow2"

if [[ "$VM_DRY_RUN" != "1" && -e "$disk" ]]; then
    printf 'Disk already exists: %s\n' "$disk" >&2
    exit 1
fi

if [[ "$VM_DRY_RUN" == "1" ]]; then
    printf 'Would create qcow2 disk %s size %s\n' "$disk" "$VM_DISK_SIZE"
else
    "${SUDO[@]}" qemu-img create -f qcow2 "$disk" "$VM_DISK_SIZE"
fi

if virsh -c "$LIBVIRT_URI" list --all >/dev/null 2>&1; then
    VIRT=(virt-install -c "$LIBVIRT_URI")
else
    if ! command -v sudo >/dev/null 2>&1; then
        printf 'Need sudo to access %s, but sudo is not installed.\n' "$LIBVIRT_URI" >&2
        exit 1
    fi
    VIRT=(sudo virt-install -c "$LIBVIRT_URI")
fi

virt_args=(
    --name "$VM_NAME"
    --arch s390x
    --machine s390-ccw-virtio
    --virt-type qemu
    --memory "$VM_RAM"
    --vcpus "$VM_VCPUS"
    --osinfo "$SLES_OSINFO"
    --disk "path=$disk,format=qcow2,bus=virtio"
    --network "$VM_NETWORK,model=virtio"
    --graphics none
    --console pty,target_type=sclp
    --location "$SLES_ISO"
    --extra-args "console=ttysclp0"
    --noautoconsole
)

if [[ "$VM_DRY_RUN" == "1" ]]; then
    printf 'Would run virt-install with:\n'
    printf '  virt-install -c %q' "$LIBVIRT_URI"
    printf ' %q' "${virt_args[@]}"
    printf '\n'
    exit 0
fi

"${VIRT[@]}" "${virt_args[@]}"

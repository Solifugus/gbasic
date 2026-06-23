#!/usr/bin/env bash
set -euo pipefail

VM_NAME="${VM_NAME:-gbasic-debian-s390x}"
LIBVIRT_URI="${LIBVIRT_URI:-qemu:///system}"
VM_START="${VM_START:-1}"
VM_FORCE_DESTROY="${VM_FORCE_DESTROY:-0}"

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Missing required command: %s\n' "$1" >&2
        exit 1
    fi
}

need virsh
need virt-xml

if ! virsh -c "$LIBVIRT_URI" dominfo "$VM_NAME" >/dev/null 2>&1; then
    printf 'VM not found: %s\n' "$VM_NAME" >&2
    exit 1
fi

state="$(virsh -c "$LIBVIRT_URI" domstate "$VM_NAME")"
if [[ "$state" == "running" ]]; then
    if [[ "$VM_FORCE_DESTROY" == "1" ]]; then
        printf 'Forcing off running VM %s\n' "$VM_NAME"
        virsh -c "$LIBVIRT_URI" destroy "$VM_NAME"
    else
        printf 'Shutting down running VM %s\n' "$VM_NAME"
        virsh -c "$LIBVIRT_URI" shutdown "$VM_NAME" >/dev/null || true
        for _ in {1..30}; do
            sleep 1
            state="$(virsh -c "$LIBVIRT_URI" domstate "$VM_NAME")"
            if [[ "$state" != "running" ]]; then
                break
            fi
        done
        if [[ "$state" == "running" ]]; then
            printf 'VM is still running. Re-run with VM_FORCE_DESTROY=1 after confirming it is safe to power off.\n' >&2
            exit 1
        fi
    fi
fi

virt-xml -c "$LIBVIRT_URI" "$VM_NAME" \
    --edit \
    --boot hd \
    --xml xpath.delete=./os/kernel \
    --xml xpath.delete=./os/initrd \
    --xml xpath.delete=./os/cmdline

printf 'VM %s now boots from disk.\n' "$VM_NAME"

if [[ "$VM_START" == "1" ]]; then
    virsh -c "$LIBVIRT_URI" start "$VM_NAME"
    printf 'Started %s. Attach with:\n' "$VM_NAME"
    printf '  virsh -c %s console %s\n' "$LIBVIRT_URI" "$VM_NAME"
fi

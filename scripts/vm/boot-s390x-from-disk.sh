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
need python3

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

xml_file="$(mktemp)"
trap 'rm -f "$xml_file"' EXIT

virsh -c "$LIBVIRT_URI" dumpxml "$VM_NAME" >"$xml_file"

python3 - "$xml_file" <<'PY'
import sys
import xml.etree.ElementTree as ET

path = sys.argv[1]
tree = ET.parse(path)
root = tree.getroot()
os_node = root.find("os")
if os_node is None:
    raise SystemExit("domain XML has no <os> node")

for tag in ("kernel", "initrd", "cmdline"):
    child = os_node.find(tag)
    if child is not None:
        os_node.remove(child)

for child in list(os_node.findall("boot")):
    os_node.remove(child)

boot = ET.Element("boot")
boot.set("dev", "hd")
os_node.append(boot)

ET.indent(tree, space="  ")
tree.write(path, encoding="unicode")
PY

virsh -c "$LIBVIRT_URI" define "$xml_file" >/dev/null

printf 'VM %s now boots from disk.\n' "$VM_NAME"

if [[ "$VM_START" == "1" ]]; then
    virsh -c "$LIBVIRT_URI" start "$VM_NAME"
    printf 'Started %s. Attach with:\n' "$VM_NAME"
    printf '  virsh -c %s console %s\n' "$LIBVIRT_URI" "$VM_NAME"
fi

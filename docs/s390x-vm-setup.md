# s390x VM Setup

These helpers create libvirt/QEMU s390x guests that are visible in
virt-manager. They are intended for testing gBASIC on IBM Z architecture
without committing host-specific VM state to the repository.

Debian is the easiest target because s390x installer files are public. RHEL and
SLES scripts are included as wrappers, but they require local s390x install
media from Red Hat or SUSE.

## Prerequisites

Run:

```bash
scripts/vm/check-s390x-vm-prereqs.sh
```

Typical packages:

```bash
sudo apt install qemu-system-s390x virtinst libvirt-daemon-system libvirt-clients virt-manager qemu-utils curl
```

The scripts default to `qemu:///system`, `/var/lib/libvirt/images`, and the
libvirt `default` NAT network. That usually requires either membership in the
`libvirt` group or `sudo`.

## Debian

Debian stable is currently Debian 13 "trixie". The Debian script downloads the
current s390x installer kernel, initrd, and parmfile from the Debian mirror and
boots the text installer.

```bash
scripts/vm/create-debian-s390x.sh
```

Useful overrides:

```bash
VM_NAME=gbasic-debian13-s390x \
VM_RAM=4096 \
VM_VCPUS=4 \
VM_DISK_SIZE=40G \
scripts/vm/create-debian-s390x.sh
```

Preview the generated commands without creating a disk or VM:

```bash
VM_DRY_RUN=1 scripts/vm/create-debian-s390x.sh
```

Use virt-manager to open the VM console, or attach from a terminal:

```bash
virsh -c qemu:///system console gbasic-debian-s390x
```

The default installer assets are:

```text
https://deb.debian.org/debian/dists/trixie/main/installer-s390x/current/images/generic/kernel.debian
https://deb.debian.org/debian/dists/trixie/main/installer-s390x/current/images/generic/initrd.debian
https://deb.debian.org/debian/dists/trixie/main/installer-s390x/current/images/generic/parmfile.debian
```

Set `DEBIAN_SUITE`, `DEBIAN_OSINFO`, or `DEBIAN_INSTALLER_BASE` if Debian moves
to a newer stable release.

After the installer finishes, switch the VM from installer kernel boot to disk
boot:

```bash
scripts/vm/boot-s390x-from-disk.sh
```

If the VM is still running in the installer and does not shut down cleanly:

```bash
VM_FORCE_DESTROY=1 scripts/vm/boot-s390x-from-disk.sh
```

### Debian Guest Bootstrap

Inside the installed Debian s390x guest, install the build dependencies. If you
are logged in as root, omit `sudo`. The first line is what the build requires
(PCRE2 is the regex engine and has been required since 2026-11-03); the second
is the optional modules, each detected by `pkg-config` and turned into a clean
runtime error when absent.

```bash
sudo apt update
sudo apt install git build-essential bison pkg-config libpcre2-dev
sudo apt install libsqlite3-dev libpq-dev libcurl4-openssl-dev libcrypt-dev \
    libssl-dev libxml2-dev zlib1g-dev unixodbc-dev libldap-dev
```

`libcrypt-dev` provides `crypt.h` and `-lcrypt` on Debian. The package may be
named differently on other distributions.

The GUI is Qt 6 and is optional (GTK was removed on 2026-10-23, so `HAVE_GTK`
no longer exists). A headless guest builds without it automatically; install
`qt6-base-dev` only to test the `qt` module, which needs a display.

Clone and test:

```bash
mkdir -p ~/development
cd ~/development
git clone https://github.com/Solifugus/gbasic.git
cd gbasic
make
./tests/run_all.sh
```

When this page was written (43304f6, 2026-06-22) it recorded the workflow as
run on a Debian 13 s390x QEMU guest, with the GTK-era build, a since-merged
branch, and only the `run_examples.sh` and `run_negative.sh` suites. The current tree, the dependency list above and the
full gate have NOT been run on s390x since; that is the next thing to do on the
guest. s390x is big-endian, which is what makes it worth running: serialize v3
and the G-SQL key encoding are pinned little-endian and checked by
`run_serialize.sh` and `run_gsql_key.sh`.

Codex is not expected to run inside the s390x guest as a normal supported
setup. Use Codex on the host, push fixes to GitHub, pull them in the guest, and
run the build/test commands there.

## RHEL

The RHEL script requires a local RHEL s390x installer ISO:

```bash
RHEL_ISO=$HOME/iso/rhel-s390x-dvd.iso scripts/vm/create-rhel-s390x.sh
```

Set `RHEL_OSINFO` to a more specific value, such as `rhel9-unknown`, if your
local osinfo database provides it.

## SLES

The SLES script requires a local SLES s390x installer ISO:

```bash
SLES_ISO=$HOME/iso/sles-s390x-dvd.iso scripts/vm/create-sles-s390x.sh
```

Set `SLES_OSINFO` to the matching local osinfo value for the media you have.

## Notes

- These guests emulate s390x with QEMU. On a non-s390x host, this is useful for
  compatibility testing but much slower than native virtualization.
- The scripts refuse to overwrite an existing VM disk.
- The scripts do not remove existing libvirt domains. Use virt-manager or
  `virsh undefine` intentionally if you want to recreate a VM.
- For repeatable CI-style testing later, the next step would be unattended
  install files: Debian preseed, RHEL kickstart, and SLES AutoYaST.

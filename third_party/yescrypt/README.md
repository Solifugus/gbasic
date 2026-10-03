# Vendored: yescrypt

- **Upstream:** https://www.openwall.com/yescrypt/ (Alexander Peslyak; scrypt
  core by Colin Percival)
- **Version:** 1.1.0. `yescrypt-1.1.0.tar.gz`, SHA-256
  `85d0cd7d387a43bed55eb896c84cde0fa35ae5c169706082c94ae4717f66c70c`. Every
  file vendored here is byte-identical to the `YESCRYPT_1_1_0` tag of
  https://github.com/openwall/yescrypt (compared 2026-10-03). The two archives
  differ in one line of `CHANGES`, which is not vendored.
- **License:** BSD-2-Clause, or a shorter "redistribution and use are
  permitted" grant on some files. Each file carries its own notice, retained
  verbatim. The binary-distribution condition is met by the third-party notices
  shipped with gBASIC.
- **Files:** `yescrypt.h`, `yescrypt-opt.c` (which `#include`s
  `yescrypt-platform.c`), `yescrypt-common.c`, `sha256.[ch]`,
  `insecure_memzero.[ch]` and `sysendian.h`. All are unmodified.

## Why it's here

`password_hash` and `password_verify` are `crypt(3)` on Linux, through
libxcrypt. libxcrypt is LGPL, and the Windows `gbasic.exe` links no LGPL code
(docs/windows_port_status.md §19). This is the same algorithm from its
authors, under a permissive licence.

It has to be the same algorithm, because a password hash is DATA. It is written
by one machine and checked by another, so a hash made on Linux must verify on
Windows and the reverse. `src/eval.c` therefore asks this code for exactly what
libxcrypt's default produces: `$y$j9T$` (`YESCRYPT_DEFAULTS`, N=4096, r=32,
p=1) and 16 bytes of salt. `tests/windows/password_interop.bas` holds the
evidence. It verifies a hash that libxcrypt wrote, so that test runs on both
backends.

The build uses this code only where libxcrypt is absent (`HAVE_YESCRYPT`, set
in the Makefile's Windows block, or `make YESCRYPT_VENDORED=1` elsewhere).
Linux builds are unchanged.

`sha256.h` renames its functions to `libcperciva_*`, so they cannot collide
with OpenSSL's `SHA256_Init` and friends in the same static link.

## Updating

Fetch the release archive from openwall.com and check its SHA-256 against a
second copy, the GitHub tag. Replace the files listed above, then rerun
`tests/run_windows_suite.sh` and the `password_*` cases.

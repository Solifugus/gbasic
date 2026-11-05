#!/usr/bin/env python3
"""Assemble src/vendor/bcrypt_pbkdf.c from openssh-portable's
openbsd-compat/bcrypt_pbkdf.c and blowfish.c, with the documented edits.
Reads the two files as text; runs nothing from them."""
import re
import sys

src_dir, out_path = sys.argv[1], sys.argv[2]
bf = open(f"{src_dir}/blowfish.c").read()
pb = open(f"{src_dir}/bcrypt_pbkdf.c").read()


def func(name):
    i = bf.index("\n" + name + "(")
    start = bf.rindex("\n", 0, i - 1) + 1          # the return-type line
    j = bf.index("\n}\n", i) + 3
    return bf[start:j]


bf_license = bf[bf.index("/*\n * Blowfish block cipher"):bf.index("*/", bf.index("/*\n * Blowfish block cipher")) + 2]
bf_note = bf[bf.index("/*\n * This code is derived from section 14.3"):]
bf_note = bf_note[:bf_note.index("*/") + 2]
pb_license = pb[pb.index("/*\n * Copyright (c) 2013 Ted"):]
pb_license = pb_license[:pb_license.index("*/") + 2]
pb_doc = pb[pb.index("/*\n * pkcs #5 pbkdf2"):]
pb_doc = pb_doc[:pb_doc.index("*/") + 2]
body = pb[pb.index("#define BCRYPT_WORDS 8"):pb.rindex("#endif /* HAVE_BCRYPT_PBKDF */")]

body = body.replace("int\nbcrypt_pbkdf(", "static int\ngb_bcrypt_pbkdf(")
body = re.sub(r"crypto_hash_sha512\(([^,]+), ([^,]+), (.+?)\);$",
              r"SHA512((const unsigned char *)(\2), \3, \1);", body, flags=re.M)
body = re.sub(r"explicit_bzero\(([^;]*)\);", r"OPENSSL_cleanse(\1);", body)
body = body.replace("freezero(countsalt, saltlen + 4);",
                    "OPENSSL_cleanse(countsalt, saltlen + 4);\n\tfree(countsalt);")
body = body.replace(
    "\t/* overwrite with random in case caller doesn't check return code */\n\tarc4random_buf(key, keylen);",
    "\t/* zeroed: the caller here always checks the return code */\n\tmemset(key, 0, keylen);")
for bad in ("arc4random", "explicit_bzero", "freezero", "crypto_hash_sha512"):
    assert bad not in body, bad

parts = []
for name in ("Blowfish_encipher", "Blowfish_initstate", "Blowfish_stream2word",
             "Blowfish_expand0state", "Blowfish_expandstate", "blf_enc"):
    p = func(name)
    p = p.replace("u_int32_t", "uint32_t").replace("u_int16_t", "uint16_t").replace("u_int8_t", "uint8_t")
    parts.append("static " + p)

out = f"""/* SPDX-License-Identifier: ISC AND BSD-3-Clause
 *
 * bcrypt_pbkdf, the key derivation of passphrase-protected `openssh-key-v1`
 * files, and the Blowfish it is built on. VENDORED, NOT WRITTEN HERE: OpenSSL
 * has no bcrypt_pbkdf, and its Blowfish has no expensive key schedule, so this
 * is the code OpenSSH itself ships, from openssh-portable V_10_2_P1
 * (openbsd-compat/bcrypt_pbkdf.c rev 1.16, sha256 d4985712...; blowfish.c
 * rev 1.20, sha256 c1ce17be...). Both licences follow unchanged, and
 * LICENSING.md and NOTICE record them. Assembled by
 * tools/vendor_bcrypt_pbkdf.py, which makes only these changes:
 *
 * - the six Blowfish functions bcrypt_pbkdf uses are kept and made `static`
 *   (decipher, the ECB/CBC modes and the self-test `main` are dropped);
 * - `u_int8_t`/`u_int16_t`/`u_int32_t` are <stdint.h>'s types;
 * - SHA-512 is OpenSSL's `SHA512()` in place of SUPERCOP's, so this file is
 *   compiled only where libcrypto is (HAVE_LIBCRYPTO);
 * - `explicit_bzero`/`freezero` are `OPENSSL_cleanse` (+ `free`);
 * - on a refused argument the key is ZEROED rather than filled from
 *   `arc4random_buf`: the one caller always checks the return value;
 * - the entry point is `static` and named `gb_bcrypt_pbkdf`.
 *
 * #include'd by src/modules/crypto_ssh.c.
 */

/* ---- blowfish.c ---- */
{bf_license}

{bf_note}

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/crypto.h>
#include <openssl/sha.h>

#define BLF_N	16			/* Number of Subkeys */

/* Blowfish context */
typedef struct BlowfishContext {{
	uint32_t S[4][256];	/* S-Boxes */
	uint32_t P[BLF_N + 2];	/* Subkeys */
}} blf_ctx;

/* Function for Feistel Networks */

#define F(s, x) ((((s)[        (((x)>>24)&0xFF)]  \\
		 + (s)[0x100 + (((x)>>16)&0xFF)]) \\
		 ^ (s)[0x200 + (((x)>> 8)&0xFF)]) \\
		 + (s)[0x300 + ( (x)     &0xFF)])

#define BLFRND(s,p,i,j,n) (i ^= F(s,j) ^ (p)[n])

{chr(10).join(parts)}
#undef F
#undef BLFRND

/* ---- bcrypt_pbkdf.c ---- */
{pb_license}

#define	MINIMUM(a,b) (((a) < (b)) ? (a) : (b))

{pb_doc}

{body}"""
open(out_path, "w").write(out)

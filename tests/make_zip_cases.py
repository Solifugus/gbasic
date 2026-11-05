#!/usr/bin/env python3
"""Archives for tests/run_zip.sh, made by Python's zipfile (the oracle), and
the answers zipfile gives for them. Usage: make_zip_cases.py OUTDIR SEED.

Writes, into OUTDIR:
  plain.zip     stored and deflated members, a folder, a UTF-8 name, a
                cp437 name, a member written through a data descriptor
                (sizes zeroed in its local header), an archive comment
  random_N.zip  random archives, for the agreement tier
  expect.txt    one line per member of every archive: archive, name, size,
                crc, method, folder, and the member's bytes as hex (empty for
                a folder); zipfile's answers, never gBASIC's
  and the refusal cases, each one defect:
  dup.zip       two members of one name
  slip.zip      a member named ../evil.txt
  abs.zip       a member named /etc/evil
  link.zip      a Unix symbolic link
  bzip2.zip     a bzip2 member
  crc.zip       a deflated member whose stored CRC-32 is wrong
  enc.zip       a member flagged as encrypted
  trunc.zip     plain.zip cut short (no end-of-central-directory)
  short.zip     plain.zip cut inside its archive comment
"""
import io
import os
import random
import struct
import sys
import zipfile

out, seed = sys.argv[1], int(sys.argv[2])
rng = random.Random(seed)
expect = []


def record(path):
    with zipfile.ZipFile(path) as z:
        for i in z.infolist():
            data = b"" if i.is_dir() else z.read(i)
            method = {0: "store", 8: "deflate", 12: "bzip2"}.get(i.compress_type, "other")
            expect.append("%s|%s|%d|%d|%s|%s|%s" % (os.path.basename(path), i.filename, i.file_size, i.CRC, method,
                                                    "true" if i.is_dir() else "false", data.hex()))


def info(name, method=zipfile.ZIP_DEFLATED, when=(2026, 11, 5, 9, 30, 12)):
    zi = zipfile.ZipInfo(name, date_time=when)
    zi.compress_type = method
    return zi


# --- plain ---------------------------------------------------------------------
p = os.path.join(out, "plain.zip")
with zipfile.ZipFile(p, "w") as z:
    z.writestr(info("readme.txt", zipfile.ZIP_STORED), b"stored, not compressed\n")
    z.writestr(info("data/"), b"")
    z.writestr(info("data/invoices.csv"), b"id,amount\n" + b"".join(b"%d,%d.00\n" % (i, i * 7) for i in range(400)))
    z.writestr(info("data/café.txt"), "naïve café\n".encode())        # UTF-8 flag set by zipfile
    z.writestr(info("empty.bin", zipfile.ZIP_STORED), b"")
    z.writestr(info("nul.bin"), b"a\x00b\x00\xff\xfe")
    z.comment = b"from the partner, with a comment"
# a cp437 name: written by hand into a second archive, flag bit 11 clear
cp = os.path.join(out, "cp437.zip")
buf = io.BytesIO()
with zipfile.ZipFile(buf, "w") as z:
    z.writestr(info("PLACEHOLD.TX"), b"from an old tool\n")
raw = buf.getvalue().replace(b"PLACEHOLD.TX", "RÉSUMÉ_1.TXT".encode("cp437"))
# the two names are the same length, so every offset stays right
assert len("RÉSUMÉ_1.TXT".encode("cp437")) == len(b"PLACEHOLD.TX")
open(cp, "wb").write(raw)
# a data descriptor: zipfile writes one when the output cannot seek
dd = os.path.join(out, "stream.zip")


class Unseekable(io.RawIOBase):
    def __init__(self, f):
        self.f = f

    def writable(self):
        return True

    def write(self, b):
        return self.f.write(b)


with open(dd, "wb") as raw_f:
    with zipfile.ZipFile(Unseekable(raw_f), "w", zipfile.ZIP_DEFLATED) as z:
        with z.open(info("streamed.txt"), "w") as w:
            w.write(b"written through a data descriptor\n" * 50)
for path in (p, cp, dd):
    record(path)

# --- random ----------------------------------------------------------------------
for n in range(40):
    path = os.path.join(out, "random_%d.zip" % n)
    with zipfile.ZipFile(path, "w") as z:
        for k in range(rng.randint(1, 12)):
            size = rng.choice([0, 1, 7, 100, 4096, 70000])
            kind = rng.random()
            if kind < 0.4:
                data = bytes(rng.getrandbits(8) for _ in range(size))
            else:
                data = bytes(rng.choice(b"abc,\n0123") for _ in range(size))
            method = rng.choice([zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED])
            z.writestr(info("m%d_%d/%s" % (n, k, rng.choice(["a.csv", "b.txt", "c.bin"])), method), data)
    record(path)

# --- refusals --------------------------------------------------------------------
with zipfile.ZipFile(os.path.join(out, "dup.zip"), "w") as z:
    z.writestr(info("same.txt"), b"one")
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        z.writestr(info("same.txt"), b"two")
with zipfile.ZipFile(os.path.join(out, "slip.zip"), "w") as z:
    z.writestr(info("fine.txt"), b"fine")
    z.writestr(info("../evil.txt"), b"evil")
with zipfile.ZipFile(os.path.join(out, "abs.zip"), "w") as z:
    zi = info("PLACEHOLDE")
    z.writestr(zi, b"evil")
raw = open(os.path.join(out, "abs.zip"), "rb").read().replace(b"PLACEHOLDE", b"/etc/evil_")
open(os.path.join(out, "abs.zip"), "wb").write(raw)
with zipfile.ZipFile(os.path.join(out, "link.zip"), "w") as z:
    zi = info("link", zipfile.ZIP_STORED)
    zi.create_system = 3
    zi.external_attr = (0o120777 << 16)
    z.writestr(zi, b"/etc/passwd")
with zipfile.ZipFile(os.path.join(out, "bzip2.zip"), "w") as z:
    z.writestr(info("b.txt", zipfile.ZIP_BZIP2), b"bzip2 bytes " * 20)
buf = io.BytesIO()
with zipfile.ZipFile(buf, "w") as z:
    z.writestr(info("c.txt"), b"a CRC that is wrong")
raw = bytearray(buf.getvalue())
crc = struct.pack("<I", zipfile.crc32(b"a CRC that is wrong"))
cd = raw.rfind(b"PK\x01\x02")
i = raw.index(crc, cd)
raw[i] ^= 0xFF
open(os.path.join(out, "crc.zip"), "wb").write(bytes(raw))
buf = io.BytesIO()
with zipfile.ZipFile(buf, "w") as z:
    z.writestr(info("e.txt"), b"pretend this is encrypted")
raw = bytearray(buf.getvalue())
cd = raw.rfind(b"PK\x01\x02")
raw[cd + 8] |= 0x01
open(os.path.join(out, "enc.zip"), "wb").write(bytes(raw))
whole = open(p, "rb").read()
# cut past the end record (22 bytes and the 32-byte comment), and cut into the comment
open(os.path.join(out, "trunc.zip"), "wb").write(whole[:-60])
open(os.path.join(out, "short.zip"), "wb").write(whole[:-10])

with open(os.path.join(out, "expect.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(expect) + "\n")

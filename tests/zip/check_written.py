#!/usr/bin/env python3
"""Each archive zip.write made holds exactly the members
make_zip_write_cases.py chose, in order, by Python's zipfile: names, bytes,
folders, CRCs it checks itself, and the 1980-01-01 date. zipfile reads the
central directory alone, so each local header is read here byte for byte and
held to it: a streaming reader sees only the local header.

  check_written.py DIR K...
"""
import os
import struct
import sys
import zipfile

d = sys.argv[1]
bad = n = 0
for k in sys.argv[2:]:
    with open(os.path.join(d, f"case_{k}.spec"), encoding="utf-8") as f:
        spec = [line.split("\t", 1) for line in f.read().splitlines() if line]
    with zipfile.ZipFile(os.path.join(d, f"case_{k}.zip")) as z:
        infos = z.infolist()
        if z.testzip() is not None:
            print(f"MISMATCH case {k}: zipfile's CRC check failed at {z.testzip()}")
            bad += 1
        if [i.filename for i in infos] != [name for _, name in spec]:
            print(f"MISMATCH case {k}: names {[i.filename for i in infos]} want {[nm for _, nm in spec]}")
            bad += 1
            continue
        for (i, name), info in zip(spec, infos):
            n += 1
            if name.endswith("/"):
                ok = info.is_dir() and info.file_size == 0
            else:
                with open(os.path.join(d, f"case_{k}", f"m_{i}"), "rb") as f:
                    want = f.read()
                ok = z.read(info) == want and not info.is_dir()
            ok = ok and info.date_time == (1980, 1, 1, 0, 0, 0) and not info.extra
            ok = ok and info.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)
            # the local header: signature, version, flags, method, time, date,
            # crc, sizes, name length, extra length, then the name
            with open(os.path.join(d, f"case_{k}.zip"), "rb") as raw:
                raw.seek(info.header_offset)
                h = raw.read(30)
                sig, _ver, flags, method, t, dt, crc, packed, size, nn, xn = struct.unpack("<IHHHHHIIIHH", h)
                lname = raw.read(nn)
            ok = ok and sig == 0x04034B50 and flags == info.flag_bits and method == info.compress_type
            ok = ok and (t, dt) == (0, 0x21) and crc == info.CRC and packed == info.compress_size
            ok = ok and size == info.file_size and xn == 0 and lname == info.orig_filename.encode(
                "utf-8" if info.flag_bits & 0x800 else "cp437")
            if not ok:
                print(f"MISMATCH case {k} member {name!r}")
                bad += 1
print(f"members: {n}")
print(f"mismatches: {bad}")

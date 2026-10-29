#!/usr/bin/env python3
"""An independent reader of 2.C's format 1, written from
docs/gsql_storage_design.md section 3.1.5 and not from src/gsql/lsm.c.

    format_check.py DIR

Decodes every file the manifest names, checks every checksum and every field
the section fixes, follows every blob pointer into its blob file, and walks
the logs' chains. Prints one MISMATCH per disagreement, then a census and
`ok` (or `FAILED`). Every other check in the suite reads the store back
through the engine's own reader, so a writer and a reader agreeing on a wrong
layout pass all of them; this one cannot be satisfied that way.
"""
import os
import struct
import sys

# ---- CRC32C (Castagnoli, reflected 0x82F63B78) --------------------------------
TABLE = []
for i in range(256):
    c = i
    for _ in range(8):
        c = (c >> 1) ^ 0x82F63B78 if c & 1 else c >> 1
    TABLE.append(c)


def crc32c(b):
    c = 0xFFFFFFFF
    for x in b:
        c = TABLE[(c ^ x) & 0xFF] ^ (c >> 8)
    return c ^ 0xFFFFFFFF


MAGIC = b"GSQLDB\x00\x01"
SEAL = 28
problems = []
census = {"sorted files": 0, "data blocks": 0, "entries": 0, "blob files": 0,
          "blob values": 0, "log records": 0}


def bad(msg):
    problems.append(msg)
    print("MISMATCH " + msg)


def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def u64(b, o): return struct.unpack_from("<Q", b, o)[0]


def leb(b, o):
    v = shift = 0
    while True:
        if o >= len(b):
            raise ValueError("a LEB128 runs off the end")
        x = b[o]
        o += 1
        v |= (x & 0x7F) << shift
        shift += 7
        if not x & 0x80:
            return v, o


def zero(b, what):
    if any(b):
        bad(what + " is not zero while encryption is off")


# ---- the manifest ------------------------------------------------------------
def read_manifest(path):
    m = open(path, "rb").read()
    if len(m) < 80 + 32 + 4:
        bad("the manifest is %d bytes" % len(m))
        return None
    if m[:8] != MAGIC:
        bad("the manifest's magic is %r" % m[:8])
    if u16(m, 8) != 1:
        bad("the manifest's major is %d, not 1" % u16(m, 8))
    if u32(m, 76) != crc32c(m[:76]):
        bad("the manifest header's checksum at [76,80) does not cover [0,76)")
    if u32(m, len(m) - 4) != crc32c(m[:-4]):
        bad("the manifest's final checksum does not cover what precedes it")
    if u32(m, 40) & 1:
        bad("the manifest is flagged encrypted")
    zero(m[44:76], "the key-check block")
    zero(m[80:112], "the manifest's key epoch and seal")
    ps = u32(m, 12)
    if ps < 512 or ps & (ps - 1):
        bad("the block target %d is not a power of two from 512" % ps)
    ident = m[16:32]
    o = 112
    end = len(m) - 4

    def g64():
        nonlocal o
        v = u64(m, o)
        o += 8
        return v

    def gbytes():
        nonlocal o
        n, o2 = leb(m, o)
        v = m[o2:o2 + n]
        o = o2 + n
        return v

    man = {"identity": ident, "next_file": g64(), "log_first": g64(), "last_seq": g64(), "files": []}
    for level in range(7):
        for _ in range(g64()):
            f = {"level": level, "num": g64(), "size": g64(), "entries": g64(), "smallest": gbytes(),
                 "small_seq": g64(), "largest": gbytes(), "large_seq": g64(), "seq_over": g64(),
                 "own_blob": g64(), "blobs": {}}
            for _ in range(g64()):
                bn = g64()
                f["blobs"][bn] = g64()
            man["files"].append(f)
    for _ in range(g64()):
        g64(), g64()
    if o != end:
        bad("the manifest body ends at %d, not at its checksum (%d)" % (o, end))
    return man


# ---- a file's header ---------------------------------------------------------
def check_header(b, kind, num, ident, name):
    if len(b) < 64:
        bad("%s is %d bytes, short of its header" % (name, len(b)))
        return
    if b[:8] != MAGIC:
        bad("%s: magic %r" % (name, b[:8]))
    if u32(b, 60) != crc32c(b[:60]):
        bad("%s: the header checksum does not cover its 60 bytes" % name)
    if u16(b, 8) != 1:
        bad("%s: major %d" % (name, u16(b, 8)))
    if b[12] != kind:
        bad("%s: kind %d, want %d" % (name, b[12], kind))
    if b[13]:
        bad("%s: flags %d" % (name, b[13]))
    if b[16:32] != ident:
        bad("%s: the identity is not the manifest's" % name)
    if u64(b, 32) != num:
        bad("%s: the header names file %d" % (name, u64(b, 32)))
    zero(b[14:16] + b[40:60], name + "'s reserved bytes and key epoch")


def trailer(blk, what):
    """The u32 before the seal, and the body before it; checks the rest."""
    if len(blk) < 4 + SEAL + 4:
        bad(what + " is shorter than its trailer")
        return None, b""
    if u32(blk, len(blk) - 4) != crc32c(blk[:-4]):
        bad(what + ": the checksum does not cover the block")
    zero(blk[-4 - SEAL:-4], what + "'s seal")
    return u32(blk, len(blk) - 4 - SEAL - 4), blk[:-4 - SEAL - 4]


# ---- sorted and blob files ---------------------------------------------------
blob_cache = {}


def blob_file(d, num, ident):
    if num not in blob_cache:
        name = "db.%d.blob" % num
        p = os.path.join(d, name)
        if not os.path.exists(p):
            bad("a pointer names %s, which is not there" % name)
            blob_cache[num] = None
        else:
            b = open(p, "rb").read()
            check_header(b, 2, num, ident, name)
            census["blob files"] += 1
            blob_cache[num] = b
    return blob_cache[num]


def check_sorted(d, f, ident):
    name = "db.%d.sst" % f["num"]
    b = open(os.path.join(d, name), "rb").read()
    census["sorted files"] += 1
    if len(b) != f["size"]:
        bad("%s is %d bytes, the manifest says %d" % (name, len(b), f["size"]))
    check_header(b, 1, f["num"], ident, name)
    foot = b[-64:]
    if u32(foot, 60) != crc32c(foot[:60]):
        bad(name + ": the footer's checksum does not cover its 60 bytes")
    zero(foot[32:60], name + "'s footer seal")
    ioff, isz, boff, bsz, entries = u64(foot, 0), u32(foot, 8), u64(foot, 12), u32(foot, 20), u64(foot, 24)
    if entries != f["entries"]:
        bad("%s: the footer counts %d entries, the manifest %d" % (name, entries, f["entries"]))
    nidx, ibody = trailer(b[ioff:ioff + isz], name + " index")
    if nidx != entries:
        bad("%s: the index counts %d entries" % (name, nidx))
    bits, bbody = trailer(b[boff:boff + bsz], name + " bloom")
    if bits is not None and len(bbody) != bits // 8 + 1:
        bad("%s: %d bloom bytes for %d bits" % (name, len(bbody), bits))
    o, seen, expect_off, last = 0, 0, 64, None
    while o < len(ibody):
        kn, o = leb(ibody, o)
        lastkey = ibody[o:o + kn]
        o += kn
        lastseq, off, size = u64(ibody, o), u64(ibody, o + 8), u32(ibody, o + 16)
        o += 20
        if off != expect_off:
            bad("%s: a data block at %d, where the one before ended at %d" % (name, off, expect_off))
        expect_off = off + size
        count, body = trailer(b[off:off + size], "%s block at %d" % (name, off))
        census["data blocks"] += 1
        q, n = 0, 0
        while q < len(body):
            kn, q = leb(body, q)
            key = body[q:q + kn]
            q += kn
            seq, typ = u64(body, q), body[q + 8]
            q += 9
            vn, q = leb(body, q)
            val = body[q:q + vn]
            q += vn
            n += 1
            if len(key) < 8:
                bad("%s: a key of %d bytes has no tree id" % (name, len(key)))
            if last is not None and (key < last[0] or (key == last[0] and seq >= last[1])):
                bad("%s: entries out of internal order" % name)
            last = (key, seq)
            if typ not in (0, 1, 2):
                bad("%s: entry type %d" % (name, typ))
            if typ == 2:
                check_pointer(d, val, ident, name)
        if n != count:
            bad("%s block at %d: %d entries, the trailer says %d" % (name, off, n, count))
        if last is not None and (last[0] != lastkey or last[1] != lastseq):
            bad("%s block at %d: the index names another last entry" % (name, off))
        seen += n
    if expect_off != ioff:
        bad("%s: the data blocks end at %d, the index begins at %d" % (name, expect_off, ioff))
    if seen != entries:
        bad("%s: %d entries in the blocks, %d in the footer" % (name, seen, entries))
    census["entries"] += seen
    if last is not None and last[0] != f["largest"]:
        bad("%s: the last key is not the manifest's largest" % name)


def check_pointer(d, ptr, ident, name):
    if len(ptr) != 24:
        bad("%s: a blob pointer of %d bytes" % (name, len(ptr)))
        return
    num, off, ln, crc = u64(ptr, 0), u64(ptr, 8), u32(ptr, 16), u32(ptr, 20)
    b = blob_file(d, num, ident)
    if b is None:
        return
    rec = b[off:off + SEAL + ln]
    if off < 64 or len(rec) != SEAL + ln:
        bad("db.%d.blob: a value at %d of %d bytes runs outside the file" % (num, off, ln))
        return
    if crc32c(rec) != crc:
        bad("db.%d.blob: the pointer's checksum does not cover the seal and the value at %d" % (num, off))
    zero(rec[:SEAL], "db.%d.blob's seal at %d" % (num, off))
    census["blob values"] += 1


# ---- the logs ----------------------------------------------------------------
def check_log(d, num):
    b = open(os.path.join(d, "db.log.%d" % num), "rb").read()
    chain = ((num * 0x9E3779B1) & 0xFFFFFFFF) ^ 0x5BD1E995
    o = 0
    while o + 8 <= len(b):
        ln, crc = u32(b, o), u32(b, o + 4)
        if ln < 36 + 24 or o + 8 + ln > len(b):
            break
        rec = b[o + 8:o + 8 + ln]
        if crc32c(rec) != crc or u32(rec, 36 + 12) != chain:
            break  # the end: zeros, or what a lost commit or a recycled log left
        if rec[0] or rec[1:4] != b"\0\0\0" or u32(rec, 4):
            bad("db.log.%d at %d: flags, reserved bytes or key epoch set" % (num, o))
        zero(rec[8:36], "db.log.%d's seal at %d" % (num, o))
        p = rec[36:]
        count, q = u32(p, 8), 20
        for _ in range(count):
            q += 1
            kn, q = leb(p, q)
            q += kn
            vn, q = leb(p, q)
            q += vn
        nret = u32(p, q)
        if q + 4 + 8 * nret != len(p):
            bad("db.log.%d at %d: the payload does not end where its length says" % (num, o))
        census["log records"] += 1
        chain = crc
        o += 8 + ln


def main():
    d = sys.argv[1]
    man = read_manifest(os.path.join(d, "db"))
    if man is not None:
        for f in man["files"]:
            check_sorted(d, f, man["identity"])
        num = man["log_first"]
        while os.path.exists(os.path.join(d, "db.log.%d" % num)):
            check_log(d, num)
            num += 1
    print("census " + ", ".join("%s %d" % kv for kv in census.items()))
    for need in ("sorted files", "blob values", "log records"):
        if census[need] == 0:
            bad("no %s were read: the format was not exercised" % need)
    print("FAILED" if problems else "ok")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

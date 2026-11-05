"""Cases for transcode, valid_utf8 and first_invalid_utf8, decided by Python's
codecs and not by gBASIC.

    encoding_cases.py OUT_FILE

Each line: INPUT_HEX FROM TO RESULT, RESULT being the output's hex or
ERR:<offset> (decoding: the byte offset Python's UnicodeDecodeError starts
at; encoding: the character index its UnicodeEncodeError starts at). Inputs
are text from a pool that crosses every boundary the encodings have (ASCII,
Latin-1's upper half, Windows-1252's 0x80-0x9F, the BMP, astral planes)
encoded by Python, then some of them corrupted at random.

One deliberate difference from Python is not sampled: "utf-16" with no
byte-order mark. Python reads it in the machine's order, gBASIC big-endian
(RFC 2781); the fixture checks that case against "utf-16-be" itself.
"""
import random
import sys

rng = random.Random(1252)
pool = (list("abcXYZ09 ,\n") + [chr(c) for c in range(0xA0, 0x100, 7)] +
        ["€", "‚", "ƒ", "„", "…", "†", "‡", "ˆ", "‰", "Š", "‹", "Œ", "Ž", "‘", "’", "“", "”", "•", "–", "—",
         "˜", "™", "š", "›", "œ", "ž", "Ÿ", "\u0081", "\u008d", "Ā", "ł", "ı", "Ω", "中", "￿", "\U0001d11e",
         "\U0010ffff", "\U0001f600", "\x00"])
pycodec = {"utf-8": "utf-8", "latin-1": "latin-1", "windows-1252": "cp1252", "utf-16le": "utf-16-le",
           "utf-16be": "utf-16-be", "utf-16": "utf-16"}
names = list(pycodec)


def py(data, f, t):
    try:
        text = data.decode(pycodec[f])
    except UnicodeDecodeError as e:
        return "ERR:%d" % e.start
    try:
        out = text.encode(pycodec[t])
    except UnicodeEncodeError as e:
        return "ERR:%d" % e.start
    if t == "utf-16" and not out.startswith(b"\xff\xfe"):
        raise SystemExit("this machine's Python writes utf-16 big-endian; the oracle assumes FF FE")
    return out.hex() or "-"


lines = []
for _ in range(4000):
    f = rng.choice(names)
    t = rng.choice(names)
    text = "".join(rng.choice(pool) for _ in range(rng.randint(0, 8)))
    try:
        data = text.encode(pycodec[f])
    except UnicodeEncodeError:
        data = text.encode("utf-8")      # not representable: feed its UTF-8 bytes, usually refused
    if f == "utf-16" and not data.startswith((b"\xff\xfe", b"\xfe\xff")):
        continue
    if data and rng.random() < 0.3:
        b = bytearray(data)
        for _ in range(rng.randint(1, 2)):
            at = rng.randrange(len(b))
            if f == "utf-16" and at < 2:
                continue
            b[at] = rng.randrange(256)
        if rng.random() < 0.3 and len(b) > 1:
            del b[rng.randrange(2 if f == "utf-16" else 0, len(b)) if len(b) > 2 else -1]
        data = bytes(b)
    lines.append("%s %s %s %s" % (data.hex() or "-", f, t, py(data, f, t)))
# The edges random corruption seldom reaches: encoded surrogates, overlong
# forms, past U+10FFFF, and sequences cut short, each read as UTF-8 and written
# to every encoding.
edges = [b"\xed\xa0\x80", b"a\xed\xbf\xbfb", b"\xed\x9f\xbf", b"\xee\x80\x80", b"\xc0\x80", b"\xc1\xbf",
         b"\xe0\x80\xaf", b"\xe0\x9f\xbf", b"\xe0\xa0\x80", b"\xf0\x8f\xbf\xbf", b"\xf0\x90\x80\x80",
         b"\xf4\x8f\xbf\xbf", b"\xf4\x90\x80\x80", b"\xf5\x80\x80\x80", b"\xe2\x82", b"ok\xf0\x9f\x98",
         b"\x80", b"\xbf", b"\xfe", b"\xff"]
for e in edges:
    for t in names:
        lines.append("%s %s %s %s" % (e.hex(), "utf-8", t, py(e, "utf-8", t)))
with open(sys.argv[1], "w") as out:
    out.write("\n".join(lines) + "\n")

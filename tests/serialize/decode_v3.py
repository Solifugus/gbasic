#!/usr/bin/env python3
"""An INDEPENDENT decoder for `serialize` version 3, written from the layout in
docs/gsql_storage_design.md §3.3 and nothing else.

Why it exists: every other check of the format reads a payload back through
gBASIC's own `deserialize`, so a writer and reader that agree on a WRONG layout
(a byte order flipped on both sides, a field in the wrong place) pass all of
them. This reads the bytes with Python's `struct` and compares against values
written here by hand from tests/serialize/serialize_test.bas's battery -- not
against anything gBASIC printed.

Input: `name<TAB>hex` lines on stdin (the fixture's `dump` mode).
Output: `ok name` or `MISMATCH name: ...`, then a count line.
"""
import math
import struct
import sys

TAGS = {1: "null", 2: "unknown", 3: "bool", 4: "number", 5: "string",
        6: "array", 7: "record", 8: "datetime", 9: "duration", 10: "money",
        11: "file", 12: "dir"}


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, n):
        if self.pos + n > len(self.data):
            raise ValueError("truncated")
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out

    def u8(self):
        return self.take(1)[0]

    def uleb(self):
        value, shift = 0, 0
        while True:
            byte = self.u8()
            value |= (byte & 0x7F) << shift
            if not byte & 0x80:
                return value
            shift += 7
            if shift > 63:
                raise ValueError("overlong LEB128")

    def value(self):
        tag = TAGS.get(self.u8())
        if tag == "null":
            return ("nothing",)
        if tag == "unknown":
            return ("unknown",)
        if tag == "bool":
            return ("bool", self.u8() == 1)
        if tag == "number":
            flag = self.u8()
            raw = self.take(8)
            if flag == 1:
                return ("exact", struct.unpack("<q", raw)[0])
            if flag == 0:
                return ("inexact", struct.unpack("<d", raw)[0])
            raise ValueError("bad exactness flag")
        if tag in ("string", "file", "dir"):
            return (tag, self.take(self.uleb()))
        if tag == "array":
            return ("array", [self.value() for _ in range(self.uleb())])
        if tag == "record":
            fields = []
            for _ in range(self.uleb()):
                name = self.take(self.uleb()).decode("utf-8")
                fields.append((name, self.value()))
            return ("record", dict(fields))
        if tag == "datetime":
            parts = struct.unpack("<6i", self.take(24))
            return ("datetime", parts, self.u8(), self.u8())
        if tag == "duration":
            return ("duration", struct.unpack("<7i", self.take(28)))
        if tag == "money":
            units = struct.unpack("<q", self.take(8))[0]
            exponent = self.u8()
            numeric = struct.unpack("<H", self.take(2))[0]
            code = self.take(self.u8()).decode("ascii")
            return ("money", units, exponent, numeric, code)
        raise ValueError("unknown tag")


def s(text):
    return ("string", text.encode("utf-8"))


# Precision codes: year 1 .. second 6, as DateTimePrecision in src/eval.c.
EXPECTED = {
    "zero": ("exact", 0),
    "exact_big": ("exact", 9007199254740993),
    "exact_min": ("exact", -2**63),
    "exact_max": ("exact", 2**63 - 1),
    "neg_exact": ("exact", -42),
    "half": ("inexact", 1.5),
    "neg_zero": ("inexact", -0.0),
    "tiny": ("inexact", 5e-324),
    "inf": ("inexact", math.inf),
    "neg_inf": ("inexact", -math.inf),
    "empty": s(""),
    "nul_utf8": s("a\x00é☃"),
    "long": s("x" * 300),
    "yes": ("bool", True),
    "no": ("bool", False),
    "nothing": ("nothing",),
    "unknown": ("unknown",),
    "date": ("datetime", (2026, 6, 25, 0, 0, 0), 0, 3),
    "datetime": ("datetime", (2026, 6, 25, 13, 4, 5), 0, 6),
    "time": ("datetime", (0, 0, 0, 7, 8, 9), 1, 6),
    "span": ("duration", (0, 0, 0, 2, 3, 0, 0)),
    # Storage scale is the minor unit plus four guard digits.
    "usd": ("money", 19951200, 2, 840, "USD"),
    "jpy": ("money", 12000000, 0, 392, "JPY"),
    "kwd": ("money", -31250000, 3, 414, "KWD"),
    "nested": ("record", {
        "list": ("array", [("exact", 1),
                           ("array", [("inexact", 2.5), s("x")]),
                           ("record", {"k": s("v")})]),
        "u": ("unknown",)}),
    "file": ("file", b"/etc/hostname"),
    "dir": ("dir", b"/tmp"),
}


def same(a, b):
    """Structural equality that tells -0.0 from 0.0, which `==` does not."""
    if isinstance(a, float) and isinstance(b, float):
        return a == b and math.copysign(1, a) == math.copysign(1, b)
    if isinstance(a, (tuple, list)) and isinstance(b, (tuple, list)):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
    return type(a) is type(b) and a == b


def main():
    checks = mismatches = 0
    seen = set()
    for line in sys.stdin:
        name, _, hexed = line.rstrip("\n").partition("\t")
        data = bytes.fromhex(hexed)
        checks += 1
        seen.add(name)
        try:
            if data[:4] != b"gBS\x03":
                raise ValueError("header %r" % data[:4])
            r = Reader(data[4:])
            got = r.value()
            if r.pos != len(r.data):
                raise ValueError("%d trailing bytes" % (len(r.data) - r.pos))
        except ValueError as err:
            mismatches += 1
            print("MISMATCH %s: %s" % (name, err))
            continue
        if name not in EXPECTED:
            mismatches += 1
            print("MISMATCH %s: no expected value written for it" % name)
        elif not same(got, EXPECTED[name]):
            mismatches += 1
            print("MISMATCH %s: decoded %r, want %r" % (name, got, EXPECTED[name]))
        else:
            print("ok %s" % name)
    for name in sorted(set(EXPECTED) - seen):
        mismatches += 1
        print("MISMATCH %s: expected but not dumped" % name)
    print("decoded %d payloads, %d mismatches" % (checks, mismatches))
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())

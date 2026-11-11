"""Cases for valid_utf8 and bytes_xor, decided by Python and not by gBASIC.

    bytes_cases.py OUT_FILE

Each line: HEX VALID XOR_KEY_HEX XOR_RESULT_HEX. VALID is Python's strict
decoder's answer (bytes.decode("utf-8"), which refuses overlongs, surrogates
and anything past U+10FFFF, as RFC 3629 does). The strings are biased to the
edges: lead bytes, continuation bytes, the surrogate block and the top of the
range, and truncations of valid sequences.
"""
import random
import sys

rng = random.Random(6455)
edges = [0x00, 0x7F, 0x80, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF, 0xE0, 0xED, 0xEF, 0xF0, 0xF4, 0xF5, 0xFF,
         0x9F, 0xA0, 0x8F, 0x90]
valid_samples = ["a", "é", "€", "𝄞", "\U0010FFFF", "퟿", "", "ࠀ", "߿", "\U00010000"]
cases = []
for _ in range(3000):
    kind = rng.random()
    if kind < 0.4:
        b = bytes(rng.choice(edges) for _ in range(rng.randint(1, 6)))
    elif kind < 0.7:
        s = "".join(rng.choice(valid_samples) for _ in range(rng.randint(1, 4)))
        b = s.encode()
        if rng.random() < 0.5 and len(b) > 1:
            b = b[: rng.randint(1, len(b) - 1)]
    else:
        b = bytes(rng.randrange(256) for _ in range(rng.randint(0, 8)))
    cases.append(b)
cases += [b"", b"\xed\xa0\x80", b"\xed\x9f\xbf", b"\xf4\x90\x80\x80", b"\xf4\x8f\xbf\xbf", b"\xc0\x80",
          b"\xe0\x80\xaf", b"\xf0\x80\x80\xaf"]
with open(sys.argv[1], "w", newline="\n", encoding="utf-8") as f:
    for b in cases:
        try:
            b.decode("utf-8")
            ok = "true"
        except UnicodeDecodeError:
            ok = "false"
        key = bytes(rng.randrange(256) for _ in range(rng.randint(1, 5)))
        x = bytes(c ^ key[i % len(key)] for i, c in enumerate(b))
        f.write("%s %s %s %s\n" % (b.hex() or "-", ok, key.hex(), x.hex() or "-"))

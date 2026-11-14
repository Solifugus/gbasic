"""Write a serialize v1/v2 payload as the OTHER byte order would have.

Versions 1 and 2 wrote integers, doubles and C structs in the writing
machine's byte order and recorded nothing of it, so a file written on x86
reads on s390x only because deserialize retries byte-swapped (2026-11-14).
This walks a payload by the v1/v2 layout and reverses every multi-byte field,
giving the payload a big-endian machine would have written for the same
value. The twins are committed beside the real fixtures, and
tests/serialize/serialize_test.bas requires each to read as its original:
on x86 that exercises the swapped path, on s390x the original does.

    python3 -I tests/serialize/other_order.py IN.hex > OUT.hex
"""
import sys

NULL, UNKNOWN, BOOL, NUMBER, STRING, ARRAY, RECORD, DATETIME, DURATION, \
    MONEY, FILE, DIR = range(1, 13)


def twin(data):
    if data[:3] != b"gBS" or data[3] not in (1, 2):
        sys.exit("other_order: not a serialize v1/v2 payload")
    version = data[3]
    out = bytearray(data[:4])
    pos = 4

    def take(n):
        nonlocal pos
        if pos + n > len(data):
            sys.exit("other_order: truncated payload")
        chunk = data[pos:pos + n]
        pos += n
        return chunk

    def flip(n):            # one field of n bytes, reversed
        out.extend(take(n)[::-1])

    def count():            # a length or count: u64, writer's order
        raw = take(8)
        out.extend(raw[::-1])
        return int.from_bytes(raw, "little")

    def value():
        tag = take(1)[0]
        out.append(tag)
        if tag in (NULL, UNKNOWN):
            return
        if tag == BOOL:
            out.extend(take(1))
        elif tag == NUMBER:
            flip(8)
        elif tag in (STRING, FILE, DIR):
            out.extend(take(count()))
        elif tag == ARRAY:
            for _ in range(count()):
                value()
        elif tag == RECORD:
            for _ in range(count()):
                out.extend(take(count()))
                value()
        elif tag == DATETIME:
            for _ in range(8):      # year .. second, time_only, precision
                flip(4)
        elif tag == DURATION:
            for _ in range(7):
                flip(4)
        elif tag == MONEY:
            if version == 1:
                flip(8)             # cents
            else:
                flip(8)             # units
                flip(2)             # currency
                out.extend(take(1))  # exponent
        else:
            sys.exit("other_order: tag %d has no v1/v2 layout here" % tag)

    value()
    if pos != len(data):
        sys.exit("other_order: %d bytes after the value" % (len(data) - pos))
    return bytes(out)


if __name__ == "__main__":
    with open(sys.argv[1]) as f:
        src = bytes.fromhex(f.read().strip())
    print(twin(src).hex())

#!/usr/bin/env python3
"""Members for tests/zip/write.bas to put in a ZIP (zip.write, gap G55), made
here so what the archive must hold is decided outside gBASIC.

  make_zip_write_cases.py DIR SEED

Writes DIR/case_K/ for K in 0..19: each member's bytes as a file m_I, and
DIR/case_K.spec, one line per member, "I<TAB>name" (a folder's line has no
file). tests/run_zip.sh then reads the archives back with zipfile.
"""
import os
import random
import sys

out, seed = sys.argv[1], int(sys.argv[2])
rng = random.Random(seed)
words = ["data", "x", "é", "名前", "part", "sheet1.xml", "a b", "[Content_Types].xml"]
for k in range(20):
    d = os.path.join(out, f"case_{k}")
    os.makedirs(d)
    names, lines = set(), []
    for i in range(rng.randint(1, 12)):
        while True:
            depth = rng.randint(0, 3)
            name = "/".join(rng.choice(words) + str(rng.randint(0, 9)) for _ in range(depth + 1))
            folder = rng.random() < 0.1
            if folder:
                name += "/"
            if name not in names:
                break
        names.add(name)
        if folder:
            lines.append(f"{i}\t{name}")
            continue
        kind = rng.randint(0, 3)
        if kind == 0:
            data = b""
        elif kind == 1:
            data = bytes(rng.randrange(256) for _ in range(rng.randint(1, 3000)))   # will not shrink: stored
        elif kind == 2:
            data = (rng.choice(words).encode() + b"\x00\r\n") * rng.randint(1, 4000)  # shrinks: deflated
        else:
            data = bytes([rng.randrange(256)])
        with open(os.path.join(d, f"m_{i}"), "wb") as f:
            f.write(data)
        lines.append(f"{i}\t{name}")
    with open(os.path.join(out, f"case_{k}.spec"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

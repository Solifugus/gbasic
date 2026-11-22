#!/usr/bin/env python3
"""How many times named functions called strcmp, read from a callgrind file.

    strcmp_calls.py CALLGRIND_OUT FUNCTION...

Prints one number: the calls to strcmp made directly by any function whose
name (with callgrind's recursion suffix `'2` removed) is one of FUNCTION, or
by any function at all when FUNCTION is `ALL`.
A call through the PLT shows as an unnamed stub (`???:0x...`) whose own
callee is strcmp, so a stub is counted as strcmp when it calls one. Reads
`callgrind_annotate --tree=calling`, which comes with valgrind.
"""
import re
import subprocess
import sys

out = subprocess.run(["callgrind_annotate", "--tree=calling", "--auto=no",
                      "--threshold=100", sys.argv[1]],
                     capture_output=True, text=True, check=True).stdout
wanted = set(sys.argv[2:])
head = re.compile(r"^\s*[\d,]+ \(\s*[\d.]+%\)\s+\*\s+(\S+)")
call = re.compile(r"^\s*[\d,]+ \(\s*[\d.]+%\)\s+>\s+(\S+) \(([\d,]+)x\)")

def short(fn):
    return fn.rsplit(":", 1)[-1].split("'")[0].split(".isra")[0]

blocks = {}
current = None
for line in out.splitlines():
    m = head.match(line)
    if m:
        current = blocks.setdefault(m.group(1), [])
        continue
    m = call.match(line)
    if m and current is not None:
        current.append((m.group(1), int(m.group(2).replace(",", ""))))

def is_strcmp(fn):
    if "strcmp" in short(fn):
        return True
    return fn.startswith("???:0x") and any("strcmp" in short(c) for c, _ in blocks.get(fn, []))

total = 0
for fn, callees in blocks.items():
    if fn.startswith("???:0x") and is_strcmp(fn):
        continue   # a PLT stub, counted at its callers
    if short(fn) in wanted or ("ALL" in wanted and not is_strcmp(fn)):
        total += sum(n for c, n in callees if is_strcmp(c))
print(total)

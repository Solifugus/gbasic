#!/usr/bin/env python3
"""PLANT A COUNT-BASED SLOT CHOICE IN A COPY, so the scanner beside this has
something it must flag. "No offenders" is equally satisfied by a scanner that
read nothing, and a copy under the work directory is the only safe place to
plant one: src/ is a BUILD INPUT, and the gate's lane scan reports a suite that
writes one -- it was right to when run_qt_gutter.sh did it (§0.29).

Usage: slot_choice_plant.py <source> <destination>
"""
import io
import sys

GOOD = '        } else if (pt == "int") {\n            slot = "fireIndex(int)";\n        }'
BAD = '        } else {\n            slot = "fireIndex(int)";\n        }'

src = io.open(sys.argv[1], encoding="utf-8").read()
bad = src.replace(GOOD, BAD, 1)
if bad == src:
    sys.exit("the plant did not apply -- the scanner would measure nothing")
io.open(sys.argv[2], "w", encoding="utf-8").write(bad)

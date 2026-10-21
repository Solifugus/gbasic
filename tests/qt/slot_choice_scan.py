#!/usr/bin/env python3
"""IS THE RELAY SLOT CHOSEN BY THE SIGNAL'S TYPE NAME, OR BY ITS ARGUMENT COUNT?

A SOURCE TRIPWIRE BECAUSE NOTHING ELSE CAN SEE IT, measured rather than
assumed: QMetaObject::connect by index does NOT check argument types -- it
hands the slot a void* to reinterpret -- so a `toggled(bool)` connected to
`fireIndex(int)` reads four bytes out of one. That is undefined behaviour and
it is also UNOBSERVABLE from here: the value is discarded for anything that is
not a tab widget, and the perturbation that chooses the slot by argument COUNT
leaves every behavioural check green AND valgrind silent (measured -- the bool
sits in an initialised, aligned stack slot, so there is nothing for valgrind to
object to).

THE RULE: every arm that assigns a relay slot must be guarded by an exact type
comparison (`pt == "..."`). An `else` arm that assigns one is the offence.

Prints one line per offence and exits 1 if there were any; exits 0 silently
otherwise, which is why the suite plants a copy as a control -- "no offenders"
is equally satisfied by a scanner that read nothing.
"""
import re
import sys

START = 'const char *slot = "fire()";'
END = "indexOfSlot(slot)"


def offences(path):
    text = open(path, encoding="utf-8").read()
    if START not in text or END not in text:
        return ["the slot-choice block was not found at all"]
    block = text.split(START, 1)[1].split(END, 1)[0]
    found = []
    last_condition = None
    for line in block.splitlines():
        stripped = line.strip()
        # a comment line is not a condition and not an assignment
        if stripped.startswith("*") or stripped.startswith("/*"):
            continue
        if re.search(r"\b(if|else)\b", stripped):
            last_condition = stripped
        m = re.search(r'slot\s*=\s*"([^"]+)"', stripped)
        if m:
            if last_condition is None:
                found.append("`%s` is assigned with no condition at all"
                             % m.group(1))
            elif 'pt == "' not in last_condition:
                found.append("`%s` is chosen by `%s`, which is not a type "
                             "comparison" % (m.group(1), last_condition))
    if not found:
        # AND THE RULE MUST HAVE SOMETHING TO GOVERN: a block that chose no
        # slot at all would pass vacuously.
        if 'pt == "QPoint"' not in block or 'pt == "int"' not in block:
            found.append("neither type is tested, so no slot is chosen by type")
    return found


if __name__ == "__main__":
    bad = []
    for arg in sys.argv[1:]:
        for reason in offences(arg):
            bad.append("%s: %s" % (arg, reason))
    for line in bad:
        print(line)
    sys.exit(1 if bad else 0)

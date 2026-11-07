#!/usr/bin/env python3
"""Every object's hand-written header list holds the headers its source includes.

run_make_deps.sh. The Makefile lists each object's prerequisites by hand, and
`make` rebuilds an object only when one of them changes. A header its source
includes and its rule leaves out is a header whose change leaves that object
STALE: measured 2026-11-07, adding TOKEN_TRY to include/lexer.h renumbered the
tokens, src/frontend.o (whose rule omitted lexer.h) kept the old numbers, and
its token scan for unused `sql` blocks missed every use -- warning 2121 in two
suites of the gate, until a `make clean` elsewhere in the run rebuilt it.

Usage: make_deps_check.py MAKEFILE -- prints one line per gap, exits 1 if any.
"""
import os
import re
import sys

mk = open(sys.argv[1]).read()
mk = re.sub(r'\\\n', ' ', mk)
rules = {}
for line in mk.split('\n'):
    m = re.match(r'^(src/[\w/]+\.o)\s*:(?!=)(.*)$', line)
    if m and 'CPPFLAGS' not in m.group(2):
        rules[m.group(1)] = rules.get(m.group(1), '') + ' ' + m.group(2)
wildcards = lambda p: [re.compile('^' + re.escape(w).replace(r'\*', '[^/]*') + '$')
                       for w in re.findall(r'\$\(wildcard ([^)]+)\)', p)]
gaps = 0
checked = 0
for obj, prereq in sorted(rules.items()):
    src = next((obj[:-2] + e for e in ('.c', '.cpp') if os.path.exists(obj[:-2] + e)), None)
    if not src:
        continue
    checked += 1
    listed = set(prereq.split())
    pats = wildcards(prereq)
    # the source, and every .c the rule lists beside it (eval.c includes the
    # modules), each resolving its own quoted includes from its own folder
    files = [src] + [c for c in listed if c.endswith('.c') and c != src and os.path.exists(c)]
    incs = set()
    for f in files:
        for h in re.findall(r'#include "([^"]+)"', open(f).read()):
            incs.add((os.path.dirname(f), h))
    for base, h in sorted(incs):
        cands = [os.path.normpath(os.path.join(base, h)), os.path.join('include', h)]
        real = next((c for c in cands if os.path.exists(c)), None)
        if not real or real.endswith('.c'):
            continue   # a system header, or a .c the object's rule lists as a source
        if real in listed or any(p.match(real) for p in pats):
            continue
        print('%s: %s includes %s, which its rule does not list' % (obj, src, real))
        gaps += 1
print('checked %d object rules, %d gaps' % (checked, gaps))
sys.exit(1 if gaps else 0)

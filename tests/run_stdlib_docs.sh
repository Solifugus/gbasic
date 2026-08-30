#!/usr/bin/env bash
set -uo pipefail

# Every PUBLIC function in stdlib/*.bas must be documented somewhere.
#
# This exists because 64 of them were not, and nothing could tell. The design
# documents explain WHY each library exists and the cookbooks show recipes, but
# neither is a per-function reference, so a function could ship, be tested, be
# correct, and be undiscoverable. `matrix` was the extreme: eight public
# functions -- the primitives every regression in `stats` is built on -- and
# ZERO mentions in any document. `stats` had 32 undocumented distribution
# functions. The remaining libraries had one or two each.
#
# WHAT "PUBLIC" MEANS HERE is the only thing gBASIC actually enforces: a
# function whose name does not start with `_`. There is no export list and no
# privacy, so a caller can reach anything without an underscore, which makes
# the underscore the entire contract. A helper that should not be called is
# therefore not an exception to this rule -- it is a function that should be
# renamed with a leading underscore, and doing that is the other valid way to
# make this suite pass.
#
# WHAT COUNTS AS DOCUMENTED is deliberately loose: the name appearing in any
# docs/**/*.md or README.md, as `lib.name`, `` `name(` `` or `` `name` ``. This
# checks DISCOVERABILITY, not quality -- a suite that tried to judge whether
# prose was good would either be unfalsifiable or a style linter. What it
# catches is the real failure: a function that exists and is written down
# nowhere.
#
# It also cannot catch the inverse -- documentation for a function that no
# longer exists -- so the second tier does that, since a reference naming a
# removed function is worse than one missing a present one.
#
# THE BUILTIN TIER USES A STRICTER STANDARD, AND THE DIFFERENCE IS THE WHOLE
# POINT. For stdlib the bar is "named in any doc", which is right for a library
# whose design document is where you would read about it. For a BUILTIN it is
# not enough, and that was measured rather than argued: an application author
# needed the bare filename out of a path, could not find `file_name`, and wrote
# their own splitter -- while `file_name` WAS documented, in docs/tutorial.md
# and a historical archive, so every "is it documented" check in this repo said
# yes. What they did was open docs/reference.md, which is where you look a
# function UP, and it was not there. Four path builtins were missing from it and
# 31 in total. So builtins are checked against reference.md specifically; the
# loose standard is exactly the one that let this through.
#
# Headless, no build required, never skips.

cd "$(dirname "$0")/.."

python3 - <<'PY'
import re, os, glob, sys

docs = {}
for p in glob.glob('docs/**/*.md', recursive=True) + ['README.md']:
    docs[p] = open(p, encoding='utf-8', errors='replace').read()
blob = "\n".join(docs.values())

def documented(lib, fn):
    return (f"{lib}.{fn}" in blob) or (f"`{fn}(" in blob) or (f"`{fn}`" in blob)

libs = {}
for f in sorted(glob.glob('stdlib/*.bas')):
    lib = os.path.basename(f)[:-4]
    src = open(f, encoding='utf-8', errors='replace').read()
    libs[lib] = sorted({m for m in re.findall(
        r'^\s*function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(', src, re.M)
        if not m.startswith('_')})

failures = 0
total = 0
print("TIER every public stdlib function is documented")
for lib, fns in sorted(libs.items()):
    missing = [f for f in fns if not documented(lib, f)]
    total += len(fns)
    if missing:
        failures += len(missing)
        print(f"  FAIL {lib}: {len(missing)} undocumented -> {' '.join(missing)}")
if failures == 0:
    print(f"  ok   all {total} public functions across {len(libs)} libraries are documented")

# --- the inverse: a documented name that no longer exists -------------------
print("TIER no documentation for a function that was removed")
known = {f for fns in libs.values() for f in fns}
# `lib.bas` is a FILENAME in prose, not a call, and prose routinely writes
# "edgar.bas (WP-3)" -- which looks exactly like a call to a `bas` function.
IGNORE = {'bas', 'gb', 'md', 'sh'}
# Names that are WRITTEN as calls on purpose while deliberately not existing.
# Each needs a reason, and the reason is the point: an allowlist without one is
# where a real defect goes to be forgotten. Prose is where these belong --
# `stats.mean` was in prose too, and it WAS a bug, so "only check code fences"
# is not a safe rule.
DELIBERATE = {
    # chart_design.md explains, in that same paragraph, that a library cannot
    # DEFINE a function named `new` -- so the constructor is `chart.spec`. The
    # name is named in order to say it does not exist.
    'chart.new':
        'named as the spelling that is impossible; the constructor is chart.spec',
    # Removed in 0.1.0-rc8 once json_encode became a core builtin. The design
    # doc records the removal, which requires naming the thing removed.
    'crypto.json_encode':
        'removed in 0.1.0-rc8; the doc records the removal',
    # Stated future scope, explicitly flagged as not built.
    'llm.embed':
        'declared near-scope future work, not shipped',
}
stale = {}
for path, text in sorted(docs.items()):
    for lib in libs:
        for m in re.findall(r'\b%s\.([a-z_][a-z0-9_]*)\s*\(' % lib, text):
            name = f"{lib}.{m}"
            if m in known or m in IGNORE or name in DELIBERATE or name in stale:
                continue
            stale[name] = path
if stale:
    for name, path in sorted(stale.items()):
        print(f"  FAIL {name} is written as a call in {path} but no library defines it")
    failures += len(stale)
else:
    print(f"  ok   every documented lib.function still exists "
          f"({len(DELIBERATE)} named-but-absent by design)")
    for name, why in sorted(DELIBERATE.items()):
        print(f"       - {name}: {why}")

print("")
print("TIER every registered builtin appears in docs/reference.md")
ref = open('docs/reference.md', encoding='utf-8', errors='replace').read()
bsrc = open('src/builtins.c', encoding='utf-8', errors='replace').read()
builtins = []
for m in re.findall(r'^\s*"([a-z_0-9]+)",?\s*$', bsrc, re.M):
    if m not in builtins:
        builtins.append(m)
if len(builtins) < 100:
    print("  FAIL only %d builtin names parsed out of src/builtins.c --"
          " the registry format changed and this tier is not looking at it"
          % len(builtins))
    failures += 1
else:
    missing = [b for b in builtins
               if not (("`%s(" % b) in ref or ("`%s`" % b) in ref
                       or ("`%s " % b) in ref)]
    total += len(builtins)
    if missing:
        failures += 1
        print("  FAIL %d of %d builtins have no docs/reference.md entry:"
              % (len(missing), len(builtins)))
        for b in missing:
            print("       %s" % b)
        print("       reference.md is where a function is looked UP. A mention"
              " in a design doc or the tutorial does not substitute.")
    else:
        print("  ok   all %d builtins appear in docs/reference.md" % len(builtins))

sys.exit(1 if failures else 0)
PY
rc=$?

if [ "$rc" -ne 0 ]; then
    printf 'FAIL tests/run_stdlib_docs.sh\n'
    printf '     Fix either way: document the function, or rename it with a leading\n'
    printf '     underscore if it was never meant to be called from outside.\n'
    exit 1
fi
printf 'PASS tests/run_stdlib_docs.sh\n'

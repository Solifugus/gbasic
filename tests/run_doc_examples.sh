#!/usr/bin/env bash
set -euo pipefail

# The documentation's gBASIC code is checked by RUNNING it.
#
# WHY THIS EXISTS. Three documentation sweeps in one day each missed the same
# defect class, because each was READING: 163 lines across the docs used `#` as
# a comment, which gBASIC does not have -- every one of those samples dies on
# the first character of the comment if a reader copies it. Beside them sat
# `r = try charge(card)`, teaching a `try` keyword the language has never had,
# and a file example still using the modifier spelling PLAT-BRACE retired in
# rc6. None of it was catchable by eye; all of it falls out of `./gbasic`.
#
# WHY NOT THE COOKBOOK HARNESS. The cookbooks INVERT ownership: the `.bas` file
# owns the code, `tools/sync_*_cookbook.sh` copies it into the page, and the
# suite fails while they disagree. That is right for a page of self-contained
# recipes and wrong here. A tutorial's code is interleaved teaching -- 71 blocks,
# most of them three lines illustrating one form -- and inverting it would turn
# every edit into a sync round trip and every fragment into a file of
# boilerplate. So the page stays the source of truth for its own code, and this
# suite verifies it in place.
#
# THE TIERS
#
#   PARSE   Every block must lex and parse. This is the tier that catches
#           everything listed above, and it needs no output to compare against.
#   RUN     Every block that can stand alone must also RUN to exit 0, which
#           catches what parsing cannot: a builtin that changed signature, a
#           library call that went away.
#   MARKERS Exemptions are declared IN the page, immediately above the fence,
#           and every one must be EARNED -- a block marked `needs-context` that
#           in fact runs clean fails this tier. Without that, the exemption
#           list becomes the place broken samples go to be forgotten.
#
#   <!--fragment: why-->    excused from PARSE and RUN. For deliberate
#                           non-programs: grammar sketches with `statement`
#                           placeholders, lists of syntax that is INVALID on
#                           purpose. The reason is required, not decorative.
#   <!--needs-context-->    must PARSE; excused from RUN. For a snippet that
#                           names something defined in the prose around it, or
#                           wants a file, a database or stdin.
#
# Headless, no network, never skips. GBASIC_PATH points at the source stdlib so
# `load`-bearing examples resolve.

cd "$(dirname "$0")/.."

make >/dev/null

export GBASIC_PATH=stdlib

# Every page whose ```basic blocks claim to be gBASIC, and how hard to press.
#
#   :run    PARSE + RUN. For pages that TEACH -- their examples are meant to be
#           copied and to work, so a block that cannot run is a defect unless it
#           says why.
#   :parse  PARSE only. For the reference, whose blocks are mostly API SHAPES
#           (`webclient.get(url [, options])`, a record's field list) rather
#           than programs. Running them would need ~60 exemption markers to say
#           nothing useful; parsing them catches every defect that has actually
#           occurred here -- `#` comments, an invented `try` keyword, a retired
#           modifier spelling -- at a cost of one marker per real fragment.
DOCS="docs/tutorial.md:run README.md:run docs/reference.md:parse docs/webclient_design.md:parse"

failures=0
checks=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

grand_total=0

for entry in $DOCS; do
DOC="${entry%%:*}"; MODE="${entry##*:}"
printf '\n== %s (%s) ==\n' "$DOC" "$MODE"
rm -rf "$work"; mkdir -p "$work"

# Split the page into one file per fenced gbasic block, plus a manifest naming
# each block's line number and whichever marker precedes it. Done in python
# because the alternative is a markdown parser in awk.
python3 - "$DOC" "$work" <<'PY'
import re, sys, os
doc, work = sys.argv[1], sys.argv[2]
src = open(doc).read()
lines = src.splitlines()
manifest = []
for i, m in enumerate(re.finditer(r'```basic\n(.*?)```', src, re.S)):
    line = src[:m.start()].count('\n') + 1          # 1-based line of the fence
    # the marker, if any, is the nearest non-blank line above the fence
    marker = ""
    j = line - 2
    while j >= 0 and lines[j].strip() == "":
        j -= 1
    if j >= 0:
        t = lines[j].strip()
        if t.startswith("<!--fragment:") and t.endswith("-->"):
            marker = "fragment"
        elif t == "<!--needs-context-->":
            marker = "needs-context"
    path = os.path.join(work, f"block_{i:03d}.bas")
    open(path, "w").write(m.group(1))
    manifest.append(f"{i:03d}\t{line}\t{marker}")
open(os.path.join(work, "manifest"), "w").write("\n".join(manifest) + "\n")
PY

total=0; parsed=0; ran=0; fragments=0; contextual=0
parse_bad=(); run_bad=(); unearned=()

while IFS=$'\t' read -r idx line marker; do
    total=$((total + 1))
    src="$work/block_$idx.bas"

    if [[ "$marker" == "fragment" ]]; then
        fragments=$((fragments + 1))
        continue
    fi

    # --- PARSE: --ast reaches the parser and stops, so it separates a parse
    # failure from a runtime one without executing anything.
    if ! ./gbasic --ast "$src" >/dev/null 2>"$work/err"; then
        if command grep -qE "parse error|lexer error" "$work/err"; then
            parse_bad+=("$DOC:$line  $(sed 's/^[a-z ]*error at [^ ]*: //' "$work/err" | head -1)")
            continue
        fi
    fi
    parsed=$((parsed + 1))

    if [[ "$MODE" == "parse" ]]; then
        continue
    fi

    # --- RUN. A marked block only has to be shown NOT to run clean, so it gets
    # a short leash: several are servers, which stay alive on purpose and would
    # otherwise burn the full budget each. An unmarked block gets longer,
    # because a doc example that needs more than that is its own problem.
    set +e
    if [[ "$marker" == "needs-context" ]]; then
        timeout 2 ./gbasic "$src" >/dev/null 2>"$work/rerr" </dev/null
    else
        timeout 10 ./gbasic "$src" >/dev/null 2>"$work/rerr" </dev/null
    fi
    rc=$?
    set -e

    if [[ "$marker" == "needs-context" ]]; then
        contextual=$((contextual + 1))
        # The exemption must be EARNED: if it runs clean, the marker is stale.
        if [[ $rc -eq 0 ]]; then
            unearned+=("$DOC:$line  marked needs-context but runs clean")
        fi
        continue
    fi

    if [[ $rc -ne 0 ]]; then
        run_bad+=("$DOC:$line  $(head -1 "$work/rerr" | sed 's/^[a-z ]*error at [^ ]*: //')")
        continue
    fi
    ran=$((ran + 1))
done < "$work/manifest"

printf 'TIER parse\n'
if [[ ${#parse_bad[@]} -eq 0 ]]; then
    pass "every block lexes and parses ($parsed of $total; $fragments declared fragments)"
else
    fail "every block lexes and parses (${#parse_bad[@]} failed)"
    printf '    %s\n' "${parse_bad[@]}"
fi

if [[ "$MODE" == "parse" ]]; then
    printf '  -- %d blocks parsed; %d declared fragments (parse-only page)\n' "$parsed" "$fragments"
    grand_total=$((grand_total + total))
    continue
fi

printf 'TIER run\n'
if [[ ${#run_bad[@]} -eq 0 ]]; then
    pass "every self-contained block runs to exit 0 ($ran of $total)"
else
    fail "every self-contained block runs to exit 0 (${#run_bad[@]} failed)"
    printf '    %s\n' "${run_bad[@]}"
    printf '    (if the block genuinely needs surrounding context, mark it\n'
    printf '     <!--needs-context--> on the line above its fence)\n'
fi

printf 'TIER markers\n'
if [[ ${#unearned[@]} -eq 0 ]]; then
    pass "every exemption is earned ($contextual contextual, $fragments fragments)"
else
    fail "every exemption is earned (${#unearned[@]} stale)"
    printf '    %s\n' "${unearned[@]}"
    printf '    (remove the marker: the block runs on its own now)\n'
fi

printf '  -- %d blocks: %d run, %d contextual, %d fragments\n' \
    "$total" "$ran" "$contextual" "$fragments"
grand_total=$((grand_total + total))
done

# A page that stopped containing code would otherwise pass by checking nothing.
if [[ $grand_total -lt 200 ]]; then
    printf 'FAIL coverage floor: only %d gbasic blocks found across the pages\n' "$grand_total"
    exit 1
fi

if [[ $failures -gt 0 ]]; then
    printf '\nFAIL tests/run_doc_examples.sh (%d of %d checks failed)\n' "$failures" "$checks"
    exit 1
fi

printf '\nPASS tests/run_doc_examples.sh (%d gbasic blocks across %d pages)\n' \
    "$grand_total" "$(echo $DOCS | wc -w)"

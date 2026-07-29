#!/usr/bin/env bash
# Executable-docs gate (PLAN.md Phase D3, deliverable 4).
#
# The AI cookbook (docs/ai/COOKBOOK.md) promises that every idiom points at a
# real, suite-verified file. This gate enforces that promise: every file the
# cookbook references must (a) exist and (b) be wired into some test runner —
# either named in a tests/*.sh case list, or covered by a directory glob a runner
# uses (e.g. run_gui_parse.sh globs examples/gi/*.bas). It does not re-run the
# programs; the suites already do that.
set -u

cd "$(dirname "$0")/.."

COOK=docs/ai/COOKBOOK.md
if [ ! -f "$COOK" ]; then
    echo "FAIL run_docs_gate: $COOK not found"
    exit 1
fi

# Runners to search, excluding this gate itself.
runners=$(ls tests/*.sh tests/lsp/*.sh 2>/dev/null | grep -v 'run_docs_gate.sh')

refs=$(grep -oE '(examples|tests)/[A-Za-z0-9_./-]+\.(bas|gb)' "$COOK" | sort -u)
if [ -z "$refs" ]; then
    echo "FAIL run_docs_gate: no file references found in $COOK"
    exit 1
fi

status=0
for f in $refs; do
    if [ ! -f "$f" ]; then
        echo "FAIL missing        $f"
        status=1
        continue
    fi
    base=$(basename "$f")
    stem="${base%.*}"
    ext="${f##*.}"
    glob="$(dirname "$f")/*.$ext"
    if grep -qF "$stem" $runners 2>/dev/null || grep -qF "$glob" $runners 2>/dev/null; then
        echo "PASS wired          $f"
    else
        echo "FAIL not wired      $f"
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo "run_docs_gate: some cookbook references are missing or unwired"
fi

# --- Performance claims must be able to expire ---------------------------------
#
# UNLEARN.md told readers that `arr[i]` and `append` were O(n^2) for six weeks
# after a copy-on-write package made them linear. Nothing failed. The claim was
# then repeated into a phase report without measurement and scoped a whole phase
# against a problem that no longer existed.
#
# The rule that prevents a repeat: every performance claim must cite a RUNNER,
# and that runner must assert the claim in its own direction. A runner is
# required rather than a `.bas` golden because a golden asserts VALUES, and no
# arrangement of values can tell you what something costs — only a runner times
# anything. So:
#
#   * a claim that something IS slow is carried by a negative control, which
#     fails when the thing gets fixed (run_arridx.sh requires repeated string
#     concatenation to exceed the quadratic gate);
#   * a claim that something is NOT slow is carried by a shape tier, which fails
#     when it regresses (run_arridx.sh, run_stridx.sh).
#
# Either way the claim has an expiry: change the behaviour and a test goes red,
# so the doc cannot quietly become a lie.
#
# TWO LIMITS, STATED SO NOBODY MISTAKES THIS FOR MORE THAN IT IS:
#
#   1. It checks the CITATION, not the semantics. It cannot read a runner and
#      decide whether its assertions really match the prose. What it guarantees
#      is that every claim points at a live runner, so there is always something
#      that can go red and somewhere to look when it does.
#   2. In COOKBOOK.md it finds claims by KEYWORD, and prose can always make a
#      cost claim without using one. That is not hypothetical: the bullet
#      "remember `append` copies" was stale for months, sat two lines from a
#      cited one, and tripped no trigger until it was found by reading. The word
#      list below was widened to catch that shape; it will not catch every shape.
#
# The UNLEARN.md half has neither weakness, because it checks every bullet in the
# section regardless of wording. Prefer putting a performance claim there.
perf_status=0

# Emit one line per markdown bullet (continuation lines folded in).
fold_bullets() {
    awk '/^- /{if(b!="")print b; b=$0; next}
         {if(b!="")b=b" "$0}
         END{if(b!="")print b}'
}

check_citation() { # label bullet-text
    local label=$1 bullet=$2 runner
    runner=$(printf '%s\n' "$bullet" | grep -oE 'tests/run_[A-Za-z0-9_]+\.sh' | head -1)
    if [ -z "$runner" ]; then
        printf 'FAIL uncited perf   %s\n' "$label"
        printf '     a performance claim must cite a tests/run_*.sh that asserts it,\n'
        printf '     so that changing the behaviour makes a test fail rather than the\n'
        printf '     documentation quietly become wrong.\n'
        perf_status=1
        return
    fi
    if [ ! -f "$runner" ]; then
        printf 'FAIL missing runner %s -> %s\n' "$label" "$runner"
        perf_status=1
        return
    fi
    printf 'PASS perf cited     %-58s -> %s\n' "$label" "$runner"
}

# UNLEARN.md: every bullet in the performance-traps section.
UNLEARN=docs/ai/UNLEARN.md
if [ ! -f "$UNLEARN" ]; then
    echo "FAIL run_docs_gate: $UNLEARN not found"
    exit 1
fi
perf_section=$(awk '/^## Performance traps/{f=1;next} f && (/^---/ || /^## /){exit} f' "$UNLEARN")
if [ -z "$(printf '%s' "$perf_section" | tr -d '[:space:]')" ]; then
    echo "FAIL run_docs_gate: no '## Performance traps' section in $UNLEARN"
    echo "     If the section was renamed, update this gate with it -- silently"
    echo "     checking nothing is exactly the failure this gate exists to stop."
    exit 1
fi
while IFS= read -r bullet; do
    [ -n "$bullet" ] || continue
    label="UNLEARN: $(printf '%s' "$bullet" | sed -e 's/^- //' -e 's/\*\*//g' | cut -c1-52)"
    check_citation "$label" "$bullet"
done <<EOF
$(printf '%s\n' "$perf_section" | fold_bullets)
EOF

# COOKBOOK.md: any bullet that makes a claim about cost. The trigger words are
# deliberately few and blunt; a bullet that trips one and is not really a
# performance claim should be reworded rather than exempted, because an
# exemption list is the thing that rots.
while IFS= read -r bullet; do
    [ -n "$bullet" ] || continue
    printf '%s\n' "$bullet" | grep -qE 'O\(n²\)|O\(n\^2\)|O\(1\)|quadratic|superlinear|linear|amortized|copies' || continue
    label="COOKBOOK: $(printf '%s' "$bullet" | sed -e 's/^- //' -e 's/\*\*//g' | cut -c1-51)"
    check_citation "$label" "$bullet"
done <<EOF
$(fold_bullets <"$COOK")
EOF

if [ "$perf_status" -ne 0 ]; then
    echo "run_docs_gate: a performance claim has no test that can prove it stale"
    status=1
fi

exit "$status"

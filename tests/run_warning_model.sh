#!/usr/bin/env bash
# PLAT-WARN — the warning channel (docs/warning_model_design.md).
#
# The point of this feature is not any single warning: it is that SUPPRESSION
# makes an aggressive warning affordable and ESCALATION makes it enforceable.
# Every warning gBASIC shipped before this had to be near-zero-false-positive,
# because a program had no way to say "I meant that here" -- which is exactly
# why the worst traps (a discarded return, a literal regex pattern, a blind
# shadow) stayed silent.
#
# Two tiers carry the design's load-bearing claims:
#
#   no_rule2      PLAT-ERR's rule 2 (a pending error re-raises at frame exit)
#                 must NOT leak to warnings, or every unchecked warning becomes
#                 an error -- the one thing a warning must never do.
#   soft_name     `warning` is resolved AFTER the environment walk, so a
#                 variable shadows it and `r.warning` still parses. That single
#                 placement is what lets the feature exist with ZERO reserved
#                 words added.
#
# Written from the design before the implementation existed.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fail() { printf 'FAIL %s\n' "$1"; exit 1; }

positive=(
    modes
    read_claims
    snapshot
    no_rule2
    independent
    escalate
    dynamic_scope
    soft_name
    raise_warning
    unused_result
)

for name in "${positive[@]}"; do
    ./gbasic "tests/warning_model/$name.bas" >"$scratch/got" 2>"$scratch/err" \
        || fail "$name (exited nonzero: $(cat "$scratch/err"))"
    diff -u "tests/warning_model/$name.out" "$scratch/got" \
        || fail "$name (stdout diverged)"
    printf 'PASS %s\n' "$name"
done

# --- `print` is the default and actually reaches stderr --------------------
# Asserted separately from stdout because the whole point of the default mode
# is the side channel, and a golden on stdout cannot see it.
./gbasic tests/warning_model/modes.bas >/dev/null 2>"$scratch/err" \
    || fail "modes (stderr tier: exited nonzero)"
grep -q "warning:" "$scratch/err" \
    || fail "modes (the default mode printed nothing to stderr)"
[ "$(grep -c 'warning:' "$scratch/err")" = "1" ] \
    || fail "modes (expected exactly ONE warning on stderr -- ignore and goto-next must not print; got $(grep -c 'warning:' "$scratch/err"))"
printf 'PASS modes_stderr (print reaches stderr; ignore and goto-next do not)\n'

# --- the negatives ---------------------------------------------------------
neg() { # file expected-fragment
    if ./gbasic "tests/warning_model/$1" >/dev/null 2>"$scratch/err"; then
        fail "$1 (expected nonzero exit)"
    fi
    grep -qF "$2" "$scratch/err" \
        || fail "$1 (missing: $2; got: $(cat "$scratch/err"))"
    printf 'PASS %s\n' "${1%.bas}"
}

# A warning fires from a statement that SUCCEEDED, so jumping to a label would
# mean leaving successful code on an advisory signal.
neg neg_label.bas "on warning has no goto-label form"
neg neg_typo.bas "wanring"
neg neg_no_message.bas "warning record requires a message field"

printf 'run_warning_model: %d cases passed\n' "$(( ${#positive[@]} + 4 ))"

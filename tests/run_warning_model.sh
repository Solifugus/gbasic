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
    builtin_shadow_value
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

# --- collision coverage: the warning must know EVERY reachable builtin -------
#
# The `override` warning is the ONLY thing standing between a library author
# and a silent shadow: an unqualified call to a library function whose name
# matches a builtin reaches the BUILTIN, and the failure that follows carries
# the builtin's own message, naming neither the library nor the collision.
# A real session hit this with `audit.record` and was saved by the warning.
#
# It was consulting the WRONG LIST. builtins.c holds two -- the 166 registered
# names, and `dispatch_only` for the file/directory families (`exists`, `read`,
# `write`, `bytes`, `lines`, `chars`, `lock`, `unlock`, `list`, `files`,
# `folders`) that eval.c dispatches at top level without registering. Those
# eleven are every bit as reachable, and shadowing one warned NOTHING.
#
# So this tier does not check a list; it asks the interpreter which names it
# considers builtins (`has_builtin`) and requires each to be un-shadowable in
# silence. A twelfth entry added to either list is covered the day it lands,
# which is the property the two-list arrangement could not offer.
probe_dir="$scratch/collide"
mkdir -p "$probe_dir"
silent=0
checked=0
for name in $(grep -o '"[a-z_][a-z0-9_]*"' src/builtins.c | tr -d '"' | sort -u); do
    printf 'print(has_builtin("%s"))\n' "$name" > "$probe_dir/probe.bas"
    probe_says="$(./gbasic "$probe_dir/probe.bas" 2>/dev/null)" || true
    [ "$probe_says" = "true" ] || continue
    checked=$((checked + 1))
    printf 'library shadow\n    function %s(a)\n        return 1\n    end function\nend library\n' \
        "$name" > "$probe_dir/shadow.bas"
    printf 'load shadow\nprint "loaded"\n' > "$probe_dir/use.bas"
    # `|| true`: several of these deliberately exit nonzero (a keyword name is
    # a parse error), and under errexit the capture alone would end the suite.
    out="$(./gbasic "$probe_dir/use.bas" 2>&1)" || true
    # Either outcome is safe: a warning, or a parse refusal because the name is
    # a KEYWORD and cannot be a function name at all (`watchers`). What must
    # never happen is a clean load, which means a silent shadow.
    case "$out" in
        *"same name as a built-in"*) ;;
        *"syntax error"*|*"could not parse library file"*) ;;
        *) printf "  SILENT SHADOW: %s\n" "$name"; silent=$((silent + 1)) ;;
    esac
done
if [ "$silent" -ne 0 ]; then
    fail "collision coverage ($silent of $checked builtin names shadow silently)"
fi
printf 'PASS collision_coverage (%d builtin names, none shadows silently)\n' "$checked"

printf 'run_warning_model: %d cases passed\n' "$(( ${#positive[@]} + 5 ))"

# --- local shadows a LIBRARY function (Transward report, item 5) ------------
#
# Two defects in one scenario, and the first is a BEHAVIOUR bug rather than a
# missing warning: importing a library function whose name matched an existing
# local RETURNED WITHOUT REGISTERING IT, so `lib.name(...)` failed with
# "invalid function call". The qualifier is precisely what one reaches for when
# a name collides, and it did not escape the collision -- while the diagnostic
# pointed at the CALL, sending the reader to look inside a library for a
# function that was there all along.
#
# The second is the missing warning: library-versus-builtin has warned for a
# long time; local-versus-library is the same shape one level over.
out="$(GBASIC_PATH=stdlib ./gbasic tests/warning_model/local_shadows_library.bas 2>&1)"

# THE BEHAVIOUR, which matters more than the warning: unqualified reaches the
# LOCAL (unchanged precedence) and qualified reaches the LIBRARY (the fix).
printf '%s' "$out" | grep -qx 'local:1' \
    || { printf 'FAIL local_shadows_library (unqualified must reach the local)\n'; exit 1; }
printf '%s' "$out" | grep -qx 'library:2' \
    || { printf 'FAIL local_shadows_library (QUALIFIED must reach the library)\n'; exit 1; }
printf '%s' "$out" | grep -q "shadows 'start_server' from library 'shadowlib'" \
    || { printf 'FAIL local_shadows_library (no shadow warning)\n'; exit 1; }
printf 'PASS local_shadows_library (qualified call escapes the collision, and it warns)\n'

# --- a top-level `load` never runs -----------------------------------------
#
# `load` is executable and the statements outside a program block are not
# walked. Documented, and still the most confusing way to lose an import: for a
# .bas library the symptom is `invalid function call: lib.name`, which points
# at the call rather than the import.
out="$(GBASIC_PATH=stdlib ./gbasic tests/warning_model/top_level_load.bas 2>&1)"
printf '%s' "$out" | grep -q 'is outside the program block, so it never runs' \
    || { printf 'FAIL top_level_load (no warning)\n'; exit 1; }
printf 'PASS top_level_load (a load outside the program block is named as dead)\n'

# THE CONTROL: a load INSIDE the block must stay silent, or the warning is
# noise on every correctly-written program.
out="$(GBASIC_PATH=stdlib ./gbasic tests/warning_model/local_shadows_library.bas 2>&1)"
if printf '%s' "$out" | grep -q 'never runs'; then
    printf 'FAIL top_level_load control (a load INSIDE the block must not warn)\n'
    exit 1
fi
printf 'PASS top_level_load control (a load inside the block is silent)\n'

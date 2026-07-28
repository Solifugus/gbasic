#!/usr/bin/env bash
# PLAT-GUARD: a tripwire on the set of declarations eval_program pre-registers
# before it runs a program block.
#
# Why this exists
# ---------------
# When a `program` block is present it is the only thing that runs, so the top-level
# statements around it are never walked. eval_program compensates by registering
# certain declaration kinds up front, which is what lets a block call a helper
# written below `end program`.
#
# gBASIC Studio's STU-4B declaration hoisting is defined as *that exact set*: when
# Studio materializes a byte prefix to run one execution section, it appends
# post-target declarations of those kinds and no others, precisely because the
# interpreter is position-blind to those and to nothing else. The coupling is
# deliberate and documented (docs/gbasic_studio_stu4b.md, "What qualifies as
# hoistable"), but it is invisible from either side -- nothing in Studio references
# eval_program, and nothing in eval_program references Studio.
#
# So this test sits with the platform, next to the code it describes, and fails
# loudly if the set moves. It asserts the set two ways, because either alone has a
# blind spot:
#
#   structurally -- reading the marked region of src/eval.c, which catches a kind
#                   being added or removed even when no behavioural test covers it
#   behaviourally -- running a program that reaches each kind from below the block,
#                   which catches the registration being broken while the source
#                   still looks right
#
# Headless, GI-independent, no display. Runs everywhere.
set -u

cd "$(dirname "$0")/.."

status=0

# --- Structural: the marked region of eval_program ------------------------------
#
# The expected set below is a copy of the contract, not its source of truth. If it
# disagrees with the code, that is the signal -- do not reconcile by editing this
# list alone.
EXPECTED_KINDS='AST_STMT_FUNCTION AST_STMT_MODIFIER'
EXPECTED_CALLS='function_register modifier_register register_method_bodies_in'

region=$(awk '/BEGIN PRE-REGISTRATION SET/,/END PRE-REGISTRATION SET/' src/eval.c)

if [ -z "$region" ]; then
    printf 'FAIL plat_guard_markers (the PRE-REGISTRATION SET markers are gone from src/eval.c)\n'
    printf '  STU-4B hoisting rule depends on this region being identifiable.\n'
    printf '  Restore the markers around the pre-registration loop in eval_program.\n'
    exit 1
fi

actual_kinds=$(printf '%s\n' "$region" | grep -oE 'AST_STMT_[A-Z_]+' | sort -u | tr '\n' ' ')
actual_kinds=${actual_kinds% }
actual_calls=$(printf '%s\n' "$region" \
    | grep -oE '\b[a-z_]+_register[a-z_]*\b|\bregister_[a-z_]+\b' | sort -u | tr '\n' ' ')
actual_calls=${actual_calls% }

report_drift() {
    printf 'FAIL plat_guard_prereg_set (%s)\n' "$1"
    printf '  expected: %s\n' "$2"
    printf '  actual:   %s\n' "$3"
    printf '\n'
    printf '  The set of declarations eval_program pre-registers has changed.\n'
    printf '  gBASIC STUDIO STU-4B DECLARATION HOISTING MUST MOVE WITH IT.\n'
    printf '\n'
    printf '  studio_session._hoistable_kind() in stdlib/studio_session.bas decides\n'
    printf '  which post-target declarations are appended to a materialized prefix,\n'
    printf '  and it is defined AS this set. Adding a kind here without updating it\n'
    printf '  leaves hoisting incomplete (Studio refuses to hoist something the\n'
    printf '  runtime now registers); removing one leaves it unsound (Studio hoists\n'
    printf '  something the runtime no longer registers, manufacturing behaviour the\n'
    printf '  document does not have).\n'
    printf '\n'
    printf '  Fix the hoisting rule and docs/gbasic_studio_stu4b.md first, then\n'
    printf '  update the expected set in this file.\n'
    status=1
}

if [ "$actual_kinds" = "$EXPECTED_KINDS" ]; then
    printf 'PASS plat_guard_prereg_kinds (%s)\n' "$actual_kinds"
else
    report_drift "statement kinds" "$EXPECTED_KINDS" "$actual_kinds"
fi

if [ "$actual_calls" = "$EXPECTED_CALLS" ]; then
    printf 'PASS plat_guard_prereg_calls (%s)\n' "$actual_calls"
else
    report_drift "registration calls" "$EXPECTED_CALLS" "$actual_calls"
fi

# Only NON-attached functions are registered by the loop. That guard is part of the
# contract: a dotted definition is handled by register_method_bodies_in instead, and
# STU-4B treats attached definitions as inert on the strength of it.
if printf '%s\n' "$region" | grep -q '!stmt->as.function.object'; then
    printf 'PASS plat_guard_prereg_guard (only non-attached functions register here)\n'
else
    printf 'FAIL plat_guard_prereg_guard (the non-attached-function guard is gone)\n'
    printf '  STU-4B treats attached (dotted) definitions as inert because of it.\n'
    status=1
fi

# --- Behavioural: each kind reached from below the block ------------------------
if ! make >/dev/null 2>&1; then
    printf 'FAIL run_pre_registration: build failed\n'
    exit 1
fi

expected_out='function=42
modifier=QUIET
library=helper-tag
function-value=10
top-level-ran=false'

actual_out=$(timeout 60 ./gbasic tests/native_platform/plat_guard_prereg_child.bas 2>&1 </dev/null)
if [ "$actual_out" = "$expected_out" ]; then
    printf 'PASS plat_guard_prereg_behaviour (each kind reachable from below the block)\n'
else
    printf 'FAIL plat_guard_prereg_behaviour\n'
    printf '  A program block could not reach a declaration written below it, or a\n'
    printf '  top-level statement below the block executed when it must not.\n'
    printf '  STU-4B declaration hoisting rests on both halves of that.\n'
    diff -u <(printf '%s\n' "$expected_out") <(printf '%s\n' "$actual_out") || true
    status=1
fi

exit "$status"

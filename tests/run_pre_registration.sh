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
# deliberate and documented (gbasic-studio's docs/gbasic_studio_stu4b.md, "What qualifies as
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
# PLAT-WEB-5 added AST_STMT_SERVER: a server declaration binds its name to inert
# data and registers hidden handler functions, position-blind like a function.
# studio_session._hoistable_kind() gained "server" in the same change.
#
# AST_STMT_USE joined it when `load` became a declaration. Before that the parent
# and the actor child ran SEPARATE registration loops over DIFFERENT sets, and
# their blind spots were exactly complementary -- a top-level `load` never ran in
# the parent (which warned) and a `load` inside `program` was never seen by the
# child (which said nothing and hung). AST_STMT_PROGRAM is in the set because the
# pass reaches into the block's DIRECT children for those loads; it is not itself
# a hoisted declaration.
EXPECTED_KINDS='AST_STMT_FUNCTION AST_STMT_MODIFIER AST_STMT_PROGRAM AST_STMT_SERVER AST_STMT_USE'
EXPECTED_CALLS='function_register library_import modifier_register register_hoistable_declarations register_method_bodies_in server_register'

# Comment lines are stripped: the set is a fact about CODE, and a comment that
# happens to name `function_register_def` should not enter the contract.
region=$(awk '/BEGIN PRE-REGISTRATION SET/,/END PRE-REGISTRATION SET/' src/eval.c \
         | grep -vE '^[[:space:]]*(\*|/\*|//)')

if [ -z "$region" ]; then
    printf 'FAIL plat_guard_markers (the PRE-REGISTRATION SET markers are gone from src/eval.c)\n'
    printf '  STU-4B hoisting rule depends on this region being identifiable.\n'
    printf '  Restore the markers around the pre-registration loop in eval_program.\n'
    exit 1
fi

actual_kinds=$(printf '%s\n' "$region" | grep -oE 'AST_STMT_[A-Z_]+' | sort -u | tr '\n' ' ')
actual_kinds=${actual_kinds% }
actual_calls=$(printf '%s\n' "$region" \
    | grep -oE '\b[a-z_]+_register[a-z_]*\b|\bregister_[a-z_]+\b|\blibrary_import\b' \
    | sort -u | tr '\n' ' ')
actual_calls=${actual_calls% }

report_drift() {
    printf 'FAIL plat_guard_prereg_set (%s)\n' "$1"
    printf '  expected: %s\n' "$2"
    printf '  actual:   %s\n' "$3"
    printf '\n'
    printf '  The set of declarations eval_program pre-registers has changed.\n'
    printf '  gBASIC STUDIO STU-4B DECLARATION HOISTING MUST MOVE WITH IT.\n'
    printf '\n'
    printf '  studio_session._hoistable_kind() in the SEPARATE gbasic-studio project\n'
    printf '  which post-target declarations are appended to a materialized prefix,\n'
    printf '  and it is defined AS this set. Adding a kind here without updating it\n'
    printf '  leaves hoisting incomplete (Studio refuses to hoist something the\n'
    printf '  runtime now registers); removing one leaves it unsound (Studio hoists\n'
    printf '  something the runtime no longer registers, manufacturing behaviour the\n'
    printf '  document does not have).\n'
    printf '\n'
    printf '  (lib/studio_session.bas) mirrors this set. Fix it there first, then\n'
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

# --- Shared: the parent and the actor child must run THE SAME pass --------------
#
# THIS IS THE TIER THE `load` DEFECT WOULD HAVE FAILED. eval_program and
# eval_run_actor each had their own registration loop over its own set, and
# nothing said they were meant to agree. They did not: a top-level `load` never
# ran in the parent (which warned about it) and a `load` inside `program` was
# never seen by the child (which said nothing). Whichever position an author
# chose, one of the two processes was missing the import -- and following the
# parent's advice made the child die on its first qualified call while the parent
# waited in receive(), so the symptom was a hang that pointed nowhere near the
# `load`.
#
# Asserted structurally rather than behaviourally, because the property is
# "there is only one pass" and no single program can demonstrate that: a
# behavioural test proves the two agree about the case it exercises, which is
# exactly what a second loop would also do until the day it drifted.
if grep -q 'register_hoistable_declarations(program);' src/eval.c \
   && [ "$(grep -c 'register_hoistable_declarations(program);' src/eval.c)" -ge 2 ]; then
    printf 'PASS plat_guard_prereg_shared (parent and actor child call one pass)\n'
else
    printf 'FAIL plat_guard_prereg_shared (the parent and the actor child no longer share one pass)\n'
    printf '  eval_program and eval_run_actor must BOTH call\n'
    printf '  register_hoistable_declarations. When they had separate loops their\n'
    printf '  blind spots were complementary and a `load` was invisible to one of\n'
    printf '  them whichever position it was written in.\n'
    status=1
fi

# And the child must not have grown its own loop back beside the shared call.
actor_region=$(awk '/^int eval_run_actor\(/,/^}/' src/eval.c)
if printf '%s\n' "$actor_region" | grep -qE '\b(function_register|modifier_register|library_import)\b'; then
    printf 'FAIL plat_guard_prereg_actor_loop (eval_run_actor registers declarations itself)\n'
    printf '  It must delegate to register_hoistable_declarations and nothing else,\n'
    printf '  or the two passes can disagree again.\n'
    status=1
else
    printf 'PASS plat_guard_prereg_actor_loop (the child registers nothing on its own)\n'
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
server=late/1
server-handler=hoisted
top-level-ran=false'

# GBASIC_PATH: registering the fixture's server block imports `web` (the block
# implies its library), and that must resolve against THIS TREE's stdlib, not
# whatever happens to be installed.
actual_out=$(GBASIC_PATH=stdlib timeout 60 ./gbasic tests/native_platform/plat_guard_prereg_child.bas 2>&1 </dev/null)
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

# --- Behavioural: a spawned actor reaches a library from EITHER position -------
#
# Bounded, because the failure this guards is a HANG rather than a wrong answer:
# the worker dies on its first qualified call and the parent waits in receive()
# for as long as anything lets it. Without a bound the suite would sit there
# reporting nothing until run_all's cap, which is the quiet way a gate stops
# being a gate. -k because this interpreter installs a SIGTERM handler.
expected_actor='child=top/block
parent-top=top
parent-block=block'

actual_actor=$(timeout -k 5 60 ./gbasic tests/native_platform/plat_guard_prereg_actor.bas 2>&1 </dev/null)
if [ "$actual_actor" = "$expected_actor" ]; then
    printf 'PASS plat_guard_prereg_actor (a child reaches a library loaded either side of the block)\n'
else
    printf 'FAIL plat_guard_prereg_actor\n'
    printf '  A spawned actor could not reach a library, or the parent could not.\n'
    printf '  The child never enters the program block, so both positions must be\n'
    printf '  registered by the shared pass; a timeout here is the old hang.\n'
    diff -u <(printf '%s\n' "$expected_actor") <(printf '%s\n' "$actual_actor") || true
    status=1
fi

exit "$status"

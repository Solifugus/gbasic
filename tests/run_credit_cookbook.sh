#!/usr/bin/env bash
# The credit cookbook (docs/credit_cookbook.md) -- a tutorial that cannot lie.
# RUN / CODE / OUTPUT / COVER, all in tests/cookbook_harness.sh.
#
# Never skips: `credit` is pure gBASIC over `lending`, which is pure gBASIC
# over core `money`.
#
# EVERY DEFECT THIS LIBRARY EXISTS TO PREVENT IS AN ORDINARY-LOOKING
# PERCENTAGE, so the recipes are written to make the wrong answer VISIBLE
# beside the right one rather than merely absent. Recipe 4 prints both
# denominators -- 100% "stayed current" counting only the loans still visible,
# 75% counting the whole starting bucket -- because the first is not bad
# arithmetic, it is the answer to a different question, and nothing on a
# report distinguishes them. Recipe 5's vintage table prints DASHES for ages a
# young cohort has not lived through, since a 0% there reads as "no losses"
# where the truth is "no data".
COOKBOOK=credit
RECIPE_GLOB='examples/credit_cookbook/*.bas'

. "$(dirname "$0")/cookbook_harness.sh"

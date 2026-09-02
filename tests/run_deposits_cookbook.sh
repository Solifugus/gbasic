#!/usr/bin/env bash
# The deposits cookbook (docs/deposits_cookbook.md) -- a tutorial that cannot
# lie. RUN / CODE / OUTPUT / COVER, all in tests/cookbook_harness.sh.
#
# Never skips: `deposits` is pure gBASIC over core `money`.
#
# The page carries one thing worth naming, because it is the opposite of what
# a reader expects a test suite to assert. Recipe 2 shows `daily` and
# `minimum` giving 28.77 and 4.11 on identical activity -- and then shows
# `daily` and `average_daily` giving THE SAME NUMBER, asserted as equal and
# explained: simple interest is linear in the balance, so the mean balance
# earns exactly what each day's balance earns, and the two part company only
# under tiers. Claiming a difference there would be claiming something false.
COOKBOOK=deposits
RECIPE_GLOB='examples/deposits_cookbook/*.bas'

. "$(dirname "$0")/cookbook_harness.sh"

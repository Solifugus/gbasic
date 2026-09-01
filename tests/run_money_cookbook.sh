#!/usr/bin/env bash
# The money cookbook (docs/money_cookbook.md) -- a tutorial that cannot lie.
#
# A cookbook's failure mode is not being wrong when written, it is being wrong
# six months later while still reading as authoritative. This suite removes the
# gap between the page and the product in both directions:
#
#   RUN     -- every recipe in examples/money_cookbook/ executes, and its stdout
#              must match its committed .out byte for byte. So the page's output
#              is not a transcript somebody pasted; it is a golden.
#   CODE    -- every ```basic block on the page must equal the .bas file it came
#              from, byte for byte. Editing a recipe without re-running
#              tools/sync_money_cookbook.sh fails here.
#   OUTPUT  -- every output block on the page must equal that recipe's .out. So
#              a behaviour change that legitimately moves a golden ALSO moves
#              the page, and the two cannot drift apart silently.
#   COVER   -- every recipe file must appear on the page, and every marker on
#              the page must have a file. Adding recipe 13 and forgetting to
#              document it is a failure, not a silent omission.
#
# Never skips: money is core and `finance` is pure gBASIC, so no optional
# native dependency is involved.
#
# THIS PAGE EARNED ITS HARNESS IMMEDIATELY. Writing recipe 7 -- an ordinary
# amortization schedule -- surfaced a silent defect in `money * scalar` that
# the unit tests had missed: a scalar whose shortest decimal needs 19
# fractional places was treated as negligible and returned 0.00. Every payment
# in the schedule came out zero. The unit fixtures all used short scalars
# (2, 3, 1.08, 0.5); only realistic arithmetic produced a long one.
COOKBOOK=money
RECIPE_GLOB='examples/money_cookbook/*.bas'

. "$(dirname "$0")/cookbook_harness.sh"

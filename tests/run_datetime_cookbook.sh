#!/usr/bin/env bash
# The xlsx cookbook (docs/datetime_cookbook.md) -- a tutorial that cannot lie.
#
# A cookbook's failure mode is not being wrong when written, it is being wrong
# six months later while still reading as authoritative. This suite removes the
# gap between the page and the product in both directions:
#
#   RUN     -- every recipe in examples/datetime_cookbook/ executes, and its stdout
#              must match its committed .out byte for byte. So the page's output
#              is not a transcript somebody pasted; it is a golden.
#   CODE    -- every ```basic block on the page must equal the .bas file it came
#              from, byte for byte. Editing a recipe without re-running
#              tools/sync_datetime_cookbook.sh fails here.
#   OUTPUT  -- every output block on the page must equal that recipe's .out. So
#              a behaviour change that legitimately moves a golden ALSO moves
#              the page, and the two cannot drift apart silently.
#   COVER   -- every recipe file must appear on the page, and every marker on
#              the page must have a file. Adding recipe 13 and forgetting to
#              document it is a failure, not a silent omission.
#
# Never skips: everything here is core gBASIC plus the pure-gBASIC dates and
# schedule libraries -- no optional native dependency is involved.
COOKBOOK=datetime
RECIPE_GLOB='examples/datetime_cookbook/*.bas'

. "$(dirname "$0")/cookbook_harness.sh"

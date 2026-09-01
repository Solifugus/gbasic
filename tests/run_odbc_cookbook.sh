#!/usr/bin/env bash
# The ODBC cookbook (docs/odbc_cookbook.md) -- a tutorial that cannot lie.
#
# A cookbook's failure mode is not being wrong when written, it is being wrong
# six months later while still reading as authoritative. This suite removes the
# gap between the page and the product in both directions:
#
#   RUN     -- every recipe in examples/odbc_cookbook/ executes, and its stdout
#              must match its committed .out byte for byte. So the page's output
#              is not a transcript somebody pasted; it is a golden.
#   CODE    -- every ```basic block on the page must equal the .bas file it came
#              from, byte for byte. Editing a recipe without re-running
#              tools/sync_odbc_cookbook.sh fails here.
#   OUTPUT  -- every output block on the page must equal that recipe's .out. So
#              a behaviour change that legitimately moves a golden ALSO moves
#              the page, and the two cannot drift apart silently.
#   COVER   -- every recipe file must appear on the page, and every marker on
#              the page must have a file. Adding recipe 13 and forgetting to
#              document it is a failure, not a silent omission.
#
# SKIPS CLEANLY in two cases, and they are different: a build without ODBC
# (the module is compiled out), and a build with ODBC but no SQLite3 driver
# installed for the recipes to talk to. The second is the one a bare "does it
# run" check would report as a failure of the cookbook rather than of the
# machine, so it is detected by the driver manager's own refusal text.
COOKBOOK=odbc
RECIPE_GLOB='examples/odbc_cookbook/*.bas'

COOKBOOK_CLEANUP='examples/tmp_cookbook_odbc.db'

# TWO SKIP CLASSES, DELIBERATELY DISTINGUISHED. "not available in this build"
# (handled by the harness) means ODBC was compiled out. This one means ODBC is
# present and the driver manager could not load the driver the recipes use --
# a fact about this machine, not about the cookbook, and a bare "did it run"
# check would report it as a failure of the page.
COOKBOOK_SKIP_PATTERN="Can't open lib|Data source name not found|Driver does not support"
COOKBOOK_SKIP_REASON='no SQLite3 ODBC driver installed'

. "$(dirname "$0")/cookbook_harness.sh"

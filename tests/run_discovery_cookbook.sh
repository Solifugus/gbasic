#!/usr/bin/env bash
# The discovery cookbook (docs/discovery_cookbook.md) -- a tutorial that cannot
# lie, on the shared harness: RUN, CODE, OUTPUT, COVER.
#
# NEVER SKIPS, and that is the point of which recipes were chosen. `discovery`
# needs a database to SCAN one, but the half that answers "where did this number
# come from" -- references, explain, derivations -- is a pure function of SQL
# TEXT, and the annotation half is a pure function of a catalog value. So every
# recipe here runs with no driver, no connection and no server, which is also
# the honest shape of the library: reading SQL and recording what a person knows
# are things you can do to an estate you cannot currently reach.
COOKBOOK=discovery
RECIPE_GLOB='examples/discovery_cookbook/*.bas'

. "$(dirname "$0")/cookbook_harness.sh"

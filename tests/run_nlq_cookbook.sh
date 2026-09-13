#!/usr/bin/env bash
# The nlq cookbook (docs/nlq_cookbook.md) -- a tutorial that cannot lie, on the
# shared harness: RUN, CODE, OUTPUT, COVER.
#
# NEVER SKIPS, AND THAT IS WHAT THE FIRST INCREMENT IS. `nlq` has no model call
# and no database in it: grounding is a pure function of a catalog and a
# question, the prompt is text, and `interpret` reads SQL somebody else
# produced. So every recipe here runs with no network, no driver and no
# connection -- which is also the honest shape of the library, and the reason
# the part that decides WHICH TABLES A QUESTION IS ABOUT could be built and
# measured before any model was involved at all.
COOKBOOK=nlq
RECIPE_GLOB='examples/nlq_cookbook/*.bas'

. "$(dirname "$0")/cookbook_harness.sh"

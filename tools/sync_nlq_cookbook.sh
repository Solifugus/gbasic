#!/usr/bin/env bash
# The nlq cookbook shares the xlsx cookbook's sync harness -- one
# implementation, so the cookbooks cannot drift apart in behaviour.
exec "$(dirname "$0")/sync_xlsx_cookbook.sh" docs/nlq_cookbook.md examples/nlq_cookbook

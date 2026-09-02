#!/usr/bin/env bash
# The deposits cookbook shares the xlsx cookbook's sync harness -- one
# implementation, so the cookbooks cannot drift apart in behaviour.
exec "$(dirname "$0")/sync_xlsx_cookbook.sh" docs/deposits_cookbook.md examples/deposits_cookbook

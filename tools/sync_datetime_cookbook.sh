#!/usr/bin/env bash
# The datetime cookbook shares the xlsx cookbook's sync harness -- one
# implementation, so the two cannot drift apart in behaviour.
exec "$(dirname "$0")/sync_xlsx_cookbook.sh" docs/datetime_cookbook.md examples/datetime_cookbook

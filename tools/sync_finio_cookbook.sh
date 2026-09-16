#!/usr/bin/env bash
# The finio cookbook shares the xlsx cookbook's sync harness -- one
# implementation, so the cookbooks cannot drift apart in behaviour.
exec "$(dirname "$0")/sync_xlsx_cookbook.sh" docs/finio_cookbook.md examples/finio_cookbook

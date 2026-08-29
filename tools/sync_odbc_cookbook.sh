#!/usr/bin/env bash
# The odbc cookbook shares the xlsx cookbook's sync harness -- one
# implementation, so the cookbooks cannot drift apart in behaviour.
exec "$(dirname "$0")/sync_xlsx_cookbook.sh" docs/odbc_cookbook.md examples/odbc_cookbook

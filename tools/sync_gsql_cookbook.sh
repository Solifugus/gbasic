#!/usr/bin/env bash
# The G-SQL cookbook shares the xlsx cookbook's sync harness -- one
# implementation, so the cookbooks cannot drift apart in behaviour.
exec "$(dirname "$0")/sync_xlsx_cookbook.sh" docs/gsql_cookbook.md examples/gsql_cookbook

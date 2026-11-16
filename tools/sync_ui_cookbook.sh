#!/usr/bin/env bash
# The Qt cookbook shares the xlsx cookbook's sync harness -- one
# implementation, so the cookbooks cannot drift apart in behaviour.
exec "$(dirname "$0")/sync_xlsx_cookbook.sh" docs/ui_cookbook.md examples/ui_cookbook

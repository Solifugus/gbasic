#!/usr/bin/env bash
# The finio tutorial shares the xlsx cookbook's sync harness -- one
# implementation, so a tutorial and a cookbook cannot drift apart in behaviour.
exec "$(dirname "$0")/sync_xlsx_cookbook.sh" docs/finio_tutorial.md examples/finio_tutorial
